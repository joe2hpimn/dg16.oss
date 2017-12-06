/*-------------------------------------------------------------------------
 *
 * filespace.c
 *	  Commands to manipulate filespaces
 *
 * Copyright (c) 2009-2010 Greenplum Inc
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "miscadmin.h"

/* System libraries for file and directory operations */
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "catalog/catalog.h"
#include "catalog/dependency.h"
#include "catalog/gp_segment_config.h"
#include "catalog/heap.h"
#include "catalog/indexing.h"
#include "catalog/pg_filespace.h"
#include "catalog/pg_filespace_entry.h"
#include "catalog/pg_tablespace.h"
#include "commands/comment.h"
#include "commands/filespace.h"
#include "storage/fd.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"

#include "cdb/cdbdisp.h"
#include "cdb/cdbmirroredfilesysobj.h"
#include "cdb/cdbvars.h"
#include "cdb/cdbutil.h"
#include "catalog/catquery.h"
#include "access/genam.h"
#include "postmaster/primary_mirror_mode.h"

static void checkPathFormat(char *path);
static void checkPathPermissions(char *path);
static void duplicatePathCheck(FileSpaceEntry *fse1, FileSpaceEntry *fse2);
static void filespace_check_empty(Oid fsoid);

static void DeleteFilespaceEntryTuples(Oid fsoid);


/*
 * Calculate maximum filespace path length, Remember that we're going to append
 * '/<tbsoid>/<dboid>/<relid>.<nnn>'  
 *
 *      10 digits for each oid and the extension number,
 *       1 digit for each slash and the '.' of the extension
 *       = 10*4 + 4 = +44 characters
 *
 * Note: This may be overly conservative.  Do we ever form the whole path 
 * explicitly?
 */
#define MAX_FILESPACE_PATH (MAXPGPATH - 44)

/*
 * Set maximum allowed number of filespaces.
 *
 * Expected number of filespaces is < 10
 *
 * Should probably be made into a guc.
 */
#define MAX_FILESPACES 64


typedef struct
{
	int32                dbid;
	FileSpaceEntry		*fse;
} segHashElem;

/*
 * Create a filespace
 *
 * Only superusers can create a filespace. This seems a reasonable restriction
 * since we're determining the system layout and, anyway, we probably have root
 * if we're doing this kind of activity
 */
