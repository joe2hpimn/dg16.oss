/*-------------------------------------------------------------------------
 *
 * cdb_dump_util.c
 *
 *
 * Portions Copyright (c) 1996-2003, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

#include "postgres_fe.h"
#include "libpq-fe.h"
#include <time.h>
#include <ctype.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include "pqexpbuffer.h"
#include "cdb_dump_util.h"

#define DDP_CL_DDP 1

static char predump_errmsg[1024];

bool shouldDumpSchemaOnly(int g_role, bool incrementalBackup, void *list) {
    if (g_role != ROLE_SEGDB || !incrementalBackup)
        return false;    

    if (list)
        return false;
    else
        return true;
}


/*
 * DoCancelNotifyListen: This function executes a LISTEN or a NOTIFY command, with
 * name in the format N%s_%d_%d, where the %s is replaced by the CDBDumpKey,
 * and the 2 integers are the contentid and dbid.
 */
void
DoCancelNotifyListen(PGconn *pConn,
					 bool bListen,
					 const char *pszCDBDumpKey,
					 int role_id,
					 int db_id,
					 int target_db_id,
					 const char *pszSuffix)
{
	PGresult   *pRes;
	PQExpBuffer q = createPQExpBuffer();
	char	   *pszCmd = bListen ? "LISTEN" : "NOTIFY";

	appendPQExpBuffer(q, "%s N%s_%d_%d",
					  pszCmd, pszCDBDumpKey, role_id, db_id);

	/* this is valid only for restore operations */
	if (target_db_id != -1)
		appendPQExpBuffer(q, "_T%d", target_db_id);

	if (pszSuffix != NULL)
		appendPQExpBuffer(q, "_%s", pszSuffix);

	pRes = PQexec(pConn, q->data);
	if (pRes == NULL || PQresultStatus(pRes) != PGRES_COMMAND_OK)
	{
		mpp_err_msg_cache("%s command failed for for backup key %s, instid %d, segid %d failed : %s",
			   pszCmd, pszCDBDumpKey, role_id, db_id, PQerrorMessage(pConn));
	}

	PQclear(pRes);
	destroyPQExpBuffer(q);
}

/*
 * FreeInputOptions: This function frees all the memory allocated for the fields
 * in an InputOptions struct, but not the pointer to the struct itself
 */
void
FreeInputOptions(InputOptions * pInputOpts)
{
	if (pInputOpts->pszDBName != NULL)
		free(pInputOpts->pszDBName);

	if (pInputOpts->pszPGHost != NULL)
		free(pInputOpts->pszPGHost);

	if (pInputOpts->pszPGPort != NULL)
		free(pInputOpts->pszPGPort);

	if (pInputOpts->pszUserName != NULL)
		free(pInputOpts->pszUserName);

	if (pInputOpts->pszBackupDirectory != NULL)
		free(pInputOpts->pszBackupDirectory);

	if (pInputOpts->pszReportDirectory != NULL)
		free(pInputOpts->pszReportDirectory);

    /* hard coded as gzip for now, no need to free
	if ( pInputOpts->pszCompressionProgram != NULL )
		free( pInputOpts->pszCompressionProgram ); 
	*/

	if (pInputOpts->pszPassThroughParms != NULL)
		free(pInputOpts->pszPassThroughParms);

	if (pInputOpts->pszCmdLineParms != NULL)
		free(pInputOpts->pszCmdLineParms);

	if (pInputOpts->pszKey != NULL)
		free(pInputOpts->pszKey);

	if (pInputOpts->pszMasterDBName != NULL)
		free(pInputOpts->pszMasterDBName);

	/* FreeCDBSet( &pInputOpts->set ); */
}

/*
 * GetMatchInt: This function parses a pMatch objects's result as an integer.
 */
int
GetMatchInt(regmatch_t *pMatch, char *pszInput)
{
	int			start = pMatch->rm_so;
	int			end = pMatch->rm_eo;
	char		save;
	int			rtn;

	assert(end >= start);

	save = pszInput[end];
	pszInput[end] = '\0';
	rtn = atoi(pszInput + start);
	pszInput[end] = save;

	return rtn;
}

/*
 * GetMatchString: This function parses a pMatch objects's result as a string.
 * It allocates memory for the string.	This should be freed by the caller.
 */
char *
GetMatchString(regmatch_t *pMatch, char *pszInput)
{
	int			start = pMatch->rm_so;
	int			end = pMatch->rm_eo;
	char		save;
	char	   *pszRtn;

	assert(end >= start);
	pszRtn = (char *) malloc(end - start + 1);
	if (pszRtn == NULL)
		return NULL;

	save = pszInput[end];
	pszInput[end] = '\0';
	strcpy(pszRtn, pszInput + start);
	pszInput[end] = save;

	return pszRtn;
}

bool
shouldExpandChildren(bool g_gp_supportsPartitioning, bool no_expand_children)
{
    return (g_gp_supportsPartitioning && !no_expand_children);
}

/*
 * isFilteringAllowed: This method checks if we should filter out tables based on 
 *                     whether we are using incremental mode for backup and if 
 *                     we are on the master
 *  Arguments:
 *           role - The role of the segment E.g ROLE_MASTER, NON_MASTER etc
 *           incremental - a bool which is true if we are in incremental backup mode
 *                                         false otherwise
 *
 *   Returns: true if filtering is allowed in this context and false otherwise
 *
 */
bool
isFilteringAllowedNow(int role, bool incrementalBackup, char *incrementalFilter) 
{
	if (!incrementalBackup)
		return true;

	if (role != ROLE_MASTER)
		return true;

	if (role == ROLE_MASTER && incrementalFilter != NULL)
		return true;

	return false;
}


/*
 * GetTimeNow: This function formats a string with the current local time
 *	formatted as YYYYMMDD:HH:MM:SS.
 *
 *	Arguments:
 *		szTimeNow - pointer to a character array of at least 18 bytes
 *				(inclusive of a terminating NUL).
 *
 *	Returns:
 *		szTimeNow
 */
char *
GetTimeNow(char *szTimeNow)
{
	struct tm	pNow;
	time_t		tNow = time(NULL);
	char	   *format = "%Y%m%d:%H:%M:%S";

	localtime_r(&tNow, &pNow);
	strftime(szTimeNow, 18, format, &pNow);

	return szTimeNow;
}

/*
 * GetMasterConnection: This function makes a database connection with the given parameters.
 * The connection handle is returned.
 * An interactive password prompt is automatically issued if required.
 * This is a copy of the one in pg_dump.
 */
