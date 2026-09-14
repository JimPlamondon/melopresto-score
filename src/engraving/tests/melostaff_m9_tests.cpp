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

// JiMStaff Milestone 9 — SATB open score (owner decisions 2026-08-22).
//
// Three things are under test here.
//
// 1. The shipped "SATB (MeloPresto Staff)" Choral template
//    (share/templates/02-Choral/12-SATB_(MeloPresto_Staff)): four stock vocal Parts in
//    open score, one MeloPresto Staff each, following MuseScore's own choral
//    conventions, registered in all three template-registration files.
// 2. Empty-staff defaults. The Kernel derives each frame from the Part's
//    declared amateur range; Bass alone uses the tonic-anchored exception.
//    Framing is tonic-relative throughout.
// 3. Owner decision 2a: a key/mode/scale change applied anywhere in a
//    multi-part MeloPresto score reaches every MeloPresto part at the same measure as ONE
//    undo step, and a refusal anywhere mutates nothing.
//
// Every musical answer still comes from the Kernel. These tests assert what
// the fork transported and drew, never a musical fact the fork computed.

#include <gtest/gtest.h>

#include <fstream>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "engraving/dom/bracketItem.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/clef.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/lyrics.h"
#include "engraving/dom/system.h"
#include "engraving/style/style.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/score.h"
#include "engraving/dom/scoreorder.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftype.h"
#include "engraving/dom/stafftypechange.h"
#include "engraving/editing/undo.h"
#include "engraving/melo/melobridge.h"
#include "engraving/melo/melochange.h"
#include "engraving/melo/melochangecontroller.h"

#include "utils/scorerw.h"

using namespace mu::engraving;

namespace {
// The fork root, derived from the test data root, so the tests read the
// SHIPPED template rather than a second hand-maintained copy of it.
muse::String forkRoot()
{
    return ScoreRW::rootPath() + u"/../../..";
}

muse::String satbTemplateDir()
{
    return forkRoot() + u"/share/templates/02-Choral/12-SATB_(MeloPresto_Staff)";
}

muse::String satbTemplatePath()
{
    return satbTemplateDir() + u"/12-SATB_(MeloPresto_Staff).mscx";
}

std::string readFile(const muse::String& path)
{
    std::ifstream in(path.toStdString(), std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool fileExists(const muse::String& path)
{
    std::ifstream in(path.toStdString(), std::ios::binary);
    return in.good();
}

MasterScore* openShippedTemplate()
{
    return ScoreRW::readScore(satbTemplatePath(), true);
}

Measure* measureNo(Score* score, int n)
{
    Measure* m = score->firstMeasure();
    for (int i = 1; m && i < n; ++i) {
        m = m->nextMeasure();
    }
    return m;
}

const StaffType* typeAt(const Score* score, staff_idx_t staffIdx, const Measure* m)
{
    return score->staff(staffIdx)->staffType(m->tick());
}

muse::String stateAt(const Score* score, staff_idx_t staffIdx, const Measure* m)
{
    const StaffType* st = typeAt(score, staffIdx, m);
    return st ? st->meloStateJson() : muse::String();
}

// The four voices' states must agree in every field except the one the
// range-derived default gives each voice its own value of. Comparing states for
// SAMENESS is a template contract check; it classifies no musical field.
muse::String withoutExtent(const muse::String& json)
{
    const size_t at = json.indexOf(u"\"extent\"");
    if (at == muse::nidx) {
        return json;
    }
    const size_t begin = json.indexOf(u'{', at);
    if (begin == muse::nidx) {
        return json;
    }
    size_t end = begin;
    int depth = 0;
    for (; end < json.size(); ++end) {
        if (json.at(end) == u'{') {
            ++depth;
        } else if (json.at(end) == u'}' && --depth == 0) {
            break;
        }
    }
    if (end >= json.size()) {
        return json;
    }
    muse::String out = json;
    if (end + 1 < out.size() && out.at(end + 1) == u',') {
        out.remove(at, end + 2 - at);
    } else if (at > 0 && out.at(at - 1) == u',') {
        out.remove(at - 1, end + 2 - at);
    } else {
        out.remove(at, end + 1 - at);
    }
    return out;
}

// Tonic ambit is one song-wide value. This helper remains for historical
// assertions that intentionally compare only the other song-wide fields.
muse::String withoutAmbit(const muse::String& json)
{
    muse::String out = json;
    for (const char16_t* tok : { u",\"tonic_ambit\":\"tonic-bounded\"", u",\"tonic_ambit\":\"tonic-centered\"" }) {
        out.replace(muse::String(tok), muse::String());
    }
    return out;
}

size_t undoDepth(const Score* score)
{
    return score->undoStack()->currentIndex();
}

// The phrases this milestone may not introduce, assembled at run time so this
// checker is not itself a hit. Framing is tonic-relative; those spellings name
// the Do-mode specialisation as though it were the general rule.
std::vector<std::string> forbiddenFramingPhrases()
{
    const std::string doUpper = "D";
    const std::string doLower = "d";
    std::vector<std::string> out;
    for (const std::string& lead : { doUpper + "o-", doLower + "o-" }) {
        out.push_back(lead + "anchored");
        out.push_back(lead + "bounded");
    }
    return out;
}

std::vector<Note*> notesOn(Score* score, staff_idx_t staffIdx)
{
    std::vector<Note*> out;
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            for (voice_idx_t v = 0; v < VOICES; ++v) {
                EngravingItem* e = s->element(staffIdx * VOICES + v);
                if (e && e->isChord()) {
                    for (Note* n : toChord(e)->notes()) {
                        out.push_back(n);
                    }
                }
            }
        }
    }
    return out;
}
}

// ---------------------------------------------------------------------------
// Template structure — MuseScore's own SATB conventions, on MeloPresto staves
// ---------------------------------------------------------------------------

TEST(Engraving_MeloStaffM9SATBTests, m9TemplateIsShippedAndOpensAsFourVocalPartsInOpenScore)
{
    ASSERT_TRUE(fileExists(satbTemplatePath()))
        << "the SATB (MeloPresto Staff) template is not shipped at " << satbTemplatePath().toStdString();
    MasterScore* score = openShippedTemplate();
    ASSERT_TRUE(score);
    score->doLayout();

    ASSERT_EQ(score->parts().size(), 4u);
    ASSERT_EQ(score->nstaves(), 4u);

    const char16_t* ids[4] = { u"soprano", u"alto", u"tenor", u"bass" };
    const char16_t* musicXmlIds[4] = { u"voice.soprano", u"voice.alto", u"voice.tenor", u"voice.bass" };
    for (size_t i = 0; i < 4; ++i) {
        const Part* p = score->parts()[i];
        EXPECT_EQ(p->nstaves(), 1u) << "part " << i << " must be one staff (open score)";
        const Instrument* instr = p->instrument();
        ASSERT_TRUE(instr);
        EXPECT_EQ(instr->id(), muse::String(ids[i]));
        EXPECT_EQ(instr->musicXmlId(), muse::String(musicXmlIds[i]));
        EXPECT_TRUE(instr->isVocalInstrument()) << "part " << i << " must be family 'voices'";
    }

    EXPECT_EQ(score->scoreOrder().id, muse::String(u"choir"));

    delete score;
}

