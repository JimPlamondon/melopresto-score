/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

//! A small CLAP (CLever Audio Plug-in) bundle for the clap module's tests.
//! Its factory holds two instruments and one audio effect:
//!
//!  - "Fixture Tone" takes CLAP note events and MIDI bytes and follows the
//!    CLAP tuning note expression;
//!  - "Fixture Midi Only" takes notes only as MIDI bytes;
//!  - "Fixture Effect" is not an instrument and is never created.
//!
//! Each instrument sounds one sine wave per note, so a test can measure the
//! pitch it was asked for. It behaves in the ways a host has to respect:
//!
//!  - the MPE (MIDI Polyphonic Expression) Configuration Message takes
//!    effect only when the host serves the main-thread callback the plug-in
//!    asks for;
//!  - a process call with no frames spoils every later block (not-a-number
//!    output), as one real instrument was measured to do;
//!  - its state is opaque bytes that it refuses when they are not its own.

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>

#include <clap/clap.h>

namespace {
constexpr char TONE_ID[] = "org.melopresto.clap-fixture.tone";
constexpr char MIDI_ONLY_ID[] = "org.melopresto.clap-fixture.midi-only";
constexpr char EFFECT_ID[] = "org.melopresto.clap-fixture.effect";

const char* const INSTRUMENT_FEATURES[] = { CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr };
const char* const EFFECT_FEATURES[] = { CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, nullptr };

const clap_plugin_descriptor_t DESCRIPTORS[] = {
    { CLAP_VERSION_INIT, EFFECT_ID, "Fixture Effect", "MeloPresto Tests", "", "", "", "1.2.3", "Not an instrument", EFFECT_FEATURES },
    { CLAP_VERSION_INIT, TONE_ID, "Fixture Tone", "MeloPresto Tests", "", "", "", "1.2.3", "One sine wave per note",
      INSTRUMENT_FEATURES },
    { CLAP_VERSION_INIT, MIDI_ONLY_ID, "Fixture Midi Only", "MeloPresto Tests", "", "", "", "1.2.3",
      "One sine wave per note, notes as MIDI bytes only", INSTRUMENT_FEATURES },
};

//! Parameter ids. The first two are the plug-in's sound; the rest let a test
//! read what the plug-in observed of its host.
enum Param : clap_id {
    Level = 1,          // 0 to 2, default 1: a plain range that is not 0 to 1
    Program = 2,        // 0 to 127, stepped
    MainThreadCalls = 3,
    MpeEnabled = 4,
    ZeroFrameProcessCalls = 5,
    FlushCalls = 6,
    ProcessCalls = 7,
    EditorOpen = 8,
    EditorParentSet = 9,
    EmitNonFinite = 10, // 1: every later block is not-a-number
    FailProcess = 11,   // 1: process answers CLAP_PROCESS_ERROR
    NotesStarted = 12,
    VoicesSounding = 13,
    LastTuningDelay = 14, // samples between the last note-on and the first tuning or bend that followed it
    AudioThreadInProcess = 15,     // the host called this thread the audio thread during process
    AudioThreadOnMainThread = 16,  // ... during on_main_thread
    AudioThreadOnOtherThread = 17, // ... on a thread of the plug-in's own, which is neither
    ParamCount = 17
};

constexpr int MAX_VOICES = 64;
constexpr double TWO_PI = 6.283185307179586476925286766559;
constexpr char STATE_MAGIC[4] = { 'M', 'P', 'C', 'F' };

struct Voice {
    bool active = false;
    bool releasing = false;
    int32_t noteId = -1;
    int16_t channel = 0;
    int16_t key = 0;
    double tuningSteps = 0.0;
    double phase = 0.0;
    double gain = 0.0;
    double amplitude = 0.0;
};

struct Fixture {
    clap_plugin_t plugin {};
    const clap_host_t* host = nullptr;
    bool midiOnly = false;

    double sampleRate = 48000.0;
    bool processing = false;
    bool poisoned = false;

    Voice voices[MAX_VOICES];
    double channelBendSteps[16] = {};
    double channelBendRange[16];
    int rpnMsb[16];
    int rpnLsb[16];

