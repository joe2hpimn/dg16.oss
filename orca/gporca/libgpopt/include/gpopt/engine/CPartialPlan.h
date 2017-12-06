//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2014 Pivotal Inc.
//
//	@filename:
//		CPartialPlan.h
//
//	@doc:
//
//		A partial plan is a group expression where none (or not all) of its
//		optimal child plans are discovered yet,
//		by assuming the smallest possible cost of unknown child plans, a partial
//		plan's cost gives a lower bound on the cost of the corresponding complete plan,
//		this information is used to prune the optimization search space during branch
//		and bound search
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CPartialPlan_H
#define GPOPT_CPartialPlan_H

#include "gpos/base.h"
#include "gpos/common/CRefCount.h"

#include "gpopt/base/CReqdProp.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CPartialPlan
	//
	//	@doc:
	//		Description of partial plans created during optimization
	//
	//---------------------------------------------------------------------------
	class CPartialPlan : public CRefCount
	{

		private:

			// root group expression
			CGroupExpression *m_pgexpr;

			// required plan properties of root operator
			CReqdPropPlan *m_prpp;

			// cost context of known child plan -- can be null if no child plans are known
			CCostContext *m_pccChild;

			// index of known child plan
			ULONG m_ulChildIndex;

			// private copy ctor
			CPartialPlan(const CPartialPlan &);

			// extract costing info from children
			void ExtractChildrenCostingInfo
				(
				IMemoryPool *pmp,
				ICostModel *pcm,
				CExpressionHandle &exprhdl,
				ICostModel::SCostingInfo *pci
				);

			// raise exception if the stats object is NULL
			void RaiseExceptionIfStatsNull(IStatistics *pstats);

		public:

			// ctor
			CPartialPlan
				(
				CGroupExpression *pgexpr,
				CReqdPropPlan *prpp,
				CCostContext *pccChild,
				ULONG ulChildIndex
				);

			// dtor
			virtual
			~CPartialPlan();

			// group expression accessor
			CGroupExpression *Pgexpr() const
			{
				return m_pgexpr;
			}

			// plan properties accessor
			CReqdPropPlan *Prpp() const
			{
				return m_prpp;
			}

			// child cost context accessor
			CCostContext *PccChild() const
			{
				return m_pccChild;
			}

			// child index accessor
			ULONG UlChildIndex() const
			{
				return m_ulChildIndex;
			}

			// compute partial plan cost
			CCost CostCompute(IMemoryPool *pmp);

			// hash function used for cost bounding
			static
			ULONG UlHash(const CPartialPlan *ppp);

			// equality function used for for cost bounding
			static
			BOOL FEqual(const CPartialPlan *pppFst, const CPartialPlan *pppSnd);

		}; // class CPartialPlan
}


#endif // !GPOPT_CPartialPlan_H

// EOF