TEST(Engraving_MeloStaffM9SATBTests, m9TemplateBracketsAndBarlinesFollowTheOctavoConvention)
{
    MasterScore* score = openShippedTemplate();
    ASSERT_TRUE(score);
    score->doLayout();
    ASSERT_EQ(score->nstaves(), 4u);

    // One NORMAL bracket at column 0 spanning all four staves, declared on
    // staff 1 only — MuseScore's own choral shape.
    const Staff* s0 = score->staff(0);
    size_t declared = 0;
    for (const BracketItem* b : s0->brackets()) {
        if (b && b->bracketType() != BracketType::NO_BRACKET) {
            ++declared;
            EXPECT_EQ(b->bracketType(), BracketType::NORMAL);
            EXPECT_EQ(b->bracketSpan(), 4u);
            EXPECT_EQ(b->column(), 0u);
        }
    }
    EXPECT_EQ(declared, 1u) << "staff 1 must declare exactly one bracket";
    for (staff_idx_t i = 1; i < 4; ++i) {
        for (const BracketItem* b : score->staff(i)->brackets()) {
            EXPECT_TRUE(!b || b->bracketType() == BracketType::NO_BRACKET)
                << "staff " << i << " must declare no bracket";
        }
    }

    // Barlines do not span between vocal staves.
    for (staff_idx_t i = 0; i < 4; ++i) {
        EXPECT_FALSE(score->staff(i)->barLineSpan()) << "staff " << i << " must not span its barline";
    }

    delete score;
}

TEST(Engraving_MeloStaffM9SATBTests, m9TemplateFileCarriesNoStyleBlockAndVoiceOneOnly)
{
    const std::string mscx = readFile(satbTemplatePath());
    ASSERT_FALSE(mscx.empty()) << "the shipped template could not be read";
    EXPECT_EQ(mscx.find("<Style>"), std::string::npos) << "the .mscx must carry no <Style> block";
    // The stock clef declarations stay, even though a MeloPresto Staff draws none.
    EXPECT_NE(mscx.find("<defaultClef>G8vb</defaultClef>"), std::string::npos);
    EXPECT_NE(mscx.find("<defaultClef>F</defaultClef>"), std::string::npos);
    EXPECT_NE(mscx.find("<clef>G8vb</clef>"), std::string::npos);
    EXPECT_NE(mscx.find("<clef>F</clef>"), std::string::npos);

    MasterScore* score = openShippedTemplate();
    ASSERT_TRUE(score);
    score->doLayout();
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            for (staff_idx_t st = 0; st < score->nstaves(); ++st) {
                for (voice_idx_t v = 1; v < VOICES; ++v) {
                    EXPECT_FALSE(s->element(st * VOICES + v))
                        << "measure " << m->no() + 1 << " staff " << st << " uses voice " << v + 1;
                }
            }
        }
    }
    delete score;
}

TEST(Engraving_MeloStaffM9SATBTests, m9TemplateIsRegisteredInAllThreeRegistrationFilesWithItsPackageAssets)
{
    const muse::String rel(u"02-Choral/12-SATB_(MeloPresto_Staff)/12-SATB_(MeloPresto_Staff).mscx");

    const std::string categories = readFile(forkRoot() + u"/share/templates/categories.json");
    ASSERT_FALSE(categories.empty());
    EXPECT_NE(categories.find(rel.toStdString()), std::string::npos)
        << "categories.json is what makes the template discoverable in the wizard";

    const std::string cmake = readFile(forkRoot() + u"/share/templates/CMakeLists.txt");
    ASSERT_FALSE(cmake.empty());
    EXPECT_NE(cmake.find("02-Choral/12-SATB_\\(MeloPresto_Staff\\)"), std::string::npos)
        << "CMakeLists.txt must install the package, with the parenthesis escaping the Barbershop entries use";

    const std::string convert = readFile(forkRoot() + u"/share/templates/convert.json");
    ASSERT_FALSE(convert.empty());
    EXPECT_NE(convert.find("02-Choral/12-SATB_(MeloPresto_Staff).mscx"), std::string::npos)
        << "convert.json uses the flattened path convention";

    // The package assets the current format requires, matching the stock
    // choral packages.
    for (const char16_t* asset : { u"/META-INF/container.xml", u"/score_style.mss", u"/audiosettings.json",
                                   u"/viewsettings.json", u"/Thumbnails/thumbnail.png" }) {
        EXPECT_TRUE(fileExists(satbTemplateDir() + muse::String(asset)))
            << "missing package asset " << muse::String(asset).toStdString();
    }
    const std::string container = readFile(satbTemplateDir() + u"/META-INF/container.xml");
    EXPECT_NE(container.find("12-SATB_(MeloPresto_Staff).mscx"), std::string::npos);
}

// ---------------------------------------------------------------------------
// The empty-staff singer-range defaults
// ---------------------------------------------------------------------------

TEST(Engraving_MeloStaffM9SATBTests, m9EveryStaffIsAMeloStaffCarryingItsRangeDerivedDefault)
{
    MasterScore* score = openShippedTemplate();
    ASSERT_TRUE(score);
    score->doLayout();
    ASSERT_EQ(score->nstaves(), 4u);

    for (staff_idx_t i = 0; i < 4; ++i) {
        const StaffType* st = score->staff(i)->staffType(Fraction(0, 1));
        ASSERT_TRUE(st) << "staff " << i;
        EXPECT_TRUE(st->isMelo()) << "staff " << i << " must be a MeloPresto Staff";
        EXPECT_EQ(st->xmlName(), muse::String(u"jims12tet")) << "staff " << i;
        // The modern key. The legacy `tonic_extent` spelling must not appear
        // in a template authored today.
        EXPECT_TRUE(st->meloStateJson().contains(u"\"tonic_ambit\":\"tonic-bounded\"")) << "staff " << i;
        EXPECT_FALSE(st->meloStateJson().contains(u"tonic_extent")) << "staff " << i;
    }

    // Identical in every field except the per-staff extent.
    // voice its own value of.
    const muse::String s0 = score->staff(0)->staffType(Fraction(0, 1))->meloStateJson();
    for (staff_idx_t i = 1; i < 4; ++i) {
        const muse::String si = score->staff(i)->staffType(Fraction(0, 1))->meloStateJson();
        EXPECT_EQ(withoutExtent(withoutAmbit(si)), withoutExtent(withoutAmbit(s0)))
            << "staff " << i << " differs from the Soprano outside its frame extent";
    }
    EXPECT_NE(score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(),
              score->staff(3)->staffType(Fraction(0, 1))->meloStateJson())
        << "the Soprano and Bass frames must differ";

    delete score;
}

