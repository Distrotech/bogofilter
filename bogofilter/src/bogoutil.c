/* $Id$ */
/*****************************************************************************

NAME:
   bogoutil.c -- dumps and loads bogofilter text files from/to Berkeley DB format.

AUTHOR:
   Gyepi Sam <gyepi@praxis-sw.com>
   
******************************************************************************/

#include "common.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

#include "bogofilter.h"
#include "buff.h"
#include "datastore.h"
#include "error.h"
#include "graham.h"			/* for UNKNOWN_WORD */
#include "maint.h"
#include "paths.h"
#include "robinson.h"			/* for ROBS and ROBX */
#include "swap.h"
#include "xmalloc.h"
#include "xstrdup.h"

#define PROGNAME "bogoutil"

#define MINIMUM_FREQ	5		/* minimum freq */

run_t run_type = RUN_NORMAL;

const char *progname = PROGNAME;

static int dump_count = 0;

bool onlyprint = false;

/* Function Prototypes */

static int process_args(int argc, char **argv);

int ds_dump_hook(word_t *key, dsv_t *data,
		 /*@unused@*/ void *userdata);

/* Function Definitions */

int ds_dump_hook(word_t *key, dsv_t *data,
		 /*@unused@*/ void *userdata)
{
    (void)userdata;

    dump_count += 1;

    if ((!keep_count(data->goodcount) && !keep_count(data->spamcount)) ||
	!keep_date(data->date) ||
	!keep_size(key->leng))
	return 0;

    if (replace_nonascii_characters)
	do_replace_nonascii_characters(key->text, key->leng);

    printf( "%*s %lu %lu", 
	    key->leng, key->text,
	    (unsigned long)data->spamcount,
	    (unsigned long)data->goodcount);
    if (data->date)
	printf(" %lu", (unsigned long)data->date);
    putchar('\n');

    return !!ferror(stdout);
}

struct robhook_data {
    double   *sum;
    uint32_t *count;
    void     *dbh;
    double   scalefactor;
};

static int robx_hook(word_t *key, dsv_t *data, 
		     void *userdata)
{
    struct robhook_data *rd = userdata;

    uint32_t goodness;
    uint32_t spamness;
    double   prob;
    static word_t *x;
    static size_t x_size = MAXTOKENLEN + 1;

    /* ignore system meta-data */
    if (*key->text == '.')
	return 0;

    if (x == NULL || key->leng + 1 > x_size) {
//	if (x) word_free(x);
	x_size = max(x_size, key->leng + 1);
	x = word_new(NULL, x_size);
    }

    word_cpy(x, key);

    spamness = data->spamcount;
    goodness = data->goodcount;

    /* tokens in good list were already counted */
    /* now add in tokens only in spam list */
/*
    if (goodness == 0) {
*/
    prob = spamness / (goodness * rd->scalefactor + spamness);
    if (goodness + spamness >= 10) {
	(*rd->sum) += prob;
	(*rd->count) += 1;
    }
    /* print if -vv and token in both word lists, or -vvv */
    if ((verbose > 1 && goodness && spamness) || verbose > 2) {
	printf("cnt: %4lu,  sum: %11.6f,  ratio: %9.6f,"
	       "  sp: %3lu,  gd: %3lu,  p: %9.6f,  t: ", 
	       (unsigned long)*rd->count, *rd->sum, *rd->sum / *rd->count,
	       (unsigned long)spamness, (unsigned long)goodness, prob);
	word_puts(x, 0, stdout);
	fputc('\n', stdout);
    }
/*
    }
*/
    return 0;
}

static int ds_oper(const char *path, dbmode_t open_mode, 
		   ds_foreach_t *hook, void *userdata)
{
    int   ret = 0;
    void *dbh;

    dbh = ds_open(".", 1, &path, open_mode);

    if (dbh == NULL) {
	fprintf(stderr, "Can't open file %s\n", path);
	exit(EX_ERROR);
    }

    ret = ds_foreach(dbh, hook, userdata);
    if (ret)
	ds_close(dbh, false);

    return ret;
}

static int dump_file(char *ds_file)
{
    int rc;

    dump_count = 0;

    rc = ds_oper(ds_file, DB_READ, ds_dump_hook, NULL);

    if (verbose)
	fprintf(dbgout, "%d tokens dumped\n", dump_count);

    return rc;
}

