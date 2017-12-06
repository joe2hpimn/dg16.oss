//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2014 Pivotal Inc..
//
//	@filename:
//		CXformImplementLeftAntiSemiCorrelatedApplyNotIn.h
//
//	@doc:
//		Transform left anti semi correlated apply with NOT-IN/ALL semantics
//		to physical left anti semi correlated join
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformImplementLeftAntiSemiCorrelatedApplyNotIn_H
#define GPOPT_CXformImplementLeftAntiSemiCorrelatedApplyNotIn_H

#include "gpos/base.h"
#include "gpopt/xforms/CXformImplementCorrelatedApply.h"

namespace gpopt
{
	using namespace gpos;

	//-------------------------------------------------------------------------
	//	@class:
	//		CXformImplementLeftAntiSemiCorrelatedApplyNotIn
	//
	//	@doc:
	//		Transform left anti semi correlated apply with NOT-IN/ALL semantics
	//		to physical left anti semi correlated join
	//
	//-------------------------------------------------------------------------
	class CXformImplementLeftAntiSemiCorrelatedApplyNotIn :
		public CXformImplementCorrelatedApply<CLogicalLeftAntiSemiCorrelatedApplyNotIn, CPhysicalCorrelatedNotInLeftAntiSemiNLJoin>
	{

		private:

			// private copy ctor
			CXformImplementLeftAntiSemiCorrelatedApplyNotIn(const CXformImplementLeftAntiSemiCorrelatedApplyNotIn &);

		public:

			// ctor
			explicit
			CXformImplementLeftAntiSemiCorrelatedApplyNotIn
				(
				IMemoryPool *pmp
				)
				:
				CXformImplementCorrelatedApply<CLogicalLeftAntiSemiCorrelatedApplyNotIn, CPhysicalCorrelatedNotInLeftAntiSemiNLJoin>(pmp)
			{}

			// dtor
			virtual
			~CXformImplementLeftAntiSemiCorrelatedApplyNotIn()
			{}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfImplementLeftAntiSemiCorrelatedApplyNotIn;
			}

			virtual
			const CHAR *SzId() const
			{
				return "CXformImplementLeftAntiSemiCorrelatedApplyNotIn";
			}

	}; // class CXformImplementLeftAntiSemiCorrelatedApplyNotIn

}

#endif // !GPOPT_CXformImplementLeftAntiSemiCorrelatedApplyNotIn_H

// EOF
