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

// Native V5 MusicXML import preserves a spelled composition reference and
// structural relative events. Kernel validation gates reference-free configurations.

#include <gtest/gtest.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <functional>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "engraving/dom/masterscore.h"
#include "engraving/iengravingfont.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/fret.h"
#include "engraving/dom/harmony.h"
#include "engraving/rendering/paintoptions.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/lyrics.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafflines.h"
#include "engraving/dom/stafftype.h"
#include "engraving/dom/system.h"
#include "engraving/dom/stafftypechange.h"
#include "engraving/editing/transpose.h"
#include "engraving/melo/melochange.h"
#include "engraving/melo/melochangecontroller.h"
#include "engraving/melo/melotuningcontroller.h"
#include "engraving/style/style.h"

#include "importexport/musicxml/internal/import/importmusicxml.h"
#include "importexport/musicxml/internal/export/exportmusicxml.h"
#include "importexport/musicxml/imusicxmlconfiguration.h"
#include "engraving/melo/melobridge.h"
#include "engraving/rw/xmlwriter.h"
#include "engraving/rw/mscsaver.h"
#include "engraving/infrastructure/mscwriter.h"
#include "engraving/melo/melointerchange.h"
#include "engraving/playback/renderingcontext.h"
#include "engraving/playback/playbackeventsrenderer.h"
#include "engraving/playback/playbackcontext.h"
#include "mpe/tests/utils/articulationutils.h"
#include "io/buffer.h"
#include "io/file.h"
#include "io/fileinfo.h"
#include "io/dir.h"
#include "engraving/tests/utils/scorerw.h"
#include "draw/bufferedpaintprovider.h"
#include "draw/painter.h"

using namespace mu;
using namespace mu::engraving;
using namespace mu::engraving::rendering;
using namespace mu::iex::musicxml;
using namespace muse::draw;

static const String MELO_DATA_DIR(u"data/jims/");

namespace {
String exportToScratch(MasterScore* score, const char* name);
String readAll(const String& path);
}

class MusicXml_Melo_Tests : public ::testing::Test
{
public:
    MasterScore* readMelo(const char* file)
    {
        auto importXml = [](MasterScore* score, const muse::io::path_t& path) -> engraving::Err {
            return importMusicXml(score, path.toQString(), false);
        };
        return ScoreRW::readScore(MELO_DATA_DIR + String::fromUtf8(file), false, importXml);
    }

    static const StaffType* staffTypeAtStart(Score* score, staff_idx_t staffIdx = 0)
    {
        return score->staff(staffIdx)->staffType(Fraction(0, 1));
    }

    static Measure* measureNo(Score* score, int n)
    {
        Measure* m = score->firstMeasure();
        for (int i = 1; m && i < n; ++i) {
            m = m->nextMeasure();
        }
        return m;
    }

    static std::vector<Harmony*> harmoniesInOrder(Score* score)
    {
        std::vector<Harmony*> result;
        for (Segment* segment = score->firstSegment(SegmentType::ChordRest); segment;
             segment = segment->next1(SegmentType::ChordRest)) {
            for (EngravingItem* item : segment->annotations()) {
                if (item && item->isHarmony()) {
                    result.push_back(toHarmony(item));
                }
            }
        }
        return result;
    }

    static std::vector<Harmony*> harmoniesOnStaff(Score* score, staff_idx_t staffIdx)
    {
        std::vector<Harmony*> result;
        for (Harmony* harmony : harmoniesInOrder(score)) {
            if (harmony->staffIdx() == staffIdx) {
                result.push_back(harmony);
            }
        }
        return result;
    }

    static std::vector<const Note*> notesInOrder(Score* score, staff_idx_t staffIdx = 0)
    {
        std::vector<const Note*> out;
        for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
            for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
                for (voice_idx_t v = 0; v < VOICES; ++v) {
                    EngravingItem* e = s->element(staffIdx * VOICES + v);
                    if (e && e->isChord()) {
                        for (const Note* n : toChord(e)->notes()) {
                            out.push_back(n);
                        }
                    }
                }
            }
        }
        return out;
    }
};

// The exact converter output for the accepted m5-key-mode piece
// (jims-evidence/m5-acceptance/m5-key-mode/m5-key-mode.mscx): byte-shape,
// key order, no spaces, tonic_ambit last.
static const char* KEY_MODE_STATE_1
    = "{\"scale\":[\"M2\",\"m2\",\"M2\",\"M2\",\"M2\",\"m2\",\"M2\"],\"collection_rotation\":0,\"mode_rotation\":0,"
      "\"generator_cents\":700.0,\"period_cents\":1200.0,\"embedding\":{\"large_steps\":5,\"small_steps\":2},"
      "\"extent\":{\"lower\":{\"nPer\":1,\"nGen\":-2},\"upper\":{\"nPer\":2,\"nGen\":-2}},\"schema\":\"jimstaff-v3\","
      "\"tonic_ambit\":\"tonic-bounded\"}";
static const char* KEY_MODE_STATE_2
    = "{\"scale\":[\"M2\",\"m2\",\"M2\",\"M2\",\"M2\",\"m2\",\"M2\"],\"collection_rotation\":0,\"mode_rotation\":5,"
      "\"generator_cents\":700.0,\"period_cents\":1200.0,\"embedding\":{\"large_steps\":5,\"small_steps\":2},"
      "\"extent\":{\"lower\":{\"nPer\":1,\"nGen\":-2},\"upper\":{\"nPer\":2,\"nGen\":-2}},\"schema\":\"jimstaff-v3\","
      "\"tonic_ambit\":\"tonic-bounded\"}";

static String sharedState(const String& state)
{
    String shared;
    String error;
    if (!melo::musicxmlConfigurationV5Xml(state, 0, true, shared, error)) {
        return String();
    }
    return shared;
}

TEST_F(MusicXml_Melo_Tests, v5ImportBuildsCanonicalStaffConfigurationsAndRelativeTimeline)
{
    MasterScore* score = readMelo("v5/jims-v3-m5-key-mode.musicxml");
    ASSERT_TRUE(score);
    ASSERT_EQ(score->nstaves(), 1u);
    const StaffType* st = staffTypeAtStart(score);
    ASSERT_TRUE(st);
    EXPECT_TRUE(st->isMelo());
    EXPECT_EQ(st->xmlName(), String(u"jims12tet"));
    EXPECT_EQ(st->lines(), 13);
    // Load-time reconciliation replaces the serialized extent with the exact
    // written-note bounds. All song-wide content remains the converter's.
    EXPECT_EQ(sharedState(st->meloStateJson()), sharedState(String::fromUtf8(KEY_MODE_STATE_1)));
    EXPECT_TRUE(st->meloJiLines());
    EXPECT_EQ(st->meloTonicAmbit(), u"tonic-bounded");
    // The change measure carries the complete second state (never derived from melo:change).
    Measure* m2 = measureNo(score, 2);
    ASSERT_TRUE(m2);
    const StaffTypeChange* stc = melo::changeCarrier(m2, 0);
    ASSERT_TRUE(stc);
    ASSERT_TRUE(stc->staffType());
    EXPECT_EQ(sharedState(stc->staffType()->meloStateJson()), sharedState(String::fromUtf8(KEY_MODE_STATE_2)));
    EXPECT_TRUE(stc->staffType()->meloStateJson().contains(u"\"tonic_ambit\":\"tonic-"))
        << stc->staffType()->meloStateJson().toStdString();
    EXPECT_FALSE(melo::changeCarrier(measureNo(score, 1), 0));
    EXPECT_FALSE(melo::changeCarrier(measureNo(score, 3), 0));
    // Every pitched note carries its Kernel identity from melo:pitch.
    auto notes = notesInOrder(score);
    ASSERT_EQ(notes.size(), 12u);
    for (const Note* n : notes) {
        EXPECT_TRUE(n->hasMeloPitch());
    }
    EXPECT_EQ(notes[0]->meloNPer(), -4);
    EXPECT_EQ(notes[0]->meloNGen(), 7);
    EXPECT_EQ(notes[1]->meloNPer(), 3);
    EXPECT_EQ(notes[1]->meloNGen(), -5);
    // Engraving font seam (Milestone 3).
    EXPECT_EQ(score->style().value(Sid::musicalSymbolFont).value<String>(), String(u"JiMSMusic"));
    EXPECT_FALSE(score->style().value(Sid::hideInstrumentNameIfOneInstrument).toBool());
    delete score;
}

TEST_F(MusicXml_Melo_Tests, midBarStateChangeImportsAndExportsAtItsExactTick)
{
    MasterScore* score = readMelo("v5/jims-mid-bar-state-change.musicxml");
    ASSERT_TRUE(score);
    const std::vector<const Note*> notes = notesInOrder(score);
    ASSERT_EQ(notes.size(), 4u);
    const Fraction changeTick = notes[2]->tick();
    Measure* measure = measureNo(score, 1);
    ASSERT_TRUE(measure);
    ASSERT_GT(changeTick, measure->tick());
    ASSERT_LT(changeTick, measure->endTick());
    const StaffTypeChange* carrier = melo::changeCarrierAt(measure, 0, changeTick);
    ASSERT_TRUE(carrier);
    EXPECT_EQ(carrier->rtick(), changeTick - measure->tick());
    const StaffType* oldStaffType = notes[0]->staff()->staffTypeForElement(notes[0]);
    const StaffType* newStaffType = notes[2]->staff()->staffTypeForElement(notes[2]);
    ASSERT_TRUE(oldStaffType);
    ASSERT_TRUE(newStaffType);
    EXPECT_TRUE(oldStaffType->meloStateJson().contains(u"\"mode_rotation\":0"));
    EXPECT_TRUE(notes[1]->staff()->staffTypeForElement(notes[1])->meloStateJson().contains(u"\"mode_rotation\":0"));
    EXPECT_TRUE(newStaffType->meloStateJson().contains(u"\"mode_rotation\":5"));
    EXPECT_TRUE(notes[3]->staff()->staffTypeForElement(notes[3])->meloStateJson().contains(u"\"mode_rotation\":5"));
    EXPECT_TRUE(oldStaffType->meloStateJson().contains(u"jimstaff-request-v3"));
    EXPECT_TRUE(newStaffType->meloStateJson().contains(u"jimstaff-request-v3"));

    melo::ChangeIndicator indicator;
    ASSERT_TRUE(melo::midBarChangeIndicator(carrier, indicator));
    EXPECT_EQ(indicator.kinds, std::vector<String>({ u"mode" }));
    EXPECT_TRUE(indicator.dotStacks.empty());
    ASSERT_EQ(indicator.tonicIndicators.size(), 2u);
    EXPECT_EQ(indicator.tonicIndicators[0].label, u"Do");
    EXPECT_EQ(indicator.tonicIndicators[1].label, u"La");
    ASSERT_EQ(indicator.arrows.size(), 1u);
    EXPECT_EQ(indicator.arrows[0].kind, u"mode");
    EXPECT_TRUE(indicator.arrows[0].trumps.isEmpty());

    const String out = exportToScratch(score, "jims-mid-bar-state-change-roundtrip.musicxml");
    const String xml = readAll(out);
    EXPECT_EQ(xml.count(u"<melo:staff-state"), 2u);
    const size_t firstNote = xml.indexOf(u"<note");
    const size_t secondNote = xml.indexOf(u"<note", firstNote + 1);
    const size_t thirdNote = xml.indexOf(u"<note", secondNote + 1);
    const size_t firstState = xml.indexOf(u"<melo:staff-state");
    const size_t secondState = xml.indexOf(u"<melo:staff-state", firstState + 1);
    ASSERT_NE(secondState, muse::nidx);
    EXPECT_GT(secondState, secondNote);
    EXPECT_LT(secondState, thirdNote);

    auto importXml = [](MasterScore* target, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(target, path.toQString(), false);
    };
    MasterScore* again = ScoreRW::readScore(out, true, importXml);
    ASSERT_TRUE(again);
    Measure* againMeasure = measureNo(again, 1);
    ASSERT_TRUE(againMeasure);
    EXPECT_TRUE(melo::changeCarrierAt(againMeasure, 0, changeTick));
    EXPECT_EQ(notesInOrder(again).size(), 4u);
    delete again;
    delete score;
}

TEST_F(MusicXml_Melo_Tests, explicitTonicAmbitsSurviveNativeScoreReload)
{
    MasterScore* score = readMelo("v5/jims-mid-bar-state-change.musicxml");
    ASSERT_TRUE(score);
    StaffType* base = score->staff(0)->staffType(Fraction(0, 1));
    ASSERT_TRUE(base);
    String baseState = base->meloStateJson();
    baseState.replace(u"\"tonic_ambit\":\"tonic-bounded\"", u"\"tonic_ambit\":\"tonic-centered\"");
    base->setMeloStateJson(baseState);
    ASSERT_EQ(base->meloTonicAmbit(), u"tonic-centered");

    const String dir(u"jims-export-scratch");
    muse::io::Dir::mkpath(dir);
    const String mscz = dir + u"/explicit-tonic-ambit-roundtrip.mscz";
    muse::io::File::remove(mscz);
    {
        muse::io::File file(mscz);
        ASSERT_TRUE(file.open(muse::io::IODevice::WriteOnly));
        MscWriter::Params params;
        params.device = &file;
        params.filePath = mscz;
        params.mode = MscIoMode::Zip;
        MscWriter writer(params);
        ASSERT_TRUE(writer.open());
        MscSaver saver(score->iocContext());
        ASSERT_TRUE(saver.writeMscz(score, writer, false));
        writer.close();
        file.close();
    }
    delete score;

    MasterScore* reloaded = ScoreRW::readScore(mscz, true);
    ASSERT_TRUE(reloaded);
    const StaffType* reloadedBase = reloaded->staff(0)->staffType(Fraction(0, 1));
    ASSERT_TRUE(reloadedBase);
    EXPECT_EQ(reloadedBase->meloTonicAmbit(), u"tonic-centered");
    const std::vector<const Note*> notes = notesInOrder(reloaded);
    ASSERT_EQ(notes.size(), 4u);
    const StaffTypeChange* change = melo::changeCarrierAt(measureNo(reloaded, 1), 0, notes[2]->tick());
    ASSERT_TRUE(change);
    ASSERT_TRUE(change->staffType());
    EXPECT_EQ(change->staffType()->meloTonicAmbit(), u"tonic-bounded");
    delete reloaded;
}

