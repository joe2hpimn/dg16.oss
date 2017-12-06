/*
 * execHeapScan.c
 *   Support routines for scanning Heap tables.
 *
 * Copyright (c) 2012 - present, EMC/Greenplum
 */
#include "postgres.h"

#include "executor/executor.h"
#include "nodes/execnodes.h"
#include "executor/nodeTableScan.h"

static void
InitHeapScanOpaque(ScanState *scanState)
{
	SeqScanState *state = (SeqScanState *)scanState;
	Assert(state->opaque == NULL);
	state->opaque = palloc0(sizeof(SeqScanOpaqueData));
}

static void
FreeHeapScanOpaque(ScanState *scanState)
{
	SeqScanState *state = (SeqScanState *)scanState;
	Assert(state->opaque != NULL);
	pfree(state->opaque);
	state->opaque = NULL;
}


int exx_HeapScanNextBatch(ScanState* scanState, HeapTupleHeader* res_orig, int n)
{
    /* copy from HeapScanNext() below */
	Assert(IsA(scanState, TableScanState) ||
		   IsA(scanState, DynamicTableScanState));
	SeqScanState *node = (SeqScanState *)scanState;
	Assert(node->opaque != NULL);

	HeapTuple	tuple;
	HeapScanDesc scandesc;
	Index		scanrelid;
	EState	   *estate;
	ScanDirection direction;
	TupleTableSlot *slot;
    HeapTupleHeader* res = res_orig;

	Assert((node->ss.scan_state & SCAN_SCAN) != 0);
    Assert(n > 0);

	/*
	 * get information from the estate and scan state
	 */
	estate = node->ss.ps.state;
	scandesc = node->opaque->ss_currentScanDesc;
	scanrelid = ((SeqScan *) node->ss.ps.plan)->scanrelid;
	direction = estate->es_direction;
	slot = node->ss.ss_ScanTupleSlot;

	/*
	 * Check if we are evaluating PlanQual for tuple of this relation.
	 * Additional checking is not good, but no other way for now. We could
	 * introduce new nodes for this case and handle SeqScan --> NewNode
	 * switching in Init/ReScan plan...
	 */
	if (estate->es_evTuple != NULL &&
		estate->es_evTuple[scanrelid - 1] != NULL)
	{
		if (estate->es_evTupleNull[scanrelid - 1])
		{
            return 0;
		}

        res[0] = estate->es_evTuple[scanrelid - 1]->t_data;
		// ExecStoreGenericTuple(estate->es_evTuple[scanrelid - 1], slot, false);

		/*
		 * Note that unlike IndexScan, SeqScan never uses keys in
		 * heap_beginscan (and this is very bad) - so, here we do not check
		 * the keys.
		 */

		/* Flag for the next call that no more tuples */
		estate->es_evTupleNull[scanrelid - 1] = true;
		return 1;
	}

	/*
	 * get the next tuple from the access methods
	 */
	if (node->opaque->ss_heapTupleData.bot == node->opaque->ss_heapTupleData.top &&
		!node->opaque->ss_heapTupleData.seen_EOS)
	{
		node->opaque->ss_heapTupleData.last = NULL;
		node->opaque->ss_heapTupleData.bot = 0;
		node->opaque->ss_heapTupleData.top = lengthof(node->opaque->ss_heapTupleData.item);
		heap_getnextx(scandesc, direction, node->opaque->ss_heapTupleData.item,
					  &node->opaque->ss_heapTupleData.top,
					  &node->opaque->ss_heapTupleData.seen_EOS);

		if (scandesc->rs_pageatatime &&
		   IsA(scanState, TableScanState))
		{
			Gpmon_M_Incr(GpmonPktFromTableScanState((TableScanState *)scanState), GPMON_TABLESCAN_PAGE);
			CheckSendPlanStateGpmonPkt(&node->ss.ps);
		}
	}

	node->opaque->ss_heapTupleData.last = NULL;
	while (node->opaque->ss_heapTupleData.bot < node->opaque->ss_heapTupleData.top && n > 0)
	{
        tuple = &node->opaque->ss_heapTupleData.item[node->opaque->ss_heapTupleData.bot++];

        node->opaque->ss_heapTupleData.last = tuple;
        *res++ = tuple->t_data;
        n--;
	}

	
	const int cnt = res - res_orig;
#ifdef REVERSE
	/* CK: is scanning the tuples in reverse any faster? 
	 * I don't see it. To test, #define REVERSE before this clause.
	 */
	{
		int i;
		for (i = 0; i < cnt/2; i--) {
			/* swap res_orig[i] with res_orig[n - 1 - i] */
			HeapTupleHeader x = res_orig[i];
			res_orig[i] = res_orig[n - 1 - i];
			res_orig[n - 1 - i] = x;
		}
	}
#endif
    return cnt;
}

