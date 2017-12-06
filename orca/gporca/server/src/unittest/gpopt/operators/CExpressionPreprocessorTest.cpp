//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CExpressionPreprocessorTest.cpp
//
//	@doc:
//		Test for expression preprocessing
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#define ALLOW_strstr

#include <string.h>

#include "gpos/io/COstreamString.h"
#include "gpos/string/CWStringDynamic.h"

#include "gpopt/base/CUtils.h"
#include "gpopt/eval/CConstExprEvaluatorDefault.h"
#include "gpopt/mdcache/CAutoMDAccessor.h"
#include "gpopt/operators/CPredicateUtils.h"
#include "gpopt/operators/CScalarProjectElement.h"
#include "gpopt/operators/CLogicalNAryJoin.h"
#include "gpopt/operators/CLogicalInnerJoin.h"
#include "gpopt/operators/CLogicalLeftOuterJoin.h"
#include "gpopt/operators/CExpressionUtils.h"
#include "gpopt/xforms/CXformUtils.h"

#include "unittest/base.h"
#include "unittest/gpopt/operators/CExpressionPreprocessorTest.h"
#include "unittest/gpopt/CTestUtils.h"

#include "naucrates/md/CMDProviderMemory.h"
#include "naucrates/md/CMDIdGPDB.h"

using namespace gpopt;
using namespace gpmd;

//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresUnittest
//
//	@doc:
//		Unittest for predicate utilities
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresUnittest()
{

	CUnittest rgut[] =
		{
		GPOS_UNITTEST_FUNC(EresUnittest_UnnestSubqueries),
		GPOS_UNITTEST_FUNC(EresUnittest_PreProcess),
		GPOS_UNITTEST_FUNC(EresUnittest_PreProcessWindowFunc),
		GPOS_UNITTEST_FUNC(EresUnittest_InferPredsOnLOJ),
		GPOS_UNITTEST_FUNC(EresUnittest_PreProcessWindowFuncWithLOJ),
		GPOS_UNITTEST_FUNC(EresUnittest_PreProcessWindowFuncWithOuterRefs),
		GPOS_UNITTEST_FUNC(EresUnittest_PreProcessWindowFuncWithDistinctAggs),
		GPOS_UNITTEST_FUNC(EresUnittest_PreProcessNestedScalarSubqueries),
		GPOS_UNITTEST_FUNC(EresUnittest_PreProcessOuterJoin),
		GPOS_UNITTEST_FUNC(EresUnittest_PreProcessOuterJoinMinidumps),
		GPOS_UNITTEST_FUNC(EresUnittest_PreProcessOrPrefilters),
		GPOS_UNITTEST_FUNC(EresUnittest_PreProcessOrPrefiltersPartialPush),
		GPOS_UNITTEST_FUNC(EresUnittest_CollapseInnerJoin)
		};

	return CUnittest::EresExecute(rgut, GPOS_ARRAY_SIZE(rgut));
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::FHasSubqueryAll
//
//	@doc:
//		Check if a given expression has an ALL subquery
//
//---------------------------------------------------------------------------
BOOL
CExpressionPreprocessorTest::FHasSubqueryAll
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	COperator::EOperatorId rgeopid[] =
	{
		COperator::EopScalarSubqueryAll,
	};

	return CUtils::FHasOp(pexpr, rgeopid, GPOS_ARRAY_SIZE(rgeopid));
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::FHasSubqueryAny
//
//	@doc:
//		Check if a given expression has an ANY subquery
//
//---------------------------------------------------------------------------
BOOL
CExpressionPreprocessorTest::FHasSubqueryAny
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	COperator::EOperatorId rgeopid[] =
	{
		COperator::EopScalarSubqueryAny,
	};

	return CUtils::FHasOp(pexpr, rgeopid, GPOS_ARRAY_SIZE(rgeopid));
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::FHasSubqueryExists
//
//	@doc:
//		Check if a given expression has a subquery exists
//
//---------------------------------------------------------------------------
BOOL
CExpressionPreprocessorTest::FHasSubqueryExists
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	COperator::EOperatorId rgeopid[] =
	{
		COperator::EopScalarSubqueryExists,
	};

	return CUtils::FHasOp(pexpr, rgeopid, GPOS_ARRAY_SIZE(rgeopid));
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::FHasSubqueryNotExists
//
//	@doc:
//		Check if a given expression has a subquery not exists
//
//---------------------------------------------------------------------------
BOOL
CExpressionPreprocessorTest::FHasSubqueryNotExists
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	COperator::EOperatorId rgeopid[] =
	{
		COperator::EopScalarSubqueryNotExists,
	};

	return CUtils::FHasOp(pexpr, rgeopid, GPOS_ARRAY_SIZE(rgeopid));
}


