/* $Id$ */

/*
NAME:
   wordhash.c -- Implements a hash data structure.

AUTHOR:
   Gyepi Sam <gyepi@praxis-sw.com>

THEORY:
  See 'Programming Pearls' by Jon Bentley for a good treatment of word hashing.
  The algorithm, magic number selections, and hash function are based on
  his implementation.

  This module has been tuned to perform fast inserts and searches, using
  the following techniques:

  1. Insert operation allocates and returns a pointer to a memory
  buffer, in which the caller can store associated data, eliminating the
  need for further  retrieval or storage operations.

  2. Maintains a linked list of hash nodes in insert order for fast
  traversal of hash table.
*/

#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <stddef.h>	/* for offsetof */

#ifdef	ENABLE_DEPRECATED_CODE
#include "degen.h"
#endif
#include "wordhash.h"
#include "xmalloc.h"

#define NHASH 29989
#define MULT 31

/* Note:  every wordhash includes three large chunks of memory:
   20k - S_CHUNK -- 
   32k - N_CHUNK * sizeof (hashnode_t)
   120k - NHASH * sizeof (hashnode_t **)
*/

#define N_CHUNK 2000
#define S_CHUNK 20000

#define	WH_INIT	64
#define	WH_INCR	64

#ifndef offsetof
#define offsetof(type, member) ((size_t) &((type*)0)->member )
#endif

/* global variables */

/* for  bogotune */
wordhash_t *memory_db;

/* function prototypes */

void wh_trap(void);
void wh_trap(void) {}

/* function definitions */

/* 06/14/03
** profiling shows wordhash_new() is significant portion 
** of time spent processing msg-count files:
**
**   %   cumulative   self              self     total           
**  time   seconds   seconds    calls  ms/call  ms/call  name    
**  30.84      0.70     0.70     2535     0.28     0.28  wordhash_new
**
** wordhash_new() made faster by using xcalloc() to provide
** initialized storage.
*/

wordhash_t *
wordhash_init (wh_t t, uint c)
{
    wordhash_t *wh = xcalloc (1, sizeof (wordhash_t));

    wh->type = t;
    wh->size = (t == WH_NORMAL) ? 0 : ((c == 0) ? WH_INIT : c);

    if (t == WH_NORMAL)
	wh->bin = xcalloc (NHASH, sizeof (hashnode_t **));

    if (t == WH_CNTS)
	wh->cnts = (wordcnts_t *) xcalloc(wh->size, sizeof(wordcnts_t));

    if (t == WH_PROPS) {
	wh->freeable = true;
	wh->props = (hashnode_t *) xcalloc(wh->size, sizeof(hashnode_t));
    }

    return wh;
}

wordhash_t *
wordhash_new (void)
{
    wh_t t = (!fBogotune || !msg_count_file) ? WH_NORMAL : WH_CNTS;
    return wordhash_init(t, 0);
}

static void
wordhash_free_alloc_nodes (wordhash_t *wh)
{
    wh_alloc_node *node, *next;

    /* iteration pointers are inside buf,
    ** so freeing buf requires clearing them.
    */
    wh->iter_head = wh->iter_tail = NULL;

    for (node = wh->nodes; node; node = next)
    {
	next = node->next;
	xfree (node->buf);
	xfree (node);
    }
    wh->nodes = NULL;
}

static void
wordhash_free_hash_nodes (wordhash_t *wh)
{
    hashnode_t *item, *next;

    for (item = wh->iter_head; item != NULL ; item = next)
    {
	next = item->iter_next;
	word_free( item->key );
	item->key = NULL;
    }
    wh->iter_head = NULL;
}

static void
wordhash_free_strings (wordhash_t *wh)
{
    wh_alloc_str *sp, *sq;

    for (sp = wh->strings; sp; sp = sq)
    {
	sq = sp->next;
	xfree (sp->buf);
	xfree (sp);
    }
    wh->strings = NULL;
}

