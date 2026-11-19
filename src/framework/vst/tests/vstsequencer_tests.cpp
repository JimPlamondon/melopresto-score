/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
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

// MeloSynth VST3 workstream (2026-08-19): the VST sequencer
//  - gives every note its own nonnegative 31-bit VST3 note identifier from a
//    monotonic allocator, shared by its Note On, Note Off and every per-note
//    expression event (no new event carries -1);
//  - uses a lattice-identified JiMS note's exact pitch (Kernel key + full
//    residual cents) for its Note On, and the stock construction otherwise;
//  - sends a note's pitch curve as kTuningTypeID note expressions paired to
//    its id when the plug-in advertises the standard tuning expression, and
//    keeps the global pitch-bend compatibility path otherwise;
//  - sends the frozen JiMS lattice expressions only to a plug-in that
//    advertises both, once, at the Note On, after the Note On.

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <vector>

#include "mpe/events.h"
#include "vst/internal/synth/vstsequencer.h"

using namespace muse;
using namespace muse::vst;
using namespace muse::mpe;

namespace {
constexpr muse::audio::msecs_t SECOND = 1000000;
constexpr ControlIdx PITCH_BEND_IDX = static_cast<ControlIdx>(Steinberg::Vst::kPitchBend);
constexpr PluginParamId PITCH_BEND_PARAM = 777;

NoteEvent stockNote(timestamp_t at, duration_t dur, pitch_level_t level, const PitchCurve& curve = {})
{
    return NoteEvent(at, dur, 0, 0, level, 5000, ArticulationMap(), 2.0, 0.f, curve);
}

ExactPitch exactOf(double hz, int key, double cents, int nPer, int nGen)
{
    ExactPitch e;
    e.frequencyHz = hz;
    e.midiKey = key;
    e.centsOffset = cents;
    e.hasLattice = 1;
    e.nPer = nPer;
    e.nGen = nGen;
    return e;
}

NoteEvent meloNote(timestamp_t at, duration_t dur, pitch_level_t level, const ExactPitch& exact)
{
    return NoteEvent(at, dur, 0, 0, level, 5000, ArticulationMap(), 2.0, 0.f, PitchCurve(), exact);
}

/// A held note with a Multibend articulation and a two-point pitch curve
/// (0 → +bendLevels over the whole note), built from explicit contexts.
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

struct Harness {
    VstSequencer sequencer;

    /// Load `events`, initialise with `mapping` and `caps`, and return the
    /// whole main-stream sequence.
    VstSequencer::EventSequenceMap run(const PlaybackEventsMap& events, const VstNoteExpressionCapabilities& caps,
                                       ParamsMapping mapping = {}, bool useDynamicEvents = false)
    {
        PlaybackData data;
        data.originEvents = events;
        sequencer.load(data);
        sequencer.init(std::move(mapping), useDynamicEvents, caps);
        sequencer.setActive(true);
        sequencer.setPlaybackPosition(0);
        return sequencer.movePlaybackForward(100 * SECOND);
    }
};

std::vector<VstEvent> vstEventsOf(const VstSequencer::EventSequenceMap& map, uint16_t type)
{
    std::vector<VstEvent> out;
    for (const auto& pair : map) {
        for (const auto& e : pair.second) {
            if (std::holds_alternative<VstEvent>(e) && std::get<VstEvent>(e).type == type) {
                out.push_back(std::get<VstEvent>(e));
            }
        }
    }
    return out;
}

std::vector<ParamChangeEvent> paramEventsOf(const VstSequencer::EventSequenceMap& map, PluginParamId id)
{
    std::vector<ParamChangeEvent> out;
    for (const auto& pair : map) {
        for (const auto& e : pair.second) {
            if (std::holds_alternative<ParamChangeEvent>(e) && std::get<ParamChangeEvent>(e).paramId == id) {
                out.push_back(std::get<ParamChangeEvent>(e));
            }
        }
    }
    return out;
}

VstNoteExpressionCapabilities meloSynthCaps()
{
    // What MeloPresto Synth advertises: standard tuning + both lattice types over
    // the Kernel-declared domains (nPer [-64, +63], nGen [-17, +17]).
    VstNoteExpressionCapabilities c;
    c.tuning = true;
    c.meloLattice = true;
    c.nPerMin = -64;
    c.nPerStepCount = 127;
    c.nGenMin = -17;
    c.nGenStepCount = 34;
    return c;
}

DynamicTonalityProfileEvent profileEvent(double marker)
{
    DynamicTonalityProfileEvent profile;
    for (size_t i = 0; i < profile.points.size(); ++i) {
        profile.points[i] = { 0x4A500100u + static_cast<uint32_t>(i), marker };
    }
    return profile;
}
}

