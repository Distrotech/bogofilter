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

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>		/* for SEEK_SET for SunOS 4.1.x */
#include <sys/resource.h>
#include <assert.h>

#include <db.h>

#include "datastore.h"
#include "datastore_db.h"
#include "datastore_dbcommon.h"
#include "datastore_db_private.h"

#include "bogohome.h"
#include "error.h"
#include "paths.h"		/* for build_path */
#include "rand_sleep.h"
#include "xmalloc.h"
#include "xstrdup.h"

extern dsm_t *dsm;			/* in datastore.c */

extern dsm_t dsm_traditional;		/* in datastore_db_trad.c */
extern dsm_t dsm_transactional;		/* in datastore_db_trans.c */

static const DBTYPE dbtype = DB_BTREE;

/** translate BerkeleyDB \a flags bitfield for DB->open method back to symbols */
static const char *resolveopenflags(u_int32_t flags) {
    static char buf[160];
    char b2[80];
    strlcpy(buf, "", sizeof(buf));
    if (flags & DB_CREATE) flags &= ~DB_CREATE, strlcat(buf, "DB_CREATE ", sizeof(buf));
    if (flags & DB_EXCL) flags &= ~DB_EXCL, strlcat(buf, "DB_EXCL ", sizeof(buf));
    if (flags & DB_NOMMAP) flags &= ~DB_NOMMAP, strlcat(buf, "DB_NOMMAP ", sizeof(buf));
    if (flags & DB_RDONLY) flags &= ~DB_RDONLY, strlcat(buf, "DB_RDONLY ", sizeof(buf));
#if DB_AT_LEAST(4,1)
    if (flags & DB_AUTO_COMMIT) flags &= ~DB_AUTO_COMMIT, strlcat(buf, "DB_AUTO_COMMIT ", sizeof(buf));
#endif
    snprintf(b2, sizeof(b2), "%#lx", (unsigned long)flags);
    if (flags) strlcat(buf, b2, sizeof(buf));
    return buf;
}

/** wrapper for Berkeley DB's DB->open() method which has changed API and
 * semantics -- this should deal with 3.2, 3.3, 4.0, 4.1 and 4.2. */
static int DB_OPEN(DB *db, const char *db_path,
	const char *db_file, DBTYPE type, u_int32_t flags, int mode)
{
    int ret;

#if DB_AT_LEAST(4,1)
    flags |= dsm->dsm_auto_commit_flags();
#endif

    ret = db->open(db,
#if DB_AT_LEAST(4,1)
	    0,	/* TXN handle - we use autocommit instead */
#endif
	    db_path, db_file, type, flags, mode);

    if (DEBUG_DATABASE(1) || getenv("BF_DEBUG_DB_OPEN"))
	fprintf(dbgout, "[pid %lu] DB->open(db=%p, file=%s, database=%s, "
		"type=%x, flags=%#lx=%s, mode=%#o) -> %d %s\n",
		(unsigned long)getpid(), (void *)db, db_path,
		db_file ? db_file : "NIL", type, (unsigned long)flags,
		resolveopenflags(flags), mode, ret, db_strerror(ret));

    return ret;
}

#if DB_AT_LEAST(4,1)
/** translate BerkeleyDB \a flags bitfield for DB->set_flags method back to symbols */
static const char *resolvesetflags(u_int32_t flags) {
    static char buf[160];
    char b2[80];
    strlcpy(buf, "", sizeof(buf));
#if DB_EQUAL(4,1)
    if (flags & DB_CHKSUM_SHA1) flags &= ~DB_CHKSUM_SHA1, strlcat(buf, "DB_CHKSUM_SHA1 ", sizeof(buf));
#endif
#if DB_AT_LEAST(4,2)
    if (flags & DB_CHKSUM) flags &= ~DB_CHKSUM, strlcat(buf, "DB_CHKSUM ", sizeof(buf));
#endif
    snprintf(b2, sizeof(b2), "%#lx", (unsigned long)flags);
    if (flags) strlcat(buf, b2, sizeof(buf));
    return buf;
}

