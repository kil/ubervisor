/*
 * Copyright (c) 2011 Kilian Klimek <kilian.klimek@googlemail.com>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 * 
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/queue.h>

#include "misc.h"
#include "uvhash.h"

static inline uint32_t
hash_uint(uint32_t x)
{
	return x * 2654435761;
}


static inline uint32_t
hash_str(char *x)
{
#if 0
	uint32_t	i,
			ret,
			len = strlen(x);

	ret = len;
	for (i = 0; i < len; i++, x++)
		ret = (ret << 5) ^ (ret >> 27) ^ (unsigned char) *x;

	return ret;
#endif
	uint32_t	hash = 5381;
	int		c;

	while ((c = *x++))
		hash = ((hash << 5) + hash) ^ c;
	return hash;
}

uvhash_t *
uvhash_new(int size)
{
	uvhash_t			*ret;
	int				i;

	ret = xmalloc(sizeof(uvhash_t));
	ret->h_size = size;
	ret->h_buckets = xmalloc(sizeof(struct uvhash_ent_list *) * size);

	for (i = 0; i < size; i++) {
		ret->h_buckets[i] = xmalloc(sizeof(struct uvhash_ent_list));
		LIST_INIT(ret->h_buckets[i]);
	}

	return ret;
}

void
uvhash_insert(uvhash_t *h, uint32_t k, void *v)
{
	struct uvhash_ent_list		*l;
	struct uvhash_ent_s		*ent;

	ent = xmalloc(sizeof(struct uvhash_ent_s));
	ent->he_key = k;
	ent->he_value = v;

	l = h->h_buckets[hash_uint(k) % h->h_size];
	LIST_INSERT_HEAD(l, ent, he_ent);
}

void *
uvhash_find(uvhash_t *h, uint32_t k)
{
	struct uvhash_ent_list		*l;
	struct uvhash_ent_s		*ent;

	l = h->h_buckets[hash_uint(k) % h->h_size];
	LIST_FOREACH (ent, l, he_ent) {
		if (ent->he_key == k)
			return ent->he_value;
	}
	return NULL;
}

void
uvhash_remove(uvhash_t *h, uint32_t k)
{
	struct uvhash_ent_list		*l;
	struct uvhash_ent_s		*ent;

	l = h->h_buckets[hash_uint(k) % h->h_size];
	LIST_FOREACH (ent, l, he_ent) {
		if (ent->he_key == k) {
			LIST_REMOVE(ent, he_ent);
			free(ent);
			return;
		}
	}
}

void
uvhash_bucket_fill(uvhash_t *h, FILE *f)
{
	int				i,
					c;
	struct uvhash_ent_list		*l;
	struct uvhash_ent_s		*ent;

	fprintf(f, "=======================\n");
	fprintf(f, "Hash table @%p:\n", h);
	for (i = 0; i < h->h_size; i++) {
		l = h->h_buckets[i];
		c = 0;
		LIST_FOREACH (ent, l, he_ent) {
			c++;
		}
		fprintf(f, "%d\t%d\n", i, c);
	}
	fprintf(f, "=======================\n");
}

/*
 * string hash
 */
uvstrhash_t *
uvstrhash_new(int size)
{
	uvstrhash_t			*ret;
	int				i;

	ret = xmalloc(sizeof(uvstrhash_t));
	ret->sh_size = size;
	ret->sh_buckets = xmalloc(sizeof(struct uvstrhash_ent_list *) * size);

	for (i = 0; i < size; i++) {
		ret->sh_buckets[i] = xmalloc(sizeof(struct uvstrhash_ent_list));
		LIST_INIT(ret->sh_buckets[i]);
	}

	return ret;
}

void
uvstrhash_insert(uvstrhash_t *h, char *k, void *v)
{
	struct uvstrhash_ent_list	*l;
	struct uvstrhash_ent_s		*ent;

	ent = xmalloc(sizeof(struct uvstrhash_ent_s));
	ent->she_key = k;
	ent->she_value = v;

	l = h->sh_buckets[hash_str(k) % h->sh_size];
	LIST_INSERT_HEAD(l, ent, she_ent);
}

void *
uvstrhash_find(uvstrhash_t *h, char *k)
{
	struct uvstrhash_ent_list	*l;
	struct uvstrhash_ent_s		*ent;

	l = h->sh_buckets[hash_str(k) % h->sh_size];
	LIST_FOREACH (ent, l, she_ent) {
		if (!strcmp(ent->she_key, k))
			return ent->she_value;
	}
	return NULL;
}

void
uvstrhash_remove(uvstrhash_t *h, char *k)
{
	struct uvstrhash_ent_list	*l;
	struct uvstrhash_ent_s		*ent;

	l = h->sh_buckets[hash_str(k) % h->sh_size];
	LIST_FOREACH (ent, l, she_ent) {
		if (ent->she_key == k) {
			LIST_REMOVE(ent, she_ent);
			free(ent);
			return;
		}
	}
}

void
uvstrhash_bucket_fill(uvstrhash_t *h, FILE *f)
{
	int				i,
					c;
	struct uvstrhash_ent_list	*l;
	struct uvstrhash_ent_s		*ent;

	fprintf(f, "=======================\n");
	fprintf(f, "String Hash table @%p:\n", h);
	for (i = 0; i < h->sh_size; i++) {
		l = h->sh_buckets[i];
		c = 0;
		LIST_FOREACH (ent, l, she_ent) {
			c++;
		}
		fprintf(f, "%d\t%d\n", i, c);
	}
	fprintf(f, "=======================\n");
}
