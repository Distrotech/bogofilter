/* $Id$ */

/*****************************************************************************

NAME:
   qp.c -- decode quoted printable text

AUTHOR:
   David Relson <relson@osagesoftware.com>

******************************************************************************/

#include <config.h>
#include "common.h"

#include "qp.h"

static int hex_to_bin(byte c) {
    switch(c) {
	case '0': return 0;
	case '1': return 1;
	case '2': return 2;
	case '3': return 3;
	case '4': return 4;
	case '5': return 5;
	case '6': return 6;
	case '7': return 7;
	case '8': return 8;
	case '9': return 9;
	case 'a': case 'A': return 10;
	case 'b': case 'B': return 11;
	case 'c': case 'C': return 12;
	case 'd': case 'D': return 13;
	case 'e': case 'E': return 14;
	case 'f': case 'F': return 15;
	default: return -1;
    }
}

size_t qp_decode(byte *buff, size_t size)
{
    byte *s = buff;		/* src */
    byte *d = buff;		/* dst */
    byte *e = buff + size;	/* end */

    while (s < e)
    {
	byte ch = *s++;
	if (ch == '=') {
	    ch = *s++;
	    if (ch != '\n') {
		byte cx = *s++;
		int x, y;
		y = hex_to_bin(ch);
		x = hex_to_bin(cx);
		if (y < 0 || x < 0) {
		    /* illegal stuff in =XX sequence */
		    *d++ = '=';
		    *d++ = ch;
		    *d++ = cx;
		    continue;
		}
		ch = y << 4 | x;
	    } else {
		continue;
	    }
	}
	*d++ = ch;
    }
    return d - buff;
}
