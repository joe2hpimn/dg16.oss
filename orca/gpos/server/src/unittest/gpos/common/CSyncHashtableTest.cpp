//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2008 Greenplum, Inc.
//
//	@filename:
//		CSyncHashtableTest.cpp
//
//	@doc:
//      Tests for CSyncHashtableTest; spliced out into a separate
//		class CSyncHashtableTest to avoid template parameter confusion for the compiler
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/base.h"

#include "gpos/common/CBitVector.h"

#include "gpos/memory/CAutoMemoryPool.h"

#include "gpos/sync/CAutoMutex.h"

#include "gpos/task/CAutoTaskProxy.h"

#include "gpos/test/CUnittest.h"

#include "unittest/gpos/common/CSyncHashtableTest.h"

using namespace gpos;

#define GPOS_SHT_SMALL_BUCKETS	5
#define GPOS_SHT_BIG_BUCKETS	100
#define GPOS_SHT_ELEMENTS	10
#define GPOS_SHT_LOOKUPS	500
#define GPOS_SHT_INITIAL_ELEMENTS	(1 + GPOS_SHT_ELEMENTS / 2)
#define GPOS_SHT_ELEMENT_DUPLICATES		5
#define GPOS_SHT_THREADS	15


// invalid key
const ULONG CSyncHashtableTest::SElem::m_ulInvalid = ULONG_MAX;

// invalid element
const CSyncHashtableTest::SElem CSyncHashtableTest::SElem::m_elemInvalid
	(
	ULONG_MAX,
	ULONG_MAX
	);

//---------------------------------------------------------------------------
//	@function:
//		CSyncHashtableTest::EresUnittest
//
//	@doc:
//		Unittest for sync'd hashtable
//
//---------------------------------------------------------------------------
GPOS_RESULT
CSyncHashtableTest::EresUnittest()
{
	CUnittest rgut[] =
		{
		GPOS_UNITTEST_FUNC(CSyncHashtableTest::EresUnittest_Basics),
		GPOS_UNITTEST_FUNC(CSyncHashtableTest::EresUnittest_Accessor),
		GPOS_UNITTEST_FUNC(CSyncHashtableTest::EresUnittest_ComplexEquality),
		GPOS_UNITTEST_FUNC(CSyncHashtableTest::EresUnittest_SameKeyIteration),
		GPOS_UNITTEST_FUNC(CSyncHashtableTest::EresUnittest_NonConcurrentIteration),
		GPOS_UNITTEST_FUNC(CSyncHashtableTest::EresUnittest_ConcurrentIteration),
		GPOS_UNITTEST_FUNC(CSyncHashtableTest::EresUnittest_Concurrency)

#ifdef GPOS_DEBUG
		,
		GPOS_UNITTEST_FUNC_ASSERT(CSyncHashtableTest::EresUnittest_AccessorDeadlock)
#endif // GPOS_DEBUG
		};

	return CUnittest::EresExecute(rgut, GPOS_ARRAY_SIZE(rgut));
}


//---------------------------------------------------------------------------
//	@function:
//		CSyncHashtableTest::EresUnittest_Basics
//
//	@doc:
//		Various list operations
//
//---------------------------------------------------------------------------
GPOS_RESULT
CSyncHashtableTest::EresUnittest_Basics()
{
	// create memory pool
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	SElem *rgelem = GPOS_NEW_ARRAY(pmp, SElem, GPOS_SHT_ELEMENTS);
	CSyncHashtable<SElem, ULONG, CSpinlockDummy> sht;
	sht.Init
		(
		pmp,
		GPOS_SHT_SMALL_BUCKETS,
		GPOS_OFFSET(SElem, m_link),
		GPOS_OFFSET(SElem, m_ulKey),
		&(SElem::m_ulInvalid),
		SElem::UlHash,
		SElem::FEqualKeys
		);

	for (ULONG i = 0; i < GPOS_SHT_ELEMENTS; i++)
	{
		rgelem[i] = SElem(i, i);
		sht.Insert(&rgelem[i]);
	}

	GPOS_DELETE_ARRAY(rgelem);

	return GPOS_OK;
}


