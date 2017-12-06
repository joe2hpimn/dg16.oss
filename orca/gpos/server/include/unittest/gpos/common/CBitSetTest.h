//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CBitSetTest.h
//
//	@doc:
//		Test for CBitSet
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CBitSetTest_H
#define GPOS_CBitSetTest_H

namespace gpos
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CBitSetTest
	//
	//	@doc:
	//		Static unit tests for bit set
	//
	//---------------------------------------------------------------------------
	class CBitSetTest
	{

		public:

			// unittests
			static GPOS_RESULT EresUnittest();
			static GPOS_RESULT EresUnittest_Basics();
			static GPOS_RESULT EresUnittest_Removal();
			static GPOS_RESULT EresUnittest_SetOps();
			static GPOS_RESULT EresUnittest_Performance();

	}; // class CBitSetTest
}

#endif // !GPOS_CBitSetTest_H

// EOF

