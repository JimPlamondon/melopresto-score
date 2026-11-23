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

#include "claptypes.h"

using namespace muse::clap;

//! CLAP is the CLever Audio Plug-in interface. The module is built from the
//! headers of one pinned commit, whose version these macros state.
TEST(ClapHeadersTests, TheModuleIsBuiltFromThePinnedHeaders)
{
    EXPECT_EQ(CLAP_VERSION_MAJOR, 1);
    EXPECT_EQ(CLAP_VERSION_MINOR, 2);
    EXPECT_EQ(CLAP_VERSION_REVISION, 10);
}

//! Every event this host sends fits the one storage it keeps events in.
TEST(ClapHeadersTests, OneEventStorageHoldsEveryKindOfEventSent)
{
    EXPECT_GE(sizeof(ClapEventStorage), sizeof(clap_event_note_t));
    EXPECT_GE(sizeof(ClapEventStorage), sizeof(clap_event_note_expression_t));
    EXPECT_GE(sizeof(ClapEventStorage), sizeof(clap_event_param_value_t));
    EXPECT_GE(sizeof(ClapEventStorage), sizeof(clap_event_midi_t));
    EXPECT_EQ(std::string(CLAP_PACKAGE_EXTENSION), "clap");
}
