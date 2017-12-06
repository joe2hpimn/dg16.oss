/*-------------------------------------------------------------------------
 *
 * execTuples.c
 *	  Routines dealing with the executor tuple tables.	These are used to
 *	  ensure that the executor frees copies of tuples (made by
 *	  ExecTargetList) properly.
 *
 *	  Routines dealing with the type information for tuples. Currently,
 *	  the type information for a tuple is an array of FormData_pg_attribute.
 *	  This information is needed by routines manipulating tuples
 *	  (getattribute, formtuple, etc.).
 *
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/executor/execTuples.c,v 1.98 2006/10/04 00:29:52 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
/*
 * INTERFACE ROUTINES
 *
 *	 TABLE CREATE/DELETE
 *		ExecCreateTupleTable	- create a new tuple table
 *		ExecDropTupleTable		- destroy a table
 *		MakeSingleTupleTableSlot - make a single-slot table
 *		ExecDropSingleTupleTableSlot - destroy same
 *
 *	 SLOT RESERVATION
 *		ExecAllocTableSlot		- find an available slot in the table
 *
 *	 SLOT ACCESSORS
 *		ExecSetSlotDescriptor	- set a slot's tuple descriptor
 *		ExecStoreTuple			- store a physical tuple in the slot
 *		ExecStoreMinimalTuple	- store a minimal physical tuple in the slot
 *		ExecClearTuple			- clear contents of a slot
 *		ExecStoreVirtualTuple	- mark slot as containing a virtual tuple
 *		ExecCopySlotTuple		- build a physical tuple from a slot
 *		ExecCopySlotMinimalTuple - build a minimal physical tuple from a slot
 *		ExecMaterializeSlot		- convert virtual to physical storage
 *		ExecCopySlot			- copy one slot's contents to another
 *
 *	 CONVENIENCE INITIALIZATION ROUTINES
 *		ExecInitResultTupleSlot    \	convenience routines to initialize
 *		ExecInitScanTupleSlot		\	the various tuple slots for nodes
 *		ExecInitExtraTupleSlot		/	which store copies of tuples.
 *		ExecInitNullTupleSlot	   /
 *
 *	 Routines that probably belong somewhere else:
 *		ExecTypeFromTL			- form a TupleDesc from a target list
 *
 *	 EXAMPLE OF HOW TABLE ROUTINES WORK
 *		Suppose we have a query such as SELECT emp.name FROM emp and we have
 *		a single SeqScan node in the query plan.
 *
 *		At ExecutorStart()
 *		----------------
 *		- InitPlan() calls ExecCreateTupleTable() to create the tuple
 *		  table which will hold tuples processed by the executor.
 *
 *		- ExecInitSeqScan() calls ExecInitScanTupleSlot() and
 *		  ExecInitResultTupleSlot() to reserve places in the tuple
 *		  table for the tuples returned by the access methods and the
 *		  tuples resulting from performing target list projections.
 *
 *		During ExecutorRun()
 *		----------------
 *		- SeqNext() calls ExecStoreTuple() to place the tuple returned
 *		  by the access methods into the scan tuple slot.
 *
 *		- ExecSeqScan() calls ExecStoreTuple() to take the result
 *		  tuple from ExecProject() and place it into the result tuple slot.
 *
 *		- ExecutePlan() calls ExecSelect(), which passes the result slot
 *		  to printtup(), which uses slot_getallattrs() to extract the
 *		  individual Datums for printing.
 *
 *		At ExecutorEnd()
 *		----------------
 *		- EndPlan() calls ExecDropTupleTable() to clean up any remaining
 *		  tuples left over from executing the query.
 *
 *		The important thing to watch in the executor code is how pointers
 *		to the slots containing tuples are passed instead of the tuples
 *		themselves.  This facilitates the communication of related information
 *		(such as whether or not a tuple should be pfreed, what buffer contains
 *		this tuple, the tuple's tuple descriptor, etc).  It also allows us
 *		to avoid physically constructing projection tuples in many cases.
 */
#include "postgres.h"

#include "funcapi.h"
#include "access/heapam.h"
#include "access/htup.h"
#include "catalog/pg_type.h"
#include "executor/executor.h"
#include "parser/parse_expr.h"
#include "parser/parsetree.h"               /* rt_fetch() */
#include "utils/lsyscache.h"
#include "utils/typcache.h"

#include "cdb/cdbvars.h"                    /* Gp_segment */

static TupleDesc ExecTypeFromTLInternal(List *targetList,
					   bool hasoid, bool skipjunk);

/* ----------------------------------------------------------------
 *				  tuple table create/delete functions
 * ----------------------------------------------------------------
 */

/* --------------------------------
 *		ExecCreateTupleTable
 *
 *		This creates a new tuple table of the specified size.
 *
 *		This should be used by InitPlan() to allocate the table.
 *		The table's address will be stored in the EState structure.
 * --------------------------------
 */
void init_slot(TupleTableSlot *slot, TupleDesc tupdesc)
{
    MemSet(slot, 0, sizeof(*slot));

	slot->type = T_TupleTableSlot;
	slot->PRIVATE_tts_flags = TTS_ISEMPTY;
	slot->tts_tupleDescriptor = tupdesc;
	slot->tts_mcxt = CurrentMemoryContext;
	slot->tts_buffer = InvalidBuffer;
}

static void cleanup_slot(TupleTableSlot *slot)
{
	ExecClearTuple(slot);

	if (slot->tts_mt_bind)
	{
		destroy_memtuple_binding(slot->tts_mt_bind);
		slot->tts_mt_bind = NULL;
	}

	if (slot->tts_tupleDescriptor)
		ReleaseTupleDesc(slot->tts_tupleDescriptor);

	if (slot->PRIVATE_tts_htup_buf)
	{
		pfree(slot->PRIVATE_tts_htup_buf);
		slot->PRIVATE_tts_htup_buf = NULL;
		slot->PRIVATE_tts_htup_buf_len = 0;
	}

	if (slot->PRIVATE_tts_mtup_buf)
	{
		pfree(slot->PRIVATE_tts_mtup_buf);
		slot->PRIVATE_tts_mtup_buf = NULL;
		slot->PRIVATE_tts_mtup_buf_len = 0;
	}

	if (slot->PRIVATE_tts_values)
	{
		pfree(slot->PRIVATE_tts_values);
		slot->PRIVATE_tts_values = NULL;
	}
	if (slot->PRIVATE_tts_isnull)
	{
		pfree(slot->PRIVATE_tts_isnull);
		slot->PRIVATE_tts_isnull = NULL;
	}
}

