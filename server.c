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
#include <grp.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>

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
#include "subscription.h"
#include "process.h"
#include "uvhash.h"
#include "server.h"

#ifdef HAVE_SYS_QUEUE_H
#include <sys/queue.h>
#else
#include "compat/queue.h"
#endif

/*
 * types
 */
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
static LIST_HEAD(client_con_list, client_con)		client_con_list_head;

static struct event_base	*evloop;
static FILE			*log_fd;
static int			auto_dump,
				allow_exit;

/*
 * prototypes
 */
static void heartbeat_cb(int, short, void *);
static void slog(const char *, ...);

static int c_dele(struct client_con *, char *);
static int c_dump(struct client_con *, char *);
static int c_exit(struct client_con *, char *);
static int c_getc(struct client_con *, char *);
static int c_helo(struct client_con *, char *);
static int c_kill(struct client_con *, char *);
static int c_list(struct client_con *, char *);
static int c_pids(struct client_con *, char *);
static int c_spwn(struct client_con *, char *);
static int c_subs(struct client_con *, char *);
static int c_updt(struct client_con *, char *);
static int c_read(struct client_con *, char *);

typedef int (*cfunc_t)(struct client_con *, char *);

struct commands_s {
	char		c_name[4];
	cfunc_t		c_func;
};

struct commands_s commands[] = {
	{"DELE",	c_dele},
	{"DUMP",	c_dump},
	{"EXIT",	c_exit},
	{"GETC",	c_getc},
	{"HELO",	c_helo},
	{"KILL",	c_kill},
	{"LIST",	c_list},
	{"PIDS",	c_pids},
	{"READ",	c_read},
	{"SPWN",	c_spwn},
	{"SUBS",	c_subs},
	{"UPDT",	c_updt},
};

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

#define LOG_TS_FORMAT	"%b %d %T"

/* logfile create mode */
#define _LO_C		(O_APPEND | O_CREAT | O_WRONLY)

/* logfile open mode */
#define _LO_O 		(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

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
	bufferevent_disable(c->c_be, EV_READ | EV_WRITE);
	bufferevent_free(c->c_be);
	close(c->c_sock);
	subscription_remove_for_client(c);
	LIST_REMOVE(c, c_ent);
	free(c);
}


static int
send_message(struct client_con *con, const char *ret, size_t ret_len)
{
	uint16_t		len;
	size_t			r;
	size_t			off = 0;


	do {
		if ((ret_len - off) > CHUNKSIZ) {
			len = htons(CHUNKEXT | CHUNKSIZ);
			r = CHUNKSIZ;
		} else {
			len = htons(ret_len - off);
			r = ret_len - off;
		}

		if (bufferevent_write(con->c_be, &len, sizeof(uint16_t)) == -1) {
			slog("write failed in send_message (len)\n");
			return 0;
		}

		if (bufferevent_write(con->c_be, &con->c_cid, sizeof(uint16_t)) == -1) {
			slog("write failed in send_message\n");
			return 0;
		}

		if (bufferevent_write(con->c_be, ret + off, r) == -1) {
			slog("write failed in send_message\n");
			return 0;
		}
		off += r;
	} while (off < ret_len);

	return 1;
}


/*
 * send notification message to subscribed clients.
 */
static void
send_notification(int n, const char *ret)
{
	struct subscription	*s;
	uint16_t		len;

	int			ret_len;

	ret_len = strlen(ret);
	assert(ret_len <= CHUNKSIZ);
	len = htons(ret_len);

	s = LIST_FIRST(&subscription_list_head);
	while (s != NULL) {
		if ((s->s_ident & n) != 0) {
			do {
				if (bufferevent_write(s->s_client->c_be,
							&len,
							sizeof(uint16_t)) == -1)
					break;
				if (bufferevent_write(s->s_client->c_be,
							&s->s_channel,
							sizeof(uint16_t)) == -1)
					break;
				bufferevent_write(s->s_client->c_be, ret, ret_len);
			} while (0);
		}
		s = LIST_NEXT(s, s_ent);
	}
}


/*
 * send log file notification.
 */
static void
send_log_notification(const char *msg)
{
	const char		*ret;

	json_object		*obj,
				*c;

	if ((obj = json_object_new_object()) == NULL)
		return;

	if ((c = json_object_new_string(msg)) == NULL)
		return;

	json_object_object_add(obj, "msg", c);

	ret = json_object_to_json_string(obj);
	send_notification(SUBS_SERVER, ret);
	json_object_put(obj);
}

/*
 * write message to server log and send message to clients subscribed to
 * server messages.
 */
