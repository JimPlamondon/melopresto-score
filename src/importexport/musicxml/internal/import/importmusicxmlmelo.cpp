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
#include "importmusicxmlmelo.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "engraving/dom/factory.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftype.h"
#include "engraving/dom/stafftypechange.h"
#include "engraving/melo/melobridge.h"
#include "engraving/melo/melochangecontroller.h"
#include "engraving/style/style.h"

#include "importmusicxmllogger.h"

#include "log.h"
#include "engraving/melo/melostrings.h"

using namespace muse;
using namespace mu::engraving;

namespace mu::iex::musicxml {
static const char* MELO_URI_STEM = "urn:melopresto:musicxml:";
// The retired stem (before 2026-09-11) stays readable; the exporter never writes it.
static const char* RETIRED_URI_STEM = "urn:jims:musicxml:";

// Fatal MeloPresto import conditions go to the MusicXML logger (the import
// dialog) AND the console log, so a refused import is never silent.
static void meloFatal(MusicXmlLogger* logger, const String& text, const XmlStreamReader* e = nullptr)
{
    LOGE() << mu::engraving::melo::diagnostic::musicXmlImport << text;
    if (logger) {
        logger->logError(text, e);
    }
}

//---------------------------------------------------------
//   resolveFromRoot
//---------------------------------------------------------

Err MeloImportContext::resolveFromRoot(const std::vector<XmlStreamReader::Attribute>& attributes,
                                       MusicXmlLogger* logger, const XmlStreamReader* e)
{
    for (const XmlStreamReader::Attribute& a : attributes) {
        const String name = String::fromAscii(a.name.ascii());
        const char* stem = a.value.startsWith(String::fromAscii(MELO_URI_STEM)) ? MELO_URI_STEM
                           : a.value.startsWith(String::fromAscii(RETIRED_URI_STEM)) ? RETIRED_URI_STEM : nullptr;
        if (!stem) {
            continue;
        }
        const String versionText = a.value.mid(String::fromAscii(stem).size());
        bool ok = false;
        const int version = versionText.toInt(&ok);
        if (!ok || version < MIN_VERSION || version > MAX_VERSION) {
            meloFatal(logger,
                      String(
                          mu::engraving::melo::diagnostic::unsupportedMusicXmlNamespace)
                      .arg(a.value).arg(MIN_VERSION).arg(MAX_VERSION), e);
            return Err::FileBadFormat;
        }
        if (name == u"xmlns") {
            meloFatal(logger, String(mu::engraving::melo::diagnostic::defaultNamespaceUnsupported).arg(
                          a.value), e);
            return Err::FileBadFormat;
        }
        if (!name.startsWith(u"xmlns:")) {
            continue;             // some other attribute happens to hold a MeloPresto URI
        }
        const String prefix = name.mid(6);
        if (prefix.empty()) {
            continue;
        }
        if (hasMelo()) {
            if (version != m_version) {
                meloFatal(logger, String(mu::engraving::melo::diagnostic::conflictingMusicXmlProfiles)
                          .arg(m_version).arg(version), e);
                return Err::FileBadFormat;
            }
            logger->logDebugInfo(String(mu::engraving::melo::diagnostic::namespaceBoundTwice).arg(m_prefix), e);
            continue;
        }
        m_prefix = prefix;
        m_version = version;
    }
    return Err::NoError;
}

//---------------------------------------------------------
//   isJimsElement
//---------------------------------------------------------

bool MeloImportContext::isMeloElement(const AsciiStringView& qualifiedName, const char* local) const
{
    if (!hasMelo()) {
        return false;
    }
    const String name = String::fromAscii(qualifiedName.ascii());
    return name == m_prefix + u":" + String::fromAscii(local);
}

//---------------------------------------------------------
//   jsonNumber
//---------------------------------------------------------

String MeloImportContext::jsonNumber(const String& text, bool& ok)
{
    const std::string s = text.trimmed().toStdString();
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    ok = !s.empty() && end && *end == '\0' && std::isfinite(v);
    if (!ok) {
        return String();
    }
    char buf[64];
    if (v == std::floor(v) && std::fabs(v) < 1e15) {
        std::snprintf(buf, sizeof buf, "%.0f.0", v);
        return String::fromAscii(buf);
    }
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buf, sizeof buf, "%.*g", precision, v);
        if (std::strtod(buf, nullptr) == v) {
            break;
        }
    }
    String out = String::fromAscii(buf);
    if (!out.contains(u'.') && !out.contains(u'e')) {
        out += u".0";
    }
    return out;
}

