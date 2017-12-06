//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CTranslatorDXLToExprTest.h
//
//	@doc:
//		Tests translating DXL trees into Expr tree
//		
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------


#ifndef GPOPT_CTranslatorDXLToExprTest_H
#define GPOPT_CTranslatorDXLToExprTest_H

#include "gpos/base.h"
#include "gpos/common/CRefCount.h"
#include "gpos/io/COstreamString.h"
#include "gpos/string/CWStringDynamic.h"

#include "gpopt/base/CColRef.h"
#include "gpopt/metadata/CColumnDescriptor.h"
#include "gpopt/metadata/CTableDescriptor.h"
#include "gpopt/mdcache/CMDAccessor.h"
#include "gpopt/operators/CExpression.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CTranslatorDXLToExprTest
	//
	//	@doc:
	//		Static unit tests
	//
	//---------------------------------------------------------------------------

	class CTranslatorDXLToExprTest
	{
		private:

			// translate a dxl document and check against the expected Expr tree's string representation
			static GPOS_RESULT EresTranslateAndCheck
						(
						IMemoryPool *pmp,
						const CHAR *szDXLFileName, // DXL document representing the DXL logical tree
						const CWStringDynamic *pstrExpQuery // string representation of the expected query
						);

			// translate a dxl document into Expr Tree
			static CExpression* Pexpr
						(
						IMemoryPool *pmp, // memory pool
						const CHAR *szDXLFileName // DXL document representing the DXL logical tree
						);

			// generate a string representation of a given Expr tree
			static CWStringDynamic *Pstr
					(
					IMemoryPool *pmp,
					CExpression *pexpr
					);

			// create a get expression for a table (r) with two integer columns
			static CExpression *PexprGet(IMemoryPool *pmp);

		public:

			// unittests
			static GPOS_RESULT EresUnittest();

			// tests for supported operators
			static GPOS_RESULT EresUnittest_SingleTableQuery();
			static GPOS_RESULT EresUnittest_SelectQuery();
			static GPOS_RESULT EresUnittest_SelectQueryWithConst();
			static GPOS_RESULT EresUnittest_SelectQueryWithBoolExpr();
			static GPOS_RESULT EresUnittest_SelectQueryWithScalarOp();
			static GPOS_RESULT EresUnittest_Limit();
			static GPOS_RESULT EresUnittest_LimitNoOffset();
			static GPOS_RESULT EresUnittest_ScalarSubquery();
			static GPOS_RESULT EresUnittest_TVF();
			
	}; // class CTranslatorDXLToExprTest
}

#endif // !GPOPT_CTranslatorDXLToExprTest_H

// EOF
