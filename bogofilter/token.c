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

bool block_on_subnets = false;

static token_t save_class = NONE;
static char save_text[256];

token_t get_token(void)
{
    token_t class;
    unsigned char *cp;
    char *t;

    /* If saved IPADDR, truncate last octet */
    if ( block_on_subnets && save_class == IPADDR )
    {
	t = strrchr(save_text, '.');
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

    while ((class = yylex()) > 0) {
	if (DEBUG_LEXER(3)) fprintf(stderr, "*** yylex: %d %d %-.*s\n", class, yyleng, yyleng, yytext);
	/* don't return boundary tokens to the user */
	if (class == BOUNDARY && yyleng >= 4)
	    continue;

	/* ignore anything when not reading text MIME types */
	if (class == TOKEN && mime_lexer && stackp > 0 && msg_state->mime_type != MIME_TEXT)
	    continue;
	
	if (class == IPADDR && block_on_subnets)
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
