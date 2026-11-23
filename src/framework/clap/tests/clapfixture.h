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

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "claptypes.h"

//! What the tests know of the fixture plug-in (tests/fixture/clapfixtureplugin.cpp).
namespace muse::clap::fixture {
//! The path of the built fixture bundle.
inline std::string bundlePath()
{
    return CLAP_FIXTURE_PATH;
}

inline constexpr char TONE_ID[] = "org.melopresto.clap-fixture.tone";
inline constexpr char MIDI_ONLY_ID[] = "org.melopresto.clap-fixture.midi-only";
inline constexpr char EFFECT_ID[] = "org.melopresto.clap-fixture.effect";

enum Param : uint32_t {
    Level = 1,
    Program = 2,
    MainThreadCalls = 3,
    MpeEnabled = 4,
    ZeroFrameProcessCalls = 5,
    FlushCalls = 6,
    ProcessCalls = 7,
    EditorOpen = 8,
    EditorParentSet = 9,
    EmitNonFinite = 10,
    FailProcess = 11,
    NotesStarted = 12,
    VoicesSounding = 13,
    LastTuningDelay = 14,
    AudioThreadInProcess = 15,
    AudioThreadOnMainThread = 16,
    AudioThreadOnOtherThread = 17,
    Serial = 18,
};

//! How the fixture tells its host of a change made in its editor.
enum GuiEdit : int {
    //! As a real plug-in does: request_flush, then the value on the output events.
    FlushThenReport = 0,
    //! Nothing reaches the host.
    Silent = 1,
    MarkDirty = 2,
    RescanValues = 3,
};

//! The musician changes a parameter in the editor of the fixture instance
//! with this serial (its parameter Serial). POSIX only.
void guiEdit(int serial, uint32_t paramId, double value, GuiEdit how);

//! One CLAP note event written from clap/events.h.
inline ClapEventStorage noteEvent(uint16_t type, int32_t noteId, int16_t key, double velocity)
{
    ClapEventStorage event {};
    event.note.header.size = sizeof(clap_event_note_t);
    event.note.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.note.header.type = type;
    event.note.note_id = noteId;
    event.note.port_index = 0;
    event.note.channel = 0;
    event.note.key = key;
    event.note.velocity = velocity;
    return event;
}

inline ClapEventStorage tuningEvent(int32_t noteId, int16_t key, double steps)
{
    ClapEventStorage event {};
    event.expression.header.size = sizeof(clap_event_note_expression_t);
    event.expression.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.expression.header.type = CLAP_EVENT_NOTE_EXPRESSION;
    event.expression.expression_id = CLAP_NOTE_EXPRESSION_TUNING;
    event.expression.note_id = noteId;
    event.expression.port_index = 0;
    event.expression.channel = 0;
    event.expression.key = key;
    event.expression.value = steps;
    return event;
}

inline ClapEventStorage midiEvent(uint8_t status, uint8_t first, uint8_t second)
{
    ClapEventStorage event {};
    event.midi.header.size = sizeof(clap_event_midi_t);
    event.midi.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.midi.header.type = CLAP_EVENT_MIDI;
    event.midi.port_index = 0;
    event.midi.data[0] = status;
    event.midi.data[1] = first;
    event.midi.data[2] = second;
    return event;
}

//! The frequency of a steady sine wave in one channel of interleaved audio,
//! from the time between its first and last upward zero crossings, each
//! placed between two samples by a straight line.
inline double measuredFrequency(const std::vector<float>& interleaved, uint32_t channels, double sampleRate, size_t fromFrame = 0)
{
    const size_t frames = interleaved.size() / channels;
    double first = -1.0;
    double last = -1.0;
    int crossings = 0;
    for (size_t i = fromFrame + 1; i < frames; ++i) {
        const double before = interleaved[(i - 1) * channels];
        const double after = interleaved[i * channels];
        if (before < 0.0 && after >= 0.0) {
            const double at = (i - 1) + before / (before - after);
            if (first < 0.0) {
                first = at;
            }
            last = at;
            ++crossings;
        }
    }
    if (crossings < 2) {
        return 0.0;
    }
    return (crossings - 1) * sampleRate / (last - first);
}

//! How far \p frequency is from \p reference, in cents (1200 to the octave).
inline double centsBetween(double frequency, double reference)
{
    return 1200.0 * std::log2(frequency / reference);
}

//! The frequency of a MIDI key number in 12-TET at A4 = 440 Hz, moved by \p cents.
inline double keyFrequency(int key, double cents)
{
    return 440.0 * std::pow(2.0, (key - 69 + cents / 100.0) / 12.0);
}
}
