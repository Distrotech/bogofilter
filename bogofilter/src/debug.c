/* $Id$ */

/*****************************************************************************

NAME:
   debug.c - shared debug functions

AUTHOR:
   David Relson <relson@osagesoftware.com>

******************************************************************************/

#include "common.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

FILE *dbgout;
int debug_mask = DEBUG_NONE;

void set_debug_mask(const char *mask)
{
    char ch;
    const char *maskbits = BIT_NAMES;
    for (ch = tolower((unsigned char)*mask); ch != '\0'; ch = *++mask)
    {
	/*@-shiftnegative@*/
	if (strchr(maskbits, ch) != NULL)
	    debug_mask |= (1 << (ch - 'a'));
	/*@=shiftnegative@*/
	else
	{
	    (void)fprintf(stderr, "set_debug_mask:  unknown mask specification '%c'\n", ch);
	    exit(EX_ERROR);
	}
    }
}

/* 'C' - parse "&#61;" as char  */
/* 'T' - parse "&#61;" as token */
/* 'L' - enable lexer_v3 debug output */

void set_bogotest(const char *mask)
{
    char ch;
    while ((ch = *mask++) != '\0' && isalpha(ch))
    {
	ch = tolower(ch);
	bogotest |= BOGOTEST(ch);
    }
}
