amanda_library(libamandad SOURCES amandad_util.c LIBS libamanda)

amanda_program(amandad DEST "${amlibexecdir}"
               SOURCES amandad.c LIBS libamandad libamanda)
