//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2012 EMC Corp.
//
//	@filename:
//		CTraceFlagIter.h
//
//	@doc:
//		Trace flag iterator
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CTraceFlagIter_H
#define GPOS_CTraceFlagIter_H

#include "gpos/base.h"
#include "gpos/common/CBitSetIter.h"


namespace gpos
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CTraceFlagIter
	//
	//	@doc:
	//		Trace flag iterator for the currently executing task
	//
	//---------------------------------------------------------------------------
	class CTraceFlagIter : public CBitSetIter
	{
		private:

			// no copy ctor
			CTraceFlagIter(const CTraceFlagIter&);

		public:

			// ctor
			CTraceFlagIter()
				:
				CBitSetIter(*CTask::PtskSelf()->Ptskctxt()->m_pbs)
			{}

			// dtor
			virtual
			~CTraceFlagIter ()
			{}

	}; // class CTraceFlagIter

}


#endif // !GPOS_CTraceFlagIter_H

// EOF

