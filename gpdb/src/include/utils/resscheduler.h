/*-------------------------------------------------------------------------
 *
 * resscheduler.h
 *	  POSTGRES resource scheduler definitions.
 *
 *
 * Copyright (c) 2006-2008, Greenplum inc.
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  $PostgreSQL: $
 *
 *-------------------------------------------------------------------------
 */
#ifndef RESSCHEDULER_H
#define RESSCHEDULER_H

#include "nodes/plannodes.h"
#include "storage/lock.h"
#include "tcop/dest.h"

/*
 * GUC variables.
 */
extern bool	ResourceScheduler;
extern int	MaxResourceQueues;
extern int	MaxResourcePortalsPerXact;
extern bool	ResourceSelectOnly;
extern bool	ResourceCleanupIdleGangs;

extern Oid MyQueueId; /* resource queue for current role. */


/*
 * Data structures
 *
 * TODO:
 * To add a equivalent of locallock to handle extensions to proclock
 * and back out the changes to it.
 */


/* Resource Limits */
#define INVALID_RES_LIMIT_THRESHOLD		(-1)
#define NUM_RES_LIMIT_TYPES				3

typedef enum ResLimitType
{
	RES_COUNT_LIMIT,					/* Limit total # */
	RES_COST_LIMIT,						/* Limit total cost */
	RES_MEMORY_LIMIT
} ResLimitType;


typedef struct ResLimitData
{
	ResLimitType	type;				/* Type of limit */
	Cost			threshold_value;	/* Max (or min) value */
	Cost			current_value;		/* Current value */
	bool			threshold_is_max;	/* Is threshold a max or min? */
} ResLimitData;
typedef ResLimitData	*ResLimit;


/* Resource Queues */
typedef struct ResQueueData
{
	Oid				queueid;			/* Id for this queue */
	int				num_limits;			/* Number of limits in this queue */
	bool			overcommit;			/* Does queue allow overcommit? */
	float4			ignorecostlimit;	/* Ignore queries with cost less than.*/
	ResLimitData	limits[NUM_RES_LIMIT_TYPES];	/* The limits */
} ResQueueData;
typedef ResQueueData	*ResQueue;


typedef struct ResSchedulerData
{
	int		num_queues;					/* Current # of queues.*/
} ResSchedulerData;
extern ResSchedulerData	*ResScheduler;

/* Portal and increments  */
#define INVALID_PORTALID 				(~(uint32)0)
typedef struct ResPortalIncrement
{
	int			pid;					/* Process this increment is for. */
	uint32		portalId;				/* Portal Id */
	ResourceOwner	owner;				/* Resource Owner. */
	bool		isHold;					/* Holdable cursor? */
	bool		isCommitted;			/* 1st commit complete? */
	SHM_QUEUE	portalLink;				/* List link in PROCLOCKS list 
										   of ResPortalIncrements. */
	/* The increments - use Cost as it has a suitably large range. */
	Cost		increments[NUM_RES_LIMIT_TYPES];
} ResPortalIncrement;

typedef struct ResPortalTag
{
	int			pid;
	uint32		portalId;
} ResPortalTag;


/*
 * Lock/Queue macros (maybe first one should be in lock.h)?
 */
#define SET_LOCKTAG_RESOURCE_QUEUE(locktag, queueid) \
	((locktag).locktag_field1 = (queueid), \
	 (locktag).locktag_field2 = 0, \
	 (locktag).locktag_field3 = 0, \
	 (locktag).locktag_field4 = 0, \
	 (locktag).locktag_type = LOCKTAG_RESOURCE_QUEUE,		\
	 (locktag).locktag_lockmethodid = RESOURCE_LOCKMETHOD)

#define GET_RESOURCE_QUEUEID_FOR_LOCK(lock) \
	((lock->tag).locktag_field1)

/*
 * Result codes for resource queue alteration.
 */
typedef enum
{
	ALTERQUEUE_OK,					/* Alteration ok. */
	ALTERQUEUE_SMALL_THRESHOLD,		/* New thresholds are too small. */
	ALTERQUEUE_OVERCOMMITTED,		/* Queue is overcommitted state. */
	ALTERQUEUE_ERROR				/* Internal error. */
} ResAlterQueueResult;

/*
 * Functions in resqueue.c
 */
extern LockAcquireResult ResLockAcquire(LOCKTAG *locktag, 
										ResPortalIncrement *incrementSet);
extern bool				ResLockRelease(LOCKTAG *locktag, uint32 resPortalId);
extern int				ResLockCheckLimit(LOCK *lock, PROCLOCK *proclock, 
										  ResPortalIncrement *incrementSet,
										  bool increment);
extern ResQueue			GetResQueueFromLock(LOCK *lock);

extern void				ResProcLockRemoveSelfAndWakeup(LOCK *lock);
extern PGPROC 			*ResProcWakeup(PGPROC *proc, int waitStatus);
extern void				ResRemoveFromWaitQueue(PGPROC *proc, uint32 hashcode);
extern bool				ResCheckSelfDeadLock(LOCK *lock, PROCLOCK *proclock, ResPortalIncrement *incSet);

extern ResPortalIncrement	*ResIncrementFind(ResPortalTag *portaltag);

extern bool					ResPortalIncrementHashTableInit(void);

extern bool 			ResQueueHashTableInit(void);
extern ResQueue			ResQueueHashNew(Oid queueid);
extern ResQueue			ResQueueHashFind(Oid queueid);
extern bool				ResQueueHashRemove(Oid queueid);


/*
 * Functions in resscheduler.c
 */
extern Size	ResSchedulerShmemSize(void);
extern Size ResPortalIncrementShmemSize(void);
extern void	InitResScheduler(void);
extern void InitResPortalIncrementHash(void);
extern void	InitResQueues(void);
extern bool ResCreateQueue(Oid queueid, Cost limits[NUM_RES_LIMIT_TYPES], 
						   bool overcommit, float4 ignorelimit);
extern ResAlterQueueResult ResAlterQueue(Oid queueid, 
						   Cost limits[NUM_RES_LIMIT_TYPES], 
						  bool overcommit, float4 ignorelimit);
extern bool ResDestroyQueue(Oid queueid);

extern bool ResLockPortal(Portal portal, QueryDesc *qDesc);
extern void ResUnLockPortal(Portal portal);

extern void ResCheckPortalType(Portal portal);
extern Oid	GetResQueueForRole(Oid roleid);
extern Oid	GetResQueueId(void);
extern Oid	GetResQueueIdForName(char *name);
extern void SetResQueueId(void);
extern uint32 ResCreatePortalId(const char *name);
extern void AtCommit_ResScheduler(void);
extern void AtAbort_ResScheduler(void);
extern void ResHandleUtilityStmt(Portal portal, Node *stmt);
extern bool ResLockUtilityPortal(Portal portal, float4 ignoreCostLimit);

/**
 * Assert that the in-memory state matches the catalog table.
 */
#ifdef USE_ASSERT_CHECKING
extern void AssertMemoryLimitsMatch(void);
#endif

#endif   /* RESSCHEDULER_H */
