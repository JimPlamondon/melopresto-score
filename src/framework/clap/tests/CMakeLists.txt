# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
#
# MuseScore Studio
# Music Composition & Notation
#
# Copyright (C) 2026 MuseScore Limited and others

set(MODULE_TEST muse_clap_tests)

set(MODULE_TEST_SRC
    ${CMAKE_CURRENT_LIST_DIR}/clapmodule_tests.cpp
)

set(MODULE_TEST_LINK
    muse_clap
    clap_headers
)

set(MODULE_TEST_INCLUDE
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/../..
)

include(SetupGTest)
