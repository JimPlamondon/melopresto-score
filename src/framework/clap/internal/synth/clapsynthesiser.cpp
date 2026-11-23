/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "clapsynthesiser.h"

#include <cmath>
#include <filesystem>

#include "audio/common/audiosanitizer.h"

#include "../claphostedplugin.h"

#include "log.h"

using namespace muse;
using namespace muse::clap;
using namespace muse::audio;
using namespace muse::audio::synth;

namespace muse::clap {
//! The folder of MeloPresto receiver profiles, beside the application's other data.
static constexpr std::string_view CLAP_MELO_PROFILES_FOLDER = "melo_profiles";
static constexpr size_t CLAP_NOTE_EVENT_CAPACITY = 4096;
}

ClapSynthesiser::ClapSynthesiser(const TrackId trackId, const muse::audio::AudioInputParams& params,
                                 const modularity::ContextPtr& iocCtx)
    : AbstractSynthesizer(params, iocCtx), m_trackId(trackId)
{
    m_noteEvents.reserve(CLAP_NOTE_EVENT_CAPACITY);
}

ClapSynthesiser::~ClapSynthesiser()
{
    if (!m_pluginPtr) {
        return;
    }
    // Processing stops here, on the audio engine thread, before the main
    // thread deactivates and destroys the plug-in.
    m_output.stopNow();
    if (m_pluginPtr->isLoaded()) {
        m_pluginPtr->hosted().stopProcessingNow();
    }
    instancesRegister()->unregisterById(m_pluginPtr->id());
}

void ClapSynthesiser::init(const OutputSpec& spec)
{
    IF_ASSERT_FAILED(spec.isValid()) {
        return;
    }

    m_outputSpec = spec;

    m_pluginPtr = instancesRegister()->makeAndRegisterInstrPlugin(m_params.resourceMeta.id, m_trackId, spec.sampleRate);

    // Subscribe before anything can be reported.
    m_pluginPtr->pluginSettingsChanged().onReceive(this, [this](const muse::audio::AudioUnitConfig& newConfig) {
        if (m_params.configuration == newConfig) {
            return;
        }

        m_params.configuration = newConfig;
        m_paramsChanges.send(m_params);
    });

    // The configuration saved in the score: a plug-in that is still loading
    // takes its state before it is activated, so its first note has it.
    m_pluginPtr->updatePluginConfig(m_params.configuration);

    m_pluginPtr->settled().onNotify(this, [this]() {
        if (!m_settled) {
            m_settled = true;
            m_readyToPlayChanged.notify();
        }
    });

    // A state applied to a loaded plug-in may undo what prepared it for its
    // pitch path: prepare it again.
    m_pluginPtr->stateRestored().onNotify(this, [this]() {
        if (m_inited) {
            m_output.sendSetup();
            m_pluginPtr->requestSettle();
        }
    });

    if (m_pluginPtr->isLoaded()) {
        onPluginLoaded();
    } else {
        m_pluginPtr->loadingCompleted().onNotify(this, [this]() {
            onPluginLoaded();
        });
    }

    // Notes whose releases will not come from the score are ended here.
    m_sequencer.setOnOffStreamFlushed([this]() {
        m_output.allNotesOff();
    });
    m_sequencer.setOnMainStreamFlushed([this]() {
        m_output.allNotesOff();
    });
}

void ClapSynthesiser::onPluginLoaded()
{
    if (m_inited || m_loadFailed) {
        return;
    }
    if (!m_pluginPtr->isLoaded()) {
        // The load failed for good (a missing plug-in, for one). The track
        // keeps its plug-in and its saved state and is silent; whoever waits
        // for it stops waiting.
        m_loadFailed = true;
        m_readyToPlayChanged.notify();
        return;
    }

    prepareAndSettle();
}

