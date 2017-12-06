/*-------------------------------------------------------------------------
 *
 * xlog.c
 *		PostgreSQL transaction log manager
 *
 *
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $PostgreSQL: pgsql/src/backend/access/transam/xlog.c,v 1.258.2.3 2008/04/17 00:00:00 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <ctype.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "access/clog.h"
#include "access/heapam.h"
#include "access/multixact.h"
#include "access/distributedlog.h"
#include "access/subtrans.h"
#include "access/transam.h"
#include "access/twophase.h"
#include "access/xact.h"
#include "access/xlog_internal.h"
#include "access/xlogmm.h"
#include "access/xlogutils.h"
#include "catalog/catalog.h"
#include "catalog/catversion.h"
#include "catalog/pg_control.h"
#include "catalog/pg_type.h"
#include "catalog/pg_database.h"
#include "catalog/pg_tablespace.h"
#include "executor/spi.h"
#include "funcapi.h"
#include "libpq/pqsignal.h"
#include "libpq/hba.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "postmaster/postmaster.h"
#include "postmaster/checkpoint.h"
#include "storage/latch.h"
#include "storage/proc.h"
#include "storage/bufpage.h"
#include "storage/fd.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/pmsignal.h"
#include "storage/procarray.h"
#include "storage/spin.h"
#include "utils/builtins.h"
#include "utils/faultinjector.h"
#include "utils/flatfiles.h"
#include "utils/pg_locale.h"
#include "utils/nabstime.h"
#include "utils/guc.h"
#include "utils/ps_status.h"
#include "utils/resscheduler.h"
#include "pg_trace.h"
#include "utils/catcache.h"
#include "utils/memutils.h"
#include "utils/syscache.h"
#include "utils/pg_crc.h"
#include "replication/walreceiver.h"
#include "replication/walsender.h"
#include "storage/backendid.h"
#include "storage/sinvaladt.h"

#include "cdb/cdbtm.h"
#include "cdb/cdbfilerep.h"
#include "cdb/cdbfilerepresyncmanager.h"
#include "cdb/cdbvars.h"
#include "cdb/cdbpersistentrelation.h"
#include "cdb/cdbmirroredflatfile.h"
#include "cdb/cdbpersistentrecovery.h"
#include "cdb/cdbresynchronizechangetracking.h"
#include "cdb/cdbpersistentfilesysobj.h"
#include "cdb/cdbpersistentcheck.h"
#include "cdb/cdbfilerep.h"
#include "postmaster/primary_mirror_mode.h"
#include "utils/elog.h"

/*
 *	Because O_DIRECT bypasses the kernel buffers, and because we never
 *	read those buffers except during crash recovery, it is a win to use
 *	it in all cases where we sync on each write().	We could allow O_DIRECT
 *	with fsync(), but because skipping the kernel buffer forces writes out
 *	quickly, it seems best just to use it for O_SYNC.  It is hard to imagine
 *	how fsync() could be a win for O_DIRECT compared to O_SYNC and O_DIRECT.
 *	Also, O_DIRECT is never enough to force data to the drives, it merely
 *	tries to bypass the kernel cache, so we still need O_SYNC or fsync().
 */

/*
 * This chunk of hackery attempts to determine which file sync methods
 * are available on the current platform, and to choose an appropriate
 * default method.	We assume that fsync() is always available, and that
 * configure determined whether fdatasync() is.
 */
#if defined(O_SYNC)
#define BARE_OPEN_SYNC_FLAG		O_SYNC
#elif defined(O_FSYNC)
#define BARE_OPEN_SYNC_FLAG		O_FSYNC
#endif
#ifdef BARE_OPEN_SYNC_FLAG
#define OPEN_SYNC_FLAG			(BARE_OPEN_SYNC_FLAG | PG_O_DIRECT)
#endif

#if defined(O_DSYNC)
#if defined(OPEN_SYNC_FLAG)
/* O_DSYNC is distinct? */
#if O_DSYNC != BARE_OPEN_SYNC_FLAG
#define OPEN_DATASYNC_FLAG		(O_DSYNC | PG_O_DIRECT)
#endif
#else							/* !defined(OPEN_SYNC_FLAG) */
/* Win32 only has O_DSYNC */
#define OPEN_DATASYNC_FLAG		(O_DSYNC | PG_O_DIRECT)
#endif
#endif

/*
 * We don't want the default for Solaris to be OPEN_DATASYNC, because
 * (for some reason) it is absurdly slow.
 */
#if !defined(pg_on_solaris) && defined(OPEN_DATASYNC_FLAG)
#define DEFAULT_SYNC_METHOD_STR "open_datasync"
#define DEFAULT_SYNC_METHOD		SYNC_METHOD_OPEN
#define DEFAULT_SYNC_FLAGBIT	OPEN_DATASYNC_FLAG
#elif defined(HAVE_FDATASYNC)
#define DEFAULT_SYNC_METHOD_STR "fdatasync"
#define DEFAULT_SYNC_METHOD		SYNC_METHOD_FDATASYNC
#define DEFAULT_SYNC_FLAGBIT	0
#elif defined(HAVE_FSYNC_WRITETHROUGH_ONLY)
#define DEFAULT_SYNC_METHOD_STR "fsync_writethrough"
#define DEFAULT_SYNC_METHOD		SYNC_METHOD_FSYNC_WRITETHROUGH
#define DEFAULT_SYNC_FLAGBIT	0
#else
#define DEFAULT_SYNC_METHOD_STR "fsync"
#define DEFAULT_SYNC_METHOD		SYNC_METHOD_FSYNC
#define DEFAULT_SYNC_FLAGBIT	0
#endif


/*
 * Limitation of buffer-alignment for direct IO depends on OS and filesystem,
 * but XLOG_BLCKSZ is assumed to be enough for it.
 */
#ifdef O_DIRECT
#define ALIGNOF_XLOG_BUFFER		XLOG_BLCKSZ
#else
#define ALIGNOF_XLOG_BUFFER		ALIGNOF_BUFFER
#endif


/* File path names (all relative to $PGDATA) */
#define RECOVERY_COMMAND_FILE	"recovery.conf"
#define RECOVERY_COMMAND_DONE	"recovery.done"
#define PROMOTE_SIGNAL_FILE "promote"


/* User-settable parameters */
int			CheckPointSegments = 3;
int			XLOGbuffers = 8;
int			XLogArchiveTimeout = 0;
char	   *XLogArchiveCommand = NULL;
char	   *XLOG_sync_method = NULL;
const char	XLOG_sync_method_default[] = DEFAULT_SYNC_METHOD_STR;
bool		fullPageWrites = true;

#ifdef WAL_DEBUG
bool		XLOG_DEBUG = false;
#endif

/*
 * XLOGfileslop is used in the code as the allowed "fuzz" in the number of
 * preallocated XLOG segments --- we try to have at least XLOGfiles advance
 * segments but no more than XLOGfileslop segments.  This could
 * be made a separate GUC variable, but at present I think it's sufficient
 * to hardwire it as 2*CheckPointSegments+1.  Under normal conditions, a
 * checkpoint will free no more than 2*CheckPointSegments log segments, and
 * we want to recycle all of them; the +1 allows boundary cases to happen
 * without wasting a delete/create-segment cycle.
 */

#define XLOGfileslop	(2*CheckPointSegments + 1)


/* these are derived from XLOG_sync_method by assign_xlog_sync_method */
int			sync_method = DEFAULT_SYNC_METHOD;
static int	open_sync_bit = DEFAULT_SYNC_FLAGBIT;

/*
 * walreceiver process receives xlog data from walsender process.
 * It needs to write the xlog data as soon as it receives and the amount it receives.
 * As the amount of data received by it to write cannot be guaranteed to be
 * OS/FS block size aligned, should never use O_DIRECT for the same.
 * Also, as code is not expecting O_DIRECT to be used for xlog writes on walreceiver,
 * the buffer pointer to perform xlog writes is not made usre to be OS/FS blocks size aligned.
 */
#define XLOG_SYNC_BIT  (enableFsync && (MyAuxProcType != WalReceiverProcess) ? \
						open_sync_bit : 0)

bool am_startup = false;

/*
 * ThisTimeLineID will be same in all backends --- it identifies current
 * WAL timeline for the database system.
 */
TimeLineID	ThisTimeLineID = 0;

/* Are we doing recovery from XLOG */
bool		InRecovery = false;

/*
 * Local copy of SharedRecoveryInProgress variable. True actually means "not
 * known, need to check the shared state".
 */
static bool LocalRecoveryInProgress = true;

/* Was the last xlog file restored from archive, or local? */
static bool restoredFromArchive = false;

/* options taken from recovery.conf */
static bool recoveryTarget = false;
static bool recoveryTargetExact = false;
static bool recoveryTargetInclusive = true;
static TransactionId recoveryTargetXid;
static time_t recoveryTargetTime;

/* options taken from recovery.conf for XLOG streaming */
static bool StandbyModeRequested = false;
static char *PrimaryConnInfo = NULL;

/* are we currently in standby mode? */
bool StandbyMode = false;

/* if recoveryStopsHere returns true, it saves actual stop xid/time here */
static TransactionId recoveryStopXid;
static time_t recoveryStopTime;
static bool recoveryStopAfter;

/*
 * During normal operation, the only timeline we care about is ThisTimeLineID.
 * During recovery, however, things are more complicated.  To simplify life
 * for rmgr code, we keep ThisTimeLineID set to the "current" timeline as we
 * scan through the WAL history (that is, it is the line that was active when
 * the currently-scanned WAL record was generated).  We also need these
 * timeline values:
 *
 * recoveryTargetTLI: the desired timeline that we want to end in.
 *
 * expectedTLIs: an integer list of recoveryTargetTLI and the TLIs of
 * its known parents, newest first (so recoveryTargetTLI is always the
 * first list member).	Only these TLIs are expected to be seen in the WAL
 * segments we read, and indeed only these TLIs will be considered as
 * candidate WAL files to open at all.
 *
 * curFileTLI: the TLI appearing in the name of the current input WAL file.
 * (This is not necessarily the same as ThisTimeLineID, because we could
 * be scanning data that was copied from an ancestor timeline when the current
 * file was created.)  During a sequential scan we do not allow this value
 * to decrease.
 */
static TimeLineID recoveryTargetTLI;
List *expectedTLIs;
static TimeLineID curFileTLI;

/*
 * MyLastRecPtr points to the start of the last XLOG record inserted by the
 * current transaction.  If MyLastRecPtr.xrecoff == 0, then the current
 * xact hasn't yet inserted any transaction-controlled XLOG records.
 *
 * Note that XLOG records inserted outside transaction control are not
 * reflected into MyLastRecPtr.  They do, however, cause MyXactMadeXLogEntry
 * to be set true.	The latter can be used to test whether the current xact
 * made any loggable changes (including out-of-xact changes, such as
 * sequence updates).
 *
 * When we insert/update/delete a tuple in a temporary relation, we do not
 * make any XLOG record, since we don't care about recovering the state of
 * the temp rel after a crash.	However, we will still need to remember
 * whether our transaction committed or aborted in that case.  So, we must
 * set MyXactMadeTempRelUpdate true to indicate that the XID will be of
 * interest later.
 */
XLogRecPtr	MyLastRecPtr = {0, 0};

bool		MyXactMadeXLogEntry = false;

bool		MyXactMadeTempRelUpdate = false;

/*
 * ProcLastRecPtr points to the start of the last XLOG record inserted by the
 * current backend.  It is updated for all inserts, transaction-controlled
 * or not.	ProcLastRecEnd is similar but points to end+1 of last record.
 */
static XLogRecPtr ProcLastRecPtr = {0, 0};

XLogRecPtr	ProcLastRecEnd = {0, 0};

static uint32 ProcLastRecTotalLen = 0;

static uint32 ProcLastRecDataLen = 0;

/*
 * RedoRecPtr is this backend's local copy of the REDO record pointer
 * (which is almost but not quite the same as a pointer to the most recent
 * CHECKPOINT record).	We update this from the shared-memory copy,
 * XLogCtl->Insert.RedoRecPtr, whenever we can safely do so (ie, when we
 * hold the Insert lock).  See XLogInsert for details.	We are also allowed
 * to update from XLogCtl->Insert.RedoRecPtr if we hold the info_lck;
 * see GetRedoRecPtr.  A freshly spawned backend obtains the value during
 * InitXLOGAccess.
 */
static XLogRecPtr RedoRecPtr;

/*
 * RedoStartLSN points to the checkpoint's REDO location which is specified
 * in a backup label file, backup history file or control file. In standby
 * mode, XLOG streaming usually starts from the position where an invalid
 * record was found. But if we fail to read even the initial checkpoint
 * record, we use the REDO location instead of the checkpoint location as
 * the start position of XLOG streaming. Otherwise we would have to jump
 * backwards to the REDO location after reading the checkpoint record,
 * because the REDO record can precede the checkpoint record.
 */
static XLogRecPtr RedoStartLSN = {0, 0};

/*----------
 * Shared-memory data structures for XLOG control
 *
 * LogwrtRqst indicates a byte position that we need to write and/or fsync
 * the log up to (all records before that point must be written or fsynced).
 * LogwrtResult indicates the byte positions we have already written/fsynced.
 * These structs are identical but are declared separately to indicate their
 * slightly different functions.
 *
 * We do a lot of pushups to minimize the amount of access to lockable
 * shared memory values.  There are actually three shared-memory copies of
 * LogwrtResult, plus one unshared copy in each backend.  Here's how it works:
 *		XLogCtl->LogwrtResult is protected by info_lck
 *		XLogCtl->Write.LogwrtResult is protected by WALWriteLock
 *		XLogCtl->Insert.LogwrtResult is protected by WALInsertLock
 * One must hold the associated lock to read or write any of these, but
 * of course no lock is needed to read/write the unshared LogwrtResult.
 *
 * XLogCtl->LogwrtResult and XLogCtl->Write.LogwrtResult are both "always
 * right", since both are updated by a write or flush operation before
 * it releases WALWriteLock.  The point of keeping XLogCtl->Write.LogwrtResult
 * is that it can be examined/modified by code that already holds WALWriteLock
 * without needing to grab info_lck as well.
 *
 * XLogCtl->Insert.LogwrtResult may lag behind the reality of the other two,
 * but is updated when convenient.	Again, it exists for the convenience of
 * code that is already holding WALInsertLock but not the other locks.
 *
 * The unshared LogwrtResult may lag behind any or all of these, and again
 * is updated when convenient.
 *
 * The request bookkeeping is simpler: there is a shared XLogCtl->LogwrtRqst
 * (protected by info_lck), but we don't need to cache any copies of it.
 *
 * Note that this all works because the request and result positions can only
 * advance forward, never back up, and so we can easily determine which of two
 * values is "more up to date".
 *
 * info_lck is only held long enough to read/update the protected variables,
 * so it's a plain spinlock.  The other locks are held longer (potentially
 * over I/O operations), so we use LWLocks for them.  These locks are:
 *
 * WALInsertLock: must be held to insert a record into the WAL buffers.
 *
 * WALWriteLock: must be held to write WAL buffers to disk (XLogWrite or
 * XLogFlush).
 *
 * ControlFileLock: must be held to read/update control file or create
 * new log file.
 *
 * CheckpointLock: must be held to do a checkpoint (ensures only one
 * checkpointer at a time; currently, with all checkpoints done by the
 * bgwriter, this is just pro forma).
 *
 *----------
 */

typedef struct XLogwrtRqst
{
	XLogRecPtr	Write;			/* last byte + 1 to write out */
	XLogRecPtr	Flush;			/* last byte + 1 to flush */
} XLogwrtRqst;

typedef struct XLogwrtResult
{
	XLogRecPtr	Write;			/* last byte + 1 written out */
	XLogRecPtr	Flush;			/* last byte + 1 flushed */
} XLogwrtResult;

/*
 * Shared state data for XLogInsert.
 */
typedef struct XLogCtlInsert
{
	XLogwrtResult LogwrtResult; /* a recent value of LogwrtResult */
	XLogRecPtr	PrevRecord;		/* start of previously-inserted record */
	int			curridx;		/* current block index in cache */
	XLogPageHeader currpage;	/* points to header of block in cache */
	char	   *currpos;		/* current insertion point in cache */
	XLogRecPtr	RedoRecPtr;		/* current redo point for insertions */
	bool		forcePageWrites;	/* forcing full-page writes for PITR? */

	/*
	 * exclusiveBackup is true if a backup started with pg_start_backup() is
	 * in progress, and nonExclusiveBackups is a counter indicating the number
	 * of streaming base backups currently in progress. forcePageWrites is set
	 * to true when either of these is non-zero. lastBackupStart is the latest
	 * checkpoint redo location used as a starting point for an online backup.
	 */
	bool		exclusiveBackup;
	int			nonExclusiveBackups;
	XLogRecPtr	lastBackupStart;
} XLogCtlInsert;

/*
 * Shared state data for XLogWrite/XLogFlush.
 */
typedef struct XLogCtlWrite
{
	XLogwrtResult LogwrtResult; /* current value of LogwrtResult */
	int			curridx;		/* cache index of next block to write */
	time_t		lastSegSwitchTime;		/* time of last xlog segment switch */
} XLogCtlWrite;

/*
 * Total shared-memory state for XLOG.
 */
typedef struct XLogCtlData
{
	/* Protected by WALInsertLock: */
	XLogCtlInsert Insert;

	/* Protected by info_lck: */
	XLogwrtRqst LogwrtRqst;
	XLogwrtResult LogwrtResult;
	uint32		ckptXidEpoch;	/* nextXID & epoch of latest checkpoint */
	TransactionId ckptXid;
	uint32		lastRemovedLog; /* latest removed/recycled XLOG segment */
	uint32		lastRemovedSeg;

	/* Protected by WALWriteLock: */
	XLogCtlWrite Write;

	/* Protected by ChangeTrackingTransitionLock. */
	XLogRecPtr	lastChangeTrackingEndLoc;
								/*
								 * End + 1 of the last XLOG record inserted and
 								 * (possible) change tracked.
 								 */

	/* Resynchronize */
	bool		sendingResynchronizeTransitionMsg;
	slock_t		resynchronize_lck;		/* locks shared variables shown above */

	/*
	 * These values do not change after startup, although the pointed-to pages
	 * and xlblocks values certainly do.  Permission to read/write the pages
	 * and xlblocks values depends on WALInsertLock and WALWriteLock.
	 */
	char	   *pages;			/* buffers for unwritten XLOG pages */
	XLogRecPtr *xlblocks;		/* 1st byte ptr-s + XLOG_BLCKSZ */
	Size		XLogCacheByte;	/* # bytes in xlog buffers */
	int			XLogCacheBlck;	/* highest allocated xlog buffer index */
	TimeLineID	ThisTimeLineID;

	/*
	 * SharedRecoveryInProgress indicates if we're still in crash or standby
	 * mode. Currently use of this variable is very limited for e.g. WAL receiver
	 * Protected by info_lck.
	 */
	bool		SharedRecoveryInProgress;

	/*
	 * recoveryWakeupLatch is used to wake up the startup process to continue
	 * WAL replay, if it is waiting for WAL to arrive or failover trigger file
	 * to appear.
	 */
	Latch		recoveryWakeupLatch;

	/*
	 * the standby's dbid when it runs.  Used in mmxlog to emit standby filepath.
	 * Protected by info_lck
	 */
	int16		standbyDbid;

	slock_t		info_lck;		/* locks shared variables shown above */

	/*
	 * Save the location of the last checkpoint record to enable supressing
	 * unnecessary checkpoint records -- when no new xlog has been written
	 * since the last one.
	 */
	bool 		haveLastCheckpointLoc;
	XLogRecPtr	lastCheckpointLoc;
	XLogRecPtr	lastCheckpointEndLoc;

	/*
	 * lastReplayedEndRecPtr points to end+1 of the last record successfully
	 * replayed.
	 */
	XLogRecPtr	lastReplayedEndRecPtr;

	/* current effective recovery target timeline */
	TimeLineID	RecoveryTargetTLI;

	/*
	 * timestamp of when we started replaying the current chunk of WAL data,
	 * only relevant for replication or archive recovery
	 */
	TimestampTz currentChunkStartTime;

	/*
	 * Save the redo range used in Pass 1 recovery so it can be used in subsequent passes.
	 */
	bool		multipleRecoveryPassesNeeded;
	XLogRecPtr	pass1StartLoc;
	XLogRecPtr	pass1LastLoc;
	XLogRecPtr	pass1LastCheckpointLoc;

	/*=================Pass 4 PersistentTable-Cat verification================*/
	/*If true integrity checks will be performed in Pass4.*/
	bool		integrityCheckNeeded;

	/*
	 * Currently set database and tablespace to be verified for database specific
	 * PT-Cat verification in Pass4. These fields also act as implicit flags
	 * PT-Cat which indicate if there are any more databases to perform
	 * PT-Cat verifications checks on.
	 */
	Oid			currentDatabaseToVerify;
	Oid			tablespaceOfCurrentDatabaseToVerify;

	/*Indicates if pass4 PT-Cat verification checks passed*/
	bool		pass4_PTCatVerificationPassed;
	/*==========Pass 4 PersistentTable-Cat verification End===================*/

} XLogCtlData;

static XLogCtlData *XLogCtl = NULL;

/*
 * We maintain an image of pg_control in shared memory.
 */
static ControlFileData *ControlFile = NULL;

typedef struct ControlFileWatch
{
	bool		watcherInitialized;
	XLogRecPtr	current_checkPointLoc;		/* current last check point record ptr */
	XLogRecPtr	current_prevCheckPointLoc;  /* current previous check point record ptr */
	XLogRecPtr	current_checkPointCopy_redo;
								/* current checkpointCopy value for
								 * next RecPtr available when we began to
								 * create CheckPoint (i.e. REDO start point) */

} ControlFileWatch;


/*
 * We keep the watcher in shared memory.
 */
static ControlFileWatch *ControlFileWatcher = NULL;

/*
 * Macros for managing XLogInsert state.  In most cases, the calling routine
 * has local copies of XLogCtl->Insert and/or XLogCtl->Insert->curridx,
 * so these are passed as parameters instead of being fetched via XLogCtl.
 */

/* Free space remaining in the current xlog page buffer */
#define INSERT_FREESPACE(Insert)  \
	(XLOG_BLCKSZ - ((Insert)->currpos - (char *) (Insert)->currpage))

/* Construct XLogRecPtr value for current insertion point */
#define INSERT_RECPTR(recptr,Insert,curridx)  \
	( \
	  (recptr).xlogid = XLogCtl->xlblocks[curridx].xlogid, \
	  (recptr).xrecoff = \
		XLogCtl->xlblocks[curridx].xrecoff - INSERT_FREESPACE(Insert) \
	)

#define PrevBufIdx(idx)		\
		(((idx) == 0) ? XLogCtl->XLogCacheBlck : ((idx) - 1))

#define NextBufIdx(idx)		\
		(((idx) == XLogCtl->XLogCacheBlck) ? 0 : ((idx) + 1))

/*
 * Private, possibly out-of-date copy of shared LogwrtResult.
 * See discussion above.
 */
static XLogwrtResult LogwrtResult = {{0, 0}, {0, 0}};

/*
 * Codes indicating where we got a WAL file from during recovery, or where
 * to attempt to get one.  These are chosen so that they can be OR'd together
 * in a bitmask state variable.
 */
#define XLOG_FROM_ARCHIVE		(1<<0)	/* Restored using restore_command */
#define XLOG_FROM_PG_XLOG		(1<<1)	/* Existing file in pg_xlog */
#define XLOG_FROM_STREAM		(1<<2)	/* Streamed from master */

/*
 * openLogFile is -1 or a kernel FD for an open log file segment.
 * When it's open, openLogOff is the current seek offset in the file.
 * openLogId/openLogSeg identify the segment.  These variables are only
 * used to write the XLOG, and so will normally refer to the active segment.
 */
static MirroredFlatFileOpen	mirroredLogFileOpen = MirroredFlatFileOpen_Init;
static uint32 openLogId = 0;
static uint32 openLogSeg = 0;
static uint32 openLogOff = 0;

/*
 * These variables are used similarly to the ones above, but for reading
 * the XLOG.  Note, however, that readOff generally represents the offset
 * of the page just read, not the seek position of the FD itself, which
 * will be just past that page.readLen indicates how much of the current
 * page has been read into readBuf, and readSource indicates where we got
 * the currently open file from.
 */
static int	readFile = -1;
static uint32 readId = 0;
static uint32 readSeg = 0;
static uint32 readOff = 0;
static uint32 readLen = 0;
static int	readSource = 0;		/* XLOG_FROM_* code */

/*
 * Keeps track of which sources we've tried to read the current WAL
 * record from and failed.
 */
static int	failedSources = 0;	/* OR of XLOG_FROM_* codes */

/*
 * These variables track when we last obtained some WAL data to process,
 * and where we got it from.  (XLogReceiptSource is initially the same as
 * readSource, but readSource gets reset to zero when we don't have data
 * to process right now.)
 */
static TimestampTz XLogReceiptTime = 0;
static int	XLogReceiptSource = 0;		/* XLOG_FROM_* code */

/* Buffer for currently read page (XLOG_BLCKSZ bytes) */
static char *readBuf = NULL;

/* Buffer for current ReadRecord result (expandable) */
static char *readRecordBuf = NULL;
static uint32 readRecordBufSize = 0;

/* State information for XLOG reading */
static XLogRecPtr ReadRecPtr;	/* start of last record read */
static XLogRecPtr EndRecPtr;	/* end+1 of last record read */
static XLogRecord *nextRecord = NULL;
static TimeLineID lastPageTLI = 0;
static TimeLineID lastSegmentTLI = 0;

static bool InRedo = false;

/* Aligning xlog mirrored buffer */
static int32 writeBufLen = 0;
static char	*writeBuf = NULL;

char	*writeBufAligned = NULL;

/*
 * Flag set by interrupt handlers for later service in the redo loop.
 */
static volatile sig_atomic_t got_SIGHUP = false;
static volatile sig_atomic_t shutdown_requested = false;
static volatile sig_atomic_t promote_triggered = false;

/*
 * Flag set when executing a restore command, to tell SIGTERM signal handler
 * that it's safe to just proc_exit.
 */
static volatile sig_atomic_t in_restore_command = false;

static void XLogArchiveNotify(const char *xlog);
static void XLogArchiveNotifySeg(uint32 log, uint32 seg);
static bool XLogArchiveCheckDone(const char *xlog);
static void XLogArchiveCleanup(const char *xlog);
static void exitArchiveRecovery(TimeLineID endTLI,
					uint32 endLogId, uint32 endLogSeg);
static bool recoveryStopsHere(XLogRecord *record, bool *includeThis);
static void CheckPointGuts(XLogRecPtr checkPointRedo);
static void Checkpoint_RecoveryPass(XLogRecPtr checkPointRedo);
static bool XLogCheckBuffer(XLogRecData *rdata, bool doPageWrites,
				XLogRecPtr *lsn, BkpBlock *bkpb);
static bool AdvanceXLInsertBuffer(bool new_segment);
static void XLogWrite(XLogwrtRqst WriteRqst, bool flexible, bool xlog_switch);
static void XLogFileInit(
			 MirroredFlatFileOpen *mirroredOpen,
			 uint32 log, uint32 seg,
			 bool *use_existent, bool use_lock);
static bool InstallXLogFileSegment(uint32 *log, uint32 *seg, char *tmppath,
					   bool find_free, int *max_advance,
					   bool use_lock, char *tmpsimpleFileName);
static void XLogFileOpen(
				MirroredFlatFileOpen *mirroredOpen,
				uint32 log,
				uint32 seg);

static bool StartupXLOG_Pass4_CheckIfAnyInDoubtPreparedTransactions(void);
static void StartupXLOG_Pass4_NonDBSpecificPTCatVerification(void);
static void StartupXLOG_Pass4_DBSpecificPTCatVerification(void);
static bool StartupXLOG_Pass4_GetDBForPTCatVerification(void);

static bool XLogPageRead(XLogRecPtr *RecPtr, int emode, bool fetching_ckpt,
			 bool randAccess);

static int	PreallocXlogFiles(XLogRecPtr endptr);
static void UpdateLastRemovedPtr(char *filename);
static void MoveOfflineLogs(uint32 log, uint32 seg, XLogRecPtr endptr,
				int *nsegsremoved, int *nsegsrecycled);
static void CleanupBackupHistory(void);
static bool ValidXLOGHeader(XLogPageHeader hdr, int emode, bool segmentonly);
static void UnpackCheckPointRecord(
	XLogRecord			*record,
	XLogRecPtr 			*location,
	TMGXACT_CHECKPOINT	**dtxCheckpoint,
	uint32				*dtxCheckpointLen,
	char				**masterMirroringCheckpoint,
	uint32				*masterMirroringCheckpointLen,
	int					errlevelMasterMirroring,
        prepared_transaction_agg_state  **ptas);

static bool existsTimeLineHistory(TimeLineID probeTLI);
static TimeLineID findNewestTimeLine(TimeLineID startTLI);
static void writeTimeLineHistory(TimeLineID newTLI, TimeLineID parentTLI,
					 TimeLineID endTLI,
					 uint32 endLogId, uint32 endLogSeg);
static void ControlFileWatcherSaveInitial(void);
static void ControlFileWatcherCheckForChange(void);
static bool XLogGetWriteAndFlushedLoc(XLogRecPtr *writeLoc, XLogRecPtr *flushedLoc);
static XLogRecPtr XLogInsert_Internal(RmgrId rmid, uint8 info, XLogRecData *rdata, TransactionId headerXid);
static void WriteControlFile(void);
static void ReadControlFile(void);
static char *str_time(pg_time_t tnow);
#ifdef suppress
static void issue_xlog_fsync(void);
#endif
static void pg_start_backup_callback(int code, Datum arg);
static bool read_backup_label(XLogRecPtr *checkPointLoc, bool *backupEndRequired);
static void ValidateXLOGDirectoryStructure(void);

/* New functions added for WAL replication */
static void SetCurrentChunkStartTime(TimestampTz xtime);
static int XLogFileReadAnyTLI(uint32 log, uint32 seg, int emode, int sources);
static void XLogProcessCheckpointRecord(XLogRecord *rec, XLogRecPtr loc);

typedef struct RedoErrorCallBack
{
	XLogRecPtr	location;

	XLogRecord 	*record;
} RedoErrorCallBack;

static void rm_redo_error_callback(void *arg);

static int XLogGetEof(XLogRecPtr *eofRecPtr);

static	int XLogFillZero(
				 uint32	logId,
				 uint32	seg,
				 uint32	startOffset,
				 uint32	endOffset);

static int XLogReconcileEofInternal(
							 XLogRecPtr	startLocation,
							 XLogRecPtr	endLocation);

void HandleStartupProcInterrupts(void);
static bool CheckForStandbyTrigger(void);

/*
 * Whether we need to always generate transaction log (XLOG), or if we can
 * bypass it and get better performance.
 *
 * For GPDB, we currently do not support XLogArchivingActive(), so we don't
 * use it as a condition.
 */
bool XLog_CanBypassWal(void)
{
	if (Debug_bulk_load_bypass_wal)
	{
		/*
		 * We need the XLOG to be transmitted to the standby master since
		 * it is not using FileRep technology yet.  Master also could skip
		 * some of the WAL operations for optimization when standby is not
		 * configured, but for now we lean towards safety.
		 */
		return GpIdentity.segindex != MASTER_CONTENT_ID;
	}
	else
	{
		return false;
	}
}

/*
 * For FileRep code that doesn't have the Bypass WAL logic yet.
 */
bool XLog_UnconvertedCanBypassWal(void)
{
	return false;
}

static char *XLogContiguousCopy(
	XLogRecord 		*record,

	XLogRecData 	*rdata)
{
	XLogRecData *rdt;
	int32 len;
	char *buffer;

	rdt = rdata;
	len = sizeof(XLogRecord);
	while (rdt != NULL)
	{
		if (rdt->data != NULL)
		{
			len += rdt->len;
		}
		rdt = rdt->next;
	}

	buffer = (char*)palloc(len);

	memcpy(buffer, record, sizeof(XLogRecord));
	rdt = rdata;
	len = sizeof(XLogRecord);
	while (rdt != NULL)
	{
		if (rdt->data != NULL)
		{
			memcpy(&buffer[len], rdt->data, rdt->len);
			len += rdt->len;
		}
		rdt = rdt->next;
	}

	return buffer;
}

/*
 * Insert an XLOG record having the specified RMID and info bytes,
 * with the body of the record being the data chunk(s) described by
 * the rdata chain (see xlog.h for notes about rdata).
 *
 * Returns XLOG pointer to end of record (beginning of next record).
 * This can be used as LSN for data pages affected by the logged action.
 * (LSN is the XLOG point up to which the XLOG must be flushed to disk
 * before the data page can be written out.  This implements the basic
 * WAL rule "write the log before the data".)
 *
 * NB: this routine feels free to scribble on the XLogRecData structs,
 * though not on the data they reference.  This is OK since the XLogRecData
 * structs are always just temporaries in the calling code.
 */
XLogRecPtr
XLogInsert(RmgrId rmid, uint8 info, XLogRecData *rdata)
{
	return XLogInsert_Internal(rmid, info, rdata, GetCurrentTransactionIdIfAny());
}

XLogRecPtr
XLogInsert_OverrideXid(RmgrId rmid, uint8 info, XLogRecData *rdata, TransactionId overrideXid)
{
	return XLogInsert_Internal(rmid, info, rdata, overrideXid);
}


static XLogRecPtr
XLogInsert_Internal(RmgrId rmid, uint8 info, XLogRecData *rdata, TransactionId headerXid)
{

	XLogCtlInsert *Insert = &XLogCtl->Insert;
	XLogRecord *record;
	XLogContRecord *contrecord;
	XLogRecPtr	RecPtr;
	XLogRecPtr	WriteRqst;
	uint32		freespace;
	int			curridx;
	XLogRecData *rdt;
	char 		*rdatabuf = NULL;
	Buffer		dtbuf[XLR_MAX_BKP_BLOCKS];
	bool		dtbuf_bkp[XLR_MAX_BKP_BLOCKS];
	BkpBlock	dtbuf_xlg[XLR_MAX_BKP_BLOCKS];
	XLogRecPtr	dtbuf_lsn[XLR_MAX_BKP_BLOCKS];
	XLogRecData dtbuf_rdt1[XLR_MAX_BKP_BLOCKS];
	XLogRecData dtbuf_rdt2[XLR_MAX_BKP_BLOCKS];
	XLogRecData dtbuf_rdt3[XLR_MAX_BKP_BLOCKS];
	pg_crc32	rdata_crc;
	uint32		len,
				write_len;
	unsigned	i;
	XLogwrtRqst LogwrtRqst;
	bool		updrqst;
	bool		doPageWrites;
	bool		isLogSwitch = (rmid == RM_XLOG_ID && info == XLOG_SWITCH);
	bool		no_tran = (rmid == RM_XLOG_ID);

	bool		rdata_iscopy = false;

    /* Safety check in case our assumption is ever broken. */
	/* NOTE: This is slightly modified from the one in xact.c -- the test for */
 	/* NOTE: seqXlogWrite is omitted... */
	/* NOTE: some local-only changes are OK */
 	if (Gp_role == GP_ROLE_EXECUTE && !Gp_is_writer)
 	{
 		/*
 	     * we better only do really minor things on the reader that result
 	     * in writing to the xlog here at commit.  for now sequences
 	     * should be the only one
 	     */
		if (DistributedTransactionContext == DTX_CONTEXT_LOCAL_ONLY)
		{
			/* MPP-1687: readers may under some circumstances extend the CLOG
			 * rmid == RM_CLOG_ID and info having CLOG_ZEROPAGE set */
			elog(LOG, "Reader qExec committing LOCAL_ONLY changes. (%d %d)", rmid, info);
		}
		else
		{
			/*
			 * We are allowing the QE Reader to write to support error tables.
			 */
			elog(DEBUG1, "Reader qExec writing changes. (%d %d)", rmid, info);
#ifdef nothing
			ereport(ERROR,
					(errmsg("Reader qExec had local changes to commit! (rmid = %u)",
							rmid),
					 errdetail("A Reader qExec tried to commit local changes.  "
							   "Only the single Writer qExec can do so. "),
					 errhint("This is most likely the result of a feature being turned "
							 "on that violates the single WRITER principle")));
#endif
		}
 	}

	if (info & XLR_INFO_MASK)
	{
		if ((info & XLR_INFO_MASK) != XLOG_NO_TRAN)
			elog(PANIC, "invalid xlog info mask %02X", (info & XLR_INFO_MASK));
		no_tran = true;
		info &= ~XLR_INFO_MASK;
	}

	/*
	 * In bootstrap mode, we don't actually log anything but XLOG resources;
	 * return a phony record pointer.
	 */
	if (IsBootstrapProcessingMode() && rmid != RM_XLOG_ID)
	{
		RecPtr.xlogid = 0;
		RecPtr.xrecoff = SizeOfXLogLongPHD;		/* start of 1st chkpt record */
		return RecPtr;
	}

	/*
	 * Here we scan the rdata chain, determine which buffers must be backed
	 * up, and compute the CRC values for the data.  Note that the record
	 * header isn't added into the CRC initially since we don't know the final
	 * length or info bits quite yet.  Thus, the CRC will represent the CRC of
	 * the whole record in the order "rdata, then backup blocks, then record
	 * header".
	 *
	 * We may have to loop back to here if a race condition is detected below.
	 * We could prevent the race by doing all this work while holding the
	 * insert lock, but it seems better to avoid doing CRC calculations while
	 * holding the lock.  This means we have to be careful about modifying the
	 * rdata chain until we know we aren't going to loop back again.  The only
	 * change we allow ourselves to make earlier is to set rdt->data = NULL in
	 * chain items we have decided we will have to back up the whole buffer
	 * for.  This is OK because we will certainly decide the same thing again
	 * for those items if we do it over; doing it here saves an extra pass
	 * over the chain later.
	 */
begin:;
	for (i = 0; i < XLR_MAX_BKP_BLOCKS; i++)
	{
		dtbuf[i] = InvalidBuffer;
		dtbuf_bkp[i] = false;
	}

	/*
	 * Decide if we need to do full-page writes in this XLOG record: true if
	 * full_page_writes is on or we have a PITR request for it.  Since we
	 * don't yet have the insert lock, forcePageWrites could change under us,
	 * but we'll recheck it once we have the lock.
	 */
	doPageWrites = fullPageWrites || Insert->forcePageWrites;

	rdata_crc = crc32cInit();
	len = 0;
	for (rdt = rdata;;)
	{
		if (rdt->buffer == InvalidBuffer)
		{
			/* Simple data, just include it */
			len += rdt->len;
			rdata_crc = crc32c(rdata_crc, rdt->data, rdt->len);
		}
		else
		{
			/* Find info for buffer */
			for (i = 0; i < XLR_MAX_BKP_BLOCKS; i++)
			{
				if (rdt->buffer == dtbuf[i])
				{
					/* Buffer already referenced by earlier chain item */
					if (dtbuf_bkp[i])
						rdt->data = NULL;
					else if (rdt->data)
					{
						len += rdt->len;
						rdata_crc = crc32c(rdata_crc, rdt->data, rdt->len);
					}
					break;
				}
				if (dtbuf[i] == InvalidBuffer)
				{
					/* OK, put it in this slot */
					dtbuf[i] = rdt->buffer;
					if (XLogCheckBuffer(rdt, doPageWrites,
										&(dtbuf_lsn[i]), &(dtbuf_xlg[i])))
					{
						dtbuf_bkp[i] = true;
						rdt->data = NULL;
					}
					else if (rdt->data)
					{
						len += rdt->len;
						rdata_crc = crc32c(rdata_crc, rdt->data, rdt->len);
					}
					break;
				}
			}
			if (i >= XLR_MAX_BKP_BLOCKS)
				elog(PANIC, "can backup at most %d blocks per xlog record",
					 XLR_MAX_BKP_BLOCKS);
		}
		/* Break out of loop when rdt points to last chain item */
		if (rdt->next == NULL)
			break;
		rdt = rdt->next;
	}

	/*
	 * Now add the backup block headers and data into the CRC
	 */
	for (i = 0; i < XLR_MAX_BKP_BLOCKS; i++)
	{
		if (dtbuf_bkp[i])
		{
			BkpBlock   *bkpb = &(dtbuf_xlg[i]);
			char	   *page;

			rdata_crc = crc32c(rdata_crc,
					   (char *) bkpb,
					   sizeof(BkpBlock));
			page = (char *) BufferGetBlock(dtbuf[i]);
			if (bkpb->hole_length == 0)
			{
				rdata_crc = crc32c(rdata_crc,
						   page,
						   BLCKSZ);
			}
			else
			{
				/* must skip the hole */
				rdata_crc = crc32c(rdata_crc,
						   page,
						   bkpb->hole_offset);
				rdata_crc = crc32c(rdata_crc,
						   page + (bkpb->hole_offset + bkpb->hole_length),
						   BLCKSZ - (bkpb->hole_offset + bkpb->hole_length));
			}
		}
	}

	/*
	 * NOTE: We disallow len == 0 because it provides a useful bit of extra
	 * error checking in ReadRecord.  This means that all callers of
	 * XLogInsert must supply at least some not-in-a-buffer data.  However, we
	 * make an exception for XLOG SWITCH records because we don't want them to
	 * ever cross a segment boundary.
	 */
	if (len == 0 && !isLogSwitch)
		elog(PANIC, "invalid xlog record length %u", len);

	START_CRIT_SECTION();

	/* update LogwrtResult before doing cache fill check */
	{
		/* use volatile pointer to prevent code rearrangement */
		volatile XLogCtlData *xlogctl = XLogCtl;

		SpinLockAcquire(&xlogctl->info_lck);
		LogwrtRqst = xlogctl->LogwrtRqst;
		LogwrtResult = xlogctl->LogwrtResult;
		SpinLockRelease(&xlogctl->info_lck);
	}

	/*
	 * If cache is half filled then try to acquire write lock and do
	 * XLogWrite. Ignore any fractional blocks in performing this check.
	 */
	LogwrtRqst.Write.xrecoff -= LogwrtRqst.Write.xrecoff % XLOG_BLCKSZ;
	if (LogwrtRqst.Write.xlogid != LogwrtResult.Write.xlogid ||
		(LogwrtRqst.Write.xrecoff >= LogwrtResult.Write.xrecoff +
		 XLogCtl->XLogCacheByte / 2))
	{
		if (LWLockConditionalAcquire(WALWriteLock, LW_EXCLUSIVE))
		{
			/*
			 * Since the amount of data we write here is completely optional
			 * anyway, tell XLogWrite it can be "flexible" and stop at a
			 * convenient boundary.  This allows writes triggered by this
			 * mechanism to synchronize with the cache boundaries, so that in
			 * a long transaction we'll basically dump alternating halves of
			 * the buffer array.
			 */
			LogwrtResult = XLogCtl->Write.LogwrtResult;
			if (XLByteLT(LogwrtResult.Write, LogwrtRqst.Write))
				XLogWrite(LogwrtRqst, true, false);
			LWLockRelease(WALWriteLock);
		}
	}

	/* Now wait to get insert lock */
	LWLockAcquire(WALInsertLock, LW_EXCLUSIVE);

	/*
	 * Check to see if my RedoRecPtr is out of date.  If so, may have to go
	 * back and recompute everything.  This can only happen just after a
	 * checkpoint, so it's better to be slow in this case and fast otherwise.
	 *
	 * If we aren't doing full-page writes then RedoRecPtr doesn't actually
	 * affect the contents of the XLOG record, so we'll update our local copy
	 * but not force a recomputation.
	 */
	if (!XLByteEQ(RedoRecPtr, Insert->RedoRecPtr))
	{
		Assert(XLByteLT(RedoRecPtr, Insert->RedoRecPtr));
		RedoRecPtr = Insert->RedoRecPtr;

		if (doPageWrites)
		{
			for (i = 0; i < XLR_MAX_BKP_BLOCKS; i++)
			{
				if (dtbuf[i] == InvalidBuffer)
					continue;
				if (dtbuf_bkp[i] == false &&
					XLByteLE(dtbuf_lsn[i], RedoRecPtr))
				{
					/*
					 * Oops, this buffer now needs to be backed up, but we
					 * didn't think so above.  Start over.
					 */
					LWLockRelease(WALInsertLock);

					END_CRIT_SECTION();
					goto begin;
				}
			}
		}
	}

	/*
	 * Also check to see if forcePageWrites was just turned on; if we weren't
	 * already doing full-page writes then go back and recompute. (If it was
	 * just turned off, we could recompute the record without full pages, but
	 * we choose not to bother.)
	 */
	if (Insert->forcePageWrites && !doPageWrites)
	{
		/* Oops, must redo it with full-page data */
		LWLockRelease(WALInsertLock);

		END_CRIT_SECTION();
		goto begin;
	}

	/*
	 * Make additional rdata chain entries for the backup blocks, so that we
	 * don't need to special-case them in the write loop.  Note that we have
	 * now irrevocably changed the input rdata chain.  At the exit of this
	 * loop, write_len includes the backup block data.
	 *
	 * Also set the appropriate info bits to show which buffers were backed
	 * up. The i'th XLR_SET_BKP_BLOCK bit corresponds to the i'th distinct
	 * buffer value (ignoring InvalidBuffer) appearing in the rdata chain.
	 */
	write_len = len;
	for (i = 0; i < XLR_MAX_BKP_BLOCKS; i++)
	{
		BkpBlock   *bkpb;
		char	   *page;

		if (!dtbuf_bkp[i])
			continue;

		info |= XLR_SET_BKP_BLOCK(i);

		bkpb = &(dtbuf_xlg[i]);
		page = (char *) BufferGetBlock(dtbuf[i]);

		rdt->next = &(dtbuf_rdt1[i]);
		rdt = rdt->next;

		rdt->data = (char *) bkpb;
		rdt->len = sizeof(BkpBlock);
		write_len += sizeof(BkpBlock);

		rdt->next = &(dtbuf_rdt2[i]);
		rdt = rdt->next;

		if (bkpb->hole_length == 0)
		{
			rdt->data = page;
			rdt->len = BLCKSZ;
			write_len += BLCKSZ;
			rdt->next = NULL;
		}
		else
		{
			/* must skip the hole */
			rdt->data = page;
			rdt->len = bkpb->hole_offset;
			write_len += bkpb->hole_offset;

			rdt->next = &(dtbuf_rdt3[i]);
			rdt = rdt->next;

			rdt->data = page + (bkpb->hole_offset + bkpb->hole_length);
			rdt->len = BLCKSZ - (bkpb->hole_offset + bkpb->hole_length);
			write_len += rdt->len;
			rdt->next = NULL;
		}
	}

	/*
	 * If there isn't enough space on the current XLOG page for a record
	 * header, advance to the next page (leaving the unused space as zeroes).
	 */
	updrqst = false;
	freespace = INSERT_FREESPACE(Insert);
	if (freespace < SizeOfXLogRecord)
	{
		updrqst = AdvanceXLInsertBuffer(false);
		freespace = INSERT_FREESPACE(Insert);
	}

	/* Compute record's XLOG location */
	curridx = Insert->curridx;
	INSERT_RECPTR(RecPtr, Insert, curridx);

	/*
	 * If the record is an XLOG_SWITCH, and we are exactly at the start of a
	 * segment, we need not insert it (and don't want to because we'd like
	 * consecutive switch requests to be no-ops).  Instead, make sure
	 * everything is written and flushed through the end of the prior segment,
	 * and return the prior segment's end address.
	 */
	if (isLogSwitch &&
		(RecPtr.xrecoff % XLogSegSize) == SizeOfXLogLongPHD)
	{
		LWLockRelease(WALInsertLock);

		RecPtr.xrecoff -= SizeOfXLogLongPHD;
		if (RecPtr.xrecoff == 0)
		{
			/* crossing a logid boundary */
			RecPtr.xlogid -= 1;
			RecPtr.xrecoff = XLogFileSize;
		}

		LWLockAcquire(WALWriteLock, LW_EXCLUSIVE);
		LogwrtResult = XLogCtl->Write.LogwrtResult;
		if (!XLByteLE(RecPtr, LogwrtResult.Flush))
		{
			XLogwrtRqst FlushRqst;

			FlushRqst.Write = RecPtr;
			FlushRqst.Flush = RecPtr;
			XLogWrite(FlushRqst, false, false);
		}
		LWLockRelease(WALWriteLock);

		END_CRIT_SECTION();

		return RecPtr;
	}

	/* Insert record header */

	record = (XLogRecord *) Insert->currpos;
	record->xl_prev = Insert->PrevRecord;
	record->xl_xid = headerXid;
	record->xl_tot_len = SizeOfXLogRecord + write_len;
	record->xl_len = len;		/* doesn't include backup blocks */
	record->xl_info = info;
	record->xl_rmid = rmid;

	/* Now we can finish computing the record's CRC */
	rdata_crc = crc32c(rdata_crc, (char *) record + sizeof(pg_crc32),
			   SizeOfXLogRecord - sizeof(pg_crc32));
	crc32cFinish(rdata_crc);
	record->xl_crc = rdata_crc;

	/* Record begin of record in appropriate places */
	if (!no_tran)
		MyLastRecPtr = RecPtr;
	ProcLastRecPtr = RecPtr;
	Insert->PrevRecord = RecPtr;
	MyXactMadeXLogEntry = true;

	ProcLastRecTotalLen = record->xl_tot_len;
	ProcLastRecDataLen = write_len;

	Insert->currpos += SizeOfXLogRecord;
	freespace -= SizeOfXLogRecord;

	if (Debug_xlog_insert_print)
	{
		StringInfoData buf;
		char *contiguousCopy;

		initStringInfo(&buf);
		appendStringInfo(&buf, "XLOG INSERT @ %s, total length %u, data length %u: ",
						 XLogLocationToString(&RecPtr),
						 ProcLastRecTotalLen,
						 ProcLastRecDataLen);
		XLog_OutRec(&buf, record);

		contiguousCopy = XLogContiguousCopy(record, rdata);
		appendStringInfo(&buf, " - ");
		RmgrTable[record->xl_rmid].rm_desc(&buf, RecPtr, (XLogRecord*)contiguousCopy);
		pfree(contiguousCopy);

		elog(LOG, "%s", buf.data);
		pfree(buf.data);
	}

	/*
	 * Always copy of the relevant rdata information in case we discover below we
	 * are in 'Change Tracking' mode and need to call ChangeTracking_AddRecordFromXlog().
	 */

	rdatabuf = ChangeTracking_CopyRdataBuffers(rdata, rmid, info, &rdata_iscopy);

	/*
	 * Append the data, including backup blocks if any
	 */
	while (write_len)
	{
		while (rdata->data == NULL)
			rdata = rdata->next;

		if (freespace > 0)
		{
			if (rdata->len > freespace)
			{
				memcpy(Insert->currpos, rdata->data, freespace);
				rdata->data += freespace;
				rdata->len -= freespace;
				write_len -= freespace;
			}
			else
			{
				/* enough room to write whole data. do it. */
				memcpy(Insert->currpos, rdata->data, rdata->len);
				freespace -= rdata->len;
				write_len -= rdata->len;
				Insert->currpos += rdata->len;
				rdata = rdata->next;
				continue;
			}
		}

		/* Use next buffer */
		updrqst = AdvanceXLInsertBuffer(false);
		curridx = Insert->curridx;
		/* Insert cont-record header */
		Insert->currpage->xlp_info |= XLP_FIRST_IS_CONTRECORD;
		contrecord = (XLogContRecord *) Insert->currpos;
		contrecord->xl_rem_len = write_len;
		Insert->currpos += SizeOfXLogContRecord;
		freespace = INSERT_FREESPACE(Insert);
	}

	/* Ensure next record will be properly aligned */
	Insert->currpos = (char *) Insert->currpage +
		MAXALIGN(Insert->currpos - (char *) Insert->currpage);
	freespace = INSERT_FREESPACE(Insert);

	/*
	 * The recptr I return is the beginning of the *next* record. This will be
	 * stored as LSN for changed data pages...
	 */
	INSERT_RECPTR(RecPtr, Insert, curridx);

	/*
	 * If the record is an XLOG_SWITCH, we must now write and flush all the
	 * existing data, and then forcibly advance to the start of the next
	 * segment.  It's not good to do this I/O while holding the insert lock,
	 * but there seems too much risk of confusion if we try to release the
	 * lock sooner.  Fortunately xlog switch needn't be a high-performance
	 * operation anyway...
	 */
	if (isLogSwitch)
	{
		XLogCtlWrite *Write = &XLogCtl->Write;
		XLogwrtRqst FlushRqst;
		XLogRecPtr	OldSegEnd;

		LWLockAcquire(WALWriteLock, LW_EXCLUSIVE);

		/*
		 * Flush through the end of the page containing XLOG_SWITCH, and
		 * perform end-of-segment actions (eg, notifying archiver).
		 */
		WriteRqst = XLogCtl->xlblocks[curridx];
		FlushRqst.Write = WriteRqst;
		FlushRqst.Flush = WriteRqst;
		XLogWrite(FlushRqst, false, true);

		/* Set up the next buffer as first page of next segment */
		/* Note: AdvanceXLInsertBuffer cannot need to do I/O here */
		(void) AdvanceXLInsertBuffer(true);

		/* There should be no unwritten data */
		curridx = Insert->curridx;
		Assert(curridx == Write->curridx);

		/* Compute end address of old segment */
		OldSegEnd = XLogCtl->xlblocks[curridx];
		OldSegEnd.xrecoff -= XLOG_BLCKSZ;
		if (OldSegEnd.xrecoff == 0)
		{
			/* crossing a logid boundary */
			OldSegEnd.xlogid -= 1;
			OldSegEnd.xrecoff = XLogFileSize;
		}

		/* Make it look like we've written and synced all of old segment */
		LogwrtResult.Write = OldSegEnd;
		LogwrtResult.Flush = OldSegEnd;

		/*
		 * Update shared-memory status --- this code should match XLogWrite
		 */
		{
			/* use volatile pointer to prevent code rearrangement */
			volatile XLogCtlData *xlogctl = XLogCtl;

			SpinLockAcquire(&xlogctl->info_lck);
			xlogctl->LogwrtResult = LogwrtResult;
			if (XLByteLT(xlogctl->LogwrtRqst.Write, LogwrtResult.Write))
				xlogctl->LogwrtRqst.Write = LogwrtResult.Write;
			if (XLByteLT(xlogctl->LogwrtRqst.Flush, LogwrtResult.Flush))
				xlogctl->LogwrtRqst.Flush = LogwrtResult.Flush;
			SpinLockRelease(&xlogctl->info_lck);
		}

		Write->LogwrtResult = LogwrtResult;

		LWLockRelease(WALWriteLock);

		updrqst = false;		/* done already */
	}
	else
	{
		/* normal case, ie not xlog switch */

		/* Need to update shared LogwrtRqst if some block was filled up */
		if (freespace < SizeOfXLogRecord)
		{
			/* curridx is filled and available for writing out */
			updrqst = true;
		}
		else
		{
			/* if updrqst already set, write through end of previous buf */
			curridx = PrevBufIdx(curridx);
		}
		WriteRqst = XLogCtl->xlblocks[curridx];
	}

	/*
	 * Use this lock to make sure we add Change Tracking records correctly.
	 *
	 * IMPORTANT: Acquiring this lock must be done AFTER ALL WRITE AND FSYNC calls under
	 * WALInsertLock.  Otherwise, the write suspension that occurs as a natural part of
	 * mirror communication loss and fault handling would suspend us and cause a deadlock.
	 *
	 * When this lock is held EXCLUSIVE, we are in transition from 'In Sync' to
	 * 'Change Tracking'.  During that time other processes are initializing the
	 * 'Change Tracking' log with information since the last checkpoint.  Thus, we need to
	 * wait here before we add our information.
	 */
	LWLockAcquire(ChangeTrackingTransitionLock, LW_SHARED);

	if (Debug_print_xlog_relation_change_info && rdatabuf != NULL)
	{
		bool skipIssue;

		skipIssue =
			ChangeTracking_PrintRelationChangeInfo(
												rmid,
												info,
												(void *)rdatabuf,
												&RecPtr,
												/* weAreGeneratingXLogNow */ true,
												/* printSkipIssuesOnly */ Debug_print_xlog_relation_change_info_skip_issues_only);

		if (Debug_print_xlog_relation_change_info_backtrace_skip_issues &&
			skipIssue)
		{
			/* Code for investigating MPP-13909, will be removed as part of the fix */
			elog(WARNING, 
				 "ChangeTracking_PrintRelationChangeInfo hang skipIssue %s",
				 (skipIssue ? "true" : "false"));
			
			for (int i=0; i < 24 * 60; i++)
			{
				pg_usleep(60000000L); /* 60 sec */
			}
			Insist(0);
			debug_backtrace();
		}
	}

	/* if needed, send this record to the changetracker */
	if (ChangeTracking_ShouldTrackChanges() && rdatabuf != NULL)
	{
		ChangeTracking_AddRecordFromXlog(rmid, info, (void *)rdatabuf, &RecPtr);
	}

	/*
	 * Last LSN location has to be tracked also when no mirrors are configured
	 * in order to handle gpaddmirrors correctly
	 */
	XLogCtl->lastChangeTrackingEndLoc = RecPtr;

	if(rdata_iscopy)
	{
		if (rdatabuf != NULL)
		{
			pfree(rdatabuf);
			rdatabuf = NULL;
		}
		rdata_iscopy = false;
	}

	LWLockRelease(ChangeTrackingTransitionLock);

	LWLockRelease(WALInsertLock);

	if (updrqst)
	{
		/* use volatile pointer to prevent code rearrangement */
		volatile XLogCtlData *xlogctl = XLogCtl;

		SpinLockAcquire(&xlogctl->info_lck);
		/* advance global request to include new block(s) */
		if (XLByteLT(xlogctl->LogwrtRqst.Write, WriteRqst))
			xlogctl->LogwrtRqst.Write = WriteRqst;
		/* update local result copy while I have the chance */
		LogwrtResult = xlogctl->LogwrtResult;
		SpinLockRelease(&xlogctl->info_lck);
	}

	ProcLastRecEnd = RecPtr;

	END_CRIT_SECTION();

	return RecPtr;
}

XLogRecPtr
XLogLastInsertBeginLoc(void)
{
	return ProcLastRecPtr;
}

XLogRecPtr
XLogLastInsertEndLoc(void)
{
	return ProcLastRecEnd;
}

XLogRecPtr
XLogLastChangeTrackedLoc(void)
{
	return XLogCtl->lastChangeTrackingEndLoc;
}

uint32
XLogLastInsertTotalLen(void)
{
	return ProcLastRecTotalLen;
}

uint32
XLogLastInsertDataLen(void)
{
	return ProcLastRecDataLen;
}

/*
 * Determine whether the buffer referenced by an XLogRecData item has to
 * be backed up, and if so fill a BkpBlock struct for it.  In any case
 * save the buffer's LSN at *lsn.
 */
static bool
XLogCheckBuffer(XLogRecData *rdata, bool doPageWrites,
				XLogRecPtr *lsn, BkpBlock *bkpb)
{
	PageHeader	page;

	page = (PageHeader) BufferGetBlock(rdata->buffer);

	/*
	 * XXX We assume page LSN is first data on *every* page that can be passed
	 * to XLogInsert, whether it otherwise has the standard page layout or
	 * not.
	 */
	*lsn = page->pd_lsn;

	if (doPageWrites &&
		XLByteLE(page->pd_lsn, RedoRecPtr))
	{
		/*
		 * The page needs to be backed up, so set up *bkpb
		 */
		bkpb->node = BufferGetFileNode(rdata->buffer);
		bkpb->block = BufferGetBlockNumber(rdata->buffer);

		if (rdata->buffer_std)
		{
			/* Assume we can omit data between pd_lower and pd_upper */
			uint16		lower = page->pd_lower;
			uint16		upper = page->pd_upper;

			if (lower >= SizeOfPageHeaderData &&
				upper > lower &&
				upper <= BLCKSZ)
			{
				bkpb->hole_offset = lower;
				bkpb->hole_length = upper - lower;
			}
			else
			{
				/* No "hole" to compress out */
				bkpb->hole_offset = 0;
				bkpb->hole_length = 0;
			}
		}
		else
		{
			/* Not a standard page header, don't try to eliminate "hole" */
			bkpb->hole_offset = 0;
			bkpb->hole_length = 0;
		}

		return true;			/* buffer requires backup */
	}

	return false;				/* buffer does not need to be backed up */
}

/*
 * XLogArchiveNotify
 *
 * Create an archive notification file
 *
 * The name of the notification file is the message that will be picked up
 * by the archiver, e.g. we write 0000000100000001000000C6.ready
 * and the archiver then knows to archive XLOGDIR/0000000100000001000000C6,
 * then when complete, rename it to 0000000100000001000000C6.done
 */
static void
XLogArchiveNotify(const char *xlog)
{
	char		archiveStatusPath[MAXPGPATH];
	FILE	   *fd;

	/* insert an otherwise empty file called <XLOG>.ready */
	StatusFilePath(archiveStatusPath, xlog, ".ready");
	fd = AllocateFile(archiveStatusPath, "w");
	if (fd == NULL)
	{
		ereport(LOG,
				(errcode_for_file_access(),
				 errmsg("could not create archive status file \"%s\": %m",
						archiveStatusPath)));
		return;
	}
	if (FreeFile(fd))
	{
		ereport(LOG,
				(errcode_for_file_access(),
				 errmsg("could not write archive status file \"%s\": %m",
						archiveStatusPath)));
		return;
	}

	/* Notify archiver that it's got something to do */
	if (IsUnderPostmaster)
		SendPostmasterSignal(PMSIGNAL_WAKEN_ARCHIVER);
}

/*
 * Convenience routine to notify using log/seg representation of filename
 */
static void
XLogArchiveNotifySeg(uint32 log, uint32 seg)
{
	char		xlog[MAXFNAMELEN];

	XLogFileName(xlog, ThisTimeLineID, log, seg);
	XLogArchiveNotify(xlog);
}

/*
 * XLogArchiveCheckDone
 *
 * This is called when we are ready to delete or recycle an old XLOG segment
 * file or backup history file.  If it is okay to delete it then return true.
 * If it is not time to delete it, make sure a .ready file exists, and return
 * false.
 *
 * If <XLOG>.done exists, then return true; else if <XLOG>.ready exists,
 * then return false; else create <XLOG>.ready and return false.
 *
 * The reason we do things this way is so that if the original attempt to
 * create <XLOG>.ready fails, we'll retry during subsequent checkpoints.
 */
static bool
XLogArchiveCheckDone(const char *xlog)
{
	char		archiveStatusPath[MAXPGPATH];
	struct stat stat_buf;

	/* Always deletable if archiving is off */
	if (!XLogArchivingActive())
		return true;

	/* First check for .done --- this means archiver is done with it */
	StatusFilePath(archiveStatusPath, xlog, ".done");
	if (stat(archiveStatusPath, &stat_buf) == 0)
		return true;

	/* check for .ready --- this means archiver is still busy with it */
	StatusFilePath(archiveStatusPath, xlog, ".ready");
	if (stat(archiveStatusPath, &stat_buf) == 0)
		return false;

	/* Race condition --- maybe archiver just finished, so recheck */
	StatusFilePath(archiveStatusPath, xlog, ".done");
	if (stat(archiveStatusPath, &stat_buf) == 0)
		return true;

	/* Retry creation of the .ready file */
	XLogArchiveNotify(xlog);
	return false;
}

/*
 * XLogArchiveCleanup
 *
 * Cleanup archive notification file(s) for a particular xlog segment
 */
static void
XLogArchiveCleanup(const char *xlog)
{
	char		archiveStatusPath[MAXPGPATH];

	/* Remove the .done file */
	StatusFilePath(archiveStatusPath, xlog, ".done");
	unlink(archiveStatusPath);
	/* should we complain about failure? */

	/* Remove the .ready file if present --- normally it shouldn't be */
	StatusFilePath(archiveStatusPath, xlog, ".ready");
	unlink(archiveStatusPath);
	/* should we complain about failure? */
}

/*
 * Advance the Insert state to the next buffer page, writing out the next
 * buffer if it still contains unwritten data.
 *
 * If new_segment is TRUE then we set up the next buffer page as the first
 * page of the next xlog segment file, possibly but not usually the next
 * consecutive file page.
 *
 * The global LogwrtRqst.Write pointer needs to be advanced to include the
 * just-filled page.  If we can do this for free (without an extra lock),
 * we do so here.  Otherwise the caller must do it.  We return TRUE if the
 * request update still needs to be done, FALSE if we did it internally.
 *
 * Must be called with WALInsertLock held.
 */
static bool
AdvanceXLInsertBuffer(bool new_segment)
{
	XLogCtlInsert *Insert = &XLogCtl->Insert;
	XLogCtlWrite *Write = &XLogCtl->Write;
	int			nextidx = NextBufIdx(Insert->curridx);
	bool		update_needed = true;
	XLogRecPtr	OldPageRqstPtr;
	XLogwrtRqst WriteRqst;
	XLogRecPtr	NewPageEndPtr;
	XLogPageHeader NewPage;

	/* Use Insert->LogwrtResult copy if it's more fresh */
	if (XLByteLT(LogwrtResult.Write, Insert->LogwrtResult.Write))
		LogwrtResult = Insert->LogwrtResult;

	/*
	 * Get ending-offset of the buffer page we need to replace (this may be
	 * zero if the buffer hasn't been used yet).  Fall through if it's already
	 * written out.
	 */
	OldPageRqstPtr = XLogCtl->xlblocks[nextidx];
	if (!XLByteLE(OldPageRqstPtr, LogwrtResult.Write))
	{
		/* nope, got work to do... */
		XLogRecPtr	FinishedPageRqstPtr;

		FinishedPageRqstPtr = XLogCtl->xlblocks[Insert->curridx];

		/* Before waiting, get info_lck and update LogwrtResult */
		{
			/* use volatile pointer to prevent code rearrangement */
			volatile XLogCtlData *xlogctl = XLogCtl;

			SpinLockAcquire(&xlogctl->info_lck);
			if (XLByteLT(xlogctl->LogwrtRqst.Write, FinishedPageRqstPtr))
				xlogctl->LogwrtRqst.Write = FinishedPageRqstPtr;
			LogwrtResult = xlogctl->LogwrtResult;
			SpinLockRelease(&xlogctl->info_lck);
		}

		update_needed = false;	/* Did the shared-request update */

		if (XLByteLE(OldPageRqstPtr, LogwrtResult.Write))
		{
			/* OK, someone wrote it already */
			Insert->LogwrtResult = LogwrtResult;
		}
		else
		{
			/* Must acquire write lock */
			LWLockAcquire(WALWriteLock, LW_EXCLUSIVE);
			LogwrtResult = Write->LogwrtResult;
			if (XLByteLE(OldPageRqstPtr, LogwrtResult.Write))
			{
				/* OK, someone wrote it already */
				LWLockRelease(WALWriteLock);
				Insert->LogwrtResult = LogwrtResult;
			}
			else
			{
				/*
				 * Have to write buffers while holding insert lock. This is
				 * not good, so only write as much as we absolutely must.
				 */
				WriteRqst.Write = OldPageRqstPtr;
				WriteRqst.Flush.xlogid = 0;
				WriteRqst.Flush.xrecoff = 0;
				XLogWrite(WriteRqst, false, false);
				LWLockRelease(WALWriteLock);
				Insert->LogwrtResult = LogwrtResult;
			}
		}
	}

	/*
	 * Now the next buffer slot is free and we can set it up to be the next
	 * output page.
	 */
	NewPageEndPtr = XLogCtl->xlblocks[Insert->curridx];

	if (new_segment)
	{
		/* force it to a segment start point */
		NewPageEndPtr.xrecoff += XLogSegSize - 1;
		NewPageEndPtr.xrecoff -= NewPageEndPtr.xrecoff % XLogSegSize;
	}

	if (NewPageEndPtr.xrecoff >= XLogFileSize)
	{
		/* crossing a logid boundary */
		NewPageEndPtr.xlogid += 1;
		NewPageEndPtr.xrecoff = XLOG_BLCKSZ;
	}
	else
		NewPageEndPtr.xrecoff += XLOG_BLCKSZ;
	XLogCtl->xlblocks[nextidx] = NewPageEndPtr;
	NewPage = (XLogPageHeader) (XLogCtl->pages + nextidx * (Size) XLOG_BLCKSZ);

	Insert->curridx = nextidx;
	Insert->currpage = NewPage;

	Insert->currpos = ((char *) NewPage) +SizeOfXLogShortPHD;

	/*
	 * Be sure to re-zero the buffer so that bytes beyond what we've written
	 * will look like zeroes and not valid XLOG records...
	 */
	MemSet((char *) NewPage, 0, XLOG_BLCKSZ);

	/*
	 * Fill the new page's header
	 */
	NewPage   ->xlp_magic = XLOG_PAGE_MAGIC;

	/* NewPage->xlp_info = 0; */	/* done by memset */
	NewPage   ->xlp_tli = ThisTimeLineID;
	NewPage   ->xlp_pageaddr.xlogid = NewPageEndPtr.xlogid;
	NewPage   ->xlp_pageaddr.xrecoff = NewPageEndPtr.xrecoff - XLOG_BLCKSZ;

	/*
	 * If first page of an XLOG segment file, make it a long header.
	 */
	if ((NewPage->xlp_pageaddr.xrecoff % XLogSegSize) == 0)
	{
		XLogLongPageHeader NewLongPage = (XLogLongPageHeader) NewPage;

		NewLongPage->xlp_sysid = ControlFile->system_identifier;
		NewLongPage->xlp_seg_size = XLogSegSize;
		NewLongPage->xlp_xlog_blcksz = XLOG_BLCKSZ;
		NewPage   ->xlp_info |= XLP_LONG_HEADER;

		Insert->currpos = ((char *) NewPage) +SizeOfXLogLongPHD;
	}

	return update_needed;
}

void XLogGetBuffer(int startidx, int npages, char **from, Size *nbytes)
{
	*from = XLogCtl->pages + startidx * (Size) XLOG_BLCKSZ;
	*nbytes = npages * (Size) XLOG_BLCKSZ;
}

/*
 * Write and/or fsync the log at least as far as WriteRqst indicates.
 *
 * If flexible == TRUE, we don't have to write as far as WriteRqst, but
 * may stop at any convenient boundary (such as a cache or logfile boundary).
 * This option allows us to avoid uselessly issuing multiple writes when a
 * single one would do.
 *
 * If xlog_switch == TRUE, we are intending an xlog segment switch, so
 * perform end-of-segment actions after writing the last page, even if
 * it's not physically the end of its segment.  (NB: this will work properly
 * only if caller specifies WriteRqst == page-end and flexible == false,
 * and there is some data to write.)
 *
 * Must be called with WALWriteLock held.
 */
static void
XLogWrite(XLogwrtRqst WriteRqst, bool flexible, bool xlog_switch)
{
	XLogCtlWrite *Write = &XLogCtl->Write;
	bool		ispartialpage;
	bool		last_iteration;
	bool		finishing_seg;
	bool		use_existent;
	int			curridx;
	int			npages;
	int			startidx;
	uint32		startoffset;

	/* We should always be inside a critical section here */
	Assert(CritSectionCount > 0);

	/*
	 * Update local LogwrtResult (caller probably did this already, but...)
	 */
	LogwrtResult = Write->LogwrtResult;

	/*
	 * Since successive pages in the xlog cache are consecutively allocated,
	 * we can usually gather multiple pages together and issue just one
	 * write() call.  npages is the number of pages we have determined can be
	 * written together; startidx is the cache block index of the first one,
	 * and startoffset is the file offset at which it should go. The latter
	 * two variables are only valid when npages > 0, but we must initialize
	 * all of them to keep the compiler quiet.
	 */
	npages = 0;
	startidx = 0;
	startoffset = 0;

	/*
	 * Within the loop, curridx is the cache block index of the page to
	 * consider writing.  We advance Write->curridx only after successfully
	 * writing pages.  (Right now, this refinement is useless since we are
	 * going to PANIC if any error occurs anyway; but someday it may come in
	 * useful.)
	 */
	curridx = Write->curridx;

	while (XLByteLT(LogwrtResult.Write, WriteRqst.Write))
	{
		/*
		 * Make sure we're not ahead of the insert process.  This could happen
		 * if we're passed a bogus WriteRqst.Write that is past the end of the
		 * last page that's been initialized by AdvanceXLInsertBuffer.
		 */
		if (!XLByteLT(LogwrtResult.Write, XLogCtl->xlblocks[curridx]))
			elog(PANIC, "xlog write request %X/%X is past end of log %X/%X",
				 LogwrtResult.Write.xlogid, LogwrtResult.Write.xrecoff,
				 XLogCtl->xlblocks[curridx].xlogid,
				 XLogCtl->xlblocks[curridx].xrecoff);

		/* Advance LogwrtResult.Write to end of current buffer page */
		LogwrtResult.Write = XLogCtl->xlblocks[curridx];
		ispartialpage = XLByteLT(WriteRqst.Write, LogwrtResult.Write);

		if (!XLByteInPrevSeg(LogwrtResult.Write, openLogId, openLogSeg))
		{
			/*
			 * Switch to new logfile segment.  We cannot have any pending
			 * pages here (since we dump what we have at segment end).
			 */
			Assert(npages == 0);
			if (MirroredFlatFile_IsActive(&mirroredLogFileOpen))
				XLogFileClose();
			XLByteToPrevSeg(LogwrtResult.Write, openLogId, openLogSeg);

			/* create/use new log file */
			use_existent = true;

			XLogFileInit(
					&mirroredLogFileOpen,
					openLogId, openLogSeg,
					&use_existent, true);
			openLogOff = 0;

			/* update pg_control, unless someone else already did */
			LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
			if (ControlFile->logId < openLogId ||
				(ControlFile->logId == openLogId &&
				 ControlFile->logSeg < openLogSeg + 1))
			{
				ControlFile->logId = openLogId;
				ControlFile->logSeg = openLogSeg + 1;
				ControlFile->time = time(NULL);
				UpdateControlFile();

				/*
				 * Signal bgwriter to start a checkpoint if it's been too long
				 * since the last one.	(We look at local copy of RedoRecPtr
				 * which might be a little out of date, but should be close
				 * enough for this purpose.)
				 *
				 * A straight computation of segment number could overflow 32
				 * bits.  Rather than assuming we have working 64-bit
				 * arithmetic, we compare the highest-order bits separately,
				 * and force a checkpoint immediately when they change.
				 */
				if (IsUnderPostmaster)
				{
					uint32		old_segno,
								new_segno;
					uint32		old_highbits,
								new_highbits;

					old_segno = (RedoRecPtr.xlogid % XLogSegSize) * XLogSegsPerFile +
						(RedoRecPtr.xrecoff / XLogSegSize);
					old_highbits = RedoRecPtr.xlogid / XLogSegSize;
					new_segno = (openLogId % XLogSegSize) * XLogSegsPerFile +
						openLogSeg;
					new_highbits = openLogId / XLogSegSize;
					if (new_highbits != old_highbits ||
						new_segno >= old_segno + (uint32) CheckPointSegments)
					{
#ifdef WAL_DEBUG
						if (XLOG_DEBUG)
							elog(LOG, "time for a checkpoint, signaling bgwriter");
#endif
						if (Debug_print_qd_mirroring)
							elog(LOG, "time for a checkpoint, signaling bgwriter");
						RequestCheckpoint(false, true);
					}
				}
			}
			LWLockRelease(ControlFileLock);
		}

		/* Make sure we have the current logfile open */
		if (!MirroredFlatFile_IsActive(&mirroredLogFileOpen))
		{
			XLByteToPrevSeg(LogwrtResult.Write, openLogId, openLogSeg);
			XLogFileOpen(
					&mirroredLogFileOpen,
					openLogId,
					openLogSeg);
			openLogOff = 0;
		}

		/* Add current page to the set of pending pages-to-dump */
		if (npages == 0)
		{
			/* first of group */
			startidx = curridx;
			startoffset = (LogwrtResult.Write.xrecoff - XLOG_BLCKSZ) % XLogSegSize;
		}
		npages++;

		/*
		 * Dump the set if this will be the last loop iteration, or if we are
		 * at the last page of the cache area (since the next page won't be
		 * contiguous in memory), or if we are at the end of the logfile
		 * segment.
		 */
		last_iteration = !XLByteLT(LogwrtResult.Write, WriteRqst.Write);

		finishing_seg = !ispartialpage &&
			(startoffset + npages * XLOG_BLCKSZ) >= XLogSegSize;

		if (last_iteration ||
			curridx == XLogCtl->XLogCacheBlck ||
			finishing_seg)
		{
			char	   *from;
			Size		nbytes;

			/* Need to seek in the file? */
			if (openLogOff != startoffset)
			{
				openLogOff = startoffset;
			}

			/* OK to write the page(s) */
			from = XLogCtl->pages + startidx * (Size) XLOG_BLCKSZ;
			nbytes = npages * (Size) XLOG_BLCKSZ;

			/* The following code is a sanity check to try to catch the issue described in MPP-12611 */
			if (!IsBootstrapProcessingMode())
			  {
			  char   simpleFileName[MAXPGPATH];
			  XLogFileName(simpleFileName, ThisTimeLineID, openLogId, openLogSeg);
                          if (strcmp(simpleFileName, mirroredLogFileOpen.simpleFileName) != 0)
			    {
			      ereport( PANIC
				       , (errmsg_internal("Expected Xlog file name does not match current open xlog file name. \
                                                           Expected file = %s, \
                                                           open file = %s, \
                                                           WriteRqst.Write = %s, \
                                                           WriteRqst.Flush = %s "
							 , simpleFileName
							 , mirroredLogFileOpen.simpleFileName
							 , XLogLocationToString(&(WriteRqst.Write))
							 , XLogLocationToString(&(WriteRqst.Flush)))));
			    }
			  }

			if (MirroredFlatFile_Write(
							&mirroredLogFileOpen,
							openLogOff,
							from,
							nbytes,
							/* suppressError */ true))
			{
				ereport(PANIC,
						(errcode_for_file_access(),
						 errmsg("could not write to log file %u, segment %u "
								"at offset %u, length %lu: %m",
								openLogId, openLogSeg,
								openLogOff, (unsigned long) nbytes)));
			}

			/* Update state for write */
			openLogOff += nbytes;
			Write->curridx = ispartialpage ? curridx : NextBufIdx(curridx);
			npages = 0;

			/*
			 * If we just wrote the whole last page of a logfile segment,
			 * fsync the segment immediately.  This avoids having to go back
			 * and re-open prior segments when an fsync request comes along
			 * later. Doing it here ensures that one and only one backend will
			 * perform this fsync.
			 *
			 * We also do this if this is the last page written for an xlog
			 * switch.
			 *
			 * This is also the right place to notify the Archiver that the
			 * segment is ready to copy to archival storage, and to update the
			 * timer for archive_timeout.
			 */
			if (finishing_seg || (xlog_switch && last_iteration))
			{
				if (MirroredFlatFile_IsActive(&mirroredLogFileOpen))
					MirroredFlatFile_Flush(
									&mirroredLogFileOpen,
									/* suppressError */ false);

				elog((Debug_print_qd_mirroring ? LOG : DEBUG5),
					 "XLogWrite (#1): flush loc %s; write loc %s",
					 XLogLocationToString_Long(&LogwrtResult.Flush),
					 XLogLocationToString2_Long(&LogwrtResult.Write));

				LogwrtResult.Flush = LogwrtResult.Write;		/* end of page */

				if (XLogArchivingActive())
					XLogArchiveNotifySeg(openLogId, openLogSeg);

				Write->lastSegSwitchTime = time(NULL);
			}
		}

		if (ispartialpage)
		{
			/* Only asked to write a partial page */
			LogwrtResult.Write = WriteRqst.Write;
			break;
		}
		curridx = NextBufIdx(curridx);

		/* If flexible, break out of loop as soon as we wrote something */
		if (flexible && npages == 0)
			break;
	}

	Assert(npages == 0);
	Assert(curridx == Write->curridx);

	/*
	 * If asked to flush, do so
	 */
	if (XLByteLT(LogwrtResult.Flush, WriteRqst.Flush) &&
		XLByteLT(LogwrtResult.Flush, LogwrtResult.Write))
	{
		/*
		 * Could get here without iterating above loop, in which case we might
		 * have no open file or the wrong one.	However, we do not need to
		 * fsync more than one file.
		 */
		if (sync_method != SYNC_METHOD_OPEN)
		{
			if (MirroredFlatFile_IsActive(&mirroredLogFileOpen) &&
				!XLByteInPrevSeg(LogwrtResult.Write, openLogId, openLogSeg))
				XLogFileClose();
			if (!MirroredFlatFile_IsActive(&mirroredLogFileOpen))
			{
				XLByteToPrevSeg(LogwrtResult.Write, openLogId, openLogSeg);
				XLogFileOpen(
						&mirroredLogFileOpen,
						openLogId,
						openLogSeg);
				openLogOff = 0;
			}
			if (MirroredFlatFile_IsActive(&mirroredLogFileOpen))
				MirroredFlatFile_Flush(
								&mirroredLogFileOpen,
								/* suppressError */ false);

			elog((Debug_print_qd_mirroring ? LOG : DEBUG5),
				 "XLogWrite (#2): flush loc %s; write loc %s",
				 XLogLocationToString_Long(&LogwrtResult.Flush),
				 XLogLocationToString2_Long(&LogwrtResult.Write));
		}

		LogwrtResult.Flush = LogwrtResult.Write;
	}

	/*
	 * Update shared-memory status
	 *
	 * We make sure that the shared 'request' values do not fall behind the
	 * 'result' values.  This is not absolutely essential, but it saves some
	 * code in a couple of places.
	 */
	{
		/* use volatile pointer to prevent code rearrangement */
		volatile XLogCtlData *xlogctl = XLogCtl;

		SpinLockAcquire(&xlogctl->info_lck);
		xlogctl->LogwrtResult = LogwrtResult;
		if (XLByteLT(xlogctl->LogwrtRqst.Write, LogwrtResult.Write))
			xlogctl->LogwrtRqst.Write = LogwrtResult.Write;
		if (XLByteLT(xlogctl->LogwrtRqst.Flush, LogwrtResult.Flush))
			xlogctl->LogwrtRqst.Flush = LogwrtResult.Flush;
		SpinLockRelease(&xlogctl->info_lck);
	}

	Write->LogwrtResult = LogwrtResult;
}

/*
 * Ensure that all XLOG data through the given position is flushed to disk.
 *
 * NOTE: this differs from XLogWrite mainly in that the WALWriteLock is not
 * already held, and we try to avoid acquiring it if possible.
 */
void
XLogFlush(XLogRecPtr record)
{
	XLogRecPtr	WriteRqstPtr;
	XLogwrtRqst WriteRqst;

	/* Disabled during REDO */
	if (InRedo)
		return;

	if (Debug_print_qd_mirroring)
		elog(LOG, "xlog flush request %s; write %s; flush %s",
			 XLogLocationToString(&record),
			 XLogLocationToString2(&LogwrtResult.Write),
			 XLogLocationToString3(&LogwrtResult.Flush));

	/* Quick exit if already known flushed */
	if (XLByteLE(record, LogwrtResult.Flush))
		return;

#ifdef WAL_DEBUG
	if (XLOG_DEBUG)
		elog(LOG, "xlog flush request %X/%X; write %X/%X; flush %X/%X",
			 record.xlogid, record.xrecoff,
			 LogwrtResult.Write.xlogid, LogwrtResult.Write.xrecoff,
			 LogwrtResult.Flush.xlogid, LogwrtResult.Flush.xrecoff);
#endif

	START_CRIT_SECTION();

	/*
	 * Since fsync is usually a horribly expensive operation, we try to
	 * piggyback as much data as we can on each fsync: if we see any more data
	 * entered into the xlog buffer, we'll write and fsync that too, so that
	 * the final value of LogwrtResult.Flush is as large as possible. This
	 * gives us some chance of avoiding another fsync immediately after.
	 */

	/* initialize to given target; may increase below */
	WriteRqstPtr = record;

	/* read LogwrtResult and update local state */
	{
		/* use volatile pointer to prevent code rearrangement */
		volatile XLogCtlData *xlogctl = XLogCtl;

		SpinLockAcquire(&xlogctl->info_lck);
		if (XLByteLT(WriteRqstPtr, xlogctl->LogwrtRqst.Write))
			WriteRqstPtr = xlogctl->LogwrtRqst.Write;
		LogwrtResult = xlogctl->LogwrtResult;
		SpinLockRelease(&xlogctl->info_lck);
	}

	/* done already? */
	if (!XLByteLE(record, LogwrtResult.Flush))
	{
		/* now wait for the write lock */
		LWLockAcquire(WALWriteLock, LW_EXCLUSIVE);
		LogwrtResult = XLogCtl->Write.LogwrtResult;
		if (!XLByteLE(record, LogwrtResult.Flush))
		{
			/* try to write/flush later additions to XLOG as well */
			if (LWLockConditionalAcquire(WALInsertLock, LW_EXCLUSIVE))
			{
				XLogCtlInsert *Insert = &XLogCtl->Insert;
				uint32		freespace = INSERT_FREESPACE(Insert);

				if (freespace < SizeOfXLogRecord)		/* buffer is full */
					WriteRqstPtr = XLogCtl->xlblocks[Insert->curridx];
				else
				{
					WriteRqstPtr = XLogCtl->xlblocks[Insert->curridx];
					WriteRqstPtr.xrecoff -= freespace;
				}
				LWLockRelease(WALInsertLock);
				WriteRqst.Write = WriteRqstPtr;
				WriteRqst.Flush = WriteRqstPtr;
			}
			else
			{
				WriteRqst.Write = WriteRqstPtr;
				WriteRqst.Flush = record;
			}
			XLogWrite(WriteRqst, false, false);
		}
		LWLockRelease(WALWriteLock);
	}

	END_CRIT_SECTION();

	/*
	 * If we still haven't flushed to the request point then we have a
	 * problem; most likely, the requested flush point is past end of XLOG.
	 * This has been seen to occur when a disk page has a corrupted LSN.
	 *
	 * Formerly we treated this as a PANIC condition, but that hurts the
	 * system's robustness rather than helping it: we do not want to take down
	 * the whole system due to corruption on one data page.  In particular, if
	 * the bad page is encountered again during recovery then we would be
	 * unable to restart the database at all!  (This scenario has actually
	 * happened in the field several times with 7.1 releases. Note that we
	 * cannot get here while InRedo is true, but if the bad page is brought in
	 * and marked dirty during recovery then CreateCheckPoint will try to
	 * flush it at the end of recovery.)
	 *
	 * The current approach is to ERROR under normal conditions, but only
	 * WARNING during recovery, so that the system can be brought up even if
	 * there's a corrupt LSN.  Note that for calls from xact.c, the ERROR will
	 * be promoted to PANIC since xact.c calls this routine inside a critical
	 * section.  However, calls from bufmgr.c are not within critical sections
	 * and so we will not force a restart for a bad LSN on a data page.
	 */
	if (XLByteLT(LogwrtResult.Flush, record))
		elog(InRecovery ? WARNING : ERROR,
		"xlog flush request %X/%X is not satisfied --- flushed only to %X/%X",
			 record.xlogid, record.xrecoff,
			 LogwrtResult.Flush.xlogid, LogwrtResult.Flush.xrecoff);
}

/*
 * TODO: This is just for the matter of WAL receiver build.  We cannot
 * expose MirroredFlatFileOpen in xlog.h.
 */
int
XLogFileInitExt(uint32 log, uint32 seg, bool *use_existent, bool use_lock)
{
	MirroredFlatFileOpen mirroredOpen;

	XLogFileInit(&mirroredOpen, log, seg, use_existent, use_lock);
	return mirroredOpen.primaryFile;
}

/*
 * Create a new XLOG file segment, or open a pre-existing one.
 *
 * log, seg: identify segment to be created/opened.
 *
 * *use_existent: if TRUE, OK to use a pre-existing file (else, any
 * pre-existing file will be deleted).	On return, TRUE if a pre-existing
 * file was used.
 *
 * use_lock: if TRUE, acquire ControlFileLock while moving file into
 * place.  This should be TRUE except during bootstrap log creation.  The
 * caller must *not* hold the lock at call.
 *
 * Returns FD of opened file.
 *
 * Note: errors here are ERROR not PANIC because we might or might not be
 * inside a critical section (eg, during checkpoint there is no reason to
 * take down the system on failure).  They will promote to PANIC if we are
 * in a critical section.
 */
static void
XLogFileInit(
	MirroredFlatFileOpen *mirroredOpen,
	uint32 log, uint32 seg,
	bool *use_existent, bool use_lock)
{
	char		simpleFileName[MAXPGPATH];
	char		tmpsimple[MAXPGPATH];
	char		tmppath[MAXPGPATH];

	MirroredFlatFileOpen tmpMirroredOpen;

	char		zbuffer[XLOG_BLCKSZ];
	uint32		installed_log;
	uint32		installed_seg;
	int			max_advance;
	int			nbytes;
	char			*xlogDir = NULL;

	XLogFileName(simpleFileName, ThisTimeLineID, log, seg);

	/*
	 * Try to use existent file (checkpoint maker may have created it already)
	 */
	if (*use_existent)
	{
		if (MirroredFlatFile_Open(
							mirroredOpen,
							XLOGDIR,
							simpleFileName,
							O_RDWR | PG_BINARY | XLOG_SYNC_BIT,
						    S_IRUSR | S_IWUSR,
						    /* suppressError */ true,
							/* atomic operation */ false,
							/*isMirrorRecovery */ false))
		{
			char		path[MAXPGPATH];

			XLogFilePath(path, ThisTimeLineID, log, seg);

			if (errno != ENOENT)
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not open file \"%s\" (log file %u, segment %u): %m",
								path, log, seg)));
		}
		else
			return;
	}

	/*
	 * Initialize an empty (all zeroes) segment.  NOTE: it is possible that
	 * another process is doing the same thing.  If so, we will end up
	 * pre-creating an extra log segment.  That seems OK, and better than
	 * holding the lock throughout this lengthy process.
	 */
	xlogDir = makeRelativeToTxnFilespace(XLOGDIR);
		
	if (snprintf(tmpsimple, MAXPGPATH, "xlogtemp.%d", (int) getpid()) > MAXPGPATH)
	{
		ereport(ERROR, (errmsg("could not generate filename xlogtemp.%d", (int)getpid())));
        }

	if (snprintf(tmppath, MAXPGPATH, "%s/%s", xlogDir, tmpsimple) > MAXPGPATH)
	{
		ereport(ERROR, (errmsg("could not generate filename %s/%s", xlogDir, tmpsimple)));
        }


	MirroredFlatFile_Drop(
						  XLOGDIR,
						  tmpsimple,
						  /* suppressError */ true,
						  /*isMirrorRecovery */ false);

	/* do not use XLOG_SYNC_BIT here --- want to fsync only at end of fill */
	MirroredFlatFile_Open(
						&tmpMirroredOpen,
						XLOGDIR,
						tmpsimple,
						O_RDWR | O_CREAT | O_EXCL | PG_BINARY,
					    S_IRUSR | S_IWUSR,
					    /* suppressError */ false,
						/* atomic operation */ false,
						/*isMirrorRecovery */ false);

	/*
	 * Zero-fill the file.	We have to do this the hard way to ensure that all
	 * the file space has really been allocated --- on platforms that allow
	 * "holes" in files, just seeking to the end doesn't allocate intermediate
	 * space.  This way, we know that we have all the space and (after the
	 * fsync below) that all the indirect blocks are down on disk.	Therefore,
	 * fdatasync(2) or O_DSYNC will be sufficient to sync future writes to the
	 * log file.
	 */
	MemSet(zbuffer, 0, sizeof(zbuffer));
	for (nbytes = 0; nbytes < XLogSegSize; nbytes += sizeof(zbuffer))
	{
		errno = 0;
		if (MirroredFlatFile_Append(
							&tmpMirroredOpen,
							zbuffer,
							sizeof(zbuffer),
							/* suppressError */ true))
		{
			int			save_errno = errno;

			/*
			 * If we fail to make the file, delete it to release disk space
			 */
			MirroredFlatFile_Drop(
							XLOGDIR,
							tmpsimple,
							/* suppressError */ false,
							/*isMirrorRecovery */ false);

			/* if write didn't set errno, assume problem is no disk space */
			errno = save_errno ? save_errno : ENOSPC;

			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not write to file \"%s\": %m", tmppath)));
		}
	}

	MirroredFlatFile_Flush(
				&tmpMirroredOpen,
				/* suppressError */ false);

	MirroredFlatFile_Close(&tmpMirroredOpen);

	/*
	 * Now move the segment into place with its final name.
	 *
	 * If caller didn't want to use a pre-existing file, get rid of any
	 * pre-existing file.  Otherwise, cope with possibility that someone else
	 * has created the file while we were filling ours: if so, use ours to
	 * pre-create a future log segment.
	 */
	installed_log = log;
	installed_seg = seg;
	max_advance = XLOGfileslop;
	if (!InstallXLogFileSegment(&installed_log, &installed_seg, tmppath,
								*use_existent, &max_advance,
								use_lock, tmpsimple))
	{
		/* No need for any more future segments... */
		MirroredFlatFile_Drop(
						XLOGDIR,
						tmpsimple,
						/* suppressError */ false,
						/*isMirrorRecovery */ false);
	}

	/* Set flag to tell caller there was no existent file */
	*use_existent = false;

	/* Now open original target segment (might not be file I just made) */
	MirroredFlatFile_Open(
						mirroredOpen,
						XLOGDIR,
						simpleFileName,
						O_RDWR | PG_BINARY | XLOG_SYNC_BIT,
					    S_IRUSR | S_IWUSR,
					    /* suppressError */ false,
						/* atomic operation */ false,
						/*isMirrorRecovery */ false);

	pfree(xlogDir);
}

/*
 * Create a new XLOG file segment by copying a pre-existing one.
 *
 * log, seg: identify segment to be created.
 *
 * srcTLI, srclog, srcseg: identify segment to be copied (could be from
 *		a different timeline)
 *
 * Currently this is only used during recovery, and so there are no locking
 * considerations.	But we should be just as tense as XLogFileInit to avoid
 * emplacing a bogus file.
 */
static void
XLogFileCopy(uint32 log, uint32 seg,
			 TimeLineID srcTLI, uint32 srclog, uint32 srcseg)
{
	char		path[MAXPGPATH];
	char		tmppath[MAXPGPATH];
	char		buffer[XLOG_BLCKSZ];
	int			srcfd;
	int			fd;
	int			nbytes;
	char		*xlogDir = NULL;

	/*
	 * Open the source file
	 */
	XLogFilePath(path, srcTLI, srclog, srcseg);
	srcfd = BasicOpenFile(path, O_RDONLY | PG_BINARY, 0);
	if (srcfd < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open file \"%s\": %m", path)));

	/*
	 * Copy into a temp file name.
	 */
	xlogDir = makeRelativeToTxnFilespace(XLOGDIR);
	if (snprintf(tmppath, MAXPGPATH, "%s/xlogtemp.%d",
				 xlogDir, (int) getpid()) > MAXPGPATH)
		ereport(ERROR,
				(errmsg("could not generate filename %s/xlogtemp.%d",
						xlogDir, (int) getpid())));
	pfree(xlogDir);	
	unlink(tmppath);

	elog((Debug_print_qd_mirroring ? LOG : DEBUG5), "Master Mirroring: copying xlog file '%s' to '%s'",
		 path, tmppath);

	/* do not use XLOG_SYNC_BIT here --- want to fsync only at end of fill */
	fd = BasicOpenFile(tmppath, O_RDWR | O_CREAT | O_EXCL | PG_BINARY,
					   S_IRUSR | S_IWUSR);
	if (fd < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not create file \"%s\": %m", tmppath)));

	/*
	 * Do the data copying.
	 */
	for (nbytes = 0; nbytes < XLogSegSize; nbytes += sizeof(buffer))
	{
		errno = 0;
		if ((int) read(srcfd, buffer, sizeof(buffer)) != (int) sizeof(buffer))
		{
			if (errno != 0)
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not read file \"%s\": %m", path)));
			else
				ereport(ERROR,
						(errmsg("not enough data in file \"%s\"", path)));
		}
		errno = 0;
		if ((int) write(fd, buffer, sizeof(buffer)) != (int) sizeof(buffer))
		{
			int			save_errno = errno;

			/*
			 * If we fail to make the file, delete it to release disk space
			 */
			unlink(tmppath);
			/* if write didn't set errno, assume problem is no disk space */
			errno = save_errno ? save_errno : ENOSPC;

			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not write to file \"%s\": %m", tmppath)));
		}
	}

	if (pg_fsync(fd) != 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not fsync file \"%s\": %m", tmppath)));

	if (close(fd))
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not close file \"%s\": %m", tmppath)));

	close(srcfd);

	/*
	 * Now move the segment into place with its final name.
	 */
	if (!InstallXLogFileSegment(&log, &seg, tmppath, false, NULL, false, NULL))
		elog(ERROR, "InstallXLogFileSegment should not have failed");
}

/*
 * Install a new XLOG segment file as a current or future log segment.
 *
 * This is used both to install a newly-created segment (which has a temp
 * filename while it's being created) and to recycle an old segment.
 *
 * *log, *seg: identify segment to install as (or first possible target).
 * When find_free is TRUE, these are modified on return to indicate the
 * actual installation location or last segment searched.
 *
 * tmppath: initial name of file to install.  It will be renamed into place.
 *
 * find_free: if TRUE, install the new segment at the first empty log/seg
 * number at or after the passed numbers.  If FALSE, install the new segment
 * exactly where specified, deleting any existing segment file there.
 *
 * *max_advance: maximum number of log/seg slots to advance past the starting
 * point.  Fail if no free slot is found in this range.  On return, reduced
 * by the number of slots skipped over.  (Irrelevant, and may be NULL,
 * when find_free is FALSE.)
 *
 * use_lock: if TRUE, acquire ControlFileLock while moving file into
 * place.  This should be TRUE except during bootstrap log creation.  The
 * caller must *not* hold the lock at call.
 *
 * Returns TRUE if file installed, FALSE if not installed because of
 * exceeding max_advance limit.  On Windows, we also return FALSE if we
 * can't rename the file into place because someone's got it open.
 * (Any other kind of failure causes ereport().)
 */
static bool
InstallXLogFileSegment(uint32 *log, uint32 *seg, char *tmppath,
					   bool find_free, int *max_advance,
					   bool use_lock, char* tmpsimpleFileName)
{
	char		path[MAXPGPATH];
	char		simpleFileName[MAXPGPATH];
	struct stat stat_buf;
	int retval = 0;

	errno = 0;

	XLogFileName(simpleFileName, ThisTimeLineID, *log, *seg);

	XLogFilePath(path, ThisTimeLineID, *log, *seg);

	/*
	 * We want to be sure that only one process does this at a time.
	 */
	if (use_lock)
		LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);

	if (!find_free)
	{
		/* Force installation: get rid of any pre-existing segment file */
		if (tmpsimpleFileName) {

			MirroredFlatFile_Drop(
								  XLOGDIR,
								  simpleFileName,
								  /* suppressError */ true,
								  /*isMirrorRecovery */ false);
		} else {
			unlink(path);
		}
	}
	else
	{
		/* Find a free slot to put it in */
		while (stat(path, &stat_buf) == 0)
		{
			if (*max_advance <= 0)
			{
				/* Failed to find a free slot within specified range */
				if (use_lock)
					LWLockRelease(ControlFileLock);
				return false;
			}
			NextLogSeg(*log, *seg);
			(*max_advance)--;

			XLogFileName(simpleFileName, ThisTimeLineID, *log, *seg);
			XLogFilePath(path, ThisTimeLineID, *log, *seg);
		}
	}

	/*
	 * Prefer link() to rename() here just to be really sure that we don't
	 * overwrite an existing logfile.  However, there shouldn't be one, so
	 * rename() is an acceptable substitute except for the truly paranoid.
	 */
#if HAVE_WORKING_LINK

	if (tmpsimpleFileName) {
		retval = MirroredFlatFile_Rename(
										 XLOGDIR,
										 /* old name */ tmpsimpleFileName,
										 /* new name */ simpleFileName,
										 /* can exist? */ false,
										 /* isMirrorRecovery */ false);
	} else {
		retval = link(tmppath, path);
	}

	if (retval < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not link file \"%s\" to \"%s\" (initialization of log file %u, segment %u): %m",
						tmppath, path, *log, *seg)));

	if (tmpsimpleFileName) {

		MirroredFlatFile_Drop(
						  XLOGDIR,
						  tmpsimpleFileName,
						  /* suppressError */ true,
						  /*isMirrorRecovery */ false);
	} else {
		unlink(tmppath);
	}

#else
	if (tmpsimpleFileName) {
		retval = MirroredFlatFile_Rename(
						  XLOGDIR,
						  /* old name */ tmpsimpleFileName,
						  /* new name */ simpleFileName,
						  /* can exist */ false,
							/* isMirrorRecovery */ false);
	} else {
		retval = rename(tmppath, path);
	}

	if (retval < 0)
	{
#ifdef WIN32
#if !defined(__CYGWIN__)
		if (GetLastError() == ERROR_ACCESS_DENIED)
#else
		if (errno == EACCES)
#endif
		{
			if (use_lock)
				LWLockRelease(ControlFileLock);
			return false;
		}
#endif /* WIN32 */

		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not rename file \"%s\" to \"%s\" (initialization of log file %u, segment %u): %m",
						tmppath, path, *log, *seg)));
	}
#endif

	if (use_lock)
		LWLockRelease(ControlFileLock);

	return true;
}

/*
 * Open a pre-existing logfile segment for writing.
 */
static void
XLogFileOpen(
	MirroredFlatFileOpen *mirroredOpen,
	uint32 log,
	uint32 seg)
{
	char		simpleFileName[MAXPGPATH];

	XLogFileName(simpleFileName, ThisTimeLineID, log, seg);

	if (MirroredFlatFile_Open(
					mirroredOpen,
					XLOGDIR,
					simpleFileName,
					O_RDWR | PG_BINARY | XLOG_SYNC_BIT,
					S_IRUSR | S_IWUSR,
					/* suppressError */ false,
					/* atomic operation */ false,
					/*isMirrorRecovery */ false))
	{
		char		path[MAXPGPATH];

		XLogFileName(path, ThisTimeLineID, log, seg);

		ereport(PANIC,
				(errcode_for_file_access(),
		   errmsg("could not open file \"%s\" (log file %u, segment %u): %m",
				  path, log, seg)));
	}
}

/*
 * Close the current logfile segment for writing.
 */
void
XLogFileClose(void)
{
	Assert(MirroredFlatFile_IsActive(&mirroredLogFileOpen));

	/*
	 * posix_fadvise is problematic on many platforms: on older x86 Linux it
	 * just dumps core, and there are reports of problems on PPC platforms as
	 * well.  The following is therefore disabled for the time being. We could
	 * consider some kind of configure test to see if it's safe to use, but
	 * since we lack hard evidence that there's any useful performance gain to
	 * be had, spending time on that seems unprofitable for now.
	 */
#ifdef NOT_USED

	/*
	 * WAL segment files will not be re-read in normal operation, so we advise
	 * OS to release any cached pages.	But do not do so if WAL archiving is
	 * active, because archiver process could use the cache to read the WAL
	 * segment.
	 *
	 * While O_DIRECT works for O_SYNC, posix_fadvise() works for fsync() and
	 * O_SYNC, and some platforms only have posix_fadvise().
	 */
#if defined(HAVE_DECL_POSIX_FADVISE) && defined(POSIX_FADV_DONTNEED)
	if (!XLogArchivingActive())
		posix_fadvise(openLogFile, 0, 0, POSIX_FADV_DONTNEED);
#endif
#endif   /* NOT_USED */

	MirroredFlatFile_Close(&mirroredLogFileOpen);
}

/*
 * Preallocate log files beyond the specified log endpoint, according to
 * the XLOGfile user parameter.
 */
static int
PreallocXlogFiles(XLogRecPtr endptr)
{
	int			nsegsadded = 0;
	uint32		_logId;
	uint32		_logSeg;

	MirroredFlatFileOpen	mirroredOpen;

	bool		use_existent;

	XLByteToPrevSeg(endptr, _logId, _logSeg);
	if ((endptr.xrecoff - 1) % XLogSegSize >=
		(uint32) (0.75 * XLogSegSize))
	{
		NextLogSeg(_logId, _logSeg);
		use_existent = true;
		XLogFileInit(
			&mirroredOpen,
			_logId, _logSeg, &use_existent, true);
		MirroredFlatFile_Close(&mirroredOpen);
		if (!use_existent)
			nsegsadded++;
	}
	return nsegsadded;
}

/*
 * Get the log/seg of the latest removed or recycled WAL segment.
 * Returns 0/0 if no WAL segments have been removed since startup.
 */
void
XLogGetLastRemoved(uint32 *log, uint32 *seg)
{
	/* use volatile pointer to prevent code rearrangement */
	volatile XLogCtlData *xlogctl = XLogCtl;

	SpinLockAcquire(&xlogctl->info_lck);
	*log = xlogctl->lastRemovedLog;
	*seg = xlogctl->lastRemovedSeg;
	SpinLockRelease(&xlogctl->info_lck);
}

/*
 * Update the last removed log/seg pointer in shared memory, to reflect
 * that the given XLOG file has been removed.
 */
static void
UpdateLastRemovedPtr(char *filename)
{
	/* use volatile pointer to prevent code rearrangement */
	volatile XLogCtlData *xlogctl = XLogCtl;
	uint32		tli,
				log,
				seg;

	XLogFromFileName(filename, &tli, &log, &seg);

	SpinLockAcquire(&xlogctl->info_lck);
	if (log > xlogctl->lastRemovedLog ||
		(log == xlogctl->lastRemovedLog && seg > xlogctl->lastRemovedSeg))
	{
		xlogctl->lastRemovedLog = log;
		xlogctl->lastRemovedSeg = seg;
	}
	SpinLockRelease(&xlogctl->info_lck);
}

/*
 * Remove or move offline all log files older or equal to passed log/seg#
 *
 * endptr is current (or recent) end of xlog; this is used to determine
 * whether we want to recycle rather than delete no-longer-wanted log files.
 */
static void
MoveOfflineLogs(uint32 log, uint32 seg, XLogRecPtr endptr,
				int *nsegsremoved, int *nsegsrecycled)
{
	uint32		endlogId;
	uint32		endlogSeg;
	int			max_advance;
	DIR		   *xldir;
	struct dirent *xlde;
	char		lastoff[MAXFNAMELEN];
	char		path[MAXPGPATH];
	char		*xlogDir = NULL;

	*nsegsremoved = 0;
	*nsegsrecycled = 0;

	/*
	 * Initialize info about where to try to recycle to.  We allow recycling
	 * segments up to XLOGfileslop segments beyond the current XLOG location.
	 */
	XLByteToPrevSeg(endptr, endlogId, endlogSeg);
	max_advance = XLOGfileslop;

	xlogDir = makeRelativeToTxnFilespace(XLOGDIR);
	xldir = AllocateDir(xlogDir);
	if (xldir == NULL)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open transaction log directory \"%s\": %m",
						xlogDir)));

	XLogFileName(lastoff, ThisTimeLineID, log, seg);

	while ((xlde = ReadDir(xldir, xlogDir)) != NULL)
	{
		/*
		 * We ignore the timeline part of the XLOG segment identifiers in
		 * deciding whether a segment is still needed.	This ensures that we
		 * won't prematurely remove a segment from a parent timeline. We could
		 * probably be a little more proactive about removing segments of
		 * non-parent timelines, but that would be a whole lot more
		 * complicated.
		 *
		 * We use the alphanumeric sorting property of the filenames to decide
		 * which ones are earlier than the lastoff segment.
		 */
		if (strlen(xlde->d_name) == 24 &&
			strspn(xlde->d_name, "0123456789ABCDEF") == 24 &&
			strcmp(xlde->d_name + 8, lastoff + 8) <= 0)
		{
			if (XLogArchiveCheckDone(xlde->d_name))
			{
				if (snprintf(path, MAXPGPATH, "%s/%s", xlogDir, xlde->d_name) > MAXPGPATH)
				{
					ereport(ERROR, (errmsg("cannot generate filename %s/%s", xlogDir, xlde->d_name)));
				}

				/* Update the last removed location in shared memory first */
				UpdateLastRemovedPtr(xlde->d_name);

				/*
				 * Before deleting the file, see if it can be recycled as a
				 * future log segment.
				 */
				if (InstallXLogFileSegment(&endlogId, &endlogSeg, path,
										   true, &max_advance,
										   true, xlde->d_name))
				{
					ereport(DEBUG2,
							(errmsg("recycled transaction log file \"%s\"",
									xlde->d_name)));
					(*nsegsrecycled)++;
					/* Needn't recheck that slot on future iterations */
					if (max_advance > 0)
					{
						NextLogSeg(endlogId, endlogSeg);
						max_advance--;
					}
				}
				else
				{
					/* No need for any more future segments... */
					ereport(DEBUG2,
							(errmsg("removing transaction log file \"%s\"",
									xlde->d_name)));

					MirroredFlatFile_Drop(
										  XLOGDIR,
										  xlde->d_name,
										  /* suppressError */ true,
										  /*isMirrorRecovery */ false);

					(*nsegsremoved)++;
				}

				XLogArchiveCleanup(xlde->d_name);
			}
		}
	}

	FreeDir(xldir);
	pfree(xlogDir);
}

/*
 * Print log files in the system log.
 *
 */
void
XLogPrintLogNames(void)
{
	DIR		   *xldir;
	struct dirent *xlde;
	int count = 0;
	char *xlogDir = NULL;

	xlogDir = makeRelativeToTxnFilespace(XLOGDIR);
	xldir = AllocateDir(xlogDir);
	if (xldir == NULL)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open transaction log directory \"%s\": %m",
						xlogDir)));

	while ((xlde = ReadDir(xldir, xlogDir)) != NULL)
	{
		if (strlen(xlde->d_name) == 24 &&
			strspn(xlde->d_name, "0123456789ABCDEF") == 24)
		{
			elog(LOG,"found log file \"%s\"",
				 xlde->d_name);
			count++;
		}
	}

	FreeDir(xldir);
	pfree(xlogDir);

	elog(LOG,"%d files found", count);
}

/*
 * Remove previous backup history files.  This also retries creation of
 * .ready files for any backup history files for which XLogArchiveNotify
 * failed earlier.
 */
static void
CleanupBackupHistory(void)
{
	DIR		   *xldir;
	struct dirent *xlde;
	char		path[MAXPGPATH];
	char	*xlogDir = NULL;

	xlogDir = makeRelativeToTxnFilespace(XLOGDIR);
	xldir = AllocateDir(xlogDir);
	if (xldir == NULL)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open transaction log directory \"%s\": %m",
						xlogDir)));

	while ((xlde = ReadDir(xldir, xlogDir)) != NULL)
	{
		if (strlen(xlde->d_name) > 24 &&
			strspn(xlde->d_name, "0123456789ABCDEF") == 24 &&
			strcmp(xlde->d_name + strlen(xlde->d_name) - strlen(".backup"),
				   ".backup") == 0)
		{
			if (XLogArchiveCheckDone(xlde->d_name))
			{
				ereport(DEBUG2,
				(errmsg("removing transaction log backup history file \"%s\"",
						xlde->d_name)));
				if (snprintf(path, MAXPGPATH, "%s/%s", xlogDir, xlde->d_name) > MAXPGPATH)
				{
					elog(LOG, "CleanupBackupHistory: Cannot generate filename %s/%s", xlogDir, xlde->d_name);
				}
				unlink(path);
				XLogArchiveCleanup(xlde->d_name);
			}
		}
	}

	pfree(xlogDir);
	FreeDir(xldir);
}

/*
 * Restore the backup blocks present in an XLOG record, if any.
 *
 * We assume all of the record has been read into memory at *record.
 *
 * Note: when a backup block is available in XLOG, we restore it
 * unconditionally, even if the page in the database appears newer.
 * This is to protect ourselves against database pages that were partially
 * or incorrectly written during a crash.  We assume that the XLOG data
 * must be good because it has passed a CRC check, while the database
 * page might not be.  This will force us to replay all subsequent
 * modifications of the page that appear in XLOG, rather than possibly
 * ignoring them as already applied, but that's not a huge drawback.
 */
static void
RestoreBkpBlocks(XLogRecord *record, XLogRecPtr lsn)
{
	MIRROREDLOCK_BUFMGR_DECLARE;

	Relation	reln;
	Buffer		buffer;
	Page		page;
	BkpBlock	bkpb;
	char	   *blk;
	int			i;

	blk = (char *) XLogRecGetData(record) + record->xl_len;
	for (i = 0; i < XLR_MAX_BKP_BLOCKS; i++)
	{
		if (!(record->xl_info & XLR_SET_BKP_BLOCK(i)))
			continue;

		memcpy(&bkpb, blk, sizeof(BkpBlock));
		blk += sizeof(BkpBlock);

		reln = XLogOpenRelation(bkpb.node);

		// -------- MirroredLock ----------
		MIRROREDLOCK_BUFMGR_LOCK;

		buffer = XLogReadBuffer(reln, bkpb.block, true);
		Assert(BufferIsValid(buffer));
		page = (Page) BufferGetPage(buffer);

		if (bkpb.hole_length == 0)
		{
			memcpy((char *) page, blk, BLCKSZ);
		}
		else
		{
			/* must zero-fill the hole */
			MemSet((char *) page, 0, BLCKSZ);
			memcpy((char *) page, blk, bkpb.hole_offset);
			memcpy((char *) page + (bkpb.hole_offset + bkpb.hole_length),
				   blk + bkpb.hole_offset,
				   BLCKSZ - (bkpb.hole_offset + bkpb.hole_length));
		}

		PageSetLSN(page, lsn);
		PageSetTLI(page, ThisTimeLineID);
		MarkBufferDirty(buffer);
		UnlockReleaseBuffer(buffer);

		MIRROREDLOCK_BUFMGR_UNLOCK;
		// -------- MirroredLock ----------

		blk += BLCKSZ - bkpb.hole_length;
	}
}

/*
 * CRC-check an XLOG record.  We do not believe the contents of an XLOG
 * record (other than to the minimal extent of computing the amount of
 * data to read in) until we've checked the CRCs.
 *
 * We assume all of the record has been read into memory at *record.
 */
static bool
RecordIsValid(XLogRecord *record, XLogRecPtr recptr, int emode)
{
	pg_crc32	crc;
	int			i;
	uint32		len = record->xl_len;
	BkpBlock	bkpb;
	char	   *blk;

	/*
	 * Calculate the crc using the new fast crc32c algorithm
	 */

	/* First the rmgr data */
	crc = crc32c(crc32cInit(), XLogRecGetData(record), len);

	/* Add in the backup blocks, if any */
	blk = (char *) XLogRecGetData(record) + len;
	for (i = 0; i < XLR_MAX_BKP_BLOCKS; i++)
	{
		uint32		blen;

		if (!(record->xl_info & XLR_SET_BKP_BLOCK(i)))
			continue;

		memcpy(&bkpb, blk, sizeof(BkpBlock));
		if (bkpb.hole_offset + bkpb.hole_length > BLCKSZ)
		{
			ereport(emode,
					(errmsg("incorrect hole size in record at %X/%X",
							recptr.xlogid, recptr.xrecoff)));
			return false;
		}
		blen = sizeof(BkpBlock) + BLCKSZ - bkpb.hole_length;
		crc = crc32c(crc, blk, blen);
		blk += blen;
	}

	/* Check that xl_tot_len agrees with our calculation */
	if (blk != (char *) record + record->xl_tot_len)
	{
		ereport(emode,
				(errmsg("incorrect total length in record at %X/%X",
						recptr.xlogid, recptr.xrecoff)));
		return false;
	}

	/* Finally include the record header */
	crc = crc32c(crc, (char *) record + sizeof(pg_crc32),
			   SizeOfXLogRecord - sizeof(pg_crc32));
	crc32cFinish(crc);

	if (!EQ_CRC32(record->xl_crc, crc))
	{
		/*
		 * Ok, the crc failed, but it may be that we have a record using the old crc algorithm.
		 * Re-compute the crc using the old algorithm, and check that.
		 */

		/* First the rmgr data */
		INIT_CRC32(crc);
		COMP_CRC32(crc, XLogRecGetData(record), len);

		/* Add in the backup blocks, if any */
		blk = (char *) XLogRecGetData(record) + len;
		for (i = 0; i < XLR_MAX_BKP_BLOCKS; i++)
		{
			uint32		blen;

			if (!(record->xl_info & XLR_SET_BKP_BLOCK(i)))
				continue;

			memcpy(&bkpb, blk, sizeof(BkpBlock));
			if (bkpb.hole_offset + bkpb.hole_length > BLCKSZ)
			{
				ereport(emode,
						(errmsg("incorrect hole size in record at %X/%X",
								recptr.xlogid, recptr.xrecoff)));
				return false;
			}
			blen = sizeof(BkpBlock) + BLCKSZ - bkpb.hole_length;
			COMP_CRC32(crc, blk, blen);
			blk += blen;
		}

		/* Finally include the record header */
		COMP_CRC32(crc, (char *) record + sizeof(pg_crc32),
				   SizeOfXLogRecord - sizeof(pg_crc32));
		FIN_CRC32(crc);
	}

	if (!EQ_CRC32(record->xl_crc, crc))
	{
		ereport(emode,
		(errmsg("incorrect resource manager data checksum in record at %X/%X",
				recptr.xlogid, recptr.xrecoff)));
		return false;
	}

	return true;
}

/*
 * Verify whether pg_xlog exists
 *
 * It is not the goal of this function to verify the contents of these
 * directories, but to help in cases where someone has performed a
 * copy but omitted pg_xlog from the copy.
 */
static void
ValidateXLOGDirectoryStructure(void)
{
	struct stat stat_buf;

	/* Check for pg_xlog; if it doesn't exist, error out */
	if (stat(makeRelativeToTxnFilespace(XLOGDIR), &stat_buf) != 0 ||
			!S_ISDIR(stat_buf.st_mode))
			ereport(FATAL,
					(errmsg("required WAL directory \"%s\" does not exist",
							XLOGDIR)));
}

/*
 * Open a logfile segment for reading (during recovery).
 * It's assumed to be already available in pg_xlog.
 */
static int
XLogFileRead(uint32 log, uint32 seg, int emode, TimeLineID tli,
			 int source, bool notfoundOk)
{
	char		xlogfname[MAXFNAMELEN];
	char		activitymsg[MAXFNAMELEN + 16];
	char		path[MAXPGPATH];
	int			fd;

	XLogFileName(xlogfname, tli, log, seg);

	switch (source)
	{
		case XLOG_FROM_PG_XLOG:
		case XLOG_FROM_STREAM:
			XLogFilePath(path, tli, log, seg);
			restoredFromArchive = false;
			break;

		default:
			elog(ERROR, "invalid XLogFileRead source %d", source);
	}

	elogif(debug_xlog_record_read, LOG,
		   "xlog file read -- File read request with log %u, seg %u,"
		   "tli %u, source = %s, notfoundok = %s",
		   log, seg, (uint32) tli,
		   source == XLOG_FROM_PG_XLOG ? "xlog" : "stream",
		   notfoundOk ? "true" : "false");

	fd = BasicOpenFile(path, O_RDONLY | PG_BINARY, 0);
	if (fd >= 0)
	{
		/* Success! */
		curFileTLI = tli;

		/*
		 * Report recovery progress in PS display, if we are in
		 * startup process.  There are more cases like Filerep recovery
		 * and Prepare phase where we don't want to report it.
		 */
		if (am_startup)
		{
			snprintf(activitymsg, sizeof(activitymsg), "recovering %s",
					 xlogfname);
			set_ps_display(activitymsg, false);
		}

		/* Track source of data in assorted state variables */
		readSource = source;
		XLogReceiptSource = source;
		/* In FROM_STREAM case, caller tracks receipt time, not me */
		if (source != XLOG_FROM_STREAM)
			XLogReceiptTime = GetCurrentTimestamp();

		elogif(debug_xlog_record_read, LOG,
			   "xlog file read -- Read file %s (log %u, seg %u)",
			   path, log, seg);

		return fd;
	}

	if (errno != ENOENT || !notfoundOk) /* unexpected failure? */
		ereport(PANIC,
				(errcode_for_file_access(),
		   errmsg("could not open file \"%s\" (log file %u, segment %u): %m",
				  path, log, seg)));

	elogif(debug_xlog_record_read, LOG,
		   "xlog file read -- Couldn't read file %s (log %u, seg %u)",
		   path, log, seg);
	return -1;
}


/*
 * Open a logfile segment for reading (during recovery).
 *
 * This version searches for the segment with any TLI listed in expectedTLIs.
 */
static int
XLogFileReadAnyTLI(uint32 log, uint32 seg, int emode, int sources)
{
	char		path[MAXPGPATH];
	ListCell   *cell;
	int			fd;

	/*
	 * Loop looking for a suitable timeline ID: we might need to read any of
	 * the timelines listed in expectedTLIs.
	 *
	 * We expect curFileTLI on entry to be the TLI of the preceding file in
	 * sequence, or 0 if there was no predecessor.	We do not allow curFileTLI
	 * to go backwards; this prevents us from picking up the wrong file when a
	 * parent timeline extends to higher segment numbers than the child we
	 * want to read.
	 */
	foreach(cell, expectedTLIs)
	{
		TimeLineID	tli = (TimeLineID) lfirst_int(cell);

		if (tli < curFileTLI)
			break;				/* don't bother looking at too-old TLIs */

		if (sources & XLOG_FROM_PG_XLOG)
		{
			elogif(debug_xlog_record_read, LOG,
				   "xlog file read (tli) -- requesting a file read (log %u, seg %u)"
				   "with currenttli %d ", log, seg, curFileTLI);

			fd = XLogFileRead(log, seg, emode, tli, XLOG_FROM_PG_XLOG, true);
			if (fd != -1)
				return fd;
		}
	}

	/* Couldn't find it.  For simplicity, complain about front timeline */
	XLogFilePath(path, recoveryTargetTLI, log, seg);
	errno = ENOENT;
	ereport(emode,
			(errcode_for_file_access(),
		   errmsg("could not open file \"%s\" (log file %u, segment %u): %m",
				  path, log, seg)));
	return -1;
}


/*
 * Read the XLOG page containing RecPtr into readBuf (if not read already).
 * Returns true if the page is read successfully.
 *
 * This is responsible for waiting for the requested WAL record to arrive in
 * standby mode.
 *
 * 'emode' specifies the log level used for reporting "file not found" or
 * "end of WAL" situations in standby mode when a trigger file is found.
 * If set to WARNING or below, XLogPageRead() returns false in those situations
 * on higher log levels the ereport() won't return.
 *
 * In standby mode, this only returns false if promotion has been triggered.
 * Otherwise it keeps sleeping and retrying indefinitely.
 */
static bool
XLogPageRead(XLogRecPtr *RecPtr, int emode, bool fetching_ckpt,
			 bool randAccess)
{
	static XLogRecPtr receivedUpto = {0, 0};
	bool		switched_segment = false;
	uint32		targetPageOff;
	uint32		targetRecOff;
	uint32		targetId;
	uint32		targetSeg;
	static pg_time_t last_fail_time = 0;

	XLByteToSeg(*RecPtr, targetId, targetSeg);
	targetPageOff = ((RecPtr->xrecoff % XLogSegSize) / XLOG_BLCKSZ) * XLOG_BLCKSZ;
	targetRecOff = RecPtr->xrecoff % XLOG_BLCKSZ;

	/* Fast exit if we have read the record in the current buffer already */
	if (failedSources == 0 && targetId == readId && targetSeg == readSeg &&
		targetPageOff == readOff && targetRecOff < readLen)
	{
		elogif(debug_xlog_record_read, LOG,
			   "xlog page read -- Requested record %X/%X (targetlogid %u,"
			   "targetset %u, targetpageoff %u, targetrecoff %u) already"
			   "exists in current read buffer",
			   RecPtr->xlogid, RecPtr->xrecoff,
			   targetId, targetSeg, targetPageOff, targetRecOff);

		return true;
	}

	/*
	 * See if we need to switch to a new segment because the requested record
	 * is not in the currently open one.
	 */
	if (readFile >= 0 && !XLByteInSeg(*RecPtr, readId, readSeg))
	{
		elogif(debug_xlog_record_read, LOG,
			   "xlog page read -- Requested record %X/%X does not exist in"
			   "current read xlog file (readlog %u, readseg %u)",
			   RecPtr->xlogid, RecPtr->xrecoff, readId, readSeg);

		close(readFile);
		readFile = -1;
		readSource = 0;
	}

	XLByteToSeg(*RecPtr, readId, readSeg);

	elogif(debug_xlog_record_read, LOG,
		   "xlog page read -- Requested record %X/%X has targetlogid %u, "
		   "targetseg %u, targetpageoff %u, targetrecoff %u",
		   RecPtr->xlogid, RecPtr->xrecoff,
		   targetId, targetSeg, targetPageOff, targetRecOff);

retry:
	/* See if we need to retrieve more data */
	if (readFile < 0 ||
		(readSource == XLOG_FROM_STREAM && !XLByteLT(*RecPtr, receivedUpto)))
	{
		if (StandbyMode)
		{
			/*
			 * In standby mode, wait for the requested record to become
			 * available, via WAL receiver having streamed the record.
			 */
			for (;;)
			{
				if (WalRcvInProgress())
				{
					bool		havedata;

					/*
					 * If we find an invalid record in the WAL streamed from
					 * master, something is seriously wrong. There's little
					 * chance that the problem will just go away, but PANIC is
					 * not good for availability. Disconnect, and retry from
					 * pg_xlog again (That may spawn the Wal receiver again!).
					 * XXX
					 */
					if (failedSources & XLOG_FROM_STREAM)
					{
						elogif(debug_xlog_record_read, LOG,
							   "xlog page read -- Xlog from stream is a failed"
							   "source, hence requesting walreceiver shutdown.");

						ShutdownWalRcv();
						continue;
					}

					/*
					 * WAL receiver is active, so see if new data has arrived.
					 *
					 * We only advance XLogReceiptTime when we obtain fresh
					 * WAL from walreceiver and observe that we had already
					 * processed everything before the most recent "chunk"
					 * that it flushed to disk.  In steady state where we are
					 * keeping up with the incoming data, XLogReceiptTime will
					 * be updated on each cycle.  When we are behind,
					 * XLogReceiptTime will not advance, so the grace time
					 * alloted to conflicting queries will decrease.
					 */
					if (XLByteLT(*RecPtr, receivedUpto))
						havedata = true;
					else
					{
						XLogRecPtr	latestChunkStart;

						receivedUpto = GetWalRcvWriteRecPtr(&latestChunkStart);
						if (XLByteLT(*RecPtr, receivedUpto))
						{
							havedata = true;
							if (!XLByteLT(*RecPtr, latestChunkStart))
							{
								XLogReceiptTime = GetCurrentTimestamp();
								SetCurrentChunkStartTime(XLogReceiptTime);
							}
						}
						else
							havedata = false;
					}

					if (havedata)
					{
						elogif(debug_xlog_record_read, LOG,
							   "xlog page read -- There is enough xlog data to be "
							   "read (receivedupto %X/%X, requestedrec %X/%X)",
							   receivedUpto.xlogid, receivedUpto.xrecoff,
							   RecPtr->xlogid, RecPtr->xrecoff);

						/*
						 * Great, streamed far enough. Open the file if it's
						 * not open already.  Use XLOG_FROM_STREAM so that
						 * source info is set correctly and XLogReceiptTime
						 * isn't changed.
						 */
						if (readFile < 0)
						{
							readFile =
								XLogFileRead(readId, readSeg, PANIC,
											 recoveryTargetTLI,
											 XLOG_FROM_STREAM, false);
							Assert(readFile >= 0);
							switched_segment = true;
						}
						else
						{
							/* just make sure source info is correct... */
							readSource = XLOG_FROM_STREAM;
							XLogReceiptSource = XLOG_FROM_STREAM;
						}
						break;
					}

					/*
					 * Data not here yet, so check for trigger then sleep for
					 * five seconds like in the WAL file polling case below.
					 */
					if (CheckForStandbyTrigger())
					{
						elogif(debug_xlog_record_read, LOG,
							   "xlog page read -- Standby trigger was activated");

						goto retry;
					}

					elogif(debug_xlog_record_read, LOG,
						   "xlog page read -- No xlog data to read as of now. "
						   "Will Wait on latch till some event occurs");

					/*
					 * Wait for more WAL to arrive, or timeout to be reached
					 */
					WaitLatch(&XLogCtl->recoveryWakeupLatch,
							  WL_LATCH_SET | WL_TIMEOUT,
							  5000L);
					ResetLatch(&XLogCtl->recoveryWakeupLatch);
				}
				else
				{
					int			sources;
					pg_time_t	now;

					if (readFile >= 0)
					{
						close(readFile);
						readFile = -1;
					}

					/* Reset curFileTLI if random fetch. */
					if (randAccess)
						curFileTLI = 0;

					/* Read an existing file from pg_xlog. */
					sources = XLOG_FROM_PG_XLOG;
					if (!(sources & ~failedSources))
					{
						/*
						 * Check if we have been asked to be promoted. If yes,
						 * no use of requesting a new WAL receiver
						 */
						if (CheckForStandbyTrigger())
							goto triggered;

						/*
						 * We've exhausted all options for retrieving the
						 * file. Retry.
						 */
						failedSources = 0;

						elogif(debug_xlog_record_read, LOG,
							   "xlog page read -- All read sources have failed. So, retry.");

						/*
						 * If it hasn't been long since last attempt, sleep to
						 * avoid busy-waiting.
						 */
						now = (pg_time_t) time(NULL);
						if ((now - last_fail_time) < 5)
						{
							pg_usleep(1000000L * (5 - (now - last_fail_time)));
							now = (pg_time_t) time(NULL);
						}
						last_fail_time = now;

						/*
						 * If primary_conninfo is set, launch walreceiver to
						 * try to stream the missing WAL.
						 *
						 * If fetching_ckpt is TRUE, RecPtr points to the
						 * initial checkpoint location. In that case, we use
						 * RedoStartLSN as the streaming start position
						 * instead of RecPtr, so that when we later jump
						 * backwards to start redo at RedoStartLSN, we will
						 * have the logs streamed already.
						 */
						if (PrimaryConnInfo)
						{
							RequestXLogStreaming(
									  fetching_ckpt ? RedoStartLSN : *RecPtr,
												 PrimaryConnInfo);
							continue;
						}
					}
					/* Don't try to read from a source that just failed */
					sources &= ~failedSources;
					readFile = XLogFileReadAnyTLI(readId, readSeg, DEBUG2,
												  sources);
					switched_segment = true;
					if (readFile >= 0)
						break;

					/*
					 * Nope, not found in pg_xlog.
					 */
					failedSources |= sources;

					/*
					 * Check to see if the trigger file exists. Note that we
					 * do this only after failure, so when you create the
					 * trigger file, we still finish replaying as much as we
					 * can from pg_xlog before failover.
					 */
					if (CheckForStandbyTrigger())
						goto triggered;
				}

				/*
				 * This possibly-long loop needs to handle interrupts of
				 * startup process.
				 */
				HandleStartupProcInterrupts();
			}
		}
		else
		{
			/* In crash recovery. */
			if (readFile < 0)
			{
				int			sources;

				/* Reset curFileTLI if random fetch. */
				if (randAccess)
					curFileTLI = 0;

				sources = XLOG_FROM_PG_XLOG;

				readFile = XLogFileReadAnyTLI(readId, readSeg, emode,
											sources);
				switched_segment = true;
				if (readFile < 0)
					return false;
			}
		}
	}

	/*
	 * At this point, we have the right segment open and if we're streaming we
	 * know the requested record is in it.
	 */
	Assert(readFile != -1);

	/*
	 * If the current segment is being streamed from master, calculate how
	 * much of the current page we have received already. We know the
	 * requested record has been received, but this is for the benefit of
	 * future calls, to allow quick exit at the top of this function.
	 */
	if (readSource == XLOG_FROM_STREAM)
	{
		if (RecPtr->xlogid != receivedUpto.xlogid ||
			(RecPtr->xrecoff / XLOG_BLCKSZ) != (receivedUpto.xrecoff / XLOG_BLCKSZ))
		{
			readLen = XLOG_BLCKSZ;
		}
		else
			readLen = receivedUpto.xrecoff % XLogSegSize - targetPageOff;
	}
	else
		readLen = XLOG_BLCKSZ;

	if (switched_segment && targetPageOff != 0)
	{
		/*
		 * Whenever switching to a new WAL segment, we read the first page of
		 * the file and validate its header, even if that's not where the
		 * target record is.  This is so that we can check the additional
		 * identification info that is present in the first page's "long"
		 * header.
		 */
		readOff = 0;
		if (read(readFile, readBuf, XLOG_BLCKSZ) != XLOG_BLCKSZ)
		{
			ereport(emode,
					(errcode_for_file_access(),
					 errmsg("could not read from log file %u, segment %u, offset %u: %m",
							readId, readSeg, readOff)));
			goto next_record_is_invalid;
		}
		if (!ValidXLOGHeader((XLogPageHeader) readBuf, emode, true))
		{
			elogif(debug_xlog_record_read, LOG,
				   "xlog page read -- xlog page header invalid");
			goto next_record_is_invalid;
		}
	}

	/* Read the requested page */
	readOff = targetPageOff;
	if (lseek(readFile, (off_t) readOff, SEEK_SET) < 0)
	{
		ereport(emode,
				(errcode_for_file_access(),
		 errmsg("could not seek in log file %u, segment %u to offset %u: %m",
				readId, readSeg, readOff)));
		goto next_record_is_invalid;
	}
	if (read(readFile, readBuf, XLOG_BLCKSZ) != XLOG_BLCKSZ)
	{
		ereport(emode,
				(errcode_for_file_access(),
		 errmsg("could not read from log file %u, segment %u, offset %u: %m",
				readId, readSeg, readOff)));
		goto next_record_is_invalid;
	}
	if (!ValidXLOGHeader((XLogPageHeader) readBuf, emode, false))
	{
		elogif(debug_xlog_record_read, LOG,
			   "xlog page read -- xlog page header invalid");
		goto next_record_is_invalid;
	}

	Assert(targetId == readId);
	Assert(targetSeg == readSeg);
	Assert(targetPageOff == readOff);
	Assert(targetRecOff < readLen);

	return true;

next_record_is_invalid:

	elogif(debug_xlog_record_read, LOG,
		   "xlog page read -- next record is invalid.");

	failedSources |= readSource;

	if (readFile >= 0)
		close(readFile);
	readFile = -1;
	readLen = 0;
	readSource = 0;

	/* In standby-mode, keep trying */
	if (StandbyMode)
		goto retry;
	else
		return false;

triggered:
	if (readFile >= 0)
		close(readFile);
	readFile = -1;
	readLen = 0;
	readSource = 0;

	return false;
}

/*
 * Attempt to read an XLOG record.
 *
 * If RecPtr is not NULL, try to read a record at that position.  Otherwise
 * try to read a record just after the last one previously read.
 *
 * If no valid record is available, returns NULL, or fails if emode is PANIC.
 * (emode must be either PANIC or LOG.)
 *
 * The record is copied into readRecordBuf, so that on successful return,
 * the returned record pointer always points there.
 */
XLogRecord *
XLogReadRecord(XLogRecPtr *RecPtr, bool fetching_ckpt, int emode)
{
	XLogRecord *record;
	char	   *buffer;
	XLogRecPtr	tmpRecPtr = EndRecPtr;
	bool		randAccess = false;
	uint32		len,
				total_len;
	uint32		targetRecOff;
	uint32		pageHeaderSize;

	if (readBuf == NULL)
	{
		/*
		 * First time through, permanently allocate readBuf.  We do it this
		 * way, rather than just making a static array, for two reasons: (1)
		 * no need to waste the storage in most instantiations of the backend;
		 * (2) a static char array isn't guaranteed to have any particular
		 * alignment, whereas malloc() will provide MAXALIGN'd storage.
		 */
		readBuf = (char *) malloc(XLOG_BLCKSZ);
		if(!readBuf)
			ereport(PANIC, (errmsg("Cannot allocate memory for read log record. Out of Memory")));
	}

	if (RecPtr == NULL)
	{
		RecPtr = &tmpRecPtr;

		/*
		 * RecPtr is pointing to end+1 of the previous WAL record. We must
		 * advance it if necessary to where the next record starts.  First,
		 * align to next page if no more records can fit on the current page.
		 */
		if (nextRecord == NULL)
		{
			/* align old recptr to next page */

			if (tmpRecPtr.xrecoff % XLOG_BLCKSZ != 0)
				tmpRecPtr.xrecoff += (XLOG_BLCKSZ - tmpRecPtr.xrecoff % XLOG_BLCKSZ);

			if (tmpRecPtr.xrecoff >= XLogFileSize)
			{
				(tmpRecPtr.xlogid)++;
				tmpRecPtr.xrecoff = 0;
			}
		}
		/* We will account for page header size below */
	}
	else
	{
		/*
		 * In this case, the passed-in record pointer should already be
		 * pointing to a valid record starting position.
		 */
		if (!XRecOffIsValid(RecPtr->xrecoff))
			ereport(PANIC,
					(errmsg("invalid record offset at %X/%X",
							RecPtr->xlogid, RecPtr->xrecoff)));

		/*
		 * Since we are going to a random position in WAL, forget any prior
		 * state about what timeline we were in, and allow it to be any
		 * timeline in expectedTLIs.  We also set a flag to allow curFileTLI
		 * to go backwards (but we can't reset that variable right here, since
		 * we might not change files at all).
		 */
		lastPageTLI = 0;		/* see comment in ValidXLOGHeader */
		lastSegmentTLI = 0;
		randAccess = true;		/* allow curFileTLI to go backwards too */
	}

	/* This is the first try to read this page. */
	failedSources = 0;
retry:
	/* Read the page containing the record */
	if (!XLogPageRead(RecPtr, emode, fetching_ckpt, randAccess))
	{
		/*
		 * In standby mode, XLogPageRead returning false means that promotion
		 * has been triggered.
		 */
		if (StandbyMode)
			return NULL;
		else
			goto next_record_is_invalid;
	}

	/* *********Above this xlogpageread should called ***********/
	pageHeaderSize = XLogPageHeaderSize((XLogPageHeader) readBuf);
	targetRecOff = RecPtr->xrecoff % XLOG_BLCKSZ;
	if (targetRecOff == 0)
	{
		/*
		 * Can only get here in the continuing-from-prev-page case, because
		 * XRecOffIsValid eliminated the zero-page-offset case otherwise. Need
		 * to skip over the new page's header.
		 */
		Assert(RecPtr == &tmpRecPtr);
		tmpRecPtr.xrecoff += pageHeaderSize;
		targetRecOff = pageHeaderSize;
	}
	else if (targetRecOff < pageHeaderSize)
	{
		ereport(emode,
				(errmsg("invalid record offset at %X/%X",
						RecPtr->xlogid, RecPtr->xrecoff)));
		goto next_record_is_invalid;
	}
	if ((((XLogPageHeader) readBuf)->xlp_info & XLP_FIRST_IS_CONTRECORD) &&
		targetRecOff == pageHeaderSize)
	{
		ereport(emode,
				(errmsg("contrecord is requested by %X/%X",
						RecPtr->xlogid, RecPtr->xrecoff)));
		goto next_record_is_invalid;
	}
	record = (XLogRecord *) ((char *) readBuf + RecPtr->xrecoff % XLOG_BLCKSZ);

	/*
	 * xl_len == 0 is bad data for everything except XLOG SWITCH, where it is
	 * required.
	 */
	if (record->xl_rmid == RM_XLOG_ID && record->xl_info == XLOG_SWITCH)
	{
		if (record->xl_len != 0)
		{
			ereport(emode,
					(errmsg("invalid xlog switch record at %X/%X",
							RecPtr->xlogid, RecPtr->xrecoff)));
			goto next_record_is_invalid;
		}
	}
	else if (record->xl_len == 0)
	{
		ereport(emode,
				(errmsg("record with zero length at %X/%X",
						RecPtr->xlogid, RecPtr->xrecoff)));
		goto next_record_is_invalid;
	}
	if (record->xl_tot_len < SizeOfXLogRecord + record->xl_len ||
		record->xl_tot_len > SizeOfXLogRecord + record->xl_len +
		XLR_MAX_BKP_BLOCKS * (sizeof(BkpBlock) + BLCKSZ))
	{
		ereport(emode,
				(errmsg("invalid record length at %X/%X",
						RecPtr->xlogid, RecPtr->xrecoff)));
		goto next_record_is_invalid;
	}
	if (record->xl_rmid > RM_MAX_ID)
	{
		ereport(emode,
				(errmsg("invalid resource manager ID %u at %X/%X",
						record->xl_rmid, RecPtr->xlogid, RecPtr->xrecoff)));
		goto next_record_is_invalid;
	}
	if (randAccess)
	{
		/*
		 * We can't exactly verify the prev-link, but surely it should be less
		 * than the record's own address.
		 */
		if (!XLByteLT(record->xl_prev, *RecPtr))
		{
			ereport(emode,
					(errmsg("record with incorrect prev-link %X/%X at %X/%X",
							record->xl_prev.xlogid, record->xl_prev.xrecoff,
							RecPtr->xlogid, RecPtr->xrecoff)));
			goto next_record_is_invalid;
		}
	}
	else
	{
		/*
		 * Record's prev-link should exactly match our previous location. This
		 * check guards against torn WAL pages where a stale but valid-looking
		 * WAL record starts on a sector boundary.
		 */
		if (!XLByteEQ(record->xl_prev, ReadRecPtr))
		{
			ereport(emode,
					(errmsg("record with incorrect prev-link %X/%X at %X/%X",
							record->xl_prev.xlogid, record->xl_prev.xrecoff,
							RecPtr->xlogid, RecPtr->xrecoff)));
			goto next_record_is_invalid;
		}
	}

	/*
	 * Allocate or enlarge readRecordBuf as needed.  To avoid useless small
	 * increases, round its size to a multiple of XLOG_BLCKSZ, and make sure
	 * it's at least 4*Max(BLCKSZ, XLOG_BLCKSZ) to start with.  (That is
	 * enough for all "normal" records, but very large commit or abort records
	 * might need more space.)
	 */
	total_len = record->xl_tot_len;
	if (total_len > readRecordBufSize)
	{
		uint32		newSize = total_len;

		newSize += XLOG_BLCKSZ - (newSize % XLOG_BLCKSZ);
		newSize = Max(newSize, 4 * Max(BLCKSZ, XLOG_BLCKSZ));
		if (readRecordBuf)
			free(readRecordBuf);
		readRecordBuf = (char *) malloc(newSize);
		if (!readRecordBuf)
		{
			readRecordBufSize = 0;
			/* We treat this as a "bogus data" condition */
			ereport(emode,
					(errmsg("record length %u at %X/%X too long",
							total_len, RecPtr->xlogid, RecPtr->xrecoff)));
			goto next_record_is_invalid;
		}
		readRecordBufSize = newSize;
	}

	buffer = readRecordBuf;
	nextRecord = NULL;
	len = XLOG_BLCKSZ - RecPtr->xrecoff % XLOG_BLCKSZ;
	if (total_len > len)
	{
		/* Need to reassemble record */
		XLogContRecord *contrecord;
		XLogRecPtr	pagelsn;
		uint32		gotlen = len;

		/* Initialize pagelsn to the beginning of the page this record is on */
		pagelsn = *RecPtr;
		pagelsn.xrecoff = (pagelsn.xrecoff / XLOG_BLCKSZ) * XLOG_BLCKSZ;

		memcpy(buffer, record, len);
		record = (XLogRecord *) buffer;
		buffer += len;
		for (;;)
		{
			/* Calculate pointer to beginning of next page */
			pagelsn.xrecoff += XLOG_BLCKSZ;
			if (pagelsn.xrecoff >= XLogFileSize)
			{
				(pagelsn.xlogid)++;
				pagelsn.xrecoff = 0;
			}
			/* Wait for the next page to become available */
			if (!XLogPageRead(&pagelsn, emode, false, false))
			{
				/*
				 * In standby-mode, XLogPageRead returning false means that
				 * promotion has been triggered.
				 */
				if (StandbyMode)
					return NULL;
				else
					goto next_record_is_invalid;
			}

			if (!(((XLogPageHeader) readBuf)->xlp_info & XLP_FIRST_IS_CONTRECORD))
			{
				ereport(emode,
						(errmsg("there is no contrecord flag in log file %u, segment %u, offset %u",
								readId, readSeg, readOff)));
				goto next_record_is_invalid;
			}
			pageHeaderSize = XLogPageHeaderSize((XLogPageHeader) readBuf);
			contrecord = (XLogContRecord *) ((char *) readBuf + pageHeaderSize);
			if (contrecord->xl_rem_len == 0 ||
				total_len != (contrecord->xl_rem_len + gotlen))
			{
				ereport(emode,
						(errmsg("invalid contrecord length %u in log file %u, segment %u, offset %u",
								contrecord->xl_rem_len,
								readId, readSeg, readOff)));
				goto next_record_is_invalid;
			}
			len = XLOG_BLCKSZ - pageHeaderSize - SizeOfXLogContRecord;
			if (contrecord->xl_rem_len > len)
			{
				memcpy(buffer, (char *) contrecord + SizeOfXLogContRecord, len);
				gotlen += len;
				buffer += len;
				continue;
			}
			memcpy(buffer, (char *) contrecord + SizeOfXLogContRecord,
				   contrecord->xl_rem_len);
			break;
		}
		if (!RecordIsValid(record, *RecPtr, emode))
			goto next_record_is_invalid;
		pageHeaderSize = XLogPageHeaderSize((XLogPageHeader) readBuf);
		if (XLOG_BLCKSZ - SizeOfXLogRecord >= pageHeaderSize +
			MAXALIGN(SizeOfXLogContRecord + contrecord->xl_rem_len))
		{
			nextRecord = (XLogRecord *) ((char *) contrecord +
					MAXALIGN(SizeOfXLogContRecord + contrecord->xl_rem_len));
		}
		EndRecPtr.xlogid = readId;
		EndRecPtr.xrecoff = readSeg * XLogSegSize + readOff +
			pageHeaderSize +
			MAXALIGN(SizeOfXLogContRecord + contrecord->xl_rem_len);
		ReadRecPtr = *RecPtr;
		/* needn't worry about XLOG SWITCH, it can't cross page boundaries */
		return record;
	}

	/* Record does not cross a page boundary */
	if (!RecordIsValid(record, *RecPtr, emode))
		goto next_record_is_invalid;
	if (XLOG_BLCKSZ - SizeOfXLogRecord >= RecPtr->xrecoff % XLOG_BLCKSZ +
		MAXALIGN(total_len))
		nextRecord = (XLogRecord *) ((char *) record + MAXALIGN(total_len));
	EndRecPtr.xlogid = RecPtr->xlogid;
	EndRecPtr.xrecoff = RecPtr->xrecoff + MAXALIGN(total_len);
	ReadRecPtr = *RecPtr;
	memcpy(buffer, record, total_len);

	/*
	 * Special processing if it's an XLOG SWITCH record
	 */
	if (record->xl_rmid == RM_XLOG_ID && record->xl_info == XLOG_SWITCH)
	{
		/* Pretend it extends to end of segment */
		EndRecPtr.xrecoff += XLogSegSize - 1;
		EndRecPtr.xrecoff -= EndRecPtr.xrecoff % XLogSegSize;
		nextRecord = NULL;		/* definitely not on same page */

		/*
		 * Pretend that readBuf contains the last page of the segment. This is
		 * just to avoid Assert failure in StartupXLOG if XLOG ends with this
		 * segment.
		 */
		readOff = XLogSegSize - XLOG_BLCKSZ;
	}

	elogif(debug_xlog_record_read, LOG,
		   "xlog read record -- Read record %X/%X successfully with endrecptr %X/%X",
		   ReadRecPtr.xlogid, ReadRecPtr.xrecoff,
		   EndRecPtr.xlogid, EndRecPtr.xrecoff);

	return (XLogRecord *) buffer;

next_record_is_invalid:

	elogif(debug_xlog_record_read, LOG,
		   "xlog record read -- next record is invalid.");

	failedSources |= readSource;

	if (readFile >= 0)
	{
		close(readFile);
		readFile = -1;
	}

	nextRecord = NULL;

	/* In standby-mode, keep trying */
	if (StandbyMode && !CheckForStandbyTrigger())
		goto retry;
	else
		return NULL;
}

/*
 * Close, re-set and clean all the necessary resources used during reading
 * XLog records.
 */
void
XLogCloseReadRecord(void)
{
	if (readFile >= 0)
	{
		close(readFile);
		readFile = -1;
	}
	else
		Assert(readFile == -1);

	if (readBuf)
	{
		free(readBuf);
		readBuf = NULL;
	}

	if (readRecordBuf)
	{
		free(readRecordBuf);
		readRecordBuf = NULL;
	}

	readId = 0;
	readSeg = 0;
	readOff = 0;
	readLen = 0;
	readRecordBufSize = 0;
	nextRecord = NULL;

	memset(&ReadRecPtr, 0, sizeof(XLogRecPtr));
	memset(&EndRecPtr, 0, sizeof(XLogRecPtr));

	elog((Debug_print_qd_mirroring ? LOG : DEBUG1),"close read record");
}

/*
 * Check whether the xlog header of a page just read in looks valid.
 *
 * This is just a convenience subroutine to avoid duplicated code in
 * ReadRecord.	It's not intended for use from anywhere else.
 */
static bool
ValidXLOGHeader(XLogPageHeader hdr, int emode, bool segmentonly)
{
	XLogRecPtr	recaddr;

	if (hdr->xlp_magic != XLOG_PAGE_MAGIC)
	{
		ereport(emode,
				(errmsg("invalid magic number %04X in log file %u, segment %u, offset %u",
						hdr->xlp_magic, readId, readSeg, readOff)));
		return false;
	}
	if ((hdr->xlp_info & ~XLP_ALL_FLAGS) != 0)
	{
		ereport(emode,
				(errmsg("invalid info bits %04X in log file %u, segment %u, offset %u",
						hdr->xlp_info, readId, readSeg, readOff)));
		return false;
	}
	if (hdr->xlp_info & XLP_LONG_HEADER)
	{
		XLogLongPageHeader longhdr = (XLogLongPageHeader) hdr;

		if (longhdr->xlp_sysid != ControlFile->system_identifier)
		{
			char		fhdrident_str[32];
			char		sysident_str[32];

			/*
			 * Format sysids separately to keep platform-dependent format code
			 * out of the translatable message string.
			 */
			snprintf(fhdrident_str, sizeof(fhdrident_str), UINT64_FORMAT,
					 longhdr->xlp_sysid);
			snprintf(sysident_str, sizeof(sysident_str), UINT64_FORMAT,
					 ControlFile->system_identifier);
			ereport(emode,
					(errmsg("WAL file is from different system"),
					 errdetail("WAL file SYSID is %s, pg_control SYSID is %s",
							   fhdrident_str, sysident_str)));
			return false;
		}
		if (longhdr->xlp_seg_size != XLogSegSize)
		{
			ereport(emode,
					(errmsg("WAL file is from different system"),
					 errdetail("Incorrect XLOG_SEG_SIZE in page header.")));
			return false;
		}
		if (longhdr->xlp_xlog_blcksz != XLOG_BLCKSZ)
		{
			ereport(emode,
					(errmsg("WAL file is from different system"),
					 errdetail("Incorrect XLOG_BLCKSZ in page header.")));
			return false;
		}
	}
	else if (readOff == 0)
	{
		/* hmm, first page of file doesn't have a long header? */
		ereport(emode,
				(errmsg("invalid info bits %04X in log file %u, segment %u, offset %u",
						hdr->xlp_info, readId, readSeg, readOff)));
		return false;
	}

	recaddr.xlogid = readId;
	recaddr.xrecoff = readSeg * XLogSegSize + readOff;
	if (!XLByteEQ(hdr->xlp_pageaddr, recaddr))
	{
		ereport(emode,
				(errmsg("unexpected pageaddr %X/%X in log file %u, segment %u, offset %u",
						hdr->xlp_pageaddr.xlogid, hdr->xlp_pageaddr.xrecoff,
						readId, readSeg, readOff)));
		return false;
	}

	/*
	 * Check page TLI is one of the expected values.
	 */
	if (!list_member_int(expectedTLIs, (int) hdr->xlp_tli))
	{
		ereport(emode,
				(errmsg("unexpected timeline ID %u in log file %u, segment %u, offset %u",
						hdr->xlp_tli,
						readId, readSeg, readOff)));
		return false;
	}

	/*
	 * Since child timelines are always assigned a TLI greater than their
	 * immediate parent's TLI, we should never see TLI go backwards across
	 * successive pages of a consistent WAL sequence.
	 *
	 * Of course this check should only be applied when advancing sequentially
	 * across pages; therefore ReadRecord resets lastPageTLI and
	 * lastSegmentTLI to zero when going to a random page.
	 *
	 * Sometimes we re-open a segment that's already been partially replayed.
	 * In that case we cannot perform the normal TLI check: if there is a
	 * timeline switch within the segment, the first page has a smaller TLI
	 * than later pages following the timeline switch, and we might've read
	 * them already. As a weaker test, we still check that it's not smaller
	 * than the TLI we last saw at the beginning of a segment. Pass
	 * segmentonly = true when re-validating the first page like that, and the
	 * page you're actually interested in comes later.
	 */
	if (hdr->xlp_tli < (segmentonly ? lastSegmentTLI : lastPageTLI))
	{
		ereport(emode,
				(errmsg("out-of-sequence timeline ID %u (after %u) in log file %u, segment %u, offset %u",
						hdr->xlp_tli, lastPageTLI,
						readId, readSeg, readOff)));
		return false;
	}
	lastPageTLI = hdr->xlp_tli;
	if (readOff == 0)
		lastSegmentTLI = hdr->xlp_tli;

	return true;
}

/*
 * Try to read a timeline's history file.
 *
 * If successful, return the list of component TLIs (the given TLI followed by
 * its ancestor TLIs).	If we can't find the history file, assume that the
 * timeline has no parents, and return a list of just the specified timeline
 * ID.
 */
List *
XLogReadTimeLineHistory(TimeLineID targetTLI)
{
	List	   *result;
	char		path[MAXPGPATH];
	char		fline[MAXPGPATH];
	FILE	   *fd;

	/* Timeline 1 does not have a history file, so no need to check */
	if (targetTLI == 1)
		return list_make1_int((int) targetTLI);

	TLHistoryFilePath(path, targetTLI);

	fd = AllocateFile(path, "r");
	if (fd == NULL)
	{
		if (errno != ENOENT)
			ereport(FATAL,
					(errcode_for_file_access(),
					 errmsg("could not open file \"%s\": %m", path)));
		/* Not there, so assume no parents */
		return list_make1_int((int) targetTLI);
	}

	result = NIL;

	/*
	 * Parse the file...
	 */
	while (fgets(fline, MAXPGPATH, fd) != NULL)
	{
		/* skip leading whitespace and check for # comment */
		char	   *ptr;
		char	   *endptr;
		TimeLineID	tli;

		for (ptr = fline; *ptr; ptr++)
		{
			if (!isspace((unsigned char) *ptr))
				break;
		}
		if (*ptr == '\0' || *ptr == '#')
			continue;

		/* expect a numeric timeline ID as first field of line */
		tli = (TimeLineID) strtoul(ptr, &endptr, 0);
		if (endptr == ptr)
			ereport(FATAL,
					(errmsg("syntax error in history file: %s", fline),
					 errhint("Expected a numeric timeline ID.")));

		if (result &&
			tli <= (TimeLineID) linitial_int(result))
			ereport(FATAL,
					(errmsg("invalid data in history file: %s", fline),
				   errhint("Timeline IDs must be in increasing sequence.")));

		/* Build list with newest item first */
		result = lcons_int((int) tli, result);

		/* we ignore the remainder of each line */
	}

	FreeFile(fd);

	if (result &&
		targetTLI <= (TimeLineID) linitial_int(result))
		ereport(FATAL,
				(errmsg("invalid data in history file \"%s\"", path),
			errhint("Timeline IDs must be less than child timeline's ID.")));

	result = lcons_int((int) targetTLI, result);

	ereport(DEBUG3,
			(errmsg_internal("history of timeline %u is %s",
							 targetTLI, nodeToString(result))));

	return result;
}

/*
 * Probe whether a timeline history file exists for the given timeline ID
 */
static bool
existsTimeLineHistory(TimeLineID probeTLI)
{
	char		path[MAXPGPATH];
	FILE	   *fd;

	TLHistoryFilePath(path, probeTLI);

	fd = AllocateFile(path, "r");
	if (fd != NULL)
	{
		FreeFile(fd);
		return true;
	}
	else
	{
		if (errno != ENOENT)
			ereport(FATAL,
					(errcode_for_file_access(),
					 errmsg("could not open file \"%s\": %m", path)));
		return false;
	}
}

/*
 * Find the newest existing timeline, assuming that startTLI exists.
 *
 * Note: while this is somewhat heuristic, it does positively guarantee
 * that (result + 1) is not a known timeline, and therefore it should
 * be safe to assign that ID to a new timeline.
 */
static TimeLineID
findNewestTimeLine(TimeLineID startTLI)
{
	TimeLineID	newestTLI;
	TimeLineID	probeTLI;

	/*
	 * The algorithm is just to probe for the existence of timeline history
	 * files.  XXX is it useful to allow gaps in the sequence?
	 */
	newestTLI = startTLI;

	for (probeTLI = startTLI + 1;; probeTLI++)
	{
		if (existsTimeLineHistory(probeTLI))
		{
			newestTLI = probeTLI;		/* probeTLI exists */
		}
		else
		{
			/* doesn't exist, assume we're done */
			break;
		}
	}

	return newestTLI;
}

/*
 * Create a new timeline history file.
 *
 *	newTLI: ID of the new timeline
 *	parentTLI: ID of its immediate parent
 *	endTLI et al: ID of the last used WAL file, for annotation purposes
 *
 * Currently this is only used during recovery, and so there are no locking
 * considerations.	But we should be just as tense as XLogFileInit to avoid
 * emplacing a bogus file.
 */
static void
writeTimeLineHistory(TimeLineID newTLI, TimeLineID parentTLI,
					 TimeLineID endTLI, uint32 endLogId, uint32 endLogSeg)
{
	char		path[MAXPGPATH];
	char		tmppath[MAXPGPATH];
	char		histfname[MAXFNAMELEN];
	char		xlogfname[MAXFNAMELEN];
	char		buffer[BLCKSZ];
	int			srcfd;
	int			fd;
	int			nbytes;
	char		*xlogDir = NULL;

	Assert(newTLI > parentTLI); /* else bad selection of newTLI */

	/*
	 * Write into a temp file name.
	 */
	xlogDir = makeRelativeToTxnFilespace(XLOGDIR);
	if (snprintf(tmppath, MAXPGPATH, "%s/xlogtemp.%d", xlogDir, (int) getpid()) > MAXPGPATH)
	{
		ereport(ERROR, (errmsg("cannot generate filename %s/xlogtemp.%d", xlogDir, (int) getpid())));
	}
	pfree(xlogDir);
	unlink(tmppath);

	/* do not use XLOG_SYNC_BIT here --- want to fsync only at end of fill */
	fd = BasicOpenFile(tmppath, O_RDWR | O_CREAT | O_EXCL,
					   S_IRUSR | S_IWUSR);
	if (fd < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not create file \"%s\": %m", tmppath)));

	TLHistoryFilePath(path, parentTLI);

	srcfd = BasicOpenFile(path, O_RDONLY, 0);
	if (srcfd < 0)
	{
		if (errno != ENOENT)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not open file \"%s\": %m", path)));
		/* Not there, so assume parent has no parents */
	}
	else
	{
		for (;;)
		{
			errno = 0;
			nbytes = (int) read(srcfd, buffer, sizeof(buffer));
			if (nbytes < 0 || errno != 0)
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not read file \"%s\": %m", path)));
			if (nbytes == 0)
				break;
			errno = 0;
			if ((int) write(fd, buffer, nbytes) != nbytes)
			{
				int			save_errno = errno;

				/*
				 * If we fail to make the file, delete it to release disk
				 * space
				 */
				unlink(tmppath);

				/*
				 * if write didn't set errno, assume problem is no disk space
				 */
				errno = save_errno ? save_errno : ENOSPC;

				ereport(ERROR,
						(errcode_for_file_access(),
					 errmsg("could not write to file \"%s\": %m", tmppath)));
			}
		}
		close(srcfd);
	}

	/*
	 * Append one line with the details of this timeline split.
	 *
	 * If we did have a parent file, insert an extra newline just in case the
	 * parent file failed to end with one.
	 */
	XLogFileName(xlogfname, endTLI, endLogId, endLogSeg);

	snprintf(buffer, sizeof(buffer),
			 "%s%u\t%s\t%s transaction %u at %s\n",
			 (srcfd < 0) ? "" : "\n",
			 parentTLI,
			 xlogfname,
			 recoveryStopAfter ? "after" : "before",
			 recoveryStopXid,
			 str_time(recoveryStopTime));

	nbytes = strlen(buffer);
	errno = 0;
	if ((int) write(fd, buffer, nbytes) != nbytes)
	{
		int			save_errno = errno;

		/*
		 * If we fail to make the file, delete it to release disk space
		 */
		unlink(tmppath);
		/* if write didn't set errno, assume problem is no disk space */
		errno = save_errno ? save_errno : ENOSPC;

		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not write to file \"%s\": %m", tmppath)));
	}

	if (pg_fsync(fd) != 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not fsync file \"%s\": %m", tmppath)));

	if (close(fd))
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not close file \"%s\": %m", tmppath)));


	/*
	 * Now move the completed history file into place with its final name.
	 */
	TLHistoryFilePath(path, newTLI);

	/*
	 * Prefer link() to rename() here just to be really sure that we don't
	 * overwrite an existing logfile.  However, there shouldn't be one, so
	 * rename() is an acceptable substitute except for the truly paranoid.
	 */
#if HAVE_WORKING_LINK
	if (link(tmppath, path) < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not link file \"%s\" to \"%s\": %m",
						tmppath, path)));
	unlink(tmppath);
#else
	if (rename(tmppath, path) < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not rename file \"%s\" to \"%s\": %m",
						tmppath, path)));
#endif

	/* The history file can be archived immediately. */
	TLHistoryFileName(histfname, newTLI);
	XLogArchiveNotify(histfname);
}

static void
ControlFileWatcherSaveInitial(void)
{
	ControlFileWatcher->current_checkPointLoc = ControlFile->checkPoint;
	ControlFileWatcher->current_prevCheckPointLoc = ControlFile->prevCheckPoint;
	ControlFileWatcher->current_checkPointCopy_redo = ControlFile->checkPointCopy.redo;

	if (Debug_print_control_checkpoints)
		elog(LOG,"pg_control checkpoint: initial values (checkpoint loc %s, previous loc %s, copy's redo loc %s)",
			 XLogLocationToString_Long(&ControlFile->checkPoint),
			 XLogLocationToString2_Long(&ControlFile->prevCheckPoint),
			 XLogLocationToString3_Long(&ControlFile->checkPointCopy.redo));

	ControlFileWatcher->watcherInitialized = true;
}

static void
ControlFileWatcherCheckForChange(void)
{
	XLogRecPtr  writeLoc;
	XLogRecPtr  flushedLoc;

	if (!XLByteEQ(ControlFileWatcher->current_checkPointLoc,ControlFile->checkPoint) ||
		!XLByteEQ(ControlFileWatcher->current_prevCheckPointLoc,ControlFile->prevCheckPoint) ||
		!XLByteEQ(ControlFileWatcher->current_checkPointCopy_redo,ControlFile->checkPointCopy.redo))
	{
		ControlFileWatcher->current_checkPointLoc = ControlFile->checkPoint;
		ControlFileWatcher->current_prevCheckPointLoc = ControlFile->prevCheckPoint;
		ControlFileWatcher->current_checkPointCopy_redo = ControlFile->checkPointCopy.redo;

		if (XLogGetWriteAndFlushedLoc(&writeLoc, &flushedLoc))
		{
			bool problem = XLByteLE(flushedLoc,ControlFile->checkPoint);
			if (problem)
				elog(PANIC,"Checkpoint location %s for pg_control file is not flushed (write loc %s, flushed loc is %s)",
				     XLogLocationToString_Long(&ControlFile->checkPoint),
				     XLogLocationToString2_Long(&writeLoc),
				     XLogLocationToString3_Long(&flushedLoc));

			if (Debug_print_control_checkpoints)
				elog(LOG,"pg_control checkpoint: change (checkpoint loc %s, previous loc %s, copy's redo loc %s, write loc %s, flushed loc %s)",
					 XLogLocationToString_Long(&ControlFile->checkPoint),
					 XLogLocationToString2_Long(&ControlFile->prevCheckPoint),
					 XLogLocationToString3_Long(&ControlFile->checkPointCopy.redo),
					 XLogLocationToString4_Long(&writeLoc),
					 XLogLocationToString5_Long(&flushedLoc));
		}
		else
		{
			if (Debug_print_control_checkpoints)
				elog(LOG,"pg_control checkpoint: change (checkpoint loc %s, previous loc %s, copy's redo loc %s)",
					 XLogLocationToString_Long(&ControlFile->checkPoint),
					 XLogLocationToString2_Long(&ControlFile->prevCheckPoint),
					 XLogLocationToString3_Long(&ControlFile->checkPointCopy.redo));
		}
	}
}

/*
 * I/O routines for pg_control
 *
 * *ControlFile is a buffer in shared memory that holds an image of the
 * contents of pg_control.	WriteControlFile() initializes pg_control
 * given a preloaded buffer, ReadControlFile() loads the buffer from
 * the pg_control file (during postmaster or standalone-backend startup),
 * and UpdateControlFile() rewrites pg_control after we modify xlog state.
 *
 * For simplicity, WriteControlFile() initializes the fields of pg_control
 * that are related to checking backend/database compatibility, and
 * ReadControlFile() verifies they are correct.  We could split out the
 * I/O and compatibility-check functions, but there seems no need currently.
 */
static void
WriteControlFile(void)
{
	MirroredFlatFileOpen	mirroredOpen;

	char		buffer[PG_CONTROL_SIZE];		/* need not be aligned */
	char	   *localeptr;

	/*
	 * Initialize version and compatibility-check fields
	 */
	ControlFile->pg_control_version = PG_CONTROL_VERSION;
	ControlFile->catalog_version_no = CATALOG_VERSION_NO;

	ControlFile->maxAlign = MAXIMUM_ALIGNOF;
	ControlFile->floatFormat = FLOATFORMAT_VALUE;

	ControlFile->blcksz = BLCKSZ;
	ControlFile->relseg_size = RELSEG_SIZE;
	ControlFile->xlog_blcksz = XLOG_BLCKSZ;
	ControlFile->xlog_seg_size = XLOG_SEG_SIZE;

	ControlFile->nameDataLen = NAMEDATALEN;
	ControlFile->indexMaxKeys = INDEX_MAX_KEYS;

#ifdef HAVE_INT64_TIMESTAMP
	ControlFile->enableIntTimes = TRUE;
#else
	ControlFile->enableIntTimes = FALSE;
#endif

	ControlFile->localeBuflen = LOCALE_NAME_BUFLEN;
	localeptr = setlocale(LC_COLLATE, NULL);
	if (!localeptr)
		ereport(PANIC,
				(errmsg("invalid LC_COLLATE setting")));
	StrNCpy(ControlFile->lc_collate, localeptr, LOCALE_NAME_BUFLEN);
	localeptr = setlocale(LC_CTYPE, NULL);
	if (!localeptr)
		ereport(PANIC,
				(errmsg("invalid LC_CTYPE setting")));
	StrNCpy(ControlFile->lc_ctype, localeptr, LOCALE_NAME_BUFLEN);

	/* Contents are protected with a CRC */
	ControlFile->crc = crc32c(crc32cInit(),
			   (char *) ControlFile,
			   offsetof(ControlFileData, crc));
	crc32cFinish(ControlFile->crc);

	/*
	 * We write out PG_CONTROL_SIZE bytes into pg_control, zero-padding the
	 * excess over sizeof(ControlFileData).  This reduces the odds of
	 * premature-EOF errors when reading pg_control.  We'll still fail when we
	 * check the contents of the file, but hopefully with a more specific
	 * error than "couldn't read pg_control".
	 */
	if (sizeof(ControlFileData) > PG_CONTROL_SIZE)
		elog(PANIC, "sizeof(ControlFileData) is larger than PG_CONTROL_SIZE; fix either one");

	memset(buffer, 0, PG_CONTROL_SIZE);
	memcpy(buffer, ControlFile, sizeof(ControlFileData));

	MirroredFlatFile_Open(
					&mirroredOpen,
					XLOG_CONTROL_FILE_SUBDIR,
					XLOG_CONTROL_FILE_SIMPLE,
					O_RDWR | O_CREAT | O_EXCL | PG_BINARY,
					S_IRUSR | S_IWUSR,
					/* suppressError */ false,
					/* atomic operation */ false,
					/*isMirrorRecovery */ false);

	MirroredFlatFile_Write(
					&mirroredOpen,
					0,
					buffer,
					PG_CONTROL_SIZE,
					/* suppressError */ false);

	MirroredFlatFile_Flush(
					&mirroredOpen,
					/* suppressError */ false);

	MirroredFlatFile_Close(&mirroredOpen);

	ControlFileWatcherSaveInitial();
}

static void
ReadControlFile(void)
{
	pg_crc32	crc;
	int			fd;

	/*
	 * Read data...
	 */
	fd = BasicOpenFile(XLOG_CONTROL_FILE,
					   O_RDWR | PG_BINARY,
					   S_IRUSR | S_IWUSR);
	if (fd < 0)
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not open control file \"%s\": %m",
						XLOG_CONTROL_FILE)));

	if (read(fd, ControlFile, sizeof(ControlFileData)) != sizeof(ControlFileData))
		ereport(PANIC,
				(errcode_for_file_access(),
				 errmsg("could not read from control file: %m")));

	close(fd);

	/*
	 * Check for expected pg_control format version.  If this is wrong, the
	 * CRC check will likely fail because we'll be checking the wrong number
	 * of bytes.  Complaining about wrong version will probably be more
	 * enlightening than complaining about wrong CRC.
	 */
	if (ControlFile->pg_control_version != PG_CONTROL_VERSION)
		ereport(FATAL,
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with PG_CONTROL_VERSION %d,"
				  " but the server was compiled with PG_CONTROL_VERSION %d.",
						ControlFile->pg_control_version, PG_CONTROL_VERSION),
				 errhint("It looks like you need to initdb.")));

	/* Now check the CRC. */
	crc = crc32c(crc32cInit(),
			   (char *) ControlFile,
			   offsetof(ControlFileData, crc));
	crc32cFinish(crc);

	if (!EQ_CRC32(crc, ControlFile->crc))
	{
		/* We might have an old record.  Recompute using old crc algorithm, and re-check. */
		INIT_CRC32(crc);
		COMP_CRC32(crc,
				   (char *) ControlFile,
				   offsetof(ControlFileData, crc));
		FIN_CRC32(crc);
		if (!EQ_CRC32(crc, ControlFile->crc))
				ereport(FATAL,
						(errmsg("incorrect checksum in control file")));
	}

	/*
	 * Do compatibility checking immediately, except during upgrade.
	 * We do this here for 2 reasons:
	 *
	 * (1) if the database isn't compatible with the backend executable, we
	 * want to abort before we can possibly do any damage;
	 *
	 * (2) this code is executed in the postmaster, so the setlocale() will
	 * propagate to forked backends, which aren't going to read this file for
	 * themselves.	(These locale settings are considered critical
	 * compatibility items because they can affect sort order of indexes.)
	 */
	if (ControlFile->catalog_version_no != CATALOG_VERSION_NO &&
		!gp_upgrade_mode)
		ereport(FATAL,
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with CATALOG_VERSION_NO %d,"
				  " but the server was compiled with CATALOG_VERSION_NO %d.",
						ControlFile->catalog_version_no, CATALOG_VERSION_NO),
				 errhint("It looks like you need to initdb.")));
	if (ControlFile->maxAlign != MAXIMUM_ALIGNOF)
		ereport(FATAL,
				(errmsg("database files are incompatible with server"),
		   errdetail("The database cluster was initialized with MAXALIGN %d,"
					 " but the server was compiled with MAXALIGN %d.",
					 ControlFile->maxAlign, MAXIMUM_ALIGNOF),
				 errhint("It looks like you need to initdb.")));
	if (ControlFile->floatFormat != FLOATFORMAT_VALUE)
		ereport(FATAL,
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster appears to use a different floating-point number format than the server executable."),
				 errhint("It looks like you need to initdb.")));
	if (ControlFile->blcksz != BLCKSZ)
		ereport(FATAL,
				(errmsg("database files are incompatible with server"),
			 errdetail("The database cluster was initialized with BLCKSZ %d,"
					   " but the server was compiled with BLCKSZ %d.",
					   ControlFile->blcksz, BLCKSZ),
				 errhint("It looks like you need to recompile or initdb.")));
	if (ControlFile->relseg_size != RELSEG_SIZE)
		ereport(FATAL,
				(errmsg("database files are incompatible with server"),
		errdetail("The database cluster was initialized with RELSEG_SIZE %d,"
				  " but the server was compiled with RELSEG_SIZE %d.",
				  ControlFile->relseg_size, RELSEG_SIZE),
				 errhint("It looks like you need to recompile or initdb.")));
	if (ControlFile->xlog_blcksz != XLOG_BLCKSZ)
		ereport(FATAL,
				(errmsg("database files are incompatible with server"),
		errdetail("The database cluster was initialized with XLOG_BLCKSZ %d,"
				  " but the server was compiled with XLOG_BLCKSZ %d.",
				  ControlFile->xlog_blcksz, XLOG_BLCKSZ),
				 errhint("It looks like you need to recompile or initdb.")));
	if (ControlFile->xlog_seg_size != XLOG_SEG_SIZE)
		ereport(FATAL,
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with XLOG_SEG_SIZE %d,"
					   " but the server was compiled with XLOG_SEG_SIZE %d.",
						   ControlFile->xlog_seg_size, XLOG_SEG_SIZE),
				 errhint("It looks like you need to recompile or initdb.")));
	if (ControlFile->nameDataLen != NAMEDATALEN)
		ereport(FATAL,
				(errmsg("database files are incompatible with server"),
		errdetail("The database cluster was initialized with NAMEDATALEN %d,"
				  " but the server was compiled with NAMEDATALEN %d.",
				  ControlFile->nameDataLen, NAMEDATALEN),
				 errhint("It looks like you need to recompile or initdb.")));
	if (ControlFile->indexMaxKeys != INDEX_MAX_KEYS)
		ereport(FATAL,
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with INDEX_MAX_KEYS %d,"
					  " but the server was compiled with INDEX_MAX_KEYS %d.",
						   ControlFile->indexMaxKeys, INDEX_MAX_KEYS),
				 errhint("It looks like you need to recompile or initdb.")));

#ifdef HAVE_INT64_TIMESTAMP
	if (ControlFile->enableIntTimes != TRUE)
		ereport(FATAL,
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized without HAVE_INT64_TIMESTAMP"
				  " but the server was compiled with HAVE_INT64_TIMESTAMP."),
				 errhint("It looks like you need to recompile or initdb.")));
#else
	if (ControlFile->enableIntTimes != FALSE)
		ereport(FATAL,
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with HAVE_INT64_TIMESTAMP"
			   " but the server was compiled without HAVE_INT64_TIMESTAMP."),
				 errhint("It looks like you need to recompile or initdb.")));
#endif

	if (ControlFile->localeBuflen != LOCALE_NAME_BUFLEN)
		ereport(FATAL,
				(errmsg("database files are incompatible with server"),
				 errdetail("The database cluster was initialized with LOCALE_NAME_BUFLEN %d,"
				  " but the server was compiled with LOCALE_NAME_BUFLEN %d.",
						   ControlFile->localeBuflen, LOCALE_NAME_BUFLEN),
				 errhint("It looks like you need to recompile or initdb.")));
	if (pg_perm_setlocale(LC_COLLATE, ControlFile->lc_collate) == NULL)
		ereport(FATAL,
			(errmsg("database files are incompatible with operating system"),
			 errdetail("The database cluster was initialized with LC_COLLATE \"%s\","
					   " which is not recognized by setlocale().",
					   ControlFile->lc_collate),
			 errhint("It looks like you need to initdb or install locale support.")));
	if (pg_perm_setlocale(LC_CTYPE, ControlFile->lc_ctype) == NULL)
		ereport(FATAL,
			(errmsg("database files are incompatible with operating system"),
		errdetail("The database cluster was initialized with LC_CTYPE \"%s\","
				  " which is not recognized by setlocale().",
				  ControlFile->lc_ctype),
			 errhint("It looks like you need to initdb or install locale support.")));

	/* Make the fixed locale settings visible as GUC variables, too */
	SetConfigOption("lc_collate", ControlFile->lc_collate,
					PGC_INTERNAL, PGC_S_OVERRIDE);
	SetConfigOption("lc_ctype", ControlFile->lc_ctype,
					PGC_INTERNAL, PGC_S_OVERRIDE);

	if (!ControlFileWatcher->watcherInitialized)
	{
		ControlFileWatcherSaveInitial();
	}
	else
	{
		ControlFileWatcherCheckForChange();
	}
}

static bool
XLogGetWriteAndFlushedLoc(XLogRecPtr *writeLoc, XLogRecPtr *flushedLoc)
{
	/* use volatile pointer to prevent code rearrangement */
	volatile XLogCtlData *xlogctl = XLogCtl;

	SpinLockAcquire(&xlogctl->info_lck);
	*writeLoc = xlogctl->LogwrtResult.Write;
	*flushedLoc = xlogctl->LogwrtResult.Flush;
	SpinLockRelease(&xlogctl->info_lck);

	return (writeLoc->xlogid != 0 || writeLoc->xrecoff != 0);
}

/*
 * Very specific purpose routine for FileRep that flushes out XLOG records from the
 * XLOG memory cache to disk.
 */
void
XLogFileRepFlushCache(
	XLogRecPtr	*lastChangeTrackingEndLoc)
{
	/*
	 * We hold the ChangeTrackingTransitionLock EXCLUSIVE, thus the lastChangeTrackingEndLoc
	 * value is the previous location -- the one we want.
	 *
	 * Since the lock is acquired after ALL WRITES and FSYNCS in XLogInsert_Internal,
	 * we know this flush is safe (i.e. will not hang) and will push out all XLOG records we
	 * want to see in the next call to ChangeTracking_CreateInitialFromPreviousCheckpoint.
	 */

	*lastChangeTrackingEndLoc = XLogCtl->lastChangeTrackingEndLoc;

	if (Debug_persistent_print)
		elog(Persistent_DebugPrintLevel(),
			 "XLogFileRepFlushCache: Going flush through location %s...",
			 XLogLocationToString(lastChangeTrackingEndLoc));

	XLogFlush(*lastChangeTrackingEndLoc);
}

void
XLogInChangeTrackingTransition(void)
{
	XLogRecPtr	lastChangeTrackingEndLoc;
	XLogRecPtr      recPtrInit = {0, 0};

	if (Debug_persistent_print)
		elog(Persistent_DebugPrintLevel(),
			 "XLogInChangeTrackingTransition: Acquiring ChangeTrackingTransitionLock...");

	LWLockAcquire(ChangeTrackingTransitionLock, LW_EXCLUSIVE);

	if (Debug_persistent_print)
		elog(Persistent_DebugPrintLevel(),
			 "XLogInChangeTrackingTransition: SegmentStateInChangeTrackingTransition...");

	FileRep_SetSegmentState(SegmentStateInChangeTrackingTransition, FaultTypeNotInitialized);

	XLogFileRepFlushCache(&lastChangeTrackingEndLoc);

	if (Debug_persistent_print)
		elog(Persistent_DebugPrintLevel(),
			 "XLogInChangeTrackingTransition: Calling ChangeTracking_CreateInitialFromPreviousCheckpoint with lastChangeTrackingEndLoc %s",
			 XLogLocationToString(&lastChangeTrackingEndLoc));

	/*
	 * During gpstart the following order is followed for recovery:
	 *                      a) xlog records from last checkpoint are replayed into change tracking log file
	 *                      b) xlog is replayed by 3 pass mechanism
	 * In that case XLogCtl->lastChangeTrackingEndLoc will be still set to {0,0} and
	 * in order to insert all xlog records from last checkpoint into change tracking log file
	 * ChangeTracking_CreateInitialFromPreviousCheckpoint(NULL); has to be called.
	 */
	if (XLByteEQ(lastChangeTrackingEndLoc, recPtrInit))
	{
		ChangeTracking_CreateInitialFromPreviousCheckpoint(NULL);
	}
	else
	{
		ChangeTracking_CreateInitialFromPreviousCheckpoint(&lastChangeTrackingEndLoc);
	}

	LWLockRelease(ChangeTrackingTransitionLock);

	if (Debug_persistent_print)
		elog(Persistent_DebugPrintLevel(),
			 "XLogInChangeTrackingTransition: Released ChangeTrackingTransitionLock");

}

void
UpdateControlFile(void)
{
	MirroredFlatFileOpen	mirroredOpen;

	ControlFile->crc = crc32c(crc32cInit(),
				   (char *) ControlFile,
				   offsetof(ControlFileData, crc));
	crc32cFinish(ControlFile->crc);

	MirroredFlatFile_Open(
					&mirroredOpen,
					XLOG_CONTROL_FILE_SUBDIR,
					XLOG_CONTROL_FILE_SIMPLE,
					O_RDWR | PG_BINARY,
					S_IRUSR | S_IWUSR,
					/* suppressError */ false,
					/* atomic operation */ false,
					/*isMirrorRecovery */ false);

	MirroredFlatFile_Write(
					&mirroredOpen,
					0,
					ControlFile,
					PG_CONTROL_SIZE,
					/* suppressError */ false);

	MirroredFlatFile_Flush(
					&mirroredOpen,
					/* suppressError */ false);

	MirroredFlatFile_Close(&mirroredOpen);

	Assert (ControlFileWatcher->watcherInitialized);

	ControlFileWatcherCheckForChange();
}

/*
 * Returns the unique system identifier from control file.
 */
uint64
GetSystemIdentifier(void)
{
	Assert(ControlFile != NULL);
	return ControlFile->system_identifier;
}

/*
 * Initialization of shared memory for XLOG
 */
Size
XLOGShmemSize(void)
{
	Size		size;

	/* XLogCtl */
	size = sizeof(XLogCtlData);
	/* xlblocks array */
	size = add_size(size, mul_size(sizeof(XLogRecPtr), XLOGbuffers));
	/* extra alignment padding for XLOG I/O buffers */
	size = add_size(size, ALIGNOF_XLOG_BUFFER);
	/* and the buffers themselves */
	size = add_size(size, mul_size(XLOG_BLCKSZ, XLOGbuffers));

	/*
	 * Note: we don't count ControlFileData, it comes out of the "slop factor"
	 * added by CreateSharedMemoryAndSemaphores.  This lets us use this
	 * routine again below to compute the actual allocation size.
	 */

	/*
	 * Similary, we also don't PgControlWatch for the above reasons, too.
	 */

	return size;
}

void
XLOGShmemInit(void)
{
	bool		foundCFile,
				foundXLog,
				foundCFileWatcher;
	char	   *allocptr;

	ControlFile = (ControlFileData *)
		ShmemInitStruct("Control File", sizeof(ControlFileData), &foundCFile);
	ControlFileWatcher = (ControlFileWatch *)
		ShmemInitStruct("Control File Watcher", sizeof(ControlFileWatch), &foundCFileWatcher);
	XLogCtl = (XLogCtlData *)
		ShmemInitStruct("XLOG Ctl", XLOGShmemSize(), &foundXLog);

	if (foundCFile || foundXLog || foundCFileWatcher)
	{
		/* both should be present or neither */
		Assert(foundCFile && foundXLog && foundCFileWatcher);
		return;
	}

	memset(XLogCtl, 0, sizeof(XLogCtlData));

	XLogCtl->pass4_PTCatVerificationPassed = true;

	/*
	 * Since XLogCtlData contains XLogRecPtr fields, its sizeof should be a
	 * multiple of the alignment for same, so no extra alignment padding is
	 * needed here.
	 */
	allocptr = ((char *) XLogCtl) + sizeof(XLogCtlData);
	XLogCtl->xlblocks = (XLogRecPtr *) allocptr;
	memset(XLogCtl->xlblocks, 0, sizeof(XLogRecPtr) * XLOGbuffers);
	allocptr += sizeof(XLogRecPtr) * XLOGbuffers;

	/*
	 * Align the start of the page buffers to an ALIGNOF_XLOG_BUFFER boundary.
	 */
	allocptr = (char *) TYPEALIGN(ALIGNOF_XLOG_BUFFER, allocptr);
	XLogCtl->pages = allocptr;
	memset(XLogCtl->pages, 0, (Size) XLOG_BLCKSZ * XLOGbuffers);

	/*
	 * Do basic initialization of XLogCtl shared data. (StartupXLOG will fill
	 * in additional info.)
	 */
	XLogCtl->XLogCacheByte = (Size) XLOG_BLCKSZ *XLOGbuffers;

	XLogCtl->XLogCacheBlck = XLOGbuffers - 1;
	XLogCtl->SharedRecoveryInProgress = true;
	XLogCtl->Insert.currpage = (XLogPageHeader) (XLogCtl->pages);
	SpinLockInit(&XLogCtl->info_lck);
	InitSharedLatch(&XLogCtl->recoveryWakeupLatch);

	XLogCtl->haveLastCheckpointLoc = false;
	memset(&XLogCtl->lastCheckpointLoc, 0, sizeof(XLogRecPtr));
	memset(&XLogCtl->lastCheckpointEndLoc, 0, sizeof(XLogRecPtr));

	/*
	 * Initialize the shared memory by the parameter given to postmaster.
	 * GpStandbyDbid could be inconsistent with the catalog if the postmaster
	 * is given wrong id, but there is no chance to check it in this early
	 * stage of startup, and this is how we have been doing historically.
	 */
	XLogCtl->standbyDbid = GpStandbyDbid;

	SpinLockInit(&XLogCtl->resynchronize_lck);
}

/**
 * This should be called when we are sure that it is safe to try to read the control file and BEFORE
 *  we have launched any child processes that need access to collation and ctype data.
 *
 * It is not safe to read the control file on a mirror because it may not be synchronized
 */
void
XLogStartupInit(void)
{
	/*
	 * If we are not in bootstrap mode, pg_control should already exist. Read
	 * and validate it immediately (see comments in ReadControlFile() for the
	 * reasons why).
	 */
	if (!IsBootstrapProcessingMode())
		ReadControlFile();
}

/*
 * This func must be called ONCE on system install.  It creates pg_control
 * and the initial XLOG segment.
 */
void
BootStrapXLOG(void)
{
	CheckPoint	checkPoint;
	char	   *buffer;
	XLogPageHeader page;
	XLogLongPageHeader longpage;
	XLogRecord *record;
	bool		use_existent;
	uint64		sysidentifier;
	struct timeval tv;
	pg_crc32	crc;

	/*
	 * Select a hopefully-unique system identifier code for this installation.
	 * We use the result of gettimeofday(), including the fractional seconds
	 * field, as being about as unique as we can easily get.  (Think not to
	 * use random(), since it hasn't been seeded and there's no portable way
	 * to seed it other than the system clock value...)  The upper half of the
	 * uint64 value is just the tv_sec part, while the lower half is the XOR
	 * of tv_sec and tv_usec.  This is to ensure that we don't lose uniqueness
	 * unnecessarily if "uint64" is really only 32 bits wide.  A person
	 * knowing this encoding can determine the initialization time of the
	 * installation, which could perhaps be useful sometimes.
	 */
	gettimeofday(&tv, NULL);
	sysidentifier = ((uint64) tv.tv_sec) << 32;
	sysidentifier |= (uint32) (tv.tv_sec | tv.tv_usec);

	/* First timeline ID is always 1 */
	ThisTimeLineID = 1;

	/* page buffer must be aligned suitably for O_DIRECT */
	buffer = (char *) palloc(XLOG_BLCKSZ + ALIGNOF_XLOG_BUFFER);
	page = (XLogPageHeader) TYPEALIGN(ALIGNOF_XLOG_BUFFER, buffer);
	memset(page, 0, XLOG_BLCKSZ);

	/*
	 * Set up information for the initial checkpoint record
	 *
	 * The initial checkpoint record is written to the beginning of the WAL
	 * segment with logid=0 logseg=1. The very first WAL segment, 0/0, is not
	 * used, so that we can use 0/0 to mean "before any valid WAL segment".
	 */
	checkPoint.redo.xlogid = 0;
	checkPoint.redo.xrecoff = XLogSegSize + SizeOfXLogLongPHD;
	checkPoint.undo = checkPoint.redo;
	checkPoint.ThisTimeLineID = ThisTimeLineID;
	checkPoint.nextXidEpoch = 0;
	checkPoint.nextXid = FirstNormalTransactionId;
	checkPoint.nextOid = FirstBootstrapObjectId;
	checkPoint.nextMulti = FirstMultiXactId;
	checkPoint.nextMultiOffset = 0;
	checkPoint.time = time(NULL);

	ShmemVariableCache->nextXid = checkPoint.nextXid;
	ShmemVariableCache->nextOid = checkPoint.nextOid;
	ShmemVariableCache->oidCount = 0;
	MultiXactSetNextMXact(checkPoint.nextMulti, checkPoint.nextMultiOffset);

	/* Set up the XLOG page header */
	page->xlp_magic = XLOG_PAGE_MAGIC;
	page->xlp_info = XLP_LONG_HEADER;
	page->xlp_tli = ThisTimeLineID;
	page->xlp_pageaddr.xlogid = 0;
	page->xlp_pageaddr.xrecoff = XLogSegSize;
	longpage = (XLogLongPageHeader) page;
	longpage->xlp_sysid = sysidentifier;
	longpage->xlp_seg_size = XLogSegSize;
	longpage->xlp_xlog_blcksz = XLOG_BLCKSZ;

	/* Insert the initial checkpoint record */
	record = (XLogRecord *) ((char *) page + SizeOfXLogLongPHD);
	record->xl_prev.xlogid = 0;
	record->xl_prev.xrecoff = 0;
	record->xl_xid = InvalidTransactionId;
	record->xl_tot_len = SizeOfXLogRecord + sizeof(checkPoint);
	record->xl_len = sizeof(checkPoint);
	record->xl_info = XLOG_CHECKPOINT_SHUTDOWN;
	record->xl_rmid = RM_XLOG_ID;
	memcpy(XLogRecGetData(record), &checkPoint, sizeof(checkPoint));

	crc = crc32c(crc32cInit(), &checkPoint, sizeof(checkPoint));
	crc = crc32c(crc, (char *) record + sizeof(pg_crc32),
			   SizeOfXLogRecord - sizeof(pg_crc32));
	crc32cFinish(crc);

	record->xl_crc = crc;

	/* Create first XLOG segment file */
	use_existent = false;
	XLogFileInit(
		&mirroredLogFileOpen,
		0, 1, &use_existent, false);

	/* Write the first page with the initial record */
	errno = 0;
	if (MirroredFlatFile_Append(
			&mirroredLogFileOpen,
			page,
			XLOG_BLCKSZ,
			/* suppressError */ true))
	{
		ereport(PANIC,
				(errcode_for_file_access(),
			  errmsg("could not write bootstrap transaction log file: %m")));
	}

	if (MirroredFlatFile_Flush(
			&mirroredLogFileOpen,
			/* suppressError */ true))
		ereport(PANIC,
				(errcode_for_file_access(),
			  errmsg("could not fsync bootstrap transaction log file: %m")));

	MirroredFlatFile_Close(
			&mirroredLogFileOpen);

	/* Now create pg_control */

	memset(ControlFile, 0, sizeof(ControlFileData));
	/* Initialize pg_control status fields */
	ControlFile->system_identifier = sysidentifier;
	ControlFile->state = DB_SHUTDOWNED;
	ControlFile->time = checkPoint.time;
	ControlFile->logId = 0;
	ControlFile->logSeg = 1;
	ControlFile->checkPoint = checkPoint.redo;
	ControlFile->checkPointCopy = checkPoint;
	/* some additional ControlFile fields are set in WriteControlFile() */

	WriteControlFile();

	/* Bootstrap the commit log, too */
	BootStrapCLOG();
	BootStrapSUBTRANS();
	BootStrapMultiXact();
	DistributedLog_BootStrap();

	pfree(buffer);
}

static char *
str_time(pg_time_t tnow)
{
	static char buf[128];

	pg_strftime(buf, sizeof(buf),
			 /* Win32 timezone names are too long so don't print them */
#ifndef WIN32
			 "%Y-%m-%d %H:%M:%S %Z",
#else
			 "%Y-%m-%d %H:%M:%S",
#endif
			 pg_localtime(&tnow, log_timezone ? log_timezone : gmt_timezone));

	return buf;
}

/*
 * See if there is a recovery command file (recovery.conf), and if so
 * read in parameters for recovery in standby mode.
 *
 * XXX longer term intention is to expand this to
 * cater for additional parameters and controls
 * possibly use a flex lexer similar to the GUC one
 */
void
XLogReadRecoveryCommandFile(int emode)
{
	FILE	   *fd;
	char		cmdline[MAXPGPATH];
	bool		syntaxError = false;

	fd = AllocateFile(RECOVERY_COMMAND_FILE, "r");
	if (fd == NULL)
	{
		if (errno == ENOENT)
			return;				/* not there, so no recovery in standby mode */
		ereport(FATAL,
				(errcode_for_file_access(),
				 errmsg("could not open recovery command file \"%s\": %m",
						RECOVERY_COMMAND_FILE)));
	}

	ereport(emode,
			(errmsg("Found recovery.conf file, checking appropriate parameters "
					" for recovery in standby mode")));

	/*
	 * Parse the file...
	 */
	while (fgets(cmdline, MAXPGPATH, fd) != NULL)
	{
		/* skip leading whitespace and check for # comment */
		char	   *ptr;
		char	   *tok1;
		char	   *tok2;

		for (ptr = cmdline; *ptr; ptr++)
		{
			if (!isspace((unsigned char) *ptr))
				break;
		}
		if (*ptr == '\0' || *ptr == '#')
			continue;

		/* identify the quoted parameter value */
		tok1 = strtok(ptr, "'");
		if (!tok1)
		{
			syntaxError = true;
			break;
		}
		tok2 = strtok(NULL, "'");
		if (!tok2)
		{
			syntaxError = true;
			break;
		}
		/* reparse to get just the parameter name */
		tok1 = strtok(ptr, " \t=");
		if (!tok1)
		{
			syntaxError = true;
			break;
		}

		if (strcmp(tok1, "primary_conninfo") == 0)
		{
			PrimaryConnInfo = pstrdup(tok2);
			ereport(emode,
					(errmsg("primary_conninfo = \"%s\"",
							PrimaryConnInfo)));
		}
		else if (strcmp(tok1, "standby_mode") == 0)
		{
			/*
			 * does nothing if a recovery_target is not also set
			 */
			if (strcmp(tok2, "on") == 0)
				StandbyModeRequested = true;
			else
			{
				StandbyModeRequested = false;
				tok2 = "false";
			}
			ereport(emode,
					(errmsg("StandbyModeRequested = %s", tok2)));
		}
		else
			ereport(FATAL,
					(errmsg("unrecognized recovery parameter \"%s\"",
							tok1)));
	}

	FreeFile(fd);

	if (syntaxError)
		ereport(FATAL,
				(errmsg("syntax error in recovery command file: %s",
						cmdline),
			  errhint("Lines should have the format parameter = 'value'.")));

	/*
	 * Check for compulsory parameters
	 */
	if (StandbyModeRequested)
	{
		if (PrimaryConnInfo == NULL)
			ereport(FATAL,
					(errmsg("recovery command file \"%s\" primary_conninfo not specified",
							RECOVERY_COMMAND_FILE),
					 errhint("The database server in standby mode needs primary_connection to connect to primary.")));
	}
	else
	{
		/* Currently, standby mode request is a must if recovery.conf file exists */
		ereport(FATAL,
				(errmsg("recovery command file \"%s\" request for standby mode not specified",
						RECOVERY_COMMAND_FILE)));
	}
}

/*
 * Exit archive-recovery state
 */
static void
exitArchiveRecovery(TimeLineID endTLI, uint32 endLogId, uint32 endLogSeg)
{
	char		recoveryPath[MAXPGPATH];
	char		xlogpath[MAXPGPATH];
	char		*xlogDir = NULL;

	/*
	 * We should have the ending log segment currently open.  Verify, and then
	 * close it (to avoid problems on Windows with trying to rename or delete
	 * an open file).
	 */
	Assert(readFile >= 0);
	Assert(readId == endLogId);
	Assert(readSeg == endLogSeg);

	close(readFile);
	readFile = -1;

	/*
	 * If the segment was fetched from archival storage, we want to replace
	 * the existing xlog segment (if any) with the archival version.  This is
	 * because whatever is in XLOGDIR is very possibly older than what we have
	 * from the archives, since it could have come from restoring a PGDATA
	 * backup.	In any case, the archival version certainly is more
	 * descriptive of what our current database state is, because that is what
	 * we replayed from.
	 *
	 * Note that if we are establishing a new timeline, ThisTimeLineID is
	 * already set to the new value, and so we will create a new file instead
	 * of overwriting any existing file.  (This is, in fact, always the case
	 * at present.)
	 */
	xlogDir = makeRelativeToTxnFilespace(XLOGDIR);
	if (snprintf(recoveryPath, MAXPGPATH, "%s/RECOVERYXLOG", xlogDir) > MAXPGPATH)
	{
		ereport(ERROR, (errmsg("cannot generate path %s/RECOVERYXLOG", xlogDir)));	
	}
	XLogFilePath(xlogpath, ThisTimeLineID, endLogId, endLogSeg);

	if (restoredFromArchive)
	{
		ereport(DEBUG3,
				(errmsg_internal("moving last restored xlog to \"%s\"",
								 xlogpath)));
		unlink(xlogpath);		/* might or might not exist */
		if (rename(recoveryPath, xlogpath) != 0)
			ereport(FATAL,
					(errcode_for_file_access(),
					 errmsg("could not rename file \"%s\" to \"%s\": %m",
							recoveryPath, xlogpath)));
		/* XXX might we need to fix permissions on the file? */
	}
	else
	{
		/*
		 * If the latest segment is not archival, but there's still a
		 * RECOVERYXLOG laying about, get rid of it.
		 */
		unlink(recoveryPath);	/* ignore any error */

		/*
		 * If we are establishing a new timeline, we have to copy data from
		 * the last WAL segment of the old timeline to create a starting WAL
		 * segment for the new timeline.
		 */
		if (endTLI != ThisTimeLineID)
			XLogFileCopy(endLogId, endLogSeg,
						 endTLI, endLogId, endLogSeg);
	}

	/*
	 * Let's just make real sure there are not .ready or .done flags posted
	 * for the new segment.
	 */
	XLogFileName(xlogpath, ThisTimeLineID, endLogId, endLogSeg);
	XLogArchiveCleanup(xlogpath);

	/* Get rid of any remaining recovered timeline-history file, too */
	if (snprintf(recoveryPath, MAXPGPATH, "%s/RECOVERYHISTORY", xlogDir) > MAXPGPATH)
	{
		ereport(ERROR, (errmsg("cannot generate path %s/RECOVERYHISTORY", xlogDir)));
	}
	unlink(recoveryPath);		/* ignore any error */

	/*
	 * Rename the config file out of the way, so that we don't accidentally
	 * re-enter archive recovery mode in a subsequent crash.
	 */
	unlink(RECOVERY_COMMAND_DONE);
	if (rename(RECOVERY_COMMAND_FILE, RECOVERY_COMMAND_DONE) != 0)
		ereport(FATAL,
				(errcode_for_file_access(),
				 errmsg("could not rename file \"%s\" to \"%s\": %m",
						RECOVERY_COMMAND_FILE, RECOVERY_COMMAND_DONE)));

	pfree(xlogDir);
}

/*
 * For point-in-time recovery, this function decides whether we want to
 * stop applying the XLOG at or after the current record.
 *
 * Returns TRUE if we are stopping, FALSE otherwise.  On TRUE return,
 * *includeThis is set TRUE if we should apply this record before stopping.
 * Also, some information is saved in recoveryStopXid et al for use in
 * annotating the new timeline's history file.
 */
static bool
recoveryStopsHere(XLogRecord *record, bool *includeThis)
{
	bool		stopsHere;
	uint8		record_info;
	time_t		recordXtime;

	/* Do we have a PITR target at all? */
	if (!recoveryTarget)
		return false;

	/* We only consider stopping at COMMIT or ABORT records */
	if (record->xl_rmid != RM_XACT_ID)
		return false;
	record_info = record->xl_info & ~XLR_INFO_MASK;
	if (record_info == XLOG_XACT_COMMIT)
	{
		xl_xact_commit *recordXactCommitData;

		recordXactCommitData = (xl_xact_commit *) XLogRecGetData(record);
		recordXtime = recordXactCommitData->xtime;
	}
	else if (record_info == XLOG_XACT_ABORT)
	{
		xl_xact_abort *recordXactAbortData;

		recordXactAbortData = (xl_xact_abort *) XLogRecGetData(record);
		recordXtime = recordXactAbortData->xtime;
	}
	else
		return false;

	if (recoveryTargetExact)
	{
		/*
		 * there can be only one transaction end record with this exact
		 * transactionid
		 *
		 * when testing for an xid, we MUST test for equality only, since
		 * transactions are numbered in the order they start, not the order
		 * they complete. A higher numbered xid will complete before you about
		 * 50% of the time...
		 */
		stopsHere = (record->xl_xid == recoveryTargetXid);
		if (stopsHere)
			*includeThis = recoveryTargetInclusive;
	}
	else
	{
		/*
		 * there can be many transactions that share the same commit time, so
		 * we stop after the last one, if we are inclusive, or stop at the
		 * first one if we are exclusive
		 */
		if (recoveryTargetInclusive)
			stopsHere = (recordXtime > recoveryTargetTime);
		else
			stopsHere = (recordXtime >= recoveryTargetTime);
		if (stopsHere)
			*includeThis = false;
	}

	if (stopsHere)
	{
		recoveryStopXid = record->xl_xid;
		recoveryStopTime = recordXtime;
		recoveryStopAfter = *includeThis;

		if (record_info == XLOG_XACT_COMMIT)
		{
			if (recoveryStopAfter)
				ereport(LOG,
						(errmsg("recovery stopping after commit of transaction %u, time %s",
							  recoveryStopXid, str_time(recoveryStopTime))));
			else
				ereport(LOG,
						(errmsg("recovery stopping before commit of transaction %u, time %s",
							  recoveryStopXid, str_time(recoveryStopTime))));
		}
		else
		{
			if (recoveryStopAfter)
				ereport(LOG,
						(errmsg("recovery stopping after abort of transaction %u, time %s",
							  recoveryStopXid, str_time(recoveryStopTime))));
			else
				ereport(LOG,
						(errmsg("recovery stopping before abort of transaction %u, time %s",
							  recoveryStopXid, str_time(recoveryStopTime))));
		}
	}

	return stopsHere;
}

/*
 * Save timestamp of the next chunk of WAL records to apply.
 *
 * We keep this in XLogCtl, not a simple static variable, so that it can be
 * seen by all backends.
 */
static void
SetCurrentChunkStartTime(TimestampTz xtime)
{
	/* use volatile pointer to prevent code rearrangement */
	volatile XLogCtlData *xlogctl = XLogCtl;

	SpinLockAcquire(&xlogctl->info_lck);
	xlogctl->currentChunkStartTime = xtime;
	SpinLockRelease(&xlogctl->info_lck);
}

static void
printEndOfXLogFile(XLogRecPtr	*loc)
{
	uint32 seg = loc->xrecoff / XLogSegSize;

	XLogRecPtr roundedDownLoc;

	XLogRecord *record;
	XLogRecPtr	LastRec;

	/*
	 * Go back to the beginning of the log file and read forward to find
	 * the end of the transaction log.
	 */
	roundedDownLoc.xlogid = loc->xlogid;
	roundedDownLoc.xrecoff = (seg * XLogSegSize) + SizeOfXLogLongPHD;

	XLogCloseReadRecord();

	record = XLogReadRecord(&roundedDownLoc, false, LOG);
	if (record == NULL)
	{
		elog(LOG,"Couldn't read transaction log file (logid %d, seg %d)",
			 loc->xlogid, seg);
		return;
	}

	do
	{
		LastRec = ReadRecPtr;

		record = XLogReadRecord(NULL, false, DEBUG5);
	} while (record != NULL);

	record = XLogReadRecord(&LastRec, false, ERROR);

	elog(LOG,"found end of transaction log file %s",
		 XLogLocationToString_Long(&EndRecPtr));

	XLogCloseReadRecord();
}

static void
StartupXLOG_InProduction(void)
{
	TransactionId oldestActiveXID;

	/* Pre-scan prepared transactions to find out the range of XIDs present */
	oldestActiveXID = PrescanPreparedTransactions();

	elog(LOG, "Oldest active transaction from prepared transactions %u", oldestActiveXID);

	/* Start up the commit log and related stuff, too */
	StartupCLOG();
	StartupSUBTRANS(oldestActiveXID);
	StartupMultiXact();
	DistributedLog_Startup(
						oldestActiveXID,
						ShmemVariableCache->nextXid);

	/* Reload shared-memory state for prepared transactions */
	RecoverPreparedTransactions();

	/*
	 * Perform a checkpoint to update all our recovery activity to disk.
	 *
	 * Note that we write a shutdown checkpoint rather than an on-line
	 * one. This is not particularly critical, but since we may be
	 * assigning a new TLI, using a shutdown checkpoint allows us to have
	 * the rule that TLI only changes in shutdown checkpoints, which
	 * allows some extra error checking in xlog_redo.
	 *
	 * Note that - Creation of shutdown checkpoint changes the state in pg_control.
	 * If that happens when we are standby who was recently promoted, the
	 * state in pg_control indicating promotion phases (e.g. DB_IN_STANDBY_PROMOTION,
	 * DB_INSTANDBY_NEW_TLI_SET) before the checkpoint creation will get
	 * overwritten posing a problem for further flow. Hence, CreateCheckpoint()
	 * has an ungly hack to avoid this situation and thus we avoid change of
	 * pg_control state just in this special situation. CreateCheckpoint() also
	 * has a comment referring this.
	 */
	CreateCheckPoint(true, true);

	/*
	 * If this system was a standby which was promoted (or whose catalog is not
	 * yet updated after promote), we delay going into actual production till Pass4.
	 * Pass4 updates the catalog to comply with the standby promotion changes.
	 */
	if (ControlFile->state == DB_IN_STANDBY_PROMOTED
		|| ControlFile->state == DB_IN_STANDBY_NEW_TLI_SET)
	{
		ControlFile->state = DB_IN_STANDBY_NEW_TLI_SET;
		ControlFile->time = time(NULL);
		UpdateControlFile();
		ereport(LOG, (errmsg("database system is almost ready")));
	}
	else
	{
		ControlFile->state = DB_IN_PRODUCTION;
		ControlFile->time = time(NULL);
		UpdateControlFile();
		ereport(LOG, (errmsg("database system is ready")));
	}

	{
		char version[512];

		strcpy(version, PG_VERSION_STR " compiled on " __DATE__ " " __TIME__);

#ifdef USE_ASSERT_CHECKING
		strcat(version, " (with assert checking)");
#endif
		ereport(LOG,(errmsg("%s", version)));

	}

	BuildFlatFiles(false);

	/*
	 * All done.  Allow backends to write WAL.	(Although the bool flag is
	 * probably atomic in itself, we use the info_lck here to ensure that
	 * there are no race conditions concerning visibility of other recent
	 * updates to shared memory.)
	 */
	{
		/* use volatile pointer to prevent code rearrangement */
		volatile XLogCtlData *xlogctl = XLogCtl;

		SpinLockAcquire(&xlogctl->info_lck);
		xlogctl->SharedRecoveryInProgress = false;
		SpinLockRelease(&xlogctl->info_lck);
	}
}

/*
 * Error context callback for tracing or errors occurring during PASS 1 redo.
 */
static void
StartupXLOG_RedoPass1Context(void *arg)
{
	XLogRecord		*record = (XLogRecord*) arg;

	StringInfoData buf;

	initStringInfo(&buf);
	appendStringInfo(&buf, "REDO PASS 1 @ %s; LSN %s: ",
					 XLogLocationToString(&ReadRecPtr),
					 XLogLocationToString2(&EndRecPtr));
	XLog_OutRec(&buf, record);
	appendStringInfo(&buf, " - ");
	RmgrTable[record->xl_rmid].rm_desc(&buf,
									   ReadRecPtr,
									   record);

	errcontext("%s", buf.data);

	pfree(buf.data);
}

/*
 * Error context callback for tracing or errors occurring during PASS 1 redo.
 */
static void
StartupXLOG_RedoPass3Context(void *arg)
{
	XLogRecord		*record = (XLogRecord*) arg;

	StringInfoData buf;

	initStringInfo(&buf);
	appendStringInfo(&buf, "REDO PASS 3 @ %s; LSN %s: ",
					 XLogLocationToString(&ReadRecPtr),
					 XLogLocationToString2(&EndRecPtr));
	XLog_OutRec(&buf, record);
	appendStringInfo(&buf, " - ");
	RmgrTable[record->xl_rmid].rm_desc(&buf,
									   ReadRecPtr,
									   record);

	errcontext("%s", buf.data);

	pfree(buf.data);
}


static void
ApplyStartupRedo(
	XLogRecPtr		*beginLoc,

	XLogRecPtr		*lsn,

	XLogRecord		*record)
{
	MIRROREDLOCK_BUFMGR_VERIFY_NO_LOCK_LEAK_DECLARE;
	RedoErrorCallBack redoErrorCallBack;

	ErrorContextCallback errcontext;

	MIRROREDLOCK_BUFMGR_VERIFY_NO_LOCK_LEAK_ENTER;

	/* Setup error traceback support for ereport() */
	redoErrorCallBack.location = *beginLoc;
	redoErrorCallBack.record = record;

	errcontext.callback = rm_redo_error_callback;
	errcontext.arg = (void *) &redoErrorCallBack;
	errcontext.previous = error_context_stack;
	error_context_stack = &errcontext;

	/* nextXid must be beyond record's xid */
	if (TransactionIdFollowsOrEquals(record->xl_xid,
									 ShmemVariableCache->nextXid))
	{
		ShmemVariableCache->nextXid = record->xl_xid;
		TransactionIdAdvance(ShmemVariableCache->nextXid);
	}

	if (record->xl_info & XLR_BKP_BLOCK_MASK)
		RestoreBkpBlocks(record, *lsn);

	RmgrTable[record->xl_rmid].rm_redo(*beginLoc, *lsn, record);

	/* Pop the error context stack */
	error_context_stack = errcontext.previous;

	MIRROREDLOCK_BUFMGR_VERIFY_NO_LOCK_LEAK_EXIT;

}

/*
 * Process passed checkpoint record either during normal recovery or
 * in standby mode.
 *
 * If in standby mode, master mirroring information stored by the checkpoint
 * record is processed as well.
 */
static void
XLogProcessCheckpointRecord(XLogRecord *rec, XLogRecPtr loc)
{
	TMGXACT_CHECKPOINT 	*dtxCheckpoint;
	uint32 				dtxCheckpointLen;
	char				*masterMirroringCheckpoint;
	uint32				masterMirroringCheckpointLen;
	prepared_transaction_agg_state  *ptas = NULL;

	/* In standby mode, empty all master mirroring related hash tables. */
	if (IsStandbyMode())
		mmxlog_empty_hashtables();

	UnpackCheckPointRecord(
						rec,
						&loc,
						&dtxCheckpoint,
						&dtxCheckpointLen,
						&masterMirroringCheckpoint,
						&masterMirroringCheckpointLen,
						(IsStandbyMode() ? PANIC : LOG),
						&ptas);

	if (Debug_persistent_recovery_print && dtxCheckpoint != NULL)
	{
		elog(PersistentRecovery_DebugPrintLevel(),
			 "XLogProcessCheckPoint: Checkpoint record data length = %u, DTX "
			 "committed count %d, DTX data length %u, Master Mirroring "
			 "information length %u, location %s",
			 rec->xl_len,
			 dtxCheckpoint->committedCount,
			 dtxCheckpointLen,
			 masterMirroringCheckpointLen,
			 XLogLocationToString(&loc));

		if (ptas != NULL)
		{
			elog(PersistentRecovery_DebugPrintLevel(),
			"XLogProcessCheckPoint: Checkpoint record prepared transaction "
			"agg state count = %d",
			ptas->count);
		}

		if (masterMirroringCheckpointLen > 0)
		{
			int filespaceCount;
			int tablespaceCount;
			int databaseCount;

			if (mmxlog_get_checkpoint_counts(
									masterMirroringCheckpoint,
									masterMirroringCheckpointLen,
									rec->xl_len,
									&loc,
									/* errlevel */ COMMERROR,
									&filespaceCount,
									&tablespaceCount,
									&databaseCount))
			{
				elog(PersistentRecovery_DebugPrintLevel(),
					 "XLogProcessCheckPoint: master mirroring information: %d "
					 "filespaces, %d tablespaces, %d databases, location %s",
					 filespaceCount,
					 tablespaceCount,
					 databaseCount,
					 XLogLocationToString(&loc));
			}
		}
	}

	/*
	 * Get filespace, tablespace and database info from checkpoint record (master
	 * mirroring part) and maintain them in hash tables.
	 *
	 * We'll perform this only during standby mode because during normal non-standby
	 * recovery Persistent Tables do that job.
	 */
	if (IsStandbyMode() && masterMirroringCheckpoint != NULL)
	{
		mmxlog_read_checkpoint_data(masterMirroringCheckpoint,
									masterMirroringCheckpointLen,
									rec->xl_len,
									&loc);
	}

	/* Handle the DTX information. */
	if (dtxCheckpoint != NULL)
	{
		UtilityModeFindOrCreateDtmRedoFile();
		redoDtxCheckPoint(dtxCheckpoint);
		UtilityModeCloseDtmRedoFile();
	}
}


/*
 * This must be called ONCE during postmaster or standalone-backend startup
 *
 *	How Recovery works ?
 *---------------------------------------------------------------
 *| Clean Shutdown case    	| 	Not Clean Shutdown  case|
 *|(InRecovery = false)		|	(InRecovery = true)	|
 *---------------------------------------------------------------
 *|				|		   |		|
 *|				|record after	   |record after|
 *|				|checkpoint =	   |checkpoint =|
 *|				|NULL		   |NOT NULL	|
 *|				|(bypass Redo  	   |(dont bypass|
 *|				|  	   	   |Redo	|
 *---------------------------------------------------------------
 *|				|		   |		|
 *|	No Redo			|No Redo	   |Redo done	|
 *|	No Recovery Passes	|Recovery Pass done|Recovery 	|
 *|				|		   |Pass done	|
 *---------------------------------------------------------------
 */
void
StartupXLOG(void)
{
	XLogCtlInsert *Insert;
	CheckPoint	checkPoint;
	bool		wasShutdown;
	bool		reachedStopPoint = false;
	bool		haveBackupLabel = false;
	XLogRecPtr	RecPtr,
				LastRec,
				checkPointLoc,
				EndOfLog;
	uint32		endLogId;
	uint32		endLogSeg;
	XLogRecord *record;
	uint32		freespace;
	bool		multipleRecoveryPassesNeeded = false;
	bool		backupEndRequired = false;

	/*
	 * Read control file and check XLOG status looks valid.
	 *
	 * Note: in most control paths, *ControlFile is already valid and we need
	 * not do ReadControlFile() here, but might as well do it to be sure.
	 */
	ReadControlFile();

	if (ControlFile->logSeg == 0 ||
		ControlFile->state < DB_SHUTDOWNED ||
		ControlFile->state > DB_IN_PRODUCTION ||
		!XRecOffIsValid(ControlFile->checkPoint.xrecoff))
		ereport(FATAL,
				(errmsg("control file contains invalid data")));

	if (ControlFile->state == DB_SHUTDOWNED)
		ereport(LOG,
				(errmsg("database system was shut down at %s",
						str_time(ControlFile->time))));
	else if (ControlFile->state == DB_SHUTDOWNING)
		ereport(LOG,
				(errmsg("database system shutdown was interrupted at %s",
						str_time(ControlFile->time))));
	else if (ControlFile->state == DB_IN_CRASH_RECOVERY)
		ereport(LOG,
		   (errmsg("database system was interrupted while in recovery at %s",
				   str_time(ControlFile->time)),
			errhint("This probably means that some data is corrupted and"
					" you will have to use the last backup for recovery."),
			errSendAlert(true)));
	else if (ControlFile->state == DB_IN_STANDBY_MODE)
		ereport(LOG,
				(errmsg("database system was interrupted while in standby mode at  %s",
						str_time(ControlFile->checkPointCopy.time)),
						errhint("This probably means something unexpected happened either"
								" during replay at standby or receipt of XLog from primary."),
				 errSendAlert(true)));
	else if (ControlFile->state == DB_IN_STANDBY_PROMOTED)
		ereport(LOG,
				(errmsg("database system was interrupted after standby was promoted at %s",
						str_time(ControlFile->checkPointCopy.time)),
				 errhint("If this has occurred more than once something unexpected is happening"
				" after standby has been promoted"),
				 errSendAlert(true)));
	else if (ControlFile->state == DB_IN_STANDBY_NEW_TLI_SET)
		ereport(LOG,
				(errmsg("database system was interrupted post new TLI was setup on standby promotion at %s",
						str_time(ControlFile->checkPointCopy.time)),
						 errhint("If this has occurred more than once something unexpected is happening"
						" after standby has been promoted and new TLI has been set"),
				 errSendAlert(true)));
	else if (ControlFile->state == DB_IN_PRODUCTION)
		ereport(LOG,
				(errmsg("database system was interrupted at %s",
						str_time(ControlFile->time))));

	/* This is just to allow attaching to startup process with a debugger */
#ifdef XLOG_REPLAY_DELAY
	if (ControlFile->state != DB_SHUTDOWNED)
		pg_usleep(60000000L);
#endif

	/* Verify that pg_xlog exist */
	ValidateXLOGDirectoryStructure();

	/*
	 * Clear out any old relcache cache files.	This is *necessary* if we do
	 * any WAL replay, since that would probably result in the cache files
	 * being out of sync with database reality.  In theory we could leave them
	 * in place if the database had been cleanly shut down, but it seems
	 * safest to just remove them always and let them be rebuilt during the
	 * first backend startup.
	 */
	RelationCacheInitFileRemove();

	/*
	 * Initialize on the assumption we want to recover to the same timeline
	 * that's active according to pg_control.
	 */
	recoveryTargetTLI = ControlFile->checkPointCopy.ThisTimeLineID;

	/*
	 * Check for recovery control file, and if so set up state for offline
	 * recovery
	 */
	XLogReadRecoveryCommandFile(LOG);

	if (StandbyModeRequested)
	{
		Assert(ControlFile->state != DB_IN_CRASH_RECOVERY
				&& ControlFile->state != DB_IN_STANDBY_NEW_TLI_SET);

		/*
		 * If the standby was promoted (last time) and recovery.conf
		 * is still found this time with standby mode request,
		 * it means the standby crashed post promotion but before recovery.conf
		 * cleanup. Hence, it is not considered a standby request this time.
		 */
		if (ControlFile->state == DB_IN_STANDBY_PROMOTED)
			StandbyModeRequested = false;
	}

	/* Now we can determine the list of expected TLIs */
	expectedTLIs = XLogReadTimeLineHistory(recoveryTargetTLI);

	/*
	 * If pg_control's timeline is not in expectedTLIs, then we cannot
	 * proceed: the backup is not part of the history of the requested
	 * timeline.
	 */
	if (!list_member_int(expectedTLIs,
						 (int) ControlFile->checkPointCopy.ThisTimeLineID))
		ereport(FATAL,
				(errmsg("requested timeline %u is not a child of database system timeline %u",
						recoveryTargetTLI,
						ControlFile->checkPointCopy.ThisTimeLineID)));

	/*
	 * Save the selected recovery target timeline ID in shared memory so that
	 * other processes can see them
	 */
	XLogCtl->RecoveryTargetTLI = recoveryTargetTLI;

	if (StandbyModeRequested)
		ereport(LOG,
				(errmsg("entering standby mode")));

	/*
	 * Take ownership of the wakeup latch if we're going to sleep during
	 * recovery.
	 */
	if (StandbyModeRequested)
		OwnLatch(&XLogCtl->recoveryWakeupLatch);

	if (read_backup_label(&checkPointLoc, &backupEndRequired))
	{
		/*
		 * Currently, it is assumed that a backup file exists iff a base backup
		 * has been performed and then the recovery.conf file is generated, thus
		 * standby mode has to be requested
		 */
		if (!StandbyModeRequested)
			ereport(FATAL,
					(errmsg("Found backup.label file without any standby mode request")));

		/* Activate recovery in standby mode */
		StandbyMode = true;

		Assert(backupEndRequired);

		/*
		 * When a backup_label file is present, we want to roll forward from
		 * the checkpoint it identifies, rather than using pg_control.
		 */
		record = ReadCheckpointRecord(checkPointLoc, 0);
		if (record != NULL)
		{
			memcpy(&checkPoint, XLogRecGetData(record), sizeof(CheckPoint));
			wasShutdown = (record->xl_info == XLOG_CHECKPOINT_SHUTDOWN);
			ereport(LOG,
					(errmsg("checkpoint record is at %X/%X",
							checkPointLoc.xlogid, checkPointLoc.xrecoff)));
			InRecovery = true;	/* force recovery even if SHUTDOWNED */

			/*
			 * Make sure that REDO location exists. This may not be the case
			 * if there was a crash during an online backup
			 */
			if (XLByteLT(checkPoint.redo, checkPointLoc))
			{
				if (!XLogReadRecord(&(checkPoint.redo), false, LOG))
					ereport(FATAL,
							(errmsg("could not find redo location referenced by checkpoint record"),
							 errhint("If you are not restoring from a backup, try removing the file \"%s/backup_label\".", DataDir)));
			}
		}
		else
		{
			ereport(PANIC,
					(errmsg("could not locate required checkpoint record"),
					 errhint("If you are not restoring from a backup, try removing the file \"%s/backup_label\".", DataDir)));
			wasShutdown = false;	/* keep compiler quiet */
		}
		/* set flag to delete it later */
		haveBackupLabel = true;
	}
	else
	{
		if (StandbyModeRequested)
		{
			/* Activate recovery in standby mode */
			StandbyMode = true;
		}

		/*
		 * Get the last valid checkpoint record.  If the latest one according
		 * to pg_control is broken, try the next-to-last one.
		 */
		checkPointLoc = ControlFile->checkPoint;
		RedoStartLSN = ControlFile->checkPointCopy.redo;

		record = ReadCheckpointRecord(checkPointLoc, 1);
		if (record != NULL)
		{
			ereport(LOG,
					(errmsg("checkpoint record is at %X/%X",
							checkPointLoc.xlogid, checkPointLoc.xrecoff)));
		}
		else if (StandbyMode)
		{
			/*
			 * The last valid checkpoint record required for a streaming
			 * recovery exists in neither standby nor the primary.
			 */
			ereport(PANIC,
					(errmsg("could not locate a valid checkpoint record")));
		}
		else
		{
			printEndOfXLogFile(&checkPointLoc);

			checkPointLoc = ControlFile->prevCheckPoint;
			record = ReadCheckpointRecord(checkPointLoc, 2);
			if (record != NULL)
			{
				ereport(LOG,
						(errmsg("using previous checkpoint record at %X/%X",
							  checkPointLoc.xlogid, checkPointLoc.xrecoff)));
				InRecovery = true;		/* force recovery even if SHUTDOWNED */
			}
			else
			{
				printEndOfXLogFile(&checkPointLoc);
				ereport(PANIC,
					 (errmsg("could not locate a valid checkpoint record")));
			}
		}
		memcpy(&checkPoint, XLogRecGetData(record), sizeof(CheckPoint));
		wasShutdown = (record->xl_info == XLOG_CHECKPOINT_SHUTDOWN);
	}

	LastRec = RecPtr = checkPointLoc;
	XLogCtl->pass1LastCheckpointLoc = checkPointLoc;

	/*
	 * Currently, standby mode (WAL based replication support) is not provided
	 * to segments.
	 * Hence it's okay to do the following only once on the segments as there
	 * will be only one checkpoint to be analyzed.
	 */
	if (GpIdentity.segindex != MASTER_CONTENT_ID)
		SetupCheckpointPreparedTransactionList(record);

	/*
	 * Find Xacts that are distributed committed from the checkpoint record and
	 * store them such that they can utilized later during DTM recovery.
	 */
	XLogProcessCheckpointRecord(record, checkPointLoc);

	ereport(LOG,
	 (errmsg("redo record is at %X/%X; undo record is at %X/%X; shutdown %s",
			 checkPoint.redo.xlogid, checkPoint.redo.xrecoff,
			 checkPoint.undo.xlogid, checkPoint.undo.xrecoff,
			 wasShutdown ? "TRUE" : "FALSE")));
	ereport(LOG,
			(errmsg("next transaction ID: %u/%u; next OID: %u",
					checkPoint.nextXidEpoch, checkPoint.nextXid,
					checkPoint.nextOid)));
	ereport(LOG,
			(errmsg("next MultiXactId: %u; next MultiXactOffset: %u",
					checkPoint.nextMulti, checkPoint.nextMultiOffset)));

	if (!TransactionIdIsNormal(checkPoint.nextXid))
		ereport(PANIC,
				(errmsg("invalid next transaction ID")));

	/* initialize shared memory variables from the checkpoint record */
	ShmemVariableCache->nextXid = checkPoint.nextXid;
	ShmemVariableCache->nextOid = checkPoint.nextOid;
	ShmemVariableCache->oidCount = 0;
	MultiXactSetNextMXact(checkPoint.nextMulti, checkPoint.nextMultiOffset);
	XLogCtl->ckptXidEpoch = checkPoint.nextXidEpoch;
	XLogCtl->ckptXid = checkPoint.nextXid;

	/*
	 * We must replay WAL entries using the same TimeLineID they were created
	 * under, so temporarily adopt the TLI indicated by the checkpoint (see
	 * also xlog_redo()).
	 */
	ThisTimeLineID = checkPoint.ThisTimeLineID;

	RedoRecPtr = XLogCtl->Insert.RedoRecPtr = checkPoint.redo;

	if (XLByteLT(RecPtr, checkPoint.redo))
		ereport(PANIC,
				(errmsg("invalid redo in checkpoint record")));
	if (checkPoint.undo.xrecoff == 0)
		checkPoint.undo = RecPtr;

	/*
	 * Check whether we need to force recovery from WAL.  If it appears to
	 * have been a clean shutdown and we did not have a recovery.conf file,
	 * then assume no recovery needed.
	 */
	if (XLByteLT(checkPoint.undo, RecPtr) ||
		XLByteLT(checkPoint.redo, RecPtr))
	{
		if (wasShutdown)
			ereport(PANIC,
				(errmsg("invalid redo/undo record in shutdown checkpoint")));
		InRecovery = true;
	}
	else if (StandbyModeRequested)
	{
		/* force recovery due to presence of recovery.conf */
		ereport(LOG,
				(errmsg("setting recovery standby mode active")));
		InRecovery = true;
	}
	else if (ControlFile->state != DB_SHUTDOWNED)
		InRecovery = true;

	/*
	 * We need to create the pg_distributedlog directory if we are
	 * upgrading from before 3.1.1.4 patch.
	 *
	 * Force a recovery to replay into the distributed log the
	 * recent distributed transaction commits.
	 */
	if (DistributedLog_UpgradeCheck(InRecovery))
		InRecovery = true;

	if (InRecovery && !IsUnderPostmaster)
	{
		ereport(FATAL,
				(errmsg("Database must be shutdown cleanly when using single backend start")));
	}

	if (InRecovery && gp_before_persistence_work)
	{
		ereport(FATAL,
				(errmsg("Database must be shutdown cleanly when using gp_before_persistence_work = on")));
	}

	/* Recovery from xlog */
	if (InRecovery)
	{
		int			rmid;

		/* use volatile pointer to prevent code rearrangement */
		volatile XLogCtlData *xlogctl = XLogCtl;

		/*
		 * Update pg_control to show that we are recovering and to show the
		 * selected checkpoint as the place we are starting from. We also mark
		 * pg_control with any minimum recovery stop point
		 */
		if (StandbyMode)
		{
			ereport(LOG,
					(errmsg("recovery in standby mode in progress")));
			ControlFile->state = DB_IN_STANDBY_MODE;
		}
		else
		{
			ereport(LOG,
					(errmsg("database system was not properly shut down; "
							"automatic recovery in progress")));

			if (ControlFile->state != DB_IN_STANDBY_PROMOTED
				&& ControlFile->state != DB_IN_STANDBY_NEW_TLI_SET)
				ControlFile->state = DB_IN_CRASH_RECOVERY;
		}

		ControlFile->prevCheckPoint = ControlFile->checkPoint;
		ControlFile->checkPoint = checkPointLoc;
		ControlFile->checkPointCopy = checkPoint;

		if (StandbyMode)
		{
			/* initialize minRecoveryPoint if not set yet */
			if (XLByteLT(ControlFile->minRecoveryPoint, checkPoint.redo))
				ControlFile->minRecoveryPoint = checkPoint.redo;
		}

		/* Set backupStartPoint if we're starting recovery from a base backup. */
		if (haveBackupLabel)
		{
			Assert(ControlFile->state == DB_IN_STANDBY_MODE);
			ControlFile->backupStartPoint = checkPoint.redo;
			ControlFile->backupEndRequired = backupEndRequired;
		}

		ControlFile->time = time(NULL);
		UpdateControlFile();

		pgstat_reset_all();

		/*
		 * If there was a backup label file, it's done its job and the info
		 * has now been propagated into pg_control.  We must get rid of the
		 * label file so that if we crash during recovery, we'll pick up at
		 * the latest recovery restartpoint instead of going all the way back
		 * to the backup start point.  It seems prudent though to just rename
		 * the file out of the way rather than delete it completely.
		 */
		if (haveBackupLabel)
		{
			unlink(BACKUP_LABEL_OLD);
			if (rename(BACKUP_LABEL_FILE, BACKUP_LABEL_OLD) != 0)
				ereport(FATAL,
						(errcode_for_file_access(),
						 errmsg("could not rename file \"%s\" to \"%s\": %m",
								BACKUP_LABEL_FILE, BACKUP_LABEL_OLD)));
		}

		/* Start up the recovery environment */
		XLogInitRelationCache();

		UtilityModeFindOrCreateDtmRedoFile();

		for (rmid = 0; rmid <= RM_MAX_ID; rmid++)
		{
			if (RmgrTable[rmid].rm_startup != NULL)
				RmgrTable[rmid].rm_startup();
		}

		/*
		 * Initialize shared lastReplayedEndRecPtr.
		 *
		 * This is slightly confusing if we're starting from an online
		 * checkpoint; we've just read and replayed the chekpoint record, but
		 * we're going to start replay from its redo pointer, which precedes
		 * the location of the checkpoint record itself. So even though the
		 * last record we've replayed is indeed ReadRecPtr, we haven't
		 * replayed all the preceding records yet. That's OK for the current
		 * use of these variables.
		 */
		SpinLockAcquire(&xlogctl->info_lck);
		xlogctl->lastReplayedEndRecPtr = EndRecPtr;
		xlogctl->currentChunkStartTime = 0;
		SpinLockRelease(&xlogctl->info_lck);

		/* Also ensure XLogReceiptTime has a sane value */
		XLogReceiptTime = GetCurrentTimestamp();

		/*
		 * Find the first record that logically follows the checkpoint --- it
		 * might physically precede it, though.
		 */
		if (XLByteLT(checkPoint.redo, RecPtr))
		{
			/* back up to find the record */
			record = XLogReadRecord(&(checkPoint.redo), false, PANIC);
		}
		else
		{
			/* just have to read next record after CheckPoint */
			record = XLogReadRecord(NULL, false, LOG);
		}

		/*
		 * In case where its not a clean shutdown but it doesn't have a record
		 * following the checkpoint record, just proceed with the Pass 2, 3, 4
		 * to clear any inconsistent entries in Persistent Tables without
		 * doing the whole redo loop below.
		 */
		if (record == NULL)	
		{
			/*
			 * There are no WAL records following the checkpoint
			 */
			ereport(LOG,
					(errmsg("no record for redo after checkpoint, skip redo and proceed for recovery pass")));
		}

		XLogCtl->pass1StartLoc = ReadRecPtr;

		/*
		 * MPP-11179
		 * Recovery Passes will be done in both the cases:
		 * 1. When record after checkpoint = NULL (No redo)
		 * 2. When record after checkpoint != NULL (redo also)
		 */
		multipleRecoveryPassesNeeded = true;

		/*
		 * main redo apply loop, executed if we have record after checkpoint
		 */
		if (record != NULL)
		{
			bool		recoveryContinue = true;
			bool		recoveryApply = true;
			InRedo = true;
			bool		lastReadRecWasCheckpoint=false;
			CurrentResourceOwner = ResourceOwnerCreate(NULL, "xlog");

			ereport(LOG,
						(errmsg("redo starts at %X/%X",
								ReadRecPtr.xlogid, ReadRecPtr.xrecoff)));
			do
			{
				ErrorContextCallback errcontext;

				HandleStartupProcInterrupts();

				/*
				 * Have we reached our recovery target?
				 */
				if (recoveryStopsHere(record, &recoveryApply))
				{
					reachedStopPoint = true;		/* see below */
					recoveryContinue = false;
					if (!recoveryApply)
						break;
				}

				errcontext.callback = StartupXLOG_RedoPass1Context;
				errcontext.arg = (void *) record;
				errcontext.previous = error_context_stack;
				error_context_stack = &errcontext;

				/*
				 * Replay every XLog record read in continuous recovery (standby) mode
				 * But while in normal crash recovery mode apply only Persistent
				 * Tables' related XLog records
				 */
				if (IsStandbyMode() ||
					PersistentRecovery_ShouldHandlePass1XLogRec(&ReadRecPtr, &EndRecPtr, record))
				{
					/*
					 * See if this record is a checkpoint, if yes then uncover it to
					 * find distributed committed Xacts.
					 * No need to unpack checkpoint in crash recovery mode
					 */
					uint8 xlogRecInfo = record->xl_info & ~XLR_INFO_MASK;

					if (IsStandbyMode() &&
						record->xl_rmid == RM_XLOG_ID &&
						(xlogRecInfo == XLOG_CHECKPOINT_SHUTDOWN
						|| xlogRecInfo == XLOG_CHECKPOINT_ONLINE))
					{
						XLogProcessCheckpointRecord(record, ReadRecPtr);
						XLogCtl->pass1LastCheckpointLoc = ReadRecPtr;
						memcpy(&checkPoint, XLogRecGetData(record), sizeof(CheckPoint));
						lastReadRecWasCheckpoint = true;
					}

					ApplyStartupRedo(&ReadRecPtr, &EndRecPtr, record);

					/*
					 * Update lastReplayedEndRecPtr after this record has been
					 * successfully replayed.
					 */
					SpinLockAcquire(&xlogctl->info_lck);
					xlogctl->lastReplayedEndRecPtr = EndRecPtr;
					SpinLockRelease(&xlogctl->info_lck);
				}

				/* Pop the error context stack */
				error_context_stack = errcontext.previous;

				LastRec = ReadRecPtr;

				record = XLogReadRecord(NULL, false, LOG);

				/*
				 *  If the last (actually it is last-to-last in case there is any
				 *  record after the latest checkpoint record during the reading
				 *  the Xlog records) record is a checkpoint, then startlocation
				 *  for Pass 1 should be decided
				 *
				 *  This step looks redundant in case of normal recovery (no
				 *  standby mode) but its not that costly.
				 */
				if (lastReadRecWasCheckpoint)
				{
					if (XLByteLT(checkPoint.redo, RecPtr))
						XLogCtl->pass1StartLoc = checkPoint.redo;
					else
						XLogCtl->pass1StartLoc = ReadRecPtr;
					lastReadRecWasCheckpoint = false;
				}

			} while (record != NULL && recoveryContinue);

			ereport(LOG,
					(errmsg("redo done at %X/%X",
							ReadRecPtr.xlogid, ReadRecPtr.xrecoff)));

			CurrentResourceOwner = NULL;
			InRedo = false;
		}
		/*
		 * end of main redo apply loop
		 */
	}

	/*
	 * Kill WAL receiver, if it's still running, before we continue to write
	 * the startup checkpoint record. It will trump over the checkpoint and
	 * subsequent records if it's still alive when we start writing WAL.
	 */
	ShutdownWalRcv();

	/*
	 * We don't need the latch anymore. It's not strictly necessary to disown
	 * it, but let's do it for the sake of tidiness.
	 */
	if (StandbyModeRequested)
		DisownLatch(&XLogCtl->recoveryWakeupLatch);

	/*
	 * We are now done reading the xlog from stream.
	 */
	if (StandbyMode)
	{
		Assert(ControlFile->state == DB_IN_STANDBY_MODE);
		StandbyMode = false;

		/* Transition to promoted mode */
		ControlFile->state = DB_IN_STANDBY_PROMOTED;
		ControlFile->time = time(NULL);
		UpdateControlFile();
	}

	/*
	 * Re-fetch the last valid or last applied record, so we can identify the
	 * exact endpoint of what we consider the valid portion of WAL.
	 */
	record = XLogReadRecord(&LastRec, false, PANIC);
	EndOfLog = EndRecPtr;
	XLByteToPrevSeg(EndOfLog, endLogId, endLogSeg);

	elog(LOG,"end of transaction log location is %s",
		 XLogLocationToString(&EndOfLog));

	XLogCtl->pass1LastLoc = ReadRecPtr;

	/*
	 * Complain if we did not roll forward far enough to render the backup
	 * dump consistent
	 */
	if (InRecovery &&
		(XLByteLT(EndOfLog, ControlFile->minRecoveryPoint) ||
		 !XLogRecPtrIsInvalid(ControlFile->backupStartPoint)))
	{
		/*
		 * Ran off end of WAL before reaching end-of-backup WAL record, or
		 * minRecoveryPoint. That's usually a bad sign, indicating that you
		 * tried to recover from an online backup but never called
		 * pg_stop_backup(), or you didn't archive all the WAL up to that
		 * point. However, this also happens in crash recovery, if the system
		 * crashes while an online backup is in progress. We must not treat
		 * that as an error, or the database will refuse to start up.
		 */
		if (StandbyModeRequested || ControlFile->backupEndRequired)
		{
			if (ControlFile->backupEndRequired)
				ereport(FATAL,
						(errmsg("WAL ends before end of online backup"),
						 errhint("All WAL generated while online backup was taken must be available at recovery.")));
			else if (!XLogRecPtrIsInvalid(ControlFile->backupStartPoint))
				ereport(FATAL,
						(errmsg("WAL ends before end of online backup"),
						 errhint("Online backup should be complete, and all WAL up to that point must be available at recovery.")));
			else
				ereport(FATAL,
					  (errmsg("WAL ends before consistent recovery point")));
		}
	}

	/* Save the selected TimeLineID in shared memory, too */
	XLogCtl->ThisTimeLineID = ThisTimeLineID;

	/*
	 * Prepare to write WAL starting at EndOfLog position, and init xlog
	 * buffer cache using the block containing the last record from the
	 * previous incarnation.
	 */
	openLogId = endLogId;
	openLogSeg = endLogSeg;
	XLogFileOpen(
			&mirroredLogFileOpen,
			openLogId,
			openLogSeg);
	openLogOff = 0;
	ControlFile->logId = openLogId;
	ControlFile->logSeg = openLogSeg + 1;
	Insert = &XLogCtl->Insert;
	Insert->PrevRecord = LastRec;
	XLogCtl->xlblocks[0].xlogid = openLogId;
	XLogCtl->xlblocks[0].xrecoff =
		((EndOfLog.xrecoff - 1) / XLOG_BLCKSZ + 1) * XLOG_BLCKSZ;

	/*
	 * Tricky point here: readBuf contains the *last* block that the LastRec
	 * record spans, not the one it starts in.	The last block is indeed the
	 * one we want to use.
	 */
	Assert(readOff == (XLogCtl->xlblocks[0].xrecoff - XLOG_BLCKSZ) % XLogSegSize);
	memcpy((char *) Insert->currpage, readBuf, XLOG_BLCKSZ);
	Insert->currpos = (char *) Insert->currpage +
		(EndOfLog.xrecoff + XLOG_BLCKSZ - XLogCtl->xlblocks[0].xrecoff);

	LogwrtResult.Write = LogwrtResult.Flush = EndOfLog;

	XLogCtl->Write.LogwrtResult = LogwrtResult;
	Insert->LogwrtResult = LogwrtResult;
	XLogCtl->LogwrtResult = LogwrtResult;

	XLogCtl->LogwrtRqst.Write = EndOfLog;
	XLogCtl->LogwrtRqst.Flush = EndOfLog;

	freespace = INSERT_FREESPACE(Insert);
	if (freespace > 0)
	{
		/* Make sure rest of page is zero */
		MemSet(Insert->currpos, 0, freespace);
		XLogCtl->Write.curridx = 0;
	}
	else
	{
		/*
		 * Whenever Write.LogwrtResult points to exactly the end of a page,
		 * Write.curridx must point to the *next* page (see XLogWrite()).
		 *
		 * Note: it might seem we should do AdvanceXLInsertBuffer() here, but
		 * this is sufficient.	The first actual attempt to insert a log
		 * record will advance the insert state.
		 */
		XLogCtl->Write.curridx = NextBufIdx(0);
	}

	if (InRecovery)
	{
		/*
		 * Close down Recovery for Startup PASS 1.
		 */
		int			rmid;

		/*
		 * Allow resource managers to do any required cleanup.
		 */
		for (rmid = 0; rmid <= RM_MAX_ID; rmid++)
		{
			if (RmgrTable[rmid].rm_cleanup != NULL)
				RmgrTable[rmid].rm_cleanup();
		}

		/*
		 * Check to see if the XLOG sequence contained any unresolved
		 * references to uninitialized pages.
		 */
		XLogCheckInvalidPages();

		/*
		 * Reset pgstat data, because it may be invalid after recovery.
		 */
		pgstat_reset_all();

		/*
		 * We are not finished with multiple passes, so we do not do a
		 * shutdown checkpoint here as we did in the past.
		 *
		 * We only flush out the Resource Managers.
		 */
		Checkpoint_RecoveryPass(XLogCtl->pass1LastLoc);

		/*
		 * Close down recovery environment
		 */
		XLogCloseRelationCache();

		UtilityModeCloseDtmRedoFile();
	}

	/*
	 * Preallocate additional log files, if wanted.
	 */
	(void) PreallocXlogFiles(EndOfLog);

	/*
	 * Okay, we're finished with Pass 1.
	 */
	InRecovery = false;

	/* start the archive_timeout timer running */
	XLogCtl->Write.lastSegSwitchTime = ControlFile->time;

	/* initialize shared-memory copy of latest checkpoint XID/epoch */
	XLogCtl->ckptXidEpoch = ControlFile->checkPointCopy.nextXidEpoch;
	XLogCtl->ckptXid = ControlFile->checkPointCopy.nextXid;

	if (!gp_before_persistence_work)
	{
		/*
		 * Create a resource owner to keep track of our resources (currently only
		 * buffer pins).
		 */
		CurrentResourceOwner = ResourceOwnerCreate(NULL, "StartupXLOG");

		/*
		 * We don't have any hope of running a real relcache, but we can use the
		 * same fake-relcache facility that WAL replay uses.
		 */
		XLogInitRelationCache();

		/*
		 * During startup after we have performed recovery is the only place we
		 * scan in the persistent meta-data into memory on already initdb database.
		 */
		PersistentFileSysObj_StartupInitScan();

		/*
		 * Close down recovery environment
		 */
		XLogCloseRelationCache();
	}

	if (!IsUnderPostmaster)
	{
		Assert(!multipleRecoveryPassesNeeded);

		StartupXLOG_InProduction();

		ereport(LOG,
				(errmsg("Finished single backend startup")));
	}
	else
	{
		XLogCtl->multipleRecoveryPassesNeeded = multipleRecoveryPassesNeeded;

		if (!gp_startup_integrity_checks)
		{
			ereport(LOG,
					(errmsg("Integrity checks will be skipped because gp_startup_integrity_checks = off")));
		}
		else
		{
			XLogCtl->integrityCheckNeeded = true;
		}

		if (!XLogCtl->multipleRecoveryPassesNeeded)
		{
			StartupXLOG_InProduction();

			ereport(LOG,
					(errmsg("Finished normal startup for clean shutdown case")));

		}
		else
		{
			ereport(LOG,
					(errmsg("Finished startup pass 1.  Proceeding to startup crash recovery passes 2 and 3.")));
		}
	}

	XLogCloseReadRecord();
}

bool XLogStartupMultipleRecoveryPassesNeeded(void)
{
	Assert(XLogCtl != NULL);
	return XLogCtl->multipleRecoveryPassesNeeded;
}

bool XLogStartupIntegrityCheckNeeded(void)
{
	Assert(XLogCtl != NULL);
	return XLogCtl->integrityCheckNeeded;
}

static void
GetRedoRelationFileName(char *path)
{
	char *xlogDir = makeRelativeToTxnFilespace(XLOGDIR);
	if (snprintf(path, MAXPGPATH, "%s/RedoRelationFile", xlogDir) > MAXPGPATH)
	{
		ereport(ERROR, (errmsg("cannot generate pathname %s/RedoRelationFile", xlogDir)));
	}
	pfree(xlogDir);
}

static int
CreateRedoRelationFile(void)
{
	char	path[MAXPGPATH];

	int		result;

	GetRedoRelationFileName(path);

	result = open(path, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
	if (result < 0)
	{
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not create redo relation file \"%s\"",
						path)));
	}

	return result;
}

static int
OpenRedoRelationFile(void)
{
	char	path[MAXPGPATH];

	int		result;

	GetRedoRelationFileName(path);

	result = open(path, O_RDONLY, S_IRUSR | S_IWUSR);
	if (result < 0)
	{
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not open redo relation file \"%s\"",
						path)));
	}

	return result;
}

static void
UnlinkRedoRelationFile(void)
{
	char	path[MAXPGPATH];

	GetRedoRelationFileName(path);

	if (unlink(path) < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not unlink redo relation file \"%s\": %m", path)));
}

/*
 * This must be called ONCE during postmaster or standalone-backend startup
 */
void
StartupXLOG_Pass2(void)
{
	XLogRecord *record;

	int redoRelationFile;

	if (Debug_persistent_recovery_print)
		elog(PersistentRecovery_DebugPrintLevel(),
		     "Entering StartupXLOG_Pass2");

	/*
	 * Read control file and verify XLOG status looks valid.
	 */
	ReadControlFile();

	if (ControlFile->logSeg == 0 ||
		ControlFile->state < DB_SHUTDOWNED ||
		ControlFile->state > DB_IN_PRODUCTION ||
		!XRecOffIsValid(ControlFile->checkPoint.xrecoff))
		ereport(FATAL,
				(errmsg("Startup Pass 2: control file contains invalid data")));

	recoveryTargetTLI = ControlFile->checkPointCopy.ThisTimeLineID;
	XLogCtl->RecoveryTargetTLI = recoveryTargetTLI;

	/* Now we can determine the list of expected TLIs */
	expectedTLIs = XLogReadTimeLineHistory(recoveryTargetTLI);

	if (Debug_persistent_recovery_print)
		elog(PersistentRecovery_DebugPrintLevel(),
		     "ControlFile with recoveryTargetTLI %u, transaction log to start location is %s",
			 recoveryTargetTLI,
			 XLogLocationToString(&XLogCtl->pass1StartLoc));

	if (GpIdentity.segindex != MASTER_CONTENT_ID)
	{
		if (Debug_persistent_recovery_print)
			elog(PersistentRecovery_DebugPrintLevel(),
				"Read the checkpoint record location saved from pass1, "
				"and setup the prepared transation hash list.");
		record = XLogReadRecord(&XLogCtl->pass1LastCheckpointLoc, false, PANIC);
		SetupCheckpointPreparedTransactionList(record);
	}

	record = XLogReadRecord(&XLogCtl->pass1StartLoc, false, PANIC);

	/*
	 * Pass 2 XLOG scan
	 */
	while (true)
	{
		PersistentRecovery_HandlePass2XLogRec(&ReadRecPtr, &EndRecPtr, record);

		if (XLByteEQ(ReadRecPtr, XLogCtl->pass1LastLoc))
			break;

		Assert(XLByteLE(ReadRecPtr,XLogCtl->pass1LastLoc));

		record = XLogReadRecord(NULL, false, PANIC);
	}
	XLogCloseReadRecord();

	PersistentRecovery_Scan();

	PersistentRecovery_CrashAbort();

	PersistentRecovery_Update();

	PersistentRecovery_Drop();

	PersistentRecovery_UpdateAppendOnlyMirrorResyncEofs();

#ifdef USE_ASSERT_CHECKING
//	PersistentRecovery_VerifyTablesAgainstMemory();
#endif

	Checkpoint_RecoveryPass(XLogCtl->pass1LastLoc);

	/*
	 * Create a file that passes information to pass 3.
	 */
	redoRelationFile = CreateRedoRelationFile();

	PersistentRecovery_SerializeRedoRelationFile(redoRelationFile);

	close(redoRelationFile);

	ereport(LOG,
			(errmsg("Finished startup crash recovery pass 2")));

	if (Debug_persistent_recovery_print)
		elog(PersistentRecovery_DebugPrintLevel(),
		     "Exiting StartupXLOG_Pass2");
}

/*
 * This must be called ONCE during postmaster or standalone-backend startup
 */
void
StartupXLOG_Pass3(void)
{
	int redoRelationFile;
	XLogRecord *record;
	int 		rmid;
	uint32		endLogId;
	uint32		endLogSeg;

	if (Debug_persistent_recovery_print)
		elog(PersistentRecovery_DebugPrintLevel(),
		     "Entering StartupXLOG_Pass3");

	redoRelationFile = OpenRedoRelationFile();

	PersistentRecovery_DeserializeRedoRelationFile(redoRelationFile);

	close(redoRelationFile);

	UnlinkRedoRelationFile();

	if (Debug_persistent_recovery_print)
		elog(PersistentRecovery_DebugPrintLevel(),
			 "StartupXLOG_Pass3: Begin re-scanning XLOG");

	InRecovery = true;

	/*
	 * Read control file and verify XLOG status looks valid.
	 */
	ReadControlFile();

	if (ControlFile->logSeg == 0 ||
		ControlFile->state < DB_SHUTDOWNED ||
		ControlFile->state > DB_IN_PRODUCTION ||
		!XRecOffIsValid(ControlFile->checkPoint.xrecoff))
		ereport(FATAL,
				(errmsg("Startup Pass 2: control file contains invalid data")));

	recoveryTargetTLI = ControlFile->checkPointCopy.ThisTimeLineID;
	XLogCtl->RecoveryTargetTLI = recoveryTargetTLI;

	/* Now we can determine the list of expected TLIs */
	expectedTLIs = XLogReadTimeLineHistory(recoveryTargetTLI);

	if (Debug_persistent_recovery_print)
	{
		elog(PersistentRecovery_DebugPrintLevel(),
			 "ControlFile with recoveryTargetTLI %u, transaction log to start location is %s",
			 recoveryTargetTLI,
			 XLogLocationToString(&XLogCtl->pass1StartLoc));
		elog(PersistentRecovery_DebugPrintLevel(),
		     "StartupXLOG_RedoPass3Context: Control File checkpoint location is %s",
		     XLogLocationToString(&ControlFile->checkPoint));
	}

	/* Start up the recovery environment */
	XLogInitRelationCache();

	UtilityModeFindOrCreateDtmRedoFile();

	for (rmid = 0; rmid <= RM_MAX_ID; rmid++)
	{
		if (RmgrTable[rmid].rm_startup != NULL)
			RmgrTable[rmid].rm_startup();
	}

	if (GpIdentity.segindex != MASTER_CONTENT_ID)
	{
		record = XLogReadRecord(&XLogCtl->pass1LastCheckpointLoc, false, PANIC);
		SetupCheckpointPreparedTransactionList(record);
	}

	record = XLogReadRecord(&XLogCtl->pass1StartLoc, false, PANIC);

	/*
	 * Pass 3 XLOG scan
	 */
	while (true)
	{
		ErrorContextCallback errcontext;

		/* Setup error traceback support for ereport() */
		errcontext.callback = StartupXLOG_RedoPass3Context;
		errcontext.arg = (void *) record;
		errcontext.previous = error_context_stack;
		error_context_stack = &errcontext;

		if (PersistentRecovery_ShouldHandlePass3XLogRec(&ReadRecPtr, &EndRecPtr, record))
			ApplyStartupRedo(&ReadRecPtr, &EndRecPtr, record);

		/* Pop the error context stack */
		error_context_stack = errcontext.previous;

		/*
		 * For Pass 3, we read through the new log generated by Pass 2 in case
		 * there are Master Mirror XLOG records we need to take action on.
		 *
		 * It is obscure: Pass 3 REDO of Create fs-obj may need to be compensated for
		 * by Deletes generated in Pass 2...
		 */
		record = XLogReadRecord(NULL, false, LOG);
		if (record == NULL)
			break;
	}

	XLogCloseReadRecord();

	/*
	 * Consider whether we need to assign a new timeline ID.
	 *
	 * If we were in standby mode, we always assign a new ID.
	 * This currently helps for avoiding standby fail-back situation (If the original
	 * primary is down and original standby is acting as the new primary, in such
	 * a case original primary can't act as the new standby to avoid XLog mismatch)
	 *
	 * In a normal crash recovery, we can just extend the timeline we were in.
	 */
	if (ControlFile->state == DB_IN_STANDBY_PROMOTED)
	{
		/* Read the last XLog record */
		record = XLogReadRecord(&XLogCtl->pass1LastLoc, false, PANIC);
		XLByteToPrevSeg(EndRecPtr, endLogId, endLogSeg);

		ThisTimeLineID = findNewestTimeLine(recoveryTargetTLI) + 1;
		writeTimeLineHistory(ThisTimeLineID, recoveryTargetTLI,
							curFileTLI, endLogId, endLogSeg);
		ereport(LOG,
				(errmsg("selected new timeline ID: %u", ThisTimeLineID)));

		/* Save the selected TimeLineID in shared memory, too */
		XLogCtl->ThisTimeLineID = ThisTimeLineID;

		/*
		 * We are now done reading the old WAL. Make a writable copy of the last
		 * WAL segment.
		 */
		exitArchiveRecovery(curFileTLI, endLogId, endLogSeg);

		XLogCloseReadRecord();
	}

	/*
	 * Allow resource managers to do any required cleanup.
	 */
	for (rmid = 0; rmid <= RM_MAX_ID; rmid++)
	{
		if (RmgrTable[rmid].rm_cleanup != NULL)
			RmgrTable[rmid].rm_cleanup();
	}

	/*
	 * Check to see if the XLOG sequence contained any unresolved
	 * references to uninitialized pages.
	 */
	XLogCheckInvalidPages();

	/* Reset pgstat data, because it may be invalid after recovery */
	pgstat_reset_all();

	/*
	 * Close down recovery environment
	 */
	XLogCloseRelationCache();

	UtilityModeCloseDtmRedoFile();

	if (Debug_persistent_recovery_print)
		elog(PersistentRecovery_DebugPrintLevel(),
			 "StartupXLOG_Pass3: End re-scanning XLOG");

	InRecovery = false;

	StartupXLOG_InProduction();

	ereport(LOG,
			(errmsg("Finished startup crash recovery pass 3")));

	if (Debug_persistent_recovery_print)
		elog(PersistentRecovery_DebugPrintLevel(),
			 "Exiting StartupXLOG_Pass3");
}

/*
 * Startup Pass 4 can perform basic integrity checks as well as
 * PersistentTable-Catalog verification (if appropriate GUC is turned on).
 * If the GUC is NOT set --
 * 1. Only basic integrity checks will be performed.
 *
 * If the GUC is set --
 * 1. Both Non-DB specific and DB-specific verification checks
 * are executed to see if the system is consistent.
 * 2. First run of Pass 4 performs basic integrity checks and
 * non-DB specific checks. At the same time, next DB on which DB-specific
 * verifications are to be performed is selected.
 * 3. This DB selected in #2 will be used in the next cycle of Pass 4
 * as a new spawned process for verification purposes. At the same time,
 * a new database is selected for the subsequent cycle and thus this
 * continues until there are no more DBs left to be verified in the system.
 */
void
StartupXLOG_Pass4(void)
{
	bool doPTCatVerification = false;

	/*
	 * Start with the basic Pass 4 integrity checks. If requested (GUC & No In-Doubt
	 * prepared Xacts) then pursue non-database specific integrity checks
	 */
	if(!XLogStartup_DoNextPTCatVerificationIteration())
	{
		PersistentFileSysObj_StartupIntegrityCheck();

		/*
		 * Do the check for inconsistencies in global sequence number after the catalog cache is set up
		 * MPP-17207. Inconsistent global sequence number can be fixed with setting the guc
		 * gp_persistent_repair_global_sequence
		 */

		PersistentFileSysObj_DoGlobalSequenceScan();
		Insist(isFilespaceInfoConsistent());

		/*
		 * Now we can update the catalog to tell the system is fully-promoted,
		 * if was standby.  This should be done after all WAL-replay finished
		 * otherwise we'll be in inconsistent state where catalog says I'm in
		 * primary state while the recovery is trying to stream.
		 */
		if (ControlFile->state == DB_IN_STANDBY_NEW_TLI_SET)
		{
			GpRoleValue old_role = Gp_role;
	
			/* I am privileged */
			InitializeSessionUserIdStandalone();
			/* Start transaction locally */
			Gp_role = GP_ROLE_UTILITY;
			StartTransactionCommand();
			GetTransactionSnapshot();
			DirectFunctionCall1(gp_activate_standby, (Datum) 0);
			/* close the transaction we started above */
			CommitTransactionCommand();
			Gp_role = old_role;

			ereport(LOG, (errmsg("Updated catalog to support standby promotion")));

			ControlFile->state = DB_IN_PRODUCTION;
			ControlFile->time = time(NULL);
			UpdateControlFile();
			ereport(LOG, (errmsg("database system is ready")));
		}

		ereport(LOG,
			(errmsg("Finished BASIC startup integrity checking")));

		/*
		 * Check if the system has any in-doubt prepared transactions
		 * If No(Yes) - Do(nt) perform extra verification checks
		 */
		if (debug_persistent_ptcat_verification)
		{
			/*
			 * As the startup passes are Auxiliary processes and not pure Backeneds
			 * they don't have a user set. Hence, a concrete user id is needed.
			 * Pass 4 may perform some PersistentTable-Catalog verification, which
			 * uses SPI and hence will need a user id set
			 * Set the user id to bootstrap user id to obtain super user rights.
			 */
			if (GpIdentity.segindex != MASTER_CONTENT_ID)
				Gp_role = GP_ROLE_UTILITY;

			SetSessionUserId(BOOTSTRAP_SUPERUSERID, true);
			StartTransactionCommand();
			doPTCatVerification = !StartupXLOG_Pass4_CheckIfAnyInDoubtPreparedTransactions();

			/* Perform non-database specific verification checks */
			if (doPTCatVerification)
				StartupXLOG_Pass4_NonDBSpecificPTCatVerification();

			CommitTransactionCommand();
		}
	}

	/*
	 * If a database (and thus its tablespace) has already been selected, perform
	 * database specific verifications.
	 *
	 * And then get the first or the next database (and its tablespace) for the first or
	 * next cycle of Pass4 database specific extra verification checks
	 */
	if (doPTCatVerification || XLogStartup_DoNextPTCatVerificationIteration())
	{
		/* Redundant usage to maintain code readability */
		if (GpIdentity.segindex != MASTER_CONTENT_ID)
			Gp_role = GP_ROLE_UTILITY;

		SetSessionUserId(BOOTSTRAP_SUPERUSERID, true);
		StartTransactionCommand();

		if(XLogStartup_DoNextPTCatVerificationIteration())
			StartupXLOG_Pass4_DBSpecificPTCatVerification();

		if (!StartupXLOG_Pass4_GetDBForPTCatVerification())
		{
			if (!XLogCtl->pass4_PTCatVerificationPassed)
				elog(FATAL,"Startup Pass 4 PersistentTable-Catalog verification failed!!!");
		}
		else
		{
			if (Gp_role == GP_ROLE_DISPATCH && (GpIdentity.segindex == MASTER_CONTENT_ID))
			{
				bool exists = false;
				postDTMRecv_dbTblSpc_Hash_Entry currentDbTblSpc;

				if (!Persistent_PostDTMRecv_IsHashFull())
				{
					currentDbTblSpc.database = XLogCtl->currentDatabaseToVerify;
					currentDbTblSpc.tablespace = XLogCtl->tablespaceOfCurrentDatabaseToVerify;

					if (Persistent_PostDTMRecv_InsertHashEntry(currentDbTblSpc.database, &currentDbTblSpc, &exists))
					{
						if (exists)
							elog(FATAL,"Database already present in the Hash Table");
					}
				}
			}
		}

		CommitTransactionCommand();
	}
}

static bool StartupXLOG_Pass4_CheckIfAnyInDoubtPreparedTransactions(void)
{
	bool retVal = false;
	Persistent_Pre_ExecuteQuery();

	PG_TRY();
	{
		int ret = Persistent_ExecuteQuery("select * from pg_prepared_xacts", true);

		if (ret > 0)
			retVal = true;
		else if(ret == 0)
			retVal = false;
		else
			Insist(0);
	}
	PG_CATCH();
	{
		Persistent_ExecuteQuery_Cleanup();
		elog(FATAL, "In-Doubt transaction Check: Failure");
	}
	PG_END_TRY();

	Persistent_Post_ExecuteQuery();
	return retVal;
}

/*
 * Indicates if more verification cycles are needed. XLogCtl current database
 * and tablespace act as the flags and also carry database Oid and tablespace Oid
 * for the next cycle of verification.
 */
bool XLogStartup_DoNextPTCatVerificationIteration(void)
{
	if (XLogCtl->currentDatabaseToVerify != InvalidOid &&
			XLogCtl->tablespaceOfCurrentDatabaseToVerify != InvalidOid)
		return true;

	Insist(XLogCtl->currentDatabaseToVerify == InvalidOid &&
				XLogCtl->tablespaceOfCurrentDatabaseToVerify == InvalidOid);
		return false;
}

bool
StartupXLOG_Pass4_GetDBForPTCatVerification(void)
{
	char		*filename;
	FILE		*db_file;
	char		dbName[NAMEDATALEN];
	Oid			dbId= InvalidOid;
	Oid			tblSpaceId = InvalidOid;
	Oid			selectDbId = InvalidOid;
	Oid			selectTblSpaceId = InvalidOid;
	TransactionId dbFrozenxid;
	bool		gotDatabase = false;
	bool		chooseNextDatabase = false;

	filename = database_getflatfilename();
	db_file = AllocateFile(filename, "r");
	if (db_file == NULL)
		ereport(FATAL,
				(errcode_for_file_access(),
				 errmsg("could not open file \"%s\": %m", filename)));

	while (read_pg_database_line(db_file, dbName, &dbId,
								 &tblSpaceId, &dbFrozenxid))
	{
		Assert(dbId != InvalidOid && tblSpaceId != InvalidOid);
		if(XLogCtl->currentDatabaseToVerify == InvalidOid)
		{
			Assert(XLogCtl->tablespaceOfCurrentDatabaseToVerify == InvalidOid);
			selectDbId = dbId;
			selectTblSpaceId = tblSpaceId;
			gotDatabase = true;
			break;
		}
		else if (XLogCtl->currentDatabaseToVerify == dbId)
		{
			Assert(XLogCtl->tablespaceOfCurrentDatabaseToVerify == tblSpaceId);
			chooseNextDatabase = true;
		}
		else if (chooseNextDatabase)
		{
			selectDbId = dbId;
			selectTblSpaceId = tblSpaceId;
			gotDatabase = true;
			break;
		}
	}

	FreeFile(db_file);
	pfree(filename);

	XLogCtl->currentDatabaseToVerify = selectDbId;
	XLogCtl->tablespaceOfCurrentDatabaseToVerify = selectTblSpaceId;
	return gotDatabase;
}

/*
 * Perform Verification which is database specific
 * - Currently performed as part of Startup Pass 4
 */
void
StartupXLOG_Pass4_DBSpecificPTCatVerification()
{
	elog(LOG,"DB specific PersistentTable-Catalog Verification using DB %d", MyDatabaseId);
	if (!Persistent_DBSpecificPTCatVerification())
		XLogCtl->pass4_PTCatVerificationPassed = false;
}

/*
 * Perform Verification which is NOT based on particular database
 * - Currently performed as part of Startup Pass 4
 */
void
StartupXLOG_Pass4_NonDBSpecificPTCatVerification(void)
{
	elog(LOG,"Non-DB specific PersistentTable-Catalog Verification using DB %d", MyDatabaseId);
	if (!Persistent_NonDBSpecificPTCatVerification())
		XLogCtl->pass4_PTCatVerificationPassed = false;
}

/*
 * Determine the recovery redo start location from the pg_control file.
 *
 *    1) Only uses information from the pg_control file.
 *    2) This simplified routine does not examine the offline recovery file or
 *       the online backup labels, etc.
 *    3) This routine is a heavily reduced version of StartXLOG.
 *    4) IMPORTANT NOTE: This routine sets global variables that establish
 *       the timeline context necessary to do ReadRecord.  The ThisTimeLineID
 *       and expectedTLIs globals are set.
 *
 */
void
XLogGetRecoveryStart(char *callerStr, char *reasonStr, XLogRecPtr *redoCheckPointLoc, CheckPoint *redoCheckPoint)
{
	CheckPoint	checkPoint;
	XLogRecPtr	checkPointLoc;
	XLogRecord *record;
	bool previous;
	XLogRecPtr checkPointLSN;

	Assert(redoCheckPointLoc != NULL);
	Assert(redoCheckPoint != NULL);

	ereport((Debug_print_qd_mirroring ? LOG : DEBUG1),
			(errmsg("%s: determine restart location %s",
			 callerStr, reasonStr)));

	XLogCloseReadRecord();

	if (Debug_print_qd_mirroring)
	{
		XLogPrintLogNames();
	}

	/*
	 * Read control file and verify XLOG status looks valid.
	 *
	 */
	ReadControlFile();

	if (ControlFile->logSeg == 0 ||
		ControlFile->state < DB_SHUTDOWNED ||
		ControlFile->state > DB_IN_PRODUCTION ||
		!XRecOffIsValid(ControlFile->checkPoint.xrecoff))
		ereport(FATAL,
				(errmsg("%s: control file contains invalid data", callerStr)));

	/*
	 * Get the last valid checkpoint record.  If the latest one according
	 * to pg_control is broken, try the next-to-last one.
	 */
	checkPointLoc = ControlFile->checkPoint;
	ThisTimeLineID = ControlFile->checkPointCopy.ThisTimeLineID;

	/*
	 * Check for recovery control file, and if so set up state for offline
	 * recovery
	 */
	XLogReadRecoveryCommandFile(DEBUG5);

	/* Now we can determine the list of expected TLIs */
	expectedTLIs = XLogReadTimeLineHistory(ThisTimeLineID);

	record = ReadCheckpointRecord(checkPointLoc, 1);
	if (record != NULL)
	{
		previous = false;
		ereport((Debug_print_qd_mirroring ? LOG : DEBUG1),
				(errmsg("%s: checkpoint record is at %s (LSN %s)",
						callerStr,
						XLogLocationToString(&checkPointLoc),
						XLogLocationToString2(&EndRecPtr))));
	}
	else
	{
		previous = true;
		checkPointLoc = ControlFile->prevCheckPoint;
		record = ReadCheckpointRecord(checkPointLoc, 2);
		if (record != NULL)
		{
			ereport((Debug_print_qd_mirroring ? LOG : DEBUG1),
					(errmsg("%s: using previous checkpoint record at %s (LSN %s)",
						    callerStr,
							XLogLocationToString(&checkPointLoc),
						    XLogLocationToString2(&EndRecPtr))));
		}
		else
		{
			FileRep_SetSegmentState(SegmentStateFault, FaultTypeDB);

			ereport(ERROR,
				 (errmsg("%s: could not locate a valid checkpoint record", callerStr)));
		}
	}

	memcpy(&checkPoint, XLogRecGetData(record), sizeof(CheckPoint));
	checkPointLSN = EndRecPtr;

	if (XLByteEQ(checkPointLoc,checkPoint.redo))
	{
		{
			char	tmpBuf[FILEREP_MAX_LOG_DESCRIPTION_LEN];

			snprintf(tmpBuf, sizeof(tmpBuf),
					 "control file has restart '%s' and redo start checkpoint at location(lsn) '%s(%s)' ",
					 (previous ? "previous " : ""),
					 XLogLocationToString3(&checkPointLoc),
					 XLogLocationToString4(&checkPointLSN));

			FileRep_InsertConfigLogEntry(tmpBuf);
		}
	}
 	else if (XLByteLT(checkPointLoc, checkPoint.redo))
	{
		FileRep_SetSegmentState(SegmentStateFault, FaultTypeDB);

		ereport(ERROR,
				(errmsg("%s: invalid redo in checkpoint record", callerStr)));
	}
	else
	{
		XLogRecord *record;

		record = XLogReadRecord(&checkPoint.redo, false, LOG);
		if (record == NULL)
		{
			FileRep_SetSegmentState(SegmentStateFault, FaultTypeDB);

			ereport(ERROR,
			 (errmsg("%s: first redo record before checkpoint not found at %s",
					 callerStr, XLogLocationToString(&checkPoint.redo))));
		}

		{
			char	tmpBuf[FILEREP_MAX_LOG_DESCRIPTION_LEN];

			snprintf(tmpBuf, sizeof(tmpBuf),
					 "control file has restart '%s' checkpoint at location(lsn) '%s(%s)', redo starts at location(lsn) '%s(%s)' ",
					 (previous ? "previous " : ""),
					 XLogLocationToString3(&checkPointLoc),
					 XLogLocationToString4(&checkPointLSN),
					 XLogLocationToString(&checkPoint.redo),
					 XLogLocationToString2(&EndRecPtr));

			FileRep_InsertConfigLogEntry(tmpBuf);
		}
	}

	XLogCloseReadRecord();

	*redoCheckPointLoc = checkPointLoc;
	*redoCheckPoint = checkPoint;

}

/*
 * Is the system still in recovery?
 *
 * Unlike testing InRecovery, this works in any process that's connected to
 * shared memory.
 *
 * As a side-effect, we initialize the local TimeLineID and RedoRecPtr
 * variables the first time we see that recovery is finished.
 */
bool
RecoveryInProgress(void)
{
	/*
	 * We check shared state each time only until we leave recovery mode. We
	 * can't re-enter recovery, so there's no need to keep checking after the
	 * shared variable has once been seen false.
	 */
	if (!LocalRecoveryInProgress)
		return false;
	else
	{
		/* use volatile pointer to prevent code rearrangement */
		volatile XLogCtlData *xlogctl = XLogCtl;

		/* spinlock is essential on machines with weak memory ordering! */
		SpinLockAcquire(&xlogctl->info_lck);
		LocalRecoveryInProgress = xlogctl->SharedRecoveryInProgress;
		SpinLockRelease(&xlogctl->info_lck);

		/*
		 * Initialize TimeLineID and RedoRecPtr when we discover that recovery
		 * is finished. InitPostgres() relies upon this behaviour to ensure
		 * that InitXLOGAccess() is called at backend startup.	(If you change
		 * this, see also LocalSetXLogInsertAllowed.)
		 */
		if (!LocalRecoveryInProgress)
			InitXLOGAccess();

		return LocalRecoveryInProgress;
	}
}

/*
 * Subroutine to try to fetch and validate a prior checkpoint record.
 *
 * whichChkpt identifies the checkpoint (merely for reporting purposes).
 * 1 for "primary", 2 for "secondary", 0 for "other" (backup_label)
 */
XLogRecord *
ReadCheckpointRecord(XLogRecPtr RecPtr, int whichChkpt)
{
	XLogRecord *record;
	bool sizeOk;
	uint32 delta_xl_tot_len;		/* delta of total len of entire record */
	uint32 delta_xl_len;			/* delta of total len of rmgr data */

	if (!XRecOffIsValid(RecPtr.xrecoff))
	{
		switch (whichChkpt)
		{
			case 1:
				ereport(LOG,
				(errmsg("invalid primary checkpoint link in control file")));
				break;
			case 2:
				ereport(LOG,
						(errmsg("invalid secondary checkpoint link in control file")));
				break;
			default:
				ereport(LOG,
				   (errmsg("invalid checkpoint link in backup_label file")));
				break;
		}
		return NULL;
	}

	/*
	 * Set fetching_ckpt to true here, so that XLogReadRecord()
	 * uses RedoStartLSN as the start replication location used
	 * by WAL receiver (when StandbyMode is on). See comments
	 * for fetching_ckpt in XLogReadPage()
	 */
	record = XLogReadRecord(&RecPtr, true /* fetching_checkpoint */, LOG);

	if (record == NULL)
	{
		switch (whichChkpt)
		{
			case 1:
				ereport(LOG,
						(errmsg("invalid primary checkpoint record at location %s",
						        XLogLocationToString_Long(&RecPtr))));
				break;
			case 2:
				ereport(LOG,
						(errmsg("invalid secondary checkpoint record at location %s",
						        XLogLocationToString_Long(&RecPtr))));
				break;
			default:
				ereport(LOG,
						(errmsg("invalid checkpoint record at location %s",
						        XLogLocationToString_Long(&RecPtr))));
				break;
		}
		return NULL;
	}
	if (record->xl_rmid != RM_XLOG_ID)
	{
		switch (whichChkpt)
		{
			case 1:
				ereport(LOG,
						(errmsg("invalid resource manager ID in primary checkpoint record at location %s",
						        XLogLocationToString_Long(&RecPtr))));
				break;
			case 2:
				ereport(LOG,
						(errmsg("invalid resource manager ID in secondary checkpoint record at location %s",
						        XLogLocationToString_Long(&RecPtr))));
				break;
			default:
				ereport(LOG,
				(errmsg("invalid resource manager ID in checkpoint record at location %s",
				        XLogLocationToString_Long(&RecPtr))));
				break;
		}
		return NULL;
	}
	if (record->xl_info != XLOG_CHECKPOINT_SHUTDOWN &&
		record->xl_info != XLOG_CHECKPOINT_ONLINE)
	{
		switch (whichChkpt)
		{
			case 1:
				ereport(LOG,
				   (errmsg("invalid xl_info in primary checkpoint record at location %s",
				           XLogLocationToString_Long(&RecPtr))));
				break;
			case 2:
				ereport(LOG,
				 (errmsg("invalid xl_info in secondary checkpoint record at location %s",
				         XLogLocationToString_Long(&RecPtr))));
				break;
			default:
				ereport(LOG,
						(errmsg("invalid xl_info in checkpoint record at location %s",
						        XLogLocationToString_Long(&RecPtr))));
				break;
		}
		return NULL;
	}

	sizeOk = false;
	if (record->xl_len == sizeof(CheckPoint) &&
		record->xl_tot_len == SizeOfXLogRecord + sizeof(CheckPoint))
	{
		sizeOk = true;
	}
	else if (record->xl_len > sizeof(CheckPoint) &&
		record->xl_tot_len > SizeOfXLogRecord + sizeof(CheckPoint))
	{
		delta_xl_len = record->xl_len - sizeof(CheckPoint);
		delta_xl_tot_len = record->xl_tot_len - (SizeOfXLogRecord + sizeof(CheckPoint));

		if (delta_xl_len == delta_xl_tot_len)
		{
			sizeOk = true;
		}
	}

	if (!sizeOk)
	{
		switch (whichChkpt)
		{
			case 1:
				ereport(LOG,
					(errmsg("invalid length of primary checkpoint at location %s",
					        XLogLocationToString_Long(&RecPtr))));
				break;
			case 2:
				ereport(LOG,
				  (errmsg("invalid length of secondary checkpoint record at location %s",
				          XLogLocationToString_Long(&RecPtr))));
				break;
			default:
				ereport(LOG,
						(errmsg("invalid length of checkpoint record at location %s",
						        XLogLocationToString_Long(&RecPtr))));
				break;
		}
		return NULL;
	}
	return record;
}

static void
UnpackCheckPointRecord(
	XLogRecord			*record,

	XLogRecPtr 			*location,

	TMGXACT_CHECKPOINT	**dtxCheckpoint,

	uint32				*dtxCheckpointLen,

	char				**masterMirroringCheckpoint,

	uint32				*masterMirroringCheckpointLen,

	int					errlevelMasterMirroring,

        prepared_transaction_agg_state  **ptas)
{

	*dtxCheckpoint = NULL;
	*dtxCheckpointLen = 0;
	*masterMirroringCheckpoint = NULL;
	*masterMirroringCheckpointLen = 0;
	*ptas = NULL;


	/*********************************************************************************
	  A checkpoint can have four formats (one special, two 4.0, and one 4.1 and later).

	  Special (for bootstrap, xlog switch, maybe others).
	    Checkpoint

	  4.0 QD
	    CheckPoint
            TMGXACT_CHECKPOINT
            fspc_agg_state
            tspc_agg_state
            dbdir_agg_state


	  4.0 QE
            CheckPoint
            TMGXACT_CHECKPOINT

	 4.1 and later
            CheckPoint
            TMGXACT_CHECKPOINT
            fspc_agg_state
            tspc_agg_state
            dbdir_agg_state
            prepared_transaction_agg_state
	**********************************************************************************/

	if (record->xl_len > sizeof(CheckPoint))
	{
		uint32 remainderLen;

		remainderLen = record->xl_len - sizeof(CheckPoint);
		if (remainderLen < TMGXACT_CHECKPOINT_BYTES(0))
		{
			SUPPRESS_ERRCONTEXT_DECLARE;

			SUPPRESS_ERRCONTEXT_PUSH();

			ereport(PANIC,
				 (errmsg("Bad checkpoint record length %u (DTX information header: expected at least length %u, actual length %u) at location %s",
				 		 record->xl_len,
				 		 (uint32)TMGXACT_CHECKPOINT_BYTES(0),
				 		 remainderLen,
				 		 XLogLocationToString(location))));

			SUPPRESS_ERRCONTEXT_POP();
		}

		*dtxCheckpoint = (TMGXACT_CHECKPOINT *)(XLogRecGetData(record) + sizeof(CheckPoint));
		*dtxCheckpointLen = TMGXACT_CHECKPOINT_BYTES((*dtxCheckpoint)->committedCount);
		if (remainderLen < *dtxCheckpointLen)
		{
			SUPPRESS_ERRCONTEXT_DECLARE;

			SUPPRESS_ERRCONTEXT_PUSH();

			ereport(PANIC,
				 (errmsg("Bad checkpoint record length %u (DTX information: expected at least length %u, actual length %u) at location %s",
				 		 record->xl_len,
				 		 *dtxCheckpointLen,
				 		 remainderLen,
				 		 XLogLocationToString(location))));

			SUPPRESS_ERRCONTEXT_POP();
		}

		remainderLen -= *dtxCheckpointLen;

		int mmInfoLen = 0;

		if (remainderLen > 0)
		   {
			*masterMirroringCheckpoint = ((char*)*dtxCheckpoint) + *dtxCheckpointLen;

			/* TODO, The masterMirrongCheckpointLen actually contains the length of the master/mirroring section, */
			/* plus the new 4.1 and later prepared transaction section. This value is used else where, and needs  */
			/* to include the total length of the master/mirror section and anything that follows it.             */
			/* The code should be re-written to be more understandable.                                           */
			*masterMirroringCheckpointLen = remainderLen;

			if (!mmxlog_verify_checkpoint_info(
									*masterMirroringCheckpoint,
									*masterMirroringCheckpointLen,
									record->xl_len,
									location,
									errlevelMasterMirroring))
			   {
			        *masterMirroringCheckpoint = NULL;
			        *masterMirroringCheckpointLen = 0;
			   }
			else
			  {
			    /*
			      This appears to be either a old checkpoint with master/mirror information attached to it,
			      or it is a new (4.1) checkpoint that has the master/mirror information and the prepared
			      transaction information. In either case, get the location of the next byte past the
			      master/mirror section, and use it to determine the section's length.
			    */
			    char *nextPos = mmxlog_get_checkpoint_record_suffix(record);

			    mmInfoLen = nextPos - *masterMirroringCheckpoint;
			  }
		   }

		remainderLen -= mmInfoLen;

		/* This is a fix for MPP-12738 "Alibaba - upgrade from 4.0.4.0 to 4.1 failure"                                 */
		/* Under some circumstances, an old style checkpoint may exist (upgrade switch xlog...).                       */
		/* Check to see if it looks like a new checkpoint. A new checkpoint contains the prepared transaction section. */
		if (remainderLen > 0)
		  {
		    *ptas = (prepared_transaction_agg_state *)mmxlog_get_checkpoint_record_suffix(record);
		  }
		else
		  {
		    elog(WARNING,"UnpackCheckPointRecord: The checkpoint at %s appears to be a 4.0 checkpoint", XLogLocationToString(location));
		  }

	}  /* end if (record->xl_len > sizeof(CheckPoint)) */
}

/*
 * This must be called during startup of a backend process, except that
 * it need not be called in a standalone backend (which does StartupXLOG
 * instead).  We need to initialize the local copies of ThisTimeLineID and
 * RedoRecPtr.
 *
 * Note: before Postgres 8.0, we went to some effort to keep the postmaster
 * process's copies of ThisTimeLineID and RedoRecPtr valid too.  This was
 * unnecessary however, since the postmaster itself never touches XLOG anyway.
 */
void
InitXLOGAccess(void)
{
	/* ThisTimeLineID doesn't change so we need no lock to copy it */
	ThisTimeLineID = XLogCtl->ThisTimeLineID;
	/* Use GetRedoRecPtr to copy the RedoRecPtr safely */
	(void) GetRedoRecPtr();
}

/*
 * Once spawned, a backend may update its local RedoRecPtr from
 * XLogCtl->Insert.RedoRecPtr; it must hold the insert lock or info_lck
 * to do so.  This is done in XLogInsert() or GetRedoRecPtr().
 */
XLogRecPtr
GetRedoRecPtr(void)
{
	/* use volatile pointer to prevent code rearrangement */
	volatile XLogCtlData *xlogctl = XLogCtl;

	SpinLockAcquire(&xlogctl->info_lck);
	Assert(XLByteLE(RedoRecPtr, xlogctl->Insert.RedoRecPtr));
	RedoRecPtr = xlogctl->Insert.RedoRecPtr;
	SpinLockRelease(&xlogctl->info_lck);

	return RedoRecPtr;
}

/*
 * GetInsertRecPtr -- Returns the current insert position.
 *
 * NOTE: The value *actually* returned is the position of the last full
 * xlog page. It lags behind the real insert position by at most 1 page.
 * For that, we don't need to acquire WALInsertLock which can be quite
 * heavily contended, and an approximation is enough for the current
 * usage of this function.
 */
XLogRecPtr
GetInsertRecPtr(void)
{
	/* use volatile pointer to prevent code rearrangement */
	volatile XLogCtlData *xlogctl = XLogCtl;
	XLogRecPtr	recptr;

	SpinLockAcquire(&xlogctl->info_lck);
	recptr = xlogctl->LogwrtRqst.Write;
	SpinLockRelease(&xlogctl->info_lck);

	return recptr;
}

/*
 * GetFlushRecPtr -- Returns the current flush position, ie, the last WAL
 * position known to be fsync'd to disk.
 */
XLogRecPtr
GetFlushRecPtr(void)
{
	/* use volatile pointer to prevent code rearrangement */
	volatile XLogCtlData *xlogctl = XLogCtl;
	XLogRecPtr	recptr;

	SpinLockAcquire(&xlogctl->info_lck);
	recptr = xlogctl->LogwrtResult.Flush;
	SpinLockRelease(&xlogctl->info_lck);

	return recptr;
}

/*
 * Get the time of the last xlog segment switch
 */
time_t
GetLastSegSwitchTime(void)
{
	time_t		result;

	/* Need WALWriteLock, but shared lock is sufficient */
	LWLockAcquire(WALWriteLock, LW_SHARED);
	result = XLogCtl->Write.lastSegSwitchTime;
	LWLockRelease(WALWriteLock);

	return result;
}

/*
 * GetNextXidAndEpoch - get the current nextXid value and associated epoch
 *
 * This is exported for use by code that would like to have 64-bit XIDs.
 * We don't really support such things, but all XIDs within the system
 * can be presumed "close to" the result, and thus the epoch associated
 * with them can be determined.
 */
void
GetNextXidAndEpoch(TransactionId *xid, uint32 *epoch)
{
	uint32		ckptXidEpoch;
	TransactionId ckptXid;
	TransactionId nextXid;

	/* Must read checkpoint info first, else have race condition */
	{
		/* use volatile pointer to prevent code rearrangement */
		volatile XLogCtlData *xlogctl = XLogCtl;

		SpinLockAcquire(&xlogctl->info_lck);
		ckptXidEpoch = xlogctl->ckptXidEpoch;
		ckptXid = xlogctl->ckptXid;
		SpinLockRelease(&xlogctl->info_lck);
	}

	/* Now fetch current nextXid */
	nextXid = ReadNewTransactionId();

	/*
	 * nextXid is certainly logically later than ckptXid.  So if it's
	 * numerically less, it must have wrapped into the next epoch.
	 */
	if (nextXid < ckptXid)
		ckptXidEpoch++;

	*xid = nextXid;
	*epoch = ckptXidEpoch;
}

/*
 * This must be called ONCE during postmaster or standalone-backend shutdown
 */
void
ShutdownXLOG(int code __attribute__((unused)) , Datum arg __attribute__((unused)) )
{
	ereport(LOG,
			(errmsg("shutting down")));

	/*
	 * If recovery has not finished, we should not create a checkpoint.
	 * Restart point has been updated by xlog redo in case of recovery.
	 */
	if (!RecoveryInProgress())
		CreateCheckPoint(true, true);

	ShutdownCLOG();
	ShutdownSUBTRANS();
	ShutdownMultiXact();
	DistributedLog_Shutdown();

	ereport(LOG,
			(errmsg("database system is shut down"),
					errSendAlert(true)));
}

/*
 * Calculate the last segment that we need to retain because of
 * keep_wal_segments, by subtracting keep_wal_segments from the passed
 * xlog location
 */
static void
CheckKeepWalSegments(XLogRecPtr recptr, uint32 *_logId, uint32 *_logSeg)
{
	uint32	log;
	uint32	seg;
	uint32	keep_log;
	uint32	keep_seg;

	if (keep_wal_segments <= 0)
		return;

	XLByteToSeg(recptr, log, seg);

	keep_seg = keep_wal_segments % XLogSegsPerFile;
	keep_log = keep_wal_segments / XLogSegsPerFile;
	ereport(DEBUG1,
			(errmsg("%s: Input %d %d (Keep %d %d) (current %d %d)",
					PG_FUNCNAME_MACRO, *_logId, *_logSeg, keep_log,
					keep_seg, log, seg)));
	if (seg < keep_seg)
	{
		keep_log += 1;
		seg = seg - keep_seg + XLogSegsPerFile;
	}
	else
	{
		seg = seg - keep_seg;
	}

	/* Avoid underflow, don't go below (0,1) */
	if (log < keep_log || (log == keep_log && seg == 0))
	{
		log = 0;
		seg = 1;
	}
	else
	{
		log = log - keep_log;
	}

	/* check not to delete WAL segments newer than the calculated segment */
	if (log < *_logId || (log == *_logId && seg < *_logSeg))
	{
		*_logId = log;
		*_logSeg = seg;
	}

	ereport(DEBUG1,
			(errmsg("%s: Output %d %d",
					PG_FUNCNAME_MACRO, *_logId, *_logSeg)));
}

/*
 * Perform a checkpoint --- either during shutdown, or on-the-fly
 *
 * If force is true, we force a checkpoint regardless of whether any XLOG
 * activity has occurred since the last one.
 */
void
CreateCheckPoint(bool shutdown, bool force)
{
	MIRRORED_LOCK_DECLARE;

	CheckPoint	checkPoint;
	XLogRecPtr	recptr;
	XLogCtlInsert *Insert = &XLogCtl->Insert;
	XLogRecData rdata[6];
	char* 		dtxCheckPointInfo;
	int			dtxCheckPointInfoSize;
	uint32		freespace;
	uint32		_logId;
	uint32		_logSeg;
	int			nsegsadded = 0;
	int			nsegsremoved = 0;
	int			nsegsrecycled = 0;

	if (Debug_persistent_recovery_print)
	  {
	    elog(PersistentRecovery_DebugPrintLevel(),
                         "CreateCheckPoint: entering..."
		 );
	  }
	if (shutdown && ControlFile->state == DB_STARTUP)
	{
		return;
	}

#ifdef FAULT_INJECTOR
	/* During resync checkpoint has to complete otherwise segment cannot transition into Sync state */
	if (! FileRepResync_IsTransitionFromResyncToInSync())
	{
		if (FaultInjector_InjectFaultIfSet(
										   Checkpoint,
										   DDLNotSpecified,
										   "" /* databaseName */,
										   "" /* tableName */) == FaultInjectorTypeSkip)
			return;  // skip checkpoint
	}
#endif

	/*
	 * Acquire CheckpointLock to ensure only one checkpoint happens at a time.
	 * (This is just pro forma, since in the present system structure there is
	 * only one process that is allowed to issue checkpoints at any given
	 * time.)
	 */
	LWLockAcquire(CheckpointLock, LW_EXCLUSIVE);

	if (FileRepResync_IsTransitionFromResyncToInSync())
	{
		LWLockAcquire(MirroredLock, LW_EXCLUSIVE);

		/* database transitions to suspended state, IO activity on the segment is suspended */
		primaryMirrorSetIOSuspended(TRUE);

#ifdef FAULT_INJECTOR
		FaultInjector_InjectFaultIfSet(
									   FileRepTransitionToInSyncBeforeCheckpoint,
									   DDLNotSpecified,
									   "",	// databaseName
									   ""); // tableName
#endif
	}
	else
	{
		/*
		 * Normal case.
		 */
		MIRRORED_LOCK;
	}

	/*
	 * Use a critical section to force system panic if we have trouble.
	 */
	START_CRIT_SECTION();

	if (shutdown)
	{
		/*
		 * This is an ugly fix to dis-allow changing the pg_control
		 * state for standby promotion continuity.
		 *
		 * Refer to Startup_InProduction() for more details
		 */
		if (ControlFile->state != DB_IN_STANDBY_PROMOTED
			&& ControlFile->state != DB_IN_STANDBY_NEW_TLI_SET)
		{
			ControlFile->state = DB_SHUTDOWNING;
			ControlFile->time = time(NULL);
			UpdateControlFile();
		}
	}

	MemSet(&checkPoint, 0, sizeof(checkPoint));
	checkPoint.ThisTimeLineID = ThisTimeLineID;
	checkPoint.time = time(NULL);

	/*
	 * The WRITE_PERSISTENT_STATE_ORDERED_LOCK gets these locks:
	 *    MirroredLock SHARED, and
	 *    CheckpointStartLock SHARED,
	 *    PersistentObjLock EXCLUSIVE.
	 *
	 * The READ_PERSISTENT_STATE_ORDERED_LOCK gets this lock:
	 *    PersistentObjLock SHARED.
	 *
	 * They do this to prevent Persistent object changes during checkpoint and
	 * prevent persistent object reads while writing.  And acquire the MirroredLock
	 * at a level that blocks DDL during FileRep statechanges...
	 *
	 * We get the CheckpointStartLock to prevent Persistent object writers as
	 * we collect the Master Mirroring information from mmxlog_append_checkpoint_data
	 * until finally after the checkpoint record is inserted into the XLOG to prevent the
	 * persistent information from changing, and all buffers have been flushed to disk..
	 *
	 * We must hold CheckpointStartLock while determining the checkpoint REDO
	 * pointer.  This ensures that any concurrent transaction commits will be
	 * either not yet logged, or logged and recorded in pg_clog. See notes in
	 * RecordTransactionCommit().
	 */
	LWLockAcquire(CheckpointStartLock, LW_EXCLUSIVE);

	/* And we need WALInsertLock too */
	LWLockAcquire(WALInsertLock, LW_EXCLUSIVE);

	/*
	 * If this isn't a shutdown or forced checkpoint, and we have not inserted
	 * any XLOG records since the start of the last checkpoint, skip the
	 * checkpoint.	The idea here is to avoid inserting duplicate checkpoints
	 * when the system is idle. That wastes log space, and more importantly it
	 * exposes us to possible loss of both current and previous checkpoint
	 * records if the machine crashes just as we're writing the update.
	 * (Perhaps it'd make even more sense to checkpoint only when the previous
	 * checkpoint record is in a different xlog page?)
	 *
	 * We have to make two tests to determine that nothing has happened since
	 * the start of the last checkpoint: current insertion point must match
	 * the end of the last checkpoint record, and its redo pointer must point
	 * to itself.
	 */
	if (!shutdown && !force)
	{
		XLogRecPtr	curInsert;

		INSERT_RECPTR(curInsert, Insert, Insert->curridx);
#ifdef originalCheckpointChecking
		if (curInsert.xlogid == ControlFile->checkPoint.xlogid &&
			curInsert.xrecoff == ControlFile->checkPoint.xrecoff +
			MAXALIGN(SizeOfXLogRecord + sizeof(CheckPoint)) &&
			ControlFile->checkPoint.xlogid ==
			ControlFile->checkPointCopy.redo.xlogid &&
			ControlFile->checkPoint.xrecoff ==
			ControlFile->checkPointCopy.redo.xrecoff)
#else
		/*
		 * GP: Modified since the checkpoint record is not fixed length
		 * so we keep track of the last checkpoint locations (beginning and
		 * end) and use thoe values for comparison.
		 */
		if (XLogCtl->haveLastCheckpointLoc &&
			XLByteEQ(XLogCtl->lastCheckpointLoc,ControlFile->checkPoint) &&
			XLByteEQ(curInsert,XLogCtl->lastCheckpointEndLoc) &&
			XLByteEQ(ControlFile->checkPoint,ControlFile->checkPointCopy.redo))
#endif
		{
			LWLockRelease(WALInsertLock);
			LWLockRelease(CheckpointStartLock);
			if (FileRepResync_IsTransitionFromResyncToInSync())
			{
				LWLockRelease(MirroredLock);
			}
			else
			{
				/*
				 * Normal case.
				 */
				MIRRORED_UNLOCK;
			}
			LWLockRelease(CheckpointLock);

			END_CRIT_SECTION();
			return;
		}
	}

	/*
	 * Compute new REDO record ptr = location of next XLOG record.
	 *
	 * NB: this is NOT necessarily where the checkpoint record itself will be,
	 * since other backends may insert more XLOG records while we're off doing
	 * the buffer flush work.  Those XLOG records are logically after the
	 * checkpoint, even though physically before it.  Got that?
	 */
	freespace = INSERT_FREESPACE(Insert);
	if (freespace < SizeOfXLogRecord)
	{
		(void) AdvanceXLInsertBuffer(false);
		/* OK to ignore update return flag, since we will do flush anyway */
		freespace = INSERT_FREESPACE(Insert);
	}
	INSERT_RECPTR(checkPoint.redo, Insert, Insert->curridx);

	/*
	 * Here we update the shared RedoRecPtr for future XLogInsert calls; this
	 * must be done while holding the insert lock AND the info_lck.
	 *
	 * Note: if we fail to complete the checkpoint, RedoRecPtr will be left
	 * pointing past where it really needs to point.  This is okay; the only
	 * consequence is that XLogInsert might back up whole buffers that it
	 * didn't really need to.  We can't postpone advancing RedoRecPtr because
	 * XLogInserts that happen while we are dumping buffers must assume that
	 * their buffer changes are not included in the checkpoint.
	 */
	{
		/* use volatile pointer to prevent code rearrangement */
		volatile XLogCtlData *xlogctl = XLogCtl;

		SpinLockAcquire(&xlogctl->info_lck);
		RedoRecPtr = xlogctl->Insert.RedoRecPtr = checkPoint.redo;
		SpinLockRelease(&xlogctl->info_lck);
	}

	/*
	 * Now we can release insert lock and checkpoint start lock, allowing
	 * other xacts to proceed even while we are flushing disk buffers.
	 */
	LWLockRelease(WALInsertLock);

	// We used to release the CheckpointStartLock here.  Now we do it after the checkpoint
	// record is written...
//	LWLockRelease(CheckpointStartLock);

	if (!shutdown)
		ereport(DEBUG2,
				(errmsg("checkpoint starting")));

	/*
	 * Get the other info we need for the checkpoint record.
	 */
	LWLockAcquire(XidGenLock, LW_SHARED);
	checkPoint.nextXid = ShmemVariableCache->nextXid;
	LWLockRelease(XidGenLock);

	/* Increase XID epoch if we've wrapped around since last checkpoint */
	checkPoint.nextXidEpoch = ControlFile->checkPointCopy.nextXidEpoch;
	if (checkPoint.nextXid < ControlFile->checkPointCopy.nextXid)
		checkPoint.nextXidEpoch++;

	LWLockAcquire(OidGenLock, LW_SHARED);
	checkPoint.nextOid = ShmemVariableCache->nextOid;
	if (!shutdown)
		checkPoint.nextOid += ShmemVariableCache->oidCount;
	LWLockRelease(OidGenLock);

	MultiXactGetCheckptMulti(shutdown,
							 &checkPoint.nextMulti,
							 &checkPoint.nextMultiOffset);

	/*
	 * Having constructed the checkpoint record, ensure all shmem disk buffers
	 * and commit-log buffers are flushed to disk.
	 *
	 * This I/O could fail for various reasons.  If so, we will fail to
	 * complete the checkpoint, but there is no reason to force a system
	 * panic. Accordingly, exit critical section while doing it.
	 */
	END_CRIT_SECTION();

	CheckPointGuts(checkPoint.redo);

	START_CRIT_SECTION();

	/*
	 * Now insert the checkpoint record into XLOG.
	 */

	getDtxCheckPointInfoAndLock(&dtxCheckPointInfo, &dtxCheckPointInfoSize);

	rdata[0].data = (char *) (&checkPoint);
	rdata[0].len = sizeof(checkPoint);
	rdata[0].buffer = InvalidBuffer;
	rdata[0].next = &(rdata[1]);

	rdata[1].data = (char *) dtxCheckPointInfo;
	rdata[1].len = dtxCheckPointInfoSize;
	rdata[1].buffer = InvalidBuffer;
	rdata[1].next = NULL;

	/*
	 * Have the master mirror sync code add filespace and tablespace
	 * meta data to keep the standby consistent. Safe to call on segments
	 * as this is a NOOP if we're not the master.
	 */
	mmxlog_append_checkpoint_data(rdata);

	prepared_transaction_agg_state *p = NULL;

	getTwoPhasePreparedTransactionData(&p, "CreateCheckPoint");
	elog(PersistentRecovery_DebugPrintLevel(), "CreateCheckPoint: prepared transactions = %d", p->count);
	rdata[5].data = (char*)p;
	rdata[5].buffer = InvalidBuffer;
	rdata[5].len = PREPARED_TRANSACTION_CHECKPOINT_BYTES(p->count);
	rdata[4].next = &(rdata[5]);
	rdata[5].next = NULL;

	if (Debug_persistent_recovery_print)
	{
		elog(PersistentRecovery_DebugPrintLevel(),
	        "CreateCheckPoint: Regular checkpoint length = %u"
	        ", DTX checkpoint length %u (rdata[1].next is NULL %s)"
	        ", Master mirroring filespace length = %u (rdata[2].next is NULL %s)"
	        ", Master mirroring tablespace length = %u (rdata[3].next is NULL %s)"
	       ", Master mirroring database directory length = %u",
	       rdata[0].len,
	       rdata[1].len,
	       (rdata[1].next == NULL ? "true" : "false"),
	       rdata[2].len,
	       (rdata[2].next == NULL ? "true" : "false"),
	       rdata[3].len,
	       (rdata[3].next == NULL ? "true" : "false"),
	       rdata[4].len);
		elog(PersistentRecovery_DebugPrintLevel(), "CreateCheckPoint; Prepared Transaction length = %u", rdata[5].len);
	}


	/*
	 * Need to save the oldest prepared transaction XLogRecPtr for use later.
	 * It is not sufficient to just save the pointer because we may remove the
	 * space after it is written in XLogInsert.
	 */
	XLogRecPtr *ptrd_oldest_ptr = NULL;
	XLogRecPtr ptrd_oldest;

	memset(&ptrd_oldest, 0, sizeof(ptrd_oldest));

	ptrd_oldest_ptr = getTwoPhaseOldestPreparedTransactionXLogRecPtr(&rdata[5]);

	if (Debug_persistent_recovery_print)
	{
		if (ptrd_oldest_ptr == NULL)
			elog(PersistentRecovery_DebugPrintLevel(), "Oldest Prepared Record = NULL");
		else
			elog(PersistentRecovery_DebugPrintLevel(), "CreateCheckPoint: Oldest Prepared Record = %s",
					XLogLocationToString(ptrd_oldest_ptr));
	}


	if (ptrd_oldest_ptr != NULL)
		memcpy(&ptrd_oldest, ptrd_oldest_ptr, sizeof(ptrd_oldest));

	recptr = XLogInsert(RM_XLOG_ID,
			            shutdown ? XLOG_CHECKPOINT_SHUTDOWN : XLOG_CHECKPOINT_ONLINE,
			            rdata);

	if (Debug_persistent_recovery_print)
	{
		elog(PersistentRecovery_DebugPrintLevel(),
			 "CreateCheckPoint: Checkpoint location = %s, total length %u, data length %d",
			 XLogLocationToString(&recptr),
			 XLogLastInsertTotalLen(),
			 XLogLastInsertDataLen());
	}

	// See the comments above where this lock is acquired.
	LWLockRelease(CheckpointStartLock);

	freeDtxCheckPointInfoAndUnlock(dtxCheckPointInfo, dtxCheckPointInfoSize, &recptr);

	XLogFlush(recptr);

	/*
	 * We now have ProcLastRecPtr = start of actual checkpoint record, recptr
	 * = end of actual checkpoint record.
	 */
	if (shutdown && !XLByteEQ(checkPoint.redo, ProcLastRecPtr))
		ereport(PANIC,
				(errmsg("concurrent transaction log activity while database system is shutting down")));

	/*
	 * Select point at which we can truncate the log, which we base on the
	 * prior checkpoint's earliest info or the oldest prepared transaction xlog record's info.
	 */
	if (ptrd_oldest_ptr != NULL && XLByteLE(ptrd_oldest, ControlFile->checkPointCopy.redo))
		XLByteToSeg(ptrd_oldest, _logId, _logSeg);
	else
		XLByteToSeg(ControlFile->checkPointCopy.redo, _logId, _logSeg);

	elog((Debug_print_qd_mirroring ? LOG : DEBUG1),
		 "CreateCheckPoint: previous checkpoint's earliest info (copy redo location %s, previous checkpoint location %s)",
		 XLogLocationToString(&ControlFile->checkPointCopy.redo),
		 XLogLocationToString2(&ControlFile->prevCheckPoint));

	/*
	 * Update the control file.
	 */
	LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);
	if (shutdown)
	{
		/*
		 * Ugly fix to dis-allow changing pg_control state
		 * for standby promotion continuity
		 */
		if (ControlFile->state != DB_IN_STANDBY_PROMOTED
			&& ControlFile->state != DB_IN_STANDBY_NEW_TLI_SET)
			ControlFile->state = DB_SHUTDOWNED;
	}

	ControlFile->prevCheckPoint = ControlFile->checkPoint;
	ControlFile->checkPoint = ProcLastRecPtr;
	ControlFile->checkPointCopy = checkPoint;
	/* crash recovery should always recover to the end of WAL */
	MemSet(&ControlFile->minRecoveryPoint, 0, sizeof(XLogRecPtr));
	ControlFile->time = time(NULL);

	/*
	 * Save the last checkpoint position.
	 */
	XLogCtl->haveLastCheckpointLoc = true;
	XLogCtl->lastCheckpointLoc = ProcLastRecPtr;
	XLogCtl->lastCheckpointEndLoc = ProcLastRecEnd;

	UpdateControlFile();
	LWLockRelease(ControlFileLock);

	/* Update shared-memory copy of checkpoint XID/epoch */
	{
		/* use volatile pointer to prevent code rearrangement */
		volatile XLogCtlData *xlogctl = XLogCtl;

		SpinLockAcquire(&xlogctl->info_lck);
		xlogctl->ckptXidEpoch = checkPoint.nextXidEpoch;
		xlogctl->ckptXid = checkPoint.nextXid;
		SpinLockRelease(&xlogctl->info_lck);
	}

	/*
	 * We are now done with critical updates; no need for system panic if we
	 * have trouble while fooling with offline log segments.
	 */
	END_CRIT_SECTION();

	/*
	 * Delete offline log files (those no longer needed even for previous
	 * checkpoint).
	 */
	if (gp_keep_all_xlog == false && (_logId || _logSeg))
	{
		/* Only for MASTER check this GUC and act */
		if (GpIdentity.segindex == MASTER_CONTENT_ID)
		{
			/*
			 * See if we have a live WAL sender and see if it has a
			 * start xlog location (with active basebackup) or standby fsync location
			 * (with active standby). We have to compare it with prev. checkpoint
			 * location. We use the min out of them to figure out till
			 * what point we need to save the xlog seg files
			 * Currently, applicable to Master only
			 */
			XLogRecPtr xlogCleanUpTo = WalSndCtlGetXLogCleanUpTo();
			if (!XLogRecPtrIsInvalid(xlogCleanUpTo))
			{
				if (XLByteLT(recptr, xlogCleanUpTo))
					xlogCleanUpTo = recptr;
			}
			else
				xlogCleanUpTo = recptr;

			CheckKeepWalSegments(xlogCleanUpTo, &_logId, &_logSeg);
		}

		PrevLogSeg(_logId, _logSeg);
		MoveOfflineLogs(_logId, _logSeg, recptr,
						&nsegsremoved, &nsegsrecycled);
	}

	/*
	 * Make more log segments if needed.  (Do this after deleting offline log
	 * segments, to avoid having peak disk space usage higher than necessary.)
	 */
	if (!shutdown)
		nsegsadded = PreallocXlogFiles(recptr);

	/*
	 * Truncate pg_subtrans if possible.  We can throw away all data before
	 * the oldest XMIN of any running transaction.	No future transaction will
	 * attempt to reference any pg_subtrans entry older than that (see Asserts
	 * in subtrans.c).	During recovery, though, we mustn't do this because
	 * StartupSUBTRANS hasn't been called yet.
	 */
	if (!InRecovery)
		TruncateSUBTRANS(GetOldestXmin(true));

	if (!shutdown)
		ereport(DEBUG2,
				(errmsg("checkpoint complete; %d transaction log file(s) added, %d removed, %d recycled",
						nsegsadded, nsegsremoved, nsegsrecycled)));

	if (Debug_persistent_recovery_print)
		elog(PersistentRecovery_DebugPrintLevel(),
			 "CreateCheckPoint: shutdown %s, force %s, checkpoint location %s, redo location %s",
			 (shutdown ? "true" : "false"),
			 (force ? "true" : "false"),
			 XLogLocationToString(&ControlFile->checkPoint),
			 XLogLocationToString2(&checkPoint.redo));

	if (FileRepResync_IsTransitionFromResyncToInSync())
	{
		RequestXLogSwitch();

		FileRepResyncManager_ResyncFlatFiles();

		UpdateControlFile();

		LWLockRelease(MirroredLock);

		/* database is resumed */
		primaryMirrorSetIOSuspended(FALSE);
	}
	else
	{
		/*
		 * Normal case.
		 */
		MIRRORED_UNLOCK;
	}

	LWLockRelease(CheckpointLock);
}

/*
 * Flush all data in shared memory to disk, and fsync
 *
 * This is the common code shared between regular checkpoints and
 * recovery restartpoints.
 */
static void
CheckPointGuts(XLogRecPtr checkPointRedo)
{
	CheckPointCLOG();
	CheckPointSUBTRANS();
	CheckPointMultiXact();
	CheckPointChangeTracking();
	DistributedLog_CheckPoint();
	FlushBufferPool();			/* performs all required fsyncs */
	/* We deliberately delay 2PC checkpointing as long as possible */
	CheckPointTwoPhase(checkPointRedo);
}

static void Checkpoint_RecoveryPass(XLogRecPtr checkPointRedo)
{
	CheckPointGuts(checkPointRedo);
}

/*
 * Set a recovery restart point if appropriate
 *
 * This is similar to CreateCheckpoint, but is used during WAL recovery
 * to establish a point from which recovery can roll forward without
 * replaying the entire recovery log.  This function is called each time
 * a checkpoint record is read from XLOG; it must determine whether a
 * restartpoint is needed or not.
 */
static void
RecoveryRestartPoint(const CheckPoint *checkPoint)
{
//	int			elapsed_secs;
	int			rmid;
	uint _logId = 0;
	uint _logSeg = 0;

	/* use volatile pointer to prevent code rearrangement */
	volatile XLogCtlData *xlogctl = XLogCtl;

	/*
	 * Do nothing if the elapsed time since the last restartpoint is less than
	 * half of checkpoint_timeout.	(We use a value less than
	 * checkpoint_timeout so that variations in the timing of checkpoints on
	 * the master, or speed of transmission of WAL segments to a slave, won't
	 * make the slave skip a restartpoint once it's synced with the master.)
	 * Checking true elapsed time keeps us from doing restartpoints too often
	 * while rapidly scanning large amounts of WAL.
	 */

	// UNDONE: For now, turn this off!
//	elapsed_secs = time(NULL) - ControlFile->time;
//	if (elapsed_secs < CheckPointTimeout / 2)
//		return;

	/*
	 * Is it safe to checkpoint?  We must ask each of the resource managers
	 * whether they have any partial state information that might prevent a
	 * correct restart from this point.  If so, we skip this opportunity, but
	 * return at the next checkpoint record for another try.
	 */
	for (rmid = 0; rmid <= RM_MAX_ID; rmid++)
	{
		if (RmgrTable[rmid].rm_safe_restartpoint != NULL)
			if (!(RmgrTable[rmid].rm_safe_restartpoint()))
				return;
	}

	/* Update the shared RedoRecPtr */
	 SpinLockAcquire(&xlogctl->info_lck);
	 xlogctl->Insert.RedoRecPtr = checkPoint->redo;
	 SpinLockRelease(&xlogctl->info_lck);

	/*
	 * OK, force data out to disk
	 */
	CheckPointGuts(checkPoint->redo);

	if (IsStandbyMode())
	{
		/*
		 * Select point at which we can truncate the log, which we base on the
		 * prior checkpoint's earliest info.
		*/
		XLByteToSeg(ControlFile->checkPointCopy.redo, _logId, _logSeg);
	}

	/*
	 * Update pg_control so that any subsequent crash will restart from this
	 * checkpoint.	Note: ReadRecPtr gives the XLOG address of the checkpoint
	 * record itself.
	 */
	ControlFile->prevCheckPoint = ControlFile->checkPoint;
	ControlFile->checkPoint = ReadRecPtr;
	ControlFile->checkPointCopy = *checkPoint;
	ControlFile->time = time(NULL);

	/*
	 * Save the last checkpoint position.
	 */
	XLogCtl->haveLastCheckpointLoc = true;
	XLogCtl->lastCheckpointLoc = ReadRecPtr;
	XLogCtl->lastCheckpointEndLoc = EndRecPtr;

	UpdateControlFile();

	ereport(LOG,
			(errmsg("recovery restart point at %X/%X",
					checkPoint->redo.xlogid, checkPoint->redo.xrecoff)));
	elog((Debug_print_qd_mirroring ? LOG : DEBUG1), "RecoveryRestartPoint: checkpoint copy redo location %s, previous checkpoint location %s",
		 XLogLocationToString(&ControlFile->checkPointCopy.redo),
		 XLogLocationToString2(&ControlFile->prevCheckPoint));

	if (IsStandbyMode())
	{
		/*
		 * Delete offline log files (those no longer needed even for previous
		 * checkpoint).
		 */
		if (gp_keep_all_xlog == false && (_logId || _logSeg))
		{
			XLogRecPtr endptr;
			int        nsegsremoved;
			int        nsegsrecycled;

			/* Get the current (or recent) end of xlog */
			endptr = GetStandbyFlushRecPtr(NULL);

			PrevLogSeg(_logId, _logSeg);
			MoveOfflineLogs(_logId, _logSeg, endptr, &nsegsremoved, &nsegsrecycled);
		}
	}
}

/*
 * Write a NEXTOID log record
 */
void
XLogPutNextOid(Oid nextOid)
{
	XLogRecData rdata;

	rdata.data = (char *) (&nextOid);
	rdata.len = sizeof(Oid);
	rdata.buffer = InvalidBuffer;
	rdata.next = NULL;
	(void) XLogInsert(RM_XLOG_ID, XLOG_NEXTOID, &rdata);

	/*
	 * We need not flush the NEXTOID record immediately, because any of the
	 * just-allocated OIDs could only reach disk as part of a tuple insert or
	 * update that would have its own XLOG record that must follow the NEXTOID
	 * record.	Therefore, the standard buffer LSN interlock applied to those
	 * records will ensure no such OID reaches disk before the NEXTOID record
	 * does.
	 *
	 * Note, however, that the above statement only covers state "within" the
	 * database.  When we use a generated OID as a file or directory name,
	 * we are in a sense violating the basic WAL rule, because that filesystem
	 * change may reach disk before the NEXTOID WAL record does.  The impact
	 * of this is that if a database crash occurs immediately afterward,
	 * we might after restart re-generate the same OID and find that it
	 * conflicts with the leftover file or directory.  But since for safety's
	 * sake we always loop until finding a nonconflicting filename, this poses
	 * no real problem in practice. See pgsql-hackers discussion 27-Sep-2006.
	 */
}

/*
 * Write an XLOG SWITCH record.
 *
 * Here we just blindly issue an XLogInsert request for the record.
 * All the magic happens inside XLogInsert.
 *
 * The return value is either the end+1 address of the switch record,
 * or the end+1 address of the prior segment if we did not need to
 * write a switch record because we are already at segment start.
 */
XLogRecPtr
RequestXLogSwitch(void)
{
	XLogRecPtr	RecPtr;
	XLogRecData rdata;

	/* XLOG SWITCH, alone among xlog record types, has no data */
	rdata.buffer = InvalidBuffer;
	rdata.data = NULL;
	rdata.len = 0;
	rdata.next = NULL;

	RecPtr = XLogInsert(RM_XLOG_ID, XLOG_SWITCH, &rdata);

	return RecPtr;
}

static void
xlog_redo_print_extended_checkpoint_info(XLogRecPtr beginLoc, XLogRecord *record)
{
	TMGXACT_CHECKPOINT	*dtxCheckpoint;
	uint32				dtxCheckpointLen;
	char				*masterMirroringCheckpoint;
	uint32				masterMirroringCheckpointLen;
	prepared_transaction_agg_state  *ptas;

	/*
	 * The UnpackCheckPointRecord routine will print under the
	 * Debug_persistent_recovery_print GUC.
	 */
	UnpackCheckPointRecord(
						record,
						&beginLoc,
						&dtxCheckpoint,
						&dtxCheckpointLen,
						&masterMirroringCheckpoint,
						&masterMirroringCheckpointLen,
						/* errlevel */ -1,		// Suppress elog altogether on master mirroring checkpoint length checking.
                                                &ptas);
	if (dtxCheckpointLen > 0)
	{
  	        elog(PersistentRecovery_DebugPrintLevel(),
	             "xlog_redo_print_extended_checkpoint_info: Checkpoint record data length = %u, DTX committed count %d, DTX data length %u, Master Mirroring information length %u, location %s",
	             record->xl_len,
	             dtxCheckpoint->committedCount,
	             dtxCheckpointLen,
	             masterMirroringCheckpointLen,
	             XLogLocationToString(&beginLoc));
	        if (ptas != NULL)
  	            elog(PersistentRecovery_DebugPrintLevel(),
		         "xlog_redo_print_extended_checkpoint_info: prepared transaction agg state count = %d",
                          ptas->count);

		if (masterMirroringCheckpointLen > 0)
		{
			int filespaceCount;
			int tablespaceCount;
			int databaseCount;

			if (!mmxlog_get_checkpoint_counts(
									masterMirroringCheckpoint,
									masterMirroringCheckpointLen,
									record->xl_len,
									&beginLoc,
									/* errlevel */ -1,		// Suppress elog altogether on master mirroring checkpoint length checking.
									&filespaceCount,
									&tablespaceCount,
									&databaseCount))
			{
				elog(PersistentRecovery_DebugPrintLevel(),
					 "xlog_redo_print_extended_checkpoint_info: master mirroring information: %d filespaces, %d tablespaces, %d databases, location %s",
					 filespaceCount,
					 tablespaceCount,
					 databaseCount,
					 XLogLocationToString(&beginLoc));
			}
		}
	}
}

/*
 * XLOG resource manager's routines
 *
 * Definitions of info values are in include/catalog/pg_control.h, though
 * not all records types are related to control file processing.
 */
void
xlog_redo(XLogRecPtr beginLoc __attribute__((unused)), XLogRecPtr lsn __attribute__((unused)), XLogRecord *record)
{
	uint8		info = record->xl_info & ~XLR_INFO_MASK;

	if (info == XLOG_NEXTOID)
	{
		Oid			nextOid;

		memcpy(&nextOid, XLogRecGetData(record), sizeof(Oid));
		if (ShmemVariableCache->nextOid < nextOid)
		{
			ShmemVariableCache->nextOid = nextOid;
			ShmemVariableCache->oidCount = 0;
		}
	}
	else if (info == XLOG_CHECKPOINT_SHUTDOWN)
	{
		CheckPoint	checkPoint;

		memcpy(&checkPoint, XLogRecGetData(record), sizeof(CheckPoint));
		/* In a SHUTDOWN checkpoint, believe the counters exactly */
		ShmemVariableCache->nextXid = checkPoint.nextXid;
		ShmemVariableCache->nextOid = checkPoint.nextOid;
		ShmemVariableCache->oidCount = 0;
		MultiXactSetNextMXact(checkPoint.nextMulti,
							  checkPoint.nextMultiOffset);

		/*
		 * If we see a shutdown checkpoint while waiting for an end-of-backup
		 * record, the backup was canceled and the end-of-backup record will
		 * never arrive.
		 */
		if (StandbyMode &&
			!XLogRecPtrIsInvalid(ControlFile->backupStartPoint))
			ereport(PANIC,
			(errmsg("online backup was canceled, recovery cannot continue")));

		/* ControlFile->checkPointCopy always tracks the latest ckpt XID */
		ControlFile->checkPointCopy.nextXidEpoch = checkPoint.nextXidEpoch;
		ControlFile->checkPointCopy.nextXid = checkPoint.nextXid;

		/* Update shared-memory copy of checkpoint XID/epoch */
		 {
			 /* use volatile pointer to prevent code rearrangement */
			 volatile XLogCtlData *xlogctl = XLogCtl;

			 SpinLockAcquire(&xlogctl->info_lck);
			 xlogctl->ckptXidEpoch = checkPoint.nextXidEpoch;
			 xlogctl->ckptXid = checkPoint.nextXid;
			 SpinLockRelease(&xlogctl->info_lck);
		 }

		/*
		 * TLI may change in a shutdown checkpoint, but it shouldn't decrease
		 */
		if (checkPoint.ThisTimeLineID != ThisTimeLineID)
		{
			if (checkPoint.ThisTimeLineID < ThisTimeLineID ||
				!list_member_int(expectedTLIs,
								 (int) checkPoint.ThisTimeLineID))
				ereport(PANIC,
						(errmsg("unexpected timeline ID %u (after %u) in checkpoint record",
								checkPoint.ThisTimeLineID, ThisTimeLineID)));
			/* Following WAL records should be run with new TLI */
			ThisTimeLineID = checkPoint.ThisTimeLineID;
		}

		RecoveryRestartPoint(&checkPoint);

		// Could run into old format checkpoint redo records...
		if (Debug_persistent_recovery_print)
		{
			xlog_redo_print_extended_checkpoint_info(beginLoc, record);
		}
	}
	else if (info == XLOG_CHECKPOINT_ONLINE)
	{
		CheckPoint	checkPoint;

		memcpy(&checkPoint, XLogRecGetData(record), sizeof(CheckPoint));
		/* In an ONLINE checkpoint, treat the counters like NEXTOID */
		if (TransactionIdPrecedes(ShmemVariableCache->nextXid,
								  checkPoint.nextXid))
			ShmemVariableCache->nextXid = checkPoint.nextXid;
		if (ShmemVariableCache->nextOid < checkPoint.nextOid)
		{
			ShmemVariableCache->nextOid = checkPoint.nextOid;
			ShmemVariableCache->oidCount = 0;
		}
		MultiXactAdvanceNextMXact(checkPoint.nextMulti,
								  checkPoint.nextMultiOffset);

		/* ControlFile->checkPointCopy always tracks the latest ckpt XID */
		ControlFile->checkPointCopy.nextXidEpoch = checkPoint.nextXidEpoch;
		ControlFile->checkPointCopy.nextXid = checkPoint.nextXid;

		/* Update shared-memory copy of checkpoint XID/epoch */
		 {
			 /* use volatile pointer to prevent code rearrangement */
			 volatile XLogCtlData *xlogctl = XLogCtl;

			 SpinLockAcquire(&xlogctl->info_lck);
			 xlogctl->ckptXidEpoch = checkPoint.nextXidEpoch;
			 xlogctl->ckptXid = checkPoint.nextXid;
			 SpinLockRelease(&xlogctl->info_lck);
		 }

		/* TLI should not change in an on-line checkpoint */
		if (checkPoint.ThisTimeLineID != ThisTimeLineID)
			ereport(PANIC,
					(errmsg("unexpected timeline ID %u (should be %u) in checkpoint record",
							checkPoint.ThisTimeLineID, ThisTimeLineID)));

		RecoveryRestartPoint(&checkPoint);

		// Could run into old format checkpoint redo records...
		if (Debug_persistent_recovery_print)
		{
			xlog_redo_print_extended_checkpoint_info(beginLoc, record);
		}
	}
	else if (info == XLOG_SWITCH)
	{
		/* nothing to do here */
	}
	else if (info == XLOG_BACKUP_END)
	{
		XLogRecPtr	startpoint;

		memcpy(&startpoint, XLogRecGetData(record), sizeof(startpoint));

		if (XLByteEQ(ControlFile->backupStartPoint, startpoint))
		{
			/*
			 * We have reached the end of base backup, the point where
			 * pg_stop_backup() was done.
			 * Reset backupStartPoint, and update minRecoveryPoint to make
			 * sure we don't allow starting up at an earlier point even if
			 * recovery is stopped and restarted soon after this.
			 */
			elog(DEBUG1, "end of backup reached");

			LWLockAcquire(ControlFileLock, LW_EXCLUSIVE);

			if (XLByteLT(ControlFile->minRecoveryPoint, lsn))
				ControlFile->minRecoveryPoint = lsn;
			MemSet(&ControlFile->backupStartPoint, 0, sizeof(XLogRecPtr));
			ControlFile->backupEndRequired = false;
			UpdateControlFile();

			LWLockRelease(ControlFileLock);
		}
	}
}

void
xlog_desc(StringInfo buf, XLogRecPtr beginLoc, XLogRecord *record)
{
	uint8		info = record->xl_info & ~XLR_INFO_MASK;
	char		*rec = XLogRecGetData(record);

	if (info == XLOG_CHECKPOINT_SHUTDOWN ||
		info == XLOG_CHECKPOINT_ONLINE)
	{
		CheckPoint *checkpoint = (CheckPoint *) rec;

		TMGXACT_CHECKPOINT	*dtxCheckpoint;
		uint32				dtxCheckpointLen;
		char				*masterMirroringCheckpoint;
		uint32				masterMirroringCheckpointLen;
		prepared_transaction_agg_state  *ptas;

		appendStringInfo(buf, "checkpoint: redo %X/%X; undo %X/%X; "
						 "tli %u; xid %u/%u; oid %u; multi %u; offset %u; %s",
						 checkpoint->redo.xlogid, checkpoint->redo.xrecoff,
						 checkpoint->undo.xlogid, checkpoint->undo.xrecoff,
						 checkpoint->ThisTimeLineID,
						 checkpoint->nextXidEpoch, checkpoint->nextXid,
						 checkpoint->nextOid,
						 checkpoint->nextMulti,
						 checkpoint->nextMultiOffset,
				 (info == XLOG_CHECKPOINT_SHUTDOWN) ? "shutdown" : "online");

		/*
		 * The UnpackCheckPointRecord routine will print under the
		 * Debug_persistent_recovery_print GUC.
		 */
		UnpackCheckPointRecord(
							record,
							&beginLoc,
							&dtxCheckpoint,
							&dtxCheckpointLen,
							&masterMirroringCheckpoint,
							&masterMirroringCheckpointLen,
							/* errlevel */ -1, 	// Suppress elog altogether on master mirroring checkpoint length checking.
                                                        &ptas);
		if (dtxCheckpointLen > 0)
		{
			appendStringInfo(buf,
				 ", checkpoint record data length = %u, DTX committed count %d, DTX data length %u, Master Mirroring information length %u",
				 record->xl_len,
				 dtxCheckpoint->committedCount,
				 dtxCheckpointLen,
				 masterMirroringCheckpointLen);
			if (ptas != NULL)
                           appendStringInfo(buf,
                                     ", prepared transaction agg state count = %d",
				      ptas->count);

			if (masterMirroringCheckpointLen > 0)
			  /* KAS this is probably always true for new twophase. */
			{
				int filespaceCount;
				int tablespaceCount;
				int databaseCount;

				if (mmxlog_get_checkpoint_counts(
										masterMirroringCheckpoint,
										masterMirroringCheckpointLen,
										record->xl_len,
										&beginLoc,
										/* errlevel */ -1,	// Suppress elog altogether on master mirroring checkpoint length checking.
										&tablespaceCount,
										&filespaceCount,
										&databaseCount))

				{
					appendStringInfo(buf,
						 ", master mirroring information: %d filespaces, %d tablespaces, %d databases",
						 filespaceCount,
						 tablespaceCount,
						 databaseCount);
				}
			}
		}
	}
	else if (info == XLOG_NEXTOID)
	{
		Oid			nextOid;

		memcpy(&nextOid, rec, sizeof(Oid));
		appendStringInfo(buf, "nextOid: %u", nextOid);
	}
	else if (info == XLOG_SWITCH)
	{
		appendStringInfo(buf, "xlog switch");
	}
	else
		appendStringInfo(buf, "UNKNOWN");
}

void
XLog_OutRec(StringInfo buf, XLogRecord *record)
{
	int			i;

	appendStringInfo(buf, "prev %X/%X; xid %u",
					 record->xl_prev.xlogid, record->xl_prev.xrecoff,
					 record->xl_xid);

	for (i = 0; i < XLR_MAX_BKP_BLOCKS; i++)
	{
		if (record->xl_info & XLR_SET_BKP_BLOCK(i))
			appendStringInfo(buf, "; bkpb%d", i + 1);
	}

	appendStringInfo(buf, ": %s", RmgrTable[record->xl_rmid].rm_name);
}


/*
 * GUC support
 */
const char *
assign_xlog_sync_method(const char *method, bool doit, GucSource source __attribute__((unused)) )
{
	int			new_sync_method;
	int			new_sync_bit;

	if (pg_strcasecmp(method, "fsync") == 0)
	{
		new_sync_method = SYNC_METHOD_FSYNC;
		new_sync_bit = 0;
	}
#ifdef HAVE_FSYNC_WRITETHROUGH
	else if (pg_strcasecmp(method, "fsync_writethrough") == 0)
	{
		new_sync_method = SYNC_METHOD_FSYNC_WRITETHROUGH;
		new_sync_bit = 0;
	}
#endif
#ifdef HAVE_FDATASYNC
	else if (pg_strcasecmp(method, "fdatasync") == 0)
	{
		new_sync_method = SYNC_METHOD_FDATASYNC;
		new_sync_bit = 0;
	}
#endif
#ifdef OPEN_SYNC_FLAG
	else if (pg_strcasecmp(method, "open_sync") == 0)
	{
		new_sync_method = SYNC_METHOD_OPEN;
		new_sync_bit = OPEN_SYNC_FLAG;
	}
#endif
#ifdef OPEN_DATASYNC_FLAG
	else if (pg_strcasecmp(method, "open_datasync") == 0)
	{
		new_sync_method = SYNC_METHOD_OPEN;
		new_sync_bit = OPEN_DATASYNC_FLAG;
	}
#endif
	else
		return NULL;

	if (!doit)
		return method;

	if (sync_method != new_sync_method || open_sync_bit != new_sync_bit)
	{
		/*
		 * To ensure that no blocks escape unsynced, force an fsync on the
		 * currently open log segment (if any).  Also, if the open flag is
		 * changing, close the log file so it will be reopened (with new flag
		 * bit) at next use.
		 */
		if (MirroredFlatFile_IsActive(&mirroredLogFileOpen))
		{
			if (MirroredFlatFile_Flush(
								&mirroredLogFileOpen,
								/* suppressError */ true))
				ereport(PANIC,
						(errcode_for_file_access(),
						 errmsg("could not fsync log file %u, segment %u: %m",
								openLogId, openLogSeg)));
			if (open_sync_bit != new_sync_bit)
				XLogFileClose();
		}
		sync_method = new_sync_method;
		open_sync_bit = new_sync_bit;
	}

	return method;
}

/*
 * Issue appropriate kind of fsync (if any) for an XLOG output file.
 *
 * 'fd' is a file descriptor for the XLOG file to be fsync'd.
 * 'log' and 'seg' are for error reporting purposes.
 */
void
issue_xlog_fsync(int fd, uint32 log, uint32 seg)
{
	switch (sync_method)
	{
		case SYNC_METHOD_FSYNC:
			if (pg_fsync_no_writethrough(fd) != 0)
				ereport(PANIC,
						(errcode_for_file_access(),
						 errmsg("could not fsync log file %u, segment %u: %m",
								log, seg)));
			break;
#ifdef HAVE_FSYNC_WRITETHROUGH
		case SYNC_METHOD_FSYNC_WRITETHROUGH:
			if (pg_fsync_writethrough(fd) != 0)
				ereport(PANIC,
						(errcode_for_file_access(),
						 errmsg("could not fsync write-through log file %u, segment %u: %m",
								log, seg)));
			break;
#endif
#ifdef HAVE_FDATASYNC
		case SYNC_METHOD_FDATASYNC:
			if (pg_fdatasync(fd) != 0)
				ereport(PANIC,
						(errcode_for_file_access(),
					errmsg("could not fdatasync log file %u, segment %u: %m",
						   log, seg)));
			break;
#endif
		case SYNC_METHOD_OPEN:
//		case SYNC_METHOD_OPEN_DSYNC:
			/* write synced it already */
			break;
		default:
			elog(PANIC, "unrecognized wal_sync_method: %d", sync_method);
			break;
	}
}

/*
 * do_pg_start_backup is the workhorse of the user-visible pg_start_backup()
 * function. It creates the necessary starting checkpoint and constructs the
 * backup label file.
 *
 * There are two kind of backups: exclusive and non-exclusive. An exclusive
 * backup is started with pg_start_backup(), and there can be only one active
 * at a time. The backup label file of an exclusive backup is written to
 * $PGDATA/backup_label, and it is removed by pg_stop_backup().
 *
 * A non-exclusive backup is used for the streaming base backups (see
 * src/backend/replication/basebackup.c). The difference to exclusive backups
 * is that the backup label file is not written to disk. Instead, its would-be
 * contents are returned in *labelfile, and the caller is responsible for
 * including it in the backup archive as 'backup_label'. There can be many
 * non-exclusive backups active at the same time, and they don't conflict
 * with an exclusive backup either.
 *
 * Every successfully started non-exclusive backup must be stopped by calling
 * do_pg_stop_backup() or do_pg_abort_backup().
 */
XLogRecPtr
do_pg_start_backup(const char *backupidstr, bool fast, char **labelfile)
{
	bool		exclusive = (labelfile == NULL);
	bool		backup_started_in_recovery = false;
	XLogRecPtr	checkpointloc;
	XLogRecPtr	startpoint;
	pg_time_t	stamp_time;
	char		strfbuf[128];
	char		xlogfilename[MAXFNAMELEN];
	uint32		_logId;
	uint32		_logSeg;
	struct stat stat_buf;
	FILE	   *fp;
	StringInfoData labelfbuf;

	/* base backup in recovery mode not currently supported */
	backup_started_in_recovery = false;

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
		   errmsg("must be superuser or replication role to run a backup")));

	if (strlen(backupidstr) > MAXPGPATH)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("backup label too long (max %d bytes)",
						MAXPGPATH)));

	/*
	 * Mark backup active in shared memory.  We must do full-page WAL writes
	 * during an on-line backup even if not doing so at other times, because
	 * it's quite possible for the backup dump to obtain a "torn" (partially
	 * written) copy of a database page if it reads the page concurrently with
	 * our write to the same page.	This can be fixed as long as the first
	 * write to the page in the WAL sequence is a full-page write. Hence, we
	 * turn on forcePageWrites and then force a CHECKPOINT, to ensure there
	 * are no dirty pages in shared memory that might get dumped while the
	 * backup is in progress without having a corresponding WAL record.  (Once
	 * the backup is complete, we need not force full-page writes anymore,
	 * since we expect that any pages not modified during the backup interval
	 * must have been correctly captured by the backup.)
	 *
	 * Note that forcePageWrites has no effect during an online backup from
	 * the standby.
	 *
	 * We must hold WALInsertLock to change the value of forcePageWrites, to
	 * ensure adequate interlocking against XLogInsert().
	 */
	LWLockAcquire(WALInsertLock, LW_EXCLUSIVE);
	if (exclusive)
	{
		if (XLogCtl->Insert.exclusiveBackup)
		{
			LWLockRelease(WALInsertLock);
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("a backup is already in progress"),
					 errhint("Run pg_stop_backup() and try again.")));
		}
		XLogCtl->Insert.exclusiveBackup = true;
	}
	else
		XLogCtl->Insert.nonExclusiveBackups++;
	XLogCtl->Insert.forcePageWrites = true;
	LWLockRelease(WALInsertLock);

	/* Ensure we release forcePageWrites if fail below */
	PG_ENSURE_ERROR_CLEANUP(pg_start_backup_callback, (Datum) BoolGetDatum(exclusive));
	{
		bool		gotUniqueStartpoint = false;

		/*
		 * Force an XLOG file switch before the checkpoint, to ensure that the
		 * WAL segment the checkpoint is written to doesn't contain pages with
		 * old timeline IDs.  That would otherwise happen if you called
		 * pg_start_backup() right after restoring from a PITR archive: the
		 * first WAL segment containing the startup checkpoint has pages in
		 * the beginning with the old timeline ID.	That can cause trouble at
		 * recovery: we won't have a history file covering the old timeline if
		 * pg_xlog directory was not included in the base backup and the WAL
		 * archive was cleared too before starting the backup.
		 *
		 * This also ensures that we have emitted a WAL page header that has
		 * XLP_BKP_REMOVABLE off before we emit the checkpoint record.
		 * Therefore, if a WAL archiver (such as pglesslog) is trying to
		 * compress out removable backup blocks, it won't remove any that
		 * occur after this point.
		 *
		 * During recovery, we skip forcing XLOG file switch, which means that
		 * the backup taken during recovery is not available for the special
		 * recovery case described above.
		 */
		if (!backup_started_in_recovery)
			RequestXLogSwitch();

		do
		{
			/*
			 * Force a CHECKPOINT.	Aside from being necessary to prevent torn
			 * page problems, this guarantees that two successive backup runs
			 * will have different checkpoint positions and hence different
			 * history file names, even if nothing happened in between.
			 *
			 * During recovery, establish a restartpoint if possible. We use
			 * the last restartpoint as the backup starting checkpoint. This
			 * means that two successive backup runs can have same checkpoint
			 * positions.
			 *
			 * Since the fact that we are executing do_pg_start_backup()
			 * during recovery means that checkpointer is running, we can use
			 * RequestCheckpoint() to establish a restartpoint.
			 *
			 * We use CHECKPOINT_IMMEDIATE only if requested by user (via
			 * passing fast = true).  Otherwise this can take awhile.
			 */
			RequestCheckpoint(true, false);

			/*
			 * Now we need to fetch the checkpoint record location, and also
			 * its REDO pointer.  The oldest point in WAL that would be needed
			 * to restore starting from the checkpoint is precisely the REDO
			 * pointer.
			 */
			LWLockAcquire(ControlFileLock, LW_SHARED);
			checkpointloc = ControlFile->checkPoint;
			startpoint = ControlFile->checkPointCopy.redo;
			LWLockRelease(ControlFileLock);

			/*
			 * If two base backups are started at the same time (in WAL sender
			 * processes), we need to make sure that they use different
			 * checkpoints as starting locations, because we use the starting
			 * WAL location as a unique identifier for the base backup in the
			 * end-of-backup WAL record and when we write the backup history
			 * file. Perhaps it would be better generate a separate unique ID
			 * for each backup instead of forcing another checkpoint, but
			 * taking a checkpoint right after another is not that expensive
			 * either because only few buffers have been dirtied yet.
			 */
			LWLockAcquire(WALInsertLock, LW_SHARED);
			if (XLByteLT(XLogCtl->Insert.lastBackupStart, startpoint))
			{
				XLogCtl->Insert.lastBackupStart = startpoint;
				gotUniqueStartpoint = true;
			}
			LWLockRelease(WALInsertLock);
		} while (!gotUniqueStartpoint);

		XLByteToSeg(startpoint, _logId, _logSeg);
		XLogFileName(xlogfilename, ThisTimeLineID, _logId, _logSeg);

		/*
		 * Construct backup label file
		 */
		initStringInfo(&labelfbuf);

		/* Use the log timezone here, not the session timezone */
		stamp_time = (pg_time_t) time(NULL);
		pg_strftime(strfbuf, sizeof(strfbuf),
					"%Y-%m-%d %H:%M:%S %Z",
					pg_localtime(&stamp_time, log_timezone));
		appendStringInfo(&labelfbuf, "START WAL LOCATION: %X/%X (file %s)\n",
						 startpoint.xlogid, startpoint.xrecoff, xlogfilename);
		appendStringInfo(&labelfbuf, "CHECKPOINT LOCATION: %X/%X\n",
						 checkpointloc.xlogid, checkpointloc.xrecoff);
		appendStringInfo(&labelfbuf, "BACKUP METHOD: %s\n",
						 exclusive ? "pg_start_backup" : "streamed");
		appendStringInfo(&labelfbuf, "BACKUP FROM: %s\n",
						 backup_started_in_recovery ? "standby" : "master");
		appendStringInfo(&labelfbuf, "START TIME: %s\n", strfbuf);
		appendStringInfo(&labelfbuf, "LABEL: %s\n", backupidstr);

		elogif(debug_basebackup, LOG, "basebackup label file --\n%s", labelfbuf.data);

		/*
		 * Okay, write the file, or return its contents to caller.
		 */
		if (exclusive)
		{
			/*
			 * Check for existing backup label --- implies a backup is already
			 * running.  (XXX given that we checked exclusiveBackup above,
			 * maybe it would be OK to just unlink any such label file?)
			 */
			if (stat(BACKUP_LABEL_FILE, &stat_buf) != 0)
			{
				if (errno != ENOENT)
					ereport(ERROR,
							(errcode_for_file_access(),
							 errmsg("could not stat file \"%s\": %m",
									BACKUP_LABEL_FILE)));
			}
			else
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						 errmsg("a backup is already in progress"),
						 errhint("If you're sure there is no backup in progress, remove file \"%s\" and try again.",
								 BACKUP_LABEL_FILE)));

			fp = AllocateFile(BACKUP_LABEL_FILE, "w");

			if (!fp)
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not create file \"%s\": %m",
								BACKUP_LABEL_FILE)));
			if (fwrite(labelfbuf.data, labelfbuf.len, 1, fp) != 1 ||
				fflush(fp) != 0 ||
				pg_fsync(fileno(fp)) != 0 ||
				ferror(fp) ||
				FreeFile(fp))
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not write file \"%s\": %m",
								BACKUP_LABEL_FILE)));
			pfree(labelfbuf.data);
		}
		else
			*labelfile = labelfbuf.data;
	}
	PG_END_ENSURE_ERROR_CLEANUP(pg_start_backup_callback, (Datum) BoolGetDatum(exclusive));

	/*
	 * We're done.  As a convenience, return the starting WAL location.
	 */
	return startpoint;
}

/* Error cleanup callback for pg_start_backup */
static void
pg_start_backup_callback(int code, Datum arg)
{
	bool		exclusive = DatumGetBool(arg);

	/* Update backup counters and forcePageWrites on failure */
	LWLockAcquire(WALInsertLock, LW_EXCLUSIVE);
	if (exclusive)
	{
		Assert(XLogCtl->Insert.exclusiveBackup);
		XLogCtl->Insert.exclusiveBackup = false;
	}
	else
	{
		Assert(XLogCtl->Insert.nonExclusiveBackups > 0);
		XLogCtl->Insert.nonExclusiveBackups--;
	}

	if (!XLogCtl->Insert.exclusiveBackup &&
		XLogCtl->Insert.nonExclusiveBackups == 0)
	{
		XLogCtl->Insert.forcePageWrites = false;
	}
	LWLockRelease(WALInsertLock);
}

/*
 * do_pg_stop_backup is the workhorse of the user-visible pg_stop_backup()
 * function.

 * If labelfile is NULL, this stops an exclusive backup. Otherwise this stops
 * the non-exclusive backup specified by 'labelfile'.
 */
XLogRecPtr
do_pg_stop_backup(char *labelfile)
{
	bool		exclusive = (labelfile == NULL);
	bool		backup_started_in_recovery = false;
	XLogRecPtr	startpoint;
	XLogRecPtr	stoppoint;
	XLogRecData rdata;
	pg_time_t	stamp_time;
	char		strfbuf[128];
	char		histfilepath[MAXPGPATH];
	char		startxlogfilename[MAXFNAMELEN];
	char		stopxlogfilename[MAXFNAMELEN];
	char		backupfrom[20];
	uint32		_logId;
	uint32		_logSeg;
	FILE	   *lfp;
	FILE	   *fp;
	char		ch;
	char	   *remaining;
	char	   *ptr;

	/* Currently backup during recovery not supported */
	backup_started_in_recovery = false;

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
		 (errmsg("must be superuser or replication role to run a backup"))));

	/*
	 * OK to update backup counters and forcePageWrites
	 */
	LWLockAcquire(WALInsertLock, LW_EXCLUSIVE);
	if (exclusive)
		XLogCtl->Insert.exclusiveBackup = false;
	else
	{
		/*
		 * The user-visible pg_start/stop_backup() functions that operate on
		 * exclusive backups can be called at any time, but for non-exclusive
		 * backups, it is expected that each do_pg_start_backup() call is
		 * matched by exactly one do_pg_stop_backup() call.
		 */
		Assert(XLogCtl->Insert.nonExclusiveBackups > 0);
		XLogCtl->Insert.nonExclusiveBackups--;
	}

	if (!XLogCtl->Insert.exclusiveBackup &&
		XLogCtl->Insert.nonExclusiveBackups == 0)
	{
		XLogCtl->Insert.forcePageWrites = false;
	}
	LWLockRelease(WALInsertLock);

	if (exclusive)
	{
		/*
		 * Read the existing label file into memory.
		 */
		struct stat statbuf;
		int			r;

		if (stat(BACKUP_LABEL_FILE, &statbuf))
		{
			if (errno != ENOENT)
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not stat file \"%s\": %m",
								BACKUP_LABEL_FILE)));
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("a backup is not in progress")));
		}

		lfp = AllocateFile(BACKUP_LABEL_FILE, "r");
		if (!lfp)
		{
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not read file \"%s\": %m",
							BACKUP_LABEL_FILE)));
		}
		labelfile = palloc(statbuf.st_size + 1);
		r = fread(labelfile, statbuf.st_size, 1, lfp);
		labelfile[statbuf.st_size] = '\0';

		/*
		 * Close and remove the backup label file
		 */
		if (r != 1 || ferror(lfp) || FreeFile(lfp))
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not read file \"%s\": %m",
							BACKUP_LABEL_FILE)));
		if (unlink(BACKUP_LABEL_FILE) != 0)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not remove file \"%s\": %m",
							BACKUP_LABEL_FILE)));
	}

	/*
	 * Read and parse the START WAL LOCATION line (this code is pretty crude,
	 * but we are not expecting any variability in the file format).
	 */
	if (sscanf(labelfile, "START WAL LOCATION: %X/%X (file %24s)%c",
			   &startpoint.xlogid, &startpoint.xrecoff, startxlogfilename,
			   &ch) != 4 || ch != '\n')
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("invalid data in file \"%s\"", BACKUP_LABEL_FILE)));
	remaining = strchr(labelfile, '\n') + 1;	/* %n is not portable enough */

	/*
	 * Parse the BACKUP FROM line. If we are taking an online backup from the
	 * standby, we confirm that the standby has not been promoted during the
	 * backup.
	 */
	ptr = strstr(remaining, "BACKUP FROM:");
	if (!ptr || sscanf(ptr, "BACKUP FROM: %19s\n", backupfrom) != 1)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("invalid data in file \"%s\"", BACKUP_LABEL_FILE)));
	if (strcmp(backupfrom, "standby") == 0 && !backup_started_in_recovery)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("the standby was promoted during online backup"),
				 errhint("This means that the backup being taken is corrupt "
						 "and should not be used. "
						 "Try taking another online backup.")));

	/*
	 * Write the backup-end xlog record
	 */
	rdata.data = (char *) (&startpoint);
	rdata.len = sizeof(startpoint);
	rdata.buffer = InvalidBuffer;
	rdata.next = NULL;
	stoppoint = XLogInsert(RM_XLOG_ID, XLOG_BACKUP_END, &rdata);

	elog(LOG, "Basebackup stop point is at %X/%X.",
			   stoppoint.xlogid, stoppoint.xrecoff);

	/*
	 * Force a switch to a new xlog segment file, so that the backup is valid
	 * as soon as archiver moves out the current segment file.
	 */
	RequestXLogSwitch();

	XLByteToPrevSeg(stoppoint, _logId, _logSeg);
	XLogFileName(stopxlogfilename, ThisTimeLineID, _logId, _logSeg);

	/* Use the log timezone here, not the session timezone */
	stamp_time = (pg_time_t) time(NULL);
	pg_strftime(strfbuf, sizeof(strfbuf),
				"%Y-%m-%d %H:%M:%S %Z",
				pg_localtime(&stamp_time, log_timezone));

	/*
	 * Write the backup history file
	 */
	XLByteToSeg(startpoint, _logId, _logSeg);
	BackupHistoryFilePath(histfilepath, ThisTimeLineID, _logId, _logSeg,
						  startpoint.xrecoff % XLogSegSize);
	fp = AllocateFile(histfilepath, "w");
	if (!fp)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not create file \"%s\": %m",
						histfilepath)));
	fprintf(fp, "START WAL LOCATION: %X/%X (file %s)\n",
			startpoint.xlogid, startpoint.xrecoff, startxlogfilename);
	fprintf(fp, "STOP WAL LOCATION: %X/%X (file %s)\n",
			stoppoint.xlogid, stoppoint.xrecoff, stopxlogfilename);
	/* transfer remaining lines from label to history file */
	fprintf(fp, "%s", remaining);
	fprintf(fp, "STOP TIME: %s\n", strfbuf);
	if (fflush(fp) || ferror(fp) || FreeFile(fp))
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not write file \"%s\": %m",
						histfilepath)));

	/*
	 * Clean out any no-longer-needed history files.  As a side effect, this
	 * will post a .ready file for the newly created history file, notifying
	 * the archiver that history file may be archived immediately.
	 */
	CleanupBackupHistory();

	/*
	 * We're done.  As a convenience, return the ending WAL location.
	 */
	return stoppoint;
}

/*
 * do_pg_abort_backup: abort a running backup
 *
 * This does just the most basic steps of do_pg_stop_backup(), by taking the
 * system out of backup mode, thus making it a lot more safe to call from
 * an error handler.
 *
 * NB: This is only for aborting a non-exclusive backup that doesn't write
 * backup_label. A backup started with pg_stop_backup() needs to be finished
 * with pg_stop_backup().
 */
void
do_pg_abort_backup(void)
{
	LWLockAcquire(WALInsertLock, LW_EXCLUSIVE);
	Assert(XLogCtl->Insert.nonExclusiveBackups > 0);
	XLogCtl->Insert.nonExclusiveBackups--;

	if (!XLogCtl->Insert.exclusiveBackup &&
		XLogCtl->Insert.nonExclusiveBackups == 0)
	{
		XLogCtl->Insert.forcePageWrites = false;
	}
	LWLockRelease(WALInsertLock);
}


/*
 * pg_switch_xlog: switch to next xlog file
 */
Datum
pg_switch_xlog(PG_FUNCTION_ARGS __attribute__((unused)) )
{
	text	   *result;
	XLogRecPtr	switchpoint;
	char		location[MAXFNAMELEN];

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 (errmsg("must be superuser to switch transaction log files"))));

	switchpoint = RequestXLogSwitch();

	/*
	 * As a convenience, return the WAL location of the switch record
	 */
	snprintf(location, sizeof(location), "%X/%X",
			 switchpoint.xlogid, switchpoint.xrecoff);
	result = DatumGetTextP(DirectFunctionCall1(textin,
											   CStringGetDatum(location)));
	PG_RETURN_TEXT_P(result);
}

/*
 * Report the current WAL write location (same format as pg_start_backup etc)
 *
 * This is useful for determining how much of WAL is visible to an external
 * archiving process.  Note that the data before this point is written out
 * to the kernel, but is not necessarily synced to disk.
 */
Datum
pg_current_xlog_location(PG_FUNCTION_ARGS __attribute__((unused)) )
{
	text	   *result;
	char		location[MAXFNAMELEN];

	/* Make sure we have an up-to-date local LogwrtResult */
	{
		/* use volatile pointer to prevent code rearrangement */
		volatile XLogCtlData *xlogctl = XLogCtl;

		SpinLockAcquire(&xlogctl->info_lck);
		LogwrtResult = xlogctl->LogwrtResult;
		SpinLockRelease(&xlogctl->info_lck);
	}

	snprintf(location, sizeof(location), "%X/%X",
			 LogwrtResult.Write.xlogid, LogwrtResult.Write.xrecoff);

	result = DatumGetTextP(DirectFunctionCall1(textin,
											   CStringGetDatum(location)));
	PG_RETURN_TEXT_P(result);
}

/*
 * Report the current WAL insert location (same format as pg_start_backup etc)
 *
 * This function is mostly for debugging purposes.
 */
Datum
pg_current_xlog_insert_location(PG_FUNCTION_ARGS __attribute__((unused)) )
{
	text	   *result;
	XLogCtlInsert *Insert = &XLogCtl->Insert;
	XLogRecPtr	current_recptr;
	char		location[MAXFNAMELEN];

	/*
	 * Get the current end-of-WAL position ... shared lock is sufficient
	 */
	LWLockAcquire(WALInsertLock, LW_SHARED);
	INSERT_RECPTR(current_recptr, Insert, Insert->curridx);
	LWLockRelease(WALInsertLock);

	snprintf(location, sizeof(location), "%X/%X",
			 current_recptr.xlogid, current_recptr.xrecoff);

	result = DatumGetTextP(DirectFunctionCall1(textin,
											   CStringGetDatum(location)));
	PG_RETURN_TEXT_P(result);
}

/*
 * Compute an xlog file name and decimal byte offset given a WAL location,
 * such as is returned by pg_stop_backup() or pg_xlog_switch().
 *
 * Note that a location exactly at a segment boundary is taken to be in
 * the previous segment.  This is usually the right thing, since the
 * expected usage is to determine which xlog file(s) are ready to archive.
 */
Datum
pg_xlogfile_name_offset(PG_FUNCTION_ARGS)
{
	text	   *location = PG_GETARG_TEXT_P(0);
	char	   *locationstr;
	unsigned int uxlogid;
	unsigned int uxrecoff;
	uint32		xlogid;
	uint32		xlogseg;
	uint32		xrecoff;
	XLogRecPtr	locationpoint;
	char		xlogfilename[MAXFNAMELEN];
	Datum		values[2];
	bool		isnull[2];
	TupleDesc	resultTupleDesc;
	HeapTuple	resultHeapTuple;
	Datum		result;

	/*
	 * Read input and parse
	 */
	locationstr = DatumGetCString(DirectFunctionCall1(textout,
												 PointerGetDatum(location)));

	if (sscanf(locationstr, "%X/%X", &uxlogid, &uxrecoff) != 2)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("could not parse transaction log location \"%s\"",
						locationstr)));

	locationpoint.xlogid = uxlogid;
	locationpoint.xrecoff = uxrecoff;

	/*
	 * Construct a tuple descriptor for the result row.  This must match this
	 * function's pg_proc entry!
	 */
	resultTupleDesc = CreateTemplateTupleDesc(2, false);
	TupleDescInitEntry(resultTupleDesc, (AttrNumber) 1, "file_name",
					   TEXTOID, -1, 0);
	TupleDescInitEntry(resultTupleDesc, (AttrNumber) 2, "file_offset",
					   INT4OID, -1, 0);

	resultTupleDesc = BlessTupleDesc(resultTupleDesc);

	/*
	 * xlogfilename
	 */
	XLByteToPrevSeg(locationpoint, xlogid, xlogseg);
	XLogFileName(xlogfilename, ThisTimeLineID, xlogid, xlogseg);

	values[0] = DirectFunctionCall1(textin,
									CStringGetDatum(xlogfilename));
	isnull[0] = false;

	/*
	 * offset
	 */
	xrecoff = locationpoint.xrecoff - xlogseg * XLogSegSize;

	values[1] = UInt32GetDatum(xrecoff);
	isnull[1] = false;

	/*
	 * Tuple jam: Having first prepared your Datums, then squash together
	 */
	resultHeapTuple = heap_form_tuple(resultTupleDesc, values, isnull);

	result = HeapTupleGetDatum(resultHeapTuple);

	PG_RETURN_DATUM(result);
}

/*
 * Compute an xlog file name given a WAL location,
 * such as is returned by pg_stop_backup() or pg_xlog_switch().
 */
Datum
pg_xlogfile_name(PG_FUNCTION_ARGS)
{
	text	   *location = PG_GETARG_TEXT_P(0);
	text	   *result;
	char	   *locationstr;
	unsigned int uxlogid;
	unsigned int uxrecoff;
	uint32		xlogid;
	uint32		xlogseg;
	XLogRecPtr	locationpoint;
	char		xlogfilename[MAXFNAMELEN];

	locationstr = DatumGetCString(DirectFunctionCall1(textout,
												 PointerGetDatum(location)));

	if (sscanf(locationstr, "%X/%X", &uxlogid, &uxrecoff) != 2)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("could not parse transaction log location \"%s\"",
						locationstr)));

	locationpoint.xlogid = uxlogid;
	locationpoint.xrecoff = uxrecoff;

	XLByteToPrevSeg(locationpoint, xlogid, xlogseg);
	XLogFileName(xlogfilename, ThisTimeLineID, xlogid, xlogseg);

	result = DatumGetTextP(DirectFunctionCall1(textin,
											 CStringGetDatum(xlogfilename)));
	PG_RETURN_TEXT_P(result);
}

/*
 * read_backup_label: check to see if a backup_label file is present
 *
 * If we see a backup_label during recovery, we assume that we are recovering
 * from a backup dump file, and we therefore roll forward from the checkpoint
 * identified by the label file, NOT what pg_control says.	This avoids the
 * problem that pg_control might have been archived one or more checkpoints
 * later than the start of the dump, and so if we rely on it as the start
 * point, we will fail to restore a consistent database state.
 *
 * Returns TRUE if a backup_label was found (and fills the checkpoint
 * location and its REDO location into *checkPointLoc and RedoStartLSN,
 * respectively); returns FALSE if not. If this backup_label came from a
 * streamed backup, *backupEndRequired is set to TRUE.
 */
static bool
read_backup_label(XLogRecPtr *checkPointLoc, bool *backupEndRequired)
{
	char		startxlogfilename[MAXFNAMELEN];
	TimeLineID	tli;
	FILE	   *lfp;
	char		ch;
	char		backuptype[20];
	char		backupfrom[20];

	*backupEndRequired = false;

	/*
	 * See if label file is present
	 */
	lfp = AllocateFile(BACKUP_LABEL_FILE, "r");
	if (!lfp)
	{
		if (errno != ENOENT)
			ereport(FATAL,
					(errcode_for_file_access(),
					 errmsg("could not read file \"%s\": %m",
							BACKUP_LABEL_FILE)));
		return false;			/* it's not there, all is fine */
	}

	/*
	 * Read and parse the START WAL LOCATION, CHECKPOINT and BACKUP_METHOD
	 * lines (this code is pretty crude, but we are not expecting any variability
	 * in the file format).
	 */
	if (fscanf(lfp, "START WAL LOCATION: %X/%X (file %08X%16s)%c",
			   &RedoStartLSN.xlogid, &RedoStartLSN.xrecoff, &tli,
			   startxlogfilename, &ch) != 5 || ch != '\n')
		ereport(FATAL,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("invalid data in file \"%s\"", BACKUP_LABEL_FILE)));

	if (fscanf(lfp, "CHECKPOINT LOCATION: %X/%X%c",
			   &checkPointLoc->xlogid, &checkPointLoc->xrecoff,
			   &ch) != 3 || ch != '\n')
		ereport(FATAL,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("invalid data in file \"%s\"", BACKUP_LABEL_FILE)));

	if (fscanf(lfp, "BACKUP METHOD: %19s\n", backuptype) == 1)
	{
		/* Streaming backup method is only supported */
		if (strcmp(backuptype, "streamed") == 0)
			*backupEndRequired = true;
		else
			ereport(FATAL,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("invalid data in file \"%s\"", BACKUP_LABEL_FILE)));

	}

	if (fscanf(lfp, "BACKUP FROM: %19s\n", backupfrom) == 1)
	{
		/* Backup from standby is not supported */
		if (strcmp(backupfrom, "master") != 0)
			ereport(FATAL,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("invalid data in file \"%s\"", BACKUP_LABEL_FILE)));
	}

	if (ferror(lfp) || FreeFile(lfp))
		ereport(FATAL,
				(errcode_for_file_access(),
				 errmsg("could not read file \"%s\": %m",
						BACKUP_LABEL_FILE)));

	return true;
}

/*
 * Get latest redo apply position.
 *
 * Optionally, returns the current recovery target timeline. Callers not
 * interested in that may pass NULL for targetTLI.
 *
 * Exported to allow WAL receiver to read the pointer directly.
 */
XLogRecPtr
GetXLogReplayRecPtr(TimeLineID *targetTLI)
{
	/* use volatile pointer to prevent code rearrangement */
	volatile XLogCtlData *xlogctl = XLogCtl;
	XLogRecPtr	recptr;

	SpinLockAcquire(&xlogctl->info_lck);
	recptr = xlogctl->lastReplayedEndRecPtr;
	if (targetTLI)
		*targetTLI = xlogctl->RecoveryTargetTLI;
	SpinLockRelease(&xlogctl->info_lck);

	return recptr;
}

/*
 * Get current standby flush position, ie, the last WAL position
 * known to be fsync'd to disk in standby.
 *
 * If 'targetTLI' is not NULL, it's set to the current recovery target
 * timeline.
 */
XLogRecPtr
GetStandbyFlushRecPtr(TimeLineID *targetTLI)
{
	XLogRecPtr      receivePtr;
	XLogRecPtr      replayPtr;

	receivePtr = GetWalRcvWriteRecPtr(NULL);
	replayPtr = GetXLogReplayRecPtr(targetTLI);

	if (XLByteLT(receivePtr, replayPtr))
		return replayPtr;
	else
		return receivePtr;
}

/*
 * GetRecoveryTargetTLI - get the current recovery target timeline ID
 */
TimeLineID
GetRecoveryTargetTLI(void)
{
	/* use volatile pointer to prevent code rearrangement */
	volatile XLogCtlData *xlogctl = XLogCtl;
	TimeLineID result;

	SpinLockAcquire(&xlogctl->info_lck);
	result = xlogctl->RecoveryTargetTLI;
	SpinLockRelease(&xlogctl->info_lck);

	return result;
}

/*
 * Error context callback for errors occurring during rm_redo().
 */
static void
rm_redo_error_callback(void *arg)
{
	RedoErrorCallBack *redoErrorCallBack = (RedoErrorCallBack*) arg;
	StringInfoData buf;

	initStringInfo(&buf);
	RmgrTable[redoErrorCallBack->record->xl_rmid].rm_desc(
												   &buf,
												   redoErrorCallBack->location,
												   redoErrorCallBack->record);

	/* don't bother emitting empty description */
	if (buf.len > 0)
		errcontext("xlog redo %s", buf.data);

	pfree(buf.data);
}

static char *
XLogLocationToBuffer(char *buffer, XLogRecPtr *loc, bool longFormat)
{

	if (longFormat)
	{
		uint32 seg = loc->xrecoff / XLogSegSize;
		uint32 offset = loc->xrecoff % XLogSegSize;
		sprintf(buffer,
			    "%X/%X (==> seg %d, offset 0x%X)",
			    loc->xlogid, loc->xrecoff,
			    seg, offset);
	}
	else
		sprintf(buffer,
			    "%X/%X",
			    loc->xlogid, loc->xrecoff);

	return buffer;
}

static char xlogLocationBuffer[50];
static char xlogLocationBuffer2[50];
static char xlogLocationBuffer3[50];
static char xlogLocationBuffer4[50];
static char xlogLocationBuffer5[50];

char *
XLogLocationToString(XLogRecPtr *loc)
{
	return XLogLocationToBuffer(xlogLocationBuffer, loc, Debug_print_qd_mirroring);
}

char *
XLogLocationToString2(XLogRecPtr *loc)
{
	return XLogLocationToBuffer(xlogLocationBuffer2, loc, Debug_print_qd_mirroring);
}

char *
XLogLocationToString3(XLogRecPtr *loc)
{
	return XLogLocationToBuffer(xlogLocationBuffer3, loc, Debug_print_qd_mirroring);
}

char *
XLogLocationToString4(XLogRecPtr *loc)
{
	return XLogLocationToBuffer(xlogLocationBuffer4, loc, Debug_print_qd_mirroring);
}

char *
XLogLocationToString5(XLogRecPtr *loc)
{
	return XLogLocationToBuffer(xlogLocationBuffer5, loc, Debug_print_qd_mirroring);
}

char *
XLogLocationToString_Long(XLogRecPtr *loc)
{
	return XLogLocationToBuffer(xlogLocationBuffer, loc, true);
}

char *
XLogLocationToString2_Long(XLogRecPtr *loc)
{
	return XLogLocationToBuffer(xlogLocationBuffer2, loc, true);
}

char *
XLogLocationToString3_Long(XLogRecPtr *loc)
{
	return XLogLocationToBuffer(xlogLocationBuffer3, loc, true);
}

char *
XLogLocationToString4_Long(XLogRecPtr *loc)
{
	return XLogLocationToBuffer(xlogLocationBuffer4, loc, true);
}

char *
XLogLocationToString5_Long(XLogRecPtr *loc)
{
	return XLogLocationToBuffer(xlogLocationBuffer5, loc, true);
}


void xlog_print_redo_read_buffer_not_found(
	Relation 		reln,
	BlockNumber 	blkno,
	XLogRecPtr 		lsn,
	const char 		*funcName)
{
	if (funcName != NULL)
		elog(PersistentRecovery_DebugPrintLevel(),
			 "%s redo for %u/%u/%u did not find buffer for block %d (LSN %s)",
			 funcName,
			 reln->rd_node.spcNode,
			 reln->rd_node.dbNode,
			 reln->rd_node.relNode,
			 blkno,
			 XLogLocationToString(&lsn));
	else
		elog(PersistentRecovery_DebugPrintLevel(),
			 "Redo for %u/%u/%u did not find buffer for block %d (LSN %s)",
			 reln->rd_node.spcNode,
			 reln->rd_node.dbNode,
			 reln->rd_node.relNode,
			 blkno,
			 XLogLocationToString(&lsn));
}

void xlog_print_redo_lsn_application(
	Relation 		reln,
	BlockNumber 	blkno,
	void			*pagePtr,
	XLogRecPtr 		lsn,
	const char 		*funcName)
{
	Page page = (Page)pagePtr;
	XLogRecPtr	pageCurrentLsn = PageGetLSN(page);
	bool willApplyChange;

	willApplyChange = XLByteLT(pageCurrentLsn, lsn);

	if (funcName != NULL)
		elog(PersistentRecovery_DebugPrintLevel(),
			 "%s redo application for %u/%u/%u, block %d, willApplyChange = %s, current LSN %s, change LSN %s",
			 funcName,
			 reln->rd_node.spcNode,
			 reln->rd_node.dbNode,
			 reln->rd_node.relNode,
			 blkno,
			 (willApplyChange ? "true" : "false"),
			 XLogLocationToString(&pageCurrentLsn),
			 XLogLocationToString2(&lsn));
	else
		elog(PersistentRecovery_DebugPrintLevel(),
			 "Redo application for %u/%u/%u, block %d, willApplyChange = %s, current LSN %s, change LSN %s",
			 reln->rd_node.spcNode,
			 reln->rd_node.dbNode,
			 reln->rd_node.relNode,
			 blkno,
			 (willApplyChange ? "true" : "false"),
			 XLogLocationToString(&pageCurrentLsn),
			 XLogLocationToString2(&lsn));
}

/* ------------------------------------------------------
 *  Startup Process main entry point and signal handlers
 * ------------------------------------------------------
 */

/*
 * startupproc_quickdie() occurs when signalled SIGQUIT by the postmaster.
 *
 * Some backend has bought the farm,
 * so we need to stop what we're doing and exit.
 */
static void
startupproc_quickdie(SIGNAL_ARGS __attribute__((unused)))
{
	PG_SETMASK(&BlockSig);

	/*
	 * We DO NOT want to run proc_exit() callbacks -- we're here because
	 * shared memory may be corrupted, so we don't want to try to clean up our
	 * transaction.  Just nail the windows shut and get out of town.  Now that
	 * there's an atexit callback to prevent third-party code from breaking
	 * things by calling exit() directly, we have to reset the callbacks
	 * explicitly to make this work as intended.
	 */
	on_exit_reset();

	/*
	 * Note we do exit(2) not exit(0).	This is to force the postmaster into a
	 * system reset cycle if some idiot DBA sends a manual SIGQUIT to a random
	 * backend.  This is necessary precisely because we don't clean up our
	 * shared memory state.  (The "dead man switch" mechanism in pmsignal.c
	 * should ensure the postmaster sees this as a crash, too, but no harm in
	 * being doubly sure.)
	 */
	exit(2);
}

/* SIGUSR2: set flag to finish recovery */
static void
StartupProcTriggerHandler(SIGNAL_ARGS)
{
	int			save_errno = errno;

	WakeupRecovery();

	errno = save_errno;
}

/* SIGHUP: set flag to re-read config file at next convenient time */
static void
StartupProcSigHupHandler(SIGNAL_ARGS)
{
	int			save_errno = errno;

	got_SIGHUP = true;
	WakeupRecovery();

	errno = save_errno;
}

/* SIGTERM: set flag to abort redo and exit */
static void
StartupProcShutdownHandler(SIGNAL_ARGS)
{
	int			save_errno = errno;

	if (in_restore_command)
		proc_exit(1);
	else
		shutdown_requested = true;
	WakeupRecovery();

	errno = save_errno;
}

/* Handle SIGHUP and SIGTERM signals of startup process */
void
HandleStartupProcInterrupts(void)
{
	/*
	 * Check if we were requested to re-read config file.
	 */
	if (got_SIGHUP)
	{
		got_SIGHUP = false;
		ProcessConfigFile(PGC_SIGHUP);
	}

	/*
	 * Check if we were requested to exit without finishing recovery.
	 */
	if (shutdown_requested)
		proc_exit(1);

	/*
	 * Emergency bailout if postmaster has died.  This is to avoid the
	 * necessity for manual cleanup of all postmaster children.
	 */
	if (IsUnderPostmaster && !PostmasterIsAlive(true))
		exit(1);
}

static void
HandleCrash(SIGNAL_ARGS)
{
    /**
     * Handle crash is registered as a signal handler for SIGILL/SIGBUS/SIGSEGV
     *
     * This simply calls the standard handler which will log the signal and reraise the
     *      signal if needed
     */
    StandardHandlerForSigillSigsegvSigbus_OnMainThread("a startup process", PASS_SIGNAL_ARGS);
}

/* Main entry point for startup process */
void
StartupProcessMain(int passNum)
{
	char	   *fullpath;

	am_startup = true;
	/*
	 * If possible, make this process a group leader, so that the postmaster
	 * can signal any child processes too.
	 */
#ifdef HAVE_SETSID
	if (setsid() < 0)
		elog(FATAL, "setsid() failed: %m");
#endif

	/*
	 * Properly accept or ignore signals the postmaster might send us
	 */
	pqsignal(SIGHUP, StartupProcSigHupHandler);	 /* reload config file */
	pqsignal(SIGINT, SIG_IGN);					/* ignore query cancel */
	pqsignal(SIGTERM, StartupProcShutdownHandler); /* request shutdown */
	pqsignal(SIGQUIT, startupproc_quickdie);		/* hard crash time */
	pqsignal(SIGALRM, SIG_IGN);
	pqsignal(SIGPIPE, SIG_IGN);
	pqsignal(SIGUSR1, SIG_IGN);
	if (passNum == 1)
		pqsignal(SIGUSR2, StartupProcTriggerHandler);
	else
		pqsignal(SIGUSR2, SIG_IGN);

#ifdef SIGBUS
	pqsignal(SIGBUS, HandleCrash);
#endif
#ifdef SIGILL
    pqsignal(SIGILL, HandleCrash);
#endif
#ifdef SIGSEGV
	pqsignal(SIGSEGV, HandleCrash);
#endif

	/*
	 * Reset some signals that are accepted by postmaster but not here
	 */
	pqsignal(SIGCHLD, SIG_DFL);
	pqsignal(SIGTTIN, SIG_DFL);
	pqsignal(SIGTTOU, SIG_DFL);
	pqsignal(SIGCONT, SIG_DFL);
	pqsignal(SIGWINCH, SIG_DFL);

	/*
	 * Unblock signals (they were blocked when the postmaster forked us)
	 */
	PG_SETMASK(&UnBlockSig);

	switch (passNum)
	{
	case 1:
		StartupXLOG();
		break;

	case 2:
	case 4:
		/*
		 * NOTE: The following initialization logic was borrrowed from ftsprobe.
		 */
		SetProcessingMode(InitProcessing);

		/*
		 * Create a resource owner to keep track of our resources (currently only
		 * buffer pins).
		 */
		if (passNum == 2)
		{
			CurrentResourceOwner = ResourceOwnerCreate(NULL, "Startup Pass 2");
		}
		else
		{
			Assert(passNum == 4);
			CurrentResourceOwner = ResourceOwnerCreate(NULL, "Startup Pass 4");
		}

		/*
		 * NOTE: AuxiliaryProcessMain has already called:
		 * NOTE:      BaseInit,
		 * NOTE:      InitAuxiliaryProcess instead of InitProcess, and
		 * NOTE:      InitBufferPoolBackend.
		 */

		InitXLOGAccess();

		SetProcessingMode(NormalProcessing);

		/*
		 * Add my PGPROC struct to the ProcArray.
		 *
		 * Once I have done this, I am visible to other backends!
		 */
		InitProcessPhase2();

		/*
		 * Initialize my entry in the shared-invalidation manager's array of
		 * per-backend data.
		 *
		 * Sets up MyBackendId, a unique backend identifier.
		 */
		MyBackendId = InvalidBackendId;

		/*
		 * Though this is a startup process and currently no one sends invalidation
		 * messages concurrently, we set sendOnly = false, since we have relcaches.
		 */
		SharedInvalBackendInit(false);

		if (MyBackendId > MaxBackends || MyBackendId <= 0)
			elog(FATAL, "bad backend id: %d", MyBackendId);

		/*
		 * bufmgr needs another initialization call too
		 */
		InitBufferPoolBackend();

		/* heap access requires the rel-cache */
		RelationCacheInitialize();
		InitCatalogCache();

		/*
		 * It's now possible to do real access to the system catalogs.
		 *
		 * Load relcache entries for the system catalogs.  This must create at
		 * least the minimum set of "nailed-in" cache entries.
		 */
		RelationCacheInitializePhase2();

		/*
		 * In order to access the catalog, we need a database, and a
		 * tablespace; our access to the heap is going to be slightly
		 * limited, so we'll just use some defaults.
		 */
		if (!XLogStartup_DoNextPTCatVerificationIteration())
		{
			MyDatabaseId = TemplateDbOid;
			MyDatabaseTableSpace = DEFAULTTABLESPACE_OID;
		}
		else
		{
			MyDatabaseId = XLogCtl->currentDatabaseToVerify;
			MyDatabaseTableSpace = XLogCtl->tablespaceOfCurrentDatabaseToVerify;
		}

		/*
		 * Now we can mark our PGPROC entry with the database ID */
		/* (We assume this is an atomic store so no lock is needed) 
		 */
		MyProc->databaseId = MyDatabaseId;

		fullpath = GetDatabasePath(MyDatabaseId, MyDatabaseTableSpace);

		SetDatabasePath(fullpath);

		RelationCacheInitializePhase3();

		if (passNum == 2)
		{
			StartupXLOG_Pass2();
		}
		else
		{
			Assert(passNum == 4);
			StartupXLOG_Pass4();
		}

		break;

	case 3:
		/*
		 * Pass 3 does REDO work for all non-meta-data (i.e. not the gp_persistent_* tables).
		 */
		SetProcessingMode(InitProcessing);

		/*
		 * Create a resource owner to keep track of our resources (currently only
		 * buffer pins).
		 */
		CurrentResourceOwner = ResourceOwnerCreate(NULL, "Startup Pass 3");

		/*
		 * NOTE: AuxiliaryProcessMain has already called:
		 * NOTE:      BaseInit,
		 * NOTE:      InitAuxiliaryProcess instead of InitProcess, and
		 * NOTE:      InitBufferPoolBackend.
		 */

		InitXLOGAccess();

		SetProcessingMode(NormalProcessing);

		StartupXLOG_Pass3();

		PgVersionRecoverMirror();
		break;

	default:
		elog(PANIC, "Unexpected pass number %d", passNum);
	}

	/*
	 * Exit normally. Exit code 0 tells postmaster that we completed
	 * recovery successfully.
	 */
	proc_exit(0);
}

/*
 *
 */
static
int XLogGetEof(XLogRecPtr *eofRecPtr)
{
	int	status = STATUS_OK;

	XLogRecPtr	redoCheckpointLoc;
	CheckPoint	redoCheckpoint;

	XLogRecPtr	startLoc;

	XLogRecord	*record;
	XLogRecPtr	LastRec;

	XLogGetRecoveryStart("filerep",
						 "get checkpoint location",
						 &redoCheckpointLoc,
						 &redoCheckpoint);

	startLoc = redoCheckpoint.redo;

	XLogCloseReadRecord();

	record = XLogReadRecord(&startLoc, false, DEBUG1);
	if (record == NULL)
	{
		FileRep_SetSegmentState(SegmentStateFault, FaultTypeDB);

		elog(WARNING," couldn't read start location %s",
			 XLogLocationToString(&startLoc));
		status = STATUS_ERROR;
	}

	do
	{
		LastRec = ReadRecPtr;

		record = XLogReadRecord(NULL, false, DEBUG1);
	} while (record != NULL);

	record = XLogReadRecord(&LastRec, false, ERROR);
	*eofRecPtr = EndRecPtr;

	XLogCloseReadRecord();

	return status;
}

/*
 *
 */
static
int XLogReconcileEofInternal(
					XLogRecPtr	startLocation,
					XLogRecPtr	endLocation)
{

	uint32		startLogId;
	uint32		startSeg;

	uint32		endLogId;
	uint32		endSeg;

	uint32		logId;
	uint32		seg;

	uint32		startOffset;
	uint32		endOffset;

	int			status = STATUS_OK;

	Assert(XLByteLT(startLocation, endLocation));

	XLByteToSeg(startLocation, startLogId, startSeg);
	XLByteToSeg(endLocation, endLogId, endSeg);

	logId = startLogId;
	seg = startSeg;

	while (1) {

		if (logId == startLogId && seg == startSeg)
			startOffset = startLocation.xrecoff % XLogSegSize;
		else
			startOffset = 0;

		if (logId == endLogId && seg == endSeg)
			endOffset = endLocation.xrecoff % XLogSegSize;
		else
			endOffset = XLogSegSize;

		{
			char	tmpBuf[FILEREP_MAX_LOG_DESCRIPTION_LEN];

			snprintf(tmpBuf, sizeof(tmpBuf),
					 "xlog reconcile log id '%u' seg '%u' start offset '%d' end offset '%d' xlog size '%d' ",
					 logId, seg, startOffset, endOffset, XLogSegSize);

			FileRep_InsertConfigLogEntry(tmpBuf);
		}

		status = XLogFillZero(logId, seg, startOffset, endOffset);
		if (status != STATUS_OK)
		{
			FileRep_SetSegmentState(SegmentStateFault, FaultTypeIO);

			break;
		}

		if (logId == endLogId && seg == endSeg)
			break;

		NextLogSeg(logId, seg);
	}

	return STATUS_OK;
}

static
int XLogFillZero(
				 uint32	logId,
				 uint32	seg,
				 uint32	startOffset,
				 uint32	endOffset)
{
	char		path[MAXPGPATH];
	char		fname[MAXPGPATH];
	char		zbuffer[XLOG_BLCKSZ];

	int			fd = 0;
	uint32		offset = startOffset;
	Size		writeLen = 0;

	int			status = STATUS_OK;
	char		*xlogDir = NULL;

	Assert(startOffset < endOffset);

	errno = 0;

	XLogFileName(fname, ThisTimeLineID, logId, seg);

	xlogDir = makeRelativeToTxnFilespace(XLOGDIR);
	if (snprintf(path, MAXPGPATH, "%s/%s", xlogDir, fname) >= MAXPGPATH) {
		ereport(WARNING,
				(errcode_for_file_access(),
				 errmsg("could not allocate path, path too long \"%s/%s\"",
						xlogDir, fname)));
		return STATUS_ERROR;
	}
	pfree(xlogDir);

	fd = open(path, O_RDWR, 0);
	if (fd < 0) {
			ereport(WARNING,
					(errcode_for_file_access(),
					 errmsg("could not open xlog file \"%s\" : %m",
							path)));
			return STATUS_ERROR;
	}

	if (ftruncate(fd, startOffset) < 0) {
		ereport(WARNING,
				(errcode_for_file_access(),
				 errmsg("could not truncate xlog file \"%s\" to position \"%d\" : %m",
						path, startOffset)));
		status = STATUS_ERROR;
		goto exit;
	}

	if (lseek(fd, (off_t) startOffset, SEEK_SET) < 0) {
		ereport(WARNING,
				(errcode_for_file_access(),
				 errmsg("could not seek xlog file \"%s\" to position \"%d\" : %m",
						path, startOffset)));
		status = STATUS_ERROR;
		goto exit;
	}

	/*
	 * Zero-fill the file.	We have to do this the hard way to ensure that all
	 * the file space has really been allocated --- on platforms that allow
	 * "holes" in files, just seeking to the end doesn't allocate intermediate
	 * space.  This way, we know that we have all the space and (after the
	 * fsync below) that all the indirect blocks are down on disk.	Therefore,
	 * fdatasync(2) or O_DSYNC will be sufficient to sync future writes to the
	 * log file.
	 */
	MemSet(zbuffer, 0, sizeof(zbuffer));

	while (1) {
		errno = 0;
		writeLen = (Size) Min(XLOG_BLCKSZ - (offset % XLOG_BLCKSZ), endOffset - offset);

		if ((int) write(fd, zbuffer, writeLen) != (int) writeLen) {
			int			save_errno = errno;

			/*
			 * If we fail to make the file, delete it to release disk space
			 */

			/* if write didn't set errno, assume problem is no disk space */
			errno = save_errno ? save_errno : ENOSPC;

			ereport(WARNING,
					(errcode_for_file_access(),
					 errmsg("could not write to file \"%s\": %m", path)));
			status = STATUS_ERROR;
			goto exit;
		}
		offset += writeLen;
		if (offset >= endOffset) {
			break;
		}
	}

	if (pg_fsync(fd) != 0) {
		ereport(WARNING,
				(errcode_for_file_access(),
				 errmsg("could not fsync file \"%s\": %m", path)));
		status = STATUS_ERROR;
	}

exit:
	if (fd > 0) {
		if (close(fd)) {
			ereport(WARNING,
					(errcode_for_file_access(),
					 errmsg("could not close file \"%s\": %m", path)));
			status = STATUS_ERROR;
		}
	}

	return status;
}


/*
 *
 *		a) get logical XLog EOF on primary
 *		b) send logical XLog EOF to mirror
 *		c) if mirror ahead then reconcile XLog EOF on mirror
 *		d) if primary ahead then reconcile XLog EOF on primary
 */
int
XLogReconcileEofPrimary(void)
{
	XLogRecPtr	primaryEof = {0, 0};
	XLogRecPtr	mirrorEof;

	uint32		logId;
	uint32		seg;

	char		simpleFileName[MAXPGPATH];

	int			status = STATUS_OK;

	status = XLogGetEof(&primaryEof);

	if (status != STATUS_OK) {
		return status;
	}

	XLByteToSeg(primaryEof, logId, seg);

	XLogFileName(simpleFileName, ThisTimeLineID, logId, seg);

	status = MirroredFlatFile_ReconcileXLogEof(
											   XLOGDIR,
											   simpleFileName,
											   primaryEof,
											   &mirrorEof);

	if (status != STATUS_OK) {
		return status;
	}

	if (XLByteEQ(primaryEof, mirrorEof))
	{
		FileRep_InsertConfigLogEntry("primary and mirror xlog eof match");
		return STATUS_OK;
	}

	if (XLByteLT(primaryEof, mirrorEof))
	{
		FileRep_InsertConfigLogEntry("primary is behind, xlog was truncated on mirror");

		status = MirrorFlatFile(
								XLOGDIR,
								simpleFileName);
		return STATUS_OK;
	}

	FileRep_InsertConfigLogEntry("mirror is behind, xlog will be copied to mirror");

	status = MirrorFlatFile(
							XLOGDIR,
							simpleFileName);
	return status;
}

/*
 *
 */
int
XLogReconcileEofMirror(
		XLogRecPtr	primaryEof,
		XLogRecPtr	*mirrorEof)
{
	XLogRecPtr	mirrorEofLocal = {0, 0};
	int			status = STATUS_OK;

	status = XLogGetEof(&mirrorEofLocal);

	*mirrorEof = mirrorEofLocal;

	if (status != STATUS_OK) {
		return status;
	}


	if (XLByteEQ(primaryEof, mirrorEofLocal)) {
		FileRep_InsertConfigLogEntry("primary and mirror xlog eof match");
		return STATUS_OK;
	}

	if (! XLByteLT(primaryEof, mirrorEofLocal)) {
		FileRep_InsertConfigLogEntry("mirror is behind, xlog will be truncated on primary");
		return STATUS_OK;
	}

	FileRep_InsertConfigLogEntry("primary is behind, xlog was truncated on mirror");

	status = XLogReconcileEofInternal(
						  primaryEof,
						  mirrorEofLocal);

	if (status != STATUS_OK) {
		return status;
	}

	return status;
}

/*
 * The routine recovers pg_control flat file on mirror side.
 *		a) It copies pg_control file from primary to mirror
 *      b) pg_control file is overwritten on mirror
 *
 * Status is not returned, If an error occurs segmentState will be set to Fault.
 */
int
XLogRecoverMirrorControlFile(void)
{
	MirroredFlatFileOpen	mirroredOpen;
	int						retval = 0;

	while (1) {

		ReadControlFile();

		retval = MirroredFlatFile_Open(
							  &mirroredOpen,
							  XLOG_CONTROL_FILE_SUBDIR,
							  XLOG_CONTROL_FILE_SIMPLE,
							  O_CREAT | O_RDWR | PG_BINARY,
							  S_IRUSR | S_IWUSR,
							  /* suppressError */ false,
							  /* atomic operation */ false,
							  /*isMirrorRecovery */ TRUE);
		if (retval != 0)
			break;

		retval = MirroredFlatFile_Write(
							   &mirroredOpen,
							   0,
							   ControlFile,
							   PG_CONTROL_SIZE,
							   /* suppressError */ false);
		if (retval != 0)
			break;

		retval = MirroredFlatFile_Flush(
							   &mirroredOpen,
							   /* suppressError */ false);
		if (retval != 0)
			break;

		MirroredFlatFile_Close(&mirroredOpen);
		break;
	} // while(1)

	return retval;
}

/*
 * The ChangeTracking module will call this xlog routine in order for
 * it to gather all the xlog records since the last checkpoint and
 * add any relevant information to the change log if necessary.
 *
 * It returns the number of records that were found (not all of them
 * were interesting to the changetracker though).
 *
 * See ChangeTracking_CreateInitialFromPreviousCheckpoint()
 * for more information.
 */
int XLogAddRecordsToChangeTracking(
	XLogRecPtr	*lastChangeTrackingEndLoc)
{
	XLogRecord *record;
	XLogRecPtr	redoCheckpointLoc;
	CheckPoint	redoCheckpoint;
	XLogRecPtr	startLoc;
	XLogRecPtr	lastEndLoc;
	XLogRecPtr	lastChangeTrackingLogEndLoc = {0, 0};
	int count = 0;

	/*
	 * Find latest checkpoint record and the redo record from xlog. This record
	 * will be used to find the starting point to scan xlog records to be pushed
	 * to changetracking log. This is needed either to generate/produce new change
	 * tracking log or to make the changetracking log catchup with xlog in case
	 * it has fallen behind.
	 * TODO: does this function really work for us? if so, change its name for something more global
	 */
	XLogGetRecoveryStart("CHANGETRACKING",
						 "get checkpoint location",
						 &redoCheckpointLoc,
						 &redoCheckpoint);

	startLoc = redoCheckpoint.redo;

	XLogCloseReadRecord();
	elog(LOG, "last checkpoint location for generating initial changetracking log %s",
			XLogLocationToString(&startLoc));

	/*
	 * Find the last entry and thus the LSN recorded by it from the CT_FULL
	 * log. Later, it will be used to maintain the xlog and changetracking log
	 * to the same end point.
	 * We perform this when the lastChangetrackingEndLoc is not known.
	 */
	if (lastChangeTrackingEndLoc == NULL)
	{
		ChangeTracking_GetLastChangeTrackingLogEndLoc(&lastChangeTrackingLogEndLoc);
		elog(LOG, "last changetracked location in changetracking full log %s",
				XLogLocationToString(&lastChangeTrackingLogEndLoc));
	}

	record = XLogReadRecord(&startLoc, false, LOG);
	if (record == NULL)
	{
		elog(ERROR," couldn't read start location %s",
			 XLogLocationToString(&startLoc));
	}

	if (lastChangeTrackingEndLoc != NULL &&
		XLByteLT(*lastChangeTrackingEndLoc, EndRecPtr))
	{
		XLogCloseReadRecord();

		if (Debug_persistent_print)
			elog(Persistent_DebugPrintLevel(),
				 "XLogAddRecordsToChangeTracking: Returning 0 records through end location %s",
				 XLogLocationToString(lastChangeTrackingEndLoc));

		return 0;
	}

	/*
	 * Make a pass through all xlog records from last checkpoint and
	 * gather information from the interesting ones into the change log.
	 */
	while (true)
	{
		if (Debug_persistent_print)
			elog(Persistent_DebugPrintLevel(),
				 "XLogAddRecordsToChangeTracking: Going to add change tracking record for XLOG (end) location %s",
				 XLogLocationToString(&EndRecPtr));

		ChangeTracking_AddRecordFromXlog(record->xl_rmid,
									     record->xl_info,
										 (XLogRecData *)XLogRecGetData(record),
										 &EndRecPtr);
		count++;

		lastEndLoc = EndRecPtr;

#ifdef FAULT_INJECTOR
		FaultInjector_InjectFaultIfSet(
					       FileRepTransitionToChangeTracking,
					       DDLNotSpecified,
					       "",//databaseName
					       ""); // tableName
#endif

		if (filerep_inject_change_tracking_recovery_fault)
		{
			if (isDatabaseRunning() == FALSE)
			{
				filerep_inject_change_tracking_recovery_fault = FALSE;

				ereport(PANIC,
					(errmsg("change tracking failure, "
					"injected fault by guc filerep_inject_change_tracking_recovery_fault, "
					"postmaster reset requested"),
					FileRep_errcontext()));
			}
		}

		if (lastChangeTrackingEndLoc != NULL)
		{
			if (XLByteEQ(EndRecPtr, *lastChangeTrackingEndLoc))
			{
				if (Debug_persistent_print)
					elog(Persistent_DebugPrintLevel(),
						 "XLogAddRecordsToChangeTracking: Returning %d records from start location %s through end location %s",
						 count,
						 XLogLocationToString(&startLoc),
						 XLogLocationToString2(lastChangeTrackingEndLoc));
				break;
			}

			record = XLogReadRecord(NULL, false, ERROR);
			Assert (record != NULL);

			if (!XLByteLE(EndRecPtr, *lastChangeTrackingEndLoc))
			{
				if (Debug_persistent_print)
					elog(Persistent_DebugPrintLevel(),
						 "XLogAddRecordsToChangeTracking: Read beyond expected last change tracking XLOG record.  "
						 "Returning %d records. "
						 "Last change tracking XLOG record (end) position is %s; scanned XLOG record (end) position is %s (start location is %s)",
						 count,
						 XLogLocationToString(lastChangeTrackingEndLoc),
						 XLogLocationToString2(&EndRecPtr),
						 XLogLocationToString3(&startLoc));
				break;
			}
		}
		else
		{
			/*
			 * Read to end of log.
			 */
			record = XLogReadRecord(NULL, false, LOG);
			if (record == NULL)
			{
				if (Debug_persistent_print)
					elog(Persistent_DebugPrintLevel(),
						 "XLogAddRecordsToChangeTracking: Returning %d records through end of log location %s",
						 count,
						 XLogLocationToString(&lastEndLoc));

				break;
			}
		}
	}

	/*
	 * We now need to make sure that (in the case of crash recovery) there are no
	 * records in the change tracking logs that have lsn higher than the highest lsn in xlog.
	 *
	 *	a) Find the highest lsn in xlog
	 *	b) Find the highest lsn in change tracking log files before interesting
	 *	   xlog entries from last checkpoint onwards are appended to it
	 *	   (see above)
	 *	c) if the highest lsn in change tracking > the highest lsn in xlog then
	 *		i) store in compacting shared memory the highest lsn in xlog
	 *		ii) Flush all data into CT_LOG_FULL
	 *		iii) Rename CT_LOG_FULL to CT_LOG_TRANSIENT
	 *	d) after database is started the compacting (CT_LOG_TRANSIENT) will discard all records from
	 *	   change tracking log file that are higher than the highest lsn in xlog
	 */
	if (lastChangeTrackingEndLoc == NULL)
	{
		/* read xlog till the end to get last lsn on disk (EndRecPtr) */
		while(record != NULL)
			record = XLogReadRecord(NULL, false, LOG);

		if (! (lastChangeTrackingLogEndLoc.xlogid == 0 && lastChangeTrackingLogEndLoc.xrecoff == 0) &&
			XLByteLT(EndRecPtr, lastChangeTrackingLogEndLoc))
		{
			elog(LOG,
				 "changetracking: "
				 "found last changetracking log LSN (%s) higher than last xlog LSN, "
				 "invalid records will be discarded",
				 XLogLocationToString(&lastChangeTrackingLogEndLoc));

			elog(LOG, "xlog LSN (%s)", XLogLocationToString(&EndRecPtr));

			ChangeTracking_FsyncDataIntoLog(CTF_LOG_FULL);
			ChangeTrackingSetXLogEndLocation(EndRecPtr);
			ChangeTracking_CreateTransientLog();
		}
	}

	XLogCloseReadRecord();
	return count;
}

/*
 * The following two gucs
 *					a) fsync=on
 *					b) wal_sync_method=open_sync
 * open XLOG files with O_DIRECT flag.
 * O_DIRECT flag requires XLOG buffer to be 512 byte aligned.
 */
void
XLogInitMirroredAlignedBuffer(int32 bufferLen)
{
	if (bufferLen > writeBufLen)
	{
		if (writeBuf != NULL)
		{
			pfree(writeBuf);
			writeBuf = NULL;
			writeBufAligned = NULL;
			writeBufLen = 0;
		}
	}

	if (writeBuf == NULL)
	{
		writeBufLen = bufferLen;

		writeBuf = MemoryContextAlloc(TopMemoryContext, writeBufLen + ALIGNOF_XLOG_BUFFER);
		if (writeBuf == NULL)
			ereport(ERROR,
					(errcode(ERRCODE_OUT_OF_MEMORY),
					 (errmsg("could not allocate memory for mirrored aligned buffer"))));
		writeBufAligned = (char *) TYPEALIGN(ALIGNOF_XLOG_BUFFER, writeBuf);
	}
}


int
XLogRecoverMirror(void)
{
  DIR                *cldir;
  struct dirent     *clde;
  int                retval = 0;
	char            *xlogDir = makeRelativeToTxnFilespace(XLOGDIR);

  cldir = AllocateDir(xlogDir);
  while ((clde = ReadDir(cldir, xlogDir)) != NULL) {
    if (strlen(clde->d_name) == 24 &&
	strspn(clde->d_name, "0123456789ABCDEF") == 24) {

      retval = MirrorFlatFile( XLOGDIR, clde->d_name);

      if (retval != 0)
	break;

    }
  }
  FreeDir(cldir);
	pfree(xlogDir);

  return retval;
}

/*
 * Check to see whether the user-specified trigger file exists and whether a
 * promote request has arrived.  If either condition holds, request postmaster
 * to shut down walreceiver, wait for it to exit, and return true.
 */
static bool
CheckForStandbyTrigger(void)
{
	static bool triggered = false;

	if (triggered)
		return true;

	if (CheckPromoteSignal(true))
	{
		ereport(LOG,
				(errmsg("received promote request")));
		ShutdownWalRcv();
		triggered = true;
		return true;
	}

	return false;
}

/*
 * Check to see if a promote request has arrived. Should be
 * called by postmaster after receiving SIGUSR1.
 */
bool
CheckPromoteSignal(bool do_unlink)
{
	struct stat stat_buf;

	if (stat(PROMOTE_SIGNAL_FILE, &stat_buf) == 0)
	{
		/*
		 * Since we are in a signal handler, it's not safe to elog. We
		 * silently ignore any error from unlink.
		 */
		if (do_unlink)
			unlink(PROMOTE_SIGNAL_FILE);
		return true;
	}
	return false;
}

/*
 * Wake up startup process to replay newly arrived WAL, or to notice that
 * failover has been requested.
 */
void
WakeupRecovery(void)
{
	SetLatch(&XLogCtl->recoveryWakeupLatch);
}

/*
 * Put the current standby master dbid in the shared memory, which will
 * be looked up from mmxlog.
 */
void
SetStandbyDbid(int16 dbid)
{
	/* use volatile pointer to prevent code rearrangement */
	volatile XLogCtlData *xlogctl = XLogCtl;

	SpinLockAcquire(&xlogctl->info_lck);
	xlogctl->standbyDbid = dbid;
	SpinLockRelease(&xlogctl->info_lck);

	/*
	 * Let postmaster know we've changed standby dbid.
	 */
	SendPostmasterSignal(PMSIGNAL_SEGCONFIG_CHANGE);
}

/*
 * Returns current standby dbid.
 */
int16
GetStandbyDbid(void)
{
	/* use volatile pointer to prevent code rearrangement */
	volatile XLogCtlData *xlogctl = XLogCtl;
	int16	dbid;

	SpinLockAcquire(&xlogctl->info_lck);
	dbid = xlogctl->standbyDbid;
	SpinLockRelease(&xlogctl->info_lck);

	return dbid;
}

/*
 * True if we are running standby-mode continuous recovery.
 * Note this would return false after finishing the recovery, even if
 * we are still on standby master with a primary master running.
 * Also this only works in the startup process as the StandbyMode
 * flag is not in shared memory.
 */
bool
IsStandbyMode(void)
{
	return StandbyMode;
}
