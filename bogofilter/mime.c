/* $Id$ */

/*****************************************************************************

NAME:
   mime.c -- lexer mime processing

NOTES:

    RFC2045:

       Header fields occur in at least two contexts:

	(1)   As part of a regular RFC 822 message header.

	(2)   In a MIME body part header within a multipart construct.

AUTHOR:
   Matthias Andree <matthias.andree@gmx.de>
   David Relson <relson@osagesoftware.com>

******************************************************************************/

#include <ctype.h>

#include <config.h>
#include "common.h"

#include "base64.h"
#include "lexer.h"
#include "mime.h"
#include "qp.h"
#include "uudecode.h"
#include "xstrdup.h"
#include "xmalloc.h"

/* Global Variables */

int stackp = 0;

struct msg_state msg_stack[MIME_STACK_MAX];
struct msg_state *msg_state = msg_stack;

struct type_s { 
    enum mimetype type;
    const char *name; 
    size_t len;
} types[] = { 
    { MIME_TEXT,	"text/",	 5, },
    { MIME_MULTIPART,	"multipart/", 	10, },
    { MIME_MESSAGE,	"message/", 	 8, },
};

struct encoding_s { 
    enum mimeencoding encoding;
    const char *name; 
} encodings[] = {
    { MIME_7BIT,	"7BIT",	  		},
    { MIME_8BIT,	"8BIT",	  		},
    { MIME_BINARY,	"BINARY", 		},
    { MIME_QP,		"QUOTED-PRINTABLE",	},
    { MIME_BASE64,	"BASE64", 		},
    { MIME_UUENCODE,	"X-UUENCODE", 		},
};

struct disposition_s { 
    enum mimedisposition disposition;
    const char *name; 
} dispositions[] = { 
    { MIME_INLINE,	"inline",     },
    { MIME_ATTACHMENT,	"attachment", },
};

/* Function Prototypes */

static char *skipws(char *t, char *e);
static char *getword(char *t, char *e);
#if	0
static char *getparam(char *t, char *e, const char *param);
#endif

void msg_state_init(bool header, char *boundary, size_t len);

/* Function Definitions */

void msg_state_init(bool header, char *boundary, size_t len)
{
    msg_state->mime_header = header;
    msg_state->mime_mail = false;
    msg_state->mime_type = MIME_TEXT;
    msg_state->mime_encoding = MIME_7BIT;
    msg_state->boundary = boundary;
    msg_state->boundary_len = len;
}

void resetmsgstate(struct msg_state *ms, int new)
{
    while (new > 0) {
	struct msg_state *t = ms + new;
	if (t->boundary)
	    xfree(t->boundary);
	if (t->charset)
	    xfree(t->charset);
	if (t->version)
	    xfree(t->version);
	new -= 1;
    }
    msg_state = ms;
    msg_state_init(false, NULL, 0);
    ms->charset = xstrdup("US-ASCII");
}

/* skips whitespace, returns NULL when ran into end of string */
char *skipws(char *t, char *e)
{
    while(t < e && isspace((byte)*t))
	t++;
    if (t < e) return t;
    return NULL;
}

/* skips [ws]";"[ws] */
#if	0
char *skipsemi(char *t, char *e)
{
    if (!(t = skipws(t, e))) return NULL;
    if (*t == ';') t++;
    return skipws(t, e);
}
#endif

/* get next MIME word, NULL when none found.
 * caller must free returned string with xfree() */
char *getword(char *t, char *e)
{
    int quote = 0;
    int l;
    char *ts;
    char *n;

    t = skipws(t, e);
    if (!t) return NULL;
    if (*t == '"') {
	quote++;
	t++;
    }
    ts = t;
    while ((t < e) && (quote ? *t != '"' : !isspace((byte)*t))) {
	t++;
    }
    l = t - ts;
    n = xmalloc(l+1);
    memcpy(n, ts, l);
    n[l] = '\0';
    return n;
}

#if	0
char *getparam(char *t, char *e, const char *param)
{
//    char *w, *u;

    return NULL; /* NOT YET IMPLEMENTED */
}
#endif

void mime_version(void)
{
    size_t l = strlen("MIME-Version:");
    char *w = getword(yytext+l, yytext + yyleng);
    msg_state->mime_mail = true;
    msg_state->mime_header = true;
    msg_state->version = w;
}

void mime_disposition(void)
{
    size_t l = strlen("Content-Disposition:");
    char *w = getword(yytext+l, yytext + yyleng);
    struct disposition_s *dis;
    for (dis = dispositions ; dis < dispositions+COUNTOF(dispositions); dis+= 1) {
	if (strcasecmp(w, dis->name) == 0) {
	    msg_state->mime_disposition = dis->disposition;
	    if (DEBUG_MIME(1)) fprintf(stderr, "*** mime_disposition: %s\n", yytext);
	}
    }
    xfree(w);
}

