/* $Id$ */

/*****************************************************************************

NAME:
   bogotune.c -- determine optimal parameters for bogofilter

AUTHORS:
   Greg Louis - perl version
   David Relson - C version

******************************************************************************/

/* To allow tuning of large corpora, the memory use must be minimized
** for each messages.  Given the wordlist in ram, foreach token of a
** test message bogotune only needs to know its spam and ham counts.
**
** 1. external wordlist ("-d") flag
**    a. read wordlist.db
**    b. read messages for test set
**       1. lookup words in wordlist
**       2. discard words
**    c. replace wordhashs with wordcnts
**    d. de-allocate resident wordlist
**
** 2. internal wordlist ("-D") flag
**    a. read all messages
**    b. distribute messages
**        1. proper pct to wordlist
**        2. proper pct to test set
** 	  2a. create wordprops from wordlists
**    c. replace wordprops with wordcnts
**    d. de-allocate resident wordlist
*/

/* Limitations:
**
**	If all the input messages are in msg-count format,
**	bogotune will use default ROBX value since an external
**	wordlist is needed to compute a real robx value.
*/

#include "common.h"

#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "bogotune.h"

#include "bogoconfig.h"
#include "bogohome.h"
#include "bogoreader.h"
#include "collect.h"
#include "datastore.h"
#include "msgcounts.h"
#include "mime.h"
#include "paths.h"
#include "robx.h"
#include "rstats.h"
#include "score.h"
#include "token.h"
#include "tunelist.h"
#include "wordhash.h"
#include "wordlists.h"
#include "xmalloc.h"
#include "xstrdup.h"

#undef	HAM_CUTOFF	/* ignore value in score.h */
#undef	SPAM_CUTOFF	/* ignore value in score.h */

#define	TEST_COUNT	500	/* minimum allowable message count */
#define	LIST_COUNT	2000	/* minimum msg count in tunelist   */
#define	PREF_COUNT	4000	/* preferred message count         */
#define	LARGE_COUNT	40000

#define	HAM_CUTOFF	0.10
#define MIN_HAM_CUTOFF	0.10	/* minimum final ham_cutoff */
#define MAX_HAM_CUTOFF	0.45	/* maximum final ham_cutoff */
#define	MIN_CUTOFF	0.55	/* minimum cutoff  for set_thresh() */
#define	WARN_MIN	0.50	/* warning minimum for set_thresh() */
#define	WARN_MAX	0.99	/* warning maximum for set_thresh() */
#define	MAX_CUTOFF	0.99	/* maximum cutoff  for set_thresh() */
#define	SPAM_CUTOFF	0.975
#define FP_CUTOFF	0.999
#define	SCAN_CUTOFF	0.500	/* skip scans if cutoff is less or equal */

#define	CHECK_PCT	0.01	/* for checking high scoring non-spam
				** and low scoring spam */

/* bogotune's default parameters */
#define	DEFAULT_ROBS	ROBS	/* 0.010 */
#define	DEFAULT_ROBX	ROBX	/* 0.415 */
#define	DEFAULT_MIN_DEV	0.02

/* coarse scan parms */
#define	MD_MIN_C	0.06	/* smallest min_dev to test */
#define	MD_MAX_C	0.38	/* largest  min_dev to test */
#define	MD_DLT_C	0.08	/* increment		    */

/* fine scan parms */
#define	MD_MIN_F	0.02
#define	MD_MAX_F	MD_MAX_C+MD_DLT_F
#define	MD_DLT_F	0.014

/* robx limits */
#define RX_MIN		0.4
#define RX_MAX		0.6

enum e_verbosity {
    SUMMARY	   = 1,	/* summarize main loop iterations	*/
    TIME	   = 2, /* include timing info           	*/
    PARMS	   = 3,	/* print parameter sets (rs, md, rx)	*/
    SCORE_SUMMARY  = 5,	/* verbosity level for printing scores	*/
    SCORE_DETAIL	/* verbosity level for printing scores	*/
};

typedef enum e_ds_loc {
    DS_NONE	   = 0,	/* datastore locn not specified */
    DS_ERR	   = 1,	/* error in datastore locn spec */
    DS_DSK	   = 2,	/* datastore on disk */
    DS_RAM	   = 4 	/* datastore in ram  */
} ds_loc;

#define	MOD(n,m)	((n) - (floor((n)/(m)))*(m))
#define	ROUND(m,n)	floor((m)*(n)+.5)/(n)

#define	MIN(n)		((n)/60)
#define SECONDS(n)	((n) - MIN(n)*60)

#define KEY(r)		((r)->fn + (((r)->co > 0.5) ? (r)->co : 0.99))
#define	ESF_SEL(a,b)	(!esf_flag ? (a) : (b))

/* Global Variables */

const char *progname = "bogotune";
static char *ds_file;
static char *ds_path;
static ds_loc ds_flag = DS_NONE;

static bool    bogolex = false;		/* true if convert input to msg-count format */
static bool    esf_flag = true;		/* test ESF factors if true */
static char   *bogolex_file = NULL;
static word_t *w_msg_count;

static tunelist_t *ns_and_sp;
static tunelist_t *ns_msglists, *sp_msglists;

static flhead_t *spam_files, *ham_files;

typedef struct {
    uint cnt;
    double *data;
} data_t;

static data_t *rsval;
static data_t *rxval;
static data_t *mdval;
static data_t *spexp;
static data_t *nsexp;

static uint target;
static uint ns_cnt, sp_cnt;

static double spex, nsex;
static double check_percent;		/* initial check for excessively high/low scores */
static double *ns_scores;
static double *sp_scores;
static double user_robx = 0.0;		/* from option '-r value' */
static uint   coerced_target = 0;	/* user supplied with '-T value' */

static uint   ncnt, nsum;		/* neighbor count and sum - for gfn() averaging */

#undef	TEST

#ifdef	TEST
uint test = 0;
#endif

/* Function Prototypes */

static void bt_exit(void);
static void check_wordlist_path(void);
static int  load_hook(word_t *key, dsv_t *data, void *userdata);
static void load_wordlist(ds_foreach_t *hook, void *userdata);
static void show_elapsed_time(int beg, int end, uint cnt, double val,
			      const char *lbl1, const char *lbl2);
static void write_msgcount_file(wordhash_t *wh);

/* Function Definitions */

static void bt_trap(void) {}

static int get_cnt(double fst, double lst, double amt)
{
    int cnt = (fabs(lst - fst) + EPS) / (fabs(amt) - EPS) + 1;
    return cnt;
}

static data_t *seq_by_amt(double fst, double lst, double amt)
{
    uint i;
    data_t *val = xcalloc(1, sizeof(data_t));

    val->cnt = get_cnt(fst, lst, amt);
    val->data = xcalloc(val->cnt, sizeof(double));

    for (i = 0; i < val->cnt; i += 1)
	val->data[i] = fst + i * amt;

    return val;
}

