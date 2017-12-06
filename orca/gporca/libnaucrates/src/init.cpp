//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2010 Greenplum, Inc.
//
//	@filename:
//		init.cpp
//
//	@doc:
//		Implementation of initialization and termination functions for
//		libgpdxl.
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/memory/CAutoMemoryPool.h"

#include "naucrates/exception.h"
#include "naucrates/init.h"
#include "naucrates/dxl/xml/CDXLMemoryManager.h"
#include "naucrates/dxl/xml/dxltokens.h"
#include "naucrates/dxl/parser/CParseHandlerFactory.h"

#include <xercesc/framework/MemBufInputSource.hpp>
#include <xercesc/util/XMLString.hpp>

using namespace gpos;
using namespace gpdxl;

static
CDXLMemoryManager *pmm = NULL;

static
IMemoryPool *pmpXerces = NULL;

static
IMemoryPool *pmpDXL = NULL;

// safe-guard to prevent initializing DXL support more than once
static
volatile ULONG_PTR m_ulpInitDXL = 0;

// safe-guard to prevent shutting DXL support down more than once
static
volatile ULONG_PTR m_ulpShutdownDXL = 0;


//---------------------------------------------------------------------------
//      @function:
//              InitDXL
//
//      @doc:
//				Initialize DXL support; must be called before any library
//				function is called
//
//
//---------------------------------------------------------------------------
void InitDXL()
{
	if (0 < UlpExchangeAdd(&m_ulpInitDXL, 1))
	{
		// DXL support is already initialized by a previous call
		(void) UlpExchangeAdd(&m_ulpInitDXL, -1);

		return;
	}

	GPOS_ASSERT(NULL != pmpXerces);
	GPOS_ASSERT(NULL != pmpDXL);

	// setup own memory manager
	pmm = GPOS_NEW(pmpXerces) CDXLMemoryManager(pmpXerces);

	// initialize Xerces, if this fails library initialization should crash here
	XMLPlatformUtils::Initialize(
			XMLUni::fgXercescDefaultLocale, // locale
			NULL, // nlsHome: location for message files
			NULL, // panicHandler
			pmm // memoryManager
			);

	// initialize DXL tokens
	CDXLTokens::Init(pmpDXL);

	// initialize parse handler mappings
	CParseHandlerFactory::Init(pmpDXL);
}


//---------------------------------------------------------------------------
//      @function:
//              ShutdownDXL
//
//      @doc:
//				Shutdown DXL support; called only at library termination
//
//---------------------------------------------------------------------------
void ShutdownDXL()
{
	if (0 < UlpExchangeAdd(&m_ulpShutdownDXL, 1))
	{
		// DXL support is already shut-down by a previous call
		(void) UlpExchangeAdd(&m_ulpShutdownDXL, -1);

		return;
	}

	GPOS_ASSERT(NULL != pmpXerces);

	XMLPlatformUtils::Terminate();

	CDXLTokens::Terminate();

	GPOS_DELETE(pmm);
	pmm = NULL;
}


//---------------------------------------------------------------------------
//      @function:
//              gpdxl_init
//
//      @doc:
//              Initialize Xerces parser utils
//
//---------------------------------------------------------------------------
void __attribute__((constructor)) gpdxl_init()
{
	// create memory pool for Xerces global allocations
	{
		CAutoMemoryPool amp;

		// detach safety
		pmpXerces = amp.PmpDetach();
	}
	
	// create memory pool for DXL global allocations
	{
		CAutoMemoryPool amp;

		// detach safety
		pmpDXL = amp.PmpDetach();
	}

	// add standard exception messages
	(void) EresExceptionInit(pmpDXL);
}


//---------------------------------------------------------------------------
//      @function:
//              gpdxl_terminate
//
//      @doc:
//              Terminate Xerces parser utils and destroy memory pool
//
//---------------------------------------------------------------------------
void __attribute__((destructor)) gpdxl_terminate()
{
#ifdef GPOS_DEBUG
	ShutdownDXL();

	if (NULL != pmpDXL)
	{
		(CMemoryPoolManager::Pmpm())->Destroy(pmpDXL);
		pmpDXL = NULL;
	}

	if (NULL != pmpXerces)
	{
		(CMemoryPoolManager::Pmpm())->Destroy(pmpXerces);
		pmpXerces = NULL;
	}
#endif // GPOS_DEBUG
}

// EOF
