//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CLogicalDifferenceAll.h
//
//	@doc:
//		Logical Difference all operator (Difference all does not remove
//		duplicates from the left child)
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CLogicalDifferenceAll_H
#define GPOPT_CLogicalDifferenceAll_H

#include "gpos/base.h"

#include "gpopt/operators/CExpressionHandle.h"
#include "gpopt/operators/CLogicalSetOp.h"

namespace gpopt
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CLogicalDifferenceAll
	//
	//	@doc:
	//		Difference all operators
	//
	//---------------------------------------------------------------------------
	class CLogicalDifferenceAll : public CLogicalSetOp
	{

		private:

			// private copy ctor
			CLogicalDifferenceAll(const CLogicalDifferenceAll &);

		public:

			// ctor
			explicit
			CLogicalDifferenceAll(IMemoryPool *pmp);

			CLogicalDifferenceAll
				(
				IMemoryPool *pmp,
				DrgPcr *pdrgpcrOutput,
				DrgDrgPcr *pdrgpdrgpcrInput
				);

			// dtor
			virtual
			~CLogicalDifferenceAll();

			// ident accessors
			virtual
			EOperatorId Eopid() const
			{
				return EopLogicalDifferenceAll;
			}

			// return a string for operator name
			virtual
			const CHAR *SzId() const
			{
				return "CLogicalDifferenceAll";
			}

			// sensitivity to order of inputs
			BOOL FInputOrderSensitive() const
			{
				return true;
			}

			// return a copy of the operator with remapped columns
			virtual
			COperator *PopCopyWithRemappedColumns(IMemoryPool *pmp, HMUlCr *phmulcr, BOOL fMustExist);

			//-------------------------------------------------------------------------------------
			// Derived Relational Properties
			//-------------------------------------------------------------------------------------

			// derive max card
			virtual
			CMaxCard Maxcard(IMemoryPool *pmp, CExpressionHandle &exprhdl) const;

			// derive key collections
			virtual
			CKeyCollection *PkcDeriveKeys(IMemoryPool *pmp, CExpressionHandle &exprhdl) const;

			// derive constraint property
			virtual
			CPropConstraint *PpcDeriveConstraint
				(
				IMemoryPool *, //pmp,
				CExpressionHandle &exprhdl
				)
				const
			{
				return PpcDeriveConstraintPassThru(exprhdl, 0 /*ulChild*/);
			}

			//-------------------------------------------------------------------------------------
			// Transformations
			//-------------------------------------------------------------------------------------

			// candidate set of xforms
			CXformSet *PxfsCandidates(IMemoryPool *pmp) const;

			// stat promise
			virtual
			EStatPromise Esp
				(
				CExpressionHandle &  // exprhdl
				)
				const
			{
				return CLogical::EspLow;
			}

			// derive statistics
			virtual
			IStatistics *PstatsDerive
				(
				IMemoryPool *pmp,
				CExpressionHandle &exprhdl,
				DrgPstat *pdrgpstatCtxt
				)
				const;

			// conversion function
			static
			CLogicalDifferenceAll *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopLogicalDifferenceAll == pop->Eopid());

				return reinterpret_cast<CLogicalDifferenceAll*>(pop);
			}

	}; // class CLogicalDifferenceAll

}

#endif // !GPOPT_CLogicalDifferenceAll_H

// EOF