TupleTableSlot *
HeapScanNext(ScanState *scanState)
{
	Assert(IsA(scanState, TableScanState) ||
		   IsA(scanState, DynamicTableScanState));
	SeqScanState *node = (SeqScanState *)scanState;
	Assert(node->opaque != NULL);

	HeapTuple	tuple;
	HeapScanDesc scandesc;
	Index		scanrelid;
	EState	   *estate;
	ScanDirection direction;
	TupleTableSlot *slot;

	Assert((node->ss.scan_state & SCAN_SCAN) != 0);

	/*
	 * get information from the estate and scan state
	 */
	estate = node->ss.ps.state;
	scandesc = node->opaque->ss_currentScanDesc;
	scanrelid = ((SeqScan *) node->ss.ps.plan)->scanrelid;
	direction = estate->es_direction;
	slot = node->ss.ss_ScanTupleSlot;

	/*
	 * Check if we are evaluating PlanQual for tuple of this relation.
	 * Additional checking is not good, but no other way for now. We could
	 * introduce new nodes for this case and handle SeqScan --> NewNode
	 * switching in Init/ReScan plan...
	 */
	if (estate->es_evTuple != NULL &&
		estate->es_evTuple[scanrelid - 1] != NULL)
	{
		if (estate->es_evTupleNull[scanrelid - 1])
		{
			return ExecClearTuple(slot);
		}

		ExecStoreGenericTuple(estate->es_evTuple[scanrelid - 1], slot, false);

		/*
		 * Note that unlike IndexScan, SeqScan never uses keys in
		 * heap_beginscan (and this is very bad) - so, here we do not check
		 * the keys.
		 */

		/* Flag for the next call that no more tuples */
		estate->es_evTupleNull[scanrelid - 1] = true;
		return slot;
	}

	/*
	 * get the next tuple from the access methods
	 */
	if (node->opaque->ss_heapTupleData.bot == node->opaque->ss_heapTupleData.top &&
		!node->opaque->ss_heapTupleData.seen_EOS)
	{
		node->opaque->ss_heapTupleData.last = NULL;
		node->opaque->ss_heapTupleData.bot = 0;
		node->opaque->ss_heapTupleData.top = lengthof(node->opaque->ss_heapTupleData.item);
		heap_getnextx(scandesc, direction, node->opaque->ss_heapTupleData.item,
					  &node->opaque->ss_heapTupleData.top,
					  &node->opaque->ss_heapTupleData.seen_EOS);

		if (scandesc->rs_pageatatime &&
		   IsA(scanState, TableScanState))
		{
			Gpmon_M_Incr(GpmonPktFromTableScanState((TableScanState *)scanState), GPMON_TABLESCAN_PAGE);
			CheckSendPlanStateGpmonPkt(&node->ss.ps);
		}
	}

	node->opaque->ss_heapTupleData.last = NULL;
	if (node->opaque->ss_heapTupleData.bot < node->opaque->ss_heapTupleData.top)
	{
		 node->opaque->ss_heapTupleData.last = 
			 &node->opaque->ss_heapTupleData.item[node->opaque->ss_heapTupleData.bot++];
	}

	tuple = node->opaque->ss_heapTupleData.last;

	/*
	 * save the tuple and the buffer returned to us by the access methods in
	 * our scan tuple slot and return the slot.  Note: we pass 'false' because
	 * tuples returned by heap_getnext() are pointers onto disk pages and were
	 * not created with palloc() and so should not be pfree()'d.  Note also
	 * that ExecStoreTuple will increment the refcount of the buffer; the
	 * refcount will not be dropped until the tuple table slot is cleared.
	 */
	if (tuple)
	{
		ExecStoreHeapTuple(tuple,
						   slot,
						   scandesc->rs_cbuf,
						   false);
	}
	
	else
	{
		ExecClearTuple(slot);
	}

	return slot;
}

