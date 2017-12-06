//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CXformImplementInnerCorrelatedApply.h
//
//	@doc:
//		Transform inner correlated apply to physical inner correlated apply
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformImplementInnerCorrelatedApply_H
#define GPOPT_CXformImplementInnerCorrelatedApply_H

#include "gpos/base.h"
#include "gpopt/xforms/CXformImplementCorrelatedApply.h"

namespace gpopt
{
	using namespace gpos;

	//-------------------------------------------------------------------------
	//	@class:
	//		CXformImplementInnerCorrelatedApply
	//
	//	@doc:
	//		Transform inner correlated apply to physical inner correlated apply
	//
	//-------------------------------------------------------------------------
	class CXformImplementInnerCorrelatedApply :
		public CXformImplementCorrelatedApply<CLogicalInnerCorrelatedApply, CPhysicalCorrelatedInnerNLJoin>
	{

		private:

			// private copy ctor
			CXformImplementInnerCorrelatedApply(const CXformImplementInnerCorrelatedApply &);

		public:

			// ctor
			explicit
			CXformImplementInnerCorrelatedApply
				(
				IMemoryPool *pmp
				)
				:
				CXformImplementCorrelatedApply<CLogicalInnerCorrelatedApply, CPhysicalCorrelatedInnerNLJoin>(pmp)
			{}

			// dtor
			virtual
			~CXformImplementInnerCorrelatedApply()
			{}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfImplementInnerCorrelatedApply;
			}

			virtual
			const CHAR *SzId() const
			{
				return "CXformImplementInnerCorrelatedApply";
			}

	}; // class CXformImplementInnerCorrelatedApply

}

#endif // !GPOPT_CXformImplementInnerCorrelatedApply_H

// EOF
