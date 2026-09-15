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

#pragma once

#include <array>
#include <map>

#include "draw/types/font.h"

#include "engravingitem.h"

#include "../types/types.h"

#include "modularity/ioc.h"
#include "../iengravingconfiguration.h"

namespace mu::engraving {
class Chord;
class ChordRest;
class Staff;
class System;

//---------------------------------------------------------
//   TablatureFont
//---------------------------------------------------------

constexpr int NUM_OF_DIGITFRETS = 100; // the max fret number which can be rendered with numbers
constexpr int NUM_OF_LETTERFRETS = 17; // the max fret number which can be rendered with letters
constexpr int NUM_OF_BASSSTRING_SLASHES = 5; // the max number of slashes supported for French bass strings notation
                                             // (currently, only 3 slashes are used at most; another two are
                                             // foreseen for future customizability)

// default values for 'grid'-like beaming to use with value symbols in stemless TAB
constexpr double GRID_BEAM_DEF_WIDTH  = 0.25; // all values in sp
constexpr double GRID_STEM_DEF_HEIGHT = 1.75;
constexpr double GRID_STEM_DEF_WIDTH  = 0.125;

struct TablatureFretFont {
    TablatureFretFont();

    String family;                                            // the family of the physical font to use
    String displayName;                                       // the name to display to the user
    double defSize = 9.0;                                     // the default size of the font
    double defYOffset = 0.0;                                  // the default Y displacement
    Char xChar = u'X';                                        // the char to use for 'x'
    std::array<String, NUM_OF_BASSSTRING_SLASHES> slashChar;  // the char used to draw one or more '/' symbols
    std::array<String, NUM_OF_DIGITFRETS> displayDigit;       // the string to draw for digit frets
    std::array<Char, NUM_OF_LETTERFRETS> displayLetter;       // the char to use for letter frets

    bool read(XmlReader&, int mscVersion);
};

enum class TabVal : char {
    VAL_LONGA = 0,
    VAL_BREVIS,
    VAL_SEMIBREVIS,
    VAL_MINIMA,
    VAL_SEMIMINIMA,
    VAL_FUSA,
    VAL_SEMIFUSA,
    VAL_32,
    VAL_64,
    VAL_128,
    VAL_256,
    VAL_512,
    VAL_1024,
    NUM_OF
};

enum class TablatureMinimStyle : char {
    NONE = 0,                         // do not draw half notes at all
    SHORTER,                          // draw half notes with a shorter stem
    SLASHED                           // draw half notes with stem with two slashes
};

enum class TablatureSymbolRepeat : char {
    NEVER = 0,                      // never repeat the same duration symbol
    SYSTEM,                         // repeat at the beginning of a new system
    MEASURE,                        // repeat at the beginning of a new measure
    ALWAYS                          // always repeat
};

struct TablatureDurationFont {
    String family;                   // the family of the physical font to use
    String displayName;              // the name to display to the user
    double defSize = 0.0;            // the default size of the font
    double defYOffset = 0.0;         // the default Y displacement
    double gridBeamWidth  = GRID_BEAM_DEF_WIDTH;       // the width of the 'grid'-style beam (in sp)
    double gridStemHeight = GRID_STEM_DEF_HEIGHT;      // the height of the 'grid'-style stem (in sp)
    double gridStemWidth  = GRID_STEM_DEF_WIDTH;       // the width of the 'grid'-style stem (in sp)
    // the note value with no beaming in 'grid'-style beaming
    DurationType zeroBeamLevel = DurationType::V_QUARTER;
    Char displayDot;                 // the char to use to draw a dot
    Char displayValue[int(TabVal::NUM_OF)];           // the char to use to draw a duration value

