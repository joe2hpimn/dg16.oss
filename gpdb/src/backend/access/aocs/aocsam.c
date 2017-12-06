/*
 * Append only columnar access methods
 *
 *	Copyright (c), 2009-2010, Greenplum Inc.
 */

#include "postgres.h"

#include "fmgr.h"
#include "access/appendonlytid.h"
#include "access/aomd.h"
#include "access/heapam.h"
#include "access/hio.h"
#include "access/multixact.h"
#include "access/transam.h"
#include "access/tuptoaster.h"
#include "access/valid.h"
#include "access/xact.h"
#include "access/appendonlywriter.h"
#include "catalog/catalog.h"
#include "catalog/pg_appendonly.h"
#include "catalog/pg_attribute_encoding.h"
#include "catalog/namespace.h"
#include "catalog/gp_fastsequence.h"
#include "cdb/cdbvars.h"
#include "cdb/cdbappendonlyblockdirectory.h"
#include "cdb/cdbappendonlystoragelayer.h"
#include "cdb/cdbappendonlystorageread.h"
#include "cdb/cdbappendonlystoragewrite.h"
#include "utils/datumstream.h"
#include "access/aocssegfiles.h"
#include "cdb/cdbaocsam.h"
#include "cdb/cdbappendonlyam.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "storage/procarray.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/relcache.h"
#include "utils/syscache.h"
#include "storage/freespace.h"
#include "storage/smgr.h"
#include "utils/faultinjector.h"
#include "utils/guc.h"

#include "utils/debugbreak.h"

static AOCSScanDesc
aocs_beginscan_internal(Relation relation,
		AppendOnlyEntry *aoentry,
		AOCSFileSegInfo **seginfo,
		int total_seg,
		Snapshot snapshot,
		Snapshot appendOnlyMetaDataSnapshot,
		TupleDesc relationTupleDesc, bool *proj);

/*
 * Open the segment file for a specified column
 * associated with the datum stream.
 */
static void open_datumstreamread_segfile(
									 char *basepath, RelFileNode node,
									 AOCSFileSegInfo *segInfo,
									 DatumStreamRead *ds,
									 int colNo)
{
	int segNo = segInfo->segno;
	char fn[MAXPGPATH];
	int32	fileSegNo;

	AOCSVPInfoEntry *e = getAOCSVPEntry(segInfo, colNo);

	FormatAOSegmentFileName(basepath, segNo, colNo, &fileSegNo, fn);
	Assert(strlen(fn) + 1 <= MAXPGPATH);

	Assert(ds);
	datumstreamread_open_file(ds, fn, e->eof, e->eof_uncompressed, node, fileSegNo);
}

/*
 * Open all segment files associted with the datum stream.
 *
 * Currently, there is one segment file for each column. This function
 * only opens files for those columns which are in the projection.
 *
 * If blockDirectory is not NULL, the first block info is written to
 * the block directory.
 */
static void open_all_datumstreamread_segfiles(Relation rel,
										  AOCSFileSegInfo *segInfo,
										  DatumStreamRead **ds,
										  bool *proj,
										  int nvp,
										  AppendOnlyBlockDirectory *blockDirectory)
{
    char *basepath = relpath(rel->rd_node);

    int i;

    Assert(proj);

    for(i=0; i<nvp; ++i)
    {
        if (proj[i])
        {
            open_datumstreamread_segfile(basepath, rel->rd_node, segInfo, ds[i], i);
            datumstreamread_block(ds[i]);

            if (blockDirectory != NULL)
            {
                AppendOnlyBlockDirectory_InsertEntry(blockDirectory,
                        i,
                        ds[i]->blockFirstRowNum,
                        ds[i]->blockFileOffset,
                        ds[i]->blockRowCount);
            }
        }
    }

    pfree(basepath);
}

/*
 * Initialise data streams for every column used in this query. For writes, this
 * means all columns.
 */
static void
open_ds_write(Relation rel, DatumStreamWrite **ds, TupleDesc relationTupleDesc,
			  bool *proj, AORelationVersion version, bool checksum)
{
	int		nvp = relationTupleDesc->natts;
	StdRdOptions 	**opts = RelationGetAttributeOptions(rel);

	/* open datum streams.  It will open segment file underneath */
	for (int i = 0; i < nvp; ++i)
	{
		Form_pg_attribute attr = relationTupleDesc->attrs[i];
		char *ct;
		int32 clvl;
		int32 blksz;

		StringInfoData titleBuf;

		// UNDONE: Need to track and dispose of this storage...
		initStringInfo(&titleBuf);
		appendStringInfo(&titleBuf, "Write of Append-Only Column-Oriented relation '%s', column #%d '%s'",
						 RelationGetRelationName(rel),
						 i + 1,
						 NameStr(attr->attname));

		/*
		 * We always record all the three column specific attributes
		 * for each column of a column oriented table.  Note: checksum
		 * is a table level attribute.
		 */
		Assert(opts[i]);
		ct = opts[i]->compresstype;
		clvl = opts[i]->compresslevel;
		blksz = opts[i]->blocksize;

		ds[i] = create_datumstreamwrite(
									ct,
									clvl,
									checksum,
									/* safeFSWriteSize */ 0,	// UNDONE: Need to wire down pg_appendonly column?
									blksz,
									version,
									attr,
									RelationGetRelationName(rel),
									/* title */ titleBuf.data);

	}
}

/*
 * Initialise data streams for every column used in this query. For writes, this
 * means all columns.
 */
static void
open_ds_read(Relation rel, DatumStreamRead **ds, TupleDesc relationTupleDesc,
			 bool *proj, AORelationVersion version, bool checksum)
{
	int		nvp = relationTupleDesc->natts;
	StdRdOptions 	**opts = RelationGetAttributeOptions(rel);

	/* open datum streams.  It will open segment file underneath */
	for (int i = 0; i < nvp; ++i)
	{
		Form_pg_attribute attr = relationTupleDesc->attrs[i];
		char *ct;
		int32 clvl;
		int32 blksz;

		/*
		 * We always record all the three column specific attributes
		 * for each column of a column oriented table.  Note: checksum
		 * is a table level attribute.
		 */
		Assert(opts[i]);
		ct = opts[i]->compresstype;
		clvl = opts[i]->compresslevel;
		blksz = opts[i]->blocksize;

		if (proj[i])
		{
			StringInfoData titleBuf;

			// UNDONE: Need to track and dispose of this storage...
			initStringInfo(&titleBuf);
			appendStringInfo(&titleBuf, "Scan of Append-Only Column-Oriented relation '%s', column #%d '%s'",
							 RelationGetRelationName(rel),
							 i + 1,
							 NameStr(attr->attname));

			ds[i] = create_datumstreamread(
										ct,
										clvl,
										checksum,
										/* safeFSWriteSize */ false,	// UNDONE: Need to wire down pg_appendonly column
										blksz,
										version,
										attr,
										RelationGetRelationName(rel),
										/* title */ titleBuf.data);

		}
		else
			/* We aren't projecting this column, so nothing to do */
			ds[i] = NULL;
	}
}

static void close_ds_read(DatumStreamRead **ds, int nvp)
{
    int i;
    for(i=0; i<nvp; ++i)
    {
        if(ds[i])
        {
            destroy_datumstreamread(ds[i]);
            ds[i] = NULL;
        }
    }
}

static void close_ds_write(DatumStreamWrite **ds, int nvp)
{
    int i;
    for(i=0; i<nvp; ++i)
    {
        if(ds[i])
        {
            destroy_datumstreamwrite(ds[i]);
            ds[i] = NULL;
        }
    }
}


static void aocs_initscan(AOCSScanDesc scan)
{
    scan->cur_seg = -1;

    ItemPointerSet(&scan->cdb_fake_ctid, 0, 0);
    scan->cur_seg_row = 0;

    open_ds_read(scan->aos_rel, scan->ds, scan->relationTupleDesc,
				 scan->proj, scan->aoEntry->version, scan->aoEntry->checksum);

    pgstat_count_heap_scan(scan->aos_rel);
}

static int open_next_scan_seg(AOCSScanDesc scan)
{
    int nvp = scan->relationTupleDesc->natts;

    while(++scan->cur_seg < scan->total_seg)
    {
        AOCSFileSegInfo * curSegInfo = scan->seginfo[scan->cur_seg];

        if(curSegInfo->total_tupcount > 0)
        {
            int i = 0;
            bool emptySeg = false;
            for(; i<nvp; ++i)
            {
                if (scan->proj[i])
                {
                    AOCSVPInfoEntry *e = getAOCSVPEntry(curSegInfo, i);
                    if (e->eof == 0  || curSegInfo->state == AOSEG_STATE_AWAITING_DROP)
                        emptySeg = true;

                    break;
                }
            }

            if (!emptySeg)
            {

				/* If the scan also builds the block directory, initialize it here. */
				if (scan->buildBlockDirectory)
				{
					Assert(scan->blockDirectory != NULL);
					Assert(scan->aoEntry != NULL);

                    /*
                     * if building the block directory, we need to make sure the sequence
                     *   starts higher than our highest tuple's rownum.  In the case of upgraded blocks,
                     *   the highest tuple will have tupCount as its row num
                     * for non-upgrade cases, which use the sequence, it will be enough to start
                     *   off the end of the sequence; note that this is not ideal -- if we are at
                     *   least curSegInfo->tupcount + 1 then we don't even need to update
                     *   the sequence value
                     */
                    int64 firstSequence =
                        GetFastSequences(scan->aoEntry->segrelid,
                                         curSegInfo->segno,
                                         curSegInfo->total_tupcount + 1,
                                         NUM_FAST_SEQUENCES);

					AppendOnlyBlockDirectory_Init_forInsert(scan->blockDirectory,
															scan->aoEntry,
															scan->appendOnlyMetaDataSnapshot,
															(FileSegInfo *) curSegInfo,
															0 /* lastSequence */,
															scan->aos_rel,
															curSegInfo->segno,
															nvp,
															true);

                    InsertFastSequenceEntry(scan->aoEntry->segrelid,
											curSegInfo->segno,
											firstSequence);
				}

				open_all_datumstreamread_segfiles(
											  scan->aos_rel,
											  curSegInfo,
											  scan->ds,
											  scan->proj,
											  nvp,
											  scan->blockDirectory);

				return scan->cur_seg;
			}
		}
    }

    return -1;
}

