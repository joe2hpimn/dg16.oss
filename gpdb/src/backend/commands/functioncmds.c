/*-------------------------------------------------------------------------
 *
 * functioncmds.c
 *
 *	  Routines for CREATE and DROP FUNCTION commands and CREATE and DROP
 *	  CAST commands.
 *
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/commands/functioncmds.c,v 1.80 2006/10/06 17:13:58 petere Exp $
 *
 *
 * DESCRIPTION
 *	  These routines take the parse tree and pick out the
 *	  appropriate arguments/flags, and pass the results to the
 *	  corresponding "FooDefine" routines (in src/catalog) that do
 *	  the actual catalog-munging.  These routines also verify permission
 *	  of the user to execute the command.
 *
 * NOTES
 *	  These things must be defined and committed in the following order:
 *		"create function":
 *				input/output, recv/send procedures
 *		"create type":
 *				type
 *		"create operator":
 *				operators
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "catalog/dependency.h"
#include "catalog/catquery.h"
#include "catalog/indexing.h"
#include "catalog/pg_aggregate.h"
#include "catalog/pg_cast.h"
#include "catalog/pg_language.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_proc_callback.h"
#include "catalog/pg_type.h"
#include "commands/defrem.h"
#include "commands/proclang.h"
#include "miscadmin.h"
#include "parser/parse_func.h"
#include "parser/parse_type.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "cdb/cdbvars.h"
#include "cdb/cdbdisp.h"

static void AlterFunctionOwner_internal(cqContext *pcqCtx,
										Relation rel, HeapTuple tup, 
										Oid newOwnerId);
static void CheckForModifySystemFunc(Oid funcOid, List *funcName);

/*
 *	 Examine the RETURNS clause of the CREATE FUNCTION statement
 *	 and return information about it as *prorettype_p and *returnsSet.
 *
 * This is more complex than the average typename lookup because we want to
 * allow a shell type to be used, or even created if the specified return type
 * doesn't exist yet.  (Without this, there's no way to define the I/O procs
 * for a new type.)  But SQL function creation won't cope, so error out if
 * the target language is SQL.	(We do this here, not in the SQL-function
 * validator, so as not to produce a NOTICE and then an ERROR for the same
 * condition.)
 */
static void
compute_return_type(TypeName *returnType, Oid languageOid,
					Oid *prorettype_p, bool *returnsSet_p, Oid shelltypeOid)
{
	Oid			rettype;

	rettype = LookupTypeName(NULL, returnType);

	if (OidIsValid(rettype))
	{
		if (!get_typisdefined(rettype))
		{
			if (languageOid == SQLlanguageId)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
						 errmsg("SQL function cannot return shell type %s",
								TypeNameToString(returnType))));
			else if (Gp_role != GP_ROLE_EXECUTE)
				ereport(NOTICE,
						(errcode(ERRCODE_WRONG_OBJECT_TYPE),
						 errmsg("return type %s is only a shell",
								TypeNameToString(returnType))));
		}
	}
	else
	{
		char	   *typnam = TypeNameToString(returnType);
		Oid			namespaceId;
		AclResult	aclresult;
		char	   *typname;

		/*
		 * Only C-coded functions can be I/O functions.  We enforce this
		 * restriction here mainly to prevent littering the catalogs with
		 * shell types due to simple typos in user-defined function
		 * definitions.
		 */
		if (languageOid != INTERNALlanguageId &&
			languageOid != ClanguageId)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("type \"%s\" does not exist", typnam)));

		/* Otherwise, go ahead and make a shell type */
		if (Gp_role == GP_ROLE_EXECUTE)
		{
			ereport(DEBUG1,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("type \"%s\" is not yet defined", typnam),
				 errdetail("Creating a shell type definition.")));
		}
		else
		{
			ereport(NOTICE,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("type \"%s\" is not yet defined", typnam),
					 errdetail("Creating a shell type definition.")));
		}
		namespaceId = QualifiedNameGetCreationNamespace(returnType->names,
														&typname);
		aclresult = pg_namespace_aclcheck(namespaceId, GetUserId(),
										  ACL_CREATE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, ACL_KIND_NAMESPACE,
						   get_namespace_name(namespaceId));
		rettype = TypeShellMakeWithOid(typname, namespaceId, GetUserId(), shelltypeOid);
		Assert(OidIsValid(rettype));
	}

	*prorettype_p = rettype;
	*returnsSet_p = returnType->setof;
}

/*
 * Interpret the parameter list of the CREATE FUNCTION statement.
 *
 * Results are stored into output parameters.  parameterTypes must always
 * be created, but the other arrays are set to NULL if not needed.
 * requiredResultType is set to InvalidOid if there are no OUT parameters,
 * else it is set to the OID of the implied result type.
 */
static void
examine_parameter_list(List *parameters, Oid languageOid,
					   oidvector **parameterTypes,
					   ArrayType **allParameterTypes,
					   ArrayType **parameterModes,
					   ArrayType **parameterNames,
					   Oid *requiredResultType)
{
	int			parameterCount = list_length(parameters);
	Oid		   *inTypes;
	int			inCount = 0;
	Datum	   *allTypes;
	Datum	   *paramModes;
	Datum	   *paramNames;
	int			outCount = 0;
	int         varCount = 0;
	int         multisetCount = 0;
	bool		have_names = false;
	ListCell   *x;
	int			i;

	/* default results */
	*requiredResultType = InvalidOid;
	*parameterNames		= NULL;
	*allParameterTypes	= NULL;
	*parameterModes		= NULL;

	/* Allocate local memory */
	inTypes = (Oid *) palloc(parameterCount * sizeof(Oid));
	allTypes = (Datum *) palloc(parameterCount * sizeof(Datum));
	paramModes = (Datum *) palloc(parameterCount * sizeof(Datum));
	paramNames = (Datum *) palloc0(parameterCount * sizeof(Datum));

	/* Scan the list and extract data into work arrays */
	i = 0;
	foreach(x, parameters)
	{
		FunctionParameter *fp = (FunctionParameter *) lfirst(x);
		TypeName   *t = fp->argType;
		Oid			toid;

		toid = LookupTypeName(NULL, t);
		if (OidIsValid(toid))
		{
			if (!get_typisdefined(toid))
			{
				/* As above, hard error if language is SQL */
				if (languageOid == SQLlanguageId)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
						   errmsg("SQL function cannot accept shell type %s",
								  TypeNameToString(t))));
				else if (Gp_role != GP_ROLE_EXECUTE)
					ereport(NOTICE,
							(errcode(ERRCODE_WRONG_OBJECT_TYPE),
							 errmsg("argument type %s is only a shell",
									TypeNameToString(t))));
			}
		}
		else
		{
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("type %s does not exist",
							TypeNameToString(t))));
		}

		if (t->setof)
		{
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("functions cannot accept set arguments")));
		}

		/* track input vs output parameters */
		switch (fp->mode)
		{
			/* input only modes */
			case FUNC_PARAM_VARIADIC:	/* GPDB: not yet supported */
			case FUNC_PARAM_IN:
				inTypes[inCount++] = toid;

				/* Keep track of the number of anytable arguments */
				if (toid == ANYTABLEOID)
					multisetCount++;

				/* Other input parameters cannot follow VARIADIC parameter */
				if (fp->mode == FUNC_PARAM_VARIADIC)
					varCount++;
				else if (varCount > 0)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
							 errmsg("VARIADIC parameter must be the last input parameter")));
				break;

			/* output only modes */
			case FUNC_PARAM_OUT:
			case FUNC_PARAM_TABLE:
				if (toid == ANYTABLEOID)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
							 errmsg("functions cannot return \"anytable\" arguments")));

				if (outCount == 0)	/* save first OUT param's type */
					*requiredResultType = toid;
				outCount++;
				break;

			/* input and output */
			case FUNC_PARAM_INOUT:
				if (toid == ANYTABLEOID)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
							 errmsg("functions cannot return \"anytable\" arguments")));

				inTypes[inCount++] = toid;

				if (outCount == 0)	/* save first OUT param's type */
					*requiredResultType = toid;
				outCount++;
				break;

			/* above list must be exhaustive */
			default:
				elog(ERROR, "unrecognized function parameter mode: %c", fp->mode);
				break;
		}

		allTypes[i] = ObjectIdGetDatum(toid);

		paramModes[i] = CharGetDatum(fp->mode);

		if (fp->name && fp->name[0])
		{
			paramNames[i] = CStringGetTextDatum(fp->name);
			have_names = true;
		}

		i++;
	}

	/* Currently only support single multiset input parameters */
	if (multisetCount > 1)
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("functions cannot have multiple \"anytable\" arguments")));
	}

	/* Now construct the proper outputs as needed */
	*parameterTypes = buildoidvector(inTypes, inCount);

	if (outCount > 0 || varCount > 0)
	{
		*allParameterTypes = construct_array(allTypes, parameterCount, OIDOID,
											 sizeof(Oid), true, 'i');
		*parameterModes = construct_array(paramModes, parameterCount, CHAROID,
										  1, true, 'c');
		if (outCount > 1)
			*requiredResultType = RECORDOID;
		/* otherwise we set requiredResultType correctly above */
	}

	if (have_names)
	{
		for (i = 0; i < parameterCount; i++)
		{
			if (paramNames[i] == PointerGetDatum(NULL))
				paramNames[i] = CStringGetTextDatum("");
		}
		*parameterNames = construct_array(paramNames, parameterCount, TEXTOID,
										  -1, false, 'i');
	}

}


