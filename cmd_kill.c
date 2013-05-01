/*
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

#include <json/json.h>

#include "main.h"
#include "client.h"
#include "misc.h"


static char kill_opts[] = "hi:s:";

static struct option kill_longopts[] = {
	{ "index",	required_argument,	NULL,	'i' },
	{ "signal",	required_argument,	NULL,	's' },
	{ NULL,		0,			NULL,	0 }
};

static void
help_kill(void)
{
	printf("Usage: %s kill [Options] <name>\n", program_name);
	printf("\n");
	printf("Options:\n");
	printf("\t-i, --index NUM    send signal only to a single process.\n");
	printf("\t-s, --signal SIG   send SIG to processes.\n");
	printf("\n");
	exit(EXIT_FAILURE);
}

int
cmd_kill(int argc, char **argv)
{
	int			sock,
				ch,
				len,
				ret,
				i,
				idx = -1,
				sig = -1;

	char			*msg;

	char			*buf;
	size_t			buf_siz;

	json_object		*obj,
				*n,
				*e;

	while ((ch = getopt_long(argc, argv, kill_opts, kill_longopts, NULL)) != -1) {
		switch (ch) {
		case 'i':
			idx = strtol(optarg, NULL, 10);
			break;
		case 's':
			sig = strtol(optarg, NULL, 10);
			break;
		case 'h':
		default:
			help_kill();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		help_kill();

	obj = json_object_new_object();
	n = json_object_new_string(argv[0]);
	json_object_object_add(obj, "name", n);

	if (sig != -1) {
		n = json_object_new_int(sig);
		json_object_object_add(obj, "sig", n);
	}

	if (idx != -1) {
		n = json_object_new_int(idx);
		json_object_object_add(obj, "index", n);
	}

	msg = xstrdup(json_object_to_json_string(obj));
	json_object_put(obj);

	if ((sock = sock_connect()) == -1) {
		die("Failed to connect server");
	}

	if (sock_send_command(sock, "KILL", msg) == -1) {
		fprintf(stderr, "command failed\n");
	}

	free(msg);

	if ((buf = read_reply(sock, &buf_siz)) == NULL) {
		fprintf(stderr, "Failed to parse reply.\n");
		return EXIT_FAILURE;
	}

	if ((obj = json_tokener_parse(buf)) == NULL) {
		free(buf);
		fprintf(stderr, "Failed to parse reply.\n");
		return EXIT_FAILURE;
	}

	free(buf);

	if ((n = json_object_object_get(obj, "code")) == NULL) {
		fprintf(stderr, "Failed to parse reply.\n");
		return EXIT_FAILURE;
	}

	ret = json_object_get_boolean(n);
	json_object_put(n);

	if (ret == 0) {
		fprintf(stderr, "Command failed.\n");
		close(sock);
		return EXIT_FAILURE;
	}

	if ((n = json_object_object_get(obj, "pids")) == NULL) {
		fprintf(stderr, "Failed to parse reply.\n");
		return EXIT_FAILURE;
	}

	len = json_object_array_length(n);
	for (i = 0; i < len; i++) {
		e = json_object_array_get_idx(n, i);
		printf("%d\n", json_object_get_int(e));
	}
	json_object_put(obj);
	return EXIT_SUCCESS;
}