//---------------------------------------------------------
//   parseStaffState
//---------------------------------------------------------

static String canonicalFragment(XmlStreamReader& e, const String& prefix)
{
    const String name = String::fromAscii(e.name().ascii());
    String fragment = u"<" + name;
    bool hasLocalNamespace = false;
    for (const auto& attribute : e.attributes()) {
        const String attrName = String::fromAscii(attribute.name.ascii());
        hasLocalNamespace = hasLocalNamespace || attrName == u"xmlns:" + prefix;
        fragment += u" " + attrName + u"=\"" + attribute.value.toXmlEscaped() + u"\"";
    }
    if (!hasLocalNamespace) {
        fragment += u" xmlns:" + prefix + u"=\"urn:melopresto:musicxml:5\"";
    }
    fragment += u">" + e.readBody() + u"</" + name + u">";
    e.skipCurrentElement();
    return fragment;
}

bool MeloImportContext::parseReferenceTimeline(XmlStreamReader& e, String& json, String& error) const
{
    if (m_version != 5) {
        error = u"A canonical reference timeline requires MusicXML-Melo version 5.";
        e.skipCurrentElement();
        return false;
    }
    if (e.namespaceUri(m_prefix) != u"urn:melopresto:musicxml:5") {
        error = u"The reference timeline's in-scope namespace is not MusicXML-Melo version 5.";
        e.skipCurrentElement();
        return false;
    }
    return melo::musicxmlReferenceV5Json(canonicalFragment(e, m_prefix), json, error);
}

bool MeloImportContext::parseStaffState(XmlStreamReader& e, String& json, int& staffNumber, String& error) const
{
    // The whole element is always consumed, whatever goes wrong inside it,
    // so the caller's reader stays aligned; the first problem is reported.
    staffNumber = e.hasAttribute("number") ? e.intAttribute("number") : 0;
    if (m_version == 5) {
        if (e.namespaceUri(m_prefix) != u"urn:melopresto:musicxml:5") {
            error = u"The staff configuration's in-scope namespace is not MusicXML-Melo version 5.";
            e.skipCurrentElement();
            return false;
        }
        return melo::musicxmlConfigurationV5Json(canonicalFragment(e, m_prefix), json, error);
    }
    error
        =
            u"This legacy staff state lacks the spelled initial reference and authored relative history required by MusicXML-Melo version 5. Import the original spelled source instead.";
    e.skipCurrentElement();
    return false;
}

//---------------------------------------------------------
//   buffer / statesFor
//---------------------------------------------------------

void MeloImportContext::buffer(const String& partId, const Fraction& tick, int staffNumber, const String& json)
{
    m_states[partId].push_back(BufferedState { tick, staffNumber, json });
}

const std::vector<MeloImportContext::BufferedState>* MeloImportContext::statesFor(const String& partId) const
{
    auto it = m_states.find(partId);
    return it == m_states.end() ? nullptr : &it->second;
}

//---------------------------------------------------------
//   applyToPart
//---------------------------------------------------------

static StaffType meloStaffTypeFor(const String& json)
{
    // The fork's MeloPresto preset (jims12tet: clef/key signature/ledger lines
    // suppressed, JI lines on) with THIS state and its presentation line count.
    StaffType st = *StaffType::preset(StaffTypes::MELO_12TET);
    st.setMelo(true);
    st.setMeloJiLines(true);
    st.setMeloStateJson(json);
    return st;
}

