/*-------------------------------------------------------------------------
 *
 * catalog.h
 *	  prototypes for functions in backend/catalog/catalog.c
 *
 *
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $PostgreSQL: pgsql/src/include/catalog/catalog.h,v 1.36 2006/07/31 20:09:05 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef CATALOG_H
#define CATALOG_H

#include "access/genam.h"
#include "utils/relcache.h"
#include "utils/rel.h"


extern char *relpath(RelFileNode rnode);
extern void CopyRelPath(char *target, int targetMaxLen, RelFileNode rnode);
extern char *GetDatabasePath(Oid dbNode, Oid spcNode);
extern void CopyDatabasePath(char *target, int targetMaxLen, Oid dbNode, Oid spcNode);
extern void FormDatabasePath(char *databasePath, char *filespaceLocation, Oid tablespaceOid, Oid databaseOid);
extern void FormTablespacePath(char *tablespacePath, char *filespaceLocation, Oid tablespaceOid);
extern void FormRelationPath(char *relationPath, char *filespaceLocation, RelFileNode rnode);

extern bool IsSystemRelation(Relation relation);
extern bool IsToastRelation(Relation relation);
extern bool IsAoSegmentRelation(Relation relation);

extern bool IsSystemClass(Form_pg_class reltuple);
extern bool IsToastClass(Form_pg_class reltuple);
extern bool IsAoSegmentClass(Form_pg_class reltuple);

extern bool IsSystemNamespace(Oid namespaceId);
extern bool IsToastNamespace(Oid namespaceId);
extern bool IsAoSegmentNamespace(Oid namespaceId);


extern bool isMasterOnly(Oid relationOid);

extern bool IsReservedName(const char *name);
extern char* GetReservedPrefix(const char *name);

extern bool IsSharedRelation(Oid relationId);

extern Oid	GetNewOid(Relation relation);
extern Oid	GetNewOidWithIndex(Relation relation, Relation indexrel);
extern Oid GetNewRelFileNode(Oid reltablespace, bool relisshared,
				  Relation pg_class);
extern bool CheckNewRelFileNodeIsOk(Oid newOid, Oid reltablespace, bool relisshared, Relation pg_class);

/* hiddencat.c */
extern HeapTuple *GetHiddenPgProcTuples(Relation pg_proc, int *len);
extern HiddenScanDesc hidden_beginscan(Relation heapRelation, int nkeys, ScanKey key);
extern HeapTuple hidden_getnext(HiddenScanDesc hscan, ScanDirection direction);
extern void hidden_endscan(HiddenScanDesc hscan);

#endif   /* CATALOG_H */
