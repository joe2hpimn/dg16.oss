/*-------------------------------------------------------------------------
 *
 * cdbpersistentbuild.c
 *
 * Copyright (c) 2009-2010, Greenplum inc
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "utils/palloc.h"
#include "storage/fd.h"
#include "storage/relfilenode.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/persistentfilesysobjname.h"
#include "access/nbtree.h"
#include "access/transam.h"
#include "catalog/catalog.h"
#include "catalog/catquery.h"
#include "catalog/heap.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_tablespace.h"
#include "catalog/pg_database.h"
#include "catalog/gp_persistent.h"
#include "cdb/cdbdirectopen.h"
#include "cdb/cdbpersistentstore.h"
#include "cdb/cdbpersistentfilesysobj.h"
#include "cdb/cdbpersistentdatabase.h"
#include "cdb/cdbpersistenttablespace.h"
#include "cdb/cdbpersistentrelation.h"
#include "storage/itemptr.h"
#include "utils/hsearch.h"
#include "storage/shmem.h"
#include "utils/guc.h"
#include "storage/smgr.h"
#include "utils/builtins.h"
#include "cdb/cdbdatabaseinfo.h"
#include "utils/syscache.h"
#include "storage/bufpage.h"
#include "utils/faultinjector.h"

static void
PersistentBuild_NonTransactionTruncate(RelFileNode *relFileNode)
{
	SMgrRelation smgrRelation;

	PersistentFileSysObjName fsObjName;

	PersistentFileSysObjName_SetRelationFile(
										&fsObjName, 
										relFileNode,
										/* segmentFileNum */ 0);
	if (Debug_persistent_print)
		elog(Persistent_DebugPrintLevel(), 
			 "Non-transaction truncate of '%s'", 
			 PersistentFileSysObjName_ObjectName(&fsObjName));
	
	smgrRelation = smgropen(*relFileNode);
	
	smgrtruncate(
			smgrRelation, 
			0, 
			/* isTemp */ true, 
			/* isLocalBuf */ false,
			/* persistentTid */ NULL,
			/* persistentSerialNum */ 0);
	
	smgrclose(smgrRelation);
}


static void PersistentBuild_ScanGpPersistentRelationNodeForGlobal(
	Relation 	gp_relation_node,

	int64		*count)
{
	PersistentFileSysObjData *fileSysObjData;
	PersistentFileSysObjSharedData	*fileSysObjSharedData;

	PersistentStoreScan storeScan;
	 
	Datum values[Natts_gp_persistent_relation_node];
	 
	ItemPointerData persistentTid;
	int64 persistentSerialNum;

	PersistentFileSysObj_GetDataPtrs(
								PersistentFsObjType_RelationFile,
								&fileSysObjData,
								&fileSysObjSharedData);
		 
	PersistentStore_BeginScan(
						&fileSysObjData->storeData,
						&fileSysObjSharedData->storeSharedData,
						&storeScan);

	while (PersistentStore_GetNext(
							&storeScan,
							values,
							&persistentTid,
							&persistentSerialNum))
	{
		RelFileNode 					relFileNode;
		int32 							segmentFileNum;
		
		PersistentFileSysRelStorageMgr	relationStorageManager;
		PersistentFileSysState			persistentState;
		int64							createMirrorDataLossTrackingSessionNum;
		MirroredObjectExistenceState	mirrorExistenceState;
		MirroredRelDataSynchronizationState mirrorDataSynchronizationState;
		bool							mirrorBufpoolMarkedForScanIncrementalResync;
		int64							mirrorBufpoolResyncChangedPageCount;
		XLogRecPtr						mirrorBufpoolResyncCkptLoc;
		BlockNumber 					mirrorBufpoolResyncCkptBlockNum;
		int64							mirrorAppendOnlyLossEof;
		int64							mirrorAppendOnlyNewEof;
		PersistentFileSysRelBufpoolKind relBufpoolKind;
		TransactionId					parentXid;
		int64							serialNum;
		ItemPointerData					previousFreeTid;
		
		PersistentFileSysObjName		fsObjName;

		GpPersistentRelationNode_GetValues(
										values,
										&relFileNode.spcNode,
										&relFileNode.dbNode,
										&relFileNode.relNode,
										&segmentFileNum,
										&relationStorageManager,
										&persistentState,
										&createMirrorDataLossTrackingSessionNum,
										&mirrorExistenceState,
										&mirrorDataSynchronizationState,
										&mirrorBufpoolMarkedForScanIncrementalResync,
										&mirrorBufpoolResyncChangedPageCount,
										&mirrorBufpoolResyncCkptLoc,
										&mirrorBufpoolResyncCkptBlockNum,
										&mirrorAppendOnlyLossEof,
										&mirrorAppendOnlyNewEof,
										&relBufpoolKind,
										&parentXid,
										&serialNum,
										&previousFreeTid);

		if (persistentState == PersistentFileSysState_Free)
			continue;

		PersistentFileSysObjName_SetRelationFile(
											&fsObjName,
											&relFileNode,
											segmentFileNum);

		if (relFileNode.spcNode != GLOBALTABLESPACE_OID)
			continue;

		if (relationStorageManager != PersistentFileSysRelStorageMgr_BufferPool)
			elog(ERROR, "Only expecting global tables to be Buffer Pool managed");

		InsertGpRelationNodeTuple(
						gp_relation_node,
						relFileNode.relNode, 	// pg_class OID
						/* relationName */ NULL,	// Optional.
						relFileNode.relNode,	// pg_class relfilenode
						/* segmentFileNum */ 0,
						/* updateIndex */ false,
						&persistentTid,
						persistentSerialNum);

		(*count)++;
	}

	PersistentStore_EndScan(&storeScan);
}

