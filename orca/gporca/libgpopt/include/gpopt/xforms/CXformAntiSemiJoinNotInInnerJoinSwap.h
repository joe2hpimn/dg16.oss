//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2013 EMC Corp.
//
//	@filename:
//		CXformAntiSemiJoinNotInInnerJoinSwap.h
//
//	@doc:
//		Swap cascaded anti semi-join (NotIn) and inner join
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformAntiSemiJoinNotInInnerJoinSwap_H
#define GPOPT_CXformAntiSemiJoinNotInInnerJoinSwap_H

#include "gpos/base.h"

#include "gpopt/operators/ops.h"
#include "gpopt/xforms/CXformJoinSwap.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CXformAntiSemiJoinNotInInnerJoinSwap
	//
	//	@doc:
	//		Swap cascaded anti semi-join (NotIn) and inner join
	//
	//---------------------------------------------------------------------------
	class CXformAntiSemiJoinNotInInnerJoinSwap : public CXformJoinSwap<CLogicalLeftAntiSemiJoinNotIn, CLogicalInnerJoin>
	{

		private:

			// private copy ctor
			CXformAntiSemiJoinNotInInnerJoinSwap(const CXformAntiSemiJoinNotInInnerJoinSwap &);

		public:

			// ctor
			explicit
			CXformAntiSemiJoinNotInInnerJoinSwap
				(
				IMemoryPool *pmp
				)
				:
				CXformJoinSwap<CLogicalLeftAntiSemiJoinNotIn, CLogicalInnerJoin>(pmp)
			{}

			// dtor
			virtual
			~CXformAntiSemiJoinNotInInnerJoinSwap()
			{}

			// Compatibility function
			virtual
			BOOL FCompatible
				(
				CXform::EXformId exfid
				)
			{
				return ExfInnerJoinAntiSemiJoinNotInSwap != exfid;
			}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfAntiSemiJoinNotInInnerJoinSwap;
			}

			virtual
			const CHAR *SzId() const
			{
				return "CXformAntiSemiJoinNotInInnerJoinSwap";
			}

	}; // class CXformAntiSemiJoinNotInInnerJoinSwap

}

#endif // !GPOPT_CXformAntiSemiJoinNotInInnerJoinSwap_H

// EOF
