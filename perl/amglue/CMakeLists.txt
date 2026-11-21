# glue library shared by the SWIG-generated Perl modules
add_library(libamglue SHARED ghashtable.c gerror.c bigint.c source.c objwrap.c xferwrap.c)
set_target_properties(libamglue PROPERTIES
    PREFIX "" OUTPUT_NAME "libamglue-${AMANDA_VERSION}")
target_compile_definitions(libamglue PRIVATE SWIG)
target_include_directories(libamglue PRIVATE
    ${AMANDA_COMMON_INCLUDES} "${PROJECT_SOURCE_DIR}/xfer-src" "${PERL_INC}")
target_link_libraries(libamglue PRIVATE libamxfer PkgConfig::GLIB m)
amanda_file_defs(libamglue)
install(TARGETS libamglue LIBRARY DESTINATION "${amlibdir}")
install(CODE "file(CREATE_LINK \"libamglue-${AMANDA_VERSION}.so\"
    \"\$ENV{DESTDIR}${amlibdir}/libamglue.so\" SYMBOLIC)")
