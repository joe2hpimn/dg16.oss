//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		dxltokens.h
//
//	@doc:
//		Constants for the tokens used in the DXL document.
//		Tokens are represented in both in CWStringConst format, and as XMLCh
//		arrays - the native format of the Xerces parser.
//		Constants are dynamically created during loading of the dxl library.
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPDXL_dxltokens_H
#define GPDXL_dxltokens_H

#include "gpos/base.h"
#include "gpos/string/CWStringConst.h"
#include "gpos/common/CHashMap.h"

#include <xercesc/util/XMLUniDefs.hpp>
#include <xercesc/sax2/DefaultHandler.hpp>

namespace gpdxl
{
	using namespace gpos;
	
	XERCES_CPP_NAMESPACE_USE
	
	//fwd decl
	class CDXLMemoryManager;
	
	enum Edxltoken
	{
		EdxltokenDXLMessage,
		
		EdxltokenPlan,
		EdxltokenPlanId,
		EdxltokenPlanSpaceSize,
		EdxltokenSamplePlans,
		EdxltokenSamplePlan,
		EdxltokenCostDistr,
		EdxltokenRelativeCost,
		EdxltokenX,
		EdxltokenY,
		
		EdxltokenOptimizerConfig,
		EdxltokenEnumeratorConfig,
		EdxltokenStatisticsConfig,
		EdxltokenDampingFactorFilter,
		EdxltokenDampingFactorJoin,
		EdxltokenDampingFactorGroupBy,
		EdxltokenCTEConfig,
		EdxltokenCTEInliningCutoff,
		EdxltokenCostModelConfig,
		EdxltokenCostModelType,
		EdxltokenSegmentsForCosting,

		EdxltokenPlanSamples,

		EdxltokenMetadata,
		EdxltokenTraceFlags,
		EdxltokenMDRequest,
		
		EdxltokenSysids,
		EdxltokenSysid,
		
		EdxltokenThread,
		
		EdxltokenPhysical,
		
		EdxltokenPhysicalTableScan,
		EdxltokenPhysicalBitmapTableScan,
		EdxltokenPhysicalDynamicBitmapTableScan,
		EdxltokenPhysicalExternalScan,
		EdxltokenPhysicalIndexScan,
		EdxltokenPhysicalIndexOnlyScan,
		EdxltokenPhysicalHashJoin,
		EdxltokenPhysicalNLJoin,
		EdxltokenPhysicalNLJoinIndex,
		EdxltokenPhysicalMergeJoin,
		EdxltokenPhysicalGatherMotion,
		EdxltokenPhysicalBroadcastMotion,
		EdxltokenPhysicalRedistributeMotion,
		EdxltokenPhysicalRoutedDistributeMotion,
		EdxltokenPhysicalRandomMotion,
		EdxltokenPhysicalSort,
		EdxltokenPhysicalLimit,
		EdxltokenPhysicalResult,
		EdxltokenPhysicalSubqueryScan,
		EdxltokenPhysicalAggregate,
		EdxltokenPhysicalAppend,
		EdxltokenPhysicalSharedScan,
		EdxltokenPhysicalMaterialize,
		EdxltokenPhysicalSequence,
		EdxltokenPhysicalDynamicTableScan,
		EdxltokenPhysicalDynamicIndexScan,
		EdxltokenPhysicalTVF,
		EdxltokenPhysicalWindow,
		EdxltokenPhysicalPartitionSelector,
		EdxltokenPhysicalPartitionSelectorLevels,
		EdxltokenPhysicalPartitionSelectorScanId,

		EdxltokenPhysicalCTEProducer,
		EdxltokenPhysicalCTEConsumer,

		EdxltokenDuplicateSensitive,
		
		EdxltokenPartIndexId,
		EdxltokenPartIndexIdPrintable,
		EdxltokenSegmentIdCol,
		
		EdxltokenScalar,
		
