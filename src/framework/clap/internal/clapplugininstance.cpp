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

//! Main-thread visits between a change of the plug-in's state and saving it,
//! so that a value being dragged is saved once, when it has come to rest.
static constexpr int CLAP_STATE_SETTLE_VISITS = 25;
}

ClapPluginInstance::ClapPluginInstance(const audio::AudioResourceId& resourceId, const modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx), m_resourceId(resourceId), m_name(resourceId), m_hosted(std::make_shared<ClapHostedPlugin>())
{
    m_id = ++s_lastClapInstanceId;
    m_mainThreadId = threadSecurer()->mainThreadId();
}

ClapPluginInstance::~ClapPluginInstance()
{
    m_isLoaded = false;

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
    std::weak_ptr<ClapPluginInstance> weak = weak_from_this();
    Async::call(this, [weak, sampleRate]() {
        if (std::shared_ptr<ClapPluginInstance> self = weak.lock()) {
            self->doLoad(sampleRate);
        }
    }, m_mainThreadId);
}

void ClapPluginInstance::doLoad(double sampleRate)
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
        m_loadingCompleted.notify();
        return;
    }

    std::string problem;
    if (!m_hosted->create(bundle, m_resourceId, problem)) {
        LOGE() << "CLAP plug-in " << m_resourceId << " could not be created: " << problem;
        m_loadingCompleted.notify();
        return;
    }
    m_name = m_hosted->descriptor().name.empty() ? m_resourceId : m_hosted->descriptor().name;

    // The saved state goes in before the plug-in is activated, so the first
    // note already sounds with it.
    if (config.has_value() && !applyConfig(*config)) {
        m_stateRejected = true;
        LOGE() << "CLAP plug-in " << m_resourceId << " refused its saved state. "
               << "That state is kept in the score; it is not replaced by the plug-in's default.";
    }

    if (!m_hosted->activate(sampleRate)) {
        LOGE() << "CLAP plug-in " << m_resourceId << " refused to activate at " << sampleRate << " Hz";
        m_hosted->destroy();
        m_loadingCompleted.notify();
        return;
    }

    m_isLoaded = true;
    m_loadingCompleted.notify();
}

bool ClapPluginInstance::applyConfig(const audio::AudioUnitConfig& config)
{
    ONLY_MAIN_THREAD(threadSecurer);

    auto state = config.find(std::string(CLAP_STATE_KEY));
    if (state == config.end() || state->second.empty()) {
        return true;
    }
    if (!m_hosted->hasState()) {
        return false;
    }
    // The plug-in's opaque bytes are kept in the configuration as base64 text.
    const QByteArray bytes = QByteArray::fromBase64(QByteArray::fromStdString(state->second),
                                                    QByteArray::AbortOnBase64DecodingErrors);
    if (bytes.isEmpty()) {
        return false;
    }
    const std::vector<uint8_t> raw(bytes.begin(), bytes.end());
    return m_hosted->loadState(raw);
}

void ClapPluginInstance::sendState()
{
    ONLY_MAIN_THREAD(threadSecurer);

    if (!m_isLoaded || m_stateRejected || !m_hosted->hasState()) {
        return;
    }
    std::vector<uint8_t> raw;
    if (!m_hosted->saveState(raw)) {
        LOGW() << "CLAP plug-in " << m_resourceId << " did not save its state";
        return;
    }
    const QByteArray bytes(reinterpret_cast<const char*>(raw.data()), static_cast<qsizetype>(raw.size()));

    audio::AudioUnitConfig config;
    config.emplace(std::string(CLAP_STATE_KEY), bytes.toBase64().toStdString());
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

    // The plug-in asked to be restarted, or the audio side asked for another
    // sample rate. Either waits until the audio thread has stopped processing.
    if (m_hosted->takeRestartRequested()) {
        m_restartWanted = true;
    }
    const double wantedRate = m_reactivationRate.load();
    if ((m_restartWanted || wantedRate > 0.0) && m_hosted->requestAudioStop()) {
        const double rate = wantedRate > 0.0 ? wantedRate : m_hosted->sampleRate();
        m_hosted->deactivate();
        if (!m_hosted->activate(rate)) {
            LOGE() << "CLAP plug-in " << m_resourceId << " refused to activate at " << rate << " Hz";
        }
        m_restartWanted = false;
        // A newer request that arrived meanwhile stays for the next visit.
        double served = wantedRate;
        m_reactivationRate.compare_exchange_strong(served, 0.0);
    }

    if (m_hosted->takeStateChanged()) {
        m_stateCountdown = CLAP_STATE_SETTLE_VISITS;
    } else if (m_stateCountdown > 0 && --m_stateCountdown == 0) {
        sendState();
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
            self->m_stateRejected = false;
            self->m_stateRestored.notify();
        } else {
            self->m_stateRejected = true;
            LOGE() << "CLAP plug-in " << self->m_resourceId << " refused a saved state; that state is kept";
        }
    }, m_mainThreadId);
}

void ClapPluginInstance::refreshConfig()
{
    ONLY_MAIN_THREAD(threadSecurer);

    // The musician has had the editor open: what the plug-in holds now is their choice.
    m_stateRejected = false;
    m_stateCountdown = 0;
    m_hosted->takeStateChanged();
    sendState();
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

void ClapPluginInstance::requestReactivation(double sampleRate)
{
    m_reactivationRate.store(sampleRate);
}

void ClapPluginInstance::reportAudioFault(int fault)
{
    int expected = 0;
    m_fault.compare_exchange_strong(expected, fault);
}
