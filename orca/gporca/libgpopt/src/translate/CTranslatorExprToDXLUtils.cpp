//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CTranslatorExprToDXLUtils.cpp
//
//	@doc:
//		Implementation of the helper methods used during Expr to DXL translation
//		
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpopt/translate/CTranslatorExprToDXLUtils.h"

#include "naucrates/dxl/operators/dxlops.h"
#include "naucrates/dxl/operators/CDXLDatumInt4.h"
#include "naucrates/dxl/operators/CDXLDatumBool.h"
#include "naucrates/dxl/operators/CDXLDatumOid.h"
#include "naucrates/dxl/operators/CDXLDirectDispatchInfo.h"

#include "naucrates/md/IMDScalarOp.h"
#include "naucrates/md/IMDTypeOid.h"

#include "naucrates/statistics/IStatistics.h"

#include "gpopt/base/CConstraint.h"
#include "gpopt/base/CConstraintConjunction.h"
#include "gpopt/base/CConstraintDisjunction.h"
#include "gpopt/base/CConstraintNegation.h"
#include "gpopt/base/CConstraintInterval.h"

using namespace gpos;
using namespace gpmd;
using namespace gpdxl;
using namespace gpopt;
using namespace gpnaucrates;

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnInt4Const
//
//	@doc:
// 		Construct a scalar const value expression for the given INT value
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnInt4Const
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	INT iVal
	)
{
	GPOS_ASSERT(NULL != pmp);

	const IMDTypeInt4 *pmdtypeint4 = pmda->PtMDType<IMDTypeInt4>();
	pmdtypeint4->Pmdid()->AddRef();
	
	CDXLDatumInt4 *pdxldatum = GPOS_NEW(pmp) CDXLDatumInt4(pmp, pmdtypeint4->Pmdid(), false /*fNull*/, iVal);
	CDXLScalarConstValue *pdxlConst = GPOS_NEW(pmp) CDXLScalarConstValue(pmp, pdxldatum);
	
	return GPOS_NEW(pmp) CDXLNode(pmp, pdxlConst);
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnBoolConst
//
//	@doc:
// 		Construct a scalar const value expression for the given BOOL value
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnBoolConst
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	BOOL fVal
	)
{
	GPOS_ASSERT(NULL != pmp);

	const IMDTypeBool *pmdtype = pmda->PtMDType<IMDTypeBool>();
	pmdtype->Pmdid()->AddRef();
	
	CDXLDatumBool *pdxldatum = GPOS_NEW(pmp) CDXLDatumBool(pmp, pmdtype->Pmdid(), false /*fNull*/, fVal);
	CDXLScalarConstValue *pdxlConst = GPOS_NEW(pmp) CDXLScalarConstValue(pmp, pdxldatum);
	
	return GPOS_NEW(pmp) CDXLNode(pmp, pdxlConst);
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnPartialScanTest
//
//	@doc:
// 		Construct a test expression for the given part constraint
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnPartialScanTest
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda, 
	CColumnFactory *pcf,
	const CPartConstraint *ppartcnstr,
	DrgDrgPcr *pdrgpdrgpcrPartKeys
	)
{	
	DrgPdxln *pdrgpdxln = GPOS_NEW(pmp) DrgPdxln(pmp);

	const ULONG ulLevels = pdrgpdrgpcrPartKeys->UlLength();
	for (ULONG ul = 0; ul < ulLevels; ul++)
	{
		CConstraint *pcnstr = ppartcnstr->Pcnstr(ul);
		DrgDrgPcr *pdrgpdrgpcr = ppartcnstr->Pdrgpdrgpcr();
		CDXLNode *pdxlnPartialScanTest = PdxlnPartialScanTest(pmp, pmda, pcf, pcnstr, pdrgpdrgpcr);

		// check whether the scalar filter is of the form "where false"
		BOOL fScalarFalse = FScalarConstFalse(pmda, pdxlnPartialScanTest);
		if (!fScalarFalse)
		{
			// add (AND not defaultpart) to the previous condition
			CDXLNode *pdxlnNotDefault = GPOS_NEW(pmp) CDXLNode
											(
											pmp,
											GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxlnot),
											PdxlnDefaultPartitionTest(pmp, ul)
											);

			pdxlnPartialScanTest = GPOS_NEW(pmp) CDXLNode
											(
											pmp,
											GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxland),
											pdxlnNotDefault,
											pdxlnPartialScanTest
											);
		}

		if (ppartcnstr->FDefaultPartition(ul))
		{
			CDXLNode *pdxlnDefaultPartitionTest = PdxlnDefaultPartitionTest(pmp, ul);

			if (fScalarFalse)
			{
				pdxlnPartialScanTest->Release();
				pdxlnPartialScanTest = pdxlnDefaultPartitionTest;
			}
			else
			{
				pdxlnPartialScanTest = GPOS_NEW(pmp) CDXLNode
										(
										pmp,
										GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxlor),
										pdxlnPartialScanTest,
										pdxlnDefaultPartitionTest
										);
			}
		}

		pdrgpdxln->Append(pdxlnPartialScanTest);
	}

	if (1 == pdrgpdxln->UlLength())
	{
		CDXLNode *pdxln = (*pdrgpdxln)[0];
		pdxln->AddRef();
		pdrgpdxln->Release();
		return pdxln;
	}

	return GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxland), pdrgpdxln);
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnDefaultPartitionTest
//
//	@doc:
// 		Construct a test expression for default partitions
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnDefaultPartitionTest
	(
	IMemoryPool *pmp, 
	ULONG ulPartLevel
	)
{	
	CDXLNode *pdxlnDefaultPart = GPOS_NEW(pmp) CDXLNode
									(
									pmp,
									GPOS_NEW(pmp) CDXLScalarPartDefault(pmp, ulPartLevel)
									);

	return pdxlnDefaultPart;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnPartialScanTest
//
//	@doc:
// 		Construct a test expression for the given part constraint
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnPartialScanTest
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda, 
	CColumnFactory *pcf,
	CConstraint *pcnstr,
	DrgDrgPcr *pdrgpdrgpcrPartKeys
	)
{
	GPOS_ASSERT(NULL != pcnstr);
	
	if (pcnstr->FContradiction())
	{
		return PdxlnBoolConst(pmp, pmda, false /*fVal*/);
	}
	
	switch (pcnstr->Ect())
	{
		case CConstraint::EctConjunction:
			return PdxlnPartialScanTestConjunction(pmp, pmda, pcf, pcnstr, pdrgpdrgpcrPartKeys);
			
		case CConstraint::EctDisjunction:
			return PdxlnPartialScanTestDisjunction(pmp, pmda, pcf, pcnstr, pdrgpdrgpcrPartKeys);

		case CConstraint::EctNegation:
			return PdxlnPartialScanTestNegation(pmp, pmda, pcf, pcnstr, pdrgpdrgpcrPartKeys);

		case CConstraint::EctInterval:
			return PdxlnPartialScanTestInterval(pmp, pmda, pcnstr, pdrgpdrgpcrPartKeys);

		default:
			GPOS_RAISE
				(
				gpdxl::ExmaDXL,
				gpdxl::ExmiExpr2DXLUnsupportedFeature,
				GPOS_WSZ_LIT("Unrecognized constraint type")
				);
			return NULL;
	}
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnPartialScanTestConjDisj
//
//	@doc:
// 		Construct a test expression for the given conjunction or disjunction 
//		based part constraint
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnPartialScanTestConjDisj
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda, 
	CColumnFactory *pcf,
	DrgPcnstr *pdrgpcnstr,
	BOOL fConjunction,
	DrgDrgPcr *pdrgpdrgpcrPartKeys
	)
{	
	GPOS_ASSERT(NULL != pdrgpcnstr);
	
	const ULONG ulLength = pdrgpcnstr->UlLength();
	
	if (1 == ulLength)
	{
		return PdxlnPartialScanTest(pmp, pmda, pcf, (*pdrgpcnstr)[0], pdrgpdrgpcrPartKeys);
	}
	
	EdxlBoolExprType edxlbooltype = Edxlor;
	
	if (fConjunction)
	{
		edxlbooltype = Edxland;
	}
	
	CDXLNode *pdxlnResult = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, edxlbooltype));
	
	for (ULONG ul = 0; ul < ulLength; ul++)
	{
		CConstraint *pcnstr = (*pdrgpcnstr)[ul];
		CDXLNode *pdxln = PdxlnPartialScanTest(pmp, pmda, pcf, pcnstr, pdrgpdrgpcrPartKeys);
		pdxlnResult->AddChild(pdxln);
	}
	
	return pdxlnResult;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnPropagationExpressionForPartConstraints
