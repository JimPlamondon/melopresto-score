# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
#
# MuseScore Studio
# Music Composition & Notation
#
# Copyright (C) 2021 MuseScore Limited and others
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 3 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

set(MODULE_TEST notationscene_qml_tests)

set(MODULE_TEST_SRC
    ${PROJECT_SOURCE_DIR}/src/engraving/tests/utils/scorerw.cpp
    ${PROJECT_SOURCE_DIR}/src/engraving/tests/utils/scorerw.h
    ${PROJECT_SOURCE_DIR}/src/engraving/tests/utils/testutils.cpp
    ${PROJECT_SOURCE_DIR}/src/framework/stubs/shortcuts/shortcutsregisterstub.cpp
    ${PROJECT_SOURCE_DIR}/src/framework/stubs/shortcuts/shortcutsregisterstub.h

    ${CMAKE_CURRENT_LIST_DIR}/mocks/controlledviewmock.h

    ${CMAKE_CURRENT_LIST_DIR}/environment.cpp
    ${CMAKE_CURRENT_LIST_DIR}/notationviewinputcontroller_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/melouimodel_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/melosaveas_tests.cpp
)

find_package(Qt6 REQUIRED COMPONENTS Test)

set(MODULE_TEST_LINK
    project
    muse_actions
    notation
    engraving
    inspector_qml
    muse_ui_qml
    muse_graphicaleffects_qml
    context
    notationscene_qml
    Qt::Quick
    Qt::Test
)

set(MODULE_TEST_DATA_ROOT ${CMAKE_CURRENT_LIST_DIR})

if (MUE_BUILD_BRAILLE_QML)
    list(APPEND MODULE_TEST_LINK braille_qml)
endif()

include(SetupGTest)

# These tests capture actual QQuickWindow pixels; the minimal platform cannot grab them.
set_tests_properties(${MODULE_TEST} PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic"
)

target_compile_definitions(${MODULE_TEST} PRIVATE MELO_UI_TEST_DATA_ROOT="${PROJECT_SOURCE_DIR}/src/engraving/tests")

target_include_directories(${MODULE_TEST} PRIVATE ${PROJECT_SOURCE_DIR}/src/inspector)
target_include_directories(${MODULE_TEST} PRIVATE ${PROJECT_SOURCE_DIR}/src/notation)
target_include_directories(${MODULE_TEST} PRIVATE ${PROJECT_SOURCE_DIR}/src/project)
target_include_directories(${MODULE_TEST} PRIVATE ${PROJECT_SOURCE_DIR}/src/framework/ui)
target_include_directories(${MODULE_TEST} PRIVATE ${PROJECT_SOURCE_DIR}/src/inspector/qml/MuseScore/Inspector)
