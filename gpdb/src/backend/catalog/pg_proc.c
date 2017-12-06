/*-------------------------------------------------------------------------
 *
 * pg_proc.c
 *	  routines to support manipulation of the pg_proc relation
 *
 * Portions Copyright (c) 1996-2009, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/catalog/pg_proc.c,v 1.141 2006/10/19 18:32:46 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/xact.h"
#include "catalog/catquery.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/pg_depend.h"
#include "catalog/pg_language.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_proc_callback.h"
#include "catalog/pg_type.h"
#include "catalog/pg_rewrite.h"
#include "executor/functions.h"
#include "funcapi.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "parser/parse_type.h"
#include "tcop/pquery.h"
#include "tcop/tcopprot.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "cdb/cdbvars.h"


Datum		fmgr_internal_validator(PG_FUNCTION_ARGS);
Datum		fmgr_c_validator(PG_FUNCTION_ARGS);
Datum		fmgr_sql_validator(PG_FUNCTION_ARGS);

static void sql_function_parse_error_callback(void *arg);
static int match_prosrc_to_query(const char *prosrc, const char *queryText,
					  int cursorpos);
static bool match_prosrc_to_literal(const char *prosrc, const char *literal,
						int cursorpos, int *newcursorpos);


/* ----------------------------------------------------------------
 *		ProcedureCreate
 *
 * Note: allParameterTypes, parameterModes, parameterNames are either arrays
 * of the proper types or NULL.  We declare them Datum, not "ArrayType *",
 * to avoid importing array.h into pg_proc.h.
 * ----------------------------------------------------------------
 */
