/* $Id$ */

/*****************************************************************************

NAME:
   base64.h -- prototypes and definitions for base64.c

******************************************************************************/

#ifndef	HAVE_BASE64_H
#define	HAVE_BASE64_H

#include "word.h"

unsigned int	base64_decode(word_t *word);
bool	base64_validate(word_t *word);

#endif	/* HAVE_BASE64_H */
