/*-------------------------------------------------------------------------
 *
 * assert.c
 *	  Assert code.
 *
 * Portions Copyright (c) 2005-2009, Greenplum inc
 * Portions Copyright (c) 1996-2009, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/utils/error/assert.c,v 1.31 2005/10/15 02:49:32 momjian Exp $
 *
 * NOTE
 *	  This should eventually work with elog()
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "libpq/pqsignal.h"
#include "cdb/cdbvars.h"                /* currentSliceId */

#include <unistd.h>

/*
 * ExceptionalCondition - Handles the failure of an Assert()
 *
 * Note: this can't actually return, but we declare it as returning int
 * because the TrapMacro() macro might get wonky otherwise.
 */
int
ExceptionalCondition(const char *conditionName,
					 const char *errorType,
					 const char *fileName,
					 int lineNumber)
{
    /* CDB: Try to tell the QD or client what happened. */
    if (errstart(FATAL, fileName, lineNumber, NULL,TEXTDOMAIN))
    {
		if (!PointerIsValid(conditionName)
			|| !PointerIsValid(fileName)
			|| !PointerIsValid(errorType))
			errfinish(errcode(ERRCODE_INTERNAL_ERROR),
					  errFatalReturn(gp_reraise_signal),
					  errmsg("TRAP: ExceptionalCondition: bad arguments"));
		else
			errfinish(errcode(ERRCODE_INTERNAL_ERROR),
					  errFatalReturn(gp_reraise_signal),
					  errmsg("Unexpected internal error"),
					  errdetail("%s(\"%s\", File: \"%s\", Line: %d)\n",
								errorType, conditionName, fileName, lineNumber)
				);
				
		/* Usually this shouldn't be needed, but make sure the msg went out */
		fflush(stderr);
	}

	abort();
	return 0;
}
