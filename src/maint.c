/* $Id$ */

/*****************************************************************************

NAME:
   maint.c -- wordlist maintenance functions

AUTHOR:
   David Relson

******************************************************************************/

#include "common.h"

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

#include "datastore.h"
#include "error.h"
#include "maint.h"
#include "wordlists.h"
#include "xmalloc.h"
#include "xstrdup.h"

YYYYMMDD today;			/* date as YYYYMMDD */
uint32_t thresh_count = 0;
YYYYMMDD thresh_date  = 0;
size_t	 size_min = 0;
size_t	 size_max = 0;
bool     datestamp_tokens = true;

/* Function Prototypes */
static YYYYMMDD time_to_date(long days);

static int maintain_wordlist(void *vhandle);

/* Function Definitions */

static
YYYYMMDD time_to_date(long days)
{
    time_t t = time(NULL) - days * 86400;
    struct tm *tm = localtime( &t );
    YYYYMMDD date = ((((tm->tm_year + (YYYYMMDD)1900) * 100 + tm->tm_mon + 1) * 100) + tm->tm_mday);
    return date;
}

void set_date(YYYYMMDD date)
{
    today = date;
}

void set_today(void)
{
    today = time_to_date(0);
}

YYYYMMDD string_to_date(const char *s)
{
    YYYYMMDD date = atol(s);
    if (date < 20020801 && date != 0) {
	date = time_to_date(date);
    }
    return date;
}

/* Keep high counts */
bool keep_count(uint32_t count)
{
    bool ok = count > thresh_count;
    if (count > 0 && DEBUG_DATABASE(1))
	fprintf(dbgout, "keep_count:  %lu > %lu -> %c\n",
		(unsigned long)count, (unsigned long)thresh_count,
		ok ? 't' : 'f' );
    return ok;
}

/* Keep recent dates */
bool keep_date(YYYYMMDD date)
{
    if (thresh_date == 0 || date == 0 || date == today)
	return true;
    else {
	bool ok = thresh_date < date;
	if (DEBUG_DATABASE(1))
	    fprintf(dbgout, "keep_date: %ld < %ld -> %c\n",
		    (long)thresh_date, (long)date, ok ? 't' : 'f' );
	return ok;
    }
}

/* Keep sizes within bounds */
bool keep_size(size_t size)
{
    if (size_min == 0 || size_max == 0)
	return true;
    else {
	bool ok = (size_min <= size) && (size <= size_max);
	if (DEBUG_DATABASE(1))
	    fprintf(dbgout, "keep_size:  %lu <= %lu <= %lu -> %c\n", 
		    (unsigned long)size_min, (unsigned long)size, (unsigned long)size_max, 
		    ok ? 't' : 'f' );
	return ok;
    }
}

bool do_replace_nonascii_characters(register byte *str, register size_t len)
{
    bool change = false;
    assert(str != NULL);
    while(len--) {
	if (*str & 0x80) {
	    *str = '?';
	    change = true;
	}
	str++;
    }
    return change;
}

void maintain_wordlists(void)
{
    wordlist_t *list;

    set_list_active_status(true);

    for (list = word_lists; list != NULL; list = list->next) {
	maintain_wordlist(list->dsh);
	list = list->next;
    }
}

int maintain_wordlist_file(const char *db_file)
{
    int rc;
    dsh_t *dsh = ds_open(".", 1, &db_file, DB_WRITE);

    if (dsh == NULL)
	return EX_ERROR;

    rc = maintain_wordlist(dsh);

    ds_close(dsh, false);

    return rc;
}


static int maintain_hook(word_t *w_key, dsv_t *in_val,
			 /*@unused@*/ void *userdata)
{
    size_t len;
    word_t token;
    void *vhandle = userdata;

    token.text = w_key->text;
    token.leng = w_key->leng;

    len = strlen(MSG_COUNT);
    if (len == token.leng && 
	strncmp((char *)token.text, MSG_COUNT, token.leng) == 0)
	return 0;

    if ((!keep_count(in_val->spamcount) && !keep_count(in_val->goodcount)) || 
	!keep_date(in_val->date) || !keep_size(token.leng)) {
	int ret = ds_delete(vhandle, &token);
	if (DEBUG_DATABASE(0))
	    fprintf(dbgout, "deleting '%*s'\n", (int)min(INT_MAX, token.leng), (char *)token.text);
	return ret;
    }

    if (replace_nonascii_characters)
    {
	byte *key_tmp = (byte *)xmalloc(token.leng + 1);
	memcpy(key_tmp, token.text, token.leng);
	key_tmp[token.leng] = '\0';
	if (do_replace_nonascii_characters(key_tmp, token.leng))
	{
	    int	  ret;
	    dsv_t old_tmp;

	    /* delete original token */
	    ds_delete(vhandle, &token);	

	    /* retrieve and update nonascii token*/
	    token.text = key_tmp;
	    ret = ds_read(vhandle, &token, &old_tmp);

	    if (ret == 0) {
		in_val->spamcount += old_tmp.spamcount;
		in_val->goodcount += old_tmp.goodcount;
		in_val->date       = max(old_tmp.date, in_val->date);	/* date in form YYYYMMDD */
	    }
	    set_date(in_val->date);	/* set timestamp */
	    ds_write(vhandle, &token, in_val);
	}
	xfree(key_tmp);
    }
    return 0;
}

int maintain_wordlist(void *vhandle)
{
    return ds_foreach(vhandle, maintain_hook, vhandle);
}
