/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#pragma once

#include "types/ret.h"

namespace muse::clap {
enum class Err {
    Undefined       = static_cast<int>(Ret::Code::Undefined),

    //! The numbers follow the VST module's own range of plug-in errors.
    NoBundle = 1451,
    NoEntry = 1452,
    NoFactory = 1453,
    NoInstrument = 1454,
    NoPluginWithId = 1455,
};

inline Ret make_ret(Err e)
{
    const int retCode = static_cast<int>(e);

    switch (e) {
    case Err::Undefined: return Ret(retCode);
    case Err::NoBundle: return Ret(retCode, "Could not open the CLAP bundle");
    case Err::NoEntry: return Ret(retCode, "The file has no usable CLAP entry point");
    case Err::NoFactory: return Ret(retCode, "The CLAP file has no plug-in factory");
    case Err::NoInstrument: return Ret(retCode, "The CLAP file contains no instrument");
    case Err::NoPluginWithId: return Ret(retCode, "The CLAP plug-in is not found");
    }

    return Ret(retCode);
}
}
