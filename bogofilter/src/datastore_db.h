/* $Id$ */

/*****************************************************************************

NAME:
datastore.h - API for bogofilter datastore.  

   The idea here is to make bogofilter independent of the database
   system used to store words.  The interface specified by this file
   determines the entire interaction between bogofilter and the
   database.  Writing a new database backend merely requires the
   implementation of the interface.

AUTHORS:
Gyepi Sam <gyepi@praxis-sw.com>   2002 - 2003
Matthias Andree <matthias.andree@gmx.de> 2003

******************************************************************************/

#ifndef DATASTORE_DB_H
#define DATASTORE_DB_H

/** Initialize database, open and lock files, etc.
 * params: char * path to database file, char * name of database
 * \return opaque pointer to database handle, which must be saved and
 * passed as the first parameter in all subsequent database function calls. 
 */
/*@only@*/ /*@null@*/
void *db_open(const char *path	/** path to database file */, 
	      size_t count	/** number of data base(s) */,
	      const char **name /** name(s) of data base(s) */,
	      dbmode_t mode	/** open mode, DB_READ or DB_WRITE */);

/** Close file and clean up. */
void  db_close(/*@only@*/ void *vhandle, bool nosync  /** Normally false, if true, do not synchronize data. This should not be used in regular operation but only to ease the disk I/O load when the lock operation failed. */);

/** Flush pending writes to disk */
void db_flush(void *vhandle);

/** Cleanup storage allocation */
void db_cleanup(void);

bool db_is_swapped(void *vhandle);

/** Retrieve the value associated with a given word in a list.
 * \return zero if the word does not exist in the database. Front-end
 */
bool db_getvalues(void *vhandle, const dbv_t *key, dbv_t *val);

/** Retrieve the value associated with a given word in a list. 
 * \return zero if the word does not exist in the database. Implementation
 */
int db_get_dbvalue(void *vhandle, const dbv_t *token, /*@out@*/ dbv_t *val);

/** Delete the key */
int db_delete(void *vhandle, const dbv_t *data);

/** Set the value associated with a given word in a list. Front end */
int db_setvalues(void *vhandle, const dbv_t *token, dbv_t *val);

/** Set the value associated with a given word in a list. Implementation */
int db_set_dbvalue(void *vhandle, const dbv_t *token, dbv_t *val);

/** Update the value associated with a given word in a list */
void db_updvalues(void *vhandle, const dbv_t *token, const dbv_t *updval);

/** Iterate over all elements in data base and call \p hook for each item.
 * \p userdata is passed through to the hook function unaltered.
 */
typedef int (*db_foreach_t)(dbv_t *token, dbv_t *data, void *userdata);
int db_foreach(void *vhandle, db_foreach_t hook, void *userdata);

/* Get the current process id */
unsigned long db_handle_pid(void *vhandle);

/* Get the database filename */
char *db_handle_filename(void *vhandle);

/* Locks and unlocks file descriptor */
int db_lock(int fd, int cmd, short int type);

/* Prints wordlist name(s) */
void dbh_print_names(void *vhandle, const char *msg);

/* Returns version string */
const char *db_version_str(void);

/* This is not currently used ...
 * 
#define db_write_lock(fd) db_lock(fd, F_SETLKW, F_WRLCK)
#define db_read_lock(fd) db_lock(fd, F_SETLKW, F_RDLCK)
#define db_unlock(fd) db_lock(fd, F_SETLK, F_UNLCK)

*/

#endif
