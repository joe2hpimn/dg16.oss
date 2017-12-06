//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		IDatumGeneric.h
//
//	@doc:
//		Base abstract class for generic datum representation
//
//	@owner:
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPNAUCRATES_IDatumGeneric_H
#define GPNAUCRATES_IDatumGeneric_H

#include "gpos/base.h"

#include "naucrates/base/IDatum.h"
#include "gpos/common/CDouble.h"
#include "naucrates/base/IDatumStatisticsMappable.h"

namespace gpnaucrates
{

	//---------------------------------------------------------------------------
	//	@class:
	//		IDatumGeneric
	//
	//	@doc:
	//		Base abstract class for generic datum representation
	//
	//---------------------------------------------------------------------------
	class IDatumGeneric : public IDatumStatisticsMappable
	{

		private:

			// private copy ctor
			IDatumGeneric(const IDatumGeneric &);

		public:

			// ctor
			IDatumGeneric()
			{};

			// dtor
			virtual
			~IDatumGeneric()
			{};

			// accessor for datum type
			virtual IMDType::ETypeInfo Eti()
			{
				return IMDType::EtiGeneric;
			}

	}; // class IDatumGeneric

}


#endif // !GPNAUCRATES_IDatumGeneric_H

// EOF