void ClapSynthesiser::prepareAndSettle()
{
    ClapHostedPlugin& hosted = m_pluginPtr->hosted();

    // One selection for this plug-in, made by the MeloPresto shared code from
    // its receiver profile, if one matches the name and version the plug-in
    // states, and from what the plug-in's note port takes.
    ClapPerNotePitchConfig pitchConfig;
    pitchConfig.pluginName = hosted.descriptor().name;
    pitchConfig.pluginVersion = hosted.descriptor().version;
    pitchConfig.takesNoteEvents = hosted.notePorts().takesNoteEvents;
    pitchConfig.takesMidiBytes = hosted.notePorts().takesMidiBytes;

    const std::string profilesFolder = (globalConfiguration()->appDataPath() + CLAP_MELO_PROFILES_FOLDER.data()).toStdString();
    std::error_code error;
    if (std::filesystem::is_directory(std::filesystem::path(profilesFolder), error)) {
        pitchConfig.profilesFolder = profilesFolder;
    } else {
        // What is observed is reported: there are no profiles. The selection stays the shared code's.
        LOGW() << "No MeloPresto receiver profiles at " << profilesFolder
               << ": CLAP plug-in " << m_params.resourceMeta.id << " is selected for from its own report alone";
    }

    const std::string pitchProblem = m_output.prepare(&hosted, pitchConfig, m_outputSpec.audioChannelCount);
    if (!pitchProblem.empty()) {
        LOGE() << "MeloPresto pitch path for CLAP plug-in " << m_params.resourceMeta.id << " could not be prepared: "
               << pitchProblem << ". The instrument is silent.";
    }
    LOGI() << "MeloPresto pitch path for CLAP plug-in " << m_params.resourceMeta.id << ": mode "
           << static_cast<int>(m_output.adapter().mode()) << ", transport " << static_cast<int>(m_output.adapter().transport())
           << ", profile \"" << m_output.adapter().profileId() << "\"";

    m_sequencer.init(m_useDynamicEvents);
    m_inited = true;

    // What prepares the plug-in is sent now, when it has loaded, not when
    // playback starts. The synthesiser is ready to play once the main thread
    // has served what the plug-in asked of it.
    m_output.sendSetup();
    m_pluginPtr->requestSettle();
}

void ClapSynthesiser::toggleVolumeGain(const bool isActive)
{
    static constexpr muse::audio::gain_t NON_ACTIVE_GAIN = 0.5f;

    m_output.setVolumeGain(isActive ? m_sequencer.currentGain() : NON_ACTIVE_GAIN);
}

bool ClapSynthesiser::isValid() const
{
    return m_pluginPtr && m_pluginPtr->isLoaded();
}

bool ClapSynthesiser::setHostParameterPlain(uint32_t paramId, double plain)
{
    ONLY_AUDIO_ENGINE_THREAD;
    if (!m_inited || !std::isfinite(plain)) {
        return false;
    }
    return m_output.parameter(paramId, plain);
}

bool ClapSynthesiser::readyToPlay() const
{
    ONLY_AUDIO_ENGINE_THREAD;

    return (m_inited && m_settled) || m_loadFailed || !m_pluginPtr;
}

muse::audio::AudioSourceType ClapSynthesiser::type() const
{
    return m_params.type();
}

std::string ClapSynthesiser::name() const
{
    if (!m_pluginPtr) {
        return std::string();
    }

    return m_pluginPtr->name();
}

void ClapSynthesiser::flushSound()
{
    m_sequencer.flushOffstream();
    m_output.allNotesOff();
}

void ClapSynthesiser::setupSound(const mpe::PlaybackSetupData& setupData)
{
    m_useDynamicEvents = setupData.supportsSingleNoteDynamics;
}

void ClapSynthesiser::setupEvents(const mpe::PlaybackData& playbackData)
{
    m_sequencer.load(playbackData);
}

const mpe::PlaybackData& ClapSynthesiser::playbackData() const
{
    return m_sequencer.playbackData();
}

bool ClapSynthesiser::isActive() const
{
    return m_sequencer.isActive();
}

void ClapSynthesiser::setIsActive(const bool isActive)
{
    if (m_sequencer.isActive() == isActive) {
        return;
    }

    m_sequencer.setActive(isActive);
    toggleVolumeGain(isActive);
}

muse::audio::msecs_t ClapSynthesiser::playbackPosition() const
{
    return m_sequencer.playbackPosition();
}

void ClapSynthesiser::setPlaybackPosition(const muse::audio::msecs_t newPosition)
{
    // A jump (a seek, a repeat): the releases of the notes that are sounding
    // lie where playback no longer goes, so they are ended now.
    if (m_inited && newPosition != m_sequencer.playbackPosition()) {
        m_output.allNotesOff();
    }

    m_sequencer.setPlaybackPosition(newPosition);

    if (isActive()) {
        m_output.setVolumeGain(m_sequencer.currentGain());
    }
}

