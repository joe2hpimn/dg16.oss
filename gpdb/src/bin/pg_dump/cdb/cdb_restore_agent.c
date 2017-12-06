/*-------------------------------------------------------------------------
 *
 * pg_restore.c
 *	pg_restore is an utility extracting postgres database definitions
 *	from a backup archive created by pg_dump using the archiver
 *	interface.
 *
 *	pg_restore will read the backup archive and
 *	dump out a script that reproduces
 *	the schema of the database in terms of
 *		  user-defined types
 *		  user-defined functions
 *		  tables
 *		  indexes
 *		  aggregates
 *		  operators
 *		  ACL - grant/revoke
 *
 * the output script is SQL that is understood by PostgreSQL
 *
 * Basic process in a restore operation is:
 *
 *	Open the Archive and read the TOC.
 *	Set flags in TOC entries, and *maybe* reorder them.
 *	Generate script to stdout
 *	Exit
 *
 * Copyright (c) 2000, Philip Warner
 *		Rights are granted to use this software in any way so long
 *		as this notice is not removed.
 *
 *	The author is not responsible for loss or damages that may
 *	result from its use.
 *
 *
 * IDENTIFICATION
 *		$PostgreSQL: pgsql/src/bin/pg_dump/pg_restore.c,v 1.68 2004/12/03 18:48:19 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */

#include "pg_backup.h"
#include "pg_backup_archiver.h"
#include "dumputils.h"

#include <ctype.h>
/* #include <sys/types.h> */
#include <sys/stat.h>
#include <poll.h>

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#include <unistd.h>

#include "getopt_long.h"
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>
#include "cdb_dump_util.h"
#include "cdb_backup_status.h"


#ifndef HAVE_INT_OPTRESET
int			optreset;
#endif

#ifdef ENABLE_NLS
#include <locale.h>
#endif

#ifdef USE_DDBOOST
#include "ddp_api.h"
static char *formDDBoostFileName(char *pszBackupKey, bool isPostData, char *ddBoostDir);
static int cleanupDDSystem(void);

extern ddp_client_info_t dd_client_info;

static ddp_inst_desc_t ddp_inst = DDP_INVALID_DESCRIPTOR;
static ddp_conn_desc_t ddp_conn = DDP_INVALID_DESCRIPTOR;

static char *DDP_SU_NAME = NULL;
static char *DEFAULT_BACKUP_DIRECTORY = NULL;

char *log_message_path = NULL;
static int dd_boost_enabled = 0;
static char *dd_boost_buf_size = NULL;
static char *ddboostPg = NULL;
#endif

#ifndef PATH_NAME_MAX
#define PATH_NAME_MAX 1024
#endif


#define DUMP_PREFIX (dump_prefix==NULL?"":dump_prefix)

static void usage(const char *progname);

typedef struct option optType;

/*	mpp specific stuff */
extern void makeSureMonitorThreadEnds(int TaskRC, char *pszErrorMsg);
static void myChildHandler(int signo);
static void psqlHandler(int signo);
static void myHandler(int signo);
static void *monitorThreadProc(void *arg __attribute__((unused)));
static void _check_database_version(ArchiveHandle *AH);
/* static bool execMPPCatalogFunction(PGconn* pConn); */
static char *testProgramExists(char *pszProgramName);

static bool bUsePSQL = false;
static volatile sig_atomic_t bPSQLDone = false;
static volatile sig_atomic_t bKillPsql = false;
static bool bCompUsed = false;	/* de-compression is used */
static char *g_compPg = NULL;	/* de-compression program with full path */
static char *g_gpdumpInfo = NULL;
static char *g_gpdumpKey = NULL;
static int	g_sourceDBID = 0;
static int	g_targetDBID = 0;
static char *g_targetHost = NULL;
static char *g_targetPort = NULL;
static char *g_MPPPassThroughCredentials = NULL;
static char *g_pszMPPOutputDirectory = NULL;
static PGconn *g_conn_status = NULL;
static pthread_t g_main_tid = (pthread_t) 0;
static pthread_t g_monitor_tid = (pthread_t) 0;
static bool g_bOnErrorStop = false;
PGconn	   *g_conn = NULL;
pthread_mutex_t g_threadSyncPoint = PTHREAD_MUTEX_INITIALIZER;

static const char *logInfo = "INFO";
static const char *logWarn = "WARN";
static const char *logError = "ERROR";
/* static const char* logFatal = "FATAL"; */

static StatusOpList *g_pStatusOpList = NULL;
char	   *g_LastMsg = NULL;

int			g_role = ROLE_MASTER;		/* would change to ROLE_MASTER when
										 * necessary */
static const unsigned long MAX_COMMANDLINE_LEN = 1000000;
static char * table_filter_file = NULL;
static char * dump_prefix = NULL;
/* end cdb additions */
/*	end cdb specific stuff */

/* NetBackup related variables */
static char *netbackup_service_host = NULL;
static char *netbackup_block_size = NULL;

