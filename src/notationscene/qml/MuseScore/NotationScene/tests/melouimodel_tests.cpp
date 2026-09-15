// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
#include <vector>

#include <gtest/gtest.h>
#include <QtTest/QTest>
#include <QQuickWindow>
#include <QQuickItem>
#include <QRawFont>
#include <QPainterPath>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QFile>
#include <QXmlStreamReader>
#include "context/internal/globalcontext.h"
#include "engraving/tests/utils/scorerw.h"
#include "engraving/tests/utils/testutils.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/note.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftype.h"
#include "engraving/dom/stafftypechange.h"
#include "engraving/dom/part.h"
#include "notation/internal/masternotationparts.h"
#include "notation/internal/notationundostack.h"
#include "notation/internal/notationnoteinput.h"
#include "engraving/editing/undo.h"
#include "engraving/melo/melochangecontroller.h"
#include "engraving/melo/melochange.h"
#include "engraving/melo/melotuningcontroller.h"
#include "engraving/melo/melopitchlabel.h"
#include "draw/fontmetrics.h"
#include "inspector/internal/elementrepositoryservice.h"
#include "inspector/qml/MuseScore/Inspector/general/playback/internal/noteplaybackmodel.h"
#include "inspector/qml/MuseScore/Inspector/melostaffsettingsmodel.h"
#include "inspector/qml/MuseScore/Inspector/meloscoresettingsmodel.h"
#include "inspector/qml/MuseScore/Inspector/melotuningmodel.h"
#include "inspector/qml/MuseScore/Inspector/notation/chordsymbols/chordsymbolsettingsmodel.h"
#include "notation/tests/mocks/notationinteractionmock.h"
#include "notation/tests/mocks/notationconfigurationmock.h"
#include "notation/tests/mocks/notationselectionmock.h"
#include "notation/internal/notation.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/tie.h"
#include "engraving/dom/pitchspelling.h"
#include "engraving/melo/melobridge.h"
#include "playback/tests/mocks/playbackcontrollermock.h"
#include "notationscene/qml/MuseScore/NotationScene/noteinputbarmodel.h"
#include "notationscene/qml/MuseScore/NotationScene/meloinitialpitchmodel.h"
#include "notationscene/qml/MuseScore/NotationScene/melokeychangemodel.h"
#include "notationscene/qml/MuseScore/NotationScene/notationviewinputcontroller.h"
#include "mocks/controlledviewmock.h"
#include "actions/tests/mocks/actionsdispatchermock.h"
#include "engraving/dom/stafflines.h"
#include "engraving/rendering/iscorerenderer.h"
#include "engraving/rendering/score/tdraw.h"
#include "draw/bufferedpaintprovider.h"
#include "draw/painter.h"
#include "ui/qml/Muse/Ui/navigationpanel.h"
#include <QPainter>
using namespace mu;
using namespace mu::engraving;
using namespace mu::inspector;
namespace {
bool sendEditorKey(QObject* input, int key, const QString& text = QString())
{
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, text);
    return QCoreApplication::sendEvent(input, &event);
}

void configureEditorEngine(QQmlEngine& engine)
{
    engine.globalObject().setProperty("qsTrc", engine.evaluate("(function(context, text) { return text; })"));
    engine.rootContext()->setContextProperty("ui", QVariantMap { { "theme", QVariantMap {
                                                     { "bodyFont", QFont() }, { "bodyBoldFont", QFont() },
                                                     { "fontPrimaryColor", QColor(Qt::black) },
                                                     { "backgroundPrimaryColor", QColor(Qt::white) },
                                                     { "backgroundSecondaryColor", QColor(Qt::white) },
                                                     { "textFieldColor", QColor(Qt::white) }, { "strokeColor", QColor(Qt::gray) },
                                                     { "accentColor", QColor(Qt::blue) },
                                                     { "buttonColor", QColor(Qt::lightGray) }, { "buttonOpacityNormal", 1.0 },
                                                     { "itemOpacityDisabled", 0.5 },
                                                     { "borderWidth", 1 }, { "navCtrlBorderWidth", 1 }, { "iconsFont", QFont() },
                                                     { "defaultButtonSize", 32 }, { "linkColor", QColor(Qt::blue) }
                                                 } } });
    engine.addImportPath("qrc:/qt/qml");
}

class TestAnnouncements : public muse::accessibility::IAccessibilityController
{
public:
    void reg(muse::accessibility::IAccessible*) override {}
    void unreg(muse::accessibility::IAccessible*) override {}
    bool isReg(muse::accessibility::IAccessible*) const override { return false; }
    void announce(const QString& text) override { messages << text; }
    QString announcement() const override { return messages.isEmpty() ? QString() : messages.last(); }
    const muse::accessibility::IAccessible* accessibleRoot() const override { return nullptr; }
    const muse::accessibility::IAccessible* lastFocused() const override { return nullptr; }
    bool needToVoicePanelInfo() const override { return false; }
    QString currentPanelAccessibleName() const override { return {}; }
    void setIgnoreQtAccessibilityEvents(bool) override {}
    QStringList messages;
};
class TestElements : public notation::INotationElements
{
public:
    explicit TestElements(Score* score)
        : m_score(score) {}
    Score* msScore() const override { return m_score; }
    std::vector<EngravingItem*> search(const QString&) const override { return {}; }
    std::vector<EngravingItem*> elements(const notation::FilterElementsOptions&) const override { return {}; }
    Measure* measure(int) const override { return m_score->firstMeasure(); }
    const notation::PageList& pages() const override { return m_score->pages(); }
    const Page* pageByPoint(const muse::PointF&) const override { return nullptr; }
private:
    Score* m_score;
};
class TestNotation : public notation::INotation
{
public:
    explicit TestNotation(Score* score)
        : m_elements(std::make_shared<TestElements>(score)) {}
    project::INotationProject* project() const override { return nullptr; }
    notation::IMasterNotationPtr masterNotation() const override { return nullptr; }
    QString name() const override { return {}; }
    QString projectName() const override { return {}; }
    QString projectNameAndPartName() const override { return {}; }
    QString workTitle() const override { return {}; }
    QString projectWorkTitle() const override { return {}; }
    QString projectWorkTitleAndPartName() const override { return {}; }
    bool isOpen() const override { return true; }
    void setIsOpen(bool) override {}
    muse::async::Notification openChanged() const override { return {}; }
    bool hasVisibleParts() const override { return true; }
    bool isMaster() const override { return true; }
    notation::ViewMode viewMode() const override { return {}; }
    void setViewMode(const notation::ViewMode&) override {}
    muse::async::Notification viewModeChanged() const override { return {}; }
    notation::INotationPaintingPtr painting() const override { return nullptr; }
    notation::INotationViewStatePtr viewState() const override { return nullptr; }
    notation::INotationSoloMuteStatePtr soloMuteState() const override { return nullptr; }
    notation::INotationInteractionPtr interaction() const override { return testInteraction; }
    notation::INotationInteractionPtr testInteraction;
    notation::INotationMidiInputPtr midiInput() const override { return nullptr; }
    notation::INotationUndoStackPtr undoStack() const override { return nullptr; }
    notation::INotationStylePtr style() const override { return nullptr; }
    notation::INotationElementsPtr elements() const override { return m_elements; }
    notation::INotationAccessibilityPtr accessibility() const override { return nullptr; }
    notation::INotationPartsPtr parts() const override { return nullptr; }
    muse::async::Notification notationChanged() const override { return m_changed; }
private:
    std::shared_ptr<TestElements> m_elements;
    muse::async::Notification m_changed;
};
}
class MeloUiModelTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        score.reset(ScoreRW::readScore(muse::String::fromUtf8(MELO_UI_TEST_DATA_ROOT) + u"/jimstaff_data/mode-change.mscx", true));
        ASSERT_TRUE(score);
        global = std::make_shared<context::GlobalContext>();
        notation = std::make_shared<TestNotation>(score.get());
        playback = std::make_shared<testing::NiceMock<playback::PlaybackControllerMock> >();
        global->setCurrentNotation(notation);
    }

    EngravingItem* selectMeasure(int index)
    {
        Measure* measure = score->firstMeasure();
        while (index-- && measure) {
            measure = measure->nextMeasure();
        }
        EXPECT_TRUE(measure);
        EngravingItem* item = measure->first(SegmentType::ChordRest)->element(0);
        EXPECT_TRUE(item);
        score->select(item, SelectType::SINGLE);
        repository.updateElementList({ item }, SelState::LIST);
        return item;
    }

    void authorKnownD4Reference()
    {
        muse::String error, root;
        ASSERT_TRUE(melo::defaultReferenceTimeline(root, error));
        score->setMetaTag(melo::REFERENCE_TIMELINE_TAG, root);
        for (Score* related : score->scoreList()) {
            for (Staff* staff : related->staves()) {
                const auto authorConfiguration = [](StaffType* type) {
                    QJsonObject config = QJsonDocument::fromJson(type->meloStateJson().toQString().toUtf8()).object();
                    if (config["schema"].toString() == "jimstaff-request-v3") {
                        config = config["configuration"].toObject();
                    }
                    config.remove("reference");
                    config["schema"] = "jimstaff-v3";
                    type->setMeloStateJson(muse::String::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact).constData()));
                };
                authorConfiguration(staff->staffType(Fraction(0, 1)));
                for (Measure* measure = related->firstMeasure(); measure; measure = measure->nextMeasure()) {
                    for (const StaffTypeChange* carrier : melo::changeCarriers(measure, staff->idx())) {
                        authorConfiguration(staff->staffType(carrier->tick()));
                    }
                }
            }
        }
        ASSERT_TRUE(melo::rebuildCanonicalReferenceContexts(score.get(), error)) << error.toStdString();
        notation = std::make_shared<TestNotation>(score.get());
        global->setCurrentNotation(notation);
    }

    std::unique_ptr<MasterScore> score;
    std::shared_ptr<context::GlobalContext> global;
    std::shared_ptr<TestNotation> notation;
    std::shared_ptr<testing::NiceMock<playback::PlaybackControllerMock> > playback;
    ElementRepositoryService repository;
};
TEST_F(MeloUiModelTests, ReferenceBridgeKeepsHeaderEditingSeparateFromRelativeEvents)
{
    const auto object = [](const muse::String& value) { return QJsonDocument::fromJson(value.toQString().toUtf8()).object(); };
    const QJsonObject configuration
        = QJsonDocument::fromJson(
              R"({"schema":"jimstaff-v3","scale":["M2","m2","M2","M2","M2","m2","M2"],"collection_rotation":0,"mode_rotation":0,"generator_cents":700.0,"period_cents":1200.0,"embedding":{"large_steps":5,"small_steps":2},"extent":{"lower":{"nPer":1,"nGen":-2},"upper":{"nPer":2,"nGen":-2}},"tonic_ambit":"tonic-bounded"})")
          .object();
    const muse::String zero = u"{\"numerator\":0,\"denominator\":1}";
    const muse::String time = u"{\"numerator\":7,\"denominator\":8}";
    muse::String root, error;
    ASSERT_TRUE(melo::defaultReferenceTimeline(root, error)) << error.toStdString();
    for (const double generator : { 700.0, 4800.0 / 7.0, 720.0 }) {
        QJsonObject tuned = configuration;
        tuned["generator_cents"] = generator;
        const QJsonArray entries { QJsonObject { { "at", object(zero) }, { "configuration", tuned } } };
        const muse::String history = muse::String::fromUtf8(QJsonDocument(entries).toJson(QJsonDocument::Compact).constData());
        muse::String request, updated, stored;
        ASSERT_TRUE(melo::staffRequest(history, root, zero, request, error)) << error.toStdString();
        ASSERT_TRUE(melo::staffConfiguration(request, stored, error)) << error.toStdString();
        EXPECT_EQ(object(stored),
                  tuned) << stored.toStdString() << " expected " << QJsonDocument(tuned).toJson(QJsonDocument::Compact).constData();
        melo::TonicPitchLabel label;
        ASSERT_TRUE(melo::tonicPitchLabel(request, label));
        EXPECT_FALSE(melo::editHeaderPitch(request, u"key-change-pitch", 0, u"C4", updated, error));
        EXPECT_FALSE(melo::editHeaderPitch(request, u"staff-header-tonic-pitch", 0, u"64", updated, error));
        ASSERT_TRUE(melo::editRelativeKey(request, time, u"{\"nPer\":-1,\"nGen\":3}", updated, error)) << error.toStdString();
        EXPECT_EQ(object(updated)["initial"], object(root)["initial"]);
        EXPECT_EQ(object(updated)["events"].toArray().at(0).toObject()["interval"].toObject(), object(u"{\"nPer\":-1,\"nGen\":3}"));
        if (generator == 700.0) {
            ASSERT_TRUE(melo::staffRequest(history, updated, time, request, error)) << error.toStdString();
            ASSERT_TRUE(melo::editHeaderPitch(request, u"staff-header-tonic-pitch", 0, u"Bb4", updated, error)) << error.toStdString();
            EXPECT_EQ(object(updated)["initial"].toObject(), object(u"{\"step\":\"E\",\"alter\":-1,\"octave\":4}"));
        }
        EXPECT_FALSE(melo::editRelativeKey(request, time, u"{\"nPer\":0,\"nGen\":1,\"pitch\":\"E4\"}", updated, error));
    }
}

TEST_F(MeloUiModelTests, InitialPitchModelPreservesInvalidTextAndCancelsOnScoreChange)
{
    authorKnownD4Reference();
    notation::MeloInitialPitchModel model;
    model.context.set(global);
    const auto open = [&]() {
        model.load(0, 0, 1, 0, score->staff(0)->staffType(Fraction(0, 1))->meloStateJson().toQString(),
                   score->metaTag(melo::REFERENCE_TIMELINE_TAG).toQString());
    };
    const auto original = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
    const int undo = score->undoStack()->size();
    open();
    ASSERT_TRUE(model.active());
    EXPECT_FALSE(model.pitch().isEmpty());
    EXPECT_EQ(score->undoStack()->size(), undo);
    ASSERT_FALSE(model.commit("64"));
    EXPECT_EQ(model.pitch(), "64");
    EXPECT_FALSE(model.error().isEmpty());
    EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), original);
    EXPECT_EQ(score->undoStack()->size(), undo);
    model.cancel();
    EXPECT_FALSE(model.active());
    EXPECT_FALSE(model.commit("D4"));
    open();
    ASSERT_TRUE(model.commit(model.pitch()));
    EXPECT_EQ(score->undoStack()->size(), undo);
    open();
    ASSERT_TRUE(model.commit("Db4"));
    EXPECT_EQ(score->undoStack()->size(), undo + 1);
    open();
    ASSERT_TRUE(model.active());
    EXPECT_EQ(model.pitch(), QString::fromUtf8("D♭4"));
    global->setCurrentNotation(nullptr);
    EXPECT_FALSE(model.active());
    EXPECT_FALSE(model.commit("E4"));
    EXPECT_EQ(score->undoStack()->size(), undo + 1);
    model.load(-1, 0, 1, 0, {}, {});
    EXPECT_FALSE(model.async_isConnected());
}

