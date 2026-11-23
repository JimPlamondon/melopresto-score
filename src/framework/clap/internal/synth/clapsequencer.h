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

#include "audio/engine/internal/abstracteventsequencer.h"

namespace muse::clap {
//! One thing a note of the score asks of the instrument. The pitch is stated
//! as every MeloPresto host states it to the shared projector: a MIDI 1.0 key
//! number and the offset from that key's 12-TET pitch in cents.
struct ClapNoteEvent {
    enum class Kind {
        NoteOn, NoteOff,
        //! A new pitch for a note that is sounding (a bend, a glide).
        PitchChange
    };

    Kind kind = Kind::NoteOn;
    //! Not negative; the note's own, shared by its start, its end and its pitch changes.
    int32_t noteId = 0;
    int32_t keyNumber = 0;
    double cents = 0.0;
    float velocity = 0.f;
};

//! Turns the playback events of a track into ClapNoteEvents in time order:
//! the counterpart of VstSequencer.
class ClapSequencer : public audio::engine::AbstractEventSequencer<ClapNoteEvent, muse::audio::gain_t>
{
public:
    void init(bool useDynamicEvents);

    muse::audio::gain_t currentGain() const;

    //! Every new note gets its own note id from this counter, which starts
    //! again from zero only after the callbacks that end all sounding notes.
    int32_t allocateNoteId();
    void setNextNoteIdForTesting(int32_t next) { m_nextNoteId = next; }
    static constexpr int32_t MAX_NOTE_ID = 0x7FFFFFF0;

private:
    void updateOffStreamEvents(const mpe::PlaybackEventsMap& events, const mpe::DynamicLevelLayers& dynamics) override;
    void updateMainStreamEvents(const mpe::PlaybackEventsMap& events, const mpe::DynamicLevelLayers& dynamics) override;

    void addPlaybackEvents(EventSequenceMap& destination, const mpe::PlaybackEventsMap& events);
    void addDynamicEvents(EventSequenceMap& destination, const mpe::DynamicLevelLayers& layers);
    void addNoteEvent(EventSequenceMap& destination, const mpe::NoteEvent& noteEvent);
    void addPitchCurve(EventSequenceMap& destination, const mpe::NoteEvent& noteEvent, const mpe::ArticulationMeta& artMeta, int32_t noteId,
                       int32_t keyNumber, double baseCents);
    void orderEventsOfOneMoment(EventSequenceMap& destination);

    int32_t keyNumberOf(const mpe::pitch_level_t pitchLevel) const;
    double centsOf(const mpe::NoteEvent& noteEvent, int32_t keyNumber) const;
    float velocityOf(const mpe::NoteEvent& noteEvent) const;
    float expressionLevel(const mpe::dynamic_level_t dynamicLevel) const;

    bool m_inited = false;
    bool m_useDynamicEvents = false;
    int32_t m_nextNoteId = 0;
};
}