Oid
ProcedureCreate(const char *procedureName,
				Oid procNamespace,
				bool replace,
				bool returnsSet,
				Oid returnType,
				Oid languageObjectId,
				Oid languageValidator,
				Oid describeFuncOid,
				const char *prosrc,
				const char *probin,
				bool isAgg,
				bool isWin,
				bool security_definer,
				bool isStrict,
				char volatility,
				const oidvector *parameterTypes,
				Datum allParameterTypes,
				Datum parameterModes,
				Datum parameterNames,
				char prodataaccess,
				Oid funcOid)
{
	Oid			retval;
	int			parameterCount;
	int			allParamCount;
	const Oid  *allParams;
	bool		genericInParam = false;
	bool		genericOutParam = false;
	bool		internalInParam = false;
	bool		internalOutParam = false;
	Relation	rel;
	HeapTuple	tup;
	HeapTuple	oldtup;
	bool		nulls[Natts_pg_proc];
	Datum		values[Natts_pg_proc];
	bool		replaces[Natts_pg_proc];
	Oid			relid;
	NameData	procname;
	bool		is_update;
	ObjectAddress myself,
				referenced;
	int			i;
	cqContext	cqc;
	cqContext  *pcqCtx;

	/*
	 * sanity checks
	 */
	Assert(PointerIsValid(prosrc));
	Assert(PointerIsValid(probin));

	parameterCount = parameterTypes->dim1;
	if (parameterCount < 0 || parameterCount > FUNC_MAX_ARGS)
		ereport(ERROR,
				(errcode(ERRCODE_TOO_MANY_ARGUMENTS),
				 errmsg("functions cannot have more than %d arguments",
						FUNC_MAX_ARGS)));
	/* note: the above is correct, we do NOT count output arguments */

	if (allParameterTypes != PointerGetDatum(NULL))
	{
		/*
		 * We expect the array to be a 1-D OID array; verify that. We don't
		 * need to use deconstruct_array() since the array data is just going
		 * to look like a C array of OID values.
		 */
		ArrayType  *allParamArray = (ArrayType *) DatumGetPointer(allParameterTypes);

		allParamCount = ARR_DIMS(allParamArray)[0];
		if (ARR_NDIM(allParamArray) != 1 ||
			allParamCount <= 0 ||
			ARR_HASNULL(allParamArray) ||
			ARR_ELEMTYPE(allParamArray) != OIDOID)
			elog(ERROR, "allParameterTypes is not a 1-D Oid array");
		allParams = (Oid *) ARR_DATA_PTR(allParamArray);
		Assert(allParamCount >= parameterCount);
		/* we assume caller got the contents right */
	}
	else
	{
		allParamCount = parameterCount;
		allParams = parameterTypes->values;
	}

	/*
	 * Do not allow return type ANYARRAY or ANYELEMENT unless at least one
	 * input argument is ANYARRAY or ANYELEMENT.  Also, do not allow return
	 * type INTERNAL unless at least one input argument is INTERNAL.
	 */
	for (i = 0; i < parameterCount; i++)
	{
		switch (parameterTypes->values[i])
		{
			case ANYARRAYOID:
			case ANYELEMENTOID:
				genericInParam = true;
				break;
			case INTERNALOID:
				internalInParam = true;
				break;
		}
	}

	if (allParameterTypes != PointerGetDatum(NULL))
	{
		for (i = 0; i < allParamCount; i++)
		{
			/*
			 * We don't bother to distinguish input and output params here, so
			 * if there is, say, just an input INTERNAL param then we will
			 * still set internalOutParam.	This is OK since we don't really
			 * care.
			 */
			switch (allParams[i])
			{
				case ANYARRAYOID:
				case ANYELEMENTOID:
					genericOutParam = true;
					break;
				case INTERNALOID:
					internalOutParam = true;
					break;
			}
		}
	}

	/* Normally, we don't allow ANY* return type without an ANY* input type.
	 * But during upgrade, we're creating "special" function used by aggregate
	 * and we'll bypass this check for those functions.
	 */
	if ((returnType == ANYARRAYOID || returnType == ANYELEMENTOID ||
		 genericOutParam) && !genericInParam && !gp_upgrade_mode)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("cannot determine result data type"),
				 errdetail("A function returning \"anyarray\" or \"anyelement\" must have at least one argument of either type.")));

	if ((returnType == INTERNALOID || internalOutParam) && !internalInParam)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("unsafe use of pseudo-type \"internal\""),
				 errdetail("A function returning \"internal\" must have at least one \"internal\" argument.")));

	/*
	 * don't allow functions of complex types that have the same name as
	 * existing attributes of the type
	 */
	if (parameterCount == 1 &&
		OidIsValid(parameterTypes->values[0]) &&
		(relid = typeidTypeRelid(parameterTypes->values[0])) != InvalidOid &&
		get_attnum(relid, procedureName) != InvalidAttrNumber)
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_COLUMN),
				 errmsg("\"%s\" is already an attribute of type %s",
						procedureName,
						format_type_be(parameterTypes->values[0]))));

	/*
	 * All seems OK; prepare the data to be inserted into pg_proc.
	 */

	for (i = 0; i < Natts_pg_proc; ++i)
	{
		nulls[i] = false;
		values[i] = (Datum) 0;
		replaces[i] = true;
	}

	namestrcpy(&procname, procedureName);
	values[Anum_pg_proc_proname - 1] = NameGetDatum(&procname);
	values[Anum_pg_proc_pronamespace - 1] = ObjectIdGetDatum(procNamespace);
	values[Anum_pg_proc_proowner - 1] = ObjectIdGetDatum(GetUserId());
	values[Anum_pg_proc_prolang - 1] = ObjectIdGetDatum(languageObjectId);
	values[Anum_pg_proc_proisagg - 1] = BoolGetDatum(isAgg);
	values[Anum_pg_proc_proiswin - 1] = BoolGetDatum(isWin);
	values[Anum_pg_proc_prosecdef - 1] = BoolGetDatum(security_definer);
	values[Anum_pg_proc_proisstrict - 1] = BoolGetDatum(isStrict);
	values[Anum_pg_proc_proretset - 1] = BoolGetDatum(returnsSet);
	values[Anum_pg_proc_provolatile - 1] = CharGetDatum(volatility);
	values[Anum_pg_proc_pronargs - 1] = UInt16GetDatum(parameterCount);
	values[Anum_pg_proc_prorettype - 1] = ObjectIdGetDatum(returnType);
	values[Anum_pg_proc_proargtypes - 1] = PointerGetDatum(parameterTypes);
	if (allParameterTypes != PointerGetDatum(NULL))
		values[Anum_pg_proc_proallargtypes - 1] = allParameterTypes;
	else
		nulls[Anum_pg_proc_proallargtypes - 1] = true;
	if (parameterModes != PointerGetDatum(NULL))
		values[Anum_pg_proc_proargmodes - 1] = parameterModes;
	else
		nulls[Anum_pg_proc_proargmodes - 1] = true;
	if (parameterNames != PointerGetDatum(NULL))
		values[Anum_pg_proc_proargnames - 1] = parameterNames;
	else
		nulls[Anum_pg_proc_proargnames - 1] = true;
	values[Anum_pg_proc_prosrc - 1] = CStringGetTextDatum(prosrc);
	if (probin)
		values[Anum_pg_proc_probin - 1] = CStringGetTextDatum(probin);
	else
		nulls[Anum_pg_proc_probin - 1] = true;
	/* start out with empty permissions */
	nulls[Anum_pg_proc_proacl - 1] = true;
	values[Anum_pg_proc_prodataaccess - 1] = CharGetDatum(prodataaccess);

	rel = heap_open(ProcedureRelationId, RowExclusiveLock);

	/* Check for pre-existing definition */

	pcqCtx = caql_beginscan(
			caql_addrel(cqclr(&cqc), rel),
			cql("SELECT * FROM pg_proc "
				" WHERE proname = :1 "
				" AND proargtypes = :2 "
				" AND pronamespace = :3 "
				" FOR UPDATE ",
				CStringGetDatum((char *) procedureName),
				PointerGetDatum(parameterTypes),
				ObjectIdGetDatum(procNamespace)));

	oldtup = caql_getnext(pcqCtx);

	if (HeapTupleIsValid(oldtup))
	{
		/* There is one; okay to replace it? */
		Form_pg_proc oldproc = (Form_pg_proc) GETSTRUCT(oldtup);
		Oid oldOid = HeapTupleGetOid(oldtup);

		if (!replace)
			ereport(ERROR,
					(errcode(ERRCODE_DUPLICATE_FUNCTION),
			errmsg("function \"%s\" already exists with same argument types",
				   procedureName)));
		if (!pg_proc_ownercheck(oldOid, GetUserId()))
			aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_PROC,
						   procedureName);

		/*
		 * Not okay to change the return type of the existing proc, since
		 * existing rules, views, etc may depend on the return type.
		 */
		if (returnType != oldproc->prorettype ||
			returnsSet != oldproc->proretset)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("cannot change return type of existing function"),
					 errhint("Use DROP FUNCTION first.")));

		/*
		 * If it returns RECORD, check for possible change of record type
		 * implied by OUT parameters
		 */
		if (returnType == RECORDOID)
		{
			TupleDesc	olddesc;
			TupleDesc	newdesc;

			olddesc = build_function_result_tupdesc_t(oldtup);
			newdesc = build_function_result_tupdesc_d(allParameterTypes,
													  parameterModes,
													  parameterNames);
			if (olddesc == NULL && newdesc == NULL)
				 /* ok, both are runtime-defined RECORDs */ ;
			else if (olddesc == NULL || newdesc == NULL ||
					 !equalTupleDescs(olddesc, newdesc, true))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					errmsg("cannot change return type of existing function"),
				errdetail("Row type defined by OUT parameters is different."),
						 errhint("Use DROP FUNCTION first.")));
		}

		/*
		 * Cannot add a describe callback to a function that has views defined
		 * on it.  This restriction is for the same set of reasons that we 
		 * cannot change the return type of existing functions.
		 */
		if (OidIsValid(describeFuncOid))
		{
			HeapTuple   depTup;
			cqContext  *pcqCtx2;
			

			/* XXX XXX: SELECT COUNT(*) ... AND classid = :3, RewriteRelationId) */
			pcqCtx2 = caql_beginscan(
					NULL,
					cql("SELECT * FROM pg_depend "
						" WHERE refclassid = :1 "
						" AND refobjid = :2 ",
						ObjectIdGetDatum(ProcedureRelationId),
						ObjectIdGetDatum(oldOid)));
			
			while (HeapTupleIsValid(depTup = caql_getnext(pcqCtx2)))
			{
				Form_pg_depend depData = (Form_pg_depend) GETSTRUCT(depTup);
				if (depData->classid == RewriteRelationId)
				{
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
							 errmsg("cannot add DESCRIBE callback to function used in view(s)")));
				}
			}
			caql_endscan(pcqCtx2);
		}

		/* Can't change aggregate or window status, either */
		if (oldproc->proisagg != isAgg)
		{
			if (oldproc->proisagg)
				ereport(ERROR,
						(errcode(ERRCODE_WRONG_OBJECT_TYPE),
						 errmsg("function \"%s\" is an aggregate",
								procedureName)));
			else
				ereport(ERROR,
						(errcode(ERRCODE_WRONG_OBJECT_TYPE),
						 errmsg("function \"%s\" is not an aggregate",
								procedureName)));
		}
		if (oldproc->proiswin != isWin)
		{
			if (oldproc->proiswin)
				ereport(ERROR,
						(errcode(ERRCODE_WRONG_OBJECT_TYPE),
						 errmsg("function \"%s\" is a window function",
								procedureName)));
			else
				ereport(ERROR,
						(errcode(ERRCODE_WRONG_OBJECT_TYPE),
						 errmsg("function \"%s\" is not a window function",
								procedureName)));
		}

		/* do not change existing ownership or permissions, either */
		replaces[Anum_pg_proc_proowner - 1] = false;
		replaces[Anum_pg_proc_proacl - 1] = false;

		/* Okay, do it... */
		tup = caql_modify_current(pcqCtx, values, nulls, replaces);
		caql_update_current(pcqCtx, tup); /* implicit update of index as well*/

		caql_endscan(pcqCtx);
		is_update = true;
	}
	else
	{
		pcqCtx = caql_beginscan(
				caql_addrel(cqclr(&cqc), rel),
				cql("INSERT INTO pg_proc ",
					NULL));

		/* Creating a new procedure */
		tup = caql_form_tuple(pcqCtx, values, nulls);
				
		if (OidIsValid(funcOid))
			HeapTupleSetOid(tup, funcOid);
		
		/* Insert tuple into the relation */
		caql_insert(pcqCtx, tup);  /* implicit update of index as well */

		caql_endscan(pcqCtx);
		is_update = false;
	}

	retval = HeapTupleGetOid(tup);

	/*
	 * Create dependencies for the new function.  If we are updating an
	 * existing function, first delete any existing pg_depend entries.
	 */
	if (is_update)
	{
		deleteDependencyRecordsFor(ProcedureRelationId, retval);
		deleteSharedDependencyRecordsFor(ProcedureRelationId, retval);
		deleteProcCallbacks(retval);
	}

	myself.classId = ProcedureRelationId;
	myself.objectId = retval;
	myself.objectSubId = 0;

	/* dependency on namespace */
	referenced.classId = NamespaceRelationId;
	referenced.objectId = procNamespace;
	referenced.objectSubId = 0;
	recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);

	/* dependency on implementation language */
	referenced.classId = LanguageRelationId;
	referenced.objectId = languageObjectId;
	referenced.objectSubId = 0;
	recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);

	/* dependency on return type */
	referenced.classId = TypeRelationId;
	referenced.objectId = returnType;
	referenced.objectSubId = 0;
	recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);

	/* dependency on describe function */
	if (OidIsValid(describeFuncOid))
	{
		referenced.classId = ProcedureRelationId;
		referenced.objectId = describeFuncOid;
		referenced.objectSubId = 0;
		recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);
		addProcCallback(retval, describeFuncOid, PROMETHOD_DESCRIBE);
	}

	/* dependency on parameter types */
	for (i = 0; i < allParamCount; i++)
	{
		referenced.classId = TypeRelationId;
		referenced.objectId = allParams[i];
		referenced.objectSubId = 0;
		recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);
	}

	/* dependency on owner */
	recordDependencyOnOwner(ProcedureRelationId, retval, GetUserId());

	heap_freetuple(tup);

	heap_close(rel, RowExclusiveLock);

	/* Verify function body */
	if (OidIsValid(languageValidator))
	{
		/* Advance command counter so new tuple can be seen by validator */
		CommandCounterIncrement();
		OidFunctionCall1(languageValidator, ObjectIdGetDatum(retval));
	}

	return retval;
}



