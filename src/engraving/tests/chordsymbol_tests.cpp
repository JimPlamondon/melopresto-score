/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

#include <gtest/gtest.h>

#include <QMimeData>

#include "engraving/api/v1/apitypes.h"
#include "engraving/compat/scoreaccess.h"
#include "engraving/dom/chordrest.h"
#include "engraving/dom/durationtype.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/fret.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/editing/editdata.h"
#include "engraving/editing/transpose.h"
#include "engraving/internal/qmimedataadapter.h"

#include "utils/scorerw.h"
#include "utils/scorecomp.h"

using namespace mu::engraving;

static constexpr double POSITION_ERROR = 1e-6;

static const String CHORDSYMBOL_DATA_DIR("chordsymbol_data/");

class Engraving_ChordSymbolTests : public ::testing::Test
{
public:
    MasterScore* test_pre(const char16_t* p);
    void test_post(MasterScore* score, const char16_t* p);

    void selectAllChordSymbols(MasterScore* score);
    void realizeSelectionVoiced(MasterScore* score, Voicing voicing);
};

MasterScore* Engraving_ChordSymbolTests::test_pre(const char16_t* p)
{
    String p1 = CHORDSYMBOL_DATA_DIR + p + ".mscx";
    MasterScore* score = ScoreRW::readScore(p1);
    EXPECT_TRUE(score);
    score->doLayout();
    return score;
}

void Engraving_ChordSymbolTests::test_post(MasterScore* score, const char16_t* p)
{
    String p1 = p;
    p1 += u"-test.mscx";
    String p2 = CHORDSYMBOL_DATA_DIR + p + u"-ref.mscx";
    EXPECT_TRUE(ScoreComp::saveCompareScore(score, p1, p2));
    delete score;
}

//---------------------------------------------------------
//   select all chord symbols within the specified score
//---------------------------------------------------------
void Engraving_ChordSymbolTests::selectAllChordSymbols(MasterScore* score)
{
    //find a chord symbol
    Segment* seg = score->firstSegment(SegmentType::ChordRest);
    EngravingItem* e = 0;
    while (seg) {
        e = seg->findAnnotation(ElementType::HARMONY,
                                0, score->ntracks());
        if (e) {
            break;
        }
        seg = seg->next1();
    }
    score->selectSimilar(e, false);
}

//---------------------------------------------------------
//   realize the current selection of the score using
//   the specified voicing
//---------------------------------------------------------
void Engraving_ChordSymbolTests::realizeSelectionVoiced(MasterScore* score, Voicing voicing)
{
    for (EngravingItem* e : score->selection().elements()) {
        if (e->isHarmony()) {
            e->setProperty(Pid::HARMONY_VOICING, int(voicing));
        }
    }
    score->startCmd(TranslatableString::untranslatable("Realize selection voiced"));
    score->cmdRealizeChordSymbols();
    score->endCmd();
}

TEST_F(Engraving_ChordSymbolTests, testExtend)
{
    MasterScore* score = test_pre(u"extend");
    Measure* m = score->firstMeasure();
    Segment* s = m->first(SegmentType::ChordRest);
    ChordRest* cr = s->cr(0);
    score->changeCRlen(cr, DurationType::V_WHOLE);
    score->doLayout();
    test_post(score, u"extend");
}

TEST_F(Engraving_ChordSymbolTests, testClear)
{
    MasterScore* score = test_pre(u"clear");
    Measure* m = score->firstMeasure();
    score->select(m, SelectType::SINGLE, 0);
    score->cmdDeleteSelection();
    score->doLayout();
    test_post(score, u"clear");
}

TEST_F(Engraving_ChordSymbolTests, testAddLink)
{
    MasterScore* score = test_pre(u"add-link");
    Segment* seg = score->firstSegment(SegmentType::ChordRest);
    ChordRest* cr = seg->cr(0);
    Harmony* harmony = new Harmony(cr->segment());
    harmony->setHarmony(u"C7");
    harmony->setTrack(cr->track());
    harmony->setParent(cr->segment());
    score->undoAddElement(harmony);
    score->doLayout();
    test_post(score, u"add-link");
}

