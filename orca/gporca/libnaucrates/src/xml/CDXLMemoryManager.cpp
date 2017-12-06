//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2010 Greenplum, Inc.
//
//	@filename:
//		CDXLMemoryManager.cpp
//
//	@doc:
//		Implementation of the DXL memory manager to be plugged in Xerces.
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/base.h"

#include "naucrates/dxl/xml/CDXLMemoryManager.h"

using namespace gpdxl;

//---------------------------------------------------------------------------
//	@function:
//		CDXLMemoryManager::CDXLMemoryManager
//
//	@doc:
//		Constructs a memory manager around a given memory pool.
//
//---------------------------------------------------------------------------
CDXLMemoryManager::CDXLMemoryManager
	(
	IMemoryPool *pmp
	)
	:m_pmp(pmp)
{
	GPOS_ASSERT(NULL != m_pmp);
}

//---------------------------------------------------------------------------
//	@function:
//		CDXLMemoryManager::allocate
//
//	@doc:
//		Memory allocation.
//
//---------------------------------------------------------------------------
void *
CDXLMemoryManager::allocate
	(
	XMLSize_t xmlsize
	)
{
	GPOS_ASSERT(NULL != m_pmp);
	return GPOS_NEW_ARRAY(m_pmp, BYTE, xmlsize);
}

//---------------------------------------------------------------------------
//	@function:
//		CDXLMemoryManager::deallocate
//
//	@doc:
//		Memory deallocation.
//
//---------------------------------------------------------------------------
void
CDXLMemoryManager::deallocate
	(
	void *pv
	)
{
	GPOS_DELETE_ARRAY(reinterpret_cast<BYTE*>(pv));
}


// EOF