TEST(Vst_SequencerTests, profileChangesPrecedeNotesAndIdenticalConsecutiveProfilesAreSuppressed)
{
    PlaybackEventsMap events;
    const DynamicTonalityProfileEvent firstProfile = profileEvent(0.25);
    const DynamicTonalityProfileEvent secondProfile = profileEvent(0.75);
    events[0].push_back(firstProfile);
    events[0].push_back(stockNote(0, SECOND, 2400));
    events[0].push_back(firstProfile);
    events[SECOND].push_back(secondProfile);
    events[SECOND].push_back(stockNote(SECOND, SECOND, 2600));

    Harness h;
    const VstSequencer::EventSequenceMap sequence = h.run(events, VstNoteExpressionCapabilities());
    ASSERT_EQ(sequence.size(), 3u);

    const auto& first = sequence.at(0);
    ASSERT_EQ(first.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<DynamicTonalityProfileEvent>(first[0]));
    EXPECT_TRUE(std::holds_alternative<VstEvent>(first[1]));
    EXPECT_EQ(std::get<DynamicTonalityProfileEvent>(first[0]), firstProfile);
    EXPECT_EQ(std::get<VstEvent>(first[1]).type, VstEvent::kNoteOnEvent);

    const auto& changed = sequence.at(SECOND);
    ASSERT_EQ(changed.size(), 3u);
    EXPECT_TRUE(std::holds_alternative<VstEvent>(changed[0]));
    EXPECT_TRUE(std::holds_alternative<DynamicTonalityProfileEvent>(changed[1]));
    EXPECT_EQ(std::get<VstEvent>(changed[0]).type, VstEvent::kNoteOffEvent);
    EXPECT_EQ(std::get<DynamicTonalityProfileEvent>(changed[1]), secondProfile);
    EXPECT_EQ(std::get<VstEvent>(changed[2]).type, VstEvent::kNoteOnEvent);
}

// ---- unique note identity ----------------------------------------------------

TEST(Vst_SequencerTests, everyNoteGetsItsOwnNonnegativeIdSharedByOnAndOff)
{
    // A chord, a repeated pitch and an overlapping unison: six notes, six ids.
    PlaybackEventsMap events;
    events[0].push_back(stockNote(0, SECOND, 2400));   // chord ...
    events[0].push_back(stockNote(0, SECOND, 2600));
    events[0].push_back(stockNote(0, SECOND, 2400));   // ... with a unison inside it
    events[SECOND].push_back(stockNote(SECOND, SECOND, 2400)); // repeated pitch
    events[SECOND / 2].push_back(stockNote(SECOND / 2, SECOND, 2400)); // overlapping unison
    events[3 * SECOND].push_back(stockNote(3 * SECOND, SECOND, 3000));

    Harness h;
    VstSequencer::EventSequenceMap seq = h.run(events, VstNoteExpressionCapabilities());

    std::vector<VstEvent> ons = vstEventsOf(seq, VstEvent::kNoteOnEvent);
    std::vector<VstEvent> offs = vstEventsOf(seq, VstEvent::kNoteOffEvent);
    ASSERT_EQ(ons.size(), 6u);
    ASSERT_EQ(offs.size(), 6u);
    std::set<int32_t> ids;
    for (const VstEvent& e : ons) {
        EXPECT_GE(e.noteOn.noteId, 0) << "no new event carries -1";
        ids.insert(e.noteOn.noteId);
    }
    EXPECT_EQ(ids.size(), 6u) << "every simultaneous or repeated note has a distinct id";
    for (const VstEvent& off : offs) {
        EXPECT_GE(off.noteOff.noteId, 0);
        EXPECT_TRUE(ids.count(off.noteOff.noteId)) << "each Note Off pairs with a Note On id";
    }
    // Off id matches the On of the same pitch/time: check the overlapping unison pair.
    std::map<int32_t, int> onPitch, offPitch;
    for (const VstEvent& e : ons) {
        onPitch[e.noteOn.noteId] = e.noteOn.pitch;
    }
    for (const VstEvent& e : offs) {
        offPitch[e.noteOff.noteId] = e.noteOff.pitch;
    }
    EXPECT_EQ(onPitch, offPitch);
}