static void
slog(const char *fmt, ...)
{
	va_list		ap;
	char		ts_buf[64],
			msg_buf[BUFFER_SIZ],
			out[BUFFER_SIZ];
	int		r;
	struct tm	*t;
	time_t		tt;

	tt = time(NULL);
	t = gmtime(&tt);
	strftime(ts_buf, sizeof(ts_buf), LOG_TS_FORMAT, t);

	va_start(ap, fmt);
	r = vsnprintf(msg_buf, BUFFER_SIZ, fmt, ap);
	va_end(ap);

	if (r < 0) {
		slog("slog failed.\n");
		return;
	}

	r = snprintf(out, BUFFER_SIZ, "%s -- %s", ts_buf, msg_buf);

	if (r < 0) {
		slog("slog failed.\n");
		return;
	}

	if (r >= BUFFER_SIZ) {
		slog("too big log message is being truncated.\n");
		r = BUFFER_SIZ;
		out[BUFFER_SIZ - 2] = '\n';
	}

	fwrite(out, r, 1, log_fd);
	send_log_notification(out);
}

/*
 * send notification about group status update.
 */
static void
send_status_update_notification(const char *name, int status)
{
	json_object		*obj,
				*t;
	const char		*str;

	obj = json_object_new_object();

	t = json_object_new_string(name);
	json_object_object_add(obj, "name", t);

	t = json_object_new_int(status);
	json_object_object_add(obj, "status", t);

	str = json_object_to_json_string(obj);
	send_notification(SUBS_STATUS, str);

	json_object_put(obj);
}

/*
 * run fatal callback command for a group.
 */
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

	slog("running fatal_cb \"%s\" for %s ...\n", cc->cc_fatal_cb, cc->cc_name);
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
	struct passwd		*pw;
	struct group		*gr;

	if (cc->cc_username != NULL) {
		if ((pw = getpwnam(cc->cc_username)) == NULL)
			exit(1);
		cc->cc_uid = pw->pw_uid;
	}

	if (cc->cc_groupname != NULL) {
		 if ((gr = getgrnam(cc->cc_groupname)) == NULL)
			 exit(1);
		 cc->cc_gid = gr->gr_gid;
	}

	if (cc->cc_gid != -1) {
		if (setgid(cc->cc_gid) != 0)
			exit(1);

		if (setegid(cc->cc_gid) != 0)
			exit(1);
	}

	if (cc->cc_uid != -1) {
		if (setuid(cc->cc_uid) != 0)
			exit(1);

		if (seteuid(cc->cc_uid) != 0)
			exit(1);

		if (cc->cc_uid != 0) {
			if (setuid(0) != -1)
				exit(1);
		}
	}

	if (cc->cc_gid > 0) {
		if (setgid(0) != -1)
			exit(1);
	}
}

/*
 * Inplace substring replace. Only handling the cases where strlen(b) <=
 * strlen(a) - which is fine for the purpose this is used for: replacing
 * the instance number in log files.
 */
static void
replace_str(char *in, const char *a, const char *b)
{
	char		*in_ptr;
	size_t		a_len,
			b_len,
			in_len;

	if ((in_ptr = strstr(in, a)) == NULL)
		return;

	a_len = strlen(a);
	b_len = strlen(b);
	if (b_len > a_len)
		return;
	in_len = strlen(in_ptr);
	memcpy(in_ptr, b, b_len);
	memmove(in_ptr + b_len, in_ptr + a_len, in_len - a_len + 1);
}

/*
 * try to log errors from spawn_child.
 *
 * We try to log to stderr, if defined. Otherwise, or if we can't open the
 * stderr logfile, we try stdout.
 */
static void
spawn_child_log(struct child_config *cc, const char *func) {
	int		fd;
	FILE		*f = NULL;

	if (cc->cc_stderr != NULL) {
		if ((fd = open(cc->cc_stderr, _LO_C, _LO_O)) != -1)
			f = fdopen(fd, "w");
	}

	if (!f && cc->cc_stdout != NULL) {
		if ((fd = open(cc->cc_stdout, _LO_C, _LO_O)) == -1) {
			exit(EXIT_FAILURE);
		}
		f = fdopen(fd, "w");
	}

	if (f) {
		fprintf(f, "ubervisor: spawn failed for \"%s\": %s: %s\n",
				cc->cc_name, func, strerror(errno));
		fclose(f);
	}
	exit(EXIT_FAILURE);
}

/*
 * setup child process. we are already forked here.
 */
