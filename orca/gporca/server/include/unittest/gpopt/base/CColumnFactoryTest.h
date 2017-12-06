//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2008, 2009 Greenplum, Inc.
//
//	@filename:
//		CColumnFactoryTest.h
//
//	@doc:
//		Test for CColumnFactory
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CColumnFactoryTest_H
#define GPOPT_CColumnFactoryTest_H

#include "gpos/base.h"

namespace gpopt
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CColumnFactoryTest
	//
	//	@doc:
	//		unittests
	//
	//---------------------------------------------------------------------------
	class CColumnFactoryTest
	{

		public:

			// actual unittests
			static GPOS_RESULT EresUnittest();
			static GPOS_RESULT EresUnittest_Basic();
	};
}

#endif // !GPOPT_CColumnFactoryTest_H

// EOF
