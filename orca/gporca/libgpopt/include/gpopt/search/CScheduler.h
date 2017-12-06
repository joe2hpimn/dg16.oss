//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2008-2011 Greenplum, Inc.
//
//	@filename:
//		CScheduler.h
//
//	@doc:
//		Scheduler interface for execution of optimization jobs
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CScheduler_H
#define GPOPT_CScheduler_H

#include "gpos/base.h"
#include "gpos/common/CSyncList.h"
#include "gpos/common/CSyncPool.h"
#include "gpos/sync/CEvent.h"

#include "gpopt/search/CJob.h"

#define OPT_SCHED_QUEUED_RUNNING_RATIO 10
#define OPT_SCHED_CFA 100

namespace gpopt
{
	using namespace gpos;
	
	// prototypes
	class CSchedulerContext;

	//---------------------------------------------------------------------------
	//	@class:
	//		CScheduler
	//
	//	@doc:
	//		MT-scheduler for optimization jobs
	//
	//---------------------------------------------------------------------------
	class CScheduler
	{	
		// friend classes
		friend class CJob;

		public:

			// enum for job execution result
			enum EJobResult
			{
				EjrRunnable = 0,
				EjrSuspended,
				EjrCompleted,

				EjrSentinel
			};

		private:

			// job wrapper; used for inserting job to waiting list (lock-free)
			struct SJobLink
			{
				// link id, set by sync set
				ULONG m_ulId;

				// pointer to job
				CJob *m_pj;

				// slink for list of waiting jobs
				SLink m_link;

				// initialize link
				void Init(CJob *pj)
				{
					m_pj = pj;
					m_link.m_pvPrev = m_link.m_pvNext = NULL;
				}
			};

			// mutex and event mechanism for individual workers
			CMutex m_mutex;
			CEvent m_event;
					
			// list of jobs waiting to execute
			CSyncList<SJobLink> m_listjlWaiting;

			// pool of job link objects
			CSyncPool<SJobLink> m_spjl;

			// number of tasks assigned
			const ULONG_PTR m_ulpTasksMax;

			// number of active tasks;
			volatile ULONG_PTR m_ulpTasksActive;

			// current job counters
			volatile ULONG_PTR m_ulpTotal;
			volatile ULONG_PTR m_ulpRunning;
			volatile ULONG_PTR m_ulpQueued;

			// stats
			volatile ULONG_PTR m_ulpStatsQueued;
			volatile ULONG_PTR m_ulpStatsDequeued;
			volatile ULONG_PTR m_ulpStatsSuspended;
			volatile ULONG_PTR m_ulpStatsCompleted;
			volatile ULONG_PTR m_ulpStatsCompletedQueued;
			volatile ULONG_PTR m_ulpStatsResumed;

#ifdef GPOS_DEBUG
			// list of running jobs
			CList<CJob> m_listjRunning;

			// list of suspended jobs
			CList<CJob> m_listjSuspended;

			// flag indicating if scheduler keeps track
			// of running and suspended jobs
			const BOOL m_fTrackingJobs;
#endif // GPOS_DEBUG

			// internal job processing task
			void ProcessJobs(CSchedulerContext *psc);

			// keep executing waiting jobs (if any)
			void ExecuteJobs(CSchedulerContext *psc);

			// process job execution results
			void ProcessJobResult
				(
				CJob *pj,
				CSchedulerContext *psc,
				BOOL fCompleted
				);

			// retrieve next job to run
			CJob *PjRetrieve();

			// schedule job for execution
			void Schedule(CJob *pj);

			// prepare for job execution
			void PreExecute(CJob *pj);

			// execute job
			BOOL FExecute(CJob *pj, CSchedulerContext *psc);

			// process job execution outcome
			EJobResult EjrPostExecute(CJob *pj, BOOL fCompleted);

			// resume parent job
			void ResumeParent(CJob *pj);

			// check if all jobs have completed
			BOOL FEmpty() const
			{
				return (0 == m_ulpTotal);
			}

			// increment counter of active tasks
			void IncTasksActive()
			{
				(void) UlpExchangeAdd(&m_ulpTasksActive, 1);
			}

			// decrement counter of active tasks
			void DecrTasksActive()
			{
				(void) UlpExchangeAdd(&m_ulpTasksActive, -1);
			}

			// check if there is enough work for more workers
			BOOL FIncreaseWorkers() const
			{
				GPOS_ASSERT(m_ulpTasksMax >= m_ulpRunning);
				return
					(
					m_ulpTasksMax > m_ulpTasksActive &&
					(OPT_SCHED_QUEUED_RUNNING_RATIO < m_ulpQueued / (m_ulpTasksActive + 1))
				    )
				    ;
			}

			// no copy ctor
			CScheduler(const CScheduler&);

		public:
		
			// ctor
			CScheduler
				(
				IMemoryPool *pmp,
				ULONG ulJobs,
				ULONG_PTR ulpTasks
#ifdef GPOS_DEBUG
				,
				BOOL fTrackingJobs = true
#endif // GPOS_DEBUG
				);

			// dtor
			virtual	~CScheduler();

			// main job processing task
			static
			void *Run(void*);

			// transition job to completed
			void Complete(CJob *pj);

			// transition queued job to completed
			void CompleteQueued(CJob *pj);

			// transition job to suspended
			void Suspend(CJob *pj);
			
			// add new job for scheduling
			void Add(CJob *pj, CJob *pjParent);

			// resume suspended job
			void Resume(CJob *pj);

			// print statistics
			void PrintStats() const;
			
#ifdef GPOS_DEBUG
			// get flag for tracking jobs
			BOOL FTrackingJobs() const
			{
				return m_fTrackingJobs;
			}

			// print queue
			IOstream &OsPrintActiveJobs(IOstream &);

#endif // GPOS_DEBUG

	}; // class CScheduler

	// shorthand for printing
	inline
	IOstream &operator <<
		(
		IOstream &os,
		CScheduler &
#ifdef GPOS_DEBUG
		sched
#endif // GPOS_DEBUG
		)
	{
#ifdef GPOS_DEBUG
		return sched.OsPrintActiveJobs(os);
#else
		return os;
#endif // GPOS_DEBUG
	}
}

#endif // !GPOPT_CScheduler_H


// EOF
