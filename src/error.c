/* $Id$ */

/*****************************************************************************

NAME:
   error.c -- print and log error messages

AUTHOR:
   David Relson <relson@osagesoftware.com>

******************************************************************************/

#include "common.h"

#include <stdarg.h>
#include <ctype.h>
#include <syslog.h>

#include "error.h"

#ifdef NEEDTRIO
#include <trio.h>
#endif

void print_error( const char *file, unsigned long line, const char *format, ... )
{
    char message[256];
    size_t l;

    va_list ap;
    va_start(ap, format);
    l = (size_t)vsnprintf(message, sizeof(message), format, ap);
    if (l >= sizeof(message)) {
	/* output was truncated, mark truncation */
	strcpy(message + sizeof(message) - 4, "...");
    }
    va_end(ap);

    /* security: replace unprintable characters by underscore "_" */
    for (l = 0; l < strlen(message); l++)
	if (!isprint((unsigned char)message[l]))
	    message[l] = '_';

    fprintf(stderr, "%s: %s\n", progname, message);
#ifdef HAVE_SYSLOG_H
    if (logflag)
	syslog(LOG_INFO, "%s:%lu: %s", file, line, message );
#endif
}