static void PersistentBuild_PopulateGpRelationNode(
	DatabaseInfo 	*info,

	Oid				defaultTablespace,

	MirroredObjectExistenceState mirrorExistenceState,

	MirroredRelDataSynchronizationState relDataSynchronizationState,

	int64			*count)
{
	Relation gp_relation_node;
	int r;
	RelFileNode indexRelFileNode;
	bool indexFound;
	Relation gp_relation_node_index;
	struct IndexInfo *indexInfo;

	if (Debug_persistent_print)
		elog(Persistent_DebugPrintLevel(), 
			 "PersistentBuild_PopulateGpRelationNode: Enter for dbOid %u",
			 info->database);

	MemSet(&indexRelFileNode, 0, sizeof(RelFileNode));
	indexFound = false;
	
	gp_relation_node = 
			DirectOpen_GpRelationNodeOpen(
							defaultTablespace, 
							info->database);

	for (r = 0; r < info->dbInfoRelArrayCount; r++)
	{
		DbInfoRel *dbInfoRel = &info->dbInfoRelArray[r];
		
		RelFileNode relFileNode;

		PersistentFileSysRelStorageMgr relStorageMgr;

		ItemPointerData persistentTid;
		int64 persistentSerialNum;

		if (dbInfoRel->reltablespace == GLOBALTABLESPACE_OID &&
			info->database != TemplateDbOid)
			continue;

		relFileNode.spcNode = dbInfoRel->reltablespace;
		relFileNode.dbNode = 
				(dbInfoRel->reltablespace == GLOBALTABLESPACE_OID ?
														0 : info->database);
		relFileNode.relNode = dbInfoRel->relfilenodeOid;

		if (dbInfoRel->relationOid == GpRelationNodeOidIndexId)
		{
			indexRelFileNode = relFileNode;
			indexFound = true;
		}

		relStorageMgr = (
				 (dbInfoRel->relstorage == RELSTORAGE_AOROWS ||
				  dbInfoRel->relstorage == RELSTORAGE_AOCOLS	) ?
								PersistentFileSysRelStorageMgr_AppendOnly :
								PersistentFileSysRelStorageMgr_BufferPool);

		/*
		 * The gp_relation_node mapping table is empty, so use the physical files as
		 * the guide.
		 */
		if (relStorageMgr == PersistentFileSysRelStorageMgr_BufferPool)
		{
			PersistentFileSysRelStorageMgr localRelStorageMgr;
			PersistentFileSysRelBufpoolKind relBufpoolKind;
			
			GpPersistentRelationNode_GetRelationInfo(
												dbInfoRel->relkind,
												dbInfoRel->relstorage,
												dbInfoRel->relam,
												&localRelStorageMgr,
												&relBufpoolKind);
			Assert(localRelStorageMgr == PersistentFileSysRelStorageMgr_BufferPool);

			/*
			 * Heap tables only ever add a single segment_file_num=0 entry to
			 * gp_persistent_relation regardless of how many segment files there
			 * really are.
			 */
			PersistentRelation_AddCreated(
										&relFileNode,
										/* segmentFileNum */ 0,
										relStorageMgr,
										relBufpoolKind,
										mirrorExistenceState,
										relDataSynchronizationState,
										/* mirrorAppendOnlyLossEof */ 0,
										/* mirrorAppendOnlyNewEof */ 0,
										dbInfoRel->relname,
										&persistentTid,
										&persistentSerialNum,
										/* flushToXLog */ false);
		
			InsertGpRelationNodeTuple(
							gp_relation_node,
							dbInfoRel->relationOid,	// pg_class OID
							dbInfoRel->relname,
							relFileNode.relNode,	// pg_class relfilenode
							/* segmentFileNum */ 0,
							/* updateIndex */ false,
							&persistentTid,
							persistentSerialNum);
			
		}
		else
		{
			int a;
			int p;

			/*
			 * Append-Only.
			 */
			/*
			 * Merge physical file existence and ao[cs]seg catalog logical EOFs .
			 */
			a = 0;
			for (p = 0; p < dbInfoRel->physicalSegmentFilesCount; p++)
			{
				int physicalSegmentFileNum = dbInfoRel->physicalSegmentFiles[p].segmentFileNum;

				bool	haveCatalogInfo;
				int64	logicalEof;

				/* 
				 * There is mostly a 1:1 matching of physical files and logical
				 * files and we just have to match them up correctly.  However
				 * there are several cases where this can diverge that we have
				 * to be able to handle.
				 *
				 * 1) Segment file 0 always exists as a physical file, but is
				 * only cataloged when it actually contains data - this only
				 * occurs for ao when data is inserted in utility mode.
				 *
				 * 2) Files created in aborted transactions where an initial
				 * frozen tuple never made it to disk may have a physical file
				 * with no logical file.  
				 *     XXX - These are leaked files that should probably be 
				 *     cleaned up at some point.
				 *
				 * 3) It is possible to have files that logically exist with a
				 * logical EOF of 0 but not exist in the filesystem.  
				 *     XXX - how does this happen, is it really safe?
				 */

				logicalEof		= 0;
				haveCatalogInfo = false;

				/* If we exhaust the loop then we are in case 2 */
				while (a < dbInfoRel->appendOnlyCatalogSegmentInfoCount)
				{
					DbInfoAppendOnlyCatalogSegmentInfo *logicalSegInfo = \
						&dbInfoRel->appendOnlyCatalogSegmentInfo[a];

					/* Normal Case: both exist */
					if (logicalSegInfo->segmentFileNum == physicalSegmentFileNum)
					{
						logicalEof		= logicalSegInfo->logicalEof;
						haveCatalogInfo = true;
						a++;
						break;  /* found */
					}
					
					/* case 0 or case 2 */
					else if (logicalSegInfo->segmentFileNum > physicalSegmentFileNum)
					{
						logicalEof		= 0;
						haveCatalogInfo = false;
						break;  /* not found */
					}

					/* case 3 - skip over logical segments w/o physical files */
					else if (logicalSegInfo->logicalEof == 0)
					{
						a++;
						continue;  /* keep looking */
					}

					/* otherwise it is an error */
					else
					{
						elog(ERROR, "logical EOF greater than zero (" INT64_FORMAT ") for segment file #%d in relation '%s' but physical file is missing",
							 logicalSegInfo->logicalEof,
							 logicalSegInfo->segmentFileNum,
							 dbInfoRel->relname);
					}

					/* unreachable */
					Assert(false);
				}

				/* 
				 * case 2) Ignore segment file left over from pre-Release 4.0 aborted
				 * transaction whose initial frozen ao[cs]seg tuple never made it to
				 * disk.  This will be a file that can result in an upgrade complaint...
				 */
				if (physicalSegmentFileNum > 0 && !haveCatalogInfo)
					continue;
				
				PersistentRelation_AddCreated(
											&relFileNode,
											physicalSegmentFileNum,
											relStorageMgr,
											PersistentFileSysRelBufpoolKind_None,
											mirrorExistenceState,
											relDataSynchronizationState,
											/* mirrorAppendOnlyLossEof */ logicalEof,
											/* mirrorAppendOnlyNewEof */ logicalEof,
											dbInfoRel->relname,
											&persistentTid,
											&persistentSerialNum,
											/* flushToXLog */ false);
				
				InsertGpRelationNodeTuple(
								gp_relation_node,
								dbInfoRel->relationOid, // pg_class OID
								dbInfoRel->relname,
								relFileNode.relNode,	// pg_class relfilenode
								physicalSegmentFileNum,
								/* updateIndex */ false,
								&persistentTid,
								persistentSerialNum);
			}
		}
		(*count)++;
	}

	if (info->database != TemplateDbOid)
	{
		PersistentBuild_ScanGpPersistentRelationNodeForGlobal(
														gp_relation_node,
														count);
	}

	/*
	 * Build the index for gp_relation_node.  
	 *
	 * The problem is the session we are using is associated with one particular database
	 * of the cluster, but we need to iterate through all the databases.  So, unfortunately, 
	 * the solution has been to use the "Direct Open" stuff.
	 *
	 * We do this because MyDatabaseId, the default tablespace of the session should not be
	 * changed.  The various caches and many other implicit things assume the object is for
	 * MyDatabaseId and the default tablespace. For example, we cannot use 
	 * CatalogUpdateIndexes called in InsertGpRelationNodeTuple because it will not do
	 * the right thing.
	 *
	 * Also, if they re-indexed gp_relation_node, it will have a different relfilenode and so we 
	 * must have found it (above) and open it with dynamically.
	 */
	Assert(indexFound);
	
	PersistentBuild_NonTransactionTruncate(
									&indexRelFileNode);
	
	gp_relation_node_index = 
			DirectOpen_GpRelationNodeIndexOpenDynamic(
										GpRelationNodeOidIndexId,
										indexRelFileNode.spcNode, 
										indexRelFileNode.dbNode,
										indexRelFileNode.relNode);

	indexInfo = makeNode(IndexInfo);
	
	indexInfo->ii_NumIndexAttrs = Natts_gp_relation_node_index;
	indexInfo->ii_KeyAttrNumbers[0] = 1;
	indexInfo->ii_KeyAttrNumbers[1] = 2;
	indexInfo->ii_Unique = true;

	if (Debug_persistent_print)
		elog(Persistent_DebugPrintLevel(), 
			 "PersistentBuild_PopulateGpRelationNode: building gp_relation_node_index %u/%u/%u for gp_relation_node %u/%u/%u",
			 gp_relation_node_index->rd_node.spcNode,
			 gp_relation_node_index->rd_node.dbNode,
			 gp_relation_node_index->rd_node.relNode,
			 gp_relation_node->rd_node.spcNode,
			 gp_relation_node->rd_node.dbNode,
			 gp_relation_node->rd_node.relNode);

	index_build(
			gp_relation_node,
			gp_relation_node_index,
			indexInfo,
			false);

	DirectOpen_GpRelationNodeIndexClose(gp_relation_node_index);

	DirectOpen_GpRelationNodeClose(gp_relation_node);


	if (Debug_persistent_print)
		elog(Persistent_DebugPrintLevel(), 
			 "PersistentBuild_PopulateGpRelationNode: Exit for dbOid %u",
			 info->database);

}

