//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2013 Pivotal, Inc.
//
//	@filename:
//		CXformPushGbDedupBelowJoin.h
//
//	@doc:
//		Push dedup group by below join transform
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformPushGbDedupBelowJoin_H
#define GPOPT_CXformPushGbDedupBelowJoin_H

#include "gpos/base.h"
#include "gpopt/xforms/CXformPushGbBelowJoin.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CXformPushGbDedupBelowJoin
	//
	//	@doc:
	//		Push dedup group by below join transform
	//
	//---------------------------------------------------------------------------
	class CXformPushGbDedupBelowJoin : public CXformPushGbBelowJoin
	{

		private:

			// private copy ctor
			CXformPushGbDedupBelowJoin(const CXformPushGbDedupBelowJoin &);

		public:

			// ctor
			explicit
			CXformPushGbDedupBelowJoin(IMemoryPool *pmp);

			// dtor
			virtual
			~CXformPushGbDedupBelowJoin()
			{}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfPushGbDedupBelowJoin;
			}

			virtual
			const CHAR *SzId() const
			{
				return "CXformPushGbDedupBelowJoin";
			}

	}; // class CXformPushGbDedupBelowJoin

}

#endif // !GPOPT_CXformPushGbDedupBelowJoin_H

// EOF
