/* $Id$ */

/*****************************************************************************

NAME:
datastore_db.c -- implements the datastore, using Berkeley DB

AUTHORS:
Gyepi Sam <gyepi@praxis-sw.com>   2002 - 2003
Matthias Andree <matthias.andree@gmx.de> 2003 - 2004

******************************************************************************/

/* To avoid header file conflicts the order is:
**	1. System header files
**	2. Header files for external packages
**	3. Bogofilter's header files
*/

/* This code has been tested with BerkeleyDB 3.1 3.2, 3.3, 4.0,
 * 4.1 and 4.2.  -- Matthias Andree, 2004-11-29 */

/* TODO:
 * - implement proper retry when our transaction is aborted after a
 *   deadlock
 * - document code changes
 * - conduct massive tests
 * - check if we really need the log files for "catastrophic recovery"
 *   or if we can remove them (see db_archive documentation)
 *   as the log files are *HUGE* even compared with the data base
 */

/*
 * NOTE: this code is an "#if" nightmare due to the many different APIs
 * in the many different BerkeleyDB versions.
 */

#define DONT_TYPEDEF_SSIZE_T 1
#include "common.h"

#include <assert.h>
#include <error.h>
#include <db.h>

#include "datastore.h"
#include "datastore_db_private.h"
#include "datastore_db.h"
#include "datastore_dbcommon.h"

/** Default flags for DB_ENV->open() */
static const u_int32_t dbenv_defflags = DB_INIT_MPOOL | DB_INIT_LOCK
				      | DB_INIT_LOG | DB_INIT_TXN;

u_int32_t db_max_locks = 16384;		/* set_lk_max_locks    32768 */
u_int32_t db_max_objects = 16384;	/* set_lk_max_objects  32768 */

#ifdef	FUTURE_DB_OPTIONS
bool	  db_log_autoremove = false;	/* DB_LOG_AUTOREMOVE */
bool	  db_txn_durable = true;	/* not DB_TXN_NOT_DURABLE */
#endif

static DB_ENV	  *txn_get_env_dbe	(dbe_t *env);
static DB_ENV	  *txn_recover_open	(const char *db_file, DB **dbp);
static int	   txn_auto_commit_flags(void);
static int	   txn_get_rmw_flag	(int open_mode);
static int	  txn_begin		(void *vhandle);
static int  	  txn_abort		(void *vhandle);
static int  	  txn_commit		(void *vhandle);

/* OO function lists */

dsm_t dsm_traditional = {
    &txn_get_env_dbe,
    &txn_recover_open,
    &txn_auto_commit_flags,
    &txn_get_rmw_flag,
    &txn_begin,
    &txn_abort,
    &txn_commit,
};

DB_ENV *txn_get_env_dbe(dbe_t *env)
{
    return env->dbe;
}

int  txn_auto_commit_flags(void)
{
#if DB_AT_LEAST(4,1)
    return DB_AUTO_COMMIT;
#else
    return 0;
#endif
}

int txn_get_rmw_flag(int open_mode)
{
    int flag;

    if (open_mode == DS_READ)
	flag = 0;
    else
	flag = DB_RMW;

    return flag;
}

static DB_ENV *dbe_recover_open(const char *db_file, uint32_t flags)
{
    const uint32_t local_flags = flags | DB_CREATE;
    DB_ENV *env;
    int e;

    char *dir;
    char *tmp;

    dir = xstrdup(db_file);
    tmp = strrchr(dir, DIRSEP_C);

    if (!tmp)
	free(dir), dir = xstrdup(CURDIR_S);
    else
	*tmp = '\0';

    if (DEBUG_DATABASE(0))
        fprintf(dbgout, "trying to lock database directory\n");
    db_try_glock(tmp, F_WRLCK, F_SETLKW); /* wait for exclusive lock */

    /* run recovery */
    bf_dbenv_create(&env);

    if (DEBUG_DATABASE(0))
        fprintf(dbgout, "running regular data base recovery%s\n",
	       flags & DB_PRIVATE ? " and removing environment" : "");

    /* quirk: DB_RECOVER requires DB_CREATE and cannot work with DB_JOINENV */

    /*
     * Hint from Keith Bostic, SleepyCat support, 2004-11-29,
     * we can use the DB_PRIVATE flag, that rebuilds the database
     * environment in heap memory, so we don't need to remove it.
     */

    e = env->open(env, tmp,
		  dbenv_defflags | local_flags | DB_RECOVER, DS_MODE);
    if (e == DB_RUNRECOVERY) {
	/* that didn't work, try harder */
	if (DEBUG_DATABASE(0))
	    fprintf(dbgout, "running catastrophic data base recovery\n");
	e = env->open(env, tmp,
		      dbenv_defflags | local_flags | DB_RECOVER_FATAL, DS_MODE);
    }
    if (e) {
	print_error(__FILE__, __LINE__, "Cannot recover environment \"%s\": %s",
		tmp, db_strerror(e));
	free(tmp);
	exit(EX_ERROR);
    }

    free(tmp);

    return env;
}

