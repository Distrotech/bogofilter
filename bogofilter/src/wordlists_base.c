/* $Id$ */

#include "common.h"

#include "bogohome.h"
#include "paths.h"
#include "wordlists.h"
#include "wordlists_base.h"
#include "xmalloc.h"
#include "xstrdup.h"

bool	config_setup = false;

/* Default wordlist mode is now wordlist.db -
   a single wordlist containing ham and spam tokens */

wordlist_t *word_list;
/*@null@*/ wordlist_t* word_lists=NULL;

/* returns -1 for error, 0 for success */
int init_wordlist(/*@out@*/ wordlist_t **list, 
		  const char* name, const char* path,
		  bool sbad, bool gbad, int override, WL_TYPE type)
{
    wordlist_t *n = (wordlist_t *)xcalloc(1, sizeof(*n));
    wordlist_t *list_ptr;
    static int listcount;

    *list = n;

    n->dsh=NULL;
    n->listname=xstrdup(name);
    n->filepath=xstrdup(path);
    n->index   =++listcount;
    n->type    =type;
    n->override=override;
    n->bad[IX_SPAM]=sbad;
    n->bad[IX_GOOD]=gbad;

    list_ptr=word_lists;

    while(1) {
	if (list_ptr == NULL ||
	    list_ptr->override > override) {
	    word_lists=n;
	    n->next=list_ptr;
	    break;
        }

        if (list_ptr->next == NULL) {
	    /* end of list */
	    list_ptr->next=n;
	    n->next=NULL;
	    break;
	}
	list_ptr=list_ptr->next;
    }
    return 0;
}

/* Set default wordlist for registering messages, finding robx, etc */

void set_default_wordlist(void)
{
    wordlist_t *list;
    for ( list = word_lists; list != NULL; list = list->next )
    {
 	if (word_list == NULL && list->type != 'I')
 	    word_list = list;
    }
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

int set_wordlist_dir(const char* d, priority_t precedence)
{
    int rc = 0;
    char *dir;
    static priority_t saved_precedence = PR_NONE;

    if (DEBUG_WORDLIST(2))
	fprintf(dbgout, "p: %d, s: %d\n", (int) precedence, (int) saved_precedence);

    if (precedence < saved_precedence)
	return rc;

    dir = (d != NULL) ? xstrdup(d) : get_directory(precedence);
    if (dir == NULL)
	return -1;

    if (DEBUG_WORDLIST(2))
	fprintf(dbgout, "d: %s\n", dir);

    saved_precedence = precedence;

    if (!check_directory(dir)) {
	(void)fprintf(stderr, "%s: cannot find bogofilter directory.\n"
		      "You must specify a directory on the command line, in the config file,\n"
#ifndef __riscos__
		      "or by using the BOGOFILTER_DIR or HOME environment variables.\n"
#else
		      "or by ensuring that <Bogofilter$Dir> is set correctly.\n"
#endif
		      "Program aborting.\n", progname);
	rc = -1;
    }

    set_bogohome(dir);

    return rc;
}

static wordlist_t *free_wordlist(wordlist_t *list)
{
    wordlist_t *next = list->next;

    xfree(list->listname);
    xfree(list->filepath);
    xfree(list);

    return next;
}

void free_wordlists(void)
{
    wordlist_t *list = word_lists;

    while ( list != NULL )
    {
	list = free_wordlist(list);
    }

    word_lists = NULL;

    free_bogohome();
}
