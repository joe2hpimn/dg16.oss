//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2010 Greenplum, Inc.
//
//	@filename:
//		CParseHandlerDummy.h
//
//	@doc:
//		Dummy SAX parse handler used for validation of XML documents.
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#ifndef GPDXL_CParseHandlerDummy_H
#define GPDXL_CParseHandlerDummy_H

#include "gpos/base.h"

#include <xercesc/sax2/DefaultHandler.hpp>

namespace gpdxl
{
	using namespace gpos;

	XERCES_CPP_NAMESPACE_USE
	
	class CDXLMemoryManager;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CParseHandlerDummy
	//
	//	@doc:
	//		Dummy parse handler for validation of XML documents.
	//
	//---------------------------------------------------------------------------
	class CParseHandlerDummy : public DefaultHandler
	{
		private:
			// memory manager to use for Xerces allocation
			CDXLMemoryManager *m_pmm;
			
		public:
			// ctor
			explicit CParseHandlerDummy(CDXLMemoryManager *pmm);
						
			// process a parsing error
			void error(const SAXParseException &saxex);
	};
}

#endif // !GPDXL_CParseHandlerDummy_H

// EOF
