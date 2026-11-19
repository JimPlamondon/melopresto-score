/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 */
#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "audio/engine/internal/sequenceio.h"

using namespace muse::audio;
using namespace muse::audio::engine;

namespace {
class TestInput final : public ITrackAudioInput
{
public:
    bool isActive() const override { return false; }
    void setIsActive(bool) override {}
    void setOutputSpec(const OutputSpec&) override {}
    unsigned int audioChannelsCount() const override { return 0; }
    muse::async::Channel<unsigned int> audioChannelsCountChanged() const override { return {}; }
    samples_t process(float*, samples_t) override { return 0; }
    void seek(msecs_t, bool) override {}
    void flush() override {}
    const AudioInputParams& inputParams() const override { return params; }
    void applyInputParams(const AudioInputParams&) override {}
    muse::async::Channel<AudioInputParams> inputParamsChanged() const override { return {}; }
    void setInputParamPlain(uint32_t paramId, double plain) override { received.emplace_back(paramId, plain); }
    void prepareToPlay() override {}
    bool readyToPlay() const override { return true; }
    muse::async::Notification readyToPlayChanged() const override { return {}; }
    bool hasPendingChunks() const override { return false; }
    void processInput() override {}
    InputProcessingProgress inputProcessingProgress() const override { return {}; }
    void clearCache() override {}

    AudioInputParams params;
    std::vector<std::pair<uint32_t, double> > received;
};

class TestTracks final : public IGetTracks
{
public:
    TrackPtr track(TrackId id) const override
    {
        const auto search = tracks.find(id);
        return search == tracks.end() ? nullptr : search->second;
    }

    const TracksMap& allTracks() const override { return tracks; }
    muse::async::Channel<TrackPtr> trackAboutToBeAdded() const override { return {}; }
    muse::async::Channel<TrackPtr> trackAboutToBeRemoved() const override { return {}; }

    TracksMap tracks;
};
}

TEST(SequenceIOTests, RejectsStaleHostParameterAfterInputResourceReplacement)
{
    TestTracks tracks;
    auto input = std::make_shared<TestInput>();
    input->params.resourceMeta.type = AudioResourceType::VstPlugin;
    input->params.resourceMeta.id = "MeloSynth";
    auto track = std::make_shared<EventTrack>();
    track->id = 7;
    track->inputHandler = input;
    tracks.tracks.emplace(track->id, track);
    SequenceIO sequence(&tracks);

    sequence.setInputParamPlain(track->id, "MeloSynth", 0x4A530020u, 690.0);
    ASSERT_EQ(input->received.size(), 1u);

    input->params.resourceMeta.id = "ReplacementSynth";
    sequence.setInputParamPlain(track->id, "MeloSynth", 0x4A530020u, 700.0);
    EXPECT_EQ(input->received.size(), 1u);

    input->params.resourceMeta.id = "MeloSynth";
    input->params.resourceMeta.type = AudioResourceType::MusePlugin;
    sequence.setInputParamPlain(track->id, "MeloSynth", 0x4A530020u, 700.0);
    EXPECT_EQ(input->received.size(), 1u);
}
