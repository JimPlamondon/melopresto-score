/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * JiMStaff Milestone 6 — change-insertion controller (owner decision 1a,
 * 2026-08-16). Transports Kernel-returned states into the StaffTypeChange
 * carrier; computes no musical fact.
 */
#include "engraving/melo/melostrings.h"
#include "melochangecontroller.h"

#include "../dom/factory.h"
#include "../dom/chord.h"
#include "../dom/measure.h"
#include "../dom/masterscore.h"
#include "../dom/note.h"
#include "../dom/part.h"
#include "../dom/score.h"
#include "../dom/segment.h"
#include "../dom/staff.h"
#include "../dom/stafftype.h"
#include "../dom/stafftypechange.h"
#include "../dom/stafflines.h"
#include "../dom/tie.h"
#include "../dom/utils.h"
#include "../editing/undo.h"
#include "../editing/editscoreproperties.h"
#include "serialization/json.h"
#include "melochange.h"
#include "melobridge.h"

#include "translation.h"

#include <cmath>
#include <set>
#include <limits>
#include <optional>

using namespace muse;

namespace mu::engraving::melo {
namespace {
struct StateEdit {
    Staff* staff = nullptr;
    staff_idx_t staffIdx = 0;
    Fraction tick;
    Fraction stop { -1, 1 };
    String state;
    bool referenceOnly = false;
};

Fraction nextCarrierTick(const Score* score, staff_idx_t staffIdx, const Fraction& tick)
{
    Measure* start = score->tick2measure(tick);
    for (Measure* measure = start; measure; measure = measure->nextMeasure()) {
        for (const StaffTypeChange* carrier : changeCarriers(measure, staffIdx)) {
            if (carrier->tick() > tick) {
                return carrier->tick();
            }
        }
    }
    return Fraction(-1, 1);
}

const StateEdit* stateEditFor(const std::vector<StateEdit>& edits, const Note* note)
{
    for (const StateEdit& edit : edits) {
        if (note->staff() != edit.staff || note->tick() < edit.tick) {
            continue;
        }
        if (edit.stop.negative() || note->tick() < edit.stop) {
            return &edit;
        }
    }
    return nullptr;
}

bool projectionFor(const std::vector<StateEdit>& edits, Note* note, SoundingPitch& projection, String& error)
{
    const StateEdit* edit = stateEditFor(edits, note);
    const StaffType* current = note->staff() ? note->staff()->staffTypeForElement(note) : nullptr;
    const String state = edit ? edit->state : (current ? current->meloStateJson() : String());
    if (state.isEmpty()) {
        error = mu::engraving::melo::linkedNoteMissingState();
        return false;
    }

    Note* first = note;
    while (first->tieBackNonPartial() && first->tieBackNonPartial()->startNote()) {
        first = first->tieBackNonPartial()->startNote();
    }
    if (edit && first->tick() < edit->tick) {
        SoundingPitch established;
        const StateEdit* firstEdit = stateEditFor(edits, first);
        const StaffType* firstCurrent = first->staff() ? first->staff()->staffTypeForElement(first) : nullptr;
        const String firstState = firstEdit ? firstEdit->state : (firstCurrent ? firstCurrent->meloStateJson() : String());
        if (firstState.isEmpty()
            || !noteSoundingPitch(firstState, first->meloNPer(), first->meloNGen(), established, &error)) {
            return false;
        }
        // A mode edit can leave the known lattice note sounding unchanged.
        // Preserve that identity before considering a different continuation.
        if (noteSoundingPitch(state, note->meloNPer(), note->meloNGen(), projection, &error)
            && std::abs(projection.frequencyHz - established.frequencyHz) < 1e-9) {
            return true;
        }
        return noteContinuation(state, established.frequencyHz, projection, &error);
    }
    return noteSoundingPitch(state, note->meloNPer(), note->meloNGen(), projection, &error);
}

bool sameProjection(const SoundingPitch& a, const SoundingPitch& b)
{
    return a.nPer == b.nPer && a.nGen == b.nGen && a.midiKey == b.midiKey
           && a.step == b.step && a.alter == b.alter && a.octave == b.octave
           && std::abs(a.centsOffset - b.centsOffset) < 1e-9
           && std::abs(a.frequencyHz - b.frequencyHz) < 1e-9;
}

bool prepareNoteEdits(Score*, const std::vector<StateEdit>& stateEdits,
                      std::vector<NoteEdit>& noteEdits, String& error)
{
    std::set<Note*> seen;
    for (const StateEdit& edit : stateEdits) {
        Measure* start = edit.staff->score()->tick2measure(edit.tick);
        for (Measure* measure = start; measure; measure = measure->nextMeasure()) {
            if (!edit.stop.negative() && measure->tick() > edit.stop) {
                break;
            }
            for (Segment* segment = measure->first(SegmentType::ChordRest); segment;
                 segment = segment->next(SegmentType::ChordRest)) {
                for (voice_idx_t voice = 0; voice < VOICES; ++voice) {
                    EngravingItem* item = segment->element(edit.staffIdx * VOICES + voice);
                    if (!item || !item->isChord()) {
                        continue;
                    }
                    std::vector<Chord*> chords = toChord(item)->graceNotes();
                    chords.push_back(toChord(item));
                    for (Chord* chord : chords) {
                        for (Note* note : chord->notes()) {
                            if (note->tick() < edit.tick || (!edit.stop.negative() && note->tick() >= edit.stop)) {
                                continue;
                            }
                            if (!note->hasMeloPitch() || seen.count(note)) {
                                continue;
                            }
                            if (note->incomingPartialTie() || note->outgoingPartialTie()) {
                                error = mu::engraving::melo::partialTieCrossesState();
                                return false;
                            }
                            SoundingPitch projection;
                            if (!projectionFor(stateEdits, note, projection, error)) {
                                return false;
                            }
                            for (EngravingObject* linkedObject : note->linkList()) {
                                Note* linked = toNote(linkedObject);
                                if (!linked->hasMeloPitch()) {
                                    error = mu::engraving::melo::linkedNoteIdentityMismatch();
                                    return false;
                                }
                                SoundingPitch linkedProjection;
                                if (!projectionFor(stateEdits, linked, linkedProjection, error)) {
                                    return false;
                                }
                                if (!sameProjection(projection, linkedProjection)) {
                                    error = mu::engraving::melo::conflictingLinkedProjections();
                                    return false;
                                }
                                seen.insert(linked);
                            }
                            const int step = int(String(u"CDEFGAB").indexOf(Char(projection.step)));
                            noteEdits.push_back({ note, projection, step2tpc(step, AccidentalVal(projection.alter)) });
                        }
                    }
                }
            }
        }
    }
    return true;
}

void commitNoteEdits(Score* score, const std::vector<NoteEdit>& edits)
{
    for (const NoteEdit& edit : edits) {
        edit.note->undoChangeProperty(Pid::MELO_NPER, edit.projection.nPer);
        edit.note->undoChangeProperty(Pid::MELO_NGEN, edit.projection.nGen);
        score->undoChangePitch(edit.note, edit.projection.midiKey, edit.tpc, edit.tpc);
        edit.note->undoChangeProperty(Pid::TUNING, edit.projection.centsOffset);
    }
}

// Persisted coordinates are structural. Loading may repair projections only;
// state-edit continuation is intentionally not used by this validation path.
bool prepareStoredProjections(const Score* score, std::vector<NoteEdit>& projected, String& error)
{
    std::set<Note*> seen;
    for (Segment* segment = score->firstSegment(SegmentType::ChordRest); segment;
         segment = segment->next1(SegmentType::ChordRest)) {
        for (track_idx_t track = 0; track < score->ntracks(); ++track) {
            EngravingItem* item = segment->element(track);
            if (!item || !item->isChord()) {
                continue;
            }
            std::vector<Chord*> chords = toChord(item)->graceNotes();
            chords.push_back(toChord(item));
            for (Chord* chord : chords) {
                for (Note* note : chord->notes()) {
                    const StaffType* type = note->staff()->staffTypeForElement(note);
                    if (!type->isMelo() || seen.count(note)) {
                        continue;
                    }
                    SoundingPitch projection;
                    if (!note->hasMeloPitch()
                        || !noteSoundingPitch(type->meloStateJson(), note->meloNPer(), note->meloNGen(), projection, &error)) {
                        error = mtrc("engraving", "A lattice note has missing or invalid coordinates.");
                        return false;
                    }
                    if (Tie* tie = note->tieBackNonPartial()) {
                        Note* start = tie->startNote();
                        const StaffType* startType = start && start->staff() ? start->staff()->staffTypeForElement(start) : nullptr;
                        SoundingPitch startProjection;
                        if (!startType || !startType->isMelo() || !start->hasMeloPitch()
                            || !noteSoundingPitch(startType->meloStateJson(), start->meloNPer(), start->meloNGen(), startProjection, &error)
                            || std::abs(startProjection.frequencyHz - projection.frequencyHz) >= 1e-9) {
                            error = mtrc("engraving",
                                         "A full tie connects contradictory lattice sounds. Its coordinates were not changed.");
                            return false;
                        }
                    }
                    for (EngravingObject* object : note->linkList()) {
                        Note* linked = toNote(object);
                        const StaffType* linkedType = linked->staff() ? linked->staff()->staffTypeForElement(linked) : nullptr;
                        SoundingPitch linkedProjection;
                        if (!linkedType || !linkedType->isMelo() || !linked->hasMeloPitch()
                            || !noteSoundingPitch(linkedType->meloStateJson(), linked->meloNPer(), linked->meloNGen(), linkedProjection,
                                                  &error)
                            || !sameProjection(projection, linkedProjection)) {
                            error = conflictingLinkedProjections();
                            return false;
                        }
                        seen.insert(linked);
                    }
                    const int step = int(String(u"CDEFGAB").indexOf(Char(projection.step)));
                    projected.push_back({ note, projection, step2tpc(step, AccidentalVal(projection.alter)) });
                }
            }
        }
    }
    return true;
}

bool validateReferences(const Score* score, const std::vector<StateEdit>& edits, String& error)
{
    std::set<Fraction> ticks { Fraction(0, 1) };
    std::vector<const Staff*> staves;
    for (const Score* related : score->masterScore()->scoreList()) {
        for (const Staff* staff : related->staves()) {
            staves.push_back(staff);
        }
        for (const Measure* measure = related->firstMeasure(); measure; measure = measure->nextMeasure()) {
            for (const EngravingItem* item : measure->el()) {
                if (item->isStaffTypeChange()) {
                    ticks.insert(item->tick());
                }
            }
        }
    }
    for (const StateEdit& edit : edits) {
        ticks.insert(edit.tick);
    }
    for (const Fraction& tick : ticks) {
        String first;
        staff_idx_t firstIdx = 0;
        for (const Staff* staff : staves) {
            const StaffType* type = staff->staffType(tick);
            if (!type || !type->isMelo()) {
                continue;
            }
            String state = type->meloStateJson();
            for (const StateEdit& edit : edits) {
                if (edit.staff == staff && edit.tick <= tick && (edit.stop.negative() || tick < edit.stop)) {
                    state = edit.state;
                    break;
                }
            }
            if (!validateState(state, error)) {
                return false;
            }
            if (first.isEmpty()) {
                first = state;
                firstIdx = staff->idx();
                continue;
            }
            bool same = false;
            if (!sameReference(first, state, same, &error)) {
                return false;
            }
            if (!same) {
                error = mtrc("engraving", "Reference Pitch disagrees between staves %1 and %2 at tick %3.")
                        .arg(int(firstIdx) + 1).arg(int(staff->idx()) + 1).arg(tick.ticks());
                return false;
            }
        }
    }
    return true;
}

/// Replace the MeloPresto state of the staff type in force at `tick` on `staff`
/// (the base type or a carrier's copy in the staff's list) — one undoable
/// flip, layout invalidated (the same shape the tuning controller uses).
class MeloChangeStateAt : public UndoCommand
{
    OBJECT_ALLOCATOR(engraving, MeloChangeStateAt)

