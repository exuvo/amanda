# Version: the VERSION file, plus ".git.<sha>" when building from git
# (what config/set_full_version did for autotools).

file(STRINGS "${PROJECT_SOURCE_DIR}/VERSION" AMANDA_BASE_VERSION LIMIT_COUNT 1)
string(STRIP "${AMANDA_BASE_VERSION}" AMANDA_BASE_VERSION)

set(AMANDA_VERSION "${AMANDA_BASE_VERSION}")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE _amanda_git_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_amanda_git_sha)
        string(SUBSTRING "${_amanda_git_sha}" 0 8 _amanda_git_sha)
        set(AMANDA_VERSION "${AMANDA_BASE_VERSION}.git.${_amanda_git_sha}")
    endif()
endif()

# VERSION_MAJOR / MINOR / PATCH / COMMENT, as AMANDA_SPLIT_VERSION did
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.?([0-9]*)(.*)$" _m "${AMANDA_VERSION}")
set(VERSION_MAJOR "${CMAKE_MATCH_1}")
set(VERSION_MINOR "${CMAKE_MATCH_2}")
set(VERSION_PATCH "${CMAKE_MATCH_3}")
set(VERSION_COMMENT "${CMAKE_MATCH_4}")
if(VERSION_PATCH STREQUAL "")
    set(VERSION_PATCH 0)
endif()

message(STATUS "Amanda version: ${AMANDA_VERSION}")
