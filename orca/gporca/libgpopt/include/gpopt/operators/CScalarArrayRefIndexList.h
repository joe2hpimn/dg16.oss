//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2014 Pivotal, Inc.
//
//	@filename:
//		CScalarArrayRefIndexList.h
//
//	@doc:
//		Class for scalar arrayref index list
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CScalarArrayRefIndexList_H
#define GPOPT_CScalarArrayRefIndexList_H

#include "gpos/base.h"
#include "naucrates/md/IMDId.h"

#include "gpopt/operators/CScalar.h"

namespace gpopt
{

	using namespace gpos;
	using namespace gpmd;

	//---------------------------------------------------------------------------
	//	@class:
	//		CScalarArrayRefIndexList
	//
	//	@doc:
	//		Scalar arrayref index list
	//
	//---------------------------------------------------------------------------
	class CScalarArrayRefIndexList : public CScalar
	{
		public:

			enum EIndexListType
			{
				EiltLower,		// lower index
				EiltUpper,		// upper index
				EiltSentinel
			};

		private:

			// index list type
			EIndexListType m_eilt;

			// private copy ctor
			CScalarArrayRefIndexList(const CScalarArrayRefIndexList &);

		public:

			// ctor
			CScalarArrayRefIndexList(IMemoryPool *pmp, EIndexListType eilt);

			// ident accessors
			virtual
			EOperatorId Eopid() const
			{
				return EopScalarArrayRefIndexList;
			}

			// operator name
			virtual
			const CHAR *SzId() const
			{
				return "CScalarArrayRefIndexList";
			}

			// index list type
			EIndexListType Eilt() const
			{
				return m_eilt;
			}

			// match function
			virtual
			BOOL FMatch(COperator *pop) const;

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

			// type of expression's result
			virtual
			IMDId *PmdidType() const
			{
				GPOS_ASSERT(!"Invalid function call: CScalarArrayRefIndexList::PmdidType()");
				return NULL;
			}

			// conversion function
			static
			CScalarArrayRefIndexList *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopScalarArrayRefIndexList == pop->Eopid());

				return dynamic_cast<CScalarArrayRefIndexList*>(pop);
			}

	}; // class CScalarArrayRefIndexList
}

#endif // !GPOPT_CScalarArrayRefIndexList_H

// EOF
