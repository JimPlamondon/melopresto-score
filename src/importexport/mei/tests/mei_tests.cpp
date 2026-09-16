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
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include "engraving/dom/part.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/pitchspelling.h"
#include "engraving/dom/measure.h"

#include "io/file.h"

#include "engraving/tests/utils/scorerw.h"
#include "engraving/tests/utils/scorecomp.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/note.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftype.h"
#include "engraving/melo/melotuningcontroller.h"
#include "engraving/melo/melochangecontroller.h"
#include "engraving/melo/melobridge.h"

#include "modularity/ioc.h"
#include "importexport/mei/imeiconfiguration.h"
#include "importexport/mei/internal/meireader.h"
#include "importexport/mei/internal/meiwriter.h"

using namespace mu::engraving;

static const String MEI_DIR(u"data/");

////////////////////////////////////////////////////////////////
// Set to true to re-generate the MuseScore reference test files
#define BUILD_MSCORE_REF_FILE false
////////////////////////////////////////////////////////////////

namespace mu::iex::mei {
class Mei_Tests : public ::testing::Test
{
public:
    void meiReadTest(const char* file);

    inline static bool s_generateReferenceFile = BUILD_MSCORE_REF_FILE;
};

void Mei_Tests::meiReadTest(const char* file)
{
    String fileName = String::fromUtf8(file);

    auto importFunc = [](MasterScore* score, const muse::io::path_t& path) -> Err {
        MeiReader meiReader(nullptr);
        return meiReader.import(score, path);
    };

    auto exportFunc = [](Score* score, const muse::io::path_t& path) -> Err {
        MeiWriter meiWriter;
        return meiWriter.writeScore(score, path);
    };

    // Load the .mei file
    MasterScore* score = ScoreRW::readScore(MEI_DIR + fileName + u".mei", false, importFunc);
    EXPECT_TRUE(score);

    // Flag to be turned on to generate the test reference .mscx files from the .mei
    if (s_generateReferenceFile) {
        bool res = ScoreRW::saveScore(score, ScoreRW::rootPath() + u"/" + MEI_DIR + fileName + u".mscx");
        EXPECT_TRUE(res);
        return;
    }

    // Compare with the reference MuseScore file
    EXPECT_TRUE(ScoreComp::saveCompareScore(score, fileName + u".mscx", MEI_DIR + fileName + u".mscx"));

    // Save the .mei file for round trip testing
    bool output = ScoreRW::saveScore(score,  fileName + u".test.mei", exportFunc);
    EXPECT_TRUE(output);
    delete score;

    // Compare the mei files
    EXPECT_TRUE(ScoreComp::compareFiles(fileName + u".test.mei", ScoreRW::rootPath() + u"/" + MEI_DIR + fileName + u".mei"));
}

TEST_F(Mei_Tests, timedConventionalInstrumentControlsWrittenOctave)
{
    std::unique_ptr<MasterScore> score(ScoreRW::readScore(u"data/accid-01.mscx"));
    ASSERT_TRUE(score);
    ASSERT_FALSE(score->staff(0)->staffType(Fraction(0, 1))->isMelo());
    Measure* second = score->firstMeasure()->nextMeasure();
    ASSERT_TRUE(second);
    Instrument later(*score->parts().front()->instrument());
    later.setTranspose(Interval(7, 12));
    score->parts().front()->setInstrument(later, second->tick());
    std::vector<int> expected;
    for (Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        EngravingItem* item = seg->element(0);
        if (!item || !item->isChord()) {
            continue;
        }
        for (Note* note : toChord(item)->notes()) {
            note->setTpcFromPitch();
            const int offset = seg->tick() < second->tick() ? 0 : 12;
            expected.push_back((note->pitch() - offset - tpc2alterByKey(note->tpc2(), Key::C)) / 12 - 1);
        }
    }
    ASSERT_FALSE(expected.empty());
    QTemporaryDir directory;
    const String path = String::fromQString(directory.filePath("timed.mei"));
    auto write = [](Score* source, const muse::io::path_t& destination) -> Err {
        MeiWriter writer;
        return writer.writeScore(source, destination);
    };
    ASSERT_TRUE(ScoreRW::saveScore(score.get(), path, write));
    QFile file(path.toQString());
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QXmlStreamReader xml(&file);
    std::vector<int> actual;
    bool firstVoice = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == u"layer") {
            firstVoice = xml.attributes().value(u"n") == u"1";
        }
        if (xml.isStartElement() && xml.name() == u"note" && firstVoice) {
            actual.push_back(xml.attributes().value(u"oct").toInt());
        }
    }
    ASSERT_FALSE(xml.hasError());
    EXPECT_EQ(actual, expected);
}

