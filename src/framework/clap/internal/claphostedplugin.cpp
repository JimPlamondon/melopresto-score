/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "claphostedplugin.h"

#include <cmath>
#include <cstring>

#include "muse_framework_config.h"

#include "log.h"

using namespace muse::clap;

#ifndef MELO_SCORE_APP_NAME
#define MELO_SCORE_APP_NAME "MuseScore Studio"
#endif
#ifndef MELO_SCORE_APP_VERSION
#define MELO_SCORE_APP_VERSION "0"
#endif

namespace muse::clap {
//! More than zero while this thread is inside one of the plug-in's audio
//! functions. One thread may be both the main and the audio thread.
static thread_local int t_clapAudioCallDepth = 0;
static std::atomic<std::thread::id> s_clapMainThread {};

struct ClapAudioCallScope {
    ClapAudioCallScope() { ++t_clapAudioCallDepth; }
    ~ClapAudioCallScope() { --t_clapAudioCallDepth; }
};

static ClapHostedPlugin* clapHosted(const clap_host_t* host)
{
    return host ? static_cast<ClapHostedPlugin*>(host->host_data) : nullptr;
}

//! The host side of the CLAP extensions this host offers a plug-in.
struct ClapHostExtensions {
    // thread-check
    static bool CLAP_ABI isMainThread(const clap_host_t*)
    {
        return std::this_thread::get_id() == s_clapMainThread.load() && t_clapAudioCallDepth == 0;
    }

    static bool CLAP_ABI isAudioThread(const clap_host_t*)
    {
        // Only a thread that is inside one of the plug-in's audio functions.
        // A thread of the plug-in's own is neither the main nor the audio thread.
        return t_clapAudioCallDepth > 0;
    }

    static bool onMainThread()
    {
        return std::this_thread::get_id() == s_clapMainThread.load() && t_clapAudioCallDepth == 0;
    }

    // log
    static void CLAP_ABI log(const clap_host_t* host, clap_log_severity severity, const char* message)
    {
        const ClapHostedPlugin* self = clapHosted(host);
        const std::string name = self ? self->m_descriptor.name : std::string();
        const char* text = message ? message : "";
        if (severity >= CLAP_LOG_ERROR) {
            LOGE() << "CLAP " << name << ": " << text;
        } else if (severity == CLAP_LOG_WARNING) {
            LOGW() << "CLAP " << name << ": " << text;
        } else {
            LOGI() << "CLAP " << name << ": " << text;
        }
    }

    // state
    static void CLAP_ABI stateMarkDirty(const clap_host_t* host)
    {
        if (ClapHostedPlugin* self = clapHosted(host)) {
            self->m_signalsSeen.fetch_or(ClapHostedPlugin::MarkedDirty);
            self->m_stateChanged.store(true);
        }
    }

    // params
    static void CLAP_ABI paramsRescan(const clap_host_t* host, clap_param_rescan_flags flags)
    {
        if (ClapHostedPlugin* self = clapHosted(host)) {
            if (flags & (CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_ALL)) {
                // Read again at the main thread's next visit; on_main_thread is not called for it.
                self->m_parametersRescanRequested.store(true);
            }
            // Whatever is to be scanned again, the state may have changed.
            self->m_signalsSeen.fetch_or(ClapHostedPlugin::AskedForRescan);
            self->m_stateChanged.store(true);
        }
    }

    static void CLAP_ABI paramsClear(const clap_host_t*, clap_id, clap_param_clear_flags)
    {
        // This host keeps no automation and no modulation of a plug-in's parameters.
    }

    static void CLAP_ABI paramsRequestFlush(const clap_host_t* host)
    {
        // The plug-in has parameter values to hand over (a control moved in
        // its editor, for one). While a score plays, the next process call
        // takes them. While playback is stopped the engine renders no block,
        // so the audio side is asked to call the parameter flush.
        if (ClapHostedPlugin* self = clapHosted(host)) {
            self->m_signalsSeen.fetch_or(ClapHostedPlugin::AskedForFlush);
            self->m_flushRequested.store(true);
        }
    }

    // gui
    static void CLAP_ABI guiResizeHintsChanged(const clap_host_t*)
    {
    }

    static bool CLAP_ABI guiRequestResize(const clap_host_t* host, uint32_t width, uint32_t height)
    {
        // The editor's state belongs to the main thread; a call from any other is refused.
        ClapHostedPlugin* self = clapHosted(host);
        if (!self || !onMainThread() || !self->m_editorOpen || !self->m_editorCallbacks.resizeRequested) {
            return false;
        }
        return self->m_editorCallbacks.resizeRequested(width, height);
    }