TEST_F(MusicXml_Melo_Tests, midBarIndicatorElementsAlignWithTheirDisplayedStaffNoteLines)
{
    MasterScore* score = readMelo("v5/jims-mid-bar-state-change.musicxml");
    ASSERT_TRUE(score);
    score->doLayout();
    Measure* measure = measureNo(score, 1);
    ASSERT_TRUE(measure);
    const std::vector<const Note*> notes = notesInOrder(score);
    ASSERT_EQ(notes.size(), 4u);
    const Fraction changeTick = notes[2]->tick();
    const StaffTypeChange* carrier = melo::changeCarrierAt(measure, 0, changeTick);
    ASSERT_TRUE(carrier);
    melo::ChangeIndicator indicator;
    const StaffType* changedStaffType = nullptr;
    ASSERT_TRUE(melo::midBarChangeIndicator(carrier, indicator, &changedStaffType));
    ASSERT_TRUE(changedStaffType);

    const StaffLines* lines = measure->staffLines(0);
    ASSERT_TRUE(lines);
    size_t doLineCount = 0;
    for (const StaffLines::MeloGuideLine& guide : lines->meloGuideLines()) {
        if (!guide.dashed && guide.colorStyle == Sid::meloDoLineColor) {
            ++doLineCount;
        }
    }
    EXPECT_EQ(doLineCount, 2u) << "one-period tonic-bounded MeloPresto Staff must be Do-to-Do";
    const StaffType* displayedStaffType = score->staff(0)->staffType(measure->tick());
    ASSERT_TRUE(displayedStaffType);
    ASSERT_TRUE(displayedStaffType->isMelo());
    ASSERT_NE(changedStaffType, displayedStaffType);
    const StaffType::MeloFrameView& wholeView = displayedStaffType->meloFrameView(score, 0, nullptr);
    ASSERT_FALSE(wholeView.empty());
    EXPECT_NEAR(wholeView.bottomCents(), 0.0, 1e-6);
    EXPECT_NEAR(wholeView.topCents(), 1200.0, 1e-6);
    const StaffType::MeloFrameView& view
        = displayedStaffType->meloFrameView(score, 0, measure->system());
    ASSERT_FALSE(view.empty());
    EXPECT_NEAR(view.bottomCents(), 0.0, 1e-6);
    EXPECT_NEAR(view.topCents(), 1200.0, 1e-6);
    ASSERT_FALSE(view.bands.front().segments.empty());
    EXPECT_NEAR(view.bands.front().segments.front().lowerCents, 0.0, 1e-6);
    EXPECT_NEAR(view.bands.back().segments.back().upperCents, 1200.0, 1e-6);
    melo::PeriodicOrigins origins;
    ASSERT_TRUE(melo::periodicOrigins(displayedStaffType->meloStateJson(), origins));
    EXPECT_NEAR(origins.doCentsAboveExtentLower, 0.0, 1e-6);
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
    Painter painter(provider, "midbar-musicxml-lines");
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
    const Segment* anchor = measure->findSegmentR(Segment::CHORD_REST_OR_TIME_TICK_TYPE, carrier->rtick());
    ASSERT_TRUE(anchor);
    const double expectedNoteGap = score->style().styleMM(Sid::barNoteDistance);
    EXPECT_GE(anchor->x() - flankXs.back(), expectedNoteGap - 1e-6)
        << "the note after a mid-bar indicator must clear its right dashed flank";
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

TEST_F(MusicXml_Melo_Tests, numericAndMissingReferencesAreRefusedWithoutChangingInput)
{
    for (const char* file : { "jims-reference-none.musicxml", "jims-reference-none-explicit.musicxml",
                              "jims-reference-pitch.musicxml", "jims-reference-pitch-class.musicxml", "jims-reference-hertz.musicxml",
                              "jims-v1-collision.musicxml", "jims-v2-mode-change.musicxml", "jims-v3-m5-key-mode.musicxml" }) {
        const String path = ScoreRW::rootPath() + u"/" + MELO_DATA_DIR + String::fromUtf8(file);
        const String before = readAll(path);
        std::unique_ptr<MasterScore> refused(readMelo(file));
        EXPECT_FALSE(refused) << file;
        EXPECT_EQ(readAll(path), before);
    }
}

TEST_F(MusicXml_Melo_Tests, namespaceIsResolvedByUriNotByPrefix)
{
    MasterScore* a = readMelo("v5/jims-v3-m5-mode.musicxml");
    MasterScore* b = readMelo("v5/jims-v3-other-prefix.musicxml");
    ASSERT_TRUE(a && b);
    EXPECT_EQ(staffTypeAtStart(a)->meloStateJson(), staffTypeAtStart(b)->meloStateJson());
    ASSERT_TRUE(melo::changeCarrier(measureNo(b, 2), 0));
    EXPECT_EQ(melo::changeCarrier(measureNo(a, 2), 0)->staffType()->meloStateJson(),
              melo::changeCarrier(measureNo(b, 2), 0)->staffType()->meloStateJson());
    EXPECT_EQ(notesInOrder(b).size(), 12u);
    EXPECT_TRUE(notesInOrder(b)[0]->hasMeloPitch());
    delete a;
    delete b;
}

TEST_F(MusicXml_Melo_Tests, unknownMeloNamespaceVersionIsAFatalImportError)
{
    // A document that declares itself MeloPresto with a version this fork does not
    // know must not silently import as a plain five-line staff.
    MasterScore* score = readMelo("jims-unknown-version-invalid.musicxml");
    EXPECT_FALSE(score);
    delete score;
}

TEST_F(MusicXml_Melo_Tests, ChordNameV4ImportsAsOpaquePerObjectHarmonyBesideStandardHarmony)
{
    MasterScore* score = readMelo("v5/jims-chord-name-v4.musicxml");
    ASSERT_TRUE(score);
    ASSERT_EQ(score->nstaves(), 2u);
    ASSERT_TRUE(staffTypeAtStart(score, 0));
    ASSERT_TRUE(staffTypeAtStart(score, 1));
    EXPECT_TRUE(staffTypeAtStart(score, 0)->isMelo());
    EXPECT_FALSE(staffTypeAtStart(score, 1)->isMelo());
    const std::vector<const Note*> meloNotes = notesInOrder(score, 0);
    const std::vector<const Note*> stockNotes = notesInOrder(score, 1);
    ASSERT_EQ(meloNotes.size(), 2u);
    ASSERT_EQ(stockNotes.size(), 2u);
    EXPECT_TRUE(meloNotes[0]->hasMeloPitch());
    EXPECT_TRUE(meloNotes[1]->hasMeloPitch());
    EXPECT_EQ(meloNotes[0]->meloNPer(), 1);
    EXPECT_EQ(meloNotes[0]->meloNGen(), -2);
    EXPECT_EQ(meloNotes[1]->meloNPer(), 2);
    EXPECT_EQ(meloNotes[1]->meloNGen(), -2);
    EXPECT_TRUE(staffTypeAtStart(score, 0)->meloStateJson().contains(
                    u"\"extent\":{\"lower\":{\"nPer\":1,\"nGen\":-2},\"upper\":{\"nPer\":2,\"nGen\":-2}}"));
    EXPECT_FALSE(stockNotes[0]->hasMeloPitch());
    EXPECT_FALSE(stockNotes[1]->hasMeloPitch());
    score->doLayout();
    const std::vector<Harmony*> harmonies = harmoniesInOrder(score);
    const std::vector<Harmony*> meloHarmonies = harmoniesOnStaff(score, 0);
    const std::vector<Harmony*> stockHarmonies = harmoniesOnStaff(score, 1);
    ASSERT_EQ(harmonies.size(), 4u);
    ASSERT_EQ(meloHarmonies.size(), 2u);
    ASSERT_EQ(stockHarmonies.size(), 2u);
    EXPECT_EQ(meloHarmonies[0]->harmonyType(), HarmonyType::MELO);
    EXPECT_EQ(meloHarmonies[0]->harmonyName(), u"!So7/3");
    EXPECT_EQ(meloHarmonies[0]->tick(), Fraction(0, 1));
    EXPECT_EQ(meloHarmonies[0]->staffIdx(), 0u);
    EXPECT_EQ(meloHarmonies[0]->placement(), PlacementV::ABOVE);
    EXPECT_FALSE(meloHarmonies[0]->isPlayable());
    EXPECT_FALSE(meloHarmonies[0]->isRealizable());
    ASSERT_EQ(meloHarmonies[0]->chords().size(), 1u);
    EXPECT_EQ(meloHarmonies[0]->chords().front()->textName(), u"!So7/3");
    EXPECT_EQ(meloHarmonies[0]->chords().front()->rootTpc(), Tpc::TPC_INVALID);
    EXPECT_GT(meloHarmonies[0]->ldata()->bbox().width(), 0.0);
    EXPECT_EQ(meloHarmonies[0]->ldata()->renderItemList().size(), 1u);
    EXPECT_EQ(meloHarmonies[1]->harmonyType(), HarmonyType::MELO);
    EXPECT_EQ(meloHarmonies[1]->harmonyName(), u"Re:So7");
    EXPECT_EQ(meloHarmonies[1]->tick(), Fraction(1, 1));
    EXPECT_EQ(meloHarmonies[1]->staffIdx(), 0u);
    EXPECT_EQ(meloHarmonies[1]->placement(), PlacementV::ABOVE);
    EXPECT_GT(meloHarmonies[1]->ldata()->bbox().width(), 0.0);
    EXPECT_EQ(meloHarmonies[1]->ldata()->renderItemList().size(), 1u);
    EXPECT_EQ(stockHarmonies[0]->harmonyType(), HarmonyType::STANDARD);
    EXPECT_EQ(stockHarmonies[1]->harmonyType(), HarmonyType::STANDARD);
    EXPECT_TRUE(tpcIsValid(stockHarmonies[0]->rootTpc()));
    EXPECT_TRUE(tpcIsValid(stockHarmonies[1]->rootTpc()));
    delete score;
}

TEST_F(MusicXml_Melo_Tests, ChordNameEditingKeepsTheWholeOpaqueStringAndRefusesTilde)
{
    MasterScore* score = readMelo("v5/jims-chord-name-v4.musicxml");
    ASSERT_TRUE(score);
    const std::vector<Harmony*> harmonies = harmoniesOnStaff(score, 0);
    ASSERT_EQ(harmonies.size(), 2u);
    Harmony* melo = harmonies.front();
    const String names[] = { u"Do5", u"Fa5", u"Do:La7", u"!So7/3", u"Do5|Fa5", u"Fi@Te:M3²+La,Ti/Re" };
    for (const String& name : names) {
        melo->setHarmony(name);
        ASSERT_EQ(melo->chords().size(), 1u) << name.toStdString();
        EXPECT_EQ(melo->chords().front()->textName(), name) << name.toStdString();
        EXPECT_EQ(melo->harmonyName(), name) << name.toStdString();
        EXPECT_EQ(melo->chords().front()->rootTpc(), Tpc::TPC_INVALID) << name.toStdString();
    }
    const String before = melo->harmonyName();
    melo->setHarmony(u"~So7/3");
    EXPECT_EQ(melo->harmonyName(), before);
    delete score;
}

TEST_F(MusicXml_Melo_Tests, ChordNameTranspositionLeavesMeloOpaqueAndTransposesStandardHarmony)
{
    MasterScore* score = readMelo("v5/jims-chord-name-v4.musicxml");
    ASSERT_TRUE(score);
    const std::vector<Harmony*> beforeMelo = harmoniesOnStaff(score, 0);
    const std::vector<Harmony*> beforeStock = harmoniesOnStaff(score, 1);
    ASSERT_EQ(beforeMelo.size(), 2u);
    ASSERT_EQ(beforeStock.size(), 2u);
    const String firstMelo = beforeMelo[0]->harmonyName();
    const String secondMelo = beforeMelo[1]->harmonyName();
    const int firstStandardRoot = beforeStock[0]->rootTpc();
    const int secondStandardRoot = beforeStock[1]->rootTpc();

    score->cmdSelectAll();
    score->startCmd(TranslatableString::untranslatable("Test MeloPresto chord-name transposition"));
    Transpose::transpose(score, TransposeMode::BY_INTERVAL, TransposeDirection::UP, Key::C, 4,
                         true, true, true);
    score->endCmd();

    const std::vector<Harmony*> afterMelo = harmoniesOnStaff(score, 0);
    const std::vector<Harmony*> afterStock = harmoniesOnStaff(score, 1);
    ASSERT_EQ(afterMelo.size(), 2u);
    ASSERT_EQ(afterStock.size(), 2u);
    EXPECT_EQ(afterMelo[0]->harmonyName(), firstMelo);
    EXPECT_EQ(afterMelo[1]->harmonyName(), secondMelo);
    EXPECT_EQ(afterMelo[0]->chords().front()->rootTpc(), Tpc::TPC_INVALID);
    EXPECT_EQ(afterMelo[1]->chords().front()->rootTpc(), Tpc::TPC_INVALID);
    EXPECT_NE(afterStock[0]->rootTpc(), firstStandardRoot);
    EXPECT_NE(afterStock[1]->rootTpc(), secondStandardRoot);
    delete score;
}

TEST_F(MusicXml_Melo_Tests, ChordNameV4SurvivesNativeAndMusicXmlRoundTripsExactly)
{
    MasterScore* score = readMelo("v5/jims-chord-name-v4.musicxml");
    ASSERT_TRUE(score);
    const String dir(u"jims-export-scratch");
    muse::io::Dir::mkpath(dir);
    MasterScore* native = nullptr;
    for (const String& extension : { String(u"mscx"), String(u"mscz") }) {
        const String nativePath = dir + u"/jims-chord-name-native." + extension;
        if (extension == u"mscz") {
            muse::io::File::remove(nativePath);
            muse::io::File file(nativePath);
            ASSERT_TRUE(file.open(muse::io::IODevice::WriteOnly));
            MscWriter::Params params;
            params.device = &file;
            params.filePath = nativePath;
            params.mode = MscIoMode::Zip;
            MscWriter writer(params);
            ASSERT_TRUE(writer.open());
            MscSaver saver(score->iocContext());
            ASSERT_TRUE(saver.writeMscz(score, writer, false));
            writer.close();
            file.close();
        } else {
            ASSERT_TRUE(ScoreRW::saveScore(score, nativePath));
        }
        MasterScore* loaded = ScoreRW::readScore(nativePath, true);
        ASSERT_TRUE(loaded) << extension.toStdString();
        ASSERT_EQ(loaded->nstaves(), 2u) << extension.toStdString();
        EXPECT_TRUE(staffTypeAtStart(loaded, 0)->isMelo()) << extension.toStdString();
        EXPECT_FALSE(staffTypeAtStart(loaded, 1)->isMelo()) << extension.toStdString();
        ASSERT_EQ(harmoniesInOrder(loaded).size(), 4u) << extension.toStdString();
        ASSERT_EQ(harmoniesOnStaff(loaded, 0).size(), 2u) << extension.toStdString();
        ASSERT_EQ(harmoniesOnStaff(loaded, 1).size(), 2u) << extension.toStdString();
        EXPECT_EQ(harmoniesOnStaff(loaded, 0)[0]->harmonyType(), HarmonyType::MELO) << extension.toStdString();
        EXPECT_EQ(harmoniesOnStaff(loaded, 0)[0]->harmonyName(), u"!So7/3") << extension.toStdString();
        if (extension == u"mscz") {
            native = loaded;
        } else {
            delete loaded;
        }
    }
    ASSERT_TRUE(native);

    const String out = exportToScratch(native, "jims-chord-name-roundtrip.musicxml");
    const String xml = readAll(out);
    EXPECT_TRUE(xml.contains(u"xmlns:melo=\"urn:melopresto:musicxml:5\""));
    EXPECT_EQ(xml.count(u"<melo:chord-name>!So7/3</melo:chord-name>"), 1);
    EXPECT_EQ(xml.count(u"<melo:chord-name>Re:So7</melo:chord-name>"), 1);
    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    MasterScore* again = ScoreRW::readScore(out, true, importXml);
    ASSERT_TRUE(again);
    ASSERT_EQ(again->nstaves(), 2u);
    EXPECT_TRUE(staffTypeAtStart(again, 0)->isMelo());
    EXPECT_FALSE(staffTypeAtStart(again, 1)->isMelo());
    const std::vector<Harmony*> againMelo = harmoniesOnStaff(again, 0);
    const std::vector<Harmony*> againStock = harmoniesOnStaff(again, 1);
    ASSERT_EQ(harmoniesInOrder(again).size(), 4u);
    ASSERT_EQ(againMelo.size(), 2u);
    ASSERT_EQ(againStock.size(), 2u);
    EXPECT_EQ(againMelo[0]->harmonyType(), HarmonyType::MELO);
    EXPECT_EQ(againMelo[0]->harmonyName(), u"!So7/3");
    EXPECT_EQ(againMelo[1]->harmonyType(), HarmonyType::MELO);
    EXPECT_EQ(againMelo[1]->harmonyName(), u"Re:So7");
    EXPECT_EQ(againStock[0]->harmonyType(), HarmonyType::STANDARD);
    EXPECT_EQ(againStock[1]->harmonyType(), HarmonyType::STANDARD);
    delete score;
    delete native;
    delete again;
}

TEST_F(MusicXml_Melo_Tests, ChordNameV4PreservesOffsetStaffAndSupportedFormatting)
{
    MasterScore* score = readMelo("v5/jims-chord-name-offset-staff-format-v4.musicxml");
    ASSERT_TRUE(score);
    ASSERT_EQ(score->nstaves(), 2u);
    EXPECT_FALSE(staffTypeAtStart(score, 0)->isMelo());
    EXPECT_TRUE(staffTypeAtStart(score, 1)->isMelo());
    const std::vector<Harmony*> imported = harmoniesInOrder(score);
    ASSERT_EQ(imported.size(), 1u);
    Harmony* harmony = imported.front();
    EXPECT_EQ(harmony->harmonyType(), HarmonyType::MELO);
    EXPECT_EQ(harmony->harmonyName(), u"La:So7");
    EXPECT_EQ(harmony->tick(), Fraction(1, 4));
    EXPECT_EQ(harmony->staffIdx(), 1u);
    EXPECT_EQ(harmony->placement(), PlacementV::BELOW);
    EXPECT_FALSE(harmony->visible());
    EXPECT_EQ(harmony->color(), Color::fromString("#112233"));
    EXPECT_EQ(harmony->family(), u"Edwin");
    EXPECT_DOUBLE_EQ(harmony->size(), 13.0);
    EXPECT_TRUE(harmony->fontStyle() & FontStyle::Italic);
    EXPECT_TRUE(harmony->fontStyle() & FontStyle::Bold);
    EXPECT_EQ(harmony->propertyFlags(Pid::OFFSET), PropertyFlags::UNSTYLED);
    EXPECT_NEAR(harmony->offset().x() / harmony->spatium(), 2.0, 0.01);
    EXPECT_NEAR(harmony->offset().y() / harmony->spatium(), 1.0, 0.01);

    auto musicXmlConfiguration = muse::modularity::globalIoc()->resolve<IMusicXmlConfiguration>("iex_musicxml");
    ASSERT_TRUE(musicXmlConfiguration);
    musicXmlConfiguration->setExportLayout(true);
    const String out = exportToScratch(score, "jims-chord-name-offset-staff-format-roundtrip.musicxml");
    musicXmlConfiguration->setExportLayout(false);
    const String xml = readAll(out);
    EXPECT_TRUE(xml.contains(u"<offset>"));
    EXPECT_TRUE(xml.contains(u"<staff>2</staff>"));
    EXPECT_TRUE(xml.contains(u"font-family=\"Edwin\""));
    EXPECT_TRUE(xml.contains(u"font-size=\"13\""));
    EXPECT_TRUE(xml.contains(u"font-style=\"italic\""));
    EXPECT_TRUE(xml.contains(u"font-weight=\"bold\""));
    EXPECT_TRUE(xml.contains(u"relative-x=\"20"));
    EXPECT_TRUE(xml.contains(u"relative-y=\"-10"));

    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    MasterScore* again = ScoreRW::readScore(out, true, importXml);
    ASSERT_TRUE(again);
    const std::vector<Harmony*> roundTripped = harmoniesInOrder(again);
    ASSERT_EQ(roundTripped.size(), 1u);
    const Harmony* roundTrip = roundTripped.front();
    EXPECT_EQ(roundTrip->harmonyType(), HarmonyType::MELO);
    EXPECT_EQ(roundTrip->harmonyName(), u"La:So7");
    EXPECT_EQ(roundTrip->tick(), Fraction(1, 4));
    EXPECT_EQ(roundTrip->staffIdx(), 1u);
    EXPECT_EQ(roundTrip->placement(), PlacementV::BELOW);
    EXPECT_FALSE(roundTrip->visible());
    EXPECT_EQ(roundTrip->color(), Color::fromString("#112233"));
    EXPECT_EQ(roundTrip->family(), u"Edwin");
    EXPECT_DOUBLE_EQ(roundTrip->size(), 13.0);
    EXPECT_TRUE(roundTrip->fontStyle() & FontStyle::Italic);
    EXPECT_TRUE(roundTrip->fontStyle() & FontStyle::Bold);
    EXPECT_NEAR(roundTrip->offset().x() / roundTrip->spatium(), 2.0, 0.01);
    EXPECT_NEAR(roundTrip->offset().y() / roundTrip->spatium(), 1.0, 0.01);

    const String nativePath = u"jims-export-scratch/jims-chord-name-offset-staff-format-roundtrip.mscz";
    muse::io::File::remove(nativePath);
    muse::io::File nativeFile(nativePath);
    ASSERT_TRUE(nativeFile.open(muse::io::IODevice::WriteOnly));
    MscWriter::Params params;
    params.device = &nativeFile;
    params.filePath = nativePath;
    params.mode = MscIoMode::Zip;
    MscWriter writer(params);
    ASSERT_TRUE(writer.open());
    MscSaver saver(again->iocContext());
    ASSERT_TRUE(saver.writeMscz(again, writer, false));
    writer.close();
    nativeFile.close();

    MasterScore* native = ScoreRW::readScore(nativePath, true);
    ASSERT_TRUE(native);
    const std::vector<Harmony*> nativeHarmonies = harmoniesInOrder(native);
    ASSERT_EQ(nativeHarmonies.size(), 1u);
    const Harmony* nativeHarmony = nativeHarmonies.front();
    EXPECT_EQ(nativeHarmony->harmonyType(), HarmonyType::MELO);
    EXPECT_EQ(nativeHarmony->harmonyName(), u"La:So7");
    EXPECT_EQ(nativeHarmony->tick(), Fraction(1, 4));
    EXPECT_EQ(nativeHarmony->staffIdx(), 1u);
    EXPECT_EQ(nativeHarmony->placement(), PlacementV::BELOW);
    EXPECT_FALSE(nativeHarmony->visible());
    EXPECT_EQ(nativeHarmony->color(), Color::fromString("#112233"));
    EXPECT_EQ(nativeHarmony->family(), u"Edwin");
    EXPECT_DOUBLE_EQ(nativeHarmony->size(), 13.0);
    EXPECT_TRUE(nativeHarmony->fontStyle() & FontStyle::Italic);
    EXPECT_TRUE(nativeHarmony->fontStyle() & FontStyle::Bold);
    EXPECT_NEAR(nativeHarmony->offset().x() / nativeHarmony->spatium(), 2.0, 0.01);
    EXPECT_NEAR(nativeHarmony->offset().y() / nativeHarmony->spatium(), 1.0, 0.01);
    delete score;
    delete again;
    delete native;
}

TEST_F(MusicXml_Melo_Tests, ChordNameV4PreservesOffsetBetweenNoteOnsets)
{
    MasterScore* score = readMelo("v5/jims-chord-name-unaligned-offset-v4.musicxml");
    ASSERT_TRUE(score);
    const std::vector<Harmony*> imported = harmoniesInOrder(score);
    ASSERT_EQ(imported.size(), 2u);
    EXPECT_EQ(imported[1]->harmonyName(), u"So5");
    EXPECT_EQ(imported[1]->tick(), Fraction(7, 16));

    const String out = exportToScratch(score, "jims-chord-name-unaligned-offset-roundtrip.musicxml");
    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    MasterScore* again = ScoreRW::readScore(out, true, importXml);
    ASSERT_TRUE(again);
    const std::vector<Harmony*> roundTripped = harmoniesInOrder(again);
    ASSERT_EQ(roundTripped.size(), 2u);
    EXPECT_EQ(roundTripped[1]->harmonyName(), u"So5");
    EXPECT_EQ(roundTripped[1]->tick(), Fraction(7, 16));
    delete score;
    delete again;
}

TEST_F(MusicXml_Melo_Tests, ChordNameV4RejectsSupersededTildeMarker)
{
    MasterScore* score = readMelo("v5/jims-chord-name-tilde-invalid.musicxml");
    EXPECT_FALSE(score);
    delete score;
}

TEST_F(MusicXml_Melo_Tests, ChordNameV4RefusesNestedFretDiagramCarrierOnExport)
{
    MasterScore* score = ScoreRW::readScore(u"../../../engraving/tests/chordsymbol_data/add-to-fret.mscz");
    ASSERT_TRUE(score);
    Segment* segment = score->firstMeasure()->findFirstR(SegmentType::ChordRest, Fraction(0, 1));
    ASSERT_TRUE(segment);
    FretDiagram* fretDiagram = toFretDiagram(segment->findAnnotation(ElementType::FRET_DIAGRAM, 0, 0));
    ASSERT_TRUE(fretDiagram);
    fretDiagram->setHarmony(u"!So7/3");
    ASSERT_TRUE(fretDiagram->harmony());
    fretDiagram->harmony()->setHarmonyType(HarmonyType::MELO);

    muse::io::Buffer buffer;
    buffer.open(muse::io::IODevice::WriteOnly);
    EXPECT_FALSE(saveXml(score, &buffer));
    EXPECT_TRUE(buffer.data().empty());
    delete score;
}

TEST_F(MusicXml_Melo_Tests, numberedStatesLandOnTheirStavesAndMidScoreStatesRideChangeCarriers)
{
    MasterScore* multi = readMelo("v5/jims-multi-staff.musicxml");
    ASSERT_TRUE(multi);
    ASSERT_EQ(multi->nstaves(), 2u);
    const StaffType* s1 = staffTypeAtStart(multi, 0);
    const StaffType* s2 = staffTypeAtStart(multi, 1);
    ASSERT_TRUE(s1 && s2);
    EXPECT_TRUE(s1->isMelo());
    EXPECT_TRUE(s2->isMelo());
    EXPECT_NE(s1->meloStateJson(), s2->meloStateJson());
    delete multi;

    MasterScore* mid = readMelo("v5/jims-mid-score-state-change.musicxml");
    ASSERT_TRUE(mid);
    const StaffTypeChange* stc = melo::changeCarrier(measureNo(mid, 2), 0);
    ASSERT_TRUE(stc);
    EXPECT_TRUE(stc->staffType()->meloStateJson().contains(u"\"generator_cents\":696.578"));
    EXPECT_TRUE(staffTypeAtStart(mid)->meloStateJson().contains(u"\"generator_cents\":700.0"));
    delete mid;
}

TEST_F(MusicXml_Melo_Tests, allSixAuthoredPiecesImportTheirConfigurationAndRelativeChanges)
{
    struct Case {
        const char* file;
        int measure;
        const char* marker;
        const char* interval;
    };
    const Case cases[] = {
        { "v5/jims-v3-m5-mode.musicxml", 2, "\"mode_rotation\":5", nullptr },
        { "v5/jims-v3-m5-key-up.musicxml", 2, "\"mode_rotation\":0", "{\"nPer\":0,\"nGen\":1}" },
        { "v5/jims-v3-m5-key-down.musicxml", 2, "\"mode_rotation\":0", "{\"nPer\":-1,\"nGen\":1}" },
        { "v5/jims-v3-m5-scale.musicxml", 2, "\"collection_rotation\":-3", nullptr },
        { "v5/jims-v3-m5-key-mode.musicxml", 2, "\"mode_rotation\":5", "{\"nPer\":-1,\"nGen\":3}" },
        { "v5/jims-v3-m5-syshead.musicxml", 6, "\"mode_rotation\":0", nullptr }
    };
    for (const Case& c : cases) {
        std::unique_ptr<MasterScore> score(readMelo(c.file));
        ASSERT_TRUE(score) << c.file;
        const StaffTypeChange* carrier=melo::changeCarrier(measureNo(score.get(), c.measure), 0);
        ASSERT_TRUE(carrier) << c.file;
        EXPECT_TRUE(carrier->staffType()->meloStateJson().contains(String::fromUtf8(c.marker))) << c.file;
        EXPECT_FALSE(score->metaTag(melo::REFERENCE_TIMELINE_TAG).contains(u"key_number"));
        if (c.interval) {
            melo::RelativeKeyEditor editor;
            String error;
            ASSERT_TRUE(melo::prepareRelativeKeyEditor(score.get(), 0, measureNo(score.get(), c.measure)->tick(), nullptr, editor,
                                                       error)) << error.toStdString();
            EXPECT_EQ(editor.interval, String::fromUtf8(c.interval));
        }
        for (const Note* note:notesInOrder(score.get())) {
            EXPECT_TRUE(note->hasMeloPitch());
        }
    }
}

TEST_F(MusicXml_Melo_Tests, authoritativeMeloIdentityNormalizesContradictoryStandardPitchOnImport)
{
    MasterScore* score = readMelo("v5/jims-v3-m5-key-down.musicxml");
    ASSERT_TRUE(score);
    int disagreements = 0;
    for (const Note* note : notesInOrder(score)) {
        ASSERT_TRUE(note->hasMeloPitch());
        const StaffType* state = note->staff()->staffTypeForElement(note);
        ASSERT_TRUE(state && state->isMelo());
        melo::SoundingPitch projected;
        String error;
        ASSERT_TRUE(melo::noteSoundingPitch(state->meloStateJson(), note->meloNPer(), note->meloNGen(), projected, &error))
            << error.toStdString();
        disagreements += note->pitch() != projected.midiKey;
    }
    EXPECT_EQ(disagreements, 0) << "melo:pitch is authoritative; adjacent standard pitch must be normalized";
    delete score;
}

// ---------------------------------------------------------------------------
// Interchange hardening — native JiMS MusicXML EXPORT (converged FINAL 96%,
// 2026-08-17). The Kernel writes every melo:staff-state / melo:change element
// in full (bridge ops from jims PR 214); the fork places them verbatim, adds
// melo:pitch from each JiMS note's two stored integers, declares the V3
// V4 namespace when JiMS content is present, and fails closed.
// ---------------------------------------------------------------------------
namespace {
struct MeloSnapshot {
    std::vector<String> baseStates;                             // Kernel-canonical XML per staff
    std::vector<std::pair<int, String> > carriers;              // (tick, Kernel-canonical XML) per staff, in order
    std::vector<std::pair<int, int> > identities;               // MeloPresto notes in document order (all tracks)
};

/// The Kernel's own canonical serialization of a state — semantic equality
/// through the Kernel, never a byte comparison of two JSON spellings.
String canonicalState(const String& stateJson)
{
    String xml;
    String err;
    String configuration;
    if (melo::staffConfiguration(stateJson, configuration, err)) {
        EXPECT_TRUE(melo::musicxmlConfigurationV5Xml(configuration, 0, false, xml, err)) << err.toStdString();
        return xml;
    }
    ADD_FAILURE() << "Expected canonical state: " << err.toStdString();
    return xml;
}

MeloSnapshot snapshotOf(Score* score)
{
    MeloSnapshot snap;
    for (staff_idx_t s = 0; s < score->nstaves(); ++s) {
        const Staff* staff = score->staff(s);
        const StaffType* base = staff->staffType(Fraction(0, 1));
        snap.baseStates.push_back(base && base->isMelo() ? canonicalState(base->meloStateJson()) : String());
        for (const Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
            const StaffTypeChange* c = melo::changeCarrier(m, s);
            if (c && c->staffType() && c->staffType()->isMelo()) {
                snap.carriers.emplace_back(m->tick().ticks(), canonicalState(staff->staffType(m->tick())->meloStateJson()));
            }
        }
    }
    for (const Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (track_idx_t t = 0; t < score->ntracks(); ++t) {
            const EngravingItem* el = seg->element(t);
            if (el && el->isChord()) {
                for (const Chord* g : toChord(el)->graceNotes()) {
                    for (const Note* n : g->notes()) {
                        if (n->hasMeloPitch()) {
                            snap.identities.emplace_back(n->meloNPer(), n->meloNGen());
                        }
                    }
                }
                for (const Note* n : toChord(el)->notes()) {
                    if (n->hasMeloPitch()) {
                        snap.identities.emplace_back(n->meloNPer(), n->meloNGen());
                    }
                }
            }
        }
    }
    return snap;
}

String exportToScratch(MasterScore* score, const char* name)
{
    // The stock export tests do the same before saving (musicxml_tests.cpp).
    score->connectTies();
    score->masterScore()->rebuildMidiMapping();
    score->doLayout();
    const String dir(u"jims-export-scratch");
    muse::io::Dir::mkpath(dir);
    const String path = dir + u"/" + String::fromUtf8(name).replace(u"/", u"-");
    muse::io::File::remove(path);
    EXPECT_TRUE(saveXml(score, path)) << name;
    return path;
}

String readAll(const String& path)
{
    muse::io::File f(path);
    EXPECT_TRUE(f.open(muse::io::IODevice::ReadOnly)) << path.toStdString();
    return String::fromUtf8(f.readAll());
}
}

TEST_F(MusicXml_Melo_Tests, CanonicalV5RoundTripPreservesSpellingRelativeEventsAndAllThreeTunings)
{
    for (double generator : { 700.0, 4800.0 / 7.0, 720.0 }) {
        SCOPED_TRACE(generator);
        std::unique_ptr<MasterScore> score(readMelo("v5/jims-12tet-diatonic.musicxml"));
        ASSERT_TRUE(score);
        // Re-author this known test's musical intent explicitly; this is not a legacy import conversion.
        const String root = u"{\"schema\":\"jimstaff-reference-v1\",\"initial\":{\"step\":\"D\",\"alter\":-1,\"octave\":4},\"events\":[]}";
        score->setMetaTag(melo::REFERENCE_TIMELINE_TAG, root);
        for (Staff* staff : score->staves()) {
            StaffType* type = staff->staffType(Fraction(0, 1));
            auto configuration
                = QJsonDocument::fromJson(type->meloStateJson().toQString().toUtf8()).object().value("configuration").toObject();
            configuration["schema"] = "jimstaff-v3";
            configuration["tonic_ambit"] = "tonic-bounded";
            type->setMeloStateJson(String::fromUtf8(QJsonDocument(configuration).toJson(QJsonDocument::Compact).constData()));
        }
        String error;
        ASSERT_TRUE(melo::rebuildCanonicalReferenceContexts(score.get(), error)) << error.toStdString();
        melo::TuningController tuning(score.get(), 0);
        ASSERT_TRUE(tuning.beginPreview());
        ASSERT_TRUE(tuning.commit(generator));
        const Fraction tick(1, 2);
        const String interval = generator == 4800.0 / 7.0 ? u"{\"nPer\":-4,\"nGen\":7}" : u"{\"nPer\":-1,\"nGen\":3}";
        ASSERT_TRUE(melo::changeRelativeKey(score.get(), 0, tick, interval, score->metaTag(melo::REFERENCE_TIMELINE_TAG),
                                            error)) << error.toStdString();
        QTemporaryDir directory;
        ASSERT_TRUE(directory.isValid());
        const String output = String::fromQString(directory.path() + "/canonical.musicxml");
        score->connectTies();
        score->rebuildMidiMapping();
        score->doLayout();
        ASSERT_TRUE(saveXml(score.get(), output));
        const String xml = readAll(output);
        const QString artifactDirectory = qEnvironmentVariable("MELO_REFERENCE_ARTIFACT_DIR");
        if (!artifactDirectory.isEmpty()) {
            const int divisions = generator == 700.0 ? 12 : generator == 720.0 ? 5 : 7;
            QFile artifact(artifactDirectory + QString("/canonical-%1-tet.musicxml").arg(divisions));
            ASSERT_TRUE(artifact.open(QIODevice::WriteOnly));
            const auto bytes = xml.toQString().toUtf8();
            ASSERT_EQ(artifact.write(bytes), bytes.size());
        }
        EXPECT_TRUE(xml.contains(u"urn:melopresto:musicxml:5"));
        EXPECT_EQ(xml.count(u"<melo:reference-timeline>"), 1);
        EXPECT_FALSE(xml.contains(u"<melo:reference>"));
        EXPECT_FALSE(xml.contains(u"key-number"));
        EXPECT_FALSE(xml.contains(u"miscellaneous-field name=\"meloReferenceTimelineV1\""));
        EXPECT_FALSE(xml.contains(u"<melo:change>"));
        const auto importXml = [](MasterScore* target, const muse::io::path_t& path) {
            return importMusicXml(target, path.toQString(), false);
        };
        std::unique_ptr<MasterScore> reopened(ScoreRW::readScore(output, true, importXml));
        ASSERT_TRUE(reopened);
        EXPECT_EQ(reopened->metaTag(melo::REFERENCE_TIMELINE_TAG), score->metaTag(melo::REFERENCE_TIMELINE_TAG));
        EXPECT_EQ(snapshotOf(reopened.get()).identities, snapshotOf(score.get()).identities);
        melo::RelativeKeyEditor editor;
        ASSERT_TRUE(melo::prepareRelativeKeyEditor(reopened.get(), 0, tick, nullptr, editor, error)) << error.toStdString();
        EXPECT_EQ(editor.interval, interval);
        double actualGenerator = 0, period = 0;
        ASSERT_TRUE(melo::staffMetrics(editor.destinationState, actualGenerator, period));
        EXPECT_DOUBLE_EQ(actualGenerator, generator);
        const int rootStart = xml.indexOf(u"<melo:reference-timeline>");
        const int rootEnd = xml.indexOf(u"</melo:reference-timeline>") + String(u"</melo:reference-timeline>").size();
        ASSERT_GE(rootStart, 0);
        ASSERT_GT(rootEnd, rootStart);
        const String rootXml = xml.mid(rootStart, rootEnd - rootStart);
        for (const auto& invalid : {
            xml.left(rootStart) + xml.mid(rootEnd),
            xml.left(rootStart) + rootXml + xml.mid(rootStart),
            String(xml).replace(u"step=\"D\" alter=\"-1\" octave=\"4\"", u"key-number=\"61\""),
            String(xml).replace(u"</melo:staff-state>", u"<melo:reference><melo:none/></melo:reference></melo:staff-state>"),
            String(xml).replace(u"<melo:reference-timeline>", u"<melo:reference-timeline xmlns:melo=\"urn:melopresto:musicxml:4\">"),
            String(xml).replace(u"<melo:staff-state", u"<melo:staff-state xmlns:melo=\"urn:melopresto:musicxml:4\""),
            String(xml).replace(u"<attributes>", u"<attributes xmlns:melo=\"urn:melopresto:musicxml:4\">"),
            String(xml).replace(u"<part id=", u"<part xmlns:melo=\"urn:unrelated\" id=")
        }) {
            QFile malformed(directory.path() + "/invalid.musicxml");
            ASSERT_TRUE(malformed.open(QIODevice::WriteOnly));
            const auto bytes = invalid.toQString().toUtf8();
            ASSERT_EQ(malformed.write(bytes), bytes.size());
            malformed.close();
            std::unique_ptr<MasterScore> refused(ScoreRW::readScore(String::fromQString(malformed.fileName()), true, importXml));
            EXPECT_FALSE(refused);
            ASSERT_TRUE(malformed.open(QIODevice::ReadOnly));
            EXPECT_EQ(malformed.readAll(), bytes);
            EXPECT_EQ(readAll(output), xml);
        }
    }
}

TEST_F(MusicXml_Melo_Tests, exportWritesV5AndRoundTripsThroughTheNativeImporter)
{
    const char* corpus[] = {
        "v5/jims-v3-m5-mode.musicxml", "v5/jims-v3-m5-key-up.musicxml", "v5/jims-v3-m5-key-down.musicxml",
        "v5/jims-v3-m5-scale.musicxml", "v5/jims-v3-m5-key-mode.musicxml", "v5/jims-v3-m5-syshead.musicxml",
        "v5/jims-mid-score-state-change.musicxml",
        "v5/jims-multi-staff.musicxml", "v5/jims-12tet-diatonic.musicxml", "v5/jims-mode-change-v2.musicxml",
    };
    for (const char* file : corpus) {
        MasterScore* original = readMelo(file);
        ASSERT_TRUE(original) << file;
        original->doLayout();
        const MeloSnapshot before = snapshotOf(original);
        ASSERT_FALSE(before.identities.empty()) << file;
        const String out = exportToScratch(original, (String(u"export-") + String::fromUtf8(file)).toStdString().c_str());
        const String xml = readAll(out);
        EXPECT_TRUE(xml.contains(u"xmlns:melo=\"urn:melopresto:musicxml:5\"")) << file;
        EXPECT_TRUE(xml.contains(u"<melo:staff-state")) << file;
        EXPECT_TRUE(xml.contains(u"<melo:pitch ")) << file;
        // Round trip through the accepted native importer.
        auto importXml = [](MasterScore* score, const muse::io::path_t& path) -> engraving::Err {
            return importMusicXml(score, path.toQString(), false);
        };
        MasterScore* again = ScoreRW::readScore(out, true, importXml);
        ASSERT_TRUE(again) << file;
        again->doLayout();
        const MeloSnapshot after = snapshotOf(again);
        EXPECT_EQ(after.baseStates, before.baseStates) << file;
        EXPECT_EQ(after.carriers, before.carriers) << file;
        EXPECT_EQ(after.identities, before.identities) << file;
        // Re-export is again valid-looking and semantically identical.
        const String out2 = exportToScratch(again, (String(u"reexport-") + String::fromUtf8(file)).toStdString().c_str());
        MasterScore* third = ScoreRW::readScore(out2, true, importXml);
        ASSERT_TRUE(third) << file;
        third->doLayout();
        const MeloSnapshot after2 = snapshotOf(third);
        EXPECT_EQ(after2.baseStates, before.baseStates) << file;
        EXPECT_EQ(after2.carriers, before.carriers) << file;
        EXPECT_EQ(after2.identities, before.identities) << file;
        delete original;
        delete again;
        delete third;
    }
}

TEST_F(MusicXml_Melo_Tests, exportOfANativeMeloScoreCarriesStatesChangesAndIdentities)
{
    // The M6/M7 gate file: base reference 62, bar-2 carrier (reference 53,
    // mode La), bar 3 pasted identities.
    MasterScore* score = ScoreRW::readScore(MELO_DATA_DIR + u"m7-gate.mscz");
    ASSERT_TRUE(score);
    score->doLayout();
    const MeloSnapshot before = snapshotOf(score);
    ASSERT_EQ(before.identities.size(), 12u);
    ASSERT_EQ(before.carriers.size(), 1u);
    const String out = exportToScratch(score, "export-m7-gate.musicxml");
    const String xml = readAll(out);
    EXPECT_TRUE(xml.contains(u"xmlns:melo=\"urn:melopresto:musicxml:5\""));
    EXPECT_EQ(int(xml.count(u"<melo:staff-state>")), 2);
    EXPECT_FALSE(xml.contains(u"<melo:change>"));
    EXPECT_EQ(int(xml.count(u"<melo:relative-key-change ")), 1);
    EXPECT_TRUE(xml.contains(u"n-per=\"-1\" n-gen=\"3\""));
    EXPECT_EQ(int(xml.count(u"<melo:pitch ")), 12);
    // No melo:staff-state shares an <attributes> block with staff-lines (Schematron rule).
    size_t pos = 0;
    while ((pos = xml.indexOf(u"<attributes>", pos)) != muse::nidx) {
        const size_t end = xml.indexOf(u"</attributes>", pos);
        const String block = xml.mid(pos, end - pos);
        EXPECT_FALSE(block.contains(u"<melo:staff-state") && block.contains(u"<staff-lines>")) << block.toStdString();
        pos = end;
    }
    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    MasterScore* again = ScoreRW::readScore(out, true, importXml);
    ASSERT_TRUE(again);
    again->doLayout();
    const MeloSnapshot after = snapshotOf(again);
    EXPECT_EQ(after.baseStates, before.baseStates);
    EXPECT_EQ(after.carriers, before.carriers);
    EXPECT_EQ(after.identities, before.identities);
    delete score;
    delete again;
}

TEST_F(MusicXml_Melo_Tests, multiStaffExportNumbersStatesThroughTheKernel)
{
    MasterScore* score = readMelo("v5/jims-multi-staff.musicxml");
    ASSERT_TRUE(score);
    score->doLayout();
    const String out = exportToScratch(score, "export-multi-staff.musicxml");
    const String xml = readAll(out);
    EXPECT_TRUE(xml.contains(u"<melo:staff-state number=\"1\">")) << xml.toStdString().substr(0, 2000);
    EXPECT_TRUE(xml.contains(u"<melo:staff-state number=\"2\">"));
    delete score;
}

TEST_F(MusicXml_Melo_Tests, stockScoreExportDeclaresNoMeloNamespace)
{
    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    MasterScore* score = ScoreRW::readScore(u"data/testHello.xml", false, importXml);
    ASSERT_TRUE(score);
    score->doLayout();
    const String out = exportToScratch(score, "export-stock-hello.musicxml");
    const String xml = readAll(out);
    EXPECT_FALSE(xml.contains(u"jims"));
    EXPECT_TRUE(xml.contains(u"<score-partwise version=\"4.0\">"));
    delete score;
}

TEST_F(MusicXml_Melo_Tests, exportFailsClosedWhenAMeloNoteLacksItsIdentity)
{
    MasterScore* score = ScoreRW::readScore(MELO_DATA_DIR + u"m7-gate.mscz");
    ASSERT_TRUE(score);
    score->doLayout();
    // Strip one identity (a defective document): export must refuse and
    // write nothing.
    for (Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        EngravingItem* el = seg->element(0);
        if (el && el->isChord()) {
            toChord(el)->notes().front()->setMeloPitch(INT_MIN, INT_MIN);   // Note::MELO_UNSET (private)
            break;
        }
    }
    muse::io::Buffer buf;
    buf.open(muse::io::IODevice::WriteOnly);
    String error;
    EXPECT_FALSE(saveXml(score, &buf, &error));
    EXPECT_TRUE(error.contains(u"no lattice identity"));
    EXPECT_TRUE(buf.data().empty());
    muse::io::Buffer mxl;
    mxl.open(muse::io::IODevice::ReadWrite);
    String mxlError;
    EXPECT_FALSE(saveMxl(score, &mxl, &mxlError));
    EXPECT_EQ(mxlError, error);
    EXPECT_TRUE(mxl.data().empty());
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    for (const auto& suffix : { "musicxml", "mxl" }) {
        const QString path = directory.filePath(QString("score.%1").arg(suffix));
        auto exportFile = [&]() {
            return std::string(suffix) == "mxl" ? saveMxl(score, String(path)) : saveXml(score, String(path));
        };
        EXPECT_FALSE(exportFile());
        EXPECT_FALSE(QFile::exists(path));
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("previous valid export");
        file.close();
        EXPECT_FALSE(exportFile());
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        EXPECT_EQ(file.readAll(), "previous valid export");
    }
    delete score;
}

TEST_F(MusicXml_Melo_Tests, trustedRawFragmentWriterInsertsVerbatimAndKeepsBalance)
{
    muse::io::Buffer buf;
    buf.open(muse::io::IODevice::WriteOnly);
    XmlWriter xml(&buf);
    xml.startDocument();
    xml.startElement("attributes");
    xml.tag("divisions", 1);
    xml.writeTrustedRawFragment(u"<melo:staff-state number=\"2\"><melo:x a=\"&amp;\"/></melo:staff-state>");
    xml.tag("after", 2);
    xml.endElement();
    xml.flush();
    const String out = String::fromUtf8(buf.data());
    EXPECT_TRUE(out.contains(u"<melo:staff-state number=\"2\"><melo:x a=\"&amp;\"/></melo:staff-state>\n")) << out.toStdString();
    EXPECT_LT(out.indexOf(u"<divisions>"), out.indexOf(u"<melo:staff-state"));
    EXPECT_LT(out.indexOf(u"</melo:staff-state>"), out.indexOf(u"<after>"));
    EXPECT_TRUE(out.contains(u"</attributes>"));
}

// JiMStaff Milestone 8 (octave-band elision): the three presentation switches
// (score style jimsElideEmptyOctaves / jimsShowAllOctavesInFirstSystem, staff
// type Auto/On/Off) never reach MusicXML — export is byte-identical with
// elision off and on, and no melo:staff-state carries them.
TEST_F(MusicXml_Melo_Tests, m8ElisionSwitchesNeverChangeMusicXmlExport)
{
    MasterScore* score = ScoreRW::readScore(MELO_DATA_DIR + u"m8-two-hand.mscx");
    ASSERT_TRUE(score);
    score->doLayout();
    const String off = readAll(exportToScratch(score, "export-m8-two-hand-off.musicxml"));
    ASSERT_TRUE(off.contains(u"<melo:staff-state>"));
    EXPECT_FALSE(off.contains(u"elide"));
    EXPECT_FALSE(off.contains(u"Elide"));

    score->style().set(Sid::meloElideEmptyOctaves, true);
    score->style().set(Sid::meloShowAllOctavesInFirstSystem, false);
    score->staff(0)->staffType(Fraction(0, 1))->setMeloElideOctaves(MeloElideOctaves::On);
    score->setLayoutAll();
    score->doLayout();
    // The banded layout is in effect (system 2 has two bands) ...
    System* system2 = nullptr;
    int measureSystems = 0;
    for (System* s : score->systems()) {
        if (s->firstMeasure() && ++measureSystems == 2) {
            system2 = s;
        }
    }
    ASSERT_TRUE(system2);
    EXPECT_EQ(staffTypeAtStart(score)->meloFrameView(score, 0, system2).bands.size(), 2u);
    // ... and the export is byte for byte the same.
    const String on = readAll(exportToScratch(score, "export-m8-two-hand-on.musicxml"));
    EXPECT_EQ(on, off);
    EXPECT_FALSE(on.contains(u"elide"));
    delete score;
}

// ---------------------------------------------------------------------------
// Interchange hardening 2 (owner decisions 2026-08-19): melo:provenance and
// melo:tuning-trajectory are transported — imported, saved in the score file,
// exported back exactly as carried — and multi-part documents follow the
// owner's rule: several JiMS parts allowed, mixed JiMS + stock parts allowed,
// every JiMS part shares one state timeline.
// ---------------------------------------------------------------------------

TEST_F(MusicXml_Melo_Tests, provenanceIsImportedSavedAndExportedVerbatim)
{
    MasterScore* score = readMelo("v5/jims-provenance.musicxml");
    ASSERT_TRUE(score);
    score->doLayout();
    const melo::Provenance prov = score->meloProvenance();   // by value: the score is deleted before the reload check
    ASSERT_EQ(prov.resources.size(), 3u);
    EXPECT_TRUE(prov.strictFallback);
    EXPECT_EQ(prov.resources[0].role, u"source");
    EXPECT_EQ(prov.resources[0].uri, u"https://example.org/scores/original.pdf");
    EXPECT_EQ(prov.resources[0].mediaType, u"application/pdf");
    EXPECT_EQ(prov.resources[0].sha256, u"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(prov.resources[0].text, u"Original engraving");
    EXPECT_EQ(prov.resources[1].role, u"master");
    EXPECT_TRUE(prov.resources[1].sha256.isEmpty());
    EXPECT_TRUE(prov.resources[1].text.isEmpty());
    EXPECT_EQ(prov.resources[2].role, u"arrangement");
    // Export writes it back inside identification, before miscellaneous.
    const String out = exportToScratch(score, "export-provenance.musicxml");
    const String xml = readAll(out);
    EXPECT_TRUE(xml.contains(u"<melo:provenance fallback-profile=\"strict\">"));
    EXPECT_EQ(int(xml.count(u"<melo:resource ")), 3);
    EXPECT_TRUE(xml.contains(
                    u"sha-256=\"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\">Original engraving</melo:resource>"));
    const size_t provPos = xml.indexOf(u"<melo:provenance");
    const size_t identEnd = xml.indexOf(u"</identification>");
    const size_t misc = xml.indexOf(u"<miscellaneous>");
    ASSERT_NE(provPos, muse::nidx);
    EXPECT_LT(provPos, identEnd);
    if (misc != muse::nidx) {
        EXPECT_LT(provPos, misc);
    }
    // Round trip: the reimported carrier is equal.
    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    MasterScore* again = ScoreRW::readScore(out, true, importXml);
    ASSERT_TRUE(again);
    EXPECT_TRUE(again->meloProvenance() == prov);
    delete again;
    // Score-file persistence (.mscz): survives save + reload, then exports the same.
    const String dir(u"jims-export-scratch");
    muse::io::Dir::mkpath(dir);
    const String mscz = dir + u"/provenance-roundtrip.mscz";
    muse::io::File::remove(mscz);
    {
        muse::io::File file(mscz);
        ASSERT_TRUE(file.open(muse::io::IODevice::WriteOnly));
        MscWriter::Params params;
        params.device = &file;
        params.filePath = mscz;
        params.mode = MscIoMode::Zip;
        MscWriter writer(params);
        ASSERT_TRUE(writer.open());
        MscSaver saver(score->iocContext());
        ASSERT_TRUE(saver.writeMscz(score, writer, false));
        writer.close();
        file.close();
    }
    delete score;
    MasterScore* reloaded = ScoreRW::readScore(mscz, true);
    ASSERT_TRUE(reloaded);
    reloaded->doLayout();
    EXPECT_TRUE(reloaded->meloProvenance() == prov);
    const String xml2 = readAll(exportToScratch(reloaded, "export-provenance-after-mscz.musicxml"));
    EXPECT_EQ(int(xml2.count(u"<melo:resource ")), 3);
    EXPECT_TRUE(xml2.contains(u"<melo:provenance fallback-profile=\"strict\">"));
    delete reloaded;
}

TEST_F(MusicXml_Melo_Tests, tuningTrajectoriesAreImportedSavedAndExportedVerbatim)
{
    struct Case {
        const char* file;
        const char* interpolation;
        size_t controls;
    };
    const Case cases[] = { { "v5/jims-trajectory-linear.musicxml", "linear", 0 },
        { "v5/jims-trajectory-cubic.musicxml", "cubic-bezier", 2 } };
    for (const Case& c : cases) {
        MasterScore* score = readMelo(c.file);
        ASSERT_TRUE(score) << c.file;
        score->doLayout();
        const std::vector<melo::TuningTrajectory>& ts = score->staff(0)->meloTuningTrajectories();
        ASSERT_EQ(ts.size(), 1u) << c.file;
        const melo::TuningTrajectory t = ts[0];   // by value: the score is deleted before the reload check
        EXPECT_EQ(t.tick, Fraction(0, 1)) << c.file;
        EXPECT_EQ(t.placement, u"above") << c.file;
        ASSERT_EQ(t.segments.size(), 1u) << c.file;
        const melo::TrajectorySegment& seg = t.segments[0];
        EXPECT_EQ(seg.duration, Fraction(4, 4)) << c.file;   // 16 divisions at divisions=4: one whole note
        EXPECT_EQ(seg.startCents, u"700") << c.file;
        EXPECT_EQ(seg.endCents, u"696") << c.file;
        EXPECT_EQ(seg.interpolation, String::fromAscii(c.interpolation)) << c.file;
        ASSERT_EQ(seg.controls.size(), c.controls) << c.file;
        if (c.controls == 2) {
            EXPECT_EQ(seg.controls[0].time, u"0.25");
            EXPECT_EQ(seg.controls[0].valueCents, u"699");
            EXPECT_EQ(seg.controls[1].time, u"0.75");
            EXPECT_EQ(seg.controls[1].valueCents, u"697");
        }
        // Export: a direction at the trajectory's measure with the carrier and
        // the duration re-expressed in the export's own divisions.
        const String out = exportToScratch(score, (String(u"export-") + String::fromUtf8(c.file)).toStdString().c_str());
        const String xml = readAll(out);
        EXPECT_TRUE(xml.contains(u"<direction placement=\"above\">")) << c.file;
        EXPECT_TRUE(xml.contains(u"<melo:tuning-trajectory>")) << c.file;
        EXPECT_TRUE(xml.contains(u"start-cents=\"700\" end-cents=\"696\" interpolation=\"" + String::fromAscii(c.interpolation) + u"\""))
            << c.file << "\n" << xml.toStdString().substr(0, 3000);
        if (c.controls == 2) {
            EXPECT_TRUE(xml.contains(u"<melo:control time=\"0.25\" value-cents=\"699\"/>")) << c.file;
            EXPECT_TRUE(xml.contains(u"<melo:control time=\"0.75\" value-cents=\"697\"/>")) << c.file;
        }
        // The exported duration-divisions is one whole note in the export's divisions.
        const size_t divPos = xml.indexOf(u"<divisions>");
        ASSERT_NE(divPos, muse::nidx) << c.file;
        const size_t divEnd = xml.indexOf(u"</divisions>", divPos);
        const int divisions = xml.mid(divPos + 11, divEnd - divPos - 11).toInt();
        EXPECT_TRUE(xml.contains(String(u"duration-divisions=\"%1\"").arg(4 * divisions))) << c.file << " divisions=" << divisions;
        // Round trip through the importer: equal carrier.
        auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
            return importMusicXml(s, path.toQString(), false);
        };
        MasterScore* again = ScoreRW::readScore(out, true, importXml);
        ASSERT_TRUE(again) << c.file;
        again->doLayout();
        ASSERT_EQ(again->staff(0)->meloTuningTrajectories().size(), 1u) << c.file;
        EXPECT_TRUE(again->staff(0)->meloTuningTrajectories()[0] == t) << c.file;
        delete again;
        // Score-file persistence (.mscz).
        const String dir(u"jims-export-scratch");
        muse::io::Dir::mkpath(dir);
        const String mscz = dir + u"/" + String::fromUtf8(c.file).replace(u"/", u"-") + u".mscz";
        muse::io::File::remove(mscz);
        {
            muse::io::File file(mscz);
            ASSERT_TRUE(file.open(muse::io::IODevice::WriteOnly));
            MscWriter::Params params;
            params.device = &file;
            params.filePath = mscz;
            params.mode = MscIoMode::Zip;
            MscWriter writer(params);
            ASSERT_TRUE(writer.open());
            MscSaver saver(score->iocContext());
            ASSERT_TRUE(saver.writeMscz(score, writer, false));
            writer.close();
            file.close();
        }
        delete score;
        MasterScore* reloaded = ScoreRW::readScore(mscz, true);
        ASSERT_TRUE(reloaded) << c.file;
        reloaded->doLayout();
        ASSERT_EQ(reloaded->staff(0)->meloTuningTrajectories().size(), 1u) << c.file;
        EXPECT_TRUE(reloaded->staff(0)->meloTuningTrajectories()[0] == t) << c.file;
        delete reloaded;
    }
}

TEST_F(MusicXml_Melo_Tests, malformedCarriersAreFatalImportErrors)
{
    // A trajectory segment without interpolation, and a provenance resource
    // without a role: a MeloPresto document never imports with part of its MeloPresto
    // content silently dropped.
    const String dir(u"jims-export-scratch");
    muse::io::Dir::mkpath(dir);
    struct Bad {
        const char* base;
        const char* find;
        const char* replace;
        const char* name;
    };
    const Bad bads[] = {
        { "v5/jims-trajectory-linear.musicxml", " interpolation=\"linear\"", "", "bad-trajectory.musicxml" },
        { "v5/jims-provenance.musicxml", "role=\"master\" ", "", "bad-provenance.musicxml" },
    };
    for (const Bad& b : bads) {
        String text = readAll(ScoreRW::rootPath() + u"/" + MELO_DATA_DIR + String::fromUtf8(b.base));
        ASSERT_TRUE(text.contains(String::fromUtf8(b.find))) << b.name;
        text.replace(String::fromUtf8(b.find), String::fromUtf8(b.replace));
        const String path = dir + u"/" + String::fromUtf8(b.name);
        muse::io::File f(path);
        ASSERT_TRUE(f.open(muse::io::IODevice::WriteOnly));
        f.write(text.toUtf8());
        f.close();
        auto importXml = [](MasterScore* s, const muse::io::path_t& p) -> engraving::Err {
            return importMusicXml(s, p.toQString(), false);
        };
        MasterScore* score = ScoreRW::readScore(path, true, importXml);
        EXPECT_FALSE(score) << b.name;
        delete score;
    }
}

TEST_F(MusicXml_Melo_Tests, severalMeloPartsSharingOneTimelineImportAndRoundTrip)
{
    MasterScore* score = readMelo("v5/jims-multi-part-shared.musicxml");
    ASSERT_TRUE(score);
    score->doLayout();
    ASSERT_EQ(score->nstaves(), 2u);
    for (staff_idx_t s = 0; s < 2; ++s) {
        EXPECT_TRUE(staffTypeAtStart(score, s)->isMelo()) << s;
        Measure* m2 = measureNo(score, 2);
        ASSERT_TRUE(m2);
        EXPECT_TRUE(melo::changeCarrier(m2, s) != nullptr) << s;   // the La-mode section on both parts
    }
    const MeloSnapshot before = snapshotOf(score);
    EXPECT_EQ(before.identities.size(), 4u);
    EXPECT_EQ(before.carriers.size(), 2u);
    EXPECT_EQ(sharedState(before.baseStates[0]), sharedState(before.baseStates[1]));
    EXPECT_EQ(sharedState(before.carriers[0].second), sharedState(before.carriers[1].second));
    const String out = exportToScratch(score, "export-multi-part-shared.musicxml");
    const String xml = readAll(out);
    EXPECT_EQ(int(xml.count(u"<melo:staff-state>")), 4);   // two parts x (base + bar 2)
    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    MasterScore* again = ScoreRW::readScore(out, true, importXml);
    ASSERT_TRUE(again);
    again->doLayout();
    const MeloSnapshot after = snapshotOf(again);
    EXPECT_EQ(after.baseStates, before.baseStates);
    EXPECT_EQ(after.carriers, before.carriers);
    EXPECT_EQ(after.identities, before.identities);
    delete score;
    delete again;
}

TEST_F(MusicXml_Melo_Tests, unequalStaffCountsShareOneMusicalTimeline)
{
    MasterScore* score = readMelo("v5/melo-unequal-staff-counts.musicxml");
    ASSERT_TRUE(score);
    ASSERT_EQ(score->parts().size(), 2u);
    ASSERT_EQ(score->nstaves(), 3u);
    String error;
    EXPECT_TRUE(melo::validateSharedStateTimeline(score, error)) << error.toStdString();
    const MeloSnapshot before = snapshotOf(score);
    const String out = exportToScratch(score, "unequal-staff-counts.musicxml");
    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    MasterScore* again = ScoreRW::readScore(out, true, importXml);
    ASSERT_TRUE(again);
    const MeloSnapshot after = snapshotOf(again);
    EXPECT_EQ(before.identities, after.identities);
    EXPECT_EQ(before.baseStates, after.baseStates);
    EXPECT_EQ(before.carriers, after.carriers);
    delete again;
    delete score;
}

// Owner ruling 2026-08-22 (M8.9): parts of one document are compared on the
// Kernel's shared projection, which omits the per-staff extent. Four SATB
// voices legitimately differ in frame extent, while tonic-ambit is one
// song-wide value that every transport carrier must share. Before this
// change the whole element was compared and such a document was refused.
TEST_F(MusicXml_Melo_Tests, partsDifferingOnlyInPerStaffFieldsImportAndRoundTrip)
{
    MasterScore* score = readMelo("v5/jims-multi-part-perstaff-differs.musicxml");
    ASSERT_TRUE(score);
    score->doLayout();
    ASSERT_EQ(score->nstaves(), 2u);
    const MeloSnapshot before = snapshotOf(score);
    ASSERT_EQ(before.baseStates.size(), 2u);
    // The two parts really do differ — this is the condition that used to be
    // refused outright, so importing at all is the behaviour under test.
    EXPECT_NE(before.baseStates[0], before.baseStates[1]);

    // The Kernel's shared projection is blind only to extent.
    const String centered
        =
            uR"({"scale":["M2","m2","M2","M2","M2","m2","M2"],"collection_rotation":0,"mode_rotation":0,"generator_cents":700.0,"period_cents":1200.0,"embedding":{"large_steps":5,"small_steps":2},"extent":{"lower":{"nPer":1,"nGen":-2},"upper":{"nPer":2,"nGen":-2}},"tonic_ambit":"tonic-centered","schema":"jimstaff-v3"})";
    const String otherExtent
        =
            uR"({"scale":["M2","m2","M2","M2","M2","m2","M2"],"collection_rotation":0,"mode_rotation":0,"generator_cents":700.0,"period_cents":1200.0,"embedding":{"large_steps":5,"small_steps":2},"extent":{"lower":{"nPer":0,"nGen":-2},"upper":{"nPer":1,"nGen":-2}},"tonic_ambit":"tonic-centered","schema":"jimstaff-v3"})";
    String sharedCentered, sharedOtherExtent, err;
    ASSERT_TRUE(melo::musicxmlConfigurationV5Xml(centered, 0, true, sharedCentered, err)) << err.toStdString();
    ASSERT_TRUE(melo::musicxmlConfigurationV5Xml(otherExtent, 0, true, sharedOtherExtent, err)) << err.toStdString();
    EXPECT_EQ(sharedCentered, sharedOtherExtent) << "extent must not make parts disagree";
    EXPECT_FALSE(sharedCentered.contains(u"melo:extent"));
    EXPECT_TRUE(sharedCentered.contains(u"melo:tonic-ambit"));
    const String otherAmbit = String(centered).replace(u"tonic-centered", u"tonic-bounded");
    String sharedOtherAmbit;
    ASSERT_TRUE(melo::musicxmlConfigurationV5Xml(otherAmbit, 0, true, sharedOtherAmbit, err)) << err.toStdString();
    EXPECT_NE(sharedOtherAmbit, sharedCentered) << "tonic-ambit is song-wide and must be compared";
    // ...while a real musical difference still shows up as one.
    String sharedOtherMode;
    const String otherMode = String(centered).replace(u"\"mode_rotation\":0", u"\"mode_rotation\":5");
    ASSERT_TRUE(melo::musicxmlConfigurationV5Xml(otherMode, 0, true, sharedOtherMode, err)) << err.toStdString();
    EXPECT_NE(sharedOtherMode, sharedCentered);

    const String out = exportToScratch(score, "export-multi-part-perstaff-differs.musicxml");
    const String xml = readAll(out);
    // Both per-staff values survive export verbatim: the data stays, only the
    // comparison narrowed.
    EXPECT_TRUE(xml.contains(u"lower-n-per=\"0\" lower-n-gen=\"0\" upper-n-per=\"0\" upper-n-gen=\"0\""));
    EXPECT_TRUE(xml.contains(u"lower-n-per=\"-1\" lower-n-gen=\"-1\" upper-n-per=\"-1\" upper-n-gen=\"-1\""));
    EXPECT_TRUE(xml.contains(u"<melo:tonic-ambit>"));   // the field is still written per staff

    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    MasterScore* again = ScoreRW::readScore(out, true, importXml);
    ASSERT_TRUE(again);
    again->doLayout();
    const MeloSnapshot after = snapshotOf(again);
    EXPECT_EQ(after.baseStates, before.baseStates);
    EXPECT_EQ(after.identities, before.identities);
    delete score;
    delete again;
}

TEST_F(MusicXml_Melo_Tests, aMeloPartBesideAStockPartImportsAndRoundTrips)
{
    MasterScore* score = readMelo("v5/jims-multi-part-mixed.musicxml");
    ASSERT_TRUE(score);
    score->doLayout();
    ASSERT_EQ(score->nstaves(), 2u);
    EXPECT_TRUE(staffTypeAtStart(score, 0)->isMelo());
    EXPECT_FALSE(staffTypeAtStart(score, 1)->isMelo());
    const MeloSnapshot before = snapshotOf(score);
    EXPECT_EQ(before.identities.size(), 2u);   // only the MeloPresto part carries identities
    const String out = exportToScratch(score, "export-multi-part-mixed.musicxml");
    const String xml = readAll(out);
    EXPECT_EQ(int(xml.count(u"<melo:staff-state>")), 2);
    // The stock part is exported as stock: its notes carry no melo:pitch.
    const size_t p2 = xml.indexOf(u"<part id=\"P2\">");
    ASSERT_NE(p2, muse::nidx);
    EXPECT_EQ(xml.mid(p2).indexOf(u"<melo:pitch"), muse::nidx);
    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    MasterScore* again = ScoreRW::readScore(out, true, importXml);
    ASSERT_TRUE(again);
    again->doLayout();
    EXPECT_TRUE(staffTypeAtStart(again, 0)->isMelo());
    EXPECT_FALSE(staffTypeAtStart(again, 1)->isMelo());
    const MeloSnapshot after = snapshotOf(again);
    EXPECT_EQ(after.baseStates, before.baseStates);
    EXPECT_EQ(after.identities, before.identities);
    delete score;
    delete again;
}

TEST_F(MusicXml_Melo_Tests, meloPartsWithDifferentTimelinesAreRefusedOnImportAndOnExport)
{
    // Import: the divergent fixture is refused outright.
    EXPECT_FALSE(readMelo("jims-multi-part-divergent-invalid.musicxml"));
    // Export: a document whose MeloPresto parts have drifted apart in the editor
    // is refused, and nothing is written.
    MasterScore* score = readMelo("v5/jims-multi-part-shared.musicxml");
    ASSERT_TRUE(score);
    score->doLayout();
    StaffType* st = score->staff(1)->staffType(Fraction(0, 1));
    String json = st->meloStateJson();
    ASSERT_TRUE(json.contains(u"\"generator_cents\":700.0"));
    json.replace(u"\"generator_cents\":700.0", u"\"generator_cents\":696.578");
    st->setMeloStateJson(json);
    score->setLayoutAll();
    score->doLayout();
    muse::io::Buffer buf;
    buf.open(muse::io::IODevice::WriteOnly);
    EXPECT_FALSE(saveXml(score, &buf));
    EXPECT_TRUE(buf.data().empty());
    delete score;
}

// ---------------------------------------------------------------------------
// Milestone 9 — the SATB (JiMStaff) template through the current JiMS namespace
// ---------------------------------------------------------------------------

namespace {
String satbTemplatePath()
{
    // This module's data root is src/importexport/musicxml/tests, so the fork
    // root is four levels up (the engraving suite is three).
    return ScoreRW::rootPath() + u"/../../../../share/templates/02-Choral/12-SATB_(MeloPresto_Staff)/12-SATB_(MeloPresto_Staff).mscx";
}
}

// The four voices carry one shared musical timeline and four DIFFERENT frame
// extents. Under the narrowed comparison that document is
// accepted, and every voice's own frame height survives the round trip.
TEST_F(MusicXml_Melo_Tests, m9SATBTemplateRoundTripsPreservingEachVoicesOwnExtent)
{
    MasterScore* score = ScoreRW::readScore(satbTemplatePath(), true);
    ASSERT_TRUE(score) << "the SATB (MeloPresto Staff) template is not shipped";
    score->doLayout();
    ASSERT_EQ(score->nstaves(), 4u);

    // Empty staves retain each singer's range centre. The current policy
    // expands a half-period window to nearby ratio lines with tempering snap;
    // it does not force every voice to start on Do. These are the four
    // independently expected bounding rows of the shipped 12-tet template.
    auto expectEmptyFrames = [](MasterScore* checked) {
        const double lowerRatios[] = { 4.0 / 3.0, 1.0, 3.0 / 2.0, 15.0 / 8.0 };
        const double upperRatios[] = { 1.0, 3.0 / 2.0, 9.0 / 8.0, 4.0 / 3.0 };
        const int lowerPeriods[] = { 0, 0, -1, -2 };
        const int upperPeriods[] = { 1, 0, 0, -1 };
        for (staff_idx_t idx = 0; idx < checked->nstaves(); ++idx) {
            const StaffType* st = checked->staff(idx)->staffType(Fraction(0, 1));
            const auto& segments = st->meloFrameSegments();
            ASSERT_FALSE(segments.empty());
            double doZero = 0.0;
            ASSERT_TRUE(melo::noteCentsAboveExtentLower(st->meloStateJson(), 1, -2, doZero));
            const double period = st->meloPeriodCents();
            EXPECT_NEAR(segments.front().lowerCents, doZero
                        + period * (std::log2(lowerRatios[idx]) + lowerPeriods[idx]), 1e-9);
            EXPECT_NEAR(segments.back().upperCents, doZero
                        + period * (std::log2(upperRatios[idx]) + upperPeriods[idx]), 1e-9);
            EXPECT_LE(segments.front().lowerCents, -period / 4.0 + 25.0);
            EXPECT_GE(segments.back().upperCents, period / 4.0 - 25.0);
        }
    };
    expectEmptyFrames(score);
    const MeloSnapshot before = snapshotOf(score);
    ASSERT_EQ(before.baseStates.size(), 4u);
    for (const String& s : before.baseStates) {
        EXPECT_FALSE(s.empty()) << "every SATB staff must be a MeloPresto Staff";
    }

    const String out = exportToScratch(score, "export-m9-satb-template.musicxml");
    const String xml = readAll(out);
    EXPECT_EQ(xml.count(u"lower-n-per=\"0\" lower-n-gen=\"1\" upper-n-per=\"0\" upper-n-gen=\"1\""), 1);
    EXPECT_EQ(xml.count(u"lower-n-per=\"-1\" lower-n-gen=\"2\" upper-n-per=\"-1\" upper-n-gen=\"2\""), 1);
    EXPECT_EQ(xml.count(u"lower-n-per=\"-2\" lower-n-gen=\"3\" upper-n-per=\"-2\" upper-n-gen=\"3\""), 1);
    EXPECT_EQ(xml.count(u"lower-n-per=\"-1\" lower-n-gen=\"0\" upper-n-per=\"-1\" upper-n-gen=\"0\""), 1)
        << "each empty SATB voice exports only its exact lattice centre anchor";

    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    MasterScore* again = ScoreRW::readScore(out, true, importXml);
    ASSERT_TRUE(again) << "a four-part document differing only in melo:extent must import";
    again->doLayout();
    ASSERT_EQ(again->nstaves(), 4u);
    const MeloSnapshot after = snapshotOf(again);
    EXPECT_EQ(after.baseStates, before.baseStates);
    EXPECT_EQ(after.identities, before.identities);
    expectEmptyFrames(again);

    // Re-export once more: no drift in anything JiMS owns. The one byte that
    // does move is stock MuseScore's part-group round trip — a re-imported
    // score re-exports <group-barline>yes even though the source omitted it —
    // and the stock 02-Choral/01-SATB template drifts identically, so it is
    // not a JiMS behaviour and not this milestone's to change. Recorded as an
    // observed follow-up in the M9 final report.
    const String out2 = exportToScratch(again, "export-m9-satb-template-2.musicxml");
    const String xml2 = readAll(out2);
    auto meloLinesOf = [](const String& doc) {
        StringList out;
        for (const String& line : doc.split(u'\n')) {
            if (line.contains(u"melo:")) {
                out.push_back(line.trimmed());
            }
        }
        return out;
    };
    ASSERT_FALSE(meloLinesOf(xml).empty());
    EXPECT_EQ(meloLinesOf(xml2), meloLinesOf(xml)) << "the MeloPresto content must not drift across a second round trip";
    EXPECT_EQ(xml2.count(u"lower-n-per=\"0\" lower-n-gen=\"1\" upper-n-per=\"0\" upper-n-gen=\"1\""), 1);
    EXPECT_EQ(xml2.count(u"lower-n-per=\"-1\" lower-n-gen=\"2\" upper-n-per=\"-1\" upper-n-gen=\"2\""), 1);
    EXPECT_EQ(xml2.count(u"lower-n-per=\"-2\" lower-n-gen=\"3\" upper-n-per=\"-2\" upper-n-gen=\"3\""), 1);
    EXPECT_EQ(xml2.count(u"lower-n-per=\"-1\" lower-n-gen=\"0\" upper-n-per=\"-1\" upper-n-gen=\"0\""), 1);

    delete again;
    delete score;
}

// A state change applied through the decision-2a path leaves every MeloPresto part
// on the same musical chronology, which is exactly what the interchange rule
// requires — so the changed score still exports.
TEST_F(MusicXml_Melo_Tests, m9SATBScoreWideChangeKeepsOneSharedTimelineOnExport)
{
    MasterScore* score = ScoreRW::readScore(satbTemplatePath(), true);
    ASSERT_TRUE(score) << "the SATB (MeloPresto Staff) template is not shipped";
    score->doLayout();
    ASSERT_EQ(score->nstaves(), 4u);

    Measure* m2 = measureNo(score, 2);
    ASSERT_TRUE(m2);
    String error;
    ASSERT_TRUE(melo::applyChangeToAllMeloParts(score, m2, { u"mode:1" }, error)) << error.toStdString();
    score->doLayout();

    const String out = exportToScratch(score, "export-m9-satb-mode-change.musicxml");
    const String xml = readAll(out);
    EXPECT_FALSE(xml.empty()) << "a score-wide change must leave the document exportable";
    EXPECT_EQ(xml.count(u"<melo:mode-rotation>5</melo:mode-rotation>"), 4)
        << "every one of the four parts must carry the change";
    // Every empty vocal staff retains its independent singer-range centre.
    EXPECT_EQ(xml.count(u"lower-n-per=\"0\" lower-n-gen=\"1\" upper-n-per=\"0\" upper-n-gen=\"1\""), 2);
    EXPECT_EQ(xml.count(u"lower-n-per=\"-1\" lower-n-gen=\"2\" upper-n-per=\"-1\" upper-n-gen=\"2\""), 2);
    EXPECT_EQ(xml.count(u"lower-n-per=\"-2\" lower-n-gen=\"3\" upper-n-per=\"-2\" upper-n-gen=\"3\""), 2);
    EXPECT_EQ(xml.count(u"lower-n-per=\"-1\" lower-n-gen=\"0\" upper-n-per=\"-1\" upper-n-gen=\"0\""), 2);

    delete score;
}

// The per-staff exclusion is exactly melo:extent; every song-wide divergence,
// including tonic-ambit, is still refused in both
// directions.
TEST_F(MusicXml_Melo_Tests, m9SATBExtentOnlyDivergenceIsAcceptedAndMusicalDivergenceIsStillRefused)
{
    MasterScore* accepted = readMelo("v5/jims-multi-part-perstaff-differs.musicxml");
    ASSERT_TRUE(accepted) << "parts differing only in per-staff fields must import";
    delete accepted;

    EXPECT_FALSE(readMelo("jims-multi-part-divergent-invalid.musicxml"))
        << "a musical-field divergence must still be refused on import";

    MasterScore* score = ScoreRW::readScore(satbTemplatePath(), true);
    ASSERT_TRUE(score) << "the SATB (MeloPresto Staff) template is not shipped";
    score->doLayout();
    ASSERT_EQ(score->nstaves(), 4u);
    // Diverge one voice in a MUSICAL field: export must fail closed.
    StaffType* tenor = score->staff(2)->staffType(Fraction(0, 1));
    String json = tenor->meloStateJson();
    ASSERT_TRUE(json.contains(u"\"generator_cents\":700.0"));
    json.replace(u"\"generator_cents\":700.0", u"\"generator_cents\":696.578");
    tenor->setMeloStateJson(json);
    score->setLayoutAll();
    score->doLayout();
    muse::io::Buffer buf;
    buf.open(muse::io::IODevice::WriteOnly);
    EXPECT_FALSE(saveXml(score, &buf)) << "a tuning divergence across parts must be refused on export";
    EXPECT_TRUE(buf.data().empty());

    delete score;
}

TEST_F(MusicXml_Melo_Tests, MelodyPartDefaultsToSopranoAndDefaultIsOmittedOnExport)
{
    MasterScore* score = ScoreRW::readScore(satbTemplatePath(), true);
    ASSERT_TRUE(score);
    EXPECT_EQ(score->meloMelodyPart(), melo::MelodyPart::Soprano);
    const String out = exportToScratch(score, "export-m10-melody-default.musicxml");
    const String xml = readAll(out);
    EXPECT_FALSE(xml.contains(u"<melo:melody-part>"));
    delete score;

    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    MasterScore* reloaded = ScoreRW::readScore(out, true, importXml);
    ASSERT_TRUE(reloaded);
    EXPECT_EQ(reloaded->meloMelodyPart(), melo::MelodyPart::Soprano);
    delete reloaded;
}

TEST_F(MusicXml_Melo_Tests, MelodyPartTenorOverrideRoundTripsAndInvalidValueIsRefused)
{
    MasterScore* score = ScoreRW::readScore(satbTemplatePath(), true);
    ASSERT_TRUE(score);
    score->setMeloMelodyPart(melo::MelodyPart::Tenor);
    const String out = exportToScratch(score, "export-m10-melody-tenor.musicxml");
    String xml = readAll(out);
    EXPECT_TRUE(xml.contains(u"<melo:melody-part>tenor</melo:melody-part>"));
    delete score;

    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    MasterScore* reloaded = ScoreRW::readScore(out, true, importXml);
    ASSERT_TRUE(reloaded);
    EXPECT_EQ(reloaded->meloMelodyPart(), melo::MelodyPart::Tenor);
    delete reloaded;

    xml.replace(u"<melo:melody-part>tenor</melo:melody-part>",
                u"<melo:melody-part>descant</melo:melody-part>");
    const String invalid(u"jims-export-scratch/m10-melody-invalid.musicxml");
    muse::io::File file(invalid);
    ASSERT_TRUE(file.open(muse::io::IODevice::WriteOnly));
    file.write(xml.toUtf8());
    file.close();
    MasterScore* refused = ScoreRW::readScore(invalid, true, importXml);
    EXPECT_FALSE(refused);
    delete refused;
}

TEST_F(MusicXml_Melo_Tests, GeneratedChordEvidenceSurvivesNativeAndXmlAndDetectsEdits)
{
    MasterScore* score = readMelo("v5/melo-generated-chord-evidence.musicxml");
    ASSERT_TRUE(score);
    Harmony* harmony = harmoniesInOrder(score).front();
    const String proof = harmony->meloEvidence();
    ASSERT_FALSE(proof.empty());
    EXPECT_EQ(harmony->meloEvidenceOrigin(), u"generated");
    EXPECT_TRUE(harmony->meloEvidenceError().empty()) << harmony->meloEvidenceError().toStdString();
    const String dir(u"jims-export-scratch");
    muse::io::Dir::mkpath(dir);
    const String nativePath = dir + u"/generated-chord-evidence.mscx";
    ASSERT_TRUE(ScoreRW::saveScore(score, nativePath));
    MasterScore* native = ScoreRW::readScore(nativePath, true);
    ASSERT_TRUE(native);
    EXPECT_EQ(harmoniesInOrder(native).front()->meloEvidence(), proof);
    EXPECT_TRUE(harmoniesInOrder(native).front()->meloEvidenceError().empty());
    const String output = exportToScratch(native, "generated-chord-evidence.musicxml");
    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    MasterScore* again = ScoreRW::readScore(output, true, importXml);
    ASSERT_TRUE(again);
    EXPECT_EQ(harmoniesInOrder(again).front()->meloEvidence(), proof);
    EXPECT_TRUE(harmoniesInOrder(again).front()->meloEvidenceError().empty());

    rendering::PaintOptions screen;
    rendering::PaintOptions print;
    print.isPrinting = true;
    const auto validColor = harmony->curColor(screen);
    Note* bass = const_cast<Note*>(notesInOrder(score, 0).front());
    score->startCmd(TranslatableString::untranslatable("Change supporting chord note"));
    bass->undoChangeProperty(Pid::MELO_NPER, bass->meloNPer() + 1);
    score->endCmd();
    EXPECT_FALSE(harmony->meloNameError().empty());
    EXPECT_NE(harmony->curColor(screen), validColor);
    EXPECT_EQ(harmony->curColor(print), harmony->curColor(screen));
    muse::io::Buffer refused;
    refused.open(muse::io::IODevice::WriteOnly);
    EXPECT_FALSE(saveXml(score, &refused));
    score->undoRedo(true, nullptr);
    EXPECT_TRUE(harmony->meloEvidenceError().empty());
    score->undoRedo(false, nullptr);
    EXPECT_FALSE(harmony->meloEvidenceError().empty());
    score->undoRedo(true, nullptr);
    EXPECT_TRUE(harmony->meloEvidenceError().empty());

    harmony->setHarmony(u"Re5");
    EXPECT_EQ(harmony->meloEvidenceOrigin(), u"manual");
    EXPECT_EQ(harmony->meloEvidence(), proof);
    EXPECT_TRUE(harmony->meloEvidenceError().empty());
    const String manual = exportToScratch(score, "manual-with-generation-history.musicxml");
    EXPECT_TRUE(readAll(manual).contains(u"origin=\"manual\""));
    MasterScore* manualAgain = ScoreRW::readScore(manual, true, importXml);
    ASSERT_TRUE(manualAgain);
    EXPECT_EQ(harmoniesInOrder(manualAgain).front()->meloEvidenceOrigin(), u"manual");
    harmony->setHarmony(u"Do5");
    EXPECT_EQ(harmony->meloEvidenceOrigin(), u"generated");
    EXPECT_TRUE(harmony->meloEvidenceError().empty());
    delete manualAgain;
    delete again;
    delete native;
    delete score;
}

TEST_F(MusicXml_Melo_Tests, ContinuousTuningChangesNotePlacementAndPlaybackAtTheActualOnset)
{
    std::unique_ptr<MasterScore> score(readMelo("v5/melo-continuous-tuning.musicxml"));
    ASSERT_TRUE(score);
    score->doLayout();
    const auto notes = notesInOrder(score.get(), 0);
    ASSERT_EQ(notes.size(), 2u);
    const Note* note = notes[1];
    String expected, error;
    ASSERT_TRUE(melo::retuneGenerator(score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), 698.0, expected));
    melo::SoundingPitch sounding;
    ASSERT_TRUE(melo::noteSoundingPitch(expected, note->meloNPer(), note->meloNGen(), sounding, &error));
    std::optional<muse::mpe::ExactPitch> actual;
    NominalNoteCtx::nominalPitchLevelOf(note, &actual);
    ASSERT_TRUE(actual.has_value());
    EXPECT_NEAR(actual->frequencyHz, sounding.frequencyHz, 1e-8);
    double cents;
    ASSERT_TRUE(melo::noteCentsAboveExtentLower(expected, note->meloNPer(), note->meloNGen(), cents));
    ASSERT_TRUE(note->meloCentsValid());
    EXPECT_NEAR(note->meloCentsAboveDo(), cents, 1e-8);
    for (Harmony* harmony : harmoniesInOrder(score.get())) {
        EXPECT_TRUE(harmony->meloEvidenceError().empty()) << harmony->meloEvidenceError().toStdString();
    }
    EXPECT_FALSE(readAll(exportToScratch(score.get(), "continuous-tuning.musicxml")).empty());
}