    bool read(XmlReader&, int mscVersion);
};

// ready-made staff types
// keep in sync with the _presets initialization in StaffType::initStaffTypes() and _defaultPreset

enum class StaffTypes : signed char {
    STANDARD,
    PERC_1LINE, PERC_2LINE, PERC_3LINE, PERC_5LINE,
    TAB_6SIMPLE, TAB_6COMMON, TAB_6FULL,
    TAB_4SIMPLE, TAB_4COMMON, TAB_4FULL,
    TAB_5SIMPLE, TAB_5COMMON, TAB_5FULL,
    TAB_UKULELE, TAB_BALALAJKA, TAB_DULCIMER,
    TAB_ITALIAN, TAB_FRENCH,
    TAB_7COMMON, TAB_8COMMON, TAB_9COMMON, TAB_10COMMON,
    TAB_7SIMPLE, TAB_8SIMPLE, TAB_9SIMPLE, TAB_10SIMPLE,
    MELO_12TET,
    STAFF_TYPES,
    // some useful shorthands:
    PERC_DEFAULT = StaffTypes::PERC_5LINE,
    TAB_DEFAULT = StaffTypes::TAB_6COMMON,
};

enum class ShowTiedFret : unsigned char {
    TIE_AND_FRET,
    TIE,
    NONE,
};

enum class ParenthesizeTiedFret : unsigned char {
    START_OF_SYSTEM,
    START_OF_MEASURE,
    NEVER,
};

// JiMStaff scale-dot label display mode (owner epiphany 2026-08-15):
// presentation-only, fork-owned — never part of the Kernel state.
// Auto resolves to Left strictly inside the Kernel's label-legibility
// range and Split at or outside it; the RESOLVED value is never
// serialized.
enum class MeloScaleDotLabelMode : unsigned char {
    Auto = 0,
    None,
    Left,
    Split,
};

// JiMStaff Milestone 8: per-staff-type octave-band elision override
// (MuseScore's per-staff hideWhenEmpty shape). Presentation-only,
// fork-owned, never part of the Kernel state. Auto follows the score
// style; On/Off beat the style in both directions. Persisted in .mscx
// beside jimsScaleDotLabels; absent reads as Auto.
enum class MeloElideOctaves : unsigned char {
    Auto = 0,
    On,
    Off,
};

//---------------------------------------------------------
//   StaffType
//---------------------------------------------------------

class StaffType
{
    static inline muse::GlobalInject<IEngravingConfiguration> configuration;
public:
    StaffType();

    StaffType(StaffGroup sg, const String& xml, const String& name, int lines, int stpOff, double lineDist, bool genClef, bool showBarLines,
              bool stemless, bool genTimeSig, bool genKeySig, bool showLedgerLiness, bool invisible, const Color& color);

    StaffType(StaffGroup sg, const String& xml, const String& name, int lines, int stpOff, double lineDist, bool genClef, bool showBarLines,
              bool stemless, bool genTimesig, bool invisible, const Color& color, const String& durFontName, double durFontSize,
              double durFontUserY, double genDur, bool fretFontUseTextStyle, const String& fretFontName, double fretFontSize,
              double fretFontUserY, TablatureSymbolRepeat symRepeat, bool linesThrough, TablatureMinimStyle minimStyle, bool onLines,
              bool showRests, bool stemsDown, bool stemThrough, bool upsideDown, bool showTabFingering, bool useNumbers, bool showBackTied);

    virtual ~StaffType() = default;

    bool operator==(const StaffType&) const;

    const MStyle& style() const;
    const Score* score() const { return m_score; }

    StaffGroup group() const { return m_group; }
    void setGroup(StaffGroup g) { m_group = g; }
    StaffTypes type() const;
    const String& name() const { return m_name; }
    const String& xmlName() const { return m_xmlName; }
    void setName(const String& val) { m_name = val; }
    void setXmlName(const String& val) { m_xmlName = val; }
    String translatedGroupName() const;

    void setLines(int val) { m_lines = val; }
    int lines() const { return m_lines; }
    int middleLine() const;
    int bottomLine() const;
    void setStepOffset(int v) { m_stepOffset = v; }
    int stepOffset() const { return m_stepOffset; }
    void setLineDistance(const Spatium& val) { m_lineDistance = val; }
    Spatium lineDistance() const { return m_lineDistance; }
    void setGenClef(bool val) { m_genClef = val; }
    bool genClef() const { return m_genClef; }
    void setShowBarlines(bool val) { m_showBarlines = val; }
    bool showBarlines() const { return m_showBarlines; }
    double userMag() const { return m_userMag; }
    bool isSmall() const { return m_small; }
    bool invisible() const { return m_invisible; }
    const Color& color() const { return m_color; }
    void setUserMag(double val) { m_userMag = val; }
    void setSmall(bool val) { m_small = val; }
    void setInvisible(bool val) { m_invisible = val; }
    void setColor(const Color& val) { m_color = val; }
    Spatium yoffset() const { return m_yoffset; }
    void setYoffset(Spatium val) { m_yoffset = val; }
    double spatium() const;

