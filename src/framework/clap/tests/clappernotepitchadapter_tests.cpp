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

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "internal/clappernotepitchadapter.h"

using namespace muse::clap;

//! The expected events below are written from the CLAP specification
//! (clap/events.h): a note-on is a clap_event_note of type CLAP_EVENT_NOTE_ON;
//! a tuning note expression is a clap_event_note_expression whose
//! expression_id is CLAP_NOTE_EXPRESSION_TUNING and whose value is in 12-TET
//! steps of 100 cents; a MIDI message is a clap_event_midi of three bytes.
//! MPE is MIDI Polyphonic Expression: one note per MIDI channel.
namespace {
//! A folder holding one receiver profile for the CLAP build of a plug-in named \p name.
std::string clapProfilesFolder(const std::string& name, const std::string& facts)
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path()
                                         / ("melo_clap_profiles_" + std::to_string(std::hash<std::string> {}(name + facts)));
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "test-clap-synth.json")
        <<
        R"({"schema_version": "melo-receiver-profile/v2", "profile_id": "test-clap-synth", "display_name": "Test",
        "profile_kind": "built_in", "category": "legacy", "voice_allocation_policy": "omit_new_note",
        "basic_midi_policy": "polyphonic_quantized",
        "endpoint_match": {"stable_id_prefix": ")"
        << name << R"(", "firmware_prefix": "1.", "route_prefix": "clap"},
        "facts": [)" << facts << R"(], "source_evidence": ["test"]})";
    return folder.string();
}

const char* const FOLLOWS_TUNING
    =
        R"({"domain": "microtuning", "key": "clap_tuning_expression", "value": true},
    {"domain": "microtuning", "key": "mpe", "value": false},
    {"domain": "ordering", "key": "note_identity", "value": "note_id"})";

//! Measured behaviour like Dexed's: MPE on as loaded, 2,400 cents, the range
//! not settable, the bend follows its note-on.
const char* const MPE_ALREADY_ON
    =
        R"({"domain": "microtuning", "key": "clap_tuning_expression", "value": false},
    {"domain": "microtuning", "key": "mpe", "value": true},
    {"domain": "microtuning", "key": "mpe_enable", "value": "already_on"},
    {"domain": "microtuning", "key": "mpe_pitch_bend_range_semitones", "value": 24},
    {"domain": "microtuning", "key": "mpe_pitch_bend_range_settable", "value": false},
    {"domain": "ordering", "key": "mpe_pitch_bend_follows_note_on", "value": true})";

//! Like OB-Xf's: MPE turned on by the MPE Configuration Message, the range settable.
const char* const MPE_BY_MESSAGE
    =
        R"({"domain": "microtuning", "key": "clap_tuning_expression", "value": false},
    {"domain": "microtuning", "key": "mpe", "value": true},
    {"domain": "microtuning", "key": "mpe_enable", "value": "configuration_message"},
    {"domain": "microtuning", "key": "mpe_pitch_bend_range_semitones", "value": 12},
    {"domain": "microtuning", "key": "mpe_pitch_bend_range_settable", "value": true},
    {"domain": "ordering", "key": "mpe_pitch_bend_follows_note_on", "value": true})";

ClapPerNotePitchConfig plugIn(const std::string& name, const std::string& facts, bool noteEvents, bool midiBytes)
{
    ClapPerNotePitchConfig config;
    config.pluginName = name;
    config.pluginVersion = "1.2.3";
    config.profilesFolder = facts.empty() ? std::string() : clapProfilesFolder(name, facts);
    config.takesNoteEvents = noteEvents;
    config.takesMidiBytes = midiBytes;
    return config;
}

void expectHeader(const clap_event_header_t& header, uint16_t type, size_t size)
{
    EXPECT_EQ(header.size, size);
    EXPECT_EQ(header.space_id, CLAP_CORE_EVENT_SPACE_ID);
    EXPECT_EQ(header.type, type);
    EXPECT_EQ(header.time, 0u) << "the caller places the event in the block";
}

