//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2014 Pivotal Inc.
//
//	@filename:
//		CMDIdGPDBCtas.cpp
//
//	@doc:
//		Implementation of metadata identifiers for GPDB CTAS queries
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "naucrates/md/CMDIdGPDBCtas.h"
#include "naucrates/dxl/xml/CXMLSerializer.h"

using namespace gpos;
using namespace gpmd;

#define GPMD_GPDB_CTAS_SYSID GPOS_WSZ_LIT("CTAS")

// invalid key
CMDIdGPDBCtas CMDIdGPDBCtas::m_mdidInvalidKey(0);

//---------------------------------------------------------------------------
//	@function:
//		CMDIdGPDBCtas::CMDIdGPDBCtas
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CMDIdGPDBCtas::CMDIdGPDBCtas
	(
	OID oid
	)
	:
	CMDIdGPDB(CSystemId(IMDId::EmdidGPDB, GPMD_GPDB_CTAS_SYSID), oid)
{
	Serialize();
}


//---------------------------------------------------------------------------
//	@function:
//		CMDIdGPDBCtas::CMDIdGPDBCtas
//
//	@doc:
//		Copy constructor
//
//---------------------------------------------------------------------------
CMDIdGPDBCtas::CMDIdGPDBCtas
	(
	const CMDIdGPDBCtas &mdidSource
	)
	:
	CMDIdGPDB(mdidSource.Sysid(), mdidSource.OidObjectId())
{
	GPOS_ASSERT(mdidSource.FValid());
	GPOS_ASSERT(IMDId::EmdidGPDBCtas == mdidSource.Emdidt());
	Serialize();
}

//---------------------------------------------------------------------------
//	@function:
//		CMDIdGPDBCtas::FEquals
//
//	@doc:
//		Checks if the version of the current object is compatible with another version
//		of the same object
//
//---------------------------------------------------------------------------
BOOL
CMDIdGPDBCtas::FEquals
	(
	const IMDId *pmdid
	) 
	const
{
	if (NULL == pmdid || EmdidGPDBCtas != pmdid->Emdidt())
	{
		return false;
	}
	
	const CMDIdGPDBCtas *pmdidGPDBCTAS = CMDIdGPDBCtas::PmdidConvert(const_cast<IMDId *>(pmdid));
	
	return m_oid == pmdidGPDBCTAS->OidObjectId(); 
}

//---------------------------------------------------------------------------
//	@function:
//		CMDIdGPDBCtas::FValid
//
//	@doc:
//		Is the mdid valid
//
//---------------------------------------------------------------------------
BOOL CMDIdGPDBCtas::FValid() const
{
	// FIXME(chasseur): in well-formed C++ code, the implicit 'this' pointer in
	// a member method can never be NULL; however, some callers may invoke this
	// method from a (possibly-NULL) pointer with the '->' operator; callers
	// should be modified to explicitly do NULL-checks on pointers so that this
	// method does not rely on undefined behavior
	return NULL != this && !FEquals(&CMDIdGPDBCtas::m_mdidInvalidKey);
}

//---------------------------------------------------------------------------
//	@function:
//		CMDIdGPDBCtas::OsPrint
//
//	@doc:
//		Debug print of the id in the provided stream
//
//---------------------------------------------------------------------------
IOstream &
CMDIdGPDBCtas::OsPrint
	(
	IOstream &os
	) 
	const
{
	os << "(" << OidObjectId() << "," << 
				UlVersionMajor() << "." << UlVersionMinor() << ")";
	return os;
}

// EOF
