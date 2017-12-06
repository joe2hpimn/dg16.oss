/*-------------------------------------------------------------------------
 *
 * tablespace.c
 *	  Commands to manipulate table spaces
 *
 * Tablespaces in PostgreSQL are designed to allow users to determine
 * where the data file(s) for a given database object reside on the file
 * system.
 *
 * A tablespace represents a directory on the file system. At tablespace
 * creation time, the directory must be empty. To simplify things and
 * remove the possibility of having file name conflicts, we isolate
 * files within a tablespace into database-specific subdirectories.
 *
 * To support file access via the information given in RelFileNode, we
 * maintain a symbolic-link map in $PGDATA/pg_tblspc. The symlinks are
 * named by tablespace OIDs and point to the actual tablespace directories.
 * Thus the full path to an arbitrary file is
 *			$PGDATA/pg_tblspc/spcoid/dboid/relfilenode
 *
 * There are two tablespaces created at initdb time: pg_global (for shared
 * tables) and pg_default (for everything else).  For backwards compatibility
 * and to remain functional on platforms without symlinks, these tablespaces
 * are accessed specially: they are respectively
 *			$PGDATA/global/relfilenode
 *			$PGDATA/base/dboid/relfilenode
 *
 * To allow CREATE DATABASE to give a new database a default tablespace
 * that's different from the template database's default, we make the
 * provision that a zero in pg_class.reltablespace means the database's
 * default tablespace.	Without this, CREATE DATABASE would have to go in
 * and munge the system catalogs of the new database.
 *
 *
 * Copyright (c) 2005-2010 Greenplum Inc
 * Portions Copyright (c) 1996-2009, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/commands/tablespace.c,v 1.39 2006/10/04 00:29:51 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "access/heapam.h"
#include "catalog/heap.h"
#include "access/sysattr.h"
#include "access/xact.h"
#include "catalog/catalog.h"
#include "catalog/catquery.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/pg_filespace.h"
#include "catalog/pg_tablespace.h"
#include "catalog/pg_type.h"
#include "cdb/cdbmirroredflatfile.h"
#include "commands/comment.h"
#include "commands/filespace.h"
#include "commands/tablespace.h"
#include "miscadmin.h"
#include "storage/fd.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"

#include "cdb/cdbdisp.h"
#include "cdb/cdbsrlz.h"
#include "cdb/cdbvars.h"
#include "cdb/cdbutil.h"
#include "access/persistentfilesysobjname.h"
#include "cdb/cdbpersistentrelation.h"
#include "cdb/cdbmirroredfilesysobj.h"



/* GUC variable */
char	   *default_tablespace = NULL;


static bool remove_tablespace_directories(Oid tablespaceoid, bool redo,
										  char *location);
/*
 * Create a table space
 *
 * Only superusers can create a tablespace. This seems a reasonable restriction
 * since we're determining the system layout and, anyway, we probably have
 * root if we're doing this kind of activity
 */
