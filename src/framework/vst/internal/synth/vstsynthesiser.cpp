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
#include "vstsynthesiser.h"

#include "audio/common/audiosanitizer.h"

#include "log.h"

#include <cmath>

using namespace muse;
using namespace muse::vst;
using namespace muse::audio::synth;
using namespace muse::audio;
using namespace muse::audioplugins;

//! The folder of MeloPresto receiver profiles, beside the application's other data.
static constexpr std::string_view MELO_PROFILES_FOLDER = "melo_profiles";

static const std::set<Steinberg::Vst::CtrlNumber> SUPPORTED_CONTROLLERS = {
    Steinberg::Vst::kCtrlVolume,
    Steinberg::Vst::kCtrlExpression,
    Steinberg::Vst::kCtrlSustainOnOff,
    Steinberg::Vst::kCtrlSustenutoOnOff,
    Steinberg::Vst::kPitchBend,
};

static const mpe::DynamicTonalityProfileEvent* firstDynamicTonalityProfile(const mpe::PlaybackData& data)
{
    for (const auto& [_, events] : data.originEvents) {
        for (const mpe::PlaybackEvent& event : events) {
            if (std::holds_alternative<mpe::DynamicTonalityProfileEvent>(event)) {
                return &std::get<mpe::DynamicTonalityProfileEvent>(event);
            }
        }
    }
    return nullptr;
}

VstSynthesiser::VstSynthesiser(const TrackId trackId, const muse::audio::AudioInputParams& params,
                               const modularity::ContextPtr& iocCtx)
    : AbstractSynthesizer(params, iocCtx),
    m_vstAudioClient(std::make_unique<VstAudioClient>(iocCtx)),
    m_trackId(trackId)
{
}

VstSynthesiser::~VstSynthesiser()
{
    m_pendingHostParameters.clear();
    instancesRegister()->unregisterInstrPlugin(m_params.resourceMeta.id, m_trackId);
}

void VstSynthesiser::init(const OutputSpec& spec)
{
    IF_ASSERT_FAILED(spec.isValid()) {
        return;
    }

    m_outputSpec = spec;

    m_pluginPtr = instancesRegister()->makeAndRegisterInstrPlugin(m_params.resourceMeta.id, m_trackId);

    m_vstAudioClient->init(AudioPluginType::Instrument, m_pluginPtr);

    // Subscribe before loading or replaying values. A synchronous load or an
    // idle replay may request a config scan immediately.
    m_pluginPtr->pluginSettingsChanged().onReceive(this, [this](const muse::audio::AudioUnitConfig& newConfig) {
        if (m_params.configuration == newConfig) {
            return;
        }

        m_params.configuration = newConfig;
        m_paramsChanges.send(m_params);
    });

    // Hand the saved configuration (component/controller state) over now: a
    // not-yet-loaded instance applies it inside load() before it reports
    // completion, so the plug-in starts in its saved state; an already
    // loaded one applies it right away.
    m_pluginPtr->updatePluginConfig(m_params.configuration);

    auto onPluginLoaded = [this]() {
        if (!m_pluginPtr->isLoaded()) {
            // loadingCompleted fired for a failed load: nothing will ever be
            // set up, so stop waiters from waiting on this track.
            m_loadFailed = true;
            m_pendingHostParameters.clear();
            m_readyToPlayChanged.notify();
            return;
        }
        m_vstAudioClient->loadSupportedParams();
        if (const auto* profile = firstDynamicTonalityProfile(m_sequencer.playbackData())) {
            m_vstAudioClient->handleDynamicTonalityProfile(*profile);
        }
        m_vstAudioClient->setOutputSpec(m_outputSpec);
        const VstNoteExpressionCapabilities noteExpressionCapabilities = m_vstAudioClient->noteExpressionCapabilities();
        m_sequencer.init(m_vstAudioClient->paramsMapping(SUPPORTED_CONTROLLERS), m_useDynamicEvents,
                         noteExpressionCapabilities);
        // One selection for this plug-in, made by the MeloPresto shared code
        // from its receiver profile, if one matches the name and version the
        // plug-in states, and from what the plug-in itself lists.
        VstPerNotePitchConfig pitchConfig;
        pitchConfig.pluginName = m_pluginPtr->className();
        pitchConfig.pluginVersion = m_pluginPtr->classVersion();
        pitchConfig.profilesFolder = (globalConfiguration()->appDataPath() + MELO_PROFILES_FOLDER.data()).toStdString();
        pitchConfig.advertisesTuningExpression = noteExpressionCapabilities.tuning;
        for (int16_t channel = 0; channel < 16; ++channel) {
            for (size_t controller = 0; controller < VST_MIDI_CONTROLLER_COUNT; ++controller) {
                pitchConfig.midiParams[channel][controller]
                    = m_vstAudioClient->midiControllerParam(static_cast<Steinberg::Vst::CtrlNumber>(controller), channel);
            }
        }
        const std::string pitchProblem = m_perNotePitchAdapter.configure(pitchConfig);
        if (!pitchProblem.empty()) {
            LOGW() << "MeloPresto pitch path for " << m_params.resourceMeta.id << ": " << pitchProblem;
        }
        LOGI() << "MeloPresto pitch path for " << m_params.resourceMeta.id << ": mode "
               << static_cast<int>(m_perNotePitchAdapter.mode()) << ", profile \"" << m_perNotePitchAdapter.profileId() << "\"";
        m_pluginPtr->pluginParamChanged().onReceive(this, [this](PluginParamId id, PluginParamValue normalized,
                                                                 PluginParamChangeGeneration generation) {
            // Editor values are normalized already. Treat them as score-owned
            // state so a stop flush cannot restore MeloPresto Synth's default.
            if (m_vstAudioClient->handlePersistentParamChange({ id, normalized })) {
                // This exact main-thread edit may be acknowledged only after
                // the audio client reports its processor delivery.
                m_pendingPersistentEditorGeneration = generation;
                requestConfigRefreshAfterPersistentDelivery();
            } else if (m_pendingPersistentEditorGeneration) {
                // A newer refused edit must wait for an earlier accepted edit
                // still in the processor queue; that delivery will acknowledge
                // the newest editor generation.
                m_pendingPersistentEditorGeneration = generation;
            } else {
                // No processor delivery is outstanding, so this editor state
                // can be serialized immediately and clear its edit token.
                m_pluginPtr->requestConfigRefresh(generation);
            }
        });
        m_inited = true;
        replayPendingHostParameters();
        m_readyToPlayChanged.notify();
    };

    if (m_pluginPtr->isLoaded()) {
        onPluginLoaded();
    } else {
        m_pluginPtr->loadingCompleted().onNotify(this, onPluginLoaded);
    }

    m_sequencer.setOnOffStreamFlushed([this]() {
        m_vstAudioClient->flushSound();
    });
}