TEST_F(MeloUiModelTests, InitialPitchDialogLoadsTheEffectivePitchAndRetainsInvalidInput)
{
    authorKnownD4Reference();
    QQmlEngine engine;
    configureEditorEngine(engine);
    QQmlComponent component(&engine, QUrl("qrc:/qt/qml/MuseScore/NotationScene/MeloInitialPitchDialog.qml"));
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> dialog(component.beginCreate(engine.rootContext()));
    ASSERT_TRUE(dialog) << component.errorString().toStdString();
    auto* model = dialog->findChild<notation::MeloInitialPitchModel*>();
    ASSERT_TRUE(model);
    model->context.set(global);
    dialog->setProperty("staffIndex", 0);
    dialog->setProperty("expectedState", score->staff(0)->staffType(Fraction(0, 1))->meloStateJson().toQString());
    dialog->setProperty("expectedTimeline", score->metaTag(melo::REFERENCE_TIMELINE_TAG).toQString());
    component.completeCreate();
    ASSERT_TRUE(model->active()) << model->error().toStdString();
    QObject* input = dialog->findChild<QObject*>("meloInitialPitchInput");
    ASSERT_TRUE(input);
    EXPECT_EQ(input->property("currentText").toString(), model->pitch());
    const int undo = score->undoStack()->size();
    QObject* textInput = input->property("inputField").value<QObject*>();
    ASSERT_TRUE(textInput);
    textInput->setProperty("text", "");
    ASSERT_TRUE(sendEditorKey(textInput, Qt::Key_6, "6"));
    ASSERT_TRUE(sendEditorKey(textInput, Qt::Key_4, "4"));
    ASSERT_TRUE(sendEditorKey(textInput, Qt::Key_Return));
    EXPECT_FALSE(model->error().isEmpty());
    EXPECT_EQ(textInput->property("text").toString(), "64");
    EXPECT_EQ(score->undoStack()->size(), undo);
    ASSERT_TRUE(sendEditorKey(textInput, Qt::Key_Escape));
    EXPECT_FALSE(model->active());
    EXPECT_EQ(score->undoStack()->size(), undo);
}

TEST_F(MeloUiModelTests, RelativeKeyModelPreviewsWithoutMutatingThenCreatesEditsAndRemoves)
{
    authorKnownD4Reference();
    const Fraction tick = score->firstMeasure()->nextMeasure()->tick();
    const auto initial = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
    const int undo = score->undoStack()->size();
    notation::MeloKeyChangeModel model;
    model.context.set(global);
    auto open = [&]() {
        muse::String state;
        ASSERT_TRUE(melo::effectiveState(score.get(), 0, score->tick2measure(tick), tick, state));
        model.load(0, tick.numerator(), tick.denominator(), state.toQString(), score->metaTag(melo::REFERENCE_TIMELINE_TAG).toQString());
        ASSERT_TRUE(model.active()) << model.error().toStdString();
        ASSERT_TRUE(model.valid()) << model.error().toStdString();
    };
    open();
    EXPECT_FALSE(model.existing());
    EXPECT_EQ(model.expression(), "M6");
    EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), initial);
    EXPECT_EQ(score->undoStack()->size(), undo);
    model.preview("C4");
    EXPECT_FALSE(model.valid());
    EXPECT_EQ(model.expression(), "C4");
    EXPECT_FALSE(model.commit("C4"));
    EXPECT_EQ(score->undoStack()->size(), undo);
    model.preview("M6");
    ASSERT_TRUE(model.valid()) << model.error().toStdString();
    QImage image(460, 240, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    model.paintPreview(&painter, image.width(), image.height());
    painter.end();
    int ink = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qGray(image.pixel(x, y)) < 128) {
                ++ink;
            }
        }
    }
    EXPECT_GT(ink, 100);
    ASSERT_TRUE(model.commit("M6")) << model.error().toStdString();
    EXPECT_EQ(score->undoStack()->size(), undo + 1);
    const auto inserted = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
    open();
    EXPECT_TRUE(model.existing());
    EXPECT_EQ(model.expression(), "M6");
    ASSERT_TRUE(model.commit("M6"));
    EXPECT_EQ(score->undoStack()->size(), undo + 1);
    open();
    ASSERT_TRUE(model.commit("-M6")) << model.error().toStdString();
    EXPECT_NE(score->metaTag(melo::REFERENCE_TIMELINE_TAG), inserted);
    open();
    ASSERT_TRUE(model.commit("P1")) << model.error().toStdString();
    EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), initial);
    EXPECT_EQ(score->undoStack()->size(), undo + 3);
    model.load(-1, 0, 1, {}, {});
    EXPECT_FALSE(model.async_isConnected());
}

TEST_F(MeloUiModelTests, RelativeKeyPreviewKeepsLargeAuthoredIntervalsInAllThreeTunings)
{
    authorKnownD4Reference();
    const Fraction tick = score->firstMeasure()->nextMeasure()->tick();
    for (const auto& tuning : std::vector<std::pair<int, double> > { { 12, 700.0 }, { 7, 4800.0 / 7.0 }, { 5, 720.0 } }) {
        SCOPED_TRACE(tuning.first);
        melo::TuningController tuner(score.get(), 0);
        ASSERT_TRUE(tuner.beginPreview());
        ASSERT_TRUE(tuner.commit(tuning.second));
        const auto root = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
        const int undo = score->undoStack()->size();
        notation::MeloKeyChangeModel model;
        model.context.set(global);
        const auto state = score->staff(0)->staffType(tick)->meloStateJson();
        model.load(0, tick.numerator(), tick.denominator(), state.toQString(), root.toQString());
        for (const QString expression : { QString("100*M5 - 57*P8"), QString("57*P8 - 100*M5") }) {
            model.preview(expression);
            ASSERT_TRUE(model.valid()) << model.error().toStdString();
            QImage image(700, 340, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            model.paintPreview(&painter, image.width(), image.height());
            painter.end();
            int ink = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    ink += qGray(image.pixel(x, y)) < 128;
                }
            }
            EXPECT_GT(ink, 100);
            const QString output = qEnvironmentVariable("MELO_REFERENCE_ARTIFACT_DIR");
            if (!output.isEmpty()) {
                ASSERT_TRUE(image.save(output + QString("/large-relative-%1-tet-%2.png").arg(tuning.first)
                                       .arg(expression.startsWith("100") ? "positive" : "negative")));
            }
            EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), root);
            EXPECT_EQ(score->undoStack()->size(), undo);
        }
        ASSERT_TRUE(model.commit("100*M5 - 57*P8")) << model.error().toStdString();
        const auto events
            = QJsonDocument::fromJson(score->metaTag(melo::REFERENCE_TIMELINE_TAG).toQString().toUtf8()).object()["events"].toArray();
        ASSERT_EQ(events.size(), 1);
        EXPECT_EQ(events[0].toObject()["interval"].toObject()["nPer"].toInt(), -57);
        EXPECT_EQ(events[0].toObject()["interval"].toObject()["nGen"].toInt(), 100);
        score->undoRedo(true, nullptr);
        EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), root);
    }
}

TEST_F(MeloUiModelTests, RelativeKeyDialogUsesTypedIntervalAndNeverAnAbsolutePitchField)
{
    authorKnownD4Reference();
    const Fraction tick = score->firstMeasure()->nextMeasure()->tick();
    const auto initial = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
    QQmlEngine engine;
    configureEditorEngine(engine);
    QQmlComponent component(&engine, QUrl("qrc:/qt/qml/MuseScore/NotationScene/MeloKeyChangeDialog.qml"));
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> dialog(component.beginCreate(engine.rootContext()));
    ASSERT_TRUE(dialog) << component.errorString().toStdString();
    auto* model = dialog->findChild<notation::MeloKeyChangeModel*>();
    ASSERT_TRUE(model);
    model->context.set(global);
    dialog->setProperty("staffIndex", 0);
    dialog->setProperty("tickNumerator", tick.numerator());
    dialog->setProperty("tickDenominator", tick.denominator());
    muse::String state;
    ASSERT_TRUE(melo::effectiveState(score.get(), 0, score->tick2measure(tick), tick, state));
    dialog->setProperty("expectedState", state.toQString());
    dialog->setProperty("expectedTimeline", initial.toQString());
    component.completeCreate();
    ASSERT_TRUE(model->valid()) << model->error().toStdString();
    auto* preview = dialog->findChild<notation::MeloKeyChangePreview*>("meloKeyChangePreview");
    ASSERT_TRUE(preview);
    EXPECT_EQ(preview->model(), model);
    QObject* input = dialog->findChild<QObject*>("meloRelativeIntervalInput");
    ASSERT_TRUE(input);
    QObject* textInput = input->property("inputField").value<QObject*>();
    ASSERT_TRUE(textInput);
    const int undo = score->undoStack()->size();
    textInput->setProperty("text", "D4");
    ASSERT_TRUE(sendEditorKey(textInput, Qt::Key_Return));
    EXPECT_FALSE(model->valid());
    EXPECT_EQ(textInput->property("text").toString(), "D4");
    EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), initial);
    EXPECT_EQ(score->undoStack()->size(), undo);
    textInput->setProperty("text", "-M6");
    ASSERT_TRUE(sendEditorKey(textInput, Qt::Key_Return));
    EXPECT_FALSE(model->active());
    EXPECT_TRUE(model->error().isEmpty()) << model->error().toStdString();
    EXPECT_EQ(score->undoStack()->size(), undo + 1);
    melo::RelativeKeyEditor check;
    muse::String error;
    ASSERT_TRUE(melo::prepareRelativeKeyEditor(score.get(), 0, tick, nullptr, check, error));
    EXPECT_EQ(check.expression, u"-M6");
}

TEST_F(MeloUiModelTests, RelativeKeyPreviewUsesProductionTerrainIn12Tet7TetAnd5Tet)
{
    authorKnownD4Reference();
    const Fraction tick = score->firstMeasure()->nextMeasure()->tick();
    const auto root = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
    for (const auto& tuning : std::vector<std::pair<int, double> > { { 12, 700.0 }, { 7, 4800.0 / 7.0 }, { 5, 720.0 } }) {
        SCOPED_TRACE(tuning.first);
        melo::TuningController tuningController(score.get(), 0);
        ASSERT_TRUE(tuningController.beginPreview());
        ASSERT_TRUE(tuningController.commit(tuning.second));
        const int undo = score->undoStack()->size();
        muse::String state;
        ASSERT_TRUE(melo::effectiveState(score.get(), 0, score->tick2measure(tick), tick, state));
        notation::MeloKeyChangeModel model;
        model.context.set(global);
        model.load(0, tick.numerator(), tick.denominator(), state.toQString(), root.toQString());
        ASSERT_TRUE(model.valid()) << model.error().toStdString();
        if (tuning.first == 7) {
            model.preview("7*M5 - 4*P8");
        }
        if (tuning.first == 5) {
            model.preview("5*M5 - 3*P8");
        }
        ASSERT_TRUE(model.valid()) << model.error().toStdString();
        QImage image(460, 240, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        model.paintPreview(&painter, image.width(), image.height());
        ASSERT_TRUE(painter.isActive());
        painter.end();
        int ink = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qGray(image.pixel(x, y)) < 128) {
                    ++ink;
                }
            }
        }
        EXPECT_GT(ink, 100);
        const QString output = qEnvironmentVariable("MELO_REFERENCE_ARTIFACT_DIR");
        if (!output.isEmpty()) {
            ASSERT_TRUE(image.save(output + QString("/relative-editor-%1-tet.png").arg(tuning.first)));
        }
        model.cancel();
        EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), root);
        EXPECT_EQ(score->undoStack()->size(), undo);
    }
}

TEST_F(MeloUiModelTests, NativeReferenceStorageHasOneRootAndNoDisposableStaffContext)
{
    global->setCurrentNotation(nullptr);
    score.reset(ScoreRW::readScore(muse::String::fromUtf8(MELO_UI_TEST_DATA_ROOT) + u"/jimstaff_data/m9-satb-hymn.mscx", true));
    ASSERT_TRUE(score);
    ASSERT_TRUE(TestUtils::createPart(score.get()));
    authorKnownD4Reference();
    const Fraction tick = score->firstMeasure()->nextMeasure()->tick();
    muse::String error;
    ASSERT_TRUE(melo::changeRelativeKey(score.get(), 0, tick, u"{\"nPer\":-1,\"nGen\":3}",
                                        score->metaTag(melo::REFERENCE_TIMELINE_TAG), error)) << error.toStdString();
    const auto root = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const QString path = temporary.path() + "/canonical-reference.mscx";
    ASSERT_TRUE(ScoreRW::saveScore(score.get(), muse::String::fromQString(path)));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    QXmlStreamReader xml(bytes);
    int roots = 0;
    int configurations = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }
        if (xml.name() == u"metaTag" && xml.attributes().value("name") == u"meloReferenceTimelineV1") {
            ++roots;
            EXPECT_EQ(xml.readElementText(), root.toQString());
        } else if (xml.name() == u"jimsStateJson") {
            const auto value = QJsonDocument::fromJson(xml.readElementText().toUtf8()).object();
            EXPECT_EQ(value["schema"].toString(), "jimstaff-v3");
            EXPECT_FALSE(value.contains("reference_timeline"));
            EXPECT_FALSE(value.contains("reference"));
            EXPECT_FALSE(value.contains("at"));
            ++configurations;
        }
    }
    EXPECT_FALSE(xml.hasError());
    EXPECT_EQ(roots, 1);
    EXPECT_GT(configurations, 1);
    std::unique_ptr<MasterScore> reopened(ScoreRW::readScore(muse::String::fromQString(path), true));
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->scoreList().size(), score->scoreList().size());
    EXPECT_EQ(reopened->metaTag(melo::REFERENCE_TIMELINE_TAG), root);
    muse::String state;
    ASSERT_TRUE(melo::effectiveState(reopened.get(), 0, reopened->tick2measure(tick), tick, state));
    ASSERT_TRUE(melo::validateState(state, error)) << error.toStdString();
    melo::RelativeKeyEditor editor;
    ASSERT_TRUE(melo::prepareRelativeKeyEditor(reopened.get(), 0, tick, nullptr, editor, error));
    EXPECT_EQ(editor.expression, u"M6");

    // A disposable request is not a second allowed native representation.
    QByteArray disguised = bytes;
    const int start = disguised.indexOf("<jimsStateJson>") + int(QByteArray("<jimsStateJson>").size());
    const int end = disguised.indexOf("</jimsStateJson>", start);
    ASSERT_GT(end, start);
    disguised.replace(start, end - start, state.toQString().toHtmlEscaped().toUtf8());
    const QString invalidPath = temporary.path() + "/disposable-request.mscx";
    QFile invalid(invalidPath);
    ASSERT_TRUE(invalid.open(QIODevice::WriteOnly));
    ASSERT_EQ(invalid.write(disguised), disguised.size());
    invalid.close();
    std::unique_ptr<MasterScore> refused(ScoreRW::readScore(muse::String::fromQString(invalidPath), true));
    EXPECT_FALSE(refused);
}

TEST_F(MeloUiModelTests, NewlyAuthoredPresetGetsOneSpelledDefaultAndImmediateRepeatableEditing)
{
    global->setCurrentNotation(nullptr);
    score.reset(ScoreRW::readScore(muse::String::fromUtf8(MELO_UI_TEST_DATA_ROOT) + u"/note_data/empty.mscx", true));
    ASSERT_TRUE(score);
    ASSERT_TRUE(score->metaTag(melo::REFERENCE_TIMELINE_TAG).isEmpty());
    const auto* preset = StaffType::preset(StaffTypes::MELO_12TET);
    ASSERT_TRUE(preset);
    muse::String configuration, error;
    ASSERT_TRUE(melo::storedStaffConfiguration(preset->meloStateJson(), configuration, error)) << error.toStdString();
    for (Staff* staff : score->staves()) {
        staff->setStaffType(Fraction(0, 1), *preset);
    }
    const int undo = score->undoStack()->size();
    ASSERT_TRUE(melo::initializeNewMeloComposition(score.get(), error)) << error.toStdString();
    const auto root = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
    const auto initial = QJsonDocument::fromJson(root.toQString().toUtf8()).object()["initial"].toObject();
    EXPECT_EQ(initial["step"].toString(), "D");
    EXPECT_EQ(initial["alter"].toInt(), 0);
    EXPECT_EQ(initial["octave"].toInt(), 4);
    EXPECT_EQ(score->undoStack()->size(), undo);
    ASSERT_TRUE(melo::initializeNewMeloComposition(score.get(), error));
    EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), root);
    for (const auto& pitch : { u"D4", u"B♭3" }) {
        const auto state = score->staff(0)->staffType(Fraction(0, 1))->meloStateJson();
        ASSERT_TRUE(melo::changeInitialTonicPitch(score.get(), 0, Fraction(0, 1), 0, pitch, state,
                                                  score->metaTag(melo::REFERENCE_TIMELINE_TAG), error)) << error.toStdString();
        ASSERT_TRUE(melo::validateLatticeContent(score.get(), error)) << error.toStdString();
    }
    EXPECT_EQ(score->undoStack()->size(), undo + 2);
}