/*********
**
** RFC2045, Section 6.1.  Content-Transfer-Encoding Syntax
**
**     encoding := "Content-Transfer-Encoding" ":" mechanism
**
**     mechanism := "7bit" / "8bit" / "binary" /
**                  "quoted-printable" / "base64" /
**                  ietf-token / x-token
**
*********/

void mime_encoding(void)
{
    size_t l = strlen("Content-Transfer-Encoding:");
    char *w = getword(yytext+l, yytext + yyleng);
    struct encoding_s *enc;
    for (enc = encodings ; enc < encodings+COUNTOF(encodings); enc+= 1) {
	if (strcasecmp(w, enc->name) == 0) {
	    msg_state->mime_encoding = enc->encoding;
	    if (DEBUG_MIME(1)) fprintf(stderr, "*** mime_encoding: %s\n", yytext);
	}
    }
    xfree(w);
    return;
}

enum mimetype get_mime_type(void)
{
    return msg_state->mime_type;
}

void mime_type(void)
{
    size_t l = strlen("Content-Type:");
    char *w = getword(yytext+l, yytext + yyleng);
    struct type_s *typ;

    if (!w) return;

    msg_state->mime_type = MIME_OTHER;
    for (typ = types ; typ < types+COUNTOF(types); typ+= 1) {
	if (strncasecmp(w, typ->name, typ->len) == 0) {
	    msg_state->mime_type = typ->type;
	    if (DEBUG_MIME(1)) fprintf(stderr, "*** mime_type: %s\n", yytext);
	}
    }
    xfree(w);

    switch(msg_state->mime_type) {
    case MIME_TEXT:
	/* XXX: read charset */
	return;
    case MIME_OTHER:
	return;
    case MIME_MULTIPART:
	return;
    case MIME_MESSAGE:
	/* XXX: read boundary */
	return;
    }
    /* XXX: incomplete */
    return;
}

void mime_boundary(void)
{
    char *boundary;
    size_t len;

    if (DEBUG_MIME(1))
	fprintf(stderr, "*** enter_mime_boundary: %d  '%-.*s'\n", stackp, yyleng, yytext);

    if (yytext[0] != '-' || yytext[1] != '-' ) {
	len = strlen("boundary=");
	boundary = getword(yytext+len, yytext + yyleng);
	msg_state_init(true, boundary, strlen(boundary));
    } else {			/* verify that it's really a boundary line */
	boundary = msg_state->boundary;
	if (boundary == NULL )
	    return;
	len = msg_state->boundary_len;
	/* XXX FIXME: recover from missing end boundaries? */
	if (memcmp(yytext+2, boundary, len) == 0) {
	    msg_state = &msg_stack[stackp];
	    memset(msg_state, 0, sizeof(*msg_state));
	    msg_state_init(true, boundary, len);
	}
    }

    if (DEBUG_MIME(1))
	fprintf(stderr, "*** mime_boundary: %d  %p '%s'\n", stackp, boundary, boundary);
    return;
}

size_t mime_decode(char *buff, size_t size)
{
    size_t count = size;

    if (DEBUG_MIME(3)) fprintf(stderr, "*** mime_decode %d \"%s\"\n", size, buff);
    if (msg_state->mime_header)	/* do nothing if in header */
	return count;

    /* Don't try to decode boundary */
    if (size > 2 && buff[0] == '-' && buff[1] == '-') {

	if (DEBUG_MIME(3)) fprintf(stderr, "*** try_boundar %d \"%s\" == %d \"%s\"\n", size, buff+2, msg_state->boundary_len, msg_state->boundary);

	if (memcmp(msg_state->boundary, buff+2, msg_state->boundary_len) == 0) {
	    msg_state->mime_header  = 1;
	    msg_state->mime_type  = MIME_TEXT;
	    msg_state->mime_encoding  = MIME_7BIT;
	    return count;
	}
    }

    switch(msg_state->mime_encoding) {
    case MIME_7BIT:
	/* do nothing */
	break;
    case MIME_8BIT:
	/* do nothing */
	break;
    case MIME_BINARY:
	/* do nothing */
	break;
    case MIME_QP:
	count = qp_decode(buff, size);
	break;
    case MIME_BASE64:
	if (size > 4) count = base64_decode(buff, size);
	break;
    case MIME_UUENCODE:
	count = uudecode(buff, size);
	break;
    default:
	fprintf( stderr, "Unknown mime encoding - %d\n", msg_state->mime_encoding);
	exit(2);
	break;
    }
    return count;
}
