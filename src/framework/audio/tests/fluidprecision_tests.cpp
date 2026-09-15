// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
// Copyright (C) 2026 Jim Plamondon
#include <gtest/gtest.h>
#include <cmath>
#include "audio/engine/internal/synthesizers/fluidsynth/fluidsequencer.h"

using namespace muse;

TEST(FluidPitchPrecision, ExactKernelPitchSurvivesTheSequencerAndNoteOff)
{
    for (const double cents : { 200.0 / 7.0, -100.0 / 7.0, -0.25, 0.25, 40.0 }) {
        SCOPED_TRACE(cents);
        audio::synth::FluidSequencer sequencer;
        mpe::PlaybackData data;
        sequencer.init(data.setupData, midi::Program { 8, 80 }, false);
        mpe::ExactPitch exact;
        exact.midiKey = 72;
        exact.centsOffset = cents;
        exact.frequencyHz = 440.0 * std::pow(2.0, (300.0 + cents) / 1200.0);
        exact.hasLattice = 1;
        exact.nPer = 2;
        exact.nGen = -2;
        const auto nominal = static_cast<mpe::pitch_level_t>(3000 + std::lround(cents / 2.0));
        data.originEvents[0].emplace_back(mpe::NoteEvent(0, 1000000, 0, 0, nominal,
                                                         mpe::dynamicLevelFromType(mpe::DynamicType::Natural), {}, 2.0, 0.f, {}, exact));
        sequencer.load(data);
        sequencer.setActive(true);
        auto sequences = sequencer.movePlaybackForward(2000000);
        int starts = 0, stops = 0;
        for (const auto& [time, events] : sequences) {
            (void)time;
            for (const auto& event : events) {
                if (const auto* note = std::get_if<audio::AudioNoteEvent>(&event)) {
                    EXPECT_TRUE(note->hasExactTuning);
                    EXPECT_NEAR(note->pitch * 100.0 + note->tuningCents, 7200.0 + cents, 0.00002);
                    note->type == audio::AudioNoteEvent::Type::NoteOn ? ++starts : ++stops;
                }
            }
        }
        EXPECT_EQ(starts, 1);
        EXPECT_EQ(stops, 1);
    }
}

TEST(FluidPitchPrecision, StockNotesKeepTheirNominalPitch)
{
    for (const auto nominal : { 3000, 3001, 2999 }) {
        SCOPED_TRACE(nominal);
        audio::synth::FluidSequencer sequencer;
        mpe::PlaybackData data;
        sequencer.init(data.setupData, midi::Program { 0, 0 }, false);
        data.originEvents[0].emplace_back(mpe::NoteEvent(0, 1000000, 0, 0, nominal,
                                                         mpe::dynamicLevelFromType(mpe::DynamicType::Natural), {}, 2.0));
        sequencer.load(data);
        sequencer.setActive(true);
        int checked = 0;
        for (const auto& [time, events] : sequencer.movePlaybackForward(2000000)) {
            (void)time;
            for (const auto& event : events) {
                if (const auto* note = std::get_if<audio::AudioNoteEvent>(&event)) {
                    EXPECT_FALSE(note->hasExactTuning);
                    EXPECT_NEAR(note->pitch * 100.0 + note->tuningCents, 7200.0 + (nominal - 3000) * 2.0, 0.00002);
                    ++checked;
                }
            }
        }
        EXPECT_EQ(checked, 2);
    }
}