TEST_F(MusicXml_Melo_Tests, StaffCopyKeepsSelectedContinuousTuning)
{
    std::unique_ptr<MasterScore> score(readMelo("v5/melo-continuous-tuning.musicxml"));
    ASSERT_TRUE(score);
    const Staff* source = score->staff(0);
    ASSERT_FALSE(source->meloTuningTrajectories().empty());
    std::unique_ptr<Staff> copy(Factory::createStaff(source->part()));
    copy->init(source);
    EXPECT_EQ(copy->meloTuningTrajectories().size(), source->meloTuningTrajectories().size());
    EXPECT_EQ(copy->meloStateAt(Fraction(1, 4)), source->meloStateAt(Fraction(1, 4)));
}

TEST_F(MusicXml_Melo_Tests, HeldNoteDefinesExtentAfterReferenceChange)
{
    std::unique_ptr<MasterScore> score(readMelo("v5/melo-held-note-reference.musicxml"));
    ASSERT_TRUE(score);
    const Note* note = notesInOrder(score.get(), 0).front();
    const String state = score->staff(0)->meloStateAt(Fraction(1, 4));
    melo::SoundingPitch reframed;
    ASSERT_TRUE(melo::reframeNote(score->staff(0)->meloStateAt(note->tick()), state,
                                  note->meloNPer(), note->meloNGen(), reframed));
    String expected;
    ASSERT_TRUE(melo::fitExtent(state, String(u"{\"notes\":[{\"nPer\":%1,\"nGen\":%2}]}")
                                .arg(reframed.nPer).arg(reframed.nGen), expected));
    EXPECT_EQ(state, expected) << "A held note remains part of the next section's extent";
}