TEST_F(MeloUiModelTests, AddingAnInstrumentInheritsExistingRelativeAndConfigurationHistory)
{
    for (const double generator : { 700.0, 4800.0 / 7.0, 720.0 }) {
        SCOPED_TRACE(generator);
        global->setCurrentNotation(nullptr);
        score.reset(ScoreRW::readScore(muse::String::fromUtf8(MELO_UI_TEST_DATA_ROOT) + u"/jimstaff_data/m9-satb-hymn.mscx", true));
        ASSERT_TRUE(score);
        ASSERT_TRUE(TestUtils::createPart(score.get()));
        authorKnownD4Reference();
        melo::TuningController tuning(score.get(), 0);
        ASSERT_TRUE(tuning.beginPreview());
        ASSERT_TRUE(tuning.commit(generator));
        Measure* changed = score->firstMeasure()->nextMeasure();
        const Fraction tick = changed->tick();
        muse::String error;
        ASSERT_TRUE(melo::changeRelativeKey(score.get(), 0, tick, u"{\"nPer\":-1,\"nGen\":3}", score->metaTag(melo::REFERENCE_TIMELINE_TAG),
                                            error));
        ASSERT_TRUE(melo::applyChangeToAllMeloParts(score.get(), changed, { u"mode:3" }, error));
        const auto root = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
        struct GetScore : notation::IGetScore {
            explicit GetScore(Score* value)
                : value(value) {}
            Score* score() const override { return value; }
            muse::async::Notification scoreInited() const override { return {}; }
            Score* value;
        } getScore(score.get());
        auto interaction = std::make_shared<testing::NiceMock<notation::NotationInteractionMock> >();
        auto undoStack = std::make_shared<notation::NotationUndoStack>(&getScore, muse::async::Notification());
        auto noteInput = std::make_shared<notation::NotationNoteInput>(&getScore, interaction.get(), undoStack,
                                                                       muse::modularity::globalCtx());
        ON_CALL(*interaction, noteInput()).WillByDefault(testing::Return(noteInput));
        notation::MasterNotationParts parts(&getScore, interaction, undoStack);
        notation::PartInstrumentList choices;
        for (const Part* part : score->parts()) {
            notation::PartInstrument choice;
            choice.isExistingPart = true;
            choice.partId = part->id();
            choices.push_back(choice);
        }
        notation::PartInstrument added;
        added.instrumentTemplate.id = u"test-melo-voice";
        added.instrumentTemplate.trackName = u"New voice";
        added.instrumentTemplate.staffCount = 1;
        added.instrumentTemplate.staffGroup = StaffGroup::STANDARD;
        added.instrumentTemplate.staffTypePreset = StaffType::preset(StaffTypes::MELO_12TET);
        choices.push_back(added);
        const size_t count = score->nstaves();
        const int undo = score->undoStack()->size();
        parts.setParts(choices, score->scoreOrder());
        ASSERT_EQ(score->nstaves(), count + 1);
        EXPECT_EQ(score->undoStack()->size(), undo + 1);
        EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), root);
        for (const Fraction at : { Fraction(0, 1), tick }) {
            muse::String sourceConfig, addedConfig;
            ASSERT_TRUE(melo::staffConfiguration(score->staff(0)->staffType(at)->meloStateJson(), sourceConfig, error));
            ASSERT_TRUE(melo::staffConfiguration(score->staff(count)->staffType(at)->meloStateJson(), addedConfig, error));
            auto a = QJsonDocument::fromJson(sourceConfig.toQString().toUtf8()).object();
            auto b = QJsonDocument::fromJson(addedConfig.toQString().toUtf8()).object();
            a.remove("extent");
            b.remove("extent");
            EXPECT_EQ(a, b);
        }
        ASSERT_TRUE(melo::validateLatticeContent(score.get(), error)) << error.toStdString();
        EditData data;
        score->undoStack()->undo(&data);
        EXPECT_EQ(score->nstaves(), count);
        EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), root);
        score->undoStack()->redo(&data);
        EXPECT_EQ(score->nstaves(), count + 1);
        EXPECT_TRUE(melo::validateLatticeContent(score.get(), error)) << error.toStdString();
    }
}

TEST_F(MeloUiModelTests, StaffPropertiesAndTypeActionsInitializeCanonicalReferenceAtomically)
{
    for (const bool useProperties : { false, true }) {
        global->setCurrentNotation(nullptr);
        score.reset(ScoreRW::readScore(muse::String::fromUtf8(MELO_UI_TEST_DATA_ROOT) + u"/note_data/empty.mscx", true));
        ASSERT_TRUE(score);
        struct GetScore : notation::IGetScore {
            explicit GetScore(Score* value)
                : value(value) {}
            Score* score() const override { return value; }
            muse::async::Notification scoreInited() const override { return {}; }
            Score* value;
        } getScore(score.get());
        auto interaction = std::make_shared<testing::NiceMock<notation::NotationInteractionMock> >();
        auto undoStack = std::make_shared<notation::NotationUndoStack>(&getScore, muse::async::Notification());
        auto noteInput = std::make_shared<notation::NotationNoteInput>(&getScore, interaction.get(), undoStack,
                                                                       muse::modularity::globalCtx());
        ON_CALL(*interaction, noteInput()).WillByDefault(testing::Return(noteInput));
        notation::MasterNotationParts parts(&getScore, interaction, undoStack);
        const auto id = score->staff(0)->id();
        const int undo = score->undoStack()->size();
        if (useProperties) {
            auto config = parts.staffConfig(id);
            config.staffType = *StaffType::preset(StaffTypes::MELO_12TET);
            parts.setStaffConfig(id, config);
        } else {
            parts.setStaffType(id, StaffTypes::MELO_12TET);
        }
        ASSERT_TRUE(score->staff(0)->staffType(Fraction(0, 1))->isMelo());
        EXPECT_FALSE(score->metaTag(melo::REFERENCE_TIMELINE_TAG).isEmpty());
        muse::String error;
        EXPECT_TRUE(melo::validateLatticeContent(score.get(), error)) << error.toStdString();
        EXPECT_EQ(score->undoStack()->size(), undo + 1);
        EditData data;
        score->undoStack()->undo(&data);
        EXPECT_FALSE(score->staff(0)->staffType(Fraction(0, 1))->isMelo());
        EXPECT_TRUE(score->metaTag(melo::REFERENCE_TIMELINE_TAG).isEmpty());
        score->undoStack()->redo(&data);
        EXPECT_TRUE(melo::validateLatticeContent(score.get(), error)) << error.toStdString();
        for (const double generator : { 700.0, 4800.0 / 7.0, 720.0 }) {
            melo::TuningController tuning(score.get(), 0);
            ASSERT_TRUE(tuning.beginPreview());
            ASSERT_TRUE(tuning.commit(generator));
            const Fraction tick = score->firstMeasure()->nextMeasure()->tick();
            ASSERT_TRUE(melo::changeRelativeKey(score.get(), 0, tick, u"{\"nPer\":-1,\"nGen\":3}",
                                                score->metaTag(melo::REFERENCE_TIMELINE_TAG), error));
            const auto root = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
            const auto base = score->staff(0)->staffType(Fraction(0, 1))->meloStateJson();
            const auto later = score->staff(0)->staffType(tick)->meloStateJson();
            if (useProperties) {
                auto config = parts.staffConfig(id);
                config.staffType = *StaffType::preset(StaffTypes::MELO_12TET);
                parts.setStaffConfig(id, config);
            } else {
                parts.setStaffType(id, StaffTypes::MELO_12TET);
            }
            EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), root);
            EXPECT_EQ(score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), base);
            EXPECT_EQ(score->staff(0)->staffType(tick)->meloStateJson(), later);
            EXPECT_TRUE(melo::validateLatticeContent(score.get(), error)) << error.toStdString();
        }
    }
}

TEST_F(MeloUiModelTests, NewCompositionInitializationRefusesLegacyReferenceWithoutMutation)
{
    auto legacy = QJsonDocument::fromJson(score->staff(0)->staffType(Fraction(0,
                                                                              1))->meloStateJson().toQString().toUtf8()).object()[
        "configuration"].toObject();
    legacy.remove("schema");
    legacy["reference"] = QJsonObject { { "reference-pitch", QJsonObject { { "key_number", 62 } } } };
    score->staff(0)->staffType(Fraction(0,
                                        1))->setMeloStateJson(muse::String::fromUtf8(QJsonDocument(legacy).toJson(
                                                                                         QJsonDocument::Compact).constData()));
    score->setMetaTag(melo::REFERENCE_TIMELINE_TAG, muse::String());
    const auto state = score->staff(0)->staffType(Fraction(0, 1))->meloStateJson();
    const auto root = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
    ASSERT_TRUE(root.isEmpty());
    const int undo = score->undoStack()->size();
    muse::String error;
    EXPECT_FALSE(melo::initializeNewMeloComposition(score.get(), error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_EQ(score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), state);
    EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), root);
    EXPECT_EQ(score->undoStack()->size(), undo);
}

TEST_F(MeloUiModelTests, NativeWriterRefusesStaleDisposableReferenceBeforeWriting)
{
    authorKnownD4Reference();
    const auto original = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
    const auto state = score->staff(0)->staffType(Fraction(0, 1))->meloStateJson();
    muse::String revised, error;
    ASSERT_TRUE(melo::editHeaderPitch(state, u"staff-header-tonic-pitch", 0, u"D4", revised, error));
    ASSERT_NE(revised, original);
    score->setMetaTag(melo::REFERENCE_TIMELINE_TAG, revised);
    const size_t undoSize = score->undoStack()->size();
    EXPECT_FALSE(melo::validateLatticeContent(score.get(), error));
    EXPECT_FALSE(error.isEmpty());
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    EXPECT_FALSE(ScoreRW::saveScore(score.get(), muse::String::fromQString(temporary.path() + "/stale.mscx")));
    EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), revised);
    EXPECT_EQ(score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), state);
    EXPECT_EQ(score->undoStack()->size(), undoSize);
    score->setMetaTag(melo::REFERENCE_TIMELINE_TAG, original);
    EXPECT_TRUE(melo::validateLatticeContent(score.get(), error));
}

TEST_F(MeloUiModelTests, SpelledPitchGlyphsAlignWithTextAndMergedChangeLabelsFitTheirEnclosure)
{
    authorKnownD4Reference();
    const auto font = score->engravingFont();
    ASSERT_TRUE(font);
    muse::draw::Font textFont(u"Edwin", muse::draw::Font::Type::Text);
    textFont.setPointSizeF(9.0);
    muse::draw::FontMetrics metrics(textFont);
    for (const auto& text : { u"B♯3: Do Di", u"D♭4: La", u"F𝄪4: La", u"B𝄫3: La" }) {
        const auto layout = melo::pitchLabelLayout(text, textFont, font);
        const auto glyph = font->bbox(layout.parts.accidental, layout.accidentalMag)
                           .translated(muse::PointF(layout.accidentalX, layout.accidentalY));
        EXPECT_NEAR(glyph.center().y(), metrics.boundingRect(u"B").center().y(), 1e-6);
        EXPECT_GE(glyph.left(), metrics.boundingRect(layout.parts.beforeAccidental).right());
        EXPECT_GE(layout.afterX, glyph.right());
    }
    melo::TuningController tuning(score.get(), 0);
    ASSERT_TRUE(tuning.beginPreview());
    ASSERT_TRUE(tuning.commit(4800.0 / 7.0));
    const Fraction tick = score->firstMeasure()->nextMeasure()->tick();
    muse::String error;
    ASSERT_TRUE(melo::changeRelativeKey(score.get(), 0, tick, u"{\"nPer\":-4,\"nGen\":7}", score->metaTag(melo::REFERENCE_TIMELINE_TAG),
                                        error));
    melo::RelativeKeyEditor editor;
    ASSERT_TRUE(melo::prepareRelativeKeyEditor(score.get(), 0, tick, nullptr, editor, error));
    melo::ChangeIndicator indicator;
    ASSERT_TRUE(melo::changeIndicator(editor.sourceState, editor.destinationState, indicator));
    ASSERT_EQ(indicator.dotStacks.size(), 1u);
    const StaffType* type = score->staff(0)->staffType(tick);
    const auto geometry = melo::changeTerrainGeometry(type, score->firstMeasure()->spatium(), score->style().defaultSpatium(), indicator);
    melo::TonicPitchLabel pitch;
    ASSERT_TRUE(melo::tonicPitchLabel(editor.destinationState, pitch));
    muse::String text = pitch.label + u": ";
    for (const auto& member : indicator.dotStacks.front().members) {
        text += member.label + u" ";
    }
    text = text.trimmed();
    textFont.setPointSizeF(9.0 * score->firstMeasure()->spatium() / score->style().defaultSpatium());
    const auto label = melo::pitchLabelLayout(text, textFont, font);
    EXPECT_GE(geometry.changeLabelBand, label.bounds.width() + 0.25 * score->firstMeasure()->spatium());
}

