/* $Id$ */

/*****************************************************************************

NAME:
   bogoconfig.h -- prototypes and definitions for bogoconfig.c.

AUTHOR:
   David Relson <relson@osagesoftware.com>

******************************************************************************/

#ifndef BOGOCONFIG_H
#define BOGOCONFIG_H

#include "system.h"

#include "configfile.h"
#include "getopt.h"

/* Global variables */

extern const char *logtag;
extern const char *spam_header_name;
extern const char *user_config_file;

extern void query_config(void) __attribute__ ((noreturn));
extern void process_parameters(int argc, char **argv, bool warn_on_error);

extern struct option long_options[];
extern void process_arg(int option, const char *name, const char *arg, priority_t precedence, arg_pass_t pass);

#endif