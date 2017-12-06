//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 - 2011 EMC Corp.
//
//	@filename:
//		CMemo.cpp
//
//	@doc:
//		Implementation of Memo structure
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpos/base.h"
#include "gpos/common/CAutoTimer.h"
#include "gpos/common/CSyncHashtableAccessByIter.h"
#include "gpos/common/CSyncHashtableAccessByKey.h"
#include "gpos/io/COstreamString.h"
#include "gpos/string/CWStringDynamic.h"

#include "gpopt/exception.h"

#include "gpopt/base/CDrvdProp.h"
#include "gpopt/base/CDrvdPropCtxtPlan.h"
#include "gpopt/base/CReqdPropPlan.h"
#include "gpopt/base/COptimizationContext.h"
#include "gpopt/base/COptCtxt.h"

#include "gpopt/search/CGroupProxy.h"
#include "gpopt/search/CMemo.h"
#include "gpopt/operators/CExpressionHandle.h"
#include "gpopt/operators/CLogicalCTEProducer.h"

#include "gpopt/engine/CEngine.h"

using namespace gpopt;

#define GPOPT_MEMO_HT_BUCKETS	50000
			
//---------------------------------------------------------------------------
//	@function:
//		CMemo::CMemo
//
//	@doc:
//		Ctor
//
//---------------------------------------------------------------------------
CMemo::CMemo
	(
	IMemoryPool *pmp
	)
	:
	m_pmp(pmp),
	m_pgroupRoot(NULL),
	m_ulpGrps(0),
	m_pmemotmap(NULL)
{
	GPOS_ASSERT(NULL != pmp);

	m_sht.Init
		(
		pmp,
		GPOPT_MEMO_HT_BUCKETS,
		GPOS_OFFSET(CGroupExpression, m_linkMemo),
		0, /*cKeyOffset (0 because we use CGroupExpression class as key)*/
		&(CGroupExpression::m_gexprInvalid),
		CGroupExpression::UlHash,
		CGroupExpression::FEqual
		);

	m_listGroups.Init(GPOS_OFFSET(CGroup, m_link));
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::~CMemo
//
//	@doc:
//		Dtor
//
//---------------------------------------------------------------------------
CMemo::~CMemo()
{
	CGroup *pgroup = m_listGroups.PtFirst();
	while(NULL != pgroup)
	{
		CGroup *pgroupNext = m_listGroups.PtNext(pgroup);
		pgroup->Release();
		
		pgroup = pgroupNext;
	}

	GPOS_DELETE(m_pmemotmap);
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::SetRoot
//
//	@doc:
//		Set root group
//
//---------------------------------------------------------------------------
void
CMemo::SetRoot
	(
	CGroup *pgroup
	)
{
	GPOS_ASSERT(NULL == m_pgroupRoot);
	GPOS_ASSERT(NULL != pgroup);

	m_pgroupRoot = pgroup;
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::Add
//
//	@doc:
//		Add new group to list
//
//---------------------------------------------------------------------------
void
CMemo::Add
	(
	CGroup *pgroup,
	CExpression *pexprOrigin // origin expression that produced the group
	)
{
	GPOS_ASSERT(NULL != pgroup);
	GPOS_ASSERT(NULL != pexprOrigin);
	GPOS_ASSERT(!pexprOrigin->Pop()->FPhysical() && "Physical operators do not create new groups");

	// extract expression props
	CDrvdProp *pdp = NULL;
	if (pexprOrigin->Pop()->FScalar())
	{
		pdp = pexprOrigin->Pdp(CDrvdProp::EptScalar);
	}
	else
	{
		pdp = pexprOrigin->Pdp(CDrvdProp::EptRelational);
	}
	GPOS_ASSERT(NULL != pdp);

	ULONG ulId = m_aul.TIncr();
	pdp->AddRef();
#ifdef GPOS_DEBUG
	CGroupExpression *pgexpr = NULL;
#endif  // GPOS_DEBUG
	{
		CGroupProxy gp(pgroup);
		gp.SetId(ulId);
		gp.InitProperties(pdp);
#ifdef GPOS_DEBUG
		pgexpr = gp.PgexprFirst();
#endif  // GPOS_DEBUG
	}

	GPOS_ASSERT(NULL != pgexpr);
	m_listGroups.Push(pgroup);
	(void) UlpExchangeAdd(&m_ulpGrps, 1);
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::PgroupInsert
//
//	@doc:
//		Helper for inserting group expression in target group
//
//
//---------------------------------------------------------------------------
CGroup *
CMemo::PgroupInsert
	(
	CGroup *pgroupTarget,
	CGroupExpression *pgexpr,
	CExpression *pexprOrigin,
	BOOL fNewGroup
	)
{
	GPOS_ASSERT(NULL != pgroupTarget);
	GPOS_ASSERT(NULL != pgexpr);

	ShtAcc shta(m_sht, *pgexpr);

	// we do a lookup since group expression may have been already inserted
	CGroupExpression *pgexprFound = shta.PtLookup();
	if (NULL == pgexprFound)
	{
		shta.Insert(pgexpr);

		// group proxy scope
		{
			CGroupProxy gp(pgroupTarget);
			gp.Insert(pgexpr);
		}

		if (fNewGroup)
		{
			Add(pgroupTarget, pexprOrigin);
		}

		return pgexpr->Pgroup();
	}

	return pgexprFound->Pgroup();
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::PgroupInsert
//
//	@doc:
//		Helper to check if a new group needs to be created
//
//---------------------------------------------------------------------------
BOOL
CMemo::FNewGroup
	(
	CGroup **ppgroupTarget,
	CGroupExpression *pgexpr,
	BOOL fScalar
	)
{
	GPOS_ASSERT(NULL != ppgroupTarget);

	if (NULL == *ppgroupTarget && NULL == pgexpr)
	{
		*ppgroupTarget = GPOS_NEW(m_pmp) CGroup(m_pmp, fScalar);

		return true;
	}

	return false;
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::PgroupInsert
//
//	@doc:
//		Attempt inserting a group expression in a target group;
//		if group expression is not in the hash table, insertion
//		succeeds and the function returns the input target group;
//		otherwise insertion fails and the function returns the
//		group containing the existing group expression
//
//---------------------------------------------------------------------------
CGroup *
CMemo::PgroupInsert
	(
	CGroup *pgroupTarget,
	CExpression *pexprOrigin,
	CGroupExpression *pgexpr
	)
{
	GPOS_ASSERT(NULL != pgexpr);
	GPOS_CHECK_ABORT;
	GPOS_ASSERT(NULL != pexprOrigin);
	GPOS_ASSERT(pgexpr->Pop() == pexprOrigin->Pop());
	GPOS_ASSERT(pgexpr->UlArity() == pexprOrigin->UlArity());

	CGroup *pgroupContainer = NULL;
	CGroupExpression *pgexprFound = NULL;
	// hash table accessor's scope
	{
		ShtAcc shta(m_sht, *pgexpr);
		pgexprFound = shta.PtLookup();
	}

	// check if we may need to create a new group
	BOOL fNewGroup = FNewGroup(&pgroupTarget, pgexprFound, pgexpr->Pop()->FScalar());
	if (fNewGroup)
	{
		// we may add a new group to Memo, so we derive props here
		(void) pexprOrigin->PdpDerive();
	}

	if (NULL != pgexprFound)
	{
		pgroupContainer = pgexprFound->Pgroup();
	}
	else
	{
		pgroupContainer = PgroupInsert(pgroupTarget, pgexpr, pexprOrigin, fNewGroup);
	}

	// if insertion failed, release group as needed
	if (NULL == pgexpr->Pgroup() && fNewGroup)
	{
		fNewGroup = false;
		pgroupTarget->Release();
	}

	// if a new scalar group is added, we materialize a scalar expression
	// for statistics derivation purposes
	if (fNewGroup && pgroupTarget->FScalar())
	{
		pgroupTarget->CreateScalarExpression();
		pgroupTarget->CreateDummyCostContext();
	}

	return pgroupContainer;
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::PexprExtractPlan
//
//	@doc:
//		Extract a plan that delivers the given required properties
//
//---------------------------------------------------------------------------
CExpression *
CMemo::PexprExtractPlan
	(
	IMemoryPool *pmp,
	CGroup *pgroupRoot,
	CReqdPropPlan *prppInput,
	ULONG ulSearchStages
	)
{
	// check stack size
	GPOS_CHECK_STACK_SIZE;
	GPOS_CHECK_ABORT;

	CGroupExpression *pgexprBest = NULL;
	COptimizationContext *poc = NULL;
	CCost cost = GPOPT_INVALID_COST;
	IStatistics *pstats = NULL;
	if (pgroupRoot->FScalar())
	{
		CGroupProxy gp(pgroupRoot);
		pgexprBest = gp.PgexprFirst();
	}
	else
	{
		poc = pgroupRoot->PocLookupBest(pmp, ulSearchStages, prppInput);
		GPOS_ASSERT(NULL != poc);

		pgexprBest = pgroupRoot->PgexprBest(poc);
		if (NULL != pgexprBest)
		{
			cost = poc->PccBest()->Cost();
			pstats = poc->PccBest()->Pstats();
		}
	}

	if (NULL == pgexprBest)
	{
		// no plan found
		return NULL;
	}

	DrgPexpr *pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
	ULONG ulArity = pgexprBest->UlArity();
	for (ULONG i = 0; i < ulArity; i++)
	{
		CGroup *pgroupChild = (*pgexprBest)[i];
		CReqdPropPlan * prpp = NULL;
		if (!pgroupChild->FScalar())
		{
			COptimizationContext *pocChild = (*poc->PccBest()->Pdrgpoc())[i];
			GPOS_ASSERT(NULL != pocChild);

			prpp = pocChild->Prpp();
		}

		CExpression *pexprChild = PexprExtractPlan(pmp, pgroupChild, prpp, ulSearchStages);
		pdrgpexpr->Append(pexprChild);
	}

	pgexprBest->Pop()->AddRef();
	CExpression *pexpr = GPOS_NEW(pmp) CExpression
							(
							pmp,
							pgexprBest->Pop(),
							pgexprBest,
							pdrgpexpr,
							pstats,
							cost
							);

	if (pexpr->Pop()->FPhysical() && !poc->PccBest()->FValid(pmp))
	{
		GPOS_RAISE(gpopt::ExmaGPOPT, gpopt::ExmiUnsatisfiedRequiredProperties);
	}

	return pexpr;
}


#ifdef GPOS_DEBUG
//---------------------------------------------------------------------------
//	@function:
//		CMemo::Pgroup
//
//	@doc:
//		Get group by id;
//
//---------------------------------------------------------------------------
CGroup *
CMemo::Pgroup
	(
	ULONG ulId
	)
{
	CGroup *pgroup = m_listGroups.PtFirst();

	while (NULL != pgroup)
	{
		if (ulId == pgroup->UlId())
		{
			return pgroup;
		}
		pgroup = m_listGroups.PtNext(pgroup);
	}

	return NULL;
}
#endif // GPOS_DEBUG


//---------------------------------------------------------------------------
//	@function:
//		CMemo::MarkDuplicates
//
//	@doc:
//		Mark groups as duplicates
//
//---------------------------------------------------------------------------
void
CMemo::MarkDuplicates
	(
	CGroup *pgroupFst,
	CGroup *pgroupSnd
	)
{
	GPOS_ASSERT(NULL != pgroupFst);
	GPOS_ASSERT(NULL != pgroupSnd);

	pgroupFst->AddDuplicateGrp(pgroupSnd);
	pgroupFst->ResolveDuplicateMaster();
	pgroupSnd->ResolveDuplicateMaster();
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::FRehash
//
//	@doc:
//		Delete then re-insert all group expressions in memo hash table;
//		we do this at the end of exploration phase since identified
//		duplicate groups during exploration may cause changing hash values
//		of current group expressions,
//		rehashing group expressions using the new hash values allows
//		identifying duplicate group expressions that can be skipped from
//		further processing;
//
//		the function returns TRUE if rehashing resulted in discovering
//		new duplicate groups;
//
//		this function is NOT thread safe, and must not be called while
//		exploration/implementation/optimization is undergoing
//
//
//---------------------------------------------------------------------------
BOOL
CMemo::FRehash()
{
	GPOS_ASSERT(m_pgroupRoot->FExplored());
	GPOS_ASSERT(!m_pgroupRoot->FImplemented());

	// dump memo hash table into a local list
	CList<CGroupExpression> listGExprs;
	listGExprs.Init(GPOS_OFFSET(CGroupExpression, m_linkMemo));

	ShtIter shtit(m_sht);
	CGroupExpression *pgexpr = NULL;
	while (NULL != pgexpr || shtit.FAdvance())
	{
		{
			ShtAccIter shtitacc(shtit);
			pgexpr = shtitacc.Pt();
			if (NULL != pgexpr)
			{
				shtitacc.Remove(pgexpr);
				listGExprs.Append(pgexpr);
			}
		}
		GPOS_CHECK_ABORT;
	}

	// iterate on list and insert non-duplicate group expressions
	// back to memo hash table
	BOOL fNewDupGroups = false;
	while (!listGExprs.FEmpty())
	{
		CGroupExpression *pgexpr = listGExprs.RemoveHead();
		CGroupExpression *pgexprFound = NULL;

		{
			// hash table accessor scope
 			ShtAcc shta(m_sht, *pgexpr);
 			pgexprFound = shta.PtLookup();

 			if (NULL == pgexprFound)
 			{
 				// group expression has no duplicates, insert back to memo hash table
 				shta.Insert(pgexpr);
 				continue;
 			}
		}

		GPOS_ASSERT(pgexprFound != pgexpr);

		// mark duplicate group expression
		pgexpr->SetDuplicate(pgexprFound);
		CGroup *pgroup = pgexpr->Pgroup();

		// move group expression to duplicates list in owner group
		{
			// group proxy scope
			CGroupProxy gp(pgroup);
			gp.MoveDuplicateGExpr(pgexpr);
		}

		// check if we need also to mark duplicate groups
		CGroup *pgroupFound = pgexprFound->Pgroup();
		if (pgroupFound != pgroup)
		{
			CGroup *pgroupDup =  pgroup->PgroupDuplicate();
			CGroup *pgroupFoundDup = pgroupFound->PgroupDuplicate();
			if ((NULL == pgroupDup && NULL == pgroupFoundDup) || (pgroupDup != pgroupFoundDup))
			{
				MarkDuplicates(pgroup, pgroupFound);
				fNewDupGroups = true;
			}
		}

		GPOS_CHECK_ABORT;
	}

	return fNewDupGroups;
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::GroupMerge
//
//	@doc:
//		Merge duplicate groups
//
//---------------------------------------------------------------------------
void
CMemo::GroupMerge()
{
	GPOS_ASSERT(m_pgroupRoot->FExplored());
	GPOS_ASSERT(!m_pgroupRoot->FImplemented());

	CAutoTimer at("\n[OPT]: Group Merge Time", GPOS_FTRACE(EopttracePrintOptStats));

	// keep merging groups until we have no new duplicates
	BOOL fNewDupGroups = true;
	while (fNewDupGroups)
	{
		CGroup *pgroup = m_listGroups.PtFirst();
		while (NULL != pgroup)
		{
			pgroup->MergeGroup();
			pgroup = m_listGroups.PtNext(pgroup);

			GPOS_CHECK_ABORT;
		}

		// check if root has been merged
		if (m_pgroupRoot->FDuplicateGroup())
		{
			m_pgroupRoot = m_pgroupRoot->PgroupDuplicate();
		}

		fNewDupGroups = FRehash();
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::Trace
//
//	@doc:
//		Print memo to output logger
//
//---------------------------------------------------------------------------
void
CMemo::Trace()
{
	CWStringDynamic str(m_pmp);
	COstreamString oss(&str);

	OsPrint(oss);

	GPOS_TRACE(str.Wsz());
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::OsPrint
//
//	@doc:
//		Print function
//
//---------------------------------------------------------------------------
IOstream &
CMemo::OsPrint
	(
	IOstream &os
	)
{
	CGroup *pgroup = m_listGroups.PtFirst();
	
	while (NULL != pgroup)
	{
		CAutoTrace at(m_pmp);

		if (m_pgroupRoot == pgroup)
		{
			at.Os() << std::endl << "ROOT ";
		}
		
		pgroup->OsPrint(at.Os());
		pgroup = m_listGroups.PtNext(pgroup);

		GPOS_CHECK_ABORT;
	}

	return os;
}

//---------------------------------------------------------------------------
//	@function:
//		CMemo::DeriveStatsIfAbsent
//
//	@doc:
//		Derive stats when no stats not present for the group
//
//---------------------------------------------------------------------------
void
CMemo::DeriveStatsIfAbsent
	(
	IMemoryPool *pmpLocal
	)
{
	CGroup *pgroup = m_listGroups.PtFirst();

	while (NULL != pgroup)
	{
		GPOS_ASSERT(!pgroup->FImplemented());
		if (NULL == pgroup->Pstats())
		{
			CGroupExpression *pgexprFirst = CEngine::PgexprFirst(pgroup);

			CExpressionHandle exprhdl(m_pmp);
			exprhdl.Attach(pgexprFirst);
			exprhdl.DeriveStats(pmpLocal, m_pmp, NULL, NULL);
		}

		pgroup = m_listGroups.PtNext(pgroup);

		GPOS_CHECK_ABORT;
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::ResetGroupStates
//
//	@doc:
//		Reset states and job queues of memo groups
//
//---------------------------------------------------------------------------
void
CMemo::ResetGroupStates()
{
	CGroup *pgroup = m_listGroups.PtFirst();

	while (NULL != pgroup)
	{
		pgroup->ResetGroupState();
		pgroup->ResetGroupJobQueues();
		pgroup->ResetHasNewLogicalOperators();

		pgroup = m_listGroups.PtNext(pgroup);
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::ResetStats
//
//	@doc:
//		Reset statistics by recursively descending from root group;
//		we call this function before stats derivation to reset stale stats
//		computed during previous search stages
//
//---------------------------------------------------------------------------
void
CMemo::ResetStats()
{
	(void) m_pgroupRoot->FResetStats();
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::BuildTreeMap
//
//	@doc:
//		Build tree map of member group expressions
//
//---------------------------------------------------------------------------
void
CMemo::BuildTreeMap
	(
	COptimizationContext *poc
	)
{
	GPOS_ASSERT(NULL != poc);
	GPOS_ASSERT(NULL == m_pmemotmap && "tree map is already built");

	m_pmemotmap = GPOS_NEW(m_pmp) MemoTreeMap(m_pmp, CExpression::PexprRehydrate);
	m_pgroupRoot->BuildTreeMap(m_pmp, poc, NULL /*pccParent*/, ULONG_MAX /*ulChildIndex*/, m_pmemotmap);
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::ResetTreeMap
//
//	@doc:
//		Reset tree map
//
//---------------------------------------------------------------------------
void
CMemo::ResetTreeMap()
{
	if (NULL != m_pmemotmap)
	{
		GPOS_DELETE(m_pmemotmap);
		m_pmemotmap = NULL;
	}

	// reset link map of all groups
	CGroup *pgroup = m_listGroups.PtFirst();
	while (NULL != pgroup)
	{
		pgroup->ResetLinkMap();
		pgroup = m_listGroups.PtNext(pgroup);
	}
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::UlDuplicateGroups
//
//	@doc:
//		Return number of duplicate groups
//
//---------------------------------------------------------------------------
ULONG
CMemo::UlDuplicateGroups()
{
	ULONG ulDuplicates = 0;
	CGroup *pgroup = m_listGroups.PtFirst();
	while (NULL != pgroup)
	{
		if (pgroup->FDuplicateGroup())
		{
			ulDuplicates ++;
		}
		pgroup = m_listGroups.PtNext(pgroup);
	}

	return ulDuplicates;
}


//---------------------------------------------------------------------------
//	@function:
//		CMemo::UlGrpExprs
//
//	@doc:
//		Return total number of group expressions
//
//---------------------------------------------------------------------------
ULONG
CMemo::UlGrpExprs()
{
	ULONG ulGExprs = 0;
	CGroup *pgroup = m_listGroups.PtFirst();
	while (NULL != pgroup)
	{
		ulGExprs += pgroup->UlGExprs();
		pgroup = m_listGroups.PtNext(pgroup);
	}

	return ulGExprs;
}

// EOF