TEST_F(MeloUiModelTests, DoAndLaHeaderPitchInkAndEditTargetClearTheClefInAllThreeTunings)
{
    for (const auto& tonicName : { u"Do", u"La" }) {
        SCOPED_TRACE(muse::String(tonicName).toStdString());
        for (int alteration : { 0, -2, 2 }) {
            for (const auto& ambit : { "tonic-bounded", "tonic-centered" }) {
                SCOPED_TRACE(alteration);
                SCOPED_TRACE(ambit);
                global->setCurrentNotation(nullptr);
                score.reset(ScoreRW::readScore(muse::String::fromUtf8(MELO_UI_TEST_DATA_ROOT) + u"/jimstaff_data/m9-satb-hymn.mscx", true));
                ASSERT_TRUE(score);
                authorKnownD4Reference();
                melo::StateChangeOptions options;
                ASSERT_TRUE(melo::changeOptions(score.get(), 0, score->firstMeasure(), options));
                const auto tonicOption = std::find_if(options.tonics.begin(), options.tonics.end(), [&](const auto& option) {
                    return option.label == tonicName;
                });
                ASSERT_NE(tonicOption, options.tonics.end());
                muse::String error;
                if (muse::String(tonicName) != u"Do") {
                    ASSERT_TRUE(melo::applyChangeToAllMeloParts(score.get(), score->firstMeasure(), { tonicOption->id },
                                                                error)) << error.toStdString();
                }
                auto root = QJsonDocument::fromJson(score->metaTag(melo::REFERENCE_TIMELINE_TAG).toQString().toUtf8()).object();
                auto initial = root["initial"].toObject();
                initial["alter"] = alteration;
                root["initial"] = initial;
                score->setMetaTag(melo::REFERENCE_TIMELINE_TAG,
                                  muse::String::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact).constData()));
                for (Staff* staff : score->staves()) {
                    StaffType* type = staff->staffType(Fraction(0, 1));
                    muse::String stored;
                    ASSERT_TRUE(melo::staffConfiguration(type->meloStateJson(), stored, error));
                    auto configuration = QJsonDocument::fromJson(stored.toQString().toUtf8()).object();
                    configuration["tonic_ambit"] = ambit;
                    type->setMeloStateJson(muse::String::fromUtf8(QJsonDocument(configuration).toJson(QJsonDocument::Compact).constData()));
                }
                ASSERT_TRUE(melo::rebuildCanonicalReferenceContexts(score.get(), error)) << error.toStdString();
                for (const auto& tuning : std::vector<std::pair<int, double> > { { 12, 700.0 }, { 7, 4800.0 / 7.0 }, { 5, 720.0 } }) {
                    SCOPED_TRACE(tuning.first);
                    melo::TuningController controller(score.get(), 0);
                    ASSERT_TRUE(controller.beginPreview());
                    ASSERT_TRUE(controller.commit(tuning.second));
                    score->doLayout();
                    StaffLines* lines = score->firstMeasure()->staffLines(0);
                    ASSERT_TRUE(lines);
                    const StaffType* type = score->staff(0)->staffType(Fraction(0, 1));
                    const auto& view = type->meloFrameView(score.get(), 0, score->firstMeasure()->system());
                    const auto geometry = type->meloHeaderGeometry(lines->spatium(), score->style().defaultSpatium(), &view);
                    const double clefLeft = lines->pos().x() - 0.3 * lines->spatium() - geometry.clefRx;
                    const double dotCenter = clefLeft - geometry.rightLabelBand - geometry.indicatorW;
                    melo::PeriodicOrigins origins;
                    ASSERT_TRUE(melo::periodicOrigins(type->meloStateJson(), origins));
                    double tonic = 0;
                    ASSERT_TRUE(melo::tonicCentsAboveDo(type->meloStateJson(), tonic));
                    ASSERT_FALSE(view.empty());
                    ASSERT_FALSE(lines->meloGuideLines().empty());
                    auto provider = std::make_shared<muse::draw::BufferedPaintProvider>();
                    muse::draw::Painter recorder(provider, "La-header-clearance");
                    recorder.setViewport(muse::RectF(0, 0, 4000, 4000));
                    rendering::PaintOptions paint;
                    lines->renderer()->drawItem(lines, &recorder, paint);
                    recorder.endDraw();
                    ASSERT_FALSE(lines->meloHeaderPitchTargets().empty()) << type->meloStateJson().toStdString()
                                                                          << " origins " << origins.doCentsAboveExtentLower << "," <<
                        origins.tonicCentsAboveExtentLower
                                                                          << " tonic " << tonic << " band " <<
                        view.bands.front().labelPeriodIndex << ","
                                                                          << view.bands.front().tonicLabel.toStdString();
                    muse::RectF bounds;
                    for (const auto& target : lines->meloHeaderPitchTargets()) {
                        if (muse::String(tonicName) == u"Do") {
                            EXPECT_GT(target.ink.left(), dotCenter + geometry.indicatorW);
                            EXPECT_LT(target.ink.right(), clefLeft + geometry.clefRx);
                        } else {
                            EXPECT_LT(target.ink.right(), clefLeft);
                            EXPECT_LT(target.ink.right(), dotCenter - geometry.indicatorW);
                        }
                        melo::HeaderPitchContext hit;
                        EXPECT_TRUE(melo::findHeaderPitch(score.get(), target.ink.center() + lines->canvasPos(), hit));
                        EXPECT_EQ(hit.periodIndex, target.periodIndex);
                        bounds.unite(target.ink);
                    }
                    // Render the actual header at two scales without bringing any window forward.
                    const double top = lines->pos().y() + type->meloYFromCents(view.topCents(), view) * lines->spatium();
                    const double bottom = lines->pos().y() + type->meloYFromCents(view.bottomCents(), view) * lines->spatium();
                    bounds.unite(muse::RectF(clefLeft, top, geometry.clefRx + lines->spatium(), bottom - top));
                    const double labelLeft = dotCenter - geometry.indicatorW - geometry.leftLabelBand;
                    bounds.unite(muse::RectF(labelLeft, top, clefLeft + geometry.clefRx - labelLeft, bottom - top));
                    const double pad = 2 * lines->spatium();
                    bounds.adjust(-pad, -3 * pad, pad, pad);
                    for (int zoom : { 1, 2 }) {
                        QImage image(600 * zoom, 300 * zoom, QImage::Format_ARGB32_Premultiplied);
                        image.fill(Qt::white);
                        QPainter qp(&image);
                        const double scale = std::min(image.width() / bounds.width(), image.height() / bounds.height());
                        qp.scale(scale, scale);
                        qp.translate(-bounds.left(), -bounds.top());
                        {
                            muse::draw::Painter painter(&qp, "La-header-clearance-render");
                            lines->renderer()->drawItem(lines, &painter, paint);
                        }
                        qp.end();
                        const QString output = qEnvironmentVariable("MELO_REFERENCE_ARTIFACT_DIR");
                        if (!output.isEmpty()) {
                            ASSERT_TRUE(image.save(output + QString("/%1-header-%2-tet-%3-%4-%5x.png")
                                                   .arg(muse::String(tonicName).toQString().toLower()).arg(tuning.first).arg(
                                                       alteration).arg(ambit).arg(zoom)));
                        }
                    }
                }
            }
        }
    }
}

TEST_F(MeloUiModelTests, PaintedHeaderOpensThroughDoubleClickAndEnterWithoutEditingOrdinaryText)
{
    authorKnownD4Reference();
    score->doLayout();
    StaffLines* lines = score->firstMeasure()->staffLines(0);
    ASSERT_TRUE(lines);
    auto provider = std::make_shared<muse::draw::BufferedPaintProvider>();
    muse::draw::Painter painter(provider, "reference-header-input");
    painter.setViewport(muse::RectF(0, 0, 4000, 4000));
    engraving::rendering::PaintOptions options;
    lines->renderer()->drawItem(lines, &painter, options);
    painter.endDraw();
    ASSERT_FALSE(lines->meloHeaderPitchTargets().empty());
    const auto painted = lines->meloHeaderPitchTargets().front();
    const muse::PointF point = painted.ink.center() + lines->canvasPos();
    melo::HeaderPitchContext hit;
    ASSERT_TRUE(melo::findHeaderPitch(score.get(), point, hit));
    EXPECT_EQ(hit.label, painted.label);
    const auto root = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
    const int undo = score->undoStack()->size();
    testing::NiceMock<notation::ControlledViewMock> view;
    QQuickItem viewItem;
    auto interaction = std::make_shared<testing::NiceMock<notation::NotationInteractionMock> >();
    auto dispatcher = std::make_shared<testing::NiceMock<muse::actions::ActionsDispatcherMock> >();
    notation::INotationInteraction::HitElementContext emptyHit;
    ON_CALL(*interaction, hitElementContext()).WillByDefault(testing::ReturnRef(emptyHit));
    ON_CALL(view, notationInteraction()).WillByDefault(testing::Return(interaction));
    ON_CALL(view, asItem()).WillByDefault(testing::Return(&viewItem));
    ON_CALL(view, toLogical(testing::An<const QPointF&>())).WillByDefault(testing::Return(point));
    notation::NotationViewInputController controller(&view, muse::modularity::globalCtx());
    controller.globalContext.set(global);
    controller.dispatcher.set(dispatcher);
    controller.playbackController.set(playback);
    EXPECT_CALL(*dispatcher, dispatch(muse::actions::ActionCode("melo-edit-initial-pitch"), testing::_)).Times(2);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(100, 100), QPointF(100, 100), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    controller.mousePressEvent(&press);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    controller.keyPressEvent(&enter);
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(100, 100), QPointF(100, 100), Qt::LeftButton, Qt::LeftButton,
                            Qt::NoModifier);
    controller.mouseDoubleClickEvent(&doubleClick);
    testing::Mock::VerifyAndClearExpectations(dispatcher.get());
    EXPECT_CALL(*dispatcher, dispatch(muse::actions::ActionCode("melo-edit-initial-pitch"), testing::_)).Times(0);
    // Ordinary and key-change text never registers a header target. Even the
    // same screen point and pitch spelling cannot activate after its owner is absent.
    lines->clearMeloHeaderPitchTargets();
    EXPECT_FALSE(melo::findHeaderPitch(score.get(), point, hit));
    controller.mouseDoubleClickEvent(&doubleClick);
    EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), root);
    EXPECT_EQ(score->undoStack()->size(), undo);
}

TEST_F(MeloUiModelTests, HeaderKeyboardFocusAnnouncesAndActivatesWithoutMouseAndRejectsStaleContext)
{
    authorKnownD4Reference();
    score->doLayout();
    auto paintHeaders = [&]() {
        auto provider = std::make_shared<muse::draw::BufferedPaintProvider>();
        muse::draw::Painter painter(provider, "header-keyboard-navigation");
        painter.setViewport(muse::RectF(0, 0, 4000, 4000));
        for (Measure* measure = score->firstMeasure(); measure; measure = measure->nextMeasure()) {
            for (staff_idx_t index = 0; index < score->nstaves(); ++index) {
                StaffLines* lines = measure->staffLines(index);
                if (lines) {
                    lines->renderer()->drawItem(lines, &painter, engraving::rendering::PaintOptions());
                }
            }
        }
        painter.endDraw();
    };
    paintHeaders();
    const auto targets = melo::headerPitchTargets(score.get());
    ASSERT_FALSE(targets.empty());
    const auto root = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
    const auto undo = score->undoStack()->currentIndex();
    testing::NiceMock<notation::ControlledViewMock> view;
    QQuickItem viewItem;
    auto interaction = std::make_shared<testing::NiceMock<notation::NotationInteractionMock> >();
    auto dispatcher = std::make_shared<testing::NiceMock<muse::actions::ActionsDispatcherMock> >();
    auto announcements = std::make_shared<TestAnnouncements>();
    ON_CALL(view, notationInteraction()).WillByDefault(testing::Return(interaction));
    ON_CALL(view, asItem()).WillByDefault(testing::Return(&viewItem));
    notation::NotationViewInputController controller(&view, muse::modularity::globalCtx());
    controller.globalContext.set(global);
    controller.dispatcher.set(dispatcher);
    controller.accessibilityController.set(announcements);
    controller.playbackController.set(playback);
    for (const auto& expected : targets) {
        ASSERT_TRUE(controller.focusMeloHeaderPitch(true));
        EXPECT_FALSE(controller.focusedMeloHeaderPitchInk().isEmpty());
        EXPECT_TRUE(announcements->announcement().contains(expected.label.toQString()));
        EXPECT_TRUE(announcements->announcement().contains("Middle C = C4"));
        QKeyEvent override (QEvent::ShortcutOverride, Qt::Key_Return, Qt::NoModifier);
        EXPECT_TRUE(controller.shortcutOverrideEvent(&override));
        EXPECT_CALL(*dispatcher, dispatch(muse::actions::ActionCode("melo-edit-initial-pitch"), testing::_))
        .WillOnce([&](const auto&, const muse::actions::ActionData& data) {
            ASSERT_EQ(data.count(), 2);
            const auto target = data.arg<melo::HeaderPitchContext>(0);
            EXPECT_EQ(target.staffIdx, expected.staffIdx);
            EXPECT_EQ(target.tick, expected.tick);
            EXPECT_EQ(target.periodIndex, expected.periodIndex);
            EXPECT_EQ(data.arg<muse::String>(1), root);
        });
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        controller.keyPressEvent(&enter);
        testing::Mock::VerifyAndClearExpectations(dispatcher.get());
    }
    // Paint coordinates may change; semantic focus survives only while the
    // same staff/position/period and canonical context still exist.
    score->setLayoutAll();
    score->doLayout();
    paintHeaders();
    EXPECT_FALSE(controller.focusedMeloHeaderPitchInk().isEmpty());
    muse::String changedRoot = root;
    changedRoot.replace(u"\"step\":\"D\"", u"\"step\":\"E\"");
    score->setMetaTag(melo::REFERENCE_TIMELINE_TAG, changedRoot);
    EXPECT_TRUE(controller.focusedMeloHeaderPitchInk().isEmpty());
    EXPECT_CALL(*dispatcher, dispatch(muse::actions::ActionCode("melo-edit-initial-pitch"), testing::_)).Times(0);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    controller.keyPressEvent(&enter);
    EXPECT_TRUE(announcements->announcement().contains("no longer available"));
    score->setMetaTag(melo::REFERENCE_TIMELINE_TAG, root);
    EXPECT_EQ(score->undoStack()->currentIndex(), undo);
    controller.setReadonly(true);
    EXPECT_FALSE(controller.focusMeloHeaderPitch(true));
    controller.setReadonly(false);
    ON_CALL(view, isNoteEnterMode()).WillByDefault(testing::Return(true));
    EXPECT_FALSE(controller.focusMeloHeaderPitch(true));
}

