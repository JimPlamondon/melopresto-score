// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
// Copyright (C) 2026 Jim Plamondon
// Coordinate authority and failure-atomicity regressions.
#include <gtest/gtest.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QTemporaryDir>
#include "engraving/dom/chord.h"
#include "engraving/dom/chordline.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/input.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftype.h"
#include "engraving/dom/tie.h"
#include "engraving/dom/timesig.h"
#include "engraving/dom/stafftypechange.h"
#include "engraving/dom/partialtie.h"
#include "engraving/dom/tiejumppointlist.h"
#include "engraving/dom/utils.h"
#include "engraving/editing/undo.h"
#include "engraving/editing/transpose.h"
#include "engraving/api/v1/cursor.h"
#include "engraving/api/v1/elements.h"
#include "engraving/editing/editdata.h"
#include "engraving/melo/melobridge.h"
#include "engraving/melo/melochange.h"
#include "engraving/melo/melochangecontroller.h"
#include "engraving/melo/melotuningcontroller.h"
#include "utils/scorerw.h"
#include "utils/melocanonical.h"
#include "utils/testutils.h"
using namespace mu::engraving;
using muse::String;

static std::vector<Note*> notes(Score* s)
{
    std::vector<Note*> out;
    for (Segment* seg = s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        auto* e = seg->element(0);
        if (e && e->isChord()) {
            for (auto* n : toChord(e)->notes()) {
                out.push_back(n);
            }
        }
    }
    return out;
}

static bool place(Note* n, int p, int g)
{
    melo::SoundingPitch v;
    if (!melo::noteSoundingPitch(n->staff()->staffTypeForElement(n)->meloStateJson(), p, g, v)) {
        return false;
    }
    n->setMeloPitch(p, g);
    int tpc = step2tpc(int(String(u"CDEFGAB").indexOf(muse::Char(v.step))), AccidentalVal(v.alter));
    n->setPitch(v.midiKey, tpc, tpc);
    n->setTuning(v.centsOffset);
    return true;
}

TEST(MeloLatticeConformanceTests, NoteValueTransferKeepsCoordinatesAcrossTunings) {
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    Chord* c=notes(s)[0]->chord();
    int mismatches=0, checked=0;
    for (double gen : { 1200.0 * 4.0 / 7.0, 686.0, 700.0, 720.0 }) {
        melo::TuningController tune(s, 0);
        ASSERT_TRUE(tune.beginPreview());
        ASSERT_TRUE(tune.commit(gen));
        int genChecked=0, genMissing=0, genChanged=0, commonMismatch=0;
        for (int g=-17; g <= 17; ++g) {
            for (int p=-10; p <= 10; ++p) {
                auto* a=Factory::createNote(c);
                a->setParent(c);
                a->setTrack(0);
                if (!place(a, p, g) || a->pitch() < 48 || a->pitch() > 84) {
                    delete a;
                    continue;
                }
                auto* b=Factory::createNote(c);
                b->setParent(c);
                b->setTrack(0);
                ASSERT_TRUE(b->setNval(a->noteVal()));
                ++checked;
                ++genChecked;
                if (!b->hasMeloPitch() || b->meloNPer() != p || b->meloNGen() != g) {
                    if (mismatches < 8) {
                        std::cout << "TRANSFER_MISMATCH gen=" << gen << " source=(" << p << "," << g << ") target=(" << b->meloNPer() <<
                            "," << b->meloNGen() << ")\n";
                    }
                    ++mismatches;
                    if (!b->hasMeloPitch()) {
                        ++genMissing;
                    } else {
                        ++genChanged;
                    }
                    if (g >= -3 && g <= 3) {
                        ++commonMismatch;
                    }
                }
                delete b;
                delete a;
            }
        }
        std::cout << "TRANSFER_SUMMARY gen=" << gen << " checked=" << genChecked << " missing=" << genMissing << " changed=" <<
            genChanged << " common_mismatches=" << commonMismatch << "\n";
        ::testing::Test::RecordProperty("transfer_" + std::to_string(int(gen)),
                                        std::to_string(genChecked) + " checked; " + std::to_string(genMissing) + " missing; "
                                        + std::to_string(genChanged) + " changed; " + std::to_string(commonMismatch)
                                        + " common mismatches");
    }
    EXPECT_EQ(mismatches, 0) << "checked " << checked << " source-coordinate transfers";
    delete s;
}

static void addKnownRe0(double generator)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    melo::TuningController tune(s, 0);
    ASSERT_TRUE(tune.beginPreview());
    ASSERT_TRUE(tune.commit(generator));
    Note* source=notes(s)[0];
    ASSERT_TRUE(place(source, 0, 0));
    auto nv=source->noteVal();
    melo::SoundingPitch inverse;
    String error;
    bool recovered=melo::entryFromStandardPitch(source->staff()->staffTypeForElement(source)->meloStateJson(), 'D', 0, 4, inverse, &error);
    std::cout << "RE0_REVERSE generator=" << generator << " success=" << recovered << " error=" << error.toStdString() << "\n";
    ::testing::Test::RecordProperty("reverse_error", error.toStdString());
    s->startCmd(muse::TranslatableString::untranslatable("audit public add note"));
    Note* inserted=s->addNote(source->chord(), nv);
    s->endCmd();
    ASSERT_TRUE(inserted);
    EXPECT_TRUE(inserted->hasMeloPitch()) << "Public insertion accepted a note with no lattice coordinates";
    EXPECT_EQ(inserted->meloNPer(), 0);
    EXPECT_EQ(inserted->meloNGen(), 0);
    delete s;
}

TEST(MeloLatticeConformanceTests, PublicInsertionPreservesKnownRe0At720) {
    addKnownRe0(720);
}
TEST(MeloLatticeConformanceTests, PublicInsertionAt700Control) {
    addKnownRe0(700);
}

TEST(MeloLatticeConformanceTests, UnspelledCoordinateLessInsertionIsNonMutating)
{
    auto* s = ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    melo::TuningController tune(s, 0);
    ASSERT_TRUE(tune.beginPreview());
    ASSERT_TRUE(tune.commit(720));
    Note* source = notes(s).front();
    const String state = source->staff()->staffTypeForElement(source)->meloStateJson();
    const size_t count = source->chord()->notes().size();
    const size_t undo = s->undoStack()->currentIndex();
    s->select(source, SelectType::SINGLE, 0);
    const auto segment = s->inputState().segment();
    const auto selected = s->selection().element();
    s->setPlayNote(false);
    s->setPlayChord(false);
    NoteVal value(62);
    value.tpc1 = value.tpc2 = Tpc::TPC_INVALID;
    s->startCmd(muse::TranslatableString::untranslatable("ambiguous input"));
    Note* inserted = s->addNote(source->chord(), value);
    EXPECT_EQ(inserted, nullptr);
    EXPECT_FALSE(s->playNote());
    EXPECT_FALSE(s->playChord());
    s->endCmd();
    EXPECT_EQ(source->chord()->notes().size(), count);
    EXPECT_EQ(source->staff()->staffTypeForElement(source)->meloStateJson(), state);
    EXPECT_EQ(s->undoStack()->currentIndex(), undo);
    EXPECT_EQ(s->inputState().segment(), segment);
    EXPECT_EQ(s->selection().element(), selected);
    delete s;
}

TEST(MeloLatticeConformanceTests, TransferEqualityDistinguishesCoincidentCoordinates)
{
    NoteVal first(62);
    NoteVal second = first;
    EXPECT_TRUE(first == second);
    first.hasMeloPitch = true;
    EXPECT_FALSE(first == second);
    second.hasMeloPitch = true;
    EXPECT_TRUE(first == second);
    second.meloNPer = -7;
    second.meloNGen = 12;
    EXPECT_FALSE(first == second);
}

TEST(MeloLatticeConformanceTests, UnspelledPublicEntryPreservesInputAndScore)
{
    for (bool addToChord : { false, true }) {
        auto* s = ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
        ASSERT_TRUE(s);
        melo::TuningController tune(s, 0);
        ASSERT_TRUE(tune.beginPreview());
        ASSERT_TRUE(tune.commit(720));
        Note* source = notes(s).front();
        InputState input;
        input.setTrack(0);
        input.setSegment(source->chord()->segment());
        input.setLastSegment(source->chord()->segment());
        input.setDuration(TDuration(DurationType::V_QUARTER));
        const auto position = input.segment();
        const auto count = notes(s).size();
        const auto undo = s->undoStack()->currentIndex();
        NoteVal value(62);
        value.tpc1 = value.tpc2 = Tpc::TPC_INVALID;
        s->startCmd(muse::TranslatableString::untranslatable("ambiguous public entry"));
        EXPECT_EQ(s->addPitch(value, addToChord, &input), nullptr);
        EXPECT_EQ(input.segment(), position);
        EXPECT_EQ(input.lastSegment(), position);
        s->endCmd();
        EXPECT_EQ(notes(s).size(), count);
        EXPECT_EQ(s->undoStack()->currentIndex(), undo);
        delete s;
    }
}

