/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2022 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "vstsequencer.h"

#include <unordered_map>

#include "global/interpolation.h"

#include "audioplugins/audiopluginnotepitch.h"

using namespace muse;
using namespace muse::vst;

static constexpr ControlIdx MODWHEEL_IDX = static_cast<ControlIdx>(Steinberg::Vst::kCtrlModWheel);
static constexpr ControlIdx SUSTAIN_IDX = static_cast<ControlIdx>(Steinberg::Vst::kCtrlSustainOnOff);
static constexpr ControlIdx SOSTENUTO_IDX = static_cast<ControlIdx>(Steinberg::Vst::kCtrlSustenutoOnOff);
static constexpr ControlIdx PITCH_BEND_IDX = static_cast<ControlIdx>(Steinberg::Vst::kPitchBend);

static const mpe::ArticulationTypeSet SUSTAIN_PEDAL_CC_SUPPORTED_TYPES {
    mpe::ArticulationType::Pedal,
};

static const mpe::ArticulationTypeSet SOSTENUTO_PEDAL_CC_SUPPORTED_TYPES {
    mpe::ArticulationType::LaissezVibrer,
};

static const mpe::ArticulationTypeSet BEND_SUPPORTED_TYPES {
    mpe::ArticulationType::Multibend, mpe::ArticulationType::ContinuousGlissando,
};

void VstSequencer::init(ParamsMapping&& mapping, bool useDynamicEvents, const VstNoteExpressionCapabilities& capabilities)
{
    m_mapping = std::move(mapping);
    m_useDynamicEvents = useDynamicEvents;
    m_capabilities = capabilities;
    m_inited = true;

    updateMainStreamEvents(m_playbackData.originEvents, m_playbackData.dynamics);
}

int32_t VstSequencer::allocateNoteId()
{
    const int32_t id = m_nextNoteId;
    if (m_nextNoteId >= MAX_NOTE_ID) {
        // Wrap only after every sounding note has been released under its own
        // id: the flush callbacks end the active notes before ids are reused.
        if (m_onMainStreamFlushed) {
            m_onMainStreamFlushed();
        }
        if (m_onOffStreamFlushed) {
            m_onOffStreamFlushed();
        }
        m_nextNoteId = 0;
    } else {
        ++m_nextNoteId;
    }
    return id;
}

void VstSequencer::updateOffStreamEvents(const mpe::PlaybackEventsMap& events, const mpe::DynamicLevelLayers& dynamics)
{
    addPlaybackEvents(m_offStreamEvents, events);

    if (m_useDynamicEvents) {
        addDynamicEvents(m_offStreamEvents, dynamics);
    }

    updateOffSequenceIterator();
}

void VstSequencer::updateMainStreamEvents(const mpe::PlaybackEventsMap& events, const mpe::DynamicLevelLayers& dynamics)
{
    if (!m_inited) {
        return;
    }

    m_mainStreamEvents.clear();

    if (m_onMainStreamFlushed) {
        m_onMainStreamFlushed();
    }

    addPlaybackEvents(m_mainStreamEvents, events);
    sortNoteOnEventsByPitch(m_mainStreamEvents);

    if (m_useDynamicEvents) {
        addDynamicEvents(m_mainStreamEvents, dynamics);
    }

    updateMainSequenceIterator();
}

muse::audio::gain_t VstSequencer::currentGain() const
{
    if (m_useDynamicEvents) {
        mpe::dynamic_level_t currentDynamicLevel = dynamicLevel(m_playbackPosition);
        return expressionLevel(currentDynamicLevel);
    }

    return 0.5f;
}

