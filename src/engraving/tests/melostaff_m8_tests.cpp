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

// JiMStaff Milestone 8 — octave-band elision ("hollow stacks"; owner-approved
// plan Plans/Add_JiMS_to_MuseScore/M8_OctaveBandElision_Plan.md, 2026-08-18).
// The Kernel decides which periods survive, each band's bounds and label,
// and the omitted count; the fork slices the melody per system, resolves the
// presentation policy (score style + first-system rule + per-staff
// Auto/On/Off — MuseScore's hide-empty-staves shape), caches, maps y<->cents
// piecewise, and draws. Elision is off by default and changes nothing then.

#include <gtest/gtest.h>

#include "engraving/dom/barline.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafflines.h"
#include "engraving/dom/stafftype.h"
#include "engraving/dom/system.h"
#include "engraving/iengravingfont.h"
#include "engraving/infrastructure/mscwriter.h"
#include "engraving/melo/melobridge.h"
#include "engraving/melo/melochange.h"
#include "engraving/rendering/iscorerenderer.h"
#include "engraving/rw/mscsaver.h"
#include "engraving/style/style.h"
#include "draw/bufferedpaintprovider.h"
#include "draw/painter.h"
#include "io/file.h"
#include "io/dir.h"

#include "utils/scorerw.h"

using namespace mu::engraving;
using namespace mu::engraving::rendering;
using namespace muse;
using namespace muse::draw;

namespace {
const String TWO_HAND(u"jimstaff_data/m8-two-hand.mscx");
const String TWO_STAVES(u"jimstaff_data/m8-two-staves.mscx");
const String SINGLE_OCTAVE(u"jimstaff_data/collision.mscx");

const double EPS = 1e-9;

class Engraving_MeloStaffM8BandElisionTests : public ::testing::Test
{
protected:
    static const StaffType* st(Score* score, staff_idx_t staffIdx = 0)
    {
        return score->staff(staffIdx)->staffType(Fraction(0, 1));
    }

    static StaffType* mutSt(Score* score, staff_idx_t staffIdx = 0)
    {
        return score->staff(staffIdx)->staffType(Fraction(0, 1));
    }

    static std::vector<System*> measureSystems(Score* score)
    {
        std::vector<System*> out;
        for (System* s : score->systems()) {
            if (s->firstMeasure()) {
                out.push_back(s);
            }
        }
        return out;
    }

    static const StaffType::MeloFrameView& viewOn(Score* score, System* system, staff_idx_t staffIdx = 0)
    {
        return st(score, staffIdx)->meloFrameView(score, staffIdx, system);
    }

    static void setElision(Score* score, bool on)
    {
        score->style().set(Sid::meloElideEmptyOctaves, on);
        score->setLayoutAll();
        score->doLayout();
    }

    static void setFirstSystemAll(Score* score, bool on)
    {
        score->style().set(Sid::meloShowAllOctavesInFirstSystem, on);
        score->setLayoutAll();
        score->doLayout();
    }

    static void setOverride(Score* score, MeloElideOctaves mode, staff_idx_t staffIdx = 0)
    {
        mutSt(score, staffIdx)->setMeloElideOctaves(mode);
        score->setLayoutAll();
        score->doLayout();
    }

    static Measure* measureNo(Score* score, int n)
    {
        Measure* m = score->firstMeasure();
        for (int i = 1; m && i < n; ++i) {
            m = m->nextMeasure();
        }
        return m;
    }

    static std::vector<Chord*> chordsOf(Measure* m, staff_idx_t staffIdx = 0)
    {
        std::vector<Chord*> out;
        for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            for (voice_idx_t v = 0; v < VOICES; ++v) {
                EngravingItem* e = s->element(staffIdx * VOICES + v);
                if (e && e->isChord()) {
                    out.push_back(toChord(e));
                }
            }
        }
        return out;
    }

    static int redDoLineCount(const StaffLines* lines)
    {
        int n = 0;
        for (const StaffLines::MeloGuideLine& g : lines->meloGuideLines()) {
            if (!g.dashed && g.colorStyle == Sid::meloDoLineColor) {
                ++n;
            }
        }
        return n;
    }

    /// The fixture text with extra Re notes injected into bar `bar`'s first
    /// RH chord (period indices given), written to a scratch file.
    static String fixtureWithExtraNotes(const std::vector<int>& periods, int bar, const char* name)
    {
        io::File f(ScoreRW::rootPath() + u"/" + TWO_HAND);
        EXPECT_TRUE(f.open(io::IODevice::ReadOnly));
        String text = String::fromUtf8(f.readAll());
        // Find the bar: the (bar)-th "<Measure>" occurrence, then its first "<Chord>".
        size_t pos = 0;
        for (int i = 0; i < bar; ++i) {
            pos = text.indexOf(u"<Measure>", pos + 1);
        }
        const size_t chordEnd = text.indexOf(u"</Note>", pos) + String(u"</Note>").size();
        String extra;
        for (int p : periods) {
            // Re in period p: lattice (p, 0); MIDI D(p+4) = 62 + 12*p, tpc 16.
            extra += String(u"\n            <Note>\n              <pitch>%1</pitch>\n              <tpc>16</tpc>\n"
                            u"              <jimsNPer>%2</jimsNPer>\n              <jimsNGen>0</jimsNGen>\n              </Note>")
                     .arg(62 + 12 * p).arg(p);
        }
        text.insert(chordEnd, extra);
        const String dir = ScoreRW::rootPath() + u"/../../../build.release/jims-m8-scratch";
        io::Dir::mkpath(dir);
        const String path = dir + u"/" + String::fromUtf8(name);
        io::File out(path);
        EXPECT_TRUE(out.open(io::IODevice::WriteOnly));
        out.write(text.toUtf8());
        out.close();
        return path;
    }
};

// One-band structural identity (Phase 2): the whole-piece view is one band at
// yTop 0 whose map is the legacy seam bit for bit, and the inverse round-trips.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8WholeViewIsOneBandWithLegacyGeometry)
{
    MasterScore* score = ScoreRW::readScore(TWO_HAND);
    ASSERT_TRUE(score);
    score->doLayout();
    const StaffType* jst = st(score);
    ASSERT_TRUE(jst->isMelo());
    const StaffType::MeloFrameView& whole = jst->meloWholeFrameView(score, 0);
    ASSERT_EQ(whole.bands.size(), 1u);
    EXPECT_FALSE(whole.banded);
    EXPECT_EQ(whole.omittedPeriodCount, 0);
    EXPECT_EQ(whole.bands[0].yTopLd, 0.0);
    EXPECT_EQ(whole.bands[0].segments.size(), jst->meloFrameSegments().size());
    EXPECT_EQ(whole.bands[0].segments.size(), 5u);   // five segments across the fitted note extent
    // The whole frame's "[PitchN]:" names the period index selected by the
    // Kernel for its lowest labelled tonic row, not an inferred extent centre.
    EXPECT_EQ(whole.bands[0].labelPeriodIndex, -1);
    melo::TonicPitchLabel wholeLabel;
    ASSERT_TRUE(melo::tonicPitchLabelInPeriod(jst->meloStateJson(), whole.bands[0].labelPeriodIndex, wholeLabel));
    EXPECT_TRUE(whole.bands[0].tonicLabel == wholeLabel.label);
    for (double cents : { 0.0, 900.0, 2400.0, 3637.5, 5700.0, 5800.0 }) {
        EXPECT_EQ(jst->meloYFromCents(cents, whole), jst->meloYFromCents(cents)) << cents;
        EXPECT_NEAR(whole.centsFromYLd(whole.yLdFromCents(cents)), cents, EPS) << cents;
    }
    // Every system uses the whole view when elision is off, and no chord moved.
    for (System* system : measureSystems(score)) {
        const StaffType::MeloFrameView& v = viewOn(score, system);
        EXPECT_FALSE(v.banded);
        EXPECT_EQ(v.bands.size(), 1u);
        for (MeasureBase* mb : system->measures()) {
            if (!mb->isMeasure()) {
                continue;
            }
            for (Chord* c : chordsOf(toMeasure(mb))) {
                EXPECT_EQ(c->ldata()->pos().y(), 0.0);
            }
        }
    }
    delete score;
}

// (i) Elision off (the default): every system draws the whole stack; the
// three switches read their defaults; the staff type override is Auto.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8ElisionOffMatchesPhase2Baseline)
{
    MasterScore* score = ScoreRW::readScore(TWO_HAND);
    ASSERT_TRUE(score);
    score->doLayout();
    EXPECT_FALSE(score->style().styleB(Sid::meloElideEmptyOctaves));
    EXPECT_TRUE(score->style().styleB(Sid::meloShowAllOctavesInFirstSystem));
    EXPECT_EQ(st(score)->meloElideOctaves(), MeloElideOctaves::Auto);
    const std::vector<System*> systems = measureSystems(score);
    ASSERT_EQ(systems.size(), 4u);
    for (System* system : systems) {
        EXPECT_FALSE(st(score)->meloElisionActive(score, 0, system));
        const StaffType::MeloFrameView& v = viewOn(score, system);
        EXPECT_EQ(v.bands.size(), 1u);
        EXPECT_NEAR(v.bottomCents(), -200.0, EPS);
        EXPECT_NEAR(v.topCents(), 5800.0, EPS);
        Measure* m = system->firstMeasure();
        // The fitted extent lower is not Do. The six actual Do rows
        // inside this frame are each drawn exactly once.
        EXPECT_EQ(redDoLineCount(m->staffLines(0)), 6);
    }
    delete score;
}

// (ii) Style on + staff Auto: system 1 whole (first-system rule), later
// systems two bands with three intervening segments omitted; per-band labels and Do-line
// counts; staff height = band heights + one staffDistance gap.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8StyleOnBandsLaterSystemsWithLabelsAndHeight)
{
    MasterScore* score = ScoreRW::readScore(TWO_HAND);
    ASSERT_TRUE(score);
    score->doLayout();
    setElision(score, true);
    const std::vector<System*> systems = measureSystems(score);
    ASSERT_EQ(systems.size(), 4u);
    // System 1: the whole-piece frame.
    {
        const StaffType::MeloFrameView& v = viewOn(score, systems[0]);
        EXPECT_FALSE(v.banded);
        EXPECT_EQ(v.bands.size(), 1u);
        EXPECT_EQ(redDoLineCount(systems[0]->firstMeasure()->staffLines(0)), 6);
    }
    const double ld = st(score)->lineDistance().val();
    const double gapLd = score->style().styleS(Sid::staffDistance).val() / ld;
    for (size_t i = 1; i < systems.size(); ++i) {
        const StaffType::MeloFrameView& v = viewOn(score, systems[i]);
        EXPECT_TRUE(v.banded) << "system " << i + 1;
        ASSERT_EQ(v.bands.size(), 2u) << "system " << i + 1;
        EXPECT_EQ(v.omittedPeriodCount, 3);
        EXPECT_NEAR(v.bands[0].lowerCents, -200.0, EPS);
        EXPECT_NEAR(v.bands[0].upperCents, 1000.0, EPS);
        EXPECT_NEAR(v.bands[1].lowerCents, 4600.0, EPS);
        EXPECT_NEAR(v.bands[1].upperCents, 5800.0, EPS);
        EXPECT_EQ(v.bands[0].labelPeriodIndex, -1);
        EXPECT_EQ(v.bands[1].labelPeriodIndex, 3);
        for (const StaffType::MeloFrameBand& band : v.bands) {
            melo::TonicPitchLabel expected;
            ASSERT_TRUE(melo::tonicPitchLabelInPeriod(st(score)->meloStateJson(), band.labelPeriodIndex, expected));
            EXPECT_TRUE(band.tonicLabel == expected.label);
        }
        // Geometry: top band at 0, bottom band below it plus one gap.
        EXPECT_NEAR(v.bands[1].yTopLd, 0.0, EPS);
        EXPECT_NEAR(v.bands[0].yTopLd, 12.0 + gapLd, EPS);
        EXPECT_NEAR(v.heightLd(), 24.0 + gapLd, EPS);
        EXPECT_NEAR(v.gapLd, gapLd, EPS);
        // Two boundary Do rows in each band,
        // with none in the gap.
        Measure* m = systems[i]->firstMeasure();
        EXPECT_EQ(redDoLineCount(m->staffLines(0)), 4);
        // The staff lines' bbox is the drawn height (band heights + gap).
        const double spatium = score->style().spatium();
        const StaffLines* lines = systems[i]->lastMeasure()->staffLines(0);
        EXPECT_NEAR(lines->ldata()->bbox().height(), v.heightLd() * ld * spatium + lines->lw(), 1e-6);
        // Every chord sits in a band: LH chords shifted to the bottom band,
        // RH chords in the top band (offset 0 relative to the whole frame's
        // top, which is also the top band's top here).
        for (MeasureBase* mb : systems[i]->measures()) {
            if (!mb->isMeasure()) {
                continue;
            }
            for (Chord* c : chordsOf(toMeasure(mb))) {
                const Note* n = c->notes().front();
                const StaffType::MeloFrameBand* band = v.bandForCents(n->meloCentsAboveDo());
                ASSERT_TRUE(band);
                const double expectedLd = band->yTopLd + (band->upperCents - v.topCents()) / StaffType::MELO_CENTS_PER_LINE_DISTANCE;
                EXPECT_NEAR(c->ldata()->pos().y(), expectedLd * ld * c->spatium(), 1e-6);
                // And the note's page y equals the piecewise map of its cents.
                const double noteYLd = (c->ldata()->pos().y() + n->ldata()->pos().y()) / (ld * c->spatium());
                EXPECT_NEAR(noteYLd, v.yLdFromCents(n->meloCentsAboveDo()), 0.2) << "notehead centroid correction aside";
            }
        }
    }
    delete score;
}

