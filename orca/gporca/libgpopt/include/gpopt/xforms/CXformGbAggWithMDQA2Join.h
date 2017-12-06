//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2015 Pivotal Inc.
//
//	@filename:
//		CXformGbAggWithMDQA2Join.h
//
//	@doc:
//		Transform a GbAgg with multiple distinct qualified aggregates (MDQAs)
//		to a join tree with single DQA leaves
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformGbAggWithMDQA2Join_H
#define GPOPT_CXformGbAggWithMDQA2Join_H

#include "gpos/base.h"
#include "gpopt/xforms/CXformExploration.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CXformGbAggWithMDQA2Join
	//
	//	@doc:
	//		Transform a GbAgg with multiple distinct qualified aggregates (MDQAs)
	//		to a join tree with single DQA leaves
	//
	//---------------------------------------------------------------------------
	class CXformGbAggWithMDQA2Join : public CXformExploration
	{

		private:

			// private copy ctor
			CXformGbAggWithMDQA2Join(const CXformGbAggWithMDQA2Join &);

			static
			CExpression *PexprMDQAs2Join(IMemoryPool *pmp, CExpression *pexpr);

			// expand GbAgg with multiple distinct aggregates into a join of single distinct aggregates
			static
			CExpression *PexprExpandMDQAs(IMemoryPool *pmp, CExpression *pexpr);

			// main transformation function driver
			static
			CExpression *PexprTransform(IMemoryPool *pmp, CExpression *pexpr);

		public:

			// ctor
			explicit
			CXformGbAggWithMDQA2Join(IMemoryPool *pmp);

			// dtor
			virtual
			~CXformGbAggWithMDQA2Join()
			{}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfGbAggWithMDQA2Join;
			}

			// return a string for xform name
			virtual
			const CHAR *SzId() const
			{
				return "CXformGbAggWithMDQA2Join";
			}

			// compute xform promise for a given expression handle
			virtual
			EXformPromise Exfp(CExpressionHandle &exprhdl) const;

			// actual transform
			void Transform(CXformContext *, CXformResult *, CExpression *) const;

	}; // class CXformGbAggWithMDQA2Join

}

#endif // !GPOPT_CXformGbAggWithMDQA2Join_H

// EOF