static double hz(Note* n)
{
    melo::SoundingPitch v;
    if (!melo::noteSoundingPitch(n->staff()->staffTypeForElement(n)->meloStateJson(), n->meloNPer(), n->meloNGen(), v)) {
        return -1;
    }
    return v.frequencyHz;
}

static void tie(Score* s, Note* a, Note* b)
{
    Tie* t = Factory::createTie(s->dummy());
    t->setStartNote(a);
    t->setEndNote(b);
    t->setTrack(a->track());
    t->setTick(a->tick());
    t->setTick2(b->tick());
    s->startCmd(muse::TranslatableString::untranslatable("audit fixture"));
    s->undoAddElement(t);
    s->endCmd();
}

TEST(MeloLatticeConformanceTests, TiedChordAcceptsDistinctLatticePositionAtCoincidentPitch) {
    auto* s = ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    auto ns=notes(s);
    ASSERT_GE(ns.size(), 2u);
    ASSERT_TRUE(place(ns[0], 0, 0));
    ASSERT_TRUE(place(ns[1], 0, 0));
    tie(s, ns[0], ns[1]);
    auto* candidate=Factory::createNote(ns[0]->chord());
    candidate->setTrack(0);
    ASSERT_TRUE(place(candidate, -7, 12));
    ASSERT_EQ(candidate->pitch(), ns[0]->pitch());
    auto nv=candidate->noteVal();
    delete candidate;
    s->startCmd(muse::TranslatableString::untranslatable("audit add distinct coincident note"));
    InputState input;
    input.setTrack(0);
    input.setSegment(ns[0]->chord()->segment());
    input.setLastSegment(ns[0]->chord()->segment());
    auto* added=s->addPitch(nv, true, &input);
    s->endCmd();
    ASSERT_NE(added, nullptr);
    EXPECT_TRUE(added->tieFor() || added->tieBack()) << "Distinct coincident lattice position must propagate through tied chord";
    EXPECT_EQ(ns[1]->chord()->notes().size(), 2u);
    delete s;
}

TEST(MeloLatticeConformanceTests, TiedChordNoncoincidentControl) {
    auto* s = ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    auto ns=notes(s);
    ASSERT_GE(ns.size(), 2u);
    ASSERT_TRUE(place(ns[0], 0, 0));
    ASSERT_TRUE(place(ns[1], 0, 0));
    tie(s, ns[0], ns[1]);
    auto* candidate=Factory::createNote(ns[0]->chord());
    candidate->setTrack(0);
    ASSERT_TRUE(place(candidate, 0, 1));
    auto nv=candidate->noteVal();
    delete candidate;
    s->startCmd(muse::TranslatableString::untranslatable("audit add control"));
    InputState input;
    input.setTrack(0);
    input.setSegment(ns[0]->chord()->segment());
    input.setLastSegment(ns[0]->chord()->segment());
    auto* added=s->addPitch(nv, true, &input);
    s->endCmd();
    EXPECT_NE(added, nullptr);
    ASSERT_NE(added, nullptr);
    EXPECT_TRUE(added->tieFor() || added->tieBack());
    EXPECT_EQ(ns[1]->chord()->notes().size(), 2u);
    delete s;
}

TEST(MeloLatticeConformanceTests, GraceUsesSelectedOccurrenceNotFirstMatchingMidiPitch)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    for (double generator:{ 700.0, 701.0, 1200.0 * 4.0 / 7.0, 720.0 }) {
        for (bool chooseOther:{ false, true }) {
            auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
            ASSERT_TRUE(s);
            melo::TuningController tuning(s, 0);
            ASSERT_TRUE(tuning.beginPreview());
            ASSERT_TRUE(tuning.commit(generator));
            Note* a=notes(s).front();
            Chord* chord=a->chord();
            ASSERT_TRUE(place(a, 0, 0));
            Note* other=Factory::createNote(chord);
            other->setTrack(a->track());
            ASSERT_TRUE(place(other, generator < 690.0 ? -4 : generator == 720.0 ? -3 : -7,
                              generator < 690.0 ? 7 : generator == 720.0 ? 5 : 12));
            chord->add(other);
            if (generator != 701.0) {
                EXPECT_NEAR(hz(a), hz(other), 1e-8);
            } else {
                EXPECT_GT(std::abs(hz(a) - hz(other)), 0.1);
            }
            Note* selected=chooseOther ? other : a;
            const NoteVal expected=selected->noteVal();
            s->select(selected, SelectType::SINGLE);
            const auto index=s->undoStack()->currentIndex();
            s->startCmd(muse::TranslatableString::untranslatable("Grace chosen occurrence"));
            s->cmdAddGrace(NoteType::ACCIACCATURA, Constants::DIVISION / 2);
            s->endCmd();
            ASSERT_EQ(chord->graceNotes().size(), 1u);
            EXPECT_TRUE(chord->graceNotes()[0]->notes()[0]->noteVal() == expected);
            s->undoRedo(true, nullptr);
            EXPECT_TRUE(chord->graceNotes().empty());
            EXPECT_EQ(s->undoStack()->currentIndex(), index);
            s->undoRedo(false, nullptr);
            ASSERT_EQ(chord->graceNotes().size(), 1u);
            EXPECT_TRUE(chord->graceNotes()[0]->notes()[0]->noteVal() == expected);
            const String path=String::fromQString(directory.filePath("selected-grace.mscx"));
            ASSERT_TRUE(ScoreRW::saveScore(s, path));
            if (const char* output = std::getenv("MELO_LATTICE_SENSORY_OUT")) {
                ASSERT_TRUE(ScoreRW::saveScore(s, String::fromUtf8(output) + u"/selected-grace-" + String::number(int(generator))
                                               + (chooseOther ? u"-other.mscx" : u"-first.mscx")));
            }
            auto* again=ScoreRW::readScore(path, true);
            ASSERT_TRUE(again);
            Chord* copied=notes(again).front()->chord();
            ASSERT_EQ(copied->graceNotes().size(), 1u);
            EXPECT_TRUE(copied->graceNotes()[0]->notes()[0]->noteVal() == expected);
            delete again;
            delete s;
        }
    }
}

TEST(MeloLatticeConformanceTests, L10ChordLineFollowsOriginalOccurrence)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    Note* a=notes(s).front();
    ASSERT_TRUE(place(a, 0, 0));
    Chord* c=a->chord();
    Note* other=Factory::createNote(c);
    other->setTrack(a->track());
    ASSERT_TRUE(place(other, -7, 12));
    c->add(other);
    Note* selected=c->findNote(a->pitch()) == a ? other : a;
    ChordLine* line=Factory::createChordLine(c);
    c->add(line);
    line->setNote(selected);
    Chord* copy=Factory::copyChord(*c);
    ASSERT_TRUE(copy->chordLine());
    ASSERT_TRUE(copy->chordLine()->note());
    EXPECT_EQ(copy->chordLine()->note()->meloNPer(), selected->meloNPer());
    EXPECT_EQ(copy->chordLine()->note()->meloNGen(), selected->meloNGen());
    delete copy;
    delete s;
}

static void stepTie(bool different)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
    ASSERT_TRUE(s);
    if (!different) {
        String err;
        ASSERT_TRUE(melo::removeChange(s, 0, s->firstMeasure()->nextMeasure(), err));
    }
    auto ns=notes(s);
    ASSERT_GE(ns.size(), 5u);
    Note* a=ns[3];
    Note* b=ns[4];
    ASSERT_TRUE(place(a, 0, 0));
    melo::SoundingPitch v;
    ASSERT_TRUE(melo::noteContinuation(b->staff()->staffTypeForElement(b)->meloStateJson(), hz(a), v));
    ASSERT_TRUE(place(b, v.nPer, v.nGen));
    tie(s, a, b);
    EXPECT_NEAR(hz(a), hz(b), 1e-8);
    s->select(a, SelectType::SINGLE, 0);
    const double initialFrequency=hz(a);
    s->startCmd(muse::TranslatableString::untranslatable("audit step tie"));
    s->upDown(true, UpDownMode::OCTAVE);
    s->endCmd();
    EXPECT_NEAR(hz(a), 2 * initialFrequency, 1e-8);
    EXPECT_NEAR(hz(a), hz(b), 1e-8) << "One tied sound must remain coherent after ordinary keyboard step";
    for (Note* n : { a, b }) {
        melo::SoundingPitch actual;
        ASSERT_TRUE(melo::noteSoundingPitch(n->staff()->staffTypeForElement(n)->meloStateJson(), n->meloNPer(), n->meloNGen(), actual));
        EXPECT_EQ(n->pitch(), actual.midiKey);
    }
    s->undoRedo(true, nullptr);
    EXPECT_NEAR(hz(a), hz(b), 1e-8);
    delete s;
}

