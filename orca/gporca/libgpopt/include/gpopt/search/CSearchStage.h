//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CSearchStage.h
//
//	@doc:
//		Search stage
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CSearchStage_H
#define GPOPT_CSearchStage_H

#include "gpos/base.h"
#include "gpos/common/CTimerUser.h"
#include "gpos/common/CDynamicPtrArray.h"

#include "gpopt/xforms/CXform.h"


namespace gpopt
{
	using namespace gpos;

	// forward declarations
	class CSearchStage;
	class CExpression;

	// definition of array of search stages
	typedef CDynamicPtrArray<CSearchStage, CleanupDelete> DrgPss;


	//---------------------------------------------------------------------------
	//	@class:
	//		CSearchStage
	//
	//	@doc:
	//		Search stage
	//
	//---------------------------------------------------------------------------
	class CSearchStage
	{

		private:

			// set of xforms to be applied during stage
			CXformSet *m_pxfs;

			// time threshold in milliseconds
			ULONG m_ulTimeThreshold;

			// cost threshold
			CCost m_costThreshold;

			// best plan found at the end of search stage
			CExpression *m_pexprBest;

			// cost of best plan found
			CCost m_costBest;

			// elapsed time
			CTimerUser m_timer;

		public:

			// ctor
			CSearchStage
				(
				CXformSet *pxfs,
				ULONG ulTimeThreshold = ULONG_MAX,
				CCost costThreshold = CCost(0.0)
				);

			// dtor
			virtual
			~CSearchStage();

			// restart timer
			void RestartTimer()
			{
				m_timer.Restart();
			}

			// is search stage timed-out?
			BOOL FTimedOut() const
			{
				return m_timer.UlElapsedMS() > m_ulTimeThreshold;
			}

			// return elapsed time (in millseconds) since timer was last restarted
			ULONG UlElapsedTime() const
			{
				return m_timer.UlElapsedMS();
			}

			BOOL FAchievedReqdCost() const
			{
				return (NULL != m_pexprBest && m_costBest <= m_costThreshold);
			}

			// xforms set accessor
			CXformSet *Pxfs() const
			{
				return m_pxfs;
			}

			// time threshold accessor
			ULONG UlTimeThreshold() const
			{
				return m_ulTimeThreshold;
			}

			// cost threshold accessor
			CCost CostThreshold() const
			{
				return m_costThreshold;
			}

			// set best plan found at the end of search stage
			void SetBestExpr(CExpression *pexpr);

			// best plan found accessor
			CExpression *PexprBest() const
			{
				return m_pexprBest;
			}

			// best plan cost accessor
			CCost CostBest() const
			{
				return m_costBest;
			}

			// print function
			virtual
			IOstream &OsPrint(IOstream &);

			// generate default search strategy
			static
			DrgPss *PdrgpssDefault(IMemoryPool *pmp);

	};

	// shorthand for printing
	inline
	IOstream &operator << (IOstream &os, CSearchStage &ss)
	{
		return ss.OsPrint(os);
	}

}

#endif // !GPOPT_CSearchStage_H


// EOF
