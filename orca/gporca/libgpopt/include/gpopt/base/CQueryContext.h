//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CQueryContext.h
//
//	@doc:
//		A container for query-specific input objects to the optimizer
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CQueryContext_H
#define GPOPT_CQueryContext_H

#include "gpos/base.h"

#include "gpopt/base/CReqdPropRelational.h"
#include "gpopt/base/CReqdPropPlan.h"
#include "gpopt/search/CGroupExpression.h"
#include "gpopt/operators/CExpressionPreprocessor.h"


namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CQueryContext
	//
	//	@doc:
	//		Query context object
	//
	//---------------------------------------------------------------------------
	class CQueryContext
	{

		private:

			// memory pool
			IMemoryPool *m_pmp;

			// required plan properties in optimizer's produced plan
			CReqdPropPlan *m_prpp;

			// required array of output columns
			DrgPcr *m_pdrgpcr;

			// required system columns, collected from of output columns
			DrgPcr *m_pdrgpcrSystemCols;

			// array of output column names
			DrgPmdname *m_pdrgpmdname;

			// logical expression tree to be optimized
			CExpression *m_pexpr;

			// should statistics derivation take place
			BOOL m_fDeriveStats;

			// collect system columns from output columns
			void SetSystemCols(IMemoryPool *pmp);

			// return top level operator in the given expression
			static
			COperator *PopTop(CExpression *pexpr);

			// private copy ctor
			CQueryContext(const CQueryContext &);

		public:

			// ctor
			CQueryContext
				(
				IMemoryPool *pmp,
				CExpression *pexpr,
				CReqdPropPlan *prpp,
				DrgPcr *pdrgpcr,
				DrgPmdname *pdrgpmdname,
				BOOL fDeriveStats
				);

			// dtor
			virtual
			~CQueryContext();

			BOOL FDeriveStats() const
			{
				return m_fDeriveStats;
			}

			// expression accessor
			CExpression *Pexpr() const
			{
				return m_pexpr;
			}

			// required plan properties accessor
			CReqdPropPlan *Prpp() const
			{
				return m_prpp;
			}
			
			// return the array of output column references
			DrgPcr *PdrgPcr() const
			{
				return m_pdrgpcr;
			}

			// system columns
			DrgPcr *PdrgpcrSystemCols() const
			{
				return m_pdrgpcrSystemCols;
			}

			// return the array of output column names
			DrgPmdname *Pdrgpmdname() const
			{
				return m_pdrgpmdname;
			}

			// generate the query context for the given expression and array of output column ref ids
			static
			CQueryContext *PqcGenerate
							(
							IMemoryPool *pmp, // memory pool
							CExpression *pexpr, // expression representing the query
							DrgPul *pdrgpulQueryOutputColRefId, // array of output column reference id
							DrgPmdname *pdrgpmdname, // array of output column names
							BOOL fDeriveStats
							);

#ifdef GPOS_DEBUG
			// debug print
			virtual
			IOstream &OsPrint(IOstream &) const;
#endif // GPOS_DEBUG

			// walk the expression and add the mapping between computed column
			// and their corresponding used column(s)
			static
			void MapComputedToUsedCols(CColumnFactory *pcf, CExpression *pexpr);

	}; // class CQueryContext
}


#endif // !GPOPT_CQueryContext_H

// EOF
