/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "clapiresolver.h"

#include "log.h"

using namespace muse::clap;
using namespace muse::audio;
using namespace muse::audio::synth;

ISynthesizerPtr ClapiResolver::resolveSynth(const audio::TrackId trackId, const audio::AudioInputParams& params,
                                            const audio::OutputSpec& outputSpec) const
{
    // A synthesiser is made even for a plug-in that is not installed. The
    // audio engine gives a track with no synthesiser the default sound and
    // reports that sound as the track's, which would replace the plug-in and
    // its state saved in the score. A CLAP track whose plug-in is missing
    // keeps both and is silent; the plug-in instance says so in the log.
    if (!pluginModulesRepo()->exists(params.resourceMeta.id)) {
        LOGE() << "CLAP plug-in " << params.resourceMeta.id << " is not among the scanned plug-ins";
    }

    auto synth = std::make_shared<ClapSynthesiser>(trackId, params, iocContext());
    synth->init(outputSpec);
    return synth;
}

bool ClapiResolver::hasCompatibleResources(const muse::audio::PlaybackSetupData& /*setup*/) const
{
    return true;
}

void ClapiResolver::refresh()
{
}

AudioResourceMetaList ClapiResolver::resolveResources() const
{
    return pluginModulesRepo()->instrumentModulesMeta();
}

SoundPresetList ClapiResolver::resolveSoundPresets(const muse::audio::AudioResourceMeta&) const
{
    return SoundPresetList();
}

void ClapiResolver::clearSources()
{
    instancesRegister()->unregisterAllInstrPlugin();
}
