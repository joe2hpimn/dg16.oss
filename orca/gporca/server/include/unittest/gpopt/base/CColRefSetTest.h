//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CColRefSetTest.h
//
//	@doc:
//	    Test for CColRefSet
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CColRefSetTest_H
#define GPOS_CColRefSetTest_H

#include "gpos/base.h"

namespace gpopt
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CColRefSetTest
	//
	//	@doc:
	//		Static unit tests for column reference set
	//
	//---------------------------------------------------------------------------
	class CColRefSetTest
	{

		public:

			// unittests
			static GPOS_RESULT EresUnittest();
			static GPOS_RESULT EresUnittest_Basics();

	}; // class CColRefSetTest
}

#endif // !GPOS_CColRefSetTest_H


// EOF
