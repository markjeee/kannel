#!/bin/sh
#
# Use `test/fakesmsc' to test sendsms in smsbox.

set -e
#set -x

host=127.0.0.1
times=10
interval=0
loglevel=0
sendsmsport=13013
global_sender=13013 
username=tester
password=foobar

url="http://$host:$sendsmsport/cgi-bin/sendsms?from=123&to=234&\
text=test&username=$username&password=$password"

gw/bearerbox -v $loglevel gw/smskannel.conf > check_sendsms_bb.log 2>&1 &
bbpid=$!

sleep 2

test/fakesmsc -H $host -r 20000 -i $interval -m $times '123 234 text nop' \
    > check_sendsms_smsc.log 2>&1 &

sleep 1
gw/smsbox -v $loglevel gw/smskannel.conf > check_sendsms_sms.log 2>&1 &

sleep 2

# All cgivars are OK
url="http://$host:$sendsmsport/cgi-bin/sendsms?from=123&to=234&\
text=test&username=$username&password=$password"

i=0
while [ $i -lt $times ]
do
    test/test_http $url >> check_sendsms.log 2>&1
    i=`expr $i + 1`
done

sleep 5

if grep 'WARNING:|ERROR:|PANIC:' check_sendsms*.log >/dev/null ||
   [ $times -ne `grep -c 'Got message .*: <123 234 text test>' \
    check_sendsms_smsc.log` ]
then
	echo check_sendsms.sh failed with non-empty fields 1>&2
	echo See check_sendsms*.log for info 1>&2
	exit 1
fi

# Empty fields: message. This is OK, we must get a canned reply
url="http://$host:$sendsmsport/cgi-bin/sendsms?from=123&to=234&\
text=&username=$username&password=$password"

test/test_http $url >> check_sendsms.log 2>&1
sleep 1

if grep 'WARNING:|ERROR:|PANIC:' check_sendsms*.log >/dev/null
then
	echo check_sendsms.sh failed with empty message 1>&2
	echo See check_sendsms*.log for info 1>&2
	exit 1
fi

# From. This is OK, too: now global-sender replaces from field
url="http://$host:$sendsmsport/cgi-bin/sendsms?from=&to=234&\
text=test&username=$username&password=$password"

test/test_http $url >> check_sendsms.log 2>&1
sleep 1

if grep 'WARNING:|ERROR:|PANIC:' check_sendsms*.log >/dev/null ||
   [ 1 -ne `grep -c '<'$global_sender' 234 text test>' check_sendsms_smsc.log` ]
then
	echo check_sendsms.sh failed with empty from field 1>&2
	echo See check_sendsms*.log for info 1>&2
	exit 1
fi

# To. Now smsbox must report an error.
url="http://$host:$sendsmsport/cgi-bin/sendsms?from=123&to=&\
text=&username=$username&password=$password"

test/test_http $url >> check_sendsms.log 2>&1
sleep 1

if grep 'WARNING:|ERROR:|PANIC:' check_sendsms*.log >/dev/null ||
   [ 1 -ne `grep -c 'got empty <to> cgi variable' check_sendsms_sms.log` ]
then
	echo check_sendsms.sh failed with empty to field 1>&2
	echo See check_sendsms*.log for info 1>&2
	exit 1
fi

# Username. This is an authentication error.
url="http://$host:$sendsmsport/cgi-bin/sendsms?from=123&to=234&\
text=&username=&password=$password"

test/test_http $url >> check_sendsms.log 2>&1
sleep 1

if grep 'WARNING:|ERROR:|PANIC:' check_sendsms*.log >/dev/null ||
   [ 1 -ne `grep -c '<Authorization failed for sendsms>' \
       check_sendsms_sms.log` ]
then
	echo check_sendsms.sh failed username authorisation test 1>&2
	echo See check_sendsms*.log for info 1>&2
	exit 1
fi

# Password. Ditto.
url="http://$host:$sendsmsport/cgi-bin/sendsms?from=123&to=234&\
text=&username=$username&password="

if grep 'WARNING:|ERROR:|PANIC:' check_sendsms*.log >/dev/null ||
   [ 1 -ne `grep -c '<Authorization failed for sendsms>' \
       check_sendsms_sms.log` ]
then
	echo check_sendsms.sh failed with password authorisation test 1>&2
	echo See check_sendsms*.log for info 1>&2
	exit 1
fi

kill -INT $bbpid
wait

# Do we panic when going down ?
if grep 'WARNING:|ERROR:|PANIC:' check_sendsms*.log >/dev/null
then
	echo check_sendsms.sh failed when going down 1>&2
	echo See check_sendsms*.log for info 1>&2
	exit 1
fi

rm -f check_sendsms*.log

exit 0




