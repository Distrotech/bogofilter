/* $Id$ */

#include "common.h"

#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#include "bogofilter.h"
#include "datastore.h"
#include "error.h"
#include "find_home.h"
#include "paths.h"
#include "wordlists.h"
#include "xmalloc.h"
#include "xstrdup.h"

/* Default wordlist mode is now wordlist.db -
   a single wordlist containing ham and spam tokens */
wl_t	wl_default = WL_M_COMBINED;
wl_t	wl_mode    = WL_M_UNKNOWN;	/* '-W' flag */

#define	MIN_SLEEP	0.5e+3		/* .5 milliseconds */
#define	MAX_SLEEP	2.0e+6		/* 2 seconds */

wordlist_t *word_list;
wordlist_t *good_list;
wordlist_t *spam_list;
/*@null@*/ wordlist_t* word_lists=NULL;

/* Function Prototypes */

static void rand_sleep(double min, double max);

/* Function Definitions */

static void rand_sleep(double min, double max)
{
    static bool need_init = true;
    struct timeval timeval;
    long delay;

    if (need_init) {
	need_init = false;
	gettimeofday(&timeval, NULL);
	srand(timeval.tv_usec);
    }
    delay = min + ((max-min)*rand()/(RAND_MAX+1.0));
    timeval.tv_sec  = delay / 1000000;
    timeval.tv_usec = delay % 1000000;
    select(0,NULL,NULL,NULL,&timeval);
}

/* returns -1 for error, 0 for success */
static int init_wordlist(/*@out@*/ wordlist_t **list, const char* name, const char* path,
			 double sweight, bool sbad, 
			 double gweight, bool gbad, 
			 int override, bool ignore)
{
    wordlist_t *new = (wordlist_t *)xcalloc(1, sizeof(*new));
    wordlist_t *list_ptr;
    static int listcount;

    *list = new;

    new->dsh=NULL;
    new->filename=xstrdup(name);
    new->filepath=xstrdup(path);
    new->index = ++listcount;
    new->override=override;
    new->active=false;
    new->weight[SPAM]=sweight;
    new->weight[GOOD]=gweight;
    new->bad[SPAM]=sbad;
    new->bad[GOOD]=gbad;
    new->ignore=ignore;

    if (! word_lists) {
	word_lists=new;
	new->next=NULL;
	return 0;
    }
    list_ptr=word_lists;

    /* put lists with high override numbers at the front. */
    while(1) {
	if (list_ptr->override < override) {
	    word_lists=new;
	    new->next=list_ptr;
	    break;
        }
        if (! list_ptr->next) {
	    /* end of list */
	    list_ptr->next=new;
	    new->next=NULL;
	    break;
	}
	list_ptr=list_ptr->next;
    }
    return 0;
}

/* setup_wordlists()
   returns: -1 for error, 0 for success
   **
   ** precedence: (high to low)
   **
   **	command line
   **	$BOGOFILTER_DIR
   **	user config file
   **	site config file
   **	$HOME
   */

int setup_wordlists(const char* d, priority_t precedence)
{
    int rc = 0;
    char *dir;
    double good_weight = 1.0;
    double bad_weight  = 1.0;
    static priority_t saved_precedence = PR_NONE;

    if (DEBUG_WORDLIST(2))
	fprintf(dbgout, "p: %d, s: %d\n", (int) precedence, (int) saved_precedence);

    if (precedence < saved_precedence)
	return rc;

    if (d) {
	dir = xstrdup(d);
    }
    else {
	dir = get_directory(precedence);
	if (dir == NULL)
	    return rc;
    }

    if (DEBUG_WORDLIST(2))
	fprintf(dbgout, "d: %s\n", dir);

    if (saved_precedence != precedence && word_lists != NULL) {
	free_wordlists();
    }

    saved_precedence = precedence;

    if (check_directory(dir)) {
	(void)fprintf(stderr, "%s: cannot find bogofilter directory.\n"
		      "You must specify a directory on the command line, in the config file,\n"
		      "or by using the BOGOFILTER_DIR or HOME environment variables.\n"
		      "Program aborting.\n", progname);
	rc = -1;
    }

    if (init_wordlist(&word_list, "word", dir, bad_weight, true, good_weight, false, 0, false) != 0)
	rc = -1;

    xfree(dir);

    return rc;
}

