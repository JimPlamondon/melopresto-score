/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "audioplugins/internal/audiopluginsutils.h"

#include "internal/clapaudioclient.h"
#include "internal/clapbundle.h"
#include "internal/claphostedplugin.h"
#include "internal/clappluginmetareader.h"
#include "internal/clappluginsscanner.h"

#include "clapfixture.h"

using namespace muse;
using namespace muse::clap;
namespace fs = std::filesystem;

//! CLAP is the CLever Audio Plug-in interface. These tests load the fixture
//! bundle built beside them; no third-party plug-in is needed.
namespace {
ClapBundlePtr fixtureBundle()
{
    std::string problem;
    ClapBundlePtr bundle = ClapBundle::open(fixture::bundlePath(), problem);
    EXPECT_TRUE(bundle) << problem;
    return bundle;
}

double param(const ClapHostedPlugin& plugin, uint32_t id)
{
    double value = -12345.0;
    EXPECT_TRUE(plugin.parameterValue(id, value)) << "parameter " << id;
    return value;
}

//! A folder of things a scan meets: bundles, a plain file, a nested folder,
//! something that is not a plug-in and a bundle with nothing in it.
fs::path scanFolder()
{
    const fs::path root = fs::temp_directory_path() / "melo_clap_scan";
    fs::remove_all(root);
    fs::create_directories(root / "x" / "A.clap" / "Contents" / "MacOS" / "Inner.clap");
    std::ofstream(root / "x" / "A.clap" / "Contents" / "MacOS" / "A") << "code";
    fs::create_directories(root / "x" / "sub");
    std::ofstream(root / "x" / "sub" / "B.clap") << "code";
    std::ofstream(root / "x" / "readme.txt") << "not a plug-in";
    fs::create_directories(root / "y" / "Broken.clap");
    return root;
}

bool allFinite(const std::vector<float>& buffer)
{
    for (const float sample : buffer) {
        if (!std::isfinite(sample)) {
            return false;
        }
    }
    return true;
}

bool allZero(const std::vector<float>& buffer)
{
    for (const float sample : buffer) {
        if (sample != 0.f) {
            return false;
        }
    }
    return true;
}

struct Loaded {
    ClapBundlePtr bundle;
    ClapHostedPlugin plugin;
    ClapAudioClient client;

    explicit Loaded(const char* descriptorId, double sampleRate = 48000.0)
    {
        bundle = fixtureBundle();
        std::string problem;
        EXPECT_TRUE(plugin.create(bundle, descriptorId, problem)) << problem;
        EXPECT_TRUE(plugin.activate(sampleRate));
        client.init(&plugin, 2);
    }

    void setParam(uint32_t id, double plain)
    {
        ClapEventStorage event;
        ASSERT_TRUE(plugin.makeParameterEvent(id, plain, event));
        ASSERT_TRUE(client.pushEvent(event, 0));
    }
};
}

TEST(ClapScannerTests, ClapPathIsAListOfFoldersAndTheStandardFoldersAreThePlatforms)
{
#if defined(_WIN32)
    EXPECT_EQ(clapFoldersOfPathValue("a;b;;c"), (std::vector<std::string> { "a", "b", "c" }));
#else
    EXPECT_EQ(clapFoldersOfPathValue("/a:/b::/c"), (std::vector<std::string> { "/a", "/b", "/c" }));
#endif
    EXPECT_TRUE(clapFoldersOfPathValue("").empty());

#if defined(__APPLE__)
    const std::vector<std::string> standard = clapStandardFolders();
    ASSERT_EQ(standard.size(), 2u);
    EXPECT_EQ(standard[0], "/Library/Audio/Plug-Ins/CLAP");
    EXPECT_EQ(standard[1], std::string(std::getenv("HOME")) + "/Library/Audio/Plug-Ins/CLAP");
#endif
}

TEST(ClapScannerTests, EveryClapFileUnderTheFoldersIsListedOnceAndABundleIsNotLookedInto)
{
    const fs::path root = scanFolder();
    const std::vector<std::string> found = clapFindPlugins({ (root / "x").string(), (root / "missing").string(),
                                                             (root / "y").string(), (root / "x").string() });
    const std::vector<std::string> expected = {
        (root / "x" / "A.clap").string(),
        (root / "x" / "sub" / "B.clap").string(),
        (root / "y" / "Broken.clap").string(),
    };
    EXPECT_EQ(found, expected);

    EXPECT_TRUE(clapFindPlugins({}).empty());
    EXPECT_TRUE(clapFindPlugins({ (root / "missing").string() }).empty());
}

