/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2024 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "testing/environment.h"

#include "draw/drawmodule.h"
#include "engraving/engravingmodule.h"
#include "engraving/tests/utils/scorerw.h"

#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/mscore.h"
#include "modularity/ioc.h"
#include "notation/tests/mocks/notationconfigurationmock.h"
#include "shortcuts/ishortcutsregister.h"
#include "stubs/shortcuts/shortcutsregisterstub.h"
#include "ui/internal/navigationcontroller.h"
#include "ui/internal/uiconfiguration.h"

namespace {
class NotationConfigurationTestModule : public muse::modularity::IModuleSetup
{
public:
    std::string moduleName() const override { return "lattice_drag_test_configuration"; }
    void registerExports() override
    {
        ioc()->registerExport<mu::notation::INotationConfiguration>(moduleName(),
                                                                    new testing::NiceMock<mu::notation::NotationConfigurationMock>());
    }
};
class ShortcutRegisterTestModule : public muse::modularity::IModuleSetup
{
public:
    std::string moduleName() const override { return "jims_tuning_test_shortcuts"; }
    void registerExports() override
    {
        ioc()->registerExport<muse::shortcuts::IShortcutsRegister>(moduleName(), new muse::shortcuts::ShortcutsRegisterStub());
    }
};

class NavigationTestModule : public muse::modularity::IModuleSetup
{
public:
    std::string moduleName() const override { return "jims_tuning_test_navigation"; }
    void registerExports() override
    {
        ioc()->registerExport<muse::ui::INavigationController>(moduleName(), new muse::ui::NavigationController(nullptr));
    }
};

class DialogConfigurationStub : public muse::ui::UiConfiguration
{
public:
    DialogConfigurationStub()
        : UiConfiguration(nullptr) {}
    const muse::ui::ThemeInfo& currentTheme() const override { return m_theme; }
    muse::async::Notification currentThemeChanged() const override { return {}; }
    muse::io::path_t appIconPath() const override { return {}; }
private:
    muse::ui::ThemeInfo m_theme;
};

class DialogConfigurationTestModule : public muse::modularity::IModuleSetup
{
public:
    std::string moduleName() const override { return "melo_editor_dialog_configuration"; }
    void registerExports() override
    {
        globalIoc()->registerExport<muse::ui::IUiConfiguration>(moduleName(), new DialogConfigurationStub());
    }
};
}

static muse::testing::SuiteEnvironment notation_se
    = muse::testing::SuiteEnvironment()
      .setDependencyModules({ new muse::draw::DrawModule(), new mu::engraving::EngravingModule(), new ShortcutRegisterTestModule(),
                              new NavigationTestModule(), new NotationConfigurationTestModule(), new DialogConfigurationTestModule() })
      .setPostInit([]() {
    LOGI() << "notationscene_qml tests suite post init";

    mu::engraving::ScoreRW::setRootPath(muse::String::fromUtf8(notationscene_qml_tests_DATA_ROOT));

    mu::engraving::MScore::testMode = true;
    mu::engraving::MScore::testWriteStyleToScore = false;
    mu::engraving::MScore::noGui = true;

    mu::engraving::loadInstrumentTemplates(":/engraving/instruments/instruments.xml");
}).setDeInit([]() {
    muse::modularity::globalIoc()->unregister<muse::ui::IUiConfiguration>("melo_editor_dialog_configuration");
});
