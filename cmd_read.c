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
#include <getopt.h>

#include <json/json.h>

#include "main.h"
#include "client.h"
#include "misc.h"

static char read_opts[] = "b:hi:o:s:";

static struct option read_longopts[] = {
	{ "bytes",	required_argument,	NULL,	'b' },
	{ "help",	no_argument,		NULL,	'h' },
	{ "instance",	required_argument,	NULL,	'i' },
	{ "offset",	required_argument,	NULL,	'o' },
	{ "stream",	required_argument,	NULL,	's' },
	{ NULL,		0,			NULL,	0 }
};

static void
help_read(void)
{
	printf("Usage: %s read [Options] <name>\n", program_name);
	printf("\n");
	printf("Options:\n");
	printf("\t-b, --bytes COUNT     read COUNT bytes (default: 1024).\n");
	printf("\t-i, --instance INST   log file for instance INST (default: 0).\n");
	printf("\t-o, --offset OFF      start reading at offset OFF (default: -1).\n");
	printf("\t-s, --stream STREAM   stream to read. 1 = stdout, 2 = stderr "
			"(default: 1).\n");
	printf("\n");
	printf("Examples:\n");
	printf("\tuber read -i 4 test\n");
	printf("\n");
	exit(1);
}

int
cmd_read(int argc, char **argv)
{
	int			ch,
				sock,
				instance = 0,
				stream = 1,
				bytes = 1024;

	const char		*b;

	char			buf[BUFFER_SIZ];

	double			off = -1.0;

	json_object		*obj,
				*n;

	if (argc < 2)
		help_read();

	while ((ch = getopt_long(argc, argv, read_opts, read_longopts, NULL)) != -1) {
		switch (ch) {
		case 'i':
			instance = strtol(optarg, NULL, 10);
			break;
		case 'b':
			bytes = strtol(optarg, NULL, 10);
			break;
		case 's':
			stream = strtol(optarg, NULL, 10);
			break;
		case 'o':
			off = strtod(optarg, NULL);
			break;
		default:
			help_read();
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		help_read();

	if ((sock = sock_connect()) == -1) {
		fprintf(stderr, "server running?\n");
		return 1;
	}

	obj = json_object_new_object();
	n = json_object_new_int(bytes);
	json_object_object_add(obj, "bytes", n);
	n = json_object_new_int(stream);
	json_object_object_add(obj, "stream", n);
	n = json_object_new_string(argv[0]);
	json_object_object_add(obj, "name", n);
	n = json_object_new_int(instance);
	json_object_object_add(obj, "instance", n);
	n = json_object_new_double(off);
	json_object_object_add(obj, "offset", n);

	b = json_object_to_json_string(obj);

	if (sock_send_command(sock, "READ", b) == -1) {
		fprintf(stderr, "failed to send command.\n");
		return 1;
	}

	json_object_put(obj);

	if (read_reply(sock, buf, BUFFER_SIZ) == -1) {
		fprintf(stderr, "failed to read reply.\n");
		return 1;
	}

	close(sock);

	if ((obj = json_tokener_parse(buf)) == NULL) {
		fprintf(stderr, "failed.\n");
		return 1;
	}

	if (is_error(obj)) {
		fprintf(stderr, "failed.\n");
		return 1;
	}

	if (!json_object_is_type(obj, json_type_object)) {
		fprintf(stderr, "failed.\n");
		return 1;
	}

	if ((n = json_object_object_get(obj, "offset")) == NULL) {
		fprintf(stderr, "failed.\n");
		return 1;
	}

	if (!json_object_is_type(n, json_type_double)) {
		fprintf(stderr, "failed.\n");
		return 1;
	}

	printf("off = %lf\n", json_object_get_double(n));

	if ((n = json_object_object_get(obj, "fsize")) == NULL) {
		fprintf(stderr, "failed.\n");
		return 1;
	}

	if (!json_object_is_type(n, json_type_double)) {
		fprintf(stderr, "failed.\n");
		return 1;
	}

	printf("fsize = %lf\n", json_object_get_double(n));

	if ((n = json_object_object_get(obj, "log")) == NULL) {
		fprintf(stderr, "failed.\n");
		return 1;
	}

	if (!json_object_is_type(n, json_type_string)) {
		fprintf(stderr, "failed.\n");
		return 1;
	}

	printf("log = %s\n", json_object_get_string(n));

	return 0;
}