    std::atomic<double> level { 1.0 };
    std::atomic<double> program { 0.0 };
    std::atomic<double> emitNonFinite { 0.0 };
    std::atomic<double> failProcess { 0.0 };

    std::atomic<int> pendingMpeMembers { -1 };
    std::atomic<bool> mpeEnabled { false };
    std::atomic<int> mainThreadCalls { 0 };
    std::atomic<int> zeroFrameProcessCalls { 0 };
    std::atomic<int> flushCalls { 0 };
    std::atomic<int> processCalls { 0 };
    std::atomic<int> notesStarted { 0 };
    std::atomic<int> voicesSounding { 0 };
    std::atomic<int> lastTuningDelay { -1 };
    std::atomic<int> audioThreadInProcess { -1 };
    std::atomic<int> audioThreadOnMainThread { -1 };
    std::atomic<int> audioThreadOnOtherThread { -1 };
    std::atomic<bool> editorOpen { false };
    std::atomic<bool> editorParentSet { false };

    int64_t samplesSinceNoteOn = -1;
    bool tuningSeenSinceNoteOn = false;

    Fixture()
    {
        for (int i = 0; i < 16; ++i) {
            channelBendRange[i] = 2.0;
            rpnMsb[i] = 127;
            rpnLsb[i] = 127;
        }
    }
};

Fixture* self(const clap_plugin_t* plugin)
{
    return static_cast<Fixture*>(plugin->plugin_data);
}

// --- sound -----------------------------------------------------------------

void noteTuningArrived(Fixture* f)
{
    if (f->samplesSinceNoteOn >= 0 && !f->tuningSeenSinceNoteOn) {
        f->tuningSeenSinceNoteOn = true;
        f->lastTuningDelay.store(static_cast<int>(f->samplesSinceNoteOn));
    }
}

void startVoice(Fixture* f, int32_t noteId, int16_t channel, int16_t key, double velocity)
{
    for (Voice& voice : f->voices) {
        if (!voice.active) {
            voice = Voice();
            voice.active = true;
            voice.noteId = noteId;
            voice.channel = channel;
            voice.key = key;
            voice.amplitude = 0.2 * velocity;
            f->notesStarted.fetch_add(1);
            f->samplesSinceNoteOn = 0;
            f->tuningSeenSinceNoteOn = false;
            return;
        }
    }
}

bool matches(const Voice& voice, int32_t noteId, int16_t channel, int16_t key)
{
    if (!voice.active || voice.releasing) {
        return false;
    }
    // Every field the host states must agree: its note id, its channel and
    // its key. A host that addresses a note by the wrong channel or key
    // reaches no voice, even with the right note id.
    if (noteId >= 0 && voice.noteId >= 0 && voice.noteId != noteId) {
        return false;
    }
    return (channel < 0 || voice.channel == channel) && (key < 0 || voice.key == key);
}

void releaseVoices(Fixture* f, int32_t noteId, int16_t channel, int16_t key)
{
    for (Voice& voice : f->voices) {
        if (matches(voice, noteId, channel, key)) {
            voice.releasing = true;
        }
    }
}

void controlChange(Fixture* f, int channel, int controller, int value)
{
    switch (controller) {
    case 101: f->rpnMsb[channel] = value;
        break;
    case 100: f->rpnLsb[channel] = value;
        break;
    case 6:
        if (f->rpnMsb[channel] == 0 && f->rpnLsb[channel] == 6) {
            // The MPE Configuration Message. It is applied on the main
            // thread, when the host serves the callback asked for here.
            f->pendingMpeMembers.store(value);
            f->host->request_callback(f->host);
        } else if (f->rpnMsb[channel] == 0 && f->rpnLsb[channel] == 0) {
            // Registered parameter 0: pitch bend sensitivity in 12-TET steps.
            f->channelBendRange[channel] = value;
        }
        break;
    case 123: releaseVoices(f, -1, static_cast<int16_t>(channel), -1);
        break;
    default:
        break;
    }
}

void handleEvent(Fixture* f, const clap_event_header_t* header)
{
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) {
        return;
    }
    switch (header->type) {
    case CLAP_EVENT_NOTE_ON: {
        if (f->midiOnly) {
            break;
        }
        const clap_event_note_t* note = reinterpret_cast<const clap_event_note_t*>(header);
        startVoice(f, note->note_id, note->channel, note->key, note->velocity);
    } break;
    case CLAP_EVENT_NOTE_OFF:
    case CLAP_EVENT_NOTE_CHOKE: {
        if (f->midiOnly) {
            break;
        }
        const clap_event_note_t* note = reinterpret_cast<const clap_event_note_t*>(header);
        releaseVoices(f, note->note_id, note->channel, note->key);
    } break;
    case CLAP_EVENT_NOTE_EXPRESSION: {
        if (f->midiOnly) {
            break;
        }
        const clap_event_note_expression_t* expression = reinterpret_cast<const clap_event_note_expression_t*>(header);
        if (expression->expression_id != CLAP_NOTE_EXPRESSION_TUNING) {
            break;
        }
        for (Voice& voice : f->voices) {
            if (matches(voice, expression->note_id, expression->channel, expression->key)) {
                voice.tuningSteps = expression->value;
            }
        }
        noteTuningArrived(f);
    } break;
    case CLAP_EVENT_MIDI: {
        const clap_event_midi_t* midi = reinterpret_cast<const clap_event_midi_t*>(header);
        const int status = midi->data[0] & 0xF0;
        const int channel = midi->data[0] & 0x0F;
        if (status == 0x90 && midi->data[2] > 0) {
            startVoice(f, -1, static_cast<int16_t>(channel), midi->data[1], midi->data[2] / 127.0);
        } else if (status == 0x80 || status == 0x90) {
            releaseVoices(f, -1, static_cast<int16_t>(channel), midi->data[1]);
        } else if (status == 0xE0) {
            const int value = midi->data[1] | (midi->data[2] << 7);
            // Until MPE is on, a bend moves the pitch by the plain MIDI range of two steps.
            const double range = f->mpeEnabled.load() ? f->channelBendRange[channel] : 2.0;
            f->channelBendSteps[channel] = value >= 8192 ? (value - 8192) / 8191.0 * range : (value - 8192) / 8192.0 * range;
            noteTuningArrived(f);
        } else if (status == 0xB0) {
            controlChange(f, channel, midi->data[1], midi->data[2]);
        }
    } break;
    case CLAP_EVENT_PARAM_VALUE: {
        const clap_event_param_value_t* param = reinterpret_cast<const clap_event_param_value_t*>(header);
        switch (param->param_id) {
        case Level: f->level.store(param->value);
            break;
        case Program: f->program.store(param->value);
            break;
        case EmitNonFinite: f->emitNonFinite.store(param->value);
            break;
        case FailProcess: f->failProcess.store(param->value);
            break;
        default:
            break;
        }
    } break;
    default:
        break;
    }
}