    static bool CLAP_ABI guiRequestShow(const clap_host_t*)
    {
        return false;
    }

    static bool CLAP_ABI guiRequestHide(const clap_host_t*)
    {
        return false;
    }

    static void CLAP_ABI guiClosed(const clap_host_t* host, bool)
    {
        ClapHostedPlugin* self = clapHosted(host);
        if (self && onMainThread() && self->m_editorCallbacks.closed) {
            self->m_editorCallbacks.closed();
        }
    }

    // note-ports
    static uint32_t CLAP_ABI notePortsSupportedDialects(const clap_host_t*)
    {
        return CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE;
    }

    static void CLAP_ABI notePortsRescan(const clap_host_t* host, uint32_t)
    {
        // Ports are read when the plug-in is created; a change takes a restart.
        if (ClapHostedPlugin* self = clapHosted(host)) {
            self->m_restartRequested.store(true);
        }
    }

    // audio-ports
    static bool CLAP_ABI audioPortsIsRescanFlagSupported(const clap_host_t*, uint32_t)
    {
        return false;
    }

    static void CLAP_ABI audioPortsRescan(const clap_host_t*, uint32_t)
    {
    }

    // latency
    static void CLAP_ABI latencyChanged(const clap_host_t*)
    {
        // The mixer does not compensate an instrument's latency.
    }
};

static const clap_host_thread_check_t CLAP_HOST_THREAD_CHECK = {
    &ClapHostExtensions::isMainThread, &ClapHostExtensions::isAudioThread
};
static const clap_host_log_t CLAP_HOST_LOG = { &ClapHostExtensions::log };
static const clap_host_state_t CLAP_HOST_STATE = { &ClapHostExtensions::stateMarkDirty };
static const clap_host_params_t CLAP_HOST_PARAMS = {
    &ClapHostExtensions::paramsRescan, &ClapHostExtensions::paramsClear, &ClapHostExtensions::paramsRequestFlush
};
static const clap_host_gui_t CLAP_HOST_GUI = {
    &ClapHostExtensions::guiResizeHintsChanged, &ClapHostExtensions::guiRequestResize, &ClapHostExtensions::guiRequestShow,
    &ClapHostExtensions::guiRequestHide, &ClapHostExtensions::guiClosed
};
static const clap_host_note_ports_t CLAP_HOST_NOTE_PORTS = {
    &ClapHostExtensions::notePortsSupportedDialects, &ClapHostExtensions::notePortsRescan
};
static const clap_host_audio_ports_t CLAP_HOST_AUDIO_PORTS = {
    &ClapHostExtensions::audioPortsIsRescanFlagSupported, &ClapHostExtensions::audioPortsRescan
};
static const clap_host_latency_t CLAP_HOST_LATENCY = { &ClapHostExtensions::latencyChanged };

struct ClapStateWriter {
    std::vector<uint8_t>* bytes = nullptr;

    static int64_t CLAP_ABI write(const clap_ostream_t* stream, const void* buffer, uint64_t size)
    {
        //! More than any instrument's state should need; a refusal, not a crash, beyond it.
        static constexpr uint64_t LIMIT = 256u * 1024u * 1024u;
        ClapStateWriter* self = static_cast<ClapStateWriter*>(stream->ctx);
        if (!buffer || self->bytes->size() + size > LIMIT) {
            return -1;
        }
        const uint8_t* data = static_cast<const uint8_t*>(buffer);
        self->bytes->insert(self->bytes->end(), data, data + size);
        return static_cast<int64_t>(size);
    }
};

struct ClapStateReader {
    const std::vector<uint8_t>* bytes = nullptr;
    size_t cursor = 0;

    static int64_t CLAP_ABI read(const clap_istream_t* stream, void* buffer, uint64_t size)
    {
        ClapStateReader* self = static_cast<ClapStateReader*>(stream->ctx);
        if (!buffer) {
            return -1;
        }
        const size_t available = self->bytes->size() - self->cursor;
        const size_t count = static_cast<size_t>(std::min<uint64_t>(size, available));
        if (count > 0) {
            std::memcpy(buffer, self->bytes->data() + self->cursor, count);
        }
        self->cursor += count;
        return static_cast<int64_t>(count);
    }
};
}

