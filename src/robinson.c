/* $Id$ */

/*****************************************************************************

NAME:
   robinson.c -- implements f(w) and S, or Fisher, algorithm for computing spamicity.

******************************************************************************/

#include "common.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "bogoconfig.h"
#include "bogofilter.h"
#include "collect.h"
#include "datastore.h"
#include "degen.h"
#include "msgcounts.h"
#include "robinson.h"
#include "rstats.h"
#include "wordhash.h"
#include "wordlists.h"

#define ROBINSON_MIN_DEV	0.0	/* if nonzero, use characteristic words */
#define ROBINSON_SPAM_CUTOFF	0.54	/* if it's spammier than this... */
#define ROBINSON_MAX_REPEATS	1	/* cap on word frequency per message */
  
extern int Rtable;

double	thresh_rtable = 0.0;		/* used in fisher.c */
double	robx = 0.0;			/* used in fisher.c and rstats.c */
double	robs = 0.0;			/* used in fisher.c and rstats.c */

#define ROBX_S ".ROBX"

static bool	need_stats = false;
static rob_stats_t  rob_stats;

const parm_desc rob_parm_table[] =	/* needed by fisher.c */
{
    { "robx",		  CP_DOUBLE,	{ (void *) &robx } },
    { "robs",		  CP_DOUBLE,	{ (void *) &robs } },
    { "thresh_rtable",	  CP_DOUBLE,	{ (void *) &thresh_rtable } },
    { NULL,		  CP_NONE,	{ (void *) NULL } },
};

/* Function Prototypes */

#ifdef	ENABLE_ROBINSON_METHOD
static void	rob_initialize_constants(void);
static double	rob_get_spamicity(size_t robn, FLOAT P, FLOAT Q);
static void	rob_print_summary(void);
#endif

/* Static Variables */

double bad_cnt;
double good_cnt;

#ifdef	ENABLE_ROBINSON_METHOD
rf_method_t rf_robinson_method = {	/* needed by config.c */
    {
	"robinson",			/* const char		  *name;		*/
	rob_parm_table,	 		/* m_parm_table		  *parm_table		*/
	rob_initialize_constants,	/* m_initialize_constants *initialize_constants	*/
	rob_bogofilter,	 		/* m_compute_spamicity	  *compute_spamicity	*/
	mth_spamicity,			/* m_spamicity		  *spamicity		*/
	mth_status,			/* m_status		  *status		*/
	rob_print_stats, 		/* m_print_bogostats	  *print_stats		*/
	rob_cleanup, 			/* m_free		  *cleanup		*/
    },
    rob_get_spamicity,			/* rf_get_spamicity	  *get_spamicity	*/
    rob_print_summary			/* rf_print_summary	  *print_summary	*/
};
#endif

void rob_print_stats(FILE *fp)
{
    fp = NULL; 	/* quench compiler warning */
    if (force || Rtable || verbose>=3 ||
	rob_stats.s.spamicity > thresh_stats || 
	rob_stats.s.spamicity > thresh_rtable || 
	method->status() == RC_UNSURE )
	rstats_print();
}

static void wordprob_add(wordprop_t* wordstats, int count, int bad)
{
    if (bad)
	wordstats->bad += count;
    else
	wordstats->good += count;
}

static double wordprob_result(wordprop_t* wordstats)
{
    double g = min(wordstats->good, msgs_good);
    double b = min(wordstats->bad,  msgs_bad);
    double n = g + b;

    double pw = (n < EPS) ? 0.0 : ((b / bad_cnt) / 
				   (b / bad_cnt + g / good_cnt));
    double fw = (robs * robx + n * pw) / (robs + n);

    return (fw);
}

static void lookup(const word_t *token, wordprop_t *wordstats)
{
    int override=0;
    wordlist_t* list;

    for (list=word_lists; list != NULL; list=list->next)
    {
	size_t i;
	dsv_t val;

	if (list->ignore)
	    return;

	if (override > list->override)
	    break;
	override=list->override;

	ds_read(list->dsh, token, &val);

	for (i=0; i<COUNTOF(val.count); i++) {
	    /* Protect against negatives */
	    if ((int) val.count[i] < 0) {
		val.count[i] = 0;
		ds_write(list->dsh, token, &val);
	    }

	    if (val.count[i] == 0)
		continue;

	    wordprob_add(wordstats, val.count[i], list->bad[i]);
	    if (DEBUG_ROBINSON(1)) {
		fprintf(dbgout, "%2d %2d \n", (int) wordstats->good, (int) wordstats->bad);
		word_puts(token, 0, dbgout);
		fputc('\n', dbgout);
	    }
	}
    }
    return;
}

