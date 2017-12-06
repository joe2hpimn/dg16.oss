//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright 2013 EMC Corp.
//
//	@filename:
//		CStatisticsUtils.cpp
//
//	@doc:
//		Statistics helper routines
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/base.h"

#include "gpopt/base/CUtils.h"
#include "gpopt/base/CColRefTable.h"
#include "gpopt/base/CColRefSetIter.h"
#include "gpopt/exception.h"
#include "gpopt/operators/ops.h"
#include "gpopt/operators/CExpressionUtils.h"
#include "gpopt/operators/CPredicateUtils.h"
#include "gpopt/mdcache/CMDAccessor.h"
#include "gpopt/engine/CStatisticsConfig.h"
#include "gpopt/optimizer/COptimizerConfig.h"

#include "naucrates/statistics/CStatisticsUtils.h"
#include "naucrates/statistics/CStatistics.h"
#include "naucrates/statistics/CStatsPredUtils.h"
#include "naucrates/statistics/CStatsPredDisj.h"
#include "naucrates/statistics/CStatsPredConj.h"
#include "naucrates/statistics/CStatsPredLike.h"
#include "naucrates/statistics/CScaleFactorUtils.h"
#include "naucrates/statistics/CHistogram.h"

#include "naucrates/md/IMDScalarOp.h"
#include "naucrates/md/IMDType.h"
#include "naucrates/md/IMDTypeInt2.h"
#include "naucrates/md/IMDTypeInt8.h"
#include "naucrates/md/IMDTypeOid.h"
#include "naucrates/md/CMDIdColStats.h"

#include "naucrates/base/IDatumInt2.h"
#include "naucrates/base/IDatumInt4.h"
#include "naucrates/base/IDatumInt8.h"
#include "naucrates/base/IDatumOid.h"

using namespace gpopt;
using namespace gpmd;