#ifdef GPOS_DEBUG
//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::FHasNoOuterJoin
//
//	@doc:
//		Check if a given expression has no Outer Join nodes
//
//---------------------------------------------------------------------------
BOOL
CExpressionPreprocessorTest::FHasNoOuterJoin
	(
	CExpression *pexpr
	)
{
	COperator::EOperatorId rgeopid[] =
	{
		COperator::EopLogicalLeftOuterJoin,
	};

	return !CUtils::FHasOp(pexpr, rgeopid, GPOS_ARRAY_SIZE(rgeopid));
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::FHasOuterRefs
//
//	@doc:
//		Check if a given expression has outer references in any node
//
//---------------------------------------------------------------------------
BOOL
CExpressionPreprocessorTest::FHasOuterRefs
	(
	CExpression *pexpr
	)
{
	COperator *pop = pexpr->Pop();
	BOOL fHasOuterRefs = (pop->FLogical() && CUtils::FHasOuterRefs(pexpr));
	if (fHasOuterRefs)
	{
		return true;
	}

	// recursively process children
	const ULONG ulArity = pexpr->UlArity();
	fHasOuterRefs = false;
	for (ULONG ul = 0; !fHasOuterRefs && ul < ulArity; ul++)
	{
		fHasOuterRefs = FHasOuterRefs((*pexpr)[ul]);
	}
	return fHasOuterRefs;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::FHasSeqPrj
//
//	@doc:
//		Check if a given expression has Sequence Project nodes
//
//---------------------------------------------------------------------------
BOOL
CExpressionPreprocessorTest::FHasSeqPrj
	(
	CExpression *pexpr
	)
{
	COperator::EOperatorId rgeopid[] =
	{
		COperator::EopLogicalSequenceProject,
	};

	return CUtils::FHasOp(pexpr, rgeopid, GPOS_ARRAY_SIZE(rgeopid));
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::FHasIDF
//
//	@doc:
//		Check if a given expression has IS DISTINCT FROM nodes
//
//---------------------------------------------------------------------------
BOOL
CExpressionPreprocessorTest::FHasIDF
	(
	CExpression *pexpr
	)
{
	COperator::EOperatorId rgeopid[] =
	{
		COperator::EopScalarIsDistinctFrom,
	};

	return CUtils::FHasOp(pexpr, rgeopid, GPOS_ARRAY_SIZE(rgeopid));
}

#endif // GPOD_DEBUG


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresUnittest_PreProcess
//
//	@doc:
//		Test of logical expression preprocessing
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresUnittest_PreProcess()
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	// setup a file-based provider
	CMDProviderMemory *pmdp = CTestUtils::m_pmdpf;
	pmdp->AddRef();
	CMDAccessor mda(pmp, CMDCache::Pcache(), CTestUtils::m_sysidDefault, pmdp);

	typedef CExpression *(*Pfpexpr)(IMemoryPool*);
	Pfpexpr rgpf[] =
		{
		CTestUtils::PexprLogicalSelectWithConstAnySubquery,
		CTestUtils::PexprLogicalSelectWithConstAllSubquery,
		CTestUtils::PexprLogicalSelectWithNestedAnd,
		CTestUtils::PexprLogicalSelectWithNestedOr,
		CTestUtils::PexprLogicalSelectWithEvenNestedNot,
		CTestUtils::PexprLogicalSelectWithOddNestedNot,
		CTestUtils::PexprLogicalSelectWithNestedAndOrNot,
		CTestUtils::PexprLogicalSelectOnOuterJoin,
		CTestUtils::PexprNAryJoinOnLeftOuterJoin,
		};

	for (ULONG i = 0; i < GPOS_ARRAY_SIZE(rgpf); i++)
	{
		// install opt context in TLS
		CAutoOptCtxt aoc
						(
						pmp,
						&mda,
						NULL,  /* pceeval */
						CTestUtils::Pcm(pmp)
						);

		// generate expression
		CExpression *pexpr = rgpf[i](pmp);

		CWStringDynamic str(pmp);
		COstreamString oss(&str);

		oss	<< std::endl << "EXPR:" << std::endl << *pexpr << std::endl;
		GPOS_TRACE(str.Wsz());
		str.Reset();

 		CExpression *pexprPreprocessed = CExpressionPreprocessor::PexprPreprocess(pmp, pexpr);
		oss	<< std::endl << "PREPROCESSED EXPR:" << std::endl << *pexprPreprocessed << std::endl;
		GPOS_TRACE(str.Wsz());
		str.Reset();

		pexprPreprocessed->Release();
		pexpr->Release();
	}

	return GPOS_OK;
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresUnittest_PreProcessWindowFunc
//
//	@doc:
//		Test preprocessing of window functions with unpushable predicates
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresUnittest_PreProcessWindowFunc()
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	// reset metadata cache
	CMDCache::Reset();

	// setup a file-based provider
	CMDProviderMemory *pmdp = CTestUtils::m_pmdpf;
	pmdp->AddRef();
	CMDAccessor mda(pmp, CMDCache::Pcache(), CTestUtils::m_sysidDefault, pmdp);

	CAutoOptCtxt aoc
					(
					pmp,
					&mda,
					NULL,  /* pceeval */
					CTestUtils::Pcm(pmp)
					);

	// generate a Select with a null-filtering predicate on top of Outer Join,
	// pre-processing should transform Outer Join to Inner Join
	CExpression *pexprSelectOnOuterJoin = CTestUtils::PexprLogicalSelectOnOuterJoin(pmp);

	// add a window function with a predicate on top of the Outer Join expression
	CExpression *pexprWindow = CTestUtils::PexprLogicalSequenceProject(pmp, GPDB_WIN_ROW_NUMBER, pexprSelectOnOuterJoin);
	CExpression *pexpr = CTestUtils::PexprLogicalSelect(pmp, pexprWindow);

	CWStringDynamic str(pmp);
	COstreamString oss(&str);

	oss << std::endl << "EXPR:" << std::endl << *pexpr << std::endl;
	GPOS_TRACE(str.Wsz());
	str.Reset();

 	CExpression *pexprPreprocessed = CExpressionPreprocessor::PexprPreprocess(pmp, pexpr);
	oss << std::endl << "PREPROCESSED EXPR:" << std::endl << *pexprPreprocessed << std::endl;
	GPOS_TRACE(str.Wsz());

	GPOS_ASSERT(FHasNoOuterJoin(pexprPreprocessed) && "unexpected outer join");

	str.Reset();

	pexprPreprocessed->Release();
	pexpr->Release();

	return GPOS_OK;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::PexprJoinHelper
//
//	@doc:
//		Helper function for testing cascaded inner/outer joins
//
//---------------------------------------------------------------------------
CExpression *
CExpressionPreprocessorTest::PexprJoinHelper
	(
	IMemoryPool *pmp,
	CExpression *pexprLOJ,
	BOOL fCascadedLOJ,
	BOOL fIntermediateInnerjoin
	)
{
	CExpression *pexprBottomJoin = pexprLOJ;
	CExpression *pexprResult = pexprBottomJoin;

	if (fIntermediateInnerjoin)
	{
		CExpression *pexprGet = CTestUtils::PexprLogicalGet(pmp);
		CColRef *pcrLeft = CDrvdPropRelational::Pdprel((*pexprBottomJoin)[0]->PdpDerive())->PcrsOutput()->PcrAny();
		CColRef *pcrRight = CDrvdPropRelational::Pdprel(pexprGet->PdpDerive())->PcrsOutput()->PcrAny();
		CExpression *pexprEquality = CUtils::PexprScalarEqCmp(pmp, pcrLeft, pcrRight);

		pexprBottomJoin = GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CLogicalInnerJoin(pmp), pexprBottomJoin, pexprGet, pexprEquality);
		pexprResult = pexprBottomJoin;
	}

	if (fCascadedLOJ)
	{
		// generate cascaded LOJ expression
		CExpression *pexprGet = CTestUtils::PexprLogicalGet(pmp);
		CColRef *pcrLeft = CDrvdPropRelational::Pdprel(pexprBottomJoin->PdpDerive())->PcrsOutput()->PcrAny();
		CColRef *pcrRight = CDrvdPropRelational::Pdprel(pexprGet->PdpDerive())->PcrsOutput()->PcrAny();
		CExpression *pexprEquality = CUtils::PexprScalarEqCmp(pmp, pcrLeft, pcrRight);

		pexprResult = GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CLogicalLeftOuterJoin(pmp), pexprBottomJoin, pexprGet, pexprEquality);
	}

	return pexprResult;
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::PexprWindowFuncWithLOJHelper
//
//	@doc:
//		Helper function for testing window functions with outer join
//
//---------------------------------------------------------------------------
CExpression *
CExpressionPreprocessorTest::PexprWindowFuncWithLOJHelper
	(
	IMemoryPool *pmp,
	CExpression *pexprLOJ,
	CColRef *pcrPartitionBy,
	BOOL fAddWindowFunction,
	BOOL fOuterChildPred,
	BOOL fCascadedLOJ,
	BOOL fPredBelowWindow
	)
{
	// add window function on top of join expression
	DrgPcr *pdrgpcrPartitionBy = GPOS_NEW(pmp) DrgPcr(pmp);
	pdrgpcrPartitionBy->Append(pcrPartitionBy);

	// add Select node on top of window function
	CExpression *pexprPred = CUtils::PexprScalarEqCmp(pmp, pcrPartitionBy, CUtils::PexprScalarConstInt4(pmp, 1 /*iVal*/));
	if (!fOuterChildPred && fCascadedLOJ)
	{
		// add another predicate on inner child of top LOJ
		CColRef *pcrInner = CDrvdPropRelational::Pdprel((*pexprLOJ)[1]->PdpDerive())->PcrsOutput()->PcrAny();
		if (fAddWindowFunction)
		{
			pdrgpcrPartitionBy->Append(pcrInner);
		}
		CExpression *pexprPred2 = CUtils::PexprScalarEqCmp(pmp, pcrInner, CUtils::PexprScalarConstInt4(pmp, 1 /*iVal*/));
		CExpression *pexprConjunction = CPredicateUtils::PexprConjunction(pmp, pexprPred, pexprPred2);
		pexprPred->Release();
		pexprPred2->Release();
		pexprPred = pexprConjunction;
	}

	if (fAddWindowFunction)
	{
		if (fPredBelowWindow)
		{
			pexprPred->AddRef();
			pexprLOJ = CUtils::PexprLogicalSelect(pmp, pexprLOJ, pexprPred);
		}

		CExpression *pexprPartitionedWinFunc = CXformUtils::PexprWindowWithRowNumber(pmp, pexprLOJ, pdrgpcrPartitionBy);
		pexprLOJ->Release();
		pdrgpcrPartitionBy->Release();

		return CUtils::PexprLogicalSelect(pmp, pexprPartitionedWinFunc, pexprPred);
	}

	pdrgpcrPartitionBy->Release();
	return 	CUtils::PexprLogicalSelect(pmp, pexprLOJ, pexprPred);
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::PreprocessOuterJoin
//
//	@doc:
//		Helper for preprocessing outer join by rewriting as inner join
//
//---------------------------------------------------------------------------
void
CExpressionPreprocessorTest::PreprocessOuterJoin
	(
	const CHAR *szFilePath,
	BOOL
#ifdef GPOS_DEBUG
	 fAllowOuterJoin
#endif // GPOS_DEBUG
	)
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	// reset metadata cache
	CMDCache::Reset();

	// set up MD providers
	CMDProviderMemory *pmdp = GPOS_NEW(pmp) CMDProviderMemory(pmp, szFilePath);
	GPOS_CHECK_ABORT;

	{
		CAutoMDAccessor amda(pmp, pmdp,  CTestUtils::m_sysidDefault);
		CAutoOptCtxt aoc(pmp, amda.Pmda(), NULL,  /* pceeval */ CTestUtils::Pcm(pmp));

		// read query expression
		CExpression *pexpr = CTestUtils::PexprReadQuery(pmp, szFilePath);
		GPOS_ASSERT(!FHasNoOuterJoin(pexpr) && "expected outer join");

		CWStringDynamic str(pmp);
		COstreamString oss(&str);

		oss << std::endl << "EXPR:" << std::endl << *pexpr << std::endl;
		GPOS_TRACE(str.Wsz());
		str.Reset();

		CExpression *pexprPreprocessed = CExpressionPreprocessor::PexprPreprocess(pmp, pexpr);
		oss << std::endl << "PREPROCESSED EXPR:" << std::endl << *pexprPreprocessed << std::endl;
		GPOS_TRACE(str.Wsz());

#ifdef GPOS_DEBUG
		if (fAllowOuterJoin)
		{
			GPOS_ASSERT(!FHasNoOuterJoin(pexprPreprocessed) && "expected outer join");
		}
		else
		{
			GPOS_ASSERT(FHasNoOuterJoin(pexprPreprocessed) && "unexpected outer join");
		}
#endif // GPOS_DEBUG

		str.Reset();

		pexprPreprocessed->Release();
		pexpr->Release();
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresUnittest_PreProcessOuterJoinMinidumps
//
//	@doc:
//		Test preprocessing of outer-joins ini minidumps by rewriting as inner-joins
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresUnittest_PreProcessOuterJoinMinidumps()
{
	// tests where OuterJoin must be converted to InnerJoin
	const CHAR *rgszOuterJoinPositiveTests[] =
	{
		"../data/dxl/expressiontests/LOJ-TO-InnerJoin-Q1.xml",
		"../data/dxl/expressiontests/LOJ-TO-InnerJoin-Q3.xml",
		"../data/dxl/expressiontests/LOJ-TO-InnerJoin-Q5.xml",
		"../data/dxl/expressiontests/LOJ-TO-InnerJoin-Q7.xml",
		"../data/dxl/expressiontests/LOJ-TO-InnerJoin-Q9.xml",
		"../data/dxl/expressiontests/LOJ-TO-InnerJoin-Q11.xml",
		"../data/dxl/expressiontests/LOJ-TO-InnerJoin-Q13.xml",
		"../data/dxl/expressiontests/LOJ-TO-InnerJoin-Q15.xml",
	};

	for (ULONG ul = 0; ul < GPOS_ARRAY_SIZE(rgszOuterJoinPositiveTests); ul++)
	{
		const CHAR *szFilePath = rgszOuterJoinPositiveTests[ul];
		PreprocessOuterJoin(szFilePath, false /*fAllowOuterJoin*/);
	}

	// tests where OuterJoin must NOT be converted to InnerJoin
	const CHAR *rgszOuterJoinNegativeTests[] =
	{
		"../data/dxl/expressiontests/LOJ-TO-InnerJoin-Q2.xml",
		"../data/dxl/expressiontests/LOJ-TO-InnerJoin-Q4.xml",
		"../data/dxl/expressiontests/LOJ-TO-InnerJoin-Q6.xml",
		"../data/dxl/expressiontests/LOJ-TO-InnerJoin-Q8.xml",
		"../data/dxl/expressiontests/LOJ-TO-InnerJoin-Q10.xml",
		"../data/dxl/expressiontests/LOJ-TO-InnerJoin-Q12.xml",
		"../data/dxl/expressiontests/LOJ-TO-InnerJoin-Q14.xml",
		"../data/dxl/expressiontests/LOJ-TO-InnerJoin-Q16.xml",
	};

	for (ULONG ul = 0; ul < GPOS_ARRAY_SIZE(rgszOuterJoinNegativeTests); ul++)
	{
		const CHAR *szFilePath = rgszOuterJoinNegativeTests[ul];
		PreprocessOuterJoin(szFilePath, true /*fAllowOuterJoin*/);
	}

	return GPOS_OK;
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresCompareExpressions
//
//	@doc:
//		Helper function for comparing expressions resulting from preprocessing
//		window functions with outer join
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresCompareExpressions
	(
	IMemoryPool *pmp,
	CWStringDynamic *rgstr[],
	ULONG ulSize
	)
{
	// check equality of processed expressions with/without the duplicate Select below Window
	for (ULONG ul = 0; ul < ulSize; ul+=2)
	{
		CWStringDynamic *pstrFst = rgstr[ul];
		CWStringDynamic *pstrSnd = rgstr[ul + 1];
		BOOL fEqual = pstrFst->FEquals(pstrSnd);

		if (!fEqual)
		{
			CAutoTrace at(pmp);
			at.Os() << std::endl << "EXPECTED EQUAL EXPRESSIONS:";
			at.Os() << std::endl << "EXPR1:" << std::endl << pstrFst->Wsz() << std::endl;
			at.Os() << std::endl << "EXPR2:" << std::endl << pstrSnd->Wsz() << std::endl;
		}
		GPOS_ASSERT(fEqual && "expected equal expressions");

		GPOS_DELETE(pstrFst);
		GPOS_DELETE(pstrSnd);

		if (!fEqual)
		{
			return GPOS_FAILED;
		}
	}

	return GPOS_OK;
}


//---------------------------------------------------------------------------
//
//	@function:
//		CExpressionPreprocessorTest::EresTestLOJ
//
//	@doc:
//		Test case generator for outer joins
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresTestLOJ
	(
	BOOL fAddWindowFunction // if true, a window function is added on top of outer join test cases
	)
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	// reset metadata cache
	CMDCache::Reset();

	// setup a file-based provider
	CMDProviderMemory *pmdp = CTestUtils::m_pmdpf;
	pmdp->AddRef();
	CMDAccessor mda(pmp, CMDCache::Pcache(), CTestUtils::m_sysidDefault, pmdp);


	// test for Select(SequenceProject(OuterJoin)) where Select's predicate is either:
	// 	(1) a predicate on LOJ's outer child that can be pushed through to the leaves, or
	// 	(2) a predicate on LOJ's inner child that can be used to turn outer join to inner join
	//
	// the flag fIntermediateInnerjoin controls adding InnerJoin to the expression
	// the flag fCascadedLOJ controls adding two cascaded LOJ's
	// the flag fOuterChildPred controls where the predicate columns are coming from with respect to LOJ children
	// the flag fPredBelowWindow controls duplicating the predicate below window function

	// the input expression looks like the following:
	//
	//		+--CLogicalSelect
	//		   |--CLogicalSequenceProject (HASHED: [ +--CScalarIdent "column_0000" (3) , nulls colocated ], [], [])  /* (added if fAddWindowFunction is TRUE) */
	//		   |  |--CLogicalSelect								/* (added if fPredBelowWindow is TRUE) */
	//		   |  |  |--CLogicalLeftOuterJoin
	//		   |  |  |  |--CLogicalInnerJoin					/* (added if fIntermediateInnerjoin is TRUE) */
	//		   |  |  |  |  |--CLogicalLeftOuterJoin				/* (added if fCascadedLOJ is TRUE) */
	//		   |  |  |  |  |  |--CLogicalGet "BaseTableAlias" ("BaseTable"), Columns: ["column_0000" (3), "column_0001" (4), "column_0002" (5)] Key sets: {[0]}
	//		   |  |  |  |  |  |--CLogicalGet "BaseTableAlias" ("BaseTable"), Columns: ["column_0000" (0), "column_0001" (1), "column_0002" (2)] Key sets: {[0]}
	//		   |  |  |  |  |  +--CScalarCmp (=)
	//		   |  |  |  |  |     |--CScalarIdent "column_0000" (3)
	//		   |  |  |  |  |     +--CScalarIdent "column_0000" (0)
	//		   |  |  |  |  |--CLogicalGet "BaseTableAlias" ("BaseTable"), Columns: ["column_0000" (6), "column_0001" (7), "column_0002" (8)] Key sets: {[0]}
	//		   |  |  |  |  +--CScalarCmp (=)
	//		   |  |  |  |     |--CScalarIdent "column_0000" (3)
	//		   |  |  |  |     +--CScalarIdent "column_0000" (6)
	//		   |  |  |  |--CLogicalGet "BaseTableAlias" ("BaseTable"), Columns: ["column_0000" (9), "column_0001" (10), "column_0002" (11)] Key sets: {[0]}
	//		   |  |  |  +--CScalarCmp (=)
	//		   |  |  |     |--CScalarIdent "column_0000" (0)
	//		   |  |  |     +--CScalarIdent "column_0000" (9)
	//		   |  |  +--CScalarCmp (=)
	//		   |  |     |--CScalarIdent "column_0000" (3)
	//		   |  |     +--CScalarConst (1)
	//		   |  +--CScalarProjectList
	//		   |     +--CScalarProjectElement "row_number" (12)
	//		   |        +--CScalarWindowFunc (row_number , Distinct: false)
	//		   +--CScalarCmp (=)
	//			  |--CScalarIdent "column_0000" (3)				/* (column is generated from LOJ's outer child if fOuterChildPred is TRUE) */
	//			  +--CScalarConst (1)


	// we generate 16 test cases using 4 nested loops that consider all possible values of the 4 flags

	// array to store string representation of all preprocessed expressions
	CWStringDynamic *rgstrResult[16];
	ULONG ulTestCases = 0;
	BOOL fIntermediateInnerjoin = false;
	for (ULONG ulInnerJoinCases = 0; ulInnerJoinCases < 2; ulInnerJoinCases++)
	{
		fIntermediateInnerjoin = !fIntermediateInnerjoin;

		BOOL fCascadedLOJ = false;
		for (ULONG ulLOJCases = 0; ulLOJCases < 2; ulLOJCases++)
		{
			fCascadedLOJ = !fCascadedLOJ;

			BOOL fOuterChildPred = false;
			for (ULONG ulPredCases = 0; ulPredCases < 2; ulPredCases++)
			{
				fOuterChildPred = !fOuterChildPred;

				BOOL fPredBelowWindow = false;
				for (ULONG ulPredBelowWindowCases = 0; ulPredBelowWindowCases < 2; ulPredBelowWindowCases++)
				{
					CAutoOptCtxt aoc(pmp, &mda, NULL /*pceeval*/,  CTestUtils::Pcm(pmp));

					fPredBelowWindow = fAddWindowFunction && !fPredBelowWindow;

					CExpression *pexprLOJ = CTestUtils::PexprLogicalJoin<CLogicalLeftOuterJoin>(pmp);
					CColRef *pcr = NULL;
					if (fOuterChildPred)
					{
						pcr = CDrvdPropRelational::Pdprel((*pexprLOJ)[0]->PdpDerive())->PcrsOutput()->PcrAny();
					}
					else
					{
						pcr = CDrvdPropRelational::Pdprel((*pexprLOJ)[1]->PdpDerive())->PcrsOutput()->PcrAny();
					}

					pexprLOJ = PexprJoinHelper(pmp, pexprLOJ, fCascadedLOJ, fIntermediateInnerjoin);

					CExpression *pexprSelect = PexprWindowFuncWithLOJHelper(pmp, pexprLOJ, pcr, fAddWindowFunction, fOuterChildPred, fCascadedLOJ, fPredBelowWindow);

					CExpression *pexprPreprocessed = CExpressionPreprocessor::PexprPreprocess(pmp, pexprSelect);

					{
						CAutoTrace at(pmp);
						at.Os() << std::endl << "WindowFunction: "<< fAddWindowFunction << ", IntermediateInnerjoin: " << fIntermediateInnerjoin << ", CascadedLOJ: " << fCascadedLOJ << ", OuterChildPred: " << fOuterChildPred << ", PredBelowWindow: " << fPredBelowWindow;
						at.Os() << std::endl << "EXPR:" << std::endl << *pexprSelect << std::endl;
						at.Os() << std::endl << "PREPROCESSED EXPR:" << std::endl << *pexprPreprocessed << std::endl;
					}

#ifdef GPOS_DEBUG
					if (fOuterChildPred)
					{
						GPOS_ASSERT(!FHasNoOuterJoin(pexprPreprocessed) && "expected outer join");
					}
					else
					{
						GPOS_ASSERT(FHasNoOuterJoin(pexprPreprocessed) && "unexpected outer join");
					}
#endif // GPOS_DEBUG

					// store string representation of preprocessed expression
					CWStringDynamic *pstr = GPOS_NEW(pmp) CWStringDynamic(pmp);
					COstreamString oss(pstr);
					oss << *pexprPreprocessed;
					rgstrResult[ulTestCases] = pstr;
					ulTestCases++;

					pexprSelect->Release();
					pexprPreprocessed->Release();
				}
			}
		}
	}

	return EresCompareExpressions(pmp, rgstrResult, ulTestCases);
}


//---------------------------------------------------------------------------
//
//	@function:
//		CExpressionPreprocessorTest::EresUnittest_InferPredsOnLOJ
//
//	@doc:
//		Test of inferring predicates on outer join
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresUnittest_InferPredsOnLOJ()
{
	return EresTestLOJ(false /*fAddWindowFunction*/);
}

//---------------------------------------------------------------------------
//
//	@function:
//		CExpressionPreprocessorTest::EresUnittest_PreProcessWindowFuncWithLOJ
//
//	@doc:
//		Test preprocessing of outer join with window functions
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresUnittest_PreProcessWindowFuncWithLOJ()
{
	return EresTestLOJ(true /*fAddWindowFunction*/);
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::PreprocessWinFuncWithOuterRefs
//
//	@doc:
//		Helper for preprocessing window functions with outer references
//
//---------------------------------------------------------------------------
void
CExpressionPreprocessorTest::PreprocessWinFuncWithOuterRefs
	(
	const CHAR *szFilePath,
	BOOL
#ifdef GPOS_DEBUG
		fAllowWinFuncOuterRefs
#endif // GPOS_DEBUG
	)
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();


	// reset metadata cache
	CMDCache::Reset();

	// set up MD providers
	CMDProviderMemory *pmdp = GPOS_NEW(pmp) CMDProviderMemory(pmp, szFilePath);
	GPOS_CHECK_ABORT;

	{
		CAutoMDAccessor amda(pmp, pmdp,  CTestUtils::m_sysidDefault);
		CAutoOptCtxt aoc(pmp, amda.Pmda(), NULL,  /* pceeval */ CTestUtils::Pcm(pmp));

		// read query expression
		CExpression *pexpr = CTestUtils::PexprReadQuery(pmp, szFilePath);
		GPOS_ASSERT(FHasOuterRefs(pexpr) && "expected outer references");

		CWStringDynamic str(pmp);
		COstreamString oss(&str);

		oss << std::endl << "EXPR:" << std::endl << *pexpr << std::endl;
		GPOS_TRACE(str.Wsz());
		str.Reset();

		CExpression *pexprPreprocessed = CExpressionPreprocessor::PexprPreprocess(pmp, pexpr);
		oss << std::endl << "PREPROCESSED EXPR:" << std::endl << *pexprPreprocessed << std::endl;
		GPOS_TRACE(str.Wsz());

#ifdef GPOS_DEBUG
		if (fAllowWinFuncOuterRefs)
		{
			GPOS_ASSERT(FHasOuterRefs(pexprPreprocessed) && "expected outer references");
		}
		else
		{
			GPOS_ASSERT(!FHasOuterRefs(pexprPreprocessed) && "unexpected outer references");
		}
#endif // GPOS_DEBUG

		str.Reset();

		pexprPreprocessed->Release();
		pexpr->Release();
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresUnittest_PreProcessWindowFuncWithOuterRefs
//
//	@doc:
//		Test preprocessing of window functions when no outer references
//		are expected
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresUnittest_PreProcessWindowFuncWithOuterRefs()
{
	const CHAR *rgszTestsNoOuterRefs[] =
	{
		"../data/dxl/expressiontests/WinFunc-OuterRef-Partition-Query.xml",
		"../data/dxl/expressiontests/WinFunc-OuterRef-Partition-Order-Query.xml",
	};

	for (ULONG ul = 0; ul < GPOS_ARRAY_SIZE(rgszTestsNoOuterRefs); ul++)
	{
		const CHAR *szFilePath = rgszTestsNoOuterRefs[ul];
		PreprocessWinFuncWithOuterRefs(szFilePath, false /*fAllowWinFuncOuterRefs*/);
	}

	const CHAR *rgszTestsOuterRefs[] =
	{
		"../data/dxl/expressiontests/WinFunc-OuterRef-Partition-Order-Frames-Query.xml",
	};

	for (ULONG ul = 0; ul < GPOS_ARRAY_SIZE(rgszTestsOuterRefs); ul++)
	{
		const CHAR *szFilePath = rgszTestsOuterRefs[ul];
		PreprocessWinFuncWithOuterRefs(szFilePath, true /*fAllowWinFuncOuterRefs*/);
	}

	return GPOS_OK;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::PreprocessWinFuncWithDistinctAggs
//
//	@doc:
//		Helper for preprocessing window functions with distinct aggs
//
//---------------------------------------------------------------------------
void
CExpressionPreprocessorTest::PreprocessWinFuncWithDistinctAggs
	(
	IMemoryPool *pmp,
	const CHAR *szFilePath,
	BOOL
#ifdef GPOS_DEBUG
		fAllowSeqPrj
#endif // GPOS_DEBUG
	,
	BOOL
#ifdef GPOS_DEBUG
		fAllowIDF
#endif // GPOS_DEBUG

	)
{
	// read query expression
	CExpression *pexpr = CTestUtils::PexprReadQuery(pmp, szFilePath);
	GPOS_ASSERT(FHasSeqPrj(pexpr) && "expected sequence project");

	CWStringDynamic str(pmp);
	COstreamString oss(&str);

	oss << std::endl << "EXPR:" << std::endl << *pexpr << std::endl;
	GPOS_TRACE(str.Wsz());
	str.Reset();

	CExpression *pexprPreprocessed = CExpressionPreprocessor::PexprPreprocess(pmp, pexpr);
	oss << std::endl << "PREPROCESSED EXPR:" << std::endl << *pexprPreprocessed << std::endl;
	GPOS_TRACE(str.Wsz());

#ifdef GPOS_DEBUG
	if (fAllowSeqPrj)
	{
		GPOS_ASSERT(FHasSeqPrj(pexprPreprocessed) && "expected sequence project");
	}
	else
	{
		GPOS_ASSERT(!FHasSeqPrj(pexprPreprocessed) && "unexpected sequence project");
	}

	if (fAllowIDF)
	{
		GPOS_ASSERT(FHasIDF(pexprPreprocessed) && "expected (is distinct from)");
	}
	else
	{
		GPOS_ASSERT(!FHasIDF(pexprPreprocessed) && "unexpected (is distinct from)");
	}

#endif // GPOS_DEBUG

	str.Reset();

	pexprPreprocessed->Release();
	pexpr->Release();
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresUnittest_PreProcessWindowFuncWithDistinctAggs
//
//	@doc:
//		Test preprocessing of window functions with distinct aggregates
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresUnittest_PreProcessWindowFuncWithDistinctAggs()
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	// tests where preprocessing removes SeqPrj nodes
	const CHAR *rgszTestsDistinctAggsRemoveWindow[] =
	{
		"../data/dxl/expressiontests/WinFunc-Single-DQA-Query.xml",
		"../data/dxl/expressiontests/WinFunc-Multiple-DQA-Query.xml",
		"../data/dxl/expressiontests/WinFunc-Multiple-DQA-Query-2.xml",
		"../data/dxl/expressiontests/WinFunc-Multiple-DQA-Query-3.xml",
	};

	// tests where preprocessing removes SeqPrj nodes and adds join with INDF condition
	const CHAR *rgszTestsDistinctAggsRemoveWindowINDF[] =
	{
		"../data/dxl/expressiontests/WinFunc-Multiple-DQA-Query-PartitionBy-SameColumn.xml",
		"../data/dxl/expressiontests/WinFunc-Multiple-DQA-Query-PartitionBy-SameColumn-2.xml",
		"../data/dxl/expressiontests/WinFunc-Multiple-DQA-Query-PartitionBy-DifferentColumn.xml",
		"../data/dxl/expressiontests/WinFunc-Multiple-DQA-Query-PartitionBy-DifferentColumn-2.xml",
	};

	// tests where preprocessing does not remove SeqPrj nodes
	const CHAR *rgszTestsDistinctAggsDoNotRemoveWindow[] =
	{
		"../data/dxl/expressiontests/WinFunc-Multiple-DQA-Query-RowNumber.xml",
		"../data/dxl/expressiontests/WinFunc-Multiple-DQA-Query-RowNumber-2.xml",
	};

	// tests where preprocessing does not remove SeqPrj nodes and add join with INDF condition
	const CHAR *rgszTestsDistinctAggsDoNotRemoveWindowINDF[] =
	{
		"../data/dxl/expressiontests/WinFunc-Multiple-DQA-Query-RowNumber-PartitionBy-SameColumn.xml",
		"../data/dxl/expressiontests/WinFunc-Multiple-DQA-Query-RowNumber-PartitionBy-SameColumn-2.xml",
		"../data/dxl/expressiontests/WinFunc-Multiple-DQA-Query-RowNumber-OrderBy-PartitionBy-SameColumn.xml",
		"../data/dxl/expressiontests/WinFunc-Multiple-DQA-Query-RowNumber-OrderBy-PartitionBy-SameColumn-2.xml",
		"../data/dxl/expressiontests/WinFunc-Multiple-DQA-Query-RowNumber-Distinct-Different-Columns.xml",
		"../data/dxl/expressiontests/WinFunc-Multiple-DQA-Query-RowNumber-Distinct-ParitionBy-Different-Columns.xml",
		"../data/dxl/expressiontests/WinFunc-Multiple-DQA-Query-RowNumber-Multiple-ParitionBy-Columns.xml",
	};

	// path to metadata file of the previous tests
	const CHAR *szMDFilePath = "../data/dxl/expressiontests/WinFunc-Tests-MD.xml";

	// reset metadata cache
	CMDCache::Reset();

	// set up MD providers
	CMDProviderMemory *pmdp = GPOS_NEW(pmp) CMDProviderMemory(pmp, szMDFilePath);

	GPOS_CHECK_ABORT;

	{
		CAutoMDAccessor amda(pmp, pmdp,  CTestUtils::m_sysidDefault);
		CAutoOptCtxt aoc(pmp, amda.Pmda(), NULL,  /* pceeval */ CTestUtils::Pcm(pmp));

		for (ULONG ul = 0; ul < GPOS_ARRAY_SIZE(rgszTestsDistinctAggsRemoveWindow); ul++)
		{
			PreprocessWinFuncWithDistinctAggs(pmp, rgszTestsDistinctAggsRemoveWindow[ul], false /* fAllowSeqPrj */, false /* fAllowIDF */);
		}

		for (ULONG ul = 0; ul < GPOS_ARRAY_SIZE(rgszTestsDistinctAggsRemoveWindowINDF); ul++)
		{
			PreprocessWinFuncWithDistinctAggs(pmp, rgszTestsDistinctAggsRemoveWindowINDF[ul], false /* fAllowSeqPrj */, true /* fAllowIDF */);
		}

		for (ULONG ul = 0; ul < GPOS_ARRAY_SIZE(rgszTestsDistinctAggsDoNotRemoveWindow); ul++)
		{
			PreprocessWinFuncWithDistinctAggs(pmp, rgszTestsDistinctAggsDoNotRemoveWindow[ul], true /* fAllowSeqPrj */, false /* fAllowIDF */);
		}

		for (ULONG ul = 0; ul < GPOS_ARRAY_SIZE(rgszTestsDistinctAggsDoNotRemoveWindowINDF); ul++)
		{
			PreprocessWinFuncWithDistinctAggs(pmp, rgszTestsDistinctAggsDoNotRemoveWindowINDF[ul], true /* fAllowSeqPrj */, true /* fAllowIDF */);
		}
	}

	return GPOS_OK;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::UlScalarSubqs
//
//	@doc:
//		Count number of scalar subqueries
//
//---------------------------------------------------------------------------
ULONG
CExpressionPreprocessorTest::UlScalarSubqs
	(
	CExpression *pexpr
	)
{
	GPOS_CHECK_STACK_SIZE;
	GPOS_ASSERT(NULL != pexpr);

	ULONG ulSubqs = 0;
	COperator *pop = pexpr->Pop();
	if (COperator::EopScalarSubquery == pop->Eopid())
	{
		ulSubqs = 1;
	}

	// recursively process children
	const ULONG ulArity = pexpr->UlArity();
	ULONG ulChildSubqs = 0;
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		ulChildSubqs += UlScalarSubqs((*pexpr)[ul]);
	}

	return ulSubqs + ulChildSubqs;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresUnittest_UnnestSubqueries
//
//	@doc:
//		Test preprocessing of nested scalar subqueries that can be eliminated
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresUnittest_UnnestSubqueries()
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	// reset metadata cache
	CMDCache::Reset();

	// setup a file-based provider
	CMDProviderMemory *pmdp = CTestUtils::m_pmdpf;
	pmdp->AddRef();
	CMDAccessor mda(pmp, CMDCache::Pcache(), CTestUtils::m_sysidDefault, pmdp);

	CAutoOptCtxt aoc(pmp, &mda, NULL,  /* pceeval */ CTestUtils::Pcm(pmp));

	SUnnestSubqueriesTestCase rgunnesttc[] =
		{
			{CTestUtils::PexprScalarCmpIdentToConstant, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopOr, COperator::EopSentinel, false},
			{CTestUtils::PexprScalarCmpIdentToConstant, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopAnd, COperator::EopSentinel, false},

			{CTestUtils::PexprScalarCmpIdentToConstant, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopOr, COperator::EopSentinel,  true},
			{CTestUtils::PexprScalarCmpIdentToConstant, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopAnd, COperator::EopSentinel, true},

			{CTestUtils::PexprExistsSubquery, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopOr, COperator::EopScalarSubqueryNotExists, false},
			{CTestUtils::PexprExistsSubquery, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopAnd, COperator::EopScalarSubqueryNotExists, false},

			{CTestUtils::PexprNotExistsSubquery, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopOr, COperator::EopScalarSubqueryExists, false},
			{CTestUtils::PexprNotExistsSubquery, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopAnd, COperator::EopScalarSubqueryExists, false},

			{CTestUtils::PexprExistsSubquery, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopOr, COperator::EopScalarSubqueryExists, true},
			{CTestUtils::PexprExistsSubquery, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopAnd, COperator::EopScalarSubqueryExists, true},

			{CTestUtils::PexprNotExistsSubquery, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopOr, COperator::EopScalarSubqueryNotExists, true},
			{CTestUtils::PexprNotExistsSubquery, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopAnd, COperator::EopScalarSubqueryNotExists, true},

			{CTestUtils::PexpSubqueryAny, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopOr, COperator::EopScalarSubqueryAny, false},
			{CTestUtils::PexpSubqueryAny, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopAnd, COperator::EopScalarSubqueryAny, false},

			{CTestUtils::PexpSubqueryAll, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopOr, COperator::EopScalarSubqueryAll, false},
			{CTestUtils::PexpSubqueryAll, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopAnd, COperator::EopScalarSubqueryAll, false},

			{CTestUtils::PexpSubqueryAny, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopOr, COperator::EopScalarSubqueryAny, true},
			{CTestUtils::PexpSubqueryAny, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopAnd, COperator::EopScalarSubqueryAny, true},

			{CTestUtils::PexpSubqueryAll, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopOr, COperator::EopScalarSubqueryAll, true},
			{CTestUtils::PexpSubqueryAll, CTestUtils::PexprScalarCmpIdentToConstant, CScalarBoolOp::EboolopAnd, COperator::EopScalarSubqueryAll, true},
		};

	GPOS_RESULT eres = GPOS_OK;

	const ULONG ulTestCases = GPOS_ARRAY_SIZE(rgunnesttc);
	for (ULONG ul = 0; ul < ulTestCases && (GPOS_OK == eres); ul++)
	{
		SUnnestSubqueriesTestCase elem = rgunnesttc[ul];

		// generate the logical get
		CExpression *pexprGet = CTestUtils::PexprLogicalGet(pmp);

		// generate the children of AND/OR predicate
		FnPexprUnnestTestCase *pfFst = elem.m_pfFst;
		FnPexprUnnestTestCase *pfSnd = elem.m_pfSnd;

		GPOS_ASSERT(NULL != pfFst);
		GPOS_ASSERT(NULL != pfSnd);
		CExpression *pexprPredFst = pfFst(pmp, pexprGet);
		CExpression *pexprPredSnd = pfSnd(pmp, pexprGet);

		BOOL fNegateChildren = elem.m_fNegateChildren;
		DrgPexpr *pdrgpexprAndOr = GPOS_NEW(pmp) DrgPexpr(pmp);

		if (fNegateChildren)
		{
			DrgPexpr *pdrgpexprFst = GPOS_NEW(pmp) DrgPexpr(pmp);
			pdrgpexprFst->Append(pexprPredFst);
			pdrgpexprAndOr->Append(CUtils::PexprScalarBoolOp(pmp, CScalarBoolOp::EboolopNot , pdrgpexprFst));

			DrgPexpr *pdrgpexprSnd = GPOS_NEW(pmp) DrgPexpr(pmp);
			pdrgpexprSnd->Append(pexprPredSnd);
			pdrgpexprAndOr->Append(CUtils::PexprScalarBoolOp(pmp, CScalarBoolOp::EboolopNot , pdrgpexprSnd));
		}
		else
		{
			pdrgpexprAndOr->Append(pexprPredFst);
			pdrgpexprAndOr->Append(pexprPredSnd);
		}

		CScalarBoolOp::EBoolOperator eboolop = elem.m_eboolop;
		CExpression *pexprAndOr = CUtils::PexprScalarBoolOp(pmp, eboolop , pdrgpexprAndOr);

		DrgPexpr *pdrgpexprNot = GPOS_NEW(pmp) DrgPexpr(pmp);
		pdrgpexprNot->Append(pexprAndOr);

		CExpression *pexpr = GPOS_NEW(pmp) CExpression
								(
								pmp,
								GPOS_NEW(pmp) CLogicalSelect(pmp),
								pexprGet,
								CUtils::PexprScalarBoolOp(pmp, CScalarBoolOp::EboolopNot , pdrgpexprNot)
								);

		CExpression *pexprProcessed = CExpressionUtils::PexprUnnest(pmp, pexpr);

		{
			CAutoTrace at(pmp);
			at.Os() << std::endl << "EXPR:" << std::endl << *pexpr << std::endl;
			at.Os() << std::endl << "PREPROCESSED EXPR:" << std::endl << *pexprProcessed << std::endl;
		}

		const CExpression *pexprFirst = CTestUtils::PexprFirst(pexprProcessed, COperator::EopScalarBoolOp);
		if (NULL == pexprFirst || (eboolop == CScalarBoolOp::PopConvert(pexprFirst->Pop())->Eboolop()))
		{
			eres = GPOS_FAILED;
		}

		// operator that should be present after unnesting
		eres = EresCheckSubqueryType(pexprProcessed, elem.m_eopidPresent);

		// clean up
		pexpr->Release();
		pexprProcessed->Release();
	}

	return eres;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresCheckExistsSubqueryType
//
//	@doc:
//		Check the type of the existential subquery
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresCheckExistsSubqueryType
	(
	CExpression *pexpr,
	COperator::EOperatorId eopidPresent
	)
{
	if (COperator::EopScalarSubqueryNotExists == eopidPresent && (FHasSubqueryExists(pexpr) || !FHasSubqueryNotExists(pexpr)))
	{
		return GPOS_FAILED;
	}

	if (COperator::EopScalarSubqueryExists == eopidPresent && (FHasSubqueryNotExists(pexpr) || !FHasSubqueryExists(pexpr)))
	{
		return GPOS_FAILED;
	}

	return GPOS_OK;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresCheckQuantifiedSubqueryType
//
//	@doc:
//		Check the type of the quantified subquery
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresCheckQuantifiedSubqueryType
	(
	CExpression *pexpr,
	COperator::EOperatorId eopidPresent
	)
{
	if (COperator::EopScalarSubqueryAny == eopidPresent && !FHasSubqueryAny(pexpr))
	{
		return GPOS_FAILED;
	}

	if (COperator::EopScalarSubqueryAll == eopidPresent && !FHasSubqueryAll(pexpr))
	{
		return GPOS_FAILED;
	}

	return GPOS_OK;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresCheckSubqueryType
//
//	@doc:
//		Check the type of the subquery
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresCheckSubqueryType
	(
	CExpression *pexpr,
	COperator::EOperatorId eopidPresent
	)
{
	GPOS_ASSERT(NULL != pexpr);

	if (COperator::EopSentinel == eopidPresent)
	{
		// no checks needed

		return GPOS_OK;
	}

	if (GPOS_OK == EresCheckExistsSubqueryType(pexpr, eopidPresent))
	{
		return GPOS_OK;
	}

	if (GPOS_OK == EresCheckQuantifiedSubqueryType(pexpr, eopidPresent))
	{
		return GPOS_OK;
	}

	return GPOS_FAILED;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresUnittest_PreProcessNestedScalarSubqueries
//
//	@doc:
//		Test preprocessing of nested scalar subqueries that can be eliminated
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresUnittest_PreProcessNestedScalarSubqueries()
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	// reset metadata cache
	CMDCache::Reset();

	// setup a file-based provider
	CMDProviderMemory *pmdp = CTestUtils::m_pmdpf;
	pmdp->AddRef();
	CMDAccessor mda(pmp, CMDCache::Pcache(), CTestUtils::m_sysidDefault, pmdp);

	CAutoOptCtxt aoc
					(
					pmp,
					&mda,
					NULL,  /* pceeval */
					CTestUtils::Pcm(pmp)
					);

	CExpression *pexprGet = CTestUtils::PexprLogicalGet(pmp);
	const CColRef *pcrInner = CDrvdPropRelational::Pdprel(pexprGet->PdpDerive())->PcrsOutput()->PcrAny();
	CExpression *pexprSubqInner = GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CScalarSubquery(pmp, pcrInner, false /*fGeneratedByExist*/, false /*fGeneratedByQuantified*/), pexprGet);
	CExpression *pexprCTG1 = CUtils::PexprLogicalCTGDummy(pmp);
	CExpression *pexprPrj1 = CUtils::PexprAddProjection(pmp, pexprCTG1, pexprSubqInner);

	const CColRef *pcrComputed = CScalarProjectElement::PopConvert((*(*pexprPrj1)[1])[0]->Pop())->Pcr();
	CExpression *pexprSubqOuter = GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CScalarSubquery(pmp, pcrComputed, false /*fGeneratedByExist*/, false /*fGeneratedByQuantified*/), pexprPrj1);
	CExpression *pexprCTG2 = CUtils::PexprLogicalCTGDummy(pmp);
	CExpression *pexprPrj2 = CUtils::PexprAddProjection(pmp, pexprCTG2, pexprSubqOuter);

	CWStringDynamic str(pmp);
	COstreamString oss(&str);

	oss << std::endl << "EXPR:" << std::endl << *pexprPrj2 << std::endl;
	GPOS_TRACE(str.Wsz());
	str.Reset();

 	CExpression *pexprPreprocessed = CExpressionPreprocessor::PexprPreprocess(pmp, pexprPrj2);
	oss << std::endl << "PREPROCESSED EXPR:" << std::endl << *pexprPreprocessed << std::endl;
	GPOS_TRACE(str.Wsz());

	GPOS_ASSERT(1 == UlScalarSubqs(pexprPreprocessed) &&
			"expecting ONE scalar subquery in preprocessed expression");

	pexprPreprocessed->Release();
	pexprPrj2->Release();

	return GPOS_OK;
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresUnittest_PreProcessOuterJoin
//
//	@doc:
//		Test of preprocessing outer-joins by rewriting as inner-joins
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresUnittest_PreProcessOuterJoin()
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	// reset metadata cache
	CMDCache::Reset();

	// setup a file-based provider
	CMDProviderMemory *pmdp = CTestUtils::m_pmdpf;
	pmdp->AddRef();
	CMDAccessor mda(pmp, CMDCache::Pcache(), CTestUtils::m_sysidDefault, pmdp);

	CAutoOptCtxt aoc(pmp, &mda, NULL /*pceeval*/, CTestUtils::Pcm(pmp));

	CExpression *pexprLOJ = CTestUtils::PexprLogicalJoin<CLogicalLeftOuterJoin>(pmp);
	CColRefSet *pcrsInner = CDrvdPropRelational::Pdprel((*pexprLOJ)[1]->PdpDerive())->PcrsOutput();

	// test case 1: generate a single comparison predicate between an inner column and const
	CColRef *pcrInner = pcrsInner->PcrAny();
	CExpression *pexprPredicate1 = CUtils::PexprScalarEqCmp(pmp, pcrInner, CUtils::PexprScalarConstInt4(pmp, 1 /*iVal*/));
	CExpression *pexprSelect1 = CUtils::PexprLogicalSelect(pmp, pexprLOJ, pexprPredicate1);

 	CExpression *pexprPreprocessed1 = CExpressionPreprocessor::PexprPreprocess(pmp, pexprSelect1);
 	{
 		CAutoTrace at(pmp);
 		at.Os() << "EXPR:" << std::endl << *pexprSelect1 << std::endl;
 		at.Os() << "No outer joins are expected after preprocessing:" << std::endl;
 		at.Os() << "PREPROCESSED EXPR:" << std::endl << *pexprPreprocessed1 << std::endl;
 	}
	GPOS_ASSERT(FHasNoOuterJoin(pexprPreprocessed1) && "unexpected outer join");


 	// test case 2: generate a conjunction of predicates involving inner columns
 	CColRefSet *pcrsOuter = CDrvdPropRelational::Pdprel((*pexprLOJ)[0]->PdpDerive())->PcrsOutput();
	CColRef *pcrOuter = pcrsOuter->PcrAny();
	CExpression *pexprCmp1 = CUtils::PexprScalarEqCmp(pmp, pcrInner, pcrOuter);
	CExpression *pexprCmp2 = CUtils::PexprScalarEqCmp(pmp, pcrInner, CUtils::PexprScalarConstInt4(pmp, 1 /*iVal*/));
	CExpression *pexprPredicate2 = CPredicateUtils::PexprConjunction(pmp, pexprCmp1, pexprCmp2);
	pexprCmp1->Release();
	pexprCmp2->Release();
	pexprLOJ->AddRef();
	CExpression *pexprSelect2 = CUtils::PexprLogicalSelect(pmp, pexprLOJ, pexprPredicate2);
	CExpression *pexprPreprocessed2 = CExpressionPreprocessor::PexprPreprocess(pmp, pexprSelect2);
 	{
 		CAutoTrace at(pmp);
 		at.Os() << "EXPR:" << std::endl << *pexprSelect2 << std::endl;
 		at.Os() << "No outer joins are expected after preprocessing:" << std::endl;
 		at.Os() << "PREPROCESSED EXPR:" << std::endl << *pexprPreprocessed2 << std::endl;
 	}
	GPOS_ASSERT(FHasNoOuterJoin(pexprPreprocessed2) && "unexpected outer join");


	// test case 3: generate a disjunction of predicates involving inner columns
	pexprCmp1 = CUtils::PexprScalarEqCmp(pmp, pcrInner, pcrOuter);
	pexprCmp2 = CUtils::PexprScalarEqCmp(pmp, pcrInner,  CUtils::PexprScalarConstInt4(pmp, 1 /*iVal*/));
	CExpression *pexprPredicate3 = CPredicateUtils::PexprDisjunction(pmp, pexprCmp1, pexprCmp2);
	pexprCmp1->Release();
	pexprCmp2->Release();
	pexprLOJ->AddRef();
	CExpression *pexprSelect3 = CUtils::PexprLogicalSelect(pmp, pexprLOJ, pexprPredicate3);

	CExpression *pexprPreprocessed3 = CExpressionPreprocessor::PexprPreprocess(pmp, pexprSelect3);
 	{
 		CAutoTrace at(pmp);
 		at.Os() << "EXPR:" << std::endl << *pexprSelect3 << std::endl;
 		at.Os() << "No outer joins are expected after preprocessing:" << std::endl;
 		at.Os() << "PREPROCESSED EXPR:" << std::endl << *pexprPreprocessed3 << std::endl;
 	}
	GPOS_ASSERT(FHasNoOuterJoin(pexprPreprocessed3) && "unexpected outer join");

	// test case 4: generate a null-rejecting conjunction since it involves one null-rejecting conjunct
	pexprCmp1 = CUtils::PexprScalarEqCmp(pmp, pcrInner, pcrOuter);
	pexprCmp2 = CUtils::PexprScalarEqCmp(pmp, pcrOuter, CUtils::PexprScalarConstInt4(pmp, 1 /*iVal*/));
	CExpression *pexprPredicate4 = CPredicateUtils::PexprConjunction(pmp, pexprCmp1, pexprCmp2);
	pexprCmp1->Release();
	pexprCmp2->Release();
	pexprLOJ->AddRef();
	CExpression *pexprSelect4 = CUtils::PexprLogicalSelect(pmp, pexprLOJ, pexprPredicate4);

	CExpression *pexprPreprocessed4 = CExpressionPreprocessor::PexprPreprocess(pmp, pexprSelect4);
 	{
 		CAutoTrace at(pmp);
 		at.Os() << "EXPR:" << std::endl << *pexprSelect4 << std::endl;
 		at.Os() << "No outer joins are expected after preprocessing:" << std::endl;
 		at.Os() << "PREPROCESSED EXPR:" << std::endl << *pexprPreprocessed4 << std::endl;
 	}
	GPOS_ASSERT(FHasNoOuterJoin(pexprPreprocessed4) && "unexpected outer join");


	// test case 5: generate a null-passing disjunction since it involves a predicate on outer columns
	pexprCmp1 = CUtils::PexprScalarEqCmp(pmp, pcrInner, pcrOuter);
	pexprCmp2 = CUtils::PexprScalarEqCmp(pmp, pcrOuter, CUtils::PexprScalarConstInt4(pmp, 1 /*iVal*/));
	CExpression *pexprPredicate5 = CPredicateUtils::PexprDisjunction(pmp, pexprCmp1, pexprCmp2);
	pexprCmp1->Release();
	pexprCmp2->Release();
	pexprLOJ->AddRef();
	CExpression *pexprSelect5 = CUtils::PexprLogicalSelect(pmp, pexprLOJ, pexprPredicate5);

	CExpression *pexprPreprocessed5 = CExpressionPreprocessor::PexprPreprocess(pmp, pexprSelect5);
 	{
 		CAutoTrace at(pmp);
 		at.Os() << "EXPR:" << std::endl << *pexprSelect5 << std::endl;
 		at.Os() << "Outer joins are expected after preprocessing:" << std::endl;
 		at.Os() << "PREPROCESSED EXPR:" << std::endl << *pexprPreprocessed5 << std::endl;
 	}
	GPOS_ASSERT(!FHasNoOuterJoin(pexprPreprocessed5) && "expected outer join");

	// test case 6: generate a negated null-passing disjunction
	pexprPredicate5->AddRef();
	CExpression *pexprPredicate6 = CUtils::PexprNegate(pmp, pexprPredicate5);
	pexprLOJ->AddRef();
	CExpression *pexprSelect6 = CUtils::PexprLogicalSelect(pmp, pexprLOJ, pexprPredicate6);

	CExpression *pexprPreprocessed6 = CExpressionPreprocessor::PexprPreprocess(pmp, pexprSelect6);
 	{
 		CAutoTrace at(pmp);
 		at.Os() << "EXPR:" << std::endl << *pexprSelect6 << std::endl;
 		at.Os() << "No outer joins are expected after preprocessing:" << std::endl;
 		at.Os() << "PREPROCESSED EXPR:" << std::endl << *pexprPreprocessed6 << std::endl;
 	}
	GPOS_ASSERT(FHasNoOuterJoin(pexprPreprocessed6) && "unexpected outer join");

 	pexprSelect1->Release();
 	pexprPreprocessed1->Release();
 	pexprSelect2->Release();
 	pexprPreprocessed2->Release();
	pexprSelect3->Release();
 	pexprPreprocessed3->Release();
	pexprSelect4->Release();
 	pexprPreprocessed4->Release();
	pexprSelect5->Release();
 	pexprPreprocessed5->Release();
	pexprSelect6->Release();
 	pexprPreprocessed6->Release();

	return GPOS_OK;
}


//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::PexprCreateConjunction
//
//	@doc:
//		Create a conjunction of comparisons using the given columns.
//
//---------------------------------------------------------------------------
CExpression *
CExpressionPreprocessorTest::PexprCreateConjunction
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr
	)
{
	GPOS_ASSERT(NULL != pdrgpcr);

	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	ULONG ulLength = pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulLength; ++ul)
	{
		CExpression *pexprComparison= CUtils::PexprScalarEqCmp
										(
										pmp,
										(*pdrgpcr)[ul],
										CUtils::PexprScalarConstInt4(pmp, ul)
										);
		pdrgpexpr->Append(pexprComparison);
	}

	return CPredicateUtils::PexprConjunction(pmp, pdrgpexpr);
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresUnittest_PreProcessPrefilters
//
//	@doc:
//		Test extraction of prefilters out of disjunctive expressions
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresUnittest_PreProcessOrPrefilters()
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	// reset metadata cache
	CMDCache::Reset();

	// setup a file-based provider
	CMDProviderMemory *pmdp = CTestUtils::m_pmdpf;
	pmdp->AddRef();
	CMDAccessor mda(pmp, CMDCache::Pcache(), CTestUtils::m_sysidDefault, pmdp);
	CAutoOptCtxt aoc(pmp, &mda, NULL /*pceeval*/, CTestUtils::Pcm(pmp));
	CExpression *pexprJoin = CTestUtils::PexprLogicalJoin<CLogicalInnerJoin>(pmp);

	CColRefSet *pcrsInner = CDrvdPropRelational::Pdprel((*pexprJoin)[1]->PdpDerive())->PcrsOutput();
	DrgPcr *pdrgpcrInner = pcrsInner->Pdrgpcr(pmp);
	GPOS_ASSERT(3 <= pdrgpcrInner->UlSafeLength());

	CColRefSet *pcrsOuter = CDrvdPropRelational::Pdprel((*pexprJoin)[0]->PdpDerive())->PcrsOutput();
	DrgPcr *pdrgpcrOuter = pcrsOuter->Pdrgpcr(pmp);
	GPOS_ASSERT(3 <= pdrgpcrOuter->UlSafeLength());

	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);

	// every disjunct has one or two comparisons on various outer columns and one comparison on
	// the first inner column, so we expect to have prefilters both on the outer and the inner tables
	DrgPcr *pdrgpcrDisjunct = GPOS_NEW(pmp) DrgPcr(pmp);
	CColRef *pcr0_0 = (*pdrgpcrOuter)[0];
	pdrgpcrDisjunct->Append(pcr0_0);
	CColRef *pcr1_0 = (*pdrgpcrInner)[0];
	pdrgpcrDisjunct->Append(pcr1_0);
	pdrgpexpr->Append(PexprCreateConjunction(pmp, pdrgpcrDisjunct));
	pdrgpcrDisjunct->Release();

	pdrgpcrDisjunct = GPOS_NEW(pmp) DrgPcr(pmp);
	CColRef *pcr0_1 = (*pdrgpcrOuter)[1];
	pdrgpcrDisjunct->Append(pcr0_1);
	CColRef *pcr0_2 = (*pdrgpcrOuter)[2];
	pdrgpcrDisjunct->Append(pcr0_2);
	pdrgpcrDisjunct->Append(pcr1_0);
	pdrgpexpr->Append(PexprCreateConjunction(pmp, pdrgpcrDisjunct));
	pdrgpcrDisjunct->Release();

	pdrgpcrDisjunct = GPOS_NEW(pmp) DrgPcr(pmp);
	pdrgpcrDisjunct->Append(pcr0_2);
	pdrgpcrDisjunct->Append(pcr1_0);
	pdrgpexpr->Append(PexprCreateConjunction(pmp, pdrgpcrDisjunct));
	pdrgpcrDisjunct->Release();

	pdrgpcrInner->Release();
	pdrgpcrOuter->Release();

	CExpression *pexprPredicate = CPredicateUtils::PexprDisjunction(pmp, pdrgpexpr);
	CExpression *pexprSelect = CUtils::PexprLogicalSelect(pmp, pexprJoin, pexprPredicate);
 	CExpression *pexprPreprocessed = CExpressionPreprocessor::PexprPreprocess(pmp, pexprSelect);

 	CWStringDynamic strSelect(pmp);
 	COstreamString oss(&strSelect);
 	pexprSelect->OsPrint(oss);
	CWStringConst strExpectedDebugPrintForSelect(GPOS_WSZ_LIT(
			"+--CLogicalSelect\n"
			"   |--CLogicalInnerJoin\n"
			"   |  |--CLogicalGet \"BaseTableAlias\" (\"BaseTable\"), Columns: [\"column_0000\" (3), \"column_0001\" (4), \"column_0002\" (5)] Key sets: {[0]}\n"
			"   |  |--CLogicalGet \"BaseTableAlias\" (\"BaseTable\"), Columns: [\"column_0000\" (0), \"column_0001\" (1), \"column_0002\" (2)] Key sets: {[0]}\n"
			"   |  +--CScalarCmp (=)\n"
			"   |     |--CScalarIdent \"column_0000\" (3)\n"
			"   |     +--CScalarIdent \"column_0000\" (0)\n"
			"   +--CScalarBoolOp (EboolopOr)\n"
			"      |--CScalarBoolOp (EboolopAnd)\n"
			"      |  |--CScalarCmp (=)\n"
			"      |  |  |--CScalarIdent \"column_0000\" (3)\n"
			"      |  |  +--CScalarConst (0)\n"
			"      |  +--CScalarCmp (=)\n"
			"      |     |--CScalarIdent \"column_0000\" (0)\n"
			"      |     +--CScalarConst (1)\n"
			"      |--CScalarBoolOp (EboolopAnd)\n"
			"      |  |--CScalarCmp (=)\n"
			"      |  |  |--CScalarIdent \"column_0001\" (4)\n"
			"      |  |  +--CScalarConst (0)\n"
			"      |  |--CScalarCmp (=)\n"
			"      |  |  |--CScalarIdent \"column_0002\" (5)\n"
			"      |  |  +--CScalarConst (1)\n"
			"      |  +--CScalarCmp (=)\n"
			"      |     |--CScalarIdent \"column_0000\" (0)\n"
			"      |     +--CScalarConst (2)\n"
			"      +--CScalarBoolOp (EboolopAnd)\n"
			"         |--CScalarCmp (=)\n"
			"         |  |--CScalarIdent \"column_0002\" (5)\n"
			"         |  +--CScalarConst (0)\n"
			"         +--CScalarCmp (=)\n"
			"            |--CScalarIdent \"column_0000\" (0)\n"
			"            +--CScalarConst (1)\n"));

	GPOS_ASSERT(strSelect.FEquals(&strExpectedDebugPrintForSelect));

	BOOL fEqual = strSelect.FEquals(&strExpectedDebugPrintForSelect);
	if (!fEqual)
	{
		CAutoTrace at(pmp);
		at.Os() << std::endl << "RETURNED EXPRESSION:" << std::endl << strSelect.Wsz();
		at.Os() << std::endl << "EXPECTED EXPRESSION:" << std::endl << strExpectedDebugPrintForSelect.Wsz();

		return GPOS_FAILED;
	}

 	CWStringDynamic strPreprocessed(pmp);
 	COstreamString ossPreprocessed(&strPreprocessed);
 	pexprPreprocessed->OsPrint(ossPreprocessed);
	CWStringConst strExpectedDebugPrintForPreprocessed(GPOS_WSZ_LIT(
			"+--CLogicalNAryJoin\n"
			"   |--CLogicalSelect\n"
			"   |  |--CLogicalGet \"BaseTableAlias\" (\"BaseTable\"), Columns: [\"column_0000\" (3), \"column_0001\" (4), \"column_0002\" (5)] Key sets: {[0]}\n"
			"   |  +--CScalarBoolOp (EboolopAnd)\n"
			"   |     |--CScalarBoolOp (EboolopOr)\n"
			"   |     |  |--CScalarCmp (=)\n"
			"   |     |  |  |--CScalarIdent \"column_0000\" (3)\n"
			"   |     |  |  +--CScalarConst (0)\n"
			"   |     |  |--CScalarBoolOp (EboolopAnd)\n"
			"   |     |  |  |--CScalarCmp (=)\n"
			"   |     |  |  |  |--CScalarIdent \"column_0001\" (4)\n"
			"   |     |  |  |  +--CScalarConst (0)\n"
			"   |     |  |  +--CScalarCmp (=)\n"
			"   |     |  |     |--CScalarIdent \"column_0002\" (5)\n"
			"   |     |  |     +--CScalarConst (1)\n"
			"   |     |  +--CScalarCmp (=)\n"
			"   |     |     |--CScalarIdent \"column_0002\" (5)\n"
			"   |     |     +--CScalarConst (0)\n"
			"   |     +--CScalarBoolOp (EboolopOr)\n"
			"   |        |--CScalarCmp (=)\n"
			"   |        |  |--CScalarIdent \"column_0000\" (3)\n"
			"   |        |  +--CScalarConst (1)\n"
			"   |        +--CScalarCmp (=)\n"
			"   |           |--CScalarIdent \"column_0000\" (3)\n"
			"   |           +--CScalarConst (2)\n"
			"   |--CLogicalSelect\n"
			"   |  |--CLogicalGet \"BaseTableAlias\" (\"BaseTable\"), Columns: [\"column_0000\" (0), \"column_0001\" (1), \"column_0002\" (2)] Key sets: {[0]}\n"
			"   |  +--CScalarBoolOp (EboolopOr)\n"
			"   |     |--CScalarCmp (=)\n"
			"   |     |  |--CScalarIdent \"column_0000\" (0)\n"
			"   |     |  +--CScalarConst (1)\n"
			"   |     +--CScalarCmp (=)\n"
			"   |        |--CScalarIdent \"column_0000\" (0)\n"
			"   |        +--CScalarConst (2)\n"
			"   +--CScalarBoolOp (EboolopAnd)\n"
			"      |--CScalarBoolOp (EboolopOr)\n"
			"      |  |--CScalarBoolOp (EboolopAnd)\n"
			"      |  |  |--CScalarCmp (=)\n"
			"      |  |  |  |--CScalarIdent \"column_0000\" (3)\n"
			"      |  |  |  +--CScalarConst (0)\n"
			"      |  |  +--CScalarCmp (=)\n"
			"      |  |     |--CScalarIdent \"column_0000\" (0)\n"
			"      |  |     +--CScalarConst (1)\n"
			"      |  |--CScalarBoolOp (EboolopAnd)\n"
			"      |  |  |--CScalarCmp (=)\n"
			"      |  |  |  |--CScalarIdent \"column_0001\" (4)\n"
			"      |  |  |  +--CScalarConst (0)\n"
			"      |  |  |--CScalarCmp (=)\n"
			"      |  |  |  |--CScalarIdent \"column_0002\" (5)\n"
			"      |  |  |  +--CScalarConst (1)\n"
			"      |  |  +--CScalarCmp (=)\n"
			"      |  |     |--CScalarIdent \"column_0000\" (0)\n"
			"      |  |     +--CScalarConst (2)\n"
			"      |  +--CScalarBoolOp (EboolopAnd)\n"
			"      |     |--CScalarCmp (=)\n"
			"      |     |  |--CScalarIdent \"column_0002\" (5)\n"
			"      |     |  +--CScalarConst (0)\n"
			"      |     +--CScalarCmp (=)\n"
			"      |        |--CScalarIdent \"column_0000\" (0)\n"
			"      |        +--CScalarConst (1)\n"
			"      +--CScalarCmp (=)\n"
			"         |--CScalarIdent \"column_0000\" (0)\n"
			"         +--CScalarIdent \"column_0000\" (3)\n"));
	pexprSelect->Release();
	pexprPreprocessed->Release();

	fEqual = strPreprocessed.FEquals(&strExpectedDebugPrintForPreprocessed);
	if (!fEqual)
	{
		CAutoTrace at(pmp);
		at.Os() << std::endl << "RETURNED EXPRESSION:" << std::endl << strPreprocessed.Wsz();
		at.Os() << std::endl << "EXPECTED EXPRESSION:" << std::endl << strExpectedDebugPrintForPreprocessed.Wsz();

		return GPOS_FAILED;
	}

	return GPOS_OK;
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresUnittest_PreProcessPrefiltersPartialPush
//
//	@doc:
//		Test extraction of prefilters out of disjunctive expressions where
//		not all conditions can be pushed.
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresUnittest_PreProcessOrPrefiltersPartialPush()
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	// reset metadata cache
	CMDCache::Reset();

	// setup a file-based provider
	CMDProviderMemory *pmdp = CTestUtils::m_pmdpf;
	pmdp->AddRef();
	CMDAccessor mda(pmp, CMDCache::Pcache(), CTestUtils::m_sysidDefault, pmdp);
	CAutoOptCtxt aoc(pmp, &mda, NULL /*pceeval*/, CTestUtils::Pcm(pmp));
	CExpression *pexprJoin = CTestUtils::PexprLogicalJoin<CLogicalInnerJoin>(pmp);

	CColRefSet *pcrsInner = CDrvdPropRelational::Pdprel((*pexprJoin)[1]->PdpDerive())->PcrsOutput();
	DrgPcr *pdrgpcrInner = pcrsInner->Pdrgpcr(pmp);
	GPOS_ASSERT(3 <= pdrgpcrInner->UlSafeLength());

	CColRefSet *pcrsOuter = CDrvdPropRelational::Pdprel((*pexprJoin)[0]->PdpDerive())->PcrsOutput();
	DrgPcr *pdrgpcrOuter = pcrsOuter->Pdrgpcr(pmp);
	GPOS_ASSERT(3 <= pdrgpcrOuter->UlSafeLength());

	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);

	// first disjunct has conditions on both tables that can be pushed
	DrgPcr *pdrgpcrDisjunct = GPOS_NEW(pmp) DrgPcr(pmp);
	CColRef *pcr0_0 = (*pdrgpcrOuter)[0];
	pdrgpcrDisjunct->Append(pcr0_0);
	CColRef *pcr1_0 = (*pdrgpcrInner)[0];
	pdrgpcrDisjunct->Append(pcr1_0);
	pdrgpexpr->Append(PexprCreateConjunction(pmp, pdrgpcrDisjunct));
	pdrgpcrDisjunct->Release();

	// second disjunct has only a condition on the inner branch
	pdrgpcrDisjunct = GPOS_NEW(pmp) DrgPcr(pmp);
	CColRef *pcr1_2 = (*pdrgpcrInner)[2];
	pdrgpcrDisjunct->Append(pcr1_2);
	pdrgpexpr->Append(PexprCreateConjunction(pmp, pdrgpcrDisjunct));
	pdrgpcrDisjunct->Release();

	pdrgpcrInner->Release();
	pdrgpcrOuter->Release();

	CExpression *pexprPredicate = CPredicateUtils::PexprDisjunction(pmp, pdrgpexpr);
	CExpression *pexprSelect = CUtils::PexprLogicalSelect(pmp, pexprJoin, pexprPredicate);
 	CExpression *pexprPreprocessed = CExpressionPreprocessor::PexprPreprocess(pmp, pexprSelect);

 	CWStringDynamic strSelect(pmp);
 	COstreamString oss(&strSelect);
 	pexprSelect->OsPrint(oss);
	CWStringConst strExpectedDebugPrintForSelect(GPOS_WSZ_LIT(
			"+--CLogicalSelect\n"
			"   |--CLogicalInnerJoin\n"
			"   |  |--CLogicalGet \"BaseTableAlias\" (\"BaseTable\"), Columns: [\"column_0000\" (3), \"column_0001\" (4), \"column_0002\" (5)] Key sets: {[0]}\n"
			"   |  |--CLogicalGet \"BaseTableAlias\" (\"BaseTable\"), Columns: [\"column_0000\" (0), \"column_0001\" (1), \"column_0002\" (2)] Key sets: {[0]}\n"
			"   |  +--CScalarCmp (=)\n"
			"   |     |--CScalarIdent \"column_0000\" (3)\n"
			"   |     +--CScalarIdent \"column_0000\" (0)\n"
			"   +--CScalarBoolOp (EboolopOr)\n"
			"      |--CScalarBoolOp (EboolopAnd)\n"
			"      |  |--CScalarCmp (=)\n"
			"      |  |  |--CScalarIdent \"column_0000\" (3)\n"
			"      |  |  +--CScalarConst (0)\n"
			"      |  +--CScalarCmp (=)\n"
			"      |     |--CScalarIdent \"column_0000\" (0)\n"
			"      |     +--CScalarConst (1)\n"
		    "      +--CScalarCmp (=)\n"
		    "         |--CScalarIdent \"column_0002\" (2)\n"
		    "         +--CScalarConst (0)\n"));

	GPOS_ASSERT(strSelect.FEquals(&strExpectedDebugPrintForSelect));

 	CWStringDynamic strPreprocessed(pmp);
 	COstreamString ossPreprocessed(&strPreprocessed);
 	pexprPreprocessed->OsPrint(ossPreprocessed);
	CWStringConst strExpectedDebugPrintForPreprocessed(GPOS_WSZ_LIT(
			"+--CLogicalNAryJoin\n"
			"   |--CLogicalGet \"BaseTableAlias\" (\"BaseTable\"), Columns: [\"column_0000\" (3), \"column_0001\" (4), \"column_0002\" (5)] Key sets: {[0]}\n"
			"   |--CLogicalSelect\n"
			"   |  |--CLogicalGet \"BaseTableAlias\" (\"BaseTable\"), Columns: [\"column_0000\" (0), \"column_0001\" (1), \"column_0002\" (2)] Key sets: {[0]}\n"
			"   |  +--CScalarBoolOp (EboolopOr)\n"
			"   |     |--CScalarCmp (=)\n"
			"   |     |  |--CScalarIdent \"column_0000\" (0)\n"
			"   |     |  +--CScalarConst (1)\n"
			"   |     +--CScalarCmp (=)\n"
			"   |        |--CScalarIdent \"column_0002\" (2)\n"
			"   |        +--CScalarConst (0)\n"
			"   +--CScalarBoolOp (EboolopAnd)\n"
			"      |--CScalarBoolOp (EboolopOr)\n"
			"      |  |--CScalarBoolOp (EboolopAnd)\n"
			"      |  |  |--CScalarCmp (=)\n"
			"      |  |  |  |--CScalarIdent \"column_0000\" (3)\n"
			"      |  |  |  +--CScalarConst (0)\n"
			"      |  |  +--CScalarCmp (=)\n"
			"      |  |     |--CScalarIdent \"column_0000\" (0)\n"
			"      |  |     +--CScalarConst (1)\n"
			"      |  +--CScalarCmp (=)\n"
			"      |     |--CScalarIdent \"column_0002\" (2)\n"
			"      |     +--CScalarConst (0)\n"
			"      +--CScalarCmp (=)\n"
			"         |--CScalarIdent \"column_0000\" (0)\n"
			"         +--CScalarIdent \"column_0000\" (3)\n"));


 	pexprSelect->Release();
 	pexprPreprocessed->Release();
	BOOL fEqual = strExpectedDebugPrintForPreprocessed.FEquals(&strPreprocessed);
	if (!fEqual)
	{
		CAutoTrace at(pmp);
		at.Os() << std::endl << "RETURNED EXPRESSION:" << std::endl << strPreprocessed.Wsz();
		at.Os() << std::endl << "EXPECTED EXPRESSION:" << std::endl << strExpectedDebugPrintForPreprocessed.Wsz();

		return GPOS_FAILED;
	}

	return GPOS_OK;
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresUnittest_CollapseInnerJoinHelper
//
//	@doc:
//		Helper function for testing collapse of Inner Joins
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresUnittest_CollapseInnerJoinHelper
	(
	IMemoryPool *pmp,
	COperator *popJoin,
	CExpression *rgpexpr[],
	CDrvdPropRelational *rgpdprel[]
	)
{
	GPOS_ASSERT(NULL != popJoin);

	// (1) generate two nested outer joins
	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	rgpexpr[0]->AddRef();
	pdrgpexpr->Append(rgpexpr[0]);
	rgpexpr[1]->AddRef();
	pdrgpexpr->Append(rgpexpr[1]);
	CTestUtils::EqualityPredicate(pmp, rgpdprel[0]->PcrsOutput(), rgpdprel[1]->PcrsOutput(), pdrgpexpr);
	CExpression *pexprLOJ1 = GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CLogicalLeftOuterJoin(pmp), pdrgpexpr);

	pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	rgpexpr[2]->AddRef();
	pdrgpexpr->Append(rgpexpr[2]);
	pdrgpexpr->Append(pexprLOJ1);
	CTestUtils::EqualityPredicate(pmp, rgpdprel[0]->PcrsOutput(), rgpdprel[2]->PcrsOutput(), pdrgpexpr);
	CExpression *pexprLOJ2 = GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CLogicalLeftOuterJoin(pmp), pdrgpexpr);

	// (2) add Inner/NAry Join on top of outer join
	pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	pdrgpexpr->Append(pexprLOJ2);
	rgpexpr[3]->AddRef();
	pdrgpexpr->Append(rgpexpr[3]);
	CTestUtils::EqualityPredicate(pmp, rgpdprel[0]->PcrsOutput(), rgpdprel[3]->PcrsOutput(), pdrgpexpr);
	popJoin->AddRef();
	CExpression *pexprJoin1 = GPOS_NEW(pmp) CExpression(pmp, popJoin, pdrgpexpr);

	// (3) add another Inner/NAry Join on top of Inner/NAry Join
	pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	pdrgpexpr->Append(pexprJoin1);
	rgpexpr[4]->AddRef();
	pdrgpexpr->Append(rgpexpr[4]);
	CTestUtils::EqualityPredicate(pmp, rgpdprel[0]->PcrsOutput(), rgpdprel[4]->PcrsOutput(), pdrgpexpr);
	popJoin->AddRef();
	CExpression *pexprJoin2 = GPOS_NEW(pmp) CExpression(pmp, popJoin, pdrgpexpr);

	// (4) add another Inner/NAry Join on top of Inner/NAry Join
	pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	pdrgpexpr->Append(pexprJoin2);
	rgpexpr[5]->AddRef();
	pdrgpexpr->Append(rgpexpr[5]);
	CTestUtils::EqualityPredicate(pmp, rgpdprel[0]->PcrsOutput(), rgpdprel[5]->PcrsOutput(), pdrgpexpr);
	popJoin->AddRef();
	CExpression *pexprJoin3 = GPOS_NEW(pmp) CExpression(pmp, popJoin, pdrgpexpr);

	// (5) create Select with predicate that can turn all outer joins into inner joins,
	// add the Select on top of the top Inner/NAry Join
	CExpression *pexprCmpLOJInner = CUtils::PexprScalarEqCmp(pmp, rgpdprel[1]->PcrsOutput()->PcrFirst(), CUtils::PexprScalarConstInt4(pmp, 1 /*fVal*/));
	CExpression *pexprSelect = CUtils::PexprSafeSelect(pmp, pexprJoin3, pexprCmpLOJInner);
	CExpression *pexprPreprocessed = CExpressionPreprocessor::PexprPreprocess(pmp, pexprSelect);
	{
		CAutoTrace at(pmp);
		at.Os() <<  std::endl << "EXPR:" << std::endl << *pexprSelect << std::endl;
		at.Os() << "No outer joins are expected after preprocessing:" << std::endl;
		at.Os() << "PREPROCESSED EXPR:" << std::endl << *pexprPreprocessed << std::endl;
	}
	GPOS_ASSERT(FHasNoOuterJoin(pexprPreprocessed) && "unexpected outer join");

	// assert the structure of resulting expression,
	// root must be NAryJoin operator,
	// selection predicate must be pushed below the NaryJoin,
	// no other deep join trees remain below the root NAryJoin
	GPOS_ASSERT(COperator::EopLogicalNAryJoin == pexprPreprocessed->Pop()->Eopid() &&
			"root operator is expected to be NAryJoin");

#ifdef GPOS_DEBUG
	const ULONG ulArity = pexprPreprocessed->UlArity();
	for (ULONG ul = 0; ul < ulArity - 1; ul++)
	{
		CExpression *pexprChild = (*pexprPreprocessed)[ul];
		GPOS_ASSERT(1 == CDrvdPropRelational::Pdprel(pexprChild->PdpDerive())->UlJoinDepth() &&
				"unexpected deep join tree below NAryJoin");

		COperator::EOperatorId eopid = pexprChild->Pop()->Eopid();
		GPOS_ASSERT((COperator::EopLogicalGet == eopid || COperator::EopLogicalSelect == eopid) &&
					"child operator is expected to be either Get or Select");
		GPOS_ASSERT_IMP(COperator::EopLogicalSelect == eopid, COperator::EopLogicalGet == (*pexprChild)[0]->Pop()->Eopid() &&
				"expected Select operator to be directly on top of Get operator");
	}
#endif // GPOS_DEBUG

	// cleanup
	pexprSelect->Release();
	pexprPreprocessed->Release();

	return GPOS_OK;
}

//---------------------------------------------------------------------------
//	@function:
//		CExpressionPreprocessorTest::EresUnittest_CollapseInnerJoin
//
//	@doc:
//		Test collapsing of Inner Joins
//
//---------------------------------------------------------------------------
GPOS_RESULT
CExpressionPreprocessorTest::EresUnittest_CollapseInnerJoin()
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	// array of relation names
	CWStringConst rgscRel[] =
	{
		GPOS_WSZ_LIT("Rel1"),
		GPOS_WSZ_LIT("Rel2"),
		GPOS_WSZ_LIT("Rel3"),
		GPOS_WSZ_LIT("Rel4"),
		GPOS_WSZ_LIT("Rel5"),
		GPOS_WSZ_LIT("Rel6"),

	};

	// array of relation IDs
	ULONG rgulRel[] =
	{
		GPOPT_TEST_REL_OID1,
		GPOPT_TEST_REL_OID2,
		GPOPT_TEST_REL_OID3,
		GPOPT_TEST_REL_OID4,
		GPOPT_TEST_REL_OID5,
		GPOPT_TEST_REL_OID6,
	};

	// reset metadata cache
	CMDCache::Reset();

	// setup a file-based provider
	CMDProviderMemory *pmdp = CTestUtils::m_pmdpf;
	pmdp->AddRef();
	CMDAccessor mda(pmp, CMDCache::Pcache(), CTestUtils::m_sysidDefault, pmdp);
	CAutoOptCtxt aoc(pmp, &mda, NULL /*pceeval*/, CTestUtils::Pcm(pmp));

	CExpression *rgpexpr[GPOS_ARRAY_SIZE(rgscRel)];
	CDrvdPropRelational *rgpdprel[GPOS_ARRAY_SIZE(rgscRel)];
	for (ULONG ul = 0; ul < GPOS_ARRAY_SIZE(rgscRel); ul++)
	{
		rgpexpr[ul] = CTestUtils::PexprLogicalGet(pmp, &rgscRel[ul], &rgscRel[ul], rgulRel[ul]);
		rgpdprel[ul] = CDrvdPropRelational::Pdprel(rgpexpr[ul]->PdpDerive());
	}

	// the following expression is used as input,
	// we also generate another variant with CLogicalInnerJoin instead of CLogicalNAryJoin

	//	+--CLogicalSelect
	//	   |--CLogicalNAryJoin
	//	   |  |--CLogicalNAryJoin
	//	   |  |  |--CLogicalNAryJoin
	//	   |  |  |  |--CLogicalLeftOuterJoin
	//	   |  |  |  |  |--CLogicalGet "Rel3" ("Rel3"), Columns: ["column_0000" (6), "column_0001" (7), "column_0002" (8)] Key sets: {[0]}
	//	   |  |  |  |  |--CLogicalLeftOuterJoin
	//	   |  |  |  |  |  |--CLogicalGet "Rel1" ("Rel1"), Columns: ["column_0000" (0), "column_0001" (1), "column_0002" (2)] Key sets: {[0]}
	//	   |  |  |  |  |  |--CLogicalGet "Rel2" ("Rel2"), Columns: ["column_0000" (3), "column_0001" (4), "column_0002" (5)] Key sets: {[0]}
	//	   |  |  |  |  |  +--CScalarCmp (=)
	//	   |  |  |  |  |     |--CScalarIdent "column_0000" (0)
	//	   |  |  |  |  |     +--CScalarIdent "column_0000" (3)
	//	   |  |  |  |  +--CScalarCmp (=)
	//	   |  |  |  |     |--CScalarIdent "column_0000" (0)
	//	   |  |  |  |     +--CScalarIdent "column_0000" (6)
	//	   |  |  |  |--CLogicalGet "Rel4" ("Rel4"), Columns: ["column_0000" (9), "column_0001" (10), "column_0002" (11)] Key sets: {[0]}
	//	   |  |  |  +--CScalarCmp (=)
	//	   |  |  |     |--CScalarIdent "column_0000" (0)
	//	   |  |  |     +--CScalarIdent "column_0000" (9)
	//	   |  |  |--CLogicalGet "Rel5" ("Rel5"), Columns: ["column_0000" (12), "column_0001" (13), "column_0002" (14)] Key sets: {[0]}
	//	   |  |  +--CScalarCmp (=)
	//	   |  |     |--CScalarIdent "column_0000" (0)
	//	   |  |     +--CScalarIdent "column_0000" (12)
	//	   |  |--CLogicalGet "Rel6" ("Rel6"), Columns: ["column_0000" (15), "column_0001" (16), "column_0002" (17)] Key sets: {[0]}
	//	   |  +--CScalarCmp (=)
	//	   |     |--CScalarIdent "column_0000" (0)
	//	   |     +--CScalarIdent "column_0000" (15)
	//	   +--CScalarCmp (=)
	//	      |--CScalarIdent "column_0000" (3)
	//	      +--CScalarConst (1)

	GPOS_RESULT eres = GPOS_OK;
	for (ULONG ulInput = 0; eres == GPOS_OK && ulInput < 2; ulInput++)
	{
		COperator *popJoin = NULL;
		if (0 == ulInput)
		{
			popJoin = GPOS_NEW(pmp) CLogicalNAryJoin(pmp);
		}
		else
		{
			popJoin = GPOS_NEW(pmp) CLogicalInnerJoin(pmp);
		}

		eres = EresUnittest_CollapseInnerJoinHelper(pmp, popJoin, rgpexpr, rgpdprel);
		popJoin->Release();
	}

	// cleanup input expressions
	for (ULONG ul = 0; ul < GPOS_ARRAY_SIZE(rgscRel); ul++)
	{
		rgpexpr[ul]->Release();
	}

	return GPOS_OK;
}

// EOF

