//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CQueryContext.cpp
//
//	@doc:
//		Implementation of optimization context
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/base.h"

#include "gpopt/base/CColumnFactory.h"
#include "gpopt/base/CColRefSetIter.h"
#include "gpopt/base/CDistributionSpecAny.h"
#include "gpopt/base/CQueryContext.h"
#include "gpopt/base/COptCtxt.h"
#include "gpopt/operators/CLogicalLimit.h"

using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CQueryContext::CQueryContext
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CQueryContext::CQueryContext
	(
	IMemoryPool *pmp,
	CExpression *pexpr,
	CReqdPropPlan *prpp,
	DrgPcr *pdrgpcr,
	DrgPmdname *pdrgpmdname,
	BOOL fDeriveStats
	)
	:
	m_pmp(pmp),
	m_prpp(prpp),
	m_pdrgpcr(pdrgpcr),
	m_pdrgpcrSystemCols(NULL),
	m_pdrgpmdname(pdrgpmdname),
	m_fDeriveStats(fDeriveStats)
{
	GPOS_ASSERT(NULL != pexpr);
	GPOS_ASSERT(NULL != prpp);
	GPOS_ASSERT(NULL != pdrgpcr);
	GPOS_ASSERT(NULL != pdrgpmdname);
	GPOS_ASSERT(pdrgpcr->UlLength() == pdrgpmdname->UlLength());

#ifdef GPOS_DEBUG
	const ULONG ulReqdColumns = m_pdrgpcr->UlLength();
#endif //GPOS_DEBUG

	// mark unused CTEs
	CCTEInfo *pcteinfo = COptCtxt::PoctxtFromTLS()->Pcteinfo();
	pcteinfo->MarkUnusedCTEs();

	CColRefSet *pcrsOutputAndOrderingCols = GPOS_NEW(pmp) CColRefSet(pmp);
	CColRefSet *pcrsOrderSpec = prpp->Peo()->PosRequired()->PcrsUsed(pmp);

	pcrsOutputAndOrderingCols->Include(pdrgpcr);
	pcrsOutputAndOrderingCols->Include(pcrsOrderSpec);
	pcrsOrderSpec->Release();

	m_pexpr = CExpressionPreprocessor::PexprPreprocess(pmp, pexpr, pcrsOutputAndOrderingCols);

	pcrsOutputAndOrderingCols->Release();
	GPOS_ASSERT(m_pdrgpcr->UlLength() == ulReqdColumns);

	// collect required system columns
	SetSystemCols(pmp);

	// collect CTE predicates and add them to CTE producer expressions
	CExpressionPreprocessor::AddPredsToCTEProducers(pmp, m_pexpr);

	CColumnFactory *pcf = COptCtxt::PoctxtFromTLS()->Pcf();

	// create the mapping between the computed column, defined in the expression
	// and all CTEs, and its corresponding used columns
	MapComputedToUsedCols(pcf, m_pexpr);
	pcteinfo->MapComputedToUsedCols(pcf);
}


//---------------------------------------------------------------------------
//	@function:
//		CQueryContext::~CQueryContext
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CQueryContext::~CQueryContext()
{
	m_pexpr->Release();
	m_prpp->Release();
	m_pdrgpcr->Release();
	m_pdrgpmdname->Release();
	CRefCount::SafeRelease(m_pdrgpcrSystemCols);
}


//---------------------------------------------------------------------------
//	@function:
//		CQueryContext::PopTop
//
//	@doc:
// 		 Return top level operator in the given expression
//
//---------------------------------------------------------------------------
COperator *
CQueryContext::PopTop
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	// skip CTE anchors if any
	CExpression *pexprCurr = pexpr;
	while (COperator::EopLogicalCTEAnchor == pexprCurr->Pop()->Eopid())
	{
		pexprCurr = (*pexprCurr)[0];
		GPOS_ASSERT(NULL != pexprCurr);
	}

	return pexprCurr->Pop();
}