void render(Fixture* f, float* left, float* right, uint32_t from, uint32_t to)
{
    const double level = f->level.load();
    //! A released note fades over five milliseconds.
    const double fadeStep = 1.0 / (0.005 * f->sampleRate);
    int sounding = 0;
    for (Voice& voice : f->voices) {
        if (!voice.active) {
            continue;
        }
        const double steps = voice.key - 69 + voice.tuningSteps + f->channelBendSteps[voice.channel & 0x0F];
        const double increment = TWO_PI * 440.0 * std::pow(2.0, steps / 12.0) / f->sampleRate;
        for (uint32_t i = from; i < to; ++i) {
            if (voice.releasing) {
                voice.gain -= fadeStep;
                if (voice.gain <= 0.0) {
                    voice.active = false;
                    break;
                }
            } else if (voice.gain < 1.0) {
                voice.gain = std::fmin(1.0, voice.gain + fadeStep);
            }
            const float sample = static_cast<float>(std::sin(voice.phase) * voice.amplitude * voice.gain * level);
            left[i] += sample;
            if (right) {
                right[i] += sample;
            }
            voice.phase += increment;
            if (voice.phase > TWO_PI) {
                voice.phase -= TWO_PI;
            }
        }
        if (voice.active) {
            ++sounding;
        }
    }
    f->voicesSounding.store(sounding);
    if (f->samplesSinceNoteOn >= 0) {
        f->samplesSinceNoteOn += to - from;
    }
}

// --- plug-in ---------------------------------------------------------------

