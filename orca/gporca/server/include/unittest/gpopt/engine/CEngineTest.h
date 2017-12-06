//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 - 2011 EMC Corp.
//
//	@filename:
//		CEngineTest.h
//
//	@doc:
//		Test for CEngine
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CEngineTest_H
#define GPOPT_CEngineTest_H


#include "gpos/base.h"
#include "gpos/common/CDynamicPtrArray.h"

#include "gpopt/base/COptimizationContext.h"
#include "gpopt/search/CSearchStage.h"
#include "gpopt/operators/CExpression.h"

namespace gpopt
{

	//---------------------------------------------------------------------------
	//	@class:
	//		CEngineTest
	//
	//	@doc:
	//		Unittests
	//
	//---------------------------------------------------------------------------
	class CEngineTest
	{

		private:

#ifdef GPOS_DEBUG

			// type definition for of expression generator
			typedef CExpression *(*Pfpexpr)(IMemoryPool*);

			// helper for testing engine using an array of expression generators
			static
			GPOS_RESULT EresTestEngine(Pfpexpr rgpf[], ULONG ulSize);

#endif // GPOS_DEBUG

			// counter used to mark last successful test
			static ULONG m_ulTestCounter;

			// counter used to mark last successful test in subquery test
			static ULONG m_ulTestCounterSubq;

		public:

			// type definition of optimizer test function
			typedef void (FnOptimize)(IMemoryPool*, CExpression *, DrgPss *);

			// main driver
			static
			GPOS_RESULT EresUnittest();

			// basic unittest
			static
			GPOS_RESULT EresUnittest_Basic();

			// helper function for optimizing deep join trees
			static
			GPOS_RESULT EresOptimize
				(
				FnOptimize *pfopt, // optimization function
				CWStringConst *pstr, // array of relation names
				ULONG *pul,// array of relation OIDs
				ULONG ulRels, // number of array entries
				CBitSet *pbs // if a bit is set, the corresponding join expression will be optimized
				);

#ifdef GPOS_DEBUG

			// build memo by recursive optimization
			static
			void BuildMemoRecursive(IMemoryPool *pmp, CExpression *pexprInput, DrgPss *pdrgpss);

			// test of recursive memo building
			static
			GPOS_RESULT EresUnittest_BuildMemo();

			// test of appending stats during optimization
			static
			GPOS_RESULT EresUnittest_AppendStats();

			// test of recursive memo building with a large number of joins
			static
			GPOS_RESULT EresUnittest_BuildMemoLargeJoins();

			// test of building memo for expressions with subqueries
			static
			GPOS_RESULT EresUnittest_BuildMemoWithSubqueries();

			// test of building memo for expressions with TVFs
			static
			GPOS_RESULT EresUnittest_BuildMemoWithTVF();

			// test of building memo for expressions with grouping
			static
			GPOS_RESULT EresUnittest_BuildMemoWithGrouping();

			// test of building memo for expressions with partitioning
			static
			GPOS_RESULT EresUnittest_BuildMemoWithPartitioning();

			// test of building memo for expressions with partitioning
			static
			GPOS_RESULT EresUnittest_BuildMemoWithWindowing();

			// test of building memo for expressions with CTEs
			static
			GPOS_RESULT EresUnittest_BuildMemoWithCTE();

#endif // GPOS_DEBUG

	}; // class CEngineTest
}

#endif // !GPOPT_CEngineTest_H


// EOF
