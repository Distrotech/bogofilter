/* Lock handler to detect application crashes
 * (C) 2004 Matthias Andree, GNU GPL v2
 * with optimization ideas by Pavel Kankovsky */

/* Lock file layout:
 * the lock file has a list of cells, which can be either 0 or 1.
 * 0 means: slot is free
 * 1 means: slot in use
 *   1 with fcntl lock: process running
 *   1 without lock: process quit prematurely, recovery needed
 * see http://article.gmane.org/gmane.mail.bogofilter.devel/3240
 *     http://article.gmane.org/gmane.mail.bogofilter.devel/3260
 *     http://article.gmane.org/gmane.mail.bogofilter.devel/3270
 */

#include "debug.h"
#include "globals.h"
#include "mxcat.h"
#include "system.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

static const char aprt[] = DIRSEP_S "process-table";
static const off_t cellsize = 1;
static int lockfd = -1; /* lock file descriptor */

static const char cell_inuse = '1';
static const char cell_free = '0';

/* the lock file descriptor must be long-lived because fcntl() or
 * lockf() will immediately release all locks held on a file once we
 * call close() for the file for the first time no matter how many file
 * descriptors to the file we still hold, so we cannot use
 * open-check-close cycles but need to keep the
 * descriptor open until will let go of the locks. */

typedef char cell_t;

/* 1 if locked, 0 if unlocked, negative if error */
static int check_celllock(int fd, off_t offset) {
    struct flock fl;

    fl.l_type = F_RDLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = cellsize;
    if (fcntl(fd, F_GETLK, &fl)) return -1;
    return fl.l_type == F_UNLCK ? 0 : 1;
}

/* fast lock function, uses F_SETLK so does not wait */
static int set_celllock(int fd, off_t offset, int locktype) {
    struct flock fl;

    fl.l_type = locktype;
    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = cellsize;
    return fcntl(fd, F_SETLK, &fl);
}

/* returns 0 for success, -1 for error
 * this will do nothing and flag success if the file is open already */
static int open_lockfile(const char *bogohome) {
    char *fn;

    if (lockfd >= 0) return 0;
    fn = mxcat(bogohome, aprt, NULL);

    lockfd = open(fn, O_RDWR|O_SYNC);
    if (lockfd < 0) {
	int e = errno;
	if (DEBUG_DATABASE(1))
	    fprintf(dbgout, "open_lockfile: open(%s): %s\n", fn, strerror(e));
    }

    free(fn);
    return (lockfd < 0) ? -1 : 0;
}

static void close_lockfile(void) {
    if (lockfd >= 0) {
	close(lockfd);
	lockfd = -1;
    }
}

void lock_cleanup(void) {
    close_lockfile();
}

/*  0 - no zombies
 *  1 - zombies
 *  2 - file not found
 * -1 - other error */
int check_zombies(const char *bogohome) {
    ssize_t r;
    off_t pos;
    cell_t cell;

    if (open_lockfile(bogohome)) {
	return errno == ENOENT ? 2 : -1;
    }

    for (;;) {
	pos = lseek(lockfd, 0, SEEK_CUR);
	r = read(lockfd, &cell, sizeof(cell));
	if (r != sizeof(cell)) break;
	if (cell == '1' && 1 != check_celllock(lockfd, pos)) {
	    return 1;
	}
    }

    return r == 0 ? 0 : -1;
}

/*
 * lock a cell and returns its offset, or
 * -2 if zombie was found or
 * -1 in case of trouble
 */
off_t set_lock(const char *bogohome) {
    cell_t cell;
    off_t pos;
    ssize_t r;

    if (open_lockfile(bogohome)) {
	return errno == ENOENT ? (off_t)-2 : (off_t)-1;
    }

    for (;;) {
	pos = lseek(lockfd, 0, SEEK_CUR);
	r = read(lockfd, &cell, sizeof(cell));
	if (r != sizeof(cell))
	    return (off_t)-1; /* XXX FIXME: retry? */
	if (cell == cell_free && 0 == set_celllock(lockfd, pos, F_WRLCK)) {
	    if (lseek(lockfd, pos, SEEK_SET) >= 0) {
		r = read(lockfd, &cell, sizeof(cell));
		if (r != sizeof(cell) || cell != cell_free) {
		    /* found fresh zombie */
		    set_celllock(lockfd, pos, F_UNLCK);
		    return (off_t)-2;
		}
		if (cellsize != write(lockfd, &cell_inuse, cellsize))
		    return (off_t)-1;
		if (fdatasync(lockfd))
		    return (off_t)-1;
		return pos;
	    }
	}
    }
}

/* returns 0 for success */
int clear_lock(off_t pos) {
    if (lseek(lockfd, pos, SEEK_SET) < 0)
	return -1;
    if (cellsize != write(lockfd, &cell_free, cellsize))
	return -1;
    return set_celllock(lockfd, pos, F_UNLCK);
}
