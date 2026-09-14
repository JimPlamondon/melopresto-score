/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

#ifndef MU_ENGRAVING_STAFFLINES_H
#define MU_ENGRAVING_STAFFLINES_H

#include <vector>

#include "engravingitem.h"

namespace mu::engraving {
//-------------------------------------------------------------------
//   @@ StaffLines
///    The StaffLines class is the graphic representation of a staff,
///    it draws the horizontal staff lines.
//-------------------------------------------------------------------

class StaffLines final : public EngravingItem
{
    OBJECT_ALLOCATOR(engraving, StaffLines)
    DECLARE_CLASSOF(ElementType::STAFF_LINES)

public:

    StaffLines* clone() const override { return new StaffLines(*this); }

    PointF pagePos() const override;      ///< position in page coordinates
    PointF canvasPos() const override;    ///< position in page coordinates

    const std::vector<LineF>& lines() const { return m_lines; }
    void setLines(const std::vector<LineF>& l) { m_lines = l; }

    // JiMStaff guide lines (Milestone 1): the only drawn lines on a JiMS
    // staff — solid red Do-lines at the period boundaries and the dashed
    // yellow mid-period line. Derived at layout, never serialized. Empty
    // on every non-JiMS staff, which keeps the stock draw path untouched.
    struct MeloGuideLine {
        LineF line;
        bool dashed = false;
        Sid colorStyle = Sid::meloDoLineColor;
        int primeLimit = 0;
    };
    const std::vector<MeloGuideLine>& meloGuideLines() const { return m_meloGuideLines; }
    void setMeloGuideLines(const std::vector<MeloGuideLine>& l) { m_meloGuideLines = l; }

    // Screen-rendered header pitch targets only. Key-change annotations never
    // enter this cache. Layout clears it; a screen paint supplies exact ink.
    struct MeloHeaderPitchTarget {
        RectF ink;
        int periodIndex = 0;
        String state;
        String label;
    };
    const std::vector<MeloHeaderPitchTarget>& meloHeaderPitchTargets() const { return m_meloHeaderPitchTargets; }
    void clearMeloHeaderPitchTargets() const { m_meloHeaderPitchTargets.clear(); }
    void recordMeloHeaderPitchTarget(const MeloHeaderPitchTarget& target) const { m_meloHeaderPitchTargets.push_back(target); }

    Measure* measure() const { return (Measure*)explicitParent(); }
    double y1() const;

    double lw() const { return m_lw; }
    void setLw(double w) { m_lw = w; }

    RectF hitBBox() const override;
    Shape hitShape() const override;

    bool collectForDrawing() const override;

private:
    friend class Factory;
    StaffLines(Measure* parent);

    double m_lw = 0.0;
    std::vector<LineF> m_lines;
    std::vector<MeloGuideLine> m_meloGuideLines;
    mutable std::vector<MeloHeaderPitchTarget> m_meloHeaderPitchTargets;
};
}

#endif // MU_LIBMSCORE_STAFFLINES_H