TEST(MeloLatticeConformanceTests, KeyboardStepAcrossReferenceChangePreservesTie) {
    stepTie(true);
}
TEST(MeloLatticeConformanceTests, KeyboardStepSameReferenceControl) {
    stepTie(false);
}

TEST(MeloLatticeConformanceTests, L06CrossReferenceDragPreservesSoundAndUndo)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
    ASSERT_TRUE(s);
    auto ns=notes(s);
    ASSERT_GE(ns.size(), 5u);
    Note* a=ns[3];
    Note* b=ns[4];
    ASSERT_TRUE(place(a, 0, 0));
    melo::SoundingPitch v;
    ASSERT_TRUE(melo::noteContinuation(b->staff()->staffTypeForElement(b)->meloStateJson(), hz(a), v));
    ASSERT_TRUE(place(b, v.nPer, v.nGen));
    tie(s, a, b);
    auto av=a->noteVal(), bv=b->noteVal();
    double bt=b->tuning();
    s->doLayout();
    s->select(a, SelectType::SINGLE, 0);
    EditData ed(nullptr);
    EngravingItem* item=a;
    s->startCmd(muse::TranslatableString::untranslatable("cross-reference drag"));
    item->startDrag(ed);
    ed.evtDelta=PointF(0, -s->style().spatium());
    ed.moveDelta=ed.evtDelta;
    item->drag(ed);
    EXPECT_NEAR(hz(a), hz(b), 1e-8);
    auto moved=a->noteVal();
    for (int i=0; i < 5; ++i) {
        ed.evtDelta=PointF();
        item->drag(ed);
        EXPECT_TRUE(a->noteVal() == moved);
        EXPECT_NEAR(hz(a), hz(b), 1e-8);
    }
    item->endDrag(ed);
    s->endCmd();
    s->undoRedo(true, nullptr);
    EXPECT_TRUE(a->noteVal() == av);
    EXPECT_TRUE(b->noteVal() == bv);
    EXPECT_DOUBLE_EQ(b->tuning(), bt);
    delete s;
}

TEST(MeloLatticeConformanceTests, L07ContradictoryNativeTieIsRefusedWithoutCoordinateRewrite)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
    ASSERT_TRUE(s);
    auto ns=notes(s);
    ASSERT_GE(ns.size(), 5u);
    Note* a=ns[3];
    Note* b=ns[4];
    ASSERT_TRUE(place(a, 0, 0));
    ASSERT_TRUE(place(b, 0, 0));
    ASSERT_GT(std::abs(hz(a) - hz(b)), 1.0);
    tie(s, a, b);
    auto before=b->noteVal();
    size_t repairs=0;
    String error;
    EXPECT_FALSE(melo::normalizeStoredPitchesAfterLoad(s, repairs, error));
    EXPECT_TRUE(b->noteVal() == before);
    delete s;
}

TEST(MeloLatticeConformanceTests, L09TieTargetsTheCorrespondingCoincidentOccurrence)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    auto ns=notes(s);
    ASSERT_GE(ns.size(), 2u);
    Note* a=ns[0];
    Note* b=ns[1];
    ASSERT_TRUE(place(a, -7, 12));
    ASSERT_TRUE(place(b, 0, 0));
    Note* wanted=Factory::createNote(b->chord());
    wanted->setTrack(b->track());
    ASSERT_TRUE(place(wanted, -7, 12));
    b->chord()->add(wanted);
    ASSERT_EQ(a->pitch(), b->pitch());
    s->select(a, SelectType::SINGLE, 0);
    s->startCmd(muse::TranslatableString::untranslatable("tie selected occurrence"));
    Tie* added=s->cmdToggleTie();
    s->endCmd();
    ASSERT_TRUE(added);
    EXPECT_EQ(added->endNote(), wanted);
    delete s;
}

TEST(MeloLatticeConformanceTests, L11ImplodeKeepsDistinctCoincidentPositions)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    auto* seg=s->firstSegment(SegmentType::ChordRest);
    auto* ca=toChord(seg->element(0));
    auto* cb=toChord(seg->element(1));
    ASSERT_TRUE(ca);
    ASSERT_TRUE(cb);
    ASSERT_TRUE(place(ca->notes().front(), 0, 0));
    ASSERT_TRUE(place(cb->notes().front(), -7, 12));
    cb->setDurationType(ca->durationType());
    cb->setTicks(ca->ticks());
    s->cmdSelectAll();
    s->startCmd(muse::TranslatableString::untranslatable("implode voices"));
    ASSERT_TRUE(s->cmdImplode());
    s->endCmd();
    EXPECT_EQ(ca->notes().size(), 2u);
    delete s;
}

TEST(MeloLatticeConformanceTests, InitialPitchEditingAnySingerKeepsOneReference) {
    for (staff_idx_t selected=0; selected < 6; ++selected) {
        auto* s = ScoreRW::readScore(ScoreRW::rootPath()
                                     + u"/../../../share/templates/02-Choral/12-SATB_(MeloPresto_Staff)/12-SATB_(MeloPresto_Staff).mscx",
                                     true);
        ASSERT_TRUE(s);
        Score* part = selected == 5 ? TestUtils::createPart(s) : nullptr;
        if (selected == 5) {
            ASSERT_TRUE(part);
        }
        std::vector<String> before;
        for (Staff* staff:s->staves()) {
            before.push_back(staff->staffType(Fraction(0, 1))->meloStateJson());
        }
        const auto index=s->undoStack()->currentIndex();
        String err;
        ASSERT_TRUE(test::initialPitch(part ? part : s, selected < 4 ? selected : 0, u"D4", err)) << err.toStdString();
        if (part) {
            bool same = false;
            ASSERT_TRUE(melo::sameReference(s->staff(0)->staffType(Fraction(0, 1))->meloStateJson(),
                                            part->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), same));
            EXPECT_TRUE(same);
        }
        for (size_t i=0; i < before.size(); ++i) {
            auto oldState=QJsonDocument::fromJson(before[i].toQString().toUtf8()).object();
            auto newState=QJsonDocument::fromJson(s->staff(i)->staffType(Fraction(0, 1))->meloStateJson().toQString().toUtf8()).object();
            EXPECT_EQ(oldState["configuration"].toObject()["extent"], newState["configuration"].toObject()["extent"]);
            melo::SoundingPitch bound;
            ASSERT_TRUE(melo::noteSoundingPitch(s->staff(i)->staffType(Fraction(0, 1))->meloStateJson(), 0, 0, bound));
            EXPECT_DOUBLE_EQ(bound.referenceKeyNumber, test::referenceNumber(s->staff(0)->staffType(Fraction(0, 1))->meloStateJson()));
        }
        s->undoRedo(true, nullptr);
        EXPECT_EQ(s->undoStack()->currentIndex(), index);
        for (size_t i=0; i < before.size(); ++i) {
            EXPECT_EQ(s->staff(i)->staffType(Fraction(0, 1))->meloStateJson(), before[i]);
        }
        s->undoRedo(false, nullptr);
        std::vector<double> refs;
        for (staff_idx_t i=0; i < s->nstaves(); ++i) {
            melo::SoundingPitch v;
            ASSERT_TRUE(melo::noteSoundingPitch(s->staff(i)->staffType(Fraction(0, 1))->meloStateJson(), 0, 0, v));
            refs.push_back(v.frequencyHz);
        }
        for (size_t i=1; i < refs.size(); ++i) {
            EXPECT_DOUBLE_EQ(refs[0], refs[i]);
        }
        ASSERT_TRUE(ScoreRW::saveScore(s, u"divergent-reference.mscx"));
        auto* reopened=ScoreRW::readScore(String::fromUtf8((std::string(::testing::UnitTest::GetInstance()->original_working_dir())
                                                            + "/divergent-reference.mscx").c_str()), true);
        ASSERT_TRUE(reopened);
        for (staff_idx_t i=1; i < reopened->nstaves(); ++i) {
            melo::SoundingPitch first, other;
            ASSERT_TRUE(melo::noteSoundingPitch(reopened->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), 0, 0, first));
            ASSERT_TRUE(melo::noteSoundingPitch(reopened->staff(i)->staffType(Fraction(0, 1))->meloStateJson(), 0, 0, other));
            EXPECT_DOUBLE_EQ(first.frequencyHz, other.frequencyHz) << "Native reopening admitted inconsistent references";
        }
        delete reopened;
        delete s;
    }
}

