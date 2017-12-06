//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2010 Greenplum, Inc.
//
//	@filename:
//		CXMLSerializer.h
//
//	@doc:
//		Class for creating XML documents.
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#ifndef GPDXL_CXMLSerializer_H
#define GPDXL_CXMLSerializer_H

#include "gpos/base.h"
#include "gpos/common/CDouble.h"
#include "gpos/string/CWStringConst.h"
#include "gpos/io/COstream.h"

#include "gpos/common/CStack.h"
#include "naucrates/dxl/xml/dxltokens.h"

namespace gpdxl
{
	using namespace gpos;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CXMLSerializer
	//
	//	@doc:
	//		Class for creating XML documents.
	//
	//---------------------------------------------------------------------------
	class CXMLSerializer
	{
		// stack of strings
		typedef CStack<const CWStringBase> StrStack;
		
		private:
			// memory pool
			IMemoryPool *m_pmp;
			
			// output stream for writing out the xml document
			IOstream &m_os;
						
			// should XML document be indented
			BOOL m_fIndent;
			
			// stack of open elements
			StrStack *m_strstackElems;
		
			// denotes whether the last written tag is open and needs closing
			BOOL m_fOpenTag;
			
			// level of nesting in the XML document (i.e. number of open XML tags)
			ULONG m_ulLevel;
			
			// steps since last check for aborts
			ULONG m_ulIterLastCFA;
			
			// private copy ctor
			CXMLSerializer(const CXMLSerializer&);
			
			// add indentation
			void Indent();
			
			// escape the given string and write it to the given stream
			static
			void WriteEscaped(IOstream &os, const CWStringBase *pstr);
			
		public:
			// ctor/dtor
			CXMLSerializer
				(
				IMemoryPool *pmp,
				IOstream &os,
				BOOL fIndent = true
				)
				:
				m_pmp(pmp),
				m_os(os),
				m_fIndent(fIndent),
				m_strstackElems(NULL),
				m_fOpenTag(false),
				m_ulLevel(0),
				m_ulIterLastCFA(0)
			{
				m_strstackElems = GPOS_NEW(m_pmp) StrStack(m_pmp);
			}
			
			~CXMLSerializer();
			
			// get underlying memory pool
			IMemoryPool *Pmp() const
			{
				return m_pmp;
			}

			// starts an XML document
			void StartDocument();
			
			// opens a new element with the given name
			void OpenElement(const CWStringBase *pstrNamespace, const CWStringBase *pstrElem);
			
			// closes the element with the given name
			void CloseElement(const CWStringBase *pstrNamespace, const CWStringBase *pstrElem);
			
			// adds a string-valued attribute
			void AddAttribute(const CWStringBase *pstrAttr, const CWStringBase *pstrValue);
			
			// adds a character string attribute
			void AddAttribute(const CWStringBase *pstrAttr, const CHAR *szValue);

			// adds an unsigned integer-valued attribute
			void AddAttribute(const CWStringBase *pstrAttr, ULONG ulValue);
			
			// adds an unsigned long integer attribute
			void AddAttribute(const CWStringBase *pstrAttr, ULLONG ullValue);

			// adds an integer-valued attribute
			void AddAttribute(const CWStringBase *pstrAttr, INT iValue);
			
			// adds an integer-valued attribute
			void AddAttribute(const CWStringBase *pstrAttr, LINT lValue);

			// adds a boolean attribute
			void AddAttribute(const CWStringBase *pstrAttr, BOOL fValue);
			
			// add a double-valued attribute
			void AddAttribute(const CWStringBase *pstrAttr, CDouble dValue);

			// add a byte array attribute
			void AddAttribute(const CWStringBase *pstrAttr, BOOL fNull, const BYTE *pba, ULONG ulLen);
	};
	
}

#endif //!GPDXL_CXMLSerializer_H

// EOF
