/* $Id$ */

/*****************************************************************************

NAME:
   common.h -- common definitions and prototypes for bogofilter

******************************************************************************/

#ifndef COMMON_H
#define COMMON_H

#ifndef	CONFIG_H
# define  CONFIG_H
# include "config.h"
#endif

#ifndef BFTYPES_H
#  include "bftypes.h"
#endif

#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif
#include <stdio.h>

#if defined(HAVE_LIMITS_H)
#include <limits.h>
#elif defined(HAVE_SYS_PARAM_H)
#include <sys/param.h>
#endif

#include "debug.h"

#ifdef	ENABLE_MEMDEBUG
#include "memdebug.h"
#else
#define	MEMDISPLAY  do { } while(0)
#endif

/* for easier debugging - can be disabled */
#if	0
#define	D	0	/* size adjustment */
#define	Z(n)		/* mark end of string */
#else
#define	D	1	/* size adjustment */
#define	Z(n) n=(byte)'\0' /* mark end of string */
#endif

/* length of token will not exceed this... */
#define MAXTOKENLEN	30

typedef enum sh_e { IX_SPAM = 0, 	/* index for SPAM */
		    IX_GOOD = 1, 	/* index for GOOD */
		    IX_SIZE = 2, 	/* array size     */
		    IX_UNDF = 3 	/* ... undefined  */
} sh_t;

#define max(x, y)	(((x) > (y)) ? (x) : (y))
#define min(x, y)	(((x) < (y)) ? (x) : (y))

#define	NL	"\n"
#define	CRLF	"\r\n"

#if defined(PATH_MAX)
#define PATH_LEN PATH_MAX
#elif defined(MAXPATHLEN)
#define PATH_LEN MAXPATHLEN
#else
#define PATH_LEN 1024
#endif

/** Default database file mode */
#define	DS_MODE 	(mode_t) 0664

/** Default directory */
#define	DIR_MODE	(mode_t) 0775

#define COUNTOF(array)	(uint) (sizeof(array)/sizeof(array[0]))

typedef unsigned char byte;

enum dbmode_e { DS_READ = 1, DS_WRITE = 2, DS_LOAD = 8 };
typedef enum dbmode_e dbmode_t;

#define BIT(n)	(1 << n)

typedef enum rc_e { RC_SPAM	= 0,
		    RC_HAM	= 1,
		    RC_UNSURE	= 2,
		    RC_OK,
		    RC_MORE	}  rc_t;

typedef enum ex_e { EX_SPAM	= RC_SPAM,
		    EX_HAM	= RC_HAM,
		    EX_UNSURE	= RC_UNSURE,
		    EX_OK	= 0,
		    EX_ERROR	= 3 } ex_t;

typedef enum run_e {
    RUN_UNKNOWN= 0,
    RUN_NORMAL = BIT(0),
    RUN_UPDATE = BIT(1),
    REG_SPAM   = BIT(2),
    REG_GOOD   = BIT(3),
    UNREG_SPAM = BIT(4),
    UNREG_GOOD = BIT(5)
} run_t;
extern run_t run_type;
extern bool  run_classify;
extern bool  run_register;

typedef struct {
    double mant;
    int    exp;
} FLOAT;

typedef enum priority_e {
    PR_NONE,		/* 0 */
    PR_ENV_HOME,	/* 1 */
    PR_CFG_SITE,	/* 2 */
    PR_CFG_USER,	/* 3 */
    PR_CFG_UPDATE,	/* 4 */
    PR_ENV_BOGO,	/* 5 */
    PR_COMMAND		/* 6 */
} priority_t;

typedef enum bulk_e {
    B_NORMAL,
    B_CMDLINE,
    B_STDIN
} bulk_t;

#include "globals.h"

/* Represents the secondary data for a word key */
typedef struct {
    u_int32_t	good;
    u_int32_t	bad;
    u_int32_t	msgs_good;
    u_int32_t	msgs_bad;
} wordcnts_t;

typedef struct {
    wordcnts_t  cnts;
    double 	prob;
    int freq;
} wordprop_t;

#define internal_error do { fprintf(stderr, "Internal error in %s:%lu\n", __FILE__, (unsigned long)__LINE__); abort(); } while(0)

typedef enum e_wordlist_version {
    ORIGINAL_VERSION = 0,
    IP_PREFIX = 20040500
} t_wordlist_version;

#define	CURRENT_VERSION	IP_PREFIX

/* for bogoutil.c and datastore_db_trans.c */

typedef enum { M_NONE, M_DUMP, M_LOAD, M_WORD, M_MAINTAIN, M_ROBX, M_HIST,
    M_RECOVER, M_CRECOVER, M_PURGELOGS, M_VERIFY, M_REMOVEENV, M_CHECKPOINT }
    cmd_t;

#endif
