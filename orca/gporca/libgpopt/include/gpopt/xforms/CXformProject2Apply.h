//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CXformProject2Apply.h
//
//	@doc:
//		Transform Project to Apply
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformProject2Apply_H
#define GPOPT_CXformProject2Apply_H

#include "gpos/base.h"
#include "gpopt/xforms/CXformSubqueryUnnest.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CXformProject2Apply
	//
	//	@doc:
	//		Transform Project to Apply; this transformation is only applicable
	//		to a Project expression with subqueries in its scalar project list
	//
	//---------------------------------------------------------------------------
	class CXformProject2Apply : public CXformSubqueryUnnest
	{

		private:

			// private copy ctor
			CXformProject2Apply(const CXformProject2Apply &);

		public:

			// ctor
			explicit
			CXformProject2Apply(IMemoryPool *pmp);

			// dtor
			virtual
			~CXformProject2Apply()
			{}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfProject2Apply;
			}

			// return a string for xform name
			virtual
			const CHAR *SzId() const
			{
				return "CXformProject2Apply";
			}

			// is transformation a subquery unnesting (Subquery To Apply) xform?
			virtual
			BOOL FSubqueryUnnesting() const
			{
				return true;
			}

	}; // class CXformProject2Apply

}

#endif // !GPOPT_CXformProject2Apply_H

// EOF
