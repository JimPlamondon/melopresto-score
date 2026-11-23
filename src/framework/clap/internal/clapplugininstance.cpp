/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "clapplugininstance.h"

#include <chrono>
#include <condition_variable>

#include <QByteArray>
#include <QCoreApplication>
#include <QTimer>

#include "global/async/async.h"

#include "clapaudioclient.h"

#include "log.h"

using namespace muse;
using namespace muse::clap;
using namespace muse::async;

namespace muse::clap {
static std::atomic<ClapPluginInstanceId> s_lastClapInstanceId { 0 };

//! What the audio engine thread waits on while the main thread does
//! something for it.
struct ClapMainThreadWait {
    std::mutex mutex;
    std::condition_variable changed;
    bool done = false;
    bool ok = false;
    //! The waiter gave up; what the main thread still does is reported another way.
    bool abandoned = false;

    //! Returns whether the waiter had given up by now.
    bool finish(bool result)
    {
        bool gaveUp = false;
        {
            std::lock_guard lock(mutex);
            ok = result;
            done = true;
            gaveUp = abandoned;
        }
        changed.notify_all();
        return gaveUp;
    }

    //! True if the main thread finished in time; otherwise the wait is abandoned.
    bool wait(int timeoutMsec)
    {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, std::chrono::milliseconds(timeoutMsec), [this]() { return done; })) {
            abandoned = true;
            return false;
        }
        return true;
    }
};
}

ClapPluginInstance::ClapPluginInstance(const audio::AudioResourceId& resourceId, const modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx), m_resourceId(resourceId), m_name(resourceId), m_hosted(std::make_shared<ClapHostedPlugin>()),
    m_stateWatch(std::make_unique<ClapStateWatch>(*m_hosted))
{
    m_id = ++s_lastClapInstanceId;
    m_mainThreadId = threadSecurer()->mainThreadId();
}

ClapPluginInstance::~ClapPluginInstance()
{
    m_isLoaded = false;
    m_stateWatch.reset();

    // The plug-in is deactivated and destroyed on the main thread, wherever
    // the last owner of this instance let go of it. The audio side has
    // already stopped processing (ClapSynthesiser's destructor).
    std::shared_ptr<ClapHostedPlugin> hosted = std::move(m_hosted);
    Async::call(nullptr, [hosted]() mutable {
        hosted->destroy();
        hosted.reset();
    }, m_mainThreadId);
}

void ClapPluginInstance::load(double sampleRate)
{
    m_wantedRate.store(sampleRate);

    std::weak_ptr<ClapPluginInstance> weak = weak_from_this();
    Async::call(this, [weak]() {
        if (std::shared_ptr<ClapPluginInstance> self = weak.lock()) {
            self->doLoad();
        }
    }, m_mainThreadId);
}

void ClapPluginInstance::finishLoad(bool loaded)
{
    // The flags are set before the notification goes out: whoever subscribes
    // too late to receive it asks isLoadFinished().
    m_isLoaded = loaded;
    m_loadFinished = true;
    m_loadingCompleted.notify();
}

void ClapPluginInstance::doLoad()
{
    ONLY_MAIN_THREAD(threadSecurer);

    // Whatever configuration arrives from now on is applied after this load.
    std::optional<audio::AudioUnitConfig> config;
    {
        std::lock_guard lock(m_configMutex);
        config = std::move(m_pendingConfig);
        m_pendingConfig.reset();
        m_configTaken = true;
    }

    // Every way out sends loadingCompleted, so that whoever waits for this
    // plug-in stops waiting; isLoaded() says how the load ended.
    const ClapBundlePtr bundle = modulesRepo()->bundle(m_resourceId);
    if (!bundle) {
        LOGE() << "CLAP plug-in " << m_resourceId << " is missing or could not be opened. "
               << "The staff stays assigned to it, with its saved state, and is silent.";
        finishLoad(false);
        return;
    }

    std::string problem;
    if (!m_hosted->create(bundle, m_resourceId, problem)) {
        LOGE() << "CLAP plug-in " << m_resourceId << " could not be created: " << problem;
        finishLoad(false);
        return;
    }
    m_name = m_hosted->descriptor().name.empty() ? m_resourceId : m_hosted->descriptor().name;

    // The saved state goes in before the plug-in is activated, so the first
    // note already sounds with it.
    if (config.has_value() && !applyConfig(*config)) {
        LOGE() << "CLAP plug-in " << m_resourceId << " refused its saved state. "
               << "That state is kept in the score; it is not replaced by the plug-in's default.";
    }
    // What the plug-in holds now is what the score holds: only a state that
    // differs from it is reported, so opening a score marks nothing as modified.
    if (!m_stateWatch->isRejected()) {
        m_stateWatch->setBaseline();
    }

    // The sample rate wanted now, which may be newer than the one the load
    // was asked with; and again if a newer one arrived while activating.
    double rate = m_wantedRate.load();
    bool activated = m_hosted->activate(rate);
    while (activated && m_wantedRate.load() != rate) {
        rate = m_wantedRate.load();
        m_hosted->requestAudioStop();
        m_hosted->deactivate();
        activated = m_hosted->activate(rate);
    }
    if (!activated) {
        LOGE() << "CLAP plug-in " << m_resourceId << " refused to activate at " << rate << " Hz";
        m_hosted->destroy();
        finishLoad(false);
        return;
    }

    finishLoad(true);
}