// (iv) First-system switch off: system 1 is banded too, and the header time
// signature (tick 0) sits inside a band — the band holding the stack's
// vertical middle, or the band above the gap when the middle falls in it —
// never in the gap and never below the stack.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8FirstSystemSwitchOffBandsSystemOneToo)
{
    MasterScore* score = ScoreRW::readScore(TWO_HAND);
    ASSERT_TRUE(score);
    score->doLayout();
    setElision(score, true);
    setFirstSystemAll(score, false);
    const std::vector<System*> systems = measureSystems(score);
    ASSERT_EQ(systems.size(), 4u);
    for (System* system : systems) {
        const StaffType::MeloFrameView& v = viewOn(score, system);
        EXPECT_TRUE(v.banded);
        EXPECT_EQ(v.bands.size(), 2u);
        EXPECT_EQ(v.omittedPeriodCount, 3);
    }
    {
        Measure* m1 = systems[0]->firstMeasure();
        Segment* tsSeg = m1->findSegmentR(SegmentType::TimeSig, Fraction(0, 1));
        ASSERT_TRUE(tsSeg);
        EngravingItem* ts = tsSeg->element(0);
        ASSERT_TRUE(ts && ts->isTimeSig());
        const StaffType::MeloFrameView& v = viewOn(score, systems[0]);
        const double ld = st(score)->lineDistance().val() * ts->spatium();
        // The time signature's vertical centre, in line distances below the staff top.
        const double centerLd = (ts->ldata()->pos().y() + ts->ldata()->bbox().center().y()) / ld;
        const StaffType::MeloFrameBand& top = v.bands.back();
        EXPECT_GE(centerLd, top.yTopLd - 0.5);
        EXPECT_LE(centerLd, top.yTopLd + top.heightLd() + 0.5);
        EXPECT_NEAR(centerLd, top.yTopLd + top.heightLd() / 2.0, 1.0);
    }
    setFirstSystemAll(score, true);
    EXPECT_FALSE(viewOn(score, measureSystems(score)[0]).banded);
    delete score;
}

// (iii) Staff Off beats style On; staff On beats style Off; Auto follows.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8StaffOverrideBeatsStyleInBothDirections)
{
    MasterScore* score = ScoreRW::readScore(TWO_HAND);
    ASSERT_TRUE(score);
    score->doLayout();
    setElision(score, true);
    ASSERT_TRUE(viewOn(score, measureSystems(score)[1]).banded);
    setOverride(score, MeloElideOctaves::Off);
    for (System* system : measureSystems(score)) {
        EXPECT_FALSE(viewOn(score, system).banded);
    }
    setElision(score, false);
    EXPECT_FALSE(viewOn(score, measureSystems(score)[1]).banded);
    setOverride(score, MeloElideOctaves::On);
    EXPECT_FALSE(viewOn(score, measureSystems(score)[0]).banded);   // first-system rule still applies
    EXPECT_TRUE(viewOn(score, measureSystems(score)[1]).banded);
    setOverride(score, MeloElideOctaves::Auto);
    EXPECT_FALSE(viewOn(score, measureSystems(score)[1]).banded);   // Auto follows the (off) style
    delete score;
}

// Single-octave melody: one band, unchanged from the whole frame.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8SingleOctaveMelodyIsOneBandUnchanged)
{
    MasterScore* score = ScoreRW::readScore(SINGLE_OCTAVE);
    ASSERT_TRUE(score);
    score->doLayout();
    std::vector<std::vector<StaffType::MeloSegment> > before;
    for (System* system : measureSystems(score)) {
        before.push_back(viewOn(score, system).bands.front().segments);
    }
    setElision(score, true);
    setFirstSystemAll(score, false);
    const std::vector<System*> systems = measureSystems(score);
    ASSERT_EQ(systems.size(), before.size());
    for (size_t i = 0; i < systems.size(); ++i) {
        const StaffType::MeloFrameView& v = viewOn(score, systems[i]);
        EXPECT_EQ(v.bands.size(), 1u);
        EXPECT_EQ(v.omittedPeriodCount, 0);
        ASSERT_EQ(v.bands.front().segments.size(), before[i].size());
        for (size_t k = 0; k < before[i].size(); ++k) {
            EXPECT_NEAR(v.bands.front().segments[k].lowerCents, before[i][k].lowerCents, EPS);
            EXPECT_NEAR(v.bands.front().segments[k].upperCents, before[i][k].upperCents, EPS);
            EXPECT_EQ(v.bands.front().segments[k].whole, before[i][k].whole);
        }
        EXPECT_NEAR(v.heightLd(), (v.topCents() - v.bottomCents()) / StaffType::MELO_CENTS_PER_LINE_DISTANCE, EPS);
    }
    delete score;
}

// Every octave touched on a system: one band there, bands elsewhere.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8EveryOctaveTouchedIsOneBand)
{
    const String path = fixtureWithExtraNotes({ -1, 0, 1 }, 3, "m8-every-octave.mscx");
    MasterScore* score = ScoreRW::readScore(path, true);
    ASSERT_TRUE(score);
    score->doLayout();
    setElision(score, true);
    const std::vector<System*> systems = measureSystems(score);
    ASSERT_EQ(systems.size(), 4u);
    const StaffType::MeloFrameView& sys2 = viewOn(score, systems[1]);
    EXPECT_TRUE(sys2.banded);
    EXPECT_EQ(sys2.bands.size(), 1u);
    EXPECT_EQ(sys2.omittedPeriodCount, 0);
    EXPECT_EQ(sys2.bands.front().segments.size(), 5u);
    EXPECT_EQ(viewOn(score, systems[2]).bands.size(), 2u);
    delete score;
}

// Gap clicks snap to the nearest band edge; the exact midpoint resolves toward
// the lower-pitched band; the model inverse and note entry agree.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8GapClickSnapsToNearestBandEdgeAndTieGoesLow)
{
    MasterScore* score = ScoreRW::readScore(TWO_HAND);
    ASSERT_TRUE(score);
    score->doLayout();
    setElision(score, true);
    System* system2 = measureSystems(score)[1];
    const StaffType::MeloFrameView& v = viewOn(score, system2);
    ASSERT_EQ(v.bands.size(), 2u);
    const StaffType::MeloFrameBand& top = v.bands[1];
    const StaffType::MeloFrameBand& bottom = v.bands[0];
    const double gapTop = top.yTopLd + top.heightLd();     // top band's bottom edge (2400 c)
    const double gapBottom = bottom.yTopLd;                // bottom band's top edge (-1200 c)
    ASSERT_GT(gapBottom, gapTop);
    // Model inverse.
    EXPECT_NEAR(v.centsFromYLd(gapTop + 0.1), top.lowerCents, EPS);
    EXPECT_NEAR(v.centsFromYLd(gapBottom - 0.1), bottom.upperCents, EPS);
    EXPECT_NEAR(v.centsFromYLd((gapTop + gapBottom) / 2.0), bottom.upperCents, EPS);   // tie -> lower-pitched
    // Inside bands: the affine inverse.
    EXPECT_NEAR(v.centsFromYLd(top.yTopLd + 6.0), top.upperCents - 600.0, EPS);
    EXPECT_NEAR(v.centsFromYLd(bottom.yTopLd + 6.0), bottom.upperCents - 600.0, EPS);
    // Note entry through Score::noteValForPosition: line counts half line
    // distances below the staff top; the snapped cents quantize to the
    // nearest lattice pitch (C6 = 84 at the top band's bottom edge, C3 = 48
    // at the bottom band's top edge, the tie to C3).
    Measure* m3 = system2->firstMeasure();
    Segment* seg = m3->first(SegmentType::ChordRest);
    ASSERT_TRUE(seg);
    auto entryPitch = [&](double yLd) {
        Position pos;
        pos.segment = seg;
        pos.staffIdx = 0;
        pos.line = int(std::lround(yLd * 2.0));
        pos.fret = INVALID_FRET_INDEX;
        bool error = false;
        NoteVal nval = score->noteValForPosition(pos, AccidentalType::NONE, error);
        EXPECT_FALSE(error);
        return nval.pitch;
    };
    EXPECT_EQ(entryPitch(gapTop + 0.5), 84);
    EXPECT_EQ(entryPitch(gapBottom - 0.5), 48);
    EXPECT_EQ(entryPitch((gapTop + gapBottom) / 2.0), 48);
    delete score;
}

// Drag freeze: while frozen the system view never re-derives; the drop
// (unfreeze + layout) re-derives once and grows the band.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8DragFreezeThenDropRederives)
{
    MasterScore* score = ScoreRW::readScore(TWO_HAND);
    ASSERT_TRUE(score);
    score->doLayout();
    setElision(score, true);
    System* system2 = measureSystems(score)[1];
    ASSERT_EQ(viewOn(score, system2).bands.size(), 2u);
    Chord* rh = chordsOf(system2->firstMeasure()).front();
    Note* n = rh->notes().front();
    ASSERT_EQ(n->meloNPer(), 2);   // D6 (2,0)
    st(score)->meloSetFrameFrozen(true);
    // Move the note into the omitted interior period; its occupancy must
    // add the occupied middle band only after the frozen frame is released.
    n->setMeloPitch(0, 0);
    n->setPitch(62);
    score->setLayoutAll();
    score->doLayout();
    // Frozen: the two-band view survives. (System objects are recreated by
    // every layout — always re-fetch them; the view cache itself is keyed by
    // tick range, never by System pointer.)
    system2 = measureSystems(score)[1];
    EXPECT_EQ(viewOn(score, system2).bands.size(), 2u);
    EXPECT_EQ(viewOn(score, system2).omittedPeriodCount, 3);
    st(score)->meloSetFrameFrozen(false);
    score->setLayoutAll();
    score->doLayout();
    system2 = measureSystems(score)[1];
    const StaffType::MeloFrameView& after = viewOn(score, system2);
    ASSERT_EQ(after.bands.size(), 3u);
    EXPECT_EQ(after.omittedPeriodCount, 2);
    EXPECT_NEAR(after.bottomCents(), -200.0, EPS);
    EXPECT_NEAR(after.topCents(), 5800.0, EPS);
    delete score;
}

// A keyboard octave step into a missing register grows only that system.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8KeyboardOctaveStepGrowsOnlyTheAffectedSystem)
{
    MasterScore* score = ScoreRW::readScore(TWO_HAND);
    ASSERT_TRUE(score);
    score->doLayout();
    setElision(score, true);
    std::vector<System*> systems = measureSystems(score);
    Chord* rh = chordsOf(systems[1]->firstMeasure()).front();
    Note* n = rh->notes().front();
    ASSERT_EQ(n->meloNPer(), 2);
    score->select(n);
    score->startCmd(TranslatableString::untranslatable("M8 test octave step"));
    score->upDown(false, UpDownMode::OCTAVE);
    score->upDown(false, UpDownMode::OCTAVE);
    score->endCmd();
    score->doLayout();
    EXPECT_EQ(n->meloNPer(), 0);
    systems = measureSystems(score);
    ASSERT_EQ(systems.size(), 4u);
    const StaffType::MeloFrameView& sys2 = viewOn(score, systems[1]);
    EXPECT_EQ(sys2.bands.size(), 3u);
    EXPECT_EQ(sys2.omittedPeriodCount, 2);
    EXPECT_NEAR(sys2.bottomCents(), -200.0, EPS);
    EXPECT_NEAR(sys2.topCents(), 5800.0, EPS);
    for (size_t i : { 2u, 3u }) {
        const StaffType::MeloFrameView& other = viewOn(score, systems[i]);
        EXPECT_EQ(other.bands.size(), 2u) << "system " << i + 1;
        EXPECT_EQ(other.omittedPeriodCount, 3) << "system " << i + 1;
    }
    // Undo restores the three-omitted-period view on system 2.
    score->undoRedo(true, nullptr);
    score->doLayout();
    EXPECT_EQ(viewOn(score, measureSystems(score)[1]).omittedPeriodCount, 3);
    delete score;
}