TEST(ClapScannerTests, TheScannerReadsTheClapPathVariableAndSurvivesAnEmptyOne)
{
    const fs::path root = scanFolder();
    ClapPluginsScanner scanner;

    ASSERT_EQ(setenv("CLAP_PATH", (root / "y").string().c_str(), 1), 0);
    const io::paths_t withPath = scanner.scanPlugins();
    EXPECT_NE(std::find(withPath.begin(), withPath.end(), io::path_t((root / "y" / "Broken.clap").string())), withPath.end());

    ASSERT_EQ(setenv("CLAP_PATH", "", 1), 0);
    const io::paths_t withEmpty = scanner.scanPlugins();
    EXPECT_EQ(std::find(withEmpty.begin(), withEmpty.end(), io::path_t((root / "y" / "Broken.clap").string())), withEmpty.end());
    unsetenv("CLAP_PATH");
}

TEST(ClapBundleTests, TheBundleNamesItsOwnExecutableAndListsEveryDescriptor)
{
#if defined(__APPLE__)
    std::string problem;
    const std::string executable = clapExecutablePath(fixture::bundlePath(), problem);
    EXPECT_EQ(fs::path(executable).filename().string(), "fixture-code") << problem;
    EXPECT_NE(fs::path(fixture::bundlePath()).stem().string(), "fixture-code");
#endif

    const ClapBundlePtr bundle = fixtureBundle();
    ASSERT_TRUE(bundle);
    const std::vector<ClapDescriptorInfo> descriptors = bundle->descriptors();
    ASSERT_EQ(descriptors.size(), 3u);
    EXPECT_EQ(descriptors[0].id, fixture::EFFECT_ID);
    EXPECT_EQ(descriptors[1].id, fixture::TONE_ID);
    EXPECT_EQ(descriptors[1].name, "Fixture Tone");
    EXPECT_EQ(descriptors[1].vendor, "MeloPresto Tests");
    EXPECT_EQ(descriptors[1].version, "1.2.3");
    EXPECT_TRUE(descriptors[1].hasFeature("instrument"));
    EXPECT_FALSE(descriptors[0].hasFeature("instrument"));
    EXPECT_EQ(descriptors[2].id, fixture::MIDI_ONLY_ID);
}

TEST(ClapBundleTests, ABrokenBundleAndAMissingFileAreRefusedWithAReason)
{
    const fs::path root = scanFolder();
    std::string problem;
    EXPECT_FALSE(ClapBundle::open((root / "y" / "Broken.clap").string(), problem));
    EXPECT_FALSE(problem.empty());

    problem.clear();
    EXPECT_FALSE(ClapBundle::open((root / "nowhere.clap").string(), problem));
    EXPECT_FALSE(problem.empty());
}

TEST(ClapMetaReaderTests, EachInstrumentDescriptorIsAResourceIdentifiedByItsDescriptorId)
{
    ClapPluginMetaReader reader;
    EXPECT_EQ(reader.metaType(), audio::AudioResourceType::ClapPlugin);
    EXPECT_TRUE(reader.canReadMeta("/some/where/Synth.clap"));
    EXPECT_FALSE(reader.canReadMeta("/some/where/Synth.vst3"));

    const RetVal<audio::AudioResourceMetaList> metas = reader.readMeta(io::path_t(fixture::bundlePath()));
    ASSERT_TRUE(metas.ret) << metas.ret.toString();
    ASSERT_EQ(metas.val.size(), 2u) << "the audio effect in the factory is not an instrument";

    const audio::AudioResourceMeta& tone = metas.val[0];
    EXPECT_EQ(tone.id, fixture::TONE_ID);
    EXPECT_EQ(tone.type, audio::AudioResourceType::ClapPlugin);
    EXPECT_EQ(tone.vendor, "MeloPresto Tests");
    EXPECT_EQ(tone.attributeVal(u"clapName"), String(u"Fixture Tone"));
    EXPECT_EQ(tone.attributeVal(u"clapVersion"), String(u"1.2.3"));
    EXPECT_TRUE(tone.hasNativeEditorSupport);
    EXPECT_EQ(audioplugins::audioPluginTypeFromCategoriesString(tone.attributeVal(audio::CATEGORIES_ATTRIBUTE)),
              audioplugins::AudioPluginType::Instrument);

    const audio::AudioResourceMeta& midiOnly = metas.val[1];
    EXPECT_EQ(midiOnly.id, fixture::MIDI_ONLY_ID);
    EXPECT_EQ(midiOnly.attributeVal(u"clapName"), String(u"Fixture Midi Only"));
    EXPECT_FALSE(midiOnly.hasNativeEditorSupport);
}

