#!/bin/sh
#
# thttpd.sh - startup script for thttpd on FreeBSD
#
# This goes in /usr/local/etc/rc.d and gets run at boot-time.
#
# Variables available:
#   thttpd_enable='YES/NO'
#   thttpd_program='path'
#   thttpd_pidfile='path'
#   thttpd_devfs='path'
#
# PROVIDE: thttpd
# REQUIRE: LOGIN FILESYSTEMS
# KEYWORD: shutdown

. /etc/rc.subr

name='thttpd'
rcvar='thttpd_enable'

load_rc_config "$name"

# Defaults.
thttpd_enable="${thttpd_enable:-'NO'}"
thttpd_program="${thttpd_program:-'/usr/local/sbin/thttpd'}"
thttpd_pidfile="${thttpd_pidfile:-'/var/run/thttpd.pid'}"

thttpd_precmd ()
    {
    if [ '' != "$thttpd_devfs" ] ; then
	mount -t devfs devfs "$thttpd_devfs"
	devfs -m "$thttpd_devfs" rule -s 1 applyset
	devfs -m "$thttpd_devfs" rule -s 2 applyset
    fi
    }

thttpd_stop ()
    {
    kill -USR1 `cat "$pidfile"`
    }

command="$thttpd_program"
pidfile="$thttpd_pidfile"
start_precmd='thttpd_precmd'
stop_cmd='thttpd_stop'

run_rc_command "$1"
