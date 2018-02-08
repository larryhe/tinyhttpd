#!/bin/sh
#
# thttpd.sh - startup script for thttpd on FreeBSD
#
# This should be manually installed as:
#   /usr/local/etc/rc.d/thttpd
# It gets run at boot-time.
#
# Variables available:
#   thttpd_enable='YES'
#   thttpd_program='/usr/local/sbin/thttpd'
#   thttpd_pidfile='/var/run/thttpd.pid'
#   thttpd_devfs=...
#   thttpd_flags=...
#
# PROVIDE: thttpd
# REQUIRE: LOGIN FILESYSTEMS
# KEYWORD: shutdown

. /etc/rc.subr

name='thttpd'
rcvar='thttpd_enable'
start_precmd='thttpd_precmd'
stop_cmd='thttpd_stop'
thttpd_enable_defval='NO'

load_rc_config "$name"
command="${thttpd_program:-/usr/local/sbin/${name}}"
pidfile="${thttpd_pidfile:-/var/run/${name}.pid}"
command_args="-i ${pidfile}"

thttpd_precmd ()
{
	if [ -n "$thttpd_devfs" ] ; then
		mount -t devfs devfs "$thttpd_devfs"
		devfs -m "$thttpd_devfs" rule -s 1 applyset
		devfs -m "$thttpd_devfs" rule -s 2 applyset
	fi
}

thttpd_stop ()
{
	kill -USR1 `cat "$pidfile"`
}

run_rc_command "$1"
