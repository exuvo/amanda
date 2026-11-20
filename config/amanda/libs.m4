# OVERVIEW
#
#   This file contains macros that search for specific libraries that are
#   required or utilized by Amanda.

# SYNOPSIS
#
#   AMANDA_CHECK_LIBDL
#
# OVERVIEW
#
#   Check for LIBDL support.  Sets the shell variable HAVE_CURL to "yes" or
#   "no" depending on the result of the test.  If CURL is found, the necessary
#   compiler flags are added, and a few other type checks are performed.
#
#   Note that libcurl itself defines a number of useful symbols as well; see
#   the libcurl distribution for details.
#
AC_DEFUN([AMANDA_CHECK_LIBDL], [
    AC_CHECK_LIB(dl, dlopen, [ NEED_LIBDL=1 ], [ NEED_LIBDL=0 ])
    if test "$NEED_LIBDL" = "1"; then
	LIBDL="-ldl"
	AC_SUBST(LIBDL)
    fi
])


# SYNOPSIS
#
#   AMANDA_CHECK_HMAC
#
# OVERVIEW
#
#   Check for HMAC support in -lcrypto.  If found, the shell
#   variable HAVE_HMAC will be set to 'yes'.  The appropriate one of
#   HAVE_OPENSSL_HMAC_H, HAVE_CRYPTO_HMAC_H, and HAVE_HMAC_H are also
#   defined via AC_CHECK_HEADERS.
#
AC_DEFUN([AMANDA_CHECK_HMAC], [
    HAVE_HMAC=yes
    AC_CHECK_LIB([crypto], [HMAC_CTX_init], [], [HAVE_HMAC_CTX_INIT=no])
    AC_CHECK_LIB([crypto], [HMAC_CTX_reset], [], [HAVE_HMAC_CTX_RESET=no])
    if test x"HAVE_HMAC_CTX_INIT" = x"no" -a \
	    x"HAVE_HMAC_CTX_RESET" = x"no"; then
	HAVE_HMAC=no
    fi

    found_hmac_h=no
    AC_CHECK_HEADERS([openssl/hmac.h crypto/hmac.h hmac.h],
		    [found_hmac_h=yes; break])
    if test x"$found_hmac_h" != x"yes"; then
	HAVE_HMAC=no
    fi
])

