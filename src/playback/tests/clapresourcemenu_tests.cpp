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

#include "playback/qml/MuseScore/Playback/clapresourcemenu.h"

using namespace mu::playback;
using namespace muse;
using namespace muse::audio;

//! CLAP is the CLever Audio Plug-in interface. In the mixer's sound menu the
//! CLAP instruments have a submenu of their own, "CLAP", beside "VST3".
namespace {
AudioResourceMeta clapMeta(const std::string& id, const std::string& vendor, const char16_t* name)
{
    AudioResourceMeta meta;
    meta.id = id;
    meta.type = AudioResourceType::ClapPlugin;
    meta.vendor = vendor;
    if (name) {
        meta.attributes.insert({ String(u"clapName"), String(name) });
    }
    return meta;
}
}

TEST(Playback_ClapResourceMenuTests, PlugInsAreGroupedByVendorAndShownByName)
{
    std::map<AudioResourceVendor, AudioResourceMetaList> byVendor;
    byVendor["Surge Synth Team"] = { clapMeta("org.surge-synth-team.surge-xt", "Surge Synth Team", u"Surge XT"),
                                     clapMeta("org.baconpaul.six-sines", "Surge Synth Team", u"Six Sines") };
    byVendor["Digital Suburban"] = { clapMeta("com.digital-suburban.dexed", "Digital Suburban", u"Dexed") };
    byVendor["Nameless"] = { clapMeta("org.example.nameless", "Nameless", nullptr) };

    AudioResourceMeta current = clapMeta("org.baconpaul.six-sines", "Surge Synth Team", u"Six Sines");
    const std::vector<ClapMenuVendor> menu = clapMenuModel(byVendor, current);

    ASSERT_EQ(menu.size(), 3u);
    EXPECT_EQ(menu[0].vendor, QString("Digital Suburban"));
    EXPECT_FALSE(menu[0].current);
    ASSERT_EQ(menu[0].entries.size(), 1u);
    EXPECT_EQ(menu[0].entries[0].title, QString("Dexed"));
    EXPECT_EQ(menu[0].entries[0].itemId, QString("CLAP\\com.digital-suburban.dexed")) << "the item is the descriptor id";

    EXPECT_EQ(menu[1].vendor, QString("Nameless"));
    EXPECT_EQ(menu[1].entries[0].title, QString("org.example.nameless")) << "with no name, the id is shown";

    EXPECT_EQ(menu[2].vendor, QString("Surge Synth Team"));
    EXPECT_TRUE(menu[2].current);
    ASSERT_EQ(menu[2].entries.size(), 2u);
    EXPECT_EQ(menu[2].entries[0].title, QString("Six Sines")) << "sorted by name";
    EXPECT_TRUE(menu[2].entries[0].current);
    EXPECT_EQ(menu[2].entries[1].title, QString("Surge XT"));
    EXPECT_FALSE(menu[2].entries[1].current);
}

TEST(Playback_ClapResourceMenuTests, AVst3BuildOfTheSameSynthesizerIsNotTheCurrentClapItem)
{
    std::map<AudioResourceVendor, AudioResourceMetaList> byVendor;
    byVendor["Surge Synth Team"] = { clapMeta("Surge XT", "Surge Synth Team", u"Surge XT") };

    // The staff uses the VST3 build, whose id happens to equal this CLAP item's id.
    AudioResourceMeta current;
    current.id = "Surge XT";
    current.type = AudioResourceType::VstPlugin;
    current.vendor = "Surge Synth Team";

    const std::vector<ClapMenuVendor> menu = clapMenuModel(byVendor, current);
    ASSERT_EQ(menu.size(), 1u);
    EXPECT_FALSE(menu[0].current);
    EXPECT_FALSE(menu[0].entries[0].current);
}

TEST(Playback_ClapResourceMenuTests, OnlyAClapSourceOpensTheClapEditor)
{
    // The action's name is written once (clap/clapactions.h); the clap module
    // registers that same constant and the mixer sends it.
    EXPECT_EQ(clapInstrumentEditorAction(AudioSourceType::Clap), std::string(muse::clap::CLAP_INSTRUMENT_EDITOR_ACTION));
    EXPECT_FALSE(std::string(muse::clap::CLAP_INSTRUMENT_EDITOR_ACTION).empty());
    EXPECT_EQ(clapInstrumentEditorAction(AudioSourceType::Vsti), "");
    EXPECT_EQ(clapInstrumentEditorAction(AudioSourceType::Fluid), "");
    EXPECT_EQ(clapInstrumentEditorAction(AudioSourceType::MuseSampler), "");
    EXPECT_EQ(clapInstrumentEditorAction(AudioSourceType::Undefined), "");
}
