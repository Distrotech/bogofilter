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

#include "datastore.h"

extern	u_int32_t db_max_locks;		/* set_lk_max_locks   */
extern	u_int32_t db_max_objects;	/* set_lk_max_objects */

extern	bool	  db_log_autoremove;	/* DB_LOG_AUTOREMOVE  */
extern	bool	  db_txn_durable;	/* not DB_TXN_NOT_DURABLE */

/** Initialize database, open and lock files, etc.
 * params: char * path to database file, char * name of database
 * \return opaque pointer to database handle, which must be saved and
 * passed as the first parameter in all subsequent database function calls. 
 */
/*@only@*/ /*@null@*/
void *db_open(void *env,	/**< database environment to open DB in */
	      const char *path,	/**< path to database file */
	      const char *name,	/**< name(s) of data base(s) */
	      dbmode_t mode	/**< open mode, DS_READ or DS_WRITE */);

/** Close file and clean up. */
void  db_close(/*@only@*/ void *vhandle);

/** Flush pending writes to disk */
void db_flush(void *handle);

/** Do global initializations. \return pointer to environment for success, NULL for
 * error. */
void *dbe_init(bfdir *directory, bffile *file);

/** Cleanup storage allocation */
void dbe_cleanup(void *);

/** Retrieve the value associated with a given word in a list.
 * \return zero if the word does not exist in the database.
 */
int db_get_dbvalue(void *handle, const dbv_t *token, /*@out@*/ dbv_t *val);

/** Delete the key */
int db_delete(void *handle, const dbv_t *data);

/** Set the value associated with a given word in a list. */
int db_set_dbvalue(void *handle, const dbv_t *token, const dbv_t *val);

/** Callback hook used by db_foreach, passes the original \p userdata
 * down as well as \a token and \a data. If the function returns a
 * nonzero value, the traversal is aborted. */
typedef ex_t (*db_foreach_t)(dbv_t *token, dbv_t *data, void *userdata);
/** Iterate over all elements in data base and call \p hook for each item.
 * \p userdata is passed through to the hook function unaltered. */
ex_t db_foreach(void *handle, db_foreach_t hook, void *userdata);

/** Returns error string associated with \a code. */
const char *db_str_err(int code);

/** Returns version string (pointer to a static buffer). */
const char *db_version_str(void);

/* help messages and option processing */
const char **dsm_help_bogofilter(void);
const char **dsm_help_bogoutil(void);

/** parse bogofilter option
 * \return true if an option was recognized, false otherwise */
bool dsm_options_bogofilter(int option, const char *name, const char *val);

/** parse bogoutil option
 * \return true if an option was recognized, false otherwise */
bool dsm_options_bogoutil(int option, cmd_t *flag, int *count, const char **ds_file, const char *name, const char *val);

/** Begin new transaction. */
int db_txn_begin(void *vhandle);

/** Abort a pending transaction. */
int db_txn_abort(void *vhandle);

/** Commit a pending transaction. */
int db_txn_commit(void *vhandle);

/** Recover the environment given in \a directory. */
ex_t dbe_recover(bfdir *directory, bool catastrophic, bool force);

/** Remove the environment from \a directory. */
ex_t dbe_remove(bfdir *directory);

/** Checkpoint environment in \a directory. */
ex_t dbe_checkpoint(bfdir *directory);

/** Mark inactive and remove older write-ahead log files. */
ex_t dbe_purgelogs(bfdir *directory);

/** Check if \a databasefile is a valid database. */
ex_t db_verify(bfdir *directory, bffile *databasefile);

/** Returns true if the database is byteswapped. */
bool db_is_swapped(void *vhandle);

/** Returns true if the database has been created in this session. */
bool db_created(void *vhandle);

/** Returns parent environment handle. */
void *db_get_env(void *vhandle);

int db_lock(int fd, int cmd, short int type);

#endif
