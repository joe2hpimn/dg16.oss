//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CAutoTimer.h
//
//	@doc:
//		A timer which records wall-time between construction and destruction;
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CAutoTimer_H
#define GPOS_CAutoTimer_H

#include "gpos/common/CStackObject.h"
#include "gpos/common/CWallClock.h"


namespace gpos
{

	//---------------------------------------------------------------------------
	//	@class:
	//		CAutoTimer
	//
	//	@doc:
    //		Wrapper around timer object; prints elapsed time when going out of
	//		scope as indicated (ctor argument); 
	//
	//---------------------------------------------------------------------------
	class CAutoTimer : public CStackObject
	{

		private:
			
			// actual timer
			CWallClock m_clock;
			
			// label for timer output
			const CHAR *m_sz;
			
			// trigger printing at destruction time
			BOOL m_fPrint;
		
			// private copy ctor
			CAutoTimer(const CAutoTimer &);
	
		public:

            // ctor
            CAutoTimer(const CHAR *sz, BOOL fPrint);

			// dtor
            ~CAutoTimer() throw();

	}; // class CAutoTimer
}

#endif // !GPOS_CAutoTimer_H

// EOF

