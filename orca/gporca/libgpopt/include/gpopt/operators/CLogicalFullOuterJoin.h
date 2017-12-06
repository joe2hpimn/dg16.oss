//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2013 EMC Corp.
//
//	@filename:
//		CLogicalFullOuterJoin.h
//
//	@doc:
//		Full outer join operator
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CLogicalFullOuterJoin_H
#define GPOS_CLogicalFullOuterJoin_H

#include "gpos/base.h"
#include "gpopt/operators/CLogicalJoin.h"

namespace gpopt
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CLogicalFullOuterJoin
	//
	//	@doc:
	//		Full outer join operator
	//
	//---------------------------------------------------------------------------
	class CLogicalFullOuterJoin : public CLogicalJoin
	{
		private:

			// private copy ctor
			CLogicalFullOuterJoin(const CLogicalFullOuterJoin &);

		public:

			// ctor
			explicit
			CLogicalFullOuterJoin(IMemoryPool *pmp);

			// dtor
			virtual
			~CLogicalFullOuterJoin()
			{}

			// ident accessors
			virtual
			EOperatorId Eopid() const
			{
				return EopLogicalFullOuterJoin;
			}

			// return a string for operator name
			virtual
			const CHAR *SzId() const
			{
				return "CLogicalFullOuterJoin";
			}

			// return true if we can pull projections up past this operator from its given child
			virtual
			BOOL FCanPullProjectionsUp
				(
				ULONG //ulChildIndex
				) const
			{
				return false;
			}

			//-------------------------------------------------------------------------------------
			// Derived Relational Properties
			//-------------------------------------------------------------------------------------

			// derive not nullable output columns
			virtual
			CColRefSet *PcrsDeriveNotNull
				(
				IMemoryPool *pmp,
				CExpressionHandle & //exprhdl
				)
				const
			{
				// all output columns are nullable
				return GPOS_NEW(pmp) CColRefSet(pmp);
			}

			// derive max card
			virtual
			CMaxCard Maxcard(IMemoryPool *pmp, CExpressionHandle &exprhdl) const;

			// derive constraint property
			virtual
			CPropConstraint *PpcDeriveConstraint
				(
				IMemoryPool *pmp,
				CExpressionHandle & //exprhdl
				)
				const
			{
				return GPOS_NEW(pmp) CPropConstraint(pmp, GPOS_NEW(pmp) DrgPcrs(pmp), NULL /*pcnstr*/);
			}

			// promise level for stat derivation
			virtual
			EStatPromise Esp
				(
				CExpressionHandle & //exprhdl
				)
				const
			{
				// Low to prefer the stats coming from the equivalent UNION expression
				return EspLow;
			}

			//-------------------------------------------------------------------------------------
			// Transformations
			//-------------------------------------------------------------------------------------

			// candidate set of xforms
			CXformSet *PxfsCandidates(IMemoryPool *pmp) const;

			//-------------------------------------------------------------------------------------
			//-------------------------------------------------------------------------------------
			//-------------------------------------------------------------------------------------

			// conversion function
			static
			CLogicalFullOuterJoin *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopLogicalFullOuterJoin == pop->Eopid());

				return dynamic_cast<CLogicalFullOuterJoin*>(pop);
			}

	}; // class CLogicalFullOuterJoin

}


#endif // !GPOS_CLogicalFullOuterJoin_H

// EOF
