//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CJobQueue.h
//
//	@doc:
//		Class controlling unique execution of an operation that is
//		potentially assigned to many jobs.
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CJobQueue_H
#define GPOPT_CJobQueue_H

#include "gpos/base.h"
#include "gpos/common/CList.h"
#include "gpos/sync/CSpinlock.h"

#include "gpopt/spinlock.h"
#include "gpopt/search/CJob.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CJobQueue
	//
	//	@doc:
	//		Forces unique execution of an operation assigned to many jobs.
	//
	//---------------------------------------------------------------------------
	class CJobQueue
	{
		private:

			// main job
			volatile CJob *m_pj;

			// flag indicating if main job has completed
			volatile BOOL m_fCompleted;

			// list of jobs waiting for main job to complete
			CList<CJob> m_listjQueued;

			// lock protecting queue
			CSpinlockJobQueue m_slock;

		public:

			// enum indicating job queueing result
			enum EJobQueueResult
			{
				EjqrMain = 0,
				EjqrQueued,
				EjqrCompleted
			};

			// ctor
			CJobQueue()
				:
				m_pj(NULL),
				m_fCompleted(false)
			{
				m_listjQueued.Init(GPOS_OFFSET(CJob, m_linkQueue));
			}

			// dtor
			~CJobQueue()
			{
				GPOS_ASSERT_IMP
					(
					NULL != ITask::PtskSelf() &&
					!ITask::PtskSelf()->FPendingExc(),
					m_listjQueued.FEmpty()
					);
			}

			// reset job queue
			void Reset()
			{
				GPOS_ASSERT(m_listjQueued.FEmpty());

				m_pj = NULL;
				m_fCompleted = false;
			}

			// add job as a waiter;
			EJobQueueResult EjqrAdd(CJob *pj);

			// notify waiting jobs of job completion
			void NotifyCompleted(CSchedulerContext *psc);

#ifdef GPOS_DEBUG
			// print queue - not thread-safe
			IOstream &OsPrintQueuedJobs(IOstream &);
#endif // GPOS_DEBUG

	}; // class CJobQueue

}

#endif // !GPOPT_CJobQueue_H


// EOF
