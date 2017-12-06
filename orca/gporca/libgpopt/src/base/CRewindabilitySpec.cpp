//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CRewindabilitySpec.cpp
//
//	@doc:
//		Specification of rewindability of intermediate query results
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpopt/base/CRewindabilitySpec.h"
#include "gpopt/operators/CPhysicalSpool.h"

using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CRewindabilitySpec::CRewindabilitySpec
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CRewindabilitySpec::CRewindabilitySpec
	(
	ERewindabilityType ert
	)
	:
	m_ert(ert)
{}


//---------------------------------------------------------------------------
//	@function:
//		CRewindabilitySpec::~CRewindabilitySpec
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CRewindabilitySpec::~CRewindabilitySpec()
{}


//---------------------------------------------------------------------------
//	@function:
//		CRewindabilitySpec::FMatch
//
//	@doc:
//		Check for equality between rewindability specs
//
//---------------------------------------------------------------------------
BOOL
CRewindabilitySpec::FMatch
	(
	const CRewindabilitySpec *prs
	)
	const
{
	GPOS_ASSERT(NULL != prs);

	return Ert() == prs->Ert();
}


//---------------------------------------------------------------------------
//	@function:
//		CRewindabilitySpec::FSatisfies
//
//	@doc:
//		Check if this rewindability spec satisfies the given one
//
//---------------------------------------------------------------------------
BOOL
CRewindabilitySpec::FSatisfies
	(
	const CRewindabilitySpec *prs
	)
	const
{
	return
		FMatch(prs) ||
		ErtNone == prs->Ert() ||
		(ErtMarkRestore == Ert() && ErtGeneral == prs->Ert());
}


//---------------------------------------------------------------------------
//	@function:
//		CRewindabilitySpec::UlHash
//
//	@doc:
//		Hash of components
//
//---------------------------------------------------------------------------
ULONG
CRewindabilitySpec::UlHash() const
{
	return gpos::UlHash<ERewindabilityType>(&m_ert);
}


//---------------------------------------------------------------------------
//	@function:
//		CRewindabilitySpec::AppendEnforcers
//
//	@doc:
//		Add required enforcers to dynamic array
//
//---------------------------------------------------------------------------
void
CRewindabilitySpec::AppendEnforcers
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
	GPOS_ASSERT(NULL != prpp);
	GPOS_ASSERT(NULL != pmp);
	GPOS_ASSERT(NULL != pdrgpexpr);
	GPOS_ASSERT(NULL != pexpr);
	GPOS_ASSERT(this == prpp->Per()->PrsRequired() &&
				"required plan properties don't match enforced rewindability spec");

	pexpr->AddRef();
	CExpression *pexprSpool = GPOS_NEW(pmp) CExpression
									(
									pmp, 
									GPOS_NEW(pmp) CPhysicalSpool(pmp),
									pexpr
									);
	pdrgpexpr->Append(pexprSpool);
}


//---------------------------------------------------------------------------
//	@function:
//		CRewindabilitySpec::OsPrint
//
//	@doc:
//		Print rewindability spec
//
//---------------------------------------------------------------------------
IOstream &
CRewindabilitySpec::OsPrint
	(
	IOstream &os
	)
	const
{
	switch (Ert())
	{
		case ErtGeneral:
			return os << "REWINDABLE";

		case ErtMarkRestore:
			return os << "MARK-RESTORE";

		case ErtNone:
			return os << "NON-REWINDABLE";

		default:
			GPOS_ASSERT(!"Unrecognized rewindability type");
			return os;
	}
}


// EOF

