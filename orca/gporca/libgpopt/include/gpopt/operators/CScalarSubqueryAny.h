//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CScalarSubqueryAny.h
//
//	@doc:
//		Class for scalar subquery ANY operators
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CScalarSubqueryAny_H
#define GPOPT_CScalarSubqueryAny_H

#include "gpos/base.h"

#include "gpopt/operators/CScalarSubqueryQuantified.h"

namespace gpopt
{

	using namespace gpos;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CScalarSubqueryAny
	//
	//	@doc:
	//		Scalar subquery ANY.
	//		A scalar subquery ANY expression has two children: relational and scalar.
	//
	//---------------------------------------------------------------------------
	class CScalarSubqueryAny : public CScalarSubqueryQuantified
	{
		
		private:

			// private copy ctor
			CScalarSubqueryAny(const CScalarSubqueryAny &);
		
		public:
		
			// ctor
			CScalarSubqueryAny
				(
				IMemoryPool *pmp, 
				IMDId *pmdidScalarOp, 
				const CWStringConst *pstrScalarOp,
				const CColRef *pcr
				);

			// dtor
			virtual 
			~CScalarSubqueryAny()
			{}

			// ident accessors
			virtual 
			EOperatorId Eopid() const
			{
				return EopScalarSubqueryAny;
			}
			
			// return a string for scalar subquery
			virtual 
			const CHAR *SzId() const
			{
				return "CScalarSubqueryAny";
			}

			// return a copy of the operator with remapped columns
			virtual
			COperator *PopCopyWithRemappedColumns(IMemoryPool *pmp, HMUlCr *phmulcr, BOOL fMustExist);

			// conversion function
			static
			CScalarSubqueryAny *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopScalarSubqueryAny == pop->Eopid());
				
				return reinterpret_cast<CScalarSubqueryAny*>(pop);
			}

	}; // class CScalarSubqueryAny
}

#endif // !GPOPT_CScalarSubqueryAny_H

// EOF
