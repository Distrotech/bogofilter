/* $Id$ */

#include "common.h"

#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

#include "bogofilter.h"
#include "bogohome.h"
#include "datastore.h"
#include "msgcounts.h"
#include "paths.h"
#include "rand_sleep.h"
#include "wordlists.h"
#include "xmalloc.h"
#include "xstrdup.h"

#define	MIN_SLEEP	0.5e+3		/* 0.5 milliseconds */
#define	MAX_SLEEP	2.0e+6		/* 2.0 seconds */

#ifndef	RAND_MAX			/* SunOS 4.1.X needs this */
#define	RAND_MAX	2147483647	/* May not work on SysV   */
#endif

/* Function Definitions */

static bool open_wordlist(wordlist_t *list, dbmode_t mode)
{
    bool retry = false;

    if (list->type == WL_IGNORE) 	/* open ignore list in read-only mode */
	mode = DS_READ;

    list->dsh = ds_open(bogohome, list->filepath, mode);
    if (list->dsh == NULL) {
	int err = errno;
	close_wordlists(true); /* unlock and close */
	switch(err) {
	    /* F_SETLK can't obtain lock */
	case EAGAIN:
#ifdef __EMX__
	case EACCES:
#endif
	    /* case EACCES: */
	    rand_sleep(MIN_SLEEP, MAX_SLEEP);
	    retry = true;
	    break;
	default:
	    if (query)	/* If "-Q", failure is OK */
		return false;
	    fprintf(stderr,
		    "Can't open file '%s' in directory '%s'.\n",
		    list->filepath, bogohome);
	    if (err != 0)
		fprintf(stderr,
			"error #%d - %s.\n", err, strerror(err));
	    if (err == ENOENT)
		fprintf(stderr, 
			"\n"
			"Remember to register some spam and ham messages before you\n"
			"use bogofilter to evaluate mail for its probable spam status!\n");
	    if (err == EINVAL)
		fprintf(stderr,
			"\n"
			"Make sure that the BerkeleyDB version this program is linked against\n"
			"can handle the format of the data base file (after updates in particular)\n"
			"and that your NFS locking, if applicable, works.\n");
	    exit(EX_ERROR);
	} /* switch */
    } else { /* ds_open */
	dsv_t val;
	ds_get_msgcounts(list->dsh, &val);
	list->msgcount[IX_GOOD] = val.goodcount;
	list->msgcount[IX_SPAM] = val.spamcount;
	if (wordlist_version == 0 &&
	    ds_get_wordlist_version(list->dsh, &val))
	    wordlist_version = val.count[0];
    } /* ds_open */

    return retry;
}

void open_wordlists(dbmode_t mode)
{
    bool retry = true;

    if (word_lists == NULL)
	init_wordlist("word", WORDLIST, 0, WL_REGULAR);

    ds_init();

    while (retry) {
	if (run_type & (REG_SPAM | REG_GOOD | UNREG_SPAM | UNREG_GOOD))
	    retry = open_wordlist(default_wordlist(), mode);
	else {
	    wordlist_t *list;
	    retry = false;
	    for (list = word_lists; list != NULL; list = list->next) {
		retry |= open_wordlist(list, list->type != WL_IGNORE ? mode : DS_READ);
	    }  /* for */
	}
    }
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

    ds_cleanup();
}

bool build_wordlist_path(char *filepath, size_t size, const char *path)
{
    bool ok = build_path(filepath, size, path, WORDLIST);
    return ok;
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

/** Sum up the msgcount for each of the word lists, store the
 * count of bad messages in \a mb and the count of good messages in the
 * data bases in \a mg. */
void compute_msg_counts(void)
{
    u_int32_t s = 0, g = 0;
    wordlist_t* list;

    for(list=word_lists; list != NULL; list=list->next)
    {
	s += list->msgcount[IX_SPAM];
	g += list->msgcount[IX_GOOD];
    }
    set_msg_counts(g, s);
}

static char *spanword(char *p)
{
    const char *delim = ", \t";
    p += strcspn(p, delim);		/* skip to end of word */
    *p++ = '\0';
    while (isspace((unsigned char)*p)) 	/* skip trailing whitespace */
	p += 1;
    return p;
}

/* type - 'n', or 'i' (normal or ignore)
 * name - 'user', 'system', 'ignore', etc
 * path - 'wordlist.db', 'ignorelist.db', etc
 * override - 1,2,...
 */

/* returns true for success, false for error */
bool configure_wordlist(const char *val)
{
    char  ch;
    WL_TYPE type;
    char* listname;
    char* filename;
    int  precedence;

    char *tmp = xstrdup(val);
    
    ch= tmp[0];		/* save wordlist type (good/spam) */
    tmp = spanword(tmp);
    
    switch (toupper(ch))
    {
    case 'R':
	type = WL_REGULAR;
	break;
    case 'I':
	type = WL_IGNORE;
	break;
    default:
	fprintf( stderr, "Unknown wordlist type - '%c'\n", ch);
	return (false);
    }
    
    listname=tmp;		/* name of wordlist */
    tmp = spanword(tmp);
    
    filename=tmp;		/* path to wordlist */
    tmp = spanword(tmp);
    
    precedence=atoi(tmp);
    tmp = spanword(tmp);
    
    init_wordlist(listname, filename, precedence, type);
    
    config_setup = true;

    return true;
}
