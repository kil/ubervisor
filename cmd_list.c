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

#include <json/json.h>

#include "main.h"
#include "client.h"
#include "misc.h"

int
cmd_list(int argc, char **argv)
{
	int		sock,
			len,
			i;

	char		*buf;
	size_t		buf_siz;

	json_object	*obj,
			*n;

	if (argc > 1) {
		fprintf(stderr, "%s takes no options.\n", argv[0]);
		return EXIT_FAILURE;
	}

	if ((sock = sock_connect()) == -1) {
		die("Failed to connect server");
	}

	if (sock_send_command(sock, "LIST", NULL) == -1) {
		fprintf(stderr, "failed.\n");
		return EXIT_FAILURE;
	}


	if ((buf = read_reply(sock, &buf_siz)) == NULL) {
		fprintf(stderr, "Failed to read reply.\n");
		return EXIT_FAILURE;
	}

	close(sock);

	if ((obj = json_tokener_parse(buf)) == NULL) {
		free(buf);
		fprintf(stderr, "Failed to parse reply.\n");
		return EXIT_FAILURE;
	}

	free(buf);

	if (!json_object_is_type(obj, json_type_array)) {
		fprintf(stderr, "Failed to parse reply.\n");
		return EXIT_FAILURE;
	}

	len = json_object_array_length(obj);

	for (i = 0; i < len; i++) {
		if ((n = json_object_array_get_idx(obj, i)) == NULL) {
			fprintf(stderr, "Failed to parse reply.\n");
			return EXIT_FAILURE;
		}
		printf("%s\n", json_object_get_string(n));
	}

	return EXIT_SUCCESS;
}