    void setStemless(bool val) { m_stemless = val; }
    bool stemless() const { return m_stemless; }
    bool genTimesig() const { return m_genTimesig; }
    void setGenTimesig(bool val) { m_genTimesig = val; }

    // static function to deal with presets
    static const StaffType* getDefaultPreset(StaffGroup grp);
    static const StaffType* preset(StaffTypes idx);
    static const StaffType* presetFromXmlName(const String& xmlName);

    void setGenKeysig(bool val) { m_genKeysig = val; }
    bool genKeysig() const { return m_genKeysig; }
    void setShowLedgerLines(bool val) { m_showLedgerLines = val; }
    bool showLedgerLines() const { return m_showLedgerLines; }
    void setNoteHeadScheme(NoteHeadScheme s) { m_noteHeadScheme = s; }
    NoteHeadScheme noteHeadScheme() const { return m_noteHeadScheme; }

    String fretString(int fret, int string, bool deadNote) const;     // returns a string with the text for fret
    String durationString(DurationType type, int dots) const;

    // functions to cope with historic TAB's peculiarities, like upside-down, bass string notations
    int     physStringToVisual(int strg) const;                   // return the string in visual order from physical string
    int     visualStringToPhys(int line) const;                   // return the string in physical order from visual string
    double   physStringToYOffset(int strg) const;                  // return the string Y offset (in sp, chord-relative)

    // JiMStaff (Milestone 1): the STANDARD-group JiMS staff variant.
    bool isMelo() const { return m_melo; }
    void setMelo(bool val) { m_melo = val; }
    const String& meloStateJson() const { return m_meloStateJson; }
    void setMeloStateJson(const String& s);
    const String& meloTonicAmbit() const { return m_meloTonicAmbit; }
    void setMeloTonicAmbit(const String& s) { m_meloTonicAmbit = s; }
    // EXPERIMENTAL (owner request 2026-08-14, not locked in): draw the
    // Just Intonation diatonic scaffold instead of the mid-period line.
    MeloScaleDotLabelMode meloScaleDotLabelMode() const { return m_meloScaleDotLabelMode; }
    MeloScaleDotLabelMode meloResolvedScaleDotLabelMode() const;
    struct MeloFrameView;   // Milestone 8 (defined below)
    // The ONE shared header-geometry calculation (labels FINAL §5.4.4):
    // layout, drawing, and every system's margin reservation all read
    // this — never independent formulas. Widths in points.
    struct MeloHeaderGeometry {
        double clefRx = 0.0;
        double indicatorW = 0.0;
        double leftLabelBand = 0.0;    // label column left of the dots
        double rightLabelBand = 0.0;   // Split-mode band right of the dots
        double headerWidth = 0.0;      // total reserve left of the first measure
        // Milestone 5 change-indicator terrain (mid-system, after the
        // barline): label band (always present in the terrain, labels sit
        // LEFT of the dots) + dot column + arrow lane + closing stroke.
        double changeLabelBand = 0.0;        // White-member labels, LEFT of the dots
        double changeRightLabelBand = 0.0;   // Grey (chromatic) labels, RIGHT of the dots (owner 2026-08-16)
        double changeLeftArrowLane = 0.0;    // mode arrow(s), LEFT of the dots between dots and labels (owner 2026-09-12)
        double changeArrowLane = 0.0;        // key arrow(s), RIGHT of the dots
        double changeTerrainWidth = 0.0;
        double keyLabelAdvance = 0.0;        // "[PitchN]: " current-key label, left of the tonic row (owner spec 2026-08-17)
        // Milestone 8, owner ruling 3b (2026-08-18): a hollow stack follows the
        // keyboard precedent — a brace joins its bands at the system head.
        double braceWidth = 0.0;             // brace glyph width (x-magnified) + akkoladeBarDistance; 0 unless banded
        double braceMagX = 0.0;              // horizontal magnification of the brace glyph (MuseScore's Bracket rule)
    };
    // `view` (Milestone 8, optional): for a banded view the current-key
    // label advance is the WIDEST band label, so every band's "[PitchN]:"
    // fits the reserved header; null or a one-band view is today's.
    MeloHeaderGeometry meloHeaderGeometry(double spatium, double defaultSpatium, const MeloFrameView* view = nullptr) const;
    void setMeloScaleDotLabelMode(MeloScaleDotLabelMode mode) { m_meloScaleDotLabelMode = mode; }
    MeloElideOctaves meloElideOctaves() const { return m_meloElideOctaves; }
    void setMeloElideOctaves(MeloElideOctaves mode) { m_meloElideOctaves = mode; }
    const String& meloRatioLineExtentJson() const { return m_meloRatioLineExtentJson; }
    void setMeloRatioLineExtentJson(const String& value) { m_meloRatioLineExtentJson = value; }
    bool meloJiLines() const { return m_meloJiLines; }
    bool meloExtentIsEmptyDefault() const { return m_meloExtentIsEmptyDefault; }
    void setMeloExtentIsEmptyDefault(bool value) { m_meloExtentIsEmptyDefault = value; }
    void setMeloJiLines(bool val) { m_meloJiLines = val; }
    // Derived frame cache (Kernel frame_for_melody result; keyed by the
    // state + melody it was computed from; NEVER serialized). Top cents
    // drives the seam's affine map; empty cache means the degenerate
    // whole-period frame from the configured line count.
    struct MeloSegment {
        double lowerCents = 0.0;
        double upperCents = 0.0;
        bool whole = true;
    };
    const std::vector<MeloSegment>& meloFrameSegments() const { return m_meloFrameSegments; }
    const muse::String& meloFrameKey() const { return m_meloFrameKey; }
    void setMeloFrame(const muse::String& key, const std::vector<MeloSegment>& segments) const
    {
        m_meloFrameKey = key;
        m_meloFrameSegments = segments;
    }

