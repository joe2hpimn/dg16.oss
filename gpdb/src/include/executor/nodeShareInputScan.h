/*-------------------------------------------------------------------------
 *
 * nodeShareInputScan.h
 *
 * Portions Copyright (c) 2007-2008, Greenplum inc
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODESHAREINPUTSCAN_H
#define NODESHAREINPUTSCAN_H

#include "nodes/execnodes.h"
extern int ExecCountSlotsShareInputScan(ShareInputScan* node);
extern ShareInputScanState *ExecInitShareInputScan(ShareInputScan *node, EState *estate, int eflags);
extern TupleTableSlot *ExecShareInputScan(ShareInputScanState *node);
extern void ExecEndShareInputScan(ShareInputScanState *node);
extern void ExecShareInputScanMarkPos(ShareInputScanState *node);
extern void ExecShareInputScanRestrPos(ShareInputScanState *node);
extern void ExecShareInputScanReScan(ShareInputScanState *node, ExprContext *exprCtxt);
extern void ExecEagerFreeShareInputScan(ShareInputScanState *node);

extern void ExecSliceDependencyShareInputScan(ShareInputScanState *node);

enum {
	GPMON_SHAREINPUT_RESTOREPOS = GPMON_QEXEC_M_NODE_START,
	GPMON_SHAREINPUT_RESCAN,
	GPMON_SHAREINPUT_TOTAL,
};

static inline gpmon_packet_t * GpmonPktFromShareInputState(ShareInputScanState *node)
{
	return &node->ss.ps.gpmon_pkt;
}

#endif   /* NODESHAREINPUTSCAN_H */