int
main(int argc, char **argv)
{
	RestoreOptions *opts;
	int			c;
	int			exit_code = 0;
	Archive    *AH;
	char	   *inputFileSpec;
	extern int	optind;
	extern char *optarg;
	static int	use_setsessauth = 0;
	static int	disable_triggers = 0;
	SegmentDatabase SegDB;
	StatusOp   *pOp;
	struct sigaction act;
	pid_t		newpid;
	char	   *pszOnErrorStop = "-v ON_ERROR_STOP=";

	/* int i; */
	char	   *pszCmdLine;
	int			status;
	int			rc;
	char	   *pszErrorMsg;
	ArchiveHandle *pAH;
	int 		postDataSchemaOnly = 0;

#ifdef USE_DDBOOST
	char    *ddp_file_name = NULL;
	char 	*dd_boost_dir = NULL;
#endif

	struct option cmdopts[] = {
		{"clean", 0, NULL, 'c'},
		{"create", 0, NULL, 'C'},
		{"data-only", 0, NULL, 'a'},
		{"dbname", 1, NULL, 'd'},
		{"exit-on-error", 0, NULL, 'e'},
		{"file", 1, NULL, 'f'},
		{"format", 1, NULL, 'F'},
		{"function", 1, NULL, 'P'},
		{"host", 1, NULL, 'h'},
		{"ignore-version", 0, NULL, 'i'},
		{"index", 1, NULL, 'I'},
		{"list", 0, NULL, 'l'},
		{"no-privileges", 0, NULL, 'x'},
		{"no-acl", 0, NULL, 'x'},
		{"no-owner", 0, NULL, 'O'},
		{"no-reconnect", 0, NULL, 'R'},
		{"port", 1, NULL, 'p'},
		{"password", 0, NULL, 'W'},
		{"schema-only", 0, NULL, 's'},
		{"superuser", 1, NULL, 'S'},
		{"table", 1, NULL, 't'},
		{"trigger", 1, NULL, 'T'},
		{"use-list", 1, NULL, 'L'},
		{"username", 1, NULL, 'U'},
		{"verbose", 0, NULL, 'v'},

		/*
		 * the following options don't have an equivalent short option letter,
		 * but are available as '-X long-name'
		 */
		{"use-set-session-authorization", no_argument, &use_setsessauth, 1},
		{"disable-triggers", no_argument, &disable_triggers, 1},

		/*
		 * the following are cdb specific, and don't have an equivalent short
		 * option
		 */
		{"gp-d", required_argument, NULL, 1},
		{"gp-e", no_argument, NULL, 2},
		{"gp-k", required_argument, NULL, 3},
		{"gp-c", required_argument, NULL, 4},
		{"target-dbid", required_argument, NULL, 5},
		{"target-host", required_argument, NULL, 6},
		{"target-port", required_argument, NULL, 7},
		{"post-data-schema-only", no_argument, &postDataSchemaOnly, 1},
		{"dd_boost_file", required_argument, NULL, 8},
		{"dd_boost_enabled", no_argument, NULL, 9},
		{"dd_boost_dir", required_argument, NULL, 10},
		{"dd_boost_buf_size", required_argument, NULL, 11},
		{"gp-f", required_argument, NULL, 12},
		{"prefix", required_argument, NULL, 13},
		{"status", required_argument, NULL, 14},
		{"netbackup-service-host", required_argument, NULL, 15},
		{"netbackup-block-size", required_argument, NULL, 16},
		{NULL, 0, NULL, 0}
	};

	set_pglocale_pgservice(argv[0], "pg_dump");

	opts = NewRestoreOptions();

	/* set format default */
	opts->formatName = "p";

	progname = get_progname(argv[0]);

	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-?") == 0)
		{
			usage(progname);
			exit(0);
		}
		if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)
		{
			puts("pg_restore (Greenplum Database) " PG_VERSION);
			exit(0);
		}
	}

#ifdef USE_DDBOOST
	dd_boost_enabled = 0;
#endif
	while ((c = getopt_long(argc, argv, "acCd:ef:F:h:iI:lL:Op:P:RsS:t:T:uU:vwWxX:",
							cmdopts, NULL)) != -1)
	{
		switch (c)
		{
			case 'a':			/* Dump data only */
				opts->dataOnly = 1;
				break;
			case 'c':			/* clean (i.e., drop) schema prior to create */
				opts->dropSchema = 1;
				break;
			case 'C':
				opts->createDB = 1;
				break;
			case 'd':
				opts->dbname = strdup(optarg);
				break;
			case 'e':
				opts->exit_on_error = true;
				break;
			case 'f':			/* output file name */
				opts->filename = strdup(optarg);
				break;
			case 'F':
				if (strlen(optarg) != 0)
					opts->formatName = strdup(optarg);
				break;
			case 'h':
				if (strlen(optarg) != 0)
					opts->pghost = strdup(optarg);
				break;
			case 'i':
				/* obsolete option */
				break;

			case 'l':			/* Dump the TOC summary */
				opts->tocSummary = 1;
				break;

			case 'L':			/* input TOC summary file name */
				opts->tocFile = strdup(optarg);
				break;

			case 'O':
				opts->noOwner = 1;
				break;
			case 'p':
				if (strlen(optarg) != 0)
					opts->pgport = strdup(optarg);
				break;
			case 'R':
				/* no-op, still accepted for backwards compatibility */
				break;
			case 'P':			/* Function */
				opts->selTypes = 1;
				opts->selFunction = 1;
				opts->functionNames = strdup(optarg);
				break;
			case 'I':			/* Index */
				opts->selTypes = 1;
				opts->selIndex = 1;
				opts->indexNames = strdup(optarg);
				break;
			case 'T':			/* Trigger */
				opts->selTypes = 1;
				opts->selTrigger = 1;
				opts->triggerNames = strdup(optarg);
				break;
			case 's':			/* dump schema only */
				opts->schemaOnly = 1;
				break;
			case 'S':			/* Superuser username */
				if (strlen(optarg) != 0)
					opts->superuser = strdup(optarg);
				break;
			case 't':			/* Dump data for this table only */
				opts->selTypes = 1;
				opts->selTable = 1;
				opts->tableNames = strdup(optarg);
				break;

			case 'u':
				opts->promptPassword = TRI_YES;
				opts->username = simple_prompt("User name: ", 100, true);
				break;

			case 'U':
				opts->username = optarg;
				break;

			case 'v':			/* verbose */
				opts->verbose = 1;
				break;

			case 'w':
				opts->promptPassword = TRI_NO;
				break;

			case 'W':
				opts->promptPassword = TRI_YES;
				break;

			case 'x':			/* skip ACL dump */
				opts->aclsSkip = 1;
				break;

			case 'X':
				/* -X is a deprecated alternative to long options */
				if (strcmp(optarg, "use-set-session-authorization") == 0)
					use_setsessauth = 1;
				else if (strcmp(optarg, "disable-triggers") == 0)
					disable_triggers = 1;
				else
				{
					fprintf(stderr,
							_("%s: invalid -X option -- %s\n"),
							progname, optarg);
					fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
					exit(1);
				}
				break;

			case 0:
				/* This covers the long options equivalent to -X xxx. */
				break;

			case 1:				/* --gp-d MPP Output Directory */
				g_pszMPPOutputDirectory = strdup(optarg);
				break;

			case 2:				/* --gp-e On Error Stop for psql */
				g_bOnErrorStop = opts->exit_on_error = true;
				break;

			case 3:				/* --gp-k MPP Dump Info Format is
								 * Key_s-dbid_s-role_t-dbid */
				g_gpdumpInfo = strdup(optarg);
				if (!ParseCDBDumpInfo(progname, g_gpdumpInfo, &g_gpdumpKey, &g_role, &g_sourceDBID, &g_MPPPassThroughCredentials))
				{
					exit(1);
				}
				break;
			case 4:				/* gp-c */
				g_compPg = strdup(optarg);
				bCompUsed = true;		/* backup data is coming from stdin
										 * (piped from gunzip) */
				break;
			case 5:				/* target-dbid */
				g_targetDBID = atoi(strdup(optarg));
				break;
			case 6:				/* target-host */
				g_targetHost = strdup(optarg);
				break;
			case 7:				/* target-port */
				g_targetPort = strdup(optarg);
				break;
#ifdef USE_DDBOOST
			case 9:
				dd_boost_enabled = 1;
				break;
			case 10:
				dd_boost_dir = strdup(optarg);
				break;
			case 11:
				dd_boost_buf_size = strdup(optarg);
				break;
#endif
			case 12:
				table_filter_file = strdup(optarg);
				break;

			case 13:
				dump_prefix = strdup(optarg);
				break;
            /* Hack to pass in the status_file name to cdbbackup.c (gp_restore_launch) */
			case 14:
				break;
			case 15:
				netbackup_service_host = strdup(optarg);
				break;
			case 16:
				netbackup_block_size = strdup(optarg);
				break;


			default:
				fprintf(stderr, _("Try \"%s --help\" for more information.\n"), progname);
				exit(1);
		}
	}

	/* backup file name */

	/* TODO: use findAcceptableBackupFilePathName(...) to look for the file name
	 *       if user invoked gp_restore_agent directly without supplying a file name.
	 *       If the agent is invoked from gp_restore_launch, then we are ok.
	 */
	if (optind < argc)
		inputFileSpec = argv[optind];
	else
		inputFileSpec = NULL;

	if (postDataSchemaOnly && inputFileSpec != NULL)
	{
		if (strstr(inputFileSpec,"_post_data") == NULL)
		{
			fprintf(stderr,"Adding _post_data to the end of the file name?\n");
			char * newFS = malloc(strlen(inputFileSpec) + strlen("_post_data") + 1);
			strcpy(newFS, inputFileSpec);
			strcat(newFS, "_post_data");
			inputFileSpec = newFS;
		}
	}

	/* Should get at most one of -d and -f, else user is confused */
	if (opts->dbname)
	{
		if (opts->filename)
		{
			fprintf(stderr, _("%s: cannot specify both -d and -f output\n"),
					progname);
			fprintf(stderr, _("Try \"%s --help\" for more information.\n"),
					progname);
			exit(1);
		}
		opts->useDB = 1;
	}

	opts->disable_triggers = disable_triggers;
	opts->use_setsessauth = use_setsessauth;

	if (opts->formatName)
	{

		switch (opts->formatName[0])
		{

			case 'c':
			case 'C':
				opts->format = archCustom;
				break;

			case 'f':
			case 'F':
				opts->format = archFiles;
				break;

			case 't':
			case 'T':
				opts->format = archTar;
				break;

			case 'p':
			case 'P':
				bUsePSQL = true;
				break;

			default:
				mpp_err_msg(logInfo, progname, "unrecognized archive format '%s'; please specify 't' or 'c'\n",
							opts->formatName);
				exit(1);
		}
	}

	if (g_gpdumpInfo == NULL)
	{
		mpp_err_msg(logInfo, progname, "missing required parameter gp-k (backup key)\n");
		exit(1);
	}