void VstSequencer::addPlaybackEvents(EventSequenceMap& destination, const mpe::PlaybackEventsMap& events)
{
    SostenutoTimeAndDurations sostenutoTimeAndDurations;
    std::optional<mpe::DynamicTonalityProfileEvent> currentProfile;

    for (const auto& evPair : events) {
        for (const mpe::PlaybackEvent& event : evPair.second) {
            if (std::holds_alternative<mpe::DynamicTonalityProfileEvent>(event)) {
                const auto& profile = std::get<mpe::DynamicTonalityProfileEvent>(event);
                if (!currentProfile.has_value() || !(profile == *currentProfile)) {
                    destination[evPair.first].emplace_back(profile);
                    currentProfile = profile;
                }
            } else if (std::holds_alternative<mpe::NoteEvent>(event)) {
                addNoteEvent(destination, std::get<mpe::NoteEvent>(event), sostenutoTimeAndDurations);
            } else if (std::holds_alternative<mpe::ControllerChangeEvent>(event)) {
                addControlChangeEvent(destination, evPair.first, std::get<mpe::ControllerChangeEvent>(event));
            }
        }
    }

    addSostenutoEvents(destination, sostenutoTimeAndDurations);
}

void VstSequencer::addDynamicEvents(EventSequenceMap& destination, const mpe::DynamicLevelLayers& layers)
{
    for (const auto& layer : layers) {
        for (const auto& dynamic : layer.second) {
            destination[dynamic.first].emplace_back(expressionLevel(dynamic.second));
        }
    }
}

void VstSequencer::addNoteEvent(EventSequenceMap& destination, const mpe::NoteEvent& noteEvent,
                                SostenutoTimeAndDurations& sostenutoTimeAndDurations)
{
    const mpe::ArrangementContext& arrangementCtx = noteEvent.arrangementCtx();
    // MeloSynth VST3 workstream: the VST3 note identifier is its own unique,
    // monotonic value — never the pitch index (the stock code wrote -1).
    const int32_t noteId = allocateNoteId();
    const std::optional<mpe::ExactPitch>& exact = noteEvent.pitchCtx().exactPitch;
    // A lattice-identified MeloPresto note carries its exact sounding pitch: the
    // Note On uses the Kernel's key and the FULL residual cents instead of the
    // 2-cent pitch-level grid; stock notes keep the stock construction. The
    // derivation is shared with the clap module (audiopluginnotepitch.h). A
    // Kernel key outside 0 to 127 is passed on as it is, for the shared
    // projector to refuse; it is not moved to another key with its cents kept.
    const audioplugins::NotePitch pitch = audioplugins::notePitchOf(noteEvent);
    const int32_t noteIdx = pitch.keyNumber;
    const float velocityFraction = noteVelocityFraction(noteEvent);
    const float tuning = static_cast<float>(pitch.cents);

    if (arrangementCtx.hasStart()) {
        destination[arrangementCtx.actualTimestamp].emplace_back(buildEvent(VstEvent::kNoteOnEvent, noteIdx, velocityFraction,
                                                                            tuning, noteId));
        // Lattice identity travels only to a plug-in that advertises BOTH
        // frozen custom types, once, at the Note On offset, under the same id.
        if (exact.has_value() && exact->hasLattice && m_capabilities.meloLattice) {
            addLatticeIdentity(destination, arrangementCtx.actualTimestamp, *exact, noteId);
        }
    }

    if (arrangementCtx.hasEnd()) {
        const mpe::timestamp_t timestampTo = arrangementCtx.actualTimestamp + noteEvent.arrangementCtx().actualDuration;
        destination[timestampTo].emplace_back(buildEvent(VstEvent::kNoteOffEvent, noteIdx, velocityFraction, tuning, noteId));
    }

    for (const auto& artPair : noteEvent.expressionCtx().articulations) {
        if (artPair.first == mpe::ArticulationType::Standard) {
            continue;
        }

        const mpe::ArticulationMeta& meta = artPair.second.meta;

        if (!noteEvent.pitchCtx().pitchCurve.empty() && muse::contains(BEND_SUPPORTED_TYPES, meta.type)) {
            if (m_capabilities.tuning) {
                // The plug-in advertises the standard per-note tuning
                // expression: the curve goes to THIS note only; the global
                // pitch-bend parameter is never used for it.
                addTuningExpressionCurve(destination, noteEvent, meta, noteId);
            } else {
                addPitchCurve(destination, noteEvent, meta, noteId);
            }
            continue;
        }

        if (muse::contains(SUSTAIN_PEDAL_CC_SUPPORTED_TYPES, meta.type)) {
            addPedalEvent(destination, meta);
            continue;
        }

        if (muse::contains(SOSTENUTO_PEDAL_CC_SUPPORTED_TYPES, meta.type)) {
            const mpe::timestamp_t timestamp = arrangementCtx.actualTimestamp + noteEvent.arrangementCtx().actualDuration * 0.1; // add offset for Sostenuto to take effect
            sostenutoTimeAndDurations.push_back(mpe::TimestampAndDuration { timestamp, meta.overallDuration });
            continue;
        }
    }
}

