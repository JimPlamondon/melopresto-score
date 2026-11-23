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
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../claptypes.h"
#include "clapbundle.h"

namespace muse::clap {
//! What the plug-in states about its first note input port.
struct ClapNotePortReport {
    bool hasNoteInput = false;
    //! The port takes CLAP note events.
    bool takesNoteEvents = false;
    //! The port takes MIDI 1.0 bytes (plain MIDI or MPE).
    bool takesMidiBytes = false;
};

//! Channel counts of the plug-in's audio ports, in the plug-in's order.
struct ClapAudioPortLayout {
    std::vector<uint32_t> inputChannels;
    std::vector<uint32_t> outputChannels;
};

//! How the plug-in's editor can be shown on this platform.
enum class ClapEditorKind {
    None,
    //! Inside a window this host supplies.
    Embedded,
    //! In a window the plug-in makes for itself.
    Floating
};

struct ClapEditorCallbacks {
    //! The plug-in asks for its embedded editor to take this size.
    std::function<bool(uint32_t width, uint32_t height)> resizeRequested;
    //! The plug-in's own window was closed.
    std::function<void()> closed;
};

//! One plug-in created from a CLAP bundle, with the host it talks to.
//!
//! Threads follow the CLAP specification. The thread that calls create() is
//! the main thread: create, activate, deactivate, destroy, state, editor and
//! serveMainThreadCallbacks run there. The audio functions run on the audio
//! thread and never at the same time as each other. An activation is handed
//! between the two threads through audioState().
class ClapHostedPlugin
{
public:
    //! Who may touch the activated plug-in.
    enum class AudioState {
        //! Not activated: the audio thread leaves the plug-in alone.
        Inactive,
        //! Activated again after a restart, and kept from the audio thread
        //! until the audio side has prepared for it (releaseToAudio).
        Held,
        //! Activated and waiting for the audio thread to start processing.
        Ready,
        Processing,
        //! start_processing was refused; silent until activated again.
        StartFailed,
        //! The main thread asks the audio thread to stop processing.
        StopRequested,
        //! The audio thread has stopped; the main thread may deactivate.
        Stopped
    };

    ClapHostedPlugin();
    ~ClapHostedPlugin();
    ClapHostedPlugin(const ClapHostedPlugin&) = delete;
    ClapHostedPlugin& operator=(const ClapHostedPlugin&) = delete;

    // --- main thread ---
    bool create(const ClapBundlePtr& bundle, const std::string& descriptorId, std::string& problem);
    bool isCreated() const;
    const ClapDescriptorInfo& descriptor() const;
    const ClapNotePortReport& notePorts() const;
    const ClapAudioPortLayout& audioPorts() const;

    //! Reads the plug-in's ports again, then activates it. \p held leaves it
    //! in the Held state.
    bool activate(double sampleRate, uint32_t maxFrames = CLAP_MAX_BLOCK_FRAMES, bool held = false);
    //! How many times the plug-in has been activated. Any thread.
    uint32_t activationCount() const;
    //! Audio engine thread: the audio side is prepared; Held becomes Ready.
    void releaseToAudio();
    //! Only in the Inactive, Ready, StartFailed or Stopped states.
    void deactivate();
    void destroy();
    double sampleRate() const;

    //! Asks the audio thread to stop processing. True once it has (or never started).
    bool requestAudioStop();

    bool hasState() const;
    bool saveState(std::vector<uint8_t>& bytes) const;
    bool loadState(const std::vector<uint8_t>& bytes);

    //! The plain value of a parameter, as the plug-in reports it.
    bool parameterValue(uint32_t paramId, double& value) const;

    //! Serves every request_callback made so far, and those made while
    //! serving. Returns how many times on_main_thread was called.
    int serveMainThreadCallbacks();
    bool takeStateChanged();
    //! Any thread, no allocation: the plug-in's state is no longer what was last saved.
    void markStateChanged();
    bool takeRestartRequested();
    //! True once after the plug-in asked for a parameter flush (request_flush).
    bool takeFlushRequested();

    ClapEditorKind editorKind() const;
    bool openEditor(ClapEditorKind kind, void* nativeParent, const std::string& title, const ClapEditorCallbacks& callbacks,
                    uint32_t& width, uint32_t& height);
    bool resizeEditor(uint32_t& width, uint32_t& height);
    void closeEditor();
    bool isEditorOpen() const;

    // --- audio thread ---
    AudioState audioState() const;
    //! Moves Ready to Processing, or acknowledges a stop request. True when a process call may follow.
    bool prepareToProcess();
    //! Stops processing at once. For the audio thread when no process call can be running.
    void stopProcessingNow();
    clap_process_status process(const clap_process_t* process);
    //! Parameter events with no audio. False when the plug-in has no parameters.
    bool flushParameters(const clap_input_events_t* in, const clap_output_events_t* out);

    //! A parameter value event for \p paramId, if the plug-in has that
    //! parameter and \p plain is inside its range. No allocation, no wait.
    bool makeParameterEvent(uint32_t paramId, double plain, ClapEventStorage& event) const;

    static const char* platformWindowApi();

private:
    struct ParamInfo {
        clap_id id = CLAP_INVALID_ID;
        void* cookie = nullptr;
        double min = 0.0;
        double max = 0.0;
    };

    static const void* CLAP_ABI hostGetExtension(const clap_host_t* host, const char* id);
    static void CLAP_ABI hostRequestRestart(const clap_host_t* host);
    static void CLAP_ABI hostRequestProcess(const clap_host_t* host);
    static void CLAP_ABI hostRequestCallback(const clap_host_t* host);

    void readPorts();
    void readParameters();

    clap_host_t m_host {};
    ClapBundlePtr m_bundle;
    const clap_plugin_t* m_plugin = nullptr;
    ClapDescriptorInfo m_descriptor;
    ClapNotePortReport m_notePorts;
    ClapAudioPortLayout m_audioPorts;

    const clap_plugin_state_t* m_state = nullptr;
    const clap_plugin_gui_t* m_gui = nullptr;
    const clap_plugin_params_t* m_params = nullptr;

    mutable std::mutex m_paramInfoMutex;
    std::vector<ParamInfo> m_paramInfo;

    std::atomic<AudioState> m_audioState { AudioState::Inactive };
    bool m_activated = false;
    std::atomic<uint32_t> m_activationCount { 0 };
    bool m_startedProcessing = false; // audio thread only
    double m_sampleRate = 0.0;

    std::atomic<bool> m_callbackRequested { false };
    std::atomic<bool> m_restartRequested { false };
    std::atomic<bool> m_flushRequested { false };
    std::atomic<bool> m_stateChanged { false };
    std::atomic<bool> m_parametersRescanRequested { false };

    bool m_editorOpen = false;
    bool m_editorFloating = false;
    ClapEditorCallbacks m_editorCallbacks;

    friend struct ClapHostExtensions;
};
}