//
//	@doc:
// 		Construct a nested if statement testing the constraints in the 
//		given part index map and propagating to the right part index id
//		
//		For example for the following part constraint map:
//		1->[1,3), 2->[3,5), 3->![1,5), the generated if expr will be:
//		If (min,max,minincl,maxincl) \subseteq [1,3)
//		Then 1
//		Else If (min,max,minincl,maxincl) \subseteq [3,5)
//		     Then 2
//		     Else If (min,max,minincl,maxincl) \subseteq Not([1,5))
//		          Then 3
//		          Else NULL
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnPropagationExpressionForPartConstraints
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda, 
	CColumnFactory *pcf,
	PartCnstrMap *ppartcnstrmap,
	DrgDrgPcr *pdrgpdrgpcrPartKeys
	)
{	
	PartCnstrMapIter pcmi(ppartcnstrmap);
		
	CDXLNode *pdxlnScalarRootIfStmt = NULL;
	CDXLNode *pdxlnScalarLeafIfStmt = NULL;
	
	const IMDTypeInt4 *pmdtypeint4 = pmda->PtMDType<IMDTypeInt4>();
	IMDId *pmdidRetType = pmdtypeint4->Pmdid();

	while (pcmi.FAdvance())
	{
		ULONG ulSecondaryScanId = *(pcmi.Pk());
		const CPartConstraint *ppartcnstr = pcmi.Pt();
		CDXLNode *pdxlnTest = PdxlnPartialScanTest
									(
									pmp, 
									pmda, 
									pcf,
									ppartcnstr,
									pdrgpdrgpcrPartKeys
									);
		
		CDXLNode *pdxlnPropagate = PdxlnInt4Const(pmp, pmda, (INT) ulSecondaryScanId);
		
		pmdidRetType->AddRef();
		CDXLNode *pdxlnScalarIf = GPOS_NEW(pmp) CDXLNode
										(
										pmp, 
										GPOS_NEW(pmp) CDXLScalarIfStmt(pmp, pmdidRetType),
										pdxlnTest, 
										pdxlnPropagate
										);
		
		if (NULL == pdxlnScalarRootIfStmt)
		{
			pdxlnScalarRootIfStmt = pdxlnScalarIf;
		}
		else
		{
			// add nested if statement to the latest leaf if statement as the else case of the already constructed if stmt
			GPOS_ASSERT(NULL != pdxlnScalarLeafIfStmt && 2 == pdxlnScalarLeafIfStmt->UlArity());
			pdxlnScalarLeafIfStmt->AddChild(pdxlnScalarIf);
		}
		
		pdxlnScalarLeafIfStmt = pdxlnScalarIf;
	}
	
	GPOS_ASSERT(2 == pdxlnScalarLeafIfStmt->UlArity());
	
	// add a dummy value for the top and bottom level else cases
	const IMDType *pmdtypeVoid = pmda->Pmdtype(pmdidRetType);
	CDXLDatum *pdxldatum = pmdtypeVoid->PdxldatumNull(pmp);
	CDXLNode *pdxlnNullConst = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarConstValue(pmp, pdxldatum));
	pdxlnScalarLeafIfStmt->AddChild(pdxlnNullConst);
	
	if (2 == pdxlnScalarRootIfStmt->UlArity())
	{
		pdxlnNullConst->AddRef();
		pdxlnScalarRootIfStmt->AddChild(pdxlnNullConst);
	}
	
	return pdxlnScalarRootIfStmt;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnPartialScanTestConjunction
//
//	@doc:
// 		Construct a test expression for the given conjunction  
//		based part constraint
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnPartialScanTestConjunction
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda, 
	CColumnFactory *pcf,
	CConstraint *pcnstr,
	DrgDrgPcr *pdrgpdrgpcrPartKeys
	)
{	
	GPOS_ASSERT(CConstraint::EctConjunction == pcnstr->Ect());
	
	CConstraintConjunction *pcnstrConj = dynamic_cast<CConstraintConjunction *>(pcnstr);
	
	DrgPcnstr *pdrgpcnstr = pcnstrConj->Pdrgpcnstr();
	return PdxlnPartialScanTestConjDisj(pmp, pmda, pcf, pdrgpcnstr, true /*fConjunction*/, pdrgpdrgpcrPartKeys);
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnPartialScanTestDisjunction
//
//	@doc:
// 		Construct a test expression for the given disjunction  
//		based part constraint
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnPartialScanTestDisjunction
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda, 
	CColumnFactory *pcf,
	CConstraint *pcnstr,
	DrgDrgPcr *pdrgpdrgpcrPartKeys
	)
{	
	GPOS_ASSERT(CConstraint::EctDisjunction == pcnstr->Ect());
	
	CConstraintDisjunction *pcnstrDisj = dynamic_cast<CConstraintDisjunction *>(pcnstr);
	
	DrgPcnstr *pdrgpcnstr = pcnstrDisj->Pdrgpcnstr();
	return PdxlnPartialScanTestConjDisj(pmp, pmda, pcf, pdrgpcnstr, false /*fConjunction*/, pdrgpdrgpcrPartKeys);
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnPartialScanTestNegation
//
//	@doc:
// 		Construct a test expression for the given negation  
//		based part constraint
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnPartialScanTestNegation
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda, 
	CColumnFactory *pcf,
	CConstraint *pcnstr,
	DrgDrgPcr *pdrgpdrgpcrPartKeys
	)
{	
	GPOS_ASSERT(CConstraint::EctNegation == pcnstr->Ect());
	
	CConstraintNegation *pcnstrNeg = dynamic_cast<CConstraintNegation *>(pcnstr);
	
	CConstraint *pcnstrChild = pcnstrNeg->PcnstrChild();

	CDXLNode *pdxlnChild = PdxlnPartialScanTest(pmp, pmda, pcf, pcnstrChild, pdrgpdrgpcrPartKeys);
	
	return GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxlnot), pdxlnChild);
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnPartialScanTestInterval
//
//	@doc:
// 		Construct a test expression for the given interval  
//		based part constraint
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnPartialScanTestInterval
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda, 
	CConstraint *pcnstr,
	DrgDrgPcr *pdrgpdrgpcrPartKeys
	)
{	
	GPOS_ASSERT(CConstraint::EctInterval == pcnstr->Ect());
	
	CConstraintInterval *pcnstrInterval = dynamic_cast<CConstraintInterval *>(pcnstr);
	
	const CColRef *pcrPartKey = pcnstrInterval->Pcr();
	IMDId *pmdidPartKeyType = pcrPartKey->Pmdtype()->Pmdid();
	ULONG ulPartLevel = UlPartKeyLevel(pcrPartKey, pdrgpdrgpcrPartKeys);

	DrgPrng *pdrgprng = pcnstrInterval->Pdrgprng();
	const ULONG ulRanges = pdrgprng->UlLength();
	 
	GPOS_ASSERT(0 < ulRanges);
	
	if (1 == ulRanges)
	{
		return PdxlnPartialScanTestRange(pmp, pmda, (*pdrgprng)[0], pmdidPartKeyType, ulPartLevel);
	}
	
	CDXLNode *pdxlnDisjunction = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxlor));
	
	for (ULONG ul = 0; ul < ulRanges; ul++)
	{
		CRange *prng = (*pdrgprng)[ul];
		CDXLNode *pdxlnChild = PdxlnPartialScanTestRange(pmp, pmda, prng, pmdidPartKeyType, ulPartLevel);
		pdxlnDisjunction->AddChild(pdxlnChild);
	}
	
	return pdxlnDisjunction;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::UlPartKeyLevel
//
//	@doc:
// 		Find the partitioning level of the given part key, given the whole
//		array of part keys
//
//---------------------------------------------------------------------------
ULONG
CTranslatorExprToDXLUtils::UlPartKeyLevel
	(
	const CColRef *pcr,
	DrgDrgPcr *pdrgpdrgpcr
	)
{
	GPOS_ASSERT(0 < pdrgpdrgpcr->UlLength() && "No partitioning keys found");

	const ULONG ulLength = pdrgpdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulLength; ul++)
	{
		if (CUtils::PcrExtractPartKey(pdrgpdrgpcr, ul) == pcr)
		{
			return ul;
		}
	}

	GPOS_ASSERT(!"Could not find partitioning key");
	return 0;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnPartialScanTestRange
