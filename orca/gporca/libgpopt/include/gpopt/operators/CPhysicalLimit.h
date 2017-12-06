//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CPhysicalLimit.h
//
//	@doc:
//		Physical Limit operator
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CPhysicalLimit_H
#define GPOPT_CPhysicalLimit_H

#include "gpos/base.h"
#include "gpopt/base/COrderSpec.h"
#include "gpopt/operators/CPhysical.h"

namespace gpopt
{
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CPhysicalLimit
	//
	//	@doc:
	//		Limit operator
	//
	//---------------------------------------------------------------------------
	class CPhysicalLimit : public CPhysical
	{

		private:
		
			// order spec
			COrderSpec *m_pos;

			// global limit
			BOOL m_fGlobal;

			// does limit specify a number of rows?
			BOOL m_fHasCount;

			// this is a top limit right under a DML or CTAS operation
			BOOL m_fTopLimitUnderDML;

			// columns used by order spec
			CColRefSet *m_pcrsSort;

			// private copy ctor
			CPhysicalLimit(const CPhysicalLimit &);

		public:
		
			// ctor
			CPhysicalLimit
				(
				IMemoryPool *pmp,
				COrderSpec *pos,
				BOOL fGlobal,
				BOOL fHasCount,
				BOOL fTopLimitUnderDML
				);

			// dtor
			virtual 
			~CPhysicalLimit();

			// ident accessors
			virtual 
			EOperatorId Eopid() const
			{
				return EopPhysicalLimit;
			}
			
			virtual 
			const CHAR *SzId() const
			{
				return "CPhysicalLimit";
			}
			
			// hash function
			virtual
			ULONG UlHash() const
			{
				return gpos::UlCombineHashes
						(
						gpos::UlCombineHashes(COperator::UlHash(), m_pos->UlHash()),
						gpos::UlCombineHashes(gpos::UlHash<BOOL>(&m_fGlobal), gpos::UlHash<BOOL>(&m_fHasCount))
						);
			}

			// order spec
			COrderSpec *Pos() const
			{
				return m_pos;
			}
			
			// global limit
			BOOL FGlobal() const
			{
				return m_fGlobal;
			}

			// does limit specify a number of rows
			BOOL FHasCount() const
			{
				return m_fHasCount;
			}

			// must the limit be always kept
			BOOL FTopLimitUnderDML() const
			{
				return m_fTopLimitUnderDML;
			}

			// match function
			virtual
			BOOL FMatch(COperator *) const;

			// sensitivity to order of inputs
			virtual
			BOOL FInputOrderSensitive() const
			{
				return true;
			}

			//-------------------------------------------------------------------------------------
			// Required Plan Properties
			//-------------------------------------------------------------------------------------

			// compute required output columns of the n-th child
			virtual
			CColRefSet *PcrsRequired
				(
				IMemoryPool *pmp,
				CExpressionHandle &exprhdl,
				CColRefSet *pcrsRequired,
				ULONG ulChildIndex,
				DrgPdp *pdrgpdpCtxt,
				ULONG ulOptReq
				);

			// compute required ctes of the n-th child
			virtual
			CCTEReq *PcteRequired
				(
				IMemoryPool *pmp,
				CExpressionHandle &exprhdl,
				CCTEReq *pcter,
				ULONG ulChildIndex,
				DrgPdp *pdrgpdpCtxt,
				ULONG ulOptReq
				)
				const;

			// compute required sort order of the n-th child
			virtual
			COrderSpec *PosRequired
				(
				IMemoryPool *pmp,
				CExpressionHandle &exprhdl,
				COrderSpec *posRequired,
				ULONG ulChildIndex,
				DrgPdp *pdrgpdpCtxt,
				ULONG ulOptReq
				)
				const;

			// compute required distribution of the n-th child
			virtual
			CDistributionSpec *PdsRequired
				(
				IMemoryPool *pmp,
				CExpressionHandle &exprhdl,
				CDistributionSpec *pdsRequired,
				ULONG ulChildIndex,
				DrgPdp *pdrgpdpCtxt,
				ULONG ulOptReq
				)
				const;

			// compute required rewindability of the n-th child
			virtual
			CRewindabilitySpec *PrsRequired
				(
				IMemoryPool *pmp,
				CExpressionHandle &exprhdl,
				CRewindabilitySpec *prsRequired,
				ULONG ulChildIndex,
				DrgPdp *pdrgpdpCtxt,
				ULONG ulOptReq
				)
				const;
			
			// compute required partition propagation of the n-th child
			virtual
			CPartitionPropagationSpec *PppsRequired
				(
				IMemoryPool *pmp,
				CExpressionHandle &exprhdl,
				CPartitionPropagationSpec *pppsRequired,
				ULONG ulChildIndex,
				DrgPdp *pdrgpdpCtxt,
				ULONG ulOptReq
				);
			
			// check if required columns are included in output columns
			virtual
			BOOL FProvidesReqdCols(CExpressionHandle &exprhdl, CColRefSet *pcrsRequired, ULONG ulOptReq) const;

			//-------------------------------------------------------------------------------------
			// Derived Plan Properties
			//-------------------------------------------------------------------------------------

			// derive sort order
			virtual
			COrderSpec *PosDerive(IMemoryPool *pmp, CExpressionHandle &exprhdl) const;

			// derive distribution
			virtual
			CDistributionSpec *PdsDerive(IMemoryPool *pmp, CExpressionHandle &exprhdl) const;

			// derive rewindability
			virtual
			CRewindabilitySpec *PrsDerive(IMemoryPool *pmp, CExpressionHandle &exprhdl) const;

			// derive partition index map
			virtual
			CPartIndexMap *PpimDerive
				(
				IMemoryPool *, // pmp
				CExpressionHandle &exprhdl,
				CDrvdPropCtxt * //pdpctxt
				)
				const
			{
				return PpimPassThruOuter(exprhdl);
			}
			
			// derive partition filter map
			virtual
			CPartFilterMap *PpfmDerive
				(
				IMemoryPool *, // pmp
				CExpressionHandle &exprhdl
				)
				const
			{
				return PpfmPassThruOuter(exprhdl);
			}

			//-------------------------------------------------------------------------------------
			// Enforced Properties
			//-------------------------------------------------------------------------------------

			// return order property enforcing type for this operator
			virtual
			CEnfdProp::EPropEnforcingType EpetOrder
				(
				CExpressionHandle &exprhdl,
				const CEnfdOrder *peo
				) const;

			// return distribution property enforcing type for this operator
			virtual
			CEnfdProp::EPropEnforcingType EpetDistribution
				(
				CExpressionHandle &exprhdl,
				const CEnfdDistribution *ped
				) const;

			// return rewindability property enforcing type for this operator
			virtual
			CEnfdProp::EPropEnforcingType EpetRewindability
				(
				CExpressionHandle &, // exprhdl
				const CEnfdRewindability * // per
				) const;

			// return true if operator passes through stats obtained from children,
			// this is used when computing stats during costing
			virtual
			BOOL FPassThruStats() const
			{
				return false;
			}

			//-------------------------------------------------------------------------------------
			//-------------------------------------------------------------------------------------
			//-------------------------------------------------------------------------------------

			// print
			virtual 
			IOstream &OsPrint(IOstream &) const;
		
			// conversion function
			static
			CPhysicalLimit *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopPhysicalLimit == pop->Eopid());
				
				return dynamic_cast<CPhysicalLimit*>(pop);
			}			
					
	}; // class CPhysicalLimit

}

#endif // !GPOPT_CPhysicalLimit_H

// EOF