static void
spawn_child(struct child_config *cc, int instance)
{
	int		stdout_fd = 0,
			stderr_fd = 0;
	char		instance_str[5];

	snprintf(instance_str, sizeof(instance_str), "%d", instance);
	spawn_child_setids(cc);

	if (cc->cc_dir != NULL) {
		if (chdir(cc->cc_dir) == -1)
			spawn_child_log(cc, "chdir");
	}
	close(0);
	close(1);
	close(2);

	if (cc->cc_stdout != NULL) {
		replace_str(cc->cc_stdout, "%(NUM)", instance_str);
		if ((stdout_fd = open(cc->cc_stdout, _LO_C, _LO_O)) == -1)
			spawn_child_log(cc, "open (stdout)");
		dup2(stdout_fd, STDOUT_FILENO);
		close(stdout_fd);
	}

	if (cc->cc_stderr != NULL) {
		replace_str(cc->cc_stderr, "%(NUM)", instance_str);
		if ((stderr_fd = open(cc->cc_stderr, _LO_C, _LO_O)) == -1)
			spawn_child_log(cc, "open (stderr)");
		dup2(stderr_fd, STDERR_FILENO);
		close(stderr_fd);
	}

	setsid();
	execv(cc->cc_command[0], cc->cc_command);
	spawn_child_log(cc, "execv");
}

/*
 * start process.
 */
