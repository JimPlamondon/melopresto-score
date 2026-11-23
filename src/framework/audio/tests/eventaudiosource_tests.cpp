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

#include "audio/engine/internal/eventaudiosource.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::engine;

//! A track for which no synthesiser could be made (no sound font, for one)
//! will never become ready. Playback and audio export wait for every track
//! that is not ready, so such a track must not be waited for: the export
//! ends, and the track is silent. Why there is no synthesiser is written to
//! the log where that is found out.
TEST(Audio_EventAudioSourceTests, ATrackWithNoSynthesiserIsNotWaitedFor)
{
    EventAudioSource source(1, mpe::PlaybackData(), [](const TrackId) {}, muse::modularity::globalCtx());

    EXPECT_TRUE(source.readyToPlay());

    float buffer[16] = {};
    EXPECT_EQ(source.process(buffer, 8), 0u);
    EXPECT_FALSE(source.isActive());
}
