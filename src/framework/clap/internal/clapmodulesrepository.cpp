/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "clapmodulesrepository.h"

#include "log.h"

using namespace muse;
using namespace muse::clap;

namespace muse::clap {
static audioplugins::AudioPluginInfoList clapKnownInstruments(const audioplugins::IKnownAudioPluginsRegister& knownPlugins,
                                                              const audio::AudioResourceId* onlyId)
{
    return knownPlugins.pluginInfoList([onlyId](const audioplugins::AudioPluginInfo& info) {
        return info.meta.type == audio::AudioResourceType::ClapPlugin
               && info.type == audioplugins::AudioPluginType::Instrument
               && info.enabled
               && (!onlyId || info.meta.id == *onlyId);
    });
}
}

void ClapModulesRepository::deInit()
{
    std::lock_guard lock(m_mutex);
    m_bundles.clear();
}

bool ClapModulesRepository::exists(const audio::AudioResourceId& resourceId) const
{
    std::lock_guard lock(m_mutex);
    return !clapKnownInstruments(*knownPlugins(), &resourceId).empty();
}

std::shared_ptr<ClapBundle> ClapModulesRepository::bundle(const audio::AudioResourceId& resourceId)
{
    std::lock_guard lock(m_mutex);

    const audioplugins::AudioPluginInfoList known = clapKnownInstruments(*knownPlugins(), &resourceId);
    if (known.empty()) {
        return nullptr;
    }
    const std::string path = known.front().path.toStdString();

    auto found = m_bundles.find(path);
    if (found != m_bundles.end()) {
        return found->second;
    }

    std::string problem;
    ClapBundlePtr bundle = ClapBundle::open(path, problem);
    if (!bundle) {
        LOGE() << "CLAP " << path << ": " << problem;
        return nullptr;
    }
    m_bundles.emplace(path, bundle);
    return bundle;
}

audio::AudioResourceMetaList ClapModulesRepository::instrumentModulesMeta() const
{
    std::lock_guard lock(m_mutex);

    audio::AudioResourceMetaList result;
    for (const audioplugins::AudioPluginInfo& info : clapKnownInstruments(*knownPlugins(), nullptr)) {
        result.push_back(info.meta);
    }
    return result;
}
