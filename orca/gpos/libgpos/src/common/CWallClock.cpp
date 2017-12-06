//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp
//
//	@filename:
//		CWallClock.cpp
//
//	@doc:
//		Implementation of wall clock timer
//
//	@owner: 
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/base.h"
#include "gpos/common/syslibwrapper.h"
#include "gpos/common/CWallClock.h"

using namespace gpos;


//---------------------------------------------------------------------------
//	@function:
//		CWallClock::UlElapsedUS
//
//	@doc:
//		Wall-clock time in micro-seconds since object construction
//
//---------------------------------------------------------------------------
ULONG
CWallClock::UlElapsedUS() const
{
	timeval time;
	syslib::GetTimeOfDay(&time, NULL/*timezone*/);

	ULONG ulDiff = (ULONG)
		(((time.tv_sec - m_time.tv_sec) * GPOS_USEC_IN_SEC) +
		 (time.tv_usec - m_time.tv_usec));

	return ulDiff;
}


//---------------------------------------------------------------------------
//	@function:
//		CWallClock::Restart
//
//	@doc:
//		Restart timer
//
//---------------------------------------------------------------------------
void
CWallClock::Restart()
{
	syslib::GetTimeOfDay(&m_time, NULL/*timezone*/);
}


// EOF

