/* $Id$ */

%{
/*
 * NAME
 *   lexer_v3.l -- bogofilter's lexical analyzer for message headers
 *
 *   01/01/2003 - split out of lexer.l
 *
*/

/*
 * Our lexical analysis is different from Paul Graham's rules: 
 *
 * We throw away headers that are readily identifiable as dates.
 * We throw away all digit strings that don't look like IP address parts.
 * We thow away lines beginning with <tab>id<space> -- mailer UDs.
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
 * LEXED_TOKENS, which are found in "msg-count" files need a special pattern
 * because they can be:
 *	1 - normal bogofilter tokens
 *	2 - url:xxx and subj: tokens
 *	3 - mime boundaries
 */

/* 12 May 2003
 * Added Paul Graham's latest ideas on parsing.
 * (From http://www.paulgraham.com/better.html)
 *
 * 1. Case is preserved.
 *
 * 2. Exclamation points are constituent characters.
 *
 * 3. Periods and commas are constituents if they occur between two
 *    digits. This lets me get ip addresses and prices intact.
 *
 * 4. A price range like $20-25 yields two tokens, $20 and $25.
 *
 * 5. Tokens that occur within the To, From, Subject, and Return-Path
 *    lines, or within urls, get marked accordingly.
 *    For example. "foo" in the Subject line becomes "subj:foo".
*/

/* DR 08/29/2003:
**
** With flex-2.5.31 and '%option never-interactive noreject', file
** msg.dr.0118.base64 (in tests/bogofilter/inputs/split.d) parses
** incorrectly because line 24 isn't base64 decoded.
*/

#include "common.h"

#include <ctype.h>
#include <stdlib.h>

#include "buff.h"
#include "charset.h"
#include "lexer.h"
#include "mime.h"		/* for mime_*() */
#include "msgcounts.h"
#include "textblock.h"
#include "token.h"
#include "xmalloc.h"

#define YY_DECL token_t yylex(void)
    YY_DECL;			/* declare function */

#define YY_INPUT(buf,result,max_size) result = yyinput((byte *)buf, result, max_size)
#define YY_EXIT_FAILURE EX_ERROR

#undef	stderr
#define	stderr	dbgout		/* for debug & -D options */

static word_t yyt;
static int lineno;

/* Function Prototypes */

static word_t *yy_text(void);
static void html_char(void);
static void html_reorder(void);

static void url_char(void);

static void skip_to(char chr);
static void yy_unput(const byte *txt, uint len);

char yy_get_state(void);
void yy_set_state_initial(void);

/* Function Definitions */

static word_t *yy_text(void)
{
    yyt.text = (byte *)yytext;
    yyt.leng = yyleng;
    return &yyt;
}

%}

%option warn
%option nodebug debug
%option align caseless 8bit
%option never-interactive
%option noreject noyywrap

