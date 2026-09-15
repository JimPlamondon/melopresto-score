/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * JiMStaff Milestone 5 — change-indicator seam. See melochange.h.
 */
#include "melochange.h"
#include "melopitchlabel.h"
#include "draw/fontmetrics.h"

#include <map>

#include <algorithm>
#include <cmath>
#include <limits>

#include "dom/measure.h"
#include "dom/segment.h"
#include "dom/note.h"
#include "dom/part.h"
#include "dom/instrument.h"
#include "dom/chord.h"
#include "dom/score.h"
#include "dom/staff.h"
#include "dom/stafftype.h"
#include "dom/stafftypechange.h"
#include "dom/system.h"
#include "editing/undo.h"
#include "serialization/json.h"

using namespace muse;

namespace mu::engraving::melo {
namespace {
class ChangeMeloExtent : public UndoCommand
{
    OBJECT_ALLOCATOR(engraving, ChangeMeloExtent)

    Staff* m_staff = nullptr;
    Fraction m_tick;
    String m_state;
    bool m_emptyDefault = false;
    bool m_referenceOnly = false;

    void flip(EditData*) override
    {
        StaffType* st = m_staff ? m_staff->staffType(m_tick) : nullptr;
        if (!st) {
            return;
        }
        String previous = st->meloStateJson();
        const bool previousEmptyDefault = st->meloExtentIsEmptyDefault();
        auto* carrier = const_cast<StaffTypeChange*>(changeCarrierAt(m_staff->score()->tick2measure(m_tick), m_staff->idx(), m_tick));
        if (carrier) {
            const bool previousReferenceOnly = carrier->meloReferenceOnly();
            carrier->setMeloReferenceOnly(m_referenceOnly);
            m_referenceOnly = previousReferenceOnly;
        }
        st->setMeloStateJson(m_state);
        st->setMeloExtentIsEmptyDefault(m_emptyDefault);
        m_state = previous;
        m_emptyDefault = previousEmptyDefault;
        m_staff->staffTypeListChanged(m_tick);
        m_staff->score()->setLayoutAll();
    }

public:
    ChangeMeloExtent(Staff* staff, const Fraction& tick, String state, bool emptyDefault = false)
        : m_staff(staff), m_tick(Fraction::fromTicks(std::max(0, staff->staffTypeRange(tick).first))),
        m_state(std::move(state)), m_emptyDefault(emptyDefault) {}
    UNDO_NAME("ChangeMeloExtent")
    UNDO_CHANGED_OBJECTS({ m_staff })
};
}

const StaffTypeChange* changeCarrier(const Measure* measure, staff_idx_t staffIdx)
{
    return measure ? changeCarrierAt(measure, staffIdx, measure->tick()) : nullptr;
}

const StaffTypeChange* changeCarrierAt(const Measure* measure, staff_idx_t staffIdx, const Fraction& tick)
{
    if (!measure) {
        return nullptr;
    }
    for (const EngravingItem* el : measure->el()) {
        if (el && el->isStaffTypeChange() && el->staffIdx() == staffIdx) {
            const StaffTypeChange* stc = toStaffTypeChange(el);
            if (stc->tick() == tick && stc->staffType() && stc->staffType()->isMelo()) {
                return stc;
            }
        }
    }
    return nullptr;
}

std::vector<const StaffTypeChange*> changeCarriers(const Measure* measure, staff_idx_t staffIdx)
{
    std::vector<const StaffTypeChange*> result;
    if (!measure) {
        return result;
    }
    for (const EngravingItem* el : measure->el()) {
        if (!el || !el->isStaffTypeChange() || el->staffIdx() != staffIdx) {
            continue;
        }
        const StaffTypeChange* stc = toStaffTypeChange(el);
        if (stc->staffType() && stc->staffType()->isMelo()) {
            result.push_back(stc);
        }
    }
    std::sort(result.begin(), result.end(), [](const StaffTypeChange* a, const StaffTypeChange* b) {
        return a->tick() < b->tick();
    });
    return result;
}

static bool indicatorForCarrier(const StaffTypeChange* stc, ChangeIndicator& out, const StaffType** newStaffType)
{
    if (!stc || !stc->measure() || !stc->staff()) {
        return false;
    }
    const Staff* staff = stc->staff();
    const StaffType* newSt = staff->staffType(stc->tick());
    const Fraction before = Fraction::fromTicks(std::max(0, stc->tick().ticks() - 1));
    const StaffType* oldSt = staff->staffType(before);
    if (!newSt || !oldSt || !newSt->isMelo() || !oldSt->isMelo() || newSt == oldSt) {
        return false;
    }
    if (!changeIndicator(oldSt->meloStateJson(), newSt->meloStateJson(), out)) {
        return false;
    }
    if (newStaffType) {
        *newStaffType = newSt;
    }
    return !out.empty();
}

bool midSystemChangeIndicator(const Measure* measure, staff_idx_t staffIdx,
                              ChangeIndicator& out, const StaffType** newStaffType)
{
    const StaffTypeChange* stc = changeCarrier(measure, staffIdx);
    if (!stc) {
        return false;
    }
    // System head: the full header already shows the new state.
    if (measure->system() && measure->system()->firstMeasure() == measure) {
        return false;
    }
    return indicatorForCarrier(stc, out, newStaffType);
}

bool midBarChangeIndicator(const StaffTypeChange* carrier, ChangeIndicator& out, const StaffType** newStaffType)
{
    if (!carrier || carrier->rtick().isZero()) {
        return false;
    }
    return indicatorForCarrier(carrier, out, newStaffType);
}

StaffType::MeloHeaderGeometry changeTerrainGeometry(const StaffType* staffType, double spatium,
                                                    double defaultSpatium, const ChangeIndicator& model)
{
    auto geometry = staffType->meloHeaderGeometry(spatium, defaultSpatium);
    // The terrain can merge classes which are not together in a scale-dot
    // row. Measure the actual merged label, including its spelled pitch.
    muse::draw::Font labelFont(u"Edwin", muse::draw::Font::Type::Text);
    labelFont.setPointSizeF(9.0 * spatium / defaultSpatium);
    const auto font = staffType->score() ? staffType->score()->engravingFont() : nullptr;
    TonicPitchLabel tonic;
    const bool haveTonic = tonicPitchLabel(staffType->meloStateJson(), tonic);
    const double oldBand = geometry.changeLabelBand;
    auto measureLabel = [&](String text, bool hasTonic) {
        if (hasTonic && haveTonic) {
            text = tonic.label + u": " + text;
        }
        const auto layout = pitchLabelLayout(text, labelFont, font);
        geometry.changeLabelBand = std::max(geometry.changeLabelBand, layout.bounds.width() + 0.25 * spatium);
    };
    for (const ChangeStack& stack : model.dotStacks) {
        String text;
        bool hasTonic = false;
        for (const ChangePoint& member : stack.members) {
            if (!text.isEmpty()) {
                text += u" ";
            }
            text += member.label;
            hasTonic = hasTonic || (haveTonic && member.nGen == tonic.nGen);
        }
        measureLabel(text, hasTonic);
    }
    for (const ChangePoint& point : model.tonicIndicators) {
        measureLabel(point.label, haveTonic && point.nGen == tonic.nGen);
    }
    geometry.changeTerrainWidth += geometry.changeLabelBand - oldBand;
    if (!model.arrows.empty()) {
        // Owner rule 2026-09-12 (2a), from the corpus census (mode-only is the
        // rarest kind in every corpus, key-only the commonest): mode arrows
        // take a lane LEFT of the dots, between the dots and their labels;
        // key arrows take the lane RIGHT of the dots. The two kinds never
        // share a side, so a combined change can never overlap its arrows.
        double lane = geometry.changeArrowLane;
        if (model.arrows.size() > 1) {
            const double dist = staffType->lineDistance().val() * spatium;
            ConnectorGlyph head;
            if (connectorGlyph(head)) {
                lane = std::max(lane, 2.0 * head.headHalfWidthCents
                                / StaffType::MELO_CENTS_PER_LINE_DISTANCE * dist + 0.5 * dist);
            }
        }
        size_t modeArrows = 0;
        size_t keyArrows = 0;
        for (const ChangeArrow& arrow : model.arrows) {
            (arrow.kind == u"mode" ? modeArrows : keyArrows)++;
        }
        const double left = lane * double(modeArrows);
        const double right = lane * double(keyArrows);
        geometry.changeTerrainWidth += left + right - geometry.changeArrowLane;
        geometry.changeLeftArrowLane = left;
        geometry.changeArrowLane = right;
    }
    return geometry;
}

double changeTerrainWidth(const Measure* measure)
{
    if (!measure || !measure->score()) {
        return 0.0;
    }
    double width = 0.0;
    const Score* score = measure->score();
    for (staff_idx_t s = 0; s < score->nstaves(); ++s) {
        ChangeIndicator model;
        const StaffType* st = nullptr;
        if (midSystemChangeIndicator(measure, s, model, &st) && st) {
            const double sp = score->style().spatium();
            width = std::max(width, changeTerrainGeometry(st, sp, score->style().defaultSpatium(), model).changeTerrainWidth);
        }
    }
    return width;
}

double changeTerrainWidthAt(const Measure* measure, const Fraction& tick)
{
    if (!measure || !measure->score()) {
        return 0.0;
    }
    double width = 0.0;
    const Score* score = measure->score();
    for (staff_idx_t s = 0; s < score->nstaves(); ++s) {
        const StaffTypeChange* carrier = changeCarrierAt(measure, s, tick);
        ChangeIndicator model;
        const StaffType* st = nullptr;
        if (midBarChangeIndicator(carrier, model, &st) && st) {
            const double sp = score->style().spatium();
            width = std::max(width, changeTerrainGeometry(st, sp, score->style().defaultSpatium(), model).changeTerrainWidth);
        }
    }
    return width;
}

bool courtesyChangeIndicator(const Measure* measure, staff_idx_t staffIdx,
                             ChangeIndicator& out, const StaffType** stateStaffType)
{
    if (!measure || !measure->system() || measure->system()->lastMeasure() != measure) {
        return false;
    }
    const Measure* next = measure->nextMeasure();
    if (!next || !changeCarrier(next, staffIdx)) {
        return false;
    }
    const Staff* staff = measure->score()->staff(staffIdx);
    if (!staff) {
        return false;
    }
    const StaffType* oldSt = staff->staffType(measure->tick());
    const StaffType* newSt = staff->staffType(next->tick());
    if (!oldSt || !newSt || !oldSt->isMelo() || !newSt->isMelo() || oldSt == newSt) {
        return false;
    }
    if (!changeIndicator(oldSt->meloStateJson(), newSt->meloStateJson(), out)) {
        return false;
    }
    if (stateStaffType) {
        *stateStaffType = oldSt;   // the courtesy terrain sits on the OLD staff (this system)
    }
    return !out.empty();
}

double courtesyTerrainWidth(const Measure* measure)
{
    if (!measure || !measure->score()) {
        return 0.0;
    }
    double width = 0.0;
    const Score* score = measure->score();
    for (staff_idx_t s = 0; s < score->nstaves(); ++s) {
        ChangeIndicator model;
        const StaffType* st = nullptr;
        if (courtesyChangeIndicator(measure, s, model, &st) && st) {
            st = score->staff(s)->staffType(measure->nextMeasure()->tick());
            const double sp = score->style().spatium();
            width = std::max(width, changeTerrainGeometry(st, sp, score->style().defaultSpatium(), model).changeTerrainWidth);
        }
    }
    return width;
}

double changeAnchorPeriodCents(const StaffType::MeloFrameView& view, const ChangeIndicator& model, double periodCents,
                               double doCentsAboveExtentLower, const std::vector<double>& noteCents)
{
    const double eps = 1e-6;
    const double fallback = doCentsAboveExtentLower
                            + std::floor((view.bottomCents() - doCentsAboveExtentLower) / periodCents + eps) * periodCents;
    if (view.empty() || periodCents <= 0.0) {
        return fallback;
    }
    // The points that move with the anchor: tonic indicators and arrow ends,
    // as (periodOffset + ordinate) in periods.
    std::vector<double> offsets;
    for (const ChangePoint& p : model.tonicIndicators) {
        offsets.push_back(p.periodOffset + p.ordinate);
    }
    for (const ChangeArrow& a : model.arrows) {
        offsets.push_back(a.from.periodOffset + a.from.ordinate);
        offsets.push_back(a.to.periodOffset + a.to.ordinate);
    }
    if (offsets.empty()) {
        return fallback;
    }
    // Period zero need not itself be visible: an arrow may start on Do in
    // period one. Include anchors whose translated endpoints can fit, or
    // extending a short frame can make the chosen anchor jump an octave.
    std::vector<double> candidates;
    const auto offsetRange = std::minmax_element(offsets.begin(), offsets.end());
    for (const StaffType::MeloFrameBand& band : view.bands) {
        for (const StaffType::MeloSegment& seg : band.segments) {
            const double first = doCentsAboveExtentLower
                                 + std::floor((seg.lowerCents - doCentsAboveExtentLower) / periodCents
                                              - *offsetRange.second) * periodCents;
            const double last = seg.upperCents - *offsetRange.first * periodCents;
            for (double b = first; b <= last + eps; b += periodCents) {
                if (candidates.empty() || std::abs(candidates.back() - b) > eps) {
                    candidates.push_back(b);
                }
            }
        }
    }
    std::sort(candidates.begin(), candidates.end());
    if (candidates.empty()) {
        return fallback;
    }
    // Overflow of a point: its distance outside the nearest drawn segment.
    auto overflowOf = [&](double cents) {
        double best = std::numeric_limits<double>::infinity();
        for (const StaffType::MeloFrameBand& band : view.bands) {
            for (const StaffType::MeloSegment& seg : band.segments) {
                const double d = std::max({ 0.0, seg.lowerCents - cents, cents - seg.upperCents });
                best = std::min(best, d);
            }
        }
        return best <= eps ? 0.0 : best;
    };
    // Owner decision 2026-09-14 (S3): least overflow first; among equals the
    // placement whose rows lie nearest the staff's notes on this system
    // (zero gap when they share rows); among equals again, the highest.
    double noteLow = std::numeric_limits<double>::infinity();
    double noteHigh = -std::numeric_limits<double>::infinity();
    for (double cents : noteCents) {
        noteLow = std::min(noteLow, cents);
        noteHigh = std::max(noteHigh, cents);
    }
    auto gapToNotes = [&](double anchor) {
        if (noteCents.empty()) {
            return 0.0;
        }
        const double low = anchor + *offsetRange.first * periodCents;
        const double high = anchor + *offsetRange.second * periodCents;
        return std::max({ 0.0, noteLow - high, low - noteHigh });
    };
    double bestAnchor = candidates.front();
    double bestOverflow = std::numeric_limits<double>::infinity();
    double bestGap = std::numeric_limits<double>::infinity();
    for (double anchor : candidates) {
        double overflow = 0.0;
        for (double off : offsets) {
            overflow += overflowOf(anchor + off * periodCents);
        }
        const double gap = gapToNotes(anchor);
        const bool lessOverflow = overflow < bestOverflow - eps;
        const bool sameOverflow = std::abs(overflow - bestOverflow) <= eps;
        const bool nearerNotes = gap < bestGap - eps;
        const bool sameGap = std::abs(gap - bestGap) <= eps;
        if (lessOverflow || (sameOverflow && (nearerNotes || (sameGap && anchor > bestAnchor)))) {
            bestOverflow = overflow;
            bestGap = gap;
            bestAnchor = anchor;
        }
    }
    return bestAnchor;
}

bool changeIndicatorIntoStaffType(const Score* score, staff_idx_t staffIdx, const StaffType* newStaffType,
                                  ChangeIndicator& out)
{
    if (!score || !newStaffType || !newStaffType->isMelo()) {
        return false;
    }
    const Staff* staff = score->staff(staffIdx);
    if (!staff) {
        return false;
    }
    for (const Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        for (const StaffTypeChange* carrier : changeCarriers(m, staffIdx)) {
            if (staff->staffType(carrier->tick()) != newStaffType) {
                continue;
            }
            const Fraction before = Fraction::fromTicks(std::max(0, carrier->tick().ticks() - 1));
            const StaffType* oldSt = staff->staffType(before);
            if (!oldSt || !oldSt->isMelo() || oldSt == newStaffType) {
                return false;
            }
            return changeIndicator(oldSt->meloStateJson(), newStaffType->meloStateJson(), out);
        }
    }
    return false;
}

bool changeIndicatorsTouchingStaffType(const Score* score, staff_idx_t staffIdx, const StaffType* staffType,
                                       ChangeIndicator& out)
{
    out = {};
    if (!score || !staffType || !staffType->isMelo()) {
        return false;
    }
    const Staff* staff = score->staff(staffIdx);
    if (!staff) {
        return false;
    }
    auto append = [&](const ChangeIndicator& model) {
        for (const String& kind : model.kinds) {
            if (std::find(out.kinds.begin(), out.kinds.end(), kind) == out.kinds.end()) {
                out.kinds.push_back(kind);
            }
        }
        out.dotStacks.insert(out.dotStacks.end(), model.dotStacks.begin(), model.dotStacks.end());
        out.tonicIndicators.insert(out.tonicIndicators.end(), model.tonicIndicators.begin(), model.tonicIndicators.end());
        out.arrows.insert(out.arrows.end(), model.arrows.begin(), model.arrows.end());
    };
    for (const Measure* measure = score->firstMeasure(); measure; measure = measure->nextMeasure()) {
        for (const StaffTypeChange* carrier : changeCarriers(measure, staffIdx)) {
            const StaffType* newStaffType = staff->staffType(carrier->tick());
            const Fraction before = Fraction::fromTicks(std::max(0, carrier->tick().ticks() - 1));
            const StaffType* oldStaffType = staff->staffType(before);
            if (!oldStaffType || !newStaffType || oldStaffType == newStaffType) {
                continue;
            }
            const bool midBar = !carrier->rtick().isZero();
            const bool drawsAgainstStaffType = midBar ? oldStaffType == staffType : newStaffType == staffType;
            if (!drawsAgainstStaffType) {
                continue;
            }
            ChangeIndicator model;
            if (changeIndicator(oldStaffType->meloStateJson(), newStaffType->meloStateJson(), model)
                && !model.empty()) {
                append(model);
            }
        }
    }
    return !out.empty();
}

std::vector<double> systemNoteCents(const System* system, staff_idx_t staffIdx, const StaffType* displayed)
{
    std::vector<double> out;
    if (!system || !displayed || !displayed->isMelo()) {
        return out;
    }
    const Score* score = system->score();
    const Staff* staff = score ? score->staff(staffIdx) : nullptr;
    double displayedDo0 = 0.0;
    if (!staff || !noteCentsAboveExtentLower(displayed->meloStateJson(), 1, -2, displayedDo0)) {
        return out;
    }
    std::map<const StaffType*, double> do0;
    for (const MeasureBase* mb : system->measures()) {
        if (!mb->isMeasure()) {
            continue;
        }
        for (const Segment* seg = toMeasure(mb)->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
            const StaffType* type = staff->staffType(seg->tick());
            if (!type || !type->isMelo()) {
                continue;
            }
            auto origin = do0.find(type);
            if (origin == do0.end()) {
                double d = 0.0;
                if (!noteCentsAboveExtentLower(type->meloStateJson(), 1, -2, d)) {
                    continue;
                }
                origin = do0.emplace(type, d).first;
            }
            for (track_idx_t track = staffIdx * VOICES; track < (staffIdx + 1) * VOICES; ++track) {
                const EngravingItem* el = seg->element(track);
                if (!el || !el->isChord()) {
                    continue;
                }
                for (const Note* note : toChord(el)->notes()) {
                    double cents = 0.0;
                    if (note->hasMeloPitch()
                        && noteCentsAboveExtentLower(type->meloStateJson(), note->meloNPer(), note->meloNGen(), cents)) {
                        out.push_back(cents - origin->second + displayedDo0);
                    }
                }
            }
        }
    }
    return out;
}

std::vector<double> changeIndicatorOverflowCents(const StaffType::MeloFrameView& view, const ChangeIndicator& model,
                                                 double periodCents, double doCentsAboveExtentLower,
                                                 const std::vector<double>& noteCents)
{
    std::vector<double> out;
    if (view.empty() || periodCents <= 0.0) {
        return out;
    }
    const double eps = 1e-6;
    const double anchor = changeAnchorPeriodCents(view, model, periodCents, doCentsAboveExtentLower, noteCents);
    auto inside = [&](double cents) {
        for (const StaffType::MeloFrameBand& band : view.bands) {
            for (const StaffType::MeloSegment& seg : band.segments) {
                if (cents >= seg.lowerCents - eps && cents <= seg.upperCents + eps) {
                    return true;
                }
            }
        }
        return false;
    };
    auto consider = [&](const ChangePoint& p) {
        const double cents = anchor + (p.periodOffset + p.ordinate) * periodCents;
        if (!inside(cents)) {
            out.push_back(cents);
        }
    };
    for (const ChangePoint& p : model.tonicIndicators) {
        consider(p);
    }
    for (const ChangeArrow& a : model.arrows) {
        consider(a.from);
        consider(a.to);
    }
    return out;
}

static bool partHasVocalRole(const Part* part, const String& role)
{
    if (!part) {
        return false;
    }
    const String id = part->instrumentId();
    return id == role || id == u"voice." + role;
}

bool hasCompleteTonicAmbits(const Score* score)
{
    if (!score) {
        return false;
    }
    bool foundMelo = false;
    for (staff_idx_t staffIdx = 0; staffIdx < score->nstaves(); ++staffIdx) {
        const Staff* staff = score->staff(staffIdx);
        const StaffType* base = staff ? staff->staffType(Fraction(0, 1)) : nullptr;
        if (base && base->isMelo()) {
            foundMelo = true;
            if (base->meloTonicAmbit().empty()) {
                return false;
            }
        }
        for (const Measure* measure = score->firstMeasure(); measure; measure = measure->nextMeasure()) {
            for (const StaffTypeChange* carrier : changeCarriers(measure, staffIdx)) {
                const StaffType* type = carrier ? carrier->staffType() : nullptr;
                if (!type || !type->isMelo()) {
                    continue;
                }
                foundMelo = true;
                if (type->meloTonicAmbit().empty()) {
                    return false;
                }
            }
        }
    }
    return foundMelo;
}

int deriveTonicAmbits(Score* score)
{
    if (!score) {
        return 0;
    }
    const String wanted = melodyPartToken(score->meloMelodyPart());
    Staff* melodyStaff = nullptr;
    for (Part* part : score->parts()) {
        if (partHasVocalRole(part, wanted) && !part->staves().empty()) {
            melodyStaff = part->staves().front();
            break;
        }
    }
    if (!melodyStaff) {
        return 0;
    }
    const staff_idx_t melodyStaffIdx = melodyStaff->idx();
    int changed = 0;
    // Tonal carriers are timeline spans, not separate melodies.
    std::vector<Fraction> starts = { Fraction(0, 1) };
    for (const Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        for (const StaffTypeChange* carrier : changeCarriers(m, melodyStaffIdx)) {
            if (carrier->tick() > Fraction(0, 1)) {
                starts.push_back(carrier->tick());
            }
        }
    }
    if (!score->lastMeasure()) {
        return 0;
    }
    String spans = u"[";
    for (size_t i = 0; i < starts.size(); ++i) {
        const StaffType* authority = melodyStaff->staffType(starts[i]);
        if (!authority || !authority->isMelo() || authority->meloStateJson().isEmpty()) {
            return 0;
        }
        const Fraction end = i + 1 < starts.size() ? starts[i + 1] : score->lastMeasure()->endTick();
        if (i > 0) {
            spans += u",";
        }
        spans += String(u"{\"state\":%1,\"duration\":%2,\"melody\":{\"notes\":[")
                 .arg(authority->meloStateJson()).arg((end - starts[i]).ticks());
        bool first = true;
        for (const Segment* seg = score->firstSegment(SegmentType::ChordRest); seg;
             seg = seg->next1(SegmentType::ChordRest)) {
            if (seg->tick() < starts[i]) {
                continue;
            }
            if (seg->tick() >= end) {
                break;
            }
            for (track_idx_t track = melodyStaffIdx * VOICES; track < (melodyStaffIdx + 1) * VOICES; ++track) {
                const EngravingItem* el = seg->element(track);
                if (!el || !el->isChord()) {
                    continue;
                }
                for (const Note* note : toChord(el)->notes()) {
                    if (!note->hasMeloPitch()) {
                        continue;
                    }
                    if (!first) {
                        spans += u",";
                    }
                    first = false;
                    spans += String(u"{\"nPer\":%1,\"nGen\":%2}").arg(note->meloNPer()).arg(note->meloNGen());
                }
            }
        }
        spans += u"]}}";
    }
    spans += u"]";
    String token;
    if (!songwideTonicAmbit(spans, token)) {
        return 0;
    }
    for (size_t i = 0; i < starts.size(); ++i) {
        // The identical Kernel token is repeated through every staff carrier;
        // repetition is transport, never a second authority.
        for (staff_idx_t staffIdx = 0; staffIdx < score->nstaves(); ++staffIdx) {
            Staff* staff = score->staff(staffIdx);
            StaffType* st = staff ? staff->staffType(starts[i]) : nullptr;
            if (!st || !st->isMelo() || token == st->meloTonicAmbit()) {
                continue;
            }
            JsonObject request = JsonDocument::fromJson(st->meloStateJson().toUtf8()).rootObject();
            JsonObject configuration = request.value("configuration").toObject();
            configuration.set("tonic_ambit", token);
            request.set("configuration", configuration);
            String state = String::fromUtf8(JsonDocument(request).toJson(JsonDocument::Format::Compact));
            String error;
            if (!validateState(state, error)) {
                continue;
            }
            if (score->undoStack()->hasActiveCommand()) {
                // A melody edit affects every repeated carrier. Capture those
                // derived fields in the same command so cancel/undo is complete.
                score->undo(new ChangeMeloExtent(staff, starts[i], state, st->meloExtentIsEmptyDefault()));
            } else {
                st->setMeloStateJson(state);
                auto* carrier = const_cast<StaffTypeChange*>(changeCarrierAt(score->tick2measure(starts[i]), staffIdx, starts[i]));
                if (carrier) {
                    carrier->setMeloReferenceOnly(false);
                }
            }
            ++changed;
        }
    }
    return changed;
}

static const char* vocalRole(const Part* part)
{
    if (partHasVocalRole(part, u"soprano")) {
        return "soprano";
    }
    if (partHasVocalRole(part, u"alto")) {
        return "alto";
    }
    if (partHasVocalRole(part, u"tenor")) {
        return "tenor";
    }
    if (partHasVocalRole(part, u"bass")) {
        return "bass";
    }
    return nullptr;
}

bool staffSpanIsEmpty(const Staff* staff, const Fraction& start, const Fraction& stop)
{
    if (!staff || !staff->score()) {
        return false;
    }
    const staff_idx_t staffIdx = staff->idx();
    for (const Segment* seg = staff->score()->firstSegment(SegmentType::ChordRest); seg;
         seg = seg->next1(SegmentType::ChordRest)) {
        if (seg->tick() < start) {
            continue;
        }
        if (!stop.negative() && seg->tick() >= stop) {
            break;
        }
        for (track_idx_t track = staffIdx * VOICES; track < (staffIdx + 1) * VOICES; ++track) {
            const EngravingItem* el = seg->element(track);
            if (!el || !el->isChord()) {
                continue;
            }
            for (const Note* note : toChord(el)->notes()) {
                if (note->hasMeloPitch()) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool defaultExtentForEmptyStaffSpan(const Staff* staff, const Fraction& start,
                                    const Fraction& stop, const String& state,
                                    String& updated)
{
    updated = state;
    if (!staff || !staff->score()) {
        return false;
    }
    if (!staffSpanIsEmpty(staff, start, stop)) {
        return true;
    }
    const char* role = vocalRole(staff->part());
    const Instrument* instrument = staff->part()->instrument();
    if (!instrument) {
        return defaultInstrumentExtent(state, 0, 127, updated);
    }
    if (role) {
        return defaultVocalExtent(state, instrument->minPitchA(), instrument->maxPitchA(), role, updated);
    }
    return defaultInstrumentExtent(state, instrument->minPitchA(), instrument->maxPitchA(), updated);
}

int reconcileExtents(Score* score)
{
    if (!score) {
        return 0;
    }
    int changed = 0;
    for (staff_idx_t staffIdx = 0; staffIdx < score->nstaves(); ++staffIdx) {
        Staff* staff = score->staff(staffIdx);
        if (!staff) {
            continue;
        }
        std::vector<Fraction> starts = { Fraction(0, 1) };
        for (const Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
            for (const StaffTypeChange* carrier : changeCarriers(m, staffIdx)) {
                if (carrier->tick() > Fraction(0, 1)) {
                    starts.push_back(carrier->tick());
                }
            }
        }
        for (size_t i = 0; i < starts.size(); ++i) {
            StaffType* st = staff->staffType(starts[i]);
            if (!st || !st->isMelo()) {
                continue;
            }
            const bool bounded = i + 1 < starts.size();
            const Fraction end = bounded ? starts[i + 1] : Fraction(0, 1);
            String melody = u"{\"notes\":[";
            bool first = true;
            for (const Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
                if (seg->tick() < starts[i]) {
                    continue;
                }
                if (bounded && seg->tick() >= end) {
                    break;
                }
                for (track_idx_t track = staffIdx * VOICES; track < (staffIdx + 1) * VOICES; ++track) {
                    const EngravingItem* el = seg->element(track);
                    if (!el || !el->isChord()) {
                        continue;
                    }
                    for (const Note* note : toChord(el)->notes()) {
                        if (!note->hasMeloPitch()) {
                            continue;
                        }
                        if (!first) {
                            melody += u",";
                        }
                        melody += String(u"{\"nPer\":%1,\"nGen\":%2}").arg(note->meloNPer()).arg(note->meloNGen());
                        first = false;
                    }
                }
            }
            melody += u"]}";
            String updated;
            bool ok = false;
            if (!first) {
                ok = fitExtent(st->meloStateJson(), melody, updated);
            } else {
                ok = defaultExtentForEmptyStaffSpan(staff, starts[i], bounded ? end : Fraction(-1, 1),
                                                    st->meloStateJson(), updated);
            }
            if (ok) {
                st->setMeloExtentIsEmptyDefault(first);
            }
            if (ok && updated != st->meloStateJson()) {
                st->setMeloStateJson(updated);
                // A fitted extent is a reference-free configuration. Preserve
                // it even when this boundary originated as a relative event.
                auto* carrier = const_cast<StaffTypeChange*>(changeCarrierAt(score->tick2measure(starts[i]), staffIdx, starts[i]));
                if (carrier) {
                    carrier->setMeloReferenceOnly(false);
                }
                ++changed;
            }
            if (i > 0) {
                auto* carrier = const_cast<StaffTypeChange*>(changeCarrierAt(score->tick2measure(starts[i]), staffIdx, starts[i]));
                if (carrier && carrier->meloReferenceOnly()) {
                    String previousConfiguration, currentConfiguration, error;
                    if (staffConfiguration(staff->staffType(starts[i - 1])->meloStateJson(), previousConfiguration, error)
                        && staffConfiguration(st->meloStateJson(), currentConfiguration, error)
                        && previousConfiguration != currentConfiguration) {
                        carrier->setMeloReferenceOnly(false);
                    }
                }
            }
        }
    }
    return changed;
}

bool widenExtentForNote(Note* note)
{
    if (!note || !note->staff() || !note->hasMeloPitch()) {
        return false;
    }
    StaffType* st = note->staff()->staffType(note->tick());
    if (!st || !st->isMelo()) {
        return false;
    }
    String updated;
    const bool wasEmpty = st->meloExtentIsEmptyDefault();
    const bool ok = wasEmpty
                    ? fitExtent(st->meloStateJson(),
                                String(u"{\"notes\":[{\"nPer\":%1,\"nGen\":%2}]}").arg(note->meloNPer()).arg(note->meloNGen()), updated)
                    : widenExtent(st->meloStateJson(), note->meloNPer(), note->meloNGen(), updated);
    const bool changed = ok && (wasEmpty || updated != st->meloStateJson());
    if (changed) {
        note->score()->undo(new ChangeMeloExtent(note->staff(), note->tick(), updated));
    }
    designatedMelodyNoteChanged(note);
    return changed;
}

void designatedMelodyNoteChanged(Note* note)
{
    if (!note || !note->score() || !note->part()) {
        return;
    }
    const String wanted = melodyPartToken(note->score()->meloMelodyPart());
    if (partHasVocalRole(note->part(), wanted)) {
        deriveTonicAmbits(note->score());
    }
}
}
