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

#include <memory>

#include "modularity/imoduleinterface.h"

#include "audio/common/audiotypes.h"

#include "claptypes.h"

namespace muse::clap {
class ClapBundle;

class IClapModulesRepository : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IClapModulesRepository)

public:
    virtual ~IClapModulesRepository() = default;

    //! The known-plug-ins register has an enabled CLAP plug-in with this descriptor id.
    virtual bool exists(const muse::audio::AudioResourceId& resourceId) const = 0;

    //! Main thread: the loaded bundle that holds this descriptor id; loads it
    //! if needed. One bundle is loaded once however many plug-ins use it.
    virtual std::shared_ptr<ClapBundle> bundle(const muse::audio::AudioResourceId& resourceId) = 0;

    virtual muse::audio::AudioResourceMetaList instrumentModulesMeta() const = 0;
};
}
