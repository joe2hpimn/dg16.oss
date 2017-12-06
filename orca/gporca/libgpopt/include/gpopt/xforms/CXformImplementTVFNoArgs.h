//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CXformImplementTVFNoArgs.h
//
//	@doc:
//		Implement logical TVF with a physical TVF with no arguments
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CXformImplementTVFNoArgs_H
#define GPOPT_CXformImplementTVFNoArgs_H

#include "gpos/base.h"
#include "gpopt/xforms/CXformImplementTVF.h"

namespace gpopt
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		CXformImplementTVFNoArgs
	//
	//	@doc:
	//		Implement TVF with no arguments
	//
	//---------------------------------------------------------------------------
	class CXformImplementTVFNoArgs : public CXformImplementTVF
	{

		private:

			// private copy ctor
			CXformImplementTVFNoArgs(const CXformImplementTVFNoArgs &);

		public:

			// ctor
			explicit
			CXformImplementTVFNoArgs(IMemoryPool *pmp);

			// dtor
			virtual
			~CXformImplementTVFNoArgs() {}

			// ident accessors
			virtual
			EXformId Exfid() const
			{
				return ExfImplementTVFNoArgs;
			}

			// return a string for xform name
			virtual
			const CHAR *SzId() const
			{
				return "CXformImplementTVFNoArgs";
			}

	}; // class CXformImplementTVFNoArgs

}

#endif // !GPOPT_CXformImplementTVFNoArgs_H

// EOF
