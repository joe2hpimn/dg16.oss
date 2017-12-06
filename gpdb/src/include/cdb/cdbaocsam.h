/*-------------------------------------------------------------------------
 *
 * cdbaocsam.h
 *	  append-only columnar relation access method definitions.
 *
 * Portions Copyright (c) 2009, Greenplum Inc.
 *
 *-------------------------------------------------------------------------
 */
#ifndef CDB_AOCSAM_H
#define CDB_AOCSAM_H

#include "access/relscan.h"
#include "access/sdir.h"
#include "access/tupmacs.h"
#include "access/xlogutils.h"
#include "access/appendonlytid.h"
#include "access/appendonly_visimap.h"
#include "executor/tuptable.h"
#include "nodes/primnodes.h"
#include "storage/block.h"
#include "storage/lmgr.h"
#include "utils/rel.h"
#include "utils/tqual.h"
#include "cdb/cdbappendonlyblockdirectory.h"
#include "cdb/cdbappendonlystoragelayer.h"
#include "cdb/cdbappendonlystorageread.h"
#include "cdb/cdbappendonlystoragewrite.h"
#include "utils/datumstream.h"

/*
 * AOCSInsertDescData is used for inserting data into append-only columnar
 * relations. It serves an equivalent purpose as AOCSScanDescData
 * only that the later is used for scanning append-only columnar
 * relations.
 */
struct DatumStream;
struct AOCSFileSegInfo;

typedef struct AOCSInsertDescData
{
	Relation	aoi_rel;
	Snapshot	appendOnlyMetaDataSnapshot;
	AOCSFileSegInfo *fsInfo;
	int64		insertCount;
	int64		varblockCount;
	int64		rowCount; /* total row count before insert */
	int64		numSequences; /* total number of available sequences */
	int64		lastSequence; /* last used sequence */
	int32		cur_segno;
	AppendOnlyEntry *aoEntry;

	char *compType;
	int32 compLevel;
	int32 blocksz;

	struct DatumStreamWrite **ds;

	AppendOnlyBlockDirectory blockDirectory;

	/**
	 * When initialized in update mode, the insert is really part of
	 * an AO update.
	 * Certain statistics are then counted differently.
	 */ 
	bool update_mode;
} AOCSInsertDescData;

typedef AOCSInsertDescData *AOCSInsertDesc;

/*
 * used for scan of append only relations using BufferedRead and VarBlocks
 */
typedef struct AOCSScanDescData
{
	/* scan parameters */
	Relation	aos_rel;			/* target relation descriptor */
	Snapshot	appendOnlyMetaDataSnapshot;

	/*
	 * Snapshot to use for non-metadata operations.
	 * Usually snapshot = appendOnlyMetaDataSnapshot, but they
	 * differ e.g. if gp_select_invisible is set.
	 */ 
	Snapshot    snapshot;

	/* tuple descriptor of table to scan.
	 *  Code should use this rather than aos_rel->rd_att,
	 *  as THEY MAY BE DIFFERENT.
	 *  See code in aocsam.c's aocs_beginscan for more info
	 */
	TupleDesc   relationTupleDesc;	

	Index aos_scanrelid; /* index */

	int total_seg;
	int cur_seg;

	char *compType;
	int32 compLevel;
	int32 blocksz;

	struct AOCSFileSegInfo **seginfo;
	struct DatumStreamRead **ds;
	bool *proj;

	/* synthetic system attributes */
	ItemPointerData cdb_fake_ctid;
	int64 total_row;
	int64 cur_seg_row;

	AppendOnlyEntry *aoEntry;

	/*
	 * The block directory info.
	 *
	 * For CO tables that are upgraded from pre-3.4 release, the block directory
	 * built during the first index creation.
	 */
	bool buildBlockDirectory;
	AppendOnlyBlockDirectory *blockDirectory;

	AppendOnlyVisimap visibilityMap;
}	AOCSScanDescData;

typedef AOCSScanDescData *AOCSScanDesc;

/*
 * Used for fetch individual tuples from specified by TID of append only relations
 * using the AO Block Directory.
 */
typedef struct AOCSFetchDescData
{
	Relation		relation;
	Snapshot		appendOnlyMetaDataSnapshot;

	/*
	 * Snapshot to use for non-metadata operations.
	 * Usually snapshot = appendOnlyMetaDataSnapshot, but they
	 * differ e.g. if gp_select_invisible is set.
	 */ 
	Snapshot    snapshot;

	MemoryContext	initContext;

	int				totalSegfiles;
	struct AOCSFileSegInfo **segmentFileInfo;
	bool			*proj;

	char			*segmentFileName;
	int				segmentFileNameMaxLen;
	char            *basepath;

	AppendOnlyBlockDirectory	blockDirectory;

	DatumStreamFetchDesc *datumStreamFetchDesc;

	AppendOnlyEntry *aoEntry;

	int64	skipBlockCount;

	AppendOnlyVisimap visibilityMap;

} AOCSFetchDescData;

typedef AOCSFetchDescData *AOCSFetchDesc;

typedef struct AOCSUpdateDescData *AOCSUpdateDesc;
typedef struct AOCSDeleteDescData *AOCSDeleteDesc;

typedef struct AOCSHeaderScanDescData
{
	int32 colno;  /* chosen column number to read headers from */

	AppendOnlyStorageRead ao_read;

} AOCSHeaderScanDescData;

