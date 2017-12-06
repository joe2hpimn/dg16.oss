/*-------------------------------------------------------------------------
 *
 * xact.h
 *	  postgres transaction system definitions
 *
 *
 * Portions Copyright (c) 1996-2009, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $PostgreSQL: pgsql/src/include/access/xact.h,v 1.83 2006/07/11 18:26:11 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef XACT_H
#define XACT_H

#include "access/xlog.h"
#include "nodes/pg_list.h"
#include "storage/relfilenode.h"
#include "utils/timestamp.h"
#include "access/persistentendxactrec.h"

#include "cdb/cdbpublic.h"

/*
 * Xact isolation levels
 */
#define XACT_READ_UNCOMMITTED	0
#define XACT_READ_COMMITTED		1
#define XACT_REPEATABLE_READ	2
#define XACT_SERIALIZABLE		3

extern int	DefaultXactIsoLevel;
extern int	XactIsoLevel;

/*
 * We only implement two isolation levels internally.  This macro should
 * be used to check which one is selected.
 */
#define IsXactIsoLevelSerializable (XactIsoLevel >= XACT_REPEATABLE_READ)

/* Xact read-only state */
extern bool DefaultXactReadOnly;
extern bool XactReadOnly;

/*
 *	start- and end-of-transaction callbacks for dynamically loaded modules
 */
typedef enum
{
	XACT_EVENT_COMMIT,
	XACT_EVENT_ABORT,
	XACT_EVENT_PREPARE
} XactEvent;

typedef void (*XactCallback) (XactEvent event, void *arg);

typedef enum
{
	SUBXACT_EVENT_START_SUB,
	SUBXACT_EVENT_COMMIT_SUB,
	SUBXACT_EVENT_ABORT_SUB
} SubXactEvent;

typedef void (*SubXactCallback) (SubXactEvent event, SubTransactionId mySubid,
									SubTransactionId parentSubid, void *arg);


/* ----------------
 *		transaction-related XLOG entries
 * ----------------
 */

/*
 * XLOG allows to store some information in high 4 bits of log
 * record xl_info field
 */
#define XLOG_XACT_COMMIT			0x00
#define XLOG_XACT_PREPARE			0x10
#define XLOG_XACT_ABORT				0x20
#define XLOG_XACT_COMMIT_PREPARED	0x30
#define XLOG_XACT_ABORT_PREPARED	0x40
#define XLOG_XACT_DISTRIBUTED_COMMIT 0x50
#define XLOG_XACT_DISTRIBUTED_FORGET 0x60

typedef struct xl_xact_commit
{
	time_t		xtime;

	int16		persistentCommitObjectCount;	
								/* number of PersistentEndXactRec style objects */

	int			nsubxacts;		/* number of subtransaction XIDs */

	/* PersistentEndXactRec style objects for commit */
	uint8 data[0];		/* VARIABLE LENGTH ARRAY */

	/* ARRAY OF COMMITTED SUBTRANSACTION XIDs FOLLOWS */
} xl_xact_commit;

#define MinSizeOfXactCommit offsetof(xl_xact_commit, data)

typedef struct xl_xact_abort
{
	time_t		xtime;

	int16		persistentAbortObjectCount;	
								/* number of PersistentEndXactRec style objects */

	int			nsubxacts;		/* number of subtransaction XIDs */
	
	/* PersistentEndXactRec style objects for abort */
	uint8 data[0];		/* VARIABLE LENGTH ARRAY */
	
	/* ARRAY OF COMMITTED SUBTRANSACTION XIDs FOLLOWS */
} xl_xact_abort;

#define MinSizeOfXactAbort offsetof(xl_xact_abort, data)

/*
 * COMMIT_PREPARED and ABORT_PREPARED are identical to COMMIT/ABORT records
 * except that we have to store the XID of the prepared transaction explicitly
 * --- the XID in the record header will be for the transaction doing the
 * COMMIT PREPARED or ABORT PREPARED command.
 */

typedef struct xl_xact_commit_prepared
{
	TransactionId xid;			/* XID of prepared xact */
	xl_xact_commit crec;		/* COMMIT record */
	/* MORE DATA FOLLOWS AT END OF STRUCT */
} xl_xact_commit_prepared;

#define MinSizeOfXactCommitPrepared offsetof(xl_xact_commit_prepared, crec.data)

typedef struct xl_xact_abort_prepared
{
	TransactionId xid;			/* XID of prepared xact */
	xl_xact_abort arec;			/* ABORT record */
	/* MORE DATA FOLLOWS AT END OF STRUCT */
} xl_xact_abort_prepared;

#define MinSizeOfXactAbortPrepared offsetof(xl_xact_abort_prepared, arec.data)

/* 
 * xl_xact_distributed_forget - moved to cdb/cdbtm.h 
 */
typedef struct xl_xact_distributed_forget
{
	TMGXACT_LOG gxact_log;
} xl_xact_distributed_forget;

/*
 * Maximum number of subtransactions per transaction allowed under MPP until
 * we can move away from a static length SharedSnapshot structure.
 */
#define MaxGpSavePoints 100

typedef enum
{
	XACT_INFOKIND_NONE = 0,
	XACT_INFOKIND_COMMIT,
	XACT_INFOKIND_ABORT,
	XACT_INFOKIND_PREPARE
} XactInfoKind;