TEST(Vst_SequencerTests, idsAreMonotonicAcrossRebuildsAndWrapOnlyAfterFlushing)
{
    Harness h;
    int flushes = 0;
    h.sequencer.setOnMainStreamFlushed([&flushes]() { ++flushes; });
    PlaybackEventsMap events;
    events[0].push_back(stockNote(0, SECOND, 2400));
    h.run(events, VstNoteExpressionCapabilities());
    const int32_t after1 = h.sequencer.nextNoteIdForTesting();
    EXPECT_EQ(after1, 1);

    // Near the limit: the next two allocations cross the wrap; the flush
    // callback runs before ids are reused and every id stays nonnegative.
    h.sequencer.setNextNoteIdForTesting(VstSequencer::MAX_NOTE_ID - 1);
    const int flushesBefore = flushes;
    const int32_t a = h.sequencer.allocateNoteId();
    const int32_t b = h.sequencer.allocateNoteId();
    const int32_t c = h.sequencer.allocateNoteId();
    EXPECT_EQ(a, VstSequencer::MAX_NOTE_ID - 1);
    EXPECT_EQ(b, VstSequencer::MAX_NOTE_ID);
    EXPECT_EQ(c, 0) << "wraps to 0";
    EXPECT_EQ(flushes, flushesBefore + 1) << "active notes were flushed exactly at the wrap";
    EXPECT_GE(a, 0);
    EXPECT_GE(b, 0);
    EXPECT_NE(a, b);
}

// ---- exact optional MeloPresto pitch --------------------------------------------------

TEST(Vst_SequencerTests, meloNoteOnUsesTheExactKernelKeyAndFullResidualCents)
{
    // A note 33.3333 cents above A4: the pitch level grid would round to
    // 34 cents (2-cent steps); the exact field keeps 33.3333.
    const double cents = 33.3333;
    ExactPitch exact = exactOf(440.0 * std::pow(2.0, cents / 1200.0), 69, cents, 0, 3);
    PlaybackEventsMap events;
    events[0].push_back(meloNote(0, SECOND, 69 * 50 - 600 + 17, exact)); // level ≈ A4 + 34 cents (2-cent grid)

    Harness h;
    VstSequencer::EventSequenceMap seq = h.run(events, meloSynthCaps());
    std::vector<VstEvent> ons = vstEventsOf(seq, VstEvent::kNoteOnEvent);
    ASSERT_EQ(ons.size(), 1u);
    EXPECT_EQ(ons[0].noteOn.pitch, 69);
    EXPECT_NEAR(ons[0].noteOn.tuning, 33.3333f, 1e-4f) << "full residual cents, not the 2-cent grid";
    // Transport precision: key + cents recover the frequency within 0.01 cent.
    const double hz = 440.0 * std::pow(2.0, (ons[0].noteOn.pitch - 69 + ons[0].noteOn.tuning / 100.0) / 12.0);
    EXPECT_NEAR(1200.0 * std::log2(hz / exact.frequencyHz), 0.0, 0.01);
    std::vector<VstEvent> offs = vstEventsOf(seq, VstEvent::kNoteOffEvent);
    ASSERT_EQ(offs.size(), 1u);
    EXPECT_EQ(offs[0].noteOff.noteId, ons[0].noteOn.noteId);
    EXPECT_EQ(offs[0].noteOff.pitch, 69);
}

TEST(Vst_SequencerTests, stockNotesKeepTheStockPitchConstruction)
{
    // Same pitch level without the exact field → the stock index/tuning.
    PlaybackEventsMap events;
    events[0].push_back(stockNote(0, SECOND, 69 * 50 - 600 + 17));
    Harness h;
    VstSequencer::EventSequenceMap seq = h.run(events, meloSynthCaps());
    std::vector<VstEvent> ons = vstEventsOf(seq, VstEvent::kNoteOnEvent);
    ASSERT_EQ(ons.size(), 1u);
    // Stock: noteIndex = 12 + level/50 (float, clamped) → 69.34 → 69; tuning = 34 cents.
    EXPECT_EQ(ons[0].noteOn.pitch, 69);
    EXPECT_NEAR(ons[0].noteOn.tuning, 34.0f, 1e-4f);
}

// ---- lattice identity, capability-gated ------------------------------------------

