/*-------------------------------------------------------------------------
 *
 * pg_filespace.h
 *	  definition of the system "filespace" relation (pg_filespace)
 *	  along with the relation's initial contents.
 *
 *
 * Copyright (c) 2009-2010, Greenplum Inc
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $PostgreSQL$
 *
 * NOTES
 *	  the genbki.sh script reads this file and generates .bki
 *	  information from the DATA() statements.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_FILESPACE_H
#define PG_FILESPACE_H

#include "catalog/genbki.h"

/* ----------------
 *		postgres.h contains the system type definitions and the
 *		CATALOG(), BKI_BOOTSTRAP and DATA() sugar words so this file
 *		can be read by both genbki.sh and the C compiler.
 * ----------------
 */

/* TIDYCAT_BEGINFAKEDEF

   CREATE TABLE pg_filespace
   with (camelcase=FileSpace, shared=true, relid=5009, reltype_oid=6438)
   (
   fsname name, -- filespace name
   fsowner oid -- owner of filespace
   );

   create unique index on pg_filespace(oid) with (indexid=2858, CamelCase=FilespaceOid);
   create unique index on pg_filespace(fsname) with (indexid=2859, CamelCase=FilespaceName);


   TIDYCAT_ENDFAKEDEF
*/


/* ----------------
 *		pg_filespace definition.  cpp turns this into
 *		typedef struct FormData_pg_filespace
 * ----------------
 */
#define FileSpaceRelationId  5009

CATALOG(pg_filespace,5009) BKI_SHARED_RELATION
{
	NameData	fsname;		/* filespace name */
	Oid			fsowner;	/* owner of filespace */
} FormData_pg_filespace;

/* ----------------
 *		Form_pg_filespace corresponds to a pointer to a tuple with
 *		the format of pg_tablespace relation.
 * ----------------
 */
typedef FormData_pg_filespace *Form_pg_filespace;

/* ----------------
 *		compiler constants for pg_filespace
 * ----------------
 */

#define Natts_pg_filespace				2
#define Anum_pg_filespace_fsname		1
#define Anum_pg_filespace_fsowner		2

DATA(insert OID = 3052 ( pg_system PGUID ));
SHDESCR("System catalog filespace");

#define SYSTEMFILESPACE_OID 3052
#define SYSTEMFILESPACE_NAME "pg_system"

#endif   /* PG_FILESPACE_H */