void
wordhash_free (wordhash_t *wh)
{
    if (wh == NULL)
	return;

    wordhash_free_hash_nodes(wh);
    wordhash_free_alloc_nodes(wh);
    wordhash_free_strings(wh);

    xfree (wh->order);
    if (wh->type == WH_CNTS)
	xfree (wh->cnts);
    if (wh->type == WH_PROPS) {
	if (wh->freeable) {
	    uint i;
	    for (i=0; i<wh->size; i++)
		xfree(wh->props[i].buf);
	}
	xfree (wh->props);
    }
    xfree (wh->bin);
    xfree (wh);
}

static hashnode_t *
nmalloc (wordhash_t *wh) /*@modifies wh->nodes@*/
{
    /*@dependent@*/ wh_alloc_node *wn = wh->nodes;
    hashnode_t *hn;

    if (wn == NULL || wn->avail == 0)
    {
	wn = xmalloc (sizeof (wh_alloc_node));
	wn->next = wh->nodes;
	wh->nodes = wn;

	wn->buf = xmalloc (N_CHUNK * sizeof (hashnode_t));
	wn->avail = N_CHUNK;
	wn->used = 0;
    }
    wn->avail--;
    hn = (wn->buf) + wn->used;
    wn->used ++;
    return (hn);
}

/* determine architecture's alignment boundary */
struct dummy { char *c; double d; };
#define ALIGNMENT offsetof(struct dummy, d)

static char *
smalloc (wordhash_t *wh, size_t n) /*@modifies wh->strings@*/
{
    wh_alloc_str *s = wh->strings;
    /*@dependent@*/ char *t;

    /* Force alignment on architecture's natural boundary.*/
    if ((n % ALIGNMENT) != 0)
	n += ALIGNMENT - ( n % ALIGNMENT);
   
    if (s == NULL || s->avail < n)
    {
	s = xmalloc (sizeof (wh_alloc_str));
	s->next = wh->strings;
	wh->strings = s;

	s->buf = xmalloc (S_CHUNK + n);
	s->avail = S_CHUNK + n;
	s->used = 0;
    }
    s->avail -= n;
    t = s->buf + s->used;
    s->used += n;
    return (t);
}

static unsigned int
hash (const word_t *t)
{
    unsigned int h = 0;
    size_t l;
    for (l=0; l<t->leng; l++)
	h = MULT * h + t->text[l];
    return h % NHASH;
}

static void display_node(hashnode_t *n, const char *str)
{
    wordprop_t *p = (wordprop_t *)n->buf;
    if (verbose > 2)
	printf( "%20.20s %5d %5d%s", n->key->text, (int)p->cnts.bad, (int)p->cnts.good, str);
}

/* this function accumulates the word frequencies from the src hash to
 * those of the dest hash
 */
void wordhash_add(wordhash_t *dest, wordhash_t *src, void (*initializer)(void *))
{
    wordprop_t *d;
    hashnode_t *s;

    uint count = dest->count + src->count;	/* use dest count as total */

    dest->count += src->count;

    if (verbose > 20) {
	printf( "%5lu  ", (unsigned long)dest->count);
	display_node(src->iter_head, "  ");
    }

    for (s = wordhash_first(src); s != NULL; s = wordhash_next(src)) {
	wordprop_t *p = (wordprop_t *)s->buf;
	word_t *key = s->key;
	if (key == NULL)
	    continue;
	d = wordhash_insert(dest, key, sizeof(wordprop_t), initializer);
	d->freq += p->freq;
	d->cnts.good += p->cnts.good;
	d->cnts.bad  += p->cnts.bad;
    }

    if (verbose > 200)
	display_node(dest->iter_head, "\n");

    dest->size = count;
}

void
wordhash_foreach (wordhash_t *wh, wh_foreach_t *hook, void *userdata)
{
    hashnode_t *hn;

    for (hn = wordhash_first(wh); hn != NULL; hn = wordhash_next(wh)) {
	(*hook)(hn->key, hn->buf, userdata);
    }

    return;
}

void *
wordhash_search_memory (const word_t *t)
{
    void *ans = NULL;
    if (memory_db)
	ans = wordhash_search(memory_db, t, 0);
    return ans;
}