static void close_cur_scan_seg(AOCSScanDesc scan)
{
    int nvp = scan->relationTupleDesc->natts;

	if (scan->cur_seg < 0)
		return;

    for(int i=0; i<nvp; ++i)
    {
        if(scan->ds[i])
        {
            datumstreamread_close_file(scan->ds[i]);
        }
    }

	if (scan->buildBlockDirectory)
	{
		Assert(scan->blockDirectory != NULL);
		AppendOnlyBlockDirectory_End_forInsert(scan->blockDirectory);
	}
}

/*
 * aocs_beginrangescan
 *
 * begins range-limited relation scan
 */
AOCSScanDesc
aocs_beginrangescan(Relation relation,
		Snapshot snapshot,
		Snapshot appendOnlyMetaDataSnapshot,
		int *segfile_no_arr, int segfile_count,
		TupleDesc relationTupleDesc, bool *proj)
{
	AOCSFileSegInfo **seginfo;
	AppendOnlyEntry *aoentry;
	int i;

	ValidateAppendOnlyMetaDataSnapshot(&appendOnlyMetaDataSnapshot);
	RelationIncrementReferenceCount(relation);

	/*
	 * Get the pg_appendonly information for this table
	 */
	aoentry = GetAppendOnlyEntry(RelationGetRelid(relation),
		appendOnlyMetaDataSnapshot);

	seginfo = palloc0(sizeof(AOCSFileSegInfo *) * segfile_count);
	for (i = 0; i < segfile_count; i++)
	{
		seginfo[i] = GetAOCSFileSegInfo(relation, aoentry, appendOnlyMetaDataSnapshot,
				segfile_no_arr[i]);
	}
	return aocs_beginscan_internal(relation,
			aoentry,
			seginfo,
			segfile_count,
			snapshot,
			appendOnlyMetaDataSnapshot,
			relationTupleDesc,
			proj);
}

AOCSScanDesc
aocs_beginscan(Relation relation,
		Snapshot snapshot,
		Snapshot appendOnlyMetaDataSnapshot,
		TupleDesc relationTupleDesc, bool *proj)
{
    AppendOnlyEntry *aoentry;
	AOCSFileSegInfo **seginfo;
	int total_seg;

	ValidateAppendOnlyMetaDataSnapshot(&appendOnlyMetaDataSnapshot);
    RelationIncrementReferenceCount(relation);

    aoentry = GetAppendOnlyEntry(RelationGetRelid(relation), appendOnlyMetaDataSnapshot);
    Assert(aoentry->majorversion == 1 && aoentry->minorversion == 1);

    seginfo = GetAllAOCSFileSegInfo(relation, aoentry, appendOnlyMetaDataSnapshot, &total_seg);

	return aocs_beginscan_internal(relation,
			aoentry,
			seginfo,
			total_seg,
			snapshot,
			appendOnlyMetaDataSnapshot,
			relationTupleDesc,
			proj);
}

/**
 * begin the scan over the given relation.
 *
 * @param relationTupleDesc if NULL, then this function will simply use relation->rd_att.  This is the typical use-case.
 *               Passing in a separate tuple descriptor is only needed for cases for the caller has changed
 *               relation->rd_att without updating the underlying relation files yet (that is, the caller is doing
 *               an alter and relation->rd_att will be the relation's new form but relationTupleDesc is the old form)
 */
static AOCSScanDesc
aocs_beginscan_internal(Relation relation,
		AppendOnlyEntry *aoentry,
		AOCSFileSegInfo **seginfo,
		int total_seg,
		Snapshot snapshot,
		Snapshot appendOnlyMetaDataSnapshot,
		TupleDesc relationTupleDesc, bool *proj)
{
    AOCSScanDesc scan;

    if (!relationTupleDesc)
    {
        relationTupleDesc = relation->rd_att;
    }

    int nvp = relationTupleDesc->natts;

    scan = (AOCSScanDesc) palloc0(sizeof(AOCSScanDescData));
    scan->aos_rel = relation;
	scan->appendOnlyMetaDataSnapshot = appendOnlyMetaDataSnapshot;
	scan->snapshot = snapshot;
	scan->aoEntry = aoentry;
    Assert(aoentry->majorversion == 1 && aoentry->minorversion == 1);

    scan->compLevel = aoentry->compresslevel;
    scan->compType = aoentry->compresstype;
    scan->blocksz = aoentry->blocksize;

    scan->seginfo = seginfo;
	scan->total_seg = total_seg;
    scan->relationTupleDesc = relationTupleDesc;

    Assert(proj);
    scan->proj = proj;

    scan->ds = (DatumStreamRead **) palloc0(sizeof(DatumStreamRead *) * nvp);

    aocs_initscan(scan);

	scan->buildBlockDirectory = false;
	scan->blockDirectory = NULL;

	AppendOnlyVisimap_Init(&scan->visibilityMap,
						   aoentry->visimaprelid,
						   aoentry->visimapidxid,
						   AccessShareLock,
						   appendOnlyMetaDataSnapshot);

    return scan;
}

void aocs_rescan(AOCSScanDesc scan)
{
    close_cur_scan_seg(scan);
    close_ds_read(scan->ds, scan->relationTupleDesc->natts);
    aocs_initscan(scan);
}

void aocs_endscan(AOCSScanDesc scan)
{
    int i;

    RelationDecrementReferenceCount(scan->aos_rel);

	close_cur_scan_seg(scan);
    close_ds_read(scan->ds, scan->relationTupleDesc->natts);

    pfree(scan->ds);

    for(i=0; i<scan->total_seg; ++i)
    {
        if(scan->seginfo[i])
        {
            pfree(scan->seginfo[i]);
            scan->seginfo[i] = NULL;
        }
    }
    if (scan->seginfo)
        pfree(scan->seginfo);

	AppendOnlyVisimap_Finish(&scan->visibilityMap, AccessShareLock);

	pfree(scan->aoEntry);
    pfree(scan);
}

void aocs_getnext(AOCSScanDesc scan, ScanDirection direction, TupleTableSlot *slot)
{
	int ncol;
	Datum *d = slot_get_values(slot);
	bool *null = slot_get_isnull(slot);
	AOTupleId aoTupleId;
	int64 rowNum = INT64CONST(-1);

	int err = 0;
	int i;
	bool isSnapshotAny = (scan->snapshot == SnapshotAny);

	Assert(ScanDirectionIsForward(direction));

	ncol = slot->tts_tupleDescriptor->natts;
	Assert(ncol <= scan->relationTupleDesc->natts);

	while(1)
	{
ReadNext:
		/* If necessary, open next seg */
		if(scan->cur_seg < 0 || err < 0)
		{
			err = open_next_scan_seg(scan);
			if(err < 0)
			{
				/* No more seg, we are at the end */
				ExecClearTuple(slot);
				scan->cur_seg = -1;
				return;
			}
			scan->cur_seg_row = 0;
		}

		Assert(scan->cur_seg >= 0);

		/* Read from cur_seg */
		for(i=0; i<ncol; ++i)
		{
			if(scan->proj[i])
			{
				err = datumstreamread_advance(scan->ds[i]);
				Assert(err >= 0);
				if(err == 0)
				{
					err = datumstreamread_block(scan->ds[i]);
					if(err < 0)
					{
						/* Ha, cannot read next block,
						 * we need to go to next seg
						 */
						close_cur_scan_seg(scan);
						goto ReadNext;
					}

					if (scan->buildBlockDirectory)
					{
						Assert(scan->blockDirectory != NULL);

						AppendOnlyBlockDirectory_InsertEntry(scan->blockDirectory,
															 i,
															 scan->ds[i]->blockFirstRowNum,
															 scan->ds[i]->blockFileOffset,
															 scan->ds[i]->blockRowCount);
					}

					err = datumstreamread_advance(scan->ds[i]);
					Assert(err > 0);
				}

				/*
				 * Get the column's datum right here since the data structures should still
				 * be hot in CPU data cache memory.
				 */
				datumstreamread_get(scan->ds[i], &d[i], &null[i]);

				if (rowNum == INT64CONST(-1) &&
					scan->ds[i]->blockFirstRowNum != INT64CONST(-1))
				{
					Assert(scan->ds[i]->blockFirstRowNum > 0);
					rowNum = scan->ds[i]->blockFirstRowNum +
						datumstreamread_nth(scan->ds[i]);

				}
			}
		}

		AOTupleIdInit_Init(&aoTupleId);
		AOTupleIdInit_segmentFileNum(&aoTupleId,
									 scan->seginfo[scan->cur_seg]->segno);

		scan->cur_seg_row++;
		if (rowNum == INT64CONST(-1))
		{
			AOTupleIdInit_rowNum(&aoTupleId, scan->cur_seg_row);
		}
		else
		{
			AOTupleIdInit_rowNum(&aoTupleId, rowNum);
		}

		if (!isSnapshotAny && !AppendOnlyVisimap_IsVisible(&scan->visibilityMap, &aoTupleId))
		{
			rowNum = INT64CONST(-1);
			goto ReadNext;
		}
		scan->cdb_fake_ctid = *((ItemPointer)&aoTupleId);

        TupSetVirtualTupleNValid(slot, ncol);
        slot_set_ctid(slot, &(scan->cdb_fake_ctid));
        return;
    }

    Assert(!"Never here");
    return;
}


