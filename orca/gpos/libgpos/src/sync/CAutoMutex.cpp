//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2010 Greenplum, Inc.
//
//	@filename:
//		CAutoMutex.cpp
//
//	@doc:
//		Auto wrapper around base mutex class; destructor unlocks mutex
//		if it acquired the lock previously;
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------


#include "gpos/sync/CAutoMutex.h"

using namespace gpos;


//---------------------------------------------------------------------------
//	@function:
//		CAutoMutex::~CAutoMutex
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CAutoMutex::~CAutoMutex()
{
	// release all locks
	while(m_cLock--)
	{
		m_mutex.Unlock();
	}
}

// EOF