ClapHostedPlugin::ClapHostedPlugin()
{
    m_host.clap_version = CLAP_VERSION;
    m_host.host_data = this;
    m_host.name = MELO_SCORE_APP_NAME;
    m_host.vendor = "";
    m_host.url = "";
    m_host.version = MELO_SCORE_APP_VERSION;
    m_host.get_extension = &ClapHostedPlugin::hostGetExtension;
    m_host.request_restart = &ClapHostedPlugin::hostRequestRestart;
    m_host.request_process = &ClapHostedPlugin::hostRequestProcess;
    m_host.request_callback = &ClapHostedPlugin::hostRequestCallback;
}

ClapHostedPlugin::~ClapHostedPlugin()
{
    destroy();
}

const void* ClapHostedPlugin::hostGetExtension(const clap_host_t*, const char* id)
{
    if (!id) {
        return nullptr;
    }
    if (std::strcmp(id, CLAP_EXT_THREAD_CHECK) == 0) {
        return &CLAP_HOST_THREAD_CHECK;
    }
    if (std::strcmp(id, CLAP_EXT_LOG) == 0) {
        return &CLAP_HOST_LOG;
    }
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) {
        return &CLAP_HOST_STATE;
    }
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
        return &CLAP_HOST_PARAMS;
    }
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) {
        return &CLAP_HOST_GUI;
    }
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) {
        return &CLAP_HOST_NOTE_PORTS;
    }
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
        return &CLAP_HOST_AUDIO_PORTS;
    }
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) {
        return &CLAP_HOST_LATENCY;
    }
    return nullptr;
}

void ClapHostedPlugin::hostRequestRestart(const clap_host_t* host)
{
    if (ClapHostedPlugin* self = clapHosted(host)) {
        self->m_restartRequested.store(true);
    }
}

void ClapHostedPlugin::hostRequestProcess(const clap_host_t*)
{
    // Processing never sleeps in this host.
}

void ClapHostedPlugin::hostRequestCallback(const clap_host_t* host)
{
    // Any thread may ask; the main thread serves it (serveMainThreadCallbacks).
    if (ClapHostedPlugin* self = clapHosted(host)) {
        self->m_callbackRequested.store(true);
    }
}

bool ClapHostedPlugin::create(const ClapBundlePtr& bundle, const std::string& descriptorId, std::string& problem)
{
    if (m_plugin) {
        problem = "the plug-in is already created";
        return false;
    }
    if (!bundle) {
        problem = "no CLAP bundle";
        return false;
    }

    s_clapMainThread.store(std::this_thread::get_id());

    bool found = false;
    for (const ClapDescriptorInfo& info : bundle->descriptors()) {
        if (info.id == descriptorId) {
            m_descriptor = info;
            found = true;
            break;
        }
    }
    if (!found) {
        problem = "the bundle " + bundle->path() + " has no plug-in with the id " + descriptorId;
        return false;
    }

    m_bundle = bundle;
    m_plugin = bundle->createPlugin(&m_host, descriptorId);
    if (!m_plugin) {
        problem = "the factory did not create " + descriptorId;
        m_bundle.reset();
        return false;
    }
    if (!m_plugin->init(m_plugin)) {
        problem = "the plug-in " + descriptorId + " refused to initialise";
        m_plugin->destroy(m_plugin);
        m_plugin = nullptr;
        m_bundle.reset();
        return false;
    }

    m_state = static_cast<const clap_plugin_state_t*>(m_plugin->get_extension(m_plugin, CLAP_EXT_STATE));
    m_gui = static_cast<const clap_plugin_gui_t*>(m_plugin->get_extension(m_plugin, CLAP_EXT_GUI));
    m_params = static_cast<const clap_plugin_params_t*>(m_plugin->get_extension(m_plugin, CLAP_EXT_PARAMS));

    readPorts();
    readParameters();
    return true;
}