TEST_F(Mei_Tests, meloSourceRangesSurviveMeiRoundTrip)
{
    auto importFunc = [](MasterScore* score, const muse::io::path_t& path) -> Err {
        MeiReader reader(nullptr);
        return reader.import(score, path);
    };
    auto exportFunc = [](Score* score, const muse::io::path_t& path) -> Err {
        MeiWriter writer;
        return writer.writeScore(score, path);
    };
    std::unique_ptr<MasterScore> source(ScoreRW::readScore(MEI_DIR + u"jims/v5/jims-synthetic.mei", false, importFunc));
    ASSERT_TRUE(source);
    source->parts().front()->instrument()->setMinPitchA(41);
    source->parts().front()->instrument()->setMaxPitchA(60);
    source->masterScore()->rebuildMidiMapping();
    QTemporaryDir directory;
    const String path = String::fromQString(directory.filePath("source-ranges.mei"));
    ASSERT_TRUE(ScoreRW::saveScore(source.get(), path, exportFunc));
    std::unique_ptr<MasterScore> restored(ScoreRW::readScore(path, true, importFunc));
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->parts().front()->instrument()->minPitchA(), 41);
    EXPECT_EQ(restored->parts().front()->instrument()->maxPitchA(), 60);
}

// MeloPresto MEI (MeloPresto MEI profile) focused round trip: typed state import,
// native carriers, and extMeta regeneration on export.
TEST_F(Mei_Tests, mei_melo_roundtrip_01) {
    auto importFunc = [](MasterScore* score, const muse::io::path_t& path) -> Err {
        MeiReader meiReader(nullptr);
        return meiReader.import(score, path);
    };
    auto exportFunc = [](Score* score, const muse::io::path_t& path) -> Err {
        MeiWriter meiWriter;
        return meiWriter.writeScore(score, path);
    };

    MasterScore* score = ScoreRW::readScore(MEI_DIR + u"jims/v5/jims-synthetic.mei", false, importFunc);
    ASSERT_TRUE(score);

    // Typed staff state: MeloPresto staff type at tick 0 plus two later states.
    const Staff* staff = score->staff(0);
    ASSERT_TRUE(staff);
    const StaffType* base = staff->staffType(Fraction(0, 1));
    ASSERT_TRUE(base && base->isMelo());
    EXPECT_FALSE(base->meloStateJson().isEmpty());
    EXPECT_EQ(base->meloTonicAmbit(), String(u"tonic-bounded"));

    // Note identities and melody part.
    int pitched = 0;
    int identified = 0;
    for (const Segment* seg = score->firstSegment(SegmentType::ChordRest); seg;
         seg = seg->next1(SegmentType::ChordRest)) {
        for (const EngravingItem* item : seg->elist()) {
            if (!item || !item->isChord()) {
                continue;
            }
            for (const Note* note : toChord(item)->notes()) {
                ++pitched;
                if (note->hasMeloPitch()) {
                    ++identified;
                }
            }
        }
    }
    EXPECT_GT(pitched, 0);
    EXPECT_EQ(pitched, identified);
    EXPECT_EQ(score->meloMelodyPart(), engraving::melo::MelodyPart::Soprano);
    ASSERT_EQ(score->meloProvenance().resources.size(), size_t(1));
    EXPECT_TRUE(score->meloProvenance().strictFallback);
    EXPECT_EQ(staff->meloTuningTrajectories().size(), size_t(1));

    // Export regenerates the typed carriers. The harness never rebuilds the
    // MIDI mapping the way the application does on load; writeInstrDef needs
    // playback channels to exist.
    score->masterScore()->rebuildMidiMapping();
    bool output = ScoreRW::saveScore(score, u"jims-synthetic.test.mei", exportFunc);
    EXPECT_TRUE(output);
    muse::io::File out(muse::io::path_t(u"jims-synthetic.test.mei"));
    ASSERT_TRUE(out.open(muse::io::IODevice::ReadOnly));
    const muse::ByteArray meiBytes = out.readAll();
    const String mei = String::fromUtf8(meiBytes.constChar());
    out.close();
    EXPECT_TRUE(mei.contains(u"jm:record"));
    EXPECT_TRUE(mei.contains(u"melo-tonal-state"));
    EXPECT_TRUE(mei.contains(u"melo-chord-name"));
    EXPECT_TRUE(mei.contains(u"melo-tonic-ambit"));
    EXPECT_TRUE(mei.contains(u"melo-melody-part"));
    EXPECT_TRUE(mei.contains(u"<ambitus>"));
    EXPECT_TRUE(mei.contains(u"melo:tuning-trajectory"));
    EXPECT_FALSE(mei.contains(u"jims:"));
    // The state link is jm:state/@annot only; an annot @corresp into extMeta
    // trips MEI's own linking rule (mei-melo audit, 2026-09-14).
    EXPECT_TRUE(mei.contains(u"annot=\"#"));
    EXPECT_FALSE(mei.contains(u"corresp=\"#jmstate"));
    // Every annot @class resolves to a declared category (MEI's class rule).
    EXPECT_TRUE(mei.contains(u"<classDecls>"));
    EXPECT_TRUE(mei.contains(u"xml:id=\"melo.ambit.tonic-bounded\""));
    // Trajectory placement is layout and outside the profile grammar.
    EXPECT_FALSE(mei.contains(u"<jm:trajectory measure=\"3\" off=\"0/1\" staff=\"1\" placement"));
    delete score;
}

