# Installchecks: Perl tests run against an *installed* Amanda (see the
# installcheck target below and installcheck.sh).


set(common_tests
    Amanda_Archive
    Amanda_Config
    Amanda_Config_FoldingHash
    Amanda_ClientService
    Amanda_Debug
    Amanda_Feature
    Amanda_IPC_Binary
    Amanda_IPC_LineProtocol
    Amanda_MainLoop
    Amanda_Header
    Amanda_Util
    Amanda_Xfer
    amarchiver)

set(mock_tests
    mock_mtx)

set(server_tests
    Amanda_Changer
    Amanda_Changer_disk
    Amanda_Changer_diskflat
    Amanda_Changer_null
    Amanda_Changer_robot
    Amanda_Changer_single
    Amanda_Cmdline
    Amanda_Curinfo
    Amanda_Disklist
    Amanda_DB_Catalog
    Amanda_Device
    Amanda_Holding
    Amanda_Logfile
    Amanda_Process
    Amanda_Recovery_Clerk
    Amanda_Recovery_Planner
    Amanda_Recovery_Scan
    Amanda_Report
    Amanda_Tapelist
    Amanda_Taper_Scan
    Amanda_Taper_Scan_lexical
    Amanda_Taper_Scan_oldest
    Amanda_Taper_Scan_traditional
    Amanda_Taper_Scribe
    bigint
    taper
    amcheck-device
    amgetconf
    amtape
    amlabel
    amtapetype
    chunker)

set(client_tests
    noop
    amgtar
    ampgsql
    amraw
    runtar)

set(full_tests
    0_setupcache
    amadmin
    amcheck
    amcheckdump
    amdevcheck
    amdump
    amflush
    amoverview
    amreport
    amrmtape
    amserverconfig
    amservice
    amstatus
    amvault
    example
    tape_action)

set(restore_tests
    amfetchdump
    amidxtaped
    amrestore)

set(restore_tests_no_check_userid amrecover amrecover_early_stop)

set(_tests ${common_tests} ${mock_tests} ${server_tests} ${client_tests}
           ${full_tests} ${restore_tests})
if(NOT CHECK_USERID)
    list(APPEND _tests ${restore_tests_no_check_userid})
endif()

# test scripts and helpers: <name>.pl -> <name> with @VAR@ substituted
foreach(_t IN LISTS _tests)
    amanda_subst(${_t}.pl "${CMAKE_CURRENT_BINARY_DIR}/${_t}")
endforeach()
foreach(_s mock/mail mock/mtx mock/lpr amcat-error amcat-error-end)
    configure_file(${_s}.pl "${CMAKE_CURRENT_BINARY_DIR}/${_s}" @ONLY
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                         GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach()
# deliberately not executable
amanda_subst(amcat-error-no-exec.pl "${CMAKE_CURRENT_BINARY_DIR}/amcat-error-no-exec")
configure_file(amcat.sh "${CMAKE_CURRENT_BINARY_DIR}/amcat" @ONLY
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                     GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

list(JOIN _tests " " AMANDA_INSTALLCHECK_TESTS)
configure_file(installcheck.sh.in "${CMAKE_CURRENT_BINARY_DIR}/installcheck.sh" @ONLY
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                     GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# "cmake --build . --target installcheck" after "cmake --install ."
# Like autotools, it refuses to run unless CLOBBER_MY_CONFIG=OK is set in the
# environment: it overwrites amanda-client.conf and the templates in CONFIG_DIR.
add_custom_target(installcheck
    COMMAND "${CMAKE_CURRENT_BINARY_DIR}/installcheck.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    USES_TERMINAL)