const clap_host_thread_check_t* threadCheck(Fixture* f)
{
    return static_cast<const clap_host_thread_check_t*>(f->host->get_extension(f->host, CLAP_EXT_THREAD_CHECK));
}

bool pluginInit(const clap_plugin_t*)
{
    return true;
}

void pluginDestroy(const clap_plugin_t* plugin)
{
    delete self(plugin);
}

bool pluginActivate(const clap_plugin_t* plugin, double sampleRate, uint32_t, uint32_t)
{
    self(plugin)->sampleRate = sampleRate;
    return true;
}

void pluginDeactivate(const clap_plugin_t*)
{
}

bool pluginStartProcessing(const clap_plugin_t* plugin)
{
    self(plugin)->processing = true;
    return true;
}

void pluginStopProcessing(const clap_plugin_t* plugin)
{
    self(plugin)->processing = false;
}

void pluginReset(const clap_plugin_t* plugin)
{
    for (Voice& voice : self(plugin)->voices) {
        voice.active = false;
    }
}

clap_process_status pluginProcess(const clap_plugin_t* plugin, const clap_process_t* process)
{
    Fixture* f = self(plugin);
    f->processCalls.fetch_add(1);
    if (const clap_host_thread_check_t* check = threadCheck(f)) {
        f->audioThreadInProcess.store(check->is_audio_thread(f->host) ? 1 : 0);
    }
    if (process->frames_count == 0) {
        f->zeroFrameProcessCalls.fetch_add(1);
        f->poisoned = true;
        return CLAP_PROCESS_CONTINUE;
    }
    if (process->audio_outputs_count == 0 || !process->audio_outputs[0].data32 || process->audio_outputs[0].channel_count == 0) {
        return CLAP_PROCESS_ERROR;
    }

    float* left = process->audio_outputs[0].data32[0];
    float* right = process->audio_outputs[0].channel_count > 1 ? process->audio_outputs[0].data32[1] : nullptr;
    std::memset(left, 0, sizeof(float) * process->frames_count);
    if (right) {
        std::memset(right, 0, sizeof(float) * process->frames_count);
    }

    // Sample-accurate: render up to each event's time, then apply the event.
    uint32_t rendered = 0;
    const uint32_t eventCount = process->in_events ? process->in_events->size(process->in_events) : 0;
    for (uint32_t i = 0; i < eventCount; ++i) {
        const clap_event_header_t* header = process->in_events->get(process->in_events, i);
        if (!header) {
            continue;
        }
        const uint32_t time = header->time < process->frames_count ? header->time : process->frames_count;
        if (time > rendered) {
            render(f, left, right, rendered, time);
            rendered = time;
        }
        handleEvent(f, header);
    }
    render(f, left, right, rendered, process->frames_count);

    if (f->failProcess.load() >= 0.5) {
        return CLAP_PROCESS_ERROR;
    }
    if (f->poisoned || f->emitNonFinite.load() >= 0.5) {
        for (uint32_t i = 0; i < process->frames_count; ++i) {
            left[i] = std::numeric_limits<float>::quiet_NaN();
            if (right) {
                right[i] = std::numeric_limits<float>::quiet_NaN();
            }
        }
    }
    return CLAP_PROCESS_CONTINUE;
}

void pluginOnMainThread(const clap_plugin_t* plugin)
{
    Fixture* f = self(plugin);
    f->mainThreadCalls.fetch_add(1);
    if (const clap_host_thread_check_t* check = threadCheck(f)) {
        f->audioThreadOnMainThread.store(check->is_audio_thread(f->host) ? 1 : 0);
        std::thread other([f, check]() {
            f->audioThreadOnOtherThread.store(check->is_audio_thread(f->host) ? 1 : 0);
        });
        other.join();
    }
    const int members = f->pendingMpeMembers.exchange(-1);
    if (members >= 0) {
        f->mpeEnabled.store(members > 0);
    }
}

// --- note ports, audio ports -------------------------------------------------

uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 1 : 0;
}

bool notePortsGet(const clap_plugin_t* plugin, uint32_t index, bool isInput, clap_note_port_info_t* info)
{
    if (!isInput || index != 0) {
        return false;
    }
    info->id = 0;
    if (self(plugin)->midiOnly) {
        info->supported_dialects = CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE;
        info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    } else {
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    }
    std::snprintf(info->name, sizeof(info->name), "Notes");
    return true;
}

