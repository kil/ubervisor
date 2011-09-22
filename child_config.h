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
#ifndef __CHILD_CONFIG_H
#define __CHILD_CONFIG_H

#include <time.h>
#include <sys/types.h>

#ifdef HAVE_SYS_QUEUE_H
#include <sys/queue.h>
#else
#include "compat/queue.h"
#endif

#include <json/json.h>

#define STATUS_RUNNING	1
#define STATUS_STOPPED	2
#define STATUS_BROKEN	3

struct child_config {
	LIST_ENTRY(child_config)	cc_ent;

	char				**cc_command;

	char				*cc_name,
					*cc_stdout,
					*cc_stderr,
					*cc_dir,
					*cc_heartbeat,
					*cc_fatal_cb,
					*cc_stdout_pipe;

	int				cc_instances,
					cc_status,
					cc_killsig;

	/* not really ints but we use -1 to determine if this is set */
	int				cc_uid,
					cc_gid;

	/* internal */
	int				cc_error;
	time_t				cc_errtime;
};

char *child_config_serialize(const struct child_config *);
struct child_config *child_config_unserialize(const char *);
struct child_config *child_config_from_json(json_object *);
void child_config_free(struct child_config *);
struct child_config *child_config_new(void);

#endif /* __CHILD_CONFIG_H */
