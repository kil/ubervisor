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
#ifndef __UVHASH_H
#define __UVHASH_H

#include <stdio.h>

#include "config.h"

#ifdef HAVE_SYS_QUEUE_H
#include <sys/queue.h>
#else
#include "compat/queue.h"
#endif

/*
 * int key
 */
struct uvhash_ent_s {
	LIST_ENTRY(uvhash_ent_s)	he_ent;
	uint32_t			he_key;
	void				*he_value;
};

LIST_HEAD(uvhash_ent_list, uvhash_ent_s);

struct uvhash_s {
	int				h_size;
	struct uvhash_ent_list		**h_buckets;
};

typedef struct uvhash_s			uvhash_t;

uvhash_t *uvhash_new(int);
void uvhash_insert(uvhash_t *, uint32_t, void *);
void uvhash_remove(uvhash_t *, uint32_t);
void *uvhash_find(uvhash_t *, uint32_t);
void uvhash_bucket_fill(uvhash_t *, FILE *);

/*
 * string key
 */
struct uvstrhash_ent_s {
	LIST_ENTRY(uvstrhash_ent_s)	she_ent;
	char				*she_key;
	void				*she_value;
};

LIST_HEAD(uvstrhash_ent_list, uvstrhash_ent_s);

struct uvstrhash_s {
	int				sh_size;
	struct uvstrhash_ent_list	**sh_buckets;
};

typedef struct uvstrhash_s		uvstrhash_t;

uvstrhash_t *uvstrhash_new(int);
void uvstrhash_insert(uvstrhash_t *, char *, void *);
void uvstrhash_remove(uvstrhash_t *, char *);
void *uvstrhash_find(uvstrhash_t *, char *);
void uvstrhash_bucket_fill(uvstrhash_t *, FILE *);

#endif /* __UVHASH_H */
