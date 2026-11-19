/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 */
#include "vstdynamictonalityprofile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

using namespace muse::vst;

namespace {
struct ExpectedParameter {
    muse::vst::PluginParamId id;
    const char* title;
    int32_t stepCount;
};

constexpr std::array<ExpectedParameter, VstDynamicTonalityProfileHost::POINT_COUNT> EXPECTED = { {
    { 0x4A500100u, "melo.profile.capability", 0 },
    { 0x4A500101u, "melo.profile.slot", 1 },
    { 0x4A500102u, "melo.profile.generation", 0 },
    { 0x4A500103u, "melo.profile.schemaVersion", 0 },
    { 0x4A500104u, "melo.profile.flags", 0 },
    { 0x4A500105u, "melo.profile.nPerMin", 127 },
    { 0x4A500106u, "melo.profile.nPerMax", 127 },
    { 0x4A500107u, "melo.profile.nGenMin", 34 },
    { 0x4A500108u, "melo.profile.nGenMax", 34 },
    { 0x4A500109u, "melo.profile.originNPer", 127 },
    { 0x4A50010Au, "melo.profile.originNGen", 34 },
    { 0x4A50010Bu, "melo.profile.originFrequencyHi", 0 },
    { 0x4A50010Cu, "melo.profile.originFrequencyLo", 0 },
    { 0x4A50010Du, "melo.profile.periodLog2Hi", 0 },
    { 0x4A50010Eu, "melo.profile.periodLog2Lo", 0 },
    { 0x4A50010Fu, "melo.profile.generatorLog2Hi", 0 },
    { 0x4A500110u, "melo.profile.generatorLog2Lo", 0 },
    { 0x4A500111u, "melo.profile.digestWord0", 0 },
    { 0x4A500112u, "melo.profile.digestWord1", 0 },
    { 0x4A500113u, "melo.profile.digestWord2", 0 },
    { 0x4A500114u, "melo.profile.digestWord3", 0 },
    { 0x4A500115u, "melo.profile.digestWord4", 0 },
    { 0x4A500116u, "melo.profile.digestWord5", 0 },
    { 0x4A500117u, "melo.profile.digestWord6", 0 },
    { 0x4A500118u, "melo.profile.digestWord7", 0 },
    { 0x4A500119u, "melo.profile.commit", 0 },
} };

bool titleEquals(const Steinberg::Vst::TChar* actual, const char* expected)
{
    size_t i = 0;
    for (; expected[i] != '\0'; ++i) {
        if (actual[i] != static_cast<Steinberg::Vst::TChar>(expected[i])) {
            return false;
        }
    }
    return actual[i] == 0;
}

bool decodeU32(double normalized, uint32_t& value)
{
    if (!std::isfinite(normalized) || normalized < 0.0 || normalized > 1.0) {
        return false;
    }
    constexpr double MAX = static_cast<double>(UINT32_MAX);
    value = static_cast<uint32_t>(std::llround(normalized * MAX));
    return static_cast<double>(value) / MAX == normalized;
}

double canonicalPayloadValue(PluginParamId id, double normalized)
{
    if (id == 0x4A500101u) {
        return std::round(normalized);
    }
    if (id == 0x4A500105u || id == 0x4A500106u || id == 0x4A500109u) {
        constexpr double SPAN = 127.0;
        return std::round(normalized * SPAN) / SPAN;
    }
    if (id == 0x4A500107u || id == 0x4A500108u || id == 0x4A50010Au) {
        constexpr double SPAN = 34.0;
        return std::round(normalized * SPAN) / SPAN;
    }
    if (id == VstDynamicTonalityProfileHost::PARAM_COMMIT) {
        constexpr double MAX = static_cast<double>((uint64_t(1) << 33) - 1);
        return std::round(normalized * MAX) / MAX;
    }
    constexpr double MAX = static_cast<double>(UINT32_MAX);
    return std::round(normalized * MAX) / MAX;
}
}

