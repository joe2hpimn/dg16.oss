/*-------------------------------------------------------------------------
*
* aosegfiles.c
*	  routines to support manipulation of the pg_aoseg_<oid> relation
*	  that accompanies each Append Only relation.
*
* Portions Copyright (c) 2008, Greenplum Inc
* Portions Copyright (c) 1996-2006, PostgreSQL Global Development Group
* Portions Copyright (c) 1994, Regents of the University of California
*
*-------------------------------------------------------------------------
*/
#include "postgres.h"

#include "funcapi.h"
#include "miscadmin.h"
#include "access/heapam.h"
#include "access/genam.h"
#include "access/aocssegfiles.h"
#include "access/aosegfiles.h"
#include "access/appendonlytid.h"
#include "catalog/pg_type.h"
#include "catalog/pg_proc.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/gp_fastsequence.h"
#include "cdb/cdbvars.h"
#include "executor/spi.h"
#include "nodes/makefuncs.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/syscache.h"
#include "utils/fmgroids.h"
#include "utils/numeric.h"

static Datum ao_compression_ratio_internal(Oid relid);
static void UpdateFileSegInfo_internal(Relation parentrel,
				  AppendOnlyEntry *aoEntry,
				  int segno,
				  int64 eof,
				  int64 eof_uncompressed,
				  int64 tuples_added,
				  int64 varblocks_added,
				  int64 modcount_added,
				  FileSegInfoState newState);

static void PrintPgaosegAndGprelationNodeEntries(FileSegInfo **allseginfo,
												int totalsegs,
												bool *segmentFileNumMap);

static void CheckAOConsistencyWithGpRelationNode(Snapshot snapshot,
												Relation rel,
												int totalsegs,
												FileSegInfo ** allseginfo);

/* ------------------------------------------------------------------------
 *
 * FUNCTIONS FOR MANIPULATING AND QUERYING AO SEGMENT FILE CATALOG TABLES
 *
 * ------------------------------------------------------------------------
 */

FileSegInfo *
NewFileSegInfo(int segno)
{
	FileSegInfo *fsinfo;

	fsinfo = (FileSegInfo *) palloc0(sizeof(FileSegInfo));
	fsinfo->segno = segno;
	fsinfo->state = AOSEG_STATE_DEFAULT;

	return fsinfo;
}

/*
 * InsertFileSegInfo
 *
 * Adds an entry into the pg_aoseg_*  table for this Append
 * Only relation. Use use frozen_heap_insert so the tuple is
 * frozen on insert.
 *
 * Also insert a new entry to gp_fastsequence for this segment file.
 */
void
InsertInitialSegnoEntry(AppendOnlyEntry *aoEntry,
						int segno)
{
	Relation	pg_aoseg_rel;
	Relation	pg_aoseg_idx;
	TupleDesc	pg_aoseg_dsc;
	HeapTuple	pg_aoseg_tuple = NULL;
	int			natts = 0;
	bool	   *nulls;
	Datum	   *values;

	Assert(aoEntry != NULL);

	InsertFastSequenceEntry(aoEntry->segrelid,
							(int64)segno,
							0);

	pg_aoseg_rel = heap_open(aoEntry->segrelid, RowExclusiveLock);

	pg_aoseg_dsc = RelationGetDescr(pg_aoseg_rel);
	natts = pg_aoseg_dsc->natts;
	nulls = palloc(sizeof(bool) * natts);
	values = palloc0(sizeof(Datum) * natts);
	MemSet(nulls, false, sizeof(char) * natts);

	pg_aoseg_idx = index_open(aoEntry->segidxid, RowExclusiveLock);

	values[Anum_pg_aoseg_segno - 1] = Int32GetDatum(segno);
	values[Anum_pg_aoseg_tupcount - 1] = Float8GetDatum(0);
	values[Anum_pg_aoseg_varblockcount - 1] = Float8GetDatum(0);
	values[Anum_pg_aoseg_eof - 1] = Float8GetDatum(0);
	values[Anum_pg_aoseg_eofuncompressed - 1] = Float8GetDatum(0);
	values[Anum_pg_aoseg_modcount - 1] = Int64GetDatum(0);
	values[Anum_pg_aoseg_state - 1] = Int16GetDatum(AOSEG_STATE_DEFAULT);

	/*
	 * form the tuple and insert it
	 */
	pg_aoseg_tuple = heap_form_tuple(pg_aoseg_dsc, values, nulls);
	if (!HeapTupleIsValid(pg_aoseg_tuple))
		elog(ERROR, "failed to build AO file segment tuple");

	frozen_heap_insert(pg_aoseg_rel, pg_aoseg_tuple);
	CatalogUpdateIndexes(pg_aoseg_rel, pg_aoseg_tuple);

	heap_freetuple(pg_aoseg_tuple);

	index_close(pg_aoseg_idx, RowExclusiveLock);
	heap_close(pg_aoseg_rel, RowExclusiveLock);
}

/*
 * GetFileSegInfo
 *
 * Get the catalog entry for an appendonly (row-oriented) relation from the
 * pg_aoseg_* relation that belongs to the currently used
 * AppendOnly table.
 *
 * If a caller intends to append to this file segment entry they must
 * already hold a relation Append-Only segment file (transaction-scope) lock (tag 
 * LOCKTAG_RELATION_APPENDONLY_SEGMENT_FILE) in order to guarantee
 * stability of the pg_aoseg information on this segment file and exclusive right
 * to append data to the segment file.
 */
FileSegInfo *
GetFileSegInfo(Relation parentrel, AppendOnlyEntry *aoEntry, Snapshot appendOnlyMetaDataSnapshot, int segno)
{

	Relation		pg_aoseg_rel;
	Relation		pg_aoseg_idx;
	TupleDesc		pg_aoseg_dsc;
	HeapTuple		tuple;
	ScanKeyData		key;
	IndexScanDesc	aoscan;
	Datum			eof, eof_uncompressed, tupcount, varbcount, modcount, state;
	bool			isNull;
	FileSegInfo 	*fsinfo;

	/*
	 * Check the pg_aoseg relation to be certain the ao table segment file
	 * is there.
	 */
	pg_aoseg_rel = heap_open(aoEntry->segrelid, AccessShareLock);
	pg_aoseg_dsc = RelationGetDescr(pg_aoseg_rel);
	pg_aoseg_idx = index_open(aoEntry->segidxid, AccessShareLock);

	/*
	 * Setup a scan key to fetch from the index by segno.
	 */
	ScanKeyInit(&key,
				(AttrNumber) Anum_pg_aoseg_segno,
				BTEqualStrategyNumber,
				F_OIDEQ,
				ObjectIdGetDatum(segno));

	aoscan = index_beginscan(pg_aoseg_rel, pg_aoseg_idx, appendOnlyMetaDataSnapshot, 1, &key);

	tuple = index_getnext(aoscan, ForwardScanDirection);

	if (!HeapTupleIsValid(tuple))
	{
        /* This segment file does not have an entry. */
		index_endscan(aoscan);
		index_close(pg_aoseg_idx, AccessShareLock);
		heap_close(pg_aoseg_rel, AccessShareLock);
		return NULL;
	}

	/* Close the index */
	tuple = heap_copytuple(tuple);

	index_endscan(aoscan);
	index_close(pg_aoseg_idx, AccessShareLock);

	Assert(HeapTupleIsValid(tuple));

	fsinfo = (FileSegInfo *) palloc0(sizeof(FileSegInfo));

	/* get the eof */
	eof = fastgetattr(tuple, Anum_pg_aoseg_eof, pg_aoseg_dsc, &isNull);

	if(isNull)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				errmsg("got invalid eof value: NULL")));

	/* get the tupcount */
	tupcount = fastgetattr(tuple, Anum_pg_aoseg_tupcount, pg_aoseg_dsc, &isNull);

	if(isNull)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				errmsg("got invalid tupcount value: NULL")));

	/* get the varblock count */
	varbcount = fastgetattr(tuple, Anum_pg_aoseg_varblockcount, pg_aoseg_dsc, &isNull);

	if(isNull)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				errmsg("got invalid varblockcount value: NULL")));
	
	/* get the modcount */
	modcount = fastgetattr(tuple, Anum_pg_aoseg_modcount, pg_aoseg_dsc, &isNull);

	if(isNull)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				errmsg("got invalid modcount value: NULL")));

	/* get the state */
	state = fastgetattr(tuple, Anum_pg_aoseg_state, pg_aoseg_dsc, &isNull);

	if(isNull)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				errmsg("got invalid state value: NULL")));

	/* get the uncompressed eof */
	eof_uncompressed = fastgetattr(tuple, Anum_pg_aoseg_eofuncompressed, pg_aoseg_dsc, &isNull);
	/*
	 * Confusing: This eof_uncompressed variable is never used.  It appears we only
	 * call fastgetattr to get the isNull value.  this variable "eof_uncompressed" is
	 * not at all the same as fsinfo->eof_uncompressed.
	 */

	if(isNull)
	{
		/*
		 * NULL is allowed. Tables that were created before the release of the
		 * eof_uncompressed catalog column will have a NULL instead of a value.
		 */
		fsinfo->eof_uncompressed = InvalidUncompressedEof;
	}
	else
	{
		fsinfo->eof_uncompressed = (int64)DatumGetFloat8(eof_uncompressed);
	}

	fsinfo->segno = segno;
	fsinfo->eof = (int64)DatumGetFloat8(eof);
	fsinfo->total_tupcount = (int64)DatumGetFloat8(tupcount);
	fsinfo->varblockcount = (int64)DatumGetFloat8(varbcount);
	fsinfo->modcount = (int64)DatumGetInt64(modcount);
	fsinfo->state = (int32)DatumGetInt16(state);

	if (fsinfo->eof < 0)
		ereport(ERROR,
				(errcode(ERRCODE_GP_INTERNAL_ERROR),
				errmsg("Invalid eof " INT64_FORMAT " for relation %s",
					   fsinfo->eof, RelationGetRelationName(parentrel))));

	/* Finish up scan and close appendonly catalog. */
	heap_close(pg_aoseg_rel, AccessShareLock);

	return fsinfo;
}


