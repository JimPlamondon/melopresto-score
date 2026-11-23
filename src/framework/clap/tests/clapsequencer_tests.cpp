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

#include <map>
#include <set>
#include <vector>

#include "mpe/events.h"

#include "internal/synth/clapsequencer.h"

using namespace muse;
using namespace muse::clap;
using namespace muse::mpe;

namespace {
constexpr muse::audio::msecs_t SECOND = 1000000;

NoteEvent stockNote(timestamp_t at, duration_t dur, pitch_level_t level)
{
    return NoteEvent(at, dur, 0, 0, level, 5000, ArticulationMap(), 2.0, 0.f, PitchCurve());
}

//! A note of the MeloPresto Music System: the Kernel computed its key number and cents.
NoteEvent exactNote(timestamp_t at, duration_t dur, pitch_level_t level, int key, double cents)
{
    ExactPitch exact;
    exact.frequencyHz = 440.0;
    exact.midiKey = key;
    exact.centsOffset = cents;
    return NoteEvent(at, dur, 0, 0, level, 5000, ArticulationMap(), 2.0, 0.f, PitchCurve(), exact);
}

//! A held note whose pitch glides by \p bendLevels over its whole length.
NoteEvent bentNote(timestamp_t at, duration_t dur, pitch_level_t level, pitch_level_t bendLevels,
                   const std::optional<ExactPitch>& exact = std::nullopt)
{
    ArrangementContext arr;
    arr.nominalTimestamp = at;
    arr.actualTimestamp = at;
    arr.nominalDuration = dur;
    arr.actualDuration = dur;
    arr.voiceLayerIndex = 0;
    arr.staffLayerIndex = 0;
    arr.bps = 2.0;

    PitchContext pitch;
    pitch.nominalPitchLevel = level;
    pitch.pitchCurve.insert({ 0, 0 });
    pitch.pitchCurve.insert({ HUNDRED_PERCENT, bendLevels });
    pitch.exactPitch = exact;

    ExpressionContext expr;
    expr.nominalDynamicLevel = 5000;
    ArticulationMeta meta(ArticulationType::Multibend);
    meta.timestamp = at;
    meta.overallDuration = dur;
    meta.overallPitchChangesRange = bendLevels;
    expr.articulations.emplace(ArticulationType::Multibend, ArticulationAppliedData(meta, 0, HUNDRED_PERCENT));

    return NoteEvent(std::move(arr), std::move(pitch), std::move(expr));
}

struct Timed {
    muse::audio::msecs_t at = 0;
    ClapNoteEvent event;
};

std::vector<Timed> run(ClapSequencer& sequencer, const PlaybackEventsMap& events)
{
    PlaybackData data;
    data.originEvents = events;
    sequencer.load(data);
    sequencer.init(false);
    sequencer.setActive(true);
    sequencer.setPlaybackPosition(0);

    std::vector<Timed> result;
    for (const auto& pair : sequencer.movePlaybackForward(100 * SECOND)) {
        for (const auto& item : pair.second) {
            if (std::holds_alternative<ClapNoteEvent>(item)) {
                result.push_back(Timed { pair.first, std::get<ClapNoteEvent>(item) });
            }
        }
    }
    return result;
}

//! Pitch levels: one 12-TET step is PITCH_LEVEL_STEP; level 0 is key 60... as the stock sequencers count it.
pitch_level_t levelOfKey(int key)
{
    return static_cast<pitch_level_t>((key - ZERO_PITCH_LEVEL_MIDI_EQUIVALENT) * PITCH_LEVEL_STEP);
}
}

TEST(ClapSequencerTests, AStockNoteIsItsNearestKeyAndAnExactNoteIsTheKernelsKeyAndCents)
{
    ClapSequencer sequencer;
    PlaybackEventsMap events;
    events[0].push_back(stockNote(0, SECOND, levelOfKey(64)));
    events[2 * SECOND].push_back(exactNote(2 * SECOND, SECOND, levelOfKey(62), 62, -21.5063));

    const std::vector<Timed> out = run(sequencer, events);
    ASSERT_EQ(out.size(), 4u);

    EXPECT_EQ(out[0].event.kind, ClapNoteEvent::Kind::NoteOn);
    EXPECT_EQ(out[0].at, 0);
    EXPECT_EQ(out[0].event.keyNumber, 64);
    EXPECT_DOUBLE_EQ(out[0].event.cents, 0.0);
    EXPECT_GT(out[0].event.velocity, 0.f);
    EXPECT_LE(out[0].event.velocity, 1.f);

    EXPECT_EQ(out[1].event.kind, ClapNoteEvent::Kind::NoteOff);
    EXPECT_EQ(out[1].at, SECOND);
    EXPECT_EQ(out[1].event.noteId, out[0].event.noteId) << "the release carries the note's own id";

    EXPECT_EQ(out[2].event.kind, ClapNoteEvent::Kind::NoteOn);
    EXPECT_EQ(out[2].event.keyNumber, 62);
    EXPECT_DOUBLE_EQ(out[2].event.cents, -21.5063) << "the whole offset, not rounded to the pitch-level grid";
    EXPECT_NE(out[2].event.noteId, out[0].event.noteId);
    EXPECT_GE(out[2].event.noteId, 0);
    EXPECT_EQ(out[3].event.noteId, out[2].event.noteId);
}