void ClapHostedPlugin::readPorts()
{
    m_notePorts = ClapNotePortReport();
    m_audioPorts = ClapAudioPortLayout();

    const clap_plugin_note_ports_t* notePorts
        = static_cast<const clap_plugin_note_ports_t*>(m_plugin->get_extension(m_plugin, CLAP_EXT_NOTE_PORTS));
    if (notePorts && notePorts->count && notePorts->get && notePorts->count(m_plugin, true) > 0) {
        clap_note_port_info_t info {};
        if (notePorts->get(m_plugin, 0, true, &info)) {
            m_notePorts.hasNoteInput = true;
            m_notePorts.takesNoteEvents = (info.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0;
            m_notePorts.takesMidiBytes = (info.supported_dialects & (CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE)) != 0;
        }
    }

    const clap_plugin_audio_ports_t* audioPorts
        = static_cast<const clap_plugin_audio_ports_t*>(m_plugin->get_extension(m_plugin, CLAP_EXT_AUDIO_PORTS));
    if (audioPorts && audioPorts->count && audioPorts->get) {
        for (const bool isInput : { true, false }) {
            const uint32_t count = audioPorts->count(m_plugin, isInput);
            for (uint32_t i = 0; i < count; ++i) {
                clap_audio_port_info_t info {};
                const uint32_t channels = audioPorts->get(m_plugin, i, isInput, &info) ? info.channel_count : 0;
                (isInput ? m_audioPorts.inputChannels : m_audioPorts.outputChannels).push_back(channels);
            }
        }
    }
}

void ClapHostedPlugin::readParameters()
{
    std::vector<ParamInfo> list;
    if (m_params && m_params->count && m_params->get_info) {
        const uint32_t count = m_params->count(m_plugin);
        list.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            clap_param_info_t info {};
            if (m_params->get_info(m_plugin, i, &info)) {
                list.push_back(ParamInfo { info.id, info.cookie, info.min_value, info.max_value });
            }
        }
    }
    std::lock_guard lock(m_paramInfoMutex);
    m_paramInfo = std::move(list);
}

bool ClapHostedPlugin::isCreated() const
{
    return m_plugin != nullptr;
}

const ClapDescriptorInfo& ClapHostedPlugin::descriptor() const
{
    return m_descriptor;
}

const ClapNotePortReport& ClapHostedPlugin::notePorts() const
{
    return m_notePorts;
}

const ClapAudioPortLayout& ClapHostedPlugin::audioPorts() const
{
    return m_audioPorts;
}

uint32_t ClapHostedPlugin::activationCount() const
{
    return m_activationCount.load();
}

void ClapHostedPlugin::releaseToAudio()
{
    AudioState expected = AudioState::Held;
    m_audioState.compare_exchange_strong(expected, AudioState::Ready);
}

bool ClapHostedPlugin::activate(double sampleRate, uint32_t maxFrames, bool held)
{
    if (!m_plugin || m_activated || !std::isfinite(sampleRate) || sampleRate <= 0.0 || maxFrames == 0) {
        return false;
    }
    // A plug-in may change its ports while it is deactivated; they are read
    // again at every activation.
    readPorts();
    if (!m_plugin->activate(m_plugin, sampleRate, 1, maxFrames)) {
        return false;
    }
    m_activated = true;
    m_sampleRate = sampleRate;
    m_activationCount.fetch_add(1);
    m_audioState.store(held ? AudioState::Held : AudioState::Ready);
    return true;
}

void ClapHostedPlugin::deactivate()
{
    if (!m_plugin || !m_activated) {
        return;
    }
    const AudioState state = m_audioState.load();
    IF_ASSERT_FAILED(state != AudioState::Processing && state != AudioState::StopRequested) {
        return;
    }
    m_audioState.store(AudioState::Inactive);
    m_plugin->deactivate(m_plugin);
    m_activated = false;
}

void ClapHostedPlugin::destroy()
{
    if (!m_plugin) {
        return;
    }
    closeEditor();
    if (m_activated) {
        // The owner stops the audio before it lets go (ClapSynthesiser does).
        // If processing was left started, no audio thread uses the plug-in
        // any more, and it is stopped here before it is deactivated.
        if (m_startedProcessing) {
            ClapAudioCallScope scope;
            m_plugin->stop_processing(m_plugin);
            m_startedProcessing = false;
        }
        m_audioState.store(AudioState::Inactive);
        m_plugin->deactivate(m_plugin);
        m_activated = false;
    }
    m_plugin->destroy(m_plugin);
    m_plugin = nullptr;
    m_state = nullptr;
    m_gui = nullptr;
    m_params = nullptr;
    m_bundle.reset();
}

double ClapHostedPlugin::sampleRate() const
{
    return m_sampleRate;
}

bool ClapHostedPlugin::requestAudioStop()
{
    AudioState expected = AudioState::Processing;
    if (m_audioState.compare_exchange_strong(expected, AudioState::StopRequested)) {
        return false;
    }
    if (expected == AudioState::StopRequested) {
        return false;
    }
    if (expected == AudioState::Ready || expected == AudioState::StartFailed || expected == AudioState::Held) {
        // The audio thread may be taking Ready for itself at this moment.
        if (!m_audioState.compare_exchange_strong(expected, AudioState::Stopped)) {
            return requestAudioStop();
        }
    }
    return true;
}

