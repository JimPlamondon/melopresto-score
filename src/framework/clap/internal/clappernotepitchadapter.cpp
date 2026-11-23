/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "clappernotepitchadapter.h"

#include "melo_realtime_projector.h"

using namespace muse::clap;

namespace muse::clap {
//! A projector voice token is never zero; a note id may be.
static uint64_t clapVoiceOf(const int32_t noteId)
{
    return static_cast<uint64_t>(noteId) + 1;
}

static void clapEventHeader(clap_event_header_t& header, uint16_t type, uint32_t size)
{
    header.size = size;
    header.time = 0;
    header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    header.type = type;
    header.flags = 0;
}
}

ClapPerNotePitchAdapter::~ClapPerNotePitchAdapter()
{
    release();
}

void ClapPerNotePitchAdapter::release()
{
    melo_realtime_projector_destroy(m_projector);
    m_projector = nullptr;
    m_mode = Mode::None;
    m_transport = Transport::None;
    m_profileId.clear();
    m_sounding.fill(Sounding());
    m_soundingCount = 0;
}

std::string ClapPerNotePitchAdapter::configure(const ClapPerNotePitchConfig& config)
{
    release();

    //! What this host observed about the plug-in; the selection is the shared session's.
    MeloProjectorPluginReport report {};
    report.format = MELO_PROJECTOR_FORMAT_CLAP;
    report.clap_takes_note_events = config.takesNoteEvents ? 1u : 0u;
    report.clap_takes_midi_bytes = config.takesMidiBytes ? 1u : 0u;

    char error[512] = {};
    MeloProjectorPlan plan {};
    // One request, and its answer is final: this host tries no other route
    // and no other set of profiles when the shared selection refuses.
    m_projector = melo_realtime_projector_create(config.profilesFolder.empty() ? nullptr : config.profilesFolder.c_str(),
                                                 config.pluginName.c_str(), config.pluginVersion.c_str(), &report,
                                                 &plan, error, sizeof(error));
    if (!m_projector) {
        return error[0] ? std::string(error) : std::string("the shared projector could not be prepared");
    }

    switch (plan.mode) {
    case MELO_PROJECTOR_MODE_CLAP_TUNING_EXPRESSION: m_mode = Mode::TuningExpression;
        break;
    case MELO_PROJECTOR_MODE_MPE: m_mode = Mode::Mpe;
        break;
    case MELO_PROJECTOR_MODE_KEY_NUMBER_ONLY: m_mode = Mode::KeyNumberOnly;
        break;
    default:
        release();
        return "the shared selection named a route that a CLAP plug-in cannot be sent";
    }

    switch (plan.note_transport) {
    case MELO_PROJECTOR_TRANSPORT_NOTE_EVENTS: m_transport = Transport::NoteEvents;
        break;
    case MELO_PROJECTOR_TRANSPORT_MIDI_BYTES: m_transport = Transport::MidiBytes;
        break;
    default:
        release();
        return "the shared selection named no way to hand the notes over";
    }

    m_profileId = plan.profile_id;
    return std::string();
}

bool ClapPerNotePitchAdapter::isUsable() const
{
    return m_projector != nullptr;
}

ClapPerNotePitchAdapter::Mode ClapPerNotePitchAdapter::mode() const
{
    return m_mode;
}

ClapPerNotePitchAdapter::Transport ClapPerNotePitchAdapter::transport() const
{
    return m_transport;
}

const std::string& ClapPerNotePitchAdapter::profileId() const
{
    return m_profileId;
}

size_t ClapPerNotePitchAdapter::soundingNotes() const
{
    return m_soundingCount;
}

ClapPerNotePitchAdapter::Sounding* ClapPerNotePitchAdapter::find(uint64_t voice)
{
    for (Sounding& sounding : m_sounding) {
        if (sounding.voice == voice) {
            return &sounding;
        }
    }
    return nullptr;
}

ClapPerNotePitchAdapter::Sounding* ClapPerNotePitchAdapter::freeSlot()
{
    return find(0);
}

