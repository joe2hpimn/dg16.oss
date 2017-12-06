//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2013 EMC Corp.
//
//	@filename:
//		COptimizationJobsTest.h
//
//	@doc:
//		Test for jobs created during optimization
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_COptimizationJobsTest_H
#define GPOPT_COptimizationJobsTest_H

#include "gpos/base.h"


namespace gpopt
{

	//---------------------------------------------------------------------------
	//	@class:
	//		COptimizationJobsTest
	//
	//	@doc:
	//		unittest for optimization jobs
	//
	//---------------------------------------------------------------------------
	class COptimizationJobsTest
	{


		public:

			// unittests driver
			static
			GPOS_RESULT EresUnittest();

			// test of optimization jobs state machines
			static
			GPOS_RESULT EresUnittest_StateMachine();

	}; // COptimizationJobsTest

}

#endif // !GPOPT_COptimizationJobsTest_H


// EOF