void VstSequencer::addPedalEvent(EventSequenceMap& destination, const mpe::ArticulationMeta& meta)
{
    if (meta.hasStart()) {
        addParamChange(destination, meta.timestamp, SUSTAIN_IDX, 1);
    }

    if (meta.hasEnd()) {
        addParamChange(destination, meta.timestamp + meta.overallDuration, SUSTAIN_IDX, 0);
    }
}

void VstSequencer::addControlChangeEvent(EventSequenceMap& destination, const mpe::timestamp_t timestamp,
                                         const mpe::ControllerChangeEvent& event)
{
    switch (event.type) {
    case mpe::ControllerChangeEvent::Modulation:
        addParamChange(destination, timestamp, MODWHEEL_IDX, event.val);
        break;
    case mpe::ControllerChangeEvent::SustainPedalOnOff:
        addParamChange(destination, timestamp, SUSTAIN_IDX, event.val);
        break;
    case mpe::ControllerChangeEvent::PitchBend:
        addParamChange(destination, timestamp, PITCH_BEND_IDX, event.val);
        break;
    case mpe::ControllerChangeEvent::Undefined:
        break;
    }
}

void VstSequencer::addParamChange(EventSequenceMap& destination, const mpe::timestamp_t timestamp,
                                  const ControlIdx controlIdx, const PluginParamValue value)
{
    auto controlIt = m_mapping.find(controlIdx);
    if (controlIt == m_mapping.cend()) {
        return;
    }

    const PluginParamId paramId = controlIt->second;
    EventSequence& events = destination[timestamp];

    for (const EventType& e : events) {
        if (!std::holds_alternative<ParamChangeEvent>(e)) {
            continue;
        }

        const ParamChangeEvent& pce = std::get<ParamChangeEvent>(e);
        if (pce.paramId == paramId && RealIsEqual(pce.value, value)) {
            return;
        }
    }

    events.emplace_back(ParamChangeEvent { paramId, value });
}

void VstSequencer::addPitchCurve(EventSequenceMap& destination, const mpe::NoteEvent& noteEvent,
                                 const mpe::ArticulationMeta& artMeta, const int32_t /*noteId*/)
{
    // Stock compatibility path for plug-ins WITHOUT the standard per-note
    // tuning expression: the curve rides the global pitch-bend parameter.
    auto pitchBendIt = m_mapping.find(PITCH_BEND_IDX);
    if (pitchBendIt == m_mapping.cend()) {
        return;
    }

    const mpe::timestamp_t noteTimestampTo = noteEvent.arrangementCtx().actualTimestamp + noteEvent.arrangementCtx().actualDuration;
    const mpe::timestamp_t pitchBendTimestampTo = std::min(artMeta.timestamp + artMeta.overallDuration, noteTimestampTo);

    ParamChangeEvent event;
    event.paramId = pitchBendIt->second;
    event.value = 0.5f;
    destination[pitchBendTimestampTo].push_back(event);

    auto currIt = noteEvent.pitchCtx().pitchCurve.cbegin();
    auto nextIt = std::next(currIt);
    auto endIt = noteEvent.pitchCtx().pitchCurve.cend();

    float prevBendValue = -1.f;

    for (; nextIt != endIt; currIt = nextIt, nextIt = std::next(currIt)) {
        const float currValue = pitchBendLevel(currIt->second);
        const float nextValue = pitchBendLevel(nextIt->second);

        const mpe::timestamp_t currTime = artMeta.timestamp + artMeta.overallDuration * mpe::percentageToFactor(currIt->first);
        const mpe::timestamp_t nextTime = artMeta.timestamp + artMeta.overallDuration * mpe::percentageToFactor(nextIt->first);

        using namespace muse::interpolation;
        const Point currPoint { static_cast<double>(currTime), currValue };
        const Point nextPoint { static_cast<double>(nextTime), nextValue };

        const size_t pointCount = audioplugins::pitchCurvePointCount(currIt->second, nextIt->second);

        const std::vector<Point> points = lerp(currPoint, nextPoint, pointCount);

        for (const Point& point : points) {
            const mpe::timestamp_t time = static_cast<mpe::timestamp_t>(std::round(point.x));
            const float bendValue = static_cast<float>(point.y);

            if (time < pitchBendTimestampTo && !RealIsEqual(prevBendValue, bendValue)) {
                event.value = bendValue;
                destination[time].push_back(event);
            }

            prevBendValue = bendValue;
        }
    }
}

