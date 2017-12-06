/*-------------------------------------------------------------------------
 *
 * aoblkdir.c
 *   This file contains routines to support creation of append-only block
 *   directory tables. This file is identical in functionality to aoseg.c
 *   that exists in the same directory.
 *
 * Copyright (c) 2009, Greenplum Inc.
 *
 * $Id: $
 * $Change: $
 * $DateTime: $
 * $Author: $
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "catalog/pg_opclass.h"
#include "catalog/aoblkdir.h"
#include "catalog/aocatalog.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"

void
AlterTableCreateAoBlkdirTableWithOid(Oid relOid, Oid newOid, Oid newIndexOid,
									 Oid * comptypeOid, bool is_part_child)
{
	Relation	rel;
	TupleDesc	tupdesc;
	IndexInfo  *indexInfo;
	Oid			classObjectId[3];

	/*
	 * Grab an exclusive lock on the target table, which we will NOT release
	 * until end of transaction.  (This is probably redundant in all present
	 * uses...)
	 */
	if (is_part_child)
		rel = heap_open(relOid, NoLock);
	else
		rel = heap_open(relOid, AccessExclusiveLock);

	if (!RelationIsAoRows(rel) && !RelationIsAoCols(rel)) {
		heap_close(rel, NoLock);
		return;
	}

	/* Create a tuple descriptor */
	tupdesc = CreateTemplateTupleDesc(4, false);
	TupleDescInitEntry(tupdesc, (AttrNumber) 1,
					   "segno",
					   INT4OID,
					   -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 2,
					   "columngroup_no",
					   INT4OID,
					   -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 3,
					   "first_row_no",
					   INT8OID,
					   -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 4,
					   "minipage",
					   VARBITOID,
					   -1, 0);

	/*
	 * We don't want any toast columns here.
	 */
	tupdesc->attrs[0]->attstorage = 'p';
	tupdesc->attrs[1]->attstorage = 'p';
	tupdesc->attrs[2]->attstorage = 'p';
    /* TODO (dmeister): In the next line, the index should have been 3. 
     * Therefore the minipage might be toasted.
     */
	tupdesc->attrs[2]->attstorage = 'p'; 

	/*
	 * Create index on segno, first_row_no.
	 */
	indexInfo = makeNode(IndexInfo);
	indexInfo->ii_NumIndexAttrs = 3;
	indexInfo->ii_KeyAttrNumbers[0] = 1;
	indexInfo->ii_KeyAttrNumbers[1] = 2;
	indexInfo->ii_KeyAttrNumbers[2] = 3;
	indexInfo->ii_Expressions = NIL;
	indexInfo->ii_ExpressionsState = NIL;
	indexInfo->ii_Predicate = NIL;
	indexInfo->ii_PredicateState = NIL;
	indexInfo->ii_Unique = true;
	indexInfo->ii_Concurrent = false;
	
	classObjectId[0] = INT4_BTREE_OPS_OID;
	classObjectId[1] = INT4_BTREE_OPS_OID;
	classObjectId[2] = INT8_BTREE_OPS_OID;

	(void) CreateAOAuxiliaryTable(rel,
			"pg_aoblkdir",
			RELKIND_AOBLOCKDIR,
			newOid, newIndexOid, comptypeOid,
			tupdesc, indexInfo, classObjectId);

	heap_close(rel, NoLock);
}