		EdxltokenScalarProjList,
		EdxltokenScalarFilter,
		EdxltokenScalarAggref,
		EdxltokenScalarWindowref,
		EdxltokenScalarArrayComp,
		EdxltokenScalarBoolTestIsTrue,
		EdxltokenScalarBoolTestIsNotTrue,
		EdxltokenScalarBoolTestIsFalse,
		EdxltokenScalarBoolTestIsNotFalse,
		EdxltokenScalarBoolTestIsUnknown,
		EdxltokenScalarBoolTestIsNotUnknown,
		EdxltokenScalarBoolAnd,
		EdxltokenScalarBoolOr,
		EdxltokenScalarBoolNot,
		EdxltokenScalarMin,
		EdxltokenScalarMax,
		EdxltokenScalarCaseTest,
		EdxltokenScalarCoalesce,
		EdxltokenScalarComp,
		EdxltokenScalarConstValue,
		EdxltokenScalarDistinctComp,
		EdxltokenScalarFuncExpr,
		EdxltokenScalarIsNull,
		EdxltokenScalarIsNotNull,
		EdxltokenScalarNullIf,
		EdxltokenScalarHashCondList,
		EdxltokenScalarMergeCondList,
		EdxltokenScalarHashExprList,
		EdxltokenScalarHashExpr,
		EdxltokenScalarIdent,
		EdxltokenScalarIfStmt,
		EdxltokenScalarSwitch,
		EdxltokenScalarSwitchCase,
		EdxltokenScalarInitPlan,
		EdxltokenScalarSubPlan,
		EdxltokenScalarJoinFilter,
		EdxltokenScalarRecheckCondFilter,
		EdxltokenScalarLimitCount,
		EdxltokenScalarLimitOffset,
		EdxltokenScalarOneTimeFilter,
		EdxltokenScalarOpExpr,
		EdxltokenScalarProjElem,
		EdxltokenScalarCast,
		EdxltokenScalarCoerceToDomain,
		EdxltokenScalarSortCol,
		EdxltokenScalarSortColList,
		EdxltokenScalarGroupingColList,
		
		EdxltokenScalarBitmapAnd,
		EdxltokenScalarBitmapOr,
		
		EdxltokenScalarArray,
		EdxltokenScalarArrayRef,
		EdxltokenScalarArrayRefIndexList,
		EdxltokenScalarArrayRefIndexListBound,
		EdxltokenScalarArrayRefIndexListLower,
		EdxltokenScalarArrayRefIndexListUpper,
		EdxltokenScalarArrayRefExpr,
		EdxltokenScalarArrayRefAssignExpr,
		
		EdxltokenScalarAssertConstraint,
		EdxltokenScalarAssertConstraintList,

		EdxltokenScalarSubPlanType,
		EdxltokenScalarSubPlanTypeScalar,
		EdxltokenScalarSubPlanTypeExists,
		EdxltokenScalarSubPlanTypeNotExists,
		EdxltokenScalarSubPlanTypeAny,
		EdxltokenScalarSubPlanTypeAll,
		EdxltokenScalarSubPlanTestExpr,
		EdxltokenScalarSubPlanParamList,
		EdxltokenScalarSubPlanParam,

		EdxltokenScalarSubquery,
		EdxltokenScalarSubqueryAll,
		EdxltokenScalarSubqueryAny,
		EdxltokenScalarSubqueryExists,
		EdxltokenScalarSubqueryNotExists,

		EdxltokenScalarDMLAction,
		EdxltokenScalarOpList,

		EdxltokenPartLevelEqFilterList,
		EdxltokenPartLevelFilterList,
		EdxltokenPartLevel,
		EdxltokenScalarPartOid,
		EdxltokenScalarPartDefault,
		EdxltokenScalarPartBound,
		EdxltokenScalarPartBoundLower,
		EdxltokenScalarPartBoundInclusion,
		EdxltokenScalarPartBoundOpen,
		EdxltokenScalarResidualFilter,
		EdxltokenScalarPropagationExpr,
		EdxltokenScalarPrintableFilter,
		EdxltokenScalarBitmapIndexProbe,

