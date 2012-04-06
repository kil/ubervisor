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
#include <errno.h>

#include <json/json.h>

#include "misc.h"
#include "child_config.h"

struct child_config_list		child_config_list_head;
uvstrhash_t				*child_config_hash;

/*
 * Serialize child_config struct to string. Returned buffer must be freed.
 */
char *
child_config_serialize(const struct child_config *cc)
{
	json_object		*obj,
				*t,
				*s;
	int			x;
	char			*ret;

#define ADD(X, Y)	if (Y != NULL) { \
				t = json_object_new_string(Y); \
				json_object_object_add(obj, X, t); \
			}

#define ADDINT(X, Y)	if (Y != -1) { \
				t = json_object_new_int(Y); \
				json_object_object_add(obj, X, t); \
			}

	obj = json_object_new_object();
	ADD("name", cc->cc_name);
	ADD("stdout", cc->cc_stdout);
	ADD("stderr", cc->cc_stderr);
	ADD("dir", cc->cc_dir);
	ADD("heartbeat", cc->cc_heartbeat);
	ADD("fatal_cb", cc->cc_fatal_cb);
	ADD("username", cc->cc_username);
	ADD("groupname", cc->cc_groupname);
	ADDINT("instances", cc->cc_instances);
	ADDINT("status", cc->cc_status);
	ADDINT("killsig", cc->cc_killsig);
	ADDINT("uid", cc->cc_uid);
	ADDINT("gid", cc->cc_gid);
	ADDINT("error", cc->cc_error);

	if (cc->cc_command != NULL) {
		t = json_object_new_array();
		x = 0;

		while (cc->cc_command[x] != NULL) {
			s = json_object_new_string(cc->cc_command[x]);
			json_object_array_add(t, s);
			x++;
		}
		json_object_object_add(obj, "args", t);
	}

	ret = xstrdup(json_object_to_json_string(obj));
	json_object_put(obj);

	return ret;
}

struct child_config *child_config_from_json(json_object *obj)
{
	struct child_config	*ret;
	json_object		*t,
				*s;
	int			i, len;

	ret = child_config_new();

#define GET(X, Y)	if ((t = json_object_object_get(obj, Y)) != NULL) { \
				if (!json_object_is_type(t, json_type_string)) { \
					child_config_free(ret); \
					return NULL; \
				} \
				X = xstrdup(json_object_get_string(t)); \
			}


#define GETINT(X, Y)	if ((t = json_object_object_get(obj, Y)) != NULL) { \
				if (!json_object_is_type(t, json_type_int)) { \
					child_config_free(ret); \
					return NULL; \
				} \
				X = json_object_get_int(t); \
			}

	GET(ret->cc_name, "name");
	if (ret->cc_name == NULL) {
		child_config_free(ret);
		return NULL;
	}
	GET(ret->cc_stdout, "stdout");
	GET(ret->cc_stderr, "stderr");
	GET(ret->cc_dir, "dir");
	GET(ret->cc_heartbeat, "heartbeat");
	GET(ret->cc_fatal_cb, "fatal_cb");
	GET(ret->cc_username, "username");
	GET(ret->cc_groupname, "groupname");
	GETINT(ret->cc_instances, "instances");
	GETINT(ret->cc_status, "status");
	GETINT(ret->cc_killsig, "killsig");
	GETINT(ret->cc_uid, "uid");
	GETINT(ret->cc_gid, "gid");
	GETINT(ret->cc_error, "error");

	if ((t = json_object_object_get(obj, "args")) != NULL) {
		if (!json_object_is_type(t, json_type_array)) {
			child_config_free(ret);
			return NULL;
		}

		len = json_object_array_length(t);
		ret->cc_command = xmalloc(sizeof(char *) * (len + 1));

		for (i = 0; i < len; i++) {
			if ((s = json_object_array_get_idx(t, i)) == NULL) {
				ret->cc_command[i] = NULL;
				child_config_free(ret);
				return NULL;
			}
			if (!json_object_is_type(s, json_type_string)) {
				child_config_free(ret);
				return NULL;
			}
			ret->cc_command[i] = xstrdup(json_object_get_string(s));
		}
		ret->cc_command[i] = NULL;
	}
	return ret;
}

/*
 * Load child_config from json string.
 */
struct child_config *child_config_unserialize(const char *buf)
{
	json_object		*obj;
	struct child_config	*ret;

	if ((obj = json_tokener_parse(buf)) == NULL)
		return NULL;
	if (is_error(obj))
		return NULL;
	if (!json_object_is_type(obj, json_type_object)) {
		json_object_put(obj);
		return NULL;
	}
	ret = child_config_from_json(obj);
	json_object_put(obj);
	return ret;
}

/*
 * free child_config.
 */
void
child_config_free(struct child_config *cc)
{
	int		i;

#define FREE(X)		if (X) \
				free(X);

	FREE(cc->cc_name);
	FREE(cc->cc_stdout);
	FREE(cc->cc_stderr);
	FREE(cc->cc_dir);
	FREE(cc->cc_heartbeat);
	FREE(cc->cc_fatal_cb);
	FREE(cc->cc_username);
	FREE(cc->cc_groupname);
	FREE(cc->cc_childs);
	if (cc->cc_command) {
		for (i = 0; cc->cc_command[i] != NULL; i++)
			free(cc->cc_command[i]);
		free(cc->cc_command);
	}

	free(cc);
}

/*
 * Allocate new child_config struct and set defaults. -1 is used as the
 * "unset" value on numbers.
 */
struct child_config *child_config_new(void)
{
	struct child_config	*cc;

	cc = xmalloc (sizeof(struct child_config));
	memset(cc, '\0', sizeof(struct child_config));
	cc->cc_instances = -1;
	cc->cc_status = -1;
	cc->cc_killsig = -1;
	cc->cc_uid = -1;
	cc->cc_gid = -1;
	return cc;
}

/*
 * Insert child_config into global list.
 */
void
child_config_insert(struct child_config *cc)
{
	LIST_INSERT_HEAD(&child_config_list_head, cc, cc_ent);
	uvstrhash_insert(child_config_hash, cc->cc_name, cc);
}

/*
 * Find by group name.
 */
struct child_config *
child_config_find_by_name(const char *n)
{
	return uvstrhash_find(child_config_hash, n);
}

/*
 * Remove (not free) from list
 */
void
child_config_remove(struct child_config *cc)
{
	LIST_REMOVE(cc, cc_ent);
	uvstrhash_remove(child_config_hash, cc->cc_name);
}

/*
 * convert str to status code.
 */
int
child_config_status_from_string(const char *str)
{
	int	ret;

	if (!strncmp(str, "start", 5))
		return STATUS_RUNNING;
	else if (!strncmp(str, "stop", 4))
		return STATUS_STOPPED;
	else if (!strncmp(str, "fatal", 5))
		return STATUS_BROKEN;

	ret = strtol(str, NULL, 10);
	if (errno == EINVAL)
		return -1;
	if (ret > STATUS_MAX)
		return -1;
	return ret;
}
