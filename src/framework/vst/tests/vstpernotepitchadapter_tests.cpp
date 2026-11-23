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

#include <filesystem>
#include <fstream>
#include <string>

#include "internal/vstpernotepitchadapter.h"

using namespace muse::vst;

namespace {
VstEvent note(VstEvent::EventTypes type, int32_t noteId, int32_t pitch, float tuning)
{
    VstEvent event {};
    event.type = type;
    if (type == VstEvent::kNoteOnEvent) {
        event.noteOn.noteId = noteId;
        event.noteOn.pitch = pitch;
        event.noteOn.tuning = tuning;
        event.noteOn.velocity = 0.8f;
        event.noteOn.channel = 0;
    } else {
        event.noteOff.noteId = noteId;
        event.noteOff.pitch = pitch;
        event.noteOff.tuning = tuning;
        event.noteOff.channel = 0;
    }
    return event;
}
}

namespace {
//! A receiver profile for a plug-in named "Test MPE Synth" whose measured
//! behaviour is Dexed's: MPE on as loaded, 2,400 cents, the range not
//! settable, a bend that does not follow its note-on is lost.
std::string profilesFolder(
    const std::string& extraFacts =
    R"(, {"domain": "microtuning", "key": "mpe", "value": true},
        {"domain": "microtuning", "key": "mpe_enable", "value": "already_on"},
        {"domain": "microtuning", "key": "mpe_pitch_bend_range_semitones", "value": 24},
        {"domain": "microtuning", "key": "mpe_pitch_bend_range_settable", "value": false},
        {"domain": "ordering", "key": "mpe_pitch_bend_follows_note_on", "value": true})")
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path()
                                         / ("melo_profiles_" + std::to_string(std::hash<std::string> {}(extraFacts)));
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "test-mpe-synth.json")
        <<
        R"({"schema_version": "melo-receiver-profile/v2", "profile_id": "test-mpe-synth", "display_name": "Test MPE Synth",
        "profile_kind": "built_in", "category": "legacy", "voice_allocation_policy": "omit_new_note",
        "basic_midi_policy": "polyphonic_quantized",
        "endpoint_match": {"stable_id_prefix": "Test MPE Synth", "firmware_prefix": "1.", "route_prefix": "vst3"},
        "facts": [{"domain": "protocols", "key": "vst3_note_events", "value": true})"
        << extraFacts
        << R"(], "source_evidence": ["test"]})";
    return folder.string();
}

VstPerNotePitchConfig mpeSynth(const std::string& folder)
{
    VstPerNotePitchConfig config;
    config.pluginName = "Test MPE Synth";
    config.pluginVersion = "1.0";
    config.profilesFolder = folder;
    for (int16_t channel = 0; channel < 16; ++channel) {
        config.midiParams[channel][Steinberg::Vst::kPitchBend] = 1000 + channel;
        config.midiParams[channel][Steinberg::Vst::kCtrlRPNSelectMSB] = 2000 + channel;
        config.midiParams[channel][Steinberg::Vst::kCtrlRPNSelectLSB] = 3000 + channel;
        config.midiParams[channel][Steinberg::Vst::kCtrlDataEntryMSB] = 4000 + channel;
        config.midiParams[channel][Steinberg::Vst::kCtrlDataEntryLSB] = 5000 + channel;
    }
    return config;
}
}

TEST(VstPerNotePitchAdapterTests, ListedTuningExpressionCarriesTheWholeOffsetWithAZeroField)
{
    VstPerNotePitchConfig config;
    config.pluginName = "Unprofiled Synth";
    config.pluginVersion = "3.0";
    config.advertisesTuningExpression = true;
    VstPerNotePitchAdapter adapter;
    ASSERT_TRUE(adapter.configure(config).empty());
    EXPECT_EQ(adapter.mode(), VstPerNotePitchAdapter::Mode::TuningExpression);

    const VstPerNotePitchOutput on = adapter.adapt(note(VstEvent::kNoteOnEvent, 42, 60, 28.f));
    ASSERT_EQ(on.eventCount, 2u);
    EXPECT_FLOAT_EQ(on.events[0].noteOn.tuning, 0.f);
    EXPECT_EQ(on.events[0].noteOn.noteId, 42);
    EXPECT_EQ(on.events[1].type, VstEvent::kNoteExpressionValueEvent);
    EXPECT_EQ(on.events[1].noteExpressionValue.noteId, 42);
    EXPECT_EQ(on.events[1].noteExpressionValue.typeId, Steinberg::Vst::kTuningTypeID);
    EXPECT_NEAR(on.events[1].noteExpressionValue.value, 0.5 + 28.0 / 24000.0, 1e-9);

    // A note with no offset still states its expression.
    const VstPerNotePitchOutput plain = adapter.adapt(note(VstEvent::kNoteOnEvent, 43, 62, 0.f));
    ASSERT_EQ(plain.eventCount, 2u);
    EXPECT_DOUBLE_EQ(plain.events[1].noteExpressionValue.value, 0.5);

    const VstPerNotePitchOutput off = adapter.adapt(note(VstEvent::kNoteOffEvent, 42, 60, 28.f));
    ASSERT_EQ(off.eventCount, 1u);
    EXPECT_EQ(off.events[0].noteOff.noteId, 42);
    EXPECT_FLOAT_EQ(off.events[0].noteOff.tuning, 0.f);
}