TEST_F(Engraving_ChordSymbolTests, harmonyEditKeepsDisplayPosition)
{
    MasterScore* score = test_pre(u"add-link");
    ASSERT_TRUE(score);

    Segment* seg = score->firstSegment(SegmentType::ChordRest);
    ASSERT_TRUE(seg);
    ChordRest* cr = seg->cr(0);
    ASSERT_TRUE(cr);

    Harmony* harmony = new Harmony(cr->segment());
    harmony->setHarmony(u"C");
    harmony->setTrack(cr->track());
    harmony->setParent(cr->segment());
    score->undoAddElement(harmony);
    score->doLayout();

    const double x = harmony->canvasPos().x();
    const double y = harmony->canvasPos().y();

    EditData ed;
    harmony->startEdit(ed);

    EXPECT_NEAR(harmony->canvasPos().x(), x, POSITION_ERROR);
    EXPECT_NEAR(harmony->canvasPos().y(), y, POSITION_ERROR);

    ed.s = String(u"7");
    harmony->cursor()->moveCursorToEnd();
    harmony->edit(ed);

    EXPECT_NEAR(harmony->canvasPos().x(), x, POSITION_ERROR);
    EXPECT_NEAR(harmony->canvasPos().y(), y, POSITION_ERROR);

    harmony->endEdit(ed);
    delete score;
}

TEST_F(Engraving_ChordSymbolTests, testAddPart)
{
    MasterScore* score = test_pre(u"add-part");
    Segment* seg = score->firstSegment(SegmentType::ChordRest);
    ChordRest* cr = seg->cr(0);
    Harmony* harmony = new Harmony(cr->segment());
    harmony->setHarmony(u"C7");
    harmony->setTrack(cr->track());
    harmony->setParent(cr->segment());
    score->undoAddElement(harmony);
    score->doLayout();
    test_post(score, u"add-part");
}

TEST_F(Engraving_ChordSymbolTests, testNoSystem)
{
    MasterScore* score = test_pre(u"no-system");

    //
    // create first part
    //
    std::vector<Part*> parts;
    parts.push_back(score->parts().at(0));
    Score* nscore = score->createScore();

    Excerpt* ex = new Excerpt(score);
    ex->setExcerptScore(nscore);
    nscore->setExcerpt(ex);
    score->excerpts().push_back(ex);
    ex->setName(parts.front()->longName());
    ex->setParts(parts);
    Excerpt::createExcerpt(ex);

//      nscore->setTitle(parts.front()->partName());
    nscore->style().set(Sid::createMultiMeasureRests, true);

    //
    // create second part
    //
    parts.clear();
    parts.push_back(score->parts().at(1));
    nscore = score->createScore();

    ex = new Excerpt(score);
    ex->setExcerptScore(nscore);
    nscore->setExcerpt(ex);
    score->excerpts().push_back(ex);
    ex->setName(parts.front()->longName());
    ex->setParts(parts);
    Excerpt::createExcerpt(ex);

//      nscore->setTitle(parts.front()->partName());
    nscore->style().set(Sid::createMultiMeasureRests, true);

    score->setExcerptsChanged(true);
    score->doLayout();
    test_post(score, u"no-system");
}

TEST_F(Engraving_ChordSymbolTests, testTranspose)
{
    MasterScore* score = test_pre(u"transpose");
    score->startCmd(TranslatableString::untranslatable("Engraving chord symbol tests"));
    score->cmdSelectAll();
    Transpose::transpose(score, TransposeMode::BY_INTERVAL, TransposeDirection::UP, Key::C, 4, false, true, true);
    score->endCmd();
    test_post(score, u"transpose");
}

TEST_F(Engraving_ChordSymbolTests, testTransposePart)
{
    MasterScore* score = test_pre(u"transpose-part");
    score->startCmd(TranslatableString::untranslatable("Engraving chord symbol tests"));
    score->cmdSelectAll();
    Transpose::transpose(score, TransposeMode::BY_INTERVAL, TransposeDirection::UP, Key::C, 4, false, true, true);
    score->endCmd(false, /*layoutAllParts = */ true);
    test_post(score, u"transpose-part");
}

//---------------------------------------------------------
//   check close voicing algorithm
//---------------------------------------------------------
TEST_F(Engraving_ChordSymbolTests, testRealizeClose)
{
    MasterScore* score = test_pre(u"realize");
    selectAllChordSymbols(score);
    realizeSelectionVoiced(score, Voicing::CLOSE);
    test_post(score, u"realize-close");
}

//---------------------------------------------------------
//   check Drop 2 voicing algorithm
//---------------------------------------------------------
TEST_F(Engraving_ChordSymbolTests, testRealizeDrop2)
{
    MasterScore* score = test_pre(u"realize");
    selectAllChordSymbols(score);
    realizeSelectionVoiced(score, Voicing::DROP_2);
    test_post(score, u"realize-drop2");
}