static int
spawn(struct child_config *cc, int instance)
{
	pid_t		pid;

	struct process	*p;

	if ((pid = fork()) == -1) {
		return 0;
	}

	if (pid == 0) {
		spawn_child(cc, instance);
		/* not reached */
	}

	p = xmalloc(sizeof(struct process));
	p->p_pid = pid;
	p->p_child_config = cc;
	p->p_instance = instance;
	p->p_start = time(NULL);
	p->p_terminated = 0;
	p->p_age = cc->cc_age;
	schedule_heartbeat(p);
	process_insert(p);
	cc->cc_childs[instance] = p;
	slog("[process_start] %s pid: %d\n", cc->cc_name, pid);
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
	time_t			uptime;

	if (vp == NULL)
		return;

	p = vp;
	schedule_heartbeat(p);
	uptime = time(NULL) - p->p_start;

	if (p->p_age > 0 && uptime > p->p_age) {
		if (p->p_terminated) {
			slog("pid %d exceeded uptime. Sending KILL\n", p->p_pid);
			kill(p->p_pid, SIGKILL);
			return;
		}
		slog("pid %d exceeded uptime. Sending TERM\n", p->p_pid);
		kill(p->p_pid, SIGTERM);
		p->p_terminated = 1;
		return;
	}

	cc = p->p_child_config;

	if (cc == NULL || cc->cc_heartbeat == NULL)
		return;

	snprintf(pid_str, sizeof(pid_str), "%d", p->p_pid);
	snprintf(inst_str, sizeof(inst_str), "%d", p->p_instance);

	pid = fork();

	if (pid == -1) {
		slog("heartbeat spawn error in group %s.\n", cc->cc_name);
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
 * Return 1 if the exit conditon should be considered an error.
 */
static int
exit_is_error(int ret, struct child_config *cc)
{
	if ((WIFEXITED(ret) && WEXITSTATUS(ret) != 0)
			|| (WIFSIGNALED(ret)
			&& WTERMSIG(ret) == cc->cc_killsig)) {
		return 1;
	}
	return 0;
}

/*
 * libevent signal handler.
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
	char			*cc_name;

	while ((pid = waitpid(-1, &ret, WNOHANG)) > 0) {
		if ((p = process_find_by_pid(pid)) == NULL)
			return;

		cc = p->p_child_config;

		if (cc)
			cc_name = cc->cc_name;
		else
			cc_name = NULL;

		inst = p->p_instance;
		slog("[process_exit] %s pid: %d\n", cc_name, pid);
		process_remove(p);
		evtimer_del(&(p->p_heartbeat_timer));
		free(p);
		if (cc) {
			if (inst < cc->cc_instances)
				cc->cc_childs[inst] = NULL;
			if (exit_is_error(ret, cc)) {
				t = time(NULL);
				if (cc->cc_errtime + ERROR_PERIOD < t)
					cc->cc_error = 0;
				cc->cc_error++;
				cc->cc_errtime = t;

				if (cc->cc_error >= (ERROR_MAX * cc->cc_instances)) {
					cc->cc_status = STATUS_BROKEN;
					slog("spawn failures. setting broken on %s\n", cc->cc_name);
					send_status_update_notification(cc->cc_name, STATUS_BROKEN);
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

	send_message(con, ret, ret_len);

	json_object_put(obj);
}

/*
 * list command handler.
 */
static int
c_list(struct client_con *con, char *unused __attribute__((unused)))
{
	json_object		*obj,
				*p;
	struct child_config	*i;
	const char		*ret;
	ssize_t			ret_len;


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
	send_message(con, ret, ret_len);
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

	if (cc->cc_instances == -1)
	       cc->cc_instances = 1;

	if (cc->cc_instances < 1) {
		send_status_msg(con, 0, "instances > 0 required.");
		child_config_free(cc);
		return 1;
	}

	if (cc->cc_instances > MAX_INSTANCES) {
		send_status_msg(con, 0, "too many instances.");
		child_config_free(cc);
		return 1;
	}

	cc->cc_childs = xmalloc(sizeof(struct process *) * cc->cc_instances);
	memset(cc->cc_childs, '\0', sizeof(struct process *) * cc->cc_instances);
	child_config_insert(cc);
	if (!auto_dump)
		send_status_msg(con, 1, "success");
	else
		c_dump(con, NULL);

	slog("[start] creating group %s\n", cc->cc_name);
	send_status_update_notification(cc->cc_name, STATUS_CREATE);
	send_status_update_notification(cc->cc_name, cc->cc_status);
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

	if (cc->cc_instances != -1 && cc->cc_instances < 1) {
		send_status_msg(con, 0, "instances > 0 required.");
		child_config_free(cc);
		return 1;
	}

	if (cc->cc_instances > MAX_INSTANCES) {
		send_status_msg(con, 0, "too many instances.");
		child_config_free(cc);
		return 1;
	}

	if (cc->cc_dir != NULL) {
		slog("[update] %s dir \"%s\" -> \"%s\"\n", up->cc_name,
				up->cc_dir, cc->cc_dir);
		if (up->cc_dir)
			free(up->cc_dir);
		up->cc_dir = xstrdup(cc->cc_dir);
	}

	if (cc->cc_heartbeat != NULL) {
		slog("[update] %s heartbeat \"%s\" -> \"%s\"\n", up->cc_name,
				up->cc_heartbeat, cc->cc_heartbeat);
		if (up->cc_heartbeat)
			free(up->cc_heartbeat);
		up->cc_heartbeat = xstrdup(cc->cc_heartbeat);
	}

	if (cc->cc_fatal_cb != NULL) {
		slog("[update] %s fatal_cb \"%s\" -> \"%s\"\n", up->cc_name,
				up->cc_fatal_cb, cc->cc_fatal_cb);
		if (up->cc_fatal_cb)
			free(up->cc_fatal_cb);
		up->cc_fatal_cb = xstrdup(cc->cc_fatal_cb);
	}

	if (cc->cc_stdout != NULL) {
		slog("[update] %s stdout \"%s\" -> \"%s\"\n", up->cc_name,
				up->cc_stdout, cc->cc_stdout);
		if (up->cc_stdout != NULL)
			free(up->cc_stdout);
		up->cc_stdout = xstrdup(cc->cc_stdout);
	}

	if (cc->cc_stderr != NULL) {
		slog("[update] %s stderr \"%s\" -> \"%s\"\n", up->cc_name,
				up->cc_stderr, cc->cc_stderr);
		if (up->cc_stderr != NULL)
			free(up->cc_stderr);
		up->cc_stderr = xstrdup(cc->cc_stderr);
	}

	if (cc->cc_killsig != -1) {
		slog("[update] %s killsig %d -> %d\n", up->cc_name,
				up->cc_killsig, cc->cc_killsig);
		up->cc_killsig = cc->cc_killsig;
	}

	if (cc->cc_instances != -1) {
		slog("[update] %s instances %d -> %d\n", up->cc_name,
				up->cc_instances, cc->cc_instances);
		if (cc->cc_instances > up->cc_instances) {
			up->cc_childs = xrealloc(up->cc_childs,
					sizeof(struct process *) * cc->cc_instances);
			i = up->cc_instances;
			up->cc_instances = cc->cc_instances;
			for (; i < up->cc_instances; i++) {
				if (up->cc_status == STATUS_RUNNING) {
					spawn(up, i);
				} else {
					up->cc_childs[i] = NULL;
				}
			}
		} else {
			/* XXX: maybe return pids for cc->cc_instances > up->cc_instances */
			for (i = cc->cc_instances; i < up->cc_instances; i++) {
				if (up->cc_childs[i] != NULL)
					up->cc_childs[i]->p_child_config = NULL;
				up->cc_childs[i] = NULL;
			}
			up->cc_instances = cc->cc_instances;
			up->cc_childs = xrealloc(up->cc_childs,
					sizeof(struct process *) * up->cc_instances);
		}
	}

	if (cc->cc_status != -1) {
		slog("[update] %s status %d -> %d\n", up->cc_name,
				up->cc_status, cc->cc_status);
		up->cc_error = 0;
		if (up->cc_status != STATUS_RUNNING
				&& cc->cc_status == STATUS_RUNNING) {
			for (i = 0; i < up->cc_instances; i++) {
				if (process_find_instance(up, i) == NULL)
					spawn(up, i);
			}
		}
		up->cc_status = cc->cc_status;
		send_status_update_notification(up->cc_name, up->cc_status);
	}

	if (cc->cc_age > 0) {
		slog("[update] %s age %d -> %d\n", up->cc_name,
				up->cc_age, cc->cc_age);
		up->cc_age = cc->cc_age;
	}

	child_config_free(cc);

	if (!auto_dump)
		send_status_msg(con, 1, "success");
	else
		c_dump(con, NULL);
	return 1;
}

/*
 * exit command handler.
 */
static int
c_exit(struct client_con *con, char *unused __attribute__((unused)))
{
	if (!allow_exit) {
		send_status_msg(con, 0, "prohibited");
		return 1;
	}

	if (!auto_dump)
		send_status_msg(con, 1, "exiting");
	else
		c_dump(con, NULL);
	slog("server exiting due to exit command.\n");
	exit(0);
	return 1;
}

/*
 * dump command handler.
 */
static int
c_dump(struct client_con *con, char *unused __attribute__((unused)))
{
	static int		cnt = 0;
	struct child_config	*i;
	FILE			*fo;
	char 			fname[PATH_MAX],
				fname_tmp[PATH_MAX];
	int			r;
	char			*ptr;
	char			time_buf[64];
	struct tm		*t;
	time_t			tt;

	tt = time(NULL);
	t = gmtime(&tt);
	strftime(time_buf, sizeof(time_buf), "%b_%d_%H_%M_%S", t);

	cnt++;

	r = snprintf(fname, PATH_MAX, DUMP_PATH, cnt,
			geteuid(), time_buf);
	if (r < 0 || r >= PATH_MAX) {
		send_status_msg(con, 0, "failure");
		return 1;
	}

	r = snprintf(fname_tmp, PATH_MAX, "tmp." DUMP_PATH, cnt,
			geteuid(), time_buf);
	if (r < 0 || r >= PATH_MAX) {
		send_status_msg(con, 0, "failure");
		return 1;
	}

	if ((fo = fopen(fname_tmp, "w")) == NULL) {
		send_status_msg(con, 0, "failure");
		return 1;
	}

	fprintf(fo, "[\n");
	LIST_FOREACH (i, &child_config_list_head, cc_ent) {
		if ((ptr = child_config_serialize(i)) == NULL) {
			fclose(fo);
			send_status_msg(con, 0, "failure");
			return 1;
		}

		if (fprintf(fo, "%s,\n", ptr) < 0) {
			fclose(fo);
			send_status_msg(con, 0, "failure");
			free(ptr);
			return 1;
		}

		free(ptr);
	}

	if (fprintf(fo, "]\n") < 0) {
		fclose(fo);
		send_status_msg(con, 0, "failure");
	}

	fclose(fo);

	if (link(fname_tmp, fname) == -1) {
		send_status_msg(con, 0, "failure.");
		return 1;
	}

	unlink(fname_tmp);
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

	struct child_config	*cc;
	struct process		*i;

	json_object		*obj,
				*c,
				*m,
				*p;

	int			sig,
				x;

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
	slog("[kill] %s signal %d\n", cc->cc_name, sig);

	if ((m = json_object_object_get(obj, "sig")) != NULL) {
		if (json_object_is_type(m, json_type_int))
			sig = json_object_get_int(m);
	}

	json_object_put(obj);

	/* construct reply */
	if ((obj = json_object_new_object()) == NULL)
		return 1;

	if ((c = json_object_new_boolean(1)) == NULL)
		return 1;

	json_object_object_add(obj, "code", c);

	if ((m = json_object_new_array()) == NULL)
		return 1;

	json_object_object_add(obj, "pids", m);

	for (x = 0; x < cc->cc_instances; x++) {
		i = cc->cc_childs[x];
		if (i == NULL)
			continue;
		kill(i->p_pid, sig);
		if ((p = json_object_new_int(i->p_pid)) == NULL)
			return 1;
		json_object_array_add(m, p);
	}

	ret = xstrdup(json_object_to_json_string(obj));
	ret_len = strlen(ret);
	json_object_put(obj);
	send_message(con, ret, ret_len);
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

	struct child_config	*cc;
	struct process		*i;

	json_object		*obj,
				*c,
				*m,
				*p;

	int			x;

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
		json_object_put(obj);
		return 0;
	}

	if ((m = json_object_object_get(obj, "name")) == NULL) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 1;
	}

	if (!json_object_is_type(m, json_type_string)) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 1;
	}

	if ((n = json_object_get_string(m)) == NULL) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 1;
	}

	cc = child_config_find_by_name(n);
	json_object_put(obj);

	if (cc == NULL) {
		send_status_msg(con, 0, "name not found");
		return 1;
	}

	child_config_remove(cc);
	slog("[dele] %s\n", cc->cc_name);

	/* construct reply */
	if ((obj = json_object_new_object()) == NULL)
		return 1;

	if ((c = json_object_new_boolean(1)) == NULL)
		return 1;

	json_object_object_add(obj, "code", c);

	if ((m = json_object_new_array()) == NULL)
		return 1;

	json_object_object_add(obj, "pids", m);

	for (x = 0; x < cc->cc_instances; x++) {
		i = cc->cc_childs[x];
		if (i == NULL)
			continue;
		i->p_child_config = NULL;
		if ((p = json_object_new_int(i->p_pid)) == NULL)
			return 1;
		json_object_array_add(m, p);
	}

	send_status_update_notification(cc->cc_name, STATUS_DELETE);
	child_config_free(cc);

	ret = xstrdup(json_object_to_json_string(obj));
	ret_len = strlen(ret);
	json_object_put(obj);
	send_message(con, ret, ret_len);
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

	struct child_config	*cc;

	json_object		*obj,
				*m;


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
		json_object_put(obj);
		return 0;
	}

	if ((m = json_object_object_get(obj, "name")) == NULL) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	if (!json_object_is_type(m, json_type_string)) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	if ((n = json_object_get_string(m)) == NULL) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
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
	send_message(con, ret, ret_len);
	free(ret);
	return 1;
}

