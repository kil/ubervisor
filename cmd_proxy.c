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

struct bufferevent	*be_in,
			*be_out,
			*be_sock;

static void
proxy_read_cb(struct bufferevent *b, void *unused __attribute__((unused)))
{
	char			buf[BUFFER_SIZ];
	size_t			r;
	int			ret;

	r = bufferevent_read(b, buf, BUFFER_SIZ);

	if (b == be_in)
		ret = bufferevent_write(be_sock, buf, r);
	else /* if (b == be_sock) */
		ret = bufferevent_write(be_out, buf, r);

	if (ret == -1) {
		fprintf(stderr, "Error\n");
		exit(1);
	}
}

static void
proxy_error_cb(struct bufferevent *b __attribute__((unused)),
		short what __attribute__((unused)),
		void *cx __attribute__((unused)))
{
	exit(1);
}

int
cmd_proxy(int argc, char **argv)
{
	struct event_base	*evloop;
	int			sock;

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

	if ((be_out = bufferevent_new(1, NULL, NULL,
					proxy_error_cb, NULL)) == NULL)
		die("bufferevent_new");

	if (bufferevent_enable(be_out, EV_WRITE))
		die("bufferevent_enable");

	if ((be_in = bufferevent_new(0, proxy_read_cb, NULL,
					proxy_error_cb, NULL)) == NULL)
		die("bufferevent_new");

	if (bufferevent_enable(be_in, EV_READ))
		die("bufferevent_enable");

	bufferevent_setwatermark(be_in, EV_READ, 1, BUFFER_SIZ);

	if ((be_sock = bufferevent_new(sock, proxy_read_cb, NULL,
					proxy_error_cb, NULL)) == NULL)
		die("bufferevent_new");

	if (bufferevent_enable(be_sock, EV_READ))
		die("bufferevent_enable");

	bufferevent_setwatermark(be_sock, EV_READ, 1, BUFFER_SIZ);

	event_dispatch();
	return 0;
}
