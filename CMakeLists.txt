cmake_minimum_required(VERSION 3.20)

project(amanda LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

find_package(Perl REQUIRED)
include(AmandaVersion)
include(AmandaOptions)
include(CheckTypeSize)
include(CTest)

# ---------------------------------------------------------------------------
# dependencies

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET
    glib-2.0 gobject-2.0 gthread-2.0 gmodule-2.0)
find_library(READLINE_LIBRARY readline REQUIRED)
find_package(SWIG REQUIRED)
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)
find_package(Threads REQUIRED)

# Perl headers for the SWIG extension modules
execute_process(COMMAND ${PERL_EXECUTABLE} -MConfig -e "print \$Config{archlibexp}"
                OUTPUT_VARIABLE PERL_ARCHLIB)
set(PERL_INC "${PERL_ARCHLIB}/CORE")

# ---------------------------------------------------------------------------
# programs Amanda runs (compiled into config.h and substituted into scripts)

macro(amanda_find_program var default)
    find_program(${var}_FOUND NAMES ${ARGN} PATHS /usr/bin /bin NO_CACHE)
    if(${var}_FOUND)
        set(${var} "${${var}_FOUND}")
    else()
        set(${var} "${default}")
    endif()
endmacro()

amanda_find_program(GNUTAR /usr/bin/tar gtar gnutar tar)
amanda_find_program(SORT /usr/bin/sort sort)
set(SORT_PATH "${SORT}")
amanda_find_program(UNAME_PATH /usr/bin/uname uname)
amanda_find_program(MAILER /usr/bin/mailx mailx mail)
amanda_find_program(LPR /usr/bin/lpr lpr)
set(LPRFLAG "-P")
amanda_find_program(DD /usr/bin/dd dd)
amanda_find_program(GREP /usr/bin/grep grep)
set(EGREP "${GREP} -E")
set(AWK "gawk")
amanda_find_program(PS /usr/bin/ps ps)
set(PS_ARGUMENT "-eo pid,ppid,command")
set(PS_ARGUMENT_ARGS "-eo pid,ppid,command")
amanda_find_program(MOUNT /usr/bin/mount mount)
amanda_find_program(UMOUNT /usr/bin/umount umount)
if(DEFINED ENV{MT})
    set(MT "$ENV{MT}")
else()
    amanda_find_program(MT /usr/bin/mt mt mt-st)
endif()
amanda_find_program(MTX /usr/bin/mtx mtx)
set(PERL "${PERL_EXECUTABLE}")
set(SHELL "/bin/sh")
# compiler command, as configure recorded it ("gcc"). Not $ENV{CC}: CMake's
# compiler detection sets that itself on the first configure.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(CC "gcc")
else()
    get_filename_component(CC "${CMAKE_C_COMPILER}" NAME)
endif()

# compression: gzip for data, zstd for index files (gzip if zstd is missing)
amanda_find_program(GZIP_PROG /usr/bin/gzip gzip)
set(HAVE_GZIP 1)
set(COMPRESS_PATH "${GZIP_PROG}")
set(COMPRESS_SUFFIX ".gz")
set(COMPRESS_FAST_OPT "--fast")
set(COMPRESS_BEST_OPT "--best")
set(UNCOMPRESS_PATH "${GZIP_PROG}")
set(UNCOMPRESS_OPT "-dc")
find_program(ZSTD_PROG zstd PATHS /usr/bin NO_CACHE)
if(ZSTD_PROG)
    set(INDEX_COMPRESS_PATH "${ZSTD_PROG}")
    set(INDEX_COMPRESS_SUFFIX ".zst")
    set(INDEX_COMPRESS_OPT "-q")
    set(INDEX_UNCOMPRESS_PATH "${ZSTD_PROG}")
    set(INDEX_UNCOMPRESS_OPT "-dcq")
else()
    set(INDEX_COMPRESS_PATH "${COMPRESS_PATH}")
    set(INDEX_COMPRESS_SUFFIX "${COMPRESS_SUFFIX}")
    set(INDEX_COMPRESS_OPT "${COMPRESS_BEST_OPT}")
    set(INDEX_UNCOMPRESS_PATH "${UNCOMPRESS_PATH}")
    set(INDEX_UNCOMPRESS_OPT "${UNCOMPRESS_OPT}")
endif()