TupleTable
ExecCreateTupleTable(int tableSize)
{
	TupleTable	newtable;
	int			i;

	/*
	 * sanity checks
	 */
	Assert(tableSize >= 1);

	/*
	 * allocate the table itself
	 */
	newtable = (TupleTable) palloc(sizeof(TupleTableData) +
								   (tableSize - 1) *sizeof(TupleTableSlot));
	newtable->size = tableSize;
	newtable->next = 0;

	/*
	 * initialize all the slots to empty states
	 */
	for (i = 0; i < tableSize; i++)
	{
		init_slot(&(newtable->array[i]), NULL);
	}

	return newtable;
}

/* --------------------------------
 *		ExecDropTupleTable
 *
 *		This frees the storage used by the tuple table itself
 *		and optionally frees the contents of the table also.
 *		It is expected that this routine be called by EndPlan().
 * --------------------------------
 */
void
ExecDropTupleTable(TupleTable table,	/* tuple table */
				   bool shouldFree)		/* true if we should free slot
										 * contents */
{
	/*
	 * sanity checks
	 */
	Assert(table != NULL);

	/*
	 * first free all the valid pointers in the tuple array and drop refcounts
	 * of any referenced buffers, if that's what the caller wants.  (There is
	 * probably no good reason for the caller ever not to want it!)
	 */
	if (shouldFree)
	{
		int			next = table->next;
		int			i;

		for (i = 0; i < next; i++)
		{
			cleanup_slot(&(table->array[i]));
		}
	}

	/*
	 * finally free the tuple table itself.
	 */
	pfree(table);
}

/* --------------------------------
 *		MakeSingleTupleTableSlot
 *
 *		This is a convenience routine for operations that need a
 *		standalone TupleTableSlot not gotten from the main executor
 *		tuple table.  It makes a single slot and initializes it as
 *		though by ExecSetSlotDescriptor(slot, tupdesc).
 * --------------------------------
 */
TupleTableSlot *
MakeSingleTupleTableSlot(TupleDesc tupdesc)
{
	TupleTableSlot *slot = palloc(sizeof(*slot));

	init_slot(slot, NULL);
	ExecSetSlotDescriptor(slot, tupdesc);

	return slot;
}

/* --------------------------------
 *		ExecDropSingleTupleTableSlot
 *
 *		Release a TupleTableSlot made with MakeSingleTupleTableSlot.
 * --------------------------------
 */
void
ExecDropSingleTupleTableSlot(TupleTableSlot *slot)
{
	/*
	 * sanity checks
	 */
	Assert(slot != NULL);
	cleanup_slot(slot);
	pfree(slot);
}


/* ----------------------------------------------------------------
 *				  tuple table slot reservation functions
 * ----------------------------------------------------------------
 */

/* --------------------------------
 *		ExecAllocTableSlot
 *
 *		This routine is used to reserve slots in the table for
 *		use by the various plan nodes.	It is expected to be
 *		called by the node init routines (ex: ExecInitNestLoop)
 *		once per slot needed by the node.  Not all nodes need
 *		slots (some just pass tuples around).
 * --------------------------------
 */
TupleTableSlot *
ExecAllocTableSlot(TupleTable table)
{
	int			slotnum;		/* new slot number */

	/*
	 * sanity checks
	 */
	Assert(table != NULL);

	/*
	 * We expect that the table was made big enough to begin with. We cannot
	 * reallocate it on the fly since previous plan nodes have already got
	 * pointers to individual entries.
	 */
	if (table->next >= table->size)
		elog(ERROR, "plan requires more slots than are available");

	slotnum = table->next;
	table->next++;

	return &(table->array[slotnum]);
}

/* ----------------------------------------------------------------
 *				  tuple table slot accessor functions
 * ----------------------------------------------------------------
 */

/* --------------------------------
 *		ExecSetSlotDescriptor
 *
 *		This function is used to set the tuple descriptor associated
 *		with the slot's tuple.  The passed descriptor must have lifespan
 *		at least equal to the slot's.  If it is a reference-counted descriptor
 *		then the reference count is incremented for as long as the slot holds
 *		a reference.
 * --------------------------------
 */
void
ExecSetSlotDescriptor(TupleTableSlot *slot,		/* slot to change */
					  TupleDesc tupdesc)		/* new tuple descriptor */
{
	/* For safety, make sure slot is empty before changing it */
	cleanup_slot(slot);

	/*
	 * Install the new descriptor; if it's refcounted, bump its refcount.
	 */
	slot->tts_tupleDescriptor = tupdesc;
	PinTupleDesc(tupdesc);

	{
		/*
		 * Allocate Datum/isnull arrays of the appropriate size.  These must have
		 * the same lifetime as the slot, so allocate in the slot's own context.
		 */
		MemoryContext oldcontext = MemoryContextSwitchTo(slot->tts_mcxt);

		slot->tts_mt_bind = create_memtuple_binding(tupdesc);
		slot->PRIVATE_tts_values = (Datum *) palloc(tupdesc->natts * sizeof(Datum));
		slot->PRIVATE_tts_isnull = (bool *) palloc(tupdesc->natts * sizeof(bool));

		MemoryContextSwitchTo(oldcontext);
	}
}

/* --------------------------------
 *		ExecStoreTuple
 *
 *		This function is used to store a physical tuple into a specified
 *		slot in the tuple table.
 *
 *		tuple:	tuple to store
 *		slot:	slot to store it in
 *		buffer: disk buffer if tuple is in a disk page, else InvalidBuffer
 *		shouldFree: true if ExecClearTuple should pfree() the tuple
 *					when done with it
 *
 * If 'buffer' is not InvalidBuffer, the tuple table code acquires a pin
 * on the buffer which is held until the slot is cleared, so that the tuple
 * won't go away on us.
 *
 * shouldFree is normally set 'true' for tuples constructed on-the-fly.
 * It must always be 'false' for tuples that are stored in disk pages,
 * since we don't want to try to pfree those.
 *
 * Another case where it is 'false' is when the referenced tuple is held
 * in a tuple table slot belonging to a lower-level executor Proc node.
 * In this case the lower-level slot retains ownership and responsibility
 * for eventually releasing the tuple.	When this method is used, we must
 * be certain that the upper-level Proc node will lose interest in the tuple
 * sooner than the lower-level one does!  If you're not certain, copy the
 * lower-level tuple with heap_copytuple and let the upper-level table
 * slot assume ownership of the copy!
 *
 * Return value is just the passed-in slot pointer.
 *
 * NOTE: before PostgreSQL 8.1, this function would accept a NULL tuple
 * pointer and effectively behave like ExecClearTuple (though you could
 * still specify a buffer to pin, which would be an odd combination).
 * This saved a couple lines of code in a few places, but seemed more likely
 * to mask logic errors than to be really useful, so it's now disallowed.
 * --------------------------------
 */
