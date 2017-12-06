//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CDoubleTest.h
//
//	@doc:
//		Tests for the floating-point wrapper class.
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CDoubleTest_H
#define GPOS_CDoubleTest_H

namespace gpos
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CDoubleTest
	//
	//	@doc:
	//		Unittests for floating-point class
	//
	//---------------------------------------------------------------------------
	class CDoubleTest
	{

		public:

			// unittests
			static GPOS_RESULT EresUnittest();
			static GPOS_RESULT EresUnittest_Arithmetic();
			static GPOS_RESULT EresUnittest_Bool();
			static GPOS_RESULT EresUnittest_Convert();
			static GPOS_RESULT EresUnittest_Limits();

	}; // class CDoubleTest
}

#endif // !GPOS_CDoubleTest_H

// EOF

