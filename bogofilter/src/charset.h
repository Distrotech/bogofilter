/* $Id$ */

/*****************************************************************************

NAME:
   charset.h -- constants and declarations for charset.c

AUTHOR:
   David Relson <relson@osagesoftware.com>

******************************************************************************/

#ifndef	CHARSET_H
#define	CHARSET_H

extern const char *charset_default;
extern bool replace_nonascii_characters;

extern byte charset_table[256];
extern byte casefold_table[256];
extern void got_charset(const char *);
extern void set_charset(const char *charset);
extern void init_charset_table(const char *charset_name, bool use_default);

#ifdef	CP866
extern int  decode_and_htmlUNICODE_to_cp866(byte *buf, int len);
#endif

#endif /* CHARSET_H */