/** Set flags and print debugging info */
static int DB_SET_FLAGS(DB *db, u_int32_t flags)
{
    int ret = db->set_flags(db, flags);

    if (DEBUG_DATABASE(1))
	fprintf(dbgout, "[pid %lu] DB->set_flags(db=%p, flags=%#lx=%s) -> %d %s\n",
		(unsigned long)getpid(), (void *)db, (unsigned long)flags,
		resolvesetflags(flags), ret, db_strerror(ret));

    return ret;
}
#endif

void *db_get_env(void *vhandle)
{
    dbh_t *handle = vhandle;

    assert(handle->magic == MAGIC_DBH);

    return handle->dbenv;
}

/* implements locking. */
int db_lock(int fd, int cmd, short int type)
{
    struct flock lock;

    lock.l_type = type;
    lock.l_start = 0;
    lock.l_whence = (short int)SEEK_SET;
    lock.l_len = 0;
    return (fcntl(fd, cmd, &lock));
}

static void dsm_init(void)
{
    if (!fTransaction)
	dsm = &dsm_traditional;
    else
	dsm = &dsm_transactional;
}

/** "constructor" - allocate our handle and initialize its contents */
static dbh_t *handle_init(const char *db_path, const char *db_name)
{
    dbh_t *handle;

    handle = xmalloc(sizeof(dbh_t));
    memset(handle, 0, sizeof(dbh_t));	/* valgrind */

    handle->dsm = dsm;
    handle->txn = NULL;

    handle->magic= MAGIC_DBH;		/* poor man's type checking */
    handle->fd   = -1;			/* for lock */

    handle->path = xstrdup(db_path);
    handle->name = build_path(db_path, db_name);

    handle->locked     = false;
    handle->is_swapped = false;
    handle->created    = false;

    handle->txn = NULL;

    return handle;
}

/** free \a handle and associated data.
 * NB: does not close transactions, data bases or the environment! */
static void handle_free(/*@only@*/ dbh_t *handle)
{
    if (handle != NULL) {
	xfree(handle->name);
	xfree(handle->path);
	xfree(handle);
    }
    return;
}

/* initialize data base, configure some lock table sizes
 * (which can be overridden in the DB_CONFIG file)
 * and lock the file to tell other parts we're initialized and
 * do not want recovery to stomp over us
 */
void *dbe_init(const char *directory)
{
    char norm_dir[PATH_MAX+1]; /* check normalized directory names */
    char norm_home[PATH_MAX+1];/* see man realpath(3) for details */

    dbe_t *env;

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

    assert(directory);

    dsm_init();

    env = dsm->dsm_env_init(directory);

    return env;
}

/* Returns is_swapped flag */
bool db_is_swapped(void *vhandle)
{
    dbh_t *handle = vhandle;

    assert(handle->magic == MAGIC_DBH);

    return handle->is_swapped;
}


/* Returns created flag */
bool db_created(void *vhandle)
{
    dbh_t *handle = vhandle;

    assert(handle->magic == MAGIC_DBH);

    return handle->created;
}


/* If header and library version do not match,
 * print an error message on stderr and exit with EX_ERROR. */
static void check_db_version(void)
{
    int maj, min;
    static bool version_ok = false;

#if DB_AT_MOST(3,0)
#error "Berkeley DB 3.0 is not supported"
#endif

    if (!version_ok) {
	version_ok = true;
	(void)db_version(&maj, &min, NULL);

	if (DEBUG_DATABASE(1))
	    fprintf(dbgout, "db_version: Header version %d.%d, library version %d.%d\n",
		    DB_VERSION_MAJOR, DB_VERSION_MINOR, maj, min);

	if (!(maj == DB_VERSION_MAJOR && min == DB_VERSION_MINOR)) {
	    fprintf(stderr, "The DB versions do not match.\n"
		    "This program was compiled for DB version %d.%d,\n"
		    "but it is linked against DB version %d.%d.\nAborting.\n",
		    DB_VERSION_MAJOR, DB_VERSION_MINOR, maj, min);
	    exit(EX_ERROR);
	}
    }
}

