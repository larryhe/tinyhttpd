dnl Process this file with autoconf to produce a configure script.

AC_INIT(thttpd.c)

AC_CANONICAL_SYSTEM

AC_PROG_CC

V_CCOPT="-O"
if test "$GCC" = yes ; then
	AC_MSG_CHECKING(gcc version)
	AC_CACHE_VAL(ac_cv_lbl_gcc_vers,
	    ac_cv_lbl_gcc_vers=`$CC -dumpversion 2>&1 | \
		sed -e 's/\..*//'`)
	AC_MSG_RESULT($ac_cv_lbl_gcc_vers)
	if test "$ac_cv_lbl_gcc_vers" -gt 1 ; then
		V_CCOPT="-O2"
	fi
fi
if test -f .devel ; then
	V_CCOPT="-g $V_CCOPT -ansi -pedantic -U__STRICT_ANSI__ -Wall -Wpointer-arith -Wshadow -Wcast-qual -Wcast-align -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls -Wno-long-long"
fi

dnl
dnl maybe this should be a loop
dnl
AC_MSG_CHECKING(how to link static binaries)
AC_CACHE_VAL(ac_cv_lbl_static_flag,
    ac_cv_lbl_static_flag=unknown
    echo 'main() {}' > conftest.c
    if test "$GCC" != yes ; then
	    trial_flag="-Bstatic"
	    test=`$CC $trial_flag -o conftest conftest.c 2>&1`
	    if test -z "$test" ; then
		    ac_cv_lbl_static_flag="$trial_flag"
	    fi
	    rm -f conftest
    fi
    if test "$ac_cv_lbl_static_flag" = unknown ; then
	    trial_flag="-static"
	    test=`$CC $trial_flag -o conftest conftest.c 2>&1`
	    if test -z "$test" ; then
		    ac_cv_lbl_static_flag="$trial_flag"
	    fi
	    rm -f conftest
    fi
    rm conftest.c)
AC_MSG_RESULT($ac_cv_lbl_static_flag)
if test "$ac_cv_lbl_static_flag" != unknown ; then
	V_STATICFLAG="$ac_cv_lbl_static_flag"
fi

AC_MSG_CHECKING(for __progname)
AC_CACHE_VAL(ac_cv_extern__progname,
    AC_TRY_LINK([],
	[extern char *__progname; 
	puts(__progname)],
	ac_cv_extern__progname=yes,
	ac_cv_extern__progname=no))
if test $ac_cv_extern__progname = yes ; then
	AC_DEFINE(HAVE__PROGNAME)
	AC_MSG_RESULT(yes)
else
	AC_MSG_RESULT(no)   
fi

AC_CHECK_HEADERS(fcntl.h grp.h memory.h paths.h poll.h sys/poll.h sys/devpoll.h sys/event.h osreldate.h)
AC_HEADER_TIME
AC_HEADER_DIRENT

d="/usr/local/v6/lib"
AC_MSG_CHECKING(for $d)
if test -d $d; then
	AC_MSG_RESULT(yes (Adding -L$d to LDFLAGS))
	LDFLAGS="$LDFLAGS -L$d"
else
	AC_MSG_RESULT(no)
fi

dnl
dnl Most operating systems have gethostbyname() in the default searched
dnl libraries (i.e. libc):
dnl
V_NETLIBS=""
AC_CHECK_FUNC(gethostbyname, ,
    # Some OSes (eg. Solaris) place it in libnsl:
    AC_LBL_CHECK_LIB(nsl, gethostbyname,
	V_NETLIBS="-lnsl $V_NETLIBS", 
	# Some strange OSes (SINIX) have it in libsocket:
	AC_LBL_CHECK_LIB(socket, gethostbyname,
	    V_NETLIBS="-lsocket $V_NETLIBS", 
	    # Unfortunately libsocket sometimes depends on libnsl.
	    # AC_CHECK_LIB's API is essentially broken so the
	    # following ugliness is necessary:
	    AC_LBL_CHECK_LIB(socket, gethostbyname,
		V_NETLIBS="-lsocket -lnsl $V_NETLIBS",
		AC_CHECK_LIB(resolv, gethostbyname,
		    V_NETLIBS="-lresolv $V_NETLIBS"),
		-lnsl))))
AC_CHECK_FUNC(socket, ,
    AC_CHECK_LIB(socket, socket,
	V_NETLIBS="-lsocket $V_NETLIBS",
	AC_LBL_CHECK_LIB(socket, socket,
	    V_NETLIBS="-lsocket -lnsl $V_NETLIBS", , -lnsl)))

AC_CHECK_LIB(inet6, main)

AC_CHECK_FUNC(crypt, , AC_CHECK_LIB(crypt, crypt))
AC_CHECK_FUNC(hstrerror, ,
    AC_CHECK_LIB(resolv, hstrerror, V_NETLIBS="-lresolv $V_NETLIBS"))

AC_REPLACE_FUNCS(strerror)
AC_CHECK_FUNCS(waitpid vsnprintf daemon setsid setlogin getaddrinfo getnameinfo gai_strerror kqueue sigset atoll)
AC_FUNC_MMAP

case "$target_os" in
solaris*)
	dnl Solaris's select() is a bad wrapper routine.
	AC_CHECK_FUNCS(poll)
	;;
*)
	AC_CHECK_FUNCS(select poll)
	;;
esac

AC_ACME_TM_GMTOFF
AC_ACME_INT64T
AC_ACME_SOCKLENT

AC_PROG_MAKE_SET
AC_PROG_INSTALL

AC_SUBST(DEFS)
AC_SUBST(V_CCOPT)
AC_SUBST(V_STATICFLAG)
AC_SUBST(V_NETLIBS)

AC_OUTPUT(Makefile cgi-src/Makefile extras/Makefile)
