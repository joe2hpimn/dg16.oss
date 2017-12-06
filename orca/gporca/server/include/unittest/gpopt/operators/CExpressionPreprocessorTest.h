//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CExpressionPreprocessorTest.h
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
#ifndef GPOPT_CExpressionPreprocessorTest_H
#define GPOPT_CExpressionPreprocessorTest_H

#include "gpos/base.h"

namespace gpopt
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CExpressionPreprocessorTest
	//
	//	@doc:
	//		Unittests
	//
	//---------------------------------------------------------------------------
	class CExpressionPreprocessorTest
	{
		private:

			// shorthand for functions for generating the expression for unnest test
			typedef CExpression *(FnPexprUnnestTestCase)(IMemoryPool *pmp, CExpression *pexpr);

			// unnest scalar subqueries test cases
			struct SUnnestSubqueriesTestCase
			{
				// generator function of first expression
				FnPexprUnnestTestCase *m_pfFst;

				// generator function of second expression
				FnPexprUnnestTestCase *m_pfSnd;

				// AND/OR boolean operator
				CScalarBoolOp::EBoolOperator m_eboolop;

				// operator that should be present after unnesting
				COperator::EOperatorId m_eopidPresent;

				// negate the children of or/and
				BOOL m_fNegateChildren;
			}; // SUnnestSubqueriesTestCase

			// count number of scalar subqueries
			static
			ULONG UlScalarSubqs(CExpression *pexpr);

			// check if a given expression has a subquery exists node
			static
			BOOL FHasSubqueryExists(CExpression *pexpr);

			// check if a given expression has a subquery not exitst node
			static
			BOOL FHasSubqueryNotExists(CExpression *pexpr);

			// check if a given expression has an ALL subquery
			static
			BOOL FHasSubqueryAll(CExpression *pexpr);

			// check if a given expression has an ANY subquery
			static
			BOOL FHasSubqueryAny(CExpression *pexpr);

			// check the type of the subquery
			static
			GPOS_RESULT EresCheckSubqueryType(CExpression *pexpr, COperator::EOperatorId eopidPresent);

			static
			// check the type of the existential subquery
			GPOS_RESULT EresCheckExistsSubqueryType(CExpression *pexpr, COperator::EOperatorId eopidPresent);

			// check the type of the quantified subquery
			static
			GPOS_RESULT EresCheckQuantifiedSubqueryType(CExpression *pexpr, COperator::EOperatorId eopidPresent);

#ifdef GPOS_DEBUG
			// check if a given expression has no Outer Join nodes
			static
			BOOL FHasNoOuterJoin(CExpression *pexpr);

			// check if a given expression has outer references in any node
			static
			BOOL FHasOuterRefs(CExpression *pexpr);

			// check if a given expression has Sequence Project nodes
			static
			BOOL FHasSeqPrj(CExpression *pexpr);

			// check if a given expression has IS DISTINCT FROM nodes
			static
			BOOL FHasIDF(CExpression *pexpr);

#endif // GPOS_DEBUG

			// create a conjunction of comparisons using the given columns
			static CExpression *PexprCreateConjunction(IMemoryPool *pmp, DrgPcr *pdrgpcr);

			// Helper for preprocessing window functions with outer references
			static
			void PreprocessWinFuncWithOuterRefs(const CHAR *szFilePath, BOOL fAllowWinFuncOuterRefs);

			// Helper for preprocessing window functions with distinct aggs
			static
			void PreprocessWinFuncWithDistinctAggs(IMemoryPool *pmp, const CHAR *szFilePath, BOOL fAllowSeqPrj, BOOL fAllowIDF);

			// Helper for preprocessing outer joins
			static
			void PreprocessOuterJoin(const CHAR *szFilePath, BOOL fAllowOuterJoin);

			// helper function for testing collapse of Inner Joins
			static
			GPOS_RESULT EresUnittest_CollapseInnerJoinHelper
				(
				IMemoryPool *pmp,
				COperator *popJoin,
				CExpression *rgpexpr[],
				CDrvdPropRelational *rgpdprel[]
				);

			// helper function for testing cascaded inner/outer joins
			static
			CExpression *PexprJoinHelper
				(
				IMemoryPool *pmp,
				CExpression *pexprLOJ,
				BOOL fCascadedLOJ,
				BOOL fIntermediateInnerjoin
				);

			// helper function for testing window functions with outer join
			static
			CExpression *PexprWindowFuncWithLOJHelper
				(
				IMemoryPool *pmp,
				CExpression *pexprLOJ,
				CColRef *pcrPartitionBy,
				BOOL fAddWindowFunction,
				BOOL fOuterChildPred,
				BOOL fCascadedLOJ,
				BOOL fPredBelowWindow
				);

			// helper function for testing Select with outer join
			static
			CExpression *PexprSelectWithLOJHelper
				(
				IMemoryPool *pmp,
				CExpression *pexprLOJ,
				CColRef *pcr,
				BOOL fOuterChildPred,
				BOOL fCascadedLOJ
				);

			// helper function for comparing expressions resulting from preprocessing window functions with outer join
			static
			GPOS_RESULT EresCompareExpressions(IMemoryPool *pmp, CWStringDynamic *rgstr[], ULONG ulSize);

			// test case generator for outer joins
			static
			GPOS_RESULT EresTestLOJ(BOOL fAddWindowFunction);

		public:

			// unittests
			static GPOS_RESULT EresUnittest();
			static GPOS_RESULT EresUnittest_PreProcess();
			static GPOS_RESULT EresUnittest_InferPredsOnLOJ();
			static GPOS_RESULT EresUnittest_PreProcessWindowFunc();
			static GPOS_RESULT EresUnittest_PreProcessWindowFuncWithLOJ();
			static GPOS_RESULT EresUnittest_PreProcessWindowFuncWithOuterRefs();
			static GPOS_RESULT EresUnittest_PreProcessWindowFuncWithDistinctAggs();
			static GPOS_RESULT EresUnittest_PreProcessNestedScalarSubqueries();
			static GPOS_RESULT EresUnittest_UnnestSubqueries();
			static GPOS_RESULT EresUnittest_PreProcessOuterJoin();
			static GPOS_RESULT EresUnittest_PreProcessOuterJoinMinidumps();
			static GPOS_RESULT EresUnittest_PreProcessOrPrefilters();
			static GPOS_RESULT EresUnittest_PreProcessOrPrefiltersPartialPush();
			static GPOS_RESULT EresUnittest_CollapseInnerJoin();

	}; // class CExpressionPreprocessorTest
}

#endif // !GPOPT_CExpressionPreprocessorTest_H

// EOF
