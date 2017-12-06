//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2010 Greenplum, Inc.
//
//	@filename:
//		CWString.h
//
//	@doc:
//		Wide character string interface.
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CWString_H
#define GPOS_CWString_H

#include "gpos/string/CWStringBase.h"

#define GPOS_MAX_FMT_STR_LENGTH (10*1024*1024) // 10MB

namespace gpos
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CWString
	//
	//	@doc:
	//		Wide character String interface.
	//		Internally, the class uses a null-terminated WCHAR buffer to store the string characters.
	//		The class API provides functions for accessing the wide-character buffer and its length,
	//		as well as functions that modify the current string by appending another string to it,
	//		or that construct a string according to a given format.
	//		For constant strings consider using the CWStringConst class.
	//
	//---------------------------------------------------------------------------
	class CWString : public CWStringBase
	{
		protected:

			// null-terminated wide character buffer
			WCHAR *m_wszBuf;

			// appends the contents of a buffer to the current string
			virtual void AppendBuffer(const WCHAR *wszBuf) = 0;
			
		public:

			// ctor
			CWString(ULONG ulLength);

			// dtor
			virtual ~CWString()
			{}
					
			// returns the wide character buffer storing the string
			const WCHAR* Wsz() const;
			
			// appends a string
			void Append(const CWStringBase *pstr);

			// appends a formatted string
			virtual
			void AppendFormat(const WCHAR *wszFormat, ...) = 0;

			// appends a string and replaces character with string
			virtual
			void AppendEscape(const CWStringBase *pstr, WCHAR wc, const WCHAR *wszReplace) = 0;

			// appends a null terminated character array
			virtual
			void AppendCharArray(const CHAR *sz) = 0;

			// appends a null terminated wide character array
			virtual
			void AppendWideCharArray(const WCHAR *wsz) = 0;

			// resets string
			virtual void Reset() = 0;
	};
}

#endif // !GPOS_CWString_H

// EOF

