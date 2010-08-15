#!/bin/sh
#
# Use `test/fakesmsc' to test the bearerbox and the smsbox.

set -e
#set -x

times=10
interval=0
loglevel=0
host=127.0.0.1

gw/bearerbox -v $loglevel gw/smskannel.conf > check_httpsmsc_kannel_sbb.log 2>&1 &
sbbpid=$!
sleep 1

gw/bearerbox -v $loglevel gw/other_smskannel.conf > check_httpsmsc_kannel_cbb.log 2>&1 &
cbbpid=$!
sleep 2

test/fakesmsc -H $host -i $interval -m $times '123 234 text relay nop' \
    > check_httpsmsc_kannel_fake.log 2>&1 &
sleep 1

gw/smsbox -v $loglevel gw/smskannel.conf > check_httpsmsc_kannel_ssb.log 2>&1 &
gw/smsbox -v $loglevel gw/other_smskannel.conf > check_httpsmsc_kannel_csb.log 2>&1 &

running="yes"
while [ $running = "yes" ]
do
    sleep 1
    if grep -v "fakesmsc: terminating" check_httpsmsc_kannel_fake.log >/dev/null
    then
    	running="no"
    fi
done

kill -INT $sbbpid
kill -INT $cbbpid
wait

if grep 'WARNING:|ERROR:|PANIC:' check_httpsmsc_kannel_*.log >/dev/null
then
	echo check_httpsmsc_kannel.sh failed 1>&2
	echo See check_httpsmsc_kannel_*.log for info 1>&2
	exit 1
fi

rm -f check_httpsmsc_kannel_*.log
