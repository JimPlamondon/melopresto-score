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

// JiMStaff Milestone 6 — editing workflow (owner decision 1a, 2026-08-16):
// keyboard pitch editing moves on the lattice through the Kernel; every
// copy/paste/clone keeps the lattice identity; key/mode/scale changes are
// authored through a controller that transports Kernel states into the
// StaffTypeChange carrier. The fork never computes a step, a tonic, a
// reference shift, or a rotation itself.

#include <gtest/gtest.h>

#include <QMimeData>

#include "engraving/internal/qmimedataadapter.h"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <tuple>

#include "engraving/dom/accidental.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/input.h"
#include "engraving/dom/utils.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/layoutbreak.h"
#include "engraving/dom/system.h"
#include "engraving/dom/note.h"
#include "engraving/dom/partialtie.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/text.h"
#include "engraving/dom/box.h"
#include "engraving/dom/stafftype.h"
#include "engraving/dom/stafftypechange.h"
#include "engraving/dom/stafflines.h"
#include "engraving/dom/tie.h"
#include "engraving/infrastructure/mscwriter.h"
#include "engraving/melo/melobridge.h"
#include "engraving/melo/melochange.h"
#include "engraving/melo/melochangecontroller.h"
#include "engraving/rw/mscsaver.h"

#include "draw/bufferedpaintprovider.h"
#include "draw/painter.h"
#include "io/file.h"

#include "utils/scorerw.h"
#include "utils/melocanonical.h"

using namespace mu::engraving;
using namespace mu::engraving::rendering;
using namespace muse;
using namespace muse::draw;

namespace {
Measure* measureNo(Score* score, int n)
{
    Measure* m = score->firstMeasure();
    for (int i = 1; m && i < n; ++i) {
        m = m->nextMeasure();
    }
    return m;
}

std::vector<Note*> meloNotes(Score* score, staff_idx_t staffIdx = 0)
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

std::vector<Note*> notesInMeasure(Measure* measure, staff_idx_t staffIdx = 0)
{
    std::vector<Note*> out;
    for (Segment* segment = measure->first(SegmentType::ChordRest); segment;
         segment = segment->next(SegmentType::ChordRest)) {
        for (voice_idx_t voice = 0; voice < VOICES; ++voice) {
            EngravingItem* item = segment->element(staffIdx * VOICES + voice);
            if (item && item->isChord()) {
                out.insert(out.end(), toChord(item)->notes().begin(), toChord(item)->notes().end());
            }
        }
    }
    return out;
}

const StaffType* meloStaffType(Score* score)
{
    return score->staff(0)->staffType(Fraction(0, 1));
}

int compatPitch(const melo::PitchHit& hit)
{
    static const muse::String letters(u"CDEFGAB");
    const int step = int(letters.indexOf(muse::Char(hit.step)));
    static const int stepPitches[7] = { 0, 2, 4, 5, 7, 9, 11 };
    return (hit.octave + 1) * 12 + stepPitches[step] + hit.alter;
}

Score* syntheticCommonToneScore()
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/jims-template.mscz");
    if (!score) {
        return nullptr;
    }
    Measure* third = measureNo(score, 3);
    if (third) {
        score->deleteMeasures(third, third);
    }
    String error;
    if (!melo::validateState(score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), error)) {
        delete score;
        return nullptr;
    }
    InputState& input = score->inputState();
    input.setTrack(0);
    input.setSegment(score->tick2segment(Fraction(0, 1), false, SegmentType::ChordRest));
    input.setDuration(DurationType::V_WHOLE);
    input.setAccidentalType(AccidentalType::NONE);
    input.setNoteEntryMode(true);
    for (int i = 0; i < 2; ++i) {
        score->startCmd(TranslatableString::untranslatable("common-tone note"));
        score->cmdAddPitch(5 * 7 + 1, false, false); // D4, the bound Re0.
        score->endCmd();
    }
    input.setNoteEntryMode(false);
    std::vector<Note*> notes = meloNotes(score);
    if (notes.size() != 2) {
        delete score;
        return nullptr;
    }
    Tie* tie = Factory::createTie(score->dummy());
    tie->setStartNote(notes[0]);
    tie->setEndNote(notes[1]);
    tie->setTrack(notes[0]->track());
    tie->setTick(notes[0]->tick());
    tie->setTick2(notes[1]->tick());
    score->startCmd(TranslatableString::untranslatable("common-tone tie"));
    score->undoAddElement(tie);
    score->endCmd();
    if (!test::relativeKey(score, 0, measureNo(score, 2)->tick(), -1, 3, error)) {
        delete score;
        return nullptr;
    }
    score->setMetaTag(u"workTitle", u"MeloPresto Common-Tone Projection Acceptance");
    score->doLayout();
    return score;
}
}

// Binding Requirement 2: plain Up/Down = nearest realizable lattice pitch
// (CHROMATIC), Alt+Shift = collection member (DIATONIC), Ctrl = one period
// (OCTAVE) — every answer from the Kernel step_pitch op, identity and
// compatibility pitch changed together, undoable.
TEST(MeloStaffTests, m6KeyboardStepsMoveOnTheLatticeThroughTheKernel)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/collision.mscx");
    ASSERT_TRUE(score);
    score->doLayout();
    const StaffType* st = meloStaffType(score);
    ASSERT_TRUE(st && st->isMelo());
    auto notes = meloNotes(score);
    ASSERT_FALSE(notes.empty());
    Note* n = notes.front();
    ASSERT_TRUE(n->hasMeloPitch());

    struct Case {
        UpDownMode mode;
        bool up;
        const char* domain;
    };
    const Case cases[] = {
        { UpDownMode::CHROMATIC, true, "lattice" },
        { UpDownMode::CHROMATIC, false, "lattice" },
        { UpDownMode::DIATONIC, true, "collection" },
        { UpDownMode::DIATONIC, false, "collection" },
        { UpDownMode::OCTAVE, true, "period" },
        { UpDownMode::OCTAVE, false, "period" },
    };
    for (const Case& c : cases) {
        const int nPer0 = n->meloNPer(), nGen0 = n->meloNGen(), pitch0 = n->pitch();
        melo::PitchHit hit;
        ASSERT_TRUE(melo::stepPitch(st->meloStateJson(), nPer0, nGen0, c.up, c.domain, hit)) << c.domain;
        score->select(n);
        score->startCmd(TranslatableString::untranslatable("M6 test step"));
        score->upDown(c.up, c.mode);
        score->endCmd();
        score->doLayout();
        EXPECT_EQ(n->meloNPer(), hit.nPer) << c.domain << (c.up ? " up" : " down");
        EXPECT_EQ(n->meloNGen(), hit.nGen) << c.domain << (c.up ? " up" : " down");
        EXPECT_EQ(n->pitch(), compatPitch(hit)) << "compatibility pitch follows the Kernel spelling";
        EXPECT_NE(n->pitch(), pitch0) << "a step must move";
        score->undoRedo(true, nullptr);
        score->doLayout();
        EXPECT_EQ(n->meloNPer(), nPer0) << "undo restores the identity";
        EXPECT_EQ(n->meloNGen(), nGen0);
        EXPECT_EQ(n->pitch(), pitch0);
        score->undoRedo(false, nullptr);
        score->doLayout();
        EXPECT_EQ(n->meloNPer(), hit.nPer) << "redo re-applies";
        score->undoRedo(true, nullptr);
        score->doLayout();
    }
    // Period steps preserve the class; collection steps land on members.
    {
        melo::PitchHit hit;
        ASSERT_TRUE(melo::stepPitch(st->meloStateJson(), n->meloNPer(), n->meloNGen(), true, "period", hit));
        EXPECT_EQ(hit.nGen, n->meloNGen());
        EXPECT_EQ(hit.nPer, n->meloNPer() + 1);
    }
    delete score;
}

// Binding Requirement 3 (Decision 1a): a copied/cloned note keeps its
// lattice identity and forgets its cached cents; a range copy/paste keeps
// every identity in order.
TEST(MeloStaffTests, m6CopiedAndPastedNotesKeepTheirLatticeIdentity)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/collision.mscx");
    ASSERT_TRUE(score);
    score->doLayout();
    auto notes = meloNotes(score);
    ASSERT_FALSE(notes.empty());
    Note* src = notes.front();
    ASSERT_TRUE(src->hasMeloPitch());
    // Direct clone through the copy constructor (Factory::copyNote).
    Note* copy = Factory::copyNote(*src, false);
    ASSERT_TRUE(copy);
    EXPECT_TRUE(copy->hasMeloPitch()) << "the copy constructor must carry the identity";
    EXPECT_EQ(copy->meloNPer(), src->meloNPer());
    EXPECT_EQ(copy->meloNGen(), src->meloNGen());
    EXPECT_FALSE(copy->meloCentsValid()) << "derived cents are re-derived in the destination, never carried";
    delete copy;

    // Range copy of measure 1, paste at measure 3.
    Measure* m1 = measureNo(score, 1);
    Measure* m3 = measureNo(score, 3);
    ASSERT_TRUE(m1 && m3);
    std::vector<std::pair<int, int> > sourceIds;
    for (Segment* s = m1->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
        if (EngravingItem* e = s->element(0); e && e->isChord()) {
            for (Note* nn : toChord(e)->notes()) {
                sourceIds.push_back({ nn->meloNPer(), nn->meloNGen() });
            }
        }
    }
    ASSERT_FALSE(sourceIds.empty());
    score->select(m1);
    ASSERT_TRUE(score->selection().canCopy());
    QMimeData* mimeData = new QMimeData;
    mimeData->setData(score->selection().mimeType(), score->selection().mimeData().toQByteArray());
    ASSERT_TRUE(m3->first(SegmentType::ChordRest)->element(0));
    score->select(m3->first(SegmentType::ChordRest)->element(0));
    score->startCmd(TranslatableString::untranslatable("M6 test paste"));
    QMimeDataAdapter ma(mimeData);
    score->cmdPaste(&ma, 0);
    score->endCmd();
    score->doLayout();
    std::vector<std::pair<int, int> > pastedIds;
    for (Segment* s = m3->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
        if (EngravingItem* e = s->element(0); e && e->isChord()) {
            for (Note* nn : toChord(e)->notes()) {
                pastedIds.push_back({ nn->meloNPer(), nn->meloNGen() });
            }
        }
    }
    ASSERT_GE(pastedIds.size(), sourceIds.size());
    for (size_t i = 0; i < sourceIds.size(); ++i) {
        EXPECT_EQ(pastedIds[i], sourceIds[i]) << "pasted note " << i << " lost its identity";
    }
    // Undo the paste: measure 3 returns to its prior content, identities intact.
    score->undoRedo(true, nullptr);
    score->doLayout();
    for (Note* nn : meloNotes(score)) {
        EXPECT_TRUE(nn->hasMeloPitch());
    }
    delete mimeData;
    delete score;
}