/*
 * channel subscription command handler.
 */
static int
c_subs(struct client_con *con, char *buf)
{
	json_object		*obj,
				*m;

	int			ident;
	struct subscription	*subs;


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
		json_object_put(obj);
		return 0;
	}

	if ((m = json_object_object_get(obj, "ident")) == NULL) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	if (!json_object_is_type(m, json_type_int)) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	ident = json_object_get_int(m);
	json_object_put(obj);

	subs = xmalloc(sizeof(struct subscription));
	subs->s_client = con;
	subs->s_ident = ident;
	subs->s_channel = con->c_cid;
	subscription_insert(&subscription_list_head, subs);
	send_status_msg(con, 1, "success");
	return 1;
}

/*
 * get pids for a group.
 */
static int
c_pids(struct client_con *con, char *buf)
{
	char			*ret = NULL;
	const char		*n;

	ssize_t			ret_len;

	struct child_config	*cc;
	struct process		*i;

	json_object		*obj,
				*c,
				*m,
				*p;

	int			x;

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
		json_object_put(obj);
		return 0;
	}

	if ((m = json_object_object_get(obj, "name")) == NULL) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	if (!json_object_is_type(m, json_type_string)) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	if ((n = json_object_get_string(m)) == NULL) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	cc = child_config_find_by_name(n);
	json_object_put(obj);

	if (cc == NULL) {
		send_status_msg(con, 0, "name not found");
		return 1;
	}

	/* construct reply */
	if ((obj = json_object_new_object()) == NULL)
		return 1;

	if ((c = json_object_new_boolean(1)) == NULL)
		return 1;

	json_object_object_add(obj, "code", c);

	if ((m = json_object_new_array()) == NULL)
		return 1;

	json_object_object_add(obj, "pids", m);

	for (x = 0; x < cc->cc_instances; x++) {
		i = cc->cc_childs[x];
		if (i == NULL)
			continue;
		if ((p = json_object_new_int(i->p_pid)) == NULL)
			return 1;
		json_object_array_add(m, p);
	}

	ret = xstrdup(json_object_to_json_string(obj));
	ret_len = strlen(ret);
	json_object_put(obj);
	send_message(con, ret, ret_len);
	free(ret);
	return 1;
}