TEST_F(MusicXml_Melo_Tests, HeldNoteEvidenceSurvivesReferenceChange)
{
    std::unique_ptr<MasterScore> score(readMelo("v5/melo-held-note-reference.musicxml"));
    ASSERT_TRUE(score);
    ASSERT_EQ(notesInOrder(score.get(), 0).size(), 1u);
    auto harmonies = harmoniesInOrder(score.get());
    ASSERT_EQ(harmonies.size(), 2u);
    for (Harmony* harmony : harmonies) {
        EXPECT_TRUE(harmony->meloEvidenceError().empty()) << harmony->meloEvidenceError().toStdString();
    }
    const String output = exportToScratch(score.get(), "held-note-reference.musicxml");
    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    std::unique_ptr<MasterScore> again(ScoreRW::readScore(output, true, importXml));
    ASSERT_TRUE(again);
    EXPECT_EQ(notesInOrder(again.get(), 0).size(), 1u);
    for (Harmony* harmony : harmoniesInOrder(again.get())) {
        EXPECT_TRUE(harmony->meloEvidenceError().empty()) << harmony->meloEvidenceError().toStdString();
    }
    Note* held = const_cast<Note*>(notesInOrder(score.get(), 0).front());
    held->setMeloPitch(held->meloNPer() + 1, held->meloNGen());
    for (Harmony* harmony : harmonies) {
        EXPECT_FALSE(harmony->meloEvidenceError().empty());
    }
}