//
//	@doc:
// 		Construct a test expression for the given range  
//		based part constraint
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnPartialScanTestRange
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda, 
	CRange *prng,
	IMDId *pmdidPartKeyType,
	ULONG ulPartLevel
	)
{	
	CDXLNode *pdxlnStart = PdxlnRangeStartPredicate(pmp, pmda, prng->PdatumLeft(), prng->EriLeft(), pmdidPartKeyType, ulPartLevel);
	CDXLNode *pdxlnEnd = PdxlnRangeEndPredicate(pmp, pmda, prng->PdatumRight(), prng->EriRight(), pmdidPartKeyType, ulPartLevel);

	return GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxland), pdxlnStart, pdxlnEnd);
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnRangeStartPredicate
//
//	@doc:
// 		Construct a test expression for the given range start point
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnRangeStartPredicate
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda, 
	IDatum *pdatum,
	CRange::ERangeInclusion eri,
	IMDId *pmdidPartKeyType,
	ULONG ulPartLevel
	)
{	
	const IMDType *pmdtype = pmda->Pmdtype(pmdidPartKeyType);
	
	return PdxlnRangePointPredicate
			(
			pmp, 
			pmda, 
			pdatum,
			eri, 
			pmdidPartKeyType, 
			pmdtype->PmdidCmp(IMDType::EcmptL), 	// pmdidCmpExl
			pmdtype->PmdidCmp(IMDType::EcmptLEq), 	// pmdidCmpIncl
			ulPartLevel,
			true /*fLower*/
			);
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnRangeEndPredicate
//
//	@doc:
// 		Construct a test expression for the given range end point
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnRangeEndPredicate
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda, 
	IDatum *pdatum,
	CRange::ERangeInclusion eri,
	IMDId *pmdidPartKeyType,
	ULONG ulPartLevel
	)
{	
	const IMDType *pmdtype = pmda->Pmdtype(pmdidPartKeyType);
	
	return PdxlnRangePointPredicate
			(
			pmp, 
			pmda, 
			pdatum,
			eri, 
			pmdidPartKeyType, 
			pmdtype->PmdidCmp(IMDType::EcmptG), 	// pmdidCmpExl
			pmdtype->PmdidCmp(IMDType::EcmptGEq), 	// pmdidCmpIncl
			ulPartLevel,
			false /*fLower*/
			);
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnRangePointPredicate
//
//	@doc:
// 		Construct a test expression for the given range point using the 
//		provided comparison operators
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnRangePointPredicate
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda, 
	IDatum *pdatum,
	CRange::ERangeInclusion eri,
	IMDId *pmdidPartKeyType,
	IMDId *pmdidCmpExl,
	IMDId *pmdidCmpIncl,
	ULONG ulPartLevel,
	BOOL fLower
	)
{	
	if (NULL == pdatum)
	{
		// point in an unbounded range: create a predicate (open-ended)
		return GPOS_NEW(pmp) CDXLNode
					(
					pmp,
					GPOS_NEW(pmp) CDXLScalarPartBoundOpen(pmp, ulPartLevel, fLower)
					);
	}
	
	pmdidPartKeyType->AddRef();
	CDXLNode *pdxlnPartBound = GPOS_NEW(pmp) CDXLNode
										(
										pmp,
										GPOS_NEW(pmp) CDXLScalarPartBound(pmp, ulPartLevel, pmdidPartKeyType, fLower)
										);

	CDXLDatum *pdxldatum = Pdxldatum(pmp, pmda, pdatum);
	CDXLNode *pdxlnPoint = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarConstValue(pmp, pdxldatum));
	 	
	// generate a predicate of the form "point < col" / "point > col"
	pmdidCmpExl->AddRef();
	
	CWStringConst *pstrCmpExcl = GPOS_NEW(pmp) CWStringConst(pmp, pmda->Pmdscop(pmdidCmpExl)->Mdname().Pstr()->Wsz());
	CDXLNode *pdxlnPredicateExclusive = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarComp(pmp, pmdidCmpExl, pstrCmpExcl), pdxlnPoint, pdxlnPartBound);
	
	// generate a predicate of the form "point <= col and colIncluded" / "point >= col and colIncluded"
	pmdidCmpIncl->AddRef();

	CWStringConst *pstrCmpIncl = GPOS_NEW(pmp) CWStringConst(pmp, pmda->Pmdscop(pmdidCmpIncl)->Mdname().Pstr()->Wsz());
	pdxlnPartBound->AddRef();
	pdxlnPoint->AddRef();
	CDXLNode *pdxlnCmpIncl = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarComp(pmp, pmdidCmpIncl, pstrCmpIncl), pdxlnPoint, pdxlnPartBound);

	CDXLNode *pdxlnPartBoundInclusion = GPOS_NEW(pmp) CDXLNode
										(
										pmp,
										GPOS_NEW(pmp) CDXLScalarPartBoundInclusion(pmp, ulPartLevel, fLower)
										);

	if (CRange::EriExcluded == eri)
	{
		// negate the "inclusion" portion of the predicate
		pdxlnPartBoundInclusion = GPOS_NEW(pmp) CDXLNode
											(
											pmp,
											GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxlnot),
											pdxlnPartBoundInclusion
											);
	}

	CDXLNode *pdxlnPredicateInclusive = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxland), pdxlnCmpIncl, pdxlnPartBoundInclusion);

	// return the final predicate in the form "(point <= col and colInclusive) or point < col" / "(point >= col and colInclusive) or point > col"
	return GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxlor), pdxlnPredicateInclusive, pdxlnPredicateExclusive);
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnRangeFilterScCmp
//
//	@doc:
// 		Construct a Result node for a filter min <= Scalar or max >= Scalar
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnRangeFilterScCmp
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda, 
	CDXLNode *pdxlnScalar,
	IMDId *pmdidTypePartKey,
	IMDId *pmdidTypeOther,
	IMDId *pmdidTypeCastExpr,
	IMDId *pmdidCastFunc,
	IMDType::ECmpType ecmpt,
	ULONG ulPartLevel
	)
{
	if (IMDType::EcmptEq == ecmpt)
	{
		return PdxlnRangeFilterEqCmp
				(
				pmp, 
				pmda, 
				pdxlnScalar, 
				pmdidTypePartKey, 
				pmdidTypeOther,
				pmdidTypeCastExpr,
				pmdidCastFunc,
				ulPartLevel
				);
	}
	
	BOOL fLowerBound = false;
	IMDType::ECmpType ecmptScCmp = IMDType::EcmptOther;
	
	if (IMDType::EcmptLEq == ecmpt || IMDType::EcmptL == ecmpt)
	{
		// partkey </<= other: construct condition min < other
		fLowerBound = true;
		ecmptScCmp = IMDType::EcmptL;
	}
	else 
	{
		GPOS_ASSERT(IMDType::EcmptGEq == ecmpt || IMDType::EcmptG == ecmpt);
		
		// partkey >/>= other: construct condition max > other
		ecmptScCmp = IMDType::EcmptG;
	}
	
	CDXLNode *pdxlnPredicateExclusive = PdxlnCmp(pmp, pmda, ulPartLevel, fLowerBound, pdxlnScalar, ecmptScCmp, pmdidTypePartKey, pmdidTypeOther, pmdidTypeCastExpr, pmdidCastFunc);
	
	if (IMDType::EcmptLEq != ecmpt && IMDType::EcmptGEq != ecmpt)
	{
		// scalar comparison does not include equality: no need to consider part constraint boundaries
		return pdxlnPredicateExclusive;
	}
	
	pdxlnScalar->AddRef();
	CDXLNode *pdxlnInclusiveCmp = PdxlnCmp(pmp, pmda, ulPartLevel, fLowerBound, pdxlnScalar, ecmpt, pmdidTypePartKey, pmdidTypeOther, pmdidTypeCastExpr, pmdidCastFunc);
	CDXLNode *pdxlnInclusiveBoolPredicate = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarPartBoundInclusion(pmp, ulPartLevel, fLowerBound));

	CDXLNode *pdxlnPredicateInclusive = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxland), pdxlnInclusiveCmp, pdxlnInclusiveBoolPredicate);
	
	// return the final predicate in the form "(point <= col and colIncluded) or point < col" / "(point >= col and colIncluded) or point > col"
	return GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxlor), pdxlnPredicateInclusive, pdxlnPredicateExclusive);
}
 
