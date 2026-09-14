// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
// Copyright (C) 2026 Jim Plamondon
#include <gtest/gtest.h>
#include <QFile>
#include <QTemporaryDir>
#include <QPixmap>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include "actions/internal/actionsdispatcher.h"
#include "context/internal/globalcontext.h"
#include "global/settings.h"
#include "global/tests/mocks/interactivemock.h"
#include "project/internal/projectactionscontroller.h"
#include "project/internal/projectconfiguration.h"
#include "project/internal/projectcreator.h"
#include "project/internal/opensaveprojectscenario.h"
#include "project/internal/notationwritersregister.h"
#include "notation/internal/mscnotationwriter.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/note.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftype.h"
#include "engraving/melo/melochangecontroller.h"
#include "engraving/rendering/iscorerenderer.h"

using namespace muse;
using namespace mu;

namespace {
QJsonArray writtenNotes(engraving::Score* score)
{
    QJsonArray result;
    for (auto* segment = score->firstSegment(engraving::SegmentType::ChordRest); segment;
         segment = segment->next1(engraving::SegmentType::ChordRest)) {
        for (engraving::track_idx_t track = 0; track < score->ntracks(); ++track) {
            auto* item = segment->element(track);
            if (!item || !item->isChord()) {
                continue;
            }
            for (auto* note : engraving::toChord(item)->notes()) {
                result.append(QJsonArray { segment->tick().ticks(), int(track), note->meloNPer(), note->meloNGen(),
                                           note->pitch(), note->tuning() });
            }
        }
    }
    return result;
}

// Keep the operating system's recent-document list out of this save test.
class SaveTestRecentFiles : public project::IRecentFilesController
{
public:
    const project::RecentFilesList& recentFilesList() const override { return files; }
    async::Notification recentFilesListChanged() const override { return {}; }
    void prependRecentFile(const project::RecentFile& file) override { files.insert(files.begin(), file); }
    void moveRecentFile(const io::path_t&, const project::RecentFile&) override {}
    void clearRecentFiles() override { files.clear(); }
    async::Promise<QPixmap> thumbnail(const io::path_t&) const override
    {
        return async::Promise<QPixmap>([](auto resolve) { return resolve(QPixmap()); });
    }

    project::RecentFilesList files;
};

class MeloSaveAsTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        settings()->beginTransaction(false);
        config = std::make_shared<project::ProjectConfiguration>(modularity::globalCtx());
        previousConfig = modularity::globalIoc()->resolve<project::IProjectConfiguration>("save-test");
        if (previousConfig) {
            modularity::globalIoc()->unregister<project::IProjectConfiguration>("save-test");
        }
        modularity::globalIoc()->registerExport<project::IProjectConfiguration>("save-test", config);
        config->init();
        config->setShowMeloStockLossWarning(false);
        config->setCreateBackupBeforeSaving(false);
        config->setShouldAskSaveLocationType(false);
        config->setLastUsedSaveLocationType(project::SaveLocationType::Local);
        ctx = std::make_shared<modularity::Context>(915);
        auto* services = modularity::ioc(ctx);
        global = std::make_shared<context::GlobalContext>();
        interactive = std::make_shared<testing::NiceMock<InteractiveMock> >();
        dispatcher = std::make_shared<actions::ActionsDispatcher>();
        recent = std::make_shared<SaveTestRecentFiles>();
        services->registerExport<context::IGlobalContext>("save-test", global);
        services->registerExport<IInteractive>("save-test", interactive);
        services->registerExport<actions::IActionsDispatcher>("save-test", dispatcher);
        services->registerExport<project::IRecentFilesController>("save-test", recent);
        services->registerExport<project::IOpenSaveProjectScenario>("save-test", new project::OpenSaveProjectScenario(ctx));
        services->registerExport<engraving::rendering::IScoreRenderer>("save-test",
                                                                       modularity::globalIoc()->resolve<engraving::rendering::IScoreRenderer>
                                                                           ("save-test"));
        auto writers = std::make_shared<project::NotationWritersRegister>();
        writers->reg({ "meloscore", "mscz" }, std::make_shared<notation::MscNotationWriter>(engraving::MscIoMode::Zip));
        services->registerExport<project::INotationWritersRegister>("save-test", writers);
        controller = std::make_unique<project::ProjectActionsController>(ctx);
        controller->init();
    }

    void TearDown() override
    {
        global->setCurrentProject(nullptr);
        controller.reset();
        auto* services = modularity::ioc(ctx);
        services->reset();
        modularity::removeIoC(ctx);
        delete services;
        modularity::globalIoc()->unregister<project::IProjectConfiguration>("save-test");
        if (previousConfig) {
            modularity::globalIoc()->registerExport<project::IProjectConfiguration>("save-test", previousConfig);
        }
        settings()->rollbackTransaction(false);
    }

    modularity::ContextPtr ctx;
    std::shared_ptr<project::IProjectConfiguration> previousConfig;
    std::shared_ptr<project::ProjectConfiguration> config;
    std::shared_ptr<context::GlobalContext> global;
    std::shared_ptr<testing::NiceMock<InteractiveMock> > interactive;
    std::shared_ptr<actions::ActionsDispatcher> dispatcher;
    std::shared_ptr<SaveTestRecentFiles> recent;
    std::unique_ptr<project::ProjectActionsController> controller;
};
}

