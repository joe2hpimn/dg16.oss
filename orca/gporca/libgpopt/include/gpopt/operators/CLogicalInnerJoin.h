//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CLogicalInnerJoin.h
//
//	@doc:
//		Inner join operator
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CLogicalInnerJoin_H
#define GPOS_CLogicalInnerJoin_H

#include "gpos/base.h"
#include "gpopt/operators/CLogicalJoin.h"

namespace gpopt
{
	// fwd declaration
	class CColRefSet;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CLogicalInnerJoin
	//
	//	@doc:
	//		Inner join operator
	//
	//---------------------------------------------------------------------------
	class CLogicalInnerJoin : public CLogicalJoin
	{
		private:

			// private copy ctor
			CLogicalInnerJoin(const CLogicalInnerJoin &);

		public:

			// ctor
			explicit
			CLogicalInnerJoin(IMemoryPool *pmp);

			// dtor
			virtual ~CLogicalInnerJoin() 
			{}


			// ident accessors
			virtual 
			EOperatorId Eopid() const
			{
				return EopLogicalInnerJoin;
			}
			
			// return a string for operator name
			virtual 
			const CHAR *SzId() const
			{
				return "CLogicalInnerJoin";
			}

			//-------------------------------------------------------------------------------------
			// Derived Relational Properties
			//-------------------------------------------------------------------------------------

			// derive not nullable columns
			virtual
			CColRefSet *PcrsDeriveNotNull
				(
				IMemoryPool *pmp,
				CExpressionHandle &exprhdl
				)
				const
			{
				return PcrsDeriveNotNullCombineLogical(pmp, exprhdl);
			}

			// derive max card
			virtual
			CMaxCard Maxcard(IMemoryPool *pmp, CExpressionHandle &exprhdl) const;

			// derive constraint property
			virtual
			CPropConstraint *PpcDeriveConstraint
				(
				IMemoryPool *pmp,
				CExpressionHandle &exprhdl
				)
				const
			{
				return PpcDeriveConstraintFromPredicates(pmp, exprhdl);
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
			CLogicalInnerJoin *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopLogicalInnerJoin == pop->Eopid());
				
				return dynamic_cast<CLogicalInnerJoin*>(pop);
			}

			// determine if an innerJoin group expression has
			// less conjuncts than another
			static
			BOOL FFewerConj
				(
				IMemoryPool *pmp,
				CGroupExpression *pgexprFst,
				CGroupExpression *pgexprSnd
				);


	}; // class CLogicalInnerJoin

}


#endif // !GPOS_CLogicalInnerJoin_H

// EOF