		EdxltokenWindowFrame,
		EdxltokenScalarWindowFrameLeadingEdge,
		EdxltokenScalarWindowFrameTrailingEdge,
		EdxltokenWindowKeyList,
		EdxltokenWindowKey,

		EdxltokenWindowSpecList,
		EdxltokenWindowSpec,

		EdxltokenWindowFrameSpec,
		EdxltokenWindowFSRow,
		EdxltokenWindowFSRange,

		EdxltokenWindowLeadingBoundary,
		EdxltokenWindowTrailingBoundary,
		EdxltokenWindowBoundaryUnboundedPreceding,
		EdxltokenWindowBoundaryBoundedPreceding,
		EdxltokenWindowBoundaryCurrentRow,
		EdxltokenWindowBoundaryUnboundedFollowing,
		EdxltokenWindowBoundaryBoundedFollowing,
		EdxltokenWindowBoundaryDelayedBoundedPreceding,
		EdxltokenWindowBoundaryDelayedBoundedFollowing,

		EdxltokenWindowExclusionStrategy,
		EdxltokenWindowESNone,
		EdxltokenWindowESNulls,
		EdxltokenWindowESCurrentRow,
		EdxltokenWindowESGroup,
		EdxltokenWindowESTies,

		EdxltokenWindowrefOid,
		EdxltokenWindowrefDistinct,
		EdxltokenWindowrefStrategy,
		EdxltokenWindowrefStageImmediate,
		EdxltokenWindowrefStagePreliminary,
		EdxltokenWindowrefStageRowKey,
		EdxltokenWindowrefWinSpecPos,

		EdxltokenValue,
		EdxltokenTypeId,
		EdxltokenTypeIds,
		
		EdxltokenConstTuple,
		EdxltokenDatum,

		// CoerceToDomain related tokens
		EdxltokenTypeMod,
		EdxltokenCoercionForm,
		EdxltokenLocation,

		EdxltokenJoinType,
		EdxltokenJoinInner,
		EdxltokenJoinLeft,
		EdxltokenJoinFull,
		EdxltokenJoinRight,
		EdxltokenJoinIn,
		EdxltokenJoinLeftAntiSemiJoin,
		EdxltokenJoinLeftAntiSemiJoinNotIn,
		
		EdxltokenMergeJoinUniqueOuter,
		
		EdxltokenAggStrategy,
		EdxltokenAggStrategyPlain,
		EdxltokenAggStrategySorted,
		EdxltokenAggStrategyHashed,
		EdxltokenAggStreamSafe,
		
		EdxltokenAggrefOid,
		EdxltokenAggrefDistinct,
		EdxltokenAggrefStage,
		EdxltokenAggrefLookups,
		EdxltokenAggrefStageNormal,
		EdxltokenAggrefStagePartial,
		EdxltokenAggrefStageIntermediate,
		EdxltokenAggrefStageFinal,
		
		EdxltokenArrayType,
		EdxltokenArrayElementType,
		EdxltokenArrayMultiDim,
		
		EdxltokenTableDescr,
		EdxltokenProperties,
		EdxltokenOutputCols,
		EdxltokenCost,
		EdxltokenStartupCost,
		EdxltokenTotalCost,
		EdxltokenRows,
		EdxltokenWidth,
		EdxltokenCTASOptions,
		EdxltokenCTASOption,
		
		EdxltokenExecuteAsUser,
		
		EdxltokenAlias,
		EdxltokenTableName,
		EdxltokenDerivedTableName,

		EdxltokenColDescr,
		EdxltokenColRef,
		
		EdxltokenColumns,
		EdxltokenColumn,
		EdxltokenColName,
		EdxltokenTagColType,
		EdxltokenColId,
		EdxltokenAttno,
		EdxltokenColDropped,
		EdxltokenColWidth,
		EdxltokenColNullFreq,
		EdxltokenColNdvRemain,
		EdxltokenColFreqRemain,
		EdxltokenColStatsMissing,