bool ClapPluginInstance::doReactivate()
{
    ONLY_MAIN_THREAD(threadSecurer);

    if (!m_isLoaded || !m_hosted->requestAudioStop()) {
        return false;
    }
    m_hosted->deactivate();

    // Held: the ports are read again at activation and may have changed, so
    // the audio thread waits until the audio side has prepared for them.
    const double rate = m_wantedRate.load();
    if (!m_hosted->activate(rate, CLAP_MAX_BLOCK_FRAMES, /*held*/ true)) {
        LOGE() << "CLAP plug-in " << m_resourceId << " refused to activate at " << rate << " Hz";
        return false;
    }
    return true;
}

bool ClapPluginInstance::applyConfig(const audio::AudioUnitConfig& config)
{
    ONLY_MAIN_THREAD(threadSecurer);

    auto state = config.find(std::string(CLAP_STATE_KEY));
    if (state == config.end()) {
        return true;
    }
    return m_stateWatch->restore(state->second);
}

void ClapPluginInstance::sendState(const std::string& base64)
{
    ONLY_MAIN_THREAD(threadSecurer);

    LOGI() << "CLAP plug-in " << m_resourceId << ": a changed state is sent toward the score (" << base64.size()
           << " characters of base64)";

    audio::AudioUnitConfig config;
    config.emplace(std::string(CLAP_STATE_KEY), base64);
    m_pluginSettingsChanges.send(config);
}

void ClapPluginInstance::serviceOnMainThread()
{
    ONLY_MAIN_THREAD(threadSecurer);

    const int fault = m_fault.exchange(0);
    if (fault != 0) {
        LOGE() << "CLAP plug-in " << m_resourceId << ": " << clapAudioFaultText(static_cast<ClapAudioFault>(fault));
    }

    if (!m_isLoaded) {
        return;
    }

    m_hosted->serveMainThreadCallbacks();

    // The plug-in asked to be restarted. That waits until the audio thread
    // has stopped processing; then the audio side is told, and prepares again.
    if (m_hosted->takeRestartRequested()) {
        m_restartWanted = true;
    }
    if (m_restartWanted && m_hosted->requestAudioStop()) {
        m_restartWanted = false;
        if (doReactivate()) {
            m_restarted.notify();
        }
    }

    // The first time this plug-in gives each sign of a changed state, the log
    // says so: it is how one finds out what a plug-in does on an edit in its editor.
    const uint32_t signs = m_hosted->signalsSeen();
    if (signs != m_signalsLogged) {
        const uint32_t fresh = signs & ~m_signalsLogged;
        m_signalsLogged = signs;
        LOGI() << "CLAP plug-in " << m_resourceId << " signals a change of state by:"
               << ((fresh& ClapHostedPlugin::MarkedDirty) ? " state.mark_dirty" : "")
               << ((fresh& ClapHostedPlugin::AskedForFlush) ? " params.request_flush" : "")
               << ((fresh& ClapHostedPlugin::AskedForRescan) ? " params.rescan" : "")
               << ((fresh& ClapHostedPlugin::ReportedOnOutputEvents) ? " a parameter value or gesture on the output events" : "");
    }

    // The plug-in asked for a parameter flush: the audio side is told.
    if (m_hosted->takeFlushRequested()) {
        m_flushWanted.notify();
    }

    // The state is read when it may have changed and has come to rest, and
    // now and then while the editor is open; it is sent on when it differs.
    std::string state;
    if (m_stateWatch->visit(state)) {
        sendState(state);
    }
}

const audio::AudioResourceId& ClapPluginInstance::resourceId() const
{
    return m_resourceId;
}

const std::string& ClapPluginInstance::name() const
{
    return m_name;
}

ClapPluginInstanceId ClapPluginInstance::id() const
{
    return m_id;
}

bool ClapPluginInstance::isLoaded() const
{
    return m_isLoaded;
}

bool ClapPluginInstance::isLoadFinished() const
{
    return m_loadFinished;
}

Notification ClapPluginInstance::loadingCompleted() const
{
    return m_loadingCompleted;
}