// Owner ruling 3b (2026-08-18, keyboard precedent): on a banded system every
// barline form runs continuously from the top band to the bottom band —
// through the gap — with repeat dots at each band's middle rows; the tips of
// a repeat sit at the stack's ends. Elision off: today's single span, no
// band dot rows.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8BarlinesRunThroughTheGapWithDotsInEachBand)
{
    MasterScore* score = ScoreRW::readScore(TWO_HAND);
    ASSERT_TRUE(score);
    score->doLayout();
    // Give the last two bars of system 2 a double and an end-repeat barline.
    Measure* m3 = measureNo(score, 3);
    Measure* m4 = measureNo(score, 4);
    score->startCmd(TranslatableString::untranslatable("M8 test barlines"));
    m3->undoChangeProperty(Pid::REPEAT_END, true);
    m4->undoChangeProperty(Pid::REPEAT_START, true);
    score->endCmd();
    score->doLayout();
    score->startCmd(TranslatableString::untranslatable("M8 test double barline"));
    if (Segment* s = m4->findSegmentR(SegmentType::EndBarLine, m4->ticks())) {
        if (BarLine* bl = toBarLine(s->element(0))) {
            score->undoChangeBarLineType(bl, BarLineType::DOUBLE, true);
        }
    }
    score->endCmd();
    setElision(score, true);
    System* system2 = measureSystems(score)[1];
    const StaffType::MeloFrameView& v = viewOn(score, system2);
    ASSERT_EQ(v.bands.size(), 2u);
    const double ld = st(score)->lineDistance().val();
    int checked = 0;
    for (MeasureBase* mb : system2->measures()) {
        if (!mb->isMeasure()) {
            continue;
        }
        for (Segment* s = toMeasure(mb)->first(SegmentType::BarLineType); s; s = s->next(SegmentType::BarLineType)) {
            BarLine* bl = toBarLine(s->element(0));
            if (!bl) {
                continue;
            }
            const BarLine::LayoutData* data = bl->ldata();
            const double lineDistance = ld * bl->spatium();
            const double lw = score->style().styleS(Sid::staffLineWidth).val() * bl->spatium() * .5;
            // One continuous span over the whole stack (through the gap).
            EXPECT_NEAR(data->y1, -lw, 1e-6) << "barline type " << int(bl->barLineType());
            EXPECT_NEAR(data->y2, v.heightLd() * lineDistance + lw, 1e-6) << "barline type " << int(bl->barLineType());
            // Dot rows: one pair per band, inside that band, straddling its middle.
            ASSERT_EQ(data->meloBandDotRows.size(), 2u);
            for (size_t i = 0; i < 2; ++i) {
                const StaffType::MeloFrameBand& band = v.bands[v.bands.size() - 1 - i];   // top to bottom
                const double bandTop = band.yTopLd * lineDistance;
                const double bandBottom = (band.yTopLd + band.heightLd()) * lineDistance;
                EXPECT_GT(data->meloBandDotRows[i].y1, bandTop);
                EXPECT_LT(data->meloBandDotRows[i].y2, bandBottom);
                EXPECT_NEAR((data->meloBandDotRows[i].y1 + data->meloBandDotRows[i].y2) / 2.0,
                            (bandTop + bandBottom) / 2.0, 1e-6);
            }
            ++checked;
        }
    }
    EXPECT_GE(checked, 3);
    // With elision off: one span (today's y1..y2), no band dot rows.
    setElision(score, false);
    for (Segment* s = measureNo(score, 3)->first(SegmentType::BarLineType); s; s = s->next(SegmentType::BarLineType)) {
        if (BarLine* bl = toBarLine(s->element(0))) {
            EXPECT_TRUE(bl->ldata()->meloBandDotRows.empty());
        }
    }
    delete score;
}

// Owner ruling 3b: a brace joins the bands of a hollow stack at the system
// head (MuseScore's keyboard brace: the SMuFL brace glyph, x-magnified by the
// Bracket span rule, stretched to the stack); the header reserves its width;
// a whole stack has none.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8BraceJoinsTheBandsOfAHollowStack)
{
    MasterScore* score = ScoreRW::readScore(TWO_HAND);
    ASSERT_TRUE(score);
    score->doLayout();
    const String braceGlyph = score->engravingFont()->toString(SymId::brace);
    auto textsOf = [](const StaffLines* lines) {
        std::shared_ptr<BufferedPaintProvider> prv = std::make_shared<BufferedPaintProvider>();
        Painter p(prv, "m8");
        p.setViewport(RectF(0, 0, 4000, 4000));
        PaintOptions opt;
        lines->renderer()->drawItem(lines, &p, opt);
        p.endDraw();
        std::vector<String> out;
        std::function<void(const DrawData::Item&)> walk = [&](const DrawData::Item& item) {
            for (const DrawData::Data& d : item.datas) {
                for (const DrawText& t : d.texts) {
                    out.push_back(t.text);
                }
            }
            for (const DrawData::Item& c : item.chilren) {
                walk(c);
            }
        };
        walk(prv->drawData()->item);
        return out;
    };
    auto hasBrace = [&](const std::vector<String>& texts) {
        for (const String& t : texts) {
            if (t == braceGlyph) {
                return true;
            }
        }
        return false;
    };
    const double sp = score->style().spatium();
    const double dsp = score->style().defaultSpatium();
    // Whole stack: no brace, nothing reserved.
    {
        System* system2 = measureSystems(score)[1];
        const StaffType::MeloFrameView& v = viewOn(score, system2);
        EXPECT_EQ(st(score)->meloHeaderGeometry(sp, dsp, &v).braceWidth, 0.0);
        EXPECT_FALSE(hasBrace(textsOf(system2->firstMeasure()->staffLines(0))));
    }
    setElision(score, true);
    System* system2 = measureSystems(score)[1];
    const StaffType::MeloFrameView& v = viewOn(score, system2);
    ASSERT_EQ(v.bands.size(), 2u);
    const StaffType::MeloHeaderGeometry g = st(score)->meloHeaderGeometry(sp, dsp, &v);
    EXPECT_GT(g.braceWidth, 0.0);
    EXPECT_NEAR(g.braceMagX, 2 + 1.625, 1e-9);   // MuseScore's brace x-magnification for a two-staff span
    EXPECT_TRUE(hasBrace(textsOf(system2->firstMeasure()->staffLines(0))));
    EXPECT_FALSE(hasBrace(textsOf(system2->lastMeasure()->staffLines(0))));   // system head only
    // System 1 (whole stack under the first-system rule): no brace.
    EXPECT_FALSE(hasBrace(textsOf(measureSystems(score)[0]->firstMeasure()->staffLines(0))));
    delete score;
}

// Owner finding 1 (2026-08-18): a JiMStaff's height on a system is its frame
// view's height (bands + gaps), so systems keep the minimum system distance
// and a following staff keeps the staff distance — instead of collapsing to
// the skyline minimum as with the nominal one-period height.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8StaffHeightAndSystemDistanceUseTheFrame)
{
    for (bool elide : { false, true }) {
        MasterScore* score = ScoreRW::readScore(TWO_HAND);
        ASSERT_TRUE(score);
        score->doLayout();
        setElision(score, elide);
        const std::vector<System*> systems = measureSystems(score);
        ASSERT_EQ(systems.size(), 4u);
        const double ld = st(score)->lineDistance().val();
        const bool spread = score->style().styleB(Sid::enableVerticalSpread);
        const double minSystem = score->style().styleMM(spread ? Sid::minSystemSpread : Sid::minSystemDistance);
        for (size_t i = 0; i < systems.size(); ++i) {
            System* system = systems[i];
            const StaffType::MeloFrameView& v = viewOn(score, system);
            const double frameH = v.heightLd() * ld * score->staff(0)->spatium(system->firstMeasure()->tick());
            EXPECT_NEAR(system->staff(0)->bbox().height(), frameH, 1e-6) << "elide=" << elide << " system " << i + 1;
            EXPECT_NEAR(system->height(), system->staff(0)->bbox().bottom(), 1e-6);
            if (i + 1 < systems.size() && systems[i + 1]->page() == system->page()) {
                const double gap = systems[i + 1]->y() - (system->y() + system->height());
                EXPECT_GE(gap, minSystem - 1e-6) << "elide=" << elide << " between systems " << i + 1 << " and " << i + 2;
            }
        }
        delete score;
    }
    // Two staves: the second staff sits at least staffDistance below the
    // first staff's FRAME bottom (whole stack, hide-empty off).
    MasterScore* two = ScoreRW::readScore(TWO_STAVES);
    ASSERT_TRUE(two);
    two->doLayout();
    System* first = measureSystems(two)[0];
    const StaffType::MeloFrameView& v = viewOn(two, first);
    const double frameH = v.heightLd() * st(two)->lineDistance().val() * two->staff(0)->spatium(Fraction(0, 1));
    EXPECT_NEAR(first->staff(0)->bbox().height(), frameH, 1e-6);
    const double between = first->staff(1)->y() - (first->staff(0)->y() + first->staff(0)->bbox().height());
    EXPECT_GE(between, two->style().styleMM(Sid::staffDistance) - 1e-6);
    delete two;
}

// Owner finding 2 (2026-08-18): every "[PitchN]:" names the octave of the row
// it sits on — header labels of whole and banded stacks and the change
// indicator's terrain label — checked against the Kernel's per-period label
// for the row the text is drawn on.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8OctaveLabelsNameTheirRowEverywhere)
{
    struct Labeled {
        String text;
        double y;
    };
    auto labelsOf = [](const StaffLines* lines) {
        std::shared_ptr<BufferedPaintProvider> prv = std::make_shared<BufferedPaintProvider>();
        Painter p(prv, "m8");
        p.setViewport(RectF(0, 0, 4000, 4000));
        PaintOptions opt;
        lines->renderer()->drawItem(lines, &p, opt);
        p.endDraw();
        std::vector<Labeled> out;
        std::function<void(const DrawData::Item&)> walk = [&](const DrawData::Item& item) {
            for (const DrawData::Data& d : item.datas) {
                for (const DrawText& t : d.texts) {
                    // "<Letter><accidentals><octave>: ..." e.g. "C2: Do", "Eb-1:"
                    const size_t colon = t.text.indexOf(u':');
                    if (colon != muse::nidx && colon >= 2 && colon <= 5) {
                        const Char first = t.text.at(0);
                        const Char last = t.text.at(colon - 1);
                        if (first >= u'A' && first <= u'G' && last.isDigit()) {
                            out.push_back({ t.text.left(colon), t.rect.top() });
                        }
                    }
                }
            }
            for (const DrawData::Item& c : item.chilren) {
                walk(c);
            }
        };
        walk(prv->drawData()->item);
        return out;
    };
    // The nearest tonic row decides which period a label sits on; its text
    // must be the Kernel's label for that period.
    auto checkLabels = [&](Score* score, const StaffType* jst, const StaffLines* lines,
                           const StaffType::MeloFrameView& v, const char* what) {
        const double topY = lines->pos().y();
        const double ld = jst->lineDistance().val();
        const double periodCents = jst->meloPeriodCents();
        melo::PeriodicOrigins origins;
        ASSERT_TRUE(melo::periodicOrigins(jst->meloStateJson(), origins));
        const std::vector<Labeled> labels = labelsOf(lines);
        ASSERT_GE(labels.size(), 1u) << what;
        for (const Labeled& l : labels) {
            const double yLd = (l.y - topY) / (ld * lines->spatium());
            const double cents = v.centsFromYLd(yLd);
            const int k = int(std::lround((cents - origins.tonicCentsAboveExtentLower) / periodCents));
            melo::TonicPitchLabel expected;
            ASSERT_TRUE(melo::tonicPitchLabelInPeriod(jst->meloStateJson(), k, expected)) << what;
            EXPECT_EQ(l.text, expected.label) << what << " row period " << k;
        }
        UNUSED(score);
    };
    MasterScore* score = ScoreRW::readScore(TWO_HAND);
    ASSERT_TRUE(score);
    score->doLayout();
    // Whole stacks: the label is the Kernel's answer for the lowest drawn tonic row.
    for (System* system : measureSystems(score)) {
        const StaffLines* lines = system->firstMeasure()->staffLines(0);
        checkLabels(score, st(score), lines, viewOn(score, system), "whole stack");
        const std::vector<Labeled> labels = labelsOf(lines);
        ASSERT_EQ(labels.size(), 1u);
    }
    setElision(score, true);
    for (System* system : measureSystems(score)) {
        const StaffType::MeloFrameView& v = viewOn(score, system);
        const StaffLines* lines = system->firstMeasure()->staffLines(0);
        checkLabels(score, st(score), lines, v, "banded");
        EXPECT_EQ(labelsOf(lines).size(), v.bands.size());   // one label per band
    }
    delete score;
    // Change indicator terrain (M6/M7 gate: bar 2 moves to reference 53, La-mode):
    // the terrain's "[PitchN]:" is the Kernel label of ITS row's period.
    MasterScore* gate = ScoreRW::readScore(u"jimstaff_data/m7-gate.mscz");
    ASSERT_TRUE(gate);
    gate->doLayout();
    Measure* m2 = measureNo(gate, 2);
    ASSERT_TRUE(m2);
    const StaffType* changeSt = gate->staff(0)->staffType(m2->tick());
    ASSERT_TRUE(changeSt && changeSt->isMelo());
    const StaffType::MeloFrameView& gv = changeSt->meloFrameView(gate, 0, m2->system());
    ASSERT_FALSE(gv.empty());
    checkLabels(gate, changeSt, m2->staffLines(0), gv, "terrain");
    delete gate;
}

