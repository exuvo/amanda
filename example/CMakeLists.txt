set(_tmpl "${amdatadir}/template.d")
set(_ex   "${amdatadir}/example")

foreach(_f template.d/advanced.conf template.d/amanda-harddisk.conf
           template.d/amanda-single-tape.conf template.d/amanda-tape-changer.conf)
    amanda_subst("${_f}.in" "${CMAKE_CURRENT_BINARY_DIR}/${_f}")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${_f}" DESTINATION "${_tmpl}")
endforeach()
install(FILES template.d/dumptypes template.d/tapetypes template.d/README
              template.d/chg-manual.conf
        DESTINATION "${_tmpl}")

foreach(_f amanda.conf amanda-client.conf amanda-client-postgresql.conf)
    amanda_subst("${_f}.in" "${CMAKE_CURRENT_BINARY_DIR}/${_f}")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${_f}" DESTINATION "${_ex}")
endforeach()
install(FILES chg-scsi.conf disklist DESTINATION "${_ex}")

install(FILES DLT.ps EXB-8500.ps HP-DAT.ps 8.5x11.ps 3hole.ps DLT-A4.ps DIN-A4.ps
        DESTINATION "${_ex}/label-templates")