/*
 * Recognize one of the options that can be passed to both CREATE
 * FUNCTION and ALTER FUNCTION and return it via one of the out
 * parameters. Returns true if the passed option was recognized. If
 * the out parameter we were going to assign to points to non-NULL,
 * raise a duplicate error.
 */
static bool
compute_common_attribute(DefElem *defel,
						 DefElem **volatility_item,
						 DefElem **strict_item,
						 DefElem **security_item,
						 DefElem **data_access_item)
{
	if (strcmp(defel->defname, "volatility") == 0)
	{
		if (*volatility_item)
			goto duplicate_error;

		*volatility_item = defel;
	}
	else if (strcmp(defel->defname, "strict") == 0)
	{
		if (*strict_item)
			goto duplicate_error;

		*strict_item = defel;
	}
	else if (strcmp(defel->defname, "security") == 0)
	{
		if (*security_item)
			goto duplicate_error;

		*security_item = defel;
	}
	else if (strcmp(defel->defname, "data_access") == 0)
	{
		if (*data_access_item)
			goto duplicate_error;

		*data_access_item = defel;
	}
	else
		return false;

	/* Recognized an option */
	return true;

duplicate_error:
	ereport(ERROR,
			(errcode(ERRCODE_SYNTAX_ERROR),
			 errmsg("conflicting or redundant options")));
	return false;				/* keep compiler quiet */
}

static char
interpret_func_volatility(DefElem *defel)
{
	char	   *str = strVal(defel->arg);

	if (strcmp(str, "immutable") == 0)
		return PROVOLATILE_IMMUTABLE;
	else if (strcmp(str, "stable") == 0)
		return PROVOLATILE_STABLE;
	else if (strcmp(str, "volatile") == 0)
		return PROVOLATILE_VOLATILE;
	else
	{
		elog(ERROR, "invalid volatility \"%s\"", str);
		return 0;				/* keep compiler quiet */
	}
}

static char
interpret_data_access(DefElem *defel)
{
	char *str = strVal(defel->arg);
	char proDataAccess = PRODATAACCESS_NONE;

	if (strcmp(str, "none") == 0)
		proDataAccess = PRODATAACCESS_NONE;
	else if (strcmp(str, "contains") == 0)
		proDataAccess = PRODATAACCESS_CONTAINS;
	else if (strcmp(str, "reads") == 0)
		proDataAccess = PRODATAACCESS_READS;
	else if (strcmp(str, "modifies") == 0)
		proDataAccess = PRODATAACCESS_MODIFIES;
	else
		elog(ERROR, "invalid data access \"%s\"", str);

	return proDataAccess;
}

static char
getDefaultDataAccess(Oid languageOid)
{
	char proDataAccess = PRODATAACCESS_NONE;
	if (languageOid == SQLlanguageId)
		proDataAccess = PRODATAACCESS_CONTAINS;

	return proDataAccess;
}

static void
validate_sql_data_access(char data_access, char volatility, Oid languageOid)
{
	/* IMMUTABLE is not compatible with READS SQL DATA or MODIFIES SQL DATA */
	if (volatility == PROVOLATILE_IMMUTABLE &&
			data_access == PRODATAACCESS_READS)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				errmsg("conflicting options"),
				errhint("IMMUTABLE conflicts with READS SQL DATA.")));

	if (volatility == PROVOLATILE_IMMUTABLE &&
			data_access == PRODATAACCESS_MODIFIES)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("conflicting options"),
				 errhint("IMMUTABLE conflicts with MODIFIES SQL DATA.")));

	/* SQL language function cannot specify NO SQL */
	if (languageOid == SQLlanguageId && data_access == PRODATAACCESS_NONE)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("conflicting options"),
				 errhint("A SQL function cannot specify NO SQL.")));
}

/*
 * Dissect the list of options assembled in gram.y into function
 * attributes.
 */
static void
compute_attributes_sql_style(List *options,
							 List **as,
							 Oid *languageOid,
							 char **languageName,
							 char *volatility_p,
							 bool *strict_p,
							 bool *security_definer,
							 char *data_access)
{
	ListCell   *option;
	DefElem    *as_item = NULL;
	DefElem    *language_item = NULL;
	DefElem    *volatility_item = NULL;
	DefElem    *strict_item = NULL;
	DefElem    *security_item = NULL;
	DefElem    *data_access_item = NULL;

	char	   *language;

	foreach(option, options)
	{
		DefElem    *defel = (DefElem *) lfirst(option);

		if (strcmp(defel->defname, "as") == 0)
		{
			if (as_item)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("conflicting or redundant options")));
			as_item = defel;
		}
		else if (strcmp(defel->defname, "language") == 0)
		{
			if (language_item)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("conflicting or redundant options")));
			language_item = defel;
		}
		else if (compute_common_attribute(defel,
										  &volatility_item,
										  &strict_item,
										  &security_item,
										  &data_access_item))
		{
			/* recognized common option */
			continue;
		}
		else
			elog(ERROR, "option \"%s\" not recognized",
				 defel->defname);
	}

	/* process required items */
	if (as_item)
		*as = (List *) as_item->arg;
	else
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("no function body specified")));
		*as = NIL;				/* keep compiler quiet */
	}

	if (language_item)
		language = strVal(language_item->arg);
	else
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("no language specified")));
		language = NULL;	/* keep compiler quiet */
	}

	/* Convert language name to canonical case */
	*languageName = case_translate_language_name(language);

	/* Look up the language and validate permissions */
	*languageOid = caql_getoid(NULL,
			cql("SELECT * FROM pg_language "
				" WHERE lanname = :1 ",
				CStringGetDatum(*languageName)));

	if (!OidIsValid(*languageOid))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("language \"%s\" does not exist", *languageName),
				 (PLTemplateExists(*languageName) ?
				  errhint("Use CREATE LANGUAGE to load the language into the database.") : 0)));

	/* process optional items */
	if (volatility_item)
		*volatility_p = interpret_func_volatility(volatility_item);
	if (strict_item)
		*strict_p = intVal(strict_item->arg);
	if (security_item)
		*security_definer = intVal(security_item->arg);

	/* If prodataaccess indicator not specified, fill in default. */
	if (data_access_item == NULL)
		*data_access = getDefaultDataAccess(*languageOid);
	else
		*data_access = interpret_data_access(data_access_item);
}