bool ClapPerNotePitchAdapter::toEvent(const MeloProjectorRecord& record, const Sounding* sounding, ClapEventStorage& event) const
{
    event = ClapEventStorage {};

    if (m_transport == Transport::MidiBytes) {
        // The bytes are the projector's; this host only wraps them.
        uint8_t bytes[3] = {};
        uint32_t length = 0;
        if (melo_realtime_projector_midi1(&record, bytes, &length) != MELO_PROJECTOR_OK || length == 0 || length > 3) {
            return false;
        }
        clapEventHeader(event.midi.header, CLAP_EVENT_MIDI, sizeof(clap_event_midi_t));
        event.midi.port_index = 0;
        event.midi.data[0] = bytes[0];
        event.midi.data[1] = bytes[1];
        event.midi.data[2] = bytes[2];
        return true;
    }

    if (m_transport != Transport::NoteEvents) {
        return false;
    }

    switch (record.kind) {
    case MELO_PROJECTOR_RECORD_NOTE_ON:
    case MELO_PROJECTOR_RECORD_NOTE_OFF: {
        const bool on = record.kind == MELO_PROJECTOR_RECORD_NOTE_ON;
        clapEventHeader(event.note.header, on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF, sizeof(clap_event_note_t));
        event.note.note_id = record.note_id;
        event.note.port_index = 0;
        event.note.channel = record.channel;
        event.note.key = record.key_number;
        event.note.velocity = on ? static_cast<double>(record.velocity) : 0.0;
        return true;
    }
    case MELO_PROJECTOR_RECORD_TUNING_EXPRESSION: {
        // The expression is addressed to the note id of the note-on, with
        // that note's port, channel and key.
        if (!sounding) {
            return false;
        }
        clapEventHeader(event.expression.header, CLAP_EVENT_NOTE_EXPRESSION, sizeof(clap_event_note_expression_t));
        event.expression.expression_id = CLAP_NOTE_EXPRESSION_TUNING;
        event.expression.note_id = record.note_id;
        event.expression.port_index = 0;
        event.expression.channel = sounding->channel;
        event.expression.key = sounding->key;
        // CLAP states tuning in 12-TET steps of 100 cents; the record's
        // tuning_cents is the whole offset in cents.
        event.expression.value = static_cast<double>(record.tuning_cents) / 100.0;
        return true;
    }
    default:
        // A MIDI controller write has no CLAP note-event form.
        return false;
    }
}

ClapPerNotePitchOutput ClapPerNotePitchAdapter::project(uint32_t kind, uint64_t voice, int32_t keyNumber, double cents, float velocity)
{
    ClapPerNotePitchOutput output;
    if (!m_projector) {
        output.status = MELO_PROJECTOR_INVALID_ARGUMENT;
        return output;
    }

    Sounding* sounding = find(voice);
    if (kind == MELO_PROJECTOR_INPUT_NOTE_START) {
        if (sounding) {
            output.status = MELO_PROJECTOR_VOICE_ALREADY_SOUNDING;
            return output;
        }
        sounding = freeSlot();
        if (!sounding) {
            output.status = MELO_PROJECTOR_VOICE_CAPACITY_EXHAUSTED;
            return output;
        }
    } else if (!sounding) {
        output.status = MELO_PROJECTOR_VOICE_UNKNOWN;
        return output;
    }

    MeloProjectorInput input {};
    input.kind = kind;
    input.voice = voice;
    input.key_number = keyNumber;
    input.cents = cents;
    input.strength = velocity;

    std::array<MeloProjectorRecord, 4> records {};
    uint32_t count = 0;
    output.status = melo_realtime_projector_project(m_projector, &input, records.data(), static_cast<uint32_t>(records.size()),
                                                    &count);
    if (output.status != MELO_PROJECTOR_OK) {
        return output;
    }

    if (kind == MELO_PROJECTOR_INPUT_NOTE_START) {
        sounding->voice = voice;
        sounding->wireNoteId = -1;
        sounding->channel = 0;
        sounding->key = static_cast<int16_t>(keyNumber);
        for (uint32_t i = 0; i < count; ++i) {
            if (records[i].kind == MELO_PROJECTOR_RECORD_NOTE_ON) {
                sounding->wireNoteId = records[i].note_id;
                sounding->channel = records[i].channel;
                sounding->key = records[i].key_number;
                break;
            }
        }
        ++m_soundingCount;
    }

    for (uint32_t i = 0; i < count; ++i) {
        ClapAdaptedEvent adapted;
        if (toEvent(records[i], sounding, adapted.event)) {
            adapted.afterNoteOn = records[i].after_note_on != 0;
            output.events[output.count++] = adapted;
        } else {
            ++output.undeliverable;
        }
    }

    if (kind == MELO_PROJECTOR_INPUT_NOTE_END) {
        *sounding = Sounding();
        --m_soundingCount;
    }
    return output;
}

