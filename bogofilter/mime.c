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
    { MIME_TEXT_HTML,	"text/html",	 9, },
    { MIME_TEXT_PLAIN,	"text/plain",	10, },
    { MIME_TEXT,	"text/",	 5, },
    { MIME_APPLICATION,	"application/", 12, },
    { MIME_MESSAGE,	"message/", 	 8, },
    { MIME_MULTIPART,	"multipart/", 	10, },
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

static const char *skipws(const char *t, const char *e);
static char *getword(const char *t, const char *e);
#if	0
static char *getparam(const char *t, char *e, const char *param);
#endif

static void msg_state_init(bool header, char *boundary, size_t len);
static bool check_boundary(const char *text, size_t text_len, const char *save_boundary, size_t save_len);

/* Function Definitions */

void msg_state_init(bool header, char *boundary, size_t len)
{
    msg_state->mime_header = header;
    msg_state->mime_mail = false;
    msg_state->mime_type = MIME_TEXT;
/*  msg_state->mime_encoding = MIME_7BIT; */
    msg_state->boundary     = NULL;
    msg_state->boundary_len = 0;
    msg_state->nxt_boundary = boundary;
    msg_state->nxt_boundary_len = len;
}

void reset_msg_state(struct msg_state *ms, int new)
{
    while (new > 0) {
	struct msg_state *t = ms + new;
	xfree(t->nxt_boundary);
	xfree(t->charset);
	xfree(t->version);
	new -= 1;
    }
    msg_state = ms;
    msg_state_init(false, NULL, 0);
    ms->charset = xstrdup("US-ASCII");
}

/* skips whitespace, returns NULL when ran into end of string */
const char *skipws(const char *t, const char *e)
{
    while(t < e && isspace((byte)*t))
	t++;
    if (t < e) return t;
    return NULL;
}

/* skips [ws]";"[ws] */
#if	0
char *skipsemi(const char *t, const char *e)
{
    if (!(t = skipws(t, e))) return NULL;
    if (*t == ';') t++;
    return skipws(t, e);
}
#endif

/* get next MIME word, NULL when none found.
 * caller must free returned string with xfree() */
char *getword(const char *t, const char *e)
{
    int quote = 0;
    int l;
    const char *ts;
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
/*    char *w, *u; */

    return NULL; /* NOT YET IMPLEMENTED */
}
#endif

void mime_version(const char *text, int leng)
{
    size_t l = strlen("MIME-Version:");
    char *w = getword(text+l, text + leng);
    msg_state->mime_mail = true;
    msg_state->mime_header = true;
    msg_state->version = w;
}