    double meloFrameTopCents() const
    {
        return m_meloFrameSegments.empty()
               ? (double)(m_lines - 1) * MELO_CENTS_PER_LINE_DISTANCE
               : m_meloFrameSegments.back().upperCents;
    }

    double meloFrameBottomCents() const
    {
        return m_meloFrameSegments.empty() ? 0.0 : m_meloFrameSegments.front().lowerCents;
    }

    // Drawing density for the continuous JiMS cents axis: how many cents
    // one line-distance spans on the page. Presentation only — never a
    // musical fact (owner ruling 2026-08-14: the staff has no discrete
    // locations; notes sit at their Kernel-supplied cents).
    static constexpr double MELO_CENTS_PER_LINE_DISTANCE = 100.0;
    // The single seam for ALL MeloPresto Staff vertical arithmetic: cents above
    // the staff's lower Do to a chord-relative y in spatium units. The
    // staff spans exactly one 1200-cent period, 100 cents per staff
    // location (one line distance); y grows downward, so 1200 cents (the
    // upper Do boundary) sits at 0 sp and 0 cents at 12 line distances.
    // No other code may embed a cents-to-y formula.
    double meloYFromCents(double centsAboveDo) const;
    // The Kernel's period width for this staff's state (staff_metrics
    // op) — the only source of "1200" in production geometry (M4,
    // binding requirement 12). Returns 0.0 if the Kernel rejects the
    // state; callers treat that as "no frame", never as a default.
    double meloPeriodCents() const;
    // Drag freeze (M4 gate finding 1, 2026-08-16): while a note is being
    // dragged the frame must NOT re-derive — growing the stack under the
    // pointer shifts the note and the view, feeding the drag delta back
    // into itself. Frozen from Note::startDrag to Note::endDrag; the
    // drop re-derives once. Presentation state, mutable like the cache.
    void meloSetFrameFrozen(bool frozen) const { m_meloFrameFrozen = frozen; }
    bool meloFrameFrozen() const { return m_meloFrameFrozen; }
    void meloEnsureFrame(const Score* score, staff_idx_t staffIdx) const;

