/* $Id$ */

/*****************************************************************************

NAME:
   lexer.h -- prototypes and definitions for lexer.c

******************************************************************************/

#ifndef	LEXER_H
#define	LEXER_H

/* length of *yylval will not exceed this... */
#define MAXTOKENLEN	30

extern FILE *yyin;
extern int yyleng;
extern char *yytext;
extern char *yylval;

extern bool mime_lexer;
extern bool block_on_subnets;

/* lexer interface */
typedef enum {
    NONE = 0,
    TOKEN,	/* regular token */
    FROM,	/* mbox message delimiter */
    BOUNDARY,	/* MIME multipart boundary line */
    IPADDR	/* ip address */
} token_t;

extern token_t yylex(void);

#endif	/* LEXER_H */
