//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CIndexDescriptor.h
//
//	@doc:
//		Base class for index descriptor
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CIndexDescriptor_H
#define GPOPT_CIndexDescriptor_H

#include "gpos/base.h"
#include "gpos/common/CDynamicPtrArray.h"

#include "naucrates/md/IMDId.h"
#include "naucrates/md/IMDIndex.h"

#include "gpopt/base/CColRef.h"
#include "gpopt/metadata/CTableDescriptor.h"
#include "gpopt/metadata/CColumnDescriptor.h"

namespace gpopt
{
	using namespace gpos;
	using namespace gpmd;

	//---------------------------------------------------------------------------
	//	@class:
	//		CIndexDescriptor
	//
	//	@doc:
	//		Base class for index descriptor
	//
	//---------------------------------------------------------------------------
	class CIndexDescriptor : public CRefCount
	{
		private:

			// mdid of the index
			IMDId *m_pmdidIndex;

			// mdid of the table
			IMDId *m_pmdidTable;

			// name of index
			CName m_name;

			// array of index key columns
			DrgPcoldesc *m_pdrgpcoldescKeyCols;

			// array of index included columns
			DrgPcoldesc *m_pdrgpcoldescIncludedCols;

			// clustered index
			BOOL m_fClustered;

			// private copy ctor
			CIndexDescriptor(const CIndexDescriptor &);

		public:

			// ctor
			CIndexDescriptor
				(
				IMemoryPool *pmp,
				IMDId *pmdidIndex,
				IMDId *pmdidTable,
				const CName &name,
				DrgPcoldesc *pdrgcoldescKeyCols,
				DrgPcoldesc *pdrgcoldescIncludedCols,
				BOOL fClustered
				);

			// dtor
			virtual
			~CIndexDescriptor();

			// number of key columns
			ULONG UlKeys() const;

			// number of included columns
			ULONG UlIncludedColumns() const;

			// index mdid accessor
			IMDId *Pmdid() const
			{
				return m_pmdidIndex;
			}

			// table mdid
			IMDId *PmdidTable() const
			{
				return m_pmdidTable;
			}

			// index name
			const CName &Name() const
			{
				return m_name;
			}

			// key column descriptors
			DrgPcoldesc *PdrgpcoldescKey() const
			{
				return m_pdrgpcoldescKeyCols;
			}

			// included column descriptors
			DrgPcoldesc *PdrgpcoldescIncluded() const
			{
				return m_pdrgpcoldescIncludedCols;
			}
			
			// is index clustered
			BOOL FClustered() const
			{
				return m_fClustered;
			}

			// create an index descriptor
			static CIndexDescriptor *Pindexdesc
				(
				IMemoryPool *pmp,
				const CTableDescriptor *ptabdesc,
				const IMDIndex *pmdindex
				);

#ifdef GPOS_DEBUG
			IOstream &OsPrint(IOstream &) const;
#endif // GPOS_DEBUG

	}; // class CIndexDescriptor
}

#endif // !GPOPT_CIndexDescriptor_H

// EOF
