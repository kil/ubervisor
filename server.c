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
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <getopt.h>
#include <pwd.h>
#include <errno.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <dirent.h>

#include <json/json.h>
#include <event.h>

#include "config.h"
#include "main.h"
#include "client.h"
#include "child_config.h"
#include "misc.h"
#include "paths.h"

#ifdef HAVE_SYS_QUEUE_H
#include <sys/queue.h>
#else
#include "compat/queue.h"
#endif

/*
 * types
 */
struct process {
	LIST_ENTRY(process)	p_ent;
	pid_t			p_pid;
	time_t			p_start;
	struct child_config	*p_child_config;
	int			p_instance;
	struct event		p_heartbeat_timer;
};

struct client_con {
	LIST_ENTRY(client_con)	c_ent;
	int			c_sock;
	struct bufferevent	*c_be;
	size_t			c_len;
	uint16_t		c_cid;
};

/*
 * globals
 */
static LIST_HEAD(process_list, process)			process_list_head;
static LIST_HEAD(client_con_list, client_con)		client_con_list_head;

static struct event_base	*evloop;
static FILE			*log_fd;
static int			auto_dump,
				allow_exit;

/*
 * prototypes
 */
static void heartbeat_cb(int, short, void *);
static int c_dump(struct client_con *);

/*
 * if there are more then (ERROR_MAX * instances) spawn errors over
 * a period of ERROR_PERIOD seconds, mark a process group broken.
 */
#define ERROR_MAX	6
#define ERROR_PERIOD	10

/*
 * heartbeat interval.
 */
#define HEARTBEAT_SEC	5

static char		server_opts[] = "ac:d:fhlo:s";

static struct option	server_longopts[] = {
	{ "autodump",	no_argument,		NULL,	'a' },
	{ "config",	required_argument,	NULL,	'c' },
	{ "dir",	required_argument,	NULL,	'd' },
	{ "foreground",	no_argument,		NULL,	'f' },
	{ "help",	no_argument,		NULL,	'h' },
	{ "loadlatest",	no_argument,		NULL,	'l' },
	{ "noexit",	no_argument,		NULL,	'n' },
	{ "logfile",	required_argument,	NULL,	'o' },
	{ "silent",	no_argument,		NULL,	's' },
	{ NULL,		0,			NULL,	0 }
};

static void
help_server(void)
{
	printf("Usage: %s server [Options]\n", program_name);
	printf("\n");
	printf("Options:\n");
	printf("\t-a, --autodump         create a dump after each update and start command.\n");
	printf("\t-c, --config FILE      load a dump from FILE.\n");
	printf("\t-d, --dir DIR          change to DIR after start.\n");
	printf("\t-f, --foreground       don't fork into background.\n");
	printf("\t-h, --help             help.\n");
	printf("\t-l, --loadlatest FILE  load most recent dump.\n");
	printf("\t-n, --noexit           don't obey the exit command..\n");
	printf("\t-o, --logfile FILE     write log output to FILE.\n");
	printf("\t-s, --silent           exit silently if server is already running.\n");
	printf("\n");
	printf("Examples:\n");
	printf("\tubervisor server -d /tmp\n");
	printf("\n");
	exit(1);
}


static void
drop_client_connection(struct client_con *c)
{
	bufferevent_free(c->c_be);
	close(c->c_sock);
	LIST_REMOVE(c, c_ent);
	free(c);
}

static void
process_insert(struct process *p)
{
	LIST_INSERT_HEAD(&process_list_head, p, p_ent);
}

static struct process *
process_find(pid_t pid)
{
	struct process	*p;

	p = LIST_FIRST(&process_list_head);
	while (p != NULL) {
		if (p->p_pid == pid)
			return p;
		p = LIST_NEXT(p, p_ent);
	}

	return NULL;
}

static struct process *
process_find_instance(struct child_config *cc, int i)
{
	struct process	*p;

	p = LIST_FIRST(&process_list_head);
	while (p != NULL) {
		if (p->p_child_config == cc && p->p_instance == i)
			return p;
		p = LIST_NEXT(p, p_ent);
	}
	return NULL;
}

static void
process_remove(struct process *p)
{
	LIST_REMOVE(p, p_ent);
}

static void
slog(const char *fmt, ...)
{
	va_list		ap;
	char		time_buf[64],
			msg[512];
	int		msg_len;
	size_t		ts_len;
	struct tm	*t;
	time_t		tt;

	tt = time(NULL);
	t = gmtime(&tt);
	ts_len = strftime(time_buf, sizeof(time_buf), "%b %d %T ", t);
	fwrite(time_buf, ts_len, 1, log_fd);

	va_start(ap, fmt);
	msg_len = vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	fwrite(msg, msg_len, 1, log_fd);
}