void VstSynthesiser::updateRenderingMode(const RenderMode mode)
{
    if (mode == RenderMode::OfflineMode) {
        m_vstAudioClient->setProcessMode(VstProcessMode::kOffline);
    } else {
        m_vstAudioClient->setProcessMode(VstProcessMode::kRealtime);
    }
}

void VstSynthesiser::toggleVolumeGain(const bool isActive)
{
    static constexpr muse::audio::gain_t NON_ACTIVE_GAIN = 0.5f;

    if (isActive) {
        m_vstAudioClient->setVolumeGain(m_sequencer.currentGain());
    } else {
        m_vstAudioClient->setVolumeGain(NON_ACTIVE_GAIN);
    }
}

bool VstSynthesiser::isValid() const
{
    if (!m_pluginPtr) {
        return false;
    }

    return m_pluginPtr->isLoaded();
}

bool VstSynthesiser::setHostParameterPlain(uint32_t paramId, double plain)
{
    ONLY_AUDIO_ENGINE_THREAD;
    if (!std::isfinite(plain)) {
        return false;
    }
    if (!m_inited) {
        return !m_loadFailed && m_pendingHostParameters.enqueue(paramId, plain);
    }
    const bool applied = applyHostParameterPlain(paramId, plain);
    if (applied) {
        requestConfigRefreshAfterPersistentDelivery();
    }
    return applied;
}

bool VstSynthesiser::applyHostParameterPlain(uint32_t paramId, double plain)
{
    PluginControllerPtr controller = m_pluginPtr ? m_pluginPtr->controller() : nullptr;
    if (!controller) {
        return false;
    }
    const double normalized = controller->plainParamToNormalized(paramId, plain);
    if (!std::isfinite(normalized) || normalized < 0.0 || normalized > 1.0
        || controller->setParamNormalized(paramId, normalized) != Steinberg::kResultOk) {
        return false;
    }
    return m_vstAudioClient->handlePersistentParamChange(ParamChangeEvent { paramId, normalized });
}

void VstSynthesiser::replayPendingHostParameters()
{
    bool appliedAny = false;
    m_pendingHostParameters.replay([this, &appliedAny](uint32_t paramId, double plain) {
        appliedAny = applyHostParameterPlain(paramId, plain) || appliedAny;
    });
    if (appliedAny) {
        requestConfigRefreshAfterPersistentDelivery();
    }
}

