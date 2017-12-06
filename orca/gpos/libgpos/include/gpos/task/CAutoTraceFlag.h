//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2008 Greenplum, Inc.
//
//	@filename: 
//		CAutoTraceFlag.h
//
//	@doc:
//		Auto wrapper to set/reset a traceflag for a scope
//
//	@owner: 
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CAutoTraceFlag_H
#define GPOS_CAutoTraceFlag_H

#include "gpos/base.h"
#include "gpos/task/ITask.h"
#include "gpos/task/traceflags.h"
#include "gpos/common/CStackObject.h"


namespace gpos
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CAutoTraceFlag
	//
	//	@doc:
	//		Auto wrapper;
	//
	//---------------------------------------------------------------------------
	class CAutoTraceFlag : public CStackObject
	{
		private:

			// traceflag id
			ULONG m_ulTrace;

			// original value
			BOOL m_fOrig;

			// no copy ctor
			CAutoTraceFlag(const CAutoTraceFlag&);
			
		public:
		
			// ctor
			CAutoTraceFlag(ULONG ulTrace, BOOL fVal);

			// dtor
			virtual
			~CAutoTraceFlag ();

	}; // class CAutoTraceFlag
	
}


#endif // !GPOS_CAutoTraceFlag_H

// EOF