#define BUFSIZE 512

static byte *spanword(byte *t)
{
    while (isspace(*t)) t += 1;		/* skip leading whitespace */
    while (*t && !isspace(*t)) t += 1;	/* span current word	   */
    if (*t)
	*t++ = '\0';
    return t;
}

static int load_file(const char *ds_file)
{
    void *dbh;
    byte buf[BUFSIZE];
    byte *p;
    int rv = 0;
    size_t len;
    int load_count = 0;
    unsigned long line = 0;
    unsigned long count[2], date;
    YYYYMMDD today_save = today;

    dbh = ds_open(".", 1, &ds_file, DB_WRITE);
    if (dbh == NULL)
	return 2;

    memset(buf, '\0', BUFSIZE);

    for (;;) {
	dsv_t data;
	word_t *token;
	if (fgets((char *)buf, BUFSIZE, stdin) == NULL) {
	    if (ferror(stdin)) {
		perror(PROGNAME);
		rv = 2;
	    }
	    break;
	}

	line++;

	len = strlen((char *)buf);

	/* too short. */
	if (len < 4)
	    continue;

	p = spanword(buf);
	len = strlen((const char *)buf);

	spamcount = atoi((const char *)p);
	if ((int) spamcount < 0)
	    spamcount = 0;
	p = spanword(p);

	goodcount = atoi((const char *)p);
	if ((int) goodcount < 0)
	    goodcount = 0;
	p = spanword(p);

	date = atoi((const char *)p);
	p = spanword(p);

	if (*p != '\0') {
	    fprintf(stderr,
		    "%s: Unexpected input [%s] on line %lu. "
		    "Expecting whitespace before count.\n",
		    PROGNAME, buf, line);
	    rv = 1;
	    break;
	}

	if (date == 0)				/* date as YYYYMMDD */
	    date = today_save;

	if (replace_nonascii_characters)
	    do_replace_nonascii_characters(buf, len);
	if ((!keep_count(goodcount) && !keep_count(spamcount)) ||
	    !keep_date(date) || 
	    !keep_size(strlen((const char *)buf)))
	    continue;

	load_count += 1;

	/* Slower, but allows multiple lists to be concatenated */
	set_date(date);
	token = word_new(buf, len);
	ds_read(dbh, token, &data);
	data.spamcount += spamcount;
	data.goodcount += goodcount;
	ds_write(dbh, token, &data);
//	word_free(token);
    }
    ds_close(dbh, false);

    if (verbose)
	fprintf(dbgout, "%d tokens loaded\n", load_count);

    return rv;
}

static int get_token(buff_t *buff, FILE *fp)
{
    int rv = 0;

    if (fgets((char *)buff->t.text, buff->size, fp) == NULL) {
	if (ferror(fp)) {
	    perror(PROGNAME);
	    rv = 2;
	} else {
	    rv = 1;
	}
    } else {
	buff->t.leng = strlen((const char *)buff->t.text);
	if (buff->t.text[buff->t.leng - 1] == '\n' ) {
	    buff->t.leng -= 1;
	    buff->t.text[buff->t.leng] = '\0';
	}
	else
	{
	    fprintf(stderr,
		    "%s: Unexpected input [%s]. Does not end with newline "
		    "or line too long.\n",
		    PROGNAME, buff->t.text);
	    rv = 1;
	}
    }
    return rv;
}

static int words_from_list(const char *ds_file, int argc, char **argv)
{
    void *dbh;
    int rv = 0;

    dbh = ds_open(".", 1, &ds_file, DB_READ);
    if (dbh == NULL)
	return 2;

    if (argc == 0)
    {
	byte buf[BUFSIZE];
	buff_t *buff = buff_new(buf, 0, BUFSIZE);
	while (get_token(buff, stdin) == 0) {
	    dsv_t val;
	    word_t *token = &buff->t;
	    ds_read(dbh, token, &val);
	    word_puts(token, 0, stdout);
	    printf(" %lu %lu\n", (unsigned long) val.spamcount, (unsigned long) val.goodcount);
	}
//	buff_free(buff);
    }
    else
    {
	while (argc-- > 0) {
	    dsv_t val;
	    const byte *word = (const byte *) *argv++;
	    word_t *token = word_new(word, strlen((const char *)word));
	    ds_read(dbh, token, &val);
	    word_puts(token, 0, stdout);
	    printf(" %lu %lu\n", (unsigned long) val.spamcount, (unsigned long) val.goodcount);
//	    word_free(token);
	}
    }

    ds_close(dbh, false);

    return rv;
}