#ifdef USE_DDBOOST
	if (dd_boost_enabled)
	{
		/* remote is always false when restoring from primary DDR */
		int err = DD_ERR_NONE;
		err = ddp_init("gp_dump");
		if (err != DD_ERR_NONE)
		{
			mpp_err_msg("ERROR", "ddboost", "ddboost init failed. Err = %d\n", err);
			exit(1);
		}

		if (initDDSystem(&ddp_inst, &ddp_conn, &dd_client_info, &DDP_SU_NAME, false, &DEFAULT_BACKUP_DIRECTORY, false))
               	{
			mpp_err_msg(logInfo, progname, "Initializing DD system failed\n");
			exit(1);
		}
                
		mpp_err_msg(logInfo, progname, "ddboost is initialized\n");
		
     		if (!postDataSchemaOnly)
		{       
               		ddp_file_name = formDDBoostFileName(g_gpdumpKey, false, dd_boost_dir);
               		if (ddp_file_name == NULL)
               		{
				mpp_err_msg(logInfo, progname, "Error in opening ddboost file\n");
				exit(1);
               		}	       

       		}
	}
#endif

	SegDB.dbid = g_sourceDBID;
	SegDB.role = g_role;
	SegDB.port = opts->pgport ? atoi(opts->pgport) : 5432;
	SegDB.pszHost = opts->pghost ? strdup(opts->pghost) : NULL;
	SegDB.pszDBName = opts->dbname ? strdup(opts->dbname) : NULL;
	SegDB.pszDBUser = opts->username ? strdup(opts->username) : NULL;
	SegDB.pszDBPswd = NULL;

	if (g_MPPPassThroughCredentials != NULL && *g_MPPPassThroughCredentials != '\0')
	{
		unsigned int nBytes;
		char	   *pszDBPswd = Base64ToData(g_MPPPassThroughCredentials, &nBytes);

		if (pszDBPswd == NULL)
		{
			mpp_err_msg(logError, progname, "Invalid Greenplum DB Credentials:  %s\n", g_MPPPassThroughCredentials);
			exit(1);
		}
		if (nBytes > 0)
		{
			SegDB.pszDBPswd = malloc(nBytes + 1);
			if (SegDB.pszDBPswd == NULL)
			{
				mpp_err_msg(logInfo, progname, "Cannot allocate memory for Greenplum Database Credentials\n");
				exit(1);
			}

			memcpy(SegDB.pszDBPswd, pszDBPswd, nBytes);
			SegDB.pszDBPswd[nBytes] = '\0';
		}
	}

	if (g_role == ROLE_MASTER)
		g_conn = MakeDBConnection(&SegDB, true);
	else
		g_conn = MakeDBConnection(&SegDB, false);
	if (PQstatus(g_conn) == CONNECTION_BAD)
	{
		exit_horribly(NULL, NULL, "connection to database \"%s\" failed: %s",
					  PQdb(g_conn), PQerrorMessage(g_conn));
	}

	if (g_gpdumpKey != NULL)
	{
		/*
		 * Open the database again, for writing status info
		 */
		g_conn_status = MakeDBConnection(&SegDB, false);

		if (PQstatus(g_conn_status) == CONNECTION_BAD)
		{
			exit_horribly(NULL, NULL, "Connection on host %s failed: %s",
						  StringNotNull(SegDB.pszHost, "localhost"),
						  PQerrorMessage(g_conn_status));
		}

		g_main_tid = pthread_self();

		g_pStatusOpList = CreateStatusOpList();
		if (g_pStatusOpList == NULL)
		{
			exit_horribly(NULL, NULL, "cannot allocate memory for gp_backup_status operation\n");
		}

		/*
		 * Create thread for monitoring for cancel requests.  If we're running
		 * using PSQL, the monitor is not allowed to start until the worker
		 * process is forked.  This is done to prevent the forked process from
		 * being blocked by locks held by library routines (__tz_convert, for
		 * example).
		 */
		if (bUsePSQL)
		{
			pthread_mutex_lock(&g_threadSyncPoint);
		}
		pthread_create(&g_monitor_tid,
					   NULL,
					   monitorThreadProc,
					   NULL);

		/* Install Ctrl-C interrupt handler, now that we have a connection */
		if (!bUsePSQL)
		{
			act.sa_handler = myHandler;
			sigemptyset(&act.sa_mask);
			act.sa_flags = 0;
			act.sa_flags |= SA_RESTART;
			if (sigaction(SIGINT, &act, NULL) < 0)
			{
				mpp_err_msg(logInfo, progname, "Error trying to set SIGINT interrupt handler\n");
			}

			act.sa_handler = myHandler;
			sigemptyset(&act.sa_mask);
			act.sa_flags = 0;
			act.sa_flags |= SA_RESTART;
			if (sigaction(SIGTERM, &act, NULL) < 0)
			{
				mpp_err_msg(logInfo, progname, "Error trying to set SIGTERM interrupt handler\n");
			}
		}

		pOp = CreateStatusOp(TASK_START, TASK_RC_SUCCESS, SUFFIX_START, TASK_MSG_SUCCESS);
		if (pOp == NULL)
		{
			exit_horribly(NULL, NULL, "cannot allocate memory for gp_backup_status operation\n");
		}
		AddToStatusOpList(g_pStatusOpList, pOp);
	}
	/* end cdb additions */

	if (bUsePSQL)
	{
		/* Install Ctrl-C interrupt handler, now that we have a connection */
		act.sa_handler = psqlHandler;
		sigemptyset(&act.sa_mask);
		act.sa_flags = 0;
		act.sa_flags |= SA_RESTART;
		if (sigaction(SIGINT, &act, NULL) < 0)
		{
			mpp_err_msg(logInfo, progname, "Error trying to set SIGINT interrupt handler\n");
		}

		act.sa_handler = psqlHandler;
		sigemptyset(&act.sa_mask);
		act.sa_flags = 0;
		act.sa_flags |= SA_RESTART;
		if (sigaction(SIGTERM, &act, NULL) < 0)
		{
			mpp_err_msg(logInfo, progname, "Error trying to set SIGTERM interrupt handler\n");
		}

		/* Establish a SIGCHLD handler to catch termination the psql process */
		act.sa_handler = myChildHandler;
		sigemptyset(&act.sa_mask);
		act.sa_flags = 0;
		act.sa_flags |= SA_RESTART;
		if (sigaction(SIGCHLD, &act, NULL) < 0)
		{
			mpp_err_msg(logInfo, progname, "Error trying to set SIGCHLD interrupt handler\n");
			exit(1);
		}

		mpp_err_msg(logInfo, progname, "Before fork of gp_restore_agent\n");

		newpid = fork();
		if (newpid < 0)
		{
			mpp_err_msg(logError, progname, "Failed to fork\n");
		}
		else if (newpid == 0)
		{
			char	   *psqlPg = NULL;
			char	   *filterScript = NULL;
			char	   *postDataFilterScript = NULL;
			char	   *catPg = NULL;
			char	   *gpNBURestorePg = NULL;

			pszOnErrorStop = "-v ON_ERROR_STOP=";

			/* Child Process */
			/* launch psql, wait for it to finish */

			/* find psql in PATH or PGPATH */
			if ((psqlPg = testProgramExists("psql")) == NULL)
			{
				mpp_err_msg(logError, progname, "psql not found in path");
				exit(1);
			}

			if ((filterScript = testProgramExists("gprestore_filter.py")) == NULL)
			{
				mpp_err_msg(logError, progname, "Restore filter script not found in path");
				exit(1);
			}

			if ((postDataFilterScript = testProgramExists("gprestore_post_data_filter.py")) == NULL)
			{
				mpp_err_msg(logError, progname, "Restore post data filter script not found in path");
				exit(1);
			}

			if ((catPg = testProgramExists("cat")) == NULL)
			{
				mpp_err_msg(logError, progname, "cat program not found in path");
				exit(1);
			}

			if ((gpNBURestorePg = testProgramExists("gp_bsa_restore_agent")) == NULL)
			{
				mpp_err_msg(logError, progname, "NetBackup restore agent \"gp_bsa_restore_agent\" not found in path");
				exit(1);
			}

			if (bCompUsed)
			{
				g_compPg = formCompressionProgramString(g_compPg);
				if (!g_compPg)
				{
					mpp_err_msg(logError, progname, "Could not allocate memory %s:%d", __FILE__, __LINE__);
					exit(1);
				}
			}

#ifdef USE_DDBOOST
			/* find if gpddboost is present in PATH or PGPATH */
			if ((ddboostPg = testProgramExists("gpddboost")) == NULL)
			{
				mpp_err_msg(logError, progname, "gpddboost not found in path\n");
			}
#endif

			/* add all the psql args to the command string */
			/* Its too error prone to pre-calc the exact command line length
			   just allocate a chunk of memory that is not likely to be exceeded. */
			pszCmdLine = (char *) calloc(MAX_COMMANDLINE_LEN, 1);

			if (!postDataSchemaOnly)
			{
#ifdef USE_DDBOOST
				if (dd_boost_enabled)
				{
					formDDBoostPsqlCommandLine(&pszCmdLine, bCompUsed, ddboostPg, g_compPg, 
							ddp_file_name, dd_boost_buf_size,
							filterScript, table_filter_file, 
							g_role, psqlPg);
				}
				else 
				{
#endif
					/* Non ddboost restore */
					formSegmentPsqlCommandLine(&pszCmdLine, inputFileSpec, bCompUsed, g_compPg,
							filterScript, table_filter_file, 
							g_role, psqlPg, catPg,
							gpNBURestorePg, netbackup_service_host, netbackup_block_size);
#ifdef USE_DDBOOST
				}
#endif
			}
			else if (postDataSchemaOnly)
			{
                /* Right now the postdata files will be synced to segment directories before restore */
                formPostDataSchemaOnlyPsqlCommandLine(&pszCmdLine, inputFileSpec, bCompUsed, g_compPg,
							postDataFilterScript, table_filter_file, psqlPg, catPg,
							gpNBURestorePg, netbackup_service_host, netbackup_block_size);
			}

			strcat(pszCmdLine, " -h ");
			strcat(pszCmdLine, g_targetHost);
			strcat(pszCmdLine, " -p ");
			strcat(pszCmdLine, g_targetPort);
			strcat(pszCmdLine, " -U ");
			strcat(pszCmdLine, SegDB.pszDBUser);
			strcat(pszCmdLine, " -d ");
			strcat(pszCmdLine, SegDB.pszDBName);
			strcat(pszCmdLine, " -a ");

			if (g_bOnErrorStop)
			{
				strcat(pszCmdLine, " ");
				strcat(pszCmdLine, pszOnErrorStop);
			}

			if (g_role == ROLE_SEGDB)
				putenv("PGOPTIONS=-c gp_session_role=UTILITY");
			if (g_role == ROLE_MASTER)
				putenv("PGOPTIONS=-c gp_session_role=DISPATCH");

			mpp_err_msg(logInfo, progname, "Command Line: %s\n", pszCmdLine);

			/*
			 * Make this new process the process group leader of the children
			 * being launched.	This allows a signal to be sent to all
			 * processes in the group simultaneously.
			 */
			setpgid(newpid, newpid);

			execl("/bin/sh", "sh", "-c", pszCmdLine, NULL);

			mpp_err_msg(logInfo, progname, "Error in gp_restore_agent - execl of %s with Command Line %s failed",
						"/bin/sh", pszCmdLine);

			_exit(127);
		}
		else
		{
			/*
			 * Make the new child process the process group leader of the
			 * children being launched.  This allows a signal to be sent to
			 * all processes in the group simultaneously.
			 *
			 * This is a redundant call to avoid a race condition suggested by
			 * Stevens.
			 */
			setpgid(newpid, newpid);

			/* Allow the monitor thread to begin execution. */
			pthread_mutex_unlock(&g_threadSyncPoint);

			/* Parent .  Lets sleep and wake up until we see it's done */
			while (!bPSQLDone)
			{
				sleep(5);
			}

			/*
			 * If this process has been sent a SIGINT or SIGTERM, we need to
			 * send a SIGINT to the psql process GROUP.
			 */
			if (bKillPsql)
			{
				mpp_err_msg(logInfo, progname, "Terminating psql due to signal.\n");
				kill(-newpid, SIGINT);
			}

			waitpid(newpid, &status, 0);
			if (WIFEXITED(status))
			{
				rc = WEXITSTATUS(status);
				if (rc == 0)
				{
					mpp_err_msg(logInfo, progname, "psql finished with rc %d.\n", rc);
					/* Normal completion falls to end of routine. */
				}
				else
				{
					if (rc >= 128)
					{
						/*
						 * If the exit code has the 128-bit set, the exit code
						 * represents a shell exited by signal where the
						 * signal number is exitCode - 128.
						 */
						rc -= 128;
						pszErrorMsg = MakeString("psql finished abnormally with signal number %d.\n", rc);
					}
					else
					{
						pszErrorMsg = MakeString("psql finished abnormally with return code %d.\n", rc);
					}
					makeSureMonitorThreadEnds(TASK_RC_FAILURE, pszErrorMsg);
					free(pszErrorMsg);
					exit_code = 2;
				}
			}
			else if (WIFSIGNALED(status))
			{
				pszErrorMsg = MakeString("psql finished abnormally with signal number %d.\n", WTERMSIG(status));
				mpp_err_msg(logError, progname, pszErrorMsg);
				makeSureMonitorThreadEnds(TASK_RC_FAILURE, pszErrorMsg);
				free(pszErrorMsg);
				exit_code = 2;
			}
			else
			{
				pszErrorMsg = MakeString("psql crashed or finished badly; status=%#x.\n", status);
				mpp_err_msg(logError, progname, pszErrorMsg);
				makeSureMonitorThreadEnds(TASK_RC_FAILURE, pszErrorMsg);
				free(pszErrorMsg);
				exit_code = 2;
			}
		}
	}
	else
	{
		AH = OpenArchive(inputFileSpec, opts->format);

		/* Let the archiver know how noisy to be */
		AH->verbose = opts->verbose;

		/*
	     * Whether to keep submitting sql commands as "pg_restore ... | psql ... "
		 */
		AH->exit_on_error = opts->exit_on_error;

		if (opts->tocFile)
			SortTocFromFile(AH, opts);

		if (opts->tocSummary)
			PrintTOCSummary(AH, opts);
		else
		{
			pAH = (ArchiveHandle *) AH;

			if (opts->useDB)
			{
				/* check for version mismatch */
				if (pAH->version < K_VERS_1_3)
					die_horribly(NULL, NULL, "direct database connections are not supported in pre-1.3 archives\n");

				pAH->connection = g_conn;
				/* XXX Should get this from the archive */
				AH->minRemoteVersion = 070100;
				AH->maxRemoteVersion = 999999;

				_check_database_version(pAH);
			}

			RestoreArchive(AH, opts);

			/*
			 * The following is necessary when the -C option is used.  A new
			 * connection is gotten to the database within RestoreArchive
			 */
			if (pAH->connection != g_conn)
				g_conn = pAH->connection;
		}

		/* done, print a summary of ignored errors */
		if (AH->n_errors)
			fprintf(stderr, _("WARNING: errors ignored on restore: %d\n"),
					AH->n_errors);

		/* AH may be freed in CloseArchive? */
		exit_code = AH->n_errors ? 1 : 0;

		CloseArchive(AH);
	}

