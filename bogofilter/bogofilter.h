/* $Id$ */
/*  constants and declarations for bogofilter */

#ifndef	HAVE_BOGOFILTER_H
#define	HAVE_BOGOFILTER_H

#include <wordlists.h>

#define EVEN_ODDS	0.5f		/* used for words we want to ignore */
#define UNKNOWN_WORD	0.4f		/* odds that unknown word is spammish */
#define DEVIATION(n)	fabs((n) - EVEN_ODDS)	/* deviation from average */

typedef enum rc_e {RC_SPAM=0, RC_NONSPAM=1}  rc_t;

extern void initialize_constants(void);
extern rc_t bogofilter(/*@out@*/ double *xss);
extern void print_stats(FILE *fp, double spamicity);

#endif	/* HAVE_BOGOFILTER_H */
