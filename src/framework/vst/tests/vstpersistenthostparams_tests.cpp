/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 */
#include <gtest/gtest.h>

#include <condition_variable>
#include <mutex>
#include <thread>

#include <memory>
#include <limits>
#include <unordered_map>
#include <vector>

#include "async/processevents.h"
#include "async/asyncable.h"

#include "internal/vstaudioclient.h"
#include "internal/vstcomponenthandler.h"

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

using namespace muse::vst;

namespace {
constexpr PluginParamId GENERATOR_PARAM_ID = 0x4A530020u;
constexpr PluginParamId TRANSIENT_PARAM_ID = 0x4A530099u;
constexpr PluginParamValue GENERATOR_DEFAULT = 0.55;
constexpr PluginParamValue TRANSIENT_DEFAULT = 0.25;

class CapturingProcessor final : public Steinberg::Vst::AudioEffect
{
public:
    CapturingProcessor()
    {
        // VstAudioClient activates the default input bus even for an
        // instrument. Give this host fixture a valid bus for that lifecycle
        // path; the production MeloPresto Synth processor remains output-only.
        addAudioInput(STR16("Stereo In"), Steinberg::Vst::SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);
        addEventInput(STR16("Events"), 1);
    }

    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override
    {
        ++processCount;
        if (auto* changes = data.inputParameterChanges) {
            for (Steinberg::int32 i = 0; i < changes->getParameterCount(); ++i) {
                auto* queue = changes->getParameterData(i);
                if (!queue) {
                    continue;
                }
                const Steinberg::int32 pointCount = queue->getPointCount();
                for (Steinberg::int32 point = 0; point < pointCount; ++point) {
                    Steinberg::int32 offset = 0;
                    Steinberg::Vst::ParamValue value = 0.0;
                    if (queue->getPoint(point, offset, value) == Steinberg::kResultOk) {
                        received[queue->getParameterId()] = value;
                    }
                }
            }
        }
        return Steinberg::kResultOk;
    }

    int processCount = 0;
    std::unordered_map<PluginParamId, Steinberg::Vst::ParamValue> received;
};

class TestController final : public Steinberg::Vst::EditController
{
public:
    TestController()
    {
        parameters.addParameter(new Steinberg::Vst::RangeParameter(
                                    STR16("Generator"), GENERATOR_PARAM_ID, STR16("cents"),
                                    600.0, 800.0, 710.0));
        parameters.addParameter(new Steinberg::Vst::RangeParameter(
                                    STR16("Transient"), TRANSIENT_PARAM_ID, nullptr,
                                    0.0, 1.0, TRANSIENT_DEFAULT));
    }
};

class TestPluginInstance final : public IVstPluginInstance, public muse::async::Asyncable
{
public:
    TestPluginInstance()
    {
        auto* processor = new CapturingProcessor;
        m_processor = processor;
        m_component = Steinberg::owned(static_cast<Steinberg::Vst::IComponent*>(processor));
        m_controller = Steinberg::owned(static_cast<Steinberg::Vst::IEditController*>(new TestController));
        m_componentHandler = Steinberg::FUnknownPtr<VstComponentHandler>(new VstComponentHandler);
        m_componentHandler->pluginParamChanged().onReceive(this, [this](PluginParamId id, PluginParamValue normalized,
                                                                        PluginParamChangeGeneration generation) {
            m_pluginParamChanges.send(id, normalized, generation);
        });
    }

    const muse::audio::AudioResourceId& resourceId() const override { return m_resourceId; }
    const std::string& name() const override { return m_name; }
    const std::string& className() const override { return m_name; }
    const std::string& classVersion() const override { return m_name; }
    VstPluginInstanceId id() const override { return 1; }
    bool isLoaded() const override { return true; }
    muse::async::Notification loadingCompleted() const override { return {}; }
    PluginViewPtr createView() const override { return nullptr; }
    PluginControllerPtr controller() const override { return m_controller; }
    PluginComponentPtr component() const override { return m_component; }
    PluginMidiMappingPtr midiMapping() const override { return nullptr; }
    void updatePluginConfig(const muse::audio::AudioUnitConfig&) override {}
    void refreshConfig() override {}
    void requestConfigRefresh() override {}
    void requestConfigRefresh(PluginParamChangeGeneration) override {}
    muse::async::Channel<PluginParamId, PluginParamValue, PluginParamChangeGeneration> pluginParamChanged() const override
    {
        return m_pluginParamChanges;
    }

    muse::async::Channel<muse::audio::AudioUnitConfig> pluginSettingsChanged() const override { return {}; }

    CapturingProcessor* processor() const { return m_processor; }
    Steinberg::Vst::IComponentHandler* componentHandler() const { return m_componentHandler; }

private:
    muse::audio::AudioResourceId m_resourceId = "MeloPresto Synth";
    std::string m_name = "MeloPresto Synth";
    CapturingProcessor* m_processor = nullptr;
    PluginControllerPtr m_controller;
    PluginComponentPtr m_component;
    Steinberg::FUnknownPtr<VstComponentHandler> m_componentHandler;
    mutable muse::async::Channel<PluginParamId, PluginParamValue, PluginParamChangeGeneration> m_pluginParamChanges;
};

class PersistentParameterReceiver final : public muse::async::Asyncable
{
public:
    explicit PersistentParameterReceiver(VstAudioClient& client)
        : m_client(client) {}

