//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CJobGroupExploration.h
//
//	@doc:
//		Group exploration job
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CJobGroupExploration_H
#define GPOPT_CJobGroupExploration_H

#include "gpos/base.h"

#include "gpopt/search/CJobGroup.h"
#include "gpopt/search/CJobStateMachine.h"

namespace gpopt
{
	using namespace gpos;


	//---------------------------------------------------------------------------
	//	@class:
	//		CJobGroupExploration
	//
	//	@doc:
	//		Group exploration job
	//
	//---------------------------------------------------------------------------
	class CJobGroupExploration : public CJobGroup
	{
		public:

			// transition events of group exploration
			enum EEvent
			{
				eevStartedExploration,	// started group exploration
				eevNewChildren,			// new children have been added to group
				eevExplored,			// group exploration is complete

				eevSentinel
			};

			// states of group exploration job
			enum EState
			{
				estInitialized = 0,		// initial state
				estExploringChildren,	// exploring group expressions
				estCompleted,			// done exploration

				estSentinel
			};

		private:

			// shorthand for job state machine
			typedef CJobStateMachine<EState, estSentinel, EEvent, eevSentinel> JSM;

			// job state machine
			JSM m_jsm;

			// start exploration action
			static
			EEvent EevtStartExploration(CSchedulerContext *psc, CJob *pj);

			// explore child group expressions action
			static
			EEvent EevtExploreChildren(CSchedulerContext *psc, CJob *pj);

			// private copy ctor
			CJobGroupExploration(const CJobGroupExploration&);

		public:

			// ctor
			CJobGroupExploration();

			// dtor
			~CJobGroupExploration();

			// initialize job
			void Init(CGroup *pgroup);

			// get first unscheduled expression
			virtual
			CGroupExpression *PgexprFirstUnsched()
			{
				return CJobGroup::PgexprFirstUnschedLogical();
			}

			// schedule exploration jobs for of all new group expressions
			virtual
			BOOL FScheduleGroupExpressions(CSchedulerContext *psc);

			// schedule a new group exploration job
			static
			void ScheduleJob
				(
				CSchedulerContext *psc,
				CGroup *pgroup,
				CJob *pjParent
				);

			// job's function
			virtual
			BOOL FExecute(CSchedulerContext *psc);

#ifdef GPOS_DEBUG

			// print function
			virtual
			IOstream &OsPrint(IOstream &os);

			// dump state machine diagram in graphviz format
			virtual
			IOstream &OsDiagramToGraphviz
				(
				IMemoryPool *pmp,
				IOstream &os,
				const WCHAR *wszTitle
				)
				const
			{
				(void) m_jsm.OsDiagramToGraphviz(pmp, os, wszTitle);

				return os;
			}

			// compute unreachable states
			void Unreachable
				(
				IMemoryPool *pmp,
				EState **ppestate,
				ULONG *pulSize
				)
				const
			{
				m_jsm.Unreachable(pmp, ppestate, pulSize);
			}


#endif // GPOS_DEBUG

			// conversion function
			static
			CJobGroupExploration *PjConvert
				(
				CJob *pj
				)
			{
				GPOS_ASSERT(NULL != pj);
				GPOS_ASSERT(EjtGroupExploration == pj->Ejt());

				return dynamic_cast<CJobGroupExploration*>(pj);
			}


	}; // class CJobGroupExploration

}

#endif // !GPOPT_CJobGroupExploration_H


// EOF
