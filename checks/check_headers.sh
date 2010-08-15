#!/bin/sh
#
# Use `test/test_headers' to test gw/wsp_headers.c

set -e
#set -x

loglevel=1

test/test_headers -v $loglevel test/header_test > check_headers.log 2>&1
ret=$?

if [ "$ret" != 0 ] || \
   grep ERROR: check_headers.log > /dev/null 
then
	echo check_headers failed 1>&2
	echo See check_headers.log or run test/test_headers for info 1>&2
	exit 1
fi

rm -f check_headers.log
