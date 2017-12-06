/*-------------------------------------------------------------------------
 *
 * pquery.c
 *	  POSTGRES process query command code
 *
 * Portions Copyright (c) 2005-2010, Greenplum inc
 * Portions Copyright (c) 1996-2009, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/tcop/pquery.c,v 1.111.2.1 2007/02/18 19:49:30 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/xact.h"
#include "commands/prepare.h"
#include "commands/trigger.h"
#include "cdb/cdbvars.h"
#include "executor/executor.h"          /* ExecutorStart, ExecutorRun, etc */
#include "miscadmin.h"
#include "tcop/pquery.h"
#include "tcop/tcopprot.h"
#include "tcop/utility.h"
#include "utils/memutils.h"
#include "utils/resscheduler.h"
#include "commands/vacuum.h"
#include "commands/tablecmds.h"
#include "commands/queue.h"
#include "utils/lsyscache.h"
#include "nodes/makefuncs.h"
#include "utils/acl.h"
#include "catalog/catalog.h"
#include "postmaster/autovacuum.h"
#include "postmaster/backoff.h"
#include "cdb/ml_ipc.h"
#include "cdb/memquota.h"
#include "executor/spi.h"

/*
 * ActivePortal is the currently executing Portal (the most closely nested,
 * if there are several).
 */
Portal		ActivePortal = NULL;

static void ProcessQuery(Portal portal, /* Resource queueing need SQL, so we pass portal. */
			 PlannedStmt *stmt,
             ParamListInfo params,
			 DestReceiver *dest,
			 char *completionTag);
static void FillPortalStore(Portal portal, bool isTopLevel);
static uint64 RunFromStore(Portal portal, ScanDirection direction, int64 count,
			 DestReceiver *dest);
static int64 PortalRunSelect(Portal portal, bool forward, int64 count,
				DestReceiver *dest);
static void PortalRunUtility(Portal portal, Node *utilityStmt, bool isTopLevel,
				 DestReceiver *dest, char *completionTag);
static void PortalRunMulti(Portal portal, bool isTopLevel,
			   DestReceiver *dest, DestReceiver *altdest,
			   char *completionTag);
static int64 DoPortalRunFetch(Portal portal,
				 FetchDirection fdirection,
				 int64 count,
				 DestReceiver *dest);
static void DoPortalRewind(Portal portal);
static void PortalSetBackoffWeight(Portal portal);

/*
 * CreateQueryDesc
 *
 * N.B. If sliceTable is non-NULL in the top node of plantree, then 
 * nMotionNodes and nParamExec must be set correctly, as well, and
 * the QueryDesc will be arranged so that ExecutorStart and ExecutorRun
 * will handle plan slicing.
 */
QueryDesc *
CreateQueryDesc(PlannedStmt *plannedstmt,
				const char *sourceText,
				Snapshot snapshot,
				Snapshot crosscheck_snapshot,
				DestReceiver *dest,
				ParamListInfo params,
				bool doInstrument)
{
	QueryDesc  *qd = (QueryDesc *) palloc(sizeof(QueryDesc));

	qd->operation = plannedstmt->commandType;	/* operation */
	qd->plannedstmt = plannedstmt;		/* plan */
	qd->utilitystmt = plannedstmt->utilityStmt; /* in case DECLARE CURSOR */
	qd->sourceText = pstrdup(sourceText);		/* query text */
	qd->snapshot = snapshot;	/* snapshot */
	qd->crosscheck_snapshot = crosscheck_snapshot;		/* RI check snapshot */
	qd->dest = dest;			/* output dest */
	qd->params = params;		/* parameter values passed into query */
	qd->doInstrument = doInstrument;	/* instrumentation wanted? */

	/* null these fields until set by ExecutorStart */
	qd->tupDesc = NULL;
	qd->estate = NULL;
	qd->planstate = NULL;
	
	qd->extended_query = false; /* default value */
	qd->portal_name = NULL;

	qd->gpmon_pkt = NULL;
	
    if (Gp_role != GP_ROLE_EXECUTE)
	{
		increment_command_count();

		MyProc->queryCommandId = gp_command_count;
		if (gp_cancel_query_print_log)
		{
			elog(NOTICE, "running query (sessionId, commandId): (%d, %d)",
				 MyProc->mppSessionId, gp_command_count);
		}
	}
	
	if(gp_enable_gpperfmon && Gp_role == GP_ROLE_DISPATCH)
	{
		qd->gpmon_pkt = (gpmon_packet_t *) palloc0(sizeof(gpmon_packet_t));
		gpmon_qlog_packet_init(qd->gpmon_pkt);
	}

	return qd;
}

/*
 * CreateUtilityQueryDesc
 */
QueryDesc *
CreateUtilityQueryDesc(Node *utilitystmt,
					   const char *sourceText,
					   Snapshot snapshot,
					   DestReceiver *dest,
					   ParamListInfo params)
{
	QueryDesc  *qd = (QueryDesc *) palloc(sizeof(QueryDesc));
	
	qd->operation = CMD_UTILITY;	/* operation */
	qd->plannedstmt = NULL;
	qd->utilitystmt = utilitystmt;		/* utility command */
	qd->sourceText = pstrdup(sourceText);		/* query text */
	qd->snapshot = snapshot;	/* snapshot */
	qd->crosscheck_snapshot = InvalidSnapshot;	/* RI check snapshot */
	qd->dest = dest;			/* output dest */
	qd->params = params;		/* parameter values passed into query */
	qd->doInstrument = false;	/* uninteresting for utilities */
	
	/* null these fields until set by ExecutorStart */
	qd->tupDesc = NULL;
	qd->estate = NULL;
	qd->planstate = NULL;
	
	qd->extended_query = false; /* default value */
	qd->portal_name = NULL;
	
	return qd;
}

/*
 * FreeQueryDesc
 */
void
FreeQueryDesc(QueryDesc *qdesc)
{
	/* Can't be a live query */
	Assert(qdesc->estate == NULL);
	/* Only the QueryDesc itself and the sourceText need be freed */
	pfree((void*) qdesc->sourceText);
	pfree(qdesc);
}


/*
 * ProcessQuery
 *		Execute a single plannable query within a PORTAL_MULTI_QUERY
 *		or PORTAL_ONE_RETURNING portal
 *
 *	portal: the portal
 *	plan: the plan tree for the query
 *	sourceText: the source text of the query
 *	params: any parameters needed
 *	dest: where to send results
 *	completionTag: points to a buffer of size COMPLETION_TAG_BUFSIZE
 *		in which to store a command completion status string.
 *
 * completionTag may be NULL if caller doesn't want a status string.
 *
 * Must be called in a memory context that will be reset or deleted on
 * error; otherwise the executor's memory usage will be leaked.
 */
