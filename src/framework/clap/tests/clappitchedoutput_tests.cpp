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

#include "internal/clapbundle.h"
#include "internal/claphostedplugin.h"
#include "internal/clappitchedoutput.h"

#include "clapfixture.h"

using namespace muse::clap;

//! These tests play notes through the fixture plug-in and measure the pitch
//! of the sine wave it renders. The measure is the sound, not the events.
//! MPE is MIDI Polyphonic Expression: one note per MIDI channel.
namespace {
constexpr double RATE = 48000.0;
//! A syntonic comma, 81/80, in cents.
constexpr double COMMA = 21.5063;

std::string fixtureProfiles(const std::string& name, const std::string& facts)
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path()
                                         / ("melo_clap_fixture_profiles_" + std::to_string(std::hash<std::string> {}(name + facts)));
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "fixture.json")
        <<
        R"({"schema_version": "melo-receiver-profile/v2", "profile_id": "clap-fixture", "display_name": "Fixture",
        "profile_kind": "built_in", "category": "legacy", "voice_allocation_policy": "omit_new_note",
        "basic_midi_policy": "polyphonic_quantized",
        "endpoint_match": {"stable_id_prefix": ")"
        << name << R"(", "firmware_prefix": "1.", "route_prefix": "clap"},
        "facts": [)" << facts << R"(], "source_evidence": ["test"]})";
    return folder.string();
}

//! What the fixture's "Fixture Tone" does: it follows the CLAP tuning note expression.
const char* const TONE_FACTS
    =
        R"({"domain": "microtuning", "key": "clap_tuning_expression", "value": true},
    {"domain": "microtuning", "key": "mpe", "value": false},
    {"domain": "ordering", "key": "note_identity", "value": "note_id"})";

//! What "Fixture Midi Only" does: MPE after the MPE Configuration Message,
//! a settable bend range, a bend that follows its note-on.
const char* const MIDI_ONLY_FACTS
    =
        R"({"domain": "microtuning", "key": "clap_tuning_expression", "value": false},
    {"domain": "microtuning", "key": "mpe", "value": true},
    {"domain": "microtuning", "key": "mpe_enable", "value": "configuration_message"},
    {"domain": "microtuning", "key": "mpe_pitch_bend_range_semitones", "value": 12},
    {"domain": "microtuning", "key": "mpe_pitch_bend_range_settable", "value": true},
    {"domain": "ordering", "key": "mpe_pitch_bend_follows_note_on", "value": true},
    {"domain": "ordering", "key": "note_identity", "value": "channel_and_key"})";

struct Player {
    ClapBundlePtr bundle;
    ClapHostedPlugin plugin;
    ClapPitchedOutput output;
    std::string problem;

    Player(const char* descriptorId, const char* facts, const std::string& folderOverride = std::string())
    {
        std::string openProblem;
        bundle = ClapBundle::open(fixture::bundlePath(), openProblem);
        EXPECT_TRUE(bundle) << openProblem;
        EXPECT_TRUE(plugin.create(bundle, descriptorId, openProblem)) << openProblem;
        EXPECT_TRUE(plugin.activate(RATE));

        ClapPerNotePitchConfig config;
        config.pluginName = plugin.descriptor().name;
        config.pluginVersion = plugin.descriptor().version;
        config.profilesFolder = !folderOverride.empty() ? folderOverride
                                : (facts ? fixtureProfiles(plugin.descriptor().name, facts) : std::string());
        config.takesNoteEvents = plugin.notePorts().takesNoteEvents;
        config.takesMidiBytes = plugin.notePorts().takesMidiBytes;
        problem = output.prepare(&plugin, config, 2);
    }

    //! Renders in the mixer's manner: block after block of 512 frames.
    std::vector<float> render(double seconds)
    {
        const uint32_t frames = static_cast<uint32_t>(seconds * RATE);
        std::vector<float> audio(static_cast<size_t>(frames) * 2, 0.f);
        uint32_t done = 0;
        while (done < frames) {
            const uint32_t block = std::min<uint32_t>(512, frames - done);
            output.render(audio.data() + static_cast<size_t>(done) * 2, block);
            done += block;
        }
        return audio;
    }

    //! The pitch of what sounds now, in cents from \p key; the first 50 milliseconds are let pass.
    double centsFromKey(int key, double seconds = 1.0)
    {
        const std::vector<float> audio = render(seconds);
        const double frequency = fixture::measuredFrequency(audio, 2, RATE, static_cast<size_t>(0.05 * RATE));
        return frequency > 0.0 ? fixture::centsBetween(frequency, fixture::keyFrequency(key, 0.0)) : -99999.0;
    }