static data_t *seq_canonical(double fst, double amt)
{
    uint c;
    float v;
    uint i = 0;
    data_t *val = xcalloc(1, sizeof(data_t));

    val->data = xcalloc(5, sizeof(double));

    fst = max(fst, RX_MIN);
    fst = min(fst, RX_MAX);
    val->data[i++] = fst;
    for (c = 1; c <= 2; c += 1) {
	v = fst - amt * c; if (v >= RX_MIN) val->data[i++] = v;
	v = fst + amt * c; if (v <= RX_MAX) val->data[i++] = v;
    }
    val->cnt = i;

    return val;
}

static data_t *seq_by_pow(double fst, double lst, double amt)
{
    uint i;
    data_t *val = xcalloc(1, sizeof(data_t));

    val->cnt = get_cnt(fst, lst, amt);
    val->data = xcalloc(val->cnt, sizeof(double));

    for (i = 0; i < val->cnt; i += 1)
	val->data[i] = pow(10, fst + i * amt);

    return val;
}

static void data_free(data_t *val)
{
    xfree(val->data); xfree(val);
}

static void init_coarse(double _rx)
{
    rxval = seq_canonical(_rx, 0.05);
    rsval = seq_by_pow(0.0, -2.0, -1.0);
    mdval = seq_by_amt(MD_MIN_C, MD_MAX_C, MD_DLT_C);
    spexp = seq_by_amt(ESF_SEL(0.0,2.0), ESF_SEL(0.0,20.0), 3.0);
    nsexp = seq_by_amt(ESF_SEL(0.0,2.0), ESF_SEL(0.0,20.0), 3.0);
}

static void init_fine(double _rs, double _md, double _rx,
		      double _spex, double _nsex)
{
    double s0, s1;

    s0 = max(log10(_rs) - 0.5, -2.0);
    s1 = min(log10(_rs) + 0.5,  0.0);

    rsval = seq_by_pow(s1, s0, -0.25);

    s0 = max(_md - 0.042, MD_MIN_F);
    s1 = min(_md + 0.042, MD_MAX_F);

    mdval = seq_by_amt(s0, s1, MD_DLT_F);
    rxval = seq_canonical(_rx, 0.013);

    s0 = max(_spex - 1.5,  0.0);
    s1 = min(_spex + 1.5, 20.0);

    spexp = seq_by_amt(s0, ESF_SEL(s0,s1), 0.5);

    s0 = max(_nsex - 1.5,  0.0);
    s1 = min(_nsex + 1.5, 20.0);

    nsexp = seq_by_amt(s0, ESF_SEL(s0,s1), 0.5);
}

static void print_parms(const char *label, const char *format, data_t *data)
{
    uint i;
    printf("  %s: %2u ", label, data->cnt);
    for (i = 0; i < data->cnt; i += 1) {
	printf("%s", (i == 0) ? " (" : ", ");
	printf(format, data->data[i]); /* RATS: ignore */
    }
    printf(")\n"); fflush(stdout);
}

static void print_all_parms(uint r_count)
{
    if (verbose >= PARMS) {
	print_parms("rsval", "%6.4f", rsval);
	print_parms("rxval", "%5.3f", rxval);
	print_parms("mdval", "%5.3f", mdval);
	print_parms("spexp", "%5.2f", spexp);
	print_parms("nsexp", "%5.2f", nsexp);
    }
    if (verbose >= TIME)
	printf("cnt: %u (rs: %u, rx: %u, md: %u spex: %u, nsex: %u)\n",
	       r_count, rsval->cnt, rxval->cnt, mdval->cnt, spexp->cnt, nsexp->cnt);
}

static int compare_ascending(const void *const ir1, const void *const ir2)
{
    const double d1 = *(const double *)ir1;
    const double d2 = *(const double *)ir2;

    if (d1 - d2 > 0.0) return  1;
    if (d2 - d1 > 0.0) return -1;

    return 0;
}

static int compare_descending(const void *const ir1, const void *const ir2)
{
    const double d1 = *(const double *)ir1;
    const double d2 = *(const double *)ir2;

    if (d1 - d2 > 0.0) return -1;
    if (d2 - d1 > 0.0) return +1;

    return 0;
}

#define CO(r) ((r->co > 0.5) ? r->co : 0.99)

static int compare_results(const void *const ir1, const void *const ir2)
{
    result_t const *r1 = (result_t const *)ir1;
    result_t const *r2 = (result_t const *)ir2;

    if (KEY(r1) > KEY(r2)) return  1;
    if (KEY(r2) > KEY(r1)) return -1;

    /* Favor cutoffs > 0.5 */
    if (CO(r1) > CO(r2) ) return  1;
    if (CO(r2) > CO(r1) ) return -1;

    if (r1->idx > r2->idx ) return  1;
    if (r2->idx > r1->idx ) return -1;

    return 0;
}

/* Score all non-spam */

static void score_ns(double *results)
{
    uint i;
    uint count = 0;

    if (verbose >= SCORE_DETAIL)
	printf("ns:\n");

    verbose = -verbose;		/* disable bogofilter debug output */
    for (i = 0; i < COUNTOF(ns_msglists->u.sets); i += 1) {
	mlhead_t *list = ns_msglists->u.sets[i];
	mlitem_t *item;
	for (item = list->head; item != NULL; item = item->next) {
	    wordhash_t *wh = item->wh;
	    double score = msg_compute_spamicity(wh, NULL);
	    results[count++] = score;
	    if (-verbose >= SCORE_DETAIL)
		printf("%6u %0.16f\n", count-1, score);
	}
    }
    verbose = -verbose;		/* enable bogofilter debug output */

    qsort(results, count, sizeof(double), compare_descending);

    return;
}

static bool check_for_high_ns_scores(void)
{
    uint t = ceil(ns_cnt * check_percent);

    score_ns(ns_scores);	/* scores in descending order */

    /* want at least 1 high scoring non-spam for FP determination */
    if (ns_scores[t-1] < SPAM_CUTOFF)
	return false;

    if (!quiet) {
	fprintf(stderr,
		"Warning: test messages include many high scoring nonspam.\n");
	fprintf(stderr,
		"         You may wish to reclassify them and rerun.\n");
    }

    return true;
}

/* Score all spam to determine false negative counts */

static void score_sp(double *results)
{
    uint i;
    uint count = 0;

    if (verbose >= SCORE_DETAIL)
	printf("sp:\n");

    verbose = -verbose;		/* disable bogofilter debug output */
    for (i = 0; i < COUNTOF(sp_msglists->u.sets); i += 1) {
	mlhead_t *list = sp_msglists->u.sets[i];
	mlitem_t *item;
	for (item = list->head; item != NULL; item = item->next) {
	    wordhash_t *wh = item->wh;
	    double score = msg_compute_spamicity(wh, NULL);
	    results[count++] = score;
	    if (-verbose >= SCORE_DETAIL)
		printf("%6u %0.16f\n", count-1, score);
	}
    }
    verbose = -verbose;		/* enable bogofilter debug output */

    qsort(results, count, sizeof(double), compare_ascending);

    return;
}