TupleTableSlot *
ExecStoreHeapTuple(HeapTuple tuple,
			   TupleTableSlot *slot,
			   Buffer buffer,
			   bool shouldFree)
{
	/*
	 * sanity checks
	 */
	Assert(tuple != NULL);
	Assert(slot != NULL);
	Assert(slot->tts_tupleDescriptor != NULL);
	/* passing shouldFree=true for a tuple on a disk page is not sane */
	Assert(BufferIsValid(buffer) ? (!shouldFree) : true);

	/*
	 * Actually we are storing a memtuple!
	 */
	if(is_heaptuple_memtuple(tuple))
	{
		Assert(buffer == InvalidBuffer);
		return ExecStoreMemTuple((MemTuple) tuple, slot, shouldFree);
	}

	/*
	 * Free any old physical tuple belonging to the slot.
	 */
	free_heaptuple_memtuple(slot);

	/*
	 * Store the new tuple into the specified slot.
	 */

	/* Clear tts_flags, here isempty set to false */
	slot->PRIVATE_tts_flags = shouldFree ? TTS_SHOULDFREE : 0;

	/* store the tuple */
	slot->PRIVATE_tts_heaptuple = (void *) tuple;

	/* Mark extracted state invalid */
	slot->PRIVATE_tts_nvalid = 0;

	/*
	 * If tuple is on a disk page, keep the page pinned as long as we hold a
	 * pointer into it.  We assume the caller already has such a pin.
	 *
	 * This is coded to optimize the case where the slot previously held a
	 * tuple on the same disk page: in that case releasing and re-acquiring
	 * the pin is a waste of cycles.  This is a common situation during
	 * seqscans, so it's worth troubling over.
	 */
	if (slot->tts_buffer != buffer)
	{
		if (BufferIsValid(slot->tts_buffer))
			ReleaseBuffer(slot->tts_buffer);
		slot->tts_buffer = buffer;
		if (BufferIsValid(buffer))
			IncrBufferRefCount(buffer);
	}

	return slot;
}

/* --------------------------------
 *		ExecStoreMinimalTuple
 *
 *		Like ExecStoreTuple, but insert a "minimal" tuple into the slot.
 *
 * No 'buffer' parameter since minimal tuples are never stored in relations.
 * --------------------------------
 */
TupleTableSlot *
ExecStoreMemTuple(MemTuple mtup, TupleTableSlot *slot, bool shouldFree)
{
	/*
	 * sanity checks
	 */
	Assert(mtup != NULL);
	Assert(slot != NULL);
	Assert(slot->tts_tupleDescriptor != NULL);
	Assert(slot->tts_mt_bind != NULL);

	/* Acctually we are storing a HeapTuple! */
	if(!is_heaptuple_memtuple((HeapTuple) mtup))
		return ExecStoreHeapTuple((HeapTuple) mtup, slot, InvalidBuffer, shouldFree);

	/*
	 * Free any old physical tuple belonging to the slot.
	 */
	free_heaptuple_memtuple(slot);
	
	/*
	 * Store the new tuple into the specified slot.
	 */

	/* Clear tts_flags, here isempty set to false */
	if(shouldFree)
		TupSetShouldFree(slot);
	else
		TupClearShouldFree(slot);

	slot->PRIVATE_tts_memtuple = mtup;

	TupClearIsEmpty(slot);
	slot->PRIVATE_tts_nvalid = 0;

	/*
	 * Drop the pin on the referenced buffer, if there is one.
	 */
	if (BufferIsValid(slot->tts_buffer))
		ReleaseBuffer(slot->tts_buffer);

	slot->tts_buffer = InvalidBuffer;
	return slot;
}

/* --------------------------------
 *		ExecFetchSlotTupleDatum
 *			Fetch the slot's tuple as a composite-type Datum.
 *
 *		We convert the slot's contents to local physical-tuple form,
 *		and fill in the Datum header fields.  Note that the result
 *		always points to storage owned by the slot.
 * --------------------------------
 */
Datum
ExecFetchSlotTupleDatum(TupleTableSlot *slot)
{
	HeapTuple	tup;
	HeapTupleHeader td;
	TupleDesc	tupdesc;

	/* Make sure we can scribble on the slot contents ...
	 * GPDB: ExecFetchSlotHeapTuple() replaces the Postgres call to
	 * ExecMaterializeSlot() due to restructuring of the code around
	 * memtuple support */
	tup = ExecFetchSlotHeapTuple(slot);
	/* ... and set up the composite-Datum header fields, in case not done */
	td = tup->t_data;
	tupdesc = slot->tts_tupleDescriptor;
	HeapTupleHeaderSetDatumLength(td, tup->t_len);
	HeapTupleHeaderSetTypeId(td, tupdesc->tdtypeid);
	HeapTupleHeaderSetTypMod(td, tupdesc->tdtypmod);
	return PointerGetDatum(td);
}

/* --------------------------------
 *		ExecClearTuple
 *
 *		This function is used to clear out a slot in the tuple table.
 *
 *		NB: only the tuple is cleared, not the tuple descriptor (if any).
 * --------------------------------
 */
TupleTableSlot *				/* return: slot passed */
ExecClearTuple(TupleTableSlot *slot)	/* slot in which to store tuple */
{
	/*
	 * sanity checks
	 */
	Assert(slot != NULL);

	/*
	 * Free the old physical tuple if necessary.
	 */
	free_heaptuple_memtuple(slot);

	slot->PRIVATE_tts_flags = TTS_ISEMPTY;
	slot->PRIVATE_tts_nvalid = 0;

	/*
	 * Drop the pin on the referenced buffer, if there is one.
	 */
	if (BufferIsValid(slot->tts_buffer))
		ReleaseBuffer(slot->tts_buffer);

	slot->tts_buffer = InvalidBuffer;
	return slot;
}

/* --------------------------------
 *		ExecStoreVirtualTuple
 *			Mark a slot as containing a virtual tuple.
 *
 * The protocol for loading a slot with virtual tuple data is:
 *		* Call ExecClearTuple to mark the slot empty.
 *		* Store data into the Datum/isnull arrays.
 *		* Call ExecStoreVirtualTuple to mark the slot valid.
 * This is a bit unclean but it avoids one round of data copying.
 * --------------------------------
 */
TupleTableSlot *
ExecStoreVirtualTuple(TupleTableSlot *slot)
{
	/*
	 * sanity checks
	 */
	Assert(slot != NULL);
	Assert(slot->tts_tupleDescriptor != NULL);
	
	TupClearIsEmpty(slot);
	TupSetVirtualTuple(slot);

	slot->PRIVATE_tts_nvalid = slot->tts_tupleDescriptor->natts;
	return slot;
}