TEST_F(MeloSaveAsTests, ActualActionOffersANamePreservesSourceAndReopensCanonicalScore)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString source = directory.filePath("source.meloscore");
    const QString destination = directory.filePath("Named acceptance copy.meloscore");
    ASSERT_TRUE(QFile::copy(QString::fromUtf8(MELO_UI_TEST_DATA_ROOT) + "/jimstaff_data/m7-gate.mscz", source));
    QFile original(source);
    ASSERT_TRUE(original.open(QIODevice::ReadOnly));
    const QByteArray bytes = original.readAll();
    original.close();
    project::ProjectCreator creator;
    auto scoreProject = creator.newProject(ctx);
    project::OpenParams params;
    params.disablePlayback = true;
    const Ret loaded = scoreProject->load(io::path_t(source), params);
    ASSERT_TRUE(loaded) << loaded.toString();
    global->setCurrentProject(scoreProject);
    auto* score = scoreProject->masterNotation()->notation()->elements()->msScore();
    const String reference = score->metaTag(engraving::melo::REFERENCE_TIMELINE_TAG);
    ASSERT_FALSE(reference.isEmpty());
    const auto notes = writtenNotes(score);
    ASSERT_FALSE(notes.isEmpty());
    const auto state = QJsonDocument::fromJson(score->staff(0)->staffType(engraving::Fraction(0, 1))->meloStateJson().toQString().toUtf8());
    EXPECT_CALL(*interactive, selectSavingFileSync(testing::_, testing::_, testing::_, true))
    .WillOnce(testing::Invoke([&](const std::string& title, const io::path_t& suggestion,
                                  const std::vector<std::string>& filters, bool) {
        EXPECT_EQ(title, "Save score");
        EXPECT_TRUE(suggestion.toQString().endsWith(".meloscore"));
        EXPECT_TRUE(std::any_of(filters.begin(), filters.end(), [](const auto& filter) {
            return filter.find("*.meloscore") != std::string::npos;
        }));
        return io::path_t(destination);
    }));
    dispatcher->dispatch("file-save-as");
    ASSERT_TRUE(QFile::exists(destination));
    EXPECT_EQ(scoreProject->path(), io::path_t(destination));
    ASSERT_TRUE(original.open(QIODevice::ReadOnly));
    EXPECT_EQ(original.readAll(), bytes);
    auto reopened = creator.newProject(ctx);
    const Ret readCopy = reopened->load(io::path_t(destination), params);
    ASSERT_TRUE(readCopy) << readCopy.toString();
    auto* copyScore = reopened->masterNotation()->notation()->elements()->msScore();
    EXPECT_EQ(copyScore->metaTag(engraving::melo::REFERENCE_TIMELINE_TAG), reference);
    EXPECT_EQ(writtenNotes(copyScore), notes);
    EXPECT_EQ(QJsonDocument::fromJson(copyScore->staff(0)->staffType(engraving::Fraction(0, 1))->meloStateJson().toQString().toUtf8()),
              state);
    EXPECT_EQ(recent->files.size(), 1u);
}