static int64
PersistentBuild_BuildDb(
	Oid 		dbOid,

	bool 		mirrored)
{
	MirroredObjectExistenceState		 mirrorExistenceState;
	MirroredRelDataSynchronizationState	 relDataSynchronizationState;

	int64				 count = 0;
	Relation			 gp_global_sequence;
	Relation			 pg_database;
	HeapTuple			 tuple;
	Form_pg_database	 form_pg_database;
	DatabaseInfo		*info;
	Oid					 defaultTablespace;
	int					 t;
	cqContext			 cqc;
	cqContext			 dbcqc;
	cqContext			*pcqCtx;

	/*
	 * Turn this on so we don't try to fetch persistence information from
	 * gp_relation_node for gp_relation_node and its index until we've done the
	 * assignment with PersistentRelation_AddCreated.
	 */
	gp_before_persistence_work = true;

	if (mirrored)
	{
		mirrorExistenceState = MirroredObjectExistenceState_MirrorCreated;
		relDataSynchronizationState = 
			MirroredRelDataSynchronizationState_DataSynchronized;
	}
	else
	{
		mirrorExistenceState = MirroredObjectExistenceState_NotMirrored;
		relDataSynchronizationState = MirroredRelDataSynchronizationState_None;
	}

	/* 
	 * If the gp_global_sequence table hasn't been populated yet then we need 
	 * to populate it before we can procede with building the rest of the 
	 * persistent tables. 
	 */
	gp_global_sequence = heap_open(GpGlobalSequenceRelationId, RowExclusiveLock);


	pcqCtx = caql_beginscan(
			caql_addrel(cqclr(&cqc), gp_global_sequence),
			cql("SELECT * FROM gp_global_sequence "
				" FOR UPDATE ",
				NULL));

	tuple = caql_getnext(pcqCtx);

	if (!HeapTupleIsValid(tuple))
	{
		Datum			values[Natts_gp_global_sequence];
		bool			nulls[Natts_gp_global_sequence];

		/* Insert N frozen tuples of value 0 */
		MemSet(nulls, false, sizeof(nulls));
		values[Anum_gp_global_sequence_sequence_num-1] = Int64GetDatum(0);
		tuple = caql_form_tuple(pcqCtx, values, nulls);

		if (!HeapTupleIsValid(tuple))
			elog(ERROR, "failed to build global sequence tuple");
		
		for (t = 0; t < GpGlobalSequence_MaxSequenceTid; t++)
			frozen_heap_insert(gp_global_sequence, tuple);
	}
	caql_endscan(pcqCtx);
	heap_close(gp_global_sequence, RowExclusiveLock);

	/* Lookup the information for the current database */
	pg_database = heap_open(DatabaseRelationId, RowExclusiveLock);

	/* Fetch a copy of the tuple to scribble on */
	tuple = caql_getfirst(			
			caql_addrel(cqclr(&dbcqc), pg_database),
			cql("SELECT * FROM pg_database "
				" WHERE oid = :1 "
				" FOR UPDATE ",
				ObjectIdGetDatum(dbOid)));

	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "could not find tuple for database %u", dbOid);
	form_pg_database = (Form_pg_database) GETSTRUCT(tuple);
	
	defaultTablespace = form_pg_database->dattablespace;

	if (Debug_persistent_print)
		elog(Persistent_DebugPrintLevel(), 
			 "PersistentBuild_BuildDb: dbOid %u, '%s', mirror existence state '%s', "
			 "data synchronization state '%s'",
			 dbOid,
			 NameStr(form_pg_database->datname),
			 MirroredObjectExistenceState_Name(mirrorExistenceState),
			 MirroredRelDataSynchronizationState_Name(
				 relDataSynchronizationState));

	/*
	 * Special call here to scan the persistent meta-data structures so we are open for 
	 * business and then we can add information.
	 */
	PersistentFileSysObj_BuildInitScan();

	info = DatabaseInfo_Collect(
							dbOid,
							defaultTablespace,
							/* collectGpRelationNodeInfo */ false,
							/* collectAppendOnlyCatalogSegmentInfo */ true,
							/* scanFileSystem */ true);

	for (t = 0; t < info->tablespacesCount; t++)
	{
		Oid				tablespace = info->tablespaces[t];
		DbDirNode		dbDirNode;
		ItemPointerData persistentTid;

		if (tablespace == GLOBALTABLESPACE_OID)
			continue;
	
		dbDirNode.tablespace = tablespace;
		dbDirNode.database = dbOid;

		PersistentDatabase_AddCreated(
								&dbDirNode,
								mirrorExistenceState,
								&persistentTid,
								/* flushToXLog */ false);
	}								
	
	PersistentBuild_PopulateGpRelationNode(
										info,
										defaultTablespace,
										mirrorExistenceState,
										relDataSynchronizationState,
										&count);

	heap_close(pg_database, RowExclusiveLock);

	gp_before_persistence_work = false;

