/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "clappitchedoutput.h"

#include "claphostedplugin.h"

using namespace muse::clap;

std::string ClapPitchedOutput::prepare(ClapHostedPlugin* plugin, const ClapPerNotePitchConfig& config, uint32_t hostChannels)
{
    m_plugin = plugin;
    m_fault.store(0);
    m_noPitchPathReported = false;
    m_client.init(plugin, hostChannels);
    return m_adapter.configure(config);
}

bool ClapPitchedOutput::isPrepared() const
{
    return m_plugin != nullptr;
}

const ClapPerNotePitchAdapter& ClapPitchedOutput::adapter() const
{
    return m_adapter;
}

void ClapPitchedOutput::raise(ClapAudioFault fault)
{
    int expected = 0;
    m_fault.compare_exchange_strong(expected, static_cast<int>(fault));
}

ClapAudioFault ClapPitchedOutput::takeFault()
{
    const ClapAudioFault own = static_cast<ClapAudioFault>(m_fault.exchange(0));
    const ClapAudioFault client = m_client.takeFault();
    return own != ClapAudioFault::None ? own : client;
}

void ClapPitchedOutput::deliver(const ClapPerNotePitchOutput& output)
{
    if (output.undeliverable > 0) {
        raise(ClapAudioFault::UndeliverableRecord);
    }
    for (size_t i = 0; i < output.count; ++i) {
        // An event that follows its note-on goes one sample after it. When
        // the block ends on the note-on's sample, the audio client carries
        // the event into the next block's first sample.
        m_client.pushEvent(output.events[i].event, output.events[i].afterNoteOn ? 1 : 0);
    }
}

void ClapPitchedOutput::sendSetup()
{
    if (!m_plugin || !m_adapter.isUsable()) {
        return;
    }
    const ClapPerNotePitchSetup setup = m_adapter.setup();
    if (setup.undeliverable > 0) {
        raise(ClapAudioFault::UndeliverableRecord);
    }
    if (setup.count == 0) {
        return;
    }
    for (size_t i = 0; i < setup.count; ++i) {
        m_client.pushEvent(setup.events[i], 0);
    }
    // Never a block of no frames: a plug-in that applies setup only inside
    // processing gets a block of ordinary length, and its output is not kept.
    m_client.processDiscarded(CLAP_SETUP_BLOCK_FRAMES);
}

void ClapPitchedOutput::noteOn(int32_t noteId, int32_t keyNumber, double cents, float velocity)
{
    if (!m_adapter.isUsable()) {
        if (!m_noPitchPathReported) {
            m_noPitchPathReported = true;
            raise(ClapAudioFault::NoPitchPath);
        }
        return;
    }
    const ClapPerNotePitchOutput output = m_adapter.noteOn(noteId, keyNumber, cents, velocity);
    if (output.status != 0) {
        // The note is left out whole, and that is said.
        raise(ClapAudioFault::NoteRefused);
        return;
    }
    deliver(output);
}

void ClapPitchedOutput::noteOff(int32_t noteId)
{
    if (!m_adapter.isUsable()) {
        return;
    }
    // A release of a note that is not sounding (one that was refused, or
    // one ended by allNotesOff) ends nothing.
    const ClapPerNotePitchOutput output = m_adapter.noteOff(noteId);
    if (output.status == 0) {
        deliver(output);
    }
}

void ClapPitchedOutput::pitchChange(int32_t noteId, double cents)
{
    if (!m_adapter.isUsable()) {
        return;
    }
    // A route that cannot retune a held note answers so; nothing is sent.
    const ClapPerNotePitchOutput output = m_adapter.pitchChange(noteId, cents);
    if (output.status == 0) {
        deliver(output);
    }
}

void ClapPitchedOutput::allNotesOff()
{
    ClapPerNotePitchOutput output;
    while (m_adapter.releaseNext(output)) {
        if (output.status == 0) {
            deliver(output);
        }
    }
    m_adapter.reset();
}

bool ClapPitchedOutput::parameter(uint32_t paramId, double plain)
{
    if (!m_plugin) {
        return false;
    }
    ClapEventStorage event;
    return m_plugin->makeParameterEvent(paramId, plain, event) && m_client.pushEvent(event, 0);
}

uint32_t ClapPitchedOutput::render(float* interleaved, uint32_t frames)
{
    if (frames == 0) {
        m_client.flushParameters();
        return 0;
    }
    return m_client.process(interleaved, frames);
}

void ClapPitchedOutput::setVolumeGain(float gain)
{
    m_client.setVolumeGain(gain);
}

void ClapPitchedOutput::stopNow()
{
    m_adapter.reset();
    m_client.stopNow();
}

void ClapPitchedOutput::resume()
{
    m_client.resume();
}