TEST(ClapMetaReaderTests, ABrokenBundleIsAnErrorNotACrash)
{
    const fs::path root = scanFolder();
    ClapPluginMetaReader reader;
    const RetVal<audio::AudioResourceMetaList> metas = reader.readMeta(io::path_t((root / "y" / "Broken.clap").string()));
    EXPECT_FALSE(metas.ret);
    EXPECT_TRUE(metas.val.empty());
}

TEST(ClapHostedPluginTests, APlugInIsCreatedByDescriptorIdAndReportsItsPorts)
{
    const ClapBundlePtr bundle = fixtureBundle();
    std::string problem;

    ClapHostedPlugin tone;
    ASSERT_TRUE(tone.create(bundle, fixture::TONE_ID, problem)) << problem;
    EXPECT_EQ(tone.descriptor().name, "Fixture Tone");
    EXPECT_EQ(tone.descriptor().version, "1.2.3");
    EXPECT_TRUE(tone.notePorts().hasNoteInput);
    EXPECT_TRUE(tone.notePorts().takesNoteEvents);
    EXPECT_TRUE(tone.notePorts().takesMidiBytes);
    EXPECT_EQ(tone.audioPorts().outputChannels, (std::vector<uint32_t> { 2 }));
    EXPECT_TRUE(tone.audioPorts().inputChannels.empty());

    ClapHostedPlugin midiOnly;
    ASSERT_TRUE(midiOnly.create(bundle, fixture::MIDI_ONLY_ID, problem)) << problem;
    EXPECT_FALSE(midiOnly.notePorts().takesNoteEvents) << "like Dexed's CLAP build, notes only as MIDI bytes";
    EXPECT_TRUE(midiOnly.notePorts().takesMidiBytes);

    ClapHostedPlugin unknown;
    EXPECT_FALSE(unknown.create(bundle, "org.example.not-there", problem));
    EXPECT_FALSE(problem.empty());
    EXPECT_FALSE(unknown.isCreated());

    ClapHostedPlugin effect;
    EXPECT_FALSE(effect.create(bundle, fixture::EFFECT_ID, problem));
}

TEST(ClapHostedPluginTests, AnActivationIsHandedBetweenTheMainAndTheAudioThread)
{
    using State = ClapHostedPlugin::AudioState;
    Loaded loaded(fixture::TONE_ID);
    ClapHostedPlugin& plugin = loaded.plugin;
    EXPECT_EQ(plugin.audioState(), State::Ready);

    EXPECT_TRUE(plugin.prepareToProcess());
    EXPECT_EQ(plugin.audioState(), State::Processing);

    // The main thread may not deactivate until the audio thread has stopped.
    EXPECT_FALSE(plugin.requestAudioStop());
    EXPECT_EQ(plugin.audioState(), State::StopRequested);
    EXPECT_FALSE(plugin.prepareToProcess()) << "the audio thread acknowledges the stop";
    EXPECT_EQ(plugin.audioState(), State::Stopped);
    EXPECT_TRUE(plugin.requestAudioStop());

    plugin.deactivate();
    EXPECT_EQ(plugin.audioState(), State::Inactive);
    EXPECT_FALSE(plugin.prepareToProcess());

    EXPECT_TRUE(plugin.activate(44100.0));
    EXPECT_EQ(plugin.audioState(), State::Ready);
    EXPECT_DOUBLE_EQ(plugin.sampleRate(), 44100.0);

    // A plug-in that never started processing can be stopped at once.
    EXPECT_TRUE(plugin.requestAudioStop());
    EXPECT_EQ(plugin.audioState(), State::Stopped);
}

