/**
 * \file fgetsl.c Implementation file for NUL-proof fgets() replacement.
 * Also usable as standalone test code when compiled with -DMAIN.
 * \author Matthias Andree
 * \date 2002,2003
 */

#include "common.h"

#include <stdlib.h>

#include "fgetsl.h"

/* calls exit(EX_ERROR) on read error or when max_size < 2 */
int fgetsl(char *buf, int max_size, FILE *in)
{
    return xfgetsl(buf, max_size, in, 0);
}

int xfgetsl(char *buf, int max_size, FILE *in, int no_nul_terminate)
{
    int c = 0;
    char *cp = buf;
    char *end = buf + max_size;				/* Physical end of buffer */
    char *fin = end - (no_nul_terminate ? 0 : 1);	/* Last available byte    */

    if (cp >= fin) {
	fprintf(stderr, "Invalid buffer size, exiting.\n");
	abort();
    }

    if (in == NULL || feof(in))
	return(EOF);

    while ((cp < fin) && ((c = getc(in)) != EOF)) {
	*cp++ = (char)c;
	if (c == '\n')
	    break;
    }

    if (c == EOF && ferror(in)) {
	perror("stdin");
	exit(EX_ERROR);
    }

    if (cp < end)
	*cp = '\0'; /* DO NOT ADD ++ HERE! */
    if (cp == buf && feof(in)) return EOF;
    return(cp - buf);
}

#ifdef MAIN
#include <string.h>
#include <xmalloc.h>

int main(int argc, char **argv) {
    char *buf;
    int size, count, non_nul_terminate;

    if (argc != 3) {
	fprintf(stderr, "%s <size> <non_nul_terminate>\n", argv[0]);
	exit(EX_ERROR);
    }

    non_nul_terminate = atoi(argv[2]);
    size = atoi(argv[1]);
    buf = xmalloc(size);
    while(1) {
	count = xfgetsl(buf, size, stdin, non_nul_terminate);
	if (count == EOF) break;
	printf("%d ", count);
	if (count > 0) fwrite((char *)buf, 1, count, stdout);
	if (count > 0 && !non_nul_terminate && buf[count] != '\0') {
	                printf(" (NUL terminator missing)");
	}

	putchar('\n');
    }
    xfree(buf);
    exit(EXIT_SUCCESS);
}
#endif