TEST(VstPerNotePitchAdapterTests, MpeProfilePlacesTheBendAfterTheNoteOnOnTheChannelReleasedLongestAgo)
{
    VstPerNotePitchAdapter adapter;
    ASSERT_TRUE(adapter.configure(mpeSynth(profilesFolder())).empty());
    EXPECT_EQ(adapter.mode(), VstPerNotePitchAdapter::Mode::Mpe);
    EXPECT_EQ(adapter.profileId(), "test-mpe-synth");
    EXPECT_EQ(adapter.setup().count, 0u) << "MPE is on as loaded and the range is not settable";

    const VstPerNotePitchOutput first = adapter.adapt(note(VstEvent::kNoteOnEvent, 10, 60, 28.f));
    const VstPerNotePitchOutput second = adapter.adapt(note(VstEvent::kNoteOnEvent, 11, 60, -28.f));
    ASSERT_EQ(first.eventCount, 1u);
    ASSERT_EQ(first.paramCount, 1u);
    EXPECT_EQ(first.events[0].noteOn.channel, 1);
    EXPECT_FLOAT_EQ(first.events[0].noteOn.tuning, 0.f);
    EXPECT_EQ(second.events[0].noteOn.channel, 2) << "two notes on one key take two channels";
    EXPECT_EQ(first.params[0].change.paramId, 1001u);
    EXPECT_EQ(second.params[0].change.paramId, 1002u);
    EXPECT_TRUE(first.params[0].afterNoteOn) << "the bend follows the note-on";
    // 28 of 2,400 cents of the upper 8,191 steps, as a 14-bit value over 16,383.
    EXPECT_NEAR(first.params[0].change.value, (8192.0 + 96.0) / 16383.0, 1e-9);
    EXPECT_NEAR(second.params[0].change.value, (8192.0 - 96.0) / 16383.0, 1e-9);

    const VstPerNotePitchOutput firstOff = adapter.adapt(note(VstEvent::kNoteOffEvent, 10, 60, 28.f));
    ASSERT_EQ(firstOff.eventCount, 1u);
    EXPECT_EQ(firstOff.events[0].noteOff.channel, 1);
    EXPECT_EQ(firstOff.paramCount, 0u) << "no bend change at note-off: the dying sound keeps its bend";

    // Channel 1 was just released; a never-used channel is taken first.
    const VstPerNotePitchOutput third = adapter.adapt(note(VstEvent::kNoteOnEvent, 12, 67, 14.f));
    EXPECT_EQ(third.events[0].noteOn.channel, 3);
}

TEST(VstPerNotePitchAdapterTests, SixteenthHeldMpeNoteIsLeftOutAndItsReleaseEndsNothing)
{
    VstPerNotePitchAdapter adapter;
    ASSERT_TRUE(adapter.configure(mpeSynth(profilesFolder())).empty());
    for (int32_t id = 1; id <= 15; ++id) {
        EXPECT_EQ(adapter.adapt(note(VstEvent::kNoteOnEvent, id, 60, 0.f)).eventCount, 1u);
    }
    EXPECT_EQ(adapter.adapt(note(VstEvent::kNoteOnEvent, 16, 60, 0.f)).eventCount, 0u);
    EXPECT_EQ(adapter.adapt(note(VstEvent::kNoteOffEvent, 16, 60, 0.f)).eventCount, 0u);
    EXPECT_EQ(adapter.adapt(note(VstEvent::kNoteOffEvent, 1, 60, 0.f)).eventCount, 1u);
}

