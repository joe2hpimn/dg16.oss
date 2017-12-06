/*--------------------------------------------------------------------------
*
* cdbcopy.h
*	 Definitions and API functions for cdbcopy.c
*	 These are functions that are used by the backend
*	 COPY command in Greenplum Database.
*
* Copyright (c) 2005-2008, Greenplum inc
*
*--------------------------------------------------------------------------
*/
#ifndef CDBCOPY_H
#define CDBCOPY_H

#include "access/aosegfiles.h" /* for InvalidFileSegNumber const */ 
#include "lib/stringinfo.h"
#include "cdb/cdbgang.h"

#define COPYOUT_CHUNK_SIZE 16 * 1024

typedef enum SegDbState
{
	/*
	 * (it is best to avoid names like OUT that are likely to be #define'd or
	 * typedef'd in some platform-dependent runtime library header file)
	 */
	SEGDB_OUT,					/* Not participating in COPY (invalid etc...) */
	SEGDB_IDLE,					/* Participating but COPY not yet started */
	SEGDB_COPY,					/* COPY in progress */
	SEGDB_DONE					/* COPY completed (with or without errors) */
}	SegDbState;

typedef struct CdbCopy
{
	Gang	   *primary_writer;
	int			total_segs;		/* total number of segments in cdb */
	int		   *mirror_map;		/* indicates how many db's each segment has */
	bool		copy_in;		/* direction: true for COPY FROM false for COPY TO */
	bool		remote_data_err;/* data error occurred on a remote COPY session */
	bool		io_errors;		/* true if any I/O error occurred trying to
								 * communicate with segDB's */
	bool		skip_ext_partition;/* skip external partition */ 

	SegDbState		**segdb_state;
	
	StringInfoData	err_msg;		/* error message for cdbcopy operations */
	StringInfoData  err_context; /* error context from QE error */
	StringInfoData	copy_out_buf;/* holds a chunk of data from the database */
		
	List			*outseglist;    /* segs that currently take part in copy out. 
									 * Once a segment gave away all it's data rows
									 * it is taken out of the list */
	PartitionNode *partitions;
	List		  *ao_segnos;
	HTAB		  *aotupcounts; /* hash of ao relation id to processed tuple count */
} CdbCopy;



/* global function declarations */
CdbCopy    *makeCdbCopy(bool copy_in);
int			cdbCopyGetDbCount(int total_segs, int seg);
void		cdbCopyStart(CdbCopy *cdbCopy, char *copyCmd);
void		cdbCopySendData(CdbCopy *c, int target_seg, const char *buffer, int nbytes);
void		cdbCopySendDataSingle(CdbCopy *c, int target_seg, const char *buffer, int nbytes);
bool		cdbCopyGetData(CdbCopy *c, bool cancel, uint64 *rows_processed);
int			cdbCopyEnd(CdbCopy *c);
void		cdbCopyStartTransaction(void);

#endif   /* CDBCOPY_H */
