# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
#
# MuseScore Studio
# Music Composition & Notation
#
# Copyright (C) 2026 MuseScore Limited and others

# The fixture: a small CLAP (CLever Audio Plug-in) file written for these
# tests. On macOS it is a bundle whose executable is not named after the
# bundle, as real ones may be; elsewhere a ".clap" file is the library itself.
add_library(melo_clap_fixture MODULE ${CMAKE_CURRENT_LIST_DIR}/fixture/clapfixtureplugin.cpp)
target_link_libraries(melo_clap_fixture PRIVATE clap_headers)
set_target_properties(melo_clap_fixture PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
    UNITY_BUILD OFF
    PREFIX ""
    OUTPUT_NAME "fixture-code"
    CXX_VISIBILITY_PRESET hidden
)

set(CLAP_FIXTURE_EXECUTABLE "fixture-code")
if (OS_IS_MAC)
    set(CLAP_FIXTURE_PATH "${CMAKE_CURRENT_BINARY_DIR}/MeloClapFixture.clap")
    set_target_properties(melo_clap_fixture PROPERTIES SUFFIX "")
    configure_file(${CMAKE_CURRENT_LIST_DIR}/fixture/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/fixture_Info.plist @ONLY)
    add_custom_command(TARGET melo_clap_fixture POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CLAP_FIXTURE_PATH}/Contents/MacOS"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/fixture_Info.plist" "${CLAP_FIXTURE_PATH}/Contents/Info.plist"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:melo_clap_fixture>" "${CLAP_FIXTURE_PATH}/Contents/MacOS/${CLAP_FIXTURE_EXECUTABLE}"
        VERBATIM)
else()
    set(CLAP_FIXTURE_PATH "${CMAKE_CURRENT_BINARY_DIR}/MeloClapFixture.clap")
    add_custom_command(TARGET melo_clap_fixture POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:melo_clap_fixture>" "${CLAP_FIXTURE_PATH}"
        VERBATIM)
endif()

set(MODULE_TEST muse_clap_tests)

set(MODULE_TEST_SRC
    ${CMAKE_CURRENT_LIST_DIR}/clapmodule_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/clapfixture.h
    ${CMAKE_CURRENT_LIST_DIR}/claphost_tests.cpp
)

set(MODULE_TEST_LINK
    muse_clap
    muse_audio
    muse_audioplugins
    clap_headers
)

set(MODULE_TEST_INCLUDE
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/../..
)

set(MODULE_TEST_DEF
    CLAP_FIXTURE_PATH="${CLAP_FIXTURE_PATH}"
)

include(SetupGTest)

add_dependencies(muse_clap_tests melo_clap_fixture)