static int words_from_path(const char *dir, int argc, char **argv, bool show_probability)
{
    int  count = 0;

    char filepath1[PATH_LEN];
    char filepath2[PATH_LEN];
    char *filepaths[2];

    byte buf[BUFSIZE];
    buff_t *buff = buff_new(buf, 0, BUFSIZE);
    const byte *word = buf;

    unsigned long spam_count, spam_msg_count = 0 ;
    unsigned long good_count, good_msg_count = 0 ;

    const char *head_format = !show_probability ? "%-20s %6s %6s\n"   : "%-20s %6s  %6s  %6s  %6s\n";
    const char *data_format = !show_probability ? "%-20s %6lu %6lu\n" : "%-20s %6lu  %6lu  %f  %f\n";

    void *dbh;

    set_wordlist_mode(dir);
    filepaths[0] = filepath1;
    filepaths[1] = filepath2;

    count = build_wordlist_paths(filepaths, dir);

    /* XXX FIXME: deadlock possible */
    dbh = ds_open(".", count, (const char **)filepaths, DB_READ);
    if (dbh == NULL)
	return 2;

    if (show_probability)
    {
	dsv_t val;
	ds_get_msgcounts(dbh, &val);
	spam_msg_count = val.spamcount;
	good_msg_count = val.goodcount;
    }

    printf(head_format, "", "spam", "good", "Gra prob", "Rob prob");

    while (argc >= 0)
    {
	dsv_t val;
	word_t *token;
	double gra_prob = 0.0, rob_prob = 0.0;
	
	if (argc == 0)
	{
	    if (get_token(buff, stdin) != 0)
		break;
	    token = &buff->t;
	} else {
	    word = (const byte *) *argv++;
	    if (--argc == 0)
		argc = -1;
	    token = word_new(word, strlen((const char *)word));
	}

	ds_read(dbh, token, &val);
	spam_count = val.spamcount;
	good_count = val.goodcount;

	if (show_probability)
	{
	    double spamness = (double) spam_count / (double) spam_msg_count;
	    double goodness = (double) good_count / (double) good_msg_count;

	    gra_prob = (spam_count + good_count <= MINIMUM_FREQ)
		? UNKNOWN_WORD
		: spamness / (spamness+goodness);
	    rob_prob = ((ROBS * ROBX + spamness) / (ROBS + spamness+goodness));
	}
	printf(data_format, token->text, spam_count, good_count, gra_prob, rob_prob);
//	if (argc != 0)
//	    word_free(token);
    }

    ds_close(dbh, false);

    return 0;
}

static int display_words(const char *path, int argc, char **argv, bool prob)
{
    struct stat sb;

    /* protect against broken stat(2) that succeeds for empty names */
    if (path == NULL || *path == '\0') {
        fprintf(stderr, "Expecting non-empty directory or file name.\n");
        return -1;
    }

    if ( stat(path, &sb) != 0 ) {
	fprintf(stderr, "Error accessing file or directory [%s].  %s\n",
		path, strerror(errno));
	return -1;
    }

    if (S_ISDIR(sb.st_mode))
	words_from_path(path, argc, argv, prob);
    else
	words_from_list(path, argc, argv);
    
    return 0;
}

static double compute_robx(void *dbh)
{
    uint32_t tok_cnt = 0;
    double sum = 0.0;
    double robx;

    dsv_t val;
    uint32_t msg_good, msg_spam;
    struct robhook_data rh;

    ds_get_msgcounts( dbh, &val );
    msg_spam = val.spamcount;
    msg_good = val.goodcount;

    rh.scalefactor = (double)msg_spam/msg_good;
    rh.dbh = dbh;
    rh.sum = &sum;
    rh.count = &tok_cnt;

    ds_foreach(dbh, robx_hook,  &rh);

    robx = sum/tok_cnt;
    if (verbose)
	printf(".MSG_COUNT: %lu, %lu, scale: %f, sum: %f, cnt: %6d, .ROBX: %f\n",
	       (unsigned long)msg_spam, (unsigned long)msg_good, rh.scalefactor,
	       sum, (int)tok_cnt, robx);
    else if (onlyprint) printf("%f\n", robx);
    return robx;
}

