#include "swap.h"

/* stolen from glibc's byteswap.c */
long swap_32bit(long x){
  return ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) |
          (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24));
}
