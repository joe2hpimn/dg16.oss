//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2014 Pivotal, Inc.
//
//	@filename:
//		CExpressionUtils.h
//
//	@doc:
//		Utility routines for transforming expressions
//
//	@owner:
//		, 
//
//	@test:
//
//---------------------------------------------------------------------------

#ifndef GPOPT_CExpressionUtils_H
#define GPOPT_CExpressionUtils_H

#include "gpos/base.h"

namespace gpopt
{
	using namespace gpos;

	// fwd declarations
	class CExpression;

	//---------------------------------------------------------------------------
	//	@class:
	//		CExpressionUtils
	//
	//	@doc:
	//		Utility routines for transforming expressions
	//
	//---------------------------------------------------------------------------
	class CExpressionUtils
	{
		private:
			// unnest a given expression's child and append unnested nodes to given array
			static
			void UnnestChild
				(
				IMemoryPool *pmp,
				CExpression *pexpr,
				ULONG UlChildIndex,
				BOOL fAnd,
				BOOL fOr,
				BOOL fNotChildren,
				DrgPexpr *pdrgpexpr
				);

			// append the unnested children of given expression to given array
			static
			void AppendChildren(IMemoryPool *pmp, CExpression *pexpr, DrgPexpr *pdrgpexpr);

			// return an array of expression children after being unnested
			static
			DrgPexpr *PdrgpexprUnnestChildren(IMemoryPool *pmp, CExpression *pexpr);

			// push not expression one level down the given expression
			static
			CExpression *PexprPushNotOneLevel(IMemoryPool *pmp, CExpression *pexpr);

		public:
			// remove duplicate AND/OR children
			static
			CExpression *PexprDedupChildren(IMemoryPool *pmp, CExpression *pexpr);

			// unnest AND/OR/NOT predicates
			static
			CExpression *PexprUnnest(IMemoryPool *pmp, CExpression *pexpr);
	};
}

#endif // !GPOPT_CExpressionUtils_H

// EOF
