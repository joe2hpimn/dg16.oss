/*-------------------------------------------------------------------------
 *
 * nodeBitmapHeapscan.c
 *	  Routines to support bitmapped scans of relations
 *
 * NOTE: it is critical that this plan type only be used with MVCC-compliant
 * snapshots (ie, regular snapshots, not SnapshotNow or one of the other
 * special snapshots).	The reason is that since index and heap scans are
 * decoupled, there can be no assurance that the index tuple prompting a
 * visit to a particular heap TID still exists when the visit is made.
 * Therefore the tuple might not exist anymore either (which is OK because
 * heap_fetch will cope) --- but worse, the tuple slot could have been
 * re-used for a newer tuple.  With an MVCC snapshot the newer tuple is
 * certain to fail the time qual and so it will not be mistakenly returned.
 * With SnapshotNow we might return a tuple that doesn't meet the required
 * index qual conditions.
 *
 *
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/executor/nodeBitmapHeapscan.c,v 1.14.2.1 2006/12/26 19:26:56 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */
/*
 * INTERFACE ROUTINES
 *		ExecBitmapHeapScan			scans a relation using bitmap info
 *		ExecBitmapHeapNext			workhorse for above
 *		ExecInitBitmapHeapScan		creates and initializes state info.
 *		ExecBitmapHeapReScan		prepares to rescan the plan.
 *		ExecEndBitmapHeapScan		releases all storage.
 */
#include "postgres.h"

#include "access/heapam.h"
#include "access/relscan.h"
#include "access/transam.h"
#include "executor/execdebug.h"
#include "executor/nodeBitmapHeapscan.h"
#include "pgstat.h"
#include "storage/bufmgr.h"
#include "utils/memutils.h"
#include "miscadmin.h"
#include "parser/parsetree.h"
#include "cdb/cdbvars.h" /* gp_select_invisible */
#include "cdb/cdbfilerepprimary.h"
#include "nodes/tidbitmap.h"


static TupleTableSlot *BitmapHeapNext(BitmapHeapScanState *node);
static void bitgetpage(HeapScanDesc scan, TBMIterateResult *tbmres);

/*
 * EXX: expose
 */
extern void exx_bitgetpage(HeapScanDesc scan, TBMIterateResult *tbmres);
void exx_bitgetpage(HeapScanDesc scan, TBMIterateResult *tbmres) 
{
    bitgetpage(scan, tbmres);
}

/*
 * Initialize the heap scan descriptor if it is not initialized.
 */
static inline void
initScanDesc(BitmapHeapScanState *scanstate)
{
	Relation currentRelation = scanstate->ss.ss_currentRelation;
	EState *estate = scanstate->ss.ps.state;
	
	if (scanstate->ss_currentScanDesc == NULL)
	{
		/*
		 * Even though we aren't going to do a conventional seqscan, it is useful
		 * to create a HeapScanDesc --- this checks the relation size and sets up
		 * statistical infrastructure for us.
		 */
		scanstate->ss_currentScanDesc = heap_beginscan(currentRelation,
													   estate->es_snapshot,
													   0,
													   NULL);
		
		/*
		 * One problem is that heap_beginscan counts a "sequential scan" start,
		 * when we actually aren't doing any such thing.  Reverse out the added
		 * scan count.	(Eventually we may want to count bitmap scans separately.)
		 */
		pgstat_discount_heap_scan(currentRelation);
	}
}

/*
 * Free the heap scan descriptor.
 */
static inline void
freeScanDesc(BitmapHeapScanState *scanstate)
{
	if (scanstate->ss_currentScanDesc != NULL)
	{
		heap_endscan(scanstate->ss_currentScanDesc);
		scanstate->ss_currentScanDesc = NULL;
	}
}

/*
 * Initialize the state relevant to bitmaps.
 */
static inline void
initBitmapState(BitmapHeapScanState *scanstate)
{
	if (scanstate->tbmres == NULL)
		scanstate->tbmres =
			palloc0(sizeof(TBMIterateResult) +
					MAX_TUPLES_PER_PAGE * sizeof(OffsetNumber));
}

/*
 * Free the state relevant to bitmaps
 */
static inline void
freeBitmapState(BitmapHeapScanState *scanstate)
{
	if (scanstate->tbm != NULL)
	{
		if(IsA(scanstate->tbm, HashBitmap))
			tbm_free((HashBitmap *)scanstate->tbm);
		else
            tbm_bitmap_free(scanstate->tbm);

		scanstate->tbm = NULL;
	}
	if (scanstate->tbmres != NULL)
	{
		pfree(scanstate->tbmres);
		scanstate->tbmres = NULL;
	}
}

