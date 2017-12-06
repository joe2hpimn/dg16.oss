/*-------------------------------------------------------------------------
 *
 * nodeUnique.c
 *	  Routines to handle unique'ing of queries where appropriate
 *
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/executor/nodeUnique.c,v 1.53 2006/07/14 14:52:19 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
/*
 * INTERFACE ROUTINES
 *		ExecUnique		- generate a unique'd temporary relation
 *		ExecInitUnique	- initialize node and subnodes
 *		ExecEndUnique	- shutdown node and subnodes
 *
 * NOTES
 *		Assumes tuples returned from subplan arrive in
 *		sorted order.
 */

#include "postgres.h"

#include "cdb/cdbvars.h"
#include "executor/executor.h"
#include "executor/nodeUnique.h"
#include "utils/memutils.h"


/* ----------------------------------------------------------------
 *		ExecUnique
 * ----------------------------------------------------------------
 */
TupleTableSlot *				/* return: a tuple or NULL */
ExecUnique(UniqueState *node)
{
	Unique	   *plannode = (Unique *) node->ps.plan;
	TupleTableSlot *resultTupleSlot;
	TupleTableSlot *slot;
	PlanState  *outerPlan;

	/*
	 * get information from the node
	 */
	outerPlan = outerPlanState(node);
	resultTupleSlot = node->ps.ps_ResultTupleSlot;

	/*
	 * now loop, returning only non-duplicate tuples. We assume that the
	 * tuples arrive in sorted order so we can detect duplicates easily.
	 * The first tuple of each group is returned.
	 */
	for (;;)
	{
		/*
		 * fetch a tuple from the outer subplan
		 */
		slot = ExecProcNode(outerPlan);
		if (TupIsNull(slot))
		{
			/* end of subplan, so we're done */
			ExecClearTuple(resultTupleSlot);
			return NULL;
		}

		Gpmon_M_Incr(GpmonPktFromUniqueState(node), GPMON_QEXEC_M_ROWSIN); 

		/*
		 * Always return the first tuple from the subplan.
		 */
		if (TupIsNull(resultTupleSlot))
			break;

		/*
		 * Else test if the new tuple and the previously returned tuple match.
		 * If so then we loop back and fetch another new tuple from the
		 * subplan.
		 */
		if (!execTuplesMatch(slot, resultTupleSlot,
							 plannode->numCols, plannode->uniqColIdx,
							 node->eqfunctions,
							 node->tempContext))
			break;
	}

	/*
	 * We have a new tuple different from the previous saved tuple (if any).
	 * Save it and return it.  We must copy it because the source subplan
	 * won't guarantee that this source tuple is still accessible after
	 * fetching the next source tuple.
	 */
   	if (!TupIsNull(slot))
    	{
  		Gpmon_M_Incr_Rows_Out(GpmonPktFromUniqueState(node)); 
   		CheckSendPlanStateGpmonPkt(&node->ps);
    	}

	return ExecCopySlot(resultTupleSlot, slot);
}

/* ----------------------------------------------------------------
 *		ExecInitUnique
 *
 *		This initializes the unique node state structures and
 *		the node's subplan.
 * ----------------------------------------------------------------
 */
UniqueState *
ExecInitUnique(Unique *node, EState *estate, int eflags)
{
	UniqueState *uniquestate;

	/* check for unsupported flags */
	Assert(!(eflags & (EXEC_FLAG_BACKWARD | EXEC_FLAG_MARK)));

	/*
	 * create state structure
	 */
	uniquestate = makeNode(UniqueState);
	uniquestate->ps.plan = (Plan *) node;
	uniquestate->ps.state = estate;

	/*
	 * Miscellaneous initialization
	 *
	 * Unique nodes have no ExprContext initialization because they never call
	 * ExecQual or ExecProject.  But they do need a per-tuple memory context
	 * anyway for calling execTuplesMatch.
	 */
	uniquestate->tempContext =
		AllocSetContextCreate(CurrentMemoryContext,
							  "Unique",
							  ALLOCSET_DEFAULT_MINSIZE,
							  ALLOCSET_DEFAULT_INITSIZE,
							  ALLOCSET_DEFAULT_MAXSIZE);

#define UNIQUE_NSLOTS 1

	/*
	 * Tuple table initialization
	 */
	ExecInitResultTupleSlot(estate, &uniquestate->ps);

	/*
	 * then initialize outer plan
	 */
	outerPlanState(uniquestate) = ExecInitNode(outerPlan(node), estate, eflags);

	/*
	 * unique nodes do no projections, so initialize projection info for this
	 * node appropriately
	 */
	ExecAssignResultTypeFromTL(&uniquestate->ps);
	uniquestate->ps.ps_ProjInfo = NULL;

	/*
	 * Precompute fmgr lookup data for inner loop
	 */
	uniquestate->eqfunctions =
		execTuplesMatchPrepare(ExecGetResultType(&uniquestate->ps),
							   node->numCols,
							   node->uniqColIdx);

	initGpmonPktForUnique((Plan *)node, &uniquestate->ps.gpmon_pkt, estate);
	
	return uniquestate;
}

int
ExecCountSlotsUnique(Unique *node)
{
	return ExecCountSlotsNode(outerPlan(node)) +
		ExecCountSlotsNode(innerPlan(node)) +
		UNIQUE_NSLOTS;
}

/* ----------------------------------------------------------------
 *		ExecEndUnique
 *
 *		This shuts down the subplan and frees resources allocated
 *		to this node.
 * ----------------------------------------------------------------
 */
void
ExecEndUnique(UniqueState *node)
{
	/* clean up tuple table */
	ExecClearTuple(node->ps.ps_ResultTupleSlot);

	MemoryContextDelete(node->tempContext);

	ExecEndNode(outerPlanState(node));

	EndPlanStateGpmonPkt(&node->ps);
}


void
ExecReScanUnique(UniqueState *node, ExprContext *exprCtxt)
{
	/* must clear result tuple so first input tuple is returned */
	ExecClearTuple(node->ps.ps_ResultTupleSlot);

	/*
	 * if chgParam of subnode is not null then plan will be re-scanned by
	 * first ExecProcNode.
	 */
	if (((PlanState *) node)->lefttree->chgParam == NULL)
		ExecReScan(((PlanState *) node)->lefttree, exprCtxt);
}

void
initGpmonPktForUnique(Plan *planNode, gpmon_packet_t *gpmon_pkt, EState *estate)
{
	Assert(planNode != NULL && gpmon_pkt != NULL && IsA(planNode, Unique));

	{
		Assert(GPMON_UNIQUE_TOTAL <= (int)GPMON_QEXEC_M_COUNT);
		InitPlanNodeGpmonPkt(planNode, gpmon_pkt, estate, PMNT_Unique,
							 (int64)planNode->plan_rows,
							 NULL);
	}
}
