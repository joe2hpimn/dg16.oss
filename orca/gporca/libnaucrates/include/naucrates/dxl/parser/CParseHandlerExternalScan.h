//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2013 Pivotal, Inc.
//
//	@filename:
//		CParseHandlerExternalScan.h
//
//	@doc:
//		SAX parse handler class for parsing external scan operator
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#ifndef GPDXL_CParseHandlerExternalScan_H
#define GPDXL_CParseHandlerExternalScan_H

#include "gpos/base.h"
#include "naucrates/dxl/parser/CParseHandlerTableScan.h"

#include "naucrates/dxl/operators/CDXLPhysicalExternalScan.h"


namespace gpdxl
{
	using namespace gpos;

	XERCES_CPP_NAMESPACE_USE

	//---------------------------------------------------------------------------
	//	@class:
	//		CParseHandlerExternalScan
	//
	//	@doc:
	//		Parse handler for parsing external scan operator
	//
	//---------------------------------------------------------------------------
	class CParseHandlerExternalScan : public CParseHandlerTableScan
	{
		private:

			// private copy ctor
			CParseHandlerExternalScan(const CParseHandlerExternalScan &);

			// process the start of an element
			virtual
			void StartElement
				(
					const XMLCh* const xmlszUri, 		// URI of element's namespace
 					const XMLCh* const xmlszLocalname,	// local part of element's name
					const XMLCh* const xmlszQname,		// element's qname
					const Attributes& attr				// element's attributes
				);

			// process the end of an element
			virtual
			void EndElement
				(
					const XMLCh* const xmlszUri, 		// URI of element's namespace
					const XMLCh* const xmlszLocalname,	// local part of element's name
					const XMLCh* const xmlszQname		// element's qname
				);

		public:
			// ctor
			CParseHandlerExternalScan
				(
				IMemoryPool *pmp,
				CParseHandlerManager *pphm,
				CParseHandlerBase *pphRoot
				);
	};
}

#endif // !GPDXL_CParseHandlerExternalScan_H

// EOF