TEST_F(MeloUiModelTests, PaintedKeyChangePitchMatchingHeaderRejectsDoubleClickAndEnter)
{
    for (const double generator : { 700.0, 4800.0 / 7.0, 720.0 }) {
        SCOPED_TRACE(generator);
        score.reset(ScoreRW::readScore(muse::String::fromUtf8(MELO_UI_TEST_DATA_ROOT) + u"/jimstaff_data/m9-satb-hymn.mscx", true));
        ASSERT_TRUE(score);
        authorKnownD4Reference();
        melo::TuningController tuning(score.get(), 0);
        ASSERT_TRUE(tuning.beginPreview());
        ASSERT_TRUE(tuning.commit(generator));
        // Keep the soprano's C4 row present in every tuning. The frame now
        // fits actual notes on each system rather than a stored broad extent.
        Chord* firstChord = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
        ASSERT_TRUE(firstChord);
        ASSERT_FALSE(firstChord->notes().empty());
        firstChord->notes().front()->setMeloPitch(1, -2);
        muse::String error;
        const Fraction at(1, 2);
        ASSERT_TRUE(melo::changeRelativeKey(score.get(), 0, at, u"{\"nPer\":-1,\"nGen\":0}",
                                            score->metaTag(melo::REFERENCE_TIMELINE_TAG), error)) << error.toStdString();
        score->doLayout();
        StaffLines* header = score->firstMeasure()->staffLines(0);
        StaffLines* alto = score->firstMeasure()->staffLines(1);
        ASSERT_TRUE(header);
        ASSERT_TRUE(alto);
        auto headerProvider = std::make_shared<muse::draw::BufferedPaintProvider>();
        muse::draw::Painter headerPainter(headerProvider, "matching-header");
        headerPainter.setWindow(muse::RectF(0, 0, 4000, 4000));
        headerPainter.setViewport(muse::RectF(0, 0, 4000, 4000));
        engraving::rendering::PaintOptions options;
        header->renderer()->drawItem(header, &headerPainter, options);
        alto->renderer()->drawItem(alto, &headerPainter, options);
        headerPainter.endDraw();
        ASSERT_FALSE(header->meloHeaderPitchTargets().empty());
        const auto matchingHeader = std::find_if(header->meloHeaderPitchTargets().begin(), header->meloHeaderPitchTargets().end(),
                                                 [](const auto& candidate) { return candidate.label == u"C4"; });
        ASSERT_NE(matchingHeader, header->meloHeaderPitchTargets().end());
        const auto target = *matchingHeader;
        melo::HeaderPitchContext hit;
        ASSERT_TRUE(melo::findHeaderPitch(score.get(), target.ink.center() + header->canvasPos(), hit));

        const auto carriers = melo::changeCarriers(score->firstMeasure(), 1);
        ASSERT_EQ(carriers.size(), 1);
        melo::ChangeIndicator indicator;
        const StaffType* incoming = nullptr;
        ASSERT_TRUE(melo::midBarChangeIndicator(carriers.front(), indicator, &incoming));
        ASSERT_TRUE(incoming);
        const StaffType* displayed = score->staff(1)->staffType(Fraction(0, 1));
        const Segment* anchor = score->firstMeasure()->findSegmentR(Segment::CHORD_REST_OR_TIME_TICK_TYPE, at);
        ASSERT_TRUE(anchor);
        const double width = melo::changeTerrainGeometry(incoming, alto->spatium(), score->style().defaultSpatium(),
                                                         indicator).changeTerrainWidth;
        const double x = anchor->x() - width - alto->style().styleMM(Sid::barNoteDistance);
        auto provider = std::make_shared<muse::draw::BufferedPaintProvider>();
        muse::draw::Painter painter(provider, "matching-key-change");
        painter.setWindow(muse::RectF(0, 0, 4000, 4000));
        painter.setViewport(muse::RectF(0, 0, 4000, 4000));
        engraving::rendering::score::TDraw::drawMeloChangeTerrain(alto, &painter, options, indicator, incoming, displayed, x,
                                                                  engraving::rendering::score::TDraw::ChangePlacement::MID_BAR);
        painter.endDraw();
        const auto drawing = provider->drawData();
        std::vector<muse::PointF> annotationPoints;
        QStringList paintedTexts;
        std::function<void(const muse::draw::DrawData::Item&)> inspect = [&](const muse::draw::DrawData::Item& item) {
            for (const auto& data : item.datas) {
                const auto& state = drawing->states.at(data.state);
                for (const auto& text : data.texts) {
                    paintedTexts << text.text.toQString();
                    if (!text.text.startsWith(target.label + u":")) {
                        continue;
                    }
                    const auto ink = melo::pitchLabelLayout(target.label, state.font, score->engravingFont()).bounds;
                    ASSERT_TRUE(std::isfinite(state.transform.m11()));
                    ASSERT_TRUE(std::isfinite(state.transform.m22()));
                    annotationPoints.push_back(state.transform.map(ink.translated(text.rect.topLeft())).center() + alto->canvasPos());
                }
            }
            for (const auto& child : item.chilren) {
                inspect(child);
            }
        };
        inspect(drawing->item);
        ASSERT_FALSE(annotationPoints.empty()) << "The actual key-change terrain must paint the same pitch text as the header: "
                                               << target.label.toStdString() << " versus " << paintedTexts.join("|").toStdString();
        const auto root = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
        const int undo = score->undoStack()->size();
        testing::NiceMock<notation::ControlledViewMock> view;
        ON_CALL(view, currentScaling()).WillByDefault(testing::Return(1.0));
        QQuickItem viewItem;
        auto interaction = std::make_shared<testing::NiceMock<notation::NotationInteractionMock> >();
        auto selection = std::make_shared<testing::NiceMock<notation::NotationSelectionMock> >();
        const std::vector<EngravingItem*> selectedElements;
        ON_CALL(*selection, elements()).WillByDefault(testing::ReturnRef(selectedElements));
        ON_CALL(*selection, isNone()).WillByDefault(testing::Return(true));
        ON_CALL(*interaction, selection()).WillByDefault(testing::Return(selection));
        auto dispatcher = std::make_shared<testing::NiceMock<muse::actions::ActionsDispatcherMock> >();
        notation::INotationInteraction::HitElementContext emptyHit;
        ON_CALL(*interaction, hitElementContext()).WillByDefault(testing::ReturnRef(emptyHit));
        ON_CALL(view, notationInteraction()).WillByDefault(testing::Return(interaction));
        ON_CALL(view, asItem()).WillByDefault(testing::Return(&viewItem));
        notation::NotationViewInputController controller(&view, muse::modularity::globalCtx());
        controller.globalContext.set(global);
        controller.dispatcher.set(dispatcher);
        controller.playbackController.set(playback);
        auto configuration = std::make_shared<testing::NiceMock<notation::NotationConfigurationMock> >();
        ON_CALL(*configuration, selectionProximity()).WillByDefault(testing::Return(8));
        controller.configuration.set(configuration);
        EXPECT_CALL(*interaction, hitElement(testing::_, testing::FloatEq(4.0f)))
        .Times(annotationPoints.size()).WillRepeatedly(testing::Return(nullptr));
        EXPECT_CALL(*dispatcher, dispatch(muse::actions::ActionCode("melo-edit-initial-pitch"), testing::_)).Times(0);
        for (const auto& point : annotationPoints) {
            ASSERT_FALSE(melo::findHeaderPitch(score.get(), point, hit));
            ON_CALL(view, toLogical(testing::An<const QPointF&>())).WillByDefault(testing::Return(point));
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(100, 100), QPointF(100, 100), Qt::LeftButton, Qt::LeftButton,
                              Qt::NoModifier);
            controller.mousePressEvent(&press);
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            controller.keyPressEvent(&enter);
            QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(100, 100), QPointF(100, 100), Qt::LeftButton, Qt::LeftButton,
                                    Qt::NoModifier);
            controller.mouseDoubleClickEvent(&doubleClick);
        }
        ASSERT_FALSE(header->meloHeaderPitchTargets().empty());
        EXPECT_TRUE(melo::findHeaderPitch(score.get(), target.ink.center() + header->canvasPos(), hit));
        EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), root);
        EXPECT_EQ(score->undoStack()->size(), undo);
    }
}

TEST_F(MeloUiModelTests, InitialModeChangePropagatesAcrossRelativeOnlyEventsAndRemoval)
{
    for (const double generator : { 700.0, 4800.0 / 7.0, 720.0 }) {
        SCOPED_TRACE(generator);
        global->setCurrentNotation(nullptr);
        score.reset(ScoreRW::readScore(muse::String::fromUtf8(MELO_UI_TEST_DATA_ROOT) + u"/jimstaff_data/m9-satb-hymn.mscx", true));
        ASSERT_TRUE(score);
        ASSERT_TRUE(TestUtils::createPart(score.get()));
        authorKnownD4Reference();
        melo::TuningController tuning(score.get(), 0);
        ASSERT_TRUE(tuning.beginPreview());
        ASSERT_TRUE(tuning.commit(generator));
        muse::String error;
        const Fraction at = score->firstMeasure()->nextMeasure()->tick();
        const Fraction later = score->firstMeasure()->nextMeasure()->nextMeasure()->tick();
        ASSERT_TRUE(melo::changeRelativeKey(score.get(), 0, at, u"{\"nPer\":-1,\"nGen\":3}",
                                            score->metaTag(melo::REFERENCE_TIMELINE_TAG), error)) << error.toStdString();
        const auto root = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
        ASSERT_TRUE(melo::applyChangeToAllMeloParts(score.get(), score->tick2measure(later), { u"mode:1" }, error)) << error.toStdString();
        muse::String explicitConfiguration;
        ASSERT_TRUE(melo::staffConfiguration(score->staff(0)->staffType(later)->meloStateJson(), explicitConfiguration, error));
        const int explicitMode = QJsonDocument::fromJson(explicitConfiguration.toQString().toUtf8()).object()["mode_rotation"].toInt();
        const int beforeEdit = score->undoStack()->size();
        ASSERT_TRUE(melo::applyChangeToAllMeloParts(score.get(), score->firstMeasure(), { u"mode:3" }, error)) << error.toStdString();
        EXPECT_EQ(score->undoStack()->size(), beforeEdit + 1);
        for (Score* related : score->scoreList()) {
            for (Staff* staff : related->staves()) {
                for (const auto& tick : { Fraction(0, 1), at }) {
                    muse::String config;
                    ASSERT_TRUE(melo::staffConfiguration(staff->staffType(tick)->meloStateJson(), config, error));
                    EXPECT_EQ(QJsonDocument::fromJson(config.toQString().toUtf8()).object()["mode_rotation"].toInt(), 6);
                }
                muse::String config;
                ASSERT_TRUE(melo::staffConfiguration(staff->staffType(later)->meloStateJson(), config, error));
                EXPECT_EQ(QJsonDocument::fromJson(config.toQString().toUtf8()).object()["mode_rotation"].toInt(), explicitMode);
            }
        }
        EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), root);
        ASSERT_TRUE(melo::changeRelativeKey(score.get(), 0, at, u"{\"nPer\":0,\"nGen\":0}", root, error)) << error.toStdString();
        EXPECT_TRUE(melo::changeCarriers(score->tick2measure(at), 0).empty());
        score->undoRedo(true, nullptr);
        EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), root);
        ASSERT_TRUE(melo::changeCarrierAt(score->tick2measure(at), 0, at)->meloReferenceOnly());
        ASSERT_TRUE(melo::removeChange(score.get(), 0, score->tick2measure(later), later, error)) << error.toStdString();
        EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), root);
        for (Score* related : score->scoreList()) {
            for (Staff* staff : related->staves()) {
                muse::String config;
                ASSERT_TRUE(melo::staffConfiguration(staff->staffType(later)->meloStateJson(), config, error));
                EXPECT_EQ(QJsonDocument::fromJson(config.toQString().toUtf8()).object()["mode_rotation"].toInt(), 6);
            }
        }
    }
}

TEST_F(MeloUiModelTests, CanonicalReferenceRevisionIsWholePieceRepeatableAndUndoable)
{
    // Newly author the known C-major SATB fixture's reference as D4 on Re0.
    // This is explicit test provenance, not a legacy-file conversion policy.
    score.reset(ScoreRW::readScore(muse::String::fromUtf8(MELO_UI_TEST_DATA_ROOT) + u"/jimstaff_data/m9-satb-hymn.mscx", true));
    ASSERT_TRUE(score);
    ASSERT_EQ(score->nstaves(), 4);
    Score* linkedPart = TestUtils::createPart(score.get());
    ASSERT_TRUE(linkedPart);
    authorKnownD4Reference();
    muse::String error;
    const muse::String root = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
    const auto snapshot = [&]() {
        QStringList values { score->metaTag(melo::REFERENCE_TIMELINE_TAG).toQString() };
        for (Score* related : score->scoreList()) {
            for (Staff* staff : related->staves()) {
                values << staff->staffType(Fraction(0, 1))->meloStateJson().toQString();
                for (Measure* measure = related->firstMeasure(); measure; measure = measure->nextMeasure()) {
                    for (const StaffTypeChange* carrier : melo::changeCarriers(measure, staff->idx())) {
                        values << staff->staffType(carrier->tick())->meloStateJson().toQString();
                    }
                }
            }
            for (Segment* segment = related->firstSegment(SegmentType::ChordRest); segment;
                 segment = segment->next1(SegmentType::ChordRest)) {
                for (track_idx_t track = 0; track < related->ntracks(); ++track) {
                    EngravingItem* item = segment->element(track);
                    if (!item || !item->isChord()) {
                        continue;
                    }
                    for (Note* note : toChord(item)->notes()) {
                        values << QString("%1:%2:%3:%4:%5:%6").arg(note->meloNPer()).arg(note->meloNGen()).arg(note->pitch())
                            .arg(note->tpc1()).arg(note->tpc2()).arg(note->tuning(), 0, 'g', 17);
                    }
                }
            }
        }
        return values;
    };
    const QStringList original = snapshot();
    const int undo = score->undoStack()->size();
    const auto stateAt = [&](staff_idx_t staff, const Fraction& tick) { return score->staff(staff)->staffType(tick)->meloStateJson(); };
    const muse::String expected = stateAt(0, Fraction(0, 1));
    ASSERT_TRUE(melo::changeInitialTonicPitch(score.get(), 0, Fraction(0, 1), 0, u"D5", expected, root, error)) << error.toStdString();
    const QStringList changed = snapshot();
    EXPECT_NE(changed, original);
    EXPECT_EQ(score->undoStack()->size(), undo + 1);
    for (staff_idx_t staff = 0; staff < 4; ++staff) {
        melo::TonicPitchLabel label;
        ASSERT_TRUE(melo::tonicPitchLabel(stateAt(staff, Fraction(0, 1)), label));
        EXPECT_TRUE(label.label.startsWith(u"D"));
    }
    score->undoRedo(true, nullptr);
    EXPECT_EQ(snapshot(), original);
    score->undoRedo(false, nullptr);
    EXPECT_EQ(snapshot(), changed);
    EXPECT_FALSE(melo::changeInitialTonicPitch(score.get(), 0, Fraction(0, 1), 0, u"E5", expected, root, error));
    EXPECT_EQ(snapshot(), changed);
    const muse::String currentRoot = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
    const muse::String current = stateAt(0, Fraction(0, 1));
    ASSERT_TRUE(melo::changeInitialTonicPitch(score.get(), 0, Fraction(0, 1), 0, u"D5", current, currentRoot, error));
    EXPECT_EQ(score->undoStack()->size(), undo + 1);
    ASSERT_TRUE(melo::changeInitialTonicPitch(score.get(), 3, Fraction(0, 1), 0, u"C3", stateAt(3, Fraction(0, 1)), currentRoot,
                                              error)) << error.toStdString();
    EXPECT_EQ(snapshot(), original);
    const muse::String partState = linkedPart->staff(0)->staffType(Fraction(0, 1))->meloStateJson();
    melo::TonicPitchLabel partLabel;
    ASSERT_TRUE(melo::tonicPitchLabel(partState, partLabel));
    ASSERT_TRUE(melo::changeInitialTonicPitch(linkedPart, 0, Fraction(0, 1), 0,
                                              u"D" + partLabel.label.mid(1), partState, root, error)) << error.toStdString();
    EXPECT_EQ(snapshot(), changed);
    const Fraction modulationAt = score->firstMeasure()->nextMeasure()->tick();
    const muse::String beforeModulationRoot = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
    melo::SoundingPitch beforeSound;
    ASSERT_TRUE(melo::noteSoundingPitch(stateAt(0, Fraction(0, 1)), 0, 0, beforeSound));
    ASSERT_TRUE(melo::changeRelativeKey(score.get(), 0, modulationAt, u"{\"nPer\":-1,\"nGen\":3}", beforeModulationRoot,
                                        error)) << error.toStdString();
    const muse::String modulatedRoot = score->metaTag(melo::REFERENCE_TIMELINE_TAG);
    const QJsonArray events = QJsonDocument::fromJson(modulatedRoot.toQString().toUtf8()).object()["events"].toArray();
    ASSERT_EQ(events.size(), 1);
    for (Score* related : score->scoreList()) {
        for (Staff* staff : related->staves()) {
            melo::SoundingPitch afterSound;
            ASSERT_TRUE(melo::noteSoundingPitch(staff->staffType(modulationAt)->meloStateJson(), 0, 0, afterSound));
            EXPECT_NEAR(afterSound.frequencyHz / beforeSound.frequencyHz, std::pow(2.0, 900.0 / 1200.0), 1e-12);
            const auto& frame = staff->staffType(modulationAt)->meloFrameView(related, staff->idx(),
                                                                              related->tick2measure(modulationAt)->system());
            EXPECT_FALSE(frame.empty());
        }
    }
    melo::TonicPitchLabel laterLabel;
    ASSERT_TRUE(melo::tonicPitchLabel(stateAt(3, modulationAt), laterLabel));
    ASSERT_TRUE(laterLabel.label.startsWith(u"B"));
    const muse::String laterState = stateAt(3, modulationAt);
    ASSERT_TRUE(melo::changeInitialTonicPitch(score.get(), 3, modulationAt, 0, u"B♭" + laterLabel.label.mid(1), laterState, modulatedRoot,
                                              error)) << error.toStdString();
    EXPECT_EQ(QJsonDocument::fromJson(score->metaTag(melo::REFERENCE_TIMELINE_TAG).toQString().toUtf8()).object()["events"].toArray(),
              events);
    score->undoRedo(true, nullptr);
    EXPECT_EQ(score->metaTag(melo::REFERENCE_TIMELINE_TAG), modulatedRoot);
    score->undoRedo(true, nullptr);
    EXPECT_EQ(snapshot(), changed);
}

