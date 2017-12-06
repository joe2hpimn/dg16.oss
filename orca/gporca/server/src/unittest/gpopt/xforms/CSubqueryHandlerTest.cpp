//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CSubqueryHandlerTest.cpp
//
//	@doc:
//		Test for subquery handling
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#include "gpos/io/COstreamString.h"
#include "gpos/string/CWStringDynamic.h"

#include "gpopt/base/CUtils.h"
#include "gpopt/base/CQueryContext.h"
#include "gpopt/eval/CConstExprEvaluatorDefault.h"
#include "gpopt/operators/CPredicateUtils.h"
#include "gpopt/operators/ops.h"

#include "gpopt/xforms/CXformFactory.h"
#include "gpopt/xforms/CSubqueryHandler.h"

#include "unittest/base.h"
#include "unittest/gpopt/xforms/CSubqueryHandlerTest.h"
#include "unittest/gpopt/CSubqueryTestUtils.h"

#include "naucrates/md/CMDIdGPDB.h"

//---------------------------------------------------------------------------
//	@function:
//		CSubqueryHandlerTest::EresUnittest
//
//	@doc:
//		Unittest for predicate utilities
//
//---------------------------------------------------------------------------
GPOS_RESULT
CSubqueryHandlerTest::EresUnittest()
{

	CUnittest rgut[] =
		{
		GPOS_UNITTEST_FUNC(CSubqueryHandlerTest::EresUnittest_Subquery2Apply),
		GPOS_UNITTEST_FUNC(CSubqueryHandlerTest::EresUnittest_SubqueryWithDisjunction),
#ifdef GPOS_DEBUG
		GPOS_UNITTEST_FUNC_ASSERT(CSubqueryHandlerTest::EresUnittest_SubqueryWithConstSubqueries),
#endif // GPOS_DEBUG
		};

	return CUnittest::EresExecute(rgut, GPOS_ARRAY_SIZE(rgut));
}


