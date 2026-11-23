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

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "../claptypes.h"

struct MeloRealtimeProjector;
struct MeloProjectorRecord;

namespace muse::clap {
struct ClapPerNotePitchConfig {
    //! The name and version the plug-in states in its descriptor.
    std::string pluginName;
    std::string pluginVersion;
    //! The folder of MeloPresto receiver profiles; empty for none.
    std::string profilesFolder;
    //! What the plug-in's note input port states that it takes.
    bool takesNoteEvents = false;
    bool takesMidiBytes = false;
};

struct ClapAdaptedEvent {
    //! The CLAP event, with its time left at zero for the caller to set.
    ClapEventStorage event {};
    //! Deliver one sample after the note-on of the same note; otherwise with it.
    bool afterNoteOn = false;
};

struct ClapPerNotePitchOutput {
    std::array<ClapAdaptedEvent, 4> events {};
    size_t count = 0;
    //! What the projector answered (MELO_PROJECTOR_OK is 0).
    int32_t status = 0;
    //! Records that have no CLAP form on the selected transport. Not zero is a fault.
    size_t undeliverable = 0;
};

//! The events that prepare the plug-in before any note, in order.
struct ClapPerNotePitchSetup {
    std::array<ClapEventStorage, 96> events {};
    size_t count = 0;
    size_t undeliverable = 0;
};

//! Turns a note of the score into the CLAP events that sound it at its exact
//! pitch. Which route, which transport and every value come from the
//! MeloPresto shared projector (melo_realtime_projector.h), the one every
//! MeloPresto application uses; this class carries its records as CLAP
//! events and decides nothing about pitch. MIDI bytes are formed by the
//! projector's own function, never here.
class ClapPerNotePitchAdapter
{
public:
    enum class Mode {
        None, KeyNumberOnly, TuningExpression, Mpe
    };
    enum class Transport {
        None,
        //! CLAP note events and CLAP note expressions.
        NoteEvents,
        //! Every record as a CLAP MIDI event.
        MidiBytes
    };

    //! Notes that can sound at once; more than any route of the projector holds.
    static constexpr size_t MAX_SOUNDING_NOTES = 256;

    ClapPerNotePitchAdapter() = default;
    ~ClapPerNotePitchAdapter();
    ClapPerNotePitchAdapter(const ClapPerNotePitchAdapter&) = delete;
    ClapPerNotePitchAdapter& operator=(const ClapPerNotePitchAdapter&) = delete;

    //! Not on the audio thread: reads the profiles. Returns an empty string,
    //! or what went wrong; after a failure the adapter carries no note.
    std::string configure(const ClapPerNotePitchConfig& config);
    bool isUsable() const;

    Mode mode() const;
    Transport transport() const;
    const std::string& profileId() const;

    // --- audio thread: no allocation, no lock ---
    ClapPerNotePitchSetup setup();
    ClapPerNotePitchOutput noteOn(int32_t noteId, int32_t keyNumber, double cents, float velocity);
    ClapPerNotePitchOutput noteOff(int32_t noteId);
    //! A new pitch for a note that is sounding.
    ClapPerNotePitchOutput pitchChange(int32_t noteId, double cents);
    //! Ends one sounding note, if any is left; false when none was.
    bool releaseNext(ClapPerNotePitchOutput& output);
    //! Forgets every note without ending it.
    void reset();
    size_t soundingNotes() const;

private:
    struct Sounding {
        uint64_t voice = 0; // zero: free
        //! The note id, channel and key of the note-on that was sent for this note.
        int32_t wireNoteId = -1;
        int16_t channel = 0;
        int16_t key = 0;
    };

    void release();
    Sounding* find(uint64_t voice);
    Sounding* freeSlot();
    ClapPerNotePitchOutput project(uint32_t kind, uint64_t voice, int32_t keyNumber, double cents, float velocity);
    bool toEvent(const MeloProjectorRecord& record, const Sounding* sounding, ClapEventStorage& event) const;

    MeloRealtimeProjector* m_projector = nullptr;
    Mode m_mode = Mode::None;
    Transport m_transport = Transport::None;
    std::string m_profileId;
    std::array<Sounding, MAX_SOUNDING_NOTES> m_sounding {};
    size_t m_soundingCount = 0;
};
}