PGconn *
GetMasterConnection(const char *progName,
					const char *dbname,
					const char *pghost,
					const char *pgport,
					const char *username,
					int reqPwd,
					int ignoreVersion,
					bool bDispatch)
{
	char	   *pszPassword = NULL;
	bool		need_pass = false;
	PGconn	   *pConn = NULL;
	SegmentDatabase masterDB;

	if (reqPwd)
	{
		pszPassword = simple_prompt("Password: ", 100, false);
		if (pszPassword == NULL)
		{
			mpp_err_msg_cache("ERROR", progName, "out of memory when allocating password");
			return NULL;
		}
	}

	masterDB.dbid = 0;
	masterDB.role = 0;
	masterDB.port = pgport ? atoi(pgport) : 5432;
	masterDB.pszHost = (char *) pghost;
	masterDB.pszDBName = (char *) dbname;
	masterDB.pszDBUser = (char *) username;
	masterDB.pszDBPswd = pszPassword;

	/*
	 * Start the connection.  Loop until we have a password if requested by
	 * backend.
	 */
	do
	{
		need_pass = false;
		pConn = MakeDBConnection(&masterDB, bDispatch);

		if (pConn == NULL)
		{
			mpp_err_msg_cache("ERROR", progName, "failed to connect to database");
			return (NULL);
		}

		if (PQstatus(pConn) == CONNECTION_BAD &&
			strcmp(PQerrorMessage(pConn), "fe_sendauth: no password supplied\n") == 0 &&
			!feof(stdin))
		{
			PQfinish(pConn);
			need_pass = true;
			free(pszPassword);
			pszPassword = NULL;
			pszPassword = simple_prompt("Password: ", 100, false);
			masterDB.pszDBPswd = pszPassword;
		}
	} while (need_pass);

	if (pszPassword)
		free(pszPassword);

	/* check to see that the backend connection was successfully made */
	if (PQstatus(pConn) == CONNECTION_BAD)
	{
		mpp_err_msg_cache("ERROR", progName, "connection to database \"%s\" failed : %s",
						  PQdb(pConn), PQerrorMessage(pConn));
		return (NULL);
	}

	return pConn;
}

/*
 * MakeDBConnection: This function creates a connection string based on
 * fields in the SegmentDatabase parameter and then connects to the database.
 * The PGconn* is returned.  Y=This must be checked by the calling
 * routine for errors etc.
 */
PGconn *
MakeDBConnection(const SegmentDatabase *pSegDB, bool bDispatch)
{
	char	   *pszOptions;
	char	   *pszHost;
	char	   *pszDBName;
	char	   *pszUser;
	char	   *pszDBPswd;
	char	   *pszConnInfo;
	PGconn	   *pConn;


	if (bDispatch)
		pszOptions = NULL;
	else
		pszOptions = MakeString("options='-c gp_session_role=UTILITY'");

	if (pSegDB->pszHost == NULL || *pSegDB->pszHost == '\0')
		pszHost = strdup("host=''");
	else
		pszHost = MakeString("host=%s", pSegDB->pszHost);

	if (pSegDB->pszDBName != NULL && *pSegDB->pszDBName != '\0')
		pszDBName = MakeString("dbname=%s", pSegDB->pszDBName);
	else
		pszDBName = NULL;

	if (pSegDB->pszDBUser != NULL && *pSegDB->pszDBUser != '\0')
		pszUser = MakeString("user=%s", pSegDB->pszDBUser);
	else
		pszUser = NULL;

	if (pSegDB->pszDBPswd != NULL && *pSegDB->pszDBPswd != '\0')
		pszDBPswd = MakeString("password='%s'", pSegDB->pszDBPswd);
	else
		pszDBPswd = NULL;

	pszConnInfo = MakeString("%s %s port=%u %s %s %s",
							 StringNotNull(pszOptions, ""),
							 pszHost,
							 pSegDB->port,
							 StringNotNull(pszDBName, ""),
							 StringNotNull(pszUser, ""),
							 StringNotNull(pszDBPswd, ""));

	pConn = PQconnectdb(pszConnInfo);

	if (pszOptions != NULL)
		free(pszOptions);
	if (pszHost != NULL)
		free(pszHost);
	if (pszDBName != NULL)
		free(pszDBName);
	if (pszUser != NULL)
		free(pszUser);
	if (pszDBPswd != NULL)
		free(pszDBPswd);
	if (pszConnInfo != NULL)
		free(pszConnInfo);

	return pConn;
}

/*
 * MakeString: This function allocates memory for and formats a char*
 * with a format string and variable argument list.
 * It uses a PQExpBuffer and is based on the code for appendPQExpBuffer
 * Can't use that directly in the implementation because I want this to have variable args.
 */
char *
MakeString(const char *fmt,...)
{
	size_t		nBytes = 128;
	char	   *pszNew;

	char	   *pszRtn = (char *) malloc(nBytes);

	if (pszRtn == NULL)
		return NULL;

	while (true)
	{
		/*
		 * Try to format the given string into the available space;
		 */
		va_list		args;
		int			nprinted;

		va_start(args, fmt);
		nprinted = vsnprintf(pszRtn, nBytes, fmt, args);
		va_end(args);

		/*
		 * Note: some versions of vsnprintf return the number of chars
		 * actually stored, but at least one returns -1 on failure. Be
		 * conservative about believing whether the print worked.
		 */
		if (nprinted >= 0 && nprinted < (int) nBytes)
		{
			/* Success.  Note nprinted does not include trailing null. */
			break;
		}

		nBytes *= 2;
		pszNew = (char *) realloc(pszRtn, nBytes);
		if (pszNew == NULL)
		{
			free(pszRtn);
			pszRtn = NULL;
			break;
		}

		pszRtn = pszNew;

	}

	return pszRtn;
}

/*
 * ParseCDBDumpInfo: This function takes the command line parameter and parses it
 * into its 4 pieces: the dump key, the contextid, the dbid, and the CDBPassThroughCredentials
 * based on the format convention key_contextid_dbid_credentials
 */
bool
ParseCDBDumpInfo(const char *progName, char *pszCDBDumpInfo, char **ppCDBDumpKey, int *pContentID, int *pDbID, char **ppCDBPassThroughCredentials)
{
	int			rtn;

	regmatch_t	matches[5];

	regex_t		rCDBDumpInfo;

	if (0 != regcomp(&rCDBDumpInfo, "([0-9]+)_([0-9]+)_([0-9]+)_([^[:space:]]*)", REG_EXTENDED))
	{
		mpp_err_msg_cache("ERROR", progName, "Error compiling regular expression for parsing CDB Dump Info\n");
		return false;
	}

	assert(rCDBDumpInfo.re_nsub == 4);

	/* match the pszCDBDumpInfo against the regex. */
	rtn = regexec(&rCDBDumpInfo, pszCDBDumpInfo, 5, matches, 0);
	if (rtn != 0)
	{
		char		errbuf[1024];

		regerror(rtn, &rCDBDumpInfo, errbuf, 1024);
		mpp_err_msg_cache("Error parsing CDBDumpInfo %s: not valid : %s\n", pszCDBDumpInfo, errbuf);
		regfree(&rCDBDumpInfo);
		return false;
	}

	regfree(&rCDBDumpInfo);

	*ppCDBDumpKey = GetMatchString(&matches[1], pszCDBDumpInfo);
	if (*ppCDBDumpKey == NULL)
	{
		mpp_err_msg_cache("ERROR", progName, "Error parsing CDBDumpInfo %s: CDBDumpKey not valid\n", pszCDBDumpInfo);
		return false;
	}

	*pContentID = GetMatchInt(&matches[2], pszCDBDumpInfo);

	*pDbID = GetMatchInt(&matches[3], pszCDBDumpInfo);

	*ppCDBPassThroughCredentials = GetMatchString(&matches[4], pszCDBDumpInfo);
	if (*ppCDBPassThroughCredentials == NULL)
	{
		mpp_err_msg_cache("ERROR", progName, "Error parsing CDBDumpInfo %s: CDBDumpKey not valid\n", pszCDBDumpInfo);
		return false;
	}
	return true;
}

/*
 * ReadBackendBackupFile: This function calls the backend function gp_read_backup_file
 * which reads the contents out of the appropriate file on the database server.
 * If the call fails, it returns NULL.	The returned pointer must be freed by the caller.
 */
