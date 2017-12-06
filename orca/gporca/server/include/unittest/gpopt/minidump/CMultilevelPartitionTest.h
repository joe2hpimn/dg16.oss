//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2015 Pivotal, Inc.
//
//	@filename:
//		CMultilevelPartitionTest.h
//
//	@doc:
//		Test for optimizing queries on multilevel partitioned tables
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CMultilevelPartitionTest_H
#define GPOPT_CMultilevelPartitionTest_H

#include "gpos/base.h"

namespace gpopt
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CMultilevelPartitionTest
	//
	//	@doc:
	//		Unittests
	//
	//---------------------------------------------------------------------------
	class CMultilevelPartitionTest
	{
		private:

			// counter used to mark last successful test
			static
			ULONG m_ulMLPTTestCounter;

		public:

			// unittests
			static
			GPOS_RESULT EresUnittest();

			static
			GPOS_RESULT EresUnittest_RunTests();

	}; // class CMultilevelPartitionTest
}

#endif // !GPOPT_CMultilevelPartitionTest_H

// EOF

