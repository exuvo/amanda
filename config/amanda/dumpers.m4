# OVERVIEW
#

# SYNOPSIS
#
#   AMANDA_PROG_GNUTAR
#
# OVERVIEW
#
#   Search for a GNU 'tar' binary, placing the result in the precious 
#   variable GNUTAR.  The discovered binary is tested to ensure it's really
#   GNU tar.
#
#   Also handle --with-gnutar
#
AC_DEFUN([AMANDA_PROG_GNUTAR],
[
    AC_REQUIRE([AMANDA_INIT_PROGS])

    # call with
    AC_ARG_WITH(gnutar,
	AS_HELP_STRING([--with-gnutar=PROG],
		       [use PROG as GNU 'tar']),
	[
	    # check withval
	    case "$withval" in
		/*) GNUTAR="$withval";;
		y|ye|yes) :;;
		n|no) GNUTAR=no ;;
		*)  AC_MSG_ERROR([*** You must supply a full pathname to --with-gnutar]);;
	    esac
	    # done
	]
    )

    if test "x$GNUTAR" = "xno"; then
	GNUTAR=
    else
        if test "x$GNUTAR" != "x"; then
	    case `"$GNUTAR" --version 2>&1` in
	      *GNU*tar* | *Free*paxutils* )
			    # OK, it is GNU tar
			    break
			    ;;
	       *)
			    AMANDA_MSG_WARN([$GNUTAR is not GNU tar, it will be used.])
			    ;;
	    esac
        else
	    OLD_GNUTAR=$GNUTAR
	    for gnutar_name in gtar gnutar tar; do
	        AC_PATH_PROGS(GNUTAR, $gnutar_name, , $LOCSYSPATH)
	        if test -n "$GNUTAR"; then
	            case `"$GNUTAR" --version 2>&1` in
	              *GNU*tar* | *Free*paxutils* )
			    # OK, it is GNU tar
			    break
			    ;;
	              *)
			    # warning..
			    AMANDA_MSG_WARN([$GNUTAR is not GNU tar, so it will not be used.])
			    # reset the cache for GNUTAR so AC_PATH_PROGS will search again
			    GNUTAR=''
			    unset ac_cv_path_GNUTAR
			    ;;
	            esac
	        fi
	    done
        fi
    fi

    if test "x$GNUTAR" = "x"; then
	GNUTAR='/usr/bin/tar'
    fi
    if test "x$GNUTAR" != "x"; then
	# find the realpath
	if test "x$REALPATH" != "x"; then
	    GNUTAR=`$REALPATH $GNUTAR 2>&1`
	else if test "x$AM_READLINK" != "x"; then
	    GNUTAR=`$AM_READLINK -e $GNUTAR 2>&1`
	fi
	fi
	# define unquoted
	AC_DEFINE_UNQUOTED(GNUTAR, "$GNUTAR", [Location of the GNU 'tar' binary])
    fi
    AC_ARG_VAR(GNUTAR, [Location of the GNU 'tar' binary])
    AC_SUBST(GNUTAR)
])

