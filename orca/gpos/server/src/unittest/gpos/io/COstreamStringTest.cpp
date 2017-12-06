//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2008 - 2010 Greenplum, Inc.
//
//	@filename:
//		COstreamStringTest.cpp
//
//	@doc:
//		Tests for COstreamString
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/assert.h"
#include "gpos/error/CMessage.h"
#include "gpos/io/COstreamString.h"
#include "gpos/memory/CAutoMemoryPool.h"
#include "gpos/string/CWStringDynamic.h"
#include "gpos/string/CWStringConst.h"
#include "gpos/test/CUnittest.h"

#include "unittest/gpos/io/COstreamStringTest.h"

using namespace gpos;

//---------------------------------------------------------------------------
//	@function:
//		COstreamStringTest::EresUnittest
//
//	@doc:
//		Function for raising assert exceptions; again, encapsulated in a function
//		to facilitate debugging
//
//---------------------------------------------------------------------------
GPOS_RESULT
COstreamStringTest::EresUnittest()
{

	CUnittest rgut[] =
		{
		GPOS_UNITTEST_FUNC(COstreamStringTest::EresUnittest_Basic),
		GPOS_UNITTEST_FUNC_THROW
			(
			COstreamStringTest::EresUnittest_OOM,
			CException::ExmaSystem,
			CException::ExmiOOM
			),
#if defined(GPOS_DEBUG) && defined(__GLIBCXX__)
		GPOS_UNITTEST_FUNC_ASSERT(COstreamStringTest::EresUnittest_EndlAssert),
#endif
		};

	return CUnittest::EresExecute(rgut, GPOS_ARRAY_SIZE(rgut));
}


//---------------------------------------------------------------------------
//	@function:
//		COstreamStringTest::EresUnittest_Basic
//
//	@doc:
//		test for non-string/non-number types;
//
//---------------------------------------------------------------------------
GPOS_RESULT
COstreamStringTest::EresUnittest_Basic()
{
	// create memory pool of 128KB
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();
	
	CWStringDynamic str(pmp);
	
	// define basic stream over wide char out stream
	COstreamString osb(&str);

	// non-string, non-number types
	WCHAR wc = 'W';
	CHAR c = 'C';
	ULONG ul = 102;
	INT i = -10;
	WCHAR wsz[] = GPOS_WSZ_LIT("some regular string");	
	INT hex = 0xdeadbeef;
	
	osb 
		<< wc 
		<< c 
		<< ul
		<< i 
		<< wsz 
		<< COstream::EsmHex 
		<< hex
		;
	
	CWStringConst sexp(GPOS_WSZ_LIT("WC102-10some regular stringdeadbeef"));
		
	GPOS_ASSERT(str.FEquals(&sexp) && "Constructed string does not match expected output");
	
	return GPOS_OK;
}

//---------------------------------------------------------------------------
//	@function:
//		COstreamStringTest::EresOOM
//
//	@doc:
//		Out of memory test
//
//---------------------------------------------------------------------------
GPOS_RESULT
COstreamStringTest::EresUnittest_OOM()
{
	// create memory pool
	CAutoMemoryPool amp
		(
		CAutoMemoryPool::ElcExc,
		CMemoryPoolManager::EatSlab,
		false /*fThreadSafe*/,
		1024
		);
	IMemoryPool *pmp = amp.Pmp();

	CWStringDynamic str(pmp);
	
	// define basic stream over wide char out stream
	COstreamString osb(&str);
	
	// we should throw at some point in the loop
	for (int i=0;i<1024;i++)
	{
		osb << 'F' << std::endl;
	}
	
	return GPOS_FAILED;
}

#if defined(GPOS_DEBUG) && defined(__GLIBCXX__)
//---------------------------------------------------------------------------
//	@function:
//		COstreamStringTest::EresUnittest_EndlAssert
//
//	@doc:
//		Only allow std::endl
//
//---------------------------------------------------------------------------
GPOS_RESULT
COstreamStringTest::EresUnittest_EndlAssert()
{
	// create memory pool of 1KB
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	CWStringDynamic str(pmp);
	
	// define basic stream over wide char out stream
	COstreamString osb(&str);
	
	osb << std::ends;
	
	return GPOS_FAILED;
}
#endif

// EOF

