//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CXformImplementLeftOuterCorrelatedApply.h
//
//	@doc:
//		Transform LeftOuter correlated apply to physical LeftOuter correlated
//		apply
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformImplementLeftOuterCorrelatedApply_H
#define GPOPT_CXformImplementLeftOuterCorrelatedApply_H

#include "gpos/base.h"
#include "gpopt/xforms/CXformImplementCorrelatedApply.h"

namespace gpopt
{
	using namespace gpos;

	//-------------------------------------------------------------------------
	//	@class:
	//		CXformImplementLeftOuterCorrelatedApply
	//
	//	@doc:
	//		Transform LeftOuter correlated apply to physical LeftOuter correlated
	//		apply
	//
	//-------------------------------------------------------------------------
	class CXformImplementLeftOuterCorrelatedApply :
		public CXformImplementCorrelatedApply<CLogicalLeftOuterCorrelatedApply, CPhysicalCorrelatedLeftOuterNLJoin>
	{

		private:

			// private copy ctor
			CXformImplementLeftOuterCorrelatedApply(const CXformImplementLeftOuterCorrelatedApply &);

		public:

			// ctor
			explicit
			CXformImplementLeftOuterCorrelatedApply
				(
				IMemoryPool *pmp
				)
				:
				CXformImplementCorrelatedApply<CLogicalLeftOuterCorrelatedApply, CPhysicalCorrelatedLeftOuterNLJoin>(pmp)
			{}

			// dtor
			virtual
			~CXformImplementLeftOuterCorrelatedApply()
			{}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfImplementLeftOuterCorrelatedApply;
			}

			virtual
			const CHAR *SzId() const
			{
				return "CXformImplementLeftOuterCorrelatedApply";
			}

	}; // class CXformImplementLeftOuterCorrelatedApply

}

#endif // !GPOPT_CXformImplementLeftOuterCorrelatedApply_H

// EOF
