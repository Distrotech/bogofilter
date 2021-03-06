/* $Id$ */

/*****************************************************************************

NAME:
   prob.h -- constants and declarations for prob.c

AUTHOR:
   David Relson <relson@osagesoftware.com>

******************************************************************************/

#ifndef	PROB_H
#define	PROB_H

/** calculate the probability that a token is bad */
extern double calc_prob(uint good, uint bad, uint goodmsgs, uint badmsgs);

#endif	/* PROB_H */

