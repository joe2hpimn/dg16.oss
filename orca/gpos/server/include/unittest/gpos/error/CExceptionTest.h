//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2008 Greenplum, Inc.
//
//	@filename:
//		CExceptionTest.h
//
//	@doc:
//		Test for CException
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CExceptionTest_H
#define GPOS_CExceptionTest_H

#include "gpos/types.h"
#include "gpos/assert.h"

namespace gpos
{

	//---------------------------------------------------------------------------
	//	@class:
	//		CExceptionTest
	//
	//	@doc:
	//		Static unit tests for exceptions
	//
	//---------------------------------------------------------------------------
	class CExceptionTest
	{
		public:

			// unittests
			static GPOS_RESULT EresUnittest();

			static GPOS_RESULT EresUnittest_BasicThrow();
			static GPOS_RESULT EresUnittest_BasicRethrow();
			static GPOS_RESULT EresUnittest_StackOverflow();
			static GPOS_RESULT EresUnittest_AdditionOverflow();
			static GPOS_RESULT EresUnittest_MultiplicationOverflow();

#ifdef GPOS_DEBUG
			static GPOS_RESULT EresUnittest_Assert();
			static GPOS_RESULT EresUnittest_AssertImp();
			static GPOS_RESULT EresUnittest_AssertIffLHS();
			static GPOS_RESULT EresUnittest_AssertIffRHS();
#endif // GPOS_DEBUG

	};
}

#endif // !GPOS_CExceptionTest_H

// EOF

