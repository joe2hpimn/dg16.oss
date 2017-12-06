//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2010 Greenplum, Inc.
//
//	@filename:
//		CParseHandlerScalarOp.h
//
//	@doc:
//		SAX parse handler class for parsing scalar operators.
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#ifndef GPDXL_CParseHandlerScalarOp_H
#define GPDXL_CParseHandlerScalarOp_H

#include "gpos/base.h"
#include "naucrates/dxl/parser/CParseHandlerOp.h"

namespace gpdxl
{
	using namespace gpos;

	XERCES_CPP_NAMESPACE_USE

	//---------------------------------------------------------------------------
	//	@class:
	//		CParseHandlerScalarOp
	//
	//	@doc:
	//		Parse handler for parsing a scalar operator
	//
	//---------------------------------------------------------------------------
	class CParseHandlerScalarOp : public CParseHandlerOp 
	{
		private:

			// private copy ctor
			CParseHandlerScalarOp(const CParseHandlerScalarOp &);
			
		protected:

			// process notification of the beginning of an element.
			virtual void StartElement
				(
					const XMLCh* const xmlszUri, 		// URI of element's namespace
 					const XMLCh* const xmlszLocalname,	// local part of element's name
					const XMLCh* const xmlszQname,		// element's qname
					const Attributes& attr				// element's attributes
				);
				
			// process notification of the end of an element.
			virtual void EndElement
				(
					const XMLCh* const xmlszUri, 		// URI of element's namespace
					const XMLCh* const xmlszLocalname,	// local part of element's name
					const XMLCh* const xmlszQname		// element's qname
				);
			
		public:
			CParseHandlerScalarOp
				(
				IMemoryPool *pmp,
				CParseHandlerManager *pphm,
				CParseHandlerBase *pphRoot
				);
			
			virtual
			~CParseHandlerScalarOp();
			
	};
}

#endif // !GPDXL_CParseHandlerScalarOp_H

// EOF
