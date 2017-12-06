//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2014 Pivotal Inc.
//
//	@filename:
//		CCostModelGPDB.h
//
//	@doc:
//		GPDB cost model
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPDBCOST_CCostModelGPDB_H
#define GPDBCOST_CCostModelGPDB_H

#include "gpos/base.h"
#include "gpos/common/CDouble.h"

#include "gpopt/cost/CCost.h"
#include "gpopt/cost/ICostModel.h"
#include "gpopt/cost/ICostModelParams.h"

#include "gpdbcost/CCostModelParamsGPDB.h"


namespace gpdbcost
{
	using namespace gpos;
	using namespace gpopt;
	using namespace gpmd;


	//---------------------------------------------------------------------------
	//	@class:
	//		CCostModelGPDB
	//
	//	@doc:
	//		GPDB cost model
	//
	//---------------------------------------------------------------------------
	class CCostModelGPDB : public ICostModel
	{

		private:

			// definition of operator processor
			typedef CCost(FnCost)(IMemoryPool *, CExpressionHandle &, const CCostModelGPDB *, const SCostingInfo *);

			//---------------------------------------------------------------------------
			//	@struct:
			//		SCostMapping
			//
			//	@doc:
			//		Mapping of operator to a cost function
			//
			//---------------------------------------------------------------------------
			struct SCostMapping
			{
				// physical operator id
				COperator::EOperatorId m_eopid;

				// pointer to cost function
				FnCost *m_pfnc;

			}; // struct SCostMapping

			// memory pool
			IMemoryPool *m_pmp;

			// number of segments
			ULONG m_ulSegments;

			// cost model parameters
			CCostModelParamsGPDB *m_pcp;

			// array of mappings
			static
			const SCostMapping m_rgcm[];

			// return cost of processing the given number of rows
			static
			CCost CostTupleProcessing(DOUBLE dRows, DOUBLE dWidth, ICostModelParams *pcp);

			// helper function to return cost of producing output tuples from Scan operator
			static
			CCost CostScanOutput(IMemoryPool *pmp, DOUBLE dRows, DOUBLE dWidth, DOUBLE dRebinds, ICostModelParams *pcp);

			// helper function to return cost of a plan rooted by unary operator
			static
			CCost CostUnary(IMemoryPool *pmp, CExpressionHandle &exprhdl, const SCostingInfo *pci, ICostModelParams *pcp);

			// cost of spooling
			static
			CCost CostSpooling(IMemoryPool *pmp, CExpressionHandle &exprhdl, const SCostingInfo *pci, ICostModelParams *pcp);

			// add up children cost
			static
			CCost CostChildren(IMemoryPool *pmp, CExpressionHandle &exprhdl, const SCostingInfo *pci, ICostModelParams *pcp);

			// check if given operator is unary
			static
			BOOL FUnary(COperator::EOperatorId eopid);

			// cost of scan
			static
			CCost CostScan(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of filter
			static
			CCost CostFilter(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of index scan
			static
			CCost CostIndexScan(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of bitmap table scan
			static
			CCost CostBitmapTableScan(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of sequence project
			static
			CCost CostSequenceProject(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of CTE producer
			static
			CCost CostCTEProducer(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of CTE consumer
			static
			CCost CostCTEConsumer(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of const table get
			static
			CCost CostConstTableGet(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of DML
			static
			CCost CostDML(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of hash agg
			static
			CCost CostHashAgg(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of scalar agg
			static
			CCost CostScalarAgg(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of stream agg
			static
			CCost CostStreamAgg(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of sequence
			static
			CCost CostSequence(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of sort
			static
			CCost CostSort(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of TVF
			static
			CCost CostTVF(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of UnionAll
			static
			CCost CostUnionAll(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of hash join
			static
			CCost CostHashJoin(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of nljoin
			static
			CCost CostNLJoin(IMemoryPool *pmp,CExpressionHandle &exprhdl,  const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of inner index-nljoin
			static
			CCost CostInnerIndexNLJoin(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of motion
			static
			CCost CostMotion(IMemoryPool *pmp, CExpressionHandle &exprhdl, const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci);

			// cost of bitmap scan when the NDV is small
			static
			CCost CostBitmapSmallNDV(const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci, CDouble dNDV);

			// cost of bitmap scan when the NDV is large
			static
			CCost CostBitmapLargeNDV(const CCostModelGPDB *pcmgpdb, const SCostingInfo *pci, CDouble dNDV);

		public:

			// ctor
			CCostModelGPDB(IMemoryPool *pmp, ULONG ulSegments, DrgPcp *pdrgpcp = NULL);

			// dtor
			virtual
			~CCostModelGPDB();

			// number of segments
			ULONG UlHosts() const
			{
				return m_ulSegments;
			}

			// return number of rows per host
			virtual
			CDouble DRowsPerHost(CDouble dRowsTotal) const;

			// return cost model parameters
			virtual
			ICostModelParams *Pcp() const
			{
				return m_pcp;
			}

			
			// main driver for cost computation
			virtual
			CCost Cost(CExpressionHandle &exprhdl, const SCostingInfo *pci) const;
			
			// cost model type
			virtual
			ECostModelType Ecmt() const
			{
				return ICostModel::EcmtGPDBCalibrated;
			}

	}; // class CCostModelGPDB

}

#endif // !GPDBCOST_CCostModelGPDB_H

// EOF