static uint get_fn_count(uint count, double *results)
{
    uint i;
    uint fn = 0;

    for (i = 0; i < count; i += 1) {
	double score = results[i];
	if (score < spam_cutoff)
	    fn += 1;
    }

    return fn;
}

static bool check_for_low_sp_scores(void)
{
    uint t = ceil(sp_cnt * check_percent);

    score_sp(sp_scores);	/* get scores */

    /* low scoring spam may cause problems ... */
    if (sp_scores[t-1] > HAM_CUTOFF)
	return false;

    if (!quiet) {
	fprintf(stderr,
		"Warning: test messages include many low scoring spam.\n");
	fprintf(stderr,
		"         You may wish to reclassify them and rerun.\n");
    }

    return true;
}

static void scoring_error(void)
{
    int i;

    if (quiet)
	return;

    printf("    high ham scores:\n");
    for (i = 0; i < 10 && ns_scores[i] > SPAM_CUTOFF; i += 1)
	printf("      %2d %8.6f\n", i+1, ns_scores[i]);

    printf("    low spam scores:\n");
    for (i = 0; i < 10 && sp_scores[i] < HAM_CUTOFF; i += 1)
	printf("      %2d %8.6f\n", i+1, sp_scores[i]);
}

#ifdef	TEST
static char flag(uint idx, uint cnt, uint dlt)
{
    if (dlt == 0)
	return ' ';
    if (idx < cnt)
	return '+';
    else
	return '-';
}
#endif

#ifdef	TEST
static void print_ns_scores(uint beg, uint cnt, uint dlt)
{
    uint i, m = min(cnt + dlt, ns_cnt);

    printf("ns:\n");
    for (i = beg; i <= m; i += 1)
	printf("    %3d %0.16f %c\n", i+1, ns_scores[i], flag(i, cnt, dlt));
}
#endif

#ifdef	TEST
static void print_sp_scores(uint beg, uint cnt, uint dlt)

{
    uint i, m = min(cnt + dlt, sp_cnt);

    printf("sp:\n");
    for (i = beg; i <= m; i += 1)
	printf("    %3d %0.16f %c\n", i+1, sp_scores[i], flag(i, cnt, dlt));
}
#endif

static double scale(uint cnt, uint lo, uint hi, double beg, double end)
{
    double ans;

    if (cnt < lo)
	return beg;
    if (cnt > hi)
	return end;

    ans = beg + (end - beg) * (cnt - lo ) / (hi - lo);

    return ans;
}

/* compute scores and set global variables:
**
**	 target
**	 spam_cutoff
**
** As count increases from 500 to 4000 ...
**	1) initial target percent drops from 1% to 0.25%
**	2) initial minimum target increases from 5 to 10
*/

static void set_thresh(uint count, double *scores)
{
    uint   ftarget = 0;
    double cutoff, lgc;

    score_ns(scores);		/* get scores */

/*
**	Use parabolic curve to fit data
**	message counts: (70814, 12645, 2118, 550)
**	target values:  (   22,    18,    8,   1)
*/
    lgc = log(count);

    ftarget = max(ROUND(((-0.4831 * lgc) + 12.8976) * lgc - 61.5264, 1.0), 1);
    while (1) {
	cutoff = ns_scores[ftarget-1];
	if (verbose >= PARMS)
	    printf("m:  cutoff %8.6f, ftarget %u\n", cutoff, ftarget);
	if (ftarget == 1 || cutoff >= MIN_CUTOFF)
	    break;
	ftarget -= 1;
    }

    /* ensure cutoff is below SPAM_CUTOFF */
    if (cutoff > SPAM_CUTOFF) {
 	while (cutoff > SPAM_CUTOFF && ++ftarget < count) {
 	    cutoff = scores[ftarget-1];
	    if (verbose >= PARMS)
		printf("s:  cutoff %8.6f, ftarget %u%%\n", cutoff, ftarget);
	}
 	cutoff = SPAM_CUTOFF;
	--ftarget;
	if (verbose >= PARMS)
	    printf("s:  cutoff %8.6f, ftarget %u%%\n", cutoff, ftarget);
    }

    if (cutoff < WARN_MIN || cutoff > WARN_MAX) {
	if (!quiet) {
	    fprintf(stderr,
		    "%s high-scoring non-spams in this data set.\n",
		    (cutoff < WARN_MIN) ? "Too few" : "Too many");
	    fprintf(stderr,
		    "At target %u, cutoff is %8.6f.\n", ftarget, cutoff);
	}
    }

#ifdef	TEST
    if (verbose >= PARMS)
	print_ns_scores(ftarget-4, ftarget+4, 0);
#endif

    target = ftarget;
    spam_cutoff = cutoff;

    return;
}

static uint read_mailbox(char *arg, mlhead_t *msgs)
{
    uint count = 0;
    wordhash_t *train = ns_and_sp->train;

    if (verbose) {
	printf("Reading %s\n", arg);
	fflush(stdout);
    }

    mbox_mode = true;
    bogoreader_init(1, &arg);
    while ((*reader_more)()) {
	wordhash_t *whp = NULL;
	wordhash_t *whc = wordhash_new();

	collect_words(whc);

	if (ds_path != NULL && (int)(msgs_good + msgs_bad) == 0) {
	    wordprop_t *msg_count;
	    msg_count = wordhash_insert(whc, w_msg_count, sizeof(wordprop_t), NULL);
	    if (msg_count->cnts.good == 0 || msg_count->cnts.bad == 0)
		load_wordlist(load_hook, train);
	    if (msgs_good == 0 && msgs_bad == 0) {
		fprintf(stderr, "Can't find '.MSG_COUNT'.\n");
		exit(EX_ERROR);
	    }
	}

	if (whc->count == 0 && !quiet) {
	    printf("msg #%u, count is %u\n", count, whc->count);
	    bt_trap();
	}

	if (whc->count != 0) {
	    count += 1;
	    if (bogolex) {
		write_msgcount_file(whc);
	    }
	    else {
		if (!msg_count_file)
		    whp = convert_wordhash_to_propslist(whc, train);
		else
		    whp = convert_propslist_to_countlist(whc);
		msglist_add(msgs, whp);
	    }
	}

	if (whc != whp)
	    wordhash_free(whc);

	if (verbose && (count % 100) == 0) {
	    if ((count % 1000) != 0)
		putchar('.');
	    else
		printf("\r              \r%u ", count/1000 );
	    fflush(stdout);
	}
    }

    ns_and_sp->count += count;
    bogoreader_fini();

    if (verbose) {
	printf("\r              \r%u messages\n", count);
	fflush(stdout);
    }

    return count;
}