void
CreateFileSpace(CreateFileSpaceStmt *stmt)
{
	Relation			 rel;	
	HeapTuple			 tuple;
	NameData			 fsname;		/* filespace name */
	Oid					 ownerId;		/* OID of the OWNER of the filespace */
	Oid					 fsoid;	/* OID of the created filespace */
	ListCell			*cell;	/* List loop variable */
	int					 i;     /* Array loop variable */
	bool				 nulls[Natts_pg_filespace];
	Datum				 values[Natts_pg_filespace];
	FileSpaceEntry      *primary  = NULL;
	FileSpaceEntry      *mirror	  = NULL;
	List                *nodeSegs = NULL;
	ItemPointerData		 persistentTid;
	int64				 persistentSerialNum;
	cqContext			 cqc;
	cqContext			*pcqCtx;

	if (Gp_role == GP_ROLE_UTILITY)
		elog(ERROR, "cannot create filespaces in utility mode");

	/* Must be super user */
	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied to create filespace \"%s\"",
						stmt->filespacename),
				 errhint("Must be superuser to create a filespace.")));

	/* However, the eventual owner of the filespace need not be */
	if (stmt->owner)
		ownerId = get_roleid_checked(stmt->owner);
	else
		ownerId = GetUserId();

	/*
	 * Disallow creation of filespaces named "pg_xxx"; we reserve this namespace
	 * for system purposes.
	 */
	if (!allowSystemTableModsDDL && IsReservedName(stmt->filespacename))
	{
		ereport(ERROR,
				(errcode(ERRCODE_RESERVED_NAME),
				 errmsg("unacceptable filespace name \"%s\"",
						stmt->filespacename),
				 errdetail("The prefix \"%s\" is reserved for system filespaces.",
						   GetReservedPrefix(stmt->filespacename))));
	}
	namestrcpy(&fsname, stmt->filespacename);

	/*
	 * Because rollback of filespace creation is unpleasant we prefer
	 * to ensure that we fully serialize CREATE FILESPACE operations.
	 * Therefore we take a big lock up-front.
	 * NOTE: AccessExclusiveLock, not RowExclusiveLock
	 */
	rel = heap_open(FileSpaceRelationId, AccessExclusiveLock);

	pcqCtx = 
			caql_beginscan(
					caql_addrel(cqclr(&cqc), rel),
					cql("INSERT INTO pg_filespace",
						NULL));

	/* Check that there is no other filespace by this name. */
	fsoid = get_filespace_oid(rel, stmt->filespacename);
	if (OidIsValid(fsoid))
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("filespace \"%s\" already exists",
						stmt->filespacename)));

	/* Check specification against configuration information */
	if (Gp_role == GP_ROLE_DISPATCH)
	{
		CdbComponentDatabases	*segments;
		int						 numsegs;
		HTAB					*segHash;
		HASHCTL					 segInfo;
		int						 segFlags;
		segHashElem				*segElem;
		HASH_SEQ_STATUS          status;
		bool					 found;

		/* Get the segment information */
		segments = getCdbComponentDatabases();
		numsegs = segments->total_segment_dbs + segments->total_entry_dbs;

		/* 
		 * We need to perform several checks:
		 *   - Does every segment have a path specified?
		 *   - Does any segment have more than one path specified?
		 *   - Are there paths specified for non-existent segments?
		 *   - Are any paths duplicated between segments on the same host?
		 * 
		 * In order to answer those questions in a reasonable O(N) calculation
		 * we build a hash table mapping dbid => filespace entries
		 *   - segHash[dbid]      => {FileSpaceEntry}
		 *
		 * Note: the segHash is hashed on a 32 bit version of dbid.  This is due
		 * to the existence of a int32_hash function, to accomadate this we must
		 * be sure to upcast the 16 bit dbid to 32 bit versions before lookup in
		 * the hash.
		 */
		memset(&segInfo,  0, sizeof(segInfo));
		segInfo.keysize    = sizeof(int32);
		segInfo.entrysize  = sizeof(segHashElem);
		segInfo.hash       = int32_hash;
		segInfo.hcxt       = CurrentMemoryContext;
		segFlags           = HASH_ELEM | HASH_FUNCTION | HASH_CONTEXT;
		segHash = hash_create("filespace segHash", 
							  numsegs, &segInfo, segFlags);
		
		/* 
		 * Pass 1 - Loop through all locations specified in the statement:
		 *   - segHash[dbid] => { _, FileSpaceEntry} 
		 *   - check for any duplicate dbids
		*/
		foreach (cell, stmt->locations) 
		{
			FileSpaceEntry		*fse  = (FileSpaceEntry*) lfirst(cell); 
			int32				 dbid = (int32) fse->dbid;

			/* Check for existing entry */
			segElem = (segHashElem *) \
				hash_search(segHash, (void *) &dbid, HASH_ENTER, &found);

			if (found)
				ereport(ERROR,
						(errcode(ERRCODE_GP_COMMAND_ERROR),
						 errmsg("multiple filespace locations specified "
								"for dbid %d", dbid)));

			/* Populate the hash entry */
			segElem->fse = fse;
		}
		
		/* 
		 * Pass 2 - Loop through segment information in the array.
		 *   - check for any segments not specified in the command.
		 *   - annotates the FileSpaceEntry with the contentid and hostname
		 *
		 * Done in two loops, one through the "entry_dbs" list, the second
		 * through the "segment_dbs" list.
		 */
		for (i = 0; i < segments->total_entry_dbs; i++)
		{
			int32		 dbid	   = segments->entry_db_info[i].dbid;
			int32		 contentid = segments->entry_db_info[i].segindex;
			char		*hostname  = segments->entry_db_info[i].hostname;
			
			/* Lookup the entry in the segHash */
			segElem = (segHashElem *) \
				hash_search(segHash, (void *) &dbid, HASH_FIND, &found);

			if (!found)
				ereport(ERROR,
						(errcode(ERRCODE_GP_COMMAND_ERROR),
						 errmsg("missing filespace location for dbid %d",
								dbid)));

			Assert(segElem->fse);  /* should have been populated in pass 1 */
			segElem->fse->hostname  = hostname;
			segElem->fse->contentid = contentid;
		}
		for (i = 0; i < segments->total_segment_dbs; i++)
		{
			int32		dbid	  = segments->segment_db_info[i].dbid;
			int32		contentid = segments->segment_db_info[i].segindex;
			char       *hostname  = segments->segment_db_info[i].hostname;

			/* Lookup the entry in the segHash */
			segElem = (segHashElem *) \
				hash_search(segHash, (void *) &dbid, HASH_FIND, &found);

			if (!found)
				ereport(ERROR,
						(errcode(ERRCODE_GP_COMMAND_ERROR),
						 errmsg("missing filespace location for dbid %d",
								dbid)));

			Assert(segElem->fse);  /* should have been populated in pass 1 */
			segElem->fse->hostname  = hostname;
			segElem->fse->contentid = contentid;
		}

		/*
		 * Scan through the segHash to see any locations were specified for
		 * segments that do not exist.
		 */
		hash_seq_init(&status, segHash);
		while ((segElem = (segHashElem *) hash_seq_search(&status)) != NULL)
		{
			Assert(segElem->fse);  /* should have been populated in pass 1 */
			Assert(segElem->fse->location);

			/* Check that the path is well formed */
			checkPathFormat(segElem->fse->location);

			/* Check that the path is for a segment that exists */
			if (segElem->fse->hostname == NULL)
				ereport(ERROR,
						(errcode(ERRCODE_GP_COMMAND_ERROR),
						 errmsg("filespace location specified for non-existent "
								"dbid %d", segElem->dbid)));
		}

		/* Done with the hash, cleanup */
		hash_destroy(segHash);
	}

	/* 
	 * Find the location of the filespace for *this* segment, and perform 
	 * additional validations on the path.
	 */
	foreach(cell, stmt->locations)
	{
		FileSpaceEntry *fse  = (FileSpaceEntry*) lfirst(cell);

		/* 
		 * Find the location information for this dbid, and identify the
		 * dbid and location for our mirror pair (matching contentid)
		 */
		if (fse->dbid == GpIdentity.dbid)
		{
			Assert(primary == NULL);
			primary = fse;
		}
		else if (fse->contentid == GpIdentity.segindex)
		{
			/* 
			 * This will require work if we start supporting multiple 
			 * mirrors per segment. 
			 */
			Assert(mirror == NULL);
			mirror = fse;
		}
	}

	/* 
	 * Should have been checked before QE dispatch, something is weird if
	 * we hit this condition here.
	 */
	if (primary == NULL)
		elog(ERROR, "filespace location not specified for dbid %d",
			 GpIdentity.dbid);

	/* 
	 * Check the path for the primary and mirror.  
	 *
	 * We need to check this this before we ask the persistent object layer to
	 * create the directory because once we switch to "CreatePending" any
	 * failure will remove any directories that exist on the assumption that we
	 * created them, but if they already existed then that is a mistake.
	 *
	 * Note that this is theoretically a race condidition with someone manually
	 * creating the directories in the filesystem between when we check the
	 * paths and when we mark "CreatePending".  The race condition doesn't exist
	 * purely in the database because we have a giant lock and only a single
	 * transaction can create filespaces at one time.
	 */
	checkPathPermissions(primary->location);
	if (mirror)
		MirroredFileSysObj_ValidateFilespaceDir(mirror->location);

	/* 
	 * Having identified the fse entry for this segment we now know what
	 * the canonical hostname is for this host and can identify the other
	 * segments running on this host.  
	 *
	 * Note: since we need to check the mirrors against other mirrors and
	 * we can't do this on the mirror side it is the responsibility of
	 * the primaries on the node to check the mirrors.  Since they can't
	 * currently differentiate which of the other segments are primaries
	 * and which are mirrors all of them end up checking everything. 
	 * This is redundant, but its the only way we can check the mirrors.
	 *
	 * Note: This is only a best effort attempt at verifying the mirrors.
	 * If there is a configuration where some node has no primaries
	 * then this methodology means that there is no primary available to
	 * check those mirrors and we will not be able pre-validate against
	 * duplicate paths.  This could result in much uglier error messages,
	 * but the underlying code in TransactionCreateFilespaceDir() should
	 * be able to handle this.  (MPP-8595)
	 */
	foreach(cell, stmt->locations)
	{
		FileSpaceEntry	*outer = (FileSpaceEntry*) lfirst(cell);
		ListCell        *cell2;

		/* 
		 * For every segment on this host:
		 *  - compare against every segments found so far
		 *  - add to the list of segments
		 *
		 * Segments on other hosts are not compared and are not added to the
		 * list of segments to compare against.
		 */
		if ((strcmp(outer->hostname, primary->hostname) == 0))
		{
			foreach(cell2, nodeSegs)
			{
				FileSpaceEntry  *inner = (FileSpaceEntry *) lfirst(cell2);
				duplicatePathCheck(outer, inner);
			}
			nodeSegs = lappend(nodeSegs, outer);
		}
	}
	list_free(nodeSegs);
	nodeSegs = NULL;

	/* 
	 * Everything possible has been checked: 
	 *  - Begin actual creation of the filespace
	 */

	/* The relation was opened up at the top of the function */
	Assert(rel);

	/* Insert tuple into pg_filespace */
	MemSet(nulls, false, sizeof(nulls));
	values[Anum_pg_filespace_fsname - 1]  = NameGetDatum(&fsname);
	values[Anum_pg_filespace_fsowner - 1] = ObjectIdGetDatum(ownerId);
	tuple = caql_form_tuple(pcqCtx, values, nulls);

	/* Keep oids synchonized between master and segments */
	if (OidIsValid(stmt->fsoid))
		HeapTupleSetOid(tuple, stmt->fsoid);

	/* insert a new tuple */
	fsoid = caql_insert(pcqCtx, tuple); /* implicit update of index as well */
	Assert(OidIsValid(fsoid));

	heap_freetuple(tuple);

	/* Record dependency on owner */
	recordDependencyOnOwner(FileSpaceRelationId, fsoid, ownerId);

	/* Keep the lock until commit/abort */
	caql_endscan(pcqCtx);
	heap_close(rel, NoLock);

	/* 
	 * Master only:
	 *   1) Add all locations to the pg_filespace_entry table
	 *   2) Dispatch to the segments
	 *
	 * Note: we keep the filespace_entry catalog a master-only catalog
	 * because this makes expansion and adding mirrors an easier process:
	 *   - We can add them to the catalog to the master in utility mode
	 *     while the system is down.
	 *   - There isn't a bootstrapping problem of which to create first
	 *     the new segment or the catalog that describes it.
	 */
	if (Gp_role == GP_ROLE_DISPATCH)
	{
		rel = heap_open(FileSpaceEntryRelationId, RowExclusiveLock);

		foreach(cell, stmt->locations)
		{
			FileSpaceEntry *fse  = (FileSpaceEntry*) lfirst(cell);
			
			add_catalog_filespace_entry(rel, fsoid, fse->dbid, fse->location);
		}

		heap_close(rel, RowExclusiveLock);

		/* Dispatch to segments */
		stmt->fsoid = fsoid;  /* Already Asserted OidIsValid */
		CdbDispatchUtilityStatement((Node *) stmt, "CreateFilespaceCommand");

		/* MPP-6929: metadata tracking */
		MetaTrackAddObject(FileSpaceRelationId,
						   fsoid,
						   GetUserId(),
						   "CREATE", "FILESPACE"
				);
	}

	/* 
	 * Update the gp_persistent_filespace_node table.
	 *
	 * The persistent object layer is responsible for ensuring that the
	 * directories are created and maintained in the filesystem.  Most 
	 * importantly this layer knows how to cleanup filesystem objects in the
	 * event that this transaction aborts and the rollback and recovery 
	 * mechanisms know how to use this to cleanup after a hard failure or 
	 * replay the creation for mirror resynchronisation.
	 */
	if (mirror == NULL)
	{
		MirroredFileSysObj_TransactionCreateFilespaceDir(
			fsoid, 
			primary->dbid, primary->location, 0, NULL,
			&persistentTid, &persistentSerialNum);
	}
	else
	{
		MirroredFileSysObj_TransactionCreateFilespaceDir(
			fsoid, 
			primary->dbid, primary->location, mirror->dbid, mirror->location,
			&persistentTid, &persistentSerialNum);
	}
}

