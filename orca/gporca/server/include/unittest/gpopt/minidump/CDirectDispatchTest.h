//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2015 Pivotal, Inc.
//
//	@filename:
//		CDirectDispatchTest.h
//
//	@doc:
//		Test for direct dispatch
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CDirectDispatchTest_H
#define GPOPT_CDirectDispatchTest_H

#include "gpos/base.h"

namespace gpopt
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CDirectDispatchTest
	//
	//	@doc:
	//		Unittests
	//
	//---------------------------------------------------------------------------
	class CDirectDispatchTest
	{
		private:

			// counter used to mark last successful test
			static
			ULONG m_ulDirectDispatchCounter;

		public:

			// unittests
			static
			GPOS_RESULT EresUnittest();

			static
			GPOS_RESULT EresUnittest_RunTests();

	}; // class CDirectDispatchTest
}

#endif // !GPOPT_CDirectDispatchTest_H

// EOF

