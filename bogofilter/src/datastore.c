/* $Id$ */

/*****************************************************************************

NAME:
datastore.c -- contains database independent components of data storage.

AUTHORS:
Gyepi Sam <gyepi@praxis-sw.com>   2002 - 2003
Matthias Andree <matthias.andree@gmx.de> 2003
David Relson <relson@osagesoftware.com>  2003

******************************************************************************/

#include "common.h"

#include "datastore.h"
#include "xmalloc.h"
#include "maint.h"

#include "common.h"

#include <db.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "datastore.h"
#if	 ! NEED_TDB
#include "datastore_db.h"
#else
#include "datastore_tdb.h"
#endif
#include "error.h"
#include "maint.h"
#include "swap.h"
#include "word.h"
#include "xmalloc.h"
#include "xstrdup.h"

#define struct_init(s) memset(&s, 0, sizeof(s))

bool is_swapped = false;

/* Function prototypes */

void ds_init(void);
void ds_cleanup(void);

/* Function definitions */

static void convert_external_to_internal(dbv_t *ex_data, dsv_t *in_data)
{
    size_t i = 0;
    uint32_t *cv = ex_data->data;

    in_data->spamcount = !is_swapped ? cv[i++] : swap_32bit(cv[i++]);

    if (ex_data->leng <= i * sizeof(uint32_t))
	in_data->goodcount = 0;
    else
	in_data->goodcount = !is_swapped ? cv[i++] : swap_32bit(cv[i++]);

    if (ex_data->leng <= i * sizeof(uint32_t))
	in_data->date = 0;
    else {
	in_data->date = !is_swapped ? cv[i++] : swap_32bit(cv[i++]);
    }

    return;
}

static void convert_internal_to_external(dsv_t *in_data, dbv_t *ex_data)
{
    size_t i = 0;
    uint32_t *cv = ex_data->data;

    cv[i++] = !is_swapped ? in_data->spamcount : swap_32bit(in_data->spamcount);
    cv[i++] = !is_swapped ? in_data->goodcount : swap_32bit(in_data->goodcount);
    if (datestamp_tokens || in_data->date != 0)
	cv[i++] = !is_swapped ? in_data->date : swap_32bit(in_data->date);

    ex_data->leng = i * sizeof(cv[0]);

    return;
}


void *ds_open(const char *db_file, size_t count, const char **names, dbmode_t open_mode)
{
    void *v = db_open(db_file, count, names, open_mode);
    return v;
}

void ds_close(/*@only@*/ void *vhandle, bool nosync  /** Normally false, if true, do not synchronize data. This should not be used in regular operation but only to ease the disk I/O load when the lock operation failed. */)
{
    db_close(vhandle, nosync);
}

void ds_flush(void *vhandle)
{
    db_flush(vhandle);
}

int ds_read(void *vhandle, const word_t *word, /*@out@*/ dsv_t *val)
{
    int ret;
    dbv_t ex_key;
    dbv_t ex_data;
    uint32_t cv[3];

    is_swapped = db_is_swapped(vhandle);

    struct_init(ex_key);
    struct_init(ex_data);

    ex_key.data = word->text;
    ex_key.leng = ex_key.size = word->leng;

    ex_data.data = cv;
    ex_data.leng = ex_data.size = sizeof(cv);

    ret = db_get_dbvalue(vhandle, &ex_key, &ex_data);

    if (ret == 0) {
	convert_external_to_internal(&ex_data, val);

	if (DEBUG_DATABASE(3)) {
	    fprintf(dbgout, "ds_read: [%*s] -- %lu,%lu\n",
		    word->leng, word->text,
		    (unsigned long)val->spamcount,
		    (unsigned long)val->goodcount);
	}
    } else {
	memset(val, 0, sizeof(*val));
    }

    return ret;
}


int ds_write(void *vhandle, const word_t *word, dsv_t *val)
{
    bool ok;
    dbv_t ex_key;
    dbv_t ex_data;
    uint32_t cv[3];

    is_swapped = db_is_swapped(vhandle);

    struct_init(ex_key);
    struct_init(ex_data);

    ex_key.data = word->text;
    ex_key.leng = word->leng;
    ex_key.size = word->leng;

    ex_data.data = cv;
    ex_data.size = sizeof(cv);

    if (datestamp_tokens || today != 0)
	val->date = today;

    convert_internal_to_external(val, &ex_data);

    ok = db_set_dbvalue(vhandle, &ex_key, &ex_data);

    if (ok) {
	if (DEBUG_DATABASE(3)) {
	    fprintf(dbgout, "ds_write: [%*s] -- %lu,%lu\n",
		    word->leng, word->text,
		    (unsigned long)val->spamcount,
		    (unsigned long)val->goodcount);
	}
    }

    return ok ? 0 : 1;
}


int ds_delete(void *vhandle, const word_t *word)
{
    bool ok;
    dbv_t ex_key;

    struct_init(ex_key);
    ex_key.data = word->text;
    ex_key.leng = word->leng;

    ok = db_delete(vhandle, &ex_key);

    return ok ? 0 : 1;
}


typedef struct {
    ds_foreach_t *hook;
    void         *data;
} ds_userdata_t;

static int ds_hook(dbv_t *ex_key,
		   dbv_t *ex_data,
		   /*@unused@*/ void *userdata)
{
    int val;
    word_t w_key;
    dsv_t in_data;
    ds_userdata_t *ds_data = userdata;
    w_key.text = ex_key->data;
    w_key.leng = ex_key->leng;
    convert_external_to_internal(ex_data, &in_data);
    val = (*ds_data->hook)(&w_key, &in_data, ds_data->data);
    return val;
}


int ds_foreach(void *vhandle, ds_foreach_t *hook, void *userdata)
{
    int val;
    ds_userdata_t ds_data;
    ds_data.hook = hook;
    ds_data.data = userdata;

    val = db_foreach(vhandle, ds_hook, &ds_data);

    return val;
}

word_t  *msg_count_tok;

void ds_init()
{
    if (msg_count_tok == NULL) {
	msg_count_tok = word_new(MSG_COUNT_TOK, strlen((const char *)MSG_COUNT_TOK));
    }
}


/* Cleanup storage allocation */
void ds_cleanup()
{
    xfree(msg_count_tok);
    msg_count_tok = NULL;
}


/*
  Get the number of messages associated with database.
*/
void ds_get_msgcounts(void *vhandle, dsv_t *val)
{
    ds_init();
    ds_read(vhandle, msg_count_tok, val);
    return;
}


/*
 Set the number of messages associated with database.
*/
void ds_set_msgcounts(void *vhandle, dsv_t *val)
{
    if (val->date == 0 && datestamp_tokens)
	val->date = today;
    ds_write(vhandle, msg_count_tok, val);
    return;
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

const char *ds_version_str(void)
{
    return db_version_str();
}


