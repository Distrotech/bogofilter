/* $Id$ */

/*****************************************************************************

NAME:
   configfile.h -- prototypes and definitions for bogoconfig.c.

AUTHOR:
   David Relson <relson@osagesoftware.com>

******************************************************************************/

#ifndef CONFIGFILE_H
#define CONFIGFILE_H

/* Definitions */

typedef enum {
	CP_NONE,
	CP_BOOLEAN,
	CP_INTEGER,
	CP_DOUBLE,
	CP_CHAR,
	CP_STRING,
	CP_DIRECTORY,
#ifdef	ENABLE_DEPRECATED_CODE
	CP_WORDLIST,
#endif
	CP_FUNCTION
} parm_t;

typedef bool func(const unsigned char *s);

typedef struct {
    const char *name;
    parm_t	type;
    union
    {
	void	*v;
	func	*f;
	bool	*b;
	int	*i;
	double	*d;
	char	*c;
	char   **s;
    } addr;
} parm_desc;

/* Global variables */

extern const parm_desc sys_parms[];
extern const parm_desc *usr_parms;

bool process_config_files(bool warn_on_error);
bool process_config_option(char *arg, bool warn_on_error, priority_t precedence);
bool read_config_file(const char *fname, bool tilde_expand, bool warn_on_error, priority_t precedence);

#endif