		EdxltokenCtidColName,
		EdxltokenOidColName,
		EdxltokenXminColName,
		EdxltokenCminColName,
		EdxltokenXmaxColName,
		EdxltokenCmaxColName,
		EdxltokenTableOidColName,
		EdxltokenGpSegmentIdColName,
		
		EdxltokenActionColId,
		EdxltokenOidColId,
		EdxltokenCtidColId,
		EdxltokenGpSegmentIdColId,
		EdxltokenTupleOidColId,
		EdxltokenUpdatePreservesOids,
		
		EdxltokenInputSegments,
		EdxltokenOutputSegments,
		EdxltokenSegment,
		EdxltokenSegId,
		
		EdxltokenGroupingCols,
		EdxltokenGroupingCol,
		
		EdxltokenParamKind,
		
		EdxltokenAppendIsTarget,
		EdxltokenAppendIsZapped,
		
		EdxltokenOpNo,
		EdxltokenOpName,
		
		EdxltokenOpType,
		EdxltokenOpTypeAny,
		EdxltokenOpTypeAll,
		
		EdxltokenTypeName,
		EdxltokenUnknown,

		EdxltokenFuncId,
		EdxltokenFuncRetSet,
		

		EdxltokenSortOpId,
		EdxltokenSortOpName,
		EdxltokenSortDiscardDuplicates,
		EdxltokenSortNullsFirst,
		
		EdxltokenMaterializeEager,
		EdxltokenSpoolId,
		EdxltokenSpoolType,
		EdxltokenSpoolMaterialize,
		EdxltokenSpoolSort,
		EdxltokenSpoolMultiSlice,
		EdxltokenExecutorSliceId,
		EdxltokenConsumerSliceCount,
		
		EdxltokenComparisonOp,

		EdxltokenXMLDocHeader,
		EdxltokenNamespaceAttr,
		EdxltokenNamespacePrefix,
		EdxltokenNamespaceURI,
		
		EdxltokenBracketOpenTag,
		EdxltokenBracketCloseTag,
		EdxltokenBracketOpenEndTag,
		EdxltokenBracketCloseSingletonTag,
		EdxltokenColon,
		EdxltokenSemicolon,
		EdxltokenComma,
		EdxltokenDot,		
		EdxltokenDotSemicolon,
		EdxltokenSpace,
		EdxltokenQuote,
		EdxltokenEq,
		EdxltokenIndent,
		
		EdxltokenTrue,
		EdxltokenFalse,
		
		// metadata-related constants
		EdxltokenRelation,
		EdxltokenRelationExternal,
		EdxltokenRelationCTAS,
		EdxltokenName,
		EdxltokenSchema,
		EdxltokenTablespace,
		EdxltokenOid,
		EdxltokenVersion,
		EdxltokenMdid,
		EdxltokenMDTypeRequest,
		EdxltokenTypeInfo,
		EdxltokenFuncInfo,
		EdxltokenRelationMdid,
		EdxltokenRelationStats,
		EdxltokenColumnStats,
		EdxltokenColumnStatsBucket,
		EdxltokenEmptyRelation,
		EdxltokenIsByValue,
		EdxltokenIsNull,
		EdxltokenLintValue,
		EdxltokenDoubleValue,
		
		EdxltokenRelTemporary,
		EdxltokenRelHasOids,
		
		EdxltokenRelStorageType,
		EdxltokenRelStorageHeap,
		EdxltokenRelStorageAppendOnlyCols,
		EdxltokenRelStorageAppendOnlyRows,
		EdxltokenRelStorageAppendOnlyParquet,
		EdxltokenRelStorageVirtual,
		EdxltokenRelStorageExternal,
		
		EdxltokenPartKeys,
		
		EdxltokenRelDistrPolicy,
		EdxltokenRelDistrMasterOnly,
		EdxltokenRelDistrHash,
		EdxltokenRelDistrRandom,
		EdxltokenConvertHashToRandom,
		
