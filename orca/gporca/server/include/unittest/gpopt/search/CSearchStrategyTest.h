//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CSearchStrategyTest.h
//
//	@doc:
//		Test for search strategy
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CSearchStrategyTest_H
#define GPOPT_CSearchStrategyTest_H

#include "gpos/base.h"


namespace gpopt
{

	//---------------------------------------------------------------------------
	//	@class:
	//		CSearchStrategyTest
	//
	//	@doc:
	//		unittest for search strategy
	//
	//---------------------------------------------------------------------------
	class CSearchStrategyTest
	{

		private:

			// type definition for of expression generator
			typedef CExpression *(*Pfpexpr)(IMemoryPool*);

			// type definition for of optimize function
			typedef void (*PfnOptimize)(IMemoryPool *, CExpression *, DrgPss *);

			// generate random search strategy
			static
			DrgPss *PdrgpssRandom(IMemoryPool *pmp);

			// run optimize function on given expression
			static
			void Optimize(IMemoryPool *pmp, Pfpexpr pfnGenerator, DrgPss *pdrgpss, PfnOptimize pfnOptimize);

		public:

			// unittests driver
			static
			GPOS_RESULT EresUnittest();

#ifdef GPOS_DEBUG
			// test search strategy with recursive optimization
			static
			GPOS_RESULT EresUnittest_RecursiveOptimize();
#endif // GPOS_DEBUG

			// test search strategy with multi-threaded optimization
			static
			GPOS_RESULT EresUnittest_MultiThreadedOptimize();

			// test reading search strategy from XML file
			static
			GPOS_RESULT EresUnittest_Parsing();

			// test search strategy that times out
			static
			GPOS_RESULT EresUnittest_Timeout();

			// test exception handling when parsing search strategy
			static
			GPOS_RESULT EresUnittest_ParsingWithException();

	}; // CSearchStrategyTest

}

#endif // !GPOPT_CSearchStrategyTest_H


// EOF