static int txn_begin(void *vhandle)
{
    DB_TXN *t;
    int ret;

    dbh_t *dbh = vhandle;
    dbe_t *env;

    assert(dbh);
    assert(dbh->magic == MAGIC_DBH);
    assert(dbh->txn == 0);

    env = dbh->dbenv;

    assert(env);
    assert(env->dbe);

    ret = BF_TXN_BEGIN(env->dbe, NULL, &t, 0);
    if (ret) {
	print_error(__FILE__, __LINE__, "DB_ENV->txn_begin(%p), err: %s",
		(void *)env->dbe, db_strerror(ret));
	return ret;
    }
    dbh->txn = t;

    if (DEBUG_DATABASE(2))
	fprintf(dbgout, "DB_ENV->txn_begin(%p), tid: %lx\n",
		(void *)env->dbe, (unsigned long)BF_TXN_ID(t));

    return 0;
}

static int txn_abort(void *vhandle)
{
    int ret;
    dbh_t *dbh = vhandle;
    DB_TXN *t;

    assert(dbh);
    assert(dbh->magic == MAGIC_DBH);

    t = dbh->txn;

    assert(t);

    ret = BF_TXN_ABORT(t);
    if (ret)
	print_error(__FILE__, __LINE__, "DB_TXN->abort(%lx) error: %s",
		(unsigned long)BF_TXN_ID(t), db_strerror(ret));
    else
	if (DEBUG_DATABASE(2))
	    fprintf(dbgout, "DB_TXN->abort(%lx)\n",
		    (unsigned long)BF_TXN_ID(t));

    dbh->txn = NULL;

    switch (ret) {
	case 0:
	    return DST_OK;
	case DB_LOCK_DEADLOCK:
	    return DST_TEMPFAIL;
	default:
	    return DST_FAILURE;
    }
}

static int txn_commit(void *vhandle)
{
    int ret;
    dbh_t *dbh = vhandle;
    DB_TXN *t;
    u_int32_t id;

    assert(dbh);
    assert(dbh->magic == MAGIC_DBH);

    t = dbh->txn;

    assert(t);

    id = BF_TXN_ID(t);
    ret = BF_TXN_COMMIT(t, 0);
    if (ret)
	print_error(__FILE__, __LINE__, "DB_TXN->commit(%lx) error: %s",
		(unsigned long)id, db_strerror(ret));
    else
	if (DEBUG_DATABASE(2))
	    fprintf(dbgout, "DB_TXN->commit(%lx, 0)\n",
		    (unsigned long)id);

    dbh->txn = NULL;

    switch (ret) {
	case 0:
	    /* push out buffer pages so that >=15% are clean - we
	     * can ignore errors here, as the log has all the data */
	    BF_MEMP_TRICKLE(dbh->dbenv->dbe, 15, NULL);

	    return DST_OK;
	case DB_LOCK_DEADLOCK:
	    return DST_TEMPFAIL;
	default:
	    return DST_FAILURE;
    }
}

/** set an fcntl-style lock on \a path.
 * \a locktype is F_RDLCK, F_WRLCK, F_UNLCK
 * \a mode is F_SETLK or F_SETLKW
 * \return file descriptor of locked file if successful
 * negative value in case of error
 */
