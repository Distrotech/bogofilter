/* $Id$ */

/*****************************************************************************

NAME:
datastore_db_trad.c -- implements bogofilter's traditional
		       (non-transactional) datastore, 
		       using Berkeley DB

AUTHORS:
Gyepi Sam <gyepi@praxis-sw.com>   2002 - 2003
Matthias Andree <matthias.andree@gmx.de> 2003 - 2004
David Relson	<relson@osagesoftware.com> 2003 - 2005

******************************************************************************/

#define DONT_TYPEDEF_SSIZE_T 1
#include "common.h"

#include <errno.h>
#include <unistd.h>

#include <db.h>

#include "datastore.h"
#include "datastore_db_private.h"
#include "datastore_db.h"
#include "datastore_dbcommon.h"

#include "db_lock.h"
#include "error.h"

/* public -- used in datastore.c */
static int	   tra_begin		(void *vhandle);
static int  	   tra_abort		(void *vhandle);
static int  	   tra_commit		(void *vhandle);
/* private -- used in datastore_db_*.c */
static DB_ENV	  *tra_get_env_dbe	(dbe_t *env);
static const char *tra_database_name	(const char *db_file);
static DB_ENV	  *tra_recover_open	(const char *db_file, DB **dbp);
static int	   tra_auto_commit_flags(void);
static int	   tra_get_rmw_flag	(int open_mode);
static int	   tra_lock		(void *handle, int open_mode);
static ex_t	   tra_common_close	(DB_ENV *dbe, const char *db_file);
static int	   tra_sync		(DB_ENV *env, int ret);
static void	   tra_log_flush	(DB_ENV *env);

/* OO function lists */

dsm_t dsm_traditional = {
    /* public -- used in datastore.c */
    &tra_begin,
    &tra_abort,
    &tra_commit,
    /* private -- used in datastore_db_*.c */
    &tra_get_env_dbe,
    &tra_database_name,
    &tra_recover_open,
    &tra_auto_commit_flags,
    &tra_get_rmw_flag,
    &tra_lock,
    &tra_common_close,
    &tra_sync,
    &tra_log_flush
};

DB_ENV *tra_get_env_dbe	(dbe_t *env)
{
    (void) env;
    return NULL;
}

const char *tra_database_name(const char *db_file)
{
    return db_file;
}

int  tra_auto_commit_flags(void)
{
    return 0;
}

ex_t tra_common_close(DB_ENV *dbe, const char *db_file)
{
    (void) dbe;
    (void) db_file;
    return EX_OK;
}

int tra_sync(DB_ENV *env, int ret)
{
    (void) env;
    (void) ret;
    return 0;
}

int tra_lock(void *vhandle, int open_mode)
{
    int e = 0;
    dbh_t *handle = vhandle;

    /* try fcntl lock */
    handle->locked = false;
    if (db_lock(handle->fd, F_SETLK,
		(short int)(open_mode == DS_READ ? F_RDLCK : F_WRLCK)))
    {
	e = errno;
	db_close(handle);
	errno = e;
	if (errno == EACCES)
	    e = errno = EAGAIN;
    } else {
	/* have lock */
	if (handle && handle->fd > 0)
	    handle->locked = true;
    }
    return e;
}

int tra_get_rmw_flag(int open_mode)
{
    (void) open_mode;
    return 0;
}

DB_ENV *tra_recover_open(const char *db_file, DB **dbp)
{
    int e;
    int fd;

    fd = open(db_file, O_RDWR);
    if (fd < 0) {
	print_error(__FILE__, __LINE__, "db_verify: cannot open %s: %s", db_file,
		    strerror(errno));
	exit(EX_ERROR);
    }

    if (db_lock(fd, F_SETLKW, (short int)F_WRLCK)) {
	print_error(__FILE__, __LINE__,
		    "db_verify: cannot lock %s for exclusive use: %s", db_file,
		    strerror(errno));
	close(fd);
	exit(EX_ERROR);
    }

    if ((e = db_create (dbp, NULL, 0)) != 0) {
	print_error(__FILE__, __LINE__, "db_create, err: %s",
		    db_strerror(e));
	close(fd);
	exit(EX_ERROR);
    }

    return NULL;
}

void tra_log_flush(DB_ENV *dbe)
{
    int ret;

    ret = BF_LOG_FLUSH(dbe, NULL);

    if (DEBUG_DATABASE(1))
	fprintf(dbgout, "DB_ENV->log_flush(%p): %s\n", (void *)dbe,
		db_strerror(ret));
}

int  tra_begin	(void *vhandle) { (void) vhandle; return 0; }
int  tra_abort	(void *vhandle) { (void) vhandle; return 0; }
int  tra_commit	(void *vhandle) { (void) vhandle; return 0; }

