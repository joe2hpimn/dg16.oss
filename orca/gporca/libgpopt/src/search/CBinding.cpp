//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CBinding.cpp
//
//	@doc:
//		Implementation of Binding structure
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------

#include "gpopt/operators/CPattern.h"
#include "gpopt/search/CBinding.h"
#include "gpopt/search/CGroupProxy.h"
#include "gpopt/search/CMemo.h"


#include "gpos/base.h"

using namespace gpopt;

//---------------------------------------------------------------------------
//	@function:
//		CBinding::PgexprNext
//
//	@doc:
//		Move cursor within a group (initialize if NULL)
//
//---------------------------------------------------------------------------
CGroupExpression *
CBinding::PgexprNext
	(
	CGroup *pgroup,
	CGroupExpression *pgexpr
	)
	const
{
	CGroupProxy gp(pgroup);

	if (pgroup->FScalar())
	{
		// initialize
		if (NULL == pgexpr)
		{
			return gp.PgexprFirst();
		}

		return gp.PgexprNext(pgexpr);
	}

	// for non-scalar group, we only consider logical expressions in bindings
	return gp.PgexprNextLogical(pgexpr);
}


//---------------------------------------------------------------------------
//	@function:
//		CBinding::PexprExpandPattern
//
//	@doc:
//		Pattern operators which match more than one operator need to be
//		passed around;
//		Given the pattern determine if we need to re-use the pattern operators;
//
//---------------------------------------------------------------------------
CExpression *
CBinding::PexprExpandPattern
	(
	CExpression *pexprPattern,
	ULONG ulPos,
	ULONG ulArity
	)
{
	GPOS_ASSERT_IMP
		(
		pexprPattern->Pop()->FPattern(),
		!CPattern::PopConvert(pexprPattern->Pop())->FLeaf()
		);

	// re-use tree pattern
	if (COperator::EopPatternTree == pexprPattern->Pop()->Eopid() ||
		COperator::EopPatternMultiTree == pexprPattern->Pop()->Eopid())
	{
		return pexprPattern;
	}

	// re-use first child if it is a multi-leaf/tree
	if (0 < pexprPattern->UlArity() &&
		CPattern::FMultiNode((*pexprPattern)[0]->Pop()))
	{
		GPOS_ASSERT(pexprPattern->UlArity() <= 2);

		if (ulPos == ulArity - 1)
		{
			// special-case last child
			return (*pexprPattern)[pexprPattern->UlArity() - 1];
		}
		
		// otherwise re-use multi-leaf/tree child
		return (*pexprPattern)[0];
	}
	GPOS_ASSERT(pexprPattern->UlArity() > ulPos);

	return (*pexprPattern)[ulPos];
}


//---------------------------------------------------------------------------
//	@function:
//		CBinding::PexprFinalize
//
//	@doc:
//		Assemble expression; substitute operator with pattern as necessary
//
//---------------------------------------------------------------------------
CExpression *
CBinding::PexprFinalize
	(
	IMemoryPool *pmp,
	CGroupExpression *pgexpr,
	DrgPexpr *pdrgpexpr
	)
{
	COperator *pop = pgexpr->Pop();
	
	pop->AddRef();
	CExpression *pexpr = GPOS_NEW(pmp) CExpression(pmp, pop, pgexpr, pdrgpexpr, NULL /*pstatsInput*/);
	
	return pexpr;
}


