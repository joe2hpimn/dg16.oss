//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 - 2011 EMC Corp.
//
//	@filename:
//		CGroup.h
//
//	@doc:
//		Group of equivalent expressions in the Memo structure
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CGroup_H
#define GPOPT_CGroup_H

#include "gpos/base.h"
#include "gpos/sync/CMutex.h"

#include "naucrates/statistics/CStatistics.h"

#include "gpos/common/CDynamicPtrArray.h"
#include "gpos/common/CSyncHashtable.h"
#include "gpos/common/CSyncList.h"
#include "gpos/sync/atomic.h"
#include "gpos/sync/CSpinlock.h"

#include "gpopt/spinlock.h"
#include "gpopt/search/CJobQueue.h"
#include "gpopt/operators/CLogical.h"
#include "gpopt/search/CTreeMap.h"

#define GPOPT_INVALID_GROUP_ID	ULONG_MAX

namespace gpopt
{
	using namespace gpos;
	using namespace gpnaucrates;
	

	// forward declarations
	class CGroup;
	class CGroupExpression;
	class CDrvdProp;
	class CDrvdPropCtxtPlan;
	class CGroupProxy;
	class COptimizationContext;
	class CCostContext;
	class CReqdPropPlan;
	class CReqdPropRelational;
	class CExpression;

	// type definitions
	// array of groups
	typedef CDynamicPtrArray<CGroup, CleanupNULL> DrgPgroup;
	
	// map required plan props to cost lower bound of corresponding plan
	typedef CHashMap<CReqdPropPlan, CCost, CReqdPropPlan::UlHashForCostBounding, CReqdPropPlan::FEqualForCostBounding,
					CleanupRelease<CReqdPropPlan>, CleanupDelete<CCost> > CostMap;

	// optimization levels in ascending order,
	// under a given optimization context, group expressions in higher levels
	// must be optimized before group expressions in lower levels,
	// a group expression sets its level in CGroupExpression::SetOptimizationLevel()
	enum EOptimizationLevel
	{
		EolLow = 0,		// low optimization level, this is the default level
		EolHigh,		// high optimization level

		EolSentinel
	};
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CGroup
	//
	//	@doc:
	//		Group of equivalent expressions in the Memo structure
	//
	//---------------------------------------------------------------------------
	class CGroup : public CRefCount
	{	

		friend class CGroupProxy;

		public:

			// type definition of optimization context hash table
			typedef
				CSyncHashtable<
					COptimizationContext, // entry
					COptimizationContext, // search key
					CSpinlockOC> ShtOC;

			// states of a group
			enum EState
			{
				estUnexplored,		// initial state

				estExploring,		// ongoing exploration
				estExplored,		// done exploring

				estImplementing, 	// ongoing implementation
				estImplemented,		// done implementing

				estOptimizing,		// ongoing optimization
				estOptimized,		// done optimizing

				estSentinel
			};


		private:

			// definition of hash table iter
			typedef
				CSyncHashtableIter<
					COptimizationContext, // entry
					COptimizationContext , // search key
					CSpinlockOC> ShtIter;

			// definition of hash table iter accessor
			typedef
				CSyncHashtableAccessByIter<
					COptimizationContext, // entry
					COptimizationContext, // search key
					CSpinlockOC> ShtAccIter;

			// definition of hash table accessor
			typedef
				CSyncHashtableAccessByKey<
					COptimizationContext, // entry
					COptimizationContext, // search key
					CSpinlockOC> ShtAcc;

			//---------------------------------------------------------------------------
			//	@class:
			//		SContextLink
			//
			//	@doc:
			//		Internal structure to remember processed links in plan enumeration
			//
			//---------------------------------------------------------------------------
			struct SContextLink
			{
				private:

					// cost context in a parent group
					CCostContext *m_pccParent;

					// index used when treating current group as a child of group expression
					ULONG m_ulChildIndex;

					// optimization context used to locate group expressions in
					// current group to be linked with parent group expression
					COptimizationContext *m_poc;

				public:

					// ctor
					SContextLink(CCostContext *pccParent, ULONG ulChildIndex, COptimizationContext *poc);

					// dtor
					virtual
					~SContextLink();

					// hash function
					static
					ULONG UlHash(const SContextLink *pclink);

