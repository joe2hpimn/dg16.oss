//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CScalarSubqueryNotExists.h
//
//	@doc:
//		Scalar subquery NOT EXISTS operator
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CScalarSubqueryNotExists_H
#define GPOPT_CScalarSubqueryNotExists_H

#include "gpos/base.h"

#include "gpopt/operators/CScalarSubqueryExistential.h"

namespace gpopt
{

	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CScalarSubqueryNotExists
	//
	//	@doc:
	//		Scalar subquery NOT EXISTS.
	//
	//---------------------------------------------------------------------------
	class CScalarSubqueryNotExists : public CScalarSubqueryExistential
	{

		private:

			// private copy ctor
			CScalarSubqueryNotExists(const CScalarSubqueryNotExists &);

		public:

			// ctor
			CScalarSubqueryNotExists
				(
				IMemoryPool *pmp
				)
				:
				CScalarSubqueryExistential(pmp)
			{}

			// dtor
			virtual
			~CScalarSubqueryNotExists()
			{}

			// ident accessors
			virtual
			EOperatorId Eopid() const
			{
				return EopScalarSubqueryNotExists;
			}

			// return a string for scalar subquery
			virtual
			const CHAR *SzId() const
			{
				return "CScalarSubqueryNotExists";
			}

			// conversion function
			static
			CScalarSubqueryNotExists *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopScalarSubqueryNotExists == pop->Eopid());

				return reinterpret_cast<CScalarSubqueryNotExists*>(pop);
			}

	}; // class CScalarSubqueryNotExists
}

#endif // !GPOPT_CScalarSubqueryNotExists_H

// EOF