/*-------------
 *	 Interpret the parameters *parameters and return their contents via
 *	 *isStrict_p, *volatility_p and *oid_p.
 *
 *	These parameters supply optional information about a function.
 *	All have defaults if not specified. Parameters:
 *
 *	 * isStrict means the function should not be called when any NULL
 *	   inputs are present; instead a NULL result value should be assumed.
 *
 *	 * volatility tells the optimizer whether the function's result can
 *	   be assumed to be repeatable over multiple evaluations.
 *
 *   * oid (upgrade mode only) specifies that the function should be
 *     created with the user-specified oid.
 *
 *   * describeQualName is the qualified name of a describe callback function
 *     to handle dynamic type resolution.
 *------------
 */
static void
compute_attributes_with_style(List *parameters, 
							  bool *isStrict_p, 
							  char *volatility_p, 
							  Oid* oid_p,
							  List **describeQualName_p)
{
	ListCell   *pl;

	foreach(pl, parameters)
	{
		DefElem    *param = (DefElem *) lfirst(pl);

		if (pg_strcasecmp(param->defname, "isstrict") == 0)
			*isStrict_p = defGetBoolean(param);
		else if (pg_strcasecmp(param->defname, "iscachable") == 0)
		{
			/* obsolete spelling of isImmutable */
			if (defGetBoolean(param))
				*volatility_p = PROVOLATILE_IMMUTABLE;
		}
		else if (pg_strcasecmp(param->defname, "describe") == 0)
		{
			*describeQualName_p = defGetQualifiedName(param);
		}
		else if (gp_upgrade_mode && pg_strcasecmp(param->defname, "OID")==0)
		{
			/* Catalog obj's OID must be less than FirstBootstrapObjectId */
			int64 oid = defGetInt64(param);
			Assert(oid < FirstBootstrapObjectId);
			*oid_p = (Oid) oid;
		}
		else
		{
			ereport(WARNING,
					(errcode(ERRCODE_SYNTAX_ERROR),
					 errmsg("unrecognized function attribute \"%s\" ignored",
							param->defname)));
		}
	}
}


/*
 * For a dynamically linked C language object, the form of the clause is
 *
 *	   AS <object file name> [, <link symbol name> ]
 *
 * In all other cases
 *
 *	   AS <object reference, or sql code>
 */
static void
interpret_AS_clause(Oid languageOid, const char *languageName, List *as,
					char **prosrc_str_p, char **probin_str_p)
{
	Assert(as != NIL);

	if (languageOid == ClanguageId)
	{
		/*
		 * For "C" language, store the file name in probin and, when given,
		 * the link symbol name in prosrc.
		 */
		*probin_str_p = strVal(linitial(as));
		if (list_length(as) == 1)
			*prosrc_str_p = "-";
		else
			*prosrc_str_p = strVal(lsecond(as));
	}
	else
	{
		/* Everything else wants the given string in prosrc. */
		*prosrc_str_p = strVal(linitial(as));
		*probin_str_p = "-";

		if (list_length(as) != 1)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("only one AS item needed for language \"%s\"",
							languageName)));
	}
}


/*
 * Handle functions that try to define a "DESCRIBE" callback.
 */
static Oid
validate_describe_callback(List *describeQualName, 
						   Oid returnTypeOid, 
						   ArrayType *parameterModes)
{
	int					 nargs			  = 1;
	Oid					 inputTypeOids[1] = {INTERNALOID};
	Oid					*actualInputTypeOids;
	Oid					 describeReturnTypeOid;
	Oid					 describeFuncOid;
	bool				 describeReturnsSet;
	bool				 describeIsOrdered;
	bool				 describeIsStrict;
	FuncDetailCode		 fdResult;
	AclResult			 aclresult;
	int					 i;

	if (describeQualName == NIL)
		return InvalidOid;

	/* 
	 * describe callbacks only supported for functions that return either
	 * a pseudotype or a generic record.
	 */
	if (!TypeSupportsDescribe(returnTypeOid))
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("DESCRIBE only supported for functions returning \"record\"")));
	}
	if (parameterModes)
	{
		int   len   = ARR_DIMS(parameterModes)[0];
		char *modes = ARR_DATA_PTR(parameterModes);
		
		Insist(ARR_NDIM(parameterModes) == 1);
		for (i = 0; i < len; i++)
		{
			switch (modes[i])
			{
				case FUNC_PARAM_IN:
				case FUNC_PARAM_VARIADIC:
					break;

				case FUNC_PARAM_INOUT:
				case FUNC_PARAM_OUT:
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
							 errmsg("DESCRIBE is not supported for functions "
									"with OUT parameters")));
					break;

				case FUNC_PARAM_TABLE:
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
							 errmsg("DESCRIBE is not supported for functions "
									"that return TABLE")));				
					break;

				/* above list should be exhaustive */
				default:
					elog(ERROR, "unrecognized function parameter mode: %c", modes[i]);
					break;
			}
		}
	}

	/* Lookup the function in the catalog */
	fdResult = func_get_detail(describeQualName,
							   NIL,   /* argument expressions */
							   nargs, 
							   inputTypeOids,
							   &describeFuncOid,
							   &describeReturnTypeOid, 
							   &describeReturnsSet,
							   &describeIsStrict, 
							   &describeIsOrdered, 
							   &actualInputTypeOids);

	if (fdResult != FUNCDETAIL_NORMAL || !OidIsValid(describeFuncOid))
	{
		/* Should we try to create the function when not found? */
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_FUNCTION),
				 errmsg("function %s does not exist",
						func_signature_string(describeQualName, nargs, inputTypeOids))));
	}
	if (describeReturnTypeOid != INTERNALOID)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("return type of function %s is not \"internal\"",
						func_signature_string(describeQualName, nargs, inputTypeOids))));
	}
	if (describeReturnsSet)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("function %s returns a set",
						func_signature_string(describeQualName, nargs, inputTypeOids))));
	}

	/* Check that the creator has permission to call the function */
	aclresult = pg_proc_aclcheck(describeFuncOid, GetUserId(), ACL_EXECUTE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error(aclresult, ACL_KIND_PROC, get_func_name(describeFuncOid));

	/* Looks reasonable */
	return describeFuncOid;
}


/*
 * CreateFunction
 *	 Execute a CREATE FUNCTION utility statement.
 */
