/*****************************************************************************

NAME:
   datastore_db.c -- implements the datastore, using Berkeley DB 

AUTHOR:
   Gyepi Sam <gyepi@praxis-sw.com>

******************************************************************************/

#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <db.h>
#include <errno.h>

#include "xmalloc.h"
#include "datastore.h"
#include "datastore_db.h"

#define ERRSTR(str) "bogofilter (db) " str

#define DBT_init(dbt) do { memset(&dbt, 0, sizeof(DBT)); } while(0)

extern int errno;

static dbh_t *dbh_init(char *filename, char *name){
  dbh_t *handle;

  handle = xmalloc(sizeof(dbh_t));

  handle->filename = xmalloc(strlen(filename) + 1);
  strcpy(handle->filename,filename);

  handle->name = xmalloc(strlen(name)+1);
  strcpy(handle->name, name);

  handle->pid = getpid();

  return handle;
}

static void dbh_free(dbh_t *handle){
	if (handle == NULL) return;
	if (handle->filename != NULL) xfree(handle->filename);
        xfree(handle);
}

/*
  Initialize database.
  Returns: pointer database handle on success, NULL otherwise.
*/
void *db_open(char *db_file, char *name){
    int ret;
    dbh_t *handle;

    handle = dbh_init(db_file, name);

    if ((ret = db_create (&(handle->dbp), NULL, 0)) != 0){
	   fprintf (stderr, ERRSTR("db_create: %s\n"), db_strerror (ret));
    }
    else if ((ret = handle->dbp->open (handle->dbp, db_file, NULL, DB_BTREE, DB_CREATE, 0664)) != 0){
           handle->dbp->err (handle->dbp, ret, ERRSTR("open: %s"), db_file);
    }
    else {
      return (void *)handle;
    }

    dbh_free(handle);
    return NULL;
}

/*  
    Retrieve numeric value associated with word.
    Returns: value if the the word is found in database, 0 if the word is not found.
    Notes: Will call exit if an error occurs.
*/
long db_getvalue(void *vhandle, char *word){

    DBT db_key;
    DBT db_data;
    long value;
    int  ret;

    dbh_t *handle = vhandle;

    DBT_init(db_key);
    DBT_init(db_data);

    db_key.data = word;
    db_key.size = strlen(word);

    if ((ret = handle->dbp->get(handle->dbp, NULL, &db_key, &db_data, 0)) == 0){
      value = *(long *)db_data.data;

      if (verbose > 2){
        fprintf(stderr, "db_getvalue (%s): [%s] has value %ld\n",handle->name, word, value);
      }

      return(value);

    }
    else if (ret == DB_NOTFOUND){
	return 0;
    }
    else {
	handle->dbp->err (handle->dbp, ret, ERRSTR("db_getvalue: %s"), word);
	exit(2);
    }
}


/*
Store VALUE in database, using WORD as database key
Notes: Calls exit if an error occurs.
*/
void db_setvalue(void *vhandle, char * word, long value){
    int ret;
    DBT key;
    DBT data;
    dbh_t *handle = vhandle;
    
    DBT_init(key);
    DBT_init(data);

    key.data = word;
    key.size = strlen(word);

    data.data = &value;
    data.size = sizeof(long);

    if ((ret = handle->dbp->put(handle->dbp, NULL, &key, &data, 0)) == 0){
      if (verbose > 2){
            fprintf(stderr, "db_setvalue (%s): [%s] has value %ld\n", handle->name, word, value);
      }
    }
    else 
    {
	handle->dbp->err (handle->dbp, ret, ERRSTR("db_setvalue: %s"), word);
	exit(2);
    }
}


/*
  Increment count associated with WORD, by VALUE.
 */
void db_increment(void *vhandle, char *word, long value){

  value += db_getvalue(vhandle, word);

  if (value < 0)
    value = 0;

  db_setvalue(vhandle, word, value);
}


/*
  Decrement count associated with WORD by VALUE,
  if WORD exists in the database.
*/
void db_decrement(void *vhandle, char *word, long value){
  long n;

  n = db_getvalue(vhandle, word);

  if (n > value)
    n -= value;
  else
    n = 0;

  db_setvalue(vhandle, word, n);
}

/*
  Get the number of messages associated with database.
*/
long db_getcount(void *vhandle){
  return db_getvalue(vhandle, MSG_COUNT_TOK);
}

/*
 Set the number of messages associated with database.
*/
void db_setcount(void *vhandle, long count){
  db_setvalue(vhandle, MSG_COUNT_TOK, count);
}


/* Close files and clean up. */
void db_close(void *vhandle){  
  dbh_t *handle = vhandle;
  if (handle == NULL) return;
  handle->dbp->close(handle->dbp, 0);
  dbh_free(handle);
}

/*
 flush any data in memory to disk
*/
void db_flush(void *vhandle){
  dbh_t *handle = vhandle;
  handle->dbp->sync(handle->dbp, 0);
}


