# SYNOPSIS
#
#   AMANDA_SOCKLEN_T_EQUIV
#
# OVERVIEW
#
#   Find a type which will work like socklen_t should.  Unfortunately, 
#   HP/UX systems define socklen_t, but use int * as the result parameter
#   for socket functions returning a socket length.
#
#   This check defines a type socklen_t_equiv which is of the appropriate
#   size to be used with socket functions.
#
AC_DEFUN([AMANDA_SOCKLEN_T_EQUIV],
[
      AC_DEFINE([socklen_t_equiv], [socklen_t],
	[type to use for socket length parameters; use instead of socklen_t])
])
