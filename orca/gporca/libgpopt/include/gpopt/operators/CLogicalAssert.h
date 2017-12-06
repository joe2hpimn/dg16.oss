//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CLogicalAssert.h
//
//	@doc:
//		Assert operator
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CLogicalAssert_H
#define GPOS_CLogicalAssert_H

#include "gpos/base.h"

#include "naucrates/dxl/errorcodes.h"

#include "gpopt/operators/CExpressionHandle.h"
#include "gpopt/operators/CLogicalUnary.h"

namespace gpopt
{

	//---------------------------------------------------------------------------
	//	@class:
	//		CLogicalAssert
	//
	//	@doc:
	//		Assert operator
	//
	//---------------------------------------------------------------------------
	class CLogicalAssert : public CLogicalUnary
	{
			
		private:
			
			// exception
			CException *m_pexc;
			
			// private copy ctor
			CLogicalAssert(const CLogicalAssert &);

		public:

			// ctors
			explicit
			CLogicalAssert(IMemoryPool *pmp);
			
			CLogicalAssert(IMemoryPool *pmp, CException *pexc);

			// dtor
			virtual
			~CLogicalAssert()
			{
				GPOS_DELETE(m_pexc);
			}

			// ident accessors
			virtual 
			EOperatorId Eopid() const
			{
				return EopLogicalAssert;
			}
						
			// name of operator
			virtual 
			const CHAR *SzId() const
			{
				return "CLogicalAssert";
			}

			// exception
			CException *Pexc() const
			{
				return m_pexc;
			}
						
			// match function; 
			virtual 
			BOOL FMatch(COperator *pop) const;
			
			//-------------------------------------------------------------------------------------
			// Derived Relational Properties
			//-------------------------------------------------------------------------------------

			// derive output columns
			virtual
			CColRefSet *PcrsDeriveOutput(IMemoryPool *,CExpressionHandle &);
			
			// dervive keys
			virtual 
			CKeyCollection *PkcDeriveKeys(IMemoryPool *pmp, CExpressionHandle &exprhdl) const;		
					
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
			virtual
			CXformSet *PxfsCandidates(IMemoryPool *) const;

			//-------------------------------------------------------------------------------------
			//-------------------------------------------------------------------------------------
			//-------------------------------------------------------------------------------------

			// conversion function
			static
			CLogicalAssert *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopLogicalAssert == pop->Eopid());
				
				return reinterpret_cast<CLogicalAssert*>(pop);
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
			
			// debug print
			IOstream &OsPrint(IOstream &os) const;

	}; // class CLogicalAssert

}

#endif // !GPOS_CLogicalAssert_H

// EOF