// Binding Requirement 4: the change controller inserts, compounds, and
// removes the measure's carrier from Kernel-returned states only.
TEST(MeloStaffTests, m6ChangeControllerAuthorsCarriersFromKernelStates)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/collision.mscx");
    ASSERT_TRUE(score);
    score->doLayout();
    Measure* m2 = measureNo(score, 2);
    ASSERT_TRUE(m2);
    ASSERT_FALSE(melo::changeCarrier(m2, 0));

    muse::String base;
    ASSERT_TRUE(melo::effectiveState(score, 0, m2, base));
    melo::StateChangeOptions options;
    ASSERT_TRUE(melo::changeOptions(score, 0, m2, options));
    ASSERT_EQ(options.tonics.size(), 7u);
    EXPECT_EQ(options.tonics[0].label, muse::String(u"Do"));
    EXPECT_TRUE(options.tonics[0].current);
    EXPECT_EQ(options.tonics[5].label, muse::String(u"La"));
    EXPECT_EQ(options.tonics[5].id, muse::String(u"mode:1"));
    muse::String why;
    EXPECT_TRUE(melo::canInsertChange(score, 0, m2, why)) << why.toStdString();

    // Mode change Do -> La: the carrier's state is exactly the Kernel's answer.
    muse::String expected, error;
    ASSERT_TRUE(melo::applyStateChange(base, u"mode:1", expected, error)) << error.toStdString();
    ASSERT_TRUE(melo::applyChange(score, 0, m2, u"mode:1", error)) << error.toStdString();
    score->doLayout();
    const StaffTypeChange* stc = melo::changeCarrier(m2, 0);
    ASSERT_TRUE(stc);
    // Byte for byte the Kernel's answer — except the tonic ambit, which layout
    // derives from the new section's melody and saves (owner Q4 rider,
    // automatic since 2026-08-19).
    auto withoutAmbit = [](const muse::String& json) {
        muse::String out = json;
        for (const char16_t* tok : { u",\"tonic_ambit\":\"tonic-bounded\"", u",\"tonic_ambit\":\"tonic-centered\"" }) {
            out.replace(muse::String(tok), muse::String());
        }
        return out;
    };
    EXPECT_EQ(withoutAmbit(test::configuration(stc->staffType()->meloStateJson())), withoutAmbit(test::configuration(expected)));
    EXPECT_TRUE(stc->staffType()->meloStateJson().contains(u"\"tonic_ambit\":\"tonic-"));
    EXPECT_TRUE(stc->staffType()->meloStateJson().contains(u"\"mode_rotation\":5"));
    melo::ChangeIndicator model;
    ASSERT_TRUE(melo::midSystemChangeIndicator(m2, 0, model));
    ASSERT_EQ(model.kinds.size(), 1u);
    EXPECT_EQ(model.kinds[0], muse::String(u"mode"));

    // One undo step removes the carrier; redo restores it.
    score->undoRedo(true, nullptr);
    score->doLayout();
    EXPECT_FALSE(melo::changeCarrier(m2, 0));
    score->undoRedo(false, nullptr);
    score->doLayout();
    ASSERT_TRUE(melo::changeCarrier(m2, 0));

    // Removed numeric commands refuse; the canonical default needs no binding.
    EXPECT_FALSE(melo::applyChange(score, 0, m2, u"key:-1:3", error));
    EXPECT_FALSE(melo::applyChange(score, 0, m2, u"bind:reference-pitch:62", error));
    ASSERT_TRUE(melo::validateState(score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), error)) << error.toStdString();
    ASSERT_TRUE(test::relativeKey(score, 0, m2->tick(), -1, 3, error)) << error.toStdString();
    score->doLayout();
    stc = melo::changeCarrier(m2, 0);
    ASSERT_TRUE(stc);
    EXPECT_TRUE((test::referenceNumber(stc->staffType()->meloStateJson()) == 71));
    EXPECT_TRUE(stc->staffType()->meloStateJson().contains(u"\"mode_rotation\":5"));
    // The base staff type keeps its mode: the mode/key change is carried at
    // the measure only — but the BIND is staff-wide (a reference names what
    // the staff's Re0 is), so the base is now bound to 62 as well.
    EXPECT_TRUE(meloStaffType(score)->meloStateJson().contains(u"\"mode_rotation\":0"));
    EXPECT_TRUE((test::referenceNumber(meloStaffType(score)->meloStateJson()) == 62));
    // Compounded from the carrier: options now report La as current.
    ASSERT_TRUE(melo::changeOptions(score, 0, m2, options));
    EXPECT_TRUE(options.tonics[5].current);
    // With the base bound, the indicator at m2 is the owner's worked
    // example (kinds key, mode) — binding at the change bar no longer
    // leaves it silently undrawable.
    {
        melo::ChangeIndicator model;
        ASSERT_TRUE(melo::midSystemChangeIndicator(m2, 0, model));
        ASSERT_EQ(model.kinds.size(), 2u);
        EXPECT_EQ(model.kinds[0], muse::String(u"key"));
        EXPECT_EQ(model.kinds[1], muse::String(u"mode"));
    }
    // Removing the configuration leaves the separately authored key event.
    ASSERT_TRUE(melo::removeChange(score, 0, m2, error)) << error.toStdString();
    score->doLayout();
    ASSERT_TRUE(melo::changeCarrier(m2, 0));
    EXPECT_TRUE(melo::changeCarrier(m2, 0)->meloReferenceOnly());
    EXPECT_TRUE(melo::changeCarrier(m2, 0)->staffType()->meloStateJson().contains(u"\"mode_rotation\":0"));
    score->undoRedo(true, nullptr);
    score->doLayout();
    ASSERT_TRUE(melo::changeCarrier(m2, 0));
    EXPECT_TRUE(melo::changeCarrier(m2, 0)->staffType()->meloStateJson().contains(u"\"mode_rotation\":5"));
    EXPECT_EQ(test::referenceNumber(melo::changeCarrier(m2, 0)->staffType()->meloStateJson()), 71);
    // Foreign choice ids are refused without touching the score.
    EXPECT_FALSE(melo::applyChange(score, 0, m2, u"tuning:700", error));
    delete score;
}

TEST(MeloStaffTests, midBarChangeStartsAtTheSelectedExactTick)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/collision.mscx");
    ASSERT_TRUE(score);
    Measure* measure = measureNo(score, 2);
    ASSERT_TRUE(measure);
    const std::vector<Note*> notes = notesInMeasure(measure);
    ASSERT_GE(notes.size(), 2u);
    const Fraction changeTick = notes.back()->tick();
    ASSERT_GT(changeTick, measure->tick());
    ASSERT_LT(changeTick, measure->endTick());

    Staff* staff = score->staff(0);
    ASSERT_TRUE(staff);
    const String beforeState = staff->staffType(changeTick - Fraction::fromTicks(1))->meloStateJson();
    String expected;
    String error;
    ASSERT_TRUE(melo::applyStateChange(beforeState, u"mode:1", expected, error)) << error.toStdString();
    ASSERT_TRUE(melo::applyChange(score, 0, measure, changeTick, u"mode:1", error)) << error.toStdString();

    const StaffTypeChange* carrier = melo::changeCarrierAt(measure, 0, changeTick);
    ASSERT_TRUE(carrier);
    EXPECT_EQ(carrier->tick(), changeTick);
    EXPECT_EQ(carrier->rtick(), changeTick - measure->tick());
    EXPECT_EQ(staff->staffType(changeTick - Fraction::fromTicks(1))->meloStateJson(), beforeState);
    EXPECT_EQ(test::configuration(staff->staffType(changeTick)->meloStateJson()), test::configuration(expected));
    EXPECT_EQ(staff->staffTypeForElement(notes.front())->meloStateJson(), beforeState);
    EXPECT_EQ(test::configuration(staff->staffTypeForElement(notes.back())->meloStateJson()), test::configuration(expected));

    auto withoutAmbit = [](const String& json) {
        String out = json;
        for (const char16_t* token : { u",\"tonic_ambit\":\"tonic-bounded\"", u",\"tonic_ambit\":\"tonic-centered\"" }) {
            out.replace(String(token), String());
        }
        return out;
    };
    String expectedShared;
    String projectionError;
    ASSERT_TRUE(melo::musicxmlConfigurationV5Xml(withoutAmbit(expected), 0, true, expectedShared, projectionError))
        << projectionError.toStdString();
    for (const String& path : { String(u"midbar-change-roundtrip.mscx"), String(u"midbar-change-roundtrip.mscz") }) {
        if (path.endsWith(u".mscz")) {
            io::File::remove(path);
            io::File file(path);
            ASSERT_TRUE(file.open(io::IODevice::WriteOnly));
            MscWriter::Params params;
            params.device = &file;
            params.filePath = path;
            params.mode = MscIoMode::Zip;
            MscWriter writer(params);
            ASSERT_TRUE(writer.open());
            MscSaver saver(score->iocContext());
            ASSERT_TRUE(saver.writeMscz(score->masterScore(), writer, false));
            writer.close();
            file.close();
        } else {
            ASSERT_TRUE(ScoreRW::saveScore(score, path));
        }
        Score* reopened = ScoreRW::readScore(path, true);
        ASSERT_TRUE(reopened) << path.toStdString();
        Measure* reopenedMeasure = measureNo(reopened, 2);
        ASSERT_TRUE(reopenedMeasure);
        const StaffTypeChange* reopenedCarrier = melo::changeCarrierAt(reopenedMeasure, 0, changeTick);
        ASSERT_TRUE(reopenedCarrier);
        EXPECT_EQ(reopenedCarrier->rtick(), changeTick - measure->tick());
        String reopenedShared;
        ASSERT_TRUE(melo::musicxmlConfigurationV5Xml(
                        withoutAmbit(reopened->staff(0)->staffType(changeTick)->meloStateJson()), 0, true, reopenedShared, projectionError))
            << projectionError.toStdString();
        EXPECT_EQ(reopenedShared, expectedShared);
        delete reopened;
    }

    delete score;
}