TEST(Engraving_MeloStaffM9SATBTests, m9EmptyFramesKeepTheirDeclaredAnchorsWithHalfPeriodMinimum)
{
    MasterScore* score = openShippedTemplate();
    ASSERT_TRUE(score);
    score->doLayout();
    ASSERT_EQ(score->nstaves(), 4u);
    const int centreNPer[4] = { 0, -1, -2, -1 };
    const int centreNGen[4] = { 1, 2, 3, 0 };
    for (staff_idx_t i = 0; i < 4; ++i) {
        const StaffType* st = score->staff(i)->staffType(Fraction(0, 1));
        ASSERT_TRUE(st && st->isMelo());
        double centre = 0.0;
        ASSERT_TRUE(melo::noteCentsAboveExtentLower(st->meloStateJson(), centreNPer[i], centreNGen[i], centre));
        const StaffType::MeloFrameView& view = st->meloWholeFrameView(score, i);
        ASSERT_EQ(view.bands.size(), 1u);
        EXPECT_NEAR(centre, 0.0, 1e-6);
        EXPECT_GE(view.topCents() - view.bottomCents(), st->meloPeriodCents() / 2.0 - 1e-6);
        EXPECT_TRUE(st->meloExtentIsEmptyDefault());
    }
    delete score;
}

TEST(Engraving_MeloStaffM9SATBTests, m9WrittenStavesUseTheirOwnMelodyFrameWhileUnwrittenStavesKeepTheDefault)
{
    MasterScore* score = ScoreRW::readScore(u"jimstaff_data/m9-satb-mixed.mscx");
    ASSERT_TRUE(score);
    score->doLayout();
    ASSERT_EQ(score->nstaves(), 4u);

    // Staff 0 carries a melody that reaches outside its declared one-period
    // frame; staves 1..3 are empty and keep their range-derived defaults.
    ASSERT_FALSE(notesOn(score, 0).empty());
    const StaffType* written = score->staff(0)->staffType(Fraction(0, 1));
    const StaffType::MeloFrameView& wv = written->meloWholeFrameView(score, 0);
    EXPECT_GT(wv.bands.size() ? (wv.bands.back().upperCents - wv.bands.front().lowerCents) : 0.0, 1200.0)
        << "a written staff must use its melody-derived frame";

    for (staff_idx_t i = 1; i < 4; ++i) {
        EXPECT_TRUE(notesOn(score, i).empty()) << "staff " << i << " is meant to be unwritten";
        const StaffType* st = score->staff(i)->staffType(Fraction(0, 1));
        const StaffType::MeloFrameView& v = st->meloWholeFrameView(score, i);
        ASSERT_EQ(v.bands.size(), 1u) << "staff " << i;
        EXPECT_GE(v.bands[0].upperCents - v.bands[0].lowerCents, st->meloPeriodCents() / 2.0 - 1e-6)
            << "unwritten staff " << i << " must keep its half-period range default";
    }

    delete score;
}

TEST(Engraving_MeloStaffM9SATBTests, m9IntroducesNoDoAnchoredFramingLanguage)
{
    // Framing is tonic-relative. Do is only the Do-mode specialisation, and no
    // artifact this milestone introduces may say otherwise.
    const muse::String files[] = {
        muse::String(ScoreRW::rootPath()) + u"/melostaff_m9_tests.cpp",
        muse::String(ScoreRW::rootPath()) + u"/jimstaff_data/m9-satb-mixed.mscx",
        muse::String(ScoreRW::rootPath()) + u"/jimstaff_data/m9-satb-blocked.mscx",
        muse::String(ScoreRW::rootPath()) + u"/jimstaff_data/m9-dense-voices.mscx",
        forkRoot() + u"/src/engraving/melo/melochangecontroller.h",
        forkRoot() + u"/src/engraving/melo/melochangecontroller.cpp",
        forkRoot() + u"/src/inspector/qml/MuseScore/Inspector/melostaffsettingsmodel.cpp",
        forkRoot() + u"/src/inspector/qml/MuseScore/Inspector/meloscoresettingsmodel.cpp",
        forkRoot() + u"/src/inspector/qml/MuseScore/Inspector/melotuningmodel.cpp",
        forkRoot() + u"/src/inspector/qml/MuseScore/Inspector/MeloStaffSettings.qml",
        forkRoot() + u"/src/inspector/qml/MuseScore/Inspector/MeloScoreSettings.qml",
        forkRoot() + u"/src/inspector/qml/MuseScore/Inspector/MeloTuningControl.qml",
        satbTemplatePath(),
    };
    for (const muse::String& f : files) {
        const std::string text = readFile(f);
        if (text.empty()) {
            continue;
        }
        for (const std::string& needle : forbiddenFramingPhrases()) {
            EXPECT_EQ(text.find(needle), std::string::npos)
                << f.toStdString() << " states \"" << needle << "\" as the framing rule";
        }
    }
}

// ---------------------------------------------------------------------------
// Owner decision 2a — one atomic, score-wide key/mode/scale change
// ---------------------------------------------------------------------------

TEST(Engraving_MeloStaffM9SATBTests, m9ModeChangeReachesEveryMeloPartAtTheSameMeasureInOneUndoStep)
{
    MasterScore* score = openShippedTemplate();
    ASSERT_TRUE(score);
    score->doLayout();
    Measure* m2 = measureNo(score, 2);
    ASSERT_TRUE(m2);

    muse::String before[4];
    for (staff_idx_t i = 0; i < 4; ++i) {
        before[i] = stateAt(score, i, m2);
        ASSERT_FALSE(before[i].empty());
    }
    const size_t depth = undoDepth(score);

    muse::String error;
    ASSERT_TRUE(melo::applyChangeToAllMeloParts(score, m2, { u"mode:1" }, error)) << error.toStdString();
    score->doLayout();

    for (staff_idx_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(stateAt(score, i, m2).contains(u"\"mode_rotation\":5"))
            << "staff " << i << " did not receive the mode change";
        // The change is carried at the measure only; tick 0 keeps its mode.
        EXPECT_TRUE(score->staff(i)->staffType(Fraction(0, 1))->meloStateJson().contains(u"\"mode_rotation\":0"))
            << "staff " << i << " base state was changed";
    }
    EXPECT_EQ(undoDepth(score), depth + 1) << "the whole multi-part change must be exactly one undo step";

    score->undoRedo(true, nullptr);
    score->doLayout();
    for (staff_idx_t i = 0; i < 4; ++i) {
        EXPECT_EQ(withoutAmbit(stateAt(score, i, m2)), withoutAmbit(before[i]))
            << "one undo must restore staff " << i;
    }
    score->undoRedo(false, nullptr);
    score->doLayout();
    for (staff_idx_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(stateAt(score, i, m2).contains(u"\"mode_rotation\":5"))
            << "one redo must reapply staff " << i;
    }

    delete score;
}

