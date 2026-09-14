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
#include "meimelo.h"

#include <algorithm>
#include <sstream>

#include "engraving/dom/factory.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftype.h"
#include "engraving/dom/keysig.h"
#include "engraving/dom/stafftypechange.h"
#include "engraving/melo/melobridge.h"
#include "meiconverter.h"
#include "libmei.h"
#include "engraving/melo/melochange.h"
#include "engraving/style/style.h"
#include "global/serialization/json.h"

#include "log.h"
#include "engraving/melo/melostrings.h"

using namespace mu::engraving;
using muse::String;

namespace mu::iex::mei {
//---------------------------------------------------------
// small helpers
//---------------------------------------------------------

static std::string fracStr(const Fraction& quarters)
{
    Fraction f = quarters.reduced();
    return std::to_string(f.numerator()) + "/" + std::to_string(f.denominator());
}

static Fraction quartersOf(const Fraction& f)
{
    return (f * Fraction(4, 1)).reduced();
}

/// Minimal decimal formatting for a tstamp double (no trailing zeros).
static std::string tstampStr(double tstamp)
{
    std::ostringstream oss;
    oss << tstamp;
    return oss.str();
}

static double tstampFrom(const Fraction& rtick, const Fraction& timesig)
{
    // 1 + offset expressed in meter units (same as Convert::tstampFromFraction).
    return 1.0 + (rtick / timesig * Fraction(timesig.denominator(), 1)).toDouble();
}

static const char* accidOf(int alter)
{
    switch (alter) {
    case 1: return "s";
    case -1: return "f";
    case 2: return "xs";
    case -2: return "ff";
    default: return nullptr;
    }
}

/// Lattice bound from the Kernel state JSON (transport read, no arithmetic).
static bool extentBounds(const String& stateJson, int out[4])
{
    std::string err;
    muse::JsonDocument doc = muse::JsonDocument::fromJson(stateJson.toUtf8(), &err);
    if (!err.empty() || !doc.isObject()) {
        return false;
    }
    muse::JsonObject extent = doc.rootObject().value("extent").toObject();
    muse::JsonObject lower = extent.value("lower").toObject();
    muse::JsonObject upper = extent.value("upper").toObject();
    out[0] = lower.value("nPer").toInt();
    out[1] = lower.value("nGen").toInt();
    out[2] = upper.value("nPer").toInt();
    out[3] = upper.value("nGen").toInt();
    return true;
}

//---------------------------------------------------------
// typed review value tree <-> jm value elements
//---------------------------------------------------------

static void writeReviewValue(pugi::xml_node parent, const melo::ReviewValue& v)
{
    using Kind = melo::ReviewValue::Kind;
    const char* tag = "jm:z";
    switch (v.kind) {
    case Kind::Object: tag = "jm:o";
        break;
    case Kind::Array: tag = "jm:a";
        break;
    case Kind::String: tag = "jm:s";
        break;
    case Kind::Number: tag = "jm:num";
        break;
    case Kind::Bool: tag = "jm:b";
        break;
    case Kind::Null: tag = "jm:z";
        break;
    }
    pugi::xml_node node = parent.append_child(tag);
    if (!v.name.isEmpty()) {
        node.append_attribute("n") = v.name.toStdString().c_str();
    }
    if (v.kind == Kind::Object || v.kind == Kind::Array) {
        for (const melo::ReviewValue& c : v.children) {
            writeReviewValue(node, c);
        }
    } else if (v.kind != Kind::Null) {
        node.text().set(v.text.toStdString().c_str());
    }
}

static std::string localNameOf(pugi::xml_node node);

static melo::ReviewValue readReviewValue(pugi::xml_node node)
{
    using Kind = melo::ReviewValue::Kind;
    melo::ReviewValue v;
    v.name = String(node.attribute("n").value());
    const std::string tag = localNameOf(node);
    if (tag == "o" || tag == "a") {
        v.kind = (tag == "o") ? Kind::Object : Kind::Array;
        for (pugi::xml_node child : node.children()) {
            v.children.push_back(readReviewValue(child));
        }
    } else if (tag == "s") {
        v.kind = Kind::String;
        v.text = String(node.text().as_string());
    } else if (tag == "num") {
        v.kind = Kind::Number;
        v.text = String(node.text().as_string());
    } else if (tag == "b") {
        v.kind = Kind::Bool;
        v.text = String(node.text().as_string());
    } else {
        v.kind = Kind::Null;
    }
    return v;
}

//---------------------------------------------------------
// MeloMeiExporter
//---------------------------------------------------------

std::string MeloMeiExporter::respIdFor(const String& reviewer)
{
    for (size_t i = 0; i < m_reviewers.size(); ++i) {
        if (m_reviewers.at(i) == reviewer) {
            return "melo-resp-" + std::to_string(i + 1);
        }
    }
    m_reviewers.push_back(reviewer);
    return "melo-resp-" + std::to_string(m_reviewers.size());
}

bool MeloMeiExporter::buildPlan(const Score* score)
{
    m_score = score;
    m_present = false;
    m_error.clear();
    m_staves.clear();
    m_tonicAmbit.clear();
    m_measures.clear();
    m_measureIndex.clear();
    m_harms.clear();
    m_notes.clear();
    m_reviewers.clear();
    m_adjAnnotIds.clear();
    for (const melo::ReviewAdjudication& adj : score->meloReview().adjudications) {
        if (!adj.reviewer.isEmpty()) {
            respIdFor(adj.reviewer);
        }
    }
    if (!score->meloReview().empty()) {
        m_present = true;
    }

    // MeloPresto chord names must be exportable (same contract as MusicXML export).
    for (const Segment* segment = score->firstSegment(SegmentType::ChordRest); segment;
         segment = segment->next1(SegmentType::ChordRest)) {
        for (const EngravingItem* item : segment->annotations()) {
            if (!item || !item->isHarmony()) {
                continue;
            }
            const Harmony* harmony = toHarmony(item);
            if (harmony->harmonyType() != HarmonyType::MELO) {
                continue;
            }
            if (!harmony->meloEvidenceError().empty()) {
                m_error = harmony->meloEvidenceError();
                return false;
            }
            const String name = harmony->harmonyName();
            bool whitespace = false;
            for (size_t i = 0; i < name.size(); ++i) {
                if (name.at(i).isSpace()) {
                    whitespace = true;
                }
            }
            if (name.isEmpty() || whitespace || name.contains(u'~')) {
                m_error = mu::engraving::melo::diagnostic::meiExportInvalidChordName;
                return false;
            }
            m_present = true;
        }
    }

    int staffN = 0;
    for (const Staff* staff : score->staves()) {
        ++staffN;
        const StaffType* base = staff->staffType(Fraction(0, 1));
        if (!base || !base->isMelo()) {
            continue;
        }
        if (!melo::available()) {
            m_error = mu::engraving::melo::diagnostic::meiExportBridgeUnavailable;
            return false;
        }
        m_present = true;
        StaffPlan plan;
        plan.staff = staff;
        plan.staffN = staffN;
        plan.staffDefId = "melo-sd-" + std::to_string(staffN);
        plan.states.push_back({ Fraction(0, 1), base->meloStateJson() });
        for (const Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
            for (const StaffTypeChange* carrier : melo::changeCarriers(m, staff->idx())) {
                if (!carrier->staffType() || !carrier->staffType()->isMelo()) {
                    continue;
                }
                const Fraction tick = m->tick() + carrier->rtick();
                plan.states.push_back({ tick, carrier->staffType()->meloStateJson() });
            }
        }
        size_t si = 0;
        for (const auto& st : plan.states) {
            UNUSED(st);
            ++si;
            plan.stateAnnotIds.push_back("melo-stannot-" + std::to_string(staffN) + "-" + std::to_string(si));
            plan.jmStateIds.push_back("melo-state-" + std::to_string(staffN) + "-" + std::to_string(si));
        }
        if (m_tonicAmbit.empty()) {
            m_tonicAmbit = base->meloTonicAmbit();
        }
        m_staves.push_back(plan);
    }
    return true;
}

bool MeloMeiExporter::projectPitch(const String& stateJson, int nPer, int nGen, std::string& pname, int& alter, int& octave)
{
    melo::SoundingPitch projection;
    String error;
    if (!melo::noteSoundingPitch(stateJson, nPer, nGen, projection, &error)) {
        m_error = String(mu::engraving::melo::diagnostic::meiExportExtentProjectionFailed).arg(error);
        return false;
    }
    pname = std::string(1, char(std::tolower(projection.step)));
    alter = projection.alter;
    octave = projection.octave;
    return true;
}

bool MeloMeiExporter::onStaffDef(pugi::xml_node staffDefNode, const Staff* staff)
{
    for (StaffPlan& plan : m_staves) {
        if (plan.staff != staff) {
            continue;
        }
        staffDefNode.append_attribute("xml:id") = plan.staffDefId.c_str();
        // Key signature as a child keySig with @mode (mei-basic's @keysig
        // attribute cannot carry the mode; the reader accepts either).
        {
            std::string sig = "0";
            pugi::xml_attribute keysigAttr = staffDefNode.attribute("keysig");
            if (keysigAttr) {
                sig = keysigAttr.value();
                staffDefNode.remove_attribute(keysigAttr);
            }
            pugi::xml_node keySig = staffDefNode.append_child("keySig");
            keySig.append_attribute("sig") = sig.c_str();
            KeyMode mode = plan.staff->keySigEvent(Fraction(0, 1)).mode();
            if (mode == KeyMode::UNKNOWN || mode == KeyMode::NONE) {
                const Measure* first = m_score->firstMeasure();
                const Segment* keySeg = first ? first->findSegment(SegmentType::KeySig, first->tick()) : nullptr;
                if (keySeg) {
                    const EngravingItem* item = keySeg->element(plan.staff->idx() * VOICES);
                    if (item && item->isKeySig()) {
                        mode = toKeySig(item)->keySigEvent().mode();
                    }
                }
            }
            static const std::map<KeyMode, const char*> modeNames = {
                { KeyMode::MAJOR, "major" }, { KeyMode::MINOR, "minor" },
                { KeyMode::DORIAN, "dorian" }, { KeyMode::PHRYGIAN, "phrygian" },
                { KeyMode::LYDIAN, "lydian" }, { KeyMode::MIXOLYDIAN, "mixolydian" },
                { KeyMode::AEOLIAN, "aeolian" }, { KeyMode::IONIAN, "ionian" },
                { KeyMode::LOCRIAN, "locrian" },
            };
            auto it = modeNames.find(mode);
            if (it != modeNames.end()) {
                keySig.append_attribute("mode") = it->second;
            }
        }
        // Extent hull across the state timeline, projected by the Kernel.
        std::string loP, hiP;
        int loAlter = 0, hiAlter = 0, loOct = 0, hiOct = 0;
        bool first = true;
        auto rank = [](const std::string& pname, int oct) {
            static const std::string steps = "cdefgab";
            return oct * 7 + int(steps.find(pname.at(0)));
        };
        for (const auto& st : plan.states) {
            int b[4];
            if (!extentBounds(st.second, b)) {
                m_error = mu::engraving::melo::diagnostic::meiExportExtentUnreadable;
                return false;
            }
            std::string p;
            int alter = 0, oct = 0;
            if (!projectPitch(st.second, b[0], b[1], p, alter, oct)) {
                return false;
            }
            if (first || rank(p, oct) < rank(loP, loOct)) {
                loP = p;
                loAlter = alter;
                loOct = oct;
            }
            if (!projectPitch(st.second, b[2], b[3], p, alter, oct)) {
                return false;
            }
            if (first || rank(p, oct) > rank(hiP, hiOct)) {
                hiP = p;
                hiAlter = alter;
                hiOct = oct;
            }
            first = false;
        }
        if (!staffDefNode.attribute("clef.shape") && !staffDefNode.child("clef")) {
            const ClefTypeList clefs = plan.staff->clefType(Fraction(0, 1));
            libmei::Clef meiClef = Convert::clefToMEI(clefs.concertClef);
            if (meiClef.HasShape() && meiClef.HasLine()) {
                libmei::AttConverter attConverter;
                pugi::xml_node clefNode = staffDefNode.append_child("clef");
                clefNode.append_attribute("shape") = attConverter.ClefshapeToStr(meiClef.GetShape()).c_str();
                clefNode.append_attribute("line") = meiClef.GetLine();
                if (meiClef.HasDis()) {
                    int dis = 0;
                    switch (meiClef.GetDis()) {
                    case libmei::OCTAVE_DIS_8: dis = 8;
                        break;
                    case libmei::OCTAVE_DIS_15: dis = 15;
                        break;
                    case libmei::OCTAVE_DIS_22: dis = 22;
                        break;
                    default: break;
                    }
                    if (dis) {
                        clefNode.append_attribute("dis") = dis;
                        clefNode.append_attribute("dis.place") = attConverter.StaffrelBasicToStr(meiClef.GetDisPlace()).c_str();
                    }
                }
            }
        }
        pugi::xml_node ambitus = staffDefNode.append_child("ambitus");
        pugi::xml_node lo = ambitus.append_child("ambNote");
        lo.append_attribute("type") = "lowest";
        lo.append_attribute("pname") = loP.c_str();
        lo.append_attribute("oct") = loOct;
        if (const char* a = accidOf(loAlter)) {
            lo.append_attribute("accid") = a;
        }
        pugi::xml_node hi = ambitus.append_child("ambNote");
        hi.append_attribute("type") = "highest";
        hi.append_attribute("pname") = hiP.c_str();
        hi.append_attribute("oct") = hiOct;
        if (const char* a = accidOf(hiAlter)) {
            hi.append_attribute("accid") = a;
        }
    }
    return true;
}

void MeloMeiExporter::onMeasure(const Measure* measure, const std::string& xmlId)
{
    m_measureIndex[measure] = m_measures.size();
    m_measures.push_back({ measure, xmlId });
}

void MeloMeiExporter::writeScoreAnnots(pugi::xml_node scoreNode)
{
    if (!m_present) {
        return;
    }
    if (!m_tonicAmbit.empty()) {
        pugi::xml_node annot = scoreNode.append_child("annot");
        annot.append_attribute("xml:id") = "melo-ambit";
        annot.append_attribute("type") = "melo-tonic-ambit";
        annot.append_attribute("class") = ("#melo.ambit." + m_tonicAmbit.toStdString()).c_str();
        annot.text().set(m_tonicAmbit.toStdString().c_str());
    }
    const melo::ReviewRecord& review = m_score->meloReview();
    if (!review.focusedReviewReasons.empty()) {
        pugi::xml_node fr = scoreNode.append_child("annot");
        fr.append_attribute("xml:id") = "melo-focused-review";
        fr.append_attribute("type") = "melo-focused-review";
        for (const String& reason : review.focusedReviewReasons) {
            fr.append_child("p").text().set(reason.toStdString().c_str());
        }
    }
    if (!m_staves.empty()) {
        const String token = melo::melodyPartToken(m_score->meloMelodyPart());
        pugi::xml_node annot = scoreNode.append_child("annot");
        annot.append_attribute("xml:id") = "melo-melody";
        annot.append_attribute("type") = "melo-melody-part";
        annot.append_attribute("class") = ("#melo.melody." + token.toStdString()).c_str();
        for (const StaffPlan& plan : m_staves) {
            const Part* part = plan.staff->part();
            // A MusicXML-imported part carries the role in partName; an
            // MEI-imported one carries it in the staffDef label, which is
            // the instrument long name. Either names the melody staff.
            if (part && (part->partName().toLower() == token || part->longName().toLower() == token)) {
                annot.append_attribute("plist") = ("#" + plan.staffDefId).c_str();
                break;
            }
        }
        annot.text().set(token.toStdString().c_str());
    }
}

void MeloMeiExporter::writeMeasureAnnots(pugi::xml_node measureNode, const Measure* measure)
{
    // Evidentiary adjudications anchored inside this measure. An anchor that
    // no longer lands in the score is STALE: it is marked, never silently
    // re-timed (spec/MAPPING.md fact 12-14).
    const melo::ReviewRecord& review = m_score->meloReview();
    for (size_t i = 0; i < review.adjudications.size(); ++i) {
        const melo::ReviewAdjudication& adj = review.adjudications.at(i);
        if (adj.tick < measure->tick() || adj.tick >= measure->endTick()) {
            continue;
        }
        pugi::xml_node annot = measureNode.append_child("annot");
        const std::string id = adj.annotId.isEmpty()
                               ? ("melo-adj-" + std::to_string(i + 1)) : adj.annotId.toStdString();
        annot.append_attribute("xml:id") = id.c_str();
        annot.append_attribute("type") = "melo-adjudication";
        annot.append_attribute("class") = ("#melo.outcome." + adj.outcome.toStdString()).c_str();
        annot.append_attribute("tstamp")
            = tstampStr(tstampFrom(adj.tick - measure->tick(), measure->timesig())).c_str();
        if (!adj.reviewer.isEmpty()) {
            annot.append_attribute("resp") = ("#" + respIdFor(adj.reviewer)).c_str();
        }
        for (const String& note : adj.notes) {
            annot.append_child("p").text().set(note.toStdString().c_str());
        }
        pugi::xml_node ptrs = annot.append_child("p");
        for (const String& ev : adj.evidence) {
            pugi::xml_node ptr = ptrs.append_child("ptr");
            ptr.append_attribute("type") = "melo-evidence";
            ptr.append_attribute("target") = ev.toStdString().c_str();
        }
        if (!adj.sourceAnalysis.isEmpty()) {
            pugi::xml_node ptr = ptrs.append_child("ptr");
            ptr.append_attribute("type") = "melo-source-analysis";
            ptr.append_attribute("target") = adj.sourceAnalysis.toStdString().c_str();
        }
        m_adjAnnotIds.push_back(id);
    }
    for (StaffPlan& plan : m_staves) {
        for (size_t si = 0; si < plan.states.size(); ++si) {
            const Fraction tick = plan.states.at(si).first;
            if (tick < measure->tick() || tick >= measure->endTick()) {
                continue;
            }
            pugi::xml_node annot = measureNode.append_child("annot");
            annot.append_attribute("xml:id") = plan.stateAnnotIds.at(si).c_str();
            annot.append_attribute("type") = "melo-tonal-state";
            annot.append_attribute("staff") = plan.staffN;
            annot.append_attribute("tstamp") = tstampStr(tstampFrom(tick - measure->tick(), measure->timesig())).c_str();
            // No @corresp: the jm:state record links back with @annot, and
            // MEI's own linking rule resolves @corresp only against
            // MEI-namespace targets (mei-melo spec/MAPPING.md fact 8).
        }
    }
}

void MeloMeiExporter::onHarm(pugi::xml_node harmNode, const Harmony* harmony, const std::string& xmlId)
{
    if (!harmony || harmony->harmonyType() != HarmonyType::MELO) {
        return;
    }
    // @type is a space-separated token list; ensure the profile token once.
    pugi::xml_attribute type = harmNode.attribute("type");
    const std::string current = type ? type.value() : "";
    const std::string padded = " " + current + " ";
    if (padded.find(" melo-chord-name ") == std::string::npos && padded.find(" jims-chord-name ") == std::string::npos) {
        const std::string merged = current.empty() ? "melo-chord-name" : current + " melo-chord-name";
        if (type) {
            type.set_value(merged.c_str());
        } else {
            harmNode.append_attribute("type") = merged.c_str();
        }
    }
    m_harms.push_back({ xmlId, harmony });
}

void MeloMeiExporter::onNote(const Note* note, const std::string& xmlId)
{
    if (!note || !note->hasMeloPitch()) {
        return;
    }
    const StaffType* st = note->staff() ? note->staff()->staffTypeForElement(note) : nullptr;
    if (!st || !st->isMelo()) {
        return;
    }
    m_notes.push_back({ xmlId, note });
}

/// The controlled vocabulary every MeloPresto annot @class points into
/// (encodingDesc/classDecls), with the same category ids the mei-melo
/// generator declares, so that MEI's own rule "@class must correspond to
/// the @xml:id of a category" holds on a fork export as on a direct file.
void MeloMeiExporter::writeClassDecls(pugi::xml_node meiHead, pugi::xml_node fileDesc)
{
    pugi::xml_node encodingDesc = meiHead.child("encodingDesc");
    if (!encodingDesc) {
        encodingDesc = fileDesc ? meiHead.insert_child_after("encodingDesc", fileDesc)
                       : meiHead.prepend_child("encodingDesc");
    }
    if (encodingDesc.child("classDecls")) {
        return;
    }
    pugi::xml_node classDecls = encodingDesc.append_child("classDecls");
    pugi::xml_node taxonomy = classDecls.append_child("taxonomy");
    taxonomy.append_attribute("xml:id") = "melo.taxonomy";
    taxonomy.append_child("bibl").text().set("MeloPresto analysis controlled vocabulary v1");
    static const std::vector<std::pair<const char*, std::vector<const char*> > > groups = {
        { "outcome", { "modulation", "tonicization", "ambiguous", "insufficient-evidence" } },
        { "ambit", { "tonic-bounded", "tonic-centered" } },
        { "melody", { "soprano", "alto", "tenor", "bass" } },
        { "ambiguity", { "short-tonicization-vs-brief-modulation", "pivot-region",
                         "conflicting-cadence-evidence", "insufficient-context" } },
    };
    for (const auto& group : groups) {
        pugi::xml_node g = taxonomy.append_child("category");
        g.append_attribute("xml:id") = (std::string("melo.") + group.first).c_str();
        for (const char* value : group.second) {
            pugi::xml_node c = g.append_child("category");
            c.append_attribute("xml:id") = (std::string("melo.") + group.first + "." + value).c_str();
            c.append_child("label").text().set(value);
        }
    }
}

bool MeloMeiExporter::writeExtMeta(pugi::xml_node meiHead)
{
    if (!m_present) {
        return true;
    }
    // Header fixups for the MeloPresto MEI profile: the fileDesc title carries
    // the movement title, workList the work title, and the composer rides
    // in a native composer element (the reader's authoritative carriers).
    {
        const String movementTitle = m_score->metaTag(u"movementTitle");
        const String workTitle = m_score->metaTag(u"workTitle");
        const String composer = m_score->metaTag(u"composer");
        pugi::xml_node fileDesc = meiHead.child("fileDesc");
        pugi::xml_node titleStmt = fileDesc ? fileDesc.child("titleStmt") : pugi::xml_node();
        if (titleStmt) {
            pugi::xml_node title = titleStmt.child("title");
            if (title && !movementTitle.isEmpty()) {
                title.text().set(movementTitle.toStdString().c_str());
            }
            if (!titleStmt.child("composer") && !composer.isEmpty()) {
                pugi::xml_node comp = titleStmt.insert_child_after("composer", titleStmt.child("title"));
                pugi::xml_node pn = comp.append_child("persName");
                pn.text().set(composer.toStdString().c_str());
            }
        }
        writeClassDecls(meiHead, fileDesc);
        if (!meiHead.child("workList") && !workTitle.isEmpty()) {
            pugi::xml_node revisionForOrder = meiHead.child("revisionDesc");
            pugi::xml_node extForOrder = meiHead.child("extMeta");
            pugi::xml_node anchor = extForOrder ? extForOrder : revisionForOrder;
            pugi::xml_node workList = anchor ? meiHead.insert_child_before("workList", anchor)
                                      : meiHead.append_child("workList");
            pugi::xml_node work = workList.append_child("work");
            pugi::xml_node wt = work.append_child("title");
            wt.text().set(workTitle.toStdString().c_str());
        }
    }

    // Any stored extMeta record would carry stale links; regenerate.
    for (pugi::xml_node ext = meiHead.child("extMeta"); ext;) {
        pugi::xml_node next = ext.next_sibling("extMeta");
        meiHead.remove_child(ext);
        ext = next;
    }
    // extMeta must precede revisionDesc in meiHead.
    pugi::xml_node revision = meiHead.child("revisionDesc");
    pugi::xml_node ext = revision ? meiHead.insert_child_before("extMeta", revision) : meiHead.append_child("extMeta");
    pugi::xml_node rec = ext.append_child("jm:record");
    rec.append_attribute("xmlns:jm") = MELO_MEI_NS;
    rec.append_attribute("xmlns:melo") = MELO_MUSICXML_NS;
    rec.append_attribute("version") = "1";
    pugi::xml_node mx = rec.append_child("jm:musicxml");

    for (StaffPlan& plan : m_staves) {
        pugi::xml_node pe = mx.append_child("jm:part");
        pe.append_attribute("ref") = ("#" + plan.staffDefId).c_str();
        for (size_t si = 0; si < plan.states.size(); ++si) {
            const Fraction tick = plan.states.at(si).first;
            const Measure* measure = nullptr;
            size_t midx = 0;
            for (size_t mi = 0; mi < m_measures.size(); ++mi) {
                const Measure* candidate = m_measures.at(mi).first;
                if (tick >= candidate->tick() && tick < candidate->endTick()) {
                    measure = candidate;
                    midx = mi;
                    break;
                }
            }
            if (!measure) {
                m_error = mu::engraving::melo::diagnostic::meiExportStateOutsideMeasure;
                return false;
            }
            auto mit = m_measureIndex.find(measure);
            UNUSED(mit);
            pugi::xml_node se = pe.append_child("jm:state");
            se.append_attribute("xml:id") = plan.jmStateIds.at(si).c_str();
            se.append_attribute("measure") = int(midx) + 1;
            se.append_attribute("off") = fracStr(quartersOf(tick - measure->tick())).c_str();
            se.append_attribute("annot") = ("#" + plan.stateAnnotIds.at(si)).c_str();
            String fragment;
            String err;
            if (!melo::musicxmlStaffStateV3Xml(plan.states.at(si).second, 0, fragment, &err)) {
                m_error = String(mu::engraving::melo::diagnostic::meiExportStateSerializationFailed).arg(err);
                return false;
            }
            pugi::xml_document fragDoc;
            if (!fragDoc.load_string(fragment.toStdString().c_str())) {
                m_error = mu::engraving::melo::diagnostic::meiExportStateFragmentInvalid;
                return false;
            }
            se.append_copy(fragDoc.first_child());
        }
        pugi::xml_node notes = pe.append_child("jm:notes");
        for (const auto& entry : m_notes) {
            if (entry.second->staff() != plan.staff) {
                continue;
            }
            pugi::xml_node ne = notes.append_child("jm:note");
            ne.append_attribute("ref") = ("#" + entry.first).c_str();
            pugi::xml_node px = ne.append_child("melo:pitch");
            px.append_attribute("n-per") = entry.second->meloNPer();
            px.append_attribute("n-gen") = entry.second->meloNGen();
        }
        // Tuning trajectories (verbatim carriers; duration-divisions in the
        // MeloPresto MEI canonical quarter-note basis).
        for (const melo::TuningTrajectory& t : plan.staff->meloTuningTrajectories()) {
            const Measure* measure = nullptr;
            size_t midx = 0;
            for (size_t mi = 0; mi < m_measures.size(); ++mi) {
                const Measure* candidate = m_measures.at(mi).first;
                if (t.tick >= candidate->tick() && t.tick < candidate->endTick()) {
                    measure = candidate;
                    midx = mi;
                    break;
                }
            }
            if (!measure) {
                continue;
            }
            pugi::xml_node te = pe.append_child("jm:trajectory");
            te.append_attribute("measure") = int(midx) + 1;
            te.append_attribute("off") = fracStr(quartersOf(t.tick - measure->tick())).c_str();
            te.append_attribute("staff") = plan.staffN;
            // Direction placement is layout: the MEI profile grammar (mei-melo
            // customization/melo-mei-extension.rng, jm:trajectory) does not
            // carry it; the native score and MusicXML do.
            pugi::xml_node tt = te.append_child("melo:tuning-trajectory");
            for (const melo::TrajectorySegment& seg : t.segments) {
                pugi::xml_node sege = tt.append_child("melo:segment");
                // MeloPresto MEI canonical basis: 960 divisions per quarter note.
                const Fraction div = (quartersOf(seg.duration) * Fraction(960, 1)).reduced();
                if (div.denominator() != 1) {
                    m_error = mu::engraving::melo::diagnostic::meiExportDurationInvalid;
                    return false;
                }
                sege.append_attribute("duration-divisions") = div.numerator();
                sege.append_attribute("start-cents") = seg.startCents.toStdString().c_str();
                sege.append_attribute("end-cents") = seg.endCents.toStdString().c_str();
                sege.append_attribute("interpolation") = seg.interpolation.toStdString().c_str();
                for (const melo::TrajectoryControl& c : seg.controls) {
                    pugi::xml_node ce = sege.append_child("melo:control");
                    ce.append_attribute("time") = c.time.toStdString().c_str();
                    ce.append_attribute("value-cents") = c.valueCents.toStdString().c_str();
                }
            }
        }
    }

    pugi::xml_node me = mx.append_child("jm:measures");
    for (const auto& entry : m_measures) {
        pugi::xml_node ev = me.append_child("jm:measure");
        ev.append_attribute("ref") = ("#" + entry.second).c_str();
        ev.append_attribute("len") = fracStr(quartersOf(entry.first->ticks())).c_str();
    }

    pugi::xml_node he = mx.append_child("jm:harmony");
    for (const auto& entry : m_harms) {
        const Harmony* harmony = entry.second;
        const EngravingObject* parent = harmony->explicitParent();
        if (!parent || !parent->isSegment()) {
            continue;
        }
        const Segment* segment = toSegment(parent);
        const Measure* measure = segment->measure();
        auto mit = m_measureIndex.find(measure);
        if (mit == m_measureIndex.end()) {
            continue;
        }
        pugi::xml_node ev = he.append_child("jm:event");
        ev.append_attribute("harm") = ("#" + entry.first).c_str();
        if (!harmony->meloEvidence().empty()) {
            auto proof = ev.append_child("jm:chord-evidence");
            proof.append_attribute("origin") = harmony->meloEvidenceOrigin().toStdString().c_str();
            proof.text().set(harmony->meloEvidence().toStdString().c_str());
        }
        ev.append_attribute("measure") = int(mit->second) + 1;
        ev.append_attribute("off") = fracStr(quartersOf(segment->tick() - measure->tick())).c_str();
    }

    // Responsible agents for the evidentiary adjudications, and the review
    // agent every audit-history change names (MEI's change rule wants one).
    const melo::ReviewRecord& reviewForHeader = m_score->meloReview();
    if (!m_reviewers.empty() || !reviewForHeader.reviewAgent.isEmpty()) {
        pugi::xml_node fileDesc = meiHead.child("fileDesc");
        if (!fileDesc) {
            fileDesc = meiHead.prepend_child("fileDesc");
        }
        pugi::xml_node titleStmt = fileDesc.child("titleStmt");
        if (!titleStmt) {
            titleStmt = fileDesc.prepend_child("titleStmt");
        }
        pugi::xml_node respStmt = titleStmt.child("respStmt");
        if (!respStmt) {
            respStmt = titleStmt.append_child("respStmt");
        }
        for (size_t i = 0; i < m_reviewers.size(); ++i) {
            const std::string id = "melo-resp-" + std::to_string(i + 1);
            bool exists = false;
            for (pugi::xml_node pn : respStmt.children("persName")) {
                if (id == pn.attribute("xml:id").value()) {
                    exists = true;
                }
            }
            if (exists) {
                continue;
            }
            pugi::xml_node pn = respStmt.append_child("persName");
            pn.append_attribute("xml:id") = id.c_str();
            pn.append_attribute("role") = "melo-reviewer";
            pn.text().set(m_reviewers.at(i).toStdString().c_str());
        }
        if (!reviewForHeader.reviewAgent.isEmpty()
            && !respStmt.find_child_by_attribute("name", "xml:id", "resp-agent")) {
            pugi::xml_node nm = respStmt.append_child("name");
            nm.append_attribute("xml:id") = "resp-agent";
            nm.append_attribute("role") = "melo-review-agent";
            nm.text().set(reviewForHeader.reviewAgent.toStdString().c_str());
        }
    }

    // Native provenance sources (uri, media type, hash) in fileDesc/sourceDesc.
    const melo::Provenance& prov = m_score->meloProvenance();
    if (!prov.resources.empty()) {
        pugi::xml_node fileDesc = meiHead.child("fileDesc");
        if (!fileDesc) {
            fileDesc = meiHead.prepend_child("fileDesc");
        }
        pugi::xml_node sourceDesc = fileDesc.child("sourceDesc");
        if (!sourceDesc) {
            sourceDesc = fileDesc.append_child("sourceDesc");
        }
        int i = 0;
        for (const melo::ProvenanceResource& r : prov.resources) {
            ++i;
            const std::string id = "melo-src-prov-" + std::to_string(i);
            bool exists = false;
            for (pugi::xml_node src : sourceDesc.children("source")) {
                if (id == src.attribute("xml:id").value()) {
                    exists = true;
                }
            }
            if (exists) {
                continue;
            }
            pugi::xml_node src = sourceDesc.append_child("source");
            src.append_attribute("type") = "melo-provenance";
            src.append_attribute("xml:id") = id.c_str();
            pugi::xml_node bibl = src.append_child("bibl");
            auto ident = [&bibl](const char* type, const String& value) {
                if (value.isEmpty()) {
                    return;
                }
                pugi::xml_node n = bibl.append_child("identifier");
                n.append_attribute("type") = type;
                n.text().set(value.toStdString().c_str());
            };
            ident("uri", r.uri);
            ident("sha-256", r.sha256);
            ident("media-type", r.mediaType);
        }
    }

    // Provenance supplement (the MeloPresto-constrained remainder).
    if (!prov.empty()) {
        pugi::xml_node ss = mx.append_child("jm:source-supplement");
        ss.append_attribute("strict") = prov.strictFallback ? "true" : "false";
        int i = 0;
        for (const melo::ProvenanceResource& r : prov.resources) {
            ++i;
            pugi::xml_node se = ss.append_child("jm:source");
            se.append_attribute("ref") = ("#melo-src-prov-" + std::to_string(i)).c_str();
            if (!r.role.isEmpty()) {
                se.append_attribute("role") = r.role.toStdString().c_str();
            }
            if (!r.text.isEmpty()) {
                se.text().set(r.text.toStdString().c_str());
            }
        }
    }
    // The evidentiary review record. Every adjudication must still resolve
    // to a live score position; a stale anchor is reported, never silently
    // emitted as valid analysis.
    const melo::ReviewRecord& review = m_score->meloReview();
    if (!review.empty()) {
        pugi::xml_node rv = rec.append_child("jm:review");
        rv.append_attribute("schema") = review.schema.toStdString().c_str();
        if (!review.work.children.empty()) {
            pugi::xml_node w = rv.append_child("jm:work");
            writeReviewValue(w, review.work);
        }
        // Revision history: date, agent and prose ride the NATIVE
        // revisionDesc (spec/MAPPING.md fact 20); jm:audit carries only the
        // exact field/prior/new payload, linked by @change.
        pugi::xml_node revisionDesc = meiHead.child("revisionDesc");
        if (!revisionDesc && !review.audits.empty()) {
            revisionDesc = meiHead.append_child("revisionDesc");
        }
        for (size_t i = 0; i < review.audits.size(); ++i) {
            const melo::ReviewAudit& a = review.audits.at(i);
            // reuse the imported change identity so a round trip never
            // duplicates the native revision entry
            const std::string id = a.changeId.isEmpty()
                                   ? ("melo-change-" + std::to_string(i + 1))
                                   : a.changeId.toStdString();
            bool exists = false;
            for (pugi::xml_node ch : revisionDesc.children("change")) {
                if (id == ch.attribute("xml:id").value()) {
                    exists = true;
                }
            }
            if (!exists) {
                pugi::xml_node ch = revisionDesc.append_child("change");
                ch.append_attribute("xml:id") = id.c_str();
                if (!a.date.isEmpty()) {
                    ch.append_attribute("isodate") = a.date.toStdString().c_str();
                }
                if (!a.phase.isEmpty()) {
                    ch.append_attribute("label") = a.phase.toStdString().c_str();
                }
                if (!review.reviewAgent.isEmpty()) {
                    ch.append_attribute("resp") = "#resp-agent";
                }
                pugi::xml_node cd = ch.append_child("changeDesc");
                cd.append_child("p").text().set(a.reason.toStdString().c_str());
            }
            pugi::xml_node ae = rv.append_child("jm:audit");
            ae.append_attribute("change") = ("#" + id).c_str();
            writeReviewValue(ae, a.record);
        }
        size_t emitted = 0;
        for (size_t i = 0; i < review.adjudications.size(); ++i) {
            const melo::ReviewAdjudication& adj = review.adjudications.at(i);
            const std::string id = adj.annotId.isEmpty()
                                   ? ("melo-adj-" + std::to_string(i + 1)) : adj.annotId.toStdString();
            const bool placed = std::find(m_adjAnnotIds.begin(), m_adjAnnotIds.end(), id) != m_adjAnnotIds.end();
            pugi::xml_node te = rv.append_child("jm:adjudication");
            if (placed) {
                te.append_attribute("annot") = ("#" + id).c_str();
                ++emitted;
            } else {
                // the anchored position no longer exists in this score
                te.append_attribute("stale") = "true";
                te.append_attribute("id") = id.c_str();
                te.append_attribute("tick") = adj.tick.toString().toStdString().c_str();
                LOGW() << mu::engraving::melo::diagnostic::meiExportStaleAdjudication << id
                       << " is stale (its score-time anchor no longer resolves); marked stale";
            }
            writeReviewValue(te, adj.record);
        }
        UNUSED(emitted);
    }
    return true;
}

//---------------------------------------------------------
// MeloMeiImporter
//---------------------------------------------------------

void MeloMeiImporter::capture(pugi::xml_node root)
{
    m_error.clear();
    m_staffDefN.clear();
    pugi::xml_node record = root.select_node("//extMeta/*").node();
    if (!record || String(record.name()) != u"jm:record") {
        // Also accept an arbitrary prefix for urn:jims:mei:1 by local name.
        record = pugi::xml_node();
        for (pugi::xpath_node candidate : root.select_nodes("//extMeta/*")) {
            String name = String(candidate.node().name());
            if (name == u"jm:record" || name.endsWith(u":record") || name == u"record") {
                record = candidate.node();
                break;
            }
        }
    }
    if (record) {
        m_recordDoc.reset();
        m_recordDoc.append_copy(record);
        m_record = m_recordDoc.first_child();
    }
    for (pugi::xpath_node sd : root.select_nodes("//staffDef[@xml:id]")) {
        m_staffDefN[sd.node().attribute("xml:id").value()] = sd.node().attribute("n").as_int();
    }
    m_provResources.clear();
    for (pugi::xpath_node src : root.select_nodes("//sourceDesc/source[@type='melo-provenance' or @type='jims-provenance']")) {
        engraving::melo::ProvenanceResource resource;
        for (pugi::xpath_node ident : src.node().select_nodes(".//identifier")) {
            const std::string type = ident.node().attribute("type").value();
            const String value = String(ident.node().text().as_string());
            if (type == "uri") {
                resource.uri = value;
            } else if (type == "sha-256") {
                resource.sha256 = value;
            } else if (type == "media-type") {
                resource.mediaType = value;
            }
        }
        m_provResources.push_back(resource);
    }
    m_melodyToken.clear();
    pugi::xml_node melody = root.select_node("//score/annot[@type='melo-melody-part' or @type='jims-melody-part']").node();
    if (melody) {
        m_melodyToken = String(melody.text().as_string());
    }

    // Evidentiary review carriers: responsible agents, focused-review
    // reasons, and each adjudication's native annotation (its class,
    // prose, pointers, and exact timing).
    m_reviewerById.clear();
    for (pugi::xpath_node pn : root.select_nodes("//respStmt/persName[@role='melo-reviewer' or @role='jims-reviewer']")) {
        m_reviewerById[pn.node().attribute("xml:id").value()] = String(pn.node().text().as_string());
    }
    m_reviewAgent = String(root.select_node("//respStmt/name[@role='melo-review-agent']").node().text().as_string());
    m_focusedReviewReasons.clear();
    pugi::xml_node fr = root.select_node("//score/annot[@type='melo-focused-review' or @type='jims-focused-review']").node();
    if (fr) {
        for (pugi::xml_node p : fr.children("p")) {
            m_focusedReviewReasons.push_back(String(p.text().as_string()));
        }
    }
    m_adjAnnots.clear();
    for (pugi::xpath_node a : root.select_nodes("//measure/annot[@type='melo-adjudication' or @type='jims-adjudication']")) {
        m_adjAnnots[a.node().attribute("xml:id").value()] = a.node();
    }
    m_changeById.clear();
    for (pugi::xpath_node ch : root.select_nodes("//revisionDesc/change[@xml:id]")) {
        ChangeEntry entry;
        entry.date = String(ch.node().attribute("isodate").value());
        entry.phase = String(ch.node().attribute("label").value());
        entry.reason = String(ch.node().select_node(".//changeDesc/p").node().text().as_string());
        m_changeById[ch.node().attribute("xml:id").value()] = entry;
    }
    m_adjMeasureIndex.clear();
    int mi = 0;
    for (pugi::xpath_node m : root.select_nodes("//section/measure")) {
        for (pugi::xpath_node a : m.node().select_nodes("./annot[@type='melo-adjudication' or @type='jims-adjudication']")) {
            m_adjMeasureIndex[a.node().attribute("xml:id").value()] = mi;
        }
        ++mi;
    }
}

/// jx local name of a node whose prefix is unknown ("jx:staff-state",
/// "jims:staff-state", or unprefixed).
static std::string localName(pugi::xml_node node)
{
    std::string name = node.name();
    size_t colon = name.find(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}

static std::string localNameOf(pugi::xml_node node)
{
    return localName(node);
}

static pugi::xml_node childByLocal(pugi::xml_node parent, const char* local)
{
    for (pugi::xml_node child : parent.children()) {
        if (localName(child) == local) {
            return child;
        }
    }
    return pugi::xml_node();
}

bool MeloMeiImporter::stateJsonFromXml(pugi::xml_node staffStateNode, String& json)
{
    // Mirrors MeloImportContext::parseStaffState's converter byte-shape:
    // fixed key order, no spaces, tonic_ambit last (the musicxml importer
    // remains the owning transcription; re-sync on change).
    auto jsonNumber = [](const std::string& text, bool& ok) -> std::string {
        ok = false;
        if (text.empty()) {
            return text;
        }
        char* end = nullptr;
        std::strtod(text.c_str(), &end);
        ok = end && *end == '\0';
        return text;
    };

    std::vector<std::string> steps;
    pugi::xml_node scale = childByLocal(staffStateNode, "scale");
    for (pugi::xml_node step : scale.children()) {
        if (localName(step) == "step") {
            steps.push_back(step.text().as_string());
        }
    }
    pugi::xml_node embedding = childByLocal(staffStateNode, "embedding");
    pugi::xml_node extent = childByLocal(staffStateNode, "extent");
    pugi::xml_node reference = childByLocal(staffStateNode, "reference");
    pugi::xml_node ambit = childByLocal(staffStateNode, "tonic-ambit");
    if (!ambit) {
        ambit = childByLocal(staffStateNode, "tonic-extent");   // legacy spelling, read only
    }
    if (steps.empty() || !embedding || !extent
        || !childByLocal(staffStateNode, "collection-rotation")
        || !childByLocal(staffStateNode, "mode-rotation")
        || !childByLocal(staffStateNode, "generator-cents")
        || !childByLocal(staffStateNode, "period-cents")) {
        m_error = u"melo:staff-state in extMeta is missing a required child";
        return false;
    }
    bool okG = false, okP = false;
    const std::string gen = jsonNumber(childByLocal(staffStateNode, "generator-cents").text().as_string(), okG);
    const std::string per = jsonNumber(childByLocal(staffStateNode, "period-cents").text().as_string(), okP);
    if (!okG || !okP) {
        m_error = u"melo:staff-state cents fields are not numbers";
        return false;
    }
    std::string referenceJson = "\"none\"";
    if (reference) {
        pugi::xml_node form = reference.first_child();
        const std::string kind = localName(form);
        if (kind == "none") {
            referenceJson = "\"none\"";
        } else if (kind == "reference-pitch") {
            referenceJson = "{\"reference-pitch\":{\"key_number\":" + std::string(form.attribute("key-number").value()) + "}}";
        } else if (kind == "pitch-class") {
            referenceJson = "{\"pitch-class\":{\"pitch_class\":" + std::string(form.text().as_string()) + "}}";
        } else if (kind == "frequency-hz") {
            referenceJson = "{\"frequency-hz\":{\"hertz\":" + std::string(form.text().as_string()) + "}}";
        } else {
            m_error = String(u"unknown jims:reference form '%1'").arg(String::fromStdString(kind));
            return false;
        }
    }
    std::string scaleJson = "[";
    for (size_t i = 0; i < steps.size(); ++i) {
        if (i) {
            scaleJson += ",";
        }
        scaleJson += "\"" + steps.at(i) + "\"";
    }
    scaleJson += "]";
    std::string out = "{\"scale\":" + scaleJson
                      + ",\"collection_rotation\":" + std::string(childByLocal(staffStateNode, "collection-rotation").text().as_string())
                      + ",\"mode_rotation\":" + std::string(childByLocal(staffStateNode, "mode-rotation").text().as_string())
                      + ",\"generator_cents\":" + gen
                      + ",\"period_cents\":" + per
                      + ",\"embedding\":{\"large_steps\":" + std::string(embedding.attribute("large-steps").value())
                      + ",\"small_steps\":" + std::string(embedding.attribute("small-steps").value()) + "}"
                      + ",\"extent\":{\"lower\":{\"nPer\":" + std::string(extent.attribute("lower-n-per").value())
                      + ",\"nGen\":" + std::string(extent.attribute("lower-n-gen").value())
                      + "},\"upper\":{\"nPer\":" + std::string(extent.attribute("upper-n-per").value())
                      + ",\"nGen\":" + std::string(extent.attribute("upper-n-gen").value()) + "}}"
                      + ",\"reference\":" + referenceJson;
    if (ambit) {
        out += ",\"tonic_ambit\":\"" + std::string(ambit.text().as_string()) + "\"";
    }
    out += "}";
    json = String::fromStdString(out);
    return true;
}

bool MeloMeiImporter::apply(Score* score,
                            const std::function<Note* (const std::string&)>& noteForId,
                            const std::function<int(int)>& staffIndexForN)
{
    if (!present()) {
        return true;
    }
    if (!melo::available()) {
        m_error = mu::engraving::melo::diagnostic::meiImportBridgeUnavailable;
        return false;
    }
    pugi::xml_node mx;
    for (pugi::xml_node child : m_record.children()) {
        if (localName(child) == "musicxml") {
            mx = child;
        }
    }
    if (!mx) {
        m_error = mu::engraving::melo::diagnostic::meiImportMissingMusicXml;
        return false;
    }

    static const StaffType* meloPreset = StaffType::preset(StaffTypes::MELO_12TET);
    UNUSED(meloPreset);

    // Measure index -> Measure*
    std::vector<Measure*> measures;
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        measures.push_back(m);
    }
    auto tickOf = [&](pugi::xml_node node, Fraction& tick) -> bool {
        const int midx = node.attribute("measure").as_int() - 1;
        if (midx < 0 || midx >= int(measures.size())) {
            m_error = mu::engraving::melo::diagnostic::meiImportMeasureUnknown;
            return false;
        }
        std::string off = node.attribute("off").value();
        const size_t slash = off.find('/');
        if (slash == std::string::npos) {
            m_error = mu::engraving::melo::diagnostic::meiImportOffsetInvalid;
            return false;
        }
        const Fraction quarters(std::stoi(off.substr(0, slash)), std::stoi(off.substr(slash + 1)));
        tick = measures.at(midx)->tick() + (quarters / Fraction(4, 1)).reduced();
        return true;
    };

    bool anyState = false;
    for (pugi::xml_node pe : mx.children()) {
        if (localName(pe) != "part") {
            continue;
        }
        const std::string ref = std::string(pe.attribute("ref").value()).substr(1);
        auto nIt = m_staffDefN.find(ref);
        if (nIt == m_staffDefN.end()) {
            m_error = mu::engraving::melo::diagnostic::meiImportStaffDefinitionUnknown;
            return false;
        }
        const int staffIdx = staffIndexForN(nIt->second);
        if (staffIdx < 0 || staffIdx >= int(score->nstaves())) {
            m_error = mu::engraving::melo::diagnostic::meiImportStaffUnknown;
            return false;
        }
        Staff* staff = score->staff(staffIdx);
        bool first = true;
        Fraction lastTick(-1, 1);
        for (pugi::xml_node se : pe.children()) {
            const std::string local = localName(se);
            if (local == "state") {
                pugi::xml_node stateNode = childByLocal(se, "staff-state");
                String json;
                if (!stateNode || !stateJsonFromXml(stateNode, json)) {
                    if (m_error.isEmpty()) {
                        m_error = mu::engraving::melo::diagnostic::meiImportStateMissing;
                    }
                    return false;
                }
                String kernelError;
                if (!melo::validateState(json, kernelError)) {
                    m_error = String(mu::engraving::melo::diagnostic::meiImportStateRejected).arg(kernelError);
                    return false;
                }
                Fraction tick;
                if (!tickOf(se, tick)) {
                    return false;
                }
                StaffType st = *StaffType::preset(StaffTypes::MELO_12TET);
                st.setMelo(true);
                st.setMeloJiLines(true);
                st.setMeloStateJson(json);
                if (first) {
                    if (!tick.isZero()) {
                        m_error = mu::engraving::melo::diagnostic::meiImportFirstStateNotAtStart;
                        return false;
                    }
                    staff->setStaffType(Fraction(0, 1), st);
                    first = false;
                } else {
                    if (tick <= lastTick) {
                        m_error = mu::engraving::melo::diagnostic::meiImportStatesOutOfOrder;
                        return false;
                    }
                    Measure* measure = score->tick2measure(tick);
                    if (!measure || tick < measure->tick() || tick >= measure->endTick()) {
                        m_error = mu::engraving::melo::diagnostic::meiImportStateOutsideMeasure;
                        return false;
                    }
                    const Fraction rtick = tick - measure->tick();
                    if (!measure->canAddStaffTypeChange(staff->idx(), rtick)) {
                        m_error = mu::engraving::melo::diagnostic::meiImportStatePlacementFailed;
                        return false;
                    }
                    StaffTypeChange* stc = Factory::createStaffTypeChange(measure);
                    stc->setTrack(staff->idx() * VOICES);
                    stc->setParent(measure);
                    stc->setRtick(rtick);
                    stc->setStaffType(new StaffType(st), true);
                    if (rtick.isNotZero() && !measure->findSegmentR(Segment::CHORD_REST_OR_TIME_TICK_TYPE, rtick)) {
                        measure->getSegmentR(SegmentType::TimeTick, rtick);
                    }
                    measure->add(stc);
                }
                lastTick = tick;
                anyState = true;
            } else if (local == "notes") {
                for (pugi::xml_node ne : se.children()) {
                    if (localName(ne) != "note") {
                        continue;
                    }
                    const std::string noteRef = std::string(ne.attribute("ref").value()).substr(1);
                    Note* note = noteForId(noteRef);
                    pugi::xml_node px = childByLocal(ne, "pitch");
                    if (!note || !px) {
                        m_error = mu::engraving::melo::diagnostic::meiImportNoteIdentityUnresolved;
                        return false;
                    }
                    note->setMeloPitch(px.attribute("n-per").as_int(), px.attribute("n-gen").as_int());
                }
            } else if (local == "trajectory") {
                Fraction tick;
                if (!tickOf(se, tick)) {
                    return false;
                }
                pugi::xml_node tt = childByLocal(se, "tuning-trajectory");
                if (!tt) {
                    continue;
                }
                melo::TuningTrajectory trajectory;
                trajectory.tick = tick;
                for (pugi::xml_node sege : tt.children()) {
                    if (localName(sege) != "segment") {
                        continue;
                    }
                    melo::TrajectorySegment seg;
                    // duration-divisions in the MeloPresto MEI canonical basis of
                    // 960 divisions per quarter note.
                    const int div = sege.attribute("duration-divisions").as_int();
                    seg.duration = Fraction(div, 960 * 4).reduced();
                    seg.startCents = String(sege.attribute("start-cents").value());
                    seg.endCents = String(sege.attribute("end-cents").value());
                    seg.interpolation = String(sege.attribute("interpolation").value());
                    for (pugi::xml_node ce : sege.children()) {
                        if (localName(ce) != "control") {
                            continue;
                        }
                        melo::TrajectoryControl c;
                        c.time = String(ce.attribute("time").value());
                        c.valueCents = String(ce.attribute("value-cents").value());
                        seg.controls.push_back(c);
                    }
                    trajectory.segments.push_back(seg);
                }
                staff->addMeloTuningTrajectory(trajectory);
            }
        }
    }

    // Evidentiary review record: the jm payload supplies the exact fields,
    // the native annotations supply category, prose, agent, and timing.
    pugi::xml_node rv;
    for (pugi::xml_node child : m_record.children()) {
        if (localName(child) == "review") {
            rv = child;
        }
    }
    if (rv) {
        melo::ReviewRecord review;
        review.schema = String(rv.attribute("schema").value());
        review.reviewAgent = m_reviewAgent;
        review.focusedReviewReasons = m_focusedReviewReasons;
        for (pugi::xml_node child : rv.children()) {
            const std::string tag = localName(child);
            if (tag == "work") {
                for (pugi::xml_node v : child.children()) {
                    review.work = readReviewValue(v);
                }
            } else if (tag == "audit") {
                melo::ReviewAudit a;
                const std::string cid = std::string(child.attribute("change").value()).substr(1);
                a.changeId = String::fromStdString(cid);
                auto cit = m_changeById.find(cid);
                if (cit != m_changeById.end()) {
                    a.date = cit->second.date;
                    a.phase = cit->second.phase;
                    a.reason = cit->second.reason;
                }
                for (pugi::xml_node v : child.children()) {
                    a.record = readReviewValue(v);
                }
                review.audits.push_back(a);
            } else if (tag == "adjudication") {
                melo::ReviewAdjudication adj;
                for (pugi::xml_node v : child.children()) {
                    adj.record = readReviewValue(v);
                }
                if (std::string(child.attribute("stale").value()) == "true") {
                    // a stale record round-trips as stale; it is never
                    // re-anchored to an arbitrary position
                    adj.annotId = String(child.attribute("id").value());
                    adj.tick = Fraction::fromString(String(child.attribute("tick").value()));
                    review.adjudications.push_back(adj);
                    continue;
                }
                const std::string aid = std::string(child.attribute("annot").value()).substr(1);
                auto ait = m_adjAnnots.find(aid);
                if (ait == m_adjAnnots.end()) {
                    m_error = mu::engraving::melo::diagnostic::meiImportAdjudicationUnresolved;
                    return false;
                }
                pugi::xml_node annot = ait->second;
                adj.annotId = String::fromStdString(aid);
                const std::string cls = annot.attribute("class").value();
                const size_t dot = cls.rfind('.');
                adj.outcome = String::fromStdString(dot == std::string::npos ? cls : cls.substr(dot + 1));
                auto rit = m_reviewerById.find(std::string(annot.attribute("resp").value()).substr(1));
                if (rit != m_reviewerById.end()) {
                    adj.reviewer = rit->second;
                }
                for (pugi::xml_node p : annot.children("p")) {
                    if (p.child("ptr")) {
                        for (pugi::xml_node ptr : p.children("ptr")) {
                            const std::string type = ptr.attribute("type").value();
                            if (type == "melo-evidence") {
                                adj.evidence.push_back(String(ptr.attribute("target").value()));
                            } else if (type == "melo-source-analysis") {
                                adj.sourceAnalysis = String(ptr.attribute("target").value());
                            }
                        }
                    } else {
                        adj.notes.push_back(String(p.text().as_string()));
                    }
                }
                auto mit = m_adjMeasureIndex.find(aid);
                if (mit != m_adjMeasureIndex.end() && mit->second < int(measures.size())) {
                    Measure* measure = measures.at(mit->second);
                    const double tstamp = annot.attribute("tstamp").as_double(1.0);
                    const Fraction timesig = measure->timesig();
                    const Fraction off = Fraction::fromTicks(
                        int(std::lround((tstamp - 1.0) * timesig.denominator() > 0
                                        ? (tstamp - 1.0) / timesig.denominator() * 4.0 * Constants::DIVISION
                                        : 0.0)));
                    adj.tick = measure->tick() + off;
                }
                review.adjudications.push_back(adj);
            }
        }
        score->setMeloReview(review);
    }

    // Melody-part designation (typed native annotation).
    if (!m_melodyToken.isEmpty()) {
        melo::MelodyPart melodyPart = melo::MelodyPart::Soprano;
        if (melo::melodyPartFromToken(m_melodyToken, melodyPart)) {
            score->setMeloMelodyPart(melodyPart);
        }
    }

    // Provenance: native sources plus the MeloPresto-constrained supplement.
    pugi::xml_node ss = childByLocal(mx, "source-supplement");
    if (!m_provResources.empty() || ss) {
        melo::Provenance prov;
        prov.resources = m_provResources;
        if (ss) {
            prov.strictFallback = std::string(ss.attribute("strict").value()) == "true";
            int i = 0;
            for (pugi::xml_node se : ss.children()) {
                if (localName(se) != "source") {
                    continue;
                }
                if (i < int(prov.resources.size())) {
                    prov.resources.at(i).role = String(se.attribute("role").value());
                    prov.resources.at(i).text = String(se.text().as_string());
                }
                ++i;
            }
        }
        score->setMeloProvenance(prov);
    }

    if (anyState) {
        score->style().set(Sid::musicalSymbolFont, String(u"JiMSMusic"));
        score->style().set(Sid::hideInstrumentNameIfOneInstrument, false);
    }
    return true;
}
} // namespace mu::iex::mei

namespace mu::iex::mei {
bool MeloMeiImporter::onHarm(const std::string& xmlId, Harmony* harmony)
{
    auto events = childByLocal(childByLocal(m_record, "musicxml"), "harmony");
    bool found = false;
    for (auto event : events.children()) {
        if (std::string(event.attribute("harm").value()) != "#" + xmlId) {
            continue;
        }
        auto proof = childByLocal(event, "chord-evidence");
        if (!proof) {
            continue;
        }
        for (auto sibling = proof.next_sibling(); sibling; sibling = sibling.next_sibling()) {
            if (localName(sibling) == "chord-evidence") {
                m_error = u"Duplicate chord evidence carrier";
                return false;
            }
        }
        const String origin = String::fromUtf8(proof.attribute("origin").value());
        harmony->setMeloEvidence(String::fromUtf8(proof.text().get()), origin == u"manual");
        if (found || (origin != u"manual" && origin != u"generated") || harmony->meloEvidence().empty()
            || !harmony->meloEvidenceError(false).empty()
            || (origin == u"generated" && harmony->meloEvidenceOrigin() != u"generated")) {
            m_error = u"Invalid or unbound generated chord evidence";
            return false;
        }
        found = true;
    }
    return true;
}
} // namespace mu::iex::mei
