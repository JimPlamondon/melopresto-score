/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "clappluginmetareader.h"

#include "../claperrors.h"
#include "../claptypes.h"
#include "clapbundle.h"
#include "claphostedplugin.h"

#include "log.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::clap;

audio::AudioResourceType ClapPluginMetaReader::metaType() const
{
    return audio::AudioResourceType::ClapPlugin;
}

bool ClapPluginMetaReader::canReadMeta(const io::path_t& pluginPath) const
{
    return io::suffix(pluginPath) == std::string(CLAP_PACKAGE_EXTENSION);
}

RetVal<AudioResourceMetaList> ClapPluginMetaReader::readMeta(const io::path_t& pluginPath) const
{
    std::string problem;
    const ClapBundlePtr bundle = ClapBundle::open(pluginPath.toStdString(), problem);
    if (!bundle) {
        LOGE() << "CLAP " << pluginPath << ": " << problem;
        return clap::make_ret(Err::NoBundle);
    }

    AudioResourceMetaList result;
    for (const ClapDescriptorInfo& descriptor : bundle->descriptors()) {
        if (!descriptor.hasFeature(CLAP_PLUGIN_FEATURE_INSTRUMENT)) {
            continue;
        }

        AudioResourceMeta meta;
        meta.id = descriptor.id;
        meta.type = AudioResourceType::ClapPlugin;
        meta.vendor = descriptor.vendor;
        // The register sorts a plug-in as an instrument or an effect by this text.
        meta.attributes.emplace(audio::CATEGORIES_ATTRIBUTE, String(u"Instrument"));
        meta.attributes.emplace(String(CLAP_NAME_ATTRIBUTE), String::fromStdString(descriptor.name));
        meta.attributes.emplace(String(CLAP_VERSION_ATTRIBUTE), String::fromStdString(descriptor.version));

        // Whether it has an editor is something only the created plug-in says.
        ClapHostedPlugin plugin;
        std::string createProblem;
        if (plugin.create(bundle, descriptor.id, createProblem)) {
            meta.hasNativeEditorSupport = plugin.editorKind() != ClapEditorKind::None;
            plugin.destroy();
        } else {
            LOGW() << "CLAP " << descriptor.id << ": " << createProblem;
            meta.hasNativeEditorSupport = false;
        }

        result.emplace_back(std::move(meta));
    }

    if (result.empty()) {
        return clap::make_ret(Err::NoInstrument);
    }

    return RetVal<AudioResourceMetaList>::make_ok(result);
}