/*
 * Validator for internal functions
 *
 * Check that the given internal function name (the "prosrc" value) is
 * a known builtin function.
 */
Datum
fmgr_internal_validator(PG_FUNCTION_ARGS)
{
	Oid			funcoid = PG_GETARG_OID(0);
	bool		isnull;
	char	   *prosrc;
	int			fetchCount;

	if (!CheckFunctionValidatorAccess(fcinfo->flinfo->fn_oid, funcoid))
		PG_RETURN_VOID();

	/*
	 * We do not honor check_function_bodies since it's unlikely the function
	 * name will be found later if it isn't there now.
	 */

	prosrc = caql_getcstring_plus(
					NULL,
					&fetchCount,
					&isnull,
					cql("SELECT prosrc FROM pg_proc "
						" WHERE oid = :1 ",
						ObjectIdGetDatum(funcoid)));

	if (!fetchCount)
		elog(ERROR, "cache lookup failed for function %u", funcoid);

	if (isnull)
		elog(ERROR, "null prosrc");

	if (fmgr_internal_function(prosrc) == InvalidOid)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_FUNCTION),
				 errmsg("there is no built-in function named \"%s\"",
						prosrc)));

	PG_RETURN_VOID();
}



/*
 * Validator for C language functions
 *
 * Make sure that the library file exists, is loadable, and contains
 * the specified link symbol. Also check for a valid function
 * information record.
 */
