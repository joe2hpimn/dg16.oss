//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CXformImplementLimit.h
//
//	@doc:
//		Transform Logical into Physical Limit
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformImplementLimit_H
#define GPOPT_CXformImplementLimit_H

#include "gpos/base.h"
#include "gpopt/xforms/CXformImplementation.h"

namespace gpopt
{
	using namespace gpos;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CXformImplementLimit
	//
	//	@doc:
	//		Transform Logical into Physical Limit
	//
	//---------------------------------------------------------------------------
	class CXformImplementLimit : public CXformImplementation
	{

		private:

			// private copy ctor
			CXformImplementLimit(const CXformImplementLimit &);

		public:
		
			// ctor
			explicit
			CXformImplementLimit(IMemoryPool *pmp);

			// dtor
			virtual 
			~CXformImplementLimit() 
			{}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfImplementLimit;
			}
			
			virtual 
			const CHAR *SzId() const
			{
				return "CXformImplementLimit";
			}
		
			// compute xform promise for a given expression handle
			virtual
			EXformPromise Exfp
				(
				CExpressionHandle & // exprhdl
				)
				const
			{
				return CXform::ExfpHigh;
			}

			// actual transform
			void Transform(CXformContext *, CXformResult *, CExpression *) const;

	}; // class CXformImplementLimit

}

#endif // !GPOPT_CXformImplementLimit_H

// EOF
