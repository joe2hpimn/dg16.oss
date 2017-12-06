//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2010 Greenplum, Inc.
//
//	@filename:
//		CStringStatic.h
//
//	@doc:
//		ASCII-character String class with buffer
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CStringStatic_H
#define GPOS_CStringStatic_H

#include "gpos/base.h"
#include "gpos/common/clibwrapper.h"

#define GPOS_SZ_LENGTH(x) gpos::clib::UlStrLen(x)

// use this character to substitute non-ASCII wide characters
#define GPOS_WCHAR_UNPRINTABLE	'.'

// end-of-string character
#define CHAR_EOS '\0'

namespace gpos
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CStringStatic
	//
	//	@doc:
	//		ASCII-character string interface with buffer pre-allocation.
	//		Internally, the class uses a null-terminated CHAR buffer to store the string
	//		characters.	The buffer is assigned at construction time; its capacity cannot be
	//		modified, thus restricting the maximum size of the stored string. Attempting to
	//		store a larger string than the available buffer capacity results in truncation.
	//		CStringStatic owner is responsible for allocating the buffer and releasing it
	//		after the object is destroyed.
	//
	//---------------------------------------------------------------------------
	class CStringStatic
	{
		private:

			// null-terminated wide character buffer
			CHAR *m_szBuf;

			// size of the string in number of CHAR units,
			// not counting the terminating '\0'
			ULONG m_ulLength;

			// buffer capacity
			ULONG m_ulCapacity;

#ifdef GPOS_DEBUG
			// checks whether a string is properly null-terminated
			bool FValid() const;
#endif // GPOS_DEBUG

			// private copy ctor
			CStringStatic(const CStringStatic&);

		public:

			// ctor
			CStringStatic(CHAR szBuffer[], ULONG ulCapacity);

			// ctor with string initialization
			CStringStatic(CHAR szBuffer[], ULONG ulCapacity, const CHAR szInit[]);

			// dtor - owner is responsible for releasing the buffer
			~CStringStatic()
			{}

			// returns the wide character buffer storing the string
			const CHAR* Sz() const
			{
				return m_szBuf;
			}

			// returns the string length
			ULONG UlLength() const
			{
				return m_ulLength;
			}

			// checks whether the string contains any characters
			BOOL FEmpty() const
			{
				return (0 == m_ulLength);
			}

			// checks whether the string is byte-wise equal to a given string literal
			BOOL FEquals(const CHAR *szBuf) const;

			// appends a string
			void Append(const CStringStatic *pstr);

			// appends the contents of a buffer to the current string
			void AppendBuffer(const CHAR *szBuf);

			// appends a formatted string
			void AppendFormat(const CHAR *szFormat, ...);

			// appends a formatted string based on passed va list
			void AppendFormatVA(const CHAR *szFormat, VA_LIST vaArgs);

			// appends wide character string
			void AppendConvert(const WCHAR *wsz);

			// resets string
			void Reset();

	};
}

#endif // !GPOS_CStringStatic_H

// EOF

