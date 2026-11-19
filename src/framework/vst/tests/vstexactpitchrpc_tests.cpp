/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
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

// MeloSynth VST3 workstream (2026-08-19): the optional exact-pitch tail of
// PitchContext on the audio RPC, in all four writer/reader directions:
//  1. old writer → new reader: a pre-change payload (golden bytes captured
//     from the fork at c80253ec97, before this change) decodes as stock
//     behaviour (no exact pitch);
//  2. new writer, field absent → old reader: the encoding is byte-identical
//     to the golden pre-change bytes (stock events unchanged);
//  3. new writer, field present → old reader: MECHANICALLY IMPOSSIBLE for
//     this positional msgpack — the old reader has no slot for the tail and
//     would misparse the next field. Documented here as the exact boundary:
//     both ends of the audio RPC are always the same MuseScore build (the
//     in-process desktop channel and the web-worker channel), so no old
//     reader exists; the test records the fact instead of pretending.
//  4. new writer → new reader: round trips exactly.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "audio/common/rpc/rpcpacker.h"
#include "mpe/events.h"

using namespace muse;
using namespace muse::mpe;

namespace {
// Captured from the pre-change fork (jims/main c80253ec97) by packing the
// same values with RpcPacker — see the MeloSynth VST3 final report, Phase 4
// red receipt.
const std::vector<uint8_t> GOLDEN_PITCH_CONTEXT = { 0x02, 0x81, 0x0c, 0x0e };
const std::vector<uint8_t> GOLDEN_NOTE_EVENT = {
    0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0xcb, 0x40, 0x32, 0x30, 0xa3, 0xd7, 0x0a, 0x3d, 0x71,
    0x02, 0x81, 0x0c, 0x0e, 0x80, 0x03, 0x81, 0x01, 0x02, 0xc2
};
const std::vector<uint8_t> GOLDEN_PLAYBACK_EVENT = {
    0x01, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0xcb, 0x40, 0x32, 0x30, 0xa3, 0xd7, 0x0a, 0x3d, 0x71,
    0x02, 0x81, 0x0c, 0x0e, 0x80, 0x03, 0x81, 0x01, 0x02, 0xc2
};
const std::vector<uint8_t> GOLDEN_STOCK_NOTE_EVENT = {
    0xd1, 0x03, 0xe8, 0xd1, 0x03, 0xe8, 0xd1, 0x01, 0xf4, 0xd1, 0x01, 0xf4, 0x00, 0x00,
    0xcb, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd1, 0x10, 0x68, 0x80, 0x80,
    0xd1, 0x0b, 0xb8, 0x80, 0xc2
};

PitchContext goldenPitchContext()
{
    PitchContext pitchCtx;
    pitchCtx.nominalPitchLevel = 2;
    pitchCtx.pitchCurve.insert({ 12, 14 });
    return pitchCtx;
}

NoteEvent goldenNoteEvent()
{
    ArrangementContext arr;
    arr.nominalTimestamp = 12;
    arr.actualTimestamp = 13;
    arr.nominalDuration = 14;
    arr.actualDuration = 15;
    arr.voiceLayerIndex = 16;
    arr.staffLayerIndex = 17;
    arr.bps = 18.19;
    ExpressionContext expr;
    expr.nominalDynamicLevel = 3;
    expr.expressionCurve.insert({ 1, 2 });
    expr.velocityOverride = std::nullopt;
    PitchContext pitch = goldenPitchContext();
    return NoteEvent(std::move(arr), std::move(pitch), std::move(expr));
}

std::vector<uint8_t> bytesOf(const ByteArray& b)
{
    return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(b.constData()),
                                reinterpret_cast<const uint8_t*>(b.constData()) + b.size());
}

ByteArray arrayOf(const std::vector<uint8_t>& v)
{
    return ByteArray(reinterpret_cast<const char*>(v.data()), v.size());
}

ExactPitch sampleExact()
{
    ExactPitch e;
    e.frequencyHz = 293.66476791740757;
    e.midiKey = 62;
    e.centsOffset = -0.0000123;
    e.hasLattice = 1;
    e.nPer = -1;
    e.nGen = 3;
    return e;
}
}

