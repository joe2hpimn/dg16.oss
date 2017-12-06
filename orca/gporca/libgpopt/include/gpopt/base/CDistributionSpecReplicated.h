//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CDistributionSpecReplicated.h
//
//	@doc:
//		Description of a replicated distribution; 
//		Can be used as required or derived property;
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CDistributionSpecReplicated_H
#define GPOPT_CDistributionSpecReplicated_H

#include "gpos/base.h"

#include "gpopt/base/CDistributionSpec.h"
#include "gpopt/base/CDistributionSpecSingleton.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CDistributionSpecReplicated
	//
	//	@doc:
	//		Class for representing replicated distribution specification.
	//
	//---------------------------------------------------------------------------
	class CDistributionSpecReplicated : public CDistributionSpec
	{
		private:

			// private copy ctor
			CDistributionSpecReplicated(const CDistributionSpecReplicated &);
			
		public:
			// ctor
			CDistributionSpecReplicated()
			{}
			
			// accessor
			virtual 
			EDistributionType Edt() const
			{
				return CDistributionSpec::EdtReplicated;
			}
			
			// does this distribution satisfy the given one
			virtual 
			BOOL FSatisfies(const CDistributionSpec *pds) const;
			
			// append enforcers to dynamic array for the given plan properties
			virtual
			void AppendEnforcers(IMemoryPool *pmp, CExpressionHandle &exprhdl, CReqdPropPlan *prpp, DrgPexpr *pdrgpexpr, CExpression *pexpr);

			// return distribution partitioning type
			virtual
			EDistributionPartitioningType Edpt() const
			{
				return EdptNonPartitioned;
			}

			// print
			virtual
			IOstream &OsPrint(IOstream &os) const
			{
				return os << "REPLICATED ";
			}
			
			// conversion function
			static
			CDistributionSpecReplicated *PdsConvert
				(
				CDistributionSpec *pds
				)
			{
				GPOS_ASSERT(NULL != pds);
				GPOS_ASSERT(EdtReplicated == pds->Edt());

				return dynamic_cast<CDistributionSpecReplicated*>(pds);
			}

	}; // class CDistributionSpecReplicated

}

#endif // !GPOPT_CDistributionSpecReplicated_H

// EOF