char *
ReadBackendBackupFile(PGconn *pConn, const char *pszBackupDirectory, const char *pszKey, BackupFileType fileType, const char *progName)
{
	char	   *pszRtn = NULL;
	char	   *pszFileType;
	PQExpBuffer Qry;
	PGresult   *pRes;

	switch (fileType)
	{
		case BFT_BACKUP:
			pszFileType = "0";
			break;
		case BFT_BACKUP_STATUS:
			pszFileType = "1";
			break;
		case BFT_RESTORE_STATUS:
			pszFileType = "2";
			break;
		default:
			mpp_err_msg("ERROR", progName, "Unknown file type passed to ReadBackendBackupFile : %d\n", fileType);
			return NULL;
	}

	Qry = createPQExpBuffer();
	appendPQExpBuffer(Qry, "SELECT * FROM gp_read_backup_file('%s', '%s', %s)",
					  StringNotNull(pszBackupDirectory, ""),
					  StringNotNull(pszKey, ""),
					  pszFileType);

	pRes = PQexec(pConn, Qry->data);
	if (!pRes || PQresultStatus(pRes) != PGRES_TUPLES_OK || PQntuples(pRes) == 0)
	{
		mpp_err_msg_cache("ERROR", progName, "Error executing query %s : %s\n",
						  Qry->data,
						  PQerrorMessage(pConn));
	}
	else
	{
		pszRtn = strdup(PQgetvalue(pRes, 0, 0));
	}

	PQclear(pRes);
	destroyPQExpBuffer(Qry);

	return pszRtn;
}

/*
 * Safe_strdup:  returns strdup if not NULL, NULL otherwise
 */
char *
Safe_strdup(const char *s)
{
	if (s == NULL)
		return NULL;

	return (strdup(s));
}

/* stringNotNull: This function simply returns either the Input parameter if not NULL, or the
 * default parameter if the Input was NULL.
 * It is equivalent to the ternary expression
 * pszInput != NULL ? pszInput : pszDefault
 */
const char *
StringNotNull(const char *pszInput, const char *pszDefault)
{
	if (pszInput == NULL)
		return pszDefault;
	else
		return pszInput;
}

char *
get_early_error(void)
{
	return predump_errmsg;
}

/* Simple error logging to stderr  */
void
mpp_err_msg(const char *loglevel, const char *prog, const char *fmt,...)
{
	va_list		ap;
	char		szTimeNow[18];

	va_start(ap, fmt);
	fprintf(stderr, "%s|%s-[%s]:-", GetTimeNow(szTimeNow), prog, loglevel);
	vfprintf(stderr, gettext(fmt), ap);
	va_end(ap);
}

/* Simple error logging to stderr with msg caching for later re-use */
void
mpp_err_msg_cache(const char *loglevel, const char *prog, const char *fmt,...)
{
	va_list		ap;
	char		szTimeNow[18];

	va_start(ap, fmt);
	fprintf(stderr, "%s|%s-[%s]:-", GetTimeNow(szTimeNow), prog, loglevel);
	vfprintf(stderr, gettext(fmt), ap);
	va_end(ap);

	/* cache a copy of the message - we may need it for a report */
	va_start(ap, fmt);
	vsprintf(predump_errmsg, gettext(fmt), ap);
	va_end(ap);
}

/* Simple error logging to stdout  */
void
mpp_msg(const char *loglevel, const char *prog, const char *fmt,...)
{
	va_list		ap;
	char		szTimeNow[18];

	va_start(ap, fmt);
	fprintf(stdout, "%s|%s-[%s]:-", GetTimeNow(szTimeNow), prog, loglevel);
	vfprintf(stdout, gettext(fmt), ap);
	va_end(ap);
}


/* Base64 Encoding and Decoding Routines - copied form encode.c and then modified.
 * Base64 Data is assumed to be in a NULL terminated string.
 * Data is just assumed to be an array of chars, with a length.
 * Caller is expected to free return pointer in both cases.
 * In DataToBase64, return is a NULL terminated string.
 * In Base64ToData, return length is put into pOutLen partameter address.
 */

static unsigned
b64_enc_len(const char *src, unsigned srclen)
{
	/* 3 bytes will be converted to 4 */
	return (srclen + 2) * 4 / 3;
}

static unsigned
b64_dec_len(const char *src, unsigned srclen)
{
	return (srclen * 3) >> 2;
}

static const unsigned char _base64[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const char b64lookup[128] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
	-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
	-1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
};

char *
DataToBase64(char *pszIn, unsigned int InLen)
{
	char	   *p;
	const char *s;
	const char *end = pszIn + InLen;
	int			pos = 2;
	uint32		buf = 0;

	unsigned int OutLen = b64_enc_len(pszIn, InLen);
	char	   *pszOut = (char *) malloc(OutLen + 1);

	if (pszOut == NULL)
		return NULL;

	memset(pszOut, 0, OutLen + 1);

	s = pszIn;
	p = pszOut;

	while (s < end)
	{
		buf |= *s << (pos << 3);
		pos--;
		s++;

		/* write it out */
		if (pos < 0)
		{
			*p++ = _base64[(buf >> 18) & 0x3f];
			*p++ = _base64[(buf >> 12) & 0x3f];
			*p++ = _base64[(buf >> 6) & 0x3f];
			*p++ = _base64[buf & 0x3f];

			pos = 2;
			buf = 0;
		}
	}
	if (pos != 2)
	{
		*p++ = _base64[(buf >> 18) & 0x3f];
		*p++ = _base64[(buf >> 12) & 0x3f];
		*p++ = (pos == 0) ? _base64[(buf >> 6) & 0x3f] : '=';
		*p++ = '=';
	}

	return pszOut;
}

char *
Base64ToData(char *pszIn, unsigned int *pOutLen)
{
	const char *srcend;
	const char *s;
	char	   *p;
	unsigned	c;
	int			b = 0;
	uint32		buf = 0;
	int			pos = 0,
				end = 0;
	char	   *pszOut;
	unsigned int InLen = strlen(pszIn);
	unsigned int OutLen = b64_dec_len(pszIn, InLen);

	*pOutLen = OutLen;
	pszOut = (char *) malloc(OutLen);
	if (pszOut == NULL)
		return NULL;

	memset(pszOut, 0, OutLen);

	srcend = pszIn + InLen;
	s = pszIn;
	p = pszOut;


	while (s < srcend)
	{
		c = *s++;

		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			continue;

		if (c == '=')
		{
			/* end sequence */
			if (!end)
			{
				if (pos == 2)
					end = 1;
				else if (pos == 3)
					end = 2;
				else
				{
					assert(false);
					free(pszOut);
					return NULL;
				}
			}
			b = 0;
		}
		else
		{
			b = -1;
			if (c > 0 && c < 127)
				b = b64lookup[c];
			if (b < 0)
			{
				assert(false);
				free(pszOut);
				return NULL;
			}
		}
		/* add it to buffer */
		buf = (buf << 6) + b;
		pos++;
		if (pos == 4)
		{
			*p++ = (buf >> 16) & 255;
			if (end == 0 || end > 1)
				*p++ = (buf >> 8) & 255;
			if (end == 0 || end > 2)
				*p++ = buf & 255;
			buf = 0;
			pos = 0;
		}
	}

	if (pos != 0)
	{
		assert(false);
		free(pszOut);
		return NULL;
	}

	return pszOut;
}

