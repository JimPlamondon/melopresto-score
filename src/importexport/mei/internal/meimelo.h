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

// MeloPresto MEI carriage (the MeloPresto MEI profile, spec/MAPPING.md of the mei-jims
// repository). Extends the Pull-Request-19 preservation pattern: native MEI
// carries what it can express exactly (typed harm, ambitus, typed annots),
// and the irreducibly JiMS-specific remainder rides as validated typed XML
// in one extMeta jm:record (urn:melopresto:mei:1) embedding verbatim
// urn:melopresto:musicxml:4 fragments produced by the Kernel bridge. Everything is
// regenerated from the score's typed JiMS state at export time — links can
// never go stale — and imported back into the same typed state.

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "engraving/melo/melointerchange.h"
#include "engraving/types/fraction.h"
#include "global/types/string.h"

#include "pugixml.hpp"

namespace mu::engraving {
class Harmony;
class Measure;
class Note;
class Score;
class Staff;
}

namespace mu::iex::mei {
static constexpr const char* MELO_MEI_NS = "urn:melopresto:mei:1";
// Retired spellings (before 2026-09-11): still read, never written.
static constexpr const char* RETIRED_MELO_MEI_NS = "urn:jims:mei:1";
static constexpr const char* MELO_MUSICXML_NS = "urn:melopresto:musicxml:4";
static constexpr const char* MELO_MUSICXML_V5_NS = "urn:melopresto:musicxml:5";
static constexpr const char* RETIRED_MELO_MUSICXML_NS = "urn:jims:musicxml:4";

/// Export-side plan and emission.
class MeloMeiExporter
{
public:
    bool buildPlan(const engraving::Score* score);
    bool present() const { return m_present; }
    const muse::String& error() const { return m_error; }

    /// staffDef decoration: xml:id, child keySig mode, and the native
    /// ambitus (Kernel projection of the extent hull). Returns false on a
    /// projection failure (recorded in error()).
    bool onStaffDef(pugi::xml_node staffDefNode, const engraving::Staff* staff);

    /// Register a measure in document order (its generated xml:id and
    /// exact length); called once per measure from writeMeasure.
    void onMeasure(const engraving::Measure* measure, const std::string& xmlId);

    /// Typed native annotations at score level (tonic ambit, melody part).
    void writeScoreAnnots(pugi::xml_node scoreNode);

    /// Typed native annotations of this measure (tonal states).
    void writeMeasureAnnots(pugi::xml_node measureNode, const engraving::Measure* measure);

    /// Called from writeHarm: adds @type and registers the exact offset.
    void onHarm(pugi::xml_node harmNode, const engraving::Harmony* harmony, const std::string& xmlId);

    /// Called from writeNote: remembers the note's identity record.
    void onNote(const engraving::Note* note, const std::string& xmlId);

    /// Build the extMeta jm:record inside meiHead (replacing any stored
    /// one, which would carry stale links).
    bool writeExtMeta(pugi::xml_node meiHead);

private:
    struct StaffPlan {
        const engraving::Staff* staff = nullptr;
        int staffN = 0;                                            // 1-based MEI staff@n
        std::string staffDefId;
        std::vector<std::pair<engraving::Fraction, muse::String> > states;   // absolute tick -> Kernel state JSON
        std::vector<std::string> stateAnnotIds;
        std::vector<std::string> jmStateIds;
    };

    bool projectPitch(const muse::String& stateJson, int nPer, int nGen, std::string& pname, int& alter, int& octave);
    std::string respIdFor(const muse::String& reviewer);
    void writeClassDecls(pugi::xml_node meiHead, pugi::xml_node fileDesc);

    bool m_present = false;
    muse::String m_error;
    const engraving::Score* m_score = nullptr;
    std::vector<StaffPlan> m_staves;
    muse::String m_tonicAmbit;
    muse::String m_referenceXml;
    std::vector<std::pair<const engraving::Measure*, std::string> > m_measures;
    std::map<const engraving::Measure*, size_t> m_measureIndex;
    std::vector<std::pair<std::string, const engraving::Harmony*> > m_harms;
    std::vector<std::pair<std::string, const engraving::Note*> > m_notes;
    std::vector<muse::String> m_reviewers;      // responsible agents, in first-seen order
    std::vector<std::string> m_adjAnnotIds;     // adjudications actually placed in a measure
};

/// Import-side capture and application.
class MeloMeiImporter
{
public:
    /// Stash the extMeta jm:record (if any) and the staffDef id -> @n map.
    void capture(pugi::xml_node root);

    bool present() const { return !m_record.empty(); }
    bool onHarm(const std::string& xmlId, engraving::Harmony* harmony);

    /// Apply the captured record to the fully-built score: staff states
    /// (StaffType at tick 0 + StaffTypeChange at exact ticks, Kernel-
    /// validated), note identities, tonic ambit and melody part, source
    /// supplements, and tuning trajectories. `noteForId` resolves an MEI
    /// xml:id to the imported note. Returns false with error() set.
    bool apply(engraving::Score* score, const std::function<engraving::Note* (const std::string&)>& noteForId,
               const std::function<int(int)>& staffIndexForN);

    const muse::String& error() const { return m_error; }

private:
    bool stateJsonFromXml(pugi::xml_node staffStateNode, muse::String& json);
    bool m_canonical = false;

    pugi::xml_document m_recordDoc;
    pugi::xml_node m_record;
    std::map<std::string, int> m_staffDefN;    // staffDef xml:id -> @n
    std::vector<engraving::melo::ProvenanceResource> m_provResources;
    struct ChangeEntry {
        muse::String date;
        muse::String phase;
        muse::String reason;
    };

    std::map<std::string, muse::String> m_reviewerById;
    muse::String m_reviewAgent;                 // respStmt/name[@role='melo-review-agent']
    std::map<std::string, ChangeEntry> m_changeById;
    std::vector<muse::String> m_focusedReviewReasons;
    std::map<std::string, pugi::xml_node> m_adjAnnots;
    std::map<std::string, int> m_adjMeasureIndex;
    muse::String m_melodyToken;
    muse::String m_error;
};
} // namespace mu::iex::mei