/* Open next file segment for write.  See SetCurrentFileSegForWrite */
/* XXX Right now, we put each column to different files */
static void OpenAOCSDatumStreams(AOCSInsertDesc desc)
{
	char *basepath = relpath(desc->aoi_rel->rd_node);
	char fn[MAXPGPATH];
	int32	fileSegNo;
	ItemPointerData persistentTid;
	int64 persistentSerialNum;

	AOCSFileSegInfo *seginfo;

	TupleDesc tupdesc = RelationGetDescr(desc->aoi_rel);
	int nvp = tupdesc->natts;

	desc->ds = (DatumStreamWrite **) palloc0(sizeof(DatumStreamWrite *) * nvp);

	/*
	* In order to append to this file segment entry we must first
	* acquire the relation Append-Only segment file (transaction-scope) lock (tag
	* LOCKTAG_RELATION_APPENDONLY_SEGMENT_FILE) in order to guarantee
	* stability of the pg_aoseg information on this segment file and exclusive right
	* to append data to the segment file.
	*
	* NOTE: This is a transaction scope lock that must be held until commit / abort.
	*/
	LockRelationAppendOnlySegmentFile(
								&desc->aoi_rel->rd_node,
								desc->cur_segno,
								AccessExclusiveLock,
								/* dontWait */ false);

	open_ds_write(desc->aoi_rel, desc->ds, tupdesc, NULL,
				  desc->aoEntry->version, desc->aoEntry->checksum);

	/* Now open seg info file and get eof mark. */
	seginfo = GetAOCSFileSegInfo(
								 desc->aoi_rel,
								 desc->aoEntry,
								 desc->appendOnlyMetaDataSnapshot,
								 desc->cur_segno);

	if (seginfo == NULL)
	{
		if(gp_appendonly_verify_eof)
		{
			/*
			 * If the entry(s) is(are) not found in the aocseg table, then it(they)
			 * better not be in gp_relation_node table too.
			 * But, we avoid this check for segment # 0 because it is typically used
			 * by operations similar to CTAS etc and the order followed is to first
			 * add to gp_persistent_relation_node (thus gp_relation_node) and later
			 * to pg_aocsseg table.
			 */
			for (int i=0; i < nvp; i++)
			{
				if (desc->cur_segno > 0 &&
					ReadGpRelationNode(desc->aoi_rel->rd_node.relNode,
									   (i * AOTupleId_MultiplierSegmentFileNum) + desc->cur_segno,
									   &persistentTid,
									   &persistentSerialNum))
				{
					elog(ERROR, "Found gp_relation_node entry for relation name %s, "
					"relation Oid %u, relfilenode %u, segment file #%d "
					"at PTID: %s, PSN: " INT64_FORMAT " when not expected ",
					desc->aoi_rel->rd_rel->relname.data,
					desc->aoi_rel->rd_id,
					desc->aoi_rel->rd_node.relNode,
					(i * AOTupleId_MultiplierSegmentFileNum) + desc->cur_segno,
					ItemPointerToString(&persistentTid),
					persistentSerialNum);
				}
			}
		}

		InsertInitialAOCSFileSegInfo(desc->aoEntry->segrelid, desc->cur_segno, nvp);
		seginfo = NewAOCSFileSegInfo(desc->cur_segno, nvp);
	}

	desc->fsInfo = seginfo;

	/* Never insert into a segment that is awaiting a drop */
	Assert(desc->fsInfo->state != AOSEG_STATE_AWAITING_DROP);

	desc->rowCount = seginfo->total_tupcount;

	for(int i=0; i<nvp; ++i)
	{
		AOCSVPInfoEntry *e = getAOCSVPEntry(seginfo, i);

		FormatAOSegmentFileName(basepath, seginfo->segno, i, &fileSegNo, fn);
		Assert(strlen(fn) + 1 <= MAXPGPATH);

		datumstreamwrite_open_file(desc->ds[i], fn, e->eof, e->eof_uncompressed,
				desc->aoi_rel->rd_node,
				fileSegNo);
	}

	pfree(basepath);
}

static inline void
SetBlockFirstRowNums(DatumStreamWrite **datumStreams,
					 int numDatumStreams,
					 int64 blockFirstRowNum)
{
	int i;

	Assert(datumStreams != NULL);

	for (i=0; i<numDatumStreams; i++)
	{
		Assert(datumStreams[i] != NULL);

		datumStreams[i]->blockFirstRowNum =	blockFirstRowNum;
	}
}


AOCSInsertDesc aocs_insert_init(Relation rel, int segno, bool update_mode)
{
	AOCSInsertDesc desc;
	AppendOnlyEntry *aoentry;
	TupleDesc tupleDesc;
	int64 firstSequence = 0;

    aoentry = GetAppendOnlyEntry(RelationGetRelid(rel), SnapshotNow);
    Assert(aoentry->majorversion == 1 && aoentry->minorversion == 1);


    desc = (AOCSInsertDesc) palloc0(sizeof(AOCSInsertDescData));
    desc->aoi_rel = rel;
	desc->appendOnlyMetaDataSnapshot = SnapshotNow;
							// Writers uses this since they have exclusive access to the lock acquired with
							// LockRelationAppendOnlySegmentFile for the segment-file.


	tupleDesc = RelationGetDescr(desc->aoi_rel);

	Assert(segno >= 0);
	desc->cur_segno = segno;
	desc->aoEntry = aoentry;
	desc->update_mode = update_mode;

	desc->compLevel = aoentry->compresslevel;
	desc->compType = aoentry->compresstype;
	desc->blocksz = aoentry->blocksize;

	OpenAOCSDatumStreams(desc);

	/*
	 * Obtain the next list of fast sequences for this relation.
	 *
	 * Even in the case of no indexes, we need to update the fast
	 * sequences, since the table may contain indexes at some
	 * point of time.
	 */
	desc->numSequences = 0;

	firstSequence =
		GetFastSequences(desc->aoEntry->segrelid,
						 segno,
						 desc->rowCount + 1,
						 NUM_FAST_SEQUENCES);
	desc->numSequences = NUM_FAST_SEQUENCES;

	/* Set last_sequence value */
	Assert(firstSequence > desc->rowCount);
	desc->lastSequence = firstSequence - 1;

	SetBlockFirstRowNums(desc->ds, tupleDesc->natts, desc->lastSequence + 1);

	/* Initialize the block directory. */
	tupleDesc = RelationGetDescr(rel);
	AppendOnlyBlockDirectory_Init_forInsert(
		&(desc->blockDirectory),
		aoentry,
		desc->appendOnlyMetaDataSnapshot,		// CONCERN: Safe to assume all block directory entries for segment are "covered" by same exclusive lock.
		(FileSegInfo *)desc->fsInfo, desc->lastSequence,
		rel, segno, tupleDesc->natts, true);

    return desc;
}


Oid aocs_insert_values(AOCSInsertDesc idesc, Datum *d, bool * null, AOTupleId *aoTupleId)
{
	Relation rel = idesc->aoi_rel;
	int i;

	if (rel->rd_rel->relhasoids)
		ereport(ERROR,
				(errcode(ERRCODE_GP_FEATURE_NOT_SUPPORTED),
				 errmsg("append-only column-oriented tables do not support rows with OIDs")));

#ifdef FAULT_INJECTOR
	FaultInjector_InjectFaultIfSet(
		AppendOnlyInsert,
		DDLNotSpecified,
		"",	// databaseName
		RelationGetRelationName(idesc->aoi_rel)); // tableName
#endif

	/* As usual, at this moment, we assume one col per vp */
	for(i=0; i< RelationGetNumberOfAttributes(rel); ++i)
	{
		void *toFree1;
		Datum datum;

		datum = d[i];
		int err = datumstreamwrite_put(idesc->ds[i], datum, null[i], &toFree1);
		if (toFree1 != NULL)
		{
			/*
			 * Use the de-toasted and/or de-compressed as datum instead.
			 */
			datum = PointerGetDatum(toFree1);
		}
		if(err < 0)
		{
			int itemCount = datumstreamwrite_nth(idesc->ds[i]);
			void *toFree2;

			/* write the block up to this one */
			datumstreamwrite_block(idesc->ds[i]);
			if (itemCount > 0)
			{
				/* Insert an entry to the block directory */
				AppendOnlyBlockDirectory_InsertEntry(
					&idesc->blockDirectory,
					i,
					idesc->ds[i]->blockFirstRowNum,
					AppendOnlyStorageWrite_LastWriteBeginPosition(&idesc->ds[i]->ao_write),
					itemCount);

				/* since we have written all up to the new tuple,
				 * the new blockFirstRowNum is the inserted tuple's row number
				 */
				idesc->ds[i]->blockFirstRowNum = idesc->lastSequence + 1;
			}

			Assert(idesc->ds[i]->blockFirstRowNum == idesc->lastSequence + 1);


			/* now write this new item to the new block */
			err = datumstreamwrite_put(idesc->ds[i], datum, null[i], &toFree2);
			Assert(toFree2 == NULL);
			if (err < 0)
			{
				Assert(!null[i]);
				/*
				 * rle_type is running on a block stream, if an object spans multiple
				 * blocks than data will not be compressed (if rle_type is set).
				 */
				if ((idesc->compType != NULL) && (pg_strcasecmp(idesc->compType, "rle_type") == 0))
				{
					idesc->ds[i]->ao_write.storageAttributes.compress = FALSE;
				}

				err = datumstreamwrite_lob(idesc->ds[i], datum);
				Assert(err >= 0);

				/* Insert an entry to the block directory */
				AppendOnlyBlockDirectory_InsertEntry(
					&idesc->blockDirectory,
					i,
					idesc->ds[i]->blockFirstRowNum,
					AppendOnlyStorageWrite_LastWriteBeginPosition(&idesc->ds[i]->ao_write),
					1 /*itemCount -- always just the lob just inserted */
				);


				/*
				 * A lob will live by itself in the block so
				 * this assignment is for the block that contains tuples
				 * AFTER the one we are inserting
				 */
				idesc->ds[i]->blockFirstRowNum = idesc->lastSequence + 2;
			}
		}

		if (toFree1 != NULL)
		{
			pfree(toFree1);
		}
	}

	idesc->insertCount++;
	idesc->lastSequence++;
	if (idesc->numSequences > 0)
		(idesc->numSequences)--;

	Assert(idesc->numSequences >= 0);

	AOTupleIdInit_Init(aoTupleId);
	AOTupleIdInit_segmentFileNum(aoTupleId, idesc->cur_segno);
	AOTupleIdInit_rowNum(aoTupleId, idesc->lastSequence);

	/*
	 * If the allocated fast sequence numbers are used up, we request for
	 * a next list of fast sequence numbers.
	 */
	if (idesc->numSequences == 0)
	{
		int64 firstSequence;

		firstSequence =
			GetFastSequences(idesc->aoEntry->segrelid,
							 idesc->cur_segno,
							 idesc->lastSequence + 1,
							 NUM_FAST_SEQUENCES);

		Assert(firstSequence == idesc->lastSequence + 1);
		idesc->numSequences = NUM_FAST_SEQUENCES;
	}

	return InvalidOid;
}