		EdxltokenExtRelRejLimit,
		EdxltokenExtRelRejLimitInRows,
		EdxltokenExtRelFmtErrRel,

		EdxltokenMetadataColumns,
		EdxltokenMetadataColumn,
		EdxltokenColumnNullable,
		EdxltokenColumnDefaultValue,
		
		EdxltokenKeys,
		EdxltokenDistrColumns,
		
		EdxltokenIndexKeyCols,
		EdxltokenIndexIncludedCols,
		EdxltokenIndexClustered,
		EdxltokenIndexPartial,
		EdxltokenIndexType,
		EdxltokenIndexTypeBtree,
		EdxltokenIndexTypeBitmap,
		EdxltokenIndexItemType,
		
		EdxltokenOpClass,
		EdxltokenOpClasses,
		
		EdxltokenTypeInt4,
		EdxltokenTypeBool,
		
		EdxltokenMetadataIdList,
		
		EdxltokenIndexes,
		EdxltokenIndex,
		EdxltokenPartitions,
		EdxltokenPartition,
		
		EdxltokenConstraints,
		EdxltokenConstraint,
		
		EdxltokenCheckConstraints,
		EdxltokenCheckConstraint,
		EdxltokenPartConstraint,
		EdxltokenDefaultPartition,
		EdxltokenPartConstraintUnbounded,
		
		EdxltokenMDType,
		EdxltokenMDTypeRedistributable,
		EdxltokenMDTypeHashable,
		EdxltokenMDTypeComposite,
		EdxltokenMDTypeFixedLength,
		EdxltokenMDTypeLength,
		EdxltokenMDTypeByValue,
		EdxltokenMDTypeEqOp,
		EdxltokenMDTypeNEqOp,
		EdxltokenMDTypeLTOp,
		EdxltokenMDTypeLEqOp,
		EdxltokenMDTypeGTOp,
		EdxltokenMDTypeGEqOp,
		EdxltokenMDTypeCompOp,
		EdxltokenMDTypeHashOp,
		EdxltokenMDTypeArray,
		EdxltokenMDTypeRelid,
		
		EdxltokenMDTypeAggMin,
		EdxltokenMDTypeAggMax,
		EdxltokenMDTypeAggAvg,
		EdxltokenMDTypeAggSum,
		EdxltokenMDTypeAggCount,
		
		EdxltokenGPDBScalarOp,
		EdxltokenGPDBScalarOpLeftTypeId,
		EdxltokenGPDBScalarOpRightTypeId,
		EdxltokenGPDBScalarOpResultTypeId,
		EdxltokenGPDBScalarOpFuncId,
		EdxltokenGPDBScalarOpCommOpId,
		EdxltokenGPDBScalarOpInverseOpId,
		EdxltokenGPDBScalarOpLTOpId,
		EdxltokenGPDBScalarOpGTOpId,
		EdxltokenGPDBScalarOpCmpType,
		
		EdxltokenCmpEq, 
		EdxltokenCmpNeq,	
		EdxltokenCmpLt, 
		EdxltokenCmpLeq, 
		EdxltokenCmpGt, 
		EdxltokenCmpGeq, 
		EdxltokenCmpIDF, 
		EdxltokenCmpOther,
		
		EdxltokenReturnsNullOnNullInput,

		EdxltokenTriggers,
		EdxltokenTrigger,

		EdxltokenGPDBTrigger,
		EdxltokenGPDBTriggerRow,
		EdxltokenGPDBTriggerBefore,
		EdxltokenGPDBTriggerInsert,
		EdxltokenGPDBTriggerDelete,
		EdxltokenGPDBTriggerUpdate,
		EdxltokenGPDBTriggerEnabled,

