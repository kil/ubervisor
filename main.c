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

#include "server.h"
#include "client.h"
#include "cmd_start.h"
#include "cmd_update.h"
#include "cmd_get.h"
#include "cmd_proxy.h"
#include "cmd_subscribe.h"
#include "cmd_read.h"

#ifdef DEBUG
#define DEBUG_VERSION	"-debug"
#else
#define DEBUG_VERSION	""
#endif

#define AUTHOR		"Kilian Klimek <kilian.klimek@googlemail.com>"
#ifdef DEBUG
const char		*_malloc_options = "J";
#endif

char			*program_name;


static void
print_version(void)
{
	printf("ubervisor-" UV_VERSION DEBUG_VERSION " by " AUTHOR "\n");
}

static void
help(void)
{
	printf("\n");
	print_version();
	printf("\n");
	printf("Usage: %s <command> [args]\n", program_name);
	printf("\n");
	printf("Commands:\n");
	printf("\tdelete\t delete program from ubervisor\n");
	printf("\tdump\t signal server to dump current config.\n");
	printf("\texit\t stop the server\n");
	printf("\tget\t get config of a group.\n");
	printf("\tkill\t kill all processes in a group (aka restart)\n");
	printf("\tlist\t list groups.\n");
	printf("\tpids\t get pids of processes in a group.\n");
	printf("\tproxy\t multiplex stdin/out to socket.\n");
	printf("\tread\t read from log file.\n");
	printf("\tserver\t start the server\n");
	printf("\tstart\t start a program\n");
	printf("\tsubs\t subscribe to server events.\n");
	printf("\tupdate\t modify a group\n");
	printf("\n");
	printf("`ubervisor <command> -h` to get a list of supported options.\n");
	printf("\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int		ret = 1;
	char		*cmd;

	program_name = argv[0];

	if (argc < 2)
		help();

	cmd = argv[1];
	argc--;
	argv++;

	if (!strcmp(cmd, "server")) {
		ret = cmd_server(argc, argv);
	} else if (!strcmp(cmd, "start")) {
		ret = cmd_start(argc, argv);
	} else if (!strcmp(cmd, "exit")) {
		ret = cmd_exit(argc, argv);
	} else if (!strcmp(cmd, "delete")) {
		ret = cmd_delete(argc, argv);
	} else if (!strcmp(cmd, "kill")) {
		ret = cmd_kill(argc, argv);
	} else if (!strcmp(cmd, "update")) {
		ret = cmd_update(argc, argv);
	} else if (!strcmp(cmd, "dump")) {
		ret = cmd_dump(argc, argv);
	} else if (!strcmp(cmd, "get")) {
		ret = cmd_get(argc, argv);
	} else if (!strcmp(cmd, "list")) {
		ret = cmd_list(argc, argv);
	} else if (!strcmp(cmd, "proxy")) {
		ret = cmd_proxy(argc, argv);
	} else if (!strcmp(cmd, "subs")) {
		ret = cmd_subscribe(argc, argv);
	} else if (!strcmp(cmd, "pids")) {
		ret = cmd_pids(argc, argv);
	} else if (!strcmp(cmd, "read")) {
		ret = cmd_read(argc, argv);
	} else if (!strcmp(cmd, "-v") || !strcmp(cmd, "-V")) {
		print_version();
	} else {
		help();
	}

	return ret;
}

