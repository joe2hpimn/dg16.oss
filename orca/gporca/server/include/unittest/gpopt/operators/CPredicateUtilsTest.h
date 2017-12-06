//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CPredicateUtilsTest.h
//
//	@doc:
//		Test for predicate utilities
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CPredicateUtilsTest_H
#define GPOPT_CPredicateUtilsTest_H

#include "gpos/base.h"

namespace gpopt
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CPredicateUtilsTest
	//
	//	@doc:
	//		Unittests
	//
	//---------------------------------------------------------------------------
	class CPredicateUtilsTest
	{

		public:

			// unittests
			static GPOS_RESULT EresUnittest();
			static GPOS_RESULT EresUnittest_Conjunctions();
			static GPOS_RESULT EresUnittest_Disjunctions();
			static GPOS_RESULT EresUnittest_PlainEqualities();
			static GPOS_RESULT EresUnittest_Implication();


	}; // class CPredicateUtilsTest
}

#endif // !GPOPT_CPredicateUtilsTest_H

// EOF