bool VstSynthesiser::readyToPlay() const
{
    ONLY_AUDIO_ENGINE_THREAD;

    return m_inited || m_loadFailed || !m_pluginPtr;
}

muse::audio::AudioSourceType VstSynthesiser::type() const
{
    return m_params.type();
}

std::string VstSynthesiser::name() const
{
    if (!m_pluginPtr) {
        return std::string();
    }

    return m_pluginPtr->name();
}

void VstSynthesiser::flushSound()
{
    m_sequencer.flushOffstream();
    m_perNotePitchAdapter.reset();
    m_deferredPitchParamCount = 0;
    m_vstAudioClient->flushSound();
}

void VstSynthesiser::setupSound(const mpe::PlaybackSetupData& setupData)
{
    m_useDynamicEvents = setupData.supportsSingleNoteDynamics;
}

void VstSynthesiser::setupEvents(const mpe::PlaybackData& playbackData)
{
    m_sequencer.load(playbackData);
    if (m_inited) {
        if (const auto* profile = firstDynamicTonalityProfile(playbackData)) {
            m_vstAudioClient->handleDynamicTonalityProfile(*profile);
        }
    }
}

const mpe::PlaybackData& VstSynthesiser::playbackData() const
{
    return m_sequencer.playbackData();
}

bool VstSynthesiser::isActive() const
{
    return m_sequencer.isActive();
}

void VstSynthesiser::setIsActive(const bool isActive)
{
    if (m_sequencer.isActive() == isActive) {
        return;
    }

    m_sequencer.setActive(isActive);
    toggleVolumeGain(isActive);
    m_vstAudioClient->setIsPlaying(isActive);
    m_vstAudioClient->setIsActive(isActive);
    requestConfigRefreshAfterPersistentDelivery();
    if (isActive) {
        configureMpeInput();
    }
}

void VstSynthesiser::requestConfigRefreshAfterPersistentDelivery()
{
    if (!m_pluginPtr || !m_vstAudioClient->takePersistentStateRefreshRequest()) {
        return;
    }

    if (m_pendingPersistentEditorGeneration) {
        m_pluginPtr->requestConfigRefresh(*m_pendingPersistentEditorGeneration);
        m_pendingPersistentEditorGeneration.reset();
    } else {
        m_pluginPtr->requestConfigRefresh();
    }
}

void VstSynthesiser::configureMpeInput()
{
    // The writes that prepare the plug-in come from the shared projector:
    // the MPE configuration message only for a plug-in that takes it, the
    // bend-range message only where the range is settable. They are
    // delivered by processSequence.
    m_pitchSetup = m_perNotePitchAdapter.setup();
    m_pitchSetupSent = 0;
}

muse::audio::msecs_t VstSynthesiser::playbackPosition() const
{
    return m_sequencer.playbackPosition();
}

void VstSynthesiser::setPlaybackPosition(const muse::audio::msecs_t newPosition)
{
    m_sequencer.setPlaybackPosition(newPosition);
    m_currentPositionSamples = microSecsToSamples(newPosition, m_outputSpec.sampleRate);

    if (isActive()) {
        m_vstAudioClient->setVolumeGain(m_sequencer.currentGain());
    }
}

void VstSynthesiser::setOutputSpec(const audio::OutputSpec& spec)
{
    m_outputSpec = spec;
    m_currentPositionSamples = microSecsToSamples(m_sequencer.playbackPosition(), m_outputSpec.sampleRate);

    if (m_inited) {
        m_vstAudioClient->setOutputSpec(spec);
        requestConfigRefreshAfterPersistentDelivery();
    }
}

unsigned int VstSynthesiser::audioChannelsCount() const
{
    return m_outputSpec.audioChannelCount;
}

async::Channel<unsigned int> VstSynthesiser::audioChannelsCountChanged() const
{
    return m_streamsCountChanged;
}

samples_t VstSynthesiser::process(float* buffer, samples_t samplesPerChannel)
{
    m_noteEventBridge.clear();

    if (!buffer) {
        return 0;
    }

    const msecs_t nextMsecs = samplesToMsecs(samplesPerChannel, m_outputSpec.sampleRate);
    const VstSequencer::EventSequenceMap sequences = m_sequencer.movePlaybackForward(nextMsecs);
    const bool active = m_sequencer.isActive();

    samples_t sampleOffset = 0;
    samples_t processedSamples = 0;

    for (auto it = sequences.cbegin(); it != sequences.cend(); ++it) {
        samples_t durationInSamples = samplesPerChannel - sampleOffset;

        auto nextIt = std::next(it);
        if (nextIt != sequences.cend()) {
            msecs_t duration = nextIt->first - it->first;
            durationInSamples = microSecsToSamples(duration, m_outputSpec.sampleRate);
        }

        IF_ASSERT_FAILED(sampleOffset + durationInSamples <= samplesPerChannel) {
            break;
        }

        processedSamples += processSequence(it->second, sampleOffset, durationInSamples,
                                            buffer + sampleOffset * m_outputSpec.audioChannelCount);
        sampleOffset += durationInSamples;

        if (active) {
            m_currentPositionSamples += durationInSamples;
        }
    }

    return processedSamples;
}

