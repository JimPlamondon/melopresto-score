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

#include <atomic>
#include <cstdint>
#include <vector>

#include "../claptypes.h"
#include "clapeventlist.h"

namespace muse::clap {
class ClapHostedPlugin;

//! Why the audio path of one plug-in stopped, or what it had to leave out.
enum class ClapAudioFault {
    None = 0,
    //! More events than one block can carry. The path has stopped.
    EventOverflow,
    //! The plug-in answered CLAP_PROCESS_ERROR. The path has stopped.
    ProcessError,
    //! The plug-in wrote audio that is not a finite number. The path has stopped.
    NonFiniteAudio,
    //! The plug-in refused start_processing. Silent until it is activated again.
    StartRefused,
    //! The plug-in was not activated again in time after a new sample rate.
    RestartTimedOut,
    //! Events arrived while the plug-in was not activated and were left out.
    EventsWhileInactive,
    //! A projected record had no CLAP form on the selected transport.
    UndeliverableRecord,
    //! The shared projector refused a note (for example no free MPE channel).
    NoteRefused,
    //! No pitch path could be prepared; the instrument is silent.
    NoPitchPath
};

const char* clapAudioFaultText(ClapAudioFault fault);

//! Hands events and audio buffers to one activated plug-in. Every buffer is
//! allocated in init(), off the audio thread; the other functions run on the
//! audio thread, allocate nothing and take no lock.
//!
//! A fault that stops the path (an event overflow, a processing error, audio
//! that is not a finite number) stops it until the plug-in is next activated:
//! a new sample rate, or a restart the plug-in asked for. That activation
//! clears the fault; nothing else does. A stopped path still answers the
//! main thread's request to stop processing, so the restart can happen.
//!
//! A process call always carries at least one frame. Events that arrive with
//! no frames wait for the next block, or, if they are all parameter values,
//! go through the plug-in's parameter flush.
class ClapAudioClient
{
public:
    ClapAudioClient() = default;
    ClapAudioClient(const ClapAudioClient&) = delete;
    ClapAudioClient& operator=(const ClapAudioClient&) = delete;

    //! Not on the audio thread.
    void init(ClapHostedPlugin* plugin, uint32_t hostChannels);
    bool isInited() const;

    //! Queues an event for frame \p time of the next block.
    bool pushEvent(const ClapEventStorage& event, uint32_t time);
    uint32_t pendingEvents() const;

    //! Renders \p frames into \p interleaved (hostChannels values a frame).
    //! Returns the frames the plug-in rendered; the rest is silence.
    uint32_t process(float* interleaved, uint32_t frames);
    //! Delivers \p events, and only them, in a block whose output is not
    //! kept; what is queued stays queued. False, with nothing queued, when
    //! the plug-in cannot process now.
    bool deliverInDiscardedBlock(const ClapEventStorage* events, size_t count, uint32_t frames);
    //! The plug-in asked for a parameter flush: flushes now, with whatever
    //! parameter values are pending. Audio thread, between blocks. False if
    //! the plug-in cannot be flushed now.
    bool flushRequested();
    //! Delivers pending parameter values with no audio. False if anything else is pending.
    bool flushParameters();

    void setVolumeGain(float gain);

    //! Stops processing at once; for the audio thread when no block is being rendered.
    void stopNow();

    //! The first fault not yet taken; None if there is none.
    ClapAudioFault takeFault();
    bool isStopped() const;

private:
    void raise(ClapAudioFault fault, bool stopsThePath);
    bool stoppedNow();
    uint32_t renderChunk(uint32_t frames, bool takePending);

    ClapHostedPlugin* m_plugin = nullptr;
    uint32_t m_hostChannels = 2;

    ClapInputEvents m_pending;
    ClapInputEvents m_current;
    ClapOutputEvents m_out;

    std::vector<std::vector<float> > m_inputStorage;
    std::vector<std::vector<float> > m_outputStorage;
    std::vector<std::vector<float*> > m_inputChannels;
    std::vector<std::vector<float*> > m_outputChannels;
    std::vector<clap_audio_buffer_t> m_inputs;
    std::vector<clap_audio_buffer_t> m_outputs;

    float m_gain = 1.f;
    int64_t m_steadyTime = 0;
    bool m_stopped = false;
    //! The activation during which the path was stopped.
    uint32_t m_stoppedAtActivation = 0;
    //! Said once for each time the plug-in is not processing, not once a block.
    bool m_notProcessingReported = false;
    std::atomic<int> m_fault { 0 };
};
}
