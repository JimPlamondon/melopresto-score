/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
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
#include "vstaudioclient.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

#include "log.h"

using namespace muse;
using namespace muse::vst;
using namespace muse::mpe;
using namespace muse::audio;
using namespace muse::audio::engine;
using namespace muse::audioplugins;
using namespace muse::midiremote;

static std::optional<TransportEvent> mmcToTransportEvent(const IMMCDecoderPtr& decoder, const MMCMessage& msg)
{
    switch (msg.command) {
    case MMCCommand::Play:
        return TransportEvent::play();
    case MMCCommand::Pause:
        return TransportEvent::pause();
    case MMCCommand::Stop:
        return TransportEvent::stop();
    case MMCCommand::Locate: {
        const std::optional<double> pos = decoder->locateToSeconds(msg);
        if (pos.has_value()) {
            return TransportEvent::seek(pos.value());
        }
    } break;
    default: break;
    }

    return std::nullopt;
}

namespace {
// MeloSynth VST3 workstream: an optional diagnostic trace of every event the
// client forwards to the plug-in and every process call, appended as text to
// the file named by MUSE_VST_EVENT_TRACE. Off (one static check) unless the
// environment variable is set; the enabled path is not real-time safe and is
// meant for offline export evidence (end-to-end transport-precision checks),
// never for ordinary playback. Events are queued and written at the next
// process call so their `pos` is the playback position (in samples) of the
// block that consumes them.
class EventTrace
{
public:
    static EventTrace& instance()
    {
        static EventTrace trace;
        return trace;
    }

    bool enabled() const { return m_file != nullptr; }

    void event(const void* client, const VstEvent& event)
    {
        if (!m_file) {
            return;
        }
        std::lock_guard lock(m_mutex);
        m_pending.push_back({ client, event });
    }

    void param(const void* client, const ParamChangeEvent& param)
    {
        if (!m_file) {
            return;
        }
        std::lock_guard lock(m_mutex);
        std::fprintf(m_file, "client=%p param id=%u value=%.17g\n", client, param.paramId, param.value);
        std::fflush(m_file);
    }

    void process(const void* client, samples_t playbackPositionSamples, samples_t frames, float peak, bool ok)
    {
        if (!m_file) {
            return;
        }
        std::lock_guard lock(m_mutex);
        for (const PendingEvent& pending : m_pending) {
            if (pending.client == client) {
                write(pending, playbackPositionSamples);
            }
        }
        m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(),
                                       [client](const PendingEvent& p) { return p.client == client; }),
                        m_pending.end());
        std::fprintf(m_file, "client=%p pos=%lld process frames=%u peak=%.9g ok=%d\n",
                     client, static_cast<long long>(playbackPositionSamples), static_cast<unsigned>(frames), peak, ok ? 1 : 0);
        std::fflush(m_file);
    }

private:
    struct PendingEvent {
        const void* client = nullptr;
        VstEvent event;
    };

    EventTrace()
    {
        const char* path = std::getenv("MUSE_VST_EVENT_TRACE");
        if (path && *path) {
            m_file = std::fopen(path, "a");
        }
    }

    ~EventTrace()
    {
        if (m_file) {
            std::fclose(m_file);
        }
    }

    void write(const PendingEvent& pending, samples_t playbackPositionSamples)
    {
        const VstEvent& event = pending.event;
        const void* client = pending.client;
        switch (event.type) {
        case VstEvent::kNoteOnEvent:
            std::fprintf(m_file, "client=%p pos=%lld offset=%d noteOn id=%d pitch=%d tuning=%.9g velocity=%.9g channel=%d\n",
                         client, static_cast<long long>(playbackPositionSamples), event.sampleOffset, event.noteOn.noteId,
                         event.noteOn.pitch, event.noteOn.tuning, event.noteOn.velocity, event.noteOn.channel);
            break;
        case VstEvent::kNoteOffEvent:
            std::fprintf(m_file, "client=%p pos=%lld offset=%d noteOff id=%d pitch=%d velocity=%.9g channel=%d\n",
                         client, static_cast<long long>(playbackPositionSamples), event.sampleOffset, event.noteOff.noteId,
                         event.noteOff.pitch, event.noteOff.velocity, event.noteOff.channel);
            break;
        case VstEvent::kNoteExpressionValueEvent:
            std::fprintf(m_file, "client=%p pos=%lld offset=%d noteExpression id=%d typeId=%u value=%.17g\n",
                         client, static_cast<long long>(playbackPositionSamples), event.sampleOffset,
                         event.noteExpressionValue.noteId, event.noteExpressionValue.typeId, event.noteExpressionValue.value);
            break;
        default:
            std::fprintf(m_file, "client=%p pos=%lld offset=%d eventType=%d\n",
                         client, static_cast<long long>(playbackPositionSamples), event.sampleOffset, event.type);
            break;
        }
    }

    std::FILE* m_file = nullptr;
    std::mutex m_mutex;
    std::vector<PendingEvent> m_pending;
};
}