/*
 * Drop a filespace
 *
 * Be careful to check that the filespace is empty.
 */
void 
RemoveFileSpace(List *names, DropBehavior behavior, bool missing_ok)
{
	Relation      rel;
	char         *fsname;
	Oid			  fsoid;
	ObjectAddress object;

	/* 
	 * General DROP (object) syntax allows fully qualified names, but
	 * filespaces are global objects that do not live in schemas, so
	 * it is a syntax error if a fully qualified name was given.
	 */
	if (list_length(names) != 1)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("filespace name may not be qualified")));
	fsname = strVal(linitial(names));

	/* Disallow CASCADE */
	if (behavior == DROP_CASCADE)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("syntax at or near \"cascade\"")));

	/* 
	 * Because rollback of filespace operations are difficult and expected
	 * usage is anticipated to be light we remove concurency worries by
	 * taking a big lock up front.
	 */
	rel = heap_open(FileSpaceRelationId, AccessExclusiveLock);

	/* Lookup the name in pg_filespace */
	fsoid = get_filespace_oid(rel, fsname);
	if (!OidIsValid(fsoid))
	{
		heap_close(rel, AccessExclusiveLock);

		if (missing_ok)
		{
			if (Gp_role != GP_ROLE_EXECUTE)
				ereport(NOTICE,
						(errmsg("filespace \"%s\" does not exist, skipping", 
								fsname)));
			return;
		}
		else
		{
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("filespace \"%s\" does not exist", fsname)));
		}
	}

	/* Must be owner */
	if (!pg_filespace_ownercheck(fsoid, GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_FILESPACE, fsname);

	/* Disallow drop of the standard filespaces, even by superuser */
	if (fsoid == SYSTEMFILESPACE_OID)
		ereport(ERROR,
				(errmsg("cannot drop filespace %s because it is required "
						"by the database system", fsname)));

	/* 
	 * Disallow drop of filespace if it is used for transaction files or
	 * temporary files.
	 */
	if (isFilespaceUsedForTempFiles(fsoid))
		ereport(ERROR,
				(errmsg("cannot drop filespace %s because it is used "
						"by temporary files \n"
						"Use gpfilespace --movetempfilespace <newFilespaceName>	to move temporary files to a different filespace\n"
						"and then attempt DROP FILESPACE", fsname)));

	if (isFilespaceUsedForTxnFiles(fsoid))
		ereport(ERROR,
                                (errmsg("cannot drop filespace %s because it is used "
                                                "by transaction files\n"
						"Use gpfilespace --movetransfilespace <newFilespaceName> to move transaction files to a different filespace\n"
						"and then attempt DROP FILESPACE", fsname)));

	/*
	 * performDeletion only drops things that have dependencies in
	 * pg_depend/pg_shdepend which does NOT include dependencies on tablespaces
	 * (perhaps pg_shdepend should).  So we look for these dependencies by
	 * looking at the pg_tablespace table.
	 */
	filespace_check_empty(fsoid);

	/* Check for dependencies and remove the filespace */
	object.classId = FileSpaceRelationId;
	object.objectId = fsoid;
	object.objectSubId = 0;
	performDeletion(&object, DROP_RESTRICT);

	/*
	 * Remove any comments on this filespace
	 */
	DeleteSharedComments(fsoid, FileSpaceRelationId);

	/* 
	 * Keep the lock until commit/abort 
	 */
	heap_close(rel, NoLock);

	/* 
	 * Master Only:
	 *   1) Remove entries from pg_filespace_entry
	 *
	 * Note: no need for dispatch, that is handled in utility.c
	 */
	if (Gp_role == GP_ROLE_DISPATCH)
	{
		DeleteFilespaceEntryTuples(fsoid);

		/* MPP-6929: metadata tracking */
		MetaTrackDropObject(FileSpaceRelationId,
							fsoid);
	}

	/* 
	 * The persistent object layer is responsible for actually managing the
	 * actual directory on disk.  Tell it that this filespace is removed by
	 * this transaciton.  This marks the filespace as pending delete and it
	 * will be deleted iff the transaction commits.
	 */
	MirroredFileSysObj_ScheduleDropFilespaceDir(fsoid);
}

