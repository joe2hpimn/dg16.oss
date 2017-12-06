//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2014 Pivotal Inc.
//
//	@filename:
//		CCostModelParamsGPDBLegacy.cpp
//
//	@doc:
//		Parameters of GPDB's legacy cost model
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpdbcost/CCostModelParamsGPDBLegacy.h"

using namespace gpopt;

// sequential i/o bandwidth
const CDouble CCostModelParamsGPDBLegacy::DSeqIOBandwidthVal = 1024.0;

// random i/o bandwidth
const CDouble CCostModelParamsGPDBLegacy::DRandomIOBandwidthVal = 30.0;

// tuple processing bandwidth
const CDouble CCostModelParamsGPDBLegacy::DTupProcBandwidthVal = 512.0;

// tuple update bandwidth
const CDouble CCostModelParamsGPDBLegacy::DTupUpdateBandwidthVal = 256.0;

// network bandwidth
const CDouble CCostModelParamsGPDBLegacy::DNetBandwidthVal = 1024.0;

// number of segments
const CDouble CCostModelParamsGPDBLegacy::DSegmentsVal = 4.0;

// nlj outer factor
const CDouble CCostModelParamsGPDBLegacy::DNLJOuterFactorVal = 1024.0;

// nlj factor
const CDouble CCostModelParamsGPDBLegacy::DNLJFactorVal = 1.0;

// hj factor
const CDouble CCostModelParamsGPDBLegacy::DHJFactorVal = 2.5;

// hash building factor
const CDouble CCostModelParamsGPDBLegacy::DHashFactorVal = 2.0;

// default cost
const CDouble CCostModelParamsGPDBLegacy::DDefaultCostVal = 100.0;

#include "gpos/base.h"
#include "gpos/string/CWStringConst.h"


#define GPOPT_COSTPARAM_NAME_MAX_LENGTH		80

// parameter names in the same order of param enumeration
const CHAR rgszCostParamNames[CCostModelParamsGPDBLegacy::EcpSentinel][GPOPT_COSTPARAM_NAME_MAX_LENGTH] =
	{
	"SeqIOBandwidth",
	"RandomIOBandwidth",
	"TupProcBandwidth",
	"TupUpdateBandwidth",
	"NetworkBandwidth",
	"Segments",
	"NLJOuterFactor",
	"NLJFactor",
	"HJFactor",
	"HashFactor",
	"DefaultCost",
	};

//---------------------------------------------------------------------------
//	@function:
//		CCostModelParamsGPDBLegacy::CCostModelParamsGPDBLegacy
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CCostModelParamsGPDBLegacy::CCostModelParamsGPDBLegacy
	(
	IMemoryPool *pmp
	)
	:
	m_pmp(pmp)
{
	GPOS_ASSERT(NULL != pmp);

	for (ULONG ul = 0; ul < EcpSentinel; ul++)
	{
		m_rgpcp[ul] = NULL;
	}

	// populate param array with default param values
	m_rgpcp[EcpSeqIOBandwidth] = GPOS_NEW(pmp) SCostParam(EcpSeqIOBandwidth, DSeqIOBandwidthVal, DSeqIOBandwidthVal - 128.0, DSeqIOBandwidthVal + 128.0);
	m_rgpcp[EcpRandomIOBandwidth] = GPOS_NEW(pmp) SCostParam(EcpRandomIOBandwidth, DRandomIOBandwidthVal, DRandomIOBandwidthVal - 8.0, DRandomIOBandwidthVal + 8.0);
	m_rgpcp[EcpTupProcBandwidth] = GPOS_NEW(pmp) SCostParam(EcpTupProcBandwidth, DTupProcBandwidthVal, DTupProcBandwidthVal - 32.0, DTupProcBandwidthVal + 32.0);
	m_rgpcp[EcpTupUpdateBandwith] = GPOS_NEW(pmp) SCostParam(EcpTupUpdateBandwith, DTupUpdateBandwidthVal, DTupUpdateBandwidthVal - 32.0, DTupUpdateBandwidthVal + 32.0);
	m_rgpcp[EcpNetBandwidth] = GPOS_NEW(pmp) SCostParam(EcpNetBandwidth, DNetBandwidthVal, DNetBandwidthVal - 128.0, DNetBandwidthVal + 128.0);
	m_rgpcp[EcpSegments] = GPOS_NEW(pmp) SCostParam(EcpSegments, DSegmentsVal, DSegmentsVal - 2.0, DSegmentsVal + 2.0);
	m_rgpcp[EcpNLJOuterFactor] = GPOS_NEW(pmp) SCostParam(EcpNLJOuterFactor, DNLJOuterFactorVal, DNLJOuterFactorVal - 128.0, DNLJOuterFactorVal + 128.0);
	m_rgpcp[EcpNLJFactor] = GPOS_NEW(pmp) SCostParam(EcpNLJFactor, DNLJFactorVal, DNLJFactorVal - 0.5, DNLJFactorVal + 0.5);
	m_rgpcp[EcpHJFactor] = GPOS_NEW(pmp) SCostParam(EcpHJFactor, DHJFactorVal, DHJFactorVal - 1.0, DHJFactorVal + 1.0);
	m_rgpcp[EcpHashFactor] = GPOS_NEW(pmp) SCostParam(EcpHashFactor, DHashFactorVal, DHashFactorVal - 1.0, DHashFactorVal + 1.0);
	m_rgpcp[EcpDefaultCost] = GPOS_NEW(pmp) SCostParam(EcpDefaultCost, DDefaultCostVal, DDefaultCostVal - 32.0, DDefaultCostVal + 32.0);
}


