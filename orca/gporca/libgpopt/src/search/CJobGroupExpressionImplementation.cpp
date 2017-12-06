//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 Greenplum, Inc.
//
//	@filename:
//		CJobGroupExpressionImplementation.cpp
//
//	@doc:
//		Implementation of group expression implementation job
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpopt/engine/CEngine.h"
#include "gpopt/operators/CLogical.h"
#include "gpopt/search/CGroup.h"
#include "gpopt/search/CGroupExpression.h"
#include "gpopt/search/CJobFactory.h"
#include "gpopt/search/CJobGroupImplementation.h"
#include "gpopt/search/CJobGroupExpressionImplementation.h"
#include "gpopt/search/CJobTransformation.h"
#include "gpopt/search/CScheduler.h"
#include "gpopt/search/CSchedulerContext.h"
#include "gpopt/xforms/CXformFactory.h"


using namespace gpopt;

// State transition diagram for group expression implementation job state machine;
const CJobGroupExpressionImplementation::EEvent rgeev[CJobGroupExpressionImplementation::estSentinel][CJobGroupExpressionImplementation::estSentinel] =
{
	{ CJobGroupExpressionImplementation::eevImplementingChildren, CJobGroupExpressionImplementation::eevChildrenImplemented, CJobGroupExpressionImplementation::eevSentinel, CJobGroupExpressionImplementation::eevSentinel }, // estInitialized
	{ CJobGroupExpressionImplementation::eevSentinel, CJobGroupExpressionImplementation::eevImplementingSelf, CJobGroupExpressionImplementation::eevSelfImplemented, CJobGroupExpressionImplementation::eevSentinel }, // estChildrenImplemented
	{ CJobGroupExpressionImplementation::eevSentinel, CJobGroupExpressionImplementation::eevSentinel, CJobGroupExpressionImplementation::eevSentinel, CJobGroupExpressionImplementation::eevFinalized }, // estSelfImplemented
	{ CJobGroupExpressionImplementation::eevSentinel, CJobGroupExpressionImplementation::eevSentinel, CJobGroupExpressionImplementation::eevSentinel, CJobGroupExpressionImplementation::eevSentinel }, // estCompleted
};

#ifdef GPOS_DEBUG

// names for states
const WCHAR rgwszStates[CJobGroupExpressionImplementation::estSentinel][GPOPT_FSM_NAME_LENGTH] =
{
	GPOS_WSZ_LIT("initialized"),
	GPOS_WSZ_LIT("children implemented"),
	GPOS_WSZ_LIT("self implemented"),
	GPOS_WSZ_LIT("completed")
};

// names for events
const WCHAR rgwszEvents[CJobGroupExpressionImplementation::eevSentinel][GPOPT_FSM_NAME_LENGTH] =
{
	GPOS_WSZ_LIT("implementing child groups"),
	GPOS_WSZ_LIT("implemented children groups"),
	GPOS_WSZ_LIT("applying implementation xforms"),
	GPOS_WSZ_LIT("applied implementation xforms"),
	GPOS_WSZ_LIT("finalized")
};

#endif // GPOS_DEBUG


//---------------------------------------------------------------------------
//	@function:
//		CJobGroupExpressionImplementation::CJobGroupExpressionImplementation
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CJobGroupExpressionImplementation::CJobGroupExpressionImplementation()
{}


//---------------------------------------------------------------------------
//	@function:
//		CJobGroupExpressionImplementation::~CJobGroupExpressionImplementation
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CJobGroupExpressionImplementation::~CJobGroupExpressionImplementation()
{}


//---------------------------------------------------------------------------
//	@function:
//		CJobGroupExpressionImplementation::Init
//
//	@doc:
//		Initialize job
//
//---------------------------------------------------------------------------
void
CJobGroupExpressionImplementation::Init
	(
	CGroupExpression *pgexpr
	)
{
	CJobGroupExpression::Init(pgexpr);
	GPOS_ASSERT(pgexpr->Pop()->FLogical());

	m_jsm.Init
			(
			rgeev
#ifdef GPOS_DEBUG
			,
			rgwszStates,
			rgwszEvents
#endif // GPOS_DEBUG
			);

	// set job actions
	m_jsm.SetAction(estInitialized, EevtImplementChildren);
	m_jsm.SetAction(estChildrenImplemented, EevtImplementSelf);
	m_jsm.SetAction(estSelfImplemented, EevtFinalize);

	CJob::SetInit();
}