    void subscribe(const IVstPluginInstancePtr& instance)
    {
        instance->pluginParamChanged().onReceive(this, [this](PluginParamId id, PluginParamValue normalized,
                                                              PluginParamChangeGeneration) {
            m_delivered = m_client.handlePersistentParamChange({ id, normalized });
        });
    }

    bool delivered() const { return m_delivered; }

private:
    VstAudioClient& m_client;
    bool m_delivered = false;
};

muse::audio::OutputSpec validOutputSpec()
{
    return { 48000, 64, 2 };
}
}

TEST(VstPersistentHostParameterTests, DeliversLatestIdleValueAndRestagesItAcrossTransportFlushes)
{
    auto instance = std::make_shared<TestPluginInstance>();
    VstAudioClient client(nullptr);
    client.init(muse::audioplugins::AudioPluginType::Instrument, instance);
    client.loadSupportedParams();
    client.setOutputSpec(validOutputSpec());

    // 696 then Undo to 700 in the score's [600, 800] parameter domain.
    ASSERT_TRUE(client.handlePersistentParamChange({ GENERATOR_PARAM_ID, 0.48 }));
    ASSERT_TRUE(client.handlePersistentParamChange({ GENERATOR_PARAM_ID, 0.5 }));
    EXPECT_DOUBLE_EQ(instance->processor()->received[GENERATOR_PARAM_ID], 0.5);
    const int processCountAfterIdleDelivery = instance->processor()->processCount;

    // Ordinary automation remains transient: stop restores its declared
    // default while retaining the score-owned generator.
    ASSERT_TRUE(client.handleParamChange({ TRANSIENT_PARAM_ID, 0.8 }));
    client.setIsPlaying(true);
    client.setIsPlaying(false);
    client.setIsActive(false);
    client.setIsActive(true);
    std::vector<float> output(128, 0.0f);
    ASSERT_EQ(client.process(output.data(), 64), 64u);

    EXPECT_GT(instance->processor()->processCount, processCountAfterIdleDelivery);
    EXPECT_DOUBLE_EQ(instance->processor()->received[GENERATOR_PARAM_ID], 0.5);
    EXPECT_DOUBLE_EQ(instance->processor()->received[TRANSIENT_PARAM_ID], TRANSIENT_DEFAULT);
}

TEST(VstPersistentHostParameterTests, IdlePersistentValueRequestsRefreshAfterZeroFrameDelivery)
{
    auto instance = std::make_shared<TestPluginInstance>();
    VstAudioClient client(nullptr);
    client.init(muse::audioplugins::AudioPluginType::Instrument, instance);
    client.loadSupportedParams();
    client.setOutputSpec(validOutputSpec());

    ASSERT_TRUE(client.handlePersistentParamChange({ GENERATOR_PARAM_ID, 0.5 }));
    EXPECT_DOUBLE_EQ(instance->processor()->received[GENERATOR_PARAM_ID], 0.5);
    EXPECT_TRUE(client.takePersistentStateRefreshRequest());
    EXPECT_FALSE(client.takePersistentStateRefreshRequest());
}

TEST(VstPersistentHostParameterTests, EditorCloseDefersUntilItsSynchronousTokenReachesAudio)
{
    Steinberg::FUnknownPtr<VstComponentHandler> componentHandler(new VstComponentHandler);
    std::mutex mutex;
    std::condition_variable condition;
    bool audioSubscribed = false;
    bool deliverToAudio = false;
    PluginParamChangeGeneration receivedGeneration = 0;

    std::thread audioThread([&]() {
        muse::async::Asyncable receiver;
        componentHandler->pluginParamChanged().onReceive(&receiver,
                                                         [&](PluginParamId, PluginParamValue,
                                                             PluginParamChangeGeneration generation) {
            receivedGeneration = generation;
        });
        {
            std::lock_guard lock(mutex);
            audioSubscribed = true;
        }
        condition.notify_one();

        std::unique_lock lock(mutex);
        condition.wait(lock, [&]() { return deliverToAudio; });
        lock.unlock();
        // The main thread has already closed the edit. Drain the delayed
        // cross-thread channel only now, as the audio engine would.
        muse::async::processMessages(std::this_thread::get_id());
    });

    {
        std::unique_lock lock(mutex);
        condition.wait(lock, [&]() { return audioSubscribed; });
    }

    int editorCloseNotifications = 0;
    componentHandler->pluginParamsChanged().onNotify(nullptr, [&]() {
        ++editorCloseNotifications;
    });
    Steinberg::Vst::IComponentHandler* handler = componentHandler;
    EXPECT_EQ(handler->performEdit(GENERATOR_PARAM_ID, 0.5), Steinberg::kResultOk);
    EXPECT_TRUE(componentHandler->hasPendingParamChange());
    EXPECT_EQ(handler->endEdit(GENERATOR_PARAM_ID), Steinberg::kResultOk);
    EXPECT_EQ(editorCloseNotifications, 1);
    // performEdit() raises this before Channel queues work for the audio
    // subscriber, so editor close cannot serialize the preceding state.
    EXPECT_TRUE(componentHandler->hasPendingParamChange());

    {
        std::lock_guard lock(mutex);
        deliverToAudio = true;
    }
    condition.notify_one();
    audioThread.join();

    EXPECT_EQ(receivedGeneration, 1u);
    EXPECT_TRUE(componentHandler->acknowledgeParamChange(receivedGeneration));
    EXPECT_FALSE(componentHandler->hasPendingParamChange());
}

