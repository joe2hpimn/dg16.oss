/*-------------------------------------------------------------------------
 *
 * nodeAppend.h
 *
 *
 *
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $PostgreSQL: pgsql/src/include/executor/nodeAppend.h,v 1.25 2006/03/05 15:58:56 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEAPPEND_H
#define NODEAPPEND_H

#include "nodes/execnodes.h"

extern int	ExecCountSlotsAppend(Append *node);
extern AppendState *ExecInitAppend(Append *node, EState *estate, int eflags);
extern TupleTableSlot *ExecAppend(AppendState *node);
extern void ExecEndAppend(AppendState *node);
extern void ExecReScanAppend(AppendState *node, ExprContext *exprCtxt);

enum 
{
	GPMON_APPEND_CURRTABLE = GPMON_QEXEC_M_NODE_START,
	GPMON_APPEND_TOTAL
};

static inline gpmon_packet_t * GpmonPktFromAppendState(AppendState *node)
{
	return &node->ps.gpmon_pkt;
}

#endif   /* NODEAPPEND_H */
