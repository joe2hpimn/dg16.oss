//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2014 Pivotal, Inc.
//
//	@filename:
//		CConstExprEvaluatorDXLTest.h
//
//	@doc:
//		Unit tests for CConstExprEvaluatorDXL
//
//	@owner:
//		
//
//	@test:
//
//---------------------------------------------------------------------------

#ifndef GPOPT_CConstExprEvaluatorDXLTest_H
#define GPOPT_CConstExprEvaluatorDXLTest_H

#include "gpos/base.h"

#include "gpopt/eval/IConstDXLNodeEvaluator.h"

// forward decl
namespace gpdxl
{
	class CDXLNode;
}

namespace gpopt
{
	// forward decl
	class CMDAccessor;

	//---------------------------------------------------------------------------
	//	@class:
	//		CConstExprEvaluatorDXLTest
	//
	//	@doc:
	//		Unit tests for CConstExprEvaluatorDXL
	//
	//---------------------------------------------------------------------------
	class CConstExprEvaluatorDXLTest
	{
		private:
			class CDummyConstDXLNodeEvaluator : public IConstDXLNodeEvaluator
			{
				private:
					// memory pool
					IMemoryPool *m_pmp;

					// metadata accessor
					CMDAccessor *m_pmda;

					// dummy value to return
					INT m_iVal;

					// private copy ctor
					CDummyConstDXLNodeEvaluator(const CDummyConstDXLNodeEvaluator&);

				public:
					// ctor
					CDummyConstDXLNodeEvaluator
						(
						IMemoryPool *pmp,
						CMDAccessor *pmda,
						INT iVal
						)
						:
						m_pmp(pmp),
						m_pmda(pmda),
						m_iVal(iVal)
					{}

					// dtor
					virtual
					~CDummyConstDXLNodeEvaluator()
					{}

					// evaluate the given DXL node representing an expression and returns a dummy value as DXL
					virtual
					gpdxl::CDXLNode *PdxlnEvaluateExpr(const gpdxl::CDXLNode *pdxlnExpr);

					// can evaluate expressions
					virtual
					BOOL FCanEvalExpressions()
					{
						return true;
					}
			};

			// value  which the dummy constant evaluator should produce
			static
			const INT m_iDefaultEvalValue;

		public:
			// run unittests
			static
			GPOS_RESULT EresUnittest();

			// test evaluation for a constant
			static
			GPOS_RESULT EresUnittest_Constants();

			// test that evaluation fails for a non scalar input
			static
			GPOS_RESULT EresUnittest_NonScalar();

			// test that evaluation fails for a scalar with a nested subquery
			static
			GPOS_RESULT EresUnittest_NestedSubquery();

			// test that evaluation fails for a scalar with variables
			static
			GPOS_RESULT EresUnittest_ScalarContainingVariables();
	};
}

#endif // !GPOPT_CConstExprEvaluatorDXLTest_H

// EOF