/*
 * GetAllFileSegInfo
 *
 * Get all catalog entries for an appendonly relation from the
 * pg_aoseg_* relation that belongs to the currently used
 * AppendOnly table. This is basically a physical snapshot that a
 * scanner can use to scan all the data in a local segment database.
 */
FileSegInfo **GetAllFileSegInfo(Relation parentrel,
								AppendOnlyEntry *aoEntry,
								Snapshot appendOnlyMetaDataSnapshot,
								int *totalsegs)
{
	Relation		pg_aoseg_rel;
	FileSegInfo		**result;

	Assert(RelationIsAoRows(parentrel));

	pg_aoseg_rel = heap_open(aoEntry->segrelid, AccessShareLock);

	result = GetAllFileSegInfo_pg_aoseg_rel(
									RelationGetRelationName(parentrel),
									aoEntry,
									pg_aoseg_rel,
									appendOnlyMetaDataSnapshot,
									totalsegs);
	
	heap_close(pg_aoseg_rel, AccessShareLock);

	CheckAOConsistencyWithGpRelationNode(appendOnlyMetaDataSnapshot,
						parentrel,
						*totalsegs,
						result);

	return result;
}

/*
 * The comparison routine that sorts an array of FileSegInfos
 * in the ascending order of the segment number.
 */
static int
aoFileSegInfoCmp(const void *left, const void *right)
{
	FileSegInfo *leftSegInfo = *((FileSegInfo **)left);
	FileSegInfo *rightSegInfo = *((FileSegInfo **)right);
	
	if (leftSegInfo->segno < rightSegInfo->segno)
		return -1;
	
	if (leftSegInfo->segno > rightSegInfo->segno)
		return 1;
	
	return 0;
}

FileSegInfo **GetAllFileSegInfo_pg_aoseg_rel(
								char *relationName,
								AppendOnlyEntry *aoEntry,
								Relation pg_aoseg_rel,
								Snapshot appendOnlyMetaDataSnapshot,
								int *totalsegs)
{
	TupleDesc		pg_aoseg_dsc;
	HeapTuple		tuple;
	HeapScanDesc	aoscan;
	FileSegInfo		**allseginfo;
	int				seginfo_no;
	int				seginfo_slot_no = AO_FILESEGINFO_ARRAY_SIZE;
	Datum			segno,
					eof,
					eof_uncompressed,
					tupcount,
					varblockcount,
					modcount,
					state;
	bool			isNull;
	pg_aoseg_dsc = RelationGetDescr(pg_aoseg_rel);

	/* MPP-16407:
	 * Initialize the segment file information array, we first allocate 8 slot for the array,
	 * then array will be dynamically expanded later if necessary.
	 */
	allseginfo = (FileSegInfo **) palloc0(sizeof(FileSegInfo*) * seginfo_slot_no);
	seginfo_no = 0;

	/*
	 * Now get the actual segfile information
	 */
	aoscan = heap_beginscan(pg_aoseg_rel, appendOnlyMetaDataSnapshot, 0, NULL);
	while ((tuple = heap_getnext(aoscan, ForwardScanDirection)) != NULL)
	{
		/* dynamically expand space for FileSegInfo* array */
		if (seginfo_no >= seginfo_slot_no)
		{
			seginfo_slot_no *= 2;
			allseginfo = (FileSegInfo **) repalloc(allseginfo, sizeof(FileSegInfo*) * seginfo_slot_no);
		}

		FileSegInfo *oneseginfo;
		allseginfo[seginfo_no] = (FileSegInfo *)palloc0(sizeof(FileSegInfo));
		oneseginfo = allseginfo[seginfo_no];

		GetTupleVisibilitySummary(
								tuple,
								&oneseginfo->tupleVisibilitySummary);

		segno = fastgetattr(tuple, Anum_pg_aoseg_segno, pg_aoseg_dsc, &isNull);
		oneseginfo->segno = DatumGetInt32(segno);

		eof = fastgetattr(tuple, Anum_pg_aoseg_eof, pg_aoseg_dsc, &isNull);
		oneseginfo->eof += (int64)DatumGetFloat8(eof);

		tupcount = fastgetattr(tuple, Anum_pg_aoseg_tupcount, pg_aoseg_dsc, &isNull);
		oneseginfo->total_tupcount += (int64)DatumGetFloat8(tupcount);

		varblockcount = fastgetattr(tuple, Anum_pg_aoseg_varblockcount, pg_aoseg_dsc, &isNull);
		oneseginfo->varblockcount += (int64)DatumGetFloat8(varblockcount);

		/*
		 * Modcount cannot be NULL in normal operation. However, when
		 * called from gp_aoseg_history after an upgrade, the old now invisible
		 * entries may have not set the state and the modcount.
		 */
		modcount = fastgetattr(tuple, Anum_pg_aoseg_modcount, pg_aoseg_dsc, &isNull);
		Assert(!isNull || appendOnlyMetaDataSnapshot == SnapshotAny);
		if (!isNull)
			oneseginfo->modcount += (int64)DatumGetInt64(modcount);

		state = fastgetattr(tuple, Anum_pg_aoseg_state, pg_aoseg_dsc, &isNull);
		Assert(!isNull || appendOnlyMetaDataSnapshot == SnapshotAny);
		if (!isNull)
			oneseginfo->state = (int64)DatumGetInt16(state);

		eof_uncompressed = fastgetattr(tuple, Anum_pg_aoseg_eofuncompressed, pg_aoseg_dsc, &isNull);

		if(isNull)
			oneseginfo->eof_uncompressed = InvalidUncompressedEof;
		else
			oneseginfo->eof_uncompressed += (int64)DatumGetFloat8(eof);

		elogif(Debug_appendonly_print_scan, LOG,
				"Append-only found existing segno %d with eof " INT64_FORMAT " for table '%s'",
				oneseginfo->segno,
				oneseginfo->eof,
				relationName);
		seginfo_no++;

		CHECK_FOR_INTERRUPTS();
	}
	heap_endscan(aoscan);

	*totalsegs = seginfo_no;

	if (*totalsegs == 0)
	{
		pfree(allseginfo);
		return NULL;
	}

	/*
	 * Sort allseginfo by the order of segment file number.
	 *
	 * Currently this is only needed when building a bitmap index to guarantee the tids
	 * are in the ascending order. But since this array is pretty small, we just sort
	 * the array for all cases.
	 */
	qsort((char *)allseginfo, *totalsegs, sizeof(FileSegInfo *), aoFileSegInfoCmp);

	return allseginfo;
}

void
SetFileSegInfoState(Relation parentrel,
		AppendOnlyEntry *aoEntry,
		int segno,
		FileSegInfoState newState)
{
	elogif(Debug_appendonly_print_compaction, LOG,
			"Set segfile info state: segno %d, table '%s', new state %d",
			segno,
			RelationGetRelationName(parentrel),
			newState);

	UpdateFileSegInfo_internal(parentrel,
				  aoEntry,
				  segno,
				  -1,
				  -1,
				  0,
				  0,
				  0,
				 newState);
}

