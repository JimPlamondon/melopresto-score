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

#include "modularity/imoduleinterface.h"

#include "audio/common/audiotypes.h"

#include "claptypes.h"
#include "iclapplugininstance.h"

namespace muse::clap {
class IClapInstancesRegister : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IClapInstancesRegister)

public:
    virtual ~IClapInstancesRegister() = default;

    //! Each track gets a plug-in instance of its own, so two staffs using
    //! one descriptor keep separate state.
    virtual IClapPluginInstancePtr makeAndRegisterInstrPlugin(const muse::audio::AudioResourceId& resourceId,
                                                              const muse::audio::TrackId trackId, double sampleRate) = 0;

    virtual IClapPluginInstancePtr instanceById(const ClapPluginInstanceId id) const = 0;
    virtual IClapPluginInstancePtr instrumentPlugin(const muse::audio::AudioResourceId& resourceId,
                                                    const muse::audio::TrackId trackId) const = 0;

    //! By instance, not by track: a track's new instance may already be
    //! registered when the one it replaces is let go.
    virtual void unregisterById(const ClapPluginInstanceId id) = 0;
    virtual void unregisterAllInstrPlugin() = 0;
};
}