TEST_F(MeloUiModelTests, MeloChordCursorControlsUseExactDurationsWithoutEditingNotes) {
    selectMeasure(0);
    auto* segment = score->firstMeasure()->first(SegmentType::ChordRest);
    auto* harmony = new Harmony(segment);
    harmony->setTrack(0);
    harmony->setHarmonyType(HarmonyType::MELO);
    harmony->setHarmony(u"Do5");
    segment->add(harmony);
    repository.updateElementList({ harmony }, SelState::LIST);
    auto interaction = std::make_shared<testing::NiceMock<notation::NotationInteractionMock> >();
    notation->testInteraction = interaction;
    class CursorModel : public ChordSymbolSettingsModel
    {
    public:
        using ChordSymbolSettingsModel::ChordSymbolSettingsModel;
        void useHarmony(Harmony* item) { m_elementList = { item }; }
    };
    CursorModel model(nullptr, muse::modularity::globalCtx(), &repository);
    model.context.set(global);
    model.useHarmony(harmony);
    ASSERT_TRUE(model.hasMeloSelection());
    QQmlEngine engine;
    engine.addImportPath("qrc:/qt/qml");
    QQmlComponent component(&engine, QUrl("qrc:/qt/qml/MuseScore/Inspector/notation/chordsymbols/ChordSymbolSettings.qml"));
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    ASSERT_GE(model.metaObject()->indexOfMethod("advanceMeloChordCursor(int)"), 0);
    ON_CALL(*interaction, isTextEditingStarted()).WillByDefault(testing::Return(false));
    EXPECT_CALL(*interaction, startEditText(harmony, testing::_)).Times(3);
    EXPECT_CALL(*interaction, navigateToHarmony(Fraction(1, 4))).Times(1);
    EXPECT_CALL(*interaction, navigateToHarmony(Fraction(1, 8))).Times(1);
    EXPECT_CALL(*interaction, navigateToHarmony(Fraction(1, 16))).Times(1);
    ASSERT_TRUE(QMetaObject::invokeMethod(&model, "advanceMeloChordCursor", Q_ARG(int, 4)));
    ASSERT_TRUE(QMetaObject::invokeMethod(&model, "advanceMeloChordCursor", Q_ARG(int, 8)));
    ASSERT_TRUE(QMetaObject::invokeMethod(&model, "advanceMeloChordCursor", Q_ARG(int, 16)));
    ASSERT_TRUE(QMetaObject::invokeMethod(&model, "advanceMeloChordCursor", Q_ARG(int, 0)));
    ASSERT_TRUE(QMetaObject::invokeMethod(&model, "advanceMeloChordCursor", Q_ARG(int, 3)));
}
TEST_F(MeloUiModelTests, MeloAccidentalPickersUseTheMatchingNoteheads) {
    selectMeasure(0);
    QQmlEngine engine;
    muse::QmlIoCContext qmlIoc(&engine);
    qmlIoc.ctx = std::make_shared<muse::modularity::Context>(914);
    muse::modularity::ioc(qmlIoc.ctx)->registerExport<context::IGlobalContext>("melo-ui-test", global);
    engine.rootContext()->setContextProperty("ioc_context", &qmlIoc);
    notation::NoteInputBarModel model;
    QQmlEngine::setContextForObject(&model, engine.rootContext());
    const auto presentation = model.accidentalPresentation();
    muse::modularity::removeIoC(qmlIoc.ctx);
    ASSERT_EQ(presentation.value("font").value<QFont>().family(), QString("JiMSMusic"));
    const std::pair<const char*, SymId> cases[] = {
        { "sharp", SymId::noteheadTriangleUpBlack },
        { "flat", SymId::noteheadTriangleDownBlack },
        { "sharp2", SymId::noteheadDiamondBlack },
        { "flat2", SymId::noteheadSquareBlack },
    };
    muse::GlobalInject<IEngravingFontsProvider> fonts;
    auto font = fonts()->fontByName("JiMSMusic");
    for (const auto& [action, symbol] : cases) {
        const auto item = presentation.value(action).toMap();
        EXPECT_EQ(item.value("icon").toUInt(), font->symCode(symbol)) << action;
        EXPECT_FALSE(item.value("title").toString().isEmpty()) << action;
    }
    const QRawFont raw = QRawFont::fromFont(presentation.value("font").value<QFont>());
    ASSERT_TRUE(raw.isValid());
    const auto glyphBounds = [&](const char* action) {
        const char32_t code = presentation.value(action).toMap().value("icon").toUInt();
        const auto indexes = raw.glyphIndexesForString(QString::fromUcs4(&code, 1));
        EXPECT_EQ(indexes.size(), 1);
        EXPECT_NE(indexes.front(), 0u);
        return raw.pathForGlyph(indexes.front()).boundingRect();
    };
    const QRectF square = glyphBounds("flat2");
    const QRectF diamond = glyphBounds("sharp2");
    ASSERT_GT(square.width(), 0.0);
    EXPECT_NEAR(square.width(), square.height(), 0.02);
    EXPECT_NEAR(diamond.width(), diamond.height(), 0.02);
    EXPECT_NEAR(diamond.width() / square.width(), std::sqrt(2.0), 0.01);
    EXPECT_FALSE(presentation.contains("nat"));
    EXPECT_FALSE(presentation.contains("quarter"));
}

TEST_F(MeloUiModelTests, AccidentalPickersFollowSelectionAndInputStaffType) {
    selectMeasure(0);
    ASSERT_FALSE(notation::NoteInputBarModel::accidentalPresentationForScore(score.get()).isEmpty());
    StaffType* type = score->staff(0)->staffType(Fraction(0, 1));
    const StaffType original = *type;
    *type = *StaffType::preset(StaffTypes::STANDARD);
    EXPECT_TRUE(notation::NoteInputBarModel::accidentalPresentationForScore(score.get()).isEmpty());
    Measure* second = score->firstMeasure()->nextMeasure();
    ASSERT_TRUE(second);
    score->staff(0)->setStaffType(second->tick(), original);
    auto& input = score->inputState();
    input.setTrack(0);
    input.setSegment(second->first(SegmentType::ChordRest));
    input.setNoteEntryMode(true);
    EXPECT_FALSE(notation::NoteInputBarModel::accidentalPresentationForScore(score.get()).isEmpty());
    input.setNoteEntryMode(false);
    EXPECT_TRUE(notation::NoteInputBarModel::accidentalPresentationForScore(score.get()).isEmpty());
    *type = original;
    score->deselectAll();
    EXPECT_FALSE(notation::NoteInputBarModel::accidentalPresentationForScore(score.get()).isEmpty());
    EXPECT_TRUE(notation::NoteInputBarModel::accidentalPresentationForScore(nullptr).isEmpty());
}

TEST_F(MeloUiModelTests, AccidentalToolbarButtonsRenderSquaresInBothThemesAndScales) {
    selectMeasure(0);
    const auto presentation = notation::NoteInputBarModel::accidentalPresentationForScore(score.get());
    for (bool dark : { false, true }) {
        for (int scale : { 1, 2, 4 }) {
            SCOPED_TRACE(testing::Message() << "dark=" << dark << " scale=" << scale);
            const QColor background = dark ? Qt::black : Qt::white;
            const QColor foreground = dark ? Qt::white : Qt::black;
            QQmlEngine engine;
            const QVariantMap theme {
                { "accentColor", QColor(Qt::blue) }, { "accentOpacityNormal", 1.0 }, { "bodyFont", QFont() },
                { "borderWidth", 0 }, { "buttonColor", background }, { "buttonOpacityHit", 1.0 },
                { "buttonOpacityHover", 1.0 }, { "buttonOpacityNormal", 1.0 }, { "defaultButtonSize", 32 },
                { "fontPrimaryColor", foreground }, { "iconsFont", QFont() }, { "itemOpacityDisabled", 0.5 },
                { "linkColor", QColor(Qt::blue) }, { "navCtrlBorderWidth", 1 }, { "strokeColor", background },
            };
            engine.rootContext()->setContextProperty("ui", QVariantMap { { "theme", theme } });
            engine.globalObject().setProperty("qsTrc", engine.evaluate("(function(context, text) { return text; })"));
            engine.addImportPath("qrc:/qt/qml");
            QQmlComponent component(&engine, QUrl("qrc:/qt/qml/MuseScore/NotationScene/MeloAccidentalButton.qml"));
            ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
            QQuickWindow window;
            window.setColor(background);
            window.resize(80 * scale, 40 * scale);
            std::vector<std::unique_ptr<QQuickItem> > buttons;
            for (int index = 0; index < 2; ++index) {
                const char* action = index == 0 ? "flat2" : "sharp2";
                auto* button = qobject_cast<QQuickItem*>(component.createWithInitialProperties({
                    { "width", 32 }, { "height", 32 }, { "transparent", true },
                    { "useMeloIcon", true },
                    { "icon", presentation.value(action).toMap().value("icon") },
                    { "iconFont", presentation.value("font") }, { "iconColor", foreground },
                }));
                ASSERT_TRUE(button) << component.errorString().toStdString();
                buttons.emplace_back(button);
                button->setParentItem(window.contentItem());
                button->setTransformOrigin(QQuickItem::TopLeft);
                button->setScale(scale);
                button->setPosition(QPointF((4 + index * 40) * scale, 4 * scale));
            }
            window.show();
            QTest::qWait(50);
            const QImage rendered = window.grabWindow();
            ASSERT_FALSE(rendered.isNull());
            const qreal pixels = rendered.width() / 80.0;
            const auto ink = [&](int index) {
                QRect bounds;
                const QRect region(qRound((4 + index * 40) * pixels), qRound(4 * pixels), qRound(32 * pixels), qRound(32 * pixels));
                for (int y = region.top(); y <= region.bottom(); ++y) {
                    for (int x = region.left(); x <= region.right(); ++x) {
                        if (std::abs(rendered.pixelColor(x, y).red() - background.red()) > 128) {
                            bounds |= QRect(x, y, 1, 1);
                        }
                    }
                }
                EXPECT_TRUE(region.adjusted(1, 1, -1,
                                            -1).contains(bounds)) << "region " << region.x() << "," << region.y() << " ink " <<
                    bounds.x() << "," << bounds.y() << "," << bounds.width() << "," << bounds.height();
                EXPECT_NEAR(bounds.center().x(), region.center().x(), 1);
                EXPECT_NEAR(bounds.center().y(), region.center().y(), 1);
                return bounds;
            };
            const QRect square = ink(0);
            const QRect diamond = ink(1);
            ASSERT_GT(square.width(), 4);
            EXPECT_NEAR(square.width(), square.height(), 1);
            EXPECT_NEAR(diamond.width(), diamond.height(), 1);
            // Each thresholded raster width rounds two edges, with up to one pixel of width error.
            // Propagate both width errors in pixel units; the 4x render also rejects an unrotated square.
            EXPECT_NEAR(double(diamond.width()), std::sqrt(2.0) * square.width(), 1.0 + std::sqrt(2.0));
            const QString artifacts = qEnvironmentVariable("MELO_TEST_ARTIFACT_DIR");
            if (!artifacts.isEmpty()) {
                ASSERT_TRUE(rendered.save(artifacts + QString("/toolbar-%1-%2.png").arg(dark ? "dark" : "light").arg(scale)));
            }
        }
    }
}

