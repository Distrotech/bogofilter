/* $Id$ */

/*****************************************************************************

NAME:
datastore_tdb.c -- implements the datastore, using  tdb.

AUTHORS:
Gyepi Sam <gyepi@praxis-sw.com>   2003

******************************************************************************/

#include "system.h"
#include <tdb.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <config.h>
#include "common.h"

#include "datastore.h"
#include "maint.h"
#include "error.h"
#include "word.h"
#include "xmalloc.h"
#include "xstrdup.h"


typedef struct {
  char *filename;
  char *name;
  dbmode_t open_mode;
  TDB_CONTEXT  *dbp;
  pid_t pid;
} dbh_t;


/* This must come after dbh_t is defined ! */
#include "db_handle_props.h"

static dbh_t *dbh_init(const char *filename, const char *name){
  dbh_t *handle;

  handle = xmalloc(sizeof(dbh_t));
  memset(handle, 0, sizeof(dbh_t));	/* valgrind */

  handle->filename  = xstrdup(filename);
  handle->name	    = xstrdup(name);
  handle->pid	    = getpid();
  return handle;
}

static void dbh_free(/*@only@*/ dbh_t *handle){
    if (handle == NULL) return;
    xfree(handle->filename);
    xfree(handle->name);
    xfree(handle);
}


/*
  Initialize database.
  Returns: pointer to database handle on success, NULL otherwise.
*/
void *db_open(const char *db_file, const char *name, dbmode_t open_mode)
{
  dbh_t *handle = NULL;
  int tdb_flags;
  int open_flags;

  if (open_mode == DB_READ){
    tdb_flags = TDB_NOLOCK; /* Don't do any locking */
    open_flags = O_RDONLY;
  }
  else {
    tdb_flags = 0;          /* Do normal locking */ 
    open_flags = O_RDWR | O_CREAT;
  }

  handle = dbh_init(db_file, name);
  handle->open_mode = open_mode;


  handle->dbp = tdb_open(db_file, 0, tdb_flags, open_flags, 0664);

  if (handle->dbp == NULL){
    print_error(__FILE__, __LINE__, "(db) tdb_open( %s ) failed.", db_file);
    dbh_free(handle);
    handle = NULL;
  }
  else {

    if (DEBUG_DATABASE(1)) {
      fprintf(dbgout, "tdb_open( %s, %s, %d )\n", name, db_file, open_mode);
    }
    
    if (open_mode == DB_WRITE){
      if (tdb_lockall(handle->dbp) != 0){
        print_error(__FILE__, __LINE__, "(db) tdb_lockall failed on file (%s).", db_file);
        dbh_free(handle);
        handle = NULL;        
      }
    }
  }
  
  return handle;
}

void db_delete(void *vhandle, const word_t *word) {
    int ret;
    dbh_t *handle = vhandle;
    TDB_DATA db_key;

    db_key.dptr = word->text;
    db_key.dsize = word->leng;

    ret = tdb_delete(handle->dbp, db_key);

    if (ret == 0) return;
    print_error(__FILE__, __LINE__, "(db) tdb_delete('%s'), err: %d, %s", word->text, ret, tdb_errorstr(handle->dbp));
    exit(2);
}

int db_get_dbvalue(void *vhandle, const word_t *word, /*@out@*/ dbv_t *val){

  int ret = -1;
  TDB_DATA db_key;
  TDB_DATA db_val;
  uint32_t cv[2] = { 0l, 0l };

  dbh_t *handle = vhandle;

  db_key.dptr = word->text;
  db_key.dsize = word->leng;

  db_val = tdb_fetch(handle->dbp, db_key);

  if (db_val.dptr != NULL){
    
    memcpy(cv, db_val.dptr, db_val.dsize);
    val->count = cv[0];
    val->date  = cv[1];

    free(db_val.dptr);
    ret = 0;
  }
  else if (DEBUG_DATABASE(3)) {
      fprintf(dbgout, "[%lu] db_getvalue (%s): [", (unsigned long) handle->pid, handle->name);
      word_puts(word, 0, dbgout);
      fputs("] not found\n", dbgout);
  }

  return ret;
}


void db_set_dbvalue(void *vhandle, const word_t *word, dbv_t *val){
  int ret;
  TDB_DATA db_key;
  TDB_DATA db_data;
  uint32_t cv[2];
  dbh_t *handle = vhandle;


  db_key.dptr = word->text;
  db_key.dsize = word->leng;


  cv[0] = val->count;
  cv[1] = val->date;

  db_data.dptr = (char *)&cv;			/* and save array in wordlist */

  if (!datestamp_tokens || val->date == 0)
      db_data.dsize = sizeof(cv[0]);
  else
      db_data.dsize = sizeof(cv);

  ret = tdb_store(handle->dbp, db_key, db_data, TDB_REPLACE);

  if (ret == 0){
    if (DEBUG_DATABASE(3)) {
      fprintf(dbgout, "db_set_dbvalue (%s): [",
	      handle->name);
      word_puts(word, 0, dbgout);
      fprintf(dbgout, "] has value %lu\n",
	      (unsigned long)val->count);
    }
  }
  else {
    print_error(__FILE__, __LINE__, "(db) db_set_dbvalue( '%s' ), err: %d, %s", word->text, ret, tdb_errorstr(handle->dbp));
    exit(2);
  }
}


/* Close files and clean up. */
void db_close(void *vhandle, bool nosync){
  dbh_t *handle = vhandle;
  int ret;

  if (DEBUG_DATABASE(1)) {
      fprintf(dbgout, "db_close(%s, %s, %s)\n", handle->name, handle->filename,
	      nosync ? "nosync" : "sync");
  }

  if (handle == NULL) return;
  
  if (handle->open_mode == DB_WRITE){
    tdb_unlockall(handle->dbp);
  }

  if ((ret = tdb_close(handle->dbp))) {
    print_error(__FILE__, __LINE__, "(db) db_close err: %d, %s", ret, tdb_errorstr(handle->dbp));
  }

  dbh_free(handle);
}

/*
 flush any data in memory to disk
*/
void db_flush( /*@unused@*/ __attribute__ ((unused)) void *vhandle){
  /* noop */
}

typedef struct userdata_t {
  db_foreach_t hook;
  void *userdata;
} userdata_t;


static int tdb_traversor(/*@unused@*/ __attribute__ ((unused)) TDB_CONTEXT *tdb_handle, TDB_DATA key, TDB_DATA data, void *userdata)
{
  uint32_t cv[2];
  word_t w_key, w_data;
  userdata_t *hookdata = userdata;

  memcpy(&cv, data.dptr, data.dsize);

  /* switch to "word_t *" variables */
  w_key.text = key.dptr;
  w_key.leng = key.dsize;
  w_data.text = data.dptr;
  w_data.leng = data.dsize;

  /* call user function */
  return hookdata->hook(&w_key, &w_data, hookdata->userdata);
}


int db_foreach(void *vhandle, db_foreach_t hook, void *userdata) {
  dbh_t *handle = vhandle;
  int ret;
  userdata_t hookdata;
  
  hookdata.hook = hook;
  hookdata.userdata = userdata;

  ret = tdb_traverse(handle->dbp, tdb_traversor, (void *)&hookdata);
  if (ret == -1){
    print_error(__FILE__, __LINE__, "(db) db_foreach err: %d, %s", ret, tdb_errorstr(handle->dbp));
  }
  else {
    /* tdb_traverse returns a count of records. We just want success or failure */ 
    ret = 0;
  }

  return ret;
}
