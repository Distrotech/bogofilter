/* $Id$ */

/*  constants and declarations for wordlists_base.c */

#ifndef	WORDLISTS_CORE_H
#define	WORDLISTS_CORE_H

#ifndef	DATASTORE_H
typedef void *dsh_t;
#endif

extern	bool	config_setup;

typedef enum e_WL_TYPE {
    WL_REGULAR =	'R',	/**< list contains regular tokens */
    WL_IGNORE  =	'I'	/**< list contains tokens that are skipped */
} WL_TYPE;

/** type of a wordlist node */
typedef struct wordlist_s wordlist_t;
/** structure of a wordlist node */
struct wordlist_s
{
    /*@null@*/ wordlist_t *next;/**< link to next queue node */
    /*@owned@*/ char *listname;	/**< resource name (for debug/verbose messages) */
    /*@owned@*/ char *filepath;	/**< resource path (for debug/verbose messages) */
    /*@owned@*/ dsh_t *dsh;	/**< datastore handle */
    u_int32_t	msgcount[IX_SIZE];	/**< count of messages in wordlist. */
    WL_TYPE	type;		/**< datastore type */
    int		override;	/**< priority in queue */
};

/** Initialize a wordlist node and insert into the right place of the
 * priority queue, \return -1 for error, 0 for success */
void init_wordlist(const char* name, const char* path,
		   int override, WL_TYPE type);

void display_wordlists(const char *fmt);

void free_wordlists(void);

/** Get default wordlist for registering messages, finding robx, etc */
wordlist_t * default_wordlist(void);

int  set_wordlist_dir(const char* dir, priority_t precedence);

#endif	/* WORDLISTS_CORE_H */