//! The pitch bend a 14-bit MIDI message states: least significant byte first.
int bendOf(const clap_event_midi_t& midi)
{
    return midi.data[1] | (midi.data[2] << 7);
}
}

TEST(ClapPerNotePitchAdapterTests, TuningExpressionRouteSendsANoteOnAndTheOffsetInSteps)
{
    ClapPerNotePitchAdapter adapter;
    ASSERT_EQ(adapter.configure(plugIn("Follows Tuning", FOLLOWS_TUNING, true, true)), "");
    ASSERT_TRUE(adapter.isUsable());
    EXPECT_EQ(adapter.mode(), ClapPerNotePitchAdapter::Mode::TuningExpression);
    EXPECT_EQ(adapter.transport(), ClapPerNotePitchAdapter::Transport::NoteEvents);
    EXPECT_EQ(adapter.profileId(), "test-clap-synth");
    EXPECT_EQ(adapter.setup().count, 0u);

    // A note a syntonic comma (81/80, 21.5063 cents) above key 60.
    const ClapPerNotePitchOutput on = adapter.noteOn(41, 60, 21.5063, 0.75f);
    EXPECT_EQ(on.status, 0);
    EXPECT_EQ(on.undeliverable, 0u);
    ASSERT_EQ(on.count, 2u);

    const clap_event_note_t& note = on.events[0].event.note;
    expectHeader(note.header, CLAP_EVENT_NOTE_ON, sizeof(clap_event_note_t));
    EXPECT_GE(note.note_id, 0);
    EXPECT_EQ(note.port_index, 0);
    EXPECT_EQ(note.channel, 0);
    EXPECT_EQ(note.key, 60);
    EXPECT_DOUBLE_EQ(note.velocity, 0.75);

    const clap_event_note_expression_t& tuning = on.events[1].event.expression;
    expectHeader(tuning.header, CLAP_EVENT_NOTE_EXPRESSION, sizeof(clap_event_note_expression_t));
    EXPECT_EQ(tuning.expression_id, CLAP_NOTE_EXPRESSION_TUNING);
    EXPECT_EQ(tuning.note_id, note.note_id) << "the expression is addressed to the note-on's note id";
    EXPECT_EQ(tuning.port_index, 0);
    EXPECT_EQ(tuning.channel, 0);
    EXPECT_EQ(tuning.key, 60);
    EXPECT_NEAR(tuning.value, 0.215063, 1e-6) << "cents divided by 100, never the VST3-normalized value";

    // A second note on the same key at another pitch is a note of its own.
    const ClapPerNotePitchOutput lower = adapter.noteOn(42, 60, -21.5063, 0.75f);
    ASSERT_EQ(lower.count, 2u);
    EXPECT_NE(lower.events[0].event.note.note_id, note.note_id);
    EXPECT_EQ(lower.events[1].event.expression.note_id, lower.events[0].event.note.note_id);
    EXPECT_NEAR(lower.events[1].event.expression.value, -0.215063, 1e-6);

    // A note with no offset still states its expression.
    const ClapPerNotePitchOutput plain = adapter.noteOn(43, 64, 0.0, 0.5f);
    ASSERT_EQ(plain.count, 2u);
    EXPECT_DOUBLE_EQ(plain.events[1].event.expression.value, 0.0);

    const ClapPerNotePitchOutput off = adapter.noteOff(41);
    ASSERT_EQ(off.count, 1u);
    expectHeader(off.events[0].event.note.header, CLAP_EVENT_NOTE_OFF, sizeof(clap_event_note_t));
    EXPECT_EQ(off.events[0].event.note.note_id, note.note_id);
    EXPECT_EQ(off.events[0].event.note.key, 60);
    EXPECT_EQ(off.events[0].event.note.channel, 0);
    EXPECT_EQ(off.events[0].event.note.port_index, 0);
    EXPECT_EQ(adapter.soundingNotes(), 2u);
}