    // Milestone 8 (octave-band elision, "hollow stacks"): the EXPLICIT
    // derived frame view a consumer draws or hit-tests against — ordered
    // bands (bottom to top, as the Kernel emits them), each carrying its
    // segments, bounds, period indices, Kernel label, and its top edge in
    // line-distance units from the staff top; one style-derived gap
    // between adjacent bands. The legacy whole-piece frame is exactly one
    // band at yTop 0, so one-band coordinates are today's, bit for bit.
    // Which periods survive, each band's bounds/label, and the omitted
    // count are Kernel facts (frame_for_melody with options); the fork
    // slices the melody per system, caches, maps y<->cents, and draws.
    struct MeloFrameBand {
        std::vector<MeloSegment> segments;   // bottom to top
        double lowerCents = 0.0;
        double upperCents = 0.0;
        int lowestPeriodIndex = 0;
        int highestPeriodIndex = 0;
        int labelPeriodIndex = 0;            // the band's lowest tonic row (Kernel)
        muse::String tonicLabel;             // Kernel "[PitchN]" for that row (banded views)
        double yTopLd = 0.0;                 // top edge, line distances below the staff top
        double heightLd() const { return (upperCents - lowerCents) / MELO_CENTS_PER_LINE_DISTANCE; }
    };
    struct MeloFrameView {
        std::vector<MeloFrameBand> bands;    // bottom to top; empty = no frame
        double gapLd = 0.0;                  // gap between adjacent bands, line distances
        int omittedPeriodCount = 0;          // Kernel-authoritative
        bool banded = false;                 // false = the legacy whole-piece frame (one band)
        muse::String key;                    // derivation identity (cache key)
        bool empty() const { return bands.empty(); }
        double topCents() const { return bands.empty() ? 0.0 : bands.back().upperCents; }
        double bottomCents() const { return bands.empty() ? 0.0 : bands.front().lowerCents; }
        // Total drawn height in line distances: sum of band heights plus one
        // gap per interior boundary. For one band this is the frame height.
        double heightLd() const;
        // The band whose closed [lower, upper] range holds `cents`, else null.
        const MeloFrameBand* bandForCents(double cents) const;
        // Piecewise cents -> y (line-distance units below the staff top).
        // Inside a band: that band's affine map; inside a gap: linear across
        // the gap between the two band edges (drawing geometry only, e.g.
        // an arrow shaft crossing a gap).
        double yLdFromCents(double cents) const;
        // Piecewise y -> cents (inverse). y inside a gap snaps to the nearest
        // band-edge pitch; an exact midpoint resolves toward the LOWER-
        // pitched band's edge (deterministic, owner-approved plan §3.3).
        // Above the top band / below the bottom band the outer band's
        // affine map extrapolates (today's edge behaviour).
        double centsFromYLd(double yLd) const;
    };
    // The frame view for this staff on `system` (whole-piece view when
    // `system` is null or elision is not in effect for that system).
    // Cached per (staff-type, system range, state, token, melody, options,
    // effective policy); never re-derived while the frame is frozen.
    const MeloFrameView& meloFrameView(const Score* score, staff_idx_t staffIdx, const System* system) const;
    // The legacy whole-piece view (jimsEnsureFrame's cache as one band).
    const MeloFrameView& meloWholeFrameView(const Score* score, staff_idx_t staffIdx) const;
    // The EFFECTIVE elision policy for one system (style + first-system
    // rule + this staff type's Auto/On/Off override, MuseScore's
    // hide-empty-staves shape). Presentation only.
    bool meloElisionActive(const Score* score, staff_idx_t staffIdx, const System* system) const;
    // Milestone 8: cents -> chord-relative y in spatium units for `view`
    // (the seam jimsYFromCents generalizes to; identical for one band).
    double meloYFromCents(double centsAboveDo, const MeloFrameView& view) const;
    String tabBassStringPrefix(int strg, bool* hasFret) const;   // return a string with the prefix, if any, identifying a bass string
    int     numOfTabLedgerLines(int string) const;

    // properties getters (some getters require updated metrics)
    double durationBoxH() const;
    double durationBoxY() const;

