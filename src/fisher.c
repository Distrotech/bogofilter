/* $Id$ */

/*****************************************************************************

NAME:
   fisher.c -- implements Fisher variant on Robinson algorithm.

******************************************************************************/

#include "common.h"

#include <math.h>

#include <dcdflib.h>

#include "fisher.h"

#ifdef HAVE_GSL_14
#include <gsl/gsl_cdf.h>
#endif

#define	RF_DEBUG
#undef	RF_DEBUG

#define FISHER_HAM_CUTOFF	0.00	/* 0.00 for two-state, 0.10 for three-state */
#define FISHER_SPAM_CUTOFF	0.95
#define FISHER_MIN_DEV		0.10

/* Function Prototypes */

static void	fis_initialize_constants(void);
static double	fis_get_spamicity(size_t robn, FLOAT P, FLOAT Q);
static void	fis_print_summary(void);

static rc_t	fis_status(void);

/* Static Variables */

double ham_cutoff = FISHER_HAM_CUTOFF;

extern double robx;		/* in robinson.c */
extern double robs;		/* in robinson.c */
extern double thresh_rtable;	/* in robinson.c */

const parm_desc fis_parm_table[] =
{
    { "robx",		  CP_DOUBLE,	{ (void *) &robx } },
    { "robs",		  CP_DOUBLE,	{ (void *) &robs } },
    { "thresh_rtable",	  CP_DOUBLE,	{ (void *) &thresh_rtable } },
    { "ham_cutoff",	  CP_DOUBLE,	{ (void *) &ham_cutoff } },
    { NULL,		  CP_NONE,	{ (void *) NULL } },
};

rf_method_t rf_fisher_method = {	/* used by config.c */
    {
	"fisher",			/* const char		  *name;		*/
	fis_parm_table,	 		/* m_parm_table		  *parm_table		*/
	fis_initialize_constants,	/* m_initialize_constants *initialize_constants	*/
	rob_bogofilter,	 		/* m_compute_spamicity	  *compute_spamicity	*/
	mth_spamicity,			/* m_spamicity		  *spamicity		*/
	fis_status,			/* m_status		  *status		*/
	rob_print_stats, 		/* m_print_bogostats	  *print_stats		*/
	rob_cleanup, 			/* m_free		  *cleanup		*/
    },
    fis_get_spamicity,			/* rf_get_spamicity	  *get_spamicity	*/
    fis_print_summary			/* rf_print_summary	  *print_summary	*/
};

static rob_stats_t fis_stats;

/* Function Definitions */

static double prbf(double x, double df)
{
#ifdef HAVE_GSL_14
    double r = gsl_cdf_chisq_Q(x, df);
    return (r < DBL_EPSILON) ? 0.0 : r;
#else
    int which=1;
    double p, q;
    int status;
    double bound;

    /* pass in x, df; want p and q; return q */
    cdfchi(&which, &p, &q, &x, &df, &status, &bound);

    return(status==0 ? q : 1.0);
#endif
}

double fis_get_spamicity(size_t robn, FLOAT P, FLOAT Q )
{
    if (robn == 0)
    {
	fis_stats.s.spamicity = robx;
    }
    else
    {
	double df = 2.0 * robn;
	double ln2 = log(2.0);					/* ln(2) */

	fis_stats.robn = robn;

	/* convert to natural logs */
	fis_stats.p_ln = log(P.mant) + P.exp * ln2;		/* invlogsum */
	fis_stats.q_ln = log(Q.mant) + Q.exp * ln2;		/* logsum    */

	fis_stats.p_pr = prbf(-2.0 * fis_stats.p_ln, df);	/* compute P */
	fis_stats.q_pr = prbf(-2.0 * fis_stats.q_ln, df);	/* compute Q */

	fis_stats.s.spamicity = (1.0 + fis_stats.q_pr - fis_stats.p_pr) / 2.0;
    }

    return fis_stats.s.spamicity;
}

void fis_print_summary(void)
{
    if (!Rtable) {
	(void)fprintf(stdout, "%-*s %5lu %9.2e %9.2e %9.2e\n",
		      MAXTOKENLEN+2, "N_P_Q_S_s_x_md", (unsigned long)fis_stats.robn, 
		      fis_stats.p_pr, fis_stats.q_pr, fis_stats.s.spamicity);
	(void)fprintf(stdout, "%-*s %9.2e %9.2e %6.3f\n",
		      MAXTOKENLEN+2+6, " ", robs, robx, min_dev);
    }
    else
	(void)fprintf(stdout, "%-*s %5lu %9.2e %9.2e %9.2e %9.2e %9.2e %5.3f\n",
		      MAXTOKENLEN+2, "N_P_Q_S_s_x_md", (unsigned long)fis_stats.robn, 
		      fis_stats.p_pr, fis_stats.q_pr, fis_stats.s.spamicity, robs, robx, min_dev);
}

void fis_initialize_constants(void)
{
    rob_initialize_with_parameters(&fis_stats, FISHER_MIN_DEV, FISHER_SPAM_CUTOFF);
}

rc_t fis_status(void)
{
    if ( fis_stats.s.spamicity >= spam_cutoff ) 
	return RC_SPAM;

    if (twostate ||
	(ham_cutoff < EPS) ||
	(fis_stats.s.spamicity - ham_cutoff < EPS))
	return RC_HAM;

    return RC_UNSURE;
}

/* Done */