static void
ProcessQuery(Portal portal,
			 PlannedStmt *stmt,
             ParamListInfo params,
			 DestReceiver *dest,
			 char *completionTag)
{
	QueryDesc  *queryDesc;
	Oid			truncOid = InvalidOid;
	
	/* auto-stats related */
	Oid	relationOid = InvalidOid; 	/* relation that is modified */
	AutoStatsCmdType cmdType = AUTOSTATS_CMDTYPE_SENTINEL; 	/* command type */
	
	ereport(DEBUG3,
			(errmsg_internal("ProcessQuery")));

	/*
	 * Must always set snapshot for plannable queries.	Note we assume that
	 * caller will take care of restoring ActiveSnapshot on exit/error.
	 */
	ActiveSnapshot = CopySnapshot(GetTransactionSnapshot());

	/*
	 * Create the QueryDesc object
	 */
	Assert(portal);
	
	if (portal->sourceTag == T_SelectStmt && gp_select_invisible)
		queryDesc = CreateQueryDesc(stmt, portal->sourceText,
									SnapshotAny, InvalidSnapshot,
									dest, params, false);
	else
		queryDesc = CreateQueryDesc(stmt, portal->sourceText,
									ActiveSnapshot, InvalidSnapshot,
									dest, params, false);

	if (gp_enable_gpperfmon && Gp_role == GP_ROLE_DISPATCH)
	{			
		Assert(portal->sourceText);
		gpmon_qlog_query_submit(queryDesc->gpmon_pkt);
		gpmon_qlog_query_text(queryDesc->gpmon_pkt,
				portal->sourceText,
				application_name,
				GetResqueueName(portal->queueId),
				GetResqueuePriority(portal->queueId));
	}

	/*
	 * If resource scheduling is enabled and we are locking non SELECT queries,
	 * or this is a SELECT INTO then lock the portal here. 
	 * Skip if this query is added by the rewriter or
	 * we are superuser.
	 */
	if (Gp_role == GP_ROLE_DISPATCH && 
		ResourceScheduler && 
		(!ResourceSelectOnly || portal->sourceTag == T_SelectStmt) && 
		stmt->canSetTag
		&& !superuser())
	{
		PortalSetStatus(portal, PORTAL_QUEUE);
		
		if (gp_resqueue_memory_policy != RESQUEUE_MEMORY_POLICY_NONE)
			queryDesc->plannedstmt->query_mem = ResourceQueueGetQueryMemoryLimit(queryDesc->plannedstmt, portal->queueId);
		
		portal->releaseResLock = ResLockPortal(portal, queryDesc);
	}

	PortalSetStatus(portal, PORTAL_ACTIVE);
	
	if (gp_resqueue_memory_policy != RESQUEUE_MEMORY_POLICY_NONE
			&& Gp_role == GP_ROLE_DISPATCH
			&& gp_session_id > -1
			&& superuser())
	{
		queryDesc->plannedstmt->query_mem = ResourceQueueGetSuperuserQueryMemoryLimit();
	}

	/*
	 * Set up to collect AFTER triggers
	 */
	AfterTriggerBeginQuery();

	/*
	 * Call ExecutorStart to prepare the plan for execution
	 */
	ExecutorStart(queryDesc, 0);

	/*
	 * Run the plan to completion.
	 */
	ExecutorRun(queryDesc, ForwardScanDirection, 0L);

	/* Now take care of any queued AFTER triggers */
	AfterTriggerEndQuery(queryDesc->estate);

	/*
	 * MPP-4145: convert qualifying "delete from" queries into
	 * truncate, after the delete has run -- this will return the
	 * correct number of rows to the client, and will also free the
	 * underlying storage.
	 *
	 * We save Oid before ending query.
	 *
	 * If we're deleting a complete table in parallel, add on a
	 * truncate step after we've done the delete -- this frees the
	 * storage whereas the delete itself does not.
	 *
	 * A delete of the table should already have the appropriate
	 * locks (?), so we ought not deadlock here.
	 */
	if (Gp_role == GP_ROLE_DISPATCH &&
		gp_enable_delete_as_truncate &&
		queryDesc->plannedstmt->planTree != NULL &&
		queryDesc->plannedstmt->planTree->dispatch == DISPATCH_PARALLEL &&
		queryDesc->operation == CMD_DELETE &&
		list_length(queryDesc->plannedstmt->resultRelations) == 1 && /* not partitioned */
		linitial_int(queryDesc->plannedstmt->resultRelations) == 1 /* target first in range table */
		)
	{
		RangeTblEntry *rte;

		rte = linitial(queryDesc->plannedstmt->rtable);
		/*
		 * if the delete command we just ran had no qualifiers and
		 * was against a simple table, it is nice to be able to
		 * substitute a truncate-command.
		 */
		if (queryDesc->plannedstmt->planTree->qual == NULL &&
			queryDesc->plannedstmt->planTree->lefttree == NULL &&
			queryDesc->plannedstmt->planTree->righttree == NULL)
		{
			Relation truncRel;

			/*
			 * acquire the lock required by Truncate.
			 *
			 * We only want to set the truncOid if we think the
			 * truncate will not return an error (we have to be
			 * the owner of the table, for instance).
			 */
			truncRel = heap_open(rte->relid, AccessExclusiveLock);
			do
			{
				if (truncRel->rd_rel->relkind != RELKIND_RELATION ||
					!pg_class_ownercheck(RelationGetRelid(truncRel), GetUserId()) ||
					(!allowSystemTableModsDDL && IsSystemRelation(truncRel)))
				{
					heap_close(truncRel, AccessExclusiveLock);
					break;
				}

				truncOid = rte->relid;
				heap_close(truncRel, NoLock);
			} while (0);
		}
	}

	/*
	 * Now, we close down all the scans and free allocated resources.
	 */
	ExecutorEnd(queryDesc);

	/*
	 * Build command completion status string, if caller wants one.
	 */
	if (completionTag)
	{
		Oid			lastOid;

		switch (stmt->commandType)
		{
			case CMD_SELECT:
				snprintf(completionTag, COMPLETION_TAG_BUFSIZE,
						 "SELECT " UINT64_FORMAT "", queryDesc->es_processed);
				break;
			case CMD_INSERT:	
				if (queryDesc->es_processed == 1)
					lastOid = queryDesc->es_lastoid;
				else
					lastOid = InvalidOid;
				snprintf(completionTag, COMPLETION_TAG_BUFSIZE,
						 "INSERT %u " UINT64_FORMAT "", lastOid, queryDesc->es_processed);
				break;
			case CMD_UPDATE:
				snprintf(completionTag, COMPLETION_TAG_BUFSIZE,
						 "UPDATE " UINT64_FORMAT "", queryDesc->es_processed);
				break;
			case CMD_DELETE:
				snprintf(completionTag, COMPLETION_TAG_BUFSIZE,
						 "DELETE " UINT64_FORMAT "", queryDesc->es_processed);
				break;
			default:
				strcpy(completionTag, "???");
				break;
		}
	}

	if (Gp_role == GP_ROLE_DISPATCH)
	{
		/*
		 * MPP-4145: truncate using saved Oid from earlier.
		 */
		if (OidIsValid(truncOid))
		{
			TruncateStmt *truncStmt = NULL;
			RangeVar   *truncRel;
			char	*schemaName;
			char	*relName;

			truncStmt = makeNode(TruncateStmt);
			truncStmt->behavior = DROP_RESTRICT; /* don't cascade */
			truncStmt->relations = NIL;

			schemaName = get_namespace_name(get_rel_namespace(truncOid));
			relName = get_rel_name(truncOid);
			if (schemaName != NULL && relName != NULL)
			{
				elog(DEBUG1, "converting delete into truncate on %s.%s", schemaName, relName);

				/* we need a list of RangeVars */
				truncRel = makeRangeVar(get_namespace_name(get_rel_namespace(truncOid)), get_rel_name(truncOid), -1);
				truncStmt->relations = lappend(truncStmt->relations, truncRel);

				ExecuteTruncate(truncStmt);
			}
		}
	
		autostats_get_cmdtype(stmt, &cmdType, &relationOid);

		/* MPP-4407. Logging number of tuples modified. */
		if (relationOid != InvalidOid)
		{
			if (cmdType < AUTOSTATS_CMDTYPE_SENTINEL &&			
				GetPlannedStmtLogLevel(stmt) <= log_statement)
			{
				elog(DEBUG1, "type_of_statement = %s dboid = %d tableoid = %d num_tuples_modified = %u", 
					 autostats_cmdtype_to_string(cmdType), 
					 MyDatabaseId, 
					 relationOid, 
					 (unsigned int) queryDesc->es_processed);
			}
		}
		
		/* MPP-4082. Issue automatic ANALYZE if conditions are satisfied. */
		bool inFunction = false;
		auto_stats(cmdType, relationOid, queryDesc->es_processed, inFunction);
	}

	FreeQueryDesc(queryDesc);

	FreeSnapshot(ActiveSnapshot);
	ActiveSnapshot = NULL;
	
	if (gp_enable_resqueue_priority 
			&& Gp_role == GP_ROLE_DISPATCH 
			&& gp_session_id > -1)
	{
		BackoffBackendEntryExit();
	}

}

/*
 * ChoosePortalStrategy
 *		Select portal execution strategy given the intended query list.
 *
 * The list elements can be Querys, PlannedStmts, or utility statements.
 *
 * See the comments in portal.h.
 */