//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnRangeFilterEqCmp
//
//	@doc:
// 		Construct a predicate node for a filter min <= Scalar and max >= Scalar
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnRangeFilterEqCmp
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda, 
	CDXLNode *pdxlnScalar,
	IMDId *pmdidTypePartKey,
	IMDId *pmdidTypeOther,
	IMDId *pmdidTypeCastExpr,
	IMDId *pmdidCastFunc,
	ULONG ulPartLevel
	)
{
	CDXLNode *pdxlnPredicateMin = PdxlnRangeFilterPartBound(pmp, pmda, pdxlnScalar, pmdidTypePartKey, pmdidTypeOther, pmdidTypeCastExpr, pmdidCastFunc, ulPartLevel, true /*fLowerBound*/, IMDType::EcmptL);
	pdxlnScalar->AddRef();
	CDXLNode *pdxlnPredicateMax = PdxlnRangeFilterPartBound(pmp, pmda, pdxlnScalar, pmdidTypePartKey, pmdidTypeOther, pmdidTypeCastExpr, pmdidCastFunc, ulPartLevel, false /*fLowerBound*/, IMDType::EcmptG);
		
	// return the conjunction of the predicate for the lower and upper bounds
	return GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxland), pdxlnPredicateMin, pdxlnPredicateMax);
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnRangeFilterPartBound
//
//	@doc:
// 		Construct a predicate for a partition bound of one of the two forms 
//		(min <= Scalar and minincl) or min < Scalar
//		(max >= Scalar and maxinc) or Max > Scalar
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnRangeFilterPartBound
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda, 
	CDXLNode *pdxlnScalar,
	IMDId *pmdidTypePartKey,
	IMDId *pmdidTypeOther,
	IMDId *pmdidTypeCastExpr,
	IMDId *pmdidCastFunc,
	ULONG ulPartLevel,
	ULONG fLowerBound,
	IMDType::ECmpType ecmpt
	)
{
	GPOS_ASSERT(IMDType::EcmptL == ecmpt || IMDType::EcmptG == ecmpt);
	
	IMDType::ECmpType ecmptInc = IMDType::EcmptLEq;
	if (IMDType::EcmptG == ecmpt)
	{
		ecmptInc = IMDType::EcmptGEq;
	}

	CDXLNode *pdxlnPredicateExclusive = PdxlnCmp(pmp, pmda, ulPartLevel, fLowerBound, pdxlnScalar, ecmpt, pmdidTypePartKey, pmdidTypeOther, pmdidTypeCastExpr, pmdidCastFunc);

	pdxlnScalar->AddRef();
	CDXLNode *pdxlnInclusiveCmp = PdxlnCmp(pmp, pmda, ulPartLevel, fLowerBound, pdxlnScalar, ecmptInc, pmdidTypePartKey, pmdidTypeOther, pmdidTypeCastExpr, pmdidCastFunc);
	
	CDXLNode *pdxlnInclusiveBoolPredicate = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarPartBoundInclusion(pmp, ulPartLevel, fLowerBound));
	
	CDXLNode *pdxlnPredicateInclusive = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxland), pdxlnInclusiveCmp, pdxlnInclusiveBoolPredicate);
	
	// return the final predicate in the form "(point <= col and colIncluded) or point < col" / "(point >= col and colIncluded) or point > col"
	return GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxlor), pdxlnPredicateInclusive, pdxlnPredicateExclusive);
}


//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnRangeFilterDefaultAndOpenEnded
//
//	@doc:
//		Construct predicates to cover the cases of default partition and
//		open-ended partitions if necessary
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnRangeFilterDefaultAndOpenEnded
	(
	IMemoryPool *pmp, 
	ULONG ulPartLevel,
	BOOL fLTComparison,
	BOOL fGTComparison,
	BOOL fEQComparison,
	BOOL fDefaultPart
	)
{
	CDXLNode *pdxlnResult = NULL;
	if (fLTComparison || fEQComparison)
	{
		// add a condition to cover the cases of open-ended interval (-inf, x)
		pdxlnResult = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarPartBoundOpen(pmp, ulPartLevel, true /*fLower*/));
	}
	
	if (fGTComparison || fEQComparison)
	{
		// add a condition to cover the cases of open-ended interval (x, inf)
		CDXLNode *pdxlnOpenMax = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarPartBoundOpen(pmp, ulPartLevel, false /*fLower*/));

		// construct a boolean OR expression over the two expressions
		if (NULL != pdxlnResult)
		{
			pdxlnResult = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxlor), pdxlnResult, pdxlnOpenMax);
		}
		else
		{
			pdxlnResult = pdxlnOpenMax;
		}
	}

	if (fDefaultPart)
	{
		// add a condition to cover the cases of default partition
		CDXLNode *pdxlnDefault = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarPartDefault(pmp, ulPartLevel));

		if (NULL != pdxlnResult)
		{
			pdxlnResult = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, Edxlor), pdxlnDefault, pdxlnResult);
		}
		else
		{
			pdxlnResult = pdxlnDefault;
		}

	}
	
	return pdxlnResult;
}


//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlpropCopy
//
//	@doc:
//		Return a copy the dxl node's physical properties
//
//---------------------------------------------------------------------------
CDXLPhysicalProperties *
CTranslatorExprToDXLUtils::PdxlpropCopy
	(
	IMemoryPool *pmp,
	CDXLNode *pdxln
	)
{
	GPOS_ASSERT(NULL != pdxln);

	GPOS_ASSERT(NULL != pdxln->Pdxlprop());
	CDXLPhysicalProperties *pdxlprop = CDXLPhysicalProperties::PdxlpropConvert(pdxln->Pdxlprop());

	CWStringDynamic *pstrStartupcost = GPOS_NEW(pmp) CWStringDynamic(pmp, pdxlprop->Pdxlopcost()->PstrStartupCost()->Wsz());
	CWStringDynamic *pstrCost = GPOS_NEW(pmp) CWStringDynamic(pmp, pdxlprop->Pdxlopcost()->PstrTotalCost()->Wsz());
	CWStringDynamic *pstrRows = GPOS_NEW(pmp) CWStringDynamic(pmp, pdxlprop->Pdxlopcost()->PstrRows()->Wsz());
	CWStringDynamic *pstrWidth = GPOS_NEW(pmp) CWStringDynamic(pmp, pdxlprop->Pdxlopcost()->PstrWidth()->Wsz());

	return GPOS_NEW(pmp) CDXLPhysicalProperties(GPOS_NEW(pmp) CDXLOperatorCost(pstrStartupcost, pstrCost, pstrRows, pstrWidth));
}


//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnCmp
//
//	@doc:
//		Construct a scalar comparison of the given type between the column with
//		the given col id and the scalar expression
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnCmp
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda,
	ULONG ulPartLevel,
	BOOL fLowerBound,
	CDXLNode *pdxlnScalar, 
	IMDType::ECmpType ecmpt, 
	IMDId *pmdidTypePartKey,
	IMDId *pmdidTypeExpr,
	IMDId *pmdidTypeCastExpr,
	IMDId *pmdidCastFunc
	)
{
	IMDId *pmdidScCmp = NULL;
	
	if (IMDId::FValid(pmdidTypeCastExpr))
	{
		pmdidScCmp = CUtils::PmdidScCmp(pmda, pmdidTypeCastExpr, pmdidTypeExpr, ecmpt);
	}
	else
	{
		pmdidScCmp = CUtils::PmdidScCmp(pmda, pmdidTypePartKey, pmdidTypeExpr, ecmpt);
	}
	
	const IMDScalarOp *pmdscop = pmda->Pmdscop(pmdidScCmp); 
	const CWStringConst *pstrScCmp = pmdscop->Mdname().Pstr();
	
	pmdidScCmp->AddRef();
	
	CDXLScalarComp *pdxlopCmp = GPOS_NEW(pmp) CDXLScalarComp(pmp, pmdidScCmp, GPOS_NEW(pmp) CWStringConst(pmp, pstrScCmp->Wsz()));
	CDXLNode *pdxlnScCmp = GPOS_NEW(pmp) CDXLNode(pmp, pdxlopCmp);
	
	pmdidTypePartKey->AddRef();
	CDXLNode *pdxlnPartBound = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarPartBound(pmp, ulPartLevel, pmdidTypePartKey, fLowerBound));
	
	if (IMDId::FValid(pmdidTypeCastExpr))
	{
		GPOS_ASSERT(NULL != pmdidCastFunc);
		pmdidTypeCastExpr->AddRef();
		pmdidCastFunc->AddRef();

		pdxlnPartBound = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarCast(pmp, pmdidTypeCastExpr, pmdidCastFunc), pdxlnPartBound);
	}
	pdxlnScCmp->AddChild(pdxlnPartBound);
	pdxlnScCmp->AddChild(pdxlnScalar);
	
	return pdxlnScCmp;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PcrCreate