/** check limit of open file (given through descriptor \a fd) against
 * current resource limit and warn if file size is "close" (2 MB) to the
 * limit. errors from the system are ignored, no warning then.
 */
static void check_fsize_limit(int fd, uint32_t pagesize) {
#ifndef __EMX__
    struct stat st;
    struct rlimit rl;

    if (fstat(fd, &st)) return; /* ignore error */
    if (getrlimit(RLIMIT_FSIZE, &rl)) return; /* ignore error */
    if (rl.rlim_cur != (rlim_t)RLIM_INFINITY) {
	/* WARNING: Be extremely careful that in these comparisons there
	 * is no unsigned term, it will spoil everything as C will
	 * coerce into unsigned types, which would then make "file size
	 * larger than resource limit" undetectable. BUG: this doesn't
	 * work when pagesize doesn't fit into signed long. ("requires"
	 * 2**31 for file size and 32-bit integers to fail) */
	if ((off_t)(rl.rlim_cur/pagesize) - st.st_size/(long)pagesize < 16) {
	    print_error(__FILE__, __LINE__, "error: the data base file size is only 16 pages");
	    print_error(__FILE__, __LINE__, "       below the resource limit. Cowardly refusing");
	    print_error(__FILE__, __LINE__, "       to continue to avoid data base corruption.");
	    exit(EX_ERROR);
	}
	if ((off_t)(rl.rlim_cur >> 20) - (st.st_size >> 20) < 2) {
	    print_error(__FILE__, __LINE__, "warning: data base file size approaches resource limit.");
	    print_error(__FILE__, __LINE__, "         write errors (bumping into the limit) can cause");
	    print_error(__FILE__, __LINE__, "         data base corruption.");
	}
    }
#endif
}

/* The old, pre-3.3 API will not fill in the page size with
 * DB_CACHED_COUNTS, and without DB_CACHED_COUNTS, BerlekeyDB will read
 * the whole data base, incurring a severe performance penalty. We'll
 * guess a page size.  As this is a safety margin for the file size,
 * we'll return 0 and let the caller guess some size instead. */
#if DB_AT_LEAST(3,3)
/* return page size, of 0xffffffff for trouble */
static uint32_t get_psize(DB *dbp)
{
    uint32_t ret, pagesize;
    DB_BTREE_STAT *dbstat = NULL;

    ret = BF_DB_STAT(dbp, NULL, &dbstat, DB_FAST_STAT);
    if (ret) {
	print_error(__FILE__, __LINE__, "DB->stat");
	return 0xffffffff;
    }
    pagesize = dbstat->bt_pagesize;

    if (DEBUG_DATABASE(1))
	fprintf(dbgout, "DB->stat success, pagesize: %lu\n", (unsigned long)pagesize);

    free(dbstat);
    return pagesize;
}
#else
#define get_psize(discard) 0
#endif

const char *db_version_str(void)
{
    static char v[80];

#ifdef DB_VERSION_STRING
    strcpy(v, DB_VERSION_STRING);
#else
    snprintf(v, sizeof(v), "BerkeleyDB (%d.%d.%d)",
	     DB_VERSION_MAJOR, DB_VERSION_MINOR, DB_VERSION_PATCH);
#endif

    if (fTransaction)
	strcat(v, " TRANSACTIONAL");
    else
	strcat(v, " NON-TRANSACTIONAL");

    return v;
}

/** Initialize database. Expects open environment.
 * \return pointer to database handle on success, NULL otherwise.
 */
