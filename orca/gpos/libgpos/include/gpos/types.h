//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2008 Greenplum, Inc.
//
//	@filename:
//		types.h
//
//	@doc:
//		Type definitions for gpos to avoid using native types directly;
//
//		TODO: 03/15/2008; the seletion of basic types which then
//		get mapped to internal types should be done by autoconf or other
//		external tools; for the time being these are hard-coded; cpl asserts
//		are used to make sure things are failing if compiled with a different
//		compiler.
//
//	@owner: 
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_types_H
#define GPOS_types_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/types.h>

#include <iostream>

#include "gpos/assert.h"

#define GPOS_SIZEOF(x)		((gpos::ULONG) sizeof(x))
#define GPOS_ARRAY_SIZE(x)	(GPOS_SIZEOF(x) / GPOS_SIZEOF(x[0]))
#define GPOS_OFFSET(T, M)	((gpos::ULONG) (SIZE_T)&(((T*)0x1)->M)-1)

/* wide character string literate */
#define GPOS_WSZ_LIT(x)		L##x

// failpoint simulation is enabled on debug build
#ifdef GPOS_DEBUG
#define GPOS_FPSIMULATOR 1
#endif // GPOS_DEBUG

namespace gpos
{

	// Basic types to be used instead of built-ins
	// Add types as needed;
	
	typedef unsigned char BYTE;
	typedef char CHAR;
	// ignore signed char for the moment

	// wide character type
	typedef wchar_t WCHAR;
		
	typedef bool BOOL;

	// numeric types
	
	typedef size_t SIZE_T;
	typedef ssize_t SSIZE_T;
	typedef mode_t MODE_T;

	// define ULONG,ULLONG as types which implement standard's
	// requirements for ULONG_MAX and ULLONG_MAX; eliminate standard's slack
	// by fixed sizes rather than min requirements

	typedef uint32_t ULONG;
	GPOS_CPL_ASSERT(4 == sizeof(ULONG));
	
#ifdef ULONG_MAX
#undef ULONG_MAX
#endif // ULONG_MAX
#define ULONG_MAX		((ULONG)-1)

	typedef uint64_t ULLONG;
	GPOS_CPL_ASSERT(8 == sizeof(ULLONG));
#ifdef ULLONG_MAX
#undef ULLONG_MAX
#endif // ULLONG_MAX
#define ULLONG_MAX		((ULLONG)-1)

	typedef uintptr_t	ULONG_PTR;
#ifdef GPOS_32BIT
#define ULONG_PTR_MAX	(ULONG_MAX)
#else
#define ULONG_PTR_MAX	(ULLONG_MAX)
#endif

	typedef uint16_t USINT;
	typedef int16_t SINT;
	typedef int32_t INT;
	typedef int64_t LINT;
	typedef intptr_t INT_PTR;

	GPOS_CPL_ASSERT(2 == sizeof(USINT));
	GPOS_CPL_ASSERT(2 == sizeof(SINT));
	GPOS_CPL_ASSERT(4 == sizeof(INT));
	GPOS_CPL_ASSERT(8 == sizeof(LINT));

#ifdef INT_MAX
#undef INT_MAX
#endif // INT_MAX
#define INT_MAX			((INT) (ULONG_MAX >> 1))

#ifdef INT_MIN
#undef INT_MIN
#endif // INT_MIN
#define INT_MIN			(-INT_MAX - 1)

#ifdef LINT_MAX
#undef LINT_MAX
#endif // LINT_MAX
#define LINT_MAX		((LINT) (ULLONG_MAX >> 1))

#ifdef LINT_MIN
#undef LINT_MIN
#endif // LINT_MIN
#define LINT_MIN		(-LINT_MAX - 1)

#ifdef USINT_MAX
#undef USINT_MAX
#endif // USINT_MAX
#define USINT_MAX		((USINT)-1)

#ifdef SINT_MAX
#undef SINT_MAX
#endif // SINT_MAX
#define SINT_MAX		((SINT) (USINT_MAX >> 1))

#ifdef SINT_MIN
#undef SINT_MIN
#endif // SINT_MIN
#define SINT_MIN		(-SINT_MAX - 1)

	typedef double DOUBLE;

	typedef void * VOID_PTR;

	// holds for all platforms
	GPOS_CPL_ASSERT(sizeof(ULONG_PTR) == sizeof(void*));

	// variadic parameter list type
	typedef va_list VA_LIST;

	// wide char ostream
 	typedef std::basic_ostream<WCHAR, std::char_traits<WCHAR> >  WOSTREAM;
 	typedef std::ios_base IOS_BASE;

 	// bad allocation exception
 	typedef std::bad_alloc BAD_ALLOC;

 	// no throw type
 	typedef std::nothrow_t NO_THROW;

	// enum for results on OS level (instead of using a global error variable)
	enum GPOS_RESULT
	{
		GPOS_OK = 0,
		
		GPOS_FAILED = 1,
		GPOS_OOM = 2,
		GPOS_NOT_FOUND = 3,
		GPOS_TIMEOUT = 4
	};


	// enum for locale encoding
	enum ELocale
	{
		ELocInvalid,	// invalid key for hashtable iteration
		ElocEnUS_Utf8,
		ElocGeDE_Utf8,
		
		ElocSentinel
	};

}

#endif // !GPOS_types_H

// EOF

