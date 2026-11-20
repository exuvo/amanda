/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1999 University of Maryland at College Park
 * Copyright (c) 2007-2012 Zmanda, Inc.  All Rights Reserved.
 * Copyright (c) 2013-2016 Carbonite, Inc.  All Rights Reserved.
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of U.M. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  U.M. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * U.M. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL U.M.
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors: the Amanda Development Team.  Its members are listed in a
 * file named AUTHORS, in the root directory of this distribution.
 */
/*
 * $Id: amanda.h,v 1.131 2006/07/25 18:27:56 martinea Exp $
 *
 * the central header file included by all amanda sources
 */
#ifndef AMANDA_H
#define AMANDA_H

#ifdef HAVE_CONFIG_H
/* use a relative path here to avoid conflicting with Perl's config.h. */
#include "../config/config.h"
#endif

#include <glib.h>
#include <glib/gprintf.h>

#include "amflock.h"

#define GCC_VERSION (__GNUC__ * 10000 \
                     + __GNUC_MINOR__ * 100 \
                     + __GNUC_PATCHLEVEL__)

/*
 * Varargs/ellipsis handling: some broken systems DO NOT declare STDC_HEADERS,
 * which means va_start behaves differently :/ So, use our own
 */
#ifdef STDC_HEADERS

#include <stdarg.h>
#define arglist_start(arg,hook_name)	va_start(arg,hook_name)

#else /* !STDC_HEADERS */

#include <varargs.h>
#define arglist_start(arg,hook_name)	va_start(arg)

#endif /* STDC_HEADERS */

#define arglist_val(arg,type)	va_arg(arg,type)
#define arglist_end(arg)	va_end(arg)

/*
 * Force large file source even if configure guesses wrong.
 */
#ifndef  _FILE_OFFSET_BITS
#define	_FILE_OFFSET_BITS 64
#endif

#  include <sys/types.h>

/* gnulib creates this header locally if the system doesn't provide it,
 * so it uses a local ("") include */
#include "stdint.h"

/*
 * I would prefer that each Amanda module include only those system headers
 * that are locally needed, but on most Unixes the system header files are not
 * protected against multiple inclusion, so this can lead to problems.
 *
 * Also, some systems put key files in different places, so by including 
 * everything here the rest of the system is isolated from such things.
 */

/* from the autoconf documentation */
#  include <dirent.h>
#  define NAMLEN(dirent) strlen((dirent)->d_name)

#include <libintl.h>
#include <locale.h>
#define plural(String1, String2, Count)					\
		(((Count) == 1) ? (String1) : (String2))
#define T_(String)			String
#ifndef SWIG /* TODO: make this go away */
#define _(String)			dgettext("amanda", (String))
#endif

#  include <fcntl.h>

#  include <grp.h>

#if defined(USE_DB_H)
#  include <db.h>
#else
#if defined(USE_DBM_H)
#  include <dbm.h>
#else
#if defined(USE_GDBM_H)
#  include <gdbm.h>
#else
#if defined(USE_NDBM_H)
#  include <ndbm.h>
#endif
#endif
#endif
#endif

#ifdef TIME_WITH_SYS_TIME
#  include <sys/time.h>
#  include <time.h>
#else
#    include <sys/time.h>
#endif


#  include <stdlib.h>

#  include <libgen.h>

#  include <string.h>

#  include <strings.h>

#  include <syslog.h>

#  include <math.h>

#  include <sys/file.h>

#  include <sys/ioctl.h>

#include <limits.h>

#  include <sys/param.h>

#  include <sys/ipc.h>
#  include <sys/shm.h>

#  include <sys/select.h>

#  include <sys/stat.h>

#  include <sys/uio.h>

#  include <wait.h>

#  include <sys/wait.h>

#include <stdarg.h>

#ifdef WAIT_USES_INT
  typedef int amwait_t;
# ifndef WEXITSTATUS
#  define WEXITSTATUS(stat_val) (*(unsigned*)&(stat_val) >> 8)
# endif
# ifndef WTERMSIG
#  define WTERMSIG(stat_val) (*(unsigned*)&(stat_val) & 0x7F)
# endif
# ifndef WIFEXITED
#  define WIFEXITED(stat_val) ((*(unsigned*)&(stat_val) & 255) == 0)
# endif
#else
# ifdef WAIT_USES_UNION
   typedef union wait amwait_t;
