/* $Id$ */

#ifndef GLOBALS_H
#define GLOBALS_H

#include <float.h> /* has DBL_EPSILON */
#define EPS	DBL_EPSILON	 /* equality cutoff */

#include "system.h" /* has bool */
#include "common.h" /* has PATH_LEN */

#ifdef __LCLINT__
typedef int  bool;
#endif

/* command line options */
extern	bulk_t	bulk_mode;		/* '-B' */
extern	bool	suppress_config_file;	/* '-C' */
extern	bool	nonspam_exits_zero;	/* '-e' */
extern	bool	fisher;			/* '-f' */
extern	FILE	*fpin;			/* '-I' */
extern	bool	logflag;		/* '-l' */
extern	bool	mbox_mode;		/* '-M' */
extern	char	outfname[PATH_LEN];	/* '-O' */
extern	bool	passthrough;		/* '-p' */
extern	bool	quiet;			/* '-q' */
extern	int	query;			/* '-Q' */
extern	bool	Rtable;			/* '-R' */
extern	bool	terse;			/* '-t' */
extern	int	bogotest;		/* '-X', env("BOGOTEST") */
extern	int	verbose;		/* '-v' */
extern	bool	replace_nonascii_characters;	/* '-n' */

/* config file options */
extern	int	max_repeats;
extern	double	min_dev;
extern	double	ham_cutoff;
extern	double	spam_cutoff;
extern	double	thresh_update;

extern	int	abort_on_error;
extern	bool	stats_in_header;

extern	bool	header_line_markup;	/* -Ph,-PH */

extern	const char	*update_dir;
extern	const char	*progname;
extern	const char	*progtype;
/*@observer@*/
extern	const char	*stats_prefix;
extern	const char *const version;
extern	const char *const system_config_file;

/* for msgcounts.c */
#define	MSG_COUNT	".MSG_COUNT"
extern	double		 msgs_good;
extern	double		 msgs_bad;

/* for passthrough.c */
extern size_t msg_register_size;
extern char msg_register[256];

extern bool maintain;
extern bool onlyprint;

/*		    old     new
**  robs            0.010   0.0178
**  robx            0.415   0.52
**  min_dev         0.1     0.375
**  spam_cutoff     0.95    0.99
**  ham_cutoff      0.00    0.00    (bi-state)
**  ham_cutoff      0.10    0.45    (tri-state)
*/

#define ROBS		0.0178	/* Robinson's s */
#define ROBX		0.52	/* Robinson's x */
#define MIN_DEV		0.375

#define	SP_ESF		1.0
#define	NS_ESF		1.0

#define HAM_CUTOFF	0.00	/* 0.00 for two-state, 0.45 for three-state */
#define SPAM_CUTOFF	0.99

#define ROBX_W		".ROBX"

extern	double robs;
extern	double robx;
extern	double sp_esf;
extern	double ns_esf;

/* for  bogotune */
extern	bool fBogotune;

/* for  BerkeleyDB */
#define	DB_CACHESIZE	4	/* in MB */
extern	uint	db_cachesize;

/* other */

extern FILE  *fpo;

enum passmode { PASS_MEM, PASS_SEEK };
extern enum passmode passmode;

extern	bool	msg_count_file;
extern	bool	unsure_stats;	/* true if print stats for unsures */

#define	WORDLIST_VERSION	".WORDLIST_VERSION"
extern	uint	wordlist_version;

#endif
