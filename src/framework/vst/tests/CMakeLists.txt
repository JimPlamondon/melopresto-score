# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-CLA-applies
#
# MuseScore
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

# melopresto-score (MeloSynth VST3 workstream, 2026-08-19): host-side tests for
# the VST sequencer (unique note identifiers, exact optional JiMS pitch,
# per-note tuning expression, capability-gated lattice identity), the
# id-keyed active-note tracking, and the optional exact-pitch RPC tail.

set(MODULE_TEST muse_vst_tests)

if (MUSE_MODULE_VST_VST3_SDK_PATH)
    set(VST_TEST_SDK_PATH ${MUSE_MODULE_VST_VST3_SDK_PATH})
else()
    set(VST_TEST_SDK_PATH ${PROJECT_BINARY_DIR}/_deps/vst3sdk)
endif()

# The sequencer is compiled into the test directly (it depends only on the
# audio/mpe types and the VST3 SDK headers); linking the whole muse_vst module
# would drag the plug-in editor dialog and its UI modules into a unit test.
set(MODULE_TEST_SRC
    ${CMAKE_CURRENT_LIST_DIR}/vstsequencer_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/vstactivenotes_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/vstexactpitchrpc_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/vstnoteeventbridge_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/vstpernotepitchadapter_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/vstdynamictonalityprofile_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/vstpendinghostparameters_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/vstpersistenthostparams_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../internal/vstaudioclient.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../internal/vstcomponenthandler.cpp
    # The production VST host does not instantiate SDK AudioEffect or
    # EditController bases, while this real-client test does through its
    # capturing plug-in. Keep these test-only SDK implementations local.
    ${VST_TEST_SDK_PATH}/public.sdk/source/vst/vstaudioeffect.cpp
    ${VST_TEST_SDK_PATH}/public.sdk/source/vst/vstbus.cpp
    ${VST_TEST_SDK_PATH}/public.sdk/source/vst/vstcomponent.cpp
    ${VST_TEST_SDK_PATH}/public.sdk/source/vst/vstcomponentbase.cpp
    ${VST_TEST_SDK_PATH}/public.sdk/source/vst/vsteditcontroller.cpp
    ${VST_TEST_SDK_PATH}/public.sdk/source/vst/vstparameters.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../internal/synth/vstsequencer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../internal/synth/vstsequencer.h
    ${CMAKE_CURRENT_LIST_DIR}/../internal/vstactivenotes.h
    ${CMAKE_CURRENT_LIST_DIR}/../internal/vstnoteeventbridge.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../internal/vstnoteeventbridge.h
    ${CMAKE_CURRENT_LIST_DIR}/../internal/vstpernotepitchadapter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../internal/vstpernotepitchadapter.h
    ${CMAKE_CURRENT_LIST_DIR}/../internal/vstdynamictonalityprofile.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../internal/vstdynamictonalityprofile.h
)

set(MODULE_TEST_LINK
    muse_audio
    muse_mpe
    vst3sdk
    melo_musescore_bridge
)

set(MODULE_TEST_INCLUDE
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/../..
)

include(SetupGTest)