		EdxltokenGPDBFunc,
		EdxltokenGPDBFuncStability,
		EdxltokenGPDBFuncStable,
		EdxltokenGPDBFuncImmutable,
		EdxltokenGPDBFuncVolatile,
		EdxltokenGPDBFuncDataAccess,
		EdxltokenGPDBFuncNoSQL,
		EdxltokenGPDBFuncContainsSQL,
		EdxltokenGPDBFuncReadsSQLData,
		EdxltokenGPDBFuncModifiesSQLData,
		EdxltokenGPDBFuncResultTypeId,
		EdxltokenGPDBFuncReturnsSet,
		EdxltokenGPDBFuncStrict,
		
		EdxltokenGPDBCast,
		EdxltokenGPDBCastBinaryCoercible,
		EdxltokenGPDBCastSrcType,
		EdxltokenGPDBCastDestType,
		EdxltokenGPDBCastFuncId,
		
		EdxltokenGPDBMDScCmp,
		
		EdxltokenGPDBAgg,
		EdxltokenGPDBIsAggOrdered,
		EdxltokenGPDBAggResultTypeId,
		EdxltokenGPDBAggIntermediateResultTypeId,
		EdxltokenGPDBAggSplittable,
		EdxltokenGPDBAggHashAggCapable,
		
		EdxltokenEntireRow,

		EdxltokenScalarExpr,  // top level scalar expression
		EdxltokenQuery,
		EdxltokenQueryOutput,
		EdxltokenLogical,
		EdxltokenLogicalGet,
		EdxltokenLogicalExternalGet,
		EdxltokenLogicalProject,
		EdxltokenLogicalSelect,
		EdxltokenLogicalJoin,
		EdxltokenLogicalCTEProducer,
		EdxltokenLogicalCTEConsumer,
		EdxltokenCTEList,
		EdxltokenLogicalCTEAnchor,
		EdxltokenLogicalLimit,
		EdxltokenLogicalOutput,
		EdxltokenLogicalConstTable,
		EdxltokenLogicalGrpBy,
		EdxltokenLogicalSetOperation,
		EdxltokenLogicalTVF,
		EdxltokenLogicalWindow,
		
		EdxltokenLogicalInsert,
		EdxltokenLogicalDelete,
		EdxltokenLogicalUpdate,
		EdxltokenLogicalCTAS,
		EdxltokenPhysicalCTAS,
		EdxltokenPhysicalDMLInsert,
		EdxltokenPhysicalDMLDelete,
		EdxltokenPhysicalDMLUpdate,
		EdxltokenDirectDispatchInfo,
		EdxltokenDirectDispatchKeyValue,
		EdxltokenPhysicalSplit,
		EdxltokenPhysicalRowTrigger,
		EdxltokenPhysicalAssert,
		
		EdxltokenErrorCode,
		EdxltokenErrorMessage,
		
		EdxltokenOnCommitAction,
		EdxltokenOnCommitNOOP,
		EdxltokenOnCommitPreserve,
		EdxltokenOnCommitDelete,
		EdxltokenOnCommitDrop,
		
		EdxltokenInsertCols,
		EdxltokenDeleteCols,
		EdxltokenNewCols,
		EdxltokenOldCols,

		EdxltokenCTEId,

		EdxltokenLogicalGrpCols,

		EdxltokenInputCols,
		EdxltokenCastAcrossInputs,

		EdxltokenLogicalUnion,
		EdxltokenLogicalUnionAll,
		EdxltokenLogicalIntersect,
		EdxltokenLogicalIntersectAll,
		EdxltokenLogicalDifference,
		EdxltokenLogicalDifferenceAll,

		EdxltokenIndexDescr,
		EdxltokenIndexName,
		EdxltokenIndexScanDirection,
		EdxltokenIndexScanDirectionForward,
		EdxltokenIndexScanDirectionBackward,
		EdxltokenIndexScanDirectionNoMovement,
		EdxltokenScalarIndexCondList,

		EdxltokenStackTrace,
		
