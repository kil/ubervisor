/*
 * Copyright (c) 2011 Whitematter Labs GmbH
 * All rights reserved.
 * 
 * Copyright (c) 2011-2013 Kilian Klimek <kilian.klimek@googlemail.com>
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
#include <getopt.h>
#include <netinet/in.h>

#include "main.h"
#include "client.h"
#include "misc.h"
#include "child_config.h"

static char subscribe_opts[] = "h";

static struct option subscribe_longopts[] = {
	{ "help",	no_argument,		NULL,	'h' },
	{ NULL,		0,			NULL,	0 }
};

static void
help_subscribe(void)
{
	printf("Usage: %s subs [Options] <ident>\n", program_name);
	printf("\n");
	printf("Options:\n");
	printf("\t-h, --help       help.\n");
	printf("\n");
	printf("Ident:\n");
	printf("\t1:\t server log.\n");
	printf("\t2:\t group status updates.\n");
	printf("\t4:\t group configuration updates.\n");
	exit(EXIT_FAILURE);
}

int
cmd_subscribe(int argc, char **argv)
{
	int			ch,
				sock,
				ident;

	char			*msg;

	char			*buf;
	size_t			buf_siz;

	json_object		*obj,
				*n;

	if (argc < 2)
		help_subscribe();

	while ((ch = getopt_long(argc, argv, subscribe_opts, subscribe_longopts, NULL)) != -1) {
		switch (ch) {
		case 'h':
			help_subscribe();
			break;
		default:
			help_subscribe();
			break;
		}
	}

	argv += optind;
	argc -= optind;

	if (argc != 1)
		help_subscribe();

	ident = strtol(argv[0], NULL, 10);

	obj = json_object_new_object();
	n = json_object_new_int(ident);
	json_object_object_add(obj, "ident", n);

	msg = xstrdup(json_object_to_json_string(obj));
	json_object_put(obj);

	if ((sock = sock_connect()) == -1) {
		die("Failed to connect server");
	}

	if (sock_send_command(sock, "SUBS", msg) == -1) {
		fprintf(stderr, "failed to send command.\n");
		return EXIT_FAILURE;
	}

	free(msg);

	if ((buf = read_reply(sock, &buf_siz)) == NULL) {
		fprintf(stderr, "failed to read reply.\n");
		return EXIT_FAILURE;
	}

	free(buf);

	while(1) {
		if ((buf = read_reply(sock, &buf_siz)) == NULL) {
			fprintf(stderr, "failed to read reply.\n");
			return EXIT_FAILURE;
		}

		printf("%s\n", buf);

		free(buf);
	}

	return EXIT_SUCCESS;
}
