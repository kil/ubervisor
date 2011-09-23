#!/usr/bin/env python
#
# Copyright (c) 2011, Whitematter Labs GmBH
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
from ubervisor import *
from unittest import TestCase, TestLoader, TextTestRunner
from os import stat, unlink, path, environ
from time import sleep
from tempfile import mkdtemp

SEND_GARBAGE = True
SLEEP_SEC = 0.02

def _sleep_monkey(fun):
    def f(*args, **kw):
        ret = fun(*args, **kw)
        sleep(SLEEP_SEC)
        return ret
    return f

class BaseTest(TestCase):
    def setUp(self):
        self.c = UbervisorClient(host = environ.get("TEST_HOST", None),
                command = [environ.get("UBERVISOR_PATH", ""), 'proxy'])
        self.c._delete = self.c.delete
        self.c.update = _sleep_monkey(self.c.update)
        self.c.start = _sleep_monkey(self.c.start)
        self.c.kill = _sleep_monkey(self.c.kill)
        self.tmpdir = t = mkdtemp()
        self.tmpfile = path.join(t, 'tmpfile')
        self.reltmpfile = 'ubervisor_test_reltmpfile'

        try:
            unlink(self.tmpfile)
        except:
            pass

    def tearDown(self):
        try:
            unlink(self.tmpfile)
        except:
            pass
        try:
            self.c.delete('test')
        except:
            pass
        try:
            self.c.close()
        except:
            pass

class TestStartCommand(BaseTest):
    def test_start_normal(self):
        self.c.start('test', ['/bin/sleep', '0.1'])
        self.c.delete('test')

    def test_start_enter_fatal(self):
        self.c.start('test', ['illegalpath', '0.1'])
        r = self.c.get('test')
        self.assertEqual(r['status'], STATUS_BROKEN)

    def test_start_dup(self):
        self.c.start('test', ['/bin/sleep', '1'])
        self.assertRaises(UbervisorClientException, self.c.start, 'test',
                ['/bin/sleep', '1'])

    def test_start_stdout(self):
        self.c.start('test', ['/bin/sleep', '1'], stdout = self.tmpfile)
        stat(self.tmpfile)

    def test_start_stderr(self):
        self.c.start('test', ['/bin/sleep', '1'], stderr = self.tmpfile)
        stat(self.tmpfile)

    def test_start_delete(self):
        self.c.start('test', ['/bin/sleep', '2'], instances = 3)
        r = self.c.delete('test')
        self.assertEqual(len(r), 3)

    def test_start_stopped(self):
        self.c.start('test', ['/bin/sleep', '2'], status = 2)
        r = self.c.delete('test')
        self.assertEqual(len(r), 0)

    def test_start_sig(self):
        self.c.start('test', ['/bin/sleep', '2'], killsig = 9)
        r = self.c.delete('test')
        self.assertEqual(len(r), 1)

    def test_start_dir(self):
        self.c.start('test', ['/bin/sleep', '2'], dir = self.tmpdir,
                stdout = self.reltmpfile)
        r = self.c.delete('test')
        self.assertEqual(len(r), 1)
        stat(path.join(self.tmpdir, self.reltmpfile))

    # heardbeat, fatal tested in TestInt


class TestDeleteCommand(BaseTest):
    def test_delete(self):
        self.assertRaises(UbervisorClientException, self.c.delete, 'test')

    def test_delete_stopped(self):
        self.c.start('test', ['/bin/sleep', '1'], status = STATUS_STOPPED)
        r = self.c.delete('test')
        self.assertEquals(len(r), 0)

    def test_delete_multiple(self):
        self.c.start('test', ['/bin/sleep', '1'], instances = 3)
        r = self.c.kill('test')
        self.assertEqual(len(r), 3)
        r = self.c.delete('test')
        self.assertEqual(len(r), 3)