TEST_F(Mei_Tests, generated_chord_evidence_roundtrip_and_stale_export)
{
    auto importFunc = [](MasterScore* score, const muse::io::path_t& path) -> Err {
        MeiReader reader(nullptr);
        return reader.import(score, path);
    };
    auto exportFunc = [](Score* score, const muse::io::path_t& path) -> Err {
        MeiWriter writer;
        return writer.writeScore(score, path);
    };
    std::unique_ptr<MasterScore> score(ScoreRW::readScore(MEI_DIR + u"jims/v5/generated-chord-evidence.mei", false, importFunc));
    ASSERT_TRUE(score);
    Harmony* harmony = nullptr;
    Note* note = nullptr;
    for (Segment* seg = score->firstSegment(SegmentType::All); seg; seg = seg->next1()) {
        for (EngravingItem* item : seg->annotations()) {
            if (item->isHarmony()) {
                harmony = toHarmony(item);
            }
        }
        if (seg->isChordRestType() && seg->element(0) && seg->element(0)->isChord()) {
            note = toChord(seg->element(0))->notes().front();
        }
    }
    ASSERT_TRUE(harmony);
    ASSERT_TRUE(note);
    const String proof = harmony->meloEvidence();
    EXPECT_FALSE(proof.empty());
    EXPECT_EQ(harmony->meloEvidenceOrigin(), u"generated");
    EXPECT_TRUE(harmony->meloEvidenceError().empty());
    score->rebuildMidiMapping();
    EXPECT_TRUE(ScoreRW::saveScore(score.get(), u"generated-evidence.test.mei", exportFunc));
    std::unique_ptr<MasterScore> again(ScoreRW::readScore(u"generated-evidence.test.mei", true, importFunc));
    ASSERT_TRUE(again);
    muse::io::File file(muse::io::path_t(u"generated-evidence.test.mei"));
    ASSERT_TRUE(file.open(muse::io::IODevice::ReadOnly));
    EXPECT_TRUE(String::fromUtf8(file.readAll().constChar()).contains(proof));
    note->setMeloPitch(note->meloNPer() + 1, note->meloNGen());
    EXPECT_FALSE(harmony->meloEvidenceError().empty());
    EXPECT_FALSE(ScoreRW::saveScore(score.get(), u"stale-evidence.test.mei", exportFunc));
}

TEST_F(Mei_Tests, mei_accid_01) {
    meiReadTest("accid-01");
}

TEST_F(Mei_Tests, mei_accid_02) {
    meiReadTest("accid-02");
}

TEST_F(Mei_Tests, mei_arpeg_01) {
    meiReadTest("arpeg-01");
}

TEST_F(Mei_Tests, mei_artic_01) {
    meiReadTest("artic-01");
}

TEST_F(Mei_Tests, mei_artic_02) {
    meiReadTest("artic-02");
}

TEST_F(Mei_Tests, mei_beam_01) {
    meiReadTest("beam-01");
}

TEST_F(Mei_Tests, mei_beam_02) {
    meiReadTest("beam-02");
}

TEST_F(Mei_Tests, mei_beam_03) {
    meiReadTest("beam-03");
}

TEST_F(Mei_Tests, mei_breaks_01) {
    meiReadTest("breaks-01");
}

TEST_F(Mei_Tests, mei_breath_01) {
    meiReadTest("breath-01");
}

TEST_F(Mei_Tests, mei_btrem_01) {
    meiReadTest("btrem-01");
}

