/* $Id$ */

/*****************************************************************************

NAME:
   lexer.h -- prototypes and definitions for lexer.c

******************************************************************************/

#ifndef	LEXER_H
#define	LEXER_H

#include "buff.h"
#include "word.h"

extern FILE *yyin;

extern	bool	block_on_subnets;

extern	bool	kill_html_comments;
extern	int	count_html_comments;
extern	bool	score_html_comments;

#define YY_NULL 0

/* lexer interface */
typedef enum {
    NONE,
    TOKEN,	/* regular token */
    EMPTY,	/* empty line */
    BOUNDARY,	/* MIME multipart boundary line */
    IPADDR,	/* ip address */
    MSG_COUNT_LINE,
    BOGO_LEX_LINE
} token_t;

/* in lexer.c */
extern int yylineno;
extern bool msg_header;

/* Define a struct for interfacing to a lexer */

typedef token_t yylex_t(void);

typedef struct lexer_s {
    yylex_t  *yylex;
    char    **yytext;
    int      *yyleng;
} lexer_t;

extern lexer_t *lexer;

extern lexer_t v3_lexer;
extern lexer_t msg_count_lexer;

/* in lexer_v3.l */
extern token_t	lexer_v3_lex(void);
extern int	lexer_v3_leng;
extern char   * lexer_v3_text;
extern void	lexer_v3_init(FILE *fp);

/* in lexer_v4.l */
extern token_t	lexer_v4_lex(void);
extern int	lexer_v4_leng;
extern char   * lexer_v4_text;
extern void	lexer_v4_init(FILE *fp);

/* in lexer.c */
extern void	yyinit(void);
extern int	yyinput(byte *buf, size_t size);

extern int	buff_fill(buff_t *buff, size_t used, size_t need);

extern size_t	decode_text(word_t *w);

extern bool	is_eol(const char *buf, size_t cnt);
extern void	convert_eol(char *buf, char chr);

#endif	/* LEXER_H */
