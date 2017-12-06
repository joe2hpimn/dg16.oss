/*-------------------------------------------------------------------------
 *
 * relcache.h
 *	  Relation descriptor cache definitions.
 *
 *
 * Portions Copyright (c) 2005-2009, Greenplum inc.
 * Portions Copyright (c) 1996-2009, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $PostgreSQL: pgsql/src/include/utils/relcache.h,v 1.56 2006/11/05 23:40:31 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef RELCACHE_H
#define RELCACHE_H

#include "access/htup.h"
#include "access/skey.h"
#include "access/tupdesc.h"
#include "nodes/bitmapset.h"
#include "nodes/pg_list.h"
#include "catalog/pg_class.h"


struct SysScanDescData; /* defined in access/genam.h */

typedef struct RelationData *Relation;

/* ----------------
 *		RelationPtr is used in the executor to support index scans
 *		where we have to keep track of several index relations in an
 *		array.	-cim 9/10/89
 * ----------------
 */
typedef Relation *RelationPtr;

/*
 * Routines to open (lookup) and close a relcache entry
 */
extern Relation RelationIdGetRelation(Oid relationId);
extern void RelationClose(Relation relation);

/*
 * Routines to compute/retrieve additional cached information
 */
struct GpPolicy *RelationGetPartitioningKey(Relation relation);
extern List *RelationGetIndexList(Relation relation);
extern Oid	RelationGetOidIndex(Relation relation);
extern List *RelationGetIndexExpressions(Relation relation);
extern List *RelationGetIndexPredicate(Relation relation);

extern void RelationSetIndexList(Relation relation,
					 List *indexIds, Oid oidIndex);

extern void RelationInitIndexAccessInfo(Relation relation);

/*
 * Routines for backend startup
 */
extern void RelationCacheInitialize(void);
extern void RelationCacheInitializePhase2(void);
extern void RelationCacheInitializePhase3(void);
	
/*
 * Routine to create a relcache entry for an about-to-be-created relation
 */
extern Relation RelationBuildLocalRelation(const char *relname,
						   Oid relnamespace,
						   TupleDesc tupDesc,
						   Oid relid,
						   Oid reltablespace,
			               char relkind,            /*CDB*/
						   bool shared_relation);

/*
 * Routines for flushing/rebuilding relcache entries in various scenarios
 */
extern void RelationForgetRelation(Oid rid);

extern void RelationCacheInvalidateEntry(Oid relationId);

extern void RelationCacheInvalidate(void);

extern void AtEOXact_RelationCache(bool isCommit);
extern void AtEOSubXact_RelationCache(bool isCommit, SubTransactionId mySubid,
						  SubTransactionId parentSubid);

/*
 * Routines to help manage rebuilding of relcache init file
 */
extern bool RelationIdIsInInitFile(Oid relationId);
extern void RelationCacheInitFileInvalidate(bool beforeSend);
extern void RelationCacheInitFileRemove(void);

extern void IndexSupportInitialize(oidvector *indclass,
					   Oid *indexOperator,
					   RegProcedure *indexSupport,
					   StrategyNumber maxStrategyNumber,
					   StrategyNumber maxSupportNumber,
					   AttrNumber maxAttributeNumber);

/* should be used only by relcache.c and catcache.c */
extern bool criticalRelcachesBuilt;
extern bool criticalSharedRelcachesBuilt;

#endif   /* RELCACHE_H */