					// equality function
					static
					BOOL FEqual
						(
						const SContextLink *pclink1,
						const SContextLink *pclink2
						);

			}; // struct SContextLink

			// map of processed links in TreeMap structure
			typedef CHashMap<SContextLink, BOOL, SContextLink::UlHash, SContextLink::FEqual,
							CleanupDelete<SContextLink>, CleanupDelete<BOOL> > LinkMap;

			// map of computed stats objects during costing
			typedef CHashMap<COptimizationContext, IStatistics, COptimizationContext::UlHashForStats, COptimizationContext::FEqualForStats,
							CleanupRelease<COptimizationContext>, CleanupRelease<IStatistics> > StatsMap;

			// memory pool
			IMemoryPool *m_pmp;

			// id is used when printing memo contents
			ULONG m_ulId;

			// true if group hold scalar expressions
			BOOL m_fScalar;

			// hash join keys for outer child (only for scalar groups)
			DrgPexpr *m_pdrgpexprHashJoinKeysOuter;

			// hash join keys for inner child (only for scalar groups)
			DrgPexpr *m_pdrgpexprHashJoinKeysInner;

			// list of group expressions
			CList<CGroupExpression> m_listGExprs;

			// list of duplicate group expressions identified by group merge
			CList<CGroupExpression> m_listDupGExprs;

			// group derived properties
			CDrvdProp *m_pdp;

			// group stats
			IStatistics *m_pstats;

			// scalar expression for stat derivation
			CExpression *m_pexprScalar;

			// dummy cost context used in scalar groups for plan enumeration
			CCostContext *m_pccDummy;

			// pointer to group containing the group expressions
			// of all duplicate groups
			CGroup *m_pgroupDuplicate;

			// map of processed links
			LinkMap *m_plinkmap;

			// map of computed stats during costing
			StatsMap *m_pstatsmap;

			// mutex for locking stats map when adding a new entry
			CMutex m_mutexStats;


			// hashtable of optimization contexts
			ShtOC m_sht;

			// spin lock to protect operations on expression list
			CSpinlockGroup m_slock;

			// number of group expressions
			ULONG m_ulGExprs;

			// map of cost lower bounds
			CostMap *m_pcostmap;

			// number of optimization contexts
			volatile ULONG_PTR m_ulpOptCtxts;

			// current state
			EState m_estate;

			// maximum optimization level of member group expressions
			EOptimizationLevel m_eolMax;

			// were new logical operators added to the group?
			BOOL m_fHasNewLogicalOperators;

			// the id of the CTE producer (if any)
			ULONG m_ulCTEProducerId;

			// does the group have any CTE consumer
			BOOL m_fCTEConsumer;

			// exploration job queue
			CJobQueue m_jqExploration;

			// implementation job queue
			CJobQueue m_jqImplementation;

			// private copy ctor
			CGroup(const CGroup&);

			// cleanup optimization contexts on destruction
			void CleanupContexts();

			// increment number of optimization contexts
			ULONG_PTR UlpIncOptCtxts()
			{
				return UlpExchangeAdd(&m_ulpOptCtxts, 1);
			}

			// the following functions are only accessed through group proxy

			// setter of group id
			void SetId(ULONG ulId);

			// setter of group state
			void SetState(EState estNewState);

			// set hash join keys
			void SetHashJoinKeys(DrgPexpr *pdrgpexprOuter, DrgPexpr *pdrgpexprInner);

			// insert new group expression
			void Insert(CGroupExpression *pgexpr);

			// move duplicate group expression to duplicates list
			void MoveDuplicateGExpr(CGroupExpression *pgexpr);

			// initialize group's properties
			void InitProperties(CDrvdProp *pdp);

			// initialize group's stats
			void InitStats(IStatistics *pstats);

			// retrieve first group expression
			CGroupExpression *PgexprFirst();

			// retrieve next group expression
			CGroupExpression *PgexprNext(CGroupExpression *pgexpr);

			// return true if first promise is better than second promise
			BOOL FBetterPromise
				(
				IMemoryPool *pmp,
				CLogical::EStatPromise espFst,
				CGroupExpression *pgexprFst,
				CLogical::EStatPromise espSnd,
				CGroupExpression *pgexprSnd
				)
				const;