PortalStrategy
ChoosePortalStrategy(List *stmts)
{
	int			nSetTag;
	ListCell   *lc;

	/*
	 * PORTAL_ONE_SELECT and PORTAL_UTIL_SELECT need only consider the
	 * single-Query-struct case, since there are no rewrite rules that can add
	 * auxiliary queries to a SELECT or a utility command.
	 */
	if (list_length(stmts) == 1)
	{
		Node	   *stmt = (Node*) linitial(stmts);

		if (IsA(stmt, Query))
		{
			Query	   *query = (Query*)stmt;
			
			if (query->canSetTag)
			{
				if (query->commandType == CMD_SELECT &&
					query->utilityStmt == NULL &&
					query->intoClause == NULL)
					return PORTAL_ONE_SELECT;
				if (query->commandType == CMD_UTILITY &&
					query->utilityStmt != NULL)
				{
					if (UtilityReturnsTuples(query->utilityStmt))
						return PORTAL_UTIL_SELECT;
					/* it can't be ONE_RETURNING, so give up */
					return PORTAL_MULTI_QUERY;
				}
			}
		}
		else if (IsA(stmt, PlannedStmt))
		{
			PlannedStmt *pstmt = (PlannedStmt *) stmt;
			
			if (pstmt->canSetTag)
			{
				if (pstmt->commandType == CMD_SELECT &&
					pstmt->utilityStmt == NULL &&
					pstmt->intoClause == NULL)
					return PORTAL_ONE_SELECT;
			}
		}
		else
		{
			/* must be a utility command; assume it's canSetTag */
			if (UtilityReturnsTuples(stmt))
				return PORTAL_UTIL_SELECT;
			/* it can't be ONE_RETURNING, so give up */
			return PORTAL_MULTI_QUERY;
		}
	}

	/*
	 * PORTAL_ONE_RETURNING has to allow auxiliary queries added by rewrite.
	 * Choose PORTAL_ONE_RETURNING if there is exactly one canSetTag query and
	 * it has a RETURNING list.
	 */
	nSetTag = 0;
	foreach(lc, stmts)
	{
		Node	   *stmt = (Node*) lfirst(lc);
		
		if (IsA(stmt, Query))
		{
			Query	   *query = (Query *) stmt;
			
			if (query->canSetTag)
			{
				if (++nSetTag > 1)
					return PORTAL_MULTI_QUERY;	/* no need to look further */
				if (query->returningList == NIL)
					return PORTAL_MULTI_QUERY;	/* no need to look further */
			}
		}
		else if (IsA(stmt, PlannedStmt))
		{
			PlannedStmt *pstmt = (PlannedStmt *) stmt;
			
			if (pstmt->canSetTag)
			{
				if (++nSetTag > 1)
					return PORTAL_MULTI_QUERY;	/* no need to look further */
				if (pstmt->returningLists == NIL)
					return PORTAL_MULTI_QUERY;	/* no need to look further */
			}
		}
		/* otherwise, utility command, assumed not canSetTag */
	}
	if (nSetTag == 1)
		return PORTAL_ONE_RETURNING;

	/* Else, it's the general case... */
	return PORTAL_MULTI_QUERY;
}

/*
 * FetchPortalTargetList
 *		Given a portal that returns tuples, extract the query targetlist.
 *		Returns NIL if the portal doesn't have a determinable targetlist.
 *
 * Note: do not modify the result.
 *
 * XXX be careful to keep this in sync with FetchPreparedStatementTargetList,
 * and with UtilityReturnsTuples.
 */
List *
FetchPortalTargetList(Portal portal)
{
	/* no point in looking if we determined it doesn't return tuples */
	if (portal->strategy == PORTAL_MULTI_QUERY)
		return NIL;
	/* get the primary statement and find out what it returns */
	return FetchStatementTargetList(PortalGetPrimaryStmt(portal));
}

/*
 * FetchStatementTargetList
 *		Given a statement that returns tuples, extract the query targetlist.
 *		Returns NIL if the statement doesn't have a determinable targetlist.
 *
 * This can be applied to a Query, a PlannedStmt, or a utility statement.
 * That's more general than portals need, but useful.
 *
 * Note: do not modify the result.
 *
 * XXX be careful to keep this in sync with UtilityReturnsTuples.
 */
List *
FetchStatementTargetList(Node *stmt)
{
	if (stmt == NULL)
		return NIL;
	if (IsA(stmt, Query))
	{
		Query	   *query = (Query *) stmt;
		
		if (query->commandType == CMD_UTILITY &&
			query->utilityStmt != NULL)
		{
			/* transfer attention to utility statement */
			stmt = query->utilityStmt;
		}
		else
		{
			if (query->commandType == CMD_SELECT &&
				query->utilityStmt == NULL &&
				query->intoClause == NULL)
				return query->targetList;
			if (query->returningList)
				return query->returningList;
			return NIL;
		}
	}
	if (IsA(stmt, PlannedStmt))
	{
		PlannedStmt *pstmt = (PlannedStmt *) stmt;
		
		if (pstmt->commandType == CMD_SELECT &&
			pstmt->utilityStmt == NULL &&
			pstmt->intoClause == NULL)
			return pstmt->planTree->targetlist;
		if (pstmt->returningLists)
			return (List *) linitial(pstmt->returningLists);
		return NIL;
	}
	if (IsA(stmt, FetchStmt))
	{
		FetchStmt  *fstmt = (FetchStmt *) stmt;
		Portal		subportal;
		
		Assert(!fstmt->ismove);
		subportal = GetPortalByName(fstmt->portalname);
		Assert(PortalIsValid(subportal));
		return FetchPortalTargetList(subportal);
	}
	if (IsA(stmt, ExecuteStmt))
	{
		ExecuteStmt *estmt = (ExecuteStmt *) stmt;
		PreparedStatement *entry;
		
		Assert(!estmt->into);
		entry = FetchPreparedStatement(estmt->name, true);
		return FetchPreparedStatementTargetList(entry);
	}
	return NIL;
}

/*
 * PortalStart
 *		Prepare a portal for execution.
 *
 * Caller must already have created the portal, done PortalDefineQuery(),
 * and adjusted portal options if needed.  If parameters are needed by
 * the query, they must be passed in here (caller is responsible for
 * giving them appropriate lifetime).
 *
 * The caller can optionally pass a snapshot to be used; pass InvalidSnapshot
 * for the normal behavior of setting a new snapshot.  This parameter is
 * presently ignored for non-PORTAL_ONE_SELECT portals (it's only intended
 * to be used for cursors).
 *
 * On return, portal is ready to accept PortalRun() calls, and the result
 * tupdesc (if any) is known.
 */