/*
 * respond to helo command.
 */
static int
c_helo(struct client_con *con, char *unused __attribute__((unused)))
{
	if (bufferevent_write(con->c_be, "HELO", 4) == -1)
		slog("HELO failed.\n");
	return 1;
}

/*
 * read from stdout/stderr of a child.
 * (XXX: should read nonblocking).
 */
static int
c_read(struct client_con *con, char *buf)
{
	json_object		*obj,
				*m;

	const char		*ret;
	ssize_t			ret_len;

	double			doff;

	off_t			off,
				fsize;

	int			stream,
				bytes,
				instance,
				fd,
				r;

	char			instance_str[5];

	const char		*n;
	char			*fn = NULL,
				readbuf[16384];

	struct child_config	*cc;


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
		json_object_put(obj);
		return 0;
	}

	if ((m = json_object_object_get(obj, "name")) == NULL) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	if (!json_object_is_type(m, json_type_string)) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	if ((n = json_object_get_string(m)) == NULL) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	if ((m = json_object_object_get(obj, "stream")) == NULL) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	if (!json_object_is_type(m, json_type_int)) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	stream = json_object_get_int(m);

	if ((m = json_object_object_get(obj, "offset")) == NULL) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	/* json-c has only int and double as number types. If we used int
	 * here, we couldn't lseek past 2GB.
	 */
	if (!json_object_is_type(m, json_type_double)) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	doff = json_object_get_double(m);
	off = (off_t) doff;

	if ((m = json_object_object_get(obj, "bytes")) == NULL) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	if (!json_object_is_type(m, json_type_int)) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	bytes = json_object_get_int(m);

	if ((m = json_object_object_get(obj, "instance")) == NULL) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	if (!json_object_is_type(m, json_type_int)) {
		send_status_msg(con, 0, "failure");
		json_object_put(obj);
		return 0;
	}

	instance = json_object_get_int(m);

	if ((stream < 1 || stream > 2) \
			|| (bytes < 0 || bytes > (int) (sizeof(readbuf) - 1))) {
		send_status_msg(con, 0, "parameters out of bounds.");
		return 0;
	}

	if ((cc = child_config_find_by_name(n)) == NULL) {
		send_status_msg(con, 0, "no such group.");
		return 1;
	}

	json_object_put(obj);

	if (instance < 0 || instance >= cc->cc_instances) {
		send_status_msg(con, 0, "instance out of bounds.");
		return 1;
	}

	snprintf(instance_str, sizeof(instance_str), "%d", instance);
	if (stream == 1) {
		if (cc->cc_stdout)
			fn = xstrdup(cc->cc_stdout);
	} else if (stream == 2) {
		if (cc->cc_stderr)
			fn = xstrdup(cc->cc_stderr);
	}

	if (!fn) {
		send_status_msg(con, 0, "stream is not logged.");
		return 1;
	}

	replace_str(fn, "%(NUM)", instance_str);

	/* file may not exist. when starting a new group and reading
	 * immediately, the file may not be there, yet.
	 */
	fd = open(fn, O_RDONLY);
	free(fn);

	if (fd == -1) {
		send_status_msg(con, 0, "can't open logfile.");
		return 1;
	}

	fsize = lseek(fd, 0, SEEK_END);
	if (off < 0)
		off = lseek(fd, -bytes, SEEK_END);
	else
		off = lseek(fd, off, SEEK_SET);

	if ((r = read(fd, readbuf, bytes)) == -1) {
		send_status_msg(con, 0, "read failed.");
		return 1;
	}

	close(fd);
	readbuf[r] = '\0';

	/* construct reply */
	obj = json_object_new_object();

	m = json_object_new_boolean(1);
	json_object_object_add(obj, "code", m);

	m = json_object_new_string(readbuf);
	json_object_object_add(obj, "log", m);

	m = json_object_new_double((double) off);
	json_object_object_add(obj, "offset", m);

	m = json_object_new_double((double) fsize);
	json_object_object_add(obj, "fsize", m);

	ret = json_object_to_json_string(obj);
	ret_len = strlen(ret);

	send_message(con, ret, ret_len);
	json_object_put(obj);
	return 1;
}