void VstSequencer::addTuningExpressionCurve(EventSequenceMap& destination, const mpe::NoteEvent& noteEvent,
                                            const mpe::ArticulationMeta& artMeta, const int32_t noteId)
{
    // Same interpolation as the global-bend path, but every point becomes a
    // kTuningTypeID note expression paired to this note's id at its original
    // timestamp. VST3 kTuningTypeID: normalized = 0.5 + cents / 24000.
    const mpe::timestamp_t noteTimestampTo = noteEvent.arrangementCtx().actualTimestamp + noteEvent.arrangementCtx().actualDuration;
    const mpe::timestamp_t curveTimestampTo = std::min(artMeta.timestamp + artMeta.overallDuration, noteTimestampTo);

    auto currIt = noteEvent.pitchCtx().pitchCurve.cbegin();
    auto nextIt = std::next(currIt);
    auto endIt = noteEvent.pitchCtx().pitchCurve.cend();

    const double baseCents = audioplugins::notePitchOf(noteEvent).cents;
    auto tuningNormalized = [baseCents](const mpe::pitch_level_t pitchLevelOffset) {
        return 0.5 + audioplugins::pitchCurveCents(baseCents, pitchLevelOffset) / 24000.0;
    };

    double prevValue = -1.0;

    // A single-point curve still states the note's offset at its start.
    if (nextIt == endIt && currIt != endIt) {
        const double value = tuningNormalized(currIt->second);
        destination[artMeta.timestamp].push_back(buildNoteExpressionEvent(Steinberg::Vst::kTuningTypeID, noteId, value));
        return;
    }

    for (; nextIt != endIt; currIt = nextIt, nextIt = std::next(currIt)) {
        const double currValue = tuningNormalized(currIt->second);
        const double nextValue = tuningNormalized(nextIt->second);

        const mpe::timestamp_t currTime = artMeta.timestamp + artMeta.overallDuration * mpe::percentageToFactor(currIt->first);
        const mpe::timestamp_t nextTime = artMeta.timestamp + artMeta.overallDuration * mpe::percentageToFactor(nextIt->first);

        using namespace muse::interpolation;
        const Point currPoint { static_cast<double>(currTime), currValue };
        const Point nextPoint { static_cast<double>(nextTime), nextValue };

        const size_t pointCount = audioplugins::pitchCurvePointCount(currIt->second, nextIt->second);

        const std::vector<Point> points = lerp(currPoint, nextPoint, pointCount);

        for (const Point& point : points) {
            const mpe::timestamp_t time = static_cast<mpe::timestamp_t>(std::round(point.x));
            const double value = point.y;

            if (time < curveTimestampTo && !RealIsEqual(prevValue, value)) {
                destination[time].push_back(buildNoteExpressionEvent(Steinberg::Vst::kTuningTypeID, noteId, value));
            }

            prevValue = value;
        }
    }
}

