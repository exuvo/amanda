set(_inc "${PROJECT_SOURCE_DIR}/amandad-src")
set(_libs libamanda libamclient libamandad)

amanda_library(libamclient
    SOURCES amandates.c getfsent.c unctime.c client_util.c
    LIBS libamanda
    INCLUDES ${_inc})

foreach(_p noop calcsize killpgrp runtar selfcheck senddiscover sendsize)
    amanda_program(${_p} DEST "${amlibexecdir}"
                   SOURCES ${_p}.c LIBS ${_libs} INCLUDES ${_inc})
endforeach()
amanda_program(sendbackup DEST "${amlibexecdir}"
               SOURCES sendbackup.c sendbackup-gnutar.c
               LIBS ${_libs} INCLUDES ${_inc})
if(WANT_SETUID_CLIENT)
    amanda_setuid("${amlibexecdir}" calcsize killpgrp runtar)
endif()

amanda_scripts(DEST "${amlibexecdir}" PERL restore SHELL patch-system)