TEST_F(Mei_Tests, mei_chord_label_01) {
    meiReadTest("chord-label-01");
}

TEST_F(Mei_Tests, mei_clef_01) {
    meiReadTest("clef-01");
}

TEST_F(Mei_Tests, mei_color_01) {
    meiReadTest("color-01");
}

TEST_F(Mei_Tests, mei_cross_staff_01) {
    meiReadTest("cross-staff-01");
}

TEST_F(Mei_Tests, mei_dir_01) {
    meiReadTest("dir-01");
}

TEST_F(Mei_Tests, mei_dynamic_01) {
    meiReadTest("dynamic-01");
}

TEST_F(Mei_Tests, mei_ending_01) {
    meiReadTest("ending-01");
}

TEST_F(Mei_Tests, mei_fermata_01) {
    meiReadTest("fermata-01");
}

TEST_F(Mei_Tests, mei_fig_bass_01) {
    meiReadTest("fig-bass-01");
}

TEST_F(Mei_Tests, mei_fingering_01) {
    meiReadTest("fingering-01");
}

TEST_F(Mei_Tests, mei_ftrem_01) {
    meiReadTest("ftrem-01");
}

TEST_F(Mei_Tests, mei_glisss_01) {
    meiReadTest("gliss-01");
}

TEST_F(Mei_Tests, mei_gracenote_01) {
    meiReadTest("gracenote-01");
}

TEST_F(Mei_Tests, mei_gracenote_02) {
    meiReadTest("gracenote-02");
}

TEST_F(Mei_Tests, mei_hairpin_01) {
    meiReadTest("hairpin-01");
}

TEST_F(Mei_Tests, mei_harp_01) {
    meiReadTest("harp-01");
}

TEST_F(Mei_Tests, mei_harm_tstamp_01) {
    meiReadTest("harm-tstamp-01");
}

TEST_F(Mei_Tests, mei_jump_01) {
    meiReadTest("jump-01");
}

TEST_F(Mei_Tests, mei_jump_02) {
    meiReadTest("jump-02");
}

TEST_F(Mei_Tests, mei_key_signature_01) {
    meiReadTest("key-signature-01");
}

TEST_F(Mei_Tests, mei_midi_01) {
    meiReadTest("midi-01");
}

TEST_F(Mei_Tests, mei_label_01) {
    meiReadTest("label-01");
}

TEST_F(Mei_Tests, laissez_vibrer_01) {
    meiReadTest("laissez-vibrer-01");
}

TEST_F(Mei_Tests, mei_lyric_01) {
    meiReadTest("lyric-01");
}

TEST_F(Mei_Tests, mei_lyric_02) {
    meiReadTest("lyric-02");
}

TEST_F(Mei_Tests, mei_lyric_03) {
    meiReadTest("lyric-03");
}

TEST_F(Mei_Tests, mei_lyric_04) {
    meiReadTest("lyric-04");
}

TEST_F(Mei_Tests, mei_measure_01) {
    meiReadTest("measure-01");
}

TEST_F(Mei_Tests, mei_measure_02) {
    meiReadTest("measure-02");
}

TEST_F(Mei_Tests, mei_mrpt_01) {
    meiReadTest("measure-repeat-01");
}

TEST_F(Mei_Tests, mei_metadata_01) {
    meiReadTest("metadata-01");
}

TEST_F(Mei_Tests, mei_mordent_01) {
    meiReadTest("mordent-01");
}

TEST_F(Mei_Tests, mei_octave_01) {
    meiReadTest("octave-01");
}

TEST_F(Mei_Tests, mei_ornam_01) {
    meiReadTest("ornam-01");
}

TEST_F(Mei_Tests, mei_page_head_01) {
    meiReadTest("page-head-01");
}

TEST_F(Mei_Tests, mei_page_head_02) {
    meiReadTest("page-head-02");
}

TEST_F(Mei_Tests, mei_pedal_01) {
    meiReadTest("pedal-01");
}

TEST_F(Mei_Tests, mei_reh_01) {
    meiReadTest("reh-01");
}

TEST_F(Mei_Tests, mei_roman_numeral_01) {
    meiReadTest("roman-numeral-01");
}

TEST_F(Mei_Tests, mei_score_01) {
    meiReadTest("score-01");
}

TEST_F(Mei_Tests, mei_score_02) {
    meiReadTest("score-02");
}

TEST_F(Mei_Tests, mei_score_03) {
    meiReadTest("score-03");
}

TEST_F(Mei_Tests, mei_slur_01) {
    meiReadTest("slur-01");
}