void *
wordhash_search (const wordhash_t *wh, const word_t *t, unsigned int idx)
{
    hashnode_t *hn;

    if (idx == 0)
	idx = hash (t);

    for (hn = wh->bin[idx]; hn != NULL; hn = hn->next) {
	word_t *key = hn->key;
	if (key->leng == t->leng && memcmp (t->text, key->text, t->leng) == 0)
	    return hn->buf;
    }
    return NULL;
}

static void *
wordhash_standard_insert (wordhash_t *wh, word_t *t, size_t n, void (*initializer)(void *))
{
    hashnode_t *hn;
    unsigned int idx = hash (t);
    void *buf = wordhash_search(wh, t, idx);

    if (buf != NULL)
	return buf;

    wh->size += 1;
    hn = nmalloc (wh);
    hn->buf = smalloc (wh, n);
    if (initializer)
	initializer(hn->buf);
    else
	memset(hn->buf, '\0', n);

    hn->key = word_dup(t);

    hn->next = wh->bin[idx];
    wh->bin[idx] = hn;

    if (wh->iter_head == NULL){
	wh->iter_head = hn;
    }
    else {
	wh->iter_tail->iter_next = hn;
    }

    hn->iter_next = NULL;
    wh->iter_tail = hn;

    return hn->buf;
}

static void *
wordhash_counts_insert (wordhash_t *wh)
{
    wh->index += 1;
    if (wh->index == wh->size) {
	wh->size += WH_INCR;
	wh->cnts = (wordcnts_t *) xrealloc(wh->cnts, wh->size * sizeof(wordcnts_t));
    }
    return & wh->cnts[wh->index];
}

void *
wordhash_insert (wordhash_t *wh, word_t *t, size_t n, void (*initializer)(void *))
{
    void *v;
    if (wh->type == WH_CNTS)
	v = wordhash_counts_insert (wh);
    else
	v = wordhash_standard_insert (wh, t, n, initializer);
    return v;
}

size_t wordhash_count (wordhash_t *wh)
{
    return wh->size;
}

void *
wordhash_first (wordhash_t *wh)
{
    void *val = NULL;

    switch (wh->type) {
    case WH_NORMAL:
	val = wh->iter_ptr = wh->iter_head;
	break;
    case WH_ORDERED:
	wh->index = 0;
	val = wh->order[wh->index];
	break;
    case WH_PROPS:
	wh->index = 0;
	val = &wh->props[wh->index];
	break;
    case WH_CNTS:
	wh->index = 0;
	val = &wh->cnts[wh->index];
	break;
    }

    return val;
}

void *
wordhash_next (wordhash_t *wh)
{
    void *val = NULL;

    switch (wh->type) {
    case WH_NORMAL:
	if (wh->iter_ptr != NULL)
	    val = wh->iter_ptr = wh->iter_ptr->iter_next;
	break;
    case WH_ORDERED:
	if (++wh->index < wh->size)
	    val = wh->order[wh->index];
	break;
    case WH_PROPS:
	if (++wh->index < wh->size)
	    val = &wh->props[wh->index];
	break;
    case WH_CNTS:
	if (++wh->index < wh->size)
	    val = &wh->cnts[wh->index];
	break;
    }

    return val;
}

static int compare_hashnode_t(const void *const ihn1, const void *const ihn2)
{
    const hashnode_t *hn1 = *(const hashnode_t *const *)ihn1;
    const hashnode_t *hn2 = *(const hashnode_t *const *)ihn2;
    return word_cmp(hn1->key, hn2->key);
}

static wordcnts_t *wordhash_get_counts(wordhash_t *wh, hashnode_t *n)
{
    if (wh->cnts == NULL) {
	wordprop_t *p = (wordprop_t *)n->buf;
	wordcnts_t *c = &p->cnts;
	return c;
    }
    else {
	wordcnts_t *c = (wordcnts_t *) n;
	return c;
    }
}