void
CreateTableSpace(CreateTableSpaceStmt *stmt)
{
	Relation	rel;
	Relation    filespaceRel;
	Datum		values[Natts_pg_tablespace];
	bool		nulls[Natts_pg_tablespace];
	HeapTuple	tuple;
	Oid			tablespaceoid;
	Oid         filespaceoid;
	Oid			ownerId;
	TablespaceDirNode tablespaceDirNode;
	ItemPointerData persistentTid;
	int64		persistentSerialNum;
	cqContext	cqc;
	cqContext  *pcqCtx;

	/* validate */

	/* Must be super user */
	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied to create tablespace \"%s\"",
						stmt->tablespacename),
				 errhint("Must be superuser to create a tablespace.")));

	/* However, the eventual owner of the tablespace need not be */
	if (stmt->owner)
		ownerId = get_roleid_checked(stmt->owner);
	else
		ownerId = GetUserId();

	/*
	 * Disallow creation of tablespaces named "pg_xxx"; we reserve this
	 * namespace for system purposes.
	 */
	if (!allowSystemTableModsDDL && IsReservedName(stmt->tablespacename))
	{
		ereport(ERROR,
				(errcode(ERRCODE_RESERVED_NAME),
				 errmsg("unacceptable tablespace name \"%s\"",
						stmt->tablespacename),
				 errdetail("The prefix \"%s\" is reserved for system tablespaces.",
						   GetReservedPrefix(stmt->tablespacename))));
	}

	/*
	 * Check the specified filespace
	 */
	filespaceRel = heap_open(FileSpaceRelationId, RowShareLock);
	filespaceoid = get_filespace_oid(filespaceRel, stmt->filespacename);
	heap_close(filespaceRel, NoLock);  /* hold lock until commit/abort */
	if (!OidIsValid(filespaceoid))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("filespace \"%s\" does not exist",
						stmt->filespacename)));

	/*
	 * Filespace pg_system is reserved for system use:
	 *   - Used for pg_global and pg_default tablespaces only
	 *
	 * Directory layout is slightly different for the system filespace.
	 * Instead of having subdirectories for individual tablespaces instead
	 * the two system tablespaces have specific locations within it:
	 *	   pg_global  :	$PG_SYSTEM/global/relfilenode
	 *	   pg_default : $PG_SYSTEM/base/dboid/relfilenode
	 *
	 * In other words PG_SYSTEM points to the segments "datadir", or in
	 * postgres vocabulary $PGDATA.
	 *
	 */
	if (filespaceoid == SYSTEMFILESPACE_OID && !IsBootstrapProcessingMode())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied to create tablespace \"%s\"",
						stmt->tablespacename),
				 errhint("filespace %s is reserved for system use",
						 stmt->filespacename)));

	/*
	 * Check that there is no other tablespace by this name.  (The unique
	 * index would catch this anyway, but might as well give a friendlier
	 * message.)
	 */
	if (OidIsValid(get_tablespace_oid(stmt->tablespacename)))
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("tablespace \"%s\" already exists",
						stmt->tablespacename)));

	/*
	 * Insert tuple into pg_tablespace.  The purpose of doing this first is to
	 * lock the proposed tablename against other would-be creators. The
	 * insertion will roll back if we find problems below.
	 */
	rel = heap_open(TableSpaceRelationId, RowExclusiveLock);

	pcqCtx = caql_beginscan(
			caql_addrel(cqclr(&cqc), rel),
			cql("INSERT INTO pg_tablespace",
				NULL));

	MemSet(nulls, true, sizeof(nulls));

	values[Anum_pg_tablespace_spcname - 1] =
		DirectFunctionCall1(namein, CStringGetDatum(stmt->tablespacename));
	values[Anum_pg_tablespace_spcowner - 1] =
		ObjectIdGetDatum(ownerId);
	values[Anum_pg_tablespace_spcfsoid - 1] =
		ObjectIdGetDatum(filespaceoid);
	nulls[Anum_pg_tablespace_spcname - 1] = false;
	nulls[Anum_pg_tablespace_spcowner - 1] = false;
	nulls[Anum_pg_tablespace_spcfsoid - 1] = false;

	tuple = caql_form_tuple(pcqCtx, values, nulls);

	/* Keep oids synchonized between master and segments */
	if (OidIsValid(stmt->tsoid))
		HeapTupleSetOid(tuple, stmt->tsoid);

	tablespaceoid = caql_insert(pcqCtx, tuple);
	/* and Update indexes (implicit) */

	heap_freetuple(tuple);

	/* We keep the lock on pg_tablespace until commit */
	caql_endscan(pcqCtx);
	heap_close(rel, NoLock);

	/* Create the persistent directory for the tablespace */
	tablespaceDirNode.tablespace = tablespaceoid;
	tablespaceDirNode.filespace = filespaceoid;
	MirroredFileSysObj_TransactionCreateTablespaceDir(
											&tablespaceDirNode,
											&persistentTid,
											&persistentSerialNum);

	/*
	 * Record dependency on owner
	 *
	 * We do not record the dependency on pg_filespace because we do not track
	 * dependencies between shared objects.  Additionally the pg_tablespace
	 * table itself contains the foreign key back to pg_filespace and can be
	 * used to fulfill the same purpose that an entry in pg_shdepend would.
	 */
	recordDependencyOnOwner(TableSpaceRelationId, tablespaceoid, ownerId);

	/*
	 * Create the PG_VERSION file in the target directory.	This has several
	 * purposes: to make sure we can write in the directory, to prevent
	 * someone from creating another tablespace pointing at the same directory
	 * (the emptiness check above will fail), and to label tablespace
	 * directories by PG version.
	 */
	// set_short_version(sublocation);

	if (Gp_role == GP_ROLE_DISPATCH)
	{
		stmt->tsoid = tablespaceoid;
		CdbDispatchUtilityStatement((Node *) stmt, "CreateTablespaceCommand");

		/* MPP-6929: metadata tracking */
		MetaTrackAddObject(TableSpaceRelationId,
						   tablespaceoid,
						   GetUserId(),
						   "CREATE", "TABLESPACE"
				);

	}
}

/*
 * Drop a table space
 *
 * Be careful to check that the tablespace is empty.
 */