static uint filelist_read(int mode, flhead_t *list)
{
    uint count = 0;
    flitem_t *item;
    mlhead_t *msgs = (mode == REG_GOOD) ? ns_msglists->msgs : sp_msglists->msgs;
    run_type = mode;

    for (item = list->head; item != NULL; item = item->next) {
	lexer = NULL;
	msg_count_file = false;
	count += read_mailbox(item->name, msgs);
    }
    return count;
}

/* distribute()
**
**	Proportionally distribute messages between training and scoring sets.
**
**   Method:
**	If only 2500 messages, use 2000 for training and 500 for scoring.
**	If over 4000 messages, use equal numbers for training and scoring.
**	In between 2500 and 4000, do a proportional distribution.
*/

static void distribute(int mode, tunelist_t *ns_or_sp)
{
    int good = mode == REG_GOOD;
    int bad  = 1 - good;

    bool divvy = ds_file == NULL && user_robx < EPS && !msg_count_file;

    wordhash_t *train = ns_and_sp->train;
    mlhead_t *msgs = ns_or_sp->msgs;
    mlitem_t *item;

    int score_count = 0;
    int train_count = 0;
    int train_good = 0;
    int train_bad  = 0;

    double ratio = scale(msgs->count,
			 LIST_COUNT + TEST_COUNT,	/* small count */
			 LIST_COUNT + LIST_COUNT,	/* large count */
			 LIST_COUNT / TEST_COUNT,	/* small ratio */
			 LIST_COUNT / LIST_COUNT);	/* large ratio */

    for (item = msgs->head; item != NULL; item = item->next) {
	wordhash_t *wh = item->wh;

	/* training set */
	if (divvy && train_count / ratio < score_count + 1) {
	    wordhash_set_counts(wh, good, bad);
	    wordhash_add(train, wh, &wordprop_init);
	    train_count += 1;
	    wordhash_free(wh);
	    train_good += good;
	    train_bad  += bad;
	}
	/* scoring set  */
	else {
	    uint bin = divvy ? MOD(score_count,3) : 0;
	    msglist_add(ns_or_sp->u.sets[bin], wh);
	    score_count += 1;
	}
	item->wh = NULL;
    }

    if (divvy) {
	wordprop_t *msg_count;
	msg_count = wordhash_insert(train, w_msg_count, sizeof(wordprop_t), &wordprop_init);
	set_msg_counts(train_good, train_bad);
    }

    if (verbose > 1)
	printf("%s:  train_count = %d, score_count = %d\n",
	       good ? "ns" : "sp",
	       train_count, score_count);

    return;
}

static void create_countlists(tunelist_t *ns_or_sp)
{
    uint i;
    uint c = COUNTOF(ns_or_sp->u.sets);
    for (i = 0; i<c; i += 1) {
	mlhead_t *list = ns_or_sp->u.sets[i];
	mlitem_t *item;

	for (item = list->head; item != NULL; item = item->next) {
	    wordhash_t *who = item->wh;
	    wordhash_t *whn = convert_propslist_to_countlist(who);
	    if (whn != who) {
		wordhash_free(who);
		item->wh = whn;
	    }
	}
    }

    return;
}

/* write_msgcount_file()
**
**	Create a message count file from the original messages
*/

static void print_msgcount_entry(const char *token, uint bad, uint good)
{
    printf( "\"%s\" %u %u\n", token, bad, good);
}

static void write_msgcount_file(wordhash_t *wh)
{
    hashnode_t *hn;
    wordhash_t *train = ns_and_sp->train;

    print_msgcount_entry(".MSG_COUNT", (int) msgs_bad, (int) msgs_good);

    wordhash_sort(wh);

    for (hn = wordhash_first(wh); hn != NULL; hn = wordhash_next(wh)) {
	word_t *token = hn->key;
	wordprop_t *wp = (wordprop_t *) hn->buf;
	wordcnts_t *cnts = &wp->cnts;

	if (cnts->good == 0 && cnts->bad == 0) {
	    wp = wordhash_search(train, token, 0);
	    if (wp) {
		cnts->good = wp->cnts.good;
		cnts->bad  = wp->cnts.bad;
	    }
	}

	print_msgcount_entry((char *)token->text, cnts->bad, cnts->good);
    }

    return;
}

static void print_version(void)
{
    (void)fprintf(stderr,
		  "%s version %s\n"
		  "    Database: %s\n"
		  "Copyright (C) 2002-2004 Greg Louis, David Relson\n\n"
		  "%s comes with ABSOLUTELY NO WARRANTY.  "
		  "This is free software, and\nyou are welcome to "
		  "redistribute it under the General Public License.  "
		  "See\nthe COPYING file with the source distribution for "
		  "details.\n"
		  "\n",
		  progtype, version, ds_version_str(), PACKAGE);
}

static void help(void)
{
    (void)fprintf(stderr,
		  "Usage:  %s [options] { -c config } { -d directory } -n non-spam-files -s spam-files\n",
		  progname);
    (void)fprintf(stderr,
		  "\t  -h      - print this help message.\n"
		  "\t  -C      - don't read standard config files.\n"
		  "\t  -c file - read specified config file.\n"
		  "\t  -D      - don't read a wordlist file.\n"
		  "\t  -d path - specify directory for wordlists.\n"
		  "\t  -E      - disable ESF (effective size factor) tuning.\n"
		  "\t  -M      - output input file in message count format.\n"
		  "\t  -r num  - specify robx value\n");
    (void)fprintf(stderr,
		  "\t  -T num  - specify fp target value\n"
		  "\t  -s file1 file2 ... - spam files\n"
		  "\t  -n file1 file2 ... - non-spam files\n"
		  "\t  -v      - increase level of verbose messages\n"
		  "\t          - accept high scoring non-spam and low scoring spam\n"
		  "\t  -q      - quiet (suppress warnings)\n"
	);
    (void)fprintf(stderr,
		  "\n"
		  "%s (version %s) is part of the bogofilter package.\n",
		  progname, version);
}

