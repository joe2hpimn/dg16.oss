/*-------------------------------------------------------------------------
 *
 * indexam.c
 *	  general index access method routines
 *
 * Portions Copyright (c) 1996-2009, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/access/index/indexam.c,v 1.95 2006/10/04 00:29:48 momjian Exp $
 *
 * INTERFACE ROUTINES
 *		index_open		- open an index relation by relation OID
 *		index_close		- close an index relation
 *		index_beginscan - start a scan of an index with amgettuple
 *		index_beginscan_multi - start a scan of an index with amgetmulti
 *		index_rescan	- restart a scan of an index
 *		index_endscan	- end a scan
 *		index_insert	- insert an index tuple into a relation
 *		index_markpos	- mark a scan position
 *		index_restrpos	- restore a scan position
 *		index_getnext	- get the next tuple from a scan
 *		index_getmulti	- get the next bitmap from a scan
 *		index_bulk_delete	- bulk deletion of index tuples
 *		index_vacuum_cleanup	- post-deletion cleanup of an index
 *		index_getprocid - get a support procedure OID
 *		index_getprocinfo - get a support procedure's lookup info
 *
 * NOTES
 *		This file contains the index_ routines which used
 *		to be a scattered collection of stuff in access/genam.
 *
 *
 * old comments
 *		Scans are implemented as follows:
 *
 *		`0' represents an invalid item pointer.
 *		`-' represents an unknown item pointer.
 *		`X' represents a known item pointers.
 *		`+' represents known or invalid item pointers.
 *		`*' represents any item pointers.
 *
 *		State is represented by a triple of these symbols in the order of
 *		previous, current, next.  Note that the case of reverse scans works
 *		identically.
 *
 *				State	Result
 *		(1)		+ + -	+ 0 0			(if the next item pointer is invalid)
 *		(2)				+ X -			(otherwise)
 *		(3)		* 0 0	* 0 0			(no change)
 *		(4)		+ X 0	X 0 0			(shift)
 *		(5)		* + X	+ X -			(shift, add unknown)
 *
 *		All other states cannot occur.
 *
 *		Note: It would be possible to cache the status of the previous and
 *			  next item pointer using the flags.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "miscadmin.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/relscan.h"
#include "access/transam.h"
#include "pgstat.h"
#include "storage/bufmgr.h"
#include "storage/lmgr.h"
#include "utils/relcache.h"
#include "cdb/cdbfilerepprimary.h"


/* ----------------------------------------------------------------
 *					macros used in index_ routines
 * ----------------------------------------------------------------
 */
#define RELATION_CHECKS \
( \
	AssertMacro(RelationIsValid(indexRelation)), \
	AssertMacro(PointerIsValid(indexRelation->rd_am)) \
)

#define SCAN_CHECKS \
( \
	AssertMacro(IndexScanIsValid(scan)), \
	AssertMacro(RelationIsValid(scan->indexRelation)), \
	AssertMacro(PointerIsValid(scan->indexRelation->rd_am)) \
)

#define GET_REL_PROCEDURE(pname) \
do { \
	procedure = &indexRelation->rd_aminfo->pname; \
	if (!OidIsValid(procedure->fn_oid)) \
	{ \
		RegProcedure	procOid = indexRelation->rd_am->pname; \
		if (!RegProcedureIsValid(procOid)) \
			elog(ERROR, "invalid %s regproc", CppAsString(pname)); \
		fmgr_info_cxt(procOid, procedure, indexRelation->rd_indexcxt); \
	} \
} while(0)

#define GET_SCAN_PROCEDURE(pname) \
do { \
	procedure = &scan->indexRelation->rd_aminfo->pname; \
	if (!OidIsValid(procedure->fn_oid)) \
	{ \
		RegProcedure	procOid = scan->indexRelation->rd_am->pname; \
		if (!RegProcedureIsValid(procOid)) \
			elog(ERROR, "invalid %s regproc", CppAsString(pname)); \
		fmgr_info_cxt(procOid, procedure, scan->indexRelation->rd_indexcxt); \
	} \
} while(0)

static IndexScanDesc index_beginscan_internal(Relation indexRelation,
						 int nkeys, ScanKey key);


/* ----------------------------------------------------------------
 *				   index_ interface functions
 * ----------------------------------------------------------------
 */