void
RemoveTableSpace(List *names, DropBehavior behavior, bool missing_ok)
{
	char	   *tablespacename;
	Relation	rel;
	HeapTuple	tuple;
	cqContext	cqc;
	cqContext  *pcqCtx;
	Oid			tablespaceoid;
	int32		count;
	RelFileNode	relfilenode;
	DbDirNode	dbDirNode;
	PersistentFileSysState persistentState;
	ItemPointerData persistentTid;
	int64		persistentSerialNum;

	/* don't call this in a transaction block */
	// PreventTransactionChain((void *) stmt, "DROP TABLESPACE");

	/*
	 * General DROP (object) syntax allows fully qualified names, but
	 * tablespaces are global objects that do not live in schemas, so
	 * it is a syntax error if a fully qualified name was given.
	 */
	if (list_length(names) != 1)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("tablespace name may not be qualified")));
	tablespacename = strVal(linitial(names));

	/* Disallow CASCADE */
	if (behavior == DROP_CASCADE)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("syntax at or near \"cascade\"")));

	/*
	 * Find the target tuple
	 */
	rel = heap_open(TableSpaceRelationId, RowExclusiveLock);

	pcqCtx = caql_addrel(cqclr(&cqc), rel);

	tuple = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_tablespace "
				 " WHERE spcname = :1 "
				 " FOR UPDATE ",
				CStringGetDatum(tablespacename)));

	if (!HeapTupleIsValid(tuple))
	{
		/* No such tablespace, no need to hold the lock */
		heap_close(rel, RowExclusiveLock);

		if (!missing_ok)
		{
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("tablespace \"%s\" does not exist",
							tablespacename)));
		}
		else
		{
			ereport(NOTICE,
					(errmsg("tablespace \"%s\" does not exist, skipping",
							tablespacename)));
		}
		return;
	}

	tablespaceoid = HeapTupleGetOid(tuple);

	/* Must be tablespace owner */
	if (!pg_tablespace_ownercheck(tablespaceoid, GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_TABLESPACE,
					   tablespacename);

	/* Disallow drop of the standard tablespaces, even by superuser */
	if (tablespaceoid == GLOBALTABLESPACE_OID ||
		tablespaceoid == DEFAULTTABLESPACE_OID)
		aclcheck_error(ACLCHECK_NO_PRIV, ACL_KIND_TABLESPACE,
					   tablespacename);

	/*
	 * Check for any databases or relations defined in this tablespace, this
	 * is logically the same as checkSharedDependencies, however we don't
	 * actually track these in pg_shdepend, instead we lookup this information
	 * in the gp_persistent_database/relation_node tables.
	 */
	/* ... */

	/*
	 * Remove the pg_tablespace tuple (this will roll back if we fail below)
	 */
	caql_delete_current(pcqCtx);

	/*
	 * Remove any comments on this tablespace.
	 */
	DeleteSharedComments(tablespaceoid, TableSpaceRelationId);

	/*
	 * Remove dependency on owner.
	 *
	 * If shared dependencies are added between filespace <=> tablespace
	 * they will be deleted as well.
	 */
	deleteSharedDependencyRecordsFor(TableSpaceRelationId, tablespaceoid);

	/* MPP-6929: metadata tracking */
	if (Gp_role == GP_ROLE_DISPATCH)
		MetaTrackDropObject(TableSpaceRelationId,
							tablespaceoid);

	/*
	 * Acquire TablespaceCreateLock to ensure that no
	 * MirroredFileSysObj_JustInTimeDbDirCreate is running concurrently.
	 */
	LWLockAcquire(TablespaceCreateLock, LW_EXCLUSIVE);

	/*
	 * Check for any relations still defined in the tablespace.
	 */
	PersistentRelation_CheckTablespace(tablespaceoid, &count, &relfilenode);
	if (count > 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("tablespace \"%s\" is not empty", tablespacename)));
	}

	/*
	 * Schedule the removal the physical infrastructure.
	 *
	 * Note: This only schedules the delete, the delete won't actually occur
	 * until after the transaction has comitted.  This should however do
	 * everything it can to assure that the delete will occur sucessfully,
	 * e.g. check permissions etc.
	 */

    /*
	 * Schedule all persistent database directory removals for transaction commit.
	 */
    PersistentDatabase_DirIterateInit();
    while (PersistentDatabase_DirIterateNext(
                                        &dbDirNode,
                                        &persistentState,
                                        &persistentTid,
                                        &persistentSerialNum))
    {
        if (dbDirNode.tablespace != tablespaceoid)
            continue;

		/*
		 * Database directory objects can linger in 'Drop Pending' state, etc,
		 * when the mirror is down and needs drop work.  So only pay attention
		 * to 'Created' objects.
		 */
        if (persistentState != PersistentFileSysState_Created)
            continue;

        MirroredFileSysObj_ScheduleDropDbDir(
                                        &dbDirNode,
                                        &persistentTid,
                                        persistentSerialNum);
    }

	/*
	 * Now schedule the tablespace directory removal.
	 */
	MirroredFileSysObj_ScheduleDropTablespaceDir(tablespaceoid);

	/*
	 * Note: because we checked that the tablespace was empty, there should be
	 * no need to worry about flushing shared buffers or free space map
	 * entries for relations in the tablespace.
	 *
	 * CHECK THIS, also check if the lock makes any sense in this context.
	 */

	/*
	 * Allow MirroredFileSysObj_JustInTimeDbDirCreate again.
	 */
	LWLockRelease(TablespaceCreateLock);

	/* We keep the lock on the row in pg_tablespace until commit */
	heap_close(rel, NoLock);

	/* Note: no need for dispatch, that is handled in utility.c */
	return;
}