char *
GetTimestampKey(char* timestamp_key)
{
	/* User has not provided timestamp, so we generate one */
	if (!timestamp_key){
		mpp_err_msg("INFO", "GetTimestampKey", "Timestamp key is generated as it is not provided by the user.\n");
		return GenerateTimestampKey();
	} 

	/* User has provided a valid timestamp, we simply use that */
	return strdup(timestamp_key);
}

char *
GenerateTimestampKey(void)
{
	struct tm	pNow;
	char		sNow[CDB_BACKUP_KEY_LEN + 1];

	time_t		tNow = time(NULL);

	localtime_r(&tNow, &pNow);
	sprintf(sNow, "%04d%02d%02d%02d%02d%02d",
			pNow.tm_year + 1900,
			pNow.tm_mon + 1,
			pNow.tm_mday,
			pNow.tm_hour,
			pNow.tm_min,
			pNow.tm_sec);
	return strdup(sNow);
}

bool
ValidateTimestampKey(char* key)
{
	const int max_gp_key_size = 14;
	int i = 0;

	if (!key){
		return false;
	}

	for (i = 0; i < max_gp_key_size; ++i)
	{
		// early null-terminator
		if (!key[i])
			return false;

		// non key character
		if (!isdigit(key[i]))
			return false;
	}

	// expect null-terminator at end of key
	if (key[max_gp_key_size])
		return false;
	else
		return true;
}

/*
 * Get next token from string *stringp, where tokens are possibly-empty
 * strings separated by characters from delim.
 *
 * Writes NULs into the string at *stringp to end tokens.
 * delim need not remain constant from call to call.
 * On return, *stringp points past the last NUL written (if there might
 * be further tokens), or is NULL (if there are definitely no more tokens).
 *
 * If *stringp is NULL, strsep returns NULL.
 */
char *
nextToken(register char **stringp, register const char *delim)
{
	register char *s;
	register const char *spanp;
	register int c,
				sc;
	char	   *tok;

	if ((s = *stringp) == NULL)
		return (NULL);
	for (tok = s;;)
	{
		c = *s++;
		spanp = delim;
		do
		{
			if ((sc = *spanp++) == c)
			{
				if (c == 0)
					s = NULL;
				else
					s[-1] = 0;
				*stringp = s;
				return (tok);
			}
		} while (sc != 0);
	}
	/* NOTREACHED */
}

/*
 * Parse the argument of --gp-s=i[...] . The list of dbid's to dump.
 * Return the number of parsed DBID's or < 1 for failure.
 */
int
parseDbidSet(int *dbidset, char *dump_set)
{
	int			len;
	int			count = 0;
	char	   *dbid_str = NULL;

	len = strlen(dump_set);

	/* we expect something of the form "i[?,?,?,...]" */
	if (dump_set[0] != 'i' ||
		dump_set[1] != '[' ||
		dump_set[len - 1] != ']')
	{
		mpp_err_msg_cache("ERROR", "gp_dump", "invalid dump set format in %s\n", dump_set);
		return -1;
	}

	dump_set[len - 1] = '\0';	/* remove ending "]" */
	dump_set += 2;				/* skip 'i' and  '[' */

	for (; (dbid_str = nextToken(&dump_set, ",")) != NULL;)
	{
		int			dbid;

		dbid = atoi(dbid_str);

		/* illigal dbid */
		if (dbid < 1)
		{
			mpp_err_msg_cache("ERROR", "gp_dump", "Invalid dump set entry. Each entry must be separeted by comma and be greater than 0\n");
			return -1;
		}

		dbidset[count++] = dbid;
	}

	return count;
}

const char* 
getBackupTypeString(bool incremental)
{
	if (incremental)
	{
		return "Backup Type: Incremental";
	}
	else
	{
		return "Backup Type: Full";
	}
}

char* 
formCompressionProgramString(char* compPg)
{
	char extra[] = " -c ";
	char* retVal = realloc(compPg, strlen(compPg) + sizeof(extra) + 2);
	if (retVal)
	{
		strcat(retVal, extra);	/* decompress to stdout */
	}
	return retVal;
}

void 
formPostDataSchemaOnlyPsqlCommandLine(char** retVal, const char* inputFileSpec, bool compUsed, const char* compProg,
									const char* post_data_filter_script, const char* table_filter_file, 
									const char* psqlPg, const char* catPg,
									const char* gpNBURestorePg, const char* netbackupServiceHost, const char* netbackupBlockSize)
{
	char* pszCmdLine = *retVal;
	if (compUsed)
	{
		if (netbackupServiceHost)
		{
			strncpy(pszCmdLine, gpNBURestorePg, (1 + strlen(gpNBURestorePg)));
			strncat(pszCmdLine, " --netbackup-service-host ", strlen(" --netbackup-service-host "));
			strncat(pszCmdLine, netbackupServiceHost, strlen(netbackupServiceHost));
			strncat(pszCmdLine, " --netbackup-filename ", strlen(" --netbackup-filename "));
			strncat(pszCmdLine, inputFileSpec, strlen(inputFileSpec));
			if (netbackupBlockSize)
			{
				strncat(pszCmdLine, " --netbackup-block-size ", strlen(" --netbackup-block-size "));
				strncat(pszCmdLine, netbackupBlockSize, strlen(netbackupBlockSize));
			}
			strncat(pszCmdLine, " | ", strlen(" | "));
			strncat(pszCmdLine, compProg, strlen(compProg));
		}
		else
		{
			strcpy(pszCmdLine, catPg);
			strcat(pszCmdLine, " ");
			strcat(pszCmdLine, inputFileSpec);
			strcat(pszCmdLine, " | ");
			strcat(pszCmdLine, compProg);
		}

		if (table_filter_file)
		{
			strcat(pszCmdLine, " | ");
			strcat(pszCmdLine, post_data_filter_script);
			strcat(pszCmdLine, " -t ");
			strcat(pszCmdLine, table_filter_file);
		}

		strcat(pszCmdLine, " | ");
		strcat(pszCmdLine, psqlPg);
	}    
	else 
	{    
		if (netbackupServiceHost)
		{
			strncpy(pszCmdLine, gpNBURestorePg, (1 + strlen(gpNBURestorePg)));
			strncat(pszCmdLine, " --netbackup-service-host ", strlen(" --netbackup-service-host "));
			strncat(pszCmdLine, netbackupServiceHost, strlen(netbackupServiceHost));
			strncat(pszCmdLine, " --netbackup-filename ", strlen(" --netbackup-filename "));
			strncat(pszCmdLine, inputFileSpec, strlen(inputFileSpec));
			if (netbackupBlockSize)
			{
				strncat(pszCmdLine, " --netbackup-block-size ", strlen(" --netbackup-block-size "));
				strncat(pszCmdLine, netbackupBlockSize, strlen(netbackupBlockSize));
			}
			strncat(pszCmdLine, " | ", strlen(" | "));
			strncat(pszCmdLine, psqlPg, strlen(psqlPg));
		}
		else
		{
			strcat(pszCmdLine, psqlPg);
			strcat(pszCmdLine, " -f ");
			strcat(pszCmdLine, inputFileSpec);
		}
	} 
}

