//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2013 EMC Corp.
//
//	@filename:
//		CXformAntiSemiJoinNotInAntiSemiJoinNotInSwap.h
//
//	@doc:
//		Swap two cascaded anti semi-joins with NotIn semantics
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformAntiSemiJoinNotInAntiSemiJoinNotInSwap_H
#define GPOPT_CXformAntiSemiJoinNotInAntiSemiJoinNotInSwap_H

#include "gpos/base.h"

#include "gpopt/operators/ops.h"
#include "gpopt/xforms/CXformJoinSwap.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CXformAntiSemiJoinNotInAntiSemiJoinNotInSwap
	//
	//	@doc:
	//		Swap two cascaded anti semi-joins with NotIn semantics
	//
	//---------------------------------------------------------------------------
	class CXformAntiSemiJoinNotInAntiSemiJoinNotInSwap : public CXformJoinSwap<CLogicalLeftAntiSemiJoinNotIn, CLogicalLeftAntiSemiJoinNotIn>
	{

		private:

			// private copy ctor
			CXformAntiSemiJoinNotInAntiSemiJoinNotInSwap(const CXformAntiSemiJoinNotInAntiSemiJoinNotInSwap &);

		public:

			// ctor
			explicit
			CXformAntiSemiJoinNotInAntiSemiJoinNotInSwap
				(
				IMemoryPool *pmp
				)
				:
				CXformJoinSwap<CLogicalLeftAntiSemiJoinNotIn, CLogicalLeftAntiSemiJoinNotIn>(pmp)
			{}

			// dtor
			virtual
			~CXformAntiSemiJoinNotInAntiSemiJoinNotInSwap()
			{}

			// Compatibility function
			virtual
			BOOL FCompatible
				(
				CXform::EXformId exfid
				)
			{
				return ExfAntiSemiJoinNotInAntiSemiJoinNotInSwap != exfid;
			}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfAntiSemiJoinNotInAntiSemiJoinNotInSwap;
			}

			virtual
			const CHAR *SzId() const
			{
				return "CXformAntiSemiJoinNotInAntiSemiJoinNotInSwap";
			}

	}; // class CXformAntiSemiJoinNotInAntiSemiJoinNotInSwap

}

#endif // !GPOPT_CXformAntiSemiJoinNotInAntiSemiJoinNotInSwap_H

// EOF