/* 
 * RemoveFileSpaceById
 *   Guts of Filespace Deletion, called by dependency.c
 */
void
RemoveFileSpaceById(Oid fsoid)
{
	int numDel;

	numDel = caql_getcount(
			NULL,
			cql("DELETE FROM pg_filespace "
				" WHERE oid = :1 ",
				ObjectIdGetDatum(fsoid)));

	if (numDel != 1) /* shouldn't happen */
		elog(ERROR, "cache lookup failed for filespace %u", fsoid);
}

static void 
DeleteFilespaceEntryTuples(Oid fsoid)
{
	(void) caql_getcount(
		NULL,
		cql("DELETE FROM pg_filespace_entry "
			" WHERE fsefsoid = :1 ",
			ObjectIdGetDatum(fsoid)));
}


/*
 * Change filespace owner
 */
void
AlterFileSpaceOwner(List *names, Oid newOwnerId)
{
	char             *fsname;
	Oid               fsoid;
	Relation	      rel;
	Form_pg_filespace fsForm;
	HeapTuple	      tup;
	cqContext		  cqc;
	cqContext		 *pcqCtx;

	/*
	 * This was from a generic AltrStmt node which allows for fully qualified
	 * object names, but filespaces don't exist inside schemas so fully
	 * qualified names are a syntax error.
	 */
	if (list_length(names) != 1)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("filespace name may not be qualified")));
	fsname = strVal(linitial(names));

	/* Search pg_filespace */
	rel = heap_open(FileSpaceRelationId, RowExclusiveLock);

	pcqCtx = caql_addrel(cqclr(&cqc), rel);

	tup = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_filespace "
				" WHERE fsname = :1 "
				" FOR UPDATE ",
				CStringGetDatum(fsname)));

	if (!HeapTupleIsValid(tup))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("filespace \"%s\" does not exist", fsname)));
	fsoid  = HeapTupleGetOid(tup);
	fsForm = (Form_pg_filespace) GETSTRUCT(tup);

	/* Cannot alter system filespaces */
	if (!allowSystemTableModsDDL && IsReservedName(fsname))
		ereport(ERROR,
				(errcode(ERRCODE_RESERVED_NAME),
				 errmsg("permission denied: \"%s\" is a system filespace", 
						fsname)));

	/*
	 * If the new owner is the same as the existing owner, consider the
	 * command to have succeeded.  This is for dump restoration purposes.
	 */
	if (fsForm->fsowner != newOwnerId)
	{
		Datum		values[Natts_pg_filespace];
		bool		nulls[Natts_pg_filespace];
		bool		replace[Natts_pg_filespace];
		HeapTuple	newtuple;

		/* Otherwise, must be owner of the existing object */
		if (!pg_filespace_ownercheck(fsoid, GetUserId()))
			aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_FILESPACE, fsname);

		/* Must be able to become new owner */
		check_is_member_of_role(GetUserId(), newOwnerId);

		/*
		 * Normally we would also check for create permissions here, but there
		 * are none for filespaces so we follow what rename filespace does
		 * and omit the create permissions check.
		 *
		 * NOTE: Only superusers may create filespaces to begin with and so
		 * initially only a superuser would be able to change its ownership
		 * anyway.
		 */
		memset(nulls, false, sizeof(nulls));
		memset(replace, false, sizeof(replace));

		replace[Anum_pg_filespace_fsowner - 1] = true;
		values[Anum_pg_filespace_fsowner - 1] = ObjectIdGetDatum(newOwnerId);

		newtuple = caql_modify_current(pcqCtx, values, nulls, replace);

		caql_update_current(pcqCtx, newtuple);
		/* and Update indexes (implicit) */

		/* MPP-6929: metadata tracking */
		if (Gp_role == GP_ROLE_DISPATCH)
			MetaTrackUpdObject(FileSpaceRelationId,
							   fsoid,
							   GetUserId(),
							   "ALTER", "OWNER"
					);

		heap_freetuple(newtuple);

		/* Update owner dependency reference */
		changeDependencyOnOwner(FileSpaceRelationId, fsoid, newOwnerId);
	}

	heap_close(rel, RowExclusiveLock);
}


