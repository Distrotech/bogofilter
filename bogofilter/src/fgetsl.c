/* $Id$ */

/*****************************************************************************

NAME:
   fgetsl.c -- fgets(), returning length.

******************************************************************************/

#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "fgetsl.h"

/* calls exit(2) on read error or when max_size < 2 */
int fgetsl(char *buf, int max_size, FILE *s)
{
    return xfgetsl(buf, max_size, s, 0);
}

int xfgetsl(char *buf, int max_size, FILE *s, int no_nul_terminate)
{
    int c = 0;
    char *cp = buf;
    int moreroom = no_nul_terminate ? 1 : 0;

    if (max_size < 2 - moreroom) {
	fprintf(stderr, "Invalid buffer size, exiting.\n");
	abort();
	exit(2);
    }

    if (feof(s))
	return(EOF);

    while (((--max_size > 0) || (no_nul_terminate && max_size == 0))
	    && ((c = getc(s)) != EOF)) {
	*cp++ = c;
	if (c == '\n')
	    break;
    }

    if (c == EOF && ferror(s)) {
	perror("stdin");
	exit(2);
    }

    if (!no_nul_terminate)
	*cp = '\0'; /* DO NOT ADD ++ HERE! */
    return(cp - buf);
}

#ifdef MAIN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xmalloc.h>

int main(int argc, char **argv) {
    char *buf;
    int size, count;

    if (argc != 3) {
	fprintf(stderr, "%s <size> <non_nul_terminate>\n", argv[0]);
	exit(2);
    }

    size = atoi(argv[1]);
    buf = xmalloc(size);
    while(1) {
	count = xfgetsl(buf, size, stdin, atoi(argv[2]));
	if (count == EOF) break;
	printf("%d ", count);
	if (count > 0) fwrite(buf, 1, count, stdout);
	putchar('\n');
    }
    xfree(buf);
    exit(0);
}
#endif
