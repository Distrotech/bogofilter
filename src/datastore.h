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

#ifndef DATASTORE_H
#define DATASTORE_H

#include "common.h"

#include <stdlib.h>

#include "word.h"

extern YYYYMMDD today;		/* date as YYYYMMDD */

/** Name of the special token that counts the spam and ham messages
 * in the data base.
 */
#define MSG_COUNT ".MSG_COUNT"

/** Datastore handle type
** - used to communicate between datastore layer and database layer
** - known to program layer as a void*
*/
typedef struct {
    /** database handle from db_open() */
    void   *dbh;
    /** tracks endianness */
    bool is_swapped;
} dsh_t;

/** Datastore value type, used to communicate between program layer and
 * datastore layer.
 */
typedef struct {
    /** spam and ham counts */
    u_int32_t count[IX_SIZE];
    /** time stamp */
    u_int32_t date;
} dsv_t;

#define	spamcount count[IX_SPAM]
#define	goodcount count[IX_GOOD]

/** Status value used when a key is not found in the data base. */
#define DS_NOTFOUND (-1)
/** Status value when the transaction was aborted to resolve a deadlock
 * and should be retried. */
#define DS_ABORT_RETRY (-2)

/** Macro that clamps its argument to INT_MAX and casts it to int. */
#define CLAMP_INT_MAX(i) ((int)min(INT_MAX, (i)))

/** Database value type, used to communicate between datastore layer and
 * database layer.
 */
typedef struct {
    /** address of buffer    */
    void     *data;
    /** number of data bytes */
    u_int32_t leng;
} dbv_t;

/** Type of the callback function that ds_foreach calls. */
typedef int ds_foreach_t(
	/** current token that ds_foreach is looking at */
	word_t *token,
	/** data store value */
	dsv_t *data,
	/** unaltered value from ds_foreach call. */
	void *userdata);
/** Iterate over all records in data base and call \p hook for each item.
 * \p userdata is passed through to the hook function unaltered.
 */
extern int ds_foreach(void *vhandle /** data store handle */,
	ds_foreach_t *hook /** callback function */,
	void *userdata /** opaque data that is passed to the callback function
			 unaltered */);

/** Wrapper for ds_foreach that opens and closes file */
extern int ds_oper(void *dbenv,		/**< parent environment */
	           const char *path,	/**< path to database file */
		   dbmode_t open_mode,	/**< open mode, DS_READ or DS_WRITE */
		   ds_foreach_t *hook,	/**< function for actual operations */
		   void *userdata	/**< user data for \a hook */);

/** Initialize database, open and lock files, etc.
 * params: char * path to database file, char * name of database
 * \return opaque pointer to database handle, which must be saved and
 * passed as the first parameter in all subsequent database function calls. 
 */
/*@only@*/ /*@null@*/
extern void *ds_open(void *dbev,	/**< parent environment */
		     const char *path,	/**< path to database file */
		     const char *name,	/**< name(s) of data base(s) */
		     dbmode_t mode	/**< open mode, DS_READ or DS_WRITE */);

/** Close file and clean up. */
extern void  ds_close(/*@only@*/ void *vhandle);

/** Flush pending writes to disk */
extern void ds_flush(void *vhandle);

/** Global initialization of datastore layer. The directory holds the
 * database environment (only for BerkeleyDB TXN store, otherwise
 * ignored), you can - for now - pass bogohome. */
extern void *ds_init(const char *directory);

/** Cleanup storage allocation of datastore layer. After calling this,
 * datastore access is no longer permitted. */
extern void ds_cleanup(void *);

/** Initialize datastore handle. */
dsh_t *dsh_init(
    void *dbh);			/* database handle from db_open() */

/** Free data store handle that must not be used after calling this
 * function. */
void dsh_free(void *vhandle);

/** Retrieve the value associated with a given word in a list. 
 * \return zero if the word does not exist in the database. Front-end
 */
extern int  ds_read  (void *vhandle, const word_t *word, /*@out@*/ dsv_t *val);

/** Retrieve the value associated with a given word in a list. 
 * \return zero if the word does not exist in the database. Implementation
 */
