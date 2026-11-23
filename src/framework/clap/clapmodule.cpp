/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "clapmodule.h"

#include "modularity/ioc.h"

#include "audioplugins/iaudiopluginsscannerregister.h"
#include "audioplugins/iaudiopluginmetareaderregister.h"

#include "internal/clapinstancesregister.h"
#include "internal/clapmodulesrepository.h"
#include "internal/clappluginmetareader.h"
#include "internal/clappluginsscanner.h"

using namespace muse::clap;
using namespace muse::modularity;
using namespace muse::audioplugins;

std::string ClapModule::moduleName() const
{
    return "clap";
}

void ClapModule::registerExports()
{
    m_pluginModulesRepo = std::make_shared<ClapModulesRepository>(iocContext());
    m_pluginInstancesRegister = std::make_shared<ClapInstancesRegister>(iocContext());

    ioc()->registerExport<IClapModulesRepository>(moduleName(), m_pluginModulesRepo);
    ioc()->registerExport<IClapInstancesRegister>(moduleName(), m_pluginInstancesRegister);
}

void ClapModule::resolveImports()
{
    auto scannerRegister = ioc()->resolve<IAudioPluginsScannerRegister>(moduleName());
    if (scannerRegister) {
        scannerRegister->registerScanner(std::make_shared<ClapPluginsScanner>());
    }

    auto metaReaderRegister = ioc()->resolve<IAudioPluginMetaReaderRegister>(moduleName());
    if (metaReaderRegister) {
        metaReaderRegister->registerReader(std::make_shared<ClapPluginMetaReader>());
    }
}

void ClapModule::onInit(const IApplication::RunMode& mode)
{
    // Plug-ins are loaded and played in the GUI application and in the
    // console application (audio export). The plug-in registration run only
    // reads descriptors and needs no service of loaded plug-ins.
    if (mode == IApplication::RunMode::GuiApp || mode == IApplication::RunMode::ConsoleApp) {
        m_pluginInstancesRegister->init();
    }
}

void ClapModule::onDeinit()
{
    m_pluginInstancesRegister->deinit();
    m_pluginModulesRepo->deInit();
}
