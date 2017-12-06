#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include "cmockery.h"

#include "../nodeSubplan.c"

/* Function passed to testing framework
 * in order to force SetupInterconnect to fail */ 
void
_RETHROW( )
{
	PG_RE_THROW();
}


/* Checks CdbCheckDispatchResult is called when queryDesc
 * is not null (when shouldDispatch is true).
 * This test falls in PG_CATCH when SetupInterconnect
 * does not allocate queryDesc->estate->interconnect_context.
 * The test is successful if the */
void 
test__ExecSetParamPlan__Check_Dispatch_Results(void **state) 
{

	/*Set plan to explain.*/
	SubPlanState *plan = makeNode(SubPlanState);
	plan->xprstate.expr = makeNode(SubPlanState);
	plan->planstate = makeNode(SubPlanState);
	plan->planstate->instrument = (Instrumentation *)palloc(sizeof(Instrumentation));
	plan->planstate->plan = makeNode(SubPlanState);
	
	/*Function needed for estate.*/
	expect_any(AllocSetContextCreate,parent);
	expect_any(AllocSetContextCreate,name);
	expect_any(AllocSetContextCreate,minContextSize);
	expect_any(AllocSetContextCreate,initBlockSize);
	expect_any(AllocSetContextCreate,maxBlockSize);
	will_be_called(AllocSetContextCreate);	
	EState *estate = CreateExecutorState();

	/*Assign mocked estate to plan.*/
	((PlanState *)(plan->planstate))->state= estate;

	/*Function needed for GetPerTupleExprContext*/
	expect_any(AllocSetContextCreate,parent);
	expect_any(AllocSetContextCreate,name);
	expect_any(AllocSetContextCreate,minContextSize);
	expect_any(AllocSetContextCreate,initBlockSize);
	expect_any(AllocSetContextCreate,maxBlockSize);
	will_be_called(AllocSetContextCreate);

	/*Re-use estate mocked object. Needed as input parameter for
	tested function */
	ExprContext *econtext = GetPerTupleExprContext(estate);


	/*Set QueryDescriptor input parameter for tested function */
	PlannedStmt   *plannedstmt = (PlannedStmt *)palloc(sizeof(PlannedStmt));
	QueryDesc *queryDesc = (QueryDesc *)palloc(sizeof(QueryDesc));
	queryDesc->plannedstmt = plannedstmt;
	queryDesc->estate = (EState *)palloc(sizeof(EState));
	queryDesc->estate->es_sliceTable = (SliceTable *) palloc(sizeof(SliceTable));
	
	/*Set of functions called within tested function*/
	expect_any(MemoryContextGetPeakSpace,context);
	will_be_called(MemoryContextGetPeakSpace);

	expect_any(MemoryContextSetPeakSpace,context);
	expect_any(MemoryContextSetPeakSpace,nbytes);
	will_be_called(MemoryContextSetPeakSpace);

	/*QueryDescriptor generated when shouldDispatch is true.*/
	QueryDesc *internalQueryDesc = (QueryDesc *)palloc(sizeof(QueryDesc));
	internalQueryDesc->estate = (EState *)palloc(sizeof(EState));
	/* Added to force assertion on queryDesc->estate->interconnect_context;
	to fail */
	internalQueryDesc->estate->interconnect_context=NULL;
	internalQueryDesc->estate->es_sliceTable = (SliceTable *) palloc(sizeof(SliceTable));

	expect_any(CreateQueryDesc,plannedstmt);
	expect_any(CreateQueryDesc,sourceText);
	expect_any(CreateQueryDesc,snapshot);
	expect_any(CreateQueryDesc,crosscheck_snapshot);
	expect_any(CreateQueryDesc,dest);
	expect_any(CreateQueryDesc,params);
	expect_any(CreateQueryDesc,doInstrument);
	will_return(CreateQueryDesc,internalQueryDesc);
	
	expect_any(AllocSetContextCreate,parent);
	expect_any(AllocSetContextCreate,name);
	expect_any(AllocSetContextCreate,minContextSize);
	expect_any(AllocSetContextCreate,initBlockSize);
	expect_any(AllocSetContextCreate,maxBlockSize);
	will_be_called(AllocSetContextCreate);

	Gp_role = GP_ROLE_DISPATCH;
	plan->planstate->plan->dispatch=DISPATCH_PARALLEL;

	will_be_called(isCurrentDtxTwoPhase);

	expect_any(cdbdisp_dispatchPlan,queryDesc);
	expect_any(cdbdisp_dispatchPlan,planRequiresTxn);
	expect_any(cdbdisp_dispatchPlan,cancelOnError);
	expect_any(cdbdisp_dispatchPlan,ds);
	will_be_called(cdbdisp_dispatchPlan);

	expect_any(SetupInterconnect,estate);
	/* Force SetupInterconnect to fail */
	will_be_called_with_sideeffect(SetupInterconnect,&_RETHROW,NULL);


	expect_any(cdbexplain_localExecStats,planstate);
	expect_any(cdbexplain_localExecStats,showstatctx);
	will_be_called(cdbexplain_localExecStats);

	expect_any(CdbCheckDispatchResult,ds);
	expect_any(CdbCheckDispatchResult,waitMode);
	will_be_called(CdbCheckDispatchResult);

	expect_any(cdbexplain_recvExecStats,planstate);
	expect_any(cdbexplain_recvExecStats,dispatchResults);
	expect_any(cdbexplain_recvExecStats,sliceIndex);
	expect_any(cdbexplain_recvExecStats,showstatctx);
	will_be_called(cdbexplain_recvExecStats);

	expect_any(MemoryContextSetPeakSpace,context);
	expect_any(MemoryContextSetPeakSpace,nbytes);
	will_be_called(MemoryContextSetPeakSpace);

	will_be_called(TeardownSequenceServer);

	expect_any(TeardownInterconnect,transportStates);
	expect_any(TeardownInterconnect,mlStates);
	expect_any(TeardownInterconnect,forceEOS);
	will_be_called(TeardownInterconnect);

	/* Catch PG_RE_THROW(); after cleaning with CdbCheckDispatchResult */
	PG_TRY();
		ExecSetParamPlan(plan,econtext,queryDesc);
	PG_CATCH();
		assert_true(true);
	PG_END_TRY();
}

int 
main(int argc, char* argv[]) 
{
	cmockery_parse_arguments(argc, argv);

	const UnitTest tests[] = {
		unit_test(test__ExecSetParamPlan__Check_Dispatch_Results)
	};

	return run_tests(tests);
}