#ifdef FAULT_INJECTOR
	FaultInjector_InjectFaultIfSet(
								   RebuildPTDB,
								   DDLNotSpecified,
								   "",	// databaseName
								   ""); // tableName
#endif

	/* 
	 * Since we have written XLOG records with <persistentTid,
	 * persistentSerialNum> of zeroes because of the gp_before_persistence_work
	 * GUC, lets do a checkpoint to force out all buffer pool pages so we never
	 * try to redo those XLOG records in Crash Recovery.
	 */
	CreateCheckPoint(false, true);

	return count;
}

Datum
gp_persistent_build_db(PG_FUNCTION_ARGS)
{
	bool mirrored = PG_GETARG_BOOL(0);

	PersistentBuild_BuildDb(
						MyDatabaseId,
						mirrored);

	PG_RETURN_INT32(1);
}


Datum
gp_persistent_build_all(PG_FUNCTION_ARGS)
{
	bool mirrored = PG_GETARG_BOOL(0);

	Relation pg_tablespace;
	HeapTuple tuple;
	cqContext	 cqc;
	cqContext	*pcqCtx;
	
	Datum	*d;
	bool	*null;
	
	// UNDONE: Verify we are in some sort of single-user mode.

	/*
	 * Re-build tablespaces.
	 */
	d = (Datum *) palloc(sizeof(Datum) * Natts_pg_tablespace);
	null = (bool *) palloc(sizeof(bool) * Natts_pg_tablespace);	
	
	pg_tablespace = heap_open(
							  TableSpaceRelationId,
							  AccessShareLock);
	
	pcqCtx = caql_beginscan(
			caql_syscache(
					caql_indexOK(caql_addrel(cqclr(&cqc), pg_tablespace), 
								 false),
					false),
			cql("SELECT * FROM pg_tablespace ",
				NULL));

	while (HeapTupleIsValid(tuple = caql_getnext(pcqCtx)))
	{
		Oid tablespaceOid;
		
		if (!HeapTupleIsValid(tuple))
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("tablespace tuple is invalid")));
		
		tablespaceOid = HeapTupleGetOid(tuple);
		
		heap_deform_tuple(tuple, RelationGetDescr(pg_tablespace), d, null);
						
		if (tablespaceOid == DEFAULTTABLESPACE_OID ||
			tablespaceOid == GLOBALTABLESPACE_OID)
		{
			if (Debug_persistent_print)
				elog(Persistent_DebugPrintLevel(), 
					 "gp_persistent_build_all: skip pg_default and pg_global tablespaceOid %u",
				tablespaceOid);
			continue;
		}
		
		if (Debug_persistent_print)
			elog(Persistent_DebugPrintLevel(), 
				 "gp_persistent_build_all: tablespaceOid %u filespaceOid %u",
				 tablespaceOid, DatumGetInt32(d[Anum_pg_tablespace_spcfsoid - 1]));
						
		PersistentTablespace_AddCreated(
										DatumGetInt32(d[Anum_pg_tablespace_spcfsoid - 1]),
										tablespaceOid,
										mirrored ?
													MirroredObjectExistenceState_MirrorCreated :
													MirroredObjectExistenceState_NotMirrored,
										/* flushToXLog */ false);
	}
	
	caql_endscan(pcqCtx);
	
	heap_close(pg_tablespace, AccessShareLock);
	
	pfree(d);
	pfree(null);
	
	/*
	 * Re-build databases.
	 * Do template1 first since it will also populate the shared-object persistent objects.
	 */	
	PersistentBuild_BuildDb(
						TemplateDbOid,
						mirrored);

	if (Debug_persistent_print)
		elog(Persistent_DebugPrintLevel(), 
			 "gp_persistent_build_all: template1 complete");

	/*
	 * Now, the remaining databases.
	 */

	pcqCtx = caql_beginscan(
			caql_syscache(
					caql_indexOK(cqclr(&cqc), false),
					false),
			cql("SELECT * FROM pg_database ",
				NULL));

	while (HeapTupleIsValid(tuple = caql_getnext(pcqCtx)))
	{
		Oid dbOid;
		
		dbOid = HeapTupleGetOid(tuple);
		if (dbOid == TemplateDbOid)
		{
			if (Debug_persistent_print)
				elog(Persistent_DebugPrintLevel(), 
					 "gp_persistent_build_all: skip template1");
			continue;
		}

		if (Debug_persistent_print)
			elog(Persistent_DebugPrintLevel(), 
				 "gp_persistent_build_all: dbOid %u",
				 dbOid);
		
		PersistentBuild_BuildDb(
							dbOid,
							mirrored);
	}

	caql_endscan(pcqCtx);

	PersistentStore_FlushXLog();

	PG_RETURN_INT32(1);
}