class TestGetCommand(BaseTest):
    def test_get_args(self):
        self.c.start('test', ['/bin/sleep', '1'])
        r = self.c.get('test')
        self.assertEqual(r['args'], ['/bin/sleep', '1'])

    def test_get_status(self):
        self.c.start('test', ['/bin/sleep', '1'])
        r = self.c.get('test')
        self.assertEqual(r['status'], 1)

    def test_get_status2(self):
        self.c.start('test', ['/bin/sleep', '1'], status = 2)
        r = self.c.get('test')
        self.assertEqual(r['status'], 2)

    def test_get_instances(self):
        self.c.start('test', ['/bin/sleep', '1'], instances = 2)
        r = self.c.get('test')
        self.assertEqual(r['instances'], 2)

    def test_get_sig(self):
        self.c.start('test', ['/bin/sleep', '1'], killsig = 9)
        r = self.c.get('test')
        self.assertEqual(r['killsig'], 9)

    def test_get_stdout(self):
        self.c.start('test', ['/bin/sleep', '1'], stdout = self.tmpfile)
        r = self.c.get('test')
        self.assertEqual(r['stdout'], self.tmpfile)

    def test_get_stderr(self):
        self.c.start('test', ['/bin/sleep', '1'], stderr = self.tmpfile)
        r = self.c.get('test')
        self.assertEqual(r['stderr'], self.tmpfile)

    def test_get_fatal(self):
        self.c.start('test', ['/bin/sleep', '1'], fatal_cb = '/bin/echo')
        r = self.c.get('test')
        self.assertEqual(r['fatal_cb'], '/bin/echo')

    def test_get_heartbeat_not_set(self):
        self.c.start('test', ['/bin/sleep', '1'])
        r = self.c.get('test')
        self.assertEqual(r.get('heartbeat'), None)

    def test_get_err(self):
        self.assertRaises(UbervisorClientException, self.c.get, 'test')

    # XXX: gid, uid

# type validation tests
class TestInput(BaseTest):
    def test_in_t1(self):
        self.assertRaises(UbervisorClientException, self.c.start, 'test',
                '/bin/sleep')

    def test_in_t2(self):
        self.assertRaises(UbervisorClientException, self.c.start, 'test', 1,
                instances = 'a')

    def test_in_t3(self):
        self.assertRaises(UbervisorClientException, self.c.start, 'test',
                ['/bin/sleep', '1'], instances = 'a')

    def test_in_t4(self):
        self.assertRaises(UbervisorClientException, self.c.start, 'test',
                ['/bin/sleep', '1'], stderr = True)

    def test_in_t5(self):
        self.assertRaises(UbervisorClientException, self.c.start, 'test',
                ['/bin/sleep', '1'], heartbeat = 1)


# Not real tests. This is only for testing the json parsing and i/o handling in
# ubervisor server.
class TestInput2(BaseTest):
    def _send_garbage(self, p):
        if SEND_GARBAGE:
            pl = [
                "'name'",
                "{'name'",
                "{'name': 'test', 'stdin':",
                "{",
                "{}",
                ]

            for x in pl:
                self.c.close()
                self.c.connect()
                try:
                    self.c._send(p, x)
                except:
                    pass

    def test_in_t7(self):
        self._send_garbage("SPWN")

    def test_in_t8(self):
        self._send_garbage("DELE")

    def test_in_t9(self):
        self._send_garbage("KILL")

    def test_in_t10(self):
        self._send_garbage("GETC")

    def test_in_t11(self):
        self._send_garbage("HELO")

    def test_in_t12(self):
        self._send_garbage("UPDT")

    #def test_in_t13(self):
    #    self._send_garbage("DUMP")

    def test_in_t14(self):
        self._send_garbage("LIST")