    double param(uint32_t id) const
    {
        double value = -12345.0;
        EXPECT_TRUE(plugin.parameterValue(id, value));
        return value;
    }
};
}

TEST(ClapPitchedOutputTests, TuningExpressionRouteSoundsEachNoteAtItsExactPitch)
{
    Player player(fixture::TONE_ID, TONE_FACTS);
    ASSERT_EQ(player.problem, "");
    ASSERT_EQ(player.output.adapter().mode(), ClapPerNotePitchAdapter::Mode::TuningExpression);
    player.output.sendSetup();

    // An unshifted note is the control: key 60 in 12-TET.
    player.output.noteOn(1, 60, 0.0, 0.8f);
    EXPECT_NEAR(player.centsFromKey(60), 0.0, 0.05);
    player.output.noteOff(1);
    player.render(0.05);

    // A syntonic comma above, then below.
    player.output.noteOn(2, 60, COMMA, 0.8f);
    EXPECT_NEAR(player.centsFromKey(60), COMMA, 0.05);
    player.output.noteOff(2);
    player.render(0.05);
    player.output.noteOn(3, 60, -COMMA, 0.8f);
    EXPECT_NEAR(player.centsFromKey(60), -COMMA, 0.05);
    player.output.noteOff(3);
    player.render(0.05);
    EXPECT_EQ(player.param(fixture::VoicesSounding), 0.0);
    EXPECT_EQ(player.output.takeFault(), ClapAudioFault::None);
}

TEST(ClapPitchedOutputTests, TwoNotesOnOneKeyNumberKeepTheirOwnPitches)
{
    Player player(fixture::TONE_ID, TONE_FACTS);
    ASSERT_EQ(player.problem, "");

    // Both sound at once on key 69, a comma apart; their releases are staggered.
    player.output.noteOn(10, 69, COMMA, 0.8f);
    player.output.noteOn(11, 69, -COMMA, 0.8f);
    player.render(0.2);
    EXPECT_EQ(player.param(fixture::VoicesSounding), 2.0);

    player.output.noteOff(10);
    player.render(0.05);
    EXPECT_EQ(player.param(fixture::VoicesSounding), 1.0) << "the release ended one note, not the key";
    EXPECT_NEAR(player.centsFromKey(69), -COMMA, 0.05) << "the note that stayed kept its own pitch";

    player.output.noteOn(12, 69, COMMA, 0.8f);
    player.render(0.05);
    player.output.noteOff(11);
    player.render(0.05);
    EXPECT_NEAR(player.centsFromKey(69), COMMA, 0.05);
    player.output.noteOff(12);
    player.render(0.05);
    EXPECT_EQ(player.param(fixture::VoicesSounding), 0.0);
}

TEST(ClapPitchedOutputTests, AHeldNoteIsRetunedOnBothRoutes)
{
    {
        Player tone(fixture::TONE_ID, TONE_FACTS);
        ASSERT_EQ(tone.problem, "");
        tone.output.noteOn(1, 67, 2.0, 0.8f);
        EXPECT_NEAR(tone.centsFromKey(67, 0.5), 2.0, 0.05);
        tone.output.pitchChange(1, -30.0);
        EXPECT_NEAR(tone.centsFromKey(67, 0.5), -30.0, 0.05) << "a new tuning expression for the same note";
        EXPECT_EQ(tone.param(fixture::NotesStarted), 1.0) << "the note was not struck again";
    }
    {
        Player midi(fixture::MIDI_ONLY_ID, MIDI_ONLY_FACTS);
        ASSERT_EQ(midi.problem, "");
        ASSERT_EQ(midi.output.adapter().mode(), ClapPerNotePitchAdapter::Mode::Mpe);
        midi.output.sendSetup();
        midi.plugin.serveMainThreadCallbacks();
        midi.output.noteOn(1, 67, 2.0, 0.8f);
        // One step of a 14-bit bend over 1,200 cents is 0.15 cents.
        EXPECT_NEAR(midi.centsFromKey(67, 0.5), 2.0, 0.1);
        midi.output.pitchChange(1, -30.0);
        EXPECT_NEAR(midi.centsFromKey(67, 0.5), -30.0, 0.1) << "a new pitch bend on the note's own channel";
        EXPECT_EQ(midi.param(fixture::NotesStarted), 1.0);
    }
}