TEST(ClapHostedPluginTests, AMainThreadCallbackIsServedOnRequestAndSetupTakesEffectOnlyThen)
{
    Loaded loaded(fixture::MIDI_ONLY_ID);
    EXPECT_EQ(loaded.plugin.serveMainThreadCallbacks(), 0) << "nothing was asked for yet";

    // The MPE (MIDI Polyphonic Expression) Configuration Message as MIDI 1.0
    // control changes on channel index 0: registered parameter 6, 15 member channels.
    const ClapEventStorage message[3] = { fixture::midiEvent(0xB0, 101, 0), fixture::midiEvent(0xB0, 100, 6),
                                          fixture::midiEvent(0xB0, 6, 15) };
    ASSERT_TRUE(loaded.client.deliverInDiscardedBlock(message, 3, CLAP_SETUP_BLOCK_FRAMES));

    EXPECT_EQ(param(loaded.plugin, fixture::MpeEnabled), 0.0) << "the plug-in waits for the main thread";
    EXPECT_EQ(param(loaded.plugin, fixture::MainThreadCalls), 0.0);

    EXPECT_EQ(loaded.plugin.serveMainThreadCallbacks(), 1);
    EXPECT_EQ(param(loaded.plugin, fixture::MpeEnabled), 1.0);
    EXPECT_EQ(param(loaded.plugin, fixture::MainThreadCalls), 1.0);
    EXPECT_EQ(loaded.plugin.serveMainThreadCallbacks(), 0);
}

TEST(ClapHostedPluginTests, StateIsOpaqueBytesThatRoundTripAndEachInstanceKeepsItsOwn)
{
    Loaded first(fixture::TONE_ID);
    Loaded second(fixture::TONE_ID);
    ASSERT_TRUE(first.plugin.hasState());

    first.setParam(fixture::Level, 0.5);
    first.setParam(fixture::Program, 42.0);
    ASSERT_TRUE(first.client.flushParameters());
    second.setParam(fixture::Level, 1.5);
    ASSERT_TRUE(second.client.flushParameters());

    std::vector<uint8_t> firstState;
    std::vector<uint8_t> secondState;
    ASSERT_TRUE(first.plugin.saveState(firstState));
    ASSERT_TRUE(second.plugin.saveState(secondState));
    EXPECT_FALSE(firstState.empty());
    EXPECT_NE(firstState, secondState) << "two instances of one descriptor keep separate state";

    Loaded restoredFirst(fixture::TONE_ID);
    Loaded restoredSecond(fixture::TONE_ID);
    EXPECT_EQ(param(restoredFirst.plugin, fixture::Level), 1.0) << "the default before any state";
    ASSERT_TRUE(restoredFirst.plugin.loadState(firstState));
    ASSERT_TRUE(restoredSecond.plugin.loadState(secondState));
    EXPECT_EQ(param(restoredFirst.plugin, fixture::Level), 0.5);
    EXPECT_EQ(param(restoredFirst.plugin, fixture::Program), 42.0);
    EXPECT_EQ(param(restoredSecond.plugin, fixture::Level), 1.5);
    EXPECT_EQ(param(restoredSecond.plugin, fixture::Program), 0.0);

    // Bytes that are not the plug-in's own are refused and change nothing.
    EXPECT_FALSE(restoredFirst.plugin.loadState({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 }));
    EXPECT_FALSE(restoredFirst.plugin.loadState({}));
    EXPECT_EQ(param(restoredFirst.plugin, fixture::Level), 0.5);
}

TEST(ClapHostedPluginTests, ParameterValuesAreInThePlugInsPlainRange)
{
    Loaded loaded(fixture::TONE_ID);
    ClapEventStorage event;
    ASSERT_TRUE(loaded.plugin.makeParameterEvent(fixture::Level, 1.75, event));
    EXPECT_EQ(event.param.header.type, CLAP_EVENT_PARAM_VALUE);
    EXPECT_EQ(event.param.header.size, sizeof(clap_event_param_value_t));
    EXPECT_EQ(event.param.param_id, static_cast<clap_id>(fixture::Level));
    EXPECT_DOUBLE_EQ(event.param.value, 1.75) << "Level runs from 0 to 2; the value is not scaled to 0 to 1";
    EXPECT_EQ(event.param.note_id, -1);
    EXPECT_EQ(event.param.key, -1);

    EXPECT_FALSE(loaded.plugin.makeParameterEvent(fixture::Level, 2.5, event)) << "outside the range";
    EXPECT_FALSE(loaded.plugin.makeParameterEvent(999, 0.5, event)) << "no such parameter";
    EXPECT_FALSE(loaded.plugin.makeParameterEvent(fixture::Level, std::nan(""), event));
}