static void
run_fatal_cb(struct child_config *cc)
{
	char	*args[3];
	pid_t	pid;

	if (cc->cc_fatal_cb == NULL)
		return;

	if ((pid = fork()) == -1) {
		slog("fork failed when running fatal_cb for %s\n", cc->cc_name);
		return;
	}

	slog("running fatal_cb %s ...\n", cc->cc_fatal_cb);
	if (pid == 0) {
		args[0] = cc->cc_fatal_cb;
		args[1] = cc->cc_name;
		args[2] = NULL;
		execv(args[0], args);
		exit(EXIT_FAILURE);
	}
}

/*
 * start heartbeat timer for process.
 */
static void
schedule_heartbeat(struct process *p)
{
	struct timeval	tv = {HEARTBEAT_SEC, 0};
	evtimer_set(&p->p_heartbeat_timer, heartbeat_cb, p);
	evtimer_add(&p->p_heartbeat_timer, &tv);
	event_base_set(evloop, &p->p_heartbeat_timer);
}

/*
 * set uid/gid if needed.
 */
static void
spawn_child_setids(struct child_config *cc)
{
	if (cc->cc_gid != -1) {
		if (setegid(cc->cc_gid) != 0)
			exit(1);

		if (setgid(cc->cc_gid) != 0)
			exit(1);
	}

	if (cc->cc_uid != -1) {
		if (seteuid(cc->cc_uid) != 0)
			exit(1);

		if (setuid(cc->cc_uid) != 0)
			exit(1);

		if (setuid(0) != -1)
			exit(1);
	}

	if (cc->cc_gid != -1) {
		if (setgid(0) != -1)
			exit(1);
	}
}

/*
 * setup child process. we are already forked here.
 */