//---------------------------------------------------------
//   check 3 note voicing algorithm
//---------------------------------------------------------
TEST_F(Engraving_ChordSymbolTests, testRealize3Note)
{
    MasterScore* score = test_pre(u"realize");
    selectAllChordSymbols(score);
    realizeSelectionVoiced(score, Voicing::THREE_NOTE);
    test_post(score, u"realize-3note");
}

//---------------------------------------------------------
//   check 4 note voicing algorithm
//---------------------------------------------------------
TEST_F(Engraving_ChordSymbolTests, testRealize4Note)
{
    MasterScore* score = test_pre(u"realize");
    selectAllChordSymbols(score);
    realizeSelectionVoiced(score, Voicing::FOUR_NOTE);
    test_post(score, u"realize-4note");
}

//---------------------------------------------------------
//   check 6 note voicing algorithm
//---------------------------------------------------------
TEST_F(Engraving_ChordSymbolTests, testRealize6Note)
{
    MasterScore* score = test_pre(u"realize");
    selectAllChordSymbols(score);
    realizeSelectionVoiced(score, Voicing::SIX_NOTE);
    test_post(score, u"realize-6note");
}

//---------------------------------------------------------
//   Check if the note pitches and tpcs are correct after realizing
//   chord symbols on transposed instruments.
//---------------------------------------------------------
TEST_F(Engraving_ChordSymbolTests, testRealizeConcertPitch)
{
    MasterScore* score = test_pre(u"realize-concert-pitch");
    //concert pitch off
    score->startCmd(TranslatableString::untranslatable("Engraving chord symbol tests"));
    score->cmdConcertPitchChanged(false);
    score->endCmd();

    //realize all chord symbols
    selectAllChordSymbols(score);
    score->startCmd(TranslatableString::untranslatable("Engraving chord symbol tests"));
    score->cmdRealizeChordSymbols();
    score->endCmd();
    test_post(score, u"realize-concert-pitch");
}

//---------------------------------------------------------
//   Check if the note pitches and tpcs are correct after
//   transposing the score
//---------------------------------------------------------
TEST_F(Engraving_ChordSymbolTests, testRealizeTransposed)
{
    MasterScore* score = test_pre(u"transpose");
    //transpose
    score->cmdSelectAll();
    Transpose::transpose(score, TransposeMode::BY_INTERVAL, TransposeDirection::UP, Key::C, 4, false, true, true);

    //realize all chord symbols
    selectAllChordSymbols(score);
    score->startCmd(TranslatableString::untranslatable("Engraving chord symbol tests"));
    score->cmdRealizeChordSymbols();
    score->endCmd();
    test_post(score, u"realize-transpose");
}

//---------------------------------------------------------
//   Check for correctness when using the override
//   feature for realizing chord symbols
//---------------------------------------------------------
TEST_F(Engraving_ChordSymbolTests, testRealizeOverrides)
{
    MasterScore* score = test_pre(u"realize-override");
    //realize all chord symbols
    selectAllChordSymbols(score);
    score->startCmd(TranslatableString::untranslatable("Engraving chord symbol tests"));
    score->cmdRealizeChordSymbols(true, Voicing::ROOT_ONLY, HDuration::SEGMENT_DURATION);
    score->endCmd();
    test_post(score, u"realize-override");
}

//---------------------------------------------------------
//   Check for correctness when realizing chord symbols on triplets
//---------------------------------------------------------
TEST_F(Engraving_ChordSymbolTests, testRealizeTriplet)
{
    MasterScore* score = test_pre(u"realize-triplet");
    //realize all chord symbols
    selectAllChordSymbols(score);
    score->startCmd(TranslatableString::untranslatable("Engraving chord symbol tests"));
    score->cmdRealizeChordSymbols();
    score->endCmd();
    test_post(score, u"realize-triplet");
}

//---------------------------------------------------------
//   Check for correctness when realizing chord symbols
//   with different durations
//---------------------------------------------------------
TEST_F(Engraving_ChordSymbolTests, testRealizeDuration)
{
    MasterScore* score = test_pre(u"realize-duration");
    //realize all chord symbols
    selectAllChordSymbols(score);
    score->startCmd(TranslatableString::untranslatable("Engraving chord symbol tests"));
    score->cmdRealizeChordSymbols();
    score->endCmd();
    test_post(score, u"realize-duration");
}

