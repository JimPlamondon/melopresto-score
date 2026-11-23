/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "clapinstancesregister.h"

#include <vector>

#include <QTimer>

#include "clapplugininstance.h"

#include "log.h"

using namespace muse::clap;
using namespace muse::audio;

namespace muse::clap {
//! How often the main thread visits the loaded plug-ins, in milliseconds.
static constexpr int CLAP_SERVICE_INTERVAL_MSEC = 10;
}

ClapInstancesRegister::~ClapInstancesRegister()
{
    deinit();
}

void ClapInstancesRegister::init()
{
    if (m_serviceTimer) {
        return;
    }
    m_serviceTimer = new QTimer();
    m_serviceTimer->setInterval(CLAP_SERVICE_INTERVAL_MSEC);
    QObject::connect(m_serviceTimer, &QTimer::timeout, m_serviceTimer, [this]() {
        serviceInstances();
    });
    m_serviceTimer->start();
}

void ClapInstancesRegister::deinit()
{
    delete m_serviceTimer;
    m_serviceTimer = nullptr;
}

void ClapInstancesRegister::serviceInstances()
{
    // The instances are visited outside the lock: serving one may take time,
    // and the audio engine thread registers and unregisters under it.
    std::vector<std::shared_ptr<ClapPluginInstance> > instances;
    {
        std::lock_guard lock(m_mutex);
        instances.reserve(m_instances.size());
        for (const auto& pair : m_instances) {
            instances.push_back(pair.second);
        }
    }
    for (const std::shared_ptr<ClapPluginInstance>& instance : instances) {
        instance->serviceOnMainThread();
    }
}

IClapPluginInstancePtr ClapInstancesRegister::makeAndRegisterInstrPlugin(const AudioResourceId& resourceId, const TrackId trackId,
                                                                         double sampleRate)
{
    std::shared_ptr<ClapPluginInstance> instance = std::make_shared<ClapPluginInstance>(resourceId, iocContext());
    {
        std::lock_guard lock(m_mutex);
        m_instances.insert_or_assign(Key { resourceId, trackId }, instance);
    }
    instance->load(sampleRate);
    return instance;
}

IClapPluginInstancePtr ClapInstancesRegister::instanceById(const ClapPluginInstanceId id) const
{
    std::lock_guard lock(m_mutex);
    for (const auto& pair : m_instances) {
        if (pair.second->id() == id) {
            return pair.second;
        }
    }
    return nullptr;
}

IClapPluginInstancePtr ClapInstancesRegister::instrumentPlugin(const AudioResourceId& resourceId, const TrackId trackId) const
{
    std::lock_guard lock(m_mutex);
    auto it = m_instances.find(Key { resourceId, trackId });
    if (it != m_instances.end()) {
        return it->second;
    }
    return nullptr;
}

void ClapInstancesRegister::unregisterById(const ClapPluginInstanceId id)
{
    std::lock_guard lock(m_mutex);
    for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
        if (it->second->id() == id) {
            m_instances.erase(it);
            return;
        }
    }
}

void ClapInstancesRegister::unregisterAllInstrPlugin()
{
    std::lock_guard lock(m_mutex);
    m_instances.clear();
}