TEST_F(Mei_Tests, mei_slur_02) {
    meiReadTest("slur-02");
}

TEST_F(Mei_Tests, mei_stem_01) {
    meiReadTest("stem-01");
}

TEST_F(Mei_Tests, mei_tempo_01) {
    meiReadTest("tempo-01");
}

TEST_F(Mei_Tests, mei_tie_01) {
    meiReadTest("tie-01");
}

TEST_F(Mei_Tests, mei_time_signature_01) {
    meiReadTest("time-signature-01");
}

TEST_F(Mei_Tests, mei_time_signature_02) {
    meiReadTest("time-signature-02");
}

TEST_F(Mei_Tests, mei_transpose_01) {
    meiReadTest("transpose-01");
}

TEST_F(Mei_Tests, mei_trill_01) {
    meiReadTest("trill-01");
}

TEST_F(Mei_Tests, mei_tuplet_01) {
    meiReadTest("tuplet-01");
}

TEST_F(Mei_Tests, mei_tuplet_02) {
    meiReadTest("tuplet-02");
}

TEST_F(Mei_Tests, mei_tuplet_03) {
    meiReadTest("tuplet-03");
}
TEST_F(Mei_Tests, missingStateIsExplainedAndDoesNotLeakIntoTheNextFileError)
{
    QFile source(QString::fromUtf8(iex_mei_tests_DATA_ROOT) + "/data/jims/v5/jims-synthetic.mei");
    ASSERT_TRUE(source.open(QIODevice::ReadOnly));
    QByteArray xml = source.readAll();
    int start = xml.indexOf("<extMeta>");
    int end = xml.indexOf("</extMeta>", start);
    ASSERT_GE(start, 0);
    ASSERT_GT(end, start);
    xml.remove(start, end + int(sizeof("</extMeta>") - 1) - start);
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QString path = directory.filePath("missing-state.mei");
    QFile stripped(path);
    ASSERT_TRUE(stripped.open(QIODevice::WriteOnly));
    stripped.write(xml);
    stripped.close();
    muse::Ret received;
    auto import = [&received](MasterScore* score, const muse::io::path_t& input) {
        MeiReader reader(nullptr);
        received = reader.read(score, input);
        return received ? Err::NoError : Err::FileCriticallyCorrupted;
    };
    std::unique_ptr<MasterScore> refused(ScoreRW::readScore(String::fromQString(path), true, import));
    EXPECT_FALSE(refused);
    EXPECT_NE(received.text().find("required state record is missing"), std::string::npos);
    MeiReader reader(nullptr);
    muse::Ret missing = reader.read(nullptr, directory.filePath("does-not-exist.mei"));
    EXPECT_FALSE(missing);
    EXPECT_EQ(missing.text().find("required state record is missing"), std::string::npos);
}

TEST_F(Mei_Tests, unrelatedTypeTokenDoesNotClaimTheFileUsesTheProfile)
{
    QFile source(QString::fromUtf8(iex_mei_tests_DATA_ROOT) + "/data/label-01.mei");
    ASSERT_TRUE(source.open(QIODevice::ReadOnly));
    QByteArray xml = source.readAll();
    xml.replace("<score>", "<score type=\"unrelated-jims-tag\">");
    QTemporaryDir directory;
    QFile output(directory.filePath("unrelated.mei"));
    ASSERT_TRUE(output.open(QIODevice::WriteOnly));
    output.write(xml);
    output.close();
    auto import = [](MasterScore* score, const muse::io::path_t& input) {
        MeiReader reader(nullptr);
        return reader.import(score, input);
    };
    std::unique_ptr<MasterScore> score(ScoreRW::readScore(String::fromQString(output.fileName()), true, import));
    EXPECT_TRUE(score);
}