// Regression net for owner finding 5 (M8 gate 2026-08-18; sighting of
// 2026-08-17: "partial staves in which Do was not the tonic, the red staff
// line was not on Do, other lines at wrong heights relative to the crescent
// clef, which did not point at the Do line"). The sweep did not reproduce it;
// this pins the invariants a correct display has, on off-Do (La-mode,
// tonic-bounded) partial staves at 686/700/720 cents, the M7 gate terrain
// (La-mode section, reference 53), and a banded hollow stack:
//   (a) every solid red guide line is a Do row (cents = 0 mod period), every
//       Do row inside a drawn segment has one, and no dashed scaffold line
//       lies on a Do row — in every measure, head or not;
//   (b) at every system head, every segment draws every Do-to-Do crescent
//       period that intersects it; every crescent horn is a Do row and every
//       red line carries every adjoining horn (two for an interior Do in one
//       segment; more where multiple visible segments share that Do row);
//   (c) the Kernel's Do dot stack sits at 0 cents and its glyph is drawn on
//       the red line of every period drawn.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8DoRowsCarryRedLinesCrescentHornsAndDoDots)
{
    const Color RED(0xE0, 0x30, 0x30);
    struct HeadPaint {
        std::vector<double> redYs;                        // solid red polylines
        std::vector<std::pair<double, double> > horns;    // crescent top / bottom
        std::vector<double> closureYs;                    // clipped-crescent horizontal closures
        std::vector<PointF> glyphs;                       // single-codepoint (music-font) glyph origins
        int strokedCrescentSeams = 0;                     // accidental straight strokes inside crescent outlines
    };
    auto paintOf = [&](const StaffLines* lines) {
        std::shared_ptr<BufferedPaintProvider> prv = std::make_shared<BufferedPaintProvider>();
        Painter p(prv, "m8");
        p.setViewport(RectF(0, 0, 4000, 4000));
        PaintOptions opt;
        lines->renderer()->drawItem(lines, &p, opt);
        p.endDraw();
        HeadPaint out;
        const DrawDataPtr dd = prv->drawData();
        std::function<void(const DrawData::Item&)> walk = [&](const DrawData::Item& item) {
            for (const DrawData::Data& d : item.datas) {
                const DrawData::State& state = dd->states.at(d.state);
                for (const DrawPolygon& poly : d.polygons) {
                    if (poly.mode == PolygonMode::Polyline && poly.polygon.size() == 2
                        && state.pen.style() == PenStyle::SolidLine && state.pen.color() == RED
                        && std::abs(poly.polygon[0].y() - poly.polygon[1].y()) < EPS) {
                        out.redYs.push_back(poly.polygon[0].y());
                    } else if (poly.mode == PolygonMode::Polyline && poly.polygon.size() == 2
                               && state.pen.style() == PenStyle::SolidLine
                               && state.pen.color() == Color::BLACK
                               && state.pen.capStyle() == PenCapStyle::FlatCap
                               && std::abs(state.pen.widthF() - lines->lw() * 1.5) < EPS
                               && std::abs(poly.polygon[0].y() - poly.polygon[1].y()) < EPS) {
                        out.closureYs.push_back(poly.polygon[0].y());
                    }
                }
                for (const DrawPath& path : d.paths) {
                    bool hasCurve = false;
                    for (size_t i = 0; i < path.path.elementCount(); ++i) {
                        const PainterPath::Element element = path.path.elementAt(i);
                        hasCurve = hasCurve || element.type == PainterPath::ElementType::CurveToElement;
                    }
                    if (hasCurve && path.mode != DrawMode::Fill) {
                        const RectF r = path.path.boundingRect();
                        out.horns.push_back({ r.top(), r.bottom() });
                        for (size_t i = 1; i < path.path.elementCount(); ++i) {
                            const PainterPath::Element element = path.path.elementAt(i);
                            out.strokedCrescentSeams
                                += element.type == PainterPath::ElementType::LineToElement ? 1 : 0;
                        }
                    }
                }
                for (const DrawText& t : d.texts) {
                    if (t.mode == DrawText::Point && t.text.size() == 1) {
                        out.glyphs.push_back(t.rect.topLeft());
                    }
                }
            }
            for (const DrawData::Item& c : item.chilren) {
                walk(c);
            }
        };
        walk(dd->item);
        return out;
    };
    auto isDoRow = [](double cents, double period, double origin) {
        return std::abs(cents - (origin + std::round((cents - origin) / period) * period)) < 1e-3;
    };
    // (c) first half: the Kernel's dot labelled "Do" (its own canonical-solfa
    // name; the lattice is centred on Re, so Do's generator coordinate is the
    // Kernel's to know) sits at 0 cents in both the labelled and the plain
    // dot-stack views.
    auto doGlyph = [&](Score* score, const StaffType* jst, SymId& sym, double& centroidDy) {
        std::vector<melo::LabeledDotStack> labelStacks;
        ASSERT_TRUE(melo::scaleDotLabels(jst->meloStateJson(), labelStacks));
        int doMembers = 0;
        int doNGen = 0;
        for (const melo::LabeledDotStack& s : labelStacks) {
            for (const melo::LabeledDotMember& member : s.members) {
                if (member.label == u"Do") {
                    ++doMembers;
                    doNGen = member.nGen;
                    EXPECT_NEAR(s.cents, 0.0, 1e-9) << "the Do dot is not at 0 cents";
                }
            }
        }
        ASSERT_EQ(doMembers, 1);
        std::vector<melo::ScaleDotStack> stacks;
        ASSERT_TRUE(melo::scaleDots(jst->meloStateJson(), stacks));
        int doStacks = 0;
        for (const melo::ScaleDotStack& s : stacks) {
            for (int nGen : s.frontToBack) {
                if (nGen == doNGen) {
                    ++doStacks;
                    EXPECT_NEAR(s.cents, 0.0, 1e-9) << "Do dot stack is not at 0 cents";
                }
            }
        }
        EXPECT_EQ(doStacks, 1);
        muse::String token;
        ASSERT_TRUE(melo::noteheadToken(jst->meloStateJson(), doNGen, token));
        sym = SymId::noteheadHalf;
        if (token == u"triangle-vertex-up") {
            sym = SymId::noteheadTriangleUpBlack;
        } else if (token == u"triangle-vertex-down") {
            sym = SymId::noteheadTriangleDownBlack;
        } else if (token == u"square-vertex-up") {
            sym = SymId::noteheadDiamondBlack;
        } else if (token == u"square-edge-up") {
            sym = SymId::noteheadSquareBlack;
        }
        const RectF gb = score->engravingFont()->bbox(sym, 1.0);
        centroidDy = 0.0;
        if (sym == SymId::noteheadTriangleUpBlack) {
            centroidDy = -gb.height() / 6.0;
        } else if (sym == SymId::noteheadTriangleDownBlack) {
            centroidDy = gb.height() / 6.0;
        }
    };
    // Every measure of every system: (a); system heads additionally (b), (c).
    auto checkScore = [&](Score* score, const char* what, bool* sawPartial) {
        for (System* system : measureSystems(score)) {
            for (MeasureBase* mb : system->measures()) {
                if (!mb->isMeasure()) {
                    continue;
                }
                Measure* m = toMeasure(mb);
                const StaffType* jst = score->staff(0)->staffType(m->tick());
                ASSERT_TRUE(jst && jst->isMelo()) << what;
                const StaffType::MeloFrameView& v = jst->meloFrameView(score, 0, system);
                ASSERT_FALSE(v.empty()) << what;
                const double period = jst->meloPeriodCents();
                melo::PeriodicOrigins origins;
                ASSERT_TRUE(melo::periodicOrigins(jst->meloStateJson(), origins)) << what;
                std::vector<melo::JiLine> jiLines;
                ASSERT_TRUE(melo::jiLines(jst->meloStateJson(), jiLines)) << what;
                const StaffLines* lines = m->staffLines(0);
                ASSERT_TRUE(lines) << what;
                const double topY = lines->pos().y();
                const double ldSp = jst->lineDistance().val() * lines->spatium();
                auto centsOfY = [&](double y) { return v.centsFromYLd((y - topY) / ldSp); };
                auto hasGuideAt = [&](double cents) {
                    return std::any_of(lines->meloGuideLines().begin(), lines->meloGuideLines().end(),
                                       [&](const StaffLines::MeloGuideLine& g) {
                        return std::abs(centsOfY(g.line.y1()) - cents) < 1e-6;
                    });
                };
                // (a) red = Do row, dashed != Do row; one red line per Do row in a segment.
                int red = 0;
                std::vector<double> expectedDoRows;
                std::vector<double> redYs;
                for (const StaffLines::MeloGuideLine& g : lines->meloGuideLines()) {
                    const double cents = centsOfY(g.line.y1());
                    if (!g.dashed && g.colorStyle == Sid::meloDoLineColor) {
                        ++red;
                        redYs.push_back(g.line.y1());
                        EXPECT_TRUE(isDoRow(cents, period, origins.doCentsAboveExtentLower))
                            << what << " red line at " << cents << " cents";
                    } else {
                        EXPECT_FALSE(isDoRow(cents, period, origins.doCentsAboveExtentLower))
                            << what << " scaffold line on a Do row at " << cents;
                    }
                }
                for (const StaffType::MeloFrameBand& band : v.bands) {
                    for (const StaffType::MeloSegment& seg : band.segments) {
                        if (!seg.whole) {
                            *sawPartial = true;
                        }
                        const double basePeriod = origins.doCentsAboveExtentLower
                                                  + std::floor((seg.lowerCents - origins.doCentsAboveExtentLower)
                                                               / period) * period;
                        for (double p = basePeriod; p < seg.upperCents; p += period) {
                            for (const melo::JiLine& ji : jiLines) {
                                const double c = p + ji.cents;
                                if (ji.visible && c >= seg.lowerCents - 1e-6 && c <= seg.upperCents + 1e-6) {
                                    EXPECT_TRUE(hasGuideAt(c))
                                        << what << " visible Xx row has no staff line at " << c << " cents";
                                }
                            }
                        }
                        const double first = origins.doCentsAboveExtentLower
                                             + std::ceil((seg.lowerCents - origins.doCentsAboveExtentLower - 1e-6)
                                                         / period) * period;
                        for (double b = first; b <= seg.upperCents + 1e-6; b += period) {
                            if (std::none_of(expectedDoRows.begin(), expectedDoRows.end(),
                                             [&](double existing) { return std::abs(existing - b) < 1e-6; })) {
                                expectedDoRows.push_back(b);
                            }
                        }
                    }
                }
                EXPECT_EQ(red, int(expectedDoRows.size())) << what << " tick " << m->tick().ticks();
                if (system->firstMeasure() != m) {
                    continue;
                }
                // (b) horns are Do rows; every red line has a horn on it.
                const HeadPaint paint = paintOf(lines);
                EXPECT_EQ(paint.redYs.size(), redYs.size()) << what;
                EXPECT_EQ(paint.strokedCrescentSeams, 0)
                    << what << " complete crescents must not stroke their fill-path closure";
                std::vector<std::pair<double, double> > expectedHorns;
                std::vector<double> expectedClosureYs;
                for (const StaffType::MeloFrameBand& band : v.bands) {
                    for (const StaffType::MeloSegment& segment : band.segments) {
                        const double segmentTopY
                            = topY + v.yLdFromCents(segment.upperCents) * ldSp;
                        double periodFloor = origins.doCentsAboveExtentLower
                                             + std::floor((segment.lowerCents
                                                           - origins.doCentsAboveExtentLower)
                                                          / period + 1e-6) * period;
                        for (; periodFloor < segment.upperCents - 1e-6; periodFloor += period) {
                            const double periodTopY
                                = segmentTopY + (segment.upperCents - (periodFloor + period))
                                  / StaffType::MELO_CENTS_PER_LINE_DISTANCE * ldSp;
                            expectedHorns.push_back({ periodTopY,
                                                      periodTopY + period
                                                      / StaffType::MELO_CENTS_PER_LINE_DISTANCE * ldSp });
                            const double periodCeiling = periodFloor + period;
                            if (segment.upperCents > periodFloor + 1e-6
                                && segment.upperCents < periodCeiling - 1e-6) {
                                expectedClosureYs.push_back(segmentTopY);
                            }
                            if (segment.lowerCents > periodFloor + 1e-6
                                && segment.lowerCents < periodCeiling - 1e-6) {
                                expectedClosureYs.push_back(topY + v.yLdFromCents(segment.lowerCents) * ldSp);
                            }
                        }
                    }
                }
                EXPECT_EQ(paint.horns.size(), expectedHorns.size())
                    << what << " every crescent period intersecting each segment";
                std::vector<std::pair<double, double> > actualHorns = paint.horns;
                std::sort(expectedHorns.begin(), expectedHorns.end());
                std::sort(actualHorns.begin(), actualHorns.end());
                for (size_t i = 0; i < std::min(actualHorns.size(), expectedHorns.size()); ++i) {
                    EXPECT_NEAR(actualHorns[i].first, expectedHorns[i].first, 1e-6)
                        << what << " crescent " << i << " upper Do horn";
                    EXPECT_NEAR(actualHorns[i].second, expectedHorns[i].second, 1e-6)
                        << what << " crescent " << i << " lower Do horn";
                }
                std::vector<double> actualClosureYs = paint.closureYs;
                std::sort(expectedClosureYs.begin(), expectedClosureYs.end());
                std::sort(actualClosureYs.begin(), actualClosureYs.end());
                ASSERT_EQ(actualClosureYs.size(), expectedClosureYs.size())
                    << what << " every clipped crescent edge must be closed";
                for (size_t i = 0; i < expectedClosureYs.size(); ++i) {
                    EXPECT_NEAR(actualClosureYs[i], expectedClosureYs[i], 1e-6)
                        << what << " clipped crescent closure " << i;
                }
                for (double y : redYs) {
                    const double redCents = centsOfY(y);
                    int expectedHornsOnLine = 0;
                    for (const auto& h : expectedHorns) {
                        expectedHornsOnLine += std::abs(h.first - y) < 1e-6 ? 1 : 0;
                        expectedHornsOnLine += std::abs(h.second - y) < 1e-6 ? 1 : 0;
                    }
                    int hornsOnLine = 0;
                    for (const auto& h : paint.horns) {
                        hornsOnLine += std::abs(h.first - y) < 1e-6 ? 1 : 0;
                        hornsOnLine += std::abs(h.second - y) < 1e-6 ? 1 : 0;
                    }
                    EXPECT_EQ(hornsOnLine, expectedHornsOnLine)
                        << what << " Do line must carry every adjoining crescent horn at " << redCents << " cents";
                }
                // (c) the Do glyph sits on every drawn red line.
                SymId doSym = SymId::noSym;
                double doDy = 0.0;
                doGlyph(score, jst, doSym, doDy);
                // The glyph's recorded origin is the draw call's point (the
                // header draws at mag 1); the drawn codepoint is the engraving
                // font's business (a fallback font may substitute an alternate),
                // so the match is by position: in the dot column, on the row.
                for (double y : redYs) {
                    bool glyphOnLine = false;
                    for (const PointF& g : paint.glyphs) {
                        glyphOnLine = glyphOnLine
                                      || (g.x() < lines->pos().x() && std::abs(g.y() - (y + doDy)) < 1e-6);
                    }
                    EXPECT_TRUE(glyphOnLine) << what << " no Do dot on the Do line at " << centsOfY(y) << " cents";
                }
            }
        }
    };
    // Off-Do partial staves (La-mode, tonic-bounded, with a mid-piece
    // change back to Do-mode) at three generator widths.
    for (double g : { 686.0, 700.0, 720.0 }) {
        io::File f(ScoreRW::rootPath() + u"/jimstaff_data/m5-syshead.mscx");
        ASSERT_TRUE(f.open(io::IODevice::ReadOnly));
        String text = String::fromUtf8(f.readAll());
        text.replace(u"\"generator_cents\":700.0", u"\"generator_cents\":" + String::number(g, 1));
        const String dir = ScoreRW::rootPath() + u"/../../../build.release/jims-m8-scratch";
        io::Dir::mkpath(dir);
        const String path = dir + u"/m8-do-row-net-" + String::number(g, 0) + u".mscx";
        io::File out(path);
        ASSERT_TRUE(out.open(io::IODevice::WriteOnly));
        out.write(text.toUtf8());
        out.close();
        MasterScore* score = ScoreRW::readScore(path, true);
        ASSERT_TRUE(score) << g;
        score->doLayout();
        double generator = 0.0, period = 0.0;
        ASSERT_TRUE(melo::staffMetrics(st(score)->meloStateJson(), generator, period));
        EXPECT_NEAR(generator, g, 1e-9);
        double tonic = 0.0;
        ASSERT_TRUE(melo::tonicCentsAboveDo(st(score)->meloStateJson(), tonic));
        EXPECT_GT(tonic, 1.0) << "the fixture must be off-Do (La-mode)";
        bool sawPartial = false;
        const std::string what = "syshead@" + std::to_string(int(g));
        checkScore(score, what.c_str(), &sawPartial);
        EXPECT_TRUE(sawPartial) << what << " must exercise partial staves";
        delete score;
    }
    // The M7 gate terrain (bar 2: La-mode, reference 53).
    {
        MasterScore* gate = ScoreRW::readScore(u"jimstaff_data/m7-gate.mscz");
        ASSERT_TRUE(gate);
        gate->doLayout();
        bool sawPartial = false;
        checkScore(gate, "m7-gate", &sawPartial);
        delete gate;
    }
    // A banded hollow stack: Do rows in every band.
    {
        MasterScore* score = ScoreRW::readScore(TWO_HAND);
        ASSERT_TRUE(score);
        score->doLayout();
        setElision(score, true);
        bool banded = false;
        for (System* system : measureSystems(score)) {
            banded = banded || viewOn(score, system).banded;
        }
        ASSERT_TRUE(banded);
        bool sawPartial = false;
        checkScore(score, "two-hand banded", &sawPartial);
        delete score;
    }
}

