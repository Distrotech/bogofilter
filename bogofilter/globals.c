#include "system.h"
#include "globals.h"
#include "method.h"

/* exports */

bool nonspam_exits_zero;		/* '-e' */
bool force;				/* '-F' */
bool fisher;				/* '-f' */
bool logflag;				/* '-l' */
bool replace_nonascii_characters;	/* '-n' */
bool passthrough;			/* '-p' */
bool quiet;				/* '-q' */
bool terse;				/* '-t' */
int verbose;				/* '-v' */

FILE *fpin;				/* '-I' */

const	char *stats_prefix;
char *update_dir = NULL;
char *directory = NULL;
method_t *method = NULL;

int max_repeats;
double min_dev;
double spam_cutoff;
double thresh_stats;
int Rtable = 0;		/* '-R' */

