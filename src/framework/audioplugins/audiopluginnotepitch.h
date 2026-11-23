/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "mpe/events.h"

//! How a note of the playback model is stated to a hosted plug-in, whatever
//! the plug-in's format: a MIDI 1.0 key number and the offset from that
//! key's 12-TET pitch in cents. The sequencers of the vst and clap modules
//! both take it from here, so that it is written once.
namespace muse::audioplugins {
struct NotePitch {
    //! For a note of the MeloPresto Music System this is the Kernel's key
    //! number as it is, even outside 0 to 127: the shared projector refuses
    //! such a note. It is never moved to another key with its cents kept.
    int32_t keyNumber = 0;
    double cents = 0.0;
};

//! The nearest key at or below a pitch level, as the stock sequencers count it.
inline int32_t keyNumberOfPitchLevel(const mpe::pitch_level_t pitchLevel)
{
    const float stepCount = mpe::ZERO_PITCH_LEVEL_MIDI_EQUIVALENT + pitchLevel / static_cast<float>(mpe::PITCH_LEVEL_STEP);

    return static_cast<int32_t>(std::clamp(stepCount, 0.f, 127.f));
}

//! What is left of a pitch level above (or below) a key, in cents.
inline double centsOfPitchLevel(const mpe::pitch_level_t pitchLevel, const int32_t keyNumber)
{
    const int stepCount = keyNumber - mpe::ZERO_PITCH_LEVEL_MIDI_EQUIVALENT;
    const mpe::pitch_level_t rest = pitchLevel - stepCount * mpe::PITCH_LEVEL_STEP;

    return (rest / static_cast<double>(mpe::PITCH_LEVEL_STEP)) * 100.0;
}

//! A note of the MeloPresto Music System carries the key number and the
//! offset the Kernel computed for it. Any other note is its nearest key and
//! the rest of its pitch level.
inline NotePitch notePitchOf(const mpe::NoteEvent& noteEvent)
{
    const mpe::PitchContext& pitchCtx = noteEvent.pitchCtx();
    if (pitchCtx.exactPitch.has_value()) {
        return NotePitch { pitchCtx.exactPitch->midiKey, pitchCtx.exactPitch->centsOffset };
    }

    const int32_t keyNumber = keyNumberOfPitchLevel(pitchCtx.nominalPitchLevel);
    return NotePitch { keyNumber, centsOfPitchLevel(pitchCtx.nominalPitchLevel, keyNumber) };
}

//! The offset in cents at one point of a note's pitch curve: the note's own
//! offset plus the curve's, one 12-TET step of the curve being 100 cents.
inline double pitchCurveCents(const double baseCents, const double pitchLevelOffset)
{
    return baseCents + (pitchLevelOffset / static_cast<double>(mpe::PITCH_LEVEL_STEP)) * 100.0;
}

//! How many points are laid between two points of a pitch curve.
//! NOTE: a larger weight gives fewer points.
inline size_t pitchCurvePointCount(const mpe::pitch_level_t from, const mpe::pitch_level_t to)
{
    constexpr mpe::pitch_level_t POINT_WEIGHT = mpe::PITCH_LEVEL_STEP / 25;
    const size_t pointCount = static_cast<size_t>(std::abs(to - from)) / POINT_WEIGHT;

    return std::max(pointCount, size_t(1));
}
}