TEST(MeloStaffTests, twoMidBarChangesCanOccupyOneMeasure)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/collision.mscx");
    ASSERT_TRUE(score);
    Measure* measure = measureNo(score, 1);
    ASSERT_TRUE(measure);
    const std::vector<Note*> notes = notesInMeasure(measure);
    ASSERT_GE(notes.size(), 4u);
    const Fraction firstTick = notes[1]->tick();
    const Fraction secondTick = notes[2]->tick();
    ASSERT_LT(firstTick, secondTick);
    String error;
    ASSERT_TRUE(melo::applyChange(score, 0, measure, firstTick, u"mode:1", error)) << error.toStdString();
    melo::StateChangeOptions options;
    ASSERT_TRUE(melo::changeOptions(score, 0, measure, secondTick, options));
    const auto target = std::find_if(options.tonics.begin(), options.tonics.end(),
                                     [](const melo::StateChangeOption& option) { return !option.current; });
    ASSERT_NE(target, options.tonics.end());
    ASSERT_TRUE(melo::applyChange(score, 0, measure, secondTick, target->id, error)) << error.toStdString();
    const std::vector<const StaffTypeChange*> carriers = melo::changeCarriers(measure, 0);
    ASSERT_EQ(carriers.size(), 2u);
    EXPECT_EQ(carriers[0]->tick(), firstTick);
    EXPECT_EQ(carriers[1]->tick(), secondTick);
    EXPECT_TRUE(measure->canAddStaffTypeChange(0, notes[3]->tick() - measure->tick()));
    EXPECT_FALSE(measure->canAddStaffTypeChange(0, firstTick - measure->tick()));
    ASSERT_TRUE(melo::removeChange(score, 0, measure, firstTick, error)) << error.toStdString();
    EXPECT_FALSE(melo::changeCarrierAt(measure, 0, firstTick));
    EXPECT_TRUE(melo::changeCarrierAt(measure, 0, secondTick));
    score->undoRedo(true, nullptr);
    EXPECT_TRUE(melo::changeCarrierAt(measure, 0, firstTick));
    EXPECT_TRUE(melo::changeCarrierAt(measure, 0, secondTick));
    score->undoRedo(false, nullptr);
    EXPECT_FALSE(melo::changeCarrierAt(measure, 0, firstTick));
    EXPECT_TRUE(melo::changeCarrierAt(measure, 0, secondTick));
    delete score;
}

TEST(MeloStaffTests, silentMidBarChangeCreatesATimingOnlyLayoutAnchor)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/collision.mscx");
    ASSERT_TRUE(score);
    Measure* measure = measureNo(score, 1);
    ASSERT_TRUE(measure);
    const std::vector<Note*> notes = notesInMeasure(measure);
    ASSERT_GE(notes.size(), 2u);
    const Fraction silentTick = Fraction::fromTicks((notes[0]->tick().ticks() + notes[1]->tick().ticks()) / 2);
    const Fraction rtick = silentTick - measure->tick();
    ASSERT_FALSE(measure->findSegmentR(SegmentType::TimeTick, rtick));
    String error;
    ASSERT_TRUE(melo::applyChange(score, 0, measure, silentTick, u"mode:1", error)) << error.toStdString();
    const StaffTypeChange* carrier = melo::changeCarrierAt(measure, 0, silentTick);
    ASSERT_TRUE(carrier);
    EXPECT_TRUE(measure->findSegmentR(SegmentType::TimeTick, rtick));
    score->doLayout();
    melo::ChangeIndicator indicator;
    EXPECT_TRUE(melo::midBarChangeIndicator(carrier, indicator));
    delete score;
}

TEST(MeloStaffTests, midBarIndicatorHasTwoGreyDashedBarWidthFlanks)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/collision.mscx");
    ASSERT_TRUE(score);
    Measure* measure = measureNo(score, 2);
    ASSERT_TRUE(measure);
    const std::vector<Note*> notes = notesInMeasure(measure);
    ASSERT_GE(notes.size(), 2u);
    const Fraction changeTick = notes.back()->tick();
    String error;
    ASSERT_TRUE(melo::applyChange(score, 0, measure, changeTick, u"mode:1", error)) << error.toStdString();
    score->doLayout();

    const StaffLines* lines = measure->staffLines(0);
    ASSERT_TRUE(lines);
    std::shared_ptr<BufferedPaintProvider> provider = std::make_shared<BufferedPaintProvider>();
    Painter painter(provider, "midbar");
    painter.setViewport(RectF(0, 0, 4000, 4000));
    PaintOptions options;
    lines->renderer()->drawItem(lines, &painter, options);
    painter.endDraw();

    const DrawDataPtr drawData = provider->drawData();
    const Color grey(128, 128, 128);
    const double expectedWidth = score->style().styleMM(Sid::barWidth);
    size_t dashedFlanks = 0;
    std::function<void(const DrawData::Item&)> walk = [&](const DrawData::Item& item) {
        for (const DrawData::Data& data : item.datas) {
            const DrawData::State& state = drawData->states.at(data.state);
            for (const DrawPolygon& polygon : data.polygons) {
                if (polygon.mode == PolygonMode::Polyline && polygon.polygon.size() == 2
                    && std::abs(polygon.polygon[0].x() - polygon.polygon[1].x()) < 1e-6
                    && state.pen.style() == PenStyle::DashLine && state.pen.color() == grey) {
                    ++dashedFlanks;
                    EXPECT_NEAR(state.pen.widthF(), expectedWidth, 1e-9);
                    EXPECT_EQ(state.pen.capStyle(), PenCapStyle::FlatCap);
                    EXPECT_GT(std::abs(polygon.polygon[1].y() - polygon.polygon[0].y()), 0.0);
                }
            }
        }
        for (const DrawData::Item& child : item.chilren) {
            walk(child);
        }
    };
    walk(drawData->item);
    EXPECT_EQ(dashedFlanks, 2u);

    delete score;
}