		EdxltokenStatistics,
		EdxltokenStatsBaseRelation,
		EdxltokenStatsDerivedRelation,
		EdxltokenStatsDerivedColumn,
		EdxltokenStatsBucketLowerBound,
		EdxltokenStatsBucketUpperBound,
		EdxltokenStatsFrequency,
		EdxltokenStatsDistinct,
		EdxltokenStatsBoundClosed,

		// search strategy
		EdxltokenSearchStrategy,
		EdxltokenSearchStage,
		EdxltokenXform,
		EdxltokenTimeThreshold,
		EdxltokenCostThreshold,

		// cost model parameters
		EdxltokenCostParams,
		EdxltokenCostParam,
		EdxltokenCostParamLowerBound,
		EdxltokenCostParamUpperBound,

		EdxltokenTopLimitUnderDML,

		EdxltokenCtasOptionType,
		EdxltokenVarTypeModList,

		EdxltokenSentinel
	};

	//---------------------------------------------------------------------------
	//	@class:
	//		CDXLTokens
	//
	//	@doc:
	//		DXL tokens.
	//
	//---------------------------------------------------------------------------
	class CDXLTokens
	{
		private:
		
			// element for mapping Edxltoken to WCHARs
			struct SWszMapElem
			{
				Edxltoken m_edxlt;
				const WCHAR *m_wsz;				
			};
			
			// element for mapping Edxltoken to CWStringConst
			struct SStrMapElem
			{
				Edxltoken m_edxlt;
				CWStringConst *m_pstr;

				// ctor
				SStrMapElem()
					:
					m_edxlt(EdxltokenSentinel),
					m_pstr(NULL)
				{}
				
				// ctor
				SStrMapElem
					(
					Edxltoken edxlt,
					CWStringConst *pstr
					)
					:
					m_edxlt(edxlt),
					m_pstr(pstr)
				{
					GPOS_ASSERT(edxlt < EdxltokenSentinel);
					GPOS_ASSERT(pstr->FValid());
				}
				
				//dtor
				~SStrMapElem()
				{
					GPOS_DELETE(m_pstr);
				}
			};
			
			// element for mapping Edxltoken to XML string
			struct SXMLStrMapElem
			{
				Edxltoken m_edxlt;
				XMLCh *m_xmlsz;
				
				// ctor
				SXMLStrMapElem()
					:
					m_edxlt(EdxltokenSentinel),
					m_xmlsz(NULL)
				{}
				
				// ctor
				SXMLStrMapElem
					(
					Edxltoken edxlt,
					XMLCh *xmlsz
					)
					:
					m_edxlt(edxlt),
					m_xmlsz(xmlsz)
				{
					GPOS_ASSERT(edxlt < EdxltokenSentinel);
					GPOS_ASSERT(NULL != xmlsz);
				}
				
				//dtor
				~SXMLStrMapElem()
				{
					GPOS_DELETE_ARRAY(reinterpret_cast<BYTE*>(m_xmlsz));
				}				
			};
			
			// array maintaining the mapping Edxltoken -> CWStringConst
			static
			SStrMapElem *m_pstrmap;
			
			// array maintaining the mapping Edxltoken -> XML string
			static
			SXMLStrMapElem *m_pxmlszmap;

			// memory pool -- not owned
			static
			IMemoryPool *m_pmp;

			// local dxl memory manager
			static
			CDXLMemoryManager *m_pmm;

			// create a string in Xerces XMLCh* format
			static 
			XMLCh *XmlstrFromWsz(const WCHAR *wsz);
			
		public:
			
			// retrieve a token in CWStringConst and XMLCh* format, respectively
			static 
			const CWStringConst *PstrToken(Edxltoken edxltoken);
			
			static 
			const XMLCh *XmlstrToken(Edxltoken edxltoken);
		
			// initialize constants. Must be called before constants are accessed.
			static 
			void Init(IMemoryPool *pmp);

			// cleanup tokens
			static 
			void Terminate();
			
	};
	 
}

#endif // !GPDXL_dxltokens_H

// EOF
