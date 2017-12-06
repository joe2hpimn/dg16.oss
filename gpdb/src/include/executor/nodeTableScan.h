/*
 * nodeTableScan.h
 *
 * Copyright (c) 2012 - present, EMC/Greenplum
 */
#ifndef NODETABLESCAN_H
#define NODETABLESCAN_H

#include "nodes/execnodes.h"

extern int	ExecCountSlotsTableScan(TableScan *node);
extern TableScanState *ExecInitTableScan(TableScan *node, EState *estate, int eflags);
extern TupleTableSlot *ExecTableScan(TableScanState *node);
extern void ExecEndTableScan(TableScanState *node);
extern void ExecTableMarkPos(TableScanState *node);
extern void ExecTableRestrPos(TableScanState *node);
extern void ExecTableReScan(TableScanState *node, ExprContext *exprCtxt);
extern void ExecEagerFreeTableScan(TableScanState *node);

enum 
{
	GPMON_TABLESCAN_PAGE = GPMON_QEXEC_M_NODE_START,
	GPMON_TABLESCAN_RESTOREPOS,
	GPMON_TABLESCAN_RESCAN,
	GPMON_TABLESCAN_TOTAL,
};

static inline gpmon_packet_t * GpmonPktFromTableScanState(TableScanState *node)
{
	return &node->ss.ps.gpmon_pkt;
}

#endif