TEST(ClapPerNotePitchAdapterTests, TuningExpressionRouteHoldsMoreThanFifteenNotesAtOnce)
{
    ClapPerNotePitchAdapter adapter;
    ASSERT_EQ(adapter.configure(plugIn("Follows Tuning", FOLLOWS_TUNING, true, true)), "");

    std::set<int32_t> ids;
    for (int32_t i = 0; i < 40; ++i) {
        const ClapPerNotePitchOutput on = adapter.noteOn(100 + i, 40 + i, i * 0.5, 0.6f);
        ASSERT_EQ(on.status, 0) << "note " << i << ": this route has no ceiling of fifteen notes";
        ASSERT_EQ(on.count, 2u);
        EXPECT_NEAR(on.events[1].event.expression.value, i * 0.005, 1e-9);
        ids.insert(on.events[0].event.note.note_id);
    }
    EXPECT_EQ(ids.size(), 40u);
    EXPECT_EQ(adapter.soundingNotes(), 40u);
    for (int32_t i = 0; i < 40; ++i) {
        EXPECT_EQ(adapter.noteOff(100 + i).count, 1u);
    }
    EXPECT_EQ(adapter.soundingNotes(), 0u);
}

TEST(ClapPerNotePitchAdapterTests, AHeldNoteIsRetunedByANewTuningExpression)
{
    ClapPerNotePitchAdapter adapter;
    ASSERT_EQ(adapter.configure(plugIn("Follows Tuning", FOLLOWS_TUNING, true, true)), "");
    const ClapPerNotePitchOutput on = adapter.noteOn(5, 67, 2.0, 0.9f);
    ASSERT_EQ(on.count, 2u);

    const ClapPerNotePitchOutput moved = adapter.pitchChange(5, -19.5);
    EXPECT_EQ(moved.status, 0);
    ASSERT_EQ(moved.count, 1u);
    const clap_event_note_expression_t& tuning = moved.events[0].event.expression;
    expectHeader(tuning.header, CLAP_EVENT_NOTE_EXPRESSION, sizeof(clap_event_note_expression_t));
    EXPECT_EQ(tuning.expression_id, CLAP_NOTE_EXPRESSION_TUNING);
    EXPECT_EQ(tuning.note_id, on.events[0].event.note.note_id);
    EXPECT_EQ(tuning.key, 67);
    EXPECT_EQ(tuning.channel, 0);
    EXPECT_NEAR(tuning.value, -0.195, 1e-9);

    // A note that is not sounding has nothing to retune.
    const ClapPerNotePitchOutput unknown = adapter.pitchChange(6, 10.0);
    EXPECT_NE(unknown.status, 0);
    EXPECT_EQ(unknown.count, 0u);
}

