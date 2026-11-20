# SYNOPSIS
#
#   AMANDA_CHECK_COMPRESSION
#
# OVERVIEW
#
#   Find a suitable compression program and substitute and define the following:
#
#    - COMPRESS_PATH
#    - COMPRESS_SUFFIX
#    - COMPRESS_FAST_OPT
#    - COMPRESS_BEST_OPT
#    - UNCOMPRESS_PATH
#    - UNCOMPRESS_OPT
#    - HAVE_GZIP
#
#   and, for index files (zstd if available, else the same as above):
#
#    - INDEX_COMPRESS_PATH
#    - INDEX_COMPRESS_SUFFIX
#    - INDEX_COMPRESS_OPT
#    - INDEX_UNCOMPRESS_PATH
#    - INDEX_UNCOMPRESS_OPT
#
AC_DEFUN([AMANDA_CHECK_COMPRESSION],
[
    AC_REQUIRE([AMANDA_PROG_CAT])
    AC_REQUIRE([AMANDA_PROG_COMPRESS])
    AC_REQUIRE([AMANDA_PROG_GZIP])
    AC_REQUIRE([AMANDA_PROG_ZSTD])

    HAVE_GZIP=
    if test "$GZIP"; then
	AC_DEFINE(HAVE_GZIP,1,
	    [Define if Amanda is using the gzip program. ])
	HAVE_GZIP=1
	COMPRESS_PATH="$GZIP"
	COMPRESS_SUFFIX=".gz"
	COMPRESS_FAST_OPT="--fast"
	COMPRESS_BEST_OPT="--best"
	UNCOMPRESS_PATH="$GZIP"
	UNCOMPRESS_OPT="-dc"
    else
	if test "$COMPRESS"; then
	    COMPRESS_PATH="$COMPRESS"
	    COMPRESS_SUFFIX=".Z"
	    COMPRESS_FAST_OPT="-f"
	    COMPRESS_BEST_OPT="-f"
	    UNCOMPRESS_PATH="$COMPRESS"
	    UNCOMPRESS_OPT="-dc"
	else
	    # If we have to use cat, we don't define COMPRESS_FAST_OPT,
	    # COMPRESS_BEST_OPT, or UNCOMPRESS_OPT as "" since cat will look
	    # look for a file by the name of "".
	    # XXX is the above true? --dustin

	    AMANDA_MSG_WARN([Cannot find either gzip or compress.  Using cat.])
	    COMPRESS_PATH="$CAT"
	    COMPRESS_SUFFIX=""
	    COMPRESS_FAST_OPT=""
	    COMPRESS_BEST_OPT=""
	    UNCOMPRESS_PATH="$CAT"
	    UNCOMPRESS_OPT=""
	fi
    fi

    AC_DEFINE_UNQUOTED(COMPRESS_PATH,"$COMPRESS_PATH",
	[Define to the exact path to the gzip or the compress program. ])
    AC_DEFINE_UNQUOTED(COMPRESS_SUFFIX,"$COMPRESS_SUFFIX",
	[Define to the suffix for the COMPRESS_PATH compression program. ])
    AC_DEFINE_UNQUOTED(COMPRESS_FAST_OPT,"$COMPRESS_FAST_OPT",
	[Define as the command line option for fast compression. ])
    AC_DEFINE_UNQUOTED(COMPRESS_BEST_OPT,"$COMPRESS_BEST_OPT",
	[Define as the command line option for best compression. ])
    AC_DEFINE_UNQUOTED(UNCOMPRESS_PATH,"$UNCOMPRESS_PATH",
	[Define as the exact path to the gzip or compress command. ])
    AC_DEFINE_UNQUOTED(UNCOMPRESS_OPT,"$UNCOMPRESS_OPT",
	[Define as any optional arguments to get UNCOMPRESS_PATH to uncompress. ])

    AC_SUBST(COMPRESS_PATH)
    AC_SUBST(COMPRESS_SUFFIX)
    AC_SUBST(COMPRESS_FAST_OPT)
    AC_SUBST(COMPRESS_BEST_OPT)
    AC_SUBST(UNCOMPRESS_PATH)
    AC_SUBST(UNCOMPRESS_OPT)
    AC_SUBST(HAVE_GZIP)

    # Index files are compressed with zstd when available.  Readers still
    # accept gzip-compressed indexes, choosing the program by file suffix.
    if test "$ZSTD"; then
	INDEX_COMPRESS_PATH="$ZSTD"
	INDEX_COMPRESS_SUFFIX=".zst"
	INDEX_COMPRESS_OPT="-q"
	INDEX_UNCOMPRESS_PATH="$ZSTD"
	INDEX_UNCOMPRESS_OPT="-dcq"
    else
	INDEX_COMPRESS_PATH="$COMPRESS_PATH"
	INDEX_COMPRESS_SUFFIX="$COMPRESS_SUFFIX"
	INDEX_COMPRESS_OPT="$COMPRESS_BEST_OPT"
	INDEX_UNCOMPRESS_PATH="$UNCOMPRESS_PATH"
	INDEX_UNCOMPRESS_OPT="$UNCOMPRESS_OPT"
    fi
    AC_DEFINE_UNQUOTED(INDEX_COMPRESS_PATH,"$INDEX_COMPRESS_PATH",
	[Define to the program used to compress index files. ])
    AC_DEFINE_UNQUOTED(INDEX_COMPRESS_SUFFIX,"$INDEX_COMPRESS_SUFFIX",
	[Define to the suffix of compressed index files. ])
    AC_DEFINE_UNQUOTED(INDEX_COMPRESS_OPT,"$INDEX_COMPRESS_OPT",
	[Define as the option used to compress index files. ])
    AC_DEFINE_UNQUOTED(INDEX_UNCOMPRESS_PATH,"$INDEX_UNCOMPRESS_PATH",
	[Define to the program used to uncompress index files. ])
    AC_DEFINE_UNQUOTED(INDEX_UNCOMPRESS_OPT,"$INDEX_UNCOMPRESS_OPT",
	[Define as the option used to uncompress index files to stdout. ])
    AC_SUBST(INDEX_COMPRESS_PATH)
    AC_SUBST(INDEX_COMPRESS_SUFFIX)
    AC_SUBST(INDEX_COMPRESS_OPT)
    AC_SUBST(INDEX_UNCOMPRESS_PATH)
    AC_SUBST(INDEX_UNCOMPRESS_OPT)

    # Empty GZIP so that make dist works.
    GZIP=
])
