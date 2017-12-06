//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2014 Pivotal Inc.
//
//	@filename:
//		CCostModelParamsGPDB.cpp
//
//	@doc:
//		Parameters of GPDB cost model
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/base.h"
#include "gpos/string/CWStringConst.h"
#include "gpdbcost/CCostModelParamsGPDB.h"

using namespace gpopt;

// sequential i/o bandwidth
const CDouble CCostModelParamsGPDB::DSeqIOBandwidthVal = 1024.0;

// random i/o bandwidth
const CDouble CCostModelParamsGPDB::DRandomIOBandwidthVal = 30.0;

// tuple processing bandwidth
const CDouble CCostModelParamsGPDB::DTupProcBandwidthVal = 512.0;

// output bandwidth
const CDouble CCostModelParamsGPDB::DOutputBandwidthVal = 256.0;

// scan initialization cost factor
const CDouble CCostModelParamsGPDB::DInitScanFacorVal = 431.0;

// table scan cost unit
const CDouble CCostModelParamsGPDB::DTableScanCostUnitVal = 5.50e-07;

// index scan initialization cost factor
const CDouble CCostModelParamsGPDB::DInitIndexScanFactorVal = 142.0;

// index block cost unit
const CDouble CCostModelParamsGPDB::DIndexBlockCostUnitVal = 1.27e-06;

// index filtering cost unit
const CDouble CCostModelParamsGPDB::DIndexFilterCostUnitVal = 1.65e-04;

// index scan cost unit per tuple per width
const CDouble CCostModelParamsGPDB::DIndexScanTupCostUnitVal = 3.66e-06;

// index scan random IO factor
const CDouble CCostModelParamsGPDB::DIndexScanTupRandomFactorVal = 6.0;

// filter column cost unit
const CDouble CCostModelParamsGPDB::DFilterColCostUnitVal = 3.29e-05;

// output tuple cost unit
const CDouble CCostModelParamsGPDB::DOutputTupCostUnitVal = 1.86e-06;

// sending tuple cost unit in gather motion
const CDouble CCostModelParamsGPDB::DGatherSendCostUnitVal = 4.58e-06;

// receiving tuple cost unit in gather motion
const CDouble CCostModelParamsGPDB::DGatherRecvCostUnitVal = 2.20e-06;

// sending tuple cost unit in redistribute motion
const CDouble CCostModelParamsGPDB::DRedistributeSendCostUnitVal = 2.33e-06;

// receiving tuple cost unit in redistribute motion
const CDouble CCostModelParamsGPDB::DRedistributeRecvCostUnitVal = 8.0e-07;

// sending tuple cost unit in broadcast motion
const CDouble CCostModelParamsGPDB::DBroadcastSendCostUnitVal = 4.965e-05;

// receiving tuple cost unit in broadcast motion
const CDouble CCostModelParamsGPDB::DBroadcastRecvCostUnitVal = 1.35e-06;

// feeding cost per tuple per column in join operator
const CDouble CCostModelParamsGPDB::DJoinFeedingTupColumnCostUnitVal = 8.69e-05;

// feeding cost per tuple per width in join operator
const CDouble CCostModelParamsGPDB::DJoinFeedingTupWidthCostUnitVal = 6.09e-07;

// output cost per tuple in join operator
const CDouble CCostModelParamsGPDB::DJoinOutputTupCostUnitVal = 3.50e-06;

// memory threshold for hash join spilling (in bytes)
const CDouble CCostModelParamsGPDB::DHJSpillingMemThresholdVal = 50 * 1024 * 1024;

// initial cost for building hash table for hash join
const CDouble CCostModelParamsGPDB::DHJHashTableInitCostFactorVal = 500.0;

// building hash table cost per tuple per column
const CDouble CCostModelParamsGPDB::DHJHashTableColumnCostUnitVal = 5.0e-05;

// the unit cost to process each tuple with unit width when building a hash table
const CDouble CCostModelParamsGPDB::DHJHashTableWidthCostUnitVal = 3.0e-06;

// hashing cost per tuple with unit width in hash join
const CDouble CCostModelParamsGPDB::DHJHashingTupWidthCostUnitVal = 1.97e-05;