/* ----------------------------------------------------------------
 *		BitmapHeapNext
 *
 *		Retrieve next tuple from the BitmapHeapScan node's currentRelation
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
BitmapHeapNext(BitmapHeapScanState *node)
{
	EState	   *estate;
	ExprContext *econtext;
	HeapScanDesc scan;
	Index		scanrelid;
	Node  		*tbm;
	TBMIterateResult *tbmres;
	OffsetNumber targoffset;
	TupleTableSlot *slot;
	bool		more = true;

	/*
	 * extract necessary information from index scan node
	 */
	estate = node->ss.ps.state;
	econtext = node->ss.ps.ps_ExprContext;
	slot = node->ss.ss_ScanTupleSlot;

	initScanDesc(node);
	initBitmapState(node);

	scan = node->ss_currentScanDesc;
	scanrelid = ((BitmapHeapScan *) node->ss.ps.plan)->scan.scanrelid;
	tbm = node->tbm;
	tbmres = (TBMIterateResult *) node->tbmres;

	/*
	 * Check if we are evaluating PlanQual for tuple of this relation.
	 * Additional checking is not good, but no other way for now. We could
	 * introduce new nodes for this case and handle IndexScan --> NewNode
	 * switching in Init/ReScan plan...
	 */
	if (estate->es_evTuple != NULL &&
		estate->es_evTuple[scanrelid - 1] != NULL)
	{
		if (estate->es_evTupleNull[scanrelid - 1])
		{
			ExecEagerFreeBitmapHeapScan(node);
			
			return ExecClearTuple(slot);
		}
		

		ExecStoreGenericTuple(estate->es_evTuple[scanrelid - 1],
					   slot, false);

		/* Does the tuple meet the original qual conditions? */
		econtext->ecxt_scantuple = slot;

		ResetExprContext(econtext);

		if (!ExecQual(node->bitmapqualorig, econtext, false))
		{
			ExecEagerFreeBitmapHeapScan(node);

			ExecClearTuple(slot);		/* would not be returned by scan */
		}

		/* Flag for the next call that no more tuples */
		estate->es_evTupleNull[scanrelid - 1] = true;

		if (!TupIsNull(slot))
		{
			Gpmon_M_Incr_Rows_Out(GpmonPktFromBitmapHeapScanState(node));
			CheckSendPlanStateGpmonPkt(&node->ss.ps);
		}
		return slot;
	}

	/*
	 * If we haven't yet performed the underlying index scan, or
	 * we have used up the bitmaps from the previous scan, do the next scan,
	 * and prepare the bitmap to be iterated over.
 	 */
	if (tbm == NULL)
	{
		tbm = (Node *) MultiExecProcNode(outerPlanState(node));

		if (tbm != NULL && (!(IsA(tbm, HashBitmap) ||
							  IsA(tbm, StreamBitmap))))
			elog(ERROR, "unrecognized result from subplan");

		/* When a HashBitmap is returned, set the returning bitmaps
		 * in the subplan to NULL, so that the subplan nodes do not
		 * mistakenly try to release the space during the rescan.
		 */
		if (tbm != NULL && IsA(tbm, HashBitmap))
			tbm_reset_bitmaps(outerPlanState(node));

		node->tbm = tbm;
	}

	for (;;)
	{
		Page		dp;
		ItemId		lp;

		if (tbmres == NULL || tbmres->ntuples == 0)
		{
			CHECK_FOR_INTERRUPTS();

			if (QueryFinishPending)
				return NULL;

			if (tbm == NULL)
				more = false;
			else
				more = tbm_iterate(tbm, tbmres);

			if (!more)
			{
				/* no more entries in the bitmap */
				break;
			}

			/*
			 * Ignore any claimed entries past what we think is the end of
			 * the relation.  (This is probably not necessary given that we
			 * got at least AccessShareLock on the table before performing
			 * any of the indexscans, but let's be safe.)
			 */
			if (tbmres->blockno >= scan->rs_nblocks)
			{
				more = false;
				tbmres->ntuples = 0;
				continue;
			}

			/* If tbmres contains no tuples, continue. */
			if (tbmres->ntuples == 0)
				continue;

			/*
			 * Fetch the current heap page and identify candidate tuples.
			 */
			bitgetpage(scan, tbmres);

			Gpmon_M_Incr(GpmonPktFromBitmapHeapScanState(node), GPMON_BITMAPHEAPSCAN_PAGE);
			CheckSendPlanStateGpmonPkt(&node->ss.ps);

			/*
		 	* Set rs_cindex to first slot to examine
		 	*/
			scan->rs_cindex = 0;
		}
		else
		{
			/*
			 * Continuing in previously obtained page; advance rs_cindex
			 */
			scan->rs_cindex++;
			tbmres->ntuples--;
		}

		/*
		 * Out of range?  If so, nothing more to look at on this page
		 */
		if (scan->rs_cindex < 0 || scan->rs_cindex >= scan->rs_ntuples)
		{
			more = false;
			tbmres->ntuples = 0;
			continue;
		}

		/*
		 * Okay to fetch the tuple
		 */
		targoffset = scan->rs_vistuples[scan->rs_cindex];
		dp = (Page) BufferGetPage(scan->rs_cbuf);
		lp = PageGetItemId(dp, targoffset);
		Assert(ItemIdIsUsed(lp));

		scan->rs_ctup.t_data = (HeapTupleHeader) PageGetItem((Page) dp, lp);
		scan->rs_ctup.t_len = ItemIdGetLength(lp);
		ItemPointerSet(&scan->rs_ctup.t_self, tbmres->blockno, targoffset);

		pgstat_count_heap_fetch(scan->rs_rd);

		/*
		 * Set up the result slot to point to this tuple. Note that the slot
		 * acquires a pin on the buffer.
		 */
		ExecStoreHeapTuple(&scan->rs_ctup,
					   slot,
					   scan->rs_cbuf,
					   false);

		/*
		 * We recheck the qual conditions for every tuple, since the bitmap
		 * may contain invalid entries from deleted tuples.
		 */
		econtext->ecxt_scantuple = slot;
		ResetExprContext(econtext);

		if (!ExecQual(node->bitmapqualorig, econtext, false))
		{
			/* Fails recheck, so drop it and loop back for another */
			ExecClearTuple(slot);
			continue;
		}

		/* OK to return this tuple */
		if (!TupIsNull(slot))
		{
			Gpmon_M_Incr_Rows_Out(GpmonPktFromBitmapHeapScanState(node));
			CheckSendPlanStateGpmonPkt(&node->ss.ps);
		}
		return slot;
	}

	ExecEagerFreeBitmapHeapScan(node);

	/*
	 * if we get here it means we are at the end of the scan..
	 */
	return ExecClearTuple(slot);
}