//---------------------------------------------------------------------------
//	@function:
//		CSyncHashtableTest::EresUnittest_Accessor
//
//	@doc:
//		Various hashtable operations via accessor class
//
//---------------------------------------------------------------------------
GPOS_RESULT
CSyncHashtableTest::EresUnittest_Accessor()
{
	// create memory pool
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	SElem *rgelem = GPOS_NEW_ARRAY(pmp, SElem, GPOS_SHT_ELEMENTS);

	CSyncHashtable<SElem, ULONG, CSpinlockDummy> rgsht[2];

	rgsht[0].Init
		(
		pmp,
		GPOS_SHT_SMALL_BUCKETS,
		GPOS_OFFSET(SElem, m_link),
		GPOS_OFFSET(SElem, m_ulKey),
		&(SElem::m_ulInvalid),
		SElem::UlHash,
		SElem::FEqualKeys
		);

	rgsht[1].Init
		(
		pmp,
		GPOS_SHT_BIG_BUCKETS,
		GPOS_OFFSET(SElem, m_link),
		GPOS_OFFSET(SElem, m_ulKey),
		&(SElem::m_ulInvalid),
		SElem::UlHash,
		SElem::FEqualKeys
		);

	for (ULONG i = 0; i < GPOS_SHT_ELEMENTS; i++)
	{
		rgelem[i] = SElem(i, i);

		// distribute elements over both hashtables
		rgsht[i%2].Insert(&rgelem[i]);
	}

	for (ULONG i = 0; i < GPOS_SHT_ELEMENTS; i++)
	{
		SElem *pelem = &rgelem[i];
		ULONG ulKey = pelem->m_ulKey;

		CSyncHashtableAccessByKey
			<SElem,
			ULONG,
			CSpinlockDummy> shtacc0(rgsht[0], ulKey);

		CSyncHashtableAccessByKey
			<SElem,
			ULONG,
			CSpinlockDummy> shtacc1(rgsht[1], ulKey);

		if (NULL == shtacc0.PtLookup())
		{
			// must be in the other hashtable
			GPOS_ASSERT(pelem == shtacc1.PtLookup());

			// move to other hashtable
			shtacc1.Remove(pelem);
			shtacc0.Insert(pelem);
		}
	}

	// check that all elements have been moved over to the first hashtable
	for (ULONG i = 0; i < GPOS_SHT_ELEMENTS; i++)
	{
		SElem *pelem = &rgelem[i];
		ULONG ulKey = pelem->m_ulKey;

		CSyncHashtableAccessByKey
			<SElem,
			ULONG,
			CSpinlockDummy> shtacc0(rgsht[0], ulKey);

		CSyncHashtableAccessByKey
			<SElem,
			ULONG,
			CSpinlockDummy> shtacc1(rgsht[1], ulKey);

		GPOS_ASSERT(NULL == shtacc1.PtLookup());
		GPOS_ASSERT(pelem == shtacc0.PtLookup());
	}

	GPOS_DELETE_ARRAY(rgelem);

	return GPOS_OK;
}


