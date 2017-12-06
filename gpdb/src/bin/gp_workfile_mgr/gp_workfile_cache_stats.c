/*
 * Copyright (c) 2012 EMC Corporation All Rights Reserved
 *
 * This software is protected, without limitation, by copyright law
 * and international treaties. Use of this software and the intellectual
 * property contained therein is expressly limited to the terms and
 * conditions of the License Agreement under which it is provided by
 * or on behalf of EMC.
 *
 * ---------------------------------------------------------------------
 *
 * The dynamically linked library created from this source can be reference by
 * creating a function in psql that references it. For example,
 *
 * CREATE OR REPLACE FUNCTION gp_shared_cache_stats()
 *   RETURNS RECORD
 *   AS '$libdir/gp_shared_cache.so', 'gp_shared_cache_stats'
 *   LANGUAGE C;
 *
 */

#include "postgres.h"
#include "funcapi.h"
#include "cdb/cdbvars.h"
#include "utils/builtins.h"
#include "utils/workfile_mgr.h"
#include "utils/sharedcache.h"
#include "miscadmin.h"

/* The number of columns as defined in gp_workfile_mgr_cache_stats view */
#define NUM_CACHE_STATS_ELEM 23

/* The number of columns as defined in gp_workfile_mgr_cache_entries view */
#define NUM_CACHE_ENTRIES_ELEM 13

/* The number of columns as defined in gp_workfile_mgr_diskspace view */
#define NUM_USED_DISKSPACE_ELEM 2

static char *gp_workfile_operator_name(NodeTag node_type);

Datum gp_workfile_mgr_cache_stats(PG_FUNCTION_ARGS);
Datum gp_workfile_mgr_cache_entries(PG_FUNCTION_ARGS);
Datum gp_workfile_mgr_used_diskspace(PG_FUNCTION_ARGS);

/* Helper functions */
static CacheEntry *next_entry_to_list(Cache *cache, int32 *crtIndex);
static bool should_list_entry(CacheEntry *entry);

PG_FUNCTION_INFO_V1(gp_workfile_mgr_cache_stats);

/*
 * Function returning workfile cache statistics on one segment
 */