/*
 * bitgetpage - subroutine for BitmapHeapNext()
 *
 * This routine reads and pins the specified page of the relation, then
 * builds an array indicating which tuples on the page are both potentially
 * interesting according to the bitmap, and visible according to the snapshot.
 */
static void
bitgetpage(HeapScanDesc scan, TBMIterateResult *tbmres)
{
	MIRROREDLOCK_BUFMGR_DECLARE;

	BlockNumber page = tbmres->blockno;
	Buffer		buffer;
	Snapshot	snapshot;
	Page		dp;
	int			ntup;
	int			curslot;
	int			minslot;
	int			maxslot;
	int			maxoff;

	/*
	 * Acquire pin on the target heap page, trading in any pin we held before.
	 */
	Assert(page < scan->rs_nblocks);
	
	// -------- MirroredLock ----------
	MIRROREDLOCK_BUFMGR_LOCK;
	
	scan->rs_cbuf = ReleaseAndReadBuffer(scan->rs_cbuf,
										 scan->rs_rd,
										 page);

	buffer = scan->rs_cbuf;
	snapshot = scan->rs_snapshot;

	/*
	 * We must hold share lock on the buffer content while examining tuple
	 * visibility.	Afterwards, however, the tuples we have found to be
	 * visible are guaranteed good as long as we hold the buffer pin.
	 */
	LockBuffer(buffer, BUFFER_LOCK_SHARE);

	dp = (Page) BufferGetPage(buffer);
	maxoff = PageGetMaxOffsetNumber(dp);

	/*
	 * Determine how many entries we need to look at on this page. If the
	 * bitmap is lossy then we need to look at each physical item pointer;
	 * otherwise we just look through the offsets listed in tbmres.
	 */
	if (tbmres->ntuples >= 0)
	{
		/* non-lossy case */
		minslot = 0;
		maxslot = tbmres->ntuples - 1;
	}
	else
	{
		/* lossy case */
		minslot = FirstOffsetNumber;
		maxslot = maxoff;
	}

	ntup = 0;
	for (curslot = minslot; curslot <= maxslot; curslot++)
	{
		OffsetNumber targoffset;
		ItemId		lp;
		HeapTupleData loctup;
		bool		valid;

		if (tbmres->ntuples >= 0)
		{
			/* non-lossy case */
			targoffset = tbmres->offsets[curslot];
		}
		else
		{
			/* lossy case */
			targoffset = (OffsetNumber) curslot;
		}

		/*
		 * We'd better check for out-of-range offnum in case of VACUUM since
		 * the TID was obtained.
		 */
		if (targoffset < FirstOffsetNumber || targoffset > maxoff)
			continue;

		lp = PageGetItemId(dp, targoffset);

		/*
		 * Must check for deleted tuple.
		 */
		if (!ItemIdIsUsed(lp))
			continue;

		/*
		 * check time qualification of tuple, remember it if valid
		 */
		loctup.t_data = (HeapTupleHeader) PageGetItem((Page) dp, lp);
		loctup.t_len = ItemIdGetLength(lp);
		ItemPointerSet(&(loctup.t_self), page, targoffset);

		valid = HeapTupleSatisfiesVisibility(scan->rs_rd, &loctup, snapshot, buffer);
		if (valid)
			scan->rs_vistuples[ntup++] = targoffset;
	}

	LockBuffer(buffer, BUFFER_LOCK_UNLOCK);
	
	MIRROREDLOCK_BUFMGR_UNLOCK;
	// -------- MirroredLock ----------
	
	Assert(ntup <= MaxHeapTuplesPerPage);
	scan->rs_ntuples = ntup;
}