void
ClearFileSegInfo(Relation parentrel,
		AppendOnlyEntry *aoEntry,
		int segno,
		FileSegInfoState newState)
{
	LockAcquireResult acquireResult;
	
	Relation			pg_aoseg_rel;
	Relation			pg_aoseg_idx;
	TupleDesc			pg_aoseg_dsc;
	ScanKeyData			key;
	IndexScanDesc		aoscan;
	HeapTuple			tuple, new_tuple;
	Datum			   *new_record;
	bool			   *new_record_nulls;
	bool			   *new_record_repl;

    Assert(RelationIsAoRows(parentrel));
	Assert(newState >= AOSEG_STATE_USECURRENT && newState <= AOSEG_STATE_AWAITING_DROP);

	elogif(Debug_appendonly_print_compaction, LOG,
			"Clear seg file info: segno %d table '%s'",
			segno,
			RelationGetRelationName(parentrel));

	/*
	 * Verify we already have the write-lock!
	 */
	acquireResult = LockRelationAppendOnlySegmentFile(
												&parentrel->rd_node,
												segno,
												AccessExclusiveLock,
												/* dontWait */ false);
	if (acquireResult != LOCKACQUIRE_ALREADY_HELD)
	{
		elog(ERROR, "Should already have the (transaction-scope) write-lock on Append-Only segment file #%d, "
					 "relation %s", segno, RelationGetRelationName(parentrel));
	}

	/*
	 * Open the aoseg relation and its index.
	 */
	pg_aoseg_rel = heap_open(aoEntry->segrelid, RowExclusiveLock);
	pg_aoseg_dsc = RelationGetDescr(pg_aoseg_rel);
	pg_aoseg_idx = index_open(aoEntry->segidxid, RowExclusiveLock);

	/*
	 * Setup a scan key to fetch from the index by segno.
	 */
	ScanKeyInit(&key,
				(AttrNumber) Anum_pg_aoseg_segno,
				BTEqualStrategyNumber,
				F_OIDEQ,
				ObjectIdGetDatum(segno));

	aoscan = index_beginscan(pg_aoseg_rel, pg_aoseg_idx, SnapshotNow, 1, &key);

	tuple = index_getnext(aoscan, ForwardScanDirection);

	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				errmsg("append-only table \"%s\" file segment \"%d\" entry "
						"does not exist", RelationGetRelationName(parentrel),
						segno)));

	new_record = palloc0(sizeof(Datum) * pg_aoseg_dsc->natts);
	new_record_nulls = palloc0(sizeof(bool) * pg_aoseg_dsc->natts);
	new_record_repl = palloc0(sizeof(bool) * pg_aoseg_dsc->natts);
	new_record[Anum_pg_aoseg_eof - 1] = Float8GetDatum(0);
	new_record_repl[Anum_pg_aoseg_eof - 1] = true;
	new_record[Anum_pg_aoseg_tupcount - 1] = Float8GetDatum(0);
	new_record_repl[Anum_pg_aoseg_tupcount - 1] = true;
	new_record[Anum_pg_aoseg_varblockcount - 1] = Float8GetDatum(0);
	new_record_repl[Anum_pg_aoseg_varblockcount - 1] = true;
	new_record[Anum_pg_aoseg_eofuncompressed - 1] = Float8GetDatum(0);
	new_record_repl[Anum_pg_aoseg_eofuncompressed - 1] = true;
	/* We do not reset the modcount here */

	if (newState > 0)
	{
		new_record[Anum_pg_aoseg_state - 1] = Int16GetDatum(newState);
		new_record_repl[Anum_pg_aoseg_state - 1] = true;
	}

	new_tuple = heap_modify_tuple(tuple, pg_aoseg_dsc, new_record,
								new_record_nulls, new_record_repl);

	simple_heap_update(pg_aoseg_rel, &tuple->t_self, new_tuple);
	CatalogUpdateIndexes(pg_aoseg_rel, new_tuple);
	heap_freetuple(new_tuple);

	index_endscan(aoscan);
	index_close(pg_aoseg_idx, RowExclusiveLock);
	heap_close(pg_aoseg_rel, RowExclusiveLock);

	pfree(new_record);
	pfree(new_record_nulls);
	pfree(new_record_repl);
}

/*
 * Update the eof and filetupcount of an append only table.
 *
 * The parameters eof and eof_uncompressed should not be negative.
 * tuples_added and varblocks_added can be negative.
 */
void
UpdateFileSegInfo(Relation parentrel,
				  AppendOnlyEntry *aoEntry,
				  int segno,
				  int64 eof,
				  int64 eof_uncompressed,
				  int64 tuples_added,
				  int64 varblocks_added,
				  int64 modcount_added,
				  FileSegInfoState newState)
{
	Assert(eof >= 0);
	Assert(eof_uncompressed >= 0);

	elog(DEBUG3, "UpdateFileSegInfo called. segno = %d", segno);

	UpdateFileSegInfo_internal(parentrel,
				  aoEntry,
				  segno,
				  eof,
				  eof_uncompressed,
				  tuples_added,
				  varblocks_added,
				  modcount_added,
				  newState);
}

/*
 * Update the eof and filetupcount of an append only table.
 *
 * The parameters eof and eof_uncompressed can be negative.
 * In this case, the columns are not updated. 
 *
 * The parameters tuples_added and varblocks_added can be negative, e.g. after
 * a AO compaction.
 */
static void
UpdateFileSegInfo_internal(Relation parentrel,
				  AppendOnlyEntry *aoEntry,
				  int segno,
				  int64 eof,
				  int64 eof_uncompressed,
				  int64 tuples_added,
				  int64 varblocks_added,
				  int64 modcount_added,
				  FileSegInfoState newState)
{
	LockAcquireResult acquireResult;
	
	Relation			pg_aoseg_rel;
	Relation			pg_aoseg_idx;
	TupleDesc			pg_aoseg_dsc;
	ScanKeyData			key;
	IndexScanDesc		aoscan;
	HeapTuple			tuple, new_tuple;
	Datum				filetupcount;
	Datum				filevarblockcount;
	Datum				new_tuple_count;
	Datum				new_varblock_count;
	Datum               new_modcount;
	Datum               old_eof;
	Datum               old_eof_uncompressed;
	Datum               old_modcount;
	Datum			   *new_record;
	bool			   *new_record_nulls;
	bool			   *new_record_repl;
	bool				isNull;

    Assert(RelationIsAoRows(parentrel));
	Assert(newState >= AOSEG_STATE_USECURRENT && newState <= AOSEG_STATE_AWAITING_DROP);

	/*
	 * Verify we already have the write-lock!
	 */
	acquireResult = LockRelationAppendOnlySegmentFile(
												&parentrel->rd_node,
												segno,
												AccessExclusiveLock,
												/* dontWait */ false);
	if (acquireResult != LOCKACQUIRE_ALREADY_HELD)
	{
		elog(ERROR, "Should already have the (transaction-scope) write-lock on Append-Only segment file #%d, "
					 "relation %s", segno, RelationGetRelationName(parentrel));
	}

	/*
	 * Open the aoseg relation and its index.
	 */
	pg_aoseg_rel = heap_open(aoEntry->segrelid, RowExclusiveLock);
	pg_aoseg_dsc = RelationGetDescr(pg_aoseg_rel);
	pg_aoseg_idx = index_open(aoEntry->segidxid, RowExclusiveLock);

	/*
	 * Setup a scan key to fetch from the index by segno.
	 */
	ScanKeyInit(&key,
				(AttrNumber) Anum_pg_aoseg_segno,
				BTEqualStrategyNumber,
				F_OIDEQ,
				ObjectIdGetDatum(segno));

	aoscan = index_beginscan(pg_aoseg_rel, pg_aoseg_idx, SnapshotNow, 1, &key);

	tuple = index_getnext(aoscan, ForwardScanDirection);

	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				errmsg("append-only table \"%s\" file segment \"%d\" entry "
						"does not exist", RelationGetRelationName(parentrel),
						segno)));

	new_record = palloc0(sizeof(Datum) * pg_aoseg_dsc->natts);
	new_record_nulls = palloc0(sizeof(bool) * pg_aoseg_dsc->natts);
	new_record_repl = palloc0(sizeof(bool) * pg_aoseg_dsc->natts);

	if (eof < 0)
	{
		old_eof = fastgetattr(tuple,
								Anum_pg_aoseg_eof,
								pg_aoseg_dsc,
								&isNull);

		if(isNull)
			ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				errmsg("got invalid pg_aoseg eof value: NULL")));

		eof = DatumGetFloat8(old_eof);
	}

	if (eof_uncompressed < 0)
	{
		old_eof_uncompressed = fastgetattr(tuple,
								Anum_pg_aoseg_eofuncompressed,
								pg_aoseg_dsc,
								&isNull);

		if(isNull)
			ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				errmsg("got invalid pg_aoseg eofuncompressed value: NULL")));

		eof_uncompressed = DatumGetFloat8(old_eof_uncompressed);
	}
	
	/* get the current tuple count so we can add to it */
	filetupcount = fastgetattr(tuple,
								Anum_pg_aoseg_tupcount,
								pg_aoseg_dsc,
								&isNull);

	if(isNull)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				errmsg("got invalid pg_aoseg filetupcount value: NULL")));

	/* calculate the new tuple count */
	new_tuple_count = DirectFunctionCall2(float8pl,
										  filetupcount,
										  Float8GetDatum((float8)tuples_added));
	Assert(DatumGetFloat8(new_tuple_count) > -1.0);

	/* get the current varblock count so we can add to it */
	filevarblockcount = fastgetattr(tuple,
									Anum_pg_aoseg_varblockcount,
									pg_aoseg_dsc,
									&isNull);

	if(isNull)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				errmsg("got invalid pg_aoseg varblockcount value: NULL")));

	/* calculate the new tuple count */
	new_varblock_count = DirectFunctionCall2(float8pl,
											 filevarblockcount,
											 Float8GetDatum((float8)varblocks_added));
	Assert(DatumGetFloat8(new_varblock_count) > -1.0);

	/* get the current modcount so we can add to it */
	old_modcount = fastgetattr(tuple,
									Anum_pg_aoseg_modcount,
									pg_aoseg_dsc,
									&isNull);

	if(isNull)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				errmsg("got invalid pg_aoseg modcount value: NULL")));

	/* calculate the new mod count */
	new_modcount = DirectFunctionCall2(int8pl,
											 old_modcount,
											 Int64GetDatum(modcount_added));
	Assert(DatumGetInt64(new_modcount) >= 0);

	/*
	 * Build a tuple to update
	 */
	new_record[Anum_pg_aoseg_eof - 1] = Float8GetDatum((float8)eof);
	new_record_repl[Anum_pg_aoseg_eof - 1] = true;

	new_record[Anum_pg_aoseg_tupcount - 1] = new_tuple_count;
	new_record_repl[Anum_pg_aoseg_tupcount - 1] = true;

	new_record[Anum_pg_aoseg_varblockcount - 1] = new_varblock_count;
	new_record_repl[Anum_pg_aoseg_varblockcount - 1] = true;

	new_record[Anum_pg_aoseg_modcount - 1] = new_modcount;
	new_record_repl[Anum_pg_aoseg_modcount - 1] = true;

	new_record[Anum_pg_aoseg_eofuncompressed - 1] = Float8GetDatum((float8)eof_uncompressed);
	new_record_repl[Anum_pg_aoseg_eofuncompressed - 1] = true;

	if (newState > 0)
	{
		new_record[Anum_pg_aoseg_state - 1] = Int16GetDatum(newState);
		new_record_repl[Anum_pg_aoseg_state - 1] = true;
	}

	/*
	 * update the tuple in the pg_aoseg table
	 */
	new_tuple = heap_modify_tuple(tuple, pg_aoseg_dsc, new_record,
								new_record_nulls, new_record_repl);

	simple_heap_update(pg_aoseg_rel, &tuple->t_self, new_tuple);
	CatalogUpdateIndexes(pg_aoseg_rel, new_tuple);

	heap_freetuple(new_tuple);

	/* Finish up scan */
	index_endscan(aoscan);
	index_close(pg_aoseg_idx, RowExclusiveLock);
	heap_close(pg_aoseg_rel, RowExclusiveLock);

	pfree(new_record);
	pfree(new_record_nulls);
	pfree(new_record_repl);
}