double lookup_and_score(const word_t *token, wordprop_t *wordstats)
{
    double prob;

    if (wordstats->bad == 0 && wordstats->good == 0)
	lookup(token, wordstats);

    if (header_degen && token->leng >= 5 && memcmp(token->text, "head:", 5) == 0) {
	word_t *tword = word_new(token->text+5, token->leng-5);
	wordprop_t tprop;
	wordprop_init(&tprop);
	lookup(tword, &tprop);
	wordprop_incr(wordstats, &tprop);
	prob = wordprob_result(wordstats);
	word_free(tword);
    }
    else
    if (wordstats->bad != 0 || wordstats->good != 0) {
	prob = wordprob_result(wordstats);
    }
    else
    if (degen_enabled)
    {
	degen_enabled = false;	/* Disable further recursion */
	prob = degen(token, wordstats);
	degen_enabled = true;	/* Enable further recursion */
    }
    else
	prob = wordprob_result(wordstats);

    return prob;
}

static double compute_probability(const word_t *token, wordprop_t *wordstats)
{
    if (wordstats->bad != 0 || wordstats->good != 0)
	/* A msg-count file already has the values needed */
	wordstats->prob = wordprob_result(wordstats);
    else
	/* Otherwise lookup the word and get its score */
	wordstats->prob = lookup_and_score(token, wordstats);

    if (need_stats)
	rstats_add(token, wordstats);

    return wordstats->prob;
}

double rob_compute_spamicity(wordhash_t *wh, FILE *fp) /*@globals errno@*/
/* selects the best spam/non-spam indicators and calculates Robinson's S */
{
    hashnode_t *node;

    FLOAT P = {1.0, 0};		/* Robinson's P */
    FLOAT Q = {1.0, 0};		/* Robinson's Q */

    double spamicity;
    size_t robn = 0;
    size_t count = 0;

    (void) fp; 	/* quench compiler warning */

    bad_cnt  = max(1, msgs_bad);
    good_cnt = max(1, msgs_good);

    if (DEBUG_ROBINSON(2)) fprintf(dbgout, "### rob_compute_spamicity() begins\n");

    need_stats = Rtable || (verbose + passthrough) > 1;
    Rtable |= verbose > 3;

    if (need_stats)
	rstats_init();

    if (DEBUG_ROBINSON(2)) fprintf(dbgout, "min_dev: %f, robs: %f, robx: %f\n", 
				   min_dev, robs, robx);

    wordhash_sort(wh);

    for(node = wordhash_first(wh); node != NULL; node = wordhash_next(wh))
    {
	word_t *token = node->key;
	wordprop_t *props = (wordprop_t *) node->buf;
	double prob = compute_probability(token, props);

	count += 1;

	/* Robinson's P and Q; accumulation step */
        /*
	 * P = 1 - ((1-p1)*(1-p2)*...*(1-pn))^(1/n)     [spamminess]
         * Q = 1 - (p1*p2*...*pn)^(1/n)                 [non-spamminess]
	 */
        if (fabs(EVEN_ODDS - prob) - min_dev >= EPS) {
	    int e;

	    P.mant *= 1-prob;
	    if (P.mant < 1.0e-200) {
		P.mant = frexp(P.mant, &e);
		P.exp += e;
	    }

	    Q.mant *= prob;
	    if (Q.mant < 1.0e-200) {
		Q.mant = frexp(Q.mant, &e);
		Q.exp += e;
	    }
            robn ++;
        }

	if (DEBUG_ROBINSON(3)) {
	    (void)fprintf(dbgout, "%3lu %3lu %f ",
			  (unsigned long)robn, (unsigned long)count, prob);
	    (void)word_puts(token, 0, dbgout);
	    (void)fputc('\n', dbgout);
	}
    }

    /* Robinson's P, Q and S
    ** S = (P - Q) / (P + Q)                        [combined indicator]
    */

    spamicity = ((rf_method_t *) method)->get_spamicity( robn, P, Q );

    if (robn && need_stats)
	rstats_fini(robn, P, Q, spamicity );

    if (DEBUG_ROBINSON(2)) fprintf(dbgout, "### rob_compute_spamicity() ends\n");

    return (spamicity);
}