void *db_open(void *vhandle,
	      const char *path,
	      const char *name,
	      dbmode_t open_mode)
{
    int ret;
    int is_swapped;
    int retries = 2; /* how often do we retry to open after ENOENT+EEXIST
			races? 2 is sufficient unless the kernel or
			BerkeleyDB are buggy. */
    dbe_t *env = vhandle;

    dbh_t *handle = NULL;
    uint32_t opt_flags = (open_mode == DS_READ) ? DB_RDONLY : 0;

    size_t idx;
    uint32_t retryflags[] = { 0, DB_NOMMAP };

    /*
     * If locking fails with EAGAIN, then try without MMAP, fcntl()
     * locking may be forbidden on mmapped files, or mmap may not be
     * available for NFS. Thanks to Piotr Kucharski and Casper Dik,
     * see news:comp.protocols.nfs and the bogofilter mailing list,
     * message #1520, Message-ID: <20030206172016.GS1214@sgh.waw.pl>
     * Date: Thu, 6 Feb 2003 18:20:16 +0100
     */

    check_db_version();

    /* retry when locking failed */
    for (idx = 0; idx < COUNTOF(retryflags); idx += 1)
    {
	int e;
	DB *dbp;
	DB_ENV *dbe;
	bool err = false;
	const char *db_file;
	uint32_t pagesize;
	uint32_t retryflag = retryflags[idx];

	handle = handle_init(path, name);

	if (handle == NULL)
	    return NULL;

	/* create DB handle */
	dbe = dsm->dsm_get_env_dbe(env);
	if ((ret = db_create (&dbp, dbe, 0)) != 0) {
	    print_error(__FILE__, __LINE__, "(db) db_create, err: %s",
			db_strerror(ret));
	    goto open_err;
	}

	handle->dbp = dbp;
	handle->dbenv = env;

	handle->open_mode = open_mode;
	db_file = dsm->dsm_database_name(handle->name);

retry_db_open:
	handle->created = false;

	ret = DB_OPEN(dbp, db_file, NULL, dbtype, opt_flags | retryflag, DS_MODE);

	/* Begin complex change ... */
	if (ret != 0) {
	    err = (ret != ENOENT) || (opt_flags == DB_RDONLY);
	    if (!err) {
		if (
#if DB_EQUAL(4,1)
		 (ret = DB_SET_FLAGS(dbp, DB_CHKSUM_SHA1)) != 0 ||
#endif
#if DB_AT_LEAST(4,2)
		 (ret = DB_SET_FLAGS(dbp, DB_CHKSUM)) != 0 ||
#endif
		 (ret = DB_OPEN(dbp, db_file, NULL, dbtype, opt_flags | DB_CREATE | DB_EXCL | retryflag, DS_MODE)))
		    err = true;
		if (!err)
		    handle->created = true;
	    }
	}

	if (ret != 0) {
	    if (ret == ENOENT && opt_flags != DB_RDONLY)
		return NULL;
	    else
		err = true;
	}
	/* End complex change ... */

	if (err)
	{
	    if (open_mode != DB_RDONLY && ret == EEXIST && --retries) {
		/* sleep for 4 to 100 ms - this is just to give up the CPU
		 * to another process and let it create the data base
		 * file in peace */
		rand_sleep(4 * 1000, 100 * 1000);
		goto retry_db_open;
	    }

	    /* close again and bail out without further tries */
	    if (DEBUG_DATABASE(0))
		print_error(__FILE__, __LINE__, "DB->open(%s) - actually %s, directory %s, err %s",
			    handle->name, db_file, env->directory, db_strerror(ret));

	    dbp->close(dbp, 0);
	    goto open_err;
	}

	/* see if the database byte order differs from that of the cpu's */
#if DB_AT_LEAST(3,3)
	ret = dbp->get_byteswapped (dbp, &is_swapped);
#else
	ret = 0;
	is_swapped = dbp->get_byteswapped (dbp);
#endif
	handle->is_swapped = is_swapped ? true : false;

	if (ret != 0) {
	    print_error(__FILE__, __LINE__, "DB->get_byteswapped: %s",
		      db_strerror(ret));
	    db_close(handle);
	    return NULL;		/* handle already freed, ok to return */
	}

	if (DEBUG_DATABASE(1))
	    fprintf(dbgout, "DB->get_byteswapped: %s\n", is_swapped ? "true" : "false");

	ret = dbp->fd(dbp, &handle->fd);
	if (ret != 0) {
	    print_error(__FILE__, __LINE__, "DB->fd: %s",
		      db_strerror(ret));
	    db_close(handle);
	    return NULL;		/* handle already freed, ok to return */
	}

	if (DEBUG_DATABASE(1))
	    fprintf(dbgout, "DB->fd: %d\n", handle->fd);

	/* query page size */
	pagesize = get_psize(dbp);
	if (pagesize == 0xffffffff) {
	    dbp->close(dbp, 0);
	    goto open_err;
	}

	if (!pagesize)
	    pagesize = 16384;

	/* check file size limit */
	check_fsize_limit(handle->fd, pagesize);

	/* Begin complex change ... */
	e = dsm->dsm_lock(handle, open_mode);
	if (e == 0)
	    break;
	if (e != EAGAIN)
	    return NULL;
	handle = NULL;
	/* End complex change ... */

    } /* for idx over retryflags */

    return handle;

 open_err:
    handle_free(handle);

    if (ret >= 0)
	errno = ret;
    else
	errno = EINVAL;
    return NULL;
}