//---------------------------------------------------------
//   Check for correctness when realizing chord symbols
//   with jazz mode
//---------------------------------------------------------
TEST_F(Engraving_ChordSymbolTests, testRealizeJazz)
{
    MasterScore* score = test_pre(u"realize-jazz");
    //realize all chord symbols
    selectAllChordSymbols(score);
    score->startCmd(TranslatableString::untranslatable("Engraving chord symbol tests"));
    score->cmdRealizeChordSymbols();
    score->endCmd();
    test_post(score, u"realize-jazz");
}

TEST_F(Engraving_ChordSymbolTests, testNashvilleNumbers) {
    MasterScore* score = test_pre(u"nashville-numbers");
    selectAllChordSymbols(score);

    static const std::array<std::pair<int, String>, 23> tpcAndExtension { {
        { 14, u"" },
        { 16, u"" },
        { 18, u"" },
        { 13, u"" },
        { 16, u"" },
        { 18, u"" },
        { 20, u"" },
        { 15, u"" },
        { 10, u"" },
        { 12, u"" },
        { 14, u"" },
        { 9, u"" },
        { 5, u"" },
        { 7, u"" },
        { 9, u"" },
        { 4, u"" },
        { 12, u"m" },
        { 14, u"o" },
        { 16, u"o7#11" },
        { 14, u"6" },
        { 14, u"69" },
        { 18, u"sus" },
        { 13, u"6" }
    } };

    size_t idx = 0;

    std::vector<EngravingItem*> els = score->selection().elements(ElementType::HARMONY);

    ASSERT_EQ(els.size(), tpcAndExtension.size());

    for (EngravingItem* e : els) {
        Harmony* h = toHarmony(e);
        EXPECT_FALSE(h->chords().empty());
        HarmonyInfo* info = h->chords().front();

        int tpc = tpcAndExtension.at(idx).first;
        String ext = tpcAndExtension.at(idx).second;

        EXPECT_EQ(tpc, info->rootTpc());
        EXPECT_EQ(ext, info->textName());

        idx++;
    }
}

TEST_F(Engraving_ChordSymbolTests, testParserSuffix)
{
    MasterScore* score = compat::ScoreAccess::createMasterScore(nullptr);
    StringList symbols = { u"Cno3rd", u"Domit1st", u"Cadd9th", u"Ebsus2nd" };

    // Expect these symbols to parse successfully
    ParsedChord* pc = new ParsedChord();
    for (const String& chord : symbols) {
        EXPECT_TRUE(pc->parse(chord, score->chordList()));
    }

    delete pc;
    delete score;
}

TEST_F(Engraving_ChordSymbolTests, testAddHarmonyToFretDiagram)
{
    MasterScore* score = ScoreRW::readScore(CHORDSYMBOL_DATA_DIR + u"add-to-fret" + ".mscz");
    EXPECT_TRUE(score);
    score->doLayout();

    Measure* firstMeasure = score->firstMeasure();
    Segment* firstSeg = firstMeasure->findFirstR(SegmentType::ChordRest, Fraction(0, 1));
    FretDiagram* fretDiag = toFretDiagram(firstSeg->findAnnotation(ElementType::FRET_DIAGRAM, 0, 0));
    EXPECT_TRUE(fretDiag);

    score->addText(TextStyleType::HARMONY_A, fretDiag);

    EXPECT_TRUE(fretDiag->harmony());

    delete score;
}

TEST_F(Engraving_ChordSymbolTests, meloBassDegreesAndExceptionalNamesUseKernelValidation)
{
    for (const String& name : { String(u"Re:So7/3"), String(u"Do5/5"), String(u"Do:So5+M7/M7"), String(u"Ti:m3³/d7"),
                                String(u"Mi:So7+m9"), String(u"Fa5+A6,m7/5") }) {
        EXPECT_TRUE(Harmony::isValidMeloName(name)) << name.toStdString();
    }
    for (const String& name : { String(u"Re:So7/Fi"), String(u"Do5/Mi"), String(u"Do:So5+Fi/Ti"), String(u"Ti:m3³/Le"),
                                String(u"Mi:So7+Le"), String(u"Do5/0"), String(u"Do5/8"), String(u"Ti:m3³/3"), String(u"Do5/P8") }) {
        EXPECT_FALSE(Harmony::isValidMeloName(name)) << name.toStdString();
    }
}

