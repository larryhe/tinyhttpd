dnl
dnl Improved version of AC_CHECK_LIB
dnl
dnl Thanks to John Hawkinson (jhawk@mit.edu)
dnl
dnl usage:
dnl
dnl	AC_LBL_CHECK_LIB(LIBRARY, FUNCTION [, ACTION-IF-FOUND [,
dnl	    ACTION-IF-NOT-FOUND [, OTHER-LIBRARIES]]])
dnl
dnl results:
dnl
dnl	LIBS
dnl

define(AC_LBL_CHECK_LIB,
[AC_MSG_CHECKING([for $2 in -l$1])
dnl Use a cache variable name containing both the library and function name,
dnl because the test really is for library $1 defining function $2, not
dnl just for library $1.  Separate tests with the same $1 and different $2's
dnl may have different results.
ac_lib_var=`echo $1['_']$2['_']$5 | sed 'y%./+- %__p__%'`
AC_CACHE_VAL(ac_cv_lbl_lib_$ac_lib_var,
[ac_save_LIBS="$LIBS"
LIBS="-l$1 $5 $LIBS"
AC_TRY_LINK(dnl
ifelse([$2], [main], , dnl Avoid conflicting decl of main.
[/* Override any gcc2 internal prototype to avoid an error.  */
]ifelse(AC_LANG, CPLUSPLUS, [#ifdef __cplusplus
extern "C"
#endif
])dnl
[/* We use char because int might match the return type of a gcc2
    builtin and then its argument prototype would still apply.  */
char $2();
]),
	    [$2()],
	    eval "ac_cv_lbl_lib_$ac_lib_var=yes",
	    eval "ac_cv_lbl_lib_$ac_lib_var=no")
LIBS="$ac_save_LIBS"
])dnl
if eval "test \"`echo '$ac_cv_lbl_lib_'$ac_lib_var`\" = yes"; then
  AC_MSG_RESULT(yes)
  ifelse([$3], ,
[changequote(, )dnl
  ac_tr_lib=HAVE_LIB`echo $1 | sed -e 's/[^a-zA-Z0-9_]/_/g' \
    -e 'y/abcdefghijklmnopqrstuvwxyz/ABCDEFGHIJKLMNOPQRSTUVWXYZ/'`
changequote([, ])dnl
  AC_DEFINE_UNQUOTED($ac_tr_lib)
  LIBS="-l$1 $LIBS"
], [$3])
else
  AC_MSG_RESULT(no)
ifelse([$4], , , [$4
])dnl
fi
])

dnl
dnl AC_LBL_LIBRARY_NET
dnl
dnl This test is for network applications that need socket() and
dnl gethostbyname() -ish functions.  Under Solaris, those applications
dnl need to link with "-lsocket -lnsl".  Under IRIX, they need to link
dnl with "-lnsl" but should *not* link with "-lsocket" because
dnl libsocket.a breaks a number of things (for instance:
dnl gethostbyname() under IRIX 5.2, and snoop sockets under most
dnl versions of IRIX).
dnl
dnl Unfortunately, many application developers are not aware of this,
dnl and mistakenly write tests that cause -lsocket to be used under
dnl IRIX.  It is also easy to write tests that cause -lnsl to be used
dnl under operating systems where neither are necessary (or useful),
dnl such as SunOS 4.1.4, which uses -lnsl for TLI.
dnl
dnl This test exists so that every application developer does not test
dnl this in a different, and subtly broken fashion.

