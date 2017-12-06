//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CBitSetIter.h
//
//	@doc:
//		Implementation of iterator for bitset
//
//	@owner: 
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CBitSetIter_H
#define GPOS_CBitSetIter_H

#include "gpos/base.h"
#include "gpos/common/CBitSet.h"

namespace gpos
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CBitSetIter
	//
	//	@doc:
	//		Iterator for bitset's; defined as friend, ie can access bitset's 
	//		internal links
	//
	//---------------------------------------------------------------------------
	class CBitSetIter
	{
		private:

			// bitset
			const CBitSet &m_bs;

			// current cursor position (in current link)
			ULONG m_ulCursor;
				
			// current cursor link
			CBitSet::CBitSetLink *m_pbsl;
		
			// is iterator active or exhausted
			BOOL m_fActive;
						
			// private copy ctor
			CBitSetIter(const CBitSetIter&);
			
		public:
				
			// ctor
			explicit
			CBitSetIter(const CBitSet &bs);
			// dtor
			~CBitSetIter() {}

			// short hand for cast
			operator BOOL () const
			{
				return m_fActive;
			}
			
			// move to next bit
			BOOL FAdvance();
			
			// current bit
			ULONG UlBit() const;

	}; // class CBitSetIter
}


#endif // !GPOS_CBitSetIter_H

// EOF