static int process_arglist(int argc, char **argv)
{
    int  count = 1;

    bulk_mode = B_CMDLINE;

#ifdef __EMX__
    _response (&argc, &argv);	/* expand response files (@filename) */
    _wildcard (&argc, &argv);	/* expand wildcards (*.*) */
#endif

    while (--argc > 0) {
	char *arg = *++argv;
	count += 1;
	if (*arg == '-') {
	    while (*++arg) {
		switch (*arg) {
		case 'c':
		    argc -= 1;
		    read_config_file(*++argv, false, !quiet, PR_CFG_USER);
		case 'C':
		    suppress_config_file = true;
		    break;
		case 'd':
		    argc -= 1;
		    ds_file = *++argv;
		    ds_flag = (ds_flag == DS_NONE) ? DS_DSK : DS_ERR;
		    break;
		case 'D':
		    ds_flag = (ds_flag == DS_NONE) ? DS_RAM : DS_ERR;
		    break;
		case 'E':
		    esf_flag ^= true;
		    break;
		case 'I':
		    argc -= 1;
		    bogolex_file = *++argv;
		    break;
		case 'M':
		    bogolex = true;
		    break;
		case 'n':
		    run_type = REG_GOOD;
		    break;
		case 'q':
		    quiet = true;
		    break;
		case 'r':
		    argc -= 1;
		    user_robx = atof(*++argv);
		    break;
		case 's':
		    run_type = REG_SPAM;
		    break;
		case 'v':
		    verbose += 1;
		    break;
#ifdef	TEST
		case 't':
		    test += 1;
		    break;
#endif
		case 'T':
		    argc -= 1;
		    coerced_target = atoi(*++argv);
		    break;
		case 'V':
		    print_version();
		    exit(EX_OK);
		default:
		    help();
		    exit(EX_ERROR);
		}
	    }
	}
	else
	    filelist_add( (run_type == REG_GOOD) ? ham_files : spam_files, arg);
    }

    if (ds_flag == DS_ERR) {
	fprintf(stderr, "Only one '-d dir' or '-D' option is allowed.\n");
	exit(EX_ERROR);
    }

    if (!bogolex &&
	(spam_files->count == 0 || ham_files->count == 0)) {
	fprintf(stderr,
		"Bogotune needs both non-spam and spam message sets for its parameter testing.\n");
	exit(EX_ERROR);
    }

    if (!suppress_config_file)
	process_config_files(false);

    return count;
}

static void check_wordlist_path(void)
{
    struct stat sb;
    size_t len = strlen(ds_file) + strlen(WORDLIST) + 2;
    ds_path = xmalloc(len);

    build_wordlist_path(ds_path, len, ds_file);

    if (stat(ds_path, &sb) != 0) {
	fprintf(stderr, "Error accessing file or directory '%s'.\n", ds_path);
	if (errno != 0)
	    fprintf(stderr, "error #%d - %s.\n", errno, strerror(errno));
	return;
    }

    db_cachesize = ceil(sb.st_size / (3 * 1024 * 1024));

    return;
}

static void load_wordlist(ds_foreach_t *hook, void *userdata)
{
    if (verbose) {
	printf("Reading %s\n", ds_path);
	fflush(stdout);
    }

    ds_init();
    ds_oper(ds_path, DS_READ, hook, userdata);
    ds_cleanup();

    return;
}

static int load_hook(word_t *key, dsv_t *data, void *userdata)
/* returns 0 if ok, 1 if not ok */
{
    wordhash_t *train = userdata;

    wordprop_t *tokenprop = wordhash_insert(train, key, sizeof(wordprop_t), &wordprop_init);
    tokenprop->cnts.bad = data->spamcount;
    tokenprop->cnts.good = data->goodcount;

    if (strcmp((char *)key->text, ".MSG_COUNT") == 0)
	set_msg_counts(data->goodcount, data->spamcount);

    return 0;
}

static double get_robx(void)
{
    double rx;

    if (user_robx > 0.0)
	rx = user_robx;
    else {
	printf("Calculating initial x value...\n");
	verbose = -verbose;		/* disable bogofilter debug output */
	rx = compute_robinson_x(ds_file);
	verbose = -verbose;		/* enable bogofilter debug output */
    }

    if (rx > RX_MAX) rx = RX_MAX;
    if (rx < RX_MIN) rx = RX_MIN;

    printf("Initial x value is %8.6f\n", rx);

    return rx;
}

static result_t *results_sort(uint r_count, result_t *results)
{
    result_t *ans = xcalloc(r_count, sizeof(result_t));
    memcpy(ans, results, r_count * sizeof(result_t));
    qsort(ans, r_count, sizeof(result_t), compare_results);
    return ans;
}

static void top_ten(result_t *sorted, uint n)
{
    uint i, j;
    bool f;

    printf("Top ten parameter sets from this scan:\n");

    printf("        rs     md    rx    spesf    nsesf    co     fp  fn   fppc   fnpc\n");
    for(f = false; !f; f = true) {
      for (i = j = 0; i < 10 && j < n;) {
 	result_t *r = &sorted[j++];
 	if (!f && r->fp != target) continue;
	sp_esf = ESF_SEL(sp_esf, pow(0.75, r->sp_exp));
	ns_esf = ESF_SEL(ns_esf, pow(0.75, r->ns_exp));

	printf("%5u %6.4f %5.3f %5.3f %8.6f %8.6f %6.4f  %3u %3u  %6.4f %6.4f\n",
	       r->idx, r->rs, r->md, r->rx, sp_esf, ns_esf, r->co,
	       r->fp, r->fn, r->fp*100.0/ns_cnt, r->fn*100.0/sp_cnt);
	++i;
      }
      if (i) break;
      printf("Warning: fp target not met, using original results\n");
    }

    printf("\n");
    fflush(stdout);

    return;
}

/* get false negative */

static int gfn(result_t *results,
	       uint rsi, uint mdi, uint rxi,
	       uint spi, uint nsi)
{
    uint i = (((rsi * mdval->cnt + mdi) * rxval->cnt + rxi) * spexp->cnt + spi) * nsexp->cnt + nsi;
    result_t *r = &results[i];
    int fn = r->fn;
    if (r->fp != target) return INT_MAX;
    if (verbose > 100)
	printf("   %2u, %2u, %2u, %2u, %2u, %2d\n",
	       rsi, mdi, rxi, spi, nsi, fn);
    ncnt += 1;
    nsum += fn;
    return fn;
}

