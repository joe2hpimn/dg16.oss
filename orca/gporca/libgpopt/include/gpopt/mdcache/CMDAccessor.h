//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2011 EMC Corp.
//
//	@filename:
//		CMDAccessor.h
//
//	@doc:
//		Metadata cache accessor.
//
//	@owner: 
//		
//
//	@test:
//
//
//---------------------------------------------------------------------------



#ifndef GPOPT_CMDAccessor_H
#define GPOPT_CMDAccessor_H

#include "gpos/base.h"
#include "gpos/string/CWStringBase.h"
#include "gpos/memory/CCache.h"
#include "gpos/memory/CCacheAccessor.h"

#include "gpopt/spinlock.h"
#include "gpopt/mdcache/CMDKey.h"
#include "gpopt/engine/CStatisticsConfig.h"

#include "naucrates/md/IMDId.h"
#include "naucrates/md/IMDProvider.h"
#include "naucrates/md/IMDType.h"
#include "naucrates/md/IMDFunction.h"
#include "naucrates/md/CSystemId.h"
#include "naucrates/statistics/IStatistics.h"

// fwd declarations
namespace gpdxl
{
	class CDXLDatum;
}

namespace gpmd
{
	class IMDCacheObject;
	class IMDRelation;
	class IMDRelationExternal;
	class IMDScalarOp;
	class IMDAggregate;
	class IMDTrigger;
	class IMDIndex;
	class IMDCheckConstraint;
	class IMDProvider;
	class CMDProviderGeneric;
	class IMDColStats;
	class IMDRelStats;
	class CDXLBucket;
	class IMDCast;
	class IMDScCmp;
}

namespace gpnaucrates
{
	class CHistogram;
	class CBucket;
	class IStatistics;
}

namespace gpopt
{
	using namespace gpos;
	using namespace gpmd;

	
	typedef IMDId * MdidPtr;
	
	//---------------------------------------------------------------------------
	//	@class:
	//		CMDAccessor
	//
	//	@doc:
	//		Metadata cache accessor serving requests for metadata cache objects
	//		in an optimization session
	//
	//---------------------------------------------------------------------------
	class CMDAccessor
	{
		public:
			// ccache template for mdcache
			typedef CCache<IMDCacheObject*, CMDKey*> MDCache;

		private:
		// element in the hashtable of cache accessors maintained by the MD accessor
		struct SMDAccessorElem;
		struct SMDProviderElem;
		

		// cache accessor for objects in a MD cache
		typedef CCacheAccessor<IMDCacheObject*, CMDKey*> CacheAccessorMD;
		
		// hashtable for cache accessors indexed by the md id of the accessed object 
		typedef CSyncHashtable<SMDAccessorElem, MdidPtr, CSpinlockMDAcc> MDHT;
		
		typedef CSyncHashtableAccessByKey<SMDAccessorElem, MdidPtr, CSpinlockMDAcc>
			MDHTAccessor;
		
		// iterator for the cache accessors hashtable
		typedef CSyncHashtableIter<SMDAccessorElem, MdidPtr, CSpinlockMDAcc> MDHTIter;		
		typedef CSyncHashtableAccessByIter<SMDAccessorElem, MdidPtr, CSpinlockMDAcc>
				MDHTIterAccessor;
				
		// hashtable for MD providers indexed by the source system id 
		typedef CSyncHashtable<SMDProviderElem, SMDProviderElem, CSpinlockMDAccMDP> MDPHT;
		
		typedef CSyncHashtableAccessByKey<SMDProviderElem, SMDProviderElem, CSpinlockMDAccMDP>
			MDPHTAccessor;
		
		// iterator for the providers hashtable
		typedef CSyncHashtableIter<SMDProviderElem, SMDProviderElem, CSpinlockMDAccMDP> MDPHTIter;		
		typedef CSyncHashtableAccessByIter<SMDProviderElem, SMDProviderElem, CSpinlockMDAccMDP>
				MDPHTIterAccessor;
				
		// element in the cache accessor hashtable maintained by the MD Accessor
		struct SMDAccessorElem
		{
			private:
				// hashed object
				IMDCacheObject *m_pimdobj;
	
			public:
				// hash key
				IMDId *m_pmdid;
				
