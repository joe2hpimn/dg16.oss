//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2008 - 2010 Greenplum, Inc.
//
//	@filename:
//		COstreamBasicTest.h
//
//	@doc:
//		Test for COstreamBasic
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_COstreamBasicTest_H
#define GPOS_COstreamBasicTest_H

#include "gpos/types.h"
#include "gpos/assert.h"

namespace gpos
{

	//---------------------------------------------------------------------------
	//	@class:
	//		COstreamBasicTest
	//
	//	@doc:
	//		Static unit tests for messages
	//
	//---------------------------------------------------------------------------
	class COstreamBasicTest
	{
		public:

			// unittests
			static GPOS_RESULT EresUnittest();
			static GPOS_RESULT EresUnittest_Basic();
			static GPOS_RESULT EresUnittest_Strings();
			static GPOS_RESULT EresUnittest_Numbers();
	};
}

#endif // !GPOS_COstreamBasicTest_H

// EOF