static int compute_robinson_x(char *path)
{
    double robx;
    word_t *word_robx = word_new((const byte *)ROBX_W, strlen(ROBX_W));

    dsv_t val;
    void *dbh;

    setup_wordlists(path, 0);
    open_wordlists(DB_READ);

    robx = compute_robx(word_lists->dbh);

    close_wordlists(false);
    free_wordlists();

    if (!onlyprint) {
	size_t count;

	char filepath1[PATH_LEN];
	char filepath2[PATH_LEN];
	char *filepaths[2];

	filepaths[0] = filepath1;
	filepaths[1] = filepath2;

	count = build_wordlist_paths(filepaths, path);

	if (count == 0)
	{
	    fprintf(stderr, "%s: string too long creating .db file name.\n", PROGNAME);
	    exit(EX_ERROR);
	}

	dbh = ds_open(".", count, (const char **) filepaths, DB_WRITE);
	if (dbh == NULL)
	    return 2;

	val.goodcount = 0;
	val.spamcount = (uint32_t) (robx * 1000000);
	ds_write(dbh, word_robx, &val);
	ds_close(dbh, false);
    }

//    word_free(word_robx);

    return 0;
}

static void print_version(void)
{
    (void)fprintf(stderr,
		  "%s version %s\n"

		  "    Database:"
#ifdef	ENABLE_TDB_DATASTORE
		  " TrivialDB"
#else
		  " BerkeleyDB"
#endif
		  "\n"

		  "Copyright (C) 2002 Gyepi Sam\n\n"
		  "%s comes with ABSOLUTELY NO WARRANTY.\n"
		  "This is free software, and you are welcome to redistribute\n"
		  "it under the General Public License.\n"
		  "See the COPYING file with the source distribution for details.\n\n",
		  progtype, version, PROGNAME);
}

static void usage(void)
{
    fprintf(stderr, "Usage: %s { -d | -l | -w | -p } file.db | { -r | -R } directory | [ -v | -h | -V ]\n", PROGNAME);
}

static void help(void)
{
    usage();
    fprintf(stderr,
	    "\n"
	    "\t-d file\tDump data from file to stdout.\n"
	    "\t-l file\tLoad data from stdin into file.\n"
	    "\t-w directory\tDisplay counts for words from stdin.\n"
	    "\t-p directory\tDisplay word counts and probabilities.\n"
	    "\t-m\tEnable maintenance works (expiring tokens).\n"
	    "\t-h\tPrint this message.\n"
	    "\t-v\tOutput debug messages.\n"
	    "\t-r\tCompute Robinson's X for specified directory.\n"
	    "\t-R\tCompute Robinson's X and save it in the spam list.\n");
    fprintf(stderr,
	    "\t-k size\tset BerkeleyDB cache size (MB).\n"
	    "\t-W\tUse combined wordlist.db for spam and ham tokens.\n"
	    "\t-WW\tUse separate wordlists for spam and ham tokens.\n");
    fprintf(stderr,
	    "\t-a age\tExclude tokens with older ages.\n"
	    "\t-c count\tExclude tokens with lower counts.\n"
	    "\t-s min,max\tExclude tokens with lengths between min and max.\n"
	    "\t-n\tReplace non-ascii characters with '?'.\n"
	    "\t-y today\tSet default day-of-year (1..366).\n"
	    "\t-x list\tSet debug flags.\n"
	    "\t-D\tDirect debug output to stdout.\n"
	    "\t-V\tPrint program version.\n"
	    "\t-I file\tRead this file instead of standard input.\n"
	    "\n"
	    "%s (version %s) is part of the bogofilter package.\n",
	    PROGNAME, version);
}

#undef	ROBX

