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

#include "modularity/imodulesetup.h"

namespace muse::clap {
class ClapModulesRepository;
class ClapInstancesRegister;
class ClapActionsController;

//! Hosts CLAP (CLever Audio Plug-in) instruments, beside the vst module.
class ClapModule : public modularity::IModuleSetup
{
public:
    std::string moduleName() const override;

    void registerExports() override;
    void resolveImports() override;
    void onInit(const IApplication::RunMode& mode) override;
    void onDeinit() override;

private:
    std::shared_ptr<ClapModulesRepository> m_pluginModulesRepo;
    std::shared_ptr<ClapInstancesRegister> m_pluginInstancesRegister;
    std::shared_ptr<ClapActionsController> m_actionsController;
};
}