static result_t *count_outliers(uint r_count, result_t *sorted, result_t *unsorted)
{
    bool f = false;
    uint i, j = 0, o = 0;
    uint fn;
    uint rsi, mdi, rxi, spi, nsi;
    uint rsc = rsval->cnt - 1;
    uint rxc = rxval->cnt - 1;
    uint mdc = mdval->cnt - 1;
    uint spc = spexp->cnt - 1;
    uint nsc = nsexp->cnt - 1;

    result_t *r = NULL;					/* quench bogus compiler warning */
    uint q33 = sorted[r_count * 33 / 100].fn;		/* 33% quantile */
    uint med = sorted[r_count * 50 / 100].fn;		/* median false negative */

    if (verbose)
	printf("%u%% fn count was %u\n", 50u, med);

    for (i = 0; i < r_count; i += 1) {
	r = &sorted[i];
	if (r->fp != target) continue;
	if (j == 0) j = i+1;
	rsi = r->rsi; mdi = r->mdi; rxi = r->rxi; spi = r->spi; nsi = r->nsi;
	ncnt = nsum = 0;
	if (((rsi == 0   ||
	      (fn = gfn(unsorted, rsi-1, mdi, rxi, spi, nsi)) < med)) &&
	    ((rsi == rsc ||
	      (fn = gfn(unsorted, rsi+1, mdi, rxi, spi, nsi)) < med)) &&
	    ((mdi == 0   ||
	      (fn = gfn(unsorted, rsi, mdi-1, rxi, spi, nsi)) < med)) &&
	    ((mdi == mdc ||
	      (fn = gfn(unsorted, rsi, mdi+1, rxi, spi, nsi)) < med)) &&
	    ((rxi == 0   ||
	      (fn = gfn(unsorted, rsi, mdi, rxi-1, spi, nsi)) < med)) &&
	    ((rxi == rxc ||
	      (fn = gfn(unsorted, rsi, mdi, rxi+1, spi, nsi)) < med)) &&
	    ((spi == 0   ||
	      (fn = gfn(unsorted, rsi, mdi, rxi, spi-1, nsi)) < med)) &&
	    ((spi == spc ||
	      (fn = gfn(unsorted, rsi, mdi, rxi, spi+1, nsi)) < med)) &&
	    ((nsi == 0   ||
	      (fn = gfn(unsorted, rsi, mdi, rxi, spi, nsi-1)) < med)) &&
	    ((nsi == nsc ||
	      (fn = gfn(unsorted, rsi, mdi, rxi, spi, nsi+1)) < med)) &&
	    (nsum / ncnt <  q33))
	{
	    f = true;
	    break;
	}
	o++;
    }

    if (o > 0) {
	printf("%u outlier%s encountered.                                                   \n",
	       o, (o > 1) ? "s" : "");
    }

    if (!f) {
	r = &sorted[j-1];
	printf("No smooth minimum encountered, using lowest fn count (an outlier).         \n");
    }

    return r;
}

static void progress(uint cur, uint top)
{
    uint i;
    uint ndots = ceil(70.0 * cur / top);

    if (quiet)
	return;

    if (ndots < 1)
	ndots = 1;
     printf("\r%3u [", cur);
     for (i = 0; i < ndots; i += 1)
	 printf(".");
     for (i = ndots; i < 70; i += 1)
	 printf(" ");
     printf("]");
     fflush(stdout);
}

static void final_warning(void)
{
    printf(
	"The small number and/or relative uniformity of the test messages imply\n"
	"that the recommended values (above), though appropriate to the test set,\n"
	"may not remain valid for long.  Bogotune should be run again with more\n"
	"messages when that becomes possible.\n"
	);
}

static void final_recommendations(bool skip)
{
    uint m;
    bool printed = false;
    uint minn[] = { 10000, 2000, 1000, 500, 1 };

    printf("Performing final scoring:\n");

    printf("Spam...  ");
    score_sp(sp_scores);	/* get scores (in ascending order) */

    printf("Non-Spam...\n");
    score_ns(ns_scores);	/* get scores (in descending order) */

    for(m=0; m<10; ++m) printf("%8.6f %8.6f\n", sp_scores[m], ns_scores[m]);

    if (verbose >= PARMS)
	printf("# ns_cnt %u, sp_cnt %u\n", ns_cnt, sp_cnt);

    if (skip) {
	printf("\n");
	printf("### The following recommendations are provisional.\n");
	printf("### Run bogotune with more messages when possible.\n");
	printf("\n");
    }

    printf("\nRecommendations:\n\n");
    printf("---cut---\n");
    printf("db_cachesize=%u\n", db_cachesize);

    printf("robs=%6.4f\n", robs);
    printf("min_dev=%5.3f\n", min_dev);
    printf("robx=%8.6f\n", robx);
    printf("sp_esf=%8.6f\n", sp_esf);
    printf("ns_esf=%8.6f\n", ns_esf);

    for (m=0; m < COUNTOF(minn); m += 1) {
	double cutoff;
	uint i, fp = 0, fn = 0;
	uint mn = minn[m];
	double fpp, fnp;

	if (ns_cnt < mn)
	    continue;

	if (mn > 1 ) {
	    uint t = (ns_cnt + mn - 1) / mn;
	    cutoff = ns_scores[t-1];
	    if (cutoff > FP_CUTOFF)
		continue;
	    fp = ns_cnt / mn;
	    fpp = 100.0 / mn;
	}
	else {
	    cutoff = SPAM_CUTOFF;
	    if (printed)
		break;
	    for (i = 0; i < ns_cnt; i += 1) {
		if (ns_scores[i] >= cutoff)
		    fp += 1;
	    }
	    cutoff = ns_scores[fp-1];
	    fpp = 100.0 * fp / ns_cnt;
	}

	for (i = 0; i < sp_cnt; i += 1) {
	    if (sp_scores[i] >= cutoff) {
		fn = i;
		break;
	    }
	}
	fnp = 100.0 * fn / sp_cnt;

	if (printed)  printf("#");
	printf("spam_cutoff=%8.6f\t# for %4.2f%% fp (%u); expect %4.2f%% fn (%u).\n",
	       cutoff, fpp, fp, fnp, fn);

	printed = true;
	if (skip)
	    ham_cutoff = cutoff;
    }

    if (!skip) {
	uint s = ceil(sp_cnt * 0.002 - 1);
	ham_cutoff = sp_scores[s];
	if (ham_cutoff < MIN_HAM_CUTOFF) ham_cutoff = MIN_HAM_CUTOFF;
	if (ham_cutoff > MAX_HAM_CUTOFF) ham_cutoff = MAX_HAM_CUTOFF;
    }

    printf("ham_cutoff=%5.3f\t\n", ham_cutoff);
    printf("---cut---\n");
    printf("\n");

    if (skip)
	final_warning();

    printf("Tuning completed.\n");
}

static void bogotune_init(void)
{
    const char *msg_count = MSG_COUNT;
    w_msg_count = word_new((const byte *)msg_count, strlen(msg_count));
    ns_and_sp   = tunelist_new("tr");		/* training lists */
    ns_msglists = tunelist_new("ns");		/* non-spam scoring lists */
    sp_msglists = tunelist_new("sp");		/* spam     scoring lists */

    return;
}

static void bogotune_free(void)
{
    xfree(ns_scores);
    xfree(sp_scores);

    filelist_free(ham_files);
    filelist_free(spam_files);

    tunelist_free(ns_msglists);
    tunelist_free(sp_msglists);
    tunelist_free(ns_and_sp);

    word_free(w_msg_count);

    token_cleanup();
    mime_cleanup();

    xfree(ds_path);

    return;
}

static bool check_msgcount_parms(void)
{
    bool ok = true;

    if (ds_file == NULL) {
	fprintf(stderr, "A wordlist directory must be specified for converting message to the message count format.\n");
	ok = false;
    }

    if (ham_files->count != 0 && spam_files->count != 0) {
	fprintf(stderr, "Message count files may be created from spam or non-spam inputs but not both.\n");
	fprintf(stderr, "Run bogotune once for the spam and again for the non-spam.\n");
	ok = false;
    }

    return ok;
}