void
PortalStart(Portal portal, ParamListInfo params, Snapshot snapshot,
			const char *seqServerHost, int seqServerPort)
{
	Portal		saveActivePortal;
	Snapshot	saveActiveSnapshot;
	ResourceOwner saveResourceOwner;
	MemoryContext savePortalContext;
	MemoryContext oldContext = CurrentMemoryContext;
	QueryDesc  *queryDesc;
	int			eflags;

	AssertArg(PortalIsValid(portal));
	AssertState(portal->queryContext != NULL);	/* query defined? */
	AssertState(PortalGetStatus(portal)  == PORTAL_NEW);	/* else extra PortalStart */

	/* Set up the sequence server */
	SetupSequenceServer(seqServerHost, seqServerPort);

	portal->releaseResLock = false;
    
    /*
	 * Set up global portal context pointers.  (Should we set QueryContext?)
	 */
	saveActivePortal = ActivePortal;
	saveActiveSnapshot = ActiveSnapshot;
	saveResourceOwner = CurrentResourceOwner;
	savePortalContext = PortalContext;
	PG_TRY();
	{
		ActivePortal = portal;
		ActiveSnapshot = NULL;	/* will be set later */
		if (portal->resowner)
			CurrentResourceOwner = portal->resowner;
		PortalContext = PortalGetHeapMemory(portal);

		MemoryContextSwitchTo(PortalGetHeapMemory(portal));

		/* Must remember portal param list, if any */
		portal->portalParams = params;

		/*
		 * Determine the portal execution strategy
		 */
		portal->strategy = ChoosePortalStrategy(portal->stmts);

		/* Initialize the backoff weight for this backend */
		PortalSetBackoffWeight(portal);

		/*
		 * Fire her up according to the strategy
		 */
		switch (portal->strategy)
		{
			case PORTAL_ONE_SELECT:

				/*
				 * Must set snapshot before starting executor.	Be sure to
				 * copy it into the portal's context.
				 */
				if (snapshot)
					ActiveSnapshot = CopySnapshot(snapshot);
				else
					ActiveSnapshot = CopySnapshot(GetTransactionSnapshot());

				/*
				 * Create QueryDesc in portal's context; for the moment, set
				 * the destination to DestNone.
				 */
				queryDesc = CreateQueryDesc((PlannedStmt *) linitial(portal->stmts),
											portal->sourceText,
											(gp_select_invisible ? SnapshotAny : ActiveSnapshot),
											InvalidSnapshot,
											None_Receiver,
											params,
											false);
				
				if (gp_enable_gpperfmon && Gp_role == GP_ROLE_DISPATCH)
				{			
					Assert(portal->sourceText);
					gpmon_qlog_query_submit(queryDesc->gpmon_pkt);
					gpmon_qlog_query_text(queryDesc->gpmon_pkt,
							portal->sourceText,
							application_name,
							GetResqueueName(portal->queueId),
							GetResqueuePriority(portal->queueId));
				}

				/* 
				 * let queryDesc know that it is running a query in stages
				 * (cursor or bind/execute path ) so that it could do the right
				 * cleanup in ExecutorEnd.
				 */
				if (portal->is_extended_query)
				{
					queryDesc->extended_query = true;
					queryDesc->portal_name = (portal->name ? pstrdup(portal->name) : (char *) NULL);
				}

				/*
				 * If resource scheduling is enabled, lock the portal here.
				 * Skip this if we are superuser!
				 */
				if (Gp_role == GP_ROLE_DISPATCH
						&& ResourceScheduler
						&& !superuser() )
				{
					/* 
					 * MPP-16369 - If we are in SPI context, only acquire 
					 * resource queue lock if the outer portal hasn't 
					 * acquired it already. This code is analogous
					 * to the code in _SPI_pquery. For cases where there is a 
					 * cursor inside PL/pgSQL, we don't go via _SPI_pquery,
					 * but execute PortalStart directly. Hence the following
					 * check is needed to prevent self-deadlocks as described 
					 * in MPP-16369. 
					 * If not in SPI context, acquire resource queue lock with
					 * no additional checks.
					 */ 
					if (SPI_context() && 
						saveActivePortal && 
						saveActivePortal->releaseResLock)
					{
						PortalSetStatus(portal, PORTAL_QUEUE);
						if (gp_resqueue_memory_policy != RESQUEUE_MEMORY_POLICY_NONE)
							queryDesc->plannedstmt->query_mem = ResourceQueueGetQueryMemoryLimit(queryDesc->plannedstmt, portal->queueId);
					}
					else
					{
						PortalSetStatus(portal, PORTAL_QUEUE);
						
						if (gp_resqueue_memory_policy != RESQUEUE_MEMORY_POLICY_NONE)
							queryDesc->plannedstmt->query_mem = ResourceQueueGetQueryMemoryLimit(queryDesc->plannedstmt, portal->queueId);
						portal->releaseResLock = ResLockPortal(portal, queryDesc);
					}
				}

				PortalSetStatus(portal, PORTAL_ACTIVE);

				if (gp_resqueue_memory_policy != RESQUEUE_MEMORY_POLICY_NONE
						&& Gp_role == GP_ROLE_DISPATCH
						&& gp_session_id > -1
						&& superuser())
				{
					queryDesc->plannedstmt->query_mem = ResourceQueueGetSuperuserQueryMemoryLimit();
				}
				
				/*
				 * We do *not* call AfterTriggerBeginQuery() here.	We assume
				 * that a SELECT cannot queue any triggers.  It would be messy
				 * to support triggers since the execution of the portal may
				 * be interleaved with other queries.
				 */

				/*
				 * If it's a scrollable cursor, executor needs to support
				 * REWIND and backwards scan.
				 */
				if (portal->cursorOptions & CURSOR_OPT_SCROLL)
					eflags = EXEC_FLAG_REWIND | EXEC_FLAG_BACKWARD;
				else
					eflags = 0; /* default run-to-completion flags */

				/*
				 * Call ExecutorStart to prepare the plan for execution
				 */
				ExecutorStart(queryDesc, eflags);

				/*
				 * This tells PortalCleanup to shut down the executor
				 */
				portal->queryDesc = queryDesc;

				/*
				 * Remember tuple descriptor (computed by ExecutorStart)
				 */
				portal->tupDesc = queryDesc->tupDesc;

				/*
				 * Reset cursor position data to "start of query"
				 */
				portal->atStart = true;
				portal->atEnd = false;	/* allow fetches */
				portal->portalPos = 0;
				portal->posOverflow = false;
				break;

			case PORTAL_ONE_RETURNING:

				/*
				 * We don't start the executor until we are told to run the
				 * portal.	We do need to set up the result tupdesc.
				 */
			{
				PlannedStmt *pstmt;
				
				pstmt = (PlannedStmt *) PortalGetPrimaryStmt(portal);
				Assert(IsA(pstmt, PlannedStmt));
				Assert(pstmt->returningLists);
				portal->tupDesc =
					ExecCleanTypeFromTL((List *) linitial(pstmt->returningLists),
										false);
			}
				
			/*
			 * Reset cursor position data to "start of query"
			 */
			portal->atStart = true;
			portal->atEnd = false;	/* allow fetches */
			portal->portalPos = 0;
			portal->posOverflow = false;
			break;

			case PORTAL_UTIL_SELECT:

				/*
				 * We don't set snapshot here, because PortalRunUtility will
				 * take care of it if needed.
				 */
				portal->tupDesc =
					UtilityTupleDescriptor(linitial(portal->stmts));

				/*
				 * Reset cursor position data to "start of query"
				 */
				portal->atStart = true;
				portal->atEnd = false;	/* allow fetches */
				portal->portalPos = 0;
				portal->posOverflow = false;
				break;

			case PORTAL_MULTI_QUERY:
				/* Need do nothing now */
				portal->tupDesc = NULL;
				break;
		}
	}
	PG_CATCH();
	{
		/* Uncaught error while executing portal: mark it dead */
		PortalSetStatus(portal, PORTAL_FAILED);

		/* Restore global vars and propagate error */
		ActivePortal = saveActivePortal;
		ActiveSnapshot = saveActiveSnapshot;
		CurrentResourceOwner = saveResourceOwner;
		PortalContext = savePortalContext;

		PG_RE_THROW();
	}
	PG_END_TRY();

	MemoryContextSwitchTo(oldContext);

	ActivePortal = saveActivePortal;
	ActiveSnapshot = saveActiveSnapshot;
	CurrentResourceOwner = saveResourceOwner;
	PortalContext = savePortalContext;

	PortalSetStatus(portal, PORTAL_READY);
}

/*
 * PortalSetResultFormat
 *		Select the format codes for a portal's output.
 *
 * This must be run after PortalStart for a portal that will be read by
 * a DestRemote or DestRemoteExecute destination.  It is not presently needed
 * for other destination types.
 *
 * formats[] is the client format request, as per Bind message conventions.
 */
void
PortalSetResultFormat(Portal portal, int nFormats, int16 *formats)
{
	int			natts;
	int			i;

	/* Do nothing if portal won't return tuples */
	if (portal->tupDesc == NULL)
		return;
	natts = portal->tupDesc->natts;
	portal->formats = (int16 *)
		MemoryContextAlloc(PortalGetHeapMemory(portal),
						   natts * sizeof(int16));
	if (nFormats > 1)
	{
		/* format specified for each column */
		if (nFormats != natts)
			ereport(ERROR,
					(errcode(ERRCODE_PROTOCOL_VIOLATION),
					 errmsg("bind message has %d result formats but query has %d columns",
							nFormats, natts)));
		memcpy(portal->formats, formats, natts * sizeof(int16));
	}
	else if (nFormats > 0)
	{
		/* single format specified, use for all columns */
		int16		format1 = formats[0];

		for (i = 0; i < natts; i++)
			portal->formats[i] = format1;
	}
	else
	{
		/* use default format for all columns */
		for (i = 0; i < natts; i++)
			portal->formats[i] = 0;
	}
}

/*
 * PortalRun
 *		Run a portal's query or queries.
 *
 * count <= 0 is interpreted as a no-op: the destination gets started up
 * and shut down, but nothing else happens.  Also, count == FETCH_ALL is
 * interpreted as "all rows".  Note that count is ignored in multi-query
 * situations, where we always run the portal to completion.
 *
 * dest: where to send output of primary (canSetTag) query
 *
 * altdest: where to send output of non-primary queries
 *
 * completionTag: points to a buffer of size COMPLETION_TAG_BUFSIZE
 *		in which to store a command completion status string.
 *		May be NULL if caller doesn't want a status string.
 *
 * Returns TRUE if the portal's execution is complete, FALSE if it was
 * suspended due to exhaustion of the count parameter.
 */