dnl It has been argued that this test should be broken up into two
dnl seperate tests, one for the resolver libraries, and one for the
dnl libraries necessary for using Sockets API. Unfortunately, the two
dnl are carefully intertwined and allowing the autoconf user to use
dnl them independantly potentially results in unfortunate ordering
dnl dependancies -- as such, such component macros would have to
dnl carefully use indirection and be aware if the other components were
dnl executed. Since other autoconf macros do not go to this trouble,
dnl and almost no applications use sockets without the resolver, this
dnl complexity has not been implemented.
dnl
dnl The check for libresolv is in case you are attempting to link
dnl statically and happen to have a libresolv.a lying around (and no
dnl libnsl.a).
dnl
AC_DEFUN(AC_LBL_LIBRARY_NET, [
    # Most operating systems have gethostbyname() in the default searched
    # libraries (i.e. libc):
    AC_CHECK_FUNC(gethostbyname, ,
	# Some OSes (eg. Solaris) place it in libnsl:
	AC_LBL_CHECK_LIB(nsl, gethostbyname, , 
	    # Some strange OSes (SINIX) have it in libsocket:
	    AC_LBL_CHECK_LIB(socket, gethostbyname, ,
		# Unfortunately libsocket sometimes depends on libnsl.
		# AC_CHECK_LIB's API is essentially broken so the
		# following ugliness is necessary:
		AC_LBL_CHECK_LIB(socket, gethostbyname,
		    LIBS="-lsocket -lnsl $LIBS",
		    AC_CHECK_LIB(resolv, gethostbyname),
		    -lnsl))))
    AC_CHECK_FUNC(socket, , AC_CHECK_LIB(socket, socket, ,
	AC_LBL_CHECK_LIB(socket, socket, LIBS="-lsocket -lnsl $LIBS", ,
	    -lnsl)))
    # DLPI needs putmsg under HPUX so test for -lstr while we're at it
    AC_CHECK_LIB(str, putmsg)
    ])

dnl
dnl Checks to see if struct tm has the BSD tm_gmtoff member
dnl
dnl usage:
dnl
dnl	AC_ACME_TM_GMTOFF
dnl
dnl results:
dnl
dnl	HAVE_TM_GMTOFF (defined)
dnl
AC_DEFUN(AC_ACME_TM_GMTOFF,
    [AC_MSG_CHECKING(if struct tm has tm_gmtoff member)
    AC_CACHE_VAL(ac_cv_acme_tm_has_tm_gmtoff,
	AC_TRY_COMPILE([
#	include <sys/types.h>
#	include <time.h>],
	[u_int i = sizeof(((struct tm *)0)->tm_gmtoff)],
	ac_cv_acme_tm_has_tm_gmtoff=yes,
	ac_cv_acme_tm_has_tm_gmtoff=no))
    AC_MSG_RESULT($ac_cv_acme_tm_has_tm_gmtoff)
    if test $ac_cv_acme_tm_has_tm_gmtoff = yes ; then
	    AC_DEFINE(HAVE_TM_GMTOFF)
    fi])

dnl
dnl Checks to see if int64_t exists
dnl
dnl usage:
dnl
dnl	AC_ACME_INT64T
dnl
dnl results:
dnl
dnl	HAVE_INT64T (defined)
dnl
AC_DEFUN(AC_ACME_INT64T,
    [AC_MSG_CHECKING(if int64_t exists)
    AC_CACHE_VAL(ac_cv_acme_int64_t,
	AC_TRY_COMPILE([
#	include <sys/types.h>],
	[int64_t i64],
	ac_cv_acme_int64_t=yes,
	ac_cv_acme_int64_t=no))
    AC_MSG_RESULT($ac_cv_acme_int64_t)
    if test $ac_cv_acme_int64_t = yes ; then
	    AC_DEFINE(HAVE_INT64T)
    fi])

dnl
dnl Checks to see if socklen_t exists
dnl
dnl usage:
dnl
dnl	AC_ACME_SOCKLENT
dnl
dnl results:
dnl
dnl	HAVE_SOCKLENT (defined)
dnl
AC_DEFUN(AC_ACME_SOCKLENT,
    [AC_MSG_CHECKING(if socklen_t exists)
    AC_CACHE_VAL(ac_cv_acme_socklen_t,
	AC_TRY_COMPILE([
#	include <sys/types.h>
#	include <sys/socket.h>],
	[socklen_t slen],
	ac_cv_acme_socklen_t=yes,
	ac_cv_acme_socklen_t=no))
    AC_MSG_RESULT($ac_cv_acme_socklen_t)
    if test $ac_cv_acme_socklen_t = yes ; then
	    AC_DEFINE(HAVE_SOCKLENT)
    fi])
