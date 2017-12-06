//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2010 Greenplum, Inc.
//
//	@filename:
//		CWStringDynamic.h
//
//	@doc:
//		Wide character string class with dynamic buffer allocation.
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CWStringDynamic_H
#define GPOS_CWStringDynamic_H

#include "gpos/memory/CMemoryPoolManager.h"
#include "gpos/string/CWString.h"

#define GPOS_WSTR_DYNAMIC_CAPACITY_INIT	(1 << 7)
#define GPOS_WSTR_DYNAMIC_STATIC_BUFFER	(1 << 10)

namespace gpos
{
	//---------------------------------------------------------------------------
	//	@class:
	//		CWStringDynamic
	//
	//	@doc:
	//		Implementation of the string interface with dynamic buffer allocation.
	//		This CWStringDynamic class dynamically allocates memory when constructing a new
	//		string, or when modifying a string. The memory is released at destruction time
	//		or when the string is reset.
	//
	//---------------------------------------------------------------------------
	class CWStringDynamic : public CWString
	{
		private:

			// string memory pool used for allocating new memory for the string
			IMemoryPool *m_pmp;

			// string capacity
			ULONG m_ulCapacity;

			// increase string capacity
			void IncreaseCapacity(ULONG ulRequested);

			// find capacity that fits requested string size
			static
			ULONG UlCapacity(ULONG ulRequested);

			// private copy ctor
			CWStringDynamic(const CWStringDynamic&);

		protected:

			// appends the contents of a buffer to the current string
			void AppendBuffer(const WCHAR *wszBuf);

		public:

			// ctor
			CWStringDynamic(IMemoryPool *pmp);

			// ctor - copies passed string
			CWStringDynamic(IMemoryPool *pmp, const WCHAR *wszBuf);

			// appends a string and replaces character with string
			void AppendEscape(const CWStringBase *pstr, WCHAR wc, const WCHAR *wszReplace);

			// appends a formatted string
			void AppendFormat(const WCHAR *wszFormat, ...);

			// appends a null terminated character array
			virtual
			void AppendCharArray(const CHAR *sz);

			// appends a null terminated wide character array
			virtual
			void AppendWideCharArray(const WCHAR *wsz);

			// dtor
			virtual ~CWStringDynamic();

			// resets string
			void Reset();
	};
}

#endif // !GPOS_CWStringDynamic_H

// EOF