#  ifndef WEXITSTATUS
#  define WEXITSTATUS(stat_val) (((amwait_t*)&(stat_val))->w_retcode)
#  endif
#  ifndef WTERMSIG
#   define WTERMSIG(stat_val) (((amwait_t*)&(stat_val))->w_termsig)
#  endif
#  ifndef WIFEXITED
#   define WIFEXITED(stat_val) (WTERMSIG(stat_val) == 0)
#  endif
# else
   typedef int amwait_t;
#  ifndef WEXITSTATUS
#   define WEXITSTATUS(stat_val) (*(unsigned*)&(stat_val) >> 8)
#  endif
#  ifndef WTERMSIG
#   define WTERMSIG(stat_val) (*(unsigned*)&(stat_val) & 0x7F)
#  endif
#  ifndef WIFEXITED
#   define WIFEXITED(stat_val) ((*(unsigned*)&(stat_val) & 255) == 0)
#  endif
# endif
#endif

#ifndef WIFSIGNALED
# define WIFSIGNALED(stat_val)	(WTERMSIG(stat_val) != 0)
#endif

#  include <unistd.h>

#   define EUIDACCESS euidaccess


#  include <netinet/in.h>

#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/socket.h>

#ifdef WORKING_IPV6
#define INET6
#endif

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif


/*
 * Most Unixen declare errno in <errno.h>, some don't.  Some multithreaded
 * systems have errno as a per-thread macro.  So, we have to be careful.
 */
#ifndef errno
extern int errno;
#endif

/*
 * Some older BSD systems don't have these FD_ macros, so if not, provide them.
 */
#if !defined(FD_SET) || defined(LINT) || defined(__lint)
#  undef FD_SETSIZE
#  define FD_SETSIZE      (int)(sizeof(fd_set) * CHAR_BIT)

#  undef FD_SET
#  define FD_SET(n, p)    (((fd_set *)(p))->fds_bits[(n)/WORD_BIT] |= (int)((1 << ((n) % WORD_BIT))))

#  undef FD_CLR
#  define FD_CLR(n, p)    (((fd_set *)(p))->fds_bits[(n)/WORD_BIT] &= (int)(~(1 << ((n) % WORD_BIT))))

#  undef FD_ISSET
#  define FD_ISSET(n, p)  (((fd_set *)(p))->fds_bits[(n)/WORD_BIT] & (1 << ((n) % WORD_BIT)))

#  undef FD_ZERO
#  define FD_ZERO(p)      memset((p), 0, sizeof(*(p)))
#endif

#ifndef FD_COPY
#  define FD_COPY(p, q)   memcpy((q), (p), sizeof(*(p)))
#endif


/*
 * Define MAX_HOSTNAME_LENGTH as the size of arrays to hold hostname's.
 */
#undef  MAX_HOSTNAME_LENGTH
#define MAX_HOSTNAME_LENGTH 1025

/*
 * If void is broken, substitute char.
 */
#ifdef BROKEN_VOID
#  define void char
#endif

#define stringize(x) #x
#define stringconcat(x, y) x ## y

/* amanda #days calculation, with roundoff */

#define SECS_PER_DAY	(24*60*60)
#define days_diff(a, b)	(int)(((b) - (a) + SECS_PER_DAY/2) / SECS_PER_DAY)

/* Global constants.  */
#ifndef AMANDA_SERVICE_NAME
#define AMANDA_SERVICE_NAME "amanda"
#endif

#define am_round(v,u)	((((v) + (u) - 1) / (u)) * (u))
#define am_floor(v,u)	(((v) / (u)) * (u))

/* Holding disk block size.  Do not even think about changint this!  :-) */
#define DISK_BLOCK_KB		32
#define DISK_BLOCK_BYTES	(DISK_BLOCK_KB * 1024)

/* Maximum length of tape label, plus one for null-terminator. */
#define MAX_TAPE_LABEL_LEN (10240)
#define MAX_TAPE_LABEL_BUF (MAX_TAPE_LABEL_LEN+1)
#define MAX_TAPE_LABEL_FMT "%10240s"

#include "sockaddr-util.h"
#include "debug.h"
#include "file.h"

/*@only@*/ /*@null@*/ char *debug_agets(const char *file, int line, FILE *f);
/*@only@*/ /*@null@*/ char *debug_areads(const char *file, int line, int fd);
#define agets(f)	      debug_agets(__FILE__,__LINE__,(f))
#define areads(f)	      debug_areads(__FILE__,__LINE__,(f))