Datum
fmgr_c_validator(PG_FUNCTION_ARGS)
{
	Oid			funcoid = PG_GETARG_OID(0);
	void	   *libraryhandle;
	HeapTuple	tuple;
	bool		isnull;
	Datum		tmp;
	char	   *prosrc;
	char	   *probin;
	cqContext  *pcqCtx;

	if (!CheckFunctionValidatorAccess(fcinfo->flinfo->fn_oid, funcoid))
		PG_RETURN_VOID();

	/*
	 * It'd be most consistent to skip the check if !check_function_bodies,
	 * but the purpose of that switch is to be helpful for pg_dump loading,
	 * and for pg_dump loading it's much better if we *do* check.
	 */

	pcqCtx = caql_beginscan(
			NULL,
			cql("SELECT * FROM pg_proc "
				" WHERE oid = :1 ",
				ObjectIdGetDatum(funcoid)));

	tuple = caql_getnext(pcqCtx);

	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for function %u", funcoid);

	tmp = caql_getattr(pcqCtx, Anum_pg_proc_prosrc, &isnull);
	if (isnull)
		elog(ERROR, "null prosrc");
	prosrc = DatumGetCString(DirectFunctionCall1(textout, tmp));

	tmp = caql_getattr(pcqCtx, Anum_pg_proc_probin, &isnull);
	if (isnull)
		elog(ERROR, "null probin");
	probin = DatumGetCString(DirectFunctionCall1(textout, tmp));

	(void) load_external_function(probin, prosrc, true, &libraryhandle);
	(void) fetch_finfo_record(libraryhandle, prosrc);

	caql_endscan(pcqCtx);

	PG_RETURN_VOID();
}


