#!/usr/bin/env python
#
# Copyright (c) 2011, Wihtematter Labs GmBH
# All rights reserved.
#
# Copyright (c) 2011 Kilian Klimek <kilian.klimek@googlemail.com>
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 
#   1. Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
# 
#   2. Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
import sys
from socket import socket, socketpair, AF_UNIX, SOCK_STREAM
from json import dumps, loads
from os import geteuid, fork, dup2, close, execv
from struct import pack, unpack
from pwd import getpwuid

STATUS_RUNNING = 1
STATUS_STOPPED = 2
STATUS_BROKEN = 3

class UbervisorClientException(Exception):
    pass

class UbervisorClient(object):
    _SOCK_FILE = '%s/.uber/socket'

    def _send(self, d, p = ''):
        self.s.send(pack('!H', len(d) + len(p)))
        self.s.send(d)
        if p != '':
            self.s.send(p)

    def _reply(self):
        b = self.s.recv(2)
        if len(b) != 2:
            raise UbervisorClientException('reply error')
        l = unpack('!H', b)[0]
        x = self.s.recv(l)
        try:
            return loads(x)
        except ValueError:
            raise UbervisorClientException('json error')

    def close(self):
        """Close connection to server."""
        self.s.close()

    def connect(self):
        """Open connection to server."""
        if not self.sock_cmd:
            self.s = socket(AF_UNIX, SOCK_STREAM)
            self.s.connect(self.sock_file)
        else:
            ps, cs = socketpair(AF_UNIX, SOCK_STREAM, 0)
            p = fork()
            if p == 0:
                ps.close()
                f = cs.fileno()
                close(0)
                close(1)
                dup2(f, 0)
                dup2(f, 1)
                cl = ['/bin/sh', '-c', self.sock_cmd]
                execv(cl[0], cl)
                sys.exit(0)
            else:
                cs.close()
                self.s = ps
        self._send('HELO')
        if self.s.recv(4) != 'HELO':
            raise UbervisorClientException("HELO failed")

    def __init__(self, sock_file = None, command = None):
        """
        Create new client and connect to server. If neither ``sock_file`` nor
        ``command`` are specified, the default socket (``~/.uber/socket``) is
        connected to.

        :param str sock_file:       path to socket file to connect.
        :param str command:         command to execute and whos stdin and stdout
                                    are use as the socket (equivalent to
                                    ``UBERVISOR_RSH`` environment variable of
                                    ubervisor).
        """
        if not command:
            self.sock_file = sock_file or self._SOCK_FILE % getpwuid(geteuid())[5]
            self.sock_cmd = None
        else:
            self.sock_file = None
            self.sock_cmd = command
        self.connect()

    def start(self, name, args, dir = None, stdout = None, stderr = None,
            instances = 1, status = STATUS_RUNNING, killsig = 15, uid = -1,
            gid = -1, heartbeat = None, fatal_cb = None, stdout_pipe = None):
        """
        Create a new process group and start it.

        :param str name:        name of the new process group.
        :param list args:       command and arguments.
        :param str stdout:      standard output log file.
        :param str stderr:      standard error log file.
        :param int instances:   number of instances to spawn in this group.
        :param int status:      initial status of this group.
        :param int killsig:     signal used to kill processes in this group.
        :param int uid:         user id to run program as.
        :param int gid:         group id to run program as.
        :param str fatal_cb:    command to run on error conditions.
        :param str stdout_pipe: pipe standard output of processes spawned in
                                this group to stdin of ``stdout_pipe``.
        """
        d = dict(name = name, args = args,
            stderr = stderr, instances = instances, status = status,
            killsig = killsig, uid = uid, gid = gid)
        if dir:
            d['dir'] = dir
        if stdout:
            d['stdout'] = stdout
        if heartbeat:
            d['heartbeat'] = heartbeat
        if fatal_cb:
            d['fatal_cb'] = fatal_cb
        if stdout_pipe:
            d['stdout_pipe'] = stdout_pipe

        d = dumps(d)
        self._send('SPWN', d)
        r = self._reply()
        if r['code'] != True:
            raise UbervisorClientException(r['msg'])
        return self

    def delete(self, name):
        """
        Delete process group, identified by *name*

        :param str name:        name of process group to delete.
        :returns:               list of pids still alive in this process group.
        """
        d = dumps(dict(name = name))
        self._send('DELE', d)
        r = self._reply()
        if r['code'] != True:
            raise UbervisorClientException(r['msg'])
        return r['pids']

    def kill(self, name, sig = None):
        """
        Kill processes in group *name*. By default, the killsig used in start
        command is uned (which itself defaults to 15).

        :param str name:        name of group the signal shall be send to.
        :param int sig:         signal to deliver.
        :returns:               list of pids that got a signal send.
        """
        d = dumps(dict(name = name))
        if sig:
            d['sig'] = int(sig)
        self._send('KILL', d)
        r = self._reply()
        if r['code'] != True:
            raise UbervisorClientException(r['msg'])
        return r['pids']

    def get(self, name):
        """
        Get config for *name*.

        :returns:               config dictionary.
        """
        d = dumps(dict(name = name))
        self._send('GETC', d)
        r = self._reply()
        if r.get('code', None) == False:
            raise UbervisorClientException(r['msg'])
        return r

    def list(self):
        """
        List groups

        :returns:               list of group names.
        """
        self._send('LIST')
        return self._reply()

    def exit(self):
        """
        Send exit command to ubervisor.
        """
        self._send('EXIT')
        r = self._reply()
        if r['code'] != True:
            raise UbervisorClientException(r['msg'])
        return self

    def dump(self):
        """
        Send ubervisor command to dump the current configuration to a file.
        """
        self._send('DUMP')
        r = self._reply()
        if r['code'] != True:
            raise UbervisorClientException(r['msg'])
        return self

    def update(self, name, stdout = None, stderr = None,
            instances = None, status = None, killsig = None,
            heartbeat = None, fatal_cb = None, stdout_pipe = None):
        """
        Create a new process group and start it.

        :param str name:        name of the process group to modify.
        :param str stdout:      standard output log file.
        :param str stderr:      standard error log file.
        :param int instances:   number of instances to spawn in this group.
        :param int status:      initial status of this group.
        :param int killsig:     signal used to kill processes in this group.
        :param str heartbeat:   command to periodically run.
        :param str fatal_cb:    command to run on error conditions.
        :param str stdout_pipe: pipe standard output into standard input of
                                ``stdout_pipe``.
        """
        d = dict(name = name)
        if stdout:
            d['stdout'] = stdout
        if stderr:
            d['stderr'] = stderr
        if instances:
            d['instances'] = instances
        if status:
            d['status'] = status
        if killsig:
            d['killsig'] = killsig
        if heartbeat:
            d['heartbeat'] = heartbeat
        if fatal_cb:
            d['fatal_cb'] = fatal_cb
        if stdout_pipe:
            d['stdout_pipe'] = stdout_pipe
        d = dumps(d)
        self._send('UPDT', d)
        r = self._reply()
        if r['code'] != True:
            raise UbervisorClientException(r['msg'])
        return self
