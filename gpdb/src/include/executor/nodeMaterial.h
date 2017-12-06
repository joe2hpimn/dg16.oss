/*-------------------------------------------------------------------------
 *
 * nodeMaterial.h
 *
 *
 *
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $PostgreSQL: pgsql/src/include/executor/nodeMaterial.h,v 1.25 2006/03/05 15:58:56 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEMATERIAL_H
#define NODEMATERIAL_H

#include "nodes/execnodes.h"

extern int	ExecCountSlotsMaterial(Material *node);
extern MaterialState *ExecInitMaterial(Material *node, EState *estate, int eflags);
extern TupleTableSlot *ExecMaterial(MaterialState *node);
extern void ExecEndMaterial(MaterialState *node);
extern void ExecMaterialMarkPos(MaterialState *node);
extern void ExecMaterialRestrPos(MaterialState *node);
extern void ExecMaterialReScan(MaterialState *node, ExprContext *exprCtxt);
extern void ExecEagerFreeMaterial(MaterialState *node);

enum {
    GPMON_MATERIAL_RESCAN = GPMON_QEXEC_M_NODE_START,
    GPMON_MATERIAL_TOTAL,
};

static inline gpmon_packet_t * GpmonPktFromMaterialState(MaterialState *node)
{
	return &node->ss.ps.gpmon_pkt;
}

#endif   /* NODEMATERIAL_H */
