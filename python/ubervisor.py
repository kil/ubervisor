#!/usr/bin/env python
#
# Copyright (c) 2011, Whitematter Labs GmbH
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
from socket import socket, socketpair, AF_UNIX, SOCK_STREAM
from json import dumps, loads
from os import geteuid, fork, dup2, close, execv, kill, waitpid
from struct import pack, unpack
from pwd import getpwuid

STATUS_RUNNING = 1
STATUS_STOPPED = 2
STATUS_BROKEN = 3

UBERVISOR_CMD = '/usr/local/bin/ubervisor'

CHUNKEXT = 0x8000
CHUNKSIZ = 0x3fff

class UbervisorClientException(Exception):
    pass

class SSHSock(object):
    """
    Ubervisor ssh transport. This provides a socket-like object to communicate
    with an ubervisor server.
    """
    def _sshconnect(self):
        ps, cs = socketpair(AF_UNIX, SOCK_STREAM, 0)
        p = fork()
        if p == -1:
            raise Exception("Fork failed")
        if p == 0:
            ps.close()
            f = cs.fileno()
            close(0)
            close(1)
            dup2(f, 0)
            dup2(f, 1)
            cl = self.sshcmd + self.command
            execv(cl[0], cl)
            exit(1)
        else:
            cs.close()
            return p, ps

    def __init__(self, host, user, key, command, sshcmd = None):
        """
        Create new SSHSock.

        :param str host:        host to connect to.
        :param list command:    command to execute on ``host``.
        :param list sshcmd:     ssh command.
        """
        if sshcmd == None:
            sshcmd = ['/usr/bin/ssh', '-T', '-a', '-x']
        self.pid = 0
        self.sock = None
        if user:
            sshcmd += ['-l', user]
        if key:
            sshcmd += ['-i', key]
        self.sshcmd, self.command = sshcmd + [host], command

    def connect(self):
        """
        Open ssh connection.
        """
        self.pid, self.sock = self._sshconnect()
        return self.sock

    def close(self):
        """
        Close ssh connection. This closes the socket and kills the ssh child.
        """
        if self.sock:
            self.sock.close()
            self.sock = None
        if self.pid > 0:
            kill(self.pid, 15)
            waitpid(self.pid, 0)
            self.pid = 0

    def __getattr__(self, x):
        return getattr(self.sock, x)

