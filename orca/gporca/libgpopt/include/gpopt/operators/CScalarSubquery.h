//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CScalarSubquery.h
//
//	@doc:
//		Class for scalar subqueries
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CScalarSubquery_H
#define GPOPT_CScalarSubquery_H

#include "gpos/base.h"

#include "gpopt/operators/CScalar.h"

namespace gpopt
{

	using namespace gpos;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CScalarSubquery
	//
	//	@doc:
	//		Scalar subquery
	//
	//---------------------------------------------------------------------------
	class CScalarSubquery : public CScalar
	{
		private:

			// computed column reference
			const CColRef *m_pcr;
		
			// is subquery generated from existential subquery?
			BOOL m_fGeneratedByExist;

			// is subquery generated from quantified subquery?
			BOOL m_fGeneratedByQuantified;

			// private copy ctor
			CScalarSubquery(const CScalarSubquery &);
		
		public:
		
			// ctor
			CScalarSubquery(IMemoryPool *pmp, const CColRef *pcr, BOOL fGeneratedByExist, BOOL fGeneratedByQuantified);

			// dtor
			virtual 
			~CScalarSubquery();

			// ident accessors
			virtual 
			EOperatorId Eopid() const
			{
				return EopScalarSubquery;
			}
			
			// return a string for scalar subquery
			virtual 
			const CHAR *SzId() const
			{
				return "CScalarSubquery";
			}

			// accessor to computed column reference
			const CColRef *Pcr() const
			{
				return m_pcr;
			}

			// the type of the scalar expression
			virtual 
			IMDId *PmdidType() const;
			
			// operator specific hash function
			ULONG UlHash() const;
			
			// match function
			BOOL FMatch(COperator *pop) const;
			
			// sensitivity to order of inputs
			BOOL FInputOrderSensitive() const
			{
				return true;
			}
			
			// return a copy of the operator with remapped columns
			virtual
			COperator *PopCopyWithRemappedColumns(IMemoryPool *pmp, HMUlCr *phmulcr, BOOL fMustExist);

			// return locally used columns
			virtual
			CColRefSet *PcrsUsed(IMemoryPool *pmp, CExpressionHandle &exprhdl);

			// is subquery generated from existential subquery?
			BOOL FGeneratedByExist() const
			{
				return m_fGeneratedByExist;
			}

			// is subquery generated from quantified subquery?
			BOOL FGeneratedByQuantified() const
			{
				return m_fGeneratedByQuantified;
			}

			// derive partition consumer info
			virtual
			CPartInfo *PpartinfoDerive
				(
				IMemoryPool *pmp, 
				CExpressionHandle &exprhdl
				) 
				const;

			// conversion function
			static
			CScalarSubquery *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopScalarSubquery == pop->Eopid());
				
				return reinterpret_cast<CScalarSubquery*>(pop);
			}
			
			// print
			virtual 
			IOstream &OsPrint(IOstream &os) const;

	}; // class CScalarSubquery
}

#endif // !GPOPT_CScalarSubquery_H

// EOF