//
//	@doc:
//		Construct a column reference with the given name and type
//
//---------------------------------------------------------------------------
CColRef *
CTranslatorExprToDXLUtils::PcrCreate
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	CColumnFactory *pcf,
	IMDId *pmdidType,
	const WCHAR *wszName
	)
{
	const IMDType *pmdtype = pmda->Pmdtype(pmdidType);
	
	CName *pname = GPOS_NEW(pmp) CName(GPOS_NEW(pmp) CWStringConst(wszName), true /*fOwnsMemory*/);
	CColRef *pcr = pcf->PcrCreate(pmdtype, *pname);
	GPOS_DELETE(pname);
	return pcr;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::Pdxlprop
//
//	@doc:
//		Construct a DXL physical properties container with operator costs for
//		the given expression
//
//---------------------------------------------------------------------------
CDXLPhysicalProperties *
CTranslatorExprToDXLUtils::Pdxlprop
	(
	IMemoryPool *pmp
	)
{
	// TODO:  - May 10, 2012; replace the dummy implementation with a real one
	CWStringDynamic *pstrStartupcost = GPOS_NEW(pmp) CWStringDynamic(pmp, GPOS_WSZ_LIT("10"));
	CWStringDynamic *pstrTotalcost = GPOS_NEW(pmp) CWStringDynamic(pmp, GPOS_WSZ_LIT("100"));
	CWStringDynamic *pstrRows = GPOS_NEW(pmp) CWStringDynamic(pmp, GPOS_WSZ_LIT("100"));
	CWStringDynamic *pstrWidth = GPOS_NEW(pmp) CWStringDynamic(pmp, GPOS_WSZ_LIT("4"));

	CDXLOperatorCost *pdxlopcost = GPOS_NEW(pmp) CDXLOperatorCost(pstrStartupcost, pstrTotalcost, pstrRows, pstrWidth);
	return GPOS_NEW(pmp) CDXLPhysicalProperties(pdxlopcost);
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::FScalarConstTrue
//
//	@doc:
//		Checks to see if the DXL Node is a scalar const TRUE
//
//---------------------------------------------------------------------------
BOOL
CTranslatorExprToDXLUtils::FScalarConstTrue
	(
	CMDAccessor *pmda,
	CDXLNode *pdxln
	)
{
	GPOS_ASSERT(NULL != pdxln);
	if (EdxlopScalarConstValue == pdxln->Pdxlop()->Edxlop())
	{
		CDXLScalarConstValue *pdxlopConst =
				CDXLScalarConstValue::PdxlopConvert(pdxln->Pdxlop());

		const IMDType *pmdtype = pmda->Pmdtype(pdxlopConst->Pdxldatum()->Pmdid());
		if (IMDType::EtiBool ==  pmdtype->Eti())
		{
			CDXLDatumBool *pdxldatum = CDXLDatumBool::PdxldatumConvert(const_cast<CDXLDatum*>(pdxlopConst->Pdxldatum()));

			return (!pdxldatum->FNull() && pdxldatum->FValue());
		}
	}

	return false;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::FScalarConstFalse
//
//	@doc:
//		Checks to see if the DXL Node is a scalar const FALSE
//
//---------------------------------------------------------------------------
BOOL
CTranslatorExprToDXLUtils::FScalarConstFalse
	(
	CMDAccessor *pmda,
	CDXLNode *pdxln
	)
{
	GPOS_ASSERT(NULL != pdxln);
	if (EdxlopScalarConstValue == pdxln->Pdxlop()->Edxlop())
	{
		CDXLScalarConstValue *pdxlopConst =
				CDXLScalarConstValue::PdxlopConvert(pdxln->Pdxlop());

		const IMDType *pmdtype = pmda->Pmdtype(pdxlopConst->Pdxldatum()->Pmdid());
		if (IMDType::EtiBool ==  pmdtype->Eti())
		{
			CDXLDatumBool *pdxldatum = CDXLDatumBool::PdxldatumConvert(const_cast<CDXLDatum*>(pdxlopConst->Pdxldatum()));
			return (!pdxldatum->FNull() && !pdxldatum->FValue());
		}
	}

	return false;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnProjListFromChildProjList
//
//	@doc:
//		Construct a project list node by creating references to the columns
//		of the given project list of the child node 
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnProjListFromChildProjList
	(
	IMemoryPool *pmp,
	CColumnFactory *pcf,
	HMCrDxln *phmcrdxln, 
	const CDXLNode *pdxlnProjListChild
	)
{
	GPOS_ASSERT(NULL != pdxlnProjListChild);
	
	CDXLScalarProjList *pdxlopPrL = GPOS_NEW(pmp) CDXLScalarProjList(pmp);
	CDXLNode *pdxlnProjList = GPOS_NEW(pmp) CDXLNode(pmp, pdxlopPrL);
	
	// create a scalar identifier for each project element of the child
	const ULONG ulArity = pdxlnProjListChild->UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		CDXLNode *pdxlnProjElemChild = (*pdxlnProjListChild)[ul];
		
		// translate proj elem
		CDXLNode *pdxlnProjElem = PdxlnProjElem(pmp, pcf, phmcrdxln, pdxlnProjElemChild);
		pdxlnProjList->AddChild(pdxlnProjElem);
	}
		
	return pdxlnProjList;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnPrLPartitionSelector
//
//	@doc:
//		Construct the project list of a partition selector
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnPrLPartitionSelector
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	CColumnFactory *pcf,
	HMCrDxln *phmcrdxln,
	BOOL fUseChildProjList,
	CDXLNode *pdxlnPrLChild,
	CColRef *pcrOid,
	ULONG ulPartLevels
	)
{
	GPOS_ASSERT_IMP(fUseChildProjList, NULL != pdxlnPrLChild);

	CDXLNode *pdxlnPrL = NULL;
	if (fUseChildProjList)
	{
		pdxlnPrL = PdxlnProjListFromChildProjList(pmp, pcf, phmcrdxln, pdxlnPrLChild);
	}
	else
	{
		pdxlnPrL = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarProjList(pmp));
	}

	// add to it the Oid column
	if (NULL == pcrOid)
	{
		const IMDTypeOid *pmdtype = pmda->PtMDType<IMDTypeOid>();
		pcrOid = pcf->PcrCreate(pmdtype);
	}

	CMDName *pmdname = GPOS_NEW(pmp) CMDName(pmp, pcrOid->Name().Pstr());
	CDXLScalarProjElem *pdxlopPrEl = GPOS_NEW(pmp) CDXLScalarProjElem(pmp, pcrOid->UlId(), pmdname);
	CDXLNode *pdxlnPrEl = GPOS_NEW(pmp) CDXLNode(pmp, pdxlopPrEl);
	CDXLNode *pdxlnPartOid = GPOS_NEW(pmp) CDXLNode(pmp, GPOS_NEW(pmp) CDXLScalarPartOid(pmp, ulPartLevels-1));
	pdxlnPrEl->AddChild(pdxlnPartOid);
	pdxlnPrL->AddChild(pdxlnPrEl);

	return pdxlnPrL;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnPropExprPartitionSelector
//
//	@doc:
//		Construct the propagation expression for a partition selector
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnPropExprPartitionSelector
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	CColumnFactory *pcf,
	BOOL fConditional,
	PartCnstrMap *ppartcnstrmap,
	DrgDrgPcr *pdrgpdrgpcrKeys,
	ULONG ulScanId
	)
{
	if (!fConditional)
	{
		// unconditional propagation
		return PdxlnInt4Const(pmp, pmda, (INT) ulScanId);
	}

	return PdxlnPropagationExpressionForPartConstraints(pmp, pmda, pcf, ppartcnstrmap, pdrgpdrgpcrKeys);
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnProjElem
//
//	@doc:
//		Create a project elem as a scalar identifier for the given child 
//		project element
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnProjElem
	(
	IMemoryPool *pmp,
	CColumnFactory *pcf,
	HMCrDxln *phmcrdxln, 
	const CDXLNode *pdxlnChildProjElem
	)
{
	GPOS_ASSERT(NULL != pdxlnChildProjElem && 1 == pdxlnChildProjElem->UlArity());
	
	CDXLScalarProjElem *pdxlopPrElChild = dynamic_cast<CDXLScalarProjElem*>(pdxlnChildProjElem->Pdxlop());

    // find the col ref corresponding to this element's id through column factory
    CColRef *pcr = pcf->PcrLookup(pdxlopPrElChild->UlId());
    if (NULL == pcr)
    {
    	GPOS_RAISE(gpdxl::ExmaDXL, gpdxl::ExmiExpr2DXLAttributeNotFound, pdxlopPrElChild->UlId());
    }
    
    CDXLNode *pdxlnProjElemResult = PdxlnProjElem(pmp, phmcrdxln, pcr);
	
	return pdxlnProjElemResult;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::ReplaceSubplan
//
//	@doc:
//		 Replace subplan entry in the given map with a dxl column ref
//
//---------------------------------------------------------------------------
void
CTranslatorExprToDXLUtils::ReplaceSubplan
	(
	IMemoryPool *pmp,
	HMCrDxln *phmcrdxlnSubplans,  // map of col ref to subplan
	const CColRef *pcr, // key of entry in the passed map
	CDXLScalarProjElem *pdxlopPrEl // project element to use for creating DXL col ref to replace subplan
	)
{
	GPOS_ASSERT(NULL != phmcrdxlnSubplans);
	GPOS_ASSERT(NULL != pcr);
	GPOS_ASSERT(pdxlopPrEl->UlId() == pcr->UlId());

	IMDId *pmdidType = pcr->Pmdtype()->Pmdid();
	pmdidType->AddRef();
	CMDName *pmdname = GPOS_NEW(pmp) CMDName(pmp, pdxlopPrEl->PmdnameAlias()->Pstr());
	CDXLColRef *pdxlcr = GPOS_NEW(pmp) CDXLColRef(pmp, pmdname, pdxlopPrEl->UlId());
	CDXLScalarIdent *pdxlnScId = GPOS_NEW(pmp) CDXLScalarIdent(pmp, pdxlcr, pmdidType);
	CDXLNode *pdxln = GPOS_NEW(pmp) CDXLNode(pmp, pdxlnScId);
#ifdef GPOS_DEBUG
	BOOL fReplaced =
#endif // GPOS_DEBUG
		phmcrdxlnSubplans->FReplace(pcr, pdxln);
	GPOS_ASSERT(fReplaced);
}


//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnProjElem
//
//	@doc:
//		Create a project elem from a given col ref,
//
//		if the given col has a corresponding subplan entry in the given map,
//		the function returns a project element with a child subplan,
//		the function then replaces the subplan entry in the given map with the
//		projected column reference, so that all subplan references higher up in
//		the DXL tree use the projected col ref
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnProjElem
	(
	IMemoryPool *pmp,
	HMCrDxln *phmcrdxlnSubplans, // map of col ref -> subplan: can be modified by this function
	const CColRef *pcr
	)
{
	GPOS_ASSERT(NULL != pcr);
	
	CMDName *pmdname = GPOS_NEW(pmp) CMDName(pmp, pcr->Name().Pstr());
	
	CDXLScalarProjElem *pdxlopPrEl = GPOS_NEW(pmp) CDXLScalarProjElem(pmp, pcr->UlId(), pmdname);
	CDXLNode *pdxlnPrEl = GPOS_NEW(pmp) CDXLNode(pmp, pdxlopPrEl);
	
	// create a scalar identifier for the proj element expression
	CDXLNode *pdxlnScId = PdxlnIdent(pmp, phmcrdxlnSubplans, NULL /*phmcrdxlnIndexLookup*/, pcr);

	if (EdxlopScalarSubPlan == pdxlnScId->Pdxlop()->Edxlop())
	{
		// modify map by replacing subplan entry with the projected
		// column reference so that all subplan references higher up
		// in the DXL tree use the projected col ref
		ReplaceSubplan(pmp, phmcrdxlnSubplans, pcr, pdxlopPrEl);
	}

	// attach scalar id expression to proj elem
	pdxlnPrEl->AddChild(pdxlnScId);
	
	return pdxlnPrEl;
}


//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnIdent
//
//	@doc:
//		 Create a scalar identifier node from a given col ref
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnIdent
	(
	IMemoryPool *pmp,
	HMCrDxln *phmcrdxlnSubplans,
	HMCrDxln *phmcrdxlnIndexLookup,
	const CColRef *pcr
	)
{
	GPOS_ASSERT(NULL != pcr);
	GPOS_ASSERT(NULL != phmcrdxlnSubplans);
	
	CDXLNode *pdxln = phmcrdxlnSubplans->PtLookup(pcr);

	if (NULL != pdxln)
	{
		pdxln->AddRef();
		return pdxln;
	}

	if (NULL != phmcrdxlnIndexLookup)
	{
		CDXLNode *pdxlnIdent = phmcrdxlnIndexLookup->PtLookup(pcr);
		if (NULL != pdxlnIdent)
		{
			pdxlnIdent->AddRef();
			return pdxlnIdent;
		}
	}

	CMDName *pmdname = GPOS_NEW(pmp) CMDName(pmp, pcr->Name().Pstr());
	
	CDXLColRef *pdxlcr = GPOS_NEW(pmp) CDXLColRef(pmp, pmdname, pcr->UlId());
	
	IMDId *pmdid = pcr->Pmdtype()->Pmdid();
	pmdid->AddRef();
	
	CDXLScalarIdent *pdxlop = GPOS_NEW(pmp) CDXLScalarIdent(pmp, pdxlcr, pmdid);
	return GPOS_NEW(pmp) CDXLNode(pmp, pdxlop);
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdrgpdatumNulls
//
//	@doc:
//		Create an array of NULL datums for a given array of columns
//
//---------------------------------------------------------------------------
DrgPdatum *
CTranslatorExprToDXLUtils::PdrgpdatumNulls
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr
	)
{
	DrgPdatum *pdrgpdatum = GPOS_NEW(pmp) DrgPdatum(pmp);

	const ULONG ulSize = pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];
		const IMDType *pmdtype = pcr->Pmdtype();
		IDatum *pdatum = pmdtype->PdatumNull();
		pdatum->AddRef();
		pdrgpdatum->Append(pdatum);
	}

	return pdrgpdatum;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::FProjectListMatch
//
//	@doc:
//		Check whether a project list has the same columns in the given array
//		and in the same order
//
//---------------------------------------------------------------------------
BOOL
CTranslatorExprToDXLUtils::FProjectListMatch
	(
	CDXLNode *pdxlnPrL,
	DrgPcr *pdrgpcr
	)
{
	GPOS_ASSERT(NULL != pdxlnPrL);
	GPOS_ASSERT(NULL != pdrgpcr);
	GPOS_ASSERT(EdxlopScalarProjectList == pdxlnPrL->Pdxlop()->Edxlop());

	const ULONG ulLen = pdrgpcr->UlLength();
	if (pdxlnPrL->UlArity() != ulLen)
	{
		return false;
	}

	for (ULONG ul = 0; ul < ulLen; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];

		CDXLNode *pdxlnPrEl = (*pdxlnPrL)[ul];
		CDXLScalarProjElem *pdxlopPrEl = CDXLScalarProjElem::PdxlopConvert(pdxlnPrEl->Pdxlop());

		if (pcr->UlId() != pdxlopPrEl->UlId())
		{
			return false;
		}
	}

	return true;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdrgpcrMapColumns
//
//	@doc:
//		Map an array of columns to a new array of columns. The column index is
//		look up in the given hash map, then the corresponding column from
//		the destination array is used
//
//---------------------------------------------------------------------------
DrgPcr *
CTranslatorExprToDXLUtils::PdrgpcrMapColumns
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcrInput,
	HMCrUl *phmcrul,
	DrgPcr *pdrgpcrMapDest
	)
{
	GPOS_ASSERT(NULL != phmcrul);
	GPOS_ASSERT(NULL != pdrgpcrMapDest);

	if (NULL == pdrgpcrInput)
	{
		return NULL;
	}

	DrgPcr *pdrgpcrNew = GPOS_NEW(pmp) DrgPcr(pmp);

	const ULONG ulLen = pdrgpcrInput->UlLength();
	for (ULONG ul = 0; ul < ulLen; ul++)
	{
		CColRef *pcr = (*pdrgpcrInput)[ul];

		// get column index from hashmap
		ULONG *pul = phmcrul->PtLookup(pcr);
		GPOS_ASSERT (NULL != pul);

		// add corresponding column from dest array
		pdrgpcrNew->Append((*pdrgpcrMapDest)[*pul]);
	}

	return pdrgpcrNew;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnResult
//
//	@doc:
//		Create a DXL result node using the given properties, project list,
//		filters, and relational child
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnResult
	(
	IMemoryPool *pmp,
	CDXLPhysicalProperties *pdxlprop,
	CDXLNode *pdxlnPrL,
	CDXLNode *pdxlnFilter,
	CDXLNode *pdxlnOneTimeFilter,
	CDXLNode *pdxlnChild
	)
{
	CDXLPhysicalResult *pdxlop = GPOS_NEW(pmp) CDXLPhysicalResult(pmp);
	CDXLNode *pdxlnResult = GPOS_NEW(pmp) CDXLNode(pmp, pdxlop);
	pdxlnResult->SetProperties(pdxlprop);
	
	pdxlnResult->AddChild(pdxlnPrL);
	pdxlnResult->AddChild(pdxlnFilter);
	pdxlnResult->AddChild(pdxlnOneTimeFilter);

	if (NULL != pdxlnChild)
	{
		pdxlnResult->AddChild(pdxlnChild);
	}

#ifdef GPOS_DEBUG
	pdxlop->AssertValid(pdxlnResult, false /* fValidateChildren */);
#endif

	return pdxlnResult;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnPartitionSelector
//
//	@doc:
//		Construct a partition selector node
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnPartitionSelector
	(
	IMemoryPool *pmp,
	IMDId *pmdid,
	ULONG ulPartLevels,
	ULONG ulScanId,
	CDXLPhysicalProperties *pdxlprop,
	CDXLNode *pdxlnPrL,
	CDXLNode *pdxlnEqFilters,
	CDXLNode *pdxlnFilters,
	CDXLNode *pdxlnResidual,
	CDXLNode *pdxlnPropagation,
	CDXLNode *pdxlnPrintable,
	CDXLNode *pdxlnChild
	)
{
	CDXLNode *pdxlnSelector = GPOS_NEW(pmp) CDXLNode
										(
										pmp,
										GPOS_NEW(pmp) CDXLPhysicalPartitionSelector(pmp, pmdid, ulPartLevels, ulScanId)
										);

	pdxlnSelector->SetProperties(pdxlprop);
	pdxlnSelector->AddChild(pdxlnPrL);
	pdxlnSelector->AddChild(pdxlnEqFilters);
	pdxlnSelector->AddChild(pdxlnFilters);
	pdxlnSelector->AddChild(pdxlnResidual);
	pdxlnSelector->AddChild(pdxlnPropagation);
	pdxlnSelector->AddChild(pdxlnPrintable);
	if (NULL != pdxlnChild)
	{
		pdxlnSelector->AddChild(pdxlnChild);
	}

	return pdxlnSelector;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlnCombineBoolean
//
//	@doc:
//		Combine two boolean expressions using the given boolean operator
//
//---------------------------------------------------------------------------
CDXLNode *
CTranslatorExprToDXLUtils::PdxlnCombineBoolean
	(
	IMemoryPool *pmp,
	CDXLNode *pdxlnFst,
	CDXLNode *pdxlnSnd,
	EdxlBoolExprType boolexptype
	)
{
	GPOS_ASSERT(Edxlor == boolexptype ||
				Edxland == boolexptype);

	if (NULL == pdxlnFst)
	{
		return pdxlnSnd;
	}

	if (NULL == pdxlnSnd)
	{
		return pdxlnFst;
	}

	return  GPOS_NEW(pmp) CDXLNode
						(
						pmp,
						GPOS_NEW(pmp) CDXLScalarBoolExpr(pmp, boolexptype),
						pdxlnFst,
						pdxlnSnd
						);
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PhmcrulColIndex
//
//	@doc:
//		Build a hashmap based on a column array, where the key is the column
//		and the value is the index of that column in the array
//
//---------------------------------------------------------------------------
HMCrUl *
CTranslatorExprToDXLUtils::PhmcrulColIndex
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr
	)
{
	HMCrUl *phmcrul = GPOS_NEW(pmp) HMCrUl(pmp);

	const ULONG ulLen = pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulLen; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];
		ULONG *pul = GPOS_NEW(pmp) ULONG(ul);

		// add to hashmap
	#ifdef GPOS_DEBUG
		BOOL fRes =
	#endif // GPOS_DEBUG
		phmcrul->FInsert(pcr, pul);
		GPOS_ASSERT(fRes);
	}

	return phmcrul;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::SetStats
//
//	@doc:
//		Set the statistics of the dxl operator
//
//---------------------------------------------------------------------------
void
CTranslatorExprToDXLUtils::SetStats
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	CDXLNode *pdxln,
	const IStatistics *pstats,
	BOOL fRoot
	)
{
	if (NULL != pstats && GPOS_FTRACE(EopttraceExtractDXLStats) && 
		(GPOS_FTRACE(EopttraceExtractDXLStatsAllNodes) || fRoot)
		)
	{
		CDXLPhysicalProperties::PdxlpropConvert(pdxln->Pdxlprop())->SetStats(pstats->Pdxlstatsderrel(pmp, pmda));
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::SetDirectDispatchInfo
//
//	@doc:
//		Set the direct dispatch info of the dxl operator
//
//---------------------------------------------------------------------------
void
CTranslatorExprToDXLUtils::SetDirectDispatchInfo
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	CDXLNode *pdxln,
	CDrvdPropRelational *pdpRel,
	DrgPds *pdrgpdsBaseTables
	)
{
	GPOS_ASSERT(NULL != pdxln);
	GPOS_ASSERT(NULL != pdpRel);
	GPOS_ASSERT(NULL != pdrgpdsBaseTables);
	
	Edxlopid edxlopid = pdxln->Pdxlop()->Edxlop();
	if (EdxlopPhysicalCTAS == edxlopid || EdxlopPhysicalDML == edxlopid || EdxlopPhysicalRowTrigger == edxlopid)
	{
		// direct dispatch for CTAS and DML handled elsewhere
		// TODO:  - Oct 15, 2014; unify
		return;
	}
	
	if (1 != pdpRel->UlJoinDepth() || 1 != pdrgpdsBaseTables->UlLength())
	{
		// direct dispatch not supported for join queries
		return;
	}
		
	CDistributionSpec *pds = (*pdrgpdsBaseTables)[0];
	
	if (CDistributionSpec::EdtHashed != pds->Edt())
	{
		// direct dispatch only supported for scans over hash distributed tables 
		return;
	}
	
	CPropConstraint *ppc = pdpRel->Ppc();
	if (NULL == ppc->Pcnstr())
	{
		return;
	}
		
	GPOS_ASSERT(NULL != ppc->Pcnstr());
	
	CDistributionSpecHashed *pdsHashed = CDistributionSpecHashed::PdsConvert(pds);
	DrgPexpr *pdrgpexprHashed = pdsHashed->Pdrgpexpr();
	
	CDXLDirectDispatchInfo *pdxlddinfo = Pdxlddinfo(pmp, pmda, pdrgpexprHashed, ppc->Pcnstr());
	
	if (NULL != pdxlddinfo)
	{
		pdxln->SetDirectDispatchInfo(pdxlddinfo);
	}
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::Pdxlddinfo
//
//	@doc:
//		Compute the direct dispatch info spec. Returns NULL if this is not
//		possible
//
//---------------------------------------------------------------------------
CDXLDirectDispatchInfo *
CTranslatorExprToDXLUtils::Pdxlddinfo
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda,
	DrgPexpr *pdrgpexprHashed, 
	CConstraint *pcnstr
	)
{
	GPOS_ASSERT(NULL != pdrgpexprHashed);
	GPOS_ASSERT(NULL != pcnstr);
	
	const ULONG ulHashExpr = pdrgpexprHashed->UlLength();
	GPOS_ASSERT(0 < ulHashExpr);
	
	if (1 == ulHashExpr)
	{
		CExpression *pexprHashed = (*pdrgpexprHashed)[0];
		return PdxlddinfoSingleDistrKey(pmp, pmda, pexprHashed, pcnstr);
	}
	
	BOOL fSuccess = true;
	DrgPdxldatum *pdrgpdxldatum = GPOS_NEW(pmp) DrgPdxldatum(pmp);

	for (ULONG ul = 0; ul < ulHashExpr && fSuccess; ul++)
	{
		CExpression *pexpr = (*pdrgpexprHashed)[ul];
		if (!CUtils::FScalarIdent(pexpr))
		{
			fSuccess = false; 
			break;
		}
		
		const CColRef *pcrDistrCol = CScalarIdent::PopConvert(pexpr->Pop())->Pcr();
		
		CConstraint *pcnstrDistrCol = pcnstr->Pcnstr(pmp, pcrDistrCol);
		
		CDXLDatum *pdxldatum = PdxldatumFromPointConstraint(pmp, pmda, pcrDistrCol, pcnstrDistrCol);
		CRefCount::SafeRelease(pcnstrDistrCol);

		if (NULL != pdxldatum && FDirectDispatchable(pcrDistrCol, pdxldatum))
		{
			pdrgpdxldatum->Append(pdxldatum);
		}
		else
		{
			CRefCount::SafeRelease(pdxldatum);

			fSuccess = false;
			break;
		}
	}
	
	if (!fSuccess)
	{
		pdrgpdxldatum->Release();

		return NULL;
	}
	
	DrgPdrgPdxldatum *pdrgpdrgpdxldatum = GPOS_NEW(pmp) DrgPdrgPdxldatum(pmp);
	pdrgpdrgpdxldatum->Append(pdrgpdxldatum);
	return GPOS_NEW(pmp) CDXLDirectDispatchInfo(pdrgpdrgpdxldatum);
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxlddinfoSingleDistrKey
//
//	@doc:
//		Compute the direct dispatch info spec for a single distribution key.
//		Returns NULL if this is not possible
//
//---------------------------------------------------------------------------
CDXLDirectDispatchInfo *
CTranslatorExprToDXLUtils::PdxlddinfoSingleDistrKey
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda,
	CExpression *pexprHashed, 
	CConstraint *pcnstr
	)
{
	GPOS_ASSERT(NULL != pexprHashed);
	if (!CUtils::FScalarIdent(pexprHashed))
	{
		return NULL;
	}
	
	const CColRef *pcrDistrCol = CScalarIdent::PopConvert(pexprHashed->Pop())->Pcr();
	
	CConstraint *pcnstrDistrCol = pcnstr->Pcnstr(pmp, pcrDistrCol);
	
	DrgPdrgPdxldatum *pdrgpdrgpdxldatum = NULL;
	
	if (CPredicateUtils::FConstColumn(pcnstrDistrCol, pcrDistrCol))
	{
		CDXLDatum *pdxldatum = PdxldatumFromPointConstraint(pmp, pmda, pcrDistrCol, pcnstrDistrCol);
		GPOS_ASSERT(NULL != pdxldatum);

		if (FDirectDispatchable(pcrDistrCol, pdxldatum))
		{
			DrgPdxldatum *pdrgpdxldatum = GPOS_NEW(pmp) DrgPdxldatum(pmp);

			pdxldatum->AddRef();
			pdrgpdxldatum->Append(pdxldatum);
		
			pdrgpdrgpdxldatum = GPOS_NEW(pmp) DrgPdrgPdxldatum(pmp);
			pdrgpdrgpdxldatum->Append(pdrgpdxldatum);
		}

		pdxldatum->Release();
	}
	else if (CPredicateUtils::FColumnDisjunctionOfConst(pcnstrDistrCol, pcrDistrCol))
	{
		pdrgpdrgpdxldatum = PdrgpdrgpdxldatumFromDisjPointConstraint(pmp, pmda, pcrDistrCol, pcnstrDistrCol);
	}
	
	CRefCount::SafeRelease(pcnstrDistrCol);

	if (NULL == pdrgpdrgpdxldatum)
	{
		return NULL;
	}
	
	return GPOS_NEW(pmp) CDXLDirectDispatchInfo(pdrgpdrgpdxldatum);
}


//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::FDirectDispatchable
//
//	@doc:
//		Check if the given constant value for a particular distribution column
// 		can be used to identify which segment to direct dispatch to.
//
//---------------------------------------------------------------------------
BOOL
CTranslatorExprToDXLUtils::FDirectDispatchable
	(
	const CColRef *pcrDistrCol,
	const CDXLDatum *pdxldatum
	)
{
	GPOS_ASSERT(NULL != pcrDistrCol);
	GPOS_ASSERT(NULL != pdxldatum);

	IMDId *pmdidDatum = pdxldatum->Pmdid();
	IMDId *pmdidDistrCol = pcrDistrCol->Pmdtype()->Pmdid();

	// since all integer values are up-casted to int64, the hash value will be
	// consistent. If either the constant or the distribution column are
	// not integers, then their datatypes must be identical to ensure that
	// the hash value of the constant will point to the right segment.
	BOOL fBothInt = CUtils::FIntType(pmdidDistrCol) && CUtils::FIntType(pmdidDatum);

	return fBothInt || (pmdidDatum->FEquals(pmdidDistrCol));
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdxldatumFromPointConstraint
//
//	@doc:
//		Compute a DXL datum from a point constraint. Returns NULL if this is not
//		possible
//
//---------------------------------------------------------------------------
CDXLDatum *
CTranslatorExprToDXLUtils::PdxldatumFromPointConstraint
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda,
	const CColRef *pcrDistrCol, 
	CConstraint *pcnstrDistrCol
	)
{
	if (!CPredicateUtils::FConstColumn(pcnstrDistrCol, pcrDistrCol))
	{
		return NULL;
	}
	
	GPOS_ASSERT(CConstraint::EctInterval == pcnstrDistrCol->Ect());
	
	CConstraintInterval *pci = dynamic_cast<CConstraintInterval *>(pcnstrDistrCol);
	GPOS_ASSERT(1 >= pci->Pdrgprng()->UlLength());
	
	CDXLDatum *pdxldatum = NULL;
	
	if (1 == pci->Pdrgprng()->UlLength())
	{
		const CRange *prng = (*pci->Pdrgprng())[0];
		pdxldatum = CTranslatorExprToDXLUtils::Pdxldatum(pmp, pmda, prng->PdatumLeft());
	}
	else
	{
		GPOS_ASSERT(pci->FIncludesNull());
		pdxldatum = pcrDistrCol->Pmdtype()->PdxldatumNull(pmp);
	}
	
	return pdxldatum;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::PdrgpdrgpdxldatumFromDisjPointConstraint
//
//	@doc:
//		Compute an array of DXL datum arrays from a disjunction of point constraints. 
//		Returns NULL if this is not possible
//
//---------------------------------------------------------------------------
DrgPdrgPdxldatum *
CTranslatorExprToDXLUtils::PdrgpdrgpdxldatumFromDisjPointConstraint
	(
	IMemoryPool *pmp, 
	CMDAccessor *pmda,
	const CColRef *pcrDistrCol, 
	CConstraint *pcnstrDistrCol
	)
{
	GPOS_ASSERT(NULL != pcnstrDistrCol);
	if (CPredicateUtils::FConstColumn(pcnstrDistrCol, pcrDistrCol))
	{
		DrgPdrgPdxldatum *pdrgpdrgpdxldatum = NULL;

		CDXLDatum *pdxldatum = PdxldatumFromPointConstraint(pmp, pmda, pcrDistrCol, pcnstrDistrCol);

		if (FDirectDispatchable(pcrDistrCol, pdxldatum))
		{
			DrgPdxldatum *pdrgpdxldatum = GPOS_NEW(pmp) DrgPdxldatum(pmp);

			pdxldatum->AddRef();
			pdrgpdxldatum->Append(pdxldatum);

			pdrgpdrgpdxldatum = GPOS_NEW(pmp) DrgPdrgPdxldatum(pmp);
			pdrgpdrgpdxldatum->Append(pdrgpdxldatum);
		}

		// clean up
		pdxldatum->Release();

		return pdrgpdrgpdxldatum;
	}
	
	GPOS_ASSERT(CConstraint::EctInterval == pcnstrDistrCol->Ect());
	
	CConstraintInterval *pcnstrInterval = dynamic_cast<CConstraintInterval *>(pcnstrDistrCol);

	DrgPrng *pdrgprng = pcnstrInterval->Pdrgprng();

	const ULONG ulRanges = pdrgprng->UlLength();
	DrgPdrgPdxldatum *pdrgpdrgpdxdatum = GPOS_NEW(pmp) DrgPdrgPdxldatum(pmp);
	
	for (ULONG ul = 0; ul < ulRanges; ul++)
	{
		CRange *prng = (*pdrgprng)[ul];
		GPOS_ASSERT(prng->FPoint());
		CDXLDatum *pdxldatum = CTranslatorExprToDXLUtils::Pdxldatum(pmp, pmda, prng->PdatumLeft());

		if (!FDirectDispatchable(pcrDistrCol, pdxldatum))
		{
			// clean up
			pdxldatum->Release();
			pdrgpdrgpdxdatum->Release();

			return NULL;
		}

		DrgPdxldatum *pdrgpdxldatum = GPOS_NEW(pmp) DrgPdxldatum(pmp);

		pdrgpdxldatum->Append(pdxldatum);
		pdrgpdrgpdxdatum->Append(pdrgpdxldatum);
	}
	
	if (pcnstrInterval->FIncludesNull())
	{
		CDXLDatum *pdxldatum = pcrDistrCol->Pmdtype()->PdxldatumNull(pmp);

		if (!FDirectDispatchable(pcrDistrCol, pdxldatum))
		{
			// clean up
			pdxldatum->Release();
			pdrgpdrgpdxdatum->Release();

			return NULL;
		}

		DrgPdxldatum *pdrgpdxldatum = GPOS_NEW(pmp) DrgPdxldatum(pmp);
		pdrgpdxldatum->Append(pdxldatum);
		pdrgpdrgpdxdatum->Append(pdrgpdxldatum);
	}
	
	if (0 < pdrgpdrgpdxdatum->UlLength())
	{
		return pdrgpdrgpdxdatum;
	}

	// clean up
	pdrgpdrgpdxdatum->Release();

	return NULL;
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::FLocalHashAggStreamSafe
//
//	@doc:
//		Is the aggregate a local hash aggregate that is safe to stream
//
//---------------------------------------------------------------------------
BOOL
CTranslatorExprToDXLUtils::FLocalHashAggStreamSafe
	(
	CExpression *pexprAgg
	)
{
	GPOS_ASSERT(NULL != pexprAgg);

	COperator::EOperatorId eopid = pexprAgg->Pop()->Eopid();

	if (COperator::EopPhysicalHashAgg !=  eopid && COperator::EopPhysicalHashAggDeduplicate != eopid)
	{
		// not a hash aggregate
		return false;
	}

	CPhysicalAgg *popAgg = CPhysicalAgg::PopConvert(pexprAgg->Pop());

	// is a local hash aggregate and it generates duplicates (therefore safe to stream)
	return (COperator::EgbaggtypeLocal == popAgg->Egbaggtype()) && popAgg->FGeneratesDuplicates();
}

//---------------------------------------------------------------------------
//	@function:
//		CTranslatorExprToDXLUtils::ExtractCastMdids
//
//	@doc:
//		If operator is a scalar cast, extract cast type and function
//
//---------------------------------------------------------------------------
void
CTranslatorExprToDXLUtils::ExtractCastMdids
	(
	COperator *pop, 
	IMDId **ppmdidType, 
	IMDId **ppmdidCastFunc
	)
{
	GPOS_ASSERT(NULL != pop);
	GPOS_ASSERT(NULL != ppmdidType);
	GPOS_ASSERT(NULL != ppmdidCastFunc);

	if (COperator::EopScalarCast != pop->Eopid())
	{
		// not a cast
		return;
	}

	CScalarCast *popCast = CScalarCast::PopConvert(pop);
	*ppmdidType = popCast->PmdidType();
	*ppmdidCastFunc = popCast->PmdidFunc();
}

// EOF
