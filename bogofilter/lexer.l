/* $Id$ */

%{
/*
 * NAME
 *   lexer.l -- bogofilter's lexical analyzer
 *
 * ORIG:	[A-Za-z$][A-Za-z0-9$'.-]+[A-Za-z0-9$]		{return(TOKEN);}
 * ADAMS:	[^[:blank:]\n[:digit:][:punct:]][^][:blank:]<>;=():&%$#@!+|/\\{}^\"?\*,\n[]+[^[:blank:][:punct:]\n]	{return(TOKEN);}
*/
#include <stdlib.h>
#include <ctype.h>

#include <config.h>
#include "common.h"

#include "charset.h"
#include "error.h"
#include "lexer.h"
#include "mime.h"
#include "fgetsl.h"
#include "textblock.h"
#include "xmalloc.h"
#include "xstrdup.h"

/*
 * Our lexical analysis is different from Paul Graham's rules: 
 *
 * We throw away headers that are readily identifiable as dates.
 * We throw away text lines that look like BASE64 encoding.
 * We throw away all digit strings that don't look like IP address parts.
 * We thow away lines beginning with <tab>id<space> -- mailer UDs.
 * We throw away various bits of MIME cruft (things that look like
 * declarations and boundaries for multipart declarations).
 * We throw away *all* tokens of length 1 or 2.
 *
 * These are optimizations to keep the token lists from bloating.
 * The big win is recognizing machine-generated unique IDs that
 * we'll never see again and shouldn't 
 *
 * We don't treat dot between two alphanumerics as a separator,
 * because we want to keep domain names and IP addresses together as 
 * recognizable units. 
 *
 * Having done the above, there isn't much need to recognize URLs.  
 * If a URL is a spam indicator, very likely any other URL from the
 * same site is as well, so the hostname part should be an adequate
 * statistical trigger.  
 *
 * The list of HTML keywords and attributes to be ignored is from the 4.0 DTD.
 */

extern char *spam_header_name;

static int msg_header = 1;
static int yyinput(char *buf, int max_size);
static int yyredo(char del);

bool mime_lexer = true;

static int lineno;

#define YY_INPUT(buf,result,max_size) result = yyinput(buf, max_size)

#define YY_DECL token_t yylex(void)

char *yylval;

%}

%option align nounput noyywrap noreject 8bit caseless

