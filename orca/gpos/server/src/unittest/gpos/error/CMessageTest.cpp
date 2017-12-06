//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2008 - 2010 Greenplum, Inc.
//
//	@filename:
//		CMessageTest.cpp
//
//	@doc:
//		Tests for CMessage
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/assert.h"
#include "gpos/error/CMessage.h"
#include "gpos/memory/CAutoMemoryPool.h"
#include "gpos/test/CUnittest.h"

#include "unittest/gpos/error/CMessageTest.h"


using namespace gpos;

//---------------------------------------------------------------------------
//	@function:
//		CMessageTest::EresUnittest
//
//	@doc:
//		Function for raising assert exceptions; again, encapsulated in a function
//		to facilitate debugging
//
//---------------------------------------------------------------------------
GPOS_RESULT
CMessageTest::EresUnittest()
{
	CUnittest rgut[] =
		{
		GPOS_UNITTEST_FUNC(CMessageTest::EresUnittest_BasicWrapper),
		};

	return CUnittest::EresExecute(rgut, GPOS_ARRAY_SIZE(rgut));
}


//---------------------------------------------------------------------------
//	@function:
//		CMessageTest::EresUnittest_BasicWrapper
//
//	@doc:
//		Wrapper around basic test to provide va_list arguments
//
//---------------------------------------------------------------------------
GPOS_RESULT
CMessageTest::EresUnittest_BasicWrapper()
{
	// create memory pool of 128KB
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	return EresUnittest_Basic(pmp,
				// parameters to Assert message
				__FILE__, __LINE__, GPOS_WSZ_LIT("!true"));
}


//---------------------------------------------------------------------------
//	@function:
//		CMessageTest::EresUnittest_Basic
//
//	@doc:
//
//---------------------------------------------------------------------------
GPOS_RESULT
CMessageTest::EresUnittest_Basic
	(
	const void *pv,
	...
	)
{	
	const ULONG ulSize = 2048;
	
	IMemoryPool *pmp = (IMemoryPool*)pv;

	// take pre-defined assertion exc message
	CMessage *pmsg = CMessage::Pmsg(CException::ExmiAssert);
	
	GPOS_ASSERT(GPOS_MATCH_EX(pmsg->m_exc,
							  CException::ExmaSystem, 
							  CException::ExmiAssert));
		
	// target buffer for format test
	WCHAR *wsz = GPOS_NEW_ARRAY(pmp, WCHAR, ulSize);
	CWStringStatic wss(wsz, ulSize);

	VA_LIST vl;
	VA_START(vl, pv);
	
	// manufacture an OOM message (no additional parameters)
	pmsg->Format(&wss, vl);
	
	VA_END(vl);
	
	GPOS_TRACE(wsz);
	
	GPOS_DELETE_ARRAY(wsz);
	
	return GPOS_OK;
}

// EOF