TEST(ClapHostedPluginTests, TheEditorOpensInsideAParentWindowAndCloses)
{
    Loaded tone(fixture::TONE_ID);
    ASSERT_EQ(tone.plugin.editorKind(), ClapEditorKind::Embedded);

    int parentStandIn = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    ASSERT_TRUE(tone.plugin.openEditor(ClapEditorKind::Embedded, &parentStandIn, "Fixture Tone", ClapEditorCallbacks(), width, height));
    EXPECT_EQ(width, 320u);
    EXPECT_EQ(height, 200u);
    EXPECT_TRUE(tone.plugin.isEditorOpen());
    EXPECT_EQ(param(tone.plugin, fixture::EditorOpen), 1.0);
    EXPECT_EQ(param(tone.plugin, fixture::EditorParentSet), 1.0);
    EXPECT_FALSE(tone.plugin.openEditor(ClapEditorKind::Embedded, &parentStandIn, "again", ClapEditorCallbacks(), width, height));

    tone.plugin.closeEditor();
    EXPECT_FALSE(tone.plugin.isEditorOpen());
    EXPECT_EQ(param(tone.plugin, fixture::EditorOpen), 0.0);

    Loaded midiOnly(fixture::MIDI_ONLY_ID);
    EXPECT_EQ(midiOnly.plugin.editorKind(), ClapEditorKind::None);
}

TEST(ClapAudioClientTests, NoProcessCallCarriesZeroFramesAndParameterValuesGoThroughTheFlush)
{
    Loaded loaded(fixture::TONE_ID);
    std::vector<float> buffer(2 * 256, 1.f);

    // A block of no frames is no call at all.
    EXPECT_EQ(loaded.client.process(buffer.data(), 0), 0u);
    EXPECT_EQ(param(loaded.plugin, fixture::ProcessCalls), 0.0);

    // Parameter values with no audio go through the parameter flush.
    loaded.setParam(fixture::Level, 0.25);
    EXPECT_EQ(loaded.client.process(buffer.data(), 0), 0u);
    EXPECT_EQ(loaded.client.pendingEvents(), 1u);
    EXPECT_TRUE(loaded.client.flushParameters());
    EXPECT_EQ(loaded.client.pendingEvents(), 0u);
    EXPECT_EQ(param(loaded.plugin, fixture::FlushCalls), 1.0);
    EXPECT_EQ(param(loaded.plugin, fixture::Level), 0.25);
    EXPECT_EQ(param(loaded.plugin, fixture::ProcessCalls), 0.0);

    // A note is not a parameter value: it waits for the next block that has frames.
    ASSERT_TRUE(loaded.client.pushEvent(fixture::noteEvent(CLAP_EVENT_NOTE_ON, 1, 69, 1.0), 0));
    EXPECT_FALSE(loaded.client.flushParameters());
    EXPECT_EQ(loaded.client.process(buffer.data(), 0), 0u);
    EXPECT_EQ(param(loaded.plugin, fixture::NotesStarted), 0.0);
    EXPECT_EQ(loaded.client.process(buffer.data(), 256), 256u);
    EXPECT_EQ(param(loaded.plugin, fixture::NotesStarted), 1.0);
    EXPECT_EQ(param(loaded.plugin, fixture::ProcessCalls), 1.0);

    // The fixture spoils its output for good after a zero-frame call; it got none.
    EXPECT_EQ(param(loaded.plugin, fixture::ZeroFrameProcessCalls), 0.0);
    EXPECT_TRUE(allFinite(buffer));
    EXPECT_FALSE(allZero(buffer));
    EXPECT_EQ(loaded.client.takeFault(), ClapAudioFault::None);
}

TEST(ClapAudioClientTests, AnEventOneSampleAfterItsNoteOnCrossesABlockBoundary)
{
    std::vector<float> buffer(2 * 64);

    // Inside one block: the tuning arrives one sample after the note-on.
    {
        Loaded loaded(fixture::TONE_ID);
        ASSERT_TRUE(loaded.client.pushEvent(fixture::noteEvent(CLAP_EVENT_NOTE_ON, 1, 69, 1.0), 0));
        ASSERT_TRUE(loaded.client.pushEvent(fixture::tuningEvent(1, 69, 0.5), 1));
        EXPECT_EQ(loaded.client.process(buffer.data(), 64), 64u);
        EXPECT_EQ(param(loaded.plugin, fixture::LastTuningDelay), 1.0);
    }

    // The note-on on a block's last sample: the tuning opens the next block.
    {
        Loaded loaded(fixture::TONE_ID);
        ASSERT_TRUE(loaded.client.pushEvent(fixture::noteEvent(CLAP_EVENT_NOTE_ON, 1, 69, 1.0), 0));
        ASSERT_TRUE(loaded.client.pushEvent(fixture::tuningEvent(1, 69, 0.5), 1));
        EXPECT_EQ(loaded.client.process(buffer.data(), 1), 1u);
        EXPECT_EQ(loaded.client.pendingEvents(), 1u) << "the tuning waits for the next block";
        EXPECT_EQ(param(loaded.plugin, fixture::LastTuningDelay), -1.0);
        EXPECT_EQ(loaded.client.process(buffer.data(), 64), 64u);
        EXPECT_EQ(param(loaded.plugin, fixture::LastTuningDelay), 1.0);
        EXPECT_EQ(loaded.client.pendingEvents(), 0u);
    }
}