bool
PortalRun(Portal portal, int64 count, bool isTopLevel,
		  DestReceiver *dest, DestReceiver *altdest,
		  char *completionTag)
{
	bool		result = false;
	ResourceOwner saveTopTransactionResourceOwner;
	MemoryContext saveTopTransactionContext;
	Portal		saveActivePortal;
	Snapshot	saveActiveSnapshot;
	ResourceOwner saveResourceOwner;
	MemoryContext savePortalContext;
	MemoryContext saveQueryContext;
	MemoryContext saveMemoryContext;

	AssertArg(PortalIsValid(portal));

	/* Initialize completion tag to empty string */
	if (completionTag)
		completionTag[0] = '\0';

	if (log_executor_stats && portal->strategy != PORTAL_MULTI_QUERY)
	{
		ereport(DEBUG3,
				(errmsg_internal("PortalRun")));
		/* PORTAL_MULTI_QUERY logs its own stats per query */
		ResetUsage();
	}

	/*
	 * Check for improper portal use, and mark portal active.
	 */
	if (PortalGetStatus(portal) != PORTAL_READY && PortalGetStatus(portal) != PORTAL_QUEUE)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("portal \"%s\" cannot be run", portal->name)));

	PortalSetStatus(portal, PORTAL_ACTIVE);

	/*
	 * Set up global portal context pointers.
	 *
	 * We have to play a special game here to support utility commands like
	 * VACUUM and CLUSTER, which internally start and commit transactions.
	 * When we are called to execute such a command, CurrentResourceOwner will
	 * be pointing to the TopTransactionResourceOwner --- which will be
	 * destroyed and replaced in the course of the internal commit and
	 * restart.  So we need to be prepared to restore it as pointing to the
	 * exit-time TopTransactionResourceOwner.  (Ain't that ugly?  This idea of
	 * internally starting whole new transactions is not good.)
	 * CurrentMemoryContext has a similar problem, but the other pointers we
	 * save here will be NULL or pointing to longer-lived objects.
	 */
	saveTopTransactionResourceOwner = TopTransactionResourceOwner;
	saveTopTransactionContext = TopTransactionContext;
	saveActivePortal = ActivePortal;
	saveActiveSnapshot = ActiveSnapshot;
	saveResourceOwner = CurrentResourceOwner;
	savePortalContext = PortalContext;
	saveQueryContext = QueryContext;
	saveMemoryContext = CurrentMemoryContext;
	PG_TRY();
	{
		ActivePortal = portal;
		ActiveSnapshot = NULL;	/* will be set later */
		if (portal->resowner)
			CurrentResourceOwner = portal->resowner;
		PortalContext = PortalGetHeapMemory(portal);
		QueryContext = portal->queryContext;

		MemoryContextSwitchTo(PortalContext);

		switch (portal->strategy)
		{
			case PORTAL_ONE_SELECT:
				(void) PortalRunSelect(portal, true, count, dest);

				/* we know the query is supposed to set the tag */
				if (completionTag && portal->commandTag)
					strcpy(completionTag, portal->commandTag);

				/* Mark portal not active */
				PortalSetStatus(portal, PORTAL_READY);

				/*
				 * Since it's a forward fetch, say DONE iff atEnd is now true.
				 */
				result = portal->atEnd;
				break;

			case PORTAL_ONE_RETURNING:
			case PORTAL_UTIL_SELECT:
			    /*
			     * If we have not yet run the command, do so, storing its
			     * results in the portal's tuplestore.
			     */
			    if (!portal->holdStore)
				    FillPortalStore(portal, isTopLevel);

			    /*
			     * Now fetch desired portion of results.
			     */
			    (void) PortalRunSelect(portal, true, count, dest);

			    /* we know the query is supposed to set the tag */
			    if (completionTag && portal->commandTag)
				    strcpy(completionTag, portal->commandTag);

				/* Mark portal not active */
				PortalSetStatus(portal, PORTAL_READY);

				/*
				 * Since it's a forward fetch, say DONE iff atEnd is now true.
				 */
				result = portal->atEnd;
				break;

			case PORTAL_MULTI_QUERY:
				PortalRunMulti(portal, isTopLevel, 
							   dest, altdest, completionTag);

				/* Prevent portal's commands from being re-executed */
				PortalSetStatus(portal, PORTAL_DONE);

				/* Always complete at end of RunMulti */
				result = true;
				break;

			default:
				elog(ERROR, "unrecognized portal strategy: %d",
					 (int) portal->strategy);
				break;
		}
	}
	PG_CATCH();
	{
		/* Uncaught error while executing portal: mark it dead */
		PortalSetStatus(portal, PORTAL_FAILED);

		/* Restore global vars and propagate error */
		if (saveMemoryContext == saveTopTransactionContext)
			MemoryContextSwitchTo(TopTransactionContext);
		else
			MemoryContextSwitchTo(saveMemoryContext);
		ActivePortal = saveActivePortal;
		ActiveSnapshot = saveActiveSnapshot;
		if (saveResourceOwner == saveTopTransactionResourceOwner)
			CurrentResourceOwner = TopTransactionResourceOwner;
		else
			CurrentResourceOwner = saveResourceOwner;
		PortalContext = savePortalContext;
		QueryContext = saveQueryContext;

		TeardownSequenceServer();

		PG_RE_THROW();
	}
	PG_END_TRY();

	if (saveMemoryContext == saveTopTransactionContext)
		MemoryContextSwitchTo(TopTransactionContext);
	else
		MemoryContextSwitchTo(saveMemoryContext);
	ActivePortal = saveActivePortal;
	ActiveSnapshot = saveActiveSnapshot;
	if (saveResourceOwner == saveTopTransactionResourceOwner)
		CurrentResourceOwner = TopTransactionResourceOwner;
	else
		CurrentResourceOwner = saveResourceOwner;
	PortalContext = savePortalContext;
	QueryContext = saveQueryContext;

	if (log_executor_stats && portal->strategy != PORTAL_MULTI_QUERY)
		ShowUsage("EXECUTOR STATISTICS");

	return result;
}

/*
 * PortalRunSelect
 *		Execute a portal's query in PORTAL_ONE_SELECT mode, and also
 *		when fetching from a completed holdStore in PORTAL_ONE_RETURNING
 *		and PORTAL_UTIL_SELECT cases.
 *
 * This handles simple N-rows-forward-or-backward cases.  For more complex
 * nonsequential access to a portal, see PortalRunFetch.
 *
 * count <= 0 is interpreted as a no-op: the destination gets started up
 * and shut down, but nothing else happens.  Also, count == FETCH_ALL is
 * interpreted as "all rows".
 *
 * Caller must already have validated the Portal and done appropriate
 * setup (cf. PortalRun).
 *
 * Returns number of rows processed (suitable for use in result tag)
 */