//---------------------------------------------------------------------------
//	@function:
//		CSyncHashtableTest::EresUnittest_ComplexEquality
//
//	@doc:
//		Test where key is the entire object rather than a member;
//
//---------------------------------------------------------------------------
GPOS_RESULT
CSyncHashtableTest::EresUnittest_ComplexEquality()
{
	// create memory pool
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	SElem *rgelem = GPOS_NEW_ARRAY(pmp, SElem, GPOS_SHT_ELEMENTS);

	CSyncHashtable<SElem, SElem, CSpinlockDummy> sht;
	sht.Init
		(
		pmp,
		GPOS_SHT_SMALL_BUCKETS,
		GPOS_OFFSET(SElem, m_link),
		0 /*cKeyOffset*/,
		&(SElem::m_elemInvalid),
		SElem::UlHash,
		SElem::FEqual
		);

	for (ULONG i = 0; i < GPOS_SHT_ELEMENTS; i++)
	{
		rgelem[i] = SElem(GPOS_SHT_ELEMENTS + i, i);

		sht.Insert(&rgelem[i]);
	}

	for (ULONG j = 0; j < GPOS_SHT_ELEMENTS; j++)
	{
		SElem elem(GPOS_SHT_ELEMENTS + j, j);
		CSyncHashtableAccessByKey
			<SElem,
			SElem,
			CSpinlockDummy> shtacc(sht, elem);

#ifdef GPOS_DEBUG
		SElem *pelem = shtacc.PtLookup();
		GPOS_ASSERT(NULL != pelem && pelem != &elem);
		GPOS_ASSERT(pelem->UlId() == GPOS_SHT_ELEMENTS + j);
#endif // GPOS_DEBUG

	}

	GPOS_DELETE_ARRAY(rgelem);

	return GPOS_OK;
}


//---------------------------------------------------------------------------
//	@function:
//		CSyncHashtableTest::EresUnittest_SameKeyIteration
//
//	@doc:
//		Test iteration over elements with the same key
//
//---------------------------------------------------------------------------
GPOS_RESULT
CSyncHashtableTest::EresUnittest_SameKeyIteration()
{
	// create memory pool
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	const ULONG ulSize = GPOS_SHT_ELEMENTS * GPOS_SHT_ELEMENT_DUPLICATES;
	SElem *rgelem = GPOS_NEW_ARRAY(pmp, SElem, ulSize);

	SElemHashtable sht;

	sht.Init
		(
		pmp,
		GPOS_SHT_SMALL_BUCKETS,
		GPOS_OFFSET(SElem, m_link),
		GPOS_OFFSET(SElem, m_ulKey),
		&(SElem::m_ulInvalid),
		SElem::UlHash,
		SElem::FEqualKeys
		);

	// insert a mix of elements with duplicate keys
	for (ULONG j = 0; j < GPOS_SHT_ELEMENT_DUPLICATES; j++)
	{
		for (ULONG i = 0; i < GPOS_SHT_ELEMENTS; i++)
		{
			ULONG ulIndex = GPOS_SHT_ELEMENTS * j + i;
			rgelem[ulIndex] = SElem(ulIndex, i);
			sht.Insert(&rgelem[ulIndex]);
		}
	}

	// iterate over elements with the same key
	for (ULONG ulKey = 0; ulKey < GPOS_SHT_ELEMENTS; ulKey++)
	{
		SElemHashtableAccessor shtacc(sht, ulKey);

		ULONG ulCount = 0;
		SElem *pelem = shtacc.PtLookup();
		while (NULL != pelem)
		{
			ulCount++;
			pelem = shtacc.PtNext(pelem);
		}
		GPOS_ASSERT(ulCount == GPOS_SHT_ELEMENT_DUPLICATES);

	}

	GPOS_DELETE_ARRAY(rgelem);

	return GPOS_OK;
}


