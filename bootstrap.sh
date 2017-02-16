#!/bin/sh

#
# verbose
#
set -x

#
# set automake version that needed
#
export WANT_AUTOMAKE=1.8

#
# libtool stuff
#
if `which glibtoolize`
then
	glibtoolize --copy --automake --force
else
	libtoolize --copy --automake --force
fi

#
# and auto-magic stuff
#
aclocal
autoheader
autoconf
#automake --add-missing --copy --gnu