int db_delete(void *vhandle, const dbv_t *token)
{
    int ret = 0;
    dbh_t *handle = vhandle;
    DB *dbp = handle->dbp;

    DBT db_key;
    DBT_init(db_key);

    assert(handle->magic == MAGIC_DBH);
    assert((fTransaction == false) == (handle->txn == NULL));

    db_key.data = token->data;
    db_key.size = token->leng;

    ret = dbp->del(dbp, handle->txn, &db_key, 0);

    if (ret != 0 && ret != DB_NOTFOUND) {
	print_error(__FILE__, __LINE__, "DB->del('%.*s'), err: %s",
		    CLAMP_INT_MAX(db_key.size),
		    (const char *) db_key.data,
    		    db_strerror(ret));
	exit(EX_ERROR);
    }

    if (DEBUG_DATABASE(3))
	fprintf(dbgout, "DB->del(%.*s)\n", CLAMP_INT_MAX(db_key.size), (const char *) db_key.data);

    return ret;		/* 0 if ok */
}


int db_get_dbvalue(void *vhandle, const dbv_t *token, /*@out@*/ dbv_t *val)
{
    int ret = 0;
    int rmw_flag;
    DBT db_key;
    DBT db_data;

    dbh_t *handle = vhandle;
    DB *dbp = handle->dbp;

    assert(handle);
    assert(handle->magic == MAGIC_DBH);
    assert((fTransaction == false) == (handle->txn == NULL));

    DBT_init(db_key);
    DBT_init(db_data);

    db_key.data = token->data;
    db_key.size = token->leng;

    db_data.data = val->data;
    db_data.size = val->leng;		/* cur used */
    db_data.ulen = val->leng;		/* max size */
    db_data.flags = DB_DBT_USERMEM;	/* saves the memcpy */

    /* DB_RMW can avoid deadlocks */
    rmw_flag = dsm->dsm_get_rmw_flag(handle->open_mode);
    ret = dbp->get(dbp, handle->txn, &db_key, &db_data, rmw_flag );

    if (DEBUG_DATABASE(3))
	fprintf(dbgout, "DB->get(%.*s): %s\n",
		CLAMP_INT_MAX(token->leng), (char *) token->data, db_strerror(ret));

    val->leng = db_data.size;		/* read count */

    switch (ret) {
    case 0:
	break;
    case DB_NOTFOUND:
	ret = DS_NOTFOUND;
	break;
    case DB_LOCK_DEADLOCK:
	dsm->dsm_abort(handle);
	ret = DS_ABORT_RETRY;
	break;
    default:
	print_error(__FILE__, __LINE__, "(db) DB->get(TXN=%lu,  '%.*s' ), err: %s",
		    (unsigned long)handle->txn, CLAMP_INT_MAX(token->leng),
		    (char *) token->data, db_strerror(ret));
	dsm->dsm_abort(handle);
	exit(EX_ERROR);
    }

    return ret;
}