void
CreateFunction(CreateFunctionStmt *stmt)
{
	char	   *probin_str;
	char	   *prosrc_str;
	Oid			prorettype;
	bool		returnsSet;
	char	   *languageName;
	Oid			languageOid;
	Oid			languageValidator;
	char	   *funcname;
	Oid			namespaceId;
	AclResult	aclresult;
	oidvector  *parameterTypes;
	ArrayType  *allParameterTypes;
	ArrayType  *parameterModes;
	ArrayType  *parameterNames;
	Oid			requiredResultType;
	bool		isStrict,
				security;
	char		volatility;
	HeapTuple	languageTuple;
	Form_pg_language languageStruct;
	List	   *as_clause;
	List       *describeQualName = NIL;
	Oid         describeFuncOid  = InvalidOid;
	char		dataAccess;

	/* Convert list of names to a name and namespace */
	namespaceId = QualifiedNameGetCreationNamespace(stmt->funcname,
													&funcname);

	/* Check we have creation rights in target namespace */
	aclresult = pg_namespace_aclcheck(namespaceId, GetUserId(), ACL_CREATE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error(aclresult, ACL_KIND_NAMESPACE,
					   get_namespace_name(namespaceId));

	/* default attributes */
	isStrict = false;
	security = false;
	volatility = PROVOLATILE_VOLATILE;
	dataAccess = PRODATAACCESS_NONE;

	/* override attributes from explicit list */
	compute_attributes_sql_style(stmt->options,
				   &as_clause, &languageOid, &languageName, &volatility,
				   &isStrict, &security, &dataAccess);

	languageTuple = caql_getfirst(NULL,
			cql("SELECT * FROM pg_language "
				"WHERE oid = :1",
				ObjectIdGetDatum(languageOid)));
	/* language should have been found in compute_attributes_sql_style() */
	Assert(HeapTupleIsValid(languageTuple));

	languageStruct = (Form_pg_language) GETSTRUCT(languageTuple);

	if (languageStruct->lanpltrusted)
	{
		/* if trusted language, need USAGE privilege */
		AclResult	aclresult;

		aclresult = pg_language_aclcheck(languageOid, GetUserId(), ACL_USAGE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, ACL_KIND_LANGUAGE,
						   NameStr(languageStruct->lanname));
	}
	else
	{
		/* if untrusted language, must be superuser */
		if (!superuser())
			aclcheck_error(ACLCHECK_NO_PRIV, ACL_KIND_LANGUAGE,
						   NameStr(languageStruct->lanname));
	}

	languageValidator = languageStruct->lanvalidator;

	/*
	 * Check consistency for data access.  Note this comes after the language
	 * tuple lookup, as we need language oid.
	 */
	validate_sql_data_access(dataAccess, volatility, languageOid);

	/*
	 * Convert remaining parameters of CREATE to form wanted by
	 * ProcedureCreate.
	 */
	examine_parameter_list(stmt->parameters, languageOid,
						   &parameterTypes,
						   &allParameterTypes,
						   &parameterModes,
						   &parameterNames,
						   &requiredResultType);

	if (stmt->returnType)
	{
		/* explicit RETURNS clause */
		compute_return_type(stmt->returnType, languageOid,
							&prorettype, &returnsSet, stmt->shelltypeOid);
		if (OidIsValid(requiredResultType) && prorettype != requiredResultType)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("function result type must be %s because of OUT parameters",
							format_type_be(requiredResultType))));
		stmt->shelltypeOid = prorettype;
	}
	else if (OidIsValid(requiredResultType))
	{
		/* default RETURNS clause from OUT parameters */
		prorettype = requiredResultType;
		returnsSet = false;
	}
	else
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("function result type must be specified")));
		/* Alternative possibility: default to RETURNS VOID */
		prorettype = VOIDOID;
		returnsSet = false;
	}

	compute_attributes_with_style(stmt->withClause, &isStrict, &volatility,
								  &stmt->funcOid, &describeQualName);

	interpret_AS_clause(languageOid, languageName, as_clause,
						&prosrc_str, &probin_str);

	if (languageOid == INTERNALlanguageId)
	{
		/*
		 * In PostgreSQL versions before 6.5, the SQL name of the created
		 * function could not be different from the internal name, and
		 * "prosrc" wasn't used.  So there is code out there that does CREATE
		 * FUNCTION xyz AS '' LANGUAGE internal. To preserve some modicum of
		 * backwards compatibility, accept an empty "prosrc" value as meaning
		 * the supplied SQL function name.
		 */
		if (strlen(prosrc_str) == 0)
			prosrc_str = funcname;
	}

	if (languageOid == ClanguageId)
	{
		/* If link symbol is specified as "-", substitute procedure name */
		if (strcmp(prosrc_str, "-") == 0)
			prosrc_str = funcname;
	}
	
	/* double check that we really have a function body */
	if (prosrc_str == NULL)
		prosrc_str = strdup("");

	/* Handle the describe callback, if any */
	if (describeQualName != NIL)
		describeFuncOid = validate_describe_callback(describeQualName,
													 prorettype,
													 parameterModes);

	/*
	 * And now that we have all the parameters, and know we're permitted to do
	 * so, go ahead and create the function.
	 */
	stmt->funcOid = ProcedureCreate(funcname,
					namespaceId,
					stmt->replace,
					returnsSet,
					prorettype,
					languageOid,
					languageValidator,
					describeFuncOid,
					prosrc_str, /* converted to text later */
					probin_str, /* converted to text later */
					false,		/* not an aggregate */
					false,		/* not a window function */
					security,
					isStrict,
					volatility,
					parameterTypes,
					PointerGetDatum(allParameterTypes),
					PointerGetDatum(parameterModes),
					PointerGetDatum(parameterNames),
					dataAccess,
					stmt->funcOid);
					
	if (Gp_role == GP_ROLE_DISPATCH)
	{
		CdbDispatchUtilityStatement((Node *) stmt, "CreateFunction");
	}
}

/*
 * RemoveFunction
 *		Deletes a function.
 */
void
RemoveFunction(RemoveFuncStmt *stmt)
{
	List	   *functionName = stmt->name;
	List	   *argTypes = stmt->args;	/* list of TypeName nodes */
	Oid			funcOid;
	HeapTuple	tup;
	ObjectAddress object;
	cqContext  *pcqCtx;

	/*
	 * Find the function, do permissions and validity checks
	 */
	funcOid = LookupFuncNameTypeNames(functionName, argTypes, stmt->missing_ok);
	if (!OidIsValid(funcOid))
	{
		/* can only get here if stmt->missing_ok */
		ereport(NOTICE,
				(errmsg("function %s(%s) does not exist, skipping",
						NameListToString(functionName),
						TypeNameListToString(argTypes))));
		return;
	}

	pcqCtx = caql_beginscan(
			NULL,
			cql("SELECT * FROM pg_proc "
				" WHERE oid = :1 ",
				ObjectIdGetDatum(funcOid)));

	tup = caql_getnext(pcqCtx);

	if (!HeapTupleIsValid(tup)) /* should not happen */
		elog(ERROR, "cache lookup failed for function %u", funcOid);

	/* Permission check: must own func or its namespace */
	if (!pg_proc_ownercheck(funcOid, GetUserId()) &&
	  !pg_namespace_ownercheck(((Form_pg_proc) GETSTRUCT(tup))->pronamespace,
							   GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_PROC,
					   NameListToString(functionName));

	/* check bootstrap object */
	CheckForModifySystemFunc(funcOid, functionName);

	if (((Form_pg_proc) GETSTRUCT(tup))->proisagg)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is an aggregate function",
						NameListToString(functionName)),
				 errhint("Use DROP AGGREGATE to drop aggregate functions.")));

	if (((Form_pg_proc) GETSTRUCT(tup))->prolang == INTERNALlanguageId)
	{
		/* "Helpful" NOTICE when removing a builtin function ... */
		if (Gp_role != GP_ROLE_EXECUTE)
		ereport(NOTICE,
				(errcode(ERRCODE_WARNING),
				 errmsg("removing built-in function \"%s\"",
						NameListToString(functionName))));
	}

	caql_endscan(pcqCtx);

	/*
	 * Do the deletion
	 */
	object.classId = ProcedureRelationId;
	object.objectId = funcOid;
	object.objectSubId = 0;

	performDeletion(&object, stmt->behavior);
	
	if (Gp_role == GP_ROLE_DISPATCH)
	{
		CdbDispatchUtilityStatement((Node *) stmt, "RemoveFunction");
	}
}

/*
 * Guts of function deletion.
 *
 * Note: this is also used for aggregate deletion, since the OIDs of
 * both functions and aggregates point to pg_proc.
 */