/*
 * GetSegFilesTotals
 *
 * Get the total bytes, tuples, and varblocks for a specific AO table
 * from the pg_aoseg table on this local segdb.
 */
FileSegTotals *
GetSegFilesTotals(Relation parentrel, Snapshot appendOnlyMetaDataSnapshot)
{

	Relation		pg_aoseg_rel;
	TupleDesc		pg_aoseg_dsc;
	HeapTuple		tuple;
	HeapScanDesc	aoscan;
	FileSegTotals  *result;
	Datum			eof,
					eof_uncompressed,
					tupcount,
					varblockcount,
					state;
	bool			isNull;
	AppendOnlyEntry *aoEntry = NULL;
	
	Assert(RelationIsAoRows(parentrel)); /* doesn't fit for AO column store. should implement same for CO */
	
	aoEntry = GetAppendOnlyEntry(RelationGetRelid(parentrel), appendOnlyMetaDataSnapshot);

	result = (FileSegTotals *) palloc0(sizeof(FileSegTotals));

	pg_aoseg_rel = heap_open(aoEntry->segrelid, AccessShareLock);
	pg_aoseg_dsc = RelationGetDescr(pg_aoseg_rel);

	aoscan = heap_beginscan(pg_aoseg_rel, appendOnlyMetaDataSnapshot, 0, NULL);

	while ((tuple = heap_getnext(aoscan, ForwardScanDirection)) != NULL)
	{
		eof = fastgetattr(tuple, Anum_pg_aoseg_eof, pg_aoseg_dsc, &isNull);
		tupcount = fastgetattr(tuple, Anum_pg_aoseg_tupcount, pg_aoseg_dsc, &isNull);
		varblockcount = fastgetattr(tuple, Anum_pg_aoseg_varblockcount, pg_aoseg_dsc, &isNull);
		eof_uncompressed = fastgetattr(tuple, Anum_pg_aoseg_eofuncompressed, pg_aoseg_dsc, &isNull);
		state = fastgetattr(tuple, Anum_pg_aoseg_state, pg_aoseg_dsc, &isNull);

		if(isNull)
			result->totalbytesuncompressed = InvalidUncompressedEof;
		else
			result->totalbytesuncompressed += (int64)DatumGetFloat8(eof_uncompressed);

		result->totalbytes += (int64)DatumGetFloat8(eof);

		if (DatumGetInt16(state) != AOSEG_STATE_AWAITING_DROP)
		{
			result->totaltuples += (int64)DatumGetFloat8(tupcount);
		}
		result->totalvarblocks += (int64)DatumGetFloat8(varblockcount);
		result->totalfilesegs++;

		CHECK_FOR_INTERRUPTS();
	}

	heap_endscan(aoscan);
	heap_close(pg_aoseg_rel, AccessShareLock);

	pfree(aoEntry);
	
	return result;
}

/*
 * GetAOTotalBytes
 *
 * Get the total bytes for a specific AO table from the pg_aoseg table on this local segdb.
 * (A version of GetSegFilesTotals that just gets the total bytes.)
 */
int64 GetAOTotalBytes(Relation parentrel, Snapshot appendOnlyMetaDataSnapshot)
{

	Relation		pg_aoseg_rel;
	TupleDesc		pg_aoseg_dsc;
	HeapTuple		tuple;
	HeapScanDesc	aoscan;
	int64		  	result;
	Datum			eof;
	bool			isNull;
	AppendOnlyEntry *aoEntry = NULL;

    Assert(RelationIsAoRows(parentrel));

	aoEntry = GetAppendOnlyEntry(RelationGetRelid(parentrel), appendOnlyMetaDataSnapshot);

	result = 0;

	pg_aoseg_rel = heap_open(aoEntry->segrelid, AccessShareLock);
	pg_aoseg_dsc = RelationGetDescr(pg_aoseg_rel);

	aoscan = heap_beginscan(pg_aoseg_rel, appendOnlyMetaDataSnapshot, 0, NULL);

	while ((tuple = heap_getnext(aoscan, ForwardScanDirection)) != NULL)
	{
		eof = fastgetattr(tuple, Anum_pg_aoseg_eof, pg_aoseg_dsc, &isNull);
		Assert(!isNull);

		result += (int64)DatumGetFloat8(eof);

		CHECK_FOR_INTERRUPTS();
	}

	heap_endscan(aoscan);
	heap_close(pg_aoseg_rel, AccessShareLock);

	pfree(aoEntry);

	return result;
}

PG_FUNCTION_INFO_V1(gp_aoseg_history);

extern Datum
gp_aoseg_history(PG_FUNCTION_ARGS);

Datum
gp_aoseg_history(PG_FUNCTION_ARGS)
{
    int aoRelOid = PG_GETARG_OID(0);

	typedef struct Context
	{
		Oid		aoRelOid;

		FileSegInfo **aoSegfileArray;

		int totalAoSegFiles;

		int		segfileArrayIndex;
	} Context;
	
	FuncCallContext *funcctx;
	Context *context;

	if (SRF_IS_FIRSTCALL())
	{
		TupleDesc	tupdesc;
		MemoryContext oldcontext;
		Relation aocsRel;
		AppendOnlyEntry *aoEntry;
		Relation pg_aoseg_rel;

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/*
		 * switch to memory context appropriate for multiple function
		 * calls
		 */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* build tupdesc for result tuples */
		tupdesc = CreateTemplateTupleDesc(17, false);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "gp_tid",
						   TIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "gp_xmin",
						   INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "gp_xmin_status",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 4, "gp_xmin_commit_distrib_id",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 5, "gp_xmax",
						   INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 6, "gp_xmax_status",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 7, "gp_xmax_commit_distrib_id",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 8, "gp_command_id",
						   INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 9, "gp_infomask",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 10, "gp_update_tid",
						   TIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 11, "gp_visibility",
						   TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 12, "segno",
						   INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 13, "tupcount",
						   INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 14, "eof",
						   INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 15, "eof_uncompressed",
						   INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 16, "modcount",
						   INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 17, "state",
						   INT2OID, -1, 0);

		funcctx->tuple_desc = BlessTupleDesc(tupdesc);

		/*
		 * Collect all the locking information that we will format and send
		 * out as a result set.
		 */
		context = (Context *) palloc(sizeof(Context));
		funcctx->user_fctx = (void *) context;

		context->aoRelOid = aoRelOid;

		aocsRel = heap_open(aoRelOid, NoLock);
		if(!RelationIsAoRows(aocsRel))
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("'%s' is not an append-only row relation",
							RelationGetRelationName(aocsRel))));

		aoEntry = GetAppendOnlyEntry(aoRelOid, SnapshotNow);
		
		pg_aoseg_rel = heap_open(aoEntry->segrelid, NoLock);
		
		context->aoSegfileArray = 
				GetAllFileSegInfo_pg_aoseg_rel(
											RelationGetRelationName(aocsRel), 
											aoEntry, 
											pg_aoseg_rel,
											SnapshotAny,	// Get ALL tuples from pg_aoseg_% including aborted and in-progress ones. 
											&context->totalAoSegFiles);

		heap_close(pg_aoseg_rel, NoLock);
		heap_close(aocsRel, NoLock);

		// Iteration position.
		context->segfileArrayIndex = 0;

		funcctx->user_fctx = (void *) context;

		MemoryContextSwitchTo(oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP();
	context = (Context *) funcctx->user_fctx;

	/*
	 * Process each column for each segment file.
	 */
	while (true)
	{
		Datum		values[17];
		bool		nulls[17];
		HeapTuple	tuple;
		Datum		result;

		struct FileSegInfo *aoSegfile;

		if (context->segfileArrayIndex >= context->totalAoSegFiles)
		{
			break;
		}
		
		/*
		 * Form tuple with appropriate data.
		 */
		MemSet(values, 0, sizeof(values));
		MemSet(nulls, false, sizeof(nulls));

		aoSegfile = context->aoSegfileArray[context->segfileArrayIndex];

		GetTupleVisibilitySummaryDatums(
								&values[0],
								&nulls[0],
								&aoSegfile->tupleVisibilitySummary);

		values[11] = Int32GetDatum(aoSegfile->segno);
		values[12] = Int64GetDatum(aoSegfile->total_tupcount);
		values[13] = Int64GetDatum(aoSegfile->eof);
		values[14] = Int64GetDatum(aoSegfile->eof_uncompressed);
		values[15] = Int64GetDatum(aoSegfile->modcount);
		values[16] = Int16GetDatum(aoSegfile->state);

		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);
		result = HeapTupleGetDatum(tuple);

		// Indicate we emitted one segment file.
		context->segfileArrayIndex++;

		SRF_RETURN_NEXT(funcctx, result);
	}

	SRF_RETURN_DONE(funcctx);
}