    Staff* m_staff = nullptr;
    Fraction m_tick;
    String m_state;
    bool m_emptyDefault;
    std::optional<bool> m_referenceOnly;

    void flip(EditData*) override
    {
        StaffType* st = m_staff->staffType(m_tick);
        if (!st) {
            return;
        }
        String previous = st->meloStateJson();
        const bool previousEmpty = st->meloExtentIsEmptyDefault();
        st->setMeloStateJson(m_state);
        st->setMeloExtentIsEmptyDefault(m_emptyDefault);
        m_state = previous;
        m_emptyDefault = previousEmpty;
        if (m_referenceOnly) {
            auto* carrier = const_cast<StaffTypeChange*>(changeCarrierAt(m_staff->score()->tick2measure(m_tick), m_staff->idx(), m_tick));
            if (carrier) {
                const bool previous = carrier->meloReferenceOnly();
                carrier->setMeloReferenceOnly(*m_referenceOnly);
                m_referenceOnly = previous;
            }
        }
        m_staff->staffTypeListChanged(m_tick);
        m_staff->score()->setLayoutAll();
    }

public:
    MeloChangeStateAt(Staff* staff, const Fraction& tick, String state, std::optional<bool> referenceOnly = std::nullopt)
        : m_staff(staff), m_tick(tick), m_state(std::move(state)),
        m_emptyDefault(staffSpanIsEmpty(staff, tick, nextCarrierTick(staff->score(), staff->idx(), tick))), m_referenceOnly(referenceOnly)
    {
    }

