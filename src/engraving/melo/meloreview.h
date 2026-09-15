/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 Jim Plamondon
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
#pragma once

// The JiMS evidentiary review record (mei-jims profile, spec/MAPPING.md
// facts 12-15 and 20-21): the per-work analytical findings that accompany
// a JiMS score - modulation-versus-tonicization adjudications with their
// reasons, confidence, uncertainty and responsible agent, focused-review
// flags, and the work-level review findings and audit history.
//
// The score TRANSPORTS this record; it never computes a musical fact from
// it. Two properties matter for correctness:
//
//   * Every adjudication carries an EXACT score-time anchor (an absolute
//     tick). An edit that removes the anchored position makes the
//     assertion stale, and export must say so rather than silently
//     re-timing or emitting apparently valid old analysis.
//   * Open review fields ride as a TYPED value tree (object / array /
//     string / number / boolean / null), not an opaque string, so every
//     leaf stays addressable and schema-checkable in both .mscz and MEI.

#include <vector>

#include "global/types/string.h"

#include "../types/fraction.h"

namespace mu::engraving::melo {
/// One node of the typed review value tree (the .mscz and MEI encodings
/// share this shape; see the jm value elements of the mei-jims profile).
struct ReviewValue {
    enum class Kind : unsigned char {
        Object,
        Array,
        String,
        Number,
        Bool,
        Null,
    };

    Kind kind = Kind::Null;
    muse::String name;                  // member name inside an Object, else empty
    muse::String text;                  // String/Number text, "true"/"false" for Bool
    std::vector<ReviewValue> children;  // Object members / Array items

    bool operator==(const ReviewValue& o) const
    {
        return kind == o.kind && name == o.name && text == o.text && children == o.children;
    }
};

/// One modulation-versus-tonicization adjudication.
struct ReviewAdjudication {
    muse::String annotId;                 // stable identifier (MEI xml:id)
    muse::String outcome;                 // taxonomy token: modulation | tonicization | ambiguous | insufficient-evidence
    muse::String reviewer;                // responsible agent
    std::vector<muse::String> notes;      // reviewer prose
    std::vector<muse::String> evidence;   // evidence pointers (URIs)
    muse::String sourceAnalysis;          // source-analysis pointer (URI)
    Fraction tick = Fraction(-1, 1);      // EXACT absolute score-time anchor
    ReviewValue record;                   // the exact register fields

    bool operator==(const ReviewAdjudication& o) const
    {
        return annotId == o.annotId && outcome == o.outcome && reviewer == o.reviewer
               && notes == o.notes && evidence == o.evidence
               && sourceAnalysis == o.sourceAnalysis && tick == o.tick && record == o.record;
    }
};

/// One revision-history entry.
struct ReviewAudit {
    muse::String changeId;                // stable identifier of the native revisionDesc change
    muse::String date;
    muse::String phase;
    muse::String reason;
    ReviewValue record;                   // exact field / prior_value / new_value

    bool operator==(const ReviewAudit& o) const
    {
        return changeId == o.changeId && date == o.date && phase == o.phase
               && reason == o.reason && record == o.record;
    }
};

/// The complete per-work review record.
struct ReviewRecord {
    muse::String schema;                          // register schema identifier
    muse::String reviewAgent;                     // the register plan's responsible review agent (names every audit change)
    ReviewValue work;                             // work-level findings
    std::vector<muse::String> focusedReviewReasons;
    std::vector<ReviewAudit> audits;
    std::vector<ReviewAdjudication> adjudications;

    bool empty() const
    {
        return schema.isEmpty() && reviewAgent.isEmpty() && work.children.empty()
               && focusedReviewReasons.empty() && audits.empty() && adjudications.empty();
    }

    bool operator==(const ReviewRecord& o) const
    {
        return schema == o.schema && reviewAgent == o.reviewAgent && work == o.work
               && focusedReviewReasons == o.focusedReviewReasons
               && audits == o.audits && adjudications == o.adjudications;
    }

    bool operator!=(const ReviewRecord& o) const { return !(*this == o); }
};
} // namespace mu::engraving::melo