/* ----------------
 *		index_open - open an index relation by relation OID
 *
 *		If lockmode is not "NoLock", the specified kind of lock is
 *		obtained on the index.	(Generally, NoLock should only be
 *		used if the caller knows it has some appropriate lock on the
 *		index already.)
 *
 *		An error is raised if the index does not exist.
 *
 *		This is a convenience routine adapted for indexscan use.
 *		Some callers may prefer to use relation_open directly.
 * ----------------
 */
Relation
index_open(Oid relationId, LOCKMODE lockmode)
{
	Relation	r;

	r = relation_open(relationId, lockmode);

	if (r->rd_rel->relkind != RELKIND_INDEX)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not an index",
						RelationGetRelationName(r))));

	return r;
}

/* ----------------
 *		index_close - close an index relation
 *
 *		If lockmode is not "NoLock", we then release the specified lock.
 *
 *		Note that it is often sensible to hold a lock beyond index_close;
 *		in that case, the lock is released automatically at xact end.
 * ----------------
 */
void
index_close(Relation relation, LOCKMODE lockmode)
{
	LockRelId	relid = relation->rd_lockInfo.lockRelId;

	Assert(lockmode >= NoLock && lockmode < MAX_LOCKMODES);

	/* The relcache does the real work... */
	RelationClose(relation);

	if (lockmode != NoLock)
		UnlockRelationId(&relid, lockmode);
}

/* ----------------
 *		index_insert - insert an index tuple into a relation
 * ----------------
 */
bool
index_insert(Relation indexRelation,
			 Datum *values,
			 bool *isnull,
			 ItemPointer heap_t_ctid,
			 Relation heapRelation,
			 bool check_uniqueness)
{
	MIRROREDLOCK_BUFMGR_VERIFY_NO_LOCK_LEAK_DECLARE;

	FmgrInfo   *procedure;

	bool result;

	MIRROREDLOCK_BUFMGR_VERIFY_NO_LOCK_LEAK_ENTER;

	RELATION_CHECKS;
	GET_REL_PROCEDURE(aminsert);

	// Fetch gp_persistent_relation_node information that will be added to XLOG record.
	RelationFetchGpRelationNodeForXLog(indexRelation);

	/*
	 * have the am's insert proc do all the work.
	 */
	result = DatumGetBool(FunctionCall6(procedure,
									  PointerGetDatum(indexRelation),
									  PointerGetDatum(values),
									  PointerGetDatum(isnull),
									  PointerGetDatum(heap_t_ctid),
									  PointerGetDatum(heapRelation),
									  BoolGetDatum(check_uniqueness)));

	MIRROREDLOCK_BUFMGR_VERIFY_NO_LOCK_LEAK_EXIT;

	return result;
}

/*
 * index_beginscan - start a scan of an index with amgettuple
 *
 * Caller must be holding suitable locks on the heap and the index.
 *
 * Note: heapRelation may be NULL if there is no intention of calling
 * index_getnext on this scan; index_getnext_indexitem will not use the
 * heapRelation link (nor the snapshot).  However, the caller had better
 * be holding some kind of lock on the heap relation in any case, to ensure
 * no one deletes it (or the index) out from under us.	Caller must also
 * be holding a lock on the index.
 */
IndexScanDesc
index_beginscan(Relation heapRelation,
				Relation indexRelation,
				Snapshot snapshot,
				int nkeys, ScanKey key)
{
	return index_beginscan_generic(heapRelation, indexRelation,
			snapshot, nkeys, key, false /* isMultiscan */);
}

/*
 * index_beginscan_generic - start a scan of an index
 * 							with amgetmulti
 *
 * As above, caller had better be holding some lock on the parent heap
 * relation, even though it's not explicitly mentioned here.
 */
IndexScanDesc
index_beginscan_generic(Relation heapRelation,
						Relation indexRelation,
						Snapshot snapshot,
						int nkeys, ScanKey key, bool isMultiscan)
{
	MIRROREDLOCK_BUFMGR_VERIFY_NO_LOCK_LEAK_DECLARE;

	IndexScanDesc scan;

	MIRROREDLOCK_BUFMGR_VERIFY_NO_LOCK_LEAK_ENTER;

	scan = index_beginscan_internal(indexRelation, nkeys, key);

	/*
	 * Save additional parameters into the scandesc.  Everything else was
	 * set up by RelationGetIndexScan.
	 */
	scan->is_multiscan = isMultiscan;
	scan->heapRelation = heapRelation;
	scan->xs_snapshot = snapshot;

	MIRROREDLOCK_BUFMGR_VERIFY_NO_LOCK_LEAK_EXIT;

	return scan;
}