/*
 * Validator for SQL language functions
 *
 * Parse it here in order to be sure that it contains no syntax errors.
 */
Datum
fmgr_sql_validator(PG_FUNCTION_ARGS)
{
	Oid			funcoid = PG_GETARG_OID(0);
	HeapTuple	tuple;
	Form_pg_proc proc;
	List	   *querytree_list;
	bool		isnull;
	Datum		tmp;
	char	   *prosrc;
	ErrorContextCallback sqlerrcontext;
	bool		haspolyarg;
	int			i;
	cqContext  *pcqCtx;

	if (!CheckFunctionValidatorAccess(fcinfo->flinfo->fn_oid, funcoid))
		PG_RETURN_VOID();

	pcqCtx = caql_beginscan(
			NULL,
			cql("SELECT * FROM pg_proc "
				" WHERE oid = :1 ",
				ObjectIdGetDatum(funcoid)));

	tuple = caql_getnext(pcqCtx);

	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for function %u", funcoid);
	proc = (Form_pg_proc) GETSTRUCT(tuple);

	/* Disallow pseudotype result */
	/* except for RECORD, VOID, ANYARRAY, or ANYELEMENT */
	if (get_typtype(proc->prorettype) == 'p' &&
		proc->prorettype != RECORDOID &&
		proc->prorettype != VOIDOID &&
		proc->prorettype != ANYARRAYOID &&
		proc->prorettype != ANYELEMENTOID)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("SQL functions cannot return type %s",
						format_type_be(proc->prorettype))));

	/* Disallow pseudotypes in arguments */
	/* except for ANYARRAY or ANYELEMENT */
	haspolyarg = false;
	for (i = 0; i < proc->pronargs; i++)
	{
		if (get_typtype(proc->proargtypes.values[i]) == 'p')
		{
			if (proc->proargtypes.values[i] == ANYARRAYOID ||
				proc->proargtypes.values[i] == ANYELEMENTOID)
				haspolyarg = true;
			else
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("SQL functions cannot have arguments of type %s",
							format_type_be(proc->proargtypes.values[i]))));
		}
	}

	/* Postpone body checks if !check_function_bodies */
	if (check_function_bodies)
	{
		tmp = caql_getattr(pcqCtx, Anum_pg_proc_prosrc, &isnull);
		if (isnull)
			elog(ERROR, "null prosrc");

		prosrc = TextDatumGetCString(tmp);

		/*
		 * Setup error traceback support for ereport().
		 */
		sqlerrcontext.callback = sql_function_parse_error_callback;
		sqlerrcontext.arg = tuple;
		sqlerrcontext.previous = error_context_stack;
		error_context_stack = &sqlerrcontext;

		/*
		 * We can't do full prechecking of the function definition if there
		 * are any polymorphic input types, because actual datatypes of
		 * expression results will be unresolvable.  The check will be done at
		 * runtime instead.
		 *
		 * We can run the text through the raw parser though; this will at
		 * least catch silly syntactic errors.
		 */
		if (!haspolyarg)
		{
			querytree_list = pg_parse_and_rewrite(prosrc,
												  proc->proargtypes.values,
												  proc->pronargs);
			(void) check_sql_fn_retval(funcoid, proc->prorettype,
									   querytree_list, NULL);
		}
		else
			querytree_list = pg_parse_query(prosrc);

		error_context_stack = sqlerrcontext.previous;
	}

	caql_endscan(pcqCtx);

	PG_RETURN_VOID();
}