TEST(MeloStaffTests, midBarIndicatorElementsAlignWithTheDisplayedStaffNoteLines)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/collision.mscx");
    ASSERT_TRUE(score);
    Measure* firstMeasure = measureNo(score, 1);
    Measure* measure = measureNo(score, 2);
    ASSERT_TRUE(firstMeasure);
    ASSERT_TRUE(measure);
    const std::vector<Note*> notes = notesInMeasure(measure);
    ASSERT_GE(notes.size(), 2u);
    const Fraction changeTick = notes.back()->tick();
    String error;
    ASSERT_TRUE(melo::validateState(score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), error)) << error.toStdString();
    ASSERT_TRUE(melo::applyChange(score, 0, measure, changeTick, u"mode:1", error)) << error.toStdString();
    ASSERT_TRUE(test::relativeKey(score, 0, changeTick, -1, 3, error)) << error.toStdString();
    score->doLayout();

    const StaffLines* lines = measure->staffLines(0);
    ASSERT_TRUE(lines);
    const StaffType* displayedStaffType = score->staff(0)->staffType(measure->tick());
    ASSERT_TRUE(displayedStaffType);
    ASSERT_TRUE(displayedStaffType->isMelo());
    const StaffTypeChange* carrier = melo::changeCarrierAt(measure, 0, changeTick);
    ASSERT_TRUE(carrier);
    melo::ChangeIndicator indicator;
    const StaffType* changedStaffType = nullptr;
    ASSERT_TRUE(melo::midBarChangeIndicator(carrier, indicator, &changedStaffType));
    ASSERT_TRUE(changedStaffType);
    ASSERT_NE(changedStaffType, displayedStaffType);

    const StaffType::MeloFrameView& view
        = displayedStaffType->meloFrameView(score, 0, measure->system());
    ASSERT_FALSE(view.empty());
    melo::PeriodicOrigins origins;
    ASSERT_TRUE(melo::periodicOrigins(displayedStaffType->meloStateJson(), origins));
    const double periodCents = displayedStaffType->meloPeriodCents();
    ASSERT_GT(periodCents, 0.0);
    const double basePeriod = melo::changeAnchorPeriodCents(
        view, indicator, periodCents, origins.doCentsAboveExtentLower);
    std::vector<double> expectedTonicYs;
    for (const melo::ChangePoint& point : indicator.tonicIndicators) {
        const double cents = basePeriod + (point.periodOffset + point.ordinate) * periodCents;
        expectedTonicYs.push_back(lines->pos().y()
                                  + displayedStaffType->meloYFromCents(cents, view) * lines->spatium());
    }
    std::sort(expectedTonicYs.begin(), expectedTonicYs.end());

    std::shared_ptr<BufferedPaintProvider> provider = std::make_shared<BufferedPaintProvider>();
    Painter painter(provider, "midbar-lines");
    painter.setViewport(RectF(0, 0, 4000, 4000));
    PaintOptions options;
    lines->renderer()->drawItem(lines, &painter, options);
    painter.endDraw();

    const DrawDataPtr drawData = provider->drawData();
    const Color grey(128, 128, 128);
    std::vector<double> flankXs;
    std::vector<RectF> pathBounds;
    std::function<void(const DrawData::Item&)> walk = [&](const DrawData::Item& item) {
        for (const DrawData::Data& data : item.datas) {
            const DrawData::State& state = drawData->states.at(data.state);
            for (const DrawPolygon& polygon : data.polygons) {
                if (polygon.mode == PolygonMode::Polyline && polygon.polygon.size() == 2
                    && std::abs(polygon.polygon[0].x() - polygon.polygon[1].x()) < 1e-6
                    && state.pen.style() == PenStyle::DashLine && state.pen.color() == grey) {
                    flankXs.push_back(polygon.polygon[0].x());
                }
            }
            for (const DrawPath& path : data.paths) {
                pathBounds.push_back(path.path.boundingRect());
            }
        }
        for (const DrawData::Item& child : item.chilren) {
            walk(child);
        }
    };
    walk(drawData->item);
    ASSERT_EQ(flankXs.size(), 2u);
    std::sort(flankXs.begin(), flankXs.end());
    std::vector<double> paintedTonicYs;
    for (const RectF& bounds : pathBounds) {
        if (bounds.center().x() > flankXs.front() && bounds.center().x() < flankXs.back()
            && std::abs(bounds.width() - bounds.height()) < 1e-6) {
            paintedTonicYs.push_back(bounds.center().y());
        }
    }
    std::sort(paintedTonicYs.begin(), paintedTonicYs.end());
    ASSERT_EQ(paintedTonicYs.size(), expectedTonicYs.size());
    for (size_t i = 0; i < expectedTonicYs.size(); ++i) {
        EXPECT_NEAR(paintedTonicYs[i], expectedTonicYs[i], 1e-6)
            << "mid-bar tonic indicator is not aligned with its displayed staff note-line";
    }

    delete score;
}

// The owner's worked example authored entirely through the controller: base
// bound to 62 (bind at measure 1 = the base staff type), then at measure 2
// mode Do->La and key Do0->La0 — the accepted m5-key-mode semantics
// (kinds key, mode; one arrow up; La wrapped) fall out unchanged.
TEST(MeloStaffTests, m6WorkedExampleAuthoredThroughTheControllerMatchesM5)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/collision.mscx");
    ASSERT_TRUE(score);
    score->doLayout();
    Measure* m1 = measureNo(score, 1);
    Measure* m2 = measureNo(score, 2);
    muse::String error;
    // Binding at measure 1 edits the base staff type (no carrier at the origin).
    ASSERT_TRUE(melo::validateState(score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), error)) << error.toStdString();
    score->doLayout();
    EXPECT_FALSE(melo::changeCarrier(m1, 0)) << "the origin measure has no carrier; the base state is edited";
    EXPECT_TRUE((test::referenceNumber(meloStaffType(score)->meloStateJson()) == 62));
    ASSERT_TRUE(melo::applyChange(score, 0, m2, u"mode:1", error)) << error.toStdString();
    ASSERT_TRUE(test::relativeKey(score, 0, m2->tick(), -1, 3, error)) << error.toStdString();
    score->doLayout();
    melo::ChangeIndicator model;
    ASSERT_TRUE(melo::midSystemChangeIndicator(m2, 0, model));
    ASSERT_EQ(model.kinds.size(), 2u);
    EXPECT_EQ(model.kinds[0], muse::String(u"key"));
    EXPECT_EQ(model.kinds[1], muse::String(u"mode"));
    ASSERT_EQ(model.arrows.size(), 1u);
    EXPECT_TRUE(model.arrows[0].up);
    EXPECT_EQ(model.arrows[0].to.label, muse::String(u"La"));
    delete score;
}

TEST(MeloStaffTests, stateChangeAtomicallyReinterpretsAFullTieAtExactFrequency)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
    ASSERT_TRUE(score);
    auto notes = meloNotes(score);
    ASSERT_GE(notes.size(), 5u);
    Note* start = notes[3];
    Note* continuation = notes[4];
    start->setMeloPitch(0, 0);
    start->setPitch(62, 16, 16);
    melo::SoundingPitch initialSound, initialContinuation;
    ASSERT_TRUE(melo::noteSoundingPitch(start->staff()->staffTypeForElement(start)->meloStateJson(), 0, 0, initialSound));
    ASSERT_TRUE(melo::noteContinuation(continuation->staff()->staffTypeForElement(continuation)->meloStateJson(),
                                       initialSound.frequencyHz, initialContinuation));
    continuation->setMeloPitch(initialContinuation.nPer, initialContinuation.nGen);
    ASSERT_TRUE(continuation->setNval(continuation->noteVal()));
    Tie* tie = Factory::createTie(score->dummy());
    tie->setStartNote(start);
    tie->setEndNote(continuation);
    tie->setTrack(start->track());
    tie->setTick(start->tick());
    tie->setTick2(continuation->tick());
    score->startCmd(TranslatableString::untranslatable("projection tie fixture"));
    score->undoAddElement(tie);
    score->endCmd();

    melo::SoundingPitch established;
    const StaffType* oldState = start->staff()->staffTypeForElement(start);
    ASSERT_TRUE(melo::noteSoundingPitch(oldState->meloStateJson(), start->meloNPer(), start->meloNGen(), established));
    const int oldNPer = continuation->meloNPer();
    const int oldNGen = continuation->meloNGen();
    const int oldPitch = continuation->pitch();
    const int oldTpc1 = continuation->tpc1();
    const int oldTpc2 = continuation->tpc2();
    const double oldTuning = continuation->tuning();

    String error;
    ASSERT_TRUE(test::relativeKey(score, 0, measureNo(score, 2)->tick(), -1, 3, error)) << error.toStdString();
    const StaffType* newState = continuation->staff()->staffTypeForElement(continuation);
    melo::SoundingPitch projected;
    ASSERT_TRUE(melo::noteSoundingPitch(newState->meloStateJson(), continuation->meloNPer(), continuation->meloNGen(), projected));
    EXPECT_NEAR(projected.frequencyHz, established.frequencyHz, 1e-9);
    EXPECT_EQ(continuation->pitch(), projected.midiKey);
    EXPECT_NEAR(continuation->tuning(), projected.centsOffset, 1e-9);
    EXPECT_NE(continuation->meloNGen(), oldNGen) << "the continuation takes its new-state teaching identity";

    score->undoRedo(true, nullptr);
    EXPECT_EQ(continuation->meloNPer(), oldNPer);
    EXPECT_EQ(continuation->meloNGen(), oldNGen);
    EXPECT_EQ(continuation->pitch(), oldPitch);
    EXPECT_EQ(continuation->tpc1(), oldTpc1);
    EXPECT_EQ(continuation->tpc2(), oldTpc2);
    EXPECT_NEAR(continuation->tuning(), oldTuning, 1e-9);
    score->undoRedo(false, nullptr);
    EXPECT_EQ(continuation->meloNPer(), projected.nPer);
    EXPECT_EQ(continuation->meloNGen(), projected.nGen);
    EXPECT_EQ(continuation->pitch(), projected.midiKey);
    EXPECT_EQ(tpc2step(continuation->tpc1()), int(String(u"CDEFGAB").indexOf(Char(projected.step))));
    EXPECT_EQ(int(tpc2alter(continuation->tpc1())), projected.alter);
    EXPECT_EQ(continuation->tpc2(), continuation->tpc1());
    EXPECT_EQ((continuation->pitch() - projected.alter) / 12 - 1, projected.octave);
    EXPECT_NEAR(continuation->tuning(), projected.centsOffset, 1e-9);
    delete score;
}