void
RemoveFunctionById(Oid funcOid)
{
	HeapTuple	tup;
	bool		isagg;
	cqContext  *pcqCtx;

	/*
	 * Delete the pg_proc tuple.
	 */

	pcqCtx = caql_beginscan(
			NULL,
			cql("SELECT * FROM pg_proc "
				" WHERE oid = :1 "
				" FOR UPDATE ",
				ObjectIdGetDatum(funcOid)));

	tup = caql_getnext(pcqCtx);

	if (!HeapTupleIsValid(tup)) /* should not happen */
		elog(ERROR, "cache lookup failed for function %u", funcOid);

	isagg = ((Form_pg_proc) GETSTRUCT(tup))->proisagg;

	caql_delete_current(pcqCtx);
	caql_endscan(pcqCtx);

	/* Remove anything in pg_proc_callback for this function */
	deleteProcCallbacks(funcOid);

	/*
	 * If there's a pg_aggregate tuple, delete that too.
	 */
	if (isagg)
	{
		if (0 == caql_getcount(
					NULL,
					cql("DELETE FROM pg_aggregate "
						" WHERE aggfnoid = :1 ",
						ObjectIdGetDatum(funcOid)))) 
		{
			/* should not happen */
			elog(ERROR, 
				 "cache lookup failed for pg_aggregate tuple for function %u",
				 funcOid);
		}
	}
}


/*
 * Rename function
 */
void
RenameFunction(List *name, List *argtypes, const char *newname)
{
	Oid			procOid;
	Oid			namespaceOid;
	HeapTuple	tup;
	Form_pg_proc procForm;
	Relation	rel;
	AclResult	aclresult;
	cqContext	cqc2;
	cqContext	cqc;
	cqContext  *pcqCtx;

	rel = heap_open(ProcedureRelationId, RowExclusiveLock);

	procOid = LookupFuncNameTypeNames(name, argtypes, false);

	pcqCtx = caql_addrel(cqclr(&cqc), rel);

	tup = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_proc "
				" WHERE oid = :1 "
				" FOR UPDATE ",
				ObjectIdGetDatum(procOid)));

	if (!HeapTupleIsValid(tup)) /* should not happen */
		elog(ERROR, "cache lookup failed for function %u", procOid);
	procForm = (Form_pg_proc) GETSTRUCT(tup);

	if (procForm->proisagg)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is an aggregate function",
						NameListToString(name)),
			 errhint("Use ALTER AGGREGATE to rename aggregate functions.")));

	namespaceOid = procForm->pronamespace;

	/* make sure the new name doesn't exist */
	if (caql_getcount(
				caql_addrel(cqclr(&cqc2), rel),
				cql("SELECT COUNT(*) FROM pg_proc "
					" WHERE proname = :1 "
					" AND proargtypes = :2 "
					" AND pronamespace = :3 ",
					CStringGetDatum((char *) newname),
					PointerGetDatum(&procForm->proargtypes),
					ObjectIdGetDatum(namespaceOid))))
	{
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_FUNCTION),
				 errmsg("function %s already exists in schema \"%s\"",
						funcname_signature_string(newname,
												  procForm->pronargs,
											   procForm->proargtypes.values),
						get_namespace_name(namespaceOid))));
	}

	/* must be owner */
	if (!pg_proc_ownercheck(procOid, GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_PROC,
					   NameListToString(name));

	/* must have CREATE privilege on namespace */
	aclresult = pg_namespace_aclcheck(namespaceOid, GetUserId(), ACL_CREATE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error(aclresult, ACL_KIND_NAMESPACE,
					   get_namespace_name(namespaceOid));

	/* check bootstrap object */
	CheckForModifySystemFunc(procOid, name);

	/* rename */
	namestrcpy(&(procForm->proname), newname);
	caql_update_current(pcqCtx, tup); /* implicit update of index as well */

	heap_close(rel, NoLock);
	heap_freetuple(tup);
}

/*
 * Change function owner by name and args
 */
void
AlterFunctionOwner(List *name, List *argtypes, Oid newOwnerId)
{
	Relation	rel;
	Oid			procOid;
	HeapTuple	tup;
	cqContext  *pcqCtx;
	cqContext	cqc;

	rel = heap_open(ProcedureRelationId, RowExclusiveLock);

	procOid = LookupFuncNameTypeNames(name, argtypes, false);

	pcqCtx = caql_addrel(cqclr(&cqc), rel);

	tup = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_proc "
				" WHERE oid = :1 "
				" FOR UPDATE ",
				ObjectIdGetDatum(procOid)));

	if (!HeapTupleIsValid(tup)) /* should not happen */
		elog(ERROR, "cache lookup failed for function %u", procOid);

	if (((Form_pg_proc) GETSTRUCT(tup))->proisagg)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is an aggregate function",
						NameListToString(name)),
				 errhint("Use ALTER AGGREGATE to change owner of aggregate functions.")));

	/* check bootstrap object */
	CheckForModifySystemFunc(procOid, name);

	AlterFunctionOwner_internal(pcqCtx, rel, tup, newOwnerId);

	heap_close(rel, NoLock);
}

/*
 * Change function owner by Oid
 */
void
AlterFunctionOwner_oid(Oid procOid, Oid newOwnerId)
{
	Relation	rel;
	HeapTuple	tup;
	cqContext  *pcqCtx;
	cqContext	cqc;

	rel = heap_open(ProcedureRelationId, RowExclusiveLock);

	pcqCtx = caql_addrel(cqclr(&cqc), rel);

	tup = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_proc "
				" WHERE oid = :1 "
				" FOR UPDATE ",
				ObjectIdGetDatum(procOid)));

	if (!HeapTupleIsValid(tup)) /* should not happen */
		elog(ERROR, "cache lookup failed for function %u", procOid);

	AlterFunctionOwner_internal(pcqCtx, rel, tup, newOwnerId);

	heap_close(rel, NoLock);
}

static void
AlterFunctionOwner_internal(cqContext *pcqCtx,
							Relation rel, HeapTuple tup, Oid newOwnerId)
{
	Form_pg_proc procForm;
	AclResult	aclresult;
	Oid			procOid;

	Assert(RelationGetRelid(rel) == ProcedureRelationId);

	procForm = (Form_pg_proc) GETSTRUCT(tup);
	procOid = HeapTupleGetOid(tup);

	/*
	 * If the new owner is the same as the existing owner, consider the
	 * command to have succeeded.  This is for dump restoration purposes.
	 */
	if (procForm->proowner != newOwnerId)
	{
		Datum		repl_val[Natts_pg_proc];
		bool		repl_null[Natts_pg_proc];
		bool		repl_repl[Natts_pg_proc];
		Acl		   *newAcl;
		Datum		aclDatum;
		bool		isNull;
		HeapTuple	newtuple;

		/* Superusers can always do it */
		if (!superuser())
		{
			/* Otherwise, must be owner of the existing object */
			if (!pg_proc_ownercheck(procOid, GetUserId()))
				aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_PROC,
							   NameStr(procForm->proname));

			/* Must be able to become new owner */
			check_is_member_of_role(GetUserId(), newOwnerId);

			/* New owner must have CREATE privilege on namespace */
			aclresult = pg_namespace_aclcheck(procForm->pronamespace,
											  newOwnerId,
											  ACL_CREATE);
			if (aclresult != ACLCHECK_OK)
				aclcheck_error(aclresult, ACL_KIND_NAMESPACE,
							   get_namespace_name(procForm->pronamespace));
		}

		memset(repl_null, false, sizeof(repl_null));
		memset(repl_repl, false, sizeof(repl_repl));

		repl_repl[Anum_pg_proc_proowner - 1] = true;
		repl_val[Anum_pg_proc_proowner - 1] = ObjectIdGetDatum(newOwnerId);

		/*
		 * Determine the modified ACL for the new owner.  This is only
		 * necessary when the ACL is non-null.
		 */
		aclDatum = caql_getattr(pcqCtx,
								Anum_pg_proc_proacl,
								&isNull);
		if (!isNull)
		{
			newAcl = aclnewowner(DatumGetAclP(aclDatum),
								 procForm->proowner, newOwnerId);
			repl_repl[Anum_pg_proc_proacl - 1] = true;
			repl_val[Anum_pg_proc_proacl - 1] = PointerGetDatum(newAcl);
		}

		newtuple = caql_modify_current(pcqCtx, repl_val, repl_null, repl_repl);
		
		caql_update_current(pcqCtx, newtuple);
		/* and Update indexes (implicit) */

		heap_freetuple(newtuple);

		/* Update owner dependency reference */
		changeDependencyOnOwner(ProcedureRelationId, procOid, newOwnerId);
	}
	/* caller frees pcqCtx */
}

