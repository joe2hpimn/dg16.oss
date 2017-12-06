//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2010 Greenplum, Inc.
//
//	@filename:
//		CDXLScalar.h
//
//	@doc:
//		Base class for representing scalar DXL operators.
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#ifndef GPDXL_CDXLScalar_H
#define GPDXL_CDXLScalar_H

#include "gpos/base.h"
#include "naucrates/dxl/operators/CDXLOperator.h"
#include "naucrates/dxl/gpdb_types.h"

// fwd declarations
namespace gpopt
{
	class CMDAccessor;
}

namespace gpdxl
{
	using namespace gpopt;

	//---------------------------------------------------------------------------
	//	@class:
	//		CDXLScalar
	//
	//	@doc:
	//		Base class for representing scalar DXL operators
	//
	//---------------------------------------------------------------------------
	class CDXLScalar : public CDXLOperator
	{
		private:
			// private copy ctor
			CDXLScalar(CDXLScalar&);

		public:
			// ctor/dtor
			explicit
			CDXLScalar(IMemoryPool *pmp);
			
			virtual
			~CDXLScalar(){};
			
			Edxloptype Edxloperatortype() const;
			
			// does the operator return a boolean result
			virtual
			BOOL FBoolean(CMDAccessor *pmda) const = 0;

#ifdef GPOS_DEBUG
			virtual
			void AssertValid(const CDXLNode *pdxln, BOOL fValidateChildren) const = 0;
#endif // GPOS_DEBUG
	};
}

#endif // !GPDXL_CDXLScalar_H

// EOF