UINT8		([01]?[0-9]?[0-9]|2([0-4][0-9]|5[0-5]))
IPADDR		{UINT8}\.{UINT8}\.{UINT8}\.{UINT8}
BCHARSNOSPC	[[:alnum:]()+_,-./:=?#\']
BCHARS		[[:alnum:]()+_,-./:=?#\' ]
MIME_BOUNDARY	{BCHARS}*{BCHARSNOSPC}

ID		<?[[:alnum:]-]*>?
CHARSET		[[:alnum:]-]+
VERPID		[[:alnum:]#-]+[[:digit:]]+[[:alnum:]#-]+
MTYPE		[[:blank:]]*[[:alnum:]/-]*

NUM		[[:digit:]]+
NUM_NUM		\ {NUM}\ {NUM}
MSG_COUNT	^\".MSG_COUNT\"

TOKENFRONT	[^[:blank:][:cntrl:][:digit:][:punct:]]
TOKENMID	[^[:blank:][:cntrl:]<>;=():&%$#@+|/\\{}^\"?*,\[\]]+
BOGOLEX_TOKEN	[^[:blank:][:cntrl:]<>;    &%  @ |/\\{}^\" *,\[\]]+
TOKENBACK	[^[:blank:][:cntrl:]<>;=():&%$#@+|/\\{}^\"?*,\[\]._~\'\`\-]

TOKEN		{TOKENFRONT}{TOKENMID}{TOKENBACK}

T12		[[:alpha:]][[:alnum:]]?
TOKEN_12 	({TOKEN}|{T12})

/*  RFC2047.2
    encoded-word = "=?" charset "?" encoding "?" encoded-text "?="
    charset = token    ; see section 3
    encoding = token   ; see section 4
    token = 1*<Any CHAR except SPACE, CTLs, and especials>
    especials = "(" / ")" / "<" / ">" / "@" / "," / ";" / ":" / "
		<"> / "/" / "[" / "]" / "?" / "." / "="
    encoded-text = 1*<Any printable ASCII character other than "?"
		      or SPACE>
		   ; (but see "Use of encoded-words in message
		   ; headers", section 5)
*/

/* 09/01/03
  Using "[^?]" in the pattern and validating the charset in 'C'
  reduces executable size by approx 290k.
  new: ENCODED_WORD =\?{CHARSET}\?[bq]\?[^?]*\?=
  old: ENCODED_WORD =\?{CHARSET}\?(b\?{BASE64}|q\?{QP})\?=

  BASE64	[0-9a-zA-Z/+=]+
  QP		[!->@-~]+
*/

WHITESPACE	[[:blank:]\n]
NOTWHITESPACE	[^[:blank:]\n]

HTML_ENCODING	"&#"x?[[:xdigit:]]+";"
URL_ENCODING	"%"[[:xdigit:]][[:xdigit:]]

ENCODED_WORD	=\?{CHARSET}\?[bq]\?[^?]*\?=
ENCODED_TOKEN	({TOKENFRONT}{TOKENMID})?({ENCODED_WORD}{WHITESPACE}+)*{ENCODED_WORD}

/*
HTML_WI_COMMENTS	"<"[^>]*">"
HTML_WO_COMMENTS	"<"[^!][^>]*">"|"<>"
*/

HTMLTOKEN		"<"[^>]*">"

/*
 * Generally, there are some html tags that cause an "eyebreak" and some
 * that do not. For example, the "P" tag or the "BR" tag cause a break,
 * and can be interpreted in place, while, the B (bold) tag does not.
 * No close tags seem to cause a break.
 * Comments do not.  This is an attempt to make an exhaustive list of
 * tags that cause an "eyebreak". When the exit tag also causes a break,
 * we include the /?. I believe this to be a complete list of tags that
 * can cause a formatting break.
 */

HBREAK		p|br|li|h[1-6]|hr|title|table|center|dd|dt|iframe|img|input|select|td|textarea|th|\/?(div|blockquote|pre|dir|dl|fieldset|legend|form|menu|ol|ul)

BREAKHTML	"<"({HBREAK}({WHITESPACE}[^>]*|""))">"

VERP		{TOKEN}-{VERPID}-{TOKEN}={TOKEN}@{TOKEN}

%s TEXT HTML BOGO_LEX
%s HTOKEN HDISCARD SCOMMENT LCOMMENT
%s PGP_HEAD PGP_BODY

%%

<INITIAL,BOGO_LEX>{MSG_COUNT}{NUM_NUM}		{ if (lineno == 0) { 
						      BEGIN BOGO_LEX; 
						      set_msg_counts_from_str(strchr(yytext, ' ') + 1); 
						  }
						  return MSG_COUNT_LINE;
						}
<BOGO_LEX>^\"{BOGOLEX_TOKEN}\"{NUM_NUM}		{ return BOGO_LEX_LINE; }
<BOGO_LEX>\n					{ lineno += 1; }

<INITIAL>{ENCODED_TOKEN}			{ word_t *w = yy_text();
						  yy_unput(w->text, text_decode(w));
						}

<INITIAL>^(To|CC|From|Return-Path|Subject|Received):	{ set_tag(yytext); }
<INITIAL>^Content-(Transfer-Encoding|Type|Disposition):{MTYPE}	{ mime_content(yy_text()); skip_to(':'); return TOKEN; }

<INITIAL>^(Delivery-)?Date:.*			{ return HEADKEY; }
<INITIAL>^(Resent-)?Message-ID:.*		{ return HEADKEY; }
<INITIAL>^(In-Reply-To|References):.* 		{ return HEADKEY; }

<INITIAL>boundary=[ ]*\"?{MIME_BOUNDARY}\"?	{ mime_boundary_set(yy_text()); }
<INITIAL>charset=\"?{CHARSET}\"?		{ got_charset(yytext); skip_to('='); return TOKEN; }

<INITIAL>(file)?name=\"?			/* ignore */
<INITIAL>\n?[[:blank:]]id\ {ID}			/* ignore */

<INITIAL>\n[[:blank:]]				{ lineno += 1; }
<INITIAL>\n\n					{ enum mimetype type = get_content_type();
						  have_body = true;
						  msg_header = false;
						  clr_tag();
						  switch (type) { 
						  case MIME_TEXT_HTML:	BEGIN HTML; break;
						  case MIME_MESSAGE:	yy_set_state_initial(); break;
						  default:		BEGIN TEXT; 
						  }
						  return EOH;
						}

<INITIAL>\n					{ set_tag("Header"); lineno += 1; }
<INITIAL>{VERP} 				{ skip_to('='); return VERP; }

^-----BEGIN\ PGP\ SIGNATURE-----$		{ BEGIN PGP_HEAD;
						  yy_unput((byte *)yytext, yyleng);
						}
<PGP_HEAD>^\n					{ BEGIN PGP_BODY; }
<PGP_BODY>{TOKEN}				{ /* ignore */ }
^-----END\ PGP\ SIGNATURE-----$ 		{ BEGIN TEXT;
						  yy_unput((byte *)yytext, yyleng);
						}
^--{MIME_BOUNDARY}(--)?$			{ if (got_mime_boundary(yy_text())) {
						      yy_set_state_initial();
						      return BOUNDARY;
						  } else {
						      yyless(2);
						  }
						}

  /* This has to match just as much or more than the below rules, so as to be the 
     controlling rule. */
<HTML>{TOKEN}({HTMLTOKEN}*{BREAKHTML}+{HTMLTOKEN}*.?|({HTMLTOKEN})+{WHITESPACE})		{ 
    			char *chr = memchr(yytext, '<', yyleng);	/* find start of html tag */
			size_t len = chr - yytext;
			yyless(len);
			return TOKEN;
			}

<HTML>{TOKEN_12}({HTMLTOKEN})+/{NOTWHITESPACE}	{ html_reorder(); }

<HTML>"<!--"					{ BEGIN SCOMMENT; }
<HTML>"<!"					{ BEGIN LCOMMENT; }
<HTML>"<"(a|img|font){WHITESPACE}		{ BEGIN HTOKEN; }
<HTML>"<"					{ BEGIN HDISCARD; }	/* unknown tag */

<HTOKEN>{TOKEN}					{ return TOKEN; }
<HDISCARD,LCOMMENT,SCOMMENT>{TOKEN}		{ /* discard innards of html tokens and comments */ }

<HTOKEN,HDISCARD,LCOMMENT>">"			{ BEGIN HTML; }	/* end of tag, loose comment; return to normal html processing */
<SCOMMENT>"-->"					{ BEGIN HTML; }	/* end of strict comment; return to normal html processing */
"<"\!DOCTYPE\ HTML\ PUBLIC\ .*">" 		{ BEGIN HTML; }

{IPADDR}					{ return IPADDR;}
{TOKEN}						{ return TOKEN;}

<HTML>{TOKEN_12}?{HTML_ENCODING}		{ html_char(); }	/* process escaped chars, eg '&#101;' is 'a' */
<HTOKEN>"/"[^/[:blank:]]*{URL_ENCODING}+	{ url_char(); }		/* process escaped chars, eg '%61'    is 'a' */

\${NUM}(\.{NUM})?				{ return TOKEN;}	/* Dollars and cents */

.						/* ignore character */
\n						{ lineno += 1; clr_tag(); }
<<EOF>>						{ return NONE; }
%%

void lexer_v3_init(FILE *fp)
{
    lineno = 0;
    have_body = false;
    yy_set_state_initial();
    yyrestart(fp);
}

static void skip_to(char chr)
{
    size_t len = strchr(yytext, chr) - yytext;
    yyless(len);
}

static void html_reorder(void)
{
    char *chr = memchr(yytext, '<', yyleng);	/* find start of html tag */
    size_t len = chr - yytext;
    char *yycopy = xmalloc(yyleng + 1); 	/* +1 for NUL byte below */

    memcpy(yycopy, yytext+len, yyleng-len);	/* copy tag to start of buffer */
    memcpy(yycopy+yyleng-len, yytext, len);	/* copy leading text to end of buffer */
    yycopy[yyleng] = '\0';			/* for debugging */

    yy_unput((byte *)yycopy, yyleng);
    xfree(yycopy);
}

static int xtoi(char *in, size_t len)
{
    int val = 0;
    while (isxdigit((byte) *in) && (len-- > 0)) {
	char c = *in++;
	val <<= 4;
	val += isdigit((byte)c)
	    ? (c - '0')
	    : (tolower((byte)c) - 'a' + 10);
    }
    return val;
}

static void html_char(void)
{
    char *txt = strstr(yytext, "&#");	/* find decodable char */
    size_t len = txt - yytext;
    int  val;
    char *yycopy = NULL;

    if (len != 0) {
	yycopy = xmalloc(yyleng + 1);	/* +1 for NUL byte below */
	memcpy(yycopy, yytext, yyleng);	/* copy tag to start of buffer */
	yycopy[yyleng] = '\0';		/* for debugging */
    }

    txt += 2;
    val = isdigit((byte) *txt) ? atoi(txt) : xtoi(txt+1, 4);

    /* xtoi() limits conversion to 4 characters */
    /* atoi() limits value to 0x7fffffff, i.e. 2147483647 */
    /* no problem on linux */

    if ((val > 0) && (val < 256) && 
	isprint(val)) {			/* use it if printable */
	yyunput(val, yytext);
	yyleng = len;			/* adjust len to pre-char count */
    }
    else {
	if (yycopy)
	    yycopy[yyleng-1] = ' ';	/* prevents parsing loop */
    }

    if (yycopy != NULL) {
	yy_unput((byte *)yycopy, yyleng);
	xfree(yycopy);
    }
}

static void url_char(void)
{
    char *src, *dst;
    src = dst = yytext;

    while (src < yytext + yyleng) {
	char c = *src++;
	if (c == '%') {
	    c = xtoi(src, 2);
	    src += 2;
	}
	*dst++ = c;
    }
    while (dst > yytext) {
	yyunput(*--dst, yytext);
    }
}

static void yy_unput(const byte *txt, uint len)
{
    while (len-- > 0)
	unput(txt[len]);
}

char yy_get_state()
{
    switch (YYSTATE) {
    case INITIAL:  return 'i';
    case TEXT:     return 't';
    case HTML:
    case HTOKEN:   return 'h';
    case SCOMMENT: return 's';
    case LCOMMENT: return 'l';
    default:       return 'o';
    }
}

void yy_set_state_initial(void)
{
    BEGIN INITIAL;
    msg_header = true;
    set_tag("Header");

    if (DEBUG_LEXER(1))
	fprintf(dbgout, "BEGIN INITIAL\n");

#ifdef	FLEX_DEBUG
    yy_flex_debug = BOGOTEST('L');
#endif
}

/*
 * The following sets edit modes for GNU EMACS
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * End:
 */