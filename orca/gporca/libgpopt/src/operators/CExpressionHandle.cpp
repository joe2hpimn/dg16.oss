//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CExpressionHandle.cpp
//
//	@doc:
//		Handle to an expression to abstract topology;
//
//		The handle provides access to an expression and the properties
//		of its children; regardless of whether the expression is a group
//		expression or a stand-alone tree;
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/base.h"


#include "gpopt/base/CColRefSet.h"
#include "gpopt/base/CCostContext.h"
#include "gpopt/base/CCTEReq.h"
#include "gpopt/base/CDrvdPropCtxtPlan.h"
#include "gpopt/base/CDrvdPropScalar.h"
#include "gpopt/base/CReqdPropPlan.h"
#include "gpopt/base/CUtils.h"
#include "gpopt/operators/CExpressionHandle.h"
#include "gpopt/operators/CLogical.h"
#include "gpopt/operators/CLogicalGbAgg.h"
#include "gpopt/operators/COperator.h"
#include "gpopt/operators/CPattern.h"
#include "gpopt/operators/CPhysicalScan.h"
#include "gpopt/operators/CLogicalCTEConsumer.h"
#include "gpopt/operators/CPhysicalCTEConsumer.h"
#include "gpopt/base/COptCtxt.h"

#include "naucrates/statistics/CStatisticsUtils.h"