/*
 * remove_tablespace_directories: attempt to remove filesystem infrastructure
 *
 * Returns TRUE if successful, FALSE if some subdirectory is not empty
 *
 * redo indicates we are redoing a drop from XLOG; okay if nothing there
 */
static bool
remove_tablespace_directories(Oid tablespaceoid, bool redo, char *phys)
{
	char	   *location;
	DIR		   *dirdesc;
	struct dirent *de;
	char	   *subfile;
	struct stat st;
	char	*tempstr;

	location = (char *) palloc(10 + 10 + 1);
	sprintf(location, "pg_tblspc/%u", tablespaceoid);

	/*
	 * If the tablespace location has been removed previously, then we are done.
	 */
	if (stat(location, &st) < 0)
	{
		ereport(WARNING,
				(errmsg("directory linked to \"%s\" does not exist", location)
				 ));
		return true;
	}

	/*
	 * Check if the tablespace still contains any files.  We try to rmdir each
	 * per-database directory we find in it.  rmdir failure implies there are
	 * still files in that subdirectory, so give up.  (We do not have to worry
	 * about undoing any already completed rmdirs, since the next attempt to
	 * use the tablespace from that database will simply recreate the
	 * subdirectory via MirroredFileSysObj_JustInTimeDbDirCreate.)
	 *
	 * Since we hold TablespaceCreateLock, no one else should be creating any
	 * fresh subdirectories in parallel. It is possible that new files are
	 * being created within subdirectories, though, so the rmdir call could
	 * fail.  Worst consequence is a less friendly error message.
	 */
	dirdesc = AllocateDir(location);
	if (dirdesc == NULL)
	{
		if (redo && errno == ENOENT)
		{
			pfree(location);
			return true;
		}
		/* else let ReadDir report the error */
	}

	while ((de = ReadDir(dirdesc, location)) != NULL)
	{
		/* Note we ignore PG_VERSION for the nonce */
		if (strcmp(de->d_name, ".") == 0 ||
			strcmp(de->d_name, "..") == 0 ||
			strcmp(de->d_name, "PG_VERSION") == 0)
			continue;

		/* Odd... On snow leopard, we get back "/" as a subdir, which is wrong. Ingore it */
		if (de->d_name[0] == '/' && de->d_name[1] == '\0')
			continue;

		subfile = palloc(strlen(location) + 1 + strlen(de->d_name) + 1);
		sprintf(subfile, "%s/%s", location, de->d_name);

		/* This check is just to deliver a friendlier error message */
		if (!directory_is_empty(subfile))
		{
			FreeDir(dirdesc);
			return false;
		}

		/* Do the real deed */
		if (rmdir(subfile) < 0)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not remove directory \"%s\": %m",
							subfile)));

		pfree(subfile);
	}

	FreeDir(dirdesc);

	/*
	 * Okay, try to unlink PG_VERSION (we allow it to not be there, even in
	 * non-REDO case, for robustness).
	 */
	subfile = palloc(strlen(location) + 11 + 1);
	sprintf(subfile, "%s/PG_VERSION", location);

	if (unlink(subfile) < 0)
	{
		if (errno != ENOENT)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not remove file \"%s\": %m",
							subfile)));
	}

	pfree(subfile);

	/*
	 * Okay, try to remove the symlink.  We must however deal with the
	 * possibility that it's a directory instead of a symlink --- this could
	 * happen during WAL replay (see TablespaceCreateDbspace), and it is also
	 * the normal case on Windows.
	 */
	if (lstat(location, &st) == 0 && S_ISDIR(st.st_mode))
	{
		if (rmdir(location) < 0)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not remove directory \"%s\": %m",
							location)));
	}
	else
	{
		if (unlink(location) < 0)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not remove symbolic link \"%s\": %m",
							location)));
	}

	pfree(location);

	/* Now we have removed all of our linkage to the physical
	 * location; remove the per-segment location that we built at
	 * CreateTablespace() time */
 	tempstr = palloc(MAXPGPATH);

	sprintf(tempstr,"%s/seg%d",phys,Gp_segment);

	if (rmdir(tempstr) < 0)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not remove subdirectory \"%s\": %m",
						tempstr)));

	pfree(tempstr);

	return true;
}