static void
PersistentBuild_FindGpRelationNodeIndex(
	Oid				database,

	Oid				defaultTablespace,

	RelFileNode		*relFileNode)
{
	Relation	pg_class_rel;
	HeapTuple	tuple;
	cqContext	cqc;
	cqContext  *pcqCtx;
	bool found;

	/*
	 * Iterate through all the relations of the database and find gp_relation_node_index.
	 */
	pg_class_rel = 
			DirectOpen_PgClassOpen(
							defaultTablespace, 
							database);

	found = false;
	pcqCtx = caql_beginscan(
			caql_syscache(
					caql_indexOK(caql_addrel(cqclr(&cqc), pg_class_rel), 
								 false),
					false),
			cql("SELECT * FROM pg_class ",
				NULL));

	while (HeapTupleIsValid(tuple = caql_getnext(pcqCtx)))
	{
		Oid 			relationOid;

		Form_pg_class	form_pg_class;

		Oid 			reltablespace;

		relationOid = HeapTupleGetOid(tuple);
		if (relationOid != GpRelationNodeOidIndexId)
		{
			continue;
		}

		form_pg_class = (Form_pg_class) GETSTRUCT(tuple);

		reltablespace = form_pg_class->reltablespace;

		if (!OidIsValid(reltablespace))
		{
			reltablespace = defaultTablespace;
		}

		relFileNode->spcNode = reltablespace;
		relFileNode->dbNode = database;
		relFileNode->relNode= form_pg_class->relfilenode;

		found = true;
		break;
	}
	caql_endscan(pcqCtx);

	DirectOpen_PgClassClose(pg_class_rel);

	if (!found)
	{
		elog(ERROR, "pg_class entry for gp_relation_node_index not found");
	}

}