TEST(MeloLatticeConformanceTests, BindingConflictRefusesWithoutChangingAnySinger)
{
    auto* s = ScoreRW::readScore(ScoreRW::rootPath()
                                 + u"/../../../share/templates/02-Choral/12-SATB_(MeloPresto_Staff)/12-SATB_(MeloPresto_Staff).mscx", true);
    ASSERT_TRUE(s);
    auto* type = s->staff(0)->staffType(Fraction(0, 1));
    auto obj = QJsonDocument::fromJson(type->meloStateJson().toQString().toUtf8()).object();
    obj["reference"] = "none";
    type->setMeloStateJson(String::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact).constData()));
    std::vector<String> states;
    for (Staff* staff : s->staves()) {
        states.push_back(staff->staffType(Fraction(0, 1))->meloStateJson());
    }
    const size_t undo = s->undoStack()->currentIndex();
    String error;
    EXPECT_FALSE(melo::applyChange(s, 0, s->firstMeasure(), u"bind:reference-pitch:64", error));
    EXPECT_FALSE(error.isEmpty());
    for (size_t i=0; i < states.size(); ++i) {
        EXPECT_EQ(s->staff(i)->staffType(Fraction(0, 1))->meloStateJson(), states[i]);
    }
    EXPECT_EQ(s->undoStack()->currentIndex(), undo);
    delete s;
}

TEST(MeloLatticeConformanceTests, LinkedPartInitialPitchPreservesWrittenNotesAndUndoes)
{
    auto* s = ScoreRW::readScore(u"jimstaff_data/m9-satb-hymn.mscx");
    ASSERT_TRUE(s);
    std::vector<Note*> written;
    std::vector<NoteVal> before;
    for (Segment* segment = s->firstSegment(SegmentType::ChordRest); segment; segment = segment->next1(SegmentType::ChordRest)) {
        for (track_idx_t track = 0; track < s->ntracks(); ++track) {
            auto* item = segment->element(track);
            if (item && item->isChord()) {
                for (Note* note : toChord(item)->notes()) {
                    ASSERT_TRUE(note->setNval(note->noteVal()));
                    written.push_back(note);
                    before.push_back(note->noteVal());
                }
            }
        }
    }
    ASSERT_FALSE(written.empty());
    Score* part = TestUtils::createPart(s);
    ASSERT_TRUE(part);
    String error;
    ASSERT_TRUE(test::initialPitch(part, 0, u"D4", error)) << error.toStdString();
    for (size_t i = 0; i < written.size(); ++i) {
        EXPECT_EQ(written[i]->meloNPer(), before[i].meloNPer);
        EXPECT_EQ(written[i]->meloNGen(), before[i].meloNGen);
        melo::SoundingPitch expected;
        ASSERT_TRUE(melo::noteSoundingPitch(written[i]->staff()->staffTypeForElement(written[i])->meloStateJson(),
                                            before[i].meloNPer, before[i].meloNGen, expected));
        EXPECT_EQ(written[i]->pitch(), expected.midiKey);
        EXPECT_NEAR(written[i]->tuning(), expected.centsOffset, 1e-9);
    }
    s->undoRedo(true, nullptr);
    for (size_t i = 0; i < written.size(); ++i) {
        EXPECT_TRUE(written[i]->noteVal() == before[i]);
    }
    delete s;
}

TEST(MeloLatticeConformanceTests, DurationExpansionPreservesEveryFragmentCoordinate)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    Note* first=notes(s).front();
    ASSERT_TRUE(place(first, -7, 12));
    s->startCmd(muse::TranslatableString::untranslatable("expand duration"));
    s->changeCRlen(first->chord(), Fraction(5, 8));
    s->endCmd();
    ASSERT_GE(first->tiedNotes().size(), 2u);
    for (Note* n:first->tiedNotes()) {
        EXPECT_TRUE(n->hasMeloPitch());
        EXPECT_EQ(n->meloNPer(), -7);
        EXPECT_EQ(n->meloNGen(), 12);
        EXPECT_NEAR(hz(n), hz(first), 1e-8);
    }
    delete s;
}

TEST(MeloLatticeConformanceTests, PointerCreatesAndRemovesTheChosenCoincidentPosition)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    Note* first=notes(s).front();
    ASSERT_TRUE(place(first, 1, 0));
    Chord* chord=first->chord();
    s->doLayout();
    InputState& input=s->inputState();
    input.setTrack(0);
    input.setSegment(chord->segment());
    input.setLastSegment(chord->segment());
    input.setDuration(chord->durationType());
    input.setNoteEntryMode(true);
    input.setAccidentalType(AccidentalType::SHARP2);
    const auto point=first->canvasPos();
    s->startCmd(muse::TranslatableString::untranslatable("create coincident pointer note"));
    EXPECT_TRUE(s->putNote(point, false, false));
    s->endCmd();
    ASSERT_EQ(chord->notes().size(), 2u);
    Note* selected=chord->findNote(first->pitch()) == first ? chord->notes().back() : first;
    if (selected == chord->findNote(first->pitch())) {
        selected=chord->notes().front();
    }
    const auto chosen=std::make_pair(selected->meloNPer(), selected->meloNGen());
    input.setSegment(chord->segment());
    input.setDuration(chord->durationType());
    input.setAccidentalType(selected->meloNGen() == 0 ? AccidentalType::NATURAL : AccidentalType::SHARP2);
    s->doLayout();
    s->startCmd(muse::TranslatableString::untranslatable("remove coincident pointer note"));
    EXPECT_TRUE(s->putNote(selected->canvasPos(), false, false));
    s->endCmd();
    ASSERT_EQ(chord->notes().size(), 1u);
    EXPECT_NE(std::make_pair(chord->notes().front()->meloNPer(), chord->notes().front()->meloNGen()), chosen);
    s->undoRedo(true, nullptr);
    EXPECT_EQ(chord->notes().size(), 2u);
    delete s;
}

TEST(MeloLatticeConformanceTests, TransposeSelectedTieChangesEveryOccurrence)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    auto ns=notes(s);
    ASSERT_TRUE(place(ns[0], 0, 0));
    ASSERT_TRUE(place(ns[1], 0, 0));
    tie(s, ns[0], ns[1]);
    const double before=hz(ns[0]);
    s->select(ns[0], SelectType::SINGLE, 0);
    s->startCmd(muse::TranslatableString::untranslatable("transpose one tied occurrence"));
    EXPECT_TRUE(Transpose::transpose(s, TransposeMode::BY_INTERVAL, TransposeDirection::UP, Key::C, 4, true, true, true));
    s->endCmd();
    EXPECT_NE(hz(ns[0]), before);
    EXPECT_NEAR(hz(ns[0]), hz(ns[1]), 1e-8);
    delete s;
}

TEST(MeloLatticeConformanceTests, TransposeInvalidLaterTieIsNonMutating)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    auto ns=notes(s);
    ASSERT_TRUE(place(ns[0], 0, 0));
    ASSERT_TRUE(place(ns[1], 0, 0));
    tie(s, ns[0], ns[1]);
    ns[1]->setMeloPitch(INT_MAX, INT_MAX);
    const auto before=ns[0]->noteVal();
    const auto bad=ns[1]->noteVal();
    const auto undo=s->undoStack()->currentIndex();
    s->select(ns[0], SelectType::SINGLE, 0);
    s->startCmd(muse::TranslatableString::untranslatable("refuse invalid tied transpose"));
    EXPECT_FALSE(Transpose::transpose(s, TransposeMode::BY_INTERVAL, TransposeDirection::UP, Key::C, 4, true, true, true));
    s->endCmd();
    EXPECT_TRUE(ns[0]->noteVal() == before);
    EXPECT_TRUE(ns[1]->noteVal() == bad);
    EXPECT_EQ(s->undoStack()->currentIndex(), undo);
    delete s;
}

