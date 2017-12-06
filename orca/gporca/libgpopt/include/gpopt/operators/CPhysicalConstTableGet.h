//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CPhysicalConstTableGet.h
//
//	@doc:
//		Physical const table get
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CPhysicalConstTableGet_H
#define GPOPT_CPhysicalConstTableGet_H

#include "gpos/base.h"
#include "gpopt/operators/CPhysical.h"
#include "gpopt/operators/CLogicalConstTableGet.h"

namespace gpopt
{
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CPhysicalConstTableGet
	//
	//	@doc:
	//		Physical const table get operator
	//
	//---------------------------------------------------------------------------
	class CPhysicalConstTableGet : public CPhysical
	{

		private:

			// array of column descriptors: the schema of the const table
			DrgPcoldesc *m_pdrgpcoldesc;
		
			// array of datum arrays
			DrgPdrgPdatum *m_pdrgpdrgpdatum;
			
			// output columns
			DrgPcr *m_pdrgpcrOutput;
			
			// private copy ctor
			CPhysicalConstTableGet(const CPhysicalConstTableGet &);

		public:
		
			// ctor
			CPhysicalConstTableGet
				(
				IMemoryPool *pmp,
				DrgPcoldesc *pdrgpcoldesc,
				DrgPdrgPdatum *pdrgpdrgpconst,
				DrgPcr *pdrgpcrOutput
				);

			// dtor
			virtual 
			~CPhysicalConstTableGet();

			// ident accessors
			virtual 
			EOperatorId Eopid() const
			{
				return EopPhysicalConstTableGet;
			}
			
			virtual 
			const CHAR *SzId() const
			{
				return "CPhysicalConstTableGet";
			}

			// col descr accessor
			DrgPcoldesc *Pdrgpcoldesc() const
			{
				return m_pdrgpcoldesc;
			}
			
			// const table values accessor
			DrgPdrgPdatum *Pdrgpdrgpdatum () const
			{
				return m_pdrgpdrgpdatum;
			}
			
			// output columns accessors
			DrgPcr *PdrgpcrOutput() const
			{
				return m_pdrgpcrOutput;
			}


			// match function
			virtual
			BOOL FMatch(COperator *) const;

			// sensitivity to order of inputs
			virtual
			BOOL FInputOrderSensitive() const
			{
				GPOS_ASSERT(!"Unexpected function call of FInputOrderSensitive");
				return false;
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
				IMemoryPool *, //pmp,
				CExpressionHandle &, //exprhdl,
				CPartitionPropagationSpec *, //pppsRequired,
				ULONG , //ulChildIndex,
				DrgPdp *, //pdrgpdpCtxt,
				ULONG //ulOptReq
				)
			{
				GPOS_ASSERT(!"CPhysicalConstTableGet has no relational children");
				return NULL;
			}
			
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

			// derive cte map
			virtual
			CCTEMap *PcmDerive(IMemoryPool *pmp, CExpressionHandle &exprhdl) const;

			// derive partition index map
			virtual
			CPartIndexMap *PpimDerive
				(
				IMemoryPool *pmp,
				CExpressionHandle &, //exprhdl
				CDrvdPropCtxt * //pdpctxt
				)
				const
			{
				return GPOS_NEW(pmp) CPartIndexMap(pmp);
			}
			
			// derive partition filter map
			virtual
			CPartFilterMap *PpfmDerive
				(
				IMemoryPool *pmp,
				CExpressionHandle & // exprhdl
				)
				const
			{
				// return empty part filter map
				return GPOS_NEW(pmp) CPartFilterMap(pmp);
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
				) 
				const;

			// return distribution property enforcing type for this operator
			virtual
			CEnfdProp::EPropEnforcingType EpetDistribution
				(
				CExpressionHandle &exprhdl,
				const CEnfdDistribution *ped
				) 
				const;

			// return rewindability property enforcing type for this operator
			virtual
			CEnfdProp::EPropEnforcingType EpetRewindability
				(
				CExpressionHandle &, // exprhdl
				const CEnfdRewindability * // per
				) 
				const;

			// return partition propagation property enforcing type for this operator
			virtual 
			CEnfdProp::EPropEnforcingType EpetPartitionPropagation
				(
				CExpressionHandle &, // exprhdl,
				const CEnfdPartitionPropagation * // pepp
				) 
				const
			{
				return CEnfdProp::EpetRequired;
			}
			
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

			// conversion function
			static
			CPhysicalConstTableGet *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopPhysicalConstTableGet == pop->Eopid());
				
				return dynamic_cast<CPhysicalConstTableGet*>(pop);
			}
		
	}; // class CPhysicalConstTableGet

}

#endif // !GPOPT_CPhysicalConstTableGet_H

// EOF