/* ----------------------------------------------------------------
 *		ExecBitmapHeapScan(node)
 * ----------------------------------------------------------------
 */
TupleTableSlot *
ExecBitmapHeapScan(BitmapHeapScanState *node)
{
	/*
	 * use BitmapHeapNext as access method
	 */
	return ExecScan(&node->ss, (ExecScanAccessMtd) BitmapHeapNext);
}

/* ----------------------------------------------------------------
 *		ExecBitmapHeapReScan(node)
 * ----------------------------------------------------------------
 */
void
ExecBitmapHeapReScan(BitmapHeapScanState *node, ExprContext *exprCtxt)
{
	EState	   *estate;
	Index		scanrelid;

	initScanDesc(node);

	estate = node->ss.ps.state;
	scanrelid = ((BitmapHeapScan *) node->ss.ps.plan)->scan.scanrelid;

	/* node->ss.ps.ps_TupFromTlist = false; */

	/*
	 * If we are being passed an outer tuple, link it into the "regular"
	 * per-tuple econtext for possible qual eval.
	 */
	if (exprCtxt != NULL)
	{
		ExprContext *stdecontext;

		stdecontext = node->ss.ps.ps_ExprContext;
		stdecontext->ecxt_outertuple = exprCtxt->ecxt_outertuple;
	}

	/* If this is re-scanning of PlanQual ... */
	if (estate->es_evTuple != NULL &&
		estate->es_evTuple[scanrelid - 1] != NULL)
	{
		estate->es_evTupleNull[scanrelid - 1] = false;
	}

	/* rescan to release any page pin */
	heap_rescan(node->ss_currentScanDesc, NULL);

	/* undo bogus "seq scan" count (see notes in ExecInitBitmapHeapScan) */
	pgstat_discount_heap_scan(node->ss.ss_currentRelation);

	freeBitmapState(node);

	tbm_reset_bitmaps(outerPlanState(node));

	/*
	 * Always rescan the input immediately, to ensure we can pass down any
	 * outer tuple that might be used in index quals.
	 */
	Gpmon_M_Incr(GpmonPktFromBitmapHeapScanState(node), GPMON_BITMAPHEAPSCAN_RESCAN);
	CheckSendPlanStateGpmonPkt(&node->ss.ps);
	ExecReScan(outerPlanState(node), exprCtxt);
}

/* ----------------------------------------------------------------
 *		ExecEndBitmapHeapScan
 * ----------------------------------------------------------------
 */
void
ExecEndBitmapHeapScan(BitmapHeapScanState *node)
{
	Relation	relation;
	HeapScanDesc scanDesc;

	/*
	 * extract information from the node
	 */
	relation = node->ss.ss_currentRelation;
	scanDesc = node->ss_currentScanDesc;

	/*
	 * Free the exprcontext
	 */
	ExecFreeExprContext(&node->ss.ps);

	/*
	 * clear out tuple table slots
	 */
	ExecClearTuple(node->ss.ps.ps_ResultTupleSlot);
	ExecClearTuple(node->ss.ss_ScanTupleSlot);

	/*
	 * close down subplans
	 */
	ExecEndNode(outerPlanState(node));

	ExecEagerFreeBitmapHeapScan(node);

	/*
	 * close the heap relation.
	 */
	ExecCloseScanRelation(relation);

	EndPlanStateGpmonPkt(&node->ss.ps);
}

