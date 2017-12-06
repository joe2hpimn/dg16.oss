//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2008 Greenplum, Inc.
//
//	@filename:
//		CColRef.cpp
//
//	@doc:
//		Implementation of column reference class
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/base.h"
#include "gpopt/base/CColRef.h"

using namespace gpopt;

// invalid key
const ULONG CColRef::m_ulInvalid = ULONG_MAX;

//---------------------------------------------------------------------------
//	@function:
//		CColRef::CColRef
//
//	@doc:
//		ctor
//		takes ownership of string; verify string is properly formatted
//
//---------------------------------------------------------------------------
CColRef::CColRef
	(
	const IMDType *pmdtype,
	ULONG ulId,
	const CName *pname
	)
	:
	m_pmdtype(pmdtype),
	m_pname(pname),
	m_ulId(ulId)
{
	GPOS_ASSERT(NULL != pmdtype);
	GPOS_ASSERT(pmdtype->Pmdid()->FValid());
	GPOS_ASSERT(NULL != pname);
}


//---------------------------------------------------------------------------
//	@function:
//		CColRef::~CColRef
//
//	@doc:
//		ctor
//
//---------------------------------------------------------------------------
CColRef::~CColRef()
{
	// we own the name 
	GPOS_DELETE(m_pname);
}


//---------------------------------------------------------------------------
//	@function:
//		CColRef::UlHash
//
//	@doc:
//		static hash function
//
//---------------------------------------------------------------------------
ULONG
CColRef::UlHash
	(
	const ULONG &ulptr
	)
{
	return gpos::UlHash<ULONG>(&ulptr);
}

//---------------------------------------------------------------------------
//	@function:
//		CColRef::UlHash
//
//	@doc:
//		static hash function
//
//---------------------------------------------------------------------------
ULONG
CColRef::UlHash
	(
	const CColRef *pcr
	)
{
	ULONG ulId = pcr->UlId();
	return gpos::UlHash<ULONG>(&ulId);
}


//---------------------------------------------------------------------------
//	@function:
//		CColRef::OsPrint
//
//	@doc:
//		debug print
//
//---------------------------------------------------------------------------
IOstream &
CColRef::OsPrint
	(
	IOstream &os
	)
	const
{
	m_pname->OsPrint(os);
	os << " (" << UlId() << ")";
	
	return os;
}

//---------------------------------------------------------------------------
//	@function:
//		CColRef::Pdrgpul
//
//	@doc:
//		Extract array of colids from array of colrefs
//
//---------------------------------------------------------------------------
DrgPul *
CColRef::Pdrgpul
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr
	)
{
	DrgPul *pdrgpul = GPOS_NEW(pmp) DrgPul(pmp);
	const ULONG ulLen = pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulLen; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];
		pdrgpul->Append(GPOS_NEW(pmp) ULONG(pcr->UlId()));
	}

	return pdrgpul;
}

//---------------------------------------------------------------------------
//	@function:
//		CColRef::FEqual
//
//	@doc:
//		Are the two arrays of column references equivalent
//
//---------------------------------------------------------------------------
BOOL
CColRef::FEqual
	(
	const DrgPcr *pdrgpcr1,
	const DrgPcr *pdrgpcr2
	)
{
	if (NULL == pdrgpcr1 || NULL == pdrgpcr2)
	{
		return  (NULL == pdrgpcr1 && NULL == pdrgpcr2);
	}

	return pdrgpcr1->FEqual(pdrgpcr2);
}

// EOF

