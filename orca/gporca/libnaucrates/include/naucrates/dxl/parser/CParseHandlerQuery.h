//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 Greenplum, Inc.
//
//	@filename:
//		CParseHandlerQuery.h
//
//	@doc:
//		Parse handler for converting a query (logical plan) from a DXL document
//		into a DXL tree.
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#ifndef GPDXL_CParseHandlerQuery_H
#define GPDXL_CParseHandlerQuery_H

#include "gpos/base.h"
#include "naucrates/dxl/parser/CParseHandlerBase.h"

namespace gpdxl
{
	using namespace gpos;

	XERCES_CPP_NAMESPACE_USE

	//---------------------------------------------------------------------------
	//	@class:
	//		CParseHandlerQuery
	//
	//	@doc:
	//		Parse handler for converting a query (logical plan) from a DXL document
	//		into a DXL tree.
	//---------------------------------------------------------------------------
	class CParseHandlerQuery : public CParseHandlerBase
	{
		private:

			// the root of the parsed DXL tree constructed by the parse handler
			CDXLNode *m_pdxln;

			// list of output columns (represented as scalar ident nodes)
			DrgPdxln *m_pdrgpdxlnOutputCols;

			// list of CTE priducers
			DrgPdxln *m_pdrgpdxlnCTE;

			// private ctor
			CParseHandlerQuery(const CParseHandlerQuery&);

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
			CParseHandlerQuery
				(
				IMemoryPool *pmp,
				CParseHandlerManager *pphm,
				CParseHandlerBase *pphRoot
				);

			virtual
			~CParseHandlerQuery();

			// returns the root of constructed DXL plan
			CDXLNode *Pdxln() const;

			// returns the dxl representation of the query output
			DrgPdxln *PdrgpdxlnOutputCols() const;

			// returns the CTEs
			DrgPdxln *PdrgpdxlnCTE() const;

			EDxlParseHandlerType Edxlphtype() const;

	};
}

#endif // !GPDXL_CParseHandlerQuery_H

// EOF
