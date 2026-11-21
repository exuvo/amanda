set(_inc "${PROJECT_SOURCE_DIR}/xfer-src")

amanda_library(libamdevice
    SOURCES
        property.c device.c directtcp-connection.c diskflat-device.c
        null-device.c vfs-device.c xfer-source-device.c xfer-dest-device.c
        xfer-dest-taper.c xfer-dest-taper-cacher.c
        xfer-dest-taper-directtcp.c xfer-dest-taper-splitter.c
        xfer-source-recovery.c tape-device.c
    LIBS libamanda libamxfer ${CMAKE_DL_LIBS}
    INCLUDES ${_inc})

amanda_scripts(DEST "${CMAKE_INSTALL_FULL_SBINDIR}" PERL amdevcheck amtapetype)
