/* $Id$ */

/*****************************************************************************

NAME:
   robx.c -- computes robx value by reading wordlist.db

AUTHOR:
   David Relson - C version
   Greg Lous - perl version
   
******************************************************************************/

#include "common.h"

#include "datastore.h"
#include "robx.h"
#include "wordlists.h"

/* Function Prototypes */

/* Function Definitions */

typedef struct robhook_data {
    double   sum;
    uint32_t count;
    uint32_t spam_cnt;
    uint32_t good_cnt;
    dsh_t    *dsh;
    double   scalefactor;
} rhd_t;

static void robx_accum(rhd_t *rh, 
		       word_t *key,
		       dsv_t *data)
{
    uint32_t goodness = data->goodcount;
    uint32_t spamness = data->spamcount;
    double prob = spamness / (goodness * rh->scalefactor + spamness);
    bool doit = goodness + spamness >= 10;

    if (doit) {
	rh->sum += prob;
	rh->count += 1;
    }

    /* print if -vvv and token in both word lists, or -vvvv */
    if ((verbose > 2 && doit) || verbose > 3) {
	fprintf(dbgout, "cnt: %4lu,  sum: %11.6f,  ratio: %9.6f,"
		"  sp: %3lu,  gd: %3lu,  p: %9.6f,  t: %.*s\n", 
		(unsigned long)rh->count, rh->sum, rh->sum / rh->count,
		(unsigned long)spamness, (unsigned long)goodness, prob,
		CLAMP_INT_MAX(key->leng), key->text);
    }
}

static int robx_hook(word_t *key, dsv_t *data, 
		     void *userdata)
{
    struct robhook_data *rh = userdata;

    /* ignore system meta-data */
    if (*key->text == '.')
	return 0;

    robx_accum(rh, key, data);
    
    return 0;
}

/** returns negative for failure.
 * used by bogoutil and bogotune */
double compute_robinson_x(const char *path)
{
    int ret;
    double rx;
    dsh_t *dsh;
    wordlist_t *wordlist;

    struct robhook_data rh;

    set_wordlist_dir(path, PR_NONE);
    open_wordlists(word_lists, DS_READ);
    wordlist = get_default_wordlist(word_lists);

    dsh = wordlist->dsh;

    rh.spam_cnt = wordlist->msgcount[IX_SPAM];
    rh.good_cnt = wordlist->msgcount[IX_GOOD];
    rh.scalefactor = (double)rh.spam_cnt/(double)rh.good_cnt;

    rh.dsh = dsh;
    rh.sum = 0.0;
    rh.count = 0;

    ret = ds_foreach(dsh, robx_hook, &rh);

    rx = rh.sum/rh.count;
    if (verbose > 2)
	printf("%s: %u, %u, scale: %f, sum: %f, cnt: %6d, .ROBX: %f\n",
	       MSG_COUNT, rh.spam_cnt, rh.good_cnt,
	       rh.scalefactor, rh.sum, (int)rh.count, rx);

    close_wordlists(word_lists, true);

    return ret ? -1 : rx;
}
