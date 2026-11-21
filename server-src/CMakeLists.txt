set(_inc
    "${PROJECT_SOURCE_DIR}/amandad-src"
    "${PROJECT_SOURCE_DIR}/device-src"
    "${PROJECT_SOURCE_DIR}/xfer-src")
set(_libs libamanda libamserver libamdevice libamxfer)

amanda_library(libamserver
    SOURCES
        amindex.c cmdfile.c diskfile.c driverio.c cmdline.c holding.c
        infofile.c logfile.c tapefile.c find.c server_util.c
        xfer-dest-holding.c xfer-source-holding.c
    LIBS libamdevice libamanda
    INCLUDES ${_inc})

foreach(_p amadmin amcheck)
    amanda_program(${_p} DEST "${CMAKE_INSTALL_FULL_SBINDIR}"
                   SOURCES ${_p}.c LIBS ${_libs} INCLUDES ${_inc})
endforeach()
foreach(_p amtrmidx amtrmlog driver dumper planner)
    amanda_program(${_p} DEST "${amlibexecdir}"
                   SOURCES ${_p}.c LIBS ${_libs} INCLUDES ${_inc})
endforeach()
amanda_program(amindexd DEST "${amlibexecdir}"
               SOURCES amindexd.c disk_history.c list_dir.c
               LIBS ${_libs} libamandad INCLUDES ${_inc})

amanda_scripts(DEST "${amlibexecdir}"
    PERL amadmin_perl amlogroll chunker taper amcheck-device amidxtaped)
amanda_scripts(DEST "${CMAKE_INSTALL_FULL_SBINDIR}"
    PERL amoverview amserverconfig amtoc amcheckdump amcleanup amcleanupdisk
         amrmtape amlabel amreport amtape amvault amdump amflush amreindex
         amstatus amfetchdump amrestore
    SHELL amcheckdb)
