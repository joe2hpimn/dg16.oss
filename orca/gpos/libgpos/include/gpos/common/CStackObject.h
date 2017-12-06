//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename: 
//		CStackObject.h
//
//	@doc:
//		Class of all objects that must reside on the stack;
//		e.g., auto objects;
//
//	@owner: 
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CStackObject_H
#define GPOS_CStackObject_H

#include "gpos/types.h"

namespace gpos
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CStackObject
	//
	//	@doc:
	//		Constructor tests stack layout to ensure object is allocated on stack;
	//		constructor is protected to prevent direct instantiation of class;
	//
	//---------------------------------------------------------------------------
	class CStackObject
	{
		protected:

			CStackObject();
	};
}

#endif // !GPOS_CStackObject_H

// EOF