    const muse::draw::Font& durationFont() const { return m_durationFont; }
    const TablatureDurationFont& tabDurationFont() const { return m_durationFonts[m_durationFontIdx]; }
    const String& durationFontName() const { return m_durationFonts[m_durationFontIdx].displayName; }
    double durationFontSize() const { return m_durationFontSize; }
    double durationFontUserY() const { return m_durationFontUserY; }
    double durationFontYOffset() const;
    double durationGridYOffset() const { return m_durationGridYOffset; }
    double fretBoxH() const { return m_fretBoxH; }
    double deadFretBoxH() const { return m_deadFretBoxH; }
    double fretBoxY() const;
    double deadFretBoxY() const;

    // 2 methods to return the size of a box masking lines under a fret mark
    double fretMaskH() const;
    double fretMaskY() const;

    const muse::draw::Font& fretFont() const { return m_fretFont; }
    const String fretFontName() const { return m_fretFontInfo.displayName; }
    double fretFontSize() const { return m_fretFontSize; }
    double fretFontUserY() const { return m_fretFontUserY; }
    double fretFontYOffset() const;
    bool  genDurations() const { return m_genDurations; }
    bool  linesThrough() const { return m_linesThrough; }
    TablatureMinimStyle minimStyle() const { return m_minimStyle; }
    TablatureSymbolRepeat symRepeat() const { return m_symRepeat; }
    bool  onLines() const { return m_onLines; }
    bool  showRests() const { return m_showRests; }
    bool  stemsDown() const { return m_stemsDown; }
    bool  stemThrough() const { return m_stemsThrough; }
    bool  upsideDown() const { return m_upsideDown; }
    bool  showTabFingering() const { return m_showTabFingering; }
    bool  useNumbers() const { return m_useNumbers; }
    bool  showBackTied() const { return m_showBackTied; }
    bool  fretUseTextStyle() const { return m_fretUseTextStyle; }
    TextStyleType fretTextStyle() const { return m_fretTextStyle; }
    size_t fretPresetIdx() const { return m_fretPresetIdx; }

    // properties setters (setting some props invalidates metrics)
    void  setDurationFontName(const String&);
    void  setDurationFontSize(double);
    void  setDurationFontUserY(double val) { m_durationFontUserY = val; }
    void  setFretFontSize(double);
    void  setFretFontUserY(double val) { m_fretFontUserY = val; }
    void  setGenDurations(bool val) { m_genDurations = val; }
    void  setLinesThrough(bool val) { m_linesThrough = val; }
    void  setMinimStyle(TablatureMinimStyle val) { m_minimStyle = val; }
    void  setSymbolRepeat(TablatureSymbolRepeat val) { m_symRepeat  = val; }
    void  setOnLines(bool);
    void  setShowRests(bool val) { m_showRests = val; }
    void  setStemsDown(bool val) { m_stemsDown = val; }
    void  setStemsThrough(bool val) { m_stemsThrough = val; }
    void  setUpsideDown(bool val) { m_upsideDown = val; }
    void  setShowTabFingering(bool val) { m_showTabFingering = val; }
    void  setUseNumbers(bool val);
    void  setShowBackTied(bool val) { m_showBackTied = val; }
    void  setScore(Score* score) { m_score = score; }
    void  setFretUseTextStyle(bool val) { m_fretUseTextStyle = val; }
    void  setFretTextStyle(const TextStyleType& val);
    void  setFretPresetIdx(size_t idx);
    void  setFretPreset(const String& str);

    bool isTabStaff() const { return m_group == StaffGroup::TAB; }
    bool isDrumStaff() const { return m_group == StaffGroup::PERCUSSION; }

    bool isSimpleTabStaff() const;
    bool isCommonTabStaff() const;
    bool isHiddenElementOnTab(Sid commonTabStyle, Sid simpleTabStyle) const;

    void styleChanged();

    // static functions for font config files
    static std::vector<String> tabFontNames(bool bDuration);
    static bool tabFontData(bool bDuration, size_t nIdx, double& pSize, double& pYOff);

    static void initStaffTypes(const Color& defaultColor);
    static const std::vector<StaffType>& presets() { return m_presets; }

private:

    friend class TabDurationSymbol;

    Score* m_score = nullptr;

    double defaultSpatium() const;
    void  setDurationMetrics();
    void  setFretMetrics();

    static bool readTabConfigFile(const String& fileName);