VstAudioClient::VstAudioClient(const modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx)
{
    m_processContext.state = 0;
}

VstAudioClient::~VstAudioClient()
{
    // Do not call setActive(false) or terminate() here.
    // The component lifecycle is managed by VstPluginInstance,
    // which defers cleanup to the main thread so that any open
    // editor view is destroyed first (required by ZENOLOGY).
}

void VstAudioClient::init(AudioPluginType type, IVstPluginInstancePtr instance)
{
    IF_ASSERT_FAILED(instance && type != AudioPluginType::Undefined) {
        return;
    }

    m_type = type;
    m_pluginPtr = std::move(instance);

    if (mmcDecoderFactory()) {
        m_mmcDecoder = mmcDecoderFactory()->makeDecoder();
    }

    transportEventsDispatcher(); // Force resolution outside audio callback
}

void VstAudioClient::loadSupportedParams()
{
    TRACEFUNC;

    IF_ASSERT_FAILED(m_pluginPtr) {
        return;
    }

    PluginControllerPtr controller = m_pluginPtr->controller();
    IF_ASSERT_FAILED(controller) {
        return;
    }

    const int paramCount = controller->getParameterCount();
    m_inputParamChanges.setMaxParameters(paramCount);
    std::vector<PluginParamInfo> allParameterInfo;
    allParameterInfo.reserve(static_cast<size_t>(paramCount));
    m_pluginParamInfoMap.reserve(static_cast<size_t>(paramCount));

    for (int i = 0; i < paramCount; ++i) {
        PluginParamInfo info;
        if (controller->getParameterInfo(i, info) != Steinberg::kResultOk) {
            continue;
        }
        allParameterInfo.push_back(info);
        if (VstDynamicTonalityProfileHost::isReserved(info.id)) {
            continue;
        }
        m_pluginParamInfoMap.emplace(info.id, std::move(info));
    }

    const PluginParamValue capability = controller->getParamNormalized(VstDynamicTonalityProfileHost::PARAM_CAPABILITY);
    m_profileHost.discover(allParameterInfo, capability);
}

void VstAudioClient::setIsActive(const bool isActive)
{
    flushSound();

    if (isActive) {
        if (!m_profileTransactionPending && m_profileHost.hasCurrentProfile()) {
            deliverDynamicTonalityProfile(m_profileHost.currentProfile(), true);
        }
        ensureActivity();
    } else {
        disableActivity();
    }
}

void VstAudioClient::setIsPlaying(const bool newPlaying)
{
    constexpr uint32_t playingFlag = static_cast<uint32_t>(VstProcessContext::kPlaying);
    const bool playing = (m_processContext.state & playingFlag) != 0;
    if (playing == newPlaying) {
        return;
    }

    if (newPlaying) {
        m_processContext.state |= playingFlag;
        if (!m_profileTransactionPending && m_profileHost.hasCurrentProfile()) {
            // prepare(force=true) retransmits the current Kernel payload with
            // a fresh generation and the noncurrent staging slot.
            const auto& playbackProfile = m_profileHost.currentProfile();
            deliverDynamicTonalityProfile(playbackProfile, true);
        }
    } else {
        m_processContext.state &= ~playingFlag;
    }

    m_needUpdateState = m_isActive;
}

