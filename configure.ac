AMANDA_INIT_VERSION
AC_INIT([amanda], [AMANDA_F_VERSION])
AC_CONFIG_SRCDIR([common-src/amanda.h])
AC_CONFIG_AUX_DIR(config)
AC_CONFIG_MACRO_DIR(config)
AC_CANONICAL_HOST

PACKAGE=amanda
AC_SUBST(PACKAGE)

AMANDA_VERSION

LT_INIT

AMANDA_INIT_SUMMARY
AMANDA_CONFIGURE


dnl
dnl Set up files for autoconf substitution.  All files are listed here, in
dnl order by subdirectory, as a central location for this list.
dnl
AC_CONFIG_FILES([
    Makefile
    amandad-src/Makefile
    amar-src/Makefile
    application-src/Makefile
    client-src/Makefile
    common-src/Makefile
    config/Makefile
    device-src/Makefile
    example/Makefile
    installcheck/Makefile
    man/Makefile
    perl/amglue/Makefile
    perl/Makefile
    recover-src/Makefile
    server-src/Makefile
    xfer-src/Makefile
])

AC_OUTPUT
AMANDA_SHOW_SUMMARY
