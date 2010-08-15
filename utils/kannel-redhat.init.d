#!/bin/sh
#
# gateway         This shell script takes care of starting and stopping
#                 the Kannel WAP gateway (bearer/wapbox) 
#			Fabrice Gatille <fgatille@ivision.fr>
# chkconfig: 2345 97 03
# description:  Start and stop the Kannel WAP gateway used to fetch \
#		some WML content from a Web server & compile it \
# 		into WMLC mobile phone bytecode.
# probe: true

# Use start-stop-daemon
ver=0.12.1
START="start-stop-daemon-$ver -S --quiet -b -c web:web -x "
CONF=/etc/wapkannel.conf
[ $# -eq 2 ] && ver=$2

# Source function library.
. /etc/rc.d/init.d/functions

# Source networking configuration.
. /etc/sysconfig/network

# Check that networking is up.
[ ${NETWORKING} = "no" ] && exit 0

[ -x /usr/local/bin/bearerbox-$ver ] || exit 0

[ -x /usr/local/bin/wapbox-$ver ] || exit 0

[ -f $CONF ] || exit 0


RETVAL=0

# See how we were called.
case "$1" in
  start)
        # Start daemons.
        echo -n "Starting bearer service (gateway kannel $ver): "
	daemon --forcedaemon "$START" /usr/local/bin/bearerbox-$ver -- $CONF
	RETVAL1=$?
	echo
	echo -n "Starting wapbox service (gateway kannel $ver): "
	daemon --forcedaemon "$START" /usr/local/bin/wapbox-$ver -- $CONF
	RETVAL2=$?
	echo
 	[ $RETVAL1 -eq 0 -a $RETVAL2 -eq 0 ] && touch /var/lock/subsys/gateway ||\
	RETVAL=1
        ;;
  stop)
        # Stop daemons.
	echo -n "Shutting down wapbox (kannel $ver): "
	killproc wapbox-$ver quiet
	RETVAL2=$?
	echo
	echo -n "Shutting down bearerbox (kannel $ver): "
        killproc bearerbox-$ver quiet
	RETVAL1=$?
	echo
	[ $RETVAL1 -eq 0 -a $RETVAL2 -eq 0 ] && rm -f /var/lock/subsys/gateway
	echo ""
        ;;
  status)
	status bearerbox-$ver
	status wapbox-$ver
	exit $?
	;;
  restart)
	$0 stop
	sleep 1
	$0 start
	;;  
  *)
        echo "Usage: named {start|stop|status|restart}"
        exit 1
esac

exit $RETVAL

