/* $Id$ */

#ifndef FORMAT_H
#define FORMAT_H

#include "configfile.h"

/* Global variables */

extern const char *spam_header_name;
extern const char *spam_subject_tag;

/*
** extern const char *header_format;
** extern const char *terse_format;
** extern const char *update_log_format;
** extern const char *log_header_format;
*/

/* needed by bogoconfig.c */

extern const parm_desc format_parms[];
extern void set_terse_mode_format(int mode);

/* Function Prototypes */

extern char *format_header(char *buff, size_t size);
extern char *format_terse(char *buff, size_t size);
extern char *format_log_header(char *buff, size_t size);
extern char *format_log_update(char *buff, size_t size, const char *reg, const char *unreg, int wordcount, int msgcount);
#endif
