//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2013 Pivotal, Inc.
//
//	@filename:
//		CLogicalExternalGet.h
//
//	@doc:
//		Logical external get operator
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CLogicalExternalGet_H
#define GPOPT_CLogicalExternalGet_H

#include "gpos/base.h"
#include "gpopt/operators/CLogicalGet.h"

namespace gpopt
{

	// fwd declarations
	class CTableDescriptor;
	class CName;
	class CColRefSet;

	//---------------------------------------------------------------------------
	//	@class:
	//		CLogicalExternalGet
	//
	//	@doc:
	//		Logical external get operator
	//
	//---------------------------------------------------------------------------
	class CLogicalExternalGet : public CLogicalGet
	{

		private:

			// private copy ctor
			CLogicalExternalGet(const CLogicalExternalGet &);

		public:

			// ctors
			explicit
			CLogicalExternalGet(IMemoryPool *pmp);

			CLogicalExternalGet
				(
				IMemoryPool *pmp,
				const CName *pnameAlias,
				CTableDescriptor *ptabdesc
				);

			CLogicalExternalGet
				(
				IMemoryPool *pmp,
				const CName *pnameAlias,
				CTableDescriptor *ptabdesc,
				DrgPcr *pdrgpcrOutput
				);

			// ident accessors
			virtual
			EOperatorId Eopid() const
			{
				return EopLogicalExternalGet;
			}

			// return a string for operator name
			virtual
			const CHAR *SzId() const
			{
				return "CLogicalExternalGet";
			}

			// match function
			virtual
			BOOL FMatch(COperator *pop) const;

			// return a copy of the operator with remapped columns
			virtual
			COperator *PopCopyWithRemappedColumns(IMemoryPool *pmp, HMUlCr *phmulcr, BOOL fMustExist);

			//-------------------------------------------------------------------------------------
			// Required Relational Properties
			//-------------------------------------------------------------------------------------

			// compute required stat columns of the n-th child
			virtual
			CColRefSet *PcrsStat
				(
				IMemoryPool *, // pmp,
				CExpressionHandle &, // exprhdl
				CColRefSet *, // pcrsInput
				ULONG // ulChildIndex
				)
				const
			{
				GPOS_ASSERT(!"CLogicalExternalGet has no children");
				return NULL;
			}

			//-------------------------------------------------------------------------------------
			// Transformations
			//-------------------------------------------------------------------------------------

			// candidate set of xforms
			virtual
			CXformSet *PxfsCandidates(IMemoryPool *pmp) const;

			//-------------------------------------------------------------------------------------
			//-------------------------------------------------------------------------------------
			//-------------------------------------------------------------------------------------

			// conversion function
			static
			CLogicalExternalGet *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopLogicalExternalGet == pop->Eopid());

				return dynamic_cast<CLogicalExternalGet*>(pop);
			}

	}; // class CLogicalExternalGet
}

#endif // !GPOPT_CLogicalExternalGet_H

// EOF