//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PpointNext
//
//	@doc:
// 		Get the next data point for new bucket boundary 
//
//---------------------------------------------------------------------------
CPoint *
CStatisticsUtils::PpointNext
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	CPoint *ppoint
	)
{
	IMDId *pmdid = ppoint->Pdatum()->Pmdid();
	const IMDType *pmdtype = pmda->Pmdtype(pmdid);

	// type has integer mapping
	if (pmdtype->Eti() == IMDType::EtiInt2 || pmdtype->Eti() == IMDType::EtiInt4 ||
		pmdtype->Eti() == IMDType::EtiInt8 || pmdtype->Eti() == IMDType::EtiOid)
	{
		IDatum *pdatumNew = NULL;

		IDatum *pdatumOld = ppoint->Pdatum();

		if (pmdtype->Eti() == IMDType::EtiInt2)
		{
			SINT sValue = (SINT) (dynamic_cast<IDatumInt2 *>(pdatumOld)->SValue() + 1);
			pdatumNew = dynamic_cast<const IMDTypeInt2 *>(pmdtype)->PdatumInt2(pmp, sValue, false);
		}
		else if (pmdtype->Eti() == IMDType::EtiInt4)
		{
			INT iValue = dynamic_cast<IDatumInt4 *>(pdatumOld)->IValue() + 1;
			pdatumNew = dynamic_cast<const IMDTypeInt4 *>(pmdtype)->PdatumInt4(pmp, iValue, false);
		}
		else if (pmdtype->Eti() == IMDType::EtiInt8)
		{
			LINT lValue = dynamic_cast<IDatumInt8 *>(pdatumOld)->LValue() + 1;
			pdatumNew = dynamic_cast<const IMDTypeInt8 *>(pmdtype)->PdatumInt8(pmp, lValue, false);
		}
		else
		{
			OID oidValue = dynamic_cast<IDatumOid *>(pdatumOld)->OidValue() + 1;
			pdatumNew = dynamic_cast<const IMDTypeOid *>(pmdtype)->PdatumOid(pmp, oidValue, false);
		}

		return GPOS_NEW(pmp) CPoint(pdatumNew);
	}

	return NULL;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PhistTransformMCV
//
//	@doc:
//		Given MCVs and their frequencies, construct a CHistogram
//		containing MCV singleton buckets
//---------------------------------------------------------------------------
CHistogram *
CStatisticsUtils::PhistTransformMCV
	(
	IMemoryPool *pmp,
	const IMDType *, // pmdtype,
	DrgPdatum *pdrgpdatumMCV,
	DrgPdouble *pdrgpdFreq,
	ULONG ulNumMCVValues
	)
{
	GPOS_ASSERT(pdrgpdatumMCV->UlLength() == ulNumMCVValues);

	// put MCV values and their corresponding frequencies
	// into a structure in order to sort
	DrgPsmcvpair *pdrgpsmcvpair = GPOS_NEW(pmp) DrgPsmcvpair(pmp);
	for (ULONG ul = 0; ul < ulNumMCVValues; ul++)
	{
		IDatum *pdatum = (*pdrgpdatumMCV)[ul];
		CDouble dMcvFreq = *((*pdrgpdFreq)[ul]);
		pdatum->AddRef();
		SMcvPair *psmcvpair = GPOS_NEW(pmp) SMcvPair(pdatum, dMcvFreq);
		pdrgpsmcvpair->Append(psmcvpair);
	}

	// sort the MCV value-frequency pairs according to value
	if (1 < ulNumMCVValues)
	{
		pdrgpsmcvpair->Sort(CStatisticsUtils::IMcvPairCmp);
	}

	// now put MCVs and their frequencies in buckets
	DrgPbucket *pdrgpbucketMCV = GPOS_NEW(pmp) DrgPbucket(pmp);

	for (ULONG ul = 0; ul < ulNumMCVValues; ul++)
	{
		IDatum *pdatum = (*pdrgpsmcvpair)[ul]->m_pdatumMCV;
		pdatum->AddRef();
		pdatum->AddRef();
		CDouble dBucketFreq = (*pdrgpsmcvpair)[ul]->m_dMcvFreq;
		CBucket *pbucket = GPOS_NEW(pmp) CBucket
										(
										GPOS_NEW(pmp) CPoint(pdatum),
										GPOS_NEW(pmp) CPoint(pdatum),
										true /* fLowerClosed */,
										true /* fUpperClosed */,
										dBucketFreq, CDouble(1.0)
										);
		pdrgpbucketMCV->Append(pbucket);
	}
	CHistogram *phist =  GPOS_NEW(pmp) CHistogram(pdrgpbucketMCV);
	GPOS_ASSERT(phist->FValid());
	pdrgpsmcvpair->Release();

	return phist;
}

//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PhistMergeMcvHist
//
//	@doc:
//		Given MCVs and histogram in CHistogram, merge them into a single
//		CHistogram
//
//---------------------------------------------------------------------------
CHistogram *
CStatisticsUtils::PhistMergeMcvHist
	(
	IMemoryPool *pmp,
	const CHistogram *phistGPDBMcv,
	const CHistogram *phistGPDBHist
	)
{
	GPOS_ASSERT(NULL != phistGPDBMcv);
	GPOS_ASSERT(NULL != phistGPDBHist);
	GPOS_ASSERT(phistGPDBMcv->FWellDefined());
	GPOS_ASSERT(phistGPDBHist->FWellDefined());
	GPOS_ASSERT(0 < phistGPDBMcv->UlBuckets());
	GPOS_ASSERT(0 < phistGPDBHist->UlBuckets());

	const DrgPbucket *pdrgpbucketMCV = phistGPDBMcv->Pdrgpbucket();
	const DrgPbucket *pdrgpbucketHist = phistGPDBHist->Pdrgpbucket();

	IDatum *pdatum = (*pdrgpbucketMCV)[0]->PpLower()->Pdatum();

	// data types that are not supported in the new optimizer yet
	if (!pdatum->FStatsComparable(pdatum))
	{
		// fall back to the approach that chooses the one having more information
		if (0.5 < phistGPDBMcv->DFrequency())
		{
			// have to do deep copy, otherwise phistGPDBMcv and phistMerge
			// will point to the same object
			return phistGPDBMcv->PhistCopy(pmp);
		}

		return phistGPDBHist->PhistCopy(pmp);
	}

	// both MCV and histogram buckets must be sorted
	GPOS_ASSERT(phistGPDBMcv->FValid());
	GPOS_ASSERT(phistGPDBHist->FValid());

	DrgPbucket *pdrgpbucketMerged = PdrgpbucketMergeBuckets(pmp, pdrgpbucketMCV, pdrgpbucketHist);

	CHistogram *phistMerged =  GPOS_NEW(pmp) CHistogram(pdrgpbucketMerged);
	GPOS_ASSERT(phistMerged->FValid());

	return phistMerged;
}

//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PdrgpbucketCreateMergedBuckets
//
//	@doc:
//		Given histogram buckets and MCV buckets, merge them into
//		an array of buckets.
//
//---------------------------------------------------------------------------
DrgPbucket *
CStatisticsUtils::PdrgpbucketMergeBuckets
	(
	IMemoryPool *pmp,
	const DrgPbucket *pdrgpbucketMCV,
	const DrgPbucket *pdrgpbucketHist
	)
{
	DrgPbucket *pdrgpbucketMerged = GPOS_NEW(pmp) DrgPbucket(pmp);
	const ULONG ulMCV = pdrgpbucketMCV->UlLength();
	const ULONG ulHist = pdrgpbucketHist->UlLength();
	ULONG ulMCVIdx = 0;
	ULONG ulHistIdx = 0;

	while (ulMCVIdx < ulMCV && ulHistIdx < ulHist)
	{
		CBucket *pbucketMCV = (*pdrgpbucketMCV)[ulMCVIdx];
		CBucket *pbucketHist = (*pdrgpbucketHist)[ulHistIdx];

		if (pbucketMCV->FBefore(pbucketHist))
		{
			pdrgpbucketMerged->Append(pbucketMCV->PbucketCopy(pmp));
			ulMCVIdx++;
		}
		else if (pbucketMCV->FAfter(pbucketHist))
		{
			pdrgpbucketMerged->Append(pbucketHist->PbucketCopy(pmp));
			ulHistIdx++;
		}
		else // pbucketMCV is contained in pbucketHist
		{
			GPOS_ASSERT(pbucketHist->FSubsumes(pbucketMCV));
			SplitHistDriver(pmp, pbucketHist, pdrgpbucketMCV, pdrgpbucketMerged, &ulMCVIdx, ulMCV);
			ulHistIdx++;
		}
	}

	// append leftover buckets from either MCV or histogram
	AddRemainingBuckets(pmp, pdrgpbucketMCV, pdrgpbucketMerged, &ulMCVIdx);
	AddRemainingBuckets(pmp, pdrgpbucketHist, pdrgpbucketMerged, &ulHistIdx);

	return pdrgpbucketMerged;
}

//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::AddRemainingBuckets
//
//	@doc:
//		Add remaining buckets from one array of buckets to the other
//
//---------------------------------------------------------------------------
void
CStatisticsUtils::AddRemainingBuckets
	(
	IMemoryPool *pmp,
	const DrgPbucket *pdrgpbucketSrc,
	DrgPbucket *pdrgpbucketDest,
	ULONG *pulStart
	)
{
	const ULONG ulTotal = pdrgpbucketSrc->UlLength();

	while (*pulStart < ulTotal)
	{
		pdrgpbucketDest->Append((*pdrgpbucketSrc)[*pulStart]->PbucketCopy(pmp));
		(*pulStart)++;
	}
}

//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::SplitHistDriver
//
//	@doc:
//		Given an MCV that are contained in a histogram bucket,
//		find the batch of MCVs that fall in the same histogram bucket.
//		Then perform the split for this batch of MCVs.
//
//---------------------------------------------------------------------------
void
CStatisticsUtils::SplitHistDriver
	(
	IMemoryPool *pmp,
	const CBucket *pbucketHist,
	const DrgPbucket *pdrgpbucketMCV,
	DrgPbucket *pdrgpbucketMerged,
	ULONG *pulMCVIdx,
	ULONG ulMCV
	)
{
	GPOS_ASSERT(NULL != pbucketHist);
	GPOS_ASSERT(NULL != pdrgpbucketMCV);

	DrgPbucket *pdrgpbucketTmpMcv = GPOS_NEW(pmp) DrgPbucket(pmp);

	// find the MCVs that fall into the same histogram bucket and put them in a temp array
	// E.g. MCV = ..., 6, 8, 12, ... and the current histogram bucket is [5,10)
	// then 6 and 8 will be handled together, i.e. split [5,10) into [5,6) [6,6] (6,8) [8,8] (8,10)
	while ((*pulMCVIdx) < ulMCV && pbucketHist->FSubsumes((*pdrgpbucketMCV)[*pulMCVIdx]))
	{
		CBucket *pbucket = (*pdrgpbucketMCV)[*pulMCVIdx];
		pdrgpbucketTmpMcv->Append(pbucket->PbucketCopy(pmp));
		(*pulMCVIdx)++;
	}

	// split pbucketHist given one or more MCVs it contains
	DrgPbucket *pdrgpbucketSplitted = PdrgpbucketSplitHistBucket(pmp, pbucketHist, pdrgpbucketTmpMcv);
	const ULONG ulSplitted = pdrgpbucketSplitted->UlLength();

	// copy buckets from pdrgpbucketSplitted to pdrgbucketMerged
	for (ULONG ul = 0; ul < ulSplitted; ul++)
	{
		CBucket *pbucket = (*pdrgpbucketSplitted)[ul];
		pdrgpbucketMerged->Append(pbucket->PbucketCopy(pmp));
	}

	pdrgpbucketTmpMcv->Release();
	pdrgpbucketSplitted->Release();
}

//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PdrgpbucketSplitHistBucket
//
//	@doc:
//		Given an array of MCVs that are contained in a histogram bucket,
//		split the histogram bucket into smaller buckets with the MCVs being
//		the splitting points. The MCVs are returned too, among the smaller
//		buckets.
//
//---------------------------------------------------------------------------
DrgPbucket *
CStatisticsUtils::PdrgpbucketSplitHistBucket
	(
	IMemoryPool *pmp,
	const CBucket *pbucketHist,
	const DrgPbucket *pdrgpbucketMCV
	)
{
	GPOS_ASSERT(NULL != pbucketHist);
	GPOS_ASSERT(NULL != pdrgpbucketMCV);

	DrgPbucket *pdrgpbucketNew = GPOS_NEW(pmp) DrgPbucket(pmp);
	const ULONG ulMCV = pdrgpbucketMCV->UlLength();
	GPOS_ASSERT(0 < ulMCV);

	// construct first bucket, if any
	CPoint *ppointMCV = (*pdrgpbucketMCV)[0]->PpLower();
	CBucket *pbucketFirst = PbucketCreateValidBucket
								(
								pmp,
								pbucketHist->PpLower(),
								ppointMCV,
								pbucketHist->FLowerClosed(),
								false // fUpperClosed
								);
	if (NULL != pbucketFirst)
	{
		pdrgpbucketNew->Append(pbucketFirst);
	}

	// construct middle buckets, if any
	for (ULONG ulIdx = 0; ulIdx < ulMCV - 1; ulIdx++)
	{
		// first append the MCV itself
		CBucket *pbucketMCV = (*pdrgpbucketMCV)[ulIdx];
		pdrgpbucketNew->Append(pbucketMCV->PbucketCopy(pmp));

		// construct new buckets
		CPoint *ppointLeft = pbucketMCV->PpLower(); // this MCV
		CPoint *ppointRight = (*pdrgpbucketMCV)[ulIdx+1]->PpLower(); // next MCV

		CBucket *pbucketNew = PbucketCreateValidBucket(pmp, ppointLeft, ppointRight, false, false);
		if (NULL != pbucketNew)
		{
			pdrgpbucketNew->Append(pbucketNew);
		}
	}

	// append last MCV
	CBucket *pbucketMCV = (*pdrgpbucketMCV)[ulMCV-1];
	pdrgpbucketNew->Append(pbucketMCV->PbucketCopy(pmp));
	ppointMCV = pbucketMCV->PpLower();

	// construct last bucket, if any
	CBucket *pbucketLast = PbucketCreateValidBucket(pmp, ppointMCV, pbucketHist->PpUpper(), false, pbucketHist->FUpperClosed());
	if (NULL != pbucketLast)
	{
		pdrgpbucketNew->Append(pbucketLast);
	}

	// re-balance dDistinct and dFrequency in pdrgpbucketNew
	CDouble dDistinctTotal = std::max(CDouble(1.0), pbucketHist->DDistinct() - ulMCV);
	DistributeBucketProperties(pbucketHist->DFrequency(), dDistinctTotal, pdrgpbucketNew);

	return pdrgpbucketNew;
}

//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PbucketCreateValidBucket
//
//	@doc:
//		Given lower and upper and their closedness, create a bucket if they
//		can form a valid bucket
//
//---------------------------------------------------------------------------
CBucket *
CStatisticsUtils::PbucketCreateValidBucket
	(
	IMemoryPool *pmp,
	CPoint *ppointLower,
	CPoint *ppointUpper,
	BOOL fLowerClosed,
	BOOL fUpperClosed
	)
{
	if (!FValidBucket(ppointLower, ppointUpper, fLowerClosed, fUpperClosed))
	{
		return NULL;
	}
	ppointLower->AddRef();
	ppointUpper->AddRef();

	return GPOS_NEW(pmp) CBucket
						(
						ppointLower,
						ppointUpper,
						fLowerClosed,
						fUpperClosed,
						GPOPT_BUCKET_DEFAULT_FREQ, // dFrequency will be assigned later
						GPOPT_BUCKET_DEFAULT_DISTINCT // dDistinct will be assigned later
						);
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::FValidBucket
//
//	@doc:
//		Given lower and upper and their closedness, test if they
//		can form a valid bucket.
//		E.g. [1,1) (2,3) are not valid integer buckets, (2.0, 3.0) is a
//		valid numeric bucket.
//
//---------------------------------------------------------------------------
BOOL
CStatisticsUtils::FValidBucket
	(
	CPoint *ppointLower,
	CPoint *ppointUpper,
	BOOL fLowerClosed,
	BOOL fUpperClosed
	)
{
	if (ppointLower->FGreaterThan(ppointUpper))
	{
		return false;
	}

	// e.g. [1.0, 1.0) is not valid
	if (ppointLower->FEqual(ppointUpper) && (!fLowerClosed || !fUpperClosed))
	{
		return false;
	}

	// datum has statsDistance, so must be statsMappable
	const IDatum *pdatum = ppointLower->Pdatum();
	const IDatumStatisticsMappable *pdatumsm = dynamic_cast<const IDatumStatisticsMappable*>(pdatum);

	// for types which have integer mapping for stats purposes, e.g. int2,int4, etc.
	if (pdatumsm->FHasStatsLINTMapping())
	{
		// test if this integer bucket is well-defined
		CDouble dVal = ppointUpper->DDistance(ppointLower);
		if (!fLowerClosed)
		{
			dVal = dVal + CDouble(-1.0);
		}
		if (!fUpperClosed)
		{
			dVal = dVal + CDouble(-1.0);
		}
		if (CDouble(0) > dVal)
		{
			return false;
		}
	}

	return true;
}

//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::DistributeBucketProperties
//
//	@doc:
//		Set dDistinct and dFrequency of the new buckets according to
//		their ranges, based on the assumption that values are uniformly
//		distributed within a bucket.
//
//---------------------------------------------------------------------------
void
CStatisticsUtils::DistributeBucketProperties
	(
	CDouble dFrequencyTotal,
	CDouble dDistinctTotal,
	DrgPbucket *pdrgpbucket
	)
{
	GPOS_ASSERT(NULL != pdrgpbucket);

	CDouble dSumWidth = 0.0;
	const ULONG ulNew = pdrgpbucket->UlLength();

	for (ULONG ul = 0; ul < ulNew; ul++)
	{
		CBucket *pbucket = (*pdrgpbucket)[ul];
		if (!pbucket->FSingleton()) // the re-balance should exclude MCVs (singleton bucket)
		{
			dSumWidth = dSumWidth + pbucket->DWidth();
		}
	}
	for (ULONG ul = 0; ul < ulNew; ul++)
	{
		CBucket *pbucket = (*pdrgpbucket)[ul];
		if (!pbucket->FSingleton())
		{
			CDouble dFactor = pbucket->DWidth() / dSumWidth;
			pbucket->SetFrequency(dFrequencyTotal * dFactor);
			// TODO: , Aug 1 2013 - another heuristic may be max(1, dDisinct * dFactor)
			pbucket->SetDistinct(dDistinctTotal * dFactor);
		}
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PrintColStats
//
//	@doc:
//		Utility function to print column stats before/after applying filter
//
//---------------------------------------------------------------------------
void
CStatisticsUtils::PrintColStats
	(
	IMemoryPool *pmp,
	CStatsPred *pstatspred,
	ULONG ulColIdCond,
	CHistogram *phist,
	CDouble dScaleFactorLast,
	BOOL fBefore
	)
{
	GPOS_ASSERT(NULL != pstatspred);
	ULONG ulColId  = pstatspred->UlColId();
	if (ulColId == ulColIdCond && NULL != phist)
	{
		{
			CAutoTrace at(pmp);
			if (fBefore)
			{
				at.Os() << "BEFORE" << std::endl;
			}
			else
			{
				at.Os() << "AFTER" << std::endl;
			}

			phist->OsPrint(at.Os());
			at.Os() << "Scale Factor: " << dScaleFactorLast<< std::endl;
		}
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::ExtractUsedColIds
//
//	@doc:
//		Extract all the column identifiers used in the statistics filter
//
//---------------------------------------------------------------------------
void
CStatisticsUtils::ExtractUsedColIds
	(
	IMemoryPool *pmp,
	CBitSet *pbsColIds,
	CStatsPred *pstatspred,
	DrgPul *pdrgpulColIds
	)
{
	GPOS_ASSERT(NULL != pbsColIds);
	GPOS_ASSERT(NULL != pstatspred);
	GPOS_ASSERT(NULL != pdrgpulColIds);

	if (ULONG_MAX != pstatspred->UlColId())
	{
		// the predicate is on a single column

		(void) pbsColIds->FExchangeSet(pstatspred->UlColId());
		pdrgpulColIds->Append(GPOS_NEW(pmp) ULONG(pstatspred->UlColId()));

		return;
	}

	if (CStatsPred::EsptUnsupported == pstatspred->Espt())
	{
		return;
	}

	GPOS_ASSERT(CStatsPred::EsptConj == pstatspred->Espt() || CStatsPred::EsptDisj == pstatspred->Espt());

	DrgPstatspred *pdrgpstatspred = NULL;
	if (CStatsPred::EsptConj == pstatspred->Espt())
	{
		pdrgpstatspred = CStatsPredConj::PstatspredConvert(pstatspred)->Pdrgpstatspred();
	}
	else
	{
		pdrgpstatspred = CStatsPredDisj::PstatspredConvert(pstatspred)->Pdrgpstatspred();
	}

	GPOS_ASSERT(NULL != pdrgpstatspred);
	const ULONG ulArity = pdrgpstatspred->UlLength();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		CStatsPred *pstatspredCurr = (*pdrgpstatspred)[ul];
		ULONG ulColId = pstatspredCurr->UlColId();

		if (ULONG_MAX != ulColId)
		{
			if (!pbsColIds->FBit(ulColId))
			{
				(void) pbsColIds->FExchangeSet(ulColId);
				pdrgpulColIds->Append(GPOS_NEW(pmp) ULONG(ulColId));
			}
		}
		else if (CStatsPred::EsptUnsupported != pstatspredCurr->Espt())
		{
			GPOS_ASSERT(CStatsPred::EsptConj == pstatspredCurr->Espt() || CStatsPred::EsptDisj == pstatspredCurr->Espt());
			ExtractUsedColIds(pmp, pbsColIds, pstatspredCurr, pdrgpulColIds);
		}
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::UpdateDisjStatistics
//
//	@doc:
//		Given the previously generated histogram, update the intermediate
//		result of the disjunction
//
//---------------------------------------------------------------------------
void
CStatisticsUtils::UpdateDisjStatistics
	(
	IMemoryPool *pmp,
	CBitSet *pbsDontUpdateStats,
	CDouble dRowsInputDisj,
	CDouble dRowsLocal,
	CHistogram *phistPrev,
	HMUlHist *phmulhistResultDisj,
	ULONG ulColId
	)
{
	GPOS_ASSERT(NULL != pbsDontUpdateStats);
	GPOS_ASSERT(NULL != phmulhistResultDisj);

	if (NULL != phistPrev && ULONG_MAX != ulColId && !pbsDontUpdateStats->FBit(ulColId))
	{
		// 1. the filter is on the same column because ULONG_MAX != ulColId
		// 2. the histogram of the column can be updated
		CHistogram *phistResult = phmulhistResultDisj->PtLookup(&ulColId);
		if (NULL != phistResult)
		{
			// since there is already a histogram for this column,
			// union the previously generated histogram with the newly generated
			// histogram for this column
			CDouble dRowOutput(0.0);
			CHistogram *phistNew = phistPrev->PhistUnionNormalized
												(
												pmp,
												dRowsInputDisj,
												phistResult,
												dRowsLocal,
												&dRowOutput
												);

			GPOS_DELETE(phistPrev);
			phistPrev = phistNew;
		}

		AddHistogram
			(
			pmp,
			ulColId,
			phistPrev,
			phmulhistResultDisj,
			true /*fReplaceOldEntries*/
			);
	}

	GPOS_DELETE(phistPrev);
}

//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PbsNonUpdatableHistForDisj
//
//	@doc:
//		Given a disjunction filter, generate a bit set of columns whose
// 		histogram buckets cannot be changed by applying the predicates in the
//		disjunction
//---------------------------------------------------------------------------
CBitSet *
CStatisticsUtils::PbsNonUpdatableHistForDisj
	(
	IMemoryPool *pmp,
	CStatsPredDisj *pstatspred
	)
{
	GPOS_ASSERT(NULL != pstatspred);

	// Consider the following disjunctive predicates:
	// Case 1: ((x == 1) OR (x == 2 AND y == 2))
	// In such scenarios, predicate y is only operated on by the second child.
	// Therefore the output of the disjunction should not see the effects on
	// y's histogram due to the second child. In other words, DO NOT
	// update histogram buckets for y.

	// Case 2: ((x == 1 AND y== 1) OR (x == 2 AND y == 2))
	// In such scenarios both child predicate operate on both x and y
	// therefore the output of the disjunction for each column should be
	// the union of stats of each predicate being applied separately.
	// In other words, DO update histogram buckets for both x and y.

	CBitSet *pbsNonUpdateable = GPOS_NEW(pmp) CBitSet(pmp);

	const ULONG ulDisjColId = pstatspred->UlColId();
	if (ULONG_MAX != ulDisjColId)
	{
		// disjunction predicate on a single column so all are updatable
		return pbsNonUpdateable;
	}

	CBitSet *pbsDisj = GPOS_NEW(pmp) CBitSet(pmp);
	DrgPul *pdrgpulDisj = GPOS_NEW(pmp) DrgPul(pmp);
	ExtractUsedColIds(pmp, pbsDisj, pstatspred, pdrgpulDisj);
	const ULONG ulDisjUsedCol = pdrgpulDisj->UlLength();

	const ULONG ulArity = pstatspred->UlFilters();
	for (ULONG ulChildIdx = 0; ulChildIdx < ulArity; ulChildIdx++)
	{
		CStatsPred *pstatspredChild = pstatspred->Pstatspred(ulChildIdx);
		CBitSet *pbsChild = GPOS_NEW(pmp) CBitSet(pmp);
		DrgPul *pdrgpulChild = GPOS_NEW(pmp) DrgPul(pmp);
		ExtractUsedColIds(pmp, pbsChild, pstatspredChild, pdrgpulChild);

		const ULONG ulLen = pdrgpulChild->UlLength();
		GPOS_ASSERT(ulLen <= ulDisjUsedCol);
		if (ulLen < ulDisjUsedCol)
		{
			// the child predicate only operates on a subset of all the columns
			// used in the disjunction
			for (ULONG ulUsedColIdx = 0; ulUsedColIdx < ulDisjUsedCol; ulUsedColIdx++)
			{
				ULONG ulColId = *(*pdrgpulDisj)[ulUsedColIdx];
				if (!pbsChild->FBit(ulColId))
				{
					(void) pbsNonUpdateable->FExchangeSet(ulColId);
				}
			}
		}

		// clean up
		pdrgpulChild->Release();
		pbsChild->Release();
	}

	// clean up
	pdrgpulDisj->Release();
	pbsDisj->Release();

	return pbsNonUpdateable;

}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::AddHistogram
//
//	@doc:
//		Add histogram to histogram map if not already present.
//
//---------------------------------------------------------------------------
void
CStatisticsUtils::AddHistogram
	(
	IMemoryPool *pmp,
	ULONG ulColId,
	const CHistogram *phist,
	HMUlHist *phmulhist,
	BOOL fReplaceOld
	)
{
	GPOS_ASSERT(NULL != phist);

	if (NULL == phmulhist->PtLookup(&ulColId))
	{
#ifdef GPOS_DEBUG
		BOOL fRes =
#endif
		phmulhist->FInsert(GPOS_NEW(pmp) ULONG(ulColId), phist->PhistCopy(pmp));
		GPOS_ASSERT(fRes);
	}
	else if (fReplaceOld)
	{
#ifdef GPOS_DEBUG
		BOOL fRes =
#endif
		phmulhist->FReplace(&ulColId, phist->PhistCopy(pmp));
		GPOS_ASSERT(fRes);
	}
}


#ifdef GPOS_DEBUG
//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PrintHistogramMap
//
//	@doc:
//		Helper method to print the hash map of histograms
//
//---------------------------------------------------------------------------
void
CStatisticsUtils::PrintHistogramMap
	(
	IOstream &os,
	HMUlHist *phmulhist
	)
{
	GPOS_ASSERT(NULL != phmulhist);

	HMIterUlHist hmiterulhist(phmulhist);
	while (hmiterulhist.FAdvance())
	{
		ULONG ulCol = *(hmiterulhist.Pk());

		os << "Column Id: " << ulCol << std::endl;
		const CHistogram *phist = hmiterulhist.Pt();
		phist->OsPrint(os);
	}
}
#endif // GPOS_DEBUG

//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PhmulhistMergeAfterDisjChild
//
//	@doc:
//		Create a new hash map of histograms after merging
//		the histograms generated by the child of disjunction
//
//---------------------------------------------------------------------------
HMUlHist *
CStatisticsUtils::PhmulhistMergeAfterDisjChild
	(
	IMemoryPool *pmp,
	CBitSet *pbsStatsNonUpdateableCols,
	HMUlHist *phmulhist,
	HMUlHist *phmulhistDisjChild,
	CDouble dRowsCumulative,
	CDouble dRowsDisjChild
	)
{
	GPOS_ASSERT(NULL != pbsStatsNonUpdateableCols);
	GPOS_ASSERT(NULL != phmulhist);
	GPOS_ASSERT(NULL != phmulhistDisjChild);

	BOOL fEmpty = (CStatistics::DEpsilon >= dRowsDisjChild);
	CDouble dRowOutput(CStatistics::DMinRows.DVal());

	HMUlHist *phmulhistMergeResult = GPOS_NEW(pmp) HMUlHist(pmp);

	// iterate over the new hash map of histograms and only add
	// histograms of columns whose output statistics can be updated
	HMIterUlHist hmiterulhistDisjChild(phmulhistDisjChild);
	while (hmiterulhistDisjChild.FAdvance())
	{
		ULONG ulColIdDisjChild = *(hmiterulhistDisjChild.Pk());
		const CHistogram *phistDisjChild = hmiterulhistDisjChild.Pt();
		if (!pbsStatsNonUpdateableCols->FBit(ulColIdDisjChild))
		{
			if (!fEmpty)
			{
				AddHistogram(pmp, ulColIdDisjChild, phistDisjChild, phmulhistMergeResult);
			}
			else
			{
				// add a dummy statistics object since the estimated number of rows for
				// disjunction child is "0"
				phmulhistMergeResult->FInsert
									(
									GPOS_NEW(pmp) ULONG(ulColIdDisjChild),
									 GPOS_NEW(pmp) CHistogram(GPOS_NEW(pmp) DrgPbucket(pmp), false /* fWellDefined */)
									);
			}
		}
		GPOS_CHECK_ABORT;
	}

	// iterate over the previously generated histograms and
	// union them with newly created hash map of histograms (if these columns are updatable)
	HMIterUlHist hmiterulhist(phmulhist);
	while (hmiterulhist.FAdvance())
	{
		ULONG ulColId = *(hmiterulhist.Pk());
		const CHistogram *phist = hmiterulhist.Pt();
		if (NULL != phist && !pbsStatsNonUpdateableCols->FBit(ulColId))
		{
			if (fEmpty)
			{
				// since the estimated output of the disjunction child is "0" tuples
				// no point merging histograms.
				AddHistogram
					(
					pmp,
					ulColId,
					phist,
					phmulhistMergeResult,
					true /* fReplaceOld */
					);
			}
			else
			{
				const CHistogram *phistDisjChild = phmulhistDisjChild->PtLookup(&ulColId);
				CHistogram *phistMergeResult = phist->PhistUnionNormalized
													(
													pmp,
													dRowsCumulative,
													phistDisjChild,
													dRowsDisjChild,
													&dRowOutput
													);

				AddHistogram
					(
					pmp,
					ulColId,
					phistMergeResult,
					phmulhistMergeResult,
					true /* fReplaceOld */
					);

				GPOS_DELETE(phistMergeResult);
			}

			GPOS_CHECK_ABORT;
		}
	}

	return phmulhistMergeResult;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PhmulhistCopy
//
//	@doc:
//		Helper method to copy the hash map of histograms
//
//---------------------------------------------------------------------------
HMUlHist *
CStatisticsUtils::PhmulhistCopy
	(
	IMemoryPool *pmp,
	HMUlHist *phmulhist
	)
{
	GPOS_ASSERT(NULL != phmulhist);

	HMUlHist *phmulhistCopy = GPOS_NEW(pmp) HMUlHist(pmp);

	HMIterUlHist hmiterulhist(phmulhist);
	while (hmiterulhist.FAdvance())
	{
		ULONG ulColId = *(hmiterulhist.Pk());
		const CHistogram *phist = hmiterulhist.Pt();
		AddHistogram(pmp, ulColId, phist, phmulhistCopy);
		GPOS_CHECK_ABORT;
	}

	return phmulhistCopy;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::UlColId
//
//	@doc:
//		Return the column identifier of the filter if the predicate is
// 		on a single column else	return ULONG_MAX
//
//---------------------------------------------------------------------------
ULONG
CStatisticsUtils::UlColId
	(
	const DrgPstatspred *pdrgpstatspred
	)
{
	GPOS_ASSERT(NULL != pdrgpstatspred);

	ULONG ulColIdResult = ULONG_MAX;
	BOOL fSameCol = true;

	const ULONG ulLen = pdrgpstatspred->UlLength();
	for (ULONG ul = 0; ul < ulLen && fSameCol; ul++)
	{
		CStatsPred *pstatspred = (*pdrgpstatspred)[ul];
		ULONG ulColId =  pstatspred->UlColId();
		if (ULONG_MAX == ulColIdResult)
		{
			ulColIdResult = ulColId;
		}
		fSameCol = (ulColIdResult == ulColId);
	}

	if (fSameCol)
	{
		return ulColIdResult;
	}

	return ULONG_MAX;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PdatumNull
//
//	@doc:
//		Generate a null datum given a column reference
//
//---------------------------------------------------------------------------
IDatum *
CStatisticsUtils::PdatumNull
	(
	const CColRef *pcr
	)
{
	const IMDType *pmdtype = pcr->Pmdtype();

	IDatum *pdatum = pmdtype->PdatumNull();
	pdatum->AddRef();

	return pdatum;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PstatsDeriveWithOuterRefs
//
//	@doc:
//		Derive statistics when scalar expression uses outer references,
//		we pull statistics for outer references from the passed statistics
//		context, and use Join statistics derivation in this case
//
//		For example:
//
//			Join
//			 |--Get(R)
//			 +--Join
//				|--Get(S)
//				+--Select(T.t=R.r)
//					+--Get(T)
//
//		when deriving statistics on 'Select(T.t=R.r)', we join T with the
//		cross product (R x S) based on the condition (T.t=R.r)
//
//---------------------------------------------------------------------------
IStatistics *
CStatisticsUtils::PstatsDeriveWithOuterRefs
	(
	IMemoryPool *pmp,
	BOOL fOuterJoin,
	CExpressionHandle &
#ifdef GPOS_DEBUG
	exprhdl // handle attached to the logical expression we want to derive stats for
#endif // GPOS_DEBUG
	,
	CExpression *pexprScalar, // scalar condition to be used for stats derivation
	IStatistics *pstats, // statistics object of the attached expression
	DrgPstat *pdrgpstatOuter // array of stats objects where outer references are defined
	)
{
	GPOS_ASSERT(exprhdl.FHasOuterRefs() && "attached expression does not have outer references");
	GPOS_ASSERT(NULL != pexprScalar);
	GPOS_ASSERT(NULL != pstats);
	GPOS_ASSERT(NULL != pdrgpstatOuter);
	GPOS_ASSERT(0 < pdrgpstatOuter->UlLength());

	// join outer stats object based on given scalar expression,
	// we use inner join semantics here to consider all relevant combinations of outer tuples
	IStatistics *pstatsOuter = PstatsJoinArray(pmp, false /*fOuterJoin*/, pdrgpstatOuter, pexprScalar);
	CDouble dRowsOuter = pstatsOuter->DRows();

	// join passed stats object and outer stats based on the passed join type
	DrgPstat *pdrgpstat = GPOS_NEW(pmp) DrgPstat(pmp);
	pdrgpstat->Append(pstatsOuter);
	pstats->AddRef();
	pdrgpstat->Append(pstats);
	IStatistics *pstatsJoined = PstatsJoinArray(pmp, fOuterJoin, pdrgpstat, pexprScalar);
	pdrgpstat->Release();

	// scale result using cardinality of outer stats and set number of rebinds of returned stats
	IStatistics *pstatsResult = pstatsJoined->PstatsScale(pmp, CDouble(1.0/dRowsOuter));
	pstatsResult->SetRebinds(dRowsOuter);
	pstatsJoined->Release();

	return pstatsResult;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PstatsFilter
//
//	@doc:
//		Derive statistics for filter operation based on given scalar expression
//
//---------------------------------------------------------------------------
IStatistics *
CStatisticsUtils::PstatsFilter
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	IStatistics *pstatsChild,
	CExpression *pexprScalarLocal, // filter expression on local columns only
	CExpression *pexprScalarOuterRefs, // filter expression involving outer references
	DrgPstat *pdrgpstatOuter
	)
{
	GPOS_ASSERT(NULL != pstatsChild);
	GPOS_ASSERT(NULL != pexprScalarLocal);
	GPOS_ASSERT(NULL != pexprScalarOuterRefs);
	GPOS_ASSERT(NULL != pdrgpstatOuter);

	CColRefSet *pcrsOuterRefs = exprhdl.Pdprel()->PcrsOuter();
	
	// TODO  June 13 2014, we currently only cap ndvs when we have a filter
	// immediately on top of tables
	BOOL fCapNdvs = (1 == exprhdl.Pdprel()->UlJoinDepth());

	// extract local filter
	CStatsPred *pstatspred = CStatsPredUtils::PstatspredExtract(pmp, pexprScalarLocal, pcrsOuterRefs);

	// derive stats based on local filter
	IStatistics *pstatsResult = pstatsChild->PstatsFilter(pmp, pstatspred, fCapNdvs);
	pstatspred->Release();

	if (exprhdl.FHasOuterRefs() && 0 < pdrgpstatOuter->UlLength())
	{
		// derive stats based on outer references
		IStatistics *pstats = PstatsDeriveWithOuterRefs(pmp, false /*fOuterJoin*/, exprhdl, pexprScalarOuterRefs, pstatsResult, pdrgpstatOuter);
		pstatsResult->Release();
		pstatsResult = pstats;
	}

	return pstatsResult;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PstatsJoinArray
//
//	@doc:
//		Derive statistics for the given join predicate
//
//---------------------------------------------------------------------------
IStatistics *
CStatisticsUtils::PstatsJoinArray
	(
	IMemoryPool *pmp,
	BOOL fOuterJoin,
	DrgPstat *pdrgpstat,
	CExpression *pexprScalar
	)
{
	GPOS_ASSERT(NULL != pexprScalar);
	GPOS_ASSERT(NULL != pdrgpstat);
	GPOS_ASSERT(0 < pdrgpstat->UlLength());
	GPOS_ASSERT_IMP(fOuterJoin, 2 == pdrgpstat->UlLength());

	// create an empty set of outer references for statistics derivation
	CColRefSet *pcrsOuterRefs = GPOS_NEW(pmp) CColRefSet(pmp);

	// join statistics objects one by one using relevant predicates in given scalar expression
	const ULONG ulStats = pdrgpstat->UlLength();
	IStatistics *pstats = (*pdrgpstat)[0]->PstatsCopy(pmp);

	for (ULONG ul = 1; ul < ulStats; ul++)
	{
		IStatistics *pstatsCurrent = (*pdrgpstat)[ul];

		DrgPcrs *pdrgpcrsOutput= GPOS_NEW(pmp) DrgPcrs(pmp);
		pdrgpcrsOutput->Append(pstats->Pcrs(pmp));
		pdrgpcrsOutput->Append(pstatsCurrent->Pcrs(pmp));

		CStatsPred *pstatspredUnsupported = NULL;
		DrgPstatsjoin *pdrgpstatsjoin = CStatsPredUtils::PdrgpstatsjoinExtract
															(
															pmp,
															pexprScalar,
															pdrgpcrsOutput,
															pcrsOuterRefs,
															&pstatspredUnsupported
															);
		IStatistics *pstatsNew = NULL;
		if (fOuterJoin)
		{
			pstatsNew = pstats->PstatsLOJ(pmp, pstatsCurrent, pdrgpstatsjoin);
		}
		else
		{
			pstatsNew = pstats->PstatsInnerJoin(pmp, pstatsCurrent, pdrgpstatsjoin);
		}
		pstats->Release();
		pstats = pstatsNew;

		if (NULL != pstatspredUnsupported)
		{
			// apply the unsupported join filters as a filter on top of the join results.
			// TODO,  June 13 2014 we currently only cap NDVs for filters
			// immediately on top of tables.
			IStatistics *pstatsAfterJoinFilter = pstats->PstatsFilter
															(
															pmp,
															pstatspredUnsupported,
															false /* fCapNdvs */
															);
			pstats->Release();
			pstats = pstatsAfterJoinFilter;

			pstatspredUnsupported->Release();
		}

		pdrgpstatsjoin->Release();
		pdrgpcrsOutput->Release();
	}

	// clean up
	pcrsOuterRefs->Release();

	return pstats;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PstatsJoinWithOuterRefs
//
//	@doc:
//		Helper for deriving statistics for join operation based on given scalar expression
//
//---------------------------------------------------------------------------
IStatistics *
CStatisticsUtils::PstatsJoinWithOuterRefs
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	DrgPstat *pdrgpstatChildren,
	CExpression *pexprScalarLocal, // filter expression on local columns only
	CExpression *pexprScalarOuterRefs, // filter expression involving outer references
	DrgPstat *pdrgpstatOuter
	)
{
	GPOS_ASSERT(NULL != pdrgpstatChildren);
	GPOS_ASSERT(NULL != pexprScalarLocal);
	GPOS_ASSERT(NULL != pexprScalarOuterRefs);
	GPOS_ASSERT(NULL != pdrgpstatOuter);

	COperator::EOperatorId eopid = exprhdl.Pop()->Eopid();
	GPOS_ASSERT(COperator::EopLogicalLeftOuterJoin == eopid ||
			COperator::EopLogicalInnerJoin == eopid ||
			COperator::EopLogicalNAryJoin == eopid);

	BOOL fOuterJoin = (COperator::EopLogicalLeftOuterJoin == eopid);

	// derive stats based on local join condition
	IStatistics *pstatsResult = PstatsJoinArray(pmp, fOuterJoin, pdrgpstatChildren, pexprScalarLocal);

	if (exprhdl.FHasOuterRefs() && 0 < pdrgpstatOuter->UlLength())
	{
		// derive stats based on outer references
		IStatistics *pstats = PstatsDeriveWithOuterRefs(pmp, fOuterJoin, exprhdl, pexprScalarOuterRefs, pstatsResult, pdrgpstatOuter);
		pstatsResult->Release();
		pstatsResult = pstats;
	}

	return pstatsResult;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PstatsJoin
//
//	@doc:
//		 Derive statistics for join operation given array of statistics object
//
//---------------------------------------------------------------------------
IStatistics *
CStatisticsUtils::PstatsJoin
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	DrgPstat *pdrgpstatCtxt
	)
{
	GPOS_ASSERT(CLogical::EspNone < CLogical::PopConvert(exprhdl.Pop())->Esp(exprhdl));

	DrgPstat *pdrgpstat = GPOS_NEW(pmp) DrgPstat(pmp);
	const ULONG ulArity = exprhdl.UlArity();
	for (ULONG ul = 0; ul < ulArity - 1; ul++)
	{
		IStatistics *pstatsChild = exprhdl.Pstats(ul);
		pstatsChild->AddRef();
		pdrgpstat->Append(pstatsChild);
	}

	CExpression *pexprJoinPred = NULL;
	if (exprhdl.Pdpscalar(ulArity - 1)->FHasSubquery())
	{
		// in case of subquery in join predicate, assume join condition is True
		pexprJoinPred = CUtils::PexprScalarConstBool(pmp, true /*fVal*/);
	}
	else
	{
		// remove implied predicates from join condition to avoid cardinality under-estimation
		pexprJoinPred = CPredicateUtils::PexprRemoveImpliedConjuncts(pmp, exprhdl.PexprScalarChild(ulArity - 1), exprhdl);
	}

	// split join predicate into local predicate and predicate involving outer references
	CExpression *pexprLocal = NULL;
	CExpression *pexprOuterRefs = NULL;

	// get outer references from expression handle
	CColRefSet *pcrsOuter = exprhdl.Pdprel()->PcrsOuter();

	CPredicateUtils::SeparateOuterRefs(pmp, pexprJoinPred, pcrsOuter, &pexprLocal, &pexprOuterRefs);
	pexprJoinPred->Release();

	IStatistics *pstatsJoin = PstatsJoinWithOuterRefs(pmp, exprhdl, pdrgpstat, pexprLocal, pexprOuterRefs, pdrgpstatCtxt);
	pexprLocal->Release();
	pexprOuterRefs->Release();

	pdrgpstat->Release();

	return pstatsJoin;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PstatsDynamicScan
//
//	@doc:
//		Derive statistics of dynamic scan based on the stats of corresponding
//		part-selector in the given map,
//
//		for a given part table (R) with part selection predicate (R.pk=T.x),
//		the function assumes a LeftSemiJoin(R,T)_(R.pk=T.x) expression to
//		compute the stats of R after partitions are eliminated based on the
//		condition (R.pk=T.x)
//
//---------------------------------------------------------------------------
IStatistics *
CStatisticsUtils::PstatsDynamicScan
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	ULONG ulPartIndexId,
	CPartFilterMap *ppfm
	)
{
	// extract part table base stats from passed handle
	IStatistics *pstatsBase = exprhdl.Pstats();
	GPOS_ASSERT(NULL != pstatsBase);

	if (!GPOS_FTRACE(EopttraceDeriveStatsForDPE))
	{
		// if stats derivation with dynamic partition elimitaion is disabled, we return base stats
		pstatsBase->AddRef();
		return pstatsBase;
	}

	if(!ppfm->FContainsScanId(ulPartIndexId) || NULL == ppfm->Pstats(ulPartIndexId))
	{
		// no corresponding entry is found in map, return base stats
		pstatsBase->AddRef();
		return pstatsBase;
	}

	IStatistics *pstatsPartSelector = ppfm->Pstats(ulPartIndexId);
	CExpression *pexprScalar = ppfm->Pexpr(ulPartIndexId);

	DrgPcrs *pdrgpcrsOutput = GPOS_NEW(pmp) DrgPcrs(pmp);
	pdrgpcrsOutput->Append(pstatsBase->Pcrs(pmp));
	pdrgpcrsOutput->Append(pstatsPartSelector->Pcrs(pmp));

	CColRefSet *pcrsOuterRefs = GPOS_NEW(pmp) CColRefSet(pmp);

	// extract all the conjuncts
	CStatsPred *pstatspredUnsupported = NULL;
	DrgPstatsjoin *pdrgpstatsjoin = CStatsPredUtils::PdrgpstatsjoinExtract
														(
														pmp,
														pexprScalar,
														pdrgpcrsOutput,
														pcrsOuterRefs,
														&pstatspredUnsupported
														);

	IStatistics *pstatLSJoin = pstatsBase->PstatsLSJoin(pmp, pstatsPartSelector, pdrgpstatsjoin);

	// TODO:  May 15 2014, handle unsupported predicates for LS joins
	// cleanup
	CRefCount::SafeRelease(pstatspredUnsupported);
	pdrgpcrsOutput->Release();
	pcrsOuterRefs->Release();
	pdrgpstatsjoin->Release();

	return pstatLSJoin;
}

//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PstatsIndexGet
//
//	@doc:
//		Derive statistics of (dynamic) index get
//
//---------------------------------------------------------------------------
IStatistics *
CStatisticsUtils::PstatsIndexGet
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	DrgPstat *pdrgpstatCtxt
	)
{
	COperator::EOperatorId eopid = exprhdl.Pop()->Eopid() ;
	GPOS_ASSERT(CLogical::EopLogicalIndexGet == eopid ||
			CLogical::EopLogicalDynamicIndexGet == eopid);

	// collect columns used by index conditions and distribution of the table
	// for statistics
	CColRefSet *pcrsUsed = GPOS_NEW(pmp) CColRefSet(pmp);

	CTableDescriptor *ptabdesc = NULL;
	if (CLogical::EopLogicalIndexGet == eopid)
	{
		CLogicalIndexGet *popIndexGet = CLogicalIndexGet::PopConvert(exprhdl.Pop());
		ptabdesc = popIndexGet->Ptabdesc();
		if (NULL != popIndexGet->PcrsDist())
		{
			pcrsUsed->Include(popIndexGet->PcrsDist());
		}
	}
	else
	{
		CLogicalDynamicIndexGet *popDynamicIndexGet = CLogicalDynamicIndexGet::PopConvert(exprhdl.Pop());
		ptabdesc = popDynamicIndexGet->Ptabdesc();
		if (NULL != popDynamicIndexGet->PcrsDist())
		{
			pcrsUsed->Include(popDynamicIndexGet->PcrsDist());
		}
	}

	CExpression *pexprScalar = exprhdl.PexprScalarChild(0 /*ulChidIndex*/);
	CExpression *pexprLocal = NULL;
	CExpression *pexprOuterRefs = NULL;

	// get outer references from expression handle
	CColRefSet *pcrsOuter = exprhdl.Pdprel()->PcrsOuter();

	CPredicateUtils::SeparateOuterRefs(pmp, pexprScalar, pcrsOuter, &pexprLocal, &pexprOuterRefs);

	pcrsUsed->Union(exprhdl.Pdpscalar(0 /*ulChildIndex*/)->PcrsUsed());

	// filter out outer references in used columns
	pcrsUsed->Difference(pcrsOuter);

	IStatistics *pstatsBaseTable = CLogical::PstatsBaseTable(pmp, exprhdl, ptabdesc, pcrsUsed);
	pcrsUsed->Release();

	IStatistics *pstats = PstatsFilter(pmp, exprhdl, pstatsBaseTable, pexprLocal, pexprOuterRefs, pdrgpstatCtxt);

	pstatsBaseTable->Release();
	pexprLocal->Release();
	pexprOuterRefs->Release();

	return pstats;
}

//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PstatsBitmapGet
//
//	@doc:
//		Derive statistics of bitmap table get
//
//---------------------------------------------------------------------------
IStatistics *
CStatisticsUtils::PstatsBitmapTableGet
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	DrgPstat *pdrgpstatCtxt
	)
{
	GPOS_ASSERT(CLogical::EopLogicalBitmapTableGet == exprhdl.Pop()->Eopid() ||
				CLogical::EopLogicalDynamicBitmapTableGet == exprhdl.Pop()->Eopid());
	CTableDescriptor *ptabdesc = CLogical::PtabdescFromTableGet(exprhdl.Pop());

	// the index of the condition
	ULONG ulChildConditionIndex = 0;

	// get outer references from expression handle
	CColRefSet *pcrsOuter = exprhdl.Pdprel()->PcrsOuter();
	CExpression *pexprLocal = NULL;
	CExpression *pexprOuterRefs = NULL;
	CExpression *pexprScalar = exprhdl.PexprScalarChild(ulChildConditionIndex);
	CPredicateUtils::SeparateOuterRefs(pmp, pexprScalar, pcrsOuter, &pexprLocal, &pexprOuterRefs);

	// collect columns used by the index
	CColRefSet *pcrsUsed = GPOS_NEW(pmp) CColRefSet(pmp);
	pcrsUsed->Union(exprhdl.Pdpscalar(ulChildConditionIndex)->PcrsUsed());
	pcrsUsed->Difference(pcrsOuter);
	IStatistics *pstatsBaseTable = CLogical::PstatsBaseTable(pmp, exprhdl, ptabdesc, pcrsUsed);
	pcrsUsed->Release();
	IStatistics *pstats = PstatsFilter
							(
							pmp,
							exprhdl,
							pstatsBaseTable,
							pexprLocal,
							pexprOuterRefs,
							pdrgpstatCtxt
							);

	pstatsBaseTable->Release();
	pexprLocal->Release();
	pexprOuterRefs->Release();

	return pstats;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PhmpuldrgpulTblOpIdToGrpColsMap
//
//	@doc:
//		Return the mapping between the table column used for grouping to the
//	    logical operator id where it was defined. If the grouping column is
//      not a table column then the logical op id is initialized to ULONG_MAX
//---------------------------------------------------------------------------
HMUlPdrgpul *
CStatisticsUtils::PhmpuldrgpulTblOpIdToGrpColsMap
	(
	IMemoryPool *pmp,
	CStatistics *pstats,
	const CColRefSet *pcrsGrpCols,
	CBitSet *pbsKeys // keys derived during optimization
	)
{
	GPOS_ASSERT(NULL != pcrsGrpCols);
	GPOS_ASSERT(NULL != pstats);

	HMUlPdrgpul *phmulpdrgpul = GPOS_NEW(pmp) HMUlPdrgpul(pmp);

	CColumnFactory *pcf = COptCtxt::PoctxtFromTLS()->Pcf();

	// iterate over grouping columns
	CColRefSetIter crsi(*pcrsGrpCols);
	while (crsi.FAdvance())
	{
		CColRef *pcr = crsi.Pcr();
		ULONG ulColId = pcr->UlId();
		if (NULL == pbsKeys || pbsKeys->FBit(ulColId))
		{
			// if keys are available then only consider grouping columns defined as 
			// key columns else consider all grouping columns
			const CColRef *pcr = pcf->PcrLookup(ulColId);
			const ULONG ulIdxUpperBoundNDVs = pstats->UlIndexUpperBoundNDVs(pcr);
			const DrgPul *pdrgpul = phmulpdrgpul->PtLookup(&ulIdxUpperBoundNDVs);
			if (NULL == pdrgpul)
			{
				DrgPul *pdrgpulNew = GPOS_NEW(pmp) DrgPul(pmp);
				pdrgpulNew->Append(GPOS_NEW(pmp) ULONG(ulColId));
#ifdef GPOS_DEBUG
		BOOL fres =
#endif // GPOS_DEBUG
					phmulpdrgpul->FInsert(GPOS_NEW(pmp) ULONG(ulIdxUpperBoundNDVs), pdrgpulNew);
				GPOS_ASSERT(fres);
			}
			else
			{
				(const_cast<DrgPul *>(pdrgpul))->Append(GPOS_NEW(pmp) ULONG(ulColId));
			}
		}
	}

	return phmulpdrgpul;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::AddNdvForAllGrpCols
//
//	@doc:
//		Add the NDV for all of the grouping columns
//---------------------------------------------------------------------------
void
CStatisticsUtils::AddNdvForAllGrpCols
	(
	IMemoryPool *pmp,
	const CStatistics *pstatsInput,
	const DrgPul *pdrgpulGrpCol, // array of grouping column ids from a source
	DrgPdouble *pdrgpdNDV // output array of ndvs
	)
{
	GPOS_ASSERT(NULL != pdrgpulGrpCol);
	GPOS_ASSERT(NULL != pstatsInput);
	GPOS_ASSERT(NULL != pdrgpdNDV);

	const ULONG ulCols = pdrgpulGrpCol->UlLength();
	// iterate over grouping columns
	for (ULONG ul = 0; ul < ulCols; ul++)
	{
		ULONG ulColId = (*(*pdrgpulGrpCol)[ul]);

		CDouble dDistVals = CStatisticsUtils::DDefaultDistinctVals(pstatsInput->DRows());
		const CHistogram *phist = pstatsInput->Phist(ulColId);
		if (NULL != phist)
		{
			dDistVals = phist->DDistinct();
			if (phist->FEmpty())
			{
				dDistVals = DDefaultDistinctVals(pstatsInput->DRows());
			}
		}
		pdrgpdNDV->Append(GPOS_NEW(pmp) CDouble(dDistVals));
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PdrgPdoubleNDV
//
//	@doc:
//		Extract NDVs for the given array of grouping columns. If one or more
//      of the grouping columns' NDV have been capped, table has been filtered,
//      then we add the maximum NDV only for computing the cardinality of
//      the group by. The logic is that number of groups cannot be greater
//		than the card of filter table. Else we add the NDVs for all grouping
//      columns as is.
//---------------------------------------------------------------------------
DrgPdouble *
CStatisticsUtils::PdrgPdoubleNDV
	(
	IMemoryPool *pmp,
	CStatisticsConfig *pstatsconf,
	const IStatistics *pstats,
	CColRefSet *pcrsGrpCols,
	CBitSet *pbsKeys // keys derived during optimization
	)
{
	GPOS_ASSERT(NULL != pstats);
	GPOS_ASSERT(NULL != pcrsGrpCols);

	CStatistics *pstatsInput =  CStatistics::PstatsConvert(const_cast<IStatistics *>(pstats));

	DrgPdouble *pdrgpdNDV = GPOS_NEW(pmp) DrgPdouble(pmp);

	HMUlPdrgpul *phmulpdrgpul = PhmpuldrgpulTblOpIdToGrpColsMap(pmp, pstatsInput, pcrsGrpCols, pbsKeys);
	HMIterUlPdrgpul hmiterulpdrgpul(phmulpdrgpul);
	while (hmiterulpdrgpul.FAdvance())
	{
		ULONG ulSourceId = *(hmiterulpdrgpul.Pk());
		const DrgPul *pdrgpulPerSrc = hmiterulpdrgpul.Pt();

		if (ULONG_MAX == ulSourceId)
		{
			// this array of grouping columns represents computed columns.
			// Since we currently do not cap computed columns, we add all of their NDVs as is
			AddNdvForAllGrpCols(pmp, pstatsInput, pdrgpulPerSrc, pdrgpdNDV);
		}
		else
		{
			// compute the maximum number of groups when aggregated on columns from the given source
			CDouble dMaxGrpsPerSrc = DMaxGroupsFromSource(pmp, pstatsconf, pstatsInput, pdrgpulPerSrc);
			pdrgpdNDV->Append(GPOS_NEW(pmp) CDouble(dMaxGrpsPerSrc));
		}
	}

	// clean up
	phmulpdrgpul->Release();

	return pdrgpdNDV;
}

//---------------------------------------------------------------------------
//	@function:
//       CStatisticsUtils::FExistsCappedGrpCol
//
//  @doc:
//       Check to see if any one of the grouping columns has been capped
//---------------------------------------------------------------------------
BOOL
CStatisticsUtils::FExistsCappedGrpCol
	(
	const CStatistics *pstats,
	const DrgPul *pdrgpulGrpCol
	)
{
	GPOS_ASSERT(NULL != pstats);
	GPOS_ASSERT(NULL != pdrgpulGrpCol);

	const ULONG ulCols = pdrgpulGrpCol->UlLength();
	for (ULONG ul = 0; ul < ulCols; ul++)
	{
		ULONG ulColId = (*(*pdrgpulGrpCol)[ul]);
		const CHistogram *phist = pstats->Phist(ulColId);

		if (NULL != phist && phist->FScaledNDV())
		{
			return true;
		}
	}

	return false;
}


//---------------------------------------------------------------------------
//	@function:
//      CStatisticsUtils::DMaxNdv
//
//  @doc:
//      Return the maximum NDV given an array of grouping columns
//---------------------------------------------------------------------------
CDouble
CStatisticsUtils::DMaxNdv
	(
	const CStatistics *pstats,
	const DrgPul *pdrgpulGrpCol
	)
{
	GPOS_ASSERT(NULL != pstats);
	GPOS_ASSERT(NULL != pdrgpulGrpCol);

	const ULONG ulGrpCols = pdrgpulGrpCol->UlLength();

	CDouble dNdvMax(1.0);
	for (ULONG ul = 0; ul < ulGrpCols; ul++)
	{
		CDouble dNdv = CStatisticsUtils::DDefaultDistinctVals(pstats->DRows());

		ULONG ulColId = (*(*pdrgpulGrpCol)[ul]);
		const CHistogram *phist = pstats->Phist(ulColId);
		if (NULL != phist)
		{
			dNdv = phist->DDistinct();
			if (phist->FEmpty())
			{
				dNdv = CStatisticsUtils::DDefaultDistinctVals(pstats->DRows());
			}
		}

		if (dNdvMax < dNdv)
		{
			dNdvMax = dNdv;
		}
	}

	return dNdvMax;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::DMaxGroupsFromSource
//
//	@doc:
//		Compute max number of groups when grouping on columns from the given source
//---------------------------------------------------------------------------
CDouble
CStatisticsUtils::DMaxGroupsFromSource
	(
	IMemoryPool *pmp,
	CStatisticsConfig *pstatsconf,
	CStatistics *pstatsInput,
	const DrgPul *pdrgpulPerSrc
	)
{
	GPOS_ASSERT(NULL != pstatsInput);
	GPOS_ASSERT(NULL != pdrgpulPerSrc);
	GPOS_ASSERT(0 < pdrgpulPerSrc->UlLength());

	CDouble dRowsInput = pstatsInput->DRows();

	CColumnFactory *pcf = COptCtxt::PoctxtFromTLS()->Pcf();
	CColRef *pcrFirst = pcf->PcrLookup(*(*pdrgpulPerSrc)[0]);
	CDouble dUpperBoundNDVs = pstatsInput->DUpperBoundNDVs(pcrFirst);

	DrgPdouble *pdrgpdNDV = GPOS_NEW(pmp) DrgPdouble(pmp);
	AddNdvForAllGrpCols(pmp, pstatsInput, pdrgpulPerSrc, pdrgpdNDV);

	// take the minimum of (a) the estimated number of groups from the columns of this source,
	// (b) input rows, and (c) cardinality upper bound for the given source in the
	// input statistics object

	// DNumOfDistVal internally damps the number of columns with our formula.
	// (a) For columns from the same table, they will be damped based on the formula in DNumOfDistVal
	// (b) If the group by has columns from multiple tables, they will again be damped by the formula
	// in DNumOfDistVal when we compute the final group by cardinality
	CDouble dGroups = std::min
							(
							std::max
								(
								CStatistics::DMinRows.DVal(),
								DNumOfDistinctVal(pstatsconf, pdrgpdNDV).DVal()
								),
							std::min(dRowsInput.DVal(), dUpperBoundNDVs.DVal())
							);
	pdrgpdNDV->Release();

	return dGroups;
}

//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::DGroups
//
//	@doc:
//		Compute the cumulative number of groups for the given set of grouping columns
//
//---------------------------------------------------------------------------
CDouble
CStatisticsUtils::DGroups
	(
	IMemoryPool *pmp,
	IStatistics *pstats,
	CStatisticsConfig *pstatsconf,
	DrgPul *pdrgpulGC,
	CBitSet *pbsKeys // keys derived during optimization
	)
{
	GPOS_ASSERT(NULL != pstats);
	GPOS_ASSERT(NULL != pstatsconf);
	GPOS_ASSERT(NULL != pdrgpulGC);

	CColRefSet *pcrsGrpColComputed = GPOS_NEW(pmp) CColRefSet(pmp);
	CColRefSet *pcrsGrpColForStats = PcrsGrpColsForStats(pmp, pdrgpulGC, pcrsGrpColComputed);

	DrgPdouble *pdrgpdNDV = PdrgPdoubleNDV(pmp, pstatsconf, pstats, pcrsGrpColForStats, pbsKeys);
	CDouble dGroups = std::min
							(
							std::max
								(
								CStatistics::DMinRows.DVal(),
								DNumOfDistinctVal(pstatsconf, pdrgpdNDV).DVal()
								),
							pstats->DRows().DVal()
							);

	// clean up
	pcrsGrpColForStats->Release();
	pcrsGrpColComputed->Release();
	pdrgpdNDV->Release();

	return dGroups;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::DNumOfDistinctVal
//
//	@doc:
//		Compute the total number of groups of the group by operator
//		from the array of NDVs of the individual grouping columns
//---------------------------------------------------------------------------
CDouble
CStatisticsUtils::DNumOfDistinctVal
	(
	CStatisticsConfig *pstatsconf,
	DrgPdouble *pdrgpdNDV
	)
{
	GPOS_ASSERT(NULL != pstatsconf);
	GPOS_ASSERT(NULL != pdrgpdNDV);

	CScaleFactorUtils::SortScalingFactor(pdrgpdNDV, true /* fDescending */);
	const ULONG ulDistVals = pdrgpdNDV->UlLength();

	if (0 == ulDistVals)
	{
		return CDouble(1.0);
	}

	CDouble dCumulativeNDV = *(*pdrgpdNDV)[0];
	for (ULONG ulDV = 1; ulDV < ulDistVals; ulDV++)
	{
		CDouble dNDV = *(*pdrgpdNDV)[ulDV];
		CDouble dNDVDamped = std::max
									(
									CHistogram::DMinDistinct.DVal(),
									(dNDV * CScaleFactorUtils::DDampingGroupBy(pstatsconf, ulDV)).DVal()
									);
		dCumulativeNDV = dCumulativeNDV * dNDVDamped;
	}

	return dCumulativeNDV;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::AddGrpColStats
//
//	@doc:
//		Add the statistics (histogram and width) of the grouping columns
//---------------------------------------------------------------------------
void
CStatisticsUtils::AddGrpColStats
	(
	IMemoryPool *pmp,
	const CStatistics *pstatsInput,
	CColRefSet *pcrsGrpCols,
	HMUlHist *phmulhistOutput,
	HMUlDouble *phmuldoubleWidthOutput
	)
{
	GPOS_ASSERT(NULL != pstatsInput);
	GPOS_ASSERT(NULL != pcrsGrpCols);
	GPOS_ASSERT(NULL != phmulhistOutput);
	GPOS_ASSERT(NULL != phmuldoubleWidthOutput);

	// iterate over grouping columns
	CColRefSetIter crsi(*pcrsGrpCols);
	while (crsi.FAdvance())
	{
		CColRef *pcr = crsi.Pcr();
		ULONG ulGrpColId = pcr->UlId();

		CDouble dDistVals(CHistogram::DMinDistinct);
		const CHistogram *phist = pstatsInput->Phist(ulGrpColId);
		if (NULL != phist)
		{
			CHistogram *phistAfter = phist->PhistGroupByNormalized(pmp, pstatsInput->DRows(), &dDistVals);
			if (phist->FScaledNDV())
			{
				phistAfter->SetNDVScaled();
			}
			AddHistogram(pmp, ulGrpColId, phistAfter, phmulhistOutput);
			GPOS_DELETE(phistAfter);
		}

		const CDouble *pdWidth = pstatsInput->PdWidth(ulGrpColId);
		if (NULL != pdWidth)
		{
			phmuldoubleWidthOutput->FInsert(GPOS_NEW(pmp) ULONG(ulGrpColId), GPOS_NEW(pmp) CDouble(*pdWidth));
		}
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::PcrsGrpColsForStats
//
//	@doc:
//		Return the set of grouping columns for statistics computation.
//		If the grouping column (c) is a computed column, for example c = f(a,b),
//		then we include columns a and b as the grouping column instead of c.
//---------------------------------------------------------------------------
CColRefSet *
CStatisticsUtils::PcrsGrpColsForStats
	(
	IMemoryPool *pmp,
	const DrgPul *pdrgpulGrpCol,
	CColRefSet *pcrsGrpColComputed
	)
{
	GPOS_ASSERT(NULL != pdrgpulGrpCol);
	GPOS_ASSERT(NULL != pcrsGrpColComputed);

	CColumnFactory *pcf = COptCtxt::PoctxtFromTLS()->Pcf();

	CColRefSet *pcrsGrpColForStats = GPOS_NEW(pmp) CColRefSet(pmp);

	const ULONG ulGrpCols = pdrgpulGrpCol->UlLength();

	// iterate over grouping columns
	for (ULONG ul = 0; ul < ulGrpCols; ul++)
	{
		ULONG ulColId = *(*pdrgpulGrpCol) [ul];
		CColRef *pcrGrpCol = pcf->PcrLookup(ulColId);
		GPOS_ASSERT(NULL != pcrGrpCol);

		// check to see if the grouping column is a computed attribute
		const CColRefSet *pcrsUsed = pcf->PcrsUsedInComputedCol(pcrGrpCol);
		if (NULL == pcrsUsed || 0 == pcrsUsed->CElements())
		{
			(void) pcrsGrpColForStats->Include(pcrGrpCol);
		}
		else
		{
			(void) pcrsGrpColForStats->Union(pcrsUsed);
			(void) pcrsGrpColComputed->Include(pcrGrpCol);
		}
	}

	return pcrsGrpColForStats;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::DNullFreqLASJ
//
//	@doc:
//		Compute the null frequency for LASJ
//
//---------------------------------------------------------------------------
CDouble
CStatisticsUtils::DNullFreqLASJ
	(
	CStatsPred::EStatsCmpType escmpt,
	const CHistogram *phistOuter,
	const CHistogram *phistInner
	)
{
	GPOS_ASSERT(NULL != phistOuter);
	GPOS_ASSERT(NULL != phistInner);

	if (CStatsPred::EstatscmptINDF != escmpt)
	{
		// for equality predicate NULLs on the outer side of the join
		// will not join with those in the inner side
		return phistOuter->DNullFreq();
	}

	if (CStatistics::DEpsilon < phistInner->DNullFreq())
	{
		// for INDF predicate NULLs on the outer side of the join
		// will join with those in the inner side if they are present
		return CDouble(0.0);
	}

	return phistOuter->DNullFreq();
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::DDistinct
//
//	@doc:
//		Sum of number of distinct values in the given array of buckets
//
//---------------------------------------------------------------------------
CDouble
CStatisticsUtils::DDistinct
	(
	const DrgPbucket *pdrgppbucket
	)
{
	GPOS_ASSERT(NULL != pdrgppbucket);

	CDouble dDistinct = CDouble(0.0);
	const ULONG ulBuckets = pdrgppbucket->UlLength();
	for (ULONG ulBucketIdx = 0; ulBucketIdx < ulBuckets; ulBucketIdx++)
	{
		CBucket *pbucket = (*pdrgppbucket)[ulBucketIdx];
		dDistinct = dDistinct + pbucket->DDistinct();
	}

	return dDistinct;
}


//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::DFrequency
//
//	@doc:
//		Sum of the frequencies of buckets in the given array of buckets
//
//---------------------------------------------------------------------------
CDouble
CStatisticsUtils::DFrequency
	(
	const DrgPbucket *pdrgppbucket
	)
{
	GPOS_ASSERT(NULL != pdrgppbucket);

	CDouble dFreq = CDouble(0.0);
	const ULONG ulBuckets = pdrgppbucket->UlLength();
	for (ULONG ulBucketIdx = 0; ulBucketIdx < ulBuckets; ulBucketIdx++)
	{
		CBucket *pbucket = (*pdrgppbucket)[ulBucketIdx];
		dFreq = dFreq +  pbucket->DFrequency();
	}

	return dFreq;
}

//---------------------------------------------------------------------------
//	@function:
//		CStatisticsUtils::FIncreasesRisk
//
//	@doc:
//		Return true if the given operator increases the risk of cardinality
//		misestimation.
//
//---------------------------------------------------------------------------

BOOL
CStatisticsUtils::FIncreasesRisk
	(
	CLogical *popLogical
	)
{
	if (popLogical->FSelectionOp())
	{
		// operators that perform filters (e.g. joins, select) increase the risk
		return true;
	}

	static COperator::EOperatorId rgeopidGroupingAndSemiJoinOps[] =
		{ COperator::EopLogicalGbAgg, COperator::EopLogicalIntersect,
		        COperator::EopLogicalIntersectAll, COperator::EopLogicalUnion,
		        COperator::EopLogicalDifference,
		        COperator::EopLogicalDifferenceAll };

	COperator::EOperatorId eopid = popLogical->Eopid();
	for (ULONG ul = 0; ul < GPOS_ARRAY_SIZE(rgeopidGroupingAndSemiJoinOps); ul++)
	{
		if (rgeopidGroupingAndSemiJoinOps[ul] == eopid)
		{
			return true;
		}
	}

	return false;
}


//---------------------------------------------------------------------------
//     @function:
//             CStatisticsUtils::DDefaultColumnWidth
//
//     @doc:
//             Return the default column width
//
//---------------------------------------------------------------------------
CDouble
CStatisticsUtils::DDefaultColumnWidth
	(
    const IMDType *pmdtype
	)
{
       CDouble dWidth(CStatistics::DDefaultColumnWidth);
       if (pmdtype->FFixedLength())
       {
    	   dWidth = CDouble(pmdtype->UlLength());
       }

       return dWidth;
}


// EOF
