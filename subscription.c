/*
 * Copyright (c) 2011 Whitematter Labs GmbH
 * All rights reserved.
 *
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

#include "subscription.h"
#include "child_config.h"

struct subscription_list		subscription_list_head;

void
subscription_insert(struct subscription_list *l, struct subscription *s)
{
	LIST_INSERT_HEAD(l, s, s_ent);
}

void
subscription_remove(struct subscription *s)
{
	LIST_REMOVE(s, s_ent);
}

void
subscription_remove_for_client(struct client_con *c)
{
	struct subscription	*s,
				*s_tmp;

	LIST_FOREACH_SAFE(s, &subscription_list_head, s_ent, s_tmp) {
		if (s->s_client == c) {
			LIST_REMOVE(s, s_ent);
			free(s);
		}
	}
}
