/* $Id$ */

/*****************************************************************************

NAME:
   base64.c -- decode base64 text

AUTHOR:
   David Relson <relson@osagesoftware.com>

******************************************************************************/

#include "common.h"

#include "base64.h"

/* Local Variables */

/* rfc2047 - BASE64    [0-9a-zA-Z/+=]+
*/

static byte base64_charset[] = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/" };
static byte base64_xlate[256];
static const byte base64_invalid = 0x7F;

/* Function Definitions  */

uint base64_decode(word_t *word)
{
    uint count = 0;
    uint size = word->leng;
    byte *s = word->text;		/* src */
    byte *d = word->text;		/* dst */

    if (!base64_validate(word))
	return size;

    while (size)
    {
	int i;
	int shorten = 0;
	unsigned long v = 0;
	while (size && (*s == '\r' || *s == '\n')) {
	    size--;
	    s++;
	}
	if (size < 4)
	    break;
	for (i = 0; i < 4 && (uint)i < size; i += 1) {
	    byte c = *s++;
	    byte t = base64_xlate[c];
	    if (t == base64_invalid) {
		shorten = 4 - i;
		i = 4;
		v >>= (shorten * 2);
		if (shorten == 2) s++;
		break;
	    }
	    v = v << 6 | t;
	}
	size -= i;
	for (i = 2 - shorten; i >= 0; i -= 1) {
	    byte c = (byte) v & 0xFF;
	    d[i] = c;
	    v = v >> 8;
	}
	d += 3 - shorten;
	count += 3 - shorten;
    }
    *d = (byte) '\0';
    return count;
}

static void base64_init(void)
{
    size_t i;
    static bool first = true;

    if (!first)
	return;
    first = false;

    for (i = 0; i < sizeof(base64_charset); i += 1) {
	byte c = base64_charset[i];
	base64_xlate[c] = (byte) i;
    }

    base64_xlate['='] = base64_invalid;

    return;
}

bool base64_validate(word_t *word)
{
    uint i;

    base64_init();

    for (i = 0; i < word->leng; i += 1) {
	byte b = word->text[i];
	byte v = base64_xlate[b];
	if (v == 0 && b != 'A'  && b != '\n')
	    return false;
    }

    return true;
}