/*
 * Rename a filespace
 */
void
RenameFileSpace(const char *oldname, const char *newname)
{
	Relation	 rel;
	Oid          fsoid;
	HeapTuple	 newtuple;
	cqContext	 cqc;
	cqContext	 cqc2;
	cqContext	*pcqCtx;
	int			 numFsname;
	Form_pg_filespace newform;

	/* Search pg_filespace */
	rel = heap_open(FileSpaceRelationId, RowExclusiveLock);

	pcqCtx = caql_addrel(cqclr(&cqc), rel);

	newtuple = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_filespace "
				" WHERE fsname = :1 "
				" FOR UPDATE ",
				CStringGetDatum(oldname)));
	if (!HeapTupleIsValid(newtuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("filespace \"%s\" does not exist",
						oldname)));

	newform = (Form_pg_filespace) GETSTRUCT(newtuple);

	/* Can't rename system filespaces */
	if (!allowSystemTableModsDDL && IsReservedName(oldname))
		ereport(ERROR,
				(errcode(ERRCODE_RESERVED_NAME),
				 errmsg("permission denied: \"%s\" is a system filespace", 
						oldname)));

	/* Must be owner */
	fsoid = HeapTupleGetOid(newtuple);
	if (!pg_filespace_ownercheck(fsoid, GetUserId()))
		aclcheck_error(ACLCHECK_NO_PRIV, ACL_KIND_FILESPACE, oldname);

	/* Validate new name */
	if (!allowSystemTableModsDDL && IsReservedName(newname))
	{
		ereport(ERROR,
				(errcode(ERRCODE_RESERVED_NAME),
				 errmsg("unacceptable filespace name \"%s\"", newname),
				 errdetail("The prefix \"%s\" is reserved for system filespaces.",
						   GetReservedPrefix(newname))));
	}

	numFsname = caql_getcount(
			caql_addrel(cqclr(&cqc2), rel),
			cql("SELECT COUNT(*) FROM pg_filespace "
				" WHERE fsname = :1 ",
				CStringGetDatum(newname)));

	if (numFsname)
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("filespace \"%s\" already exists", newname)));

	/* OK, update the entry */
	namestrcpy(&(newform->fsname), newname);

	caql_update_current(pcqCtx, newtuple);
	/* and Update indexes (implicit) */

	/* MPP-6929: metadata tracking */
	if (Gp_role == GP_ROLE_DISPATCH)
		MetaTrackUpdObject(FileSpaceRelationId,
						   fsoid,
						   GetUserId(),
						   "ALTER", "RENAME"
				);


	heap_close(rel, RowExclusiveLock);
}