using namespace gpnaucrates;
using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::CExpressionHandle
//
//	@doc:
//		ctor
//
//---------------------------------------------------------------------------
CExpressionHandle::CExpressionHandle
	(
	IMemoryPool *pmp
	)
	:
	m_pmp(pmp),
	m_pexpr(NULL),
	m_pgexpr(NULL),
	m_pcc(NULL),
	m_pdp(NULL),
	m_pstats(NULL),
	m_prp(NULL),
	m_pdrgpdp(NULL),
	m_pdrgpstat(NULL),
	m_pdrgprp(NULL)
{
	GPOS_ASSERT(NULL != pmp);
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::~CExpressionHandle
//
//	@doc:
//		dtor
//
//		Since handles live on the stack this dtor will be called during
//		exceptions, hence, need to be defensive
//
//---------------------------------------------------------------------------
CExpressionHandle::~CExpressionHandle()
{
	CRefCount::SafeRelease(m_pexpr);
	CRefCount::SafeRelease(m_pgexpr);
	CRefCount::SafeRelease(m_pdp);
	CRefCount::SafeRelease(m_pstats);
	CRefCount::SafeRelease(m_prp);
	CRefCount::SafeRelease(m_pdrgpdp);
	CRefCount::SafeRelease(m_pdrgpstat);
	CRefCount::SafeRelease(m_pdrgprp);
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::FStatsDerived
//
//	@doc:
//		Check if stats are derived for attached expression and its children
//
//---------------------------------------------------------------------------
BOOL
CExpressionHandle::FStatsDerived() const
{
	IStatistics *pstats = NULL;
	if (NULL != m_pexpr)
	{
		pstats = const_cast<IStatistics *>(m_pexpr->Pstats());
	}
	else
	{
		GPOS_ASSERT(NULL != m_pgexpr);
		pstats = m_pgexpr->Pgroup()->Pstats();
	}

	if (NULL == pstats)
	{
		// stats of attached expression have not been derived yet
		return false;
	}

	const ULONG ulArity = UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		if (FScalarChild(ul))
		{
			// skip scalar children
			continue;
		}

		IStatistics *pstatsChild = NULL;
		if (NULL != m_pexpr)
		{
			pstatsChild = const_cast<IStatistics *>((*m_pexpr)[ul]->Pstats());
		}
		else
		{
			pstatsChild = (*m_pgexpr)[ul]->Pstats();
		}

		if (NULL == pstatsChild)
		{
			// stats of attached expression child have not been derived yet
			return false;
		}
	}

	return true;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::CopyStats
//
//	@doc:
//		Copy stats from attached expression/group expression to local stats
//		members
//
//---------------------------------------------------------------------------
void
CExpressionHandle::CopyStats()
{
	if (!FStatsDerived())
	{
		// stats of attached expression (or its children) have not been derived yet
		return;
	}

	IStatistics *pstats = NULL;
	if (NULL != m_pexpr)
	{
		pstats = const_cast<IStatistics *>(m_pexpr->Pstats());
	}
	else
	{
		GPOS_ASSERT(NULL != m_pgexpr);
		pstats = m_pgexpr->Pgroup()->Pstats();
	}
	GPOS_ASSERT(NULL != pstats);

	// attach stats
	pstats->AddRef();
	GPOS_ASSERT(NULL == m_pstats);
	m_pstats = pstats;

	// attach child stats
	GPOS_ASSERT(NULL == m_pdrgpstat);
	m_pdrgpstat = GPOS_NEW(m_pmp) DrgPstat(m_pmp);
	const ULONG ulArity = UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		IStatistics *pstatsChild = NULL;
		if (NULL != m_pexpr)
		{
			pstatsChild = const_cast<IStatistics *>((*m_pexpr)[ul]->Pstats());
		}
		else
		{
			pstatsChild = (*m_pgexpr)[ul]->Pstats();
		}

		if (NULL != pstatsChild)
		{
			pstatsChild->AddRef();
		}
		else
		{
			GPOS_ASSERT(FScalarChild(ul));

			// create dummy stats for missing scalar children
			pstatsChild = CStatistics::PstatsEmpty(m_pmp);
		}

		m_pdrgpstat->Append(pstatsChild);
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::Attach
//
//	@doc:
//		Attach to a given expression
//
//---------------------------------------------------------------------------
void
CExpressionHandle::Attach
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL == m_pexpr);
	GPOS_ASSERT(NULL == m_pgexpr);
	GPOS_ASSERT(NULL != pexpr);

	// increment ref count on base expression
	pexpr->AddRef();
	m_pexpr = pexpr;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::Attach
//
//	@doc:
//		Attach to a given group expression
//
//---------------------------------------------------------------------------
void
CExpressionHandle::Attach
	(
	CGroupExpression *pgexpr
	)
{
	GPOS_ASSERT(NULL == m_pexpr);
	GPOS_ASSERT(NULL == m_pgexpr);
	GPOS_ASSERT(NULL != pgexpr);

	// increment ref count on group expression
	pgexpr->AddRef();
	m_pgexpr = pgexpr;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::Attach
//
//	@doc:
//		Attach to a given cost context
//
//---------------------------------------------------------------------------
void
CExpressionHandle::Attach
	(
	CCostContext *pcc
	)
{
	GPOS_ASSERT(NULL == m_pcc);
	GPOS_ASSERT(NULL != pcc);

	m_pcc = pcc;
	Attach(pcc->Pgexpr());
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::CopyGroupProps
//
//	@doc:
//		Cache properties of group and its children on the handle
//
//---------------------------------------------------------------------------
void
CExpressionHandle::CopyGroupProps()
{
	GPOS_ASSERT(NULL != m_pgexpr);
	GPOS_ASSERT(NULL == m_pdrgpdp);
	GPOS_ASSERT(NULL == m_pdp);

	// add-ref group properties
	CDrvdProp *pdp = m_pgexpr->Pgroup()->Pdp();
	pdp->AddRef();
	m_pdp = pdp;

	// add-ref child groups' properties
	const ULONG ulArity = UlArity();
	m_pdrgpdp = GPOS_NEW(m_pmp) DrgPdp(m_pmp, ulArity);
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		CDrvdProp *pdpChild = (*m_pgexpr)[ul]->Pdp();
		pdpChild->AddRef();
		m_pdrgpdp->Append(pdpChild);
	}
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::CopyExprProps
//
//	@doc:
//		Cache properties of expression and its children on the handle
//
//---------------------------------------------------------------------------
void
CExpressionHandle::CopyExprProps()
{
	GPOS_ASSERT(NULL != m_pexpr);
	GPOS_ASSERT(NULL == m_pdrgpdp);
	GPOS_ASSERT(NULL == m_pdp);

	// add-ref expression properties
	CDrvdProp *pdp = m_pexpr->PdpDerive();
	pdp->AddRef();
	m_pdp = pdp;

	// add-ref child expressions' properties
	const ULONG ulArity = UlArity();
	m_pdrgpdp = GPOS_NEW(m_pmp) DrgPdp(m_pmp, ulArity);
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		CDrvdProp *pdpChild = (*m_pexpr)[ul]->PdpDerive();
		pdpChild->AddRef();
		m_pdrgpdp->Append(pdpChild);
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::CopyCostCtxtProps
//
//	@doc:
//		Cache plan properties of cost context and its children on the handle
//
//---------------------------------------------------------------------------
void
CExpressionHandle::CopyCostCtxtProps()
{
	GPOS_ASSERT(NULL != m_pcc);
	GPOS_ASSERT(NULL == m_pdrgpdp);
	GPOS_ASSERT(NULL == m_pdp);

	// add-ref context properties
	CDrvdProp *pdp = m_pcc->Pdpplan();
	pdp->AddRef();
	m_pdp = pdp;

	// add-ref child group expressions' properties
	const ULONG ulArity = UlArity();
	m_pdrgpdp = GPOS_NEW(m_pmp) DrgPdp(m_pmp, ulArity);
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		CGroup *pgroupChild = (*m_pgexpr)[ul];
		if (!pgroupChild->FScalar())
		{
			COptimizationContext *pocChild = (*m_pcc->Pdrgpoc())[ul];
			GPOS_ASSERT(NULL != pocChild);

			CCostContext *pccChild = pocChild->PccBest();
			GPOS_ASSERT(NULL != pccChild);

			pdp = pccChild->Pdpplan();
			pdp->AddRef();
			m_pdrgpdp->Append(pdp);
		}
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::DeriveProps
//
//	@doc:
//		Recursive property derivation
//
//---------------------------------------------------------------------------
void
CExpressionHandle::DeriveProps
	(
	CDrvdPropCtxt *pdpctxt
	)
{
	GPOS_ASSERT(NULL == m_pdrgpdp);
	GPOS_ASSERT(NULL == m_pdp);
	GPOS_CHECK_ABORT;

	if (NULL != m_pgexpr)
	{
		CopyGroupProps();
		return;
	}
	GPOS_ASSERT(NULL != m_pexpr);

	// check if expression already has derived props
	if (NULL != m_pexpr->Pdp(m_pexpr->Ept()))
	{
		CopyExprProps();
		return;
	}

	// copy stats of attached expression
	CopyStats();

	// extract children's properties
	m_pdrgpdp = GPOS_NEW(m_pmp) DrgPdp(m_pmp);
	const ULONG ulArity = m_pexpr->UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		CExpression *pexprChild = (*m_pexpr)[ul];
		CDrvdProp *pdp = pexprChild->PdpDerive(pdpctxt);
		pdp->AddRef();
		m_pdrgpdp->Append(pdp);

		// add child props to derivation context
		CDrvdPropCtxt::AddDerivedProps(pdp, pdpctxt);
	}

	// create/derive local properties
	m_pdp = Pop()->PdpCreate(m_pmp);
	m_pdp->Derive(m_pmp, *this, pdpctxt);
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::PdrgpstatOuterRefs
//
//	@doc:
//		Given an array of stats objects and a child index, return an array
//		of stats objects starting from the first stats object referenced by
//		child
//
//---------------------------------------------------------------------------
DrgPstat *
CExpressionHandle::PdrgpstatOuterRefs
	(
	DrgPstat *pdrgpstat,
	ULONG ulChildIndex
	)
	const
{
	GPOS_ASSERT(NULL != pdrgpstat);
	GPOS_ASSERT(ulChildIndex < UlArity());

	if (FScalarChild(ulChildIndex) || !FHasOuterRefs(ulChildIndex))
	{
		// if child is scalar or has no outer references, return empty array
		return  GPOS_NEW(m_pmp) DrgPstat(m_pmp);
	}

	DrgPstat *pdrgpstatResult = GPOS_NEW(m_pmp) DrgPstat(m_pmp);
	CColRefSet *pcrsOuter = Pdprel(ulChildIndex)->PcrsOuter();
	GPOS_ASSERT(0 < pcrsOuter->CElements());

	const ULONG ulSize = pdrgpstat->UlLength();
	ULONG ulStartIndex = ULONG_MAX;
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		IStatistics *pstats = (*pdrgpstat)[ul];
		CColRefSet *pcrsStats = pstats->Pcrs(m_pmp);
		BOOL fStatsColsUsed = !pcrsOuter->FDisjoint(pcrsStats);
		pcrsStats->Release();
		if (fStatsColsUsed)
		{
			ulStartIndex = ul;
			break;
		}
	}

	if (ULONG_MAX != ulStartIndex)
	{
		// copy stats starting from index of outer-most stats object referenced by child
		CUtils::AddRefAppend<IStatistics, CleanupStats>(pdrgpstatResult, pdrgpstat, ulStartIndex);
	}

	return pdrgpstatResult;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::FAttachedToLeafPattern
//
//	@doc:
//		Return True if handle is attached to a leaf pattern
//
//---------------------------------------------------------------------------
BOOL
CExpressionHandle::FAttachedToLeafPattern() const
{
	return
		0 == UlArity() &&
		NULL != m_pexpr &&
		NULL != m_pexpr->Pgexpr();
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::DeriveRootStats
//
//	@doc:
//		Stat derivation at root operator where handle is attached
//
//---------------------------------------------------------------------------
void
CExpressionHandle::DeriveRootStats
	(
	DrgPstat *pdrgpstatCtxt
	)
{
	GPOS_ASSERT(NULL == m_pstats);

	CLogical *popLogical = CLogical::PopConvert(Pop());
	IStatistics *pstatsRoot = NULL;
	if (FAttachedToLeafPattern())
	{
		// for leaf patterns extracted from memo, trigger state derivation on origin group
		GPOS_ASSERT(NULL != m_pexpr);
		GPOS_ASSERT(NULL != m_pexpr->Pgexpr());

		pstatsRoot = m_pexpr->Pgexpr()->Pgroup()->PstatsRecursiveDerive(m_pmp, m_pmp, CReqdPropRelational::Prprel(m_prp), pdrgpstatCtxt);
		pstatsRoot->AddRef();
	}
	else
	{
		// otherwise, derive stats using root operator
		pstatsRoot = popLogical->PstatsDerive(m_pmp, *this, pdrgpstatCtxt);
	}
	GPOS_ASSERT(NULL != pstatsRoot);

	m_pstats = pstatsRoot;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::DeriveStats
//
//	@doc:
//		Recursive stat derivation
//
//---------------------------------------------------------------------------
void
CExpressionHandle::DeriveStats
	(
	DrgPstat *pdrgpstatCtxt,
	BOOL fComputeRootStats
	)
{
	GPOS_ASSERT(NULL != pdrgpstatCtxt);
	GPOS_ASSERT(NULL == m_pdrgpstat);
	GPOS_ASSERT(NULL == m_pstats);
	GPOS_ASSERT(NULL != m_pdrgprp);

	// copy input context
	DrgPstat *pdrgpstatCurrentCtxt = GPOS_NEW(m_pmp) DrgPstat(m_pmp);
	CUtils::AddRefAppend<IStatistics, CleanupStats>(pdrgpstatCurrentCtxt, pdrgpstatCtxt);

	// create array of children stats
	m_pdrgpstat = GPOS_NEW(m_pmp) DrgPstat(m_pmp);
	ULONG ulMaxChildRisk = 1;
	const ULONG ulArity = UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		// create a new context for outer references used by current child
		DrgPstat *pdrgpstatChildCtxt = PdrgpstatOuterRefs(pdrgpstatCurrentCtxt, ul);

		IStatistics *pstats = NULL;
		if (NULL != Pexpr())
		{
			// derive stats recursively on child expression
			pstats = (*Pexpr())[ul]->PstatsDerive(Prprel(ul), pdrgpstatChildCtxt);
		}
		else
		{
			// derive stats recursively on child group
			pstats = (*Pgexpr())[ul]->PstatsRecursiveDerive(m_pmp, m_pmp, Prprel(ul), pdrgpstatChildCtxt);
		}
		GPOS_ASSERT(NULL != pstats);

		// add child stat to current context
		pstats->AddRef();
		pdrgpstatCurrentCtxt->Append(pstats);
		pdrgpstatChildCtxt->Release();

		// add child stat to children stat array
		pstats->AddRef();
		m_pdrgpstat->Append(pstats);
		if (pstats->UlStatsEstimationRisk() > ulMaxChildRisk)
		{
			ulMaxChildRisk = pstats->UlStatsEstimationRisk();
		}
	}

	if (fComputeRootStats)
	{
		// call stat derivation on operator to compute local stats
		GPOS_ASSERT(NULL == m_pstats);

		DeriveRootStats(pdrgpstatCtxt);
		GPOS_ASSERT(NULL != m_pstats);

		CLogical *popLogical = CLogical::PopConvert(Pop());
		ULONG ulRisk = ulMaxChildRisk;
		if (CStatisticsUtils::FIncreasesRisk(popLogical))
		{
			++ulRisk;
		}
		m_pstats->SetStatsEstimationRisk(ulRisk);
	}

	// clean up current stat context
	pdrgpstatCurrentCtxt->Release();
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::DeriveCostContextStats
//
//	@doc:
//		Stats derivation based on required plan properties
//
//---------------------------------------------------------------------------
void
CExpressionHandle::DeriveCostContextStats()
{
	GPOS_ASSERT(NULL != m_pcc);
	GPOS_ASSERT(NULL == m_pcc->Pstats());

	// copy group properties and stats
	CopyGroupProps();
	CopyStats();

	if (NULL != m_pstats && !m_pcc->FNeedsNewStats())
	{
		// there is no need to derive stats,
		// stats are copied from owner group

		return;
	}

	CEnfdPartitionPropagation *pepp = m_pcc->Poc()->Prpp()->Pepp();
	COperator *pop = Pop();
	if (CUtils::FPhysicalScan(pop) &&
		CPhysicalScan::PopConvert(pop)->FDynamicScan() &&
		!pepp->PpfmDerived()->FEmpty())
	{
		// derive stats on dynamic table scan using stats of part selector
		CPhysicalScan *popScan = CPhysicalScan::PopConvert(m_pgexpr->Pop());
		IStatistics *pstatsDS = popScan->PstatsDerive(m_pmp, *this, m_pcc->Poc()->Prpp(), m_pcc->Poc()->Pdrgpstat());

		CRefCount::SafeRelease(m_pstats);
		m_pstats = pstatsDS;

		return;
	}

	// release current stats since we will derive new stats
	CRefCount::SafeRelease(m_pstats);
	m_pstats = NULL;

	// load stats from child cost context -- these may be different from child groups stats
	CRefCount::SafeRelease(m_pdrgpstat);
	m_pdrgpstat = NULL;

	m_pdrgpstat = GPOS_NEW(m_pmp) DrgPstat(m_pmp);
	const ULONG ulArity = m_pcc->Pdrgpoc()->UlLength();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		COptimizationContext *pocChild = (*m_pcc->Pdrgpoc())[ul];
		CCostContext *pccChild = pocChild->PccBest();
		GPOS_ASSERT(NULL != pccChild);
		GPOS_ASSERT(NULL != pccChild->Pstats());

		pccChild->Pstats()->AddRef();
		m_pdrgpstat->Append(pccChild->Pstats());
	}

	if (CPhysical::PopConvert(m_pgexpr->Pop())->FPassThruStats())
	{
		GPOS_ASSERT(1 == m_pdrgpstat->UlLength());

		// copy stats from first child
		(*m_pdrgpstat)[0]->AddRef();
		m_pstats = (*m_pdrgpstat)[0];

		return;
	}

	// derive stats using the best logical expression with the same children as attached physical operator
	CGroupExpression *pgexprForStats = m_pcc->PgexprForStats();
	GPOS_ASSERT(NULL != pgexprForStats);

	CExpressionHandle exprhdl(m_pmp);
	exprhdl.Attach(pgexprForStats);
	exprhdl.DeriveProps(NULL /*pdpctxt*/);
	m_pdrgpstat->AddRef();
	exprhdl.m_pdrgpstat = m_pdrgpstat;
	exprhdl.ComputeReqdProps(m_pcc->Poc()->Prprel(), 0 /*ulOptReq*/);

	GPOS_ASSERT(NULL == exprhdl.m_pstats);
	IStatistics *pstats = m_pgexpr->Pgroup()->PstatsCompute(m_pcc->Poc(), exprhdl, pgexprForStats);

	// copy stats to main handle
	GPOS_ASSERT(NULL == m_pstats);
	GPOS_ASSERT(NULL != pstats);

	pstats->AddRef();
	m_pstats = pstats;

	GPOS_ASSERT(m_pstats != NULL);
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::DeriveStats
//
//	@doc:
//		Stat derivation using given properties and context
//
//---------------------------------------------------------------------------
void
CExpressionHandle::DeriveStats
	(
	IMemoryPool *pmpLocal,
	IMemoryPool *pmpGlobal,
	CReqdPropRelational *prprel,
	DrgPstat *pdrgpstatCtxt
	)
{
	CReqdPropRelational *prprelNew = prprel;
	if (NULL == prprelNew)
	{
		// create empty property container
		CColRefSet *pcrs = GPOS_NEW(pmpGlobal) CColRefSet(pmpGlobal);
		prprelNew = GPOS_NEW(pmpGlobal) CReqdPropRelational(pcrs);
	}
	else
	{
		prprelNew->AddRef();
	}

	DrgPstat *pdrgpstatCtxtNew = pdrgpstatCtxt;
	if (NULL == pdrgpstatCtxt)
	{
		// create empty context
		pdrgpstatCtxtNew = GPOS_NEW(pmpGlobal) DrgPstat(pmpGlobal);
	}
	else
	{
		pdrgpstatCtxtNew->AddRef();
	}

	if (NULL != Pgexpr())
	{
		(void) Pgexpr()->Pgroup()->PstatsRecursiveDerive(pmpLocal, pmpGlobal, prprelNew, pdrgpstatCtxtNew);
	}
	else
	{
		GPOS_ASSERT(NULL != Pexpr());

		(void) Pexpr()->PstatsDerive(prprelNew, pdrgpstatCtxtNew);
	}

	prprelNew->Release();
	pdrgpstatCtxtNew->Release();
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::DerivePlanProps
//
//	@doc:
//		Derive the properties of the plan carried by attached cost context
//
//---------------------------------------------------------------------------
void
CExpressionHandle::DerivePlanProps
	(
	CDrvdPropCtxtPlan *pdpctxtplan
	)
{
	GPOS_ASSERT(NULL != m_pcc);
	GPOS_ASSERT(NULL != m_pgexpr);
	GPOS_ASSERT(NULL == m_pdrgpdp);
	GPOS_ASSERT(NULL == m_pdp);
	GPOS_CHECK_ABORT;

	// check if properties have been already derived
	if (NULL != m_pcc->Pdpplan())
	{
		CopyCostCtxtProps();
		return;
	}
	GPOS_ASSERT(NULL != pdpctxtplan);

	// extract children's properties
	m_pdrgpdp = GPOS_NEW(m_pmp) DrgPdp(m_pmp);
	const ULONG ulArity = m_pcc->Pdrgpoc()->UlLength();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		COptimizationContext *pocChild = (*m_pcc->Pdrgpoc())[ul];
		CDrvdPropPlan *pdpplan = pocChild->PccBest()->Pdpplan();
		GPOS_ASSERT(NULL != pdpplan);

		pdpplan->AddRef();
		m_pdrgpdp->Append(pdpplan);

		// add child props to derivation context
		CDrvdPropCtxt::AddDerivedProps(pdpplan, pdpctxtplan);
	}

	COperator *pop = m_pgexpr->Pop();
	if (COperator::EopPhysicalCTEConsumer == pop->Eopid())
	{
		// copy producer plan properties to passed derived plan properties context
		ULONG ulCTEId = CPhysicalCTEConsumer::PopConvert(pop)->UlCTEId();
		CDrvdPropPlan *pdpplan = m_pcc->Poc()->Prpp()->Pcter()->Pdpplan(ulCTEId);
		if (NULL != pdpplan)
		{
			pdpctxtplan->CopyCTEProducerProps(pdpplan, ulCTEId);
		}
	}

	// set the number of expected partition selectors in the context
	pdpctxtplan->SetExpectedPartitionSelectors(pop, m_pcc);

	// create/derive local properties
	m_pdp = Pop()->PdpCreate(m_pmp);
	m_pdp->Derive(m_pmp, *this, pdpctxtplan);
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::DerivePlanProps
//
//	@doc:
//		Derive the properties of the plan carried by attached cost context
//
//---------------------------------------------------------------------------
void
CExpressionHandle::DerivePlanProps()
{
	CDrvdPropCtxtPlan *pdpctxtplan = GPOS_NEW(m_pmp) CDrvdPropCtxtPlan(m_pmp);

	// copy stats
	CopyStats();

	DerivePlanProps(pdpctxtplan);
	pdpctxtplan->Release();
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::InitReqdProps
//
//	@doc:
//		Init required properties containers
//
//
//---------------------------------------------------------------------------
void
CExpressionHandle::InitReqdProps
	(
	CReqdProp *prpInput
	)
{
	GPOS_ASSERT(NULL != prpInput);
	GPOS_ASSERT(NULL == m_prp);
	GPOS_ASSERT(NULL == m_pdrgprp);

	// set required properties of attached expr/gexpr
	m_prp = prpInput;
	m_prp->AddRef();
	
	if (m_prp->FPlan())
	{
		CReqdPropPlan *prpp = CReqdPropPlan::Prpp(prpInput);
		if (NULL == prpp->Pepp())
		{
			CPartInfo *ppartinfo = Pdprel()->Ppartinfo();
			prpp->InitReqdPartitionPropagation(m_pmp, ppartinfo);
		}
	}
	
	// compute required properties of children
	m_pdrgprp = GPOS_NEW(m_pmp) DrgPrp(m_pmp);

	// initialize array with input requirements,
	// the initial requirements are only place holders in the array
	// and they are replaced when computing the requirements of each child
	const ULONG ulArity = UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		m_prp->AddRef();
		m_pdrgprp->Append(m_prp);
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::ComputeChildReqdProps
//
//	@doc:
//		Compute required properties of the n-th child
//
//
//---------------------------------------------------------------------------
void
CExpressionHandle::ComputeChildReqdProps
	(
	ULONG ulChildIndex,
	DrgPdp *pdrgpdpCtxt,
	ULONG ulOptReq
	)
{
	GPOS_ASSERT(NULL != m_prp);
	GPOS_ASSERT(NULL != m_pdrgprp);
	GPOS_ASSERT(m_pdrgprp->UlLength() == UlArity());
	GPOS_ASSERT(ulChildIndex < m_pdrgprp->UlLength() && "uninitialized required child properties");
	GPOS_CHECK_ABORT;

	CReqdProp *prp = m_prp;
	if (FScalarChild(ulChildIndex))
	{
		// use local reqd properties to fill scalar child entry in children array
		prp->AddRef();
	}
	else
	{
		// compute required properties based on child type
		prp = Pop()->PrpCreate(m_pmp);
		prp->Compute(m_pmp, *this, m_prp, ulChildIndex, pdrgpdpCtxt, ulOptReq);
	}

	// replace required properties of given child
	m_pdrgprp->Replace(ulChildIndex, prp);
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::CopyChildReqdProps
//
//	@doc:
//		Copy required properties of the n-th child
//
//
//---------------------------------------------------------------------------
void
CExpressionHandle::CopyChildReqdProps
	(
	ULONG ulChildIndex,
	CReqdProp *prp
	)
{
	GPOS_ASSERT(NULL != prp);
	GPOS_ASSERT(NULL != m_pdrgprp);
	GPOS_ASSERT(m_pdrgprp->UlLength() == UlArity());
	GPOS_ASSERT(ulChildIndex < m_pdrgprp->UlLength() && "uninitialized required child properties");

	m_pdrgprp->Replace(ulChildIndex, prp);
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::ComputeChildReqdCols
//
//	@doc:
//		Compute required columns of the n-th child
//
//
//---------------------------------------------------------------------------
void
CExpressionHandle::ComputeChildReqdCols
	(
	ULONG ulChildIndex,
	DrgPdp *pdrgpdpCtxt
	)
{
	GPOS_ASSERT(NULL != m_prp);
	GPOS_ASSERT(NULL != m_pdrgprp);
	GPOS_ASSERT(m_pdrgprp->UlLength() == UlArity());
	GPOS_ASSERT(ulChildIndex < m_pdrgprp->UlLength() && "uninitialized required child properties");

	CReqdProp *prp = m_prp;
	if (FScalarChild(ulChildIndex))
	{
		// use local reqd properties to fill scalar child entry in children array
		prp->AddRef();
	}
	else
	{
		// compute required columns
		prp = Pop()->PrpCreate(m_pmp);
		CReqdPropPlan::Prpp(prp)->ComputeReqdCols(m_pmp, *this, m_prp, ulChildIndex, pdrgpdpCtxt);
	}

	// replace required properties of given child
	m_pdrgprp->Replace(ulChildIndex, prp);
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::ComputeReqdProps
//
//	@doc:
//		Set required properties of attached expr/gexpr, and compute required
//		properties of all children
//
//---------------------------------------------------------------------------
void
CExpressionHandle::ComputeReqdProps
	(
	CReqdProp *prpInput,
	ULONG ulOptReq
	)
{
	InitReqdProps(prpInput);
	const ULONG ulArity = UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		ComputeChildReqdProps(ul, NULL /*pdrgpdpCtxt*/, ulOptReq);
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::FScalarChild
//
//	@doc:
//		Check if a given child is a scalar expression/group
//
//---------------------------------------------------------------------------
BOOL
CExpressionHandle::FScalarChild
	(
	ULONG ulChildIndex
	)
	const
{
	if (NULL != Pexpr())
	{
		return (*Pexpr())[ulChildIndex]->Pop()->FScalar();
	}

	GPOS_ASSERT(NULL != Pgexpr());

	return (*Pgexpr())[ulChildIndex]->FScalar();
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::UlArity
//
//	@doc:
//		Return number of children of attached expression/group expression
//
//---------------------------------------------------------------------------
ULONG
CExpressionHandle::UlArity() const
{
	if (NULL != Pexpr())
	{
		return Pexpr()->UlArity();
	}

	GPOS_ASSERT(NULL != Pgexpr());

	return Pgexpr()->UlArity();
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::UlLastNonScalarChild
//
//	@doc:
//		Return the index of the last non-scalar child. This is only valid if
//		UlArity() is greater than 0
//
//---------------------------------------------------------------------------
ULONG
CExpressionHandle::UlLastNonScalarChild() const
{
	const ULONG ulArity = UlArity();
	if (0 == ulArity)
	{
		return ULONG_MAX;
	}

	ULONG ulLastNonScalarChild = ulArity - 1;
	while (0 < ulLastNonScalarChild && FScalarChild(ulLastNonScalarChild))
	{
		ulLastNonScalarChild --;
	}

	if (!FScalarChild(ulLastNonScalarChild))
	{
		// we need to check again that index points to a non-scalar child
		// since operator's children may be all scalar (e.g. index-scan)
		return ulLastNonScalarChild;
	}

	return ULONG_MAX;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::UlFirstNonScalarChild
//
//	@doc:
//		Return the index of the first non-scalar child. This is only valid if
//		UlArity() is greater than 0
//
//---------------------------------------------------------------------------
ULONG
CExpressionHandle::UlFirstNonScalarChild() const
{
	const ULONG ulArity = UlArity();
	if (0 == ulArity)
	{
		return ULONG_MAX;
	}

	ULONG ulFirstNonScalarChild = 0;
	while (ulFirstNonScalarChild  < ulArity - 1 && FScalarChild(ulFirstNonScalarChild))
	{
		ulFirstNonScalarChild ++;
	}

	if (!FScalarChild(ulFirstNonScalarChild))
	{
		// we need to check again that index points to a non-scalar child
		// since operator's children may be all scalar (e.g. index-scan)
		return ulFirstNonScalarChild;
	}

	return ULONG_MAX;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::UlNonScalarChildren
//
//	@doc:
//		Return number of non-scalar children
//
//---------------------------------------------------------------------------
ULONG
CExpressionHandle::UlNonScalarChildren() const
{
	const ULONG ulArity = UlArity();
	ULONG ulNonScalarChildren = 0;
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		if (!FScalarChild(ul))
		{
			ulNonScalarChildren++;
		}
	}

	return ulNonScalarChildren;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::Pdprel
//
//	@doc:
//		Retrieve derived relational props of n-th child;
//		Assumes caller knows what properties to ask for;
//
//---------------------------------------------------------------------------
CDrvdPropRelational *
CExpressionHandle::Pdprel
	(
	ULONG ulChildIndex
	)
	const
{
	if (NULL != Pexpr() && NULL == m_pdrgpdp)
	{
		// handle is used for required property computation
		if (Pexpr()->Pop()->FPhysical())
		{
			// relational props were copied from memo, return props directly
			return CDrvdPropRelational::Pdprel((*Pexpr())[ulChildIndex]->Pdp(CDrvdProp::EptRelational));
		}

		// return props after calling derivation function
		return CDrvdPropRelational::Pdprel((*Pexpr())[ulChildIndex]->PdpDerive());
	}

	if (FAttachedToLeafPattern())
	{
		GPOS_ASSERT(NULL != Pexpr());
		GPOS_ASSERT(NULL != Pexpr()->Pgexpr());

		// handle is attached to a leaf pattern, get relational props from child group
		return CDrvdPropRelational::Pdprel((*Pexpr()->Pgexpr())[ulChildIndex]->Pdp());
	}

	if (NULL != m_pcc)
	{
		// handle is used for deriving plan properties, get relational props from child group
		return CDrvdPropRelational::Pdprel((*Pgexpr())[ulChildIndex]->Pdp());
	}

	GPOS_ASSERT(ulChildIndex < m_pdrgpdp->UlLength());

	CDrvdProp *pdp = (*m_pdrgpdp)[ulChildIndex];

	return CDrvdPropRelational::Pdprel(pdp);
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::Pdprel
//
//	@doc:
//		Retrieve relational properties of attached expr/gexpr;
//
//---------------------------------------------------------------------------
CDrvdPropRelational *
CExpressionHandle::Pdprel() const
{
	if (NULL != Pexpr())
	{
		if (Pexpr()->Pop()->FPhysical())
		{
			// relational props were copied from memo, return props directly
			return CDrvdPropRelational::Pdprel(Pexpr()->Pdp(CDrvdProp::EptRelational));
		}
		// return props after calling derivation function
		return CDrvdPropRelational::Pdprel(Pexpr()->PdpDerive());
	}

	if (NULL != m_pcc)
	{
		// get relational props from group
		return CDrvdPropRelational::Pdprel(Pgexpr()->Pgroup()->Pdp());
	}

	return CDrvdPropRelational::Pdprel(m_pdp);
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::Pstats
//
//	@doc:
//		Return derived stats of n-th child
//
//---------------------------------------------------------------------------
IStatistics *
CExpressionHandle::Pstats
	(
	ULONG ulChildIndex
	)
	const
{
	GPOS_ASSERT(ulChildIndex < m_pdrgpstat->UlLength());

	return (*m_pdrgpstat)[ulChildIndex];
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::Pdpplan
//
//	@doc:
//		Retrieve derived plan props of n-th child;
//		Assumes caller knows what properties to ask for;
//
//---------------------------------------------------------------------------
CDrvdPropPlan *
CExpressionHandle::Pdpplan
	(
	ULONG ulChildIndex
	)
	const
{
	GPOS_ASSERT(ulChildIndex < m_pdrgpdp->UlLength());

	CDrvdProp *pdp = (*m_pdrgpdp)[ulChildIndex];

	return CDrvdPropPlan::Pdpplan(pdp);
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::Pdpscalar
//
//	@doc:
//		Retrieve derived scalar props of n-th child;
//		Assumes caller knows what properties to ask for;
//
//---------------------------------------------------------------------------
CDrvdPropScalar *
CExpressionHandle::Pdpscalar
	(
	ULONG ulChildIndex
	)
	const
{
	if (NULL != Pexpr() && NULL == m_pdrgpdp)
	{
		// handle is used for required property computation
		CDrvdProp *pdp = (*Pexpr())[ulChildIndex]->PdpDerive();
		return CDrvdPropScalar::Pdpscalar(pdp);
	}

	if (FAttachedToLeafPattern())
	{
		GPOS_ASSERT(NULL != Pexpr());
		GPOS_ASSERT(NULL != Pexpr()->Pgexpr());

		// handle is attached to a leaf pattern, get scalar props from child group
		return CDrvdPropScalar::Pdpscalar((*Pexpr()->Pgexpr())[ulChildIndex]->Pdp());
	}

	if (NULL != m_pcc)
	{
		// handle is used for deriving plan properties, get scalar props from child group
		return CDrvdPropScalar::Pdpscalar((*Pgexpr())[ulChildIndex]->Pdp());
	}

	GPOS_ASSERT(ulChildIndex < m_pdrgpdp->UlLength());

	CDrvdProp *pdp = (*m_pdrgpdp)[ulChildIndex];

	return CDrvdPropScalar::Pdpscalar(pdp);
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::Prprel
//
//	@doc:
//		Retrieve required relational props of n-th child;
//		Assumes caller knows what properties to ask for;
//
//---------------------------------------------------------------------------
CReqdPropRelational *
CExpressionHandle::Prprel
	(
	ULONG ulChildIndex
	)
	const
{
	GPOS_ASSERT(ulChildIndex < m_pdrgprp->UlLength());

	CReqdProp *prp = (*m_pdrgprp)[ulChildIndex];
	GPOS_ASSERT(prp->FRelational() && "Unexpected property type");

	return CReqdPropRelational::Prprel(prp);
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::Prpp
//
//	@doc:
//		Retrieve required relational props of n-th child;
//		Assumes caller knows what properties to ask for;
//
//---------------------------------------------------------------------------
CReqdPropPlan *
CExpressionHandle::Prpp
	(
	ULONG ulChildIndex
	)
	const
{
	GPOS_ASSERT(ulChildIndex < m_pdrgprp->UlLength());

	CReqdProp *prp = (*m_pdrgprp)[ulChildIndex];
	GPOS_ASSERT(prp->FPlan() && "Unexpected property type");

	return CReqdPropPlan::Prpp(prp);
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::Pop
//
//	@doc:
//		Get operator from handle
//
//---------------------------------------------------------------------------
COperator *
CExpressionHandle::Pop() const
{
	if (NULL != m_pexpr)
	{
		GPOS_ASSERT(NULL == m_pgexpr);

		return m_pexpr->Pop();
	}

	if (NULL != m_pgexpr)
	{
		return m_pgexpr->Pop();
	}
	
	GPOS_ASSERT(!"Handle was not attached properly");
	return NULL;
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::Pop
//
//	@doc:
//		Get child operator from handle
//
//---------------------------------------------------------------------------
COperator *
CExpressionHandle::Pop
	(
	ULONG ulChildIndex
	)
	const
{
	GPOS_ASSERT(ulChildIndex < UlArity());

	if (NULL != m_pexpr)
	{
		GPOS_ASSERT(NULL == m_pgexpr);

		return (*m_pexpr)[ulChildIndex]->Pop();
	}

	if (NULL != m_pcc)
	{
		COptimizationContext *pocChild = (*m_pcc->Pdrgpoc())[ulChildIndex];
		GPOS_ASSERT(NULL != pocChild);

		CCostContext *pccChild = pocChild->PccBest();
		GPOS_ASSERT(NULL != pccChild);

		return pccChild->Pgexpr()->Pop();
	}

	return NULL;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::DeriveProducerStats
//
//	@doc:
//		If the child (ulChildIndex) is a CTE consumer, then derive is corresponding
//		producer statistics.
//
//---------------------------------------------------------------------------
void
CExpressionHandle::DeriveProducerStats
	(
	ULONG ulChildIndex,
	CColRefSet *pcrsStats
	)
{
	// check to see if there are any CTE consumers in the group whose properties have
	// to be pushed to its corresponding CTE producer
	CGroupExpression *pgexpr = Pgexpr();
	if (NULL != pgexpr)
	{
		CGroup *pgroupChild = (*pgexpr)[ulChildIndex];
		if (pgroupChild->FHasAnyCTEConsumer())
		{
			CGroupExpression *pgexprCTEConsumer = pgroupChild->PgexprAnyCTEConsumer();
			CLogicalCTEConsumer *popConsumer = CLogicalCTEConsumer::PopConvert(pgexprCTEConsumer->Pop());
			COptCtxt::PoctxtFromTLS()->Pcteinfo()->DeriveProducerStats(popConsumer, pcrsStats);
		}

		return;
	}

	// statistics are also derived on expressions representing the producer that may have
	// multiple CTE consumers. We should ensure that their properties are to pushed to their
	// corresponding CTE producer
	CExpression *pexpr = Pexpr();
	if (NULL != pexpr)
	{
		CExpression *pexprChild = (*pexpr)[ulChildIndex];
		if (COperator::EopLogicalCTEConsumer == pexprChild->Pop()->Eopid())
		{
			CLogicalCTEConsumer *popConsumer = CLogicalCTEConsumer::PopConvert(pexprChild->Pop());
			COptCtxt::PoctxtFromTLS()->Pcteinfo()->DeriveProducerStats(popConsumer, pcrsStats);
		}
	}
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::PexprScalarChild
//
//	@doc:
//		Get the scalar child at given index
//
//---------------------------------------------------------------------------
CExpression *
CExpressionHandle::PexprScalarChild
	(
	ULONG ulChildIndex
	)
	const
{
	GPOS_ASSERT(ulChildIndex < UlArity());

	if (NULL != m_pgexpr)
	{
		// access scalar expression cached on the child scalar group
		GPOS_ASSERT((*m_pgexpr)[ulChildIndex]->FScalar());

		CExpression *pexprScalar = (*m_pgexpr)[ulChildIndex]->PexprScalar();
		GPOS_ASSERT_IMP(NULL == pexprScalar, CDrvdPropScalar::Pdpscalar((*m_pgexpr)[ulChildIndex]->Pdp())->FHasSubquery());

		return pexprScalar;
	}

	if (NULL != m_pexpr && NULL != (*m_pexpr)[ulChildIndex]->Pgexpr())
	{
		// if the expression does not come from a group, but its child does then
		// get the scalar child from that group
		CGroupExpression *pgexpr = (*m_pexpr)[ulChildIndex]->Pgexpr();
		CExpression *pexprScalar = pgexpr->Pgroup()->PexprScalar();
		GPOS_ASSERT_IMP(NULL == pexprScalar, CDrvdPropScalar::Pdpscalar((*m_pexpr)[ulChildIndex]->PdpDerive())->FHasSubquery());

		return pexprScalar;
	}

	// access scalar expression from the child expression node
	GPOS_ASSERT((*m_pexpr)[ulChildIndex]->Pop()->FScalar());

	return (*m_pexpr)[ulChildIndex];
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::PexprScalar
//
//	@doc:
//		Get the scalar expression attached to handle,
//		return NULL if handle is not attached to a scalar expression
//
//---------------------------------------------------------------------------
CExpression *
CExpressionHandle::PexprScalar() const
{
	if (!Pop()->FScalar())
	{
		return NULL;
	}

	if (NULL != m_pexpr)
	{
		return m_pexpr;
	}

	if (NULL != m_pgexpr)
	{
		return m_pgexpr->Pgroup()->PexprScalar();
	}

	return NULL;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::PfpChild
//
//	@doc:
//		Retrieve derived function props of n-th child;
//
//---------------------------------------------------------------------------
CFunctionProp *
CExpressionHandle::PfpChild
	(
	ULONG ulChildIndex
	)
	const
{
	if (FScalarChild(ulChildIndex))
	{
		return Pdpscalar(ulChildIndex)->Pfp();
	}

	return Pdprel(ulChildIndex)->Pfp();
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::FChildrenHaveVolatileFuncScan
//
//	@doc:
//		Check whether an expression's children have a volatile function
//
//---------------------------------------------------------------------------
BOOL
CExpressionHandle::FChildrenHaveVolatileFuncScan() const
{
	const ULONG ulArity = UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		if (PfpChild(ul)->FHasVolatileFunctionScan())
		{
			return true;
		}
	}

	return false;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::UlFirstOptimizedChildIndex
//
//	@doc:
//		Return the index of first child to be optimized
//
//---------------------------------------------------------------------------
ULONG
CExpressionHandle::UlFirstOptimizedChildIndex() const
{
	const ULONG ulArity = UlArity();
	GPOS_ASSERT(0 < ulArity);

	CPhysical::EChildExecOrder eceo = CPhysical::PopConvert(Pop())->Eceo();
	if (CPhysical::EceoRightToLeft == eceo)
	{
		return ulArity - 1;
	}
	GPOS_ASSERT(CPhysical::EceoLeftToRight == eceo);

	return 0;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::UlLastOptimizedChildIndex
//
//	@doc:
//		Return the index of last child to be optimized
//
//---------------------------------------------------------------------------
ULONG
CExpressionHandle::UlLastOptimizedChildIndex() const
{
	const ULONG ulArity = UlArity();
	GPOS_ASSERT(0 < ulArity);

	CPhysical::EChildExecOrder eceo = CPhysical::PopConvert(Pop())->Eceo();
	if (CPhysical::EceoRightToLeft == eceo)
	{
		return 0;
	}
	GPOS_ASSERT(CPhysical::EceoLeftToRight == eceo);

	return ulArity - 1;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::UlNextOptimizedChildIndex
//
//	@doc:
//		Return the index of child to be optimized next to the given child,
//		return ULONG_MAX if there is no next child index
//
//
//---------------------------------------------------------------------------
ULONG
CExpressionHandle::UlNextOptimizedChildIndex
	(
	ULONG ulChildIndex
	)
	const
{
	CPhysical::EChildExecOrder eceo = CPhysical::PopConvert(Pop())->Eceo();

	ULONG ulNextChildIndex = ULONG_MAX;
	if (CPhysical::EceoRightToLeft == eceo)
	{
		if (0 < ulChildIndex)
		{
			ulNextChildIndex = ulChildIndex - 1;
		}
	}
	else
	{
		GPOS_ASSERT(CPhysical::EceoLeftToRight == eceo);

		if (UlArity() - 1 > ulChildIndex)
		{
			ulNextChildIndex = ulChildIndex + 1;
		}
	}

	return ulNextChildIndex;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::UlPreviousOptimizedChildIndex
//
//	@doc:
//		Return the index of child optimized before the given child,
//		return ULONG_MAX if there is no previous child index
//
//
//---------------------------------------------------------------------------
ULONG
CExpressionHandle::UlPreviousOptimizedChildIndex
	(
	ULONG ulChildIndex
	)
	const
{
	CPhysical::EChildExecOrder eceo = CPhysical::PopConvert(Pop())->Eceo();

	ULONG ulPrevChildIndex = ULONG_MAX;
	if (CPhysical::EceoRightToLeft == eceo)
	{
		if (UlArity() - 1 > ulChildIndex)
		{
			ulPrevChildIndex = ulChildIndex + 1;
		}
	}
	else
	{
		GPOS_ASSERT(CPhysical::EceoLeftToRight == eceo);

		if (0 < ulChildIndex)
		{
			ulPrevChildIndex = ulChildIndex - 1;
		}
	}

	return ulPrevChildIndex;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::FNextChildIndex
//
//	@doc:
//		Get next child index based on child optimization order, return
//		true if such index could be found
//
//---------------------------------------------------------------------------
BOOL
CExpressionHandle::FNextChildIndex
	(
	ULONG *pulChildIndex
	)
	const
{
	GPOS_ASSERT(NULL != pulChildIndex);

	const ULONG ulArity = UlArity();
	if (0 == ulArity)
	{
		// operator does not have children
		return false;
	}

	ULONG ulNextChildIndex = UlNextOptimizedChildIndex(*pulChildIndex);
	if (ULONG_MAX == ulNextChildIndex)
	{
		return false;
	}
	*pulChildIndex = ulNextChildIndex;

	return true;
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionHandle::PcrsUsedColumns
//
//	@doc:
//		Return the columns used by a logical operator and all its scalar children
//
//---------------------------------------------------------------------------
CColRefSet *
CExpressionHandle::PcrsUsedColumns
	(
	IMemoryPool *pmp
	)
{
	COperator *pop = Pop();
	GPOS_ASSERT(pop->FLogical());

	CColRefSet *pcrs = GPOS_NEW(pmp) CColRefSet(pmp);

	// get columns used by the operator itself
	pcrs->Include(CLogical::PopConvert(pop)->PcrsLocalUsed());

	// get columns used by the scalar children
	const ULONG ulArity = UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		if (FScalarChild(ul))
		{
			pcrs->Include(Pdpscalar(ul)->PcrsUsed());
		}
	}

	return pcrs;
}

// EOF