// The inherited buffered provider deliberately ignores save/restore. Real
// painting restores glyph scales; the source oracle must record the same state.
class SourceNotationPaintProvider : public BufferedPaintProvider
{
public:
    std::vector<RectF> clipRects;
    void setClipRect(const RectF& rect) override { clipRects.push_back(rect); }

    void save() override
    {
        m_saved.push_back(drawData()->states.rbegin()->second);
    }

    void restore() override
    {
        ASSERT_FALSE(m_saved.empty());
        const auto state = m_saved.back();
        m_saved.pop_back();
        setPen(state.pen);
        setBrush(state.brush);
        setFont(state.font);
        setTransform(state.transform);
        setAntialiasing(state.isAntialiasing);
        setCompositionMode(state.compositionMode);
    }

private:
    std::vector<DrawData::State> m_saved;
};

static void verifySourceIndicator(MasterScore* score, int staffIndex, const Fraction& at, const QJsonObject& expected)
{
    const Staff* staff = score->staff(staffIndex);
    Measure* measure = score->tick2measure(at);
    if (expected["kinds"].toArray().isEmpty()) {
        const StaffTypeChange* carrier = melo::changeCarrierAt(measure, staffIndex, at);
        if (carrier) {
            melo::ChangeIndicator actual;
            EXPECT_FALSE(melo::changeIndicatorIntoStaffType(score, staffIndex, staff->staffType(at), actual));
        }
        return;
    }
    ASSERT_TRUE(measure);
    const StaffTypeChange* carrier = melo::changeCarrierAt(measure, staffIndex, at);
    ASSERT_TRUE(carrier) << "selected change has no exact-time carrier";
    const StaffType* incoming = staff->staffType(at);
    const StaffType* displayed = nullptr;
    melo::ChangeIndicator model;
    double x0 = 0.0;
    if (!carrier->rtick().isZero()) {
        ASSERT_TRUE(melo::midBarChangeIndicator(carrier, model));
        displayed = staff->staffType(measure->tick());
        const Segment* anchor = measure->findSegmentR(Segment::CHORD_REST_OR_TIME_TICK_TYPE, carrier->rtick());
        ASSERT_TRUE(anchor);
        const StaffLines* lines = measure->staffLines(staffIndex);
        ASSERT_TRUE(lines);
        x0 = anchor->x() - melo::changeTerrainGeometry(incoming, lines->spatium(), score->style().defaultSpatium(),
                                                       model).changeTerrainWidth
             - score->style().styleMM(Sid::barNoteDistance);
    } else if (measure->system()->firstMeasure() != measure) {
        ASSERT_TRUE(melo::midSystemChangeIndicator(measure, staffIndex, model));
        displayed = incoming;
        x0 = measure->staffLines(staffIndex)->pos().x();
    } else {
        measure = measure->prevMeasure();
        ASSERT_TRUE(measure);
        ASSERT_TRUE(melo::courtesyChangeIndicator(measure, staffIndex, model, &displayed));
        const Segment* end = measure->findSegmentR(SegmentType::EndBarLine, measure->ticks());
        ASSERT_TRUE(end);
        const StaffLines* lines = measure->staffLines(staffIndex);
        ASSERT_TRUE(lines);
        x0 = end->x() - melo::changeTerrainGeometry(incoming, lines->spatium(), score->style().defaultSpatium(), model).changeTerrainWidth;
    }
    QStringList actualKinds, expectedKinds;
    for (const auto& kind : model.kinds) {
        actualKinds.push_back(kind.toQString());
    }
    for (const auto& kind : expected["kinds"].toArray()) {
        expectedKinds.push_back(kind.toString());
    }
    EXPECT_EQ(actualKinds, expectedKinds);
    auto point = [](const melo::ChangePoint& actual, const QJsonObject& wanted) {
        EXPECT_EQ(actual.nGen, wanted["nGen"].toInt());
        EXPECT_EQ(actual.label.toQString(), wanted["label"].toString());
        EXPECT_NEAR(actual.ordinate, wanted["ordinate"].toDouble(), 1e-9);
        EXPECT_EQ(actual.periodOffset, wanted["period_offset"].toInt());
    };
    const QJsonObject terrain = expected["terrain"].toObject();
    const auto tonics = terrain["tonic_indicators"].toArray();
    ASSERT_EQ(model.tonicIndicators.size(), size_t(tonics.size()));
    for (size_t i = 0; i < model.tonicIndicators.size(); ++i) {
        point(model.tonicIndicators[i], tonics[int(i)].toObject());
    }
    const auto arrows = terrain["arrows"].toArray();
    ASSERT_EQ(model.arrows.size(), size_t(arrows.size()));
    for (size_t i = 0; i < model.arrows.size(); ++i) {
        const auto wanted = arrows[int(i)].toObject();
        EXPECT_EQ(model.arrows[i].kind.toQString(), wanted["kind"].toString());
        EXPECT_EQ(model.arrows[i].up, wanted["direction"].toString() == "up");
        EXPECT_EQ(model.arrows[i].trumps.toQString(), wanted["trumps"].toString());
        point(model.arrows[i].from, wanted["from"].toObject());
        point(model.arrows[i].to, wanted["to"].toObject());
    }
    const auto stacks = terrain["dot_stacks"].toArray();
    ASSERT_EQ(model.dotStacks.size(), size_t(stacks.size()));
    for (size_t i = 0; i < model.dotStacks.size(); ++i) {
        const auto wanted = stacks[int(i)].toObject();
        EXPECT_NEAR(model.dotStacks[i].ordinate, wanted["ordinate"].toDouble(), 1e-9);
        EXPECT_EQ(model.dotStacks[i].periodOffset, wanted["period_offset"].toInt());
        const auto members = wanted["members"].toArray();
        ASSERT_EQ(model.dotStacks[i].members.size(), size_t(members.size()));
        for (size_t j = 0; j < model.dotStacks[i].members.size(); ++j) {
            point(model.dotStacks[i].members[j], members[int(j)].toObject());
        }
    }
    ASSERT_TRUE(displayed);
    const StaffLines* lines = measure->staffLines(staffIndex);
    ASSERT_TRUE(lines);
    const auto& view = displayed->meloFrameView(score, staffIndex, measure->system());
    ASSERT_FALSE(view.empty());
    melo::PeriodicOrigins origins;
    ASSERT_TRUE(melo::periodicOrigins(displayed->meloStateJson(), origins));
    const double period = displayed->meloPeriodCents();
    const double base = melo::changeAnchorPeriodCents(view, model, period, origins.doCentsAboveExtentLower,
                                                      melo::systemNoteCents(measure->system(), staffIndex, displayed));
    const auto geometry = melo::changeTerrainGeometry(incoming, lines->spatium(), score->style().defaultSpatium(), model);
    const double dotX = x0 + 0.3 * lines->spatium() + geometry.changeLabelBand + geometry.changeLeftArrowLane + geometry.indicatorW;
    std::vector<double> expectedYs;
    const bool scale = expectedKinds.contains("scale");
    for (const auto& value : tonics) {
        const auto p = value.toObject();
        std::vector<double> cents;
        if (scale) {
            for (const auto& band : view.bands) {
                for (const auto& segment : band.segments) {
                    const double first = origins.doCentsAboveExtentLower
                                         + std::floor((segment.lowerCents - origins.doCentsAboveExtentLower) / period + 1e-6) * period;
                    for (double origin = first; origin <= segment.upperCents + 1e-6; origin += period) {
                        const double valueCents = origin + p["ordinate"].toDouble() * period;
                        if (valueCents >= segment.lowerCents - 1e-6 && valueCents <= segment.upperCents + 1e-6) {
                            cents.push_back(valueCents);
                        }
                    }
                }
            }
        } else {
            cents.push_back(base + (p["period_offset"].toInt() + p["ordinate"].toDouble()) * period);
        }
        for (double c : cents) {
            expectedYs.push_back(lines->pos().y() + displayed->meloYFromCents(c, view) * lines->spatium());
        }
    }
    auto provider = std::make_shared<SourceNotationPaintProvider>();
    Painter painter(provider, "source-indicator-oracle");
    painter.setWindow(RectF(0, 0, 4000, 4000));
    painter.setViewport(RectF(0, 0, 4000, 4000));
    lines->renderer()->drawItem(lines, &painter, PaintOptions());
    painter.endDraw();
    std::vector<double> actualYs;
    const double diameter = 2 * (1.15 + 0.025) * displayed->lineDistance().val() * lines->spatium();
    std::function<void(const DrawData::Item&)> visit = [&](const DrawData::Item& item) {
        for (const auto& data : item.datas) {
            for (const auto& path : data.paths) {
                const auto box = path.path.boundingRect();
                if (std::abs(box.center().x() - dotX) < 1e-6 && std::abs(box.width() - diameter) < 1e-6
                    && std::abs(box.height() - diameter) < 1e-6) {
                    actualYs.push_back(box.center().y());
                }
            }
        }
        for (const auto& child : item.chilren) {
            visit(child);
        }
    };
    visit(provider->drawData()->item);
    std::sort(actualYs.begin(), actualYs.end());
    std::sort(expectedYs.begin(), expectedYs.end());
    ASSERT_EQ(actualYs.size(), expectedYs.size()) << "Every selected tonic indicator must draw at its actual change location";
    for (size_t i = 0; i < actualYs.size(); ++i) {
        EXPECT_NEAR(actualYs[i], expectedYs[i], 1e-6);
    }

    // Compare the actual paint commands, independently of the already checked
    // semantic model. In particular, stored arrow metadata cannot stand in for
    // a visible shaft or for the right head at the destination row.
    melo::ConnectorGlyph connector;
    ASSERT_TRUE(melo::connectorGlyph(connector));
    const double dist = displayed->lineDistance().val() * lines->spatium();
    const double penWidth = connector.penCents / StaffType::MELO_CENTS_PER_LINE_DISTANCE * dist;
    const double headHeight = connector.headHeightCents / StaffType::MELO_CENTS_PER_LINE_DISTANCE * dist;
    const auto font = score->engravingFont();
    ASSERT_TRUE(font);
    const String upSymbol = String::fromUcs4(font->symCode(SymId::arrowheadBlackUp));
    const String downSymbol = String::fromUcs4(font->symCode(SymId::arrowheadBlackDown));
    const auto drawing = std::make_shared<DrawData>(*provider->drawData());
    std::vector<LineF> shafts;
    std::vector<std::pair<bool, RectF> > heads;
    const QString damage = expected["paint_damage"].toString();
    std::function<void(DrawData::Item&)> arrowPaint = [&](DrawData::Item& item) {
        for (auto& data : item.datas) {
            const auto& state = drawing->states.at(data.state);
            auto isShaft = [&](const DrawPolygon& polygon) {
                return polygon.mode == PolygonMode::Polyline && polygon.polygon.size() == 2
                       && std::abs(polygon.polygon[0].x() - polygon.polygon[1].x()) < 1e-6
                       && state.pen.capStyle() == PenCapStyle::RoundCap
                       && std::abs(state.pen.widthF() - penWidth) < 1e-6;
            };
            // Test-only fault injection removes real paint commands while
            // leaving every source fact and semantic indicator unchanged.
            if (damage == "shafts") {
                data.polygons.erase(std::remove_if(data.polygons.begin(), data.polygons.end(), isShaft), data.polygons.end());
            }
            if (damage == "heads") {
                data.texts.erase(std::remove_if(data.texts.begin(), data.texts.end(), [&](const DrawText& text) {
                    return text.text == upSymbol || text.text == downSymbol;
                }), data.texts.end());
            }
            for (const auto& polygon : data.polygons) {
                if (!isShaft(polygon)) {
                    continue;
                }
                const LineF line = state.transform.map(LineF(polygon.polygon[0], polygon.polygon[1]));
                if (line.x1() >= x0 && line.x1() <= x0 + geometry.changeTerrainWidth) {
                    shafts.push_back(line);
                }
            }
            for (const auto& text : data.texts) {
                if (text.text != upSymbol && text.text != downSymbol) {
                    continue;
                }
                const bool up = text.text == upSymbol;
                const RectF glyph = font->bbox(up ? SymId::arrowheadBlackUp : SymId::arrowheadBlackDown, 1.0);
                const RectF box = state.transform.map(glyph.translated(text.rect.topLeft()));
                if (box.center().x() >= x0 && box.center().x() <= x0 + geometry.changeTerrainWidth) {
                    heads.emplace_back(up, box);
                }
            }
        }
        for (auto& child : item.chilren) {
            arrowPaint(child);
        }
    };
    arrowPaint(drawing->item);
    ASSERT_EQ(shafts.size(), size_t(arrows.size())) << "Every selected arrow must paint one shaft in its terrain";
    ASSERT_EQ(heads.size(), size_t(arrows.size())) << "Every selected arrow must paint one head in its terrain";
    const size_t modeCount = std::count_if(arrows.begin(), arrows.end(), [](const QJsonValue& value) {
        return value.toObject()["kind"].toString() == "mode";
    });
    size_t modeIndex = 0;
    size_t keyIndex = 0;
    auto yFor = [&](const QJsonObject& point) {
        const double cents = base + (point["period_offset"].toInt() + point["ordinate"].toDouble()) * period;
        return lines->pos().y() + displayed->meloYFromCents(cents, view) * lines->spatium();
    };
    for (const auto& value : arrows) {
        const auto arrow = value.toObject();
        const bool mode = arrow["kind"].toString() == "mode";
        const bool up = arrow["direction"].toString() == "up";
        const double lane = mode ? geometry.changeLeftArrowLane : geometry.changeArrowLane;
        const size_t count = mode ? modeCount : size_t(arrows.size()) - modeCount;
        const size_t index = mode ? modeIndex++ : keyIndex++;
        const double laneLeft = mode ? dotX - geometry.indicatorW - geometry.changeLeftArrowLane
                                : dotX + geometry.indicatorW + geometry.changeRightLabelBand;
        const double x = laneLeft + (index + 0.5) * lane / count;
        const double fromY = yFor(arrow["from"].toObject());
        const double toY = yFor(arrow["to"].toObject());
        auto shaft = std::find_if(shafts.begin(), shafts.end(), [&](const LineF& line) {
            return std::abs(line.x1() - x) < 1e-6;
        });
        ASSERT_NE(shaft, shafts.end()) << "expected shaft x " << x << " actual first " << shafts.front().x1()
                                       << " terrain " << x0 << " width " << geometry.changeTerrainWidth;
        EXPECT_NEAR(shaft->y1(), fromY, 1e-6);
        EXPECT_NEAR(shaft->y2(), toY + (up ? headHeight : -headHeight), 1e-6);
        auto head = std::find_if(heads.begin(), heads.end(), [&](const auto& item) {
            return std::abs(item.second.center().x() - x) < 1e-6;
        });
        ASSERT_NE(head, heads.end());
        EXPECT_EQ(head->first, up);
        EXPECT_NEAR(head->second.height(), headHeight, 1e-6);
        EXPECT_NEAR(up ? head->second.top() : head->second.bottom(), toY, 1e-6);
    }
}