/*
 * write out the PG_VERSION file in the specified directory. If mirror is true,
 * mirror the file creation to our segment mirror.
 *
 * XXX: API is terrible, make it cleaner
 */
void
set_short_version(const char *path, DbDirNode *dbDirNode, bool mirror)
{
	char	   *short_version;
	bool		gotdot = false;
	int			end;
	char	   *fullname;
	FILE	   *version_file;

	/* Construct short version string (should match initdb.c) */
	short_version = pstrdup(PG_VERSION);

	for (end = 0; short_version[end] != '\0'; end++)
	{
		if (short_version[end] == '.')
		{
			Assert(end != 0);
			if (gotdot)
				break;
			else
				gotdot = true;
		}
		else if (short_version[end] < '0' || short_version[end] > '9')
		{
			/* gone past digits and dots */
			break;
		}
	}
	Assert(end > 0 && short_version[end - 1] != '.' && gotdot);
	short_version[end++] = '\n';
	short_version[end] = '\0';

	if (mirror)
	{
		MirroredFlatFileOpen mirroredOpen;

		Insist(!PointerIsValid(path));
		Insist(PointerIsValid(dbDirNode));

		MirroredFlatFile_OpenInDbDir(&mirroredOpen, dbDirNode, "PG_VERSION",
							O_CREAT | O_WRONLY | PG_BINARY, S_IRUSR | S_IWUSR,
							/* suppressError */ false);

		MirroredFlatFile_Append(&mirroredOpen, short_version,
								end,
								/* suppressError */ false);

		MirroredFlatFile_Flush(&mirroredOpen, /* suppressError */ false);
		MirroredFlatFile_Close(&mirroredOpen);
	}
	else
	{
		Insist(!PointerIsValid(dbDirNode));
		Insist(PointerIsValid(path));

		/* Now write the file */
		fullname = palloc(strlen(path) + 11 + 1);
		sprintf(fullname, "%s/PG_VERSION", path);
		version_file = AllocateFile(fullname, PG_BINARY_W);
		if (version_file == NULL)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not write to file \"%s\": %m",
							fullname)));
		fprintf(version_file, "%s", short_version);
		if (FreeFile(version_file))
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not write to file \"%s\": %m",
							fullname)));

		pfree(fullname);
	}
	pfree(short_version);
}

/*
 * Check if a directory is empty.
 *
 * This probably belongs somewhere else, but not sure where...
 */
bool
directory_is_empty(const char *path)
{
	DIR		   *dirdesc;
	struct dirent *de;

	dirdesc = AllocateDir(path);

	while ((de = ReadDir(dirdesc, path)) != NULL)
	{
		if (strcmp(de->d_name, ".") == 0 ||
			strcmp(de->d_name, "..") == 0)
			continue;
		/* Odd... On snow leopard, we get back "/" as a subdir, which is wrong. Ingore it */
		if (de->d_name[0] == '/' && de->d_name[1] == '\0')
			continue;
		FreeDir(dirdesc);
		return false;
	}

	FreeDir(dirdesc);
	return true;
}

/*
 * Rename a tablespace
 */
