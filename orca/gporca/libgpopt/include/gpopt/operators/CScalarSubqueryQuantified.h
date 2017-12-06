//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CScalarSubqueryQuantified.h
//
//	@doc:
//		Parent class for quantified subquery operators
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CScalarSubqueryQuantified_H
#define GPOPT_CScalarSubqueryQuantified_H

#include "gpos/base.h"

#include "gpopt/operators/CScalar.h"

namespace gpopt
{

	using namespace gpos;

	// fwd declarations
	class CExpressionHandle;

	//---------------------------------------------------------------------------
	//	@class:
	//		CScalarSubqueryQuantified
	//
	//	@doc:
	//		Parent class for quantified subquery operators (ALL/ANY subqueries);
	//		A quantified subquery expression has two children:
	//		- Logical child: the inner logical expression
	//		- Scalar child:	the scalar expression in the outer expression that
	//		is used in quantified comparison;
	//
	//		Example: SELECT * from R where a+b = ANY (SELECT c from S);
	//		- logical child: (SELECT c from S)
	//		- scalar child : (a+b)
	//
	//---------------------------------------------------------------------------
	class CScalarSubqueryQuantified : public CScalar
	{
		private:

			// id of comparison operator
			IMDId *m_pmdidScalarOp;

			// name of comparison operator
			const CWStringConst *m_pstrScalarOp;

			// column reference used in comparison
			const CColRef *m_pcr;

			// private copy ctor
			CScalarSubqueryQuantified(const CScalarSubqueryQuantified &);

		protected:

			// ctor
			CScalarSubqueryQuantified
				(
				IMemoryPool *pmp,
				IMDId *pmdidScalarOp,
				const CWStringConst *pstrScalarOp,
				const CColRef *pcr
				);

			// dtor
			virtual
			~CScalarSubqueryQuantified();

		public:

			// operator mdid accessor
			IMDId *PmdidOp() const;

			// operator name accessor
			const CWStringConst *PstrOp() const;

			// column reference accessor
			const CColRef *Pcr() const
			{
				return m_pcr;
			}

			// return the type of the scalar expression
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

			// return locally used columns
			virtual
			CColRefSet *PcrsUsed(IMemoryPool *pmp, CExpressionHandle &exprhdl);

			// derive partition consumer info
			virtual
			CPartInfo *PpartinfoDerive
				(
				IMemoryPool *pmp, 
				CExpressionHandle &exprhdl
				) 
				const;
			
			// build an expression for the quantified comparison of the subquery
			virtual
			CExpression *PexprSubqueryPred
				(
				IMemoryPool *pmp,
				CExpressionHandle &exprhdl
				)
				const;

			// conversion function
			static
			CScalarSubqueryQuantified *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopScalarSubqueryAny == pop->Eopid() ||
							EopScalarSubqueryAll == pop->Eopid());

				return reinterpret_cast<CScalarSubqueryQuantified*>(pop);
			}

			// print
			virtual
			IOstream &OsPrint(IOstream &os) const;

	}; // class CScalarSubqueryQuantified
}

#endif // !GPOPT_CScalarSubqueryQuantified_H

// EOF
