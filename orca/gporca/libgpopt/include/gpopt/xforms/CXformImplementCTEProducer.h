//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CXformImplementCTEProducer.h
//
//	@doc:
//		Transform Logical CTE Producer to Physical CTE Producer
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformImplementCTEProducer_H
#define GPOPT_CXformImplementCTEProducer_H

#include "gpos/base.h"
#include "gpopt/xforms/CXformImplementation.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CXformImplementCTEProducer
	//
	//	@doc:
	//		Transform Logical CTE Producer to Physical CTE Producer
	//
	//---------------------------------------------------------------------------
	class CXformImplementCTEProducer : public CXformImplementation
	{

		private:

			// private copy ctor
			CXformImplementCTEProducer(const CXformImplementCTEProducer &);

		public:

			// ctor
			explicit
			CXformImplementCTEProducer(IMemoryPool *pmp);

			// dtor
			virtual
			~CXformImplementCTEProducer() {}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfImplementCTEProducer;
			}

			// return a string for xform name
			virtual
			const CHAR *SzId() const
			{
				return "CXformImplementCTEProducer";
			}

			// compute xform promise for a given expression handle
			virtual
			EXformPromise Exfp(CExpressionHandle &exprhdl) const;

			// actual transform
			void Transform
				(
				CXformContext *pxfctxt,
				CXformResult *pxfres,
				CExpression *pexpr
				)
				const;

	}; // class CXformImplementCTEProducer
}

#endif // !GPOPT_CXformImplementCTEProducer_H

// EOF
