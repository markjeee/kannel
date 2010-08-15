#!/bin/sh
# Start/stop the Kannel boxes: One bearer box and one WAP box.

# This is the default init.d script for Kannel.  Its configuration is
# appropriate for a small site running Kannel on one machine.

# Make sure that the Kannel binaries can be found in $BOXPATH or somewhere
# else along $PATH.  run_kannel_box has to be in $BOXPATH.

BOXPATH=/usr/bin
PIDFILES=/var/run
CONF=/etc/kannel/kannel.conf

USER=kannel
VERSION=""
#VERSION="-0.12.4"

RB=run_kannel_box$VERSION
BB=bearerbox$VERSION
WB=wapbox$VERSION
SB=smsbox$VERSION
SSD=start-stop-daemon$VERSION

PATH=$BOXPATH:$PATH

# On Debian, the most likely reason for the bearerbox not being available
# is that the package is in the "removed" or "unconfigured" state, and the
# init.d script is still around because it's a conffile.  This is normal,
# so don't generate any output.
test -x $BOXPATH/$BB || exit 0

case "$1" in
  start)
    echo -n "Starting WAP gateway: bearerbox"
    $SSD --start --quiet --pidfile $PIDFILES/kannel_bearerbox.pid --exec $RB -- --pidfile $PIDFILES/kannel_bearerbox.pid $BB -- $CONF
    echo -n " wapbox"
    $SSD --start --quiet --pidfile $PIDFILES/kannel_wapbox.pid --exec $RB -- --pidfile $PIDFILES/kannel_wapbox.pid $WB -- $CONF
#    echo -n " smsbox"
#    $SSD --start --quiet --pidfile $PIDFILES/kannel_smsbox.pid --exec $RB -- --pidfile $PIDFILES/kannel_smsbox.pid $SB -- $CONF
    echo "."
    ;;

  stop)
    echo -n "Stopping WAP gateway: wapbox"
    $SSD --stop --quiet --pidfile $PIDFILES/kannel_wapbox.pid --exec $RB
#    echo -n " smsbox"
#    $SSD --stop --quiet --pidfile $PIDFILES/kannel_smsbox.pid --exec $RB
    echo -n " bearerbox"
    $SSD --stop --quiet --pidfile $PIDFILES/kannel_bearerbox.pid --exec $RB
    echo "."
    ;;

  reload)
    # We don't have support for this yet.
    exit 1
    ;;

  restart|force-reload)
    $0 stop
    sleep 1
    $0 start
    ;;

  *)
    echo "Usage: $0 {start|stop|reload|restart|force-reload}"
    exit 1

esac

exit 0