Datum
gp_workfile_mgr_cache_stats(PG_FUNCTION_ARGS)
{
	/* Build a tuple descriptor for our result type */
	TupleDesc tupledesc;
	if (get_call_result_type(fcinfo, NULL, &tupledesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");

	/* Locate the appropriate Cache header in shared memory and get stats */
	Cache *cache = workfile_mgr_get_cache();
	CacheHdr *cacheHdr = cache->cacheHdr;
	Cache_Stats *cacheStats = &cacheHdr->cacheStats;

	Datum		values[NUM_CACHE_STATS_ELEM];
	bool		nulls[NUM_CACHE_STATS_ELEM];
	MemSet(nulls, 0, sizeof(nulls));

	/*
	 * Build a tuple descriptor for our result type
	 * The number and type of attributes have to match the definition of the
	 * view gp_workfile_mgr_cache_stats
	 */
	values[0] = CStringGetTextDatum(cache->cacheName);
	values[1] = Int32GetDatum(Gp_segment);
	values[2] = UInt32GetDatum(cacheStats->noLookups);
	values[3] = UInt32GetDatum(cacheStats->noInserts);
	values[4] = UInt32GetDatum(cacheStats->noEvicts);
	values[5] = UInt32GetDatum(cacheStats->noCacheHits);
	values[6] = UInt32GetDatum(cacheStats->noCompares);
	values[7] = UInt32GetDatum(cacheStats->noPinnedEntries);
	values[8] = UInt32GetDatum(cacheStats->noCachedEntries);
	values[9] = UInt32GetDatum(cacheStats->noDeletedEntries);
	values[10] = UInt32GetDatum(cacheStats->noAcquiredEntries);
	values[11] = UInt32GetDatum(cacheStats->noFreeEntries);
	values[12] = Int64GetDatum(cacheStats->totalEntrySize);
	values[13] = UInt32GetDatum(cacheStats->noEntriesScanned);
	values[14] = UInt32GetDatum(cacheStats->maxEntriesScanned);
	values[15] = UInt32GetDatum(cacheStats->noWraparound);
	values[16] = UInt32GetDatum(cacheStats->maxWraparound);

	values[17] = UInt64GetDatum(INSTR_TIME_GET_MICROSEC(cacheStats->timeInserts));
	values[18] = UInt64GetDatum(INSTR_TIME_GET_MICROSEC(cacheStats->timeLookups));
	values[19] = UInt64GetDatum(INSTR_TIME_GET_MICROSEC(cacheStats->timeEvictions));
	values[20] = UInt64GetDatum(INSTR_TIME_GET_MICROSEC(cacheStats->maxTimeInsert));
	values[21] = UInt64GetDatum(INSTR_TIME_GET_MICROSEC(cacheStats->maxTimeLookup));
	values[22] = UInt64GetDatum(INSTR_TIME_GET_MICROSEC(cacheStats->maxTimeEvict));

	Assert(NUM_CACHE_STATS_ELEM == 23);

	HeapTuple tuple = heap_form_tuple(tupledesc, values, nulls);

	Datum result = HeapTupleGetDatum(tuple);

	PG_RETURN_DATUM(result);
}


PG_FUNCTION_INFO_V1(gp_workfile_mgr_cache_entries);

/*
 * Function returning all workfile cache entries for one segment
 */
Datum
gp_workfile_mgr_cache_entries(PG_FUNCTION_ARGS)
{

	FuncCallContext *funcctx;
	int32 *crtIndexPtr;

	if (SRF_IS_FIRSTCALL())
	{
		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/* Switch to memory context appropriate for multiple function calls */
		MemoryContext oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/*
		 * Build a tuple descriptor for our result type
		 * The number and type of attributes have to match the definition of the
		 * view gp_workfile_mgr_cache_entries
		 */
		TupleDesc tupdesc = CreateTemplateTupleDesc(NUM_CACHE_ENTRIES_ELEM, false);

		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "segid",
				INT4OID, -1 /* typmod */, 0 /* attdim */);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "path",
				TEXTOID, -1 /* typmod */, 0 /* attdim */);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "hash",
				INT4OID, -1 /* typmod */, 0 /* attdim */);
		TupleDescInitEntry(tupdesc, (AttrNumber) 4, "size",
				INT8OID, -1 /* typmod */, 0 /* attdim */);
		TupleDescInitEntry(tupdesc, (AttrNumber) 5, "utility",
				INT4OID, -1 /* typmod */, 0 /* attdim */);
		TupleDescInitEntry(tupdesc, (AttrNumber) 6, "state",
				INT4OID, -1 /* typmod */, 0 /* attdim */);
		TupleDescInitEntry(tupdesc, (AttrNumber) 7, "workmem",
				INT4OID, -1 /* typmod */, 0 /* attdim */);
		TupleDescInitEntry(tupdesc, (AttrNumber) 8, "optype",
				TEXTOID, -1 /* typmod */, 0 /* attdim */);
		TupleDescInitEntry(tupdesc, (AttrNumber) 9, "slice",
				INT4OID, -1 /* typmod */, 0 /* attdim */);
		TupleDescInitEntry(tupdesc, (AttrNumber) 10, "sessionid",
				INT4OID, -1 /* typmod */, 0 /* attdim */);
		TupleDescInitEntry(tupdesc, (AttrNumber) 11, "commandid",
				INT4OID, -1 /* typmod */, 0 /* attdim */);
		TupleDescInitEntry(tupdesc, (AttrNumber) 12, "query_start",
				TIMESTAMPTZOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 13, "numfiles",
				INT4OID, -1 /* typmod */, 0 /* attdim */);

		Assert(NUM_CACHE_ENTRIES_ELEM == 13);

		funcctx->tuple_desc = BlessTupleDesc(tupdesc);

		crtIndexPtr = (int32 *) palloc(sizeof(*crtIndexPtr));
		*crtIndexPtr = 0;
		funcctx->user_fctx = crtIndexPtr;
		MemoryContextSwitchTo(oldcontext);
	}

	Cache *cache = workfile_mgr_get_cache();
	funcctx = SRF_PERCALL_SETUP();
	crtIndexPtr = (int32 *) funcctx->user_fctx;

	while (true)
	{

		CacheEntry *crtEntry = next_entry_to_list(cache, crtIndexPtr);

		if (!crtEntry)
		{
			/* Reached the end of the entry array, we're done */
			SRF_RETURN_DONE(funcctx);
		}

		Datum		values[NUM_CACHE_ENTRIES_ELEM];
		bool		nulls[NUM_CACHE_ENTRIES_ELEM];
		MemSet(nulls, 0, sizeof(nulls));

		workfile_set *work_set = CACHE_ENTRY_PAYLOAD(crtEntry);
		char work_set_path[MAXPGPATH] = "";
		char *work_set_operator_name = NULL;


		/*
		 * Lock entry in order to read its payload
		 * Don't call any functions that can get interrupted or
		 * that palloc memory while holding this lock.
		 */
		Cache_LockEntry(cache, crtEntry);

		if (!should_list_entry(crtEntry) || !work_set->on_disk)
		{
			Cache_UnlockEntry(cache, crtEntry);
			continue;
		}

		values[0] = Int32GetDatum(Gp_segment);
		if (work_set->on_disk)
		{
			/* Only physical sets have a meaningful path */
			strncpy(work_set_path, work_set->path, MAXPGPATH);
		}

		values[2] = UInt32GetDatum(crtEntry->hashvalue);

		int64 work_set_size = work_set->size;
		if (crtEntry->state == CACHE_ENTRY_ACQUIRED)
		{
			/*
			 * work_set->size is not updated until the entry is cached.
			 * For in-progress queries, the up-to-date size is stored in
			 * work_set->in_progress_size.
			 */
			work_set_size = work_set->in_progress_size;
		}

		values[3] = Int64GetDatum(work_set_size);
		values[4] = UInt32GetDatum(crtEntry->utility);
		values[5] = UInt32GetDatum(crtEntry->state);
		values[6] = UInt32GetDatum(work_set->metadata.operator_work_mem);

		work_set_operator_name = gp_workfile_operator_name(work_set->node_type);
		values[8] = UInt32GetDatum(work_set->slice_id);
		values[9] = UInt32GetDatum(work_set->session_id);
		values[10] = UInt32GetDatum(work_set->command_count);
		values[11] = TimestampTzGetDatum(work_set->session_start_time);
		values[12] = UInt32GetDatum(work_set->no_files);

		/* Done reading from the payload of the entry, release lock */
		Cache_UnlockEntry(cache, crtEntry);


		/*
		 * Fill in the rest of the entries of the tuple with data copied
		 * from the descriptor.
		 * CStringGetTextDatum calls palloc so we cannot do this while
		 * holding the lock above.
		 */
		values[1] = CStringGetTextDatum(work_set_path);
		values[7] = CStringGetTextDatum(work_set_operator_name);


		HeapTuple tuple = heap_form_tuple(funcctx->tuple_desc, values, nulls);
		Datum result = HeapTupleGetDatum(tuple);
		SRF_RETURN_NEXT(funcctx, result);
	}
}

