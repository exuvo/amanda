amanda_library(libamar SOURCES amar.c LIBS libamanda)

amanda_program(amarchiver DEST "${CMAKE_INSTALL_FULL_SBINDIR}"
               SOURCES amarchiver.c LIBS libamar libamanda)

if(BUILD_TESTING)
    amanda_test(amar-test SOURCES amar-test.c LIBS libamar testutils libamanda)
endif()