// feeding cost per tuple per column in hash join if spilling
const CDouble CCostModelParamsGPDB::DHJFeedingTupColumnSpillingCostUnitVal = 1.97e-04;

// feeding cost per tuple with unit width in hash join if spilling
const CDouble CCostModelParamsGPDB::DHJFeedingTupWidthSpillingCostUnitVal = 3.0e-06;

// hashing cost per tuple with unit width in hash join if spilling
const CDouble CCostModelParamsGPDB::DHJHashingTupWidthSpillingCostUnitVal = 2.30e-05;

// cost for building hash table for per tuple per grouping column in hash aggregate
const CDouble CCostModelParamsGPDB::DHashAggInputTupColumnCostUnitVal = 1.20e-04;

// cost for building hash table for per tuple with unit width in hash aggregate
const CDouble CCostModelParamsGPDB::DHashAggInputTupWidthCostUnitVal = 1.12e-07;

// cost for outputting for per tuple with unit width in hash aggregate
const CDouble CCostModelParamsGPDB::DHashAggOutputTupWidthCostUnitVal = 5.61e-07;

// sorting cost per tuple with unit width
const CDouble CCostModelParamsGPDB::DSortTupWidthCostUnitVal = 5.67e-06;

// cost for processing per tuple with unit width
const CDouble CCostModelParamsGPDB::DTupDefaultProcCostUnitVal = 1.0e-06;

// cost for materializing per tuple with unit width
const CDouble CCostModelParamsGPDB::DMaterializeCostUnitVal = 4.68e-06;

// tuple update bandwidth
const CDouble CCostModelParamsGPDB::DTupUpdateBandwidthVal = 256.0;

// network bandwidth
const CDouble CCostModelParamsGPDB::DNetBandwidthVal = 1024.0;

// number of segments
const CDouble CCostModelParamsGPDB::DSegmentsVal = 4.0;

// nlj factor
const CDouble CCostModelParamsGPDB::DNLJFactorVal = 1.0;

// hj factor
const CDouble CCostModelParamsGPDB::DHJFactorVal = 2.5;

// hash building factor
const CDouble CCostModelParamsGPDB::DHashFactorVal = 2.0;

// default cost
const CDouble CCostModelParamsGPDB::DDefaultCostVal = 100.0;

// largest estimation risk for which we don't penalize index join
const CDouble CCostModelParamsGPDB::DIndexJoinAllowedRiskThreshold = 3;

// default bitmap IO co-efficient when NDV is larger
const CDouble CCostModelParamsGPDB::DBitmapIOCostLargeNDV(0.0082);

// default bitmap IO co-efficient when NDV is smaller
const CDouble CCostModelParamsGPDB::DBitmapIOCostSmallNDV(0.2138);

// default bitmap page cost when NDV is larger
const CDouble CCostModelParamsGPDB::DBitmapPageCostLargeNDV(83.1651);

// default bitmap page cost when NDV is larger
const CDouble CCostModelParamsGPDB::DBitmapPageCostSmallNDV(204.3810);

// default threshold of NDV for bitmap costing
const CDouble CCostModelParamsGPDB::DBitmapNDVThreshold(200);

#define GPOPT_COSTPARAM_NAME_MAX_LENGTH		80