ClapPerNotePitchOutput ClapPerNotePitchAdapter::noteOn(int32_t noteId, int32_t keyNumber, double cents, float velocity)
{
    if (noteId < 0) {
        ClapPerNotePitchOutput output;
        output.status = MELO_PROJECTOR_VOICE_TOKEN_INVALID;
        return output;
    }
    return project(MELO_PROJECTOR_INPUT_NOTE_START, clapVoiceOf(noteId), keyNumber, cents, velocity);
}

ClapPerNotePitchOutput ClapPerNotePitchAdapter::noteOff(int32_t noteId)
{
    if (noteId < 0) {
        ClapPerNotePitchOutput output;
        output.status = MELO_PROJECTOR_VOICE_TOKEN_INVALID;
        return output;
    }
    return project(MELO_PROJECTOR_INPUT_NOTE_END, clapVoiceOf(noteId), 0, 0.0, 0.f);
}

ClapPerNotePitchOutput ClapPerNotePitchAdapter::pitchChange(int32_t noteId, double cents)
{
    if (noteId < 0) {
        ClapPerNotePitchOutput output;
        output.status = MELO_PROJECTOR_VOICE_TOKEN_INVALID;
        return output;
    }
    return project(MELO_PROJECTOR_INPUT_PITCH_CHANGE, clapVoiceOf(noteId), 0, cents, 0.f);
}

bool ClapPerNotePitchAdapter::releaseNext(ClapPerNotePitchOutput& output)
{
    output = ClapPerNotePitchOutput();
    for (Sounding& sounding : m_sounding) {
        if (sounding.voice == 0) {
            continue;
        }
        output = project(MELO_PROJECTOR_INPUT_NOTE_END, sounding.voice, 0, 0.0, 0.f);
        if (output.status != MELO_PROJECTOR_OK) {
            // The projector no longer holds this note; neither does this table.
            sounding = Sounding();
            --m_soundingCount;
        }
        return true;
    }
    return false;
}

void ClapPerNotePitchAdapter::reset()
{
    m_sounding.fill(Sounding());
    m_soundingCount = 0;
    if (!m_projector) {
        return;
    }
    MeloProjectorInput input {};
    input.kind = MELO_PROJECTOR_INPUT_RESET;
    uint32_t count = 0;
    melo_realtime_projector_project(m_projector, &input, nullptr, 0, &count);
}

ClapPerNotePitchSetup ClapPerNotePitchAdapter::setup()
{
    ClapPerNotePitchSetup result;
    if (!m_projector) {
        return result;
    }
    std::array<MeloProjectorRecord, MELO_PROJECTOR_SETUP_RECORDS> records {};
    MeloProjectorInput input {};
    input.kind = MELO_PROJECTOR_INPUT_SETUP;
    uint32_t count = 0;
    if (melo_realtime_projector_project(m_projector, &input, records.data(), MELO_PROJECTOR_SETUP_RECORDS, &count)
        != MELO_PROJECTOR_OK) {
        return result;
    }
    for (uint32_t i = 0; i < count && result.count < result.events.size(); ++i) {
        if (toEvent(records[i], nullptr, result.events[result.count])) {
            ++result.count;
        } else {
            ++result.undeliverable;
        }
    }
    return result;
}