bool VstDynamicTonalityProfileHost::discover(const std::vector<PluginParamInfo>& metadata,
                                             PluginParamValue capabilityValue)
{
    m_supported = false;
    std::unordered_set<PluginParamId> ids;
    bool sawReserved = false;
    for (const PluginParamInfo& info : metadata) {
        if (!ids.insert(info.id).second) {
            return false;
        }
        sawReserved = sawReserved || isReserved(info.id);
    }
    if (!sawReserved) {
        return false;
    }
    for (const ExpectedParameter& expected : EXPECTED) {
        const auto it = std::find_if(metadata.cbegin(), metadata.cend(), [&expected](const PluginParamInfo& info) {
            return info.id == expected.id;
        });
        if (it == metadata.cend() || it->stepCount != expected.stepCount
            || it->flags != Steinberg::Vst::ParameterInfo::kNoFlags || !titleEquals(it->title, expected.title)) {
            return false;
        }
    }
    for (const PluginParamInfo& info : metadata) {
        if (isReserved(info.id) && info.id > PARAM_COMMIT) {
            return false;
        }
    }
    uint32_t capability = 0;
    if (!decodeU32(capabilityValue, capability) || capability != CAPABILITY_V1_0) {
        return false;
    }
    m_supported = true;
    return true;
}

bool VstDynamicTonalityProfileHost::validPayload(const mpe::DynamicTonalityProfileEvent& profile)
{
    std::unordered_set<PluginParamId> ids;
    for (const mpe::DynamicTonalityProfilePoint& point : profile.points) {
        if (!isReserved(point.paramId) || point.paramId > PARAM_COMMIT
            || !std::isfinite(point.normalized) || point.normalized < 0.0 || point.normalized > 1.0
            || !ids.insert(point.paramId).second) {
            return false;
        }
    }
    return std::all_of(EXPECTED.cbegin(), EXPECTED.cend(), [&ids](const ExpectedParameter& expected) {
        return ids.find(expected.id) != ids.cend();
    });
}

bool VstDynamicTonalityProfileHost::prepare(const mpe::DynamicTonalityProfileEvent& profile, bool force,
                                            std::array<ParamChangeEvent, POINT_COUNT>& transaction)
{
    if (!m_supported || !validPayload(profile)) {
        return false;
    }
    if (!force && m_hasCurrentProfile && profile == m_currentProfile) {
        return false;
    }
    if (!m_hasCurrentProfile || !(profile == m_currentProfile)) {
        m_currentProfile = profile;
    }
    m_generation += 1;
    m_slot = m_hasCurrentProfile ? 1u - m_slot : 0u;
    m_hasCurrentProfile = true;

    for (size_t i = 0; i < profile.points.size(); ++i) {
        transaction[i] = { profile.points[i].paramId,
                           canonicalPayloadValue(profile.points[i].paramId, profile.points[i].normalized) };
        if (transaction[i].paramId == PARAM_SLOT) {
            transaction[i].value = static_cast<double>(m_slot);
        } else if (transaction[i].paramId == PARAM_GENERATION) {
            transaction[i].value = encodeU32(m_generation);
        } else if (transaction[i].paramId == PARAM_COMMIT) {
            transaction[i].value = encodeCommit(m_slot, m_generation);
        }
    }
    return true;
}

PluginParamValue VstDynamicTonalityProfileHost::encodeU32(uint32_t value)
{
    return static_cast<double>(value) / static_cast<double>(UINT32_MAX);
}

PluginParamValue VstDynamicTonalityProfileHost::encodeCommit(uint32_t slot, uint32_t generation)
{
    constexpr uint64_t MAX = (uint64_t(1) << 33) - 1;
    const uint64_t word = (uint64_t(slot) << 32) | generation;
    return static_cast<double>(word) / static_cast<double>(MAX);
}