// parameter names in the same order of param enumeration
const CHAR rgszCostParamNames[CCostModelParamsGPDB::EcpSentinel][GPOPT_COSTPARAM_NAME_MAX_LENGTH] =
	{
	"SeqIOBandwidth",
	"RandomIOBandwidth",
	"TupProcBandwidth",
	"OutputBandwidth",
	"InitScanFacor",
	"TableScanCostUnit",
	"InitIndexScanFactor",
	"IndexBlockCostUnit",
	"IndexFilterCostUnit",
	"IndexScanTupCostUnit",
	"IndexScanTupRandomFactor",
	"FilterColCostUnit",
	"OutputTupCostUnit", 
	"GatherSendCostUnit",
	"GatherRecvCostUnit",
	"RedistributeSendCostUnit",
	"RedistributeRecvCostUnit",
	"BroadcastSendCostUnit",
	"BroadcastRecvCostUnit",
	"JoinFeedingTupColumnCostUnit",
	"JoinFeedingTupWidthCostUnit",
	"JoinOutputTupCostUnit",
	"HJSpillingMemThreshold",
	"HJHashTableInitCostFactor",
	"HJHashTableColumnCostUnit",
	"HJHashTableWidthCostUnit",
	"HJHashingTupWidthCostUnit",
	"HJFeedingTupColumnSpillingCostUnit",
	"HJFeedingTupWidthSpillingCostUnit",
	"HJHashingTupWidthSpillingCostUnit",
	"HashAggInputTupColumnCostUnit",
	"HashAggInputTupWidthCostUnit",
	"HashAggOutputTupWidthCostUnit",
	"SortTupWidthCostUnit",
	"TupDefaultProcCostUnit",
	"MaterializeCostUnit",
	"TupUpdateBandwidth",
	"NetworkBandwidth",
	"Segments",
	"NLJFactor",
	"HJFactor",
	"HashFactor",
	"DefaultCost",
	"IndexJoinAllowedRiskThreshold",
	"BitmapIOLargerNDV",
	"BitmapIOSmallerNDV",
	"BitmapPageCostLargerNDV",
	"BitmapPageCostSmallerNDV",
	"BitmapNDVThreshold",
	};

