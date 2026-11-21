add_subdirectory(amglue)

# --- SWIG modules --------------------------------------------------------------
# Amanda/<Mod>.swg -> Amanda/<Mod>.c + Amanda/<Mod>.pm (proxy) and the
# extension auto/Amanda/<Mod>/lib<Mod>.so, laid out like the installed tree so
# the build directory can be used on @INC.

set(AMGLUE_SWG
    amglue/amglue.swg amglue/constants.swg amglue/directtcp.swg
    amglue/dumpspecs.swg amglue/exports.swg amglue/filehandles.swg
    amglue/glib.swg amglue/integers.swg)
list(TRANSFORM AMGLUE_SWG PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

set(_perl_inc
    ${AMANDA_COMMON_INCLUDES}
    "${PROJECT_SOURCE_DIR}/amandad-src" "${PROJECT_SOURCE_DIR}/amar-src"
    "${PROJECT_SOURCE_DIR}/xfer-src" "${CMAKE_CURRENT_SOURCE_DIR}/amglue"
    "${PROJECT_SOURCE_DIR}/device-src" "${PROJECT_SOURCE_DIR}/server-src"
    "${PROJECT_SOURCE_DIR}/client-src" "${PROJECT_SOURCE_DIR}/recover-src"
    "${PERL_INC}")

# amanda_perl_module(<path under Amanda/, e.g. Config or IPC/Binary> LIBS ...)
function(amanda_perl_module path)
    cmake_parse_arguments(A "" "" "LIBS;DEPENDS" ${ARGN})
    get_filename_component(_name "${path}" NAME)
    get_filename_component(_sub "${path}" DIRECTORY)
    set(_swg "${CMAKE_CURRENT_SOURCE_DIR}/Amanda/${path}.swg")
    set(_outdir "${CMAKE_CURRENT_BINARY_DIR}/Amanda/${_sub}")
    set(_c "${CMAKE_CURRENT_BINARY_DIR}/Amanda/${path}.c")
    set(_pm "${CMAKE_CURRENT_BINARY_DIR}/Amanda/${path}.pm")
    add_custom_command(
        OUTPUT "${_c}" "${_pm}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_outdir}"
        COMMAND ${SWIG_EXECUTABLE} -perl5 -proxy -DSWIG_TYPE_TABLE=SWIG_TYPES_Amanda
                -I${CMAKE_CURRENT_SOURCE_DIR} -outdir "${_outdir}" -o "${_c}" "${_swg}"
        DEPENDS "${_swg}" "${CMAKE_CURRENT_SOURCE_DIR}/Amanda/${path}.pod"
                ${AMGLUE_SWG} ${A_DEPENDS}
        COMMENT "SWIG Amanda/${path}")
    set(_t perl_${_name})
    add_library(${_t} MODULE "${_c}")
    set_target_properties(${_t} PROPERTIES
        PREFIX "lib" OUTPUT_NAME "${_name}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/auto/Amanda/${path}")
    target_compile_definitions(${_t} PRIVATE SWIG)
    target_include_directories(${_t} PRIVATE ${_perl_inc})
    target_link_libraries(${_t} PRIVATE libamglue ${A_LIBS} PkgConfig::GLIB m)
    amanda_file_defs(${_t})
    install(TARGETS ${_t} LIBRARY DESTINATION "${amperldir}/auto/Amanda/${path}")
    install(FILES "${_pm}" DESTINATION "${amperldir}/Amanda/${_sub}")
endfunction()

amanda_perl_module(Application LIBS libamclient)
amanda_perl_module(Archive    LIBS libamar libamanda)
amanda_perl_module(IPC/Binary LIBS libamanda)
amanda_perl_module(Cmdfile    LIBS libamserver libamanda)
amanda_perl_module(Cmdline    LIBS libamserver libamanda)
amanda_perl_module(Config     LIBS libamanda)
amanda_perl_module(Debug      LIBS libamanda)
amanda_perl_module(Device     LIBS libamdevice libamxfer libamanda)
amanda_perl_module(Disklist   LIBS libamserver libamanda)
amanda_perl_module(Feature    LIBS libamanda
                   DEPENDS "${PROJECT_SOURCE_DIR}/common-src/amfeatures.h")
amanda_perl_module(Header     LIBS libamanda)
amanda_perl_module(Logfile    LIBS libamserver libamanda)
amanda_perl_module(MainLoop   LIBS libamanda)
amanda_perl_module(Tapelist   LIBS libamserver libamanda)
amanda_perl_module(Tests      LIBS libamanda)
amanda_perl_module(Util       LIBS libamanda)
amanda_perl_module(Xfer       LIBS libamxfer)
amanda_perl_module(XferServer LIBS libamserver)

# --- configured modules --------------------------------------------------------

amanda_subst(Amanda/Paths.pm.in "${CMAKE_CURRENT_BINARY_DIR}/Amanda/Paths.pm")
# Constants.pm: stamp the build date, then substitute
file(READ Amanda/Constants.pm.in.src _constants)
string(REGEX REPLACE "\n\\$BUILT_DATE[^\n]*" "\n$BUILT_DATE = \"${AMANDA_BUILT_DATE}\";" _constants "${_constants}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Constants.pm.in" "${_constants}")
amanda_subst("${CMAKE_CURRENT_BINARY_DIR}/Constants.pm.in"
             "${CMAKE_CURRENT_BINARY_DIR}/Amanda/Constants.pm")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Amanda/Paths.pm"
              "${CMAKE_CURRENT_BINARY_DIR}/Amanda/Constants.pm"
        DESTINATION "${amperldir}/Amanda")

