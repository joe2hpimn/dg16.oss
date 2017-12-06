//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CDistributionSpecRandom.h
//
//	@doc:
//		Description of a forced random distribution; 
//		Can be used as required or derived property;
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CDistributionSpecRandom_H
#define GPOPT_CDistributionSpecRandom_H

#include "gpos/base.h"

#include "gpopt/base/CDistributionSpec.h"

namespace gpopt
{
	using namespace gpos;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CDistributionSpecRandom
	//
	//	@doc:
	//		Class for representing forced random distribution.
	//
	//---------------------------------------------------------------------------
	class CDistributionSpecRandom : public CDistributionSpec
	{
		protected:

			// is the random distribution sensitive to duplicates
			BOOL m_fDuplicateSensitive;
			
			// does Singleton spec satisfy current distribution?
			// by default, Singleton satisfies hashed/random since all tuples with the same hash value
			// are moved to the same host/segment,
			// this flag adds the ability to mark a distribution request as non-satisfiable by Singleton
			// in case we need to enforce across segments distribution
			BOOL m_fSatisfiedBySingleton;

			// private copy ctor
			CDistributionSpecRandom(const CDistributionSpecRandom &);
			
		public:

			//ctor
			CDistributionSpecRandom();
			
			// accessor
			virtual 
			EDistributionType Edt() const
			{
				return CDistributionSpec::EdtRandom;
			}
			
			// is distribution duplicate sensitive
			BOOL FDuplicateSensitive() const
			{
				return m_fDuplicateSensitive;
			}
			
			// mark distribution as unsatisfiable by Singleton
			void MarkDuplicateSensitive()
			{
				GPOS_ASSERT(!m_fDuplicateSensitive);

				m_fDuplicateSensitive = true;
			}

			// does Singleton spec satisfy current distribution?
			BOOL FSatisfiedBySingleton() const
			{
				return m_fSatisfiedBySingleton;
			}

			// mark distribution as unsatisfiable by Singleton
			void MarkUnsatisfiableBySingleton()
			{
				GPOS_ASSERT(m_fSatisfiedBySingleton);

				m_fSatisfiedBySingleton = false;
			}

			// does this distribution match the given one
			virtual 
			BOOL FMatch(const CDistributionSpec *pds) const;
			
			// does current distribution satisfy the given one
			virtual 
			BOOL FSatisfies(const CDistributionSpec *pds) const;
			
			// append enforcers to dynamic array for the given plan properties
			virtual
			void AppendEnforcers(IMemoryPool *pmp, CExpressionHandle &exprhdl, CReqdPropPlan *prpp, DrgPexpr *pdrgpexpr, CExpression *pexpr);				

			// return distribution partitioning type
			virtual
			EDistributionPartitioningType Edpt() const
			{
				return EdptPartitioned;
			}

			// print
			virtual
			IOstream &OsPrint(IOstream &os) const;

			// conversion function
			static
			CDistributionSpecRandom *PdsConvert
				(
				CDistributionSpec *pds
				)
			{
				GPOS_ASSERT(NULL != pds);
				GPOS_ASSERT(EdtRandom == pds->Edt());

				return dynamic_cast<CDistributionSpecRandom*>(pds);
			}
			
			// conversion function: const argument
			static
			const CDistributionSpecRandom *PdsConvert
				(
				const CDistributionSpec *pds
				)
			{
				GPOS_ASSERT(NULL != pds);
				GPOS_ASSERT(EdtRandom == pds->Edt());

				return dynamic_cast<const CDistributionSpecRandom*>(pds);
			}
			
	}; // class CDistributionSpecRandom

}

#endif // !GPOPT_CDistributionSpecRandom_H

// EOF