//---------------------------------------------------------------------------
//	@function:
//		CCostModelParamsGPDB::CCostModelParamsGPDB
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CCostModelParamsGPDB::CCostModelParamsGPDB
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
	m_rgpcp[EcpOutputBandwidth] = GPOS_NEW(pmp) SCostParam(EcpOutputBandwidth, DOutputBandwidthVal, DOutputBandwidthVal - 32.0, DOutputBandwidthVal + 32.0);
	m_rgpcp[EcpInitScanFactor] = GPOS_NEW(pmp) SCostParam(EcpInitScanFactor, DInitScanFacorVal, DInitScanFacorVal - 2.0, DInitScanFacorVal + 2.0);
	m_rgpcp[EcpTableScanCostUnit] = GPOS_NEW(pmp) SCostParam(EcpTableScanCostUnit, DTableScanCostUnitVal, DTableScanCostUnitVal - 1.0, DTableScanCostUnitVal + 1.0);
	m_rgpcp[EcpInitIndexScanFactor] = GPOS_NEW(pmp) SCostParam(EcpInitIndexScanFactor, DInitIndexScanFactorVal, DInitIndexScanFactorVal - 1.0, DInitIndexScanFactorVal + 1.0);
	m_rgpcp[EcpIndexBlockCostUnit] = GPOS_NEW(pmp) SCostParam(EcpIndexBlockCostUnit, DIndexBlockCostUnitVal, DIndexBlockCostUnitVal - 1.0, DIndexBlockCostUnitVal + 1.0);
	m_rgpcp[EcpIndexFilterCostUnit] = GPOS_NEW(pmp) SCostParam(EcpIndexFilterCostUnit, DIndexFilterCostUnitVal, DIndexFilterCostUnitVal - 1.0, DIndexFilterCostUnitVal + 1.0);
	m_rgpcp[EcpIndexScanTupCostUnit] = GPOS_NEW(pmp) SCostParam(EcpIndexScanTupCostUnit, DIndexScanTupCostUnitVal, DIndexScanTupCostUnitVal - 1.0, DIndexScanTupCostUnitVal + 1.0);
	m_rgpcp[EcpIndexScanTupRandomFactor] = GPOS_NEW(pmp) SCostParam(EcpIndexScanTupRandomFactor, DIndexScanTupRandomFactorVal, DIndexScanTupRandomFactorVal - 1.0, DIndexScanTupRandomFactorVal + 1.0);
	m_rgpcp[EcpFilterColCostUnit] = GPOS_NEW(pmp) SCostParam(EcpFilterColCostUnit, DFilterColCostUnitVal, DFilterColCostUnitVal - 1.0, DFilterColCostUnitVal + 1.0);
	m_rgpcp[EcpOutputTupCostUnit] = GPOS_NEW(pmp) SCostParam(EcpOutputTupCostUnit, DOutputTupCostUnitVal, DOutputTupCostUnitVal - 1.0, DOutputTupCostUnitVal + 1.0);
	m_rgpcp[EcpGatherSendCostUnit] = GPOS_NEW(pmp) SCostParam(EcpGatherSendCostUnit, DGatherSendCostUnitVal, DGatherSendCostUnitVal - 0.0, DGatherSendCostUnitVal + 0.0);
	m_rgpcp[EcpGatherRecvCostUnit] = GPOS_NEW(pmp) SCostParam(EcpGatherRecvCostUnit, DGatherRecvCostUnitVal, DGatherRecvCostUnitVal - 0.0, DGatherRecvCostUnitVal + 0.0);
	m_rgpcp[EcpRedistributeSendCostUnit] = GPOS_NEW(pmp) SCostParam(EcpRedistributeSendCostUnit, DRedistributeSendCostUnitVal, DRedistributeSendCostUnitVal - 0.0, DRedistributeSendCostUnitVal + 0.0);
	m_rgpcp[EcpRedistributeRecvCostUnit] = GPOS_NEW(pmp) SCostParam(EcpRedistributeRecvCostUnit, DRedistributeRecvCostUnitVal, DRedistributeRecvCostUnitVal - 0.0, DRedistributeRecvCostUnitVal + 0.0);
	m_rgpcp[EcpBroadcastSendCostUnit] = GPOS_NEW(pmp) SCostParam(EcpBroadcastSendCostUnit, DBroadcastSendCostUnitVal, DBroadcastSendCostUnitVal - 0.0, DBroadcastSendCostUnitVal + 0.0);
	m_rgpcp[EcpBroadcastRecvCostUnit] = GPOS_NEW(pmp) SCostParam(EcpBroadcastRecvCostUnit, DBroadcastRecvCostUnitVal, DBroadcastRecvCostUnitVal - 0.0, DBroadcastRecvCostUnitVal + 0.0);
	m_rgpcp[EcpJoinFeedingTupColumnCostUnit] = GPOS_NEW(pmp) SCostParam(EcpJoinFeedingTupColumnCostUnit, DJoinFeedingTupColumnCostUnitVal, DJoinFeedingTupColumnCostUnitVal - 0.0, DJoinFeedingTupColumnCostUnitVal + 0.0);
	m_rgpcp[EcpJoinFeedingTupWidthCostUnit] = GPOS_NEW(pmp) SCostParam(EcpJoinFeedingTupWidthCostUnit, DJoinFeedingTupWidthCostUnitVal, DJoinFeedingTupWidthCostUnitVal - 0.0, DJoinFeedingTupWidthCostUnitVal + 0.0);
	m_rgpcp[EcpJoinOutputTupCostUnit] = GPOS_NEW(pmp) SCostParam(EcpJoinOutputTupCostUnit, DJoinOutputTupCostUnitVal, DJoinOutputTupCostUnitVal - 0.0, DJoinOutputTupCostUnitVal + 0.0);
	m_rgpcp[EcpHJSpillingMemThreshold] = GPOS_NEW(pmp) SCostParam(EcpHJSpillingMemThreshold, DHJSpillingMemThresholdVal, DHJSpillingMemThresholdVal - 0.0, DHJSpillingMemThresholdVal + 0.0);
	m_rgpcp[EcpHJHashTableInitCostFactor] = GPOS_NEW(pmp) SCostParam(EcpHJHashTableInitCostFactor, DHJHashTableInitCostFactorVal, DHJHashTableInitCostFactorVal - 0.0, DHJHashTableInitCostFactorVal + 0.0);
	m_rgpcp[EcpHJHashTableColumnCostUnit] = GPOS_NEW(pmp) SCostParam(EcpHJHashTableColumnCostUnit, DHJHashTableColumnCostUnitVal, DHJHashTableColumnCostUnitVal - 0.0, DHJHashTableColumnCostUnitVal + 0.0);
	m_rgpcp[EcpHJHashTableWidthCostUnit] = GPOS_NEW(pmp) SCostParam(EcpHJHashTableWidthCostUnit, DHJHashTableWidthCostUnitVal, DHJHashTableWidthCostUnitVal - 0.0, DHJHashTableWidthCostUnitVal + 0.0);
	m_rgpcp[EcpHJHashingTupWidthCostUnit] = GPOS_NEW(pmp) SCostParam(EcpHJHashingTupWidthCostUnit, DHJHashingTupWidthCostUnitVal, DHJHashingTupWidthCostUnitVal - 0.0, DHJHashingTupWidthCostUnitVal + 0.0);
	m_rgpcp[EcpHJFeedingTupColumnSpillingCostUnit] = GPOS_NEW(pmp) SCostParam(EcpHJFeedingTupColumnSpillingCostUnit, DHJFeedingTupColumnSpillingCostUnitVal, DHJFeedingTupColumnSpillingCostUnitVal - 0.0, DHJFeedingTupColumnSpillingCostUnitVal + 0.0);
	m_rgpcp[EcpHJFeedingTupWidthSpillingCostUnit] = GPOS_NEW(pmp) SCostParam(EcpHJFeedingTupWidthSpillingCostUnit, DHJFeedingTupWidthSpillingCostUnitVal, DHJFeedingTupWidthSpillingCostUnitVal - 0.0, DHJFeedingTupWidthSpillingCostUnitVal + 0.0);
	m_rgpcp[EcpHJHashingTupWidthSpillingCostUnit] = GPOS_NEW(pmp) SCostParam(EcpHJHashingTupWidthSpillingCostUnit, DHJHashingTupWidthSpillingCostUnitVal, DHJHashingTupWidthSpillingCostUnitVal - 0.0, DHJHashingTupWidthSpillingCostUnitVal + 0.0);
	m_rgpcp[EcpHashAggInputTupColumnCostUnit] = GPOS_NEW(pmp) SCostParam(EcpHashAggInputTupColumnCostUnit, DHashAggInputTupColumnCostUnitVal, DHashAggInputTupColumnCostUnitVal - 0.0, DHashAggInputTupColumnCostUnitVal + 0.0);
	m_rgpcp[EcpHashAggInputTupWidthCostUnit] = GPOS_NEW(pmp) SCostParam(EcpHashAggInputTupWidthCostUnit, DHashAggInputTupWidthCostUnitVal, DHashAggInputTupWidthCostUnitVal - 0.0, DHashAggInputTupWidthCostUnitVal + 0.0);
	m_rgpcp[EcpHashAggOutputTupWidthCostUnit] = GPOS_NEW(pmp) SCostParam(EcpHashAggOutputTupWidthCostUnit, DHashAggOutputTupWidthCostUnitVal, DHashAggOutputTupWidthCostUnitVal - 0.0, DHashAggOutputTupWidthCostUnitVal + 0.0);
	m_rgpcp[EcpSortTupWidthCostUnit] = GPOS_NEW(pmp) SCostParam(EcpSortTupWidthCostUnit, DSortTupWidthCostUnitVal, DSortTupWidthCostUnitVal - 0.0, DSortTupWidthCostUnitVal + 0.0);
	m_rgpcp[EcpTupDefaultProcCostUnit] = GPOS_NEW(pmp) SCostParam(EcpTupDefaultProcCostUnit, DTupDefaultProcCostUnitVal, DTupDefaultProcCostUnitVal - 0.0, DTupDefaultProcCostUnitVal + 0.0);
	m_rgpcp[EcpMaterializeCostUnit] = GPOS_NEW(pmp) SCostParam(EcpMaterializeCostUnit, DMaterializeCostUnitVal, DMaterializeCostUnitVal - 0.0, DMaterializeCostUnitVal + 0.0);
	m_rgpcp[EcpTupUpdateBandwith] = GPOS_NEW(pmp) SCostParam(EcpTupUpdateBandwith, DTupUpdateBandwidthVal, DTupUpdateBandwidthVal - 32.0, DTupUpdateBandwidthVal + 32.0);
	m_rgpcp[EcpNetBandwidth] = GPOS_NEW(pmp) SCostParam(EcpNetBandwidth, DNetBandwidthVal, DNetBandwidthVal - 128.0, DNetBandwidthVal + 128.0);
	m_rgpcp[EcpSegments] = GPOS_NEW(pmp) SCostParam(EcpSegments, DSegmentsVal, DSegmentsVal - 2.0, DSegmentsVal + 2.0);
	m_rgpcp[EcpNLJFactor] = GPOS_NEW(pmp) SCostParam(EcpNLJFactor, DNLJFactorVal, DNLJFactorVal - 0.5, DNLJFactorVal + 0.5);
	m_rgpcp[EcpHJFactor] = GPOS_NEW(pmp) SCostParam(EcpHJFactor, DHJFactorVal, DHJFactorVal - 1.0, DHJFactorVal + 1.0);
	m_rgpcp[EcpHashFactor] = GPOS_NEW(pmp) SCostParam(EcpHashFactor, DHashFactorVal, DHashFactorVal - 1.0, DHashFactorVal + 1.0);
	m_rgpcp[EcpDefaultCost] = GPOS_NEW(pmp) SCostParam(EcpDefaultCost, DDefaultCostVal, DDefaultCostVal - 32.0, DDefaultCostVal + 32.0);
	m_rgpcp[EcpIndexJoinAllowedRiskThreshold] = GPOS_NEW(pmp) SCostParam(EcpIndexJoinAllowedRiskThreshold, DIndexJoinAllowedRiskThreshold, 0, ULONG_MAX);
	m_rgpcp[EcpBitmapIOCostLargeNDV] = GPOS_NEW(pmp) SCostParam(EcpBitmapIOCostLargeNDV, DBitmapIOCostLargeNDV, DBitmapIOCostLargeNDV - 0.0001, DBitmapIOCostLargeNDV + 0.0001);
	m_rgpcp[EcpBitmapIOCostSmallNDV] = GPOS_NEW(pmp) SCostParam(EcpBitmapIOCostSmallNDV, DBitmapIOCostSmallNDV, DBitmapIOCostSmallNDV - 0.0001, DBitmapIOCostSmallNDV + 0.0001);
	m_rgpcp[EcpBitmapPageCostLargeNDV] = GPOS_NEW(pmp) SCostParam(EcpBitmapPageCostLargeNDV, DBitmapPageCostLargeNDV, DBitmapPageCostLargeNDV - 1.0, DBitmapPageCostLargeNDV + 1.0);
	m_rgpcp[EcpBitmapPageCostSmallNDV] = GPOS_NEW(pmp) SCostParam(EcpBitmapPageCostSmallNDV, DBitmapPageCostSmallNDV, DBitmapPageCostSmallNDV - 1.0, DBitmapPageCostSmallNDV + 1.0);
	m_rgpcp[EcpBitmapNDVThreshold] = GPOS_NEW(pmp) SCostParam(EcpBitmapNDVThreshold, DBitmapNDVThreshold, DBitmapNDVThreshold - 1.0, DBitmapNDVThreshold + 1.0);
}