TEST(MeloStaffTests, stateChangeKeepsAnExistingFullTieIdentityAtTheSameReference)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
    ASSERT_TRUE(score);
    Measure* m2 = measureNo(score, 2);
    ASSERT_TRUE(m2);
    String error;
    // Remove only the separately authored relative event.
    ASSERT_TRUE(test::relativeKey(score, 0, m2->tick(), 0, 0, error)) << error.toStdString();
    score->doLayout();

    auto notes = meloNotes(score);
    ASSERT_GE(notes.size(), 5u);
    Note* start = notes[3];
    Note* continuation = notes[4];
    const String sameState = meloStaffType(score)->meloStateJson();
    ASSERT_EQ(start->staff()->staffTypeForElement(start)->meloStateJson(), sameState);
    bool sameReference = false;
    ASSERT_TRUE(melo::sameReference(continuation->staff()->staffTypeForElement(continuation)->meloStateJson(), sameState, sameReference));
    ASSERT_TRUE(sameReference);
    double generatorCents = 0.0;
    double periodCents = 0.0;
    ASSERT_TRUE(melo::staffMetrics(sameState, generatorCents, periodCents));
    EXPECT_DOUBLE_EQ(generatorCents, 700.0);
    EXPECT_DOUBLE_EQ(periodCents, 1200.0);

    melo::SoundingPitch established;
    ASSERT_TRUE(melo::noteSoundingPitch(sameState, -6, 12, established));
    const int establishedTpc = step2tpc(int(String(u"CDEFGAB").indexOf(Char(established.step))),
                                        AccidentalVal(established.alter));
    for (Note* note : { start, continuation }) {
        note->setMeloPitch(-6, 12);
        note->setPitch(established.midiKey, establishedTpc, establishedTpc);
        note->setTuning(established.centsOffset);
    }
    Tie* tie = Factory::createTie(score->dummy());
    tie->setStartNote(start);
    tie->setEndNote(continuation);
    tie->setTrack(start->track());
    tie->setTick(start->tick());
    tie->setTick2(continuation->tick());
    score->startCmd(TranslatableString::untranslatable("same-reference tie fixture"));
    score->undoAddElement(tie);
    score->endCmd();
    ASSERT_TRUE(start->tieForNonPartial());
    ASSERT_EQ(start->tieForNonPartial()->endNote(), continuation);

    ASSERT_TRUE(melo::applyChange(score, 0, m2, u"mode:1", error)) << error.toStdString();
    const StaffType* newState = continuation->staff()->staffTypeForElement(continuation);
    ASSERT_TRUE(newState);
    EXPECT_TRUE((test::referenceNumber(newState->meloStateJson()) == 62));
    EXPECT_TRUE(newState->meloStateJson().contains(u"\"generator_cents\":700.0"));
    melo::SoundingPitch projected;
    ASSERT_TRUE(melo::noteSoundingPitch(newState->meloStateJson(), -6, 12, projected));
    EXPECT_EQ(start->meloNPer(), -6);
    EXPECT_EQ(start->meloNGen(), 12);
    EXPECT_EQ(continuation->meloNPer(), -6);
    EXPECT_EQ(continuation->meloNGen(), 12);
    EXPECT_NEAR(projected.frequencyHz, established.frequencyHz, 1e-9);
    EXPECT_EQ(continuation->pitch(), projected.midiKey);
    EXPECT_NEAR(continuation->tuning(), projected.centsOffset, 1e-9);

    score->undoRedo(true, nullptr);
    score->doLayout();
    EXPECT_EQ(continuation->meloNPer(), -6);
    EXPECT_EQ(continuation->meloNGen(), 12);
    EXPECT_EQ(continuation->pitch(), established.midiKey);
    EXPECT_NEAR(continuation->tuning(), established.centsOffset, 1e-9);
    score->undoRedo(false, nullptr);
    score->doLayout();
    EXPECT_EQ(continuation->meloNPer(), -6);
    EXPECT_EQ(continuation->meloNGen(), 12);
    EXPECT_EQ(continuation->pitch(), projected.midiKey);
    EXPECT_NEAR(continuation->tuning(), projected.centsOffset, 1e-9);

    const String path(u"tie-identity-regression-roundtrip.mscx");
    ASSERT_TRUE(ScoreRW::saveScore(score, path));
    Score* reopened = ScoreRW::readScore(path, true);
    ASSERT_TRUE(reopened);
    auto reopenedNotes = meloNotes(reopened);
    ASSERT_GE(reopenedNotes.size(), 5u);
    const Fraction tieStartTick = start->tick();
    const Fraction tieContinuationTick = continuation->tick();
    Note* reopenedContinuation = nullptr;
    for (Note* candidate : reopenedNotes) {
        Tie* incoming = candidate->tieBackNonPartial();
        if (candidate->tick() == tieContinuationTick && incoming && incoming->startNote()
            && incoming->startNote()->tick() == tieStartTick) {
            reopenedContinuation = candidate;
            break;
        }
    }
    ASSERT_TRUE(reopenedContinuation);
    ASSERT_TRUE(reopenedContinuation->tieBackNonPartial());
    ASSERT_TRUE(reopenedContinuation->tieBackNonPartial()->startNote());
    EXPECT_EQ(reopenedContinuation->meloNPer(), -6);
    EXPECT_EQ(reopenedContinuation->meloNGen(), 12);
    melo::SoundingPitch reopenedProjection;
    ASSERT_TRUE(melo::noteSoundingPitch(reopenedContinuation->staff()->staffTypeForElement(reopenedContinuation)->meloStateJson(),
                                        reopenedContinuation->meloNPer(), reopenedContinuation->meloNGen(), reopenedProjection));
    EXPECT_NEAR(reopenedProjection.frequencyHz, established.frequencyHz, 1e-9);
    EXPECT_EQ(reopenedContinuation->pitch(), reopenedProjection.midiKey);
    EXPECT_NEAR(reopenedContinuation->tuning(), reopenedProjection.centsOffset, 1e-9);
    delete reopened;
    delete score;
}

TEST(MeloStaffTests, syntheticTwoMeasureCommonTonePersistsItsExactContinuation)
{
    Score* score = syntheticCommonToneScore();
    ASSERT_TRUE(score);
    ASSERT_EQ(score->firstMeasure()->nextMeasure(), score->lastMeasure());
    std::vector<Note*> notes = meloNotes(score);
    ASSERT_EQ(notes.size(), 2u);
    ASSERT_TRUE(notes[0]->tieForNonPartial());
    ASSERT_EQ(notes[0]->tieForNonPartial()->endNote(), notes[1]);
    melo::SoundingPitch first;
    melo::SoundingPitch continuation;
    ASSERT_TRUE(melo::noteSoundingPitch(notes[0]->staff()->staffTypeForElement(notes[0])->meloStateJson(),
                                        notes[0]->meloNPer(), notes[0]->meloNGen(), first));
    ASSERT_TRUE(melo::noteSoundingPitch(notes[1]->staff()->staffTypeForElement(notes[1])->meloStateJson(),
                                        notes[1]->meloNPer(), notes[1]->meloNGen(), continuation));
    EXPECT_NEAR(first.frequencyHz, continuation.frequencyHz, 1e-9);
    EXPECT_NE(notes[0]->meloNGen(), notes[1]->meloNGen());

    const String path(u"synthetic-common-tone-roundtrip.mscx");
    ASSERT_TRUE(ScoreRW::saveScore(score, path));
    Score* reopened = ScoreRW::readScore(path, true);
    ASSERT_TRUE(reopened);
    std::vector<Note*> reopenedNotes = meloNotes(reopened);
    ASSERT_EQ(reopenedNotes.size(), 2u);
    melo::SoundingPitch reopenedContinuation;
    ASSERT_TRUE(melo::noteSoundingPitch(reopenedNotes[1]->staff()->staffTypeForElement(reopenedNotes[1])->meloStateJson(),
                                        reopenedNotes[1]->meloNPer(), reopenedNotes[1]->meloNGen(), reopenedContinuation));
    EXPECT_NEAR(first.frequencyHz, reopenedContinuation.frequencyHz, 1e-9);
    delete reopened;
    delete score;
}

TEST(MeloStaffTests, writeSyntheticCommonToneAcceptanceScore)
{
    const char* outDir = std::getenv("JIMS_NOTE_CONFORMANCE_OUT");
    if (!outDir) {
        GTEST_SKIP() << "set JIMS_NOTE_CONFORMANCE_OUT to write the common-tone acceptance score";
    }
    Score* score = syntheticCommonToneScore();
    ASSERT_TRUE(score);
    const String path = String::fromUtf8(outDir) + u"/common-tone-projection.mscx";
    ASSERT_TRUE(ScoreRW::saveScore(score, path));
    delete score;
}

TEST(MeloStaffTests, consecutiveStateChangesKeepAMultiSegmentTieExact)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
    ASSERT_TRUE(score);
    std::vector<Note*> notes = meloNotes(score);
    ASSERT_GE(notes.size(), 9u);
    Note* chain[] = { notes[3], notes[4], notes[8] };
    for (Note* note : chain) {
        melo::SoundingPitch reference, continuation;
        ASSERT_TRUE(melo::noteSoundingPitch(chain[0]->staff()->staffTypeForElement(chain[0])->meloStateJson(), 0, 0, reference));
        ASSERT_TRUE(melo::noteContinuation(note->staff()->staffTypeForElement(note)->meloStateJson(), reference.frequencyHz, continuation));
        note->setMeloPitch(continuation.nPer, continuation.nGen);
        ASSERT_TRUE(note->setNval(note->noteVal()));
    }
    score->startCmd(TranslatableString::untranslatable("multi-segment tie fixture"));
    for (size_t i = 0; i < 2; ++i) {
        Tie* tie = Factory::createTie(score->dummy());
        tie->setStartNote(chain[i]);
        tie->setEndNote(chain[i + 1]);
        tie->setTrack(chain[i]->track());
        tie->setTick(chain[i]->tick());
        tie->setTick2(chain[i + 1]->tick());
        score->undoAddElement(tie);
    }
    score->endCmd();
    melo::SoundingPitch established;
    ASSERT_TRUE(melo::noteSoundingPitch(chain[0]->staff()->staffTypeForElement(chain[0])->meloStateJson(), 0, 0, established));
    String error;
    ASSERT_TRUE(melo::applyChange(score, 0, measureNo(score, 2), u"mode:1", error)) << error.toStdString();
    ASSERT_TRUE(test::relativeKey(score, 0, measureNo(score, 3)->tick(), 0, 1, error)) << error.toStdString();
    for (Note* note : chain) {
        melo::SoundingPitch projection;
        ASSERT_TRUE(melo::noteSoundingPitch(note->staff()->staffTypeForElement(note)->meloStateJson(),
                                            note->meloNPer(), note->meloNGen(), projection));
        EXPECT_NEAR(projection.frequencyHz, established.frequencyHz, 1e-9);
        EXPECT_EQ(note->pitch(), projection.midiKey);
        EXPECT_NEAR(note->tuning(), projection.centsOffset, 1e-9);
    }
    EXPECT_NE(chain[0]->meloNGen(), chain[2]->meloNGen());
    delete score;
}