				// generic link
				SLink m_link;
		
				// invalid key
				static const MdidPtr m_pmdidInvalid;
		
				// ctor
				SMDAccessorElem(IMDCacheObject *pimdobj, IMDId *pmdid);
				
				// dtor
				~SMDAccessorElem();
		
				// hashed object
				IMDCacheObject *Pimdobj()
				{
					return m_pimdobj;
				}

				// return the key for this hashtable element
				IMDId *Pmdid();

				// equality function for hash tables
				static
				BOOL FEqual(const MdidPtr &pmdidLeft, const MdidPtr &pmdidRight);
				
				// hash function for cost contexts hash table
				static
				ULONG UlHash(const MdidPtr& pmdid);
		};
		
		// element in the MD provider hashtable
		struct SMDProviderElem
		{
			private:
				// source system id
				CSystemId m_sysid;

				// value of the hashed element
				IMDProvider *m_pmdp;

			public:
				// generic link
				SLink m_link;
		
				// invalid key
				static const SMDProviderElem m_mdpelemInvalid;
		
				// ctor
				SMDProviderElem(CSystemId sysid, IMDProvider *pmdp);
				
				// dtor
				~SMDProviderElem();
		
				// return the MD provider
				IMDProvider *Pmdp();
				
				// return the system id
				CSystemId Sysid() const;

				// equality function for hash tables
				static
				BOOL FEqual(const SMDProviderElem &mdpelemLeft, const SMDProviderElem &mdpelemRight);

				// hash function for MD providers hash table
				static
				ULONG UlHash(const SMDProviderElem &mdpelem);
		};
		
		private:
			// memory pool
			IMemoryPool *m_pmp;
			
			// metadata cache
			MDCache *m_pcache;
						
			// generic metadata provider
			CMDProviderGeneric *m_pmdpGeneric;
		
			// hashtable of cache accessors
			MDHT m_shtCacheAccessors;
			
			// hashtable of MD providers
			MDPHT m_shtProviders;

			// total time consumed in looking up MD objects (including time used to fetch objects from MD provider)
			CDouble m_dLookupTime;

			// total time consumed in fetching MD objects from MD provider,
			// this time is currently dominated by serialization time
			CDouble m_dFetchTime;

			// private copy ctor
			CMDAccessor(const CMDAccessor&);
			
			// interface to a MD cache object
			const IMDCacheObject *Pimdobj(IMDId *pmdid);

			// return the type corresponding to the given type info and source system id
			const IMDType *Pmdtype(CSystemId sysid, IMDType::ETypeInfo eti);

			// return the generic type corresponding to the given type info
			const IMDType *Pmdtype(IMDType::ETypeInfo eti);
			
			// destroy accessor element when MDAccessor is destroyed
			static
			void DestroyAccessorElement(SMDAccessorElem *pmdaccelem);

			// destroy accessor element when MDAccessor is destroyed
			static
			void DestroyProviderElement(SMDProviderElem *pmdpelem);
			
			// lookup an MD provider by system id
			IMDProvider *Pmdp(CSystemId sysid);
			
			// initialize hash tables
			void InitHashtables(IMemoryPool *pmp);

			// return the column statistics meta data object for a given column of a table
			const IMDColStats *Pmdcolstats(IMemoryPool *pmp, IMDId *pmdidRel, ULONG ulPos);

			// record histogram and width information for a given column of a table
			void RecordColumnStats
					(
					IMemoryPool *pmp,
					IMDId *pmdidRel,
					ULONG ulColId,
					ULONG ulPos,
					BOOL fSystemCol,
					BOOL fEmptyTable,
					HMUlHist *phmulhist,
					HMUlDouble *phmuldoubleWidth,
					CStatisticsConfig *pstatsconf
					);

			// construct a stats histogram from an MD column stats object  
			CHistogram *Phist(IMemoryPool *pmp, IMDId *pmdidType, const IMDColStats *pmdcolstats);

			// construct a typed bucket from a DXL bucket  
			CBucket *Pbucket(IMemoryPool *pmp, IMDId *pmdidType, const CDXLBucket *pdxlbucket);
			