#ifdef USE_DDBOOST
	if(dd_boost_enabled)
		cleanupDDSystem();
#endif

	makeSureMonitorThreadEnds(TASK_RC_SUCCESS, TASK_MSG_SUCCESS);

	DestroyStatusOpList(g_pStatusOpList);

	if (SegDB.pszHost)
		free(SegDB.pszHost);
	if (SegDB.pszDBName)
		free(SegDB.pszDBName);
	if (SegDB.pszDBUser)
		free(SegDB.pszDBUser);
	if (SegDB.pszDBPswd)
		free(SegDB.pszDBPswd);

	PQfinish(g_conn);
	if (exit_code == 0)
		mpp_err_msg(logInfo, progname, "Finished successfully\n");
	else
		mpp_err_msg(logError, progname, "Finished with errors\n");

	return exit_code;
}

static void
usage(const char *progname)
{
	printf(_("%s restores a PostgreSQL database from an archive created by pg_dump.\n\n"), progname);
	printf(_("Usage:\n"));
	printf(_("  %s [OPTION]... [FILE]\n"), progname);

	printf(_("\nGeneral options:\n"));
	printf(_("  -d, --dbname=NAME        connect to database name\n"));
	printf(_("  -f, --file=FILENAME      output file name\n"));
	printf(_("  -F, --format=c|t         specify backup file format\n"));
	printf(_("  -i, --ignore-version     proceed even when server version mismatches\n"));
	printf(_("  -l, --list               print summarized TOC of the archive\n"));
	printf(_("  -v, --verbose            verbose mode\n"));
	printf(_("  --help                   show this help, then exit\n"));
	printf(_("  --version                output version information, then exit\n"));

	printf(_("\nOptions controlling the restore:\n"));
	printf(_("  -a, --data-only          restore only the data, no schema\n"));
	printf(_("  -c, --clean              clean (drop) schema prior to create\n"));
	printf(_("  -C, --create             create the target database\n"));
	printf(_("  -I, --index=NAME         restore named index\n"));
	printf(_("  -L, --use-list=FILENAME  use specified table of contents for ordering\n"
			 "                           output from this file\n"));
	printf(_("  -O, --no-owner           skip restoration of object ownership\n"));
	printf(_("  -P, --function='NAME(args)'\n"
			 "                           restore named function. name must be exactly\n"
			 "                           as appears in the TOC, and inside single quotes\n"));
	printf(_("  -s, --schema-only        restore only the schema, no data\n"));
	printf(_("  -S, --superuser=NAME     specify the superuser user name to use for\n"
			 "                           disabling triggers\n"));
	printf(_("  -t, --table=NAME         restore named table\n"));
	printf(_("  -T, --trigger=NAME       restore named trigger\n"));
	printf(_("  -x, --no-privileges      skip restoration of access privileges (grant/revoke)\n"));
	printf(_("  --disable-triggers       disable triggers during data-only restore\n"));
	printf(_("  --use-set-session-authorization\n"
			 "                           use SESSION AUTHORIZATION commands instead of\n"
			 "                           OWNER TO commands\n"));

	printf(_("\nConnection options:\n"));
	printf(_("  -h, --host=HOSTNAME      database server host or socket directory\n"));
	printf(_("  -p, --port=PORT          database server port number\n"));
	printf(_("  -U, --username=NAME      connect as specified database user\n"));
	printf(_("  -W, --password           force password prompt (should happen automatically)\n"));
	printf(_("  -e, --exit-on-error      exit on error, default is to continue\n"));

	printf(("\nGreenplum Database specific options:\n"));
	printf(("   --gp-k=BACKUPKEY        key for Greenplum Database Backup\n"));
	printf(("   --gp-f=TABLEFILTERFILE  schema.tables to be restored are added to this file\n"));
	printf(("   --post-data-schema-only restore schema only from special post-data file\n"));

	printf(_("\nIf no input file name is supplied, then standard input is used.\n\n"));
	printf(_("Report bugs to <pgsql-bugs@postgresql.org>.\n"));
}