TEST_F(MeloUiModelTests, StaffSectionsAreRelevantOnlyForCompatibleSelection) {
    auto* selected = selectMeasure(0);
    ElementKeySet keys { AbstractInspectorModel::makeKey(selected) };
    auto sections = AbstractInspectorModel::sectionTypesByElementKeys(keys, false, { selected });
    EXPECT_TRUE(sections.count(InspectorSectionType::SECTION_MELO_STAFF));
    EXPECT_TRUE(sections.count(InspectorSectionType::SECTION_MELO_SCORE));
    EXPECT_TRUE(AbstractInspectorModel::sectionTypesByElementKeys({}, false, {}).empty());
    StaffType original = *score->staff(0)->staffType(Fraction(0, 1));
    *score->staff(0)->staffType(Fraction(0, 1)) = *StaffType::preset(StaffTypes::STANDARD);
    sections = AbstractInspectorModel::sectionTypesByElementKeys(keys, false, { selected });
    EXPECT_FALSE(sections.count(InspectorSectionType::SECTION_MELO_STAFF));
    *score->staff(0)->staffType(Fraction(0, 1)) = original;
}
TEST_F(MeloUiModelTests, StaffPresentationIsUndoableAndPreservesMusicalState) {
    selectMeasure(0);
    MeloStaffSettingsModel model(nullptr, muse::modularity::globalCtx(), &repository);
    model.context.set(global);
    model.loadProperties();
    ASSERT_TRUE(model.settings()["available"].toBool());
    auto before = score->staff(0)->staffType(Fraction(0, 1))->meloStateJson();
    int undo = score->undoStack()->size();
    model.setStaffOption("labels", 3);
    EXPECT_EQ(score->undoStack()->size(), undo + 1);
    EXPECT_EQ(score->staff(0)->staffType(Fraction(0, 1))->meloScaleDotLabelMode(), MeloScaleDotLabelMode::Split);
    EXPECT_EQ(score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), before);
    model.setStaffOption("labels", 3);
    EXPECT_EQ(score->undoStack()->size(), undo + 1);
    score->undoRedo(true, nullptr);
    model.loadProperties();
    EXPECT_EQ(model.settings()["labels"].toInt(), 0);
}
TEST_F(MeloUiModelTests, SelectionRefreshClearsOldFeedbackAndChangesTheTarget) {
    selectMeasure(0);
    MeloStaffSettingsModel model(nullptr, muse::modularity::globalCtx(), &repository);
    model.context.set(global);
    model.loadProperties();
    auto first = model.settings()["target"].toString();
    model.setStaffOption("labels", 3);
    EXPECT_FALSE(model.status().isEmpty());
    selectMeasure(4);
    model.loadProperties();
    EXPECT_TRUE(model.status().isEmpty());
    EXPECT_NE(model.settings()["target"].toString(), first);
    EXPECT_TRUE(model.settings()["hasChange"].toBool());
    EXPECT_FALSE(model.settings()["indicator"].toString().isEmpty());
}
TEST_F(MeloUiModelTests, InspectorOpensOnlyTheSeparateRelativeIntervalAction) {
    authorKnownD4Reference();
    selectMeasure(1);
    MeloStaffSettingsModel model(nullptr, muse::modularity::globalCtx(), &repository);
    model.context.set(global);
    model.loadProperties();
    EXPECT_FALSE(model.settings().contains("keys"));
    EXPECT_FALSE(model.settings().contains("referenceBound"));
    ASSERT_TRUE(model.settings()["canKeyChange"].toBool());
    auto dispatcher = std::make_shared<testing::NiceMock<muse::actions::ActionsDispatcherMock> >();
    model.dispatcher.set(dispatcher);
    EXPECT_CALL(*dispatcher, dispatch(testing::Eq("melo-edit-key-change"), testing::_)).Times(1);
    model.editKeyChange();
    selectMeasure(0);
    model.loadProperties();
    EXPECT_FALSE(model.settings()["canKeyChange"].toBool());
    model.editKeyChange();
}
TEST_F(MeloUiModelTests, ScaleChoiceChangesAllPartsAndReportsTheMutation) {
    global->setCurrentNotation(nullptr);
    score.reset(ScoreRW::readScore(muse::String::fromUtf8(MELO_UI_TEST_DATA_ROOT) + u"/jimstaff_data/m9-satb-hymn.mscx", true));
    ASSERT_TRUE(score);
    ASSERT_EQ(score->nstaves(), 4);
    notation = std::make_shared<TestNotation>(score.get());
    global->setCurrentNotation(notation);
    selectMeasure(1);
    Measure* measure = score->firstMeasure()->nextMeasure();
    muse::String error;
    ASSERT_TRUE(melo::applyChangeToAllMeloParts(score.get(), measure, { u"scale:cycle:double-harmonic-minor" },
                                                error)) << error.toStdString();
    MeloStaffSettingsModel model(nullptr, muse::modularity::globalCtx(), &repository);
    model.context.set(global);
    model.loadProperties();
    ASSERT_NE(model.settings()["scalesIndex"].toInt(), 0);
    const int undo = score->undoStack()->size();
    model.applyOption("scales", 0);
    ASSERT_FALSE(model.hasError()) << model.status().toStdString();
    EXPECT_EQ(score->undoStack()->size(), undo + 1);
    EXPECT_FALSE(model.status().contains("no change"));
    for (staff_idx_t staff = 0; staff < 4; ++staff) {
        melo::StateChangeOptions options;
        ASSERT_TRUE(melo::changeOptions(score.get(), staff, measure, options));
        for (const auto& cycle : options.cycles) {
            if (cycle.id == u"scale:cycle:diatonic") {
                EXPECT_TRUE(cycle.current) << staff;
            }
        }
    }
    score->undoRedo(true, nullptr);
    melo::StateChangeOptions options;
    ASSERT_TRUE(melo::changeOptions(score.get(), 1, measure, options));
    for (const auto& cycle : options.cycles) {
        if (cycle.id == u"scale:cycle:double-harmonic-minor") {
            EXPECT_TRUE(cycle.current);
        }
    }
}
TEST_F(MeloUiModelTests, TuningRejectsInvalidAndNoOpGesturesWithoutUndoEntries) {
    MeloTuningModel model;
    model.context.set(global);
    model.playbackController.set(playback);
    model.init();
    ASSERT_TRUE(model.available());
    double original = model.cents();
    int undo = score->undoStack()->size();
    model.acceptText("invalid");
    EXPECT_FALSE(model.error().isEmpty());
    model.commit(model.maximum() + 1);
    EXPECT_EQ(model.cents(), original);
    ASSERT_TRUE(model.beginPreview());
    model.commit(original);
    EXPECT_EQ(score->undoStack()->size(), undo);
    ASSERT_TRUE(model.beginPreview());
    model.preview(690.0);
    model.cancel();
    EXPECT_NEAR(model.cents(), original, 1e-8);
    EXPECT_EQ(score->undoStack()->size(), undo);
}
TEST_F(MeloUiModelTests, TuningAnnouncesEachRefusalOnceAndRefreshesSilently) {
    auto announcements = std::make_shared<TestAnnouncements>();
    MeloTuningModel model;
    model.context.set(global);
    model.playbackController.set(playback);
    model.accessibilityController.set(announcements);
    model.init();
    model.acceptText("730");
    ASSERT_EQ(announcements->messages.size(), 1);
    EXPECT_EQ(announcements->messages.last(), model.error());
    notation->notationChanged().notify();
    EXPECT_EQ(announcements->messages.size(), 1);
    model.acceptText("730");
    EXPECT_EQ(announcements->messages.size(), 2);
    model.acceptText("690");
    EXPECT_TRUE(model.error().isEmpty());
    EXPECT_EQ(announcements->messages.size(), 2);
}
TEST_F(MeloUiModelTests, TuningCommitsOnceAndCancelsWhenSwitchingScores) {
    MeloTuningModel model;
    model.context.set(global);
    model.playbackController.set(playback);
    model.init();
    const double original = model.cents();
    int undo = score->undoStack()->size();
    model.commit(690.0);
    EXPECT_EQ(score->undoStack()->size(), undo + 1);
    EXPECT_NEAR(model.cents(), 690.0, 1e-8);
    score->undoRedo(true, nullptr);
    notation->notationChanged().notify();
    EXPECT_NEAR(model.cents(), original, 1e-8);
    model.preview(690.0);
    global->setCurrentNotation(nullptr);
    EXPECT_FALSE(model.available());
    double cents = 0, period = 0;
    ASSERT_TRUE(melo::staffMetrics(score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), cents, period));
    EXPECT_NEAR(cents, original, 1e-8);
}
TEST_F(MeloUiModelTests, TuningRoutesLiveGeneratorOnlyForTheCurrentScore)
{
    std::vector<melo::ToneDiamondSetting> settings;
    uint32_t generatorParamId = 0;
    uint32_t xParamId = 0;
    uint32_t yParamId = 0;
    ASSERT_TRUE(melo::toneDiamondSettings(settings, generatorParamId, xParamId, yParamId));

    struct RoutedValue {
        notation::INotationPtr notation;
        muse::audio::AudioResourceId resourceId;
        uint32_t paramId = 0;
        double value = 0.0;
    };
    std::vector<RoutedValue> routed;
    ON_CALL(*playback, setInputParamPlainForResource(testing::_, testing::_, testing::_, testing::_))
    .WillByDefault([&routed](const notation::INotationPtr& expectedNotation, const muse::audio::AudioResourceId& resourceId,
                             uint32_t paramId, double value) {
        routed.push_back({ expectedNotation, resourceId, paramId, value });
    });

    muse::async::Notification resourceChanged;
    ON_CALL(*playback, inputResourceChanged()).WillByDefault(testing::Return(resourceChanged));
    MeloTuningModel model;
    model.context.set(global);
    model.playbackController.set(playback);
    model.init();
    ASSERT_FALSE(routed.empty());
    EXPECT_EQ(routed.back().notation, notation);
    EXPECT_EQ(routed.back().resourceId, muse::audio::AudioResourceId("MeloPresto Synth"));
    EXPECT_EQ(routed.back().paramId, generatorParamId);

    const size_t callsBeforeRejectedPreview = routed.size();
    model.preview(model.maximum() + 1.0);
    EXPECT_EQ(routed.size(), callsBeforeRejectedPreview);

    const double original = model.cents();
    model.preview(690.0);
    ASSERT_FALSE(routed.empty());
    EXPECT_DOUBLE_EQ(routed.back().value, 690.0);
    model.cancel();
    EXPECT_DOUBLE_EQ(routed.back().value, original);

    model.commit(690.0);
    EXPECT_DOUBLE_EQ(routed.back().value, 690.0);
    score->undoRedo(true, nullptr);
    notation->notationChanged().notify();
    EXPECT_DOUBLE_EQ(routed.back().value, original);
    score->undoRedo(false, nullptr);
    notation->notationChanged().notify();
    EXPECT_DOUBLE_EQ(routed.back().value, 690.0);

    // Selecting MeloPresto Synth after the track already exists must adopt the
    // score's current generator without requiring another tuning gesture.
    const size_t callsBeforeResourceChange = routed.size();
    resourceChanged.notify();
    ASSERT_GT(routed.size(), callsBeforeResourceChange);
    EXPECT_DOUBLE_EQ(routed.back().value, 690.0);

    model.preview(691.0);
    const size_t callsBeforeScoreSwitch = routed.size();
    auto nextNotation = std::make_shared<TestNotation>(score.get());
    global->setCurrentNotation(nextNotation);
    ASSERT_GT(routed.size(), callsBeforeScoreSwitch);
    EXPECT_EQ(routed.back().notation, nextNotation);
    EXPECT_DOUBLE_EQ(routed.back().value, 690.0);
}
TEST_F(MeloUiModelTests, RejectedCommitRestoresLiveTuningAfterAValidPreview)
{
    Note* boundaryNote = nullptr;
    for (Segment* segment = score->firstSegment(SegmentType::ChordRest); segment;
         segment = segment->next1(SegmentType::ChordRest)) {
        EngravingItem* item = segment->element(0);
        if (item && item->isChord()) {
            boundaryNote = toChord(item)->notes().front();
            break;
        }
    }
    ASSERT_TRUE(boundaryNote);
    // This exact lattice identity is playable at generator700 and690;
    // generator720 projects it beyond the host's MIDI range.
    boundaryNote->setMeloPitch(-1, 11);
    size_t repairs = 0;
    muse::String error;
    ASSERT_TRUE(melo::normalizeStoredPitchesAfterLoad(score.get(), repairs, error, false));
    std::vector<double> routed;
    ON_CALL(*playback, setInputParamPlainForResource(testing::_, testing::_, testing::_, testing::_))
    .WillByDefault([&routed](const notation::INotationPtr&, const muse::audio::AudioResourceId&, uint32_t, double value) {
        routed.push_back(value);
    });
    MeloTuningModel model;
    model.context.set(global);
    model.playbackController.set(playback);
    model.init();
    const double original = model.cents();
    const int originalUndo = score->undoStack()->size();
    model.preview(690.0);
    ASSERT_FALSE(routed.empty());
    ASSERT_DOUBLE_EQ(routed.back(), 690.0);
    model.commit(720.0);
    EXPECT_FALSE(model.error().isEmpty());
    EXPECT_DOUBLE_EQ(model.cents(), original);
    EXPECT_DOUBLE_EQ(routed.back(), original);
    EXPECT_EQ(score->undoStack()->size(), originalUndo);
    EXPECT_EQ(boundaryNote->meloNPer(), -1);
    EXPECT_EQ(boundaryNote->meloNGen(), 11);
}
TEST_F(MeloUiModelTests, TuningControlMouseDragPreviewsCommitsOneUndoAndEscCancels)
{
    MeloTuningModel model;
    model.context.set(global);
    model.init();
    ASSERT_TRUE(model.available());

    QQmlEngine engine;
    const QVariantMap theme {
        { "accentColor", QColor(Qt::blue) }, { "accentOpacityNormal", 1.0 }, { "bodyFont", QFont() },
        { "borderWidth", 1 }, { "buttonColor", QColor(Qt::white) }, { "buttonOpacityHit", 1.0 },
        { "buttonOpacityHover", 1.0 }, { "buttonOpacityNormal", 1.0 }, { "defaultButtonSize", 24 },
        { "fontPrimaryColor", QColor(Qt::black) }, { "iconsFont", QFont() }, { "itemOpacityDisabled", 0.5 },
        { "linkColor", QColor(Qt::blue) }, { "navCtrlBorderWidth", 1 }, { "popupBackgroundColor", QColor(Qt::white) },
        { "strokeColor", QColor(Qt::black) }, { "textFieldColor", QColor(Qt::white) },
    };
    engine.rootContext()->setContextProperty("ui", QVariantMap { { "theme", theme } });
    engine.globalObject().setProperty("qsTrc", engine.evaluate("(function(context, text) { return text; })"));
    engine.addImportPath("qrc:/qt/qml");
    QQmlComponent component(&engine, QUrl("qrc:/qt/qml/MuseScore/Inspector/MeloTuningControl.qml"));
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();

    muse::ui::NavigationPanel panel;
    QQuickWindow window;
    window.resize(800, 1000);
    auto* control = qobject_cast<QQuickItem*>(component.createWithInitialProperties({
        { "model", QVariant::fromValue(&model) }, { "navigationPanel", QVariant::fromValue(&panel) },
    }));
    ASSERT_TRUE(control) << component.errorString().toStdString();
    control->setParentItem(window.contentItem());
    window.show();
    QCoreApplication::processEvents();

    auto* slider = control->findChild<QQuickItem*>("jimsTuningSlider");
    ASSERT_TRUE(slider);
    ASSERT_GT(slider->height(), 20.0);
    const double original = model.cents();
    const int undoBefore = score->undoStack()->size();
    const QPoint press = slider->mapToScene({ slider->width() / 2.0, slider->height() / 2.0 }).toPoint();
    const QPoint moved = slider->mapToScene({ slider->width() / 2.0, slider->height() * 0.75 }).toPoint();

    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, press);
    QTest::mouseMove(&window, moved);
    QCoreApplication::processEvents();
    const double previewed = slider->property("value").toDouble();
    EXPECT_NEAR(model.cents(), previewed, 0.1);
    EXPECT_EQ(score->undoStack()->size(), undoBefore);
    EXPECT_GT(std::abs(previewed - original), 0.1);

    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, moved);
    QCoreApplication::processEvents();
    EXPECT_EQ(score->undoStack()->size(), undoBefore + 1);
    EXPECT_NEAR(model.cents(), previewed, 0.1);
    score->undoRedo(true, nullptr);
    notation->notationChanged().notify();
    EXPECT_NEAR(model.cents(), original, 0.1);

    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, press);
    QTest::mouseMove(&window, moved);
    QCoreApplication::processEvents();
    slider->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_Escape);
    QCoreApplication::processEvents();
    EXPECT_NEAR(model.cents(), original, 0.1);
    EXPECT_EQ(score->undoStack()->size(), undoBefore + 1);
}
TEST_F(MeloUiModelTests, ScorePresentationDoesNotChangeMusicalStateAndUndoes) {
    selectMeasure(0);
    MeloScoreSettingsModel model(nullptr, muse::modularity::globalCtx(), &repository);
    model.context.set(global);
    model.loadProperties();
    auto state = score->staff(0)->staffType(Fraction(0, 1))->meloStateJson();
    bool original = model.settings()["elide"].toBool();
    int undo = score->undoStack()->size();
    model.setOption("elide", !original);
    EXPECT_EQ(score->undoStack()->size(), undo + 1);
    EXPECT_EQ(score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(), state);
    score->undoRedo(true, nullptr);
    model.loadProperties();
    EXPECT_EQ(model.settings()["elide"].toBool(), original);
}

TEST_F(MeloUiModelTests, LatticeTuningIsDerivedWhileConventionalTuningRemainsEditable)
{
    NotePlaybackModel model(nullptr, muse::modularity::globalCtx(), &repository);
    model.context.set(global);
    auto* meloNote = toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->notes().front();
    repository.updateElementList({ meloNote }, SelState::LIST);
    EXPECT_FALSE(model.tuning()->isEnabled());
    EXPECT_TRUE(model.velocity()->isEnabled());
    const double tuning = meloNote->tuning();
    const size_t undo = score->undoStack()->currentIndex();
    model.tuning()->setValue(tuning + 50.0);
    model.tuning()->resetToDefault();
    EXPECT_DOUBLE_EQ(meloNote->tuning(), tuning);
    EXPECT_EQ(score->undoStack()->currentIndex(), undo);

    std::unique_ptr<MasterScore> conventional(ScoreRW::readScore(
                                                  muse::String::fromUtf8(MELO_UI_TEST_DATA_ROOT)
                                                  + u"/midi/midirenderer_data/one_guitar_note.mscx", true));
    ASSERT_TRUE(conventional);
    auto* standardNote = toChord(conventional->firstSegment(SegmentType::ChordRest)->element(0))->notes().front();
    repository.updateElementList({ standardNote }, SelState::LIST);
    EXPECT_TRUE(model.tuning()->isEnabled());
    EXPECT_TRUE(model.velocity()->isEnabled());
    repository.updateElementList({ standardNote, meloNote }, SelState::LIST);
    EXPECT_FALSE(model.tuning()->isEnabled());
    EXPECT_TRUE(model.velocity()->isEnabled());
    repository.updateElementList({ standardNote }, SelState::LIST);
    EXPECT_TRUE(model.tuning()->isEnabled());
}

