# SYNOPSIS
#
#   AMANDA_CONFIGURE
#
# OVERVIEW
#
#   Most of the configuration
#
AC_DEFUN([AMANDA_CONFIGURE],[

AM_INIT_AUTOMAKE([tar-ustar 1.10 subdir-objects])
AC_CONFIG_HEADERS([config/config.h])

dnl Minimum Autoconf version required.
AC_PREREQ(2.64)

#
# Take care of some early Amanda-specific setup
#
AMANDA_CONFIGURE_ARGS
AMANDA_SNAPSHOT_STAMP
AMANDA_SPLIT_VERSION
AMANDA_CONFIG_LOCAL

#
# Set up compiler location, basic flags, and include locations
# and library locations before we start checking the system
# configuration in more detail...
#
AC_REQUIRE([AC_PROG_CC])
AC_REQUIRE([AC_OBJEXT])
AC_REQUIRE([AC_EXEEXT])
AC_REQUIRE([AMANDA_INIT_FLAGS])
AMANDA_ADD_CPPFLAGS([-D_GNU_SOURCE])
AMANDA_AS_NEEDED

dnl -------------------------------------------------------------------------

#
# Configuration flags (--with-xxx and --enable-xxx)
#

AMANDA_WITH_USER
AMANDA_WITH_GROUP
AMANDA_WITH_APPLICATIONDIR
AMANDA_WITH_CONFIGDIR
AMANDA_WITH_INDEXDIR
AMANDA_WITH_DBDIR
AMANDA_WITH_LOGDIR
AMANDA_WITH_SUFFIXES
AMANDA_WITH_GNUTAR_LISTDIR
AMANDA_WITH_TMPDIR
AMANDA_WITH_FORCE_UID
AMANDA_WITH_OWNER
AMANDA_WITH_FQDN
AMANDA_WITH_REUSEPORTS
AMANDA_WITH_PORTRANGES
AMANDA_WITH_BUFFERED_DUMP
AMANDA_WITH_MAXTAPEBLOCKSIZE
AMANDA_WITH_ASSERTIONS
AMANDA_WITH_DEBUGGING
AMANDA_WITH_DEBUG_DAYS
AMANDA_WITH_SINGLE_USERID
AMANDA_WITH_FAILURE_CODE
AMANDA_DISABLE_INSTALLPERMS
AMANDA_DISABLE_SYNTAX_CHECKS

dnl -------------------------------------------------------------------------

#
# Set up for Amanda components and modules
#
AC_REQUIRE([AMANDA_CHECK_COMPONENTS])
AMANDA_SETUP_DOCUMENTATION
AMANDA_SETUP_DEFAULTS
AMANDA_SETUP_LFS
AMANDA_SETUP_GETFSENT
AMANDA_SETUP_SWIG
AMANDA_CHECK_COMPRESSION
AMANDA_CHECK_IPV6
AMANDA_CHECK_READDIR
AMANDA_CHECK_DEVICE_PREFIXES
AMANDA_SYSHACKS
AMANDA_EXPAND_DIRS

#
# Dumpers
#
AMANDA_PROG_REALPATH
AMANDA_PROG_READLINK
AMANDA_PROG_GNUTAR

#
# Look for other programs Amanda will use
# 
AMANDA_PROG_GREP
AC_PROG_EGREP
AMANDA_PROG_LINT
AMANDA_PROG_LEX
AMANDA_PROG_AR
AC_PROG_AWK
AC_PROG_YACC
AC_PROG_MKDIR_P
AMANDA_PROG_DD
AMANDA_PROG_BASH
AMANDA_PROG_SORT
AMANDA_PROG_MAILER
AMANDA_PROG_MT
AMANDA_PROG_MTX
AMANDA_PROG_MOUNT
AMANDA_PROG_UMOUNT
AMANDA_PROG_UNAME
AMANDA_PROG_LPR
AMANDA_PROG_PCAT
AMANDA_PROG_PERL
AMANDA_PROG_SWIG
AMANDA_PS_ARGUMENT

dnl -------------------------------------------------------------------------

#
# Compiler / system characteristics
#

#
# compiler
#
AC_PROG_GCC_TRADITIONAL
AC_C_CONST
AC_C_BIGENDIAN


#
# Warnings
#
AMANDA_ENABLE_GCC_WARNING([parentheses])
AMANDA_ENABLE_GCC_WARNING([declaration-after-statement])
AMANDA_ENABLE_GCC_WARNING([missing-prototypes])
AMANDA_ENABLE_GCC_WARNING([strict-prototypes])
AMANDA_ENABLE_GCC_WARNING([missing-declarations])
AMANDA_ENABLE_GCC_WARNING([format])
AMANDA_ENABLE_GCC_WARNING([format-security])
AMANDA_ENABLE_GCC_WARNING([sign-compare])
AMANDA_ENABLE_GCC_WARNING([float-equal])
AMANDA_ENABLE_GCC_WARNING([old-style-definition])
AMANDA_DISABLE_GCC_WARNING([strict-aliasing])
AMANDA_DISABLE_GCC_WARNING([unknown-pragmas])
AMANDA_CHECK_SSE42
AMANDA_WERROR_FLAGS
AMANDA_SWIG_ERROR

#
# Libtool
#
AM_PROG_LIBTOOL
AC_SUBST(LIBTOOL_DEPS)
AMANDA_STATIC_FLAGS

#
# headers
#
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_TIME
AC_CHECK_HEADERS(
	grp.h \
	libc.h \
	libgen.h \
	limits.h \
	math.h \
	netinet/in.h \
	regex.h \
	stdarg.h \
	stdlib.h \
	stdint.h \
	strings.h \
	sys/file.h \
	sys/ioctl.h \
	sys/ipc.h \
	sys/mntent.h \
	sys/param.h \
	sys/select.h \
	sys/stat.h \
	sys/shm.h \
	sys/time.h \
	sys/types.h \
	sys/uio.h \
	syslog.h \
	time.h \
	unistd.h \
)
AC_DEFINE([HAVE_AMANDA_H], 1, [Define to 1 if you have the "amanda.h" header file.])
AC_DEFINE([HAVE_UTIL_H], 1, [Define to 1 if you have the "util.h" header file.])



#
# Types
#
AC_CHECK_SIZEOF(int)
AC_CHECK_SIZEOF(long)
AC_CHECK_SIZEOF(long long)
AC_CHECK_SIZEOF(intmax_t)
AC_CHECK_SIZEOF(off_t)
AC_CHECK_SIZEOF(size_t)
AC_CHECK_SIZEOF(ssize_t)
AC_CHECK_SIZEOF(time_t)
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_UID_T
AC_TYPE_SIGNAL
AC_STRUCT_TM
AMANDA_SOCKLEN_T_EQUIV
AMANDA_CHECK_TYPE(sa_family_t, unsigned short, sys/socket.h)
AMANDA_CHECK_TYPE(in_port_t, unsigned short, netinet/in.h)
CF_WAIT
CF_WAIT_INT

#
# Libraries
#
AMANDA_CHECK_GLIB
AMANDA_CHECK_READLINE
AC_CHECK_LIB(m,modf)
AMANDA_CHECK_LIBDL
AMANDA_GLIBC_BACKTRACE
AC_SEARCH_LIBS([shm_open], [rt], [], [
  AC_MSG_ERROR([unable to find the shm_open() function])
])



#
# Declarations
#
# Checks for library functions and if the function is declared in
# an appropriate header file.  Functions which exist, but for which
# no declaration is available, are declared in common-src/amanda.h.
# It's not clear that any existing system implements but does not
# declare common functions such as these.
#
AC_FUNC_ALLOCA
AC_CHECK_FUNCS(atexit)
AC_CHECK_FUNCS(basename)
AC_FUNC_CLOSEDIR_VOID
AC_CHECK_FUNCS(getgrgid_r)
AC_CHECK_FUNCS(getpwuid_r)
AC_CHECK_FUNCS(realpath)
AC_CHECK_FUNC(getpgrp)
AC_FUNC_GETPGRP
AC_CHECK_FUNCS(on_exit)
AC_CHECK_FUNCS(putenv)
AMANDA_FUNC_SELECT_ARG_TYPE
AMANDA_FUNC_SETPGID
AC_CHECK_FUNC(setpgrp,[AC_FUNC_SETPGRP])
AC_CHECK_FUNCS(sigaction sigemptyset sigvec)
AC_FUNC_STRFTIME
AC_CHECK_FUNCS(unsetenv)
AC_CHECK_FUNC(wait4)
AC_CHECK_FUNCS(sem_timedwait)

#
# Devices
#
AMANDA_CHECK_DEVICES

])