void VstAudioClient::setOutputSpec(const audio::OutputSpec& spec)
{
    if (m_outputSpec == spec) {
        return;
    }

    m_processData.numSamples = static_cast<Steinberg::int32>(spec.samplesPerChannel);
    m_outputSpec = spec;
    m_needUnprepareProcessData = true;
    m_needUpdateState = false;

    updateProcessSetup();
}

void VstAudioClient::setProcessMode(VstProcessMode mode)
{
    if (m_processMode == mode) {
        return;
    }

    m_processMode = mode;
    m_needUnprepareProcessData = true;
    m_needUpdateState = false;

    updateProcessSetup();
}

void VstAudioClient::setVolumeGain(const muse::audio::gain_t newVolumeGain)
{
    m_volumeGain = newVolumeGain;
}

bool VstAudioClient::handleEvent(const VstEvent& event)
{
    ensureActivity();

    // MeloSynth VST3 workstream: active notes are keyed by their VST3 note
    // identifier (pitch/channel only for legacy events with noteId < 0), so
    // overlapping unisons with distinct ids are tracked — and released —
    // separately. Note-expression events pass straight through.
    if (event.type == VstEvent::kNoteOnEvent) {
        m_playingNotes.noteOn(event);
    } else if (event.type == VstEvent::kNoteOffEvent) {
        m_playingNotes.noteOff(event);
    }

    EventTrace::instance().event(this, event);

    if (m_inputEvents.addEvent(const_cast<VstEvent&>(event)) == Steinberg::kResultTrue) {
        return true;
    }

    return false;
}

bool VstAudioClient::handleParamChange(const ParamChangeEvent& param)
{
    ensureActivity();
    addParamChange(param);
    EventTrace::instance().param(this, param);

    m_playingParams.push_back(param.paramId);

    return true;
}

bool VstAudioClient::handlePersistentParamChange(const ParamChangeEvent& param)
{
    if (!std::isfinite(param.value) || param.value < 0.0 || param.value > 1.0
        || m_pluginParamInfoMap.find(param.paramId) == m_pluginParamInfoMap.end()) {
        return false;
    }

    ensureActivity();
    if (!m_isActive) {
        return false;
    }

    rememberPersistentParam(param);
    ++m_persistentStateGeneration;
    addParamChange(param);
    EventTrace::instance().param(this, param);

    // A stopped score has no render callback to consume the VST queue. Apply
    // the score-owned value through a silent parameter-only process so a
    // subsequent component-state scan persists it without requiring play.
    if (!isPlaying()) {
        if (!processIdleParamChange(param)) {
            return false;
        }
        m_persistentStateDeliveredGeneration = m_persistentStateGeneration;
        return true;
    }

    m_persistentStateRefreshPending = true;
    return true;
}

bool VstAudioClient::takePersistentStateRefreshRequest()
{
    if (m_persistentStateRefreshPending
        || m_persistentStateDeliveredGeneration != m_persistentStateGeneration
        || m_persistentStateRefreshRequestedGeneration == m_persistentStateGeneration) {
        return false;
    }

    m_persistentStateRefreshRequestedGeneration = m_persistentStateGeneration;
    return true;
}

bool VstAudioClient::handleDynamicTonalityProfile(const mpe::DynamicTonalityProfileEvent& profile, bool force)
{
    if (m_profileTransactionPending
        && (!m_profileHost.hasCurrentProfile() || !(profile == m_profileHost.currentProfile()))) {
        LOGE() << "Conflicting VST3 Dynamic Tonality profiles at one process position";
        return false;
    }
    const bool restageCurrent = !m_profileTransactionPending && m_profileHost.hasCurrentProfile()
                                && profile == m_profileHost.currentProfile();
    return deliverDynamicTonalityProfile(profile, force || restageCurrent);
}