# ---------------------------------------------------------------------------
# config.h

check_type_size(int SIZEOF_INT)
check_type_size(long SIZEOF_LONG)
set(CMAKE_EXTRA_INCLUDE_FILES sys/types.h)
check_type_size(off_t SIZEOF_OFF_T)
check_type_size(size_t SIZEOF_SIZE_T)
check_type_size(ssize_t SIZEOF_SSIZE_T)
check_type_size(time_t SIZEOF_TIME_T)
unset(CMAKE_EXTRA_INCLUDE_FILES)

set(AMANDA_CONFIGURE_ARGS "cmake -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DAMANDA_USER=${AMANDA_USER} -DAMANDA_GROUP=${AMANDA_GROUP} -DAMANDA_CONFIG_DIR=${AMANDA_CONFIG_DIR}")
# amanda.h includes "../config/config.h", which resolves through the
# <build>/common-src include directory
configure_file(cmake/config.h.cmake "${PROJECT_BINARY_DIR}/config/config.h")

# variables used by @VAR@ substitution in scripts and templates
set(VERSION "${AMANDA_VERSION}")
set(CLIENT_LOGIN "${AMANDA_USER}")
set(SETUID_GROUP "${AMANDA_GROUP}")
set(BINARY_OWNER "${AMANDA_OWNER}")
set(CONFIG_DIR "${AMANDA_CONFIG_DIR}")
set(DEFAULT_SECURITY_FILE "${AMANDA_SECURITY_FILE}")
set(DEFAULT_AMANDATES_FILE "${AMANDA_AMANDATES}")
set(GNUTAR_LISTED_INCREMENTAL_DIR "${AMANDA_GNUTAR_LISTDIR}")
set(AMANDA_DBGDIR "${AMANDA_TMPDIR}")
set(DEFAULT_CONFIG "${AMANDA_DEFAULT_CONFIG}")
set(DEFAULT_SERVER "${AMANDA_DEFAULT_SERVER}")
set(DEFAULT_TAPE_SERVER "${AMANDA_DEFAULT_TAPE_SERVER}")
set(DEFAULT_TAPE_DEVICE "${AMANDA_DEFAULT_TAPE_DEVICE}")
set(EXAMPLE_TAPEDEV "${AMANDA_EXAMPLE_TAPEDEV}")
set(AMANDA_COMPONENTS " server restore client amrecover")
set(AMANDA_DEVICES " file diskflat null tape")
set(LOCKING "POSIX_FCNTL")
# configure set these to empty values ...
foreach(_v BUILT_BRANCH BUILT_DATE BUILT_REV GZIP)
    set(${_v} "")
endforeach()
# ... and no longer defined these at all, so config.status left them as
# literal @NAME@; keep that
foreach(_v GETTEXT GNUPLOT NC6 NETCAT REST_EXTENSIONS_DIR SAMBA_CLIENT STAR)
    set(${_v} "@${_v}@")
endforeach()
set(XSLREL "current")

# ---------------------------------------------------------------------------
# compiler settings shared by all of Amanda's C code

add_compile_definitions(HAVE_CONFIG_H _GNU_SOURCE)
add_compile_options(-fno-strict-aliasing)
set(AMANDA_WARNING_FLAGS
    -Wall -Wextra -Wparentheses -Wdeclaration-after-statement
    -Wmissing-prototypes -Wstrict-prototypes -Wmissing-declarations
    -Wformat -Wformat-security -Wsign-compare -Wfloat-equal
    -Wold-style-definition -Wno-strict-aliasing -Wno-unknown-pragmas
    -Wno-deprecated-declarations)

# installed libraries and programs find the private libraries here
set(CMAKE_INSTALL_RPATH "${amlibdir}")

include(Amanda)

add_subdirectory(common-src)
add_subdirectory(amar-src)
add_subdirectory(amandad-src)
add_subdirectory(xfer-src)
add_subdirectory(device-src)
add_subdirectory(server-src)
add_subdirectory(client-src)
add_subdirectory(application-src)
add_subdirectory(recover-src)
add_subdirectory(perl)
add_subdirectory(example)
if(AMANDA_MANPAGES)
    add_subdirectory(man)
endif()
add_subdirectory(installcheck)

install(FILES ReleaseNotes COPYRIGHT NEWS ChangeLog DESTINATION "${amdatadir}")