TEST(MeloLatticeConformanceTests, PluginRawUnspelledInsertionRemainsDetached)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    melo::TuningController tune(s, 0);
    ASSERT_TRUE(tune.beginPreview());
    ASSERT_TRUE(tune.commit(720));
    apiv1::Cursor cursor(s);
    cursor.rewind(apiv1::Cursor::SCORE_START);
    Note* raw=Factory::createNote(s->dummy()->chord());
    {
        apiv1::Note wrapped(raw, apiv1::Ownership::PLUGIN);
        wrapped.setProperty("pitch", 62);
        wrapped.setProperty("tpc1", Tpc::TPC_INVALID);
        wrapped.setProperty("tpc2", Tpc::TPC_INVALID);
        const auto count=notes(s).size();
        const auto undo=s->undoStack()->currentIndex();
        s->startCmd(muse::TranslatableString::untranslatable("plugin ambiguous raw input"));
        cursor.add(&wrapped);
        s->endCmd();
        EXPECT_EQ(wrapped.ownership(), apiv1::Ownership::PLUGIN);
        EXPECT_EQ(notes(s).size(), count);
        EXPECT_EQ(s->undoStack()->currentIndex(), undo);
    }
    delete s;
}

TEST(MeloLatticeConformanceTests, PluginDerivedSettersCannotChangeLiveLatticeNotes)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    Note* note=notes(s).front();
    const auto before=note->noteVal();
    const double tuning=note->tuning();
    const auto undo=s->undoStack()->currentIndex();
    apiv1::Note wrapped(note, apiv1::Ownership::SCORE);
    s->startCmd(muse::TranslatableString::untranslatable("plugin derived field setters"));
    wrapped.setProperty("pitch", note->pitch() + 1);
    wrapped.setProperty("tpc1", step2tpc(0, AccidentalVal::SHARP));
    wrapped.setProperty("tpc2", step2tpc(0, AccidentalVal::SHARP));
    wrapped.setProperty("tuning", tuning + 50.0);
    s->endCmd();
    EXPECT_TRUE(note->noteVal() == before);
    EXPECT_DOUBLE_EQ(note->tuning(), tuning);
    EXPECT_EQ(s->undoStack()->currentIndex(), undo);
    delete s;
}

TEST(MeloLatticeConformanceTests, NativeWriterRefusesMissingCoordinates)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    notes(s).front()->setMeloPitch(INT_MIN, INT_MIN);
    EXPECT_FALSE(ScoreRW::saveScore(s, u"invalid-missing-lattice-note.mscx"));
    delete s;
}

TEST(MeloLatticeConformanceTests, TieSelectionDoesNotDependOnCoincidentChordOrder)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    auto ns=notes(s);
    Note* a=ns[0];
    Note* b=ns[1];
    ASSERT_TRUE(place(a, 0, 0));
    ASSERT_TRUE(place(b, -7, 12));
    Note* otherA=Factory::createNote(a->chord());
    otherA->setTrack(0);
    ASSERT_TRUE(place(otherA, -7, 12));
    a->chord()->add(otherA);
    Note* otherB=Factory::createNote(b->chord());
    otherB->setTrack(0);
    ASSERT_TRUE(place(otherB, 0, 0));
    b->chord()->add(otherB);
    s->select(a, SelectType::SINGLE, 0);
    Tie* added=s->cmdToggleTie();
    ASSERT_TRUE(added);
    EXPECT_EQ(added->endNote(), otherB);
    EXPECT_NE(added->endNote(), b);
    delete s;
}

TEST(MeloLatticeConformanceTests, PublicAddTieAcrossReferenceChangeSustainsTheSource)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
    ASSERT_TRUE(s);
    auto ns=notes(s);
    Note* a=ns[3];
    ASSERT_TRUE(place(a, 0, 0));
    s->select(a, SelectType::SINGLE, 0);
    s->inputState().setDuration(a->chord()->durationType());
    s->cmdAddTie();
    ASSERT_TRUE(a->tieFor());
    ASSERT_TRUE(a->tieFor()->endNote());
    EXPECT_NEAR(hz(a), hz(a->tieFor()->endNote()), 1e-8);
    EXPECT_FALSE(s->undoStack()->hasActiveCommand());
    delete s;
}

TEST(MeloLatticeConformanceTests, LinkedPartEditsEveryEndpointAndUndoRestoresEachOccurrence)
{
    for (int selectedIndex : { 0, 1, 2 }) {
        for (bool fromPart : { false, true }) {
            auto* s=ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
            ASSERT_TRUE(s);
            auto ns=notes(s);
            std::vector<Note*> chain { ns[3], ns[4], ns[6] };
            ASSERT_TRUE(place(chain[0], 0, 0));
            for (size_t i=1; i < chain.size(); ++i) {
                melo::SoundingPitch value;
                ASSERT_TRUE(melo::noteContinuation(chain[i]->staff()->staffTypeForElement(chain[i])->meloStateJson(), hz(chain[0]), value));
                ASSERT_TRUE(place(chain[i], value.nPer, value.nGen));
                tie(s, chain[i - 1], chain[i]);
            }
            Score* part=TestUtils::createPart(s);
            ASSERT_TRUE(part);
            std::vector<Note*> linked;
            for (Note* note:chain) {
                Note* match=nullptr;
                for (EngravingObject* object:note->linkList()) {
                    if (object->score() == part) {
                        match=toNote(object);
                    }
                }
                ASSERT_TRUE(match);
                linked.push_back(match);
            }
            std::vector<Note*> all=chain;
            all.insert(all.end(), linked.begin(), linked.end());
            std::vector<NoteVal> before;
            std::vector<double> tuning;
            for (Note* note:all) {
                before.push_back(note->noteVal());
                tuning.push_back(note->tuning());
            }
            Note* selected=fromPart ? linked[selectedIndex] : chain[selectedIndex];
            Score* edited=selected->score();
            edited->select(selected, SelectType::SINGLE, 0);
            edited->startCmd(muse::TranslatableString::untranslatable("linked endpoint step"));
            const double initialFrequency=hz(selected);
            edited->upDown(true, UpDownMode::OCTAVE);
            edited->endCmd();
            EXPECT_NEAR(hz(selected), 2 * initialFrequency, 1e-8);
            for (Note* note:all) {
                EXPECT_NEAR(hz(note), hz(chain[0]), 1e-8);
            }
            s->undoRedo(true, nullptr);
            for (size_t i=0; i < all.size(); ++i) {
                EXPECT_TRUE(all[i]->noteVal() == before[i]);
                EXPECT_DOUBLE_EQ(all[i]->tuning(), tuning[i]);
            }
            s->undoRedo(false, nullptr);
            for (Note* note:all) {
                EXPECT_NEAR(hz(note), hz(chain[0]), 1e-8);
            }
            delete s;
        }
    }
}

TEST(MeloLatticeConformanceTests, NativeReaderRefusesStructuralContradictionsWithoutRewritingTheFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    auto ns=notes(s);
    ASSERT_TRUE(place(ns[0], 0, 0));
    ASSERT_TRUE(place(ns[1], 0, 0));
    tie(s, ns[0], ns[1]);
    const QString path=directory.filePath("contradictory-tie.mscx");
    ASSERT_TRUE(ScoreRW::saveScore(s, String::fromQString(path)));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QByteArray content=file.readAll();
    file.close();
    const QByteArray tag="<jimsNPer>0</jimsNPer>";
    int index=content.indexOf(tag);
    ASSERT_GE(index, 0);
    content.replace(index, tag.size(), "<jimsNPer>1</jimsNPer>");
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(content), content.size());
    file.close();
    auto* invalid=ScoreRW::readScore(String::fromQString(path), true);
    EXPECT_EQ(invalid, nullptr);
    delete invalid;
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), content);
    EXPECT_NEAR(hz(ns[0]), hz(ns[1]), 1e-8);
    delete s;
}

TEST(MeloLatticeConformanceTests, TypedShapeUsesKernelClassAndKeepsCrossReferenceTie)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
    ASSERT_TRUE(s);
    auto ns=notes(s);
    Note* a=ns[3];
    Note* b=ns[4];
    ASSERT_TRUE(place(a, 0, 0));
    melo::SoundingPitch continuation;
    ASSERT_TRUE(melo::noteContinuation(b->staff()->staffTypeForElement(b)->meloStateJson(), hz(a), continuation));
    ASSERT_TRUE(place(b, continuation.nPer, continuation.nGen));
    tie(s, a, b);
    const auto av=a->noteVal(), bv=b->noteVal();
    const double original=hz(a);
    double cents;
    const String state=a->staff()->staffTypeForElement(a)->meloStateJson();
    ASSERT_TRUE(melo::noteCentsAboveExtentLower(state, a->meloNPer(), a->meloNGen(), cents));
    melo::PitchHit expected;
    ASSERT_TRUE(melo::nearestPitch(state, cents, true, a->meloNPer(), a->meloNGen(), expected, u"square-vertex-up"));
    s->select(a, SelectType::SINGLE);
    s->startCmd(muse::TranslatableString::untranslatable("Selected square shape"));
    s->changeAccidental(AccidentalType::SHARP2);
    s->endCmd();
    EXPECT_EQ(a->meloNPer(), expected.nPer);
    EXPECT_EQ(a->meloNGen(), expected.nGen);
    EXPECT_NEAR(hz(a), hz(b), 1e-8);
    EXPECT_TRUE(a->tieFor());
    s->undoRedo(true, nullptr);
    EXPECT_TRUE(a->noteVal() == av);
    EXPECT_TRUE(b->noteVal() == bv);
    EXPECT_DOUBLE_EQ(hz(a), original);
    delete s;
}

