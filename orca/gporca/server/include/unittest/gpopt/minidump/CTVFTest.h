//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2015 Pivotal, Inc.
//
//	@filename:
//		CTVFTest.h
//
//	@doc:
//		Test for optimizing queries with TVF
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CTVFTest_H
#define GPOPT_CTVFTest_H

#include "gpos/base.h"

namespace gpopt
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CTVFTest
	//
	//	@doc:
	//		Unittests
	//
	//---------------------------------------------------------------------------
	class CTVFTest
	{
		private:

			// counter used to mark last successful test
			static
			ULONG m_ulTVFTestCounter;

		public:

			// unittests
			static
			GPOS_RESULT EresUnittest();

			static
			GPOS_RESULT EresUnittest_RunTests();

	}; // class CTVFTest
}

#endif // !GPOPT_CTVFTest_H

// EOF