/*
 * Implements the ALTER FUNCTION utility command (except for the
 * RENAME and OWNER clauses, which are handled as part of the generic
 * ALTER framework).
 */
void
AlterFunction(AlterFunctionStmt *stmt)
{
	HeapTuple	tup;
	Oid			funcOid;
	Form_pg_proc procForm;
	Relation	rel;
	ListCell   *l;
	DefElem    *volatility_item = NULL;
	DefElem    *strict_item = NULL;
	DefElem    *security_def_item = NULL;
	DefElem    *data_access_item = NULL;
	cqContext	cqc;
	cqContext  *pcqCtx;
	bool		isnull;
	char		data_access;

	rel = heap_open(ProcedureRelationId, RowExclusiveLock);

	funcOid = LookupFuncNameTypeNames(stmt->func->funcname,
									  stmt->func->funcargs,
									  false);

	pcqCtx = caql_addrel(cqclr(&cqc), rel);

	tup = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_proc "
				" WHERE oid = :1 "
				" FOR UPDATE ",
				ObjectIdGetDatum(funcOid)));

	if (!HeapTupleIsValid(tup)) /* should not happen */
		elog(ERROR, "cache lookup failed for function %u", funcOid);

	procForm = (Form_pg_proc) GETSTRUCT(tup);

	/* Permission check: must own function */
	if (!pg_proc_ownercheck(funcOid, GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_PROC,
					   NameListToString(stmt->func->funcname));

	/* check bootstrap object */
	CheckForModifySystemFunc(funcOid, stmt->func->funcname);

	if (procForm->proisagg)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is an aggregate function",
						NameListToString(stmt->func->funcname))));

	/* Examine requested actions. */
	foreach(l, stmt->actions)
	{
		DefElem    *defel = (DefElem *) lfirst(l);

		if (compute_common_attribute(defel,
									 &volatility_item,
									 &strict_item,
									 &security_def_item,
									 &data_access_item) == false)
			elog(ERROR, "option \"%s\" not recognized", defel->defname);
	}

	if (volatility_item)
		procForm->provolatile = interpret_func_volatility(volatility_item);
	if (strict_item)
		procForm->proisstrict = intVal(strict_item->arg);
	if (security_def_item)
		procForm->prosecdef = intVal(security_def_item->arg);
	if (data_access_item)
	{
		Datum		repl_val[Natts_pg_proc];
		bool		repl_null[Natts_pg_proc];
		bool		repl_repl[Natts_pg_proc];

		MemSet(repl_null, 0, sizeof(repl_null));
		MemSet(repl_repl, 0, sizeof(repl_repl));
		repl_repl[Anum_pg_proc_prodataaccess - 1] = true;
		repl_val[Anum_pg_proc_prodataaccess - 1] =
			CharGetDatum(interpret_data_access(data_access_item));

		tup = caql_modify_current(pcqCtx, repl_val, repl_null, repl_repl);
	}

	/* Not caql_getattr, as it might be a copy. */
	data_access = DatumGetChar(
			heap_getattr(tup, Anum_pg_proc_prodataaccess,
				pcqCtx->cq_tupdesc, &isnull));
	Assert(!isnull);
	/* Cross check for various properties. */
	validate_sql_data_access(data_access,
							 procForm->provolatile,
							 procForm->prolang);


	/* Do the update */

	caql_update_current(pcqCtx, tup); /* implicit update of index as well */

	heap_close(rel, NoLock);
	heap_freetuple(tup);
	
	if (Gp_role == GP_ROLE_DISPATCH)
	{
		CdbDispatchUtilityStatement((Node *) stmt, "AlterFunction");
	}
}

/*
 * SetFunctionReturnType - change declared return type of a function
 *
 * This is presently only used for adjusting legacy functions that return
 * OPAQUE to return whatever we find their correct definition should be.
 * The caller should emit a suitable warning explaining what we did.
 */
void
SetFunctionReturnType(Oid funcOid, Oid newRetType)
{
	Relation	pg_proc_rel;
	HeapTuple	tup;
	Form_pg_proc procForm;
	cqContext	cqc;
	cqContext  *pcqCtx;

	pg_proc_rel = heap_open(ProcedureRelationId, RowExclusiveLock);

	pcqCtx = caql_addrel(cqclr(&cqc), pg_proc_rel);

	tup = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_proc "
				" WHERE oid = :1 "
				" FOR UPDATE ",
				ObjectIdGetDatum(funcOid)));

	if (!HeapTupleIsValid(tup)) /* should not happen */
		elog(ERROR, "cache lookup failed for function %u", funcOid);
	procForm = (Form_pg_proc) GETSTRUCT(tup);

	if (procForm->prorettype != OPAQUEOID)		/* caller messed up */
		elog(ERROR, "function %u doesn't return OPAQUE", funcOid);

	/* okay to overwrite copied tuple */
	procForm->prorettype = newRetType;

	/* update the catalog and its indexes */
	caql_update_current(pcqCtx, tup); /* implicit update of index as well */

	heap_close(pg_proc_rel, RowExclusiveLock);
}


/*
 * SetFunctionArgType - change declared argument type of a function
 *
 * As above, but change an argument's type.
 */
void
SetFunctionArgType(Oid funcOid, int argIndex, Oid newArgType)
{
	Relation	pg_proc_rel;
	HeapTuple	tup;
	Form_pg_proc procForm;
	cqContext	cqc;
	cqContext  *pcqCtx;

	pg_proc_rel = heap_open(ProcedureRelationId, RowExclusiveLock);

	pcqCtx = caql_addrel(cqclr(&cqc), pg_proc_rel);

	tup = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_proc "
				" WHERE oid = :1 "
				" FOR UPDATE ",
				ObjectIdGetDatum(funcOid)));

	if (!HeapTupleIsValid(tup)) /* should not happen */
		elog(ERROR, "cache lookup failed for function %u", funcOid);
	procForm = (Form_pg_proc) GETSTRUCT(tup);

	if (argIndex < 0 || argIndex >= procForm->pronargs ||
		procForm->proargtypes.values[argIndex] != OPAQUEOID)
		elog(ERROR, "function %u doesn't take OPAQUE", funcOid);

	/* okay to overwrite copied tuple */
	procForm->proargtypes.values[argIndex] = newArgType;

	/* update the catalog and its indexes */
	caql_update_current(pcqCtx, tup); /* implicit update of index as well */

	heap_close(pg_proc_rel, RowExclusiveLock);
}



/*
 * CREATE CAST
 */