static int plock(const char *path, short locktype, int mode)
{
    struct flock fl;
    int fd, r;

    fd = open(path, O_RDWR);
    if (fd < 0) return fd;

    fl.l_type = locktype;
    fl.l_whence = SEEK_SET;
    fl.l_start = (off_t)0;
    fl.l_len = (off_t)0;
    r = fcntl(fd, mode, &fl);
    if (r < 0)
	return r;
    return fd;
}

static int db_try_glock(const char *directory, short locktype, int lockcmd)
{
    int ret;
    char *t;
    const char *const tackon = DIRSEP_S "lockfile-d";

    assert(directory);

    /* lock */
    ret = mkdir(directory, DIR_MODE);
    if (ret && errno != EEXIST) {
	print_error(__FILE__, __LINE__, "mkdir(%s): %s",
		directory, strerror(errno));
	exit(EX_ERROR);
    }

    t = mxcat(directory, tackon, NULL);

    /* All we are interested in is that this file exists, we'll close it
     * right away as plock down will open it again */
    ret = open(t, O_RDWR|O_CREAT|O_EXCL, DS_MODE);
    if (ret < 0 && errno != EEXIST) {
	print_error(__FILE__, __LINE__, "open(%s): %s",
		t, strerror(errno));
	exit(EX_ERROR);
    }

    if (ret >= 0)
	close(ret);

    lockfd = plock(t, locktype, lockcmd);
    if (lockfd < 0 && errno != EAGAIN && errno != EACCES) {
	print_error(__FILE__, __LINE__, "lock(%s): %s",
		t, strerror(errno));
	exit(EX_ERROR);
    }

    free(t);
    /* lock set up */
    return lockfd;
}

/** Create environment or exit with EX_ERROR */
static int bf_dbenv_create(DB_ENV **env)
{
    int ret = db_env_create(env, 0);
    if (ret != 0) {
	print_error(__FILE__, __LINE__, "db_env_create, err: %s",
		db_strerror(ret));
	exit(EX_ERROR);
    }
    if (DEBUG_DATABASE(1))
	fprintf(dbgout, "db_env_create: %p\n", (void *)env);
    (*env)->set_errfile(*env, stderr);

    return ret;
}

static void dbe_config(void *vhandle, u_int32_t numlocks, u_int32_t numobjs)
{
    dbe_t *env = vhandle;
    int ret = 0;
    u_int32_t logsize = 1048576;    /* 1 MByte (default in BDB 10 MByte) */

    /* configure lock system size - locks */
#if DB_AT_LEAST(3,2)
    if ((ret = env->dbe->set_lk_max_locks(env->dbe, numlocks)) != 0)
#else
    if ((ret = env->dbe->set_lk_max(env->dbe, numlocks)) != 0)
#endif
    {
	print_error(__FILE__, __LINE__, "DB_ENV->set_lk_max_locks(%p, %lu), err: %s", (void *)env->dbe,
		(unsigned long)numlocks, db_strerror(ret));
	exit(EX_ERROR);
    }

    if (DEBUG_DATABASE(1))
	fprintf(dbgout, "DB_ENV->set_lk_max_locks(%p, %lu)\n", (void *)env->dbe, (unsigned long)numlocks);

#if DB_AT_LEAST(3,2)
    /* configure lock system size - objects */
    if ((ret = env->dbe->set_lk_max_objects(env->dbe, numobjs)) != 0) {
	print_error(__FILE__, __LINE__, "DB_ENV->set_lk_max_objects(%p, %lu), err: %s", (void *)env->dbe,
		(unsigned long)numobjs, db_strerror(ret));
	exit(EX_ERROR);
    }
    if (DEBUG_DATABASE(1))
	fprintf(dbgout, "DB_ENV->set_lk_max_objects(%p, %lu)\n", (void *)env->dbe, (unsigned long)numlocks);
#else
    /* suppress compiler warning for unused variable */
    (void)numobjs;
#endif

    /* configure automatic deadlock detector */
    if ((ret = env->dbe->set_lk_detect(env->dbe, DB_LOCK_DEFAULT)) != 0) {
	print_error(__FILE__, __LINE__, "DB_ENV->set_lk_detect(DB_LOCK_DEFAULT), err: %s", db_strerror(ret));
	exit(EX_ERROR);
    }

    if (DEBUG_DATABASE(1))
	fprintf(dbgout, "DB_ENV->set_lk_detect(DB_LOCK_DEFAULT)\n");

    /* configure log file size */
    ret = env->dbe->set_lg_max(env->dbe, logsize);
    if (ret) {
	print_error(__FILE__, __LINE__, "DB_ENV->set_lg_max(%lu) err: %s",
		(unsigned long)logsize, db_strerror(ret));
	exit(EX_ERROR);
    }

    if (DEBUG_DATABASE(1))
	fprintf(dbgout, "DB_ENV->set_lg_max(%lu)\n", (unsigned long)logsize);
}

