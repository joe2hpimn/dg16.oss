//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CTimerUser.h
//
//	@doc:
//		A timer which records elapsed user time;
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CTimerUser_H
#define GPOS_CTimerUser_H

#include "gpos/common/ITimer.h"

namespace gpos
{

	//---------------------------------------------------------------------------
	//	@class:
	//		CTimerUser
	//
	//	@doc:
	//		Records user time;
	//
	//---------------------------------------------------------------------------
	class CTimerUser : public ITimer
	{
		private:

			// actual timer
			RUSAGE m_rusage;

		public:

			// ctor
			CTimerUser()
			{
				Restart();
			}

			// retrieve elapsed user time in micro-seconds
			virtual
			ULONG UlElapsedUS() const;

			// restart timer
			virtual
			void Restart();

	}; // class CTimerUser
}

#endif // !GPOS_CTimerUser_H

// EOF

