//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 Greenplum, Inc.
//
//	@filename:
//		CEnfdDistribution.cpp
//
//	@doc:
//		Implementation of enforceable distribution property
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/base.h"

#include "gpopt/base/CDistributionSpec.h"
#include "gpopt/base/CDistributionSpecHashed.h"
#include "gpopt/base/CDistributionSpecSingleton.h"
#include "gpopt/base/CEnfdDistribution.h"
#include "gpopt/base/CDrvdPropPlan.h"
#include "gpopt/base/CReqdPropPlan.h"
#include "gpopt/base/CDrvdPropPlan.h"
#include "gpopt/base/CPartIndexMap.h"
#include "gpopt/operators/CExpressionHandle.h"
#include "gpopt/operators/CPhysicalMotionGather.h"
#include "gpopt/operators/CPhysicalMotionHashDistribute.h"
#include "gpopt/operators/CPhysicalMotionBroadcast.h"


using namespace gpopt;


// initialization of static variables
const CHAR *CEnfdDistribution::m_szDistributionMatching[EdmSentinel] =
{
	"exact",
	"satisfy",
	"subset"
};


//---------------------------------------------------------------------------
//	@function:
//		CEnfdDistribution::CEnfdDistribution
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CEnfdDistribution::CEnfdDistribution
	(
	CDistributionSpec *pds,
	EDistributionMatching edm
	)
	:
	m_pds(pds),
	m_edm(edm)
{
	GPOS_ASSERT(NULL != pds);
	GPOS_ASSERT(EdmSentinel > edm);
}


//---------------------------------------------------------------------------
//	@function:
//		CEnfdDistribution::~CEnfdDistribution
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CEnfdDistribution::~CEnfdDistribution()
{
	CRefCount::SafeRelease(m_pds);
}


//---------------------------------------------------------------------------
//	@function:
//		CEnfdDistribution::FCompatible
//
//	@doc:
//		Check if the given distribution specification is compatible with the
//		distribution specification of this object for the specified matching type
//
//---------------------------------------------------------------------------
BOOL
CEnfdDistribution::FCompatible
	(
	CDistributionSpec *pds
	)
	const
{
	GPOS_ASSERT(NULL != pds);

	switch (m_edm)
	{
		case EdmExact:
			return pds->FMatch(m_pds);

		case EdmSatisfy:
			return pds->FSatisfies(m_pds);

		case EdmSubset:
			GPOS_ASSERT(CDistributionSpec::EdtHashed == m_pds->Edt());
			GPOS_ASSERT(CDistributionSpec::EdtHashed == pds->Edt());

			return dynamic_cast<const CDistributionSpecHashed *>(pds)->FMatchSubset
						(
						dynamic_cast<const CDistributionSpecHashed *>(m_pds)
						);

		case (EdmSentinel):
			GPOS_ASSERT("invalid matching type");
	}

	return false;
}


//---------------------------------------------------------------------------
//	@function:
//		CEnfdDistribution::UlHash
//
//	@doc:
// 		Hash function
//
//---------------------------------------------------------------------------
ULONG
CEnfdDistribution::UlHash() const
{
	return gpos::UlCombineHashes(m_edm + 1, m_pds->UlHash());
}

//---------------------------------------------------------------------------
//	@function:
//		CEnfdDistribution::Epet
//
//	@doc:
// 		Get distribution enforcing type for the given operator
//
//---------------------------------------------------------------------------
CEnfdProp::EPropEnforcingType
CEnfdDistribution::Epet
	(
	CExpressionHandle &exprhdl,
	CPhysical *popPhysical,
	CPartitionPropagationSpec *pppsReqd,
	BOOL fDistribReqd
	)
	const
{
	if (fDistribReqd)
	{
		CDistributionSpec *pds = CDrvdPropPlan::Pdpplan(exprhdl.Pdp())->Pds();

		if (CDistributionSpec::EdtReplicated == pds->Edt() &&
			CDistributionSpec::EdtHashed == PdsRequired()->Edt() &&
			EdmSatisfy == m_edm)
		{
			// child delivers a replicated distribution, no need to enforce hashed distribution
			// if only satisfiability is needed
			return EpetUnnecessary;
		}

		// if operator is a propagator/consumer of any partition index id, prohibit
		// enforcing any distribution not compatible with what operator delivers
		// if the derived partition consumers are a subset of the ones in the given
		// required partition propagation spec, those will be enforced in the same group
		CPartIndexMap *ppimDrvd = CDrvdPropPlan::Pdpplan(exprhdl.Pdp())->Ppim();
		GPOS_ASSERT(NULL != ppimDrvd);
		if (ppimDrvd->FContainsUnresolved() && !this->FCompatible(pds) &&
			!ppimDrvd->FSubset(pppsReqd->Ppim()))
		{
			return CEnfdProp::EpetProhibited;
		}
	 	
		return popPhysical->EpetDistribution(exprhdl, this);
	}

	return EpetUnnecessary;
}

//---------------------------------------------------------------------------
//	@function:
//		CEnfdDistribution::OsPrint
//
//	@doc:
//		Print function
//
//---------------------------------------------------------------------------
IOstream &
CEnfdDistribution::OsPrint
	(
	IOstream &os
	)
	const
{
	os = m_pds->OsPrint(os);

	return os << " match: " << m_szDistributionMatching[m_edm];
}


// EOF