void
myChildHandler(int signo)
{
	bPSQLDone = true;
}


/*
 * psqlHandler: This function is the signal handler for SIGINT and SIGTERM when a psql
 * process is used to restore data.  This process
 */
static void
psqlHandler(int signo)
{
	bKillPsql = true;
	bPSQLDone = true;
}


/*
 * makeSureMonitorThreadEnds: This function adds a request for a TASK_FINISH
 * status row, and then waits for the g_monitor_tid to end.
 * This will happen as soon as the monitor thread processes this request.
 */
void
makeSureMonitorThreadEnds(int TaskRC, char *pszErrorMsg)
{
	StatusOp   *pOp;

	if (g_monitor_tid)
	{
		char	   *pszSuffix;

		if (TaskRC == TASK_RC_SUCCESS)
			pszSuffix = SUFFIX_SUCCEED;
		else
			pszSuffix = SUFFIX_FAIL;

		pOp = CreateStatusOp(TASK_FINISH, TaskRC, pszSuffix, pszErrorMsg);
		if (pOp == NULL)
		{
			/*
			 * This is really bad, since the only way we cab shut down
			 * gracefully is if we can insert a row into the ststus table.	Oh
			 * well, at least log a message and exit.
			 */
			mpp_err_msg(logError, progname, "*** aborted because of fatal memory allocation error\n");
			exit(1);
		}
		else
			AddToStatusOpList(g_pStatusOpList, pOp);

		pthread_join(g_monitor_tid, NULL);
	}
}