/* --------------------------------
 *		ExecStoreAllNullTuple
 *			Set up the slot to contain a null in every column.
 *
 * At first glance this might sound just like ExecClearTuple, but it's
 * entirely different: the slot ends up full, not empty.
 * --------------------------------
 */
TupleTableSlot *
ExecStoreAllNullTuple(TupleTableSlot *slot)
{
	/*
	 * sanity checks
	 */
	Assert(slot != NULL);
	Assert(slot->tts_tupleDescriptor != NULL);

	/* Clear any old contents */
	free_heaptuple_memtuple(slot);

	/*
	 * Drop the pin on the referenced buffer, if there is one.
	 */
	if (BufferIsValid(slot->tts_buffer))
		ReleaseBuffer(slot->tts_buffer);
	slot->tts_buffer = InvalidBuffer;

	/*
	 * Fill all the columns of the virtual tuple with nulls
	 */
	MemSet(slot->PRIVATE_tts_isnull, true,
		   slot->tts_tupleDescriptor->natts * sizeof(bool));

	slot->PRIVATE_tts_flags = TTS_VIRTUAL;
	slot->PRIVATE_tts_nvalid = slot->tts_tupleDescriptor->natts;

	return slot;
}

/* --------------------------------
 *		ExecCopySlotTuple
 *			Obtain a copy of a slot's regular physical tuple.  The copy is
 *			palloc'd in the current memory context.
 *
 *		This works even if the slot contains a virtual or minimal tuple;
 *		however the "system columns" of the result will not be meaningful.
 * --------------------------------
 */
HeapTuple
ExecCopySlotHeapTuple(TupleTableSlot *slot)
{
	/*
	 * sanity checks
	 */
	Assert(!TupIsNull(slot));

	if(slot->PRIVATE_tts_heaptuple)
		return heap_copytuple(slot->PRIVATE_tts_heaptuple);


	slot_getallattrs(slot);

	/*
	 * Otherwise we need to build a tuple from the Datum array.
	 */
	return heap_form_tuple(slot->tts_tupleDescriptor,
						   slot_get_values(slot),
						   slot_get_isnull(slot));
}

/* --------------------------------
 *		ExecCopySlotMinimalTuple
 *			Obtain a copy of a slot's minimal physical tuple.  The copy is
 *			palloc'd in the current memory context.
 * --------------------------------
 */
MemTuple ExecCopySlotMemTuple(TupleTableSlot *slot)
{
	/*
	 * sanity checks
	 */
	Assert(!TupIsNull(slot));
	Assert(slot->tts_mt_bind);

	/*
	 * If we have a physical tuple then just copy it.
	 */
	if (slot->PRIVATE_tts_memtuple)
		return memtuple_copy_to(slot->PRIVATE_tts_memtuple, slot->tts_mt_bind, NULL, NULL);
	
	slot_getallattrs(slot);

	/*
	 * Otherwise we need to build a tuple from the Datum array.
	 */
	return memtuple_form_to(slot->tts_mt_bind, slot_get_values(slot), slot_get_isnull(slot), NULL, 0, false);
}

extern Datum toast_flatten_tuple_attribute(Datum value, Oid typeId, int32 typeMod);
MemTuple ExecCopySlotMemTupleTo(TupleTableSlot *slot, MemoryContext pctxt, char *dest, unsigned int *len)
{
	uint32 dumlen;
	MemTuple mtup = NULL;

	Assert(!TupIsNull(slot));
	Assert(slot->tts_mt_bind);

	if(!len)
		len = &dumlen;
	
	if (TupHasMemTuple(slot))
	{
		mtup = memtuple_copy_to(slot->PRIVATE_tts_memtuple, slot->tts_mt_bind, (MemTuple) dest, len);
		if(mtup || !pctxt)
			return mtup;

		mtup = (MemTuple) ctxt_alloc(pctxt, *len);
		mtup = memtuple_copy_to(slot->PRIVATE_tts_memtuple, slot->tts_mt_bind, mtup, len);
		Assert(mtup);

		return mtup;
	}

	slot_getallattrs(slot);
	mtup = memtuple_form_to(slot->tts_mt_bind, slot_get_values(slot), slot_get_isnull(slot), (MemTuple) dest, len, false);

	if(mtup || !pctxt)
		return mtup;
	mtup = (MemTuple) ctxt_alloc(pctxt, *len);
	mtup = memtuple_form_to(slot->tts_mt_bind, slot_get_values(slot), slot_get_isnull(slot), mtup, len, false);

	Assert(mtup);
	return mtup;
}
		
/* --------------------------------
 *		ExecCopySlotHeadTupleTo
 * 			Copy heapTuple to a preallocated buffer.   Code adapted from ExecCopySlotTuple
 *
 * 			return the copied heaptule if there is enough space, or, if the memorycontext is
 *              not null, which the function will alloc enough space from the context.  One can
 *				test if the tuple is alloced (ret == dest)
 *
 *			return NULL and set *len to space need if there is not enough space and the mem context is null.
 *			return NULL if heap tuple is not valid, and set *len = 0.  See slot->tts_tuple case below.
 * -------------------------------
 */
HeapTuple ExecCopySlotHeapTupleTo(TupleTableSlot *slot, MemoryContext pctxt, char* dest, unsigned int *len)
{
	uint32 dumlen;
	HeapTuple tup = NULL;

	Assert(!TupIsNull(slot));
	Assert(slot->tts_tupleDescriptor);

	if(!len)
		len = &dumlen;
	
	if (slot->PRIVATE_tts_heaptuple)
	{
		tup = heaptuple_copy_to(slot->PRIVATE_tts_heaptuple, (HeapTuple) dest, len);

		if(tup || !pctxt)
			return tup;

		tup = (HeapTuple) ctxt_alloc(pctxt, *len);
		tup = heaptuple_copy_to(slot->PRIVATE_tts_heaptuple, tup, len);
		Assert(tup);

		return tup;
	}

	slot_getallattrs(slot);
	tup = heaptuple_form_to(slot->tts_tupleDescriptor, slot_get_values(slot), slot_get_isnull(slot), (HeapTuple) dest, len);

	if(tup || !pctxt)
		return tup;
	tup = (HeapTuple) ctxt_alloc(pctxt, *len);
	tup = heaptuple_form_to(slot->tts_tupleDescriptor, slot_get_values(slot), slot_get_isnull(slot), tup, len);
	Assert(tup);
	return tup;
}
	