TEST(ClapAudioClientTests, ALongBlockIsSplitAndAMonoOrStereoOutputFillsTheMixersChannels)
{
    Loaded loaded(fixture::TONE_ID);
    const uint32_t frames = CLAP_MAX_BLOCK_FRAMES * 2 + 100;
    std::vector<float> buffer(2 * frames, 0.f);
    ASSERT_TRUE(loaded.client.pushEvent(fixture::noteEvent(CLAP_EVENT_NOTE_ON, 1, 69, 1.0), 0));
    EXPECT_EQ(loaded.client.process(buffer.data(), frames), frames);
    EXPECT_EQ(param(loaded.plugin, fixture::ProcessCalls), 3.0);
    EXPECT_EQ(param(loaded.plugin, fixture::NotesStarted), 1.0);
    // Key 69 is 440 Hz; the sine runs unbroken across the split.
    EXPECT_NEAR(fixture::centsBetween(fixture::measuredFrequency(buffer, 2, 48000.0, 480), 440.0), 0.0, 0.05);
    EXPECT_FLOAT_EQ(buffer[2 * 1000], buffer[2 * 1000 + 1]);

    loaded.client.setVolumeGain(0.f);
    EXPECT_EQ(loaded.client.process(buffer.data(), 128), 128u);
    EXPECT_TRUE(allZero(std::vector<float>(buffer.begin(), buffer.begin() + 256)));
}

TEST(ClapAudioClientTests, NonFiniteAudioStopsThePathWithAFaultAndSilence)
{
    Loaded loaded(fixture::TONE_ID);
    std::vector<float> buffer(2 * 128, 1.f);
    ASSERT_TRUE(loaded.client.pushEvent(fixture::noteEvent(CLAP_EVENT_NOTE_ON, 1, 60, 1.0), 0));
    loaded.setParam(fixture::EmitNonFinite, 1.0);

    EXPECT_EQ(loaded.client.process(buffer.data(), 128), 0u);
    EXPECT_TRUE(allZero(buffer)) << "nothing that is not a number reaches the mixer";
    EXPECT_EQ(loaded.client.takeFault(), ClapAudioFault::NonFiniteAudio);
    EXPECT_EQ(loaded.client.takeFault(), ClapAudioFault::None) << "reported once";
    EXPECT_TRUE(loaded.client.isStopped());

    const double calls = param(loaded.plugin, fixture::ProcessCalls);
    std::fill(buffer.begin(), buffer.end(), 1.f);
    EXPECT_EQ(loaded.client.process(buffer.data(), 128), 0u);
    EXPECT_TRUE(allZero(buffer));
    EXPECT_EQ(param(loaded.plugin, fixture::ProcessCalls), calls) << "a stopped path does not call the plug-in";
    EXPECT_FALSE(loaded.client.pushEvent(fixture::noteEvent(CLAP_EVENT_NOTE_ON, 2, 61, 1.0), 0));
}

TEST(ClapAudioClientTests, AProcessErrorAndAnEventOverflowStopThePathWithAFault)
{
    std::vector<float> buffer(2 * 128, 1.f);
    {
        Loaded loaded(fixture::TONE_ID);
        loaded.setParam(fixture::FailProcess, 1.0);
        EXPECT_EQ(loaded.client.process(buffer.data(), 128), 0u);
        EXPECT_TRUE(allZero(buffer));
        EXPECT_EQ(loaded.client.takeFault(), ClapAudioFault::ProcessError);
        EXPECT_TRUE(loaded.client.isStopped());
    }
    {
        Loaded loaded(fixture::TONE_ID);
        for (uint32_t i = 0; i < CLAP_EVENT_CAPACITY; ++i) {
            ASSERT_TRUE(loaded.client.pushEvent(fixture::tuningEvent(1, 60, 0.0), 0));
        }
        EXPECT_FALSE(loaded.client.pushEvent(fixture::noteEvent(CLAP_EVENT_NOTE_ON, 1, 60, 1.0), 0));
        EXPECT_EQ(loaded.client.takeFault(), ClapAudioFault::EventOverflow);
        EXPECT_TRUE(loaded.client.isStopped());
    }
}