//---------------------------------------------------------------------------
//	@function:
//		CSyncHashtableTest::EresUnittest_NonConcurrentIteration
//
//	@doc:
//		Test iteration by a single client over all hash table elements
//
//---------------------------------------------------------------------------
GPOS_RESULT
CSyncHashtableTest::EresUnittest_NonConcurrentIteration()
{
	// create memory pool
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	SElem *rgelem = GPOS_NEW_ARRAY(pmp, SElem, GPOS_SHT_ELEMENTS);

	SElemHashtable sht;

	sht.Init
		(
		pmp,
		GPOS_SHT_SMALL_BUCKETS,
		GPOS_OFFSET(SElem, m_link),
		GPOS_OFFSET(SElem, m_ulKey),
		&(SElem::m_ulInvalid),
		SElem::UlHash,
		SElem::FEqualKeys
		);


	// iterate over empty hash table
	SElemHashtableIter shtitEmpty(sht);
	GPOS_ASSERT(!shtitEmpty.FAdvance() &&
				"Iterator advanced in an empty hash table");


	// insert elements
	for (ULONG i = 0; i < GPOS_SHT_ELEMENTS; i++)
	{
		rgelem[i] = SElem(i, i);
		sht.Insert(&rgelem[i]);
	}

	// iteration with no concurrency - each access must
	// produce a unique, valid and not NULL element
	SElemHashtableIter shtit(sht);
	ULONG ulCount = 0;

#ifdef GPOS_DEBUG
	// maintain a flag for visiting each element
	CBitVector bv(pmp, GPOS_SHT_ELEMENTS);
#endif	// GPOS_DEBUG

	while (shtit.FAdvance())
	{
		SElemHashtableIterAccessor htitacc(shtit);

#ifdef GPOS_DEBUG
		SElem *pelem =
#endif	// GPOS_DEBUG
			htitacc.Pt();

		GPOS_ASSERT(NULL != pelem);

		GPOS_ASSERT(SElem::FValid(pelem->m_ulKey));

		// check if element has been visited before
		GPOS_ASSERT(!bv.FExchangeSet(pelem->UlId()) &&
				    "Iterator returned duplicates");

		ulCount++;
	}

	GPOS_ASSERT(ulCount == GPOS_SHT_ELEMENTS);

	GPOS_DELETE_ARRAY(rgelem);

	return GPOS_OK;
}


//---------------------------------------------------------------------------
//	@function:
//		CSyncHashtableTest::EresUnittest_ConcurrentIteration
//
//	@doc:
//		Test iteration by multiple clients over all hash table elements
//
//---------------------------------------------------------------------------
GPOS_RESULT
CSyncHashtableTest::EresUnittest_ConcurrentIteration()
{
	// create memory pool
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	SElem *rgelem = GPOS_NEW_ARRAY(pmp, SElem, GPOS_SHT_ELEMENTS);

	SElemHashtable sht;

	sht.Init
		(
		pmp,
		GPOS_SHT_SMALL_BUCKETS,
		GPOS_OFFSET(SElem, m_link),
		GPOS_OFFSET(SElem, m_ulKey),
		&(SElem::m_ulInvalid),
		SElem::UlHash,
		SElem::FEqualKeys
		);


	// insert elements
	for (ULONG i = 0; i < GPOS_SHT_ELEMENTS; i++)
	{
		rgelem[i] = SElem(i, i);
		sht.Insert(&rgelem[i]);
	}

	// test concurrent iteration
	PvUnittest_IteratorsRun(pmp, sht, rgelem, 0 /* ulStartIndex */);

	// remove a subset of elements
	const ULONG ulRemoved = GPOS_SHT_ELEMENTS / 2;
	for (ULONG i = 0; i < ulRemoved; i++)
	{
		SElemHashtableAccessor shtacc(sht, i);

		SElem *pelem = shtacc.PtLookup();
		GPOS_ASSERT(NULL != pelem);

		shtacc.Remove(pelem);
	}

	// test concurrent iteration again
	PvUnittest_IteratorsRun(pmp, sht, rgelem, ulRemoved);

	GPOS_DELETE_ARRAY(rgelem);

	return GPOS_OK;
}