void 
formSegmentPsqlCommandLine(char** retVal, const char* inputFileSpec, bool compUsed, const char* compProg,
							const char* filter_script, const char* table_filter_file, 
							int role, const char* psqlPg, const char* catPg,
							const char* gpNBURestorePg, const char* netbackupServiceHost, const char* netbackupBlockSize)
{
	char* pszCmdLine = *retVal;
	if (compUsed)
	{
		if (netbackupServiceHost)
		{
			strncpy(pszCmdLine, gpNBURestorePg, (1 + strlen(gpNBURestorePg)));
			strncat(pszCmdLine, " --netbackup-service-host ", strlen(" --netbackup-service-host "));
			strncat(pszCmdLine, netbackupServiceHost, strlen(netbackupServiceHost));
			strncat(pszCmdLine, " --netbackup-filename ", strlen(" --netbackup-filename "));
			strncat(pszCmdLine, inputFileSpec, strlen(inputFileSpec));
			if (netbackupBlockSize)
			{
				strncat(pszCmdLine, " --netbackup-block-size ", strlen(" --netbackup-block-size "));
				strncat(pszCmdLine, netbackupBlockSize, strlen(netbackupBlockSize));
			}
			strncat(pszCmdLine, " | ", strlen(" | "));
			strncat(pszCmdLine, compProg, strlen(compProg));
		}
		else
		{
			strcpy(pszCmdLine, catPg);
			strcat(pszCmdLine, " ");
			strcat(pszCmdLine, inputFileSpec);
			strcat(pszCmdLine, " | ");
			strcat(pszCmdLine, compProg);
		}
	}
	else
	{
		if (netbackupServiceHost)
		{
			strncpy(pszCmdLine, gpNBURestorePg, (1 + strlen(gpNBURestorePg)));
			strncat(pszCmdLine, " --netbackup-service-host ", strlen(" --netbackup-service-host "));
			strncat(pszCmdLine, netbackupServiceHost, strlen(netbackupServiceHost));
			strncat(pszCmdLine, " --netbackup-filename ", strlen(" --netbackup-filename "));
			strncat(pszCmdLine, inputFileSpec, strlen(inputFileSpec));
			if (netbackupBlockSize)
			{
				strncat(pszCmdLine, " --netbackup-block-size ", strlen(" --netbackup-block-size "));
				strncat(pszCmdLine, netbackupBlockSize, strlen(netbackupBlockSize));
			}
		}
		else
		{
			strcpy(pszCmdLine, catPg);
			strcat(pszCmdLine, " ");
			strcat(pszCmdLine, inputFileSpec);
		}
	}

	if (table_filter_file)
	{
		strcat(pszCmdLine, " | ");
		strcat(pszCmdLine, filter_script);
		if (role == ROLE_MASTER)
			strcat(pszCmdLine, " -m");
		strcat(pszCmdLine, " -t ");
		strcat(pszCmdLine, table_filter_file);
	}

    strcat(pszCmdLine, " | ");
    strcat(pszCmdLine, psqlPg);
}
	

#ifdef USE_DDBOOST
#include <dlfcn.h>
#include "clb_base.h"

#define	NO_ERR	0

ddp_client_info_t dd_client_info = {DDP_CL_DDP, "Greenplum Database version " GP_VERSION};
struct ddboost_logs
{
	ddp_severity_t logLevel ;
	unsigned int logsSize ;
} ddboost_logs_info;

/* define function pointers in order to use the LB functions with dlsym */
void (*clb_getErrorMessage)(int errorCode, char **errorMessage) = NULL;
int (*clb_create)(const char* lockboxId, const char* passphrase, int overwrite, clbContext lbCntx, clbHandle *clbH) = NULL;
int (*clb_open)(const char* lockboxId, const char* passphrase, clbContext lbCntx, clbHandle *clbH) = NULL;
int (*clb_close)(clbHandle clbH) = NULL;
void (*clb_free)(void *buffer) = NULL;
int (*clb_setLockboxMode)(clbHandle clbH, clb_mode_e mode) = NULL;
int (*clb_createItemAsText)(clbHandle clbH, const char* itemName, const char* secret) = NULL;
int (*clb_retrieveItemAsText)(clbHandle clbH, const char* itemName, char** secret) = NULL;


static int setItem(clbHandle* LB, char *key, char *value);
static int getItem(clbHandle* LB, char *key, char **value);
static int setLBEnv(void);
static int createLB(clbHandle* LB,char* name);
static int openLB(clbHandle* LB,char* name);
static int validateDDBoostCredential(char *hostname, char *user, char *password, char* log_level ,char* log_size, char *default_backup_directory, bool remote);
int getDDBoostCredential(char** hostname, char** user, char** password, char** log_level ,char** log_size, char **default_backup_directory, bool remote);

/*
 * Set the environment variable LD_LIBRARY_PATH in order to dynamically load LB's libraries.
 * Returns 0 in case of success, and -1 otherwise.
 */
static int
setLBEnv(void)
{
	void *hdl = NULL;
	char *gphome = getenv("GPHOME");
	char *ldpath = getenv("LD_LIBRARY_PATH");
	char LBpath[PATH_MAX];
	char *newldpath = NULL;
	char libpath[PATH_MAX];
	char *libdirname = "lib/rsa_csp";
	char *libname = "libCSP-lb.so";
	int  newldpath_len = 0;

	if (NULL == gphome)
	{
		mpp_err_msg("ERROR", "ddboost", "GPHOME undefined, can't set ddboost credentials\n");
		return -1;
	}

	if (NULL == ldpath)
	{
		mpp_err_msg("ERROR", "ddboost", "LD_LIBRARY_PATH undefined, can't set ddboost credentials\n");
		return -1;
	}

	snprintf(LBpath, strlen(gphome) + strlen(libdirname) + 2, "%s/%s", gphome, libdirname);

	newldpath_len = strlen(ldpath) + strlen(LBpath) + 2;
	newldpath = malloc(newldpath_len);

	if (NULL == newldpath)
	{
		mpp_err_msg("ERROR", "ddboost", "Memory allocation failed during DDBoost credentials initialization\n");
		return -1;
	}

	snprintf(newldpath, newldpath_len, "%s:%s", LBpath, ldpath);
	setenv("LD_LIBRARY_PATH", newldpath, 1);
	free(newldpath);
	newldpath = NULL;

	snprintf(libpath, strlen(LBpath) + strlen(libname) + 2, "%s/%s", LBpath, libname);

	if (NULL == (hdl = dlopen(libpath, RTLD_NOW | RTLD_LOCAL)))
	{
		mpp_err_msg("ERROR", "ddboost", "libCSP-lb.so was not found. Can't set ddboost credentials\n");
		return -1;
	}

	if (NULL == (clb_getErrorMessage = dlsym(hdl, "clb_getErrorMessage")) ||
			NULL == (clb_create = dlsym(hdl, "clb_create")) ||
			NULL == (clb_open = dlsym(hdl, "clb_open")) ||
			NULL == (clb_close = dlsym(hdl, "clb_close")) ||
			NULL == (clb_free = dlsym(hdl, "clb_free")) ||
			NULL == (clb_setLockboxMode = dlsym(hdl, "clb_setLockboxMode")) ||
			NULL == (clb_createItemAsText = dlsym(hdl, "clb_createItemAsText")) ||
			NULL == (clb_retrieveItemAsText = dlsym(hdl, "clb_retrieveItemAsText")))
	{
		mpp_err_msg("ERROR", "ddboost", "Failed to load dynamic libraries. Can't set ddboost credentials\n");
		return -1;
	}

	mpp_err_msg("DEBUG", "ddboost", "Libraries were loaded successfully\n");
	return 0;
}