void aocs_insert_finish(AOCSInsertDesc idesc)
{
	Relation rel = idesc->aoi_rel;
	int i;

	for(i=0; i<rel->rd_att->natts; ++i)
	{
		int itemCount = datumstreamwrite_nth(idesc->ds[i]);

		datumstreamwrite_block(idesc->ds[i]);

		AppendOnlyBlockDirectory_InsertEntry(
			&idesc->blockDirectory,
			i,
			idesc->ds[i]->blockFirstRowNum,
			AppendOnlyStorageWrite_LastWriteBeginPosition(&idesc->ds[i]->ao_write),
			itemCount);

		datumstreamwrite_close_file(idesc->ds[i]);
	}

	AppendOnlyBlockDirectory_End_forInsert(&(idesc->blockDirectory));

	UpdateAOCSFileSegInfo(idesc);

	pfree(idesc->fsInfo);
	pfree(idesc->aoEntry);

	close_ds_write(idesc->ds, rel->rd_att->natts);
}

static void
positionFirstBlockOfRange(
	DatumStreamFetchDesc datumStreamFetchDesc)
{
	AppendOnlyBlockDirectoryEntry_GetBeginRange(
				&datumStreamFetchDesc->currentBlock.blockDirectoryEntry,
				&datumStreamFetchDesc->scanNextFileOffset,
				&datumStreamFetchDesc->scanNextRowNum);
}

static void
positionLimitToEndOfRange(
	DatumStreamFetchDesc datumStreamFetchDesc)
{
	AppendOnlyBlockDirectoryEntry_GetEndRange(
				&datumStreamFetchDesc->currentBlock.blockDirectoryEntry,
				&datumStreamFetchDesc->scanAfterFileOffset,
				&datumStreamFetchDesc->scanLastRowNum);
}


static void
positionSkipCurrentBlock(
	DatumStreamFetchDesc datumStreamFetchDesc)
{
	datumStreamFetchDesc->scanNextFileOffset =
		datumStreamFetchDesc->currentBlock.fileOffset +
		datumStreamFetchDesc->currentBlock.overallBlockLen;

	datumStreamFetchDesc->scanNextRowNum =
		datumStreamFetchDesc->currentBlock.lastRowNum + 1;
}

static void
fetchFromCurrentBlock(AOCSFetchDesc aocsFetchDesc,
					  int64 rowNum,
					  TupleTableSlot *slot,
					  int colno)
{
	DatumStreamFetchDesc datumStreamFetchDesc =
		aocsFetchDesc->datumStreamFetchDesc[colno];
	DatumStreamRead *datumStream = datumStreamFetchDesc->datumStream;
	Datum value;
	bool null;

	int rowNumInBlock = rowNum - datumStreamFetchDesc->currentBlock.firstRowNum;

	Assert(rowNumInBlock >= 0);

	/*
	 * MPP-17061: gotContents could be false in the case of aborted rows.
	 * As described in the repro in MPP-17061, if aocs_fetch is trying to
	 * fetch an invisible/aborted row, it could set the block header metadata
	 * of currentBlock to the next CO block, but without actually reading in
	 * next CO block's content.
	 */
	if (datumStreamFetchDesc->currentBlock.gotContents == false)
	{
		datumstreamread_block_content(datumStream);
		datumStreamFetchDesc->currentBlock.gotContents = true;
	}

	datumstreamread_find(datumStream, rowNumInBlock);

	if (slot != NULL)
	{
		Datum *values = slot_get_values(slot);
		bool *nulls = slot_get_isnull(slot);

		datumstreamread_get(datumStream, &(values[colno]), &(nulls[colno]));
	}
	else
	{
		datumstreamread_get(datumStream, &value, &null);
	}
}

static bool
scanToFetchValue(AOCSFetchDesc aocsFetchDesc,
				 int64 rowNum,
				 TupleTableSlot *slot,
				 int colno)
{
	DatumStreamFetchDesc datumStreamFetchDesc =
		aocsFetchDesc->datumStreamFetchDesc[colno];
	DatumStreamRead *datumStream = datumStreamFetchDesc->datumStream;
	bool found;

	found = datumstreamread_find_block(datumStream,
								   datumStreamFetchDesc,
								   rowNum);
	if (found)
		fetchFromCurrentBlock(aocsFetchDesc, rowNum, slot, colno);

	return found;
}

static void
closeFetchSegmentFile(DatumStreamFetchDesc datumStreamFetchDesc)
{
	Assert(datumStreamFetchDesc->currentSegmentFile.isOpen);

	datumstreamread_close_file(datumStreamFetchDesc->datumStream);
	datumStreamFetchDesc->currentSegmentFile.isOpen = false;
}

static bool
openFetchSegmentFile(AOCSFetchDesc aocsFetchDesc,
					 int openSegmentFileNum,
					 int colNo)
{
	int		i;

	AOCSFileSegInfo	*fsInfo;
	int			segmentFileNum;
	int64		logicalEof;
	DatumStreamFetchDesc datumStreamFetchDesc =
		aocsFetchDesc->datumStreamFetchDesc[colNo];

	Assert(!datumStreamFetchDesc->currentSegmentFile.isOpen);

	i = 0;
	while (true)
	{
		if (i >= aocsFetchDesc->totalSegfiles)
			return false;	// Segment file not visible in catalog information.

		fsInfo = aocsFetchDesc->segmentFileInfo[i];
		segmentFileNum = fsInfo->segno;
		if (openSegmentFileNum == segmentFileNum)
		{
			AOCSVPInfoEntry *entry = getAOCSVPEntry(fsInfo, colNo);

			logicalEof = entry->eof;
			break;
		}
		i++;
	}

	/*
	 * Don't try to open a segment file when its EOF is 0, since the file may not
	 * exist. See MPP-8280.
     * Also skip the segment file if it is awaiting a drop
	 */
	if (logicalEof == 0 || fsInfo->state == AOSEG_STATE_AWAITING_DROP)
		return false;

	open_datumstreamread_segfile(
							 aocsFetchDesc->basepath, aocsFetchDesc->relation->rd_node,
							 fsInfo,
							 datumStreamFetchDesc->datumStream,
							 colNo);

	datumStreamFetchDesc->currentSegmentFile.num = openSegmentFileNum;
	datumStreamFetchDesc->currentSegmentFile.logicalEof = logicalEof;

	datumStreamFetchDesc->currentSegmentFile.isOpen = true;

	return true;
}

static void
resetCurrentBlockInfo(CurrentBlock *currentBlock)
{
	currentBlock->have = false;
	currentBlock->firstRowNum = 0;
	currentBlock->lastRowNum = 0;
}

/*
 * Initialize the fetch descriptor.
 */
