/* $Id$ */

/* (C) 2004 Matthias Andree. License: GNU GPL v2 */

#include "memstr.h"

void *memstr(const void *hay, size_t n, const char *needle)
{
    unsigned const char *haystack = hay;
    size_t l = strlen(needle);

    while (n >= l) {
	if (0 == memcmp(haystack, needle, l))
	    return (void *)haystack;
	haystack++;
	n--;
    }
    return (void *)0;
}