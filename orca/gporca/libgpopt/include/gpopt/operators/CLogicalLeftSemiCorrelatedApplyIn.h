//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2014 Pivotal Inc.
//
//	@filename:
//		CLogicalLeftSemiCorrelatedApplyIn.h
//
//	@doc:
//		Logical Left Semi Correlated Apply operator;
//		a variant of left semi apply that captures the need to implement a
//		correlated-execution strategy on the physical side
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CLogicalLeftSemiCorrelatedApplyIn_H
#define GPOPT_CLogicalLeftSemiCorrelatedApplyIn_H

#include "gpos/base.h"
#include "gpopt/operators/CLogicalLeftSemiApplyIn.h"

namespace gpopt
{


	//---------------------------------------------------------------------------
	//	@class:
	//		CLogicalLeftSemiCorrelatedApplyIn
	//
	//	@doc:
	//		Logical Apply operator used in scalar subquery transformations
	//
	//---------------------------------------------------------------------------
	class CLogicalLeftSemiCorrelatedApplyIn : public CLogicalLeftSemiApplyIn
	{

		private:

			// private copy ctor
			CLogicalLeftSemiCorrelatedApplyIn(const CLogicalLeftSemiCorrelatedApplyIn &);

		public:

			// ctor for patterns
			explicit
			CLogicalLeftSemiCorrelatedApplyIn(IMemoryPool *pmp);

			// ctor
			CLogicalLeftSemiCorrelatedApplyIn(IMemoryPool *pmp,  DrgPcr *pdrgpcrInner, EOperatorId eopidOriginSubq);

			// dtor
			virtual
			~CLogicalLeftSemiCorrelatedApplyIn()
			{}

			// ident accessors
			virtual
			EOperatorId Eopid() const
			{
				return EopLogicalLeftSemiCorrelatedApplyIn;
			}

			// return a string for operator name
			virtual
			const CHAR *SzId() const
			{
				return "CLogicalLeftSemiCorrelatedApplyIn";
			}

			// applicable transformations
			virtual
			CXformSet *PxfsCandidates(IMemoryPool *pmp) const;

			// return true if operator is a correlated apply
			virtual
			BOOL FCorrelated() const
			{
				return true;
			}

			// return a copy of the operator with remapped columns
			virtual
			COperator *PopCopyWithRemappedColumns(IMemoryPool *pmp, HMUlCr *phmulcr, BOOL fMustExist);

			// conversion function
			static
			CLogicalLeftSemiCorrelatedApplyIn *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopLogicalLeftSemiCorrelatedApplyIn == pop->Eopid());

				return dynamic_cast<CLogicalLeftSemiCorrelatedApplyIn*>(pop);
			}

	}; // class CLogicalLeftSemiCorrelatedApplyIn

}


#endif // !GPOPT_CLogicalLeftSemiCorrelatedApplyIn_H

// EOF
