	//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CUtils.cpp
//
//	@doc:
//		Implementation of general utility functions
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#include "gpos/common/clibwrapper.h"
#include "gpos/common/syslibwrapper.h"
#include "gpos/string/CWStringDynamic.h"
#include "gpos/io/CFileDescriptor.h"
#include "gpos/io/COstreamString.h"
#include "gpos/memory/CAutoMemoryPool.h"
#include "gpos/task/CWorker.h"

#include "gpopt/base/CColRefSetIter.h"
#include "gpopt/base/CColRefTable.h"
#include "gpopt/base/CConstraintInterval.h"
#include "gpopt/base/CKeyCollection.h"
#include "gpopt/base/CUtils.h"
#include "gpopt/base/CPartIndexMap.h"
#include "gpopt/base/CDistributionSpecRandom.h"
#include "gpopt/operators/CPhysicalMotionRandom.h"
#include "gpopt/operators/ops.h"
#include "gpopt/operators/CLogicalCTEProducer.h"
#include "gpopt/operators/CLogicalCTEConsumer.h"
#include "gpopt/translate/CTranslatorExprToDXLUtils.h"
#include "gpopt/search/CMemo.h"
#include "gpopt/mdcache/CMDAccessorUtils.h"

#include "naucrates/base/IDatumInt2.h"
#include "naucrates/base/IDatumInt4.h"
#include "naucrates/base/IDatumInt8.h"
#include "naucrates/base/IDatumOid.h"
#include "naucrates/md/IMDAggregate.h"
#include "naucrates/md/IMDScalarOp.h"
#include "naucrates/md/IMDScCmp.h"
#include "naucrates/md/IMDType.h"
#include "naucrates/md/IMDTypeBool.h"
#include "naucrates/md/IMDTypeInt4.h"
#include "naucrates/md/IMDTypeInt8.h"
#include "naucrates/md/IMDTypeOid.h"
#include "naucrates/md/CMDIdGPDB.h"
#include "naucrates/md/IMDCast.h"
#include "naucrates/md/CMDIdScCmp.h"
#include "naucrates/traceflags/traceflags.h"

using namespace gpopt;
using namespace gpmd;

#ifdef GPOS_DEBUG

// buffer of 16M characters for print wrapper
const ULONG ulBufferCapacity = 16 * 1024 * 1024;
static WCHAR wszBuffer[ulBufferCapacity];

