//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 - 2011 EMC Corp.
//
//	@filename:
//		CEngine.h
//
//	@doc:
//		Optimization engine
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CEngine_H
#define GPOPT_CEngine_H

#include "gpos/base.h"
#include "gpos/sync/CMutex.h"

#include "gpopt/xforms/CXform.h"
#include "gpopt/search/CMemo.h"
#include "gpopt/search/CSearchStage.h"

namespace gpopt
{
	using namespace gpos;


	// forward declarations
	class CGroup;
	class CExpression;
	class CJob;
	class CJobFactory;
	class CPhysical;
	class CQueryContext;
	class COptimizationContext;
	class CReqdPropPlan;
	class CReqdPropRelational;
	class CEnumeratorConfig;

	//---------------------------------------------------------------------------
	//	@class:
	//		CEngine
	//
	//	@doc:
	//		Optimization engine; owns entire optimization workflow
	//
	//---------------------------------------------------------------------------
	class CEngine
	{	

		private:

			// memory pool
			IMemoryPool *m_pmp;
			
			// query context
			CQueryContext *m_pqc;

			// search strategy
			DrgPss *m_pdrgpss;

			// index of current search stage
			ULONG m_ulCurrSearchStage;

			// memo table
			CMemo *m_pmemo;

			//  pattern used for adding enforcers
			CExpression *m_pexprEnforcerPattern;

			// the following variables are used for maintaining optimization statistics

			// set of activated xforms
			CXformSet *m_pxfs;

			// number of calls to each xform
			DrgPulp *m_pdrgpulpXformCalls;

			// time consumed by each xform
			DrgPulp *m_pdrgpulpXformTimes;

			// mutex for locking shared data structures when updating optimization statistics
			CMutex m_mutexOptStats;

#ifdef GPOS_DEBUG

			// a set of internal debugging function used for recursive
			// memo construction

			// apply xforms to group expression and insert results to memo
			void ApplyTransformations
				(
				IMemoryPool *pmpLocal,
				CXformSet *pxfs,
				CGroupExpression *pgexpr
				);

			// transition a given group to a target state
			void TransitionGroup
					(
					IMemoryPool *pmpLocal,
					CGroup *pgroup,
					CGroup::EState estTarget
					);

			// transition a given group expression to a target state
			void TransitionGroupExpression
					(
					IMemoryPool *pmpLocal,
					CGroupExpression *pgexpr,
					CGroupExpression::EState estTarget
					);

			// create optimization context for child group
			COptimizationContext *PocChild
				(
				CGroupExpression *pgexpr, // parent expression
				COptimizationContext *pocOrigin, // optimization context of parent operator
				CExpressionHandle &exprhdlPlan, // handle to compute required plan properties
				CExpressionHandle &exprhdlRel, // handle to compute required relational properties
				DrgPdp *pdrgpdpChildren, // derived plan properties of optimized children
				DrgPstat *pdrgpstatCurrentCtxt,
				ULONG ulChildIndex,
				ULONG ulOptReq
				);

			// optimize child group and return best cost context satisfying required properties
			CCostContext *PccOptimizeChild
				(
				CExpressionHandle &exprhdl,
				CExpressionHandle &exprhdlRel,
				COptimizationContext *pocOrigin,
				DrgPdp *pdrgpdp,
				DrgPstat *pdrgpstatCurrentCtxt,
				ULONG ulChildIndex,
				ULONG ulOptReq
				);

			// optimize child groups of a given group expression
			DrgPoc *PdrgpocOptimizeChildren(CExpressionHandle &exprhdl, COptimizationContext *pocOrigin, ULONG ulOptReq);

			// optimize group expression under a given context
			void OptimizeGroupExpression(CGroupExpression *pgexpr, COptimizationContext *poc);

			// optimize group under a given context
			CGroupExpression * PgexprOptimize(CGroup *pgroup, COptimizationContext *poc, CGroupExpression *pgexprOrigin);
#endif // GPOS_DEBUG

			// initialize query logical expression
			void InitLogicalExpression(CExpression *pexpr);

			// insert children of the given expression to memo, and
			// copy the resulting groups to the given group array
			void InsertExpressionChildren
					(
					CExpression *pexpr,
					DrgPgroup *pdrgpgroupChildren,
					CXform::EXformId exfidOrigin,
					CGroupExpression *pgexprOrigin
					);

