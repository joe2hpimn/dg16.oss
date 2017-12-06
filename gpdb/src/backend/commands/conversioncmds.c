/*-------------------------------------------------------------------------
 *
 * conversioncmds.c
 *	  conversion creation command support code
 *
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/commands/conversioncmds.c,v 1.29 2006/07/14 14:52:18 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/heapam.h"
#include "catalog/catquery.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/pg_conversion.h"
#include "catalog/pg_type.h"
#include "commands/conversioncmds.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "parser/parse_func.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

#include "cdb/cdbvars.h"
#include "cdb/cdbdisp.h"

static void AlterConversionOwner_internal(Relation rel, Oid conversionOid,
							  Oid newOwnerId);

/*
 * CREATE CONVERSION
 */
void
CreateConversionCommand(CreateConversionStmt *stmt)
{
	Oid			namespaceId;
	char	   *conversion_name;
	AclResult	aclresult;
	int			from_encoding;
	int			to_encoding;
	Oid			funcoid;
	const char *from_encoding_name = stmt->for_encoding_name;
	const char *to_encoding_name = stmt->to_encoding_name;
	List	   *func_name = stmt->func_name;
	static Oid	funcargs[] = {INT4OID, INT4OID, CSTRINGOID, INTERNALOID, INT4OID};
	char		result[1];

	/* Convert list of names to a name and namespace */
	namespaceId = QualifiedNameGetCreationNamespace(stmt->conversion_name,
													&conversion_name);

	/* Check we have creation rights in target namespace */
	aclresult = pg_namespace_aclcheck(namespaceId, GetUserId(), ACL_CREATE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error(aclresult, ACL_KIND_NAMESPACE,
					   get_namespace_name(namespaceId));

	/* Check the encoding names */
	from_encoding = pg_char_to_encoding(from_encoding_name);
	if (from_encoding < 0)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("source encoding \"%s\" does not exist",
						from_encoding_name)));

	to_encoding = pg_char_to_encoding(to_encoding_name);
	if (to_encoding < 0)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("destination encoding \"%s\" does not exist",
						to_encoding_name)));

	/*
	 * Check the existence of the conversion function. Function name could be
	 * a qualified name.
	 */
	funcoid = LookupFuncName(func_name, sizeof(funcargs) / sizeof(Oid),
							 funcargs, false);

	/* Check it returns VOID, else it's probably the wrong function */
	if (get_func_rettype(funcoid) != VOIDOID)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("encoding conversion function %s must return type \"void\"",
						NameListToString(func_name))));

	/* Check we have EXECUTE rights for the function */
	aclresult = pg_proc_aclcheck(funcoid, GetUserId(), ACL_EXECUTE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error(aclresult, ACL_KIND_PROC,
					   NameListToString(func_name));

	/*
	 * Check that the conversion function is suitable for the requested
	 * source and target encodings. We do that by calling the function with
	 * an empty string; the conversion function should throw an error if it
	 * can't perform the requested conversion.
	 */
	OidFunctionCall5(funcoid,
					 Int32GetDatum(from_encoding),
					 Int32GetDatum(to_encoding),
					 CStringGetDatum(""),
					 CStringGetDatum(result),
					 Int32GetDatum(0));
	/*
	 * All seem ok, go ahead (possible failure would be a duplicate conversion
	 * name)
	 */
	stmt->convOid = ConversionCreate(conversion_name, namespaceId, GetUserId(),
					 from_encoding, to_encoding, funcoid, stmt->def, stmt->convOid);
					 
	if (Gp_role == GP_ROLE_DISPATCH)
	{
		CdbDispatchUtilityStatement((Node *) stmt, "CreateConversionCommand");
	}
}

/*
 * DROP CONVERSION
 */
void
DropConversionCommand(List *name, DropBehavior behavior, bool missing_ok)
{
	Oid			conversionOid;

	conversionOid = FindConversionByName(name);
	if (!OidIsValid(conversionOid))
	{
		if (!missing_ok)
		{
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("conversion \"%s\" does not exist",
							NameListToString(name))));
		}
		else
		{
			if (Gp_role != GP_ROLE_EXECUTE)
			ereport(NOTICE,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("conversion \"%s\" does not exist, skipping",
							NameListToString(name))));
		}

		return;
	}

	ConversionDrop(conversionOid, behavior);
	
	if (Gp_role == GP_ROLE_DISPATCH)
	{
		DropStmt * stmt = makeNode(DropStmt);
		stmt->objects = list_make1(name);
		stmt->removeType = OBJECT_CONVERSION;
		stmt->behavior=behavior;
		stmt->missing_ok = true;
		CdbDispatchUtilityStatement((Node *) stmt, "DropConversionCommand");
	}
}

/*
 * Rename conversion
 */