static int64
PortalRunSelect(Portal portal,
				bool forward,
				int64 count,
				DestReceiver *dest)
{
	QueryDesc  *queryDesc;
	ScanDirection direction;
	uint64		nprocessed;

	/*
	 * NB: queryDesc will be NULL if we are fetching from a held cursor or a
	 * completed utility query; can't use it in that path.
	 */
	queryDesc = PortalGetQueryDesc(portal);

	/* Caller messed up if we have neither a ready query nor held data. */
	Assert(queryDesc || portal->holdStore);

	/*
	 * Force the queryDesc destination to the right thing.	This supports
	 * MOVE, for example, which will pass in dest = DestNone.  This is okay to
	 * change as long as we do it on every fetch.  (The Executor must not
	 * assume that dest never changes.)
	 */
	if (queryDesc)
		queryDesc->dest = dest;

	/*
	 * Determine which direction to go in, and check to see if we're already
	 * at the end of the available tuples in that direction.  If so, set the
	 * direction to NoMovement to avoid trying to fetch any tuples.  (This
	 * check exists because not all plan node types are robust about being
	 * called again if they've already returned NULL once.)  Then call the
	 * executor (we must not skip this, because the destination needs to see a
	 * setup and shutdown even if no tuples are available).  Finally, update
	 * the portal position state depending on the number of tuples that were
	 * retrieved.
	 */
	if (forward)
	{
		if (portal->atEnd || count <= 0)
			direction = NoMovementScanDirection;
		else
			direction = ForwardScanDirection;

		/* In the executor, zero count processes all rows */
		if (count == FETCH_ALL)
			count = 0;

		if (portal->holdStore)
			nprocessed = RunFromStore(portal, direction, count, dest);
		else
		{
			ActiveSnapshot = queryDesc->snapshot;
			ExecutorRun(queryDesc, direction, count);
			nprocessed = queryDesc->estate->es_processed;
		}

		if (!ScanDirectionIsNoMovement(direction))
		{
			long		oldPos;

			if (nprocessed > 0)
				portal->atStart = false;		/* OK to go backward now */
			if (count == 0 ||
				(unsigned long) nprocessed < (unsigned long) count)
				portal->atEnd = true;	/* we retrieved 'em all */
			oldPos = portal->portalPos;
			portal->portalPos += nprocessed;
			/* portalPos doesn't advance when we fall off the end */
			if (portal->portalPos < oldPos)
				portal->posOverflow = true;
		}
	}
	else
	{
		if (portal->cursorOptions & CURSOR_OPT_NO_SCROLL)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("cursor can only scan forward"),
					 errhint("Declare it with SCROLL option to enable backward scan.")));

		if (portal->atStart || count <= 0)
			direction = NoMovementScanDirection;
		else
			direction = BackwardScanDirection;

		/* In the executor, zero count processes all rows */
		if (count == FETCH_ALL)
			count = 0;

		if (portal->holdStore)
		{
			nprocessed = RunFromStore(portal, direction, count, dest);
		}
		else
		{
			ActiveSnapshot = queryDesc->snapshot;
			ExecutorRun(queryDesc, direction, count);
			nprocessed = queryDesc->estate->es_processed;
		}

		if (!ScanDirectionIsNoMovement(direction))
		{
			if (nprocessed > 0 && portal->atEnd)
			{
				portal->atEnd = false;	/* OK to go forward now */
				portal->portalPos++;	/* adjust for endpoint case */
			}
			if (count == 0 ||
				(unsigned long) nprocessed < (unsigned long) count)
			{
				portal->atStart = true; /* we retrieved 'em all */
				portal->portalPos = 0;
				portal->posOverflow = false;
			}
			else
			{
				int64		oldPos;

				oldPos = portal->portalPos;
				portal->portalPos -= nprocessed;
				if (portal->portalPos > oldPos ||
					portal->portalPos <= 0)
					portal->posOverflow = true;
			}
		}
	}

	return nprocessed;
}

/*
 * FillPortalStore
 *		Run the query and load result tuples into the portal's tuple store.
 *
 * This is used for PORTAL_ONE_RETURNING and PORTAL_UTIL_SELECT cases only.
 */
static void
FillPortalStore(Portal portal, bool isTopLevel)
{
	DestReceiver *treceiver;
	char		completionTag[COMPLETION_TAG_BUFSIZE];

	PortalCreateHoldStore(portal);
	treceiver = CreateDestReceiver(DestTuplestore, portal);

	completionTag[0] = '\0';

	switch (portal->strategy)
	{
		case PORTAL_ONE_RETURNING:

			/*
			 * Run the portal to completion just as for the default
			 * MULTI_QUERY case, but send the primary query's output to the
			 * tuplestore. Auxiliary query outputs are discarded.
			 */
			PortalRunMulti(portal, isTopLevel, 
						   treceiver, None_Receiver, completionTag);
			break;

		case PORTAL_UTIL_SELECT:
			PortalRunUtility(portal, linitial(portal->stmts),
							 isTopLevel, treceiver, completionTag);
			break;

		default:
			elog(ERROR, "unsupported portal strategy: %d",
				 (int) portal->strategy);
			break;
	}

	/* Override default completion tag with actual command result */
	if (completionTag[0] != '\0')
		portal->commandTag = pstrdup(completionTag);

	(*treceiver->rDestroy) (treceiver);
}

/*
 * RunFromStore
 *		Fetch tuples from the portal's tuple store.
 *
 * Calling conventions are similar to ExecutorRun, except that we
 * do not depend on having a queryDesc or estate.  Therefore we return the
 * number of tuples processed as the result, not in estate->es_processed.
 *
 * One difference from ExecutorRun is that the destination receiver functions
 * are run in the caller's memory context (since we have no estate).  Watch
 * out for memory leaks.
 */
static uint64
RunFromStore(Portal portal, ScanDirection direction, int64 count,
			 DestReceiver *dest)
{
	int64		current_tuple_count = 0;
	TupleTableSlot *slot;

	slot = MakeSingleTupleTableSlot(portal->tupDesc);

	(*dest->rStartup) (dest, CMD_SELECT, portal->tupDesc);

	if (ScanDirectionIsNoMovement(direction))
	{
		/* do nothing except start/stop the destination */
	}
	else
	{
		bool		forward = ScanDirectionIsForward(direction);

		for (;;)
		{
			MemoryContext oldcontext;
			bool		ok;

			oldcontext = MemoryContextSwitchTo(portal->holdContext);

			ok = tuplestore_gettupleslot(portal->holdStore, forward, slot);

			MemoryContextSwitchTo(oldcontext);

			if (!ok)
				break;

			(*dest->receiveSlot) (slot, dest);

			ExecClearTuple(slot);

			/*
			 * check our tuple count.. if we've processed the proper number
			 * then quit, else loop again and process more tuples. Zero count
			 * means no limit.
			 */
			current_tuple_count++;
			if (count && count == current_tuple_count)
				break;
		}
	}

	(*dest->rShutdown) (dest);

	ExecDropSingleTupleTableSlot(slot);

	return (uint32) current_tuple_count;
}

/*
 * PortalRunUtility
 *		Execute a utility statement inside a portal.
 */
static void
PortalRunUtility(Portal portal, Node *utilityStmt, bool isTopLevel,
				 DestReceiver *dest, char *completionTag)
{
	ereport(DEBUG3,
			(errmsg_internal("ProcessUtility")));

	/*
	 * Set snapshot if utility stmt needs one.	Most reliable way to do this
	 * seems to be to enumerate those that do not need one; this is a short
	 * list.  Transaction control, LOCK, and SET must *not* set a snapshot
	 * since they need to be executable at the start of a serializable
	 * transaction without freezing a snapshot.  By extension we allow SHOW
	 * not to set a snapshot.  The other stmts listed are just efficiency
	 * hacks.  Beware of listing anything that can modify the database --- if,
	 * say, it has to update an index with expressions that invoke
	 * user-defined functions, then it had better have a snapshot.
	 *
	 * Note we assume that caller will take care of restoring ActiveSnapshot
	 * on exit/error.
	 */
	if (!(IsA(utilityStmt, TransactionStmt) ||
		  IsA(utilityStmt, LockStmt) ||
		  IsA(utilityStmt, VariableSetStmt) ||
		  IsA(utilityStmt, VariableShowStmt) ||
		  IsA(utilityStmt, VariableResetStmt) ||
		  IsA(utilityStmt, ConstraintsSetStmt) ||
	/* efficiency hacks from here down */
		  IsA(utilityStmt, FetchStmt) ||
		  IsA(utilityStmt, ListenStmt) ||
		  IsA(utilityStmt, NotifyStmt) ||
		  IsA(utilityStmt, UnlistenStmt) ||
		  IsA(utilityStmt, CheckPointStmt)))
		ActiveSnapshot = CopySnapshot(GetTransactionSnapshot());
	else
		ActiveSnapshot = NULL;
	gpmon_packet_t *gpmon_pkt = NULL;
	if(gp_enable_gpperfmon && Gp_role == GP_ROLE_DISPATCH)
	{
		gpmon_pkt = (gpmon_packet_t *) palloc0(sizeof(gpmon_packet_t));
		gpmon_qlog_packet_init(gpmon_pkt);
		/* set gpmon_pkt->u.qlog.key.ccnt to 0 in utility mode becasue the gp_command_count is changing */
		gpmon_pkt->u.qlog.key.ccnt = 0;
		gpmon_qlog_query_submit(gpmon_pkt);
		gpmon_qlog_query_text(gpmon_pkt,
				portal->sourceText ? portal->sourceText: "(Source text for portal is not available)",
				application_name,
				GetResqueueName(portal->queueId),
				GetResqueuePriority(portal->queueId));
				gpmon_qlog_query_start(gpmon_pkt);
	}


	/* check if this utility statement need to be involved into resoure queue
	 * mgmt */
	ResHandleUtilityStmt(portal, utilityStmt);

	if ( isTopLevel )
	{
		/* utility statement can override default tag string */
		ProcessUtility(utilityStmt, 
					   portal->sourceText ? portal->sourceText : "(Source text for portal is not available)",
					   portal->portalParams,
					   isTopLevel,
					   dest, 
					   completionTag);
		if (completionTag && completionTag[0] == '\0' && portal->commandTag)
			strcpy(completionTag, portal->commandTag);	/* use the default */
	}
	else
	{
		/* utility added by rewrite cannot set tag */
		ProcessUtility(utilityStmt, 
					   portal->sourceText ? portal->sourceText : "(Source text for portal is not available)",
					   portal->portalParams, 
					   isTopLevel,
					   dest, 
					   NULL);
	}

	/* Some utility statements may change context on us */
	MemoryContextSwitchTo(PortalGetHeapMemory(portal));

	if (ActiveSnapshot)
		FreeSnapshot(ActiveSnapshot);
	ActiveSnapshot = NULL;

	if(gp_enable_gpperfmon && Gp_role == GP_ROLE_DISPATCH)
	{
		gpmon_qlog_query_end(gpmon_pkt);
		pfree(gpmon_pkt);
	}
}