// 1. old writer → new reader
TEST(Vst_ExactPitchRpcTests, oldWriterPayloadDecodesAsStock)
{
    PitchContext pitch;
    ASSERT_TRUE(audio::rpc::RpcPacker::unpack(arrayOf(GOLDEN_PITCH_CONTEXT), pitch));
    EXPECT_EQ(pitch.nominalPitchLevel, 2);
    EXPECT_EQ(pitch.pitchCurve.size(), 1u);
    EXPECT_FALSE(pitch.exactPitch.has_value());

    PlaybackEvent event;
    ASSERT_TRUE(audio::rpc::RpcPacker::unpack(arrayOf(GOLDEN_PLAYBACK_EVENT), event));
    ASSERT_TRUE(std::holds_alternative<NoteEvent>(event));
    const NoteEvent& note = std::get<NoteEvent>(event);
    EXPECT_FALSE(note.pitchCtx().exactPitch.has_value());
    EXPECT_EQ(note.arrangementCtx().nominalTimestamp, 12);
    EXPECT_EQ(note.expressionCtx().nominalDynamicLevel, 3);
    EXPECT_TRUE(note == goldenNoteEvent());

    NoteEvent stock;
    ASSERT_TRUE(audio::rpc::RpcPacker::unpack(arrayOf(GOLDEN_STOCK_NOTE_EVENT), stock));
    EXPECT_FALSE(stock.pitchCtx().exactPitch.has_value());
    EXPECT_EQ(stock.pitchCtx().nominalPitchLevel, 4200);
}

// 2. new writer, field absent → old reader (byte-identical encoding)
TEST(Vst_ExactPitchRpcTests, newWriterWithoutExactPitchIsByteIdenticalToThePreChangeEncoding)
{
    EXPECT_EQ(bytesOf(audio::rpc::RpcPacker::pack(goldenPitchContext())), GOLDEN_PITCH_CONTEXT);
    EXPECT_EQ(bytesOf(audio::rpc::RpcPacker::pack(goldenNoteEvent())), GOLDEN_NOTE_EVENT);
    PlaybackEvent pe = goldenNoteEvent();
    EXPECT_EQ(bytesOf(audio::rpc::RpcPacker::pack(pe)), GOLDEN_PLAYBACK_EVENT);
    NoteEvent stock(1000, 500, 0, 0, 4200, 3000, ArticulationMap(), 2.0, 0.0f, PitchCurve());
    EXPECT_EQ(bytesOf(audio::rpc::RpcPacker::pack(stock)), GOLDEN_STOCK_NOTE_EVENT);
}

// 3. new writer, field present → old reader: the documented boundary.
TEST(Vst_ExactPitchRpcTests, newWriterWithExactPitchAppendsATailAnOldReaderCannotSkip)
{
    PitchContext pitch = goldenPitchContext();
    pitch.exactPitch = sampleExact();
    const std::vector<uint8_t> bytes = bytesOf(audio::rpc::RpcPacker::pack(pitch));
    // The stock prefix is unchanged and the tail starts with msgpack `true`.
    ASSERT_GT(bytes.size(), GOLDEN_PITCH_CONTEXT.size());
    EXPECT_TRUE(std::equal(GOLDEN_PITCH_CONTEXT.begin(), GOLDEN_PITCH_CONTEXT.end(), bytes.begin()));
    EXPECT_EQ(bytes[GOLDEN_PITCH_CONTEXT.size()], 0xc3);
    // An old reader consumes only the stock prefix and leaves the tail
    // unread; inside a NoteEvent that tail would be misread as the
    // ExpressionContext — this positional encoding has no field tags, so no
    // compatible encoding exists for that direction. Both RPC ends are the
    // same build, so the direction never occurs in MuseScore.
    msgpack::Cursor cursor(bytes.data(), bytes.size());
    PitchContext stockView;
    msgpack::UnPacker oldReader(cursor);
    oldReader.process(stockView.nominalPitchLevel, stockView.pitchCurve); // what the pre-change reader did
    EXPECT_EQ(stockView.nominalPitchLevel, 2);
    EXPECT_EQ(oldReader.cursor().remain(), bytes.size() - GOLDEN_PITCH_CONTEXT.size()) << "the tail is left over";
}