static int64
PersistentBuild_TruncateAllGpRelationNode(void)
{
	HeapTuple	 tuple;
	cqContext	 cqc;
	cqContext	*pcqCtx;
	int64		 count;

	/*
	 * Truncate gp_relation_node and its index in each database.
	 */
	count = 0;
	pcqCtx = caql_beginscan(
			caql_syscache(
					caql_indexOK(cqclr(&cqc), false),
					false),
			cql("SELECT * FROM pg_database ",
				NULL));

	while (HeapTupleIsValid(tuple = caql_getnext(pcqCtx)))
	{
		Form_pg_database form_pg_database =
						(Form_pg_database)GETSTRUCT(tuple);

		Oid dbOid;
		Oid dattablespace;
		RelFileNode relFileNode;
		SMgrRelation smgrRelation;
		Page btree_metapage;
		
		dbOid = HeapTupleGetOid(tuple);
		dattablespace = form_pg_database->dattablespace;

		if (Debug_persistent_print)
			elog(Persistent_DebugPrintLevel(), 
				 "PersistentBuild_TruncateAllGpRelationNode: dbOid %u, '%s'",
				 dbOid,
				 NameStr(form_pg_database->datname));

		if (Debug_persistent_print)
			elog(Persistent_DebugPrintLevel(), 
				 "Truncating gp_relation_node %u/%u/%u in database oid %u ('%s')",
				 relFileNode.spcNode,
				 relFileNode.dbNode,
				 relFileNode.relNode,
				 dbOid,
				 NameStr(form_pg_database->datname));

		relFileNode.spcNode = dattablespace;
		relFileNode.dbNode = dbOid;
		relFileNode.relNode = GpRelationNodeRelationId;

		/*
		 * Truncate WITHOUT generating an XLOG record (i.e. pretend it is a temp relation).
		 */
		PersistentBuild_NonTransactionTruncate(&relFileNode);
		count++;

		/*
		 * And, the index.  Unfortunately, the relfilenode OID can change due to a
		 * REINDEX {TABLE|INDEX} command.
		 */
		PersistentBuild_FindGpRelationNodeIndex(
											dbOid,
											dattablespace,
											&relFileNode);

		if (Debug_persistent_print)
			elog(Persistent_DebugPrintLevel(), 
				 "Truncating gp_relation_node_index %u/%u/%u in database oid %u ('%s').  relfilenode different %s, tablespace different %s",
				 relFileNode.spcNode,
				 relFileNode.dbNode,
				 relFileNode.relNode,
				 dbOid,
				 NameStr(form_pg_database->datname),
				 ((relFileNode.relNode != GpRelationNodeOidIndexId) ? "true" : "false"),
				 ((relFileNode.spcNode != dattablespace) ? "true" : "false"));

		PersistentBuild_NonTransactionTruncate(&relFileNode);

		// The BTree needs an empty meta-data block.
		smgrRelation = smgropen(relFileNode);

		btree_metapage = (Page)palloc(BLCKSZ);
		_bt_initmetapage(btree_metapage, P_NONE, 0);
		smgrwrite(
			smgrRelation, 
			/* blockNum */ 0, 
			(char*)btree_metapage,
			/* isTemp */ false);
		smgrimmedsync(smgrRelation);
		pfree(btree_metapage);

		smgrclose(smgrRelation);

		count++;
	}

	caql_endscan(pcqCtx);

	return count;
}