/*
 * index_beginscan_internal --- common code for index_beginscan variants
 */
static IndexScanDesc
index_beginscan_internal(Relation indexRelation,
						 int nkeys, ScanKey key)
{
	IndexScanDesc scan;
	FmgrInfo   *procedure;

	RELATION_CHECKS;
	GET_REL_PROCEDURE(ambeginscan);

	/*
	 * We hold a reference count to the relcache entry throughout the scan.
	 */
	RelationIncrementReferenceCount(indexRelation);

	/*
	 * Tell the AM to open a scan.
	 */
	scan = (IndexScanDesc)
		DatumGetPointer(FunctionCall3(procedure,
									  PointerGetDatum(indexRelation),
									  Int32GetDatum(nkeys),
									  PointerGetDatum(key)));

	return scan;
}

/* ----------------
 *		index_rescan  - (re)start a scan of an index
 *
 * The caller may specify a new set of scankeys (but the number of keys
 * cannot change).	To restart the scan without changing keys, pass NULL
 * for the key array.
 *
 * Note that this is also called when first starting an indexscan;
 * see RelationGetIndexScan.  Keys *must* be passed in that case,
 * unless scan->numberOfKeys is zero.
 * ----------------
 */
void
index_rescan(IndexScanDesc scan, ScanKey key)
{
	FmgrInfo   *procedure;

	SCAN_CHECKS;
	GET_SCAN_PROCEDURE(amrescan);

	/* Release any held pin on a heap page */
	if (BufferIsValid(scan->xs_cbuf))
	{
		ReleaseBuffer(scan->xs_cbuf);
		scan->xs_cbuf = InvalidBuffer;
	}

	scan->kill_prior_tuple = false;		/* for safety */

	FunctionCall2(procedure,
				  PointerGetDatum(scan),
				  PointerGetDatum(key));
}

/* ----------------
 *		index_endscan - end a scan
 * ----------------
 */
void
index_endscan(IndexScanDesc scan)
{
	FmgrInfo   *procedure;

	SCAN_CHECKS;
	GET_SCAN_PROCEDURE(amendscan);

	/* Release any held pin on a heap page */
	if (BufferIsValid(scan->xs_cbuf))
	{
		ReleaseBuffer(scan->xs_cbuf);
		scan->xs_cbuf = InvalidBuffer;
	}

	/* End the AM's scan */
	FunctionCall1(procedure, PointerGetDatum(scan));

	/* Release index refcount acquired by index_beginscan */
	RelationDecrementReferenceCount(scan->indexRelation);

	/* Release the scan data structure itself */
	IndexScanEnd(scan);
}

/* ----------------
 *		index_markpos  - mark a scan position
 * ----------------
 */
void
index_markpos(IndexScanDesc scan)
{
	FmgrInfo   *procedure;

	SCAN_CHECKS;
	GET_SCAN_PROCEDURE(ammarkpos);

	FunctionCall1(procedure, PointerGetDatum(scan));
}

/* ----------------
 *		index_restrpos	- restore a scan position
 *
 * NOTE: this only restores the internal scan state of the index AM.
 * The current result tuple (scan->xs_ctup) doesn't change.  See comments
 * for ExecRestrPos().
 * ----------------
 */
void
index_restrpos(IndexScanDesc scan)
{
	FmgrInfo   *procedure;

	SCAN_CHECKS;
	GET_SCAN_PROCEDURE(amrestrpos);

	scan->kill_prior_tuple = false;		/* for safety */

	FunctionCall1(procedure, PointerGetDatum(scan));
}

/* ----------------
 *		index_getnext - get the next heap tuple from a scan
 *
 * The result is the next heap tuple satisfying the scan keys and the
 * snapshot, or NULL if no more matching tuples exist.	On success,
 * the buffer containing the heap tuple is pinned (the pin will be dropped
 * at the next index_getnext or index_endscan).
 * ----------------
 */
