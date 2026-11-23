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

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <clap/clap.h>

//! CLAP is the CLever Audio Plug-in interface (https://github.com/free-audio/clap, MIT licence).
namespace muse::clap {
class IClapPluginInstance;
using IClapPluginInstancePtr = std::shared_ptr<IClapPluginInstance>;
using ClapPluginInstanceId = int;

//! A CLAP plug-in is a bundle (macOS) or a shared library whose name ends in ".clap".
inline constexpr std::string_view CLAP_PACKAGE_EXTENSION = "clap";
inline constexpr std::string_view CLAP_PACKAGE_FILTER = "*.clap";

//! The key under which a plug-in's opaque state, as base64 text, is kept in an AudioUnitConfig.
inline constexpr std::string_view CLAP_STATE_KEY = "clapState";

//! The plug-in's display name, kept among a resource's attributes. The
//! resource id is the descriptor id, which is not a name a musician reads.
inline constexpr char16_t CLAP_NAME_ATTRIBUTE[] = u"clapName";
inline constexpr char16_t CLAP_VERSION_ATTRIBUTE[] = u"clapVersion";

//! The most frames one process call carries. A longer block is split.
inline constexpr uint32_t CLAP_MAX_BLOCK_FRAMES = 4096;
//! The length of the block that delivers setup when a plug-in loads; its output is discarded.
inline constexpr uint32_t CLAP_SETUP_BLOCK_FRAMES = 512;
//! Events one process call can carry. Storage is allocated before playback.
inline constexpr uint32_t CLAP_EVENT_CAPACITY = 4096;

//! One CLAP event of any of the kinds this host sends, in storage of one size.
union ClapEventStorage {
    clap_event_header_t header;
    clap_event_note_t note;
    clap_event_note_expression_t expression;
    clap_event_param_value_t param;
    clap_event_midi_t midi;
};
}
