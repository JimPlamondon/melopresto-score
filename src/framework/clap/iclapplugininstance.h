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

#include "global/async/channel.h"
#include "global/async/notification.h"

#include "audio/common/audiotypes.h"

#include "claptypes.h"

namespace muse::clap {
class ClapHostedPlugin;

//! One CLAP plug-in loaded for one track: the counterpart of IVstPluginInstance.
class IClapPluginInstance
{
public:
    virtual ~IClapPluginInstance() = default;

    //! The descriptor id of the plug-in.
    virtual const muse::audio::AudioResourceId& resourceId() const = 0;
    //! The name the plug-in states for itself; the descriptor id until it is loaded.
    virtual const std::string& name() const = 0;
    virtual ClapPluginInstanceId id() const = 0;

    virtual bool isLoaded() const = 0;
    //! Sent once, when loading has ended; isLoaded() says how it ended.
    virtual async::Notification loadingCompleted() const = 0;

    //! The plug-in and its host. Main-thread and audio-thread functions are
    //! marked on ClapHostedPlugin.
    virtual ClapHostedPlugin& hosted() = 0;

    //! Audio engine thread: hand over the configuration saved in the score.
    virtual void updatePluginConfig(const muse::audio::AudioUnitConfig& config) = 0;
    //! Main thread: read the plug-in's state now and report it.
    virtual void refreshConfig() = 0;
    virtual async::Channel<muse::audio::AudioUnitConfig> pluginSettingsChanged() const = 0;
    //! Sent after a saved state was applied to an already loaded plug-in.
    virtual async::Notification stateRestored() const = 0;

    //! Audio engine thread: have the main thread serve what the plug-in asked
    //! of it, let the main event loop turn once, serve again, then send settled().
    virtual void requestSettle() = 0;
    virtual async::Notification settled() const = 0;

    //! Audio engine thread: the audio has stopped using the plug-in; activate
    //! it again at this sample rate.
    virtual void requestReactivation(double sampleRate) = 0;

    //! Any thread, no allocation: something on the audio path went wrong and
    //! that path has stopped. The main thread writes it to the log.
    virtual void reportAudioFault(int fault) = 0;
};
}
