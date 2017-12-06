//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CJob.h
//
//	@doc:
//		Interface class for optimization job abstraction
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CJob_H
#define GPOPT_CJob_H

#include "gpos/base.h"
#include "gpos/common/CList.h"
#include "gpos/sync/atomic.h"

namespace gpopt
{
	using namespace gpos;
	
	// prototypes
	class CJobQueue;
	class CScheduler;
	class CSchedulerContext;

	//---------------------------------------------------------------------------
	//	@class:
	//		CJob
	//
	//	@doc:
	//		Superclass of all optimization jobs
	//
	//---------------------------------------------------------------------------
	class CJob
	{	
		// friends
		friend class CJobFactory;
		friend class CJobQueue;
		friend class CScheduler;
		
		public:

			// job type
			enum EJobType
			{
				EjtTest = 0,
				EjtGroupOptimization,
				EjtGroupImplementation,
				EjtGroupExploration,
				EjtGroupExpressionOptimization,
				EjtGroupExpressionImplementation,
				EjtGroupExpressionExploration,
				EjtTransformation,

				EjtInvalid,
				EjtSentinel = EjtInvalid
			};

		private:

#ifdef GPOS_DEBUG
			// enum for job state
			enum EJobState
			{
				EjsInit = 0,
				EjsWaiting,
				EjsRunning,
				EjsSuspended,
				EjsCompleted
			};
#endif // GPOS_DEBUG

			// parent job
			CJob *m_pjParent;

			// assigned job queue
			CJobQueue *m_pjq;

			// reference counter
			volatile ULONG_PTR m_ulpRefs;

			// job id - set by job factory
			ULONG m_ulId;

			// job type
			EJobType m_ejt;

			// flag indicating if job is initialized
			BOOL m_fInit;

#ifdef GPOS_DEBUG
			// job state
			EJobState m_ejs;
#endif // GPOS_DEBUG

			//-------------------------------------------------------------------
			// Interface for CJobFactory
			//-------------------------------------------------------------------

			// set type
			void SetJobType(EJobType ejt)
			{
				m_ejt = ejt;
			}

			//-------------------------------------------------------------------
			// Interface for CScheduler
			//-------------------------------------------------------------------

			// parent accessor
			CJob *PjParent() const
			{
				return m_pjParent;
			}

			// set parent
			void SetParent(CJob *pj)
			{
				GPOS_ASSERT(this != pj);

				m_pjParent = pj;
			}

			// increment reference counter
			void IncRefs()
			{
				(void) UlpExchangeAdd(&m_ulpRefs, 1);
			}

			// decrement reference counter
			ULONG_PTR UlpDecrRefs()
			{
				ULONG_PTR ulpRefs = UlpExchangeAdd(&m_ulpRefs, -1);
				GPOS_ASSERT(0 < ulpRefs && "Decrement counter from 0");
				return ulpRefs;
			}

			// notify parent of job completion;
			// return true if parent is runnable;
			BOOL FResumeParent() const;

#ifdef GPOS_DEBUG
			// reference counter accessor
			ULONG_PTR UlpRefs() const
			{
				return m_ulpRefs;
			}

			// check if job type is valid
			BOOL FValidType() const
			{
				return (EjtTest <= m_ejt && EjtSentinel > m_ejt);
			}

			// get state
			EJobState Ejs() const
			{
				return m_ejs;
			}

			// set state
			void SetState(EJobState ejs)
			{
				m_ejs = ejs;
			}
#endif // GPOS_DEBUG

			// private copy ctor
			CJob(const CJob&);

		protected:

			// id accessor
			ULONG UlId() const
			{
				return m_ulId;
			}

			// ctor
			CJob()
				:
				m_pjParent(NULL),
				m_pjq(NULL),
				m_ulpRefs(0),
				m_ulId(0),
				m_fInit(false)
#ifdef GPOS_DEBUG
				,
				m_ejs(EjsInit)
#endif // GPOS_DEBUG
			{}
			
			// dtor
			virtual ~CJob() 
			{
				GPOS_ASSERT_IMP
					(
					!ITask::PtskSelf()->FPendingExc(),
					0 == m_ulpRefs
					);
			}

			// reset job
			virtual
			void Reset();

			// check if job is initialized
			BOOL FInit() const
			{
				return m_fInit;
			}

			// mark job as initialized
			void SetInit()
			{
				GPOS_ASSERT(false == m_fInit);

				m_fInit = true;
			}

		public:

			// actual job execution given a scheduling context
			// returns true if job completes, false if it is suspended
			virtual BOOL FExecute(CSchedulerContext *psc) = 0;

			// type accessor
			EJobType Ejt() const
			{
				return m_ejt;
			}

			// job queue accessor
			CJobQueue *Pjq() const
			{
				return m_pjq;
			}

			// set job queue
			void SetJobQueue(CJobQueue *pjq)
			{
				GPOS_ASSERT(NULL != pjq);
				m_pjq = pjq;
			}

			// cleanup internal state
			virtual
			void Cleanup() {}

#ifdef GPOS_DEBUG
			// print job description
			virtual
			IOstream &OsPrint(IOstream &os);

			// link for running job list
			SLink m_linkRunning;

			// link for suspended job list
			SLink m_linkSuspended;

#endif // GPOS_DEBUG

			// link for job queueing
			SLink m_linkQueue;
		
	}; // class CJob
	
}

#endif // !GPOPT_CJob_H


// EOF