/*
 * PortalRunMulti
 *		Execute a portal's queries in the general case (multi queries
 *		or non-SELECT-like queries)
 */
static void
PortalRunMulti(Portal portal, bool isTopLevel,
			   DestReceiver *dest, DestReceiver *altdest,
			   char *completionTag)
{
	ListCell   *stmtlist_item;

	/*
	 * If the destination is DestRemoteExecute, change to DestNone.  The
	 * reason is that the client won't be expecting any tuples, and indeed has
	 * no way to know what they are, since there is no provision for Describe
	 * to send a RowDescription message when this portal execution strategy is
	 * in effect.  This presently will only affect SELECT commands added to
	 * non-SELECT queries by rewrite rules: such commands will be executed,
	 * but the results will be discarded unless you use "simple Query"
	 * protocol.
	 */
	if (dest->mydest == DestRemoteExecute)
		dest = None_Receiver;
	if (altdest->mydest == DestRemoteExecute)
		altdest = None_Receiver;

	/*
	 * Loop to handle the individual queries generated from a single parsetree
	 * by analysis and rewrite.
	 */
	foreach(stmtlist_item, portal->stmts)
	{
		Node *stmt = lfirst(stmtlist_item);

		/*
		 * If we got a cancel signal in prior command, quit
		 */
		CHECK_FOR_INTERRUPTS();

		if (IsA(stmt, PlannedStmt) &&
			((PlannedStmt *) stmt)->utilityStmt == NULL)
		{
			/*
			 * process a plannable query.
			 */
			PlannedStmt *pstmt = (PlannedStmt *) stmt;
			
			if (log_executor_stats)
				ResetUsage();

			if (pstmt->canSetTag)
			{
				/* statement can set tag string */
				ProcessQuery(portal, pstmt,
							 portal->portalParams,
							 dest, completionTag);
			}
			else
			{
				/* stmt added by rewrite cannot set tag */
				ProcessQuery(portal, pstmt,
							 portal->portalParams,
							 altdest, NULL);
			}

			if (log_executor_stats)
				ShowUsage("EXECUTOR STATISTICS");
		}
		else
		{
			/*
			 * process utility functions (create, destroy, etc..)
			 *
			 * These are assumed canSetTag if they're the only stmt in the
			 * portal, with the following exception:
			 *
			 *  A COPY FROM that specifies a non-existent error table, will
			 *  be transformed (parse_analyze) into a (CreateStmt, CopyStmt).
			 *  XXX Maybe this should be treated like DECLARE CURSOR?
			 */
			if (list_length(portal->stmts) == 1 || portal->sourceTag == T_CopyStmt)
				PortalRunUtility(portal, stmt, isTopLevel, dest, completionTag);
			else
				PortalRunUtility(portal, stmt, isTopLevel, altdest, NULL);
		}
		

		/*
		 * Increment command counter between queries, but not after the last
		 * one.
		 */
		if (lnext(stmtlist_item) != NULL)
			CommandCounterIncrement();

		/*
		 * Clear subsidiary contexts to recover temporary memory.
		 */
		Assert(PortalGetHeapMemory(portal) == CurrentMemoryContext);

		MemoryContextDeleteChildren(PortalGetHeapMemory(portal));
	}

	/*
	 * If a command completion tag was supplied, use it.  Otherwise use the
	 * portal's commandTag as the default completion tag.
	 *
	 * Exception: clients will expect INSERT/UPDATE/DELETE tags to have
	 * counts, so fake something up if necessary.  (This could happen if the
	 * original query was replaced by a DO INSTEAD rule.)
	 */
	if (completionTag && completionTag[0] == '\0')
	{
		if (portal->commandTag)
			strcpy(completionTag, portal->commandTag);
		if (strcmp(completionTag, "INSERT") == 0)
			strcpy(completionTag, "INSERT 0 0");
		else if (strcmp(completionTag, "UPDATE") == 0)
			strcpy(completionTag, "UPDATE 0");
		else if (strcmp(completionTag, "DELETE") == 0)
			strcpy(completionTag, "DELETE 0");
	}
}

/*
 * PortalRunFetch
 *		Variant form of PortalRun that supports SQL FETCH directions.
 *
 * Returns number of rows processed (suitable for use in result tag)
 */
int64
PortalRunFetch(Portal portal,
			   FetchDirection fdirection,
			   int64 count,
			   DestReceiver *dest)
{
	int64		result = 0;
	Portal		saveActivePortal;
	Snapshot	saveActiveSnapshot;
	ResourceOwner saveResourceOwner;
	MemoryContext savePortalContext;
	MemoryContext saveQueryContext;
	MemoryContext oldContext = CurrentMemoryContext;

	AssertArg(PortalIsValid(portal));

	/*
	 * Check for improper portal use, and mark portal active.
	 */
	if (PortalGetStatus(portal) != PORTAL_READY)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("portal \"%s\" cannot be run", portal->name)));

	PortalSetStatus(portal, PORTAL_ACTIVE);

	/*
	 * Set up global portal context pointers.
	 */
	saveActivePortal = ActivePortal;
	saveActiveSnapshot = ActiveSnapshot;
	saveResourceOwner = CurrentResourceOwner;
	savePortalContext = PortalContext;
	saveQueryContext = QueryContext;
	PG_TRY();
	{
		ActivePortal = portal;
		ActiveSnapshot = NULL;	/* will be set later */
		if (portal->resowner)
			CurrentResourceOwner = portal->resowner;
		PortalContext = PortalGetHeapMemory(portal);
		QueryContext = portal->queryContext;

		MemoryContextSwitchTo(PortalContext);

		switch (portal->strategy)
		{
			case PORTAL_ONE_SELECT:
				result = DoPortalRunFetch(portal, fdirection, count, dest);
				break;

			case PORTAL_ONE_RETURNING:
			case PORTAL_UTIL_SELECT:

				/*
				 * If we have not yet run the command, do so, storing its
				 * results in the portal's tuplestore.
				 */
				if (!portal->holdStore)
				{
					FillPortalStore(portal, false /* isTopLevel */ );
				}

				/*
				 * Now fetch desired portion of results.
				 */
				result = DoPortalRunFetch(portal, fdirection, count, dest);
				break;

			default:
				elog(ERROR, "unsupported portal strategy");
				break;
		}
	}
	PG_CATCH();
	{
		/* Uncaught error while executing portal: mark it dead */
		PortalSetStatus(portal, PORTAL_FAILED);

		/* Restore global vars and propagate error */
		ActivePortal = saveActivePortal;
		ActiveSnapshot = saveActiveSnapshot;
		CurrentResourceOwner = saveResourceOwner;
		PortalContext = savePortalContext;
		QueryContext = saveQueryContext;

		PG_RE_THROW();
	}
	PG_END_TRY();

	MemoryContextSwitchTo(oldContext);

	/* Mark portal not active */
	PortalSetStatus(portal, PORTAL_READY);

	ActivePortal = saveActivePortal;
	ActiveSnapshot = saveActiveSnapshot;
	CurrentResourceOwner = saveResourceOwner;
	PortalContext = savePortalContext;
	QueryContext = saveQueryContext;

	return result;
}

