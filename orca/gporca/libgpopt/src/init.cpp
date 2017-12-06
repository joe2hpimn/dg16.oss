//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 Greenplum, Inc.
//
//	@filename:
//		init.cpp
//
//	@doc:
//		Implementation of initialization and termination functions for
//		libgpopt.
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/memory/CAutoMemoryPool.h"
#include "gpos/task/CWorker.h"

#include "gpopt/init.h"
#include "gpopt/mdcache/CMDCache.h"
#include "gpopt/exception.h"
#include "gpopt/xforms/CXformFactory.h"

using namespace gpos;
using namespace gpopt;

static IMemoryPool *pmp = NULL;


//---------------------------------------------------------------------------
//      @function:
//              gpopt_init
//
//      @doc:
//              Initialize gpopt library
//
//---------------------------------------------------------------------------
void __attribute__((constructor)) gpopt_init()
{
	{
		CAutoMemoryPool amp;
		pmp = amp.Pmp();

		// add standard exception messages
		(void) gpopt::EresExceptionInit(pmp);
	
		// detach safety
		(void) amp.PmpDetach();
	}

	if (GPOS_OK != gpopt::CXformFactory::EresInit())
	{
		return;
	}
}

//---------------------------------------------------------------------------
//      @function:
//              gpopt_terminate
//
//      @doc:
//              Destroy the memory pool
//
//---------------------------------------------------------------------------
void __attribute__((destructor)) gpopt_terminate()
{
#ifdef GPOS_DEBUG
	CMDCache::Shutdown();

	CMemoryPoolManager::Pmpm()->Destroy(pmp);

	CXformFactory::Pxff()->Shutdown();
#endif // GPOS_DEBUG
}

// EOF