# SYNOPSIS
#
#   AMANDA_CHECK_GLIB
#
# OVERVIEW
#
#   Search for glib.  This is basically a wrapper for AM_PATH_GLIB_2_0, with
#   the addition of system-specific configuration to convince Amanda to compile
#   "out of the box" on more boxes.
#
AC_DEFUN([AMANDA_CHECK_GLIB], [
    AC_ARG_VAR(GLIB_CFLAGS, [CFLAGS to build with glib; disables use of pkg-config; must specify all GLIB_ vars])
    AC_ARG_VAR(GLIB_LIBS, [libraries to build with glib; disables use of pkg-config; must specify all GLIB_vars])
    AC_ARG_VAR(GLIB_GENMARSHAL, [genmarshal binary to use with glib; disables use of pkg-config; must specify all GLIB_ vars])
    AC_ARG_VAR(GOBJECT_QUERY, [gobject_query binary to use with glib; disables use of pkg-config; must specify all GLIB_ vars])
    AC_ARG_VAR(GLIB_MKENUMS, [mkenums binary to use with glib; disables use of pkg-config; must specify all GLIB_ vars])

    # if any of the precious variables are set, disable the pkg-config run.
    # Further, if any is specified, all must be specified.
    explicit_glib=no
    test x"$GLIB_CFLAGS" = x"" || explicit_glib=yes
    test x"$GLIB_LIBS" = x"" || explicit_glib=yes
    test x"$GLIB_GENMARSHAL" = x"" || explicit_glib=yes
    test x"$GOBJECT_QUERY" = x"" || explicit_glib=yes
    test x"$GLIB_MKENUMS" = x"" || explicit_glib=yes

    if test x"$explicit_glib" = x"no"; then
	# search for pkg-config, which the glib configuration uses, adding a few
	# system-specific search paths.
	AC_PATH_PROG(PKG_CONFIG, pkg-config, [], $LOCSYSPATH:/opt/csw/bin:/usr/local/bin:/opt/local/bin:/opt/csw/bin)

	case "$host" in
	    sparc-sun-solaris2.8) # Solaris 8
		# give the linker a runtime search path; pkg-config doesn't supply this.
		# Users could also specify this with LD_LIBRARY_PATH to both ./configure
		# and make.  Adding this support here makes straight './configure; make'
		# "just work" on Solaris 8
		if test -n "$PKG_CONFIG"; then
		    glib_R_flag=`$PKG_CONFIG glib-2.0 --libs-only-L 2>/dev/null | sed -e 's/-L/-R/g'`
		    LDFLAGS="$LDFLAGS $glib_R_flag"
		fi
		;;
	esac

	AM_PATH_GLIB_2_0(2.2.0,,[
	    AC_MSG_ERROR(glib not found or too old; See http://wiki.zmanda.com/index.php/Installation for help)
	], gmodule gobject gthread)
    else
        # Confirm that all GLIB_ variables are set
        if test ! x"$GLIB_CFLAGS" = x"" && \
           test ! x"$GLIB_LIBS" = x"" && \
           test ! x"$GLIB_GENMARSHAL" = x"" && \
           test ! x"$GOBJECT_QUERY" = x"" && \
           test ! x"$GLIB_MKENUMS" = x""; then
            :
        else
            AC_MSG_ERROR(Not all precious glib variables were set.)
        fi
    fi

    # remove deprecated warning for newer version
    if $PKG_CONFIG --atleast-version 2.30.0 glib-2.0; then
	AMANDA_DISABLE_GCC_WARNING(deprecated-declarations)
    fi

    # GLIB_CPPFLAGS is not set by autoconf, yet GLIB_CFLAGS contains what GLIB_CPPFLAGS should contain.
    AMANDA_ADD_CPPFLAGS($GLIB_CFLAGS)
    AMANDA_ADD_LIBS($GLIB_LIBS)
])

# SYNOPSIS
#
#   AMANDA_CHECK_TIRPC
#
# OVERVIEW
#
#   Search for tirpc.
#
AC_DEFUN([AMANDA_CHECK_TIRPC],
[
    AC_PATH_PROG(PKG_CONFIG, pkg-config, [], $LOCSYSPATH:/opt/csw/bin:/usr/local/bin:/opt/local/bin)

    tirpc_ld_flags=`$PKG_CONFIG libtirpc --libs-only-L 2>/dev/null`
    tirpc_lib_flags=`$PKG_CONFIG libtirpc --libs-only-l --libs-only-other 2>/dev/null`
    tirpc_cppflags=`$PKG_CONFIG libtirpc --cflags-only-I 2>/dev/null`
    tirpc_cflags=`$PKG_CONFIG libtirpc --cflags-only-other 2>/dev/null`

    _libtirpc_save_cppflags=$CPPFLAGS
    CPPFLAGS="$CPPFLAGS $tirpc_cppflags"
    _libtirpc_save_libs=$LIBS
    LIBS="$LIBS $tirpc_lib_flags"

    unset HAVE_RPC_RPC_H
    unset ac_cv_header_rpc_rpc_h
    AC_CHECK_HEADERS(rpc/rpc.h, HAVE_RPC_RPC_H=1)

    CPPFLAGS=$_libtirpc_save_cppflags
    LIBS=$_libtirpc_save_libs

    if test x"$HAVE_RPC_RPC_H" = x"1"; then
	AMANDA_ADD_LDFLAGS($tirpc_ld_flags)
	AMANDA_ADD_LIBS($tirpc_lib_flags)

	AMANDA_ADD_CPPFLAGS($tirpc_cppflags)
	AMANDA_ADD_CFLAGS($tirpc_cflags)
    fi
])


