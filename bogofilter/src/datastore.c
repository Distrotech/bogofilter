/*
$Id$
NAME:
datastore.c -- contains database independent components of data storage.

AUTHORS:
Gyepi Sam <gyepi@praxis-sw.com>   2002 - 2003
Matthias Andree <matthias.andree@gmx.de> 2003

*/

#include "datastore.h"
#include "xmalloc.h"
#include  "maint.h"

word_t  *msg_count_tok;
int db_cachesize = 0;	/* in MB */

/* Cleanup storage allocation */
void db_cleanup()
{
    xfree(msg_count_tok);
    msg_count_tok = NULL;
}

/*
    Retrieve numeric value associated with word.
    Returns: value if the the word is found in database,
    0 if the word is not found.
    Notes: Will call exit if an error occurs.
*/
uint32_t db_getvalue(void *vhandle, const word_t *word){
  dbv_t val;
  int ret;
  uint32_t value = 0;

  ret = db_get_dbvalue(vhandle, word, &val);

  if (ret == 0) {
    value = val.count;

    if (DEBUG_DATABASE(3)) {
      fprintf(dbgout, "[%lu] db_getvalue (%s): [",
	              db_handle_pid(vhandle), db_handle_name(vhandle));
      word_puts(word, 0, dbgout);
      fprintf(dbgout, "] has value %lu\n",
	      (unsigned long)value);
    }

    if ((int32_t)value < (int32_t)0)
      value = 0;

    return value;
  } else {
    return 0;
  }
}

/*
Store VALUE in database, using WORD as database key
Notes: Calls exit if an error occurs.
*/
void db_setvalue(void *vhandle, const word_t *word, uint32_t count){
  dbv_t val;
  val.count = count;
  val.date  = today;		/* date in form YYYYMMDD */
  db_set_dbvalue(vhandle, word, &val);
}


/*
Update the VALUE in database, using WORD as database key.
Adds COUNT to existing count.
Sets date to newer of TODAY and date in database.
*/
void db_updvalue(void *vhandle, const word_t *word, const dbv_t *updval){
  dbv_t val;
  int ret = db_get_dbvalue(vhandle, word, &val);
  if (ret != 0) {
    val.count = updval->count;
    val.date  = updval->date;		/* date in form YYYYMMDD */
  }
  else {
    val.count += updval->count;
    val.date  = max(val.date, updval->date);	/* date in form YYYYMMDD */
  }
  db_set_dbvalue(vhandle, word, &val);
}



/*
  Increment count associated with WORD, by VALUE.
 */
void db_increment(void *vhandle, const word_t *word, uint32_t value){
  uint32_t dv = db_getvalue(vhandle, word);
  value = UINT32_MAX - dv < value ? UINT32_MAX : dv + value;
  db_setvalue(vhandle, word, value);
}

/*
  Decrement count associated with WORD by VALUE,
  if WORD exists in the database.
*/
void db_decrement(void *vhandle, const word_t *word, uint32_t value){
  uint32_t dv = db_getvalue(vhandle, word);
  value = dv < value ? 0 : dv - value;
  db_setvalue(vhandle, word, value);
}

/*
  Get the number of messages associated with database.
*/
uint32_t db_get_msgcount(void *vhandle){
  uint32_t msg_count;

  if (msg_count_tok == NULL)
    msg_count_tok = word_new(MSG_COUNT_TOK, strlen((const char *)MSG_COUNT_TOK));

  msg_count = db_getvalue(vhandle, msg_count_tok);

  if (DEBUG_DATABASE(2)) {
    fprintf(dbgout, "db_get_msgcount( %s ) -> %lu\n", db_handle_name(vhandle), (unsigned long)msg_count);
  }

  return msg_count;
}

/*
 Set the number of messages associated with database.
*/
void db_set_msgcount(void *vhandle, uint32_t count){
  db_setvalue(vhandle, msg_count_tok, count);
  if (DEBUG_DATABASE(2)) {
    fprintf(dbgout, "db_set_msgcount( %s ) -> %lu\n", db_handle_name(vhandle),
            (unsigned long)count);
  }
}

/* implements locking. */
int db_lock(int fd, int cmd, short int type){
    struct flock lock;

    lock.l_type = type;
    lock.l_start = 0;
    lock.l_whence = (short int)SEEK_SET;
    lock.l_len = 0;
    return (fcntl(fd, cmd, &lock));
}