TEST(ClapAudioClientTests, AFaultStopsThePathUntilThePlugInIsActivatedAgain)
{
    using State = ClapHostedPlugin::AudioState;
    Loaded loaded(fixture::TONE_ID);
    std::vector<float> buffer(2 * 128, 1.f);
    loaded.setParam(fixture::FailProcess, 1.0);
    EXPECT_EQ(loaded.client.process(buffer.data(), 128), 0u);
    ASSERT_TRUE(loaded.client.isStopped());
    EXPECT_EQ(loaded.client.takeFault(), ClapAudioFault::ProcessError);

    // The main thread wants to restart the plug-in (a new sample rate, or the
    // plug-in asked). A stopped path still answers the stop request, or the
    // restart would never happen.
    EXPECT_FALSE(loaded.plugin.requestAudioStop());
    EXPECT_EQ(loaded.client.process(buffer.data(), 128), 0u);
    EXPECT_EQ(loaded.plugin.audioState(), State::Stopped);
    ASSERT_TRUE(loaded.plugin.requestAudioStop());

    // Deactivated and activated again, as the application does it. The new
    // activation is what clears the fault; nothing else does.
    loaded.plugin.deactivate();
    EXPECT_EQ(loaded.client.process(buffer.data(), 128), 0u);
    EXPECT_TRUE(loaded.client.isStopped());
    ASSERT_TRUE(loaded.plugin.activate(48000.0));

    ClapEventStorage healthy;
    ASSERT_TRUE(loaded.plugin.makeParameterEvent(fixture::FailProcess, 0.0, healthy));
    ASSERT_TRUE(loaded.client.pushEvent(healthy, 0)) << "the path takes events again";
    ASSERT_TRUE(loaded.client.pushEvent(fixture::noteEvent(CLAP_EVENT_NOTE_ON, 1, 69, 1.0), 0));
    EXPECT_EQ(loaded.client.process(buffer.data(), 128), 128u);
    EXPECT_FALSE(loaded.client.isStopped());
    EXPECT_FALSE(allZero(buffer));
    EXPECT_EQ(loaded.client.takeFault(), ClapAudioFault::None);
}

TEST(ClapHostedPluginTests, APlugInHeldAfterARestartIsNotProcessedUntilTheAudioSideIsPrepared)
{
    using State = ClapHostedPlugin::AudioState;
    Loaded loaded(fixture::TONE_ID);
    ASSERT_TRUE(loaded.plugin.requestAudioStop());
    loaded.plugin.deactivate();
    const uint32_t before = loaded.plugin.activationCount();

    // Activated again for a restart: its ports may have changed, so the audio
    // thread may not touch it until the audio side has prepared for them.
    ASSERT_TRUE(loaded.plugin.activate(44100.0, CLAP_MAX_BLOCK_FRAMES, /*held*/ true));
    EXPECT_EQ(loaded.plugin.activationCount(), before + 1);
    EXPECT_EQ(loaded.plugin.audioState(), State::Held);
    EXPECT_FALSE(loaded.plugin.prepareToProcess());
    EXPECT_EQ(loaded.plugin.audioState(), State::Held);
    EXPECT_EQ(param(loaded.plugin, fixture::ProcessCalls), 0.0);

    loaded.plugin.releaseToAudio();
    EXPECT_EQ(loaded.plugin.audioState(), State::Ready);
    EXPECT_TRUE(loaded.plugin.prepareToProcess());

    // A held plug-in can be withdrawn again without the audio thread.
    EXPECT_FALSE(loaded.plugin.requestAudioStop());
    EXPECT_FALSE(loaded.plugin.prepareToProcess());
    loaded.plugin.deactivate();
    ASSERT_TRUE(loaded.plugin.activate(44100.0, CLAP_MAX_BLOCK_FRAMES, true));
    EXPECT_TRUE(loaded.plugin.requestAudioStop());
    EXPECT_EQ(loaded.plugin.audioState(), State::Stopped);
}