			// derive stats recursively on child groups
			CLogical::EStatPromise EspDerive
				(
				IMemoryPool *pmpLocal,
				IMemoryPool *pmpGlobal,
				CGroupExpression *pgexpr,
				CReqdPropRelational *prprel,
				DrgPstat *pdrgpstatCtxt,
				BOOL fDeriveChildStats
				);

			// reset computed stats
			void ResetStats();

			// helper function to add links in child groups
			void RecursiveBuildTreeMap
				(
				IMemoryPool *pmp,
				COptimizationContext *poc,
				CCostContext *pccParent,
				CGroupExpression *pgexprCurrent,
				ULONG ulChildIndex,
				CTreeMap<CCostContext, CExpression, CDrvdPropCtxtPlan, CCostContext::UlHash, CCostContext::FEqual> *ptmap
				);

			// print scalar group properties
			IOstream &OsPrintGrpScalarProps(IOstream &os, const CHAR *szPrefix);

			// print group properties
			IOstream &OsPrintGrpProps(IOstream &os, const CHAR *szPrefix);

			// print group optimization contexts
			IOstream &OsPrintGrpOptCtxts(IOstream &os, const CHAR *szPrefix);

			// initialize and return empty stats for this group
			IStatistics *PstatsInitEmpty(IMemoryPool *pmpGlobal);

			// find the group expression having the best stats promise
			CGroupExpression *PgexprBestPromise
				(
				IMemoryPool *pmpLocal,
				IMemoryPool *pmpGlobal,
				CReqdPropRelational *prprelInput,
				DrgPstat *pdrgpstatCtxt
				);

		public:

			// ctor
			CGroup(IMemoryPool *pmp, BOOL fScalar = false);
			
			// dtor
			~CGroup();
			
			// id accessor
			ULONG UlId() const
			{
				return m_ulId;
			}
			
			// group properties accessor
			CDrvdProp *Pdp() const
			{
				return m_pdp;
			}

			// group stats accessor
			IStatistics *Pstats() const;

			// attempt initializing stats with the given stat object
			BOOL FInitStats(IStatistics *pstats);

			// append given stats object to group stats
			void AppendStats(IMemoryPool *pmp, IStatistics *pstats);
			
			// accessor of maximum optimization level of member group expressions
			EOptimizationLevel EolMax() const
			{
				return m_eolMax;
			}

			// does group hold scalar expressions ?
			BOOL FScalar() const
			{
				return m_fScalar;
			}

			// hash join keys of outer child
			DrgPexpr *PdrgpexprHashJoinKeysOuter() const
			{
				return m_pdrgpexprHashJoinKeysOuter;
			}

			// hash join keys of inner child
			DrgPexpr *PdrgpexprHashJoinKeysInner() const
			{
				return m_pdrgpexprHashJoinKeysInner;
			}

			// return cached scalar expression
			CExpression *PexprScalar() const
			{
				return m_pexprScalar;
			}

			// return dummy cost context for scalar group
			CCostContext *PccDummy() const
			{
				GPOS_ASSERT(FScalar());

				return m_pccDummy;
			}

			// hash function
			ULONG UlHash() const;
			
			// number of group expressions accessor
			ULONG UlGExprs() const
			{
				return m_ulGExprs;
			}
			
			// optimization contexts hash table accessor
			ShtOC &Sht()
			{
				return m_sht;
			}

			// exploration job queue accessor
			CJobQueue *PjqExploration()
			{
				return &m_jqExploration;
			}

			// implementation job queue accessor
			CJobQueue *PjqImplementation()
			{
				return &m_jqImplementation;
			}

			// has group been explored?
			BOOL FExplored() const
			{
				return estExplored <= m_estate;
			}

			// has group been implemented?
			BOOL FImplemented() const
			{
				return estImplemented <= m_estate;
			}

			// has group been optimized?
			BOOL FOptimized() const
			{
				return estOptimized <= m_estate;
			}

			// were new logical operators added to the group?
			BOOL FHasNewLogicalOperators() const
			{
				return m_fHasNewLogicalOperators;
			}

			// reset has new logical operators flag
			void ResetHasNewLogicalOperators()
			{
				m_fHasNewLogicalOperators = false;
			}

			// reset group state
			void ResetGroupState();