int db_set_dbvalue(void *vhandle, const dbv_t *token, const dbv_t *val)
{
    int ret;

    DBT db_key;
    DBT db_data;

    dbh_t *handle = vhandle;
    DB *dbp = handle->dbp;

    assert(handle->magic == MAGIC_DBH);
    assert((fTransaction == false) == (handle->txn == NULL));

    DBT_init(db_key);
    DBT_init(db_data);

    db_key.data = token->data;
    db_key.size = token->leng;

    db_data.data = val->data;
    db_data.size = val->leng;		/* write count */

    ret = dbp->put(dbp, handle->txn, &db_key, &db_data, 0);

    if (ret == DB_LOCK_DEADLOCK) {
	dsm->dsm_abort(handle);
	return DS_ABORT_RETRY;
    }

    if (ret != 0) {
	print_error(__FILE__, __LINE__, "db_set_dbvalue( '%.*s' ), err: %s",
		    CLAMP_INT_MAX(token->leng), (char *)token->data, db_strerror(ret));
	exit(EX_ERROR);
    }

    if (DEBUG_DATABASE(3))
	fprintf(dbgout, "DB->put(%.*s): %s\n",
		CLAMP_INT_MAX(token->leng), (char *) token->data, db_strerror(ret));

    return 0;
}

/* Close files and clean up. */
void db_close(void *vhandle)
{
    int ret;
    dbh_t *handle = vhandle;
    DB *dbp = handle->dbp;
    uint32_t f = DB_NOSYNC;	/* safe as long as we're logging TXNs */

    assert(handle->magic == MAGIC_DBH);

#if DB_AT_LEAST(4,2)
    {
	/* get_flags and DB_TXN_NOT_DURABLE are new in 4.2 */
	uint32_t t;
	ret = dbp->get_flags(dbp, &t);
	if (ret) {
	    print_error(__FILE__, __LINE__, "DB->get_flags returned error: %s",
		    db_strerror(ret));
	    f = 0;
	} else {
	    if (t & DB_TXN_NOT_DURABLE)
		f &= ~DB_NOSYNC;
	}
    }
#endif

#if DB_AT_LEAST(4,3)
    /* DB_LOG_INMEMORY is new in 4,3 */
    {
	uint32_t t;
	ret = dbp->get_flags(dbp, &t);
	if (ret) {
	    print_error(__FILE__, __LINE__, "DB_ENV->get_flags returned error: %s",
		    db_strerror(ret));
	    f = 0;
	} else {
	    if (t & DB_LOG_INMEMORY)
		f &= ~DB_NOSYNC;
	}
    }
#endif

    if (DEBUG_DATABASE(1))
	fprintf(dbgout, "DB->close(%s, %s)\n",
		handle->name, f & DB_NOSYNC ? "nosync" : "sync");

    if (handle->txn) {
	print_error(__FILE__, __LINE__, "db_close called with transaction still open, program fault!");
    }

    ret = dbp->close(dbp, f);
#if DB_AT_LEAST(3,2) && DB_AT_MOST(4,0)
    /* ignore dirty pages in buffer pool */
    if (ret == DB_INCOMPLETE)
	ret = 0;
#endif

    ret = dsm->dsm_sync(handle->dbenv->dbe, ret);

    if (ret)
	print_error(__FILE__, __LINE__, "DB->close error: %s",
		db_strerror(ret));

    handle->dbp = NULL;
    handle_free(handle);
}