static int
setItem(clbHandle* LB, char *key, char *value)
{
	int iError = clb_createItemAsText(*LB, key, value);
	if (iError != NO_ERR)
	{
		char* eMsg= NULL;;
		clb_getErrorMessage(iError,&eMsg);
		mpp_err_msg("ERROR", "ddboost", eMsg);
		clb_free(eMsg);
		clb_close(*LB);
		return iError;
	};

	return 0;
}

static int
getItem(clbHandle* LB, char *key, char **value)
{
	int iError = clb_retrieveItemAsText(*LB, key, value);
	if (iError != NO_ERR)
	{
		char* eMsg= NULL;;
		clb_getErrorMessage(iError,&eMsg);
		mpp_err_msg("ERROR", "ddboost", eMsg);
		clb_free(eMsg);
		clb_close(*LB);
		return iError;
	};

	return 0;
}

static int
createLB(clbHandle* LB,char* name)
{
	int iError, i;
	char filepath[PATH_MAX];
	char *home = getenv("HOME");
	char* eMsg = NULL;
	char clb_pass[35] = "1!qQ";
	int _base64_len = sizeof(_base64)/sizeof(unsigned char) - 2;

	if (NULL == home)
	{
		mpp_err_msg("ERROR", "ddboost", "HOME undefined, can't set ddboost credentials\n");
		return -1;
	}

	memset(filepath, 0, PATH_MAX);
	snprintf(filepath, strlen(home) + strlen(name) + 2, "%s/%s", home, name);

	if (setLBEnv() < 0)
	{
		return -1;
	}

	/* generate random password to create the lockbox */
	srand ((unsigned) time(NULL));

	/* choose valid characters from _base64 for the lockbox password */
	for (i=4; i < 34; i++)
	{
		clb_pass[i] = _base64[rand() % _base64_len];
	}
	clb_pass[34] = '\0';
	
	/* 
	 * for creating the lockbox we should call to clb_create.
	 * this function needs a password with at least 8 characters, with several constraints.
	 * the password is set to optional few lines later, but we must initialize it during the LB creation. 
	 * of course we don't want to use fixed password, so we're using a random password
	 */
	mpp_err_msg("INFO", "ddboost", "creating LB on %s\n", filepath);
	if ((iError = clb_create(filepath, clb_pass, 1 /*overwrite flag*/, 0 /*reserved*/,LB))!=NO_ERR)
	{
		clb_getErrorMessage(iError,&eMsg);
		mpp_err_msg("ERROR", "ddboost", "%s\n", eMsg);
		clb_free(eMsg);
		return iError;
	};

	if ((iError = clb_setLockboxMode(*LB, CLB_PASSPHRASE_OPTIONAL))!=NO_ERR)
	{
		clb_getErrorMessage(iError,&eMsg);
		mpp_err_msg("ERROR", "ddboost", "%s\n", eMsg);
		clb_free(eMsg);
		clb_close(*LB);

		return iError;
	};

	return 0;
}

static int
openLB(clbHandle* LB,char* name)
{
	int iError;
	char filepath[PATH_MAX];	
	char *home = getenv("HOME");	
	char* eMsg = NULL;

	if (NULL == home)
	{
		mpp_err_msg("ERROR", "ddboost", "HOME undefined, can't set ddboost credentials\n");
		return -1;
	}
	
	memset(filepath, 0, PATH_MAX);
	snprintf(filepath, strlen(home) + strlen(name) + 2, "%s/%s", home, name);
	
	if (setLBEnv() < 0)
	{
		return -1;
	}

	mpp_err_msg("INFO", "ddboost", "opening LB on %s\n", filepath);
	if ((iError = clb_open(filepath, NULL, 0 /*reserved*/,LB))!=NO_ERR)
	{
		clb_getErrorMessage(iError,&eMsg);
		mpp_err_msg("ERROR", "ddboost", "%s\n", eMsg);
		clb_free(eMsg);
		return iError;
	};
	return 0;
}

/*
 * Write the hostname, user, password, log_level and log_size to the LB
 * Returns 0 in case of success, and -1 otherwise.
 */
int
setDDBoostCredential(char *hostname, char *user, char *password, char* log_level ,char* log_size, char *default_backup_directory, bool remote)
{
	/* TODO: validate default backup directory name if needed */
	if (validateDDBoostCredential(hostname, user, password, log_level , log_size, default_backup_directory, remote))
		return -1;

	clbHandle LB;
	if (remote)
	{
		if (createLB(&LB, "DDBOOST_MFR_CONFIG"))
			return -1;
	}
	else
	{
		if (createLB(&LB, "DDBOOST_CONFIG"))
			return -1;
	}
	if (setItem(&LB , "hostname",hostname))
		return -1;
	if (setItem(&LB , "user",user))
		return -1;
	if (setItem(&LB , "password",password))
		return -1;
	if (!remote)
	{
		if (setItem(&LB , "default_backup_directory",default_backup_directory))
			return -1;
	}

	if (log_level)
	{
		if (setItem(&LB , "log_level",log_level))
			return -1;
	}
	else
	{
		if (setItem(&LB , "log_level","WARNING"))
			return -1;
	}

	if (log_size)
	{
		if (setItem(&LB , "log_size",log_size))
			return -1;
	}
	else
	{
		if (setItem(&LB , "log_size","50"))
			return -1;
	}

	clb_close(LB);

	return 0;
}

int
getDDBoostCredential(char** hostname, char** user, char** password, char **log_level ,char** log_size, char **default_backup_directory, bool remote)
{
	clbHandle LB;
	
	if (remote)
	{
		if (openLB(&LB,"DDBOOST_MFR_CONFIG"))
			return -1;
	}
	else
	{
		if (openLB(&LB,"DDBOOST_CONFIG"))
			return -1;
	}
	if (getItem(&LB , "hostname",hostname))
		return -1;
	if (getItem(&LB , "user",user))
		return -1;
	if (getItem(&LB , "password",password))
		return -1;
	if (!remote)
	{
		if (getItem(&LB , "default_backup_directory",default_backup_directory))
			return -1;
	}
	if (getItem(&LB , "log_level",log_level))
		return -1;
	if (getItem(&LB , "log_size",log_size))
		return -1;
	clb_close(LB);
	return 0;
}

int
validateDDBoostCredential(char *hostname, char *user, char *password, char* log_level ,char* log_size, char * default_backup_directory, bool remote)
{
	if (!user)
	{
		mpp_err_msg("ERROR", "ddboost", "no user \n");
		return -1;
	}

	if (strlen(user) > (DDBOOST_USERNAME_MAXLENGTH) || strlen(user) < 2)
	{
		mpp_err_msg("ERROR","ddboost", "username too long or too short, user name is limited to 30 characters\n");
		return -1;
	}

	if (!password)
	{
		mpp_err_msg("ERROR", "ddboost", "no password \n");
		return -1;
	}

	if (strlen(password) > (DDBOOST_PASSWORD_MAXLENGTH) || strlen(password) < 2)
	{
		mpp_err_msg("ERROR", "ddboost", "password too long or too short, password is limited to 40 characters\n");
		return -1;
	}

	if (log_level)
	{
		if ((strncmp("INFO",log_level,5) &&
			strncmp("WARN",log_level,5) &&
			strncmp("DEBUG",log_level,6) &&
			strncmp("ERROR",log_level,6) &&
			strncmp("NONE",log_level,5)) !=0)
		{
			mpp_err_msg("ERROR", "ddboost", "Illegal value for log level: %s, please use INFO, WARN, DEBUG, ERROR or NONE\n",log_level);
			return -1;
		}
	}

	if (log_size)
	{
		int size = atoi(log_size);
		if (size < 1 || size > 1000)
		{
			mpp_err_msg("ERROR", "ddboost", "Illegal value for log size(MB): %s, please use size between 1 and 1000\n",log_size);
			return -1;
		}
	}

	if (!remote && !default_backup_directory)
	{
		mpp_err_msg("ERROR", "ddboost", "no default_backup_directory configured\n");
		return -1;
	}
	return 0;
}