/*
 * Error context callback for handling errors in SQL function definitions
 */
static void
sql_function_parse_error_callback(void *arg)
{
	HeapTuple	tuple = (HeapTuple) arg;
	Form_pg_proc proc = (Form_pg_proc) GETSTRUCT(tuple);
	bool		isnull;
	Datum		tmp;
	char	   *prosrc;

	/* See if it's a syntax error; if so, transpose to CREATE FUNCTION */
	tmp = SysCacheGetAttr(PROCOID, tuple, Anum_pg_proc_prosrc, &isnull);
	if (isnull)
		elog(ERROR, "null prosrc");
	prosrc = DatumGetCString(DirectFunctionCall1(textout, tmp));

	if (!function_parse_error_transpose(prosrc))
	{
		/* If it's not a syntax error, push info onto context stack */
		errcontext("SQL function \"%s\"", NameStr(proc->proname));
	}

	pfree(prosrc);
}

/*
 * Adjust a syntax error occurring inside the function body of a CREATE
 * FUNCTION command.  This can be used by any function validator, not only
 * for SQL-language functions.	It is assumed that the syntax error position
 * is initially relative to the function body string (as passed in).  If
 * possible, we adjust the position to reference the original CREATE command;
 * if we can't manage that, we set up an "internal query" syntax error instead.
 *
 * Returns true if a syntax error was processed, false if not.
 */
bool
function_parse_error_transpose(const char *prosrc)
{
	int			origerrposition;
	int			newerrposition;
	const char *queryText;

	/*
	 * Nothing to do unless we are dealing with a syntax error that has a
	 * cursor position.
	 *
	 * Some PLs may prefer to report the error position as an internal error
	 * to begin with, so check that too.
	 */
	origerrposition = geterrposition();
	if (origerrposition <= 0)
	{
		origerrposition = getinternalerrposition();
		if (origerrposition <= 0)
			return false;
	}

	/* We can get the original query text from the active portal (hack...) */
	/* Assert(ActivePortal && PortalGetStatus(ActivePortal) == PORTAL_ACTIVE); */
	queryText = ActivePortal->sourceText;

	/* Try to locate the prosrc in the original text */
	newerrposition = match_prosrc_to_query(prosrc, queryText, origerrposition);

	if (newerrposition > 0)
	{
		/* Successful, so fix error position to reference original query */
		errposition(newerrposition);
		/* Get rid of any report of the error as an "internal query" */
		internalerrposition(0);
		internalerrquery(NULL);
	}
	else
	{
		/*
		 * If unsuccessful, convert the position to an internal position
		 * marker and give the function text as the internal query.
		 */
		errposition(0);
		internalerrposition(origerrposition);
		internalerrquery(prosrc);
	}

	return true;
}

