#!/bin/sh
#
# Use `test/test_compiler' to test gw/wml_compiler.c

set -e

test/wml_tester -b test/testcase.wml | test/decompile | diff test/testcase.wml - > check_compiler.log 2>&1
ret=$?

if [ "$ret" != 0 ]
then
	echo check_compiler failed 1>&2
	echo See check_compiler.log or run test/test_compiler for info 1>&2
	exit 1
fi

rm -f check_compiler.log