int
parseDDBoostCredential(char *hostname, char *user, char *password, const char *progName)
{
	char filepath[PATH_MAX];	
	char *home = getenv("HOME");
	char line[PATH_MAX];
	
	if (NULL == home)
	{
		mpp_err_msg("ERROR", progName, "HOME undefined, can't set ddboost credentials\n");
		return -1;
	}
	
	memset(filepath, 0, PATH_MAX);
	snprintf(filepath, strlen(home) + strlen(DDBOOST_CONFIG_FILE) + 2, "%s/%s", home, DDBOOST_CONFIG_FILE);

	memset(line, 0, PATH_MAX);
	memset(hostname, 0, PATH_MAX);
	memset(user, 0, PATH_MAX);
	memset(password, 0, PATH_MAX);

	FILE *fp = fopen(filepath, "r");
	if (!fp)
	{
		mpp_err_msg("ERROR", progName, "error opening DD Boost credential config file %s\n", filepath);
		return -1;
	}

	if (!fgets(line, PATH_MAX, fp))
	{
		mpp_err_msg("ERROR", progName, "end of DD Boost config file, missing hostname\n");
		return -1;
	}
	strncpy(hostname, line, strlen(line) - 1);
	memset(line, 0, PATH_MAX);

	if (!fgets(line, PATH_MAX, fp))
	{
		mpp_err_msg("ERROR", progName, "end of DD Boost config file, missing user name \n");
		return -1;
	}
	if (strlen(line) > (DDBOOST_USERNAME_MAXLENGTH + 1) || strlen(line) < 2)
	{
		mpp_err_msg("ERROR", progName, "username too long or too short, user name is limited to 30 characters\n");
		return -1;
	}
	strncpy(user, line, strlen(line) - 1);
	memset(line, 0, PATH_MAX);

	if (!fgets(line, PATH_MAX, fp))
	{
		mpp_err_msg("ERROR", progName, "end of DD Boost config file, missing password\n");
		return -1;
	}
	if (strlen(line) > (DDBOOST_PASSWORD_MAXLENGTH + 1) || strlen(line) < 2)
	{
		mpp_err_msg("ERROR", progName, "password too long or too short, password is limited to 40 characters\n");
		return -1;
	}
	strncpy(password, line, strlen(line) - 1);


	if (!fgets(line, PATH_MAX, fp))
	{
		mpp_err_msg("INFO", progName, "end of DD Boost config file, using defaults log param (level = WARN, size = 50MB) \n");
		ddboost_logs_info.logLevel = DDP_SEV_WARN;
		ddboost_logs_info.logsSize = 50*1024*1024;
		return 0;
	}
	if (!strncmp("INFO",line,4))
		ddboost_logs_info.logLevel = DDP_SEV_INFO;
	else if (!strncmp("WARN",line,4))
		ddboost_logs_info.logLevel = DDP_SEV_WARN;
	else if (!strncmp("DEBUG",line,5))
		ddboost_logs_info.logLevel = DDP_SEV_DEBUG;
	else if (!strncmp("ERROR",line,5))
		ddboost_logs_info.logLevel = DDP_SEV_ERROR;
	else if (!strncmp("NONE",line,4))
		ddboost_logs_info.logLevel = DDP_SEV_NONE;
	else
	{
		mpp_err_msg("ERROR", progName, "Illegal value for log level: %s, please use INFO, WARN, DEBUG, ERROR or NONE\n",line);
		return -1;
	}
	mpp_err_msg("INFO", progName, "DD log level = %u \n",ddboost_logs_info.logLevel);


	if (!fgets(line, PATH_MAX, fp))
	{
		mpp_err_msg("INFO", progName, "end of DD Boost config file, using defaults log size = 50(MB) \n");
		return 0;
	}
	int size = atoi(line);
	if (size < 1 || size > 1000)
	{
		mpp_err_msg("ERROR", progName, "Illegal value for log size(MB): %s, please use size between 1 and 1000\n",line);
		return -1;
	}
	ddboost_logs_info.logsSize = size * 1024 * 1024;
	mpp_err_msg("INFO", progName, "DD log size = %u(bytes) \n",ddboost_logs_info.logsSize);
	return 0;
}

/* if the file too long this will rotate the files_name to file_name_0 - .._10 . the last file is deleted*/
void rotate_dd_logs(const char *file_name, unsigned int num_of_files, unsigned int log_size)
{
    struct stat st;
    
    if (stat(file_name,&st) == 0)
        {
            unsigned int size = (unsigned int)st.st_size;
            if (size > log_size)
                {
                    
                    char tmp_name[80];
                    char next_tmp_name[80];
                    sprintf(tmp_name,"%s_%u",file_name,num_of_files);
                    int r = remove(tmp_name);
                    if (r != 0)
                        mpp_err_msg("INFO","rotate_dd_logs","didn't delete of %s , %s\n" ,tmp_name, strerror( errno ));

                    for (unsigned int i = num_of_files - 1; i > 0; i--){                        
                        snprintf(next_tmp_name, 80, "%s_%u",file_name,i + 1);
                        snprintf(tmp_name,80 ,"%s_%u", file_name,i);
                      
                        if (rename(tmp_name, next_tmp_name) != 0)
                            mpp_err_msg("INFO","rotate_dd_logs","didn't rename of %s to %s : %s\n" ,tmp_name,next_tmp_name,strerror( errno ));
                    }
                    snprintf(next_tmp_name, 80, "%s_%u",file_name,1);
                    if ((r = rename(file_name, next_tmp_name)) != 0)
                        mpp_err_msg("INFO","rotate_dd_logs","didn't rename first log %s to %s : %s" ,tmp_name,next_tmp_name,strerror( errno ));
                   
                }
        }
    else
        mpp_err_msg("INFO","rotate_dd_logs","failed to find size");
}
/* Initialize the file for logging DDboost related information */
void 
_ddp_test_log(const void *session_ptr, const ddp_char_t *log_msg, ddp_severity_t severity)
{

    if (severity >  ddboost_logs_info.logLevel)//filtering of the logs
        return;

    FILE* log_file = NULL;
    time_t ltime;
    struct tm *Tm;
    char file_name[] = "libDDBoost.log";
    
    rotate_dd_logs(file_name, DDBOOST_LOG_NUM_OF_FILES, ddboost_logs_info.logsSize / DDBOOST_LOG_NUM_OF_FILES);

    log_file = fopen(file_name, "a");

       
    if (log_file) {
        ltime = time(NULL);
        Tm = localtime(&ltime);
        fprintf(log_file, "%02d/%02d %02d:%02d:%02d: %s\n",
                           Tm->tm_mon + 1, Tm->tm_mday,
                           Tm->tm_hour + (1 - Tm->tm_isdst), Tm->tm_min, Tm->tm_sec,
                           log_msg);
        fclose(log_file);
    }
}