/*
 * get_filespace_name - given a filespace OID, look up the name
 *
 * Returns a palloc'd string, or NULL if no such filespace
 */
char *
get_filespace_name(Oid fsoid)
{
	char		*result;

	/*
	 * Search pg_filespace.  We use a heapscan here even though there is an
	 * index on oid, on the theory that pg_filespace will usually have just a
	 * few entries and so an indexed lookup is a waste of effort.
	 */

	result = caql_getcstring(
			NULL,
			cql("SELECT fsname FROM pg_filespace "
				" WHERE oid = :1 ",
				ObjectIdGetDatum(fsoid)));
			
	/* We assume that there can be at most one matching tuple */

	return result;
}


/*
 * get_filespace_oid - given a filespace name, look up the OID
 *
 * Returns InvalidOid if filespace name not found.
 */
Oid
get_filespace_oid(Relation rel, const char *filespacename)
{
	Oid			 result;
	cqContext	 cqc;

	/* We assume that there can be at most one matching tuple */

	result = caql_getoid(
			caql_addrel(cqclr(&cqc), rel),
			cql("SELECT oid FROM pg_filespace "
				" WHERE fsname = :1 ",
				CStringGetDatum(filespacename)));

	return result;
}

/*
 * checkPathFormat(path)
 *
 * Runs simple validations on a path supplied to CREATE FILESPACE:
 *  - Standardizes paths via canonicalize_path()
 *  - Disallow paths with single quotes
 *  - Disallow relative paths
 *  - Disallow paths that are too long.
 *
 * We have other checks to perform, but these are the only ones that we
 * can run based only on the name without the local file system present.
 */
