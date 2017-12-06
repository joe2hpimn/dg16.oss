//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2010 Greenplum, Inc.
//
//	@filename:
//		CWStringConst.h
//
//	@doc:
//		Constant string class
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CWStringConst_H
#define GPOS_CWStringConst_H

#include "gpos/string/CWStringBase.h"

namespace gpos
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CWStringConst
	//
	//	@doc:
	//		Constant string class.
	//		The class represents constant strings, which cannot be modified after creation.
	//		The class can either own its own memory, or be supplied with an external
	//		memory buffer holding the string characters.
	//		For a general string class that can be modified, see CWString.
	//
	//---------------------------------------------------------------------------
	class CWStringConst : public CWStringBase
	{
		private:
			// null terminated wide character buffer
			const WCHAR *m_wszBuf;
			
		public:
			// ctors
			CWStringConst(const WCHAR *wszBuf);
			CWStringConst(IMemoryPool *pmp, const WCHAR *wszBuf);
			
			// shallow copy ctor
			CWStringConst(const CWStringConst&);
			
			//dtor
			~CWStringConst();
			
			// returns the wide character buffer storing the string
			const WCHAR* Wsz() const;
	};
}

#endif // #ifndef GPOS_CWStringConst_H

// EOF