//---------------------------------------------------------------------------
//	@function:
//		CCostModelParamsGPDB::~CCostModelParamsGPDB
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CCostModelParamsGPDB::~CCostModelParamsGPDB()
{
	for (ULONG ul = 0; ul < EcpSentinel; ul++)
	{
		GPOS_DELETE(m_rgpcp[ul]);
		m_rgpcp[ul] = NULL;
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CCostModelParamsGPDB::PcpLookup
//
//	@doc:
//		Lookup param by id;
//
//
//---------------------------------------------------------------------------
CCostModelParamsGPDB::SCostParam *
CCostModelParamsGPDB::PcpLookup
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
//		CCostModelParamsGPDB::PcpLookup
//
//	@doc:
//		Lookup param by name;
//		return NULL if name is not recognized
//
//---------------------------------------------------------------------------
CCostModelParamsGPDB::SCostParam *
CCostModelParamsGPDB::PcpLookup
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
//		CCostModelParamsGPDB::SetParam
//
//	@doc:
//		Set param by id
//
//---------------------------------------------------------------------------
void
CCostModelParamsGPDB::SetParam
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
//		CCostModelParamsGPDB::SetParam
//
//	@doc:
//		Set param by name
//
//---------------------------------------------------------------------------
void
CCostModelParamsGPDB::SetParam
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
//		CCostModelParamsGPDB::OsPrint
//
//	@doc:
//		Print function
//
//---------------------------------------------------------------------------
IOstream &
CCostModelParamsGPDB::OsPrint
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