TEST(Engraving_MeloStaffM9SATBTests, m9KeyChangeReachesEveryMeloPartAtTheSameMeasureInOneUndoStep)
{
    MasterScore* score = openShippedTemplate();
    ASSERT_TRUE(score);
    score->doLayout();
    Measure* m2 = measureNo(score, 2);
    ASSERT_TRUE(m2);

    // A key change needs a bound reference. `bind:` stays staff-wide and
    // unchanged (owner decision 9), so it is applied per staff, as before.
    muse::String error;
    for (staff_idx_t i = 0; i < 4; ++i) {
        ASSERT_TRUE(melo::applyChange(score, i, m2, u"bind:reference-pitch:62", error)) << error.toStdString();
    }
    score->doLayout();
    const size_t depth = undoDepth(score);

    ASSERT_TRUE(melo::applyChangeToAllMeloParts(score, m2, { u"key:-1:3" }, error)) << error.toStdString();
    score->doLayout();
    for (staff_idx_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(stateAt(score, i, m2).contains(u"\"key_number\":53"))
            << "staff " << i << " did not receive the key change";
    }
    EXPECT_EQ(undoDepth(score), depth + 1);

    delete score;
}

// The tuning panel builds a scale entry as an ORDERED LIST of Kernel ids —
// from a non-diatonic collection, "Parallel Minor" is the diatonic cycle plus
// a rotation. Looping one-id applications made that two undo steps for one
// user gesture; the list-valued seam makes it one.
TEST(Engraving_MeloStaffM9SATBTests, m9MultiChoiceScaleChangeIsOneAtomicOperationAcrossEveryPart)
{
    MasterScore* score = openShippedTemplate();
    ASSERT_TRUE(score);
    score->doLayout();
    Measure* m2 = measureNo(score, 2);
    ASSERT_TRUE(m2);

    // Step off the diatonic collection first, exactly as a user would.
    muse::String error;
    ASSERT_TRUE(melo::applyChangeToAllMeloParts(score, m2, { u"scale:cycle:double-harmonic-minor" }, error))
        << error.toStdString();
    score->doLayout();

    // Now rebuild the panel's "Parallel Minor" entry from the Kernel's own
    // options for the state in force here: the cycle, then the rotation.
    melo::StateChangeOptions options;
    ASSERT_TRUE(melo::changeOptions(score, 0, m2, options));
    const melo::StateChangeOption* diatonic = nullptr;
    const melo::StateChangeOption* rotationMinus3 = nullptr;
    for (const melo::StateChangeOption& c : options.cycles) {
        if (c.id == u"scale:cycle:diatonic") {
            diatonic = &c;
        }
    }
    for (const melo::StateChangeOption& r : options.rotations) {
        if (r.id == u"scale:rotation:-3") {
            rotationMinus3 = &r;
        }
    }
    ASSERT_TRUE(diatonic) << "the Kernel must still offer the diatonic cycle";
    ASSERT_FALSE(diatonic->current) << "the fixture must be off the diatonic collection by now";
    std::vector<muse::String> steps = { diatonic->id };
    if (rotationMinus3) {
        steps.push_back(rotationMinus3->id);
    }
    ASSERT_GE(steps.size(), 2u) << "this test needs a genuinely multi-id scale gesture";

    const char* roles[4] = { "soprano", "alto", "tenor", "bass" };
    muse::String expected[4];
    for (staff_idx_t i = 0; i < 4; ++i) {
        muse::String cur = stateAt(score, i, m2);
        for (const muse::String& id : steps) {
            muse::String out, err;
            ASSERT_TRUE(melo::applyStateChange(cur, id, out, err)) << err.toStdString();
            cur = out;
        }
        const Instrument* instrument = score->staff(i)->part()->instrument();
        ASSERT_TRUE(melo::defaultVocalExtent(cur, instrument->minPitchA(), instrument->maxPitchA(), roles[i], expected[i]));
    }

    const size_t depth = undoDepth(score);
    ASSERT_TRUE(melo::applyChangeToAllMeloParts(score, m2, steps, error)) << error.toStdString();
    score->doLayout();

    EXPECT_EQ(undoDepth(score), depth + 1)
        << "a multi-choice scale application must contribute exactly one undo step";
    for (staff_idx_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(withoutAmbit(stateAt(score, i, m2)) == withoutAmbit(expected[i]))
            << "staff " << i << " is not the Kernel's answer for the whole step list";
    }

    // One undo takes the whole gesture back to the state before it.
    score->undoRedo(true, nullptr);
    score->doLayout();
    for (staff_idx_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(stateAt(score, i, m2).contains(u"double-harmonic")
                    || !stateAt(score, i, m2).contains(u"\"collection_rotation\":-3"))
            << "one undo must take staff " << i << " back one gesture, not one Kernel id";
    }

    delete score;
}

TEST(Engraving_MeloStaffM9SATBTests, m9PropagationStartsFromAnyVoiceAndKeepsEachPartsOwnKernelState)
{
    for (staff_idx_t origin = 0; origin < 4; ++origin) {
        MasterScore* score = openShippedTemplate();
        ASSERT_TRUE(score);
        score->doLayout();
        Measure* m2 = measureNo(score, 2);
        ASSERT_TRUE(m2);

        // Each target's own extent survives: the Kernel returns a complete
        // replacement state per target and the fork copies no part's state
        // onto another.
        const char* roles[4] = { "soprano", "alto", "tenor", "bass" };
        muse::String expected[4];
        for (staff_idx_t i = 0; i < 4; ++i) {
            muse::String err;
            muse::String changed;
            ASSERT_TRUE(melo::applyStateChange(stateAt(score, i, m2), u"mode:1", changed, err)) << err.toStdString();
            const Instrument* instrument = score->staff(i)->part()->instrument();
            ASSERT_TRUE(melo::defaultVocalExtent(changed, instrument->minPitchA(), instrument->maxPitchA(), roles[i], expected[i]));
        }

        muse::String error;
        ASSERT_TRUE(melo::applyChangeToAllMeloParts(score, m2, { u"mode:1" }, error)) << error.toStdString();
        score->doLayout();

        for (staff_idx_t i = 0; i < 4; ++i) {
            EXPECT_TRUE(withoutAmbit(stateAt(score, i, m2)) == withoutAmbit(expected[i]))
                << "origin " << origin << ", staff " << i << " is not the Kernel's own answer for that staff";
            EXPECT_TRUE(melo::changeCarrier(m2, i)) << "origin " << origin << ", staff " << i << " has no carrier";
        }
        // Soprano/Alto and Tenor/Bass keep their distinct frame heights.
        EXPECT_NE(stateAt(score, 0, m2), stateAt(score, 3, m2));

        delete score;
    }
}

