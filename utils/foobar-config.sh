#!/bin/sh
#
# foobar-config.sh -- a generic foobar-config shell script generator
#
# This generator takes 3 arguments and creates a foobar-config shell
# scirpt that is used to determine the common CFLAGS for compiling
# again this foobar package, LIBS for linking against dependency libs
# and VERSION for displaying which version is used/installed.
#
# Derived from Ulric Eriksson <ulric@siag.nu> from the libsdb project.
#
# Stipe Tolj <stolj@wapme.de>
#

cat << EOF
#!/bin/sh

usage()
{
	echo "usage: \$0 [--cflags] [--libs] [--version]"
	exit 0
}

cflags=no
libs=no
version=no

test "\$1" || usage

while test "\$1"; do
	case "\$1" in
	--cflags )
		cflags=yes
		;;
	--libs )
		libs=yes
		;;
	--version )
		version=yes
		;;
	* )
		usage
		;;
	esac
	shift
done

test "\$cflags" = yes && cat << FOO
$1
FOO

test "\$libs" = yes && cat << FOO
$2
FOO

test "\$version" = yes && cat << FOO
$3
FOO

EOF