/*
 * DoPortalRunFetch
 *		Guts of PortalRunFetch --- the portal context is already set up
 *
 * Returns number of rows processed (suitable for use in result tag)
 */
static int64
DoPortalRunFetch(Portal portal,
				 FetchDirection fdirection,
				 int64 count,
				 DestReceiver *dest)
{
	bool		forward;

	Assert(portal->strategy == PORTAL_ONE_SELECT ||
		   portal->strategy == PORTAL_ONE_RETURNING ||
		   portal->strategy == PORTAL_UTIL_SELECT);

	switch (fdirection)
	{
		case FETCH_FORWARD:
			if (count < 0)
			{
				fdirection = FETCH_BACKWARD;
				count = -count;
				
				/* until we enable backward scan - bail out here */
				ereport(ERROR,
						(errcode(ERRCODE_GP_FEATURE_NOT_YET),
						 errmsg("backward scan is not supported in this version of Greenplum Database")));
			}
			/* fall out of switch to share code with FETCH_BACKWARD */
			break;
		case FETCH_BACKWARD:
			if (count < 0)
			{
				fdirection = FETCH_FORWARD;
				count = -count;
			}
			else
			{
				/* until we enable backward scan - bail out here */
				ereport(ERROR,
						(errcode(ERRCODE_GP_FEATURE_NOT_YET),
						 errmsg("backward scan is not supported in this version of Greenplum Database")));
			}
			/* fall out of switch to share code with FETCH_FORWARD */
			break;
		case FETCH_ABSOLUTE:
			if (count > 0)
			{
				/*
				 * Definition: Rewind to start, advance count-1 rows, return
				 * next row (if any).  In practice, if the goal is less than
				 * halfway back to the start, it's better to scan from where
				 * we are.	In any case, we arrange to fetch the target row
				 * going forwards.
				 */
				if (portal->posOverflow || 
					portal->portalPos ==  INT64CONST(0x7FFFFFFFFFFFFFFF) ||
					count - 1 <= portal->portalPos / 2)
				{
					/* until we enable backward scan - bail out here */
					if(portal->portalPos > 0)
						ereport(ERROR,
								(errcode(ERRCODE_GP_FEATURE_NOT_YET),
								 errmsg("backward scan is not supported in this version of Greenplum Database")));
					
					DoPortalRewind(portal);
					if (count > 1)
						PortalRunSelect(portal, true, count - 1,
										None_Receiver);
				}
				else
				{
					int64		pos = portal->portalPos;

					if (portal->atEnd)
						pos++;	/* need one extra fetch if off end */
					if (count <= pos)
						PortalRunSelect(portal, false, pos - count + 1,
										None_Receiver);
					else if (count > pos + 1)
						PortalRunSelect(portal, true, count - pos - 1,
										None_Receiver);
				}
				return PortalRunSelect(portal, true, 1L, dest);
			}
			else if (count < 0)
			{
				/*
				 * Definition: Advance to end, back up abs(count)-1 rows,
				 * return prior row (if any).  We could optimize this if we
				 * knew in advance where the end was, but typically we won't.
				 * (Is it worth considering case where count > half of size of
				 * query?  We could rewind once we know the size ...)
				 */
				
				/* until we enable backward scan - bail out here */
				ereport(ERROR,
						(errcode(ERRCODE_GP_FEATURE_NOT_YET),
						 errmsg("backward scan is not supported in this version of Greenplum Database")));
				
				PortalRunSelect(portal, true, FETCH_ALL, None_Receiver);
				if (count < -1)
					PortalRunSelect(portal, false, -count - 1, None_Receiver);
				return PortalRunSelect(portal, false, 1L, dest);
			}
			else
			{
				/* count == 0 */
				
				/* until we enable backward scan - bail out here */
				ereport(ERROR,
						(errcode(ERRCODE_GP_FEATURE_NOT_YET),
						 errmsg("backward scan is not supported in this version of Greenplum Database")));
				
				/* Rewind to start, return zero rows */
				DoPortalRewind(portal);
				return PortalRunSelect(portal, true, 0L, dest);
			}
			break;
		case FETCH_RELATIVE:
			if (count > 0)
			{
				/*
				 * Definition: advance count-1 rows, return next row (if any).
				 */
				if (count > 1)
					PortalRunSelect(portal, true, count - 1, None_Receiver);
				return PortalRunSelect(portal, true, 1L, dest);
			}
			else if (count < 0)
			{
				/*
				 * Definition: back up abs(count)-1 rows, return prior row (if
				 * any).
				 */
				
				/* until we enable backward scan - bail out here */
				ereport(ERROR,
						(errcode(ERRCODE_GP_FEATURE_NOT_YET),
						 errmsg("backward scan is not supported in this version of Greenplum Database")));				
				
				if (count < -1)
					PortalRunSelect(portal, false, -count - 1, None_Receiver);
				return PortalRunSelect(portal, false, 1L, dest);
			}
			else
			{
				/* count == 0 */
				/* Same as FETCH FORWARD 0, so fall out of switch */
				fdirection = FETCH_FORWARD;
			}
			break;
		default:
			elog(ERROR, "bogus direction");
			break;
	}

	/*
	 * Get here with fdirection == FETCH_FORWARD or FETCH_BACKWARD, and count
	 * >= 0.
	 */
	forward = (fdirection == FETCH_FORWARD);

	/*
	 * Zero count means to re-fetch the current row, if any (per SQL92)
	 */
	if (count == 0)
	{
		bool		on_row;

		/* Are we sitting on a row? */
		on_row = (!portal->atStart && !portal->atEnd);

		if (dest->mydest == DestNone)
		{
			/* MOVE 0 returns 0/1 based on if FETCH 0 would return a row */
			return on_row ? 1L : 0L;
		}
		else
		{
			/*
			 * If we are sitting on a row, back up one so we can re-fetch it.
			 * If we are not sitting on a row, we still have to start up and
			 * shut down the executor so that the destination is initialized
			 * and shut down correctly; so keep going.	To PortalRunSelect,
			 * count == 0 means we will retrieve no row.
			 */
			if (on_row)
			{
				PortalRunSelect(portal, false, 1L, None_Receiver);
				/* Set up to fetch one row forward */
				count = 1;
				forward = true;
			}
		}
	}

	/*
	 * Optimize MOVE BACKWARD ALL into a Rewind.
	 */
	if (!forward && count == FETCH_ALL && dest->mydest == DestNone)
	{
		int64		result = portal->portalPos;

		/* until we enable backward scan - bail out here */
		ereport(ERROR,
				(errcode(ERRCODE_GP_FEATURE_NOT_YET),
				 errmsg("backward scan is not supported in this version of Greenplum Database")));
		
		if (result > 0 && !portal->atEnd)
			result--;
		DoPortalRewind(portal);
		/* result is bogus if pos had overflowed, but it's best we can do */
		return result;
	}

	return PortalRunSelect(portal, forward, count, dest);
}

/*
 * DoPortalRewind - rewind a Portal to starting point
 */
static void
DoPortalRewind(Portal portal)
{
	if (portal->holdStore)
	{
		MemoryContext oldcontext;

		oldcontext = MemoryContextSwitchTo(portal->holdContext);
		tuplestore_rescan(portal->holdStore);
		MemoryContextSwitchTo(oldcontext);
	}
	if (PortalGetQueryDesc(portal))
		ExecutorRewind(PortalGetQueryDesc(portal));

	portal->atStart = true;
	portal->atEnd = false;
	portal->portalPos = 0;
	portal->posOverflow = false;
}

/*
 * Computes the backoff weight (for query prioritization) for this backend,
 * and initializes the corresponding BackoffBackendEntry.
 */
static void
PortalSetBackoffWeight(Portal portal)
{
	int weight = BackoffDefaultWeight();

	if (gp_enable_resqueue_priority &&
		(Gp_role == GP_ROLE_DISPATCH || Gp_role == GP_ROLE_EXECUTE) &&
		gp_session_id > -1)
	{
		if (superuser())
		{
			weight = BackoffSuperuserStatementWeight();
		} else
		{
			weight = ResourceQueueGetPriorityWeight(portal->queueId);
		}

		/* Initialize the SHM backend entry with the computed backoff weight */
		BackoffBackendEntryInit(gp_session_id, gp_command_count, weight);
	}
}
