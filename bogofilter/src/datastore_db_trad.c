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
#include "xmalloc.h"
#include "xstrdup.h"

/* public -- used in datastore.c */
static int	   bft_begin		(void *vhandle);
static int  	   bft_abort		(void *vhandle);
static int  	   bft_commit		(void *vhandle);
/* private -- used in datastore_db_*.c */
static DB_ENV	  *bft_get_env_dbe	(dbe_t *env);
static const char *bft_database_name	(const char *db_file);
static DB_ENV	  *bft_recover_open	(const char *db_file, DB **dbp);
static int	   bft_auto_commit_flags(void);
static int	   bft_get_rmw_flag	(int open_mode);
static int	   bft_lock		(void *handle, int open_mode);
static ex_t	   bft_common_close	(DB_ENV *dbe, const char *db_file);
static int	   bft_sync		(DB_ENV *env, int ret);
static void	   bft_log_flush	(DB_ENV *env);
static dbe_t	  *bft_init		(const char *directory);
static void 	   bft_cleanup_lite	(dbe_t *env);

/* OO function lists */

dsm_t dsm_traditional = {
    /* public -- used in datastore.c */
    &bft_begin,
    &bft_abort,
    &bft_commit,

    /* private -- used in datastore_db_*.c */
    &bft_init,
    &bft_cleanup_lite,
    &bft_get_env_dbe,
    &bft_database_name,
    &bft_recover_open,
    &bft_auto_commit_flags,
    &bft_get_rmw_flag,
    &bft_lock,
    &bft_common_close,
    &bft_sync,
    &bft_log_flush
};

DB_ENV *bft_get_env_dbe	(dbe_t *env)
{
    (void) env;
    return NULL;
}

const char *bft_database_name(const char *db_file)
{
    return db_file;
}

int  bft_auto_commit_flags(void)
{
    return 0;
}

ex_t bft_common_close(DB_ENV *dbe, const char *db_file)
{
    (void) dbe;
    (void) db_file;
    return EX_OK;
}

int bft_sync(DB_ENV *env, int ret)
{
    (void) env;
    (void) ret;
    return 0;
}

int bft_lock(void *vhandle, int open_mode)
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
	if (handle->fd > 0)
	    handle->locked = true;
    }
    return e;
}

int bft_get_rmw_flag(int open_mode)
{
    (void) open_mode;
    return 0;
}

DB_ENV *bft_recover_open(const char *db_file, DB **dbp)
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

void bft_log_flush(DB_ENV *dbe)
{
    int ret;

    ret = BF_LOG_FLUSH(dbe, NULL);

    if (DEBUG_DATABASE(1))
	fprintf(dbgout, "DB_ENV->log_flush(%p): %s\n", (void *)dbe,
		db_strerror(ret));
}

int  bft_begin	(void *vhandle) { (void) vhandle; return 0; }
int  bft_abort	(void *vhandle) { (void) vhandle; return 0; }
int  bft_commit	(void *vhandle) { (void) vhandle; return 0; }

dbe_t *bft_init(const char *directory)
{
    dbe_t *env = xcalloc(1, sizeof(dbe_t));

    env->magic = MAGIC_DBE;	    /* poor man's type checking */
    env->directory = xstrdup(directory);

    return env;
}

void bft_cleanup_lite(dbe_t *env)
{
    (void) env;
}