static void verifySourceHeaders(MasterScore* score, const QString& damage)
{
    auto sameRect = [](const RectF& a, const RectF& b) {
        return std::abs(a.left() - b.left()) < 1e-6 && std::abs(a.top() - b.top()) < 1e-6
               && std::abs(a.width() - b.width()) < 1e-6 && std::abs(a.height() - b.height()) < 1e-6;
    };
    for (const System* system : score->systems()) {
        Measure* measure = system->firstMeasure();
        if (!measure) {
            continue;
        }
        bool firstVisible = true;
        for (staff_idx_t index = 0; index < score->nstaves(); ++index) {
            const Staff* staff = score->staff(index);
            const StaffType* type = staff->staffType(measure->tick());
            if (!type->isMelo() || !staff->show() || !system->staff(index)->show()) {
                continue;
            }
            SCOPED_TRACE(std::string("header staff ") + std::to_string(index) + " at " + measure->tick().toString().toStdString());
            const StaffLines* lines = measure->staffLines(index);
            ASSERT_TRUE(lines);
            const auto& view = type->meloFrameView(score, index, system);
            ASSERT_FALSE(view.empty());
            const double sp = lines->spatium();
            const double dist = type->lineDistance().val() * sp;
            const double period = type->meloPeriodCents();
            melo::PeriodicOrigins origins;
            ASSERT_TRUE(melo::periodicOrigins(type->meloStateJson(), origins));
            const auto geometry = type->meloHeaderGeometry(sp, score->style().defaultSpatium(), &view);
            const double height = period / StaffType::MELO_CENTS_PER_LINE_DISTANCE * dist;
            const double width = std::max(geometry.clefRx, height / 2.0);
            const double right = lines->pos().x() - 0.3 * sp;
            auto yFor = [&](double cents) { return lines->pos().y() + type->meloYFromCents(cents, view) * sp; };
            std::vector<RectF> expectedPaths;
            std::vector<RectF> expectedClips;
            for (const auto& band : view.bands) {
                for (const auto& segment : band.segments) {
                    const double top = yFor(segment.upperCents);
                    const double bottom = yFor(segment.lowerCents);
                    expectedClips.emplace_back(right - geometry.clefRx - sp, top - lines->lw(),
                                               geometry.clefRx + 2 * sp, bottom - top + 2 * lines->lw());
                    const double first = origins.doCentsAboveExtentLower
                                         + std::floor((segment.lowerCents - origins.doCentsAboveExtentLower) / period + 1e-6) * period;
                    for (double floor = first; floor < segment.upperCents - 1e-6; floor += period) {
                        const double periodTop = top + (segment.upperCents - floor - period)
                                                 / StaffType::MELO_CENTS_PER_LINE_DISTANCE * dist;
                        // Each visible interval needs an opaque crescent body
                        // plus its outline; clipping limits the full-period arcs.
                        expectedPaths.emplace_back(right - width, periodTop, width, height);
                        expectedPaths.emplace_back(right - width, periodTop, width, height);
                    }
                }
            }
            auto provider = std::make_shared<SourceNotationPaintProvider>();
            Painter painter(provider, "source-header-oracle");
            painter.setWindow(RectF(0, 0, 4000, 4000));
            painter.setViewport(RectF(0, 0, 4000, 4000));
            lines->renderer()->drawItem(lines, &painter, PaintOptions());
            painter.endDraw();
            if (damage == "clips") {
                provider->clipRects.clear();
            }
            ASSERT_EQ(provider->clipRects.size(), expectedClips.size());
            for (size_t i = 0; i < expectedClips.size(); ++i) {
                EXPECT_TRUE(sameRect(provider->clipRects[i],
                                     expectedClips[i])) << "crescent clipping differs from the visible staff segment";
            }
            auto drawing = std::make_shared<DrawData>(*provider->drawData());
            std::vector<RectF> actualPaths;
            std::vector<DrawText> tuning;
            std::function<void(DrawData::Item&)> visit = [&](DrawData::Item& item) {
                for (auto& data : item.datas) {
                    auto isClef = [&](const DrawPath& path) {
                        const RectF box = path.path.boundingRect();
                        return std::abs(box.right() - right) < 1e-6 && std::abs(box.width() - width) < 1e-6;
                    };
                    if (damage == "clefs") {
                        data.paths.erase(std::remove_if(data.paths.begin(), data.paths.end(), isClef), data.paths.end());
                    }
                    if (damage == "tuning") {
                        data.texts.erase(std::remove_if(data.texts.begin(), data.texts.end(), [](const DrawText& text) {
                            return text.text.startsWith(u"M5= ");
                        }), data.texts.end());
                    }
                    for (const auto& path : data.paths) {
                        if (isClef(path)) {
                            EXPECT_GT(path.path.elementCount(), 8u) << "clef lost its curved geometry";
                            actualPaths.push_back(path.path.boundingRect());
                        }
                    }
                    for (const auto& text : data.texts) {
                        if (text.text.startsWith(u"M5= ")) {
                            tuning.push_back(text);
                        }
                    }
                }
                for (auto& child : item.chilren) {
                    visit(child);
                }
            };
            visit(drawing->item);
            ASSERT_EQ(actualPaths.size(), expectedPaths.size()) << "each visible crescent requires body and outline";
            for (size_t i = 0; i < expectedPaths.size(); ++i) {
                EXPECT_TRUE(sameRect(actualPaths[i], expectedPaths[i])) << "clef dimensions or period placement changed";
            }
            ASSERT_EQ(tuning.size(), firstVisible ? 1u : 0u);
            if (firstVisible) {
                double generator = 0, periodWidth = 0;
                ASSERT_TRUE(melo::staffMetrics(type->meloStateJson(), generator, periodWidth));
                EXPECT_EQ(tuning[0].text, String(u"M5= %1¢").arg(String::number(generator, 1)));
                EXPECT_NEAR(tuning[0].rect.top(), yFor(view.topCents()) - 1.2 * sp, 1e-6);
                EXPECT_NEAR(tuning[0].rect.left(), right - geometry.clefRx - 2 * geometry.indicatorW, 1e-6);
            }
            firstVisible = false;
        }
    }
}

