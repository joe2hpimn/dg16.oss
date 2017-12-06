//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CTableDescriptorTest.cpp
//
//	@doc:
//		Test for CTableDescriptor
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#include "gpos/io/COstreamString.h"
#include "gpos/string/CWStringDynamic.h"

#include "gpopt/base/CUtils.h"
#include "gpopt/base/CColumnFactory.h"
#include "gpopt/metadata/CTableDescriptor.h"
#include "gpopt/base/CQueryContext.h"

#include "unittest/base.h"
#include "unittest/gpopt/CTestUtils.h"
#include "unittest/gpopt/metadata/CTableDescriptorTest.h"
#include "unittest/gpopt/metadata/CColumnDescriptorTest.h"
#include "unittest/gpopt/translate/CTranslatorExprToDXLTest.h"
#include "unittest/gpopt/CTestUtils.h"

#include "naucrates/md/IMDTypeInt4.h"
#include "naucrates/md/CMDIdGPDB.h"
#include "naucrates/md/CMDProviderMemory.h"

#include "gpopt/eval/CConstExprEvaluatorDefault.h"

//---------------------------------------------------------------------------
//	@function:
//		CTableDescriptorTest::EresUnittest
//
//	@doc:
//		Unittest for metadata names
//
//---------------------------------------------------------------------------
GPOS_RESULT
CTableDescriptorTest::EresUnittest()
{
	CUnittest rgut[] =
		{
		GPOS_UNITTEST_FUNC(CTableDescriptorTest::EresUnittest_Basic)
		};

	return CUnittest::EresExecute(rgut, GPOS_ARRAY_SIZE(rgut));
}


//---------------------------------------------------------------------------
//	@function:
//		CTableDescriptorTest::EresUnittest_Basic
//
//	@doc:
//		basic naming, assignment thru copy constructors
//
//---------------------------------------------------------------------------
GPOS_RESULT
CTableDescriptorTest::EresUnittest_Basic()
{
	CAutoMemoryPool amp;
	IMemoryPool *pmp = amp.Pmp();

	// Setup an MD cache with a file-based provider
	CMDProviderMemory *pmdp = CTestUtils::m_pmdpf;
	pmdp->AddRef();
	CMDAccessor mda(pmp, CMDCache::Pcache(), CTestUtils::m_sysidDefault, pmdp);

	// install opt context in TLS
	CAutoOptCtxt aoc
					(
					pmp,
					&mda,
					NULL,  /* pceeval */
					CTestUtils::Pcm(pmp)
					);

	CWStringConst strName(GPOS_WSZ_LIT("MyTable"));
	CMDIdGPDB *pmdid = GPOS_NEW(pmp) CMDIdGPDB(GPOPT_MDCACHE_TEST_OID, 1, 1);
	CTableDescriptor *ptabdesc = CTestUtils::PtabdescCreate(pmp, 10, pmdid, CName(&strName));

#ifdef GPOS_DEBUG
	CWStringDynamic str(pmp);
	COstreamString oss(&str);
	ptabdesc->OsPrint(oss);

	GPOS_TRACE(str.Wsz());
#endif // GPOS_DEBUG

	ptabdesc->Release();

	return GPOS_OK;
}

// EOF
