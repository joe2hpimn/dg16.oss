//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2014 Pivotal Inc.
//
//	@filename:
//		CPhysicalCorrelatedLeftAntiSemiNLJoin.h
//
//	@doc:
//		Physical Left Anti Semi NLJ operator capturing correlated execution
//		of NOT EXISTS subqueries
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CPhysicalCorrelatedLeftAntiSemiNLJoin_H
#define GPOPT_CPhysicalCorrelatedLeftAntiSemiNLJoin_H

#include "gpos/base.h"
#include "gpopt/operators/CPhysicalLeftAntiSemiNLJoin.h"

namespace gpopt
{


	//---------------------------------------------------------------------------
	//	@class:
	//		CPhysicalCorrelatedLeftAntiSemiNLJoin
	//
	//	@doc:
	//		Physical left anti semi NLJ operator capturing correlated execution of
	//		NOT EXISTS subqueries
	//
	//---------------------------------------------------------------------------
	class CPhysicalCorrelatedLeftAntiSemiNLJoin : public CPhysicalLeftAntiSemiNLJoin
	{

		private:

			// columns from inner child used in correlated execution
			DrgPcr *m_pdrgpcrInner;

			// origin subquery id
			EOperatorId m_eopidOriginSubq;

			// private copy ctor
			CPhysicalCorrelatedLeftAntiSemiNLJoin(const CPhysicalCorrelatedLeftAntiSemiNLJoin &);

		public:

			// ctor
			CPhysicalCorrelatedLeftAntiSemiNLJoin
				(
				IMemoryPool *pmp,
				DrgPcr *pdrgpcrInner,
				EOperatorId eopidOriginSubq
				)
				:
				CPhysicalLeftAntiSemiNLJoin(pmp),
				m_pdrgpcrInner(pdrgpcrInner),
				m_eopidOriginSubq(eopidOriginSubq)
			{
				GPOS_ASSERT(NULL != pdrgpcrInner);

				SetDistrRequests(UlDistrRequestsForCorrelatedJoin());
				GPOS_ASSERT(0 < UlDistrRequests());
			}

			// dtor
			virtual
			~CPhysicalCorrelatedLeftAntiSemiNLJoin()
			{
				m_pdrgpcrInner->Release();
			}

			// ident accessors
			virtual
			EOperatorId Eopid() const
			{
				return EopPhysicalCorrelatedLeftAntiSemiNLJoin;
			}

			// return a string for operator name
			virtual
			const CHAR *SzId() const
			{
				return "CPhysicalCorrelatedLeftAntiSemiNLJoin";
			}

			// match function
			virtual
			BOOL FMatch
				(
				COperator *pop
				)
				const
			{
				if (pop->Eopid() == Eopid())
				{
					return m_pdrgpcrInner->FEqual(CPhysicalCorrelatedLeftAntiSemiNLJoin::PopConvert(pop)->PdrgPcrInner());
				}

				return false;
			}

			// distribution matching type
			virtual
			CEnfdDistribution::EDistributionMatching Edm
				(
				CReqdPropPlan *, // prppInput
				ULONG,  // ulChildIndex
				DrgPdp *, //pdrgpdpCtxt
				ULONG // ulOptReq
				)
			{
				return CEnfdDistribution::EdmSatisfy;
			}

			// compute required distribution of the n-th child
			virtual
			CDistributionSpec *PdsRequired
				(
				IMemoryPool *pmp,
				CExpressionHandle &exprhdl,
				CDistributionSpec *pdsRequired,
				ULONG ulChildIndex,
				DrgPdp *pdrgpdpCtxt,
				ULONG  ulOptReq
				)
				const
			{
				return PdsRequiredCorrelatedJoin(pmp, exprhdl, pdsRequired, ulChildIndex, pdrgpdpCtxt, ulOptReq);
			}

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
				const
			{
				return PrsRequiredCorrelatedJoin(pmp, exprhdl, prsRequired, ulChildIndex, pdrgpdpCtxt, ulOptReq);
			}

			// return true if operator is a correlated NL Join
			virtual
			BOOL FCorrelated() const
			{
				return true;
			}

			// return required inner columns
			virtual
			DrgPcr *PdrgPcrInner() const
			{
				return m_pdrgpcrInner;
			}

			// print
			virtual
			IOstream &OsPrint
				(
				IOstream &os
				)
				const
			{
				os << this->SzId() << "(";
				(void) CUtils::OsPrintDrgPcr(os, m_pdrgpcrInner);
				os << ")";

				return os;
			}

			// origin subquery id
			EOperatorId EopidOriginSubq() const
			{
				return m_eopidOriginSubq;
			}

			// conversion function
			static
			CPhysicalCorrelatedLeftAntiSemiNLJoin *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopPhysicalCorrelatedLeftAntiSemiNLJoin == pop->Eopid());

				return dynamic_cast<CPhysicalCorrelatedLeftAntiSemiNLJoin*>(pop);
			}

	}; // class CPhysicalCorrelatedLeftAntiSemiNLJoin

}


#endif // !GPOPT_CPhysicalCorrelatedLeftAntiSemiNLJoin_H

// EOF