void VstSequencer::addLatticeIdentity(EventSequenceMap& destination, const mpe::timestamp_t timestamp,
                                      const mpe::ExactPitch& exact, const int32_t noteId)
{
    // VST3 discrete-step encoding over the plug-in-declared domain:
    // normalized = (coord - min) / stepCount. A coordinate outside the
    // declared domain sends NO identity (explicit non-MeloPresto fallback; the
    // exact pitch itself still travels in the Note On).
    const auto encode = [](const int32_t coord, const int32_t min, const int32_t stepCount, double& out) {
        if (stepCount <= 0 || coord < min || coord > min + stepCount) {
            return false;
        }
        out = static_cast<double>(coord - min) / static_cast<double>(stepCount);
        return true;
    };
    double nPer = 0.0;
    double nGen = 0.0;
    if (!encode(exact.nPer, m_capabilities.nPerMin, m_capabilities.nPerStepCount, nPer)
        || !encode(exact.nGen, m_capabilities.nGenMin, m_capabilities.nGenStepCount, nGen)) {
        return;
    }
    destination[timestamp].push_back(buildNoteExpressionEvent(MELO_NOTE_EXPRESSION_NPER, noteId, nPer));
    destination[timestamp].push_back(buildNoteExpressionEvent(MELO_NOTE_EXPRESSION_NGEN, noteId, nGen));
}

void VstSequencer::addSostenutoEvents(EventSequenceMap& destination, const SostenutoTimeAndDurations& sostenutoTimeAndDurations)
{
    for (size_t i = 0; i < sostenutoTimeAndDurations.size(); ++i) {
        const mpe::TimestampAndDuration& currentTnD = sostenutoTimeAndDurations.at(i);
        const mpe::timestamp_t timestampTo = currentTnD.timestamp + currentTnD.duration;

        addParamChange(destination, currentTnD.timestamp, SOSTENUTO_IDX, 1);

        if (i == sostenutoTimeAndDurations.size() - 1) {
            addParamChange(destination, timestampTo, SOSTENUTO_IDX, 0);
            continue;
        }

        const mpe::TimestampAndDuration& nextTnD = sostenutoTimeAndDurations.at(i + 1);
        if (timestampTo <= nextTnD.timestamp) { // handle potential overlap
            addParamChange(destination, timestampTo, SOSTENUTO_IDX, 0);
        }
    }
}

//! Hack to make keyswitches work until we have proper UI support
//! see: https://github.com/musescore/MuseScore/issues/32150
//!
//! MeloSynth VST3 workstream: per-note expression events (lattice identity,
//! tuning) that share a timestamp with their Note On travel WITH it — they
//! are grouped behind the Note On of the same id before the pitch sort, so a
//! plug-in always sees the Note On before the note's expressions.
void VstSequencer::sortNoteOnEventsByPitch(EventSequenceMap& destination)
{
    for (auto& [_, seq] : destination) {
        if (seq.size() <= 1) {
            continue;
        }

        struct Group {
            bool isNoteOn = false;
            int pitch = 0;
            int32_t noteId = -1;
            std::vector<EventType> items;
        };

        std::vector<Group> groups;
        groups.reserve(seq.size());

        // Note Ons open groups; expressions join the group of their id.
        std::unordered_map<int32_t, size_t> groupOfId;
        for (const EventType& e : seq) {
            if (std::holds_alternative<VstEvent>(e)) {
                const VstEvent& ve = std::get<VstEvent>(e);
                if (ve.type == VstEvent::kNoteOnEvent) {
                    Group g;
                    g.isNoteOn = true;
                    g.pitch = ve.noteOn.pitch;
                    g.noteId = ve.noteOn.noteId;
                    g.items.push_back(e);
                    if (ve.noteOn.noteId >= 0) {
                        groupOfId[ve.noteOn.noteId] = groups.size();
                    }
                    groups.push_back(std::move(g));
                    continue;
                }
                if (ve.type == VstEvent::kNoteExpressionValueEvent && ve.noteExpressionValue.noteId >= 0) {
                    auto it = groupOfId.find(ve.noteExpressionValue.noteId);
                    if (it != groupOfId.end()) {
                        groups[it->second].items.push_back(e);
                        continue;
                    }
                }
            }
            Group g;
            g.items.push_back(e);
            groups.push_back(std::move(g));
        }

        std::stable_sort(groups.begin(), groups.end(), [](const Group& g1, const Group& g2) {
            if (g1.isNoteOn && g2.isNoteOn) {
                return g1.pitch < g2.pitch;
            }
            return false;
        });

        seq.clear();
        for (Group& g : groups) {
            for (EventType& e : g.items) {
                seq.push_back(std::move(e));
            }
        }
    }
}

