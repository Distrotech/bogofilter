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

#include <stdlib.h>
#include <string.h>

/* for offsetof */
#include <stddef.h>

#include "common.h"

#include "wordhash.h"
#include "xmalloc.h"

#define NHASH 29989
#define MULT 31

#define N_CHUNK 2000
#define S_CHUNK 20000

#ifndef offsetof
#define offsetof(type, member) ( (int) & ((type*)0) -> member )
#endif

void wordhash_free_1 (wordhash_t * h);
void wordhash_free_2 (wordhash_t * h);

wordhash_t *
wordhash_init (void)
{
  int i;

  wordhash_t *h = xmalloc (sizeof (wordhash_t));

  h->bin = xmalloc (NHASH * sizeof (hashnode_t **));

  for (i = 0; i < NHASH; i++)
      h->bin[i] = NULL;

  h->nodes = NULL;
  h->strings = NULL;

  h->iter_ptr = NULL;
  h->iter_head = NULL;
  h->iter_tail = NULL;

  h->index = 0;
  h->count = 0;
  h->order = NULL;
  h->wordcount = 0;

  return h;
}

void
wordhash_free (wordhash_t * h)
{
    hashnode_t *p, *q;
    wh_alloc_str *sp, *sq;
    wh_alloc_node *np, *nq;

    if (h == NULL)
	return;

    for (p = h->iter_head; p != NULL ; p = q)
    {
	q = p->iter_next;
	word_free( p->key );
    }

    for (np = h->nodes; np; np = nq)
    {
	nq = np->next;
	xfree (np->buf);
	xfree (np);
    }

    for (sp = h->strings; sp; sp = sq)
    {
	sq = sp->next;
	xfree (sp->buf);
	xfree (sp);
    }

    if (h->order != NULL) {
	xfree (h->order);
    }

    xfree (h->bin);
    xfree (h);
}

static hashnode_t *
nmalloc (wordhash_t * h) /*@modifies h->nodes@*/
{
    /*@dependent@*/ wh_alloc_node *n = h->nodes;
    hashnode_t *t;

    if (n == NULL || n->avail == 0)
    {
	n = xmalloc (sizeof (wh_alloc_node));
	n->next = h->nodes;
	h->nodes = n;

	n->buf = xmalloc (N_CHUNK * sizeof (hashnode_t));
	n->avail = N_CHUNK;
	n->used = 0;
    }
    n->avail--;
    t = (n->buf) + n->used;
    n->used ++;
    return (t);
}

/* determine architecture's alignment boundary */
struct dummy { char *c; double d; };
#define ALIGNMENT offsetof(struct dummy, d)

static char *
smalloc (wordhash_t * h, size_t n) /*@modifies h->strings@*/
{
    wh_alloc_str *s = h->strings;
    /*@dependent@*/ char *t;

    /* Force alignment on architecture's natural boundary.*/
    if ((n % ALIGNMENT) != 0)
	n += ALIGNMENT - ( n % ALIGNMENT);
   
    if (s == NULL || s->avail < n)
    {
	s = xmalloc (sizeof (wh_alloc_str));
	s->next = h->strings;
	h->strings = s;

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
hash (word_t *t)
{
    unsigned int h = 0;
    size_t l;
    for (l=0; l<t->leng; l++)
	h = MULT * h + t->text[l];
    return h % NHASH;
}

void *
wordhash_insert (wordhash_t * h, word_t *t, size_t n, void (*initializer)(void *))
{
    hashnode_t *p;
    unsigned int idx = hash (t);

    for (p = h->bin[idx]; p != NULL; p = p->next) {
	word_t *key = p->key;
	if (key->leng == t->leng && memcmp (t->text, key->text, t->leng) == 0)
	{
	    return p->buf;
	}
    }

    h->count += 1;
    p = nmalloc (h);
    p->buf = smalloc (h, n);
    if (initializer)
	initializer(p->buf);
		  
    p->key = word_dup(t);
    p->next = h->bin[idx];
    h->bin[idx] = p;

    if (h->iter_head == NULL){
	h->iter_head = p;
    }
    else {
	h->iter_tail->iter_next = p;
    }

    p->iter_next = NULL;
    h->iter_tail = p;

    return p->buf;
}

size_t wordhash_count (wordhash_t * h)
{
    return h->count;
}

hashnode_t *
wordhash_first (wordhash_t * h)
{
    hashnode_t *val = NULL;

    if (h->order) {
	val = h->order[h->index = 0];
    }
    else {
	val = h->iter_ptr = h->iter_head;
    }
    return val;
}

hashnode_t *
wordhash_next (wordhash_t * h)
{
    hashnode_t *val = NULL;

    if (h->order) {
	if (++h->index < h->count) {
	    val = h->order[h->index];
	}
    }
    else {
	if (h->iter_ptr != NULL) {
	    val = h->iter_ptr = h->iter_ptr->iter_next;
	}
    }
    return val;
}

static int compare_hashnode_t(const void *const ihn1, const void *const ihn2)
{
    const hashnode_t *hn1 = *(const hashnode_t *const *)ihn1;
    const hashnode_t *hn2 = *(const hashnode_t *const *)ihn2;
    return word_cmp(hn1->key, hn2->key);
}

void
wordhash_sort (wordhash_t * h)
{
    hashnode_t *node;
    hashnode_t **order;

    if (h->count == 0 || h->order != NULL)
	return;

    order = (hashnode_t **) xcalloc(h->count, sizeof(hashnode_t *));

    h->count = 0;
    for(node = wordhash_first(h); node != NULL; node = wordhash_next(h))
	order[h->count++] = node;

    qsort(order, h->count, sizeof(hashnode_t *), compare_hashnode_t);
    h->order = order;
}
