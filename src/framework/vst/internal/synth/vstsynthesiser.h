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

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "audio/engine/internal/synthesizers/abstractsynthesizer.h"
#include "audio/engine/iaudioengineconfiguration.h"
#include "audio/common/audiotypes.h"
#include "global/iglobalconfiguration.h"
#include "modularity/ioc.h"
#include "mpe/events.h"

#include "../vstaudioclient.h"
#include "../vstnoteeventbridge.h"
#include "../vstpernotepitchadapter.h"
#include "../../ivstinstancesregister.h"
#include "vstsequencer.h"
#include "vsttypes.h"

namespace muse::vst {
namespace detail {
// Loading a VST3 happens asynchronously. Keep only the latest requested
// value for each host parameter while that one synth instance is loading;
// this is state, not an automation-event history.
class PendingHostParameterQueue
{
public:
    static constexpr size_t MAX_PARAMETERS = 32;

    bool enqueue(uint32_t paramId, double plain)
    {
        if (!std::isfinite(plain)) {
            return false;
        }
        for (size_t i = 0; i < m_size; ++i) {
            if (m_values[i].paramId == paramId) {
                m_values[i].plain = plain;
                return true;
            }
        }
        if (m_size == m_values.size()) {
            return false;
        }
        m_values[m_size++] = { paramId, plain };
        return true;
    }

    template<typename Apply>
    void replay(Apply apply)
    {
        const std::array<Value, MAX_PARAMETERS> values = m_values;
        const size_t size = m_size;
        clear();
        for (size_t i = 0; i < size; ++i) {
            apply(values[i].paramId, values[i].plain);
        }
    }

    void clear()
    {
        m_size = 0;
    }

    size_t size() const
    {
        return m_size;
    }

private:
    struct Value {
        uint32_t paramId = 0;
        double plain = 0.0;
    };
    std::array<Value, MAX_PARAMETERS> m_values {};
    size_t m_size = 0;
};
}

class VstSynthesiser : public muse::audio::synth::AbstractSynthesizer
{
    GlobalInject<audio::engine::IAudioEngineConfiguration> config;
    GlobalInject<IGlobalConfiguration> globalConfiguration;
    ContextInject<IVstInstancesRegister> instancesRegister = { this };

public:
    explicit VstSynthesiser(const muse::audio::TrackId trackId, const muse::audio::AudioInputParams& params,
                            const modularity::ContextPtr& iocCtx);
    ~VstSynthesiser() override;

    void init(const audio::OutputSpec& spec);

    bool isValid() const override;
    bool setHostParameterPlain(uint32_t paramId, double plain) override;

    //! MeloSynth VST3 workstream: a VST instrument is ready to play only once
    //! its module has loaded and the audio client/sequencer have been set up
    //! on the audio-engine thread (or the load has failed for good), so play
    //! and offline export wait for it instead of dropping the first notes or
    //! rendering silence while the plug-in is still loading.
    bool readyToPlay() const override;

    muse::audio::AudioSourceType type() const override;
    std::string name() const override;

    void flushSound() override;

    void setupSound(const mpe::PlaybackSetupData& setupData) override;
    void setupEvents(const mpe::PlaybackData& playbackData) override;
    const mpe::PlaybackData& playbackData() const override;

    bool isActive() const override;
    void setIsActive(const bool isActive) override;

    muse::audio::msecs_t playbackPosition() const override;
    void setPlaybackPosition(const muse::audio::msecs_t newPosition) override;

    // IAudioSource
    void setOutputSpec(const audio::OutputSpec& spec) override;
    unsigned int audioChannelsCount() const override;
    async::Channel<unsigned int> audioChannelsCountChanged() const override;
    muse::audio::samples_t process(float* buffer, muse::audio::samples_t samplesPerChannel) override;
    const muse::audio::AudioNoteEvents& noteEvents() const override;

private:
    void updateRenderingMode(const audio::RenderMode mode) override;

    void toggleVolumeGain(const bool isActive);
    void configureMpeInput();
    void requestConfigRefreshAfterPersistentDelivery();
    bool applyHostParameterPlain(uint32_t paramId, double plain);
    void replayPendingHostParameters();
    audio::samples_t processSequence(const VstSequencer::EventSequence& sequence, const audio::samples_t sequenceSampleOffset,
                                     const audio::samples_t samples, float* buffer);

    IVstPluginInstancePtr m_pluginPtr = nullptr;
    std::unique_ptr<VstAudioClient> m_vstAudioClient = nullptr;

    audio::OutputSpec m_outputSpec;
    async::Channel<unsigned int> m_streamsCountChanged;

    VstSequencer m_sequencer;
    VstNoteEventBridge m_noteEventBridge;
    VstPerNotePitchAdapter m_perNotePitchAdapter;

    muse::audio::TrackId m_trackId = muse::audio::INVALID_TRACK_ID;

    bool m_inited = false;
    bool m_loadFailed = false;
    detail::PendingHostParameterQueue m_pendingHostParameters;
    std::optional<PluginParamChangeGeneration> m_pendingPersistentEditorGeneration;
    bool m_useDynamicEvents = false;

    // What prepares the plug-in for its pitch path, drained over successive
    // blocks: one write at most per parameter in a block.
    VstPerNotePitchSetup m_pitchSetup;
    size_t m_pitchSetupSent = 0;

    // Pitch writes that follow a note-on by one sample. `noteOnProcessed`
    // says the note-on's block has already been rendered.
    struct DeferredPitchParam {
        ParamChangeEvent change {};
        bool noteOnProcessed = false;
    };
    std::array<DeferredPitchParam, 32> m_deferredPitchParams {};
    size_t m_deferredPitchParamCount = 0;

    audio::samples_t m_currentPositionSamples = 0;
};

using VstSynthPtr = std::shared_ptr<VstSynthesiser>;
}
