/* $Id$ */

/*
* NAME:
*    xrealloc.c -- front-end to standard heap manipulation routines, with error checking.
*
* AUTHOR:
*    Gyepi Sam <gyepi@praxis-sw.com>
*
*/

#include <stdlib.h>

#include "config.h"

#define	 BF_MALLOC
#include "xmalloc.h"

void
*xrealloc(void *ptr, size_t size){
   ptr = bf_realloc(ptr, size);
   if (ptr == NULL && size == 0)
       ptr = bf_calloc(1, 1);
   if (ptr == NULL)
       xmem_error("xrealloc");
   return ptr;
}
