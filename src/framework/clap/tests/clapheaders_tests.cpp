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

#include <clap/clap.h>

//! CLAP is the CLever Audio Plug-in interface. The module is built from the
//! headers of one pinned commit, whose version these macros state.
TEST(ClapHeadersTests, TheModuleIsBuiltFromThePinnedHeaders)
{
    EXPECT_EQ(CLAP_VERSION_MAJOR, 1);
    EXPECT_EQ(CLAP_VERSION_MINOR, 2);
    EXPECT_EQ(CLAP_VERSION_REVISION, 10);
}
