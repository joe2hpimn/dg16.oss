//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2014 Pivotal, Inc.
//
//	@filename:
//		CParseHandlerScalarBitmapIndexProbe.h
//
//	@doc:
//		SAX parse handler class for parsing bitmap index probe operator nodes
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#ifndef GPDXL_CParseHandlerScalarBitmapIndexProbe_H
#define GPDXL_CParseHandlerScalarBitmapIndexProbe_H

#include "gpos/base.h"

#include "naucrates/dxl/parser/CParseHandlerScalarOp.h"

namespace gpdxl
{
	using namespace gpos;

	XERCES_CPP_NAMESPACE_USE

	//---------------------------------------------------------------------------
	//	@class:
	//		CParseHandlerScalarBitmapIndexProbe
	//
	//	@doc:
	//		Parse handler for bitmap index probe operator nodes
	//
	//---------------------------------------------------------------------------
	class CParseHandlerScalarBitmapIndexProbe : public CParseHandlerScalarOp
	{
		private:
			// private copy ctor
			CParseHandlerScalarBitmapIndexProbe(const CParseHandlerScalarBitmapIndexProbe &);

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
			CParseHandlerScalarBitmapIndexProbe
				(
				IMemoryPool *pmp,
				CParseHandlerManager *pphm,
				CParseHandlerBase *pphRoot
				);
	};  // class CParseHandlerScalarBitmapIndexProbe
}

#endif // !GPDXL_CParseHandlerScalarBitmapIndexProbe_H

// EOF
