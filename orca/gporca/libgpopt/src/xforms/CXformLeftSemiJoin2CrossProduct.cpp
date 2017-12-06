//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2013 EMC Corp.
//
//	@filename:
//		CXformLeftSemiJoin2CrossProduct.cpp
//
//	@doc:
//		Implementation of transform
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/base.h"


#include "gpopt/operators/ops.h"
#include "gpopt/operators/CNormalizer.h"
#include "gpopt/xforms/CXformLeftSemiJoin2CrossProduct.h"
#include "gpopt/xforms/CXformUtils.h"



using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CXformLeftSemiJoin2CrossProduct::CXformLeftSemiJoin2CrossProduct
//
//	@doc:
//		ctor
//
//---------------------------------------------------------------------------
CXformLeftSemiJoin2CrossProduct::CXformLeftSemiJoin2CrossProduct
	(
	IMemoryPool *pmp
	)
	:
	// pattern
	CXformExploration
		(
		GPOS_NEW(pmp) CExpression
					(
					pmp,
					GPOS_NEW(pmp) CLogicalLeftSemiJoin(pmp),
					GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CPatternTree(pmp)), // left child is a tree since we may need to push predicates down
					GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CPatternLeaf(pmp)), // right child
					GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CPatternTree(pmp))  // predicate is a tree since we may need to do clean-up of scalar expression
					)
		)
{}


//---------------------------------------------------------------------------
//	@function:
//		CXformLeftSemiJoin2CrossProduct::Exfp
//
//	@doc:
//		 Compute xform promise for a given expression handle
//
//---------------------------------------------------------------------------
CXform::EXformPromise
CXformLeftSemiJoin2CrossProduct::Exfp
	(
	CExpressionHandle &exprhdl
	)
	const
{
	return CXformUtils::ExfpSemiJoin2CrossProduct(exprhdl);
}


//---------------------------------------------------------------------------
//	@function:
//		CXformLeftSemiJoin2CrossProduct::Transform
//
//	@doc:
//		Semi join whose join predicate does not use columns from join's inner
//		child is equivalent to a Cross Product between outer child (after
//		pushing join predicate), and one tuple from inner child
//
//---------------------------------------------------------------------------
void
CXformLeftSemiJoin2CrossProduct::Transform
	(
	CXformContext *pxfctxt,
	CXformResult *pxfres,
	CExpression *pexpr
	)
	const
{
	GPOS_ASSERT(NULL != pxfctxt);
	GPOS_ASSERT(FPromising(pxfctxt->Pmp(), this, pexpr));
	GPOS_ASSERT(FCheckPattern(pexpr));

	IMemoryPool *pmp = pxfctxt->Pmp();

	// extract components
	CExpression *pexprOuter = (*pexpr)[0];
	CExpression *pexprInner = (*pexpr)[1];
	CExpression *pexprScalar = (*pexpr)[2];
	pexprOuter->AddRef();
	pexprInner->AddRef();
	pexprScalar->AddRef();

	// create a (limit 1) on top of inner child
	CExpression *pexprLimitOffset = CUtils::PexprScalarConstInt8(pmp, 0 /*iVal*/);
	CExpression *pexprLimitCount = CUtils::PexprScalarConstInt8(pmp, 1 /*iVal*/);
	COrderSpec *pos = GPOS_NEW(pmp) COrderSpec(pmp);
	CLogicalLimit *popLimit =
			GPOS_NEW(pmp) CLogicalLimit(pmp, pos, true /*fGlobal*/, true /*fHasCount*/, false /*fNonRemovableLimit*/);
	CExpression *pexprLimit = GPOS_NEW(pmp) CExpression(pmp, popLimit, pexprInner, pexprLimitOffset, pexprLimitCount);

	// create cross product
	CExpression *pexprJoin = CUtils::PexprLogicalJoin<CLogicalInnerJoin>(pmp, pexprOuter, pexprLimit, pexprScalar);
	CExpression *pexprNormalized = CNormalizer::PexprNormalize(pmp, pexprJoin);
	pexprJoin->Release();

	pxfres->Add(pexprNormalized);
}


// EOF