//---------------------------------------------------------------------------
//	@function:
//		CSyncHashtableTest::PvUnittest_IteratorsRun
//
//	@doc:
//		Spawn a number of iterator check tasks; wait until all are done
//
//---------------------------------------------------------------------------
void *
CSyncHashtableTest::PvUnittest_IteratorsRun
	(
	IMemoryPool *pmp,
	SElemHashtable &sht,
	SElem *rgelem,
	ULONG ulStartIndex
	)
{
	CWorkerPoolManager *pwpm = CWorkerPoolManager::Pwpm();

	// scope for task proxy
	{
		CAutoTaskProxy atp(pmp, pwpm);
		CTask *rgtask[GPOS_SHT_THREADS];

		// create a test object shared by tasks
		SElemTest elemtest(sht, rgelem + ulStartIndex);

		// create tasks
		for (ULONG i = 0; i < GPOS_SHT_THREADS; i++)
		{
			rgtask[i] = atp.PtskCreate
							(
							PvUnittest_IteratorCheck,
							&elemtest
							);

			atp.Schedule(rgtask[i]);
		}

		// wait for completion
		for (ULONG i = 0; i < GPOS_SHT_THREADS; i++)
		{
			GPOS_CHECK_ABORT;

			atp.Wait(rgtask[i]);
		}
	}

	return NULL;
}


//---------------------------------------------------------------------------
//	@function:
//		CSyncHashtableTest::PvUnittest_IteratorCheck
//
//	@doc:
//		Iterator task that checks the correctness of both the contents
//		and number of visited elements; this function assumes a hash table
//		with no insert/delete operations, concurrent with iteration, for
//		the soundness of the checks
//
//---------------------------------------------------------------------------
void *
CSyncHashtableTest::PvUnittest_IteratorCheck
	(
	void *pv
	)
{
	SElemTest *pelemtest = (SElemTest *)pv;
	SElemHashtable &sht = pelemtest->Sht();

	CAutoMemoryPool amp;

#ifdef GPOS_DEBUG
	SElem *rgelem = pelemtest->Rgelem();
	ULONG ulStartId = rgelem[0].UlId();
	CBitVector bv(amp.Pmp(), GPOS_SHT_ELEMENTS);
#endif	// GPOS_DEBUG

	// start iteration
	SElemHashtableIter htit(sht);
	ULONG ulCount = 0;
	while (htit.FAdvance())
	{
		ULONG ulId = ULONG_MAX;

		// accessor scope
		{
			SElemHashtableIterAccessor htitacc(htit);
			SElem *pelem = htitacc.Pt();
			if (NULL != pelem)
			{
				GPOS_ASSERT(SElem::FValid(pelem->m_ulKey));

				ulId = pelem->UlId();
			}
		}

		if (ulId != ULONG_MAX)
		{
			// check if element has been visited before
			GPOS_ASSERT(!bv.FExchangeSet(ulId) &&
					    "Iterator returned duplicates");

			GPOS_ASSERT(ulId >= ulStartId && ulId < GPOS_SHT_ELEMENTS);

			ulCount++;
		}
	}

	// a final check for the total number of visited elements
	GPOS_ASSERT(ulCount == (GPOS_SHT_ELEMENTS - ulStartId));

	return NULL;
}