TEST_F(Mei_Tests, latticeExportRefusesMissingIdentityAndDivergentReferencesBeforeWriting)
{
    for (bool missingIdentity : { true, false }) {
        std::unique_ptr<MasterScore> score(ScoreRW::readScore(u"../../../engraving/tests/jimstaff_data/m9-satb-hymn.mscx"));
        ASSERT_TRUE(score);
        score->rebuildMidiMapping();
        QTemporaryDir directory;
        const QString path = directory.filePath("protected.mei");
        MeiWriter writer;
        ASSERT_EQ(writer.writeScore(score.get(), path), Err::NoError);
        QFile original(path);
        ASSERT_TRUE(original.open(QIODevice::ReadOnly));
        const QByteArray before = original.readAll();
        original.close();
        if (missingIdentity) {
            Note* note = toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->notes().front();
            ASSERT_TRUE(note->hasMeloPitch());
            note->setMeloPitch(INT_MIN, INT_MIN);
        } else {
            auto* type = score->staff(1)->staffType(Fraction(0, 1));
            auto json = QJsonDocument::fromJson(type->meloStateJson().toQString().toUtf8()).object();
            auto reference = json["reference_timeline"].toObject();
            auto pitch = reference["initial"].toObject();
            ASSERT_TRUE(pitch.contains("step"));
            pitch["step"] = "E";
            reference["initial"] = pitch;
            json["reference_timeline"] = reference;
            type->setMeloStateJson(String::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact).constData()));
        }
        EXPECT_NE(writer.writeScore(score.get(), path), Err::NoError);
        ASSERT_TRUE(original.open(QIODevice::ReadOnly));
        EXPECT_EQ(original.readAll(), before);
    }
}

TEST_F(Mei_Tests, latticeImportRefusesOmittedIdentityWithoutChangingInput)
{
    QFile source(QString::fromUtf8(iex_mei_tests_DATA_ROOT) + "/data/jims/v5/jims-synthetic.mei");
    ASSERT_TRUE(source.open(QIODevice::ReadOnly));
    QByteArray xml = source.readAll();
    const int start = xml.indexOf("<jm:note ref=");
    const int end = xml.indexOf("</jm:note>", start);
    ASSERT_GE(start, 0);
    ASSERT_GT(end, start);
    xml.remove(start, end + int(sizeof("</jm:note>") - 1) - start);
    QTemporaryDir directory;
    QFile stripped(directory.filePath("omitted-identity.mei"));
    ASSERT_TRUE(stripped.open(QIODevice::WriteOnly));
    ASSERT_EQ(stripped.write(xml), xml.size());
    stripped.close();
    muse::Ret received;
    auto import = [&received](MasterScore* score, const muse::io::path_t& input) {
        MeiReader reader(nullptr);
        received = reader.read(score, input);
        return received ? Err::NoError : Err::FileCriticallyCorrupted;
    };
    std::unique_ptr<MasterScore> refused(ScoreRW::readScore(String::fromQString(stripped.fileName()), true, import));
    EXPECT_FALSE(refused);
    EXPECT_FALSE(received);
    EXPECT_NE(received.text().find("coordinates"), std::string::npos) << received.text();
    ASSERT_TRUE(stripped.open(QIODevice::ReadOnly));
    EXPECT_EQ(stripped.readAll(), xml);
}

TEST_F(Mei_Tests, latticeImportRefusesDivergentReferencesWithoutChangingInput)
{
    std::unique_ptr<MasterScore> score(ScoreRW::readScore(u"../../../engraving/tests/jimstaff_data/m9-satb-hymn.mscx"));
    ASSERT_TRUE(score);
    score->rebuildMidiMapping();
    QTemporaryDir directory;
    QFile file(directory.filePath("reference.mei"));
    MeiWriter writer;
    ASSERT_EQ(writer.writeScore(score.get(), file.fileName()), Err::NoError);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QByteArray xml = file.readAll();
    file.close();
    const int start = xml.indexOf("<melo:reference-timeline>");
    const int end = xml.indexOf("</melo:reference-timeline>") + int(QByteArray("</melo:reference-timeline>").size());
    ASSERT_GE(start, 0);
    ASSERT_GT(end, start);
    QByteArray conflicting = xml.mid(start, end - start);
    conflicting.replace("step=\"D\"", "step=\"E\"");
    xml.insert(end, conflicting);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(xml), xml.size());
    file.close();
    muse::Ret received;
    auto import = [&received](MasterScore* destination, const muse::io::path_t& input) {
        MeiReader reader(nullptr);
        received = reader.read(destination, input);
        return received ? Err::NoError : Err::FileCriticallyCorrupted;
    };
    std::unique_ptr<MasterScore> refused(ScoreRW::readScore(String::fromQString(file.fileName()), true, import));
    EXPECT_FALSE(refused);
    EXPECT_FALSE(received);
    EXPECT_NE(received.text().find("exactly one canonical reference timeline"), std::string::npos) << received.text();
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), xml);
}

