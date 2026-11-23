/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "clapsequencer.h"

#include <algorithm>
#include <cmath>

#include "global/interpolation.h"
#include "global/realfn.h"

using namespace muse;
using namespace muse::clap;

namespace muse::clap {
static const mpe::ArticulationTypeSet CLAP_BEND_SUPPORTED_TYPES {
    mpe::ArticulationType::Multibend, mpe::ArticulationType::ContinuousGlissando,
};
}

void ClapSequencer::init(bool useDynamicEvents)
{
    m_useDynamicEvents = useDynamicEvents;
    m_inited = true;

    updateMainStreamEvents(m_playbackData.originEvents, m_playbackData.dynamics);
}

int32_t ClapSequencer::allocateNoteId()
{
    const int32_t id = m_nextNoteId;
    if (m_nextNoteId >= MAX_NOTE_ID) {
        // Ids start again only after every sounding note has been ended
        // under its own id: the flush callbacks do that.
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

void ClapSequencer::updateOffStreamEvents(const mpe::PlaybackEventsMap& events, const mpe::DynamicLevelLayers& dynamics)
{
    addPlaybackEvents(m_offStreamEvents, events);
    orderEventsOfOneMoment(m_offStreamEvents);

    if (m_useDynamicEvents) {
        addDynamicEvents(m_offStreamEvents, dynamics);
    }

    updateOffSequenceIterator();
}

void ClapSequencer::updateMainStreamEvents(const mpe::PlaybackEventsMap& events, const mpe::DynamicLevelLayers& dynamics)
{
    if (!m_inited) {
        return;
    }

    m_mainStreamEvents.clear();

    // The notes of the stream that is replaced get new ids below; the ones
    // that are sounding are ended now, or their releases would never come.
    if (m_onMainStreamFlushed) {
        m_onMainStreamFlushed();
    }

    addPlaybackEvents(m_mainStreamEvents, events);
    orderEventsOfOneMoment(m_mainStreamEvents);

    if (m_useDynamicEvents) {
        addDynamicEvents(m_mainStreamEvents, dynamics);
    }

    updateMainSequenceIterator();
}

muse::audio::gain_t ClapSequencer::currentGain() const
{
    if (m_useDynamicEvents) {
        return expressionLevel(dynamicLevel(m_playbackPosition));
    }

    return 0.5f;
}

void ClapSequencer::addPlaybackEvents(EventSequenceMap& destination, const mpe::PlaybackEventsMap& events)
{
    for (const auto& evPair : events) {
        for (const mpe::PlaybackEvent& event : evPair.second) {
            if (std::holds_alternative<mpe::NoteEvent>(event)) {
                addNoteEvent(destination, std::get<mpe::NoteEvent>(event));
            }
        }
    }
}

void ClapSequencer::addDynamicEvents(EventSequenceMap& destination, const mpe::DynamicLevelLayers& layers)
{
    for (const auto& layer : layers) {
        for (const auto& dynamic : layer.second) {
            destination[dynamic.first].emplace_back(expressionLevel(dynamic.second));
        }
    }
}

void ClapSequencer::addNoteEvent(EventSequenceMap& destination, const mpe::NoteEvent& noteEvent)
{
    const mpe::ArrangementContext& arrangementCtx = noteEvent.arrangementCtx();
    const int32_t noteId = allocateNoteId();

    // A note of the MeloPresto Music System carries the key number and the
    // offset in cents that the Kernel computed for it. Any other note is its
    // nearest key and the rest of its pitch level, as the stock sequencers
    // count them.
    const std::optional<mpe::ExactPitch>& exact = noteEvent.pitchCtx().exactPitch;
    const int32_t keyNumber = exact.has_value() ? std::clamp(exact->midiKey, 0, 127)
                              : keyNumberOf(noteEvent.pitchCtx().nominalPitchLevel);
    const double cents = exact.has_value() ? exact->centsOffset : centsOf(noteEvent, keyNumber);
    const float velocity = velocityOf(noteEvent);

    if (arrangementCtx.hasStart()) {
        destination[arrangementCtx.actualTimestamp].emplace_back(
            ClapNoteEvent { ClapNoteEvent::Kind::NoteOn, noteId, keyNumber, cents, velocity });
    }

    if (arrangementCtx.hasEnd()) {
        const mpe::timestamp_t timestampTo = arrangementCtx.actualTimestamp + arrangementCtx.actualDuration;
        destination[timestampTo].emplace_back(ClapNoteEvent { ClapNoteEvent::Kind::NoteOff, noteId, keyNumber, cents, velocity });
    }

    for (const auto& artPair : noteEvent.expressionCtx().articulations) {
        const mpe::ArticulationMeta& meta = artPair.second.meta;
        if (!noteEvent.pitchCtx().pitchCurve.empty() && muse::contains(CLAP_BEND_SUPPORTED_TYPES, meta.type)) {
            addPitchCurve(destination, noteEvent, meta, noteId, keyNumber, cents);
        }
    }
}

void ClapSequencer::addPitchCurve(EventSequenceMap& destination, const mpe::NoteEvent& noteEvent, const mpe::ArticulationMeta& artMeta,
                                  int32_t noteId, int32_t keyNumber, double baseCents)
{
    // Every point of the curve is a new pitch for this note alone: its base
    // offset plus the curve's own, one 12-TET step being 100 cents.
    const mpe::timestamp_t noteTimestampTo = noteEvent.arrangementCtx().actualTimestamp + noteEvent.arrangementCtx().actualDuration;
    const mpe::timestamp_t curveTimestampTo = std::min(artMeta.timestamp + artMeta.overallDuration, noteTimestampTo);

    auto centsAt = [baseCents](const double pitchLevelOffset) {
        return baseCents + (pitchLevelOffset / static_cast<double>(mpe::PITCH_LEVEL_STEP)) * 100.0;
    };
    auto change = [noteId, keyNumber](const double cents) {
        return ClapNoteEvent { ClapNoteEvent::Kind::PitchChange, noteId, keyNumber, cents, 0.f };
    };

    auto currIt = noteEvent.pitchCtx().pitchCurve.cbegin();
    auto nextIt = std::next(currIt);
    auto endIt = noteEvent.pitchCtx().pitchCurve.cend();

    // A curve of one point still states the note's pitch at its start.
    if (nextIt == endIt && currIt != endIt) {
        destination[artMeta.timestamp].emplace_back(change(centsAt(currIt->second)));
        return;
    }

    bool any = false;
    double previous = 0.0;

    for (; nextIt != endIt; currIt = nextIt, nextIt = std::next(currIt)) {
        const mpe::timestamp_t currTime = artMeta.timestamp + artMeta.overallDuration * mpe::percentageToFactor(currIt->first);
        const mpe::timestamp_t nextTime = artMeta.timestamp + artMeta.overallDuration * mpe::percentageToFactor(nextIt->first);

        using namespace muse::interpolation;
        const Point currPoint { static_cast<double>(currTime), centsAt(currIt->second) };
        const Point nextPoint { static_cast<double>(nextTime), centsAt(nextIt->second) };

        //! NOTE: the same density of points as the stock sequencers use for a bend
        constexpr mpe::pitch_level_t POINT_WEIGHT = mpe::PITCH_LEVEL_STEP / 25;
        size_t pointCount = std::abs(nextIt->second - currIt->second) / POINT_WEIGHT;
        pointCount = std::max(pointCount, size_t(1));

        for (const Point& point : lerp(currPoint, nextPoint, pointCount)) {
            const mpe::timestamp_t time = static_cast<mpe::timestamp_t>(std::round(point.x));
            if (time < curveTimestampTo && (!any || !RealIsEqual(previous, point.y))) {
                destination[time].emplace_back(change(point.y));
            }
            previous = point.y;
            any = true;
        }
    }
}

//! At one moment: the releases of notes that began earlier come first, so a
//! receiver that knows notes only by key hears a repeated key struck again;
//! then the new notes from the lowest key up, as the stock sequencers order
//! them; then pitch changes; then the releases of notes that began at this
//! same moment, so that a note of no length is not left sounding.
void ClapSequencer::orderEventsOfOneMoment(EventSequenceMap& destination)
{
    for (auto& moment : destination) {
        EventSequence& sequence = moment.second;
        if (sequence.size() <= 1) {
            continue;
        }

        auto noteOf = [](const EventType& event) -> const ClapNoteEvent* {
            return std::holds_alternative<ClapNoteEvent>(event) ? &std::get<ClapNoteEvent>(event) : nullptr;
        };
        auto startsHere = [&sequence, &noteOf](int32_t noteId) {
            for (const EventType& event : sequence) {
                const ClapNoteEvent* note = noteOf(event);
                if (note && note->kind == ClapNoteEvent::Kind::NoteOn && note->noteId == noteId) {
                    return true;
                }
            }
            return false;
        };
        auto rank = [&noteOf, &startsHere](const EventType& event) {
            const ClapNoteEvent* note = noteOf(event);
            if (!note) {
                return 4;
            }
            switch (note->kind) {
            case ClapNoteEvent::Kind::NoteOff: return startsHere(note->noteId) ? 3 : 0;
            case ClapNoteEvent::Kind::NoteOn: return 1;
            case ClapNoteEvent::Kind::PitchChange: return 2;
            }
            return 4;
        };

        std::stable_sort(sequence.begin(), sequence.end(), [&rank, &noteOf](const EventType& first, const EventType& second) {
            const int firstRank = rank(first);
            const int secondRank = rank(second);
            if (firstRank != secondRank) {
                return firstRank < secondRank;
            }
            if (firstRank == 1) {
                return noteOf(first)->keyNumber < noteOf(second)->keyNumber;
            }
            return false;
        });
    }
}

int32_t ClapSequencer::keyNumberOf(const mpe::pitch_level_t pitchLevel) const
{
    const float stepCount = mpe::ZERO_PITCH_LEVEL_MIDI_EQUIVALENT + pitchLevel / static_cast<float>(mpe::PITCH_LEVEL_STEP);

    return static_cast<int32_t>(std::clamp(stepCount, 0.f, 127.f));
}

double ClapSequencer::centsOf(const mpe::NoteEvent& noteEvent, int32_t keyNumber) const
{
    const int stepCount = keyNumber - mpe::ZERO_PITCH_LEVEL_MIDI_EQUIVALENT;
    const mpe::pitch_level_t rest = noteEvent.pitchCtx().nominalPitchLevel - stepCount * mpe::PITCH_LEVEL_STEP;

    return (rest / static_cast<double>(mpe::PITCH_LEVEL_STEP)) * 100.0;
}

float ClapSequencer::velocityOf(const mpe::NoteEvent& noteEvent) const
{
    const mpe::ExpressionContext& expressionCtx = noteEvent.expressionCtx();

    if (expressionCtx.velocityOverride.has_value()) {
        return std::clamp(expressionCtx.velocityOverride.value(), 0.f, 1.f);
    }

    return expressionLevel(expressionCtx.expressionCurve.maxAmplitudeLevel());
}

float ClapSequencer::expressionLevel(const mpe::dynamic_level_t dynamicLevel) const
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
