# Helpers shared by the per-directory CMakeLists.txt files.

# Every C file is compiled with AMANDA_FILE set to its basename (used in
# messages and debug output), as AMANDA_FILE_CFLAGS did.
function(amanda_file_defs target)
    get_target_property(_srcs ${target} SOURCES)
    foreach(_s IN LISTS _srcs)
        if(_s MATCHES "\\.(c|y|l)$")
            get_filename_component(_n "${_s}" NAME)
            string(REGEX REPLACE "\\.[yl]$" ".c" _n "${_n}")
            set_property(SOURCE "${_s}" APPEND PROPERTY
                         COMPILE_DEFINITIONS "AMANDA_FILE=\"${_n}\"")
        endif()
    endforeach()
endfunction()

# common include directories: the generated config.h/genversion.h live in
# <build>/config and <build>/common-src
set(AMANDA_COMMON_INCLUDES
    "${PROJECT_BINARY_DIR}/config"
    "${PROJECT_BINARY_DIR}/common-src"
    "${PROJECT_SOURCE_DIR}/common-src")

# amanda_library(<target> SOURCES ... [LIBS ...] [INCLUDES ...])
# A private shared library installed to amlibdir as lib<x>-<version>.so
# (libtool's -release naming) plus a lib<x>.so symlink.
function(amanda_library target)
    cmake_parse_arguments(A "" "" "SOURCES;LIBS;INCLUDES" ${ARGN})
    add_library(${target} SHARED ${A_SOURCES})
    set_target_properties(${target} PROPERTIES
        PREFIX "" OUTPUT_NAME "${target}-${AMANDA_VERSION}")
    target_include_directories(${target} PRIVATE ${A_INCLUDES} ${AMANDA_COMMON_INCLUDES})
    target_compile_options(${target} PRIVATE ${AMANDA_WARNING_FLAGS})
    # like libtool: record only the libraries listed (no transitive NEEDED)
    target_link_libraries(${target} PRIVATE ${A_LIBS} PkgConfig::GLIB m)
    # and only those linking other Amanda libraries get a RUNPATH
    if(NOT A_LIBS MATCHES "libam")
        set_target_properties(${target} PROPERTIES INSTALL_RPATH "")
    endif()
    amanda_file_defs(${target})
    install(TARGETS ${target} LIBRARY DESTINATION "${amlibdir}")
    install(CODE "file(CREATE_LINK \"${target}-${AMANDA_VERSION}.so\"
        \"\$ENV{DESTDIR}${amlibdir}/${target}.so\" SYMBOLIC)")
endfunction()

# amanda_program(<name> DEST <dir> SOURCES ... [LIBS ...] [INCLUDES ...] [NOINSTALL])
function(amanda_program name)
    cmake_parse_arguments(A "NOINSTALL" "DEST" "SOURCES;LIBS;INCLUDES" ${ARGN})
    add_executable(${name} ${A_SOURCES})
    target_include_directories(${name} PRIVATE ${A_INCLUDES} ${AMANDA_COMMON_INCLUDES})
    target_compile_options(${name} PRIVATE ${AMANDA_WARNING_FLAGS})
    target_link_libraries(${name} PRIVATE ${A_LIBS} PkgConfig::GLIB m)
    amanda_file_defs(${name})
    if(NOT A_NOINSTALL)
        install(TARGETS ${name} RUNTIME DESTINATION "${A_DEST}")
    endif()
endfunction()

# amanda_subst(<input> <output>): @VAR@ substitution, like config.status --file
function(amanda_subst input output)
    configure_file("${input}" "${output}" @ONLY)
endfunction()

# amanda_scripts(DEST <dir> [PERL names...] [SHELL names...])
# <name>.pl / <name>.sh in the current source dir -> substituted, executable
# <name> installed to <dir>.
function(amanda_scripts)
    cmake_parse_arguments(A "" "DEST" "PERL;SHELL" ${ARGN})
    foreach(_kind PERL SHELL)
        if(_kind STREQUAL "PERL")
            set(_ext pl)
        else()
            set(_ext sh)
        endif()
        foreach(_n IN LISTS A_${_kind})
            set(_out "${CMAKE_CURRENT_BINARY_DIR}/${_n}")
            configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${_n}.${_ext}" "${_out}" @ONLY
                FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                                 GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
            install(PROGRAMS "${_out}" DESTINATION "${A_DEST}")
        endforeach()
    endforeach()
endfunction()

# amanda_setuid(<dir> <name>...): root:<group> 04750, as installperms.am did.
# chown/chmod are looked up in PATH at install time so packaging can capture
# them (the PKGBUILD replaces them with wrappers that record the commands).
function(amanda_setuid dir)
    if(NOT AMANDA_INSTALLPERMS)
        return()
    endif()
    foreach(_n IN LISTS ARGN)
        install(CODE "
            message(STATUS \"chown root:${AMANDA_GROUP} ${dir}/${_n}\")
            execute_process(COMMAND chown root:${AMANDA_GROUP} \"\$ENV{DESTDIR}${dir}/${_n}\"
                            RESULT_VARIABLE _r)
            if(_r)
                message(FATAL_ERROR \"chown failed for ${dir}/${_n}\")
            endif()
            execute_process(COMMAND chmod 04750 \"\$ENV{DESTDIR}${dir}/${_n}\"
                            RESULT_VARIABLE _r)
            if(_r)
                message(FATAL_ERROR \"chmod failed for ${dir}/${_n}\")
            endif()")
    endforeach()
endfunction()

# amanda_test(<name> SOURCES ... LIBS ...): a C unit test run by ctest
function(amanda_test name)
    cmake_parse_arguments(A "" "" "SOURCES;LIBS;INCLUDES" ${ARGN})
    amanda_program(${name} NOINSTALL SOURCES ${A_SOURCES}
                   LIBS ${A_LIBS} INCLUDES ${A_INCLUDES})
    add_test(NAME ${name} COMMAND ${name}
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endfunction()
