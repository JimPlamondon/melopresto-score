/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "clapaudioclient.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "claphostedplugin.h"

using namespace muse::clap;

const char* muse::clap::clapAudioFaultText(ClapAudioFault fault)
{
    switch (fault) {
    case ClapAudioFault::None: return "no fault";
    case ClapAudioFault::EventOverflow: return "more events than one block can carry; the audio path has stopped";
    case ClapAudioFault::ProcessError: return "the plug-in reported a processing error; the audio path has stopped";
    case ClapAudioFault::NonFiniteAudio: return "the plug-in wrote audio that is not a finite number; the audio path has stopped";
    case ClapAudioFault::StartRefused: return "the plug-in refused to start processing";
    case ClapAudioFault::RestartTimedOut: return "the plug-in was not activated again in time; it is silent until it is";
    case ClapAudioFault::EventsWhileInactive: return "events arrived while the plug-in was not activated and were left out";
    case ClapAudioFault::UndeliverableRecord: return "a projected record has no CLAP form on the selected transport";
    case ClapAudioFault::NoteRefused: return "the shared projector refused a note";
    case ClapAudioFault::NoPitchPath: return "no pitch path could be prepared; the instrument is silent";
    }
    return "unknown fault";
}

void ClapAudioClient::init(ClapHostedPlugin* plugin, uint32_t hostChannels)
{
    m_plugin = plugin;
    m_hostChannels = std::max<uint32_t>(hostChannels, 1);
    m_pending.clear();
    m_current.clear();
    m_stopped = false;
    m_notProcessingReported = false;
    m_fault.store(0);

    auto allocate = [](const std::vector<uint32_t>& ports, std::vector<std::vector<float> >& storage,
                       std::vector<std::vector<float*> >& channels, std::vector<clap_audio_buffer_t>& buffers) {
        storage.clear();
        channels.clear();
        buffers.clear();
        channels.resize(ports.size());
        size_t total = 0;
        for (const uint32_t count : ports) {
            total += count;
        }
        storage.reserve(total);
        for (size_t port = 0; port < ports.size(); ++port) {
            for (uint32_t channel = 0; channel < ports[port]; ++channel) {
                storage.emplace_back(CLAP_MAX_BLOCK_FRAMES, 0.f);
                channels[port].push_back(storage.back().data());
            }
            clap_audio_buffer_t buffer {};
            buffer.data32 = channels[port].empty() ? nullptr : channels[port].data();
            buffer.data64 = nullptr;
            buffer.channel_count = ports[port];
            buffer.latency = 0;
            buffer.constant_mask = 0;
            buffers.push_back(buffer);
        }
    };

    static const ClapAudioPortLayout NO_PORTS;
    const ClapAudioPortLayout& layout = plugin ? plugin->audioPorts() : NO_PORTS;
    allocate(layout.inputChannels, m_inputStorage, m_inputChannels, m_inputs);
    allocate(layout.outputChannels, m_outputStorage, m_outputChannels, m_outputs);
}

bool ClapAudioClient::isInited() const
{
    return m_plugin != nullptr;
}

void ClapAudioClient::raise(ClapAudioFault fault, bool stopsThePath)
{
    int expected = 0;
    m_fault.compare_exchange_strong(expected, static_cast<int>(fault));
    if (stopsThePath) {
        m_stopped = true;
        m_stoppedAtActivation = m_plugin ? m_plugin->activationCount() : 0;
        m_pending.clear();
    }
}

bool ClapAudioClient::stoppedNow()
{
    // A fault stops the path until the plug-in's next activation, which clears it.
    if (m_stopped && m_plugin && m_plugin->activationCount() != m_stoppedAtActivation) {
        m_stopped = false;
        m_notProcessingReported = false;
    }
    return m_stopped;
}

ClapAudioFault ClapAudioClient::takeFault()
{
    return static_cast<ClapAudioFault>(m_fault.exchange(0));
}

bool ClapAudioClient::isStopped() const
{
    return m_stopped;
}

bool ClapAudioClient::pushEvent(const ClapEventStorage& event, uint32_t time)
{
    if (stoppedNow()) {
        return false;
    }
    ClapEventStorage timed = event;
    timed.header.time = time;
    if (!m_pending.push(timed)) {
        raise(ClapAudioFault::EventOverflow, true);
        return false;
    }
    return true;
}

uint32_t ClapAudioClient::pendingEvents() const
{
    return m_pending.size();
}

void ClapAudioClient::setVolumeGain(float gain)
{
    m_gain = gain;
}

void ClapAudioClient::stopNow()
{
    if (m_plugin) {
        m_plugin->stopProcessingNow();
    }
    m_pending.clear();
}