// 4. new writer → new reader
TEST(Vst_ExactPitchRpcTests, newWriterNewReaderRoundTripsExactPitchInsideNoteEventsAndMaps)
{
    PitchContext pitch = goldenPitchContext();
    pitch.exactPitch = sampleExact();
    PitchContext back;
    ASSERT_TRUE(audio::rpc::RpcPacker::unpack(audio::rpc::RpcPacker::pack(pitch), back));
    EXPECT_TRUE(back == pitch);
    ASSERT_TRUE(back.exactPitch.has_value());
    EXPECT_DOUBLE_EQ(back.exactPitch->frequencyHz, 293.66476791740757);
    EXPECT_EQ(back.exactPitch->nPer, -1);
    EXPECT_EQ(back.exactPitch->nGen, 3);

    // Inside a NoteEvent followed by its ExpressionContext (the peek must
    // not confuse the tail with the articulations map) and inside a
    // PlaybackEventsMap mixing stock and MeloPresto notes.
    NoteEvent melo(100, 50, 0, 0, 3100, 3000, ArticulationMap(), 2.0, 0.0f, PitchCurve(), sampleExact());
    NoteEvent stock(1000, 500, 0, 0, 4200, 3000, ArticulationMap(), 2.0, 0.0f, PitchCurve());
    PlaybackEventsMap map;
    map[100].push_back(melo);
    map[100].push_back(stock);
    map[1000].push_back(stock);
    PlaybackEventsMap unpacked;
    ASSERT_TRUE(audio::rpc::RpcPacker::unpack(audio::rpc::RpcPacker::pack(map), unpacked));
    ASSERT_EQ(unpacked.size(), 2u);
    ASSERT_EQ(unpacked[100].size(), 2u);
    const NoteEvent& j = std::get<NoteEvent>(unpacked[100][0]);
    const NoteEvent& s = std::get<NoteEvent>(unpacked[100][1]);
    EXPECT_TRUE(j == melo);
    EXPECT_TRUE(s == stock);
    ASSERT_TRUE(j.pitchCtx().exactPitch.has_value());
    EXPECT_FALSE(s.pitchCtx().exactPitch.has_value());
    // A truncated tail never yields a half-decoded exact pitch.
    std::vector<uint8_t> cut = bytesOf(audio::rpc::RpcPacker::pack(pitch));
    cut.resize(GOLDEN_PITCH_CONTEXT.size() + 3);
    PitchContext partial;
    audio::rpc::RpcPacker::unpack(arrayOf(cut), partial);
    EXPECT_FALSE(partial.exactPitch.has_value());
}

TEST(Vst_ExactPitchRpcTests, dynamicTonalityProfileRoundTripsWithoutChangingExistingVariantIndices)
{
    DynamicTonalityProfileEvent profile;
    for (size_t i = 0; i < profile.points.size(); ++i) {
        profile.points[i] = { 0x4A500100u + static_cast<uint32_t>(i), static_cast<double>(i) / 25.0 };
    }
    PlaybackEventsMap events;
    events[100].emplace_back(profile);

    const ByteArray packed = audio::rpc::RpcPacker::pack(events);
    PlaybackEventsMap unpacked;
    audio::rpc::RpcPacker::unpack(packed, unpacked);

    ASSERT_EQ(unpacked.at(100).size(), 1u);
    ASSERT_TRUE(std::holds_alternative<DynamicTonalityProfileEvent>(unpacked.at(100).front()));
    EXPECT_EQ(std::get<DynamicTonalityProfileEvent>(unpacked.at(100).front()), profile);
    EXPECT_EQ(PlaybackEvent(NoteEvent()).index(), 1u);
    EXPECT_EQ(PlaybackEvent(ControllerChangeEvent()).index(), 5u);
    EXPECT_EQ(PlaybackEvent(profile).index(), 6u);
}