/* return a "safe" version of the current environment; pass this to execle */
#define safe_env() safe_env_full(NULL)

/* like safe_env, but optionally add additional environment variables */
char **	safe_env_full(char **add);
void free_env(char **env);

time_t	unctime(char *timestr);

/*
 * amfree(ptr) -- if allocated, release space and set ptr to NULL.
 *
 * In general, this should be called instead of just free(), unless
 * the very next source line sets the pointer to a new value.
 */

#define	amfree(ptr) do {						\
    if((ptr) != NULL) {							\
	int e__errno = errno;						\
	free(ptr);							\
	(ptr) = NULL;							\
	errno = e__errno;						\
	(void)(ptr);  /* Fix value never used warning at end of routines */ \
    }									\
} while (0)

#define strappend(s1,s2) do {						\
    char *t_t_t = (s1) ? g_strconcat(s1, s2, NULL) : g_strdup((s2));	\
    amfree((s1));							\
    (s1) = t_t_t;							\
} while(0)

/*
 * min/max.  Don't do something like
 *
 *    x = min(y++, z);
 *
 * because the increment will be duplicated.
 */
#undef min
#undef max
#define	min(a, b)	((a) < (b) ? (a) : (b))
#define	max(a, b)	((a) > (b) ? (a) : (b))

/*
 * Utility bitmask manipulation macros.
 */
#define	SET(t, f)	((t) |= (f))
#define	CLR(t, f)	((t) &= ~((unsigned)(f)))
#define	ISSET(t, f)	((t) & (f))

/*
 * Utility string macros.  All assume a variable holds the current
 * character and the string pointer points to the next character to
 * be processed.  Typical setup is:
 *
 *  s = buffer;
 *  ch = *s++;
 *  skip_whitespace(s, ch);
 *  ...
 *
 * If you advance the pointer "by hand" to skip over something, do
 * it like this:
 *
 *  s += some_amount;
 *  ch = s[-1];
 *
 * Note that ch has the character at the end of the just skipped field.
 * It is often useful to terminate a string, make a copy, then restore
 * the input like this:
 *
 *  skip_whitespace(s, ch);
 *  fp = s-1;			## save the start
 *  skip_nonwhitespace(s, ch);	## find the end
 *  p[-1] = '\0';		## temporary terminate
 *  field = g_strdup(fp);	## make a copy
 *  p[-1] = ch;			## restore the input
 *
 * The scanning macros are:
 *
 *  skip_whitespace (ptr, var)
 *    -- skip whitespace, but stops at a newline
 *  skip_non_whitespace (ptr, var)
 *    -- skip non whitespace
 *  skip_non_whitespace_cs (ptr, var)
 *    -- skip non whitespace, stop at comment
 *  skip_integer (ptr, var)
 *    -- skip an integer field
 *  skip_line (ptr, var)
 *    -- skip just past the next newline
 *  strncmp_const (str, const_str)
 *    -- compare str to const_str, a string constant
 *  strncmp_const_skip (str, const_var, ptr, var)
 *  strncmp_const_skip_no_var(str, const_var, ptr)
 *    -- like strncmp_const, but skip the string if a match is
 *       found; this macro only tests for equality, discarding
 *       ordering information.
 *
 * where:
 *
 *  ptr -- string pointer
 *  var -- current character
 *
 * These macros copy a non-whitespace field to a new buffer, and should
 * only be used if dynamic allocation is impossible (fixed size buffers
 * are asking for trouble):
 *
 *  copy_string (ptr, var, field, len, fldptr)
 *    -- copy a non-whitespace field
 *  copy_string_cs (ptr, var, field, len, fldptr)
 *    -- copy a non-whitespace field, stop at comment
 *
 * where:
 *
 *  ptr -- string pointer
 *  var -- current character
 *  field -- area to copy to
 *  len -- length of area (needs room for null byte)
 *  fldptr -- work pointer used in move
 *	      if NULL on exit, the field was too small for the input
 */

#define	STR_SIZE	4096		/* a generic string buffer size */
#define	NUM_STR_SIZE	128		/* a generic number buffer size */

#define	skip_whitespace(ptr,c) do {					\
    while((c) != '\n' && g_ascii_isspace((int)c)) (c) = *(ptr)++;		\
} while(0)

#define	skip_non_whitespace(ptr,c) do {					\
    while((c) != '\0' && !g_ascii_isspace((int)c)) (c) = *(ptr)++;		\
} while(0)

