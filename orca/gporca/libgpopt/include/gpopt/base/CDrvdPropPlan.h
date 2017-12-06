//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CDrvdPropPlan.h
//
//	@doc:
//		Derived physical properties
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CDrvdPropPlan_H
#define GPOPT_CDrvdPropPlan_H

#include "gpos/base.h"
#include "gpos/common/CRefCount.h"

#include "gpopt/base/CColRef.h"
#include "gpopt/base/CPartFilterMap.h"
#include "gpopt/base/CDrvdProp.h"

namespace gpopt
{
	using namespace gpos;

	// fwd declaration
	class CDistributionSpec;
	class CExpressionHandle;
	class COrderSpec;
	class CRewindabilitySpec;
	class CReqdPropPlan;
	class CPartIndexMap;
	class CCTEMap;

	//---------------------------------------------------------------------------
	//	@class:
	//		CDrvdPropPlan
	//
	//	@doc:
	//		Derived plan properties container.
	//
	//---------------------------------------------------------------------------
	class CDrvdPropPlan : public CDrvdProp
	{

		private:

			// derived sort order
			COrderSpec *m_pos;

			// derived distribution
			CDistributionSpec *m_pds;

			// derived rewindability
			CRewindabilitySpec *m_prs;

			// derived partition index map
			CPartIndexMap *m_ppim;
			
			// derived filter expressions indexed by the part index id
			CPartFilterMap *m_ppfm;

			// derived cte map
			CCTEMap *m_pcm;

			 // copy CTE producer plan properties from given context to current object
			void CopyCTEProducerPlanProps(IMemoryPool *pmp, CDrvdPropCtxt *pdpctxt, COperator *pop);

			// private copy ctor
			CDrvdPropPlan(const CDrvdPropPlan &);

		public:

			// ctor
			CDrvdPropPlan();

			// dtor
			virtual 
			~CDrvdPropPlan();

			// type of properties
			virtual
			EPropType Ept()
			{
				return EptPlan;
			}

			// derivation function
			void Derive(IMemoryPool *pmp, CExpressionHandle &exprhdl, CDrvdPropCtxt *pdpctxt);

			// short hand for conversion
			static
			CDrvdPropPlan *Pdpplan(CDrvdProp *pdp);

			// sort order accessor
			COrderSpec *Pos() const
			{
				return m_pos;
			}

			// distribution accessor
			CDistributionSpec *Pds() const
			{
				return m_pds;
			}

			// rewindability accessor
			CRewindabilitySpec *Prs() const
			{
				return m_prs;
			}
			
			// partition index map
			CPartIndexMap *Ppim() const
			{
				return m_ppim;
			}

			// partition filter map
			CPartFilterMap *Ppfm() const
			{
				return m_ppfm;
			}

			// cte map
			CCTEMap *Pcm() const
			{
				return m_pcm;
			}

			// hash function
			virtual
			ULONG UlHash() const;

			// equality function
			virtual
			ULONG FEqual(const CDrvdPropPlan *pdpplan) const;

			// check for satisfying required plan properties
			virtual
			BOOL FSatisfies(const CReqdPropPlan *prpp) const;

			// print function
			virtual
			IOstream &OsPrint(IOstream &os) const;

	}; // class CDrvdPropPlan
	
}


#endif // !GPOPT_CDrvdPropPlan_H

// EOF
