//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CPattern.h
//
//	@doc:
//		Base class for all pattern operators
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CPattern_H
#define GPOPT_CPattern_H

#include "gpos/base.h"
#include "gpopt/operators/COperator.h"

namespace gpopt
{
	using namespace gpos;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CPattern
	//
	//	@doc:
	//		base class for all pattern operators
	//
	//---------------------------------------------------------------------------
	class CPattern : public COperator
	{

		private:

			// private copy ctor
			CPattern(const CPattern &);

		public:
		
			// ctor
			explicit
			CPattern
				(
				IMemoryPool *pmp
				)
				: 
				COperator(pmp)
			{}

			// dtor
			virtual 
			~CPattern() {}

			// type of operator
			virtual
			BOOL FPattern() const
			{
				GPOS_ASSERT(!FPhysical() && !FScalar() && !FLogical());
				return true;
			}

			// create derived properties container
			virtual
			CDrvdProp *PdpCreate(IMemoryPool *pmp) const;

			// create required properties container
			virtual
			CReqdProp *PrpCreate(IMemoryPool *pmp) const;
						
			// match function
			BOOL FMatch(COperator *) const;
			
			// sensitivity to order of inputs
			BOOL FInputOrderSensitive() const;

			// check if operator is a pattern leaf
			virtual
			BOOL FLeaf() const = 0;
			
			// return a copy of the operator with remapped columns
			virtual
			COperator *PopCopyWithRemappedColumns
						(
						IMemoryPool *pmp,
						HMUlCr *phmulcr,
						BOOL fMustExist
						);

			// conversion function
			static
			CPattern *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(pop->FPattern());

				return reinterpret_cast<CPattern*>(pop);
			}

			// helper to check multi-node pattern
			static
			BOOL FMultiNode
				(
				COperator *pop
				)
			{
				return COperator::EopPatternMultiLeaf == pop->Eopid() ||
					COperator::EopPatternMultiTree == pop->Eopid();
			}

	}; // class CPattern

}


#endif // !GPOPT_CPattern_H

// EOF
