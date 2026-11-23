/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "vstpernotepitchadapter.h"

#include "melo_realtime_projector.h"

using namespace muse::vst;

namespace {
//! A projector voice token is never zero; a VST3 note id may be.
uint64_t voiceOf(const int32_t noteId)
{
    return static_cast<uint64_t>(noteId) + 1;
}

MeloRealtimeProjector* createProjector(const VstPerNotePitchConfig& config, const bool withProfiles, MeloProjectorPlan& plan,
                                       std::string& problem)
{
    //! What this host observed about the plug-in; the selection is the shared session's.
    MeloProjectorPluginReport report {};
    report.format = MELO_PROJECTOR_FORMAT_VST3;
    report.vst3_lists_tuning_expression = config.advertisesTuningExpression ? 1u : 0u;
    report.vst3_maps_midi_pitch_bend = config.midiParams[1][Steinberg::Vst::kPitchBend] != Steinberg::Vst::kNoParamId ? 1u : 0u;

    char error[512] = {};
    MeloRealtimeProjector* projector = melo_realtime_projector_create(
        withProfiles && !config.profilesFolder.empty() ? config.profilesFolder.c_str() : nullptr,
        config.pluginName.c_str(), config.pluginVersion.c_str(), &report,
        &plan, error, sizeof(error));
    if (!projector) {
        problem = error;
    }
    return projector;
}
}

VstPerNotePitchConfig::VstPerNotePitchConfig()
{
    for (auto& channel : midiParams) {
        channel.fill(Steinberg::Vst::kNoParamId);
    }
}

VstPerNotePitchAdapter::~VstPerNotePitchAdapter()
{
    release();
}

void VstPerNotePitchAdapter::release()
{
    melo_realtime_projector_destroy(m_projector);
    m_projector = nullptr;
}

std::string VstPerNotePitchAdapter::configure(const VstPerNotePitchConfig& config)
{
    release();
    m_config = config;
    m_mode = Mode::KeyNumberOnly;
    m_profileId.clear();

    std::string problem;
    MeloProjectorPlan plan {};
    m_projector = createProjector(config, true, plan, problem);
    if (!m_projector) {
        // With no readable profiles, the plug-in's own report still decides.
        std::string ignored;
        m_projector = createProjector(config, false, plan, ignored);
        if (!m_projector) {
            return problem;
        }
    }

    switch (plan.mode) {
    case MELO_PROJECTOR_MODE_VST3_TUNING_EXPRESSION: m_mode = Mode::TuningExpression;
        break;
    case MELO_PROJECTOR_MODE_VST3_NOTE_ON_TUNING: m_mode = Mode::NoteOnTuning;
        break;
    case MELO_PROJECTOR_MODE_MPE: m_mode = Mode::Mpe;
        break;
    default: m_mode = Mode::KeyNumberOnly;
        break;
    }
    m_profileId = plan.profile_id;
    return problem;
}

void VstPerNotePitchAdapter::reset()
{
    if (!m_projector) {
        return;
    }
    MeloProjectorInput input {};
    input.kind = MELO_PROJECTOR_INPUT_RESET;
    uint32_t count = 0;
    melo_realtime_projector_project(m_projector, &input, nullptr, 0, &count);
}

VstPerNotePitchAdapter::Mode VstPerNotePitchAdapter::mode() const
{
    return m_mode;
}

const std::string& VstPerNotePitchAdapter::profileId() const
{
    return m_profileId;
}

PluginParamId VstPerNotePitchAdapter::midiParam(const int16_t channel, const int16_t controller) const
{
    if (channel < 0 || channel >= 16 || controller < 0 || static_cast<size_t>(controller) >= VST_MIDI_CONTROLLER_COUNT) {
        return Steinberg::Vst::kNoParamId;
    }
    return m_config.midiParams[channel][controller];
}

VstPerNotePitchSetup VstPerNotePitchAdapter::setup()
{
    VstPerNotePitchSetup result;
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
    for (uint32_t i = 0; i < count; ++i) {
        const PluginParamId param = midiParam(records[i].channel, records[i].controller);
        if (records[i].kind == MELO_PROJECTOR_RECORD_MIDI_CONTROLLER_WRITE && param != Steinberg::Vst::kNoParamId) {
            result.params[result.count++] = ParamChangeEvent { param, records[i].value };
        }
    }
    return result;
}

VstPerNotePitchOutput VstPerNotePitchAdapter::adapt(const VstEvent& event)
{
    VstPerNotePitchOutput output;
    output.events[0] = event;
    output.eventCount = 1;

    const bool noteOn = event.type == VstEvent::kNoteOnEvent && event.noteOn.noteId >= 0;
    const bool noteOff = event.type == VstEvent::kNoteOffEvent && event.noteOff.noteId >= 0;
    if (!m_projector || (!noteOn && !noteOff)) {
        return output;
    }

    MeloProjectorInput input {};
    if (noteOn) {
        input.kind = MELO_PROJECTOR_INPUT_NOTE_START;
        input.voice = voiceOf(event.noteOn.noteId);
        input.key_number = event.noteOn.pitch;
        input.cents = event.noteOn.tuning;
        input.strength = event.noteOn.velocity;
    } else {
        input.kind = MELO_PROJECTOR_INPUT_NOTE_END;
        input.voice = voiceOf(event.noteOff.noteId);
    }

    std::array<MeloProjectorRecord, 4> records {};
    uint32_t count = 0;
    const int32_t status = melo_realtime_projector_project(m_projector, &input, records.data(), 4, &count);
    if (status != MELO_PROJECTOR_OK) {
        // A note the projector cannot place is left out whole. A release the
        // projector does not know (a note from before a reset) passes as it is,
        // unless it belongs to a note that was left out.
        if (noteOn || status != MELO_PROJECTOR_VOICE_UNKNOWN || m_mode == Mode::Mpe) {
            output.eventCount = 0;
        }
        return output;
    }

    output.eventCount = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const MeloProjectorRecord& record = records[i];
        switch (record.kind) {
        case MELO_PROJECTOR_RECORD_NOTE_ON: {
            VstEvent on = event;
            on.noteOn.channel = record.channel;
            on.noteOn.tuning = record.tuning_cents;
            output.events[output.eventCount++] = on;
        } break;
        case MELO_PROJECTOR_RECORD_NOTE_OFF: {
            VstEvent off = event;
            off.noteOff.channel = record.channel;
            off.noteOff.tuning = 0.f;
            output.events[output.eventCount++] = off;
        } break;
        case MELO_PROJECTOR_RECORD_TUNING_EXPRESSION: {
            VstEvent expression {};
            expression.busIndex = event.busIndex;
            expression.sampleOffset = event.sampleOffset;
            expression.ppqPosition = event.ppqPosition;
            expression.flags = event.flags;
            expression.type = VstEvent::kNoteExpressionValueEvent;
            expression.noteExpressionValue.typeId = Steinberg::Vst::kTuningTypeID;
            expression.noteExpressionValue.noteId = event.noteOn.noteId;
            expression.noteExpressionValue.value = record.value;
            output.events[output.eventCount++] = expression;
        } break;
        case MELO_PROJECTOR_RECORD_MIDI_CONTROLLER_WRITE: {
            const PluginParamId param = midiParam(record.channel, record.controller);
            if (param != Steinberg::Vst::kNoParamId && output.paramCount < output.params.size()) {
                output.params[output.paramCount++] = VstPerNotePitchParam { ParamChangeEvent { param, record.value },
                                                                            record.after_note_on != 0 };
            }
        } break;
        default:
            break;
        }
    }
    return output;
}
