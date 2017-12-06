//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2014 Pivotal Inc.
//
//	@filename:
//		CPhysicalCorrelatedNotInLeftAntiSemiNLJoin.h
//
//	@doc:
//		Physical Left Anti Semi NLJ operator capturing correlated execution
//		with NOT-IN/ALL semantics
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CPhysicalCorrelatedNotInLeftAntiSemiNLJoin_H
#define GPOPT_CPhysicalCorrelatedNotInLeftAntiSemiNLJoin_H

#include "gpos/base.h"
#include "gpopt/operators/CPhysicalLeftAntiSemiNLJoinNotIn.h"

namespace gpopt
{


	//---------------------------------------------------------------------------
	//	@class:
	//		CPhysicalCorrelatedNotInLeftAntiSemiNLJoin
	//
	//	@doc:
	//		Physical left semi NLJ operator capturing correlated execution with
	//		ANY/IN semantics
	//
	//---------------------------------------------------------------------------
	class CPhysicalCorrelatedNotInLeftAntiSemiNLJoin : public CPhysicalLeftAntiSemiNLJoinNotIn
	{

		private:

			// columns from inner child used in correlated execution
			DrgPcr *m_pdrgpcrInner;

			// origin subquery id
			EOperatorId m_eopidOriginSubq;

			// private copy ctor
			CPhysicalCorrelatedNotInLeftAntiSemiNLJoin(const CPhysicalCorrelatedNotInLeftAntiSemiNLJoin &);

		public:

			// ctor
			CPhysicalCorrelatedNotInLeftAntiSemiNLJoin
				(
				IMemoryPool *pmp,
				DrgPcr *pdrgpcrInner,
				EOperatorId eopidOriginSubq
				)
				:
				CPhysicalLeftAntiSemiNLJoinNotIn(pmp),
				m_pdrgpcrInner(pdrgpcrInner),
				m_eopidOriginSubq(eopidOriginSubq)
			{
				GPOS_ASSERT(NULL != pdrgpcrInner);

				SetDistrRequests(UlDistrRequestsForCorrelatedJoin());
				GPOS_ASSERT(0 < UlDistrRequests());
			}

			// dtor
			virtual
			~CPhysicalCorrelatedNotInLeftAntiSemiNLJoin()
			{
				m_pdrgpcrInner->Release();
			}

			// ident accessors
			virtual
			EOperatorId Eopid() const
			{
				return EopPhysicalCorrelatedNotInLeftAntiSemiNLJoin;
			}

			// return a string for operator name
			virtual
			const CHAR *SzId() const
			{
				return "CPhysicalCorrelatedNotInLeftAntiSemiNLJoin";
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
					return m_pdrgpcrInner->FEqual(CPhysicalCorrelatedNotInLeftAntiSemiNLJoin::PopConvert(pop)->PdrgPcrInner());
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

			// conversion function
			static
			CPhysicalCorrelatedNotInLeftAntiSemiNLJoin *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopPhysicalCorrelatedNotInLeftAntiSemiNLJoin == pop->Eopid());

				return dynamic_cast<CPhysicalCorrelatedNotInLeftAntiSemiNLJoin*>(pop);
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


			// origin subquery id
			EOperatorId EopidOriginSubq() const
			{
				return m_eopidOriginSubq;
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

	}; // class CPhysicalCorrelatedNotInLeftAntiSemiNLJoin

}


#endif // !GPOPT_CPhysicalCorrelatedNotInLeftAntiSemiNLJoin_H

// EOF