TEST(Engraving_MeloStaffM9SATBTests, m9ARefusedTargetLeavesTheWholeScoreUntouched)
{
    MasterScore* score = ScoreRW::readScore(u"jimstaff_data/m9-satb-blocked.mscx");
    ASSERT_TRUE(score);
    score->doLayout();
    ASSERT_EQ(score->nstaves(), 4u);
    Measure* m2 = measureNo(score, 2);
    ASSERT_TRUE(m2);

    muse::String before[4];
    for (staff_idx_t i = 0; i < 4; ++i) {
        before[i] = stateAt(score, i, m2);
    }
    const size_t depth = undoDepth(score);

    // Staff 2 already carries a NON-MeloPresto staff type change at this measure.
    muse::String why;
    ASSERT_FALSE(melo::canInsertChange(score, 2, m2, why));

    muse::String error;
    EXPECT_FALSE(melo::applyChangeToAllMeloParts(score, m2, { u"mode:1" }, error));
    EXPECT_FALSE(error.empty()) << "a refusal must name its reason";
    score->doLayout();

    for (staff_idx_t i = 0; i < 4; ++i) {
        EXPECT_EQ(stateAt(score, i, m2), before[i]) << "staff " << i << " was mutated by a refused operation";
    }
    EXPECT_EQ(undoDepth(score), depth) << "a refused operation must add no undo step";

    delete score;
}

TEST(Engraving_MeloStaffM9SATBTests, m9StockPartsAreLeftUntouchedAndASinglePartScoreBehavesAsBefore)
{
    MasterScore* score = ScoreRW::readScore(u"jimstaff_data/m8-two-staves.mscx");
    ASSERT_TRUE(score);
    score->doLayout();
    ASSERT_GE(score->nstaves(), 2u);
    Measure* m2 = measureNo(score, 2);
    ASSERT_TRUE(m2);

    std::vector<staff_idx_t> meloStaves;
    std::vector<staff_idx_t> stockStaves;
    for (staff_idx_t i = 0; i < score->nstaves(); ++i) {
        const StaffType* st = score->staff(i)->staffType(m2->tick());
        (st && st->isMelo() ? meloStaves : stockStaves).push_back(i);
    }
    ASSERT_FALSE(meloStaves.empty());

    std::vector<muse::String> stockBefore;
    for (staff_idx_t i : stockStaves) {
        stockBefore.push_back(score->staff(i)->staffType(m2->tick())->name());
    }

    const size_t depth = undoDepth(score);
    muse::String error;
    ASSERT_TRUE(melo::applyChangeToAllMeloParts(score, m2, { u"mode:1" }, error)) << error.toStdString();
    score->doLayout();

    for (staff_idx_t i : meloStaves) {
        EXPECT_TRUE(stateAt(score, i, m2).contains(u"\"mode_rotation\":5")) << "MeloPresto staff " << i;
    }
    for (size_t k = 0; k < stockStaves.size(); ++k) {
        EXPECT_EQ(score->staff(stockStaves[k])->staffType(m2->tick())->name(), stockBefore[k])
            << "stock staff " << stockStaves[k] << " was touched";
    }
    EXPECT_EQ(undoDepth(score), depth + 1);

    delete score;
}

TEST(Engraving_MeloStaffM9SATBTests, m9BindStaysStaffWideAndIsNeverPropagatedAcrossParts)
{
    MasterScore* score = openShippedTemplate();
    ASSERT_TRUE(score);
    score->doLayout();
    Measure* m2 = measureNo(score, 2);
    ASSERT_TRUE(m2);

    muse::String othersBefore[4];
    for (staff_idx_t i = 0; i < 4; ++i) {
        othersBefore[i] = score->staff(i)->staffType(Fraction(0, 1))->meloStateJson();
        // The template states its KEY as well as its mode: Re0 pinned to D4,
        // so Do is C, and mode_rotation 0 makes Do the tonic. Nothing about
        // which pitch a MeloPresto note sounds is left to inference.
        EXPECT_TRUE(othersBefore[i].contains(u"\"key_number\":62")) << "staff " << i << " states no key";
        EXPECT_TRUE(othersBefore[i].contains(u"\"mode_rotation\":0")) << "staff " << i << " states no mode";
    }

    // A staff that already states its key keeps it: `bind:` binds an UNBOUND
    // state and leaves a bound one alone (M6 rule, unchanged by M9). Either
    // way it is applied to one staff and never reaches another part.
    muse::String error;
    ASSERT_TRUE(melo::applyChange(score, 0, m2, u"bind:reference-pitch:64", error)) << error.toStdString();
    score->doLayout();
    for (staff_idx_t i = 0; i < 4; ++i) {
        EXPECT_EQ(score->staff(i)->staffType(Fraction(0, 1))->meloStateJson(), othersBefore[i])
            << "a binding applied to the Soprano changed part " << i;
    }

    // And the score-wide seam refuses a binding outright rather than widening it.
    EXPECT_FALSE(melo::applyChangeToAllMeloParts(score, m2, { u"bind:reference-pitch:65" }, error));
    EXPECT_FALSE(error.empty());
    for (staff_idx_t i = 1; i < 4; ++i) {
        EXPECT_EQ(score->staff(i)->staffType(Fraction(0, 1))->meloStateJson(), othersBefore[i])
            << "a refused binding must mutate nothing, part " << i;
    }

    delete score;
}

// ---------------------------------------------------------------------------
// Note-head collisions in dense multi-voice writing (SATB rules §5a)
// ---------------------------------------------------------------------------

namespace {
// The two note heads at the FIRST chord position of `measure`, in voice order.
std::vector<Note*> collidingPair(Measure* measure)
{
    std::vector<Note*> out;
    Segment* s = measure->first(SegmentType::ChordRest);
    if (!s) {
        return out;
    }
    for (voice_idx_t v = 0; v < VOICES; ++v) {
        EngravingItem* e = s->element(v);
        if (e && e->isChord()) {
            for (Note* n : toChord(e)->notes()) {
                out.push_back(n);
            }
        }
    }
    return out;
}
}