AOCSFetchDesc
aocs_fetch_init(Relation relation,
				Snapshot snapshot,
				Snapshot appendOnlyMetaDataSnapshot,
				bool *proj)
{
	AOCSFetchDesc aocsFetchDesc;
	int colno;
	AppendOnlyEntry *aoentry;
	char *basePath = relpath(relation->rd_node);
	TupleDesc tupleDesc = RelationGetDescr(relation);
	StdRdOptions 	**opts = RelationGetAttributeOptions(relation);

	ValidateAppendOnlyMetaDataSnapshot(&appendOnlyMetaDataSnapshot);


	/*
	 * increment relation ref count while scanning relation
	 *
	 * This is just to make really sure the relcache entry won't go away while
	 * the scan has a pointer to it.  Caller should be holding the rel open
	 * anyway, so this is redundant in all normal scenarios...
	 */
	RelationIncrementReferenceCount(relation);

	aocsFetchDesc = (AOCSFetchDesc) palloc0(sizeof(AOCSFetchDescData));
	aocsFetchDesc->relation = relation;

	aocsFetchDesc->appendOnlyMetaDataSnapshot = appendOnlyMetaDataSnapshot;
	aocsFetchDesc->snapshot = snapshot;


	aocsFetchDesc->initContext = CurrentMemoryContext;

	aocsFetchDesc->segmentFileNameMaxLen = AOSegmentFilePathNameLen(relation) + 1;
	aocsFetchDesc->segmentFileName =
						(char*)palloc(aocsFetchDesc->segmentFileNameMaxLen);
	aocsFetchDesc->segmentFileName[0] = '\0';
	aocsFetchDesc->basepath = basePath;

	Assert(proj);
	aocsFetchDesc->proj = proj;

	aoentry = GetAppendOnlyEntry(RelationGetRelid(relation), appendOnlyMetaDataSnapshot);
    Assert(aoentry->majorversion == 1 && aoentry->minorversion == 1);
	aocsFetchDesc->aoEntry = aoentry;

	aocsFetchDesc->segmentFileInfo =
		GetAllAOCSFileSegInfo(relation, aoentry, appendOnlyMetaDataSnapshot, &aocsFetchDesc->totalSegfiles);

	AppendOnlyBlockDirectory_Init_forSearch(
		&aocsFetchDesc->blockDirectory,
		aoentry,
		appendOnlyMetaDataSnapshot,
		(FileSegInfo **)aocsFetchDesc->segmentFileInfo,
		aocsFetchDesc->totalSegfiles,
		aocsFetchDesc->relation,
		relation->rd_att->natts,
		true);

	Assert(relation->rd_att != NULL);

	aocsFetchDesc->datumStreamFetchDesc = (DatumStreamFetchDesc*)
		palloc0(relation->rd_att->natts * sizeof(DatumStreamFetchDesc));

	for (colno = 0; colno < relation->rd_att->natts; colno++)
	{

		aocsFetchDesc->datumStreamFetchDesc[colno] = NULL;
		if (proj[colno])
		{
			char *ct;
			int32 clvl;
			int32 blksz;

			StringInfoData titleBuf;

			/*
			 * We always record all the three column specific
			 * attributes for each column of a column oriented table.
			 * Note: checksum is a table level attribute.
			 */
			Assert(opts[colno]);
			ct = opts[colno]->compresstype;
			clvl = opts[colno]->compresslevel;
			blksz = opts[colno]->blocksize;

			// UNDONE: Need to track and dispose of this storage...
			initStringInfo(&titleBuf);
			appendStringInfo(&titleBuf, "Fetch from Append-Only Column-Oriented relation '%s', column #%d '%s'",
							 RelationGetRelationName(relation),
							 colno + 1,
							 NameStr(tupleDesc->attrs[colno]->attname));

			aocsFetchDesc->datumStreamFetchDesc[colno] = (DatumStreamFetchDesc)
				palloc0(sizeof(DatumStreamFetchDescData));

			aocsFetchDesc->datumStreamFetchDesc[colno]->datumStream =
				create_datumstreamread(
								   ct,
								   clvl,
								   aoentry->checksum,
									/* safeFSWriteSize */ false,	// UNDONE: Need to wire down pg_appendonly column
								   blksz,
								   aoentry->version,
								   tupleDesc->attrs[colno],
								   relation->rd_rel->relname.data,
								   /* title */ titleBuf.data);

		}
		if (opts[colno])
		{
			pfree(opts[colno]);
		}
	}
	if (opts)
	{
		pfree(opts);
	}
	AppendOnlyVisimap_Init(&aocsFetchDesc->visibilityMap,
						   aoentry->visimaprelid,
						   aoentry->visimapidxid,
						   AccessShareLock,
						   appendOnlyMetaDataSnapshot);

	return aocsFetchDesc;
}

/*
 * Fetch the tuple based on the given tuple id.
 *
 * If the 'slot' is not NULL, the tuple will be assigned to the slot.
 *
 * Return true if the tuple is found. Otherwise, return false.
 */
bool
aocs_fetch(AOCSFetchDesc aocsFetchDesc,
		   AOTupleId *aoTupleId,
		   TupleTableSlot *slot)
{
	int segmentFileNum = AOTupleIdGet_segmentFileNum(aoTupleId);
	int64 rowNum = AOTupleIdGet_rowNum(aoTupleId);
	int numCols = aocsFetchDesc->relation->rd_att->natts;
	int colno;
	bool found = true;
	bool isSnapshotAny = (aocsFetchDesc->snapshot == SnapshotAny);

	Assert(numCols > 0);

	/*
	 * Go through columns one by one. Check if the current block
	 * has the requested tuple. If so, fetch it. Otherwise, read
	 * the block that contains the requested tuple.
	 */
	for(colno=0; colno<numCols; colno++)
	{
		DatumStreamFetchDesc datumStreamFetchDesc = aocsFetchDesc->datumStreamFetchDesc[colno];

		/* If this column does not need to be fetched, skip it. */
		if (datumStreamFetchDesc == NULL)
			continue;

		elogif(Debug_appendonly_print_datumstream, LOG,
				 "aocs_fetch filePathName %s segno %u rowNum  " INT64_FORMAT
				 " firstRowNum " INT64_FORMAT " lastRowNum " INT64_FORMAT " ",
				 datumStreamFetchDesc->datumStream->ao_read.bufferedRead.filePathName,
				 datumStreamFetchDesc->currentSegmentFile.num,
				 rowNum,
				 datumStreamFetchDesc->currentBlock.firstRowNum,
				 datumStreamFetchDesc->currentBlock.lastRowNum);

		/*
		 * If the current block has the requested tuple, read it.
		 */
		if (datumStreamFetchDesc->currentSegmentFile.isOpen &&
			datumStreamFetchDesc->currentSegmentFile.num == segmentFileNum &&
			aocsFetchDesc->blockDirectory.currentSegmentFileNum == segmentFileNum &&
			datumStreamFetchDesc->currentBlock.have)
		{
			if (rowNum >= datumStreamFetchDesc->currentBlock.firstRowNum &&
				rowNum <= datumStreamFetchDesc->currentBlock.lastRowNum)
			{
				if (!isSnapshotAny && !AppendOnlyVisimap_IsVisible(&aocsFetchDesc->visibilityMap, aoTupleId))
				{
					found = false;
					break;
				}

				fetchFromCurrentBlock(aocsFetchDesc, rowNum, slot, colno);
				continue;
			}

			/*
			 * Otherwise, fetch the right block.
			 */
			if (AppendOnlyBlockDirectoryEntry_RangeHasRow(
					&(datumStreamFetchDesc->currentBlock.blockDirectoryEntry),
					rowNum))
			{
				/*
				 * The tuple is covered by the current Block Directory entry,
				 * but is it before or after our current block?
				 */
				if (rowNum < datumStreamFetchDesc->currentBlock.firstRowNum)
				{
					/*
					 * Set scan range to prior block
					 */
					positionFirstBlockOfRange(datumStreamFetchDesc);

					datumStreamFetchDesc->scanAfterFileOffset =
						datumStreamFetchDesc->currentBlock.fileOffset;
					datumStreamFetchDesc->scanLastRowNum =
						datumStreamFetchDesc->currentBlock.firstRowNum - 1;
				}
				else
				{
					/*
					 * Set scan range to following blocks.
					 */
					positionSkipCurrentBlock(datumStreamFetchDesc);
					positionLimitToEndOfRange(datumStreamFetchDesc);
				}

				if (!isSnapshotAny && !AppendOnlyVisimap_IsVisible(&aocsFetchDesc->visibilityMap, aoTupleId))
				{
					found = false;
					break;
				}

				if (!scanToFetchValue(aocsFetchDesc, rowNum, slot, colno))
				{
					found = false;
					break;
				}

				continue;
			}
		}

		/*
		 * Open or switch open, if necessary.
		 */
		if (datumStreamFetchDesc->currentSegmentFile.isOpen &&
			segmentFileNum != datumStreamFetchDesc->currentSegmentFile.num)
		{
			closeFetchSegmentFile(datumStreamFetchDesc);

			Assert(!datumStreamFetchDesc->currentSegmentFile.isOpen);
		}

		if (!datumStreamFetchDesc->currentSegmentFile.isOpen)
		{
			if (!openFetchSegmentFile(
					aocsFetchDesc,
					segmentFileNum,
					colno))
			{
				found = false;	// Segment file not in aoseg table..
						   		// Must be aborted or deleted and reclaimed.
				break;
			}

			/* Reset currentBlock info */
			resetCurrentBlockInfo(&(datumStreamFetchDesc->currentBlock));
		}

		/*
		 * Need to get the Block Directory entry that covers the TID.
		 */
		if (!AppendOnlyBlockDirectory_GetEntry(
				&aocsFetchDesc->blockDirectory,
				aoTupleId,
				colno,
				&datumStreamFetchDesc->currentBlock.blockDirectoryEntry))
		{
			found = false;	/* Row not represented in Block Directory. */
				   			/* Must be aborted or deleted and reclaimed. */
			break;
		}

		if (!isSnapshotAny && !AppendOnlyVisimap_IsVisible(&aocsFetchDesc->visibilityMap, aoTupleId))
		{
			found = false;
			break;
		}

		/*
		 * Set scan range covered by new Block Directory entry.
		 */
		positionFirstBlockOfRange(datumStreamFetchDesc);

		positionLimitToEndOfRange(datumStreamFetchDesc);

		if (!scanToFetchValue(aocsFetchDesc, rowNum, slot, colno))
		{
			found = false;
			break;
		}
	}

	if (found)
	{
		if (slot != NULL)
		{
			TupSetVirtualTupleNValid(slot, colno);
			slot_set_ctid(slot, (ItemPointer)aoTupleId);
		}
	}
	else
	{
		if (slot != NULL)
			slot = ExecClearTuple(slot);
	}

	return found;
}