void
CreateCast(CreateCastStmt *stmt)
{
	Oid			sourcetypeid;
	Oid			targettypeid;
	Oid			funcid;
	int			nargs;
	char		castcontext;
	Relation	relation;
	HeapTuple	tuple;
	Datum		values[Natts_pg_cast];
	bool		nulls[Natts_pg_cast];
	ObjectAddress myself,
				referenced;
	cqContext	cqc;
	cqContext	cqc2;
	cqContext  *pcqCtx;

	sourcetypeid = typenameTypeId(NULL, stmt->sourcetype);
	targettypeid = typenameTypeId(NULL, stmt->targettype);

	/* No pseudo-types allowed */
	if (get_typtype(sourcetypeid) == 'p')
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("source data type %s is a pseudo-type",
						TypeNameToString(stmt->sourcetype))));

	if (get_typtype(targettypeid) == 'p')
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("target data type %s is a pseudo-type",
						TypeNameToString(stmt->targettype))));

	/* Permission check */
	if (!pg_type_ownercheck(sourcetypeid, GetUserId())
		&& !pg_type_ownercheck(targettypeid, GetUserId()))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be owner of type %s or type %s",
						TypeNameToString(stmt->sourcetype),
						TypeNameToString(stmt->targettype))));

	if (stmt->func != NULL)
	{
		Form_pg_proc procstruct;
		cqContext  *proccqCtx;

		funcid = LookupFuncNameTypeNames(stmt->func->funcname,
										 stmt->func->funcargs,
										 false);

		proccqCtx = caql_beginscan(
				NULL,
				cql("SELECT * FROM pg_proc "
					" WHERE oid = :1 ",
					ObjectIdGetDatum(funcid)));

		tuple = caql_getnext(proccqCtx);

		if (!HeapTupleIsValid(tuple))
			elog(ERROR, "cache lookup failed for function %u", funcid);

		procstruct = (Form_pg_proc) GETSTRUCT(tuple);
		nargs = procstruct->pronargs;
		if (nargs < 1 || nargs > 3)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				  errmsg("cast function must take one to three arguments")));
		if (procstruct->proargtypes.values[0] != sourcetypeid)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
			errmsg("argument of cast function must match source data type")));
		if (nargs > 1 && procstruct->proargtypes.values[1] != INT4OID)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
			errmsg("second argument of cast function must be type integer")));
		if (nargs > 2 && procstruct->proargtypes.values[2] != BOOLOID)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
			errmsg("third argument of cast function must be type boolean")));
		if (procstruct->prorettype != targettypeid)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("return data type of cast function must match target data type")));

		/*
		 * Restricting the volatility of a cast function may or may not be a
		 * good idea in the abstract, but it definitely breaks many old
		 * user-defined types.	Disable this check --- tgl 2/1/03
		 */
#ifdef NOT_USED
		if (procstruct->provolatile == PROVOLATILE_VOLATILE)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("cast function must not be volatile")));
#endif
		if (procstruct->proisagg)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("cast function must not be an aggregate function")));
		if (procstruct->proretset)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("cast function must not return a set")));

		caql_endscan(proccqCtx);
	}
	else
	{
		int16		typ1len;
		int16		typ2len;
		bool		typ1byval;
		bool		typ2byval;
		char		typ1align;
		char		typ2align;

		/* indicates binary coercibility */
		funcid = InvalidOid;
		nargs = 0;

		/*
		 * Must be superuser to create binary-compatible casts, since
		 * erroneous casts can easily crash the backend.
		 */
		if (!superuser())
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
			 errmsg("must be superuser to create a cast WITHOUT FUNCTION")));

		/*
		 * Also, insist that the types match as to size, alignment, and
		 * pass-by-value attributes; this provides at least a crude check that
		 * they have similar representations.  A pair of types that fail this
		 * test should certainly not be equated.
		 */
		get_typlenbyvalalign(sourcetypeid, &typ1len, &typ1byval, &typ1align);
		get_typlenbyvalalign(targettypeid, &typ2len, &typ2byval, &typ2align);
		if (typ1len != typ2len ||
			typ1byval != typ2byval ||
			typ1align != typ2align)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("source and target data types are not physically compatible")));
	}

	/*
	 * Allow source and target types to be same only for length coercion
	 * functions.  We assume a multi-arg function does length coercion.
	 */
	if (sourcetypeid == targettypeid && nargs < 2)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
			  errmsg("source data type and target data type are the same")));

	/* convert CoercionContext enum to char value for castcontext */
	switch (stmt->context)
	{
		case COERCION_IMPLICIT:
			castcontext = COERCION_CODE_IMPLICIT;
			break;
		case COERCION_ASSIGNMENT:
			castcontext = COERCION_CODE_ASSIGNMENT;
			break;
		case COERCION_EXPLICIT:
			castcontext = COERCION_CODE_EXPLICIT;
			break;
		default:
			elog(ERROR, "unrecognized CoercionContext: %d", stmt->context);
			castcontext = 0;	/* keep compiler quiet */
			break;
	}

	relation = heap_open(CastRelationId, RowExclusiveLock);
	pcqCtx = caql_beginscan(
							caql_addrel(cqclr(&cqc), relation), 
							cql("INSERT INTO pg_cast",
								NULL));

	/*
	 * Check for duplicate.  This is just to give a friendly error message,
	 * the unique index would catch it anyway (so no need to sweat about race
	 * conditions).
	 */

	if (caql_getcount(
				caql_addrel(cqclr(&cqc2), relation), 
				cql("SELECT COUNT(*) FROM pg_cast "
					" WHERE castsource = :1 "
					" AND casttarget = :2 ",
					ObjectIdGetDatum(sourcetypeid),
					ObjectIdGetDatum(targettypeid))))
	{
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("cast from type %s to type %s already exists",
						TypeNameToString(stmt->sourcetype),
						TypeNameToString(stmt->targettype))));
	}
	/* ready to go */
	values[Anum_pg_cast_castsource - 1] = ObjectIdGetDatum(sourcetypeid);
	values[Anum_pg_cast_casttarget - 1] = ObjectIdGetDatum(targettypeid);
	values[Anum_pg_cast_castfunc - 1] = ObjectIdGetDatum(funcid);
	values[Anum_pg_cast_castcontext - 1] = CharGetDatum(castcontext);

	MemSet(nulls, false, sizeof(nulls));

	tuple = caql_form_tuple(pcqCtx, values, nulls);

	if (stmt->castOid != 0)
		HeapTupleSetOid(tuple, stmt->castOid);
		
	stmt->castOid = caql_insert(pcqCtx, tuple);
	/* and Update indexes (implicit) */

	/* make dependency entries */
	myself.classId = CastRelationId;
	myself.objectId = HeapTupleGetOid(tuple);
	myself.objectSubId = 0;

	/* dependency on source type */
	referenced.classId = TypeRelationId;
	referenced.objectId = sourcetypeid;
	referenced.objectSubId = 0;
	recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);

	/* dependency on target type */
	referenced.classId = TypeRelationId;
	referenced.objectId = targettypeid;
	referenced.objectSubId = 0;
	recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);

	/* dependency on function */
	if (OidIsValid(funcid))
	{
		referenced.classId = ProcedureRelationId;
		referenced.objectId = funcid;
		referenced.objectSubId = 0;
		recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);
	}

	heap_freetuple(tuple);

	caql_endscan(pcqCtx);
	heap_close(relation, RowExclusiveLock);
	
	if (Gp_role == GP_ROLE_DISPATCH)
	{
		CdbDispatchUtilityStatement((Node *) stmt, "CreateCast");
	}
	
}



/*
 * DROP CAST
 */
