/* $Id$ */

/*****************************************************************************

NAME:
   bogolexer.c -- runs bogofilter's lexer.

AUTHOR:
   David Relson <relson@osagesoftware.com>

******************************************************************************/

/* imports */
#include "common.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern char *optarg;
extern int optind, opterr, optopt;

#include "bogoconfig.h"
#include "bogoreader.h"
#include "bool.h"
#include "charset.h"
#include "configfile.h"
#include "lexer.h"
#include "textblock.h"
#include "token.h"
#include "mime.h"

const char *progname = "bogolexer";

/* prevent larger inclusions */

const char *spam_header_name = SPAM_HEADER_NAME;

const parm_desc sys_parms[] =
{
    { "stats_in_header",		CP_BOOLEAN,	{ (void *) &stats_in_header } },
    { "user_config_file",		CP_STRING,	{ &user_config_file } },
    { "block_on_subnets",		CP_BOOLEAN,	{ (void *) &block_on_subnets } },
    { "charset_default",		CP_STRING,	{ &charset_default } },
    { "replace_nonascii_characters",	CP_BOOLEAN,	{ (void *) &replace_nonascii_characters } },
    { NULL,				CP_NONE,	{ (void *) NULL } },
};

const parm_desc format_parms[] =
{
    { NULL, CP_NONE, { (void *) NULL } },
};

/* Function Prototypes */

void initialize(void);

/* Function Definitions */

static void usage(void)
{
    fprintf(stderr, "Usage: %s [ -p | -q | -n | -h ]\n", progname);
}

static void help(void)
{
    usage();
    fprintf(stderr,
	    "\n"
	    "\t-p\t- print the tokens from stdin.\n"
	    "\t-q\t- quiet mode, no tokens are printed.\n"
	    "\t-h\t- help, this output.\n"
	    "\t-n\t- map non-ascii characters to '?'.\n"
	    "\t-v\t- set verbosity level.\n"
	    "\t-c file\t- read specified config file.\n"
	    "\t-C\t- don't read standard config files.\n"
	    "\t-I file\t- read message from file instead of stdin.\n"
	    "\t-x list\t- set debug flags.\n"
	    "\t-D\t- direct debug output to stdout.\n");
    fprintf(stderr,
	    "\n"
	    "%s (version %s) is part of the bogofilter package.\n", 
	    progname, version);
}

static void print_version(void)
{
    (void)fprintf(stderr,
		  "%s version %s\n"
		  "Copyright (C) 2002-2004 David Relson\n\n"
		  "%s comes with ABSOLUTELY NO WARRANTY.  "
		  "This is free software, and\nyou are welcome to "
		  "redistribute it under the General Public License.  "
		  "See\nthe COPYING file with the source distribution for "
		  "details.\n"
		  "\n",
		  progname, version, PACKAGE);
}

#define	OPTIONS	":c:CDhHI:npP:qvVx:X:m"

/** These functions process command line arguments.
 **
 ** They are called to perform passes 1 & 2 of command line switch processing.
 ** The config file is read in between the two function calls.
 **
 ** The functions will exit if there's an error, for example if
 ** there are leftover command line arguments.
 */

static void process_args(int argc, char **argv)
{
    int option;

    fpin = stdin;
    dbgout = stderr;

    while ((option = getopt(argc, argv, OPTIONS)) != -1)
    {
	switch (option)
	{
	case ':':
	    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
	    exit(EX_ERROR);

	case '?':
	    fprintf(stderr, "Unknown option -%c.\n", optopt);
	    exit(EX_ERROR);

	case 'c':
	    read_config_file(optarg, false, false, PR_COMMAND);
	    /*@fallthrough@*/
	    /* fall through to suppress reading config files */

	case 'C':
	    suppress_config_file = true;
	    break;

	case 'D':
	    dbgout = stdout;
	    break;

	case 'h':
	    help();
	    exit(EX_OK);

	case 'H':
	    header_line_markup = false;
	    break;

	case 'I':
	    bogoreader_name(optarg);
	    break;

	case 'n':
	    replace_nonascii_characters = true;
	    break;

	case 'p':
	    passthrough = true;
	    break;

	case 'q':
	    quiet = true;
	    break;

	case 'v':
	    verbose += 1;
	    break;

        case 'V':
	    print_version();
	    exit(EX_OK);

	case 'x':
	    set_debug_mask( optarg );
	    break;

	case 'X':
	    /* 'L' - enable lexer_v3 debug output  */
	    set_bogotest( optarg );
	    break;
	}
    }

    if (optind < argc) {
	fprintf(stderr, "Extra arguments given, first: %s. Aborting.\n",
		argv[optind]);
	exit(EX_ERROR);
    }
}

int count=0;

int main(int argc, char **argv)
{
    token_t t;

    mbox_mode = true;		/* to allow multiple messages */

    process_args(argc, argv);
    process_config_files(false);

    textblock_init();

    if (!passthrough)
    {
	if (quiet)
	    puts( "quiet mode.");
	else
	    puts("normal mode.");
    }

    bogoreader_init(argc, argv);

    while ((*reader_more)()) {
	initialize();

	while ((t = get_token()) != NONE)
	{
	    count += 1;
	    if (passthrough) {
		fprintf(stdout, "%s\n", yylval->text);
	    }
	    else if (!quiet)
		printf("get_token: %d \"%s\"\n", t, yylval->text);
	}
    }

    if ( !passthrough )
	printf( "%d tokens read.\n", count );

    textblock_free();

    MEMDISPLAY;

    return 0;
}

void initialize()
{
    init_charset_table(charset_default, true);
    mime_reset();
    token_init();
    lexer_v3_init(NULL);
}
