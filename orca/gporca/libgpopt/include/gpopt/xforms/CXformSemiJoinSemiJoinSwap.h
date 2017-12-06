//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CXformSemiJoinSemiJoinSwap.h
//
//	@doc:
//		Swap two cascaded semi-joins
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformSemiJoinSemiJoinSwap_H
#define GPOPT_CXformSemiJoinSemiJoinSwap_H

#include "gpos/base.h"

#include "gpopt/operators/ops.h"
#include "gpopt/xforms/CXformJoinSwap.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CXformSemiJoinSemiJoinSwap
	//
	//	@doc:
	//		Swap two cascaded semi-joins
	//
	//---------------------------------------------------------------------------
	class CXformSemiJoinSemiJoinSwap : public CXformJoinSwap<CLogicalLeftSemiJoin, CLogicalLeftSemiJoin>
	{

		private:

			// private copy ctor
			CXformSemiJoinSemiJoinSwap(const CXformSemiJoinSemiJoinSwap &);

		public:

			// ctor
			explicit
			CXformSemiJoinSemiJoinSwap
				(
				IMemoryPool *pmp
				)
				:
				CXformJoinSwap<CLogicalLeftSemiJoin, CLogicalLeftSemiJoin>(pmp)
			{}

			// dtor
			virtual
			~CXformSemiJoinSemiJoinSwap()
			{}

			// Compatibility function
			virtual
			BOOL FCompatible
				(
				CXform::EXformId exfid
				)
			{
				return ExfSemiJoinSemiJoinSwap != exfid;
			}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfSemiJoinSemiJoinSwap;
			}

			virtual
			const CHAR *SzId() const
			{
				return "CXformSemiJoinSemiJoinSwap";
			}

	}; // class CXformSemiJoinSemiJoinSwap

}

#endif // !GPOPT_CXformSemiJoinSemiJoinSwap_H

// EOF
