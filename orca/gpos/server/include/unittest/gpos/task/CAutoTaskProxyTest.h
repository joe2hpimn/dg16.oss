//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2010 Greenplum, Inc.
//
//	@filename:
//		CAutoTaskProxyTest.h
//
//	@doc:
//		Test for CAutoTaskProxy
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CAutoTaskProxyTest_H
#define GPOS_CAutoTaskProxyTest_H

#include "gpos/task/CAutoTaskProxy.h"

namespace gpos
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CAutoTaskProxyTest
	//
	//	@doc:
	//		Unit tests for ATP class
	//
	//---------------------------------------------------------------------------
	class CAutoTaskProxyTest
	{
		public:
			enum EWaitType
			{
				EwtInvalid = 0,

				EwtWait,
				EwtWaitAny,
				EwtTimedWait,
				EwtTimedWaitAny,
				EwtDestroy,

				EwtSentinel
			};

			// unittests
			static GPOS_RESULT EresUnittest();
			static GPOS_RESULT EresUnittest_Wait();
			static GPOS_RESULT EresUnittest_WaitAny();
			static GPOS_RESULT EresUnittest_TimedWait();
			static GPOS_RESULT EresUnittest_TimedWaitAny();
			static GPOS_RESULT EresUnittest_Destroy();
			static GPOS_RESULT EresUnittest_PropagateCancelError();
			static GPOS_RESULT EresUnittest_PropagateExecError();
			static GPOS_RESULT EresUnittest_CheckErrorPropagation();

			// propagate error with/without cancel by specific value
			// need to access the private method of CTask
			static void Unittest_PropagateErrorInternal(void *(*)(void*), BOOL);

			// execute *wait* functions with/without cancel
			static void Unittest_ExecuteWaitFunc(CAutoTaskProxy &, CTask *, BOOL, EWaitType);

			static void *PvUnittest_Short(void *);
			static void *PvUnittest_Long(void *);
			static void *PvUnittest_Infinite(void *);
			static void *PvUnittest_Error(void *);
			static void PvUnittest_CheckAborted(CTask *ptsk);

	}; // CAutoTaskProxyTest
}

#endif // !GPOS_CAutoTaskProxyTest_H

// EOF

