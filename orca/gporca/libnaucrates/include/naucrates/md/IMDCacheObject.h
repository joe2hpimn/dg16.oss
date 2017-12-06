//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 Greenplum, Inc.
//
//	@filename:
//		IMDCacheObject.h
//
//	@doc:
//		Base interface for metadata cache objects
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------



#ifndef GPMD_IMDCacheObject_H
#define GPMD_IMDCacheObject_H

#include "gpos/base.h"

#include "naucrates/md/IMDId.h"
#include "naucrates/md/CMDName.h"

#include "naucrates/md/IMDInterface.h"


// fwd decl
namespace gpos
{
	class CWStringDynamic;
}
namespace gpdxl
{
	class CXMLSerializer;
}
	
namespace gpmd
{
	using namespace gpos;

	//---------------------------------------------------------------------------
	//	@class:
	//		IMDCacheObject
	//
	//	@doc:
	//		Base interface for metadata cache objects
	//
	//---------------------------------------------------------------------------
	class IMDCacheObject : public IMDInterface
	{	
		protected:
			// Serialize a list of metadata id elements using pstrTokenList
			// as the root XML element for the list, and each metadata id is
			// serialized in the form of a pstrTokenListItem element.
			// The serialized information looks like this:
			// <pstrTokenList>
			//		<pstrTokenListItem .../>...
			// </pstrTokenList>
			static
			void SerializeMDIdList
				(
				CXMLSerializer *pxmlser,
				const DrgPmdid *pdrgpmdid,
				const CWStringConst *pstrTokenList,
				const CWStringConst *pstrTokenListItem
				);
		
		public:
			// type of md object
			enum Emdtype
			{
				EmdtRel,
				EmdtInd,
				EmdtFunc,
				EmdtAgg,
				EmdtOp,
				EmdtType,
				EmdtTrigger,
				EmdtCheckConstraint,
				EmdtRelStats,
				EmdtColStats,
				EmdtCastFunc,
				EmdtScCmp
			};
			
			// md id of cache object
			virtual 
			IMDId *Pmdid() const = 0;

			// cache object name
			virtual 
			CMDName Mdname() const = 0;

			// object type
			virtual
			Emdtype Emdt() const = 0;
			
			// serialize object in DXL format
			virtual 
			void Serialize(gpdxl::CXMLSerializer *) const = 0;

			// DXL string representation of cache object 
			virtual 
			const CWStringDynamic *Pstr() const = 0;
			
						
			// serialize the metadata id information as the attributes of an 
			// element with the given name
			virtual 
			void SerializeMDIdAsElem
				(
				gpdxl::CXMLSerializer *pxmlser, 
				const CWStringConst *pstrElem, 
				const IMDId *pmdid
				) const;
			
#ifdef GPOS_DEBUG
			virtual void DebugPrint(IOstream &os) const = 0;
#endif
	};
	
	typedef CDynamicPtrArray<IMDCacheObject, CleanupRelease> DrgPimdobj;

}



#endif // !GPMD_IMDCacheObject_H

// EOF