TEST(VstPerNotePitchAdapterTests, MpeProfileAndNoParameterMappedToPitchBendGivesKeyNumbersAlone)
{
    VstPerNotePitchConfig config = mpeSynth(profilesFolder());
    for (auto& channel : config.midiParams) {
        channel.fill(Steinberg::Vst::kNoParamId);
    }
    VstPerNotePitchAdapter adapter;
    ASSERT_TRUE(adapter.configure(config).empty()) << "the shared selection decides this; it is not a fault of the adapter";
    EXPECT_EQ(adapter.mode(), VstPerNotePitchAdapter::Mode::KeyNumberOnly);
    EXPECT_EQ(adapter.profileId(), "test-mpe-synth");

    const VstPerNotePitchOutput on = adapter.adapt(note(VstEvent::kNoteOnEvent, 7, 67, 14.f));
    ASSERT_EQ(on.eventCount, 1u);
    EXPECT_EQ(on.paramCount, 0u);
}

TEST(VstPerNotePitchAdapterTests, MeasuredProfileFactOutranksThePlugInsOwnListing)
{
    VstPerNotePitchConfig config
        = mpeSynth(profilesFolder(R"(, {"domain": "microtuning", "key": "vst3_tuning_expression", "value": false})"));
    config.advertisesTuningExpression = true;
    VstPerNotePitchAdapter adapter;
    ASSERT_TRUE(adapter.configure(config).empty());
    EXPECT_EQ(adapter.mode(), VstPerNotePitchAdapter::Mode::KeyNumberOnly);

    const VstPerNotePitchOutput on = adapter.adapt(note(VstEvent::kNoteOnEvent, 7, 67, 14.f));
    ASSERT_EQ(on.eventCount, 1u);
    EXPECT_EQ(on.paramCount, 0u);
    EXPECT_FLOAT_EQ(on.events[0].noteOn.tuning, 0.f) << "key numbers alone carry no exact pitch";
}

TEST(VstPerNotePitchAdapterTests, SetupComesFromTheProfileAndAnUnreadableFolderIsReported)
{
    VstPerNotePitchAdapter adapter;
    ASSERT_TRUE(adapter.configure(mpeSynth(profilesFolder(
                                               R"(, {"domain": "microtuning", "key": "mpe", "value": true},
        {"domain": "microtuning", "key": "mpe_enable", "value": "configuration_message"},
        {"domain": "microtuning", "key": "mpe_pitch_bend_range_semitones", "value": 12},
        {"domain": "microtuning", "key": "mpe_pitch_bend_range_settable", "value": true},
        {"domain": "ordering", "key": "mpe_pitch_bend_follows_note_on", "value": false})")))
                .empty());
    const VstPerNotePitchSetup setup = adapter.setup();
    ASSERT_EQ(setup.count, 96u);
    EXPECT_EQ(setup.params[0].paramId, 2000u) << "registered parameter select, most significant, on the manager channel";
    EXPECT_DOUBLE_EQ(setup.params[0].value, 0.0);
    EXPECT_EQ(setup.params[1].paramId, 3000u);
    EXPECT_NEAR(setup.params[1].value, 6.0 / 127.0, 1e-12);
    EXPECT_EQ(setup.params[2].paramId, 4000u);
    EXPECT_NEAR(setup.params[2].value, 15.0 / 127.0, 1e-12);
    const VstPerNotePitchOutput on = adapter.adapt(note(VstEvent::kNoteOnEvent, 1, 69, 50.f));
    ASSERT_EQ(on.paramCount, 1u);
    EXPECT_FALSE(on.params[0].afterNoteOn) << "this receiver takes the bend before the note-on";

    VstPerNotePitchConfig missing = mpeSynth("/nonexistent/melo_profiles");
    EXPECT_FALSE(adapter.configure(missing).empty());
    EXPECT_EQ(adapter.mode(), VstPerNotePitchAdapter::Mode::KeyNumberOnly) << "with no readable profiles the plug-in's own report decides";
}

TEST(VstPerNotePitchAdapterTests, AKeyNumberOutsideMidiIsRefusedByTheProjectorAndTheNoteIsLeftOut)
{
    VstPerNotePitchConfig config;
    config.pluginName = "Unprofiled Synth";
    config.pluginVersion = "3.0";
    config.advertisesTuningExpression = true;
    VstPerNotePitchAdapter adapter;
    ASSERT_TRUE(adapter.configure(config).empty());

    EXPECT_EQ(adapter.adapt(note(VstEvent::kNoteOnEvent, 50, 130, 7.5f)).eventCount, 0u);
    EXPECT_EQ(adapter.adapt(note(VstEvent::kNoteOnEvent, 51, -2, 7.5f)).eventCount, 0u);
    // A note inside the range still sounds.
    EXPECT_EQ(adapter.adapt(note(VstEvent::kNoteOnEvent, 52, 60, 7.5f)).eventCount, 2u);
}