static void 
checkPathFormat(char *path)
{
	/* Unix-ify the offered path and strip any trailing slashes */
	canonicalize_path(path);

	/* disallow quotes, else CREATE DATABASE would be at risk */
	if (strchr(path, '\''))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_NAME),
				 errmsg("filespace location \"%s\" "
						"cannot contain single quotes", path)));
	
	/*
	 * Allowing relative paths seems risky
	 *
	 * this also helps us ensure that path is not empty or whitespace
	 */
	if (!is_absolute_path(path))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("filespace location \"%s\" "
						"must be an absolute path", path)));

	/*
	 * Check that location isn't too long. 
	 */
	if (strlen(path) >= MAX_FILESPACE_PATH)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("filespace location \"%s\" is too long",
						path),
				 errhint("maximum length %d characters", 
						 MAX_FILESPACE_PATH)));
}

/*
 * checkPathPermissions(path)
 *
 * Runs additional validations on a path supplied to CREATE FILESPACE.
 * The assumption is that the path given to us is the original path
 * that was specified in the gpfilespace command plus a path extension
 * to uniquely identify this segment.  We further assume that the original
 * path exists, but that the segment extension does not and must be created.
 * Or... if the extension path does exist then it must be an empty directory.
 *
 *  We must:
 *    - Validate that the specified path does not exist
 *    - Validate that the parent directory exists
 *    - Validate that the parent is a directory.
 *    - Validate that the parent has apropriate permissions
 *
 * Note: Passing these checks does not guarantee that everything is good.
 * In particular we have not checked anywhere that the paths are all 
 * unique on a given host.  We omit this only because this is a difficult
 * test when we don't have metadata about what segments are on the same host.
 * 
 * If there is a conflict we should see it when we actually try to claim
 * the directories for the segments.
 *
 * Note: May need to add additional checks that there is not a pending
 * background delete on this directory location?
 *
 * Note: See FileRepMirror_Validation() in cdb/cdbfilerepmirror.c for the same
 * checks run on the mirror side.
 */
static void 
checkPathPermissions(char *path)
{
	struct stat st;
	char *parentdir;

	/* The specified path should not exist yet */
	if (stat(path, &st) >= 0)
	{
		ereport(ERROR, 
				(errcode_for_file_access(),
				 errmsg("%s: File exists", path)));
	}

	/* Find the parent directory */
	parentdir = pstrdup(path);
	get_parent_directory(parentdir);

	/* The parent directory must already exist */
	if (stat(parentdir, &st) < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("%s: No such file or directory", 
						parentdir)));
		
	/* The parent directory must be a directory */
	if (! S_ISDIR(st.st_mode))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("%s: Not a directory", parentdir)));

	/* 
	 * Check write permissions of the parent directory 
	 *
	 * Note: Accornding to the BSD manual access shouldn't be used because it
	 * is a security hole, but what they are actually refering to is the fact
	 * that the permissions could change between the time of the check and the
	 * time an action is taken.  This is primarily a courtousy check to produce
	 * a cleaner error message.  If the filesystem should change between now
	 * and the actual mkdir() then the transaction will abort later with an
	 * uglier error message, but it is not actually a security hole.
	 */
	if (access(parentdir, W_OK|X_OK) < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("%s: Permission denied", path)));
}

/*
 * duplicatePathCheck()
 *
 * Compares two filespace entries to determine if they have the same location.
 * 
 * The main complication is that two paths that look different from
 * each other could in fact be the same due to links, mount points, etc.
 *
 * Note: it is assumed that both paths have already been canonicalized.
 */
