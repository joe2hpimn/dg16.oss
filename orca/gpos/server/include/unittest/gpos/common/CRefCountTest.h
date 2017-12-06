//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2008 Greenplum, Inc.
//
//	@filename:
//		CRefCount.h
//
//	@doc:
//      Test for CRefCount
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CRefCountTest_H
#define GPOS_CRefCountTest_H

#include "gpos/base.h"

namespace gpos
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CRefCountTest
	//
	//	@doc:
	//		Static unit tests
	//
	//---------------------------------------------------------------------------
	class CRefCountTest
	{
		private:

		//---------------------------------------------------------------------------
		//	@class:
		//		CDeletableTest
		//
		//	@doc:
		//		Local class for testing deletable/undeletable objects
		//
		//---------------------------------------------------------------------------
		class CDeletableTest : public CRefCount
		{

			private:

				// is calling object's destructor allowed?
				BOOL m_fDeletable;

			public:

				// ctor
				CDeletableTest()
					:
					m_fDeletable(false) // by default, object is not deletable
				{}

				// return true if calling object's destructor is allowed
				virtual
				BOOL FDeletable() const
				{
					return m_fDeletable;
				}

				// allow calling object's destructor
				void AllowDeletion()
				{
					m_fDeletable = true;
				}

		}; // class CDeletableTest


		public:

			// unittests
			static GPOS_RESULT EresUnittest();
			static GPOS_RESULT EresUnittest_CountUpAndDown();
			static GPOS_RESULT EresUnittest_DeletableObjects();

#ifdef GPOS_DEBUG
			static GPOS_RESULT EresUnittest_Stack();
			static GPOS_RESULT EresUnittest_Check();
#endif // GPOS_DEBUG
	};
}

#endif // !GPOS_CRefCountTest_H

// EOF