VstEvent VstSequencer::buildEvent(const VstEvent::EventTypes type, const int32_t noteIdx, const float velocityFraction,
                                  const float tuning, const int32_t noteId) const
{
    VstEvent result;

    result.busIndex = 0;
    result.sampleOffset = 0;
    result.ppqPosition = 0;
    result.flags = VstEvent::kIsLive;
    result.type = type;

    if (type == VstEvent::kNoteOnEvent) {
        result.noteOn.noteId = noteId;
        result.noteOn.channel = 0;
        result.noteOn.pitch = noteIdx;
        result.noteOn.tuning = tuning;
        result.noteOn.velocity = velocityFraction;
        result.noteOn.length = 0;
    } else {
        result.noteOff.noteId = noteId;
        result.noteOff.channel = 0;
        result.noteOff.pitch = noteIdx;
        result.noteOff.tuning = tuning;
        result.noteOff.velocity = velocityFraction;
    }

    return result;
}

VstEvent VstSequencer::buildNoteExpressionEvent(const Steinberg::Vst::NoteExpressionTypeID typeId, const int32_t noteId,
                                                const double normalizedValue) const
{
    VstEvent result;

    result.busIndex = 0;
    result.sampleOffset = 0;
    result.ppqPosition = 0;
    result.flags = VstEvent::kIsLive;
    result.type = VstEvent::kNoteExpressionValueEvent;
    result.noteExpressionValue.typeId = typeId;
    result.noteExpressionValue.noteId = noteId;
    result.noteExpressionValue.value = std::clamp(normalizedValue, 0.0, 1.0);

    return result;
}

int32_t VstSequencer::noteIndex(const mpe::pitch_level_t pitchLevel) const
{
    return audioplugins::keyNumberOfPitchLevel(pitchLevel);
}

float VstSequencer::noteTuning(const mpe::NoteEvent& noteEvent, const int noteIdx) const
{
    return static_cast<float>(audioplugins::centsOfPitchLevel(noteEvent.pitchCtx().nominalPitchLevel, noteIdx));
}

float VstSequencer::noteVelocityFraction(const mpe::NoteEvent& noteEvent) const
{
    const mpe::ExpressionContext& expressionCtx = noteEvent.expressionCtx();

    if (expressionCtx.velocityOverride.has_value()) {
        return std::clamp(expressionCtx.velocityOverride.value(), 0.f, 1.f);
    }

    mpe::dynamic_level_t dynamicLevel = expressionCtx.expressionCurve.maxAmplitudeLevel();
    return expressionLevel(dynamicLevel);
}

float VstSequencer::expressionLevel(const mpe::dynamic_level_t dynamicLevel) const
{
    static constexpr mpe::dynamic_level_t MIN_SUPPORTED_DYNAMIC_LEVEL = mpe::dynamicLevelFromType(mpe::DynamicType::ppp);
    static constexpr mpe::dynamic_level_t MAX_SUPPORTED_DYNAMIC_LEVEL = mpe::dynamicLevelFromType(mpe::DynamicType::fff);
    static constexpr mpe::dynamic_level_t AVAILABLE_RANGE = MAX_SUPPORTED_DYNAMIC_LEVEL - MIN_SUPPORTED_DYNAMIC_LEVEL;

    if (dynamicLevel <= MIN_SUPPORTED_DYNAMIC_LEVEL) {
        return (0.5f * mpe::ONE_PERCENT) / AVAILABLE_RANGE;
    }

    if (dynamicLevel >= MAX_SUPPORTED_DYNAMIC_LEVEL) {
        return 1.f;
    }

    return RealRound((dynamicLevel - MIN_SUPPORTED_DYNAMIC_LEVEL) / static_cast<float>(AVAILABLE_RANGE), 2);
}

float VstSequencer::pitchBendLevel(const mpe::pitch_level_t pitchLevel) const
{
    static constexpr float SEMITONE_RANGE = 2.f;
    static constexpr float PITCH_BEND_SEMITONE_STEP = 0.5f / SEMITONE_RANGE;

    float pitchLevelSteps = pitchLevel / static_cast<float>(mpe::PITCH_LEVEL_STEP);
    float offset = pitchLevelSteps * PITCH_BEND_SEMITONE_STEP;

    return std::clamp(0.5f + offset, 0.f, 1.f);
}
