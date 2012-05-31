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

#include "main.h"
#include "client.h"
#include "misc.h"
#include "child_config.h"

static char update_opts[] = "a:d:e:f:hH:i:k:o:s:";

static struct option update_longopts[] = {
	{ "age",	required_argument,	NULL,	'a' },
	{ "dir",	required_argument,	NULL,	'd' },
	{ "stderr",	required_argument,	NULL,	'e' },
	{ "fatal",	required_argument,	NULL,	'f' },
	{ "help",	no_argument,		NULL,	'h' },
	{ "heartbeat",	required_argument,	NULL,	'H' },
	{ "instances",	required_argument,	NULL,	'i' },
	{ "killsig",	required_argument,	NULL,	'k' },
	{ "stdout",	required_argument,	NULL,	'o' },
	{ "status",	required_argument,	NULL,	's' },
	{ NULL,		0,			NULL,	0 }
};

static void
help_update(void)
{
	printf("Usage: %s update [Options] <name>\n", program_name);
	printf("\n");
	printf("Options:\n");
	printf("\t-a, --age SEC         max process age in seconds.\n");
	printf("\t-d, --dir DIR         chdir to DIR.\n");
	printf("\t-e, --stderr FILE     stderr log FILE.\n");
	printf("\t-f, --fatal COMMAND   run COMMAND if fatal state.\n");
	printf("\t-h, --help            help.\n");
	printf("\t-H, --heartbeat COMMAND\n");
	printf("\t                      run COMMAND 5 secondly.\n");
	printf("\t-i, --instances COUNT number of process to start.\n");
	printf("\t-k, --killsig SIGNAL  signal used to kill processes in this group.\n");
	printf("\t-o, --stdout FILE     stdout log FILE.\n");
	printf("\t-s, --status STATUS   status to create group with.\n");
	printf("\n");
	printf("Examples:\n");
	printf("\tuber update -i 4 test\n");
	printf("\n");
	exit(1);
}

int
cmd_update(int argc, char **argv)
{
	struct child_config	*cc;
	int			ch,
				ret;
	const char		*b;
	int			sock;

	if (argc < 2)
		help_update();

	cc = child_config_new();

	while ((ch = getopt_long(argc, argv, update_opts, update_longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			cc->cc_age = strtol(optarg, NULL, 10);
			break;
		case 'd':
			cc->cc_dir = optarg;
			break;
		case 'e':
			cc->cc_stderr = optarg;
			break;
		case 'f':
			cc->cc_fatal_cb = optarg;
			break;
		case 'h':
			help_update();
			break;
		case 'H':
			cc->cc_heartbeat = optarg;
			break;
		case 'i':
			cc->cc_instances = strtol(optarg, NULL, 10);
			break;
		case 'k':
			cc->cc_killsig = strtol(optarg, NULL, 10);
			break;
		case 'o':
			cc->cc_stdout = optarg;
			break;
		case 's':
			cc->cc_status = child_config_status_from_string(optarg);
			if (cc->cc_status == -1) {
				fprintf(stderr, "Illegal status code\n");
				exit(1);
			}
			break;
		default:
			help_update();
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		help_update();

	cc->cc_name = argv[0];

	b = child_config_serialize(cc);

	if ((sock = sock_connect()) == -1) {
		fprintf(stderr, "server running?\n");
		return 1;
	}

	if (sock_send_command(sock, "UPDT", b) == -1) {
		fprintf(stderr, "failed to send command.\n");
		return 1;
	}

	ret = get_status_reply(sock);
	close(sock);
	return ret;
}