			// construct a typed datum from a DXL bucket  
			IDatum *Pdatum(IMemoryPool *pmp, IMDId *pmdidType, const CDXLDatum *pdxldatum);

		public:
			// ctors
			CMDAccessor(IMemoryPool *pmp, MDCache *pcache);
			CMDAccessor(IMemoryPool *pmp, MDCache *pcache, CSystemId sysid, IMDProvider *pmdp);
			CMDAccessor(IMemoryPool *pmp, MDCache *pcache, const DrgPsysid *pdrgpsysid, const DrgPmdp *pdrgpmdp);
			
			//dtor
			~CMDAccessor();
			
			// return MD cache
			MDCache *Pcache() const
			{
				return m_pcache;
			}

			// register a new MD provider
			void RegisterProvider(CSystemId sysid, IMDProvider *pmdp);
			
			// register given MD providers
			void RegisterProviders(const DrgPsysid *pdrgpsysid, const DrgPmdp *pdrgpmdp);

			// interface to a relation object from the MD cache
			const IMDRelation *Pmdrel(IMDId *pmdid);

			// interface to type's from the MD cache given the type's mdid
			const IMDType *Pmdtype(IMDId *pmdid);
			
			// obtain the specified base type given by the template parameter
			template <class T>
			const T* PtMDType()
			{
				IMDType::ETypeInfo eti = T::EtiType();
				GPOS_ASSERT(IMDType::EtiGeneric != eti);
				return dynamic_cast<const T*>(Pmdtype(eti));
			}
			
			// obtain the specified base type given by the template parameter
			template <class T>
			const T* PtMDType(CSystemId sysid)
			{
				IMDType::ETypeInfo eti = T::EtiType();
				GPOS_ASSERT(IMDType::EtiGeneric != eti);
				return dynamic_cast<const T*>(Pmdtype(sysid, eti));
			}
			
			// interface to a scalar operator from the MD cache
			const IMDScalarOp *Pmdscop(IMDId *pmdid);
			
			// interface to a function from the MD cache
			const IMDFunction *Pmdfunc(IMDId *pmdid);
			
			// interface to check if the window function from the MD cache is an aggregate window function
			BOOL FAggWindowFunc(IMDId *pmdid);

			// interface to an aggregate from the MD cache
			const IMDAggregate *Pmdagg(IMDId *pmdid);
	
			// interface to a trigger from the MD cache
			const IMDTrigger *Pmdtrigger(IMDId *pmdid);

			// interface to an index from the MD cache
			const IMDIndex *Pmdindex(IMDId *pmdid);

			// interface to a check constraint from the MD cache
			const IMDCheckConstraint *Pmdcheckconstraint(IMDId *pmdid);

			// retrieve a column stats object from the cache
			const IMDColStats *Pmdcolstats(IMDId *pmdid);

			// retrieve a relation stats object from the cache
			const IMDRelStats *Pmdrelstats(IMDId *pmdid);
			
			// retrieve a cast object from the cache
			const IMDCast *Pmdcast(IMDId *pmdidSrc, IMDId *pmdidDest);
			
			// retrieve a scalar comparison object from the cache
			const IMDScCmp *Pmdsccmp(IMDId *pmdidLeft, IMDId *pmdidRight, IMDType::ECmpType ecmpt);

			// construct a statistics object for the columns of the given relation
			IStatistics *Pstats
				(
				IMemoryPool *pmp, 
				IMDId *pmdidRel,
				CColRefSet *pcrsHist,  // set of column references for which stats are needed
				CColRefSet *pcrsWidth, // set of column references for which the widths are needed
				CStatisticsConfig *pstatsconf = NULL
				);
			
			// calculate space necessary for serializing sysids
			ULONG_PTR UlpRequiredSysIdSpace();

			// calculate space necessary for serializing MD objects
			ULONG_PTR UlpRequiredSpace();
			
			// serialize object to passed buffer
			ULONG_PTR UlpSerialize(WCHAR *wszBuffer, ULONG_PTR ulpAllocSize);
			
			// serialize system ids to passed buffer
			ULONG_PTR UlpSerializeSysid(WCHAR *wszBuffer, ULONG_PTR ulpAllocSize);
	};
}



#endif // !GPOPT_CMDAccessor_H

// EOF