static bool check_msg_counts(void)
{
    bool ok = true;

    if (msgs_good < LIST_COUNT || msgs_bad < LIST_COUNT) {
	if (!quiet)
	    fprintf(stderr,
		    "The wordlist contains %d non-spam and %d spam messages.\n"
		    "Bogotune must be run with at least %d of each.\n",
		    (int) msgs_good, (int) msgs_bad, LIST_COUNT);
	ok = false;
    }

    if (msgs_bad < msgs_good / 5 ||
	msgs_bad > msgs_good * 5) {
	if (!quiet)
	    fprintf(stderr,
		    "The wordlist has a ratio of spam to non-spam of %0.1f to 1.0.\n"
		    "Bogotune requires the ratio be in the range of 0.2 to 5.\n",
		    msgs_bad / msgs_good);
	ok = false;
    }

    if (ns_cnt < TEST_COUNT || sp_cnt < TEST_COUNT) {
	if (!quiet)
	    fprintf(stderr,
		    "The messages sets contain %u non-spam and %u spam.  Bogotune "
		    "requires at least %d non-spam and %d spam messages to run.\n",
		    ns_cnt, sp_cnt, TEST_COUNT, TEST_COUNT);
	exit(EX_ERROR);
    }

    return ok;
}

static rc_t bogotune(void)
{
    bool skip;
    result_t *best;

    int beg, end;
    uint cnt, scan;
    rc_t status = RC_OK;

    bogotune_init();

    if (bogolex) {
	if (check_msgcount_parms())
	    load_wordlist(load_hook, ns_and_sp->train);
	else
	    exit(EX_ERROR);
    }

    beg = time(NULL);

    ham_cutoff = 0.0;
    spam_cutoff = 0.1;

    /* Note: memory usage highest while reading messages */
    /* usage decreases as distribute() converts to count format */

    if (bogolex && bogolex_file != NULL) {
	robx = ROBX;			/* needed for degen */
	read_mailbox(bogolex_file, NULL);
	return status;
    }

    /* read all messages, merge training sets, look up scoring sets */
    ns_cnt = filelist_read(REG_GOOD, ham_files);
    sp_cnt = filelist_read(REG_SPAM, spam_files);
    cnt = ns_cnt + sp_cnt;

    end = time(NULL);
    if (verbose >= TIME) {
	show_elapsed_time(beg, end, ns_cnt + sp_cnt, (double)cnt/(end-beg), "messages", "msg/sec");
    }

    if (bogolex)
	return status;

    distribute(REG_GOOD, ns_msglists);
    distribute(REG_SPAM, sp_msglists);

    create_countlists(ns_msglists);
    create_countlists(sp_msglists);

    if (verbose >= TIME && time(NULL) - end > 2) {
	end = time(NULL);
	show_elapsed_time(beg, end, ns_cnt + sp_cnt, (double)cnt/(end-beg), "messages", "msg/sec");
    }

    if (verbose > PARMS+1) {
	tunelist_print(ns_and_sp);
	tunelist_print(ns_msglists);
	tunelist_print(sp_msglists);
    }

    ns_cnt = count_messages(ns_msglists);
    sp_cnt = count_messages(sp_msglists);
    cnt = ns_cnt + sp_cnt;

    if (ds_file && !check_msg_counts())
	exit(EX_ERROR);

    fflush(stdout);

    check_percent = CHECK_PCT;	/* for checking high scoring non-spam
				** and low scoring spam */

    ns_scores = xcalloc(ns_cnt, sizeof(double));
    sp_scores = xcalloc(sp_cnt, sizeof(double));

    robs = DEFAULT_ROBS;
    robx = DEFAULT_ROBX;
    min_dev = DEFAULT_MIN_DEV;

    if (check_for_high_ns_scores() | check_for_low_sp_scores())
	scoring_error();

    /*
    ** 5.  Calculate x
    ** Calculate x with bogoutil's -r option (a new addition).
    ** Bound the calculated value within [0.4, 0.6] and set the range to be
    ** investigated to [x-0.1, x+0.1].
    */

    if (user_robx > EPS)
	robx = user_robx;
    else if (ds_file != NULL)
	robx = get_robx();

    /*
    ** 6.  Calculate fp target
    ** The fp target will be derived thus: score non-spams with s and md as
    ** shipped, and determine the count that will result from a spam cutoff
    ** of 0.95; if that is < 0.25%, try 0.9375 etc.
    */

    min_dev = 0.02;

    /* set target and spam_cutoff */

    if (coerced_target == 0)
	set_thresh(ns_cnt, ns_scores);
    else {
	/* if coerced target ... */
	target = coerced_target;
	spam_cutoff = ns_scores[target-1];
    }

    skip = ROUND(spam_cutoff,100000) < SCAN_CUTOFF;
    printf("False-positive target is %u (cutoff %8.6f)\n", target, spam_cutoff);

#ifdef	TEST
    if (test) {
	printf("m: %8.6f, s: %8.6f, x: %0.16f\n", min_dev, robs, robx);
	if (verbose < PARMS)
	    print_ns_scores(target-2, target+2, 0);
    }
#endif

    if (!esf_flag && (sp_esf < 1.0 || ns_esf < 1.0))
	fprintf(stderr, "Warning:  Using ESF values (sp=%8.6f, ns=%8.6f) from config file.\n", sp_esf, ns_esf);

    /* No longer needed */
    wordhash_free(ns_and_sp->train);
    ns_and_sp->train = NULL;

    for (scan=0; scan <= 1 && !skip; scan ++) {
	uint r_count;
	uint rsi, rxi, mdi, spi, nsi;
	result_t *results, *r, *sorted;

	printf("Performing %s scan:\n", scan==0 ? "coarse" : "fine");

	switch (scan) {
	case 0:		/* COARSE */
	    /*
	    ** 7.  Coarsely scan s, md and x
	    ** The coarse s scan will range from 1 to 0.01 in half decades, and the
	    ** coarse md scan will range from 0.05 to 0.45 in steps of 0.05.  The
	    ** coarse x scan will use steps of 0.05. The trough must be surrounded on
	    ** six sides by values below the 33% quantile (unless bounded on one or
	    ** more sides).
	    */
	    init_coarse(robx);
	    break;
	case 1:		/* FINE */
	    /*
	    ** 8.  Finely scan the peak region
	    ** The fine s scan will range over the estimated s +/- half a decade in
	    ** steps of a quarter decade, and the fine md scan will range over the
	    ** estimated md +/- 0.075 in steps of 0.015.  The fine x scan will range
	    ** over the estimated x +/- 0.04 in steps of 0.02.  Scans of s and md
	    ** are bounded by the limits of the coarse scan.  Again, the trough must
	    ** be surrounded on six sides by values below the 33% quantile.  If no
	    ** such trough exists, a warning is given.
	    */
	    init_fine(robs, min_dev, robx, spex, nsex);
	    break;
	}

	r_count = rsval->cnt * mdval->cnt * rxval->cnt * spexp->cnt * nsexp->cnt;
	results = (result_t *) xcalloc(r_count, sizeof(result_t));

	print_all_parms(r_count);

	if (verbose >= SUMMARY) {
	    if (verbose >= SUMMARY+1)
		printf("%3s ", "cnt");
	    if (verbose >= SUMMARY+2)
		printf(" %s %s %s      ", "s", "m", "x");
	    printf(" %4s %5s   %4s %8s %8s %7s %3s %3s\n",
		   "rs", "md", "rx", "spesf", "nsesf", "cutoff", "fp", "fn");
	}

	cnt = 0;
	beg = time(NULL);
	for (rsi = 0; rsi < rsval->cnt; rsi++) {
	  robs = rsval->data[rsi];
	  for (mdi = 0; mdi < mdval->cnt; mdi++) {
	    min_dev = mdval->data[mdi];
	    for (rxi = 0; rxi < rxval->cnt; rxi++) {
	      robx = rxval->data[rxi];
	      for (spi = 0; spi < spexp->cnt; spi++) {
		spex = spexp->data[spi];
		sp_esf = ESF_SEL(sp_esf, pow(0.75, spex));
		for (nsi = 0; nsi < nsexp->cnt; nsi++) {
		    uint fp, fn;
		    nsex = nsexp->data[nsi];
		    ns_esf = ESF_SEL(ns_esf, pow(0.75, nsex));

		    /* save parms */
		    r = &results[cnt++];
		    r->idx = cnt;
		    r->rsi = rsi; r->rs = robs;
		    r->rxi = rxi; r->rx = robx;
		    r->mdi = mdi; r->md = min_dev;
		    r->spi = spi; r->sp_exp = spex;
		    r->nsi = nsi; r->ns_exp = nsex;

		    if (verbose >= SUMMARY) {
			if (verbose >= SUMMARY+1)
			    printf("%3u ", cnt);
			if (verbose >= SUMMARY+2)
			    printf(" %u %u %u %u %u  ",
				rsi, mdi, rxi, spi, nsi);
			printf("%6.4f %5.3f %5.3f %8.6f %8.6f",
			    robs, min_dev, robx, sp_esf, ns_esf);
			fflush(stdout);
		    }

		    spam_cutoff = 0.01;
		    score_ns(ns_scores);	/* scores in descending order */

		    /* Determine spam_cutoff and false_pos */
		    for (fp = target; fp < ns_cnt; fp += 1) {
			spam_cutoff = ns_scores[fp-1];
			if (spam_cutoff < 0.999999)
			    break;
			if (coerced_target != 0)
			    break;
		    }
		    if (ns_cnt < fp)
			fprintf(stderr,
				"Too few false positives to determine a valid cutoff\n");

		    score_sp(sp_scores);	/* scores in ascending order */
		    fn = get_fn_count(sp_cnt, sp_scores);

		    /* save results */
		    r->co = spam_cutoff;
		    r->fp = fp;
		    r->fn = fn;

		    if (verbose < SUMMARY)
			progress(cnt, r_count);
		    else {
			printf(" %8.6f %2u %3u\n", spam_cutoff, fp, fn);
			fflush(stdout);
		    }

#ifdef	TEST
		    if (test && spam_cutoff < 0.501) {
			printf("co: %0.16f\n", spam_cutoff);
			print_ns_scores(0, fp, 2);
			print_sp_scores(fn-10, fn, 10);
		    }
#endif
		}
	      }
	    }
	  }
	  fflush(stdout);
	}

	if (verbose >= TIME) {
	    end = time(NULL);
	    show_elapsed_time(beg, end, cnt, (double)(end-beg)/cnt, "iterations", "secs");
	}

	printf("\n");

	/* Scan complete, now find minima */

	sorted = results_sort(r_count, results);
	top_ten(sorted, r_count);

	best = count_outliers(r_count, sorted, results);
	robs = rsval->data[best->rsi];
	robx = rxval->data[best->rxi];
	min_dev = mdval->data[best->mdi];

	spex = spexp->data[best->spi]; sp_esf = ESF_SEL(sp_esf, pow(0.75, spex));
	nsex = nsexp->data[best->nsi]; ns_esf = ESF_SEL(ns_esf, pow(0.75, nsex));

	printf(
    "Minimum found at s %6.4f, md %5.3f, x %5.3f, spesf %8.6f, nsesf %8.6f\n",
    		robs, min_dev, robx, sp_esf, ns_esf);
	printf("        fp %u (%6.4f%%), fn %u (%6.4f%%)\n",
		best->fp, best->fp*100.0/ns_cnt,
		best->fn, best->fn*100.0/sp_cnt);
	printf("\n");

	data_free(rsval);
	data_free(rxval);
	data_free(mdval);
	data_free(spexp);
	data_free(nsexp);

	xfree(results);
	xfree(sorted);
    }

    /*
    ** 9.  Suggest possible spam and non-spam cutoff values
    ** With the final x, md and s values, score the spams and non-spams and
    ** sort the non-spam scores decreasing and the spam scores increasing;
    ** then, traverse the non-spam list until the 0.2% point; report cutoffs
    ** that give 0.05%, 0.1% and 0.2% fp.
    */

    final_recommendations(skip);

    return status;
}

