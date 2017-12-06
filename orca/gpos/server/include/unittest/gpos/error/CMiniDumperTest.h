//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CMiniDumperTest.h
//
//	@doc:
//		Test for minidump handler
//
//	@owner:
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CMiniDumperTest_H
#define GPOS_CMiniDumperTest_H

#include "gpos/base.h"
#include "gpos/error/CMiniDumper.h"
#include "gpos/error/CSerializable.h"

#define GPOS_MINIDUMP_BUF_SIZE (1024 * 8)

namespace gpos
{

	//---------------------------------------------------------------------------
	//	@class:
	//		CMiniDumperTest
	//
	//	@doc:
	//		Static unit tests for minidump handler
	//
	//---------------------------------------------------------------------------
	class CMiniDumperTest
	{
		private:

			//---------------------------------------------------------------------------
			//	@class:
			//		CMiniDumperStream
			//
			//	@doc:
			//		Local minidump handler
			//
			//---------------------------------------------------------------------------
			class CMiniDumperStream : public CMiniDumper
			{
				public:

					// ctor
					CMiniDumperStream(IMemoryPool *pmp);

					// dtor
					virtual
					~CMiniDumperStream();

					// serialize minidump header
					virtual
					void SerializeHeader();

					// serialize minidump footer
					virtual
					void SerializeFooter();

					// size to reserve for entry header
					virtual
					ULONG_PTR UlpRequiredSpaceEntryHeader();

					// size to reserve for entry footer
					virtual
					ULONG_PTR UlpRequiredSpaceEntryFooter();

					// serialize entry header
					virtual
					ULONG_PTR UlpSerializeEntryHeader(WCHAR *wszEntry, ULONG_PTR ulpAllocSize);

					// serialize entry footer
					virtual
					ULONG_PTR UlpSerializeEntryFooter(WCHAR *wszEntry, ULONG_PTR ulpAllocSize);

			}; // class CMiniDumperStream

			//---------------------------------------------------------------------------
			//	@class:
			//		CSerializableStack
			//
			//	@doc:
			//		Stack serializer
			//
			//---------------------------------------------------------------------------
			class CSerializableStack : public CSerializable
			{
				private:

					// buffer used for creating the stack trace
					WCHAR m_wszBuffer[GPOS_MINIDUMP_BUF_SIZE];

				public:

					// ctor
					CSerializableStack();

					// dtor
					virtual
					~CSerializableStack();

					// calculate space needed for serialization
					virtual
					ULONG_PTR UlpRequiredSpace();

					// serialize object to passed buffer
					virtual
					ULONG_PTR UlpSerialize(WCHAR *wszBuffer, ULONG_PTR ulpAllocSize);

			}; // class CSerializableStack

			// helper functions
			static void *PvRaise(void *);
			static void *PvLoop(void *);
			static void *PvLoopSerialize(void *);

		public:

			// unittests
			static GPOS_RESULT EresUnittest();
			static GPOS_RESULT EresUnittest_Basic();
			static GPOS_RESULT EresUnittest_Concurrency();

	}; // class CMiniDumperTest
}

#endif // !GPOS_CMiniDumperTest_H

// EOF

