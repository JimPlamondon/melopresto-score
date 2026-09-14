/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * JiMStaff Milestone 3 — shared continuous-tuning controller.
 * See melotuningcontroller.h.
 */
#include "engraving/melo/melostrings.h"
#include "melotuningcontroller.h"

#include <algorithm>
#include <chrono>

#include "serialization/json.h"

#include "melobridge.h"
#include "melochangecontroller.h"

#include "../dom/chord.h"
#include "../dom/masterscore.h"
#include "../dom/measure.h"
#include "../dom/note.h"
#include "../dom/score.h"
#include "../dom/segment.h"
#include "../dom/staff.h"
#include "../dom/stafftype.h"
#include "../dom/stafftypechange.h"
#include "../editing/undo.h"

using namespace muse;

namespace mu::engraving::melo {
namespace {
// One undoable edit covering every MeloPresto span in the score: flip() swaps all
// captured state JSONs at once, so undo/redo is a single step.
class MeloChangeStaffStates : public UndoCommand
{
    OBJECT_ALLOCATOR(engraving, MeloChangeStaffStates)

    std::vector<Staff*> m_staves;
    std::vector<Fraction> m_ticks;
    std::vector<String> m_states;

    void flip(EditData*) override
    {
        std::vector<String> previous;
        previous.reserve(m_ticks.size());
        for (size_t i = 0; i < m_ticks.size(); ++i) {
            StaffType* st = m_staves[i]->staffType(m_ticks[i]);
            previous.push_back(st ? st->meloStateJson() : String());
            if (st) {
                st->setMeloStateJson(m_states[i]);
                m_staves[i]->staffTypeListChanged(m_ticks[i]);
                m_staves[i]->score()->setLayoutAll();
            }
        }
        m_states = previous;
        if (!m_staves.empty()) {
            m_staves.front()->score()->setLayoutAll();
        }
    }

public:
    MeloChangeStaffStates(std::vector<Staff*> staves, std::vector<Fraction> ticks, std::vector<String> states)
        : m_staves(std::move(staves)), m_ticks(std::move(ticks)), m_states(std::move(states)) {}

