//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CPhysicalLeftAntiSemiHashJoin.cpp
//
//	@doc:
//		Implementation of left anti semi hash join operator
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/base.h"
#include "gpopt/base/CUtils.h"
#include "gpopt/base/CDistributionSpecHashed.h"
#include "gpopt/operators/CPhysicalLeftAntiSemiHashJoin.h"


using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalLeftAntiSemiHashJoin::CPhysicalLeftAntiSemiHashJoin
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CPhysicalLeftAntiSemiHashJoin::CPhysicalLeftAntiSemiHashJoin
	(
	IMemoryPool *pmp,
	DrgPexpr *pdrgpexprOuterKeys,
	DrgPexpr *pdrgpexprInnerKeys
	)
	:
	CPhysicalHashJoin(pmp, pdrgpexprOuterKeys, pdrgpexprInnerKeys)
{
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalLeftAntiSemiHashJoin::~CPhysicalLeftAntiSemiHashJoin
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CPhysicalLeftAntiSemiHashJoin::~CPhysicalLeftAntiSemiHashJoin()
{
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalLeftAntiSemiHashJoin::FProvidesReqdCols
//
//	@doc:
//		Check if required columns are included in output columns
//
//---------------------------------------------------------------------------
BOOL
CPhysicalLeftAntiSemiHashJoin::FProvidesReqdCols
	(
	CExpressionHandle &exprhdl,
	CColRefSet *pcrsRequired,
	ULONG // ulOptReq
	)
	const
{
	// left anti semi join only propagates columns from left child
	return FOuterProvidesReqdCols(exprhdl, pcrsRequired);
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalLeftAntiSemiHashJoin::PppsRequired
//
//	@doc:
//		Compute required partition propagation of the n-th child
//
//---------------------------------------------------------------------------
CPartitionPropagationSpec *
CPhysicalLeftAntiSemiHashJoin::PppsRequired
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	CPartitionPropagationSpec *pppsRequired,
	ULONG ulChildIndex,
	DrgPdp *, // pdrgpdpCtxt,
	ULONG // ulOptReq
	)
{
	// no partition elimination for LASJ: push request to the respective child
	return CPhysical::PppsRequiredPushThruNAry(pmp, exprhdl, pppsRequired, ulChildIndex);
}

// EOF