TEST(ClapHostedPluginTests, OnlyAThreadInsideAnAudioCallIsCalledTheAudioThread)
{
    Loaded loaded(fixture::MIDI_ONLY_ID);
    // The MPE Configuration Message makes the fixture ask for its main-thread callback.
    const ClapEventStorage message[3] = { fixture::midiEvent(0xB0, 101, 0), fixture::midiEvent(0xB0, 100, 6),
                                          fixture::midiEvent(0xB0, 6, 15) };
    ASSERT_TRUE(loaded.client.deliverInDiscardedBlock(message, 3, CLAP_SETUP_BLOCK_FRAMES));
    ASSERT_EQ(loaded.plugin.serveMainThreadCallbacks(), 1);

    EXPECT_EQ(param(loaded.plugin, fixture::AudioThreadInProcess), 1.0);
    EXPECT_EQ(param(loaded.plugin, fixture::AudioThreadOnMainThread), 0.0);
    EXPECT_EQ(param(loaded.plugin, fixture::AudioThreadOnOtherThread), 0.0)
        << "a thread of the plug-in's own is neither the main nor the audio thread";
}

TEST(ClapAudioClientTests, SetupForAPlugInThatCannotProcessIsNotQueuedAndNotLost)
{
    Loaded loaded(fixture::MIDI_ONLY_ID);
    ASSERT_TRUE(loaded.plugin.requestAudioStop());
    loaded.plugin.deactivate();

    const ClapEventStorage setup[3] = { fixture::midiEvent(0xB0, 101, 0), fixture::midiEvent(0xB0, 100, 6),
                                        fixture::midiEvent(0xB0, 6, 15) };
    EXPECT_FALSE(loaded.client.deliverInDiscardedBlock(setup, 3, CLAP_SETUP_BLOCK_FRAMES)) << "the caller still owes the setup";
    EXPECT_EQ(loaded.client.pendingEvents(), 0u);
    EXPECT_EQ(loaded.client.takeFault(), ClapAudioFault::None);

    // Activated: the same events are delivered, apart from any note waiting in the queue.
    ASSERT_TRUE(loaded.plugin.activate(48000.0));
    ASSERT_TRUE(loaded.client.pushEvent(fixture::midiEvent(0x91, 60, 100), 0));
    EXPECT_TRUE(loaded.client.deliverInDiscardedBlock(setup, 3, CLAP_SETUP_BLOCK_FRAMES));
    EXPECT_EQ(loaded.client.pendingEvents(), 1u) << "the note waits for a block that is kept";
    EXPECT_EQ(param(loaded.plugin, fixture::NotesStarted), 0.0);
    EXPECT_EQ(loaded.plugin.serveMainThreadCallbacks(), 1);
    EXPECT_EQ(param(loaded.plugin, fixture::MpeEnabled), 1.0);
}

TEST(ClapAudioClientTests, EventsForAPlugInThatIsNotActivatedAreLeftOutWithAFault)
{
    Loaded loaded(fixture::TONE_ID);
    std::vector<float> buffer(2 * 128, 1.f);
    ASSERT_TRUE(loaded.plugin.requestAudioStop());
    loaded.plugin.deactivate();

    ASSERT_TRUE(loaded.client.pushEvent(fixture::noteEvent(CLAP_EVENT_NOTE_ON, 1, 60, 1.0), 0));
    EXPECT_EQ(loaded.client.process(buffer.data(), 128), 0u);
    EXPECT_TRUE(allZero(buffer));
    EXPECT_EQ(loaded.client.takeFault(), ClapAudioFault::EventsWhileInactive);
    EXPECT_FALSE(loaded.client.isStopped());
    EXPECT_EQ(param(loaded.plugin, fixture::ProcessCalls), 0.0);

    // It is said once while the plug-in stays inactive, not once a block.
    ASSERT_TRUE(loaded.client.pushEvent(fixture::noteEvent(CLAP_EVENT_NOTE_ON, 2, 62, 1.0), 0));
    EXPECT_EQ(loaded.client.process(buffer.data(), 128), 0u);
    EXPECT_EQ(loaded.client.takeFault(), ClapAudioFault::None);

    // Activated again, it plays; left inactive once more, that is said again.
    ASSERT_TRUE(loaded.plugin.activate(48000.0));
    ASSERT_TRUE(loaded.client.pushEvent(fixture::noteEvent(CLAP_EVENT_NOTE_ON, 3, 64, 1.0), 0));
    EXPECT_EQ(loaded.client.process(buffer.data(), 128), 128u);
    EXPECT_EQ(param(loaded.plugin, fixture::NotesStarted), 1.0);
    loaded.client.stopNow();
    ASSERT_TRUE(loaded.client.pushEvent(fixture::noteEvent(CLAP_EVENT_NOTE_ON, 4, 65, 1.0), 0));
    EXPECT_EQ(loaded.client.process(buffer.data(), 128), 0u);
    EXPECT_EQ(loaded.client.takeFault(), ClapAudioFault::EventsWhileInactive);
}
