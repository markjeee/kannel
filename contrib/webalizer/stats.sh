#!/bin/bash

logdir="var/tmp"
http="public_html/web/stats"

cd /home/kannel || exit 1

old=""

rm -f $logdir/*.log

cat var/log/bearerbox_access.log$old | bin/multi-line.pl | bin/split.pl "$logdir/"

# This block checks reads access.log from other machine
#if [ "`/sbin/ifconfig | grep 100.204`" = "" ] ; then
#	other=204
#else
#	other=205
#fi
#ssh <otherprefixip>.$other "cat var/log/bearerbox_access.log$old" | bin/multi-line.pl | bin/split.pl "$logdir/"

for i in `(cd $logdir ; ls -1 *.log)` ; do 
	dir=${i%%-*}
	file=${i#*-} ; file=${file%.log}

	echo "**$dir**$file**"
	if [ ! -d "$http/$dir" ] ; then
		mkdir -p "$http/$dir"
	fi
	if [ ! -d "$http/$dir/$file" ] ; then
		mkdir -p "$http/$dir/$file/"
	fi
	cat $logdir/$i | sort | bin/converte.pl $file | /usr/bin/webalizer -q -c etc/webalizer.conf -o $http/$dir/$file -t "$file" -
done
rm -f $logdir/*.log
