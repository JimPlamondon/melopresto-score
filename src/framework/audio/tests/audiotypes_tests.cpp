/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include <gtest/gtest.h>

#include "audio/common/audiotypes.h"
#include "audio/common/audioutils.h"

using namespace muse;
using namespace muse::audio;

//! CLAP is the CLever Audio Plug-in interface. A CLAP plug-in is a resource
//! type of its own and is played by a source type of its own.
TEST(Audio_AudioTypesTests, ClapPluginHasItsOwnResourceTextAndSourceType)
{
    EXPECT_EQ(audioResourceTypeToString(AudioResourceType::ClapPlugin), String(u"clap_plugin"));
    EXPECT_EQ(sourceTypeFromResourceType(AudioResourceType::ClapPlugin), AudioSourceType::Clap);

    AudioInputParams params;
    params.resourceMeta.type = AudioResourceType::ClapPlugin;
    params.resourceMeta.id = "org.example.synth";
    EXPECT_EQ(params.type(), AudioSourceType::Clap);
}

//! The score file names a resource's type by its text; the reader finds the type by that text.
TEST(Audio_AudioTypesTests, EveryResourceTextNamesOneType)
{
    auto typeOfText = [](const QString& text) {
        for (const auto& pair : RESOURCE_TYPE_MAP) {
            if (pair.second == text) {
                return pair.first;
            }
        }
        return AudioResourceType::Undefined;
    };

    EXPECT_EQ(typeOfText("clap_plugin"), AudioResourceType::ClapPlugin);
    EXPECT_EQ(typeOfText("vst_plugin"), AudioResourceType::VstPlugin);
    EXPECT_EQ(audioResourceTypeToString(AudioResourceType::VstPlugin), String(u"vst_plugin"));
    EXPECT_EQ(sourceTypeFromResourceType(AudioResourceType::VstPlugin), AudioSourceType::Vsti);
}

//! A CLAP resource's id is its descriptor id; the name shown is the plug-in's own.
TEST(Audio_AudioTypesTests, ClapSourceIsNamedByThePlugInNotByItsId)
{
    AudioInputParams params;
    params.resourceMeta.type = AudioResourceType::ClapPlugin;
    params.resourceMeta.id = "org.example.synth";
    EXPECT_EQ(audioSourceName(params), String(u"org.example.synth"));

    params.resourceMeta.attributes.insert({ String(u"clapName"), String(u"Example Synth") });
    EXPECT_EQ(audioSourceName(params), String(u"Example Synth"));

    AudioInputParams vst;
    vst.resourceMeta.type = AudioResourceType::VstPlugin;
    vst.resourceMeta.id = "Example Synth VST";
    vst.resourceMeta.attributes.insert({ String(u"clapName"), String(u"Not Used") });
    EXPECT_EQ(audioSourceName(vst), String(u"Example Synth VST"));
}