bool VstAudioClient::deliverDynamicTonalityProfile(const mpe::DynamicTonalityProfileEvent& profile, bool force)
{
    std::array<ParamChangeEvent, VstDynamicTonalityProfileHost::POINT_COUNT> transaction;
    if (!m_profileHost.prepare(profile, force, transaction)) {
        return false;
    }
    PluginControllerPtr controller = m_pluginPtr ? m_pluginPtr->controller() : nullptr;
    if (!controller) {
        return false;
    }
    for (const ParamChangeEvent& point : transaction) {
        controller->setParamNormalized(point.paramId, point.value);
        addParamChange(point);
        EventTrace::instance().param(this, point);
    }
    m_profileTransactionPending = true;
    return true;
}

void VstAudioClient::flushSound()
{
    if (m_playingNotes.empty() && m_playingParams.empty()) {
        m_inputEvents.clear();
        m_inputParamChanges.clearQueue();
        restagePersistentParams();
        m_profileTransactionPending = false;
        return;
    }

    flushBuffers();

    m_inputEvents.clear();
    m_inputParamChanges.clearQueue();
    m_profileTransactionPending = false;

    // Every sounding note ends under its own id (the paired Note Off).
    m_playingNotes.flush([this](const VstEvent& noteOff) {
        m_inputEvents.addEvent(const_cast<VstEvent&>(noteOff));
    });

    for (PluginParamId id : m_playingParams) {
        auto infoIt = m_pluginParamInfoMap.find(id);
        if (infoIt == m_pluginParamInfoMap.end()) {
            continue;
        }

        ParamChangeEvent paramOff;
        paramOff.paramId = id;
        paramOff.value = infoIt->second.defaultNormalizedValue;

        addParamChange(paramOff);
    }

    m_playingParams.clear();
    restagePersistentParams();
}

VstNoteExpressionCapabilities VstAudioClient::noteExpressionCapabilities() const
{
    VstNoteExpressionCapabilities caps;
    if (!m_pluginPtr) {
        return caps;
    }
    PluginControllerPtr controller = m_pluginPtr->controller();
    if (!controller) {
        return caps;
    }
    Steinberg::FUnknownPtr<Steinberg::Vst::INoteExpressionController> nec(controller);
    if (!nec) {
        return caps;
    }
    bool hasNPer = false;
    bool hasNGen = false;
    int32_t nPerMin = 0, nPerSteps = 0, nGenMin = 0, nGenSteps = 0;
    const Steinberg::int32 count = nec->getNoteExpressionCount(0, 0);
    for (Steinberg::int32 i = 0; i < count; ++i) {
        Steinberg::Vst::NoteExpressionTypeInfo info;
        if (nec->getNoteExpressionInfo(0, 0, i, info) != Steinberg::kResultOk) {
            continue;
        }
        if (info.typeId == Steinberg::Vst::kTuningTypeID) {
            caps.tuning = true;
        } else if (info.typeId == MELO_NOTE_EXPRESSION_NPER || info.typeId == MELO_NOTE_EXPRESSION_NGEN) {
            // The plug-in declares the discrete-step domain: stepCount and
            // the encoding of coordinate 0 (its default) give min.
            const int32_t steps = info.valueDesc.stepCount;
            if (steps <= 0) {
                continue;
            }
            const int32_t min = -static_cast<int32_t>(std::lround(info.valueDesc.defaultValue * steps));
            if (info.typeId == MELO_NOTE_EXPRESSION_NPER) {
                hasNPer = true;
                nPerMin = min;
                nPerSteps = steps;
            } else {
                hasNGen = true;
                nGenMin = min;
                nGenSteps = steps;
            }
        }
    }
    if (hasNPer && hasNGen) {
        caps.meloLattice = true;
        caps.nPerMin = nPerMin;
        caps.nPerStepCount = nPerSteps;
        caps.nGenMin = nGenMin;
        caps.nGenStepCount = nGenSteps;
    }
    return caps;
}

