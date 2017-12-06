//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CPhysicalLeftOuterHashJoin.h
//
//	@doc:
//		Left outer hash join operator
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CPhysicalLeftOuterHashJoin_H
#define GPOPT_CPhysicalLeftOuterHashJoin_H

#include "gpos/base.h"
#include "gpopt/operators/CPhysicalHashJoin.h"

namespace gpopt
{

	//---------------------------------------------------------------------------
	//	@class:
	//		CPhysicalLeftOuterHashJoin
	//
	//	@doc:
	//		Left outer hash join operator
	//
	//---------------------------------------------------------------------------
	class CPhysicalLeftOuterHashJoin : public CPhysicalHashJoin
	{

		private:

			// private copy ctor
			CPhysicalLeftOuterHashJoin(const CPhysicalLeftOuterHashJoin &);

		public:

			// ctor
			CPhysicalLeftOuterHashJoin
				(
				IMemoryPool *pmp,
				DrgPexpr *pdrgpexprOuterKeys,
				DrgPexpr *pdrgpexprInnerKeys
				);

			// dtor
			virtual
			~CPhysicalLeftOuterHashJoin();

			// ident accessors
			virtual
			EOperatorId Eopid() const
			{
				return EopPhysicalLeftOuterHashJoin;
			}

			 // return a string for operator name
			virtual
			const CHAR *SzId() const
			{
				return "CPhysicalLeftOuterHashJoin";
			}

			// conversion function
			static
			CPhysicalLeftOuterHashJoin *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopPhysicalLeftOuterHashJoin == pop->Eopid());

				return dynamic_cast<CPhysicalLeftOuterHashJoin*>(pop);
			}


	}; // class CPhysicalLeftOuterHashJoin

}

#endif // !GPOPT_CPhysicalLeftOuterHashJoin_H

// EOF