HeapTuple
index_getnext(IndexScanDesc scan, ScanDirection direction)
{
	MIRROREDLOCK_BUFMGR_DECLARE;

	HeapTuple	heapTuple = &scan->xs_ctup;
	FmgrInfo   *procedure;

	SCAN_CHECKS;
	GET_SCAN_PROCEDURE(amgettuple);

	/* just make sure this is false... */
	scan->kill_prior_tuple = false;

	for (;;)
	{
		bool		found;

		/*
		 * The AM's gettuple proc finds the next tuple matching the scan keys.
		 */
		found = DatumGetBool(FunctionCall2(procedure,
										   PointerGetDatum(scan),
										   Int32GetDatum(direction)));

		/* Reset kill flag immediately for safety */
		scan->kill_prior_tuple = false;

		if (!found)
		{
			/* Release any held pin on a heap page */
			if (BufferIsValid(scan->xs_cbuf))
			{
				ReleaseBuffer(scan->xs_cbuf);
				scan->xs_cbuf = InvalidBuffer;
			}
			return NULL;		/* failure exit */
		}

		pgstat_count_index_tuples(scan->indexRelation, 1);

		/*
		 * Fetch the heap tuple and see if it matches the snapshot.
		 */
		if (heap_release_fetch(scan->heapRelation, scan->xs_snapshot,
							   heapTuple, &scan->xs_cbuf, true,
							   scan->indexRelation))
			break;

		/* Skip if no undeleted tuple at this location */
		if (heapTuple->t_data == NULL)
			continue;

		/*
		 * If we can't see it, maybe no one else can either.  Check to see if
		 * the tuple is dead to all transactions.  If so, signal the index AM
		 * to not return it on future indexscans.
		 *
		 * We told heap_release_fetch to keep a pin on the buffer, so we can
		 * re-access the tuple here.  But we must re-lock the buffer first.
		 */

		// -------- MirroredLock ----------
		MIRROREDLOCK_BUFMGR_LOCK;

		LockBuffer(scan->xs_cbuf, BUFFER_LOCK_SHARE);

		if (HeapTupleSatisfiesVacuum(heapTuple->t_data, RecentGlobalXmin,
									 scan->xs_cbuf, true) == HEAPTUPLE_DEAD)
			scan->kill_prior_tuple = true;

		LockBuffer(scan->xs_cbuf, BUFFER_LOCK_UNLOCK);
		
		MIRROREDLOCK_BUFMGR_UNLOCK;
		// -------- MirroredLock ----------
		
	}

	/* Success exit */
	return heapTuple;
}

/* ----------------
 *		index_getnext_indexitem - get the next index tuple from a scan
 *
 * Finds the next index tuple satisfying the scan keys.  Note that the
 * corresponding heap tuple is not accessed, and thus no time qual (snapshot)
 * check is done, other than the index AM's internal check for killed tuples
 * (which most callers of this routine will probably want to suppress by
 * setting scan->ignore_killed_tuples = false).
 *
 * On success (TRUE return), the heap TID of the found index entry is in
 * scan->xs_ctup.t_self.  scan->xs_cbuf is untouched.
 * ----------------
 */
bool
index_getnext_indexitem(IndexScanDesc scan,
						ScanDirection direction)
{
	FmgrInfo   *procedure;
	bool		found;

	SCAN_CHECKS;
	GET_SCAN_PROCEDURE(amgettuple);

	/* just make sure this is false... */
	scan->kill_prior_tuple = false;

	/*
	 * have the am's gettuple proc do all the work.
	 */
	found = DatumGetBool(FunctionCall2(procedure,
									   PointerGetDatum(scan),
									   Int32GetDatum(direction)));

	pgstat_count_index_tuples(scan->indexRelation, 1 /*ntids*/);

	return found;
}

/*
 *		index_getmulti - get the next bitmap from an index scan.
 */
Node *
index_getmulti(IndexScanDesc scan, Node *bitmap)
{
	FmgrInfo   *procedure;
	Node		*bm;

	SCAN_CHECKS;
	GET_SCAN_PROCEDURE(amgetmulti);

	/* just make sure this is false... */
	scan->kill_prior_tuple = false;

	/*
	 * have the am's getmulti proc do all the work.
	 * index_beginscan_multi already set up fn_getmulti.
	 */
	bm = (Node *)DatumGetPointer(FunctionCall2(procedure,
									  PointerGetDatum(scan),
									  PointerGetDatum(bitmap)));

	return bm;
}

/* ----------------
 *		index_bulk_delete - do mass deletion of index entries
 *
 *		callback routine tells whether a given main-heap tuple is
 *		to be deleted
 *
 *		return value is an optional palloc'd struct of statistics
 * ----------------
 */
