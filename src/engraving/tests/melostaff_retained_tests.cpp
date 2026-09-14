// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
#include <gtest/gtest.h>
#include <memory>
#include "engraving/dom/chord.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftype.h"
#include "engraving/melo/melochangecontroller.h"
#include "engraving/melo/melobridge.h"
#include "utils/scorerw.h"
#include "utils/melocanonical.h"
using namespace mu::engraving;

TEST(MeloStaffRetained, MovableNoteTransposeKeepsStructuralIdentityAndUndo)
{
    std::unique_ptr<MasterScore> score(ScoreRW::readScore(u"jimstaff_data/collision.mscx"));
    ASSERT_TRUE(score);
    Note* note = toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->notes().front();
    ASSERT_TRUE(note->hasMeloPitch());
    const int per = note->meloNPer(), gen = note->meloNGen(), pitch = note->pitch();
    const String state = score->staff(0)->staffType(Fraction(0, 1))->meloStateJson();
    melo::SoundingPitch expected;
    ASSERT_TRUE(melo::noteSoundingPitch(state, per, gen + 1, expected));
    score->startCmd(muse::TranslatableString::untranslatable("Transpose note"));
    ASSERT_TRUE(note->transpose(Interval(4, 7), true));
    score->endCmd();
    EXPECT_EQ(note->meloNPer(), per);
    EXPECT_EQ(note->meloNGen(), gen + 1);
    EXPECT_EQ(note->pitch(), expected.midiKey);
    size_t repairs = 0;
    String error;
    ASSERT_TRUE(melo::normalizeStoredPitchesAfterLoad(score.get(), repairs, error, false)) << error.toStdString();
    EXPECT_EQ(repairs, 0u) << "A valid transpose must not be undone by load normalization";
    EXPECT_EQ(note->pitch(), expected.midiKey);
    score->undoRedo(true, nullptr);
    EXPECT_EQ(note->meloNPer(), per);
    EXPECT_EQ(note->meloNGen(), gen);
    EXPECT_EQ(note->pitch(), pitch);
}

TEST(MeloStaffRetained, MovableReferenceChangeIncludesGraceNotes)
{
    std::unique_ptr<MasterScore> score(ScoreRW::readScore(u"jimstaff_data/collision.mscx"));
    ASSERT_TRUE(score);
    Chord* chord = toChord(score->firstSegment(SegmentType::ChordRest)->element(0));
    Chord* grace = Factory::createChord(chord->segment());
    grace->setTrack(chord->track());
    grace->setNoteType(NoteType::ACCIACCATURA);
    grace->add(chord->notes().front()->clone());
    chord->add(grace);
    Note* note = grace->notes().front();
    const int per = note->meloNPer(), gen = note->meloNGen();
    String error;
    ASSERT_TRUE(melo::validateState(score.get()->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), error)) << error.toStdString();
    ASSERT_TRUE(test::initialPitch(score.get(), 0, u"C5", error)) << error.toStdString();
    melo::SoundingPitch expected;
    ASSERT_TRUE(melo::noteSoundingPitch(score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), per, gen, expected));
    EXPECT_EQ(note->pitch(), expected.midiKey);
    EXPECT_EQ(note->meloNPer(), per);
    EXPECT_EQ(note->meloNGen(), gen);
}