    StaffGroup m_group = StaffGroup::STANDARD;

    String m_xmlName;         // the name used to reference this preset in instruments.xml
    String m_name;            // user visible name

    double m_userMag = 1.0;           // allowed 0.1 - 10.0
    Spatium m_yoffset;
    bool m_small = false;
    bool m_invisible = false;
    Color m_color;

    int m_lines = 5;
    int m_stepOffset = 0;
    Spatium m_lineDistance = 1_sp;

    // JiMStaff (Milestone 1): marker, Kernel-owned serialized section
    // state, and the tonic-ambit token (jims.tonic-ambit.v1 envelope).
    // Only authoritative state — projected geometry is never stored.
    bool m_melo = false;
    String m_meloStateJson;
    String m_meloTonicAmbit;
    bool m_meloJiLines = false;
    // Derived on load; copied with a span and swapped by extent undo commands.
    // The first note replaces its empty centre anchor, rather than widening it.
    bool m_meloExtentIsEmptyDefault = true;
    MeloScaleDotLabelMode m_meloScaleDotLabelMode = MeloScaleDotLabelMode::Auto;
    MeloElideOctaves m_meloElideOctaves = MeloElideOctaves::Auto;
    String m_meloRatioLineExtentJson;
    mutable muse::String m_meloFrameKey;
    mutable muse::String m_meloSectionFrameKey;
    mutable std::vector<MeloSegment> m_meloSectionFrameSegments;
    void meloEnsureSectionFrame(const Score* score, staff_idx_t staffIdx) const;
    const MeloFrameView& meloSectionFrameView(const Score* score, staff_idx_t staffIdx, const System* system) const;
    // Owner decision 2026-09-12 (1a): with octave elision off, the union of
    // the coverage of exactly the state sections a system contains, Do-line
    // anchored within the system (melo::alignSectionFrames), as one band.
    const MeloFrameView& meloSystemUnionFrameView(const Score* score, staff_idx_t staffIdx, const System* system) const;
    mutable bool m_meloFrameFrozen = false;
    mutable std::vector<MeloSegment> m_meloFrameSegments;
    // Milestone 8: explicit per-range frame views, keyed by the system
    // range ("whole" or the system's tick range); each entry remembers the
    // full derivation key it was computed from. NEVER serialized.
    mutable std::map<muse::String, MeloFrameView> m_meloFrameViews;

    bool m_showBarlines = true;
    bool m_showLedgerLines = true;
    bool m_stemless = false;       // do not show stems

    bool m_genClef = true;         // create clef at beginning of system
    bool m_genTimesig = true;      // whether time signature is shown or not
    bool m_genKeysig = true;       // create key signature at beginning of system

    // Standard: configurable properties
    NoteHeadScheme m_noteHeadScheme = NoteHeadScheme::HEAD_NORMAL;

    // TAB: configurable propertiesm
    double m_durationFontSize = 15.0;       // the size (in points) for the duration symbol font
    double m_durationFontUserY = 0.0;       // the vertical offset (spatium units) for the duration symb. font
    // user configurable
    double m_fretFontSize  = 10.0;          // the size (in points) for the fret marks font
    double m_fretFontUserY = 0.0;           // additional vert. offset of fret marks with respect to
    // the string line (spatium unit); user configurable
    bool m_genDurations = false;            // whether duration symbols are drawn or not
    bool m_linesThrough = false;            // whether lines for strings and stems may pass through fret marks or not
    TablatureMinimStyle m_minimStyle = TablatureMinimStyle::NONE;      // how to draw minim stems (stem-and-beam durations only)
    TablatureSymbolRepeat m_symRepeat = TablatureSymbolRepeat::NEVER;  // if and when to repeat the same duration symbol
    bool m_onLines = true;                  // whether fret marks are drawn on the string lines or between them
    bool m_showRests = false;               // whether to draw rests or not
    bool m_stemsDown = true;                // stems are drawn downward (stem-and-beam durations only)
    bool m_stemsThrough = true;             // stems are drawn through the staff rather than beside it (stem-and-beam durations only)
    bool m_upsideDown = false;              // whether lines are drawn with highest string at top (false) or at bottom (true)
    bool m_showTabFingering = false;        // Allow fingering in tablature staff (true) or not (false)
    bool m_useNumbers = true;               // true: use numbers ('0' - ...) for frets | false: use letters ('a' - ...)
    bool m_showBackTied = true;             // whether back-tied notes are shown or not

