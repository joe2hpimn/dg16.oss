//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2013 EMC Corp.
//
//	@filename:
//		COptimizerConfig.h
//
//	@doc:
//		Configurations used by the optimizer
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_COptimizerConfig_H
#define GPOPT_COptimizerConfig_H

#include "gpos/base.h"
#include "gpos/common/CDynamicPtrArray.h"
#include "gpos/common/CRefCount.h"

#include "gpopt/engine/CStatisticsConfig.h"
#include "gpopt/engine/CEnumeratorConfig.h"
#include "gpopt/engine/CCTEConfig.h"

namespace gpopt
{
	using namespace gpos;

	// fwd decl
	class ICostModel;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		COptimizerConfig
	//
	//	@doc:
	//		Configurations used by the optimizer
	//
	//---------------------------------------------------------------------------
	class COptimizerConfig : public CRefCount
	{

		private:
			
			// plan enumeration configuration
			CEnumeratorConfig *m_pec;

			// statistics configuration
			CStatisticsConfig *m_pstatsconf;

			// CTE configuration
			CCTEConfig *m_pcteconf;
			
			// cost model configuration
			ICostModel *m_pcm;

		public:

			// ctor
			COptimizerConfig
				(
				CEnumeratorConfig *pec,
				CStatisticsConfig *pstatsconf,
				CCTEConfig *pcteconf,
				ICostModel *pcm
				);

			// dtor
			virtual
			~COptimizerConfig();

			
			// plan enumeration configuration
			CEnumeratorConfig *Pec() const
			{
				return m_pec;
			}

			// statistics configuration
			CStatisticsConfig *Pstatsconf() const
			{
				return m_pstatsconf;
			}

			// CTE configuration
			CCTEConfig *Pcteconf() const
			{
				return m_pcteconf;
			}

			// cost model configuration
			ICostModel *Pcm() const
			{
				return m_pcm;
			}
			
			// generate default optimizer configurations
			static
			COptimizerConfig *PoconfDefault(IMemoryPool *pmp);
			
			// generate default optimizer configurations with the given cost model
			static
			COptimizerConfig *PoconfDefault(IMemoryPool *pmp, ICostModel *pcm);

	}; // class COptimizerConfig

}

#endif // !GPOPT_COptimizerConfig_H

// EOF