//---------------------------------------------------------------------------
//	@function:
//		CSubqueryHandlerTest::EresUnittest_Subquery2Apply
//
//	@doc:
//		Test of subquery handler
//
//---------------------------------------------------------------------------
GPOS_RESULT
CSubqueryHandlerTest::EresUnittest_Subquery2Apply()
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	// setup a file-based provider
	CMDProviderMemory *pmdp = CTestUtils::m_pmdpf;
	pmdp->AddRef();
	CMDAccessor mda(pmp, CMDCache::Pcache(), CTestUtils::m_sysidDefault, pmdp);
	
	typedef CExpression *(*Pfpexpr)(IMemoryPool*, BOOL);
	Pfpexpr rgpf[] =
		{
		CSubqueryTestUtils::PexprSelectWithAggSubquery,
		CSubqueryTestUtils::PexprSelectWithAggSubqueryConstComparison,
		CSubqueryTestUtils::PexprProjectWithAggSubquery,
		CSubqueryTestUtils::PexprSelectWithAnySubquery,
		CSubqueryTestUtils::PexprProjectWithAnySubquery,
		CSubqueryTestUtils::PexprSelectWithAllSubquery,
		CSubqueryTestUtils::PexprProjectWithAllSubquery,
		CSubqueryTestUtils::PexprSelectWithExistsSubquery,
		CSubqueryTestUtils::PexprProjectWithExistsSubquery,
		CSubqueryTestUtils::PexprSelectWithNotExistsSubquery,
		CSubqueryTestUtils::PexprProjectWithNotExistsSubquery,
		CSubqueryTestUtils::PexprSelectWithNestedCmpSubquery,
		CSubqueryTestUtils::PexprSelectWithCmpSubqueries,
		CSubqueryTestUtils::PexprSelectWithSubqueryConjuncts,
		CSubqueryTestUtils::PexprProjectWithSubqueries,
		CSubqueryTestUtils::PexprSelectWith2LevelsCorrSubquery,
		CSubqueryTestUtils::PexprJoinWithAggSubquery,
		CSubqueryTestUtils::PexprSelectWithAggSubqueryOverJoin,
		CSubqueryTestUtils::PexprSelectWithNestedSubquery,
		CSubqueryTestUtils::PexprSubqueriesInNullTestContext,
		CSubqueryTestUtils::PexprSubqueriesInDifferentContexts,
		CSubqueryTestUtils::PexprSelectWithSubqueryDisjuncts,
		CSubqueryTestUtils::PexprSelectWithNestedAnySubqueries,
		CSubqueryTestUtils::PexprSelectWithNestedAllSubqueries,
		CSubqueryTestUtils::PexprUndecorrelatableAnySubquery,
		CSubqueryTestUtils::PexprUndecorrelatableAllSubquery,
		CSubqueryTestUtils::PexprUndecorrelatableExistsSubquery,
		CSubqueryTestUtils::PexprUndecorrelatableNotExistsSubquery,
		CSubqueryTestUtils::PexprUndecorrelatableScalarSubquery,
		};

	// xforms to test
	CXformSet *pxfs = GPOS_NEW(pmp) CXformSet(pmp);
	(void) pxfs->FExchangeSet(CXform::ExfSubqJoin2Apply);
	(void) pxfs->FExchangeSet(CXform::ExfSelect2Apply);
	(void) pxfs->FExchangeSet(CXform::ExfProject2Apply);

	BOOL fCorrelated = true;
	// we generate two expressions using each generator
	const ULONG ulSize = 2 * GPOS_ARRAY_SIZE(rgpf);
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		ULONG ulIndex = ul / 2;
		// install opt context in TLS
		CAutoOptCtxt aoc
					(
					pmp,
					&mda,
					NULL,  /* pceeval */
					CTestUtils::Pcm(pmp)
					);

		// generate expression
		CExpression *pexpr = rgpf[ulIndex](pmp, fCorrelated);
		
		// check for subq xforms
		CXformSet *pxfsCand = CLogical::PopConvert(pexpr->Pop())->PxfsCandidates(pmp);
		pxfsCand->Intersection(pxfs);
		
		CXformSetIter xsi(*pxfsCand);
		while (xsi.FAdvance())
		{			
			CXform *pxform = CXformFactory::Pxff()->Pxf(xsi.TBit());
			GPOS_ASSERT(NULL != pxform);

			CWStringDynamic str(pmp);
			COstreamString oss(&str);

			oss	<< std::endl << "INPUT:" << std::endl << *pexpr << std::endl;

			CXformContext *pxfctxt = GPOS_NEW(pmp) CXformContext(pmp);
			CXformResult *pxfres = GPOS_NEW(pmp) CXformResult(pmp);

			// calling the xform to perform subquery to Apply transformation
			pxform->Transform(pxfctxt, pxfres, pexpr);
			CExpression *pexprResult = pxfres->PexprNext();

			oss	<< std::endl << "OUTPUT:" << std::endl;
			if (NULL != pexprResult)
			{
				oss << *pexprResult << std::endl;
			}
			else
			{
				oss << "\tNo subquery unnesting output" << std::endl;
			}

			GPOS_TRACE(str.Wsz());
			str.Reset();

			pxfres->Release();
			pxfctxt->Release();
		}
		
		pxfsCand->Release();
		pexpr->Release();
		fCorrelated = !fCorrelated;
	}

	pxfs->Release();

	return GPOS_OK;
}

//---------------------------------------------------------------------------
//	@function:
//		CSubqueryHandlerTest::EresUnittest_SubqueryWithConstSubqueries
//
//	@doc:
//		Test of subquery handler for ALL subquery over const table get
//
//---------------------------------------------------------------------------
GPOS_RESULT
CSubqueryHandlerTest::EresUnittest_SubqueryWithConstSubqueries()
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	// setup a file-based provider
	CMDProviderMemory *pmdp = CTestUtils::m_pmdpf;
	pmdp->AddRef();
	
	// we need to use an auto pointer for the cache here to ensure
	// deleting memory of cached objects when we throw
	CAutoP<CMDAccessor::MDCache> apcache;
	apcache = CCacheFactory::PCacheCreate<gpopt::IMDCacheObject*, gpopt::CMDKey*>
					(
					true, // fUnique
					0 /* unlimited cache quota */,
					CMDKey::UlHashMDKey,
					CMDKey::FEqualMDKey
					);

	CMDAccessor::MDCache *pcache = apcache.Pt();

	{
		CMDAccessor mda(pmp, pcache, CTestUtils::m_sysidDefault, pmdp);

		// install opt context in TLS
		CAutoOptCtxt aoc
						(
						pmp,
						&mda,
						NULL,  /* pceeval */
						CTestUtils::Pcm(pmp)
						);

		// create a subquery with const table get expression
		CExpression *pexpr = CSubqueryTestUtils::PexprSubqueryWithDisjunction(pmp);
		CXform *pxform = CXformFactory::Pxff()->Pxf(CXform::ExfSelect2Apply);

		CWStringDynamic str(pmp);
		COstreamString oss(&str);

		oss	<< std::endl << "EXPRESSION:" << std::endl << *pexpr << std::endl;

		CExpression *pexprLogical = (*pexpr)[0];
		CExpression *pexprScalar = (*pexpr)[1];
		oss	<< std::endl << "LOGICAL:" << std::endl << *pexprLogical << std::endl;
		oss	<< std::endl << "SCALAR:" << std::endl << *pexprScalar << std::endl;

		GPOS_TRACE(str.Wsz());
		str.Reset();

		CXformContext *pxfctxt = GPOS_NEW(pmp) CXformContext(pmp);
		CXformResult *pxfres = GPOS_NEW(pmp) CXformResult(pmp);

		// calling the xform to perform subquery to Apply transformation;
		// xform must fail since we do not expect constant subqueries
		pxform->Transform(pxfctxt, pxfres, pexpr);
		CExpression *pexprResult = pxfres->PexprNext();
		
		oss	<< std::endl << "NEW LOGICAL:" << std::endl << *((*pexprResult)[0]) << std::endl;
		oss	<< std::endl << "RESIDUAL SCALAR:" << std::endl << *((*pexprResult)[1]) << std::endl;

		GPOS_TRACE(str.Wsz());
		str.Reset();

		pxfres->Release();
		pxfctxt->Release();
		pexpr->Release();
	
	}

	return GPOS_FAILED;
}