typedef struct XidBuffer
{
	uint32          max_bufs;
	uint32          actual_bufs;
	uint32          last_buf_cnt;
	TransactionId   **ids_buf;
} XidBuffer;

extern XidBuffer subxbuf;
extern File subxip_file;

/* ----------------
 *		extern definitions
 * ----------------
 */

/* Greenplum Database specific */ 
extern char *XactInfoKind_Name(
	const XactInfoKind		kind);
extern void SetSharedTransactionId(void);
extern void SetSharedTransactionId_reader(TransactionId xid, CommandId cid);
extern void SetXactSeqXlog(void);
extern bool IsTransactionState(void);
extern bool IsAbortInProgress(void);
extern bool IsAbortedTransactionBlockState(void);
extern void GetAllTransactionXids(
	DistributedTransactionId	*distribXid,
	TransactionId				*localXid,
	TransactionId				*subXid);
extern TransactionId GetTopTransactionId(void);
extern TransactionId GetCurrentTransactionId(void);
extern TransactionId GetCurrentTransactionIdIfAny(void);
extern SubTransactionId GetCurrentSubTransactionId(void);
extern CommandId GetCurrentCommandId(void);
extern TimestampTz GetCurrentTransactionStartTimestamp(void);
extern TimestampTz GetCurrentStatementStartTimestamp(void);
extern TimestampTz GetCurrentTransactionStopTimestamp(void);
extern void SetCurrentStatementStartTimestamp(void);
extern void SetCurrentStatementStartTimestampToMaster(TimestampTz masterTime);
extern int	GetCurrentTransactionNestLevel(void);
extern bool TransactionIdIsCurrentTransactionId(TransactionId xid);
extern void CommandCounterIncrement(void);
extern void StartTransactionCommand(void);
extern void CommitTransactionCommand(void);
extern void AbortCurrentTransaction(void);
extern void BeginTransactionBlock(void);
extern bool EndTransactionBlock(void);
extern bool PrepareTransactionBlock(char *gid);
extern void UserAbortTransactionBlock(void);
extern void ReleaseSavepoint(List *options);
extern void DefineSavepoint(char *name);
extern void DefineDispatchSavepoint(char *name);
extern void RollbackToSavepoint(List *options);
extern void BeginInternalSubTransaction(char *name);
extern void ReleaseCurrentSubTransaction(void);
extern void RollbackAndReleaseCurrentSubTransaction(void);
extern void TransactionInformationQEWriter(DistributedTransactionId *QEDistributedTransactionId, CommandId *QECommandId, bool *QEDirty);
extern bool IsSubTransaction(void);
extern bool IsTransactionBlock(void);
extern bool IsTransactionOrTransactionBlock(void);
extern bool IsTransactionDirty(void);
extern void ExecutorMarkTransactionUsesSequences(void);
extern void ExecutorMarkTransactionDoesWrites(void);
extern bool ExecutorSaysTransactionDoesWrites(void);
extern char TransactionBlockStatusCode(void);
extern void AbortOutOfAnyTransaction(void);
extern void PreventTransactionChain(void *stmtNode, const char *stmtType);
extern void RequireTransactionChain(void *stmtNode, const char *stmtType);
extern bool IsInTransactionChain(void *stmtNode);
extern void RegisterXactCallback(XactCallback callback, void *arg);
extern void UnregisterXactCallback(XactCallback callback, void *arg);
extern void RegisterXactCallbackOnce(XactCallback callback, void *arg);
extern void UnregisterXactCallbackOnce(XactCallback callback, void *arg);
extern void RegisterSubXactCallback(SubXactCallback callback, void *arg);
extern void UnregisterSubXactCallback(SubXactCallback callback, void *arg);

extern void RecordTransactionCommit(void);
extern void RecordDistributedForgetCommitted(struct TMGXACT_LOG *gxact_log);
extern bool RecordCrashTransactionAbortRecord(
	TransactionId				xid,
	PersistentEndXactRecObjects *persistentAbortObjects);

extern int	xactGetCommittedChildren(TransactionId **ptr);

extern void xact_redo(XLogRecPtr beginLoc, XLogRecPtr lsn, XLogRecord *record);
extern bool xact_redo_get_info(
		XLogRecord					*record,
		XactInfoKind				*infoKind,
		TransactionId				*xid,
		PersistentEndXactRecObjects *persistentObjects,
		TransactionId				**subXids,
		int 						*subXidCount);
extern void xact_desc(StringInfo buf, XLogRecPtr beginLoc, XLogRecord *record);
extern const char *IsoLevelAsUpperString(int IsoLevel);

#ifdef WATCH_VISIBILITY_IN_ACTION
extern char* WatchCurrentTransactionString(void);
#endif

extern bool FindXidInXidBuffer(XidBuffer *xidbuf,
			       TransactionId xid, uint32 *cnt, int32 *index);
extern void AddSortedToXidBuffer(XidBuffer *xidbuf, TransactionId *xids,
                                 uint32 cnt);
extern void ResetXidBuffer(XidBuffer *xidbuf);
extern TransactionId GetLastTransactionIdFromSnapshot(DistributedTransactionId);

void ShowSubtransactionsForSharedSnapshot(void);
void GetSubXidsInXidBuffer(void);
void UpdateSubtransactionsInSharedSnapshot(DistributedTransactionId dxid);

extern void ClearTransactionFromPgProc_UnderLock(void);

#endif   /* XACT_H */