class UbervisorClient(object):
    """
    Client for ubervisor server.
    """
    _SOCK_FILE = '%s/.uber/socket'

    def _send(self, d, p = ''):
        self.cid += 1
        self.s.sendall(pack('!HH', len(d) + len(p), self.cid) + d + p)
        return self.cid

    def _reply(self, exp_cid):
        cid, d = self.wait()
        if cid != exp_cid:
            raise UbervisorClientException("cid don't match")
        return d

    def wait(self):
        """
        Wait for command reply from server.

        :returns: ``(cid, payload)`` tuple.
        """
        x = ''
        while True:
            b = self.s.recv(4)
            if len(b) != 4:
                raise UbervisorClientException('reply error')
            l, cid = unpack('!HH', b)
            x += self.s.recv(l & CHUNKSIZ)
            if not (l & CHUNKEXT):
                break
        try:
            return cid, loads(x)
        except ValueError:
            raise UbervisorClientException('json error: \"%s\"' % x)

    def close(self):
        """Close connection to server."""
        self.s.close()

    def connect(self):
        """Open connection to server."""
        self.cid = 1
        if not self.sock_cmd:
            self.s = socket(AF_UNIX, SOCK_STREAM)
            self.s.connect(self.sock_file)
        else:
            self.s = SSHSock(self.host, self.user, self.key, self.sock_cmd)
            self.s.connect()

        self._send('HELO')
        if self.s.recv(4) != 'HELO':
            raise UbervisorClientException("HELO failed")

    def __init__(self, sock_file = None, host = None, user = None, key = None,
            command = [UBERVISOR_CMD, 'proxy']):
        """
        Create new client and connect to server. If neither ``sock_file`` nor
        ``command`` are specified, the default socket (``~/.uber/socket``) is
        connected to.

        :param str sock_file:       path to socket file to connect.
        :param str host:            host ubervisor server is running on. If set,
                                    use ssh with ``command`` to connect the
                                    server.
        :param str user:            username to login as when using the ``host``
                                    argument.
        :param str key:             path to ssh private key, when using the
                                    ``host`` argument.
        :param str command:         command to execute and whos stdin and stdout
                                    are use as the socket (equivalent to
                                    ``UBERVISOR_RSH`` environment variable of
                                    ubervisor).
        """
        self.host = host
        if not host:
            self.sock_file = sock_file or self._SOCK_FILE % getpwuid(geteuid())[5]
            self.sock_cmd = None
        else:
            self.sock_file = None
            self.user = user
            self.key = key
            self.sock_cmd = command
            if sock_file:
                self.sock_cmd += ['-s', sock_file]
        self.connect()

    def start(self, name, args, dir = None, stdout = None, stderr = None,
            instances = 1, status = STATUS_RUNNING, killsig = 15, uid = -1,
            gid = -1, heartbeat = None, fatal_cb = None, age = None, wait = True):
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
        :param int age:         maximum runtime of a process in this group in
                                seconds.
        :param bool wait:       if ``True``, wait for server reply.
        """
        d = dict(name = name, args = args,
            stderr = stderr, instances = instances, status = status,
            killsig = killsig, uid = uid, gid = gid, age = age)
        if dir:
            d['dir'] = dir
        if stdout:
            d['stdout'] = stdout
        if heartbeat:
            d['heartbeat'] = heartbeat
        if fatal_cb:
            d['fatal_cb'] = fatal_cb
        if age != None:
            d['age'] = age

        d = dumps(d)
        c = self._send('SPWN', d)
        if not wait:
            return c
        r = self._reply(c)
        if r['code'] != True:
            raise UbervisorClientException(r['msg'])
        return self

    def delete(self, name, wait = True):
        """
        Delete process group, identified by *name*

        :param str name:        name of process group to delete.
        :returns:               list of pids still alive in this process group.
        :param bool wait:       if ``True``, wait for server reply.
        """
        d = dumps(dict(name = name))
        x = self._send('DELE', d)
        if not wait:
            return x
        r = self._reply(x)
        if r['code'] != True:
            raise UbervisorClientException(r['msg'])
        return r['pids']

    def kill(self, name, sig = None, wait = True):
        """
        Kill processes in group *name*. By default, the killsig used in start
        command is uned (which itself defaults to 15).

        :param str name:        name of group the signal shall be send to.
        :param int sig:         signal to deliver.
        :param bool wait:       if ``True``, wait for server reply.
        :returns:               list of pids that got a signal send.
        """
        d = dumps(dict(name = name))
        if sig:
            d['sig'] = int(sig)
        x = self._send('KILL', d)
        if not wait:
            return x
        r = self._reply(x)
        if r['code'] != True:
            raise UbervisorClientException(r['msg'])
        return r['pids']

    def pids(self, name, wait = True):
        """
        Get current pids in group *name*.

        :param str name:        name of group.
        :param bool wait:       if ``True``, wait for server reply.
        :returns:               list of pids in the group.
        """
        d = dumps(dict(name = name))
        x = self._send('PIDS', d)
        if not wait:
            return x
        r = self._reply(x)
        if r['code'] != True:
            raise UbervisorClientException(r['msg'])
        return r['pids']

    def get(self, name, wait = True):
        """
        Get config for *name*.

        :param bool wait:       if ``True``, wait for server reply.
        :returns:               config dictionary.
        """
        d = dumps(dict(name = name))
        x = self._send('GETC', d)
        if not wait:
            return x
        r = self._reply(x)
        if r.get('code', None) == False:
            raise UbervisorClientException(r['msg'])
        return r

    def list(self, wait = True):
        """
        List groups

        :param bool wait:       if ``True``, wait for server reply.
        :returns:               list of group names.
        """
        x = self._send('LIST')
        if not wait:
            return x
        return self._reply(x)

    def exit(self, wait = True):
        """
        Send exit command to ubervisor.

        :param bool wait:       if ``True``, wait for server reply.
        """
        x = self._send('EXIT')
        if not wait:
            return x
        r = self._reply(x)
        if r['code'] != True:
            raise UbervisorClientException(r['msg'])
        return self

    def dump(self, wait = True):
        """
        Send ubervisor command to dump the current configuration to a file.

        :param bool wait:       if ``True``, wait for server reply.
        """
        x = self._send('DUMP')
        if not wait:
            return x
        r = self._reply(x)
        if r['code'] != True:
            raise UbervisorClientException(r['msg'])
        return self

    def update(self, name, stdout = None, stderr = None,
            instances = None, status = None, killsig = None,
            heartbeat = None, fatal_cb = None, age = None, wait = True):
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
        :param bool wait:       if ``True``, wait for server reply.
        """
        d = dict(name = name)
        if stdout:
            d['stdout'] = stdout
        if stderr:
            d['stderr'] = stderr
        if instances != None:
            d['instances'] = instances
        if status:
            d['status'] = status
        if killsig:
            d['killsig'] = killsig
        if heartbeat:
            d['heartbeat'] = heartbeat
        if fatal_cb:
            d['fatal_cb'] = fatal_cb
        if age != None:
            d['age'] = age
        d = dumps(d)
        x = self._send('UPDT', d)
        if not wait:
            return x
        r = self._reply(x)
        if r['code'] != True:
            raise UbervisorClientException(r['msg'])
        return self

    def subs(self, ident = 1, wait = True):
        """
        Subscribe to server events.
        """
        d = dumps(dict(ident = ident))
        x = self._send('SUBS', d)
        if not wait:
            return x
        r = self._reply(x)
        if r['code'] != True:
            raise UbervisorClientException(r['msg'])
        return x

    def read(self, name, stream, off = -1, instance = 0, bytes = 1024, wait = True):
        """
        Read from logfile.

        :param str name:        name of the process group.
        :param int instance:    id of an instance in the group.
        :param int stream:      id of the stream to read: 1 = stdout,
                                2 = stderr.
        :param int off:         offset in bytes in log file. -1 = from end of
                                file.
        :param int bytes:       number of bytes to read from file.
        """
        d = dumps(dict(name = name,
                stream = stream,
                offset = float(off),
                bytes = bytes,
                instance = instance))
        x = self._send('READ', d)
        if not wait:
            return x
        r = self._reply(x)
        if r['code'] != True:
            raise UbervisorClientException(r['msg'])
        return r