#define	skip_non_whitespace_cs(ptr,c) do {				\
    while((c) != '\0' && (c) != '#' && !g_ascii_isspace((int)c)) (c) = *(ptr)++;\
} while(0)

#define	skip_non_integer(ptr,c) do {					\
    while((c) != '\0' && !isdigit(c)) (c) = *(ptr)++;			\
} while(0)

#define	skip_integer(ptr,c) do {					\
    if((c) == '+' || (c) == '-') (c) = *(ptr)++;			\
    while(isdigit(c)) (c) = *(ptr)++;					\
} while(0)

#define skip_quoted_string(ptr, c) do {					\
    int	iq = 0;								\
    while (((c) != '\0') && !((iq == 0) && g_ascii_isspace((int)c))) {		\
	if ((c) == '"') {						\
	    iq = !iq;							\
	} else if ((c) == '\\') {					\
	    if (*ptr)	/* not last character */			\
		(ptr)++;						\
	}								\
	(c) = *(ptr)++;							\
    }									\
} while (0)

#define	skip_quoted_line(ptr, c) do {					\
    int	iq = 0;								\
    while((c) && !((iq == 0) && ((c) == '\n'))) {			\
	if ((c) == '"')							\
	    iq = !iq;							\
	(c) = *(ptr)++;							\
    }									\
    if(c)								\
	(c) = *(ptr)++;							\
} while(0)

#define	skip_line(ptr,c) do {						\
    while((c) && (c) != '\n')						\
	(c) = *(ptr)++;							\
    if(c)								\
	(c) = *(ptr)++;							\
} while(0)

#define	copy_string(ptr,c,f,l,fp) do {					\
    (fp) = (f);								\
    while((c) != '\0' && !g_ascii_isspace((int)c)) {				\
	if((fp) >= (f) + (l) - 1) {					\
	    *(fp) = '\0';						\
	    (fp) = NULL;						\
	    (void)(fp);  /* Fix value never used warning at end of routines */ \
	    break;							\
	}								\
	*(fp)++ = (c);							\
	(c) = *(ptr)++;							\
    }									\
    if(fp)								\
	*fp = '\0';							\
} while(0)

#define	copy_string_cs(ptr,c,f,l,fp) do {				\
    (fp) = (f);								\
    while((c) != '\0' && (c) != '#' && !g_ascii_isspace((int)c)) {		\
	if((fp) >= (f) + (l) - 1) {					\
	    *(fp) = '\0';						\
	    (fp) = NULL;						\
	    break;							\
	}								\
	*(fp)++ = (c);							\
	(c) = *(ptr)++;							\
    }									\
    if(fp) *fp = '\0';							\
} while(0)

#define is_dot_or_dotdot(s)						\
    ((s)[0] == '.'							\
     && ((s)[1] == '\0'							\
	 || ((s)[1] == '.' && (s)[2] == '\0')))

#define strncmp_const(str, cnst)					\
	strncmp((str), (cnst), sizeof((cnst))-1)

/* (have to roll this up in an expression, so it can be used in if()) */
#define strncmp_const_skip(str, cnst, ptr, var)				\
	((g_str_has_prefix((str), (cnst)))?		\
		 ((ptr)+=sizeof((cnst))-1, (var)=(ptr)[-1], 0)		\
		:1)

/* (have to roll this up in an expression, so it can be used in if()) */
#define strncmp_const_skip_no_var(str, cnst, ptr)				\
	((g_str_has_prefix((str), (cnst)))?		\
		 ((ptr)+=sizeof((cnst))-1, 0)		\
		:1)

/* from old bsd-security.c */

/*
 * Handle functions which are not always declared on all systems.  This
 * stops gcc -Wall and lint from complaining.
 */

/* AIX #defines accept, and provides a prototype for the alternate name */

















/* AIX #defines getpeername, and provides a prototype for the alternate name */

/* AIX #defines getsockname, and provides a prototype for the alternate name */




#ifndef isnormal
#define	isnormal(f) (((f) < 0.0) || ((f) > 0.0))
#endif















/* AIX #defines recvfrom, and provides a prototype for the alternate name */







#define	setegid(x)	setresgid((gid_t)-1,(x),(gid_t)-1)

#define	seteuid(x)	setresuid((uid_t)-1,(x),(uid_t)-1)