/* dummy infrastructure, to be expanded by environment
 * or transactional initialization/shutdown */
static dbe_t *dbe_xinit(const char *directory, u_int32_t numlocks, u_int32_t numobjs, u_int32_t flags)
{
    int ret;
    dbe_t *env = xcalloc(1, sizeof(dbe_t));

    assert(directory);

    env->magic = MAGIC_DBE;	    /* poor man's type checking */
    env->directory = xstrdup(directory);

    ret = bf_dbenv_create(&env->dbe);

    if (db_cachesize != 0 &&
	    (ret = env->dbe->set_cachesize(env->dbe, db_cachesize/1024, (db_cachesize % 1024) * 1024*1024, 1)) != 0) {
	print_error(__FILE__, __LINE__, "DB_ENV->set_cachesize(%u), err: %s",
		db_cachesize, db_strerror(ret));
	exit(EX_ERROR);
    }

    if (DEBUG_DATABASE(1))
	fprintf(dbgout, "DB_ENV->set_cachesize(%u)\n", db_cachesize);

    dbe_config(env, numlocks, numobjs);

    flags |= DB_CREATE | dbenv_defflags;

    ret = env->dbe->open(env->dbe, directory, flags, DS_MODE);
    if (ret != 0) {
	env->dbe->close(env->dbe, 0);
	print_error(__FILE__, __LINE__, "DB_ENV->open, err: %s", db_strerror(ret));
	switch (ret) {
	    case DB_RUNRECOVERY:
		if (flags & DB_RECOVER) {
		    fprintf(stderr,
			    "\n"
			    "### Standard recovery failed. ###\n"
			    "\n"
			    "Please check section 3.3 in bogofilter's README.db file\n"
			    "for help.\n");
		    /* ask that the user runs catastrophic recovery */
		} else if (flags & DB_RECOVER_FATAL) {
		    fprintf(stderr,
			    "\n"
			    "### Catastrophic recovery failed. ###\n"
			    "\n"
			    "Please check the README.db file that came with bogofilter for hints,\n"
			    "section 3.3, or remove all __db.*, log.* and *.db files in \"%s\"\n"
			    "and start from scratch.\n", directory);
		    /* catastrophic recovery failed */
		} else {
		    fprintf(stderr, "To recover, run: bogoutil -v --db-recover \"%s\"\n",
			    directory);
		}
		break;
	    case EINVAL:
		fprintf(stderr, "\n"
			"If you have just got a message that only private environments are supported,\n"
			"your Berkeley DB %d.%d was not configured properly.\n"
			"Bogofilter requires shared environments to support Berkeley DB transactions.\n",
			DB_VERSION_MAJOR, DB_VERSION_MINOR);
		fprintf(stderr,
			"Reconfigure and recompile Berkeley DB with the right mutex interface,\n"
			"see the docs/ref/build_unix/conf.html file that comes with your db source code.\n"
			"This can happen when the DB library was compiled with POSIX threads\n"
			"but your system does not support NPTL.\n");
		break;
	}

	exit(EX_ERROR);
    }

    if (DEBUG_DATABASE(1))
	fprintf(dbgout, "DB_ENV->open(home=%s)\n", directory);

    return env;
}