Datum
gp_persistent_reset_all(PG_FUNCTION_ARGS)
{
	RelFileNode relFileNode;

	// UNDONE: Verify we are in some sort of single-user mode.

	/*
	 * Truncate all database's gp_relation_node and their indices.
	 */
	PersistentBuild_TruncateAllGpRelationNode();

	/*
	 * Truncate the 4 persistent shared tables.
	 * 'gp_persistent_filespace_node' persistent table is not dropped 
	 * since it cannot be re-built. 'pg_filespace' table does not exist
	 * on segments by design.
	 */
	relFileNode.spcNode = GLOBALTABLESPACE_OID;
	relFileNode.dbNode = 0;
	
	relFileNode.relNode = GpPersistentRelationNodeRelationId;
	PersistentBuild_NonTransactionTruncate(&relFileNode);
	
	relFileNode.relNode = GpPersistentDatabaseNodeRelationId;
	PersistentBuild_NonTransactionTruncate(&relFileNode);
	
	relFileNode.relNode = GpPersistentTablespaceNodeRelationId;
	PersistentBuild_NonTransactionTruncate(&relFileNode);
	
	relFileNode.relNode = GpPersistentRelationNodeRelationId;
	PersistentBuild_NonTransactionTruncate(&relFileNode);

	/*
	 * Reset the persistent shared-memory free list heads and all shared-memory hash-tables.
	 */
	PersistentFileSysObj_Reset();

	PG_RETURN_INT32(1);
}

