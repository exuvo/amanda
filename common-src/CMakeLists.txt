# --- generated headers -------------------------------------------------------

# no svn: genversion.c still includes svn-info.h
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/svn-info.h" "/* no information available */\n")

# build information for genversion (CC, BUILT_DATE, BUILT_MACH); honours
# SOURCE_DATE_EPOCH like the autotools rule did
string(TIMESTAMP AMANDA_BUILT_DATE "%a %b %d %H:%M:%S UTC %Y" UTC)
execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpmachine
                OUTPUT_VARIABLE _amanda_mach OUTPUT_STRIP_TRAILING_WHITESPACE)
set(AMANDA_BUILT_DATE "${AMANDA_BUILT_DATE}" PARENT_SCOPE)
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/genversion.h" CONTENT
"#define CC \"${CC}\"
#define BUILT_DATE \"${AMANDA_BUILT_DATE}\"
#define BUILT_MACH \"${_amanda_mach}\"
")

# genversion prints the build configuration as C; version.c is its output
amanda_program(genversion NOINSTALL SOURCES genversion.c)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/version.c"
    COMMAND genversion > "${CMAKE_CURRENT_BINARY_DIR}/version.c"
    DEPENDS genversion
    COMMENT "Generating version.c")

# --- libamanda ---------------------------------------------------------------

amanda_library(libamanda
    SOURCES
        alloc.c am_sl.c amfeatures.c amflock.c amjson.c ammessage.c
        ipc-binary.c amxml.c backup_support_option.c clock.c compat.c
        conffile.c debug.c event.c file.c fileheader.c glib-util.c match.c
        mem-ring.c packet.c pipespawn.c protocol.c amsemaphore.c security.c
        security-file.c security-util.c shm-ring.c simpleprng.c
        sockaddr-util.c stream.c tapelist.c timestamp.c amutil.c
        amcrc32chw.c amflock-posix.c local-security.c
        "${CMAKE_CURRENT_BINARY_DIR}/version.c")
set_property(SOURCE amcrc32chw.c APPEND PROPERTY COMPILE_OPTIONS -msse4.2)
target_link_libraries(libamanda PRIVATE Threads::Threads)

# --- programs ----------------------------------------------------------------

amanda_program(amservice DEST "${CMAKE_INSTALL_FULL_SBINDIR}"
               SOURCES amservice.c LIBS libamanda)
amanda_program(ambind DEST "${amlibexecdir}" SOURCES ambind.c LIBS libamanda)
amanda_setuid("${amlibexecdir}" ambind)

# amanda-security.conf is written by a small build-time program
amanda_program(make_security_file NOINSTALL SOURCES make_security_file.c LIBS libamanda)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/amanda-security.conf"
    COMMAND make_security_file
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS make_security_file)
add_custom_target(amanda-security-conf ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/amanda-security.conf")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/amanda-security.conf"
        DESTINATION "${AMANDA_CONFIG_DIR}")

# --- scripts -----------------------------------------------------------------

amanda_scripts(DEST "${CMAKE_INSTALL_FULL_SBINDIR}"
    PERL amgpgcrypt amcryptsimple amgetconf
    SHELL amcrypt amaespipe amcrypt-ossl amcrypt-ossl-asym)
amanda_subst(amanda-sh-lib.sh.in "${CMAKE_CURRENT_BINARY_DIR}/amanda-sh-lib.sh")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/amanda-sh-lib.sh" DESTINATION "${amlibexecdir}")

# --- tests -------------------------------------------------------------------

if(BUILD_TESTING)
    add_library(testutils STATIC testutils.c)
    target_include_directories(testutils PRIVATE ${AMANDA_COMMON_INCLUDES})
    target_link_libraries(testutils PUBLIC PkgConfig::GLIB)
    amanda_file_defs(testutils)
    foreach(_t amflock-test event-test amsemaphore-test crc32-test quoting-test
               ipc-binary-test hexencode-test fileheader-test match-test)
        amanda_test(${_t} SOURCES ${_t}.c LIBS libamanda testutils)
    endforeach()
    set_property(SOURCE crc32-test.c APPEND PROPERTY COMPILE_OPTIONS -msse4.2)
endif()