#define GET_STR(textp) DatumGetCString(DirectFunctionCall1(textout, PointerGetDatum(textp)))

static Datum
gp_update_aorow_master_stats_internal(Relation parentrel, Snapshot appendOnlyMetaDataSnapshot)
{
	StringInfoData 	sqlstmt;
	Relation		aosegrel;
	bool 			connected = false;
	char			aoseg_relname[NAMEDATALEN];
	int 			proc;
	int 			ret;
	int64			total_count = 0;
	MemoryContext 	oldcontext = CurrentMemoryContext;
	AppendOnlyEntry *aoEntry = NULL;
	
    Assert(RelationIsAoRows(parentrel));
	aoEntry = GetAppendOnlyEntry(RelationGetRelid(parentrel), appendOnlyMetaDataSnapshot);
	Assert(aoEntry != NULL);

	/*
	 * get the name of the aoseg relation
	 */
	aosegrel = heap_open(aoEntry->segrelid, AccessShareLock);
	snprintf(aoseg_relname, NAMEDATALEN, "%s", RelationGetRelationName(aosegrel));
	heap_close(aosegrel, AccessShareLock);

	/*
	 * assemble our query string
	 */
	initStringInfo(&sqlstmt);
	appendStringInfo(&sqlstmt, "select segno,sum(tupcount) "
					"from gp_dist_random('pg_aoseg.%s') "
					"group by (segno)", aoseg_relname);


	PG_TRY();
	{

		if (SPI_OK_CONNECT != SPI_connect())
		{
			ereport(ERROR, (errcode(ERRCODE_CDB_INTERNAL_ERROR),
							errmsg("Unable to obtain AO relation information from segment databases."),
							errdetail("SPI_connect failed in gp_update_ao_master_stats")));
		}
		connected = true;

		/* Do the query. */
		ret = SPI_execute(sqlstmt.data, false, 0);
		proc = SPI_processed;


		if (ret > 0 && SPI_tuptable != NULL)
		{
			TupleDesc tupdesc = SPI_tuptable->tupdesc;
			SPITupleTable *tuptable = SPI_tuptable;
			int i;

			/*
			 * Iterate through each result tuple
			 */
			for (i = 0; i < proc; i++)
			{
				HeapTuple 	tuple = tuptable->vals[i];
				FileSegInfo *fsinfo = NULL;
				int			qe_segno;
				int64 		qe_tupcount;
				char 		*val_segno;
				char 		*val_tupcount;
				MemoryContext 	cxt_save;

				/*
				 * Get totals from QE's for a specific segment
				 */
				val_segno = SPI_getvalue(tuple, tupdesc, 1);
				val_tupcount = SPI_getvalue(tuple, tupdesc, 2);

				/* use our own context so that SPI won't free our stuff later */
				cxt_save = MemoryContextSwitchTo(oldcontext);

				/*
				 * Convert to desired data type
				 */
				qe_segno = pg_atoi(val_segno, sizeof(int32), 0);
				qe_tupcount = (int64)DatumGetFloat8(DirectFunctionCall1(float8in,
																 CStringGetDatum(val_tupcount)));


				total_count += qe_tupcount;

				/*
				 * Get the numbers on the QD for this segment
				 */

				
				// CONSIDER: For integrity, we should lock ALL segment files first before 
				// executing the query.  And, the query of the segments (the SPI_execute)
				// and the update (UpdateFileSegInfo) should be in the same transaction.
				//
				// If there are concurrent Append-Only inserts, we can end up with
				// the wrong answer...
				//
				// NOTE: This is a transaction scope lock that must be held until commit / abort.
				//
				LockRelationAppendOnlySegmentFile(
												&parentrel->rd_node,
												qe_segno,
												AccessExclusiveLock,
												/* dontWait */ false);

				fsinfo = GetFileSegInfo(parentrel, aoEntry, appendOnlyMetaDataSnapshot, qe_segno);
				if (fsinfo == NULL)
				{
					InsertInitialSegnoEntry(aoEntry, qe_segno);

					fsinfo = NewFileSegInfo(qe_segno);
				}

				/*
				 * check if numbers match.
				 * NOTE: proper way is to use int8eq() but since we
				 * don't expect any NAN's in here better do it directly
				 */
				if(fsinfo->total_tupcount != qe_tupcount)
				{
					int64 	tupcount_diff = qe_tupcount - fsinfo->total_tupcount;

					elog(DEBUG3, "gp_update_ao_master_stats: updating "
						"segno %d with tupcount %d", qe_segno,
						(int)qe_tupcount);

					/*
					 * QD tup count !=  QE tup count. update QD count by
					 * passing in the diff (may be negative sometimes).
					 */
					UpdateFileSegInfo_internal(parentrel, aoEntry, qe_segno, -1, -1, 
							tupcount_diff, 0, 1, AOSEG_STATE_USECURRENT);
				}
				else
					elog(DEBUG3, "gp_update_ao_master_stats: no need to "
						"update segno %d. it is synced", qe_segno);

				pfree(fsinfo);

				MemoryContextSwitchTo(cxt_save);

				/*
				 * TODO: if an entry exists for this rel in the AO hash table
				 * need to also update that entry in shared memory. Need to
				 * figure out how to do this safely when concurrent operations
				 * are in progress. note that if no entry exists we are ok.
				 *
				 * At this point this doesn't seem too urgent as we generally
				 * only expect this function to update segno 0 only and the QD
				 * never cares about segment 0 anyway.
				 */
			}
		}

		connected = false;
		SPI_finish();
	}

	/* Clean up in case of error. */
	PG_CATCH();
	{
		if (connected)
			SPI_finish();

		/* Carry on with error handling. */
		PG_RE_THROW();
	}
	PG_END_TRY();

	pfree(aoEntry);
	pfree(sqlstmt.data);

	PG_RETURN_FLOAT8((float8)total_count);
}

PG_FUNCTION_INFO_V1(gp_aoseg_name);

extern Datum
gp_aoseg_name(PG_FUNCTION_ARGS);

/*
 * UDF to get aoseg information by relation name
 */
