/* $Id$ */

/*****************************************************************************

NAME:
   token.c -- post-lexer token processing

   12/08/02 - split out from lexer.l

AUTHOR:
   David Relson <relson@osagesoftware.com>

******************************************************************************/

#include <stdlib.h>

#include <config.h>
#include "common.h"

#include "charset.h"
#include "error.h"
#include "token.h"
#include "mime.h"

typedef enum {
    LEXER_HEAD,
    LEXER_TEXT,
    LEXER_HTML,
} lexer_state_t;

static char *yytext;
static int yyleng;

bool block_on_subnets = false;

static token_t save_class = NONE;
static char save_text[256];

int html_tag_level = 0;
int html_comment_level = 0;

lexer_state_t lexer_state = LEXER_HEAD;

void html_tag(int level)
{
    html_tag_level += level;
    if (html_tag_level < 0)
	html_tag_level = 0;
}

void html_comment(int level)
{
    html_comment_level += level;
    if (html_comment_level < 0)
	html_comment_level = 0;
}

token_t get_token(void)
{
    token_t class = NONE;
    unsigned char *cp;

    /* If saved IPADDR, truncate last octet */
    if ( block_on_subnets && save_class == IPADDR )
    {
	char *t = strrchr(save_text, '.');
	if (t == NULL)
	    save_class = NONE;
	else
	{
	    *t = '\0';	
	    yylval = save_text;
	    yyleng = strlen(save_text);
	    return save_class;
	}
    }

    while (true) {
	switch (lexer_state) {
	case LEXER_HEAD: 
 	    class  = lexer_lex();
	    yyleng = lexer_leng;
	    yytext = lexer_text;
	    break;
	case LEXER_TEXT: 
	    class  = text_plain_lex();
	    yyleng = text_plain_leng;
	    yytext = text_plain_text;
	    break;
	case LEXER_HTML: 
	    class  = text_html_lex();
	    yyleng = text_html_leng;
	    yytext = text_html_text;
	    break;
	}

	if (class <= 0)
	    break;

	switch (class) {

	case EMPTY:	/* empty line -- ignore */
	    continue;

	case BOUNDARY:	/* don't return boundary tokens to the user */
	    lexer_state = LEXER_HEAD;
	    if (yyleng >= 4)
		continue;

	case TOKEN:	/* ignore anything when not reading text MIME types */
	    if (html_tag_level > 0 || html_comment_level > 0)
		continue;
	    if (mime_lexer && stackp > 0)
		switch (msg_state->mime_type) {
		case MIME_TEXT:
		case MIME_TEXT_HTML:
		case MIME_TEXT_PLAIN:
		    break;
		default:
		    continue;
		}
	    break;

	case IPADDR:
	    if (!block_on_subnets)
		break;
	    else
	    {
		const char *prefix="url:";
		size_t len = strlen(prefix);
		size_t avl = sizeof(save_text);
		yylval = save_text;
		save_class = IPADDR;
		avl -= strlcpy( yylval, "url:", avl);
		yyleng = strlcpy( yylval+len, yytext, avl);
		return (class);
	    }
	}

	/* eat all long words */
	if (yyleng <= MAXTOKENLEN)
	    break;
    }

    /* Need separate loop so lexer can see "From", "Date", etc */
    for (cp = (unsigned char *)yytext; *cp; cp++)
	*cp = casefold_table[(unsigned char)*cp];

    yylval = yytext;
    return(class);
}

token_t got_from(const char *text)
{
    if (memcmp(text, "From ", 5) != 0 ) 
	return(TOKEN);
    else { 
	msg_header = 1; 
	lexer_state = LEXER_HEAD;
	if (mime_lexer) {
	    stackp = 0;
	    reset_msg_state(&msg_stack[stackp], 0); 
	} 
	return(FROM);
    }
}

#define	DEBUG	0

#if	!DEBUG

#define	change_lexer_state(new) lexer_state = new

#else

const char *state_name(lexer_state_t new);
void change_lexer_state(lexer_state_t new);

const char *state_name(lexer_state_t state)
{
    switch(state) {
    case LEXER_TEXT: return "TEXT";
    case LEXER_HTML: return "HTML";
    } 
    return "unknown";
}

void change_lexer_state(lexer_state_t new)
{
    if (lexer_state != new)
	fprintf(stdout, "lexer_state: %s -> %s\n", state_name(lexer_state), state_name(new));
    lexer_state = new;
    return;
}
#endif

void got_newline()
{
    if (!msg_header && !msg_state->mime_header)
	return;

    msg_header = msg_state->mime_header = false;
    
    switch (msg_state->mime_type) {

    case MIME_TEXT:
	change_lexer_state(LEXER_TEXT);
	break;

    case MIME_TEXT_PLAIN:
	change_lexer_state(LEXER_TEXT);
	break;

    case MIME_TEXT_HTML:
	change_lexer_state(LEXER_HTML);
	break;

    default:
	change_lexer_state(LEXER_TEXT);
    } 

    return;
}