TEST(ClapPitchedOutputTests, MpeSetupIsSentAtLoadAndTakesEffectOnceTheMainThreadServedIt)
{
    // Served: setup is delivered in a discarded block, the plug-in asks for
    // its main-thread callback, the host serves it, and the first note is in tune.
    {
        Player player(fixture::MIDI_ONLY_ID, MIDI_ONLY_FACTS);
        ASSERT_EQ(player.problem, "");
        ASSERT_EQ(player.output.adapter().transport(), ClapPerNotePitchAdapter::Transport::MidiBytes);
        player.output.sendSetup();
        EXPECT_GE(player.param(fixture::ProcessCalls), 1.0) << "setup went through a real block at load, before any note";
        EXPECT_EQ(player.param(fixture::ZeroFrameProcessCalls), 0.0);
        EXPECT_EQ(player.param(fixture::MpeEnabled), 0.0);
        EXPECT_GE(player.plugin.serveMainThreadCallbacks(), 1);
        EXPECT_EQ(player.param(fixture::MpeEnabled), 1.0);

        // A note at the very start of playback.
        player.output.noteOn(1, 60, COMMA, 0.8f);
        EXPECT_NEAR(player.centsFromKey(60), COMMA, 0.1);
        EXPECT_EQ(player.param(fixture::LastTuningDelay), 1.0) << "the bend followed its note-on by one sample";
        EXPECT_EQ(player.output.takeFault(), ClapAudioFault::None);
    }
    // Not served: the same notes are out of tune. Waiting a while would not
    // have proved that the setup took effect; serving the callback does.
    {
        Player player(fixture::MIDI_ONLY_ID, MIDI_ONLY_FACTS);
        player.output.sendSetup();
        player.output.noteOn(1, 60, COMMA, 0.8f);
        EXPECT_GT(std::abs(player.centsFromKey(60) - COMMA), 5.0);
    }
}

TEST(ClapPitchedOutputTests, ABendThatFollowsItsNoteOnCrossesABlockOfOneFrame)
{
    Player player(fixture::MIDI_ONLY_ID, MIDI_ONLY_FACTS);
    ASSERT_EQ(player.problem, "");
    player.output.sendSetup();
    player.plugin.serveMainThreadCallbacks();

    // The note-on lands on the only sample of a block; the bend opens the next block.
    std::vector<float> one(2, 0.f);
    player.output.noteOn(1, 60, COMMA, 0.8f);
    EXPECT_EQ(player.output.render(one.data(), 1), 1u);
    EXPECT_EQ(player.param(fixture::LastTuningDelay), -1.0);
    EXPECT_NEAR(player.centsFromKey(60), COMMA, 0.1);
    EXPECT_EQ(player.param(fixture::LastTuningDelay), 1.0);

    // With no frames at all nothing is processed and the note still starts in tune.
    player.output.noteOff(1);
    player.render(0.05);
    player.output.noteOn(2, 62, -COMMA, 0.8f);
    EXPECT_EQ(player.output.render(one.data(), 0), 0u);
    EXPECT_NEAR(player.centsFromKey(62), -COMMA, 0.1);
    EXPECT_EQ(player.param(fixture::LastTuningDelay), 1.0);
    EXPECT_EQ(player.param(fixture::ZeroFrameProcessCalls), 0.0);
}

TEST(ClapPitchedOutputTests, MoreThanFifteenNotesSoundTogetherOnTheTuningExpressionRoute)
{
    Player player(fixture::TONE_ID, TONE_FACTS);
    ASSERT_EQ(player.problem, "");
    for (int32_t i = 0; i < 24; ++i) {
        player.output.noteOn(100 + i, 48 + i, i * 1.5, 0.3f);
    }
    player.render(0.1);
    EXPECT_EQ(player.param(fixture::NotesStarted), 24.0);
    EXPECT_EQ(player.param(fixture::VoicesSounding), 24.0);
    EXPECT_EQ(player.output.takeFault(), ClapAudioFault::None);

    // On the MPE route a sixteenth note has no channel: it is refused with a
    // fault, never dropped in silence, and the fifteen go on sounding.
    Player midi(fixture::MIDI_ONLY_ID, MIDI_ONLY_FACTS);
    ASSERT_EQ(midi.problem, "");
    midi.output.sendSetup();
    midi.plugin.serveMainThreadCallbacks();
    for (int32_t i = 0; i < 16; ++i) {
        midi.output.noteOn(100 + i, 48 + i, 0.0, 0.3f);
    }
    midi.render(0.1);
    EXPECT_EQ(midi.param(fixture::VoicesSounding), 15.0);
    EXPECT_EQ(midi.output.takeFault(), ClapAudioFault::NoteRefused);
}