const clap_plugin_note_ports_t NOTE_PORTS = { notePortsCount, notePortsGet };

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 0 : 1;
}

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (isInput || index != 0) {
        return false;
    }
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t AUDIO_PORTS = { audioPortsCount, audioPortsGet };

// --- parameters --------------------------------------------------------------

struct ParamDefinition {
    clap_id id;
    const char* name;
    double min;
    double max;
    double defaultValue;
    uint32_t flags;
};

const ParamDefinition PARAMS[ParamCount] = {
    { Level, "Level", 0.0, 2.0, 1.0, CLAP_PARAM_IS_AUTOMATABLE },
    { Program, "Program", 0.0, 127.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED },
    { MainThreadCalls, "MainThreadCalls", 0.0, 1e9, 0.0, CLAP_PARAM_IS_READONLY },
    { MpeEnabled, "MpeEnabled", 0.0, 1.0, 0.0, CLAP_PARAM_IS_READONLY },
    { ZeroFrameProcessCalls, "ZeroFrameProcessCalls", 0.0, 1e9, 0.0, CLAP_PARAM_IS_READONLY },
    { FlushCalls, "FlushCalls", 0.0, 1e9, 0.0, CLAP_PARAM_IS_READONLY },
    { ProcessCalls, "ProcessCalls", 0.0, 1e9, 0.0, CLAP_PARAM_IS_READONLY },
    { EditorOpen, "EditorOpen", 0.0, 1.0, 0.0, CLAP_PARAM_IS_READONLY },
    { EditorParentSet, "EditorParentSet", 0.0, 1.0, 0.0, CLAP_PARAM_IS_READONLY },
    { EmitNonFinite, "EmitNonFinite", 0.0, 1.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE },
    { FailProcess, "FailProcess", 0.0, 1.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE },
    { NotesStarted, "NotesStarted", 0.0, 1e9, 0.0, CLAP_PARAM_IS_READONLY },
    { VoicesSounding, "VoicesSounding", 0.0, 1e9, 0.0, CLAP_PARAM_IS_READONLY },
    { LastTuningDelay, "LastTuningDelay", -1.0, 1e9, -1.0, CLAP_PARAM_IS_READONLY },
    { AudioThreadInProcess, "AudioThreadInProcess", -1.0, 1.0, -1.0, CLAP_PARAM_IS_READONLY },
    { AudioThreadOnMainThread, "AudioThreadOnMainThread", -1.0, 1.0, -1.0, CLAP_PARAM_IS_READONLY },
    { AudioThreadOnOtherThread, "AudioThreadOnOtherThread", -1.0, 1.0, -1.0, CLAP_PARAM_IS_READONLY },
};

uint32_t paramsCount(const clap_plugin_t*)
{
    return ParamCount;
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (index >= ParamCount) {
        return false;
    }
    const ParamDefinition& definition = PARAMS[index];
    *info = clap_param_info_t {};
    info->id = definition.id;
    info->flags = definition.flags;
    info->cookie = nullptr;
    std::snprintf(info->name, sizeof(info->name), "%s", definition.name);
    info->min_value = definition.min;
    info->max_value = definition.max;
    info->default_value = definition.defaultValue;
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* value)
{
    Fixture* f = self(plugin);
    switch (id) {
    case Level: *value = f->level.load();
        return true;
    case Program: *value = f->program.load();
        return true;
    case MainThreadCalls: *value = f->mainThreadCalls.load();
        return true;
    case MpeEnabled: *value = f->mpeEnabled.load() ? 1.0 : 0.0;
        return true;
    case ZeroFrameProcessCalls: *value = f->zeroFrameProcessCalls.load();
        return true;
    case FlushCalls: *value = f->flushCalls.load();
        return true;
    case ProcessCalls: *value = f->processCalls.load();
        return true;
    case EditorOpen: *value = f->editorOpen.load() ? 1.0 : 0.0;
        return true;
    case EditorParentSet: *value = f->editorParentSet.load() ? 1.0 : 0.0;
        return true;
    case EmitNonFinite: *value = f->emitNonFinite.load();
        return true;
    case FailProcess: *value = f->failProcess.load();
        return true;
    case NotesStarted: *value = f->notesStarted.load();
        return true;
    case VoicesSounding: *value = f->voicesSounding.load();
        return true;
    case LastTuningDelay: *value = f->lastTuningDelay.load();
        return true;
    case AudioThreadInProcess: *value = f->audioThreadInProcess.load();
        return true;
    case AudioThreadOnMainThread: *value = f->audioThreadOnMainThread.load();
        return true;
    case AudioThreadOnOtherThread: *value = f->audioThreadOnOtherThread.load();
        return true;
    default:
        return false;
    }
}