//---------------------------------------------------------------------------
//	@function:
//		CQueryContext::SetReqdSystemCols
//
//	@doc:
// 		Collect system columns from output columns
//
//---------------------------------------------------------------------------
void
CQueryContext::SetSystemCols
	(
	IMemoryPool *pmp
	)
{
	GPOS_ASSERT(NULL == m_pdrgpcrSystemCols);
	GPOS_ASSERT(NULL != m_pdrgpcr);

	m_pdrgpcrSystemCols = GPOS_NEW(pmp) DrgPcr(pmp);
	const ULONG ulReqdCols = m_pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulReqdCols; ul++)
	{
		CColRef *pcr = (*m_pdrgpcr)[ul];
		if (pcr->FSystemCol())
		{
			m_pdrgpcrSystemCols->Append(pcr);
		}
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CQueryContext::PqcGenerate
//
//	@doc:
// 		Generate the query context for the given expression and array of
//		output column ref ids
//
//---------------------------------------------------------------------------
CQueryContext *
CQueryContext::PqcGenerate
	(
	IMemoryPool *pmp,
	CExpression * pexpr,
	DrgPul *pdrgpulQueryOutputColRefId,
	DrgPmdname *pdrgpmdname,
	BOOL fDeriveStats
	)
{
	GPOS_ASSERT(NULL != pexpr && NULL != pdrgpulQueryOutputColRefId);

	CColRefSet *pcrs = GPOS_NEW(pmp) CColRefSet(pmp);
	DrgPcr *pdrgpcr = GPOS_NEW(pmp) DrgPcr(pmp);

	COptCtxt *poptctxt = COptCtxt::PoctxtFromTLS();
	CColumnFactory *pcf = poptctxt->Pcf();
	GPOS_ASSERT(NULL != pcf);

	const ULONG ulLen = pdrgpulQueryOutputColRefId->UlLength();
	for (ULONG ul = 0; ul < ulLen; ul++)
	{
		ULONG *pul = (*pdrgpulQueryOutputColRefId)[ul];
		GPOS_ASSERT(NULL != pul);

		CColRef *pcr = pcf->PcrLookup(*pul);
		GPOS_ASSERT(NULL != pcr);

		pcrs->Include(pcr);
		pdrgpcr->Append(pcr);
	}

	COrderSpec *pos = NULL;
	CExpression *pexprResult = pexpr;
	COperator *popTop = PopTop(pexpr);
	if (COperator::EopLogicalLimit == popTop->Eopid())
	{
		// top level operator is a limit, copy order spec to query context
		pos = CLogicalLimit::PopConvert(popTop)->Pos();
		pos->AddRef();
	}
	else
	{
		// no order required
		pos = GPOS_NEW(pmp) COrderSpec(pmp);
	}

	CDistributionSpec *pds = NULL;
	
	BOOL fDML = CUtils::FLogicalDML(pexpr->Pop());
	poptctxt->MarkDMLQuery(fDML);

	if (fDML)
	{
		pds = GPOS_NEW(pmp) CDistributionSpecAny();
	}
	else
	{
		pds = GPOS_NEW(pmp) CDistributionSpecSingleton(CDistributionSpecSingleton::EstMaster);
	}

	CRewindabilitySpec *prs = GPOS_NEW(pmp) CRewindabilitySpec(CRewindabilitySpec::ErtNone /*ert*/);

	CEnfdOrder *peo = GPOS_NEW(pmp) CEnfdOrder(pos, CEnfdOrder::EomSatisfy);

	// we require satisfy matching on distribution since final query results must be sent to master
	CEnfdDistribution *ped = GPOS_NEW(pmp) CEnfdDistribution(pds, CEnfdDistribution::EdmSatisfy);

	CEnfdRewindability *per = GPOS_NEW(pmp) CEnfdRewindability(prs, CEnfdRewindability::ErmSatisfy);

	CCTEReq *pcter = poptctxt->Pcteinfo()->PcterProducers(pmp);

	CReqdPropPlan *prpp = GPOS_NEW(pmp) CReqdPropPlan(pcrs, peo, ped, per, pcter);

	pdrgpmdname->AddRef();
	return GPOS_NEW(pmp) CQueryContext(pmp, pexprResult, prpp, pdrgpcr, pdrgpmdname, fDeriveStats);
}

#ifdef GPOS_DEBUG

//---------------------------------------------------------------------------
//	@function:
//		CQueryContext::OsPrint
//
//	@doc:
//		Debug print
//
//---------------------------------------------------------------------------
IOstream &
CQueryContext::OsPrint
	(
	IOstream &os
	)
	const
{
	return os << *m_pexpr << std::endl << *m_prpp;
}

#endif // GPOS_DEBUG


//---------------------------------------------------------------------------
//	@function:
//		CQueryContext::MapComputedToUsedCols
//
//	@doc:
//		Walk the expression and add the mapping between computed column
//		and its used columns
//
//---------------------------------------------------------------------------
void
CQueryContext::MapComputedToUsedCols
	(
	CColumnFactory *pcf,
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	if (COperator::EopLogicalProject == pexpr->Pop()->Eopid())
	{
		CExpression *pexprPrL = (*pexpr)[1];

		const ULONG ulArity = pexprPrL->UlArity();
		for (ULONG ul = 0; ul < ulArity; ul++)
		{
			CExpression *pexprPrEl = (*pexprPrL)[ul];
			pcf->AddComputedToUsedColsMap(pexprPrEl);
		}
	}

	// process children
	const ULONG ulChildren = pexpr->UlArity();
	for (ULONG ul = 0; ul < ulChildren; ul++)
	{
		MapComputedToUsedCols(pcf, (*pexpr)[ul]);
	}
}

// EOF