audio::samples_t VstAudioClient::process(float* output, samples_t samplesPerChannel,
                                         samples_t playbackPositionSamples)
{
    IAudioProcessorPtr processor = pluginProcessor();
    if (!processor || !output) {
        return 0;
    }

    if (!m_isActive) {
        return 0;
    }

    //! NOTE: From the VST3 documentation:
    //!
    //! Note that the ProcessData->numSamples
    //! which indicates how many samples are used in a process call can change from call to call,
    //! but never bigger than the maxSamplesPerBlock
    m_processData.numSamples = samplesPerChannel;

    m_processContext.projectTimeSamples = playbackPositionSamples;

    if (samplesPerChannel > m_outputSpec.samplesPerChannel) {
        OutputSpec newSpec = m_outputSpec;
        newSpec.samplesPerChannel = samplesPerChannel;
        setOutputSpec(newSpec);
    }

    if (m_type == AudioPluginType::Fx) {
        extractInputSamples(samplesPerChannel, output);
    }

    if (processor->process(m_processData) != Steinberg::kResultOk) {
        EventTrace::instance().process(this, playbackPositionSamples, samplesPerChannel, 0.f, false);
        return 0;
    }

    if (m_persistentStateRefreshPending) {
        m_persistentStateRefreshPending = false;
        m_persistentStateDeliveredGeneration = m_persistentStateGeneration;
    }

    m_needUpdateState = false;

    m_inputEvents.clear();
    m_inputParamChanges.clearQueue();
    m_profileTransactionPending = false;
    if (m_type == AudioPluginType::Instrument) {
        fillOutputBufferInstrument(samplesPerChannel, output);
    } else {
        fillOutputBufferFx(samplesPerChannel, output);
    }

    if (EventTrace::instance().enabled()) {
        float peak = 0.f;
        const size_t count = static_cast<size_t>(samplesPerChannel) * m_outputSpec.audioChannelCount;
        for (size_t i = 0; i < count; ++i) {
            peak = std::max(peak, std::fabs(output[i]));
        }
        EventTrace::instance().process(this, playbackPositionSamples, samplesPerChannel, peak, true);
    }

    processOutputEvents();

    return samplesPerChannel;
}

ParamsMapping VstAudioClient::paramsMapping(const std::set<Steinberg::Vst::CtrlNumber>& controllers) const
{
    ParamsMapping result;

    if (!m_pluginPtr) {
        return result;
    }

    PluginMidiMappingPtr midiMapping = m_pluginPtr->midiMapping();
    if (!midiMapping) {
        return result;
    }

    for (const int busIdx : m_activeInputBusses) {
        for (const auto& ctrlNum : controllers) {
            PluginParamId id = 0;

            if (midiMapping->getMidiControllerAssignment(busIdx, 0, ctrlNum, id) != Steinberg::kResultOk) {
                continue;
            }

            result.emplace(ctrlNum, id);
        }
    }

    return result;
}

PluginParamId VstAudioClient::midiControllerParam(const Steinberg::Vst::CtrlNumber controller, const int16_t channel) const
{
    if (!m_pluginPtr) {
        return Steinberg::Vst::kNoParamId;
    }

    PluginMidiMappingPtr midiMapping = m_pluginPtr->midiMapping();
    if (!midiMapping) {
        return Steinberg::Vst::kNoParamId;
    }

    for (const int busIdx : m_activeInputBusses) {
        PluginParamId id = Steinberg::Vst::kNoParamId;
        if (midiMapping->getMidiControllerAssignment(busIdx, channel, controller, id) == Steinberg::kResultOk) {
            return id;
        }
    }
    return Steinberg::Vst::kNoParamId;
}

IAudioProcessorPtr VstAudioClient::pluginProcessor() const
{
    return static_cast<IAudioProcessorPtr>(pluginComponent());
}

PluginComponentPtr VstAudioClient::pluginComponent() const
{
    if (!m_pluginComponent) {
        if (!m_pluginPtr) {
            return nullptr;
        }

        m_pluginComponent = m_pluginPtr->component();
    }

    return m_pluginComponent;
}