// MuseScore_SATB_Rules.md §5a (owner ruling 2026-08-22). JiMS carries
// accidental identity in the note-head SHAPE, so two voices at a conventional
// unison may legitimately want different heads. When the shapes differ the
// heads must not be merged: one is placed across the stem from the other, the
// way MuseScore separates any unshareable unison.
TEST(Engraving_MeloStaffM9SATBTests, m9CollidingHeadsOfDifferentShapesAreOffsetAcrossTheStemAndNeverShared)
{
    MasterScore* score = ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(score);
    score->doLayout();

    Measure* m1 = measureNo(score, 1);
    ASSERT_TRUE(m1);
    std::vector<Note*> heads = collidingPair(m1);
    ASSERT_EQ(heads.size(), 2u) << "the different-shape fixture must present exactly two colliding heads";
    ASSERT_TRUE(heads[0]->hasMeloPitch() && heads[1]->hasMeloPitch());

    const StaffType* st = score->staff(0)->staffType(m1->tick());
    ASSERT_TRUE(st && st->isMelo());
    // They really do collide: the Kernel puts both at the same height.
    EXPECT_NEAR(heads[0]->meloCentsAboveDo(), heads[1]->meloCentsAboveDo(), 1e-6);
    // ...and the Kernel really does give them different shapes.
    muse::String tokA, tokB;
    ASSERT_TRUE(melo::noteheadToken(st->meloStateJson(), heads[0]->meloNGen(), tokA));
    ASSERT_TRUE(melo::noteheadToken(st->meloStateJson(), heads[1]->meloNGen(), tokB));
    ASSERT_NE(tokA, tokB) << "the fixture must give the two heads different Kernel shapes";

    EXPECT_TRUE(heads[0]->visible() && heads[1]->visible()) << "neither head may be hidden away";
    EXPECT_NE(heads[0]->pagePos().x(), heads[1]->pagePos().x())
        << "different-shaped colliding heads must be placed apart, not merged into one";

    delete score;
}

// The same position, the same lattice identity, therefore the same Kernel
// shape: stock behaviour, the heads may share. MuseScore's sharing decision is
// observable as the two chords being left at ONE x — no unison separation
// offset is applied to push them apart.
//
// Stock MuseScore right-aligns an UP-stem chord's heads to
// `Chord::noteHeadWidth()`, the score's nominal noteheadBlack advance, so a
// head narrower than that nominal used to keep a small x offset (5 to 13
// layout units) from its down-stem twin: the Milestone 9 follow-up. On a
// MeloPresto staff `Chord::noteHeadWidth()` is now the widest head actually
// drawn (fixed 2026-09-14), so two shared heads coincide exactly.
TEST(Engraving_MeloStaffM9SATBTests, m9CollidingHeadsOfIdenticalShapeMayShareOneHead)
{
    MasterScore* score = ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(score);
    score->doLayout();

    // Bar 2 is the half head, bar 3 the black head: the sharing decision must
    // be the same for both.
    for (int bar = 2; bar <= 3; ++bar) {
        Measure* m = measureNo(score, bar);
        ASSERT_TRUE(m) << "bar " << bar;
        std::vector<Note*> heads = collidingPair(m);
        ASSERT_EQ(heads.size(), 2u) << "bar " << bar << " must present exactly two colliding heads";
        ASSERT_EQ(heads[0]->meloNPer(), heads[1]->meloNPer()) << "bar " << bar;
        ASSERT_EQ(heads[0]->meloNGen(), heads[1]->meloNGen()) << "bar " << bar;

        const StaffType* st = score->staff(0)->staffType(m->tick());
        ASSERT_TRUE(st && st->isMelo());
        muse::String tokA, tokB;
        ASSERT_TRUE(melo::noteheadToken(st->meloStateJson(), heads[0]->meloNGen(), tokA));
        ASSERT_TRUE(melo::noteheadToken(st->meloStateJson(), heads[1]->meloNGen(), tokB));
        ASSERT_EQ(tokA, tokB) << "bar " << bar << ": the two heads must be the same Kernel shape";

        // The sharing decision itself: no separation offset, so the two chords
        // are left on one x. This is what MuseScore_SATB_Rules.md §5a rules on.
        EXPECT_EQ(heads[0]->chord()->pagePos().x(), heads[1]->chord()->pagePos().x())
            << "bar " << bar << ": identical shapes must be shared, not pushed apart";
        EXPECT_TRUE(heads[0]->visible() && heads[1]->visible()) << "bar " << bar;

        // The shared heads coincide: the up-stem chord aligns to the head it
        // actually draws, not to the nominal black-head advance.
        Note* upNote = heads[0]->chord()->up() ? heads[0] : heads[1];
        Note* downNote = heads[0]->chord()->up() ? heads[1] : heads[0];
        ASSERT_TRUE(upNote->chord()->up() && !downNote->chord()->up()) << "bar " << bar;
        const double gap = upNote->pagePos().x() - downNote->pagePos().x();
        EXPECT_NEAR(gap, 0.0, 1e-6)
            << "bar " << bar << ": two shared heads of one shape must sit on one x";
        EXPECT_LT(upNote->chord()->noteHeadWidth(), upNote->score()->noteHeadWidth() * upNote->chord()->mag())
            << "bar " << bar << ": the chord aligns to its drawn head, which is narrower than the nominal";
    }

    delete score;
}

// ---------------------------------------------------------------------------
// MuseScore_SATB_Rules.md §6 seam sweep — seams that were untested rather than
// known-broken. Each row's evidence is the test named here or, where the answer
// is only visible in a render, the M9 sensory bundle.
// ---------------------------------------------------------------------------

namespace {
const char16_t* HYMN = u"jimstaff_data/m9-satb-hymn.mscx";
}

// A MeloPresto Staff draws no clef and no key signature, while the stock clef entries
// stay in the Part definitions so a staff switched back to stdNormal renders
// correctly again.
TEST(Engraving_MeloStaffM9SATBTests, m9SweepNoClefIsDrawnWhileTheStockClefEntriesSurvive)
{
    MasterScore* score = openShippedTemplate();
    ASSERT_TRUE(score);
    score->doLayout();
    for (staff_idx_t i = 0; i < 4; ++i) {
        const StaffType* st = score->staff(i)->staffType(Fraction(0, 1));
        ASSERT_TRUE(st && st->isMelo()) << "staff " << i;
        EXPECT_FALSE(st->genClef()) << "staff " << i << " must draw no clef";
        EXPECT_FALSE(st->genKeysig()) << "staff " << i << " must draw no key signature";
    }
    // ...and the stock declarations are still on file (asserted on the shipped
    // .mscx by m9TemplateFileCarriesNoStyleBlockAndVoiceOneOnly).
    EXPECT_EQ(score->parts()[2]->instrument()->clefType(0).concertClef, ClefType::G8_VB);
    EXPECT_EQ(score->parts()[3]->instrument()->clefType(0).concertClef, ClefType::F);
    delete score;
}