bool MeloImportContext::applyToPart(Score* score, Part* part, const String& partId,
                                    const std::function<int(int)>& staffIndexForNumber, MusicXmlLogger* logger)
{
    const std::vector<BufferedState>* states = statesFor(partId);
    if (!states || states->empty()) {
        return true;
    }
    if (!melo::available()) {
        meloFatal(logger, mu::engraving::melo::diagnostic::importBridgeUnavailable);
        return false;
    }

    // Group by part-relative staff index, preserving document order.
    std::map<int, std::vector<const BufferedState*> > perStaff;
    for (const BufferedState& s : *states) {
        int idx = 0;
        if (s.staffNumber > 0) {
            idx = staffIndexForNumber(s.staffNumber);
            if (idx < 0 || idx >= int(part->nstaves())) {
                meloFatal(logger, String(u"melo:staff-state number %1 names no staff of this part").arg(s.staffNumber));
                return false;
            }
        }
        perStaff[idx].push_back(&s);
    }

    for (const auto& entry : perStaff) {
        const int partStaff = entry.first;
        Staff* staff = part->staff(partStaff);
        const staff_idx_t staffIdx = score->staffIdx(part) + partStaff;
        bool first = true;
        Fraction lastTick(-1, 1);
        for (const BufferedState* s : entry.second) {
            String kernelError;
            String configuration;
            const bool valid = m_version == 5 && melo::storedStaffConfiguration(s->json, configuration, kernelError);
            if (!valid) {
                meloFatal(logger, String(mu::engraving::melo::diagnostic::importStateRejected).arg(kernelError));
                return false;
            }
            if (first) {
                if (!s->tick.isZero()) {
                    meloFatal(logger, u"the first jims:staff-state of a staff must be declared in the first measure");
                    return false;
                }
                staff->setStaffType(Fraction(0, 1), meloStaffTypeFor(s->json));
                first = false;
            } else {
                if (s->tick <= lastTick) {
                    meloFatal(logger, u"melo:staff-state declarations must be in strictly increasing score-time order");
                    return false;
                }
                Measure* measure = score->tick2measure(s->tick);
                if (!measure || s->tick < measure->tick() || s->tick >= measure->endTick()) {
                    meloFatal(logger, u"melo:staff-state does not sit inside a score measure");
                    return false;
                }
                const Fraction rtick = s->tick - measure->tick();
                if (!measure->canAddStaffTypeChange(staffIdx, rtick)) {
                    meloFatal(logger, u"cannot place a staff type change for this jims:staff-state at its exact tick");
                    return false;
                }
                // File-read style construction (TRead::read for StaffTypeChange):
                // Measure::add() replaces the owned type with the staff's copy.
                StaffTypeChange* stc = Factory::createStaffTypeChange(measure);
                stc->setTrack(staffIdx * VOICES);
                stc->setParent(measure);
                stc->setRtick(rtick);
                stc->setStaffType(new StaffType(meloStaffTypeFor(s->json)), true);
                if (rtick.isNotZero()
                    && !measure->findSegmentR(Segment::CHORD_REST_OR_TIME_TICK_TYPE, rtick)) {
                    measure->getSegmentR(SegmentType::TimeTick, rtick);
                }
                measure->add(stc);
            }
            lastTick = s->tick;
        }
    }

    // Milestone 3 engraving-font seam: a JiMS score selects the
    // Kernel-generated JiMSMusic outlines (stock glyphs fall back).
    score->style().set(Sid::musicalSymbolFont, String(u"JiMSMusic"));
    score->style().set(Sid::hideInstrumentNameIfOneInstrument, false);
    return true;
}

//---------------------------------------------------------
//   parseProvenance
//---------------------------------------------------------