/*
 * monitorThreadProc: This function runs in a thread and owns the other connection to the database. g_conn_status
 * It listens using this connection for notifications.	A notification only arrives if we are to
 * cancel. In that case, we cause the interrupt handler to be called by sending ourselves a SIGINT
 * signal.	The other duty of this thread, since it owns the g_conn_status, is to periodically examine
 * a linked list of gp_backup_status insert command requests.  If any are there, it causes an insert and a notify
 * to be sent.
 */
void *
monitorThreadProc(void *arg __attribute__((unused)))
{
	int			sock;
	bool		bGotFinished;
	bool		bGotCancelRequest;
	int			PID;
	int			nNotifies;
	bool		bFromOtherProcess;
	StatusOp   *pOp;
	char	   *pszMsg;
	PGnotify   *notify;
	sigset_t	newset;
	struct pollfd *pollInput;
	int			pollResult = 0;
	int			pollTimeout;

	/* Block until parent thread is ready for us to run. */
	pthread_mutex_lock(&g_threadSyncPoint);
	pthread_mutex_unlock(&g_threadSyncPoint);

	/* Block SIGINT and SIGKILL signals (we want them only ion main thread) */
	sigemptyset(&newset);
	sigaddset(&newset, SIGINT);
	sigaddset(&newset, SIGKILL);
	pthread_sigmask(SIG_SETMASK, &newset, NULL);

	/* mpp_msg(NULL, "Starting monitor thread at %s\n", GetTimeNow() ); */
	mpp_err_msg(logInfo, progname, "Starting monitor thread\n");

	/* Issue Listen command  */
	DoCancelNotifyListen(g_conn_status, true, g_gpdumpKey, g_role, g_sourceDBID, g_targetDBID, NULL);

	/* Now wait for a cancel notification */
	sock = PQsocket(g_conn_status);
	bGotFinished = false;
	bGotCancelRequest = false;
	PID = PQbackendPID(g_conn_status);

	pollInput = (struct pollfd *)malloc(sizeof(struct pollfd));

	/* Once we've seen the TASK_FINISH insert request, we know to leave */
	while (!bGotFinished)
	{
		/* Replacing select() by poll() here to overcome the limitations of 
		select() to handle large socket file descriptor values.
		*/

		pollInput->fd = sock;
		pollInput->events = POLLIN;
		pollInput->revents = 0; 
		pollTimeout = 2000;
		pollResult = poll(pollInput, 1, pollTimeout);

		if(pollResult < 0) 
		{
			mpp_err_msg(logError, progname, "poll failed for backup key %s, instid %d, segid %d failed\n",
						g_gpdumpKey, g_role, g_sourceDBID);
			bGotCancelRequest = true;
		}

		if (PQstatus(g_conn_status) == CONNECTION_BAD)
		{
			mpp_err_msg(logError, progname, "Status Connection went down for backup key %s, instid %d, segid %d\n",
						g_gpdumpKey, g_role, g_sourceDBID);
			bGotCancelRequest = true;
		}

		PQconsumeInput(g_conn_status);
		nNotifies = 0;
		bFromOtherProcess = false;
		while (NULL != (notify = PQnotifies(g_conn_status)))
		{
			nNotifies++;

			/*
			 * Need to make sure that the notify is from another process,
			 * since this processes also issues notifies that aren't cancel
			 * requests.
			 */
			if (notify->be_pid != PID)
				bFromOtherProcess = true;

			PQfreemem(notify);
		}

		if (nNotifies > 0 && bFromOtherProcess)
			bGotCancelRequest = true;

		if (bGotCancelRequest)
		{
			mpp_err_msg(logInfo, progname, "Notification received that we need to cancel for backup key %s, instid %d, segid %d failed\n",
						g_gpdumpKey, g_role, g_sourceDBID);

			pthread_kill(g_main_tid, SIGINT);
		}

		/* Now deal with any insert requests */
		Lock(g_pStatusOpList);
		while (NULL != (pOp = TakeFromStatusOpList(g_pStatusOpList)))
		{
			DoCancelNotifyListen(g_conn_status, false, g_gpdumpKey, g_role, g_sourceDBID, g_targetDBID, pOp->pszSuffix);

			if (pOp->TaskID == TASK_FINISH)
			{
				bGotFinished = true;

				if (pOp->TaskRC == TASK_RC_SUCCESS)
				{
					pszMsg = "Succeeded\n";
					mpp_err_msg(logInfo, progname, pszMsg);
				}
				else
				{
					pszMsg = pOp->pszMsg;
					mpp_err_msg(logError, progname, pszMsg);
				}
			}

			if (pOp->TaskID == TASK_SET_SERIALIZABLE)
			{
				mpp_err_msg(logInfo, progname, "TASK_SET_SERIALIZABLE");
			}

			FreeStatusOp(pOp);
		}

		Unlock(g_pStatusOpList);
	}

	/* Close the g_conn_status connection. */
	PQfinish(g_conn_status);

	return NULL;
}