/*
 * Traverses the list of cache entries and looks for the next interesting entry
 * Returns an entry if found, NULL if we reached the end of the loop.
 *
 * crtIndex is a pointer to the current index in the list. It is updated to
 * the index of the current entry while traversing.
 */
static CacheEntry *
next_entry_to_list(Cache *cache, int32 *crtIndex)
{
	CacheHdr *cacheHdr = cache->cacheHdr;
	CacheEntry *crtEntry = NULL;

	for ( ;  (*crtIndex) < cacheHdr->nEntries ; (*crtIndex)++)
	{
		crtEntry = Cache_GetEntryByIndex(cacheHdr, *crtIndex);
		if (should_list_entry(crtEntry))
		{
			(*crtIndex)++;
			return crtEntry;
		}
	}

	/* Finished the list and did not find any interesting entries */
	return NULL;
}

/*
 * Determines if an entry should be included in the output based on the state.
 */
static bool
should_list_entry(CacheEntry *entry)
{
	return (entry->state == CACHE_ENTRY_ACQUIRED) || (entry->state == CACHE_ENTRY_CACHED)
			|| (entry->state == CACHE_ENTRY_DELETED);
}

PG_FUNCTION_INFO_V1(gp_workfile_mgr_used_diskspace);

/*
 * Returns the number of bytes used for workfiles on a segment
 * according to WorkfileDiskspace
 */
Datum
gp_workfile_mgr_used_diskspace(PG_FUNCTION_ARGS)
{
	/*
	 * Build a tuple descriptor for our result type
	 * The number and type of attributes have to match the definition of the
	 * view gp_workfile_mgr_diskspace
	 */
	TupleDesc tupdesc = CreateTemplateTupleDesc(NUM_USED_DISKSPACE_ELEM, false);

	TupleDescInitEntry(tupdesc, (AttrNumber) 1, "segid",
			INT4OID, -1 /* typmod */, 0 /* attdim */);
	TupleDescInitEntry(tupdesc, (AttrNumber) 2, "bytes",
			INT8OID, -1 /* typmod */, 0 /* attdim */);

	tupdesc =  BlessTupleDesc(tupdesc);

	Datum		values[NUM_USED_DISKSPACE_ELEM];
	bool		nulls[NUM_USED_DISKSPACE_ELEM];
	MemSet(nulls, 0, sizeof(nulls));

	values[0] = Int32GetDatum(Gp_segment);
	values[1] = Int64GetDatum(WorkfileSegspace_GetSize());

	HeapTuple tuple = heap_form_tuple(tupdesc, values, nulls);
	Datum result = HeapTupleGetDatum(tuple);

	PG_RETURN_DATUM(result);
}

/*
 * Converts from a NodeTag id to an operator name. Only for operators
 * supported by the workfile manager.
 */
static char *
gp_workfile_operator_name(NodeTag node_type)
{
	char *ret = NULL;
	switch (node_type)
	{
		case T_HashJoinState:
			ret = "Hash Join";
			break;
		case T_SortState:
			ret = "Sort";
			break;
		case T_AggState:
			ret = "HashAggregate";
			break;
		case T_MaterialState:
			ret = "Materialize";
			break;
		case T_Invalid:
			/* Spilling from a builtin function, we don't have a valid node type */
			ret = "BuiltinFunction";
			break;

		default:
			Assert(false && "Invalid operator type");
	}
	return ret;
}