    UNDO_NAME("MeloChangeStateAt")
    UNDO_CHANGED_OBJECTS({ m_staff })
};

const StaffTypeChange* anyCarrierAt(const Measure* measure, staff_idx_t staffIdx, const Fraction& tick)
{
    for (const EngravingItem* el : measure->el()) {
        if (el && el->isStaffTypeChange() && el->staffIdx() == staffIdx) {
            const StaffTypeChange* carrier = toStaffTypeChange(el);
            if (carrier->tick() == tick) {
                return carrier;
            }
        }
    }
    return nullptr;
}

String exactTimeJson(const Fraction& tick)
{
    JsonObject time;
    time.set("numerator", tick.numerator());
    time.set("denominator", tick.denominator());
    return String::fromUtf8(JsonDocument(time).toJson());
}

bool prepareCanonicalReferenceStates(Score* score, const String& timeline, std::vector<StateEdit>& edits, String& error,
                                     const std::vector<StateEdit>& configurations = {})
{
    std::set<Fraction> eventTimes;
    const JsonArray events = JsonDocument::fromJson(timeline.toUtf8()).rootObject().value("events").toArray();
    for (size_t i = 0; i < events.size(); ++i) {
        const JsonObject time = events[i].toObject().value("at").toObject();
        const double n = time.value("numerator").toDouble();
        const double d = time.value("denominator").toDouble();
        if (!std::isfinite(n) || !std::isfinite(d) || n < 0 || d <= 0 || std::floor(n) != n || std::floor(d) != d
            || n > std::numeric_limits<int>::max() || d > std::numeric_limits<int>::max()) {
            error = mtrc("engraving", "This exact key-change time exceeds Score's supported range.");
            return false;
        }
        eventTimes.insert(Fraction(int(n), int(d)));
    }
    const std::list<Score*> relatedScores = score->isMaster() ? score->scoreList() : std::list<Score*> { score };
    for (Score* related : relatedScores) {
        for (Staff* staff : related->staves()) {
            const StaffType* initial = staff->staffType(Fraction(0, 1));
            Fraction firstMelo(0, 1);
            if (!initial || !initial->isMelo()) {
                initial = nullptr;
                for (const Measure* measure = related->firstMeasure(); measure && !initial; measure = measure->nextMeasure()) {
                    const auto carriers = changeCarriers(measure, staff->idx());
                    if (!carriers.empty()) {
                        initial = carriers.front()->staffType();
                        firstMelo = carriers.front()->tick();
                    }
                }
                if (!initial) {
                    continue;
                }
            }
            std::map<Fraction, String> types;
            String initialConfiguration;
            if (!staffConfiguration(initial->meloStateJson(), initialConfiguration, error)) {
                return false;
            }
            types[Fraction(0, 1)] = initialConfiguration;
            String previousConfiguration = initialConfiguration;
            for (const Measure* measure = related->firstMeasure(); measure; measure = measure->nextMeasure()) {
                for (const StaffTypeChange* carrier : changeCarriers(measure, staff->idx())) {
                    if (carrier->meloReferenceOnly()) {
                        continue;
                    }
                    String configuration;
                    if (!carrier->staffType() || !carrier->staffType()->isMelo()
                        || !staffConfiguration(carrier->staffType()->meloStateJson(), configuration, error)) {
                        return false;
                    }
                    // Equal consecutive configuration records add no authored
                    // change, even if an interchange transport repeats them.
                    if (configuration != previousConfiguration || carrier->tick() == firstMelo) {
                        types[carrier->tick()] = configuration;
                    }
                    previousConfiguration = configuration;
                }
            }
            for (const auto& replacement : configurations) {
                if (replacement.staff != staff) {
                    continue;
                }
                if (replacement.state.isEmpty()) {
                    types.erase(replacement.tick);
                } else if (!staffConfiguration(replacement.state, types[replacement.tick], error)) {
                    return false;
                }
            }
            String history = u"[";
            std::set<Fraction> times = eventTimes;
            for (const auto& entry : types) {
                if (history != u"[") {
                    history += u",";
                }
                history += u"{\"at\":" + exactTimeJson(entry.first) + u",\"configuration\":" + entry.second + u"}";
                times.insert(entry.first);
            }
            history += u"]";
            // Conventional spans have no MeloPresto context. Retain their
            // boundaries so a prepared MeloPresto span never crosses them.
            for (const Measure* measure = related->firstMeasure(); measure; measure = measure->nextMeasure()) {
                for (const EngravingItem* item : measure->el()) {
                    if (item->isStaffTypeChange() && item->staffIdx() == staff->idx()
                        && !toStaffTypeChange(item)->staffType()->isMelo()) {
                        times.insert(item->tick());
                    }
                }
            }
            for (auto at = times.begin(); at != times.end(); ++at) {
                if (*at < firstMelo || !staff->staffType(*at)->isMelo()) {
                    continue;
                }
                Measure* measure = related->tick2measure(*at);
                if (!measure || *at >= measure->endTick()
                    || (!at->isZero() && !canInsertChange(related, staff->idx(), measure, *at, error))) {
                    if (error.isEmpty()) {
                        error = mtrc("engraving", "A key change is outside the score's editable musical positions.");
                    }
                    return false;
                }
                String request;
                if (!staffRequest(history, timeline, exactTimeJson(*at), request, error)) {
                    return false;
                }
                const auto next = std::next(at);
                edits.push_back({ staff, staff->idx(), *at, next == times.end() ? Fraction(-1, 1) : *next, request,
                                  types.find(*at) == types.end() });
            }
        }
    }
    if (edits.empty()) {
        error = mu::engraving::melo::canonicalStaffUnavailable();
        return false;
    }
    return validateReferences(score, edits, error);
}

void installCanonicalReferenceStates(Score* score, const std::vector<StateEdit>& edits, bool undoable)
{
    std::set<Staff*> staves;
    for (const auto& edit : edits) {
        staves.insert(edit.staff);
    }
    for (Staff* staff : staves) {
        for (Measure* measure = staff->score()->firstMeasure(); measure; measure = measure->nextMeasure()) {
            for (const auto* carrier : changeCarriers(measure, staff->idx())) {
                const bool retained = std::any_of(edits.begin(), edits.end(), [&](const auto& edit) {
                    return edit.staff == staff && edit.tick == carrier->tick();
                });
                if (retained) {
                    continue;
                }
                if (undoable) {
                    score->undoRemoveElement(const_cast<StaffTypeChange*>(carrier));
                } else {
                    measure->remove(const_cast<StaffTypeChange*>(carrier));
                    delete carrier;
                }
            }
        }
    }
    for (const StateEdit& edit : edits) {
        Score* owner = edit.staff->score();
        Measure* measure = owner->tick2measure(edit.tick);
        if (edit.tick.isZero() || changeCarrierAt(measure, edit.staffIdx, edit.tick)) {
            if (undoable) {
                score->undo(new MeloChangeStateAt(edit.staff, edit.tick, edit.state, edit.referenceOnly));
            } else {
                edit.staff->staffType(edit.tick)->setMeloStateJson(edit.state);
                auto* carrier = const_cast<StaffTypeChange*>(changeCarrierAt(measure, edit.staffIdx, edit.tick));
                if (carrier) {
                    carrier->setMeloReferenceOnly(edit.referenceOnly);
                }
                edit.staff->staffTypeListChanged(edit.tick);
            }
        } else {
            StaffTypeChange* carrier = Factory::createStaffTypeChange(measure);
            carrier->setParent(measure);
            carrier->setRtick(edit.tick - measure->tick());
            carrier->setTrack(edit.staffIdx * VOICES);
            carrier->setMeloReferenceOnly(edit.referenceOnly);
            StaffType* type = new StaffType(*edit.staff->staffType(edit.tick));
            type->setMeloStateJson(edit.state);
            carrier->setStaffType(type, true);
            if (undoable) {
                score->undoAddElement(carrier);
            } else {
                measure->add(carrier);
            }
        }
        owner->setLayoutAll();
    }
}

bool commitCanonicalTimeline(Score* score, const String& timeline, const String& expected, const TranslatableString& action, String& error)
{
    Score* composition = score->masterScore();
    if (composition->metaTag(REFERENCE_TIMELINE_TAG) != expected) {
        error = mtrc("engraving", "The reference changed while this editor was open. Reopen the editor and try again.");
        return false;
    }
    if (!validateLatticeContent(composition, error) || !validateSharedStateTimeline(composition, error)) {
        return false;
    }
    if (timeline == expected) {
        return true;
    }
    std::vector<StateEdit> edits;
    std::vector<NoteEdit> notes;
    if (!prepareCanonicalReferenceStates(composition, timeline, edits, error)
        || !prepareNoteEdits(composition, edits, notes, error)) {
        return false;
    }
    composition->startCmd(action);
    composition->undo(new ChangeMetaText(composition, REFERENCE_TIMELINE_TAG, timeline));
    installCanonicalReferenceStates(composition, edits, true);
    commitNoteEdits(composition, notes);
    composition->endCmd();
    return true;
}

bool commitCanonicalConfigurations(Score* score, const std::vector<StateEdit>& configurations, String& error)
{
    if (configurations.empty()) {
        return true;
    }
    Score* composition = score->masterScore();
    if (!validateLatticeContent(composition, error) || !validateSharedStateTimeline(composition, error)) {
        return false;
    }
    std::vector<StateEdit> edits;
    std::vector<NoteEdit> notes;
    if (!prepareCanonicalReferenceStates(composition, composition->metaTag(REFERENCE_TIMELINE_TAG), edits, error, configurations)
        || !prepareNoteEdits(composition, edits, notes, error)) {
        return false;
    }
    composition->startCmd(mu::engraving::melo::insertChangeAction());
    installCanonicalReferenceStates(composition, edits, true);
    commitNoteEdits(composition, notes);
    composition->endCmd();
    return true;
}
}

bool initializeAuthoredMeloStaves(Score* score, const std::vector<Staff*>& staves, String& error)
{
    if (!score || !score->firstMeasure()) {
        return false;
    }
    Score* composition = score->masterScore();
    std::set<Staff*> authored;
    for (Staff* staff : staves) {
        if (staff && staff->score()->masterScore() == composition && staff->staffType(Fraction(0, 1))->isMelo()) {
            authored.insert(staff);
        }
    }
    if (authored.empty()) {
        return true;
    }
    const Staff* source = nullptr;
    for (const Staff* staff : composition->staves()) {
        if (!authored.count(const_cast<Staff*>(staff)) && staff->staffType(Fraction(0, 1))->isMelo()) {
            source = staff;
            break;
        }
    }
    String timeline = composition->metaTag(REFERENCE_TIMELINE_TAG);
    const bool createRoot = timeline.isEmpty();
    if (!createRoot && !source) {
        error = mtrc("engraving", "The existing reference has no surviving staff configuration history to inherit.");
        return false;
    }
    if (createRoot && !defaultReferenceTimeline(timeline, error)) {
        return false;
    }
    std::map<Fraction, String> shared;
    if (source) {
        if (!staffConfiguration(source->staffType(Fraction(0, 1))->meloStateJson(), shared[Fraction(0, 1)], error)) {
            return false;
        }
        for (const Measure* measure = composition->firstMeasure(); measure; measure = measure->nextMeasure()) {
            for (const StaffTypeChange* carrier : changeCarriers(measure, source->idx())) {
                if (carrier->meloReferenceOnly()) {
                    continue;
                }
                if (!staffConfiguration(carrier->staffType()->meloStateJson(), shared[carrier->tick()], error)) {
                    return false;
                }
            }
        }
    }
    std::vector<StateEdit> configurations;
    for (Staff* staff : authored) {
        String local;
        if (!storedStaffConfiguration(staff->staffType(Fraction(0, 1))->meloStateJson(), local, error)) {
            return false;
        }
        for (const auto& entry : shared) {
            String configuration;
            if (!inheritStaffConfiguration(entry.second, local, configuration, error)) {
                return false;
            }
            configurations.push_back({ staff, staff->idx(), entry.first, Fraction(-1, 1), configuration });
        }
    }
    std::vector<StateEdit> edits;
    std::vector<NoteEdit> notes;
    if (!prepareCanonicalReferenceStates(composition, timeline, edits, error, configurations)
        || !prepareNoteEdits(composition, edits, notes, error)) {
        return false;
    }
    if (createRoot) {
        composition->undo(new ChangeMetaText(composition, REFERENCE_TIMELINE_TAG, timeline));
    }
    installCanonicalReferenceStates(composition, edits, true);
    commitNoteEdits(composition, notes);
    return true;
}

bool initializeNewMeloComposition(Score* score, String& error)
{
    if (!score || !score->isMaster() || !score->firstMeasure()) {
        error = mu::engraving::melo::createMeasuresBeforeReference();
        return false;
    }
    if (!score->metaTag(REFERENCE_TIMELINE_TAG).isEmpty()) {
        return validateLatticeContent(score, error);
    }
    bool hasMelo = false;
    for (const Staff* staff : score->staves()) {
        const StaffType* type = staff->staffType(Fraction(0, 1));
        if (!type || !type->isMelo()) {
            continue;
        }
        hasMelo = true;
        String configuration;
        if (!storedStaffConfiguration(type->meloStateJson(), configuration, error)) {
            return false;
        }
        for (const Measure* measure = score->firstMeasure(); measure; measure = measure->nextMeasure()) {
            for (const StaffTypeChange* carrier : changeCarriers(measure, staff->idx())) {
                if (!carrier->staffType() || !storedStaffConfiguration(carrier->staffType()->meloStateJson(), configuration, error)) {
                    return false;
                }
            }
        }
    }
    if (!hasMelo) {
        return true;
    }
    String timeline;
    std::vector<StateEdit> edits;
    std::vector<NoteEdit> notes;
    if (!defaultReferenceTimeline(timeline, error)
        || !prepareCanonicalReferenceStates(score, timeline, edits, error)
        || !prepareNoteEdits(score, edits, notes, error)) {
        return false;
    }
    score->setMetaTag(REFERENCE_TIMELINE_TAG, timeline);
    installCanonicalReferenceStates(score, edits, false);
    for (const NoteEdit& edit : notes) {
        edit.note->setPitch(edit.projection.midiKey, edit.tpc, edit.tpc);
        edit.note->setTuning(edit.projection.centsOffset);
    }
    return true;
}

bool rebuildCanonicalReferenceContexts(Score* score, String& error)
{
    if (!score) {
        return false;
    }
    std::vector<StateEdit> edits;
    std::vector<NoteEdit> notes;
    if (!prepareCanonicalReferenceStates(score, score->masterScore()->metaTag(REFERENCE_TIMELINE_TAG), edits, error)
        || !prepareNoteEdits(score, edits, notes, error)) {
        return false;
    }
    for (const NoteEdit& edit : notes) {
        if (edit.note->meloNPer() != edit.projection.nPer || edit.note->meloNGen() != edit.projection.nGen) {
            error = mtrc("engraving",
                         "A stored tied note disagrees with the relative reference timeline. Its written pitch cannot be inferred or replaced while loading.");
            return false;
        }
    }
    installCanonicalReferenceStates(score, edits, false);
    // Stored playback fields are normalized by the disclosed, undoable load
    // repair pass. Rebuilding disposable contexts must not hide that repair.
    return true;
}

std::vector<HeaderPitchContext> headerPitchTargets(const Score* score)
{
    std::vector<HeaderPitchContext> targets;
    if (!score) {
        return targets;
    }
    for (const Measure* measure = score->firstMeasure(); measure; measure = measure->nextMeasure()) {
        for (staff_idx_t index = 0; index < score->nstaves(); ++index) {
            const StaffLines* lines = measure->staffLines(index);
            const Staff* staff = score->staff(index);
            const StaffType* type = staff->staffType(measure->tick());
            if (!lines || !lines->visible() || !staff->show() || !type || !type->isMelo()) {
                continue;
            }
            for (const auto& painted : lines->meloHeaderPitchTargets()) {
                if (painted.state != type->meloStateJson()) {
                    continue;
                }
                targets.push_back({ index, measure->tick(), painted.periodIndex, painted.state, painted.label,
                                    painted.ink.translated(lines->canvasPos()) });
            }
        }
    }
    return targets;
}

bool resolveHeaderPitch(const Score* score, const HeaderPitchContext& expected, HeaderPitchContext& current)
{
    for (const auto& target : headerPitchTargets(score)) {
        if (target.staffIdx == expected.staffIdx && target.tick == expected.tick && target.periodIndex == expected.periodIndex
            && target.state == expected.state && target.label == expected.label) {
            current = target;
            return true;
        }
    }
    return false;
}

bool findHeaderPitch(const Score* score, const PointF& canvasPoint, HeaderPitchContext& target)
{
    for (const auto& painted : headerPitchTargets(score)) {
        if (headerPitchHit(painted.state, painted.periodIndex, painted.canvasInk, canvasPoint)) {
            target = painted;
            return true;
        }
    }
    return false;
}

bool changeInitialTonicPitch(Score* score, staff_idx_t staffIdx, const Fraction& tick, int periodIndex, const String& pitch,
                             const String& expectedState, const String& expectedTimeline, String& error)
{
    String current;
    if (!score || !effectiveState(score, staffIdx, score->tick2measure(tick), tick, current)
        || current != expectedState || score->masterScore()->metaTag(REFERENCE_TIMELINE_TAG) != expectedTimeline) {
        error = mtrc("engraving", "The score changed while this pitch editor was open. Reopen the editor and try again.");
        return false;
    }
    String timeline;
    return editHeaderPitch(current, u"staff-header-tonic-pitch", periodIndex, pitch, timeline, error)
           && commitCanonicalTimeline(score, timeline, expectedTimeline, TranslatableString("engraving", "Change initial tonic pitch"),
                                      error);
}

bool changeRelativeKey(Score* score, staff_idx_t staffIdx, const Fraction& tick, const String& interval,
                       const String& expectedTimeline, String& error)
{
    String current;
    if (!score || !effectiveState(score, staffIdx, score->tick2measure(tick), tick, current)) {
        error = mu::engraving::melo::selectMeloPosition();
        return false;
    }
    String timeline;
    return editRelativeKey(current, exactTimeJson(tick), interval, timeline, error)
           && commitCanonicalTimeline(score, timeline, expectedTimeline, TranslatableString("engraving", "Change relative key interval"),
                                      error);
}

bool prepareRelativeKeyEditor(Score* score, staff_idx_t staffIdx, const Fraction& tick,
                              const String* expression, RelativeKeyEditor& result, String& error)
{
    Measure* measure = score ? score->tick2measure(tick) : nullptr;
    String destination;
    if (tick <= Fraction(0, 1) || !score || staffIdx >= score->nstaves()
        || !canInsertChange(score, staffIdx, measure, tick, error)
        || !effectiveState(score, staffIdx, measure, tick, destination)) {
        if (error.isEmpty()) {
            error = mtrc("engraving",
                         "Select a later musical position for a key change. Edit the staff-header pitch to revise the initial key.");
        }
        return false;
    }
    const StaffType* source = score->staff(staffIdx)->staffType(Fraction(0, 1));
    for (const Measure* candidate = score->firstMeasure(); candidate && candidate->tick() <= tick; candidate = candidate->nextMeasure()) {
        for (const StaffTypeChange* carrier : changeCarriers(candidate, staffIdx)) {
            if (carrier->tick() < tick) {
                source = carrier->staffType();
            }
        }
    }
    if (!source || !source->isMelo()) {
        error = mu::engraving::melo::precedingCanonicalStateUnavailable();
        return false;
    }
    return relativeKeyEditor(source->meloStateJson(), destination, exactTimeJson(tick), expression, result, error);
}

bool validateSharedStateTimeline(const Score* score, String& error)
{
    if (!score) {
        return false;
    }
    std::set<Fraction> ticks { Fraction(0, 1) };
    for (const Measure* measure = score->firstMeasure(); measure; measure = measure->nextMeasure()) {
        for (const EngravingItem* item : measure->el()) {
            if (item->isStaffTypeChange()) {
                ticks.insert(item->tick());
            }
        }
    }
    // Compare distinct effective musical configurations, not staff counts.
    // A voice and a two-staff accompaniment can share the same configuration;
    // repeating that configuration on another staff adds no musical difference.
    for (const Fraction& tick : ticks) {
        std::set<String> reference;
        int firstPart = -1;
        for (size_t partIndex = 0; partIndex < score->parts().size(); ++partIndex) {
            std::set<String> shared;
            const Part* part = score->parts()[partIndex];
            for (const Staff* staff : part->staves()) {
                const StaffType* type = staff->staffType(tick);
                if (!type || !type->isMelo()) {
                    continue;
                }
                String projection;
                const bool canonical = !score->masterScore()->metaTag(REFERENCE_TIMELINE_TAG).isEmpty();
                if (!canonical) {
                    error = mu::engraving::melo::canonicalReferenceRequired();
                    return false;
                }
                if (!musicxmlConfigurationV5Xml(type->meloStateJson(), 0, true, projection, error)) {
                    return false;
                }
                shared.insert(projection);
            }
            if (shared.empty()) {
                continue;
            }
            if (firstPart < 0) {
                firstPart = int(partIndex);
                reference = shared;
            } else if (shared != reference) {
                error = exportTimelinesDiffer().arg(firstPart + 1).arg(int(partIndex) + 1);
                return false;
            }
        }
    }
    return true;
}

bool validateLatticeContent(const Score* score, String& error)
{
    if (!score) {
        return false;
    }
    const String root = score->masterScore()->metaTag(REFERENCE_TIMELINE_TAG);
    if (root.isEmpty()) {
        for (const Staff* staff : score->staves()) {
            if (staff->staffType(Fraction(0, 1))->isMelo()) {
                error = mu::engraving::melo::canonicalReferenceRequired();
                return false;
            }
            for (const Measure* measure = score->firstMeasure(); measure; measure = measure->nextMeasure()) {
                if (!changeCarriers(measure, staff->idx()).empty()) {
                    error = mu::engraving::melo::canonicalChangeReferenceRequired();
                    return false;
                }
            }
        }
    } else {
        // Reconstruct from the owning root and authored configurations, then
        // check the disposable contexts without repairing or mutating them.
        std::vector<StateEdit> expected;
        if (!prepareCanonicalReferenceStates(score->masterScore(), root, expected, error)) {
            return false;
        }
        for (const auto& edit : expected) {
            const StaffType* type = edit.staff->staffType(edit.tick);
            if (!type || !validateStaffContext(type->meloStateJson(), edit.state, error)) {
                return false;
            }
        }
        for (const Score* related : score->masterScore()->scoreList()) {
            for (const Measure* measure = related->firstMeasure(); measure; measure = measure->nextMeasure()) {
                for (const Staff* staff : related->staves()) {
                    for (const StaffTypeChange* carrier : changeCarriers(measure, staff->idx())) {
                        if (!carrier->staffType() || !carrier->staffType()->isMelo()) {
                            continue;
                        }
                        const auto found = std::find_if(expected.begin(), expected.end(), [&](const auto& edit) {
                            return edit.staff == staff && edit.tick == carrier->tick();
                        });
                        if (found == expected.end()) {
                            error = mtrc("engraving", "A derived staff change has no owning reference or configuration event.");
                            return false;
                        }
                    }
                }
            }
        }
    }
    std::vector<NoteEdit> projected;
    return validateReferences(score, {}, error) && prepareStoredProjections(score, projected, error);
}

bool prepareLinkedNoteValue(NoteVal& value, const Chord* chord)
{
    if (!chord || !Note::prepareNval(value, chord->staff(), chord->tick())) {
        return false;
    }
    const StaffType* type = chord->staff() ? chord->staff()->staffType(chord->tick()) : nullptr;
    SoundingPitch expected;
    const bool lattice = type && type->isMelo();
    if (lattice && !noteSoundingPitch(type->meloStateJson(), value.meloNPer, value.meloNGen, expected)) {
        MScore::setError(MsError::CANNOT_RESOLVE_LATTICE_NOTE);
        return false;
    }
    for (EngravingObject* object : chord->linkList()) {
        const Chord* linked = toChord(object);
        const StaffType* linkedType = linked->staff() ? linked->staff()->staffType(linked->tick()) : nullptr;
        NoteVal projection = value;
        SoundingPitch sounding;
        if (!Note::prepareNval(projection, linked->staff(), linked->tick())
            || (lattice != bool(linkedType && linkedType->isMelo()))
            || (lattice && (!noteSoundingPitch(linkedType->meloStateJson(), projection.meloNPer, projection.meloNGen, sounding)
                            || !sameProjection(expected, sounding)))) {
            MScore::setError(MsError::CANNOT_RESOLVE_LATTICE_NOTE);
            return false;
        }
    }
    return true;
}

bool validateTieEndpoints(const Note* source, const Note* destination)
{
    if (!source || !destination || !source->hasMeloPitch() || !destination->hasMeloPitch()) {
        return false;
    }
    const StaffType* a = source->staff() ? source->staff()->staffTypeForElement(source) : nullptr;
    const StaffType* b = destination->staff() ? destination->staff()->staffTypeForElement(destination) : nullptr;
    SoundingPitch from, to;
    return a && b && a->isMelo() && b->isMelo()
           && noteSoundingPitch(a->meloStateJson(), source->meloNPer(), source->meloNGen(), from)
           && noteSoundingPitch(b->meloStateJson(), destination->meloNPer(), destination->meloNGen(), to)
           && std::abs(from.frequencyHz - to.frequencyHz) < 1e-9;
}

Note* continuationNote(const Note* source, Chord* destination)
{
    if (!source || !source->hasMeloPitch() || !destination) {
        return nullptr;
    }
    const StaffType* a = source->staff() ? source->staff()->staffTypeForElement(source) : nullptr;
    const StaffType* b = destination->staff() ? destination->staff()->staffType(destination->tick()) : nullptr;
    SoundingPitch from, target;
    if (!a || !b || !a->isMelo() || !b->isMelo()
        || !noteSoundingPitch(a->meloStateJson(), source->meloNPer(), source->meloNGen(), from)) {
        return nullptr;
    }
    if (!noteSoundingPitch(b->meloStateJson(), source->meloNPer(), source->meloNGen(), target)
        || std::abs(from.frequencyHz - target.frequencyHz) >= 1e-9) {
        if (!noteContinuation(b->meloStateJson(), from.frequencyHz, target)) {
            return nullptr;
        }
    }
    // Ordinals distinguish repeated occurrences of the SAME position only.
    int ordinal = 0;
    for (Note* note : source->chord()->notes()) {
        if (note == source) {
            break;
        }
        if (note->hasMeloPitch() && note->meloNPer() == source->meloNPer() && note->meloNGen() == source->meloNGen()) {
            ++ordinal;
        }
    }
    for (Note* note : destination->notes()) {
        if (note->hasMeloPitch() && note->meloNPer() == target.nPer && note->meloNGen() == target.nGen && ordinal-- == 0) {
            return note;
        }
    }
    return nullptr;
}

bool prepareContinuationValue(const NoteVal& source, const Staff* staff, const Fraction& sourceTick,
                              const Fraction& targetTick, NoteVal& result)
{
    result = source;
    const StaffType* from = staff ? staff->staffType(sourceTick) : nullptr;
    const StaffType* to = staff ? staff->staffType(targetTick) : nullptr;
    if (!from || !to || (!from->isMelo() && !to->isMelo()) || source.isRest()) {
        return Note::prepareNval(result, staff, targetTick);
    }
    SoundingPitch established, continuation;
    if (!from->isMelo() || !to->isMelo() || !source.hasMeloPitch
        || !noteSoundingPitch(from->meloStateJson(), source.meloNPer, source.meloNGen, established)) {
        MScore::setError(MsError::CANNOT_RESOLVE_LATTICE_NOTE);
        return false;
    }
    if (!noteSoundingPitch(to->meloStateJson(), source.meloNPer, source.meloNGen, continuation)
        || std::abs(continuation.frequencyHz - established.frequencyHz) >= 1e-9) {
        if (!noteContinuation(to->meloStateJson(), established.frequencyHz, continuation)) {
            MScore::setError(MsError::CANNOT_RESOLVE_LATTICE_NOTE);
            return false;
        }
    }
    result.meloNPer = continuation.nPer;
    result.meloNGen = continuation.nGen;
    return Note::prepareNval(result, staff, targetTick);
}

bool preparePitchEdit(Note* anchor, int nPer, int nGen, std::vector<NoteEdit>& edits, String& error)
{
    const StaffType* anchorType = anchor && anchor->staff() ? anchor->staff()->staffTypeForElement(anchor) : nullptr;
    SoundingPitch requested;
    if (!anchorType || !anchorType->isMelo() || !anchor->hasMeloPitch()
        || !noteSoundingPitch(anchorType->meloStateJson(), nPer, nGen, requested, &error)) {
        return false;
    }
    std::set<Note*> affected;
    std::vector<Note*> pending { anchor };
    for (size_t i = 0; i < pending.size(); ++i) {
        Note* note = pending[i];
        if (!affected.insert(note).second) {
            continue;
        }
        if (note->incomingPartialTie() || note->outgoingPartialTie()) {
            error = partialTieCrossesState();
            return false;
        }
        for (Note* tied : note->tiedNotes()) {
            if (!affected.count(tied)) {
                pending.push_back(tied);
            }
        }
        for (EngravingObject* linked : note->linkList()) {
            if (!affected.count(toNote(linked))) {
                pending.push_back(toNote(linked));
            }
        }
    }
    std::vector<NoteEdit> prepared;
    std::set<Note*> linkedSeen;
    for (Note* note : affected) {
        if (linkedSeen.count(note)) {
            continue;
        }
        SoundingPitch projection;
        const StaffType* type = note->staff() ? note->staff()->staffTypeForElement(note) : nullptr;
        SoundingPitch existing;
        if (!type || !type->isMelo() || !note->hasMeloPitch()
            || !noteSoundingPitch(type->meloStateJson(), note->meloNPer(), note->meloNGen(), existing, &error)) {
            error = linkedNoteMissingState();
            return false;
        }
        // Retain the requested exact position whenever it sustains the target
        // sound. Different settings may require the Kernel's continuation.
        if (!noteSoundingPitch(type->meloStateJson(), nPer, nGen, projection, &error)
            || std::abs(projection.frequencyHz - requested.frequencyHz) >= 1e-9) {
            if (!noteContinuation(type->meloStateJson(), requested.frequencyHz, projection, &error)) {
                return false;
            }
        }
        for (EngravingObject* linkedObject : note->linkList()) {
            Note* linked = toNote(linkedObject);
            const StaffType* linkedType = linked->staff() ? linked->staff()->staffTypeForElement(linked) : nullptr;
            SoundingPitch linkedProjection, linkedExisting;
            if (!linkedType || !linkedType->isMelo() || !linked->hasMeloPitch()
                || !noteSoundingPitch(linkedType->meloStateJson(), linked->meloNPer(), linked->meloNGen(), linkedExisting, &error)
                || !noteSoundingPitch(linkedType->meloStateJson(), projection.nPer, projection.nGen, linkedProjection, &error)
                || !sameProjection(projection, linkedProjection)) {
                error = conflictingLinkedProjections();
                return false;
            }
            linkedSeen.insert(linked);
        }
        const int step = int(String(u"CDEFGAB").indexOf(Char(projection.step)));
        prepared.push_back({ note, projection, step2tpc(step, AccidentalVal(projection.alter)) });
    }
    edits.insert(edits.end(), prepared.begin(), prepared.end());
    return true;
}

void commitPitchEdits(Score* score, const std::vector<NoteEdit>& edits, bool widenExtent)
{
    commitNoteEdits(score, edits);
    for (const NoteEdit& edit : edits) {
        for (EngravingObject* linked : edit.note->linkList()) {
            Note* note = toNote(linked);
            if (widenExtent) {
                widenExtentForNote(note);
            }
            note->triggerLayout();
        }
    }
}

bool effectiveState(const Score* score, staff_idx_t staffIdx, const Measure* measure,
                    String& stateJson, const StaffType** effective)
{
    return effectiveState(score, staffIdx, measure, measure ? measure->tick() : Fraction(-1, 1), stateJson, effective);
}

bool effectiveState(const Score* score, staff_idx_t staffIdx, const Measure* measure, const Fraction& tick,
                    String& stateJson, const StaffType** effective)
{
    if (!score || !measure || staffIdx >= score->nstaves()) {
        return false;
    }
    const StaffType* st = score->staff(staffIdx)->staffType(tick);
    if (!st || !st->isMelo()) {
        return false;
    }
    stateJson = st->meloStateJson();
    if (effective) {
        *effective = st;
    }
    return true;
}

bool changeOptions(const Score* score, staff_idx_t staffIdx, const Measure* measure,
                   StateChangeOptions& options)
{
    return changeOptions(score, staffIdx, measure, measure ? measure->tick() : Fraction(-1, 1), options);
}

bool changeOptions(const Score* score, staff_idx_t staffIdx, const Measure* measure, const Fraction& tick,
                   StateChangeOptions& options)
{
    String state;
    if (!effectiveState(score, staffIdx, measure, tick, state)) {
        return false;
    }
    return stateChangeOptions(state, options);
}

bool canInsertChange(const Score* score, staff_idx_t staffIdx, const Measure* measure, String& reason)
{
    return canInsertChange(score, staffIdx, measure, measure ? measure->tick() : Fraction(-1, 1), reason);
}

bool canInsertChange(const Score* score, staff_idx_t staffIdx, const Measure* measure, const Fraction& tick, String& reason)
{
    String state;
    if (!effectiveState(score, staffIdx, measure, tick, state)) {
        reason = mu::engraving::melo::notSystemStaff();
        return false;
    }
    if (tick.isZero()) {
        return true;        // the origin measure edits the base staff type
    }
    if (changeCarrierAt(measure, staffIdx, tick)) {
        return true;        // the MeloPresto carrier is updated in place
    }
    if (anyCarrierAt(measure, staffIdx, tick)) {
        reason = mu::engraving::melo::positionHasOtherStaffChange();
        return false;
    }
    if (!measure->canAddStaffTypeChange(staffIdx, tick - measure->tick())) {
        reason = mtrc("engraving", "MuseScore refuses a staff type change at this position");
        return false;
    }
    return true;
}

bool applyChange(Score* score, staff_idx_t staffIdx, Measure* measure, const String& choiceId, String& error)
{
    return applyChange(score, staffIdx, measure, measure ? measure->tick() : Fraction(-1, 1), choiceId, error);
}

bool applyChange(Score* score, staff_idx_t staffIdx, Measure* measure, const Fraction& tick,
                 const String& choiceId, String& error)
{
    String reason;
    if (!canInsertChange(score, staffIdx, measure, tick, reason)) {
        error = reason;
        return false;
    }
    return applyChangeToAllMeloParts(score, measure, tick, { choiceId }, error);
}

bool applyChangeToAllMeloParts(Score* score, Measure* measure, const std::vector<String>& choiceIds, String& error)
{
    return applyChangeToAllMeloParts(score, measure, measure ? measure->tick() : Fraction(-1, 1), choiceIds, error);
}

bool applyChangeToAllMeloParts(Score* score, Measure* measure, const Fraction& tick,
                               const std::vector<String>& choiceIds, String& error)
{
    // Prepare every affected configuration, then commit one composition-wide edit.
    if (!score || !measure) {
        error = mtrc("engraving", "no score or measure");
        return false;
    }
    if (choiceIds.empty()) {
        return true;
    }
    if (!score->masterScore()->metaTag(REFERENCE_TIMELINE_TAG).isEmpty()) {
        std::vector<StateEdit> configurations;
        for (Score* related : score->masterScore()->scoreList()) {
            for (Staff* staff : related->staves()) {
                const StaffType* type = staff->staffType(tick);
                const StaffTypeChange* existing = anyCarrierAt(related->tick2measure(tick), staff->idx(), tick);
                if (existing && (!existing->staffType() || !existing->staffType()->isMelo())
                    && staff->staffType(Fraction(0, 1))->isMelo()) {
                    error = mtrc("engraving", "A conventional staff-type change already occupies this musical position.");
                    return false;
                }
                if (!type || !type->isMelo()) {
                    continue;
                }
                if (!canInsertChange(related, staff->idx(), related->tick2measure(tick), tick, error)) {
                    return false;
                }
                String next = type->meloStateJson();
                for (const String& choice : choiceIds) {
                    String changed;
                    if (!applyStateChange(next, choice, changed, error)) {
                        return false;
                    }
                    next = changed;
                }
                if (next == type->meloStateJson()) {
                    continue;
                }
                if (!defaultExtentForEmptyStaffSpan(staff, tick, nextCarrierTick(related, staff->idx(), tick), next, next)) {
                    error = mu::engraving::melo::numberedStaffCentreUnavailable().arg(int(staff->idx()) + 1);
                    return false;
                }
                configurations.push_back({ staff, staff->idx(), tick, Fraction(-1, 1), next });
            }
        }
        return commitCanonicalConfigurations(score, configurations, error);
    }
    error = mtrc("engraving", "The composition needs its canonical spelled initial reference and relative key-change history.");
    return false;
}

bool removeChange(Score* score, staff_idx_t staffIdx, Measure* measure, String& error)
{
    return removeChange(score, staffIdx, measure, measure ? measure->tick() : Fraction(-1, 1), error);
}

bool removeChange(Score* score, staff_idx_t staffIdx, Measure* measure, const Fraction& tick, String& error)
{
    const StaffTypeChange* stc = changeCarrierAt(measure, staffIdx, tick);
    if (!stc) {
        error = mu::engraving::melo::changeUnavailable();
        return false;
    }
    const String timeline = score->masterScore()->metaTag(REFERENCE_TIMELINE_TAG);
    if (!timeline.isEmpty()) {
        if (stc->meloReferenceOnly()) {
            return changeRelativeKey(score, staffIdx, tick, u"{\"nPer\":0,\"nGen\":0}", timeline, error);
        }
        std::vector<StateEdit> configurations;
        for (Score* related : score->masterScore()->scoreList()) {
            for (Staff* target : related->staves()) {
                const auto* carrier = changeCarrierAt(related->tick2measure(tick), target->idx(), tick);
                if (carrier && !carrier->meloReferenceOnly()) {
                    configurations.push_back({ target, target->idx(), tick, Fraction(-1, 1), {} });
                }
            }
        }
        return commitCanonicalConfigurations(score, configurations, error);
    }
    error = mtrc("engraving", "The composition needs its canonical spelled initial reference and relative key-change history.");
    return false;
}

bool normalizeStoredPitchesAfterLoad(Score* score, size_t& repairs, String& error, bool undoable, bool commandOpen)
{
    repairs = 0;
    if (!score) {
        error = mtrc("engraving", "no score to normalize");
        return false;
    }
    std::vector<NoteEdit> projected;
    if (!validateReferences(score, {}, error) || !prepareStoredProjections(score, projected, error)) {
        return false;
    }
    std::vector<NoteEdit> repairsNeeded;
    for (const NoteEdit& edit : projected) {
        if (edit.note->meloNPer() != edit.projection.nPer
            || edit.note->meloNGen() != edit.projection.nGen
            || edit.note->pitch() != edit.projection.midiKey
            || edit.note->tpc1() != edit.tpc || edit.note->tpc2() != edit.tpc
            || std::abs(edit.note->tuning() - edit.projection.centsOffset) >= 1e-9) {
            repairsNeeded.push_back(edit);
        }
    }
    repairs = repairsNeeded.size();
    if (repairsNeeded.empty()) {
        return true;
    }
    if (undoable) {
        if (!commandOpen) {
            score->startCmd(mu::engraving::melo::normalizeStoredPitchesAction());
        }
        commitNoteEdits(score, repairsNeeded);
        if (!commandOpen) {
            score->endCmd();
        }
    } else {
        for (const NoteEdit& edit : repairsNeeded) {
            for (EngravingObject* linkedObject : edit.note->linkList()) {
                Note* linked = toNote(linkedObject);
                // Validated coordinates and extents remain untouched.
                linked->setPitch(edit.projection.midiKey, edit.tpc, edit.tpc);
                linked->setTuning(edit.projection.centsOffset);
            }
        }
    }
    score->setLayoutAll();
    return true;
}
}