TEST(MeloLatticeConformanceTests, TypedShapeRefusesInvalidLaterTieBeforeAnyMutation)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    auto ns=notes(s);
    Note* a=ns[0];
    Note* b=ns[1];
    ASSERT_TRUE(place(a, 0, 0));
    ASSERT_TRUE(place(b, 0, 0));
    tie(s, a, b);
    b->setMeloPitch(INT_MAX, INT_MAX);
    const auto av=a->noteVal(), bv=b->noteVal();
    const auto undo=s->undoStack()->currentIndex();
    s->select(a, SelectType::SINGLE);
    s->startCmd(muse::TranslatableString::untranslatable("Invalid selected shape"));
    s->changeAccidental(AccidentalType::SHARP2);
    EXPECT_TRUE(a->noteVal() == av);
    EXPECT_TRUE(b->noteVal() == bv);
    EXPECT_EQ(MScore::_error, MsError::CANNOT_RESOLVE_LATTICE_NOTE);
    s->endCmd();
    EXPECT_EQ(s->undoStack()->currentIndex(), undo);
    EXPECT_TRUE(a->noteVal() == av);
    EXPECT_TRUE(b->noteVal() == bv);
    delete s;
}

TEST(MeloLatticeConformanceTests, RepitchEntryKeepsAllTiedLatticeCoordinatesCoherent)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
    ASSERT_TRUE(s);
    auto ns=notes(s);
    Note* a=ns[3];
    Note* b=ns[4];
    ASSERT_TRUE(place(a, 0, 0));
    melo::SoundingPitch continuation;
    ASSERT_TRUE(melo::noteContinuation(b->staff()->staffTypeForElement(b)->meloStateJson(), hz(a), continuation));
    ASSERT_TRUE(place(b, continuation.nPer, continuation.nGen));
    tie(s, a, b);
    const NoteVal oldA=a->noteVal(), oldB=b->noteVal();
    const auto originalIndex=s->undoStack()->currentIndex();
    NoteVal requested=a->noteVal();
    requested.meloNGen=1;
    InputState input;
    input.setTrack(a->track());
    input.setSegment(a->chord()->segment());
    input.setLastSegment(a->chord()->segment());
    input.setNoteEntryMethod(NoteEntryMethod::REPITCH);
    s->startCmd(muse::TranslatableString::untranslatable("Repitch tied entry"));
    Note* added=s->addPitch(requested, false, &input);
    s->endCmd();
    ASSERT_TRUE(added);
    EXPECT_EQ(added->meloNGen(), 1);
    EXPECT_NEAR(hz(added), hz(b), 1e-8);
    EXPECT_TRUE(added->tieFor());
    EXPECT_EQ(s->undoStack()->currentIndex(), originalIndex + 1);
    s->undoRedo(true, nullptr);
    EXPECT_EQ(s->undoStack()->currentIndex(), originalIndex);
    EXPECT_TRUE(a->noteVal() == oldA);
    EXPECT_TRUE(b->noteVal() == oldB);
    EXPECT_NEAR(hz(a), hz(b), 1e-8);
    delete s;
}

TEST(MeloLatticeConformanceTests, LinkedTieCloneFollowsOccurrenceLinksAfterCoincidentReordering)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    auto ns=notes(s);
    Note* a=ns[0];
    Note* b=ns[1];
    ASSERT_TRUE(place(a, 0, 0));
    ASSERT_TRUE(place(b, 0, 0));
    for (Note* anchor:{ a, b }) {
        Note* other=Factory::createNote(anchor->chord());
        other->setTrack(0);
        ASSERT_TRUE(place(other, -7, 12));
        anchor->chord()->add(other);
    }
    Score* part=TestUtils::createPart(s);
    ASSERT_TRUE(part);
    auto counterpart=[part](Note* source) -> Note* {
        for (EngravingObject* object:source->linkList()) {
            if (object->score() == part) {
                return toNote(object);
            }
        }
        return nullptr;
    };
    Note* linkedA=counterpart(a);
    Note* linkedB=counterpart(b);
    ASSERT_TRUE(linkedA);
    ASSERT_TRUE(linkedB);
    std::reverse(linkedA->chord()->notes().begin(), linkedA->chord()->notes().end());
    std::reverse(linkedB->chord()->notes().begin(), linkedB->chord()->notes().end());
    s->select(a, SelectType::SINGLE);
    s->select(b, SelectType::ADD);
    s->cmdToggleTie();
    ASSERT_TRUE(a->tieFor());
    EXPECT_EQ(a->tieFor()->endNote(), b);
    ASSERT_TRUE(linkedA->tieFor());
    EXPECT_EQ(linkedA->tieFor()->endNote(), linkedB);
    s->undoRedo(true, nullptr);
    EXPECT_FALSE(a->tieFor());
    EXPECT_FALSE(linkedA->tieFor());
    delete s;
}

TEST(MeloLatticeConformanceTests, PluginInsertionValidatesEveryLinkedDestinationBeforeOwnershipTransfer)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
    ASSERT_TRUE(s);
    Note* a=notes(s).front();
    ASSERT_TRUE(place(a, 0, 0));
    Score* part=TestUtils::createPart(s);
    ASSERT_TRUE(part);
    Note* linked=nullptr;
    for (EngravingObject* object:a->linkList()) {
        if (object->score() == part) {
            linked=toNote(object);
        }
    }
    ASSERT_TRUE(linked);
    StaffType* type=linked->staff()->staffType(linked->tick());
    String invalid=type->meloStateJson();
    invalid.replace(u"\"generator_cents\":700.0", u"\"generator_cents\":-1.0");
    type->setMeloStateJson(invalid);
    Note* raw=Factory::createNote(s->dummy()->chord());
    raw->setMeloPitch(0, 0);
    raw->setPitch(a->pitch(), a->tpc1(), a->tpc2());
    const auto before=a->chord()->notes().size(), linkedBefore=linked->chord()->notes().size();
    const auto index=s->undoStack()->currentIndex();
    {
        apiv1::Note wrapped(raw, apiv1::Ownership::PLUGIN);
        apiv1::Chord destination(a->chord(), apiv1::Ownership::SCORE);
        s->startCmd(muse::TranslatableString::untranslatable("Refuse invalid plugin linked insert"));
        destination.add(&wrapped);
        EXPECT_EQ(wrapped.ownership(), apiv1::Ownership::PLUGIN);
        EXPECT_EQ(a->chord()->notes().size(), before);
        EXPECT_EQ(linked->chord()->notes().size(), linkedBefore);
        s->endCmd();
        EXPECT_EQ(s->undoStack()->currentIndex(), index);
    }
    delete s;
}

TEST(MeloLatticeConformanceTests, PartialTieActivationRevalidatesTheSelectedDestination)
{
    for (bool coherent : { false, true }) {
        auto* s=ScoreRW::readScore(u"jimstaff_data/m9-dense-voices.mscx");
        ASSERT_TRUE(s);
        auto ns=notes(s);
        Note* a=ns[0];
        Note* b=ns[1];
        ASSERT_TRUE(place(a, 0, 0));
        ASSERT_TRUE(place(b, coherent ? 0 : 1, 0));
        PartialTie* outgoing=Factory::createPartialTie(a);
        outgoing->setStartNote(a);
        outgoing->setTrack(a->track());
        outgoing->setTick(a->tick());
        s->startCmd(muse::TranslatableString::untranslatable("Partial tie fixture"));
        s->undoAddElement(outgoing);
        s->endCmd();
        TieJumpPointList* list=a->tieJumpPoints();
        ASSERT_TRUE(list);
        auto* endpoint=new TieJumpPoint(b, false, 0, false);
        list->add(endpoint);
        const auto index=s->undoStack()->currentIndex();
        list->toggleJumpPoint(endpoint->id());
        EXPECT_EQ(endpoint->active(), coherent);
        EXPECT_EQ(bool(b->incomingPartialTie()), coherent);
        EXPECT_EQ(s->undoStack()->currentIndex(), index + (coherent ? 1 : 0));
        if (coherent) {
            s->undoRedo(true, nullptr);
            EXPECT_FALSE(endpoint->active());
            EXPECT_FALSE(b->incomingPartialTie());
        }
        delete s;
    }
}