TEST(MeloStaffTests, projectionFailureRollsBackStateAndEveryStoredField)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
    ASSERT_TRUE(score);
    Measure* changed = measureNo(score, 2);
    std::vector<Note*> notes = notesInMeasure(changed);
    ASSERT_FALSE(notes.empty());
    notes.front()->setMeloPitch(INT_MAX, INT_MAX);
    const String stateBefore = score->staff(0)->staffType(changed->tick())->meloStateJson();
    const auto noteBefore = std::make_tuple(notes.front()->meloNPer(), notes.front()->meloNGen(), notes.front()->pitch(),
                                            notes.front()->tpc1(), notes.front()->tpc2(), notes.front()->tuning());
    String error;
    EXPECT_FALSE(melo::applyChange(score, 0, changed, u"mode:1", error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_EQ(score->staff(0)->staffType(changed->tick())->meloStateJson(), stateBefore);
    EXPECT_EQ(std::make_tuple(notes.front()->meloNPer(), notes.front()->meloNGen(), notes.front()->pitch(),
                              notes.front()->tpc1(), notes.front()->tpc2(), notes.front()->tuning()), noteBefore);
    delete score;
}

TEST(MeloStaffTests, linkedNotesReceiveOneCoherentProjection)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
    ASSERT_TRUE(score);
    std::vector<Note*> notes = notesInMeasure(measureNo(score, 2));
    ASSERT_GE(notes.size(), 2u);
    notes[0]->setMeloPitch(0, 0);
    notes[1]->setMeloPitch(0, 0);
    notes[1]->setPitch(notes[0]->pitch(), notes[0]->tpc1(), notes[0]->tpc2());
    notes[1]->setTuning(notes[0]->tuning());
    notes[1]->linkTo(notes[0]);
    String error;
    ASSERT_TRUE(melo::applyChange(score, 0, measureNo(score, 2), u"mode:1", error)) << error.toStdString();
    EXPECT_EQ(std::make_tuple(notes[0]->meloNPer(), notes[0]->meloNGen(), notes[0]->pitch(), notes[0]->tpc1(),
                              notes[0]->tpc2(), notes[0]->tuning()),
              std::make_tuple(notes[1]->meloNPer(), notes[1]->meloNGen(), notes[1]->pitch(), notes[1]->tpc1(),
                              notes[1]->tpc2(), notes[1]->tuning()));
    delete score;
}

TEST(MeloStaffTests, relativeKeyRevisionPropagatesThroughALaterModeCarrier)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
    ASSERT_TRUE(score);
    Measure* m2 = measureNo(score, 2);
    Measure* m3 = measureNo(score, 3);
    String error;
    ASSERT_TRUE(melo::applyChange(score, 0, m3, u"mode:1", error)) << error.toStdString();
    const auto later = notesInMeasure(m3);
    ASSERT_FALSE(later.empty());
    std::vector<NoteVal> before;
    for (Note* note : later) {
        before.push_back(note->noteVal());
    }
    ASSERT_TRUE(test::relativeKey(score, 0, m2->tick(), 1, 0, error)) << error.toStdString();
    for (size_t i = 0; i < later.size(); ++i) {
        EXPECT_EQ(later[i]->meloNPer(), before[i].meloNPer);
        EXPECT_EQ(later[i]->meloNGen(), before[i].meloNGen);
        EXPECT_NE(later[i]->pitch(), before[i].pitch);
    }
    EXPECT_TRUE(score->staff(0)->staffType(m3->tick())->meloStateJson().contains(u"\"mode_rotation\":5"));
    score->undoRedo(true, nullptr);
    for (size_t i = 0; i < later.size(); ++i) {
        EXPECT_TRUE(later[i]->noteVal() == before[i]);
    }
    delete score;
}

TEST(MeloStaffTests, ambiguousPartialTieAcrossStateBoundaryIsRefusedWithoutMutation)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
    ASSERT_TRUE(score);
    std::vector<Note*> notes = meloNotes(score);
    ASSERT_GE(notes.size(), 5u);
    Note* start = notes[3];
    Note* continuation = notes[4];
    PartialTie* tie = Factory::createPartialTie(start);
    tie->setStartNote(start);
    tie->setEndNote(continuation);
    tie->setTrack(start->track());
    tie->setTick(start->tick());
    tie->setTick2(continuation->tick());
    score->startCmd(TranslatableString::untranslatable("partial tie fixture"));
    score->undoAddElement(tie);
    score->endCmd();
    Measure* m2 = measureNo(score, 2);
    const String stateBefore = score->staff(0)->staffType(m2->tick())->meloStateJson();
    const auto noteBefore = std::make_tuple(continuation->meloNPer(), continuation->meloNGen(), continuation->pitch(),
                                            continuation->tpc1(), continuation->tpc2(), continuation->tuning());
    String error;
    EXPECT_FALSE(melo::applyChange(score, 0, m2, u"mode:1", error));
    EXPECT_TRUE(error.contains(u"path-dependent partial tie")) << error.toStdString();
    EXPECT_EQ(score->staff(0)->staffType(m2->tick())->meloStateJson(), stateBefore);
    EXPECT_EQ(std::make_tuple(continuation->meloNPer(), continuation->meloNGen(), continuation->pitch(),
                              continuation->tpc1(), continuation->tpc2(), continuation->tuning()), noteBefore);
    delete score;
}

// Binding Requirement 2 (letter entry): typing a letter (with the input
// state's accidental) on a MeloPresto Staff enters THAT NOTE — its identity is
// established through the Kernel entry seam (Note::setNval), not read off
// a stock-clef line position (a MeloPresto Staff has no clef; its lines are 50 cents
// apart). D-sharp typed = D#4 = identity (-4, 7); E-flat = (3, -5); C = (1, -2).
TEST(MeloStaffTests, m6LetterEntryEstablishesTheKernelIdentityOfTheNamedNote)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/jims-template.mscx");
    ASSERT_TRUE(score);
    score->doLayout();
    ASSERT_TRUE(meloStaffType(score)->isMelo());
    InputState& is = score->inputState();
    is.setTrack(0);
    is.setSegment(score->tick2segment(Fraction(0, 1), false, SegmentType::ChordRest));
    is.setDuration(DurationType::V_QUARTER);
    is.setNoteEntryMode(true);

    struct Entry {
        int letter;
        AccidentalType acc;
        int pitch;
        int nPer;
        int nGen;
    };
    // step = octave*7 + letter, as Score::resolveNoteInputParams produces
    // (MuseScore's octave index is pitch/12, so C4 = 60 is octave 5: step 35).
    const Entry entries[] = {
        { 1, AccidentalType::SHARP, 63, -4, 7 },     // D#4
        { 2, AccidentalType::FLAT, 63, 3, -5 },      // Eb4
        { 0, AccidentalType::NONE, 60, 1, -2 },      // C4
        { 5, AccidentalType::NONE, 69, 0, 1 },       // A4
    };
    for (const Entry& e : entries) {
        score->startCmd(TranslatableString::untranslatable("M6 test entry"));
        is.setAccidentalType(e.acc);
        score->cmdAddPitch(5 * 7 + e.letter, false, false);
        score->endCmd();
    }
    score->doLayout();
    auto notes = meloNotes(score);
    ASSERT_EQ(notes.size(), 4u);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(notes[i]->pitch(), entries[i].pitch) << "note " << i;
        EXPECT_TRUE(notes[i]->hasMeloPitch()) << "note " << i;
        EXPECT_EQ(notes[i]->meloNPer(), entries[i].nPer) << "note " << i;
        EXPECT_EQ(notes[i]->meloNGen(), entries[i].nGen) << "note " << i;
    }
    delete score;
}

