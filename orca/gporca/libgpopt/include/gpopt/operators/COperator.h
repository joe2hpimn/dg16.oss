//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009-2011 Greenplum, Inc.
//
//	@filename:
//		COperator.h
//
//	@doc:
//		Base class for all operators: logical, physical, scalar, patterns
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_COperator_H
#define GPOPT_COperator_H

#include "gpos/base.h"
#include "gpos/common/CRefCount.h"
#include "gpos/common/CHashMap.h"
#include "gpos/sync/CAtomicCounter.h"

#include "gpopt/base/CColRefSet.h"
#include "gpopt/base/CDrvdProp.h"
#include "gpopt/base/CReqdPropPlan.h"
#include "gpopt/base/CReqdPropRelational.h"
#include "gpopt/base/CFunctionProp.h"
#include "naucrates/statistics/CStatistics.h"

namespace gpopt
{
	using namespace gpos;
	
	// forward declarations
	class CExpressionHandle;
	class CReqdPropPlan;
	class CReqdPropRelational;

	// dynamic array for operators
	typedef CDynamicPtrArray<COperator, CleanupRelease> DrgPop;

	// hash map mapping CColRef -> CColRef
	typedef CHashMap<CColRef, CColRef, CColRef::UlHash, CColRef::FEqual,
					CleanupNULL<CColRef>, CleanupNULL<CColRef> > HMCrCr;

	//---------------------------------------------------------------------------
	//	@class:
	//		COperator
	//
	//	@doc:
	//		base class for all operators
	//
	//---------------------------------------------------------------------------
	class COperator : public CRefCount
	{

		private:

			// private copy ctor
			COperator(COperator &);
			
		protected:

			// operator id that is unique over all instances of all operator types
			// for the current query
			ULONG m_ulOpId;
			
			// memory pool for internal allocations
			IMemoryPool *m_pmp;
		
			// is pattern of xform
			BOOL m_fPattern;

			// return an addref'ed copy of the operator
			virtual
			COperator *PopCopyDefault();

			// derive data access function property from children
			static
			IMDFunction::EFuncDataAcc EfdaDeriveFromChildren
				(
				CExpressionHandle &exprhdl,
				IMDFunction::EFuncDataAcc efdaDefault
				);

			// derive stability function property from children
			static
			IMDFunction::EFuncStbl EfsDeriveFromChildren
				(
				CExpressionHandle &exprhdl,
				IMDFunction::EFuncStbl efsDefault
				);

			// derive function properties from children
			static
			CFunctionProp *PfpDeriveFromChildren
				(
				IMemoryPool *pmp,
				CExpressionHandle &exprhdl,
				IMDFunction::EFuncStbl efsDefault,
				IMDFunction::EFuncDataAcc efdaDefault,
				BOOL fHasVolatileFunctionScan,
				BOOL fScan
				);

			// generate unique operator ids
			static
			CAtomicULONG m_aulOpIdCounter;

		public:

			// identification
			enum EOperatorId
			{
				EopLogicalGet,
				EopLogicalExternalGet,
				EopLogicalIndexGet,
				EopLogicalBitmapTableGet,
				EopLogicalSelect,
				EopLogicalUnion,
				EopLogicalUnionAll,
				EopLogicalIntersect,
				EopLogicalIntersectAll,
				EopLogicalDifference,
				EopLogicalDifferenceAll,
				EopLogicalInnerJoin,
				EopLogicalNAryJoin,
				EopLogicalLeftOuterJoin,
				EopLogicalLeftSemiJoin,
				EopLogicalLeftAntiSemiJoin,
				EopLogicalLeftAntiSemiJoinNotIn,
				EopLogicalFullOuterJoin,
				EopLogicalGbAgg,
				EopLogicalGbAggDeduplicate,
				EopLogicalLimit,
				EopLogicalProject,
				EopLogicalRename,
				EopLogicalInnerApply,
				EopLogicalInnerCorrelatedApply,
				EopLogicalInnerIndexApply,
				EopLogicalLeftOuterApply,
				EopLogicalLeftOuterCorrelatedApply,
				EopLogicalLeftSemiApply,
				EopLogicalLeftSemiCorrelatedApply,
				EopLogicalLeftSemiApplyIn,
				EopLogicalLeftSemiCorrelatedApplyIn,
				EopLogicalLeftAntiSemiApply,
				EopLogicalLeftAntiSemiCorrelatedApply,
				EopLogicalLeftAntiSemiApplyNotIn,
				EopLogicalLeftAntiSemiCorrelatedApplyNotIn,
				EopLogicalConstTableGet,
				EopLogicalDynamicGet,
				EopLogicalDynamicIndexGet,
				EopLogicalSequence,
				EopLogicalTVF,
				EopLogicalCTEAnchor,
				EopLogicalCTEProducer,
				EopLogicalCTEConsumer,
				EopLogicalSequenceProject,
				EopLogicalInsert,
				EopLogicalDelete,
				EopLogicalUpdate,
				EopLogicalDML,
				EopLogicalSplit,
				EopLogicalRowTrigger,
				EopLogicalPartitionSelector,
				EopLogicalAssert,
				EopLogicalMaxOneRow,
				
				EopScalarCmp,
				EopScalarIsDistinctFrom,
				EopScalarIdent,
				EopScalarProjectElement,
				EopScalarProjectList,
				EopScalarConst,
				EopScalarBoolOp,
				EopScalarFunc,
				EopScalarMinMax,
				EopScalarAggFunc,
				EopScalarWindowFunc,
				EopScalarOp,
				EopScalarNullIf,
				EopScalarNullTest,
				EopScalarBooleanTest,
				EopScalarIf,
				EopScalarSwitch,
				EopScalarSwitchCase,
				EopScalarCaseTest,
				EopScalarCast,
				EopScalarCoerceToDomain,
				EopScalarCoalesce,
				EopScalarArray,
				EopScalarArrayCmp,
				EopScalarArrayRef,
				EopScalarArrayRefIndexList,
				