// Regression for the two-staff dynamic-tuning video (owner finding
// 2026-08-30): every visible JiMStaff segment has an explicit top and bottom
// boundary throughout tuning motion, and a clipped crescent's closure belongs
// only to the staff-local occurrence whose period is actually cut.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8PartialStaffEdgesPreserveRealRatioLinesAndLocalCrescentClosures)
{
    for (const String& fixture :
         { TWO_STAVES, String::fromUtf8(engraving_tests_DATA_ROOT) + u"/jimstaff_data/empty-half-staves-14.mscx" }) {
        MasterScore* score = ScoreRW::readScore(fixture, fixture != TWO_STAVES);
        ASSERT_TRUE(score);
        score->doLayout();
        ASSERT_EQ(score->nstaves(), fixture == TWO_STAVES ? 2u : 14u);

        for (staff_idx_t staffIdx = 0; staffIdx < score->nstaves(); ++staffIdx) {
            System* system = measureSystems(score).front();
            Measure* measure = system->firstMeasure();
            const StaffType* jst = st(score, staffIdx);
            const StaffType::MeloFrameView& view = viewOn(score, system, staffIdx);
            const StaffLines* lines = measure->staffLines(staffIdx);
            ASSERT_TRUE(jst && lines);
            ASSERT_FALSE(view.empty());
            const double topY = lines->pos().y();
            const double ldSp = jst->lineDistance().val() * lines->spatium();
            auto centsOfY = [&](double y) { return view.centsFromYLd((y - topY) / ldSp); };
            auto hasGuideAt = [&](double cents) {
                return std::any_of(lines->meloGuideLines().begin(), lines->meloGuideLines().end(),
                                   [&](const StaffLines::MeloGuideLine& guide) {
                    return std::abs(centsOfY(guide.line.y1()) - cents) < 1e-6;
                });
            };
            auto hasBlackGuideAt = [&](double cents) {
                return std::any_of(lines->meloGuideLines().begin(), lines->meloGuideLines().end(),
                                   [&](const StaffLines::MeloGuideLine& guide) {
                    return lines->style().value(guide.colorStyle).value<Color>() == Color::BLACK
                           && std::abs(centsOfY(guide.line.y1()) - cents) < 1e-6;
                });
            };
            std::vector<double> expectedClosures;
            const double periodCents = jst->meloPeriodCents();
            melo::PeriodicOrigins origins;
            ASSERT_TRUE(melo::periodicOrigins(jst->meloStateJson(), origins));
            std::vector<melo::JiLine> ratios;
            ASSERT_TRUE(melo::jiLines(jst->meloStateJson(), ratios));
            auto isRatioRow = [&](double cents) {
                const double relative = cents - origins.doCentsAboveExtentLower;
                if (std::abs(relative - std::round(relative / periodCents) * periodCents) < 1e-6) {
                    return true;
                }
                return std::any_of(ratios.begin(), ratios.end(), [&](const melo::JiLine& ratio) {
                    const double offset = relative - ratio.cents;
                    return std::abs(offset - std::round(offset / periodCents) * periodCents) < 1e-6;
                });
            };
            for (const StaffType::MeloFrameBand& band : view.bands) {
                for (const StaffType::MeloSegment& segment : band.segments) {
                    EXPECT_EQ(hasGuideAt(segment.lowerCents), isRatioRow(segment.lowerCents))
                        << "staff " << staffIdx << " incorrect bottom-edge ratio line at " << segment.lowerCents;
                    EXPECT_EQ(hasGuideAt(segment.upperCents), isRatioRow(segment.upperCents))
                        << "staff " << staffIdx << " incorrect top-edge ratio line at " << segment.upperCents;
                    EXPECT_FALSE(hasBlackGuideAt(segment.lowerCents))
                        << "staff " << staffIdx << " synthesized a non-musical black bottom boundary";
                    EXPECT_FALSE(hasBlackGuideAt(segment.upperCents))
                        << "staff " << staffIdx << " synthesized a non-musical black top boundary";
                    double periodFloor = origins.doCentsAboveExtentLower
                                         + std::floor((segment.lowerCents - origins.doCentsAboveExtentLower)
                                                      / periodCents + 1e-6) * periodCents;
                    for (; periodFloor < segment.upperCents - 1e-6; periodFloor += periodCents) {
                        const double periodCeiling = periodFloor + periodCents;
                        if (segment.upperCents > periodFloor + 1e-6
                            && segment.upperCents < periodCeiling - 1e-6) {
                            expectedClosures.push_back(segment.upperCents);
                        }
                        if (segment.lowerCents > periodFloor + 1e-6
                            && segment.lowerCents < periodCeiling - 1e-6) {
                            expectedClosures.push_back(segment.lowerCents);
                        }
                    }
                }
            }

            std::shared_ptr<BufferedPaintProvider> provider = std::make_shared<BufferedPaintProvider>();
            Painter painter(provider, "m8-staff-local-crescent");
            painter.setViewport(RectF(0, 0, 4000, 4000));
            PaintOptions options;
            lines->renderer()->drawItem(lines, &painter, options);
            painter.endDraw();
            std::vector<double> actualClosures;
            const DrawDataPtr drawData = provider->drawData();
            std::function<void(const DrawData::Item&)> walk = [&](const DrawData::Item& item) {
                for (const DrawData::Data& data : item.datas) {
                    const DrawData::State& state = drawData->states.at(data.state);
                    for (const DrawPolygon& poly : data.polygons) {
                        if (poly.mode == PolygonMode::Polyline && poly.polygon.size() == 2
                            && state.pen.style() == PenStyle::SolidLine
                            && state.pen.color() == Color::BLACK
                            && state.pen.capStyle() == PenCapStyle::FlatCap
                            && std::abs(state.pen.widthF() - lines->lw() * 1.5) < EPS
                            && std::abs(poly.polygon[0].y() - poly.polygon[1].y()) < EPS) {
                            actualClosures.push_back(centsOfY(poly.polygon[0].y()));
                        }
                    }
                }
                for (const DrawData::Item& child : item.chilren) {
                    walk(child);
                }
            };
            walk(drawData->item);
            std::sort(expectedClosures.begin(), expectedClosures.end());
            std::sort(actualClosures.begin(), actualClosures.end());
            ASSERT_EQ(actualClosures.size(), expectedClosures.size())
                << "staff " << staffIdx << " closure geometry leaked across crescent occurrences";
            for (size_t i = 0; i < expectedClosures.size(); ++i) {
                EXPECT_NEAR(actualClosures[i], expectedClosures[i], 1e-6)
                    << "staff " << staffIdx << " closure " << i << " is not staff-local";
            }
        }
        delete score;
    }
}

