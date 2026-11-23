/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include <gtest/gtest.h>

#include <dlfcn.h>

#include "internal/clapaudioclient.h"
#include "internal/clapbundle.h"
#include "internal/claphostedplugin.h"
#include "internal/clapstatewatch.h"

#include "clapfixture.h"

using namespace muse::clap;

//! CLAP is the CLever Audio Plug-in interface. A sound the musician changes
//! in a plug-in's editor must reach the score, with the editor open or
//! closed, playing or stopped. These tests change a parameter of the fixture
//! plug-in "from its editor" and follow the new state to where it is reported.
void muse::clap::fixture::guiEdit(int serial, uint32_t paramId, double value, GuiEdit how)
{
    using Function = void (*)(int, uint32_t, double, int);
    std::string problem;
    void* library = dlopen(clapExecutablePath(bundlePath(), problem).c_str(), RTLD_NOW | RTLD_LOCAL);
    ASSERT_TRUE(library) << problem;
    Function function = reinterpret_cast<Function>(dlsym(library, "melo_clap_fixture_gui_edit"));
    ASSERT_TRUE(function);
    function(serial, paramId, value, static_cast<int>(how));
}

namespace {
struct Edited {
    ClapBundlePtr bundle;
    ClapHostedPlugin plugin;
    ClapAudioClient client;
    ClapStateWatch watch;

    Edited()
        : watch(plugin)
    {
        std::string problem;
        bundle = ClapBundle::open(fixture::bundlePath(), problem);
        EXPECT_TRUE(bundle) << problem;
        EXPECT_TRUE(plugin.create(bundle, fixture::TONE_ID, problem)) << problem;
        EXPECT_TRUE(plugin.activate(48000.0));
        client.init(&plugin, 2);
        watch.setBaseline();
    }

    double param(uint32_t id) const
    {
        double value = -12345.0;
        EXPECT_TRUE(plugin.parameterValue(id, value));
        return value;
    }

    void edit(uint32_t id, double value, fixture::GuiEdit how)
    {
        fixture::guiEdit(static_cast<int>(param(fixture::Serial)), id, value, how);
    }

    //! Main-thread visits until a state is reported; 0 if none was in \p most visits.
    int visitsUntilReported(std::string& state, int most)
    {
        for (int visit = 1; visit <= most; ++visit) {
            if (watch.visit(state)) {
                return visit;
            }
        }
        return 0;
    }

    //! The sound a reported state gives back in a fresh plug-in.
    static double levelRestoredFrom(const std::string& state)
    {
        Edited fresh;
        EXPECT_TRUE(fresh.watch.restore(state));
        return fresh.param(fixture::Level);
    }
};

constexpr int LONG = 4 * ClapStateWatch::EDITOR_POLL_VISITS;
}

TEST(ClapStateWatchTests, AnEditorChangeWhileStoppedIsFlushedOnRequestAndItsStateReported)
{
    Edited edited;
    std::string state;
    EXPECT_EQ(edited.visitsUntilReported(state, LONG), 0) << "nothing changed, nothing is reported";

    // Playback is stopped: no block is rendered. The musician moves a control
    // in the editor; the plug-in asks for a parameter flush.
    edited.edit(fixture::Level, 0.5, fixture::FlushThenReport);
    EXPECT_EQ(edited.param(fixture::Level), 1.0) << "the plug-in applies it in its next flush or process call";
    EXPECT_TRUE(edited.plugin.takeFlushRequested()) << "request_flush is honoured";
    EXPECT_EQ(edited.plugin.signalsSeen(), static_cast<uint32_t>(ClapHostedPlugin::AskedForFlush));
    EXPECT_FALSE(edited.plugin.takeFlushRequested());

    // The audio side flushes with no audio; the plug-in reports the value.
    EXPECT_TRUE(edited.client.flushRequested());
    EXPECT_EQ(edited.param(fixture::Level), 0.5);
    EXPECT_EQ(edited.param(fixture::FlushCalls), 1.0);
    EXPECT_EQ(edited.param(fixture::ProcessCalls), 0.0);
    EXPECT_EQ(edited.param(fixture::ZeroFrameProcessCalls), 0.0);
    EXPECT_EQ(edited.plugin.signalsSeen(),
              static_cast<uint32_t>(ClapHostedPlugin::AskedForFlush | ClapHostedPlugin::ReportedOnOutputEvents));

    // Once the value has come to rest, the new state is reported, once.
    const int visits = edited.visitsUntilReported(state, LONG);
    EXPECT_GE(visits, ClapStateWatch::SETTLE_VISITS);
    EXPECT_LE(visits, ClapStateWatch::SETTLE_VISITS + 1);
    EXPECT_EQ(Edited::levelRestoredFrom(state), 0.5) << "the score gets the sound back";
    EXPECT_EQ(edited.visitsUntilReported(state, LONG), 0);
}