void
aocs_fetch_finish(AOCSFetchDesc aocsFetchDesc)
{
	int colno;
	Relation relation = aocsFetchDesc->relation;

	Assert(relation != NULL && relation->rd_att != NULL);

	for (colno = 0; colno < relation->rd_att->natts; colno++)
	{
		DatumStreamFetchDesc datumStreamFetchDesc =
			aocsFetchDesc->datumStreamFetchDesc[colno];
		if (datumStreamFetchDesc != NULL)
		{
			Assert(datumStreamFetchDesc->datumStream != NULL);
			datumstreamread_close_file(datumStreamFetchDesc->datumStream);
			destroy_datumstreamread(datumStreamFetchDesc->datumStream);
			pfree(datumStreamFetchDesc);
		}
	}
	pfree(aocsFetchDesc->datumStreamFetchDesc);

	AppendOnlyBlockDirectory_End_forSearch(&aocsFetchDesc->blockDirectory);

	if (aocsFetchDesc->segmentFileInfo)
	{
		FreeAllAOCSSegFileInfo(aocsFetchDesc->segmentFileInfo, aocsFetchDesc->totalSegfiles);
		pfree(aocsFetchDesc->segmentFileInfo);
		aocsFetchDesc->segmentFileInfo = NULL;
	}

	RelationDecrementReferenceCount(aocsFetchDesc->relation);

	pfree(aocsFetchDesc->segmentFileName);
	pfree(aocsFetchDesc->basepath);

	pfree(aocsFetchDesc->aoEntry);
	AppendOnlyVisimap_Finish(&aocsFetchDesc->visibilityMap, AccessShareLock);
}

typedef struct AOCSUpdateDescData
{
	AOCSInsertDesc insertDesc;

	/*
	 * visibility map
	 */
	AppendOnlyVisimap visibilityMap;

	/*
	 * Visimap delete support structure.
	 * Used to handle out-of-order deletes
	 */
	AppendOnlyVisimapDelete visiMapDelete;

} AOCSUpdateDescData;

AOCSUpdateDesc
aocs_update_init(Relation rel, int segno)
{
	AOCSUpdateDesc desc = (AOCSUpdateDesc) palloc0(sizeof(AOCSUpdateDescData));

	desc->insertDesc = aocs_insert_init(rel, segno, true);

	AppendOnlyVisimap_Init(&desc->visibilityMap,
			desc->insertDesc->aoEntry->visimaprelid,
			desc->insertDesc->aoEntry->visimapidxid,
			RowExclusiveLock,
			desc->insertDesc->appendOnlyMetaDataSnapshot);

	AppendOnlyVisimapDelete_Init(&desc->visiMapDelete,
			&desc->visibilityMap);

	return desc;
}

void
aocs_update_finish(AOCSUpdateDesc desc)
{
	Assert(desc);

	AppendOnlyVisimapDelete_Finish(&desc->visiMapDelete);

	aocs_insert_finish(desc->insertDesc);
	desc->insertDesc = NULL;

	/* Keep lock until the end of transaction */
	AppendOnlyVisimap_Finish(&desc->visibilityMap, NoLock);

	pfree(desc);
}

HTSU_Result
aocs_update(AOCSUpdateDesc desc, TupleTableSlot *slot,
			AOTupleId *oldTupleId, AOTupleId *newTupleId)
{
	Oid oid;
	HTSU_Result result;

	Assert(desc);
	Assert(oldTupleId);
	Assert(newTupleId);

#ifdef FAULT_INJECTOR
	FaultInjector_InjectFaultIfSet(
		AppendOnlyUpdate,
		DDLNotSpecified,
		"",	// databaseName
		RelationGetRelationName(desc->insertDesc->aoi_rel)); // tableName
#endif

	result = AppendOnlyVisimapDelete_Hide(&desc->visiMapDelete, oldTupleId);
	if (result != HeapTupleMayBeUpdated)
		return result;

	slot_getallattrs(slot);
	oid = aocs_insert_values(desc->insertDesc,
			slot_get_values(slot), slot_get_isnull(slot),
			newTupleId);
	(void) oid; /*ignore the oid value */

	return result;
 }


/*
 * AOCSDeleteDescData is used for delete data from AOCS relations.
 * It serves an equivalent purpose as AppendOnlyScanDescData
 * (relscan.h) only that the later is used for scanning append-only
 * relations.
 */
typedef struct AOCSDeleteDescData
{
	/*
	 * Relation to delete from
	 */
	Relation		aod_rel;

	/*
	 * Snapshot to use for meta data operations
	 */
	Snapshot		appendOnlyMetaDataSnapshot;

	/*
	 * pg_appendonly entry for the append-only relation
	 */
	AppendOnlyEntry *aoEntry;

	/*
	 * visibility map
	 */
	AppendOnlyVisimap visibilityMap;

	/*
	 * Visimap delete support structure.
	 * Used to handle out-of-order deletes
	 */
	AppendOnlyVisimapDelete visiMapDelete;

} AOCSDeleteDescData;


/*
 * appendonly_delete_init
 *
 * before using appendonly_delete() to delete tuples from append-only segment
 * files, we need to call this function to initialize the delete desc
 * data structured.
 */
AOCSDeleteDesc
aocs_delete_init(Relation rel)
{
	/*
	 * Get the pg_appendonly information
	 */
	AppendOnlyEntry *aoentry = GetAppendOnlyEntry(RelationGetRelid(rel),
			SnapshotNow);
	Assert(aoentry && aoentry->majorversion == 1 && aoentry->minorversion == 1);

	AOCSDeleteDesc aoDeleteDesc = palloc0(sizeof(AOCSDeleteDescData));
	aoDeleteDesc->aod_rel = rel;
	aoDeleteDesc->appendOnlyMetaDataSnapshot = SnapshotNow;
	aoDeleteDesc->aoEntry = aoentry;

	AppendOnlyVisimap_Init(&aoDeleteDesc->visibilityMap,
			aoentry->visimaprelid,
			aoentry->visimapidxid,
			RowExclusiveLock,
			aoDeleteDesc->appendOnlyMetaDataSnapshot);

	AppendOnlyVisimapDelete_Init(&aoDeleteDesc->visiMapDelete,
			&aoDeleteDesc->visibilityMap);

	return aoDeleteDesc;
}

void aocs_delete_finish(AOCSDeleteDesc aoDeleteDesc)
{
	Assert(aoDeleteDesc);

	AppendOnlyVisimapDelete_Finish(&aoDeleteDesc->visiMapDelete);

	if (aoDeleteDesc->aoEntry != NULL)
	{
		pfree(aoDeleteDesc->aoEntry);
	}

	AppendOnlyVisimap_Finish(&aoDeleteDesc->visibilityMap, NoLock);

	pfree(aoDeleteDesc);
}

HTSU_Result aocs_delete(AOCSDeleteDesc aoDeleteDesc,
		AOTupleId* aoTupleId)
{
	Assert(aoDeleteDesc);
	Assert(aoTupleId);

	elogif (Debug_appendonly_print_delete, LOG,
		"AOCS delete tuple from table '%s' (AOTupleId %s)",
		NameStr(aoDeleteDesc->aod_rel->rd_rel->relname),
		AOTupleIdToString(aoTupleId));

#ifdef FAULT_INJECTOR
	FaultInjector_InjectFaultIfSet(
		AppendOnlyDelete,
		DDLNotSpecified,
		"",	// databaseName
		RelationGetRelationName(aoDeleteDesc->aod_rel)); // tableName
#endif

	return AppendOnlyVisimapDelete_Hide(&aoDeleteDesc->visiMapDelete, aoTupleId);
}

/*
 * Initialize a scan on varblock headers in an AOCS segfile.  The
 * segfile is identified by colno.
 */
AOCSHeaderScanDesc
aocs_begin_headerscan(Relation rel,
					  AppendOnlyEntry *aoentry,
					  int colno)
{
	AOCSHeaderScanDesc hdesc;
	AppendOnlyStorageAttributes ao_attr;
	StdRdOptions 	**opts = RelationGetAttributeOptions(rel);

	Assert(opts[colno]);

	ao_attr.checksum = aoentry->checksum;

	/*
	 * We are concerned with varblock headers only, not their content.
	 * Therefore, don't waste cycles in decompressing the content.
	 */
	ao_attr.compress = false;
	ao_attr.compressType = NULL;
	ao_attr.compressLevel = 0;
	ao_attr.overflowSize = 0;
	ao_attr.safeFSWriteSize = 0;
	ao_attr.version = aoentry->version;
	hdesc = palloc(sizeof(AOCSHeaderScanDescData));
	AppendOnlyStorageRead_Init(&hdesc->ao_read,
							   NULL, // current memory context
							   opts[colno]->blocksize,
							   RelationGetRelationName(rel),
							   "ALTER TABLE ADD COLUMN scan",
							   &ao_attr);
	hdesc->colno = colno;
	return hdesc;
}

/*
 * Open AOCS segfile for scanning varblock headers.
 */