TEST(ClapPerNotePitchAdapterTests, MpeTravelsAsMidiBytesWithTheBendAfterTheNoteOn)
{
    ClapPerNotePitchAdapter adapter;
    // The note port takes CLAP note events as well: MPE is MIDI all the same.
    ASSERT_EQ(adapter.configure(plugIn("Midi Only", MPE_ALREADY_ON, true, true)), "");
    EXPECT_EQ(adapter.mode(), ClapPerNotePitchAdapter::Mode::Mpe);
    EXPECT_EQ(adapter.transport(), ClapPerNotePitchAdapter::Transport::MidiBytes);
    EXPECT_EQ(adapter.setup().count, 0u) << "MPE is on as loaded and the range is not settable";

    const ClapPerNotePitchOutput first = adapter.noteOn(10, 60, 28.0, 1.0f);
    const ClapPerNotePitchOutput second = adapter.noteOn(11, 60, -28.0, 1.0f);
    EXPECT_EQ(first.status, 0);
    EXPECT_EQ(first.undeliverable, 0u);
    ASSERT_EQ(first.count, 2u);
    ASSERT_EQ(second.count, 2u);

    // MIDI 1.0: a note-on is status 0x9n, key, velocity; channel index 1 is
    // the first member channel of the MPE lower zone.
    const clap_event_midi_t& noteOn = first.events[0].event.midi;
    expectHeader(noteOn.header, CLAP_EVENT_MIDI, sizeof(clap_event_midi_t));
    EXPECT_EQ(noteOn.port_index, 0);
    EXPECT_EQ(noteOn.data[0], 0x91);
    EXPECT_EQ(noteOn.data[1], 60);
    EXPECT_EQ(noteOn.data[2], 127);
    EXPECT_FALSE(first.events[0].afterNoteOn);
    EXPECT_EQ(second.events[0].event.midi.data[0], 0x92) << "two notes on one key take two channels";

    // MIDI 1.0: pitch bend is status 0xEn and a 14-bit value, centre 8192.
    // 28 of 2,400 cents of the upper 8,191 steps is 96 steps.
    const clap_event_midi_t& bend = first.events[1].event.midi;
    expectHeader(bend.header, CLAP_EVENT_MIDI, sizeof(clap_event_midi_t));
    EXPECT_EQ(bend.data[0], 0xE1);
    EXPECT_EQ(bendOf(bend), 8192 + 96);
    EXPECT_TRUE(first.events[1].afterNoteOn) << "this receiver loses a bend that does not follow its note-on";
    EXPECT_EQ(second.events[1].event.midi.data[0], 0xE2);
    EXPECT_EQ(bendOf(second.events[1].event.midi), 8192 - 96);

    // A held note is retuned by a new bend on its own channel.
    const ClapPerNotePitchOutput moved = adapter.pitchChange(10, 14.0);
    ASSERT_EQ(moved.count, 1u);
    EXPECT_EQ(moved.events[0].event.midi.data[0], 0xE1);
    EXPECT_EQ(bendOf(moved.events[0].event.midi), 8192 + 48);

    // MIDI 1.0: a note-off is status 0x8n and the key.
    const ClapPerNotePitchOutput off = adapter.noteOff(10);
    ASSERT_EQ(off.count, 1u);
    EXPECT_EQ(off.events[0].event.midi.data[0], 0x81);
    EXPECT_EQ(off.events[0].event.midi.data[1], 60);
}

TEST(ClapPerNotePitchAdapterTests, MpeSetupIsTheConfigurationMessageAsMidiBytes)
{
    ClapPerNotePitchAdapter adapter;
    ASSERT_EQ(adapter.configure(plugIn("Midi By Message", MPE_BY_MESSAGE, true, true)), "");
    ASSERT_EQ(adapter.mode(), ClapPerNotePitchAdapter::Mode::Mpe);

    const ClapPerNotePitchSetup setup = adapter.setup();
    EXPECT_EQ(setup.undeliverable, 0u);
    ASSERT_GE(setup.count, 3u);
    // The MPE Configuration Message: registered parameter 6 on the manager
    // channel (channel index 0), data entry 15 member channels. As MIDI 1.0
    // control changes: controller 101 = 0, controller 100 = 6, controller 6 = 15.
    const uint8_t expected[3][3] = { { 0xB0, 101, 0 }, { 0xB0, 100, 6 }, { 0xB0, 6, 15 } };
    for (size_t i = 0; i < 3; ++i) {
        const clap_event_midi_t& midi = setup.events[i].midi;
        expectHeader(midi.header, CLAP_EVENT_MIDI, sizeof(clap_event_midi_t));
        EXPECT_EQ(midi.port_index, 0);
        EXPECT_EQ(midi.data[0], expected[i][0]) << "message " << i;
        EXPECT_EQ(midi.data[1], expected[i][1]) << "message " << i;
        EXPECT_EQ(midi.data[2], expected[i][2]) << "message " << i;
    }
    for (size_t i = 0; i < setup.count; ++i) {
        EXPECT_EQ(setup.events[i].header.type, CLAP_EVENT_MIDI);
        EXPECT_EQ(setup.events[i].midi.data[0] & 0xF0, 0xB0) << "setup is made of control changes";
    }
}

