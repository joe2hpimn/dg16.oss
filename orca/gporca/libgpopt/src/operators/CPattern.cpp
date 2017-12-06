//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CPattern.cpp
//
//	@doc:
//		Implementation of base class of pattern operators
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/base.h"
#include "gpopt/operators/CPattern.h"

using namespace gpopt;


//---------------------------------------------------------------------------
//	@function:
//		CPattern::PdpCreate
//
//	@doc:
//		Pattern operators cannot derive properties; the assembly of the
//		expression has to take care of this on a higher level
//
//---------------------------------------------------------------------------
CDrvdProp *
CPattern::PdpCreate
	(
	IMemoryPool * // pmp
	)
	const
{
	GPOS_ASSERT(!"Cannot derive properties on pattern");
	return NULL;
}


//---------------------------------------------------------------------------
//	@function:
//		CPattern::PrpCreate
//
//	@doc:
//		Pattern operators cannot compute required properties; the assembly of the
//		expression has to take care of this on a higher level
//
//---------------------------------------------------------------------------
CReqdProp *
CPattern::PrpCreate
	(
	IMemoryPool * // pmp
	)
	const
{
	GPOS_ASSERT(!"Cannot compute required properties on pattern");
	return NULL;
}


//---------------------------------------------------------------------------
//	@function:
//		CPattern::FMatch
//
//	@doc:
//		match against an operator
//
//---------------------------------------------------------------------------
BOOL
CPattern::FMatch
	(
	COperator *pop
	)
	const
{
	return Eopid() == pop->Eopid();
}


//---------------------------------------------------------------------------
//	@function:
//		CPattern::FInputOrderSensitive
//
//	@doc:
//		By default patterns are leaves; no need to call this function ever
//
//---------------------------------------------------------------------------
BOOL 
CPattern::FInputOrderSensitive() const
{
	GPOS_ASSERT(!"Unexpected call to function FInputOrderSensitive");
	return true;
}

//---------------------------------------------------------------------------
//	@function:
//		CPattern::PopCopyWithRemappedColumns
//
//	@doc:
//		Return a copy of the operator with remapped columns
//
//---------------------------------------------------------------------------
COperator *
CPattern::PopCopyWithRemappedColumns
	(
	IMemoryPool *, //pmp,
	HMUlCr *, //phmulcr,
	BOOL //fMustExist
	)
{
	GPOS_ASSERT(!"PopCopyWithRemappedColumns should not be called for a pattern");
	return NULL;
}

// EOF