/*
 * Try to locate the string literal containing the function body in the
 * given text of the CREATE FUNCTION command.  If successful, return the
 * character (not byte) index within the command corresponding to the
 * given character index within the literal.  If not successful, return 0.
 */
static int
match_prosrc_to_query(const char *prosrc, const char *queryText,
					  int cursorpos)
{
	/*
	 * Rather than fully parsing the CREATE FUNCTION command, we just scan the
	 * command looking for $prosrc$ or 'prosrc'.  This could be fooled (though
	 * not in any very probable scenarios), so fail if we find more than one
	 * match.
	 */
	int			prosrclen = strlen(prosrc);
	int			querylen = strlen(queryText);
	int			matchpos = 0;
	int			curpos;
	int			newcursorpos;

	for (curpos = 0; curpos < querylen - prosrclen; curpos++)
	{
		if (queryText[curpos] == '$' &&
			strncmp(prosrc, &queryText[curpos + 1], prosrclen) == 0 &&
			queryText[curpos + 1 + prosrclen] == '$')
		{
			/*
			 * Found a $foo$ match.  Since there are no embedded quoting
			 * characters in a dollar-quoted literal, we don't have to do any
			 * fancy arithmetic; just offset by the starting position.
			 */
			if (matchpos)
				return 0;		/* multiple matches, fail */
			matchpos = pg_mbstrlen_with_len(queryText, curpos + 1)
				+ cursorpos;
		}
		else if (queryText[curpos] == '\'' &&
				 match_prosrc_to_literal(prosrc, &queryText[curpos + 1],
										 cursorpos, &newcursorpos))
		{
			/*
			 * Found a 'foo' match.  match_prosrc_to_literal() has adjusted
			 * for any quotes or backslashes embedded in the literal.
			 */
			if (matchpos)
				return 0;		/* multiple matches, fail */
			matchpos = pg_mbstrlen_with_len(queryText, curpos + 1)
				+ newcursorpos;
		}
	}

	return matchpos;
}

/*
 * Try to match the given source text to a single-quoted literal.
 * If successful, adjust newcursorpos to correspond to the character
 * (not byte) index corresponding to cursorpos in the source text.
 *
 * At entry, literal points just past a ' character.  We must check for the
 * trailing quote.
 */
static bool
match_prosrc_to_literal(const char *prosrc, const char *literal,
						int cursorpos, int *newcursorpos)
{
	int			newcp = cursorpos;
	int			chlen;

	/*
	 * This implementation handles backslashes and doubled quotes in the
	 * string literal.	It does not handle the SQL syntax for literals
	 * continued across line boundaries.
	 *
	 * We do the comparison a character at a time, not a byte at a time, so
	 * that we can do the correct cursorpos math.
	 */
	while (*prosrc)
	{
		cursorpos--;			/* characters left before cursor */

		/*
		 * Check for backslashes and doubled quotes in the literal; adjust
		 * newcp when one is found before the cursor.
		 */
		if (*literal == '\\')
		{
			literal++;
			if (cursorpos > 0)
				newcp++;
		}
		else if (*literal == '\'')
		{
			if (literal[1] != '\'')
				goto fail;
			literal++;
			if (cursorpos > 0)
				newcp++;
		}
		chlen = pg_mblen(prosrc);
		if (strncmp(prosrc, literal, chlen) != 0)
			goto fail;
		prosrc += chlen;
		literal += chlen;
	}

	if (*literal == '\'' && literal[1] != '\'')
	{
		/* success */
		*newcursorpos = newcp;
		return true;
	}

fail:
	/* Must set *newcursorpos to suppress compiler warning */
	*newcursorpos = newcp;
	return false;
}