bool ClapHostedPlugin::hasState() const
{
    return m_plugin && m_state && m_state->save && m_state->load;
}

bool ClapHostedPlugin::saveState(std::vector<uint8_t>& bytes) const
{
    bytes.clear();
    if (!hasState()) {
        return false;
    }
    ClapStateWriter writer { &bytes };
    clap_ostream_t stream { &writer, &ClapStateWriter::write };
    if (!m_state->save(m_plugin, &stream)) {
        bytes.clear();
        return false;
    }
    return true;
}

bool ClapHostedPlugin::loadState(const std::vector<uint8_t>& bytes)
{
    if (!hasState()) {
        return false;
    }
    ClapStateReader reader { &bytes, 0 };
    clap_istream_t stream { &reader, &ClapStateReader::read };
    return m_state->load(m_plugin, &stream);
}

bool ClapHostedPlugin::parameterValue(uint32_t paramId, double& value) const
{
    return m_plugin && m_params && m_params->get_value && m_params->get_value(m_plugin, paramId, &value);
}

int ClapHostedPlugin::serveMainThreadCallbacks()
{
    //! A plug-in may ask again while it is being served, more than once while it settles.
    static constexpr int MOST_IN_ONE_VISIT = 64;
    int served = 0;
    if (m_plugin && m_parametersRescanRequested.exchange(false)) {
        readParameters();
    }
    while (m_plugin && served < MOST_IN_ONE_VISIT && m_callbackRequested.exchange(false)) {
        m_plugin->on_main_thread(m_plugin);
        ++served;
    }
    return served;
}

bool ClapHostedPlugin::takeStateChanged()
{
    return m_stateChanged.exchange(false);
}

void ClapHostedPlugin::markStateChanged()
{
    m_signalsSeen.fetch_or(ReportedOnOutputEvents);
    m_stateChanged.store(true);
}

uint32_t ClapHostedPlugin::signalsSeen() const
{
    return m_signalsSeen.load();
}

bool ClapHostedPlugin::takeFlushRequested()
{
    return m_flushRequested.exchange(false);
}

bool ClapHostedPlugin::takeRestartRequested()
{
    return m_restartRequested.exchange(false);
}

const char* ClapHostedPlugin::platformWindowApi()
{
#if defined(__APPLE__)
    return CLAP_WINDOW_API_COCOA;
#elif defined(_WIN32)
    return CLAP_WINDOW_API_WIN32;
#else
    return CLAP_WINDOW_API_X11;
#endif
}

ClapEditorKind ClapHostedPlugin::editorKind() const
{
    if (!m_plugin || !m_gui || !m_gui->is_api_supported || !m_gui->create || !m_gui->destroy || !m_gui->show) {
        return ClapEditorKind::None;
    }
    if (m_gui->is_api_supported(m_plugin, platformWindowApi(), false) && m_gui->set_parent && m_gui->get_size) {
        return ClapEditorKind::Embedded;
    }
    if (m_gui->is_api_supported(m_plugin, platformWindowApi(), true)) {
        return ClapEditorKind::Floating;
    }
    return ClapEditorKind::None;
}

bool ClapHostedPlugin::openEditor(ClapEditorKind kind, void* nativeParent, const std::string& title,
                                  const ClapEditorCallbacks& callbacks, uint32_t& width, uint32_t& height)
{
    if (m_editorOpen || kind == ClapEditorKind::None || editorKind() == ClapEditorKind::None) {
        return false;
    }
    const bool floating = kind == ClapEditorKind::Floating;
    if (!floating && !nativeParent) {
        return false;
    }
    if (!m_gui->create(m_plugin, platformWindowApi(), floating)) {
        return false;
    }
    m_editorOpen = true;
    m_editorFloating = floating;
    m_editorCallbacks = callbacks;

    if (floating) {
        if (m_gui->suggest_title) {
            m_gui->suggest_title(m_plugin, title.c_str());
        }
    } else {
        if (!m_gui->get_size(m_plugin, &width, &height)) {
            closeEditor();
            return false;
        }
        clap_window_t window {};
        window.api = platformWindowApi();
#if defined(__APPLE__) || defined(_WIN32)
        window.ptr = nativeParent;
#else
        window.x11 = static_cast<clap_xwnd>(reinterpret_cast<uintptr_t>(nativeParent));
#endif
        if (!m_gui->set_parent(m_plugin, &window)) {
            closeEditor();
            return false;
        }
    }

    // A plug-in may show itself when it is given its parent; a refusal here is not a failure.
    m_gui->show(m_plugin);
    return true;
}