int main(int argc, char **argv) /*@globals errno,stderr,stdout@*/
{
    ex_t exitcode = EX_OK;

    atexit(bt_exit);

    fBogotune = true;		/* for rob_compute_spamicity() */

    dbgout = stdout;
    debug_mask=MASK_BIT('R');
    debug_mask=0;

    progtype = build_progtype(progname, DB_TYPE);

    ham_files  = filelist_new("ham");
    spam_files = filelist_new("spam");

    /* directories from command line and config file are already handled */

    ds_file = get_directory(PR_ENV_BOGO);
    if (ds_file == NULL)
	ds_file = get_directory(PR_ENV_HOME);

    /* process args and read mailboxes */
    process_arglist(argc, argv);

    if (ds_file) {
	set_bogohome(ds_file);
	check_wordlist_path();
    }

    ds_init();

    bogotune();

    bogotune_free();

    ds_cleanup();

    exit(exitcode);
}

static void bt_exit(void)
{
    return;
}

static void show_elapsed_time(int beg, int end, uint cnt, double val,
			      const char *lbl1, const char *lbl2)
{
    int tm = end - beg;
    printf("    %dm:%02ds for %u %s.  avg: %.1f %s\n",
	   MIN(tm), SECONDS(tm), cnt, lbl1, val, lbl2);
}

/* End */