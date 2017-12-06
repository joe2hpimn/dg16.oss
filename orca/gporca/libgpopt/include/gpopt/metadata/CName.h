//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2009 Greenplum, Inc.
//
//	@filename:
//		CName.h
//
//	@doc:
//		Name abstraction for metadata names to keep optimizer
//		agnostic of encodings etc.
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOPT_CName_H
#define GPOPT_CName_H

#include "gpos/base.h"
#include "gpos/string/CWStringConst.h"

#define GPOPT_NAME_QUOTE_BEGIN	"\""
#define GPOPT_NAME_QUOTE_END	"\""

#define GPOPT_NAME_SEPARATOR	GPOS_WSZ_LIT(".")

namespace gpopt
{
	using namespace gpos;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CName
	//
	//	@doc:
	//		Names consist of a null terminated wide character string; 
	//		No assumptions about format and encoding; only semantics 
	//		enforced is zero termination of string;
	//
	//---------------------------------------------------------------------------
	class CName
	{
		private:
			
			// actual name
			const CWStringConst *m_pstrName;

			// keep track of copy status
			BOOL m_fDeepCopy;
			
			// deep copy function
			void DeepCopy(IMemoryPool *pmp, const CWStringConst *pstr);
			
		public:
		
			// ctors
			CName(IMemoryPool *, const CWStringBase *);
			CName(const CWStringConst *, BOOL fOwnsMemory = false);
			CName(const CName &);

			CName(IMemoryPool *pmp, const CName &);
			CName(IMemoryPool *pmp, const CName &, const CName &);

			// dtor
			~CName();

			// accessors
			const CWStringConst *Pstr() const
			{
				return m_pstrName;
			}
			
			ULONG UlLength() const
			{
				return m_pstrName->UlLength();
			}

#ifdef GPOS_DEBUG
			// comparison
			BOOL FEquals(const CName &) const;
#endif // GPOS_DEBUG
			
			// debug print
			IOstream &OsPrint(IOstream &) const;
			
	}; // class CName
}

#endif // !GPOPT_CName_H

// EOF