IndexBulkDeleteResult *
index_bulk_delete(IndexVacuumInfo *info,
				  IndexBulkDeleteResult *stats,
				  IndexBulkDeleteCallback callback,
				  void *callback_state)
{
	Relation	indexRelation = info->index;
	FmgrInfo   *procedure;
	IndexBulkDeleteResult *result;

	RELATION_CHECKS;
	GET_REL_PROCEDURE(ambulkdelete);

	result = (IndexBulkDeleteResult *)
		DatumGetPointer(FunctionCall4(procedure,
									  PointerGetDatum(info),
									  PointerGetDatum(stats),
									  PointerGetDatum((Pointer) callback),
									  PointerGetDatum(callback_state)));

	return result;
}

/* ----------------
 *		index_vacuum_cleanup - do post-deletion cleanup of an index
 *
 *		return value is an optional palloc'd struct of statistics
 * ----------------
 */
IndexBulkDeleteResult *
index_vacuum_cleanup(IndexVacuumInfo *info,
					 IndexBulkDeleteResult *stats)
{
	Relation	indexRelation = info->index;
	FmgrInfo   *procedure;
	IndexBulkDeleteResult *result;

	RELATION_CHECKS;
	GET_REL_PROCEDURE(amvacuumcleanup);

	result = (IndexBulkDeleteResult *)
		DatumGetPointer(FunctionCall2(procedure,
									  PointerGetDatum(info),
									  PointerGetDatum(stats)));

	return result;
}

/* ----------------
 *		index_getprocid
 *
 *		Index access methods typically require support routines that are
 *		not directly the implementation of any WHERE-clause query operator
 *		and so cannot be kept in pg_amop.  Instead, such routines are kept
 *		in pg_amproc.  These registered procedure OIDs are assigned numbers
 *		according to a convention established by the access method.
 *		The general index code doesn't know anything about the routines
 *		involved; it just builds an ordered list of them for
 *		each attribute on which an index is defined.
 *
 *		This routine returns the requested procedure OID for a particular
 *		indexed attribute.
 * ----------------
 */
RegProcedure
index_getprocid(Relation irel,
				AttrNumber attnum,
				uint16 procnum)
{
	RegProcedure *loc;
	int			nproc;
	int			procindex;

	nproc = irel->rd_am->amsupport;

	Assert(procnum > 0 && procnum <= (uint16) nproc);

	procindex = (nproc * (attnum - 1)) + (procnum - 1);

	loc = irel->rd_support;

	Assert(loc != NULL);

	return loc[procindex];
}

/* ----------------
 *		index_getprocinfo
 *
 *		This routine allows index AMs to keep fmgr lookup info for
 *		support procs in the relcache.	As above, only the "default"
 *		functions for any particular indexed attribute are cached.
 *
 * Note: the return value points into cached data that will be lost during
 * any relcache rebuild!  Therefore, either use the callinfo right away,
 * or save it only after having acquired some type of lock on the index rel.
 * ----------------
 */
FmgrInfo *
index_getprocinfo(Relation irel,
				  AttrNumber attnum,
				  uint16 procnum)
{
	FmgrInfo   *locinfo;
	int			nproc;
	int			procindex;

	nproc = irel->rd_am->amsupport;

	Assert(procnum > 0 && procnum <= (uint16) nproc);

	procindex = (nproc * (attnum - 1)) + (procnum - 1);

	locinfo = irel->rd_supportinfo;

	Assert(locinfo != NULL);

	locinfo += procindex;

	/* Initialize the lookup info if first time through */
	if (locinfo->fn_oid == InvalidOid)
	{
		RegProcedure *loc = irel->rd_support;
		RegProcedure procId;

		Assert(loc != NULL);

		procId = loc[procindex];

		/*
		 * Complain if function was not found during IndexSupportInitialize.
		 * This should not happen unless the system tables contain bogus
		 * entries for the index opclass.  (If an AM wants to allow a support
		 * function to be optional, it can use index_getprocid.)
		 */
		if (!RegProcedureIsValid(procId))
			elog(ERROR, "missing support function %d for attribute %d of index \"%s\"",
				 procnum, attnum, RelationGetRelationName(irel));

		fmgr_info_cxt(procId, locinfo, irel->rd_indexcxt);
	}

	return locinfo;
}
