//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CPhysicalScalarAgg.cpp
//
//	@doc:
//		Implementation of scalar aggregation operator
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
#include "gpopt/base/COptCtxt.h"
#include "gpopt/base/CDistributionSpecHashed.h"
#include "gpopt/base/CDistributionSpecSingleton.h"
#include "gpopt/operators/CExpressionHandle.h"
#include "gpopt/operators/CPhysicalScalarAgg.h"

using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalScalarAgg::CPhysicalScalarAgg
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CPhysicalScalarAgg::CPhysicalScalarAgg
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr,
	DrgPcr *pdrgpcrMinimal,
	COperator::EGbAggType egbaggtype,
	BOOL fGeneratesDuplicates,
	DrgPcr *pdrgpcrArgDQA,
	BOOL fMultiStage
	)
	:
	CPhysicalAgg(pmp, pdrgpcr, pdrgpcrMinimal, egbaggtype, fGeneratesDuplicates, pdrgpcrArgDQA, fMultiStage)
{}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalScalarAgg::~CPhysicalScalarAgg
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CPhysicalScalarAgg::~CPhysicalScalarAgg()
{}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalScalarAgg::PosRequired
//
//	@doc:
//		Compute required sort columns of the n-th child
//
//---------------------------------------------------------------------------
COrderSpec *
CPhysicalScalarAgg::PosRequired
	(
	IMemoryPool *pmp,
	CExpressionHandle &, // exprhdl
	COrderSpec *, // posRequired
	ULONG
#ifdef GPOS_DEBUG
	ulChildIndex
#endif // GPOS_DEBUG
	,
	DrgPdp *, // pdrgpdpCtxt
	ULONG // ulOptReq
	)
	const
{
	GPOS_ASSERT(0 == ulChildIndex);

	// return empty sort order
	return GPOS_NEW(pmp) COrderSpec(pmp);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalScalarAgg::PosDerive
//
//	@doc:
//		Derive sort order
//
//---------------------------------------------------------------------------
COrderSpec *
CPhysicalScalarAgg::PosDerive
	(
	IMemoryPool *pmp,
	CExpressionHandle & // exprhdl
	)
	const
{
	// return empty sort order
	return GPOS_NEW(pmp) COrderSpec(pmp);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalScalarAgg::EpetOrder
//
//	@doc:
//		Return the enforcing type for order property based on this operator
//
//---------------------------------------------------------------------------
CEnfdProp::EPropEnforcingType
CPhysicalScalarAgg::EpetOrder
	(
	CExpressionHandle &, // exprhdl
	const CEnfdOrder *
#ifdef GPOS_DEBUG
	peo
#endif // GPOS_DEBUG
	)
	const
{
	GPOS_ASSERT(NULL != peo);
	GPOS_ASSERT(!peo->PosRequired()->FEmpty());

	// TODO: , 06/20/2012: scalar agg produces one row, and hence it should satisfy any order;
	// a problem happens if we have a NLJ(R,S) where R is Salar Agg, and we require sorting on the
	// agg on top of NLJ, in this case we should satisfy this requirement without introducing a Sort
	// even though the NLJ's max card may be > 1
	return CEnfdProp::EpetRequired;
}

// EOF