TEST(ClapStateWatchTests, AnEditorChangeWhilePlayingIsReportedFromTheProcessCall)
{
    Edited edited;
    std::vector<float> buffer(2 * 128);
    edited.edit(fixture::Level, 1.5, fixture::FlushThenReport);
    EXPECT_EQ(edited.client.process(buffer.data(), 128), 128u);
    EXPECT_EQ(edited.param(fixture::Level), 1.5);
    EXPECT_EQ(edited.param(fixture::FlushCalls), 0.0) << "the next process call is enough";

    std::string state;
    EXPECT_GT(edited.visitsUntilReported(state, LONG), 0);
    EXPECT_EQ(Edited::levelRestoredFrom(state), 1.5);
}

TEST(ClapStateWatchTests, MarkDirtyAndAParameterRescanAlsoMeanTheStateMayHaveChanged)
{
    for (const fixture::GuiEdit how : { fixture::MarkDirty, fixture::RescanValues }) {
        Edited edited;
        edited.edit(fixture::Level, 0.75, how);
        std::string state;
        const int visits = edited.visitsUntilReported(state, LONG);
        EXPECT_GE(visits, ClapStateWatch::SETTLE_VISITS) << how;
        EXPECT_LE(visits, ClapStateWatch::SETTLE_VISITS + 1) << how;
        EXPECT_EQ(Edited::levelRestoredFrom(state), 0.75) << how;
    }
}

TEST(ClapStateWatchTests, AChangeThePlugInNeverMentionsIsFoundWhileItsEditorIsOpen)
{
    Edited edited;
    int parentStandIn = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    ASSERT_TRUE(edited.plugin.openEditor(ClapEditorKind::Embedded, &parentStandIn, "t", ClapEditorCallbacks(), width, height));

    std::string state;
    EXPECT_EQ(edited.visitsUntilReported(state, LONG), 0) << "an open editor with nothing changed marks nothing";

    // The plug-in changes its state and tells the host nothing at all.
    edited.edit(fixture::Level, 0.25, fixture::Silent);
    const int visits = edited.visitsUntilReported(state, LONG);
    EXPECT_GT(visits, 0) << "the state is read now and then while the editor is open, and reported when it differs";
    EXPECT_LE(visits, ClapStateWatch::EDITOR_POLL_VISITS);
    EXPECT_EQ(Edited::levelRestoredFrom(state), 0.25);
    EXPECT_EQ(edited.visitsUntilReported(state, LONG), 0) << "the same state is not reported twice";
}

TEST(ClapStateWatchTests, ClosingTheEditorReportsAChangedStateAndNothingWhenNothingChanged)
{
    Edited edited;
    int parentStandIn = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string state;

    // Opened and closed with nothing changed: the score is not marked as modified.
    ASSERT_TRUE(edited.plugin.openEditor(ClapEditorKind::Embedded, &parentStandIn, "t", ClapEditorCallbacks(), width, height));
    edited.plugin.closeEditor();
    EXPECT_FALSE(edited.watch.editorClosed(state));

    // Changed, then closed at once: the state is read at the closing.
    ASSERT_TRUE(edited.plugin.openEditor(ClapEditorKind::Embedded, &parentStandIn, "t", ClapEditorCallbacks(), width, height));
    edited.edit(fixture::Level, 1.25, fixture::Silent);
    edited.plugin.closeEditor();
    ASSERT_TRUE(edited.watch.editorClosed(state));
    EXPECT_EQ(Edited::levelRestoredFrom(state), 1.25);
    EXPECT_FALSE(edited.watch.editorClosed(state));
}

TEST(ClapStateWatchTests, ARefusedStateIsNotReplacedUntilTheMusicianChangesTheSound)
{
    Edited edited;
    EXPECT_FALSE(edited.watch.restore("bm90IHRoZSBwbHVnLWluJ3Mgb3duIGJ5dGVz")) << "base64 of bytes that are not the plug-in's";
    EXPECT_TRUE(edited.watch.isRejected());

    // The plug-in reports changes of its own accord: the saved state stays.
    std::string state;
    edited.edit(fixture::Level, 0.5, fixture::MarkDirty);
    EXPECT_EQ(edited.visitsUntilReported(state, LONG), 0);

    // The musician had the editor open and closed it: what the plug-in holds is their choice.
    EXPECT_TRUE(edited.watch.editorClosed(state));
    EXPECT_FALSE(edited.watch.isRejected());
    EXPECT_EQ(Edited::levelRestoredFrom(state), 0.5);

    // A state the plug-in takes is not reported back, changed or not.
    Edited restored;
    ASSERT_TRUE(restored.watch.restore(state));
    EXPECT_FALSE(restored.watch.isRejected());
    EXPECT_EQ(restored.visitsUntilReported(state, LONG), 0);
    EXPECT_TRUE(restored.watch.restore("")) << "no saved state is not a refusal";
}
