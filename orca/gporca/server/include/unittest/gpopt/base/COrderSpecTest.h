//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		COrderSpecTest.h
//
//	@doc:
//		Test for order spec
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_COrderSpecTest_H
#define GPOS_COrderSpecTest_H

namespace gpopt
{

	//---------------------------------------------------------------------------
	//	@class:
	//		COrderSpecTest
	//
	//	@doc:
	//		Static unit tests for order specs
	//
	//---------------------------------------------------------------------------
	class COrderSpecTest
	{

		public:

			// unittests
			static GPOS_RESULT EresUnittest();
			static GPOS_RESULT EresUnittest_Basics();

	}; // class COrderSpecTest
}

#endif // !GPOS_COrderSpecTest_H


// EOF
