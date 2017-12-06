//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CParseHandlerArray.h
//
//	@doc:
//		SAX parse handler class for scalar array operators
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#ifndef GPDXL_CParseHandlerArray_H
#define GPDXL_CParseHandlerArray_H

#include "gpos/base.h"
#include "naucrates/dxl/parser/CParseHandlerScalarOp.h"

#include "naucrates/dxl/operators/CDXLPhysicalAgg.h"

namespace gpdxl
{
	using namespace gpos;

	XERCES_CPP_NAMESPACE_USE
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CParseHandlerArray
	//
	//	@doc:
	//		Parse handler for scalar array operators
	//
	//---------------------------------------------------------------------------
	class CParseHandlerArray : public CParseHandlerScalarOp
	{
		private:

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
			
			// private copy ctor
			CParseHandlerArray(const CParseHandlerArray &);
			
		public:
			
			// ctor
			CParseHandlerArray(IMemoryPool *pmp, CParseHandlerManager *pphm, CParseHandlerBase *pph);						
	};
}

#endif // !GPDXL_CParseHandlerArray_H

// EOF