/* --------------------------------
 *		ExecFetchSlotTuple
 *			Fetch the slot's regular physical tuple.
 *
 *		If the slot contains a virtual tuple, we convert it to physical
 *		form.  The slot retains ownership of the physical tuple.
 *		Likewise, if it contains a minimal tuple we convert to regular form.
 *
 * The difference between this and ExecMaterializeSlot() is that this
 * does not guarantee that the contained tuple is local storage.
 * Hence, the result must be treated as read-only.
 * --------------------------------
 */
HeapTuple
ExecFetchSlotHeapTuple(TupleTableSlot *slot)
{
	uint32 tuplen;
	HeapTuple htup;

	/*
	 * sanity checks
	 */
	Assert(!TupIsNull(slot));

	/*
	 * If we have a regular physical tuple then just return it.
	 */
	if(slot->PRIVATE_tts_heaptuple)
		return slot->PRIVATE_tts_heaptuple;

	slot_getallattrs(slot);

	Assert(TupHasVirtualTuple(slot));
	Assert(slot->PRIVATE_tts_nvalid == slot->tts_tupleDescriptor->natts);

	tuplen = slot->PRIVATE_tts_htup_buf_len;
	htup = heaptuple_form_to(slot->tts_tupleDescriptor, slot_get_values(slot), slot_get_isnull(slot),
			slot->PRIVATE_tts_htup_buf, &tuplen);
	
	if(!htup)
	{
		if(slot->PRIVATE_tts_htup_buf)
			pfree(slot->PRIVATE_tts_htup_buf);
		slot->PRIVATE_tts_htup_buf = (HeapTuple) MemoryContextAlloc(slot->tts_mcxt, tuplen);
		slot->PRIVATE_tts_htup_buf_len = tuplen;

		htup = heaptuple_form_to(slot->tts_tupleDescriptor, slot_get_values(slot), slot_get_isnull(slot),
			slot->PRIVATE_tts_htup_buf, &tuplen);
		Assert(htup);
	}

	slot->PRIVATE_tts_heaptuple = htup;
	return htup;
}

/* --------------------------------
 *		ExecFetchSlotMinimalTuple
 *			Fetch the slot's minimal physical tuple.
 *
 *		If the slot contains a virtual tuple, we convert it to minimal
 *		physical form.	The slot retains ownership of the physical tuple.
 *		Likewise, if it contains a regular tuple we convert to minimal form.
 *
 * As above, the result must be treated as read-only.
 * --------------------------------
 */
MemTuple ExecFetchSlotMemTuple(TupleTableSlot *slot, bool inline_toast)
{
	MemTuple newTuple;
	MemTuple oldTuple = NULL;
	uint32 tuplen;

	Assert(!TupIsNull(slot));
	Assert(slot->tts_mt_bind);

	if(slot->PRIVATE_tts_memtuple)
	{
		if(!inline_toast || !memtuple_get_hasext(slot->PRIVATE_tts_memtuple, slot->tts_mt_bind))
			return slot->PRIVATE_tts_memtuple;

		oldTuple = slot->PRIVATE_tts_mtup_buf;
		slot->PRIVATE_tts_mtup_buf = NULL;
		slot->PRIVATE_tts_mtup_buf_len = 0;
	}

	slot_getallattrs(slot);

	tuplen = slot->PRIVATE_tts_mtup_buf_len;
	newTuple = memtuple_form_to(slot->tts_mt_bind, slot_get_values(slot), slot_get_isnull(slot),
			(MemTuple) slot->PRIVATE_tts_mtup_buf, &tuplen, inline_toast);

	if(!newTuple)
	{
		if(slot->PRIVATE_tts_mtup_buf)
			pfree(slot->PRIVATE_tts_mtup_buf);

		slot->PRIVATE_tts_mtup_buf = MemoryContextAlloc(slot->tts_mcxt, tuplen);
		slot->PRIVATE_tts_mtup_buf_len = tuplen;

		newTuple = memtuple_form_to(slot->tts_mt_bind, slot_get_values(slot), slot_get_isnull(slot),
			(MemTuple) slot->PRIVATE_tts_mtup_buf, &tuplen, inline_toast);
	}

	Assert(newTuple);
	slot->PRIVATE_tts_memtuple = newTuple;

	if(oldTuple)
		pfree(oldTuple);

	return newTuple;
}

/* --------------------------------
 *		ExecCopySlot
 *			Copy the source slot's contents into the destination slot.
 *
 *		The destination acquires a private copy that will not go away
 *		if the source is cleared.
 *
 *		The caller must ensure the slots have compatible tupdescs.
 * --------------------------------
 */
TupleTableSlot *
ExecCopySlot(TupleTableSlot *dstslot, TupleTableSlot *srcslot)
{
	Assert(!TupIsNull(srcslot));

	ExecClearTuple(dstslot);
	TupClearIsEmpty(dstslot);

	/* heap tuple stuff */
	if(srcslot->PRIVATE_tts_heaptuple && !srcslot->PRIVATE_tts_memtuple) {

		uint32 tuplen = dstslot->PRIVATE_tts_htup_buf_len;
		HeapTuple htup = heaptuple_copy_to(srcslot->PRIVATE_tts_heaptuple, dstslot->PRIVATE_tts_htup_buf, &tuplen);

		if(!htup)
		{
			dstslot->PRIVATE_tts_htup_buf = MemoryContextAlloc(dstslot->tts_mcxt, tuplen);
			dstslot->PRIVATE_tts_htup_buf_len = tuplen;

			htup = heaptuple_copy_to(srcslot->PRIVATE_tts_heaptuple, dstslot->PRIVATE_tts_htup_buf, &tuplen);
		}

		Assert(htup);
		dstslot->PRIVATE_tts_heaptuple = htup;
		dstslot->PRIVATE_tts_nvalid = 0;
	}
	else
	{
		uint32 tuplen = dstslot->PRIVATE_tts_mtup_buf_len;
		MemTuple mtup;

		Assert(srcslot->tts_mt_bind != NULL && dstslot->tts_mt_bind != NULL);

		mtup = ExecCopySlotMemTupleTo(srcslot, NULL, dstslot->PRIVATE_tts_mtup_buf, &tuplen);
		if(!mtup)
		{
			dstslot->PRIVATE_tts_mtup_buf = MemoryContextAlloc(dstslot->tts_mcxt, tuplen);
			dstslot->PRIVATE_tts_mtup_buf_len = tuplen;

			mtup = ExecCopySlotMemTupleTo(srcslot, NULL, dstslot->PRIVATE_tts_mtup_buf, &tuplen);
		}

		Assert(mtup);
		dstslot->PRIVATE_tts_memtuple = mtup;
		dstslot->PRIVATE_tts_nvalid = 0;
	}

	return dstslot;
}