//---------------------------------------------------------------------------
//	@function:
//		CSyncHashtableTest::EresUnittest_Concurrency
//
//	@doc:
//		Spawn a number of tasks to access hash table; in order to increase
//		the chances of concurrent access, we force each task to wait until
//		all other tasks have actually started
//
//---------------------------------------------------------------------------
GPOS_RESULT
CSyncHashtableTest::EresUnittest_Concurrency()
{
	// create memory pool
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	CWorkerPoolManager *pwpm = CWorkerPoolManager::Pwpm();

	GPOS_ASSERT(GPOS_SHT_THREADS <= pwpm->UlWorkersMax() &&
				"Insufficient number of workers to run test");

	SElemHashtable sht;
	sht.Init
		(
		pmp,
		GPOS_SHT_SMALL_BUCKETS,
		GPOS_OFFSET(SElem, m_link),
		GPOS_OFFSET(SElem, m_ulKey),
		&(SElem::m_ulInvalid),
		SElem::UlHash,
		SElem::FEqualKeys
		);

	SElem *rgelem = GPOS_NEW_ARRAY(pmp, SElem, GPOS_SHT_ELEMENTS);

	// insert an initial set of elements in hash table
	for (ULONG i = 0; i < GPOS_SHT_ELEMENTS; i ++)
	{
		rgelem[i] = SElem(i, i);
		if (i < GPOS_SHT_INITIAL_ELEMENTS)
		{
			sht.Insert(&rgelem[i]);
		}
	}


	// create an event for tasks synchronization
	CMutex mutex;
	CEvent event;
	event.Init(&mutex);

	// scope for tasks
	{

		CAutoTaskProxy atp(pmp, pwpm);

		CTask *rgtask[GPOS_SHT_THREADS];

		pfuncHashtableTask rgpfuncTask[] =
			{
			PvUnittest_Inserter,
			PvUnittest_Remover,
			PvUnittest_Reader,
			PvUnittest_Iterator
			};

		SElemTest elemtest(sht, rgelem, &event);

		const ULONG ulTypes = GPOS_ARRAY_SIZE(rgpfuncTask);

		// create tasks
		for (ULONG i = 0; i < GPOS_SHT_THREADS; i++)
		{
			ULONG ulTaskIndex = i % ulTypes;
			rgtask[i] = atp.PtskCreate
							(
							rgpfuncTask[ulTaskIndex],
							&elemtest
							);

			atp.Schedule(rgtask[i]);
		}


		// wait for completion
		for (ULONG i = 0; i < GPOS_SHT_THREADS; i++)
		{
			GPOS_CHECK_ABORT;

			atp.Wait(rgtask[i]);
		}

	}

	GPOS_DELETE_ARRAY(rgelem);

	return GPOS_OK;
}


