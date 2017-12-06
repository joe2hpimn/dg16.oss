//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CScalarDMLAction.h
//
//	@doc:
//		Scalar DML action operator
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CScalarDMLAction_H
#define GPOPT_CScalarDMLAction_H

#include "gpos/base.h"
#include "gpopt/base/COptCtxt.h"
#include "gpopt/operators/CScalar.h"
#include "gpopt/base/CDrvdProp.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CScalarDMLAction
	//
	//	@doc:
	//		Scalar casting operator
	//
	//---------------------------------------------------------------------------
	class CScalarDMLAction : public CScalar
	{

		private:

			// private copy ctor
			CScalarDMLAction(const CScalarDMLAction &);

		public:

			// dml action specification
			enum EDMLAction
			{
				EdmlactionDelete,
				EdmlactionInsert
			};
			
			// ctor
			CScalarDMLAction
				(
				IMemoryPool *pmp
				)
				:
				CScalar(pmp)
			{}

			// dtor
			virtual
			~CScalarDMLAction() 
			{}
			// ident accessors

			// the type of the scalar expression
			virtual 
			IMDId *PmdidType() const;

			virtual
			EOperatorId Eopid() const
			{
				return EopScalarDMLAction;
			}

			// return a string for operator name
			virtual
			const CHAR *SzId() const
			{
				return "CScalarDMLAction";
			}

			// match function
			virtual
			BOOL FMatch(COperator *pop) const;

			// sensitivity to order of inputs
			virtual
			BOOL FInputOrderSensitive() const
			{
				return false;
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
			CScalarDMLAction *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopScalarDMLAction == pop->Eopid());

				return dynamic_cast<CScalarDMLAction*>(pop);
			}

	}; // class CScalarDMLAction
}

#endif // !GPOPT_CScalarDMLAction_H

// EOF
