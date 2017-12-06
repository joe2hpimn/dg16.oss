/*-------------------------------------------------------------------------
 *
 * nodeWindow.h
 *	  prototypes for nodeWindow.c
 *
 *
 * Copyright (c) 2007-2008, Greenplum inc *
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEWINDOW_H
#define NODEWINDOW_H

#include "nodes/execnodes.h"

extern int	ExecCountSlotsWindow(Window *node);
extern WindowState *ExecInitWindow(Window *node, EState *estate, int eflags);
extern TupleTableSlot *ExecWindow(WindowState *node);
extern void ExecEndWindow(WindowState *node);
extern void ExecReScanWindow(WindowState *node, ExprContext *exprCtxt);
extern void ExecEagerFreeWindow(WindowState *node);

/* Dummy window function implementation for pg_proc (prosrc). */
extern Datum window_dummy(PG_FUNCTION_ARGS);

/* Internal-use function to tag tuples with originating segment. */
extern Datum mpp_execution_segment(PG_FUNCTION_ARGS);
extern Datum gp_execution_dbid(PG_FUNCTION_ARGS);

/* Declarations for window function implementations. */
extern Datum row_number_immed(PG_FUNCTION_ARGS);
extern Datum rank_immed(PG_FUNCTION_ARGS);
extern Datum dense_rank_immed(PG_FUNCTION_ARGS);
extern Datum cume_dist_prelim(PG_FUNCTION_ARGS);
extern Datum percent_rank_final(PG_FUNCTION_ARGS);
extern Datum cume_dist_final(PG_FUNCTION_ARGS);
Datum ntile_prelim_int(PG_FUNCTION_ARGS);
Datum ntile_prelim_bigint(PG_FUNCTION_ARGS);
Datum ntile_prelim_numeric(PG_FUNCTION_ARGS);
Datum ntile_final(PG_FUNCTION_ARGS);
extern Datum lead_generic(PG_FUNCTION_ARGS);
extern Datum last_value_generic(PG_FUNCTION_ARGS);
extern Datum first_value_generic(PG_FUNCTION_ARGS);
extern Datum lag_generic(PG_FUNCTION_ARGS);
extern Datum lead_lag_frame_maker(PG_FUNCTION_ARGS);

enum {
	GPMON_WINDOW_TOTAL = GPMON_QEXEC_M_NODE_START,
};

static inline gpmon_packet_t * GpmonPktFromWindowState(WindowState *node)
{
	return &node->ps.gpmon_pkt;
}

#endif   /* NODEWINDOW_H */