//---------------------------------------------------------------------------
//	@function:
//		CSyncHashtableTest::Unittest_WaitTasks
//
//	@doc:
//		Waits until all others tasks have started
//
//---------------------------------------------------------------------------
void
CSyncHashtableTest::Unittest_WaitTasks
	(
	SElemTest *pelemtest
	)
{
	CEvent *pevent = pelemtest->Pevent();

	CAutoMutex am(*(pevent->Pmutex()));
	am.Lock();

	// increase number of started tasks
	pelemtest->IncStarted();

	// wait if some other task has not started yet
	if (pelemtest->UlStarted() < GPOS_SHT_THREADS)
	{
		pevent->Wait();
	}
	else
	{
		// wake up all waiting tasks
		pevent->Broadcast();
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CSyncHashtableTest::PvUnittest_Reader
//
//	@doc:
//		Reader task; lookups elements that will not be removed by other tasks;
//		all lookups must be successful
//
//---------------------------------------------------------------------------
void *
CSyncHashtableTest::PvUnittest_Reader
	(
	void *pv
	)
{
	SElemTest *pelemtest = (SElemTest *)pv;

	Unittest_WaitTasks(pelemtest);

	SElemHashtable &sht = pelemtest->Sht();

	for (ULONG i = 0; i < GPOS_SHT_LOOKUPS; i++)
	{
		ULONG ulKey =  i % GPOS_SHT_INITIAL_ELEMENTS;

		SElemHashtableAccessor shtacc(sht, ulKey);

#ifdef GPOS_DEBUG
		SElem *pelem =
#endif // GPOS_DEBUG
			shtacc.PtLookup();
		GPOS_ASSERT(NULL != pelem);
	}

	return NULL;
}


//---------------------------------------------------------------------------
//	@function:
//		CSyncHashtableTest::PvUnittest_Iterator
//
//	@doc:
//		Iterator task; we can only check the number of visited elements
//		to be in some range, since there can be concurrent insertions and
//		deletions
//
//---------------------------------------------------------------------------
void *
CSyncHashtableTest::PvUnittest_Iterator
	(
	void *pv
	)
{
	SElemTest *pelemtest = (SElemTest *)pv;

	Unittest_WaitTasks(pelemtest);

	CAutoMemoryPool amp;

	SElemHashtable &sht = pelemtest->Sht();
	SElemHashtableIter shtit(sht);
	ULONG ulCount = 0;

#ifdef GPOS_DEBUG
	CBitVector bv(amp.Pmp(), GPOS_SHT_ELEMENTS);
#endif	// GPOS_DEBUG

	while (shtit.FAdvance())
	{
		SElemHashtableIterAccessor shtitacc(shtit);
		SElem *pelem = shtitacc.Pt();
		if (NULL != pelem)
		{
			GPOS_ASSERT(SElem::FValid(pelem->m_ulKey));

			GPOS_ASSERT(!bv.FExchangeSet(pelem->UlId()) &&
					    "Iterator returned duplicates");

			ulCount++;
		}
	}

	GPOS_ASSERT(GPOS_SHT_INITIAL_ELEMENTS <= ulCount);

	return NULL;
}


//---------------------------------------------------------------------------
//	@function:
//		CSyncHashtableTest::PvUnittest_Remover
//
//	@doc:
//		Remover task; removal is restricted to elements in the upper
//		part of the array
//
//---------------------------------------------------------------------------
void *
CSyncHashtableTest::PvUnittest_Remover
	(
	void *pv
	)
{
	SElemTest *pelemtest = (SElemTest *)pv;

	Unittest_WaitTasks(pelemtest);

	SElemHashtable &sht = pelemtest->Sht();
	for (ULONG i = GPOS_SHT_INITIAL_ELEMENTS; i < GPOS_SHT_ELEMENTS; i++)
	{
		SElemHashtableAccessor shtacc(sht, i);

		SElem *pelem = shtacc.PtLookup();

		if (NULL != pelem)
		{
			shtacc.Remove(pelem);
		}
	}

	return NULL;
}


//---------------------------------------------------------------------------
//	@function:
//		CSyncHashtableTest::PvUnittest_Inserter
//
//	@doc:
//		Inserter task; insertion is restricted to elements in the upper part
//		of the array
//
//---------------------------------------------------------------------------
void *
CSyncHashtableTest::PvUnittest_Inserter
	(
	void *pv
	)
{
	SElemTest *pelemtest = (SElemTest *)pv;

	Unittest_WaitTasks(pelemtest);

	SElemHashtable &sht = pelemtest->Sht();
	SElem *rgelem = pelemtest->Rgelem();
	for (ULONG i = GPOS_SHT_INITIAL_ELEMENTS; i < GPOS_SHT_ELEMENTS; i++)
	{
		SElemHashtableAccessor shtacc(sht, i);

		SElem *pelem = shtacc.PtLookup();

		if (NULL == pelem)
		{
			shtacc.Insert(&rgelem[i]);
		}
	}

	return NULL;
}


#ifdef GPOS_DEBUG
//---------------------------------------------------------------------------
//	@function:
//		CSyncHashtableTest::EresUnittest_AccessorDeadlock
//
//	@doc:
//		Test for self-deadlock on hashtable accessor
//
//---------------------------------------------------------------------------
GPOS_RESULT
CSyncHashtableTest::EresUnittest_AccessorDeadlock()
{
	// create memory pool
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	// scope for hashtable
	{
		CSyncHashtable<SElem, ULONG, CSpinlockDummy> sht;
		sht.Init
			(
			pmp,
			GPOS_SHT_SMALL_BUCKETS,
			GPOS_OFFSET(SElem, m_link),
			GPOS_OFFSET(SElem, m_ulKey),
			&(SElem::m_ulInvalid),
			SElem::UlHash,
			SElem::FEqualKeys
			);

		SElem elem;
		elem.m_ulKey = 1;
		ULONG ulKey = elem.m_ulKey;

		CSyncHashtableAccessByKey<SElem, ULONG, CSpinlockDummy> shtacc0(sht, ulKey);

		// this must assert since we try to self-deadlock on a spinlock
		CSyncHashtableAccessByKey<SElem, ULONG, CSpinlockDummy> shtacc1(sht, ulKey);
	}

	return GPOS_FAILED;
}

#endif // GPOS_DEBUG

// EOF

