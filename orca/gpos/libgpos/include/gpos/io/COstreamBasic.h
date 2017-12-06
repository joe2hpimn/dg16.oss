//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2008 - 2010 Greenplum, Inc.
//
//	@filename:
//		COstreamBasic.h
//
//	@doc:
//		Output stream base class;
//
//	@owner: 
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_COstreamBasic_H
#define GPOS_COstreamBasic_H

#include "gpos/io/COstream.h"

namespace gpos
{
	//---------------------------------------------------------------------------
	//	@class:
	//		COstreamBasic
	//
	//	@doc:
	//		Implements a basic write thru interface over a std::WOSTREAM
	//
	//---------------------------------------------------------------------------

	class COstreamBasic : public COstream
	{
		private:
			
			// underlying stream
			WOSTREAM *m_pos;

			// private copy ctor
			COstreamBasic(const COstreamBasic &);
			
		public:

			// please see comments in COstream.h for an explanation
			using COstream::operator <<;
			
			// ctor
			explicit
			COstreamBasic(WOSTREAM *os);

			virtual ~COstreamBasic() {}
						
			// implement << operator				
			virtual IOstream& operator<< (const WCHAR *);
	};

}

#endif // !GPOS_COstreamBasic_H

// EOF

