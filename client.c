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
#include "cmd_server.h"
#include "client.h"
#include "misc.h"
#include "child_config.h"
#include "paths.h"


char *read_reply(int sock, size_t *buf_siz)
{
	int		r;
	uint16_t	len,
			cid,
			off;
	char		*ret = NULL;
	size_t		ret_siz = 0;
	int		cont = 0;

	*buf_siz = 0;

	do {
		if ((r = read(sock, &len, 2)) < 0) {
			if (ret)
				free(ret);
			return NULL;
		}

		if (r < 2) {
			if (ret)
				free(ret);
			return NULL;
		}

		if ((r = read(sock, &cid, 2)) < 0) {
			if (ret)
				free(ret);
			return NULL;
		}

		if (r < 2) {
			if (ret)
				free(ret);
			return NULL;
		}

		len = ntohs(len);

		if (len == 0) {
			if (ret)
				free(ret);
			return NULL;
		}

		if ((len & CHUNKEXT) == CHUNKEXT) {
			cont = 1;
			len -= CHUNKEXT;
		} else {
			cont = 0;
		}

		*buf_siz += len;
		ret = xrealloc(ret, *buf_siz + 1);

		off = 0;

		while (off < len) {
			r = read(sock, ret + ret_siz + off, len - off);
			if (r <= 0) {
				free(ret);
				return NULL;
			}
			off += r;
		}
		ret_siz += len;
	} while(cont);

	ret[*buf_siz] = '\0';
	return ret;
}

int
get_status_reply(int sock)
{
	char		*buf = NULL;
	int		r;
	size_t		buf_siz;
	json_object	*obj,
			*m;


	if ((buf = read_reply(sock, &buf_siz)) == NULL) {
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	if ((obj = json_tokener_parse(buf)) == NULL) {
		free(buf);
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	if (is_error(obj)) {
		free(buf);
		fprintf(stderr, "Failed to parse reply.\n");
		return 1;
	}

	free(buf);

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

char *
sock_path(void)
{
	static char		_sock_path[PATH_MAX];
	char			*ptr;
	struct passwd		*pw;
	int			ret;


	ptr = getenv("UBERVISOR_SOCKET");

	if (ptr == NULL) {
		if ((pw = getpwuid(geteuid())) == NULL)
			return NULL;
		ret = snprintf(_sock_path, PATH_MAX, SOCK_PATH, pw->pw_dir);
		if (ret == -1 || ret >= PATH_MAX)
			return NULL;
	} else {
		if (strlen(ptr) >= PATH_MAX - 1)
			return NULL;
		strncpy(_sock_path, ptr, PATH_MAX);
	}

	return _sock_path;
}

int
sock_connect(void)
{
	int			ret,
				pp[2];
	struct sockaddr_un	addr;
	socklen_t		addr_len;
	pid_t			pid;
	char			*ptr;
	char			arg0[] = "/bin/sh",
				arg1[] = "-c";
	char			*args[4] = {arg0, arg1, NULL, NULL};


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

	if ((ptr = sock_path()) == NULL)
		return -1;

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
	if (connect(ret, (struct sockaddr *) &addr, addr_len) == -1) {
		close(ret);
		return -1;
	}
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
	uint16_t	len = 0,
			cid = ntohs(1);

	len = strlen(cmd);

	if (pl)
		len += strlen(pl);
	if (sock_write_len(sock, len) == -1)
		return -1;
	if (sock_write_len(sock, cid) == -1)
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
sock_send_helo(int s)
{
	int		r;
	const char	*msg = "HELO";
	char		*buf;
	size_t		buf_siz;

	json_object	*obj,
			*m;


	if (sock_send_command(s, msg, NULL) != 0)
		return -1;

	buf = read_reply(s, &buf_siz);

	if (!buf) {
		close(s);
		return -1;
	}

	if ((obj = json_tokener_parse(buf)) == NULL) {
		free(buf);
		fprintf(stderr, "Failed to parse reply.\n");
		return -1;
	}

	if (is_error(obj)) {
		free(buf);
		fprintf(stderr, "Failed to parse reply.\n");
		return -1;
	}

	free(buf);

	if ((m = json_object_object_get(obj, "code")) == NULL) {
		fprintf(stderr, "Failed to parse reply.\n");
		return -1;
	}

	r = json_object_get_boolean(m);

	if ((m = json_object_object_get(obj, "msg")) == NULL) {
		fprintf(stderr, "Failed to parse reply.\n");
		return -1;
	}

	if (r != 1) {
		fprintf(stderr, "Error: %s\n", json_object_get_string(m));
	}

	json_object_put(obj);
	close(s);
	return 1;
}