void
BeginScanHeapRelation(ScanState *scanState)
{
	Assert(IsA(scanState, TableScanState) ||
		   IsA(scanState, DynamicTableScanState));
	SeqScanState *node = (SeqScanState *)scanState;
	Assert(node->opaque == NULL);
	
	Assert(node->ss.scan_state == SCAN_INIT || node->ss.scan_state == SCAN_DONE);

	InitHeapScanOpaque(scanState);
	
	Assert(node->opaque != NULL);

	node->opaque->ss_currentScanDesc = heap_beginscan(
			node->ss.ss_currentRelation,
			node->ss.ps.state->es_snapshot,
			0,
			NULL);

	node->opaque->ss_heapTupleData.bot = 0;
	node->opaque->ss_heapTupleData.top = 0;
	node->opaque->ss_heapTupleData.seen_EOS = 0;
	node->opaque->ss_heapTupleData.last = NULL;

	node->ss.scan_state = SCAN_SCAN;
}

void
EndScanHeapRelation(ScanState *scanState)
{
	Assert(IsA(scanState, TableScanState) ||
		   IsA(scanState, DynamicTableScanState));
	SeqScanState *node = (SeqScanState *)scanState;

	Assert((node->ss.scan_state & SCAN_SCAN) != 0);

	Assert(node->opaque != NULL &&
		   node->opaque->ss_currentScanDesc != NULL);
	heap_endscan(node->opaque->ss_currentScanDesc);

	FreeHeapScanOpaque(scanState);

	node->ss.scan_state = SCAN_INIT;
}

void
ReScanHeapRelation(ScanState *scanState)
{
	Assert(IsA(scanState, TableScanState) ||
		   IsA(scanState, DynamicTableScanState));
	SeqScanState *node = (SeqScanState *)scanState;
	Assert(node->opaque != NULL &&
		   node->opaque->ss_currentScanDesc != NULL);

	heap_rescan(node->opaque->ss_currentScanDesc, NULL /* new scan keys */);

	node->opaque->ss_heapTupleData.bot = 0;
	node->opaque->ss_heapTupleData.top = 0;
	node->opaque->ss_heapTupleData.seen_EOS = 0;
	node->opaque->ss_heapTupleData.last = NULL;
}

void
MarkPosHeapRelation(ScanState *scanState)
{
	Assert(IsA(scanState, TableScanState) ||
		   IsA(scanState, DynamicTableScanState));
	SeqScanState *node = (SeqScanState *)scanState;
	Assert((node->ss.scan_state & SCAN_SCAN) != 0);
	Assert(node->opaque != NULL &&
		   node->opaque->ss_currentScanDesc != NULL);

	heap_markposx(node->opaque->ss_currentScanDesc,
				  node->opaque->ss_heapTupleData.last);

	node->ss.scan_state |= SCAN_MARKPOS;
}

void
RestrPosHeapRelation(ScanState *scanState)
{
	Assert(IsA(scanState, TableScanState) ||
		   IsA(scanState, DynamicTableScanState));
	SeqScanState *node = (SeqScanState *)scanState;

	Assert((node->ss.scan_state & SCAN_SCAN) != 0);
	Assert((node->ss.scan_state & SCAN_MARKPOS) != 0);

	/*
	 * Clear any reference to the previously returned tuple.  This is needed
	 * because the slot is simply pointing at scan->rs_cbuf, which
	 * heap_restrpos will change; we'd have an internally inconsistent slot if
	 * we didn't do this.
	 */
	ExecClearTuple(node->ss.ss_ScanTupleSlot);

	Assert(node->opaque != NULL &&
		   node->opaque->ss_currentScanDesc != NULL);
	heap_restrpos(node->opaque->ss_currentScanDesc);

	node->ss.scan_state &= (~ ((int) SCAN_MARKPOS));
}