    UNDO_NAME("MeloChangeStaffStates")
    std::vector<EngravingObject*> objectItems() const override
    {
        std::vector<EngravingObject*> objects;
        for (Staff* staff : m_staves) {
            if (std::find(objects.begin(), objects.end(), staff) == objects.end()) {
                objects.push_back(staff);
            }
        }
        return objects;
    }
};
}

TuningController::TuningController(Score* score, staff_idx_t staffIdx)
    : m_score(score), m_staffIdx(staffIdx)
{
}

bool TuningController::collectSpans(std::vector<Span>& spans) const
{
    if (!m_score || m_staffIdx >= m_score->nstaves()) {
        return false;
    }
    Staff* selected = m_score->staff(m_staffIdx);
    const StaffType* selectedBase = selected ? selected->staffType(Fraction(0, 1)) : nullptr;
    if (!selectedBase || !selectedBase->isMelo()) {
        return false;
    }

    for (Score* related : m_score->masterScore()->scoreList()) {
        for (staff_idx_t staffIdx = 0; staffIdx < related->nstaves(); ++staffIdx) {
            Staff* staff = related->staff(staffIdx);
            StaffType* base = staff ? staff->staffType(Fraction(0, 1)) : nullptr;
            if (!base || !base->isMelo()) {
                continue;
            }
            spans.push_back({ staff, Fraction(0, 1), base->meloStateJson() });
            for (MeasureBase* mb = related->first(); mb; mb = mb->next()) {
                if (!mb->isMeasure()) {
                    continue;
                }
                for (EngravingItem* el : mb->el()) {
                    if (el && el->isStaffTypeChange() && el->staffIdx() == staffIdx) {
                        StaffTypeChange* change = toStaffTypeChange(el);
                        if (change->staffType() && change->staffType()->isMelo()) {
                            spans.push_back({ staff, change->tick(), change->staffType()->meloStateJson() });
                        }
                    }
                }
            }
        }
    }
    return true;
}

double TuningController::currentGeneratorCents() const
{
    if (!m_score || m_staffIdx >= m_score->nstaves()) {
        return 0.0;
    }
    const Staff* staff = m_score->staff(m_staffIdx);
    const StaffType* type = staff ? staff->staffType(Fraction(0, 1)) : nullptr;
    if (!type || !type->isMelo()) {
        return 0.0;
    }
    double generatorCents = 0.0, periodCents = 0.0;
    return staffMetrics(type->meloStateJson(), generatorCents, periodCents) ? generatorCents : 0.0;
}

bool TuningController::beginPreview()
{
    m_prePreview.clear();
    if (!collectSpans(m_prePreview)) {
        return false;
    }
    m_previewing = true;
    return true;
}

bool TuningController::applyToSpans(double generatorCents)
{
    if (!m_score) {
        return false;
    }
    std::vector<Span> before;
    if (!collectSpans(before)) {
        return false;
    }
    for (const Span& span : before) {
        StaffType* type = span.staff ? span.staff->staffType(span.tick) : nullptr;
        if (!type || !type->isMelo()) {
            restoreSpans(before);
            return false;
        }
        String updated;
        if (!retuneGenerator(type->meloStateJson(), generatorCents, updated)) {
            restoreSpans(before);
            return false;
        }
        type->setMeloStateJson(updated);
    }
    size_t repairs = 0;
    String error;
    if (!normalizeStoredPitchesAfterLoad(m_score->masterScore(), repairs, error, false)) {
        restoreSpans(before);
        return false;
    }
    invalidateAndLayout();
    return true;
}

void TuningController::invalidateAndLayout()
{
    // Every cached musical fact re-derives from the Kernel: note cents
    // caches reset (the lattice identities are the durable facts), and
    // the frame cache re-keys by construction because the state string
    // changed. Layout then reprojects through the single seam.
    for (Score* related : m_score->masterScore()->scoreList()) {
        for (staff_idx_t staffIdx = 0; staffIdx < related->nstaves(); ++staffIdx) {
            const Staff* staff = related->staff(staffIdx);
            const StaffType* base = staff ? staff->staffType(Fraction(0, 1)) : nullptr;
            if (!base || !base->isMelo()) {
                continue;
            }
            for (Segment* seg = related->firstSegment(SegmentType::ChordRest); seg;
                 seg = seg->next1(SegmentType::ChordRest)) {
                for (track_idx_t track = staffIdx * VOICES; track < (staffIdx + 1) * VOICES; ++track) {
                    EngravingItem* el = seg->element(track);
                    if (el && el->isChord()) {
                        for (Note* note : toChord(el)->notes()) {
                            if (note->hasMeloPitch()) {
                                note->setMeloPitch(note->meloNPer(), note->meloNGen());
                            }
                        }
                    }
                }
            }
        }
        related->setLayoutAll();
        related->doLayout();
        // Milestone 7 (playback): a live preview (and its cancel) edits the
        // staff states outside an undoable command, so nothing tells the
        // playback model that this staff's notes now sound differently. Send
        // the score's EXISTING change signal for this staff's whole tick range
        // — the same channel endCmd uses — so the next rebuild re-derives every
        // JiMS note's sounding pitch from the current state. Commit already
        // announces itself through endCmd.
        if (m_previewing) {
            ScoreChanges changes;
            changes.tickFrom = 0;
            const Measure* last = related->lastMeasure();
            changes.tickTo = last ? last->endTick().ticks() : 0;
            changes.staffIdxFrom = 0;
            changes.staffIdxTo = related->nstaves() ? related->nstaves() - 1 : 0;
            changes.changedTypes.insert(ElementType::STAFFTYPE_CHANGE);
            related->changesChannel().send(changes);
        }
    }
}

bool TuningController::preview(double generatorCents)
{
    if (!m_previewing) {
        return false;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = applyToSpans(generatorCents);
    m_lastApplyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return ok;
}

void TuningController::restoreSpans(const std::vector<Span>& spans)
{
    if (!m_score || spans.empty()) {
        return;
    }
    for (const Span& span : spans) {
        StaffType* type = span.staff ? span.staff->staffType(span.tick) : nullptr;
        if (type) {
            type->setMeloStateJson(span.stateJson);
        }
    }
    size_t repairs = 0;
    String error;
    normalizeStoredPitchesAfterLoad(m_score->masterScore(), repairs, error, false);
    invalidateAndLayout();
}

void TuningController::cancel()
{
    if (!m_previewing) {
        return;
    }
    restoreSpans(m_prePreview);
    m_previewing = false;
    m_prePreview.clear();
}

bool TuningController::commit(double generatorCents)
{
    if (!m_previewing) {
        return false;
    }
    // Restore the pre-preview state silently, then land the final value
    // as exactly one undoable edit from the ORIGINAL state.
    std::vector<Span> original = m_prePreview;
    restoreSpans(original);
    m_previewing = false;
    m_prePreview.clear();

    std::vector<Staff*> staves;
    std::vector<Fraction> ticks;
    std::vector<String> states;
    for (const Span& span : original) {
        staves.push_back(span.staff);
        ticks.push_back(span.tick);
        String updated;
        if (!retuneGenerator(span.stateJson, generatorCents, updated)) {
            return false;
        }
        states.push_back(updated);
    }
    // Preflight the complete target projection before opening the undo
    // transaction, then restore the original preview baseline.
    if (!applyToSpans(generatorCents)) {
        return false;
    }
    restoreSpans(original);
    const auto t0 = std::chrono::steady_clock::now();
    Score* composition = m_score->masterScore();
    composition->startCmd(mu::engraving::melo::changeTuningAction());
    composition->undo(new MeloChangeStaffStates(std::move(staves), std::move(ticks), std::move(states)));
    size_t repairs = 0;
    String error;
    if (!normalizeStoredPitchesAfterLoad(composition, repairs, error, true, true)) {
        composition->endCmd();
        composition->undoRedo(true, nullptr);
        return false;
    }
    composition->endCmd();
    invalidateAndLayout();
    m_lastApplyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return true;
}
}
