//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CXformInlineCTEConsumer.cpp
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
#include "gpopt/xforms/CXformInlineCTEConsumer.h"
#include "gpopt/xforms/CXformUtils.h"

#include "gpopt/operators/ops.h"

using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CXformInlineCTEConsumer::CXformInlineCTEConsumer
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CXformInlineCTEConsumer::CXformInlineCTEConsumer
	(
	IMemoryPool *pmp
	)
	:
	CXformExploration
		(
		 // pattern
		GPOS_NEW(pmp) CExpression
				(
				pmp,
				GPOS_NEW(pmp) CLogicalCTEConsumer(pmp)
				)
		)
{}

//---------------------------------------------------------------------------
//	@function:
//		CXformInlineCTEConsumer::Exfp
//
//	@doc:
//		Compute promise of xform
//
//---------------------------------------------------------------------------
CXform::EXformPromise
CXformInlineCTEConsumer::Exfp
	(
	CExpressionHandle &exprhdl
	)
	const
{
	const ULONG ulId = CLogicalCTEConsumer::PopConvert(exprhdl.Pop())->UlCTEId();
	CCTEInfo *pcteinfo = COptCtxt::PoctxtFromTLS()->Pcteinfo();

	if ((pcteinfo->FEnableInlining() || 1 == pcteinfo->UlConsumers(ulId)) &&
		CXformUtils::FInlinableCTE(ulId))
	{
		return CXform::ExfpHigh;
	}

	return CXform::ExfpNone;
}

//---------------------------------------------------------------------------
//	@function:
//		CXformInlineCTEConsumer::Transform
//
//	@doc:
//		Actual transformation
//
//---------------------------------------------------------------------------
void
CXformInlineCTEConsumer::Transform
	(
	CXformContext *
#ifdef GPOS_DEBUG
	pxfctxt
#endif
	,
	CXformResult *pxfres,
	CExpression *pexpr
	)
	const
{
	GPOS_ASSERT(NULL != pxfctxt);
	GPOS_ASSERT(FPromising(pxfctxt->Pmp(), this, pexpr));
	GPOS_ASSERT(FCheckPattern(pexpr));

	// inline the consumer
	CLogicalCTEConsumer *popConsumer = CLogicalCTEConsumer::PopConvert(pexpr->Pop());
	CExpression *pexprAlt = popConsumer->PexprInlined();
	pexprAlt->AddRef();
	// add alternative to xform result
	pxfres->Add(pexprAlt);
}

// EOF
