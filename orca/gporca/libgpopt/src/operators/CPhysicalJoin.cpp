//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CPhysicalJoin.cpp
//
//	@doc:
//		Implementation of physical join operator
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
#include "gpopt/base/CDistributionSpecAny.h"
#include "gpopt/base/CDistributionSpecReplicated.h"

#include "gpopt/operators/ops.h"
#include "gpopt/operators/CExpressionHandle.h"

using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::CPhysicalJoin
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CPhysicalJoin::CPhysicalJoin
	(
	IMemoryPool *pmp
	)
	:
	CPhysical(pmp)
{}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::~CPhysicalJoin
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CPhysicalJoin::~CPhysicalJoin()
{}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::FMatch
//
//	@doc:
//		Match operators
//
//---------------------------------------------------------------------------
BOOL
CPhysicalJoin::FMatch
	(
	COperator *pop
	)
	const
{
	return Eopid() == pop->Eopid();
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::PosPropagateToOuter
//
//	@doc:
//		Helper for propagating required sort order to outer child
//
//---------------------------------------------------------------------------
COrderSpec *
CPhysicalJoin::PosPropagateToOuter
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	COrderSpec *posRequired
	)
{
	// propagate the order requirement to the outer child only if all the columns
	// specified by the order requirement come from the outer child
	CColRefSet *pcrs = posRequired->PcrsUsed(pmp);
	BOOL fOuterSortCols = exprhdl.Pdprel(0)->PcrsOutput()->FSubset(pcrs);
	pcrs->Release();
	if (fOuterSortCols)
	{
		return PosPassThru(pmp, exprhdl, posRequired, 0 /*ulChildIndex*/);
	}

	return GPOS_NEW(pmp) COrderSpec(pmp);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::PcrsRequired
//
//	@doc:
//		Compute required output columns of n-th child
//
//---------------------------------------------------------------------------
CColRefSet *
CPhysicalJoin::PcrsRequired
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	CColRefSet *pcrsRequired,
	ULONG ulChildIndex,
	DrgPdp *, // pdrgpdpCtxt
	ULONG // ulOptReq
	)
{
	GPOS_ASSERT(ulChildIndex < 2 &&
				"Required properties can only be computed on the relational child");
	
	return PcrsChildReqd(pmp, exprhdl, pcrsRequired, ulChildIndex, 2 /*ulScalarIndex*/);
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::PppsRequired
//
//	@doc:
//		Compute required partition propagation of the n-th child
//
//---------------------------------------------------------------------------
CPartitionPropagationSpec *
CPhysicalJoin::PppsRequired
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	CPartitionPropagationSpec *pppsRequired,
	ULONG ulChildIndex,
	DrgPdp *, //pdrgpdpCtxt,
	ULONG //ulOptReq
	)
{
	GPOS_ASSERT(NULL != pppsRequired);
	
	return CPhysical::PppsRequiredPushThruNAry(pmp, exprhdl, pppsRequired, ulChildIndex);
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::PcteRequired
//
//	@doc:
//		Compute required CTE map of the n-th child
//
//---------------------------------------------------------------------------
CCTEReq *
CPhysicalJoin::PcteRequired
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	CCTEReq *pcter,
	ULONG ulChildIndex,
	DrgPdp *pdrgpdpCtxt,
	ULONG //ulOptReq
	)
	const
{
	GPOS_ASSERT(2 > ulChildIndex);

	return PcterNAry(pmp, exprhdl, pcter, ulChildIndex, pdrgpdpCtxt);
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::FProvidesReqdCols
//
//	@doc:
//		Helper for checking if required columns are included in output columns
//
//---------------------------------------------------------------------------
BOOL
CPhysicalJoin::FProvidesReqdCols
	(
	CExpressionHandle &exprhdl,
	CColRefSet *pcrsRequired,
	ULONG // ulOptReq
	)
	const
{
	GPOS_ASSERT(NULL != pcrsRequired);
	GPOS_ASSERT(3 == exprhdl.UlArity());

	// union columns from relational children
	CColRefSet *pcrs = GPOS_NEW(m_pmp) CColRefSet(m_pmp);
	ULONG ulArity = exprhdl.UlArity();
	for (ULONG i = 0; i < ulArity - 1; i++)
	{
		CColRefSet *pcrsChild = exprhdl.Pdprel(i)->PcrsOutput();
		pcrs->Union(pcrsChild);
	}

	BOOL fProvidesCols = pcrs->FSubset(pcrsRequired);
	pcrs->Release();

	return fProvidesCols;
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::FSortColsInOuterChild
//
//	@doc:
//		Helper for checking if required sort columns come from outer child
//
//----------------------------------------------------------------------------
BOOL
CPhysicalJoin::FSortColsInOuterChild
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	COrderSpec *pos
	)
{
	GPOS_ASSERT(NULL != pos);

	CColRefSet *pcrsSort = pos->PcrsUsed(pmp);
	CColRefSet *pcrsOuterChild = exprhdl.Pdprel(0 /*ulChildIndex*/)->PcrsOutput();
	BOOL fSortColsInOuter = pcrsOuterChild->FSubset(pcrsSort);
	pcrsSort->Release();

	return fSortColsInOuter;
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::FOuterProvidesReqdCols
//
//	@doc:
//		Helper for checking if the outer input of a binary join operator
//		includes the required columns
//
//---------------------------------------------------------------------------
BOOL
CPhysicalJoin::FOuterProvidesReqdCols
	(
	CExpressionHandle &exprhdl,
	CColRefSet *pcrsRequired
	)
{
	GPOS_ASSERT(NULL != pcrsRequired);
	GPOS_ASSERT(3 == exprhdl.UlArity() && "expected binary join");

	CColRefSet *pcrsOutput = exprhdl.Pdprel(0 /*ulChildIndex*/)->PcrsOutput();

	return pcrsOutput->FSubset(pcrsRequired);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::PdsRequired
//
//	@doc:
//		Compute required distribution of the n-th child;
//		this function creates a request for ANY distribution on the outer
//		child, then matches the delivered distribution on the inner child,
//		this results in sending either a broadcast or singleton distribution
//		requests to the inner child
//
//---------------------------------------------------------------------------
CDistributionSpec *
CPhysicalJoin::PdsRequired
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	CDistributionSpec *pdsRequired,
	ULONG ulChildIndex,
	DrgPdp *pdrgpdpCtxt,
	ULONG // ulOptReq
	)
	const
{
	GPOS_ASSERT(2 > ulChildIndex);

	// if expression has to execute on master then we need a gather
	if (exprhdl.FMasterOnly())
	{
		return PdsEnforceMaster(pmp, exprhdl, pdsRequired, ulChildIndex);
	}

	if (exprhdl.FHasOuterRefs())
	{
		if (CDistributionSpec::EdtSingleton == pdsRequired->Edt() ||
			CDistributionSpec::EdtReplicated == pdsRequired->Edt())
		{
			return PdsPassThru(pmp, exprhdl, pdsRequired, ulChildIndex);
		}
		return GPOS_NEW(pmp) CDistributionSpecReplicated();
	}

	if (1 == ulChildIndex)
	{
		// compute a matching distribution based on derived distribution of outer child
		CDistributionSpec *pdsOuter = CDrvdPropPlan::Pdpplan((*pdrgpdpCtxt)[0])->Pds();

		if (CDistributionSpec::EdtUniversal == pdsOuter->Edt())
		{
			// first child is universal, request second child to execute on the master to avoid duplicates
			return GPOS_NEW(pmp) CDistributionSpecSingleton(CDistributionSpecSingleton::EstMaster);
		}

		if (CDistributionSpec::EdtSingleton == pdsOuter->Edt() || 
			CDistributionSpec::EdtStrictSingleton == pdsOuter->Edt())
		{
			// require inner child to have matching singleton distribution
			return CPhysical::PdssMatching(pmp, CDistributionSpecSingleton::PdssConvert(pdsOuter));
		}

		// otherwise, require inner child to be replicated
		return GPOS_NEW(pmp) CDistributionSpecReplicated();
	}

	// no distribution requirement on the outer side
	return GPOS_NEW(pmp) CDistributionSpecAny();
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::PdsDerive
//
//	@doc:
//		Derive distribution
//
//---------------------------------------------------------------------------
CDistributionSpec *
CPhysicalJoin::PdsDerive
	(
	IMemoryPool *, // pmp,
	CExpressionHandle &exprhdl
	)
	const
{
	CDistributionSpec *pdsOuter = exprhdl.Pdpplan(0 /*ulChildIndex*/)->Pds();
	CDistributionSpec *pdsInner = exprhdl.Pdpplan(1 /*ulChildIndex*/)->Pds();

	if (CDistributionSpec::EdtReplicated == pdsOuter->Edt() ||
		CDistributionSpec::EdtUniversal == pdsOuter->Edt())
	{
		// if outer is replicated/universal, return inner distribution
		pdsInner->AddRef();
		return pdsInner;
	}

	// otherwise, return outer distribution
	pdsOuter->AddRef();
	return pdsOuter;
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::PrsDerive
//
//	@doc:
//		Derive rewindability
//
//---------------------------------------------------------------------------
CRewindabilitySpec *
CPhysicalJoin::PrsDerive
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl
	)
	const
{
	CRewindabilitySpec *prsOuter = exprhdl.Pdpplan(0 /*ulChildIndex*/)->Prs();
	GPOS_ASSERT(NULL != prsOuter);

	CRewindabilitySpec *prsInner = exprhdl.Pdpplan(1 /*ulChildIndex*/)->Prs();
	GPOS_ASSERT(NULL != prsInner);

	if (CUtils::FCorrelatedNLJoin(exprhdl.Pop()))
	{
		// rewindability is not established if correlated join
		return GPOS_NEW(pmp) CRewindabilitySpec(CRewindabilitySpec::ErtNone /*ert*/);
	}

	if (CRewindabilitySpec::ErtNone == prsOuter->Ert() ||
		CRewindabilitySpec::ErtNone == prsInner->Ert())
	{
		// rewindability is not established if any child is non-rewindable
		return GPOS_NEW(pmp) CRewindabilitySpec(CRewindabilitySpec::ErtNone /*ert*/);
	}

	// check if both children have the same rewindability spec
	if (prsOuter->Ert() ==  prsInner->Ert())
	{
		prsOuter->AddRef();
		return prsOuter;
	}

	// one of the two children has general rewindability while the other child has
	// mark-restore  rewindability
	return GPOS_NEW(pmp) CRewindabilitySpec(CRewindabilitySpec::ErtGeneral /*ert*/);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::EpetDistribution
//
//	@doc:
//		Return the enforcing type for distribution property based on this operator
//
//---------------------------------------------------------------------------
CEnfdProp::EPropEnforcingType
CPhysicalJoin::EpetDistribution
	(
	CExpressionHandle &exprhdl,
	const CEnfdDistribution *ped
	)
	const
{
	GPOS_ASSERT(NULL != ped);

	// get distribution delivered by the join node
	CDistributionSpec *pds = CDrvdPropPlan::Pdpplan(exprhdl.Pdp())->Pds();

	if (ped->FCompatible(pds))
	{
		// required distribution will be established by the join operator
		return CEnfdProp::EpetUnnecessary;
	}

	// required distribution will be enforced on join's output
	return CEnfdProp::EpetRequired;
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::EpetRewindability
//
//	@doc:
//		Return the enforcing type for rewindability property based on this operator
//
//---------------------------------------------------------------------------
CEnfdProp::EPropEnforcingType
CPhysicalJoin::EpetRewindability
	(
	CExpressionHandle &exprhdl,
	const CEnfdRewindability *per
	)
	const
{
	// get rewindability delivered by the join node
	CRewindabilitySpec *prs = CDrvdPropPlan::Pdpplan(exprhdl.Pdp())->Prs();

	if (per->FCompatible(prs))
	{
		// required rewindability will be established by the join operator
		return CEnfdProp::EpetUnnecessary;
	}

	// required rewindability will be enforced on join's output
	return CEnfdProp::EpetRequired;
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::FHashJoinCompatible
//
//	@doc:
//		Are the given predicate parts hash join compatible?
//		predicate parts are obtained by splitting equality into outer
//		and inner expressions
//
//---------------------------------------------------------------------------
BOOL
CPhysicalJoin::FHashJoinCompatible
	(
	CExpression *pexprOuter,	// outer child of the join
	CExpression* pexprInner,	// inner child of the join
	CExpression *pexprPredOuter,// outer part of join predicate
	CExpression *pexprPredInner // inner part of join predicate
	)
{
	GPOS_ASSERT(NULL != pexprOuter);
	GPOS_ASSERT(NULL != pexprInner);
	GPOS_ASSERT(NULL != pexprPredOuter);
	GPOS_ASSERT(NULL != pexprPredInner);

	IMDId *pmdidTypeOuter = CScalar::PopConvert(pexprPredOuter->Pop())->PmdidType();
	IMDId *pmdidTypeInner = CScalar::PopConvert(pexprPredInner->Pop())->PmdidType();

	CColRefSet *pcrsUsedPredOuter = CDrvdPropScalar::Pdpscalar(pexprPredOuter->PdpDerive())->PcrsUsed();
	CColRefSet *pcrsUsedPredInner = CDrvdPropScalar::Pdpscalar(pexprPredInner->PdpDerive())->PcrsUsed();

	CColRefSet *pcrsOuter = CDrvdPropRelational::Pdprel(pexprOuter->Pdp(CDrvdProp::EptRelational))->PcrsOutput();
	CColRefSet *pcrsInner = CDrvdPropRelational::Pdprel(pexprInner->Pdp(CDrvdProp::EptRelational))->PcrsOutput();

	// make sure that each predicate child uses columns from a different join child
	// in order to reject predicates of the form 'X Join Y on f(X.a, Y.b) = 5'
	BOOL fPredOuterUsesJoinOuterChild = (0 < pcrsUsedPredOuter->CElements()) && pcrsOuter->FSubset(pcrsUsedPredOuter);
	BOOL fPredOuterUsesJoinInnerChild = (0 < pcrsUsedPredOuter->CElements()) && pcrsInner->FSubset(pcrsUsedPredOuter);
	BOOL fPredInnerUsesJoinOuterChild = (0 < pcrsUsedPredInner->CElements()) && pcrsOuter->FSubset(pcrsUsedPredInner);
	BOOL fPredInnerUsesJoinInnerChild = (0 < pcrsUsedPredInner->CElements()) && pcrsInner->FSubset(pcrsUsedPredInner);

	BOOL fHashJoinCompatiblePred =
		(fPredOuterUsesJoinOuterChild && fPredInnerUsesJoinInnerChild) ||
		(fPredOuterUsesJoinInnerChild && fPredInnerUsesJoinOuterChild);

	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();

	return fHashJoinCompatiblePred &&
					pmda->Pmdtype(pmdidTypeOuter)->FHashable() &&
					pmda->Pmdtype(pmdidTypeInner)->FHashable();
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::FHashJoinCompatible
//
//	@doc:
//		Is given predicate hash-join compatible?
//		the function returns True if predicate is of the form (Equality) or
//		(Is Not Distinct From), both operands are hashjoinable, AND the predicate
//		uses columns from both join children
//
//---------------------------------------------------------------------------
BOOL
CPhysicalJoin::FHashJoinCompatible
	(
	CExpression *pexprPred,		// predicate in question
	CExpression *pexprOuter,	// outer child of the join
	CExpression* pexprInner		// inner child of the join
	)
{
	GPOS_ASSERT(NULL != pexprPred);
	GPOS_ASSERT(NULL != pexprOuter);
	GPOS_ASSERT(NULL != pexprInner);
	GPOS_ASSERT(pexprOuter != pexprInner);

	BOOL fCompatible = false;
	if (CPredicateUtils::FEquality(pexprPred) || CPredicateUtils::FINDF(pexprPred))
	{
		CExpression *pexprPredOuter = NULL;
		CExpression *pexprPredInner = NULL;
		ExtractHashJoinExpressions(pexprPred, &pexprPredOuter, &pexprPredInner);
		fCompatible = FHashJoinCompatible(pexprOuter, pexprInner, pexprPredOuter, pexprPredInner);
	}

	return fCompatible;
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::ExtractHashJoinExpressions
//
//	@doc:
//		Extract expressions that can be used in hash-join from the given predicate
//
//---------------------------------------------------------------------------
void
CPhysicalJoin::ExtractHashJoinExpressions
	(
	CExpression *pexprPred,
	CExpression **ppexprLeft,
	CExpression **ppexprRight
	)
{
	GPOS_ASSERT(NULL != ppexprLeft);
	GPOS_ASSERT(NULL != ppexprRight);

	*ppexprLeft = NULL;
	*ppexprRight = NULL;

	// extract outer and inner expressions from predicate
	CExpression *pexpr = pexprPred;
	if (!CPredicateUtils::FEquality(pexprPred))
	{
		GPOS_ASSERT(CPredicateUtils::FINDF(pexpr));
		pexpr = (*pexprPred)[0];
	}

	*ppexprLeft = (*pexpr)[0];
	*ppexprRight = (*pexpr)[1];
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::AddHashKeys
//
//	@doc:
//		Helper for adding a pair of hash join keys to given arrays
//
//---------------------------------------------------------------------------
void
CPhysicalJoin::AddHashKeys
	(
	CExpression *pexprPred,	// equality predicate in the form (ColRef1 = ColRef2) or
							// in the form (ColRef1 is not distinct from ColRef2)
	CExpression *pexprOuter,
	CExpression *
#ifdef GPOS_DEBUG
		pexprInner
#endif // GPOS_DEBUG
	,
	DrgPexpr *pdrgpexprOuter,	// array of outer hash keys
	DrgPexpr *pdrgpexprInner	// array of inner hash keys
	)
{
	GPOS_ASSERT(FHashJoinCompatible(pexprPred, pexprOuter, pexprInner));

	// output of outer side
	CColRefSet *pcrsOuter = CDrvdPropRelational::Pdprel(pexprOuter->PdpDerive())->PcrsOutput();

#ifdef GPOS_DEBUG
	// output of inner side
	CColRefSet *pcrsInner = CDrvdPropRelational::Pdprel(pexprInner->PdpDerive())->PcrsOutput();
#endif // GPOS_DEBUG

	// extract outer and inner columns from predicate
	CExpression *pexprPredOuter = NULL;
	CExpression *pexprPredInner = NULL;
	ExtractHashJoinExpressions(pexprPred, &pexprPredOuter, &pexprPredInner);
	GPOS_ASSERT(NULL != pexprPredOuter);
	GPOS_ASSERT(NULL != pexprPredInner);

	CColRefSet *pcrsPredOuter = CDrvdPropScalar::Pdpscalar(pexprPredOuter->PdpDerive())->PcrsUsed();
#ifdef GPOS_DEBUG
	CColRefSet *pcrsPredInner = CDrvdPropScalar::Pdpscalar(pexprPredInner->PdpDerive())->PcrsUsed();
#endif // GPOS_DEBUG

	// determine outer and inner hash keys
	CExpression *pexprKeyOuter = NULL;
	CExpression *pexprKeyInner = NULL;
	if (pcrsOuter->FSubset(pcrsPredOuter))
	{
		pexprKeyOuter = pexprPredOuter;
		GPOS_ASSERT(pcrsInner->FSubset(pcrsPredInner));

		pexprKeyInner = pexprPredInner;
	}
	else
	{
		GPOS_ASSERT(pcrsOuter->FSubset(pcrsPredInner));
		pexprKeyOuter = pexprPredInner;

		GPOS_ASSERT(pcrsInner->FSubset(pcrsPredOuter));
		pexprKeyInner = pexprPredOuter;
	}
	pexprKeyOuter->AddRef();
	pexprKeyInner->AddRef();

	pdrgpexprOuter->Append(pexprKeyOuter);
	pdrgpexprInner->Append(pexprKeyInner);

	GPOS_ASSERT(pdrgpexprInner->UlLength() == pdrgpexprOuter->UlLength());
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::FHashJoinPossible
//
//	@doc:
//		Check if predicate is hashjoin-able and extract arrays of hash keys
//
//---------------------------------------------------------------------------
BOOL
CPhysicalJoin::FHashJoinPossible
	(
	IMemoryPool *pmp,
	CExpression *pexpr,
	DrgPexpr *pdrgpexprOuter,
	DrgPexpr *pdrgpexprInner,
	CExpression **ppexprResult // output : join expression to be transformed to hash join
	)
{
	GPOS_ASSERT(COperator::EopLogicalNAryJoin != pexpr->Pop()->Eopid() &&
		CUtils::FLogicalJoin(pexpr->Pop()));

	// we should not be here if there are outer references
	GPOS_ASSERT(!CUtils::FHasOuterRefs(pexpr));
	GPOS_ASSERT(NULL != ppexprResult);

	// introduce explicit casting, if needed
	DrgPexpr *pdrgpexpr = CPredicateUtils::PdrgpexprCastEquality(pmp,  (*pexpr)[2]);

	// identify hashkeys
	ULONG ulPreds = pdrgpexpr->UlLength();
	for (ULONG ul = 0; ul < ulPreds; ul++)
	{
		CExpression *pexprPred = (*pdrgpexpr)[ul];
		if (FHashJoinCompatible(pexprPred, (*pexpr)[0], (*pexpr)[1]))
		{
			AddHashKeys(pexprPred, (*pexpr)[0], (*pexpr)[1], pdrgpexprOuter, pdrgpexprInner);
		}
	}
	
	// construct output join expression
	pexpr->Pop()->AddRef();
	(*pexpr)[0]->AddRef();
	(*pexpr)[1]->AddRef();
	*ppexprResult =
		GPOS_NEW(pmp) CExpression
			(
			pmp,
			pexpr->Pop(),
			(*pexpr)[0],
			(*pexpr)[1],
			CPredicateUtils::PexprConjunction(pmp, pdrgpexpr)
			);

	return (0 != pdrgpexprInner->UlLength());
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::AddFilterOnPartKey
//
//	@doc:
//		 Helper to add filter on part key
//
//---------------------------------------------------------------------------
void
CPhysicalJoin::AddFilterOnPartKey
	(
	IMemoryPool *pmp,
	BOOL fNLJoin,
	CExpression *pexprScalar,
	CPartIndexMap *ppimSource,
	CPartFilterMap *ppfmSource,
	ULONG ulChildIndex,
	ULONG ulPartIndexId,
	BOOL fOuterPartConsumer,
	CPartIndexMap *ppimResult,
	CPartFilterMap *ppfmResult,
	CColRefSet *pcrsAllowedRefs
	)
{
	GPOS_ASSERT(NULL != pcrsAllowedRefs);

	ULONG ulChildIndexToTestFirst = 0;
	ULONG ulChildIndexToTestSecond = 1;
	BOOL fOuterPartConsumerTest = fOuterPartConsumer;

	if (fNLJoin)
	{
		ulChildIndexToTestFirst = 1;
		ulChildIndexToTestSecond = 0;
		fOuterPartConsumerTest = !fOuterPartConsumer;
	}

	// look for a filter on the part key
	CExpression *pexprCmp = PexprJoinPredOnPartKeys(pmp, pexprScalar, ppimSource, ulPartIndexId, pcrsAllowedRefs);

	// TODO:  - Aug 14, 2013; create a conjunction of the two predicates when the partition resolver framework supports this
	if (NULL == pexprCmp && ppfmSource->FContainsScanId(ulPartIndexId))
	{
		// look if a predicates was propagated from an above level
		pexprCmp = ppfmSource->Pexpr(ulPartIndexId);
		pexprCmp->AddRef();
	}

	if (NULL != pexprCmp)
	{
		if (fOuterPartConsumerTest)
		{
			if (ulChildIndexToTestFirst == ulChildIndex)
			{
				// we know that we will be requesting the selector from the second child
				// so we need to increment the number of expected propagators here and pass through
				ppimResult->AddRequiredPartPropagation(ppimSource, ulPartIndexId, CPartIndexMap::EppraIncrementPropagators);
				pexprCmp->Release();
			}
			else
			{
				// an interesting condition found - request partition selection on the inner child
				ppimResult->AddRequiredPartPropagation(ppimSource, ulPartIndexId, CPartIndexMap::EppraZeroPropagators);
				ppfmResult->AddPartFilter(pmp, ulPartIndexId, pexprCmp, NULL /*pstats*/);
			}
		}
		else
		{
			pexprCmp->Release();
			GPOS_ASSERT(ulChildIndexToTestFirst == ulChildIndex);
		}
	}
	else if (FProcessingChildWithPartConsumer(fOuterPartConsumerTest, ulChildIndexToTestFirst, ulChildIndexToTestSecond, ulChildIndex))
	{
		// no interesting condition found - push through partition propagation request
		ppimResult->AddRequiredPartPropagation(ppimSource, ulPartIndexId, CPartIndexMap::EppraPreservePropagators);
	}
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::FProcessingChildWithPartConsumer
//
//	@doc:
//		Check whether the child being processed is the child that has the part consumer
//
//---------------------------------------------------------------------------
BOOL
CPhysicalJoin::FProcessingChildWithPartConsumer
	(
	BOOL fOuterPartConsumerTest,
	ULONG ulChildIndexToTestFirst,
	ULONG ulChildIndexToTestSecond,
	ULONG ulChildIndex
	)
{
	return (fOuterPartConsumerTest && ulChildIndexToTestFirst == ulChildIndex) ||
			(!fOuterPartConsumerTest && ulChildIndexToTestSecond == ulChildIndex);
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::PexprJoinPredOnPartKeys
//
//	@doc:
//		Helper to find join predicates on part keys. Returns NULL if not found
//
//---------------------------------------------------------------------------
CExpression *
CPhysicalJoin::PexprJoinPredOnPartKeys
	(
	IMemoryPool *pmp,
	CExpression *pexprScalar,
	CPartIndexMap *ppimSource,
	ULONG ulPartIndexId,
	CColRefSet *pcrsAllowedRefs
	)
{
	GPOS_ASSERT(NULL != pcrsAllowedRefs);

	CExpression *pexprPred = NULL;
	DrgPpartkeys *pdrgppartkeys = ppimSource->Pdrgppartkeys(ulPartIndexId);
	const ULONG ulKeysets = pdrgppartkeys->UlLength();
	for (ULONG ulKey = 0; NULL == pexprPred && ulKey < ulKeysets; ulKey++)
	{
		// get partition key
		DrgDrgPcr *pdrgpdrgpcrPartKeys = (*pdrgppartkeys)[ulKey]->Pdrgpdrgpcr();

		// try to generate a request with dynamic partition selection
		pexprPred = CPredicateUtils::PexprExtractPredicatesOnPartKeys
										(
										pmp,
										pexprScalar,
										pdrgpdrgpcrPartKeys,
										pcrsAllowedRefs,
										true // fUseConstraints
										);
	}

	return pexprPred;
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::FFirstChildToOptimize
//
//	@doc:
//		Helper to check if given child index correspond to first
//		child to be optimized
//
//---------------------------------------------------------------------------
BOOL
CPhysicalJoin::FFirstChildToOptimize
	(
	ULONG ulChildIndex
	)
	const
{
	GPOS_ASSERT(2 > ulChildIndex);

	EChildExecOrder eceo = Eceo();

	return
		(EceoLeftToRight == eceo && 0 == ulChildIndex) ||
		(EceoRightToLeft == eceo && 1 == ulChildIndex);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::UlDistrRequestsForCorrelatedJoin
//
//	@doc:
//		Return number of distribution requests for correlated join
//
//---------------------------------------------------------------------------
ULONG
CPhysicalJoin::UlDistrRequestsForCorrelatedJoin()
{
	// Correlated Join creates two distribution requests to enforce distribution of its children:
	// Req(0): If incoming distribution is (Strict) Singleton, pass it through to all children
	//			to comply with correlated execution requirements
	// Req(1): Request outer child for ANY distribution, and then match it on the inner child

	return 2;
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::PrsRequiredCorrelatedJoin
//
//	@doc:
//		Helper to compute required rewindability of correlated join's children
//
//---------------------------------------------------------------------------
CRewindabilitySpec *
CPhysicalJoin::PrsRequiredCorrelatedJoin
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	CRewindabilitySpec *prsRequired,
	ULONG ulChildIndex,
	DrgPdp *, //pdrgpdpCtxt
	ULONG // ulOptReq
	)
	const
{
	GPOS_ASSERT(3 == exprhdl.UlArity());
	GPOS_ASSERT(2 > ulChildIndex);
	GPOS_ASSERT(CUtils::FCorrelatedNLJoin(exprhdl.Pop()));

	// if there are outer references, then we need a materialize on both children
	if (exprhdl.FHasOuterRefs())
	{
		return GPOS_NEW(pmp) CRewindabilitySpec(CRewindabilitySpec::ErtGeneral);
	}

	if (1 == ulChildIndex)
	{
		// inner child has no rewindability requirement. if there is something
		// below that needs rewindability (e.g. filter, computescalar, agg), it
		// will be requested where needed. However, if inner child has no outer
		// refs (i.e. subplan with no params) then we need a materialize
		if (!exprhdl.FHasOuterRefs(1))
		{
			return GPOS_NEW(pmp) CRewindabilitySpec(CRewindabilitySpec::ErtGeneral /*ert*/);
		}

		return GPOS_NEW(pmp) CRewindabilitySpec(CRewindabilitySpec::ErtNone /*ert*/);
	}

	// pass through requirements to outer child
	return PrsPassThru(pmp, exprhdl, prsRequired, 0 /*ulChildIndex*/);
}

//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::PdsRequiredCorrelatedJoin
//
//	@doc:
//		 Helper to compute required distribution of correlated join's children
//
//---------------------------------------------------------------------------
CDistributionSpec *
CPhysicalJoin::PdsRequiredCorrelatedJoin
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	CDistributionSpec *pdsRequired,
	ULONG ulChildIndex,
	DrgPdp *pdrgpdpCtxt,
	ULONG  ulOptReq
	)
	const
{
	GPOS_ASSERT(3 == exprhdl.UlArity());
	GPOS_ASSERT(2 > ulChildIndex);
	GPOS_ASSERT(CUtils::FCorrelatedNLJoin(exprhdl.Pop()));

	if (0 == ulOptReq && pdsRequired->FSingletonOrStrictSingleton())
	{
		// propagate Singleton request to children to comply with
		// correlated execution requirements
		return PdsPassThru(pmp, exprhdl, pdsRequired, ulChildIndex);
	}

	if (exprhdl.PfpChild(1)->FHasVolatileFunctionScan() && exprhdl.FHasOuterRefs(1))
	{
		// if the inner child has a volatile TVF and has outer refs then request
		// gather from both children
		return GPOS_NEW(pmp) CDistributionSpecSingleton(CDistributionSpecSingleton::EstMaster);
	}

	if (1 == ulChildIndex)
	{
		CDistributionSpec *pdsOuter = CDrvdPropPlan::Pdpplan((*pdrgpdpCtxt)[0])->Pds();
		if (CDistributionSpec::EdtUniversal == pdsOuter->Edt())
		{
			// if outer child delivers a universal distribution, request inner child
			// to match Singleton distribution to detect more than one row generated
			// at runtime, for example: 'select (select 1 union select 2)'
			return GPOS_NEW(pmp) CDistributionSpecSingleton(CDistributionSpecSingleton::EstMaster);
		}
	}

	return CPhysicalJoin::PdsRequired(pmp, exprhdl, pdsRequired, ulChildIndex, pdrgpdpCtxt, ulOptReq);
}


//---------------------------------------------------------------------------
//	@function:
//		CPhysicalJoin::Edm
//
//	@doc:
//		 Distribution matching type
//
//---------------------------------------------------------------------------
CEnfdDistribution::EDistributionMatching
CPhysicalJoin::Edm
	(
	CReqdPropPlan *, // prppInput
	ULONG ulChildIndex,
	DrgPdp *pdrgpdpCtxt,
	ULONG // ulOptReq
	)
{
	if (FFirstChildToOptimize(ulChildIndex))
	{
		// use satisfaction for the first child to be optimized
		return CEnfdDistribution::EdmSatisfy;
	}

	// extract distribution type of previously optimized child
	GPOS_ASSERT(NULL != pdrgpdpCtxt);
	CDistributionSpec::EDistributionType edtPrevChild = CDrvdPropPlan::Pdpplan((*pdrgpdpCtxt)[0])->Pds()->Edt();

	if (CDistributionSpec::EdtReplicated == edtPrevChild ||
	    CDistributionSpec::EdtUniversal == edtPrevChild)
	{
		// if previous child is replicated or universal, we use
		// distribution satisfaction for current child
		return CEnfdDistribution::EdmSatisfy;
	}

	// use exact matching for all other children
	return CEnfdDistribution::EdmExact;
}


// EOF
