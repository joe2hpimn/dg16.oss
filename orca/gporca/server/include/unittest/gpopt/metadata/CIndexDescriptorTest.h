//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CIndexDescriptorTest.h
//
//	@doc:
//      Test for CTableDescriptor
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CIndexDescriptorTest_H
#define GPOPT_CIndexDescriptorTest_H

#include "gpos/base.h"
#include "gpopt/metadata/CTableDescriptor.h"
#include "gpopt/metadata/CIndexDescriptor.h"

namespace gpopt
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CIndexDescriptorTest
	//
	//	@doc:
	//		Static unit tests
	//
	//---------------------------------------------------------------------------
	class CIndexDescriptorTest
	{

		public:

			// unittests
			static GPOS_RESULT EresUnittest();
			static GPOS_RESULT EresUnittest_Basic();
	}; // class CIndexDescriptorTest
}

#endif // !GPOPT_CIndexDescriptorTest_H

// EOF
