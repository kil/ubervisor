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
#include <limits.h>		/* for PATH_MAX */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <unistd.h>
#include <getopt.h>
#include <pwd.h>

#include <json/json.h>

#include "main.h"
#include "client.h"
#include "misc.h"
#include "child_config.h"
#include "paths.h"


int read_reply(int sock, char *buf, size_t buf_siz)
{
	int		r;
	uint16_t	len,
			off = 0;

	if ((r = read(sock, &len, 2)) < 0)
		die("read");

	if (r < 2)
		return -1;

	len = ntohs(len);

	if (len > buf_siz)
		return -1;

	while (off < len) {
		r = read(sock, buf + off, len - off);
		off += r;
		if (r <= 0)
			return -1;
		off += r;
	}

	buf[len] = '\0';
	return len;
}

int
get_status_reply(int sock)
{
	char		buf[BUFFER_SIZ];
	int		r;

	json_object	*obj,
			*m;


	if (read_reply(sock, buf, BUFFER_SIZ) == -1) {
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	if ((obj = json_tokener_parse(buf)) == NULL) {
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	if (is_error(obj)) {
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	if ((m = json_object_object_get(obj, "code")) == NULL) {
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	r = json_object_get_boolean(m);

	if ((m = json_object_object_get(obj, "msg")) == NULL) {
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	if (r != 1)
		fprintf(stderr, "Error: %s\n", json_object_get_string(m));

	json_object_put(obj);

	return 0;
}

int
sock_connect(void)
{
	int			ret,
				pp[2];
	struct sockaddr_un	addr;
	socklen_t		addr_len;
	pid_t			pid;
	char			sock_path[PATH_MAX],
				*ptr;
	char			arg0[] = "/bin/sh",
				arg1[] = "-c";
	char			*args[4] = {arg0, arg1, NULL, NULL};
	struct passwd		*pw;


	if ((ptr = getenv("UBERVISOR_RSH")) != NULL) {
		args[2] = ptr;

		if (socketpair(AF_UNIX, SOCK_STREAM, 0, pp) == -1)
			die("socketpair");

		pid = fork();

		if (pid == -1)
			die("fork");

		if (pid == 0) {
			/* keeping stderr open */
			close(pp[1]);
			unsetenv("UBERVISOR_RSH");
			close(0);
			close(1);
			dup2(pp[0], 0);
			dup2(pp[0], 1);
			execv(args[0], args);
			exit(EXIT_FAILURE);
		}

		close(pp[0]);
		return pp[1];
	}

	if ((ptr = getenv("UBERVISOR_SOCKET")) == NULL) {
		if ((pw = getpwuid(geteuid())) == NULL)
			die("getpwuid");
		ret = snprintf(sock_path, PATH_MAX, SOCK_PATH, pw->pw_dir);
		if (ret == -1 || ret >= PATH_MAX)
			die("snprintf");
		ptr = sock_path;
	}

	if (strlen(ptr) >= sizeof(addr.sun_path)) {
		fprintf(stderr, "socket path too long\n");
		return -1;
	}

	if ((ret = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		return -1;

	memset(&addr, '\0', sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, ptr);
	addr_len = sizeof(addr);
	if (connect(ret, (struct sockaddr *) &addr, addr_len) == -1)
		return -1;
	return ret;
}

int
sock_write(int sock, const char *msg)
{
	ssize_t		msg_len = strlen(msg);
	if (write(sock, msg, msg_len) != msg_len)
		return -1;
	return 1;
}


int
sock_write_len(int sock, unsigned short len)
{
	len = htons(len);
	if (write(sock, &len, sizeof(uint16_t)) != sizeof(uint16_t))
		return -1;
	return 1;
}

int
sock_send_command(int sock, const char *cmd, const char *pl)
{
	uint16_t	len = 0;

	len = strlen(cmd);

	if (pl)
		len += strlen(pl);
	if (sock_write_len(sock, len) == -1)
		return -1;
	if (sock_write(sock, cmd) == -1)
		return -1;
	if (pl) {
		if (sock_write(sock, pl) == -1)
			return -1;
	}
	return 0;
}

int
sock_send_helo(void)
{
	int		s;
	const char	*msg = "HELO";
	char		buf[5];

	if ((s = sock_connect()) == -1)
		return -1;
	if (sock_write_len(s, 4) == -1) {
		close(s);
		return -1;
	}
	if (sock_write(s, msg) == -1) {
		close(s);
		return -1;
	}
	if (read(s, buf, 4) != 4) {
		close(s);
		return -1;
	}
	close(s);
	return 1;
}

int
cmd_exit(int argc, char **argv)
{
	int		sock,
			ret;

	if (argc > 1) {
		fprintf(stderr, "%s takes no options.\n", argv[0]);
		return 1;
	}

	if ((sock = sock_connect()) == -1) {
		fprintf(stderr, "server not running?\n");
		return 1;
	}

	if (sock_send_command(sock, "EXIT", NULL) == -1) {
		fprintf(stderr, "write data\n");
		return 1;
	}

	ret = get_status_reply(sock);
	close(sock);
	return ret;
}

static char kill_opts[] = "hs:";

static struct option kill_longopts[] = {
	{ "signal",	required_argument,	NULL,	's' },
	{ NULL,		0,			NULL,	0 }
};

static void
help_kill(void)
{
	printf("Usage: %s kill [Options] <name>\n", program_name);
	printf("\n");
	printf("Options:\n");
	printf("\t-s, --signal SIG   send SIG to processes.\n");
	printf("\n");
	exit(1);
}

int
cmd_kill(int argc, char **argv)
{
	int			sock,
				ch,
				len,
				ret,
				i,
				sig = -1;

	char			*msg;

	char			buf[BUFFER_SIZ];

	json_object		*obj,
				*n,
				*e;

	while ((ch = getopt_long(argc, argv, kill_opts, kill_longopts, NULL)) != -1) {
		switch (ch) {
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

	msg = xstrdup(json_object_to_json_string(obj));
	json_object_put(obj);

	if ((sock = sock_connect()) == -1) {
		fprintf(stderr, "server not running?\n");
		return 1;
	}

	if (sock_send_command(sock, "KILL", msg) == -1) {
		fprintf(stderr, "command failed\n");
	}

	free(msg);

	if (read_reply(sock, buf, BUFFER_SIZ) == -1) {
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	if ((obj = json_tokener_parse(buf)) == NULL) {
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	if ((n = json_object_object_get(obj, "code")) == NULL) {
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	ret = json_object_get_boolean(n);
	json_object_put(n);

	if (ret == 0) {
		close(sock);
		return 1;
	}

	if ((n = json_object_object_get(obj, "pids")) == NULL) {
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	len = json_object_array_length(n);
	for (i = 0; i < len; i++) {
		e = json_object_array_get_idx(n, i);
		printf("%d\n", json_object_get_int(e));
	}
	json_object_put(obj);
	return 0;
}


int
cmd_delete(int argc, char **argv)
{
	int			sock,
				len,
				ret,
				i;

	char			*msg;

	char			buf[BUFFER_SIZ];

	json_object		*obj,
				*n,
				*e;

	if (argc < 2) {
		printf("Usage: %s %s <name>\n", program_name, argv[0]);
		return 1;
	}

	obj = json_object_new_object();
	n = json_object_new_string(argv[1]);
	json_object_object_add(obj, "name", n);

	msg = xstrdup(json_object_to_json_string(obj));

	json_object_put(obj);

	if ((sock = sock_connect()) == -1) {
		fprintf(stderr, "server not running?\n");
		return 1;
	}

	if (sock_send_command(sock, "DELE", msg) == -1) {
		fprintf(stderr, "write\n");
		return 1;
	}

	free(msg);

	if (read_reply(sock, buf, BUFFER_SIZ) == -1) {
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	if ((obj = json_tokener_parse(buf)) == NULL) {
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	if ((n = json_object_object_get(obj, "code")) == NULL) {
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	ret = json_object_get_boolean(n);
	json_object_put(n);

	if (ret == 0) {
		close(sock);
		return 1;
	}

	if ((n = json_object_object_get(obj, "pids")) == NULL) {
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	len = json_object_array_length(n);
	for (i = 0; i < len; i++) {
		e = json_object_array_get_idx(n, i);
		printf("%d\n", json_object_get_int(e));
	}
	json_object_put(obj);
	return 0;
}

int
cmd_dump(int argc, char **argv)
{
	int		sock,
			ret;

	if (argc > 1) {
		fprintf(stderr, "%s takes no options.\n", argv[0]);
		return 1;
	}

	if ((sock = sock_connect()) == -1) {
		fprintf(stderr, "server not running?\n");
		return 1;
	}

	if (sock_send_command(sock, "DUMP", NULL) == -1) {
		fprintf(stderr, "failed.\n");
		return 1;
	}

	ret = get_status_reply(sock);
	close(sock);
	return ret;
}

int
cmd_list(int argc, char **argv)
{
	int		sock,
			len,
			i;

	char		buf[BUFFER_SIZ];

	json_object	*obj,
			*n;

	if (argc > 1) {
		fprintf(stderr, "%s takes no options.\n", argv[0]);
		return 1;
	}

	if ((sock = sock_connect()) == -1) {
		fprintf(stderr, "server not running?\n");
		return 1;
	}

	if (sock_send_command(sock, "LIST", NULL) == -1) {
		fprintf(stderr, "failed.\n");
		return 1;
	}


	if (read_reply(sock, buf, BUFFER_SIZ) == -1) {
		fprintf(stderr, "Failed to read reply.\n");
		return 1;
	}

	close(sock);

	if ((obj = json_tokener_parse(buf)) == NULL) {
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	if (!json_object_is_type(obj, json_type_array)) {
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	len = json_object_array_length(obj);

	for (i = 0; i < len; i++) {
		if ((n = json_object_array_get_idx(obj, i)) == NULL) {
			fprintf(stderr, "Failed to parse reply.\n");
			return 1;
		}
		printf("%s\n", json_object_get_string(n));
	}

	return 0;
}
