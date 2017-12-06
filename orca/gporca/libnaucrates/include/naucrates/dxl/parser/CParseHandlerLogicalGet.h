//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 Greenplum, Inc.
//
//	@filename:
//		CParseHandlerLogicalGet.h
//
//	@doc:
//		SAX parse handler class for parsing logical get operator nodes.
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#ifndef GPDXL_CParseHandlerLogicalGet_H
#define GPDXL_CParseHandlerLogicalGet_H

#include "gpos/base.h"
#include "naucrates/dxl/parser/CParseHandlerLogicalOp.h"
#include "naucrates/dxl/operators/CDXLLogicalGet.h"
#include "naucrates/dxl/xml/dxltokens.h"

namespace gpdxl
{
	using namespace gpos;


	XERCES_CPP_NAMESPACE_USE

	//---------------------------------------------------------------------------
	//	@class:
	//		CParseHandlerLogicalGet
	//
	//	@doc:
	//		Parse handler for parsing a logical get operator
	//
	//---------------------------------------------------------------------------
	class CParseHandlerLogicalGet : public CParseHandlerLogicalOp
	{
		private:

			// private copy ctor
			CParseHandlerLogicalGet(const CParseHandlerLogicalGet &);

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

		protected:

			// start element helper function
			void StartElement(const XMLCh* const xmlszLocalname, Edxltoken edxltoken);

			// end element helper function
			void EndElement(const XMLCh* const xmlszLocalname, Edxltoken edxltoken);

		public:
			// ctor
			CParseHandlerLogicalGet
				(
				IMemoryPool *pmp,
				CParseHandlerManager *pphm,
				CParseHandlerBase *pphRoot
				);

	};
}

#endif // !GPDXL_CParseHandlerLogicalGet_H

// EOF
