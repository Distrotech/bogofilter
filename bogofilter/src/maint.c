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

#include "datastore.h"
#include "error.h"
#include "maint.h"
#include "transaction.h"
#include "wordlists.h"
#include "xmalloc.h"
#include "xstrdup.h"

uint32_t thresh_count = 0;
YYYYMMDD thresh_date  = 0;
size_t	 size_min = 0;
size_t	 size_max = 0;
bool     timestamp_tokens = true;

/* Function Prototypes */

/* Function Definitions */

/* Keep high counts */
static bool keep_count(uint32_t count)
{
    bool ok = count > thresh_count;
    if (count > 0 && DEBUG_DATABASE(1))
	fprintf(dbgout, "keep_count:  %lu > %lu -> %c\n",
		(unsigned long)count, (unsigned long)thresh_count,
		ok ? 't' : 'f' );
    return ok;
}

/* Keep recent dates */
static bool keep_date(YYYYMMDD date)
{
    bool ok = thresh_date < date;
    if (DEBUG_DATABASE(1))
	fprintf(dbgout, "keep_date: %ld < %ld -> %c\n",
		(long)thresh_date, (long)date, ok ? 't' : 'f' );
    return ok;
}

/* Keep sizes within bounds */
static bool keep_size(size_t size)
{
    bool ok = (size_min <= size) && (size <= size_max);
    if (DEBUG_DATABASE(1))
	fprintf(dbgout, "keep_size:  %lu <= %lu <= %lu -> %c\n", 
		(unsigned long)size_min, (unsigned long)size, (unsigned long)size_max, 
		ok ? 't' : 'f' );
    return ok;
}

/* Keep token if at least one user given constraint should be kept */
/* Discard if all user given constraints are satisfied */

bool discard_token(word_t *token, dsv_t *in_val)
{
    bool discard;
 
    if (token->text[0] == '.') {	/* keep .MSG_COUNT and .ROBX */
	if (strcmp((const char *)token->text, MSG_COUNT) == 0)
	    return false;
	if (strcmp((const char *)token->text, ROBX_W) == 0)
	    return false;
    }

    discard = (thresh_count != 0) || (thresh_date != 0) || (size_min != 0) || (size_max != 0);

    if (discard) {
	if (thresh_count != 0 &&
	    (keep_count(in_val->spamcount) || keep_count(in_val->goodcount)))
	    discard = false;
	if (thresh_date != 0 && keep_date(in_val->date))
	    discard = false;
	if ((size_min != 0 || size_max != 0) &&
	    keep_size(token->leng))
	    discard = false;
    }

    return discard;
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

struct userdata_t {
    void *vhandle;
    ta_t *transaction;
};

static int maintain_hook(word_t *w_key, dsv_t *in_val,
			 void *userdata)
{
    size_t len;
    word_t token;
    void *vhandle = ((struct userdata_t *) userdata)->vhandle;
    ta_t *transaction = ((struct userdata_t *) userdata)->transaction;

    token.text = w_key->text;
    token.leng = w_key->leng;

    len = strlen(MSG_COUNT);
    if (len == token.leng && 
	strncmp((char *)token.text, MSG_COUNT, token.leng) == 0)
	return EX_OK;

    if (discard_token(&token, in_val)) {
	int ret = ta_delete(transaction, vhandle, &token);
	if (DEBUG_DATABASE(0))
	    fprintf(dbgout, "deleting '%.*s'\n", (int)min(INT_MAX, token.leng), (char *)token.text);
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
	    ta_delete(transaction, vhandle, &token);	

	    /* retrieve and update nonascii token*/
	    token.text = key_tmp;
	    ret = ta_read(transaction, vhandle, &token, &old_tmp);

	    if (ret == EX_OK) {
		in_val->spamcount += old_tmp.spamcount;
		in_val->goodcount += old_tmp.goodcount;
		in_val->date       = max(old_tmp.date, in_val->date);	/* date in form YYYYMMDD */
	    }
	    set_date(in_val->date);	/* set timestamp */
	    ta_write(transaction, vhandle, &token, in_val);
	    set_date(0);
	}
	xfree(key_tmp);
    }
    return EX_OK;
}

static int maintain_wordlist(void *vhandle)
{
    ta_t *transaction = ta_init();
    struct userdata_t userdata;
    int ret;
    
    userdata.vhandle = vhandle;
    userdata.transaction = transaction;
    
    if (DST_OK == ds_txn_begin(vhandle)) {
	ret = ds_foreach(vhandle, maintain_hook, &userdata);
	if (DST_OK != ds_txn_commit(vhandle))
	    ret = -1;
    } else
	ret = -1;

    return ret | ta_commit(transaction);
}

#if 0
static void maintain_wordlists(void)
{
    wordlist_t *list;

    for (list = word_lists; list != NULL; list = list->next) {
	maintain_wordlist(list->dsh);
	list = list->next;
    }
}
#endif

int maintain_wordlist_file(const char *db_file)
{
    int rc;
    dsh_t *dsh;

    ds_init();
    dsh = ds_open(CURDIR_S, db_file, DB_WRITE);

    if (dsh == NULL)
	return EX_ERROR;

    rc = maintain_wordlist(dsh);

    ds_close(dsh, false);
    ds_cleanup();

    return rc;
}