TEST(ClapPerNotePitchAdapterTests, AnUnprofiledPlugInIsSentKeyNumbersAloneAsItsPortTakesThem)
{
    ClapPerNotePitchAdapter notes;
    ASSERT_EQ(notes.configure(plugIn("Unprofiled", "", true, true)), "");
    EXPECT_EQ(notes.mode(), ClapPerNotePitchAdapter::Mode::KeyNumberOnly) << "a CLAP note port is not proof of tuning";
    EXPECT_EQ(notes.transport(), ClapPerNotePitchAdapter::Transport::NoteEvents);
    EXPECT_EQ(notes.profileId(), "");

    const ClapPerNotePitchOutput on = notes.noteOn(1, 62, 14.0, 0.25f);
    ASSERT_EQ(on.count, 1u);
    expectHeader(on.events[0].event.note.header, CLAP_EVENT_NOTE_ON, sizeof(clap_event_note_t));
    EXPECT_EQ(on.events[0].event.note.key, 62);
    EXPECT_DOUBLE_EQ(on.events[0].event.note.velocity, 0.25);

    // Key numbers alone cannot retune a held note; nothing is sent.
    const ClapPerNotePitchOutput moved = notes.pitchChange(1, 20.0);
    EXPECT_NE(moved.status, 0);
    EXPECT_EQ(moved.count, 0u);

    // The same plug-in whose port takes only MIDI bytes gets the note as MIDI.
    ClapPerNotePitchAdapter bytes;
    ASSERT_EQ(bytes.configure(plugIn("Unprofiled", "", false, true)), "");
    EXPECT_EQ(bytes.mode(), ClapPerNotePitchAdapter::Mode::KeyNumberOnly);
    EXPECT_EQ(bytes.transport(), ClapPerNotePitchAdapter::Transport::MidiBytes);
    const ClapPerNotePitchOutput midiOn = bytes.noteOn(1, 62, 14.0, 1.0f);
    ASSERT_EQ(midiOn.count, 1u);
    expectHeader(midiOn.events[0].event.midi.header, CLAP_EVENT_MIDI, sizeof(clap_event_midi_t));
    EXPECT_EQ(midiOn.events[0].event.midi.data[0] & 0xF0, 0x90);
    EXPECT_EQ(midiOn.events[0].event.midi.data[1], 62);
    EXPECT_EQ(midiOn.events[0].event.midi.data[2], 127);
}

TEST(ClapPerNotePitchAdapterTests, AProfileOfTheVst3BuildIsNotAppliedToTheClapBuild)
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "melo_clap_profiles_vst3_only";
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "follows-tuning-vst3.json")
        <<
        R"({"schema_version": "melo-receiver-profile/v2", "profile_id": "follows-tuning-vst3", "display_name": "v",
        "profile_kind": "built_in", "category": "legacy", "voice_allocation_policy": "omit_new_note",
        "basic_midi_policy": "polyphonic_quantized",
        "endpoint_match": {"stable_id_prefix": "Follows Tuning", "firmware_prefix": "1.", "route_prefix": "vst3"},
        "facts": [{"domain": "protocols", "key": "vst3_note_events", "value": true},
                  {"domain": "microtuning", "key": "vst3_note_on_tuning", "value": true}],
        "source_evidence": ["test"]})";

    ClapPerNotePitchConfig config = plugIn("Follows Tuning", "", true, true);
    config.profilesFolder = folder.string();
    ClapPerNotePitchAdapter adapter;
    ASSERT_EQ(adapter.configure(config), "");
    EXPECT_EQ(adapter.profileId(), "");
    EXPECT_EQ(adapter.mode(), ClapPerNotePitchAdapter::Mode::KeyNumberOnly);
}

