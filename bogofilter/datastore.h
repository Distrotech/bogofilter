/* $Id$ */

/* 
 API for bogofilter datastore.
 The idea here is to make bogofilter independent of the database system
 used to store words. The interface specified by this file determines
 the entire interaction between bogofilter and the database
 Writing a new database backend merely requires the implementation of the interface
 
 Authors: Gyepi Sam <gyepi@praxis-sw.com>   2002 - 2003
          Matthias Andree <matthias.andree@gmx.de> 2003
 
*/

#ifndef DATASTORE_H
#define DATASTORE_H

#include "wordlists.h"

/*
Initialize database, open files, etc.
params: char * path to database file, char * name of database
returns: opaque pointer to database handle, which must be saved and
         passed as the first parameter in all subsequent database function calls. 
*/
typedef struct {
    long count;
    long date;
} dbv_t;

void *db_open(const char *, const char *, dbmode_t, const char *);

/* Close files and clean up. */
void  db_close(void *);

/* Flush pending writes to disk */
void db_flush(void *);

/*
Increments count for given word.
Note: negative results are set to zero, 
*/
void db_increment(void *, const char *, long);

/*
Decrement count for a given word, if it exists in the datastore.
Note: negative results are set to zero, 
*/
void db_decrement(void *, const char *, long);

/*
Retrieve the value associated with a given word in a list
Returns zero if the word does not exist in the database.
*/
long db_getvalue(void *, const char *);

/* Delete the key */
void db_delete(void *, const char *);

/* Set the value associated with a given word in a list */
void db_setvalue(void *, const char *, long);

/* Get the database message count */
long db_getcount(void*);

/* set the database message count */
void db_setcount(void*, long);

typedef int (*db_foreach_t)(char *key, long key_size,
	char *value, long key_value, void *userdata);
int db_foreach(void *, db_foreach_t hook, void *userdata);

#endif