/*
 * find command to run.
 */
static int
command_compar(const void *a, const void *b)
{
	const char		*cmd = a;
	const struct commands_s	*s = b;
	return strncmp(cmd, s->c_name, 4);
}


/*
 * Execute command.
 */
static int
run_server_command(char *buf, struct client_con *c)
{
	int			cr = 0;
	char			*cmd_buf;
	struct commands_s	*s;


	s = bsearch(buf, commands, sizeof(commands) / sizeof(struct commands_s),
			sizeof(struct commands_s), command_compar);
	if (s) {
		cmd_buf = buf + 4;
		cr = s->c_func(c, cmd_buf);
	}

	if (!cr)
		drop_client_connection(c);

	return cr;
}

/*
 * libevent read callback.
 */
static void
read_cb(struct bufferevent *b, void *cx)
{
	char			buf[CHUNKSIZ];
	uint16_t		len,
				cid;
	size_t			r;
	struct client_con	*c = cx;


	/* read command len */
	while (1) {
		if (c->c_len == 0 && c->c_cid == 0) {
			if (EVBUFFER_LENGTH(b->input) < 2)
				return;

			if (bufferevent_read(b, &len, sizeof(uint16_t))
					!= sizeof(uint16_t)) {
				slog("read len failed.\n");
				drop_client_connection(c);
				return;
			}

			len = ntohs(len);

			if (len > CHUNKSIZ) {
				slog("command payload too large.\n");
				drop_client_connection(c);
				return;
			}

			if (len < 4) {
				slog("command payload too small.\n");
				drop_client_connection(c);
				return;
			}

			c->c_len = len;
			bufferevent_setwatermark(b, EV_READ, sizeof(uint16_t),
					CHUNKSIZ);
		}

		/* read command cid */
		if (c->c_len > 0 && c->c_cid == 0) {
			if (EVBUFFER_LENGTH(b->input) < 2)
				return;

			if (bufferevent_read(b, &cid, sizeof(uint16_t))
					!= sizeof(uint16_t)) {
				slog("read cid failed.\n");
				drop_client_connection(c);
				return;
			}
			c->c_cid = cid;
			bufferevent_setwatermark(b, EV_READ, c->c_len, CHUNKSIZ);
		}

		/* read command name and payload */
		if (c->c_len > 0 && c->c_cid != 0) {
			if (EVBUFFER_LENGTH(b->input) < c->c_len)
				return;

			if ((r = bufferevent_read(b, buf, c->c_len)) != c->c_len) {
				slog("read payload failed.\n");
				drop_client_connection(c);
				return;
			}

			bufferevent_setwatermark(c->c_be, EV_READ, sizeof(uint16_t),
					CHUNKSIZ);
			c->c_len = 0;
			buf[r] = '\0';
			if (!run_server_command(buf, c))
				return;
			c->c_cid = 0;
		}
	}
}