TEST(VstPersistentHostParameterTests, EditorPerformEditReachesPersistentClient)
{
    auto instance = std::make_shared<TestPluginInstance>();
    VstAudioClient client(nullptr);
    client.init(muse::audioplugins::AudioPluginType::Instrument, instance);
    client.loadSupportedParams();
    client.setOutputSpec(validOutputSpec());

    PersistentParameterReceiver receiver(client);
    receiver.subscribe(instance);

    Steinberg::Vst::IComponentHandler* handler = instance->componentHandler();
    ASSERT_NE(handler, nullptr);
    ASSERT_EQ(handler->beginEdit(GENERATOR_PARAM_ID), Steinberg::kResultOk);
    ASSERT_EQ(handler->performEdit(GENERATOR_PARAM_ID, 0.5), Steinberg::kResultOk);
    ASSERT_TRUE(receiver.delivered());
    EXPECT_DOUBLE_EQ(instance->processor()->received[GENERATOR_PARAM_ID], 0.5);
    ASSERT_EQ(handler->endEdit(GENERATOR_PARAM_ID), Steinberg::kResultOk);
    EXPECT_DOUBLE_EQ(instance->processor()->received[GENERATOR_PARAM_ID], 0.5);
}

TEST(VstPersistentHostParameterTests, PlayingEditorValueRequestsStateRefreshOnlyAfterProcessorDelivery)
{
    auto instance = std::make_shared<TestPluginInstance>();
    VstAudioClient client(nullptr);
    client.init(muse::audioplugins::AudioPluginType::Instrument, instance);
    client.loadSupportedParams();
    client.setOutputSpec(validOutputSpec());
    client.setIsPlaying(true);

    ASSERT_TRUE(client.handlePersistentParamChange({ GENERATOR_PARAM_ID, 0.5 }));
    EXPECT_FALSE(client.takePersistentStateRefreshRequest());

    std::vector<float> output(128, 0.0f);
    ASSERT_EQ(client.process(output.data(), 64), 64u);
    EXPECT_DOUBLE_EQ(instance->processor()->received[GENERATOR_PARAM_ID], 0.5);
    EXPECT_TRUE(client.takePersistentStateRefreshRequest());
    EXPECT_FALSE(client.takePersistentStateRefreshRequest());
}

TEST(VstPersistentHostParameterTests, PlayingSecondValueWaitsForItsOwnProcessorDelivery)
{
    auto instance = std::make_shared<TestPluginInstance>();
    VstAudioClient client(nullptr);
    client.init(muse::audioplugins::AudioPluginType::Instrument, instance);
    client.loadSupportedParams();
    client.setOutputSpec(validOutputSpec());
    client.setIsPlaying(true);

    ASSERT_TRUE(client.handlePersistentParamChange({ GENERATOR_PARAM_ID, 0.48 }));
    std::vector<float> output(128, 0.0f);
    ASSERT_EQ(client.process(output.data(), 64), 64u);

    // A second editor value arriving after the first render block must keep
    // the state scan deferred until the second block has applied it.
    ASSERT_TRUE(client.handlePersistentParamChange({ GENERATOR_PARAM_ID, 0.5 }));
    EXPECT_FALSE(client.takePersistentStateRefreshRequest());
    ASSERT_EQ(client.process(output.data(), 64), 64u);
    EXPECT_DOUBLE_EQ(instance->processor()->received[GENERATOR_PARAM_ID], 0.5);
    EXPECT_TRUE(client.takePersistentStateRefreshRequest());
}

TEST(VstPersistentHostParameterTests, RejectsUnsupportedOrOutOfRangeNormalizedEditorValues)
{
    auto instance = std::make_shared<TestPluginInstance>();
    VstAudioClient client(nullptr);
    client.init(muse::audioplugins::AudioPluginType::Instrument, instance);
    client.loadSupportedParams();
    client.setOutputSpec(validOutputSpec());

    EXPECT_FALSE(client.handlePersistentParamChange({ GENERATOR_PARAM_ID, std::numeric_limits<double>::quiet_NaN() }));
    EXPECT_FALSE(client.handlePersistentParamChange({ GENERATOR_PARAM_ID, -0.01 }));
    EXPECT_FALSE(client.handlePersistentParamChange({ GENERATOR_PARAM_ID, 1.01 }));
    EXPECT_FALSE(client.handlePersistentParamChange({ 0x4A53FFFFu, 0.5 }));
    EXPECT_EQ(instance->processor()->processCount, 0);
}