const AudioNoteEvents& VstSynthesiser::noteEvents() const
{
    return m_noteEventBridge.events();
}

samples_t VstSynthesiser::processSequence(const VstSequencer::EventSequence& sequence, const samples_t sequenceSampleOffset,
                                          const samples_t samples, float* buffer)
{
    // A plug-in may keep only the last write to a parameter in one block, so a
    // block carries one preparation write at most per parameter.
    const size_t setupBlockStart = m_pitchSetupSent;
    while (m_pitchSetupSent < m_pitchSetup.count) {
        const ParamChangeEvent& next = m_pitchSetup.params[m_pitchSetupSent];
        bool repeated = false;
        for (size_t i = setupBlockStart; i < m_pitchSetupSent; ++i) {
            repeated = repeated || m_pitchSetup.params[i].paramId == next.paramId;
        }
        if (repeated) {
            break;
        }
        m_vstAudioClient->handleParamChange(next);
        ++m_pitchSetupSent;
    }

    // Pitch writes whose note-on was rendered by the previous block open this one.
    size_t waiting = 0;
    for (size_t i = 0; i < m_deferredPitchParamCount; ++i) {
        if (m_deferredPitchParams[i].noteOnProcessed) {
            m_vstAudioClient->handleParamChange(m_deferredPitchParams[i].change);
        } else {
            m_deferredPitchParams[waiting++] = m_deferredPitchParams[i];
        }
    }
    m_deferredPitchParamCount = waiting;

    for (const VstSequencer::EventType& event : sequence) {
        if (std::holds_alternative<mpe::DynamicTonalityProfileEvent>(event)) {
            m_vstAudioClient->handleDynamicTonalityProfile(std::get<mpe::DynamicTonalityProfileEvent>(event));
        } else if (std::holds_alternative<VstEvent>(event)) {
            const VstEvent& vstEvent = std::get<VstEvent>(event);
            const VstPerNotePitchOutput adapted = m_perNotePitchAdapter.adapt(vstEvent);
            bool accepted = false;
            for (size_t i = 0; i < adapted.paramCount; ++i) {
                if (!adapted.params[i].afterNoteOn) {
                    m_vstAudioClient->handleParamChange(adapted.params[i].change);
                }
            }
            for (size_t i = 0; i < adapted.eventCount; ++i) {
                accepted = m_vstAudioClient->handleEvent(adapted.events[i]) || accepted;
            }
            for (size_t i = 0; i < adapted.paramCount; ++i) {
                if (adapted.params[i].afterNoteOn && m_deferredPitchParamCount < m_deferredPitchParams.size()) {
                    m_deferredPitchParams[m_deferredPitchParamCount++] = { adapted.params[i].change, false };
                }
            }
            if (accepted) {
                // Effects receive MuseScore's canonical note, including its
                // exact MeloPresto tuning, not the instrument-specific adaptation.
                m_noteEventBridge.capture(vstEvent, sequenceSampleOffset);
            }
        } else if (std::holds_alternative<ParamChangeEvent>(event)) {
            m_vstAudioClient->handleParamChange(std::get<ParamChangeEvent>(event));
        } else {
            muse::audio::gain_t newGain = std::get<muse::audio::gain_t>(event);
            m_vstAudioClient->setVolumeGain(newGain);
        }
    }

    if (samples == 0) {
        return 0;
    }

    // A pitch write that follows its note-on goes one sample after it. In a
    // block of one sample it waits for the next block's first sample.
    waiting = 0;
    for (size_t i = 0; i < m_deferredPitchParamCount; ++i) {
        if (samples >= 2) {
            m_vstAudioClient->handleParamChange(m_deferredPitchParams[i].change, 1);
        } else {
            m_deferredPitchParams[waiting] = m_deferredPitchParams[i];
            m_deferredPitchParams[waiting++].noteOnProcessed = true;
        }
    }
    m_deferredPitchParamCount = waiting;

    const samples_t processed = m_vstAudioClient->process(buffer, samples, m_currentPositionSamples);
    requestConfigRefreshAfterPersistentDelivery();
    return processed;
}