static void verifySourceNotationOracle(MasterScore* score, const QJsonObject& oracle)
{
    ASSERT_EQ(oracle["schema"].toString(), "melopresto.score-notation-oracle.v1");
    score->doLayout();
    verifySourceHeaders(score, oracle["header_paint_damage"].toString());
    EXPECT_EQ(QJsonDocument::fromJson(score->metaTag(melo::REFERENCE_TIMELINE_TAG).toUtf8().constChar()).object(),
              oracle["reference_timeline"].toObject());
    ASSERT_EQ(score->nstaves(), oracle["staves"].toArray().size());
    auto expectPaint = [](const EngravingItem* item) {
        ASSERT_TRUE(item->visible());
        auto provider = std::make_shared<BufferedPaintProvider>();
        Painter painter(provider, "source-notation-oracle");
        painter.setViewport(RectF(0, 0, 4000, 4000));
        item->renderer()->drawItem(item, &painter, PaintOptions());
        painter.endDraw();
        size_t marks = 0;
        std::function<void(const DrawData::Item&)> visit = [&](const DrawData::Item& entry) {
            for (const auto& data : entry.datas) {
                marks += data.paths.size() + data.polygons.size() + data.texts.size() + data.pixmaps.size();
            }
            for (const auto& child : entry.chilren) {
                visit(child);
            }
        };
        visit(provider->drawData()->item);
        EXPECT_GT(marks, 0u) << "visible source object produced no paint commands";
        EXPECT_TRUE(std::isfinite(item->pos().x()) && std::isfinite(item->pos().y()));
    };
    std::vector<const Note*> actual;
    std::set<const Chord*> chords;
    for (staff_idx_t staff = 0; staff < score->nstaves(); ++staff) {
        for (const Note* note : MusicXml_Melo_Tests::notesInOrder(score, staff)) {
            if (note->hasMeloPitch()) {
                actual.push_back(note);
            }
            chords.insert(note->chord());
        }
    }
    for (const Chord* chord : chords) {
        for (const Chord* grace : chord->graceNotes()) {
            for (const Note* note : grace->notes()) {
                if (note->hasMeloPitch()) {
                    actual.push_back(note);
                }
            }
        }
    }
    EXPECT_EQ(actual.size(), oracle["notes"].toArray().size());
    std::set<const Note*> used;
    std::map<QString, track_idx_t> sourceTracks;
    std::map<track_idx_t, QString> trackSources;
    for (const auto& entry : oracle["notes"].toArray()) {
        const auto expected = entry.toObject();
        SCOPED_TRACE(expected["id"].toString().toStdString());
        const Fraction at = Fraction::fromString(String::fromQString(expected["at"].toString()));
        const QString voiceGroup = expected["voice_group"].toString();
        ASSERT_FALSE(voiceGroup.isEmpty());
        auto found = std::find_if(actual.begin(), actual.end(), [&](const Note* note) {
            const auto mappedTrack = sourceTracks.find(voiceGroup);
            const auto mappedSource = trackSources.find(note->track());
            return !used.count(note) && int(note->chord()->vStaffIdx()) == expected["staff"].toInt()
                   && (mappedTrack == sourceTracks.end() || mappedTrack->second == note->track())
                   && (mappedSource == trackSources.end() || mappedSource->second == voiceGroup) && note->tick() == at
                   && note->chord()->isGrace() == expected["grace"].toBool()
                   && note->meloNPer() == expected["n_per"].toInt() && note->meloNGen() == expected["n_gen"].toInt();
        });
        ASSERT_NE(found, actual.end()) << "source occurrence has no matching Score note";
        const Note* note = *found;
        used.insert(note);
        sourceTracks[voiceGroup] = note->track();
        trackSources[note->track()] = voiceGroup;
        if (!expected["grace"].toBool()) {
            EXPECT_EQ(note->chord()->actualTicks(), Fraction::fromString(String::fromQString(expected["duration"].toString())));
        }
        ASSERT_TRUE(note->meloCentsValid());
        EXPECT_NEAR(note->meloCentsAboveDo(), expected["cents_above_extent_lower"].toDouble(), 1e-6);
        const StaffType* type = score->staff(note->chord()->vStaffIdx())->staffTypeForElement(note);
        EXPECT_NEAR(note->pos().y(), note->meloPosY(type), 1e-6);
        const QString shape = expected["notehead"].toString();
        const NoteHeadGroup group = shape == "triangle-vertex-up" ? NoteHeadGroup::HEAD_TRIANGLE_UP
                                    : shape == "triangle-vertex-down" ? NoteHeadGroup::HEAD_TRIANGLE_DOWN
                                    : shape == "square-vertex-up" ? NoteHeadGroup::HEAD_DIAMOND
                                    : shape == "square-edge-up" ? NoteHeadGroup::HEAD_LA : NoteHeadGroup::HEAD_NORMAL;
        const auto durationHead = note->headType() == NoteHeadType::HEAD_AUTO ? note->chord()->durationType().headType() : note->headType();
        EXPECT_EQ(note->noteHead(), Note::noteHead(note->chord()->up(), group, durationHead));
        std::optional<muse::mpe::ExactPitch> sounding;
        NominalNoteCtx::nominalPitchLevelOf(note, &sounding);
        ASSERT_TRUE(sounding.has_value());
        EXPECT_NEAR(sounding->frequencyHz, expected["frequency_hz"].toDouble(), 1e-6);
        expectPaint(note);
    }
    for (const auto& entry : oracle["staves"].toArray()) {
        const auto expected = entry.toObject();
        const Staff* staff = score->staff(expected["staff"].toInt());
        for (const auto& stateEntry : expected["states"].toArray()) {
            const auto state = stateEntry.toObject();
            ASSERT_TRUE(state.contains("indicator"));
            const Fraction at = Fraction::fromString(String::fromQString(state["at"].toString()));
            String configuration, error;
            ASSERT_TRUE(melo::staffConfiguration(staff->meloStateAt(at), configuration, error)) << error.toStdString();
            EXPECT_EQ(QJsonDocument::fromJson(configuration.toUtf8().constChar()).object(), state["configuration"].toObject())
                << "staff " << expected["staff"].toInt() << " at " << state["at"].toString().toStdString()
                << " actual " << configuration.toStdString() << " expected "
                << QJsonDocument(state["configuration"].toObject()).toJson(QJsonDocument::Compact).toStdString();
            ASSERT_EQ(state["indicator"].isNull(), at.isZero());
            if (!state["indicator"].isNull()) {
                SCOPED_TRACE(std::string("indicator staff ") + std::to_string(expected["staff"].toInt()) + " at "
                             + state["at"].toString().toStdString());
                verifySourceIndicator(score, expected["staff"].toInt(), at, state["indicator"].toObject());
            }
        }
    }
    std::vector<Harmony*> harmonies;
    for (Harmony* harmony : MusicXml_Melo_Tests::harmoniesInOrder(score)) {
        if (harmony->harmonyType() == HarmonyType::MELO) {
            harmonies.push_back(harmony);
        } else {
            EXPECT_FALSE(harmony->visible()) << "A source analytical label remains visible beside selected MeloPresto harmony";
        }
    }
    ASSERT_EQ(harmonies.size(), oracle["harmonies"].toArray().size());
    for (const auto& entry : oracle["harmonies"].toArray()) {
        const auto expected = entry.toObject();
        auto found = std::find_if(harmonies.begin(), harmonies.end(), [&](const Harmony* harmony) {
            return int(harmony->staffIdx()) == expected["staff"].toInt()
                   && harmony->tick() == Fraction::fromString(String::fromQString(expected["at"].toString()))
                   && harmony->harmonyName().toQString() == expected["name"].toString();
        });
        ASSERT_NE(found, harmonies.end()) << expected["name"].toString().toStdString();
        EXPECT_TRUE((*found)->meloEvidenceError().empty()) << (*found)->meloEvidenceError().toStdString();
        expectPaint(*found);
        harmonies.erase(found);
    }
    QStringList actualLyrics, expectedLyrics;
    for (const Chord* chord : chords) {
        for (const Lyrics* lyric : chord->lyrics()) {
            actualLyrics.push_back(lyric->plainText().toQString());
            expectPaint(lyric);
        }
    }
    for (const auto& lyric : oracle["source_lyric_texts"].toArray()) {
        expectedLyrics.push_back(lyric.toString());
    }
    actualLyrics.sort();
    expectedLyrics.sort();
    EXPECT_EQ(actualLyrics, expectedLyrics);
}