TEST_F(MeloUiModelTests, RealDragCancellationAndReleaseRestoreWholeTie)
{
    for (double generator : { 700.0, 1200.0 * 4.0 / 7.0, 720.0 }) {
        SCOPED_TRACE(generator);
        for (bool fromPart : { false, true }) {
            score.reset(ScoreRW::readScore(muse::String::fromUtf8(MELO_UI_TEST_DATA_ROOT) + u"/jimstaff_data/m5-key-up.mscx", true));
            ASSERT_TRUE(score);
            String setupError;
            auto* second = score->firstMeasure()->nextMeasure();
            const String interval = generator == 700.0 ? u"{\"nPer\":0,\"nGen\":-1}" : u"{\"nPer\":1,\"nGen\":0}";
            ASSERT_TRUE(melo::changeRelativeKey(score.get(), 0, second->tick(), interval, score->metaTag(melo::REFERENCE_TIMELINE_TAG),
                                                setupError));
            melo::TuningController tuning(score.get(), 0);
            ASSERT_TRUE(tuning.beginPreview());
            ASSERT_TRUE(tuning.commit(generator));
            EXPECT_DOUBLE_EQ(tuning.currentGeneratorCents(), generator);

            std::vector<Note*> notes;
            for (Segment* segment = score->firstSegment(SegmentType::ChordRest); segment;
                 segment = segment->next1(SegmentType::ChordRest)) {
                EngravingItem* item = segment->element(0);
                if (item && item->isChord()) {
                    notes.push_back(toChord(item)->notes().front());
                }
            }
            ASSERT_GE(notes.size(), 5u);
            Note* a = notes[3];
            Note* b = notes[4];
            NoteVal source = a->noteVal();
            source.hasMeloPitch = true;
            source.meloNPer = 0;
            source.meloNGen = 0;
            ASSERT_TRUE(a->setNval(source));
            NoteVal target;
            ASSERT_TRUE(melo::prepareContinuationValue(a->noteVal(), a->staff(), a->tick(), b->tick(), target));
            ASSERT_TRUE(b->setNval(target));
            Tie* tie = Factory::createTie(score->dummy());
            tie->setStartNote(a);
            tie->setEndNote(b);
            tie->setTrack(0);
            tie->setTick(a->tick());
            tie->setTick2(b->tick());
            score->startCmd(muse::TranslatableString::untranslatable("Tie drag fixture"));
            score->undoAddElement(tie);
            score->endCmd();
            score->doLayout();
            Score* part=TestUtils::createPart(score.get());
            ASSERT_TRUE(part);
            std::vector<Note*> linkedNotes;
            for (Note* note : { a, b }) {
                for (EngravingObject* object:note->linkList()) {
                    if (object->score() == part) {
                        linkedNotes.push_back(toNote(object));
                    }
                }
            }
            ASSERT_EQ(linkedNotes.size(), 2u);
            if (fromPart) {
                std::swap(a, linkedNotes[0]);
                std::swap(b, linkedNotes[1]);
            }
            notation::Notation actual(nullptr, muse::modularity::globalCtx(), a->score());
            auto interaction = actual.interaction();
            const NoteVal originalA = a->noteVal(), originalB = b->noteVal();
            const double tuningA = a->tuning(), tuningB = b->tuning();
            const StaffType* type = a->staff()->staffTypeForElement(a);
            const String state = type->meloStateJson();
            const bool empty = type->meloExtentIsEmptyDefault();
            const auto index = score->undoStack()->currentIndex();
            const muse::PointF origin(10, 10), moved(10, 10 - 24 * a->spatium() * type->lineDistance().val());
            std::vector<const StaffType*> types;
            std::vector<String> states;
            std::vector<Note*> affected { a, b, linkedNotes[0], linkedNotes[1] };
            std::vector<NoteVal> values;
            for (Note* note:affected) {
                const StaffType* current=note->staff()->staffTypeForElement(note);
                types.push_back(current);
                states.push_back(current->meloStateJson());
                values.push_back(note->noteVal());
            }
            if (!fromPart && generator == 700.0) {
                // The 400-cent target has an ambiguous continuation at the later
                // reference. A real drag must refuse it before changing either score.
                a->score()->select(a, SelectType::SINGLE);
                interaction->startDrag({ a }, {}, [](const EngravingItem*) { return true; });
                melo::PitchHit ambiguous;
                ASSERT_TRUE(melo::nearestPitch(type->meloStateJson(), a->meloCentsAboveDo() + 400,
                                               true, a->meloNPer(), a->meloNGen(), ambiguous));
                std::vector<melo::NoteEdit> rejected;
                String refusal;
                ASSERT_FALSE(melo::preparePitchEdit(a, ambiguous.nPer, ambiguous.nGen, rejected, refusal));
                for (int repeat = 0; repeat < 5; ++repeat) {
                    if (repeat >= 2) {
                        QTest::qWait(40);
                    }
                    interaction->drag(origin, muse::PointF(10, 10 - 4 * a->spatium() * type->lineDistance().val()),
                                      notation::DragMode::OnlyY);
                    for (size_t i = 0; i < affected.size(); ++i) {
                        EXPECT_TRUE(affected[i]->noteVal() == values[i]);
                    }
                }
                interaction->clearSelection();
                EXPECT_EQ(score->undoStack()->currentIndex(), index);
                EXPECT_FALSE(score->undoStack()->hasActiveCommand());
                for (size_t i = 0; i < affected.size(); ++i) {
                    EXPECT_EQ(types[i]->meloStateJson(), states[i]);
                    EXPECT_FALSE(types[i]->meloFrameFrozen());
                }
            }
            for (bool cancel : { true, false }) {
                a->score()->select(a, SelectType::SINGLE);
                interaction->startDrag({ a }, {}, [](const EngravingItem*) { return true; });
                EXPECT_TRUE(interaction->isDragStarted());
                EXPECT_FALSE(interaction->isEditingElement());
                ASSERT_TRUE(a->meloCentsValid());
                melo::PitchHit expected;
                ASSERT_TRUE(melo::nearestPitch(type->meloStateJson(), a->meloCentsAboveDo() + 2400,
                                               true, a->meloNPer(), a->meloNGen(), expected));
                ASSERT_FALSE(expected.nPer == a->meloNPer() && expected.nGen == a->meloNGen());
                std::vector<melo::NoteEdit> probe;
                String error;
                ASSERT_TRUE(melo::preparePitchEdit(a, expected.nPer, expected.nGen, probe, error)) << error.toStdString();
                EXPECT_TRUE(type->meloFrameFrozen());
                interaction->drag(origin, moved, notation::DragMode::OnlyY);
                const auto previewA = a->noteVal();
                EXPECT_FALSE(previewA == originalA);
                for (int repeat = 0; repeat < 5; ++repeat) {
                    if (repeat >= 2) {
                        QTest::qWait(40);
                    }
                    interaction->drag(origin, moved, notation::DragMode::OnlyY);
                    EXPECT_TRUE(a->noteVal() == previewA);
                    melo::SoundingPitch first, last;
                    ASSERT_TRUE(melo::noteSoundingPitch(type->meloStateJson(), a->meloNPer(), a->meloNGen(), first));
                    ASSERT_TRUE(melo::noteSoundingPitch(b->staff()->staffTypeForElement(b)->meloStateJson(), b->meloNPer(), b->meloNGen(),
                                                        last));
                    EXPECT_NEAR(first.frequencyHz, last.frequencyHz, 1e-8);
                }
                if (cancel) {
                    interaction->clearSelection();
                } else {
                    interaction->endDrag();
                    EXPECT_EQ(score->undoStack()->currentIndex(), index + 1);
                    for (size_t i=0; i < linkedNotes.size(); ++i) {
                        EXPECT_NE(types[i + 2]->meloStateJson(), states[i + 2]);
                        EXPECT_TRUE(linkedNotes[i]->noteVal() == (i == 0 ? a : b)->noteVal());
                    }
                    interaction->undo();
                }
                EXPECT_FALSE(interaction->isDragStarted());
                EXPECT_FALSE(score->undoStack()->hasActiveCommand());
                EXPECT_FALSE(type->meloFrameFrozen());
                EXPECT_EQ(score->undoStack()->currentIndex(), index);
                EXPECT_TRUE(a->noteVal() == originalA);
                EXPECT_TRUE(b->noteVal() == originalB);
                EXPECT_DOUBLE_EQ(a->tuning(), tuningA);
                EXPECT_DOUBLE_EQ(b->tuning(), tuningB);
                EXPECT_EQ(type->meloStateJson(), state);
                EXPECT_EQ(type->meloExtentIsEmptyDefault(), empty);
                for (size_t i=0; i < affected.size(); ++i) {
                    EXPECT_TRUE(affected[i]->noteVal() == values[i]);
                    EXPECT_EQ(types[i]->meloStateJson(), states[i]);
                    EXPECT_FALSE(types[i]->meloFrameFrozen());
                }
            }
            if (!fromPart) {
                score->select(a, SelectType::SINGLE);
                score->startCmd(muse::TranslatableString::untranslatable("Keyboard after drag cancellation"));
                score->upDown(true, UpDownMode::OCTAVE);
                score->endCmd();
                interaction->undo();
                EXPECT_TRUE(a->noteVal() == originalA);
                EXPECT_TRUE(b->noteVal() == originalB);
                if (const char* output = std::getenv("MELO_LATTICE_SENSORY_OUT")) {
                    for (Segment* segment = score->firstSegment(SegmentType::ChordRest); segment;
                         segment = segment->next1(SegmentType::ChordRest)) {
                        for (track_idx_t track = 0; track < score->ntracks(); ++track) {
                            auto* item = segment->element(track);
                            if (item && item->isChord()) {
                                for (Note* note : toChord(item)->notes()) {
                                    note->setPlay(note == a || note == b);
                                }
                            }
                        }
                    }
                    const String path = String::fromUtf8(output) + u"/cross-reference-full-tie-after-cancel-undo-"
                                        + String::number(generator == 700.0 ? 12 : generator == 720.0 ? 5 : 7) + u"tet.mscx";
                    ASSERT_TRUE(ScoreRW::saveScore(score.get(), path));
                    std::unique_ptr<MasterScore> reopened(ScoreRW::readScore(path, true));
                    ASSERT_TRUE(reopened);
                }
            }
        }
    }
}

TEST_F(MeloUiModelTests, RelativeChangesProduceIndependentVoiceAudioProbes)
{
    for (double generator : { 700.0, 4800.0 / 7.0, 720.0 }) {
        SCOPED_TRACE(generator);
        score.reset(ScoreRW::readScore(String::fromUtf8(MELO_UI_TEST_DATA_ROOT) + u"/jimstaff_data/m9-satb-hymn.mscx", true));
        ASSERT_TRUE(score);
        std::vector<Note*> probes;
        const int periods[] = { 2, 1, 1, 0 };
        for (Segment* segment = score->firstSegment(SegmentType::ChordRest); segment;
             segment = segment->next1(SegmentType::ChordRest)) {
            for (track_idx_t track = 0; track < score->ntracks(); ++track) {
                auto* item = segment->element(track);
                if (!item || !item->isChord()) {
                    continue;
                }
                for (Note* note : toChord(item)->notes()) {
                    NoteVal value = note->noteVal();
                    value.hasMeloPitch = true;
                    value.meloNPer = periods[note->staffIdx()];
                    value.meloNGen = -2;
                    ASSERT_TRUE(note->setNval(value));
                    probes.push_back(note);
                }
            }
        }
        ASSERT_EQ(probes.size(), 32u);
        melo::TuningController tuning(score.get(), 0);
        ASSERT_TRUE(tuning.beginPreview());
        ASSERT_TRUE(tuning.commit(generator));
        String error;
        for (const auto& event : std::vector<std::pair<Fraction, String> > {
            { Fraction(1, 1), u"{\"nPer\":-1,\"nGen\":3}" },
            { Fraction(2, 1), u"{\"nPer\":1,\"nGen\":-3}" } }) {
            ASSERT_TRUE(melo::changeRelativeKey(score.get(), 0, event.first, event.second,
                                                score->metaTag(melo::REFERENCE_TIMELINE_TAG), error)) << error.toStdString();
        }
        const auto events
            = QJsonDocument::fromJson(score->metaTag(melo::REFERENCE_TIMELINE_TAG).toQString().toUtf8()).object().value("events");
        const int phases = 3;
        for (int phase = 0; phase < phases; ++phase) {
            if (phase) {
                const String pitch = phase == 1 ? u"D5" : u"B♭4";
                ASSERT_TRUE(melo::changeInitialTonicPitch(score.get(), 0, Fraction(0, 1), 0, pitch,
                                                          score->staff(0)->staffType(Fraction(0, 1))->meloStateJson(),
                                                          score->metaTag(melo::REFERENCE_TIMELINE_TAG), error)) << error.toStdString();
            }
            const auto root = QJsonDocument::fromJson(score->metaTag(melo::REFERENCE_TIMELINE_TAG).toQString().toUtf8()).object();
            EXPECT_EQ(root["events"], events);
            const int initialKey = phase == 0 ? 62 : phase == 1 ? 64 : 60;
            EXPECT_EQ(root["initial"].toObject()["step"].toString(), phase == 0 ? "D" : phase == 1 ? "E" : "C");
            EXPECT_EQ(root["initial"].toObject()["alter"].toInt(), 0);
            EXPECT_EQ(root["initial"].toObject()["octave"].toInt(), 4);
            for (Note* note : probes) {
                const double displacement = note->tick() >= Fraction(1, 1) && note->tick() < Fraction(2, 1)
                                            ? -1200.0 + 3.0 * generator : 0.0;
                const double cents = (initialKey - 69) * 100.0 + periods[note->staffIdx()] * 1200.0 - 2.0 * generator + displacement;
                const double actual = 440.0 * std::pow(2.0, ((note->pitch() - 69) * 100.0 + note->tuning()) / 1200.0);
                EXPECT_NEAR(actual, 440.0 * std::pow(2.0, cents / 1200.0), 1e-7);
                EXPECT_EQ(note->meloNPer(), periods[note->staffIdx()]);
                EXPECT_EQ(note->meloNGen(), -2);
            }
            if (const char* output = std::getenv("MELO_LATTICE_SENSORY_OUT")) {
                for (staff_idx_t solo = 0; solo < 4; ++solo) {
                    for (Note* note : probes) {
                        note->setPlay(note->staffIdx() == solo);
                    }
                    const String path = String::fromUtf8(output) + u"/voice-probe-"
                                        + String::number(generator == 700.0 ? 12 : generator == 720.0 ? 5 : 7)
                                        + u"tet-phase" + String::number(phase) + u"-voice" + String::number(solo) + u".mscx";
                    ASSERT_TRUE(ScoreRW::saveScore(score.get(), path));
                }
                for (Note* note : probes) {
                    note->setPlay(true);
                }
            }
        }
    }
}

TEST_F(MeloUiModelTests, VocalDragCancellationRestoresEveryStaffAmbit)
{
    score.reset(ScoreRW::readScore(muse::String::fromUtf8(MELO_UI_TEST_DATA_ROOT) + u"/jimstaff_data/m9-satb-hymn.mscx", true));
    ASSERT_TRUE(score);
    Note* first=nullptr;
    for (Segment* segment=score->firstSegment(SegmentType::ChordRest); segment; segment=segment->next1(SegmentType::ChordRest)) {
        auto* item=segment->element(0);
        if (!item || !item->isChord()) {
            continue;
        }
        for (Note* note:toChord(item)->notes()) {
            NoteVal value=note->noteVal();
            value.hasMeloPitch=true;
            value.meloNPer=0;
            value.meloNGen=0;
            ASSERT_TRUE(note->setNval(value));
            if (!first) {
                first=note;
            }
        }
    }
    ASSERT_TRUE(first);
    melo::deriveTonicAmbits(score.get());
    score->setLayoutAll();
    score->doLayout();
    std::vector<const StaffType*> types;
    std::vector<String> states;
    for (Staff* staff:score->staves()) {
        const StaffType* type=staff->staffType(Fraction(0, 1));
        types.push_back(type);
        states.push_back(type->meloStateJson());
        ASSERT_EQ(type->meloTonicAmbit(), u"tonic-bounded");
    }
    const auto index=score->undoStack()->currentIndex();
    const NoteVal original=first->noteVal();
    notation::Notation actual(nullptr, muse::modularity::globalCtx(), score.get());
    auto interaction=actual.interaction();
    const muse::PointF origin(10, 10), target(10, 10 + 7 * first->spatium() * types[0]->lineDistance().val());
    for (bool cancel:{ true, false }) {
        score->select(first, SelectType::SINGLE);
        interaction->startDrag({ first }, {}, [](const EngravingItem*) { return true; });
        interaction->drag(origin, target, notation::DragMode::OnlyY);
        EXPECT_EQ(first->meloNPer(), 0);
        EXPECT_EQ(first->meloNGen(), -1);
        if (cancel) {
            interaction->clearSelection();
        } else {
            interaction->endDrag();
            for (const StaffType* type:types) {
                EXPECT_EQ(type->meloTonicAmbit(), u"tonic-centered");
            }
            interaction->undo();
        }
        EXPECT_EQ(score->undoStack()->currentIndex(), index);
        EXPECT_TRUE(first->noteVal() == original);
        for (size_t i=0; i < types.size(); ++i) {
            EXPECT_EQ(types[i]->meloStateJson(), states[i]);
            EXPECT_FALSE(types[i]->meloFrameFrozen());
        }
    }
}
