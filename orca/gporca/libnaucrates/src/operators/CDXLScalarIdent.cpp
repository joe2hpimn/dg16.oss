//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2010 Greenplum, Inc.
//
//	@filename:
//		CDXLScalarIdent.cpp
//
//	@doc:
//		Implementation of DXL scalar identifier operators
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "naucrates/dxl/operators/CDXLScalarIdent.h"
#include "naucrates/dxl/operators/CDXLNode.h"
#include "naucrates/dxl/xml/CXMLSerializer.h"

#include "gpopt/mdcache/CMDAccessor.h"

using namespace gpos;
using namespace gpopt;
using namespace gpdxl;



//---------------------------------------------------------------------------
//	@function:
//		CDXLScalarIdent::CDXLScalarIdent
//
//	@doc:
//		Constructor
//
//---------------------------------------------------------------------------
CDXLScalarIdent::CDXLScalarIdent
	(
	IMemoryPool *pmp,
	CDXLColRef *pdxlcr,
	IMDId *pmdidType
	)
	:
	CDXLScalar(pmp),
	m_pdxlcr(pdxlcr),
	m_pmdidType(pmdidType)
{
	GPOS_ASSERT(NULL != m_pdxlcr);
	GPOS_ASSERT(m_pmdidType->FValid());
}


//---------------------------------------------------------------------------
//	@function:
//		CDXLScalarIdent::~CDXLScalarIdent
//
//	@doc:
//		Destructor
//
//---------------------------------------------------------------------------
CDXLScalarIdent::~CDXLScalarIdent()
{
	m_pmdidType->Release();
	m_pdxlcr->Release();
}

//---------------------------------------------------------------------------
//	@function:
//		CDXLScalarIdent::Edxlop
//
//	@doc:
//		Operator type
//
//---------------------------------------------------------------------------
Edxlopid
CDXLScalarIdent::Edxlop() const
{
	return EdxlopScalarIdent;
}


//---------------------------------------------------------------------------
//	@function:
//		CDXLScalarIdent::PstrOpName
//
//	@doc:
//		Operator name
//
//---------------------------------------------------------------------------
const CWStringConst *
CDXLScalarIdent::PstrOpName() const
{
	return CDXLTokens::PstrToken(EdxltokenScalarIdent);
}

//---------------------------------------------------------------------------
//	@function:
//		CDXLScalarIdent::Pdxlcr
//
//	@doc:
//		Return column reference of the identifier operator
//
//---------------------------------------------------------------------------
const CDXLColRef *
CDXLScalarIdent::Pdxlcr() const
{
	return m_pdxlcr;
}

//---------------------------------------------------------------------------
//	@function:
//		CDXLScalarIdent::PmdidType
//
//	@doc:
//		Return the id of the column type
//
//---------------------------------------------------------------------------
IMDId *
CDXLScalarIdent::PmdidType() const
{
	return m_pmdidType;
}

//---------------------------------------------------------------------------
//	@function:
//		CDXLScalarIdent::SerializeToDXL
//
//	@doc:
//		Serialize operator in DXL format
//
//---------------------------------------------------------------------------
void
CDXLScalarIdent::SerializeToDXL
	(
	CXMLSerializer *pxmlser,
	const CDXLNode *pdxln
	)
	const
{
	const CWStringConst *pstrElemName = PstrOpName();
	
	pxmlser->OpenElement(CDXLTokens::PstrToken(EdxltokenNamespacePrefix), pstrElemName);
		
	// add col name and col id
	const CWStringConst *strCName = (m_pdxlcr->Pmdname())->Pstr(); 

	pxmlser->AddAttribute(CDXLTokens::PstrToken(EdxltokenColId), m_pdxlcr->UlID());
	pxmlser->AddAttribute(CDXLTokens::PstrToken(EdxltokenColName), strCName);
	m_pmdidType->Serialize(pxmlser, CDXLTokens::PstrToken(EdxltokenTypeId));
	pdxln->SerializeChildrenToDXL(pxmlser);

	pxmlser->CloseElement(CDXLTokens::PstrToken(EdxltokenNamespacePrefix), pstrElemName);	
}

//---------------------------------------------------------------------------
//	@function:
//		CDXLScalarIdent::FBoolean
//
//	@doc:
//		Does the operator return boolean result
//
//---------------------------------------------------------------------------
BOOL
CDXLScalarIdent::FBoolean
	(
	CMDAccessor *pmda
	)
	const
{
	return (IMDType::EtiBool == pmda->Pmdtype(m_pmdidType)->Eti());
}

#ifdef GPOS_DEBUG
//---------------------------------------------------------------------------
//	@function:
//		CDXLScalarIdent::AssertValid
//
//	@doc:
//		Checks whether operator node is well-structured
//
//---------------------------------------------------------------------------
void
CDXLScalarIdent::AssertValid
	(
	const CDXLNode *pdxln,
	BOOL // fValidateChildren 
	) 
	const
{
	GPOS_ASSERT(0 == pdxln->UlArity());
	GPOS_ASSERT(m_pmdidType->FValid());
	GPOS_ASSERT(NULL != m_pdxlcr);
}
#endif // GPOS_DEBUG

// EOF