// A fixed ratio-line cut is not a moving scale-dot cut. At 12-TET the So
// dot sits 1.955 cents below its pure 3/2 boundary; at other tunings it moves
// farther while the boundary remains fixed. The boundary dot and its label
// must therefore survive whenever their painted glyph intersects the staff.
// The tonic pitch label belongs in the open lane to the right of Do's dot.
TEST_F(Engraving_MeloStaffM8BandElisionTests, fixedRatioEdgeKeepsSoDotAndLabelAndPlacesTonicPitchLabelRightOfDo)
{
    MasterScore* score = ScoreRW::readScore(SINGLE_OCTAVE);
    ASSERT_TRUE(score);
    StaffType* type = mutSt(score);
    ASSERT_TRUE(type && type->isMelo());
    type->setMeloStateJson(
        u"{\"scale\":[\"M2\",\"m2\",\"M2\",\"M2\",\"M2\",\"m2\",\"M2\"],"
        u"\"collection_rotation\":0,\"mode_rotation\":0,\"generator_cents\":700.0,"
        u"\"period_cents\":1200.0,\"embedding\":{\"large_steps\":5,\"small_steps\":2},"
        u"\"extent\":{\"lower\":{\"nPer\":-2,\"nGen\":-1},"
        u"\"upper\":{\"nPer\":-1,\"nGen\":-2}},"
        u"\"reference\":{\"reference-pitch\":{\"key_number\":62}},"
        u"\"tonic_ambit\":\"tonic-bounded\"}");
    type->setMeloRatioLineExtentJson(
        u"{\"lower\":{\"period\":-1,\"ratio\":\"3/2\"},"
        u"\"upper\":{\"period\":0,\"ratio\":\"1/1\"}}");
    for (Measure* measure = score->firstMeasure(); measure; measure = measure->nextMeasure()) {
        for (Chord* chord : chordsOf(measure)) {
            for (Note* note : chord->notes()) {
                note->setMeloPitch(-2, -1);
            }
        }
    }
    score->setLayoutAll();
    score->doLayout();

    System* system = measureSystems(score).front();
    const StaffType::MeloFrameView& view = viewOn(score, system);
    ASSERT_EQ(view.bands.size(), 1u);
    EXPECT_NEAR(view.bottomCents(), 1.955000865387433, 1e-6);
    EXPECT_NEAR(view.topCents(), 500.0, 1e-6);
    const StaffLines* lines = system->firstMeasure()->staffLines(0);
    ASSERT_TRUE(lines);

    std::shared_ptr<BufferedPaintProvider> provider = std::make_shared<BufferedPaintProvider>();
    Painter painter(provider, "fixed-ratio-edge-header");
    painter.setViewport(RectF(0, 0, 4000, 4000));
    PaintOptions options;
    lines->renderer()->drawItem(lines, &painter, options);
    painter.endDraw();

    bool sawSo = false;
    bool sawTonicPitch = false;
    double tonicPitchX = 0.0;
    double tonicPitchRight = 0.0;
    const DrawDataPtr drawData = provider->drawData();
    std::function<void(const DrawData::Item&)> walk = [&](const DrawData::Item& item) {
        for (const DrawData::Data& data : item.datas) {
            for (const DrawText& text : data.texts) {
                sawSo = sawSo || text.text.contains(u"So");
                if (text.text.contains(u"C2:")) {
                    sawTonicPitch = true;
                    tonicPitchX = text.rect.left();
                    tonicPitchRight = text.rect.right();
                }
            }
        }
        for (const DrawData::Item& child : item.chilren) {
            walk(child);
        }
    };
    walk(drawData->item);
    EXPECT_TRUE(sawSo) << "the So dot intersecting the fixed 3/2 edge lost its label";
    ASSERT_TRUE(sawTonicPitch);

    const StaffType::MeloHeaderGeometry geometry
        = type->meloHeaderGeometry(lines->spatium(), score->style().defaultSpatium(), &view);
    const double clefRight = lines->pos().x() - 0.3 * lines->spatium();
    const double clefLeft = clefRight - geometry.clefRx;
    EXPECT_NEAR(geometry.rightLabelBand, 0.0, 1e-6)
        << "the tonic pitch label must not displace the scale-dot stack";
    const double dotCenterX = clefLeft - geometry.rightLabelBand
                              - 2.0 * geometry.indicatorW + geometry.indicatorW;
    EXPECT_NEAR(dotCenterX, clefLeft - geometry.indicatorW, 1e-6)
        << "the scale-dot stack moved away from its established clef geometry";
    EXPECT_GT(tonicPitchX, dotCenterX)
        << "the tonic pitch label must sit to the right of Do's scale dot";
    EXPECT_GT(tonicPitchX, clefLeft)
        << "the tonic pitch label must nestle inside the crescent's horizontal span";
    EXPECT_LT(tonicPitchRight, clefRight)
        << "the tonic pitch label must fit before the crescent's Do-line point";
    delete score;
}

// Owner ruling 2026-08-19 (seen on the two-part gate score): a Do->La mode
// change was drawn from the staff's LOWEST Do-line down to a La below the
// staff. The indicator must anchor on the Do-line that keeps the whole
// indicator on the staff — the lowest such Do-line — and, when none does,
// on the one that overflows least (extending the staff is a follow-up).
TEST_F(Engraving_MeloStaffM8BandElisionTests, changeIndicatorAnchorsOnTheDoLineThatKeepsItOnTheStaff)
{
    const double P = 1200.0;
    auto whole = [&](double lower, double upper) {
        StaffType::MeloFrameView v;
        StaffType::MeloFrameBand band;
        for (double b = lower; b < upper - 1e-6; b += P) {
            band.segments.push_back({ b, b + P, true });
        }
        band.lowerCents = lower;
        band.upperCents = upper;
        v.bands.push_back(band);
        return v;
    };
    auto point = [](double ordinate, int periodOffset) {
        melo::ChangePoint p;
        p.ordinate = ordinate;
        p.periodOffset = periodOffset;
        return p;
    };
    // Do -> La ("fewest degrees" is down: La one period offset below at 0.75).
    melo::ChangeIndicator doToLa;
    doToLa.kinds = { u"mode" };
    doToLa.tonicIndicators = { point(0.0, 0), point(0.75, -1) };
    melo::ChangeArrow down;
    down.kind = u"mode";
    down.from = point(0.0, 0);
    down.to = point(0.75, -1);
    down.up = false;
    doToLa.arrows = { down };
    // One-period staff [0,1200]: only the UPPER Do-line keeps La (900) on the staff.
    EXPECT_DOUBLE_EQ(melo::changeAnchorPeriodCents(whole(0, 1200), doToLa, P), 1200.0);
    // Two-period staff [0,2400]: both 1200 (La at 900) and 2400 (La at 2100)
    // fit; with no notes to sit beside, the highest wins (owner decision
    // 2026-09-14; the former lowest-wins was an implementer's tie-break).
    EXPECT_DOUBLE_EQ(melo::changeAnchorPeriodCents(whole(0, 2400), doToLa, P), 2400.0);
    // With notes low on the staff, the placement beside them wins.
    EXPECT_DOUBLE_EQ(melo::changeAnchorPeriodCents(whole(0, 2400), doToLa, P, 0.0, { 200.0, 400.0 }), 1200.0);
    // Do -> Re (up, inside the same period): both Do-lines fit; no notes, so the highest.
    melo::ChangeIndicator doToRe;
    doToRe.kinds = { u"mode" };
    doToRe.tonicIndicators = { point(0.0, 0), point(1.0 / 6.0, 0) };
    EXPECT_DOUBLE_EQ(melo::changeAnchorPeriodCents(whole(0, 2400), doToRe, P), 1200.0);
    // Nothing fits (a partial staff [300, 900] with Do -> La): least overflow wins.
    StaffType::MeloFrameView partial;
    StaffType::MeloFrameBand pb;
    pb.segments.push_back({ 300.0, 900.0, false });
    pb.lowerCents = 300.0;
    pb.upperCents = 900.0;
    partial.bands.push_back(pb);
    // No Do-line is inside: the upper anchor minimizes overflow to 300 cents.
    EXPECT_DOUBLE_EQ(melo::changeAnchorPeriodCents(partial, doToLa, P), 1200.0);
    // Banded (M8): [0,1200] and [3600,4800]; Do -> La fits in the low band at 1200
    // (La 900) and in the top band at 4800 (La 4500); no notes, so the top band's.
    StaffType::MeloFrameView banded = whole(0, 1200);
    StaffType::MeloFrameBand top;
    top.segments.push_back({ 3600.0, 4800.0, true });
    top.lowerCents = 3600.0;
    top.upperCents = 4800.0;
    banded.bands.push_back(top);
    banded.banded = true;
    EXPECT_DOUBLE_EQ(melo::changeAnchorPeriodCents(banded, doToLa, P), 4800.0);

    // Paint check on the accepted M5 piece: Do-mode -> La-mode at bar 2 on a
    // one-period staff. The new tonic's label must be the UPPER register
    // ("A4: La", not "A3: La") and every terrain text must lie on the staff.
    MasterScore* score = ScoreRW::readScore(u"jimstaff_data/m5-mode.mscx");
    ASSERT_TRUE(score);
    score->doLayout();
    Measure* m2 = measureNo(score, 2);
    ASSERT_TRUE(m2);
    const StaffType* changeSt = score->staff(0)->staffType(m2->tick());
    ASSERT_TRUE(changeSt && changeSt->isMelo());
    const StaffLines* lines = m2->staffLines(0);
    ASSERT_TRUE(lines);
    const StaffType::MeloFrameView& v = changeSt->meloFrameView(score, 0, m2->system());
    ASSERT_FALSE(v.empty());
    const double topY = lines->pos().y() + changeSt->meloYFromCents(v.topCents(), v) * lines->spatium();
    const double bottomY = lines->pos().y() + changeSt->meloYFromCents(v.bottomCents(), v) * lines->spatium();
    std::shared_ptr<BufferedPaintProvider> prv = std::make_shared<BufferedPaintProvider>();
    Painter p(prv, "m8");
    p.setViewport(RectF(0, 0, 4000, 4000));
    PaintOptions opt;
    lines->renderer()->drawItem(lines, &p, opt);
    p.endDraw();
    bool sawA4La = false;
    bool sawA3La = false;
    std::function<void(const DrawData::Item&)> walk = [&](const DrawData::Item& item) {
        for (const DrawData::Data& d : item.datas) {
            for (const DrawText& t : d.texts) {
                if (t.text.contains(u"A4: La")) {
                    sawA4La = true;
                    EXPECT_GE(t.rect.top(), topY - lines->spatium()) << "label above the staff";
                    EXPECT_LE(t.rect.top(), bottomY + lines->spatium()) << "label below the staff";
                }
                if (t.text.contains(u"A3: La")) {
                    sawA3La = true;
                }
            }
        }
        for (const DrawData::Item& c : item.chilren) {
            walk(c);
        }
    };
    walk(prv->drawData()->item);
    EXPECT_TRUE(sawA4La) << "the new tonic La is labelled in the register that keeps the indicator on the staff";
    EXPECT_FALSE(sawA3La) << "the indicator no longer hangs below the staff";
    delete score;
}

// MuseScore's stock hide-empty-staves still hides a fully empty MeloPresto Staff on
// a system, unchanged, whether or not elision is on.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8StockHideEmptyStavesStillHidesAnEmptyMeloStaff)
{
    MasterScore* score = ScoreRW::readScore(TWO_STAVES);
    ASSERT_TRUE(score);
    score->doLayout();
    ASSERT_EQ(score->nstaves(), 2u);
    for (bool elide : { false, true }) {
        score->style().set(Sid::meloElideEmptyOctaves, elide);
        score->style().set(Sid::hideEmptyStaves, true);
        score->setLayoutAll();
        score->doLayout();
        const std::vector<System*> systems = measureSystems(score);
        ASSERT_EQ(systems.size(), 4u) << elide;
        EXPECT_TRUE(systems[0]->staff(1)->show()) << elide;    // first system: dontHideStavesInFirstSystem
        for (size_t i = 1; i < systems.size(); ++i) {
            EXPECT_FALSE(systems[i]->staff(1)->show()) << "system " << i + 1 << " elide=" << elide;
            EXPECT_TRUE(systems[i]->staff(0)->show());
        }
    }
    delete score;
}