TEST_F(Mei_Tests, CanonicalV5ReferenceRoundTripInAllThreeTunings)
{
    for (double generator : { 700.0, 4800.0 / 7.0, 720.0 }) {
        SCOPED_TRACE(generator);
        std::unique_ptr<MasterScore> score(ScoreRW::readScore(u"../../../engraving/tests/jimstaff_data/m9-satb-hymn.mscx"));
        ASSERT_TRUE(score);
        const String initial
            = u"{\"schema\":\"jimstaff-reference-v1\",\"initial\":{\"step\":\"D\",\"alter\":-1,\"octave\":4},\"events\":[]}";
        score->setMetaTag(melo::REFERENCE_TIMELINE_TAG, initial);
        for (Staff* staff : score->staves()) {
            StaffType* type = staff->staffType(Fraction(0, 1));
            auto configuration
                = QJsonDocument::fromJson(type->meloStateJson().toQString().toUtf8()).object().value("configuration").toObject();
            configuration["schema"] = "jimstaff-v3";
            type->setMeloStateJson(String::fromUtf8(QJsonDocument(configuration).toJson(QJsonDocument::Compact).constData()));
        }
        String error;
        ASSERT_TRUE(melo::rebuildCanonicalReferenceContexts(score.get(), error)) << error.toStdString();
        melo::TuningController tuning(score.get(), 0);
        ASSERT_TRUE(tuning.beginPreview());
        ASSERT_TRUE(tuning.commit(generator));
        const Fraction tick(1, 2);
        ASSERT_TRUE(melo::changeRelativeKey(score.get(), 0, tick, u"{\"nPer\":-1,\"nGen\":3}", initial, error)) << error.toStdString();
        score->rebuildMidiMapping();
        score->doLayout();
        QTemporaryDir directory;
        ASSERT_TRUE(directory.isValid());
        const QString path = directory.filePath("canonical.mei");
        MeiWriter writer;
        ASSERT_EQ(writer.writeScore(score.get(), path), Err::NoError);
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        const QString artifactDirectory = qEnvironmentVariable("MELO_REFERENCE_ARTIFACT_DIR");
        if (!artifactDirectory.isEmpty()) {
            const int divisions = generator == 700.0 ? 12 : generator == 720.0 ? 5 : 7;
            QFile artifact(artifactDirectory + QString("/canonical-%1-tet.mei").arg(divisions));
            ASSERT_TRUE(artifact.open(QIODevice::WriteOnly));
            ASSERT_EQ(artifact.write(bytes), bytes.size());
        }
        EXPECT_TRUE(bytes.contains("urn:melopresto:musicxml:5"));
        EXPECT_EQ(bytes.count("<melo:reference-timeline>"), 1);
        EXPECT_FALSE(bytes.contains("<melo:reference>"));
        EXPECT_FALSE(bytes.contains("key-number"));
        const auto import = [](MasterScore* destination, const muse::io::path_t& input) {
            MeiReader reader(nullptr);
            return reader.import(destination, input);
        };
        std::unique_ptr<MasterScore> reopened(ScoreRW::readScore(String::fromQString(path), true, import));
        ASSERT_TRUE(reopened);
        EXPECT_EQ(reopened->metaTag(melo::REFERENCE_TIMELINE_TAG), score->metaTag(melo::REFERENCE_TIMELINE_TAG));
        ASSERT_EQ(reopened->nstaves(), 4u);
        for (staff_idx_t staff = 0; staff < reopened->nstaves(); ++staff) {
            melo::RelativeKeyEditor editor;
            ASSERT_TRUE(melo::prepareRelativeKeyEditor(reopened.get(), staff, tick, nullptr, editor, error)) << error.toStdString();
            EXPECT_EQ(editor.expression, u"M6");
            double actual = 0, period = 0;
            ASSERT_TRUE(melo::staffMetrics(editor.destinationState, actual, period));
            EXPECT_DOUBLE_EQ(actual, generator);
        }
        const int rootStart = bytes.indexOf("<melo:reference-timeline>");
        const int rootEnd = bytes.indexOf("</melo:reference-timeline>") + int(QByteArray("</melo:reference-timeline>").size());
        ASSERT_GE(rootStart, 0);
        ASSERT_GT(rootEnd, rootStart);
        const auto rootXml = bytes.mid(rootStart, rootEnd - rootStart);
        for (const auto& invalid : {
                bytes.left(rootStart) + bytes.mid(rootEnd),
                bytes.left(rootStart) + rootXml + bytes.mid(rootStart),
                QByteArray(bytes).replace("step=\"D\" alter=\"-1\" octave=\"4\"", "key-number=\"61\""),
                QByteArray(bytes).replace("</melo:staff-state>", "<melo:reference><melo:none/></melo:reference></melo:staff-state>"),
                QByteArray(bytes).replace("<melo:reference-timeline>",
                                          "<melo:reference-timeline xmlns:melo=\"urn:melopresto:musicxml:4\">"),
                QByteArray(bytes).replace("<melo:staff-state", "<melo:staff-state xmlns:melo=\"urn:melopresto:musicxml:4\"")
            }) {
            QFile malformed(directory.path() + "/invalid.mei");
            ASSERT_TRUE(malformed.open(QIODevice::WriteOnly));
            ASSERT_EQ(malformed.write(invalid), invalid.size());
            malformed.close();
            std::unique_ptr<MasterScore> refused(ScoreRW::readScore(String::fromQString(malformed.fileName()), true, import));
            EXPECT_FALSE(refused);
            ASSERT_TRUE(malformed.open(QIODevice::ReadOnly));
            EXPECT_EQ(malformed.readAll(), invalid);
            file.seek(0);
            EXPECT_EQ(file.readAll(), bytes);
        }
        const QByteArray namespaceDeclaration(" xmlns:melo=\"urn:melopresto:musicxml:5\"");
        for (const bool localToEachElement : { false, true }) {
            QByteArray relocated = bytes;
            ASSERT_TRUE(relocated.contains(namespaceDeclaration));
            relocated.replace(namespaceDeclaration, "");
            if (localToEachElement) {
                relocated.replace("<melo:reference-timeline>", "<melo:reference-timeline" + namespaceDeclaration + ">");
                relocated.replace("<melo:staff-state", "<melo:staff-state" + namespaceDeclaration);
                relocated.replace("<melo:pitch", "<melo:pitch" + namespaceDeclaration);
            } else {
                relocated.replace("<jm:musicxml>", "<jm:musicxml" + namespaceDeclaration + ">");
            }
            QFile relocatedFile(directory.path() + "/relocated.mei");
            ASSERT_TRUE(relocatedFile.open(QIODevice::WriteOnly));
            ASSERT_EQ(relocatedFile.write(relocated), relocated.size());
            relocatedFile.close();
            std::unique_ptr<MasterScore> restored(ScoreRW::readScore(String::fromQString(relocatedFile.fileName()), true, import));
            ASSERT_TRUE(restored) << "V5 declarations may be placed on the embedded subtree or its elements";
            EXPECT_EQ(restored->metaTag(melo::REFERENCE_TIMELINE_TAG), score->metaTag(melo::REFERENCE_TIMELINE_TAG));
        }
    }
}

