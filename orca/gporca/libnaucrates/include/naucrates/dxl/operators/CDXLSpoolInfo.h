//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 Greenplum, Inc.
//
//	@filename:
//		CDXLSpoolInfo.h
//
//	@doc:
//		Class for representing spooling info in shared scan nodes, and nodes that
//		allow sharing (currently Materialize and Sort).
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------



#ifndef GPDXL_CDXLSpoolInfo_H
#define GPDXL_CDXLSpoolInfo_H

#include "gpos/base.h"
#include "gpos/string/CWStringConst.h"

#include "naucrates/dxl/gpdb_types.h"

namespace gpdxl
{
	
	// fwd decl 
	class CXMLSerializer;
	
	enum Edxlspooltype
	{
		EdxlspoolNone,
		EdxlspoolMaterialize,
		EdxlspoolSort,
		EdxlspoolSentinel
	};
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CDXLSpoolInfo
	//
	//	@doc:
	//		Class for representing spooling info in shared scan nodes, and nodes that
	//		allow sharing (currently Materialize and Sort).
	//
	//---------------------------------------------------------------------------
	class CDXLSpoolInfo
	{
		private:
		
			// id of the spooling operator
			ULONG m_ulSpoolId;

			// type of the underlying spool
			Edxlspooltype m_edxlsptype;
			
			// is the spool shared across multiple slices
			BOOL m_fMultiSlice;
			
			// slice executing the underlying sort or materialize
			INT m_iExecutorSlice;
						
			// private copy ctor
			CDXLSpoolInfo(CDXLSpoolInfo&);
			
			// spool type name
			const CWStringConst *PstrSpoolType() const;
			
		public:
			// ctor/dtor
			CDXLSpoolInfo
				(
				ULONG ulSpoolId,
				Edxlspooltype edxlspstype,
				BOOL fMultiSlice,
				INT iExecutorSlice
				);
			
			// accessors
			
			// spool id
			ULONG UlSpoolId() const;
			
			// spool type (sort or materialize)
			Edxlspooltype Edxlsptype() const;
			
			// is spool shared across multiple slices
			BOOL FMultiSlice() const;
			
			// id of slice executing the underlying operation
			INT IExecutorSlice() const;
			
			// serialize operator in DXL format
			void SerializeToDXL(CXMLSerializer *) const;
						
	};
}


#endif // !GPDXL_CDXLSpoolInfo_H

// EOF