#ifdef	ENABLE_ROBINSON_METHOD
double rob_get_spamicity(size_t robn, FLOAT P, FLOAT Q)
{
    if (robn == 0)
	rob_stats.s.spamicity = robx;
    else
    {
	double r = 1.0 / robn;
	static double ln2 = 0.6931472;				/* ln(2) */

	rob_stats.robn = robn;

	/* convert to natural logs */
	rob_stats.p_ln = log(P.mant) + P.exp * ln2;		/* invlogsum */
	rob_stats.q_ln = log(Q.mant) + Q.exp * ln2;		/* logsum    */

	rob_stats.p_pr = 1.0 - exp(rob_stats.p_ln * r);		/* Robinson's P */
	rob_stats.q_pr = 1.0 - exp(rob_stats.q_ln * r);		/* Robinson's Q */

	rob_stats.s.spamicity = (1.0 + (rob_stats.p_pr - rob_stats.q_pr) / (rob_stats.p_pr + rob_stats.q_pr)) / 2.0;
    }

    return rob_stats.s.spamicity;
}
#endif

#ifdef	ENABLE_ROBINSON_METHOD
void rob_print_summary(void)
{
    if (!Rtable) {
	(void)fprintf(stdout, "%-*s %5lu %9.6f %9.6f %9.6f\n",
		      MAXTOKENLEN+2, "P_Q_S_invs_logs_md", 
		      (unsigned long)rob_stats.robn,
		      rob_stats.p_pr, rob_stats.q_pr, rob_stats.s.spamicity);
	(void)fprintf(stdout, "%-*s %9.2e %9.2e %6.3f\n",
		      MAXTOKENLEN+2+6, " ", robs, robx, min_dev);
    }
    else
	(void)fprintf(stdout, "%-*s %5lu %9.6f %9.6f %9.6f %9.3f %9.3f %5.3f\n",
		      MAXTOKENLEN+2, "P_Q_S_invs_logs_md", 
		      (unsigned long)rob_stats.robn,
		      rob_stats.p_pr, rob_stats.q_pr, rob_stats.s.spamicity, 
		      rob_stats.p_ln, rob_stats.q_ln, min_dev);
}
#endif

void rob_initialize_with_parameters(rob_stats_t *stats, double _min_dev, double _spam_cutoff)
{
    word_t *word_robx = word_new((const byte *)ROBX_W, strlen(ROBX_W));

    mth_initialize( stats, ROBINSON_MAX_REPEATS, _min_dev, _spam_cutoff, ROBINSON_GOOD_BIAS );

    /*
    ** If we're classifying messages, we need to compute the scalefactor 
    ** (from the .MSG_COUNT values)
    ** If we're registering tokens, we needn't get .MSG_COUNT
    */

    compute_msg_counts();
    if (fabs(robs) < EPS)
	robs = ROBS;

    if (fabs(robx) < EPS && word_list->dsh != NULL)
    {
	int ret;
	dsv_t val;

	/* Note: .ROBX is scaled by 1000000 in the wordlist */
	ret = ds_read(word_list->dsh, word_robx, &val);
	if (ret != 0)
	    robx = ROBX;
	else {
	    /* If found, unscale; else use predefined value */
	    long l_robx = val.count[IX_SPAM];
	    robx = l_robx ? (double)l_robx / 1000000 : ROBX;
	}
    }

    if (robx < 0.0 || 1.0 < robx) {
	fprintf(stderr, "Invalid robx value (%f).  Must be between 0.0 and 1.0\n", robx);
	exit(EX_ERROR);
    }

    word_free(word_robx);

    return;
}

#ifdef	ENABLE_ROBINSON_METHOD
void rob_initialize_constants(void)
{
    rob_initialize_with_parameters(&rob_stats, ROBINSON_MIN_DEV, ROBINSON_SPAM_CUTOFF);
}
#endif

double rob_bogofilter(wordhash_t *wh, FILE *fp) /*@globals errno@*/
{
    rob_stats.s.spamicity = rob_compute_spamicity(wh, fp);
    return rob_stats.s.spamicity;
}

void rob_cleanup(void)
{
    /* Not yet implemented. */
}

/* Done */