// Lyrics attach to JiMS notes and sit below the staff. The known cosmetic
// consequence of owner decision 3b — the lyric line sits below the whole-period
// frame bottom, which can be far from the note heads until precise frames land
// under the later plan — is accepted, and is recorded here rather than fixed.
TEST(Engraving_MeloStaffM9SATBTests, m9SweepLyricsAttachToMeloNotesAndSitBelowTheStaff)
{
    MasterScore* score = ScoreRW::readScore(HYMN);
    ASSERT_TRUE(score);
    score->doLayout();
    ASSERT_EQ(score->nstaves(), 4u);

    size_t syllables = 0;
    double lowestNoteY = -1e9;
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            EngravingItem* e = s->element(0);
            if (!e || !e->isChord()) {
                continue;
            }
            Chord* c = toChord(e);
            for (Note* n : c->notes()) {
                ASSERT_TRUE(n->hasMeloPitch()) << "the lyric carrier must be a MeloPresto note";
                lowestNoteY = std::max(lowestNoteY, n->pagePos().y());
            }
            for (Lyrics* l : c->lyrics()) {
                ++syllables;
                EXPECT_EQ(l->placement(), PlacementV::BELOW) << "lyrics on a vocal staff sit below";
                // The accepted decision-3b consequence: the lyric line clears
                // the whole frame, so it can sit well below its own note head.
                EXPECT_GT(l->pagePos().y(), lowestNoteY - 1e-6);
            }
        }
    }
    EXPECT_EQ(syllables, 8u) << "the fixture's first phrase carries eight syllables";
    delete score;
}

// Dynamics, expression text and hairpins default ABOVE a vocal staff, and that
// rule is what puts them clear of the MeloPresto header terrain and of the
// mid-system change indicators, which live on and below the staff.
TEST(Engraving_MeloStaffM9SATBTests, m9SweepDynamicsUseVocalAbovePlacementOnMeloStaves)
{
    MasterScore* score = ScoreRW::readScore(HYMN);
    ASSERT_TRUE(score);
    score->doLayout();

    EXPECT_TRUE(score->style().styleB(Sid::dynamicsHairpinsAboveForVocalStaves))
        << "the vocal-above rule must be on by default";
    for (const Part* p : score->parts()) {
        EXPECT_TRUE(p->instrument()->isVocalInstrument()) << "the rule keys off the vocal family";
    }

    size_t dynamics = 0;
    double dynamicY = 0.0;
    double highestNoteY = 1e9;                 // smallest y = highest on the page
    for (Note* n : notesOn(score, 0)) {
        highestNoteY = std::min(highestNoteY, n->pagePos().y());
    }
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        for (Segment* s = m->first(); s; s = s->next()) {
            for (EngravingItem* e : s->annotations()) {
                if (e && e->isDynamic() && e->staffIdx() == 0) {
                    ++dynamics;
                    EXPECT_EQ(e->placement(), PlacementV::ABOVE) << "a dynamic on a vocal MeloPresto Staff sits above";
                    dynamicY = e->pagePos().y();
                }
            }
        }
    }
    EXPECT_EQ(dynamics, 1u) << "the fixture carries one dynamic";
    // Above the staff means clear of the MeloPresto terrain below it: the dynamic is
    // higher on the page than every note of the staff it belongs to.
    EXPECT_LT(dynamicY, highestNoteY) << "the dynamic must clear the MeloPresto header terrain and the frame";
    delete score;
}

// Range colouring keys off the INTEGER playback pitch against the part's
// amateur/professional bounds, and is a screen-only painter decision
// (ChordLayout draws it only when !isPrinting). This asserts the seam's inputs
// at the three boundaries; the colouring itself never reaches a printed render,
// which is why the M9 pixel evidence does not and cannot show it.
TEST(Engraving_MeloStaffM9SATBTests, m9SweepRangeColouringInputsAreCorrectOnMeloVocalStaves)
{
    MasterScore* score = ScoreRW::readScore(HYMN);
    ASSERT_TRUE(score);
    score->doLayout();

    const Instrument* soprano = score->parts()[0]->instrument();
    ASSERT_TRUE(soprano);
    EXPECT_EQ(soprano->minPitchA(), 60);
    EXPECT_EQ(soprano->maxPitchA(), 79);
    EXPECT_EQ(soprano->minPitchP(), 60);
    EXPECT_EQ(soprano->maxPitchP(), 84);

    std::vector<Note*> soprano_notes = notesOn(score, 0);
    ASSERT_EQ(soprano_notes.size(), 8u);
    for (Note* n : soprano_notes) {
        ASSERT_TRUE(n->hasMeloPitch());
        // The integer playback pitch is what the range test reads, and it is
        // the Kernel identity's own compatibility pitch — not a second opinion.
        EXPECT_EQ(n->ppitch(), n->pitch()) << "range colouring must read the note's own playback pitch";
        EXPECT_GE(n->ppitch(), soprano->minPitchA());
        EXPECT_LE(n->ppitch(), soprano->maxPitchA());
    }
    // The three boundaries, exercised through the same accessors the painter
    // uses: in range, amateur-only, professional-out-of-range.
    struct Case {
        int pitch;
        bool inAmateur;
        bool inProfessional;
    };
    const Case cases[] = {
        { 72, true,  true },         // C5: comfortable
        { 81, false, true },         // A5: above amateur, inside professional
        { 88, false, false },        // E6: outside professional
    };
    for (const Case& c : cases) {
        const bool amateur = c.pitch >= soprano->minPitchA() && c.pitch <= soprano->maxPitchA();
        const bool professional = c.pitch >= soprano->minPitchP() && c.pitch <= soprano->maxPitchP();
        EXPECT_EQ(amateur, c.inAmateur) << "pitch " << c.pitch;
        EXPECT_EQ(professional, c.inProfessional) << "pitch " << c.pitch;
    }
    delete score;
}

// Hide-empty-staves on a four-MeloPresto Staff score with written and unwritten parts:
// stock behaviour, unchanged by the MeloPresto staff type.
TEST(Engraving_MeloStaffM9SATBTests, m9SweepHideEmptyStavesWorksOnAFourMeloStaffMixedScore)
{
    MasterScore* score = ScoreRW::readScore(u"jimstaff_data/m9-satb-mixed.mscx");
    ASSERT_TRUE(score);
    score->doLayout();
    ASSERT_EQ(score->nstaves(), 4u);

    score->style().set(Sid::hideEmptyStaves, true);
    score->style().set(Sid::dontHideStavesInFirstSystem, false);
    score->setLayoutAll();
    score->doLayout();

    ASSERT_FALSE(score->systems().empty());
    const System* sys = score->systems().front();
    EXPECT_TRUE(sys->staff(0)->show()) << "the written part must stay visible";
    for (staff_idx_t i = 1; i < 4; ++i) {
        EXPECT_FALSE(sys->staff(i)->show()) << "unwritten MeloPresto Staff " << i << " must hide like any empty staff";
    }

    score->style().set(Sid::hideEmptyStaves, false);
    score->setLayoutAll();
    score->doLayout();
    ASSERT_FALSE(score->systems().empty());
    for (staff_idx_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(score->systems().front()->staff(i)->show()) << "staff " << i << " must reappear";
    }
    delete score;
}

