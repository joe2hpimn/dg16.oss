//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CScalarBoolOp.h
//
//	@doc:
//		Base class for all scalar boolean operators
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CScalarBoolOp_H
#define GPOPT_CScalarBoolOp_H

#include "gpos/base.h"
#include "gpopt/operators/CScalar.h"
#include "gpopt/base/CDrvdProp.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CScalarBoolOp
	//
	//	@doc:
	//		Scalar boolean operator
	//
	//---------------------------------------------------------------------------
	class CScalarBoolOp : public CScalar
	{

		public:

			// enum of boolean operators
			enum EBoolOperator
			{
				EboolopAnd,		// AND
				EboolopOr,		// OR
				EboolopNot,		// NOT

				EboolopSentinel
			};

		private:

			static
			const WCHAR m_rgwszBool[EboolopSentinel][30];

			// boolean operator
			EBoolOperator m_eboolop;

			// private copy ctor
			CScalarBoolOp(const CScalarBoolOp &);

		public:

			// ctor
			CScalarBoolOp
				(
				IMemoryPool *pmp,
				EBoolOperator eboolop
				)
				:
				CScalar(pmp),
				m_eboolop(eboolop)
			{
				GPOS_ASSERT(0 <= eboolop && EboolopSentinel > eboolop);
			}

			// dtor
			virtual
			~CScalarBoolOp() {}


			// ident accessors
			virtual
			EOperatorId Eopid() const
			{
				return EopScalarBoolOp;
			}

			// return a string for operator name
			virtual
			const CHAR *SzId() const
			{
				return "CScalarBoolOp";
			}

			// accessor
			EBoolOperator Eboolop() const
			{
				return m_eboolop;
			}

			// operator specific hash function
			ULONG UlHash() const;

			// match function
			BOOL FMatch(COperator *) const;

			// sensitivity to order of inputs
			BOOL FInputOrderSensitive() const
			{
				return !FCommutative(Eboolop());
			}

			// return a copy of the operator with remapped columns
			virtual
			COperator *PopCopyWithRemappedColumns
						(
						IMemoryPool *, //pmp,
						HMUlCr *, //phmulcr,
						BOOL //fMustExist
						)
			{
				return PopCopyDefault();
			}

			// conversion function
			static
			CScalarBoolOp *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopScalarBoolOp == pop->Eopid());

				return reinterpret_cast<CScalarBoolOp*>(pop);
			}

			// boolean expression evaluation
			virtual
			EBoolEvalResult Eber(DrgPul *pdrgpulChildren) const;

			// decide boolean operator commutativity
			static BOOL FCommutative(EBoolOperator eboolop);
			
			// the type of the scalar expression
			virtual 
			IMDId *PmdidType() const;

			// print
			virtual
			IOstream &OsPrint(IOstream &os) const;


	}; // class CScalarBoolOp

}


#endif // !GPOPT_CScalarBoolOp_H

// EOF
