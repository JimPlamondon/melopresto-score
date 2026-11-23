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

using namespace muse::clap;

std::string ClapModule::moduleName() const
{
    return "clap";
}

void ClapModule::registerExports()
{
}

void ClapModule::resolveImports()
{
}

void ClapModule::onInit(const IApplication::RunMode&)
{
}

void ClapModule::onDeinit()
{
}
