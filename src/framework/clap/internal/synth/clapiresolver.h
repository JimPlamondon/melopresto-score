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

#include "modularity/ioc.h"
#include "audio/engine/isynthresolver.h"

#include "../../iclapinstancesregister.h"
#include "../../iclapmodulesrepository.h"
#include "clapsynthesiser.h"

namespace muse::clap {
//! Resolves CLAP instruments for the audio engine: the counterpart of VstiResolver.
class ClapiResolver : public audio::synth::ISynthResolver::IResolver, public Contextable
{
    ContextInject<IClapModulesRepository> pluginModulesRepo = { this };
    ContextInject<IClapInstancesRegister> instancesRegister = { this };
public:
    ClapiResolver(const modularity::ContextPtr& iocCtx)
        : Contextable(iocCtx) {}

    muse::audio::synth::ISynthesizerPtr resolveSynth(const audio::TrackId trackId, const audio::AudioInputParams& params,
                                                     const audio::OutputSpec& outputSpec) const override;
    bool hasCompatibleResources(const muse::audio::PlaybackSetupData& setup) const override;
    muse::audio::AudioResourceMetaList resolveResources() const override;
    muse::audio::SoundPresetList resolveSoundPresets(const muse::audio::AudioResourceMeta& resourceMeta) const override;
    void refresh() override;
    void clearSources() override;
};
}