/* XXX
 * This function is not very efficient.  We should detech if we can modify
 * the memtuple inline so no deform/form is needed
 */
void ExecModifyMemTuple(TupleTableSlot *slot, Datum *values, bool *isnull, bool *doRepl)
{
	int i;
	MemTuple mtup;
	uint32 tuplen;
	Assert(slot->PRIVATE_tts_memtuple);

	/* First, get all the attrs.  Note we set PRIVATE_tts_nvalid to 0
	 * so we force the attrs are from memtuple
	 */
	slot->PRIVATE_tts_nvalid = 0;
	slot_getallattrs(slot);
	
	/* Next, we construct a new memtuple, on the htup buf to avoid palloc */
	slot->PRIVATE_tts_heaptuple = NULL;
	for(i = 0; i<slot->tts_tupleDescriptor->natts; ++i)
	{
		if(doRepl[i])
		{
			slot->PRIVATE_tts_values[i] = values[i];
			slot->PRIVATE_tts_isnull[i] = isnull[i];
		}
	}

	tuplen = slot->PRIVATE_tts_htup_buf_len;
	mtup = memtuple_form_to(slot->tts_mt_bind, slot->PRIVATE_tts_values, slot->PRIVATE_tts_isnull,
			slot->PRIVATE_tts_htup_buf, &tuplen, false);
	if(!mtup)
	{
		slot->PRIVATE_tts_htup_buf = MemoryContextAlloc(slot->tts_mcxt, tuplen);
		slot->PRIVATE_tts_htup_buf_len = tuplen;

		mtup = memtuple_form_to(slot->tts_mt_bind, slot->PRIVATE_tts_values, slot->PRIVATE_tts_isnull,
			slot->PRIVATE_tts_htup_buf, &tuplen, false);

		Assert(mtup);
	}

	/* Check if we need to free this mem tuple */
	if(TupShouldFree(slot)
			&& slot->PRIVATE_tts_memtuple
			&& slot->PRIVATE_tts_memtuple != slot->PRIVATE_tts_mtup_buf
	  )
		pfree(slot->PRIVATE_tts_memtuple);

	slot->PRIVATE_tts_memtuple = mtup;
	/* swap mtup_buf and htup_buf stuff */

	mtup = (MemTuple) slot->PRIVATE_tts_mtup_buf;
	tuplen = slot->PRIVATE_tts_mtup_buf_len;

	slot->PRIVATE_tts_mtup_buf = slot->PRIVATE_tts_htup_buf;
	slot->PRIVATE_tts_mtup_buf_len = slot->PRIVATE_tts_htup_buf_len;
	slot->PRIVATE_tts_htup_buf = (void *) mtup;
	slot->PRIVATE_tts_htup_buf_len = tuplen;

	/* don't forget to reset PRIVATE_tts_nvalid, because we modified the memtuple */
	slot->PRIVATE_tts_nvalid = 0;
}


/* ----------------------------------------------------------------
 *				convenience initialization routines
 * ----------------------------------------------------------------
 */

/* --------------------------------
 *		ExecInit{Result,Scan,Extra}TupleSlot
 *
 *		These are convenience routines to initialize the specified slot
 *		in nodes inheriting the appropriate state.	ExecInitExtraTupleSlot
 *		is used for initializing special-purpose slots.
 * --------------------------------
 */

/* ----------------
 *		ExecInitResultTupleSlot
 * ----------------
 */
void
ExecInitResultTupleSlot(EState *estate, PlanState *planstate)
{
	planstate->ps_ResultTupleSlot = ExecAllocTableSlot(estate->es_tupleTable);
}

/* ----------------
 *		ExecInitScanTupleSlot
 *
 *      CDB: Only Scan operators should use this function.  Operators other
 *      than scans should use ExecInitExtraTupleSlot.
 *
 *      CDB: Some system-defined attributes are kept in the TupleTableSlot
 *      so they can be reused for each tuple.  They're initialized here.
 *      During execution, Var nodes referencing system-defined attrs can
 *      occur only in the targetlist and qual exprs attached to a scan
 *      operator.  Those exprs are evaluated taking their input from the 
 *      scan operator's Scan Tuple Slot.
 * ----------------
 */
void
ExecInitScanTupleSlot(EState *estate, ScanState *scanstate)
{
    TupleTableSlot *slot = ExecAllocTableSlot(estate->es_tupleTable);
    Scan           *scan = (Scan *)scanstate->ps.plan;
    RangeTblEntry  *rtentry;

	scanstate->ss_ScanTupleSlot = slot;

    /* CDB: Does this look like a Scan operator? */
    Insist(scan->scanrelid > 0 &&
           scan->scanrelid <= list_length(estate->es_range_table));

    /* What kind of scan? */
    rtentry = rt_fetch(scan->scanrelid, estate->es_range_table);
    switch (rtentry->rtekind)
    {
        case RTE_RELATION:
            /* Set 'tableoid' sysattr to the Oid of baserel's pg_class row. */
            slot->tts_tableOid = rtentry->relid;
            break;

        default:
            break;
    }
}

/* ----------------
 *		ExecInitExtraTupleSlot
 * ----------------
 */
TupleTableSlot *
ExecInitExtraTupleSlot(EState *estate)
{
	return ExecAllocTableSlot(estate->es_tupleTable);
}

/* ----------------
 *		ExecInitNullTupleSlot
 *
 * Build a slot containing an all-nulls tuple of the given type.
 * This is used as a substitute for an input tuple when performing an
 * outer join.
 * ----------------
 */
TupleTableSlot *
ExecInitNullTupleSlot(EState *estate, TupleDesc tupType)
{
	TupleTableSlot *slot = ExecInitExtraTupleSlot(estate);

	ExecSetSlotDescriptor(slot, tupType);

	return ExecStoreAllNullTuple(slot);
}

/* ----------------------------------------------------------------
 *		ExecTypeFromTL
 *
 *		Generate a tuple descriptor for the result tuple of a targetlist.
 *		(A parse/plan tlist must be passed, not an ExprState tlist.)
 *		Note that resjunk columns, if any, are included in the result.
 *
 *		Currently there are about 4 different places where we create
 *		TupleDescriptors.  They should all be merged, or perhaps
 *		be rewritten to call BuildDesc().
 * ----------------------------------------------------------------
 */
TupleDesc
ExecTypeFromTL(List *targetList, bool hasoid)
{
	return ExecTypeFromTLInternal(targetList, hasoid, false);
}

/* ----------------------------------------------------------------
 *		ExecCleanTypeFromTL
 *
 *		Same as above, but resjunk columns are omitted from the result.
 * ----------------------------------------------------------------
 */
