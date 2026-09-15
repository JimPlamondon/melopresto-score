/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */
#ifndef MU_ENGRAVING_MELOPITCHLABEL_H
#define MU_ENGRAVING_MELOPITCHLABEL_H

#include "draw/types/font.h"
#include "draw/types/geometry.h"
#include "types/string.h"
#include "engraving/types/symid.h"

#include "engraving/iengravingfont.h"

namespace muse::draw {
class Painter;
}

namespace mu::engraving::melo {
struct PitchLabelParts {
    muse::String beforeAccidental;
    muse::String afterAccidental;
    SymId accidental = SymId::noSym;
};

struct PitchLabelLayout {
    PitchLabelParts parts;
    muse::RectF bounds;
    double advance = 0.0;
    double accidentalX = 0.0;
    double accidentalY = 0.0;
    double afterX = 0.0;
    double accidentalMag = 0.0;
};

PitchLabelParts pitchLabelParts(const muse::String& text);
PitchLabelLayout pitchLabelLayout(const muse::String& text, const muse::draw::Font& textFont, const IEngravingFontPtr& engravingFont);
void drawPitchLabel(muse::draw::Painter* painter, const muse::PointF& baselineOrigin, const muse::draw::Font& textFont,
                    const IEngravingFontPtr& engravingFont, const PitchLabelLayout& layout);
}

#endif