TEST(MeloStaffTests, accidentalEditingKeepsKernelIdentityAndPlaybackTogether)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/jims-template.mscx");
    ASSERT_TRUE(score);
    score->doLayout();
    InputState& input = score->inputState();
    input.setTrack(0);
    input.setSegment(score->tick2segment(Fraction(0, 1), false, SegmentType::ChordRest));
    input.setDuration(DurationType::V_QUARTER);
    input.setNoteEntryMode(true);
    score->startCmd(TranslatableString::untranslatable("enter G4"));
    score->cmdAddPitch(5 * 7 + 4, false, false);
    score->endCmd();
    input.setNoteEntryMode(false);
    score->doLayout();
    auto notes = meloNotes(score);
    ASSERT_EQ(notes.size(), 1u);
    Note* note = notes.front();
    for (auto accidental : { AccidentalType::SHARP, AccidentalType::NATURAL, AccidentalType::FLAT }) {
        melo::SoundingPitch expected;
        ASSERT_TRUE(melo::entryFromStandardPitch(meloStaffType(score)->meloStateJson(), 'G',
                                                 int(Accidental::subtype2value(accidental)), 4, expected));
        const auto before = std::make_tuple(note->meloNPer(), note->meloNGen(), note->pitch(), note->tuning());
        score->startCmd(TranslatableString::untranslatable("edit accidental"));
        score->changeAccidental(note, accidental);
        score->endCmd();
        EXPECT_EQ(note->meloNPer(), expected.nPer);
        EXPECT_EQ(note->meloNGen(), expected.nGen);
        EXPECT_EQ(note->pitch(), expected.midiKey);
        EXPECT_NEAR(note->tuning(), expected.centsOffset, 1e-9);
        score->undoRedo(true, nullptr);
        EXPECT_EQ(std::make_tuple(note->meloNPer(), note->meloNGen(), note->pitch(), note->tuning()), before);
        score->undoRedo(false, nullptr);
        EXPECT_EQ(note->meloNPer(), expected.nPer);
        EXPECT_EQ(note->meloNGen(), expected.nGen);
        EXPECT_EQ(note->pitch(), expected.midiKey);
    }
    delete score;
}

TEST(MeloStaffTests, explicitNaturalEntryOverridesEarlierSharpInTheMeasure)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/jims-template.mscx");
    ASSERT_TRUE(score);
    score->doLayout();
    InputState& input = score->inputState();
    input.setTrack(0);
    input.setSegment(score->tick2segment(Fraction(0, 1), false, SegmentType::ChordRest));
    input.setDuration(DurationType::V_QUARTER);
    input.setNoteEntryMode(true);
    for (int i = 0; i < 4; ++i) {
        score->startCmd(TranslatableString::untranslatable("enter accidental sequence"));
        input.setAccidentalType(i % 2 ? AccidentalType::NATURAL : AccidentalType::SHARP);
        score->cmdAddPitch(4 * 7 + (i < 2 ? 4 : 3), false, false);
        score->endCmd();
    }
    auto notes = meloNotes(score);
    ASSERT_EQ(notes.size(), 4u);
    for (size_t i = 0; i < notes.size(); ++i) {
        melo::SoundingPitch expected;
        ASSERT_TRUE(melo::entryFromStandardPitch(meloStaffType(score)->meloStateJson(), i < 2 ? 'G' : 'F',
                                                 i % 2 ? 0 : 1, 3, expected));
        EXPECT_EQ(notes[i]->pitch(), expected.midiKey) << i;
        EXPECT_EQ(notes[i]->meloNPer(), expected.nPer) << i;
        EXPECT_EQ(notes[i]->meloNGen(), expected.nGen) << i;
    }
    delete score;
}

TEST(MeloStaffTests, conventionalEntryUsesTheEffectivePostChangeState)
{
    Score* score = ScoreRW::readScore(u"jimstaff_data/jims-template.mscx");
    ASSERT_TRUE(score);
    String error;
    ASSERT_TRUE(melo::validateState(score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), error)) << error.toStdString();
    ASSERT_TRUE(test::relativeKey(score, 0, measureNo(score, 2)->tick(), -1, 3, error)) << error.toStdString();
    const StaffType* state = score->staff(0)->staffType(measureNo(score, 2)->tick());
    ASSERT_TRUE(state && state->isMelo());
    melo::SoundingPitch expected;
    ASSERT_TRUE(melo::entryFromStandardPitch(state->meloStateJson(), 'D', 0, 4, expected, &error)) << error.toStdString();

    InputState& input = score->inputState();
    input.setTrack(0);
    input.setSegment(score->tick2segment(measureNo(score, 2)->tick(), false, SegmentType::ChordRest));
    input.setDuration(DurationType::V_HALF);
    input.setAccidentalType(AccidentalType::NONE);
    input.setNoteEntryMode(true);
    score->startCmd(TranslatableString::untranslatable("post-change conventional entry"));
    score->cmdAddPitch(5 * 7 + 1, false, false);
    score->endCmd();
    input.setNoteEntryMode(false);
    std::vector<Note*> notes = notesInMeasure(measureNo(score, 2));
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0]->meloNPer(), expected.nPer);
    EXPECT_EQ(notes[0]->meloNGen(), expected.nGen);
    EXPECT_EQ(notes[0]->pitch(), expected.midiKey);
    EXPECT_NEAR(notes[0]->tuning(), expected.centsOffset, 1e-9);
    delete score;
}

// Phase 7 — the editing-only scenario: starting from the MeloPresto Staff template
// (no XML edit, no converter, no caller-authored state JSON), reproduce the
// accepted m5-key-mode piece through the new commands alone — bind the
// reference at bar 1, type the notes (letters + accidentals, chords by
// Shift+letter), insert mode Do->La and key Do0->La0 at bar 2 through the
// change controller — then save; the harness renders and hashes it against
// jims-evidence/m5-acceptance/m5-key-mode/initial-1.png.
TEST(MeloStaffTests, m6WriteEditingScenario)
{
    const char* gate = std::getenv("JIMS_M6_SCENARIO");
    if (!gate || std::string(gate) != "1") {
        GTEST_SKIP() << "set JIMS_M6_SCENARIO=1 and JIMS_SWEEP_OUT to write the M6 editing scenario";
    }
    const char* outDir = std::getenv("JIMS_SWEEP_OUT");
    ASSERT_TRUE(outDir);
    const muse::String out = muse::String::fromUtf8(outDir);

    // The template as the wizard installs it (.mscz: score + its style).
    Score* score = ScoreRW::readScore(u"jimstaff_data/jims-template.mscz");
    ASSERT_TRUE(score);
    score->doLayout();
    ASSERT_TRUE(meloStaffType(score)->isMelo());

    // Title (the fixture's) — ordinary metadata/text editing.
    score->startCmd(TranslatableString::untranslatable("M6 scenario title"));
    score->setMetaTag(u"workTitle", u"Sharp-Flat Collision and Second Cluster");
    for (MeasureBase* mb = score->first(); mb; mb = mb->next()) {
        if (mb->isVBox()) {
            for (EngravingItem* e : mb->el()) {
                if (e->isText() && toText(e)->textStyleType() == TextStyleType::TITLE) {
                    toText(e)->undoChangeProperty(Pid::TEXT, muse::String(u"Sharp-Flat Collision and Second Cluster"));
                }
            }
        }
    }
    score->endCmd();

    // Bar 1: bind Re0 to key number 62 (the base state).
    muse::String error;
    ASSERT_TRUE(melo::validateState(score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), error)) << error.toStdString();

    // Notes, typed. Letters as Score::resolveNoteInputParams' step (octave index = pitch/12).
    InputState& is = score->inputState();
    is.setTrack(0);
    is.setSegment(score->tick2segment(Fraction(0, 1), false, SegmentType::ChordRest));
    is.setNoteEntryMode(true);
    auto type = [&](DurationType d, int letter, int octaveIndex, AccidentalType acc, bool addToChord) {
        score->startCmd(TranslatableString::untranslatable("M6 scenario note"));
        is.setDuration(d);
        is.setAccidentalType(acc);
        score->cmdAddPitch(octaveIndex * 7 + letter, addToChord, false);
        score->endCmd();
    };
    // Bar 1: D#4 Eb4 D#4 Eb4 (quarters).
    type(DurationType::V_QUARTER, 1, 5, AccidentalType::SHARP, false);
    type(DurationType::V_QUARTER, 2, 5, AccidentalType::FLAT, false);
    type(DurationType::V_QUARTER, 1, 5, AccidentalType::SHARP, false);
    type(DurationType::V_QUARTER, 2, 5, AccidentalType::FLAT, false);
    // Bar 2: [D#4 + Eb4] half (the collision), then the second cluster
    // [C4 D4 E4 F4] half — chord members by Shift+letter.
    type(DurationType::V_HALF, 1, 5, AccidentalType::SHARP, false);
    type(DurationType::V_HALF, 2, 5, AccidentalType::FLAT, true);
    type(DurationType::V_HALF, 0, 5, AccidentalType::NONE, false);
    type(DurationType::V_HALF, 1, 5, AccidentalType::NONE, true);
    type(DurationType::V_HALF, 2, 5, AccidentalType::NONE, true);
    type(DurationType::V_HALF, 3, 5, AccidentalType::NONE, true);
    // Bar 3: C4 half, C5 half.
    type(DurationType::V_HALF, 0, 5, AccidentalType::NONE, false);
    type(DurationType::V_HALF, 0, 6, AccidentalType::NONE, false);
    is.setNoteEntryMode(false);
    score->doLayout();
    auto notes = meloNotes(score);
    ASSERT_EQ(notes.size(), 12u);

    // Bar 2: mode Do -> La, key Do0 -> La0 (the owner's worked example).
    Measure* m2 = measureNo(score, 2);
    ASSERT_TRUE(melo::applyChange(score, 0, m2, u"mode:1", error)) << error.toStdString();
    ASSERT_TRUE(test::relativeKey(score, 0, m2->tick(), -1, 3, error)) << error.toStdString();
    score->doLayout();
    melo::ChangeIndicator model;
    ASSERT_TRUE(melo::midSystemChangeIndicator(m2, 0, model));
    ASSERT_EQ(model.kinds.size(), 2u);

    ASSERT_TRUE(ScoreRW::saveScore(score, out + u"/m6-key-mode.mscx"));
    // Semantics + identities record.
    std::ofstream rec(std::string(outDir) + "/m6-key-mode-semantics.json");
    rec << "{\"base\":" << meloStaffType(score)->meloStateJson().toStdString()
        << ",\"change\":" << melo::changeCarrier(m2, 0)->staffType()->meloStateJson().toStdString()
        << ",\"kinds\":[";
    for (size_t i = 0; i < model.kinds.size(); ++i) {
        rec << (i ? "," : "") << "\"" << model.kinds[i].toStdString() << "\"";
    }
    rec << "],\"arrows\":" << model.arrows.size() << ",\"identities\":[";
    for (size_t i = 0; i < notes.size(); ++i) {
        rec << (i ? "," : "") << "[" << notes[i]->meloNPer() << "," << notes[i]->meloNGen() << "," << notes[i]->pitch() << "]";
    }
    rec << "]}\n";
    delete score;
}

