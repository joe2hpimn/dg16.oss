//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2008, 2009 Greenplum, Inc.
//
//	@filename:
//		CColumnFactory.h
//
//	@doc:
//		Column reference management; one instance per optimization
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CColumnFactory_H
#define GPOPT_CColumnFactory_H

#include "gpos/base.h"
#include "gpos/common/CList.h"
#include "gpos/common/CSyncHashtable.h"
#include "gpos/sync/CAtomicCounter.h"

#include "gpopt/spinlock.h"
#include "gpopt/base/CColRefSet.h"
#include "gpopt/metadata/CColumnDescriptor.h"


#include "naucrates/md/IMDId.h"
#include "naucrates/md/IMDType.h"

namespace gpopt
{
	class CExpression;

	using namespace gpos;
	using namespace gpmd;

	//---------------------------------------------------------------------------
	//	@class:
	//		CColumnFactory
	//
	//	@doc:
	//		Manager of column references; owns the memory in which they are
	//		allocated, provides factory methods to generate new column references
	//
	//---------------------------------------------------------------------------
	class CColumnFactory
	{
		private:

			// MTS memory pool
			IMemoryPool *m_pmp;

			// mapping between column id of computed column and a set of used column references
			HMCrCrs *m_phmcrcrs;

			// id counter
			CAtomicULONG m_aul;

			// hash table
			CSyncHashtable
				<CColRef,
				ULONG,
				CSpinlockColumnFactory> m_sht;

			// private copy ctor
			CColumnFactory(const CColumnFactory &);

			// implementation of factory methods
			CColRef *PcrCreate(const IMDType *, ULONG, const CName &);
			CColRef *PcrCreate
					(
					const CColumnDescriptor *pcoldesc,
					ULONG ulId,
					const CName &name,
					ULONG ulOpSource
					);

		public:

			// ctor
			CColumnFactory();

			// dtor
			~CColumnFactory();

			// initialize the hash map between computed column and used columns
			void Initialize();

			// create a column reference given only its type, used for computed columns
			CColRef *PcrCreate(const IMDType *pmdtype);

			// create column reference given its type and name
			CColRef *PcrCreate(const IMDType *pmdtype, const CName &name);

			// create a column reference given its descriptor and name
			CColRef *PcrCreate
				(
				const CColumnDescriptor *pcoldescr,
				const CName &name,
				ULONG ulOpSource
				);

			// create a column reference given its type, attno, nullability and name
			CColRef *PcrCreate
				(
				const IMDType *pmdtype,
				INT iAttno,
				BOOL fNullable,
				ULONG ulId,
				const CName &name,
				ULONG ulOpSource,
				ULONG ulWidth = ULONG_MAX
				);

			// create a column reference with the same type as passed column reference
			CColRef *PcrCreate
				(
				const CColRef *pcr
				)
			{
				return PcrCreate(pcr->Pmdtype());
			}

			// add mapping between computed column to its used columns
			void AddComputedToUsedColsMap(CExpression *pexpr);

			// lookup the set of used column references (if any) based on id of computed column
			const CColRefSet *PcrsUsedInComputedCol(const CColRef *pcrComputedCol);

			// create a copy of the given colref
			CColRef *PcrCopy(const CColRef* pcr);

			// lookup by id
			CColRef *PcrLookup(ULONG ulId);
			
			// destructor
			void Destroy(CColRef *);

	}; // class CColumnFactory
}

#endif // !GPOPT_CColumnFactory_H

// EOF