static void
duplicatePathCheck(FileSpaceEntry *fse1, FileSpaceEntry *fse2)
{
	char		*path1 = fse1->location;
	char		*path2 = fse2->location;
	char		*parent1;
	char		*parent2;
	char		*tail1;
	char		*tail2;
	struct stat	 st1;
	struct stat	 st2;

	/* Split both paths into parent directory and tail */
	parent1 = pstrdup(path1);
	parent2 = pstrdup(path2);
	get_parent_directory(parent1);
	get_parent_directory(parent2);
	tail1 = path1 + strlen(parent1) + 1;
	tail2 = path2 + strlen(parent2) + 1;

	/* 
	 * It is assumed that we have run at least one of these two paths through
	 * checkPathPermissions, which means that it's parent directory exists.  If
	 * the OTHER paths parent doesn't exist, then they aren't the same path.
	 */
	if (stat(parent1, &st1) < 0 || stat(parent2, &st2) < 0)
		goto dup_path_return;

	/*
	 * If the parent paths have different inodes then they are not the same.
	 */
	if (st1.st_ino != st2.st_ino)
		goto dup_path_return;

	/*
	 * Parents are the same; check if the tails are the same.
	 *
	 * We don't know if the underlying file system is case-sensitive or not,
	 * so it is possible that this check is over-aggressive.  But it is
	 * probably not reasonable to name one directory /gpdata/gp0 and a second
	 * one /gpdata/Gp0 so we'll go ahead and complain about case-insentive
	 * matches on the tail even for case-sensitive filesystems.  This saves
	 * us the trouble of trying to figure out what this file system supports.
	 */
	if (strcasecmp(tail1, tail2) == 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("duplicate filespace locations: "
						"(%d: '%s', %d: '%s')",
						fse2->dbid, fse2->location,
						fse1->dbid, fse1->location)));
	}

dup_path_return:
	/* Release memory and return */
	pfree(parent1);
	pfree(parent2);
}

/*
 * filespace_check_empty(fsoid):
 *
 * Checks the gp_persistent_tablespace_node table to determine if the specified
 * filespace is empty.
 */
static void 
filespace_check_empty(Oid fsoid)
{
	if (caql_getcount(
				NULL,
				cql("SELECT COUNT(*) FROM pg_tablespace "
					" WHERE spcfsoid = :1 ",
					ObjectIdGetDatum(fsoid))))
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("filespace \"%s\" is not empty", 
						get_filespace_name(fsoid))));
	}
}

/* Add a pg_filespace_entry for a given filespace definition. */
void
add_catalog_filespace_entry(Relation rel, Oid fsoid, int16 dbid, char *location)
{
	HeapTuple	 tuple;
	Datum		 evalues[Natts_pg_filespace_entry];
	bool		 enulls[Natts_pg_filespace_entry];
	cqContext	 cqc;
	cqContext	*pcqCtx;

	/* NOTE: rel must have correct lock mode for INSERT */
	pcqCtx = 
			caql_beginscan(
					caql_addrel(cqclr(&cqc), rel),
					cql("INSERT INTO pg_filespace_entry",
						NULL));

	MemSet(enulls, false, sizeof(enulls));

	evalues[Anum_pg_filespace_entry_fsefsoid - 1] = ObjectIdGetDatum(fsoid);

	evalues[Anum_pg_filespace_entry_fsedbid - 1] = Int16GetDatum(dbid);
	evalues[Anum_pg_filespace_entry_fselocation - 1] =
				DirectFunctionCall1(textin, CStringGetDatum(location));
			
	tuple = caql_form_tuple(pcqCtx, evalues, enulls);

	/* insert a new tuple */
	caql_insert(pcqCtx, tuple); /* implicit update of index as well */

	heap_freetuple(tuple);
	caql_endscan(pcqCtx);

}

void
dbid_remove_filespace_entries(Relation rel, int16 dbid)
{
	cqContext	 cqc;

	/* Use the index to scan only attributes of the target relation */
	(void) caql_getcount(
		caql_addrel(cqclr(&cqc), rel),
		cql("DELETE FROM pg_filespace_entry "
			" WHERE fsedbid = :1 ",
			Int16GetDatum(dbid)));
}

int
num_filespaces(void)
{
	Relation rel = heap_open(FileSpaceRelationId, AccessShareLock);
	cqContext	 cqc;
	int n = 0;

	Assert(GpIdentity.segindex == MASTER_CONTENT_ID);

	/* XXX: should we do this via gp_persistent_filespace_node? */
	n = caql_getcount(
			caql_addrel(cqclr(&cqc), rel),
			cql("SELECT COUNT(*) FROM pg_filespace", NULL));
	
	heap_close(rel, NoLock);

	return n;
}
