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

#include <map>
#include <mutex>

#include "../iclapinstancesregister.h"

#include "modularity/ioc.h"

class QTimer;

namespace muse::clap {
class ClapPluginInstance;

class ClapInstancesRegister : public IClapInstancesRegister, public muse::Contextable
{
public:
    ClapInstancesRegister(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}
    ~ClapInstancesRegister() override;

    //! Main thread: start serving the instances' main-thread work. This runs
    //! in the console application as well as the GUI one, because audio
    //! export loads and plays plug-ins there.
    void init();
    void deinit();

    IClapPluginInstancePtr makeAndRegisterInstrPlugin(const muse::audio::AudioResourceId& resourceId, const muse::audio::TrackId trackId,
                                                      double sampleRate) override;

    IClapPluginInstancePtr instanceById(const ClapPluginInstanceId id) const override;
    IClapPluginInstancePtr instrumentPlugin(const muse::audio::AudioResourceId& resourceId,
                                            const muse::audio::TrackId trackId) const override;

    void unregisterById(const ClapPluginInstanceId id) override;
    void unregisterAllInstrPlugin() override;

private:
    void serviceInstances();

    using Key = std::pair<muse::audio::AudioResourceId, muse::audio::TrackId>;

    mutable std::mutex m_mutex;
    std::map<Key, std::shared_ptr<ClapPluginInstance> > m_instances;
    QTimer* m_serviceTimer = nullptr;
};
}