TEST(ClapPerNotePitchAdapterTests, EverySoundingNoteCanBeEndedSoThatNoneIsLeftStuck)
{
    for (const char* facts : { FOLLOWS_TUNING, MPE_ALREADY_ON }) {
        ClapPerNotePitchAdapter adapter;
        ASSERT_EQ(adapter.configure(plugIn("Stops Cleanly", facts, true, true)), "");
        const bool midi = adapter.transport() == ClapPerNotePitchAdapter::Transport::MidiBytes;

        std::multiset<int> started;
        for (int32_t i = 0; i < 6; ++i) {
            const ClapPerNotePitchOutput on = adapter.noteOn(200 + i, 50 + i, 3.0, 0.5f);
            ASSERT_EQ(on.status, 0);
            started.insert(50 + i);
        }
        ASSERT_EQ(adapter.soundingNotes(), 6u);

        // Stop, seek or repeat: each sounding note gets its own release.
        std::multiset<int> ended;
        ClapPerNotePitchOutput output;
        while (adapter.releaseNext(output)) {
            ASSERT_EQ(output.count, 1u);
            if (midi) {
                EXPECT_EQ(output.events[0].event.midi.data[0] & 0xF0, 0x80);
                ended.insert(output.events[0].event.midi.data[1]);
            } else {
                EXPECT_EQ(output.events[0].event.note.header.type, CLAP_EVENT_NOTE_OFF);
                ended.insert(output.events[0].event.note.key);
            }
        }
        EXPECT_EQ(ended, started);
        EXPECT_EQ(adapter.soundingNotes(), 0u);

        // A release that arrives afterwards ends nothing twice.
        EXPECT_EQ(adapter.noteOff(200).count, 0u);
        // The same note ids can sound again.
        EXPECT_EQ(adapter.noteOn(200, 50, 0.0, 0.5f).status, 0);
    }
}

TEST(ClapPerNotePitchAdapterTests, AnUnreadableProfileFolderIsReportedAndNoRouteIsInvented)
{
    ClapPerNotePitchConfig config = plugIn("Follows Tuning", "", true, true);
    config.profilesFolder = "/nonexistent/melo_profiles";
    ClapPerNotePitchAdapter adapter;
    EXPECT_NE(adapter.configure(config), "");
    EXPECT_FALSE(adapter.isUsable());
    EXPECT_EQ(adapter.mode(), ClapPerNotePitchAdapter::Mode::None);
    const ClapPerNotePitchOutput on = adapter.noteOn(1, 60, 0.0, 0.5f);
    EXPECT_NE(on.status, 0) << "the note is refused, not sent by a route of this host's choosing";
    EXPECT_EQ(on.count, 0u);

    // A plug-in with no note input at all is refused by the shared selection.
    ClapPerNotePitchAdapter deaf;
    EXPECT_NE(deaf.configure(plugIn("Unprofiled", "", false, false)), "");
    EXPECT_FALSE(deaf.isUsable());
}

TEST(ClapPerNotePitchAdapterTests, AKeyNumberOutsideMidiIsRefusedByTheProjectorNotMovedToAnotherKey)
{
    for (const char* facts : { FOLLOWS_TUNING, MPE_ALREADY_ON }) {
        ClapPerNotePitchAdapter adapter;
        ASSERT_EQ(adapter.configure(plugIn("Out Of Range", facts, true, true)), "");
        for (const int32_t key : { 128, 130, -1 }) {
            const ClapPerNotePitchOutput on = adapter.noteOn(500 + key, key, 12.5, 0.5f);
            EXPECT_EQ(on.status, 6) << "MELO_PROJECTOR_KEY_NUMBER_OUT_OF_RANGE for key " << key;
            EXPECT_EQ(on.count, 0u);
        }
        EXPECT_EQ(adapter.soundingNotes(), 0u);
    }
}