void aocs_headerscan_opensegfile(AOCSHeaderScanDesc hdesc,
								 AOCSFileSegInfo *seginfo,
								 char *basepath)
{
	AOCSVPInfoEntry *vpe;
	char fn[MAXPGPATH];
	int32 fileSegNo;

	/* Close currently open segfile, if any. */
	AppendOnlyStorageRead_CloseFile(&hdesc->ao_read);
	FormatAOSegmentFileName(basepath, seginfo->segno,
							hdesc->colno, &fileSegNo, fn);
	Assert(strlen(fn) + 1 <= MAXPGPATH);
	vpe = getAOCSVPEntry(seginfo, hdesc->colno);
	AppendOnlyStorageRead_OpenFile(&hdesc->ao_read, fn, vpe->eof);
}

bool aocs_get_nextheader(AOCSHeaderScanDesc hdesc)
{
	if (hdesc->ao_read.current.firstRowNum > 0)
	{
		AppendOnlyStorageRead_SkipCurrentBlock(&hdesc->ao_read);
	}
	return AppendOnlyStorageRead_ReadNextBlock(&hdesc->ao_read);
}

void aocs_end_headerscan(AOCSHeaderScanDesc hdesc)
{
	AppendOnlyStorageRead_CloseFile(&hdesc->ao_read);
	AppendOnlyStorageRead_FinishSession(&hdesc->ao_read);
	pfree(hdesc);
}

/*
 * Initialize one datum stream per new column for writing.
 */
AOCSAddColumnDesc
aocs_addcol_init(Relation rel,
				 AppendOnlyEntry *aoentry,
				 int num_newcols)
{
	char *ct;
	int32 clvl;
	int32 blksz;
	AOCSAddColumnDesc desc;
	int i;
	int iattr;
	StringInfoData titleBuf;

	desc = palloc(sizeof(AOCSAddColumnDescData));
	desc->num_newcols = num_newcols;
	desc->rel = rel;
	desc->aoEntry = aoentry;
	desc->cur_segno = -1;

	/*
	 * Rewrite catalog phase of alter table has updated catalog with
	 * info for new columns, which is available through rel.
	 */
	StdRdOptions 	**opts = RelationGetAttributeOptions(rel);
	desc->dsw = palloc(sizeof(DatumStreamWrite*) * desc->num_newcols);

	iattr = rel->rd_att->natts - num_newcols;
	for (i = 0; i < num_newcols; ++i, ++iattr)
	{
		Form_pg_attribute attr = rel->rd_att->attrs[iattr];

		initStringInfo(&titleBuf);
		appendStringInfo(&titleBuf, "ALTER TABLE ADD COLUMN new segfile");

		Assert(opts[iattr]);
		ct = opts[iattr]->compresstype;
		clvl = opts[iattr]->compresslevel;
		blksz = opts[iattr]->blocksize;
		desc->dsw[i] = create_datumstreamwrite(
				ct, clvl, aoentry->checksum, 0, blksz /* safeFSWriteSize */,
				aoentry->version, attr, RelationGetRelationName(rel),
				titleBuf.data);
	}
	return desc;
}

/*
 * Create new physical segfiles for each newly added column.
 */
void aocs_addcol_newsegfile(AOCSAddColumnDesc desc,
							AOCSFileSegInfo *seginfo,
							char *basepath,
							RelFileNode relfilenode)
{
	int32 fileSegNo;
	char fn[MAXPGPATH];
	int i;
	/* Column numbers of newly added columns start from here. */
	AttrNumber colno = desc->rel->rd_att->natts - desc->num_newcols;
	if (desc->dsw[0]->need_close_file)
	{
		aocs_addcol_closefiles(desc);
		AppendOnlyBlockDirectory_End_addCol(&desc->blockDirectory);
	}
	AppendOnlyBlockDirectory_Init_addCol(
			&desc->blockDirectory,
			desc->aoEntry,
			SnapshotNow,
			(FileSegInfo *)seginfo,
			desc->rel,
			seginfo->segno,
			desc->num_newcols,
			true /* isAOCol */);
	for (i = 0; i < desc->num_newcols; ++i, ++colno)
	{
		FormatAOSegmentFileName(basepath, seginfo->segno, colno,
								&fileSegNo, fn);
		Assert(strlen(fn) + 1 <= MAXPGPATH);
		datumstreamwrite_open_file(desc->dsw[i], fn,
								   0 /* eof */, 0 /* eof_uncompressed */,
								   relfilenode, fileSegNo);
		desc->dsw[i]->blockFirstRowNum = 1;
	}
	desc->cur_segno = seginfo->segno;
}

void aocs_addcol_closefiles(AOCSAddColumnDesc desc)
{
	int i;
	AttrNumber colno = desc->rel->rd_att->natts - desc->num_newcols;
	int itemCount;
	for (i = 0; i < desc->num_newcols; ++i)
	{
		itemCount = datumstreamwrite_nth(desc->dsw[i]);
		datumstreamwrite_block(desc->dsw[i]);
		AppendOnlyBlockDirectory_addCol_InsertEntry(
			&desc->blockDirectory,
			i + colno,
			desc->dsw[i]->blockFirstRowNum,
			AppendOnlyStorageWrite_LastWriteBeginPosition(
					&desc->dsw[i]->ao_write),
			itemCount);
		datumstreamwrite_close_file(desc->dsw[i]);
	}
	/* Update pg_aocsseg_* with eof of each segfile we just closed. */
	AOCSFileSegInfoAddVpe(desc->rel, desc->aoEntry, desc->cur_segno, desc,
						  desc->num_newcols, false /* non-empty VPEntry */);
}

/*
 * Force writing new varblock in each segfile open for insert.
 */
void aocs_addcol_endblock(AOCSAddColumnDesc desc, int64 firstRowNum)
{
	int i;
	AttrNumber colno = desc->rel->rd_att->natts - desc->num_newcols;
	int itemCount;
	for (i = 0; i < desc->num_newcols; ++i)
	{
		itemCount = datumstreamwrite_nth(desc->dsw[i]);
		datumstreamwrite_block(desc->dsw[i]);
		AppendOnlyBlockDirectory_addCol_InsertEntry(
			&desc->blockDirectory,
			i + colno,
			desc->dsw[i]->blockFirstRowNum,
			AppendOnlyStorageWrite_LastWriteBeginPosition(
					&desc->dsw[i]->ao_write),
			itemCount);
		/*
		 * Next block's first row number.  In this case, the block
		 * being ended has less number of rows than its capacity.
		 */
		desc->dsw[i]->blockFirstRowNum = firstRowNum;
	}
}

/*
 * Insert one new datum for each new column being added.  This is
 * derived from aocs_insert_values().
 */
void aocs_addcol_insert_datum(AOCSAddColumnDesc desc, Datum *d, bool *isnull)
{
	void *toFree1;
	void *toFree2;
	Datum datum;
	int err;
	int i;
	int itemCount;

	/* first column's number */
	AttrNumber colno = desc->rel->rd_att->natts - desc->num_newcols;

	for (i = 0; i < desc->num_newcols; ++i)
	{
		datum = d[i];
		err  = datumstreamwrite_put(desc->dsw[i], datum, isnull[i], &toFree1);
		if (toFree1 != NULL)
		{
			/*
			 * Use the de-toasted and/or de-compressed as datum instead.
			 */
			datum = PointerGetDatum(toFree1);
		}
		if (err < 0)
		{
			/*
			 * We have reached max number of datums that can be
			 * accommodated in current varblock.
			 */
			itemCount = datumstreamwrite_nth(desc->dsw[i]);
			/* write the block up to this one */
			datumstreamwrite_block(desc->dsw[i]);
			if (itemCount > 0)
			{
				AppendOnlyBlockDirectory_addCol_InsertEntry(
						&desc->blockDirectory,
						i + colno,
						desc->dsw[i]->blockFirstRowNum,
						AppendOnlyStorageWrite_LastWriteBeginPosition(
								&desc->dsw[i]->ao_write),
						itemCount);
				/* Next block's first row number */
				desc->dsw[i]->blockFirstRowNum += itemCount;
			}

			/* now write this new item to the new block */
			err = datumstreamwrite_put(desc->dsw[i], datum, isnull[i],
									   &toFree2);
			Assert(toFree2 == NULL);
			if (err < 0)
			{
				Assert(!isnull[i]);
				/*
				 * rle_type is running on a block stream, if an object
				 * spans multiple blocks then data will not be compressed
				 * (if rle_type is set).
				 */
				if (desc->dsw[i]->rle_want_compression)
				{
					desc->dsw[i]->ao_write.storageAttributes.compress = FALSE;
				}
				err = datumstreamwrite_lob(desc->dsw[i], datum);
				Assert(err >= 0);
			}
		}
		if (toFree1 != NULL)
		{
			pfree(toFree1);
		}
	}
}

void aocs_addcol_finish(AOCSAddColumnDesc desc)
{
	int i;
	aocs_addcol_closefiles(desc);
	AppendOnlyBlockDirectory_End_addCol(&desc->blockDirectory);
	for (i = 0; i < desc->num_newcols; ++i)
		destroy_datumstreamwrite(desc->dsw[i]);
	pfree(desc->dsw);
	if(desc->aoEntry)
	{
		pfree(desc->aoEntry);
	}

	pfree(desc);
}

/*
 * Add empty VPEs (eof=0) to pg_aocsseg_* catalog, corresponding to
 * each new column being added.
 */
