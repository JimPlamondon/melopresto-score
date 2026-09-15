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

// Native JiMS MusicXML import (owner decision 1a, 2026-08-16).
//
// The MeloPresto extension (urn:melopresto:musicxml:1 through :4; the retired urn:jims: stem is still read) transports the Kernel's
// JiMStaffStateV2 as jims:staff-state (in attributes) and each note's lattice
// identity as jims:pitch (in note). This unit is pure format transcription —
// the same mapping the fixture converter tools/melo/enriched_to_melo_mscx.py
// performs — and computes no musical fact: every state is validated by the
// Kernel (bridge op `validate`) and stored verbatim as the staff type's state
// JSON; the Kernel-written jims:change summary is never read (the renderer
// derives the change indicator from the two STATES through the Kernel).
//
// muse::XmlStreamReader is pugixml-backed and not namespace-aware: element
// names arrive as written (prefix:local). The prefix bound to a JiMS
// namespace URI is resolved once from the root element's xmlns:* attributes
// (any prefix name); a JiMS URI with an unsupported version is a fatal import
// error — a document that declares itself JiMS must never silently import as
// a plain five-line staff.

#include <functional>
#include <map>
#include <vector>

#include "global/serialization/xmlstreamreader.h"
#include "global/types/string.h"

#include "engraving/engravingerrors.h"
#include "engraving/types/fraction.h"
#include "engraving/types/types.h"
#include "engraving/melo/melointerchange.h"

namespace mu::engraving {
class Score;
class Part;
}

namespace mu::iex::musicxml {
class MusicXmlLogger;

class MeloImportContext
{
public:
    /// The MeloPresto namespace versions this importer understands.
    static constexpr int MIN_VERSION = 1;
    static constexpr int MAX_VERSION = 5;

    /// Resolve the MeloPresto prefix from the root element's attributes. Returns
    /// NoError when no MeloPresto namespace is declared or exactly one supported
    /// version is bound; FileBadFormat (after logging) for an unsupported
    /// version, a default-namespace binding, or two distinct MeloPresto profiles.
    engraving::Err resolveFromRoot(const std::vector<muse::XmlStreamReader::Attribute>& attributes, MusicXmlLogger* logger,
                                   const muse::XmlStreamReader* e);

    bool hasMelo() const { return !m_prefix.empty(); }
    int version() const { return m_version; }
    const muse::String& prefix() const { return m_prefix; }

    /// True when `qualifiedName` is `<prefix>:<local>` for the resolved prefix.
    bool isMeloElement(const muse::AsciiStringView& qualifiedName, const char* local) const;

    /// Parse a jims:staff-state element (reader on its start tag; the reader
    /// is left after its end tag) into the Kernel's compact state JSON —
    /// converter byte-shape: fixed key order, no spaces, tonic_ambit last.
    /// `staffNumber` is the optional `number` attribute (0 when absent).
    /// Returns false with `error` set on a malformed state.
    bool parseStaffState(muse::XmlStreamReader& e, muse::String& json, int& staffNumber, muse::String& error) const;
    bool parseReferenceTimeline(muse::XmlStreamReader& e, muse::String& json, muse::String& error) const;

    /// A parsed state waiting to be applied after normal part parsing.
    struct BufferedState {
        engraving::Fraction tick;   // exact score tick of the attributes event
        int staffNumber = 0;        // MusicXML staff number (0 = unnumbered)
        muse::String json;          // Kernel state JSON, converter byte-shape
    };

    void buffer(const muse::String& partId, const engraving::Fraction& tick, int staffNumber, const muse::String& json);
    const std::vector<BufferedState>* statesFor(const muse::String& partId) const;
    bool anyBuffered() const { return !m_states.empty(); }

    /// Apply the buffered states of one part: the first state per staff
    /// becomes the MeloPresto StaffType at tick 0, every later state a
    /// StaffTypeChange at its exact tick; sets the JiMSMusic engraving font.
    /// `staffIndexForNumber` maps a MusicXML staff number to a part-relative
    /// staff index (-1 when invalid). Returns false (after logging) when the
    /// Kernel rejects a state or a carrier cannot be placed.
    bool applyToPart(engraving::Score* score, engraving::Part* part, const muse::String& partId,
                     const std::function<int(int)>& staffIndexForNumber, MusicXmlLogger* logger);

    /// The MuseScore StaffType line count for a JiMS staff of `periodCount`
    /// periods — fork-owned presentation plumbing fixed by the converter
    /// contract (owner decision 1b, 2026-08-16); not a musical fact.
    static int linesForPeriodCount(int periodCount) { return 12 * periodCount + 1; }

    /// Parse a jims:provenance element (reader on its start tag; left after
    /// its end tag) into the transported carrier. Returns false with `error`
    /// set when a resource lacks role/uri/media-type or an unknown JiMS child
    /// appears. Values are carried verbatim (owner decision 2026-08-19).
    bool parseProvenance(muse::XmlStreamReader& e, engraving::melo::Provenance& out, muse::String& error) const;
    /// Parse a jims:tuning-trajectory element (reader on its start tag; left
    /// after its end tag). `ticksOf` converts a duration-divisions integer to
    /// score time (the pass-1 divisions calculator). Tick, staff and placement
    /// come from the enclosing direction and are set by the caller.
    bool parseTuningTrajectory(muse::XmlStreamReader& e, const std::function<engraving::Fraction(int)>& ticksOf,
                               engraving::melo::TuningTrajectory& out, muse::String& error) const;
    /// Every MeloPresto staff must share the Kernel's musical-state projection
    /// at every effective boundary; redundant transport carriers are allowed.
    bool checkSharedStatesAcrossParts(const engraving::Score* score, MusicXmlLogger* logger) const;
    /// Python-repr-style number text for the state JSON ("700" -> "700.0",
    /// "696.578" stays), so the importer's JSON is byte-identical to the
    /// converter's for the same document.
    static muse::String jsonNumber(const muse::String& text, bool& ok);

private:
    muse::String m_prefix;
    int m_version = 0;
    std::map<muse::String, std::vector<BufferedState> > m_states;
};
}