//---------------------------------------------------------------------------
//	@function:
//		CBinding::PexprExtract
//
//	@doc:
//		Extract a binding according to a given pattern;
//		Keep root node fixed;
//
//---------------------------------------------------------------------------
CExpression *
CBinding::PexprExtract
	(
	IMemoryPool *pmp,
	CGroupExpression *pgexpr,
	CExpression *pexprPattern,
	CExpression *pexprLast
	)
{
	GPOS_CHECK_ABORT;

	if (!pexprPattern->FMatchPattern(pgexpr))
	{
		// shallow matching fails
		return NULL;
	}
	
	// the previously extracted pattern must have the same root
	GPOS_ASSERT_IMP(NULL != pexprLast, pexprLast->Pgexpr() == pgexpr);

	COperator *popPattern = pexprPattern->Pop();
	if (popPattern->FPattern() && CPattern::PopConvert(popPattern)->FLeaf())
	{
		// return immediately; no deep extraction for leaf patterns
		pgexpr->Pop()->AddRef();
		return GPOS_NEW(pmp) CExpression(pmp, pgexpr->Pop(), pgexpr);
	}

	DrgPexpr *pdrgpexpr = NULL;
	ULONG ulArity = pgexpr->UlArity();
	if (0 == ulArity && NULL != pexprLast)
	{
		// no more bindings
		return NULL;
	}
	else
	{
		// attempt binding to children
		pdrgpexpr = GPOS_NEW(pmp) DrgPexpr(pmp);
		if (!FExtractChildren(pmp, pgexpr, pexprPattern, pexprLast, pdrgpexpr))
		{
			pdrgpexpr->Release();
			return NULL;
		}
	}					

	CExpression *pexpr = PexprFinalize(pmp, pgexpr, pdrgpexpr);
	GPOS_ASSERT(NULL != pexpr);
	
	return pexpr;
}


//---------------------------------------------------------------------------
//	@function:
//		CBinding::FInitChildCursors
//
//	@doc:
//		Initialize cursors of child expressions
//
//---------------------------------------------------------------------------
BOOL
CBinding::FInitChildCursors
	(
	IMemoryPool *pmp,
	CGroupExpression *pgexpr,
	CExpression *pexprPattern,
	DrgPexpr *pdrgpexpr
	)
{
	GPOS_ASSERT(NULL != pexprPattern);
	GPOS_ASSERT(NULL != pdrgpexpr);

	const ULONG ulArity = pgexpr->UlArity();

	// grab first expression from each cursor
	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		CGroup *pgroup = (*pgexpr)[ul];
		CExpression *pexprPatternChild = PexprExpandPattern(pexprPattern, ul, ulArity);
		CExpression *pexprNewChild =
			PexprExtract(pmp, pgroup, pexprPatternChild, NULL /*pexprLastChild*/);

		if (NULL == pexprNewChild)
		{
			// failure means we have no more expressions
			return false;
		}

		pdrgpexpr->Append(pexprNewChild);
	}

	return true;
}


//---------------------------------------------------------------------------
//	@function:
//		CBinding::FAdvanceChildCursors
//
//	@doc:
//		Advance cursors of child expressions and populate the given array
//		with the next child expressions
//
//---------------------------------------------------------------------------
BOOL
CBinding::FAdvanceChildCursors
	(
	IMemoryPool *pmp,
	CGroupExpression *pgexpr,
	CExpression *pexprPattern,
	CExpression *pexprLast,
	DrgPexpr *pdrgpexpr
	)
{
	GPOS_ASSERT(NULL != pexprPattern);
	GPOS_ASSERT(NULL != pdrgpexpr);

	const ULONG ulArity = pgexpr->UlArity();
	if (NULL == pexprLast)
	{
		// first call, initialize cursors
		return FInitChildCursors(pmp, pgexpr, pexprPattern, pdrgpexpr);
	}

	// could we advance a child's cursor?
	BOOL fCursorAdvanced = false;

	// number of exhausted cursors
	ULONG ulExhaustedCursors = 0;

	for (ULONG ul = 0; ul < ulArity; ul++)
	{
		CGroup *pgroup = (*pgexpr)[ul];
		CExpression *pexprPatternChild = PexprExpandPattern(pexprPattern, ul, ulArity);
		CExpression *pexprNewChild = NULL;

		if (fCursorAdvanced)
		{
			// re-use last extracted child expression
			(*pexprLast)[ul]->AddRef();
			pexprNewChild = (*pexprLast)[ul];
		}
		else
		{
			CExpression *pexprLastChild = (*pexprLast)[ul];
			GPOS_ASSERT(pgroup == pexprLastChild->Pgexpr()->Pgroup());

			// advance current cursor
			pexprNewChild = PexprExtract(pmp, pgroup, pexprPatternChild, pexprLastChild);

			if (NULL == pexprNewChild)
			{
				// cursor is exhausted, we need to reset it
				pexprNewChild = PexprExtract(pmp, pgroup, pexprPatternChild, NULL /*pexprLastChild*/);
				ulExhaustedCursors++;
			}
			else
			{
				// advancing current cursor has succeeded
				fCursorAdvanced =  true;
			}
		}
		GPOS_ASSERT(NULL != pexprNewChild);

		pdrgpexpr->Append(pexprNewChild);
	}

	GPOS_ASSERT(ulExhaustedCursors <= ulArity);


	return ulExhaustedCursors < ulArity;
}


