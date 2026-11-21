set(_inc "${PROJECT_SOURCE_DIR}/client-src" "${PROJECT_SOURCE_DIR}/amandad-src")
set(_libs libamanda libamclient libamandad)

amanda_program(amgtar DEST "${APPLICATION_DIR}"
               SOURCES amgtar.c LIBS ${_libs} INCLUDES ${_inc})
amanda_program(teecount DEST "${amlibexecdir}"
               SOURCES teecount.c LIBS ${_libs} INCLUDES ${_inc})
if(WANT_SETUID_CLIENT)
    amanda_setuid("${APPLICATION_DIR}" amgtar)
endif()

amanda_scripts(DEST "${APPLICATION_DIR}"
    PERL script-email amlog-script ampgsql amzfs-sendrecv amzfs-snapshot
         amrandom amraw
    SHELL script-fail)
