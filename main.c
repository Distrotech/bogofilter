/* $Id$ */

/*****************************************************************************

NAME:
   main.c -- detect spam and bogons presented on standard input.

AUTHOR:
   Eric S. Raymond <esr@thyrsus.com>

******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <config.h>
#include "common.h"

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif

#include "lexer.h"
#include "bogofilter.h"
#include "bogoconfig.h"
#include "format.h"
#include "register.h"
#include "wordlists.h"
#include "xmalloc.h"

#define BOGODIR ".bogofilter"

#define	NL	"\n"
#define	CRLF	"\r\n"

extern int Rtable;

char msg_register[256];
char msg_bogofilter[256];
size_t msg_register_size = sizeof(msg_register);

const char *progname = "bogofilter";

/* if the given environment variable 'var' exists, create a path from it and
   tack on the optional 'subdir' value.
   return value: buffer address if successful
                 NULL if failure
 */
static char *create_path_from_env(const char *var,
		       /*@null@*/ const char *subdir)
{
    char *buff, *env;
    size_t path_size, env_size;

    env = getenv(var);
    if (env == NULL) return NULL;

    env_size = strlen(env);
    path_size = env_size + (subdir ? strlen(subdir) : 0) + 2;
    buff = xmalloc(path_size);

    strcpy(buff, env);
    if ('/' != buff[env_size-1]) {
	strcat(buff, "/");
    }
    if (subdir != NULL) {
	strcat(buff, subdir);
    }
    return buff;
}

/* check that our directory exists and try to create it if it doesn't
   return -1 on failure, 0 otherwise.
 */
static int check_directory(const char* path) /*@globals errno,stderr@*/
{
    int rc;
    struct stat sb;

    if (*path == '\0') {
	(void)fprintf(stderr, "%s: cannot find bogofilter directory.\n"
		"You must set the BOGOFILTER_DIR or HOME environment variables\n"
		"or use the -d DIR option. The program aborts.\n", progname);
	return 0;
    }

    rc = stat(path, &sb);
    if (rc < 0) {
	if (ENOENT==errno) {
	    if(mkdir(path, S_IRUSR|S_IWUSR|S_IXUSR)) {
		perror("Error creating directory");
		return -1;
	    } else if (verbose > 0) {
		(void)fprintf(stderr, "Created directory %s .\n", path);
	    }
	    return 0;
	} else {
	    perror("Error accessing directory");
	    return -1;
	}
    } else {
	if (! S_ISDIR(sb.st_mode)) {
	    (void)fprintf(stderr, "Error: %s is not a directory.\n", path);
	}
    }
    return 0;
}

static void cleanup_exit(int exitcode, int killfiles) {
    if (killfiles && outfname[0] != '\0') unlink(outfname);
    exit(exitcode);
}

int main(int argc, char **argv) /*@globals errno,stderr,stdout@*/
{
    int   exitcode;
    FILE  *out;

    if (((directory = create_path_from_env("HOME", BOGODIR)) == NULL) ||
	((directory = create_path_from_env("BOGOFILTER_DIR", NULL)) == NULL)) {
	fprintf(stderr, "HOME or BOGOFILTER_DIR too long\n");
	exit(2);
    }

    exitcode = process_args(argc, argv);
    if (exitcode != 0)
	exit(exitcode);

    if (check_directory(directory))
	exit(2);

    if (setup_lists(directory, 1.0, 1.0))
	exit(2);

    process_config_files();

    if (*outfname && passthrough) {
	if ((out = fopen(outfname,"wt"))==NULL)
	{
	    fprintf(stderr,"Cannot open %s: %s\n",
		    outfname, strerror(errno));
	    exit(2);
	}
    } else {
	out = stdout;
    }

    switch(run_type) {
	case RUN_NORMAL:
	case RUN_UPDATE:
	    {
		double spamicity;
		rc_t   status = bogofilter(&spamicity);

		if (passthrough)
		{
		    /* print headers */
		    for (textend=&textblocks; textend->block; textend=textend->next)
		    {
			if ((textend->len == 1
				    && memcmp(textend->block, NL, 1) == 0)
				|| (textend->len == 2
				    && memcmp(textend->block, CRLF, 2) == 0))
			    break;

			(void) fwrite(textend->block, 1, textend->len, out);
			if (ferror(out)) cleanup_exit(2, 1);
		    }
		}

		if (passthrough || verbose)
		{
		    typedef char *formatter(char *buff, size_t size);
		    formatter *fcn = terse ? format_terse : format_header;
		    char buff[256];
		    /* print spam-status at the end of the header
		     * then mark the beginning of the message body */
		    fcn(buff, sizeof(buff));
		    fputs (buff, out);
		    fputs ("\n", out);
		}

		if (verbose || passthrough || Rtable)
		{
		    if (! stats_in_header)
			(void)fputs("\n", stdout);
		    verbose += passthrough;
		    print_stats( stdout );
		    verbose -= passthrough;
		}

		if (passthrough)
		{
		    /* If the message terminated early (without body or blank
		     * line between header and body), enforce a blank line to
		     * prevent anything past us from choking. */
		    if (!textend->block)
			(void)fputs("\n", out);

		    /* print body */
		    for (; textend->block != NULL; textend=textend->next)
		    {
			(void) fwrite(textend->block, 1, textend->len, out);
			if (ferror(out)) cleanup_exit(2, 1);
		    }

		    if (fflush(out) || ferror(out) || (out != stdout && fclose(out))) {
			cleanup_exit(2, 1);
		    }
		}

		exitcode = (status == RC_SPAM) ? 0 : 1;
		if (nonspam_exits_zero && passthrough && exitcode == 1)
		    exitcode = 0;

		format_log_header(msg_bogofilter, sizeof(msg_bogofilter));
	    }
	    break;
	default:
	    register_messages(run_type);
	    break;
    }

    close_lists();

#ifdef HAVE_SYSLOG_H
    if (logflag)
    {
	openlog("bogofilter", LOG_PID, LOG_MAIL);

	switch (run_type)
	{
	case RUN_NORMAL:
	    syslog(LOG_INFO, "%s\n", msg_bogofilter);
	    break;
	case RUN_UPDATE:
	    syslog(LOG_INFO, "%s, %s\n", msg_bogofilter, msg_register);
	    break;
	default:
	    syslog(LOG_INFO, "%s", msg_register);
	    break;
	}

	closelog();
    }
#endif

    exit(exitcode);
}

/* End */
