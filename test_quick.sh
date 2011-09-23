#!/bin/sh
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
UBER=./ubervisor

ck0() {
	MSG=$1
	shift
	RET=$1
	shift
	OUT=$1
	shift

	echo -n "$MSG..."
	$@ > out 2> err
	E=$?
	if [ "$E" -eq "$RET" ]; then
		if [ "$OUT" != "" ]; then
			if [ "$OUT" = "$(cat out)" ]; then
				echo ok
			else
				echo error
				cat out
				cat err
				exit 1
			fi
		else
			echo ok
		fi
	else
		exit 1
	fi

	rm -f out err
}

$UBER list > /dev/null 2> /dev/null
if [ "$?" = "0" ]; then
	echo ubervisor appears to be running. aborting.
	exit 1
fi

TMPDIR=`mktemp -d /tmp/uber_test_XXXX`

if [ -z "$TMPDIR" ]; then
	echo failed to create TMDIR
	exit 1
fi

st() {
	$UBER update -s 2 tst
	$UBER kill -s 9 tst > /dev/null
	$UBER delete tst
}

ck0 server	0 '' $UBER server -d $TMPDIR -o /tmp/log
ck0 start	0 '' $UBER start tst /bin/sleep 10
ck0 delete	0 '' $UBER delete tst
ck0 list	0 '' $UBER list

ck0 'start -d'	0 '' $UBER start -d /tmp tst /bin/sleep 10
st
ck0 'start -i'	0 '' $UBER start -i 2 tst /bin/sleep 10
st
ck0 'start -o'	0 '' $UBER start -o $TMPDIR/bar tst /bin/sleep 10
st
ck0 'start -e'	0 '' $UBER start -e $TMPDIR/foo tst /bin/sleep 10
st
ck0 'start -k'	0 '' $UBER start -k 9 tst /bin/sleep 10
st
ck0 'start -s'	0 '' $UBER start -s 2 tst /bin/sleep 10
st
ck0 'start -f'	0 '' $UBER start -f /bin/echo tst /bin/sleep 10
st
ck0 'start -H'	0 '' $UBER start -H /bin/echo tst /bin/sleep 10
st

ck0 start	0 '' $UBER start tst /bin/sleep 10
ck0 'update -o'	0 '' $UBER update -o $TMPDIR/x tst
ck0 'update -e'	0 '' $UBER update -e $TMPDIR/x2 tst
ck0 'update -d'	0 '' $UBER update -d /tmp tst
ck0 'update -k'	0 '' $UBER update -k 12 tst
ck0 'update -s'	0 '' $UBER update -s 2 tst
ck0 'update -i'	0 '' $UBER update -i 2 tst
ck0 'update -d'	0 '' $UBER update -d /tmp tst
ck0 'update -H'	0 '' $UBER update -H /bin/echo tst
ck0 'update -f'	0 '' $UBER update -f /bin/echo tst

ck0 kill	0 ''		$UBER kill tst
ck0 kill_s	0 ''		$UBER kill -s 9 tst
ck0 'get -D'	0 ''		$UBER get -D tst
ck0 'get -d'	0 '/tmp'	$UBER get -d tst
ck0 'get -o'	0 "$TMPDIR/x"	$UBER get -o tst
ck0 'get -e'	0 "$TMPDIR/x2"	$UBER get -e tst
ck0 'get -u'	1 ''		$UBER get -u tst
ck0 'get -g'	1 ''		$UBER get -g tst
ck0 'get -s'	0 '2'		$UBER get -s tst
ck0 'get -k'	0 '12'		$UBER get -k tst
ck0 'get -H'	0 '/bin/echo'	$UBER get -H tst
ck0 'get -f'	0 '/bin/echo'	$UBER get -f tst
ck0 'get -i'	0 '2'		$UBER get -i tst

ck0 delete	0 '' $UBER delete tst
ck0 delete	1 '' $UBER delete tst

ck0 start	0 '' $UBER start -d /tmp tst /bin/sleep 10
ck0 'get -d'	0 '' $UBER get -d tst
ck0 delete	0 '' $UBER delete tst

#ck0 exit	0 '' $UBER exit
UBERVISOR_RSH="$UBER proxy" $UBER exit
pgrep ubervisor

rm -rf $TMPDIR