//---------------------------------------------------------------------------
//	@function:
//		PrintExpr
//
//	@doc:
//		Global wrapper for debug print of expression
//
//---------------------------------------------------------------------------
void PrintExpr
	(
	void *pv
	)
{
	gpopt::CUtils::PrintExpression((gpopt::CExpression*)pv);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PrintExpression
//
//	@doc:
//		Debug print expression
//
//---------------------------------------------------------------------------
void
CUtils::PrintExpression
	(
	CExpression *pexpr
	)
{
	CWStringStatic str(wszBuffer, ulBufferCapacity);
	COstreamString oss(&str);
	
	if (NULL == pexpr)
	{
		oss << std::endl << "(null)" << std::endl;
	}
	else 
	{
		oss << std::endl << pexpr << std::endl << *pexpr << std::endl;
	}

	GPOS_TRACE(str.Wsz());
	str.Reset();
}


//---------------------------------------------------------------------------
//	@function:
//		PrintMemo
//
//	@doc:
//		Global wrapper for debug print of memo
//
//---------------------------------------------------------------------------
void PrintMemo
	(
	void *pv
	)
{
	gpopt::CUtils::PrintMemo((gpopt::CMemo*)pv);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PrintMemo
//
//	@doc:
//		Debug print Memo structure
//
//---------------------------------------------------------------------------
void
CUtils::PrintMemo
	(
	CMemo *pmemo
	)
{
	CWStringStatic str(wszBuffer, ulBufferCapacity);
	COstreamString oss(&str);
	
	if (NULL == pmemo)
	{
		oss << std::endl << "(null)" << std::endl;
	}
	else 
	{
		oss << std::endl << pmemo << std::endl;
		(void) pmemo->OsPrint(oss);
		oss << std::endl;
	}
	
	GPOS_TRACE(str.Wsz());
	str.Reset();
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::OsPrintDrgPcoldesc
//
//	@doc:
//		Helper function to print a column descriptor array
//
//---------------------------------------------------------------------------
IOstream &
CUtils::OsPrintDrgPcoldesc
	(
	IOstream &os,
	DrgPcoldesc *pdrgpcoldesc,
	ULONG ulLengthMax
	)
{
	ULONG ulLength = std::min(pdrgpcoldesc->UlLength(), ulLengthMax);
	for (ULONG ul = 0; ul < ulLength; ul++)
	{
		CColumnDescriptor *pcoldesc = (*pdrgpcoldesc)[ul];
		pcoldesc->OsPrint(os);

		// separator
		os << (ul == ulLength - 1 ? "" : ", ");
	}

	return os;
}

#endif // GPOS_DEBUG


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarIdent
//
//	@doc:
//		Generate a ScalarIdent expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarIdent
	(
	IMemoryPool *pmp,
	const CColRef *pcr
	)
{
	GPOS_ASSERT(NULL != pcr);

	return GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CScalarIdent(pmp, pcr));
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarProjectElement
//
//	@doc:
//		Generate a ScalarProjectElement expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarProjectElement
	(
	IMemoryPool *pmp,
	CColRef *pcr,
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pcr);
	GPOS_ASSERT(NULL != pexpr);

	return GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CScalarProjectElement(pmp, pcr), pexpr);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PmdidScCmp
//
//	@doc:
//		Return the scalar comparison operator id between the two types
//
//---------------------------------------------------------------------------
IMDId *
CUtils::PmdidScCmp
	(
	CMDAccessor *pmda,
	IMDId *pmdidLeft,
	IMDId *pmdidRight,
	IMDType::ECmpType ecmpt
	)
{
	GPOS_ASSERT(NULL != pmdidLeft);
	GPOS_ASSERT(NULL != pmdidRight);
	GPOS_ASSERT(IMDType::EcmptOther > ecmpt);
	
	if (pmdidLeft->FEquals(pmdidRight))
	{
		const IMDType *pmdtypeLeft = pmda->Pmdtype(pmdidLeft);
		return pmdtypeLeft->PmdidCmp(ecmpt);
	}
	
	return pmda->Pmdsccmp(pmdidLeft, pmdidRight, ecmpt)->PmdidOp();
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarCmp
//
//	@doc:
//		Generate a comparison expression over two columns
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarCmp
	(
	IMemoryPool *pmp,
	const CColRef *pcrLeft,
	const CColRef *pcrRight,
	CWStringConst strOp,
	IMDId *pmdidOp
	)
{
	GPOS_ASSERT(NULL != pcrLeft);
	GPOS_ASSERT(NULL != pcrRight);

	IMDType::ECmpType ecmpt = Ecmpt(pmdidOp);
	if (IMDType::EcmptOther != ecmpt)
	{
		IMDId *pmdidLeft = pcrLeft->Pmdtype()->Pmdid();
		IMDId *pmdidRight = pcrRight->Pmdtype()->Pmdid();

		if (FCmpOrCastedCmpExists(pmdidLeft, pmdidRight, ecmpt))
		{
			CExpression *pexprScCmp = PexprScalarCmp(pmp, pcrLeft, pcrRight, ecmpt);
			pmdidOp->Release();

			return pexprScCmp;
		}
	}

	return GPOS_NEW(pmp) CExpression
						(
						pmp,
						GPOS_NEW(pmp) CScalarCmp(pmp, pmdidOp, GPOS_NEW(pmp) CWStringConst(pmp, strOp.Wsz()), Ecmpt(pmdidOp)),
						PexprScalarIdent(pmp, pcrLeft),
						PexprScalarIdent(pmp, pcrRight)
						);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FCmpOrCastedCmpExists
//
//	@doc:
//		Check is a comparison between given types or a comparison after casting
//		one side to an another exists
//---------------------------------------------------------------------------
BOOL
CUtils::FCmpOrCastedCmpExists
	(
	IMDId *pmdidLeft,
	IMDId *pmdidRight,
	IMDType::ECmpType ecmpt
	)
{
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();

	if (CMDAccessorUtils::FCmpExists(pmda, pmdidLeft, pmdidRight, ecmpt))
	{
		return true;
	}

	if (CMDAccessorUtils::FCmpExists(pmda, pmdidRight, pmdidRight, ecmpt) && CMDAccessorUtils::FCastExists(pmda, pmdidLeft, pmdidRight))
	{
		return true;
	}

	if (CMDAccessorUtils::FCmpExists(pmda, pmdidLeft, pmdidLeft, ecmpt) && CMDAccessorUtils::FCastExists(pmda, pmdidRight, pmdidLeft))
	{
		return true;
	}

	return false;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarCmp
//
//	@doc:
//		Generate a comparison expression between a column and an expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarCmp
	(
	IMemoryPool *pmp,
	const CColRef *pcrLeft,
	CExpression *pexprRight,
	CWStringConst strOp,
	IMDId *pmdidOp
	)
{
	GPOS_ASSERT(NULL != pcrLeft);
	GPOS_ASSERT(NULL != pexprRight);

	IMDType::ECmpType ecmpt = Ecmpt(pmdidOp);
	if (IMDType::EcmptOther != ecmpt)
	{
		IMDId *pmdidLeft = pcrLeft->Pmdtype()->Pmdid();
		IMDId *pmdidRight = CScalar::PopConvert(pexprRight->Pop())->PmdidType();

		if (FCmpOrCastedCmpExists(pmdidLeft, pmdidRight, ecmpt))
		{
			CExpression *pexprScCmp = PexprScalarCmp(pmp, pcrLeft, pexprRight, ecmpt);
			pmdidOp->Release();

			return pexprScCmp;
		}
	}

	return GPOS_NEW(pmp) CExpression
						(
						pmp,
						GPOS_NEW(pmp) CScalarCmp(pmp, pmdidOp, GPOS_NEW(pmp) CWStringConst(pmp, strOp.Wsz()), Ecmpt(pmdidOp)),
						PexprScalarIdent(pmp, pcrLeft),
						pexprRight
						);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarCmp
//
//	@doc:
//		Generate a comparison expression between two columns
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarCmp
	(
	IMemoryPool *pmp,
	const CColRef *pcrLeft,
	const CColRef *pcrRight,
	IMDType::ECmpType ecmpt
	)
{
	GPOS_ASSERT(NULL != pcrLeft);
	GPOS_ASSERT(NULL != pcrRight);
	GPOS_ASSERT(IMDType::EcmptOther > ecmpt);

	CExpression *pexprLeft = PexprScalarIdent(pmp, pcrLeft);
	CExpression *pexprRight = PexprScalarIdent(pmp, pcrRight);

	return PexprScalarCmp(pmp, pexprLeft, pexprRight, ecmpt);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarCmp
//
//	@doc:
//		Generate a comparison expression over a column and an expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarCmp
	(
	IMemoryPool *pmp,
	const CColRef *pcrLeft,
	CExpression *pexprRight,
	IMDType::ECmpType ecmpt
	)
{
	GPOS_ASSERT(NULL != pcrLeft);
	GPOS_ASSERT(NULL != pexprRight);
	GPOS_ASSERT(IMDType::EcmptOther > ecmpt);

	CExpression *pexprLeft = PexprScalarIdent(pmp, pcrLeft);
	
	return PexprScalarCmp(pmp, pexprLeft, pexprRight, ecmpt);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarCmp
//
//	@doc:
//		Generate a comparison expression between an expression and a column
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarCmp
	(
	IMemoryPool *pmp,
	CExpression *pexprLeft,
	const CColRef *pcrRight,
	IMDType::ECmpType ecmpt
	)
{
	GPOS_ASSERT(NULL != pexprLeft);
	GPOS_ASSERT(NULL != pcrRight);
	GPOS_ASSERT(IMDType::EcmptOther > ecmpt);

	CExpression *pexprRight = PexprScalarIdent(pmp, pcrRight);

	return PexprScalarCmp(pmp, pexprLeft, pexprRight, ecmpt);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarCmp
//
//	@doc:
//		Generate a comparison expression over an expression and a column
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarCmp
	(
	IMemoryPool *pmp,
	CExpression *pexprLeft,
	const CColRef *pcrRight,
	CWStringConst strOp,
	IMDId *pmdidOp
	)
{
	GPOS_ASSERT(NULL != pexprLeft);
	GPOS_ASSERT(NULL != pcrRight);

	IMDType::ECmpType ecmpt = Ecmpt(pmdidOp);
	if (IMDType::EcmptOther != ecmpt)
	{
		IMDId *pmdidLeft = CScalar::PopConvert(pexprLeft->Pop())->PmdidType();
		IMDId *pmdidRight = pcrRight->Pmdtype()->Pmdid();

		if (FCmpOrCastedCmpExists(pmdidLeft, pmdidRight, ecmpt))
		{
			CExpression *pexprScCmp = PexprScalarCmp(pmp, pexprLeft, pcrRight, ecmpt);
			pmdidOp->Release();
	    
			return pexprScCmp;
		}
	}
	
	return GPOS_NEW(pmp) CExpression
						(
						pmp,
						GPOS_NEW(pmp) CScalarCmp(pmp, pmdidOp, GPOS_NEW(pmp) CWStringConst(pmp, strOp.Wsz()), ecmpt),
						pexprLeft,
						PexprScalarIdent(pmp, pcrRight)
						);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarCmp
//
//	@doc:
//		Generate a comparison expression over two expressions
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarCmp
	(
	IMemoryPool *pmp,
	CExpression *pexprLeft,
	CExpression *pexprRight,
	CWStringConst strOp,
	IMDId *pmdidOp
	)
{
	GPOS_ASSERT(NULL != pexprLeft);
	GPOS_ASSERT(NULL != pexprRight);

	IMDType::ECmpType ecmpt = Ecmpt(pmdidOp);
	if (IMDType::EcmptOther != ecmpt)
	{
		IMDId *pmdidLeft = CScalar::PopConvert(pexprLeft->Pop())->PmdidType();
		IMDId *pmdidRight = CScalar::PopConvert(pexprRight->Pop())->PmdidType();

		if (FCmpOrCastedCmpExists(pmdidLeft, pmdidRight, ecmpt))
		{
			CExpression *pexprScCmp = PexprScalarCmp(pmp, pexprLeft, pexprRight, ecmpt);
			pmdidOp->Release();

			return pexprScCmp;
		}
	}
	
	return GPOS_NEW(pmp) CExpression
						(
						pmp,
						GPOS_NEW(pmp) CScalarCmp(pmp, pmdidOp, GPOS_NEW(pmp) CWStringConst(pmp, strOp.Wsz()), ecmpt),
						pexprLeft,
						pexprRight
						);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarCmp
//
//	@doc:
//		Generate a comparison expression over two expressions
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarCmp
	(
	IMemoryPool *pmp,
	CExpression *pexprLeft,
	CExpression *pexprRight,
	IMDType::ECmpType ecmpt
	)
{
	GPOS_ASSERT(NULL != pexprLeft);
	GPOS_ASSERT(NULL != pexprRight);
	GPOS_ASSERT(IMDType::EcmptOther > ecmpt);
	
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();

	IMDId *pmdidLeft = CScalar::PopConvert(pexprLeft->Pop())->PmdidType();
	IMDId *pmdidRight = CScalar::PopConvert(pexprRight->Pop())->PmdidType();

	CExpression *pexprNewLeft = pexprLeft;
	CExpression *pexprNewRight = pexprRight;

	IMDId *pmdidCmpOp = NULL;
	if (CMDAccessorUtils::FCmpExists(pmda, pmdidLeft, pmdidRight, ecmpt))
	{
		pmdidCmpOp = PmdidScCmp(pmda, pmdidLeft, pmdidRight, ecmpt);
	}
	else
	{
		// generate a scalar comparison by casting one of the two sides
		if (CMDAccessorUtils::FCastExists(pmda, pmdidLeft, pmdidRight))
		{
			pexprNewLeft = PexprCast(pmp, pmda, pexprLeft, pmdidRight);
			pmdidCmpOp = PmdidScCmp(pmda, pmdidRight, pmdidRight, ecmpt);
		}
		else
		{
			GPOS_ASSERT(CMDAccessorUtils::FCastExists(pmda, pmdidRight, pmdidLeft));
			pexprNewRight = PexprCast(pmp, pmda, pexprRight, pmdidLeft);
			pmdidCmpOp = PmdidScCmp(pmda, pmdidLeft, pmdidLeft, ecmpt);
		}
	}
	
	pmdidCmpOp->AddRef();
	const CMDName mdname = pmda->Pmdscop(pmdidCmpOp)->Mdname();
	CWStringConst strCmpOpName(mdname.Pstr()->Wsz());
	
	return GPOS_NEW(pmp) CExpression
					(
					pmp,
					GPOS_NEW(pmp) CScalarCmp(pmp, pmdidCmpOp, GPOS_NEW(pmp) CWStringConst(pmp, strCmpOpName.Wsz()), ecmpt),
					pexprNewLeft,
					pexprNewRight
					);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarEqCmp
//
//	@doc:
//		Generate an equality comparison expression over two columns
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarEqCmp
	(
	IMemoryPool *pmp,
	const CColRef *pcrLeft,
	const CColRef *pcrRight
	)
{
	GPOS_ASSERT(NULL != pcrLeft);
	GPOS_ASSERT(NULL != pcrRight);

	return PexprScalarCmp(pmp, pcrLeft, pcrRight, IMDType::EcmptEq);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarEqCmp
//
//	@doc:
//		Generate an equality comparison expression over two expressions
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarEqCmp
	(
	IMemoryPool *pmp,
	CExpression *pexprLeft,
	CExpression *pexprRight
	)
{
	GPOS_ASSERT(NULL != pexprLeft);
	GPOS_ASSERT(NULL != pexprRight);

	return PexprScalarCmp(pmp, pexprLeft, pexprRight, IMDType::EcmptEq);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarEqCmp
//
//	@doc:
//		Generate an equality comparison expression over a column reference and
//		an expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarEqCmp
	(
	IMemoryPool *pmp,
	const CColRef *pcrLeft,
	CExpression *pexprRight
	)
{
	GPOS_ASSERT(NULL != pcrLeft);
	GPOS_ASSERT(NULL != pexprRight);

	return PexprScalarCmp(pmp, pcrLeft, pexprRight, IMDType::EcmptEq);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarEqCmp
//
//	@doc:
//		Generate an equality comparison expression over an expression and a
//		column reference
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarEqCmp
	(
	IMemoryPool *pmp,
	CExpression *pexprLeft,
	const CColRef *pcrRight
	)
{
	GPOS_ASSERT(NULL != pexprLeft);
	GPOS_ASSERT(NULL != pcrRight);

	return PexprScalarCmp(pmp, pexprLeft, pcrRight, IMDType::EcmptEq);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprZeroCmp
//
//	@doc:
//		Generate a comparison against Zero
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprCmpWithZero
	(
	IMemoryPool *pmp,
	CExpression *pexprLeft,
	IMDId *pmdidTypeLeft,
	IMDType::ECmpType ecmptype
	)
{
	GPOS_ASSERT(pexprLeft->Pop()->FScalar());

	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();
	const IMDType *pmdtype = pmda->Pmdtype(pmdidTypeLeft);
	GPOS_ASSERT(pmdtype->Eti() == IMDType::EtiInt8 && "left expression must be of type int8");

	IMDId *pmdidOp = pmdtype->PmdidCmp(ecmptype);
	pmdidOp->AddRef();
	const CMDName mdname = pmda->Pmdscop(pmdidOp)->Mdname();
	CWStringConst strOpName(mdname.Pstr()->Wsz());

	return GPOS_NEW(pmp) CExpression
						(
						pmp,
						GPOS_NEW(pmp) CScalarCmp(pmp, pmdidOp, GPOS_NEW(pmp) CWStringConst(pmp, strOpName.Wsz()), ecmptype),
						pexprLeft,
						CUtils::PexprScalarConstInt8(pmp, 0 /*iVal*/)
						);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprIDF
//
//	@doc:
//		Generate an Is Distinct From expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprIDF
	(
	IMemoryPool *pmp,
	CExpression *pexprLeft,
	CExpression *pexprRight
	)
{
	GPOS_ASSERT(NULL != pexprLeft);
	GPOS_ASSERT(NULL != pexprRight);

	IMDId *pmdidLeft = CScalar::PopConvert(pexprLeft->Pop())->PmdidType();
	IMDId *pmdidRight = CScalar::PopConvert(pexprRight->Pop())->PmdidType();

	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();

	CExpression *pexprNewLeft = pexprLeft;
	CExpression *pexprNewRight = pexprRight;

	IMDId *pmdidEqOp = NULL;
	if (CMDAccessorUtils::FCmpExists(pmda, pmdidLeft, pmdidRight, IMDType::EcmptEq))
	{
		pmdidEqOp = PmdidScCmp(pmda, pmdidLeft, pmdidRight, IMDType::EcmptEq);
	}
	else if (CMDAccessorUtils::FCastExists(pmda, pmdidLeft, pmdidRight))
	{
		pexprNewLeft = PexprCast(pmp, pmda, pexprLeft, pmdidRight);
		pmdidEqOp = PmdidScCmp(pmda, pmdidRight, pmdidRight, IMDType::EcmptEq);
	}
	else
	{
		GPOS_ASSERT(CMDAccessorUtils::FCastExists(pmda, pmdidRight, pmdidLeft));
		pexprNewRight = PexprCast(pmp, pmda, pexprRight, pmdidLeft);
		pmdidEqOp = PmdidScCmp(pmda, pmdidLeft, pmdidLeft, IMDType::EcmptEq);
	}

	pmdidEqOp->AddRef();
	const CMDName mdname = pmda->Pmdscop(pmdidEqOp)->Mdname();
	CWStringConst strEqOpName(mdname.Pstr()->Wsz());

	return GPOS_NEW(pmp) CExpression
				(
				pmp,
				GPOS_NEW(pmp) CScalarIsDistinctFrom(pmp, pmdidEqOp, GPOS_NEW(pmp) CWStringConst(pmp, strEqOpName.Wsz())),
				pexprNewLeft,
				pexprNewRight
				);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprINDF
//
//	@doc:
//		Generate an Is NOT Distinct From expression for two columns;
//		the two columns must have the same type
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprINDF
	(
	IMemoryPool *pmp,
	const CColRef *pcrLeft,
	const CColRef *pcrRight
	)
{
	GPOS_ASSERT(NULL != pcrLeft);
	GPOS_ASSERT(NULL != pcrRight);

	return PexprINDF(pmp, PexprScalarIdent(pmp, pcrLeft), PexprScalarIdent(pmp, pcrRight));
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprINDF
//
//	@doc:
//		Generate an Is NOT Distinct From expression for scalar expressions;
//		the two expressions must have the same type
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprINDF
	(
	IMemoryPool *pmp,
	CExpression *pexprLeft,
	CExpression *pexprRight
	)
{
	GPOS_ASSERT(NULL != pexprLeft);
	GPOS_ASSERT(NULL != pexprRight);

	return PexprNegate(pmp, PexprIDF(pmp, pexprLeft, pexprRight));
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprIsNull
//
//	@doc:
//		Generate an Is Null expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprIsNull
	(
	IMemoryPool *pmp,
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	return GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CScalarNullTest(pmp), pexpr);
}
//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprIsNotNull
//
//	@doc:
//		Generate an Is Not Null expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprIsNotNull
	(
	IMemoryPool *pmp,
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	return PexprNegate(pmp, PexprIsNull(pmp, pexpr));
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprIsNotFalse
//
//	@doc:
//		Generate an Is Not False expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprIsNotFalse
	(
	IMemoryPool *pmp,
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	return PexprIDF(pmp, pexpr, PexprScalarConstBool(pmp, false /*fVal*/));
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FUsesNullableCol
//
//	@doc:
//		Find if a scalar expression uses a nullable column from the
//		output of a logical expression
//
//---------------------------------------------------------------------------
BOOL
CUtils::FUsesNullableCol
	(
	IMemoryPool *pmp,
	CExpression *pexprScalar,
	CExpression *pexprLogical
	)
{
	GPOS_ASSERT(pexprScalar->Pop()->FScalar());
	GPOS_ASSERT(pexprLogical->Pop()->FLogical());

	CColRefSet *pcrsNotNull = CDrvdPropRelational::Pdprel(pexprLogical->PdpDerive())->PcrsNotNull();
	CColRefSet *pcrsUsed = GPOS_NEW(pmp) CColRefSet(pmp);
	pcrsUsed->Include(CDrvdPropScalar::Pdpscalar(pexprScalar->PdpDerive())->PcrsUsed());
	pcrsUsed->Intersection(CDrvdPropRelational::Pdprel(pexprLogical->PdpDerive())->PcrsOutput());

	BOOL fUsesNullableCol = !pcrsNotNull->FSubset(pcrsUsed);
	pcrsUsed->Release();

	return fUsesNullableCol;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PcrExtractPartKey
//
//	@doc:
//		Extract the partition key at the given level from the given array of partition keys
//
//---------------------------------------------------------------------------
CColRef *
CUtils::PcrExtractPartKey
	(
	DrgDrgPcr *pdrgpdrgpcr,
	ULONG ulLevel
	)
{
	GPOS_ASSERT(NULL != pdrgpdrgpcr);
	GPOS_ASSERT(ulLevel < pdrgpdrgpcr->UlLength());
	
	DrgPcr *pdrgpcrPartKey = (*pdrgpdrgpcr)[ulLevel];
	GPOS_ASSERT(1 == pdrgpcrPartKey->UlLength() && "Composite keys not currently supported");
	
	return (*pdrgpcrPartKey)[0];
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FHasSubquery
//
//	@doc:
//		Check for existence of subqueries
//
//---------------------------------------------------------------------------
BOOL
CUtils::FHasSubquery
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);
	GPOS_ASSERT(pexpr->Pop()->FScalar());

	CDrvdProp *pdp = pexpr->PdpDerive();
	CDrvdPropScalar *pdpscalar = CDrvdPropScalar::Pdpscalar(pdp);

	return pdpscalar->FHasSubquery();
}


//---------------------------------------------------------------------------
//	@class:
//		CUtils::FHasSubqueryOrApply
//
//	@doc:
//		Check existence of subqueries or Apply operators in deep expression tree
//
//---------------------------------------------------------------------------
BOOL
CUtils::FHasSubqueryOrApply
	(
	CExpression *pexpr,
	BOOL fCheckRoot
	)
{
	GPOS_CHECK_STACK_SIZE;
	GPOS_ASSERT(NULL != pexpr);

	if (fCheckRoot)
	{
		COperator *pop = pexpr->Pop();
		if (FApply(pop) && !FCorrelatedApply(pop))
		{
			return true;
		}

		if (pop->FScalar() && CDrvdPropScalar::Pdpscalar(pexpr->PdpDerive())->FHasSubquery())
		{
			return true;
		}
	}

	const ULONG ulArity = pexpr->UlArity();
	BOOL fSubqueryOrApply = false;
	for (ULONG ul = 0; !fSubqueryOrApply && ul < ulArity; ul++)
	{
		fSubqueryOrApply = FHasSubqueryOrApply((*pexpr)[ul]);
	}

	return fSubqueryOrApply;
}

//---------------------------------------------------------------------------
//	@class:
//		CUtils::FHasCorrelatedApply
//
//	@doc:
//		Check existence of Correlated Apply operators in deep expression tree
//
//---------------------------------------------------------------------------
BOOL
CUtils::FHasCorrelatedApply
	(
	CExpression *pexpr,
	BOOL fCheckRoot
	)
{
	GPOS_CHECK_STACK_SIZE;
	GPOS_ASSERT(NULL != pexpr);

	if (fCheckRoot && FCorrelatedApply(pexpr->Pop()))
	{
		return true;
	}

	const ULONG ulArity = pexpr->UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		if (FHasCorrelatedApply((*pexpr)[ul]))
		{
			return true;
		}
	}

	return false;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FHasOuterRefs
//
//	@doc:
//		Check for existence of outer references
//
//---------------------------------------------------------------------------
BOOL
CUtils::FHasOuterRefs
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);
	GPOS_ASSERT(pexpr->Pop()->FLogical());

	CDrvdProp *pdp = pexpr->PdpDerive();
	return 0 < CDrvdPropRelational::Pdprel(pdp)->PcrsOuter()->CElements();
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FLogicalJoin
//
//	@doc:
//		Check if a given operator is a logical join
//
//---------------------------------------------------------------------------
BOOL
CUtils::FLogicalJoin
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	CLogicalJoin *popJoin = NULL;
	if (pop->FLogical())
	{
		// attempt casting to logical join,
		// dynamic cast returns NULL if operator is not join
		popJoin = dynamic_cast<CLogicalJoin*>(pop);
	}

	return (NULL != popJoin);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FLogicalSetOp
//
//	@doc:
//		Check if a given operator is a logical set operation
//
//---------------------------------------------------------------------------
BOOL
CUtils::FLogicalSetOp
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	CLogicalSetOp *popSetOp = NULL;
	if (pop->FLogical())
	{
		// attempt casting to logical SetOp,
		// dynamic cast returns NULL if operator is not SetOp
		popSetOp = dynamic_cast<CLogicalSetOp*>(pop);
	}

	return (NULL != popSetOp);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FLogicalUnary
//
//	@doc:
//		Check if a given operator is a logical unary operator
//
//---------------------------------------------------------------------------
BOOL
CUtils::FLogicalUnary
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	CLogicalUnary *popUnary = NULL;
	if (pop->FLogical())
	{
		// attempt casting to logical unary,
		// dynamic cast returns NULL if operator is not unary
		popUnary = dynamic_cast<CLogicalUnary*>(pop);
	}

	return (NULL != popUnary);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FHashJoin
//
//	@doc:
//		Check if a given operator is a hash join
//
//---------------------------------------------------------------------------
BOOL
CUtils::FHashJoin
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	CPhysicalHashJoin *popHJN = NULL;
	if (pop->FPhysical())
	{
		popHJN = dynamic_cast<CPhysicalHashJoin*>(pop);
	}

	return (NULL != popHJN);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FCorrelatedNLJoin
//
//	@doc:
//		Check if a given operator is a correlated nested loops join
//
//---------------------------------------------------------------------------
BOOL
CUtils::FCorrelatedNLJoin
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	BOOL fCorrelatedNLJ = false;
	if (FNLJoin(pop))
	{
		fCorrelatedNLJ = dynamic_cast<CPhysicalNLJoin*>(pop)->FCorrelated();
	}

	return fCorrelatedNLJ;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FNLJoin
//
//	@doc:
//		Check if a given operator is a nested loops join
//
//---------------------------------------------------------------------------
BOOL
CUtils::FNLJoin
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	CPhysicalNLJoin *popNLJ = NULL;
	if (pop->FPhysical())
	{
		popNLJ = dynamic_cast<CPhysicalNLJoin*>(pop);
	}

	return (NULL != popNLJ);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FPhysicalJoin
//
//	@doc:
//		Check if a given operator is a logical join
//
//---------------------------------------------------------------------------
BOOL
CUtils::FPhysicalJoin
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	return FHashJoin(pop) || FNLJoin(pop);
}



//---------------------------------------------------------------------------
//	@function:
//		CUtils::FPhysicalScan
//
//	@doc:
//		Check if a given operator is a physical agg
//
//---------------------------------------------------------------------------
BOOL
CUtils::FPhysicalScan
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	CPhysicalScan *popScan = NULL;
	if (pop->FPhysical())
	{
		// attempt casting to physical scan,
		// dynamic cast returns NULL if operator is not a scan
		popScan = dynamic_cast<CPhysicalScan*>(pop);
	}

	return (NULL != popScan);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FPhysicalAgg
//
//	@doc:
//		Check if a given operator is a physical agg
//
//---------------------------------------------------------------------------
BOOL
CUtils::FPhysicalAgg
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	CPhysicalAgg *popAgg = NULL;
	if (pop->FPhysical())
	{
		// attempt casting to physical agg,
		// dynamic cast returns NULL if operator is not an agg
		popAgg = dynamic_cast<CPhysicalAgg*>(pop);
	}

	return (NULL != popAgg);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FPhysicalMotion
//
//	@doc:
//		Check if a given operator is a physical motion
//
//---------------------------------------------------------------------------
BOOL
CUtils::FPhysicalMotion
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	CPhysicalMotion *popMotion = NULL;
	if (pop->FPhysical())
	{
		// attempt casting to physical motion,
		// dynamic cast returns NULL if operator is not a motion
		popMotion = dynamic_cast<CPhysicalMotion*>(pop);
	}

	return (NULL != popMotion);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FEnforcer
//
//	@doc:
//		Check if a given operator is an FEnforcer
//
//---------------------------------------------------------------------------
BOOL
CUtils::FEnforcer
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	COperator::EOperatorId eopid = pop->Eopid();
	return
		COperator::EopPhysicalSort == eopid ||
		COperator::EopPhysicalSpool == eopid ||
		COperator::EopPhysicalPartitionSelector == eopid ||
		FPhysicalMotion(pop);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FApply
//
//	@doc:
//		Check if a given operator is an Apply
//
//---------------------------------------------------------------------------
BOOL
CUtils::FApply
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	CLogicalApply *popApply = NULL;
	if (pop->FLogical())
	{
		// attempt casting to logical apply,
		// dynamic cast returns NULL if operator is not an Apply operator
		popApply = dynamic_cast<CLogicalApply*>(pop);
	}

	return (NULL != popApply);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FCorrelatedApply
//
//	@doc:
//		Check if a given operator is a correlated Apply
//
//---------------------------------------------------------------------------
BOOL
CUtils::FCorrelatedApply
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	BOOL fCorrelatedApply = false;
	if (FApply(pop))
	{
		fCorrelatedApply = CLogicalApply::PopConvert(pop)->FCorrelated();
	}

	return  fCorrelatedApply;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FLeftSemiApply
//
//	@doc:
//		Check if a given operator is left semi apply
//
//---------------------------------------------------------------------------
BOOL
CUtils::FLeftSemiApply
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	BOOL fLeftSemiApply = false;
	if (FApply(pop))
	{
		fLeftSemiApply = CLogicalApply::PopConvert(pop)->FLeftSemiApply();
	}

	return fLeftSemiApply;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FLeftAntiSemiApply
//
//	@doc:
//		Check if a given operator is left anti semi apply
//
//---------------------------------------------------------------------------
BOOL
CUtils::FLeftAntiSemiApply
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	BOOL fLeftAntiSemiApply = false;
	if (FApply(pop))
	{
		fLeftAntiSemiApply = CLogicalApply::PopConvert(pop)->FLeftAntiSemiApply();
	}

	return fLeftAntiSemiApply;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FSubquery
//
//	@doc:
//		Check if a given operator is a subquery
//
//---------------------------------------------------------------------------
BOOL
CUtils::FSubquery
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	COperator::EOperatorId eopid = pop->Eopid();
	return pop->FScalar() &&
			(COperator::EopScalarSubquery ==  eopid ||
			COperator::EopScalarSubqueryExists == eopid ||
			COperator::EopScalarSubqueryNotExists == eopid ||
			COperator::EopScalarSubqueryAll == eopid ||
			COperator::EopScalarSubqueryAny == eopid);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FExistentialSubquery
//
//	@doc:
//		Check if a given operator is existential subquery
//
//---------------------------------------------------------------------------
BOOL
CUtils::FExistentialSubquery
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	COperator::EOperatorId eopid = pop->Eopid();
	return pop->FScalar() &&
			(COperator::EopScalarSubqueryExists == eopid ||
			COperator::EopScalarSubqueryNotExists == eopid);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FQuantifiedSubquery
//
//	@doc:
//		Check if a given operator is quantified subquery
//
//---------------------------------------------------------------------------
BOOL
CUtils::FQuantifiedSubquery
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);

	COperator::EOperatorId eopid = pop->Eopid();
	return pop->FScalar() &&
			(COperator::EopScalarSubqueryAll == eopid ||
			COperator::EopScalarSubqueryAny == eopid);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FProjectConstTableWithOneScalarSubq
//
//	@doc:
//		Check if given expression is a Project on ConstTable with one
//		scalar subquery in Project List
//
//---------------------------------------------------------------------------
BOOL
CUtils::FProjectConstTableWithOneScalarSubq
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	if (COperator::EopLogicalProject == pexpr->Pop()->Eopid() &&
		COperator::EopLogicalConstTableGet == (*pexpr)[0]->Pop()->Eopid())
	{
		CExpression *pexprScalar = (*pexpr)[1];
		GPOS_ASSERT(COperator::EopScalarProjectList == pexprScalar->Pop()->Eopid());

		if (1 == pexprScalar->UlArity() && FProjElemWithScalarSubq((*pexprScalar)[0]))
		{
			return true;
		}
	}

	return false;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FProjElemWithScalarSubq
//
//	@doc:
//		Check if given expression is a Project Element with scalar subquery
//
//---------------------------------------------------------------------------
BOOL
CUtils::FProjElemWithScalarSubq
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	return (COperator::EopScalarProjectElement == pexpr->Pop()->Eopid() &&
		COperator::EopScalarSubquery == (*pexpr)[0]->Pop()->Eopid());
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FHasZeroOffset
//
//	@doc:
//		Check if a limit expression has 0 offset
//
//---------------------------------------------------------------------------
BOOL
CUtils::FHasZeroOffset
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(COperator::EopLogicalLimit == pexpr->Pop()->Eopid() ||
			COperator::EopPhysicalLimit == pexpr->Pop()->Eopid());

	return FScalarConstIntZero((*pexpr)[1]);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FScalarConstZero
//
//	@doc:
//		Check if an expression is a 0 integer
//
//---------------------------------------------------------------------------
BOOL
CUtils::FScalarConstIntZero
	(
	CExpression *pexprOffset
	)
{
	if (COperator::EopScalarConst != pexprOffset->Pop()->Eopid())
	{
		return false;
	}

	CScalarConst *popScalarConst = CScalarConst::PopConvert(pexprOffset->Pop());
	IDatum *pdatum = popScalarConst->Pdatum();

	switch (pdatum->Eti())
	{
		case IMDType::EtiInt2:
			return (0 == dynamic_cast<IDatumInt2 *>(pdatum)->SValue());
		case IMDType::EtiInt4:
			return (0 == dynamic_cast<IDatumInt4 *>(pdatum)->IValue());
		case IMDType::EtiInt8:
			return (0 == dynamic_cast<IDatumInt8 *>(pdatum)->LValue());
		default:
			return false;
	}
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpexprDedup
//
//	@doc:
//		Deduplicate an array of expressions
//
//---------------------------------------------------------------------------
DrgPexpr *
CUtils::PdrgpexprDedup
	(
	IMemoryPool *pmp,
	DrgPexpr *pdrgpexpr
	)
{
	const ULONG ulSize = pdrgpexpr->UlLength();
	DrgPexpr *pdrgpexprDedup = GPOS_NEW(pmp) DrgPexpr(pmp);
	for (ULONG ulOuter = 0; ulOuter < ulSize; ulOuter++)
	{
		CExpression *pexprOne = (*pdrgpexpr)[ulOuter];
		BOOL fDuplicate = false;
		for (ULONG ulInner = ulOuter + 1; !fDuplicate && ulInner < ulSize; ulInner++)
		{
			CExpression *pexprTwo = (*pdrgpexpr)[ulInner];
			if (FEqual(pexprOne, pexprTwo))
			{
				fDuplicate = true;
			}
		}

		if (!fDuplicate)
		{
			pexprOne->AddRef();
			pdrgpexprDedup->Append(pexprOne);
		}
	}

	return pdrgpexprDedup;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FEqual
//
//	@doc:
//		Deep equality of expression arrays
//
//---------------------------------------------------------------------------
BOOL
CUtils::FEqual
	(
	const DrgPexpr *pdrgpexprLeft,
	const DrgPexpr *pdrgpexprRight
	)
{
	GPOS_CHECK_STACK_SIZE;

	// NULL arrays are equal
	if (NULL == pdrgpexprLeft || NULL == pdrgpexprRight)
	{
		return NULL == pdrgpexprLeft && NULL == pdrgpexprRight;
	}

	// start with pointers comparison
	if (pdrgpexprLeft == pdrgpexprRight)
	{
		return true;
	}

	const ULONG ulLen = pdrgpexprLeft->UlLength();
	BOOL fEqual = (ulLen == pdrgpexprRight->UlLength());

	for (ULONG ul = 0; ul < ulLen && fEqual; ul++)
	{
		const CExpression *pexprLeft = (*pdrgpexprLeft)[ul];
		const CExpression *pexprRight = (*pdrgpexprRight)[ul];
		fEqual = FEqual(pexprLeft, pexprRight);
	}

	return fEqual;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FEqual
//
//	@doc:
//		Deep equality of expression trees
//
//---------------------------------------------------------------------------
BOOL
CUtils::FEqual
	(
	const CExpression *pexprLeft,
	const CExpression *pexprRight
	)
{
	GPOS_CHECK_STACK_SIZE;

	// NULL expressions are equal
	if (NULL == pexprLeft || NULL == pexprRight)
	{
		return NULL == pexprLeft && NULL == pexprRight;
	}

	// start with pointers comparison
	if (pexprLeft == pexprRight)
	{
		return true;
	}

	// compare number of children and root operators
	if (pexprLeft->UlArity() != pexprRight->UlArity() ||
		!pexprLeft->Pop()->FMatch(pexprRight->Pop())	)
	{
		return false;
	}

	if (0 < pexprLeft->UlArity() && pexprLeft->Pop()->FInputOrderSensitive())
	{
		return FMatchChildrenOrdered(pexprLeft, pexprRight);
	}

	return FMatchChildrenUnordered(pexprLeft, pexprRight);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FMatchChildrenUnordered
//
//	@doc:
//		Check if two expressions have the same children in any order
//
//---------------------------------------------------------------------------
BOOL
CUtils::FMatchChildrenUnordered
	(
	const CExpression *pexprLeft,
	const CExpression *pexprRight
	)
{
	BOOL fEqual = true;
	const ULONG ulArity = pexprLeft->UlArity();
	GPOS_ASSERT(pexprRight->UlArity() == ulArity);

	for (ULONG ul = 0; fEqual && ul < ulArity; ul++)
	{
		CExpression *pexpr = (*pexprLeft)[ul];
		fEqual = (UlOccurrences(pexpr, pexprLeft->PdrgPexpr()) == UlOccurrences(pexpr, pexprRight->PdrgPexpr()));
	}

	return fEqual;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FMatchChildrenOrdered
//
//	@doc:
//		Check if two expressions have the same children in the same order
//
//---------------------------------------------------------------------------
BOOL
CUtils::FMatchChildrenOrdered
	(
	const CExpression *pexprLeft,
	const CExpression *pexprRight
	)
{
	BOOL fEqual = true;
	const ULONG ulArity = pexprLeft->UlArity();
	GPOS_ASSERT(pexprRight->UlArity() == ulArity);

	for (ULONG ul = 0; fEqual && ul < ulArity; ul++)
	{
		// child must be at the same position in the other expression
		fEqual = FEqual((*pexprLeft)[ul], (*pexprRight)[ul]);
	}

	return fEqual;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::UlOccurrences
//
//	@doc:
//		Return the number of occurrences of the given expression in the given
//		array of expressions
//
//---------------------------------------------------------------------------
ULONG
CUtils::UlOccurrences
	(
	const CExpression *pexpr,
	DrgPexpr *pdrgpexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);
	ULONG ulCount = 0;

	const ULONG ulSize = pdrgpexpr->UlLength();
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		if (FEqual(pexpr, (*pdrgpexpr)[ul]))
		{
			ulCount++;
		}
	}

	return ulCount;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FEqualAny
//
//	@doc:
//		Compare expression against an array of expressions
//
//---------------------------------------------------------------------------
BOOL
CUtils::FEqualAny
	(
	const CExpression *pexpr,
	const DrgPexpr *pdrgpexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	const ULONG ulSize = pdrgpexpr->UlLength();
	BOOL fEqual = false;
	for (ULONG ul = 0; !fEqual && ul < ulSize; ul++)
	{
		fEqual = FEqual(pexpr, (*pdrgpexpr)[ul]);
	}

	return fEqual;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FContains
//
//	@doc:
//		Check if first expression array contains all expressions in
//		second array
//
//---------------------------------------------------------------------------
BOOL
CUtils::FContains
	(
	const DrgPexpr *pdrgpexprFst,
	const DrgPexpr *pdrgpexprSnd
	)
{
	GPOS_ASSERT(NULL != pdrgpexprFst);
	GPOS_ASSERT(NULL != pdrgpexprSnd);

	if (pdrgpexprFst == pdrgpexprSnd)
	{
		return true;
	}

	if (pdrgpexprFst->UlLength() < pdrgpexprSnd->UlLength())
	{
		return false;
	}

	const ULONG ulSize = pdrgpexprSnd->UlLength();
	BOOL fContains = true;
	for (ULONG ul = 0; fContains && ul < ulSize; ul++)
	{
		fContains = FEqualAny((*pdrgpexprSnd)[ul], pdrgpexprFst);
	}

	return fContains;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprNegate
//
//	@doc:
//		Generate a Not expression on top of the given expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprNegate
	(
	IMemoryPool *pmp,
	CExpression *pexpr
	)
{
	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	pdrgpexpr->Append(pexpr);

	return PexprScalarBoolOp(pmp, CScalarBoolOp::EboolopNot, pdrgpexpr);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarOp
//
//	@doc:
//		Generate a ScalarOp expression over a column and an expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarOp
	(
	IMemoryPool *pmp,
	const CColRef *pcrLeft,
	CExpression *pexprRight,
	const CWStringConst strOp,
	IMDId *pmdidOp,
	IMDId *pmdidReturnType
	)
{
	GPOS_ASSERT(NULL != pcrLeft);
	GPOS_ASSERT(NULL != pexprRight);

	return GPOS_NEW(pmp) CExpression
			(
			pmp,
			GPOS_NEW(pmp) CScalarOp(pmp, pmdidOp, pmdidReturnType, GPOS_NEW(pmp) CWStringConst(pmp, strOp.Wsz())),
			PexprScalarIdent(pmp, pcrLeft),
			pexprRight
			);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarBoolOp
//
//	@doc:
//		Generate a ScalarBoolOp expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarBoolOp
	(
	IMemoryPool *pmp,
	CScalarBoolOp::EBoolOperator eboolop,
	DrgPexpr *pdrgpexpr
	)
{
	GPOS_ASSERT(NULL != pdrgpexpr);
	GPOS_ASSERT(0 < pdrgpexpr->UlLength());

	return GPOS_NEW(pmp) CExpression
			(
			pmp,
			GPOS_NEW(pmp) CScalarBoolOp(pmp, eboolop),
			pdrgpexpr
			);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarConstBool
//
//	@doc:
//		Generate a boolean scalar constant expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarConstBool
	(
	IMemoryPool *pmp,
	BOOL fval,
	BOOL fNull
	)
{
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();
	
	// create a BOOL datum
	const IMDTypeBool *pmdtypebool = pmda->PtMDType<IMDTypeBool>();
	IDatumBool *pdatum =  pmdtypebool->PdatumBool(pmp, fval, fNull);
	
	CExpression *pexpr =
			GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CScalarConst(pmp,(IDatum*) pdatum));

	return pexpr;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarConstInt4
//
//	@doc:
//		Generate an int4 scalar constant expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarConstInt4
	(
	IMemoryPool *pmp,
	INT iVal
	)
{
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();
	
	// create a int4 datum
	const IMDTypeInt4 *pmdtypeint4 = pmda->PtMDType<IMDTypeInt4>();
	IDatumInt4 *pdatum =  pmdtypeint4->PdatumInt4(pmp, iVal, false);
	
	CExpression *pexpr =
			GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CScalarConst(pmp, (IDatum*) pdatum));

	return pexpr;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarConstInt8
//
//	@doc:
//		Generate an int8 scalar constant expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarConstInt8
	(
	IMemoryPool *pmp,
	LINT lVal,
	BOOL fNull
	)
{
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();

	// create a int8 datum
	const IMDTypeInt8 *pmdtypeint8 = pmda->PtMDType<IMDTypeInt8>();
	IDatumInt8 *pdatum =  pmdtypeint8->PdatumInt8(pmp, lVal, fNull);

	CExpression *pexpr =
			GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CScalarConst(pmp, (IDatum*) pdatum));

	return pexpr;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarConstOid
//
//	@doc:
//		Generate an oid scalar constant expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarConstOid
	(
	IMemoryPool *pmp,
	OID oidVal
	)
{
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();

	// create a oid datum
	const IMDTypeOid *pmdtype = pmda->PtMDType<IMDTypeOid>();
	IDatumOid *pdatum =  pmdtype->PdatumOid(pmp, oidVal, false /*fNull*/);

	CExpression *pexpr =
			GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CScalarConst(pmp, (IDatum*) pdatum));

	return pexpr;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PcrFromProjElem
//
//	@doc:
//		Get column reference defined by project element
//
//---------------------------------------------------------------------------
CColRef *
CUtils::PcrFromProjElem
	(
	CExpression *pexprPrEl
	)
{
	return (CScalarProjectElement::PopConvert(pexprPrEl->Pop()))->Pcr();
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PopAggFunc
//
//	@doc:
//		Generate an aggregate function operator
//
//---------------------------------------------------------------------------
CScalarAggFunc *
CUtils::PopAggFunc
	(
	IMemoryPool *pmp,
	IMDId *pmdidAggFunc,
	const CWStringConst *pstrAggFunc,
	BOOL fDistinct,
	EAggfuncStage eaggfuncstage,
	BOOL fSplit,
	IMDId *pmdidResolvedReturnType // return type to be used if original return type is ambiguous
	)
{
	GPOS_ASSERT(NULL != pmdidAggFunc);
	GPOS_ASSERT(NULL != pstrAggFunc);
	GPOS_ASSERT_IMP(NULL != pmdidResolvedReturnType, pmdidResolvedReturnType->FValid());

	return GPOS_NEW(pmp) CScalarAggFunc(pmp, pmdidAggFunc, pmdidResolvedReturnType, pstrAggFunc, fDistinct, eaggfuncstage, fSplit);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprAggFunc
//
//	@doc:
//		Generate an aggregate function
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprAggFunc
	(
	IMemoryPool *pmp,
	IMDId *pmdidAggFunc,
	const CWStringConst *pstrAggFunc,
	const CColRef *pcr,
	BOOL fDistinct,
	EAggfuncStage eaggfuncstage,
	BOOL fSplit
	)
{
	GPOS_ASSERT(NULL != pstrAggFunc);
	GPOS_ASSERT(NULL != pcr);

	// generate aggregate function
	CScalarAggFunc *popScAggFunc = PopAggFunc(pmp, pmdidAggFunc, pstrAggFunc, fDistinct, eaggfuncstage, fSplit);

	// generate function arguments
	CExpression *pexprScalarIdent = PexprScalarIdent(pmp, pcr);
	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	pdrgpexpr->Append(pexprScalarIdent);

	return GPOS_NEW(pmp) CExpression(pmp, popScAggFunc, pdrgpexpr);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarFunc
//
//	@doc:
//		Construct a scalar function
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarFunc
	(
	IMemoryPool *pmp,
	IMDId *pmdidFunc,
	IMDId *pmdidRetType,
	const CWStringConst *pstrFuncName, 
	DrgPexpr *pdrgpexpr
	)
{
	GPOS_ASSERT(NULL != pstrFuncName);

	// construct function
	CScalarFunc *popScFunc = GPOS_NEW(pmp) CScalarFunc(pmp, pmdidFunc, pmdidRetType, pstrFuncName);

	return GPOS_NEW(pmp) CExpression(pmp, popScFunc, pdrgpexpr);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprCountStar
//
//	@doc:
//		generate a count(*) expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprCountStar
	(
	IMemoryPool *pmp
	)
{
	// TODO,  04/26/2012, create count(*) expressions in a system-independent
	// way using MDAccessor

	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	CMDIdGPDB *pmdid = GPOS_NEW(pmp) CMDIdGPDB(GPDB_COUNT_STAR);
	CWStringConst *pstr = GPOS_NEW(pmp) CWStringConst(GPOS_WSZ_LIT("count"));

	CScalarAggFunc *popScAggFunc = PopAggFunc(pmp, pmdid, pstr, false /*fDistinct*/, EaggfuncstageGlobal /*eaggfuncstage*/, false /*fSplit*/);

	CExpression *pexprCountStar = GPOS_NEW(pmp) CExpression(pmp, popScAggFunc, pdrgpexpr);

	return pexprCountStar;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprCountStar
//
//	@doc:
//		Generate a GbAgg with count(*) function over the given expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprCountStar
	(
	IMemoryPool *pmp,
	CExpression *pexprLogical
	)
{
	CColumnFactory *pcf = COptCtxt::PoctxtFromTLS()->Pcf();
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();

	// generate a count(*) expression
	CExpression *pexprCountStar = PexprCountStar(pmp);

	// generate a computed column with count(*) type
	IMDId *pmdidType = CScalarAggFunc::PopConvert(pexprCountStar->Pop())->PmdidType();
	const IMDType *pmdtype = pmda->Pmdtype(pmdidType);
	CColRef *pcrComputed = pcf->PcrCreate(pmdtype);
	CExpression *pexprPrjElem = PexprScalarProjectElement(pmp, pcrComputed, pexprCountStar);
	CExpression *pexprPrjList = GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CScalarProjectList(pmp), pexprPrjElem);

	// generate a Gb expression
	DrgPcr *pdrgpcr = GPOS_NEW(pmp) DrgPcr(pmp);
	return PexprLogicalGbAggGlobal(pmp, pdrgpcr, pexprLogical, pexprPrjList);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprCountStarAndSum
//
//	@doc:
//		Generate a GbAgg with count(*) and sum(col) over the given expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprCountStarAndSum
	(
	IMemoryPool *pmp,
	const CColRef *pcr,
	CExpression *pexprLogical
	)
{
	GPOS_ASSERT(CDrvdPropRelational::Pdprel(pexprLogical->PdpDerive())->PcrsOutput()->FMember(pcr));

	CColumnFactory *pcf = COptCtxt::PoctxtFromTLS()->Pcf();
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();

	// generate a count(*) expression
	CExpression *pexprCountStar = PexprCountStar(pmp);

	// generate a computed column with count(*) type
	IMDId *pmdidType = CScalarAggFunc::PopConvert(pexprCountStar->Pop())->PmdidType();
	const IMDType *pmdtype = pmda->Pmdtype(pmdidType);
	CColRef *pcrComputed = pcf->PcrCreate(pmdtype);
	CExpression *pexprPrjElemCount = PexprScalarProjectElement(pmp, pcrComputed, pexprCountStar);

	// generate sum(col) expression
	CExpression *pexprSum = PexprSum(pmp, pcr);
	const IMDType *pmdtypeSum = pmda->Pmdtype(CScalarAggFunc::PopConvert(pexprSum->Pop())->PmdidType());
	CColRef *pcrSum = pcf->PcrCreate(pmdtypeSum);
	CExpression *pexprPrjElemSum = PexprScalarProjectElement(pmp, pcrSum, pexprSum);
	CExpression *pexprPrjList = GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CScalarProjectList(pmp), pexprPrjElemCount, pexprPrjElemSum);

	// generate a Gb expression
	DrgPcr *pdrgpcr = GPOS_NEW(pmp) DrgPcr(pmp);
	return PexprLogicalGbAggGlobal(pmp, pdrgpcr, pexprLogical, pexprPrjList);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FCountAggProjElem
//
//	@doc:
//		Return True if passed expression is a Project Element defined on
//		count(*)/count(Any) agg
//
//---------------------------------------------------------------------------
BOOL
CUtils::FCountAggProjElem
	(
	CExpression *pexprPrjElem,
	CColRef **ppcrCount // output: count(*)/count(Any) column
	)
{
	GPOS_ASSERT(NULL != pexprPrjElem);
	GPOS_ASSERT(NULL != ppcrCount);

	COperator *pop = pexprPrjElem->Pop();
	if (COperator::EopScalarProjectElement != pop->Eopid())
	{
		return false;
	}

	if (COperator::EopScalarAggFunc == (*pexprPrjElem)[0]->Pop()->Eopid())
	{
		CScalarAggFunc *popAggFunc = CScalarAggFunc::PopConvert((*pexprPrjElem)[0]->Pop());
		if (popAggFunc->FCountStar() || popAggFunc->FCountAny())
		{
			*ppcrCount = CScalarProjectElement::PopConvert(pop)->Pcr();
			return true;
		}
	}

	return FHasCountAgg((*pexprPrjElem)[0], ppcrCount);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FHasCountAgg
//
//	@doc:
//		Check if the given expression has a count(*)/count(Any) agg,
//		return the top-most found count column
//
//---------------------------------------------------------------------------
BOOL
CUtils::FHasCountAgg
	(
	CExpression *pexpr,
	CColRef **ppcrCount // output: count(*)/count(Any) column
	)
{
	GPOS_CHECK_STACK_SIZE;
	GPOS_ASSERT(NULL != ppcrCount);

	if (COperator::EopScalarProjectElement == pexpr->Pop()->Eopid())
	{
		// base case, count(*)/count(Any) must appear below a project element
		return FCountAggProjElem(pexpr, ppcrCount);
	}

	// recursively process children
	BOOL fHasCountAgg = false;
	const ULONG ulArity = pexpr->UlArity();
	for (ULONG ul = 0; !fHasCountAgg && ul < ulArity; ul++)
	{
		fHasCountAgg = FHasCountAgg((*pexpr)[ul], ppcrCount);
	}

	return fHasCountAgg;
}

static BOOL FCountAggMatchingColumn(CExpression* pexprPrjElem, const CColRef* pcr)
{
    CColRef* pcrCount = NULL;
    return CUtils::FCountAggProjElem(pexprPrjElem, &pcrCount) && pcr == pcrCount;
}


BOOL
CUtils::FHasCountAggMatchingColumn
(
 const CExpression *pexpr,
 const CColRef *pcr,
 const CLogicalGbAgg **ppgbAgg
 )
{
    COperator* pop = pexpr->Pop();
    // base case, we have a logical agg operator
    if (COperator::EopLogicalGbAgg == pop->Eopid())
    {
        const CExpression* const pexprProjectList = (*pexpr)[1];
        GPOS_ASSERT(COperator::EopScalarProjectList == pexprProjectList->Pop()->Eopid());
        const ULONG ulArity = pexprProjectList->UlArity();
        for (ULONG ul = 0; ul < ulArity; ul++)
        {
            CExpression* const pexprPrjElem = (*pexprProjectList)[ul];
            if (FCountAggMatchingColumn(pexprPrjElem, pcr))
            {
                const CLogicalGbAgg* pgbAgg = CLogicalGbAgg::PopConvert(pop);
                *ppgbAgg = pgbAgg;
                return true;
            }
        }
    }
    // recurse
    else
    {
        const ULONG ulArity = pexpr->UlArity();
        for (ULONG ul = 0; ul < ulArity; ul++)
        {
            const CExpression* pexprChild = (*pexpr)[ul];
            if (FHasCountAggMatchingColumn(pexprChild, pcr, ppgbAgg))
            {
                return true;
            }
        }
    }
    return false;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprSum
//
//	@doc:
//		generate a sum(col) expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprSum
	(
	IMemoryPool *pmp,
	const CColRef *pcr
	)
{
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();

	return PexprAgg(pmp, pmda, IMDType::EaggSum, pcr, false /*fDistinct*/);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprGbAggSum
//
//	@doc:
//		Generate a GbAgg with sum(col) expressions for all columns in the
//		passed array
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprGbAggSum
	(
	IMemoryPool *pmp,
	CExpression *pexprLogical,
	DrgPcr *pdrgpcrSum
	)
{
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();
	CColumnFactory *pcf = COptCtxt::PoctxtFromTLS()->Pcf();

	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	const ULONG ulSize = pdrgpcrSum->UlLength();
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		CColRef *pcr = (*pdrgpcrSum)[ul];
		CExpression *pexprSum = PexprSum(pmp, pcr);
		const IMDType *pmdtypeSum = pmda->Pmdtype(CScalarAggFunc::PopConvert(pexprSum->Pop())->PmdidType());
		CColRef *pcrSum = pcf->PcrCreate(pmdtypeSum);
		CExpression *pexprPrjElemSum = PexprScalarProjectElement(pmp, pcrSum, pexprSum);
		pdrgpexpr->Append(pexprPrjElemSum);
	}

	CExpression *pexprPrjList = GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CScalarProjectList(pmp), pdrgpexpr);

	// generate a Gb expression
	DrgPcr *pdrgpcr = GPOS_NEW(pmp) DrgPcr(pmp);
	return PexprLogicalGbAggGlobal(pmp, pdrgpcr, pexprLogical, pexprPrjList);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprCount
//
//	@doc:
//		Generate a count(<distinct> col) expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprCount
	(
	IMemoryPool *pmp,
	const CColRef *pcr,
	BOOL fDistinct
	)
{
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();

	return PexprAgg(pmp, pmda, IMDType::EaggCount, pcr, fDistinct);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprMin
//
//	@doc:
//		Generate a min(col) expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprMin
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	const CColRef *pcr
	)
{
 	return PexprAgg(pmp, pmda, IMDType::EaggMin, pcr, false /*fDistinct*/);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprMin
//
//	@doc:
//		Generate an aggregate expression of the specified type
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprAgg
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	IMDType::EAggType eagg,
	const CColRef *pcr,
	BOOL fDistinct
	)
{
	GPOS_ASSERT(IMDType::EaggGeneric >eagg);
	GPOS_ASSERT(pcr->Pmdtype()->PmdidAgg(eagg)->FValid());
	
	const IMDAggregate *pmdagg = pmda->Pmdagg(pcr->Pmdtype()->PmdidAgg(eagg));
	
	IMDId *pmdidAgg = pmdagg->Pmdid();
	pmdidAgg->AddRef();
	CWStringConst *pstr = GPOS_NEW(pmp) CWStringConst(pmp, pmdagg->Mdname().Pstr()->Wsz());

	return PexprAggFunc(pmp, pmdidAgg, pstr, pcr, fDistinct, EaggfuncstageGlobal /*fGlobal*/, false /*fSplit*/);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprLogicalSelect
//
//	@doc:
//		Generate a select expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprLogicalSelect
	(
	IMemoryPool *pmp,
	CExpression *pexpr,
	CExpression *pexprPredicate
	)
{
	GPOS_ASSERT(NULL != pexpr);
	GPOS_ASSERT(NULL != pexprPredicate);

	return GPOS_NEW(pmp) CExpression
						(
						pmp,
						GPOS_NEW(pmp) CLogicalSelect(pmp),
						pexpr,
						pexprPredicate
						);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprSafeSelect
//
//	@doc:
//		If predicate is True return logical expression, otherwise return
//		a new select node
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprSafeSelect
	(
	IMemoryPool *pmp,
	CExpression *pexprLogical,
	CExpression *pexprPred
	)
{
	GPOS_ASSERT(NULL != pexprLogical);
	GPOS_ASSERT(NULL != pexprPred);

	if (FScalarConstTrue(pexprPred))
	{
		// caller must have add-refed the predicate before coming here
		pexprPred->Release();
		return pexprLogical;
	}

	return PexprLogicalSelect(pmp, pexprLogical, pexprPred);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprCollapseSelect
//
//	@doc:
//		Generate a select expression, if child is another Select expression
//		collapse both Selects into one expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprCollapseSelect
	(
	IMemoryPool *pmp,
	CExpression *pexpr,
	CExpression *pexprPredicate
	)
{
	GPOS_ASSERT(NULL != pexpr);
	GPOS_ASSERT(NULL != pexprPredicate);

	if
		(
		COperator::EopLogicalSelect == pexpr->Pop()->Eopid() &&
		2 == pexpr->UlArity() // perform collapsing only when we have a full Select tree
		)
	{
		// collapse Selects into one expression
		(*pexpr)[0]->AddRef();
		CExpression *pexprChild = (*pexpr)[0];
		CExpression *pexprScalar = CPredicateUtils::PexprConjunction(pmp, pexprPredicate, (*pexpr)[1]);

		return GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CLogicalSelect(pmp), pexprChild, pexprScalar);
	}

	pexpr->AddRef();
	pexprPredicate->AddRef();

	return GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CLogicalSelect(pmp), pexpr, pexprPredicate);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprLogicalProject
//
//	@doc:
//		Generate a project expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprLogicalProject
	(
	IMemoryPool *pmp,
	CExpression *pexpr,
	CExpression *pexprPrjList,
	BOOL fNewComputedCol
	)
{
	GPOS_ASSERT(NULL != pexpr);
	GPOS_ASSERT(NULL != pexprPrjList);
	GPOS_ASSERT(COperator::EopScalarProjectList == pexprPrjList->Pop()->Eopid());

	if (fNewComputedCol)
	{
		CColumnFactory *pcf = COptCtxt::PoctxtFromTLS()->Pcf();
		const ULONG ulArity = pexprPrjList->UlArity();
		for (ULONG ul = 0; ul < ulArity; ul++)
		{
			CExpression *pexprPrEl = (*pexprPrjList)[ul];
			pcf->AddComputedToUsedColsMap(pexprPrEl);
		}
	}
	return GPOS_NEW(pmp) CExpression
						(
						pmp,
						GPOS_NEW(pmp) CLogicalProject(pmp),
						pexpr,
						pexprPrjList
						);
}



//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprLogicalSequenceProject
//
//	@doc:
//		Generate a sequence project expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprLogicalSequenceProject
	(
	IMemoryPool *pmp,
	CDistributionSpec *pds,
	DrgPos *pdrgpos,
	DrgPwf *pdrgpwf,
	CExpression *pexpr,
	CExpression *pexprPrjList
	)
{
	GPOS_ASSERT(NULL != pds);
	GPOS_ASSERT(NULL != pdrgpos);
	GPOS_ASSERT(NULL != pdrgpwf);
	GPOS_ASSERT(pdrgpwf->UlLength() == pdrgpos->UlLength());
	GPOS_ASSERT(NULL != pexpr);
	GPOS_ASSERT(NULL != pexprPrjList);
	GPOS_ASSERT(COperator::EopScalarProjectList == pexprPrjList->Pop()->Eopid());

	return GPOS_NEW(pmp) CExpression
			(
			pmp,
			GPOS_NEW(pmp) CLogicalSequenceProject(pmp, pds, pdrgpos, pdrgpwf),
			pexpr,
			pexprPrjList
			);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprLogicalProjectNulls
//
//	@doc:
//		Construct a projection of NULL constants using the given column
//		names and types on top of the given expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprLogicalProjectNulls
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr,
	CExpression *pexpr,
	HMUlCr *phmulcr
	)
{
	DrgPdatum *pdrgpdatum = CTranslatorExprToDXLUtils::PdrgpdatumNulls(pmp, pdrgpcr);
	CExpression *pexprProjList = PexprScalarProjListConst(pmp, pdrgpcr, pdrgpdatum, phmulcr);
	pdrgpdatum->Release();

	return PexprLogicalProject(pmp, pexpr, pexprProjList, false /*fNewComputedCol*/);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprScalarProjListConst
//
//	@doc:
//		Construct a project list using the column names and types of the given
//		array of column references, and the given datums
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprScalarProjListConst
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr,
	DrgPdatum *pdrgpdatum,
	HMUlCr *phmulcr
	)
{
	GPOS_ASSERT(pdrgpcr->UlLength() == pdrgpdatum->UlLength());

	CColumnFactory *pcf = COptCtxt::PoctxtFromTLS()->Pcf();
	CScalarProjectList *pscprl = GPOS_NEW(pmp) CScalarProjectList(pmp);

	const ULONG ulArity = pdrgpcr->UlLength();
	if (0 == ulArity)
	{
		return GPOS_NEW(pmp) CExpression(pmp, pscprl);
	}

	DrgPexpr *pdrgpexprProjElems = GPOS_NEW(pmp) DrgPexpr(pmp);
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];

		IDatum *pdatum =(*pdrgpdatum)[ul];
		pdatum->AddRef();
		CScalarConst *popScConst = GPOS_NEW(pmp) CScalarConst(pmp, pdatum);
		CExpression *pexprScConst = GPOS_NEW(pmp) CExpression(pmp, popScConst);

		CColRef *pcrNew = pcf->PcrCreate(pcr->Pmdtype(), pcr->Name());
		if (NULL != phmulcr)
		{
#ifdef GPOS_DEBUG
			BOOL fInserted =
#endif
			phmulcr->FInsert(GPOS_NEW(pmp) ULONG(pcr->UlId()), pcrNew);
			GPOS_ASSERT(fInserted);
		}

		CScalarProjectElement *popScPrEl = GPOS_NEW(pmp) CScalarProjectElement(pmp, pcrNew);
		CExpression *pexprScPrEl = GPOS_NEW(pmp) CExpression(pmp, popScPrEl, pexprScConst);

		pdrgpexprProjElems->Append(pexprScPrEl);
	}

	return GPOS_NEW(pmp) CExpression(pmp, pscprl, pdrgpexprProjElems);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprAddProjection
//
//	@doc:
//		Generate a project expression with one additional project element
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprAddProjection
	(
	IMemoryPool *pmp,
	CExpression *pexpr,
	CExpression *pexprProjected
	)
{
	GPOS_ASSERT(pexpr->Pop()->FLogical());
	GPOS_ASSERT(pexprProjected->Pop()->FScalar());

	DrgPexpr *pdrgpexprProjected = GPOS_NEW(pmp) DrgPexpr(pmp);
	pdrgpexprProjected->Append(pexprProjected);

	CExpression *pexprProjection = PexprAddProjection(pmp, pexpr, pdrgpexprProjected);
	pdrgpexprProjected->Release();

	return pexprProjection;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprAddProjection
//
//	@doc:
//		Generate a project expression with one or more additional project elements
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprAddProjection
	(
	IMemoryPool *pmp,
	CExpression *pexpr,
	DrgPexpr *pdrgpexprProjected
	)
{
	GPOS_ASSERT(pexpr->Pop()->FLogical());
	GPOS_ASSERT(NULL != pdrgpexprProjected);

	if (0 == pdrgpexprProjected->UlLength())
	{
		return pexpr;
	}

	CColumnFactory *pcf = COptCtxt::PoctxtFromTLS()->Pcf();
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();

	DrgPexpr *pdrgpexprPrjElem = GPOS_NEW(pmp) DrgPexpr(pmp);

	const ULONG ulProjected = pdrgpexprProjected->UlLength();
	for (ULONG ul = 0; ul < ulProjected; ul++)
	{
		CExpression *pexprProjected = (*pdrgpexprProjected)[ul];
		GPOS_ASSERT(pexprProjected->Pop()->FScalar());

		// generate a computed column with scalar expression type
		IMDId *pmdidType = CScalar::PopConvert(pexprProjected->Pop())->PmdidType();
		const IMDType *pmdtype = pmda->Pmdtype(pmdidType);
		CColRef *pcr = pcf->PcrCreate(pmdtype);

		pexprProjected->AddRef();
		pdrgpexprPrjElem->Append(PexprScalarProjectElement(pmp, pcr, pexprProjected));
	}

	CExpression *pexprPrjList = GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CScalarProjectList(pmp), pdrgpexprPrjElem);

	return PexprLogicalProject(pmp, pexpr, pexprPrjList, true /*fNewComputedCol*/);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprLogicalGbAgg
//
//	@doc:
//		Generate an aggregate expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprLogicalGbAgg
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr,
	CExpression *pexprRelational,
	CExpression *pexprPrL,
	COperator::EGbAggType egbaggtype
	)
{
	GPOS_ASSERT(NULL != pdrgpcr);
	GPOS_ASSERT(NULL != pexprRelational);
	GPOS_ASSERT(NULL != pexprPrL);

	// create a new logical group by operator
	CLogicalGbAgg *pop = GPOS_NEW(pmp) CLogicalGbAgg(pmp, pdrgpcr, egbaggtype);

	return GPOS_NEW(pmp) CExpression(pmp, pop, pexprRelational, pexprPrL);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprLogicalGbAggGlobal
//
//	@doc:
//		Generate a global aggregate expression
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprLogicalGbAggGlobal
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr,
	CExpression *pexprRelational,
	CExpression *pexprProjList
	)
{
	return PexprLogicalGbAgg(pmp, pdrgpcr, pexprRelational, pexprProjList, COperator::EgbaggtypeGlobal);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FHasGlobalAggFunc
//
//	@doc:
//		Check if given project list has a global aggregate function
//
//---------------------------------------------------------------------------
BOOL
CUtils::FHasGlobalAggFunc
	(
	const CExpression *pexprAggProjList
	)
{
	GPOS_ASSERT(COperator::EopScalarProjectList == pexprAggProjList->Pop()->Eopid());
	BOOL fGlobal = false;

	const ULONG ulArity = pexprAggProjList->UlArity();

	for (ULONG ul = 0; ul < ulArity && !fGlobal; ul++)
	{
		CExpression *pexprPrEl = (*pexprAggProjList)[ul];
		GPOS_ASSERT(COperator::EopScalarProjectElement == pexprPrEl->Pop()->Eopid());
		// get the scalar child of the project element
		CExpression *pexprAggFunc = (*pexprPrEl)[0];
		CScalarAggFunc *popScAggFunc = CScalarAggFunc::PopConvert(pexprAggFunc->Pop());
		fGlobal = popScAggFunc->FGlobal();
	}

	return fGlobal;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::Ecmpt
//
//	@doc:
// 		Return the comparison type for the given mdid
//
//---------------------------------------------------------------------------
IMDType::ECmpType
CUtils::Ecmpt
	(
	IMDId *pmdid
	)
{
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();
	const IMDScalarOp *pmdscop = pmda->Pmdscop(pmdid);
	return pmdscop->Ecmpt();
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::Ecmpt
//
//	@doc:
// 		Return the comparison type for the given mdid
//
//---------------------------------------------------------------------------
IMDType::ECmpType
CUtils::Ecmpt
	(
	CMDAccessor *pmda,
	IMDId *pmdid
	)
{
	const IMDScalarOp *pmdscop = pmda->Pmdscop(pmdid);
	return pmdscop->Ecmpt();
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FScalarConstBool
//
//	@doc:
//		Check if the expression is a scalar boolean const
//
//---------------------------------------------------------------------------
BOOL
CUtils::FScalarConstBool
	(
	CExpression *pexpr,
	BOOL fVal
	)
{
	GPOS_ASSERT(NULL != pexpr);

	COperator *pop = pexpr->Pop();
	if (COperator::EopScalarConst == pop->Eopid())
	{
		CScalarConst *popScalarConst = CScalarConst::PopConvert(pop);
		if (IMDType::EtiBool ==  popScalarConst->Pdatum()->Eti())
		{
			IDatumBool *pdatum = dynamic_cast<IDatumBool *>(popScalarConst->Pdatum());
			return !pdatum->FNull() && pdatum->FValue() == fVal;
		}
	}

	return false;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FScalarConstTrue
//
//	@doc:
//		Checks to see if the expression is a scalar const TRUE
//
//---------------------------------------------------------------------------
BOOL
CUtils::FScalarConstTrue
	(
	CExpression *pexpr
	)
{
	return FScalarConstBool(pexpr, true /*fVal*/);
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FScalarConstFalse
//
//	@doc:
//		Checks to see if the expression is a scalar const FALSE
//
//---------------------------------------------------------------------------
BOOL
CUtils::FScalarConstFalse
	(
	CExpression *pexpr
	)
{
	return FScalarConstBool(pexpr, false /*fVal*/);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpcrNonSystemCols
//
//	@doc:
//		Return an array of non-system columns in the given set
//
//---------------------------------------------------------------------------
DrgPcr *
CUtils::PdrgpcrNonSystemCols
	(
	IMemoryPool *pmp,
	CColRefSet *pcrs
	)
{
	GPOS_ASSERT(NULL != pcrs);

	DrgPcr *pdrgpcr = GPOS_NEW(pmp) DrgPcr(pmp);
	CColRefSetIter crsi(*pcrs);
	while (crsi.FAdvance())
	{
		CColRef *pcr = crsi.Pcr();
		if (pcr->FSystemCol())
		{
			continue;
		}
		pdrgpcr->Append(pcr);
	}

	return pdrgpcr;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpcrGroupingKey
//
//	@doc:
//		Create an array of expression's output columns including a key for
//		grouping
//
//---------------------------------------------------------------------------
DrgPcr *
CUtils::PdrgpcrGroupingKey
	(
	IMemoryPool *pmp,
	CExpression *pexpr,
	DrgPcr **ppdrgpcrKey // output: array of key columns contained in the returned grouping columns
	)
{
	GPOS_ASSERT(NULL != pexpr);
	GPOS_ASSERT(NULL != ppdrgpcrKey);

	CKeyCollection *pkc = CDrvdPropRelational::Pdprel(pexpr->PdpDerive())->Pkc();
	GPOS_ASSERT(NULL != pkc);

	CColRefSet *pcrsOutput = CDrvdPropRelational::Pdprel(pexpr->PdpDerive())->PcrsOutput();

	// filter out system columns since they may introduce columns with undefined sort/hash operators
	DrgPcr *pdrgpcr = PdrgpcrNonSystemCols(pmp, pcrsOutput);
	CColRefSet *pcrs = GPOS_NEW(pmp) CColRefSet(pmp, pdrgpcr);

	// prefer extracting a hashable key since Agg operator may redistribute child on grouping columns
	DrgPcr *pdrgpcrKey = pkc->PdrgpcrHashableKey(pmp);
	if (NULL == pdrgpcrKey)
	{
		// if no hashable key, extract any key
		pdrgpcrKey = pkc->PdrgpcrKey(pmp);
	}
	GPOS_ASSERT(NULL != pdrgpcrKey);

	CColRefSet *pcrsKey = GPOS_NEW(pmp) CColRefSet(pmp, pdrgpcrKey);
	pcrs->Union(pcrsKey);

	pdrgpcr->Release();
	pcrsKey->Release();
	pdrgpcr = pcrs->Pdrgpcr(pmp);
	pcrs->Release();

	// set output key array
	*ppdrgpcrKey = pdrgpcrKey;

	return pdrgpcr;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpcrsAddEquivClass
//
//	@doc:
//		Add an equivalence class to the array. If the new equiv class contains
//		columns from separate equiv classes, then these are merged. Returns a new
//		array of equivalence classes
//
//---------------------------------------------------------------------------
DrgPcrs *
CUtils::PdrgpcrsAddEquivClass
	(
	IMemoryPool *pmp,
	CColRefSet *pcrsNew,
	DrgPcrs *pdrgpcrs
	)
{
	DrgPcrs *pdrgpcrsNew = GPOS_NEW(pmp) DrgPcrs(pmp);

	const ULONG ulLen = pdrgpcrs->UlLength();
	for (ULONG ul = 0; ul < ulLen; ul++)
	{
		CColRefSet *pcrs = (*pdrgpcrs)[ul];
		if (pcrsNew->FDisjoint(pcrs))
		{
			pcrs->AddRef();
			pdrgpcrsNew->Append(pcrs);
		}
		else
		{
			pcrsNew->Include(pcrs);
		}
	}

	pdrgpcrsNew->Append(pcrsNew);

	return pdrgpcrsNew;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpcrsMergeEquivClasses
//
//	@doc:
//		Merge 2 arrays of equivalence classes
//
//---------------------------------------------------------------------------
DrgPcrs *
CUtils::PdrgpcrsMergeEquivClasses
	(
	IMemoryPool *pmp,
	DrgPcrs *pdrgpcrsFst,
	DrgPcrs *pdrgpcrsSnd
	)
{
	pdrgpcrsFst->AddRef();
	DrgPcrs *pdrgpcrsMerged = pdrgpcrsFst;

	const ULONG ulLen = pdrgpcrsSnd->UlLength();
	for (ULONG ul = 0; ul < ulLen; ul++)
	{
		CColRefSet *pcrs = (*pdrgpcrsSnd)[ul];
		pcrs->AddRef();

		DrgPcrs *pdrgpcrs = PdrgpcrsAddEquivClass(pmp, pcrs, pdrgpcrsMerged);
		pdrgpcrsMerged->Release();
		pdrgpcrsMerged = pdrgpcrs;
	}

	return pdrgpcrsMerged;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpcrsIntersectEquivClasses
//
//	@doc:
//		Intersect 2 arrays of equivalence classes. This is accomplished by doing
//		pairwise intersection between elements of the two arrays
//
//---------------------------------------------------------------------------
DrgPcrs *
CUtils::PdrgpcrsIntersectEquivClasses
	(
	IMemoryPool *pmp,
	DrgPcrs *pdrgpcrsFst,
	DrgPcrs *pdrgpcrsSnd
	)
{
	DrgPcrs *pdrgpcrs = GPOS_NEW(pmp) DrgPcrs(pmp);

	const ULONG ulLenFst = pdrgpcrsFst->UlLength();
	const ULONG ulLenSnd = pdrgpcrsSnd->UlLength();
	for (ULONG ulFst = 0; ulFst < ulLenFst; ulFst++)
	{
		CColRefSet *pcrsFst = (*pdrgpcrsFst)[ulFst];
		for (ULONG ulSnd = 0; ulSnd < ulLenSnd; ulSnd++)
		{
			CColRefSet *pcrsSnd = (*pdrgpcrsSnd)[ulSnd];
			CColRefSet *pcrs = GPOS_NEW(pmp) CColRefSet(pmp);
			pcrs->Include(pcrsFst);
			pcrs->Intersection(pcrsSnd);
			if (0 == pcrs->CElements())
			{
				pcrs->Release();
				continue;
			}

			pdrgpcrs->Append(pcrs);
		}
	}

	return pdrgpcrs;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpcrsCopyChildEquivClasses
//
//	@doc:
//		Return a copy of equivalence classes from all children
//
//---------------------------------------------------------------------------
DrgPcrs *
CUtils::PdrgpcrsCopyChildEquivClasses
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl
	)
{
	DrgPcrs *pdrgpcrs = GPOS_NEW(pmp) DrgPcrs(pmp);
	const ULONG ulArity = exprhdl.UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		if (!exprhdl.FScalarChild(ul))
		{
			CDrvdPropRelational *pdprel = exprhdl.Pdprel(ul);
			DrgPcrs *pdrgpcrsChild = pdprel->Ppc()->PdrgpcrsEquivClasses();

			DrgPcrs *pdrgpcrsChildCopy = GPOS_NEW(pmp) DrgPcrs(pmp);
			const ULONG ulSize = pdrgpcrsChild->UlLength();
			for (ULONG ulInner = 0; ulInner < ulSize; ulInner++)
			{
				CColRefSet *pcrs = GPOS_NEW(pmp) CColRefSet(pmp, *(*pdrgpcrsChild)[ulInner]);
				pdrgpcrsChildCopy->Append(pcrs);
			}

			DrgPcrs *pdrgpcrsMerged = PdrgpcrsMergeEquivClasses(pmp, pdrgpcrs, pdrgpcrsChildCopy);
			pdrgpcrsChildCopy->Release();
			pdrgpcrs->Release();
			pdrgpcrs = pdrgpcrsMerged;
		}
	}

	return pdrgpcrs;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpcrExcludeColumns
//
//	@doc:
//		Return a copy of the given array of columns, excluding the columns
//		in the given colrefset
//
//---------------------------------------------------------------------------
DrgPcr *
CUtils::PdrgpcrExcludeColumns
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcrOriginal,
	CColRefSet *pcrsExcluded
	)
{
	GPOS_ASSERT(NULL != pdrgpcrOriginal);
	GPOS_ASSERT(NULL != pcrsExcluded);

	DrgPcr *pdrgpcr = GPOS_NEW(pmp) DrgPcr(pmp);
	const ULONG ulCols = pdrgpcrOriginal->UlLength();
	for (ULONG ul = 0; ul < ulCols; ul++)
	{
		CColRef *pcr = (*pdrgpcrOriginal)[ul];
		if (!pcrsExcluded->FMember(pcr))
		{
			pdrgpcr->Append(pcr);
		}
	}

	return pdrgpcr;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::OsPrintDrgPcr
//
//	@doc:
//		Helper function to print a colref array
//
//---------------------------------------------------------------------------
IOstream &
CUtils::OsPrintDrgPcr
	(
	IOstream &os,
	const DrgPcr *pdrgpcr,
	ULONG ulLenMax
	)
{
	ULONG ulLen = pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < std::min(ulLen, ulLenMax); ul++)
	{
		(*pdrgpcr)[ul]->OsPrint(os);
		if (ul < ulLen - 1)
		{
			os << ", ";
		}
	}
	
	if (ulLenMax < ulLen)
	{
		os << "...";
	}

	return os;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FScalarCmp
//
//	@doc:
//		 Check if given expression is a scalar comparison
//
//---------------------------------------------------------------------------
BOOL
CUtils::FScalarCmp
	(
	CExpression *pexpr
	)
{
	return (COperator::EopScalarCmp == pexpr->Pop()->Eopid());
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FScalarArrayCmp
//
//	@doc:
//		 Check if given expression is a scalar array comparison
//
//---------------------------------------------------------------------------
BOOL
CUtils::FScalarArrayCmp
	(
	CExpression *pexpr
	)
{
	return (COperator::EopScalarArrayCmp == pexpr->Pop()->Eopid());
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FHasOneStagePhysicalAgg
//
//	@doc:
//		 Check if given expression has any one stage agg nodes
//
//---------------------------------------------------------------------------
BOOL
CUtils::FHasOneStagePhysicalAgg
	(
	const CExpression *pexpr
	)
{
	GPOS_CHECK_STACK_SIZE;
	GPOS_ASSERT(NULL != pexpr);

	if (FPhysicalAgg(pexpr->Pop()) && !CPhysicalAgg::PopConvert(pexpr->Pop())->FMultiStage())
	{
		return true;
	}

	// recursively check children
	const ULONG ulArity = pexpr->UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		if (FHasOneStagePhysicalAgg((*pexpr)[ul]))
		{
			return true;
		}
	}

	return false;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FOpExists
//
//	@doc:
//		 Check if given operator exists in the given list
//
//---------------------------------------------------------------------------
BOOL
CUtils::FOpExists
	(
	const COperator *pop,
	const COperator::EOperatorId *peopid,
	ULONG ulOps
	)
{
	GPOS_ASSERT(NULL != pop);
	GPOS_ASSERT(NULL != peopid);

	COperator::EOperatorId eopid = pop->Eopid();
	for (ULONG ul = 0; ul < ulOps; ul++)
	{
		if (eopid == peopid[ul])
		{
			return true;
		}
	}

	return false;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FHasOp
//
//	@doc:
//		 Check if given expression has any operator in the given list
//
//---------------------------------------------------------------------------
BOOL
CUtils::FHasOp
	(
	const CExpression *pexpr,
	const COperator::EOperatorId *peopid,
	ULONG ulOps
	)
{
	GPOS_CHECK_STACK_SIZE;
	GPOS_ASSERT(NULL != pexpr);
	GPOS_ASSERT(NULL != peopid);

	if (FOpExists(pexpr->Pop(), peopid, ulOps))
	{
		return true;
	}

	// recursively check children
	const ULONG ulArity = pexpr->UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		if (FHasOp((*pexpr)[ul], peopid, ulOps))
		{
			return true;
		}
	}

	return false;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::UlInlinableCTEs
//
//	@doc:
//		Return number of inlinable CTEs in the given expression
//
//---------------------------------------------------------------------------
ULONG
CUtils::UlInlinableCTEs
	(
	CExpression *pexpr,
	ULONG ulDepth
	)
{
	GPOS_CHECK_STACK_SIZE;
	GPOS_ASSERT(NULL != pexpr);

	COperator *pop = pexpr->Pop();

	if (COperator::EopLogicalCTEConsumer == pop->Eopid())
	{
		CLogicalCTEConsumer *popConsumer = CLogicalCTEConsumer::PopConvert(pop);
		CExpression *pexprProducer = COptCtxt::PoctxtFromTLS()->Pcteinfo()->PexprCTEProducer(popConsumer->UlCTEId());
		GPOS_ASSERT(NULL != pexprProducer);
		return ulDepth + UlInlinableCTEs(pexprProducer, ulDepth + 1);
	}

	// recursively process children
	const ULONG ulArity = pexpr->UlArity();
	ULONG ulChildCTEs = 0;
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		ulChildCTEs += UlInlinableCTEs((*pexpr)[ul], ulDepth);
	}

	return ulChildCTEs;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::UlJoins
//
//	@doc:
//		Return number of joins in the given expression
//
//---------------------------------------------------------------------------
ULONG
CUtils::UlJoins
	(
	CExpression *pexpr
	)
{
	GPOS_CHECK_STACK_SIZE;
	GPOS_ASSERT(NULL != pexpr);

	ULONG ulJoins = 0;
	COperator *pop = pexpr->Pop();

	if (COperator::EopLogicalCTEConsumer == pop->Eopid())
	{
		CLogicalCTEConsumer *popConsumer = CLogicalCTEConsumer::PopConvert(pop);
		CExpression *pexprProducer = COptCtxt::PoctxtFromTLS()->Pcteinfo()->PexprCTEProducer(popConsumer->UlCTEId());
		return UlJoins(pexprProducer);
	}

	if (FLogicalJoin(pop) || FPhysicalJoin(pop))
	{
		ulJoins = 1;
		if (COperator::EopLogicalNAryJoin == pop->Eopid())
		{
			// N-Ary join is equivalent to a cascade of (Arity - 2) binary joins
			ulJoins = pexpr->UlArity() - 2;
		}
	}

	// recursively process children
	const ULONG ulArity = pexpr->UlArity();
	ULONG ulChildJoins = 0;
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		ulChildJoins += UlJoins((*pexpr)[ul]);
	}

	return ulJoins + ulChildJoins;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::UlSubqueries
//
//	@doc:
//		Return number of subqueries in the given expression
//
//---------------------------------------------------------------------------
ULONG
CUtils::UlSubqueries
	(
	CExpression *pexpr
	)
{
	GPOS_CHECK_STACK_SIZE;
	GPOS_ASSERT(NULL != pexpr);

	ULONG ulSubqueries = 0;
	COperator *pop = pexpr->Pop();

	if (COperator::EopLogicalCTEConsumer == pop->Eopid())
	{
		CLogicalCTEConsumer *popConsumer = CLogicalCTEConsumer::PopConvert(pop);
		CExpression *pexprProducer = COptCtxt::PoctxtFromTLS()->Pcteinfo()->PexprCTEProducer(popConsumer->UlCTEId());
		return UlSubqueries(pexprProducer);
	}

	if (FSubquery(pop))
	{
		ulSubqueries = 1;
	}

	// recursively process children
	const ULONG ulArity = pexpr->UlArity();
	ULONG ulChildSubqueries = 0;
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		ulChildSubqueries += UlSubqueries((*pexpr)[ul]);
	}

	return ulSubqueries + ulChildSubqueries;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FScalarBoolOp
//
//	@doc:
//		 Check if given expression is a scalar boolean operator
//
//---------------------------------------------------------------------------
BOOL
CUtils::FScalarBoolOp
	(
	CExpression *pexpr
	)
{
	return (COperator::EopScalarBoolOp == pexpr->Pop()->Eopid());
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FScalarBoolOp
//
//	@doc:
//		Is the given expression a scalar bool op of the passed type?
//
//---------------------------------------------------------------------------
BOOL
CUtils::FScalarBoolOp
	(
	CExpression *pexpr,
	CScalarBoolOp::EBoolOperator eboolop
	)
{
	GPOS_ASSERT(NULL != pexpr);

	COperator *pop = pexpr->Pop();
	return
		pop->FScalar() &&
		COperator::EopScalarBoolOp == pop->Eopid() &&
		eboolop == CScalarBoolOp::PopConvert(pop)->Eboolop();
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FScalarNullTest
//
//	@doc:
//		 Check if given expression is a scalar null test
//
//---------------------------------------------------------------------------
BOOL
CUtils::FScalarNullTest
	(
	CExpression *pexpr
	)
{
	return (COperator::EopScalarNullTest == pexpr->Pop()->Eopid());
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FScalarNotNull
//
//	@doc:
//		 Check if given expression is a NOT NULL predicate
//
//---------------------------------------------------------------------------
BOOL
CUtils::FScalarNotNull
	(
	CExpression *pexpr
	)
{
	return FScalarBoolOp(pexpr, CScalarBoolOp::EboolopNot) &&
			FScalarNullTest((*pexpr)[0]);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FScalarIdent
//
//	@doc:
//		 Check if given expression is a scalar identifier
//
//---------------------------------------------------------------------------
BOOL
CUtils::FScalarIdent
	(
	CExpression *pexpr
	)
{
	return (COperator::EopScalarIdent == pexpr->Pop()->Eopid());
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FScalarIdentBoolType
//
//	@doc:
//		 Check if given expression is a scalar boolean identifier
//
//---------------------------------------------------------------------------
BOOL
CUtils::FScalarIdentBoolType
	(
	CExpression *pexpr
	)
{
	return FScalarIdent(pexpr) && 
			IMDType::EtiBool == CScalarIdent::PopConvert(pexpr->Pop())->Pcr()->Pmdtype()->Eti();
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FScalarArray
//
//	@doc:
//		 Check if given expression is a scalar array
//
//---------------------------------------------------------------------------
BOOL
CUtils::FScalarArray
	(
	CExpression *pexpr
	)
{
	return (COperator::EopScalarArray == pexpr->Pop()->Eopid());
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FScalarIdent
//
//	@doc:
//		Is the given expression a scalar identifier with the given column
//		reference
//
//---------------------------------------------------------------------------
BOOL
CUtils::FScalarIdent
	(
	CExpression *pexpr,
	CColRef *pcr
	)
{
	GPOS_ASSERT(NULL != pexpr);
	GPOS_ASSERT(NULL != pcr);

	return COperator::EopScalarIdent == pexpr->Pop()->Eopid() &&
			CScalarIdent::PopConvert(pexpr->Pop())->Pcr() == pcr;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FScalarConst
//
//	@doc:
//		Check if the expression is a scalar const
//
//---------------------------------------------------------------------------
BOOL
CUtils::FScalarConst
	(
	CExpression *pexpr
	)
{
	return (COperator::EopScalarConst == pexpr->Pop()->Eopid());
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FVarFreeExpr
//
//	@doc:
//		Check if the expression is variable-free
//
//---------------------------------------------------------------------------
BOOL
CUtils::FVarFreeExpr
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(pexpr->Pop()->FScalar());
	
	if (FScalarConst(pexpr))
	{
		return true;
	}
	
	CDrvdPropScalar *pdpScalar = CDrvdPropScalar::Pdpscalar(pexpr->PdpDerive());
	if (pdpScalar->FHasSubquery())
	{
		return false;
	}
	
	GPOS_ASSERT(0 == pdpScalar->PcrsDefined()->CElements());
	CColRefSet *pcrsUsed = pdpScalar->PcrsUsed();
	
	// no variables in expression
	return 0 == pcrsUsed->CElements();
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FPredicate
//
//	@doc:
//		Check if the expression is a scalar predicate, i.e. bool op, comparison,
//		or null test
//
//---------------------------------------------------------------------------
BOOL
CUtils::FPredicate
	(
	CExpression *pexpr
	)
{
	COperator *pop = pexpr->Pop();
	return
		pop->FScalar() &&
		(FScalarCmp(pexpr) ||
		FScalarArrayCmp(pexpr) ||
		FScalarBoolOp(pexpr) ||
		FScalarNullTest(pexpr));
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FHasAllDefaultComparisons
//
//	@doc:
//		Checks that the given type has all the comparisons: Eq, NEq, L, LEq, G,
//		GEq.
//
//---------------------------------------------------------------------------
BOOL
CUtils::FHasAllDefaultComparisons(const IMDType *pmdtype)
{
	GPOS_ASSERT(NULL != pmdtype);

	return IMDId::FValid(pmdtype->PmdidCmp(IMDType::EcmptEq)) &&
		IMDId::FValid(pmdtype->PmdidCmp(IMDType::EcmptNEq)) &&
		IMDId::FValid(pmdtype->PmdidCmp(IMDType::EcmptL)) &&
		IMDId::FValid(pmdtype->PmdidCmp(IMDType::EcmptLEq)) &&
		IMDId::FValid(pmdtype->PmdidCmp(IMDType::EcmptG)) &&
		IMDId::FValid(pmdtype->PmdidCmp(IMDType::EcmptGEq));
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FConstrainableType
//
//	@doc:
//		Determine whether a type is supported for use in contradiction detection.
//		The assumption is that we only compare data of the same type.
//
//---------------------------------------------------------------------------
BOOL
CUtils::FConstrainableType
	(
	IMDId *pmdidType
	)
{
	if (FIntType(pmdidType))
	{
		return true;
	}
	if (!GPOS_FTRACE(EopttraceEnableConstantExpressionEvaluation))
	{
		return false;
	}
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();
	const IMDType *pmdtype = pmda->Pmdtype(pmdidType);

	return FHasAllDefaultComparisons(pmdtype);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FIntType
//
//	@doc:
//		Determine whether a type is an integer type
//
//---------------------------------------------------------------------------
BOOL
CUtils::FIntType
	(
	IMDId *pmdidType
	)
{
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();
	IMDType::ETypeInfo eti = pmda->Pmdtype(pmdidType)->Eti();

	return (IMDType::EtiInt2 == eti ||
			IMDType::EtiInt4 == eti ||
			IMDType::EtiInt8 == eti);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FUsesChildColsOnly
//
//	@doc:
//		Check if a binary operator uses only columns produced by its children
//
//---------------------------------------------------------------------------
BOOL
CUtils::FUsesChildColsOnly
	(
	CExpressionHandle &exprhdl
	)
{
	GPOS_ASSERT(3 == exprhdl.UlArity());

	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();
	CColRefSet *pcrsUsed =  exprhdl.Pdpscalar(2 /*ulChildIndex*/)->PcrsUsed();
	CColRefSet *pcrs = GPOS_NEW(pmp) CColRefSet(pmp);
	pcrs->Include(exprhdl.Pdprel(0 /*ulChildIndex*/)->PcrsOutput());
	pcrs->Include(exprhdl.Pdprel(1 /*ulChildIndex*/)->PcrsOutput());
	BOOL fUsesChildCols = pcrs->FSubset(pcrsUsed);
	pcrs->Release();

	return fUsesChildCols;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FInnerUsesExternalCols
//
//	@doc:
//		Check if inner child of a binary operator uses columns not produced
//		by outer child
//
//---------------------------------------------------------------------------
BOOL
CUtils::FInnerUsesExternalCols
	(
	CExpressionHandle &exprhdl
	)
{
	GPOS_ASSERT(3 == exprhdl.UlArity());

	CColRefSet *pcrsOuterRefs = exprhdl.Pdprel(1 /*ulChildIndex*/)->PcrsOuter();
	if (0 == pcrsOuterRefs->CElements())
	{
		return false;
	}
	CColRefSet *pcrsOutput = exprhdl.Pdprel(0 /*ulChildIndex*/)->PcrsOutput();

	return !pcrsOutput->FSubset(pcrsOuterRefs);
}



//---------------------------------------------------------------------------
//	@function:
//		CUtils::FInnerUsesExternalColsOnly
//
//	@doc:
//		Check if inner child of a binary operator uses only columns not
//		produced by outer child
//
//---------------------------------------------------------------------------
BOOL
CUtils::FInnerUsesExternalColsOnly
	(
	CExpressionHandle &exprhdl
	)
{
	return FInnerUsesExternalCols(exprhdl) &&
			exprhdl.Pdprel(1)->PcrsOuter()->FDisjoint(exprhdl.Pdprel(0)->PcrsOutput());
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FComparisonPossible
//
//	@doc:
//		Check if given columns have available comparison operators
//
//---------------------------------------------------------------------------
BOOL
CUtils::FComparisonPossible
	(
	DrgPcr *pdrgpcr,
	IMDType::ECmpType ecmpt
	)
{
	GPOS_ASSERT(NULL != pdrgpcr);
	GPOS_ASSERT(0 < pdrgpcr->UlLength());

	const ULONG ulSize = pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];
		const IMDType *pmdtype = pcr->Pmdtype();
		if (!pmdtype->PmdidCmp(ecmpt)->FValid())
		{
			return false;
		}
	}

	return true;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpcrHashablePrefix
//
//	@doc:
//		Return the max prefix of hashable columns for the given columns
//
//---------------------------------------------------------------------------
DrgPcr *
CUtils::PdrgpcrHashablePrefix
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr
	)
{
	GPOS_ASSERT(NULL != pdrgpcr);
	GPOS_ASSERT(0 < pdrgpcr->UlLength());

	DrgPcr *pdrgpcrHashable = GPOS_NEW(pmp) DrgPcr (pmp);
	const ULONG ulSize = pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];
		const IMDType *pmdtype = pcr->Pmdtype();
		if (!pmdtype->FHashable())
		{
			return pdrgpcrHashable;
		}
		pdrgpcrHashable->Append(pcr);
	}

	return pdrgpcrHashable;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpcrHashableSubset
//
//	@doc:
//		Return the max subset of hashable columns for the given columns
//
//---------------------------------------------------------------------------
DrgPcr *
CUtils::PdrgpcrHashableSubset
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr
	)
{
	GPOS_ASSERT(NULL != pdrgpcr);
	GPOS_ASSERT(0 < pdrgpcr->UlLength());

	DrgPcr *pdrgpcrHashable = GPOS_NEW(pmp) DrgPcr(pmp);
	const ULONG ulSize = pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];
		const IMDType *pmdtype = pcr->Pmdtype();
		if (pmdtype->FHashable())
		{
			pdrgpcrHashable->Append(pcr);
		}
	}

	return pdrgpcrHashable;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FHashable
//
//	@doc:
//		Check if hashing is possible for the given columns
//
//---------------------------------------------------------------------------
BOOL
CUtils::FHashable
	(
	DrgPcr *pdrgpcr
	)
{
	GPOS_ASSERT(NULL != pdrgpcr);
	GPOS_ASSERT(0 < pdrgpcr->UlLength());

	const ULONG ulSize = pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];
		const IMDType *pmdtype = pcr->Pmdtype();
		if (!pmdtype->FHashable())
		{
			return false;
		}
	}

	return true;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FLogicalDML
//
//	@doc:
//		Check if given operator is a logical DML operator
//
//---------------------------------------------------------------------------
BOOL
CUtils::FLogicalDML
	(
	COperator *pop
	)
{
	GPOS_ASSERT(NULL != pop);
	
	COperator::EOperatorId eopid = pop->Eopid();
	return COperator::EopLogicalDML == eopid ||
			COperator::EopLogicalInsert == eopid ||
			COperator::EopLogicalDelete == eopid ||
			COperator::EopLogicalUpdate == eopid;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::SzFromWsz
//
//	@doc:
//		Return regular string from wide-character string
//
//---------------------------------------------------------------------------
CHAR *
CUtils::SzFromWsz
	(
	IMemoryPool *pmp,
	WCHAR *wsz
	)
{
	ULONG ulMaxLength = GPOS_WSZ_LENGTH(wsz) * GPOS_SIZEOF(WCHAR) + 1;
	CHAR *sz = GPOS_NEW_ARRAY(pmp, CHAR, ulMaxLength);
	clib::LWcsToMbs(sz, wsz, ulMaxLength);
	sz[ulMaxLength - 1] = '\0';

	return sz;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FFunctionallyDependent
//
//	@doc:
//		Is the given column functionally dependent on the given keyset
//
//---------------------------------------------------------------------------
BOOL
CUtils::FFunctionallyDependent
	(
	IMemoryPool *pmp,
	CDrvdPropRelational *pdprel,
	CColRefSet *pcrsKey,
	CColRef *pcr
	)
{
	GPOS_ASSERT(NULL != pdprel);
	GPOS_ASSERT(NULL != pcrsKey);
	GPOS_ASSERT(NULL != pcr);

	// check if column is a constant
	if (NULL != pdprel->Ppc()->Pcnstr())
	{
		CConstraint *pcnstr = pdprel->Ppc()->Pcnstr()->Pcnstr(pmp, pcr);
		if (NULL != pcnstr && CPredicateUtils::FConstColumn(pcnstr, pcr))
		{
			pcnstr->Release();
			return true;
		}
		
		CRefCount::SafeRelease(pcnstr);
	}
	
	// not a constant: check if column is functionally dependent on the key
	DrgPfd *pdrgpfd = pdprel->Pdrgpfd();

	const ULONG ulSize = pdrgpfd->UlSafeLength();
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		CFunctionalDependency *pfd = (*pdrgpfd)[ul];
		if (pfd->FFunctionallyDependent(pcrsKey, pcr))
		{
			return true;
		}
	}
	
	return false;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::Pdrgpul
//
//	@doc:
//		Construct an array of colids from the given array of column references
//
//---------------------------------------------------------------------------
DrgPul *
CUtils::Pdrgpul
	(
	IMemoryPool *pmp,
	const DrgPcr *pdrgpcr
	)
{
	DrgPul *pdrgpul = GPOS_NEW(pmp) DrgPul(pmp);

	const ULONG ulLen = pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulLen; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];
		ULONG *pul = GPOS_NEW(pmp) ULONG(pcr->UlId());
		pdrgpul->Append(pul);
	}

	return pdrgpul;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::GenerateFileName
//
//	@doc:
//		Generate a timestamp-based filename in the provided buffer.
//
//---------------------------------------------------------------------------
void
CUtils::GenerateFileName
	(
	CHAR *szBuf,
	const CHAR *szPrefix,
	const CHAR *szExt,
	ULONG ulLength,
	ULONG ulSessionId,
	ULONG ulCmdId
	)
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	CWStringDynamic *pstr = GPOS_NEW(pmp) CWStringDynamic(pmp);
	COstreamString oss(pstr);
	oss << szPrefix << "_%04d%02d%02d_%02d%02d%02d_%d_%d." << szExt;

	const WCHAR *wszFileNameTemplate = pstr->Wsz();
	GPOS_ASSERT(ulLength >= GPOS_FILE_NAME_BUF_SIZE);

	TIMEVAL tv;
	TIME tm;

	// get local time
	syslib::GetTimeOfDay(&tv, NULL/*timezone*/);
#ifdef GPOS_DEBUG
	TIME *ptm =
#endif // GPOS_DEBUG
	clib::PtmLocalTimeR(&tv.tv_sec, &tm);

	GPOS_ASSERT(NULL != ptm && "Failed to get local time");

	WCHAR wszBuf[GPOS_FILE_NAME_BUF_SIZE];
	CWStringStatic str(wszBuf, GPOS_ARRAY_SIZE(wszBuf));

	str.AppendFormat
		(
		wszFileNameTemplate,
		tm.tm_year + 1900,
		tm.tm_mon + 1,
		tm.tm_mday,
		tm.tm_hour,
		tm.tm_min,
		tm.tm_sec,
		ulSessionId,
		ulCmdId
		);

	INT iResult = (INT) clib::LWcsToMbs(szBuf, wszBuf, ulLength);

	GPOS_RTL_ASSERT((INT) 0 < iResult && iResult <= (INT) str.UlLength());

	szBuf[ulLength - 1] = '\0';

	GPOS_DELETE(pstr);

#ifdef GPOS_DEBUG
	CWorker::PwrkrSelf()->ResetTimeSlice();
#endif // GPOS_DEBUG

}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PcrRemap
//
//	@doc:
//		Return the mapping of the given colref based on the given hashmap
//
//---------------------------------------------------------------------------
CColRef *
CUtils::PcrRemap
	(
	const CColRef *pcr,
	HMUlCr *phmulcr,
	BOOL
#ifdef GPOS_DEBUG
	fMustExist
#endif //GPOS_DEBUG
	)
{
	GPOS_ASSERT(NULL != pcr);
	GPOS_ASSERT(NULL != phmulcr);

	ULONG ulId = pcr->UlId();
	CColRef *pcrMapped = phmulcr->PtLookup(&ulId);

	if (NULL != pcrMapped)
	{
		GPOS_ASSERT(pcr != pcrMapped);
		return pcrMapped;
	}

	GPOS_ASSERT(!fMustExist && "Column does not exist in hashmap");
	return const_cast<CColRef*>(pcr);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PcrsRemap
//
//	@doc:
//		Create a new colrefset corresponding to the given colrefset
//		and based on the given mapping
//
//---------------------------------------------------------------------------
CColRefSet *
CUtils::PcrsRemap
	(
	IMemoryPool *pmp,
	CColRefSet *pcrs,
	HMUlCr *phmulcr,
	BOOL fMustExist
	)
{
	GPOS_ASSERT(NULL != pcrs);
	GPOS_ASSERT(NULL != phmulcr);

	CColRefSet *pcrsMapped = GPOS_NEW(pmp) CColRefSet(pmp);

	CColRefSetIter crsi(*pcrs);
	while (crsi.FAdvance())
	{
		CColRef *pcr = crsi.Pcr();
		CColRef *pcrMapped = PcrRemap(pcr, phmulcr, fMustExist);
		pcrsMapped->Include(pcrMapped);
	}

	return pcrsMapped;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpcrRemap
//
//	@doc:
//		Create an array of column references corresponding to the given array
//		and based on the given mapping
//
//---------------------------------------------------------------------------
DrgPcr *
CUtils::PdrgpcrRemap
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr,
	HMUlCr *phmulcr,
	BOOL fMustExist
	)
{
	GPOS_ASSERT(NULL != pdrgpcr);
	GPOS_ASSERT(NULL != phmulcr);

	DrgPcr *pdrgpcrNew = GPOS_NEW(pmp) DrgPcr(pmp);

	const ULONG ulLen = pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulLen; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];
		CColRef *pcrMapped = PcrRemap(pcr, phmulcr, fMustExist);
		pdrgpcrNew->Append(pcrMapped);
	}

	return pdrgpcrNew;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpcrRemapAndCreate
//
//	@doc:
//		Create an array of column references corresponding to the given array
//		and based on the given mapping. Create new colrefs if necessary
//
//---------------------------------------------------------------------------
DrgPcr *
CUtils::PdrgpcrRemapAndCreate
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr,
	HMUlCr *phmulcr
	)
{
	GPOS_ASSERT(NULL != pdrgpcr);
	GPOS_ASSERT(NULL != phmulcr);

	// get column factory from optimizer context object
	CColumnFactory *pcf = COptCtxt::PoctxtFromTLS()->Pcf();

	DrgPcr *pdrgpcrNew = GPOS_NEW(pmp) DrgPcr(pmp);

	const ULONG ulLen = pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulLen; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];
		ULONG ulId = pcr->UlId();
		CColRef *pcrMapped = phmulcr->PtLookup(&ulId);
		if (NULL == pcrMapped)
		{
			// not found in hashmap, so create a new colref and add to hashmap
			pcrMapped = pcf->PcrCopy(pcr);

#ifdef GPOS_DEBUG
			BOOL fResult =
#endif // GPOS_DEBUG
			phmulcr->FInsert(GPOS_NEW(pmp) ULONG(ulId), pcrMapped);
			GPOS_ASSERT(fResult);
		}

		pdrgpcrNew->Append(pcrMapped);
	}

	return pdrgpcrNew;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpdrgpcrRemap
//
//	@doc:
//		Create an array of column arrays corresponding to the given array
//		and based on the given mapping
//
//---------------------------------------------------------------------------
DrgDrgPcr *
CUtils::PdrgpdrgpcrRemap
	(
	IMemoryPool *pmp,
	DrgDrgPcr *pdrgpdrgpcr,
	HMUlCr *phmulcr,
	BOOL fMustExist
	)
{
	GPOS_ASSERT(NULL != pdrgpdrgpcr);
	GPOS_ASSERT(NULL != phmulcr);

	DrgDrgPcr *pdrgpdrgpcrNew = GPOS_NEW(pmp) DrgDrgPcr(pmp);

	const ULONG ulArity = pdrgpdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		DrgPcr *pdrgpcr = PdrgpcrRemap(pmp, (*pdrgpdrgpcr)[ul], phmulcr, fMustExist);
		pdrgpdrgpcrNew->Append(pdrgpcr);
	}

	return pdrgpdrgpcrNew;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpexprRemap
//
//	@doc:
//		Remap given array of expressions with provided column mappings
//
//---------------------------------------------------------------------------
DrgPexpr *
CUtils::PdrgpexprRemap
	(
	IMemoryPool *pmp,
	DrgPexpr *pdrgpexpr,
	HMUlCr *phmulcr
	)
{
	GPOS_ASSERT(NULL != pdrgpexpr);
	GPOS_ASSERT(NULL != phmulcr);

	DrgPexpr *pdrgpexprNew = GPOS_NEW(pmp) DrgPexpr(pmp);

	const ULONG ulArity = pdrgpexpr->UlLength();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		CExpression *pexpr = (*pdrgpexpr)[ul];
		pdrgpexprNew->Append(pexpr->PexprCopyWithRemappedColumns(pmp, phmulcr, true /*fMustExist*/));
	}

	return pdrgpexprNew;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PhmulcrMapping
//
//	@doc:
//		Create col ID->ColRef mapping using the given ColRef arrays
//
//---------------------------------------------------------------------------
HMUlCr *
CUtils::PhmulcrMapping
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcrFrom,
	DrgPcr *pdrgpcrTo
	)
{
	GPOS_ASSERT(NULL != pdrgpcrFrom);
	GPOS_ASSERT(NULL != pdrgpcrTo);

	HMUlCr *phmulcr = GPOS_NEW(pmp) HMUlCr(pmp);
	AddColumnMapping(pmp, phmulcr, pdrgpcrFrom, pdrgpcrTo);

	return phmulcr;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::AddColumnMapping
//
//	@doc:
//		Add col ID->ColRef mappings to the given hashmap based on the
//		given ColRef arrays
//
//---------------------------------------------------------------------------
void
CUtils::AddColumnMapping
	(
	IMemoryPool *pmp,
	HMUlCr *phmulcr,
	DrgPcr *pdrgpcrFrom,
	DrgPcr *pdrgpcrTo
	)
{
	GPOS_ASSERT(NULL != phmulcr);
	GPOS_ASSERT(NULL != pdrgpcrFrom);
	GPOS_ASSERT(NULL != pdrgpcrTo);

	const ULONG ulColumns = pdrgpcrFrom->UlLength();
	GPOS_ASSERT(ulColumns == pdrgpcrTo->UlLength());

	for (ULONG ul = 0; ul < ulColumns; ul++)
	{
		CColRef *pcrFrom = (*pdrgpcrFrom)[ul];
		ULONG ulFromId = pcrFrom->UlId();
		CColRef *pcrTo = (*pdrgpcrTo)[ul];
		GPOS_ASSERT(pcrFrom != pcrTo);

#ifdef GPOS_DEBUG
		BOOL fResult = false;
#endif // GPOS_DEBUG
		CColRef *pcrExist = phmulcr->PtLookup(&ulFromId);
		if (NULL == pcrExist)
		{
#ifdef GPOS_DEBUG
			fResult =
#endif // GPOS_DEBUG
			phmulcr->FInsert(GPOS_NEW(pmp) ULONG(ulFromId), pcrTo);
		GPOS_ASSERT(fResult);
		}
		else
		{
#ifdef GPOS_DEBUG
			fResult =
#endif // GPOS_DEBUG
			phmulcr->FReplace(&ulFromId, pcrTo);
		}
		GPOS_ASSERT(fResult);
	}
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpcrExactCopy
//
//	@doc:
//		Create a copy of the array of column references
//
//---------------------------------------------------------------------------
DrgPcr *
CUtils::PdrgpcrExactCopy
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr
	)
{
	DrgPcr *pdrgpcrNew = GPOS_NEW(pmp) DrgPcr(pmp);

	const ULONG ulCols = pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulCols; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];
		pdrgpcrNew->Append(pcr);
	}

	return pdrgpcrNew;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpcrCopy
//
//	@doc:
//		Create an array of new column references with the same names and types
//		as the given column references. If the passed map is not null, mappings
//		from old to copied variables are added to it.
//
//---------------------------------------------------------------------------
DrgPcr *
CUtils::PdrgpcrCopy
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr,
	BOOL fAllComputed,
	HMUlCr *phmulcr
	)
{
	// get column factory from optimizer context object
	CColumnFactory *pcf = COptCtxt::PoctxtFromTLS()->Pcf();

	DrgPcr *pdrgpcrNew = GPOS_NEW(pmp) DrgPcr(pmp);

	const ULONG ulCols = pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulCols; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];
		CColRef *pcrNew = NULL;
		if (fAllComputed)
		{
			pcrNew = pcf->PcrCreate(pcr);
		}
		else
		{
			pcrNew = pcf->PcrCopy(pcr);
		}

		pdrgpcrNew->Append(pcrNew);
		if (NULL != phmulcr)
		{
#ifdef GPOS_DEBUG
			BOOL fInserted =
#endif
			phmulcr->FInsert(GPOS_NEW(pmp) ULONG(pcr->UlId()), pcrNew);
			GPOS_ASSERT(fInserted);
		}
	}

	return pdrgpcrNew;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FEqual
//
//	@doc:
//		Equality check between two arrays of column refs. Inputs can be NULL
//
//---------------------------------------------------------------------------
BOOL
CUtils::FEqual
	(
	DrgPcr *pdrgpcrFst,
	DrgPcr *pdrgpcrSnd
	)
{
	if (NULL == pdrgpcrFst || NULL == pdrgpcrSnd)
	{
		return (NULL == pdrgpcrFst && NULL == pdrgpcrSnd);
	}

	return pdrgpcrFst->FEqual(pdrgpcrSnd);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::UlHashColArray
//
//	@doc:
//		Compute hash value for an array of column references
//
//---------------------------------------------------------------------------
ULONG
CUtils::UlHashColArray
	(
	const DrgPcr *pdrgpcr,
	const ULONG ulMaxCols
	)
{
	ULONG ulHash = 0;

	const ULONG ulLen = pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulLen && ul < ulMaxCols; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];
		ulHash = gpos::UlCombineHashes(ulHash, gpos::UlHashPtr<CColRef>(pcr));
	}

	return ulHash;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PcrsCTEProducerColumns
//
//	@doc:
//		Return the set of column reference from the CTE Producer corresponding to the
// 		subset of input columns from the CTE Consumer
//
//---------------------------------------------------------------------------
CColRefSet *
CUtils::PcrsCTEProducerColumns
	(
	IMemoryPool *pmp,
	CColRefSet *pcrsInput,
	CLogicalCTEConsumer *popCTEConsumer
	)
{
	CExpression *pexprProducer = COptCtxt::PoctxtFromTLS()->Pcteinfo()->PexprCTEProducer(popCTEConsumer->UlCTEId());
	GPOS_ASSERT(NULL != pexprProducer);
	CLogicalCTEProducer *popProducer = CLogicalCTEProducer::PopConvert(pexprProducer->Pop());

	DrgPcr *pdrgpcrInput = pcrsInput->Pdrgpcr(pmp);
	HMUlCr *phmulcr = PhmulcrMapping(pmp, popCTEConsumer->Pdrgpcr(), popProducer->Pdrgpcr());
	DrgPcr *pdrgpcrOutput = PdrgpcrRemap(pmp, pdrgpcrInput, phmulcr, true /*fMustExist*/);

	CColRefSet *pcrsCTEProducer = GPOS_NEW(pmp) CColRefSet(pmp);
	pcrsCTEProducer->Include(pdrgpcrOutput);

	phmulcr->Release();
	pdrgpcrInput->Release();
	pdrgpcrOutput->Release();

	return pcrsCTEProducer;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprConjINDFCond
//
//	@doc:
//		Construct the join condition (AND-tree) of INDF condition
//		from the array of input column reference arrays
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprConjINDFCond
	(
	IMemoryPool *pmp,
	DrgDrgPcr *pdrgpdrgpcrInput
	)
{
	GPOS_ASSERT(NULL != pdrgpdrgpcrInput);
	GPOS_ASSERT(2 == pdrgpdrgpcrInput->UlLength());

	// assemble the new scalar condition
	CExpression *pexprScCond = NULL;
	const ULONG ulLen = (*pdrgpdrgpcrInput)[0]->UlLength();
	GPOS_ASSERT(0 != ulLen);
	GPOS_ASSERT(ulLen == (*pdrgpdrgpcrInput)[1]->UlLength());

	DrgPexpr *pdrgpexprInput = GPOS_NEW(pmp) DrgPexpr(pmp, ulLen);
	for (ULONG ul = 0; ul < ulLen; ul++)
	{
		CColRef *pcrLeft = (*(*pdrgpdrgpcrInput)[0])[ul];
		CColRef *pcrRight = (*(*pdrgpdrgpcrInput)[1])[ul];
		GPOS_ASSERT(pcrLeft != pcrRight);

		pdrgpexprInput->Append
						(
						CUtils::PexprNegate
								(
								pmp,
								CUtils::PexprIDF
										(
										pmp,
										CUtils::PexprScalarIdent(pmp, pcrLeft),
										CUtils::PexprScalarIdent(pmp, pcrRight)
										)
								)
						);
	}

	pexprScCond = CPredicateUtils::PexprConjunction(pmp, pdrgpexprInput);

	return pexprScCond;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FBinaryCoercibleCastedScId
//
//	@doc:
// 		Is the given expression a binary coercible cast of a scalar identifier
//
//---------------------------------------------------------------------------
BOOL
CUtils::FBinaryCoercibleCastedScId
	(
	CExpression *pexpr,
	CColRef *pcr
	)
{
	GPOS_ASSERT(NULL != pexpr);
	
	if (!FBinaryCoercibleCast(pexpr))
	{
		return false;
	}
	
	CExpression *pexprChild = (*pexpr)[0];
	
	// cast(col1)
	return COperator::EopScalarIdent == pexprChild->Pop()->Eopid() &&
		pcr == CScalarIdent::PopConvert(pexprChild->Pop())->Pcr();
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FBinaryCoercibleCastedScId
//
//	@doc:
// 		Is the given expression a binary coercible cast of a scalar identifier
//
//---------------------------------------------------------------------------
BOOL
CUtils::FBinaryCoercibleCastedScId
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);
	
	if (!FBinaryCoercibleCast(pexpr))
	{
		return false;
	}
	
	CExpression *pexprChild = (*pexpr)[0];
	
	// cast(col1)
	return COperator::EopScalarIdent == pexprChild->Pop()->Eopid();
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::UlPcrIndexContainingSet
//
//	@doc:
//		Return index of the set containing given column;
//		if column is not found, return ULONG_MAX
//
//---------------------------------------------------------------------------
ULONG
CUtils::UlPcrIndexContainingSet
	(
	DrgPcrs *pdrgpcrs,
	const CColRef *pcr
	)
{
	GPOS_ASSERT(NULL != pdrgpcrs);

	const ULONG ulSize = pdrgpcrs->UlLength();
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		CColRefSet *pcrs = (*pdrgpcrs)[ul];
		if (pcrs->FMember(pcr))
		{
			return ul;
		}
	}

	return ULONG_MAX;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PcrExtractFromScIdOrCastScId
//
//	@doc:
// 		Extract the column reference if the given expression a scalar identifier
// 		or a cast of a scalar identifier or a function that casts a scalar identifier.
// 		Else return NULL.
//
//---------------------------------------------------------------------------
const CColRef *
CUtils::PcrExtractFromScIdOrCastScId
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	BOOL fScIdent = COperator::EopScalarIdent == pexpr->Pop()->Eopid();
	BOOL fCastedScIdent = CScalarIdent::FCastedScId(pexpr);

	// col or cast(col)
	if (!fScIdent && !fCastedScIdent)
	{
		return NULL;
	}

	CScalarIdent *popScIdent = NULL;
	if (fScIdent)
	{
		popScIdent = CScalarIdent::PopConvert(pexpr->Pop());
	}
	else
	{
		GPOS_ASSERT(fCastedScIdent);
		popScIdent = CScalarIdent::PopConvert((*pexpr)[0]->Pop());
	}

	return popScIdent->Pcr();
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprCast
//
//	@doc:
//		Cast the input column reference to the destination mdid
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprCast
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	const CColRef *pcr,
	IMDId *pmdidDest
	)
{
	GPOS_ASSERT(NULL != pmdidDest);

	IMDId *pmdidSrc = pcr->Pmdtype()->Pmdid();
	GPOS_ASSERT(CMDAccessorUtils::FCastExists(pmda, pmdidSrc, pmdidDest));

	const IMDCast *pmdcast = pmda->Pmdcast(pmdidSrc, pmdidDest);

	pmdidDest->AddRef();
	pmdcast->PmdidCastFunc()->AddRef();

	CScalarCast *popCast = GPOS_NEW(pmp) CScalarCast(pmp, pmdidDest, pmdcast->PmdidCastFunc(), pmdcast->FBinaryCoercible());
	return GPOS_NEW(pmp) CExpression(pmp, popCast, PexprScalarIdent(pmp, pcr));
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprCast
//
//	@doc:
//		Cast the input expression to the destination mdid
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprCast
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	CExpression *pexpr,
	IMDId *pmdidDest
	)
{
	GPOS_ASSERT(NULL != pmdidDest);
	IMDId *pmdidSrc = CScalar::PopConvert(pexpr->Pop())->PmdidType();
	GPOS_ASSERT(CMDAccessorUtils::FCastExists(pmda, pmdidSrc, pmdidDest));

	const IMDCast *pmdcast = pmda->Pmdcast(pmdidSrc, pmdidDest);

	pmdidDest->AddRef();
	pmdcast->PmdidCastFunc()->AddRef();

	CScalarCast *popCast = GPOS_NEW(pmp) CScalarCast(pmp, pmdidDest, pmdcast->PmdidCastFunc(), pmdcast->FBinaryCoercible());
	return GPOS_NEW(pmp) CExpression(pmp, popCast, pexpr);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FBinaryCoercibleCast
//
//	@doc:
//		Check whether the given expression is a binary coercible cast of something
//
//---------------------------------------------------------------------------
BOOL
CUtils::FBinaryCoercibleCast
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);
	COperator *pop = pexpr->Pop();

	return COperator::EopScalarCast == pop->Eopid() &&
			CScalarCast::PopConvert(pop)->FBinaryCoercible();
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprRemoveRelabel
//
//	@doc:
//		Return the given expression without any binary coercible casts
// 		that exist on the top
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprWithoutBinaryCoercibleCasts
	(
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);
	GPOS_ASSERT(pexpr->Pop()->FScalar());

	CExpression *pexprOutput = pexpr;

	while (FBinaryCoercibleCast(pexprOutput))
	{
		GPOS_ASSERT(1 == pexprOutput->UlArity());
		pexprOutput = (*pexprOutput)[0];
	}

	return pexprOutput;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FHasDuplicates
//
//	@doc:
//		Check whether a colref array contains repeated items
//
//---------------------------------------------------------------------------
BOOL
CUtils::FHasDuplicates
	(
		const DrgPcr *pdrgpcr
	)
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();
	CColRefSet *pcrs = GPOS_NEW(pmp) CColRefSet(pmp);

	const ULONG ulLen = pdrgpcr->UlLength();
	for (ULONG ul = 0; ul < ulLen; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];
		if (pcrs->FMember(pcr))
		{
			pcrs->Release();
			return true;
		}

		pcrs->Include(pcr);
	}

	pcrs->Release();
	return false;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprLogicalJoin
//
//	@doc:
// 		Construct a logical join expression operator of the given type, with
//		the given children
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprLogicalJoin
	(
	IMemoryPool *pmp,
	EdxlJoinType edxljointype,
	DrgPexpr *pdrgpexpr
	)
{
	COperator *pop = NULL;
	switch (edxljointype)
	{
		case EdxljtInner:
			pop = GPOS_NEW(pmp) CLogicalNAryJoin(pmp);
			break;

		case EdxljtLeft:
			pop = GPOS_NEW(pmp) CLogicalLeftOuterJoin(pmp);
			break;

		case EdxljtLeftAntiSemijoin:
			pop = GPOS_NEW(pmp) CLogicalLeftAntiSemiJoin(pmp);
			break;

		case EdxljtLeftAntiSemijoinNotIn:
			pop = GPOS_NEW(pmp) CLogicalLeftAntiSemiJoinNotIn(pmp);
			break;

		case EdxljtFull:
			pop = GPOS_NEW(pmp) CLogicalFullOuterJoin(pmp);
			break;

		default:
			GPOS_ASSERT(!"Unsupported join type");
	}

	return GPOS_NEW(pmp) CExpression(pmp, pop, pdrgpexpr);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PdrgpexprScalarIdents
//
//	@doc:
// 		Construct an array of scalar ident expressions from the given array
//		of column references
//
//---------------------------------------------------------------------------
DrgPexpr *
CUtils::PdrgpexprScalarIdents
	(
	IMemoryPool *pmp,
	DrgPcr *pdrgpcr
	)
{
	GPOS_ASSERT(NULL != pdrgpcr);

	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	const ULONG ulLen = pdrgpcr->UlLength();

	for (ULONG ul = 0; ul < ulLen; ul++)
	{
		CColRef *pcr = (*pdrgpcr)[ul];
		CExpression *pexpr = PexprScalarIdent(pmp, pcr);
		pdrgpexpr->Append(pexpr);
	}

	return pdrgpexpr;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PcrsExtractColumns
//
//	@doc:
//		Return used columns by expressions in the given array
//
//---------------------------------------------------------------------------
CColRefSet *
CUtils::PcrsExtractColumns
	(
	IMemoryPool *pmp,
	const DrgPexpr *pdrgpexpr
	)
{
	GPOS_ASSERT(NULL != pdrgpexpr);
	CColRefSet *pcrs = GPOS_NEW(pmp) CColRefSet(pmp);

	const ULONG ulLen = pdrgpexpr->UlLength();
	for (ULONG ul = 0; ul < ulLen; ul++)
	{
		CExpression *pexpr = (*pdrgpexpr)[ul];
		pcrs->Include(CDrvdPropScalar::Pdpscalar(pexpr->PdpDerive())->PcrsUsed());
	}

	return pcrs;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PhmulcnstrBoolConstOnPartKeys
//
//	@doc:
//		Create a hashmap of constraints corresponding to a bool const on the given partkeys
//		true - unbounded intervals with nulls
//		false - empty intervals with no nulls
//---------------------------------------------------------------------------
HMUlCnstr *
CUtils::PhmulcnstrBoolConstOnPartKeys
	(
	IMemoryPool *pmp,
	DrgDrgPcr *pdrgpdrgpcrPartKey,
	BOOL fVal
	)
{
	GPOS_ASSERT(NULL != pdrgpdrgpcrPartKey);
	HMUlCnstr *phmulcnstr = GPOS_NEW(pmp) HMUlCnstr(pmp);

	const ULONG ulLevels = pdrgpdrgpcrPartKey->UlLength();
	for (ULONG ul = 0; ul < ulLevels; ul++)
	{
		CColRef *pcrPartKey = PcrExtractPartKey(pdrgpdrgpcrPartKey, ul);
		CConstraint *pcnstr = NULL;
		if (fVal)
		{
			// unbounded constraint
			pcnstr = CConstraintInterval::PciUnbounded(pmp, pcrPartKey, true /*fIsNull*/);
		}
		else
		{
			// empty constraint (contradiction)
			pcnstr = GPOS_NEW(pmp) CConstraintInterval(pmp, pcrPartKey, GPOS_NEW(pmp) DrgPrng(pmp), false /*fIsNull*/);
		}

		if (NULL != pcnstr)
		{
#ifdef GPOS_DEBUG
			BOOL fResult =
#endif // GPOS_DEBUG
			phmulcnstr->FInsert(GPOS_NEW(pmp) ULONG(ul), pcnstr);
			GPOS_ASSERT(fResult);
		}
	}

	return phmulcnstr;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PbsAllSet
//
//	@doc:
//		Returns a new bitset of the given length, where all the bits are set
//---------------------------------------------------------------------------
CBitSet *
CUtils::PbsAllSet
	(
	IMemoryPool *pmp,
	ULONG ulSize
	)
{
	CBitSet *pbs = GPOS_NEW(pmp) CBitSet(pmp, ulSize);
	for (ULONG ul = 0; ul < ulSize; ul++)
	{
		pbs->FExchangeSet(ul);
	}

	return pbs;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PbsAllSet
//
//	@doc:
//		Returns a new bitset, setting the bits in the given array
//---------------------------------------------------------------------------
CBitSet *
CUtils::Pbs
	(
	IMemoryPool *pmp,
	DrgPul *pdrgpul
	)
{
	GPOS_ASSERT(NULL != pdrgpul);
	CBitSet *pbs = GPOS_NEW(pmp) CBitSet(pmp);

	const ULONG ulLength = pdrgpul->UlLength();
	for (ULONG ul = 0; ul < ulLength; ul++)
	{
		ULONG *pul = (*pdrgpul)[ul];
		pbs->FExchangeSet(*pul);
	}

	return pbs;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PpartcnstrFromMDPartCnstr
//
//	@doc:
//		Extract part constraint from metadata
//
//---------------------------------------------------------------------------
CPartConstraint *
CUtils::PpartcnstrFromMDPartCnstr
	(
	IMemoryPool *pmp,
	CMDAccessor *pmda,
	DrgDrgPcr *pdrgpdrgpcrPartKey,
	const IMDPartConstraint *pmdpartcnstr,
	DrgPcr *pdrgpcrOutput,
	BOOL fDummyConstraint
	)
{
	GPOS_ASSERT(NULL != pdrgpdrgpcrPartKey);

	const ULONG ulLevels = pdrgpdrgpcrPartKey->UlLength();
	if (NULL == pmdpartcnstr)
	{
		// no constraint found in metadata: construct an unbounded constraint
		HMUlCnstr *phmulcnstr = PhmulcnstrBoolConstOnPartKeys(pmp, pdrgpdrgpcrPartKey, true /*fVal*/);
		CBitSet *pbsDefaultParts = PbsAllSet(pmp, ulLevels);
		pdrgpdrgpcrPartKey->AddRef();

		return GPOS_NEW(pmp) CPartConstraint(pmp, phmulcnstr, pbsDefaultParts, true /*fUnbounded*/, pdrgpdrgpcrPartKey);
	}

	if (fDummyConstraint)
	{
		return GPOS_NEW(pmp) CPartConstraint(true /*fUninterpreted*/);
	}

	CExpression *pexprPartCnstr = pmdpartcnstr->Pexpr(pmp, pmda, pdrgpcrOutput);

	HMUlCnstr *phmulcnstr = NULL;
	if (CUtils::FScalarConstTrue(pexprPartCnstr))
	{
		phmulcnstr = PhmulcnstrBoolConstOnPartKeys(pmp, pdrgpdrgpcrPartKey, true /*fVal*/);
	}
	else if (CUtils::FScalarConstFalse(pexprPartCnstr))
	{
		// contradiction
		phmulcnstr = PhmulcnstrBoolConstOnPartKeys(pmp, pdrgpdrgpcrPartKey, false /*fVal*/);
	}
	else
	{
		DrgPcrs *pdrgpcrs = NULL;
		CConstraint *pcnstr = CConstraint::PcnstrFromScalarExpr(pmp, pexprPartCnstr, &pdrgpcrs);
		CRefCount::SafeRelease(pdrgpcrs);

		phmulcnstr = GPOS_NEW(pmp) HMUlCnstr(pmp);
		for (ULONG ul = 0; ul < ulLevels && NULL != pcnstr; ul++)
		{
			CColRef *pcrPartKey = PcrExtractPartKey(pdrgpdrgpcrPartKey, ul);
			CConstraint *pcnstrLevel = pcnstr->Pcnstr(pmp, pcrPartKey);
			if (NULL == pcnstrLevel)
			{
				pcnstrLevel = CConstraintInterval::PciUnbounded(pmp, pcrPartKey, true /*fIsNull*/);
			}

			if (NULL != pcnstrLevel)
			{
#ifdef GPOS_DEBUG
				BOOL fResult =
#endif // GPOS_DEBUG
				phmulcnstr->FInsert(GPOS_NEW(pmp) ULONG(ul), pcnstrLevel);
				GPOS_ASSERT(fResult);
			}
		}
		CRefCount::SafeRelease(pcnstr);
	}

	pexprPartCnstr->Release();

	CBitSet *pbsDefaultParts = Pbs(pmp, pmdpartcnstr->PdrgpulDefaultParts());
	pdrgpdrgpcrPartKey->AddRef();

	return GPOS_NEW(pmp) CPartConstraint(pmp, phmulcnstr, pbsDefaultParts, pmdpartcnstr->FUnbounded(), pdrgpdrgpcrPartKey);
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprLogicalCTGDummy
//
//	@doc:
//		Helper to create a dummy constant table expression;
//		the table has one boolean column with value True and one row
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprLogicalCTGDummy
	(
	IMemoryPool *pmp
	)
{
	CColumnFactory *pcf = COptCtxt::PoctxtFromTLS()->Pcf();
	CMDAccessor *pmda = COptCtxt::PoctxtFromTLS()->Pmda();

	// generate a bool column
	const IMDTypeBool *pmdtypebool = pmda->PtMDType<IMDTypeBool>();
	CColRef *pcr = pcf->PcrCreate(pmdtypebool);
	DrgPcr *pdrgpcrCTG = GPOS_NEW(pmp) DrgPcr(pmp);
	pdrgpcrCTG->Append(pcr);

	// generate a bool datum
	DrgPdatum *pdrgpdatum = GPOS_NEW(pmp) DrgPdatum(pmp);
	IDatumBool *pdatum =  pmdtypebool->PdatumBool(pmp, false /*fVal*/, false /*fNull*/);
	pdrgpdatum->Append(pdatum);
	DrgPdrgPdatum *pdrgpdrgpdatum = GPOS_NEW(pmp) DrgPdrgPdatum(pmp);
	pdrgpdrgpdatum->Append(pdrgpdatum);

	return GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CLogicalConstTableGet(pmp, pdrgpcrCTG, pdrgpdrgpdatum));
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PcrMap
//
//	@doc:
//		Map a column from source array to destination array based on
//		position
//
//---------------------------------------------------------------------------
CColRef *
CUtils::PcrMap
	(
	CColRef *pcrSource,
	DrgPcr *pdrgpcrSource,
	DrgPcr *pdrgpcrTarget
	)
{
	const ULONG ulCols = pdrgpcrSource->UlLength();
	GPOS_ASSERT(ulCols == pdrgpcrTarget->UlLength());

	CColRef *pcrTarget = NULL;
	for (ULONG ul = 0; NULL == pcrTarget && ul < ulCols; ul++)
	{
		if ((*pdrgpcrSource)[ul] == pcrSource)
		{
			pcrTarget = (*pdrgpcrTarget)[ul];
		}
	}

	return pcrTarget;
}

//---------------------------------------------------------------------------
//	@function:
//		CUtils::FMotionOverUnresolvedPartConsumers
//
//	@doc:
//		Check if the given operator is a motion and the derived relational 
//		properties contain a consumer which is not in the required part consumers
//
//---------------------------------------------------------------------------
BOOL
CUtils::FMotionOverUnresolvedPartConsumers
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	CPartIndexMap *ppimReqd
	)
{
	GPOS_ASSERT(NULL != ppimReqd);
	
	if (!FPhysicalMotion(exprhdl.Pop()))
	{
		return false;
	}

	CPartIndexMap *ppimDrvd = exprhdl.Pdpplan(0 /*ulChildIndex*/)->Ppim();
	DrgPul *pdrgpulScanIds = ppimDrvd->PdrgpulScanIds(pmp, true /*fConsumersOnly*/);
	BOOL fHasUnresolvedConsumers = false;

	const ULONG ulConsumers = pdrgpulScanIds->UlLength();
	if (0 < ulConsumers && !ppimReqd->FContainsUnresolved())
	{
		fHasUnresolvedConsumers = true;
	}
	
	for (ULONG ul = 0; !fHasUnresolvedConsumers && ul < ulConsumers; ul++)
	{ 
		ULONG *pulScanId = (*pdrgpulScanIds)[ul];
		if (!ppimReqd->FContains(*pulScanId))
		{
			// there is an unresolved consumer which is not included in the
			// requirements and will therefore be resolved elsewhere
			fHasUnresolvedConsumers = true;
		}
	}
	
	pdrgpulScanIds->Release();

	return fHasUnresolvedConsumers;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::FDuplicateHazardMotion
//
//	@doc:
//		Check if duplicate values can be generated when executing the given
//		Motion expression,
//		duplicates occur if Motion's input has replicated/universal distribution,
//		which means that we have exactly the same copy of input on each host,
//
//
//---------------------------------------------------------------------------
BOOL
CUtils::FDuplicateHazardMotion
	(
	CExpression *pexprMotion
	)
{
	GPOS_ASSERT(NULL != pexprMotion);
	GPOS_ASSERT(CUtils::FPhysicalMotion(pexprMotion->Pop()));

	CExpression *pexprChild = (*pexprMotion)[0];
	CDrvdPropPlan *pdpplanChild = CDrvdPropPlan::Pdpplan(pexprChild->PdpDerive());
	CDistributionSpec *pdsChild = pdpplanChild->Pds();
	CDistributionSpec::EDistributionType edtChild = pdsChild->Edt();

	BOOL fReplicatedInput =
		CDistributionSpec::EdtReplicated == edtChild ||
		CDistributionSpec::EdtUniversal == edtChild;

	return fReplicatedInput;
}


//---------------------------------------------------------------------------
//	@function:
//		CUtils::PexprCollapseProjects
//
//	@doc:
//		Collapse the top two project nodes, if unable return NULL;
//
//---------------------------------------------------------------------------
CExpression *
CUtils::PexprCollapseProjects
	(
	IMemoryPool *pmp,
	CExpression *pexpr
	)
{
	GPOS_ASSERT(NULL != pexpr);

	if (pexpr->Pop()->Eopid() != COperator::EopLogicalProject)
	{
		// not a project node
		return NULL;
	}

	CExpression *pexprRel= (*pexpr)[0];
	CExpression *pexprScalar = (*pexpr)[1];

	if (pexprRel->Pop()->Eopid() != COperator::EopLogicalProject)
	{
		// not a project node
		return NULL;
	}

	CExpression *pexprChildRel = (*pexprRel)[0];
	CExpression *pexprChildScalar = (*pexprRel)[1];

	CColRefSet *pcrsDefinedChild = GPOS_NEW(pmp) CColRefSet
										(
										pmp,
										*CDrvdPropScalar::Pdpscalar(pexprChildScalar->PdpDerive())->PcrsDefined()
										);

	// array of project elements for the new child project node
	DrgPexpr *pdrgpexprPrElChild = GPOS_NEW(pmp) DrgPexpr(pmp);

	// iterate over the parent project elements and see if we can add it to the child's project node
	DrgPexpr *pdrgpexprPrEl = GPOS_NEW(pmp) DrgPexpr(pmp);
	ULONG ulLenPr = pexprScalar->UlArity();
	for (ULONG ul1 = 0; ul1 < ulLenPr; ul1++)
	{
		CExpression *pexprPrE = (*pexprScalar)[ul1];

		CColRefSet *pcrsUsed = GPOS_NEW(pmp) CColRefSet
											(
											pmp,
											*CDrvdPropScalar::Pdpscalar(pexprPrE->PdpDerive())->PcrsUsed()
											);

		pexprPrE->AddRef();

		pcrsUsed->Intersection(pcrsDefinedChild);
		ULONG ulIntersect = pcrsUsed->CElements();

		if (0 == ulIntersect)
		{
			pdrgpexprPrElChild->Append(pexprPrE);
		}
		else
		{
			pdrgpexprPrEl->Append(pexprPrE);
		}

		pcrsUsed->Release();
	}

	// clean up
	pcrsDefinedChild->Release();

	// add all project elements of the origin child project node
	ULONG ulLenChild = pexprChildScalar->UlArity();
	for (ULONG ul = 0; ul < ulLenChild; ul++)
	{
		CExpression *pexprPrE = (*pexprChildScalar)[ul];
		pexprPrE->AddRef();
		pdrgpexprPrElChild->Append(pexprPrE);
	}

	if (ulLenPr == pdrgpexprPrEl->UlLength())
	{
		// no candidate project element found for collapsing
		pdrgpexprPrElChild->Release();
		pdrgpexprPrEl->Release();

		return NULL;
	}

	pexprChildRel->AddRef();
	CExpression *pexprProject = GPOS_NEW(pmp) CExpression
										(
										pmp,
										GPOS_NEW(pmp) CLogicalProject(pmp),
										pexprChildRel,
										GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CScalarProjectList(pmp), pdrgpexprPrElChild)
										);

	if (0 == pdrgpexprPrEl->UlLength())
	{
		// no residual project elements
		pdrgpexprPrEl->Release();

		return pexprProject;
	}

	return GPOS_NEW(pmp) CExpression
						(
						pmp,
						GPOS_NEW(pmp) CLogicalProject(pmp),
						pexprProject,
						GPOS_NEW(pmp) CExpression(pmp, GPOS_NEW(pmp) CScalarProjectList(pmp), pdrgpexprPrEl)
						);
}

// EOF