TEST(ClapSequencerTests, TwoNotesOnOneKeyAtOneMomentAreTwoNotes)
{
    ClapSequencer sequencer;
    PlaybackEventsMap events;
    events[0].push_back(exactNote(0, SECOND, levelOfKey(69), 69, 21.5063));
    events[0].push_back(exactNote(0, 2 * SECOND, levelOfKey(69), 69, -21.5063));

    const std::vector<Timed> out = run(sequencer, events);
    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(out[0].event.kind, ClapNoteEvent::Kind::NoteOn);
    EXPECT_EQ(out[1].event.kind, ClapNoteEvent::Kind::NoteOn);
    EXPECT_NE(out[0].event.noteId, out[1].event.noteId);
    EXPECT_EQ(out[0].event.keyNumber, 69);
    EXPECT_EQ(out[1].event.keyNumber, 69);
    EXPECT_NE(out[0].event.cents, out[1].event.cents);
    // Their releases are staggered and each ends its own note.
    EXPECT_EQ(out[2].at, SECOND);
    EXPECT_EQ(out[3].at, 2 * SECOND);
    const std::set<int32_t> started { out[0].event.noteId, out[1].event.noteId };
    const std::set<int32_t> ended { out[2].event.noteId, out[3].event.noteId };
    EXPECT_EQ(started, ended);
}

TEST(ClapSequencerTests, ANoteThatEndsWhereAnotherStartsIsReleasedFirst)
{
    ClapSequencer sequencer;
    PlaybackEventsMap events;
    // Added in this order: the later note first, as a rebuilt stream may hold them.
    events[SECOND].push_back(stockNote(SECOND, SECOND, levelOfKey(60)));
    events[0].push_back(stockNote(0, SECOND, levelOfKey(60)));

    const std::vector<Timed> out = run(sequencer, events);
    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(out[1].at, SECOND);
    EXPECT_EQ(out[1].event.kind, ClapNoteEvent::Kind::NoteOff) << "a receiver that knows notes by key hears the key struck again";
    EXPECT_EQ(out[1].event.noteId, out[0].event.noteId);
    EXPECT_EQ(out[2].at, SECOND);
    EXPECT_EQ(out[2].event.kind, ClapNoteEvent::Kind::NoteOn);

    // A note of no length has no start (ArrangementContext::hasStart); only
    // its release is sent, which ends nothing, so no note is left sounding.
    ClapSequencer zero;
    PlaybackEventsMap instant;
    instant[0].push_back(stockNote(0, 0, levelOfKey(72)));
    const std::vector<Timed> only = run(zero, instant);
    ASSERT_EQ(only.size(), 1u);
    EXPECT_EQ(only[0].event.kind, ClapNoteEvent::Kind::NoteOff);
}

TEST(ClapSequencerTests, AGlideBecomesPitchChangesOfTheSameNote)
{
    ClapSequencer sequencer;
    PlaybackEventsMap events;
    ExactPitch exact;
    exact.midiKey = 67;
    exact.centsOffset = 2.0;
    // One 12-TET step upward over the note: 100 cents.
    events[0].push_back(bentNote(0, SECOND, levelOfKey(67), PITCH_LEVEL_STEP, exact));

    const std::vector<Timed> out = run(sequencer, events);
    ASSERT_GE(out.size(), 4u);
    EXPECT_EQ(out.front().event.kind, ClapNoteEvent::Kind::NoteOn);
    EXPECT_EQ(out.front().event.keyNumber, 67);
    EXPECT_DOUBLE_EQ(out.front().event.cents, 2.0);
    EXPECT_EQ(out.back().event.kind, ClapNoteEvent::Kind::NoteOff);

    double previous = 2.0 - 1e-9;
    int changes = 0;
    for (size_t i = 1; i + 1 < out.size(); ++i) {
        ASSERT_EQ(out[i].event.kind, ClapNoteEvent::Kind::PitchChange);
        EXPECT_EQ(out[i].event.noteId, out.front().event.noteId);
        EXPECT_GE(out[i].event.cents, previous) << "the glide rises";
        EXPECT_LE(out[i].event.cents, 102.0 + 1e-9);
        EXPECT_LT(out[i].at, SECOND);
        previous = out[i].event.cents;
        ++changes;
    }
    EXPECT_GE(changes, 2);
    EXPECT_NEAR(previous, 102.0, 8.0) << "the last change before the release is near the glide's end: 2 + 100 cents";
}

TEST(ClapSequencerTests, NoteIdsStartAgainOnlyAfterTheSoundingNotesWereEnded)
{
    ClapSequencer sequencer;
    int flushed = 0;
    sequencer.setOnMainStreamFlushed([&flushed]() { ++flushed; });
    sequencer.setOnOffStreamFlushed([&flushed]() { ++flushed; });

    sequencer.setNextNoteIdForTesting(ClapSequencer::MAX_NOTE_ID - 1);
    EXPECT_EQ(sequencer.allocateNoteId(), ClapSequencer::MAX_NOTE_ID - 1);
    EXPECT_EQ(flushed, 0);
    EXPECT_EQ(sequencer.allocateNoteId(), ClapSequencer::MAX_NOTE_ID);
    EXPECT_EQ(flushed, 2) << "every sounding note was ended before an id can repeat";
    EXPECT_EQ(sequencer.allocateNoteId(), 0);
}

TEST(ClapSequencerTests, AKernelKeyOutsideMidiIsPassedOnForTheProjectorToRefuse)
{
    ClapSequencer sequencer;
    PlaybackEventsMap events;
    events[0].push_back(exactNote(0, SECOND, levelOfKey(127), 130, 7.5));
    events[2 * SECOND].push_back(exactNote(2 * SECOND, SECOND, levelOfKey(0), -2, -7.5));

    const std::vector<Timed> out = run(sequencer, events);
    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(out[0].event.keyNumber, 130) << "not moved to key 127 with the cents of key 130";
    EXPECT_DOUBLE_EQ(out[0].event.cents, 7.5);
    EXPECT_EQ(out[2].event.keyNumber, -2);
}