//---------------------------------------------------------------------------
//	@function:
//		CSubqueryHandlerTest::EresUnittest_SubqueryWithDisjunction
//
//	@doc:
//		Test case of subqueries in a disjunctive tree
//
//---------------------------------------------------------------------------
GPOS_RESULT
CSubqueryHandlerTest::EresUnittest_SubqueryWithDisjunction()
{
	// use own memory pool
	CAutoMemoryPool amp(CAutoMemoryPool::ElcNone);
	IMemoryPool *pmp = amp.Pmp();

	// setup a file-based provider
	CMDProviderMemory *pmdp = CTestUtils::m_pmdpf;
	pmdp->AddRef();
	CMDAccessor mda(pmp, CMDCache::Pcache());
	mda.RegisterProvider(CTestUtils::m_sysidDefault, pmdp);
	
	// install opt context in TLS
	CAutoOptCtxt aoc
					(
					pmp,
					&mda,
					NULL,  /* pceeval */
					CTestUtils::Pcm(pmp)
					);
		
	// create a subquery with const table get expression

	CExpression *pexprOuter = NULL;
	CExpression *pexprInner = NULL;
	CSubqueryTestUtils::GenerateGetExpressions(pmp, &pexprOuter, &pexprInner);

	CExpression *pexpr = CSubqueryTestUtils::PexprSelectWithSubqueryBoolOp(pmp, pexprOuter, pexprInner, true /*fCorrelated*/, CScalarBoolOp::EboolopOr);
	
	CXform *pxform = CXformFactory::Pxff()->Pxf(CXform::ExfSelect2Apply);

	CWStringDynamic str(pmp);
	COstreamString oss(&str);

	oss	<< std::endl << "EXPRESSION:" << std::endl << *pexpr << std::endl;

	CExpression *pexprLogical = (*pexpr)[0];
	CExpression *pexprScalar = (*pexpr)[1];
	oss	<< std::endl << "LOGICAL:" << std::endl << *pexprLogical << std::endl;
	oss	<< std::endl << "SCALAR:" << std::endl << *pexprScalar << std::endl;

	GPOS_TRACE(str.Wsz());
	str.Reset();

	CXformContext *pxfctxt = GPOS_NEW(pmp) CXformContext(pmp);
	CXformResult *pxfres = GPOS_NEW(pmp) CXformResult(pmp);

	// calling the xform to perform subquery to Apply transformation
	pxform->Transform(pxfctxt, pxfres, pexpr);
	CExpression *pexprResult = pxfres->PexprNext();
	
	oss	<< std::endl << "NEW LOGICAL:" << std::endl << *((*pexprResult)[0]) << std::endl;
	oss	<< std::endl << "RESIDUAL SCALAR:" << std::endl << *((*pexprResult)[1]) << std::endl;

	GPOS_TRACE(str.Wsz());
	str.Reset();

	pxfres->Release();
	pxfctxt->Release();
	pexpr->Release();

	return GPOS_OK;
}


// EOF
