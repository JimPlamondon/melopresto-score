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

//! The actions of the clap module, for whoever sends them. This header needs
//! nothing of CLAP (CLever Audio Plug-in) itself, so it can be included where
//! the module is not built.
namespace muse::clap {
//! Opens or closes the editor of the CLAP instrument of one track.
inline constexpr char CLAP_INSTRUMENT_EDITOR_ACTION[] = "action://clap/instrument_editor";
}
