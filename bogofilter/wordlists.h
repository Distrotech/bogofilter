/* $Id$ */
/* 
 * $Log$
 * Revision 1.4  2002/10/04 01:35:06  gyepi
 * Integration of wordlists with datastore and bogofilter code.
 * David Relson did most of the work. I just tweaked the locking code
 * and made a few minor changes.
 *
 * Revision 1.3  2002/10/04 00:39:57  m-a
 * First set of type fixes.
 *
 * Revision 1.2  2002/10/02 17:14:54  relson
 * main.c now calls setup_lists() for initializing the wordlist structures, including the opening of the wordlist.db files.
 * setup_list() takes a directory name as its argument and passes it to init_list(), which calls open_wordlist() for the actual open.
 *
 * */

/*  constants and declarations for wordlists */

#ifndef	HAVE_WORDLISTS_H
#define	HAVE_WORDLISTS_H

#include "common.h"

typedef struct wordlist_s wordlist_t;
struct wordlist_s
{
    wordlist_t *next;
    char *name;			// resource name (for debug/verbose messages)
    void *dbh;			// database handle 
    long msgcount;	// count of messages in wordlist.
    char *file;			// associated  file
    double weight;
    bool active;
    bool bad;
    int override;
    int ignore;
};

extern wordlist_t *word_lists;
extern wordlist_t good_list, spam_list;

extern void setup_lists();
void close_lists();
void *open_wordlist( wordlist_t *list, char *directory, char *filename );

#endif	/* HAVE_WORDLISTS_H */
