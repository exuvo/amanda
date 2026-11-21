set(_inc
    "${CMAKE_CURRENT_BINARY_DIR}"          # uparse.h
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/client-src"
    "${PROJECT_SOURCE_DIR}/amandad-src")

# yacc-compatible bison, as AC_PROG_YACC ran it ("bison -y -d")
bison_target(uparse uparse.y "${CMAKE_CURRENT_BINARY_DIR}/uparse.c"
             DEFINES_FILE "${CMAKE_CURRENT_BINARY_DIR}/uparse.h"
             COMPILE_FLAGS "-y")
flex_target(uscan uscan.l "${CMAKE_CURRENT_BINARY_DIR}/uscan.c")
add_flex_bison_dependency(uscan uparse)

amanda_program(amrecover DEST "${CMAKE_INSTALL_FULL_SBINDIR}"
    SOURCES amrecover.c display_commands.c extract_list.c help.c
            set_commands.c ${BISON_uparse_OUTPUTS} ${FLEX_uscan_OUTPUTS}
    LIBS libamanda libamclient ${READLINE_LIBRARY}
    INCLUDES ${_inc})
# generated code is not held to the warning flags (autotools compiled it
# without them too)
set_property(SOURCE ${BISON_uparse_OUTPUTS} ${FLEX_uscan_OUTPUTS}
             APPEND PROPERTY COMPILE_OPTIONS -w)
