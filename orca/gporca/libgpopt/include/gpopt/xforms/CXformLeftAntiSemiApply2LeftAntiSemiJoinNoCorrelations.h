//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CXformLeftAntiSemiApply2LeftAntiSemiJoinNoCorrelations.h
//
//	@doc:
//		Turn LS apply into LS join when inner child has no outer references
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformLeftAntiSemiApply2LeftAntiSemiJoinNoCorrelations_H
#define GPOPT_CXformLeftAntiSemiApply2LeftAntiSemiJoinNoCorrelations_H

#include "gpos/base.h"
#include "gpopt/xforms/CXformApply2Join.h"
#include "gpopt/operators/ops.h"


namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CXformLeftAntiSemiApply2LeftAntiSemiJoinNoCorrelations
	//
	//	@doc:
	//		Transform Apply into Join by decorrelating the inner side
	//
	//---------------------------------------------------------------------------
	class CXformLeftAntiSemiApply2LeftAntiSemiJoinNoCorrelations : public CXformApply2Join<CLogicalLeftAntiSemiApply, CLogicalLeftAntiSemiJoin>
	{

		private:

			// private copy ctor
			CXformLeftAntiSemiApply2LeftAntiSemiJoinNoCorrelations(const CXformLeftAntiSemiApply2LeftAntiSemiJoinNoCorrelations &);

		public:

			// ctor
			explicit
			CXformLeftAntiSemiApply2LeftAntiSemiJoinNoCorrelations
				(
				IMemoryPool *pmp
				)
				:
				CXformApply2Join<CLogicalLeftAntiSemiApply, CLogicalLeftAntiSemiJoin>(pmp)
			{}

			// dtor
			virtual
			~CXformLeftAntiSemiApply2LeftAntiSemiJoinNoCorrelations()
			{}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfLeftAntiSemiApply2LeftAntiSemiJoinNoCorrelations;
			}

			virtual
			const CHAR *SzId() const
			{
				return "CXformLeftAntiSemiApply2LeftAntiSemiJoinNoCorrelations";
			}

			// compute xform promise for a given expression handle
			virtual
			EXformPromise Exfp(CExpressionHandle &exprhdl) const;

			// actual transform
			void Transform(CXformContext *pxfctxt, CXformResult *pxfres, CExpression *pexpr) const;


	}; // class CXformLeftAntiSemiApply2LeftAntiSemiJoinNoCorrelations

}

#endif // !GPOPT_CXformLeftAntiSemiApply2LeftAntiSemiJoinNoCorrelations_H

// EOF
