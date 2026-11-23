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

#include <array>
#include <cstddef>
#include <string>

#include "vsttypes.h"

struct MeloRealtimeProjector;

namespace muse::vst {
//! MIDI controllers 0 to 127, channel pressure (128) and pitch bend (129).
inline constexpr size_t VST_MIDI_CONTROLLER_COUNT = 130;

struct VstPerNotePitchConfig {
    //! The name and version the plug-in states for itself.
    std::string pluginName;
    std::string pluginVersion;
    //! The folder of MeloPresto receiver profiles; empty for none.
    std::string profilesFolder;
    //! The plug-in lists the VST3 tuning note expression.
    bool advertisesTuningExpression = false;
    //! The parameter the plug-in maps to each MIDI controller on each channel.
    std::array<std::array<PluginParamId, VST_MIDI_CONTROLLER_COUNT>, 16> midiParams {};

    VstPerNotePitchConfig();
};

struct VstPerNotePitchParam {
    ParamChangeEvent change {};
    //! Deliver one sample after the note-on of the same event; otherwise before it.
    bool afterNoteOn = false;
};

struct VstPerNotePitchOutput {
    std::array<VstEvent, 2> events {};
    std::array<VstPerNotePitchParam, 2> params {};
    size_t eventCount = 0;
    size_t paramCount = 0;
};

//! The parameter writes that prepare the plug-in before any note, in order.
struct VstPerNotePitchSetup {
    std::array<ParamChangeEvent, 96> params {};
    size_t count = 0;
};

//! Converts the canonical VST3 note sent by MuseScore into the pitch path
//! selected for the unmodified instrument. The selection and every decision
//! come from the MeloPresto shared projector (melo_realtime_projector.h), the
//! same one every MeloPresto application uses; this class only carries its
//! records. The caller retains the canonical event for downstream effects
//! before applying this adapter.
class VstPerNotePitchAdapter
{
public:
    enum class Mode {
        KeyNumberOnly, TuningExpression, NoteOnTuning, Mpe
    };

    VstPerNotePitchAdapter() = default;
    ~VstPerNotePitchAdapter();
    VstPerNotePitchAdapter(const VstPerNotePitchAdapter&) = delete;
    VstPerNotePitchAdapter& operator=(const VstPerNotePitchAdapter&) = delete;

    //! Not on the audio thread: reads the profiles. Returns an empty string,
    //! or what went wrong; the adapter is usable either way.
    std::string configure(const VstPerNotePitchConfig& config);
    void reset();
    VstPerNotePitchOutput adapt(const VstEvent& event);

    VstPerNotePitchSetup setup();
    Mode mode() const;
    const std::string& profileId() const;

private:
    void release();
    PluginParamId midiParam(int16_t channel, int16_t controller) const;

    VstPerNotePitchConfig m_config;
    MeloRealtimeProjector* m_projector = nullptr;
    Mode m_mode = Mode::KeyNumberOnly;
    std::string m_profileId;
};
}
