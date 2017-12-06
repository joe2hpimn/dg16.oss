//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CDrvdPropScalar.h
//
//	@doc:
//		Derived scalar properties
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CDrvdPropScalar_H
#define GPOPT_CDrvdPropScalar_H

#include "gpos/base.h"
#include "gpos/common/CRefCount.h"

#include "gpopt/base/CColRef.h"
#include "gpopt/base/CColRefSet.h"
#include "gpopt/base/CDrvdProp.h"
#include "gpopt/base/CPartInfo.h"
#include "gpopt/base/CFunctionProp.h"

namespace gpopt
{
	using namespace gpos;

	// forward declaration
	class CExpressionHandle;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CDrvdPropScalar
	//
	//	@doc:
	//		Derived scalar properties container.
	//
	//---------------------------------------------------------------------------
	class CDrvdPropScalar : public CDrvdProp
	{

		private:

			// defined columns
			CColRefSet *m_pcrsDefined;

			// used columns
			CColRefSet *m_pcrsUsed;

			// do subqueries appear in the operator's tree?
			BOOL m_fHasSubquery;

			// partition table consumers in subqueries
			CPartInfo *m_ppartinfo;

			// function properties
			CFunctionProp *m_pfp;

			// scalar expression contains non-scalar function?
			BOOL m_fHasNonScalarFunction;

			// total number of Distinct Aggs (e.g., {count(distinct a), sum(distinct a), count(distinct b)}, the value is 3),
			// only applicable to project lists
			ULONG m_ulDistinctAggs;

			// does operator define Distinct Aggs on different arguments (e.g., count(distinct a), sum(distinct b)),
			// only applicable to project lists
			BOOL m_fHasMultipleDistinctAggs;

			// private copy ctor
			CDrvdPropScalar(const CDrvdPropScalar &);

		public:

			// ctor
			CDrvdPropScalar();

			// dtor
			virtual 
			~CDrvdPropScalar();

			// type of properties
			virtual
			EPropType Ept()
			{
				return EptScalar;
			}

			// derivation function
			void Derive(IMemoryPool *pmp, CExpressionHandle &exprhdl, CDrvdPropCtxt *pdpctxt);

			// check for satisfying required plan properties
			virtual
			BOOL FSatisfies(const CReqdPropPlan *prpp) const;


			// defined columns
			CColRefSet *PcrsDefined() const
			{
				return m_pcrsDefined;
			}

			// used columns
			CColRefSet *PcrsUsed() const
			{
				return m_pcrsUsed;
			}

			// do subqueries appear in the operator's tree?
			BOOL FHasSubquery() const
			{
				return m_fHasSubquery;
			}

			// derived partition consumers
			CPartInfo *Ppartinfo() const
			{
				return m_ppartinfo;
			}
			
			// function properties
			CFunctionProp *Pfp() const
			{
				return m_pfp;
			}

			// scalar expression contains non-scalar function?
			virtual
			BOOL FHasNonScalarFunction() const
			{
				return m_fHasNonScalarFunction;
			}

			// return total number of Distinct Aggs, only applicable to project list
			ULONG UlDistinctAggs() const
			{
				return m_ulDistinctAggs;
			}

			// does operator define Distinct Aggs on different arguments, only applicable to project lists
			BOOL FHasMultipleDistinctAggs() const
			{
				return m_fHasMultipleDistinctAggs;
			}

			// short hand for conversion
			static
			CDrvdPropScalar *Pdpscalar(CDrvdProp *pdp);

			// print function
			virtual
			IOstream &OsPrint(IOstream &os) const;

	}; // class CDrvdPropScalar

}


#endif // !GPOPT_CDrvdPropScalar_H

// EOF
