/* $Id$ */

/*****************************************************************************

NAME:
   mime.h -- prototypes and definitions for mime.c

******************************************************************************/

#ifndef	HAVE_MIME_H
#define	HAVE_MIME_H

#define MIME_STACK_MAX 100

enum mimetype { MIME_TYPE_UNKNOWN, MIME_MULTIPART, MIME_MESSAGE, MIME_TEXT, MIME_TEXT_PLAIN, MIME_TEXT_HTML, MIME_APPLICATION };
enum mimeencoding { MIME_ENCODING_UNKNOWN, MIME_7BIT, MIME_8BIT, MIME_BINARY, MIME_QP, MIME_BASE64, MIME_UUENCODE };
enum mimedisposition { MIME_DISPOSITION_UNKNOWN, MIME_ATTACHMENT, MIME_INLINE };

struct msg_state {
    char *charset;
    char   *boundary;	/* only valid if mime_type is MIME_MULTIPART or MIME_MESSAGE */
    size_t boundary_len;
    char  *nxt_boundary;/* only valid if mime_type is MIME_MULTIPART or MIME_MESSAGE */
    size_t nxt_boundary_len;
    char *version;
    bool mime_header;
    bool mime_mail;
    enum mimetype mime_type;
    enum mimeencoding mime_encoding;
    enum mimedisposition mime_disposition;
};

extern int stackp;
extern struct msg_state *msg_state;
extern struct msg_state msg_stack[MIME_STACK_MAX];

void reset_msg_state(struct msg_state *ms, int new);
void mime_boundary_set(const char *text, int leng);
void mime_boundary_chk(const char *text, int leng);
void mime_disposition(const char *text, int leng);
void mime_encoding(const char *text, int leng);
void mime_type(const char *text, int leng);
enum mimetype get_mime_type(void);
void mime_version(const char *text, int leng);
size_t mime_decode(char *buff, size_t size);

#endif	/* HAVE_MIME_H */
