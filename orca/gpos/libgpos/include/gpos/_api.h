/*---------------------------------------------------------------------------
 *	Greenplum Database
 *	Copyright (C) 2010 Greenplum, Inc.
 *
 *	@filename:
 *		_api.h
 *
 *	@doc:
 *		GPOS wrapper interface for GPDB.
 *
 *	@owner:
 *
 *	@test:
 *
 *
 *---------------------------------------------------------------------------*/
#ifndef GPOS_api_H
#define GPOS_api_H

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

#ifdef GPOS_DEBUG
extern
bool fEnableExtendedAsserts;   // flag to control extended (time consuming) asserts
#endif // GPOS_DEBUG

/*
 * struct with configuration parameters for task execution;
 * this needs to be in sync with the corresponding structure in optserver.h
 */
struct gpos_exec_params
{
	void *(*func)(void*);           /* task function */
	void *arg;                      /* task argument */
	void *result;                   /* task result */
	void *stack_start;              /* start of current thread's stack */
	char *error_buffer;             /* buffer used to store error messages */
	int error_buffer_size;          /* size of error message buffer */
	volatile bool *abort_requested; /* flag indicating if abort is requested */
};

/* initialize GPOS memory pool, worker pool and message repository */
void gpos_init(void);

/*
 * set number of threads in worker pool
 * return 0 for successful completion, 1 for error
 */
int gpos_set_threads(int min, int max);

/*
 * execute function as a GPOS task using current thread;
 * return 0 for successful completion, 1 for error
 */
int gpos_exec(gpos_exec_params *params);

/* shutdown GPOS memory pool, worker pool and message repository */
void gpos_terminate(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !GPOS_api_H */

// EOF