// A MeloPresto note carries two things that must agree: its lattice identity, and the
// compatibility pitch MuseScore plays and reports. Nothing enforced that, and
// the M9 fixtures shipped with identities a whole tone away from their pitches
// — invisible to every other assertion, but visible on the page as accidental
// note-head shapes in music that has no accidentals.
//
// The agreement is asked of the KERNEL, per staff state, never computed here.
// A constant like "62 + cents/100" would hard-code Re0 to D4 and make every
// score fixed-Do; MeloPresto is movable-Do, so the anchor is whatever that staff's
// reference resolves to, and only the Kernel knows it.
TEST(Engraving_MeloStaffM9SATBTests, m9EveryNotesPitchIsTheKernelsProjectionOfItsIdentity)
{
    const char16_t* fixtures[] = {
        u"jimstaff_data/m9-satb-hymn.mscx",
        u"jimstaff_data/m9-satb-mixed.mscx",
        u"jimstaff_data/m9-dense-voices.mscx",
    };
    for (const char16_t* f : fixtures) {
        MasterScore* score = ScoreRW::readScore(muse::String(f));
        ASSERT_TRUE(score) << muse::String(f).toStdString();
        score->doLayout();

        size_t checked = 0;
        for (staff_idx_t s = 0; s < score->nstaves(); ++s) {
            for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
                const StaffType* st = score->staff(s)->staffType(m->tick());
                if (!st || !st->isMelo()) {
                    continue;
                }
                for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
                    for (voice_idx_t v = 0; v < VOICES; ++v) {
                        EngravingItem* e = seg->element(s * VOICES + v);
                        if (!e || !e->isChord()) {
                            continue;
                        }
                        for (Note* n : toChord(e)->notes()) {
                            if (!n->hasMeloPitch()) {
                                continue;
                            }
                            melo::SoundingPitch sounding;
                            muse::String err;
                            ASSERT_TRUE(melo::noteSoundingPitch(st->meloStateJson(), n->meloNPer(), n->meloNGen(),
                                                                sounding, &err))
                                << muse::String(f).toStdString() << ": " << err.toStdString();
                            EXPECT_EQ(n->pitch(), sounding.midiKey)
                                << muse::String(f).toStdString() << " staff " << s
                                << " measure " << m->no() + 1
                                << ": identity (" << n->meloNPer() << "," << n->meloNGen()
                                << ") sounds at MIDI " << sounding.midiKey
                                << " under Re0=" << sounding.referenceKeyNumber
                                << " (" << sounding.anchor.toStdString() << "), but the note carries pitch "
                                << n->pitch();
                            ++checked;
                        }
                    }
                }
            }
        }
        EXPECT_GT(checked, 0u) << muse::String(f).toStdString() << " carried no MeloPresto notes to check";
        delete score;
    }
}

namespace {
// The scale array as written, e.g. ["M2","m2",...] — the collection itself,
// independent of which degree is currently the tonic.
muse::String collectionOf(const muse::String& stateJson)
{
    const size_t at = stateJson.indexOf(u"\"scale\"");
    const size_t end = stateJson.indexOf(u']', at);
    if (at == muse::nidx || end == muse::nidx) {
        return muse::String();
    }
    return stateJson.mid(at, end + 1 - at);
}
}

// Movable Do with a La-based minor is the design this milestone must not
// quietly break. Going to the relative minor moves the TONIC to La over the
// same collection; it does not re-anchor Do onto the minor tonic, and it does
// not touch the key. So after the change: Do is still C, the collection is
// still the same seven, only mode_rotation moves — and it moves for every
// part at once, because the score-wide path is what M9 added.
//
// If anyone ever "simplifies" a check by assuming Do is C, or by treating the
// mode as decoration, this test is what fails.
TEST(Engraving_MeloStaffM9SATBTests, m9RelativeMinorMovesTheTonicToLaAndLeavesDoWhereItIs)
{
    MasterScore* score = openShippedTemplate();
    ASSERT_TRUE(score);
    score->doLayout();
    Measure* m2 = measureNo(score, 2);
    ASSERT_TRUE(m2);

    // The template is in C, Do-mode: Do is the tonic and Do is C.
    melo::StateChangeOptions before;
    ASSERT_TRUE(melo::changeOptions(score, 0, m2, before));
    ASSERT_GE(before.tonics.size(), 6u);
    EXPECT_EQ(before.tonics[0].label, muse::String(u"Do"));
    EXPECT_TRUE(before.tonics[0].current) << "the template must start with Do as the tonic";
    EXPECT_EQ(before.tonics[5].label, muse::String(u"La")) << "La must be offered as a tonic";

    muse::String collectionBefore[4];
    for (staff_idx_t i = 0; i < 4; ++i) {
        const muse::String s = stateAt(score, i, m2);
        EXPECT_TRUE(s.contains(u"\"mode_rotation\":0")) << "staff " << i;
        EXPECT_TRUE(s.contains(u"\"key_number\":62")) << "staff " << i << " states no key";
        collectionBefore[i] = collectionOf(s);
        ASSERT_FALSE(collectionBefore[i].empty()) << "staff " << i;
    }

    // Take every part to the relative minor at once.
    muse::String error;
    ASSERT_TRUE(melo::applyChangeToAllMeloParts(score, m2, { before.tonics[5].id }, error)) << error.toStdString();
    score->doLayout();

    for (staff_idx_t i = 0; i < 4; ++i) {
        const muse::String s = stateAt(score, i, m2);
        // The tonic is now La...
        EXPECT_TRUE(s.contains(u"\"mode_rotation\":5")) << "staff " << i << " did not move its tonic to La";
        // ...the key did not move: Do is still C.
        EXPECT_TRUE(s.contains(u"\"key_number\":62")) << "staff " << i << " changed key when only the mode should move";
        // ...and it is the same seven notes, not a different collection.
        EXPECT_EQ(collectionOf(s), collectionBefore[i])
            << "staff " << i << " changed collection; the relative minor is the same notes";
        EXPECT_TRUE(s.contains(u"\"collection_rotation\":0")) << "staff " << i;
    }

    // The Kernel now reports La as the tonic, for every part.
    for (staff_idx_t i = 0; i < 4; ++i) {
        melo::StateChangeOptions after;
        ASSERT_TRUE(melo::changeOptions(score, i, m2, after));
        ASSERT_GE(after.tonics.size(), 6u);
        EXPECT_TRUE(after.tonics[5].current) << "staff " << i << ": La is not reported as the tonic";
        EXPECT_FALSE(after.tonics[0].current) << "staff " << i << ": Do is still reported as the tonic";
    }

    delete score;
}
