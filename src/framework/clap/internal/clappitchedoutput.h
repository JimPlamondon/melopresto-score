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

#include <atomic>
#include <string>

#include "clapaudioclient.h"
#include "clappernotepitchadapter.h"

namespace muse::clap {
class ClapHostedPlugin;

//! The way from a note of the score to the sound of one CLAP plug-in: the
//! per-note pitch adapter turns the note into CLAP events and the audio
//! client delivers them. prepare() runs off the audio thread; everything
//! else runs on it, allocates nothing and takes no lock.
class ClapPitchedOutput
{
public:
    ClapPitchedOutput() = default;

    //! Selects the pitch path for the plug-in and allocates the audio
    //! storage. Returns an empty string, or what went wrong; after a failure
    //! the plug-in is silent and every note it is asked for is a fault.
    std::string prepare(ClapHostedPlugin* plugin, const ClapPerNotePitchConfig& config, uint32_t hostChannels);
    bool isPrepared() const;
    const ClapPerNotePitchAdapter& adapter() const;

    //! Delivers what prepares the plug-in for its pitch path, in a block of
    //! ordinary length whose output is discarded. A plug-in may apply it
    //! only when its main-thread callback is served afterwards.
    void sendSetup();

    void noteOn(int32_t noteId, int32_t keyNumber, double cents, float velocity);
    void noteOff(int32_t noteId);
    //! A new pitch for a note that is sounding.
    void pitchChange(int32_t noteId, double cents);
    //! Ends every sounding note, each with its own release.
    void allNotesOff();
    //! A value for one of the plug-in's parameters, in its plain range.
    bool parameter(uint32_t paramId, double plain);

    //! Renders \p frames. With no frames nothing is processed: pending
    //! parameter values go through the parameter flush, notes wait.
    uint32_t render(float* interleaved, uint32_t frames);

    void setVolumeGain(float gain);
    //! For the audio thread when no block is being rendered.
    void stopNow();
    void resume();

    //! The first fault not yet taken; None if there is none.
    ClapAudioFault takeFault();

private:
    void deliver(const ClapPerNotePitchOutput& output);
    void raise(ClapAudioFault fault);

    ClapPerNotePitchAdapter m_adapter;
    ClapAudioClient m_client;
    ClapHostedPlugin* m_plugin = nullptr;
    std::atomic<int> m_fault { 0 };
    //! The missing pitch path is said once, not once a note.
    bool m_noPitchPathReported = false;
};
}
