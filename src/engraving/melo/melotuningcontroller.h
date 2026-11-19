/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MeloStaff Milestone 3 — the shared continuous-tuning controller. One
 * semantic seam for every tuning-control surface (panel slider, numeric
 * field, scripted evidence sweeps): it replaces ONLY generator_cents
 * across every state span of every JiMS staff in the score (each base
 * StaffType and every measure-boundary StaffTypeChange), invalidates the
 * derived caches, and relays out — the Kernel re-derives every musical fact.
 *
 * Undo semantics (binding, 2026-08-14): previews are transient and
 * create no undo entries; commit lands exactly one undoable edit;
 * cancel restores the pre-preview state. Never one undo per tick.
 */
#ifndef MU_ENGRAVING_MELOTUNINGCONTROLLER_H
#define MU_ENGRAVING_MELOTUNINGCONTROLLER_H

#include <vector>

#include "../types/types.h"

namespace mu::engraving {
class Score;
class Staff;
}

namespace mu::engraving::melo {
class TuningController
{
public:
    TuningController(Score* score, staff_idx_t staffIdx);

    /// The selected staff's current generator width, read through the Kernel.
    double currentGeneratorCents() const;

    /// Capture the pre-drag score-wide MeloPresto spans. True if the selected staff is MeloPresto.
    bool beginPreview();

    /// Transient per-tick update: every span's generator_cents replaced,
    /// caches invalidated, relayout requested. No undo entries.
    bool preview(double generatorCents);

    /// Restore the captured pre-preview state (no undo entries).
    void cancel();

    /// Land the final value as exactly ONE undoable edit. Restores the
    /// pre-preview state first so the undo stack sees a single change
    /// from the original state to the committed one.
    bool commit(double generatorCents);

    /// Milliseconds spent inside the most recent preview()/commit()
    /// apply (state mutation through layout), for the evidence driver.
    double lastApplyMs() const { return m_lastApplyMs; }

private:
    struct Span {
        Staff* staff = nullptr;
        Fraction tick;
        muse::String stateJson;
    };

    bool collectSpans(std::vector<Span>& spans) const;
    bool applyToSpans(double generatorCents);
    void restoreSpans(const std::vector<Span>& spans);
    void invalidateAndLayout();

    Score* m_score = nullptr;
    staff_idx_t m_staffIdx = 0;
    std::vector<Span> m_prePreview;
    bool m_previewing = false;
    double m_lastApplyMs = 0.0;
};
}

#endif
