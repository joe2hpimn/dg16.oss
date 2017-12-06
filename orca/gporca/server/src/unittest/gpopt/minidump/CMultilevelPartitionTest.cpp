//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2015 Pivotal, Inc.
//
//	@filename:
//		CMultilevelPartitionTest.cpp
//
//	@doc:
//		Test for optimizing queries on multilevel partitioned tables
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/base.h"
#include "gpos/memory/CAutoMemoryPool.h"
#include "gpos/task/CAutoTraceFlag.h"
#include "gpos/test/CUnittest.h"

#include "gpopt/exception.h"
#include "gpopt/minidump/CMinidumperUtils.h"

#include "unittest/gpopt/CTestUtils.h"

#include "unittest/gpopt/minidump/CMultilevelPartitionTest.h"

using namespace gpopt;

ULONG CMultilevelPartitionTest::m_ulMLPTTestCounter = 0;  // start from first test

// minidump files
const CHAR *rgszMultilevel[] =
	{
		"../data/dxl/multilevel-partitioning/Multilevel-FullScan.mdp",
		"../data/dxl/multilevel-partitioning/Multilevel-ConstPred-Level1-NoDefault.mdp",
		"../data/dxl/multilevel-partitioning/Multilevel-ConstPred-Level1-Default.mdp",
		"../data/dxl/multilevel-partitioning/Multilevel-ConstPred-Level2-NoDefault.mdp",
		"../data/dxl/multilevel-partitioning/Multilevel-ConstPred-Level2-Default.mdp",
		"../data/dxl/multilevel-partitioning/Multilevel-ConstPred-AllLevels-NoDefault.mdp",
		"../data/dxl/multilevel-partitioning/Multilevel-ConstPred-AllLevels-Default.mdp",
		"../data/dxl/multilevel-partitioning/Multilevel-JoinPred-Level1.mdp",
		"../data/dxl/multilevel-partitioning/Multilevel-JoinPred-Level2.mdp",
		"../data/dxl/multilevel-partitioning/Multilevel-JoinPred-AllLevels.mdp",
		"../data/dxl/multilevel-partitioning/Multilevel-Nary-Join.mdp",
	};

//---------------------------------------------------------------------------
//	@function:
//		CMultilevelPartitionTest::EresUnittest
//
//	@doc:
//		Unittest for expressions
//
//---------------------------------------------------------------------------
GPOS_RESULT
CMultilevelPartitionTest::EresUnittest()
{

	CUnittest rgut[] =
		{
		GPOS_UNITTEST_FUNC(EresUnittest_RunTests),
		};

	GPOS_RESULT eres = CUnittest::EresExecute(rgut, GPOS_ARRAY_SIZE(rgut));

	// reset metadata cache
	CMDCache::Reset();

	return eres;
}

//---------------------------------------------------------------------------
//	@function:
//		CMultilevelPartitionTest::EresUnittest_RunTests
//
//	@doc:
//		Run all Minidump-based tests with plan matching
//
//---------------------------------------------------------------------------
GPOS_RESULT
CMultilevelPartitionTest::EresUnittest_RunTests()
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	const ULONG ulTests = GPOS_ARRAY_SIZE(rgszMultilevel);

	return CTestUtils::EresRunMinidumps
						(
						pmp,
						rgszMultilevel,
						ulTests,
						&m_ulMLPTTestCounter,
						1, // ulSessionId
						1,  // ulCmdId
						true, // fMatchPlans
						true // fTestSpacePruning
						);
}

// EOF