TEST_F(Engraving_ChordSymbolTests, meloHarmonyCreationIsPerObjectUndoableCloneableAndAccessible)
{
    MasterScore* score = test_pre(u"add-link");
    ASSERT_TRUE(score);
    Segment* segment = score->firstSegment(SegmentType::ChordRest);
    ASSERT_TRUE(segment);
    ChordRest* chordRest = segment->cr(0);
    ASSERT_TRUE(chordRest);

    score->startCmd(TranslatableString::untranslatable("Add MeloPresto chord name"));
    Harmony* melo = score->addHarmony(HarmonyType::MELO, chordRest);
    ASSERT_TRUE(melo);
    melo->setHarmony(u"Fi@Te:So7+m9,m13/A4");
    score->endCmd();
    EXPECT_EQ(melo->harmonyType(), HarmonyType::MELO);
    EXPECT_EQ(melo->harmonyName(), u"Fi@Te:So7+m9,m13/A4");
    EXPECT_TRUE(melo->accessibleInfo().contains(u"MeloPresto chord name"));
    EXPECT_TRUE(melo->accessibleInfo().contains(u"Fi@Te:So7+m9,m13/A4"));
    EXPECT_TRUE(melo->screenReaderInfo().contains(u"Fi@Te:So7+m9,m13/A4"));

    Harmony* clone = melo->clone();
    ASSERT_TRUE(clone);
    EXPECT_EQ(clone->harmonyType(), HarmonyType::MELO);
    EXPECT_EQ(clone->harmonyName(), u"Fi@Te:So7+m9,m13/A4");
    delete clone;

    score->undoRedo(true, nullptr);
    EXPECT_FALSE(segment->findAnnotation(ElementType::HARMONY, chordRest->track(), chordRest->track() + 1));
    score->undoRedo(false, nullptr);
    EngravingItem* restored = segment->findAnnotation(ElementType::HARMONY, chordRest->track(), chordRest->track() + 1);
    ASSERT_TRUE(restored && restored->isHarmony());
    EXPECT_EQ(toHarmony(restored)->harmonyType(), HarmonyType::MELO);
    EXPECT_EQ(toHarmony(restored)->harmonyName(), u"Fi@Te:So7+m9,m13/A4");

    TextBase* standardText = score->addText(TextStyleType::HARMONY_A, chordRest);
    ASSERT_TRUE(standardText && standardText->isHarmony());
    EXPECT_EQ(toHarmony(standardText)->harmonyType(), HarmonyType::STANDARD);
    EXPECT_EQ(int(apiv1::enums::HarmonyType::MELO), int(HarmonyType::MELO));
    delete score;
}

TEST_F(Engraving_ChordSymbolTests, typedColonStaysInsideMeloHarmonyName)
{
    MasterScore* score = test_pre(u"add-link");
    ASSERT_TRUE(score);
    Segment* segment = score->firstSegment(SegmentType::ChordRest);
    ASSERT_TRUE(segment);
    ChordRest* chordRest = segment->cr(0);
    ASSERT_TRUE(chordRest);

    Harmony* melo = score->addHarmony(HarmonyType::MELO, chordRest);
    ASSERT_TRUE(melo);
    melo->setHarmony(u"So7/7");
    score->doLayout();
    EditData ed;
    melo->startEdit(ed);
    melo->cursor()->moveCursorToStart();
    ed.key = 0;
    ed.s = String(u"Mi");
    EXPECT_TRUE(melo->edit(ed));
    ed.key = Key_Colon;
    ed.s = String(u":");
    EXPECT_TRUE(melo->isEditAllowed(ed));
    EXPECT_TRUE(melo->edit(ed));
    melo->endEdit(ed);
    EXPECT_EQ(melo->harmonyName().toStdString(), "Mi:So7/7");
    EXPECT_EQ(melo->explicitParent(), segment);

    ed.key = Key_Semicolon;
    EXPECT_FALSE(melo->isEditAllowed(ed));
    {
        Harmony standard(segment);
        ed.key = Key_Colon;
        EXPECT_FALSE(standard.isEditAllowed(ed));
    }
    delete score;
}

