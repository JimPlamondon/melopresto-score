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

#include "../iclapmodulesrepository.h"

#include "modularity/ioc.h"
#include "audioplugins/iknownaudiopluginsregister.h"

#include "clapbundle.h"

namespace muse::clap {
//! Knows the CLAP instruments the plug-in scan registered and keeps their
//! bundles loaded: the counterpart of VstModulesRepository.
class ClapModulesRepository : public IClapModulesRepository, public muse::Contextable
{
    muse::ContextInject<audioplugins::IKnownAudioPluginsRegister> knownPlugins { this };

public:
    ClapModulesRepository(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    void deInit();

    bool exists(const muse::audio::AudioResourceId& resourceId) const override;
    std::shared_ptr<ClapBundle> bundle(const muse::audio::AudioResourceId& resourceId) override;
    muse::audio::AudioResourceMetaList instrumentModulesMeta() const override;

private:
    mutable std::mutex m_mutex;
    //! By bundle path. A bundle stays loaded until the module is closed:
    //! taking a plug-in's code out of a running application is not safe for
    //! every plug-in.
    std::map<std::string, ClapBundlePtr> m_bundles;
};
}