TupleDesc
ExecCleanTypeFromTL(List *targetList, bool hasoid)
{
	return ExecTypeFromTLInternal(targetList, hasoid, true);
}

static TupleDesc
ExecTypeFromTLInternal(List *targetList, bool hasoid, bool skipjunk)
{
	TupleDesc	typeInfo;
	ListCell   *l;
	int			len;
	int			cur_resno = 1;

	if (skipjunk)
		len = ExecCleanTargetListLength(targetList);
	else
		len = ExecTargetListLength(targetList);
	typeInfo = CreateTemplateTupleDesc(len, hasoid);

	foreach(l, targetList)
	{
		TargetEntry *tle = lfirst(l);

		if (skipjunk && tle->resjunk)
			continue;
		TupleDescInitEntry(typeInfo,
						   cur_resno++,
						   tle->resname,
						   exprType((Node *) tle->expr),
						   exprTypmod((Node *) tle->expr),
						   0);
	}

	return typeInfo;
}

/*
 * ExecTypeFromExprList - build a tuple descriptor from a list of Exprs
 *
 * Here we must make up an arbitrary set of field names.
 */
TupleDesc
ExecTypeFromExprList(List *exprList)
{
	TupleDesc	typeInfo;
	ListCell   *l;
	int			cur_resno = 1;
	char		fldname[NAMEDATALEN];

	typeInfo = CreateTemplateTupleDesc(list_length(exprList), false);

	foreach(l, exprList)
	{
		Node	   *e = lfirst(l);

		sprintf(fldname, "f%d", cur_resno);

		TupleDescInitEntry(typeInfo,
						   cur_resno++,
						   fldname,
						   exprType(e),
						   exprTypmod(e),
						   0);
	}

	return typeInfo;
}

/*
 * BlessTupleDesc - make a completed tuple descriptor useful for SRFs
 *
 * Rowtype Datums returned by a function must contain valid type information.
 * This happens "for free" if the tupdesc came from a relcache entry, but
 * not if we have manufactured a tupdesc for a transient RECORD datatype.
 * In that case we have to notify typcache.c of the existence of the type.
 */
TupleDesc
BlessTupleDesc(TupleDesc tupdesc)
{
	if (tupdesc->tdtypeid == RECORDOID &&
		tupdesc->tdtypmod < 0)
		assign_record_type_typmod(tupdesc);

	return tupdesc;				/* just for notational convenience */
}

/*
 * TupleDescGetSlot - Initialize a slot based on the supplied tupledesc
 *
 * Note: this is obsolete; it is sufficient to call BlessTupleDesc on
 * the tupdesc.  We keep it around just for backwards compatibility with
 * existing user-written SRFs.
 */
TupleTableSlot *
TupleDescGetSlot(TupleDesc tupdesc)
{
	TupleTableSlot *slot;

	/* The useful work is here */
	BlessTupleDesc(tupdesc);

	/* Make a standalone slot */
	slot = MakeSingleTupleTableSlot(tupdesc);

	/* Return the slot */
	return slot;
}

/*
 * TupleDescGetAttInMetadata - Build an AttInMetadata structure based on the
 * supplied TupleDesc. AttInMetadata can be used in conjunction with C strings
 * to produce a properly formed tuple.
 */
AttInMetadata *
TupleDescGetAttInMetadata(TupleDesc tupdesc)
{
	int			natts = tupdesc->natts;
	int			i;
	Oid			atttypeid;
	Oid			attinfuncid;
	FmgrInfo   *attinfuncinfo;
	Oid		   *attioparams;
	int32	   *atttypmods;
	AttInMetadata *attinmeta;

	attinmeta = (AttInMetadata *) palloc(sizeof(AttInMetadata));

	/* "Bless" the tupledesc so that we can make rowtype datums with it */
	attinmeta->tupdesc = BlessTupleDesc(tupdesc);

	/*
	 * Gather info needed later to call the "in" function for each attribute
	 */
	attinfuncinfo = (FmgrInfo *) palloc0(natts * sizeof(FmgrInfo));
	attioparams = (Oid *) palloc0(natts * sizeof(Oid));
	atttypmods = (int32 *) palloc0(natts * sizeof(int32));

	for (i = 0; i < natts; i++)
	{
		/* Ignore dropped attributes */
		if (!tupdesc->attrs[i]->attisdropped)
		{
			atttypeid = tupdesc->attrs[i]->atttypid;
			getTypeInputInfo(atttypeid, &attinfuncid, &attioparams[i]);
			fmgr_info(attinfuncid, &attinfuncinfo[i]);
			atttypmods[i] = tupdesc->attrs[i]->atttypmod;
		}
	}
	attinmeta->attinfuncs = attinfuncinfo;
	attinmeta->attioparams = attioparams;
	attinmeta->atttypmods = atttypmods;

	return attinmeta;
}

/*
 * BuildTupleFromCStrings - build a HeapTuple given user data in C string form.
 * values is an array of C strings, one for each attribute of the return tuple.
 * A NULL string pointer indicates we want to create a NULL field.
 */
HeapTuple
BuildTupleFromCStrings(AttInMetadata *attinmeta, char **values)
{
	TupleDesc	tupdesc = attinmeta->tupdesc;
	int			natts = tupdesc->natts;
	Datum	   *dvalues;
	bool	   *nulls;
	int			i;
	HeapTuple	tuple;

	dvalues = (Datum *) palloc(natts * sizeof(Datum));
	nulls = (bool *) palloc(natts * sizeof(bool));

	/* Call the "in" function for each non-dropped attribute */
	for (i = 0; i < natts; i++)
	{
		if (!tupdesc->attrs[i]->attisdropped)
		{
			/* Non-dropped attributes */
			dvalues[i] = InputFunctionCall(&attinmeta->attinfuncs[i],
										   values[i],
										   attinmeta->attioparams[i],
										   attinmeta->atttypmods[i]);
			if (values[i] != NULL)
				nulls[i] = false;
			else
				nulls[i] = true;
		}
		else
		{
			/* Handle dropped attributes by setting to NULL */
			dvalues[i] = (Datum) 0;
			nulls[i] = true;
		}
	}

	/*
	 * Form a tuple
	 */
	tuple = heap_form_tuple(tupdesc, dvalues, nulls);

	/*
	 * Release locally palloc'd space.  XXX would probably be good to pfree
	 * values of pass-by-reference datums, as well.
	 */
	pfree(dvalues);
	pfree(nulls);

	return tuple;
}

/*
 * Functions for sending tuples to the frontend (or other specified destination)
 * as though it is a SELECT result. These are used by utility commands that
 * need to project directly to the destination and don't need or want full
 * Table Function capability. Currently used by EXPLAIN and SHOW ALL
 */