int 
initDDSystem(ddp_inst_desc_t *ddp_inst, ddp_conn_desc_t *ddp_conn, ddp_client_info_t *cl_info, char **ddp_su_name,
            bool createStorageUnit, char **default_backup_directory, bool remote)
{
	int err = DD_ERR_NONE;
	uint POOL_SIZE = DDBOOST_POOL_SIZE;
	char *storage_unit_name = NULL;
	char *dd_boost_username = NULL;
	char *dd_boost_passwd = NULL;
	char *dd_boost_hostname = NULL;
	char *log_level = NULL;
	char *log_size = NULL;

	err = getDDBoostCredential(&dd_boost_hostname,
			&dd_boost_username,
			&dd_boost_passwd ,
			&log_level,
			&log_size,
			default_backup_directory,
			remote);

	if (err)
	{
		mpp_err_msg("ERROR", "ddboost", "Parsing DDBoost login credentials failed\n");
		return -1;
	}

	storage_unit_name = (char*)malloc(PATH_MAX);
	if (storage_unit_name == NULL)
	{
		mpp_err_msg("ERROR", "ddboost", "Memory allocation failed during DDBoost initialization\n");
		return -1;
	}
	sprintf(storage_unit_name, "%s", "GPDB");	
	*ddp_su_name = storage_unit_name;
	
	if (*ddp_inst == DDP_INVALID_DESCRIPTOR)
	{
		err = ddp_instance_create(POOL_SIZE, cl_info, ddp_inst);
		if (err)
        	{
                	mpp_err_msg("ERROR", "ddboost", "ddboost instance creation failed. Err = %d\n", err);
                	return err;
        	}

		ddp_log_init(*ddp_inst, NULL, _ddp_test_log);
	}

	
	err = ddp_connect_with_user_pwd(*ddp_inst, dd_boost_hostname, NULL, dd_boost_username, dd_boost_passwd, ddp_conn);	
	if (err != DD_ERR_NONE) {
               mpp_err_msg("ERROR", "ddboost", "ddboost connect failed. Err = %d, remote = %d\n", err, remote);
               return err;
        }

	if (createStorageUnit)
	{
		err = ddp_create_storage_unit(*ddp_conn, storage_unit_name);
		if (err != DD_ERR_NONE) {
			mpp_err_msg("ERROR", "ddboost", "ddboost create storage unit failed. Err = %d\n", err);
			return err;
		}
	}

	if (!strncmp("INFO",log_level,4))
		ddboost_logs_info.logLevel = DDP_SEV_INFO;
	else if (!strncmp("WARN",log_level,4))
		ddboost_logs_info.logLevel = DDP_SEV_WARN;
	else if (!strncmp("DEBUG",log_level,5))
		ddboost_logs_info.logLevel = DDP_SEV_DEBUG;
	else if (!strncmp("ERROR",log_level,5))
		ddboost_logs_info.logLevel = DDP_SEV_ERROR;
	else if (!strncmp("NONE",log_level,4))
		ddboost_logs_info.logLevel = DDP_SEV_NONE;
	
	ddboost_logs_info.logsSize = atoi(log_size)*1024*1024;	

	return 0;
}

				

void
formDDBoostPsqlCommandLine(char** retVal, bool compUsed, const char* ddboostPg, const char* compProg, 
							const char* ddp_file_name, const char* dd_boost_buf_size,
							const char* filter_script, const char* table_filter_file, 
							int role, const char* psqlPg)
{
	char* pszCmdLine = *retVal;

	strcpy(pszCmdLine, ddboostPg);
	strcat(pszCmdLine, " --readFile");
	strcat(pszCmdLine, " --from-file=");
	strcat(pszCmdLine, ddp_file_name);
	
	if(compUsed)
	{
		strcat(pszCmdLine, ".gz");
	}

	strcat(pszCmdLine, " --dd_boost_buf_size=");
	strcat(pszCmdLine, dd_boost_buf_size);

	if(compUsed)
	{
		strcat(pszCmdLine, " | ");
		strcat(pszCmdLine, compProg);
	}

	if (table_filter_file) 
	{
		strcat(pszCmdLine, " | ");
		strcat(pszCmdLine, filter_script);
		if (role == ROLE_MASTER)
		{
			strcat(pszCmdLine, " -m");
		}
		strcat(pszCmdLine, " -t ");
		strcat(pszCmdLine, table_filter_file);
	}
	strcat(pszCmdLine, " | ");
	strcat(pszCmdLine, psqlPg);
}

#endif

/***************************************
 *
 * Below are functions relating to
 * the hash table used in getTableAttrs.
 *
 ***************************************/

typedef struct Node
{
    Oid oid;
    char typstorage;
    struct Node *next;
}Node;

Node **hash_table;
int HASH_TABLE_SIZE = 0;
const Oid EMPTY_OID = 0;
const char EMPTY_TYPSTORAGE = '\0';

int
initializeHashTable(int num_elems)
{ 
    HASH_TABLE_SIZE = num_elems;

    if(!(hash_table = (Node **)calloc(HASH_TABLE_SIZE, sizeof(Node*))))
    {
        printf("Could not allocate memory for the hash table\n");
        return -1;
    }
    return 0;
}

int
insertIntoHashTable(Oid o, char t)
{
    int index = hashFunc(o);
    Node *cur = hash_table[index];
    Node *prev = NULL;

    Node *new_node = (Node *)malloc(sizeof(Node));

	if (!new_node) 
		return -1;

    new_node->oid = o;
    new_node->typstorage = t;
    new_node->next = NULL;

    if(!cur)
    {
		hash_table[index] = new_node;
        return 0;
    }

    while(cur)
    {
        if(cur->oid == o)
		{
			free(new_node);
            return -1;
		}

        prev = cur;
        cur = cur->next;
    }

	prev->next = new_node;
    return 0;
}

int hashFunc(Oid k)
{
    return k % HASH_TABLE_SIZE;
}

char getTypstorage(Oid o)
{
    int index  = hashFunc(o);
    Node *temp = hash_table[index];

    while(temp != NULL)
    {
        if(temp->oid == o)
            return temp->typstorage;
        temp = temp->next;
    }
    
    return EMPTY_TYPSTORAGE;    
}

int removeNode(Oid o)
{
    int index = hashFunc(o);

	if(!hash_table[index])
	{
		return -1;
	}

    Node *cur = hash_table[index];
    Node *prev = NULL;

    while(cur)
    {
        if(cur->oid == o)
        {
            if(!prev)
            {
				hash_table[index] = cur->next;
				free(cur);
				return 0;
            }
			prev->next = cur->next;
			free(cur);
			return 0;
        }
        prev = cur;
        cur = cur->next;
    }
    return -1;
}

void cleanUpTable()
{

    int i = 0;
    for(; i < HASH_TABLE_SIZE; i++)
    {
        Node *cur = hash_table[i];
        Node *tmp = NULL;
        while(cur)
        {
			tmp = cur;
			cur = cur->next;
			free(tmp);
        }
    }
	if(hash_table)
    	free(hash_table);
    hash_table = NULL;
    HASH_TABLE_SIZE = 0;

}