//---------------------------------------------------------------------------
//	@function:
//		CCostModelParamsGPDBLegacy::~CCostModelParamsGPDBLegacy
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CCostModelParamsGPDBLegacy::~CCostModelParamsGPDBLegacy()
{
	for (ULONG ul = 0; ul < EcpSentinel; ul++)
	{
		GPOS_DELETE(m_rgpcp[ul]);
		m_rgpcp[ul] = NULL;
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CCostModelParamsGPDBLegacy::PcpLookup
//
//	@doc:
//		Lookup param by id;
//
//
//---------------------------------------------------------------------------
CCostModelParamsGPDBLegacy::SCostParam *
CCostModelParamsGPDBLegacy::PcpLookup
	(
	ULONG ulId
	)
	const
{
	ECostParam ecp = (ECostParam) ulId;
	GPOS_ASSERT(EcpSentinel > ecp);

	return m_rgpcp[ecp];
}


//---------------------------------------------------------------------------
//	@function:
//		CCostModelParamsGPDBLegacy::PcpLookup
//
//	@doc:
//		Lookup param by name;
//		return NULL if name is not recognized
//
//---------------------------------------------------------------------------
CCostModelParamsGPDBLegacy::SCostParam *
CCostModelParamsGPDBLegacy::PcpLookup
	(
	const CHAR *szName
	)
	const
{
	GPOS_ASSERT(NULL != szName);

	for (ULONG ul = 0; ul < EcpSentinel; ul++)
	{
		if (0 == clib::IStrCmp(szName, rgszCostParamNames[ul]))
		{
			return PcpLookup((ECostParam) ul);
		}
	}

	return NULL;
}


//---------------------------------------------------------------------------
//	@function:
//		CCostModelParamsGPDBLegacy::SetParam
//
//	@doc:
//		Set param by id
//
//---------------------------------------------------------------------------
void
CCostModelParamsGPDBLegacy::SetParam
	(
	ULONG ulId,
	CDouble dVal,
	CDouble dLowerBound,
	CDouble dUpperBound
	)
{
	ECostParam ecp = (ECostParam) ulId;
	GPOS_ASSERT(EcpSentinel > ecp);

	GPOS_DELETE(m_rgpcp[ecp]);
	m_rgpcp[ecp] = NULL;
	m_rgpcp[ecp] =  GPOS_NEW(m_pmp) SCostParam(ecp, dVal, dLowerBound, dUpperBound);
}


//---------------------------------------------------------------------------
//	@function:
//		CCostModelParamsGPDBLegacy::SetParam
//
//	@doc:
//		Set param by name
//
//---------------------------------------------------------------------------
void
CCostModelParamsGPDBLegacy::SetParam
	(
	const CHAR *szName,
	CDouble dVal,
	CDouble dLowerBound,
	CDouble dUpperBound
	)
{
	GPOS_ASSERT(NULL != szName);

	for (ULONG ul = 0; ul < EcpSentinel; ul++)
	{
		if (0 == clib::IStrCmp(szName, rgszCostParamNames[ul]))
		{
			GPOS_DELETE(m_rgpcp[ul]);
			m_rgpcp[ul] = NULL;
			m_rgpcp[ul] = GPOS_NEW(m_pmp) SCostParam(ul, dVal, dLowerBound, dUpperBound);

			return;
		}
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CCostModelParamsGPDBLegacy::OsPrint
//
//	@doc:
//		Print function
//
//---------------------------------------------------------------------------
IOstream &
CCostModelParamsGPDBLegacy::OsPrint
	(
	IOstream &os
	)
	const
{
	for (ULONG ul = 0; ul < EcpSentinel; ul++)
	{
		SCostParam *pcp = PcpLookup((ECostParam) ul);
		os
			<< rgszCostParamNames[ul] << " : "
			<< pcp->DVal()
			<< "  [" << pcp->DLowerBound() << "," << pcp->DUpperBound() <<"]"
			<< std::endl;
	}
	return os;
}


// EOF
