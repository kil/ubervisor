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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <event.h>

#include "main.h"
#include "client.h"
#include "misc.h"
#include "child_config.h"


static int sock;

static void
proxy_cb(int s, short evtype, void *unused __attribute__((unused)))
{
	int			r;
	char			buf[BUFFER_SIZ];

	if ((evtype & EV_READ) == 0) {
		exit(1);
	}

	if ((r = read(s, buf, BUFFER_SIZ)) <= 0)
		exit(1);

	if (s == 0) {
		if ((write(sock, buf, r)) != r) {
			fprintf(stderr, "err ... \n");
			exit(1);
		}
	} else {
		if ((write(1, buf, r)) != r) {
			fprintf(stderr, "err ... \n");
			exit(1);
		}
	}
}

int
cmd_proxy(int argc, char **argv)
{
	struct event_base	*evloop;
	struct event		ev_sock,
				ev_in;

	if (argc > 1) {
		fprintf(stderr, "%s takes no options.\n", argv[0]);
		return 1;
	}

	event_init();

	if ((evloop = event_base_new()) == NULL)
		die("event_base_new");

	if ((sock = sock_connect()) == -1)
		die("socket");

	setnonblock(sock);
	setnonblock(0);

	event_set(&ev_sock, sock, EV_READ | EV_PERSIST, proxy_cb, NULL);
	event_add(&ev_sock, NULL);

	event_set(&ev_in, 0, EV_READ | EV_PERSIST, proxy_cb, NULL);
	event_add(&ev_in, NULL);
	event_dispatch();
	return 0;
}
