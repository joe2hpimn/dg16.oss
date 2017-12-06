//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		exception.h
//
//	@doc:
//		Definition of GPOPT-specific exception types
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_exception_H
#define GPOPT_exception_H

#include "gpos/types.h"
#include "gpos/memory/IMemoryPool.h"

namespace gpopt
{
	// major exception types - reserve range 1000-2000
	enum ExMajor
	{
		ExmaGPOPT = 1000,

		ExmaSentinel
	};

	// minor exception types
	enum ExMinor
	{
		ExmiNoPlanFound,
		ExmiInvalidPlanAlternative,
		ExmiUnsupportedOp,
		ExmiUnexpectedOp,
		ExmiUnsupportedPred,
		ExmiUnsupportedCompositePartKey,
		ExmiUnsupportedNonDeterministicUpdate,
		ExmiUnsatisfiedRequiredProperties,
		ExmiEvalUnsupportedScalarExpr,

		ExmiSentinel
	};

	// message initialization for GPOS exceptions
	gpos::GPOS_RESULT EresExceptionInit(gpos::IMemoryPool *pmp);

}

#endif // !GPOPT_exception_H


// EOF
