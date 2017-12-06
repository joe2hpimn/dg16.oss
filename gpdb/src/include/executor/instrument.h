/*-------------------------------------------------------------------------
 *
 * instrument.h
 *	  definitions for run-time statistics collection
 *
 *
 * Portions Copyright (c) 2006-2009, Greenplum inc
 * Copyright (c) 2001-2009, PostgreSQL Global Development Group
 *
 * $PostgreSQL: pgsql/src/include/executor/instrument.h,v 1.16 2006/06/09 19:30:56 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef INSTRUMENT_H
#define INSTRUMENT_H

#include "portability/instr_time.h"

struct CdbExplain_NodeSummary;          /* private def in cdb/cdbexplain.c */

/* 
 * EXX EXX_IN_PG
 *  Jit instrumentation, and if failure, failure reason.
 */
typedef struct ExxJitInfo
{
    char    reason[80]; 
    char    filename[80]; 
    int32_t lineno;
    int32_t oid;
    int32_t sliceId;
} ExxJitInfo;

#define EXX_JIT_INFO_MAX 128
typedef struct ExxJitInstrumentation
{
    ExxJitInfo jitinfo[EXX_JIT_INFO_MAX];
} ExxJitInstrumentation;

typedef struct Instrumentation
{
	/* Info about current plan cycle: */
	bool		running;		/* TRUE if we've completed first tuple */
	instr_time	starttime;		/* Start time of current iteration of node */
	instr_time	counter;		/* Accumulated runtime for this node */
	double		firsttuple;		/* Time for first tuple of this cycle */
	double		tuplecount;		/* Tuples emitted so far this cycle */
	/* Accumulated statistics across all completed cycles: */
	double		startup;		/* Total startup time (in seconds) */
	double		total;			/* Total total time (in seconds) */
	double		ntuples;		/* Total tuples produced */
	double		nloops;			/* # of run cycles for this node */
    double		execmemused;    /* CDB: executor memory used (bytes) */
    double		workmemused;    /* CDB: work_mem actually used (bytes) */
    double		workmemwanted;  /* CDB: work_mem to avoid scratch i/o (bytes) */
	instr_time	firststart;		/* CDB: Start time of first iteration of node */
	bool		workfileReused; /* TRUE if cached workfiles reused in this node */
	bool		workfileCreated;/* TRUE if workfiles are created in this node */
	int		numPartScanned; /* Number of part tables scanned */

    struct CdbExplain_NodeSummary  *cdbNodeSummary; /* stats from all qExecs */
} Instrumentation;

extern Instrumentation *InstrAlloc(int n);
extern void InstrStartNode(Instrumentation *instr);
extern void InstrStopNode(Instrumentation *instr, double nTuples);
extern void InstrEndLoop(Instrumentation *instr);

#endif   /* INSTRUMENT_H */