TupOutputState *
begin_tup_output_tupdesc(DestReceiver *dest, TupleDesc tupdesc)
{
	TupOutputState *tstate;

	tstate = (TupOutputState *) palloc(sizeof(TupOutputState));

	tstate->metadata = TupleDescGetAttInMetadata(tupdesc);
	tstate->slot = MakeSingleTupleTableSlot(tupdesc);
	tstate->dest = dest;

	(*tstate->dest->rStartup) (tstate->dest, (int) CMD_SELECT, tupdesc);

	return tstate;
}

/*
 * write a single tuple
 *
 * values is a list of the external C string representations of the values
 * to be projected.
 *
 * XXX This could be made more efficient, since in reality we probably only
 * need a virtual tuple.
 */
void
do_tup_output(TupOutputState *tstate, char **values)
{
	/* build a tuple from the input strings using the tupdesc */
	HeapTuple	tuple = BuildTupleFromCStrings(tstate->metadata, values);

	/* put it in a slot */
	ExecStoreGenericTuple(tuple, tstate->slot, true);

	/* send the tuple to the receiver */
	(*tstate->dest->receiveSlot) (tstate->slot, tstate->dest);

	/* clean up */
	ExecClearTuple(tstate->slot);
}

/*
 * write a chunk of text, breaking at newline characters
 *
 * NB: scribbles on its input!
 *
 * Should only be used with a single-TEXT-attribute tupdesc.
 */
void
do_text_output_multiline(TupOutputState *tstate, char *text)
{
	while (*text)
	{
		char	   *eol;

		eol = strchr(text, '\n');
		if (eol)
			*eol++ = '\0';
		else
			eol = text +strlen(text);

		do_tup_output(tstate, &text);
		text = eol;
	}
}

void
end_tup_output(TupOutputState *tstate)
{
	(*tstate->dest->rShutdown) (tstate->dest);
	/* note that destroying the dest is not ours to do */
	ExecDropSingleTupleTableSlot(tstate->slot);
	/* XXX worth cleaning up the attinmetadata? */
	pfree(tstate);
}

/*
 * Get a system attribute from the tuple table slot.
 */
Datum
slot_getsysattr(TupleTableSlot *slot, int attnum, bool *isnull)
{
        Datum   result = 0;
        Assert(!TupIsNull(slot));

        /* Currently, no sys attribute ever reads as NULL. */
        if (isnull)
                *isnull = false;

        /* HeapTuple */
        if (slot->PRIVATE_tts_heaptuple)
        {
                HeapTuple   htup = slot->PRIVATE_tts_heaptuple;

                Assert(htup);
                switch(attnum)
                {
                        case SelfItemPointerAttributeNumber:
							Assert(ItemPointerIsValid(&(htup->t_self)));
							
							result = PointerGetDatum(&(htup->t_self));
							break;
                        case ObjectIdAttributeNumber:
							result = ObjectIdGetDatum(HeapTupleGetOid(htup));
							break;
                        case TableOidAttributeNumber:
							result = ObjectIdGetDatum(slot->tts_tableOid);
							break;
                        default:
							result = heap_getsysattr(htup, attnum, isnull);
                }
        }

        /* MemTuple, virtual tuple */
        else
        {
			Assert(TupHasMemTuple(slot) || TupHasVirtualTuple(slot));
			
                switch(attnum)
                {
                        case SelfItemPointerAttributeNumber:
							Assert(ItemPointerIsValid(&(slot->PRIVATE_tts_synthetic_ctid)));
							result = PointerGetDatum(&(slot->PRIVATE_tts_synthetic_ctid));
							break;
                        case ObjectIdAttributeNumber:
							if(slot->PRIVATE_tts_memtuple)
								result = ObjectIdGetDatum(MemTupleGetOid(slot->PRIVATE_tts_memtuple,
																		 slot->tts_mt_bind));
							else
								result = ObjectIdGetDatum(InvalidOid);
							break;
                        case GpSegmentIdAttributeNumber:
							result = Int32GetDatum(Gp_segment);
							break;
                        case TableOidAttributeNumber:
							result = ObjectIdGetDatum(slot->tts_tableOid);
							break;
                        default:
							elog(ERROR, "Invalid attnum: %d", attnum);
                }
        }

        return result;
}                               /* slot_getsysattr */

/*
 * Set a synthetic ctid based on a fake ctid. Fake ctid is incremented before
 * the assignment.
 */
void
slot_set_ctid_from_fake(TupleTableSlot *slot, ItemPointerData *fake_ctid)
{
    exx_advance_fake_ctid(fake_ctid);
	slot_set_ctid(slot, fake_ctid);
}

void exx_advance_fake_ctid(ItemPointerData *fake_ctid)
{
	/* Uninitialized */
	if (!ItemPointerIsValid(fake_ctid))
	{
		ItemPointerSetBlockNumber(fake_ctid, 0);
		ItemPointerSetOffsetNumber(fake_ctid, FirstOffsetNumber);
	}
	else
	{
		/* would we overflow? */
		if (ItemPointerGetOffsetNumber(fake_ctid) ==
			MaxOffsetNumber - 1)
		{
			/* How can we overflow 2^46? */
			Assert(ItemPointerGetBlockNumber(fake_ctid) !=
				   MaxBlockNumber - 1);
			ItemPointerSetBlockNumber(fake_ctid,
					ItemPointerGetBlockNumber(fake_ctid) + 1);
			ItemPointerSetOffsetNumber(fake_ctid,
					FirstOffsetNumber);
		}
		else
		{
			ItemPointerSetOffsetNumber(fake_ctid,
				ItemPointerGetOffsetNumber(fake_ctid) + 1);
		}
	}
}

TupleTableSlot* exx_MakeSingleTupleTableSlot(TupleDesc tupdesc)
{
    // 
    // See MakeSingleTupleTableSlot.   The difference is that we will
    // not Pin tupdesc, which EXX has decided not to touch at all.
    //
    TupleTableSlot *slot =  (TupleTableSlot *) palloc0(sizeof(TupleTableSlot)); 

    slot->type = T_TupleTableSlot;
    slot->tts_tupleDescriptor = tupdesc;

    slot->tts_mcxt = CurrentMemoryContext;
    slot->tts_mt_bind = create_memtuple_binding(tupdesc);

    slot->PRIVATE_tts_flags = TTS_VIRTUAL; 
    slot->PRIVATE_tts_values = (Datum *) palloc(tupdesc->natts * sizeof(Datum));
    slot->PRIVATE_tts_isnull = (bool *) palloc(tupdesc->natts * sizeof(bool));

    slot->tts_buffer = InvalidBuffer;
    return slot;
}

