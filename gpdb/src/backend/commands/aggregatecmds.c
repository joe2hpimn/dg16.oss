/*-------------------------------------------------------------------------
 *
 * aggregatecmds.c
 *
 *	  Routines for aggregate-manipulation commands
 *
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/commands/aggregatecmds.c,v 1.41 2006/10/04 00:29:50 momjian Exp $
 *
 * DESCRIPTION
 *	  The "DefineFoo" routines take the parse tree and pick out the
 *	  appropriate arguments/flags, passing the results to the
 *	  corresponding "FooDefine" routines (in src/catalog) that do
 *	  the actual catalog-munging.  These routines also verify permission
 *	  of the user to execute the command.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/heapam.h"
#include "catalog/catquery.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/pg_aggregate.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "commands/defrem.h"
#include "miscadmin.h"
#include "parser/parse_func.h"
#include "parser/parse_type.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

#include "cdb/cdbvars.h"
#include "cdb/cdbdisp.h"

/*
 *	DefineAggregate
 *
 * "oldstyle" signals the old (pre-8.2) style where the aggregate input type
 * is specified by a BASETYPE element in the parameters.  Otherwise,
 * "args" defines the input type(s).
 */
void
DefineAggregate(List *name, List *args, bool oldstyle, List *parameters, 
				Oid newOid, bool ordered)
{
	char	   *aggName;
	Oid			aggNamespace;
	AclResult	aclresult;
	List	   *transfuncName = NIL;
	List	   *prelimfuncName = NIL; /* MPP */
	List	   *finalfuncName = NIL;
	List	   *sortoperatorName = NIL;
	TypeName   *baseType = NULL;
	TypeName   *transType = NULL;
	char	   *initval = NULL;
	Oid		   *aggArgTypes;
	int			numArgs;
	Oid			transTypeId;
	ListCell   *pl;
	Oid			aggOid;
	bool need_free_value = false;

	/* Convert list of names to a name and namespace */
	aggNamespace = QualifiedNameGetCreationNamespace(name, &aggName);

	/* Check we have creation rights in target namespace */
	aclresult = pg_namespace_aclcheck(aggNamespace, GetUserId(), ACL_CREATE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error(aclresult, ACL_KIND_NAMESPACE,
					   get_namespace_name(aggNamespace));

	foreach(pl, parameters)
	{
		DefElem    *defel = (DefElem *) lfirst(pl);

		/*
		 * sfunc1, stype1, and initcond1 are accepted as obsolete spellings
		 * for sfunc, stype, initcond.
		 */
		if (pg_strcasecmp(defel->defname, "sfunc") == 0)
			transfuncName = defGetQualifiedName(defel);
		else if (pg_strcasecmp(defel->defname, "sfunc1") == 0)
			transfuncName = defGetQualifiedName(defel);
		else if (pg_strcasecmp(defel->defname, "finalfunc") == 0)
			finalfuncName = defGetQualifiedName(defel);
		else if (pg_strcasecmp(defel->defname, "sortop") == 0)
			sortoperatorName = defGetQualifiedName(defel);
		else if (pg_strcasecmp(defel->defname, "basetype") == 0)
			baseType = defGetTypeName(defel);
		else if (pg_strcasecmp(defel->defname, "stype") == 0)
			transType = defGetTypeName(defel);
		else if (pg_strcasecmp(defel->defname, "stype1") == 0)
			transType = defGetTypeName(defel);
		else if (pg_strcasecmp(defel->defname, "initcond") == 0)
			initval = defGetString(defel, &need_free_value);
		else if (pg_strcasecmp(defel->defname, "initcond1") == 0)
			initval = defGetString(defel, &need_free_value);
		else if (pg_strcasecmp(defel->defname, "prefunc") == 0) /* MPP */
			prelimfuncName = defGetQualifiedName(defel);
		else if (gp_upgrade_mode && pg_strcasecmp(defel->defname, "oid") == 0) /* OID */
		{
			int64 oid = defGetInt64(defel);
			Assert(oid < FirstBootstrapObjectId);
			newOid = (Oid)oid;
		}
		else
			ereport(WARNING,
					(errcode(ERRCODE_SYNTAX_ERROR),
					 errmsg("aggregate attribute \"%s\" not recognized",
							defel->defname)));
	}

	/*
	 * make sure we have our required definitions
	 */
	if (transType == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("aggregate stype must be specified")));
	if (transfuncName == NIL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("aggregate sfunc must be specified")));

	/*
	 * MPP: Ordered aggregates do not support prefuncs
	 */
	if (ordered && prelimfuncName != NIL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("ordered aggregate prefunc is not supported")));

	/*
	 * look up the aggregate's input datatype(s).
	 */
	if (oldstyle)
	{
		/*
		 * Old style: use basetype parameter.  This supports aggregates of
		 * zero or one input, with input type ANY meaning zero inputs.
		 *
		 * Historically we allowed the command to look like basetype = 'ANY'
		 * so we must do a case-insensitive comparison for the name ANY. Ugh.
		 */
		if (baseType == NULL)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("aggregate input type must be specified")));

		if (pg_strcasecmp(TypeNameToString(baseType), "ANY") == 0)
		{
			numArgs = 0;
			aggArgTypes = NULL;
		}
		else
		{
			numArgs = 1;
			aggArgTypes = (Oid *) palloc(sizeof(Oid));
			aggArgTypes[0] = typenameTypeId(NULL, baseType);
		}
	}
	else
	{
		/*
		 * New style: args is a list of TypeNames (possibly zero of 'em).
		 */
		ListCell   *lc;
		int			i = 0;

		if (baseType != NULL)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
					 errmsg("basetype is redundant with aggregate input type specification")));

		numArgs = list_length(args);
		aggArgTypes = (Oid *) palloc(sizeof(Oid) * numArgs);
		foreach(lc, args)
		{
			TypeName   *curTypeName = (TypeName *) lfirst(lc);

			aggArgTypes[i++] = typenameTypeId(NULL, curTypeName);
		}
	}

	/*
	 * look up the aggregate's transtype.
	 *
	 * transtype can't be a pseudo-type, (except during upgrade mode)
	 * since we need to be able to store values of the transtype.
	 * However, we can allow polymorphic transtype in some cases
	 * (AggregateCreate will check).
	 */
	transTypeId = typenameTypeId(NULL, transType);
	if (!gp_upgrade_mode &&
		(get_typtype(transTypeId) == 'p' &&
		 transTypeId != ANYARRAYOID &&
		 transTypeId != ANYELEMENTOID))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FUNCTION_DEFINITION),
				 errmsg("aggregate transition data type cannot be %s",
						format_type_be(transTypeId))));

	/*
	 * Most of the argument-checking is done inside of AggregateCreate
	 */
	aggOid = AggregateCreateWithOid(aggName,		/* aggregate name */
									aggNamespace,	/* namespace */
									aggArgTypes,	/* input data type(s) */
									numArgs,
									transfuncName,	/* step function name */
									prelimfuncName,	/* prelim function name */
									finalfuncName,	/* final function name */
									sortoperatorName, /* sort operator name */
									transTypeId,	/* transition data type */
									initval,		/* initial condition */
									ordered,		/* ordered aggregates */
									newOid);
					
	if (Gp_role == GP_ROLE_DISPATCH)
	{
		DefineStmt * stmt = makeNode(DefineStmt);
		stmt->kind = OBJECT_AGGREGATE;
		stmt->oldstyle = oldstyle;  
		stmt->defnames = name;
		stmt->args = args;
		stmt->definition = parameters;
		stmt->newOid = aggOid;
		stmt->shadowOid = 0;
		stmt->ordered = ordered;
		CdbDispatchUtilityStatement((Node *) stmt, "DefineAggregate");
	}
}