/*
 flush any data in memory to disk
*/
void db_flush(void *vhandle)
{
    int ret;
    dbh_t *handle = vhandle;
    DB *dbp = handle->dbp;

    assert(handle->magic == MAGIC_DBH);

    if (DEBUG_DATABASE(1))
	fprintf(dbgout, "db_flush(%s)\n", handle->name);

    ret = dbp->sync(dbp, 0);
#if DB_AT_LEAST(3,2) && DB_AT_MOST(4,0)
    /* ignore dirty pages in buffer pool */
    if (ret == DB_INCOMPLETE)
	ret = 0;
#endif

    ret = dsm->dsm_sync(handle->dbenv->dbe, ret);

    if (DEBUG_DATABASE(1))
	fprintf(dbgout, "DB->sync(%p): %s\n", (void *)dbp, db_strerror(ret));

    if (ret)
	print_error(__FILE__, __LINE__, "db_sync: err: %s", db_strerror(ret));

    dsm->dsm_log_flush(handle->dbenv->dbe);
}

ex_t db_foreach(void *vhandle, db_foreach_t hook, void *userdata)
{
    dbh_t *handle = vhandle;
    DB *dbp = handle->dbp;

    ex_t ret = EX_OK;
    bool eflag = false;

    DBC dbc;
    DBC *dbcp = &dbc;
    DBT key, data;

    dbv_t dbv_key, dbv_data;

    assert(handle->magic == MAGIC_DBH);
    assert((fTransaction == false) == (handle->txn == NULL));

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));

    ret = dbp->cursor(dbp, handle->txn, &dbcp, 0);
    if (ret) {
	print_error(__FILE__, __LINE__, "(cursor): %s", handle->path);
	return EX_ERROR;
    }

    for (ret =  dbcp->c_get(dbcp, &key, &data, DB_FIRST);
	 ret == 0;
	 ret =  dbcp->c_get(dbcp, &key, &data, DB_NEXT))
    {
	int rc;

	/* Question: Is there a way to avoid using malloc/free? */

	/* switch to "dbv_t *" variables */
	dbv_key.leng = key.size;
	dbv_key.data = xmalloc(dbv_key.leng+1);
	memcpy(dbv_key.data, key.data, dbv_key.leng);
	((char *)dbv_key.data)[dbv_key.leng] = '\0';

	dbv_data.data = data.data;
	dbv_data.leng = data.size;

	/* call user function */
	rc = hook(&dbv_key, &dbv_data, userdata);
	xfree(dbv_key.data);

	/* returns 0 if ok, 1 if not */
	if (rc != 0)
	    break;
    }

    switch (ret) {
    case 0:
    case DB_NOTFOUND:
	/* OK */
	ret = EX_OK;
	break;
    default:
	print_error(__FILE__, __LINE__, "(c_get): %s", db_strerror(ret));
	eflag = true;
	break;
    }

    if ((ret = dbcp->c_close(dbcp))) {
	print_error(__FILE__, __LINE__, "(c_close): %s", db_strerror(ret));
	eflag = true;
    }

    return eflag ? EX_ERROR : ret;
}

const char *db_str_err(int e) {
    return db_strerror(e);
}

ex_t db_verify(const char *db_file)
{
    DB_ENV *dbe = NULL;
    DB *db;
    int e;

    if (!is_file(db_file)) {
	print_error(__FILE__, __LINE__, "\"%s\" is not a file.", db_file);
	return EX_ERROR;
    }

    dbe = dsm->dsm_recover_open(db_file, &db);
    if (dbe == NULL) {
	exit(EX_ERROR);
    }

    e = db->verify(db, db_file, NULL, NULL, 0);
    if (e) {
	print_error(__FILE__, __LINE__, "database %s does not verify: %s",
		db_file, db_strerror(e));
	exit(EX_ERROR);
    }

    e = dsm->dsm_common_close(dbe, db_file);

    if (e == 0 && verbose)
	printf("%s OK.\n", db_file);

    return e;
}
