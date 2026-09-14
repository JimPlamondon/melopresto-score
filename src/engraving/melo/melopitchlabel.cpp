/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */
#include "melopitchlabel.h"

#include "draw/fontmetrics.h"
#include "draw/painter.h"

#include "style/styledef.h"

using namespace muse;
using namespace muse::draw;

namespace mu::engraving::melo {
static const std::pair<String, SymId> ACCIDENTALS[] = {
    { u"\U0001D12B", SymId::accidentalDoubleFlat },
    { u"\U0001D12A", SymId::accidentalDoubleSharp },
    { u"\u266D", SymId::accidentalFlat },
    { u"\u266F", SymId::accidentalSharp },
};

PitchLabelParts pitchLabelParts(const String& text)
{
    PitchLabelParts parts { text, String(), SymId::noSym };
    for (const auto& [symbol, symId] : ACCIDENTALS) {
        const size_t pos = text.indexOf(symbol);
        if (pos == muse::nidx) {
            continue;
        }
        parts.beforeAccidental = text.left(pos);
        parts.afterAccidental = text.mid(pos + symbol.size());
        parts.accidental = symId;
        break;
    }
    return parts;
}

PitchLabelLayout pitchLabelLayout(const String& text, const Font& textFont,
                                  const IEngravingFontPtr& engravingFont)
{
    PitchLabelLayout layout;
    layout.parts = pitchLabelParts(text);
    FontMetrics fm(textFont);
    if (layout.parts.accidental == SymId::noSym || !engravingFont) {
        layout.bounds = fm.boundingRect(text);
        layout.advance = fm.horizontalAdvance(text);
        return layout;
    }

    layout.accidentalMag = textFont.pointSizeF() / StyleDef::DEFAULT_SMUFL_POINT_SIZE();
    const RectF accidentalInk = engravingFont->bbox(layout.parts.accidental, layout.accidentalMag);
    const double beforeAdvance = fm.horizontalAdvance(layout.parts.beforeAccidental);
    layout.accidentalX = std::max(beforeAdvance, fm.boundingRect(layout.parts.beforeAccidental).right()) - accidentalInk.left();
    // Music glyphs are centered on a staff line; text uses a baseline.
    // Align the measured accidental ink with the pitch letter's ink.
    layout.accidentalY = fm.boundingRect(u"B").center().y() - accidentalInk.center().y();
    layout.afterX = layout.accidentalX + std::max(accidentalInk.right(),
                                                  engravingFont->advance(layout.parts.accidental, layout.accidentalMag));
    layout.advance = layout.afterX + fm.horizontalAdvance(layout.parts.afterAccidental);

    layout.bounds = fm.boundingRect(layout.parts.beforeAccidental);
    layout.bounds.unite(accidentalInk.translated(PointF(layout.accidentalX, layout.accidentalY)));
    layout.bounds.unite(fm.boundingRect(layout.parts.afterAccidental).translated(PointF(layout.afterX, 0.0)));
    return layout;
}

void drawPitchLabel(Painter* painter, const PointF& baselineOrigin, const Font& textFont,
                    const IEngravingFontPtr& engravingFont, const PitchLabelLayout& layout)
{
    if (layout.parts.accidental == SymId::noSym || !engravingFont) {
        painter->setFont(textFont);
        painter->drawText(baselineOrigin, layout.parts.beforeAccidental);
        return;
    }

    if (!layout.parts.beforeAccidental.isEmpty()) {
        painter->setFont(textFont);
        painter->drawText(baselineOrigin, layout.parts.beforeAccidental);
    }
    engravingFont->draw(layout.parts.accidental, painter, layout.accidentalMag,
                        baselineOrigin + PointF(layout.accidentalX, layout.accidentalY));
    if (!layout.parts.afterAccidental.isEmpty()) {
        painter->setFont(textFont);
        painter->drawText(baselineOrigin + PointF(layout.afterX, 0.0), layout.parts.afterAccidental);
    }
}
}