TEST_F(Mei_Tests, exactEqualTemperamentRoundTripRestoresDerivedTuning)
{
    for (double generator : { 700.0, 1200.0 * 4.0 / 7.0, 720.0 }) {
        SCOPED_TRACE(generator);
        std::unique_ptr<MasterScore> score(ScoreRW::readScore(u"../../../engraving/tests/jimstaff_data/m9-satb-hymn.mscx"));
        ASSERT_TRUE(score);
        melo::TuningController tuning(score.get(), 0);
        ASSERT_TRUE(tuning.beginPreview());
        ASSERT_TRUE(tuning.commit(generator));
        auto values = [](Score* score) {
            std::vector<std::pair<NoteVal, double> > result;
            for (Segment* segment = score->firstSegment(SegmentType::ChordRest); segment;
                 segment = segment->next1(SegmentType::ChordRest)) {
                for (track_idx_t track = 0; track < score->ntracks(); ++track) {
                    auto* item = segment->element(track);
                    if (item && item->isChord()) {
                        for (Note* note : toChord(item)->notes()) {
                            result.emplace_back(note->noteVal(), note->tuning());
                        }
                    }
                }
            }
            return result;
        };
        const auto before = values(score.get());
        score->rebuildMidiMapping();
        QTemporaryDir directory;
        const QString path = directory.filePath("tempered.mei");
        MeiWriter writer;
        ASSERT_EQ(writer.writeScore(score.get(), path), Err::NoError);
        auto import = [](MasterScore* destination, const muse::io::path_t& input) {
            MeiReader reader(nullptr);
            return reader.import(destination, input);
        };
        std::unique_ptr<MasterScore> reopened(ScoreRW::readScore(String::fromQString(path), true, import));
        ASSERT_TRUE(reopened);
        const auto after = values(reopened.get());
        ASSERT_EQ(after.size(), before.size());
        for (size_t i = 0; i < before.size(); ++i) {
            EXPECT_TRUE(after[i].first == before[i].first) << "note " << i;
            EXPECT_NEAR(after[i].second, before[i].second, 1e-8) << "note " << i;
        }
    }
}
}
