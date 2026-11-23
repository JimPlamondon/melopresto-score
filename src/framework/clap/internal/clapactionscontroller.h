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

#include "actions/actionable.h"
#include "actions/iactionsdispatcher.h"
#include "global/iinteractive.h"
#include "modularity/ioc.h"
#include "ui/iinteractiveuriregister.h"

#include "../iclapinstancesregister.h"

namespace muse::clap {
//! Opens and closes the editor of a CLAP instrument when the mixer asks:
//! the counterpart of VstActionsController.
class ClapActionsController : public actions::Actionable, public muse::Contextable
{
    muse::ContextInject<actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<IInteractive> interactive = { this };
    muse::ContextInject<IClapInstancesRegister> instancesRegister = { this };
    muse::ContextInject<ui::IInteractiveUriRegister> interactiveUriRegister = { this };

public:
    ClapActionsController(const muse::modularity::ContextPtr& iocCtx)
        : muse::Contextable(iocCtx)
    {
    }

    //! GUI application only.
    void init();

    void instEditor(const actions::ActionQuery& actionQuery);

private:
    void editorOperation(const std::string& operation, const IClapPluginInstancePtr& instance, bool sync);
};
}