#ifdef HAVE_SHMGET
#ifndef HAVE_SHMAT_DECL
extern void *shmat(int shmid, const SHM_ARG_TYPE *shmaddr, int shmflg);
#endif

#ifndef HAVE_SHMCTL_DECL
extern int shmctl(int shmid, int cmd, struct shmid_ds *buf);
#endif

#ifndef HAVE_SHMDT_DECL
extern int shmdt(SHM_ARG_TYPE *shaddr);
#endif

#ifndef HAVE_SHMGET_DECL
extern int shmget(key_t key, size_t size, int shmflg);
#endif
#endif

















/* these system headers are added by gnulib if they
 * do not exist */
#include "netdb.h"
#include "arpa/inet.h"

/* gnulib-only includes */
#include "compat.h"

#if !defined(S_ISCHR) && defined(_S_IFCHR) && defined(_S_IFMT)
#define S_ISCHR(mode) (((mode) & _S_IFMT) == _S_IFCHR)
#endif

#if !defined(S_ISREG) && defined(_S_IFREG) && defined(_S_IFMT)
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif

#ifndef HAVE_WAITPID
#ifdef HAVE_WAIT4
#define waitpid(pid,status,options) wait4(pid,status,options,0)
#else
extern pid_t waitpid(pid_t pid, amwait_t *stat_loc, int options);
#endif
#endif


#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

/* S_ISDIR is not defined on Nextstep */
#ifndef S_ISDIR
#if defined(_S_IFMT) && defined(_S_IFDIR)
#define S_ISDIR(mode)   (((mode) & (_S_IFMT)) == (_S_IFDIR))
#else
#error Don t know how to define S_ISDIR
#endif
#endif

#if SIZEOF_SIZE_T == SIZEOF_INT
#  define        SIZE_T_ATOI	(size_t)atoi
#  ifndef SIZE_MAX
#    define      SIZE_MAX	G_MAXUINT
#  endif
#else
#  define        SIZE_T_ATOI	(size_t)atol
#  ifndef SIZE_MAX
#    define      SIZE_MAX	ULONG_MAX
#  endif
#endif

#if SIZEOF_SSIZE_T == SIZEOF_INT
#  define        SSIZE_T_ATOI	(ssize_t)atoi
#  ifndef SSIZE_MAX
#    define      SSIZE_MAX	INT_MAX
#  endif
#  ifndef SSIZE_MIN
#    define      SSIZE_MIN	INT_MIN
#  endif
#else
#  define        SSIZE_T_ATOI	(ssize_t)atol
#  ifndef SSIZE_MAX
#    define      SSIZE_MAX	LONG_MAX
#  endif
#  ifndef SSIZE_MIN
#    define      SSIZE_MIN	LONG_MIN
#  endif
#endif

#if SIZEOF_TIME_T == SIZEOF_INT
#  define        TIME_T_ATOI	(time_t)atoi
#  ifndef TIME_MAX
#    define      TIME_MAX	G_MAXUINT
#  endif
#else
#  define        TIME_T_ATOI	(time_t)atol
#  ifndef TIME_MAX
#    define      TIME_MAX	ULONG_MAX
#  endif
#endif

#if SIZEOF_OFF_T > SIZEOF_LONG
#    define        OFF_T_ATOI	 (off_t)atoll
#    define        OFF_T_STRTOL	 (off_t)strtoll
#else
#  if SIZEOF_OFF_T == SIZEOF_LONG
#    define        OFF_T_ATOI	 (off_t)atol
#    define        OFF_T_STRTOL	 (off_t)strtol
#  else
#    define        OFF_T_ATOI	 (off_t)atoi
#    define        OFF_T_STRTOL	 (off_t)strtol
#  endif
#endif

#define BIND_CYCLE_RETRIES	120		/* Total of 30 minutes */

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif

typedef enum {
    KENCRYPT_NONE,	/* krb5 encryption not enabled */
    KENCRYPT_WILL_DO,	/* krb5 encryption will be enabled once amanda
		           protocol stream is closed */
    KENCRYPT_YES	/* krb5 encryption enabled on all stream */
} kencrypt_type;

#define DUMP_LEVELS    400

/* Constants to define the number of pre-opened pipes between amandad and
 * its services */

/* If you change these, change them in perl/Amanda/Constants.pm.in.src, too */
#define DATA_FD_COUNT   4               /* number of general-use pipes */
#define DATA_FD_OFFSET  150		/* Don't change it */

#endif	/* !AMANDA_H */