			// create and schedule the main optimization job
			void ScheduleMainJob(CSchedulerContext *psc, COptimizationContext *poc);

			// build memo using multiple threads
			void MultiThreadedOptimize(ULONG ulWorkers = 4);

			// run optimizer on the main thread
			void MainThreadOptimize();

			// print activated xform
			void PrintActivatedXforms(IOstream &os) const;

			// process trace flags after optimization is complete
			void ProcessTraceFlags();

			// check if search has terminated
			BOOL FSearchTerminated() const
			{
				// at least one stage has completed and achieved required cost
				return (NULL != PssPrevious() && PssPrevious()->FAchievedReqdCost());
			}

			// generate random plan id
			ULLONG UllRandomPlanId(ULONG *pulSeed);

			// extract a plan sample and handle exceptions according to enumerator configurations
			BOOL FValidPlanSample(CEnumeratorConfig *pec, ULLONG ullPlanId, CExpression **ppexpr);

			// sample possible plans uniformly
			void SamplePlans();

			// check if all children were successfully optimized
			BOOL FChildrenOptimized(DrgPoc *pdrgpoc);

			// check if ayn of the given property enforcing types prohibits enforcement
			static
			BOOL FProhibited
				(
				CEnfdProp::EPropEnforcingType epetOrder, 
				CEnfdProp::EPropEnforcingType epetDistribution, 
				CEnfdProp::EPropEnforcingType epetRewindability, 
				CEnfdProp::EPropEnforcingType epetPropagation
				);

			// check whether the given memo groups can be marked as duplicates. This is
			// true only if they have the same logical properties
			static
			BOOL FPossibleDuplicateGroups(CGroup *pgroupFst, CGroup *pgroupSnd);

			// check if optimization is possible under the given property enforcing types
			static
			BOOL FOptimize
				(
				CEnfdProp::EPropEnforcingType epetOrder, 
				CEnfdProp::EPropEnforcingType epetDistribution, 
				CEnfdProp::EPropEnforcingType epetRewindability, 
				CEnfdProp::EPropEnforcingType epetPropagation
				);
			
			// check if partition propagation resolver is passed an empty part
			// propagation spec
			static
			BOOL FCheckReqdPartPropagation(CPhysical *pop, CEnfdPartitionPropagation *pepp);

			// unrank the plan with the given 'ullPlanId' from the memo
			CExpression *PexprUnrank(ULLONG ullPlanId);

			// determine if a plan, rooted by given group expression, can be safely pruned based on cost bounds
			// when stats for Dynamic Partition Elimination are derived
			BOOL FSafeToPruneWithDPEStats(CGroupExpression *pgexpr, CReqdPropPlan *prpp, CCostContext *pccChild, ULONG ulChildIndex);

			// print current memory consumption
			IOstream &OsPrintMemoryConsumption(IOstream &os, const CHAR *szHeader) const;

			// inaccessible copy ctor
			CEngine(const CEngine &);
			
		public:
		
			// ctor
			explicit
			CEngine(IMemoryPool *pmp);
						
			// dtor
			~CEngine();

			// initialize engine with a query context and search strategy
			void Init
				(
				CQueryContext *pqc,
				DrgPss *pdrgpss
				);

			// accessor of memo's root group
			CGroup *PgroupRoot() const
			{
				GPOS_ASSERT(NULL != m_pmemo);

				return m_pmemo->PgroupRoot();
			}

			// check if a group is the root one
			BOOL FRoot(CGroup *pgroup) const
			{
				return (PgroupRoot() == pgroup);
			}

			// insert expression tree to memo
			CGroup *PgroupInsert
				(
				CGroup *pgroupTarget,
				CExpression *pexpr,
				CXform::EXformId exfidOrigin,
				CGroupExpression *pgexprOrigin,
				BOOL fIntermediate
				);

			// insert a set of xform results into the memo
			void InsertXformResult
				(
				CGroup *pgroupOrigin,
				CXformResult *pxfres,
				CXform::EXformId exfidOrigin,
				CGroupExpression *pgexprOrigin,
				ULONG ulXformTime
				);

			// add enforcers to the memo
			void AddEnforcers(CGroupExpression *pgexprChild, DrgPexpr *pdrgpexprEnforcers);

			// extract a physical plan from the memo
			CExpression *PexprExtractPlan();

