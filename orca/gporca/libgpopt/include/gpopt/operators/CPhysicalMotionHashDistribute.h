//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CPhysicalMotionHashDistribute.h
//
//	@doc:
//		Physical Hash distribute motion operator
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CPhysicalMotionHashDistribute_H
#define GPOPT_CPhysicalMotionHashDistribute_H

#include "gpos/base.h"

#include "gpopt/base/CDistributionSpecHashed.h"
#include "gpopt/base/COrderSpec.h"
#include "gpopt/operators/CPhysicalMotion.h"

namespace gpopt
{
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CPhysicalMotionHashDistribute
	//
	//	@doc:
	//		Hash distribute motion operator
	//
	//---------------------------------------------------------------------------
	class CPhysicalMotionHashDistribute : public CPhysicalMotion
	{

		private:

			// hash distribution spec
			CDistributionSpecHashed *m_pdsHashed;
			
			// required columns in distribution spec
			CColRefSet *m_pcrsRequiredLocal;

			// private copy ctor
			CPhysicalMotionHashDistribute(const CPhysicalMotionHashDistribute &);

		public:
		
			// ctor
			CPhysicalMotionHashDistribute
				(
				IMemoryPool *pmp,
				CDistributionSpecHashed *pdsHashed
				);
			
			// dtor
			virtual 
			~CPhysicalMotionHashDistribute();

			// ident accessors
			virtual 
			EOperatorId Eopid() const
			{
				return EopPhysicalMotionHashDistribute;
			}
			
			virtual 
			const CHAR *SzId() const
			{
				return "CPhysicalMotionHashDistribute";
			}
			
			// output distribution accessor
			virtual
			CDistributionSpec *Pds() const
			{
				return m_pdsHashed;
			}
			
			// is motion eliminating duplicates
			BOOL FDuplicateSensitive() const
			{
				return m_pdsHashed->FDuplicateSensitive();
			}

			// match function
			virtual
			BOOL FMatch(COperator *) const;

			//-------------------------------------------------------------------------------------
			// Required Plan Properties
			//-------------------------------------------------------------------------------------

			// compute required output columns of the n-th child
			virtual
			CColRefSet *PcrsRequired
				(
				IMemoryPool *pmp,
				CExpressionHandle &exprhdl,
				CColRefSet *pcrsInput,
				ULONG ulChildIndex,
				DrgPdp *pdrgpdpCtxt,
				ULONG ulOptReq
				);

			// compute required sort order of the n-th child
			virtual
			COrderSpec *PosRequired
				(
				IMemoryPool *pmp,
				CExpressionHandle &exprhdl,
				COrderSpec *posInput,
				ULONG ulChildIndex,
				DrgPdp *pdrgpdpCtxt,
				ULONG ulOptReq
				)
				const;

			// check if required columns are included in output columns
			virtual
			BOOL FProvidesReqdCols(CExpressionHandle &exprhdl, CColRefSet *pcrsRequired, ULONG ulOptReq) const;

			//-------------------------------------------------------------------------------------
			// Derived Plan Properties
			//-------------------------------------------------------------------------------------

			// derive sort order
			virtual
			COrderSpec *PosDerive(IMemoryPool *pmp, CExpressionHandle &exprhdl) const;

			//-------------------------------------------------------------------------------------
			// Enforced Properties
			//-------------------------------------------------------------------------------------

			// return order property enforcing type for this operator
			virtual
			CEnfdProp::EPropEnforcingType EpetOrder
				(
				CExpressionHandle &exprhdl,
				const CEnfdOrder *peo
				)
				const;

			//-------------------------------------------------------------------------------------
			//-------------------------------------------------------------------------------------
			//-------------------------------------------------------------------------------------

			// print
			virtual 
			IOstream &OsPrint(IOstream &) const;
			
			// conversion function
			static
			CPhysicalMotionHashDistribute *PopConvert(COperator *pop);			
					
	}; // class CPhysicalMotionHashDistribute

}

#endif // !GPOPT_CPhysicalMotionHashDistribute_H

// EOF
