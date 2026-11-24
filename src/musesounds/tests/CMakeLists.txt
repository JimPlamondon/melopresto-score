# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
#
# MuseScore Studio
# Music Composition & Notation
#
# Copyright (C) 2026 MuseScore Limited and others
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

set(MODULE_TEST musesounds_test)

set(MODULE_TEST_SRC
    ${CMAKE_CURRENT_LIST_DIR}/mocks/musesamplercheckstatusservicemock.h
    ${CMAKE_CURRENT_LIST_DIR}/mocks/musesoundsconfigurationmock.h
    ${CMAKE_CURRENT_LIST_DIR}/musesamplercheckstatusscenario_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/musesamplercheckstatusservice_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/promotionpolicy_tests.cpp
)

set(MODULE_TEST_LINK musesounds)

# promotionpolicy_tests.cpp reads the application shell's configuration.
if (MUE_BUILD_APPSHELL_MODULE)
    list(APPEND MODULE_TEST_LINK appshell)
else()
    # The hosted unit-test configuration omits the application shell. Compile
    # the configuration under test without requiring the full shell module.
    list(APPEND MODULE_TEST_SRC ${PROJECT_SOURCE_DIR}/src/appshell/internal/appshellconfiguration.cpp)
endif()

include(SetupGTest)

target_include_directories(musesounds_test PRIVATE ${PROJECT_SOURCE_DIR}/src/appshell ${PROJECT_SOURCE_DIR}/src/musesounds)