static void
spawn_child(struct child_config *cc)
{
	int		stdout_fd,
			stderr_fd;

	spawn_child_setids(cc);

	if (cc->cc_dir != NULL) {
		if (chdir(cc->cc_dir) == -1)
			exit(1);
	}
	close(0);
	close(1);
	close(2);

	if (cc->cc_stdout != NULL) {
		if ((stdout_fd = open(cc->cc_stdout,
				O_APPEND | O_CREAT | O_WRONLY,
				S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1) {
			exit(1);
		}
		dup2(stdout_fd, 1);
		close(stdout_fd);
	}

	if (cc->cc_stderr != NULL) {
		if ((stderr_fd = open(cc->cc_stderr,
				O_APPEND | O_CREAT | O_WRONLY,
				S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1) {
			exit(1);
		}
		dup2(stderr_fd, 2);
		close(stderr_fd);
	}

	setsid();
	execv(cc->cc_command[0], cc->cc_command);
	exit(EXIT_FAILURE);
}

/*
 * start process.
 */
static int
spawn(struct child_config *cc, int instance)
{
	pid_t		pid;

	struct process	*p;

	if((pid = fork()) == -1) {
		return 0;
	}

	if (pid == 0) {
		spawn_child(cc);
		/* not reached */
	}

	p = xmalloc(sizeof(struct process));
	p->p_pid = pid;
	p->p_child_config = cc;
	p->p_instance = instance;
	schedule_heartbeat(p);
	process_insert(p);

	return 1;
}

/*
 * heartbeat timer callback.
 */
static void
heartbeat_cb(int unused0 __attribute__((unused)),
		short unused1 __attribute__((unused)),
		void *vp)
{
	struct process		*p;
	struct child_config	*cc;
	pid_t			pid;
	char			*args[5],
				pid_str[8],
				inst_str[8];

	if (vp == NULL)
		return;

	p = vp;
	cc = p->p_child_config;

	if (cc == NULL)
		return;

	if (cc->cc_heartbeat == NULL)
		return;

	snprintf(pid_str, sizeof(pid_str), "%d", p->p_pid);
	snprintf(inst_str, sizeof(inst_str), "%d", p->p_instance);

	schedule_heartbeat(p);
	pid = fork();

	if (pid == -1) {
		slog("heartbeat spawn error.\n");
		return;
	}

	if (pid == 0) {
		args[0] = cc->cc_heartbeat;
		args[1] = cc->cc_name;
		args[2] = pid_str;
		args[3] = inst_str;
		args[4] = NULL;
		execv(args[0], args);
		exit(EXIT_FAILURE);
	}
}

/*
 * signal handler
 */
static void
signal_cb(int unused0 __attribute__((unused)),
		short unused1 __attribute__((unused)),
		void *unused2 __attribute__((unused)))
{
	pid_t			pid;
	int			ret,
				inst;
	struct process		*p;
	struct child_config	*cc;
	time_t			t;


	while ((pid = waitpid(-1, &ret, WNOHANG)) > 0) {
		if ((p = process_find(pid)) == NULL)
			return;

		cc = p->p_child_config;
		inst = p->p_instance;
		slog("[exit] pid: %d\n", pid);
		process_remove(p);
		evtimer_del(&(p->p_heartbeat_timer));
		free(p);
		if (cc) {
			if ((WIFEXITED(ret) && WEXITSTATUS(ret) != 0)
					|| (WIFSIGNALED(ret)
					&& WTERMSIG(ret) == cc->cc_killsig)) {
				t = time(NULL);
				if (cc->cc_errtime + ERROR_PERIOD < t)
					cc->cc_error = 0;
				cc->cc_error++;
				cc->cc_errtime = t;

				if (cc->cc_error >= (ERROR_MAX * cc->cc_instances)) {
					cc->cc_status = STATUS_BROKEN;
					slog("spawn failures. setting broken on %s\n", cc->cc_name);
					run_fatal_cb(cc);
				}
			}
			if (inst < cc->cc_instances && cc->cc_status == STATUS_RUNNING)
				spawn(cc, inst);
		}
	}
}

/*
 * utility function to send a status message to client.
 */
static void
send_status_msg(struct client_con *con, int code, const char *msg)
{
	const char		*ret;
	ssize_t			ret_len;
	uint16_t		len;
	json_object		*obj,
				*c,
				*m;

	obj = json_object_new_object();

	c = json_object_new_boolean(code);
	json_object_object_add(obj, "code", c);

	m = json_object_new_string(msg);
	json_object_object_add(obj, "msg", m);

	ret = json_object_to_json_string(obj);
	ret_len = strlen(ret);

	len = htons(ret_len);

	if (bufferevent_write(con->c_be, &len, sizeof(uint16_t)) == -1) {
		slog("write failed in send_status_msg (len)\n");
	}

	if (bufferevent_write(con->c_be, &con->c_cid, sizeof(uint16_t)) == -1) {
		slog("write failed in send_status_msg\n");
	}

	if (bufferevent_write(con->c_be, ret, ret_len) == -1) {
		slog("write failed in send_status_msg\n");
	}

	json_object_put(obj);
}

/*
 * list command handler.
 */
static int
c_list(struct client_con *con)
{
	json_object		*obj,
				*p;
	struct child_config	*i;
	uint16_t		len;
	const char		*ret;
	ssize_t			ret_len;


	slog("[list]\n");
	if ((obj = json_object_new_array()) == NULL) {
		send_status_msg(con, 0, "failed");
		return 1;
	}

	LIST_FOREACH (i, &child_config_list_head, cc_ent) {
		if ((p = json_object_new_string(i->cc_name)) == NULL) {
			send_status_msg(con, 0, "failed");
			return 1;
		}
		json_object_array_add(obj, p);
	}

	if ((ret = json_object_to_json_string(obj)) == NULL) {
		send_status_msg(con, 0, "failed");
		return 1;
	}

	ret_len = strlen(ret);
	len = htons(ret_len);

	if (bufferevent_write(con->c_be, &len, sizeof(uint16_t)) == -1) {
		slog("write failed in send_status_msg\n");
	}

	if (bufferevent_write(con->c_be, &con->c_cid, sizeof(uint16_t)) == -1) {
		slog("write failed in send_status_msg\n");
	}

	if (bufferevent_write(con->c_be, ret, ret_len) == -1) {
		slog("write failed in send_status_msg\n");
	}

	json_object_put(obj);
	return 1;
}

/*
 * start command handler.
 */
static int
c_spwn(struct client_con *con, char *buf)
{
	int			i;
	struct child_config	*cc;

	slog("[start]\n");
	if ((cc = child_config_unserialize(buf)) == NULL) {
		send_status_msg(con, 0, "failure");
		return 0;
	}

	if (cc->cc_name == NULL) {
		send_status_msg(con, 0, "need name");
		child_config_free(cc);
		return 1;
	}

	if (cc->cc_command == NULL) {
		send_status_msg(con, 0, "need command");
		child_config_free(cc);
		return 1;
	}

	if (child_config_find_by_name(cc->cc_name) != NULL) {
		send_status_msg(con, 0, "name exists");
		child_config_free(cc);
		return 1;
	}

	child_config_insert(cc);
	if (!auto_dump)
		send_status_msg(con, 1, "success");
	else
		c_dump(con);

	if (cc->cc_status != STATUS_RUNNING)
		return 1;

	for (i = 0; i < cc->cc_instances; i++)
		spawn(cc, i);
	return 1;
}

/*
 * update command handler.
 */
static int
c_updt(struct client_con *con, char *buf)
{
	int			i;
	struct child_config	*cc,
				*up;

	slog("[update]\n");
	if ((cc = child_config_unserialize(buf)) == NULL) {
		slog("[update] parse error\n");
		send_status_msg(con, 0, "failure");
		return 0;
	}

	if (cc->cc_name == NULL) {
		send_status_msg(con, 0, "need name");
		child_config_free(cc);
		return 0;
	}

	if ((up = child_config_find_by_name(cc->cc_name)) == NULL) {
		send_status_msg(con, 0, "not found");
		child_config_free(cc);
		return 0;
	}


	if (cc->cc_uid != -1) {
		send_status_msg(con, 1, "cannot update uid");
		child_config_free(cc);
		return 1;
	}

	if (cc->cc_gid != -1) {
		send_status_msg(con, 1, "cannot update gid");
		child_config_free(cc);
		return 1;
	}

	if (cc->cc_command != NULL) {
		send_status_msg(con, 0, "command cannot be updated");
		child_config_free(cc);
		return 1;
	}

	if (cc->cc_dir != NULL) {
		slog("[update] dir \"%s\" -> \"%s\"\n", up->cc_dir,
				cc->cc_dir);
		if (up->cc_dir)
			free(up->cc_dir);
		up->cc_dir = xstrdup(cc->cc_dir);
	}

	if (cc->cc_heartbeat != NULL) {
		slog("[update] heartbeat \"%s\" -> \"%s\"\n", up->cc_heartbeat,
				cc->cc_heartbeat);
		if (up->cc_heartbeat)
			free(up->cc_heartbeat);
		up->cc_heartbeat = xstrdup(cc->cc_heartbeat);
	}

	if (cc->cc_fatal_cb != NULL) {
		slog("[update] fatal_cb \"%s\" -> \"%s\"\n", up->cc_fatal_cb,
				cc->cc_fatal_cb);
		if (up->cc_fatal_cb)
			free(up->cc_fatal_cb);
		up->cc_fatal_cb = xstrdup(cc->cc_fatal_cb);
	}

	if (cc->cc_stdout != NULL) {
		slog("[update] stdout \"%s\" -> \"%s\"\n", up->cc_stdout,
				cc->cc_stdout);
		if (up->cc_stdout != NULL)
			free(up->cc_stdout);
		up->cc_stdout = xstrdup(cc->cc_stdout);
	}

	if (cc->cc_stderr != NULL) {
		slog("[update] stderr \"%s\" -> \"%s\"\n", up->cc_stderr,
				cc->cc_stderr);
		if (up->cc_stderr != NULL)
			free(up->cc_stderr);
		up->cc_stderr = xstrdup(cc->cc_stderr);
	}

	if (cc->cc_killsig != -1) {
		slog("[update] killsig %d -> %d\n", up->cc_killsig, cc->cc_killsig);
		up->cc_killsig = cc->cc_killsig;
	}

	if (cc->cc_instances != -1) {
		slog("[update] instances %d -> %d\n", up->cc_instances, cc->cc_instances);
		if (cc->cc_instances > up->cc_instances) {
			i = up->cc_instances;
			up->cc_instances = cc->cc_instances;
			for (; i < up->cc_instances; i++)
				spawn(up, i);
		} else {
			/* XXX: maybe return pids for cc->cc_instances > up->cc_instances */
			up->cc_instances = cc->cc_instances;
		}
	}

	if (cc->cc_status != -1) {
		slog("[update] status %d -> %d\n", up->cc_status, cc->cc_status);
		up->cc_error = 0;
		if (up->cc_status != STATUS_RUNNING
				&& cc->cc_status == STATUS_RUNNING) {
			for (i = 0; i < up->cc_instances; i++) {
				if (process_find_instance(up, i) == NULL)
					spawn(up, i);
			}
		}
		up->cc_status = cc->cc_status;
	}

	child_config_free(cc);

	if (!auto_dump)
		send_status_msg(con, 1, "success");
	else
		c_dump(con);
	return 1;
}

/*
 * exit command handler.
 */
static int
c_exit(struct client_con *con)
{
	slog("[exit]\n");

	if (!allow_exit) {
		send_status_msg(con, 0, "prohibited");
		return 1;
	}

	send_status_msg(con, 1, "exiting");
	exit(0);
	return 1;
}

/*
 * dump command handler.
 */
static int
c_dump(struct client_con *con)
{
	static int		cnt = 0;
	struct child_config	*i;
	FILE			*fo;
	char 			fname[PATH_MAX];
	int			r;
	char			*ptr;
	char			time_buf[64];
	struct tm		*t;
	time_t			tt;

	tt = time(NULL);
	t = gmtime(&tt);
	strftime(time_buf, sizeof(time_buf), "%b_%d_%H_%M_%S", t);

	r = snprintf(fname, PATH_MAX, DUMP_PATH, cnt++,
			geteuid(), time_buf);
	if (r < 0 || r >= PATH_MAX) {
		send_status_msg(con, 0, "failure");
		return 1;
	}

	if ((fo = fopen(fname, "w")) == NULL) {
		send_status_msg(con, 0, "failure");
		return 1;
	}

	slog("[dump]\n");

	fprintf(fo, "[\n");
	LIST_FOREACH (i, &child_config_list_head, cc_ent) {
		if ((ptr = child_config_serialize(i)) == NULL)
			return 1;
		fprintf(fo, "%s,\n", ptr);
		free(ptr);
	}
	fprintf(fo, "]\n");
	fclose(fo);
	send_status_msg(con, 1, "dump successful.");
	return 1;
}

/*
 * kill command handler.
 */
static int
c_kill(struct client_con *con, char *buf)
{
	char			*ret = NULL;
	const char 		*n;

	ssize_t			ret_len;

	uint16_t		len;

	struct child_config	*cc;
	struct process		*i;

	json_object		*obj,
				*c,
				*m,
				*p;

	int			sig;

	slog("[kill]\n");
	if ((obj = json_tokener_parse(buf)) == NULL) {
		send_status_msg(con, 0, "failure");
		return 0;
	}

	if (is_error(obj)) {
		send_status_msg(con, 0, "failure");
		return 0;
	}

	if (!json_object_is_type(obj, json_type_object)) {
		json_object_put(obj);
		send_status_msg(con, 0, "failure");
		return 0;
	}

	if ((m = json_object_object_get(obj, "name")) == NULL) {
		json_object_put(obj);
		send_status_msg(con, 0, "failure");
		return 1;
	}

	if (!json_object_is_type(m, json_type_string)) {
		json_object_put(obj);
		send_status_msg(con, 0, "failure");
		return 1;
	}

	if ((n = json_object_get_string(m)) == NULL) {
		json_object_put(obj);
		send_status_msg(con, 0, "failure");
		return 1;
	}

	if ((cc = child_config_find_by_name(n)) == NULL) {
		json_object_put(obj);
		send_status_msg(con, 0, "name not found");
		return 1;
	}

	sig = cc->cc_killsig;

	if ((m = json_object_object_get(obj, "sig")) != NULL) {
		if (json_object_is_type(m, json_type_int))
			sig = json_object_get_int(m);
	}

	json_object_put(obj);

	/* construct reply */
	obj = json_object_new_object();

	c = json_object_new_boolean(1);
	json_object_object_add(obj, "code", c);

	m = json_object_new_array();
	json_object_object_add(obj, "pids", m);

	LIST_FOREACH(i, &process_list_head, p_ent) {
		if (i->p_child_config == cc) {
			kill(i->p_pid, sig);
			p = json_object_new_int(i->p_pid);
			json_object_array_add(m, p);
		}
	}

	ret = xstrdup(json_object_to_json_string(obj));
	ret_len = strlen(ret);
	len = htons(ret_len);

	json_object_put(obj);

	if (bufferevent_write(con->c_be, &len, sizeof(uint16_t)) == -1) {
		slog("write failed in send_status_msg\n");
	}

	if (bufferevent_write(con->c_be, &con->c_cid, sizeof(uint16_t)) == -1) {
		slog("write failed in send_status_msg\n");
	}

	if (bufferevent_write(con->c_be, ret, ret_len) == -1) {
		slog("write failed in kill\n");
	}
	free(ret);
	return 1;
}

/*
 * delete command handler.
 */
static int
c_dele(struct client_con *con, char *buf)
{
	char			*ret = NULL;
	const char		*n;

	ssize_t			ret_len;
	uint16_t		len;

	struct child_config	*cc;
	struct process		*i;

	json_object		*obj,
				*c,
				*m,
				*p;


	slog("[dele]\n");
	if ((obj = json_tokener_parse(buf)) == NULL) {
		send_status_msg(con, 0, "failure");
		return 0;
	}

	if (is_error(obj)) {
		send_status_msg(con, 0, "failure");
		return 0;
	}

	if (!json_object_is_type(obj, json_type_object)) {
		send_status_msg(con, 0, "failure");
		return 0;
	}

	if ((m = json_object_object_get(obj, "name")) == NULL) {
		send_status_msg(con, 0, "failure");
		return 1;
	}

	if (!json_object_is_type(m, json_type_string)) {
		send_status_msg(con, 0, "failure");
		return 1;
	}

	if ((n = json_object_get_string(m)) == NULL) {
		send_status_msg(con, 0, "failure");
		return 1;
	}

	cc = child_config_find_by_name(n);
	json_object_put(obj);

	if (cc == NULL) {
		send_status_msg(con, 0, "name not found");
		return 1;
	}

	child_config_remove(cc);

	/* construct reply */
	if ((obj = json_object_new_object()) == NULL)
		return 1;

	if ((c = json_object_new_boolean(1)) == NULL)
		return 1;

	json_object_object_add(obj, "code", c);

	if ((m = json_object_new_array()) == NULL)
		return 1;

	json_object_object_add(obj, "pids", m);

	LIST_FOREACH(i, &process_list_head, p_ent) {
		if (i->p_child_config == cc) {
			i->p_child_config = NULL;
			if ((p = json_object_new_int(i->p_pid)) == NULL)
				return 1;
			json_object_array_add(m, p);
		}
	}

	child_config_free(cc);

	ret = xstrdup(json_object_to_json_string(obj));
	ret_len = strlen(ret);
	len = htons(ret_len);

	json_object_put(obj);

	if (bufferevent_write(con->c_be, &len, sizeof(uint16_t)) == -1) {
		slog("write failed in send_status_msg\n");
	}

	if (bufferevent_write(con->c_be, &con->c_cid, sizeof(uint16_t)) == -1) {
		slog("write failed in send_status_msg\n");
	}

	if (bufferevent_write(con->c_be, ret, ret_len) == -1) {
		slog("write failed in kill\n");
	}

	free(ret);
	return 1;
}

/*
 * get command handler.
 */
static int
c_getc(struct client_con *con, char *buf)
{
	char			*ret = NULL;
	const char		*n;

	ssize_t			ret_len;
	uint16_t		len;

	struct child_config	*cc;

	json_object		*obj,
				*m;


	slog("[getc]\n");
	if ((obj = json_tokener_parse(buf)) == NULL) {
		send_status_msg(con, 0, "failure");
		return 0;
	}

	if (is_error(obj)) {
		send_status_msg(con, 0, "failure");
		return 0;
	}

	if (!json_object_is_type(obj, json_type_object)) {
		send_status_msg(con, 0, "failure");
		return 0;
	}

	if ((m = json_object_object_get(obj, "name")) == NULL) {
		send_status_msg(con, 0, "failure");
		return 0;
	}

	if (!json_object_is_type(m, json_type_string)) {
		send_status_msg(con, 0, "failure");
		return 0;
	}

	if ((n = json_object_get_string(m)) == NULL) {
		send_status_msg(con, 0, "failure");
		return 0;
	}

	cc = child_config_find_by_name(n);
	json_object_put(obj);

	if (cc == NULL) {
		send_status_msg(con, 0, "name not found");
		return 1;
	}

	ret = child_config_serialize(cc);
	ret_len = strlen(ret);
	len = htons(ret_len);

	if (bufferevent_write(con->c_be, &len, sizeof(uint16_t)) == -1) {
		slog("write failed in send_status_msg\n");
	}

	if (bufferevent_write(con->c_be, &con->c_cid, sizeof(uint16_t)) == -1) {
		slog("write failed in send_status_msg\n");
	}

	if (bufferevent_write(con->c_be, ret, ret_len) == -1) {
		slog("write failed in getc\n");
	}
	free(ret);
	return 1;
}

/*
 */
static void
run_server_command(char *buf, struct client_con *c)
{
	int			cr = 0;
	char			*cmd_buf;


	/* alive check */
	if (!strcmp(buf, "HELO")) {
		if (bufferevent_write(c->c_be, "HELO", 4) == -1)
			slog("HELO failed.\n");
		return;
	}

	/* commands without payload */
	if (!strcmp(buf, "EXIT")) {
		c_exit(c);
		return;
	} else if (!strcmp(buf, "LIST")) {
		c_list(c);
		return;
	} else if (!strcmp(buf, "DUMP")) {
		c_dump(c);
		return;
	}

	cmd_buf = buf + 4;

	if (!strncmp(buf, "SPWN", 4))
		cr = c_spwn(c, cmd_buf);
	else if (!strncmp(buf, "DELE", 4))
		cr = c_dele(c, cmd_buf);
	else if (!strncmp(buf, "KILL", 4))
		cr = c_kill(c, cmd_buf);
	else if (!strncmp(buf, "UPDT", 4))
		cr = c_updt(c, cmd_buf);
	else if (!strncmp(buf, "GETC", 4))
		cr = c_getc(c, cmd_buf);
	if (!cr)
		drop_client_connection(c);
}

/*
 */
static void
read_cb(struct bufferevent *b, void *cx)
{
	char			buf[BUFFER_SIZ];
	uint16_t		len,
				cid;
	size_t			r;
	struct client_con	*c = cx;


	if (c->c_len == 0 && c->c_cid == 0) {
		if (bufferevent_read(b, &len, 2) != 2) {
			slog("read len failed.\n");
			drop_client_connection(c);
			return;
		}

		len = ntohs(len);

		if (len > BUFFER_SIZ) {
			drop_client_connection(c);
			return;
		}

		c->c_len = len;
		bufferevent_setwatermark(b, EV_READ, 2, BUFFER_SIZ);
	}

	if(c->c_len > 0 && c->c_cid == 0) {
		if (bufferevent_read(b, &cid, 2) != 2) {
			slog("read cid failed.\n");
			drop_client_connection(c);
			return;
		}
		c->c_cid = cid;
		bufferevent_setwatermark(b, EV_READ, c->c_len, BUFFER_SIZ);
	}

	if (c->c_len > 0 && c->c_cid != 0) {
		if (EVBUFFER_LENGTH(b->input) < c->c_len)
			return;

		if ((r = bufferevent_read(b, buf, c->c_len)) != c->c_len) {
			slog("read payload failed.\n");
			drop_client_connection(c);
			return;
		}

		bufferevent_setwatermark(c->c_be, EV_READ, 2, BUFFER_SIZ);
		c->c_len = 0;
		buf[r] = '\0';
		run_server_command(buf, c);
		c->c_cid = 0;
		return;
	}
}

static void
error_cb(struct bufferevent *b __attribute__((unused)), short what __attribute__((unused)), void *cx)
{
	struct client_con	*c = cx;
	slog("dropping client\n");
	drop_client_connection(c);
}

/*
 * accept connection from socket and hand it over to libevent.
 */
static void
accept_cb(int fd, short evtype, void *unused __attribute__((unused)))
{
	int			s;
	struct client_con	*c;


	if ((evtype & EV_READ) == 0)
		return;

	if ((s = accept(fd, NULL, 0)) == -1) {
		if (errno != EWOULDBLOCK && errno != EAGAIN)
			slog("Failed to accept a connection\n");
		return;
	}

	setcloseonexec(s);
	setnonblock(s);
	c = xmalloc(sizeof (struct client_con));
	c->c_sock = s;
	c->c_len = 0;
	c->c_cid = 0;

	if ((c->c_be = bufferevent_new(s, read_cb, NULL, error_cb, c)) == NULL) {
		free(c);
		slog("bufferevent_new failed.\n");
		close(s);
		return;
	}

#if 0
	if (bufferevent_base_set(evloop, c->c_be) == -1) {
		bufferevent_free(c->c_be);
		free(c);
		slog("bufferevent_base_set failed.\n");
		close(s);
		return;
	}
#endif

	if (bufferevent_enable(c->c_be, EV_READ | EV_WRITE) == -1) {
		bufferevent_free(c->c_be);
		free(c);
		slog("bufferevent_enable failed.\n");
		close(s);
		return;
	}

	bufferevent_setwatermark(c->c_be, EV_READ, 4, BUFFER_SIZ);
	LIST_INSERT_HEAD(&client_con_list_head, c, c_ent);
}

/*
 * Read dump (as created by the DUMP command) and restore process groups.
 */
static int
load_dump(char *fname)
{
	char			*buf;
	int			buf_siz,
				i,
				j,
				len;
	FILE			*f;

	struct child_config	*cc;

	json_object		*obj,
				*t;

	if ((f = fopen(fname, "r")) == NULL)
		return 0;

	fseek(f, 0, SEEK_END);
	buf_siz = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = xmalloc(buf_siz + 1);
	if (fread(buf, buf_siz, 1, f) != 1)
		die("fread");
	buf[buf_siz] = '\0';

	if ((obj = json_tokener_parse(buf)) == NULL)
		return 0;
	if (is_error(obj))
		return 0;
	if (!json_object_is_type(obj, json_type_array))
		return 0;

	len = json_object_array_length(obj);
	for (i = 0; i < len; i++) {
		t = json_object_array_get_idx(obj, i);
		if ((cc = child_config_from_json(t)) == NULL)
			return 0;
		slog("load: %s\n", cc->cc_name);
		child_config_insert(cc);
		if (cc->cc_status == STATUS_RUNNING) {
			for (j = 0; j < cc->cc_instances; j++)
				spawn(cc, j);
		}
	}
	json_object_put(obj);
	fclose(f);
	return 1;
}

/*
 * search current dir for dumps and load the newest.
 */
static void
load_newest_dump(void) {
	DIR		*dh;
	struct dirent	*d;
	struct stat	s;
	long		t = 0;
	char		name[PATH_MAX];

	name[0] = '\0';
	if ((dh = opendir(".")) == NULL)
		die("opendir");

	while ((d = readdir(dh)) != NULL) {
		if (!strncmp("uberdump", d->d_name, 8)) {
			if (stat(d->d_name, &s) == -1)
				continue;
			if (s.st_mtime > t) {
				if (strlen(d->d_name) < PATH_MAX) {
					t = s.st_mtime;
					strcpy(name, d->d_name);
				}
			}
		}
	}
	if (strlen(name)) {
		printf("loading dump from %s\n", name);
		load_dump(name);
	}
	closedir(dh);
}

/*
 * server main function.
 */
int
cmd_server(int argc, char **argv)
{
	struct event		ev, se;
	struct sockaddr_un	addr;
	struct passwd		*pw;
	struct stat		st;
	socklen_t		addr_len;
	char			sock_path[PATH_MAX],
				*dump_file = NULL,
				*sock_path_ptr,
				*dir = NULL,
				*logfile = NULL;
	int			fd,
				do_fork = 1,
				silent = 0,
				load_latest = 0,
				ch,
				r;
	pid_t			pid;
	struct sigaction	sa;


	if ((pw = getpwuid(geteuid())) == NULL)
		die("getpwuid");

#ifdef HAVE_SETPROCTITLE
	if (pw != NULL)
		setproctitle("%s", pw->pw_name);
#endif

	/* init globals */
	auto_dump = 0;
	allow_exit = 1;
	log_fd = stdout;
	LIST_INIT(&process_list_head);
	LIST_INIT(&child_config_list_head);
	LIST_INIT(&client_con_list_head);

	while ((ch = getopt_long(argc, argv, server_opts, server_longopts, NULL)) != -1) {
		switch (ch) {
		case 'a':
			auto_dump ^= 1;
			break;
		case 'c':
			dump_file = optarg;
			break;
		case 'd':
			dir = optarg;
			break;
		case 'e':
			allow_exit ^= 1;
			break;
		case 'f':
			do_fork ^= 1;
			break;
		case 'h':
			help_server();
			break;
		case 'l':
			load_latest = 1;
			break;
		case 'o':
			logfile = optarg;
			break;
		case 's':
			silent ^= 1;
			break;
		default:
			help_server();
			break;
		}
	}

	argc -= optind;

	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	if (sigemptyset(&sa.sa_mask) == -1 || sigaction(SIGPIPE, &sa, 0) == -1)
		die("sigaction");

	if (argc != 0)
		help_server();

	if (dump_file && load_latest) {
		fprintf(stderr, "-l and -c are mutually exclusive\n");
		return 1;
	}

	if (getenv("UBERVISOR_RSH") != NULL) {
		fprintf(stderr, "unsetting UBERVISOR_RSH.\n");
		unsetenv("UBERVISOR_RSH");
	}

	if (sock_send_helo() != -1) {
		if (!silent)
			fprintf(stderr, "server running?\n");
		return 1;
	}

	if (dir) {
		printf("chdir to: %s\n", dir);
		if (chdir(dir) == -1)
			die("chdir");
	} else {
		r = snprintf(sock_path, PATH_MAX, BASE_DIR, pw->pw_dir);
		if (r < 0 || r >= PATH_MAX)
			die("snprintf");

		if (stat(sock_path, &st) == -1) {
			if (mkdir(sock_path, S_IRWXU) == -1)
				die("mkdir");
		}
		printf("chdir to: %s\n", sock_path);
		if (chdir(sock_path) == -1)
			die("chdir");
	}

	/* log file */
	if (logfile == NULL) {
		r = snprintf(sock_path, PATH_MAX, LOG_PATH, pw->pw_dir);
		if (r < 0 || r >= PATH_MAX)
			die("snprintf");
		logfile = xstrdup(sock_path);
	}

	if (do_fork)
		printf("logfile: %s\n", logfile);

	/* socket file in dir */
	if ((sock_path_ptr = getenv("UBERVISOR_SOCKET")) == NULL) {
		r = snprintf(sock_path, PATH_MAX, SOCK_PATH, pw->pw_dir);
		if (r < 0 || r >= PATH_MAX)
			die("snprintf");
		sock_path_ptr = sock_path;
	}

	unlink(sock_path_ptr);

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		die("socket");

	if (strlen(sock_path_ptr) >= sizeof(addr.sun_path)) {
		fprintf(stderr, "socket path too long\n");
		return 1;
	}

	memset(&addr, '\0', sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, sock_path_ptr);
	addr_len = sizeof(addr);

	if (bind(fd, (struct sockaddr *) &addr, addr_len) == -1)
		die("bind");

	listen(fd, 8);
	printf("socket: %s\n", sock_path_ptr);

	if (do_fork) {
		pid = fork();

		if (pid == -1)
			die("fork");

		if (pid != 0)
			return EXIT_SUCCESS;

		setsid();
	}

	/* can't do this before fork */
	event_init();

	if ((evloop = event_base_new()) == NULL)
		die("event_base_new");

	/* loading a dump will start the processes - must do this after
	 * event_init */
	if (dump_file) {
		if (!load_dump(dump_file))
			die("dump_file");
	}

	if (load_latest)
		load_newest_dump();

	if (logfile != NULL && do_fork) {
		if ((log_fd = fopen(logfile, "a")) == NULL)
			die("fopen");
		r = fileno(log_fd);
		setcloseonexec(r);
	}

	setcloseonexec(fd);

	event_set(&ev, fd, EV_READ | EV_PERSIST, accept_cb, NULL);
	event_add(&ev, NULL);

	event_set(&se, SIGCHLD, EV_SIGNAL | EV_PERSIST, signal_cb, NULL);
	event_add(&se, NULL);

	event_dispatch();
	return EXIT_SUCCESS;
}
