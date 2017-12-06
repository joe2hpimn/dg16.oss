//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2014 Pivotal Inc.
//
//	@filename:
//		CParseHandlerCostModel.h
//
//	@doc:
//		SAX parse handler class for parsing cost model config
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#ifndef GPDXL_CParseHandlerCostModel_H
#define GPDXL_CParseHandlerCostModel_H

#include "gpos/base.h"
#include "naucrates/dxl/parser/CParseHandlerBase.h"

namespace gpdxl
{
	using namespace gpos;

	XERCES_CPP_NAMESPACE_USE
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CParseHandlerCostModel
	//
	//	@doc:
	//		SAX parse handler class for parsing cost model config options
	//
	//---------------------------------------------------------------------------
	class CParseHandlerCostModel : public CParseHandlerBase
	{
		private:

			// cost model
			ICostModel *m_pcm;
			
			// private copy ctor
			CParseHandlerCostModel(const CParseHandlerCostModel&); 
		
			// process the start of an element
			void StartElement
				(
					const XMLCh* const xmlszUri, 		// URI of element's namespace
 					const XMLCh* const xmlszLocalname,	// local part of element's name
					const XMLCh* const xmlszQname,		// element's qname
					const Attributes& attr				// element's attributes
				);
				
			// process the end of an element
			void EndElement
				(
					const XMLCh* const xmlszUri, 		// URI of element's namespace
					const XMLCh* const xmlszLocalname,	// local part of element's name
					const XMLCh* const xmlszQname		// element's qname
				);
			
		public:
			// ctor/dtor
			CParseHandlerCostModel
				(
				IMemoryPool *pmp,
				CParseHandlerManager *pphm,
				CParseHandlerBase *pphRoot
				);
			
			virtual ~CParseHandlerCostModel();
			
			// cost model
			ICostModel *Pcm() const;
	};
}

#endif // !GPDXL_CParseHandlerCostModel_H

// EOF