const char *aCombined[] = { WORDLIST };
size_t	    cCombined = COUNTOF(aCombined);
const char *aSeparate[] = { SPAMFILE, GOODFILE };
size_t	    cSeparate = COUNTOF(aSeparate);

void incr_wordlist_mode(void)
{
    switch (wl_mode) {
    case WL_M_UNKNOWN:  wl_mode = WL_M_COMBINED; break;
    case WL_M_COMBINED: wl_mode = WL_M_SEPARATE; break;
    case WL_M_SEPARATE: 
	fprintf(stderr, "Invalid -W option.\n");
	exit(EX_ERROR);
    }
    return;
}

void open_wordlists(dbmode_t mode)
{
    wordlist_t *list;
    int retry;

    do {
	retry = 0;
	for (list = word_lists; list != NULL; list = list->next) {
	    if (wl_mode == WL_M_UNKNOWN)
		set_wordlist_mode(list->filepath);
	    switch (wl_mode) {
	    case WL_M_COMBINED:
		if (db_cachesize < 4)
		    db_cachesize = 4;
		list->dsh = ds_open(list->filepath, cCombined, aCombined, mode);
		break;
	    case WL_M_SEPARATE:
		list->dsh = ds_open(list->filepath, cSeparate, aSeparate, mode);
		break;
	    case WL_M_UNKNOWN:
		fprintf(stderr, "Invalid wordlist mode.\n");
		exit(EX_ERROR);
	    }
	    if (list->dsh == NULL) {
		int err = errno;
		close_wordlists(true); /* unlock and close */
		switch(err) {
		    /* F_SETLK can't obtain lock */
		    case EAGAIN:
		    case EACCES:
			rand_sleep(MIN_SLEEP, MAX_SLEEP);
			retry = 1;
			break;
		    default:
			if (query)	/* If "-Q", failure is OK */
			    return;
			fprintf(stderr, "Can't open %s (%s), errno %d, %s\n",
				list->filename, list->filepath, err, strerror(err));
			exit(EX_ERROR);
		} /* switch */
	    } else { /* ds_open */
		dsv_t val;
		ds_get_msgcounts(list->dsh, &val);
		list->msgcount[GOOD] = val.goodcount;
		list->msgcount[SPAM] = val.spamcount;
	    } /* ds_open */
	} /* for */
    } while(retry);
}

/** close all open word lists */
void close_wordlists(bool nosync /** Normally false, if true, do not synchronize data. This should not be used in regular operation but only to ease the disk I/O load when the lock operation failed. */)
{
    wordlist_t *list;

    for ( list = word_lists; list != NULL; list = list->next )
    {
	if (list->dsh) ds_close(list->dsh, nosync);
	list->dsh = NULL;
    }
}

void free_wordlists(void)
{
    wordlist_t *list, *next;

    for ( list = word_lists; list != NULL; list = next )
    {
	if (list->filename) xfree(list->filename);
	if (list->filepath) xfree(list->filepath);
	next = list->next;
	xfree(list);
    }
    word_lists = NULL;
}

size_t build_wordlist_paths(char **filepaths, const char *path)
{
    bool ok;
    size_t count = 0;
    switch (wl_mode) {
    case WL_M_COMBINED:
	count = 1;
	ok = build_path(filepaths[0], sizeof(FILEPATH), path, WORDLIST) == 0;
	break;
    case WL_M_SEPARATE:
	count = 2;
	ok = (build_path(filepaths[0], sizeof(FILEPATH), path, SPAMFILE) == 0 &&
	      build_path(filepaths[1], sizeof(FILEPATH), path, GOODFILE) == 0);
	break;
    case WL_M_UNKNOWN:
	fprintf(stderr, "Invalid wordlist mode.\n");
	exit(EX_ERROR);
    }
    return count;
}

