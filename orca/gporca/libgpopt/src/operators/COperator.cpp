//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		COperator.cpp
//
//	@doc:
//		Implementation of operator base class
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/base.h"

#include "gpopt/base/CDrvdPropRelational.h"
#include "gpopt/base/CReqdPropRelational.h"
#include "gpopt/operators/CExpression.h"
#include "gpopt/operators/CExpressionHandle.h"
#include "gpopt/operators/COperator.h"

using namespace gpopt;

// generate unique operator ids
CAtomicULONG COperator::m_aulOpIdCounter(0);

//---------------------------------------------------------------------------
//	@function:
//		COperator::COperator
//
//	@doc:
//		ctor
//
//---------------------------------------------------------------------------
COperator::COperator
	(
	IMemoryPool *pmp
	)
	:
	m_ulOpId(m_aulOpIdCounter.TIncr()),
	m_pmp(pmp),
	m_fPattern(false)
{
	GPOS_ASSERT(NULL != pmp);
}


//---------------------------------------------------------------------------
//	@function:
//		COperator::UlHash
//
//	@doc:
//		default hash function based on operator ID
//
//---------------------------------------------------------------------------
ULONG
COperator::UlHash() const
{
	ULONG ulEopid = (ULONG) Eopid();
	
	return gpos::UlHash<ULONG>(&ulEopid);
}


//---------------------------------------------------------------------------
//	@function:
//		COperator::OsPrint
//
//	@doc:
//		debug print
//
//---------------------------------------------------------------------------
IOstream &
COperator::OsPrint
	(
	IOstream &os
	) 
	const
{
	os << this->SzId();
	return os;
}

//---------------------------------------------------------------------------
//	@function:
//		COperator::EfdaDeriveFromChildren
//
//	@doc:
//		Derive data access function property from child expressions
//
//---------------------------------------------------------------------------
IMDFunction::EFuncDataAcc
COperator::EfdaDeriveFromChildren
	(
	CExpressionHandle &exprhdl,
	IMDFunction::EFuncDataAcc efdaDefault
	)
{
	IMDFunction::EFuncDataAcc efda = efdaDefault;

	const ULONG ulArity = exprhdl.UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		IMDFunction::EFuncDataAcc efdaChild = exprhdl.PfpChild(ul)->Efda();
		if (efdaChild > efda)
		{
			efda = efdaChild;
		}
	}

	return efda;
}

//---------------------------------------------------------------------------
//	@function:
//		COperator::EfsDeriveFromChildren
//
//	@doc:
//		Derive stability function property from child expressions
//
//---------------------------------------------------------------------------
IMDFunction::EFuncStbl
COperator::EfsDeriveFromChildren
	(
	CExpressionHandle &exprhdl,
	IMDFunction::EFuncStbl efsDefault
	)
{
	IMDFunction::EFuncStbl efs = efsDefault;

	const ULONG ulArity = exprhdl.UlArity();
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		IMDFunction::EFuncStbl efsChild = exprhdl.PfpChild(ul)->Efs();
		if (efsChild > efs)
		{
			efs = efsChild;
		}
	}

	return efs;
}

//---------------------------------------------------------------------------
//	@function:
//		COperator::PfpDeriveFromChildren
//
//	@doc:
//		Derive function properties from child expressions
//
//---------------------------------------------------------------------------
CFunctionProp *
COperator::PfpDeriveFromChildren
	(
	IMemoryPool *pmp,
	CExpressionHandle &exprhdl,
	IMDFunction::EFuncStbl efsDefault,
	IMDFunction::EFuncDataAcc efdaDefault,
	BOOL fHasVolatileFunctionScan,
	BOOL fScan
	)
{
	IMDFunction::EFuncStbl efs = EfsDeriveFromChildren(exprhdl, efsDefault);
	IMDFunction::EFuncDataAcc efda = EfdaDeriveFromChildren(exprhdl, efdaDefault);

	return GPOS_NEW(pmp) CFunctionProp
						(
						efs,
						efda,
						fHasVolatileFunctionScan || exprhdl.FChildrenHaveVolatileFuncScan(),
						fScan
						);
}

//---------------------------------------------------------------------------
//	@function:
//		COperator::PopCopyDefault
//
//	@doc:
//		Return an addref'ed copy of the operator
//
//---------------------------------------------------------------------------
COperator *
COperator::PopCopyDefault()
{
	this->AddRef();
	return this;
}

// EOF

