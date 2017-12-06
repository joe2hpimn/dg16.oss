//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CXformTest.h
//
//	@doc:
//		Test for CXForm
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformTest_H
#define GPOPT_CXformTest_H

#include "gpos/base.h"

namespace gpopt
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CXformTest
	//
	//	@doc:
	//		Unittests
	//
	//---------------------------------------------------------------------------
	class CXformTest
	{
		private:

			// path to metadata test file
			static const CHAR* m_szMDFilePath;

			// accessor to metadata cache
			static CMDAccessor *m_pmda;

			// generate a random join tree
			static
			CExpression *PexprJoinTree(IMemoryPool *pmp);
			
			// generate random star join tree
			static
			CExpression *PexprStarJoinTree(IMemoryPool *pmp, ULONG ulTabs);

			// application of different xforms for the given expression
			static
			void ApplyExprXforms(IMemoryPool *pmp, IOstream &os, CExpression *pexpr);

		public:

			// test driver
			static GPOS_RESULT EresUnittest();

			// test application of different xforms
			static GPOS_RESULT EresUnittest_ApplyXforms();

			// test application of cte-related xforms
			static GPOS_RESULT EresUnittest_ApplyXforms_CTE();

#ifdef GPOS_DEBUG
			// test name -> xform mapping
			static GPOS_RESULT EresUnittest_Mapping();
#endif // GPOS_DEBUG

	}; // class CXformTest
}

#endif // !GPOPT_CXformTest_H

// EOF
