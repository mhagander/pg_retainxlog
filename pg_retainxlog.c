/*-------------------------------------------------------------------------
 *
 * pg_retainxlog.c - check if a PostgreSQL xlog file is ready to be
 *						recycled using archive_Command
 *
 * Author: Magnus Hagander <magnus@hagander.net>
 *
 *-------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpq-fe.h>

#include <getopt.h>

/* prototypes */
static void usage(char *prog) __attribute__((noreturn));

/* commandline arguments */
static char *appname = NULL;
static char *appquery = NULL;


static void
usage(char *prog)
{
	printf("Usage: %s [options] <filename> <connectionstr> \n", prog);
	printf("  -a, --appname    Application name to look for\n");
	printf("  -q, --query      Custom query result to look for\n");
	printf("  --verbose        Verbose output\n");
	printf("  --help           Show help\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"appname", required_argument, NULL, 'a'},
		{"query", required_argument, NULL, 'q'},
		{"help", no_argument, NULL, '?'},
		{"verbose", no_argument, NULL, 'v'},
		{NULL, 0, NULL, 0}
	};
	int c;
	int option_index;
	int verbose = 0;
	PGconn *conn;
	PGresult *res;
	char *connstr, *filename;

	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-?") == 0)
			usage(argv[0]);
	}

	while ((c = getopt_long(argc, argv, "va:q:?", long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'a':
				appname = strdup(optarg);
				break;
			case 'q':
				appquery = strdup(optarg);
				break;
			case 'v':
				verbose = 1;
				break;
			case '?':
				usage(argv[0]);
			default:
				/* getopt_long already emitted complaint */
				exit(1);
		}
	}

	if (argc - optind != 2)
		usage(argv[0]);

	if (appname != NULL && appquery != NULL)
	{
		fprintf(stderr, "%s: cannot specify both appname and query!",
				argv[0]);
		usage(argv[0]);
	}

	connstr = argv[optind+1];
	filename = argv[optind];

	conn = PQconnectdb(connstr);
	if (!conn || PQstatus(conn) != CONNECTION_OK)
	{
		fprintf(stderr, "%s: could not connect to server: %s\n",
				argv[0], PQerrorMessage(conn));
		exit(1);
	}

	if (appquery)
		res = PQexec(conn, appquery);
	else
	{
		char query[1024];
		snprintf(query,
				 sizeof(query),
				 "SELECT write_location, pg_xlogfile_name(write_location) FROM pg_stat_replication WHERE application_name='%s'",
				 appname ? appname : "pg_receivexlog"
			);
		res = PQexec(conn, query);
	}

	if (!res || PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		fprintf(stderr, "%s: could not query for replication status: %s\n",
				argv[0], PQerrorMessage(conn));
		PQclear(res);
		PQfinish(conn);
		exit(1);
	}

	if (PQntuples(res) == 0)
	{
		fprintf(stderr, "%s: no pg_receivexlog replication clients active.\n",
				argv[0]);
		PQclear(res);
		PQfinish(conn);
		exit(1);
	}

	if (PQntuples(res) > 1)
	{
		fprintf(stderr, "%s: %i pg_receivexlog replication clients found, can only work with 1.\n",
				argv[0], PQntuples(res));
		PQclear(res);
		PQfinish(conn);
		exit(1);
	}

	if (PQnfields(res) != 2)
	{
		/* Can only happen for custom queries */
		fprintf(stderr, "%s: custom query returned %i fields, must be 2!\n",
				argv[0], PQnfields(res));
		PQclear(res);
		PQfinish(conn);
		exit(1);
	}

	/*
	 * Compare the returned filename with the one that we have been asked
	 * about. If the one we've been asked to archive is the same or newer
	 * than what's seen on the slave, it's not safe to archive it.
	 */
	if (strcmp(filename, PQgetvalue(res, 0, 1)) >= 0)
	{
		fprintf(stderr, "%s: current streamed position (%s, file %s) is older than archive file (%s), not ready to archive\n",
				argv[0], PQgetvalue(res, 0, 0), PQgetvalue(res, 0, 1), filename);
		PQclear(res);
		PQfinish(conn);
		exit(1);
	}
	if (verbose)
		printf("%s: file %s is ok to archive (current streaming pos is %s, file %s)\n",
			   argv[0], filename, PQgetvalue(res, 0, 0), PQgetvalue(res, 0, 1));

	PQclear(res);
	PQfinish(conn);

	return 0;
}