TEST(Vst_SequencerTests, latticeIdentityIsSentOnceAfterNoteOnOnlyToAPlugInThatAdvertisesBothTypes)
{
    ExactPitch exact = exactOf(293.664768, 62, 0.0, 1, -4);
    PlaybackEventsMap events;
    events[0].push_back(meloNote(0, SECOND, 62 * 50 - 600, exact));

    // MeloPresto Synth: both types advertised → nPer/nGen once, same id, after the Note On.
    {
        Harness h;
        VstSequencer::EventSequenceMap seq = h.run(events, meloSynthCaps());
        const auto& first = seq.begin()->second;
        ASSERT_GE(first.size(), 3u);
        ASSERT_TRUE(std::holds_alternative<VstEvent>(first[0]));
        const VstEvent& on = std::get<VstEvent>(first[0]);
        ASSERT_EQ(on.type, VstEvent::kNoteOnEvent);
        const VstEvent& nper = std::get<VstEvent>(first[1]);
        const VstEvent& ngen = std::get<VstEvent>(first[2]);
        EXPECT_EQ(nper.type, VstEvent::kNoteExpressionValueEvent);
        EXPECT_EQ(nper.noteExpressionValue.typeId, MELO_NOTE_EXPRESSION_NPER);
        EXPECT_EQ(nper.noteExpressionValue.noteId, on.noteOn.noteId);
        EXPECT_DOUBLE_EQ(nper.noteExpressionValue.value, (1.0 - (-64.0)) / 127.0);
        EXPECT_EQ(ngen.noteExpressionValue.typeId, MELO_NOTE_EXPRESSION_NGEN);
        EXPECT_EQ(ngen.noteExpressionValue.noteId, on.noteOn.noteId);
        EXPECT_DOUBLE_EQ(ngen.noteExpressionValue.value, (-4.0 - (-17.0)) / 34.0);
        EXPECT_EQ(vstEventsOf(seq, VstEvent::kNoteExpressionValueEvent).size(), 2u) << "exactly once";
    }
    // Only one of the two types advertised → no lattice at all.
    {
        VstNoteExpressionCapabilities partial = meloSynthCaps();
        partial.meloLattice = false;
        Harness h;
        VstSequencer::EventSequenceMap seq = h.run(events, partial);
        EXPECT_TRUE(vstEventsOf(seq, VstEvent::kNoteExpressionValueEvent).empty());
    }
    // An ordinary plug-in → only standard events.
    {
        Harness h;
        VstSequencer::EventSequenceMap seq = h.run(events, VstNoteExpressionCapabilities());
        EXPECT_TRUE(vstEventsOf(seq, VstEvent::kNoteExpressionValueEvent).empty());
        EXPECT_EQ(vstEventsOf(seq, VstEvent::kNoteOnEvent).size(), 1u);
    }
    // A coordinate outside the plug-in's declared domain → explicit fallback: no identity.
    {
        ExactPitch far = exactOf(16.0, 12, 0.0, -70, 0);
        PlaybackEventsMap farEvents;
        farEvents[0].push_back(meloNote(0, SECOND, 0, far));
        Harness h;
        VstSequencer::EventSequenceMap seq = h.run(farEvents, meloSynthCaps());
        EXPECT_TRUE(vstEventsOf(seq, VstEvent::kNoteExpressionValueEvent).empty());
        EXPECT_EQ(vstEventsOf(seq, VstEvent::kNoteOnEvent).size(), 1u) << "the exact pitch still plays";
    }
}

// ---- per-note tuning expression vs global pitch bend ------------------------------------