void VstAudioClient::setUpProcessData()
{
    PluginComponentPtr component = pluginComponent();
    if (!component) {
        return;
    }

    m_processContext.sampleRate = m_outputSpec.sampleRate;
    m_processData.inputEvents = &m_inputEvents;
    m_processData.inputParameterChanges = &m_inputParamChanges;
    m_processData.outputEvents = &m_outputEvents;
    m_processData.processContext = &m_processContext;

    if (m_needUnprepareProcessData) {
        m_processData.unprepare();
        m_needUnprepareProcessData = false;
    }

    if (!m_processData.outputs || !m_processData.inputs) {
        m_processData.prepare(*component, m_outputSpec.samplesPerChannel, Steinberg::Vst::kSample32);
    }

    BusInfo busInfo;

    if (!m_eventInputBussesActivated) {
        const int eventInputCount = component->getBusCount(BusMediaType::kEvent, BusDirection::kInput);
        for (int busIndex = 0; busIndex < eventInputCount; ++busIndex) {
            component->getBusInfo(BusMediaType::kEvent, BusDirection::kInput, busIndex, busInfo);
            if (busInfo.busType == BusType::kMain && (busInfo.flags & BusInfo::kDefaultActive)) {
                component->activateBus(BusMediaType::kEvent, BusDirection::kInput, busIndex, true);
            }
        }
        m_eventInputBussesActivated = true;
    }

    if (!m_activeOutputBusses.empty() && !m_activeInputBusses.empty()) {
        return;
    }

    for (int busIndex = 0; busIndex < m_processData.numInputs; ++busIndex) {
        component->getBusInfo(BusMediaType::kAudio, BusDirection::kInput, busIndex, busInfo);

        if (busInfo.busType == BusType::kMain && (busInfo.flags & BusInfo::kDefaultActive)) {
            component->activateBus(BusMediaType::kAudio, BusDirection::kInput, busIndex, true);
            m_activeInputBusses.emplace_back(busIndex);
        }
    }

    for (int busIndex = 0; busIndex < m_processData.numOutputs; ++busIndex) {
        component->getBusInfo(BusMediaType::kAudio, BusDirection::kOutput, busIndex, busInfo);

        if (busInfo.busType == BusType::kMain && (busInfo.flags & BusInfo::kDefaultActive)) {
            component->activateBus(BusMediaType::kAudio, BusDirection::kOutput, busIndex, true);
            m_activeOutputBusses.emplace_back(busIndex);
        }

        LOGI() << "BusIndex: " << busIndex;

        if (busInfo.busType == BusType::kMain) {
            LOGI() << "BusType: Main";
        } else {
            LOGI() << "BusType: Aux";
        }

        if (busInfo.flags & BusInfo::kDefaultActive) {
            LOGI() << "BusFlag: DefaultActive";
        } else {
            LOGI() << "BusFlag: ControlVoltage";
        }
    }

    if (m_activeInputBusses.empty()) {
        LOGI() << "0 active input buses, activating default bus";
        component->activateBus(BusMediaType::kAudio, BusDirection::kInput, 0, true);
        m_activeInputBusses.emplace_back(0);
    }

    if (m_activeOutputBusses.empty()) {
        LOGI() << "0 active output buses, activating default bus";
        component->activateBus(BusMediaType::kAudio, BusDirection::kOutput, 0, true);
        m_activeOutputBusses.emplace_back(0);
    }
}

void VstAudioClient::updateProcessSetup()
{
    if (!m_outputSpec.isValid()) {
        return;
    }

    IAudioProcessorPtr processor = pluginProcessor();
    if (!processor) {
        return;
    }

    disableActivity();

    VstProcessSetup setup;
    setup.processMode = m_processMode;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.maxSamplesPerBlock = m_outputSpec.samplesPerChannel;
    setup.sampleRate = m_outputSpec.sampleRate;

    if (processor->setupProcessing(setup) != Steinberg::kResultOk) {
        return;
    }

    setUpProcessData();
    flushBuffers();

    if (!m_profileTransactionPending && m_profileHost.hasCurrentProfile()) {
        // setupProcessing resets the component's realtime contract. Restore
        // the last complete profile before processing can resume.
        deliverDynamicTonalityProfile(m_profileHost.currentProfile(), true);
    }
    ensureActivity();
}

