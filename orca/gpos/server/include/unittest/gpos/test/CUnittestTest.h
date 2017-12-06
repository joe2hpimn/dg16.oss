//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2015 Pivotal Software, Inc.
//
//	@filename:
//		CUnittestTest.h
//
//	@doc:
//		Test for CUnittest with subtests
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CUnittestTest_H
#define GPOS_CUnittestTest_H

#include "gpos/base.h"
#include "gpos/types.h"


namespace gpos
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CUnittestTest
	//
	//	@doc:
	//		Unit test with parameter denoting subtest
	//
	//---------------------------------------------------------------------------
	class CUnittestTest
	{
		public:

			// unittests
			static GPOS_RESULT EresSubtest(ULONG ulSubtest);

	}; // CAutoMutexTest
}

#endif // !GPOS_CUnittestTest_H

// EOF

