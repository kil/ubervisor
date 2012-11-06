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
#include <fcntl.h>

#include "misc.h"

void
*xmalloc(size_t x)
{
	void		*ret;

	if ((ret = malloc(x)) == NULL)
		die("malloc");
	return ret;
}

void *
xrealloc(void *p, size_t x)
{
	void		*ret;

	if ((ret = realloc(p, x)) == NULL)
		die("realloc");
	return ret;
}

char *
xstrdup(const char *x)
{
	char		*ret;

	if ((ret = strdup(x)) == NULL)
		die("strdup");
	return ret;
}

void
die(const char *msg)
{
	perror(msg);
	exit(1);
}

int
setcloseonexec(int fd)
{
	int fl;
	fl = fcntl(fd, F_GETFL);
	if (fl < 0)
		return fl;
	fl |= FD_CLOEXEC;
	if (fcntl(fd, F_SETFL, fl) < 0)
		return -1;
	return 0;
}

int
setnonblock(int fd)
{
	int fl;
	fl = fcntl(fd, F_GETFL);
	if (fl < 0)
		return fl;
	fl |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, fl) < 0)
		return -1;
	return 0;
}

int
xstrcmp(const char *a, const char *b) {
	if (a == NULL && b == NULL)
		return 0;
	if (a == NULL)
		return -1;
	if (b == NULL)
		return 1;
	return strcmp(a, b);
}