void VstAudioClient::extractInputSamples(samples_t sampleCount, const float* sourceBuffer)
{
    if (!m_processData.inputs || !sourceBuffer) {
        return;
    }

    Steinberg::Vst::AudioBusBuffers& bus = m_processData.inputs[0];

    for (samples_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        size_t offset = sampleIndex * m_outputSpec.audioChannelCount;

        for (audioch_t audioChannelIndex = 0; audioChannelIndex < bus.numChannels; ++audioChannelIndex) {
            bus.channelBuffers32[audioChannelIndex][sampleIndex] = sourceBuffer[offset + audioChannelIndex];
        }
    }
}

void VstAudioClient::fillOutputBufferInstrument(samples_t sampleCount, float* output)
{
    if (!m_processData.outputs) {
        return;
    }

    for (const int busIndex : m_activeOutputBusses) {
        Steinberg::Vst::AudioBusBuffers bus = m_processData.outputs[busIndex];

        for (samples_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
            size_t offset = sampleIndex * m_outputSpec.audioChannelCount;

            for (audioch_t audioChannelIndex = 0; audioChannelIndex < bus.numChannels; ++audioChannelIndex) {
                float sample = bus.channelBuffers32[audioChannelIndex][sampleIndex];
                output[offset + audioChannelIndex] += sample * m_volumeGain;
            }
        }
    }
}

void VstAudioClient::fillOutputBufferFx(samples_t sampleCount, float* output)
{
    if (!m_processData.outputs) {
        return;
    }

    for (const int busIndex : m_activeOutputBusses) {
        Steinberg::Vst::AudioBusBuffers bus = m_processData.outputs[busIndex];

        for (samples_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
            size_t offset = sampleIndex * m_outputSpec.audioChannelCount;

            for (audioch_t audioChannelIndex = 0; audioChannelIndex < bus.numChannels; ++audioChannelIndex) {
                float sample = bus.channelBuffers32[audioChannelIndex][sampleIndex];
                output[offset + audioChannelIndex] = sample * m_volumeGain;
            }
        }
    }
}

void VstAudioClient::processOutputEvents()
{
    if (!m_mmcDecoder || !transportEventsDispatcher()) {
        return;
    }

    const int32_t count = m_outputEvents.getEventCount();
    if (count == 0) {
        return;
    }

    TransportEvents events;

    for (int32_t i = 0; i < count; ++i) {
        VstEvent vstEvent;
        if (m_outputEvents.getEvent(i, vstEvent) != Steinberg::kResultOk) {
            continue;
        }

        if (vstEvent.type != Steinberg::Vst::Event::kDataEvent
            || vstEvent.data.type != Steinberg::Vst::DataEvent::kMidiSysEx) {
            continue;
        }

        std::optional<MMCMessage> msg = m_mmcDecoder->decode(vstEvent.data.bytes, vstEvent.data.size);
        if (!msg.has_value()) {
            continue;
        }

        std::optional<TransportEvent> event = mmcToTransportEvent(m_mmcDecoder, msg.value());
        if (event.has_value()) {
            events.push_back(event.value());
        }
    }

    m_outputEvents.clear();

    if (!events.empty()) {
        transportEventsDispatcher()->dispatch(events);
    }
}

void VstAudioClient::ensureActivity()
{
    if (m_isActive) {
        return;
    }

    IAudioProcessorPtr processor = pluginProcessor();
    if (!processor) {
        return;
    }

    PluginComponentPtr component = pluginComponent();
    if (!component) {
        return;
    }

    component->setActive(true);
    processor->setProcessing(true);

    m_isActive = true;
}

