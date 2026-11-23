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

#include <memory>

#include "audio/engine/internal/synthesizers/abstractsynthesizer.h"
#include "audio/common/audiotypes.h"
#include "global/iglobalconfiguration.h"
#include "modularity/ioc.h"
#include "mpe/events.h"

#include "../../iclapinstancesregister.h"
#include "../clappitchedoutput.h"
#include "clapsequencer.h"

namespace muse::clap {
//! Plays a track through one CLAP plug-in: the counterpart of VstSynthesiser.
//!
//! The plug-in is loaded on the main thread. When it has loaded, this class
//! has the MeloPresto shared projector select the pitch path, sends what
//! prepares the plug-in for that path, and reports itself ready to play only
//! once the main thread has served what the plug-in asked of it.
class ClapSynthesiser : public muse::audio::synth::AbstractSynthesizer
{
    GlobalInject<IGlobalConfiguration> globalConfiguration;
    ContextInject<IClapInstancesRegister> instancesRegister = { this };

public:
    explicit ClapSynthesiser(const muse::audio::TrackId trackId, const muse::audio::AudioInputParams& params,
                             const modularity::ContextPtr& iocCtx);
    ~ClapSynthesiser() override;

    void init(const audio::OutputSpec& spec);

    bool isValid() const override;
    bool setHostParameterPlain(uint32_t paramId, double plain) override;
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
    void onPluginLoaded();
    void prepareAndSettle();
    void toggleVolumeGain(const bool isActive);
    audio::samples_t processSequence(const ClapSequencer::EventSequence& sequence, const audio::samples_t sequenceSampleOffset,
                                     const audio::samples_t samples, float* buffer);
    void captureNote(const ClapNoteEvent& note, const audio::samples_t sequenceSampleOffset);

    IClapPluginInstancePtr m_pluginPtr = nullptr;
    ClapPitchedOutput m_output;
    ClapSequencer m_sequencer;

    audio::OutputSpec m_outputSpec;
    async::Channel<unsigned int> m_streamsCountChanged;
    //! The notes of the block as the score states them, for effects after this instrument.
    muse::audio::AudioNoteEvents m_noteEvents;

    muse::audio::TrackId m_trackId = muse::audio::INVALID_TRACK_ID;

    bool m_inited = false;
    bool m_settled = false;
    bool m_loadFailed = false;
    bool m_useDynamicEvents = false;
};

using ClapSynthPtr = std::shared_ptr<ClapSynthesiser>;
}
