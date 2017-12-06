//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2013 Pivotal, Inc.
//
//	@filename:
//		CPhysicalHashAggDeduplicate.cpp
//
//	@doc:
//		Implementation of Hash Aggregate operator for deduplicating join outputs
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/base.h"

#include "gpopt/operators/CExpressionHandle.h"
#include "gpopt/operators/CPhysicalHashAggDeduplicate.h"
#include "gpopt/base/CDistributionSpecAny.h"
#include "gpopt/base/CDistributionSpecRandom.h"
#include "gpopt/operators/CLogicalGbAgg.h"

using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalHashAggDeduplicate::CPhysicalHashAggDeduplicate
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CPhysicalHashAggDeduplicate::CPhysicalHashAggDeduplicate
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr,
	DrgPcr *pdrgpcrMinimal,
	COperator::EGbAggType egbaggtype,
	DrgPcr *pdrgpcrKeys,
	BOOL fGeneratesDuplicates,
	BOOL fMultiStage
	)
	:
	CPhysicalHashAgg(pmp, pdrgpcr, pdrgpcrMinimal, egbaggtype, fGeneratesDuplicates, NULL /*pdrgpcrGbMinusDistinct*/, fMultiStage),
	m_pdrgpcrKeys(pdrgpcrKeys)
{
	GPOS_ASSERT(NULL != pdrgpcrKeys);
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalHashAggDeduplicate::~CPhysicalHashAggDeduplicate
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CPhysicalHashAggDeduplicate::~CPhysicalHashAggDeduplicate()
{
	m_pdrgpcrKeys->Release();
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalHashAggDeduplicate::OsPrint
//
//	@doc:
//		Debug print
//
//---------------------------------------------------------------------------
IOstream &
CPhysicalHashAggDeduplicate::OsPrint
	(
	IOstream &os
	)
	const
{
	if (m_fPattern)
	{
		return COperator::OsPrint(os);
	}

	os	<< SzId()
		<< "( ";
	CLogicalGbAgg::OsPrintGbAggType(os, Egbaggtype());
	os	<< " )"
		<< " Grp Cols: [";

	CUtils::OsPrintDrgPcr(os, PdrgpcrGroupingCols());
	os	<< "]"
		<< ", Key Cols:[";
	CUtils::OsPrintDrgPcr(os, m_pdrgpcrKeys);
	os	<< "]";

	os	<< ", Generates Duplicates :[ " << FGeneratesDuplicates() << " ] ";

	return os;
}

// EOF

// EOF