void
RenameTableSpace(const char *oldname, const char *newname)
{
	Relation	rel;
	Oid			tablespaceoid;
	cqContext	cqc;
	cqContext	cqc2;
	cqContext  *pcqCtx;
	HeapTuple	newtuple;
	Form_pg_tablespace newform;

	/* Search pg_tablespace */
	rel = heap_open(TableSpaceRelationId, RowExclusiveLock);

	pcqCtx = caql_addrel(cqclr(&cqc), rel);

	newtuple = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_tablespace "
				" WHERE spcname = :1 "
				" FOR UPDATE ",
				CStringGetDatum(oldname)));

	if (!HeapTupleIsValid(newtuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("tablespace \"%s\" does not exist",
						oldname)));

	newform = (Form_pg_tablespace) GETSTRUCT(newtuple);

	/* Must be owner */
	tablespaceoid = HeapTupleGetOid(newtuple);
	if (!pg_tablespace_ownercheck(tablespaceoid, GetUserId()))
		aclcheck_error(ACLCHECK_NO_PRIV, ACL_KIND_TABLESPACE, oldname);

	/* Validate new name */
	if (!allowSystemTableModsDDL && IsReservedName(newname))
	{
		ereport(ERROR,
				(errcode(ERRCODE_RESERVED_NAME),
				 errmsg("unacceptable tablespace name \"%s\"", newname),
				 errdetail("The prefix \"%s\" is reserved for system tablespaces.",
						   GetReservedPrefix(newname))));
	}

	/* Make sure the new name doesn't exist */
	if (caql_getcount(
				caql_addrel(cqclr(&cqc2), rel), /* rely on rowexclusive */
				cql("SELECT COUNT(*) FROM pg_tablespace "
					" WHERE spcname = :1 ",
					CStringGetDatum(newname))))
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("tablespace \"%s\" already exists",
						newname)));

	/* OK, update the entry */
	namestrcpy(&(newform->spcname), newname);

	caql_update_current(pcqCtx, newtuple);
	/* and Update indexes (implicit) */

	/* MPP-6929: metadata tracking */
	if (Gp_role == GP_ROLE_DISPATCH)
		MetaTrackUpdObject(TableSpaceRelationId,
						   tablespaceoid,
						   GetUserId(),
						   "ALTER", "RENAME"
				);

	heap_close(rel, NoLock);
}

/*
 * Change tablespace owner
 */
void
AlterTableSpaceOwner(const char *name, Oid newOwnerId)
{
	Relation	rel;
	Oid			tablespaceoid;
	cqContext	cqc;
	cqContext  *pcqCtx;
	Form_pg_tablespace spcForm;
	HeapTuple	tup;

	/* Search pg_tablespace */
	rel = heap_open(TableSpaceRelationId, RowExclusiveLock);

	pcqCtx = caql_addrel(cqclr(&cqc), rel);

	tup = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_tablespace "
				" WHERE spcname = :1 "
				" FOR UPDATE ",
				CStringGetDatum(name)));

	if (!HeapTupleIsValid(tup))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("tablespace \"%s\" does not exist", name)));

	tablespaceoid = HeapTupleGetOid(tup);
	spcForm = (Form_pg_tablespace) GETSTRUCT(tup);

	/*
	 * If the new owner is the same as the existing owner, consider the
	 * command to have succeeded.  This is for dump restoration purposes.
	 */
	if (spcForm->spcowner != newOwnerId)
	{
		Datum		repl_val[Natts_pg_tablespace];
		bool		repl_null[Natts_pg_tablespace];
		bool		repl_repl[Natts_pg_tablespace];
		Acl		   *newAcl;
		Datum		aclDatum;
		bool		isNull;
		HeapTuple	newtuple;

		/* Otherwise, must be owner of the existing object */
		if (!pg_tablespace_ownercheck(tablespaceoid, GetUserId()))
			aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_TABLESPACE,
						   name);

		/* Must be able to become new owner */
		check_is_member_of_role(GetUserId(), newOwnerId);

		/*
		 * Normally we would also check for create permissions here, but there
		 * are none for tablespaces so we follow what rename tablespace does
		 * and omit the create permissions check.
		 *
		 * NOTE: Only superusers may create tablespaces to begin with and so
		 * initially only a superuser would be able to change its ownership
		 * anyway.
		 */

		memset(repl_null, false, sizeof(repl_null));
		memset(repl_repl, false, sizeof(repl_repl));

		repl_repl[Anum_pg_tablespace_spcowner - 1] = true;
		repl_val[Anum_pg_tablespace_spcowner - 1] = ObjectIdGetDatum(newOwnerId);

		/*
		 * Determine the modified ACL for the new owner.  This is only
		 * necessary when the ACL is non-null.
		 */
		aclDatum = heap_getattr(tup,
								Anum_pg_tablespace_spcacl,
								RelationGetDescr(rel),
								&isNull);
		if (!isNull)
		{
			newAcl = aclnewowner(DatumGetAclP(aclDatum),
								 spcForm->spcowner, newOwnerId);
			repl_repl[Anum_pg_tablespace_spcacl - 1] = true;
			repl_val[Anum_pg_tablespace_spcacl - 1] = PointerGetDatum(newAcl);
		}

		newtuple = caql_modify_current(pcqCtx, repl_val, repl_null, repl_repl);

		caql_update_current(pcqCtx, newtuple);
		/* and Update indexes (implicit) */

		/* MPP-6929: metadata tracking */
		if (Gp_role == GP_ROLE_DISPATCH)
			MetaTrackUpdObject(TableSpaceRelationId,
							   tablespaceoid,
							   GetUserId(),
							   "ALTER", "OWNER"
					);

		heap_freetuple(newtuple);

		/* Update owner dependency reference */
		changeDependencyOnOwner(TableSpaceRelationId, HeapTupleGetOid(tup),
								newOwnerId);
	}

	heap_close(rel, NoLock);
}