Datum
gp_aoseg_name(PG_FUNCTION_ARGS)
{
    int aoRelOid;
	RangeVar		*parentrv;
	text	   		*relname = PG_GETARG_TEXT_P(0);

	parentrv = makeRangeVarFromNameList(textToQualifiedNameList(relname));
	aoRelOid = RangeVarGetRelid(parentrv, false);

	typedef struct Context
	{
		Oid		aoRelOid;

		FileSegInfo **aoSegfileArray;

		int totalAoSegFiles;

		int		segfileArrayIndex;
	} Context;
	
	FuncCallContext *funcctx;
	Context *context;

	if (SRF_IS_FIRSTCALL())
	{
		TupleDesc	tupdesc;
		MemoryContext oldcontext;
		Relation aocsRel;
		AppendOnlyEntry *aoEntry;
		Relation pg_aoseg_rel;

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/*
		 * switch to memory context appropriate for multiple function
		 * calls
		 */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* build tupdesc for result tuples */
		tupdesc = CreateTemplateTupleDesc(7, false);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "segno",
						   INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "eof",
						   INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "tupcount",
						   INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 4, "varblockcount",
						   INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 5, "eofuncompressed",
						   INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 6, "modcount",
						   INT8OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 7, "state",
						   INT2OID, -1, 0);

		funcctx->tuple_desc = BlessTupleDesc(tupdesc);

		/*
		 * Collect all the locking information that we will format and send
		 * out as a result set.
		 */
		context = (Context *) palloc(sizeof(Context));
		funcctx->user_fctx = (void *) context;

		context->aoRelOid = aoRelOid;

		aocsRel = heap_open(aoRelOid, NoLock);
		if(!RelationIsAoRows(aocsRel))
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("'%s' is not an append-only row relation",
							RelationGetRelationName(aocsRel))));

		aoEntry = GetAppendOnlyEntry(aoRelOid, SnapshotNow);
		
		pg_aoseg_rel = heap_open(aoEntry->segrelid, NoLock);
		
		context->aoSegfileArray = 
				GetAllFileSegInfo_pg_aoseg_rel(
											RelationGetRelationName(aocsRel), 
											aoEntry, 
											pg_aoseg_rel,
											SnapshotNow,
											&context->totalAoSegFiles);

		heap_close(pg_aoseg_rel, NoLock);
		heap_close(aocsRel, NoLock);
		pfree(aoEntry);

		// Iteration position.
		context->segfileArrayIndex = 0;

		funcctx->user_fctx = (void *) context;

		MemoryContextSwitchTo(oldcontext);
	}

	funcctx = SRF_PERCALL_SETUP();
	context = (Context *) funcctx->user_fctx;

	/*
	 * Process each column for each segment file.
	 */
	while (true)
	{
		Datum		values[7];
		bool		nulls[7];
		HeapTuple	tuple;
		Datum		result;

		struct FileSegInfo *aoSegfile;

		if (context->segfileArrayIndex >= context->totalAoSegFiles)
		{
			break;
		}
		
		/*
		 * Form tuple with appropriate data.
		 */
		MemSet(values, 0, sizeof(values));
		MemSet(nulls, false, sizeof(nulls));

		aoSegfile = context->aoSegfileArray[context->segfileArrayIndex];

		values[0] = Int32GetDatum(aoSegfile->segno);
		values[1] = Int64GetDatum(aoSegfile->eof);
		values[2] = Int64GetDatum(aoSegfile->total_tupcount);
		values[3] = Int64GetDatum(aoSegfile->varblockcount);
		values[4] = Int64GetDatum(aoSegfile->eof_uncompressed);
		values[5] = Int64GetDatum(aoSegfile->modcount);
		values[6] = Int16GetDatum(aoSegfile->state);

		tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);
		result = HeapTupleGetDatum(tuple);

		// Indicate we emitted one segment file.
		context->segfileArrayIndex++;

		SRF_RETURN_NEXT(funcctx, result);
	}

	if (context->aoSegfileArray)
	{
		FreeAllSegFileInfo(context->aoSegfileArray, context->totalAoSegFiles);
		pfree(context->aoSegfileArray);
		context->aoSegfileArray = NULL;
		context->totalAoSegFiles = 0;
	}
	pfree(context);
	funcctx->user_fctx = NULL;

	SRF_RETURN_DONE(funcctx);
}

/*
 * gp_update_ao_master_stats
 *
 * This function is mainly created to handle cases that our product allowed
 * loading data into an append only table in utility mode, and as a result
 * the QD gets out of sync as to the number of rows in this table for each
 * segment. An example for this scenario is gp_restore. running this function
 * puts the QD aoseg table back in sync.
 */
static Datum
gp_update_ao_master_stats_internal(Oid relid, Snapshot appendOnlyMetaDataSnapshot)
{
	Relation		parentrel;
	Datum			returnDatum;

	/* open the parent (main) relation */
	parentrel = heap_open(relid, RowExclusiveLock);

	if(!RelationIsAoRows(parentrel) && !RelationIsAoCols(parentrel))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("'%s' is not an append-only relation",
						RelationGetRelationName(parentrel))));

	if (RelationIsAoRows(parentrel))
	{
		returnDatum = gp_update_aorow_master_stats_internal(parentrel, appendOnlyMetaDataSnapshot);
	}
	else
	{
		returnDatum = gp_update_aocol_master_stats_internal(parentrel, appendOnlyMetaDataSnapshot);
	}
	
	heap_close(parentrel, RowExclusiveLock);

	return returnDatum;
}

Datum
gp_update_ao_master_stats_name(PG_FUNCTION_ARGS)
{
	RangeVar		*parentrv;
	text	   		*relname = PG_GETARG_TEXT_P(0);
	Oid				relid;

	Assert(Gp_role == GP_ROLE_DISPATCH);

	parentrv = makeRangeVarFromNameList(textToQualifiedNameList(relname));
	relid = RangeVarGetRelid(parentrv, false);

	return gp_update_ao_master_stats_internal(relid, SnapshotNow);
}


/*
 * get_ao_compression_ratio_oid
 *
 * same as get_ao_compression_ratio_name, but takes rel oid as argument.
 */
Datum
gp_update_ao_master_stats_oid(PG_FUNCTION_ARGS)
{
	Oid			relid = PG_GETARG_OID(0);

	Assert(Gp_role == GP_ROLE_DISPATCH);

	return gp_update_ao_master_stats_internal(relid, SnapshotNow);
}

typedef struct
{
	int index;
	int rows;
} QueryInfo;


/**************************************************************
 * get_ao_distribution_oid
 * get_ao_distribution_name
 *
 * given an AO table name or oid, show the total distribution
 * of rows across all segment databases in the system.
 *
 * TODO: for now these 2 functions are almost completely
 * duplicated. See how to factor out a common internal function
 * such as done in get_ao_compression_ratio below.
 **************************************************************/

Datum
get_ao_distribution_oid(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	MemoryContext 	oldcontext;
	AclResult		aclresult;
	QueryInfo 		*query_block = NULL;
	StringInfoData 	sqlstmt;
	Relation		parentrel;
	Relation		aosegrel;
	char			aoseg_relname[NAMEDATALEN];
	int 			ret;
	Oid				relid = PG_GETARG_OID(0);

	Assert(Gp_role == GP_ROLE_DISPATCH);

	/*
	 * stuff done only on the first call of the function. In here we
	 * execute the query, gather the result rows and keep them in our
	 * context so that we could return them in the next calls to this func.
	 */
	if (SRF_IS_FIRSTCALL())
	{
		bool 			connected = false;
		Oid segrelid;

		funcctx = SRF_FIRSTCALL_INIT();

		/* open the parent (main) relation */
		parentrel = heap_open(relid, AccessShareLock);

		/*
		 * check permission to SELECT from this table (this function
		 * is effectively a form of COUNT(*) FROM TABLE
		 */
		aclresult = pg_class_aclcheck(parentrel->rd_id, GetUserId(),
									ACL_SELECT);

		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult,
						ACL_KIND_CLASS,
						RelationGetRelationName(parentrel));

		/*
		 * verify this is an AO relation
		 */
		if(!RelationIsAoRows(parentrel) && !RelationIsAoCols(parentrel))
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("'%s' is not an append-only relation",
							RelationGetRelationName(parentrel))));

		GetAppendOnlyEntryAuxOids(RelationGetRelid(parentrel), SnapshotNow,
								  &segrelid, NULL, NULL, NULL, NULL, NULL);
		Assert(OidIsValid(segrelid));
		
		/*
		 * get the name of the aoseg relation
		 */
		aosegrel = heap_open(segrelid, AccessShareLock);
		snprintf(aoseg_relname, NAMEDATALEN, "%s", RelationGetRelationName(aosegrel));
		heap_close(aosegrel, AccessShareLock);

		/*
		 * assemble our query string
		 */
		initStringInfo(&sqlstmt);
		if (RelationIsAoRows(parentrel))
			appendStringInfo(&sqlstmt, "select gp_segment_id,sum(tupcount) "
							"from gp_dist_random('pg_aoseg.%s') "
							"group by (gp_segment_id)", aoseg_relname);
		else
		{
			Assert(RelationIsAoCols(parentrel));

			/*
			 * Type cast the bigint tupcount to the expected output type float8.
			 */
			appendStringInfo(&sqlstmt, "select gp_segment_id,sum(tupcount::float8) "
							"from gp_dist_random('pg_aoseg.%s') "
							"group by (gp_segment_id)", aoseg_relname);
		}

		PG_TRY();
		{

			if (SPI_OK_CONNECT != SPI_connect())
			{
				ereport(ERROR, (errcode(ERRCODE_CDB_INTERNAL_ERROR),
								errmsg("Unable to obtain AO relation information from segment databases."),
								errdetail("SPI_connect failed in get_ao_distribution")));
			}
			connected = true;

			/* Do the query. */
			ret = SPI_execute(sqlstmt.data, false, 0);

			if (ret > 0 && SPI_tuptable != NULL)
			{
				QueryInfo *query_block_state = NULL;

				/* switch to memory context appropriate for multiple function calls */
				oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

				funcctx->tuple_desc = BlessTupleDesc(SPI_tuptable->tupdesc);

				/*
				 * Allocate cross-call state, so that we can keep track of
				 * where we're at in the processing.
				 */
				query_block_state = (QueryInfo *) palloc0( sizeof(QueryInfo) );
				funcctx->user_fctx = (int *)query_block_state;

				query_block_state->index	= 0;
				query_block_state->rows	= SPI_processed;
				MemoryContextSwitchTo(oldcontext);
			}
		}

		/* Clean up in case of error. */
		PG_CATCH();
		{
			if (connected)
				SPI_finish();

			/* Carry on with error handling. */
			PG_RE_THROW();
		}
		PG_END_TRY();

		pfree(sqlstmt.data);
		heap_close(parentrel, AccessShareLock);
	}

	/*
	 * Per-call operations
	 */

	funcctx = SRF_PERCALL_SETUP();

	query_block = (QueryInfo *)funcctx->user_fctx;
	if ( query_block->index < query_block->rows )
	{
		/*
		 * Get heaptuple from SPI, then deform it, and reform it using
		 * our tuple desc.
		 * If we don't do this, but rather try to pass the tuple from SPI
		 * directly back, we get an error because
		 * the tuple desc that is associated with the SPI call
		 * has not been blessed.
		 */
		HeapTuple tuple = SPI_tuptable->vals[query_block->index++];
		TupleDesc tupleDesc = funcctx->tuple_desc;

		Datum *values = (Datum *) palloc(tupleDesc->natts * sizeof(Datum));
		bool *nulls = (bool *) palloc(tupleDesc->natts * sizeof(bool));

		HeapTuple res = NULL;
		Datum result;

		heap_deform_tuple(tuple, tupleDesc, values, nulls);

		res = heap_form_tuple(tupleDesc, values, nulls );

		pfree(values);
		pfree(nulls);

		/* make the tuple into a datum */
		result  = HeapTupleGetDatum(res);

		SRF_RETURN_NEXT(funcctx, result);
	}

	/*
	 * do when there is no more left
	 */
	pfree(query_block);

	SPI_finish();

	funcctx->user_fctx = NULL;

	SRF_RETURN_DONE(funcctx);
}