UINT8		([01]?[0-9]?[0-9]|2([0-4][0-9]|5[0-5]))
IPADDR		{UINT8}\.{UINT8}\.{UINT8}\.{UINT8}
BCHARS		[0-9a-zA-Z\'()+_,-./:=?]
BCHARSNOSPC	[0-9a-zA-Z'()+_,-./:=?]
MIME_BOUNDARY	{BCHARS}*{BCHARSNOSPC}

ID		[0-9a-zA-Z-]*
MTYPE		[ \t]*[0-9a-zA-Z/-]*
TOKEN		[^[:blank:][:cntrl:][:digit:][:punct:]][^][:blank:]<>;=():&%$#@!+|/\\{}^\"\?\*,[:cntrl:][]+[^[:blank:][:punct:][:cntrl:]]

%%

abbr	;
above	;
acronym	;
accesskey	;
align	;
all	;
alt	;
alink	;
applet	;
archive	;
axis	;
basefont	;
baseline	;
below	;
bgcolor	;
big	;
body	;
border	;
bottom	;
box	;
button	;
cellpadding	;
cellspacing	;
center	;
char	;
charoff	;
charset=\"?{ID}\"?				{ got_charset(yytext); yyredo('='); }
circle	;
cite	;
colspan	;
coords	;
class	;
classid	;
clear	;
codebase	;
codetype	;
color	;
cols	;
compact	;
content	;
datetime	;
declare	;
defer	;
data	;
default	;
dfn	;
dir	;
disabled	;
face	;
font	;
frameborder	;
groups	;
head	;
headers	;
height	;
href	;
hreflang	;
hsides	;
http-equiv	;
hspace	;
iframe	;
input	;
img	;
ismap	;
justify	;
kbd	;
label	;
lang	;
language	;
left	;
lhs	;
link	;
longdesc	;
map	;
marginheight	;
marginwidth	;
media	;
meta	;
middle	;
multiple	;
name	;
nohref	;
none	;
noresize	;
noshade	;
nowrap	;
object	;
onblur	;
onchange	;
onclick	;
ondblclick	;
onfocus	;
onmousedown	;
onmouseup	;
onmouseover	;
onmousemove	;
onmouseout	;
onkeypress	;
onkeydown	;
onkeyup		;
onload	;
onselect	;
onunload	;
param	;
poly	;
profile	;
prompt	;
readonly	;
rect	;
rel	;
rev	;
rhs	;
right	;
rows	;
rowspan	;
rules	;
samp	;
scheme	;
scope	;
script	;
scrolling	;
select	;
selected	;
shape	;
size	;
small	;
span	;
src	;
standby	;
strike	;
strong	;
style	;
sub	;
summary	;
sup	;
tabindex	;
table	;
target	;
textarea	;
title	;
top	;
type	;
usemap	;
valign	;
value	;
valuetype	;
var	;
vlink	;
vsides	;
void	;
vspace	;
width	;
\<\!--	;
-->	;

^MIME-Version:.*				{ if (mime_lexer) { mime_version(); yyredo(':'); } }
^Content-Transfer-Encoding:{MTYPE}		{ if (mime_lexer) { mime_encoding(); yyredo(':'); } }
^Content-Type:{MTYPE}				{ if (mime_lexer) { mime_type(); yyredo(':'); } }
^Content-Disposition:{MTYPE}			{ if (mime_lexer) { mime_disposition(); yyredo(':'); } }
^From\ 						{ if (memcmp(yytext, "From ",5) == 0 ) { 
						      msg_header = 1; 
						      if (mime_lexer) {
							  stackp = 0;
							  reset_msg_state(&msg_stack[stackp], 0); 
						      } 
						      return(FROM);
						  } else { 
						      return(TOKEN);
						  } }
^Date:.*|Delivery-Date:.*			;
^Message-ID:.*					;
^\tid\ {ID}					;

E?SMTP\ id\ {ID}				;
boundary=.*					{ if (mime_lexer) { set_mime_boundary(); }}
name=\"?					;
filename=\"?					;
^--{MIME_BOUNDARY}(--)?$			{ if (mime_lexer) { chk_mime_boundary(); return (BOUNDARY); }}

{IPADDR}					{return(IPADDR);}
{TOKEN}						{return(TOKEN);}
.						;
^\n						{ msg_header = 0;  if (mime_lexer) { msg_stack[stackp].mime_header = 0; /* eat token */ }}
\n						;

%%

static char *yysave = NULL;

static int yygetline(char *buf, int max_size)
/* input getter for the scanner */
{
    int count;

    static size_t hdrlen = 0;
    if (hdrlen==0)
	hdrlen=strlen(spam_header_name);

    if (yysave == NULL) {
	count = fgetsl(buf, max_size, yyin);

	/* Also, save the text on a linked list of lines.
	 * Note that we store fixed-length blocks here, not lines.
	 * One very long physical line could break up into more
	 * than one of these. */
	if (passthrough)
	    textblock_add(textblocks, buf, count);
	lineno += 1;
	if (DEBUG_LEXER(0)) fprintf(stdout, "### %2d %d %s", lineno, msg_header, buf);
    }
    else {
	count = strlcpy(buf, yysave, max_size-2);
	buf[count++] = '\n';
	buf[count] = '\0';
	xfree(yysave);
	yysave = NULL;
    }

    /* skip spam_header ("X-Bogosity:") lines */
    while (msg_header
	   && count != -1
	   && memcmp(buf,spam_header_name,hdrlen) == 0)
    {
	do {
	    count = fgetsl(buf, max_size, yyin);
	    if (passthrough)
		textblock_add(textblocks, buf, count);
	    lineno += 1;
	    if (DEBUG_LEXER(0)) fprintf(stdout, "*** %2d %d %s\n", lineno, msg_header, buf);

	    if (count != -1 && *buf == '\n')
		break;
	} while (count != -1 && isspace((unsigned char)*buf));
    }

/* unfolding:
** 	causes "^\tid" to be treated as continuation of previous line
** 	hence doesn't match lexer pattern which specifies beginning of line
*/
    while (0 && msg_header) {
	int c = fgetc(yyin);
	if (c == EOF)
	    break;
	if (c == ' ' || c == '\t') {
	    int add;
	    /* continuation line */
	    ungetc(c,yyin);
	    if (buf[count - 1] == '\n') count --;
	    add = fgetsl(buf + count, max_size - count, yyin);
	    if (add == EOF) break;
	    if (passthrough)
		textblock_add(textblocks, buf+count, add);
	    lineno += 1;
	    if (DEBUG_LEXER(1)) fprintf(stdout, "*** %2d %d %s\n", lineno, msg_header, buf+count);
	    count += add;
	} else {
	    ungetc(c,yyin);
	    break;
	}
    }

    if (count == -1) {
	if (ferror(yyin)) {
	    print_error(__FILE__, __LINE__, "input in flex scanner failed\n");
	    exit(2);
	} else {
	    return YY_NULL;
	}
    }

    if (0) { /* debug */
	fprintf(stderr, "%d: ", count);
	fwrite(buf, 1, count, stderr);
	fprintf(stderr, "\n");
    }

    /* \r\n -> \n */
    if (count >= 2 && 0 == strcmp(buf + count - 2, "\r\n")) {
	count --;
	*(buf + count - 1) = '\n';
    }

    return count;
}

static int yyinput(char *buf, int max_size)
/* input getter for the scanner */
{
    int i, count, decoded_count;

    count = yygetline(buf, max_size);

    if ((count > 0) && !msg_header){

	decoded_count = mime_decode(buf, count);

	/*change buffer size only if the decoding worked */
	if (decoded_count != 0)
	    count = decoded_count;
    }

    for (i = 0; i < count; i++ )
    {	
	unsigned char ch = buf[i];
	buf[i] = charset_table[ch];
    }

    return (count == -1 ? 0 : count);
}

static int yyredo(char del)
{
    char *t;
    if (yysave)
	xfree(yysave);
    yysave = xstrdup(yytext);
    t = strchr(yysave, del);
    if (t != NULL)
	*t = ' ';

    return 1;
}

/*
 * The following sets edit modes for GNU EMACS
 * Local Variables:
 * mode:c
 * End:
 */