static bool check_wordlist_paths(const char *path, size_t count, const char **names)
{
    size_t i;
    for (i = 0; i < count ; i += 1) {
	char filepath[PATH_LEN];
	struct stat statbuf;
	const char *name = names[i];
	int rc;
	build_path(filepath, sizeof(filepath), path, name);
	memset(&statbuf, 0, sizeof(statbuf));
	rc = stat(filepath, &statbuf);
	if (DEBUG_WORDLIST(1))
	    fprintf(dbgout, "fn: %s, rc: %d\n", filepath, rc);
	if (rc != 0)
	    return false;
    }
    return true;
}

void set_wordlist_mode(const char *filepath)
{
    if (wl_mode != WL_M_UNKNOWN)
	return;

    wl_mode = wl_default;
    if (check_wordlist_paths(filepath, cCombined, aCombined))
	wl_mode = WL_M_COMBINED;
    else
    if (check_wordlist_paths(filepath, cSeparate, aSeparate))
	wl_mode = WL_M_SEPARATE;

    return;
}

void set_good_weight(double weight)
{
    wordlist_t *list;

    for ( list = word_lists; list != NULL; list = list->next )
    {
	list->weight[GOOD] = weight;
    }

    return;
}

void set_list_active_status(bool status)
{
    wordlist_t *list;

    for ( list = word_lists; list != NULL; list = list->next )
    {
	list->active = status;
    }

    return;
}

#ifdef COMPILE_DEAD_CODE
/* some sanity checking of lists is needed because we may
   allow users to specify lists eventually and there are
   opportunities to generate divide-by-zero exceptions or
   follow bogus pointers. */
static void sanitycheck_lists(void)
{
    wordlist_t* list=word_lists;
    int listcount=0;

    while(1) {
	if (!list) break;
	if (! list->name) {
	    fprintf(stderr, "A list has no name.\n");
	    exit(EX_ERROR);
	}
	if (list->msgcount==0) {
	    fprintf(stderr, "list %s has zero message count.\n", list->name);
	    exit(EX_ERROR);
	}
	listcount++;
	list=list->next;
    }
    if (0==listcount) {
	fprintf(stderr, "No wordlists available!\n");
	exit(EX_ERROR);
    }
    if (DEBUG_WORDLIST(1))
	fprintf(dbgout, "%d lists look OK.\n", listcount);
}
#endif

static char *spanword(char *p)
{
    p += strcspn(p, ", \t");		/* skip to end of word */
    *p++ = '\0';
    while (isspace((unsigned char)*p)) 	/* skip trailing whitespace */
	p += 1;
    return p;
}

/* type - 'g', 's', or 'i'
 * name - 'good', 'spam', etc
 * path - 'goodlist.db'
 * weight - 1,2,...
 * override - 1,2,...
 */

/* returns true for success, false for error */
bool configure_wordlist(const char *val)
{
    bool ok;
    int rc;
    wordlist_t* list = xcalloc(1, sizeof(wordlist_t));
    char* type;
    char* name;
    char* path;
    double sweight, gweight;
    bool bad = false;
    int  override;
    bool ignore = false;

    char *tmp = xstrdup(val);

    type=tmp;			/* save wordlist type (good/spam) */
    tmp = spanword(tmp);

    switch (type[0])
    {
	case 'g':		/* good */
	    break;
	case 's':
	case 'b':		/* spam */
	    bad = true;
	    break;
	default:
	    fprintf( stderr, "Unknown list type - '%c'\n", type[0]);
	    break;
    }

    name=tmp;			/* name of wordlist */
    tmp = spanword(tmp);

    path=tildeexpand(tmp);	/* path to wordlist */
    tmp = spanword(tmp);

    sweight=atof(tmp);
    tmp = spanword(tmp);

    gweight=atof(tmp);
    tmp = spanword(tmp);

    override=atoi(tmp);
    tmp = spanword(tmp);

    if (isdigit((unsigned char)*tmp))
	ignore=atoi(tmp);
    else {
	ignore = false;		/* default is "don't ignore" */
	switch (tolower((unsigned char)*tmp)) {
	case 'n':		/* no */
	case 'f':		/* false */
	    ignore = false;
	    break;
	case 'y':		/* yes */
	case 't':		/* true */
	    ignore = true;
	    break;
	}
    }
    tmp = spanword(tmp);

    rc = init_wordlist(&list, name, path, sweight, bad, gweight, false, override, ignore);
    ok = rc == 0;

    xfree(path);

    return ok;
}
