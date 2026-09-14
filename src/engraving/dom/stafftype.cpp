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

#include "stafftype.h"

#include "draw/fontmetrics.h"
#include "draw/painter.h"
#include "io/file.h"
#include "translation.h"

#include "../iengravingfont.h"
#include "../melo/melobridge.h"
#include "../melo/melochange.h"
#include "../melo/melopitchlabel.h"
#include "../melo/melostrings.h"

#include "rw/xmlreader.h"
#include "style/defaultstyle.h"
#include "style/style.h"
#include "style/textstyle.h"
#include "types/typesconv.h"

#include "mscore.h"
#include "chord.h"
#include "measure.h"
#include "navigate.h"
#include "note.h"
#include "segment.h"
#include "score.h"
#include "staff.h"
#include "stafftypechange.h"
#include "system.h"

#include "log.h"

using namespace muse::draw;
using namespace muse::io;
using namespace mu::engraving;

namespace mu::engraving {
// HISTORIC TAB BASS STRING NOTATION
// The following constants refer to the specifics of bass string notation in historic
//    (Renaiss./Baroque French and Italian) tablatures.

// how much to lower a bass string note with slashes with respect to line distance (in fraction of line distance)
constexpr double STAFFTYPE_TAB_BASSSLASH_YOFFSET = 0.33;

// The following constants could ideally be customizable values;
//    they are currently constants to simplify implementation;
// Note that these constants do not constrain which strings of an instrument are
//    physically frettable (which is defined in the instrument itself) but fix the
//    number of bass strings for which the notation is able to express a fret number
//    rather than simply a string ordinal.
constexpr int NUM_OF_BASSSTRINGS_WITH_LETTER = 4;     // the max number of bass strings frettable with letter notation (French)
constexpr int NUM_OF_BASSSTRINGS_WITH_NUMBER = 2;     // the max number of bass strings frettable with number notation (Italian)

//---------------------------------------------------------
//   StaffTypeTablature
//---------------------------------------------------------

constexpr double TAB_DEFAULT_DUR_YOFFS = -1.0;

std::vector<TablatureFretFont> StaffType::m_fretFonts = {};
std::vector<TablatureDurationFont> StaffType::m_durationFonts = {};

//---------------------------------------------------------
//   StaffType
//---------------------------------------------------------

StaffType::StaffType()
{
    m_color = configuration()->defaultColor();
    // set reasonable defaults for type-specific members */
    m_symRepeat = TablatureSymbolRepeat::NEVER;
    setDurationFontName(m_durationFonts[0].displayName);
    setFretPresetIdx(0);
}

StaffType::StaffType(StaffGroup sg, const String& xml, const String& name, int lines, int stpOff, double lineDist,
                     bool genClef, bool showBarLines, bool stemless, bool genTimeSig, bool genKeySig, bool showLedgerLines, bool invisible,
                     const Color& color)
    : m_group(sg), m_xmlName(xml), m_name(name),
    m_invisible(invisible),
    m_color(color),
    m_lines(lines),
    m_stepOffset(stpOff),
    m_lineDistance(Spatium(lineDist)),
    m_showBarlines(showBarLines),
    m_showLedgerLines(showLedgerLines),
    m_stemless(stemless),
    m_genClef(genClef),
    m_genTimesig(genTimeSig),
    m_genKeysig(genKeySig)
{
}

StaffType::StaffType(StaffGroup sg, const String& xml, const String& name, int lines, int stpOff, double lineDist,
                     bool genClef, bool showBarLines, bool stemless, bool genTimesig, bool invisible, const Color& color,
                     const String& durFontName, double durFontSize, double durFontUserY, double genDur, bool fretFontUseTextStyle,
                     const String& fretFontName, double fretFontSize, double fretFontUserY,
                     TablatureSymbolRepeat symRepeat, bool linesThrough, TablatureMinimStyle minimStyle, bool onLines,
                     bool showRests, bool stemsDown, bool stemThrough, bool upsideDown, bool showTabFingering, bool useNumbers,
                     bool showBackTied)
{
    UNUSED(invisible);
    m_color = color;
    m_group   = sg;
    m_xmlName = xml;
    m_name    = name;
    setLines(lines);
    setStepOffset(stpOff);
    setLineDistance(Spatium(lineDist));
    setGenClef(genClef);
    setShowBarlines(showBarLines);
    setStemless(stemless);
    setGenTimesig(genTimesig);
    setGenKeysig(sg != StaffGroup::TAB);
    setDurationFontName(durFontName);
    setDurationFontSize(durFontSize);
    setDurationFontUserY(durFontUserY);
    setGenDurations(genDur);
    setFretUseTextStyle(fretFontUseTextStyle);
    if (fretFontUseTextStyle) {
        setFretTextStyle(TextStyleType::TAB_FRET_NUMBER);
    } else {
        setFretPreset(fretFontName);
        setFretFontSize(fretFontSize);
        setFretFontUserY(fretFontUserY);
    }
    setSymbolRepeat(symRepeat);
    setLinesThrough(linesThrough);
    setMinimStyle(minimStyle);
    setOnLines(onLines);
    setShowRests(showRests);
    setStemsDown(stemsDown);
    setStemsThrough(stemThrough);
    setUpsideDown(upsideDown);
    setShowTabFingering(showTabFingering);
    setUseNumbers(useNumbers);
    setShowBackTied(showBackTied);
}

//---------------------------------------------------------
//   translatedGroupName
//---------------------------------------------------------

String StaffType::translatedGroupName() const
{
    return TConv::translatedUserName(m_group);
}

int StaffType::middleLine() const
{
    return m_lines - 1 - m_stepOffset;
}

int StaffType::bottomLine() const
{
    return (m_lines - 1) * 2;
}

//---------------------------------------------------------
//   operator==
//---------------------------------------------------------

bool StaffType::operator==(const StaffType& st) const
{
    bool equal = true;

    equal &= (m_group == st.m_group);
    equal &= (m_xmlName == st.m_xmlName);
    equal &= (m_name == st.m_name);
    equal &= (m_melo == st.m_melo);
    equal &= (m_meloStateJson == st.m_meloStateJson);
    equal &= (m_meloTonicAmbit == st.m_meloTonicAmbit);
    equal &= (m_meloJiLines == st.m_meloJiLines);
    equal &= (m_meloScaleDotLabelMode == st.m_meloScaleDotLabelMode);
    equal &= (m_meloElideOctaves == st.m_meloElideOctaves);
    equal &= (m_meloRatioLineExtentJson == st.m_meloRatioLineExtentJson);
    equal &= (m_userMag == st.m_userMag);
    equal &= (m_yoffset == st.m_yoffset);
    equal &= (m_small == st.m_small);
    equal &= (m_invisible == st.m_invisible);
    equal &= (m_color == st.m_color);
    equal &= (m_lines == st.m_lines);
    equal &= (m_stepOffset == st.m_stepOffset);
    equal &= (m_lineDistance == st.m_lineDistance);
    equal &= (m_showBarlines == st.m_showBarlines);
    equal &= (m_showLedgerLines == st.m_showLedgerLines);
    equal &= (m_stemless == st.m_stemless);
    equal &= (m_genClef == st.m_genClef);
    equal &= (m_genTimesig == st.m_genTimesig);
    equal &= (m_genKeysig == st.m_genKeysig);
    equal &= (m_noteHeadScheme == st.m_noteHeadScheme);
    equal &= (m_durationFontSize == st.m_durationFontSize);
    equal &= (m_durationFontUserY == st.m_durationFontUserY);
    equal &= (m_fretFontSize == st.m_fretFontSize);
    equal &= (m_fretFontUserY == st.m_fretFontUserY);
    equal &= (m_genDurations == st.m_genDurations);
    equal &= (m_linesThrough == st.m_linesThrough);
    equal &= (m_minimStyle == st.m_minimStyle);
    equal &= (m_symRepeat == st.m_symRepeat);
    equal &= (m_onLines == st.m_onLines);
    equal &= (m_showRests == st.m_showRests);
    equal &= (m_stemsDown == st.m_stemsDown);
    equal &= (m_stemsThrough == st.m_stemsThrough);
    equal &= (m_upsideDown == st.m_upsideDown);
    equal &= (m_showTabFingering == st.m_showTabFingering);
    equal &= (m_useNumbers == st.m_useNumbers);
    equal &= (m_showBackTied == st.m_showBackTied);
    equal &= (m_durationBoxH == st.m_durationBoxH);
    equal &= (m_durationBoxY == st.m_durationBoxY);
    equal &= (m_durationFont == st.m_durationFont);
    equal &= (m_durationFontIdx == st.m_durationFontIdx);
    equal &= (m_durationYOffset == st.m_durationYOffset);
    equal &= (m_durationGridYOffset == st.m_durationGridYOffset);
    equal &= (m_fretBoxH == st.m_fretBoxH);
    equal &= (m_deadFretBoxH == st.m_deadFretBoxH);
    equal &= (m_fretBoxY == st.m_fretBoxY);
    equal &= (m_deadFretBoxY == st.m_deadFretBoxY);
    equal &= (m_fretFont == st.m_fretFont);
    equal &= (m_fretFontInfo.family == st.m_fretFontInfo.family);
    equal &= (m_fretYOffset == st.m_fretYOffset);
    equal &= (m_fretUseTextStyle == st.m_fretUseTextStyle);
    equal &= (m_fretTextStyle == st.m_fretTextStyle);
    equal &= (m_fretPresetIdx == st.m_fretPresetIdx);

    return equal;
}

StaffTypes StaffType::type() const
{
    static const std::map<String, StaffTypes> xmlNameToType {
        { u"stdNormal", StaffTypes::STANDARD },
        { u"jims12tet", StaffTypes::MELO_12TET },

        { u"perc1Line", StaffTypes::PERC_1LINE },
        { u"perc2Line", StaffTypes::PERC_2LINE },
        { u"perc3Line", StaffTypes::PERC_3LINE },
        { u"perc5Line", StaffTypes::PERC_5LINE },

        { u"tab4StrSimple", StaffTypes::TAB_4SIMPLE },
        { u"tab4StrCommon", StaffTypes::TAB_4COMMON },
        { u"tab4StrFull", StaffTypes::TAB_4FULL },

        { u"tab5StrSimple", StaffTypes::TAB_5SIMPLE },
        { u"tab5StrCommon", StaffTypes::TAB_5COMMON },
        { u"tab5StrFull", StaffTypes::TAB_5FULL },

        { u"tab6StrSimple", StaffTypes::TAB_6SIMPLE },
        { u"tab6StrCommon", StaffTypes::TAB_6COMMON },
        { u"tab6StrFull", StaffTypes::TAB_6FULL },

        { u"tabUkulele", StaffTypes::TAB_UKULELE },
        { u"tabBalajka", StaffTypes::TAB_BALALAJKA },
        { u"tabDulcimer", StaffTypes::TAB_DULCIMER },

        { u"tab6StrItalian", StaffTypes::TAB_ITALIAN },
        { u"tab6StrFrench", StaffTypes::TAB_FRENCH },

        { u"tab7StrCommon", StaffTypes::TAB_7COMMON },
        { u"tab8StrCommon", StaffTypes::TAB_8COMMON },
        { u"tab9StrCommon", StaffTypes::TAB_9COMMON },
        { u"tab10StrCommon", StaffTypes::TAB_10COMMON },

        { u"tab7StrSimple", StaffTypes::TAB_7SIMPLE },
        { u"tab8StrSimple", StaffTypes::TAB_8SIMPLE },
        { u"tab9StrSimple", StaffTypes::TAB_9SIMPLE },
        { u"tab10StrSimple", StaffTypes::TAB_10SIMPLE },
    };

    return muse::value(xmlNameToType, m_xmlName, StaffTypes::STANDARD);
}

//---------------------------------------------------------
//   isSimpleTabStaff
//---------------------------------------------------------

bool StaffType::isSimpleTabStaff() const
{
    if (!isTabStaff()) {
        return false;
    }

    StaffTypes stType = type();

    switch (stType) {
    case StaffTypes::TAB_4SIMPLE:
    case StaffTypes::TAB_5SIMPLE:
    case StaffTypes::TAB_6SIMPLE:
    case StaffTypes::TAB_7SIMPLE:
    case StaffTypes::TAB_8SIMPLE:
    case StaffTypes::TAB_9SIMPLE:
    case StaffTypes::TAB_10SIMPLE:
    case StaffTypes::TAB_ITALIAN:
    case StaffTypes::TAB_FRENCH:
        return true;

    default:
        break;
    }

    return false;
}

//---------------------------------------------------------
//   isCommonTabStaff
//---------------------------------------------------------

bool StaffType::isCommonTabStaff() const
{
    return !isTabStaff() ? false : !isSimpleTabStaff();
}

//---------------------------------------------------------
//   isHiddenElementOnTab
//---------------------------------------------------------

bool StaffType::isHiddenElementOnTab(Sid commonTabStyle, Sid simpleTabStyle) const
{
    return (isCommonTabStaff() && !style().styleB(commonTabStyle)) || (isSimpleTabStaff() && !style().styleB(simpleTabStyle));
}

void StaffType::styleChanged()
{
    if (!m_fretUseTextStyle) {
        return;
    }
    setFretTextStyle(m_fretTextStyle);
}

//---------------------------------------------------------
//   setOnLines
//---------------------------------------------------------

void StaffType::setOnLines(bool val)
{
    m_onLines = val;
    setDurationMetrics();
    setFretMetrics();
}

void StaffType::setUseNumbers(bool val)
{
    m_useNumbers = val;
    setFretMetrics();
}

void StaffType::setFretTextStyle(const TextStyleType& val)
{
    m_fretTextStyle = val;
    m_fretFontInfo = TablatureFretFont();
    m_fretFont = Font();

    const TextStyle* ts = textStyle(m_fretTextStyle);

    for (const TextStyleProperty property : *ts) {
        switch (property.type) {
        case TextStylePropertyType::FontFace: {
            String fontName = style().styleSt(property.sid);
            m_fretFontInfo.family = fontName;
            m_fretFont.setFamily(fontName, Font::Type::Tablature);
        } break;
        case TextStylePropertyType::FontSize: {
            double fontSize = style().styleD(property.sid);
            setFretFontSize(fontSize);
        } break;
        case TextStylePropertyType::Offset: {
            PointF offset = style().styleV(property.sid).value<PointF>();
            setFretFontUserY(offset.y());
        } break;
        case TextStylePropertyType::FontStyle: {
            FontStyle fStyle = style().styleV(property.sid).value<FontStyle>();
            m_fretFont.setBold(fStyle & FontStyle::Bold);
            m_fretFont.setItalic(fStyle & FontStyle::Italic);
            m_fretFont.setUnderline(fStyle & FontStyle::Underline);
            m_fretFont.setStrike(fStyle & FontStyle::Strike);
        } break;
        default:
            continue;
        }
    }

    setFretMetrics();
}

void StaffType::setFretPresetIdx(size_t idx)
{
    // Clear all previous formatting
    m_fretFont = Font();
    if (idx >= m_fretFonts.size()) {
        m_fretPresetIdx = 0;
        m_fretFontInfo = m_fretFonts[0];
    } else {
        m_fretPresetIdx = idx;
        m_fretFontInfo = m_fretFonts[idx];
    }

    m_fretFont.setFamily(m_fretFontInfo.family, Font::Type::Tablature);
    setFretFontSize(m_fretFontInfo.defSize);
    setFretFontUserY(m_fretFontInfo.defYOffset);
    setFretMetrics();
}

void StaffType::setFretPreset(const String& name)
{
    String locName = name;
    // convert old names for two built-in fonts which have changed of name
    if (name == "MuseScore Tab Late Renaiss") {
        locName = u"MuseScore Phalèse";
    }
    size_t idx = 0;
    for (idx = 0; idx < m_fretFonts.size(); idx++) {
        if (m_fretFonts[idx].displayName == locName) {
            break;
        }
    }

    setFretPresetIdx(idx);
}

//---------------------------------------------------------
//   setDurationMetrics
//    checks whether the internally computed metrics are is still valid and re-computes them, if not
//---------------------------------------------------------

void StaffType::setDurationMetrics()
{
// FontMetrics returns results unreliably rounded to integral pixels;
// use a scaled up font and then scale computed values down
    Font font(durationFont());
    font.setPointSizeF(m_durationFontSize);
    FontMetrics fm(font);
    String txt(m_durationFonts[m_durationFontIdx].displayValue, size_t(TabVal::NUM_OF));
    RectF bb(fm.tightBoundingRect(txt));
    // raise symbols by a default margin and, if marks are above lines, by half the line distance
    // (converted from spatium units to raster units)
    m_durationGridYOffset = (TAB_DEFAULT_DUR_YOFFS - (m_onLines ? 0.0 : lineDistance().val() * 0.5)) * defaultSpatium();
    // this is the bottomest point of any duration sign
    m_durationYOffset = m_durationGridYOffset;
    // move symbols so that the lowest margin 'sits' on the base line:
    // move down by the whole part above (negative) the base line
    // ( -bb.y() ) then up by the whole height ( -bb.height() )
    m_durationYOffset        -= (bb.height() + bb.y()) / 100.0;
    m_durationBoxH           = bb.height() / 100.0;
    m_durationBoxY           = m_durationGridYOffset - bb.height() / 100.0;
}

void StaffType::setFretMetrics()
{
    FontMetrics fm(fretFont());
    RectF bb;
    // compute vertical displacement
    if (m_useNumbers) {
        // compute total height of used characters
        String txt;
        for (int idx = 0; idx < 10; idx++) {    // use only first 10 digits
            txt.append(m_fretFontInfo.displayDigit[idx]);
        }
        bb = fm.tightBoundingRect(txt);
        // for numbers: centre on '0': move down by the whole part above (negative)
        // the base line ( -bb.y() ) then up by half the whole height ( -bb.height()/2 )
        RectF bx(fm.tightBoundingRect(m_fretFontInfo.displayDigit[0]));
        m_fretYOffset = -(bx.y() + bx.height() / 2.0);
        // _fretYOffset = -(bb.y() + bb.height()/2.0);  // <- using bbox of all chars
    } else {
        // compute total height of used characters
        const String txt(m_fretFontInfo.displayLetter.data(), NUM_OF_LETTERFRETS);
        bb = fm.tightBoundingRect(txt);
        // for letters: centre on the 'a' ascender, by moving down half of the part above the base line in bx
        RectF bx(fm.tightBoundingRect(m_fretFontInfo.displayLetter[0]));
        m_fretYOffset = -bx.y() / 2.0;
    }

    // Calculate position for dead fret marks - these must be centred separately based on their glyph
    RectF deadBb = fm.tightBoundingRect(m_fretFontInfo.xChar);
    double lineThickness = style().styleS(Sid::staffLineWidth).val() * defaultSpatium() * 0.5;
    m_deadFretYOffset = -deadBb.y() / 2.0 + lineThickness;

    // if on string, we are done; if between strings, raise by half line distance
    if (!m_onLines) {
        double lineAdj = lineDistance().val() * defaultSpatium() * 0.5;
        m_fretYOffset -= lineAdj;
        m_deadFretYOffset -= lineAdj;
    }

    // from _fretYOffset, compute _fretBoxH and _fretBoxY
    m_fretBoxH = bb.height();
    m_fretBoxY = bb.y() + m_fretYOffset;

    m_deadFretBoxH = deadBb.height();
    m_deadFretBoxY = deadBb.y() + m_deadFretYOffset;
}

//---------------------------------------------------------
//   setDurationFontName
//---------------------------------------------------------

void StaffType::setDurationFontName(const String& name)
{
    size_t idx;
    for (idx = 0; idx < m_durationFonts.size(); idx++) {
        if (m_durationFonts[idx].displayName == name) {
            break;
        }
    }
    if (idx >= m_durationFonts.size()) {
        idx = 0;              // if name not found, use first font
    }
    m_durationFont.setFamily(m_durationFonts[idx].family, Font::Type::Tablature);
    m_durationFontIdx = idx;
    setDurationMetrics();
}

//---------------------------------------------------------
//   durationBoxH / durationBoxY
//---------------------------------------------------------

double StaffType::defaultSpatium() const
{
    return StyleDef::styleValues[static_cast<size_t>(Sid::spatium)].defaultValue.toDouble();
}

double StaffType::durationBoxH() const
{
    if (!m_genDurations && !m_stemless) {
        return 0.0;
    }
    return m_durationBoxH;
}

double StaffType::durationBoxY() const
{
    if (!m_genDurations && !m_stemless) {
        return 0.0;
    }
    return m_durationBoxY + m_durationFontUserY * defaultSpatium();
}

double StaffType::durationFontYOffset() const
{
    return m_durationYOffset + m_durationFontUserY * defaultSpatium();
}

double StaffType::fretBoxY() const
{
    return m_fretBoxY + m_fretFontUserY * defaultSpatium();
}

double StaffType::deadFretBoxY() const
{
    return m_deadFretBoxY + m_fretFontUserY * defaultSpatium();
}

double StaffType::fretMaskH() const
{
    return m_lineDistance.val() * defaultSpatium();
}

double StaffType::fretMaskY() const
{
    return (m_onLines ? -0.5 : -1.0) * m_lineDistance.val() * defaultSpatium();
}

double StaffType::fretFontYOffset() const
{
    return m_fretYOffset + m_fretFontUserY * defaultSpatium();
}

//---------------------------------------------------------
//   setDurationFontSize / setFretFontSize
//---------------------------------------------------------

void StaffType::setDurationFontSize(double val)
{
    m_durationFontSize = val;
    m_durationFont.setPointSizeF(val);
    setDurationMetrics();
}

void StaffType::setFretFontSize(double val)
{
    m_fretFontSize = val;
    m_fretFont.setPointSizeF(val);
    setFretMetrics();
}

//---------------------------------------------------------
//   fretString / durationString
//
//    construct the text string for a given fret / duration
//---------------------------------------------------------

static const String unknownFret = String(u"?");

String StaffType::fretString(int fret, int string, bool deadNote) const
{
    if (fret == INVALID_FRET_INDEX) {
        return unknownFret;
    }
    if (deadNote) {
        return String(m_fretFontInfo.xChar);
    } else {
        bool hasFret;
        String text  = tabBassStringPrefix(string, &hasFret);
        if (!hasFret) {             // if the notation does not allow to fret this string,
            return text;            // return the prefix only
        }
        // otherwise, add to prefix the relevant digit/letter string
        return text
               + (m_useNumbers
                  ? (fret >= NUM_OF_DIGITFRETS ? unknownFret : m_fretFontInfo.displayDigit[fret])
                  : (fret >= NUM_OF_LETTERFRETS ? unknownFret : m_fretFontInfo.displayLetter[fret]));
    }
}

String StaffType::durationString(DurationType type, int dots) const
{
    String s = m_durationFonts[m_durationFontIdx].displayValue[size_t(type)];
    for (int count = 0; count < dots; count++) {
        s.append(m_durationFonts[m_durationFontIdx].displayDot);
    }
    return s;
}

//---------------------------------------------------------
//    tabBassStringPrefix
//
//    returns a String (possibly empty) with the prefix identifying a bass string in TAB's;
//    can deal with non-bass strings (i.e. regular TAB lines).
//
//    Implements the specifics of historic notations for bass lines (i.e. strings outside
//    the lines of the tab), both Italian and French.
//
//    strg   the instrument physical string ordinal (0 = topmost string, may exceed the number
//                of lines actually present in the TAB to reference a bass string)
//    bool   pntr to a bool receiving the info if notation allows to express a fret number or not
//                (this is potentially different from the fact that the instrument string itself can be fretted or not)
//---------------------------------------------------------

String StaffType::tabBassStringPrefix(int strg, bool* hasFret) const
{
    *hasFret    = true;             // assume notation allows to fret this string
    int bassStrgIdx  = (strg >= m_lines ? strg - m_lines + 1 : 0);
    if (m_useNumbers) {
        // if above the max bass string which can be fretted with number notation
        // return a number with the string index
        if (bassStrgIdx > NUM_OF_BASSSTRINGS_WITH_NUMBER) {
            *hasFret    = false;
            return m_fretFontInfo.displayDigit[strg + 1];
        }
        // if a frettable bass string, return an empty string
        return String();
    } else {
        // bass string notation
        // if above the max bass string which can be fretted with letter notation
        // return a number with the bass string index itself
        if (bassStrgIdx > NUM_OF_BASSSTRINGS_WITH_LETTER) {
            *hasFret    = false;
            return m_fretFontInfo.displayDigit[bassStrgIdx - 1];
        }
        // if a frettable bass string, return a character with the relevant num. of slashes;
        // note that the number of slashes is bassStrgIdx-1 (1st bass has no slash)
        // and slashChar[] is 0-based (slashChar[0] => 1 slash, ...), whence the -2
        String prefix    = bassStrgIdx > 1
                           ? String(m_fretFontInfo.slashChar[bassStrgIdx - 2]) : String();
        return prefix;
    }
}

//---------------------------------------------------------
//   numOfLedgerLines
//
//    in TAB's, returns the number of ledgerlines needed by bass lines in some TAB styles.
//
//    Returns 0 if staff is not a TAB, if a TAB but style does not use ledger lines
//    or ledger lines do not apply to the given string.
//---------------------------------------------------------

int StaffType::numOfTabLedgerLines(int string) const
{
    if (m_group != StaffGroup::TAB || !m_useNumbers) {
        return 0;
    }

    int numOfLedgers= string < 0 ? -string : string - m_lines + 1;
    return numOfLedgers >= 1 && numOfLedgers <= NUM_OF_BASSSTRINGS_WITH_NUMBER ? numOfLedgers : 0;
}

//---------------------------------------------------------
//   physStringToVisual / visualStringToPhys
//
//    returns the string ordinal in visual order (top to down) from a string ordinal in physical order
//    or vice-versa: manages upsideDown
//---------------------------------------------------------

int StaffType::physStringToVisual(int strg) const
{
    if (strg < 0) {                       // if above top string, return top string
        strg = 0;
    }
//      // NO: bass strings may exist, which are in addition to tab string lines
//      if (strg >= _lines)                 // if physical string has no visual representation,
//            strg = _lines - 1;            // reduce to nearest visual line
    // if TAB upside down, flip around top line
    return m_upsideDown ? m_lines - 1 - strg : strg;
}

int StaffType::visualStringToPhys(int line) const
{
    // if TAB upside down, reverse string number
    line = (m_upsideDown ? m_lines - 1 - line : line);

    if (line < 0) {           // if above top string, reduce to top string
        line = 0;
    }
// NO: bass strings may exist, which are in addition to tab string lines
//      if (line >= _lines)
//            line = _lines - 1;
    return line;
}

//---------------------------------------------------------
//   physStringToYOffset
//
//    returns the string Y offset from a string ordinal in physical order:
//    manages upsideDown and extra bass strings.
//
//    The returned values is in sp. and is relative to the staff top line.
//
//    Note: the difference with physStringToVisual() is that this function takes into account
//          peculiarities of bass string notations.
//---------------------------------------------------------

double StaffType::physStringToYOffset(int strg) const
{
    double yOffset = strg;                       // the y offset of the visual string, as a multiple of line distance
    if (yOffset < 0) {                          // if above top physical string, limit to top string
        yOffset = 0;
    }
    if (yOffset >= m_lines) {                    // if physical string 'below' tab lines,
        yOffset = m_lines;                       // reduce to first string 'below' tab body
        if (!m_useNumbers) {                     // with letters, add some space for the slashes ascender
            yOffset = m_onLines ? m_lines : m_lines + STAFFTYPE_TAB_BASSSLASH_YOFFSET;
        }
    }
    // if TAB upside down, flip around top line
    yOffset = m_upsideDown ? (double)(m_lines - 1) - yOffset : yOffset;
    return yOffset * m_lineDistance.val();
}

//---------------------------------------------------------
//   setJimsStateJson
//    JiMStaffStateV2 (Milestone 2): the tonic-ambit token is a
//    first-class field of the Kernel-owned state JSON. When the state
//    carries it, extract it into the token member so layout consumes
//    one value regardless of source. M10 accepts exactly one state spelling;
//    aliases would create a duplicate musical authority.
//---------------------------------------------------------

void StaffType::setMeloStateJson(const String& s)
{
    m_meloStateJson = s;
    m_meloTonicAmbit.clear();
    static const String marker = u"\"tonic_ambit\":\"";
    size_t at = s.indexOf(marker);
    if (at != muse::nidx) {
        size_t from = at + marker.size();
        size_t end = s.indexOf(u'"', from);
        if (end != muse::nidx) {
            m_meloTonicAmbit = s.mid(from, end - from);
        }
    }
}

//---------------------------------------------------------
//   jimsResolvedScaleDotLabelMode
//    Auto resolves against the Kernel's label-legibility range with
//    STRICT comparisons and no epsilon (owner ruling 2026-08-15):
//    Left strictly inside, Split at or outside either exact boundary.
//    Explicit modes pass through. A bridge failure keeps labels off.
//---------------------------------------------------------

MeloScaleDotLabelMode StaffType::meloResolvedScaleDotLabelMode() const
{
    if (m_meloScaleDotLabelMode != MeloScaleDotLabelMode::Auto) {
        return m_meloScaleDotLabelMode;
    }
    double generatorCents = 0.0;
    double periodCents = 0.0;
    double minCents = 0.0;
    double maxCents = 0.0;
    if (!melo::staffMetrics(m_meloStateJson, generatorCents, periodCents)
        || !melo::labelLegibilityRange(minCents, maxCents)) {
        return MeloScaleDotLabelMode::None;
    }
    return (generatorCents > minCents && generatorCents < maxCents)
           ? MeloScaleDotLabelMode::Left
           : MeloScaleDotLabelMode::Split;
}

//---------------------------------------------------------
//   jimsHeaderGeometry
//    The single shared header calculation: crescent, indicator, and
//    label-band widths for the current state and RESOLVED label mode.
//    Label bands measure the actual Kernel-supplied label strings in
//    the label font (Edwin, 9pt at default spatium, scaled), plus the
//    0.25sp text-to-dot gap. Every consumer (StaffLines layout, draw,
//    system margin) reads this — no independent formulas.
//---------------------------------------------------------

StaffType::MeloHeaderGeometry StaffType::meloHeaderGeometry(double spatium, double defaultSpatium,
                                                            const MeloFrameView* view) const
{
    MeloHeaderGeometry g;
    const double dist = m_lineDistance.val() * spatium;
    const double periodH = (meloPeriodCents() / MELO_CENTS_PER_LINE_DISTANCE) * dist;
    g.clefRx = (periodH / 2.0) * 4.0 / 3.0;
    g.indicatorW = 1.3 * dist;
    g.headerWidth = 0.3 * spatium + g.clefRx + 2.0 * g.indicatorW;

    // Change-indicator terrain (M5): the label band is measured over the
    // state's Kernel labels regardless of the header's label mode (the
    // terrain always labels its dots and indicators, left of the glyphs);
    // arrow lane right of the dot column; closing stroke gap.
    Font labelFont(u"Edwin", Font::Type::Text);
    labelFont.setPointSizeF(9.0 * spatium / defaultSpatium);
    FontMetrics fm(labelFont);
    const IEngravingFontPtr engravingFont = m_score ? m_score->engravingFont() : nullptr;
    const double gap = 0.25 * spatium;
    std::vector<melo::LabeledDotStack> stacks;
    const bool haveLabels = melo::scaleDotLabels(m_meloStateJson, stacks);
    // Only Do's tonic pitch label nests inside the crescent; other tonic
    // labels reserve space to the left of their scale dot.
    melo::TonicPitchLabel key;
    const bool haveKey = melo::tonicPitchLabel(m_meloStateJson, key);
    const bool tonicIsDo = haveKey && std::any_of(stacks.begin(), stacks.end(), [&](const auto& stack) {
        return std::any_of(stack.members.begin(), stack.members.end(), [&](const auto& member) {
            return member.nGen == key.nGen && member.label == u"Do";
        });
    });
    double keyAdvance = haveKey
                        ? melo::pitchLabelLayout(key.label + u": ", labelFont, engravingFont).advance : 0.0;
    if (view && keyAdvance > 0.0) {
        // Milestone 8: reserve for the widest band label of this system (the
        // whole-piece view is one band carrying its own row's label).
        for (const MeloFrameBand& band : view->bands) {
            if (!band.tonicLabel.isEmpty()) {
                keyAdvance = std::max(keyAdvance,
                                      melo::pitchLabelLayout(band.tonicLabel + u": ", labelFont, engravingFont).advance);
            }
        }
    }
    g.keyLabelAdvance = keyAdvance;
    if (haveLabels) {
        // ALL labels sit LEFT of the dots (owner ruling 2026-08-16, second
        // round: the two note-stacks must look as alike as possible — the
        // interval pattern is the same, so the label-collision pattern
        // should be the same too; moving Grey labels right emphasised
        // difference). The right band stays zero.
        double widest = 0.0;
        for (const melo::LabeledDotStack& stack : stacks) {
            for (const melo::LabeledDotMember& member : stack.members) {
                widest = std::max(widest, fm.horizontalAdvance(member.label));
            }
        }
        g.changeLabelBand = widest > 0.0 ? widest + keyAdvance + gap : 0.0;
        g.changeRightLabelBand = 0.0;
    }
    g.changeArrowLane = 1.2 * g.indicatorW;
    g.changeTerrainWidth = 0.3 * spatium + g.changeLabelBand + 2.0 * g.indicatorW
                           + g.changeRightLabelBand + g.changeArrowLane + 0.3 * spatium;

    // Milestone 8, owner ruling 3b: a banded (hollow) stack is joined by a
    // brace at the system head, exactly as MuseScore joins the staves of one
    // keyboard instrument (Bracket, BracketType::BRACE): the SMuFL brace
    // glyph, x-magnified by MuseScore's span rule and stretched to the
    // stack's height, plus akkoladeBarDistance before the header. Reserve it.
    if (view && view->bands.size() > 1 && m_score) {
        const int span = int(view->bands.size());
        g.braceMagX = span + ((span - 1) * 1.625);   // Bracket::setStaffSpan's rule
        if (const IEngravingFontPtr font = m_score->engravingFont()) {
            // Font magnification as EngravingItem::magS: spatium / default spatium.
            const double glyphW = font->width(SymId::brace, spatium / defaultSpatium);
            g.braceWidth = glyphW * g.braceMagX + m_score->style().styleMM(Sid::akkoladeBarDistance);
        }
    }

    const MeloScaleDotLabelMode mode = meloResolvedScaleDotLabelMode();
    if (mode == MeloScaleDotLabelMode::None || !haveLabels) {
        g.leftLabelBand = haveKey && !tonicIsDo ? keyAdvance + gap : 0.0;
        g.headerWidth += g.leftLabelBand + g.braceWidth;
        return g;
    }
    double maxLeft = 0.0;
    double maxRight = 0.0;
    for (const melo::LabeledDotStack& stack : stacks) {
        String leftText;
        String rightText;
        for (const melo::LabeledDotMember& member : stack.members) {
            const bool leftSide = (mode == MeloScaleDotLabelMode::Left) || member.nGen <= 0;
            String& side = leftSide ? leftText : rightText;
            if (!side.isEmpty()) {
                side += u" ";
            }
            side += member.label;
        }
        double leftWidth = fm.horizontalAdvance(leftText);
        if (haveKey && !tonicIsDo && std::any_of(stack.members.begin(), stack.members.end(), [&](const auto& member) {
            return member.nGen == key.nGen;
        })) {
            leftWidth += keyAdvance;
        }
        maxLeft = std::max(maxLeft, leftWidth);
        if (!rightText.isEmpty()) {
            maxRight = std::max(maxRight, fm.horizontalAdvance(rightText));
        }
    }
    if (maxLeft > 0.0) {
        g.leftLabelBand = maxLeft + gap;
    }
    if (maxRight > 0.0 && mode == MeloScaleDotLabelMode::Split) {
        g.rightLabelBand = maxRight + gap;
    }
    g.headerWidth += g.leftLabelBand + g.rightLabelBand + g.braceWidth;
    return g;
}

//---------------------------------------------------------
//   jimsEnsureFrame
//    Derive (or reuse) the Kernel staff frame for this staff's melody.
//    Pure transport: the notes' lattice identities are collected in
//    document order and handed to the Kernel with the declared
//    tonic-ambit token; no musical fact is computed here. Keyed by
//    state+token+melody, so repeated calls are cheap. Every consumer of
//    jimsFrameTopCents/jimsFrameSegments (staff lines, note placement,
//    note entry, drawing) calls this first — note layout can run before
//    the staff lines' layout, so laziness here is what keeps the notes
//    and the frame on the same map (M2 owner correction 2026-08-14:
//    single-system scores rendered notes against the degenerate frame).
//---------------------------------------------------------

static std::vector<const StaffType*> meloSectionTypes(const Score* score, staff_idx_t staffIdx,
                                                      const System* system = nullptr)
{
    std::vector<const StaffType*> types;
    const Staff* staff = score ? score->staff(staffIdx) : nullptr;
    if (!staff) {
        return types;
    }
    auto add = [&](const StaffType* type) {
        if (type && type->isMelo() && std::find(types.begin(), types.end(), type) == types.end()) {
            types.push_back(type);
        }
    };
    for (const Measure* measure = score->firstMeasure(); measure; measure = measure->nextMeasure()) {
        if (system && measure->system() != system) {
            continue;
        }
        add(staff->staffType(measure->tick()));
        for (const StaffTypeChange* change : melo::changeCarriers(measure, staffIdx)) {
            add(staff->staffType(change->tick()));
        }
    }
    return types;
}

void StaffType::meloEnsureFrame(const Score* score, staff_idx_t staffIdx) const
{
    if (!isMelo() || !score || (m_meloFrameFrozen && !m_meloFrameSegments.empty())) {
        return;
    }
    const auto types = meloSectionTypes(score, staffIdx);
    if (types.size() <= 1) {
        meloEnsureSectionFrame(score, staffIdx);
        setMeloFrame(m_meloSectionFrameKey, m_meloSectionFrameSegments);
        return;
    }
    String key = u"shared:";
    std::vector<melo::FrameAlignmentSection> sections;
    for (const StaffType* type : types) {
        if (type->meloFrameFrozen() && !m_meloFrameSegments.empty()) {
            return;
        }
        type->meloEnsureSectionFrame(score, staffIdx);
        key += type->m_meloSectionFrameKey + u"|";
        melo::FrameAlignmentSection section;
        section.stateJson = type->meloStateJson();
        for (const MeloSegment& segment : type->m_meloSectionFrameSegments) {
            section.segments.push_back({ segment.lowerCents, segment.upperCents, segment.whole });
        }
        sections.push_back(section);
    }
    if (m_meloFrameKey == key) {
        return;
    }
    melo::FrameBands aligned;
    std::vector<MeloSegment> segments;
    if (melo::alignSectionFrames(meloStateJson(), sections, false, aligned)) {
        for (const auto& band : aligned.bands) {
            for (const auto& segment : band.segments) {
                segments.push_back({ segment.lowerCents, segment.upperCents, segment.whole });
            }
        }
    } else {
        LOGE() << melo::sectionFrameAlignmentFailed << staffIdx;
    }
    setMeloFrame(key, segments);
}

void StaffType::meloEnsureSectionFrame(const Score* score, staff_idx_t staffIdx) const
{
    if (!isMelo() || !score) {
        return;
    }
    // Mid-drag: keep the frame the drag started with (see
    // jimsSetFrameFrozen); the dragged note may draw past the frozen
    // edge as transient feedback, and the drop re-derives once.
    if (m_meloFrameFrozen && !m_meloFrameSegments.empty()) {
        return;
    }
    muse::String melody = u"{\"notes\":[";
    bool first = true;
    for (Segment* seg = score->firstSegment(SegmentType::ChordRest); seg;
         seg = seg->next1(SegmentType::ChordRest)) {
        if (score->staff(staffIdx)->staffType(seg->tick()) != this) {
            continue;
        }
        for (track_idx_t track = staffIdx * VOICES; track < (staffIdx + 1) * VOICES; ++track) {
            EngravingItem* el = seg->element(track);
            if (el && el->isChord()) {
                for (Note* note : toChord(el)->notes()) {
                    if (note->hasMeloPitch()) {
                        if (!first) {
                            melody += u",";
                        }
                        melody += muse::String(u"{\"nPer\":%1,\"nGen\":%2}")
                                  .arg(note->meloNPer()).arg(note->meloNGen());
                        first = false;
                    }
                }
            }
        }
    }
    melody += u"]}";
    const muse::String token = meloTonicAmbit();
    // Owner rule 2026-08-19 (7b): the change indicator drawn against this
    // staff type's frame (its own section start) is part of the derivation
    // input — when no Do-line keeps it on the staff, the frame is re-derived
    // covering it. Its old and new states are the key's extra ingredient.
    melo::ChangeIndicator intoThis;
    const bool hasIndicator = melo::changeIndicatorsTouchingStaffType(score, staffIdx, this, intoThis);
    muse::String indicatorKey;
    if (hasIndicator) {
        for (const melo::ChangePoint& p : intoThis.tonicIndicators) {
            indicatorKey += muse::String(u"t%1/%2;").arg(p.ordinate).arg(p.periodOffset);
        }
        for (const melo::ChangeArrow& a : intoThis.arrows) {
            indicatorKey += muse::String(u"a%1/%2>%3/%4;").arg(a.from.ordinate).arg(a.from.periodOffset)
                            .arg(a.to.ordinate).arg(a.to.periodOffset);
        }
    }
    const muse::String key = meloStateJson() + u"|" + token + u"|" + melody
                             + (m_meloExtentIsEmptyDefault ? u"|empty:1" : u"|empty:0")
                             + u"|ratio:" + m_meloRatioLineExtentJson + u"|ind:" + indicatorKey;
    if (m_meloSectionFrameKey != key) {
        // Milestone 4: EVERY melody — including the empty one — asks the
        // Kernel (empty defaults span half P8 around their centre).
        // A changed input never reuses a stale
        // successful frame: on failure the cache holds an empty frame
        // and a diagnostic is emitted; nothing is synthesized fork-side.
        std::vector<MeloSegment> cached;
        std::vector<MeloSegment> base;
        if (token.isEmpty()) {
            LOGE() << mu::engraving::melo::diagnostic::staffFrameMissingAmbit << staffIdx;
        } else {
            std::vector<melo::StaveSegment> segments;
            if (melo::frameForMelody(meloStateJson(), melody, token, segments, {}, m_meloRatioLineExtentJson,
                                     !m_meloExtentIsEmptyDefault)) {
                for (const melo::StaveSegment& segment : segments) {
                    cached.push_back({ segment.lowerCents, segment.upperCents, segment.whole });
                }
                base = cached;
                if (hasIndicator && meloPeriodCents() > 0.0) {
                    // Provisional one-band view -> overflow -> covering re-derivation.
                    MeloFrameView provisional;
                    MeloFrameBand band;
                    band.segments = cached;
                    if (!cached.empty()) {
                        band.lowerCents = cached.front().lowerCents;
                        band.upperCents = cached.back().upperCents;
                    }
                    provisional.bands.push_back(band);
                    melo::PeriodicOrigins origins;
                    const std::vector<double> extra = melo::periodicOrigins(meloStateJson(), origins)
                                                      ? melo::changeIndicatorOverflowCents(
                        provisional, intoThis, meloPeriodCents(), origins.doCentsAboveExtentLower)
                                                      : std::vector<double>();
                    if (!extra.empty()) {
                        std::vector<melo::StaveSegment> covering;
                        if (melo::frameForMelody(meloStateJson(), melody, token, covering, extra,
                                                 m_meloRatioLineExtentJson, !m_meloExtentIsEmptyDefault)) {
                            cached.clear();
                            for (const melo::StaveSegment& segment : covering) {
                                cached.push_back({ segment.lowerCents, segment.upperCents, segment.whole });
                            }
                        }
                    }
                }
            } else {
                LOGE() << mu::engraving::melo::diagnostic::staffFrameDerivationFailed << staffIdx
                       << " (state/melody rejected); frame cleared";
            }
        }
        m_meloSectionFrameKey = key;
        m_meloSectionFrameSegments = cached;
        m_meloSectionBaseFrameSegments = base;
        m_meloSectionMelodyJson = melody;
    }
}

//---------------------------------------------------------
//   jimsYFromCents
//    The single JiMStaff vertical seam (Milestone 1): cents above the
//    staff's lower Do boundary to a y offset in spatium units. One staff
//    spans exactly one 1200-cent period across (m_lines - 1) line
//    distances — 100 cents per staff location — and y grows downward,
//    so the upper Do boundary (1200 cents) maps to 0. Every JiMS note,
//    guide line, dot, and indicator ordinate routes through here; no
//    second cents-to-y formula may exist anywhere.
//---------------------------------------------------------

double StaffType::meloPeriodCents() const
{
    double generatorCents = 0.0;
    double periodCents = 0.0;
    if (!isMelo() || !melo::staffMetrics(meloStateJson(), generatorCents, periodCents)) {
        return 0.0;
    }
    return periodCents;
}

double StaffType::meloYFromCents(double centsAboveDo) const
{
    // Continuous cents axis (owner ruling 2026-08-14, Milestone 2): the
    // staff defines NO discrete locations; a note's height is its cents
    // value, affinely mapped to spatium space. JIMS_CENTS_PER_LINE_DISTANCE
    // is pure DRAWING density (how tall a cent draws), not a musical
    // fact; the frame's total height in cents is likewise drawing
    // geometry derived from the configured line count. Every musical
    // cents value entering this map comes from the Kernel. y grows
    // downward, so the frame top maps to zero.
    return (meloFrameTopCents() - centsAboveDo) / MELO_CENTS_PER_LINE_DISTANCE * m_lineDistance.val();
}

//---------------------------------------------------------
//   Milestone 8 — the explicit frame VIEW (octave-band elision)
//    A view is ordered bands with one style-derived gap between
//    neighbours. The legacy whole-piece frame is one band whose top sits
//    at 0, so every one-band coordinate is jimsYFromCents's, bit for bit
//    (same expression, same operation order). Which periods survive is
//    the Kernel's decision; this code only lays the Kernel's bands out
//    vertically and maps between y and cents.
//---------------------------------------------------------

double StaffType::MeloFrameView::heightLd() const
{
    if (bands.empty()) {
        return 0.0;
    }
    if (bands.size() == 1) {
        // Today's expression for the whole frame (kept verbatim so the
        // legacy path is unchanged bit for bit).
        return (bands.back().upperCents - bands.front().lowerCents) / MELO_CENTS_PER_LINE_DISTANCE;
    }
    // Bands are bottom to top; the top band is the last one and sits at
    // yTopLd 0; the bottom band's bottom edge is the total height.
    return bands.front().yTopLd + bands.front().heightLd();
}

const StaffType::MeloFrameBand* StaffType::MeloFrameView::bandForCents(double cents) const
{
    const double epsilon = 1e-6;
    for (const MeloFrameBand& band : bands) {
        if (cents >= band.lowerCents - epsilon && cents <= band.upperCents + epsilon) {
            return &band;
        }
    }
    return nullptr;
}

double StaffType::MeloFrameView::yLdFromCents(double cents) const
{
    if (bands.empty()) {
        return 0.0;
    }
    if (bands.size() == 1) {
        // The legacy affine map (top band at 0): identical to
        // jimsYFromCents's numerator, bit for bit.
        return (bands.back().upperCents - cents) / MELO_CENTS_PER_LINE_DISTANCE;
    }
    if (const MeloFrameBand* band = bandForCents(cents)) {
        return band->yTopLd + (band->upperCents - cents) / MELO_CENTS_PER_LINE_DISTANCE;
    }
    // Outside every band: above the top band or below the bottom band the
    // outer band's affine map extrapolates (today's edge behaviour); in a
    // gap, interpolate linearly between the two edges — drawing geometry
    // for ink that legitimately crosses a gap (an arrow shaft), never a
    // musical placement.
    const MeloFrameBand& top = bands.back();
    const MeloFrameBand& bottom = bands.front();
    if (cents > top.upperCents) {
        return top.yTopLd + (top.upperCents - cents) / MELO_CENTS_PER_LINE_DISTANCE;
    }
    if (cents < bottom.lowerCents) {
        return bottom.yTopLd + (bottom.upperCents - cents) / MELO_CENTS_PER_LINE_DISTANCE;
    }
    for (size_t i = 0; i + 1 < bands.size(); ++i) {
        const MeloFrameBand& lower = bands[i];
        const MeloFrameBand& upper = bands[i + 1];
        if (cents > lower.upperCents && cents < upper.lowerCents) {
            const double t = (cents - lower.upperCents) / (upper.lowerCents - lower.upperCents);
            const double yLowerEdge = lower.yTopLd;                       // lower band's top edge
            const double yUpperEdge = upper.yTopLd + upper.heightLd();    // upper band's bottom edge
            return yLowerEdge + (yUpperEdge - yLowerEdge) * t;
        }
    }
    return 0.0;
}

double StaffType::MeloFrameView::centsFromYLd(double yLd) const
{
    if (bands.empty()) {
        return 0.0;
    }
    if (bands.size() == 1) {
        return bands.back().upperCents - yLd * MELO_CENTS_PER_LINE_DISTANCE;
    }
    // Inside a band: that band's affine inverse. Above the top band or
    // below the bottom band: extrapolate from the outer band (today's
    // edge behaviour, so a drag past an edge keeps its cents delta).
    const MeloFrameBand& top = bands.back();
    const MeloFrameBand& bottom = bands.front();
    if (yLd <= top.yTopLd) {
        return top.upperCents - (yLd - top.yTopLd) * MELO_CENTS_PER_LINE_DISTANCE;
    }
    if (yLd >= bottom.yTopLd + bottom.heightLd()) {
        return bottom.upperCents - (yLd - bottom.yTopLd) * MELO_CENTS_PER_LINE_DISTANCE;
    }
    for (const MeloFrameBand& band : bands) {
        if (yLd >= band.yTopLd && yLd <= band.yTopLd + band.heightLd()) {
            return band.upperCents - (yLd - band.yTopLd) * MELO_CENTS_PER_LINE_DISTANCE;
        }
    }
    // In a gap: snap to the nearest band edge; an exact midpoint resolves
    // toward the LOWER-pitched band (the band below the gap).
    for (size_t i = 0; i + 1 < bands.size(); ++i) {
        const MeloFrameBand& lower = bands[i];        // lower-pitched, drawn below
        const MeloFrameBand& upper = bands[i + 1];    // higher-pitched, drawn above
        const double yUpperEdge = upper.yTopLd + upper.heightLd();   // gap top
        const double yLowerEdge = lower.yTopLd;                      // gap bottom
        if (yLd > yUpperEdge && yLd < yLowerEdge) {
            const double toUpper = yLd - yUpperEdge;
            const double toLower = yLowerEdge - yLd;
            return toUpper < toLower ? upper.lowerCents : lower.upperCents;
        }
    }
    return top.upperCents;
}

double StaffType::meloYFromCents(double centsAboveDo, const MeloFrameView& view) const
{
    return view.yLdFromCents(centsAboveDo) * m_lineDistance.val();
}

// The system-range melody collector: the same lattice-identity walk as
// jimsEnsureFrame's whole-piece collector (document order, every voice of
// the staff, main-chord notes with a MeloPresto identity), restricted to the
// measures of one system. Pure transport — no musical fact is computed.
static muse::String meloCollectSystemMelody(const System* system, staff_idx_t staffIdx, const StaffType* staffType)
{
    muse::String melody = u"{\"notes\":[";
    bool first = true;
    for (const MeasureBase* mb : system->measures()) {
        if (!mb->isMeasure()) {
            continue;
        }
        for (const Segment* seg = toMeasure(mb)->first(SegmentType::ChordRest); seg;
             seg = seg->next(SegmentType::ChordRest)) {
            if (system->score()->staff(staffIdx)->staffType(seg->tick()) != staffType) {
                continue;
            }
            for (track_idx_t track = staffIdx * VOICES; track < (staffIdx + 1) * VOICES; ++track) {
                EngravingItem* el = seg->element(track);
                if (el && el->isChord()) {
                    for (Note* note : toChord(el)->notes()) {
                        if (note->hasMeloPitch()) {
                            if (!first) {
                                melody += u",";
                            }
                            melody += muse::String(u"{\"nPer\":%1,\"nGen\":%2}")
                                      .arg(note->meloNPer()).arg(note->meloNGen());
                            first = false;
                        }
                    }
                }
            }
        }
    }
    melody += u"]}";
    return melody;
}

const StaffType::MeloFrameView& StaffType::meloWholeFrameView(const Score* score, staff_idx_t staffIdx) const
{
    static const MeloFrameView noView;
    if (!isMelo()) {
        return noView;
    }
    meloEnsureFrame(score, staffIdx);
    // The whole-piece view is the legacy cache as one band; its identity
    // is the legacy cache key, so it refreshes exactly when the frame does.
    MeloFrameView& view = m_meloFrameViews[u"whole"];
    if (view.key == m_meloFrameKey && (view.bands.empty() == m_meloFrameSegments.empty())) {
        return view;
    }
    view = MeloFrameView();
    view.key = m_meloFrameKey;
    view.banded = false;
    if (!m_meloFrameSegments.empty()) {
        MeloFrameBand band;
        band.segments = m_meloFrameSegments;
        band.lowerCents = m_meloFrameSegments.front().lowerCents;
        band.upperCents = m_meloFrameSegments.back().upperCents;
        // Ask the Kernel for the metadata of this already-derived frame.
        // A tuned tonic dot may sit just outside its fixed ratio boundary;
        // selecting an interior row here would incorrectly pick the next octave.
        melo::FrameBands metadata;
        if (melo::frameBandsForMelody(meloStateJson(), u"{\"notes\":[]}", meloTonicAmbit(), false, 1,
                                      metadata, { band.lowerCents, band.upperCents }, m_meloRatioLineExtentJson,
                                      !m_meloExtentIsEmptyDefault) && metadata.bands.size() == 1) {
            const auto& kernelBand = metadata.bands.front();
            band.lowestPeriodIndex = kernelBand.lowestPeriodIndex;
            band.highestPeriodIndex = kernelBand.highestPeriodIndex;
            band.labelPeriodIndex = kernelBand.labelPeriodIndex;
            band.tonicLabel = kernelBand.tonicLabel.label;
        }
        band.yTopLd = 0.0;
        view.bands.push_back(band);
    }
    return view;
}

//---------------------------------------------------------
//   jimsElisionActive
//    The EFFECTIVE octave-band elision policy for one system
//    (Milestone 8, MuseScore's hide-empty-staves shape): the score style
//    opts in (default off), the staff's Auto/On/Off override beats the
//    style in both directions, and by default the first system always
//    shows the whole stack. Presentation only. Phase 2 keeps every
//    switch unwired: the policy is forced off, so every system uses the
//    whole-piece legacy view and nothing on the page can change.
//---------------------------------------------------------

bool StaffType::meloElisionActive(const Score* score, staff_idx_t staffIdx, const System* system) const
{
    if (!isMelo() || !score || !system) {
        return false;
    }
    // The override is a PER-STAFF fact (MuseScore's per-staff hideWhenEmpty
    // shape): read from the staff's base staff type (tick 0), which is where
    // the MeloPresto Staff panel sets it; section copies made by a StaffTypeChange
    // do not carry their own policy.
    const Staff* staff = staffIdx < score->nstaves() ? score->staff(staffIdx) : nullptr;
    const StaffType* base = staff ? staff->staffType(Fraction(0, 1)) : this;
    bool on = false;
    switch ((base ? base : this)->m_meloElideOctaves) {
    case MeloElideOctaves::On: on = true;
        break;
    case MeloElideOctaves::Off: on = false;
        break;
    case MeloElideOctaves::Auto: on = score->style().styleB(Sid::meloElideEmptyOctaves);
        break;
    }
    if (!on) {
        return false;
    }
    if (score->style().styleB(Sid::meloShowAllOctavesInFirstSystem)) {
        // The first system of the score (or of a section — MuseScore's
        // dontHideStavesInFirstSystem treats a section's first system the
        // same way) always shows the whole stack.
        const Measure* fm = system->firstMeasure();
        if (fm) {
            const MeasureBase* prev = fm->prev();
            while (prev && !prev->isMeasure() && !prev->sectionBreak()) {
                prev = prev->prev();
            }
            if (!prev || prev->sectionBreak()) {
                return false;
            }
        }
    }
    return true;
}

const StaffType::MeloFrameView& StaffType::meloFrameView(const Score* score, staff_idx_t staffIdx,
                                                         const System* system) const
{
    if (!isMelo() || !score || !system) {
        return meloWholeFrameView(score, staffIdx);
    }
    if (!meloElisionActive(score, staffIdx, system)) {
        // Owner decision 2026-09-12 (1a): with octave elision off, a staff
        // with more than one state section shows, on each system, the union
        // of exactly the sections that system contains, Do-line anchored
        // within the system, never the whole piece's union. A staff with one
        // state keeps the whole-piece frame, bit for bit.
        if (meloSectionTypes(score, staffIdx).size() <= 1) {
            return meloWholeFrameView(score, staffIdx);
        }
        return meloSystemUnionFrameView(score, staffIdx, system);
    }
    const auto types = meloSectionTypes(score, staffIdx, system);
    if (types.size() <= 1) {
        return meloSectionFrameView(score, staffIdx, system);
    }
    const String rangeKey = String(u"aligned:%1-%2").arg(system->firstMeasure()->tick().ticks())
                            .arg(system->lastMeasure()->endTick().ticks());
    auto found = m_meloFrameViews.find(rangeKey);
    if (m_meloFrameFrozen && found != m_meloFrameViews.end()) {
        return found->second;
    }
    String key;
    std::vector<melo::FrameAlignmentSection> sections;
    for (const StaffType* type : types) {
        const MeloFrameView& local = type->meloSectionFrameView(score, staffIdx, system);
        key += local.key + u"|";
        melo::FrameAlignmentSection section;
        section.stateJson = type->meloStateJson();
        for (const auto& band : local.bands) {
            for (const auto& segment : band.segments) {
                section.segments.push_back({ segment.lowerCents, segment.upperCents, segment.whole });
            }
        }
        sections.push_back(section);
    }
    if (found != m_meloFrameViews.end() && found->second.key == key) {
        return found->second;
    }
    MeloFrameView view;
    view.key = key;
    view.banded = true;
    view.gapLd = m_lineDistance.val() > 0.0
                 ? score->style().styleS(Sid::staffDistance).val() / m_lineDistance.val() : 0.0;
    melo::FrameBands aligned;
    if (melo::alignSectionFrames(meloStateJson(), sections, true, aligned)) {
        for (const auto& source : aligned.bands) {
            MeloFrameBand band;
            for (const auto& segment : source.segments) {
                band.segments.push_back({ segment.lowerCents, segment.upperCents, segment.whole });
            }
            band.lowerCents = source.lowerCents;
            band.upperCents = source.upperCents;
            band.lowestPeriodIndex = source.lowestPeriodIndex;
            band.highestPeriodIndex = source.highestPeriodIndex;
            band.labelPeriodIndex = source.labelPeriodIndex;
            band.tonicLabel = source.tonicLabel.label;
            view.bands.push_back(band);
        }
        view.omittedPeriodCount = aligned.omittedPeriodCount;
        double y = 0.0;
        for (size_t i = view.bands.size(); i > 0; --i) {
            view.bands[i - 1].yTopLd = y;
            y += view.bands[i - 1].heightLd() + view.gapLd;
        }
    } else {
        LOGE() << melo::sectionBandAlignmentFailed << staffIdx;
    }
    return m_meloFrameViews[rangeKey] = view;
}

const StaffType::MeloFrameView& StaffType::meloSystemUnionFrameView(const Score* score, staff_idx_t staffIdx,
                                                                    const System* system) const
{
    const Measure* fm = system->firstMeasure();
    const Measure* lm = system->lastMeasure();
    if (!fm || !lm) {
        return meloWholeFrameView(score, staffIdx);
    }
    const String rangeKey = String(u"union:%1-%2").arg(fm->tick().ticks()).arg(lm->endTick().ticks());
    auto found = m_meloFrameViews.find(rangeKey);
    if (m_meloFrameFrozen && found != m_meloFrameViews.end()) {
        return found->second;
    }
    // The sections on this system, each with its indicator-free frame. A
    // change indicator extends only the frame of the system that draws it
    // (owner rule 7b applied per system): the indicators painted on this
    // system, each against its displayed state, are collected below.
    const Staff* staff = score->staff(staffIdx);
    const std::vector<const StaffType*> types = meloSectionTypes(score, staffIdx, system);
    String key = u"union:";
    std::vector<melo::FrameAlignmentSection> sections;
    std::vector<const StaffType*> sectionTypes;
    for (const StaffType* type : types) {
        type->meloEnsureSectionFrame(score, staffIdx);
        key += type->m_meloSectionFrameKey + u"|";
        melo::FrameAlignmentSection section;
        section.stateJson = type->meloStateJson();
        for (const MeloSegment& segment : type->m_meloSectionBaseFrameSegments) {
            section.segments.push_back({ segment.lowerCents, segment.upperCents, segment.whole });
        }
        if (!section.segments.empty()) {
            sections.push_back(section);
            sectionTypes.push_back(type);
        }
    }
    struct DrawnIndicator {
        melo::ChangeIndicator model;
        const StaffType* displayed = nullptr;
    };
    std::vector<DrawnIndicator> drawn;
    for (const Measure* measure = fm; measure; measure = measure->nextMeasure()) {
        melo::ChangeIndicator model;
        const StaffType* newType = nullptr;
        if (melo::midSystemChangeIndicator(measure, staffIdx, model, &newType) && newType) {
            drawn.push_back({ model, newType });
        }
        for (const StaffTypeChange* carrier : melo::changeCarriers(measure, staffIdx)) {
            if (carrier->rtick().isZero()) {
                continue;
            }
            const Fraction before = Fraction::fromTicks(std::max(0, carrier->tick().ticks() - 1));
            const StaffType* oldType = staff ? staff->staffType(before) : nullptr;
            melo::ChangeIndicator midBar;
            if (oldType && melo::midBarChangeIndicator(carrier, midBar, nullptr)) {
                drawn.push_back({ midBar, oldType });
            }
        }
        if (measure == lm) {
            break;
        }
    }
    {
        melo::ChangeIndicator courtesy;
        const StaffType* courtesyType = nullptr;
        if (melo::courtesyChangeIndicator(lm, staffIdx, courtesy, &courtesyType) && courtesyType) {
            drawn.push_back({ courtesy, courtesyType });
        }
    }
    for (const DrawnIndicator& d : drawn) {
        key += u"ind:" + d.displayed->meloStateJson() + u"@";
        for (const melo::ChangePoint& p : d.model.tonicIndicators) {
            key += muse::String(u"t%1/%2;").arg(p.ordinate).arg(p.periodOffset);
        }
        for (const melo::ChangeArrow& a : d.model.arrows) {
            key += muse::String(u"a%1/%2>%3/%4;").arg(a.from.ordinate).arg(a.from.periodOffset)
                   .arg(a.to.ordinate).arg(a.to.periodOffset);
        }
        key += u"|";
    }
    if (found != m_meloFrameViews.end() && found->second.key == key) {
        return found->second;
    }
    auto alignFor = [&](const StaffType* type, MeloFrameView& into) -> bool {
        melo::FrameBands aligned;
        if (sections.empty() || !melo::alignSectionFrames(type->meloStateJson(), sections, false, aligned)) {
            return false;
        }
        into.bands.clear();
        for (const auto& source : aligned.bands) {
            MeloFrameBand band;
            for (const auto& segment : source.segments) {
                band.segments.push_back({ segment.lowerCents, segment.upperCents, segment.whole });
            }
            band.lowerCents = source.lowerCents;
            band.upperCents = source.upperCents;
            band.lowestPeriodIndex = source.lowestPeriodIndex;
            band.highestPeriodIndex = source.highestPeriodIndex;
            band.labelPeriodIndex = source.labelPeriodIndex;
            band.tonicLabel = source.tonicLabel.label;
            into.bands.push_back(band);
        }
        into.omittedPeriodCount = aligned.omittedPeriodCount;
        double y = 0.0;
        for (size_t i = into.bands.size(); i > 0; --i) {
            into.bands[i - 1].yTopLd = y;
            y += into.bands[i - 1].heightLd() + into.gapLd;
        }
        return true;
    };
    // Cover each drawn indicator: its overflow against the union as seen by
    // its displayed state re-derives that state's section covering the
    // overflowing points (the Kernel's frame_for_melody with extra cents),
    // then the union is realigned. Extras accumulate per displayed state.
    std::map<const StaffType*, std::vector<double> > extras;
    for (const DrawnIndicator& d : drawn) {
        const StaffType* displayed = d.displayed;
        if (!displayed || !displayed->isMelo() || displayed->meloPeriodCents() <= 0.0) {
            continue;
        }
        MeloFrameView asDisplayed;
        if (!alignFor(displayed, asDisplayed) || asDisplayed.empty()) {
            continue;
        }
        melo::PeriodicOrigins origins;
        if (!melo::periodicOrigins(displayed->meloStateJson(), origins)) {
            continue;
        }
        const std::vector<double> extra = melo::changeIndicatorOverflowCents(
            asDisplayed, d.model, displayed->meloPeriodCents(), origins.doCentsAboveExtentLower);
        if (extra.empty()) {
            continue;
        }
        std::vector<double>& all = extras[displayed];
        all.insert(all.end(), extra.begin(), extra.end());
        std::vector<melo::StaveSegment> covering;
        if (!melo::frameForMelody(displayed->meloStateJson(), displayed->m_meloSectionMelodyJson,
                                  displayed->meloTonicAmbit(), covering, all,
                                  displayed->m_meloRatioLineExtentJson, !displayed->m_meloExtentIsEmptyDefault)) {
            continue;
        }
        melo::FrameAlignmentSection section;
        section.stateJson = displayed->meloStateJson();
        for (const melo::StaveSegment& segment : covering) {
            section.segments.push_back({ segment.lowerCents, segment.upperCents, segment.whole });
        }
        if (section.segments.empty()) {
            continue;
        }
        auto at = std::find(sectionTypes.begin(), sectionTypes.end(), displayed);
        if (at != sectionTypes.end()) {
            sections[size_t(at - sectionTypes.begin())] = section;
        } else {
            sections.push_back(section);
            sectionTypes.push_back(displayed);
        }
    }
    MeloFrameView view;
    view.key = key;
    view.banded = true;   // one band whose top is this system's, not the whole piece's
    view.gapLd = 0.0;
    if (!alignFor(this, view)) {
        LOGE() << melo::systemFrameUnionFailed << staffIdx;
    }
    // An empty union (no derivable section frame) is "no frame", exactly as
    // the whole-piece path reports it: never an empty banded view.
    view.banded = !view.bands.empty();
    return m_meloFrameViews[rangeKey] = view;
}

const StaffType::MeloFrameView& StaffType::meloSectionFrameView(const Score* score, staff_idx_t staffIdx,
                                                                const System* system) const
{
    static const MeloFrameView noView;
    if (!isMelo() || !score) {
        return noView;
    }
    if (!system || !meloElisionActive(score, staffIdx, system)) {
        return meloWholeFrameView(score, staffIdx);
    }
    // Per-system view: keyed by the system's tick range; the entry
    // remembers the full derivation key (state | token | melody slice |
    // options | policy) so a changed input never reuses a stale view,
    // and a frozen frame (mid-drag) never re-derives.
    const Measure* fm = system->firstMeasure();
    const Measure* lm = system->lastMeasure();
    if (!fm || !lm) {
        return meloWholeFrameView(score, staffIdx);
    }
    const muse::String rangeKey = muse::String(u"sys:%1-%2").arg(fm->tick().ticks()).arg(lm->endTick().ticks());
    auto found = m_meloFrameViews.find(rangeKey);
    if (m_meloFrameFrozen && found != m_meloFrameViews.end() && !found->second.bands.empty()) {
        return found->second;
    }
    const muse::String melody = meloCollectSystemMelody(system, staffIdx, this);
    const muse::String token = meloTonicAmbit();
    // Owner rule 2026-08-19 (7b): the change indicator drawn against this
    // staff type's frame is part of the derivation input (see the whole-
    // piece derivation); it only matters on the system that draws it.
    melo::ChangeIndicator intoThis;
    auto appendIndicator = [&](const melo::ChangeIndicator& model) {
        for (const muse::String& kind : model.kinds) {
            if (std::find(intoThis.kinds.begin(), intoThis.kinds.end(), kind) == intoThis.kinds.end()) {
                intoThis.kinds.push_back(kind);
            }
        }
        intoThis.dotStacks.insert(intoThis.dotStacks.end(), model.dotStacks.begin(), model.dotStacks.end());
        intoThis.tonicIndicators.insert(intoThis.tonicIndicators.end(), model.tonicIndicators.begin(),
                                        model.tonicIndicators.end());
        intoThis.arrows.insert(intoThis.arrows.end(), model.arrows.begin(), model.arrows.end());
    };
    const Staff* staff = score->staff(staffIdx);
    for (const Measure* measure = fm; measure; measure = measure->nextMeasure()) {
        melo::ChangeIndicator model;
        const StaffType* terrainStaffType = nullptr;
        if (melo::midSystemChangeIndicator(measure, staffIdx, model, &terrainStaffType)
            && terrainStaffType == this) {
            appendIndicator(model);
        }
        for (const StaffTypeChange* carrier : melo::changeCarriers(measure, staffIdx)) {
            if (carrier->rtick().isZero()) {
                continue;
            }
            const Fraction before = Fraction::fromTicks(std::max(0, carrier->tick().ticks() - 1));
            if (staff && staff->staffType(before) == this
                && melo::midBarChangeIndicator(carrier, model, nullptr)) {
                appendIndicator(model);
            }
        }
        if (measure == lm) {
            break;
        }
    }
    melo::ChangeIndicator courtesy;
    const StaffType* courtesyStaffType = nullptr;
    if (melo::courtesyChangeIndicator(lm, staffIdx, courtesy, &courtesyStaffType)
        && courtesyStaffType == this) {
        appendIndicator(courtesy);
    }
    const bool hasIndicator = !intoThis.empty();
    muse::String indicatorKey;
    if (hasIndicator) {
        for (const melo::ChangePoint& p : intoThis.tonicIndicators) {
            indicatorKey += muse::String(u"t%1/%2;").arg(p.ordinate).arg(p.periodOffset);
        }
        for (const melo::ChangeArrow& a : intoThis.arrows) {
            indicatorKey += muse::String(u"a%1/%2>%3/%4;").arg(a.from.ordinate).arg(a.from.periodOffset)
                            .arg(a.to.ordinate).arg(a.to.periodOffset);
        }
    }
    const muse::String key = meloStateJson() + u"|" + token + u"|" + melody
                             + (m_meloExtentIsEmptyDefault ? u"|empty:1" : u"|empty:0")
                             + u"|elide:1|min:1|ratio:" + m_meloRatioLineExtentJson + u"|ind:" + indicatorKey;
    if (found != m_meloFrameViews.end() && found->second.key == key) {
        return found->second;
    }
    MeloFrameView view;
    view.key = key;
    view.banded = true;
    // Gap between bands: the style's staff distance (staves of one part,
    // owner-approved plan §7.9), in line distances of this staff type.
    const double ld = m_lineDistance.val();
    view.gapLd = ld > 0.0 ? score->style().styleS(Sid::staffDistance).val() / ld : 0.0;
    auto deriveBands = [&](const std::vector<double>& extra, MeloFrameView& into) -> bool {
        melo::FrameBands bands;
        if (!melo::frameBandsForMelody(meloStateJson(), melody, token, true, 1, bands, extra,
                                       m_meloRatioLineExtentJson, !m_meloExtentIsEmptyDefault)) {
            return false;
        }
        into.bands.clear();
        for (const melo::FrameBand& kb : bands.bands) {
            MeloFrameBand band;
            for (const melo::StaveSegment& segment : kb.segments) {
                band.segments.push_back({ segment.lowerCents, segment.upperCents, segment.whole });
            }
            band.lowerCents = kb.lowerCents;
            band.upperCents = kb.upperCents;
            band.lowestPeriodIndex = kb.lowestPeriodIndex;
            band.highestPeriodIndex = kb.highestPeriodIndex;
            band.labelPeriodIndex = kb.labelPeriodIndex;
            band.tonicLabel = kb.tonicLabel.label;
            into.bands.push_back(band);
        }
        into.omittedPeriodCount = bands.omittedPeriodCount;
        // Vertical placement, top band at 0, then each lower band below the
        // previous one plus one gap.
        double y = 0.0;
        for (size_t i = into.bands.size(); i > 0; --i) {
            MeloFrameBand& band = into.bands[i - 1];
            band.yTopLd = y;
            y += band.heightLd() + into.gapLd;
        }
        return true;
    };
    if (token.isEmpty()) {
        LOGE() << mu::engraving::melo::diagnostic::staffBandsMissingAmbit << staffIdx;
    } else if (deriveBands({}, view)) {
        if (hasIndicator && meloPeriodCents() > 0.0) {
            melo::PeriodicOrigins origins;
            const std::vector<double> extra = melo::periodicOrigins(meloStateJson(), origins)
                                              ? melo::changeIndicatorOverflowCents(
                view, intoThis, meloPeriodCents(), origins.doCentsAboveExtentLower)
                                              : std::vector<double>();
            if (!extra.empty()) {
                deriveBands(extra, view);
            }
        }
    } else {
        LOGE() << mu::engraving::melo::diagnostic::staffBandsDerivationFailed << staffIdx
               << " (state/melody rejected); banded view cleared";
    }
    MeloFrameView& stored = m_meloFrameViews[rangeKey];
    stored = view;
    return stored;
}

//---------------------------------------------------------
//   TabDurationSymbol
//---------------------------------------------------------

TabDurationSymbol::TabDurationSymbol(ChordRest* parent)
    : EngravingItem(ElementType::TAB_DURATION_SYMBOL, parent, ElementFlag::NOT_SELECTABLE)
{
    setGenerated(true);
    m_tab        = 0;
    m_text       = String();
}

TabDurationSymbol::TabDurationSymbol(ChordRest* parent, const StaffType* tab, DurationType type, int dots)
    : EngravingItem(ElementType::TAB_DURATION_SYMBOL, parent, ElementFlag::NOT_SELECTABLE)
{
    setGenerated(true);
    setDuration(type, dots, tab);
}

TabDurationSymbol::TabDurationSymbol(const TabDurationSymbol& e)
    : EngravingItem(e)
{
    m_tab = e.m_tab;
    m_text = e.m_text;
}

//---------------------------------------------------------
//   STATIC FUNCTIONS FOR FONT CONFIGURATION MANAGEMENT
//---------------------------------------------------------

TablatureFretFont::TablatureFretFont()
{
    // Set up defaults
    for (size_t i = 0; i < NUM_OF_DIGITFRETS; i++) {
        displayDigit.at(i) = String::number(i);
    }

    for (size_t i = 0; i < NUM_OF_LETTERFRETS; i++) {
        displayLetter[i] = Char(97 + static_cast<char16_t>(i));
    }

    for (size_t i = 0; i < NUM_OF_BASSSTRING_SLASHES; i++) {
        for (size_t j = 0; j < i; j++) {
            slashChar.at(i).append(u"/");
        }
    }
}

bool TablatureFretFont::read(XmlReader& e, int mscVersion)
{
    defSize    = 9.0;
    defYOffset  = 0.0;
    while (e.readNextStartElement()) {
        const AsciiStringView tag(e.name());

        int val = e.intAttribute("value");

        if (tag == "family") {
            family = e.readText();
        } else if (tag == "displayName") {
            displayName = e.readText();
        } else if (tag == "defaultSize" || (tag == "defaultPitch" && mscVersion < 460)) {
            defSize = e.readDouble();
        } else if (tag == "defaultYOffset") {
            defYOffset = e.readDouble();
        } else if (tag == "mark") {
            String sval = e.attribute("value");
            int num  = e.intAttribute("number", 1);
            String txt(e.readText());
            if (sval.size() < 1) {
                return false;
            }
            if (sval == "x") {
                xChar = txt.at(0);
            } else if (sval == "slash") {
                // limit within legal range
                if (num < 1) {
                    num = 1;
                }
                if (num > NUM_OF_BASSSTRING_SLASHES) {
                    num = NUM_OF_BASSSTRING_SLASHES;
                }
                slashChar.at(num - 1) = txt;
            }
        } else if (tag == "fret") {
            bool bLetter = e.intAttribute("letter");
            String txt(e.readText());
            if (bLetter) {
                if (val >= 0 && val < NUM_OF_LETTERFRETS) {
                    displayLetter[val] = txt.at(0);
                }
            } else {
                if (val >= 0 && val < NUM_OF_DIGITFRETS) {
                    displayDigit.at(val) = txt;
                }
            }
        } else {
            e.unknown();
            return false;
        }
    }
    return true;
}

bool TablatureDurationFont::read(XmlReader& e, int mscVersion)
{
    while (e.readNextStartElement()) {
        const AsciiStringView tag(e.name());

        if (tag == "family") {
            family = e.readText();
        } else if (tag == "displayName") {
            displayName = e.readText();
        } else if (tag == "defaultSize" || (tag == "defaultPitch" && mscVersion < 460)) {
            defSize = e.readDouble();
        } else if (tag == "defaultYOffset") {
            defYOffset = e.readDouble();
        } else if (tag == "beamWidth") {
            gridBeamWidth = e.readDouble();
        } else if (tag == "stemHeight") {
            gridStemHeight = e.readDouble();
        } else if (tag == "stemWidth") {
            gridStemWidth = e.readDouble();
        } else if (tag == "zeroBeamValue") {
            String val(e.readText());
            if (val == "longa") {
                zeroBeamLevel = DurationType::V_LONG;
            } else if (val == "brevis") {
                zeroBeamLevel = DurationType::V_BREVE;
            } else if (val == "semibrevis") {
                zeroBeamLevel = DurationType::V_WHOLE;
            } else if (val == "minima") {
                zeroBeamLevel = DurationType::V_HALF;
            } else if (val == "semiminima") {
                zeroBeamLevel = DurationType::V_QUARTER;
            } else if (val == "fusa") {
                zeroBeamLevel = DurationType::V_EIGHTH;
            } else if (val == "semifusa") {
                zeroBeamLevel = DurationType::V_16TH;
            } else if (val == "32") {
                zeroBeamLevel = DurationType::V_32ND;
            } else if (val == "64") {
                zeroBeamLevel = DurationType::V_64TH;
            } else if (val == "128") {
                zeroBeamLevel = DurationType::V_128TH;
            } else if (val == "256") {
                zeroBeamLevel = DurationType::V_256TH;
            } else if (val == "512") {
                zeroBeamLevel = DurationType::V_512TH;
            } else if (val == "1024") {
                zeroBeamLevel = DurationType::V_1024TH;
            } else {
                e.unknown();
            }
        } else if (tag == "duration") {
            String val = e.attribute("value");
            String txt(e.readText());
            Char chr = txt.at(0);
            if (val == "longa") {
                displayValue[size_t(TabVal::VAL_LONGA)] = chr;
            } else if (val == "brevis") {
                displayValue[size_t(TabVal::VAL_BREVIS)] = chr;
            } else if (val == "semibrevis") {
                displayValue[size_t(TabVal::VAL_SEMIBREVIS)] = chr;
            } else if (val == "minima") {
                displayValue[size_t(TabVal::VAL_MINIMA)] = chr;
            } else if (val == "semiminima") {
                displayValue[size_t(TabVal::VAL_SEMIMINIMA)] = chr;
            } else if (val == "fusa") {
                displayValue[size_t(TabVal::VAL_FUSA)] = chr;
            } else if (val == "semifusa") {
                displayValue[size_t(TabVal::VAL_SEMIFUSA)] = chr;
            } else if (val == "32") {
                displayValue[size_t(TabVal::VAL_32)] = chr;
            } else if (val == "64") {
                displayValue[size_t(TabVal::VAL_64)] = chr;
            } else if (val == "128") {
                displayValue[size_t(TabVal::VAL_128)] = chr;
            } else if (val == "256") {
                displayValue[size_t(TabVal::VAL_256)] = chr;
            } else if (val == "512") {
                displayValue[size_t(TabVal::VAL_512)] = chr;
            } else if (val == "1024") {
                displayValue[size_t(TabVal::VAL_1024)] = chr;
            } else if (val == "dot") {
                displayDot = chr;
            } else {
                e.unknown();
            }
        } else {
            e.unknown();
            return false;
        }
    }
    return true;
}

//---------------------------------------------------------
//   Read Configuration File
//
//    reads a configuration and appends read data to g_TABFonts
//    resets everything and reads the built-in config file if fileName is null or empty
//---------------------------------------------------------

bool StaffType::readTabConfigFile(const String& fileName)
{
    muse::io::path_t path;

    if (fileName.isEmpty()) {         // defaults to built-in xml
        path = ":/fonts/fonts_tablature.xml";
        m_durationFonts.clear();
        m_fretFonts.clear();
    } else {
        path = fileName;
    }

    File f(path);
    if (!f.exists() || !f.open(IODevice::ReadOnly)) {
        LOGE() << "Cannot open tablature font description: " << f.filePath();
        return false;
    }

    XmlReader e(&f);
    while (e.readNextStartElement()) {
        if (e.name() == "museScore") {
            const String version = e.attribute("version");
            const StringList sl = version.split(u'.');
            const int mscVersion = sl.size() == 2 ? sl[0].toInt() * 100 + sl[1].toInt() : 0;

            while (e.readNextStartElement()) {
                const AsciiStringView tag(e.name());
                if (tag == "fretFont") {
                    TablatureFretFont ff;
                    if (ff.read(e, mscVersion)) {
                        m_fretFonts.push_back(ff);
                    } else {
                        continue;
                    }
                } else if (tag == "durationFont") {
                    TablatureDurationFont df;
                    if (df.read(e, mscVersion)) {
                        m_durationFonts.push_back(df);
                    } else {
                        continue;
                    }
                } else {
                    e.unknown();
                }
            }
            return true;
        }
    }
    return false;
}

//---------------------------------------------------------
//   fontNames
//
//    returns a list of display names for the fonts  configured to work with Tablatures;
//    the index of a name in the list can be used to retrieve the font data with fontData()
//---------------------------------------------------------

std::vector<String> StaffType::tabFontNames(bool bDuration)
{
    std::vector<String> names;
    if (bDuration) {
        for (const TablatureDurationFont& f : m_durationFonts) {
            names.push_back(f.displayName);
        }
    } else {
        for (const TablatureFretFont& f : m_fretFonts) {
            names.push_back(f.displayName);
        }
    }
    return names;
}

//---------------------------------------------------------
//   fontData
//
//    retrieves data about a Tablature font.
//    returns: true if idx is valid | false if it is not
//---------------------------------------------------------

bool StaffType::tabFontData(bool bDuration, size_t nIdx, double& pSize, double& pYOff)
{
    if (bDuration) {
        if (nIdx < m_durationFonts.size()) {
            TablatureDurationFont f = m_durationFonts.at(nIdx);
            pSize = f.defSize;
            pYOff = f.defYOffset;
            return true;
        }
    } else {
        TablatureFretFont f = nIdx < m_fretFonts.size() ? m_fretFonts.at(nIdx) : TablatureFretFont();
        pSize = f.defSize;
        pYOff = f.defYOffset;
        return true;
    }
    return false;
}

const MStyle& StaffType::style() const
{
    if (!m_score) {
        return DefaultStyle::defaultStyle();
    }

    return m_score->style();
}

//---------------------------------------------------------
//   spatium
//---------------------------------------------------------

double StaffType::spatium() const
{
    return style().spatium() * (isSmall() ? style().styleD(Sid::smallStaffMag) : 1.0) * userMag();
}

//=========================================================
//
//   BUILT-IN STAFF TYPES and STAFF TYPE PRESETS
//
//=========================================================

static const int _defaultPreset[STAFF_GROUP_MAX] =
{ 0,                    // default pitched preset is "stdNormal"
  4,                    // default percussion preset is "perc5lines"
  5                     // default tab preset is "tab6StrCommon"
};

//---------------------------------------------------------
//   Static functions for StaffType presets
//---------------------------------------------------------

const StaffType* StaffType::preset(StaffTypes idx)
{
    if (int(idx) < 0 || int(idx) >= int(m_presets.size())) {
        return &m_presets[0];
    }

    return &m_presets[int(idx)];
}

const StaffType* StaffType::presetFromXmlName(const String& xmlName)
{
    for (size_t i = 0; i < m_presets.size(); ++i) {
        if (m_presets[i].xmlName() == xmlName) {
            return &m_presets[i];
        }
    }

    return nullptr;
}

const StaffType* StaffType::getDefaultPreset(StaffGroup grp)
{
    int _idx = _defaultPreset[int(grp)];
    return &m_presets[_idx];
}

//---------------------------------------------------------
//   initStaffTypes
//---------------------------------------------------------

std::vector<StaffType> StaffType::m_presets;
/* *INDENT-OFF* */
void StaffType::initStaffTypes(const Color& defaultColor)
{
    readTabConfigFile(String());            // get TAB font config, before initStaffTypes()

    // keep in sync with enum class StaffTypes
    m_presets = {
//                       group,              xml-name,  human-readable-name,          lin stpOff  dist clef   bars stmless time  key    ledger invis     color
        StaffType(StaffGroup::STANDARD,   u"stdNormal", muse::mtrc("engraving", "Standard"),        5, 0,     1,   true,  true, false, true, true, true, false,  defaultColor),
        StaffType(StaffGroup::PERCUSSION, u"perc1Line", muse::mtrc("engraving", "Perc. 1 line"),    1, 0,     1,   true,  true, false, true, false, true, false,  defaultColor),
        StaffType(StaffGroup::PERCUSSION, u"perc2Line", muse::mtrc("engraving", "Perc. 2 lines"),   2, 0,     1,   true,  true, false, true, false, true, false,  defaultColor),
        StaffType(StaffGroup::PERCUSSION, u"perc3Line", muse::mtrc("engraving", "Perc. 3 lines"),   3, 0,     1,   true,  true, false, true, false, true, false,  defaultColor),
        StaffType(StaffGroup::PERCUSSION, u"perc5Line", muse::mtrc("engraving", "Perc. 5 lines"),   5, 0,     1,   true,  true, false, true, false, true, false,  defaultColor),

//                 group            xml-name,         human-readable-name                         lin stpOff dist clef   bars stemless time  invis     color       duration font         size off genDur textStyle fret font          size off  duration symbol repeat       thru    minim style                  onLin  rests  stmDn  stmThr upsDn  sTFing nums  bkTied
        StaffType(StaffGroup::TAB, u"tab6StrSimple",  muse::mtrc("engraving", "Tab. 6-str. simple"),  6,  0, 1.5, true,  true, true,  false, false, defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::NONE,    true,  false, true,  false, false, false, true,  false),
        StaffType(StaffGroup::TAB, u"tab6StrCommon",  muse::mtrc("engraving", "Tab. 6-str. common"),  6,  0, 1.5, true,  true, false, true, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::SHORTER, true,  true,  true,  false, false, true,  true,  true),
        StaffType(StaffGroup::TAB, u"tab6StrFull",    muse::mtrc("engraving", "Tab. 6-str. full"),    6,  0, 1.5, true,  true, false, true, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::SLASHED, true,  true,  true,  true,  false, true,  true,  true),
        StaffType(StaffGroup::TAB, u"tab4StrSimple",  muse::mtrc("engraving", "Tab. 4-str. simple"),  4,  0, 1.5, true,  true, true,  false, false, defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::NONE,    true,  false, true,  false, false, false, true,  false),
        StaffType(StaffGroup::TAB, u"tab4StrCommon",  muse::mtrc("engraving", "Tab. 4-str. common"),  4,  0, 1.5, true,  true, false, true, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::SHORTER, true,  true,  true,  false, false, true,  true,  true),
        StaffType(StaffGroup::TAB, u"tab4StrFull",    muse::mtrc("engraving", "Tab. 4-str. full"),    4,  0, 1.5, true,  true, false, true, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::SLASHED, true,  true,  true,  true,  false, true,  true,  true),
        StaffType(StaffGroup::TAB, u"tab5StrSimple",  muse::mtrc("engraving", "Tab. 5-str. simple"),  5,  0, 1.5, true,  true, true,  false, false, defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::NONE,    true,  false, true,  false, false, false, true,  false),
        StaffType(StaffGroup::TAB, u"tab5StrCommon",  muse::mtrc("engraving", "Tab. 5-str. common"),  5,  0, 1.5, true,  true, false, true, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::SHORTER, true,  true,  true,  false, false, true,  true,  true),
        StaffType(StaffGroup::TAB, u"tab5StrFull",    muse::mtrc("engraving", "Tab. 5-str. full"),    5,  0, 1.5, true,  true, false, true, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::SLASHED, true,  true,  true,  true,  false, true,  true,  true),
        StaffType(StaffGroup::TAB, u"tabUkulele",     muse::mtrc("engraving", "Tab. ukulele"),        4,  0, 1.5, true,  true, false, true, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::SHORTER, true,  true,  true,  false, false, true,  true,  true),
        StaffType(StaffGroup::TAB, u"tabBalajka",     muse::mtrc("engraving", "Tab. balalaika"),      3,  0, 1.5, true,  true, false, true, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::SHORTER, true,  true,  true,  false, false, true,  true,  true),
        StaffType(StaffGroup::TAB, u"tabDulcimer",    muse::mtrc("engraving", "Tab. dulcimer"),       3,  0, 1.5, true,  true, false, true, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::SHORTER, true,  true,  true,  false, true,  true,  true,  true),
        StaffType(StaffGroup::TAB, u"tab6StrItalian", muse::mtrc("engraving", "Tab. 6-str. Italian"), 6,  0, 1.5, false, true, true,  true, false,  defaultColor, u"MuseScore Tab Italian",15, 0, true,  false, u"MuseScore Tab Renaiss",10, 0, TablatureSymbolRepeat::NEVER, true,  TablatureMinimStyle::NONE,    true,  true,  false, false, true,  false, true,  false),
        StaffType(StaffGroup::TAB, u"tab6StrFrench",  muse::mtrc("engraving", "Tab. 6-str. French"),  6,  0, 1.5, false, true, true,  true, false,  defaultColor, u"MuseScore Tab French", 15, 0, true,  false, u"MuseScore Tab Renaiss",10, 0, TablatureSymbolRepeat::NEVER, true,  TablatureMinimStyle::NONE,    false, false, false, false, false, false, false, false),
        StaffType(StaffGroup::TAB, u"tab7StrCommon",  muse::mtrc("engraving", "Tab. 7-str. common"),  7,  0, 1.5, true,  true, false, true, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::SHORTER, true,  true,  true,  false, false, true,  true,  true),
        StaffType(StaffGroup::TAB, u"tab8StrCommon",  muse::mtrc("engraving", "Tab. 8-str. common"),  8,  0, 1.5, true,  true, false, true, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::SHORTER, true,  true,  true,  false, false, true,  true,  true),
        StaffType(StaffGroup::TAB, u"tab9StrCommon",  muse::mtrc("engraving", "Tab. 9-str. common"),  9,  0, 1.5, true,  true, false, true, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::SHORTER, true,  true,  true,  false, false, true,  true,  true),
        StaffType(StaffGroup::TAB, u"tab10StrCommon", muse::mtrc("engraving", "Tab. 10-str. common"), 10, 0, 1.5, true,  true, false, true, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::SHORTER, true,  true,  true,  false, false, true,  true,  true),
        StaffType(StaffGroup::TAB, u"tab7StrSimple",  muse::mtrc("engraving", "Tab. 7-str. simple"),  7,  0, 1.5, true,  true, true, false, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::NONE,    true,  false, true,  false, false, false, true,  false),
        StaffType(StaffGroup::TAB, u"tab8StrSimple",  muse::mtrc("engraving", "Tab. 8-str. simple"),  8,  0, 1.5, true,  true, true, false, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::NONE,    true,  false, true,  false, false, false, true,  false),
        StaffType(StaffGroup::TAB, u"tab9StrSimple",  muse::mtrc("engraving", "Tab. 9-str. simple"),  9,  0, 1.5, true,  true, true, false, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::NONE,    true,  false, true,  false, false, false, true,  false),
        StaffType(StaffGroup::TAB, u"tab10StrSimple", muse::mtrc("engraving", "Tab. 10-str. simple"), 10, 0, 1.5, true,  true, true, false, false,  defaultColor, u"MuseScore Tab Modern", 15, 0, false, true,  u"MuseScore Tab Sans",                     9, 0,  TablatureSymbolRepeat::NEVER, false, TablatureMinimStyle::NONE,    true,  false, true,  false, false, false, true,  false),
    };

    // JiMStaff 12-TET (Milestone 1): a STANDARD-group variant spanning one
    // 1200-cent period across 13 staff locations (100 cents = one line
    // distance). Clef, key signatures, and ledger lines are suppressed;
    // Kernel-selected ratio-lines are drawn by the JiMS StaffLines branch.
    // Keep in sync with StaffTypes::MELO_12TET.
    StaffType melo(StaffGroup::STANDARD, u"jims12tet", melo::presetName(),
                   13, 0, 1, false, true, false, true, false, false, false, defaultColor);
    melo.setMelo(true);
    melo.setMeloJiLines(true);
    // A preset contains configuration only. The new-score authoring boundary
    // creates the composition's single spelled reference after measures exist.
    String configuration, error;
    if (!melo::defaultStaffConfiguration(configuration, error)) {
        LOGE() << error;
    }
    melo.setMeloStateJson(configuration);
    m_presets.push_back(melo);
}
/* *INDENT-ON* */
} // namespace mu::engraving