			// Check if we need to reset computed stats
			BOOL FResetStats();

			// returns true if stats can be derived on this group
			BOOL FStatsDerivable(IMemoryPool *pmp);

			// reset group job queues
			void ResetGroupJobQueues();

			// check if group has duplicates
			BOOL FDuplicateGroup() const
			{
				return NULL != m_pgroupDuplicate;
			}

			// duplicate group accessor
			CGroup *PgroupDuplicate() const
			{
				return m_pgroupDuplicate;
			}

			// resolve master duplicate group;
			// this is the group that will host all expressions in current group after merging
			void ResolveDuplicateMaster();

			// add duplicate group
			void AddDuplicateGrp(CGroup *pgroup);

			// merge group with its duplicate - not thread-safe
			void MergeGroup();

			// lookup a given context in contexts hash table
			COptimizationContext *PocLookup(IMemoryPool *pmp, CReqdPropPlan *prpp, ULONG ulSearchStageIndex);

			// lookup the best context across all stages for the given required properties
			COptimizationContext *PocLookupBest(IMemoryPool *pmp, ULONG ulSearchStages, CReqdPropPlan *prpp);

			// insert given context into contexts hash table
			COptimizationContext *PocInsert(COptimizationContext *poc);

			// update the best group cost under the given optimization context
			void UpdateBestCost(COptimizationContext *poc, CCostContext *pcc);

			// lookup best expression under given optimization context
			CGroupExpression *PgexprBest(COptimizationContext *poc);

			// materialize a scalar expression for stat derivation if this is a scalar group
			void CreateScalarExpression();

			// materialize a dummy cost context attached to the first group expression
			void CreateDummyCostContext();

			// return the CTE producer ID in the group (if any)
			ULONG UlCTEProducerId() const
			{
				return m_ulCTEProducerId;
			}

			// check if there are any CTE producers in the group
			BOOL FHasCTEProducer() const
			{
				return (ULONG_MAX != m_ulCTEProducerId);
			}

			// check if there are any CTE consumers in the group
			BOOL FHasAnyCTEConsumer() const
			{
				return m_fCTEConsumer;
			}
			
			// derive statistics recursively on group
			IStatistics *PstatsRecursiveDerive
				(
				IMemoryPool *pmpLocal,
				IMemoryPool *pmpGlobal,
				CReqdPropRelational *prprel,
				DrgPstat *pdrgpstatCtxt
				);

			// find group expression with best stats promise and the same given children
			CGroupExpression *PgexprBestPromise(IMemoryPool *pmp, CGroupExpression *pgexprToMatch);

			// link parent group expression to group members
			void BuildTreeMap
				(
				IMemoryPool *pmp,
				COptimizationContext *poc,
				CCostContext *pccParent,
				ULONG ulChildIndex,
				CTreeMap<CCostContext, CExpression, CDrvdPropCtxtPlan, CCostContext::UlHash, CCostContext::FEqual> *ptmap
				);

			// reset link map used in plan enumeration
			void ResetLinkMap();

			// retrieve the group expression containing a CTE Consumer operator
			CGroupExpression *PgexprAnyCTEConsumer();

			// compute stats during costing
			IStatistics *PstatsCompute(COptimizationContext *poc, CExpressionHandle &exprhdl, CGroupExpression *pgexpr);

			// compute cost lower bound for the plan satisfying given required properties
			CCost CostLowerBound(IMemoryPool *pmp, CReqdPropPlan *prppInput);

			// matching of pairs of arrays of groups
			static
			BOOL FMatchGroups
				(
				DrgPgroup *pdrgpgroupFst,
				DrgPgroup *pdrgpgroupSnd
				);

			// matching of pairs of arrays of groups while skipping scalar groups
			static
			BOOL FMatchNonScalarGroups
				(
				DrgPgroup *pdrgpgroupFst,
				DrgPgroup *pdrgpgroupSnd
				);

			// determine if a pair of groups are duplicates
			static
			BOOL FDuplicateGroups
				(
				CGroup *pgroupFst,
				CGroup *pgroupSnd
				);

			// print function
			IOstream &OsPrint(IOstream &os);

			// slink for group list in memo
			SLink m_link;


	}; // class CGroup
	
}

#endif // !GPOPT_CGroup_H


// EOF
