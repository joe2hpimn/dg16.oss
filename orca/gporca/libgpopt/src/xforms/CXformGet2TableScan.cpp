//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CXformGet2TableScan.cpp
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
#include "gpopt/xforms/CXformGet2TableScan.h"

#include "gpopt/operators/ops.h"
#include "gpopt/metadata/CTableDescriptor.h"

using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CXformGet2TableScan::CXformGet2TableScan
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CXformGet2TableScan::CXformGet2TableScan
	(
	IMemoryPool *pmp
	)
	:
	CXformImplementation
		(
		 // pattern
		GPOS_NEW(pmp) CExpression
				(
				pmp,
				GPOS_NEW(pmp) CLogicalGet(pmp)
				)
		)
{}

//---------------------------------------------------------------------------
//	@function:
//		CXformGet2TableScan::Exfp
//
//	@doc:
//		Compute promise of xform
//
//---------------------------------------------------------------------------
CXform::EXformPromise 
CXformGet2TableScan::Exfp
	(
	CExpressionHandle &exprhdl
	)
	const
{
	CLogicalGet *popGet = CLogicalGet::PopConvert(exprhdl.Pop());
	
	CTableDescriptor *ptabdesc = popGet->Ptabdesc();
	if (ptabdesc->FPartitioned())
	{
		return CXform::ExfpNone;
	}
	
	return CXform::ExfpHigh;
}


//---------------------------------------------------------------------------
//	@function:
//		CXformGet2TableScan::Transform
//
//	@doc:
//		Actual transformation
//
//---------------------------------------------------------------------------
void
CXformGet2TableScan::Transform
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

	CLogicalGet *popGet = CLogicalGet::PopConvert(pexpr->Pop());
	IMemoryPool *pmp = pxfctxt->Pmp();

	// create/extract components for alternative
	CName *pname = GPOS_NEW(pmp) CName(pmp, popGet->Name());
	
	CTableDescriptor *ptabdesc = popGet->Ptabdesc();
	ptabdesc->AddRef();
	
	DrgPcr *pdrgpcrOutput = popGet->PdrgpcrOutput();
	GPOS_ASSERT(NULL != pdrgpcrOutput);

	pdrgpcrOutput->AddRef();
	
	// create alternative expression
	CExpression *pexprAlt = 
		GPOS_NEW(pmp) CExpression
			(
			pmp,
			GPOS_NEW(pmp) CPhysicalTableScan(pmp, pname, ptabdesc, pdrgpcrOutput)
			);
	// add alternative to transformation result
	pxfres->Add(pexprAlt);
}


// EOF

