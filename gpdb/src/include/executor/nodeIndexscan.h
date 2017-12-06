/*-------------------------------------------------------------------------
 *
 * nodeIndexscan.h
 *
 *
 *
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $PostgreSQL: pgsql/src/include/executor/nodeIndexscan.h,v 1.29 2006/10/04 00:30:08 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEINDEXSCAN_H
#define NODEINDEXSCAN_H

#include "nodes/execnodes.h"

extern int	ExecCountSlotsIndexScan(IndexScan *node);
extern IndexScanState *ExecInitIndexScan(IndexScan *node, EState *estate, int eflags);
extern TupleTableSlot *ExecIndexScan(IndexScanState *node);
extern void ExecEndIndexScan(IndexScanState *node);
extern void ExecIndexMarkPos(IndexScanState *node);
extern void ExecIndexRestrPos(IndexScanState *node);
extern void ExecIndexReScan(IndexScanState *node, ExprContext *exprCtxt);
extern void ExecEagerFreeIndexScan(IndexScanState *node);

/* routines exported to share code with nodeBitmapIndexscan.c */
extern void ExecIndexBuildScanKeys(PlanState *planstate, Relation index,
					   List *quals, List *strategies, List *subtypes,
					   ScanKey *scanKeys, int *numScanKeys,
					   IndexRuntimeKeyInfo **runtimeKeys, int *numRuntimeKeys,
					   IndexArrayKeyInfo **arrayKeys, int *numArrayKeys);
extern void ExecIndexEvalRuntimeKeys(ExprContext *econtext,
					   IndexRuntimeKeyInfo *runtimeKeys, int numRuntimeKeys);
extern bool ExecIndexEvalArrayKeys(ExprContext *econtext,
					   IndexArrayKeyInfo *arrayKeys, int numArrayKeys);
extern bool ExecIndexAdvanceArrayKeys(IndexArrayKeyInfo *arrayKeys, int numArrayKeys);

extern TupleTableSlot *IndexNext(IndexScanState *node);

enum {
	GPMON_INDEXSCAN_RESTOREPOS = GPMON_QEXEC_M_NODE_START,
	GPMON_INDEXSCAN_RESCAN,
	GPMON_INDEXSCAN_TOTAL,
};

static inline gpmon_packet_t * GpmonPktFromIndexScanState(IndexScanState *node)
{
	return &node->ss.ps.gpmon_pkt;
}
#endif   /* NODEINDEXSCAN_H */