/*
 * my_handler: This function is the signal handler for both SIGINT and SIGTERM signals.
 * It checks to see whether there is an active transaction running on the g_conn connection
 * If so, it issues a PQrequestCancel.	This will cause the current statement to fail, which will
 * cause gp_dump to terminate gracefully.
 * If there's no active transaction, it does nothing.
 * This is set up to always run in the context of the main thread.
 */
void
myHandler(int signo)
{
	if (g_conn != NULL && PQtransactionStatus(g_conn) == PQTRANS_ACTIVE)
	{
		mpp_err_msg(logInfo, progname, "in my_handler before PQrequestCancel\n");
		PQrequestCancel(g_conn);
	}
	else
	{
		mpp_err_msg(logInfo, progname, "in my_handler not in active transaction\n");
	}
}

/*
 * _check_database_version: This was copied here from pg_backup_db.c because it's declared static there.
 * We need to call it directly because in pg_dump.c it's called from ConnectDatabase
 * which we are not using.
 */
void
_check_database_version(ArchiveHandle *AH)
{
	int			myversion;
	const char *remoteversion_str;
	int			remoteversion;

	myversion = parse_version(PG_VERSION);
	if (myversion < 0)
		die_horribly(AH, NULL, "could not parse version string \"%s\"\n", PG_VERSION);

	remoteversion_str = PQparameterStatus(AH->connection, "server_version");
	if (!remoteversion_str)
		die_horribly(AH, NULL, "could not get server_version from libpq\n");

	remoteversion = parse_version(remoteversion_str);
	if (remoteversion < 0)
		die_horribly(AH, NULL, "could not parse version string \"%s\"\n", remoteversion_str);

	AH->public.remoteVersion = remoteversion;

	if (myversion != remoteversion
		&& (remoteversion < AH->public.minRemoteVersion ||
			remoteversion > AH->public.maxRemoteVersion))
	{
		mpp_err_msg(logWarn, progname, "server version: %s; %s version: %s\n",
					remoteversion_str, progname, PG_VERSION);
		die_horribly(AH, NULL, "aborting because of version mismatch\n");
	}
}

/*
 * execMPPCatalogFunction:
 */
/*
bool execMPPCatalogFunction(PGconn* pConn)
{
	const char* pszFunctionName		= "restore_gp_catalog";
	const char* pszNamespaceName	= "mpp";
	bool		bRtn				= false;
	PQExpBuffer bufQry				= NULL;
	PGresult*	pRes				= NULL;
	int ntups;

	bufQry = createPQExpBuffer();

	appendPQExpBuffer( bufQry, "SELECT count(*) from pg_catalog.pg_proc p"
								" inner join pg_catalog.pg_namespace n"
								" on p.pronamespace=n.oid"
								" where p.proname='%s'"
								" and n.nspname='%s'",
						pszFunctionName, pszNamespaceName );

	pRes = PQexec(pConn, bufQry->data);
	if (!pRes || PQresultStatus(pRes) != PGRES_TUPLES_OK)
	{
		mpp_err_msg("Query %s Command failed: %s",
					bufQry->data, PQerrorMessage(pConn));
		goto cleanup;
	}

	ntups = PQntuples(pRes);
	if ( ntups == 0 )
	{
		mpp_err_msg("Query %s Command failed: %s",
					bufQry->data, PQerrorMessage(pConn));
		goto cleanup;
	}

	if ( 0 == atoi( PQgetvalue(pRes, 0, 0)))
	{
		mpp_err_msg("Cannot execute function %s.%s() because it does not exist",
					pszNamespaceName, pszFunctionName );
		goto cleanup;
	}

	PQclear(pRes);
	resetPQExpBuffer(bufQry);

	appendPQExpBuffer( bufQry, "SELECT %s.%s()",
						pszNamespaceName, pszFunctionName );

	pRes = PQexec(pConn, bufQry->data);
	if (!pRes || PQresultStatus(pRes) != PGRES_TUPLES_OK)
	{
		mpp_err_msg("Cannot execute function %s.%s() because it does not exist",
					bufQry->data, PQerrorMessage(pConn) );
		goto cleanup;
	}

	bRtn = true;

cleanup:
	if ( pRes != NULL )
		PQclear(pRes);
	if ( bufQry != NULL )
		destroyPQExpBuffer(bufQry);

	return bRtn;
}
*/

