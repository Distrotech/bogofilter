/* $Id$ */

/*****************************************************************************

NAME:
datastore_db_trans_stub.c -- stub routines for bogofilter's transactional
		       datastore, using Berkeley DB

AUTHORS:

David Relson	<relson@osagesoftware.com> 2005

******************************************************************************/

#include "common.h"

#include <db.h>

#include "datastore.h"
#include "datastore_db.h"
#include "datastore_db_private.h"

/** Default flags for DB_ENV->open() */

u_int32_t db_max_locks = 16384;		/* set_lk_max_locks    32768 */
u_int32_t db_max_objects = 16384;	/* set_lk_max_objects  32768 */

bool	  db_log_autoremove = false;	/* DB_LOG_AUTOREMOVE */

#ifdef	FUTURE_DB_OPTIONS
bool	  db_txn_durable = true;	/* not DB_TXN_NOT_DURABLE */
#endif

/* OO function lists */

dsm_t dsm_transactional = {
    /* public -- used in datastore.c */
    NULL,
    NULL,
    NULL,

    /* private -- used in datastore_db_*.c */
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

#if	!defined(DISABLE_TRANSACTIONS) && !defined(ENABLE_TRANSACTIONS)
void *dbe_init(const char *unused, const char *unused2) {
    (void)unused;
    (void)unused2;
    return (void *)~0;
}
#endif

int db_txn_begin(void *vhandle) { (void)vhandle; return 0; }
int db_txn_abort(void *vhandle) { (void)vhandle; return 0; }
int db_txn_commit(void *vhandle) { (void)vhandle; return 0; }

ex_t dbe_recover(const char *directory, bool catastrophic, bool force)
{
    (void) directory;
    (void) catastrophic;
    (void) force;

    fprintf(stderr,
	    "ERROR: bogofilter can not recover databases without transaction support.\n"
	    "If you experience hangs, strange behavior, inaccurate output,\n"
	    "you must delete your data base and rebuild it, or restore an older version\n"
	    "that you know is good from your backups.\n");
    exit(EX_ERROR);
}

#if	!defined(DISABLE_TRANSACTIONS) && !defined(ENABLE_TRANSACTIONS)
void *db_get_env(void *vhandle) {
    (void)vhandle;
    return NULL;
}
#endif

ex_t dbe_purgelogs(const char *directory)
{
    (void) directory;
    return EX_OK;
}

ex_t dbe_remove(const char *directory)
{
    (void) directory;
    return EX_OK;
}