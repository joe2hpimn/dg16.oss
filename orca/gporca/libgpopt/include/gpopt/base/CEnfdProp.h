//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CEnfdProp.h
//
//	@doc:
//		Base class for all enforceable properties
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CEnfdProp_H
#define GPOPT_CEnfdProp_H

#include "gpos/base.h"
#include "gpos/common/CDynamicPtrArray.h"
#include "gpos/common/CRefCount.h"

#include "gpopt/base/CPropSpec.h"
#include "gpopt/operators/COperator.h"


namespace gpopt
{
	using namespace gpos;

	// prototypes
	class CPhysical;
	class CReqdPropPlan;

	//---------------------------------------------------------------------------
	//	@class:
	//		CEnfdProp
	//
	//	@doc:
	//		Abstract base class for all enforceable properties.
	//
	//---------------------------------------------------------------------------
	class CEnfdProp : public CRefCount
	{

		public:

			// definition of property enforcing type for a given operator
			//
			// - Required: operator cannot deliver the required properties on its
			// own, e.g., requiring a sort order from a table scan
			//
			// - Optional: operator can request the required properties from its children
			// and preserve them, e.g., requiring a sort order from a filter
			//
			// - Prohibited: operator prohibits enforcing the required properties on its
			// output, e.g., requiring a sort order on column A from a sort operator that
			// provides sorting on column B
			//
			// - Unnecessary: operator already establishes the required properties on its
			// own, e.g., requiring a sort order on column A from a sort operator that
			// provides sorting on column A. If the required property spec is empty, any
			// operator satisfies it so its type falls into this category.

			enum EPropEnforcingType
			{
				EpetRequired,
				EpetOptional,
				EpetProhibited,
				EpetUnnecessary,

				EpetSentinel
			};

		private:

			// private copy ctor
			CEnfdProp(const CEnfdProp &);

		public:

			// ctor
			CEnfdProp()
			{}

			// dtor
			virtual
			~CEnfdProp()
			{}

			// append enforcers to dynamic array for the given plan properties
			void AppendEnforcers
				(
				IMemoryPool *pmp,
				CReqdPropPlan *prpp,
				DrgPexpr *pdrgpexpr,			// array of enforcer expressions
				CExpression *pexprChild,	// leaf in the target group where enforcers will be added
				CEnfdProp::EPropEnforcingType epet,
				CExpressionHandle &exprhdl
				)
			{
				if (FEnforce(epet))
				{
					Pps()->AppendEnforcers(pmp, exprhdl, prpp, pdrgpexpr, pexprChild);
				}
			}

			// property spec accessor
			virtual
			CPropSpec *Pps() const = 0;

			// hash function
			virtual
			ULONG UlHash() const = 0;

			// print function
			virtual
			IOstream &OsPrint(IOstream &os) const = 0;

			// check if operator requires an enforcer under given enforceable property
			// based on the derived enforcing type
			static
			BOOL FEnforce(EPropEnforcingType epet)
			{
				return CEnfdProp::EpetOptional == epet || CEnfdProp::EpetRequired == epet;
			}

			// check if operator requires optimization under given enforceable property
			// based on the derived enforcing type
			static
			BOOL FOptimize(EPropEnforcingType epet)
			{
				return CEnfdProp::EpetOptional == epet || CEnfdProp::EpetUnnecessary == epet;
			}

	}; // class CEnfdProp


	// shorthand for printing
	IOstream &operator << (IOstream &os, CEnfdProp &efdprop);

}


#endif // !GPOPT_CEnfdProp_H

// EOF
