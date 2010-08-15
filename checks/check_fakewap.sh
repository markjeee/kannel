#!/bin/sh
#
# Use `test/fakewap' to test the bearerbox and the wapbox.

set -e
#set -x

host=127.0.0.1
times=2
port=8040
url="http://$host:$port/hello.wml"
loglevel=0

test/test_http_server -f test/hello.wml -p $port > check_http.log 2>&1 &
httppid=$!

gw/bearerbox -v $loglevel gw/wapkannel.conf > check_bb.log 2>&1 &
bbpid=$!

sleep 2

gw/wapbox -v $loglevel gw/wapkannel.conf > check_wap.log 2>&1 &
wappid=$!

sleep 2

test/fakewap -g $host -m $times $url > check_fake.log 2>&1
ret=$?

test/test_http -qv 4 http://$host:$port/quit

kill -INT $bbpid 
kill -INT $wappid
wait

if [ "$ret" != 0 ]
then
	echo check_fakewap failed 1>&2
	echo See check_bb.log, check_wap.log, check_fake.log, 1>&2
	echo check_http.log for info 1>&2
	exit 1
fi

rm -f check_bb.log check_wap.log check_fake.log check_http.log

exit 0
