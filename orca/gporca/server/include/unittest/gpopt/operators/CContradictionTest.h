//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CContradictionTest.h
//
//	@doc:
//		Test for contradiction detection
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CContradictionTest_H
#define GPOPT_CContradictionTest_H

#include "gpos/base.h"

namespace gpopt
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CContradictionTest
	//
	//	@doc:
	//		Unittests
	//
	//---------------------------------------------------------------------------
	class CContradictionTest
	{
		public:
			// unittests
			static GPOS_RESULT EresUnittest();
			static GPOS_RESULT EresUnittest_Constraint();

	}; // class CContradictionTest
}

#endif // !GPOPT_CContradictionTest_H

// EOF
