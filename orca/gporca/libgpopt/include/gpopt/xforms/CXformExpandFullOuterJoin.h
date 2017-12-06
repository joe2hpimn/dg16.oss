//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2013 EMC Corp.
//
//	@filename:
//		CXformExpandFullOuterJoin.h
//
//	@doc:
//		Transform logical FOJ to a UNION ALL between LOJ and LASJ
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformExpandFullOuterJoin_H
#define GPOPT_CXformExpandFullOuterJoin_H

#include "gpos/base.h"
#include "gpopt/xforms/CXformExploration.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CXformExpandFullOuterJoin
	//
	//	@doc:
	//		Transform logical FOJ with a rename on top to a UNION ALL between LOJ
	//		and LASJ
	//
	//---------------------------------------------------------------------------
	class CXformExpandFullOuterJoin : public CXformExploration
	{

		private:

			// private copy ctor
			CXformExpandFullOuterJoin(const CXformExpandFullOuterJoin &);

			// construct a join expression of two CTEs using the given CTE ids
			// and output columns
			CExpression *PexprLogicalJoinOverCTEs
				(
				IMemoryPool *pmp,
				EdxlJoinType edxljointype,
				ULONG ulLeftCTEId,
				DrgPcr *pdrgpcrLeft,
				ULONG ulRightCTEId,
				DrgPcr *pdrgpcrRight,
				CExpression *pexprScalar
				)
				const;

		public:

			// ctor
			explicit
			CXformExpandFullOuterJoin(IMemoryPool *pmp);

			// dtor
			virtual
			~CXformExpandFullOuterJoin() {}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfExpandFullOuterJoin;
			}

			// return a string for xform name
			virtual
			const CHAR *SzId() const
			{
				return "CXformExpandFullOuterJoin";
			}

			// compute xform promise for a given expression handle
			virtual
			EXformPromise Exfp(CExpressionHandle &exprhdl) const;

			// actual transform
			virtual
			void Transform
				(
				CXformContext *pxfctxt,
				CXformResult *pxfres,
				CExpression *pexpr
				)
				const;

	}; // class CXformExpandFullOuterJoin
}

#endif // !GPOPT_CXformExpandFullOuterJoin_H

// EOF