TEST(MeloLatticeConformanceTests, ExplodePreservesDistinctCoincidentPositions)
{
    auto* s=ScoreRW::readScore(u"jimstaff_data/m9-satb-hymn.mscx");
    ASSERT_TRUE(s);
    Note* first=notes(s).front();
    ASSERT_TRUE(place(first, 0, 0));
    Note* other=Factory::createNote(first->chord());
    other->setTrack(first->track());
    ASSERT_TRUE(place(other, -7, 12));
    first->chord()->add(other);
    s->select(s->firstMeasure(), SelectType::RANGE, 0);
    s->startCmd(muse::TranslatableString::untranslatable("Explode coincident positions"));
    ASSERT_TRUE(s->cmdExplode());
    s->endCmd();
    Segment* segment=s->firstSegment(SegmentType::ChordRest);
    std::set<std::pair<int, int> > identities;
    for (track_idx_t track:{ 0, 4 }) {
        auto* chord=toChord(segment->element(track));
        ASSERT_TRUE(chord);
        ASSERT_EQ(chord->notes().size(), 1u);
        Note* note=chord->notes().front();
        identities.emplace(note->meloNPer(), note->meloNGen());
    }
    EXPECT_EQ(identities, (std::set<std::pair<int, int> > { { 0, 0 }, { -7, 12 } }));
    s->undoRedo(true, nullptr);
    ASSERT_EQ(first->chord()->notes().size(), 2u);
    delete s;
}

TEST(MeloLatticeConformanceTests, TiedInsertionRefusesDuplicateAndInvalidLaterStateWithoutMutation)
{
    for (bool invalidLater : { false, true }) {
        auto* s = ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
        ASSERT_TRUE(s);
        auto ns = notes(s);
        Note* a = ns[3];
        Note* b = ns[4];
        ASSERT_TRUE(place(a, 0, 0));
        melo::SoundingPitch continued;
        ASSERT_TRUE(melo::noteContinuation(b->staff()->staffTypeForElement(b)->meloStateJson(), hz(a), continued));
        ASSERT_TRUE(place(b, continued.nPer, continued.nGen));
        tie(s, a, b);
        NoteVal value = a->noteVal();
        if (invalidLater) {
            value.meloNPer = -7;
            value.meloNGen = 12;
            const_cast<StaffType*>(b->staff()->staffTypeForElement(b))->setMeloStateJson(u"{}");
        }
        std::vector<NoteVal> before;
        for (Note* note : notes(s)) {
            before.push_back(note->noteVal());
        }
        const String state = b->staff()->staffTypeForElement(b)->meloStateJson();
        const auto index = s->undoStack()->currentIndex();
        InputState input;
        input.setTrack(0);
        input.setSegment(a->chord()->segment());
        input.setLastSegment(a->chord()->segment());
        auto* segment = input.segment();
        s->select(a, SelectType::SINGLE);
        s->startCmd(muse::TranslatableString::untranslatable("Refused tied insertion"));
        EXPECT_EQ(s->addPitch(value, true, &input), nullptr);
        s->endCmd();
        const auto after = notes(s);
        ASSERT_EQ(after.size(), before.size());
        for (size_t i = 0; i < after.size(); ++i) {
            EXPECT_TRUE(after[i]->noteVal() == before[i]);
        }
        EXPECT_EQ(input.segment(), segment);
        EXPECT_EQ(s->selection().element(), a);
        EXPECT_EQ(s->undoStack()->currentIndex(), index);
        EXPECT_EQ(a->tieFor()->endNote(), b);
        EXPECT_EQ(b->staff()->staffTypeForElement(b)->meloStateJson(), state);
        delete s;
    }
}

TEST(MeloLatticeConformanceTests, PrefixOverwritePreservesRetainedTailAcrossReferenceChange)
{
    auto* s = ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
    ASSERT_TRUE(s);
    Note* first = notes(s).front();
    ASSERT_TRUE(place(first, 0, 0));
    s->startCmd(muse::TranslatableString::untranslatable("Long note fixture"));
    s->changeCRlen(first->chord(), TDuration(DurationType::V_HALF));
    s->endCmd();
    const Fraction split = first->tick() + Fraction(1, 4);
    String error;
    ASSERT_TRUE(test::relativeKey(s, 0, split, 1, 0, error)) << error.toStdString();
    const double expectedHz = hz(first);
    melo::SoundingPitch expected;
    ASSERT_TRUE(melo::noteContinuation(first->staff()->staffType(split)->meloStateJson(), expectedHz, expected));
    const NoteVal before = first->noteVal();
    const auto index = s->undoStack()->currentIndex();
    s->startCmd(muse::TranslatableString::untranslatable("Overwrite prefix with rest"));
    ASSERT_TRUE(s->setNoteRest(first->chord()->segment(), 0, NoteVal(-1), Fraction(1, 4), DirectionV::AUTO));
    s->endCmd();
    auto* segment = s->tick2segment(split);
    ASSERT_TRUE(segment);
    auto* chord = toChord(segment->element(0));
    ASSERT_TRUE(chord);
    ASSERT_EQ(chord->notes().size(), 1u);
    Note* tail = chord->notes().front();
    EXPECT_NEAR(hz(tail), expectedHz, 1e-8);
    EXPECT_EQ(tail->meloNPer(), expected.nPer);
    EXPECT_EQ(tail->meloNGen(), expected.nGen);
    s->undoRedo(true, nullptr);
    EXPECT_TRUE(first->noteVal() == before);
    EXPECT_EQ(s->undoStack()->currentIndex(), index);
    delete s;
}

TEST(MeloLatticeConformanceTests, PrefixOverwriteRejectsInvalidRetainedTailWithoutMutation)
{
    auto* s = ScoreRW::readScore(u"jimstaff_data/m5-key-up.mscx");
    ASSERT_TRUE(s);
    Note* first = notes(s).front();
    ASSERT_TRUE(place(first, 0, 0));
    s->startCmd(muse::TranslatableString::untranslatable("Long note fixture"));
    s->changeCRlen(first->chord(), TDuration(DurationType::V_HALF));
    s->endCmd();
    const Fraction split = first->tick() + Fraction(1, 4);
    String error;
    ASSERT_TRUE(test::relativeKey(s, 0, split, 1, 0, error));
    const_cast<StaffType*>(first->staff()->staffType(split))->setMeloStateJson(u"{}");
    const NoteVal before = first->noteVal();
    const auto index = s->undoStack()->currentIndex();
    s->select(first, SelectType::SINGLE);
    const auto all = notes(s);
    s->startCmd(muse::TranslatableString::untranslatable("Refuse invalid retained tail"));
    EXPECT_EQ(s->setNoteRest(first->chord()->segment(), 0, NoteVal(-1), Fraction(1, 4), DirectionV::AUTO), nullptr);
    s->endCmd();
    EXPECT_EQ(notes(s), all);
    EXPECT_TRUE(first->noteVal() == before);
    EXPECT_EQ(s->undoStack()->currentIndex(), index);
    EXPECT_EQ(s->selection().element(), first);
    EXPECT_EQ(first->staff()->staffType(split)->meloStateJson(), u"{}");
    delete s;
}