TEST_F(Engraving_ChordSymbolTests, denseMeloHarmonyLabelsUseAlternatingVerticalLanes)
{
    MasterScore* score = test_pre(u"add-link");
    ASSERT_TRUE(score);
    score->style().set(Sid::verticallyAlignChordSymbols, true);

    Segment* firstSegment = score->firstSegment(SegmentType::ChordRest);
    Segment* secondSegment = firstSegment ? firstSegment->next(SegmentType::ChordRest) : nullptr;
    ASSERT_TRUE(firstSegment && secondSegment);
    ChordRest* firstChordRest = firstSegment->cr(0);
    ChordRest* secondChordRest = secondSegment->cr(0);
    ASSERT_TRUE(firstChordRest && secondChordRest);

    Harmony* first = score->addHarmony(HarmonyType::MELO, firstChordRest);
    Harmony* second = score->addHarmony(HarmonyType::MELO, secondChordRest);
    ASSERT_TRUE(first && second);
    first->setHarmony(u"!Ri:Ti5+d7,d8/d8");
    second->setHarmony(u"Fa5+A6,m7/5");
    first->setProperty(Pid::FONT_SIZE, 60.0);
    second->setProperty(Pid::FONT_SIZE, 60.0);
    score->doLayout();

    const RectF firstBox = first->ldata()->bbox().translated(first->canvasPos());
    const RectF secondBox = second->ldata()->bbox().translated(second->canvasPos());
    ASSERT_GT(firstBox.right() + std::max(firstBox.height(), secondBox.height()), secondBox.left())
        << "fixture must exercise chord names separated by less than one label height";
    EXPECT_GT(std::abs(first->canvasPos().y() - second->canvasPos().y()),
              std::max(firstBox.height(), secondBox.height()) * 0.5);
    const double firstY = first->canvasPos().y();
    const double secondY = second->canvasPos().y();
    score->doLayout();
    EXPECT_NEAR(first->canvasPos().y(), firstY, POSITION_ERROR);
    EXPECT_NEAR(second->canvasPos().y(), secondY, POSITION_ERROR);

    delete score;
}

TEST_F(Engraving_ChordSymbolTests, meloHarmonyRangeCopyPasteKeepsTypeAndCanonicalName)
{
    MasterScore* score = test_pre(u"add-link");
    ASSERT_TRUE(score);
    Measure* sourceMeasure = score->firstMeasure();
    Measure* destinationMeasure = sourceMeasure ? sourceMeasure->nextMeasure()->nextMeasure() : nullptr;
    ASSERT_TRUE(sourceMeasure && destinationMeasure);
    ChordRest* sourceChordRest = sourceMeasure->first(SegmentType::ChordRest)->cr(0);
    ASSERT_TRUE(sourceChordRest);

    Harmony* source = score->addHarmony(HarmonyType::MELO, sourceChordRest);
    ASSERT_TRUE(source);
    source->setHarmony(u"!So7/3");

    score->select(sourceMeasure);
    ASSERT_TRUE(score->selection().canCopy());
    QMimeData mimeData;
    mimeData.setData(score->selection().mimeType(), score->selection().mimeData().toQByteArray());

    EngravingItem* destination = destinationMeasure->first(SegmentType::ChordRest)->element(0);
    ASSERT_TRUE(destination);
    score->select(destination);
    score->startCmd(TranslatableString::untranslatable("Paste MeloPresto chord name"));
    QMimeDataAdapter adapter(&mimeData);
    score->cmdPaste(&adapter, 0);
    score->endCmd();

    Segment* destinationSegment = destinationMeasure->first(SegmentType::ChordRest);
    EngravingItem* pastedItem = destinationSegment->findAnnotation(ElementType::HARMONY, 0, 1);
    ASSERT_TRUE(pastedItem && pastedItem->isHarmony());
    Harmony* pasted = toHarmony(pastedItem);
    EXPECT_EQ(pasted->harmonyType(), HarmonyType::MELO);
    EXPECT_EQ(pasted->harmonyName(), u"!So7/3");
    delete score;
}

TEST_F(Engraving_ChordSymbolTests, meloHarmonyCannotBeCreatedInsideAFretDiagram)
{
    MasterScore* score = ScoreRW::readScore(CHORDSYMBOL_DATA_DIR + u"add-to-fret.mscz");
    ASSERT_TRUE(score);
    Segment* segment = score->firstMeasure()->findFirstR(SegmentType::ChordRest, Fraction(0, 1));
    ASSERT_TRUE(segment);
    FretDiagram* fretDiagram = toFretDiagram(segment->findAnnotation(ElementType::FRET_DIAGRAM, 0, 0));
    ASSERT_TRUE(fretDiagram);
    EXPECT_FALSE(score->addHarmony(HarmonyType::MELO, fretDiagram));
    EXPECT_FALSE(fretDiagram->harmony());
    delete score;
}