//---------------------------------------------------------------------------
//	@function:
//		CJobGroupExpressionImplementation::ScheduleApplicableTransformations
//
//	@doc:
//		Schedule transformation jobs for all applicable xforms
//
//---------------------------------------------------------------------------
void
CJobGroupExpressionImplementation::ScheduleApplicableTransformations
	(
	CSchedulerContext *psc
	)
{
	GPOS_ASSERT(!FXformsScheduled());

	// get all applicable xforms
	COperator *pop = m_pgexpr->Pop();
	CXformSet *pxfs = CLogical::PopConvert(pop)->PxfsCandidates(psc->PmpGlobal());

	// intersect them with required xforms and schedule jobs
	pxfs->Intersection(CXformFactory::Pxff()->PxfsImplementation());
	pxfs->Intersection(psc->Peng()->PxfsCurrentStage());
	ScheduleTransformations(psc, pxfs);
	pxfs->Release();

	SetXformsScheduled();
}


//---------------------------------------------------------------------------
//	@function:
//		CJobGroupExpressionImplementation::ScheduleChildGroupsJobs
//
//	@doc:
//		Schedule implementation jobs for all child groups
//
//---------------------------------------------------------------------------
void
CJobGroupExpressionImplementation::ScheduleChildGroupsJobs
	(
	CSchedulerContext *psc
	)
{
	GPOS_ASSERT(!FChildrenScheduled());

	ULONG ulArity = m_pgexpr->UlArity();

	for (ULONG i = 0; i < ulArity; i++)
	{
		CJobGroupImplementation::ScheduleJob(psc, (*(m_pgexpr))[i], this);
	}

	SetChildrenScheduled();
}


//---------------------------------------------------------------------------
//	@function:
//		CJobGroupExpressionImplementation::EevtImplementChildren
//
//	@doc:
//		Implement child groups
//
//---------------------------------------------------------------------------
CJobGroupExpressionImplementation::EEvent
CJobGroupExpressionImplementation::EevtImplementChildren
	(
	CSchedulerContext *psc,
	CJob *pjOwner
	)
{
	// get a job pointer
	CJobGroupExpressionImplementation *pjgei = PjConvert(pjOwner);
	GPOS_ASSERT(pjgei->m_pgexpr->FExplored());

	if (!pjgei->FChildrenScheduled())
	{
		pjgei->m_pgexpr->SetState(CGroupExpression::estImplementing);
		pjgei->ScheduleChildGroupsJobs(psc);

		return eevImplementingChildren;
	}
	else
	{
		return eevChildrenImplemented;
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CJobGroupExpressionImplementation::EevtImplementSelf
//
//	@doc:
//		Implement group expression
//
//---------------------------------------------------------------------------
CJobGroupExpressionImplementation::EEvent
CJobGroupExpressionImplementation::EevtImplementSelf
	(
	CSchedulerContext *psc,
	CJob *pjOwner
	)
{
	// get a job pointer
	CJobGroupExpressionImplementation *pjgei = PjConvert(pjOwner);
	if (!pjgei->FXformsScheduled())
	{
		pjgei->ScheduleApplicableTransformations(psc);
		return eevImplementingSelf;
	}
	else
	{
		return eevSelfImplemented;
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CJobGroupExpressionImplementation::EevtFinalize
//
//	@doc:
//		Finalize implementation
//
//---------------------------------------------------------------------------
CJobGroupExpressionImplementation::EEvent
CJobGroupExpressionImplementation::EevtFinalize
	(
	CSchedulerContext *, //psc
	CJob *pjOwner
	)
{
	// get a job pointer
	CJobGroupExpressionImplementation *pjgei = PjConvert(pjOwner);
	pjgei->m_pgexpr->SetState(CGroupExpression::estImplemented);

	return eevFinalized;
}


//---------------------------------------------------------------------------
//	@function:
//		CJobGroupExpressionImplementation::FExecute
//
//	@doc:
//		Main job function
//
//---------------------------------------------------------------------------
BOOL
CJobGroupExpressionImplementation::FExecute
	(
	CSchedulerContext *psc
	)
{
	GPOS_ASSERT(FInit());

	return m_jsm.FRun(psc, this);
}


//---------------------------------------------------------------------------
//	@function:
//		CJobGroupExpressionImplementation::ScheduleJob
//
//	@doc:
//		Schedule a new group expression implementation job
//
//---------------------------------------------------------------------------
void
CJobGroupExpressionImplementation::ScheduleJob
	(
	CSchedulerContext *psc,
	CGroupExpression *pgexpr,
	CJob *pjParent
	)
{
	CJob *pj = psc->Pjf()->PjCreate(CJob::EjtGroupExpressionImplementation);

	// initialize job
	CJobGroupExpressionImplementation *pjige = PjConvert(pj);
	pjige->Init(pgexpr);
	psc->Psched()->Add(pjige, pjParent);
}

#ifdef GPOS_DEBUG

//---------------------------------------------------------------------------
//	@function:
//		CJobGroupExpressionImplementation::OsPrint
//
//	@doc:
//		Print function
//
//---------------------------------------------------------------------------
IOstream &
CJobGroupExpressionImplementation::OsPrint
	(
	IOstream &os
	)
{
	return m_jsm.OsHistory(os);
}

#endif // GPOS_DEBUG

// EOF