void mime_disposition(const char *text, int leng)
{
    size_t l = strlen("Content-Disposition:");
    char *w = getword(text+l, text + leng);
    struct disposition_s *dis;
    msg_state->mime_disposition = MIME_DISPOSITION_UNKNOWN;
    for (dis = dispositions ; dis < dispositions+COUNTOF(dispositions); dis+= 1) {
	if (strcasecmp(w, dis->name) == 0) {
	    msg_state->mime_disposition = dis->disposition;
	    if (DEBUG_MIME(1)) fprintf(stdout, "*** mime_disposition: %s\n", text);
	    break;
	}
    }
    if (msg_state->mime_disposition == MIME_DISPOSITION_UNKNOWN)
	fprintf(stderr, "Unknown mime disposition - '%s'\n", w);
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

void mime_encoding(const char *text, int leng)
{
    size_t l = strlen("Content-Transfer-Encoding:");
    char *w = getword(text+l, text + leng);
    struct encoding_s *enc;
    msg_state->mime_encoding = MIME_ENCODING_UNKNOWN;
    for (enc = encodings ; enc < encodings+COUNTOF(encodings); enc+= 1) {
	if (strcasecmp(w, enc->name) == 0) {
	    msg_state->mime_encoding = enc->encoding;
	    if (DEBUG_MIME(1)) fprintf(stdout, "*** mime_encoding: %s\n", text);
	    break;
	}
    }
    xfree(w);
    return;
}

enum mimetype get_mime_type(void)
{
    return msg_state->mime_type;
}

void mime_type(const char *text, int leng)
{
    size_t l = strlen("Content-Type:");
    char *w = getword(text+l, text + leng);
    struct type_s *typ;

    if (!w) return;

    msg_state->mime_type = MIME_TYPE_UNKNOWN;
    for (typ = types ; typ < types+COUNTOF(types); typ+= 1) {
	if (strncasecmp(w, typ->name, typ->len) == 0) {
	    msg_state->mime_type = typ->type;
	    if (DEBUG_MIME(1)) fprintf(stdout, "*** mime_type: %s\n", text);
	    break;
	}
    }
    if (msg_state->mime_type == MIME_TYPE_UNKNOWN)
	fprintf(stderr, "Unknown mime type - '%s'\n", w);
    xfree(w);

    switch(msg_state->mime_type) {
    case MIME_TEXT:
    case MIME_TEXT_HTML:
    case MIME_TEXT_PLAIN:
	/* XXX: read charset */
	return;
    case MIME_TYPE_UNKNOWN:
	return;
    case MIME_MULTIPART:
	return;
    case MIME_MESSAGE:
	/* XXX: read boundary */
	return;
    case MIME_APPLICATION:
	/* XXX: read boundary */
	return;
    }
    return;
}

void mime_boundary_set(const char *text, int leng)
{
    char *boundary;
    size_t len;

    if (DEBUG_MIME(1))
	fprintf(stdout, "*** --> mime_boundary_set: %d  %p '%-.*s'\n", stackp, text, leng, text);

    /* get boundary marker for next level */
    len = strlen("boundary=");
    boundary = getword(text+len, text + leng);
    msg_state->nxt_boundary     = boundary;
    msg_state->nxt_boundary_len = strlen(boundary);

    if (DEBUG_MIME(1))
	fprintf(stdout, "*** <-- mime_boundary_set: %d  %p '%s'\n", stackp, boundary, boundary);

    return;
}

bool check_boundary(const char *text, size_t text_len, const char *save_boundary, size_t save_len)
{
    if (text[0] == '-' && text[1] == '-'
	&& text_len >= save_len + 2
	&& memcmp(text+2, save_boundary, save_len) == 0)
	return true;
    else
	return false;
}

void mime_boundary_chk(const char *text, int leng)
{
    size_t len = leng;

    if (DEBUG_MIME(1))
	fprintf(stdout, "*** --> mime_boundary_chk: %d  %p '%-.*s'\n", stackp, text, (int)len, text);

    if (msg_state->boundary == NULL && msg_state->nxt_boundary == NULL )
	return;

    /* XXX FIXME: recover from missing end boundaries? */

    /* If encountering a boundary for the first time,
    ** increment the stack and start a new stack frame
    */
    if (check_boundary(text, len, msg_state->nxt_boundary, msg_state->nxt_boundary_len)) {
	struct msg_state *cur = &msg_stack[stackp];
	struct msg_state *new = &msg_stack[++stackp];
	memcpy(new, cur, sizeof(*new));
	/* set boundary values for new stack frame */
	new->boundary     = cur->nxt_boundary;
	new->boundary_len = cur->nxt_boundary_len;
	new->nxt_boundary     = NULL;
	new->nxt_boundary_len = 0;
	msg_state = new;
    }

    /* If encountering a boundary after the first time,
    ** reinitialize the current stack frame
    */
    if (check_boundary(text, len, msg_state->boundary, msg_state->boundary_len)) {
	struct msg_state *cur = &msg_stack[stackp];
	struct msg_state *prv = &msg_stack[stackp-1];
	memcpy(cur, prv, sizeof(*cur));
	/* set boundary values for cur stack frame */
	cur->boundary     = prv->nxt_boundary;
	cur->boundary_len = prv->nxt_boundary_len;
	cur->nxt_boundary     = NULL;
	cur->nxt_boundary_len = 0;
	if (len == msg_state->boundary_len + 4 && text[len-2] == '-' && text[len-1] == '-')
	    stackp -= 1;
    }

    /* If encountering an boundary,
    ** pop the current stack frame
    */
    if (DEBUG_MIME(1))
	fprintf(stdout, "*** <-- mime_boundary_chk: %d  %p '%s'\n", stackp, msg_state->boundary, msg_state->boundary);

    return;
}

size_t mime_decode(char *buff, size_t size)
{
    size_t count = size;

    if (DEBUG_MIME(3)) fprintf(stdout, "*** mime_decode %d \"%-.*s\"\n", size, (int)size-1, buff);

    if (msg_state->mime_header)	/* do nothing if in header */
	return count;

    /* Don't try to decode boundary */
    if (size > 2 && buff[0] == '-' && buff[1] == '-') {

	if (DEBUG_MIME(3)) fprintf(stdout, "*** try_boundar %d \"%s\" == %d \"%s\"\n", size, buff+2, msg_state->boundary_len, msg_state->boundary);

	if (memcmp(msg_state->boundary, buff+2, msg_state->boundary_len) == 0) {
	    msg_state->mime_header = 1;
	    msg_state->mime_type = MIME_TEXT;
	    msg_state->mime_encoding = MIME_7BIT;
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
	count = qp_decode((unsigned char *)buff, size);
	break;
    case MIME_BASE64:
	if (size > 4) count = base64_decode((unsigned char *)buff, size);
	break;
    case MIME_UUENCODE:
	count = uudecode((unsigned char *)buff, size);
	break;
    case MIME_ENCODING_UNKNOWN:
	break;
    default:
	fprintf(stderr, "Unknown mime encoding - %d\n", msg_state->mime_encoding);
	exit(2);
	break;
    }
    return count;
}