bool paramsValueToText(const clap_plugin_t*, clap_id, double value, char* buffer, uint32_t capacity)
{
    std::snprintf(buffer, capacity, "%g", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* text, double* value)
{
    return text && std::sscanf(text, "%lf", value) == 1;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*)
{
    Fixture* f = self(plugin);
    f->flushCalls.fetch_add(1);
    const uint32_t count = in ? in->size(in) : 0;
    for (uint32_t i = 0; i < count; ++i) {
        const clap_event_header_t* header = in->get(in, i);
        if (header && header->space_id == CLAP_CORE_EVENT_SPACE_ID && header->type == CLAP_EVENT_PARAM_VALUE) {
            handleEvent(f, header);
        }
    }
}

const clap_plugin_params_t PARAMS_EXTENSION = {
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush
};

// --- state ---------------------------------------------------------------------

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    Fixture* f = self(plugin);
    char bytes[sizeof(STATE_MAGIC) + 2 * sizeof(double)];
    const double level = f->level.load();
    const double program = f->program.load();
    std::memcpy(bytes, STATE_MAGIC, sizeof(STATE_MAGIC));
    std::memcpy(bytes + sizeof(STATE_MAGIC), &level, sizeof(double));
    std::memcpy(bytes + sizeof(STATE_MAGIC) + sizeof(double), &program, sizeof(double));
    // A stream may take fewer bytes than offered; write until all are taken.
    size_t written = 0;
    while (written < sizeof(bytes)) {
        const int64_t taken = stream->write(stream, bytes + written, sizeof(bytes) - written);
        if (taken <= 0) {
            return false;
        }
        written += static_cast<size_t>(taken);
    }
    return true;
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    Fixture* f = self(plugin);
    char bytes[sizeof(STATE_MAGIC) + 2 * sizeof(double)];
    size_t got = 0;
    while (got < sizeof(bytes)) {
        const int64_t taken = stream->read(stream, bytes + got, sizeof(bytes) - got);
        if (taken <= 0) {
            return false;
        }
        got += static_cast<size_t>(taken);
    }
    if (std::memcmp(bytes, STATE_MAGIC, sizeof(STATE_MAGIC)) != 0) {
        return false;
    }
    double level = 0.0;
    double program = 0.0;
    std::memcpy(&level, bytes + sizeof(STATE_MAGIC), sizeof(double));
    std::memcpy(&program, bytes + sizeof(STATE_MAGIC) + sizeof(double), sizeof(double));
    if (!std::isfinite(level) || level < 0.0 || level > 2.0 || !std::isfinite(program)) {
        return false;
    }
    f->level.store(level);
    f->program.store(program);
    return true;
}

const clap_plugin_state_t STATE_EXTENSION = { stateSave, stateLoad };

// --- editor --------------------------------------------------------------------
// No window is made: the fixture records what the host asked of it.

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating)
{
#if defined(__APPLE__)
    const char* platformApi = CLAP_WINDOW_API_COCOA;
#elif defined(_WIN32)
    const char* platformApi = CLAP_WINDOW_API_WIN32;
#else
    const char* platformApi = CLAP_WINDOW_API_X11;
#endif
    return api && std::strcmp(api, platformApi) == 0 && !isFloating;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char**, bool*)
{
    return false;
}

bool guiCreate(const clap_plugin_t* plugin, const char* api, bool isFloating)
{
    if (!guiIsApiSupported(plugin, api, isFloating) || self(plugin)->editorOpen.load()) {
        return false;
    }
    self(plugin)->editorOpen.store(true);
    return true;
}

