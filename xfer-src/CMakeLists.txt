set(_inc "${PROJECT_SOURCE_DIR}/device-src")

amanda_library(libamxfer
    SOURCES
        dest-application.c dest-fd.c dest-null.c dest-buffer.c
        dest-directtcp-connect.c dest-directtcp-listen.c element-glue.c
        filter-crc.c filter-xor.c filter-process.c source-random.c
        source-fd.c source-file.c source-pattern.c
        source-directtcp-connect.c source-directtcp-listen.c
        source-shm-ring.c xfer-element.c xfer.c xmsg.c
    LIBS libamanda
    INCLUDES ${_inc})

if(BUILD_TESTING)
    amanda_test(xfer-test SOURCES xfer-test.c
                LIBS testutils libamxfer libamanda INCLUDES ${_inc})
endif()