bool ClapHostedPlugin::resizeEditor(uint32_t& width, uint32_t& height)
{
    if (!m_editorOpen || m_editorFloating || !m_gui->can_resize || !m_gui->can_resize(m_plugin) || !m_gui->set_size) {
        return false;
    }
    if (m_gui->adjust_size) {
        m_gui->adjust_size(m_plugin, &width, &height);
    }
    return m_gui->set_size(m_plugin, width, height);
}

void ClapHostedPlugin::closeEditor()
{
    if (!m_editorOpen || !m_plugin || !m_gui) {
        return;
    }
    m_editorOpen = false;
    m_editorCallbacks = ClapEditorCallbacks();
    if (m_gui->hide) {
        m_gui->hide(m_plugin);
    }
    m_gui->destroy(m_plugin);
}

bool ClapHostedPlugin::isEditorOpen() const
{
    return m_editorOpen;
}

ClapHostedPlugin::AudioState ClapHostedPlugin::audioState() const
{
    return m_audioState.load();
}

bool ClapHostedPlugin::prepareToProcess()
{
    AudioState state = m_audioState.load();

    if (state == AudioState::Ready) {
        // Take the activation before touching the plug-in: the main thread
        // may be withdrawing it at this moment.
        if (!m_audioState.compare_exchange_strong(state, AudioState::Processing)) {
            return prepareToProcess();
        }
        bool started = false;
        {
            ClapAudioCallScope scope;
            started = m_plugin->start_processing(m_plugin);
        }
        m_startedProcessing = started;
        if (!started) {
            AudioState expected = AudioState::Processing;
            if (!m_audioState.compare_exchange_strong(expected, AudioState::StartFailed)) {
                m_audioState.store(AudioState::Stopped);
            }
            return false;
        }
        return true;
    }

    if (state == AudioState::StopRequested) {
        if (m_startedProcessing) {
            ClapAudioCallScope scope;
            m_plugin->stop_processing(m_plugin);
            m_startedProcessing = false;
        }
        m_audioState.store(AudioState::Stopped);
        return false;
    }

    return state == AudioState::Processing;
}

void ClapHostedPlugin::stopProcessingNow()
{
    const AudioState state = m_audioState.load();
    // A held plug-in is not processing and stays held for the audio side to release.
    if (state == AudioState::Inactive || state == AudioState::Stopped || state == AudioState::Held) {
        return;
    }
    if (m_startedProcessing) {
        ClapAudioCallScope scope;
        m_plugin->stop_processing(m_plugin);
        m_startedProcessing = false;
    }
    m_audioState.store(AudioState::Stopped);
}

clap_process_status ClapHostedPlugin::process(const clap_process_t* process)
{
    // A process call always carries frames; work with none is a parameter flush.
    IF_ASSERT_FAILED(process && process->frames_count > 0 && m_startedProcessing) {
        return CLAP_PROCESS_ERROR;
    }
    ClapAudioCallScope scope;
    return m_plugin->process(m_plugin, process);
}

bool ClapHostedPlugin::flushParameters(const clap_input_events_t* in, const clap_output_events_t* out)
{
    if (!m_plugin || !m_params || !m_params->flush || !m_startedProcessing) {
        return false;
    }
    ClapAudioCallScope scope;
    m_params->flush(m_plugin, in, out);
    return true;
}

bool ClapHostedPlugin::makeParameterEvent(uint32_t paramId, double plain, ClapEventStorage& event) const
{
    if (!std::isfinite(plain)) {
        return false;
    }
    std::unique_lock lock(m_paramInfoMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return false;
    }
    for (const ParamInfo& info : m_paramInfo) {
        if (info.id != paramId) {
            continue;
        }
        // CLAP parameter values are in the plug-in's plain range.
        if (plain < info.min || plain > info.max) {
            return false;
        }
        event = ClapEventStorage {};
        event.param.header.size = sizeof(clap_event_param_value_t);
        event.param.header.time = 0;
        event.param.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.param.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param.header.flags = 0;
        event.param.param_id = info.id;
        event.param.cookie = info.cookie;
        event.param.note_id = -1;
        event.param.port_index = -1;
        event.param.channel = -1;
        event.param.key = -1;
        event.param.value = plain;
        return true;
    }
    return false;
}
