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

#include "bool.h"
#include "charset.h"
#include "configfile.h"
#include "html.h"		/* for strict_check */
#include "lexer.h"
#include "textblock.h"
#include "token.h"
#include "mime.h"

#define PROGNAME "bogolexer"

const char *progname = PROGNAME;

/* prevent larger inclusions */

const char *spam_header_name = SPAM_HEADER_NAME;

const parm_desc sys_parms[] =
{
    { "stats_in_header",		CP_BOOLEAN,	{ (void *) &stats_in_header } },
    { "user_config_file",		CP_STRING,	{ &user_config_file } },
    { "block_on_subnets",		CP_BOOLEAN,	{ (void *) &block_on_subnets } },
    { "charset_default",		CP_STRING,	{ &charset_default } },
    { "replace_nonascii_characters",	CP_BOOLEAN,	{ (void *) &replace_nonascii_characters } },
    { "header_line_markup",		CP_BOOLEAN,	{ (void *) &header_line_markup } },
    { "strict_check",			CP_BOOLEAN,	{ (void *) &strict_check } },
    { NULL,				CP_NONE,	{ (void *) NULL } },
};

const parm_desc format_parms[] =
{
    { NULL, CP_NONE, { (void *) NULL } },
};

/* Function Prototypes */

static void process_args_1(int argc, char **argv);
static void process_args_2(int argc, char **argv);
static void initialize(FILE *fp);

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
	    "\t-P {opts} - set html processing flag(s).\n"
	    "\t   where {opts} is one or more of:\n"
	    "\t    c   - enables  strict comment checking.\n"
	    "\t    C   - disables strict comment checking (default).\n"
	    "\t    i   - enables  ignoring of upper/lower case.\n"
	    "\t    I   - disables ignoring of upper/lower case (default).\n"
	    "\t    h   - enables  header line tagging (default).\n"
	    "\t    H   - disables header line tagging.\n"
	    "\t    t   - enables  parsing of html tags 'a', 'font', and 'img' (default).\n"
	    "\t    T   - disables parsing of html tags 'a', 'font', and 'img'.\n"
	    "\n"
	    "%s (version %s) is part of the bogofilter package.\n", 
	    progname, version);
}

static void print_version(void)
{
    (void)fprintf(stderr,
		  "%s version %s\n"
		  "Copyright (C) 2002 David Relson\n\n"
		  "%s comes with ABSOLUTELY NO WARRANTY. "
		  "This is free software, and you\nare welcome to "
		  "redistribute it under the General Public License. "
		  "See the\nCOPYING file with the source distribution for "
		  "details.\n"
		  "\n", 
		  progname, version, PACKAGE);
}

#define	OPTIONS	":c:CDhI:npP:qTvVx:"

/** These functions process command line arguments.
 **
 ** They are called to perform passes 1 & 2 of command line switch processing.
 ** The config file is read in between the two function calls.
 **
 ** The functions will exit if there's an error, for example if
 ** there are leftover command line arguments.
 */

static void process_args_1(int argc, char **argv)
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

	case 'I':
	    fpin = fopen( optarg, "r" );
	    if (fpin == NULL) {
		fprintf(stderr, "Can't read file '%s'\n", optarg);
		exit(EX_ERROR);
	    }
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

	case 'T':
	    test += 1;
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
	}
    }

    if (optind < argc) {
	fprintf(stderr, "Extra arguments given, first: %s. Aborting.\n",
		argv[optind]);
	exit(EX_ERROR);
    }
}

static void process_args_2(int argc, char **argv)
{
    int option;

    optind = opterr = 1;
    /* don't use #ifdef here: */
#if HAVE_DECL_OPTRESET
    optreset = 1;
#endif

    while ((option = getopt(argc, argv, OPTIONS)) != -1)
    {
	switch (option)
	{
	case 'P':
	{
	    char *s;
	    for (s = optarg; *s ; s += 1)
	    {
		switch (*s)
		{
		case 'c': case 'C': strict_check       = *s == 'c';	break;	/* -Pc and -PC */
		case 'i': case 'I': ignore_case        = *s == 'i';	break;	/* -Pi and -PI */
		case 'h': case 'H': header_line_markup = *s == 'h'; 	break;	/* -Ph and -PH */
		case 't': case 'T': tokenize_html_tags = *s == 't'; 	break;	/* -Pt and -PT */
		default:
		    fprintf(stderr, "Unknown parsing option -P%c.\n", *s);
		    exit(EX_ERROR);
		}
	    }
	    break;
	}
	break;
	}
    }
    return;
}

int main(int argc, char **argv)
{
    token_t t;
    int count=0;
    bool from_seen = false;

    process_args_1(argc, argv);
    process_config_files(false);
    process_args_2(argc, argv);

    textblocks = textblock_init();

    if (!passthrough)
    {
	if (quiet)
	    puts( "quiet mode.");
	else
	    puts("normal mode.");
    }

    initialize(fpin);
    got_from();	/* initialize */

    while ((t = get_token()) > 0)
    {
	count += 1;
	if (t == FROM && from_seen == false) {
	    from_seen = true;
	    continue;
	}
	if (passthrough) {
	    fprintf(stdout, "%s\n", yylval->text);
	}
	else if (!quiet)
	    printf("get_token: %d '%s`\n", t, yylval->text);
    }

    if ( !passthrough )
	printf( "%d tokens read.\n", count );

    textblock_free(textblocks);

    return 0;
}

static void initialize(FILE *fp)
{
    init_charset_table(charset_default, true);
    mime_reset();
    token_init();
    lexer_v3_init(fp);
}