/*
 * Routines for handling the GUC variable 'default_tablespace'.
 */

/* assign_hook: validate new default_tablespace, do extra actions as needed */
const char *
assign_default_tablespace(const char *newval, bool doit, GucSource source)
{
	/*
	 * If we aren't inside a transaction, we cannot do database access so
	 * cannot verify the name.	Must accept the value on faith.
	 */
	if (IsTransactionState())
	{
		if (newval[0] != '\0' &&
			!OidIsValid(get_tablespace_oid(newval)))
		{
			if (source >= PGC_S_INTERACTIVE)
				ereport(ERROR,
						(errcode(ERRCODE_UNDEFINED_OBJECT),
						 errmsg("tablespace \"%s\" does not exist",
								newval)));
			return NULL;
		}
	}

	return newval;
}

/*
 * GetDefaultTablespace -- get the OID of the current default tablespace
 *
 * May return InvalidOid to indicate "use the database's default tablespace"
 *
 * This exists to hide (and possibly optimize the use of) the
 * default_tablespace GUC variable.
 */
Oid
GetDefaultTablespace(void)
{
	Oid			result;

	/* Fast path for default_tablespace == "" */
	if (default_tablespace == NULL || default_tablespace[0] == '\0')
		return InvalidOid;

	/*
	 * It is tempting to cache this lookup for more speed, but then we would
	 * fail to detect the case where the tablespace was dropped since the GUC
	 * variable was set.  Note also that we don't complain if the value fails
	 * to refer to an existing tablespace; we just silently return InvalidOid,
	 * causing the new object to be created in the database's tablespace.
	 */
	result = get_tablespace_oid(default_tablespace);

	/*
	 * Allow explicit specification of database's default tablespace in
	 * default_tablespace without triggering permissions checks.
	 */
	if (result == MyDatabaseTableSpace)
		result = InvalidOid;
	return result;
}


/*
 * get_tablespace_oid - given a tablespace name, look up the OID
 *
 * Returns InvalidOid if tablespace name not found.
 */
Oid
get_tablespace_oid(const char *tablespacename)
{
	Oid			tsoid;
	Relation	rel;
	HeapTuple	tuple;
	cqContext	cqc;

	/*
	 * Search pg_tablespace.  We use a heapscan here even though there is an
	 * index on name, on the theory that pg_tablespace will usually have just
	 * a few entries and so an indexed lookup is a waste of effort.
	 */
	rel = heap_open(TableSpaceRelationId, AccessShareLock);

	tuple = caql_getfirst(
			caql_addrel(cqclr(&cqc), rel),
			cql("SELECT * FROM pg_tablespace "
				" WHERE spcname = :1 ",
				CStringGetDatum(tablespacename)));

	/* If nothing matches then the tablespace doesn't exist */
	if (HeapTupleIsValid(tuple))
		tsoid = HeapTupleGetOid(tuple);
	else
		tsoid = InvalidOid;

	/*
	 * Anything that needs to lookup a tablespace name must need a lock
	 * on the tablespace for the duration of its transaction, otherwise
	 * there is nothing preventing it from being dropped.
	 */
	if (OidIsValid(tsoid))
	{
		Buffer			buffer = InvalidBuffer;
		HTSU_Result		lockTest;
		ItemPointerData	update_ctid;
		TransactionId	update_xmax;

		/*
		 * Unfortunately locking of objects other than relations doesn't
		 * really work, the work around is to lock the tuple in pg_tablespace
		 * to prevent drops from getting the exclusive lock they need.
		 */
		lockTest = heap_lock_tuple(rel, tuple, &buffer,
								   &update_ctid, &update_xmax,
								   GetCurrentCommandId(),
								   LockTupleShared, LockTupleWait);
		ReleaseBuffer(buffer);
		switch (lockTest)
		{
			case HeapTupleMayBeUpdated:
				break;  /* Got the Lock */

			case HeapTupleSelfUpdated:
				Assert(false); /* Shouldn't ever occur */
				/* fallthrough */

			case HeapTupleBeingUpdated:
				Assert(false);  /* Not possible with LockTupleWait */
				/* fallthrough */

			case HeapTupleUpdated:
				ereport(ERROR,
						(errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
						 errmsg("could not serialize access to tablespace %s due to concurrent update",
								tablespacename)));

			default:
				elog(ERROR, "unrecognized heap_lock_tuple_status: %u", lockTest);
		}
	}

	heap_close(rel, AccessShareLock);

	return tsoid;
}