void guiDestroy(const clap_plugin_t* plugin)
{
    self(plugin)->editorOpen.store(false);
    self(plugin)->editorParentSet.store(false);
}

bool guiSetScale(const clap_plugin_t*, double)
{
    return false;
}

bool guiGetSize(const clap_plugin_t*, uint32_t* width, uint32_t* height)
{
    *width = 320;
    *height = 200;
    return true;
}

bool guiCanResize(const clap_plugin_t*)
{
    return false;
}

bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*)
{
    return false;
}

bool guiAdjustSize(const clap_plugin_t*, uint32_t*, uint32_t*)
{
    return false;
}

bool guiSetSize(const clap_plugin_t*, uint32_t, uint32_t)
{
    return false;
}

bool guiSetParent(const clap_plugin_t* plugin, const clap_window_t* window)
{
    if (!window || !window->ptr || !self(plugin)->editorOpen.load()) {
        return false;
    }
    self(plugin)->editorParentSet.store(true);
    return true;
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*)
{
    return false;
}

void guiSuggestTitle(const clap_plugin_t*, const char*)
{
}

bool guiShow(const clap_plugin_t* plugin)
{
    return self(plugin)->editorOpen.load();
}

bool guiHide(const clap_plugin_t* plugin)
{
    return self(plugin)->editorOpen.load();
}

const clap_plugin_gui_t GUI_EXTENSION = {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale, guiGetSize, guiCanResize, guiGetResizeHints,
    guiAdjustSize, guiSetSize, guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide
};

const void* pluginGetExtension(const clap_plugin_t* plugin, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) {
        return &NOTE_PORTS;
    }
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        return &AUDIO_PORTS;
    }
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return &PARAMS_EXTENSION;
    }
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) {
        return &STATE_EXTENSION;
    }
    // Only "Fixture Tone" has an editor.
    if (std::strcmp(id, CLAP_EXT_GUI) == 0 && !self(plugin)->midiOnly) {
        return &GUI_EXTENSION;
    }
    return nullptr;
}

// --- factory, entry ------------------------------------------------------------

uint32_t factoryCount(const clap_plugin_factory_t*)
{
    return sizeof(DESCRIPTORS) / sizeof(DESCRIPTORS[0]);
}

const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory_t*, uint32_t index)
{
    return index < factoryCount(nullptr) ? &DESCRIPTORS[index] : nullptr;
}

const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
{
    if (!host || !pluginId || !clap_version_is_compatible(host->clap_version)) {
        return nullptr;
    }
    const clap_plugin_descriptor_t* descriptor = nullptr;
    bool midiOnly = false;
    if (std::strcmp(pluginId, TONE_ID) == 0) {
        descriptor = &DESCRIPTORS[1];
    } else if (std::strcmp(pluginId, MIDI_ONLY_ID) == 0) {
        descriptor = &DESCRIPTORS[2];
        midiOnly = true;
    } else {
        return nullptr;
    }

    Fixture* f = new Fixture();
    f->host = host;
    f->midiOnly = midiOnly;
    f->plugin.desc = descriptor;
    f->plugin.plugin_data = f;
    f->plugin.init = pluginInit;
    f->plugin.destroy = pluginDestroy;
    f->plugin.activate = pluginActivate;
    f->plugin.deactivate = pluginDeactivate;
    f->plugin.start_processing = pluginStartProcessing;
    f->plugin.stop_processing = pluginStopProcessing;
    f->plugin.reset = pluginReset;
    f->plugin.process = pluginProcess;
    f->plugin.get_extension = pluginGetExtension;
    f->plugin.on_main_thread = pluginOnMainThread;
    return &f->plugin;
}

const clap_plugin_factory_t FACTORY = { factoryCount, factoryDescriptor, factoryCreate };

bool entryInit(const char*)
{
    return true;
}

void entryDeinit()
{
}

const void* entryGetFactory(const char* factoryId)
{
    return factoryId && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &FACTORY : nullptr;
}
}

extern "C" {
CLAP_EXPORT const clap_plugin_entry_t clap_entry = { CLAP_VERSION_INIT, entryInit, entryDeinit, entryGetFactory };
}
