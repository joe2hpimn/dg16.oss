//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CXformSubqueryUnnest.cpp
//
//	@doc:
//		Implementation of subquery unnesting base class
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
#include "gpopt/operators/COperator.h"
#include "gpopt/xforms/CXformSubqueryUnnest.h"
#include "gpopt/xforms/CSubqueryHandler.h"


using namespace gpmd;
using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CXformSubqueryUnnest::Exfp
//
//	@doc:
//		Compute xform promise for a given expression handle;
//		scalar child must have subquery
//
//---------------------------------------------------------------------------
CXform::EXformPromise
CXformSubqueryUnnest::Exfp
	(
	CExpressionHandle &exprhdl
	)
	const
{
	if (exprhdl.Pdpscalar(1)->FHasSubquery())
	{
		return CXform::ExfpHigh;
	}

	return CXform::ExfpNone;
}


//---------------------------------------------------------------------------
//	@function:
//		CXformSubqueryUnnest::PexprSubqueryUnnest
//
//	@doc:
//		Helper for unnesting subquery under a given context
//
//---------------------------------------------------------------------------
CExpression *
CXformSubqueryUnnest::PexprSubqueryUnnest
	(
	IMemoryPool *pmp,
	CExpression *pexpr,
	BOOL fEnforceCorrelatedApply
	)
{
	GPOS_ASSERT(NULL != pexpr);

	if (GPOS_FTRACE(EopttraceEnforceCorrelatedExecution) && !fEnforceCorrelatedApply)
	{
		// if correlated execution is enforced, we cannot generate an expression
		// that does not use correlated Apply
		return NULL;
	}

	// extract components
	CExpression *pexprOuter = (*pexpr)[0];
	CExpression *pexprScalar = (*pexpr)[1];

	// we add-ref the logical child since the resulting expression must re-use it
	pexprOuter->AddRef();

	CExpression *pexprNewOuter = NULL;
	CExpression *pexprResidualScalar = NULL;

	CSubqueryHandler::ESubqueryCtxt esqctxt = CSubqueryHandler::EsqctxtFilter;
	if (COperator::EopScalarProjectList == pexprScalar->Pop()->Eopid())
	{
		esqctxt = CSubqueryHandler::EsqctxtValue;
	}

	// calling the handler removes subqueries and sets new logical and scalar expressions
	CSubqueryHandler sh(pmp, fEnforceCorrelatedApply);
	if (!CSubqueryHandler::FProcess
			(
			sh,
			pexprOuter,
			pexprScalar,
			false /* fDisjunctionOrNegation */,
			esqctxt,
			&pexprNewOuter,
			&pexprResidualScalar
			)
		)
	{
		CRefCount::SafeRelease(pexprNewOuter);
		CRefCount::SafeRelease(pexprResidualScalar);

		return NULL;
	}

	// create a new alternative using the new logical and scalar expressions
	CExpression *pexprResult = NULL;
	if (CSubqueryHandler::EsqctxtFilter == esqctxt)
	{
		pexprResult = CUtils::PexprLogicalSelect(pmp, pexprNewOuter, pexprResidualScalar);
	}
	else
	{
		GPOS_ASSERT(CSubqueryHandler::EsqctxtValue == esqctxt);

		CLogicalSequenceProject *popSeqPrj = NULL;
		CLogicalGbAgg *popGbAgg = NULL;
		COperator::EOperatorId eopid = pexpr->Pop()->Eopid();

		switch (eopid)
		{
			case COperator::EopLogicalProject:
				pexprResult = CUtils::PexprLogicalProject(pmp, pexprNewOuter, pexprResidualScalar, false /*fNewComputedCol*/);
				break;

			case COperator::EopLogicalGbAgg:
				popGbAgg = CLogicalGbAgg::PopConvert(pexpr->Pop());
				popGbAgg->Pdrgpcr()->AddRef();
				pexprResult = CUtils::PexprLogicalGbAgg(pmp, popGbAgg->Pdrgpcr(), pexprNewOuter, pexprResidualScalar, popGbAgg->Egbaggtype());
				break;

			case COperator::EopLogicalSequenceProject:
				popSeqPrj = CLogicalSequenceProject::PopConvert(pexpr->Pop());
				popSeqPrj->Pds()->AddRef();
				popSeqPrj->Pdrgpos()->AddRef();
				popSeqPrj->Pdrgpwf()->AddRef();
				pexprResult =
					CUtils::PexprLogicalSequenceProject(pmp, popSeqPrj->Pds(), popSeqPrj->Pdrgpos(), popSeqPrj->Pdrgpwf(), pexprNewOuter, pexprResidualScalar);
				break;

			default:
				GPOS_ASSERT(!"Unnesting subqueries for an invalid operator");
				break;
		}
	}

	// normalize resulting expression
	CExpression *pexprNormalized = CNormalizer::PexprNormalize(pmp, pexprResult);
	pexprResult->Release();

	// pull up projections
	CExpression *pexprPullUpProjections = CNormalizer::PexprPullUpProjections(pmp, pexprNormalized);
	pexprNormalized->Release();

	return pexprPullUpProjections;
}

//---------------------------------------------------------------------------
//	@function:
//		CXformSubqueryUnnest::Transform
//
//	@doc:
//		Actual transformation;
//		assumes unary operator, e.g., Select/Project, where the scalar
//		child contains subquery
//
//---------------------------------------------------------------------------
void
CXformSubqueryUnnest::Transform
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

	// first, generate the alternative where a regular (uncorrelated) Apply
	// is generated whenever possible
	CExpression *pexprAvoidCorrelatedApply = PexprSubqueryUnnest(pmp, pexpr, false /*fEnforceCorrelatedApply*/);
	if (NULL != pexprAvoidCorrelatedApply)
	{
		// add alternative to results
		pxfres->Add(pexprAvoidCorrelatedApply);
	}

	// second, generate the alternative where a correlated Apply always enforced
	CExpression *pexprCorrelatedApply = PexprSubqueryUnnest(pmp, pexpr, true /*fEnforceCorrelatedApply*/);
	if (NULL != pexprCorrelatedApply)
	{
		// add alternative to results
		pxfres->Add(pexprCorrelatedApply);
	}
}

// EOF