void ClapSynthesiser::setOutputSpec(const audio::OutputSpec& spec)
{
    const bool rateChanged = spec.sampleRate != m_outputSpec.sampleRate;
    m_outputSpec = spec;

    if (m_inited && rateChanged) {
        // The plug-in is activated for one sample rate. Stop processing here,
        // on the audio engine thread; the main thread activates it again.
        m_output.stopNow();
        m_pluginPtr->requestReactivation(static_cast<double>(spec.sampleRate));
    }
}

unsigned int ClapSynthesiser::audioChannelsCount() const
{
    return m_outputSpec.audioChannelCount;
}

async::Channel<unsigned int> ClapSynthesiser::audioChannelsCountChanged() const
{
    return m_streamsCountChanged;
}

const AudioNoteEvents& ClapSynthesiser::noteEvents() const
{
    return m_noteEvents;
}

samples_t ClapSynthesiser::process(float* buffer, samples_t samplesPerChannel)
{
    m_noteEvents.clear();

    if (!buffer || !m_inited) {
        return 0;
    }

    const msecs_t nextMsecs = samplesToMsecs(samplesPerChannel, m_outputSpec.sampleRate);
    const ClapSequencer::EventSequenceMap sequences = m_sequencer.movePlaybackForward(nextMsecs);

    samples_t sampleOffset = 0;
    samples_t processedSamples = 0;

    for (auto it = sequences.cbegin(); it != sequences.cend(); ++it) {
        samples_t durationInSamples = samplesPerChannel - sampleOffset;

        auto nextIt = std::next(it);
        if (nextIt != sequences.cend()) {
            msecs_t duration = nextIt->first - it->first;
            durationInSamples = microSecsToSamples(duration, m_outputSpec.sampleRate);
        }

        IF_ASSERT_FAILED(sampleOffset + durationInSamples <= samplesPerChannel) {
            break;
        }

        processedSamples += processSequence(it->second, sampleOffset, durationInSamples,
                                            buffer + sampleOffset * m_outputSpec.audioChannelCount);
        sampleOffset += durationInSamples;
    }

    // Whatever stopped the path or left something out is said, in the log,
    // by the main thread; nothing is written from the audio callback.
    const ClapAudioFault fault = m_output.takeFault();
    if (fault != ClapAudioFault::None) {
        m_pluginPtr->reportAudioFault(static_cast<int>(fault));
    }

    return processedSamples;
}

samples_t ClapSynthesiser::processSequence(const ClapSequencer::EventSequence& sequence, const samples_t sequenceSampleOffset,
                                           const samples_t samples, float* buffer)
{
    for (const ClapSequencer::EventType& event : sequence) {
        if (std::holds_alternative<ClapNoteEvent>(event)) {
            const ClapNoteEvent& note = std::get<ClapNoteEvent>(event);
            switch (note.kind) {
            case ClapNoteEvent::Kind::NoteOn:
                m_output.noteOn(note.noteId, note.keyNumber, note.cents, note.velocity);
                captureNote(note, sequenceSampleOffset);
                break;
            case ClapNoteEvent::Kind::NoteOff:
                m_output.noteOff(note.noteId);
                captureNote(note, sequenceSampleOffset);
                break;
            case ClapNoteEvent::Kind::PitchChange:
                m_output.pitchChange(note.noteId, note.cents);
                break;
            }
        } else {
            m_output.setVolumeGain(std::get<muse::audio::gain_t>(event));
        }
    }

    // With no frames nothing is processed: the events wait for the next
    // frames, and parameter values alone go through the parameter flush.
    return m_output.render(buffer, samples);
}

void ClapSynthesiser::captureNote(const ClapNoteEvent& note, const samples_t sequenceSampleOffset)
{
    if (m_noteEvents.size() >= m_noteEvents.capacity()) {
        return;
    }
    // Effects receive the note as the score states it, with its exact
    // MeloPresto tuning, not as it was adapted for this instrument.
    AudioNoteEvent captured;
    captured.type = note.kind == ClapNoteEvent::Kind::NoteOn ? AudioNoteEvent::Type::NoteOn : AudioNoteEvent::Type::NoteOff;
    captured.sampleOffset = sequenceSampleOffset;
    captured.noteId = note.noteId;
    captured.pitch = static_cast<int16_t>(note.keyNumber);
    captured.tuningCents = static_cast<float>(note.cents);
    captured.velocity = note.velocity;
    m_noteEvents.push_back(captured);
}
