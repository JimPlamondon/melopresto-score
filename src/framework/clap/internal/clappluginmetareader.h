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

#include "audioplugins/iaudiopluginmetareader.h"

namespace muse::clap {
//! Reads every descriptor of a CLAP file's factory and reports one resource
//! for each instrument. A resource's id is the descriptor id.
class ClapPluginMetaReader : public audioplugins::IAudioPluginMetaReader
{
public:
    audio::AudioResourceType metaType() const override;
    bool canReadMeta(const io::path_t& pluginPath) const override;
    RetVal<audio::AudioResourceMetaList> readMeta(const io::path_t& pluginPath) const override;
};
}
