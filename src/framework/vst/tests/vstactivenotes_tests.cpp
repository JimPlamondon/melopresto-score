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

// MeloSynth VST3 workstream: the host's active-note record is keyed by the
// VST3 note identifier, so two overlapping unisons (same pitch, distinct
// ids) survive each other's Note Off and a flush releases each under its
// own id; pitch/channel identity applies only to legacy noteId = -1 events.

#include <gtest/gtest.h>

#include <vector>

#include "vst/internal/vstactivenotes.h"

using namespace muse::vst;

namespace {
VstEvent noteOn(int32_t id, int pitch, int channel = 0, float tuning = 0.f)
{
    VstEvent e {};
    e.type = VstEvent::kNoteOnEvent;
    e.noteOn.noteId = id;
    e.noteOn.pitch = pitch;
    e.noteOn.channel = channel;
    e.noteOn.tuning = tuning;
    e.noteOn.velocity = 0.8f;
    return e;
}

VstEvent noteOff(int32_t id, int pitch, int channel = 0)
{
    VstEvent e {};
    e.type = VstEvent::kNoteOffEvent;
    e.noteOff.noteId = id;
    e.noteOff.pitch = pitch;
    e.noteOff.channel = channel;
    return e;
}
}

TEST(Vst_ActiveNotesTests, overlappingUnisonsWithDistinctIdsAreTrackedSeparately)
{
    VstActiveNotes notes;
    notes.noteOn(noteOn(1, 60));
    notes.noteOn(noteOn(2, 60)); // same pitch, its own id
    EXPECT_EQ(notes.size(), 2u);
    notes.noteOff(noteOff(1, 60));
    EXPECT_EQ(notes.size(), 1u);
    EXPECT_TRUE(notes.contains(2));
    EXPECT_FALSE(notes.contains(1));
}

TEST(Vst_ActiveNotesTests, flushReleasesEveryNoteUnderItsOwnIdWithItsOwnTuning)
{
    VstActiveNotes notes;
    notes.noteOn(noteOn(10, 60, 0, 12.5f));
    notes.noteOn(noteOn(11, 60, 0, -7.25f));
    std::vector<VstEvent> offs;
    notes.flush([&offs](const VstEvent& e) { offs.push_back(e); });
    ASSERT_EQ(offs.size(), 2u);
    for (const VstEvent& e : offs) {
        EXPECT_EQ(e.type, VstEvent::kNoteOffEvent);
        EXPECT_TRUE(e.noteOff.noteId == 10 || e.noteOff.noteId == 11);
        EXPECT_EQ(e.noteOff.pitch, 60);
        EXPECT_FLOAT_EQ(e.noteOff.tuning, e.noteOff.noteId == 10 ? 12.5f : -7.25f);
    }
    EXPECT_TRUE(notes.empty());
}

TEST(Vst_ActiveNotesTests, legacyNegativeIdsFallBackToPitchChannelIdentity)
{
    VstActiveNotes notes;
    notes.noteOn(noteOn(-1, 64, 2));
    notes.noteOn(noteOn(-1, 64, 2)); // same legacy identity: replaces
    EXPECT_EQ(notes.size(), 1u);
    notes.noteOff(noteOff(-1, 64, 2));
    EXPECT_TRUE(notes.empty());
    // Legacy keys never collide with real ids.
    EXPECT_NE(VstActiveNotes::keyOf(-1, 64, 2), VstActiveNotes::keyOf(64, 64, 2));
    EXPECT_GE(VstActiveNotes::keyOf(-1, 64, 2), VstActiveNotes::LEGACY_KEY_BASE);
}
