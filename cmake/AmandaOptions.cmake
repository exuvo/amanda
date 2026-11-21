# Build options (the configure --with-* flags this fork still uses) and the
# installation directories derived from them.

include(GNUInstallDirs)

set(AMANDA_USER  "amanda" CACHE STRING "user that runs Amanda (CLIENT_LOGIN)")
set(AMANDA_GROUP "amanda" CACHE STRING "group for setuid Amanda programs (SETUID_GROUP)")
set(AMANDA_OWNER "${AMANDA_USER}" CACHE STRING "owner of installed Amanda files (BINARY_OWNER)")

set(AMANDA_CONFIG_DIR "${CMAKE_INSTALL_FULL_SYSCONFDIR}/amanda"
    CACHE PATH "directory holding the Amanda configurations")
set(AMANDA_SECURITY_FILE "${CMAKE_INSTALL_FULL_SYSCONFDIR}/amanda-security.conf"
    CACHE FILEPATH "amanda-security.conf")
set(AMANDA_GNUTAR_LISTDIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/amanda/gnutar-lists"
    CACHE PATH "GNU tar listed-incremental directory")
set(AMANDA_AMANDATES "/etc/amandates" CACHE FILEPATH "amandates file")
set(AMANDA_TMPDIR "/tmp/amanda" CACHE PATH "temporary and debug directory")
set(AMANDA_DEBUG_DAYS 4 CACHE STRING "days to keep debug files")
set(AMANDA_DEFAULT_CONFIG "DailySet1" CACHE STRING "default configuration name")
cmake_host_system_information(RESULT _amanda_host QUERY HOSTNAME)
string(REGEX REPLACE "\\..*$" "" _amanda_host "${_amanda_host}")
set(AMANDA_DEFAULT_SERVER "${_amanda_host}" CACHE STRING "default index server")
set(AMANDA_DEFAULT_TAPE_SERVER "${AMANDA_DEFAULT_SERVER}" CACHE STRING "default tape server")
set(AMANDA_DEFAULT_TAPE_DEVICE "" CACHE STRING "default tape device")
set(AMANDA_EXAMPLE_TAPEDEV "tape:/dev/YOUR-TAPE-DEVICE-HERE" CACHE STRING "tape device in examples")

option(AMANDA_SINGLE_USERID "run everything as a single user (tests)" OFF)
option(AMANDA_FORCE_UID "check that programs run as the Amanda user" ON)
option(AMANDA_INSTALLPERMS "set ownership/setuid on install" ON)
option(AMANDA_FAILURE_CODE "build failure-injection code (tests)" OFF)
option(AMANDA_ASSERTIONS "enable extra assertions" OFF)
option(AMANDA_MANPAGES "build the man pages" ON)

# Perl modules go to Perl's site directory unless told otherwise
if(NOT AMANDA_PERLDIR)
    execute_process(COMMAND ${PERL_EXECUTABLE} -MConfig -e "print \$Config{installsitelib}"
                    OUTPUT_VARIABLE _amanda_perldir)
endif()
set(AMANDA_PERLDIR "${_amanda_perldir}" CACHE PATH "Perl module directory (amperldir)")

# directories, named as in the autotools build
set(amlibdir       "${CMAKE_INSTALL_FULL_LIBDIR}/amanda")
set(amlibexecdir   "${CMAKE_INSTALL_FULL_LIBEXECDIR}/amanda")
set(APPLICATION_DIR "${amlibexecdir}/application")
set(amdatadir      "${CMAKE_INSTALL_FULL_DATADIR}/amanda")
set(amperldir      "${AMANDA_PERLDIR}")
set(prefix         "${CMAKE_INSTALL_PREFIX}")
set(bindir         "${CMAKE_INSTALL_FULL_BINDIR}")
set(sbindir        "${CMAKE_INSTALL_FULL_SBINDIR}")
set(libexecdir     "${CMAKE_INSTALL_FULL_LIBEXECDIR}")
set(sysconfdir     "${CMAKE_INSTALL_FULL_SYSCONFDIR}")
set(localstatedir  "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}")
set(mandir         "${CMAKE_INSTALL_FULL_MANDIR}")
# autotools left these unexpanded; the scripts expand them at run time
set(exec_prefix    "\${prefix}")
set(libdir         "\${exec_prefix}/${CMAKE_INSTALL_LIBDIR}")
set(datarootdir    "\${prefix}/${CMAKE_INSTALL_DATAROOTDIR}")

# derived settings
if(AMANDA_SINGLE_USERID)
    set(SINGLE_USERID 1)
    set(WANT_SETUID_CLIENT 0)
else()
    set(SINGLE_USERID 0)
    set(WANT_SETUID_CLIENT 1)
endif()
if(AMANDA_FORCE_UID)
    set(CHECK_USERID 1)
else()
    set(CHECK_USERID "")
endif()
if(AMANDA_FAILURE_CODE)
    set(FAILURE_CODE 1)
else()
    set(FAILURE_CODE 0)
endif()
if(AMANDA_ASSERTIONS)
    set(ASSERTIONS 1)
else()
    set(ASSERTIONS "")
endif()