typedef enum { NONE, DUMP, LOAD, WORD, MAINTAIN, ROBX } cmd_t;
char *ds_file = NULL;
bool  prob = false;
cmd_t flag = NONE;

#define	OPTIONS	":a:c:d:DhI:k:l:m:np:r:R:s:vVw:Wx:y:"

static int process_args(int argc, char **argv)
{
    int option;
    int count = 0;

    fpin = stdin;
    dbgout = stderr;

    while ((option = getopt(argc, argv, OPTIONS)) != -1)
	switch (option) {
	case 'd':
	    flag = DUMP;
	    count += 1;
	    ds_file = (char *) optarg;
	    break;

	case 'k':
	    db_cachesize=atoi(optarg);
	    break;

	case 'l':
	    flag = LOAD;
	    count += 1;
	    ds_file = (char *) optarg;
	    break;

	case 'm':
	    flag = MAINTAIN;
	    count += 1;
	    ds_file = (char *) optarg;
	    break;

	case 'p':
	    prob = true;
	    /*@fallthrough@*/

	case 'w':
	    flag = WORD;
	    count += 1;
	    ds_file = (char *) optarg;
	    break;

	case 'r':
	    onlyprint = true;
	case 'R':
	    flag = ROBX;
	    count += 1;
	    ds_file = (char *) optarg;
	    break;

	case 'v':
	    verbose++;
	    break;

	case ':':
	    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
	    exit(EX_ERROR);

	case '?':
	    fprintf(stderr, "Unknown option -%c.\n", optopt);
	    exit(EX_ERROR);

	case 'h':
	    help();
	    exit(EX_OK);

	case 'V':
	    print_version();
	    exit(EX_OK);

	case 'W':
	    incr_wordlist_mode();
	    break;

	case 'x':
	    set_debug_mask( (char *) optarg );
	    break;

	case 'a':
	    thresh_date = string_to_date((char *)optarg);
	    break;

	case 'c':
	    thresh_count = atol((char *)optarg);
	    break;

	case 's':
	    {
		unsigned long mi, ma;

		if (2 == sscanf((const char *)optarg, "%lu,%lu", &mi, &ma)) {
		    size_min = mi;
		    size_max = ma;
		} else {
		    fprintf(stderr, "syntax error in argument \"%s\" of -s\n.",
			    optarg);
		    exit(EX_ERROR);
		}
	    }
	    break;

	case 'n':
	    replace_nonascii_characters ^= true;
	    break;

	case 'y':		/* date as YYYYMMDD */
	    today = string_to_date((char *)optarg);
	    break;

	case 'I':
	    fpin = fopen( optarg, "r" );
	    if (fpin == NULL) {
		fprintf(stderr, "Can't read file '%s'\n", optarg);
		exit(EX_ERROR);
	    }
	    break;

	case 'D':
	    dbgout = stdout;
	    break;

	default:
	    abort();
	    exit(EX_ERROR);
	}

    if (count != 1)
    {
      fprintf(stderr, "%s: Exactly one of the -d, -l, -R or -w flags "
	      "must be present.\n", PROGNAME);
      exit(EX_ERROR);
    }

    if (optind < argc && flag != WORD) {
	fprintf(stderr, "Extra arguments given, first: %s. Aborting.\n", argv[optind]);
	exit(EX_ERROR);
    }

    return count;
}

int main(int argc, char *argv[])
{
    progtype = build_progtype(progname, DB_TYPE);

    set_today();			/* compute current date for token age */
    
    /* Get directory name from environment */
    directory = get_directory(PR_ENV_BOGO);
    if (directory == NULL)
	directory = get_directory(PR_ENV_HOME);

    process_args(argc, argv);

    /* Extra or missing parameters */
    if (flag != WORD && argc != optind) {
	usage();
	exit(EX_ERROR);
    }

    switch(flag) {
	case DUMP:
	    return dump_file(ds_file);
	case LOAD:
	    return load_file(ds_file);
	case MAINTAIN:
	    return maintain_wordlist_file(ds_file);
	case WORD:
	    argc -= optind;
	    argv += optind;
	    return display_words(ds_file, argc, argv, prob);
	case ROBX:
	    return compute_robinson_x(ds_file);
	case NONE:
	default:
	    /* should have been handled above */
	    abort();
    }
}
