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
    //! Loading has ended, well or badly. Whoever subscribes to
    //! loadingCompleted() asks this afterwards: a notification sent before
    //! the subscription is not received.
    virtual bool isLoadFinished() const = 0;
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
    //! The same, waiting for it: for where the audio engine thread renders
    //! without pause (audio export) and would not receive settled().
    //! False if it did not happen in time.
    virtual bool settleAndWait(int timeoutMsec) = 0;

    //! Any thread: the sample rate the plug-in is to be activated at. A
    //! plug-in that is still loading is activated at the latest one.
    virtual void setWantedSampleRate(double sampleRate) = 0;

    //! Audio engine thread, after it stopped processing: the main thread
    //! deactivates the plug-in and activates it again at this sample rate,
    //! and this call waits for that. The plug-in comes back held
    //! (ClapHostedPlugin::releaseToAudio). False if it did not happen in
    //! time or the plug-in refused; if it happens later, restarted() is sent.
    virtual bool reactivateAndWait(double sampleRate, int timeoutMsec) = 0;

    //! Sent when the main thread restarted the plug-in without being waited
    //! for: the plug-in asked for a restart, or a reactivation came too late.
    //! The plug-in is held, and its ports may have changed.
    virtual async::Notification restarted() const = 0;

    //! Any thread, no allocation: something on the audio path went wrong and
    //! that path has stopped. The main thread writes it to the log.
    virtual void reportAudioFault(int fault) = 0;
};
}