void VstAudioClient::disableActivity()
{
    if (!m_isActive) {
        return;
    }

    IAudioProcessorPtr processor = pluginProcessor();
    if (!processor) {
        return;
    }

    PluginComponentPtr component = pluginComponent();
    if (!component) {
        return;
    }

    if (m_needUpdateState) {
        if (processor->process(m_processData) == Steinberg::kResultOk && m_persistentStateRefreshPending) {
            m_persistentStateRefreshPending = false;
            m_persistentStateDeliveredGeneration = m_persistentStateGeneration;
        }
        m_needUpdateState = false;
    }

    processor->setProcessing(false);
    component->setActive(false);

    m_isActive = false;
}

void VstAudioClient::flushBuffers()
{
    for (int inputsNumber = 0; inputsNumber < m_processData.numInputs; ++inputsNumber) {
        Steinberg::Vst::AudioBusBuffers input = m_processData.inputs[inputsNumber];

        for (int i = 0; i < m_processData.numSamples; ++i) {
            for (int audioChannel = 0; audioChannel < input.numChannels; ++audioChannel) {
                input.channelBuffers32[audioChannel][i] = 0.f;
            }
        }
    }

    for (int outputsNumber = 0; outputsNumber < m_processData.numOutputs; ++outputsNumber) {
        Steinberg::Vst::AudioBusBuffers output = m_processData.outputs[outputsNumber];

        for (int i = 0; i < m_processData.numSamples; ++i) {
            for (int audioChannel = 0; audioChannel < output.numChannels; ++audioChannel) {
                output.channelBuffers32[audioChannel][i] = 0.f;
            }
        }
    }
}

void VstAudioClient::addParamChange(const ParamChangeEvent& param)
{
    Steinberg::int32 dummyIdx = 0;
    Steinberg::Vst::IParamValueQueue* queue = m_inputParamChanges.addParameterData(param.paramId, dummyIdx);
    if (queue) {
        queue->addPoint(0, param.value, dummyIdx);
    }
}

void VstAudioClient::rememberPersistentParam(const ParamChangeEvent& param)
{
    const auto existing = std::find_if(m_persistentParams.begin(), m_persistentParams.end(),
                                       [&param](const ParamChangeEvent& value) {
        return value.paramId == param.paramId;
    });
    if (existing != m_persistentParams.end()) {
        existing->value = param.value;
        return;
    }
    m_persistentParams.push_back(param);
}

void VstAudioClient::restagePersistentParams()
{
    for (const ParamChangeEvent& param : m_persistentParams) {
        addParamChange(param);
    }
}

bool VstAudioClient::processIdleParamChange(const ParamChangeEvent& param)
{
    IAudioProcessorPtr processor = pluginProcessor();
    if (!processor) {
        return false;
    }

    // Do not consume the live render queue while the score is stopped. MeloPresto Synth
    // accepts zero-frame parameter-only calls and applies their events to its
    // state, so an editor close can serialize the new value without advancing
    // an audition block or discarding its note events.
    VstParameterChanges parameterOnlyChanges;
    parameterOnlyChanges.setMaxParameters(1);
    Steinberg::int32 pointIndex = 0;
    Steinberg::Vst::IParamValueQueue* queue = parameterOnlyChanges.addParameterData(param.paramId, pointIndex);
    if (!queue || queue->addPoint(0, param.value, pointIndex) != Steinberg::kResultTrue) {
        return false;
    }

    // HostProcessData owns its buffer arrays, so it cannot be copied for an
    // isolated flush. The processor consumes the base VST ProcessData API.
    Steinberg::Vst::ProcessData parameterOnlyData {};
    parameterOnlyData.numSamples = 0;
    parameterOnlyData.inputEvents = nullptr;
    parameterOnlyData.inputParameterChanges = &parameterOnlyChanges;
    parameterOnlyData.outputEvents = nullptr;
    parameterOnlyData.processContext = &m_processContext;

    if (processor->process(parameterOnlyData) != Steinberg::kResultOk) {
        return false;
    }
    return true;
}

bool VstAudioClient::isPlaying() const
{
    constexpr uint32_t playingFlag = static_cast<uint32_t>(VstProcessContext::kPlaying);
    return (m_processContext.state & playingFlag) != 0;
}