Datum
get_ao_distribution_name(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	MemoryContext 	oldcontext;
	AclResult		aclresult;
	QueryInfo 		*query_block = NULL;
	StringInfoData 	sqlstmt;
	RangeVar		*parentrv;
	Relation		parentrel;
	Relation		aosegrel;
	char			aoseg_relname[NAMEDATALEN];
	int 			ret;
	text	   		*relname = PG_GETARG_TEXT_P(0);
	Oid				relid;

	Assert(Gp_role == GP_ROLE_DISPATCH);

	/*
	 * stuff done only on the first call of the function. In here we
	 * execute the query, gather the result rows and keep them in our
	 * context so that we could return them in the next calls to this func.
	 */
	if (SRF_IS_FIRSTCALL())
	{
		bool 			connected = false;
		Oid segrelid = InvalidOid;

		funcctx = SRF_FIRSTCALL_INIT();

		parentrv = makeRangeVarFromNameList(textToQualifiedNameList(relname));
		relid = RangeVarGetRelid(parentrv, false);

		/* get the relid of the parent (main) relation */
		parentrel = heap_openrv(parentrv, AccessShareLock);

		/*
		 * check permission to SELECT from this table (this function
		 * is effectively a form of COUNT(*) FROM TABLE
		 */
		aclresult = pg_class_aclcheck(parentrel->rd_id, GetUserId(),
									ACL_SELECT);

		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult,
						ACL_KIND_CLASS,
						RelationGetRelationName(parentrel));

		/*
		 * verify this is an AO relation
		 */
		if(!RelationIsAoRows(parentrel) && !RelationIsAoCols(parentrel))
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("'%s' is not an append-only relation",
							RelationGetRelationName(parentrel))));
		
		GetAppendOnlyEntryAuxOids(RelationGetRelid(parentrel), SnapshotNow,
								  &segrelid,
								  NULL, NULL, NULL, NULL, NULL);
		Assert(OidIsValid(segrelid));

		/*
		 * get the name of the aoseg relation
		 */
		aosegrel = heap_open(segrelid, AccessShareLock);
		snprintf(aoseg_relname, NAMEDATALEN, "%s", RelationGetRelationName(aosegrel));
		heap_close(aosegrel, AccessShareLock);

		/*
		 * assemble our query string
		 */
		initStringInfo(&sqlstmt);
		appendStringInfo(&sqlstmt, "select gp_segment_id,sum(tupcount) "
						"from gp_dist_random('pg_aoseg.%s') "
						"group by (gp_segment_id)", aoseg_relname);

		PG_TRY();
		{

			if (SPI_OK_CONNECT != SPI_connect())
			{
				ereport(ERROR, (errcode(ERRCODE_CDB_INTERNAL_ERROR),
								errmsg("Unable to obtain AO relation information from segment databases."),
								errdetail("SPI_connect failed in get_ao_distribution")));
			}
			connected = true;

			/* Do the query. */
			ret = SPI_execute(sqlstmt.data, false, 0);

			if (ret > 0 && SPI_tuptable != NULL)
			{
				QueryInfo *query_block_state = NULL;

				/* switch to memory context appropriate for multiple function calls */
				oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

				funcctx->tuple_desc = BlessTupleDesc(SPI_tuptable->tupdesc);

				/*
				 * Allocate cross-call state, so that we can keep track of
				 * where we're at in the processing.
				 */
				query_block_state = (QueryInfo *) palloc0( sizeof(QueryInfo) );
				funcctx->user_fctx = (int *)query_block_state;

				query_block_state->index	= 0;
				query_block_state->rows	= SPI_processed;
				MemoryContextSwitchTo(oldcontext);
			}
		}

		/* Clean up in case of error. */
		PG_CATCH();
		{
			if (connected)
				SPI_finish();

			/* Carry on with error handling. */
			PG_RE_THROW();
		}
		PG_END_TRY();

		pfree(sqlstmt.data);
		heap_close(parentrel, AccessShareLock);
	}

	/*
	 * Per-call operations
	 */

	funcctx = SRF_PERCALL_SETUP();

	query_block = (QueryInfo *)funcctx->user_fctx;
	if ( query_block->index < query_block->rows )
	{
		/*
		 * Get heaptuple from SPI, then deform it, and reform it using
		 * our tuple desc.
		 * If we don't do this, but rather try to pass the tuple from SPI
		 * directly back, we get an error because
		 * the tuple desc that is associated with the SPI call
		 * has not been blessed.
		 */
		HeapTuple tuple = SPI_tuptable->vals[query_block->index++];
		TupleDesc tupleDesc = funcctx->tuple_desc;

		Datum *values = (Datum *) palloc(tupleDesc->natts * sizeof(Datum));
		bool *nulls = (bool *) palloc(tupleDesc->natts * sizeof(bool));

		HeapTuple res = NULL;
		Datum result;

		heap_deform_tuple(tuple, tupleDesc, values, nulls);

		res = heap_form_tuple(tupleDesc, values, nulls );

		pfree(values);
		pfree(nulls);

		/* make the tuple into a datum */
		result  = HeapTupleGetDatum(res);

		SRF_RETURN_NEXT(funcctx, result);
	}

	/*
	 * do when there is no more left
	 */
	pfree(query_block);

	SPI_finish();

	funcctx->user_fctx = NULL;

	SRF_RETURN_DONE(funcctx);
}

/**************************************************************
 * get_ao_compression_ratio_oid
 * get_ao_compression_ratio_name
 *
 * Given an append-only table name or oid calculate the effective
 * compression ratio for this append only table stored data.
 * If this info is not available (pre 3.3 created tables) then
 * return -1.
 **************************************************************/

Datum
get_ao_compression_ratio_name(PG_FUNCTION_ARGS)
{
	RangeVar		*parentrv;
	text	   		*relname = PG_GETARG_TEXT_P(0);
	Oid				relid;

	Assert(Gp_role == GP_ROLE_DISPATCH);

	parentrv = makeRangeVarFromNameList(textToQualifiedNameList(relname));
	relid = RangeVarGetRelid(parentrv, false);

	return ao_compression_ratio_internal(relid);
}


/*
 * get_ao_compression_ratio_oid
 *
 * same as get_ao_compression_ratio_name, but takes rel oid as argument.
 */
Datum
get_ao_compression_ratio_oid(PG_FUNCTION_ARGS)
{
	Oid			relid = PG_GETARG_OID(0);

	Assert(Gp_role == GP_ROLE_DISPATCH);

	return ao_compression_ratio_internal(relid);
}