ClapHostedPlugin& ClapPluginInstance::hosted()
{
    return *m_hosted;
}

void ClapPluginInstance::updatePluginConfig(const audio::AudioUnitConfig& config)
{
    {
        std::lock_guard lock(m_configMutex);
        if (!m_configTaken) {
            m_pendingConfig = config;
            return;
        }
    }

    std::weak_ptr<ClapPluginInstance> weak = weak_from_this();
    Async::call(this, [weak, config]() {
        std::shared_ptr<ClapPluginInstance> self = weak.lock();
        if (!self || !self->m_isLoaded) {
            return;
        }
        if (config.find(std::string(CLAP_STATE_KEY)) == config.end()) {
            return;
        }
        if (self->applyConfig(config)) {
            self->m_stateRestored.notify();
        } else {
            LOGE() << "CLAP plug-in " << self->m_resourceId << " refused a saved state; that state is kept";
        }
    }, m_mainThreadId);
}

void ClapPluginInstance::refreshConfig()
{
    ONLY_MAIN_THREAD(threadSecurer);

    if (!m_isLoaded) {
        return;
    }
    // The editor was closed: the state is read now and sent on if it differs.
    std::string state;
    if (m_stateWatch->editorClosed(state)) {
        sendState(state);
    }
}

async::Channel<audio::AudioUnitConfig> ClapPluginInstance::pluginSettingsChanged() const
{
    return m_pluginSettingsChanges;
}

Notification ClapPluginInstance::stateRestored() const
{
    return m_stateRestored;
}

void ClapPluginInstance::requestSettle()
{
    std::weak_ptr<ClapPluginInstance> weak = weak_from_this();
    Async::call(this, [weak]() {
        std::shared_ptr<ClapPluginInstance> self = weak.lock();
        if (!self) {
            return;
        }
        if (self->m_isLoaded) {
            self->m_hosted->serveMainThreadCallbacks();
        }
        // Some plug-ins apply setup through the main event loop itself and
        // not through request_callback. Let the loop turn once, then serve
        // again, before saying the plug-in has settled.
        QTimer::singleShot(0, qApp, [weak]() {
            std::shared_ptr<ClapPluginInstance> again = weak.lock();
            if (!again) {
                return;
            }
            if (again->m_isLoaded) {
                again->m_hosted->serveMainThreadCallbacks();
            }
            again->m_settled.notify();
        });
    }, m_mainThreadId);
}

Notification ClapPluginInstance::settled() const
{
    return m_settled;
}

bool ClapPluginInstance::settleAndWait(int timeoutMsec)
{
    std::shared_ptr<ClapMainThreadWait> wait = std::make_shared<ClapMainThreadWait>();
    std::weak_ptr<ClapPluginInstance> weak = weak_from_this();
    Async::call(this, [weak, wait]() {
        std::shared_ptr<ClapPluginInstance> self = weak.lock();
        if (!self) {
            wait->finish(false);
            return;
        }
        if (self->m_isLoaded) {
            self->m_hosted->serveMainThreadCallbacks();
        }
        // As in requestSettle: one turn of the main event loop, then once more.
        QTimer::singleShot(0, qApp, [weak, wait]() {
            std::shared_ptr<ClapPluginInstance> again = weak.lock();
            if (again && again->m_isLoaded) {
                again->m_hosted->serveMainThreadCallbacks();
            }
            wait->finish(again != nullptr);
        });
    }, m_mainThreadId);

    return wait->wait(timeoutMsec) && wait->ok;
}

void ClapPluginInstance::setWantedSampleRate(double sampleRate)
{
    m_wantedRate.store(sampleRate);
}

bool ClapPluginInstance::reactivateAndWait(double sampleRate, int timeoutMsec)
{
    m_wantedRate.store(sampleRate);
    if (!m_isLoaded) {
        return false;
    }

    std::shared_ptr<ClapMainThreadWait> wait = std::make_shared<ClapMainThreadWait>();
    std::weak_ptr<ClapPluginInstance> weak = weak_from_this();
    Async::call(this, [weak, wait]() {
        std::shared_ptr<ClapPluginInstance> self = weak.lock();
        const bool ok = self && self->doReactivate();
        // If nobody waits any more, the audio side is told another way.
        if (wait->finish(ok) && ok) {
            self->m_restarted.notify();
        }
    }, m_mainThreadId);

    return wait->wait(timeoutMsec) && wait->ok;
}

Notification ClapPluginInstance::restarted() const
{
    return m_restarted;
}

Notification ClapPluginInstance::flushWanted() const
{
    return m_flushWanted;
}

void ClapPluginInstance::reportAudioFault(int fault)
{
    int expected = 0;
    m_fault.compare_exchange_strong(expected, fault);
}