/*
 * get_tablespace_name - given a tablespace OID, look up the name
 *
 * Returns a palloc'd string, or NULL if no such tablespace.
 */
char *
get_tablespace_name(Oid spc_oid)
{
	char	   *result;

	/*
	 * Search pg_tablespace.  We use a heapscan here even though there is an
	 * index on oid, on the theory that pg_tablespace will usually have just a
	 * few entries and so an indexed lookup is a waste of effort.
	 */
	result = caql_getcstring(
			NULL,
			cql("SELECT spcname FROM pg_tablespace "
				" WHERE oid = :1 ",
				ObjectIdGetDatum(spc_oid)));

	/* We assume that there can be at most one matching tuple */
	return result;
}


/*
 * TABLESPACE resource manager's routines
 */
void
tblspc_redo(XLogRecPtr beginLoc, XLogRecPtr lsn, XLogRecord *record)
{
	uint8		info = record->xl_info & ~XLR_INFO_MASK;

	if (info == XLOG_TBLSPC_CREATE)
	{
		xl_tblspc_create_rec *xlrec = (xl_tblspc_create_rec *) XLogRecGetData(record);
		char	   *location = xlrec->ts_path;
		char	   *linkloc;
		char	   *sublocation;
		struct stat	st;

		/*
		 * MPP-2333: Try to create the target directory if it does not exist.
		 */
		if (stat(location, &st) < 0)
		{
			if (mkdir(location, 0700) != 0)
			{
				ereport(ERROR,
						(errcode_for_file_access(),
					 errmsg("could not create location directory \"%s\": %m",
							location)));
			}
		}

		/*
		 * Attempt to coerce target directory to safe permissions.	If this
		 * fails, it doesn't exist or has the wrong owner.
		 */
		if (chmod(location, 0700) != 0)
			ereport(ERROR,
					(errcode_for_file_access(),
				  errmsg("could not set permissions on directory \"%s\": %m",
						 location)));

		/* Create segment subdirectory. */
	 	sublocation = palloc(MAXPGPATH);

		sprintf(sublocation,"%s/seg%d",location,Gp_segment);

		if (mkdir(sublocation, 0700) != 0)
			ereport(ERROR,
					(errcode_for_file_access(),
					 errmsg("could not create subdirectory \"%s\": %m",
							sublocation)));

		/* Create or re-create the PG_VERSION file in the target directory */
		set_short_version(sublocation, NULL, false);

		/* Create the symlink if not already present */
		linkloc = (char *) palloc(10 + 10 + 1);
		sprintf(linkloc, "pg_tblspc/%u", xlrec->ts_id);

		if (symlink(sublocation, linkloc) < 0)
		{
			if (errno != EEXIST)
				ereport(ERROR,
						(errcode_for_file_access(),
						 errmsg("could not create symbolic link \"%s\": %m",
								linkloc)));
		}

		pfree(sublocation);
		pfree(linkloc);
	}
	else if (info == XLOG_TBLSPC_DROP)
	{
		xl_tblspc_drop_rec *xlrec = (xl_tblspc_drop_rec *) XLogRecGetData(record);

		if (!remove_tablespace_directories(xlrec->ts_id, true, xlrec->ts_path))
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("tablespace %u is not empty",
							xlrec->ts_id)));
	}
	else
		elog(PANIC, "tblspc_redo: unknown op code %u", info);
}

void
tblspc_desc(StringInfo buf, XLogRecPtr beginLoc, XLogRecord *record)
{
	uint8		info = record->xl_info & ~XLR_INFO_MASK;
	char		*rec = XLogRecGetData(record);

	if (info == XLOG_TBLSPC_CREATE)
	{
		xl_tblspc_create_rec *xlrec = (xl_tblspc_create_rec *) rec;

		appendStringInfo(buf, "create ts: %u \"%s\"",
						 xlrec->ts_id, xlrec->ts_path);
	}
	else if (info == XLOG_TBLSPC_DROP)
	{
		xl_tblspc_drop_rec *xlrec = (xl_tblspc_drop_rec *) rec;

		appendStringInfo(buf, "drop ts: %u", xlrec->ts_id);
	}
	else
		appendStringInfo(buf, "UNKNOWN");
}