/* implements locking. */
static int db_lock(dbh_t *handle, int cmd, int type){
  int fd;
  int ret;
  struct flock lock;

  if ( (ret = handle->dbp->fd(handle->dbp, &fd)) != 0){
    handle->dbp->err (handle->dbp, ret, ERRSTR("db_lock:"));
    exit(2);
  }

  lock.l_type = type;
  lock.l_start = 0;
  lock.l_whence = SEEK_END;
  lock.l_len = 0;
  return (fcntl(fd, cmd, &lock));  
}

/*
Acquires blocking read lock on database.
*/
void db_lock_reader(void *vhandle){

  dbh_t *handle = vhandle;

  if (verbose > 1)
    fprintf(stderr, "[%lu] Acquiring read lock  on %s\n",(unsigned long)handle->pid, handle->filename);

  if (db_lock(handle, F_SETLKW, F_RDLCK) != 0){
    fprintf(stderr, "[%lu] Error acquiring read lock on %s\n",(unsigned long)handle->pid, handle->filename);
    exit(2);
  }

  if (verbose > 1)
    fprintf(stderr, "[%lu] Got read lock  on %s\n",(unsigned long)handle->pid, handle->filename);

}

/*
Acquires blocking write lock on database.
*/
void db_lock_writer(void *vhandle){
  dbh_t *handle = vhandle;
  if (verbose > 1)
    fprintf(stderr, "[%lu] Acquiring write lock on %s\n",(unsigned long)handle->pid, handle->filename);

  if (db_lock(handle, F_SETLKW, F_WRLCK) != 0){
    fprintf(stderr, "[%lu] Error acquiring write lock on %s\n",(unsigned long)handle->pid, handle->filename);
    exit(2);
  }

  if (verbose > 1)
    fprintf(stderr, "[%lu] Got write lock on %s\n",(unsigned long)handle->pid, handle->filename);

}

/*
Releases acquired lock
*/
void db_lock_release(void *vhandle){

  dbh_t *handle = vhandle;

  if (verbose > 1)
    fprintf(stderr, "[%lu] Releasing lock on %s\n",(unsigned long)handle->pid, handle->filename);

  if (db_lock(handle, F_SETLK, F_UNLCK) != 0){
    fprintf(stderr, "[%lu] Error releasing on %s\n", (unsigned long)handle->pid, handle->filename);
    exit(2);
  }
}


/*
Releases acquired locks on multiple databases
*/
void db_lock_release_list(void *vhandle_list[], int n){
  while (--n >= 0)
    db_lock_release(vhandle_list[n]);
}

static void lock_msg(dbh_t *handle, int index, char *msg, int cmd, int type)
{

  char *block_type[] = { "nonblocking", "blocking" };
  char *lock_type[]  = { "write", "read" };
                                                     
  fprintf(stderr, "[%lu] [%d] %s %s %s lock on %s\n", 
	          (unsigned long)handle->pid,
                  index,
		  msg,
		  block_type[cmd == F_SETLKW],
		  lock_type[type == F_RDLCK],
		  handle->filename);
}

/*
Acquires locks on multiple database.

Uses the following protocol  to avoid deadlock:

1. The first database in the list is locked in blocking mode,
   all subsequent databases are locked in non-blocking mode.

2. If a lock attempt on a database fails, all locks on existing databases are relinquished,
   and the whole operation restarted. In order to avoid syncronicity among contending lockers,
   each locker sleeps for some small, random time period before restarting.

*/
static void db_lock_list(void *vhandle_list[], int n, int type){
  int i;
  int cmd;

  dbh_t *handle;

#define do_lock_msg(msg) lock_msg(handle, i, msg, cmd, type)
 
  for(;;){
    for(i = 0, cmd = F_SETLKW ; i < n; i++, cmd = F_SETLK){

      handle = vhandle_list[i];
      
      if (verbose > 1)
	      do_lock_msg("Trying");
      
      if (db_lock(handle, cmd, type) != 0){
        if (errno == EACCES || errno == EAGAIN || errno == EINTR){
          if (verbose)
            do_lock_msg("Failed to acquire");

          break;
        }
        else {
          do_lock_msg("Error acquiring");
	  exit(2);
        }
      }
      else if (verbose) {
        do_lock_msg("Got");
      }
    }

    /* All locks acquired. */
    if (i == n)
      break;

    /* Some locks failed. Release all acquired locks */
    if (i > 0)
      db_lock_release_list(vhandle_list, i);

    //sleep for short, random time between 1 microsecond and 1 second
    usleep( 1 + (unsigned long) (1000.0*rand()/(RAND_MAX+1.0)));
  }
}

/*
Acquires read locks on multiple databases.
*/
void db_lock_reader_list(void *vhandle_list[], int n){
  db_lock_list(vhandle_list, n, F_RDLCK);
}

/*
Acquires write locks on multiple database.
*/
void db_lock_writer_list(void *vhandle_list[], int n){
  db_lock_list(vhandle_list, n, F_WRLCK);
}