				EopScalarAssertConstraintList,
				EopScalarAssertConstraint,
				
				EopScalarSubquery,
				EopScalarSubqueryAny,
				EopScalarSubqueryAll,
				EopScalarSubqueryExists,
				EopScalarSubqueryNotExists,
				
				EopScalarDMLAction,

				EopScalarBitmapIndexProbe,
				EopScalarBitmapBoolOp,

				EopPhysicalTableScan,
				EopPhysicalExternalScan,
				EopPhysicalIndexScan,
				EopPhysicalBitmapTableScan,
				EopPhysicalFilter,
				EopPhysicalInnerNLJoin,
				EopPhysicalInnerIndexNLJoin,
				EopPhysicalCorrelatedInnerNLJoin,
				EopPhysicalLeftOuterNLJoin,
				EopPhysicalCorrelatedLeftOuterNLJoin,
				EopPhysicalLeftSemiNLJoin,
				EopPhysicalCorrelatedLeftSemiNLJoin,
				EopPhysicalCorrelatedInLeftSemiNLJoin,
				EopPhysicalLeftAntiSemiNLJoin,
				EopPhysicalCorrelatedLeftAntiSemiNLJoin,
				EopPhysicalLeftAntiSemiNLJoinNotIn,
				EopPhysicalCorrelatedNotInLeftAntiSemiNLJoin,
				EopPhysicalDynamicTableScan,
				EopPhysicalSequence,
				EopPhysicalTVF,
				EopPhysicalCTEProducer,
				EopPhysicalCTEConsumer,
				EopPhysicalSequenceProject,
				EopPhysicalDynamicIndexScan,
				
				EopPhysicalInnerHashJoin,
				EopPhysicalLeftOuterHashJoin,
				EopPhysicalLeftSemiHashJoin,
				EopPhysicalLeftAntiSemiHashJoin,
				EopPhysicalLeftAntiSemiHashJoinNotIn,
				
				EopPhysicalMotionGather,
				EopPhysicalMotionBroadcast,
				EopPhysicalMotionHashDistribute,
				EopPhysicalMotionRoutedDistribute,
				EopPhysicalMotionRandom,
				
				EopPhysicalHashAgg,
				EopPhysicalHashAggDeduplicate,
				EopPhysicalStreamAgg,
				EopPhysicalStreamAggDeduplicate,
				EopPhysicalScalarAgg,

				EopPhysicalUnionAll,

				EopPhysicalSort,
				EopPhysicalLimit,
				EopPhysicalComputeScalar,
				EopPhysicalSpool,
				EopPhysicalPartitionSelector,
				EopPhysicalPartitionSelectorDML,
				
				EopPhysicalConstTableGet,
				
				EopPhysicalDML,
				EopPhysicalSplit,
				EopPhysicalRowTrigger,
				
				EopPhysicalAssert,
				
				EopPatternTree,
				EopPatternLeaf,
				EopPatternMultiLeaf,
				EopPatternMultiTree,
				
				EopLogicalDynamicBitmapTableGet,
				EopPhysicalDynamicBitmapTableScan,

				EopSentinel
			};
						
			// aggregate type
			enum EGbAggType
				{
				EgbaggtypeGlobal,       // global group by aggregate
				EgbaggtypeLocal, 		// local group by aggregate
				EgbaggtypeIntermediate, // intermediate group by aggregate

				EgbaggtypeSentinel
				};

			// ctor
			explicit
			COperator(IMemoryPool *pmp);

			// dtor
			virtual ~COperator() {}

			// the id of the operator
			ULONG UlOpId() const
			{
				return m_ulOpId;
			}

			// ident accessors
			virtual
			EOperatorId Eopid() const = 0;
			
			// return a string for operator name
			virtual 
			const CHAR *SzId() const = 0;

			// the following functions check operator's type

			// is operator logical?
			virtual
			BOOL FLogical() const
			{
				return false;
			}
			
			// is operator physical?
			virtual
			BOOL FPhysical() const
			{
				return false;
			}
			
			// is operator scalar?
			virtual
			BOOL FScalar() const
			{
				return false;
			}

			// is operator pattern?
			virtual
			BOOL FPattern() const
			{
				return false;
			}

			// hash function
			virtual ULONG UlHash() const;

			// sensitivity to order of inputs
			virtual BOOL FInputOrderSensitive() const = 0;

			// match function; 
			// abstract to enforce an implementation for each new operator
			virtual BOOL FMatch(COperator *pop) const = 0;
			
			// create container for derived properties
			virtual
			CDrvdProp *PdpCreate(IMemoryPool *pmp) const = 0;

			// create container for required properties
			virtual
			CReqdProp *PrpCreate(IMemoryPool *pmp) const = 0;

			// return empty container;
			// caller adds outer references using property derivation
			virtual
			CColRefSet *PcrsOuter
				(
				IMemoryPool *pmp
				)
			{
				return GPOS_NEW(pmp) CColRefSet(pmp);
			}

			// return a copy of the operator with remapped columns
			virtual
			COperator *PopCopyWithRemappedColumns
							(
							IMemoryPool *pmp,
							HMUlCr *phmulcr,
							BOOL fMustExist
							) = 0;

			// print
			virtual 
			IOstream &OsPrint(IOstream &os) const;

	}; // class COperator

}


#endif // !GPOPT_COperator_H

// EOF