void
DropCast(DropCastStmt *stmt)
{
	Oid			sourcetypeid;
	Oid			targettypeid;
	ObjectAddress object;
	int			fetchCount;
	Oid			castOid;

	/* when dropping a cast, the types must exist even if you use IF EXISTS */
	sourcetypeid = typenameTypeId(NULL, stmt->sourcetype);
	targettypeid = typenameTypeId(NULL, stmt->targettype);

	castOid = caql_getoid_plus(
			NULL,
			&fetchCount,
			NULL,
			cql("SELECT oid FROM pg_cast "
				" WHERE castsource = :1 "
				" AND casttarget = :2 ",
				ObjectIdGetDatum(sourcetypeid),
				ObjectIdGetDatum(targettypeid)));

	if (!fetchCount)
	{
		if (!stmt->missing_ok)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("cast from type %s to type %s does not exist",
							TypeNameToString(stmt->sourcetype),
							TypeNameToString(stmt->targettype))));
		else
			ereport(NOTICE,
					(errmsg("cast from type %s to type %s does not exist, skipping",
							TypeNameToString(stmt->sourcetype),
							TypeNameToString(stmt->targettype))));

		return;
	}

	/* Permission check */
	if (!pg_type_ownercheck(sourcetypeid, GetUserId())
		&& !pg_type_ownercheck(targettypeid, GetUserId()))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be owner of type %s or type %s",
						TypeNameToString(stmt->sourcetype),
						TypeNameToString(stmt->targettype))));

	/*
	 * Do the deletion
	 */
	object.classId = CastRelationId;
	object.objectId = castOid;
	object.objectSubId = 0;

	performDeletion(&object, stmt->behavior);
	
	if (Gp_role == GP_ROLE_DISPATCH)
	{
		CdbDispatchUtilityStatement((Node *) stmt, "DropCast");
	}
}


void
DropCastById(Oid castOid)
{
	int			numDel;

	numDel = caql_getcount(
			NULL,
			cql("DELETE FROM pg_cast "
				" WHERE oid = :1 ",
				ObjectIdGetDatum(castOid)));

	if (!numDel)
		elog(ERROR, "could not find tuple for cast %u", castOid);
}

/*
 * Execute ALTER FUNCTION/AGGREGATE SET SCHEMA
 *
 * These commands are identical except for the lookup procedure, so share code.
 */
void
AlterFunctionNamespace(List *name, List *argtypes, bool isagg,
					   const char *newschema)
{
	Oid			procOid;
	Oid			oldNspOid;
	Oid			nspOid;
	HeapTuple	tup;
	Relation	procRel;
	Form_pg_proc proc;
	cqContext	cqc2;
	cqContext	cqc;
	cqContext  *pcqCtx;

	procRel = heap_open(ProcedureRelationId, RowExclusiveLock);

	/* get function OID */
	if (isagg)
		procOid = LookupAggNameTypeNames(name, argtypes, false);
	else
		procOid = LookupFuncNameTypeNames(name, argtypes, false);

	/* check permissions on function */
	if (!pg_proc_ownercheck(procOid, GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_PROC,
					   NameListToString(name));

	pcqCtx = caql_addrel(cqclr(&cqc), procRel);

	tup = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_proc "
				" WHERE oid = :1 "
				" FOR UPDATE ",
				ObjectIdGetDatum(procOid)));

	if (!HeapTupleIsValid(tup))
		elog(ERROR, "cache lookup failed for function %u", procOid);
	proc = (Form_pg_proc) GETSTRUCT(tup);

	oldNspOid = proc->pronamespace;

	/* get schema OID and check its permissions */
	nspOid = LookupCreationNamespace(newschema);

	if (oldNspOid == nspOid)
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_FUNCTION),
				 errmsg("function \"%s\" is already in schema \"%s\"",
						NameListToString(name),
						newschema)));

	/* disallow renaming into or out of temp schemas */
	if (isAnyTempNamespace(nspOid) || isAnyTempNamespace(oldNspOid))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			errmsg("cannot move objects into or out of temporary schemas")));

	/* same for TOAST schema */
	if (nspOid == PG_TOAST_NAMESPACE || oldNspOid == PG_TOAST_NAMESPACE)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot move objects into or out of TOAST schema")));

	/* same for AO segment list schema */
	if (nspOid == PG_AOSEGMENT_NAMESPACE || oldNspOid == PG_AOSEGMENT_NAMESPACE)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot move objects into or out of AO SEGMENT schema")));
	
	/* check for duplicate name (more friendly than unique-index failure) */
	if (caql_getcount(
				caql_addrel(cqclr(&cqc2), procRel),
				cql("SELECT COUNT(*) FROM pg_proc "
					" WHERE proname = :1 "
					" AND proargtypes = :2 "
					" AND pronamespace = :3 ",
					CStringGetDatum(NameStr(proc->proname)),
					PointerGetDatum(&proc->proargtypes),
					ObjectIdGetDatum(nspOid))))
	{
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_FUNCTION),
				 errmsg("function \"%s\" already exists in schema \"%s\"",
						NameStr(proc->proname),
						newschema)));
	}

	/* OK, modify the pg_proc row */

	/* tup is a copy, so we can scribble directly on it */
	proc->pronamespace = nspOid;

	caql_update_current(pcqCtx, tup); /* implicit update of index as well */

	/* Update dependency on schema */
	if (changeDependencyFor(ProcedureRelationId, procOid,
							NamespaceRelationId, oldNspOid, nspOid) != 1)
		elog(ERROR, "failed to change schema dependency for function \"%s\"",
			 NameListToString(name));

	heap_freetuple(tup);

	heap_close(procRel, RowExclusiveLock);
}

/*
 * GetFuncSQLDataAccess
 *  Returns the data-access indication of a function specified by
 *  the input funcOid.
 */
SQLDataAccess
GetFuncSQLDataAccess(Oid funcOid)
{
	Relation	procRelation;
	HeapTuple	procTuple;
	bool		isnull = false;
	char		proDataAccess;
	SQLDataAccess	result = SDA_NO_SQL;
	cqContext	cqc;

	procRelation = heap_open(ProcedureRelationId, AccessShareLock);

	procTuple = caql_getfirst(
			caql_addrel(cqclr(&cqc), procRelation),
			cql("SELECT * from pg_proc"
				" WHERE oid = :1",
				ObjectIdGetDatum(funcOid)));

	if (!HeapTupleIsValid(procTuple))
		elog(ERROR, "cache lookup failed for function %u", funcOid);

	/* get the prodataaccess */
	proDataAccess = DatumGetChar(heap_getattr(procTuple,
											  Anum_pg_proc_prodataaccess,
											  RelationGetDescr(procRelation),
											  &isnull));

	heap_freetuple(procTuple);
	heap_close(procRelation, AccessShareLock);

	/*
	 * In upgrade mode, allow prodataaccess to be NULL, to handle the case
	 * where prodataaccess column has not been added to pg_proc yet. This is
	 * specifically to handle catDML()
	 */
	if (gp_upgrade_mode && isnull)
		return SDA_NO_SQL;

	Assert(!isnull);

	if (proDataAccess == PRODATAACCESS_NONE)
		result = SDA_NO_SQL;
	else if (proDataAccess == PRODATAACCESS_CONTAINS)
		result = SDA_CONTAINS_SQL;
	else if (proDataAccess == PRODATAACCESS_READS)
		result = SDA_READS_SQL;
	else if (proDataAccess == PRODATAACCESS_MODIFIES)
		result = SDA_MODIFIES_SQL;
	else
		elog(ERROR, "invalid data access option for function %u", funcOid);

	return result;
}

static void
CheckForModifySystemFunc(Oid funcOid, List *funcName)
{
	if (!allowSystemTableModsDDL && funcOid < FirstBootstrapObjectId)
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission defined: \"%s\" is a system function",
						NameListToString(funcName))));
}
