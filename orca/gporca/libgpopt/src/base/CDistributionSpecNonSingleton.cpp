//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2013 EMC Corp.
//
//	@filename:
//		CDistributionSpecNonSingleton.cpp
//
//	@doc:
//		Specification of non-singleton distribution
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "naucrates/traceflags/traceflags.h"
#include "gpopt/base/CUtils.h"
#include "gpopt/base/CColRefSet.h"
#include "gpopt/base/CColRefSetIter.h"
#include "gpopt/base/CDistributionSpecNonSingleton.h"
#include "gpopt/operators/CPhysicalMotionRandom.h"

using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CDistributionSpecNonSingleton::CDistributionSpecNonSingleton
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CDistributionSpecNonSingleton::CDistributionSpecNonSingleton
       ()
       :
       m_fAllowReplicated(true)
{}


 //---------------------------------------------------------------------------
 //     @function:
//             CDistributionSpecNonSingleton::CDistributionSpecNonSingleton
//
//     @doc:
//             Ctor
//
//---------------------------------------------------------------------------
CDistributionSpecNonSingleton::CDistributionSpecNonSingleton
       (
       BOOL fAllowReplicated
       )
       :
       m_fAllowReplicated(fAllowReplicated)
{}


//---------------------------------------------------------------------------
//	@function:
//		CDistributionSpecNonSingleton::FSatisfies
//
//	@doc:
//		Check if this distribution spec satisfies the given one
//
//---------------------------------------------------------------------------
BOOL
CDistributionSpecNonSingleton::FSatisfies
	(
	const CDistributionSpec * // pds
	)
	const
{
	GPOS_ASSERT(!"Non-Singleton distribution cannot be derived");

	return false;
}

//---------------------------------------------------------------------------
//	@function:
//		CDistributionSpecNonSingleton::AppendEnforcers
//
//	@doc:
//		Add required enforcers to dynamic array;
//		non-singleton distribution is enforced by spraying data randomly
//		on segments
//
//---------------------------------------------------------------------------
void
CDistributionSpecNonSingleton::AppendEnforcers
	(
	IMemoryPool *pmp,
	CExpressionHandle &, // exprhdl
	CReqdPropPlan *
#ifdef GPOS_DEBUG
	prpp
#endif // GPOS_DEBUG
	,
	DrgPexpr *pdrgpexpr,
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pmp);
	GPOS_ASSERT(NULL != prpp);
	GPOS_ASSERT(NULL != pdrgpexpr);
	GPOS_ASSERT(NULL != pexpr);
	GPOS_ASSERT(!GPOS_FTRACE(EopttraceDisableMotions));
	GPOS_ASSERT(this == prpp->Ped()->PdsRequired() &&
	            "required plan properties don't match enforced distribution spec");


	if (GPOS_FTRACE(EopttraceDisableMotionRandom))
	{
		// random Motion is disabled
		return;
	}

	// add a random distribution enforcer
	CDistributionSpecRandom *pdsrandom = GPOS_NEW(pmp) CDistributionSpecRandom();
	pexpr->AddRef();
	CExpression *pexprMotion = GPOS_NEW(pmp) CExpression
										(
										pmp,
										GPOS_NEW(pmp) CPhysicalMotionRandom(pmp, pdsrandom),
										pexpr
										);
	pdrgpexpr->Append(pexprMotion);
}

//---------------------------------------------------------------------------
//	@function:
//		CDistributionSpecNonSingleton::OsPrint
//
//	@doc:
//		Print function
//
//---------------------------------------------------------------------------
IOstream &
CDistributionSpecNonSingleton::OsPrint
	(
	IOstream &os
	)
	const
{
	os << "NON-SINGLETON ";
	if (!m_fAllowReplicated)
	{
	   os << " (NON-REPLICATED)";
	}
	return os;
}

// EOF