//---------------------------------------------------------------------------
//	@function:
//		CBinding::FExtractChildren
//
//	@doc:
//		For a given root, extract children into a dynamic array;
//		Allocates the array for the children as needed;
//
//---------------------------------------------------------------------------
BOOL
CBinding::FExtractChildren
	(
	IMemoryPool *pmp,
	CGroupExpression *pgexpr,
	CExpression *pexprPattern,
	CExpression *pexprLast,
	DrgPexpr *pdrgpexpr
	)
{
	GPOS_CHECK_STACK_SIZE;
	GPOS_CHECK_ABORT;

	GPOS_ASSERT(NULL != pexprPattern);
	GPOS_ASSERT(NULL != pdrgpexpr);
	GPOS_ASSERT_IMP
		(
		pexprPattern->Pop()->FPattern(),
		!CPattern::PopConvert(pexprPattern->Pop())->FLeaf()
		);
	GPOS_ASSERT(pexprPattern->FMatchPattern(pgexpr));

	ULONG ulArity = pgexpr->UlArity();
	if (ulArity < pexprPattern->UlArity())
	{
		// does not have enough children
		return false;
	}

	if (0 == ulArity)
	{
		GPOS_ASSERT(0 == pexprPattern->UlArity());
		return true;
	}
	

	return FAdvanceChildCursors(pmp, pgexpr, pexprPattern, pexprLast, pdrgpexpr);
}


//---------------------------------------------------------------------------
//	@function:
//		CBinding::PexprExtract
//
//	@doc:
//		Extract a binding according to a given pattern;
//		If no appropriate child pattern can be matched advance the root node
//		until group is exhausted;
//
//---------------------------------------------------------------------------
CExpression *
CBinding::PexprExtract
	(
	IMemoryPool *pmp,
	CGroup *pgroup,
	CExpression *pexprPattern,
	CExpression *pexprLast
	)
{
	GPOS_CHECK_STACK_SIZE;
	GPOS_CHECK_ABORT;

	GPOS_ASSERT(NULL != pgroup);
	GPOS_ASSERT(NULL != pexprPattern);

	CGroupExpression *pgexpr = NULL;
	if (NULL != pexprLast)
	{
		pgexpr = pexprLast->Pgexpr();
	}
	else
	{
		// init cursor
		pgexpr = PgexprNext(pgroup, NULL);
	}
	GPOS_ASSERT(NULL != pgexpr);
	
	COperator *popPattern = pexprPattern->Pop();
	if (popPattern->FPattern() && CPattern::PopConvert(popPattern)->FLeaf())
	{
		// for leaf patterns, we do not iterate on group expressions
		if (NULL != pexprLast)
		{
			// if a leaf was extracted before, then group is exhausted
			return NULL;
		}

		return PexprExtract(pmp, pgexpr, pexprPattern, pexprLast);
	}

	// start position for next binding
	CExpression *pexprStart = pexprLast;
	do 
	{
		if (pexprPattern->FMatchPattern(pgexpr))
		{
			CExpression *pexprResult =
				PexprExtract(pmp, pgexpr, pexprPattern, pexprStart);
			if (NULL != pexprResult)
			{
				return pexprResult;
			}
		}
		
		// move cursor and reset start position
		pgexpr = PgexprNext(pgroup, pgexpr);
		pexprStart = NULL;

		GPOS_CHECK_ABORT;
	}
	while (NULL != pgexpr);

	// group exhausted
	return NULL;
}


// EOF