typedef AOCSHeaderScanDescData *AOCSHeaderScanDesc;

typedef struct AOCSAddColumnDescData
{
	Relation rel;

	AppendOnlyEntry *aoEntry;

	AppendOnlyBlockDirectory blockDirectory;

	DatumStreamWrite **dsw;
	/* array of datum stream write objects, one per new column */

	int num_newcols;

	int32 cur_segno;

} AOCSAddColumnDescData;
typedef AOCSAddColumnDescData *AOCSAddColumnDesc;

/* ----------------
 *		function prototypes for appendonly access method
 * ----------------
 */

extern AOCSScanDesc aocs_beginscan(Relation relation, Snapshot snapshot,
		Snapshot appendOnlyMetaDataSnapshot, TupleDesc relationTupleDesc, bool *proj);
extern AOCSScanDesc aocs_beginrangescan(Relation relation, Snapshot snapshot,
		Snapshot appendOnlyMetaDataSnapshot, 
		int *segfile_no_arr, int segfile_count,
	TupleDesc relationTupleDesc, bool *proj);

extern void aocs_rescan(AOCSScanDesc scan);
extern void aocs_endscan(AOCSScanDesc scan);

extern void aocs_getnext(AOCSScanDesc scan, ScanDirection direction, TupleTableSlot *slot);
extern AOCSInsertDesc aocs_insert_init(Relation rel, int segno, bool update_mode);
extern Oid aocs_insert_values(AOCSInsertDesc idesc, Datum *d, bool *null, AOTupleId *aoTupleId);
static inline Oid aocs_insert(AOCSInsertDesc idesc, TupleTableSlot *slot)
{
	Oid oid;
	AOTupleId aotid;

	slot_getallattrs(slot);
	oid = aocs_insert_values(idesc, slot_get_values(slot), slot_get_isnull(slot), &aotid);
	slot_set_ctid(slot, (ItemPointer)&aotid);

	return oid;
}
extern void aocs_insert_finish(AOCSInsertDesc idesc);
extern AOCSFetchDesc aocs_fetch_init(Relation relation,
									 Snapshot snapshot,
									 Snapshot appendOnlyMetaDataSnapshot,
									 bool *proj);
extern bool aocs_fetch(AOCSFetchDesc aocsFetchDesc,
					   AOTupleId *aoTupleId,
					   TupleTableSlot *slot);
extern void aocs_fetch_finish(AOCSFetchDesc aocsFetchDesc);

extern AOCSUpdateDesc aocs_update_init(Relation rel, int segno);
extern void aocs_update_finish(AOCSUpdateDesc desc);
extern HTSU_Result aocs_update(AOCSUpdateDesc desc, TupleTableSlot *slot,
			AOTupleId *oldTupleId, AOTupleId *newTupleId);

extern AOCSDeleteDesc aocs_delete_init(Relation rel);
extern HTSU_Result aocs_delete(AOCSDeleteDesc desc, 
		AOTupleId *aoTupleId);
extern void aocs_delete_finish(AOCSDeleteDesc desc);

extern AOCSHeaderScanDesc aocs_begin_headerscan(
		Relation rel, AppendOnlyEntry *aoentry, int colno);
extern void aocs_headerscan_opensegfile(
		AOCSHeaderScanDesc hdesc, AOCSFileSegInfo *seginfo, char *basepath);
extern bool aocs_get_nextheader(AOCSHeaderScanDesc hdesc);
extern void aocs_end_headerscan(AOCSHeaderScanDesc hdesc);
extern AOCSAddColumnDesc aocs_addcol_init(
		Relation rel, AppendOnlyEntry *aoentry, int num_newcols);
extern void aocs_addcol_newsegfile(
		AOCSAddColumnDesc desc, AOCSFileSegInfo *seginfo, char *basepath,
		RelFileNode relfilenode);
extern void aocs_addcol_closefiles(AOCSAddColumnDesc desc);
extern void aocs_addcol_endblock(AOCSAddColumnDesc desc, int64 firstRowNum);
extern void aocs_addcol_insert_datum(AOCSAddColumnDesc desc,
									   Datum *d, bool *isnull);
extern void aocs_addcol_finish(AOCSAddColumnDesc desc);
extern void aocs_addcol_emptyvpe(
		Relation rel, AppendOnlyEntry *aoentry, AOCSFileSegInfo **segInfos,
		int32 nseg, int num_newcols);



/*
 * EXX_IN_PG.
 *  Batch reader mode 
 *  Returns #tup returned. 0 if at EOF.
 */
#define EXX_AOCS_BATCH_NROW 1024 
typedef struct ExxAocsBatchReaderCtxt
{
    int ncol;
    int *colattrs;
    int maxrows;
    Datum *datum[EXX_AOCS_BATCH_NROW];
    bool *isnull[EXX_AOCS_BATCH_NROW];
    AOTupleId *aotid;

    /*
     * ftian: I need some context.  This way, we can fix the interface.  You just code it.
     * Whatever I need, follows this, and you just ignore.
     */
    int seg_status;
    int *row_available; 
} ExxAocsBatchReaderCtxt;

extern int aocs_getnextbatch(AOCSScanDesc scan, ExxAocsBatchReaderCtxt *ctxt);

#endif   /* AOCSAM_H */
