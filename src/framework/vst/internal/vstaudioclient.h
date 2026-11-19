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
#pragma once

#include <cstdint>

#include "audioplugins/audiopluginstypes.h"

#include "../ivstplugininstance.h"
#include "../vsttypes.h"
#include "vstactivenotes.h"
#include "vstdynamictonalityprofile.h"

#include "modularity/ioc.h"
#include "audio/engine/itransporteventsdispatcher.h"
#include "midiremote/immcdecoderfactory.h"

namespace muse::vst {
class VstAudioClient : public muse::Contextable
{
    muse::ContextInject<muse::audio::engine::ITransportEventsDispatcher> transportEventsDispatcher = { this };
    muse::GlobalInject<muse::midiremote::IMMCDecoderFactory> mmcDecoderFactory;

public:
    VstAudioClient(const modularity::ContextPtr& iocCtx);
    ~VstAudioClient();

    void init(audioplugins::AudioPluginType type, IVstPluginInstancePtr instance);
    void loadSupportedParams();

    void setIsActive(const bool isActive);
    void setIsPlaying(const bool isPlaying);
    void setOutputSpec(const audio::OutputSpec& spec);
    void setProcessMode(VstProcessMode mode);
    void setVolumeGain(const muse::audio::gain_t newVolumeGain);

    bool handleEvent(const VstEvent& event);
    bool handleParamChange(const ParamChangeEvent& param);
    // A score-owned value must survive transport flushes. Unlike automation
    // and note-expression parameter events, it is not reset to the plug-in
    // default when playback stops.
    bool handlePersistentParamChange(const ParamChangeEvent& param);
    // Returns true once a score-owned editor value has been accepted by the
    // processor and a component-state scan can safely serialize it.
    bool takePersistentStateRefreshRequest();
    bool handleDynamicTonalityProfile(const mpe::DynamicTonalityProfileEvent& profile, bool force = false);
    bool hasDynamicTonalityProfile() const { return m_profileHost.hasCurrentProfile(); }

    void flushSound();

    audio::samples_t process(float* output, audio::samples_t samplesPerChannel, audio::samples_t playbackPositionSamples = 0);

    ParamsMapping paramsMapping(const std::set<Steinberg::Vst::CtrlNumber>& controllers) const;
    PluginParamId midiControllerParam(Steinberg::Vst::CtrlNumber controller, int16_t channel) const;

    //! MeloSynth VST3 workstream: what the plug-in advertises through
    //! INoteExpressionController on event bus 0 / channel 0 (standard
    //! per-note tuning, and both frozen JiMS lattice types with their
    //! declared discrete-step domains). Absent interface → everything false.
    VstNoteExpressionCapabilities noteExpressionCapabilities() const;

private:
    IAudioProcessorPtr pluginProcessor() const;
    PluginComponentPtr pluginComponent() const;

    void setUpProcessData();
    void updateProcessSetup();
    void extractInputSamples(muse::audio::samples_t sampleCount, const float* sourceBuffer);

    void fillOutputBufferInstrument(muse::audio::samples_t sampleCount, float* output);
    void fillOutputBufferFx(muse::audio::samples_t sampleCount, float* output);

    void processOutputEvents();

    void ensureActivity();
    void disableActivity();

    void flushBuffers();

    void addParamChange(const ParamChangeEvent& param);
    void rememberPersistentParam(const ParamChangeEvent& param);
    void restagePersistentParams();
    bool processIdleParamChange(const ParamChangeEvent& param);
    bool isPlaying() const;
    bool deliverDynamicTonalityProfile(const mpe::DynamicTonalityProfileEvent& profile, bool force);

    bool m_isActive = false;
    muse::audio::gain_t m_volumeGain = 1.f; // 0.0 - 1.0

    IVstPluginInstancePtr m_pluginPtr = nullptr;
    mutable PluginComponentPtr m_pluginComponent = nullptr;

    std::vector<int> m_activeOutputBusses;
    std::vector<int> m_activeInputBusses;
    bool m_eventInputBussesActivated = false;

    VstEventList m_inputEvents;
    VstParameterChanges m_inputParamChanges;
    VstEventList m_outputEvents;
    VstProcessData m_processData;
    VstProcessContext m_processContext;
    VstProcessMode m_processMode = VstProcessMode::kRealtime;

    VstActiveNotes m_playingNotes;
    std::vector<PluginParamId> m_playingParams;
    std::vector<ParamChangeEvent> m_persistentParams;
    bool m_persistentStateRefreshPending = false;
    uint64_t m_persistentStateGeneration = 0;
    uint64_t m_persistentStateDeliveredGeneration = 0;
    uint64_t m_persistentStateRefreshRequestedGeneration = 0;

    std::unordered_map<PluginParamId, PluginParamInfo> m_pluginParamInfoMap;
    VstDynamicTonalityProfileHost m_profileHost;
    bool m_profileTransactionPending = false;

    bool m_needUnprepareProcessData = false;
    bool m_needUpdateState = false;

    audioplugins::AudioPluginType m_type = audioplugins::AudioPluginType::Undefined;
    audio::OutputSpec m_outputSpec;

    midiremote::IMMCDecoderPtr m_mmcDecoder;
};
}