TEST(MeloStaffTests, changeTerrainLabelsOnlyTheNewTonicAndSeparatesCompoundArrows)
{
    struct Case {
        const char* name;
        bool key;
        bool mode;
        size_t arrows;
        String tonic;
    };
    for (int placement : { 0, 1, 2 }) {
        SCOPED_TRACE(placement); // mid-bar, bar boundary, end-of-system courtesy
        for (const Case& c : { Case { "combined", true, true, 2, u"Do" },
                               Case { "key-only", true, false, 1, u"La" },
                               Case { "mode-only", false, true, 1, u"Do" } }) {
            SCOPED_TRACE(c.name);
            Score* score = ScoreRW::readScore(u"jimstaff_data/collision.mscx");
            ASSERT_TRUE(score);
            Measure* first = measureNo(score, 1);
            Measure* measure = measureNo(score, 2);
            String error;
            ASSERT_TRUE(melo::validateState(score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), error));
            ASSERT_TRUE(melo::applyChange(score, 0, first, u"mode:1", error));
            const Fraction tick = placement == 0 ? notesInMeasure(measure).back()->tick() : measure->tick();
            if (placement == 2) {
                auto lineBreak = Factory::createLayoutBreak(first);
                lineBreak->setLayoutBreakType(LayoutBreakType::LINE);
                lineBreak->setTrack(0);
                first->add(lineBreak);
            }
            std::vector<String> choices;
            if (c.key) {
                ASSERT_TRUE(test::relativeKey(score, 0, tick, 0, 1, error));
            }
            if (c.mode) {
                choices.push_back(u"mode:-2");
            }
            ASSERT_TRUE(melo::applyChangeToAllMeloParts(score, measure, tick, choices, error));
            score->doLayout();
            melo::ChangeIndicator model;
            const StaffType* incoming = nullptr;
            if (placement == 0) {
                ASSERT_TRUE(melo::midBarChangeIndicator(melo::changeCarrierAt(measure, 0, tick), model, &incoming));
            } else if (placement == 1) {
                ASSERT_TRUE(melo::midSystemChangeIndicator(measure, 0, model, &incoming));
            } else {
                ASSERT_NE(first->system(), measure->system());
                ASSERT_TRUE(melo::courtesyChangeIndicator(first, 0, model));
                incoming = score->staff(0)->staffType(tick);
            }
            ASSERT_EQ(model.arrows.size(), c.arrows);
            auto provider = std::make_shared<BufferedPaintProvider>();
            Painter painter(provider, "change-label-arrows");
            painter.setViewport(RectF(0, 0, 4000, 4000));
            const StaffLines* lines = (placement == 2 ? first : measure)->staffLines(0);
            lines->renderer()->drawItem(lines, &painter, PaintOptions());
            painter.endDraw();
            melo::ConnectorGlyph head;
            ASSERT_TRUE(melo::connectorGlyph(head));
            const double dist = incoming->lineDistance().val() * lines->spatium();
            const double shaftWidth = head.penCents / StaffType::MELO_CENTS_PER_LINE_DISTANCE * dist;
            std::vector<double> shafts;
            size_t pitchLabels = 0;
            const auto drawing = provider->drawData();
            std::function<void(const DrawData::Item&)> inspect = [&](const DrawData::Item& item) {
                for (const auto& data : item.datas) {
                    const auto& state = drawing->states.at(data.state);
                    for (const auto& text : data.texts) {
                        if (text.text.contains(u": ") && text.rect.x() >= 0.0) {
                            ++pitchLabels;
                            EXPECT_TRUE(text.text.endsWith(u": " + c.tonic)) << text.text.toStdString();
                            EXPECT_TRUE(text.text.startsWith(c.key ? u"G" : u"C")) << text.text.toStdString();
                        }
                    }
                    for (const auto& polygon : data.polygons) {
                        if (polygon.mode == PolygonMode::Polyline && polygon.polygon.size() == 2
                            && std::abs(polygon.polygon[0].x() - polygon.polygon[1].x()) < 1e-6
                            && state.pen.capStyle() == PenCapStyle::RoundCap
                            && std::abs(state.pen.widthF() - shaftWidth) < 1e-6) {
                            shafts.push_back(polygon.polygon[0].x());
                        }
                    }
                }
                for (const auto& child : item.chilren) {
                    inspect(child);
                }
            };
            inspect(drawing->item);
            // A key-only change need not include the tonic among its two dots.
            EXPECT_EQ(pitchLabels, c.mode ? 1u : 0u);
            ASSERT_EQ(shafts.size(), c.arrows);
            if (shafts.size() == 2) {
                const double minimumGap = 2.0 * head.headHalfWidthCents
                                          / StaffType::MELO_CENTS_PER_LINE_DISTANCE * dist;
                EXPECT_GT(std::abs(shafts[1] - shafts[0]), minimumGap);
            }
            // Owner rule 2026-09-12 (2a), from the corpus census (mode-only is
            // the rarest kind in every corpus): the mode arrow sits LEFT of the
            // dots, between the dots and their labels; the key arrow sits RIGHT
            // of the dots. The dot column is located from the terrain's flank
            // strokes (bar-width pen, flat caps) through the one shared geometry.
            const double flankWidth = score->style().styleMM(Sid::barWidth);
            const double minShaft = *std::min_element(shafts.begin(), shafts.end());
            const double maxShaft = *std::max_element(shafts.begin(), shafts.end());
            double leftFlank = -1e9;
            double rightFlank = 1e9;
            std::function<void(const DrawData::Item&)> flanks = [&](const DrawData::Item& item) {
                for (const auto& data : item.datas) {
                    const auto& state = drawing->states.at(data.state);
                    for (const auto& polygon : data.polygons) {
                        if (polygon.mode == PolygonMode::Polyline && polygon.polygon.size() == 2
                            && std::abs(polygon.polygon[0].x() - polygon.polygon[1].x()) < 1e-6
                            && state.pen.capStyle() == PenCapStyle::FlatCap
                            && std::abs(state.pen.widthF() - flankWidth) < 1e-6) {
                            const double x = polygon.polygon[0].x();
                            if (x < minShaft) {
                                leftFlank = std::max(leftFlank, x);
                            } else if (x > maxShaft) {
                                rightFlank = std::min(rightFlank, x);
                            }
                        }
                    }
                }
                for (const auto& child : item.chilren) {
                    flanks(child);
                }
            };
            flanks(drawing->item);
            const StaffType::MeloHeaderGeometry g
                = melo::changeTerrainGeometry(incoming, lines->spatium(), score->style().defaultSpatium(), model);
            const double sp = lines->spatium();
            double dotCenterX = 0.0;
            if (placement == 2) {
                ASSERT_GT(leftFlank, -1e9) << "courtesy terrain has its added stroke on the left";
                dotCenterX = leftFlank + 0.3 * sp + g.changeLabelBand + g.changeLeftArrowLane + g.indicatorW;
            } else {
                ASSERT_LT(rightFlank, 1e9) << "start-of-bar and mid-bar terrains close with a stroke on the right";
                dotCenterX = rightFlank - 0.3 * sp - g.changeArrowLane - g.changeRightLabelBand - g.indicatorW;
                if (placement == 0) {
                    ASSERT_GT(leftFlank, -1e9);
                    EXPECT_NEAR(rightFlank - leftFlank, g.changeTerrainWidth, 1e-6) << "the two dashed flanks span the terrain";
                    // Owner finding 2026-09-12: the terrain is placed so that its
                    // closing flank stays a bar-note gap left of the note it
                    // precedes, whatever the terrain's lane count.
                    const Segment* anchor = measure->findSegmentR(Segment::CHORD_REST_OR_TIME_TICK_TYPE, tick - measure->tick());
                    ASSERT_TRUE(anchor);
                    EXPECT_LE(rightFlank, anchor->x() - score->style().styleMM(Sid::barNoteDistance) + 1e-6)
                        << "mid-bar terrain overlaps the note it precedes";
                }
            }
            const double dotLeft = dotCenterX - g.indicatorW;
            const double dotRight = dotCenterX + g.indicatorW;
            for (size_t i = 0; i < model.arrows.size(); ++i) {
                SCOPED_TRACE(model.arrows[i].kind.toStdString());
                if (model.arrows[i].kind == u"mode") {
                    EXPECT_GT(shafts[i], dotLeft - g.changeLeftArrowLane - 1e-6) << "mode arrow inside its lane left of the dots";
                    EXPECT_LT(shafts[i], dotLeft + 1e-6) << "mode arrow left of the dots";
                } else {
                    EXPECT_GT(shafts[i], dotRight + g.changeRightLabelBand - 1e-6) << "key arrow right of the dots";
                    EXPECT_LT(shafts[i], dotRight + g.changeRightLabelBand + g.changeArrowLane + 1e-6)
                        << "key arrow inside its lane";
                }
            }
            delete score;
        }
    }
}
