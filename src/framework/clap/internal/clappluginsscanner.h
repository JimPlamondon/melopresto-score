/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#pragma once

#include <string>
#include <vector>

#include "audioplugins/iaudiopluginsscanner.h"

namespace muse::clap {
//! The folders where CLAP plug-ins are installed on this platform, as the
//! CLAP specification (clap/entry.h) lists them.
std::vector<std::string> clapStandardFolders();

//! The folders named by a CLAP_PATH value: a list separated like PATH.
std::vector<std::string> clapFoldersOfPathValue(const std::string& clapPathValue);

//! Every ".clap" file or bundle under the folders, each once, sorted. A
//! folder that is missing or unreadable is passed over.
std::vector<std::string> clapFindPlugins(const std::vector<std::string>& folders);

//! Lists the CLAP plug-ins in the standard folders and in the folders the
//! CLAP_PATH environment variable names. There is no preferences page.
class ClapPluginsScanner : public audioplugins::IAudioPluginsScanner
{
public:
    io::paths_t scanPlugins() const override;
};
}