void
RenameConversion(List *name, const char *newname)
{
	Oid			conversionOid;
	Oid			namespaceOid;
	HeapTuple	tup;
	Relation	rel;
	AclResult	aclresult;
	cqContext	cqc2;
	cqContext	cqc;
	cqContext  *pcqCtx;

	rel = heap_open(ConversionRelationId, RowExclusiveLock);

	conversionOid = FindConversionByName(name);
	if (!OidIsValid(conversionOid))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("conversion \"%s\" does not exist",
						NameListToString(name))));

	pcqCtx = caql_addrel(cqclr(&cqc), rel);

	tup = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_conversion "
				" WHERE oid = :1 "
				" FOR UPDATE ",
				ObjectIdGetDatum(conversionOid)));

	if (!HeapTupleIsValid(tup)) /* should not happen */
		elog(ERROR, "cache lookup failed for conversion %u", conversionOid);

	namespaceOid = ((Form_pg_conversion) GETSTRUCT(tup))->connamespace;

	/* make sure the new name doesn't exist */
	if (caql_getcount(
				caql_addrel(cqclr(&cqc2), rel),
				cql("SELECT COUNT(*) FROM pg_conversion "
					" WHERE conname = :1 "
					" AND connamespace = :2 ",
					CStringGetDatum((char *) newname),
					ObjectIdGetDatum(namespaceOid))))
	{
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("conversion \"%s\" already exists in schema \"%s\"",
						newname, get_namespace_name(namespaceOid))));
	}

	/* must be owner */
	if (!pg_conversion_ownercheck(conversionOid, GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_CONVERSION,
					   NameListToString(name));

	/* must have CREATE privilege on namespace */
	aclresult = pg_namespace_aclcheck(namespaceOid, GetUserId(), ACL_CREATE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error(aclresult, ACL_KIND_NAMESPACE,
					   get_namespace_name(namespaceOid));

	/* rename */
	namestrcpy(&(((Form_pg_conversion) GETSTRUCT(tup))->conname), newname);
	caql_update_current(pcqCtx, tup); /* implicit update of index as well */

	heap_close(rel, NoLock);
	heap_freetuple(tup);
}

/*
 * Change conversion owner, by name
 */
void
AlterConversionOwner(List *name, Oid newOwnerId)
{
	Oid			conversionOid;
	Relation	rel;

	rel = heap_open(ConversionRelationId, RowExclusiveLock);

	conversionOid = FindConversionByName(name);
	if (!OidIsValid(conversionOid))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("conversion \"%s\" does not exist",
						NameListToString(name))));

	AlterConversionOwner_internal(rel, conversionOid, newOwnerId);

	heap_close(rel, NoLock);
}

/*
 * Change conversion owner, by oid
 */
void
AlterConversionOwner_oid(Oid conversionOid, Oid newOwnerId)
{
	Relation	rel;

	rel = heap_open(ConversionRelationId, RowExclusiveLock);

	AlterConversionOwner_internal(rel, conversionOid, newOwnerId);

	heap_close(rel, NoLock);
}

/*
 * AlterConversionOwner_internal
 *
 * Internal routine for changing the owner.  rel must be pg_conversion, already
 * open and suitably locked; it will not be closed.
 */
static void
AlterConversionOwner_internal(Relation rel, Oid conversionOid, Oid newOwnerId)
{
	Form_pg_conversion convForm;
	HeapTuple	tup;
	cqContext	cqc;
	cqContext  *pcqCtx;

	Assert(RelationGetRelid(rel) == ConversionRelationId);

	pcqCtx = caql_addrel(cqclr(&cqc), rel);

	tup = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_conversion "
				" WHERE oid = :1 "
				" FOR UPDATE ",
				ObjectIdGetDatum(conversionOid)));

	if (!HeapTupleIsValid(tup)) /* should not happen */
		elog(ERROR, "cache lookup failed for conversion %u", conversionOid);

	convForm = (Form_pg_conversion) GETSTRUCT(tup);

	/*
	 * If the new owner is the same as the existing owner, consider the
	 * command to have succeeded.  This is for dump restoration purposes.
	 */
	if (convForm->conowner == newOwnerId)
	{
		heap_freetuple(tup);
		return;
	}

	AclResult	aclresult;

	/* Superusers can always do it */
	if (!superuser())
	{
		/* Otherwise, must be owner of the existing object */
		if (!pg_conversion_ownercheck(HeapTupleGetOid(tup), GetUserId()))
				aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_CONVERSION,
							   NameStr(convForm->conname));

		/* Must be able to become new owner */
		check_is_member_of_role(GetUserId(), newOwnerId);

		/* New owner must have CREATE privilege on namespace */
		aclresult = pg_namespace_aclcheck(convForm->connamespace,
										  newOwnerId,
										  ACL_CREATE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, ACL_KIND_NAMESPACE,
						   get_namespace_name(convForm->connamespace));
	}

	/*
	 * Modify the owner --- okay to scribble on tup because it's a copy
	 */
	convForm->conowner = newOwnerId;

	caql_update_current(pcqCtx, tup); /* implicit update of index as well */

	/* Update owner dependency reference */
	changeDependencyOnOwner(ConversionRelationId, conversionOid,
								newOwnerId);

	heap_freetuple(tup);
}