TEST(MeloLatticeConformanceTests, LocalTimeStretchDoesNotValidatePastTheInsertedNote)
{
    auto* s = ScoreRW::readScore(u"../../../share/templates/02-Choral/12-SATB_(MeloPresto_Staff)/12-SATB_(MeloPresto_Staff).mscx");
    ASSERT_TRUE(s);
    TimeSig* signature = Factory::createTimeSig(s->dummy()->segment());
    signature->setSig(Fraction(8, 4), TimeSigType::NORMAL);
    s->startCmd(muse::TranslatableString::untranslatable("Local time fixture"));
    s->cmdAddTimeSig(s->firstMeasure(), 0, signature, true);
    s->endCmd();
    ASSERT_EQ(s->staff(0)->timeStretch(Fraction(0, 1)), Fraction(2, 1));
    NoteVal seed(69);
    seed.hasMeloPitch = true;
    seed.meloNPer = 0;
    seed.meloNGen = 1;
    s->startCmd(muse::TranslatableString::untranslatable("Seed local-time note"));
    ASSERT_TRUE(s->setNoteRest(s->firstSegment(SegmentType::ChordRest), 0, seed, Fraction(1, 4), DirectionV::AUTO));
    s->endCmd();
    Note* first = notes(s).front();
    ASSERT_EQ(first->staff()->timeStretch(first->tick()), Fraction(2, 1));
    ASSERT_TRUE(place(first, 0, 1));
    const NoteVal requested = first->noteVal();
    const Fraction actualEnd = first->tick() + actualTicks(Fraction(1, 4), nullptr, Fraction(2, 1));
    const Fraction probe = first->tick() + Fraction(3, 16);
    String error;
    ASSERT_TRUE(melo::applyChange(s, 0, s->firstMeasure(), probe, u"mode:1", error));
    auto* carrier = melo::changeCarrierAt(s->firstMeasure(), 0, probe);
    ASSERT_TRUE(carrier);
    String incompatible;
    ASSERT_TRUE(melo::retuneGenerator(carrier->staffType()->meloStateJson(), 720.0, incompatible));
    const_cast<StaffType*>(carrier->staffType())->setMeloStateJson(incompatible);
    melo::SoundingPitch invalid;
    ASSERT_FALSE(melo::noteContinuation(incompatible, hz(first), invalid));
    s->startCmd(muse::TranslatableString::untranslatable("Insert within local time"));
    Segment* made = s->setNoteRest(first->chord()->segment(), 0, requested, Fraction(1, 4), DirectionV::AUTO);
    s->endCmd();
    ASSERT_TRUE(made);
    ASSERT_TRUE(made->element(0));
    EXPECT_EQ(toChordRest(made->element(0))->endTick(), actualEnd);
    delete s;
}

TEST(MeloLatticeConformanceTests, SingleNotePasteUsesExplicitSpellingAtCoincidentTuning)
{
    auto* source = ScoreRW::readScore(u"playback/playbackeventsrenderer_data/single_note_no_articulations/no_articulations.mscx");
    ASSERT_TRUE(source);
    Note* copied = notes(source).front();
    copied->setPitch(62);
    copied->setTpcFromPitch();
    ASSERT_FALSE(copied->hasMeloPitch());
    auto data = copied->mimeData();
    ASSERT_FALSE(data.empty());
    auto* s = ScoreRW::readScore(u"jimstaff_data/m9-satb-hymn.mscx");
    ASSERT_TRUE(s);
    melo::TuningController tuning(s, 0);
    ASSERT_TRUE(tuning.beginPreview());
    ASSERT_TRUE(tuning.commit(720));
    Note* first = notes(s).front();
    ASSERT_GT(first->chord()->ticks(), copied->chord()->ticks());
    s->select(first, SelectType::SINGLE);
    const auto all = notes(s);
    std::vector<NoteVal> values;
    for (Note* note : all) {
        values.push_back(note->noteVal());
    }
    const auto index = s->undoStack()->currentIndex();
    s->startCmd(muse::TranslatableString::untranslatable("Refuse ambiguous note paste"));
    ASSERT_TRUE(s->cmdPasteSymbol(data, nullptr, Fraction(1, 1)));
    s->endCmd();
    const auto pasted = notes(s);
    ASSERT_FALSE(pasted.empty());
    EXPECT_TRUE(pasted.front()->hasMeloPitch());
    EXPECT_EQ(pasted.front()->meloNPer(), 0);
    EXPECT_EQ(pasted.front()->meloNGen(), 0);
    EXPECT_EQ(s->undoStack()->currentIndex(), index + 1);
    s->undoRedo(true, nullptr);
    ASSERT_EQ(notes(s), all);
    for (size_t i = 0; i < all.size(); ++i) {
        EXPECT_TRUE(all[i]->noteVal() == values[i]);
    }
    EXPECT_EQ(s->selection().element(), first);
    EXPECT_EQ(s->undoStack()->currentIndex(), index);
    delete s;
    delete source;
}

TEST(MeloLatticeConformanceTests, ReferenceTimelineEditsRefusePartialStaffDisagreement)
{
    for (int action : { 0, 1, 2 }) {
        auto* s = ScoreRW::readScore(u"jimstaff_data/m9-satb-hymn.mscx");
        ASSERT_TRUE(s);
        Measure* first = s->firstMeasure();
        Measure* second = first->nextMeasure();
        String error;
        if (action == 1) {
            ASSERT_TRUE(melo::applyChange(s, 0, first, Fraction(1, 4), u"mode:1", error));
        }
        if (action == 2) {
            ASSERT_TRUE(test::relativeKey(s, 0, second->tick(), 1, 0, error));
        }
        auto* contradictory = s->staff(1)->staffType(Fraction(0, 1));
        String invalid = contradictory->meloStateJson();
        invalid.replace(u"\"step\":\"D\"", u"\"step\":\"E\"");
        contradictory->setMeloStateJson(invalid);
        const std::vector<Fraction> ticks { Fraction(0, 1), Fraction(1, 4), second->tick() };
        std::vector<String> states;
        for (Staff* staff : s->staves()) {
            for (const auto& tick : ticks) {
                states.push_back(staff->staffType(tick)->meloStateJson());
            }
        }
        const auto index = s->undoStack()->currentIndex();
        bool applied = action == 0 ? melo::applyChange(s, 0, first, u"key:1:0", error)
                       : action == 1 ? melo::applyChangeToAllMeloParts(s, first, { u"key:1:0" }, error)
                       : melo::removeChange(s, 0, second, error);
        EXPECT_FALSE(applied);
        EXPECT_FALSE(error.isEmpty());
        EXPECT_EQ(s->undoStack()->currentIndex(), index);
        size_t i = 0;
        for (Staff* staff : s->staves()) {
            for (const auto& tick : ticks) {
                EXPECT_EQ(staff->staffType(tick)->meloStateJson(), states[i++]);
            }
        }
        delete s;
    }
}

TEST(MeloLatticeConformanceTests, ExactEqualTemperamentSatbTuningPreservesWrittenContentAndReopens)
{
    for (const auto& [divisions, generator] : std::vector<std::pair<int, double> > { { 12, 700.0 }, { 7, 1200.0 * 4.0 / 7.0 },
             { 5, 720.0 } }) {
        SCOPED_TRACE(divisions);
        std::unique_ptr<MasterScore> score(ScoreRW::readScore(u"jimstaff_data/m9-satb-hymn.mscx"));
        ASSERT_TRUE(score);
        std::vector<Note*> written;
        std::vector<std::pair<int, int> > coordinates;
        for (Segment* segment = score->firstSegment(SegmentType::ChordRest); segment; segment = segment->next1(SegmentType::ChordRest)) {
            for (track_idx_t track = 0; track < score->ntracks(); ++track) {
                auto* item = segment->element(track);
                if (item && item->isChord()) {
                    for (Note* note : toChord(item)->notes()) {
                        written.push_back(note);
                        coordinates.emplace_back(note->meloNPer(), note->meloNGen());
                    }
                }
            }
        }
        melo::TuningController tuning(score.get(), 0);
        ASSERT_TRUE(tuning.beginPreview());
        ASSERT_TRUE(tuning.commit(generator));
        EXPECT_DOUBLE_EQ(tuning.currentGeneratorCents(), generator);
        for (size_t i = 0; i < written.size(); ++i) {
            EXPECT_EQ(std::make_pair(written[i]->meloNPer(), written[i]->meloNGen()), coordinates[i]);
            melo::SoundingPitch expected;
            ASSERT_TRUE(melo::noteSoundingPitch(written[i]->staff()->staffTypeForElement(written[i])->meloStateJson(),
                                                coordinates[i].first, coordinates[i].second, expected));
            EXPECT_EQ(written[i]->pitch(), expected.midiKey);
            EXPECT_NEAR(written[i]->tuning(), expected.centsOffset, 1e-9);
        }
        QTemporaryDir directory;
        const String path = String::fromQString(directory.filePath("tempered-hymn.mscx"));
        ASSERT_TRUE(ScoreRW::saveScore(score.get(), path));
        std::unique_ptr<MasterScore> reopened(ScoreRW::readScore(path, true));
        ASSERT_TRUE(reopened);
        melo::TuningController loaded(reopened.get(), 0);
        EXPECT_DOUBLE_EQ(loaded.currentGeneratorCents(), generator);
        EXPECT_EQ(reopened->parts().size(), 4u);
        if (const char* output = std::getenv("MELO_LATTICE_SENSORY_OUT")) {
            ASSERT_TRUE(ScoreRW::saveScore(reopened.get(), String::fromUtf8(output) + u"/hymn-" + String::number(divisions) + u"tet.mscx"));
        }
    }
}
