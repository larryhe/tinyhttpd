#!/bin/sh
#
# thttpd.sh - startup script for thttpd on FreeBSD
#
# This goes in /usr/local/etc/rc.d and gets run at boot-time.

case "$1" in

    start)
    if [ -x /usr/local/sbin/thttpd_wrapper ] ; then
	echo -n " thttpd"
	/usr/local/sbin/thttpd_wrapper &
    fi
    ;;

    stop)
    kill -USR1 `cat /var/run/thttpd.pid`
    ;;

    *)
    echo "usage: $0 { start | stop }" >&2
    exit 1
    ;;

esac
