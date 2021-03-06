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

#include "main.h"
#include "client.h"
#include "misc.h"
#include "child_config.h"

static char start_opts[] = "+a:d:e:f:g:G:hH:i:k:o:s:u:U:";

static struct option start_longopts[] = {
	{ "age",	required_argument,	NULL,	'a' },
	{ "dir",	required_argument,	NULL,	'd' },
	{ "stderr",	required_argument,	NULL,	'e' },
	{ "fatal",	required_argument,	NULL,	'f' },
	{ "gid",	required_argument,	NULL,	'g' },
	{ "groupname",	required_argument,	NULL,	'G' },
	{ "help",	no_argument,		NULL,	'h' },
	{ "heartbeat",	required_argument,	NULL,	'H' },
	{ "instances",	required_argument,	NULL,	'i' },
	{ "killsig",	required_argument,	NULL,	'k' },
	{ "stdout",	required_argument,	NULL,	'o' },
	{ "status",	required_argument,	NULL,	's' },
	{ "uid",	required_argument,	NULL,	'u' },
	{ "username",	required_argument,	NULL,	'U' },
	{ NULL,		0,			NULL,	0 }
};

static void
help_start(void)
{
	printf("Usage: %s start [Options] <name> <command> [args]\n", program_name);
	printf("\n");
	printf("Options: (defaults in brackets)\n");
	printf("\t-a, --age SEC         max process age in seconds (not set).\n");
	printf("\t-d, --dir DIR         chdir to DIR (not set).\n");
	printf("\t-e, --stderr FILE     stderr log FILE (/dev/null).\n");
	printf("\t-f, --fatal COMMAND   command to run on fatal condition (not set).\n");
	printf("\t-g, --gid GID         GID to start processes as (not set).\n");
	printf("\t-G, --groupname NAME  loopup and set group id for group NAME (not set).\n");
	printf("\t-h, --help            help.\n");
	printf("\t-H, --heartbeat COMMAND\n");
	printf("\t                      run COMMAND 5 secondly (not set).\n");
	printf("\t-i, --instances COUNT number of process to start (1).\n");
	printf("\t-k, --killsig SIGNAL  signal used to kill processes in this group (15).\n");
	printf("\t-o, --stdout FILE     stdout log FILE (/dev/null).\n");
	printf("\t-s, --status STATUS   status to create group with (1).\n");
	printf("\t-u, --uid UID         UID to start processes as (not set).\n");
	printf("\t-U, --username NAME   lookup user NAME and set uid of this user (not set).\n");
	printf("\n");
	printf("Status codes:\n");
	printf("\t1 or start:           running\n");
	printf("\t2 or stop:            stopped\n");
	printf("\n");
	printf("Examples:\n");
	printf("\tuber start -o /tmp/stdout sleeper /bin/sleep 4\n");
	printf("\n");
	exit(EXIT_FAILURE);
}

int
cmd_start(int argc, char **argv)
{
	struct child_config	*cc;
	int			ch,
				x,
				ret;
	const char		*b;
	int			sock;


	if (argc < 2)
		help_start();

	cc = child_config_new();
	cc->cc_stdout = NULL;
	cc->cc_stderr = NULL;
	cc->cc_instances = 1;
	cc->cc_status = STATUS_RUNNING;
	cc->cc_killsig = 15;

	while ((ch = getopt_long(argc, argv, start_opts, start_longopts, NULL)) != -1) {
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
		case 'g':
			cc->cc_gid = strtol(optarg, NULL, 10);
			break;
		case 'G':
			cc->cc_groupname = optarg;
			break;
		case 'h':
			help_start();
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
		case 'u':
			cc->cc_uid = strtol(optarg, NULL, 10);
			break;
		case 'U':
			cc->cc_username = optarg;
			break;
		default:
			help_start();
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 2)
		help_start();

	cc->cc_name = argv[0];

	x = 1;
	cc->cc_command = xmalloc(sizeof(char *) * (argc + 1));
	while (x < argc) {
		cc->cc_command[x - 1] = argv[x];
		x++;
	}

	cc->cc_command[x - 1] = NULL;

	b = child_config_serialize(cc);

	if ((sock = sock_connect()) == -1) {
		printf("server running?\n");
		return EXIT_FAILURE;
	}

	if (sock_send_command(sock, "SPWN", b) == -1) {
		printf("failed to send command.\n");
		return EXIT_FAILURE;
	}

	ret = get_status_reply(sock);
	close(sock);
	/* lazily using return value from get_status_reply */
	return ret;
}
