//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CFunctionProp.h
//
//	@doc:
//		Representation of function properties
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CFunctionProp_H
#define GPOPT_CFunctionProp_H

#include "gpos/base.h"
#include "gpos/common/CRefCount.h"

#include "naucrates/md/IMDFunction.h"

namespace gpopt
{
	using namespace gpos;
	using namespace gpmd;

	//---------------------------------------------------------------------------
	//	@class:
	//		CFunctionProp
	//
	//	@doc:
	//		Representation of function properties
	//
	//---------------------------------------------------------------------------
	class CFunctionProp : public CRefCount
	{
		private:

			// function stability
			IMDFunction::EFuncStbl m_efs;

			// function data access
			IMDFunction::EFuncDataAcc m_efda;

			// does this expression have a volatile Function Scan
			BOOL m_fHasVolatileFunctionScan;

			// is this function used as a scan operator
			BOOL m_fScan;

			// hidden copy ctor
			CFunctionProp(const CFunctionProp&);

		public:

			// ctor
			CFunctionProp
				(
				IMDFunction::EFuncStbl efsStability,
				IMDFunction::EFuncDataAcc efdaDataAccess,
				BOOL fHasVolatileFunctionScan,
				BOOL fScan
				);

			// dtor
			virtual
			~CFunctionProp();

			// function stability
			IMDFunction::EFuncStbl Efs() const
			{
				return m_efs;
			}

			// function data access
			virtual
			IMDFunction::EFuncDataAcc Efda() const
			{
				return m_efda;
			}

			// does this expression have a volatile Function Scan
			virtual
			BOOL FHasVolatileFunctionScan() const
			{
				return m_fHasVolatileFunctionScan;
			}

			// check if must execute on the master
			BOOL FMasterOnly() const;

			// print
			IOstream &OsPrint(IOstream &os) const;

	}; // class CFunctionProp

 	// shorthand for printing
	inline
	IOstream &operator << (IOstream &os, CFunctionProp &fp)
	{
		return fp.OsPrint(os);
	}
}

#endif // !GPOPT_CFunctionProp_H

// EOF
