//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CXformIndexGet2IndexScan.h
//
//	@doc:
//		Transform Index Get to Index Scan
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformIndexGet2IndexScan_H
#define GPOPT_CXformIndexGet2IndexScan_H

#include "gpos/base.h"
#include "gpopt/xforms/CXformImplementation.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CXformIndexGet2IndexScan
	//
	//	@doc:
	//		Transform Index Get to Index Scan
	//
	//---------------------------------------------------------------------------
	class CXformIndexGet2IndexScan : public CXformImplementation
	{

		private:

			// private copy ctor
			CXformIndexGet2IndexScan(const CXformIndexGet2IndexScan &);

		public:

			// ctor
			explicit
			CXformIndexGet2IndexScan(IMemoryPool *);

			// dtor
			virtual
			~CXformIndexGet2IndexScan() {}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfIndexGet2IndexScan;
			}

			// xform name
			virtual
			const CHAR *SzId() const
			{
				return "CXformIndexGet2IndexScan";
			}

			// compute xform promise for a given expression handle
			virtual
			EXformPromise Exfp
				(
				CExpressionHandle &//exprhdl
				)
				const
			{
				return CXform::ExfpHigh;
			}

			// actual transform
			void Transform
				(
				CXformContext *pxfctxt,
				CXformResult *pxfres,
				CExpression *pexpr
				)
				const;

	}; // class CXformIndexGet2IndexScan

}

#endif // !GPOPT_CXformIndexGet2IndexScan_H

// EOF
