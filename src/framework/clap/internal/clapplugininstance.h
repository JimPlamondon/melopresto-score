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

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "../iclapplugininstance.h"
#include "../iclapmodulesrepository.h"

#include "modularity/ioc.h"
#include "global/async/asyncable.h"
#include "audio/common/iaudiothreadsecurer.h"

#include "claphostedplugin.h"
#include "clapstatewatch.h"

namespace muse::clap {
//! One CLAP plug-in for one track. It is made on the audio engine thread and
//! loaded, activated, saved, restored and destroyed on the main thread, as
//! the CLAP specification requires.
class ClapPluginInstance : public IClapPluginInstance, public muse::Contextable, public async::Asyncable,
    public std::enable_shared_from_this<ClapPluginInstance>
{
    muse::GlobalInject<muse::audio::IAudioThreadSecurer> threadSecurer;
    muse::ContextInject<IClapModulesRepository> modulesRepo { this };

public:
    ClapPluginInstance(const muse::audio::AudioResourceId& resourceId, const muse::modularity::ContextPtr& iocCtx);
    ~ClapPluginInstance() override;

    //! Audio engine thread: load and activate on the main thread.
    void load(double sampleRate);

    //! Main thread, called regularly while the application runs: serves the
    //! plug-in's main-thread callbacks, saves a changed state, restarts a
    //! plug-in that asked for it and writes audio faults to the log.
    void serviceOnMainThread();

    const muse::audio::AudioResourceId& resourceId() const override;
    const std::string& name() const override;
    ClapPluginInstanceId id() const override;

    bool isLoaded() const override;
    bool isLoadFinished() const override;
    async::Notification loadingCompleted() const override;

    ClapHostedPlugin& hosted() override;

    void updatePluginConfig(const muse::audio::AudioUnitConfig& config) override;
    void refreshConfig() override;
    async::Channel<muse::audio::AudioUnitConfig> pluginSettingsChanged() const override;
    async::Notification stateRestored() const override;

    void requestSettle() override;
    async::Notification settled() const override;
    bool settleAndWait(int timeoutMsec) override;

    void setWantedSampleRate(double sampleRate) override;
    bool reactivateAndWait(double sampleRate, int timeoutMsec) override;
    async::Notification restarted() const override;
    async::Notification flushWanted() const override;
    void reportAudioFault(int fault) override;

private:
    void doLoad();
    void finishLoad(bool loaded);
    //! Main thread. True if the plug-in is activated again, held.
    bool doReactivate();
    //! True if the configuration carried no state or the plug-in took it.
    bool applyConfig(const muse::audio::AudioUnitConfig& config);
    void sendState(const std::string& base64);

    muse::audio::AudioResourceId m_resourceId;
    std::string m_name;
    ClapPluginInstanceId m_id = -1;
    std::thread::id m_mainThreadId;

    std::shared_ptr<ClapHostedPlugin> m_hosted;
    //! When the plug-in's state is read and reported; main thread.
    std::unique_ptr<ClapStateWatch> m_stateWatch;

    std::mutex m_configMutex;
    std::optional<muse::audio::AudioUnitConfig> m_pendingConfig;
    bool m_configTaken = false;

    std::atomic<bool> m_isLoaded { false };
    std::atomic<bool> m_loadFinished { false };
    std::atomic<double> m_wantedRate { 0.0 };
    bool m_restartWanted = false;

    std::atomic<int> m_fault { 0 };

    async::Notification m_loadingCompleted;
    async::Notification m_stateRestored;
    async::Notification m_settled;
    async::Notification m_restarted;
    async::Notification m_flushWanted;
    async::Channel<muse::audio::AudioUnitConfig> m_pluginSettingsChanges;
};
}