static void
PersistentBuild_SetRelationBufpoolKind(
	Oid			defaultTablespace,

	Oid 		database)
{
	DatabaseInfo *info;
	
	int	r;

	info = DatabaseInfo_Collect(
							database,
							defaultTablespace,
							/* collectGpRelationNodeInfo */ true,
							/* collectAppendOnlyCatalogSegmentInfo */ false,
							/* scanFileSystem */ false);

	for (r = 0; r < info->dbInfoRelArrayCount; r++)
	{
		DbInfoRel *dbInfoRel = &info->dbInfoRelArray[r];
		
		RelFileNode relFileNode;

		PersistentFileSysRelStorageMgr localRelStorageMgr;
		PersistentFileSysRelBufpoolKind relBufpoolKind;

		int g;

		if (dbInfoRel->reltablespace == GLOBALTABLESPACE_OID &&
			info->database != TemplateDbOid)
			continue;

		relFileNode.spcNode = dbInfoRel->reltablespace;
		relFileNode.dbNode = 
				(dbInfoRel->reltablespace == GLOBALTABLESPACE_OID ?
														0 : info->database);
		relFileNode.relNode = dbInfoRel->relfilenodeOid;

		GpPersistentRelationNode_GetRelationInfo(
											dbInfoRel->relkind,
											dbInfoRel->relstorage,
											dbInfoRel->relam,
											&localRelStorageMgr,
											&relBufpoolKind);

		/*
		 * Update the gp_persistent_relation_node entry for each gp_relation_node entry.
		 */
		for (g = 0; g < dbInfoRel->gpRelationNodesCount; g++)
		{
			DbInfoGpRelationNode *gpRelationNode = &dbInfoRel->gpRelationNodes[g];

			PersistentFileSysObj_UpdateRelationBufpoolKind(
												&relFileNode,
												gpRelationNode->segmentFileNum,
												&gpRelationNode->persistentTid,
												gpRelationNode->persistentSerialNum,
												relBufpoolKind);
		}

	}
}

Datum
gp_persistent_set_relation_bufpool_kind_all(PG_FUNCTION_ARGS)
{
	Relation pg_database;
	HeapScanDesc scan;
	HeapTuple tuple;

        if (!gp_upgrade_mode)
                ereport(ERROR,
                        (errcode(ERRCODE_GP_FEATURE_NOT_SUPPORTED),
                         errmsg("function is not supported")));

	// UNDONE: Verify we are in some sort of single-user mode.

	/*
	 * Special call here to scan the persistent meta-data structures so we are open for 
	 * business and then we can add information.
	 */
	PersistentFileSysObj_BuildInitScan();

	/* CaQL UNDONE: no test coverage */
	pg_database = heap_open(
						DatabaseRelationId,
						AccessShareLock);

	scan = heap_beginscan(pg_database, SnapshotNow, 0, NULL);
	while ((tuple = heap_getnext(scan, ForwardScanDirection)) != NULL)
	{
		Form_pg_database form_pg_database =
						(Form_pg_database)GETSTRUCT(tuple);

		Oid dbOid;
		Oid dattablespace;
		
		dbOid = HeapTupleGetOid(tuple);
		dattablespace = form_pg_database->dattablespace;

		if (Debug_persistent_print)
			elog(Persistent_DebugPrintLevel(), 
				 "gp_persistent_set_relation_bufpool_kind_all: dbOid %u",
				 dbOid);
		
		PersistentBuild_SetRelationBufpoolKind(
											dattablespace,
											dbOid);
	}

	heap_endscan(scan);

	heap_close(pg_database, AccessShareLock);

	PG_RETURN_INT32(1);
}

Datum
gp_persistent_repair_delete(PG_FUNCTION_ARGS)
{
	int							 fsObjType;
	ItemPointerData				 persistentTid;

	fsObjType = PG_GETARG_INT32(0);
	persistentTid = PG_GETARG_TID(1);

	if (fsObjType < PersistentFsObjType_First || 
		fsObjType > PersistentFsObjType_Last)
		elog(ERROR, 
			 "Persistent object type must be in the range 1..4 "
		     "(Relation, Database Dir, Tablespace Dir, Filespace Dir)");

	PersistentFileSysObj_RepairDelete(
								fsObjType,
								&persistentTid);
	PG_RETURN_INT32(0);
}