// .mscz round trip preserves the two styles and the staff-type override;
// an absent override reads as Auto; unknown values read as Auto.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8RoundTripPreservesSwitchesAndAbsentOverrideIsAuto)
{
    MasterScore* score = ScoreRW::readScore(TWO_HAND);
    ASSERT_TRUE(score);
    score->doLayout();
    EXPECT_EQ(st(score)->meloElideOctaves(), MeloElideOctaves::Auto);
    score->style().set(Sid::meloElideEmptyOctaves, true);
    score->style().set(Sid::meloShowAllOctavesInFirstSystem, false);
    mutSt(score)->setMeloElideOctaves(MeloElideOctaves::On);
    const String dir = ScoreRW::rootPath() + u"/../../../build.release/jims-m8-scratch";
    io::Dir::mkpath(dir);
    // A real .mscz container (MscSaver -> MscWriter zip), read back through
    // the ordinary reader.
    const String out = dir + u"/m8-roundtrip.mscz";
    io::File::remove(out);
    {
        io::File file(out);
        ASSERT_TRUE(file.open(io::IODevice::WriteOnly));
        MscWriter::Params params;
        params.device = &file;
        params.filePath = out;
        params.mode = MscIoMode::Zip;
        MscWriter writer(params);
        ASSERT_TRUE(writer.open());
        MscSaver saver(score->iocContext());
        ASSERT_TRUE(saver.writeMscz(score, writer, false));
        writer.close();
        file.close();
    }
    delete score;
    MasterScore* again = ScoreRW::readScore(out, true);
    ASSERT_TRUE(again);
    again->doLayout();
    EXPECT_TRUE(again->style().styleB(Sid::meloElideEmptyOctaves));
    EXPECT_FALSE(again->style().styleB(Sid::meloShowAllOctavesInFirstSystem));
    EXPECT_EQ(st(again)->meloElideOctaves(), MeloElideOctaves::On);
    // Off round-trips too.
    mutSt(again)->setMeloElideOctaves(MeloElideOctaves::Off);
    const String out2 = dir + u"/m8-roundtrip-off.mscx";
    ASSERT_TRUE(ScoreRW::saveScore(again, out2));
    delete again;
    MasterScore* third = ScoreRW::readScore(out2, true);
    ASSERT_TRUE(third);
    EXPECT_EQ(st(third)->meloElideOctaves(), MeloElideOctaves::Off);
    delete third;
    // Unknown value -> Auto (safe parsing, no invented behaviour).
    io::File f(out2);
    ASSERT_TRUE(f.open(io::IODevice::ReadOnly));
    String text = String::fromUtf8(f.readAll());
    f.close();
    ASSERT_TRUE(text.contains(u"<jimsElideOctaves>off</jimsElideOctaves>"));
    text.replace(u"<jimsElideOctaves>off</jimsElideOctaves>", u"<jimsElideOctaves>banana</jimsElideOctaves>");
    const String out3 = dir + u"/m8-roundtrip-unknown.mscx";
    io::File w(out3);
    ASSERT_TRUE(w.open(io::IODevice::WriteOnly));
    w.write(text.toUtf8());
    w.close();
    MasterScore* fourth = ScoreRW::readScore(out3, true);
    ASSERT_TRUE(fourth);
    EXPECT_EQ(st(fourth)->meloElideOctaves(), MeloElideOctaves::Auto);
    delete fourth;
}

TEST_F(Engraving_MeloStaffM8BandElisionTests, fixedRatioLineExtentRoundTripsPerStaffType)
{
    MasterScore* score = ScoreRW::readScore(TWO_HAND);
    ASSERT_TRUE(score);
    EXPECT_TRUE(st(score)->meloRatioLineExtentJson().isEmpty());
    const String extent
        = u"{\"lower\":{\"period\":-2,\"ratio\":\"3/2\"},"
          u"\"upper\":{\"period\":6,\"ratio\":\"3/2\"}}";
    mutSt(score)->setMeloRatioLineExtentJson(extent);
    const String out = ScoreRW::rootPath() + u"/../../../build.release/jims-m8-scratch/ratio-extent-roundtrip.mscx";
    ASSERT_TRUE(ScoreRW::saveScore(score, out));
    delete score;
    MasterScore* again = ScoreRW::readScore(out, true);
    ASSERT_TRUE(again);
    EXPECT_EQ(st(again)->meloRatioLineExtentJson(), extent);
    delete again;
}

// None of the three settings enters the Kernel state; the notes' identities
// and Kernel sounding pitches (what playback consumes) are unchanged.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8SettingsNeverEnterKernelStateAndPlaybackIdentityIsUnchanged)
{
    MasterScore* score = ScoreRW::readScore(TWO_HAND);
    ASSERT_TRUE(score);
    score->doLayout();
    const String stateBefore = st(score)->meloStateJson();
    struct Ident {
        int nPer;
        int nGen;
        int midi;
        double cents;
    };
    auto collect = [&]() {
        std::vector<Ident> out;
        for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
            for (Chord* c : chordsOf(m)) {
                for (Note* n : c->notes()) {
                    melo::SoundingPitch sp;
                    EXPECT_TRUE(melo::noteSoundingPitch(st(score)->meloStateJson(), n->meloNPer(), n->meloNGen(), sp));
                    out.push_back({ n->meloNPer(), n->meloNGen(), sp.midiKey, sp.centsOffset });
                }
            }
        }
        return out;
    };
    const std::vector<Ident> before = collect();
    ASSERT_EQ(before.size(), 32u);
    score->style().set(Sid::meloElideEmptyOctaves, true);
    score->style().set(Sid::meloShowAllOctavesInFirstSystem, false);
    mutSt(score)->setMeloElideOctaves(MeloElideOctaves::On);
    score->setLayoutAll();
    score->doLayout();
    EXPECT_EQ(st(score)->meloStateJson(), stateBefore);
    EXPECT_FALSE(stateBefore.contains(u"elide"));
    const std::vector<Ident> after = collect();
    ASSERT_EQ(after.size(), before.size());
    for (size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(after[i].nPer, before[i].nPer);
        EXPECT_EQ(after[i].nGen, before[i].nGen);
        EXPECT_EQ(after[i].midi, before[i].midi);
        EXPECT_NEAR(after[i].cents, before[i].cents, EPS);
    }
    delete score;
}
// Phase 4 (optional, screen-only): the "n octaves elided" gap indicator
// reuses the StaffVisibilityIndicator paint precedent (TDraw::draw(const
// IndicatorIcon*): drawn only when !isPrinting && showUnprintable) — it is
// present on screen for a banded system head, absent from every printing
// paint path (PNG/PDF/print/SVG all paint with isPrinting), absent when the
// score hides unprintables, and absent when elision is off. Its text comes
// from the Kernel's omitted-period count.
TEST_F(Engraving_MeloStaffM8BandElisionTests, m8GapIndicatorIsScreenOnlyAndNeverPrints)
{
    MasterScore* score = ScoreRW::readScore(TWO_HAND);
    ASSERT_TRUE(score);
    score->doLayout();
    struct Drawn {
        String text;
        double x;
    };
    auto drawnOf = [](const StaffLines* lines, bool printing) {
        std::shared_ptr<BufferedPaintProvider> prv = std::make_shared<BufferedPaintProvider>();
        Painter p(prv, "m8");
        p.setViewport(RectF(0, 0, 4000, 4000));
        PaintOptions opt;
        opt.isPrinting = printing;
        lines->renderer()->drawItem(lines, &p, opt);
        p.endDraw();
        std::vector<Drawn> out;
        std::function<void(const DrawData::Item&)> walk = [&](const DrawData::Item& item) {
            for (const DrawData::Data& d : item.datas) {
                for (const DrawText& t : d.texts) {
                    out.push_back({ t.text, t.rect.left() });
                }
            }
            for (const DrawData::Item& c : item.chilren) {
                walk(c);
            }
        };
        walk(prv->drawData()->item);
        return out;
    };
    auto textsOf = [&](const StaffLines* lines, bool printing) {
        std::vector<String> out;
        for (const Drawn& d : drawnOf(lines, printing)) {
            out.push_back(d.text);
        }
        return out;
    };
    auto hasIndicator = [](const std::vector<String>& texts) {
        for (const String& t : texts) {
            if (t.contains(u"hidden")) {
                return true;
            }
        }
        return false;
    };
    // Elision off: nothing to indicate.
    const StaffLines* offLines = measureSystems(score)[1]->firstMeasure()->staffLines(0);
    EXPECT_FALSE(hasIndicator(textsOf(offLines, false)));
    setElision(score, true);
    System* system2 = measureSystems(score)[1];
    ASSERT_EQ(viewOn(score, system2).bands.size(), 2u);
    const StaffLines* lines = system2->firstMeasure()->staffLines(0);
    const std::vector<String> screen = textsOf(lines, false);
    ASSERT_TRUE(hasIndicator(screen));
    bool sawCount = false;
    for (const String& t : screen) {
        if (t.contains(u"hidden")) {
            EXPECT_TRUE(t == u"3 empty octaves hidden");
            sawCount = true;
        }
    }
    EXPECT_TRUE(sawCount);
    // Placement (owner finding 2026-08-18): the text's left edge sits at the
    // right edge of the scale-dot column (no Split-mode right label stack in
    // this fixture), i.e. dotCenterX + indicatorW of the header geometry —
    // left of the crescent, never at the measure's start.
    {
        const StaffType::MeloFrameView& v = viewOn(score, system2);
        const StaffType::MeloHeaderGeometry g
            = st(score)->meloHeaderGeometry(lines->spatium(), score->style().defaultSpatium(), &v);
        const double sp = lines->spatium();
        const double clefRight = lines->pos().x() - 0.3 * sp;
        const double clefLeft = clefRight - g.clefRx;
        const double dotCenterX = clefLeft - g.rightLabelBand - 2.0 * g.indicatorW + g.indicatorW;
        const double expectedLeft = dotCenterX + g.indicatorW + g.rightLabelBand;
        bool sawPlacement = false;
        for (const Drawn& d : drawnOf(lines, false)) {
            if (d.text.contains(u"hidden")) {
                EXPECT_NEAR(d.x, expectedLeft, 1e-6);
                EXPECT_LT(d.x, lines->pos().x());   // left of the staff's first measure
                sawPlacement = true;
            }
        }
        EXPECT_TRUE(sawPlacement);
    }
    // Printing paths never see it ...
    EXPECT_FALSE(hasIndicator(textsOf(lines, true)));
    // ... nor does the screen when unprintables are hidden ...
    score->setShowUnprintable(false);
    EXPECT_FALSE(hasIndicator(textsOf(lines, false)));
    score->setShowUnprintable(true);
    // ... nor a non-head measure of the system.
    EXPECT_FALSE(hasIndicator(textsOf(system2->lastMeasure()->staffLines(0), false)));
    // System 1 (whole stack under the first-system rule): nothing elided, no text.
    EXPECT_FALSE(hasIndicator(textsOf(measureSystems(score)[0]->firstMeasure()->staffLines(0), false)));
    delete score;
}
} // namespace

TEST_F(Engraving_MeloStaffM8BandElisionTests, legacyTonicExtentSpellingIsNotAnAlias)
{
    StaffType st;
    st.setMelo(true);
    st.setMeloStateJson(String::fromUtf8(
                            "{\"scale\":[\"M2\",\"m2\",\"M2\",\"M2\",\"M2\",\"m2\",\"M2\"],"
                            "\"collection_rotation\":0,\"mode_rotation\":0,"
                            "\"generator_cents\":700.0,\"period_cents\":1200.0,"
                            "\"embedding\":{\"large_steps\":5,\"small_steps\":2},"
                            "\"extent\":{\"lower\":{\"nPer\":1,\"nGen\":-2},\"upper\":{\"nPer\":2,\"nGen\":-2}},"
                            "\"reference\":\"none\",\"tonic_extent\":\"tonic-centered\"}"));
    EXPECT_TRUE(st.meloStateJson().contains(u"tonic_extent"));
    EXPECT_FALSE(st.meloStateJson().contains(u"\"tonic_ambit\""));
    std::vector<melo::StaveSegment> segments;
    EXPECT_FALSE(melo::frameForMelody(st.meloStateJson(), u"{\"notes\":[]}", u"tonic-bounded", segments));
}

// Owner rule 7b (2026-08-19): when no Do-line of the stave keeps a change
// indicator on the staff, the staff extends to include it (Kernel
// frame_for_melody_covering through the fork's frame derivation). Built
// from the accepted m5-mode piece with its melody rewritten to Re4..Fa4
// (200..500 cents): the Kernel's one-period window centred on that ambitus
// is [-250, 950] — one Do-line (0). The Kernel's Do->La model puts the old
// Do one period above the new La ("fewest degrees" down: Do at 1200, La at
// 900), so with the only anchor at 0 the upper Do overflows and the change
// section's frame must extend to cover it — here to the full [0, 1200]
// octave, which then holds the whole indicator.
TEST_F(Engraving_MeloStaffM8BandElisionTests, changeIndicatorExtendsTheStaffWhenNoDoLineKeepsItOn)
{
    MasterScore* score = ScoreRW::readScore(u"jimstaff_data/m5-mode.mscx");
    ASSERT_TRUE(score);
    bool alternate = false;
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        for (Chord* chord : chordsOf(m)) {
            for (Note* note : chord->notes()) {
                if (alternate) {
                    note->setMeloPitch(2, -3);   // Fa4 = 500 c
                } else {
                    note->setMeloPitch(0, 0);    // Re4 = 200 c
                }
                alternate = !alternate;
            }
        }
    }
    score->setLayoutAll();
    score->doLayout();
    Measure* m2 = measureNo(score, 2);
    ASSERT_TRUE(m2);
    const StaffType* baseSt = st(score);
    const StaffType* changeSt = score->staff(0)->staffType(m2->tick());
    ASSERT_TRUE(changeSt && changeSt->isMelo() && changeSt != baseSt);
    // Indicator coverage extends the common frame, including the base
    // section: independent section tops would detach Do from the clef.
    const StaffType::MeloFrameView& baseView = baseSt->meloWholeFrameView(score, 0);
    ASSERT_FALSE(baseView.empty());
    // The change section (Do -> La): its frame is extended to cover the
    // indicator — La sits 300 cents below Do, one margin further down.
    melo::ChangeIndicator model;
    ASSERT_TRUE(melo::changeIndicatorIntoStaffType(score, 0, changeSt, model));
    const StaffType::MeloFrameView& changeView = changeSt->meloWholeFrameView(score, 0);
    ASSERT_FALSE(changeView.empty());
    double baseDo = 0.0;
    double changedDo = 0.0;
    ASSERT_TRUE(melo::noteCentsAboveExtentLower(baseSt->meloStateJson(), 1, -2, baseDo));
    ASSERT_TRUE(melo::noteCentsAboveExtentLower(changeSt->meloStateJson(), 1, -2, changedDo));
    EXPECT_NEAR(baseView.topCents() - baseDo, changeView.topCents() - changedDo, 1e-6);
    EXPECT_NEAR(baseView.bottomCents() - baseDo, changeView.bottomCents() - changedDo, 1e-6);
    // After extension: the section's frame grew (here to the Do..Do octave)
    // and the whole indicator is on the staff.
    EXPECT_GE(changeView.topCents(), 1200.0 - 1e-6) << "the section's staff extends to the upper Do";
    EXPECT_NEAR(changeView.bottomCents(), 0.0, 1e-6);
    EXPECT_TRUE(melo::changeIndicatorOverflowCents(changeView, model, changeSt->meloPeriodCents()).empty())
        << "after extension the whole indicator is on the staff";
    EXPECT_DOUBLE_EQ(melo::changeAnchorPeriodCents(changeView, model, changeSt->meloPeriodCents()), 0.0);
    delete score;
}

