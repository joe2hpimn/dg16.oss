//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2014 Pivotal, Inc.
//
//	@filename:
//		IComparator.h
//
//	@doc:
//		Interface for comparing IDatum instances.
//
//	@owner:
//		
//
//	@test:
//
//---------------------------------------------------------------------------

#ifndef GPOPT_IComparator_H
#define GPOPT_IComparator_H

#include "gpos/base.h"

namespace gpnaucrates
{
	// fwd declarations
	class IDatum;
}

namespace gpopt
{
	using gpnaucrates::IDatum;

	//---------------------------------------------------------------------------
	//	@class:
	//		IComparator
	//
	//	@doc:
	//		Interface for comparing IDatum instances.
	//
	//---------------------------------------------------------------------------
	class IComparator
	{
		public:
			virtual
			~IComparator()
			{}

			// tests if the two arguments are equal
			virtual
			gpos::BOOL FEqual(const IDatum *pdatum1, const IDatum *pdatum2) const = 0;

			// tests if the first argument is less than the second
			virtual
			gpos::BOOL FLessThan(const IDatum *pdatum1, const IDatum *pdatum2) const = 0;

			// tests if the first argument is less or equal to the second
			virtual
			gpos::BOOL FLessThanOrEqual(const IDatum *pdatum1, const IDatum *pdatum2) const = 0;

			// tests if the first argument is greater than the second
			virtual
			gpos::BOOL FGreaterThan(const IDatum *pdatum1, const IDatum *pdatum2) const = 0;

			// tests if the first argument is greater or equal to the second
			virtual
			gpos::BOOL FGreaterThanOrEqual(const IDatum *pdatum1, const IDatum *pdatum2) const = 0;
	};
}

#endif // !GPOPT_IComparator_H

// EOF
