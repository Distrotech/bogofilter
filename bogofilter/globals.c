/* $Id$ */

/*****************************************************************************

NAME:
   globals.c -- define global variables

AUTHOR:
   Matthias Andree <matthias.andree@gmx.de>

******************************************************************************/

#include "system.h"
#include "globals.h"
#include "method.h"

/* exports */

/* command line options */
bool	nonspam_exits_zero;		/* '-e' */
bool	force;				/* '-F' */
bool	fisher;				/* '-f' */
bool	logflag;			/* '-l' */
bool	replace_nonascii_characters;	/* '-n' */
bool	passthrough;			/* '-p' */
bool	quiet;				/* '-q' */
bool	terse;				/* '-t' */
int	verbose;			/* '-v' */

FILE	*fpin = NULL;			/* '-I' */
int	Rtable = 0;			/* '-R' */

extern	char	outfname[PATH_LEN];	/* '-O' */

/* config file options */
int	max_repeats;
double	min_dev;
double	spam_cutoff;
double	thresh_stats;

const char	*update_dir;
/*@observer@*/
const char	*stats_prefix;
const char *const system_config_file;

/* dual definition options */
char	*directory;		/* '-d' */
method_t *method = NULL;
