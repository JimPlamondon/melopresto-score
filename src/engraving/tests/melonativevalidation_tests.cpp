// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
#include <gtest/gtest.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include "engraving/compat/mscxcompat.h"
#include "engraving/compat/scoreaccess.h"
#include "engraving/engravingerrors.h"
#include "utils/scorerw.h"

using namespace mu::engraving;

TEST(Engraving_MeloNativeValidation, unsupportedStateIsExplainedEvenWithoutNotesOrMeloBase)
{
    QFile fixture((ScoreRW::rootPath() + u"/melostaff_data/mode-change.mscx").toQString());
    ASSERT_TRUE(fixture.open(QIODevice::ReadOnly));
    const QString original = QString::fromUtf8(fixture.readAll());
    QTemporaryDir scratch;
    ASSERT_TRUE(scratch.isValid());
    for (int scenario = 0; scenario < 4; ++scenario) {
        SCOPED_TRACE(scenario);
        QString xml = original;
        if (scenario > 0) {
            xml.replace(QRegularExpression("<Chord>.*?<durationType>([^<]+)</durationType>.*?</Chord>",
                                           QRegularExpression::DotMatchesEverythingOption),
                        "<Rest><durationType>\\1</durationType></Rest>");
        }
        const QString openTag = "<meloStateJson>";
        int from = scenario < 2 ? xml.indexOf(openTag) : xml.lastIndexOf(openTag);
        ASSERT_GE(from, 0);
        from += openTag.size();
        const int to = xml.indexOf("</meloStateJson>", from);
        ASSERT_GT(to, from);
        QString json = xml.mid(from, to - from).replace("&quot;", "\"");
        QJsonObject state = QJsonDocument::fromJson(json.toUtf8()).object();
        ASSERT_FALSE(state.isEmpty());
        state["extent"] = QJsonObject { { "lower_do_register", 4 }, { "period_count", 2 } };
        const QString unsupportedJson = QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
        xml.replace(from, to - from, unsupportedJson);
        if (scenario == 3) {
            const int baseFlag = xml.indexOf("<melo>1</melo>");
            ASSERT_GE(baseFlag, 0);
            xml.replace(baseFlag, 14, "<melo>0</melo>");
        }
        const QString path = scratch.filePath(QString("old-state-%1.mscx").arg(scenario));
        QString validXml = xml;
        validXml.replace(from, unsupportedJson.size(), json);
        const QString controlPath = scratch.filePath(QString("valid-state-%1.mscx").arg(scenario));
        QFile controlFile(controlPath);
        ASSERT_TRUE(controlFile.open(QIODevice::WriteOnly));
        const QByteArray controlBytes = validXml.toUtf8();
        ASSERT_EQ(controlFile.write(controlBytes), controlBytes.size());
        controlFile.close();
        std::unique_ptr<MasterScore> control(compat::ScoreAccess::createMasterScoreWithBaseStyle(nullptr));
        const muse::Ret controlResult = compat::loadMsczOrMscx(control.get(), controlPath);
        ASSERT_TRUE(controlResult) << controlResult.text();
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        const QByteArray bytes = xml.toUtf8();
        ASSERT_EQ(file.write(bytes), bytes.size());
        file.close();
        std::unique_ptr<MasterScore> score(compat::ScoreAccess::createMasterScoreWithBaseStyle(nullptr));
        const muse::Ret result = compat::loadMsczOrMscx(score.get(), path);
        EXPECT_EQ(result.code(), int(Err::FileBadFormat));
        EXPECT_NE(result.text().find("MeloPresto data that this version cannot read"), std::string::npos);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        EXPECT_EQ(file.readAll(), bytes);
    }
}