uint32_t ClapAudioClient::renderChunk(uint32_t frames, bool takePending)
{
    if (takePending) {
        // The events of this chunk; later ones wait, moved to the next chunk's time.
        m_current.clear();
        uint32_t kept = 0;
        const uint32_t pending = m_pending.size();
        for (uint32_t i = 0; i < pending; ++i) {
            ClapEventStorage event = m_pending.at(i);
            if (event.header.time < frames) {
                m_current.push(event);
            } else {
                event.header.time -= frames;
                m_pending.replace(kept++, event);
            }
        }
        m_pending.truncate(kept);
    }

    for (std::vector<float>& channel : m_inputStorage) {
        std::memset(channel.data(), 0, sizeof(float) * frames);
    }
    for (clap_audio_buffer_t& buffer : m_outputs) {
        buffer.constant_mask = 0;
    }

    clap_process_t process {};
    process.steady_time = m_steadyTime;
    process.frames_count = frames;
    process.transport = nullptr;
    process.audio_inputs = m_inputs.empty() ? nullptr : m_inputs.data();
    process.audio_outputs = m_outputs.empty() ? nullptr : m_outputs.data();
    process.audio_inputs_count = static_cast<uint32_t>(m_inputs.size());
    process.audio_outputs_count = static_cast<uint32_t>(m_outputs.size());
    process.in_events = m_current.list();
    process.out_events = m_out.list();

    const clap_process_status status = m_plugin->process(&process);
    m_steadyTime += frames;
    if (m_out.takeParameterChanged()) {
        m_plugin->markStateChanged();
    }

    if (status == CLAP_PROCESS_ERROR) {
        raise(ClapAudioFault::ProcessError, true);
        return 0;
    }

    if (!m_outputChannels.empty()) {
        for (const float* channel : m_outputChannels.front()) {
            for (uint32_t i = 0; i < frames; ++i) {
                if (!std::isfinite(channel[i])) {
                    raise(ClapAudioFault::NonFiniteAudio, true);
                    return 0;
                }
            }
        }
    }
    return frames;
}

uint32_t ClapAudioClient::process(float* interleaved, uint32_t frames)
{
    if (!interleaved || frames == 0) {
        return 0;
    }
    std::memset(interleaved, 0, sizeof(float) * frames * m_hostChannels);

    if (!m_plugin) {
        return 0;
    }
    if (stoppedNow()) {
        // Nothing is rendered, but a request of the main thread to stop
        // processing is still answered, so that the plug-in can be restarted.
        m_plugin->prepareToProcess();
        return 0;
    }
    if (!m_plugin->prepareToProcess()) {
        const bool refused = m_plugin->audioState() == ClapHostedPlugin::AudioState::StartFailed;
        const bool leftOut = m_pending.size() > 0;
        m_pending.clear();
        if ((refused || leftOut) && !m_notProcessingReported) {
            m_notProcessingReported = true;
            raise(refused ? ClapAudioFault::StartRefused : ClapAudioFault::EventsWhileInactive, false);
        }
        return 0;
    }
    m_notProcessingReported = false;

    uint32_t done = 0;
    while (done < frames) {
        const uint32_t chunk = std::min(frames - done, CLAP_MAX_BLOCK_FRAMES);
        if (renderChunk(chunk, true) != chunk) {
            // The path has stopped: what was rendered of this block is not kept.
            std::memset(interleaved, 0, sizeof(float) * frames * m_hostChannels);
            return 0;
        }
        if (!m_outputChannels.empty() && !m_outputChannels.front().empty()) {
            const std::vector<float*>& main = m_outputChannels.front();
            float* out = interleaved + static_cast<size_t>(done) * m_hostChannels;
            for (uint32_t channel = 0; channel < m_hostChannels; ++channel) {
                // A mono instrument sounds on every channel of the mixer.
                const float* source = main[std::min<size_t>(channel, main.size() - 1)];
                for (uint32_t i = 0; i < chunk; ++i) {
                    out[static_cast<size_t>(i) * m_hostChannels + channel] = source[i] * m_gain;
                }
            }
        }
        done += chunk;
    }
    return frames;
}

bool ClapAudioClient::deliverInDiscardedBlock(const ClapEventStorage* events, size_t count, uint32_t frames)
{
    if (!m_plugin || frames == 0 || stoppedNow() || !m_plugin->prepareToProcess()) {
        return false;
    }

    m_current.clear();
    for (size_t i = 0; i < count; ++i) {
        ClapEventStorage event = events[i];
        event.header.time = 0;
        if (!m_current.push(event)) {
            raise(ClapAudioFault::EventOverflow, true);
            return false;
        }
    }

    // The events go in the first chunk; what is queued stays queued.
    uint32_t done = 0;
    while (done < frames) {
        const uint32_t chunk = std::min(frames - done, CLAP_MAX_BLOCK_FRAMES);
        if (renderChunk(chunk, false) != chunk) {
            return false;
        }
        m_current.clear();
        done += chunk;
    }
    return true;
}

bool ClapAudioClient::flushParameters()
{
    if (!m_plugin || stoppedNow() || m_pending.size() == 0) {
        return false;
    }
    for (uint32_t i = 0; i < m_pending.size(); ++i) {
        if (m_pending.at(i).header.type != CLAP_EVENT_PARAM_VALUE) {
            return false;
        }
    }
    if (!m_plugin->prepareToProcess()) {
        return false;
    }
    m_pending.moveAllToBlockStart();
    if (!m_plugin->flushParameters(m_pending.list(), m_out.list())) {
        return false;
    }
    m_pending.clear();
    if (m_out.takeParameterChanged()) {
        m_plugin->markStateChanged();
    }
    return true;
}