/* close the environment, but do not release locks */
static void dbe_cleanup_lite(dbe_t *env)
{
    if (env->dbe) {
	int ret;

	/* checkpoint if more than 64 kB of logs have been written
	 * or 120 min have passed since the previous checkpoint */
	/*                                kB  min flags */
	ret = BF_TXN_CHECKPOINT(env->dbe, 64, 120, 0);
	ret = db_flush_dirty(env->dbe, ret);
	if (ret)
	    print_error(__FILE__, __LINE__, "DBE->txn_checkpoint returned %s", db_strerror(ret));

	ret = env->dbe->close(env->dbe, 0);
	if (DEBUG_DATABASE(1) || ret)
	    fprintf(dbgout, "DB_ENV->close(%p): %s\n", (void *)env->dbe, db_strerror(ret));
    }
    if (env->directory)
	free(env->directory);
    free(env);
}

/* initialize data base, configure some lock table sizes
 * (which can be overridden in the DB_CONFIG file)
 * and lock the file to tell other parts we're initialized and
 * do not want recovery to stomp over us
 */
void *dbe_init(const char *directory)
{
    u_int32_t flags = 0;
    char norm_dir[PATH_MAX+1]; /* check normalized directory names */
    char norm_home[PATH_MAX+1];/* see man realpath(3) for details */

    if (NULL == realpath(directory, norm_dir)) {
	    print_error(__FILE__, __LINE__,
		    "error: cannot normalize path \"%s\": %s",
		    directory, strerror(errno));
	    exit(EX_ERROR);
    }

    if (NULL == realpath(bogohome, norm_home)) {
	    print_error(__FILE__, __LINE__,
		    "error: cannot normalize path \"%s\": %s",
		    bogohome, strerror(errno));
	    exit(EX_ERROR);
    }

    if (0 != strcmp(norm_dir, norm_home))
    {
	fprintf(stderr,
		"ERROR: only one database _environment_ (directory) can be used at a time.\n"
		"You CAN use multiple wordlists that are in the same directory.\n\n");
	fprintf(stderr,
		"If you need multiple wordlists in different directories,\n"
		"you cannot use the transactional interface, but you must configure\n"
		"the non-transactional interface, i. e. ./configure --disable-transactions\n"
		"then type make clean, after that rebuild and install as usual.\n"
		"Note that the data base will no longer be crash-proof in that case.\n"
		"Please accept our apologies for the inconvenience.\n");
	fprintf(stderr,
		"\nAborting program\n");
	exit(EX_ERROR);
    }

    /* open lock file, needed to detect previous crashes */
    if (init_dbl(directory))
	exit(EX_ERROR);

    /* run recovery if needed */
    if (needs_recovery())
	dbe_recover(directory, false, false); /* DO NOT set force flag here, may cause
						 multiple recovery! */

    /* set (or demote to) shared/read lock for regular operation */
    db_try_glock(directory, F_RDLCK, F_SETLKW);

    /* set our cell lock in the crash detector */
    if (set_lock()) {
	exit(EX_ERROR);
    }

    /* initialize */

#ifdef	FUTURE_DB_OPTIONS
#ifdef	DB_LOG_AUTOREMOVE
    if (db_log_autoremove)
	flags ^= DB_LOG_AUTOREMOVE;
#endif

#ifdef	DB_TXN_NOT_DURABLE
    if (db_txn_durable)
	flags ^= DB_TXN_NOT_DURABLE;
#endif
#endif

    return dbe_xinit(directory, db_max_locks, db_max_objects, flags);
}

ex_t dbe_recover(const char *directory, bool catastrophic, bool force)
{
    dbe_t *env;

    /* set exclusive/write lock for recovery */
    while((force || needs_recovery())
	    && (db_try_glock(directory, F_WRLCK, F_SETLKW) <= 0))
	rand_sleep(10000,1000000);

    /* ok, when we have the lock, a concurrent process may have
     * proceeded with recovery */
    if (!(force || needs_recovery()))
	return EX_OK;

retry:
    if (DEBUG_DATABASE(0))
        fprintf(dbgout, "running %s data base recovery\n",
	    catastrophic ? "catastrophic" : "regular");
    env = dbe_xinit(directory, db_max_locks, db_max_objects,
	    catastrophic ? DB_RECOVER_FATAL : DB_RECOVER);
    if (env == NULL) {
	if(!catastrophic) {
	    catastrophic = true;
	    goto retry;
	}
	goto rec_fail;
    }

    clear_lockfile();
    dbe_cleanup_lite(env);

    return EX_OK;

rec_fail:
    exit(EX_ERROR);
}

