//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2008 Greenplum, Inc.
//
//	@filename:
//		CGroupProxy.h
//
//	@doc:
//		Lock mechanism for access to a given group
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CGroupProxy_H
#define GPOPT_CGroupProxy_H

#include "gpos/base.h"

#include "gpopt/search/CGroup.h"

namespace gpopt
{
	using namespace gpos;
	
	// forward declarations
	class CGroupExpression;
	class CDrvdProp;
	class COptimizationContext;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CGroupProxy
	//
	//	@doc:
	//		Exclusive access to a given group
	//
	//---------------------------------------------------------------------------
	class CGroupProxy
	{	

		private:

			// group we're operating on
			CGroup *m_pgroup;

			// skip group expressions starting from the given expression;
			CGroupExpression *PgexprSkip(CGroupExpression *pgexprStart, BOOL fSkipLogical);

		public:
		
			// ctor
			explicit
			CGroupProxy(CGroup *pgroup);
			
			// dtor
			~CGroupProxy();

			// set group id
			void SetId(ULONG ulId)
			{
				m_pgroup->SetId(ulId);
			}

			// set group state
			void SetState
				(
				CGroup::EState estNewState
				)
			{
				m_pgroup->SetState(estNewState);
			}

			// set hash join keys
			void SetHashJoinKeys
				(
				DrgPexpr *pdrgpexprOuter,
				DrgPexpr *pdrgpexprInner
				)
			{
				m_pgroup->SetHashJoinKeys(pdrgpexprOuter, pdrgpexprInner);
			}

			// insert group expression
			void Insert(CGroupExpression *pgexpr);

			// move duplicate group expression to duplicates list
			void MoveDuplicateGExpr(CGroupExpression *pgexpr);

			// initialize group's properties;
			void InitProperties(CDrvdProp *pdp);

			// initialize group's stat;
			void InitStats(IStatistics *pstats);

			// retrieve first group expression
			CGroupExpression *PgexprFirst();
			
			// retrieve next group expression
			CGroupExpression *PgexprNext(CGroupExpression *pgexpr);

			// get the first non-logical group expression following the given expression
			CGroupExpression *PgexprSkipLogical(CGroupExpression *pgexpr);

			// get the next logical group expression following the given expression
			CGroupExpression *PgexprNextLogical(CGroupExpression *pgexpr);

			// lookup best expression under optimization context
			CGroupExpression *PgexprLookup(COptimizationContext *poc) const;


#ifdef GPOS_DEBUG
			// is group transition to given state complete?
			BOOL FTransitioned(CGroup::EState estate) const
			{
				return
					(CGroup::estExplored == estate && m_pgroup->FExplored()) ||
					(CGroup::estImplemented == estate && m_pgroup->FImplemented()) ||
					(CGroup::estOptimized == estate && m_pgroup->FOptimized());
			}
#endif // GPOS_DEBUG

	}; // class CGroupProxy
	
}

#endif // !GPOPT_CGroupProxy_H


// EOF