static Datum
aorow_compression_ratio_internal(Relation parentrel)
{
	StringInfoData 	sqlstmt;
	Relation		aosegrel;
	bool 			connected = false;
	char			aoseg_relname[NAMEDATALEN];
	int 			proc;
	int 			ret;
	float8			compress_ratio = -1; /* the default, meaning "not available" */

	MemoryContext 	oldcontext = CurrentMemoryContext;
	Oid segrelid = InvalidOid;

	GetAppendOnlyEntryAuxOids(RelationGetRelid(parentrel), SnapshotNow,
							  &segrelid,
							  NULL, NULL, NULL, NULL, NULL);
	Assert(OidIsValid(segrelid));

	/*
	 * get the name of the aoseg relation
	 */
	aosegrel = heap_open(segrelid, AccessShareLock);
	snprintf(aoseg_relname, NAMEDATALEN, "%s", RelationGetRelationName(aosegrel));
	heap_close(aosegrel, AccessShareLock);

	/*
	 * assemble our query string
	 */
	initStringInfo(&sqlstmt);
	if (Gp_role == GP_ROLE_DISPATCH)
		appendStringInfo(&sqlstmt, "select sum(eof), sum(eofuncompressed) "
								"from gp_dist_random('pg_aoseg.%s')",
									aoseg_relname);
	else
		appendStringInfo(&sqlstmt, "select eof, eofuncompressed "
								"from pg_aoseg.%s",
									aoseg_relname);

	PG_TRY();
	{

		if (SPI_OK_CONNECT != SPI_connect())
		{
			ereport(ERROR, (errcode(ERRCODE_CDB_INTERNAL_ERROR),
							errmsg("Unable to obtain AO relation information from segment databases."),
							errdetail("SPI_connect failed in get_ao_compression_ratio")));
		}
		connected = true;

		/* Do the query. */
		ret = SPI_execute(sqlstmt.data, false, 0);
		proc = SPI_processed;


		if (ret > 0 && SPI_tuptable != NULL)
		{
			TupleDesc 		tupdesc = SPI_tuptable->tupdesc;
			SPITupleTable*	tuptable = SPI_tuptable;
			HeapTuple 		tuple = tuptable->vals[0];
			char*			val_eof;
			char*			val_eof_uncomp;
			MemoryContext 	cxt_save;

			/* we expect only 1 tuple */
			Assert(proc == 1);

			/* Get totals from QE's */
			val_eof = SPI_getvalue(tuple, tupdesc, 1);
			val_eof_uncomp = SPI_getvalue(tuple, tupdesc, 2);

			/* use our own context so that SPI won't free our stuff later */
			cxt_save = MemoryContextSwitchTo(oldcontext);

			/*
			 * Calculate the compression ratio. but do it only if the uncomp
			 * value is not NULL. In older tables, that were created before
			 * GPDB 3.3 the upgrade process will leave a NULL in new aoseg
			 * table eofuncompressed column, and in that case we return -1
			 * to indicate "ratio N/A" (set by default at declare time).
			 */
			if(val_eof_uncomp != NULL && val_eof != NULL)
			{
				Datum eof = DirectFunctionCall1(float8in, CStringGetDatum(val_eof));
				Datum eof_uncomp = DirectFunctionCall1(float8in, CStringGetDatum(val_eof_uncomp));

				/* guard against division by zero */
				if(DatumGetFloat8(eof) > 0)
				{
					char  buf[8];

					/* calculate the compression ratio */
					float8 compress_ratio_raw = DatumGetFloat8(DirectFunctionCall2(float8div,
																				eof_uncomp,
																				eof));

					/* format to 2 digits past the decimal point */
					snprintf(buf, 8, "%.2f", compress_ratio_raw);

					/* format to 2 digit decimal precision */
					compress_ratio = DatumGetFloat8(DirectFunctionCall1(float8in,
													CStringGetDatum(buf)));
				}
			}

			MemoryContextSwitchTo(cxt_save);

		}

		connected = false;
		SPI_finish();
	}

	/* Clean up in case of error. */
	PG_CATCH();
	{
		if (connected)
			SPI_finish();

		/* Carry on with error handling. */
		PG_RE_THROW();
	}
	PG_END_TRY();


	pfree(sqlstmt.data);

	PG_RETURN_FLOAT8(compress_ratio);
}

static Datum
ao_compression_ratio_internal(Oid relid)
{
	Relation		parentrel;
	Datum			returnDatum;

	/* open the parent (main) relation */
	parentrel = heap_open(relid, AccessShareLock);

	if(!RelationIsAoRows(parentrel) && !RelationIsAoCols(parentrel))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				errmsg("'%s' is not an append-only relation",
						RelationGetRelationName(parentrel))));

	if (RelationIsAoRows(parentrel))
	{
		returnDatum = aorow_compression_ratio_internal(parentrel);
	}
	else
	{
		returnDatum = aocol_compression_ratio_internal(parentrel);
	}
	
	heap_close(parentrel, AccessShareLock);

	return returnDatum;
}

void
FreeAllSegFileInfo(FileSegInfo **allSegInfo, int totalSegFiles)
{
	Assert(allSegInfo);

	for(int file_no = 0; file_no < totalSegFiles; file_no++)
	{
		Assert(allSegInfo[file_no] != NULL);
		
		pfree(allSegInfo[file_no]);
	}
}


void
PrintPgaosegAndGprelationNodeEntries(FileSegInfo **allseginfo, int totalsegs, bool *segmentFileNumMap)
{
	char segnumArray[1000];
	char delimiter[5] = " ";
	char tmp[10] = {0};
	memset(segnumArray, 0, sizeof(segnumArray));

	for (int i = 0 ; i < totalsegs && allseginfo; i++)
	{
		snprintf(tmp, sizeof(tmp), "%d:%jd", allseginfo[i]->segno, allseginfo[i]->eof);

		if (strlen(segnumArray) + strlen(tmp) + strlen(delimiter) >= 1000)
		{
			break;
		}

		strncat(segnumArray, tmp, sizeof(tmp));
		strncat(segnumArray, delimiter, sizeof(delimiter));
	}
	elog(LOG, "pg_aoseg segno:eof entries: %s", segnumArray);

	memset(segnumArray, 0, sizeof(segnumArray));

	for (int i = 0; i < AOTupleId_MaxSegmentFileNum; i++)
	{
		if (segmentFileNumMap[i] == true)
		{
			snprintf(tmp, sizeof(tmp), "%d", i);

			if (strlen(segnumArray) + strlen(tmp) + strlen(delimiter) >= 1000)
			{
				break;
			}

			strncat(segnumArray, tmp, sizeof(tmp));
			strncat(segnumArray, delimiter, sizeof(delimiter));
		}
	}
	elog(LOG, "gp_relation_node segno entries: %s", segnumArray);
}


void
CheckAOConsistencyWithGpRelationNode( Snapshot snapshot, Relation rel, int totalsegs, FileSegInfo ** allseginfo)
{
	GpRelationNodeScan gpRelationNodeScan;
	int segmentFileNum = 0;
	ItemPointerData persistentTid;
	int64 persistentSerialNum = 0;
	int segmentCount = 0;
	Relation gp_relation_node;

	if (!gp_appendonly_verify_eof)
	{
		return;
	}

	/* 
	 * gp_relation_node alway has a zero. Hence we use Max segment file number plus 1 in order
	 * to accomodate the zero
	 */
	const int num_gp_relation_node_entries = AOTupleId_MaxSegmentFileNum + 1;
	bool *segmentFileNumMap = (bool*) palloc0( sizeof(bool) * num_gp_relation_node_entries);
	int i = 0;
	int j = 0;

	gp_relation_node = heap_open(GpRelationNodeRelationId, AccessShareLock);
	GpRelationNodeBeginScan(
					snapshot,
					gp_relation_node,
					rel->rd_id,
					rel->rd_rel->relfilenode,
					&gpRelationNodeScan);
	while ((NULL != GpRelationNodeGetNext(
						&gpRelationNodeScan,
						&segmentFileNum,
						&persistentTid,
						&persistentSerialNum)))
	{
		if (segmentFileNumMap[segmentFileNum] != true)
		{
			segmentFileNumMap[segmentFileNum] = true;
			segmentCount++;
		}

		if (segmentCount > totalsegs + 1)
		{
			PrintPgaosegAndGprelationNodeEntries(allseginfo, totalsegs, segmentFileNumMap);
			elog(ERROR, "gp_relation_node (%d) has more entries than pg_aoseg (%d) for relation %s",
				segmentCount,
				totalsegs,
				RelationGetRelationName(rel));
		}
	}

	GpRelationNodeEndScan(&gpRelationNodeScan);
	heap_close(gp_relation_node, AccessShareLock);

	if (totalsegs > 0)
	{
		if (allseginfo[0]->segno == 0)
		{
			i++;
		}
	}

	for (j = 1; j < num_gp_relation_node_entries; j++)
	{
		if (segmentFileNumMap[j] == true)
		{
			while(i < totalsegs && allseginfo[i]->segno != j)
			{

				if (allseginfo[i]->eof != 0 && segmentFileNumMap[allseginfo[i]->segno] == false)
				{
					PrintPgaosegAndGprelationNodeEntries(allseginfo, totalsegs, segmentFileNumMap);
					elog(ERROR, "Missing pg_aoseg entry %d in gp_relation_node for %s",
							allseginfo[i]->segno, RelationGetRelationName(rel));
				}
				i++;
			}

			if (i < totalsegs && allseginfo[i]->segno == j)
			{
				i++;
				continue;
			}

			if (i == totalsegs)
			{
				PrintPgaosegAndGprelationNodeEntries(allseginfo, totalsegs, segmentFileNumMap);
				elog(ERROR, "Missing gp_relation_node entry %d in pg_aoseg for %s",
							j, RelationGetRelationName(rel));
			}
		}
	}

	/*
	 * Check for any extra entries in pg_aoseg which are not present in gp_relation_node
	 */
	while (i < totalsegs)
	{
		Assert(segmentFileNumMap[allseginfo[i]->segno] == false);
		if (allseginfo[i]->eof != 0)
		{
			PrintPgaosegAndGprelationNodeEntries(allseginfo, totalsegs, segmentFileNumMap);
			elog(ERROR, "Missing pg_aoseg entry %d in gp_relation_node for %s",
					allseginfo[i]->segno, RelationGetRelationName(rel));
		}
		i++;
	}

	pfree(segmentFileNumMap);
}

