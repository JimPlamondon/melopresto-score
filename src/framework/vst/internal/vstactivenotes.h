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

#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>

#include "../vsttypes.h"

namespace muse::vst {
//! The VST host's record of currently sounding notes (MeloSynth VST3
//! workstream, 2026-08-19). The primary key is the note's VST3 note
//! identifier; the pitch/channel key is kept only for genuinely legacy
//! events that carry `noteId = -1`, so overlapping unisons with distinct ids
//! never collide and a flush releases every note under its own id.
class VstActiveNotes
{
public:
    static constexpr size_t LEGACY_KEY_BASE = size_t(1) << 62;

    static size_t keyOf(const VstEvent& event)
    {
        if (event.type == VstEvent::kNoteOnEvent) {
            return keyOf(event.noteOn.noteId, event.noteOn.pitch, event.noteOn.channel);
        }
        if (event.type == VstEvent::kNoteOffEvent) {
            return keyOf(event.noteOff.noteId, event.noteOff.pitch, event.noteOff.channel);
        }
        return 0;
    }

    static size_t keyOf(int32_t noteId, int pitch, int channel)
    {
        if (noteId >= 0) {
            return static_cast<size_t>(noteId);
        }
        // Legacy identity (the pre-2026 pitch/channel hash), kept apart from
        // the id space.
        std::size_t h1 = std::hash<int> {}(pitch);
        std::size_t h2 = std::hash<int> {}(channel);
        return LEGACY_KEY_BASE | ((h1 ^ (h2 << 1)) & (LEGACY_KEY_BASE - 1));
    }

    void noteOn(const VstEvent& event)
    {
        m_notes.insert_or_assign(keyOf(event), event);
    }

    void noteOff(const VstEvent& event)
    {
        m_notes.erase(keyOf(event));
    }

    bool empty() const { return m_notes.empty(); }
    size_t size() const { return m_notes.size(); }
    void clear() { m_notes.clear(); }

    bool contains(int32_t noteId, int pitch = 0, int channel = 0) const
    {
        return m_notes.find(keyOf(noteId, pitch, channel)) != m_notes.end();
    }

    //! Build the Note Off that ends each sounding note — same id, channel,
    //! pitch and tuning as its Note On — and hand it to `out`; then forget them.
    void flush(const std::function<void(const VstEvent&)>& out)
    {
        for (const auto& pair : m_notes) {
            const VstEvent& noteOn = pair.second;

            VstEvent noteOff;
            noteOff.type = VstEvent::kNoteOffEvent;
            noteOff.ppqPosition = 0;
            noteOff.sampleOffset = 0;
            noteOff.busIndex = noteOn.busIndex;
            noteOff.flags = noteOn.flags;
            noteOff.noteOff.noteId = noteOn.noteOn.noteId;
            noteOff.noteOff.channel = noteOn.noteOn.channel;
            noteOff.noteOff.pitch = noteOn.noteOn.pitch;
            noteOff.noteOff.tuning = noteOn.noteOn.tuning;
            noteOff.noteOff.velocity = noteOn.noteOn.velocity;

            out(noteOff);
        }

        m_notes.clear();
    }

private:
    std::unordered_map<size_t, VstEvent> m_notes;
};
}