bool MeloImportContext::parseProvenance(XmlStreamReader& e, engraving::melo::Provenance& out, String& error) const
{
    out = engraving::melo::Provenance();
    out.strictFallback = e.attribute("fallback-profile") == u"strict";
    while (e.readNextStartElement()) {
        if (isMeloElement(e.name(), "resource")) {
            engraving::melo::ProvenanceResource r;
            r.role = e.attribute("role");
            r.uri = e.attribute("uri");
            r.mediaType = e.attribute("media-type");
            r.sha256 = e.attribute("sha-256");
            if (r.role.isEmpty() || r.uri.isEmpty() || r.mediaType.isEmpty()) {
                error = u"melo:provenance resource is missing role, uri or media-type";
                e.skipCurrentElement();
                return false;
            }
            r.text = e.readText();
            out.resources.push_back(r);
        } else {
            error = String(u"unexpected element in jims:provenance: %1").arg(String::fromAscii(e.name().ascii()));
            e.skipCurrentElement();
            return false;
        }
    }
    return true;
}

//---------------------------------------------------------
//   parseTuningTrajectory
//---------------------------------------------------------

bool MeloImportContext::parseTuningTrajectory(XmlStreamReader& e, const std::function<engraving::Fraction(int)>& ticksOf,
                                              engraving::melo::TuningTrajectory& out, String& error) const
{
    out = engraving::melo::TuningTrajectory();
    while (e.readNextStartElement()) {
        if (!isMeloElement(e.name(), "segment")) {
            error = String(u"unexpected element in jims:tuning-trajectory: %1").arg(String::fromAscii(e.name().ascii()));
            e.skipCurrentElement();
            return false;
        }
        engraving::melo::TrajectorySegment seg;
        bool ok = false;
        const int divisions = e.attribute("duration-divisions").toInt(&ok);
        if (!ok || divisions <= 0) {
            error = u"melo:segment duration-divisions must be a positive integer";
            e.skipCurrentElement();
            return false;
        }
        seg.duration = ticksOf(divisions);
        seg.startCents = e.attribute("start-cents");
        seg.endCents = e.attribute("end-cents");
        seg.interpolation = e.attribute("interpolation");
        if (seg.startCents.isEmpty() || seg.endCents.isEmpty()
            || (seg.interpolation != u"linear" && seg.interpolation != u"cubic-bezier")) {
            error = u"melo:segment needs start-cents, end-cents and interpolation linear|cubic-bezier";
            e.skipCurrentElement();
            return false;
        }
        while (e.readNextStartElement()) {
            if (!isMeloElement(e.name(), "control")) {
                error = String(u"unexpected element in jims:segment: %1").arg(String::fromAscii(e.name().ascii()));
                e.skipCurrentElement();
                return false;
            }
            engraving::melo::TrajectoryControl c;
            c.time = e.attribute("time");
            c.valueCents = e.attribute("value-cents");
            if (c.time.isEmpty() || c.valueCents.isEmpty()) {
                error = u"melo:control needs time and value-cents";
                e.skipCurrentElement();
                return false;
            }
            seg.controls.push_back(c);
            e.skipCurrentElement();
        }
        if (seg.interpolation == u"cubic-bezier" && seg.controls.size() != 2) {
            error = u"a cubic-bezier jims:segment carries exactly two controls";
            return false;
        }
        if (seg.interpolation == u"linear" && !seg.controls.empty()) {
            error = u"a linear jims:segment carries no controls";
            return false;
        }
        out.segments.push_back(seg);
    }
    if (out.segments.empty()) {
        error = u"melo:tuning-trajectory carries no segment";
        return false;
    }
    return true;
}

//---------------------------------------------------------
//   checkSharedStatesAcrossParts
//---------------------------------------------------------

bool MeloImportContext::checkSharedStatesAcrossParts(const Score* score, MusicXmlLogger* logger) const
{
    String error;
    if (!melo::validateSharedStateTimeline(score, error)) {
        meloFatal(logger, error);
        return false;
    }
    return true;
}
}