			// check required properties;
			// return false if it's impossible for the operator to satisfy one or more
			BOOL FCheckReqdProps
				(
				CExpressionHandle &exprhdl,
				CReqdPropPlan *prpp,
				ULONG ulOptReq
				);

			// check enforceable properties;
			// return false if it's impossible for the operator to satisfy one or more
			BOOL FCheckEnfdProps
				(
				IMemoryPool *pmp,
				CGroupExpression *pgexpr,
				COptimizationContext *poc,
				ULONG ulOptReq,
				DrgPoc *pdrgpoc
				);

			// check if the given expression has valid cte and partition properties
			// with respect to the given requirements
			BOOL FValidCTEAndPartitionProperties
				(
				IMemoryPool *pmp,
				CExpressionHandle &exprhdl,
				CReqdPropPlan *prpp
				);

#ifdef GPOS_DEBUG
			// apply all exploration xforms
			void Explore();

			// apply all implementation xforms
			void Implement();

			// build memo by recursive construction (used for debugging)
			void RecursiveOptimize();
#endif // GPOS_DEBUG

			// derive statistics
			void DeriveStats(IMemoryPool *pmp);

			// execute operations after exploration completes
			void FinalizeExploration();

			// execute operations after implementation completes
			void FinalizeImplementation();

			// execute operations after search stage completes
			void FinalizeSearchStage();

			// main driver of optimization engine
			void Optimize();
					
			// print memo to output logger
			void Trace()
			{
				m_pmemo->Trace();
			}

			// merge duplicate groups
			void GroupMerge()
			{
				m_pmemo->GroupMerge();
			}

			// query context accessor
			const CQueryContext *Pqc() const
			{
				return m_pqc;
			}

			// return current search stage
			CSearchStage *PssCurrent() const
			{
				return (*m_pdrgpss)[m_ulCurrSearchStage];
			}

			// current search stage index accessor
			ULONG UlCurrSearchStage() const
			{
				return m_ulCurrSearchStage;
			}

			// return previous search stage
			CSearchStage *PssPrevious() const
			{
				if (0 == m_ulCurrSearchStage)
				{
					return NULL;
				}

				return (*m_pdrgpss)[m_ulCurrSearchStage - 1];
			}

			// number of search stages accessor
			ULONG UlSearchStages() const
			{
				return m_pdrgpss->UlLength();
			}

			// set of xforms of current stage
			CXformSet *PxfsCurrentStage() const
			{
				return (*m_pdrgpss)[m_ulCurrSearchStage]->Pxfs();
			}

			// return array of child optimization contexts corresponding to handle requirements
			DrgPoc *PdrgpocChildren(IMemoryPool *pmp, CExpressionHandle &exprhdl);

			// build tree map on memo
			MemoTreeMap *Pmemotmap();

			// reset tree map
			void ResetTreeMap()
			{
				 m_pmemo->ResetTreeMap();
			}

			// check if parent group expression can optimize child group expression
			BOOL FOptimizeChild(CGroupExpression *pgexprParent, CGroupExpression *pgexprChild, COptimizationContext *pocChild, EOptimizationLevel eol);

			// determine if a plan, rooted by given group expression, can be safely pruned based on cost bounds
			BOOL FSafeToPrune(CGroupExpression *pgexpr, CReqdPropPlan *prpp, CCostContext *pccChild, ULONG ulChildIndex, CCost *pcostLowerBound);

			// print
			IOstream &
			OsPrint(IOstream&) const;

#ifdef GPOS_DEBUG
			// print root group
			void PrintRoot();

			// print main optimization context and optimal cost context
			void PrintOptCtxts();
#endif // GPOS_DEBUG

			// damp optimization level to process group expressions
			// in the next lower optimization level
			static
			EOptimizationLevel EolDamp(EOptimizationLevel eol);

			// derive statistics
			static
			void DeriveStats(IMemoryPool *pmpLocal, IMemoryPool *pmpGlobal, CGroup *pgroup, CReqdPropRelational *prprel);

			// return the first group expression in a given group
			static
			CGroupExpression *PgexprFirst(CGroup *pgroup);

	}; // class CEngine

	// shorthand for printing
	inline
	IOstream &operator <<
		(
		IOstream &os,
		CEngine &eng
		)
	{
		return eng.OsPrint(os);
	}
}

#endif // !GPOPT_CEngine_H


// EOF