TEST_F(MusicXml_Melo_Tests, CompleteSourceNotationOracle)
{
    std::unique_ptr<MasterScore> score(readMelo("v5/melo-continuous-tuning.musicxml"));
    ASSERT_TRUE(score);
    verifySourceNotationOracle(score.get(), QJsonDocument::fromJson(
                                   readAll(ScoreRW::rootPath() + u"/" + MELO_DATA_DIR
                                           + u"v5/continuous-tuning/notation-oracle.json").toUtf8().constChar()).object());
}

TEST_F(MusicXml_Melo_Tests, CrossStaffSourceNotationOracle)
{
    std::unique_ptr<MasterScore> score(readMelo("v5/melo-cross-staff-voice.musicxml"));
    ASSERT_TRUE(score);
    verifySourceNotationOracle(score.get(), QJsonDocument::fromJson(
                                   readAll(ScoreRW::rootPath() + u"/" + MELO_DATA_DIR
                                           + u"v5/cross-staff-voice/notation-oracle.json").toUtf8().constChar()).object());
}

TEST_F(MusicXml_Melo_Tests, CompleteSourceNotationOptionalPrivateCorpus)
{
    const char* scorePath = std::getenv("MELO_NOTATION_SCORE");
    const char* oraclePath = std::getenv("MELO_NOTATION_ORACLE");
    if (!scorePath || !oraclePath) {
        GTEST_SKIP() << "Supply one explicitly selected source-derived oracle and native Score";
    }
    std::unique_ptr<MasterScore> score(ScoreRW::readScore(String::fromUtf8(scorePath), true));
    ASSERT_TRUE(score);
    verifySourceNotationOracle(score.get(), QJsonDocument::fromJson(readAll(String::fromUtf8(oraclePath)).toUtf8().constChar()).object());
}

TEST_F(MusicXml_Melo_Tests, GeneratedEvidenceOptionalPrivateCorpus)
{
    const char* path = std::getenv("MELO_CHORD_EVIDENCE_SCORE");
    if (!path) {
        GTEST_SKIP() << "Private corpus score supplied only for the local corpus gate";
    }
    MasterScore* score = ScoreRW::readScore(String::fromUtf8(path), true);
    ASSERT_TRUE(score);
    for (Harmony* harmony : harmoniesInOrder(score)) {
        EXPECT_TRUE(harmony->meloEvidenceError().empty())
            << harmony->tick().toString().toStdString() << " " << harmony->harmonyName().toStdString()
            << ": " << harmony->meloEvidenceError().toStdString();
    }
    delete score;
}

TEST_F(MusicXml_Melo_Tests, HarmonicSpanKeepsOneNameAcrossDelayedMember)
{
    MasterScore* score = readMelo("v5/melo-harmonic-span-evidence.musicxml");
    ASSERT_TRUE(score);
    ASSERT_EQ(harmoniesInOrder(score).size(), 1u);
    Harmony* harmony = harmoniesInOrder(score).front();
    EXPECT_EQ(harmony->harmonyName(), u"Do5");
    EXPECT_TRUE(harmony->meloEvidenceError().empty()) << harmony->meloEvidenceError().toStdString();
    const String output = exportToScratch(score, "harmonic-span.musicxml");
    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    MasterScore* again = ScoreRW::readScore(output, true, importXml);
    ASSERT_TRUE(again);
    ASSERT_EQ(harmoniesInOrder(again).size(), 1u);
    EXPECT_EQ(harmoniesInOrder(again).front()->meloEvidence(), harmony->meloEvidence());
    EXPECT_TRUE(harmoniesInOrder(again).front()->meloEvidenceError().empty());
    Note* arrivingRoot = nullptr;
    for (const Note* note : notesInOrder(score, 0)) {
        if (note->meloNGen() == -2) {
            arrivingRoot = const_cast<Note*>(note);
        }
    }
    ASSERT_TRUE(arrivingRoot);
    score->startCmd(TranslatableString::untranslatable("Change later supporting root"));
    arrivingRoot->undoChangeProperty(Pid::MELO_NPER, arrivingRoot->meloNPer() + 1);
    score->endCmd();
    EXPECT_FALSE(harmony->meloEvidenceError().empty());
    score->undoRedo(true, nullptr);
    EXPECT_TRUE(harmony->meloEvidenceError().empty());
    delete again;
    delete score;
}

TEST_F(MusicXml_Melo_Tests, RedundantCarrierPreservesSharedEffectiveTimeline)
{
    MasterScore* score=readMelo("v5/jims-multi-part-shared.musicxml");
    ASSERT_TRUE(score);
    Measure* measure=score->firstMeasure();
    StaffTypeChange* carrier=Factory::createStaffTypeChange(measure);
    carrier->setTrack(0);
    carrier->setRtick(Fraction(1, 4));
    carrier->setStaffType(new StaffType(*score->staff(0)->staffType(Fraction(0, 1))), true);
    measure->add(carrier);
    String error;
    ASSERT_TRUE(melo::rebuildCanonicalReferenceContexts(score, error)) << error.toStdString();
    score->rebuildMidiMapping();
    score->doLayout();
    muse::io::Buffer buffer;
    buffer.open(muse::io::IODevice::WriteOnly);
    EXPECT_TRUE(saveXml(score, &buffer));
    EXPECT_FALSE(buffer.data().empty());
    const MeloSnapshot before = snapshotOf(score);
    const String output = exportToScratch(score, "redundant-carrier-roundtrip.musicxml");
    auto importXml = [](MasterScore* s, const muse::io::path_t& path) -> engraving::Err {
        return importMusicXml(s, path.toQString(), false);
    };
    MasterScore* again = ScoreRW::readScore(output, true, importXml);
    ASSERT_TRUE(again);
    EXPECT_EQ(snapshotOf(again).identities, before.identities);
    EXPECT_EQ(snapshotOf(again).baseStates, before.baseStates);
    EXPECT_EQ(snapshotOf(again).carriers, before.carriers);
    delete again;
    delete score;
}

TEST_F(MusicXml_Melo_Tests, ContinuousTuningPlaybackProfileMatchesTheNoteOnset)
{
    std::unique_ptr<MasterScore> score(readMelo("v5/melo-continuous-tuning.musicxml"));
    ASSERT_TRUE(score);
    const auto notes = notesInOrder(score.get(), 0);
    ASSERT_EQ(notes.size(), 2u);
    auto profile = std::make_shared<muse::mpe::ArticulationsProfile>();
    muse::mpe::ArticulationPatternSegment segment;
    segment.arrangementPattern = muse::mpe::tests::createArrangementPattern(muse::mpe::HUNDRED_PERCENT, 0);
    segment.pitchPattern = muse::mpe::tests::createSimplePitchPattern(0);
    segment.expressionPattern
        = muse::mpe::tests::createSimpleExpressionPattern(muse::mpe::dynamicLevelFromType(muse::mpe::DynamicType::Natural));
    muse::mpe::ArticulationPattern pattern;
    pattern.emplace(0, segment);
    profile->setPattern(muse::mpe::ArticulationType::Standard, pattern);
    auto context = std::make_shared<PlaybackContext>();
    PlaybackEventsRenderer renderer;
    for (const Note* note : notes) {
        muse::mpe::PlaybackEventsMap eventMap;
        renderer.render(note, 0, 500000, muse::mpe::dynamicLevelFromType(muse::mpe::DynamicType::Natural), context, profile, eventMap);
        ASSERT_EQ(eventMap.size(), 1u);
        const auto& events = eventMap.at(0);
        ASSERT_EQ(events.size(), 2u);
        ASSERT_TRUE(std::holds_alternative<muse::mpe::DynamicTonalityProfileEvent>(events[0]));
        muse::mpe::DynamicTonalityProfileEvent expected;
        String error;
        ASSERT_TRUE(melo::vst3ProfileTransaction(note->staff()->meloStateAt(note->tick()), 0, 0, 0, expected,
                                                 &error)) << error.toStdString();
        EXPECT_EQ(std::get<muse::mpe::DynamicTonalityProfileEvent>(events[0]), expected);
        ASSERT_TRUE(std::holds_alternative<muse::mpe::NoteEvent>(events[1]));
        EXPECT_TRUE(std::get<muse::mpe::NoteEvent>(events[1]).pitchCtx().exactPitch.has_value());
    }
}
