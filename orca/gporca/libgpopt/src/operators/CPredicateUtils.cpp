//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright 2011 EMC Corp.
//
//	@filename:
//		CPredicateUtils.cpp
//
//	@doc:
//		Implementation of predicate normalization
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
#include "gpopt/base/CColRefTable.h"
#include "gpopt/base/CColRefSetIter.h"
#include "gpopt/base/CConstraintInterval.h"
#include "gpopt/base/CConstraintDisjunction.h"

#include "gpopt/exception.h"

#include "gpopt/operators/ops.h"
#include "gpopt/operators/CNormalizer.h"
#include "gpopt/operators/CPredicateUtils.h"
#include "gpopt/operators/CPhysicalJoin.h"

#include "gpopt/mdcache/CMDAccessor.h"
#include "gpopt/mdcache/CMDAccessorUtils.h"
#include "naucrates/md/IMDScalarOp.h"
#include "naucrates/md/IMDType.h"
#include "naucrates/md/CMDIdGPDB.h"
#include "naucrates/md/IMDCast.h"

#include "naucrates/dxl/gpdb_types.h"
#include "naucrates/statistics/CStatistics.h"

using namespace gpopt;
using namespace gpmd;

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FNegatedBooleanScalarIdent
//
//	@doc:
//		Check if the expression is a negated boolean scalar identifier
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FNegatedBooleanScalarIdent
	(
	CExpression *pexprPred
	)
{
	GPOS_ASSERT(NULL != pexprPred);

	if (CPredicateUtils::FNot(pexprPred))
	{
		return (FNot(pexprPred) && FBooleanScalarIdent((*pexprPred)[0]));
	}

	return false;
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FBooleanScalarIdent
//
//	@doc:
//		Check if the expression is a boolean scalar identifier
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FBooleanScalarIdent
	(
	CExpression *pexprPred
	)
{
	GPOS_ASSERT(NULL != pexprPred);

	if (COperator::EopScalarIdent == pexprPred->Pop()->Eopid())
	{
		CScalarIdent *popScIdent = CScalarIdent::PopConvert(pexprPred->Pop());
		if (IMDType::EtiBool == popScIdent->Pcr()->Pmdtype()->Eti())
		{
			return true;
		}
	}

	return false;
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FEquality
//
//	@doc:
//		Is the given expression an equality comparison
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FEquality
	(
	CExpression *pexpr
	)
{
	return FComparison(pexpr, IMDType::EcmptEq);
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FComparison
//
//	@doc:
//		Is the given expression a comparison
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FComparison
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	return COperator::EopScalarCmp == pexpr->Pop()->Eopid();
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FComparison
//
//	@doc:
//		Is the given expression a comparison of the given type
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FComparison
	(
	CExpression *pexpr,
	IMDType::ECmpType ecmpt
	)
{
	GPOS_ASSERT(NULL != pexpr);

	COperator *pop = pexpr->Pop();

	if (COperator::EopScalarCmp != pop->Eopid())
	{
		return false;
	}

	CScalarCmp *popScCmp = CScalarCmp::PopConvert(pop);
	GPOS_ASSERT(NULL != popScCmp);

	return ecmpt == popScCmp->Ecmpt();
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FComparison
//
//	@doc:
//		Is the given expression a comparison over the given column. A comparison
//		can only be between the given column and an expression involving only
//		the allowed columns. If the allowed columns set is NULL, then we only want
//		constant comparisons.
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FComparison
	(
	CExpression *pexpr,
	CColRef *pcr,
	CColRefSet *pcrsAllowedRefs // other column references allowed in the comparison
	)
{
	GPOS_ASSERT(NULL != pexpr);

	if (COperator::EopScalarCmp != pexpr->Pop()->Eopid())
	{
		return false;
	}

	CExpression *pexprLeft = (*pexpr)[0];
	CExpression *pexprRight = (*pexpr)[1];

	if (CUtils::FScalarIdent(pexprLeft, pcr) || CUtils::FBinaryCoercibleCastedScId(pexprLeft, pcr))
	{
		return FValidRefsOnly(pexprRight, pcrsAllowedRefs);
	}

	if (CUtils::FScalarIdent(pexprRight, pcr) || CUtils::FBinaryCoercibleCastedScId(pexprRight, pcr))
	{
		return FValidRefsOnly(pexprLeft, pcrsAllowedRefs);
	}

	return false;
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FValidRefsOnly
//
//	@doc:
//		Check whether the given expression contains references to only the given
//		columns. If pcrsAllowedRefs is NULL, then check whether the expression has
//		no column references and no volatile functions
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FValidRefsOnly
	(
	CExpression *pexprScalar,
	CColRefSet *pcrsAllowedRefs
	)
{
	CDrvdPropScalar *pdpscalar = CDrvdPropScalar::Pdpscalar(pexprScalar->PdpDerive());
	if (NULL != pcrsAllowedRefs)
	{
		return pcrsAllowedRefs->FSubset(pdpscalar->PcrsUsed());
	}

	return CUtils::FVarFreeExpr(pexprScalar) &&
			IMDFunction::EfsVolatile != pdpscalar->Pfp()->Efs();
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FConjunctionOfEqComparisons
//
//	@doc:
//		Is the given expression a conjunction of equality comparisons
//
//---------------------------------------------------------------------------
BOOL 
CPredicateUtils::FConjunctionOfEqComparisons
	(
	IMemoryPool *pmp,
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	if (FEquality(pexpr))
	{
		return true;
	}
	
	DrgPexpr *pdrgpexpr = PdrgpexprConjuncts(pmp, pexpr);
	const ULONG ulConjuncts = pdrgpexpr->UlLength();
	
	for (ULONG ul = 0; ul < ulConjuncts; ul++)
	{
		if (!FEquality((*pexpr)[ul]))
		{
			pdrgpexpr->Release();
			return false;
		}
	}
	
	pdrgpexpr->Release();
	return true;
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FHasNegatedChild
//
//	@doc:
//		Does the given expression have any NOT children?
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FHasNegatedChild
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	const ULONG ulArity = pexpr->UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		if (FNot((*pexpr)[ul]))
		{
			return true;
		}
	}

	return false;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::CollectChildren
//
//	@doc:
//		Append logical and scalar children of the given expression to
//		the given arrays
//
//---------------------------------------------------------------------------
void
CPredicateUtils::CollectChildren
	(
	CExpression *pexpr,
	DrgPexpr *pdrgpexprLogical,
	DrgPexpr *pdrgpexprScalar
	)
{
	GPOS_ASSERT(pexpr->Pop()->FLogical());

	const ULONG ulArity = pexpr->UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		CExpression *pexprChild = (*pexpr) [ul];
		pexprChild->AddRef();
		if (pexprChild->Pop()->FLogical())
		{
			pdrgpexprLogical->Append(pexprChild);
		}
		else
		{
			GPOS_ASSERT(pexprChild->Pop()->FScalar());

			pdrgpexprScalar->Append(pexprChild);
		}
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::CollectConjuncts
//
//	@doc:
//		Recursively collect conjuncts
//
//---------------------------------------------------------------------------
void
CPredicateUtils::CollectConjuncts
	(
	CExpression *pexpr,
	DrgPexpr *pdrgpexpr
	)
{
	GPOS_CHECK_STACK_SIZE;

	if (FAnd(pexpr))
	{
		const ULONG ulArity = pexpr->UlArity();
		for (ULONG ul = 0; ul < ulArity; ul++)
		{
			CollectConjuncts((*pexpr)[ul], pdrgpexpr);
		}
	}
	else
	{
		pexpr->AddRef();
		pdrgpexpr->Append(pexpr);
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::CollectDisjuncts
//
//	@doc:
//		Recursively collect disjuncts
//
//---------------------------------------------------------------------------
void
CPredicateUtils::CollectDisjuncts
	(
	CExpression *pexpr,
	DrgPexpr *pdrgpexpr
	)
{
	GPOS_CHECK_STACK_SIZE;

	if (FOr(pexpr))
	{
		const ULONG ulArity = pexpr->UlArity();
		for (ULONG ul = 0; ul < ulArity; ul++)
		{
			CollectDisjuncts((*pexpr)[ul], pdrgpexpr);
		}
	}
	else
	{
		pexpr->AddRef();
		pdrgpexpr->Append(pexpr);
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PdrgpexprConjuncts
//
//	@doc:
//		Extract conjuncts from a predicate
//
//---------------------------------------------------------------------------
DrgPexpr *
CPredicateUtils::PdrgpexprConjuncts
	(
	IMemoryPool *pmp,
	CExpression *pexpr
	)
{
	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	CollectConjuncts(pexpr, pdrgpexpr);

	return pdrgpexpr;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PdrgpexprDisjuncts
//
//	@doc:
//		Extract disjuncts from a predicate
//
//---------------------------------------------------------------------------
DrgPexpr *
CPredicateUtils::PdrgpexprDisjuncts
	(
	IMemoryPool *pmp,
	CExpression *pexpr
	)
{
	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	CollectDisjuncts(pexpr, pdrgpexpr);

	return pdrgpexpr;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PdrgpexprExpandDisjuncts
//
//	@doc:
//		This function expects an array of disjuncts (children of OR operator),
//		the function expands disjuncts in the given array by converting
//		ArrayComparison to AND/OR tree and deduplicating resulting disjuncts
//
//---------------------------------------------------------------------------
DrgPexpr *
CPredicateUtils::PdrgpexprExpandDisjuncts
	(
	IMemoryPool *pmp,
	DrgPexpr *pdrgpexprDisjuncts
	)
{
	GPOS_ASSERT(NULL != pdrgpexprDisjuncts);

	DrgPexpr *pdrgpexprExpanded = GPOS_NEW(pmp) DrgPexpr(pmp);
	const ULONG ulSize = pdrgpexprDisjuncts->UlLength();
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		CExpression *pexpr = (*pdrgpexprDisjuncts)[ul];
		if (COperator::EopScalarArrayCmp == pexpr->Pop()->Eopid())
		{
			CExpression *pexprExpanded = CScalarArrayCmp::PexprExpand(pmp, pexpr);
			if (FOr(pexprExpanded))
			{
				DrgPexpr *pdrgpexprArrayCmpDisjuncts = PdrgpexprDisjuncts(pmp, pexprExpanded);
				CUtils::AddRefAppend<CExpression, CleanupRelease>(pdrgpexprExpanded, pdrgpexprArrayCmpDisjuncts);
				pdrgpexprArrayCmpDisjuncts->Release();
				pexprExpanded->Release();
			}
			else
			{
				pdrgpexprExpanded->Append(pexprExpanded);
			}

			continue;
		}

		if (FAnd(pexpr))
		{
			DrgPexpr *pdrgpexprConjuncts = PdrgpexprConjuncts(pmp, pexpr);
			DrgPexpr *pdrgpexprExpandedConjuncts = PdrgpexprExpandConjuncts(pmp, pdrgpexprConjuncts);
			pdrgpexprConjuncts->Release();
			pdrgpexprExpanded->Append(PexprConjunction(pmp, pdrgpexprExpandedConjuncts));

			continue;
		}

		pexpr->AddRef();
		pdrgpexprExpanded->Append(pexpr);
	}

	DrgPexpr *pdrgpexprResult = CUtils::PdrgpexprDedup(pmp, pdrgpexprExpanded);
	pdrgpexprExpanded->Release();

	return pdrgpexprResult;
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PdrgpexprExpandConjuncts
//
//	@doc:
//		This function expects an array of conjuncts (children of AND operator),
//		the function expands conjuncts in the given array by converting
//		ArrayComparison to AND/OR tree and deduplicating resulting conjuncts
//
//---------------------------------------------------------------------------
DrgPexpr *
CPredicateUtils::PdrgpexprExpandConjuncts
	(
	IMemoryPool *pmp,
	DrgPexpr *pdrgpexprConjuncts
	)
{
	GPOS_ASSERT(NULL != pdrgpexprConjuncts);

	DrgPexpr *pdrgpexprExpanded = GPOS_NEW(pmp) DrgPexpr(pmp);
	const ULONG ulSize = pdrgpexprConjuncts->UlLength();
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		CExpression *pexpr = (*pdrgpexprConjuncts)[ul];
		if (COperator::EopScalarArrayCmp == pexpr->Pop()->Eopid())
		{
			CExpression *pexprExpanded = CScalarArrayCmp::PexprExpand(pmp, pexpr);
			if (FAnd(pexprExpanded))
			{
				DrgPexpr *pdrgpexprArrayCmpConjuncts = PdrgpexprConjuncts(pmp, pexprExpanded);
				CUtils::AddRefAppend<CExpression, CleanupRelease>(pdrgpexprExpanded, pdrgpexprArrayCmpConjuncts);
				pdrgpexprArrayCmpConjuncts->Release();
				pexprExpanded->Release();
			}
			else
			{
				pdrgpexprExpanded->Append(pexprExpanded);
			}

			continue;
		}

		if (FOr(pexpr))
		{
			DrgPexpr *pdrgpexprDisjuncts = PdrgpexprDisjuncts(pmp, pexpr);
			DrgPexpr *pdrgpexprExpandedDisjuncts = PdrgpexprExpandDisjuncts(pmp, pdrgpexprDisjuncts);
			pdrgpexprDisjuncts->Release();
			pdrgpexprExpanded->Append(PexprDisjunction(pmp, pdrgpexprExpandedDisjuncts));

			continue;
		}

		pexpr->AddRef();
		pdrgpexprExpanded->Append(pexpr);
	}

	DrgPexpr *pdrgpexprResult = CUtils::PdrgpexprDedup(pmp, pdrgpexprExpanded);
	pdrgpexprExpanded->Release();

	return pdrgpexprResult;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FSkippable
//
//	@doc:
//		Check if a conjunct/disjunct can be skipped
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FSkippable
	(
	CExpression *pexpr,
	BOOL fConjunction
	)
{
	return ((fConjunction && CUtils::FScalarConstTrue(pexpr)) || (!fConjunction
			&& CUtils::FScalarConstFalse(pexpr)));
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FReducible
//
//	@doc:
//		Check if a conjunction/disjunction can be reduced to a constant
//		True/False based on the given conjunct/disjunct
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FReducible
	(
	CExpression *pexpr,
	BOOL fConjunction
	)
{
	return ((fConjunction && CUtils::FScalarConstFalse(pexpr))
			|| (!fConjunction && CUtils::FScalarConstTrue(pexpr)));
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::EcmptReverse
//
//	@doc:
//		Reverse the given operator type, for example > => <, <= => >=
//
//---------------------------------------------------------------------------
IMDType::ECmpType
CPredicateUtils::EcmptReverse
	(
	IMDType::ECmpType ecmpt
	)
{
	GPOS_ASSERT(IMDType::EcmptOther > ecmpt);

	IMDType::ECmpType rgrgecmpt[][2] = {
			{ IMDType::EcmptEq, IMDType::EcmptEq }, { IMDType::EcmptG,
					IMDType::EcmptL },
			{ IMDType::EcmptGEq, IMDType::EcmptLEq }, { IMDType::EcmptNEq,
					IMDType::EcmptNEq } };

	const ULONG ulSize = GPOS_ARRAY_SIZE(rgrgecmpt);

	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		IMDType::ECmpType *pecmpt = rgrgecmpt[ul];

		if (pecmpt[0] == ecmpt)
		{
			return pecmpt[1];
		}

		if (pecmpt[1] == ecmpt)
		{
			return pecmpt[0];
		}
	}

	GPOS_ASSERT(!"Comparison does not have a reverse");

	return IMDType::EcmptOther;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FLikePredicate
//
//	@doc:
//		Is the condition a LIKE predicate
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FLikePredicate
	(
	IMDId *pmdid
	)
{
	GPOS_ASSERT(NULL != pmdid);

	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();
	const IMDScalarOp *pmdscop = pmda->Pmdscop(pmdid);

	const CWStringConst *pstrOpName = pmdscop->Mdname().Pstr();

	// comparison semantics for statistics purposes is looser
	// than regular comparison
	CWStringConst pstrLike(GPOS_WSZ_LIT("~~"));
	if (!pstrOpName->FEquals(&pstrLike))
	{
		return false;
	}

	return true;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FLikePredicate
//
//	@doc:
//		Is the condition a LIKE predicate
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FLikePredicate
	(
	CExpression *pexpr
	)
{
	COperator *pop = pexpr->Pop();
	if (COperator::EopScalarCmp != pop->Eopid())
	{
		return false;
	}

	CScalarCmp *popScCmp = CScalarCmp::PopConvert(pop);
	IMDId *pmdid = popScCmp->PmdidOp();

	return FLikePredicate(pmdid);
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::ExtractLikePredComponents
//
//	@doc:
//		Extract the components of a LIKE predicate
//
//---------------------------------------------------------------------------
void
CPredicateUtils::ExtractLikePredComponents
	(
	CExpression *pexprPred,
	CExpression **ppexprScIdent,
	CExpression **ppexprConst
	)
{
	GPOS_ASSERT(NULL != pexprPred);
	GPOS_ASSERT(2 == pexprPred->UlArity());
	GPOS_ASSERT(FLikePredicate(pexprPred));

	CExpression *pexprLeft = (*pexprPred)[0];
	CExpression *pexprRight = (*pexprPred)[1];

	*ppexprScIdent = NULL;
	*ppexprConst = NULL;

	CExpression *pexprLeftNoCast = pexprLeft;
	CExpression *pexprRightNoCast = pexprRight;

	if (COperator::EopScalarCast == pexprLeft->Pop()->Eopid())
	{
		pexprLeftNoCast = (*pexprLeft)[0];
	}

	if (COperator::EopScalarCast == pexprRight->Pop()->Eopid())
	{
		pexprRightNoCast = (*pexprRight)[0];
	}

	if (COperator::EopScalarIdent == pexprLeftNoCast->Pop()->Eopid())
	{
		*ppexprScIdent = pexprLeftNoCast;
	}
	else if (COperator::EopScalarIdent == pexprRightNoCast->Pop()->Eopid())
	{
		*ppexprScIdent = pexprRightNoCast;
	}

	if (COperator::EopScalarConst == pexprLeftNoCast->Pop()->Eopid())
	{
		*ppexprConst = pexprLeftNoCast;
	}
	else if (COperator::EopScalarConst == pexprRightNoCast->Pop()->Eopid())
	{
		*ppexprConst = pexprRightNoCast;
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::ExtractComponents
//
//	@doc:
//		Extract components in a comparison expression on the given key
//
//---------------------------------------------------------------------------
void
CPredicateUtils::ExtractComponents
	(
	CExpression *pexprScCmp,
	CColRef *pcrKey,
	CExpression **ppexprKey,
	CExpression **ppexprOther,
	IMDType::ECmpType *pecmpt
	)
{
	GPOS_ASSERT(NULL != pexprScCmp);
	GPOS_ASSERT(NULL != pcrKey);
	GPOS_ASSERT(FComparison(pexprScCmp));

	*ppexprKey = NULL;
	*ppexprOther = NULL;

	CExpression *pexprLeft = (*pexprScCmp)[0];
	CExpression *pexprRight = (*pexprScCmp)[1];

	IMDType::ECmpType ecmpt =
			CScalarCmp::PopConvert(pexprScCmp->Pop())->Ecmpt();

	if (CUtils::FScalarIdent(pexprLeft, pcrKey) || CUtils::FBinaryCoercibleCastedScId(pexprLeft, pcrKey))
	{
		*ppexprKey = pexprLeft;
		*ppexprOther = pexprRight;
		*pecmpt = ecmpt;
	}
	else if (CUtils::FScalarIdent(pexprRight, pcrKey) || CUtils::FBinaryCoercibleCastedScId(pexprRight, pcrKey))
	{
		*ppexprKey = pexprRight;
		*ppexprOther = pexprLeft;
		*pecmpt = EcmptReverse(ecmpt);
	}

	GPOS_ASSERT(NULL != *ppexprKey && NULL != *ppexprOther);
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprConjDisj
//
//	@doc:
//		Create conjunction/disjunction from array of components;
//		Takes ownership over the given array of expressions
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprConjDisj
	(
	IMemoryPool *pmp,
	DrgPexpr *pdrgpexpr,
	BOOL fConjunction
	)
{
	CScalarBoolOp::EBoolOperator eboolop = CScalarBoolOp::EboolopAnd;
	if (!fConjunction)
	{
		eboolop = CScalarBoolOp::EboolopOr;
	}

	DrgPexpr *pdrgpexprFinal = pdrgpexpr;

	pdrgpexprFinal = GPOS_NEW(pmp) DrgPexpr(pmp);
	ULONG ulSize = 0;
	if (NULL != pdrgpexpr)
	{
		ulSize = pdrgpexpr->UlLength();
	}

	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		CExpression *pexpr = (*pdrgpexpr)[ul];

		if (FSkippable(pexpr, fConjunction))
		{
			// skip current conjunct/disjunct
			continue;
		}

		if (FReducible(pexpr, fConjunction))
		{
			// a False (True) conjunct (disjunct) yields the whole conjunction (disjunction) False (True)
			CRefCount::SafeRelease(pdrgpexpr);
			CRefCount::SafeRelease(pdrgpexprFinal);

			return CUtils::PexprScalarConstBool(pmp, !fConjunction /*fValue*/);
		}

		// add conjunct/disjunct to result array
		pexpr->AddRef();
		pdrgpexprFinal->Append(pexpr);
	}
	CRefCount::SafeRelease(pdrgpexpr);

	// assemble result
	CExpression *pexprResult = NULL;
	if (NULL != pdrgpexprFinal && (0 < pdrgpexprFinal->UlLength()))
	{
		if (1 == pdrgpexprFinal->UlLength())
		{
			pexprResult = (*pdrgpexprFinal)[0];
			pexprResult->AddRef();
			pdrgpexprFinal->Release();

			return pexprResult;
		}

		return CUtils::PexprScalarBoolOp(pmp, eboolop, pdrgpexprFinal);
	}

	pexprResult = CUtils::PexprScalarConstBool(pmp, fConjunction /*fValue*/);
	CRefCount::SafeRelease(pdrgpexprFinal);

	return pexprResult;
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprConjunction
//
//	@doc:
//		Create conjunction from array of components;
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprConjunction
	(
	IMemoryPool *pmp,
	DrgPexpr *pdrgpexpr
	)
{
	return PexprConjDisj(pmp, pdrgpexpr, true /*fConjunction*/);
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprDisjunction
//
//	@doc:
//		Create disjunction from array of components;
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprDisjunction
	(
	IMemoryPool *pmp,
	DrgPexpr *pdrgpexpr
	)
{
	return PexprConjDisj(pmp, pdrgpexpr, false /*fConjunction*/);
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprConjDisj
//
//	@doc:
//		Create a conjunction/disjunction of two components;
//		Does *not* take ownership over given expressions
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprConjDisj
	(
	IMemoryPool *pmp,
	CExpression *pexprOne,
	CExpression *pexprTwo,
	BOOL fConjunction
	)
{
	GPOS_ASSERT(NULL != pexprOne);
	GPOS_ASSERT(NULL != pexprTwo);

	if (pexprOne == pexprTwo)
	{
		pexprOne->AddRef();
		return pexprOne;
	}

	DrgPexpr *pdrgpexprOne = NULL;
	DrgPexpr *pdrgpexprTwo = NULL;

	if (fConjunction)
	{
		pdrgpexprOne = PdrgpexprConjuncts(pmp, pexprOne);
		pdrgpexprTwo = PdrgpexprConjuncts(pmp, pexprTwo);
	}
	else
	{
		pdrgpexprOne = PdrgpexprDisjuncts(pmp, pexprOne);
		pdrgpexprTwo = PdrgpexprDisjuncts(pmp, pexprTwo);
	}

	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	CUtils::AddRefAppend<CExpression, CleanupRelease>(pdrgpexpr, pdrgpexprOne);
	CUtils::AddRefAppend<CExpression, CleanupRelease>(pdrgpexpr, pdrgpexprTwo);

	pdrgpexprOne->Release();
	pdrgpexprTwo->Release();

	return PexprConjDisj(pmp, pdrgpexpr, fConjunction);
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprConjunction
//
//	@doc:
//		Create a conjunction of two components;
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprConjunction
	(
	IMemoryPool *pmp,
	CExpression *pexprOne,
	CExpression *pexprTwo
	)
{
	return PexprConjDisj(pmp, pexprOne, pexprTwo, true /*fConjunction*/);
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprDisjunction
//
//	@doc:
//		Create a disjunction of two components;
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprDisjunction
	(
	IMemoryPool *pmp,
	CExpression *pexprOne,
	CExpression *pexprTwo
	)
{
	return PexprConjDisj(pmp, pexprOne, pexprTwo, false /*fConjunction*/);
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PdrgpexprPlainEqualities
//
//	@doc:
//		Extract equality predicates over scalar identifiers
//
//---------------------------------------------------------------------------
DrgPexpr *
CPredicateUtils::PdrgpexprPlainEqualities
	(
	IMemoryPool *pmp,
	DrgPexpr *pdrgpexpr
	)
{
	DrgPexpr *pdrgpexprEqualities = GPOS_NEW(pmp) DrgPexpr(pmp);
	const ULONG ulArity = pdrgpexpr->UlLength();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		CExpression *pexprCurr = (*pdrgpexpr)[ul];

		if (FPlainEquality(pexprCurr))
		{
			pexprCurr->AddRef();
			pdrgpexprEqualities->Append(pexprCurr);
		}
	}
	return pdrgpexprEqualities;
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FPlainEquality
//
//	@doc:
//		Is an expression an equality over scalar identifiers
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FPlainEquality
	(
	CExpression *pexpr
	)
{
	if (FEquality(pexpr))
	{
		CExpression *pexprLeft = (*pexpr)[0];
		CExpression *pexprRight = (*pexpr)[1];

		// check if the scalar condition is over scalar idents
		if (COperator::EopScalarIdent == pexprLeft->Pop()->Eopid()
				&& COperator::EopScalarIdent == pexprRight->Pop()->Eopid())
		{
			return true;
		}
	}
	return false;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FSelfComparison
//
//	@doc:
//		Is an expression a self comparison on some column
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FSelfComparison
	(
	CExpression *pexpr,
	IMDType::ECmpType *pecmpt
	)
{
	GPOS_ASSERT(NULL != pexpr);
	GPOS_ASSERT(NULL != pecmpt);

	*pecmpt = IMDType::EcmptOther;
	COperator *pop = pexpr->Pop();
	if (CUtils::FScalarCmp(pexpr))
	{
		*pecmpt = CScalarCmp::PopConvert(pop)->Ecmpt();
		COperator *popLeft = (*pexpr)[0]->Pop();
		COperator *popRight = (*pexpr)[1]->Pop();

		// return true if comparison is over the same column, and that column
		// is not nullable
		if (COperator::EopScalarIdent != popLeft->Eopid() ||
			COperator::EopScalarIdent != popRight->Eopid() ||
			CScalarIdent::PopConvert(popLeft)->Pcr() != CScalarIdent::PopConvert(popRight)->Pcr())
		{
			return false;
		}

		CColRef *pcr = const_cast<CColRef *>(CScalarIdent::PopConvert(popLeft)->Pcr());

		return CColRef::EcrtTable == pcr->Ecrt() &&
				!CColRefTable::PcrConvert(pcr)->FNullable();
	}

	return false;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprEliminateSelfComparison
//
//	@doc:
//		Eliminate self comparison and replace it with True or False if possible
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprEliminateSelfComparison
	(
	IMemoryPool *pmp,
	CExpression *pexpr
	)
{
	GPOS_ASSERT(pexpr->Pop()->FScalar());

	pexpr->AddRef();
	CExpression *pexprNew = pexpr;
	IMDType::ECmpType ecmpt = IMDType::EcmptOther;
	if (FSelfComparison(pexpr, &ecmpt))
	{
		switch (ecmpt)
		{
			case IMDType::EcmptEq:
			case IMDType::EcmptLEq:
			case IMDType::EcmptGEq:
				pexprNew->Release();
				pexprNew = CUtils::PexprScalarConstBool(pmp, true /*fVal*/);
				break;

			case IMDType::EcmptNEq:
			case IMDType::EcmptL:
			case IMDType::EcmptG:
			case IMDType::EcmptIDF:
				pexprNew->Release();
				pexprNew = CUtils::PexprScalarConstBool(pmp, false /*fVal*/);
				break;

			default:
				break;
		}
	}

	return pexprNew;
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FINDFScalarIdents
//
//	@doc:
//		Is the given expression in the form (col1 Is NOT DISTINCT FROM col2)
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FINDFScalarIdents
	(
	CExpression *pexpr
	)
{
	if (!FNot(pexpr))
	{
		return false;
	}

	CExpression *pexprChild = (*pexpr)[0];
	if (COperator::EopScalarIsDistinctFrom != pexprChild->Pop()->Eopid())
	{
		return false;
	}

	CExpression *pexprOuter = (*pexprChild)[0];
	CExpression *pexprInner = (*pexprChild)[1];

	return (COperator::EopScalarIdent == pexprOuter->Pop()->Eopid()
			&& COperator::EopScalarIdent == pexprInner->Pop()->Eopid());
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FIDFScalarIdents
//
//	@doc:
//		Is the given expression in the form (col1 Is DISTINCT FROM col2)
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FIDFScalarIdents
	(
	CExpression *pexpr
	)
{
	if (COperator::EopScalarIsDistinctFrom != pexpr->Pop()->Eopid())
	{
		return false;
	}

	CExpression *pexprOuter = (*pexpr)[0];
	CExpression *pexprInner = (*pexpr)[1];

	return (COperator::EopScalarIdent == pexprOuter->Pop()->Eopid()
			&& COperator::EopScalarIdent == pexprInner->Pop()->Eopid());
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FIDFFalse
//
//	@doc:
//		Is the given expression in the form 'expr IS DISTINCT FROM false)'
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FIDFFalse
	(
	CExpression *pexpr
	)
{
	if (COperator::EopScalarIsDistinctFrom != pexpr->Pop()->Eopid())
	{
		return false;
	}

	return CUtils::FScalarConstFalse((*pexpr)[0]) ||
			CUtils::FScalarConstFalse((*pexpr)[1]);
}
  
//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FIDF
//
//	@doc:
//		Is the given expression in the form (expr IS DISTINCT FROM expr)
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FIDF
	(
	CExpression *pexpr
	)
{
	return (COperator::EopScalarIsDistinctFrom == pexpr->Pop()->Eopid());
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FINDF
//
//	@doc:
//		Is the given expression in the form (expr Is NOT DISTINCT FROM expr)
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FINDF
	(
	CExpression *pexpr
	)
{
	return (FNot(pexpr) && FIDF((*pexpr)[0]));
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprINDFConjunction
//
//	@doc:
//		Generate a conjunction of INDF expressions between corresponding
//		columns in the given arrays
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprINDFConjunction
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcrFirst,
	DrgPcr *pdrgpcrSecond
	)
{
	GPOS_ASSERT(NULL != pdrgpcrFirst);
	GPOS_ASSERT(NULL != pdrgpcrSecond);
	GPOS_ASSERT(pdrgpcrFirst->UlLength() == pdrgpcrSecond->UlLength());
	GPOS_ASSERT(0 < pdrgpcrFirst->UlLength());

	const ULONG ulCols = pdrgpcrFirst->UlLength();
	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	for (ULONG ul = 0; ul < ulCols; ul++)
	{
		pdrgpexpr->Append(CUtils::PexprINDF(pmp, (*pdrgpcrFirst)[ul], (*pdrgpcrSecond)[ul]));
	}

	return PexprConjunction(pmp, pdrgpexpr);
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FRangeOrEqComp
//
//	@doc:
// 		Is the given expression a scalar range or equality comparison
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FRangeOrEqComp
	(
	CExpression *pexpr
	)
{
	if (!FCompareIdentToConst(pexpr))
	{
		return false;
	}
	COperator *pop = pexpr->Pop();
	CScalarCmp *popScCmp = CScalarCmp::PopConvert(pop);
	IMDType::ECmpType cmptype = popScCmp->Ecmpt();

	if (cmptype == IMDType::EcmptNEq ||
		cmptype == IMDType::EcmptIDF ||
		cmptype == IMDType::EcmptOther)
	{
		return false;
	}

	return true;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FCompareIdentToConst
//
//	@doc:
// 		Is the given expression a comparison between a scalar ident and a constant
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FCompareIdentToConst
	(
	CExpression *pexpr
	)
{
	COperator *pop = pexpr->Pop();

	if (COperator::EopScalarCmp != pop->Eopid())
	{
		return false;
	}

	CExpression *pexprLeft = (*pexpr)[0];
	CExpression *pexprRight = (*pexpr)[1];

	// left side must be scalar ident
	if (COperator::EopScalarIdent != pexprLeft->Pop()->Eopid())
	{
		return false;
	}

	// right side must be a constant
	if (COperator::EopScalarConst != pexprRight->Pop()->Eopid())
	{
		return false;
	}

	return true;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FIdentCmpConstIgnoreCast
//
//	@doc:
// 		Is the given expression of the form (col cmp constant)
//		ignoring casting on either sides
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FIdentCmpConstIgnoreCast
	(
	CExpression *pexpr
	)
{
	COperator *pop = pexpr->Pop();

	if (COperator::EopScalarCmp != pop->Eopid())
	{
		return false;
	}

	CExpression *pexprLeft = (*pexpr)[0];
	CExpression *pexprRight = (*pexpr)[1];

	// col cmp const
	if (COperator::EopScalarIdent == pexprLeft->Pop()->Eopid() && COperator::EopScalarConst == pexprRight->Pop()->Eopid())
	{
		return true;
	}

	// cast(col) cmp const
	if (CScalarIdent::FCastedScId(pexprLeft) && COperator::EopScalarConst == pexprRight->Pop()->Eopid())
	{
		return true;
	}

	// col cmp cast(constant)
	if (COperator::EopScalarIdent == pexprLeft->Pop()->Eopid() && CScalarConst::FCastedConst(pexprRight))
	{
		return true;
	}

	// cast(col) cmp cast(constant)
	if (CScalarIdent::FCastedScId(pexprLeft) && CScalarConst::FCastedConst(pexprRight))
	{
		return true;
	}

	return false;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FIdentIDFConstIgnoreCast
//
//	@doc:
// 		Is the given expression is of the form (col IS DISTINCT FROM const)
//		ignoring cast on either sides
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FIdentIDFConstIgnoreCast
	(
	CExpression *pexpr
	)
{
	COperator *pop = pexpr->Pop();

	if (COperator::EopScalarIsDistinctFrom != pop->Eopid())
	{
		return false;
	}

	CExpression *pexprLeft = (*pexpr)[0];
	CExpression *pexprRight = (*pexpr)[1];

	// col IDF const
	if (COperator::EopScalarIdent == pexprLeft->Pop()->Eopid() && COperator::EopScalarConst == pexprRight->Pop()->Eopid())
	{
		return true;
	}

	// cast(col) IDF const
	if (CScalarIdent::FCastedScId(pexprLeft) && COperator::EopScalarConst == pexprRight->Pop()->Eopid())
	{
		return true;
	}

	// col IDF cast(constant)
	if (COperator::EopScalarIdent == pexprLeft->Pop()->Eopid() && CScalarConst::FCastedConst(pexprRight))
	{
		return true;
	}

	// cast(col) IDF cast(constant)
	if (CScalarIdent::FCastedScId(pexprLeft) && CScalarConst::FCastedConst(pexprRight))
	{
		return true;
	}

	return false;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FIdentINDFConstIgnoreCast
//
//	@doc:
// 		Is the given expression of the form NOT (col IS DISTINCT FROM const)
//		ignoring cast on either sides
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FIdentINDFConstIgnoreCast
	(
	CExpression *pexpr
	)
{
	if (!FNot(pexpr))
	{
		return false;
	}

	return FIdentIDFConstIgnoreCast((*pexpr)[0]);
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FCompareIdentToConstArray
//
//	@doc:
// 		Is the given expression a comparison between a scalar ident
//		and a constant array
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FCompareIdentToConstArray
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	if (!CUtils::FScalarArrayCmp(pexpr) ||
		!CUtils::FScalarIdent((*pexpr)[0]) ||
		!CUtils::FScalarArray((*pexpr)[1]))
	{
		return false;
	}

	CExpression *pexprArray = (*pexpr)[1];
	const ULONG ulArity = pexprArray->UlArity();

	BOOL fAllConsts = true;
	for (ULONG ul = 0; fAllConsts && ul < ulArity; ul++)
	{
		fAllConsts = CUtils::FScalarConst((*pexprArray)[ul]);
	}

	return fAllConsts;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprPartPruningPredicate
//
//	@doc:
// 		Find a predicate that can be used for partition pruning with the given
//		part key in the array of expressions if one exists. Relevant predicates
//		are those that compare the partition key to expressions involving only
//		the allowed columns. If the allowed columns set is NULL, then we only want
//		constant comparisons.
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprPartPruningPredicate
	(
	IMemoryPool *pmp,
	const DrgPexpr *pdrgpexpr,
	CColRef *pcrPartKey,
	CExpression *pexprCol,	// const selection predicate on the given column obtained from query
	CColRefSet *pcrsAllowedRefs
	)
{
	const ULONG ulSize = pdrgpexpr->UlLength();

	DrgPexpr *pdrgpexprResult = GPOS_NEW(pmp) DrgPexpr(pmp);
	
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		CExpression *pexpr = (*pdrgpexpr)[ul];

		if (FBoolPredicateOnColumn(pexpr, pcrPartKey) ||
			FNullCheckOnColumn(pexpr, pcrPartKey) ||
			FDisjunctionOnColumn(pmp, pexpr, pcrPartKey, pcrsAllowedRefs))
		{
			pexpr->AddRef();
			pdrgpexprResult->Append(pexpr);
			continue;
		}

		if (FComparison(pexpr, pcrPartKey, pcrsAllowedRefs))
		{
			CScalarCmp *popCmp = CScalarCmp::PopConvert(pexpr->Pop());
			CDrvdPropScalar *pdpscalar = CDrvdPropScalar::Pdpscalar(pexpr->PdpDerive());
			
			if (!pdpscalar->Pfp()->FMasterOnly() && FRangeComparison(popCmp->Ecmpt()))
			{
				pexpr->AddRef();
				pdrgpexprResult->Append(pexpr);
			}
			
			continue;
		}
		
	}

	DrgPexpr *pdrgpexprResultNew = PdrgpexprAppendConjunctsDedup(pmp, pdrgpexprResult, pexprCol);
	pdrgpexprResult->Release();
	pdrgpexprResult = pdrgpexprResultNew;

	if (0 == pdrgpexprResult->UlLength())
	{
		pdrgpexprResult->Release();
		return NULL;
	}
	
	return PexprConjunction(pmp, pdrgpexprResult);
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PdrgpexprAppendConjunctsDedup
//
//	@doc:
//		Append the conjuncts from the given expression to the given array, removing
//		any duplicates, and return the resulting array
//
//---------------------------------------------------------------------------
DrgPexpr *
CPredicateUtils::PdrgpexprAppendConjunctsDedup
	(
	IMemoryPool *pmp,
	DrgPexpr *pdrgpexpr,
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pdrgpexpr);

	if (NULL == pexpr)
	{
		pdrgpexpr->AddRef();
		return pdrgpexpr;
	}

	DrgPexpr *pdrgpexprConjuncts = PdrgpexprConjuncts(pmp, pexpr);
	CUtils::AddRefAppend(pdrgpexprConjuncts, pdrgpexpr);

	DrgPexpr *pdrgpexprNew = CUtils::PdrgpexprDedup(pmp, pdrgpexprConjuncts);
	pdrgpexprConjuncts->Release();
	return pdrgpexprNew;
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FBoolPredicateOnColumn
//
//	@doc:
//		Check if the given expression is a boolean expression on the
//		given column, e.g. if its of the form "ScalarIdent(pcr)" or "Not(ScalarIdent(pcr))"
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FBoolPredicateOnColumn
	(
	CExpression *pexpr,
	CColRef *pcr
	)
{
	BOOL fBoolean = (IMDType::EtiBool == pcr->Pmdtype()->Eti());

	if (fBoolean && 
			(CUtils::FScalarIdent(pexpr, pcr) || 
					(FNot(pexpr) && CUtils::FScalarIdent((*pexpr)[0], pcr))))
	{
		return true;
	}
	
	return false;
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FNullCheckOnColumn
//
//	@doc:
//		Check if the given expression is a null check on the given column
// 		i.e. "is null" or "is not null"
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FNullCheckOnColumn
	(
	CExpression *pexpr,
	CColRef *pcr
	)
{
	GPOS_ASSERT(NULL != pexpr);
	GPOS_ASSERT(NULL != pcr);

	CExpression *pexprIsNull = pexpr;
	if (FNot(pexpr))
	{
		pexprIsNull = (*pexpr)[0];
	}

	if (CUtils::FScalarNullTest(pexprIsNull))
	{
		CExpression *pexprChild = (*pexprIsNull)[0];
		return (CUtils::FScalarIdent(pexprChild, pcr) || CUtils::FBinaryCoercibleCastedScId(pexprChild, pcr));
	}

	return false;
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FScArrayCmpOnColumn
//
//	@doc:
//		Check if the given expression is a scalar array cmp expression on the
//		given column
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FScArrayCmpOnColumn
	(
	CExpression *pexpr,
	CColRef *pcr,
	BOOL fConstOnly
	)
{
	GPOS_ASSERT(NULL != pexpr);
	
	if (COperator::EopScalarArrayCmp != pexpr->Pop()->Eopid())
	{
		return false;
	}
	
	CExpression *pexprLeft = (*pexpr)[0];
	CExpression *pexprRight = (*pexpr)[1];
	
	if (!CUtils::FScalarIdent(pexprLeft, pcr) || !CUtils::FScalarArray(pexprRight))
	{
		return false;
	}
	
	const ULONG ulArrayElems = pexprRight->UlArity();
	
	BOOL fSupported = true;
	for (ULONG ul = 0; ul < ulArrayElems && fSupported; ul++)
	{
		CExpression *pexprArrayElem = (*pexprRight)[ul];
		if (fConstOnly && !CUtils::FScalarConst(pexprArrayElem))
		{
			fSupported = false;
		}
	}
	
	return fSupported;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FDisjunctionOnColumn
//
//	@doc:
//		Check if the given expression is a disjunction of scalar cmp expression 
//		on the given column
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FDisjunctionOnColumn
	(
	IMemoryPool *pmp,
	CExpression *pexpr,
	CColRef *pcr,
	CColRefSet *pcrsAllowedRefs
	)
{
	if (!FOr(pexpr))
	{
		return false;
	}

	DrgPexpr *pdrgpexprDisjuncts = PdrgpexprDisjuncts(pmp, pexpr);
	const ULONG ulDisjuncts = pdrgpexprDisjuncts->UlLength();
	for (ULONG ulDisj = 0; ulDisj < ulDisjuncts; ulDisj++)
	{
		CExpression *pexprDisj = (*pdrgpexprDisjuncts)[ulDisj];
		if (!FComparison(pexprDisj, pcr, pcrsAllowedRefs) || !FRangeComparison(CScalarCmp::PopConvert(pexprDisj->Pop())->Ecmpt()))
		{
			pdrgpexprDisjuncts->Release();
			return false;
		}
	}
	
	pdrgpexprDisjuncts->Release();
	return true;	
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FRangeComparison
//
//	@doc:
//		Check if the given comparison type is one of the range comparisons, i.e. 
//		LT, GT, LEq, GEq, Eq
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FRangeComparison
	(
	IMDType::ECmpType ecmpt
	)
{
	return (IMDType::EcmptOther != ecmpt && IMDType::EcmptNEq != ecmpt);
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprExtractPredicatesOnPartKeys
//
//	@doc:
//		Extract interesting expressions involving the partitioning keys;
//		the function Add-Refs the returned copy if not null. Relevant predicates
//		are those that compare the partition keys to expressions involving only
//		the allowed columns. If the allowed columns set is NULL, then we only want
//		constant filters.
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprExtractPredicatesOnPartKeys
	(
	IMemoryPool *pmp,
	CExpression *pexprScalar,
	DrgDrgPcr *pdrgpdrgpcrPartKeys,
	CColRefSet *pcrsAllowedRefs,
	BOOL fUseConstraints
	)
{	
	GPOS_ASSERT(NULL != pdrgpdrgpcrPartKeys);
	if (GPOS_FTRACE(EopttraceDisablePartSelection))
	{
		return NULL;
	}

	// if we have any Array Comparisons, we expand them into conjunctions/disjunctions
	// of comparison predicates and then reconstruct scalar expression
	DrgPexpr *pdrgpexprConjuncts = PdrgpexprConjuncts(pmp, pexprScalar);
	DrgPexpr *pdrgpexprExpandedConjuncts = PdrgpexprExpandConjuncts(pmp, pdrgpexprConjuncts);
	pdrgpexprConjuncts->Release();
	CExpression *pexprExpandedScalar = PexprConjunction(pmp, pdrgpexprExpandedConjuncts);
	pdrgpexprConjuncts = PdrgpexprConjuncts(pmp, pexprExpandedScalar);

	DrgPcrs *pdrgpcrsChild = NULL;
	CConstraint *pcnstr = CConstraint::PcnstrFromScalarExpr(pmp, pexprExpandedScalar, &pdrgpcrsChild);
	CRefCount::SafeRelease(pdrgpcrsChild);
	pexprExpandedScalar->Release();

	// check if expanded scalar leads to a contradiction in computed constraint
	BOOL fContradiction = (NULL != pcnstr && pcnstr->FContradiction());
	if (fContradiction)
	{
		pdrgpexprConjuncts->Release();
		pcnstr->Release();

		return NULL;
	}

	const ULONG ulLevels = pdrgpdrgpcrPartKeys->UlLength();
	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	for (ULONG ul = 0; ul < ulLevels; ul++)
	{
		CColRef *pcr = CUtils::PcrExtractPartKey(pdrgpdrgpcrPartKeys, ul);
		CExpression *pexprCol = PexprPredicateCol(pmp, pcnstr, pcr, fUseConstraints);

		// look for a filter on the part key
		CExpression *pexprCmp =
			PexprPartPruningPredicate(pmp, pdrgpexprConjuncts, pcr, pexprCol, pcrsAllowedRefs);
		CRefCount::SafeRelease(pexprCol);
		GPOS_ASSERT_IMP(NULL != pexprCmp && COperator::EopScalarCmp == pexprCmp->Pop()->Eopid(),
				IMDType::EcmptOther != CScalarCmp::PopConvert(pexprCmp->Pop())->Ecmpt());

		if (NULL != pexprCmp && !CUtils::FScalarConstTrue(pexprCmp))
		{
			// include comparison predicate if it is non-trivial
			pexprCmp->AddRef();
			pdrgpexpr->Append(pexprCmp);
		}
		CRefCount::SafeRelease(pexprCmp);
	}

	pdrgpexprConjuncts->Release();
	CRefCount::SafeRelease(pcnstr);

	if (0 == pdrgpexpr->UlLength())
	{
		pdrgpexpr->Release();
		return NULL;
	}

	return PexprConjunction(pmp, pdrgpexpr);
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprPredicateCol
//
//	@doc:
//		Extract the constraint on the given column and return the corresponding
//		scalar expression
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprPredicateCol
	(
	IMemoryPool *pmp,
	CConstraint *pcnstr,
	CColRef *pcr,
	BOOL fUseConstraints
	)
{
	if (NULL == pcnstr || !fUseConstraints)
	{
		return NULL;
	}

	CExpression *pexprCol = NULL;
	CConstraint *pcnstrCol = pcnstr->Pcnstr(pmp, pcr);
	if (NULL != pcnstrCol && !pcnstrCol->FUnbounded())
	{
		pexprCol = pcnstrCol->PexprScalar(pmp);
		if (!CUtils::FScalarNotNull(pexprCol))
		{
			// TODO:  - Oct 15, 2014; we should also support NOT NULL predicates
			// and revisit the condition above
			pexprCol->AddRef();
		}
		else
		{
			pexprCol = NULL;
		}
	}

	CRefCount::SafeRelease(pcnstrCol);

	return pexprCol;
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FCompareColToConstOrCol
//
//	@doc:
// 		Checks if comparison is between two columns, or a column and a const
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FCompareColToConstOrCol
	(
	CExpression *pexprScalar
	)
{
	if (!CUtils::FScalarCmp(pexprScalar))
	{
		return false;
	}

	CExpression *pexprLeft = (*pexprScalar)[0];
	CExpression *pexprRight = (*pexprScalar)[1];

	BOOL fColLeft = CUtils::FScalarIdent(pexprLeft);
	BOOL fColRight = CUtils::FScalarIdent(pexprRight);
	BOOL fConstLeft = CUtils::FScalarConst(pexprLeft);
	BOOL fConstRight = CUtils::FScalarConst(pexprRight);

	return  (fColLeft && fColRight) ||
			(fColLeft && fConstRight) ||
			(fConstLeft && fColRight);
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FConstColumn
//
//	@doc:
// 		Checks if the given constraint specifies a constant column
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FConstColumn
	(
	CConstraint *pcnstr,
	const CColRef *
#ifdef GPOS_DEBUG
	pcr
#endif // GPOS_DEBUG
	)
{
	if (NULL == pcnstr || CConstraint::EctInterval != pcnstr->Ect())
	{
		// no constraint on column or constraint is not an interval
		return false;
	}
	
	GPOS_ASSERT(pcnstr->FConstraint(pcr));
	
	CConstraintInterval *pcnstrInterval = dynamic_cast<CConstraintInterval *>(pcnstr);
	DrgPrng *pdrgprng = pcnstrInterval->Pdrgprng();
	if (1 < pdrgprng->UlLength())
	{
		return false;
	}
	
	if (0 == pdrgprng->UlLength())
	{
		return pcnstrInterval->FIncludesNull();
	}
	
	GPOS_ASSERT(1 == pdrgprng->UlLength());
	
	const CRange *prng = (*pdrgprng)[0];
	
	return prng->FPoint() && !pcnstrInterval->FIncludesNull();
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FColumnDisjunctionOfConst
//
//	@doc:
// 		Checks if the given constraint specifies a set of constants for a column
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FColumnDisjunctionOfConst
	(
	CConstraint *pcnstr,
	const CColRef *pcr
	)
{
	if (FConstColumn(pcnstr, pcr))
	{
		return true;
	}
	
	if (NULL == pcnstr || CConstraint::EctInterval != pcnstr->Ect())
	{
		// no constraint on column or constraint is not an interval
		return false;
	}
	
	GPOS_ASSERT(pcnstr->FConstraint(pcr));
	
	GPOS_ASSERT(CConstraint::EctInterval == pcnstr->Ect());
		
	CConstraintInterval *pcnstrInterval = dynamic_cast<CConstraintInterval *>(pcnstr);
	return FColumnDisjunctionOfConst(pcnstrInterval, pcr);
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FColumnDisjunctionOfConst
//
//	@doc:
// 		Checks if the given constraint specifies a set of constants for a column
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FColumnDisjunctionOfConst
	(
	CConstraintInterval *pcnstrInterval,
	const CColRef *
#ifdef GPOS_DEBUG
	pcr
#endif
	)
{
	GPOS_ASSERT(pcnstrInterval->FConstraint(pcr));

	DrgPrng *pdrgprng = pcnstrInterval->Pdrgprng();
	
	if (0 == pdrgprng->UlLength())
	{
		return pcnstrInterval->FIncludesNull();
	}
	
	GPOS_ASSERT(0 < pdrgprng->UlLength());

	const ULONG ulRanges = pdrgprng->UlLength();
	
	for (ULONG ul = 0; ul < ulRanges; ul++)
	{
		CRange *prng = (*pdrgprng)[ul];
		if (!prng->FPoint())
		{
			return false;
		}
	}
	
	return true;
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprIndexLookupKeyOnLeft
//
//	@doc:
// 		Helper to create index lookup comparison predicate with index key
//		on left side
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprIndexLookupKeyOnLeft
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	CExpression *pexprScalar,
	const IMDIndex *pmdindex,
	DrgPcr *pdrgpcrIndex,
	CColRefSet *pcrsOuterRefs
	)
{
	GPOS_ASSERT(NULL != pexprScalar);

	CExpression *pexprLeft = (*pexprScalar)[0];
	CExpression *pexprRight = (*pexprScalar)[1];
	
	CColRefSet *pcrsIndex = GPOS_NEW(pmp) CColRefSet(pmp, pdrgpcrIndex);
			
	if ((CUtils::FScalarIdent(pexprLeft) && pcrsIndex->FMember(CScalarIdent::PopConvert(pexprLeft->Pop())->Pcr())) ||
		(CUtils::FBinaryCoercibleCast(pexprLeft) && pcrsIndex->FMember(CScalarIdent::PopConvert((*pexprLeft)[0]->Pop())->Pcr())))
	{
		// left expression is a scalar identifier or casted scalar identifier on an index key
		CColRefSet *pcrsUsedRight = CDrvdPropScalar::Pdpscalar(pexprRight->PdpDerive())->PcrsUsed();
		BOOL fSuccess = true;

		if (0 < pcrsUsedRight->CElements())
		{
			if (!pcrsUsedRight->FDisjoint(pcrsIndex))
			{
				// right argument uses index key, cannot use predicate for index lookup
				fSuccess = false;
			}
			else if (NULL != pcrsOuterRefs)
			{
				CColRefSet *pcrsOuterRefsRight = GPOS_NEW(pmp) CColRefSet(pmp, *pcrsUsedRight);
				pcrsOuterRefsRight->Difference(pcrsIndex);
				fSuccess = pcrsOuterRefs->FSubset(pcrsOuterRefsRight);
				pcrsOuterRefsRight->Release();
			}
		}

		fSuccess = (fSuccess && FCompatibleIndexPredicate(pexprScalar, pmdindex, pdrgpcrIndex, pmda));

		if (fSuccess)
		{
			pcrsIndex->Release();
			pexprScalar->AddRef();
			return pexprScalar;
		}
	}

	pcrsIndex->Release();
	return NULL;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprIndexLookupKeyOnRight
//
//	@doc:
// 		Helper to create index lookup comparison predicate with index key
//		on right side
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprIndexLookupKeyOnRight
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	CExpression *pexprScalar,
	const IMDIndex *pmdindex,
	DrgPcr *pdrgpcrIndex,
	CColRefSet *pcrsOuterRefs
	)
{
	GPOS_ASSERT(NULL != pexprScalar);

	CExpression *pexprLeft = (*pexprScalar)[0];
	CExpression *pexprRight = (*pexprScalar)[1];
	CScalarCmp *popScCmp = CScalarCmp::PopConvert(pexprScalar->Pop());
	
	const IMDScalarOp *pmdscalarop = pmda->Pmdscop(popScCmp->PmdidOp());
	IMDId *pmdidscalaropCommute = pmdscalarop->PmdidOpCommute();

	if (pmdidscalaropCommute->FValid())
	{
		
		// build new comparison after switching arguments and using commutative comparison operator
		pexprRight->AddRef();
		pexprLeft->AddRef();
		pmdidscalaropCommute->AddRef();
		const CWStringConst *pstr = pmda->Pmdscop(pmdidscalaropCommute)->Mdname().Pstr();

		CExpression *pexprCommuted = CUtils::PexprScalarCmp(pmp, pexprRight, pexprLeft, *pstr, pmdidscalaropCommute);
		CExpression *pexprIndexCond = PexprIndexLookupKeyOnLeft(pmp, pmda, pexprCommuted, pmdindex, pdrgpcrIndex, pcrsOuterRefs);
		pexprCommuted->Release();
		
		return pexprIndexCond;
	}
	
	return NULL;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprIndexLookup
//
//	@doc:
// 		Check if given expression is a valid index lookup predicate, and
//		return modified (as needed) expression to be used for index lookup,
//		a scalar expression is a valid index lookup predicate if it is in one
//		the two forms:
//			[index-key CMP expr]
//			[expr CMP index-key]
//		where expr is a scalar expression that is free of index keys and
//		may have outer references (in the case of index nested loops)
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprIndexLookup
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	CExpression *pexprScalar,
	const IMDIndex *pmdindex,
	DrgPcr *pdrgpcrIndex,
	CColRefSet *pcrsOuterRefs
	)
{
	GPOS_ASSERT(NULL != pexprScalar);
	GPOS_ASSERT(NULL != pdrgpcrIndex);

	if (!CUtils::FScalarCmp(pexprScalar))
	{
		return NULL;
	}

	IMDType::ECmpType cmptype = CScalarCmp::PopConvert(pexprScalar->Pop())->Ecmpt();
	if (cmptype == IMDType::EcmptNEq ||
		cmptype == IMDType::EcmptIDF ||
		cmptype == IMDType::EcmptOther)
	{
		return NULL;
	}

	CExpression *pexprIndexLookupKeyOnLeft = PexprIndexLookupKeyOnLeft(pmp, pmda, pexprScalar, pmdindex, pdrgpcrIndex, pcrsOuterRefs);
	if (NULL != pexprIndexLookupKeyOnLeft)
	{
		return pexprIndexLookupKeyOnLeft;
	}

	CExpression *pexprIndexLookupKeyOnRight = PexprIndexLookupKeyOnRight(pmp, pmda, pexprScalar, pmdindex, pdrgpcrIndex, pcrsOuterRefs);
	if (NULL != pexprIndexLookupKeyOnRight)
	{
		return pexprIndexLookupKeyOnRight;
	}

	return NULL;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::ExtractIndexPredicates
//
//	@doc:
// 		Split predicates into those that refer to an index key, and those that 
//		don't 
//
//---------------------------------------------------------------------------
void
CPredicateUtils::ExtractIndexPredicates
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	DrgPexpr *pdrgpexprPredicate,
	const IMDIndex *pmdindex,
	DrgPcr *pdrgpcrIndex,
	DrgPexpr *pdrgpexprIndex,
	DrgPexpr *pdrgpexprResidual,
	CColRefSet *pcrsAcceptedOuterRefs // outer refs that are acceptable in an index predicate
	)
{
	const ULONG ulLength = pdrgpexprPredicate->UlLength();
	
	CColRefSet *pcrsIndex = GPOS_NEW(pmp) CColRefSet(pmp, pdrgpcrIndex);

	for (ULONG ul = 0; ul < ulLength; ul++)
	{
		CExpression *pexprCond = (*pdrgpexprPredicate)[ul];

		pexprCond->AddRef();
		
		CColRefSet *pcrsUsed = GPOS_NEW(pmp) CColRefSet(pmp, *CDrvdPropScalar::Pdpscalar(pexprCond->PdpDerive())->PcrsUsed());
		if (NULL != pcrsAcceptedOuterRefs)
		{
			// filter out all accepted outer references
			pcrsUsed->Difference(pcrsAcceptedOuterRefs);
		}

		BOOL fSubset = (0 < pcrsUsed->CElements()) && (pcrsIndex->FSubset(pcrsUsed));
		pcrsUsed->Release();

		if (!fSubset)
		{
			pdrgpexprResidual->Append(pexprCond);
			continue;
		}
		
		DrgPexpr *pdrgpexprTarget = pdrgpexprIndex;

		if (CUtils::FScalarIdentBoolType(pexprCond))
		{
			// expression is a column identifier of boolean type: convert to "col = true"
			pexprCond = CUtils::PexprScalarEqCmp(pmp, pexprCond, CUtils::PexprScalarConstBool(pmp, true /*fVal*/, false /*fNull*/));
		}
		else if (FNot(pexprCond) && CUtils::FScalarIdentBoolType((*pexprCond)[0]))
		{
			// expression is of the form "not(col) for a column identifier of boolean type: convert to "col = false"
			CExpression *pexprScId = (*pexprCond)[0];
			pexprCond->Release();
			pexprScId->AddRef();
			pexprCond = CUtils::PexprScalarEqCmp(pmp, pexprScId, CUtils::PexprScalarConstBool(pmp, false /*fVal*/, false /*fNull*/));
		}
		else
		{
			// attempt building index lookup predicate
			CExpression *pexprLookupPred = PexprIndexLookup(pmp, pmda, pexprCond, pmdindex, pdrgpcrIndex, pcrsAcceptedOuterRefs);
			if (NULL != pexprLookupPred)
			{
				pexprCond->Release();
				pexprCond = pexprLookupPred;
			}
			else
			{
				// not a supported predicate
				pdrgpexprTarget = pdrgpexprResidual;
			}
		}

		pdrgpexprTarget->Append(pexprCond);
	}
	
	pcrsIndex->Release();
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::SeparateOuterRefs
//
//	@doc:
// 		Split given scalar expression into two conjunctions; without outer
//		references and with outer references
//
//---------------------------------------------------------------------------
void
CPredicateUtils::SeparateOuterRefs
	(
	IMemoryPool *pmp,
	CExpression *pexprScalar,
	CColRefSet *pcrsOuter,
	CExpression **ppexprLocal,
	CExpression **ppexprOuterRef
	)
{
	GPOS_ASSERT(NULL != pexprScalar);
	GPOS_ASSERT(NULL != pcrsOuter);
	GPOS_ASSERT(NULL != ppexprLocal);
	GPOS_ASSERT(NULL != ppexprOuterRef);

	CColRefSet *pcrsUsed = CDrvdPropScalar::Pdpscalar(pexprScalar->PdpDerive())->PcrsUsed();
	if (pcrsUsed->FDisjoint(pcrsOuter))
	{
		// if used columns are disjoint from outer references, return input expression
		pexprScalar->AddRef();
		*ppexprLocal = pexprScalar;
		*ppexprOuterRef = CUtils::PexprScalarConstBool(pmp, true /*fval*/);
		return;
	}

	DrgPexpr *pdrgpexpr = PdrgpexprConjuncts(pmp, pexprScalar);
	DrgPexpr *pdrgpexprLocal = GPOS_NEW(pmp) DrgPexpr(pmp);
	DrgPexpr *pdrgpexprOuterRefs = GPOS_NEW(pmp) DrgPexpr(pmp);

	const ULONG ulSize = pdrgpexpr->UlLength();
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		CExpression *pexprPred = (*pdrgpexpr)[ul];
		CColRefSet *pcrsPredUsed = CDrvdPropScalar::Pdpscalar(pexprPred->PdpDerive())->PcrsUsed();
		pexprPred->AddRef();
		if (0 == pcrsPredUsed->CElements() || pcrsOuter->FDisjoint(pcrsPredUsed))
		{
			pdrgpexprLocal->Append(pexprPred);
		}
		else
		{
			pdrgpexprOuterRefs->Append(pexprPred);
		}
	}
	pdrgpexpr->Release();

	*ppexprLocal = PexprConjunction(pmp, pdrgpexprLocal);
	*ppexprOuterRef = PexprConjunction(pmp, pdrgpexprOuterRefs);
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprAddCast
//
//	@doc:
// 		Add explicit casting to left child of given equality or INDF predicate
//		and return resulting casted expression;
//		the function returns NULL if operation failed
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprAddCast
	(
	IMemoryPool *pmp,
	CExpression *pexprPred
	)
{
	GPOS_ASSERT(NULL != pexprPred);
	GPOS_ASSERT(CUtils::FScalarCmp(pexprPred) || FINDF(pexprPred));

	CExpression *pexprChild = pexprPred;

	if (!CUtils::FScalarCmp(pexprPred))
	{
	    pexprChild = (*pexprPred)[0];
	}

	CExpression *pexprLeft = (*pexprChild)[0];
	CExpression *pexprRight = (*pexprChild)[1];

	IMDId *pmdidTypeLeft = CScalar::PopConvert(pexprLeft->Pop())->PmdidType();
	IMDId *pmdidTypeRight = CScalar::PopConvert(pexprRight->Pop())->PmdidType();

	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();

	CExpression *pexprNewPred = NULL;

	BOOL fTypesEqual = pmdidTypeLeft->FEquals(pmdidTypeRight);
	BOOL fCastLtoR = CMDAccessorUtils::FCastExists(pmda, pmdidTypeLeft, pmdidTypeRight);
	BOOL fCastRtoL = CMDAccessorUtils::FCastExists(pmda, pmdidTypeRight, pmdidTypeLeft);

	if (fTypesEqual || !(fCastLtoR || fCastRtoL))
	{
		return pexprNewPred;
	}

	pexprLeft->AddRef();
	pexprRight->AddRef();

	CExpression *pexprNewLeft = pexprLeft;
	CExpression *pexprNewRight = pexprRight;

	if (fCastLtoR)
	{
		pexprNewLeft = PexprCast(pmp, pmda, pexprLeft, pmdidTypeRight);
	}
	else
	{
		GPOS_ASSERT(fCastRtoL);
		pexprNewRight = PexprCast(pmp, pmda, pexprRight, pmdidTypeLeft);;
	}

	GPOS_ASSERT(NULL != pexprNewLeft && NULL != pexprNewRight);

	if (CUtils::FScalarCmp(pexprPred))
	{
		pexprNewPred = CUtils::PexprScalarCmp(pmp, pexprNewLeft, pexprNewRight, IMDType::EcmptEq);
	}
	else
	{
		pexprNewPred = CUtils::PexprINDF(pmp, pexprNewLeft, pexprNewRight);
	}

	return pexprNewPred;
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprCast
//
//	@doc:
// 		Add explicit casting on the input expression to the destination type
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprCast
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	CExpression *pexpr,
	IMDId *pmdidDest
	)
{
	IMDId *pmdidSrc = CScalar::PopConvert(pexpr->Pop())->PmdidType();
	const IMDCast *pmdcast = pmda->Pmdcast(pmdidSrc, pmdidDest);

	pmdidDest->AddRef();
	pmdcast->PmdidCastFunc()->AddRef();

	CScalarCast *popCast = GPOS_NEW(pmp) CScalarCast(pmp, pmdidDest, pmdcast->PmdidCastFunc(), pmdcast->FBinaryCoercible());
	return GPOS_NEW(pmp) CExpression(pmp, popCast, pexpr);
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PdrgpexprCastEquality
//
//	@doc:
// 		Add explicit casting to equality operations between compatible types
//
//---------------------------------------------------------------------------
DrgPexpr *
CPredicateUtils::PdrgpexprCastEquality
	(
	IMemoryPool *pmp,
	CExpression *pexpr
	)
{
	GPOS_ASSERT(pexpr->Pop()->FScalar());

	DrgPexpr *pdrgpexpr = PdrgpexprConjuncts(pmp, pexpr);
	DrgPexpr *pdrgpexprNew = GPOS_NEW(pmp) DrgPexpr(pmp);
	const ULONG ulPreds = pdrgpexpr->UlLength();
	for (ULONG ul = 0; ul < ulPreds; ul++)
	{
		CExpression *pexprPred = (*pdrgpexpr)[ul];
		pexprPred->AddRef();
		CExpression *pexprNewPred = pexprPred;

		if (FEquality(pexprPred) || FINDF(pexprPred))
		{
			CExpression *pexprCasted = PexprAddCast(pmp, pexprPred);
			if (NULL != pexprCasted)
			{
				// release predicate since we will construct a new one
				pexprNewPred->Release();
				pexprNewPred = pexprCasted;
			}
		}
		pdrgpexprNew->Append(pexprNewPred);
	}

	pdrgpexpr->Release();

	return pdrgpexprNew;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprInverseComparison
//
//	@doc:
// 		Convert predicates of the form (a Cmp b) into (a InvCmp b);
//		where InvCmp is the inverse comparison (e.g., '=' --> '<>')
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprInverseComparison
	(
	IMemoryPool *pmp,
	CExpression *pexprCmp
	)
{
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();

	IMDId *pmdidOp = CScalarCmp::PopConvert(pexprCmp->Pop())->PmdidOp();
	IMDId *pmdidInverseOp = pmda->Pmdscop(pmdidOp)->PmdidOpInverse();
	const CWStringConst *pstrFirst = pmda->Pmdscop(pmdidInverseOp)->Mdname().Pstr();

	// generate a predicate for the inversion of the comparison involved in the subquery
	(*pexprCmp)[0]->AddRef();
	(*pexprCmp)[1]->AddRef();
	pmdidInverseOp->AddRef();

	return CUtils::PexprScalarCmp(pmp, (*pexprCmp)[0], (*pexprCmp)[1], *pstrFirst, pmdidInverseOp);
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprPruneSuperfluosEquality
//
//	@doc:
// 		Convert predicates of the form (true = (a Cmp b)) into (a Cmp b);
//		do this operation recursively on deep expression tree
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprPruneSuperfluosEquality
	(
	IMemoryPool *pmp,
	CExpression *pexpr
	)
{
	GPOS_CHECK_STACK_SIZE;
	GPOS_ASSERT(pexpr->Pop()->FScalar());

	if (CUtils::FSubquery(pexpr->Pop()))
	{
		// cannot recurse below subquery
		pexpr->AddRef();
		return pexpr;
	}

	if (FEquality(pexpr))
	{
		BOOL fConstTrueLeftChild = CUtils::FScalarConstTrue((*pexpr)[0]);
		BOOL fConstTrueRightChild = CUtils::FScalarConstTrue((*pexpr)[1]);
		BOOL fConstFalseLeftChild = CUtils::FScalarConstFalse((*pexpr)[0]);
		BOOL fConstFalseRightChild = CUtils::FScalarConstFalse((*pexpr)[1]);

		BOOL fCmpLeftChild = CUtils::FScalarCmp((*pexpr)[0]);
		BOOL fCmpRightChild = CUtils::FScalarCmp((*pexpr)[1]);

		if (fCmpRightChild)
		{
			if (fConstTrueLeftChild)
			{
				return PexprPruneSuperfluosEquality(pmp, (*pexpr)[1]);
			}

			if (fConstFalseLeftChild)
			{
				CExpression *pexprInverse = PexprInverseComparison(pmp, (*pexpr)[1]);
				CExpression *pexprPruned =  PexprPruneSuperfluosEquality(pmp, pexprInverse);
				pexprInverse->Release();
				return pexprPruned;
			}
		}

		if (fCmpLeftChild)
		{
			if (fConstTrueRightChild)
			{
				return PexprPruneSuperfluosEquality(pmp, (*pexpr)[0]);
			}

			if (fConstFalseRightChild)
			{
				CExpression *pexprInverse = PexprInverseComparison(pmp, (*pexpr)[0]);
				CExpression *pexprPruned =  PexprPruneSuperfluosEquality(pmp, pexprInverse);
				pexprInverse->Release();

				return pexprPruned;
			}
		}
	}

	// process children
	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	const ULONG ulChildren = pexpr->UlArity();
	for (ULONG ul = 0; ul < ulChildren; ul++)
	{
		CExpression *pexprChild = PexprPruneSuperfluosEquality(pmp, (*pexpr)[ul]);
		pdrgpexpr->Append(pexprChild);
	}

	COperator *pop = pexpr->Pop();
	pop->AddRef();
	return GPOS_NEW(pmp) CExpression(pmp, pop, pdrgpexpr);
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FCheckPredicateImplication
//
//	@doc:
//		Determine if we should test predicate implication for stats
//		computation
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FCheckPredicateImplication
	(
	CExpression *pexprPred
	)
{
	// currently restrict testing implication to only equality of column references
	return COperator::EopScalarCmp == pexprPred->Pop()->Eopid() &&
		IMDType::EcmptEq == CScalarCmp::PopConvert(pexprPred->Pop())->Ecmpt() &&
		COperator::EopScalarIdent == (*pexprPred)[0]->Pop()->Eopid() &&
		COperator::EopScalarIdent == (*pexprPred)[1]->Pop()->Eopid();
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FImpliedPredicate
//
//	@doc:
//		Given a predicate and a list of equivalence classes,
//		return true if that predicate is implied by given equivalence
//		classes
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FImpliedPredicate
	(
	CExpression *pexprPred,
	DrgPcrs *pdrgpcrsEquivClasses
	)
{
	GPOS_ASSERT(pexprPred->Pop()->FScalar());
	GPOS_ASSERT(FCheckPredicateImplication(pexprPred));

	CColRefSet *pcrsUsed = CDrvdPropScalar::Pdpscalar(pexprPred->PdpDerive())->PcrsUsed();
	const ULONG ulSize = pdrgpcrsEquivClasses->UlLength();
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		CColRefSet *pcrs = (*pdrgpcrsEquivClasses)[ul];
		if (pcrs->FSubset(pcrsUsed))
		{
			// predicate is implied by given equivalence classes
			return true;
		}
	}

	return false;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprRemoveImpliedConjuncts
//
//	@doc:
//		Remove conjuncts that are implied based on child equivalence
//		classes,
//		the main use case is minimizing join/selection predicates to avoid
//		cardinality under-estimation,
//
//		for example, in the expression ((a=b) AND (a=c)), if a child
//		equivalence class is {b,c}, then we remove the conjunct (a=c)
//		since it can be implied from {b,c}, {a,b}
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprRemoveImpliedConjuncts
	(
	IMemoryPool *pmp,
	CExpression *pexprScalar,
	CExpressionHandle &exprhdl
	)
{
	// extract equivalence classes from logical children
	DrgPcrs *pdrgpcrs = CUtils::PdrgpcrsCopyChildEquivClasses(pmp, exprhdl);

	// extract all the conjuncts
	DrgPexpr *pdrgpexprConjuncts = PdrgpexprConjuncts(pmp, pexprScalar);
	const ULONG ulSize = pdrgpexprConjuncts->UlLength();
	DrgPexpr *pdrgpexprNewConjuncts = GPOS_NEW(pmp) DrgPexpr(pmp);
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		CExpression *pexprConj = (*pdrgpexprConjuncts)[ul];
		if (FCheckPredicateImplication(pexprConj) && FImpliedPredicate(pexprConj, pdrgpcrs))
		{
			// skip implied conjunct
			continue;
		}

		// add predicate to current equivalence classes
		DrgPcrs *pdrgpcrsConj = NULL;
		CConstraint *pcnstr = CConstraint::PcnstrFromScalarExpr(pmp, pexprConj, &pdrgpcrsConj);
		CRefCount::SafeRelease(pcnstr);
		if (NULL != pdrgpcrsConj)
		{
			DrgPcrs *pdrgpcrsMerged = CUtils::PdrgpcrsMergeEquivClasses(pmp, pdrgpcrs, pdrgpcrsConj);
			pdrgpcrs->Release();
			pdrgpcrsConj->Release();
			pdrgpcrs = pdrgpcrsMerged;
		}

		// add conjunct to new conjuncts array
		pexprConj->AddRef();
		pdrgpexprNewConjuncts->Append(pexprConj);
	}

	pdrgpexprConjuncts->Release();
	pdrgpcrs->Release();

	return PexprConjunction(pmp, pdrgpexprNewConjuncts);
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FValidSemiJoinCorrelations
//
//	@doc:
//		Check if given correlations are valid for (anti)semi-joins;
//		we disallow correlations referring to inner child, since inner
//		child columns are not visible above (anti)semi-join
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FValidSemiJoinCorrelations
	(
	IMemoryPool *pmp,
	CExpression *pexprOuter,
	CExpression *pexprInner,
	DrgPexpr *pdrgpexprCorrelations
	)
{
	GPOS_ASSERT(NULL != pexprOuter);
	GPOS_ASSERT(NULL != pexprInner);

	CColRefSet *pcrsOuterOuput = CDrvdPropRelational::Pdprel(pexprOuter->PdpDerive())->PcrsOutput();
	CColRefSet *pcrsInnerOuput = CDrvdPropRelational::Pdprel(pexprInner->PdpDerive())->PcrsOutput();

	// collect output columns of both children
	CColRefSet *pcrsChildren = GPOS_NEW(pmp) CColRefSet(pmp, *pcrsOuterOuput);
	pcrsChildren->Union(pcrsInnerOuput);

	const ULONG ulCorrs = pdrgpexprCorrelations->UlLength();
	BOOL fValid = true;
	for (ULONG ul = 0; fValid && ul < ulCorrs; ul++)
	{
		CExpression *pexprPred = (*pdrgpexprCorrelations)[ul];
		CColRefSet *pcrsUsed = CDrvdPropScalar::Pdpscalar(pexprPred->PdpDerive())->PcrsUsed();
		if (0 < pcrsUsed->CElements() && !pcrsChildren->FSubset(pcrsUsed) && !pcrsUsed->FDisjoint(pcrsInnerOuput))
		{
			// disallow correlations referring to inner child
			fValid = false;
		}
	}
	pcrsChildren->Release();

	return fValid;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FSimpleEqualityUsingCols
//
//	@doc:
//		Check if given expression is (a conjunction of) simple column
//		equality that use columns from the given column set
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FSimpleEqualityUsingCols
	(
	IMemoryPool *pmp,
	CExpression *pexprScalar,
	CColRefSet *pcrs
	)
{
	GPOS_ASSERT(NULL != pexprScalar);
	GPOS_ASSERT(pexprScalar->Pop()->FScalar());
	GPOS_ASSERT(NULL != pcrs);
	GPOS_ASSERT(0 < pcrs->CElements());

	// break expression into conjuncts
	DrgPexpr *pdrgpexpr = PdrgpexprConjuncts(pmp, pexprScalar);
	const ULONG ulSize = pdrgpexpr->UlLength();
	BOOL fSuccess = true;
	for (ULONG ul = 0; fSuccess && ul < ulSize; ul++)
	{
		// join predicate must be an equality of scalar idents and uses columns from given set
		CExpression *pexprConj = (*pdrgpexpr)[ul];
		CColRefSet *pcrsUsed = CDrvdPropScalar::Pdpscalar(pexprConj->PdpDerive())->PcrsUsed();
		fSuccess = FEquality(pexprConj) &&
				CUtils::FScalarIdent((*pexprConj)[0]) &&
				CUtils::FScalarIdent((*pexprConj)[1]) &&
				!pcrs->FDisjoint(pcrsUsed);
	}
	pdrgpexpr->Release();

	return fSuccess;
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::PexprReplaceColsWithNulls
//
//	@doc:
//		For all columns in the given expression and are members
//		of the given column set, replace columns with NULLs
//
//---------------------------------------------------------------------------
CExpression *
CPredicateUtils::PexprReplaceColsWithNulls
	(
	IMemoryPool *pmp,
	CExpression *pexprScalar,
	CColRefSet *pcrs
	)
{
	GPOS_CHECK_STACK_SIZE;
	GPOS_ASSERT(NULL != pexprScalar);

	COperator *pop = pexprScalar->Pop();
	GPOS_ASSERT(pop->FScalar());

	if (CUtils::FSubquery(pop))
	{
		// do not recurse into subqueries
		pexprScalar->AddRef();
		return pexprScalar;
	}

	if (COperator::EopScalarIdent == pop->Eopid() &&
		pcrs->FMember(CScalarIdent::PopConvert(pop)->Pcr()))
	{
		// replace column with NULL constant
		return CUtils::PexprScalarConstBool(pmp, false /*fVal*/, true /*fNull*/);
	}

	// process children recursively
	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	const ULONG ulChildren = pexprScalar->UlArity();
	for (ULONG ul = 0; ul < ulChildren; ul++)
	{
		CExpression *pexprChild = PexprReplaceColsWithNulls(pmp, (*pexprScalar)[ul], pcrs);
		pdrgpexpr->Append(pexprChild);
	}

	pop->AddRef();
	return GPOS_NEW(pmp) CExpression(pmp, pop, pdrgpexpr);
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FNullRejecting
//
//	@doc:
//		Check if scalar expression evaluates to (NOT TRUE) when
//		all columns in the given set that are included in the expression
//		are set to NULL
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FNullRejecting
	(
	IMemoryPool *pmp,
	CExpression *pexprScalar,
	CColRefSet *pcrs
	)
{
	GPOS_CHECK_STACK_SIZE;
	GPOS_ASSERT(NULL != pexprScalar);
	GPOS_ASSERT(pexprScalar->Pop()->FScalar());

	CDrvdPropScalar *pdpscalar = CDrvdPropScalar::Pdpscalar(pexprScalar->PdpDerive());
	BOOL fHasVolatileFunctions = (IMDFunction::EfsVolatile == pdpscalar->Pfp()->Efs());
	BOOL fHasSQL = (IMDFunction::EfdaNoSQL != pdpscalar->Pfp()->Efda());

	if (fHasVolatileFunctions ||
		fHasSQL ||
		pdpscalar->FHasNonScalarFunction())
	{
		// scalar expression must not have volatile functions, functions with SQL, subquery or non-scalar functions
		return false;
	}

	// create another expression copy where we replace columns included in the set with NULL values
	CExpression *pexprColsReplacedWithNulls = PexprReplaceColsWithNulls(pmp, pexprScalar, pcrs);

	// evaluate the resulting expression
	CScalar::EBoolEvalResult eber = CScalar::EberEvaluate(pmp, pexprColsReplacedWithNulls);
	pexprColsReplacedWithNulls->Release();

	// return TRUE if expression evaluation  result is (NOT TRUE), which means we need to
	// check if result is NULL or result is False,
	// in these two cases, a predicate will filter out incoming tuple, which means it is Null-Rejecting
	return (CScalar::EberNull == eber || CScalar::EberFalse == eber);
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FNotIdent
//
//	@doc:
// 		Returns true iff the given expression is a Not operator whose child is an
//		identifier
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FNotIdent
	(
	CExpression *pexpr
	)
{
	return FNot(pexpr) && COperator::EopScalarIdent == (*pexpr)[0]->Pop()->Eopid();
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FCompatiblePredicates
//
//	@doc:
//		Returns true iff all predicates in the given array are compatible with
//		the given index.
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FCompatiblePredicates
	(
	DrgPexpr *pdrgpexprPred,
	const IMDIndex *pmdindex,
	DrgPcr *pdrgpcrIndex,
	CMDAccessor *pmda
	)
{
	GPOS_ASSERT(NULL != pdrgpexprPred);
	GPOS_ASSERT(NULL != pmdindex);

	const ULONG ulNumPreds = pdrgpexprPred->UlLength();
	for (ULONG ul = 0; ul < ulNumPreds; ul++)
	{
		if (!FCompatibleIndexPredicate((*pdrgpexprPred)[ul], pmdindex, pdrgpcrIndex, pmda))
		{
			return false;
		}
	}

	return true;
}

//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FCompatibleIndexPredicates
//
//	@doc:
//		Returns true iff the given predicate 'pexprPred' is compatible with the
//		given index 'pmdindex'.
//
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FCompatibleIndexPredicate
	(
	CExpression *pexprPred,
	const IMDIndex *pmdindex,
	DrgPcr *pdrgpcrIndex,
	CMDAccessor *pmda
	)
{
	GPOS_ASSERT(NULL != pexprPred);
	GPOS_ASSERT(NULL != pmdindex);

	if (COperator::EopScalarCmp != pexprPred->Pop()->Eopid())
	{
		return false;
	}
	CScalarCmp *popScCmp = CScalarCmp::PopConvert(pexprPred->Pop());
	const IMDScalarOp *pmdobjScCmp = pmda->Pmdscop(popScCmp->PmdidOp());

	CExpression *pexprLeft = (*pexprPred)[0];
	CColRefSet *pcrsUsed = CDrvdPropScalar::Pdpscalar(pexprLeft->PdpDerive())->PcrsUsed();
	GPOS_ASSERT(1 == pcrsUsed->CElements());

	CColRef *pcrIndexKey = pcrsUsed->PcrFirst();
	ULONG ulKeyPos = pdrgpcrIndex->UlPos(pcrIndexKey);
	GPOS_ASSERT(ULONG_MAX != ulKeyPos);

	return (pmdindex->FCompatible(pmdobjScCmp, ulKeyPos));
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FConvertToCNF
//
//	@doc:
//		Check if the expensive CNF conversion is beneficial in finding
//		predicate for hash join
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FConvertToCNF
	(
	CExpression *pexprOuter,
	CExpression *pexprInner,
	CExpression *pexprScalar
	)
{
	GPOS_ASSERT(NULL != pexprScalar);

	if (FComparison(pexprScalar))
	{
		return CPhysicalJoin::FHashJoinCompatible(pexprScalar, pexprOuter, pexprInner);
	}

	BOOL fOr = FOr(pexprScalar);
	BOOL fAllChidrenDoCNF = true;
	BOOL fExistsChildDoCNF = false;

	// recursively check children
	const ULONG ulArity = pexprScalar->UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		BOOL fCNFConversion = FConvertToCNF(pexprOuter, pexprInner, (*pexprScalar)[ul]);

		if (fCNFConversion)
		{
			fExistsChildDoCNF = true;
		}
		else
		{
			fAllChidrenDoCNF = false;
		}
	}

	if (fOr)
	{
		// an OR predicate can only be beneficial is all of it
		// children state that there is a benefit for CNF conversion
		// eg: ((a1 > b1) AND (a2 > b2)) OR ((a3 == b3) AND (a4 == b4))
		// one of the OR children has no equality condition and thus when
		// we convert the expression into CNF none of then will be useful to
		// for hash join

		return fAllChidrenDoCNF;
	}
	else
	{
		// at least one one child states that CNF conversion is beneficial

		return fExistsChildDoCNF;
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::CollectGrandChildrenUnionUnionAll
//
//	@doc:
// 		If the nth child of the given union/union all expression is also a
// 		union / union all expression, then collect the latter's children and
//		set the input columns of the new n-ary union/unionall operator
//---------------------------------------------------------------------------
void
CPredicateUtils::CollectGrandChildrenUnionUnionAll
	(
	IMemoryPool *pmp,
	CExpression *pexpr,
	ULONG ulChildIndex,
	DrgPexpr *pdrgpexprResult,
	DrgDrgPcr *pdrgdrgpcrResult
	)
{
	GPOS_ASSERT(NULL != pexpr);
	GPOS_ASSERT(ulChildIndex < pexpr->UlArity());
	GPOS_ASSERT(NULL != pdrgpexprResult);
	GPOS_ASSERT(NULL != pdrgdrgpcrResult);
	GPOS_ASSERT(CPredicateUtils::FCollapsibleChildUnionUnionAll(pexpr, ulChildIndex));


	CExpression *pexprChild = (*pexpr)[ulChildIndex];
	GPOS_ASSERT(NULL != pexprChild);

	CLogicalSetOp *pop = CLogicalSetOp::PopConvert(pexpr->Pop());
	CLogicalSetOp *popChild = CLogicalSetOp::PopConvert(pexprChild->Pop());

	// the parent setop's expected input columns and the child setop's output columns
	// may have different size or order or both. We need to ensure that the new
	// n-ary setop has the right order of the input columns from its grand children
	DrgDrgPcr *pdrgpdrgpcrInput = pop->PdrgpdrgpcrInput();
	DrgPcr *pdrgpcrInputExpected = (*pdrgpdrgpcrInput)[ulChildIndex];

	const ULONG ulCols = pdrgpcrInputExpected->UlLength();

	DrgPcr *pdrgpcrOuputChild = popChild->PdrgpcrOutput();
	GPOS_ASSERT(ulCols <= pdrgpcrOuputChild->UlLength());

	DrgPul *pdrgpul = GPOS_NEW(pmp) DrgPul (pmp);
	for (ULONG ulColIdx = 0; ulColIdx < ulCols; ulColIdx++)
	{
		const CColRef *pcr = (*pdrgpcrInputExpected)[ulColIdx];
		ULONG ulPos = pdrgpcrOuputChild->UlPos(pcr);
		GPOS_ASSERT(ULONG_MAX != ulPos);
		pdrgpul->Append(GPOS_NEW(pmp) ULONG(ulPos));
	}

	DrgDrgPcr *pdrgdrgpcrChild = popChild->PdrgpdrgpcrInput();
	const ULONG ulArityChild = pexprChild->UlArity();
	GPOS_ASSERT(pdrgdrgpcrChild->UlLength() == ulArityChild);

	for (ULONG ul = 0; ul < ulArityChild; ul++)
	{
		// collect the grand child expression
		CExpression *pexprGrandchild = (*pexprChild) [ul];
		GPOS_ASSERT(pexprGrandchild->Pop()->FLogical());

		pexprGrandchild->AddRef();
		pdrgpexprResult->Append(pexprGrandchild);

		// collect the correct input columns
		DrgPcr *pdrgpcrOld = (*pdrgdrgpcrChild)[ul];
		DrgPcr *pdrgpcrNew = GPOS_NEW(pmp) DrgPcr (pmp);
		for (ULONG ulColIdx = 0; ulColIdx < ulCols; ulColIdx++)
		{
			ULONG ulPos = *(*pdrgpul)[ulColIdx];
			CColRef *pcr = (*pdrgpcrOld)[ulPos];
			pdrgpcrNew->Append(pcr);
		}

		pdrgdrgpcrResult->Append(pdrgpcrNew);
	}

	pdrgpul->Release();
}


//---------------------------------------------------------------------------
//	@function:
//		CPredicateUtils::FCollapsibleChildUnionUnionAll
//
//	@doc:
//		Check if we can collapse the nth child of the given union / union all operator
//---------------------------------------------------------------------------
BOOL
CPredicateUtils::FCollapsibleChildUnionUnionAll
	(
	CExpression *pexpr,
	ULONG ulChildIndex
	)
{
	GPOS_ASSERT(NULL != pexpr);

	if (!CPredicateUtils::FUnionOrUnionAll(pexpr))
	{
		return false;
	}

	CExpression *pexprChild = (*pexpr)[ulChildIndex];
	GPOS_ASSERT(NULL != pexprChild);

	// we can only collapse when the parent and child operator are of the same kind
	return (pexprChild->Pop()->Eopid() == pexpr->Pop()->Eopid());
}


// EOF