void aocs_addcol_emptyvpe(Relation rel, AppendOnlyEntry *aoentry,
						  AOCSFileSegInfo **segInfos, int32 nseg,
						  int num_newcols)
{
	int i;
	for (i = 0; i < nseg; ++i)
	{
		if (Gp_role == GP_ROLE_DISPATCH || segInfos[i]->total_tupcount == 0)
		{
			/*
			 * On QD, all tuples in pg_aocsseg_* catalog have eof=0.
			 * On QE, tuples with eof=0 may exist in pg_aocsseg_*
			 * already, caused by VACUUM.  We need to add
			 * corresponding tuples with eof=0 for each newly added
			 * column on QE.
			 */
			AOCSFileSegInfoAddVpe(rel, aoentry, segInfos[i]->segno, NULL,
								  num_newcols, true /* empty VPEntry */);
		}
	}
}

/*
 * EXX_IN_PG:
 */
enum {
    SEG_NOT_OPEN    = 0,   // Just emphasize that not open need to be 0.
    SEG_SINGLE_MODE,
    SEG_BATCH_MODE,
};

/*
 * XXX: This is horrible.  
 * AOCS: The interface is just wrong, twisted, and expose unnessary details.
 * The implementation is impossible to untangle.   So here is what we do (and
 * to make it worse :-), we try to open a seg, if it is nice (no lob), then 
 * we go in batch mode, otherwise, we fall back to single mode.   Note that
 * in batch mode, we may switch to single mode with in the same seg if we read
 * in a block with lob.    The hope is that lob, is really not a target usecase
 * and it will be slow, so be it, but we make the common case faster. 
 *
 * This code dive into data structures of scan, DataStreamRead, DataStreamReadBlock, 
 * too bad.
 */
int aocs_getnextbatch(AOCSScanDesc scan, ExxAocsBatchReaderCtxt *ctxt)
{
    int err = 0;
    bool isSnapshotAny = (scan->snapshot == SnapshotAny);
    int nfill; 
    int64_t rowNum; 

    // Only used in IndexBuild.   exx wont see this.
    Assert(!scan->buildBlockDirectory); 

    // open next seg.  Goto?  Well, it has the superficial benefit of if we are scanning
    // a huge table but all tuples are invisible, recursive call may exceed stack limit. 
    // Really? 
NextSeg:
    rowNum = -1;
    nfill = 0;
    if (ctxt->seg_status == SEG_NOT_OPEN) { 
        if (scan->cur_seg < 0 || err < 0) {
            err = open_next_scan_seg(scan);
            if (err < 0) {
                scan->cur_seg = -1;
                return 0;
            }
            scan->cur_seg_row = 0;
        }

        ctxt->seg_status = SEG_BATCH_MODE;
    
        // next, check if we are in single mode.
        for (int i = 0; i < ctxt->ncol; i++) {
            int projcol = ctxt->colattrs[i] - 1;
            Assert( scan->proj[projcol]);

            DatumStreamRead *ds = scan->ds[projcol]; 
            ctxt->row_available[i] = ds->blockRead.logical_row_count; 
            if (ds->largeObjectState != DatumStreamLargeObjectState_None) {
                ctxt->seg_status = SEG_SINGLE_MODE;
            }
        }
    } 
    
    if (ctxt->seg_status == SEG_BATCH_MODE) {
        // Batch mode, let's make sure each seg has something to read.
        for (int i = 0; i < ctxt->ncol; i++) {
            if (ctxt->row_available[i] == 0) {
                DatumStreamRead *ds = scan->ds[ctxt->colattrs[i] - 1];
                err = datumstreamread_block(ds);
                if (err < 0) {
                    // Exhuasted, move on to next seg.
                    close_cur_scan_seg(scan);
                    ctxt->seg_status = SEG_NOT_OPEN;
                    goto NextSeg;
                }
                ctxt->row_available[i] = ds->blockRead.logical_row_count; 

                /*
                 * Important.  Within same segment, a block may switch from no-lob to have-lob.
                 * We switch to signle row mode (and will never switch back until the segment is
                 * exhausted).
                 */
                if (ds->largeObjectState != DatumStreamLargeObjectState_None) {
                    ctxt->seg_status = SEG_SINGLE_MODE;
                }
            }
        }
    }

    if (ctxt->seg_status == SEG_SINGLE_MODE) {
        int row = 0;
        // Roughly same code as aocs_getnext.
        while (nfill == 0) {
            for (int col = 0; col < ctxt->ncol; col++) {
                DatumStreamRead *ds = scan->ds[ctxt->colattrs[col] - 1];
                err = datumstreamread_advance(ds);
                if (err == 0) {
                    err = datumstreamread_block(ds);
                    if (err < 0) {
                        // Current seg exhausted.   Move to next seg.
                        // All datastream should be exhausted, we simply will catch the first one.
                        Assert( col == 0);
                        close_cur_scan_seg(scan);
                        ctxt->seg_status = SEG_NOT_OPEN;
                        goto NextSeg;
                    }
                    err = datumstreamread_advance(ds);
                    Assert( err > 0);
                } 

                datumstreamread_get(ds, &ctxt->datum[row][col], &ctxt->isnull[row][col]); 
                if (rowNum == -1 && ds->blockFirstRowNum != -1) {
                    rowNum = ds->blockFirstRowNum + datumstreamread_nth(ds);
                }
            }

            AOTupleId aotid;
            AOTupleIdInit_Init(&aotid); 
            AOTupleIdInit_segmentFileNum(&aotid, scan->seginfo[scan->cur_seg]->segno);

            scan->cur_seg_row++;
            if (rowNum == -1) {
                AOTupleIdInit_rowNum(&aotid, scan->cur_seg_row);
            } else {
                AOTupleIdInit_rowNum(&aotid, rowNum); 
            }

            if (!isSnapshotAny && !AppendOnlyVisimap_IsVisible(&scan->visibilityMap, &aotid)) {
                rowNum = -1;
            } else {
                nfill++;
                if (ctxt->aotid) {
                    ctxt->aotid[row] = aotid;
                }
            }
        }
        Assert(nfill == 1);
        return nfill;
    }

    // Now batch mode.   First, let find out how many we can fill for this batch.
    Assert( ctxt->seg_status == SEG_BATCH_MODE);
    int nfillmax = ctxt->maxrows;
    Assert( nfillmax <= EXX_AOCS_BATCH_NROW);

    for (int i = 0; i < ctxt->ncol; i++) {
        DatumStreamRead *ds = scan->ds[ctxt->colattrs[i] - 1];
        Assert( ctxt->row_available[i] > 0);
        if (ctxt->row_available[i] < nfillmax) {
            nfillmax = ctxt->row_available[i];
        }

        if (rowNum == -1 && ds->blockFirstRowNum != -1) {
            // nth, we have not advance it yet.  the bitmap visibility code will advance first, then
            // call nth, hence, off by 1.
            rowNum = ds->blockFirstRowNum + datumstreamread_nth(ds) + 1;
        }
    }

    if (rowNum == -1) {
        rowNum = scan->cur_seg_row + 1; 
    }

    Assert( nfillmax > 0);

    if (isSnapshotAny) {
        // All rows are good.
        nfill = nfillmax;

        // Fill aotid if caller needs it.
        if (ctxt->aotid) {
            for (int i = 0; i < nfill; i++) {
                AOTupleIdInit_Init(&ctxt->aotid[i]);
                AOTupleIdInit_segmentFileNum(&ctxt->aotid[i], scan->seginfo[scan->cur_seg]->segno);
                AOTupleIdInit_rowNum(&ctxt->aotid[i], rowNum + i); 
            } 
        }

        for (int col = 0; col < ctxt->ncol; col++) {
            DatumStreamRead *ds = scan->ds[ctxt->colattrs[col] - 1];
            for (int row = 0; row < nfill; row++) {
                err = DatumStreamBlockRead_Advance(&ds->blockRead);
                Assert( err > 0);
                DatumStreamBlockRead_Get(&ds->blockRead, &ctxt->datum[row][col], &ctxt->isnull[row][col]);
            }
            ctxt->row_available[col] -= nfill;
        }
    } else {
        bool vis[nfillmax];
        AOTupleId aotid;

        Assert(nfill == 0);
        Assert(nfillmax <= 65536);

        for (int i = 0; i < nfillmax; i++) {
            AOTupleIdInit_Init(&aotid); 
            AOTupleIdInit_segmentFileNum(&aotid, scan->seginfo[scan->cur_seg]->segno);
            AOTupleIdInit_rowNum(&aotid, rowNum + i); 
            vis[i] = AppendOnlyVisimap_IsVisible(&scan->visibilityMap, &aotid); 
            if (vis[i]) {
                if (ctxt->aotid) {
                    ctxt->aotid[nfill] = aotid;
                }
                nfill++;
            }
        }

        for (int col = 0; col < ctxt->ncol; col++) {
            DatumStreamRead *ds = scan->ds[ctxt->colattrs[col] - 1];
            int row = 0;
            for (int lrow = 0; lrow < nfillmax; lrow++) {
                err = DatumStreamBlockRead_Advance(&ds->blockRead);
                Assert( err > 0);
                if (vis[lrow]) {
                    DatumStreamBlockRead_Get(&ds->blockRead, &ctxt->datum[row][col], &ctxt->isnull[row][col]);
                    row++;
                }
            }
            Assert( row == nfill);
            Assert( nfillmax <= ctxt->row_available[col]);
            ctxt->row_available[col] -= nfillmax;
        }
    }

    scan->cur_seg_row += nfillmax;

    // 
    // Well this is possible, if all rows are not visible.   Just run it again. 
    //
    if (nfill == 0) {
        goto NextSeg;
    }

    return nfill;
}