TEST(Vst_SequencerTests, bendsBecomePerNoteTuningExpressionsWhenAdvertisedAndStayGlobalBendOtherwise)
{
    // A dyad: the lower note bends up by two pitch-level steps (+4 cents...
    // let us use a whole tone: 2 × 50 = 100 levels = 200 cents? PITCH_LEVEL_STEP = 50
    // per semitone → 100 levels = 2 semitones = 200 cents), the upper holds.
    const pitch_level_t bend = 2 * PITCH_LEVEL_STEP; // +200 cents
    PlaybackEventsMap events;
    events[0].push_back(bentNote(0, SECOND, 2400, bend));
    events[0].push_back(stockNote(0, SECOND, 2800));

    // Plug-in WITHOUT the standard tuning expression (stock path): the bend
    // rides the global pitch-bend parameter — both notes would move. This is
    // the pre-change behaviour, kept for stock notation.
    {
        ParamsMapping mapping;
        mapping.emplace(PITCH_BEND_IDX, PITCH_BEND_PARAM);
        Harness h;
        VstSequencer::EventSequenceMap seq = h.run(events, VstNoteExpressionCapabilities(), mapping);
        EXPECT_FALSE(paramEventsOf(seq, PITCH_BEND_PARAM).empty()) << "global bend for a plug-in without per-note tuning";
        EXPECT_TRUE(vstEventsOf(seq, VstEvent::kNoteExpressionValueEvent).empty());
    }
    // MeloPresto Synth-like plug-in: kTuningTypeID events with the bent note's id,
    // no global bend, the partner untouched.
    {
        ParamsMapping mapping;
        mapping.emplace(PITCH_BEND_IDX, PITCH_BEND_PARAM); // even if mapped, not used for this curve
        Harness h;
        VstSequencer::EventSequenceMap seq = h.run(events, meloSynthCaps(), mapping);
        EXPECT_TRUE(paramEventsOf(seq, PITCH_BEND_PARAM).empty()) << "no global pitch-bend event represents the curve";
        std::vector<VstEvent> ons = vstEventsOf(seq, VstEvent::kNoteOnEvent);
        ASSERT_EQ(ons.size(), 2u);
        int32_t bentId = -1, heldId = -1;
        for (const VstEvent& on : ons) {
            (on.noteOn.pitch == 12 + 2400 / 50 ? bentId : heldId) = on.noteOn.noteId;
        }
        ASSERT_GE(bentId, 0);
        ASSERT_GE(heldId, 0);
        std::vector<VstEvent> exprs = vstEventsOf(seq, VstEvent::kNoteExpressionValueEvent);
        ASSERT_FALSE(exprs.empty());
        double lastValue = -1.0;
        for (const VstEvent& e : exprs) {
            EXPECT_EQ(e.noteExpressionValue.typeId, Steinberg::Vst::kTuningTypeID);
            EXPECT_EQ(e.noteExpressionValue.noteId, bentId) << "every tuning event belongs to the bent note";
            EXPECT_NE(e.noteExpressionValue.noteId, heldId) << "the partner receives nothing";
            EXPECT_GE(e.noteExpressionValue.value, 0.5 - 1e-12);
            lastValue = e.noteExpressionValue.value;
        }
        // The curve ends at +200 cents: normalized 0.5 + 200/24000.
        EXPECT_NEAR(lastValue, 0.5 + 200.0 / 24000.0, 1e-3);
        // The first tuning point shares the Note On timestamp and comes after it.
        const auto& first = seq.begin()->second;
        size_t onIdx = first.size(), exprIdx = first.size();
        for (size_t i = 0; i < first.size(); ++i) {
            if (!std::holds_alternative<VstEvent>(first[i])) {
                continue;
            }
            const VstEvent& v = std::get<VstEvent>(first[i]);
            if (v.type == VstEvent::kNoteOnEvent && v.noteOn.noteId == bentId) {
                onIdx = i;
            }
            if (v.type == VstEvent::kNoteExpressionValueEvent && exprIdx == first.size()) {
                exprIdx = i;
            }
        }
        EXPECT_LT(onIdx, exprIdx) << "Note On before its expression at the same offset";
    }
}

TEST(Vst_SequencerTests, samePitchSortKeepsExpressionsBehindTheirNoteOn)
{
    // Two MeloPresto notes at the same offset, higher pitch first; the keyswitch
    // sort reorders Note Ons by pitch, and each note's identity events stay
    // with it.
    PlaybackEventsMap events;
    events[0].push_back(meloNote(0, SECOND, 3000, exactOf(440.0, 69, 0.0, 0, 0)));
    events[0].push_back(meloNote(0, SECOND, 2000, exactOf(220.0, 57, 0.0, -1, 0)));
    Harness h;
    VstSequencer::EventSequenceMap seq = h.run(events, meloSynthCaps());
    const auto& first = seq.begin()->second;
    ASSERT_EQ(first.size(), 6u);
    std::vector<int32_t> idsInOrder;
    for (const auto& e : first) {
        const VstEvent& v = std::get<VstEvent>(e);
        idsInOrder.push_back(v.type == VstEvent::kNoteOnEvent ? v.noteOn.noteId : v.noteExpressionValue.noteId);
    }
    // Lower pitch's Note On first, then its two expressions, then the other.
    EXPECT_EQ(idsInOrder[0], idsInOrder[1]);
    EXPECT_EQ(idsInOrder[1], idsInOrder[2]);
    EXPECT_EQ(idsInOrder[3], idsInOrder[4]);
    EXPECT_EQ(idsInOrder[4], idsInOrder[5]);
    EXPECT_NE(idsInOrder[0], idsInOrder[3]);
    EXPECT_EQ(std::get<VstEvent>(first[0]).noteOn.pitch, 57);
    EXPECT_EQ(std::get<VstEvent>(first[3]).noteOn.pitch, 69);
}
