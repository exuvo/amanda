# Man pages from DocBook: xml-source/X.xml -> X.proc.xml (expand-sambadoc.xsl)
# -> X (man.xsl), as man/Makefile.am did.

find_program(XSLTPROC xsltproc REQUIRED)
set(XSLTPROC_FLAGS --nonet)

amanda_subst(entities/global.entities.in "${CMAKE_CURRENT_BINARY_DIR}/entities/global.entities")
amanda_subst(xslt/man.xsl.in "${CMAKE_CURRENT_BINARY_DIR}/xslt/man.xsl")
# man.xsl imports settings.xsl from its own directory
configure_file(xslt/settings.xsl "${CMAKE_CURRENT_BINARY_DIR}/xslt/settings.xsl" COPYONLY)
set(_path "${CMAKE_CURRENT_SOURCE_DIR}/xslt/:${CMAKE_CURRENT_BINARY_DIR}/entities/")

set(AMANDA_MAN_PAGES
    # common
    amanda.8 amanda.conf.5 amanda-client.conf.5 amanda-security.conf.5
    amanda-scripts.7 amanda-archive-format.5 amanda-auth.7 amanda-match.7
    amarchiver.8 amgetconf.8 amservice.8 script-email.8
    # client
    amanda-applications.7 amgtar.8 ampgsql.8 amraw.8 amzfs-snapshot.8
    amzfs-sendrecv.8
    # server
    amanda-devices.7 amanda-compatibility.7 amanda-changers.7
    amanda-interactivity.7 amanda-taperscan.7 amadmin.8 amaespipe.8
    amcheck.8 amcheckdb.8 amcheckdump.8 amcleanup.8 amcleanupdisk.8
    amcrypt-ossl-asym.8 amcrypt-ossl.8 amcrypt.8 amcryptsimple.8
    amdevcheck.8 amdump.8 amflush.8 amgpgcrypt.8 amlabel.8 amoverview.8
    amreindex.8 amreport.8 amrmtape.8 amserverconfig.8 amstatus.8 amtape.8
    amtapetype.8 amtoc.8 amvault.8 amanda-command-file.5 disklist.5
    tapelist.5
    # recover / restore
    amrecover.8 amrestore.8 amfetchdump.8)

set(_outputs)
foreach(_page IN LISTS AMANDA_MAN_PAGES)
    set(_proc "${CMAKE_CURRENT_BINARY_DIR}/xml-source/${_page}.proc.xml")
    set(_out "${CMAKE_CURRENT_BINARY_DIR}/${_page}")
    add_custom_command(
        OUTPUT "${_proc}"
        COMMAND ${XSLTPROC} ${XSLTPROC_FLAGS} --path "${_path}" --xinclude
                --stringparam latex.imagebasedir "${_page}/" --stringparam noreference 1
                --output "${_proc}" "${CMAKE_CURRENT_SOURCE_DIR}/xslt/expand-sambadoc.xsl"
                "${CMAKE_CURRENT_SOURCE_DIR}/xml-source/${_page}.xml"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/xml-source/${_page}.xml"
                "${CMAKE_CURRENT_SOURCE_DIR}/xslt/expand-sambadoc.xsl"
                "${CMAKE_CURRENT_BINARY_DIR}/entities/global.entities")
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${XSLTPROC} ${XSLTPROC_FLAGS} --path "${_path}"
                --output "${_out}" "${CMAKE_CURRENT_BINARY_DIR}/xslt/man.xsl" "${_proc}"
        DEPENDS "${_proc}" "${CMAKE_CURRENT_BINARY_DIR}/xslt/man.xsl")
    list(APPEND _outputs "${_out}")
    string(REGEX REPLACE "^.*\\.([0-9])$" "\\1" _sect "${_page}")
    install(FILES "${_out}" DESTINATION "${CMAKE_INSTALL_MANDIR}/man${_sect}")
endforeach()
add_custom_target(manpages ALL DEPENDS ${_outputs})
