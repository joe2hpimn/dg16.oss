//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp
//
//	@filename:
//		CPhysicalDynamicIndexScan.h
//
//	@doc:
//		Physical dynamic index scan operators on partitioned tables
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CPhysicalDynamicIndexScan_H
#define GPOPT_CPhysicalDynamicIndexScan_H

#include "gpos/base.h"
#include "gpopt/operators/CPhysicalDynamicScan.h"
#include "gpopt/metadata/CIndexDescriptor.h"

namespace gpopt
{

	// fwd declarations
	class CTableDescriptor;
	class CIndexDescriptor;
	class CName;
	class CPartConstraint;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CPhysicalDynamicIndexScan
	//
	//	@doc:
	//		Physical dynamic index scan operators for partitioned tables
	//
	//---------------------------------------------------------------------------
	class CPhysicalDynamicIndexScan : public CPhysicalDynamicScan
	{

		private:

			// index descriptor
			CIndexDescriptor *m_pindexdesc;
			
			// order
			COrderSpec *m_pos;

			// private copy ctor
			CPhysicalDynamicIndexScan(const CPhysicalDynamicIndexScan&);

		public:

			// ctors
			CPhysicalDynamicIndexScan
				(
				IMemoryPool *pmp,
				BOOL fPartial,
				CIndexDescriptor *pindexdesc,
				CTableDescriptor *ptabdesc,
				ULONG ulOriginOpId,
				const CName *pnameAlias,
				DrgPcr *pdrgpcrOutput,
				ULONG ulScanId,
				DrgDrgPcr *pdrgpdrgpcrPart,
				ULONG ulSecondaryScanId,
				CPartConstraint *ppartcnstr,
				CPartConstraint *ppartcnstrRel,
				COrderSpec *pos
				);

			// dtor
			virtual
			~CPhysicalDynamicIndexScan();


			// ident accessors
			virtual
			EOperatorId Eopid() const
			{
				return EopPhysicalDynamicIndexScan;
			}

			// operator name
			virtual
			const CHAR *SzId() const
			{
				return "CPhysicalDynamicIndexScan";
			}

			// index descriptor
			CIndexDescriptor *Pindexdesc() const
			{
				return m_pindexdesc;
			}

			// operator specific hash function
			virtual
			ULONG UlHash() const;

			// match function
			virtual
			BOOL FMatch(COperator *pop) const;

			//-------------------------------------------------------------------------------------
			// Derived Plan Properties
			//-------------------------------------------------------------------------------------

			// derive sort order
			virtual
			COrderSpec *PosDerive
							(
							IMemoryPool *,//pmp
							CExpressionHandle &//exprhdl
							)
							const
			{
				m_pos->AddRef();
				return m_pos;
			}

			//-------------------------------------------------------------------------------------
			// Enforced Properties
			//-------------------------------------------------------------------------------------

			// return order property enforcing type for this operator
			virtual
			CEnfdProp::EPropEnforcingType EpetOrder(CExpressionHandle &exprhdl, const CEnfdOrder *peo) const;

			// conversion function
			static
			CPhysicalDynamicIndexScan *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopPhysicalDynamicIndexScan == pop->Eopid());

				return dynamic_cast<CPhysicalDynamicIndexScan*>(pop);
			}

			// debug print
			virtual
			IOstream &OsPrint(IOstream &) const;

			// statistics derivation during costing
			virtual
			IStatistics *PstatsDerive
				(
				IMemoryPool *pmp,
				CExpressionHandle &exprhdl,
				CReqdPropPlan *prpplan,
				DrgPstat *pdrgpstatCtxt
				)
				const;

	}; // class CPhysicalDynamicIndexScan

}

#endif // !GPOPT_CPhysicalDynamicIndexScan_H

// EOF
