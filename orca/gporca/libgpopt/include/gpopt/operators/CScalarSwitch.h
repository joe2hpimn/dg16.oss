//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CScalarSwitch.h
//
//	@doc:
//		Scalar switch operator
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CScalarSwitch_H
#define GPOPT_CScalarSwitch_H

#include "gpos/base.h"
#include "gpopt/base/COptCtxt.h"
#include "gpopt/operators/CScalar.h"
#include "gpopt/base/CDrvdProp.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CScalarSwitch
	//
	//	@doc:
	//		Scalar switch operator. This corresponds to SQL case statments in the form
	//		(case expr when expr1 then ret1 when expr2 then ret2 ... else retdef end)
	//		The switch operator is represented as follows:
	//		Switch
	//		|-- expr1
	//		|-- SwitchCase
	//		|	|-- expr1
	//		|	+-- ret1
	//		|-- SwitchCase
	//		|	|-- expr2
	//		|	+-- ret2
	//		:
	//		+-- retdef
	//
	//---------------------------------------------------------------------------
	class CScalarSwitch : public CScalar
	{

		private:

			// return type
			IMDId *m_pmdidType;

			// is operator return type BOOL?
			BOOL m_fBoolReturnType;

			// private copy ctor
			CScalarSwitch(const CScalarSwitch &);

		public:

			// ctor
			CScalarSwitch(IMemoryPool *pmp, IMDId *pmdidType);

			// dtor
			virtual
			~CScalarSwitch();

			// ident accessors
			virtual
			EOperatorId Eopid() const
			{
				return EopScalarSwitch;
			}

			// return a string for operator name
			virtual
			const CHAR *SzId() const
			{
				return "CScalarSwitch";
			}

			// the type of the scalar expression
			virtual
			IMDId *PmdidType() const
			{
				return m_pmdidType;
			}

			// operator specific hash function
			virtual
			ULONG UlHash() const;

			// match function
			virtual BOOL
			FMatch(COperator *pop) const;

			// sensitivity to order of inputs
			virtual
			BOOL FInputOrderSensitive() const
			{
				return true;
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

			// boolean expression evaluation
			virtual
			EBoolEvalResult Eber
				(
				DrgPul *pdrgpulChildren
				)
				const
			{
				return EberNullOnAllNullChildren(pdrgpulChildren);
			}

			// conversion function
			static
			CScalarSwitch *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopScalarSwitch == pop->Eopid());

				return dynamic_cast<CScalarSwitch*>(pop);
			}

	}; // class CScalarSwitch

}


#endif // !GPOPT_CScalarSwitch_H

// EOF
