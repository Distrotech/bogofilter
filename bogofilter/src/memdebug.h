/* $Id$ */

/*
* NAME:
*    memdebug.h -- header file for memdebug.c
*
*/

#ifndef MEMDEBUG_H
#define MEMDEBUG_H

#undef	MEMDISPLAY
#define	MEMDISPLAY	memdisplay(__FILE__, __LINE__)

extern int memtrace;

void *md_calloc(size_t nmemb, size_t size);
void *md_malloc(size_t size);
void *md_realloc(void *ptr, size_t size);
void  md_free(void *ptr);
void  memdisplay(const char *file, int lineno);

enum { M_MALLOC = 1, 
       M_FREE = 2 
};

#ifndef	NO_MEMDEBUG_MACROS
#define	malloc	md_malloc
#define	calloc	md_calloc
#define	realloc	md_realloc
#define	free	md_free
#endif

#endif