TEST(ClapPitchedOutputTests, NoNoteIsLeftSoundingAfterStopSeekOrRepeat)
{
    for (const bool midiOnly : { false, true }) {
        Player player(midiOnly ? fixture::MIDI_ONLY_ID : fixture::TONE_ID, midiOnly ? MIDI_ONLY_FACTS : TONE_FACTS);
        ASSERT_EQ(player.problem, "");
        player.output.sendSetup();
        player.plugin.serveMainThreadCallbacks();

        for (int32_t i = 0; i < 5; ++i) {
            player.output.noteOn(i, 60 + i, 4.0, 0.5f);
        }
        player.render(0.1);
        ASSERT_EQ(player.param(fixture::VoicesSounding), 5.0);

        // Stop, seek or repeat: the releases of these notes will never come from the score.
        player.output.allNotesOff();
        player.render(0.1);
        EXPECT_EQ(player.param(fixture::VoicesSounding), 0.0) << (midiOnly ? "MIDI bytes" : "note events");

        // The same notes play again after the jump, and their own releases end them.
        player.output.noteOn(0, 60, 4.0, 0.5f);
        player.output.noteOff(3); // a release of a note from before the jump ends nothing
        player.render(0.1);
        EXPECT_EQ(player.param(fixture::VoicesSounding), 1.0);
        player.output.noteOff(0);
        player.render(0.1);
        EXPECT_EQ(player.param(fixture::VoicesSounding), 0.0);
        EXPECT_EQ(player.output.takeFault(), ClapAudioFault::None);
    }
}

TEST(ClapPitchedOutputTests, WithoutAProfileTheRouteIsKeyNumbersAndTheProfileIsWhatGivesExactPitch)
{
    // The same plug-in, the same note; only the profile differs.
    Player unprofiled(fixture::TONE_ID, nullptr);
    ASSERT_EQ(unprofiled.problem, "");
    EXPECT_EQ(unprofiled.output.adapter().mode(), ClapPerNotePitchAdapter::Mode::KeyNumberOnly);
    unprofiled.output.noteOn(1, 60, COMMA, 0.8f);
    EXPECT_NEAR(unprofiled.centsFromKey(60), 0.0, 0.05) << "key numbers alone carry no exact pitch";

    Player profiled(fixture::TONE_ID, TONE_FACTS);
    profiled.output.noteOn(1, 60, COMMA, 0.8f);
    EXPECT_NEAR(profiled.centsFromKey(60), COMMA, 0.05);
}

TEST(ClapPitchedOutputTests, AFailedSelectionLeavesThePlugInSilentAndEveryNoteIsAFault)
{
    Player player(fixture::TONE_ID, nullptr, "/nonexistent/melo_profiles");
    EXPECT_NE(player.problem, "");
    EXPECT_FALSE(player.output.adapter().isUsable());

    player.output.noteOn(1, 60, 0.0, 0.8f);
    const std::vector<float> audio = player.render(0.1);
    for (const float sample : audio) {
        ASSERT_EQ(sample, 0.f);
    }
    EXPECT_EQ(player.param(fixture::NotesStarted), 0.0);
    EXPECT_EQ(player.output.takeFault(), ClapAudioFault::NoPitchPath);

    // The log is told once, at the first note, not once a note.
    player.output.noteOn(2, 62, 0.0, 0.8f);
    player.render(0.01);
    EXPECT_EQ(player.output.takeFault(), ClapAudioFault::None);
}

TEST(ClapPitchedOutputTests, AHostParameterValueReachesThePlugInWithAndWithoutAudio)
{
    Player player(fixture::TONE_ID, TONE_FACTS);
    ASSERT_EQ(player.problem, "");
    std::vector<float> none(2, 0.f);

    // No frames: the value goes through the parameter flush.
    player.render(0.01);
    const double calls = player.param(fixture::ProcessCalls);
    EXPECT_TRUE(player.output.parameter(fixture::Level, 0.5));
    EXPECT_EQ(player.output.render(none.data(), 0), 0u);
    EXPECT_EQ(player.param(fixture::Level), 0.5);
    EXPECT_EQ(player.param(fixture::FlushCalls), 1.0);
    EXPECT_EQ(player.param(fixture::ProcessCalls), calls);

    // With frames: it travels in the block.
    EXPECT_TRUE(player.output.parameter(fixture::Level, 1.25));
    player.render(0.01);
    EXPECT_EQ(player.param(fixture::Level), 1.25);

    EXPECT_FALSE(player.output.parameter(fixture::Level, 9.0)) << "outside the plug-in's plain range";
    EXPECT_FALSE(player.output.parameter(4242, 0.5));
}
