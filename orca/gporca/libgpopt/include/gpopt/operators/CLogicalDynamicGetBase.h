//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2013 EMC Corp.
//
//	@filename:
//		CLogicalDynamicGetBase.h
//
//	@doc:
//		Base class for dynamic table accessors for partitioned tables
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CLogicalDynamicGetBase_H
#define GPOPT_CLogicalDynamicGetBase_H

#include "gpos/base.h"
#include "gpopt/operators/CLogical.h"

namespace gpopt
{
	
	// fwd declarations
	class CTableDescriptor;
	class CName;
	class CColRefSet;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CLogicalDynamicGetBase
	//
	//	@doc:
	//		Dynamic table accessor base class
	//
	//---------------------------------------------------------------------------
	class CLogicalDynamicGetBase : public CLogical
	{

		protected:

			// alias for table
			const CName *m_pnameAlias;

			// table descriptor
			CTableDescriptor *m_ptabdesc;
			
			// dynamic scan id
			ULONG m_ulScanId;
			
			// output columns
			DrgPcr *m_pdrgpcrOutput;
			
			// partition keys
			DrgDrgPcr *m_pdrgpdrgpcrPart;

			// secondary scan id in case of a partial scan
			ULONG m_ulSecondaryScanId;
			
			// is scan partial -- only used with heterogeneous indexes defined on a subset of partitions
			BOOL m_fPartial;
			
			// dynamic get part constraint
			CPartConstraint *m_ppartcnstr;
			
			// relation part constraint
			CPartConstraint *m_ppartcnstrRel;
			
			// distribution columns (empty for master only tables)
			CColRefSet *m_pcrsDist;

			// private copy ctor
			CLogicalDynamicGetBase(const CLogicalDynamicGetBase &);

			// given a colrefset from a table, get colids and attno
			void
			ExtractColIdsAttno(IMemoryPool *pmp, CColRefSet *pcrs, DrgPul *pdrgpulColIds, DrgPul *pdrgpulPos) const;

			// derive stats from base table using filters on partition and/or index columns
			IStatistics *PstatsDeriveFilter(IMemoryPool *pmp, CExpressionHandle &exprhdl, CExpression *pexprFilter) const;

		public:
		
			// ctors
			explicit
			CLogicalDynamicGetBase(IMemoryPool *pmp);

			CLogicalDynamicGetBase
				(
				IMemoryPool *pmp,
				const CName *pnameAlias,
				CTableDescriptor *ptabdesc,
				ULONG ulScanId,
				DrgPcr *pdrgpcr,
				DrgDrgPcr *pdrgpdrgpcrPart,
				ULONG ulSecondaryScanId,
				BOOL fPartial,
				CPartConstraint *ppartcnstr, 
				CPartConstraint *ppartcnstrRel
				);
			
			CLogicalDynamicGetBase
				(
				IMemoryPool *pmp,
				const CName *pnameAlias,
				CTableDescriptor *ptabdesc,
				ULONG ulScanId
				);

			// dtor
			virtual 
			~CLogicalDynamicGetBase();

			// accessors
			virtual
			DrgPcr *PdrgpcrOutput() const
			{
				return m_pdrgpcrOutput;
			}
			
			// return table's name
			virtual
			const CName &Name() const
			{
				return *m_pnameAlias;
			}
			
			// distribution columns
			virtual
			const CColRefSet *PcrsDist() const
			{
				return m_pcrsDist;
			}

			// return table's descriptor
			virtual
			CTableDescriptor *Ptabdesc() const
			{
				return m_ptabdesc;
			}
			
			// return scan id
			virtual
			ULONG UlScanId() const
			{
				return m_ulScanId;
			}
			
			// return the partition columns
			virtual
			DrgDrgPcr *PdrgpdrgpcrPart() const
			{
				return m_pdrgpdrgpcrPart;
			}

			// return secondary scan id
			virtual
			ULONG UlSecondaryScanId() const
			{
				return m_ulSecondaryScanId;
			}

			// is this a partial scan -- true if the scan operator corresponds to heterogeneous index
			virtual
			BOOL FPartial() const
			{
				return m_fPartial;
			}
			
			// return dynamic get part constraint
			virtual
			CPartConstraint *Ppartcnstr() const
			{
				return m_ppartcnstr;
			}

			// return relation part constraint
			virtual
			CPartConstraint *PpartcnstrRel() const
			{
				return m_ppartcnstrRel;
			}
			
			// set part constraint
			virtual
			void SetPartConstraint(CPartConstraint *ppartcnstr);
			
			// set secondary scan id
			virtual
			void SetSecondaryScanId(ULONG ulScanId);
			
			// set scan to partial
			virtual
			void SetPartial();

			//-------------------------------------------------------------------------------------
			// Derived Relational Properties
			//-------------------------------------------------------------------------------------

			// derive output columns
			virtual
			CColRefSet *PcrsDeriveOutput(IMemoryPool *, CExpressionHandle &);

			// derive keys
			virtual 
			CKeyCollection *PkcDeriveKeys(IMemoryPool *pmp, CExpressionHandle &exprhdl) const;

			// derive partition consumer info
			virtual
			CPartInfo *PpartinfoDerive(IMemoryPool *pmp, CExpressionHandle &exprhdl) const;
			
			// derive constraint property
			virtual
			CPropConstraint *PpcDeriveConstraint(IMemoryPool *pmp, CExpressionHandle &exprhdl) const;
		
			// derive join depth
			virtual
			ULONG UlJoinDepth
				(
				IMemoryPool *, // pmp
				CExpressionHandle & // exprhdl
				)
				const
			{
				return 1;
			}
			

	}; // class CLogicalDynamicGetBase

}


#endif // !GPOPT_CLogicalDynamicGetBase_H

// EOF
