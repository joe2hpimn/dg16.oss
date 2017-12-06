//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2015 Pivotal, Inc.
//
//	@filename:
//		CXformInnerJoinWithInnerSelect2BitmapIndexGetApply.cpp
//
//	@doc:
//		Transform Inner Join with a Select on the inner branch to
//		Bitmap IndexGet Apply
//
//	@owner:
//		n
//
//	@test:
//
//---------------------------------------------------------------------------

#include "gpos/base.h"

#include "gpopt/operators/ops.h"
#include "gpopt/xforms/CXformInnerJoinWithInnerSelect2BitmapIndexGetApply.h"


using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CXformInnerJoinWithInnerSelect2BitmapIndexGetApply::
//		CXformInnerJoinWithInnerSelect2BitmapIndexGetApply
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CXformInnerJoinWithInnerSelect2BitmapIndexGetApply::CXformInnerJoinWithInnerSelect2BitmapIndexGetApply
	(
	IMemoryPool *pmp
	)
	:
	// pattern
	CXformInnerJoin2IndexApply
		(
		GPOS_NEW(pmp) CExpression
				(
				pmp,
				GPOS_NEW(pmp) CLogicalInnerJoin(pmp),
				GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CPatternLeaf(pmp)), // outer child
				GPOS_NEW(pmp) CExpression  // inner child must be a Select operator
						(
						pmp,
						GPOS_NEW(pmp) CLogicalSelect(pmp),
						GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CLogicalGet(pmp)),
						GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CPatternTree(pmp))  // predicate
						),
				GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CPatternTree(pmp))  // predicate tree
				)
		)
{}

//---------------------------------------------------------------------------
//	@function:
//		CXformInnerJoinWithInnerSelect2BitmapIndexGetApply::Transform
//
//	@doc:
//		Actual transformation
//
//---------------------------------------------------------------------------
void
CXformInnerJoinWithInnerSelect2BitmapIndexGetApply::Transform
	(
	CXformContext *pxfctxt,
	CXformResult *pxfres,
	CExpression * pexpr
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

	GPOS_ASSERT(COperator::EopLogicalSelect == pexprInner->Pop()->Eopid());
	CExpression *pexprGet = (*pexprInner)[0];
	GPOS_ASSERT(COperator::EopLogicalGet == pexprGet->Pop()->Eopid());

	CTableDescriptor *ptabdescInner = CLogicalGet::PopConvert (pexprGet->Pop ())->Ptabdesc();
	CExpression *pexprAllPredicates = CPredicateUtils::PexprConjunction(pmp, pexprScalar, (*pexprInner)[1]);
	CreateHomogeneousIndexApplyAlternatives
		(
		pmp,
		pexpr->Pop()->UlOpId(),
		pexprOuter,
		pexprGet,
		pexprAllPredicates,
		ptabdescInner,
		NULL, // popDynamicGet
		pxfres,
		IMDIndex::EmdindBitmap
		);
	pexprAllPredicates->Release();
}

// EOF
