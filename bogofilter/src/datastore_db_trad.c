/* $Id$ */

/*****************************************************************************

NAME:
datastore_db.c -- implements the datastore, using Berkeley DB

AUTHORS:
Gyepi Sam <gyepi@praxis-sw.com>   2002 - 2003
Matthias Andree <matthias.andree@gmx.de> 2003 - 2004

******************************************************************************/

/* To avoid header file conflicts the order is:
**	1. System header files
**	2. Header files for external packages
**	3. Bogofilter's header files
*/

/* This code has been tested with BerkeleyDB 3.1 3.2, 3.3, 4.0,
 * 4.1 and 4.2.  -- Matthias Andree, 2004-11-29 */

/* TODO:
 * - implement proper retry when our transaction is aborted after a
 *   deadlock
 * - document code changes
 * - conduct massive tests
 * - check if we really need the log files for "catastrophic recovery"
 *   or if we can remove them (see db_archive documentation)
 *   as the log files are *HUGE* even compared with the data base
 */

/*
 * NOTE: this code is an "#if" nightmare due to the many different APIs
 * in the many different BerkeleyDB versions.
 */

#define DONT_TYPEDEF_SSIZE_T 1
#include "common.h"

#include <db.h>

#include "datastore.h"
#include "datastore_db_private.h"
#include "datastore_db.h"
#include "datastore_dbcommon.h"

static DB_ENV	  *tra_get_env_dbe	(dbe_t *env);
static int	  tra_begin		(void *vhandle);
static int  	  tra_abort		(void *vhandle);
static int  	  tra_commit		(void *vhandle);

/* OO function lists */

dsm_t dsm_traditional = {
    &tra_get_env_dbe,
    &tra_begin,
    &tra_abort,
    &tra_commit,
};

DB_ENV *tra_get_env_dbe	(dbe_t *env)
{
    (void) env;
    return NULL;
}

int  tra_begin	(void *vhandle) { (void) vhandle; return 0; }
int  tra_abort	(void *vhandle) { (void) vhandle; return 0; }
int  tra_commit	(void *vhandle) { (void) vhandle; return 0; }