# --- plain modules -------------------------------------------------------------

set(AMANDA_PM
    Amanda/Amdump.pm
    Amanda/Amflush.pm
    Amanda/Amservice.pm
    Amanda/Application/Zfs.pm
    Amanda/BigIntCompat.pm
    Amanda/Changer.pm
    Amanda/Changer/disk.pm
    Amanda/Changer/diskflat.pm
    Amanda/Changer/null.pm
    Amanda/Changer/robot.pm
    Amanda/Changer/single.pm
    Amanda/CheckDump.pm
    Amanda/Chunker/Controller.pm
    Amanda/Chunker/Protocol.pm
    Amanda/Chunker/Scribe.pm
    Amanda/Cleanup.pm
    Amanda/ClientService.pm
    Amanda/Config/FoldingHash.pm
    Amanda/Curinfo.pm
    Amanda/Curinfo/Info.pm
    Amanda/DB.pm
    Amanda/DB/Catalog.pm
    Amanda/Extract.pm
    Amanda/FetchDump.pm
    Amanda/FetchDump/Application.pm
    Amanda/FetchDump/ClientApplication.pm
    Amanda/FetchDump/Local.pm
    Amanda/Holding.pm
    Amanda/IPC/LineProtocol.pm
    Amanda/Index.pm
    Amanda/Interactivity.pm
    Amanda/Interactivity/email.pm
    Amanda/Interactivity/stdin.pm
    Amanda/Interactivity/tty.pm
    Amanda/Interactivity/tty_email.pm
    Amanda/Label.pm
    Amanda/Message.pm
    Amanda/Policy.pm
    Amanda/Process.pm
    Amanda/Recovery.pm
    Amanda/Recovery/Clerk.pm
    Amanda/Recovery/Planner.pm
    Amanda/Recovery/Scan.pm
    Amanda/Report.pm
    Amanda/Report/human.pm
    Amanda/Report/json.pm
    Amanda/Report/json_raw.pm
    Amanda/Report/postscript.pm
    Amanda/Report/xml.pm
    Amanda/Restore.pm
    Amanda/ScanInventory.pm
    Amanda/Script.pm
    Amanda/Script_App.pm
    Amanda/Service.pm
    Amanda/Service/Amidxtaped.pm
    Amanda/Service/Restore.pm
    Amanda/Status.pm
    Amanda/Storage.pm
    Amanda/Taper/Controller.pm
    Amanda/Taper/Protocol.pm
    Amanda/Taper/Scan.pm
    Amanda/Taper/Scan/lexical.pm
    Amanda/Taper/Scan/oldest.pm
    Amanda/Taper/Scan/traditional.pm
    Amanda/Taper/Scribe.pm
    Amanda/Taper/Worker.pm
    Amanda/Vault.pm
)
foreach(_pm IN LISTS AMANDA_PM)
    get_filename_component(_dir "${_pm}" DIRECTORY)
    install(FILES "${_pm}" DESTINATION "${amperldir}/${_dir}")
endforeach()