class TestUpdateCommand(BaseTest):
    def test_instances_increase(self):
        self.c.start('test', ['/bin/sleep', '1'], instances = 1)
        self.c.update('test', instances = 3)
        r = self.c.kill('test')
        self.assertEqual(len(r), 3)

    def test_instances_decrease(self):
        self.c.start('test', ['/bin/sleep', '1'], instances = 3)
        self.c.update('test', instances = 1)
        self.c.kill('test')
        r = self.c.kill('test')
        self.assertEqual(len(r), 1)

    def test_status_start(self):
        self.c.start('test', ['/bin/sleep', '1'], status = STATUS_STOPPED)
        r = self.c.kill('test')
        self.assertEqual(len(r), 0)
        self.c.update('test', status = STATUS_RUNNING)
        r = self.c.kill('test')
        self.assertEqual(len(r), 1)

    def test_status_stop(self):
        self.c.start('test', ['/bin/sleep', '1'], status = STATUS_RUNNING)
        self.c.kill('test')
        self.c.update('test', status = STATUS_STOPPED)
        self.c.kill('test')
        r = self.c.kill('test')
        self.assertEqual(len(r), 0)

    def test_redir_stdout(self):
        self.c.start('test', ['/bin/sleep', '1'])
        self.assertRaises(OSError, stat, self.tmpfile)
        self.c.update('test', stdout = self.tmpfile)
        self.c.kill('test')
        stat(self.tmpfile)

    def test_redir_stderr(self):
        self.c.start('test', ['/bin/sleep', '1'])
        self.assertRaises(OSError, stat, self.tmpfile)
        self.c.update('test', stderr = self.tmpfile)
        self.c.kill('test')
        stat(self.tmpfile)

    def test_fatal(self):
        self.c.start('test', ['/bin/sleep', '1'])
        self.c.update('test', fatal_cb = '/bin/echo')
        r = self.c.get('test')
        self.assertEqual(r['fatal_cb'], '/bin/echo')

    def test_heartbeat(self):
        self.c.start('test', ['/bin/sleep', '1'])
        self.c.update('test', heartbeat = '/bin/echo')
        r = self.c.get('test')
        self.assertEqual(r.get('heartbeat'), '/bin/echo')


class TestListCommand(BaseTest):
    def test_list0(self):
        r = self.c.list()
        self.assertEquals(r, [])

    def test_list1(self):
        self.c.start('test', ['/bin/sleep', '1'])
        r = self.c.list()
        self.assertEquals(r, ['test'])

    def test_list2(self):
        self.c.start('test', ['/bin/sleep', '1'])
        self.c.start('test2', ['/bin/sleep', '1'])
        r = self.c.list()
        self.c.delete('test2')
        self.assertEquals(sorted(r), sorted(['test', 'test2']))

    def test_list3(self):
        self.c.start('test', ['/bin/sleep', '1'])
        self.c.start('test2', ['/bin/sleep', '1'])
        self.c.delete('test2')
        r = self.c.list()
        self.assertEquals(r, ['test'])


class TestInt(BaseTest):
    def test_call_fatal(self):
        cmd = path.join(path.dirname(path.abspath(__file__)), 'fatal_test.sh')
        self.c.start('fatal_grp', ['illegalpath', '0.1'], fatal_cb = cmd)
        r = self.c.get('fatal_grp')
        self.c.delete('fatal_grp')
        self.assertEqual(r['status'], STATUS_BROKEN)
        stat('/tmp/fatal_grp')
        unlink('/tmp/fatal_grp')

    def test_call_heartbeat(self):
        cmd = path.join(path.dirname(path.abspath(__file__)), 'heartbeat_test.sh')
        self.c.start('test', ['/bin/sleep', '10.0'], heartbeat = cmd,
                instances = 3)
        r = self.c.get('test')
        self.assertEqual(r['heartbeat'], cmd)
        sleep(5.1)
        for x in range(0, 3):
            stat('/tmp/test-%d' % x)
            unlink('/tmp/test-%d' % x)

if __name__ == '__main__':
    if len(sys.argv) > 1:
        s = TestLoader().loadTestsFromName(sys.argv[1])
    else:
        s = TestLoader().loadTestsFromName('tests')
    TextTestRunner(verbosity=2).run(s)