/* testProgramExists(char* pszProgramName) returns bool
 * This runs a shell with which pszProgramName > tempfile
 * piping the output to a temp file.
 * If the file is empty, then pszProgramName didn't exist
 * on the path.
 */
static char *
testProgramExists(char *pszProgramName)
{
	char	   *pszProgramNameLocal;
	char	   *p;

	/* Now see what the length of the file is  - 0 means not there */
	struct stat buf;
	char	   *pszEnvPath;
	char	   *pszPath;
	char	   *pColon;
	char	   *pColonNext;
	char	   *pszTestPath;

	if (pszProgramName == NULL || *pszProgramName == '\0')
		return NULL;

	/*
	 * The pszProgramName might have command line arguments, so we need to
	 * stop at the first whitespace
	 */
	pszProgramNameLocal = malloc(strlen(pszProgramName) + 1);
	if (pszProgramNameLocal == NULL)
	{
		mpp_err_msg(logError, progname, "out of memory");
	}

	strcpy(pszProgramNameLocal, pszProgramName);

	p = pszProgramNameLocal;
	while (*p != '\0')
	{
		if (*p == ' ' || *p == '\t' || *p == '\n')
			break;

		p++;
	}

	*p = '\0';

	pszEnvPath = getenv("PGPATH");
	if (pszEnvPath != NULL)
	{
		pszTestPath = (char *) malloc(strlen(pszEnvPath) + 1 + strlen(pszProgramNameLocal) + 1);
		if (pszTestPath == NULL)
		{
			mpp_err_msg(logError, progname, "out of memory in testProgramExists");
		}

		sprintf(pszTestPath, "%s/%s", pszEnvPath, pszProgramNameLocal);
		if (stat(pszTestPath, &buf) >= 0)
			return pszTestPath;
	}
	pszEnvPath = getenv("PATH");
	if (pszEnvPath == NULL)
		return NULL;

	pszPath = (char *) malloc(strlen(pszEnvPath) + 1);
	if (pszPath == NULL)
	{
		mpp_err_msg(logError, progname, "out of memory in testProgramExists");
	}

	strcpy(pszPath, pszEnvPath);

	/* Try to create each level of the hierarchy that doesn't already exist */
	pColon = pszPath;

	while (pColon != NULL && *pColon != '\0')
	{
		/* Find next delimiter */
		pColonNext = strchr(pColon, ':');

		if (pColonNext == pColon)
		{
			pColon++;
			continue;
		}

		if (pColonNext == NULL)
		{
			/* See whether pszProgramName exists in subdirectory pColon */
			pszTestPath = (char *) malloc(strlen(pColon) + 1 + strlen(pszProgramNameLocal) + 1);
			if (pszTestPath == NULL)
			{
				mpp_err_msg(logError, progname, "out of memory in testProgramExists");
			}

			sprintf(pszTestPath, "%s/%s", pColon, pszProgramNameLocal);
			if (stat(pszTestPath, &buf) >= 0)
				return pszTestPath;
			else
				return NULL;
		}

		/* Temporarily end the string at the : we just found. */
		*pColonNext = '\0';

		/* See whether pszProgramName exists in subdirectory pColon */
		pszTestPath = (char *) malloc(strlen(pColon) + 1 + strlen(pszProgramNameLocal) + 1);
		if (pszTestPath == NULL)
		{
			mpp_err_msg(logError, progname, "out of memory in testProgramExists");
		}

		sprintf(pszTestPath, "%s/%s", pColon, pszProgramNameLocal);
		if (stat(pszTestPath, &buf) >= 0)
		{
			*pColonNext = ':';
			return pszTestPath;
		}

		/* Put back the colon we overwrote above. */
		*pColonNext = ':';

		/* Advance past the colon. */
		pColon = pColonNext + 1;

		/* free */
		free(pszTestPath);
	}

	return NULL;
}

#ifdef USE_DDBOOST
static char *formDDBoostFileName(char *pszBackupKey, bool isPostData, char *dd_boost_dir)
{
        /* First form the prefix */
        char	szFileNamePrefix[1 + PATH_NAME_MAX];
        int     instid;                 	/* dispatch node */
        int     segid;
        int     len = 0;
        char    *pszBackupFileName;
	char 	*dir_name = "db_dumps";		/* default directory */
	
        instid = g_role;           		/* dispatch node */
        segid = g_sourceDBID;

       	memset(szFileNamePrefix, 0, (1+PATH_NAME_MAX));
	if (dd_boost_dir)
        	snprintf(szFileNamePrefix, 1 + PATH_NAME_MAX, "%s/%sgp_dump_%d_%d_", dd_boost_dir, DUMP_PREFIX, instid, segid);
	else	
        	snprintf(szFileNamePrefix, 1 + PATH_NAME_MAX, "%s/%sgp_dump_%d_%d_", dir_name, DUMP_PREFIX, instid, segid);

        /* Now add up the length of the pieces */
        len += strlen(szFileNamePrefix);
        len += strlen(pszBackupKey);

	if (isPostData)
		len += strlen("_post_data");
        if (len > PATH_NAME_MAX)
        {
               mpp_err_msg(logInfo, progname, "Length > MAX for filename\n");
               return NULL;
        }

        pszBackupFileName = (char *) malloc(sizeof(char) * (1 + len));
        if (pszBackupFileName == NULL)
        {
              mpp_err_msg(logInfo, progname, "Error out of memory\n");
              return NULL;
        }

       	memset(pszBackupFileName, 0, len + 1 );
 
        strcat(pszBackupFileName, szFileNamePrefix);
        strcat(pszBackupFileName, pszBackupKey);

        if (isPostData)
                strcat(pszBackupFileName, "_post_data");

        return pszBackupFileName;
}

int
cleanupDDSystem(void)
{
	int err = DD_ERR_NONE;

	/* Close the connection */
	err = ddp_disconnect(ddp_conn);
	if (err)
	{
		mpp_err_msg(logError, progname, "ddboost disconnect failed. Err = %d\n", err);
		return err;
	}

	err = ddp_instance_destroy(ddp_inst);
	if (err)
	{
		mpp_err_msg(logError, progname, "ddboost instance destroy failed. Err = %d\n", err);
		return err;
	}

	ddp_shutdown();
	return 0;
}

#endif
