//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CScalarSubqueryAll.h
//
//	@doc:
//		Class for scalar subquery ALL operators
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CScalarSubqueryAll_H
#define GPOPT_CScalarSubqueryAll_H

#include "gpos/base.h"

#include "gpopt/operators/CScalarSubqueryQuantified.h"

namespace gpopt
{

	using namespace gpos;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CScalarSubqueryAll
	//
	//	@doc:
	//		Scalar subquery ALL
	//		A scalar subquery ALL expression has two children: relational and scalar.
	//
	//---------------------------------------------------------------------------
	class CScalarSubqueryAll : public CScalarSubqueryQuantified
	{

		private:		
		
			// private copy ctor
			CScalarSubqueryAll(const CScalarSubqueryAll &);
		
		public:
		
			// ctor
			CScalarSubqueryAll
				(
				IMemoryPool *pmp, 
				IMDId *pmdidScalarOp, 
				const CWStringConst *pstrScalarOp,
				const CColRef *pcr
				);

			// dtor
			virtual 
			~CScalarSubqueryAll()
			{}

			// ident accessors
			virtual 
			EOperatorId Eopid() const
			{
				return EopScalarSubqueryAll;
			}
			
			// return a string for scalar subquery
			virtual 
			const CHAR *SzId() const
			{
				return "CScalarSubqueryAll";
			}

			// return a copy of the operator with remapped columns
			virtual
			COperator *PopCopyWithRemappedColumns(IMemoryPool *pmp, HMUlCr *phmulcr, BOOL fMustExist);

			// conversion function
			static
			CScalarSubqueryAll *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopScalarSubqueryAll == pop->Eopid());
				
				return reinterpret_cast<CScalarSubqueryAll*>(pop);
			}

	}; // class CScalarSubqueryAll
}

#endif // !GPOPT_CScalarSubqueryAll_H

// EOF