void dbe_cleanup(void *vhandle)
{
    dbe_t *env = vhandle;

    assert(env->magic == MAGIC_DBE);

    dbe_cleanup_lite(env);
    clear_lock();
    if (lockfd >= 0)
	close(lockfd); /* release locks */
}

static DB_ENV *dbe_recover_open(const char *directory, uint32_t flags)
{
    const uint32_t local_flags = flags | DB_CREATE;
    DB_ENV *env;
    int e;

    if (DEBUG_DATABASE(0))
        fprintf(dbgout, "trying to lock database directory\n");
    db_try_glock(directory, F_WRLCK, F_SETLKW); /* wait for exclusive lock */

    /* run recovery */
    bf_dbenv_create(&env);

    if (DEBUG_DATABASE(0))
        fprintf(dbgout, "running regular data base recovery%s\n",
	       flags & DB_PRIVATE ? " and removing environment" : "");

    /* quirk: DB_RECOVER requires DB_CREATE and cannot work with DB_JOINENV */

    /*
     * Hint from Keith Bostic, SleepyCat support, 2004-11-29,
     * we can use the DB_PRIVATE flag, that rebuilds the database
     * environment in heap memory, so we don't need to remove it.
     */

    e = env->open(env, directory,
	    dbenv_defflags | local_flags | DB_RECOVER, DS_MODE);
    if (e == DB_RUNRECOVERY) {
	/* that didn't work, try harder */
	if (DEBUG_DATABASE(0))
	    fprintf(dbgout, "running catastrophic data base recovery\n");
	e = env->open(env, directory,
		dbenv_defflags | local_flags | DB_RECOVER_FATAL, DS_MODE);
    }
    if (e) {
	print_error(__FILE__, __LINE__, "Cannot recover environment \"%s\": %s",
		directory, db_strerror(e));
	exit(EX_ERROR);
    }

    return env;
}

static ex_t dbe_common_close(DB_ENV *env, const char *directory)
{
    int e;

    e = env->close(env, 0);
    if (e != 0) {
	print_error(__FILE__, __LINE__, "Error closing environment \"%s\": %s",
		directory, db_strerror(e));
	exit(EX_ERROR);
    }

    db_try_glock(directory, F_UNLCK, F_SETLKW); /* release lock */
    return EX_OK;
}

ex_t dbe_purgelogs(const char *directory)
{
    int e;
    DB_ENV *env = dbe_recover_open(directory, 0);
    char **i, **list;

    if (!env)
	exit(EX_ERROR);

    if (DEBUG_DATABASE(0))
	fprintf(dbgout, "checkpoint database\n");

    /* checkpoint the transactional system */
    e = BF_TXN_CHECKPOINT(env, 0, 0, 0);
    e = db_flush_dirty(env, e);
    if (e) {
	print_error(__FILE__, __LINE__, "DB_ENV->txn_checkpoint failed: %s",
		db_strerror(e));
	exit(EX_ERROR);
    }

    if (DEBUG_DATABASE(0))
	fprintf(dbgout, "removing inactive logfiles\n");

    /* figure redundant log files and nuke them */
    e = BF_LOG_ARCHIVE(env, &list, DB_ARCH_ABS);
    if (e) {
	print_error(__FILE__, __LINE__,
		"DB_ENV->log_archive failed: %s",
		db_strerror(e));
	exit(EX_ERROR);
    }

    if (list != NULL) {
	for (i = list; *i != NULL; i++) {
	    if (DEBUG_DATABASE(1))
		fprintf(dbgout, " removing logfile %s\n", *i);
	    if (unlink(*i)) {
		print_error(__FILE__, __LINE__,
			"cannot unlink \"%s\": %s", *i, strerror(errno));
		/* proceed anyways */
	    }
	}
	free(list);
    }

    if (DEBUG_DATABASE(0))
	fprintf(dbgout, "closing environment\n");

    return dbe_common_close(env, directory);
}

ex_t dbe_remove(const char *directory)
{
    DB_ENV *env = dbe_recover_open(directory, DB_PRIVATE);

    if (!env)
	exit(EX_ERROR);

    return dbe_common_close(env, directory);
}