// M10 supersedes the old layout-time derivation seam: layout is read-only.
// Song-wide tonic ambit is recomputed only by the explicit designated-melody
// triggers covered by Engraving_MeloStaffM10SATBTests.
TEST_F(Engraving_MeloStaffM8BandElisionTests, tonicAmbitIsNeverDerivedAsALayoutSideEffect)
{
    MasterScore* score = ScoreRW::readScore(SINGLE_OCTAVE);
    ASSERT_TRUE(score);
    score->doLayout();
    ASSERT_TRUE(st(score)->meloTonicAmbit() == u"tonic-bounded");
    const String stateBefore = st(score)->meloStateJson();
    const std::vector<std::pair<int, int> > plagal = { { -1, -1 }, { -2, 1 }, { 0, -2 }, { -1, 0 }, { 1, -3 } };
    size_t k = 0;
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        for (Chord* chord : chordsOf(m)) {
            for (Note* note : chord->notes()) {
                const auto& id = plagal[k++ % plagal.size()];
                note->setMeloPitch(id.first, id.second);
            }
        }
    }
    score->setLayoutAll();
    score->doLayout();
    EXPECT_TRUE(st(score)->meloTonicAmbit() == u"tonic-bounded");
    EXPECT_TRUE(st(score)->meloStateJson() == stateBefore)
        << "layout must never mutate the song-wide tonic-ambit carrier";
    delete score;
}

// The automatic frame expands from tempered So to its fixed 3/2 line.
// The bounding guide and crescent cut must share that exact ordinate.
TEST_F(Engraving_MeloStaffM8BandElisionTests, odeToJoyEndsOnItsVisibleSoRatioLine)
{
    MasterScore* score = ScoreRW::readScore(u"jimstaff_data/ode-to-joy.mscx");
    ASSERT_TRUE(score);
    score->doLayout();
    const StaffType* type = st(score);
    System* system = measureSystems(score).front();
    const auto& view = viewOn(score, system);
    const StaffLines* lines = system->firstMeasure()->staffLines(0);
    melo::PeriodicOrigins origins;
    ASSERT_TRUE(melo::periodicOrigins(type->meloStateJson(), origins));
    std::vector<melo::JiLine> ratios;
    ASSERT_TRUE(melo::jiLines(type->meloStateJson(), ratios));
    const double halfStroke = lines->lw() * 0.5;
    int intersectingOutside = 0;
    int excludedOutside = 0;
    for (const auto& ratio : ratios) {
        if (!ratio.visible) {
            continue;
        }
        const double cents = origins.doCentsAboveExtentLower + ratio.cents;
        const double y = type->meloYFromCents(cents, view) * lines->spatium();
        const double top = type->meloYFromCents(view.topCents(), view) * lines->spatium();
        const double bottom = type->meloYFromCents(view.bottomCents(), view) * lines->spatium();
        const bool intersects = y + halfStroke >= top - EPS && y - halfStroke <= bottom + EPS;
        const bool present = std::any_of(lines->meloGuideLines().begin(), lines->meloGuideLines().end(),
                                         [&](const StaffLines::MeloGuideLine& guide) {
            return std::abs(guide.line.y1() - lines->pos().y() - y) < EPS;
        });
        EXPECT_EQ(present, intersects) << "ratio at " << cents;
        if (cents > view.topCents()) {
            intersects ? ++intersectingOutside : ++excludedOutside;
        }
    }
    EXPECT_EQ(intersectingOutside, 0);
    EXPECT_NEAR(view.topCents() - origins.doCentsAboveExtentLower, 1200.0 * std::log2(3.0 / 2.0), 1e-6);
    EXPECT_GT(excludedOutside, 0);
    delete score;
}

TEST_F(Engraving_MeloStaffM8BandElisionTests, emptyHalfStaffFixtureCoversEveryModeAndAmbit)
{
    MasterScore* score = ScoreRW::readScore(String::fromUtf8(engraving_tests_DATA_ROOT)
                                            + u"/jimstaff_data/empty-half-staves-14.mscx", true);
    ASSERT_TRUE(score);
    for (double generator : { 700.0, 686.0, 696.0, 710.0, 720.0, 700.0 }) {
        for (staff_idx_t i = 0; i < score->nstaves(); ++i) {
            String tuned;
            ASSERT_TRUE(melo::retuneGenerator(mutSt(score, i)->meloStateJson(), generator, tuned));
            mutSt(score, i)->setMeloStateJson(tuned);
        }
        score->setLayoutAll();
        score->doLayout();
        ASSERT_EQ(score->nstaves(), 14u);
        const int rotations[] = { 3, 0, 4, 1, 5, 2, 6 };
        System* system = measureSystems(score).front();
        for (staff_idx_t i = 0; i < score->nstaves(); ++i) {
            const StaffType* type = st(score, i);
            ASSERT_TRUE(type->isMelo());
            EXPECT_TRUE(type->meloStateJson().contains(String(u"\"mode_rotation\":%1").arg(rotations[i / 2])));
            EXPECT_EQ(type->meloTonicAmbit(), i % 2 ? String(u"tonic-bounded") : String(u"tonic-centered"));
            const auto& view = viewOn(score, system, i);
            ASSERT_FALSE(view.empty());
            EXPECT_GE(view.topCents() - view.bottomCents(), 600.0 - 1e-6);
            melo::PeriodicOrigins origins;
            ASSERT_TRUE(melo::periodicOrigins(type->meloStateJson(), origins));
            double minimumLower = -300.0;
            double minimumUpper = 300.0;
            if (i % 2) {
                const double tonicRatios[] = { 4.0 / 3.0, 1.0, 3.0 / 2.0, 9.0 / 8.0, 5.0 / 3.0, 5.0 / 4.0, 15.0 / 8.0 };
                const double origin = origins.doCentsAboveExtentLower + 1200.0 * std::log2(tonicRatios[i / 2]);
                minimumLower = origin + std::round((-300.0 - origin) / 1200.0) * 1200.0;
                minimumUpper = minimumLower + 600.0;
                EXPECT_NEAR(view.bottomCents(), minimumLower, 1e-6) << "tonic must bound staff " << i;
            }
            EXPECT_LE(view.bottomCents(), minimumLower + 1e-6);
            EXPECT_GE(view.topCents(), minimumUpper - 1e-6);
            std::vector<melo::JiLine> ratios;
            ASSERT_TRUE(melo::jiLines(type->meloStateJson(), ratios));
            std::vector<double> candidates;
            for (int period = -2; period <= 2; ++period) {
                const double base = origins.doCentsAboveExtentLower + period * type->meloPeriodCents();
                candidates.push_back(base);
                for (const auto& ratio : ratios) {
                    if (ratio.visible) {
                        candidates.push_back(base + ratio.cents);
                    }
                }
            }
            const StaffLines* lines = system->firstMeasure()->staffLines(i);
            for (double edge : { view.bottomCents(), view.topCents() }) {
                EXPECT_TRUE(std::any_of(candidates.begin(), candidates.end(),
                                        [&](double c) { return std::abs(c - edge) < 1e-6; })) << "staff " << i;
                const double y = type->meloYFromCents(edge, view) * lines->spatium() + lines->pos().y();
                EXPECT_TRUE(std::any_of(lines->meloGuideLines().begin(), lines->meloGuideLines().end(), [&](const auto& guide) {
                    return std::abs(guide.line.y1() - y) < 1e-6;
                })) << "staff " << i << " edge " << edge;
            }
            for (double c : candidates) {
                EXPECT_FALSE(c > view.bottomCents() + 1e-6 && c <= minimumLower + 1e-6);
                EXPECT_FALSE(c < view.topCents() - 1e-6 && c >= minimumUpper - 1e-6);
            }
            for (const Segment* segment = score->firstSegment(SegmentType::ChordRest); segment;
                 segment = segment->next1(SegmentType::ChordRest)) {
                const EngravingItem* element = segment->element(i * VOICES);
                EXPECT_FALSE(element && element->isChord());
            }
        }
    }
    delete score;
}

TEST_F(Engraving_MeloStaffM8BandElisionTests, emptyHalfStaffHeadersUseTonicSpecificLabelSidesWithoutRatioLegend)
{
    MasterScore* score = ScoreRW::readScore(String::fromUtf8(engraving_tests_DATA_ROOT)
                                            + u"/jimstaff_data/empty-half-staves-14.mscx", true);
    ASSERT_TRUE(score);
    System* system = measureSystems(score).front();
    const String lowerLabels[] = { u"Do", u"Fa", u"La", u"Do", u"Mi", u"So", u"Ti", u"Re", u"Fa", u"La", u"Do", u"Mi", u"So", u"Ti" };
    const String upperLabels[] = { u"La", u"Do", u"Mi", u"So", u"Ti", u"Re", u"So", u"La", u"Do", u"Mi", u"So", u"Ti", u"Re", u"Fa" };
    int labelsSeen = 0;
    for (staff_idx_t i = 0; i < score->nstaves(); ++i) {
        const StaffType* type = st(score, i);
        const auto& view = viewOn(score, system, i);
        const StaffLines* lines = system->firstMeasure()->staffLines(i);
        const auto geometry = type->meloHeaderGeometry(lines->spatium(), score->style().defaultSpatium(), &view);
        const double clefLeft = lines->pos().x() - 0.3 * lines->spatium() - geometry.clefRx;
        const double dotCenterX = clefLeft - geometry.rightLabelBand - geometry.indicatorW;
        auto provider = std::make_shared<BufferedPaintProvider>();
        Painter painter(provider, "fourteen-staff-headers");
        painter.setViewport(RectF(0, 0, 4000, 4000));
        PaintOptions options;
        lines->renderer()->drawItem(lines, &painter, options);
        painter.endDraw();
        const DrawDataPtr data = provider->drawData();
        int staffLabels = 0;
        bool sawLower = false;
        bool sawUpper = false;
        std::function<void(const DrawData::Item&)> walk = [&](const DrawData::Item& item) {
            for (const DrawData::Data& d : item.datas) {
                for (const DrawText& text : d.texts) {
                    sawLower = sawLower || text.text.contains(lowerLabels[i]);
                    sawUpper = sawUpper || text.text.contains(upperLabels[i]);
                    if (text.text.startsWith(u"M5=")) {
                        EXPECT_FALSE(text.text.contains(u"3:"));
                        EXPECT_FALSE(text.text.contains(u"5:"));
                    } else if (text.text.contains(u":")) {
                        ++staffLabels;
                        if (i == 2 || i == 3) {
                            EXPECT_GT(text.rect.left(), dotCenterX) << "Do staff " << i;
                        } else {
                            EXPECT_LT(text.rect.right(), dotCenterX) << "non-Do staff " << i;
                            EXPECT_GT(geometry.leftLabelBand, geometry.keyLabelAdvance);
                        }
                    }
                }
            }
            for (const DrawData::Item& child : item.chilren) {
                walk(child);
            }
        };
        walk(data->item);
        EXPECT_TRUE(sawLower) << "staff " << i << " lower scale-dot label";
        EXPECT_TRUE(sawUpper) << "staff " << i << " upper scale-dot label";
        EXPECT_EQ(staffLabels, 1) << "staff " << i;
        labelsSeen += staffLabels;
    }
    EXPECT_EQ(labelsSeen, 14);
    delete score;
}
