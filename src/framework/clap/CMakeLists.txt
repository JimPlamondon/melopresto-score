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
# along with this program. If not, see <https://www.gnu.org/licenses/>.

# Hosting of CLAP (CLever Audio Plug-in) instruments.

include(${CMAKE_CURRENT_LIST_DIR}/SetupClap.cmake)

muse_create_module(muse_clap ALIAS muse::clap)

target_sources(muse_clap PRIVATE
    claperrors.h
    clapmodule.cpp
    clapmodule.h
    claptypes.h
    iclapinstancesregister.h
    iclapmodulesrepository.h
    iclapplugininstance.h

    internal/clapaudioclient.cpp
    internal/clapaudioclient.h
    internal/clapinstancesregister.cpp
    internal/clapinstancesregister.h
    internal/clapmodulesrepository.cpp
    internal/clapmodulesrepository.h
    internal/clapplugininstance.cpp
    internal/clapplugininstance.h
    internal/clapbundle.cpp
    internal/clapbundle.h
    internal/clapeventlist.h
    internal/claphostedplugin.cpp
    internal/claphostedplugin.h
    internal/clappluginmetareader.cpp
    internal/clappluginmetareader.h
    internal/clappluginsscanner.cpp
    internal/clappluginsscanner.h
)

# The platform headers this file includes (Core Foundation, windows.h) must
# not follow another file's using-directives in a unity build.
set_source_files_properties(internal/clapbundle.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

# The CLAP headers are part of the module's own headers (claptypes.h).
target_link_libraries(muse_clap PUBLIC clap_headers)

if (OS_IS_MAC)
    # The bundle's executable is found through CFBundle (Core Foundation Bundle).
    target_link_libraries(muse_clap PRIVATE "-framework CoreFoundation")
endif()

if (MUSE_MODULE_CLAP_TESTS)
    add_subdirectory(tests)
endif()