/*
 * RemoveAggregate
 *		Deletes an aggregate.
 */
void
RemoveAggregate(RemoveFuncStmt *stmt)
{
	List	   *aggName = stmt->name;
	List	   *aggArgs = stmt->args;
	Oid			procOid;
	Oid			namespaceOid;
	ObjectAddress object;

	/* Look up function and make sure it's an aggregate */
	procOid = LookupAggNameTypeNames(aggName, aggArgs, stmt->missing_ok);

	if (!OidIsValid(procOid))
	{
		/* we only get here if stmt->missing_ok is true */
		ereport(NOTICE,
				(errmsg("aggregate %s(%s) does not exist, skipping",
						NameListToString(aggName),
						TypeNameListToString(aggArgs))));
		return;
	}

	/*
	 * Find the function tuple, do permissions and validity checks
	 */
	namespaceOid = get_func_namespace(procOid);
	
	if (!OidIsValid(namespaceOid)) /* should not happen */
		elog(ERROR, "cache lookup failed for function %u", procOid);

	/* Permission check: must own agg or its namespace */
	if (!pg_proc_ownercheck(procOid, GetUserId()) &&
		!pg_namespace_ownercheck(namespaceOid,
							   GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_PROC,
					   NameListToString(aggName));


	/*
	 * Do the deletion
	 */
	object.classId = ProcedureRelationId;
	object.objectId = procOid;
	object.objectSubId = 0;

	performDeletion(&object, stmt->behavior);
	
	if (Gp_role == GP_ROLE_DISPATCH)
	{
		CdbDispatchUtilityStatement((Node *) stmt, "RemoveAggregate");
	}
}


void
RenameAggregate(List *name, List *args, const char *newname)
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

	/* Look up function and make sure it's an aggregate */
	procOid = LookupAggNameTypeNames(name, args, false);

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

	/* rename */
	namestrcpy(&(((Form_pg_proc) GETSTRUCT(tup))->proname), newname);
	caql_update_current(pcqCtx, tup); /* implicit update of index as well */

	heap_close(rel, NoLock);
	heap_freetuple(tup);
}

/*
 * Change aggregate owner
 */
void
AlterAggregateOwner(List *name, List *args, Oid newOwnerId)
{
	Oid			procOid;

	/* Look up function and make sure it's an aggregate */
	procOid = LookupAggNameTypeNames(name, args, false);

	/* The rest is just like a function */
	AlterFunctionOwner_oid(procOid, newOwnerId);
}
