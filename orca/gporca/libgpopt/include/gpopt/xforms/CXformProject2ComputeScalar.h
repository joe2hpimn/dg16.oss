//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CXformProject2ComputeScalar.h
//
//	@doc:
//		Transform Project to ComputeScalar
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformProject2ComputeScalar_H
#define GPOPT_CXformProject2ComputeScalar_H

#include "gpos/base.h"
#include "gpopt/xforms/CXformImplementation.h"

namespace gpopt
{
	using namespace gpos;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CXformProject2ComputeScalar
	//
	//	@doc:
	//		Transform Project to ComputeScalar
	//
	//---------------------------------------------------------------------------
	class CXformProject2ComputeScalar : public CXformImplementation
	{

		private:

			// private copy ctor
			CXformProject2ComputeScalar(const CXformProject2ComputeScalar &);

		public:
		
			// ctor
			explicit
			CXformProject2ComputeScalar(IMemoryPool *pmp);

			// dtor
			virtual 
			~CXformProject2ComputeScalar() 
			{}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfProject2ComputeScalar;
			}
			
			virtual 
			const CHAR *SzId() const
			{
				return "CXformProject2ComputeScalar";
			}

			// compute xform promise for a given expression handle
			virtual
			EXformPromise Exfp
				(
				CExpressionHandle &exprhdl
				)
				const
			{
				if (exprhdl.Pdpscalar(1)->FHasSubquery())
				{
					return CXform::ExfpNone;
				}

				return CXform::ExfpHigh;
			}

			// actual transform
			virtual
			void Transform(CXformContext *, CXformResult *, CExpression *) const;

	}; // class CXformProject2ComputeScalar

}

#endif // !GPOPT_CXformProject2ComputeScalar_H

// EOF
