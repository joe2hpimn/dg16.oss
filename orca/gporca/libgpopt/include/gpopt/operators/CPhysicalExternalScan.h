//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2013 Pivotal, Inc.
//
//	@filename:
//		CPhysicalExternalScan.h
//
//	@doc:
//		External scan operator
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CPhysicalExternalScan_H
#define GPOPT_CPhysicalExternalScan_H

#include "gpos/base.h"
#include "gpopt/operators/CPhysicalTableScan.h"

namespace gpopt
{

	//---------------------------------------------------------------------------
	//	@class:
	//		CPhysicalExternalScan
	//
	//	@doc:
	//		External scan operator
	//
	//---------------------------------------------------------------------------
	class CPhysicalExternalScan : public CPhysicalTableScan
	{

		private:

			// private copy ctor
			CPhysicalExternalScan(const CPhysicalExternalScan&);

		public:

			// ctor
			CPhysicalExternalScan(IMemoryPool *, const CName *, CTableDescriptor *, DrgPcr *);

			// ident accessors
			virtual
			EOperatorId Eopid() const
			{
				return EopPhysicalExternalScan;
			}

			// return a string for operator name
			virtual
			const CHAR *SzId() const
			{
				return "CPhysicalExternalScan";
			}

			// match function
			virtual
			BOOL FMatch(COperator *) const;

			//-------------------------------------------------------------------------------------
			// Derived Plan Properties
			//-------------------------------------------------------------------------------------

			// derive rewindability
			virtual
			CRewindabilitySpec *PrsDerive
				(
				IMemoryPool *pmp,
				CExpressionHandle & // exprhdl
				)
				const
			{
				// external tables are not rewindable
				return GPOS_NEW(pmp) CRewindabilitySpec(CRewindabilitySpec::ErtNone /*ert*/);
			}

			//-------------------------------------------------------------------------------------
			// Enforced Properties
			//-------------------------------------------------------------------------------------

			// return rewindability property enforcing type for this operator
			virtual
			CEnfdProp::EPropEnforcingType EpetRewindability
				(
				CExpressionHandle &exprhdl,
				const CEnfdRewindability *per
				)
				const;

			//-------------------------------------------------------------------------------------
			//-------------------------------------------------------------------------------------
			//-------------------------------------------------------------------------------------

			// conversion function
			static
			CPhysicalExternalScan *PopConvert
				(
				COperator *pop
				)
			{
				GPOS_ASSERT(NULL != pop);
				GPOS_ASSERT(EopPhysicalExternalScan == pop->Eopid());

				return reinterpret_cast<CPhysicalExternalScan*>(pop);
			}

	}; // class CPhysicalExternalScan

}

#endif // !GPOPT_CPhysicalExternalScan_H

// EOF
