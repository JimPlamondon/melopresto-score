# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
#
# The CLAP (CLever Audio Plug-in) interface headers, MIT licence, at one
# pinned commit of https://github.com/free-audio/clap. They are fetched with
# their LICENSE file, never copied into this repository.
#
# MUSE_MODULE_CLAP_HEADERS_PATH names a local checkout of that repository for
# a build with no network. A Git checkout there must be at the pinned commit.

set(CLAP_HEADERS_COMMIT "a47f6badb49d948fd009998f28309cdab78979c9")
set(CLAP_HEADERS_VERSION "1.2.10")

function(clap_headers_check path)
    if (NOT EXISTS "${path}/include/clap/clap.h")
        message(FATAL_ERROR "CLAP headers not found at ${path}/include/clap/clap.h")
    endif()
    if (NOT EXISTS "${path}/LICENSE")
        message(FATAL_ERROR "The CLAP headers at ${path} lack their LICENSE file; the MIT licence text must travel with them.")
    endif()
    file(STRINGS "${path}/include/clap/version.h" _version_lines REGEX "#define CLAP_VERSION_(MAJOR|MINOR|REVISION) ")
    string(REGEX REPLACE "[^0-9;]" "" _version_numbers "${_version_lines}")
    string(REPLACE ";" "." _version "${_version_numbers}")
    if (NOT _version STREQUAL CLAP_HEADERS_VERSION)
        message(FATAL_ERROR "The CLAP headers at ${path} state version ${_version}; the pinned commit states ${CLAP_HEADERS_VERSION}.")
    endif()
endfunction()

if (MUSE_MODULE_CLAP_HEADERS_PATH)
    set(CLAP_HEADERS_PATH "${MUSE_MODULE_CLAP_HEADERS_PATH}")
    if (EXISTS "${CLAP_HEADERS_PATH}/.git")
        find_package(Git QUIET)
        if (NOT GIT_FOUND)
            message(FATAL_ERROR "git is needed to confirm the commit of the CLAP headers at ${CLAP_HEADERS_PATH}")
        endif()
        execute_process(
            COMMAND ${GIT_EXECUTABLE} -C "${CLAP_HEADERS_PATH}" rev-parse HEAD
            OUTPUT_VARIABLE _clap_local_commit
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _clap_git_result)
        if (NOT _clap_git_result EQUAL 0 OR NOT _clap_local_commit STREQUAL CLAP_HEADERS_COMMIT)
            message(FATAL_ERROR
                    "The CLAP headers at ${CLAP_HEADERS_PATH} are at commit \"${_clap_local_commit}\"; "
                    "this build is pinned to ${CLAP_HEADERS_COMMIT}.")
        endif()
    else()
        message(WARNING "The CLAP headers at ${CLAP_HEADERS_PATH} are not a Git checkout: only their stated version is confirmed, not their commit.")
    endif()
else()
    include(FetchContent)
    # The headers are used; the upstream build files are not, so the fetched
    # tree is not added as a subdirectory (SOURCE_SUBDIR names no directory).
    FetchContent_Declare(
        clap_headers_source
        GIT_REPOSITORY https://github.com/free-audio/clap.git
        GIT_TAG        ${CLAP_HEADERS_COMMIT}
        SOURCE_SUBDIR  not-built-here
    )
    FetchContent_MakeAvailable(clap_headers_source)
    set(CLAP_HEADERS_PATH "${clap_headers_source_SOURCE_DIR}")
endif()

clap_headers_check("${CLAP_HEADERS_PATH}")

add_library(clap_headers INTERFACE)
target_include_directories(clap_headers SYSTEM INTERFACE "${CLAP_HEADERS_PATH}/include")

# The licence text travels with the application that was built from the headers.
install(FILES "${CLAP_HEADERS_PATH}/LICENSE"
        DESTINATION "${Mscore_SHARE_NAME}${Mscore_INSTALL_NAME}licenses"
        RENAME "CLAP_LICENSE.txt")

message(STATUS "CLAP headers ${CLAP_HEADERS_VERSION} (${CLAP_HEADERS_COMMIT}) from ${CLAP_HEADERS_PATH}")
