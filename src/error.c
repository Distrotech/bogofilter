/* $Id$ */

/*****************************************************************************

NAME:
   error.c -- print and log error messages

AUTHOR:
   David Relson <relson@osagesoftware.com>

******************************************************************************/

#include "common.h"

#include <stdarg.h>
#include <unistd.h>

#ifdef HAVE_SYSLOG_H
#include <syslog.h>
#endif

#include "error.h"

#if NEEDTRIO
#include <trio.h>
#endif

void print_error( const char *file, unsigned long line, const char *format, ... )
{
    pid_t pid = getpid();
    char message[256];

    va_list ap;
    va_start (ap, format);
    vsnprintf( message, sizeof(message), format, ap );
    va_end (ap);

    fprintf(stderr, "%s: %s\n", progname, message);
#ifdef HAVE_SYSLOG_H
    if (logflag)
	syslog(LOG_INFO, "[%lu] %s:%lu:  %s", (unsigned long)pid,
		file, line, message );
#endif
}
