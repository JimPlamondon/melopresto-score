/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * JiMStaff Milestone 5 — the change-indicator seam. Finds the JiMS staff-
 * type change a measure carries for a staff, hands the OLD and NEW state
 * to the Kernel (`change_indicator`), and answers "is there a mid-system
 * indicator to reserve room for and paint here?". No musical fact is
 * computed here: the fork never diffs states, classifies, or chooses.
 */
#ifndef MU_ENGRAVING_MELOCHANGE_H
#define MU_ENGRAVING_MELOCHANGE_H

#include "melobridge.h"
#include "../dom/stafftype.h"
#include "../types/types.h"

#include <vector>

namespace mu::engraving {
class Measure;
class Note;
class Staff;
class StaffType;
class StaffTypeChange;
}

namespace mu::engraving::melo {
/// The MeloPresto StaffTypeChange carried by `measure` for `staffIdx`, if any.
const StaffTypeChange* changeCarrier(const Measure* measure, staff_idx_t staffIdx);

/// The MeloPresto carrier at one exact absolute score tick, if any.
const StaffTypeChange* changeCarrierAt(const Measure* measure, staff_idx_t staffIdx, const Fraction& tick);

/// All MeloPresto carriers in `measure` for `staffIdx`, ordered by exact tick.
std::vector<const StaffTypeChange*> changeCarriers(const Measure* measure, staff_idx_t staffIdx);

/// The Kernel's change-indicator model for the change `measure` carries
/// on `staffIdx`, evaluated between the staff type in effect just before
/// the measure and the one it introduces. Returns false when the measure
/// carries no MeloPresto change, when the change sits at a system head (the
/// full header is the indication there — no enclosure), or when the
/// Kernel model is empty (tuning/extent/presentation-only differences).
bool midSystemChangeIndicator(const Measure* measure, staff_idx_t staffIdx, ChangeIndicator& out, const StaffType** newStaffType = nullptr);

/// The Kernel model for an exact carrier strictly inside its measure.
bool midBarChangeIndicator(const StaffTypeChange* carrier, ChangeIndicator& out, const StaffType** newStaffType = nullptr);

/// Host label/glyph spacing shared by reservation and painting. Each Kernel
/// arrow gets its own lane, wide enough for its head and a visible gap.
StaffType::MeloHeaderGeometry changeTerrainGeometry(const StaffType* staffType, double spatium, double defaultSpatium,
                                                    const ChangeIndicator& model);

/// The terrain width to reserve at the start of `measure` (max over
/// staves that carry a mid-system indicator), or 0.
double changeTerrainWidth(const Measure* measure);

/// Terrain width for carriers at one exact absolute score tick.
double changeTerrainWidthAt(const Measure* measure, const Fraction& tick);

/// Courtesy indicator (owner ruling 2026-08-16, option 1a): when the NEXT
/// measure carries a JiMS change and `measure` is the last of its system,
/// the change is indicated at the END of `measure` — the closing barline
/// serves as the right stroke, one added stroke opens the terrain on the
/// left — because the new system's fresh header alone hides the change.
/// Model between the state in effect through `measure` and the state the
/// next measure introduces. Returns false when not applicable or empty.
bool courtesyChangeIndicator(const Measure* measure, staff_idx_t staffIdx, ChangeIndicator& out,
                             const StaffType** stateStaffType = nullptr);

/// The courtesy terrain width to reserve at the end of `measure`, or 0.
double courtesyTerrainWidth(const Measure* measure);

/// Owner ruling 2026-08-19: the change indicator (tonic indicators and the
/// key/mode arrow ends) is drawn from a Do row of the drawn stave stack that
/// keeps the WHOLE indicator inside the staff; when none does, the Do row
/// that overflows least (rule 7b then extends the staff). Owner decision
/// 2026-09-14 (S3): among the Do rows where everything fits, the one whose
/// indicator rows lie nearest the staff's notes on that system (`noteCents`,
/// frame coordinates); with no notes, or all else equal, the highest.
/// Returns the chosen Do row as cents relative to the frame origin (a
/// multiple of the period); scale-change dot stacks are instantiated per
/// period and do not depend on it. Falls back to the stack's lowest period
/// for an empty view.
double changeAnchorPeriodCents(const StaffType::MeloFrameView& view, const ChangeIndicator& model, double periodCents,
                               double doCentsAboveExtentLower = 0.0, const std::vector<double>& noteCents = {});

/// The MeloPresto notes of `staffIdx` on `system`, as cents in `displayed`'s
/// frame coordinates: every section shares the Do row, so a note of another
/// section is translated by the difference of the two sections' Do0.
std::vector<double> systemNoteCents(const System* system, staff_idx_t staffIdx, const StaffType* displayed);

/// The change indicator drawn against THIS staff type's frame — the one
/// whose NEW state is `newStaffType` (its own section start; mid-system or
/// courtesy alike). False when the staff type starts no MeloPresto section
/// (the base type) or the Kernel derives no indicator.
bool changeIndicatorIntoStaffType(const Score* score, staff_idx_t staffIdx, const StaffType* newStaffType, ChangeIndicator& out);

/// Every whole-score change indicator whose terrain draws against
/// `staffType`: incoming changes use the new staff type, while mid-bar
/// changes use the old displayed staff type. System-local courtesy terrain
/// is added separately. Every tonic and arrow endpoint is retained.
bool changeIndicatorsTouchingStaffType(const Score* score, staff_idx_t staffIdx, const StaffType* staffType, ChangeIndicator& out);

/// Owner rule 2026-08-19 (7b): the cents (frame coordinates) of every
/// indicator point that still falls outside the drawn stave after the
/// best Do-line anchor was chosen — empty when the indicator fits. The
/// frame is then re-derived covering them (Kernel `extra_cents`), so the
/// staff extends to include the whole indicator.
std::vector<double> changeIndicatorOverflowCents(const StaffType::MeloFrameView& view, const ChangeIndicator& model, double periodCents,
                                                 double doCentsAboveExtentLower = 0.0, const std::vector<double>& noteCents = {});

/// Derive one song-wide tonic ambit from the explicitly designated melody
/// part and repeat the Kernel token through every MeloPresto transport carrier.
int deriveTonicAmbits(Score* score);

/// True when every MeloPresto base and change carrier already transports the
/// work's explicit tonic-ambit token.
bool hasCompleteTonicAmbits(const Score* score);

/// Load-time extent reconciliation: exact written-note bounds, or a
/// Kernel-derived centre anchor from the Part's declared range.
int reconcileExtents(Score* score);

/// Whether a span contains no written MeloPresto notes across all its voices.
bool staffSpanIsEmpty(const Staff* staff, const Fraction& start, const Fraction& stop);

/// Install an empty staff's declared range-centre anchor. Written staves
/// retain their extent; changing key never changes the centre-note identity.
bool defaultExtentForEmptyStaffSpan(const Staff* staff, const Fraction& start, const Fraction& stop, const muse::String& state,
                                    muse::String& updated);

/// Edit-time grow-only lifecycle transition for one entered or moved note.
bool widenExtentForNote(Note* note);

/// Recompute the song-wide tonic ambit after a note in the explicitly
/// designated melody Part changes. A note in any other Part is a no-op.
void designatedMelodyNoteChanged(Note* note);
}

#endif