    // TAB: internally managed variables
    // Note: values in RASTER UNITS are independent from score scaling and
    //    must be multiplied by magS() to be used in contexts using sp units
    double m_durationBoxH = 0.0;
    double m_durationBoxY = 0.0;            // the height and the y rect.coord. (relative to staff top line)
                                            // of a box bounding all duration symbols (raster units) internally computed:
                                            // depends upon _onString and the metrics of the duration font
    muse::draw::Font m_durationFont;        // font used to draw dur. symbols; cached for efficiency
    size_t m_durationFontIdx = 0;           // the index of current dur. font in dur. font array
    double m_durationYOffset = 0.0;         // the vertical offset to draw duration symbols with respect to the
    // string lines (raster units); internally computed: depends upon _onString and duration font
    double m_durationGridYOffset = 0.0;     // the vertical offset to draw the bottom of duration grid with respect to the
    // string lines (raster units); internally computed: depends upon _onstring and duration font
    double m_fretBoxH = 0.0;
    double m_fretBoxY = 0.0;                // the height and the y rect.coord. (relative to staff line)
    double m_deadFretBoxH = 0.0;
    double m_deadFretBoxY = 0.0;
    // of a box bounding all fret characters (raster units) internally computed:
    // depends upon _onString, _useNumbers and the metrics of the fret font
    bool m_fretUseTextStyle = false;
    TextStyleType m_fretTextStyle = TextStyleType::TAB_FRET_NUMBER;
    muse::draw::Font m_fretFont;                      // font used to draw fret marks; cached for efficiency
    TablatureFretFont m_fretFontInfo;
    size_t m_fretPresetIdx = 0;           // the index of current fret font in fret font array
    double m_fretYOffset = 0.0;             // the vertical offset to draw fret marks with respect to the string lines;
    double m_deadFretYOffset = 0.0;
    // (raster units); internally computed: depends upon _onString, _useNumbers
    // and the metrics of the fret font

    // the array of configured fonts
    static std::vector<TablatureFretFont> m_fretFonts;
    static std::vector<TablatureDurationFont> m_durationFonts;
    static std::vector<StaffType> m_presets;
};

//---------------------------------------------------------
//   TabDurationSymbol
//    EngravingItem used to draw duration symbols above tablatures
//---------------------------------------------------------

enum class TabBeamGrid : char {
    NONE = 0,
    INITIAL,
    MEDIALFINAL,
    NUM_OF
};

class TabDurationSymbol final : public EngravingItem
{
    OBJECT_ALLOCATOR(engraving, TabDurationSymbol)
    DECLARE_CLASSOF(ElementType::TAB_DURATION_SYMBOL)

public:
    TabDurationSymbol(ChordRest* parent);
    TabDurationSymbol(ChordRest* parent, const StaffType* tab, DurationType type, int dots);
    TabDurationSymbol(const TabDurationSymbol&);
    TabDurationSymbol* clone() const override { return new TabDurationSymbol(*this); }

    bool isEditable() const override { return false; }

    const StaffType* tab() const { return m_tab; }
    const String& text() const { return m_text; }
    void setDuration(DurationType type, int dots, const StaffType* tab)
    {
        m_tab = tab;
        m_text = tab->durationString(type, dots);
    }

    bool isRepeat() const { return m_repeat; }
    void setRepeat(bool val) { m_repeat = val; }

    struct LayoutData : public EngravingItem::LayoutData {
        TabBeamGrid beamGrid = TabBeamGrid::NONE;         // value for special 'English' grid display
        double beamLength = 0.0;                          // if _grid==MEDIALFINAL, length of the beam toward previous grid element
        int beamLevel = 0.0;                                // if _grid==MEDIALFINAL, the number of beams
    };
    DECLARE_LAYOUTDATA_METHODS(TabDurationSymbol)

private:

    const StaffType* m_tab = nullptr;
    String m_text;
    bool m_repeat = false;
};
} // namespace mu::engraving