/*
 * libevent error callback.
 */
static void
error_cb(struct bufferevent *b __attribute__((unused)),
		short what __attribute__((unused)), void *cx)
{
	struct client_con	*c = cx;
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
			slog("Failed to accept a connection.\n");
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

	bufferevent_setwatermark(c->c_be, EV_READ, 4, CHUNKSIZ);
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
	if (buf_siz == 0) {
		fclose(f);
		return 1;
	}
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
		if (!json_object_is_type(t, json_type_object))
			return 0;
		if ((cc = child_config_from_json(t)) == NULL)
			return 0;
		slog("load: %s\n", cc->cc_name);
		/* Set some defaults that we require to operate, if they are
		 * not set already. */
		if (cc->cc_killsig == -1)
			cc->cc_killsig = SIGTERM;
		if (cc->cc_instances == -1)
			cc->cc_instances = 1;
		if (cc->cc_status == -1)
			cc->cc_status = STATUS_RUNNING;
		cc->cc_childs = xmalloc(sizeof(struct process *) * cc->cc_instances);
		memset(cc->cc_childs, '\0', sizeof(struct process *) * cc->cc_instances);
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
	FILE			*tmp_fd;


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
	LIST_INIT(&child_config_list_head);
	LIST_INIT(&client_con_list_head);
	process_hash = uvhash_new(16);
	child_config_hash = uvstrhash_new(16);

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

	/* check if we can open the logfile before we fork. */
	if (logfile != NULL && do_fork) {
		if ((tmp_fd = fopen(logfile, "a")) == NULL)
			die("fopen");
		fclose(tmp_fd);
	}

	/* check if we can open the dumpfile before we work. */
	if (dump_file != NULL) {
		if ((tmp_fd = fopen(dump_file, "r")) == NULL)
			die("fopen");
		fclose(tmp_fd);
	}

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

	if (logfile != NULL && do_fork) {
		if ((log_fd = fopen(logfile, "a")) == NULL)
			die("fopen");
		setvbuf(log_fd, NULL, _IOLBF, 0);
		r = fileno(log_fd);
		setcloseonexec(r);
	}

	/* loading a dump will start the processes - must do this after
	 * event_init */
	if (dump_file != NULL) {
		if (!load_dump(dump_file))
			die("dump_file");
	}

	if (load_latest)
		load_newest_dump();

	setcloseonexec(fd);

	event_set(&ev, fd, EV_READ | EV_PERSIST, accept_cb, NULL);
	event_add(&ev, NULL);

	event_set(&se, SIGCHLD, EV_SIGNAL | EV_PERSIST, signal_cb, NULL);
	event_add(&se, NULL);

	slog("server started.\n");
	event_dispatch();
	return EXIT_SUCCESS;
}