void
wordhash_set_counts(wordhash_t *wh, int good, int bad)
{
    hashnode_t *n;

    for (n = wordhash_first(wh); n != NULL; n = wordhash_next(wh)) {
	wordcnts_t *c = wordhash_get_counts(wh, n);
	c->good += good;
	c->bad  += bad;
    }
}

void
wordhash_sort (wordhash_t *wh)
{
    hashnode_t *node;
    hashnode_t **order;

    if (wh->type != WH_NORMAL)
	return;

    if (wh->size == 0)
	return;

    if (msg_count_file)
	return;

    order = (hashnode_t **) xcalloc(wh->size, sizeof(hashnode_t *));

    wh->size = 0;
    for(node = wordhash_first(wh); node != NULL; node = wordhash_next(wh))
	order[wh->size++] = node;

    qsort(order, wh->size, sizeof(hashnode_t *), compare_hashnode_t);
    wh->order = order;

    wh->type = WH_ORDERED;

    return;
}

/* 
** convert_propslist_to_countlist() allocates a new wordhash_t struct
** to improve program locality and lessen need for swapping when
** processing lots of messages.
*/

wordhash_t *
convert_propslist_to_countlist(wordhash_t *whi)
{
    hashnode_t *node;
    wordhash_t *who = NULL;
    uint count = 0;

    if (whi->type == WH_CNTS)
	return whi;

    if (whi->type != WH_NORMAL &&
	whi->type != WH_PROPS) {
	fprintf(stderr, "convert_propslist_to_countlist() called with invalid input.\n");
	exit(EX_ERROR);
    }

    who = wordhash_init(WH_CNTS, whi->size);

    for(node = wordhash_first(whi); node != NULL; node = wordhash_next(whi)) {
	wordcnts_t *ci = wordhash_get_counts(whi, node);
	wordcnts_t *co = &who->cnts[count++];

	co->good = ci->good;
	co->bad  = ci->bad ;
    }

    return who;
}

wordhash_t *
convert_wordhash_to_propslist(wordhash_t *whi, wordhash_t *db)
{
    if (whi->type == WH_CNTS) {
	if (whi->index == whi->size && whi->count > whi->size) {
	    whi->size = whi->count;
	    whi->cnts = (wordcnts_t *) xrealloc(whi->cnts, whi->size * sizeof(wordcnts_t));
	}
	return whi;
    }
    else {
	wordhash_t *who = wordhash_init(WH_PROPS, whi->size);
	hashnode_t *node;

	who->count = 0;

	for(node = wordhash_first(whi); node != NULL; node = wordhash_next(whi)) {
	    wordprop_t *wp;
	    if (!msg_count_file && node->key != NULL) {
		who->freeable = false;
		wp = wordhash_insert(db, node->key, sizeof(wordprop_t), NULL);
	    }
	    else {
		wp = xcalloc(1, sizeof(wordprop_t));
		memcpy(wp, node->buf, sizeof(wordprop_t));
		if (!who->freeable)
		    wh_trap();
	    }
	    who->props[who->count].buf = wp;
	    xfree(node->key);
	    node->key = NULL;
	    who->count += 1;
	}

	return who;
    }
} 

#ifdef	ENABLE_DEPRECATED_CODE
void wordhash_degen(wordhash_t *wh, wordhash_t *db)
{
    if (degen_enabled) {
	hashnode_t *node;

	memory_db = db;
	degen_enabled = false;	/* Disable further recursion */

	for (node = wordhash_first(wh); node != NULL; node = wordhash_next(wh))
	{
	    wordprop_t *props = (wordprop_t *) node->buf;
	    wordcnts_t *cnts  = &props->cnts;
	    if (cnts->good == 0 && cnts->bad == 0) {
		word_t *token = node->key;
		props = wordhash_search_memory(token);
		if (props) {
		    cnts->good = props->cnts.good;
		    cnts->bad  = props->cnts.bad;
		}
		if (cnts->good == 0 && cnts->bad == 0)
		    degen(token, cnts);
	    }
	}
	memory_db = NULL;
	degen_enabled = true;	/* Enable further recursion */
    }

    return;
}
#endif