/* ----------------------------------------------------------------
 *		ExecInitBitmapHeapScan
 *
 *		Initializes the scan's state information.
 * ----------------------------------------------------------------
 */
BitmapHeapScanState *
ExecInitBitmapHeapScan(BitmapHeapScan *node, EState *estate, int eflags)
{
	BitmapHeapScanState *scanstate;
	Relation	currentRelation;

	/* check for unsupported flags */
	Assert(!(eflags & (EXEC_FLAG_BACKWARD | EXEC_FLAG_MARK)));

	/*
	 * Assert caller didn't ask for an unsafe snapshot --- see comments at
	 * head of file.
	 *
	 * MPP-4703: the MVCC-snapshot restriction is required for correct results.
	 * our test-mode may deliberately return incorrect results, but that's OK.
	 */
	Assert(IsMVCCSnapshot(estate->es_snapshot) || gp_select_invisible);

	/*
	 * create state structure
	 */
	scanstate = makeNode(BitmapHeapScanState);
	scanstate->ss.ps.plan = (Plan *) node;
	scanstate->ss.ps.state = estate;

	scanstate->tbm = NULL;
	scanstate->tbmres = NULL;

	/*
	 * Miscellaneous initialization
	 *
	 * create expression context for node
	 */
	ExecAssignExprContext(estate, &scanstate->ss.ps);

	/* scanstate->ss.ps.ps_TupFromTlist = false;*/

	/*
	 * initialize child expressions
	 */
	scanstate->ss.ps.targetlist = (List *)
		ExecInitExpr((Expr *) node->scan.plan.targetlist,
					 (PlanState *) scanstate);
	scanstate->ss.ps.qual = (List *)
		ExecInitExpr((Expr *) node->scan.plan.qual,
					 (PlanState *) scanstate);
	scanstate->bitmapqualorig = (List *)
		ExecInitExpr((Expr *) node->bitmapqualorig,
					 (PlanState *) scanstate);

#define BITMAPHEAPSCAN_NSLOTS 2

	/*
	 * tuple table initialization
	 */
	ExecInitResultTupleSlot(estate, &scanstate->ss.ps);
	ExecInitScanTupleSlot(estate, &scanstate->ss);

	/*
	 * open the base relation and acquire appropriate lock on it.
	 */
	currentRelation = ExecOpenScanRelation(estate, node->scan.scanrelid);

	scanstate->ss.ss_currentRelation = currentRelation;

	/*
	 * get the scan type from the relation descriptor.
	 */
	ExecAssignScanType(&scanstate->ss, RelationGetDescr(currentRelation));

	/*
	 * Initialize result tuple type and projection info.
	 */
	ExecAssignResultTypeFromTL(&scanstate->ss.ps);
	ExecAssignScanProjectionInfo(&scanstate->ss);

	/*
	 * initialize child nodes
	 *
	 * We do this last because the child nodes will open indexscans on our
	 * relation's indexes, and we want to be sure we have acquired a lock on
	 * the relation first.
	 */
	outerPlanState(scanstate) = ExecInitNode(outerPlan(node), estate, eflags);

	initGpmonPktForBitmapHeapScan((Plan *)node, &scanstate->ss.ps.gpmon_pkt, estate);

	/*
	 * all done.
	 */
	return scanstate;
}

int
ExecCountSlotsBitmapHeapScan(BitmapHeapScan *node)
{
	return ExecCountSlotsNode(outerPlan((Plan *) node)) +
		ExecCountSlotsNode(innerPlan((Plan *) node)) + BITMAPHEAPSCAN_NSLOTS;
}

void
initGpmonPktForBitmapHeapScan(Plan *planNode, gpmon_packet_t *gpmon_pkt, EState *estate)
{
	Assert(planNode != NULL && gpmon_pkt != NULL && IsA(planNode, BitmapHeapScan));

	{
		RangeTblEntry *rte = rt_fetch(((BitmapHeapScan *)planNode)->scan.scanrelid,
									  estate->es_range_table);
		char schema_rel_name[SCAN_REL_NAME_BUF_SIZE] = {0};
		
		Assert(GPMON_BITMAPHEAPSCAN_TOTAL <= (int)GPMON_QEXEC_M_COUNT);
		InitPlanNodeGpmonPkt(planNode, gpmon_pkt, estate, PMNT_BitmapHeapScan,
							 (int64)planNode->plan_rows,
							 GetScanRelNameGpmon(rte->relid, schema_rel_name));
	}
}

void
ExecEagerFreeBitmapHeapScan(BitmapHeapScanState *node)
{
	freeScanDesc(node);
	freeBitmapState(node);
}