extern int ds_get_dbvalue(void *vhandle, const dbv_t *token, /*@out@*/ dbv_t *val);

/** Delete the key. */
extern int  ds_delete(void *vhandle, const word_t *word);

/** Set the value associated with a given word in a list. Front end. */
extern int  ds_write (void *vhandle, const word_t *word, dsv_t *val);

/** Set the value associated with a given word in a list. Implementation. */
extern int ds_set_dbvalue(void *vhandle, const dbv_t *token, dbv_t *val);

/** Update the value associated with a given word in a list. */
extern void ds_updvalues(void *vhandle, const dbv_t *token, const dbv_t *updval);

/** Get the database message count. */
extern int ds_get_msgcounts(void *vhandle, dsv_t *val);

/** Set the database message count. */
extern int ds_set_msgcounts(void *vhandle, dsv_t *val);

/** Get the parent environment. */
extern void *ds_get_dbenv(void *vhandle);

/* transactional code */
/** Start a transaction for the data store identified by vhandle.
 * All data base operations, including reading, must be "opened" by
 * ds_txn_begin and must be "closed" by either ds_txn_commit (to keep
 * changes) or ds_txn_abort (to discard changes made since the last
 * ds_txn_begin for the data base). Application or system crash will
 * lose any changes made since ds_txn_begin that have not been
 * acknowledged by successful ds_txn_commit().
 * \returns
 * - DST_OK for success. It is OK to proceed in data base access.
 * - DST_TEMPFAIL for problem. It is unknown whether this actually
 *   happens. You must not touch the data base.
 * - DST_FAILURE for problem. You must not touch the data base.
 */
extern int ds_txn_begin(void *vhandle);

/** Commit a transaction, keeping changes. As with any transactional
 * data base, concurrent updates to the same pages in the data base can
 * cause a deadlock of the writers. The datastore_xxx.c code will handle
 * the detection for you, in a way that it aborts as many transactions
 * until one can proceed. The aborted transactions will return
 * DST_TEMPFAIL and must be retried. No data base access must happen
 * after this call until the next ds_txn_begin().
 * \returns
 * - DST_OK to signify that the data has made it to the disk
 *   (which means nothing if the disk's write cache is enabled and the
 *   kernel has no means of synchronizing the cache - this is unknown for
 *   most kernels)
 * - DST_TEMPFAIL when a transaction has been aborted by the deadlock
 *   detector and must be retried
 * - DST_FAILURE when a permanent error has occurred that cannot be
 *   recovered from by the application (for instance, because corruption
 *   has occurred and needs to be recovered).
 */
extern int ds_txn_commit(void *vhandle);

/** Abort a transaction, discarding all changes since the previous
 * ds_txn_begin(). Changes are rolled back as though the transaction had
 * never been tried. No data base access must happen after this call
 * until the next ds_txn_begin().
 * \returns
 * - DST_OK for success.
 * - DST_TEMPFAIL for failure. It is uncertain if this actually happens.
 * - DST_FAILURE for failure. The application cannot continue.
 */
extern int ds_txn_abort(void *vhandle);

/** Successful return from ds_txn_* operation. */
#define DST_OK (0)
/** Temporary failure return from ds_txn_* operation, the application
 * should retry the failed data base transaction. */
#define DST_TEMPFAIL (1)
/** Permanent failure return from ds_txn_* operation, the application
 * should clean up and exit. */
#define DST_FAILURE (2)

/** Get the database version */
extern int ds_get_wordlist_version(void *vhandle, dsv_t *val);

/** set the database version */
extern int ds_set_wordlist_version(void *vhandle, dsv_t *val);

/** Get the current process ID. */
extern unsigned long ds_handle_pid(void *vhandle);

/** Get the database filename. */
extern char *ds_handle_filename(void *vhandle);

/** Locks and unlocks file descriptor. */
extern int ds_lock(int fd, int cmd, short int type);

/** Returns version string. */
extern const char *ds_version_str(void);

/** Runs forced recovery on data base */
extern int ds_recover(const char *directory, bool catastrophic);

/** Remove environment in given directory, \return EX_OK or EX_ERROR */
extern int ds_remove(const char *directory);

#endif