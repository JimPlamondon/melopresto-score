/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2023 MuseScore Limited and others
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
#include "tdraw.h"

#include <map>

#include <algorithm>

#include "../../melo/melobridge.h"
#include "../../melo/melochange.h"
#include "../../melo/melopitchlabel.h"

#include "defer.h"

#include "draw/fontmetrics.h"
#include "draw/svgrenderer.h"

#include "rendering/paintoptions.h"
#include "style/style.h"
#include "style/defaultstyle.h"

#include "dom/accidental.h"
#include "dom/actionicon.h"
#include "dom/ambitus.h"
#include "dom/arpeggio.h"
#include "dom/chordbracket.h"
#include "dom/articulation.h"

#include "dom/bagpembell.h"
#include "dom/barline.h"
#include "dom/beam.h"
#include "dom/bend.h"
#include "dom/box.h"
#include "dom/bracket.h"
#include "dom/breath.h"

#include "dom/chord.h"
#include "dom/chordline.h"
#include "dom/clef.h"
#include "dom/capo.h"

#include "dom/deadslapped.h"
#include "dom/dynamic.h"

#include "dom/expression.h"

#include "dom/fermata.h"
#include "dom/figuredbass.h"
#include "dom/fingering.h"
#include "dom/fret.h"

#include "dom/glissando.h"
#include "dom/gradualtempochange.h"
#include "dom/guitarbend.h"

#include "dom/hairpin.h"
#include "dom/hammeronpulloff.h"
#include "dom/harppedaldiagram.h"
#include "dom/harmonicmark.h"
#include "dom/harmony.h"
#include "dom/hook.h"

#include "dom/image.h"
#include "dom/instrchange.h"
#include "dom/instrumentname.h"

#include "dom/jump.h"

#include "dom/keysig.h"

#include "dom/laissezvib.h"
#include "dom/lasso.h"
#include "dom/layoutbreak.h"
#include "dom/ledgerline.h"
#include "dom/letring.h"
#include "dom/lyrics.h"

#include "dom/marker.h"
#include "dom/measurenumber.h"
#include "dom/measurerepeat.h"
#include "dom/mmrest.h"
#include "dom/mmrestrange.h"

#include "dom/navigate.h"
#include "dom/note.h"
#include "dom/notedot.h"
#include "dom/noteline.h"

#include "dom/ornament.h"
#include "dom/ottava.h"

#include "dom/page.h"
#include "dom/parenthesis.h"
#include "dom/partialtie.h"
#include "dom/palmmute.h"
#include "dom/part.h"
#include "dom/pedal.h"
#include "dom/pickscrape.h"
#include "dom/playcounttext.h"
#include "dom/playtechannotation.h"

#include "dom/rasgueado.h"
#include "dom/rehearsalmark.h"
#include "dom/rest.h"

#include "dom/score.h"
#include "dom/shadownote.h"
#include "dom/slur.h"
#include "dom/spacer.h"
#include "dom/staff.h"
#include "dom/stafflines.h"
#include "dom/staffstate.h"
#include "dom/stafftext.h"
#include "dom/stafftype.h"
#include "dom/stafftypechange.h"
#include "dom/staffvisibilityindicator.h"
#include "dom/stem.h"
#include "dom/stemslash.h"
#include "dom/sticking.h"
#include "dom/stringtunings.h"
#include "dom/symbol.h"
#include "dom/systemdivider.h"
#include "dom/systemtext.h"
#include "dom/systemlock.h"
#include "dom/soundflag.h"

#include "dom/tapping.h"
#include "dom/tempotext.h"
#include "dom/text.h"
#include "dom/textbase.h"
#include "dom/textline.h"
#include "dom/textlinebase.h"
#include "dom/tie.h"
#include "dom/timesig.h"
#include "dom/anchors.h"
#include "dom/tremolosinglechord.h"
#include "dom/tremolotwochord.h"
#include "dom/tremolobar.h"
#include "dom/trill.h"
#include "dom/tripletfeel.h"
#include "dom/tuplet.h"

#include "dom/vibrato.h"
#include "dom/volta.h"

#include "dom/whammybar.h"

#include "editing/mscoreview.h"

#include "infrastructure/rtti.h"

#include "stemlayout.h"

// dev
#include "dom/system.h"
#include "dom/measure.h"
#include "dom/segment.h"
#include "dom/chord.h"

using namespace mu::engraving;
using namespace mu::engraving::rtti;
using namespace mu::engraving::rendering::score;
using namespace muse;
using namespace muse::draw;

void TDraw::drawItem(const EngravingItem* item, Painter* painter, const PaintOptions& opt)
{
    switch (item->type()) {
    case ElementType::ACCIDENTAL:   draw(item_cast<const Accidental*>(item), painter, opt);
        break;
    case ElementType::ACTION_ICON:  draw(item_cast<const ActionIcon*>(item), painter, opt);
        break;
    case ElementType::AMBITUS:      draw(item_cast<const Ambitus*>(item), painter, opt);
        break;
    case ElementType::ARPEGGIO:     draw(item_cast<const Arpeggio*>(item), painter, opt);
        break;
    case ElementType::CHORD_BRACKET: draw(item_cast<const ChordBracket*>(item), painter, opt);
        break;
    case ElementType::ARTICULATION: draw(item_cast<const Articulation*>(item), painter, opt);
        break;

    case ElementType::BAGPIPE_EMBELLISHMENT: draw(item_cast<const BagpipeEmbellishment*>(item), painter, opt);
        break;
    case ElementType::BAR_LINE:     draw(item_cast<const BarLine*>(item), painter, opt);
        break;
    case ElementType::BEAM:         draw(item_cast<const Beam*>(item), painter, opt);
        break;
    case ElementType::BEND:         draw(item_cast<const Bend*>(item), painter, opt);
        break;
    case ElementType::HBOX:         draw(item_cast<const HBox*>(item), painter, opt);
        break;
    case ElementType::VBOX:         draw(item_cast<const VBox*>(item), painter, opt);
        break;
    case ElementType::FBOX:         draw(item_cast<const FBox*>(item), painter, opt);
        break;
    case ElementType::TBOX:         draw(item_cast<const TBox*>(item), painter, opt);
        break;
    case ElementType::BRACKET:      draw(item_cast<const Bracket*>(item), painter, opt);
        break;
    case ElementType::BREATH:       draw(item_cast<const Breath*>(item), painter, opt);
        break;

    case ElementType::CHORDLINE:    draw(item_cast<const ChordLine*>(item), painter, opt);
        break;
    case ElementType::CLEF:         draw(item_cast<const Clef*>(item), painter, opt);
        break;
    case ElementType::CAPO:         draw(item_cast<const Capo*>(item), painter, opt);
        break;

    case ElementType::DEAD_SLAPPED: draw(item_cast<const DeadSlapped*>(item), painter, opt);
        break;
    case ElementType::DYNAMIC:      draw(item_cast<const Dynamic*>(item), painter, opt);
        break;

    case ElementType::EXPRESSION:   draw(item_cast<const Expression*>(item), painter, opt);
        break;

    case ElementType::FERMATA:      draw(item_cast<const Fermata*>(item), painter, opt);
        break;
    case ElementType::FIGURED_BASS: draw(item_cast<const FiguredBass*>(item), painter, opt);
        break;
    case ElementType::FINGERING:    draw(item_cast<const Fingering*>(item), painter, opt);
        break;
    case ElementType::FRET_DIAGRAM: draw(item_cast<const FretDiagram*>(item), painter, opt);
        break;
    case ElementType::FSYMBOL:      draw(item_cast<const FSymbol*>(item), painter, opt);
        break;

    case ElementType::GLISSANDO_SEGMENT: draw(item_cast<const GlissandoSegment*>(item), painter, opt);
        break;
    case ElementType::GRADUAL_TEMPO_CHANGE_SEGMENT: draw(item_cast<const GradualTempoChangeSegment*>(item), painter, opt);
        break;
    case ElementType::GUITAR_BEND_SEGMENT: draw(item_cast<const GuitarBendSegment*>(item), painter, opt);
        break;
    case ElementType::GUITAR_BEND_HOLD_SEGMENT: draw(item_cast<const GuitarBendHoldSegment*>(item), painter, opt);
        break;
    case ElementType::GUITAR_BEND_TEXT: drawTextBase(toTextBase(item), painter, opt);
        break;

    case ElementType::HAIRPIN_SEGMENT: draw(item_cast<const HairpinSegment*>(item), painter, opt);
        break;
    case ElementType::HAMMER_ON_PULL_OFF_SEGMENT: draw(item_cast<const HammerOnPullOffSegment*>(item), painter, opt);
        break;
    case ElementType::HAMMER_ON_PULL_OFF_TEXT: draw(item_cast<const HammerOnPullOffText*>(item), painter, opt);
        break;
    case ElementType::HARP_DIAGRAM: draw(item_cast<const HarpPedalDiagram*>(item), painter, opt);
        break;
    case ElementType::HARMONIC_MARK_SEGMENT: draw(item_cast<const HarmonicMarkSegment*>(item), painter, opt);
        break;
    case ElementType::HARMONY:      draw(item_cast<const Harmony*>(item), painter, opt);
        break;
    case ElementType::HOOK:         draw(item_cast<const Hook*>(item), painter, opt);
        break;

    case ElementType::IMAGE:        draw(item_cast<const Image*>(item), painter, opt);
        break;
    case ElementType::INSTRUMENT_CHANGE: draw(item_cast<const InstrumentChange*>(item), painter, opt);
        break;
    case ElementType::INSTRUMENT_NAME: draw(item_cast<const InstrumentName*>(item), painter, opt);
        break;

    case ElementType::JUMP:         draw(item_cast<const Jump*>(item), painter, opt);
        break;

    case ElementType::KEYSIG:       draw(item_cast<const KeySig*>(item), painter, opt);
        break;
    case ElementType::LAISSEZ_VIB_SEGMENT:  draw(item_cast<const LaissezVibSegment*>(item), painter, opt);
        break;
    case ElementType::LASSO:        draw(item_cast<const Lasso*>(item), painter, opt);
        break;
    case ElementType::LAYOUT_BREAK: draw(item_cast<const LayoutBreak*>(item), painter, opt);
        break;
    case ElementType::LEDGER_LINE:  draw(item_cast<const LedgerLine*>(item), painter, opt);
        break;
    case ElementType::LET_RING_SEGMENT: draw(item_cast<const LetRingSegment*>(item), painter, opt);
        break;
    case ElementType::LYRICS:       draw(item_cast<const Lyrics*>(item), painter, opt);
        break;
    case ElementType::LYRICSLINE_SEGMENT: draw(item_cast<const LyricsLineSegment*>(item), painter, opt);
        break;
    case ElementType::PARTIAL_LYRICSLINE_SEGMENT: draw(item_cast<const LyricsLineSegment*>(item), painter, opt);
        break;

    case ElementType::MARKER:       draw(item_cast<const Marker*>(item), painter, opt);
        break;
    case ElementType::MEASURE_NUMBER: draw(item_cast<const MeasureNumber*>(item), painter, opt);
        break;
    case ElementType::MEASURE_REPEAT: draw(item_cast<const MeasureRepeat*>(item), painter, opt);
        break;
    case ElementType::MMREST:       draw(item_cast<const MMRest*>(item), painter, opt);
        break;
    case ElementType::MMREST_RANGE: draw(item_cast<const MMRestRange*>(item), painter, opt);
        break;

    case ElementType::NOTE:         draw(item_cast<const Note*>(item), painter, opt);
        break;
    case ElementType::NOTEDOT:      draw(item_cast<const NoteDot*>(item), painter, opt);
        break;
    case ElementType::NOTEHEAD:     draw(item_cast<const NoteHead*>(item), painter, opt);
        break;
    case ElementType::NOTELINE_SEGMENT: draw(item_cast<const NoteLineSegment*>(item), painter, opt);
        break;

    case ElementType::ORNAMENT:     draw(item_cast<const Ornament*>(item), painter, opt);
        break;
    case ElementType::OTTAVA_SEGMENT:       draw(item_cast<const OttavaSegment*>(item), painter, opt);
        break;

    case ElementType::PARENTHESIS:          draw(item_cast<const Parenthesis*>(item), painter, opt);
        break;
    case ElementType::PARTIAL_TIE_SEGMENT:  draw(item_cast<const PartialTieSegment*>(item), painter, opt);
        break;
    case ElementType::PALM_MUTE_SEGMENT:    draw(item_cast<const PalmMuteSegment*>(item), painter, opt);
        break;
    case ElementType::PEDAL_SEGMENT:        draw(item_cast<const PedalSegment*>(item), painter, opt);
        break;
    case ElementType::PICK_SCRAPE_SEGMENT:  draw(item_cast<const PickScrapeSegment*>(item), painter, opt);
        break;
    case ElementType::PLAY_COUNT_TEXT:      draw(item_cast<const PlayCountText*>(item), painter, opt);
        break;
    case ElementType::PLAYTECH_ANNOTATION:  draw(item_cast<const PlayTechAnnotation*>(item), painter, opt);
        break;

    case ElementType::RASGUEADO_SEGMENT:    draw(item_cast<const RasgueadoSegment*>(item), painter, opt);
        break;
    case ElementType::REHEARSAL_MARK:       draw(item_cast<const RehearsalMark*>(item), painter, opt);
        break;
    case ElementType::REST:                 draw(item_cast<const Rest*>(item), painter, opt);
        break;

    case ElementType::SHADOW_NOTE:          draw(item_cast<const ShadowNote*>(item), painter, opt);
        break;
    case ElementType::SLUR_SEGMENT:         draw(item_cast<const SlurSegment*>(item), painter, opt);
        break;
    case ElementType::SPACER:               draw(item_cast<const Spacer*>(item), painter, opt);
        break;
    case ElementType::STAFF_LINES:          draw(item_cast<const StaffLines*>(item), painter, opt);
        break;
    case ElementType::STAFF_STATE:          draw(item_cast<const StaffState*>(item), painter, opt);
        break;
    case ElementType::STAFF_TEXT:           draw(item_cast<const StaffText*>(item), painter, opt);
        break;
    case ElementType::STAFFTYPE_CHANGE:     draw(item_cast<const StaffTypeChange*>(item), painter, opt);
        break;
    case ElementType::STAFF_VISIBILITY_INDICATOR: draw(item_cast<const StaffVisibilityIndicator*>(item), painter, opt);
        break;
    case ElementType::STEM:                 draw(item_cast<const Stem*>(item), painter, opt);
        break;
    case ElementType::STEM_SLASH:           draw(item_cast<const StemSlash*>(item), painter, opt);
        break;
    case ElementType::STICKING:             draw(item_cast<const Sticking*>(item), painter, opt);
        break;
    case ElementType::STRING_TUNINGS:       draw(item_cast<const StringTunings*>(item), painter, opt);
        break;
    case ElementType::SYMBOL:               draw(item_cast<const Symbol*>(item), painter, opt);
        break;
    case ElementType::SYSTEM_DIVIDER:       draw(item_cast<const SystemDivider*>(item), painter, opt);
        break;
    case ElementType::SYSTEM_TEXT:          draw(item_cast<const SystemText*>(item), painter, opt);
        break;
    case ElementType::SYSTEM_LOCK_INDICATOR: draw(item_cast<const SystemLockIndicator*>(item), painter, opt);
        break;
    case ElementType::SOUND_FLAG:           draw(item_cast<const SoundFlag*>(item), painter, opt);
        break;

    case ElementType::TAB_DURATION_SYMBOL:  draw(item_cast<const TabDurationSymbol*>(item), painter, opt);
        break;
    case ElementType::TAPPING:              draw(toTapping(item), painter, opt);
        break;
    case ElementType::TAPPING_HALF_SLUR_SEGMENT: draw(toSlurSegment(item), painter, opt);
        break;
    case ElementType::TEMPO_TEXT:           draw(item_cast<const TempoText*>(item), painter, opt);
        break;
    case ElementType::TEXT:                 draw(item_cast<const Text*>(item), painter, opt);
        break;
    case ElementType::TEXTLINE_SEGMENT:     draw(item_cast<const TextLineSegment*>(item), painter, opt);
        break;
    case ElementType::TIE_SEGMENT:          draw(item_cast<const TieSegment*>(item), painter, opt);
        break;
    case ElementType::TIMESIG:              draw(item_cast<const TimeSig*>(item), painter, opt);
        break;
    case ElementType::TIME_TICK_ANCHOR:     draw(item_cast<const TimeTickAnchor*>(item), painter, opt);
        break;
    case ElementType::TREMOLO_SINGLECHORD:  draw(item_cast<const TremoloSingleChord*>(item), painter, opt);
        break;
    case ElementType::TREMOLO_TWOCHORD:     draw(item_cast<const TremoloTwoChord*>(item), painter, opt);
        break;
    case ElementType::TREMOLOBAR:           draw(item_cast<const TremoloBar*>(item), painter, opt);
        break;
    case ElementType::TRILL_SEGMENT:        draw(item_cast<const TrillSegment*>(item), painter, opt);
        break;
    case ElementType::TRIPLET_FEEL:         draw(item_cast<const TripletFeel*>(item), painter, opt);
        break;
    case ElementType::TUPLET:               draw(item_cast<const Tuplet*>(item), painter, opt);
        break;

    case ElementType::VIBRATO_SEGMENT:      draw(item_cast<const VibratoSegment*>(item), painter, opt);
        break;
    case ElementType::VOLTA_SEGMENT:        draw(item_cast<const VoltaSegment*>(item), painter, opt);
        break;

    case ElementType::WHAMMY_BAR_SEGMENT:   draw(item_cast<const WhammyBarSegment*>(item), painter, opt);
        break;

    case ElementType::PAGE:
    case ElementType::SYSTEM:
    case ElementType::MEASURE:
    case ElementType::SEGMENT:
    case ElementType::CHORD:
    case ElementType::GRACE_NOTES_GROUP:
        break;

    default:
        NOT_IMPLEMENTED << " type: " << item->typeName();
        UNREACHABLE;
    }
}

void TDraw::draw(const Accidental* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    IF_ASSERT_FAILED(item->ldata()) {
        return;
    }

    painter->setPen(item->curColor(opt));
    for (const Accidental::LayoutData::Sym& e : item->ldata()->syms) {
        item->drawSymbol(e.sym, painter, PointF(e.x, e.y));
    }
}

void TDraw::draw(const ActionIcon* item, Painter* painter, const PaintOptions&)
{
    TRACE_DRAW_ITEM;
    const ActionIcon::LayoutData* ldata = item->ldata();
    painter->setFont(item->iconFont());
    painter->drawText(ldata->bbox(), muse::draw::AlignCenter, Char(item->icon()));
}

void TDraw::draw(const Ambitus* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    const Ambitus::LayoutData* ldata = item->ldata();
    IF_ASSERT_FAILED(ldata) {
        return;
    }

    double spatium = item->spatium();
    double lw = item->lineWidth().val() * spatium;
    painter->setPen(Pen(item->curColor(opt), lw, PenStyle::SolidLine, PenCapStyle::FlatCap));

    item->drawSymbol(item->noteHead(), painter, ldata->topPos);
    item->drawSymbol(item->noteHead(), painter, ldata->bottomPos);
    if (item->hasLine()) {
        painter->drawLine(ldata->line);
    }

    // draw ledger lines (if not in a palette)
    if (item->segment() && item->track() != muse::nidx) {
        Fraction tick = item->segment()->tick();
        Staff* staff = item->score()->staff(item->staffIdx());
        double lineDist = staff->lineDistance(tick);
        int numOfLines = staff->lines(tick);
        double step = lineDist * spatium;
        double stepTolerance = step * 0.1;
        double ledgerLineLength = item->style().styleS(Sid::ledgerLineLength).val() * spatium;
        double ledgerLineWidth = item->style().styleS(Sid::ledgerLineWidth).val() * spatium;
        painter->setPen(Pen(item->curColor(opt), ledgerLineWidth, PenStyle::SolidLine, PenCapStyle::FlatCap));

        if (ldata->topPos.y() - stepTolerance <= -step) {
            double xMin = ldata->topPos.x() - ledgerLineLength;
            double xMax = ldata->topPos.x() + item->headWidth() + ledgerLineLength;
            for (double y = -step; y >= ldata->topPos.y() - stepTolerance; y -= step) {
                painter->drawLine(PointF(xMin, y), PointF(xMax, y));
            }
        }

        if (ldata->bottomPos.y() + stepTolerance >= numOfLines * step) {
            double xMin = ldata->bottomPos.x() - ledgerLineLength;
            double xMax = ldata->bottomPos.x() + item->headWidth() + ledgerLineLength;
            for (double y = numOfLines * step; y <= ldata->bottomPos.y() + stepTolerance; y += step) {
                painter->drawLine(PointF(xMin, y), PointF(xMax, y));
            }
        }
    }
}

void TDraw::draw(const Arpeggio* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    const Arpeggio::LayoutData* ldata = item->ldata();
    IF_ASSERT_FAILED(ldata) {
        return;
    }

    const double y1 = ldata->bbox().top();
    const double y2 = ldata->bbox().bottom();
    const double lineWidth = item->style().styleMM(Sid::arpeggioLineWidth);

    painter->setPen(Pen(item->curColor(opt), lineWidth, PenStyle::SolidLine, PenCapStyle::FlatCap));
    painter->save();

    switch (item->arpeggioType()) {
    case ArpeggioType::NORMAL:
    case ArpeggioType::UP:
    {
        const RectF& r = ldata->symsBBox;
        painter->rotate(-90.0);
        item->drawSymbols(ldata->symbols, painter, PointF(-r.right() - y1, -r.bottom() + r.height()));
    } break;

    case ArpeggioType::DOWN:
    {
        const RectF& r = ldata->symsBBox;
        painter->rotate(90.0);
        item->drawSymbols(ldata->symbols, painter, PointF(-r.left() + y1, -r.top() - r.height()));
    } break;

    case ArpeggioType::UP_STRAIGHT:
    {
        const RectF& r = ldata->symsBBox;
        double x1 = item->spatium() * 0.5;
        item->drawSymbol(SymId::arrowheadBlackUp, painter, PointF(x1 - r.width() * 0.5, y1 - r.top()));
        double ny1 = y1 - r.top() * 0.5;
        painter->drawLine(LineF(x1, ny1, x1, y2));
    } break;

    case ArpeggioType::DOWN_STRAIGHT:
    {
        const RectF& r = ldata->symsBBox;
        double x1 = item->spatium() * 0.5;
        item->drawSymbol(SymId::arrowheadBlackDown, painter, PointF(x1 - r.width() * 0.5, y2 - r.bottom()));
        double ny2 = y2 + r.top() * 0.5;
        painter->drawLine(LineF(x1, y1, x1, ny2));
    } break;

    case ArpeggioType::BRACKET:
    {
        double w = item->style().styleS(Sid::arpeggioHookLen).val() * item->spatium();
        painter->drawLine(LineF(0.0, y1, w, y1));
        painter->drawLine(LineF(0.0, y2, w, y2));
        painter->drawLine(LineF(0.0, y1 - lineWidth / 2, 0.0, y2 + lineWidth / 2));
    } break;
    }
    painter->restore();
}

void TDraw::draw(const ChordBracket* item, muse::draw::Painter* painter, const PaintOptions& opt)
{
    const Arpeggio::LayoutData* ldata = item->ldata();

    const double lineWidth = item->style().styleMM(Sid::chordBracketLineWidth);
    painter->setPen(Pen(item->curColor(opt), lineWidth, PenStyle::SolidLine, PenCapStyle::FlatCap));

    const double halfLineWidth = 0.5 * lineWidth;
    const double y1 = ldata->bbox().top() + halfLineWidth;
    const double y2 = ldata->bbox().bottom() - halfLineWidth;

    double w = item->absoluteFromSpatium(item->hookLength());

    if (item->hookPos() != DirectionV::DOWN) {
        painter->drawLine(LineF(0.0, y1, w, y1));
    }
    if (item->hookPos() != DirectionV::UP) {
        painter->drawLine(LineF(0.0, y2, w, y2));
    }

    const double x = item->rightSide() ? w - halfLineWidth : halfLineWidth;
    painter->drawLine(LineF(x, y1, x, y2));
}

void TDraw::draw(const Articulation* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    painter->setPen(item->curColor(opt));

    if (item->textType() == ArticulationTextType::NO_TEXT) {
        item->drawSymbol(item->symId(), painter);
    } else {
        drawTextBase(item->text(), painter, opt);
    }
}

void TDraw::draw(const Ornament* item, Painter* painter, const PaintOptions& opt)
{
    draw(static_cast<const Articulation*>(item), painter, opt);
}

void TDraw::draw(const BagpipeEmbellishment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    const BagpipeEmbellishment::LayoutData* data = item->ldata();
    IF_ASSERT_FAILED(data) {
        return;
    }
    const BagpipeEmbellishment::LayoutData::BeamData& dataBeam = data->beamData;

    Pen pen(item->curColor(opt), data->stemLineW, PenStyle::SolidLine, PenCapStyle::FlatCap);
    painter->setPen(pen);

    // draw the notes including stem, (optional) flag and (optional) ledger line
    for (const auto& p : data->notesData) {
        const BagpipeEmbellishment::LayoutData::NoteData& noteData = p.second;

        // Draw Grace Note
        {
            // draw head
            item->drawSymbol(data->headsym, painter, noteData.headXY);

            // draw stem
            painter->drawLine(noteData.stemLine);

            if (data->isDrawFlag) {
                // draw flag
                item->drawSymbol(data->flagsym, painter, noteData.flagXY);
            }
        }

        // draw the ledger line for high A
        if (!noteData.ledgerLine.isNull()) {
            painter->drawLine(noteData.ledgerLine);
        }
    }

    if (data->isDrawBeam) {
        Pen beamPen(item->curColor(opt), dataBeam.width, PenStyle::SolidLine, PenCapStyle::FlatCap);
        painter->setPen(beamPen);
        // draw the beams
        auto drawBeams = [](Painter* painter, const double spatium,
                            const double x1, const double x2, double y)
        {
            // draw the beams
            painter->drawLine(LineF(x1, y, x2, y));
            y += spatium / 1.5;
            painter->drawLine(LineF(x1, y, x2, y));
            y += spatium / 1.5;
            painter->drawLine(LineF(x1, y, x2, y));
        };

        drawBeams(painter, data->spatium, dataBeam.x1, dataBeam.x2, dataBeam.y);
    }
}

static void drawDots(const BarLine* item, Painter* painter, double x,
                     const std::vector<BarLine::LayoutData::MeloDotRows>* dotRows = nullptr)
{
    double spatium = item->spatium();

    if (dotRows) {                          // Milestone 8: a JiMStaff band's own dot rows, per band
        for (const BarLine::LayoutData::MeloDotRows& rows : *dotRows) {
            item->drawSymbol(SymId::repeatDot, painter, PointF(x, rows.y1));
            item->drawSymbol(SymId::repeatDot, painter, PointF(x, rows.y2));
        }
        return;
    }

    double y1l;
    double y2l;
    if (item->explicitParent() == 0) {      // for use in palette (always Bravura)
        //Bravura shifted repeatDot symbol 0.5sp upper in the font itself (1.272)
        y1l = 1.5 * spatium;
        y2l = 2.5 * spatium;
    } else {
        const StaffType* st = item->staffType();
        const int lines = st->lines();
        const double lineDistance = st->lineDistance().toMM(spatium);

        y1l = (static_cast<double>((lines - 1) / 2) - 0.5) * lineDistance;
        y2l = (static_cast<double>(lines / 2) + 0.5) * lineDistance;

        //adjust for staffType offset
        double stYOffset = item->staffOffsetY();
        y1l += stYOffset;
        y2l += stYOffset;
    }

    item->drawSymbol(SymId::repeatDot, painter, PointF(x, y1l));
    item->drawSymbol(SymId::repeatDot, painter, PointF(x, y2l));
}

static void drawTips(const BarLine* item, double y1, double y2, Painter* painter, bool reversed, double x)
{
    if (reversed) {
        if (item->isTop()) {
            item->drawSymbol(SymId::reversedBracketTop, painter, PointF(x - item->symWidth(SymId::reversedBracketTop), y1));
        }
        if (item->isBottom()) {
            item->drawSymbol(SymId::reversedBracketBottom, painter, PointF(x - item->symWidth(SymId::reversedBracketBottom), y2));
        }
    } else {
        if (item->isTop()) {
            item->drawSymbol(SymId::bracketTop, painter, PointF(x, y1));
        }
        if (item->isBottom()) {
            item->drawSymbol(SymId::bracketBottom, painter, PointF(x, y2));
        }
    }
}

// One barline FORM between y1 and y2 (Milestone 8: a JiMStaff barline
// draws every form once per band, never across the gap between bands;
// dotRows, when given, are the repeat-dot rows of that band).
static void drawBarLineForm(const BarLine* item, double y1, double y2,
                            const std::vector<BarLine::LayoutData::MeloDotRows>* dotRows, Painter* painter,
                            const mu::engraving::rendering::PaintOptions& opt)
{
    switch (item->barLineType()) {
    case BarLineType::NORMAL: {
        double lw = item->style().styleMM(Sid::barWidth) * item->mag();
        painter->setPen(Pen(item->curColor(opt), lw, PenStyle::SolidLine, PenCapStyle::FlatCap));
        painter->drawLine(LineF(lw * .5, y1, lw * .5, y2));
    }
    break;

    case BarLineType::BROKEN: {
        double lw = item->style().styleMM(Sid::barWidth) * item->mag();
        painter->setPen(Pen(item->curColor(opt), lw, PenStyle::DashLine, PenCapStyle::FlatCap));
        painter->drawLine(LineF(lw * .5, y1, lw * .5, y2));
    }
    break;

    case BarLineType::DOTTED: {
        double lw = item->style().styleMM(Sid::barWidth) * item->mag();
        painter->setPen(Pen(item->curColor(opt), lw, PenStyle::DotLine, PenCapStyle::FlatCap));
        painter->drawLine(LineF(lw * .5, y1, lw * .5, y2));
    }
    break;

    case BarLineType::END: {
        double lw = item->style().styleMM(Sid::barWidth) * item->mag();
        painter->setPen(Pen(item->curColor(opt), lw, PenStyle::SolidLine, PenCapStyle::FlatCap));
        double x  = lw * .5;
        painter->drawLine(LineF(x, y1, x, y2));

        double lw2 = item->style().styleMM(Sid::endBarWidth) * item->mag();
        painter->setPen(Pen(item->curColor(opt), lw2, PenStyle::SolidLine, PenCapStyle::FlatCap));
        x += ((lw * .5) + item->style().styleMM(Sid::endBarDistance) + (lw2 * .5)) * item->mag();
        painter->drawLine(LineF(x, y1, x, y2));
    }
    break;

    case BarLineType::DOUBLE: {
        double lw = item->style().styleMM(Sid::doubleBarWidth) * item->mag();
        painter->setPen(Pen(item->curColor(opt), lw, PenStyle::SolidLine, PenCapStyle::FlatCap));
        double x = lw * .5;
        painter->drawLine(LineF(x, y1, x, y2));
        x += ((lw * .5) + item->style().styleMM(Sid::doubleBarDistance) + (lw * .5)) * item->mag();
        painter->drawLine(LineF(x, y1, x, y2));
    }
    break;

    case BarLineType::REVERSE_END: {
        double lw = item->style().styleMM(Sid::endBarWidth) * item->mag();
        painter->setPen(Pen(item->curColor(opt), lw, PenStyle::SolidLine, PenCapStyle::FlatCap));
        double x = lw * .5;
        painter->drawLine(LineF(x, y1, x, y2));

        double lw2 = item->style().styleMM(Sid::barWidth) * item->mag();
        painter->setPen(Pen(item->curColor(opt), lw2, PenStyle::SolidLine, PenCapStyle::FlatCap));
        x += ((lw * .5) + item->style().styleMM(Sid::endBarDistance) + (lw2 * .5)) * item->mag();
        painter->drawLine(LineF(x, y1, x, y2));
    }
    break;

    case BarLineType::HEAVY: {
        double lw = item->style().styleMM(Sid::endBarWidth) * item->mag();
        painter->setPen(Pen(item->curColor(opt), lw, PenStyle::SolidLine, PenCapStyle::FlatCap));
        painter->drawLine(LineF(lw * .5, y1, lw * .5, y2));
    }
    break;

    case BarLineType::DOUBLE_HEAVY: {
        double lw2 = item->style().styleMM(Sid::endBarWidth) * item->mag();
        painter->setPen(Pen(item->curColor(opt), lw2, PenStyle::SolidLine, PenCapStyle::FlatCap));
        double x = lw2 * .5;
        painter->drawLine(LineF(x, y1, x, y2));
        x += ((lw2 * .5) + item->style().styleMM(Sid::endBarDistance) + (lw2 * .5)) * item->mag();
        painter->drawLine(LineF(x, y1, x, y2));
    }
    break;

    case BarLineType::START_REPEAT: {
        double lw2 = item->style().styleMM(Sid::endBarWidth) * item->mag();
        painter->setPen(Pen(item->curColor(opt), lw2, PenStyle::SolidLine, PenCapStyle::FlatCap));
        double x = lw2 * .5;
        painter->drawLine(LineF(x, y1, x, y2));

        double lw = item->style().styleMM(Sid::barWidth) * item->mag();
        painter->setPen(Pen(item->curColor(opt), lw, PenStyle::SolidLine, PenCapStyle::FlatCap));
        x += ((lw2 * .5) + item->style().styleMM(Sid::endBarDistance) + (lw * .5)) * item->mag();
        painter->drawLine(LineF(x, y1, x, y2));

        x += ((lw * .5) + item->style().styleMM(Sid::repeatBarlineDotSeparation)) * item->mag();
        drawDots(item, painter, x, dotRows);

        if (item->style().styleB(Sid::repeatBarTips)) {
            drawTips(item, y1, y2, painter, false, 0.0);
        }
    }
    break;

    case BarLineType::END_REPEAT: {
        double lw = item->style().styleMM(Sid::barWidth) * item->mag();
        painter->setPen(Pen(item->curColor(opt), lw, PenStyle::SolidLine, PenCapStyle::FlatCap));

        double x = 0.0;
        drawDots(item, painter, x, dotRows);

        x += item->symBbox(SymId::repeatDot).width();
        x += (item->style().styleMM(Sid::repeatBarlineDotSeparation) + (lw * .5)) * item->mag();
        painter->drawLine(LineF(x, y1, x, y2));

        double lw2 = item->style().styleMM(Sid::endBarWidth) * item->mag();
        x += ((lw * .5) + item->style().styleMM(Sid::endBarDistance) + (lw2 * .5)) * item->mag();
        painter->setPen(Pen(item->curColor(opt), lw2, PenStyle::SolidLine, PenCapStyle::FlatCap));
        painter->drawLine(LineF(x, y1, x, y2));

        if (item->style().styleB(Sid::repeatBarTips)) {
            drawTips(item, y1, y2, painter, true, x + lw2 * .5);
        }
    }
    break;
    case BarLineType::END_START_REPEAT: {
        double lw = item->style().styleMM(Sid::barWidth) * item->mag();
        painter->setPen(Pen(item->curColor(opt), lw, PenStyle::SolidLine, PenCapStyle::FlatCap));

        double x = 0.0;
        drawDots(item, painter, x, dotRows);

        x += item->symBbox(SymId::repeatDot).width();
        x += (item->style().styleMM(Sid::repeatBarlineDotSeparation) + (lw * .5)) * item->mag();
        painter->drawLine(LineF(x, y1, x, y2));

        double lw2 = item->style().styleMM(Sid::endBarWidth) * item->mag();
        x += ((lw * .5) + item->style().styleMM(Sid::endBarDistance) + (lw2 * .5)) * item->mag();
        painter->setPen(Pen(item->curColor(opt), lw2, PenStyle::SolidLine, PenCapStyle::FlatCap));
        painter->drawLine(LineF(x, y1, x, y2));

        if (item->style().styleB(Sid::repeatBarTips)) {
            drawTips(item, y1, y2, painter, true, x + lw2 * .5);
        }

        painter->setPen(Pen(item->curColor(opt), lw, PenStyle::SolidLine, PenCapStyle::FlatCap));
        x  += ((lw2 * .5) + item->style().styleMM(Sid::endBarDistance) + (lw * .5)) * item->mag();
        painter->drawLine(LineF(x, y1, x, y2));

        x += ((lw * .5) + item->style().styleMM(Sid::repeatBarlineDotSeparation)) * item->mag();
        drawDots(item, painter, x, dotRows);

        if (item->style().styleB(Sid::repeatBarTips)) {
            drawTips(item, y1, y2, painter, false, 0.0);
        }
    }
    break;
    }
}

void TDraw::draw(const BarLine* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    const BarLine::LayoutData* data = item->ldata();
    IF_ASSERT_FAILED(data) {
        return;
    }

    painter->save();
    DEFER {
        painter->restore();
    };

    setMask(item, painter);

    // Milestone 8, owner ruling 3b: on a banded JiMStaff every barline form
    // runs continuously from the top band to the bottom band — through the
    // gap, as a keyboard instrument's barlines run between its staves — with
    // repeat dots at each band's middle rows; a stock staff (or a one-band
    // stack) draws exactly as before.
    drawBarLineForm(item, data->y1, data->y2,
                    data->meloBandDotRows.empty() ? nullptr : &data->meloBandDotRows, painter, opt);

    // draw irregular measure mark

    if (opt.isPrinting || !item->score()->markIrregularMeasures()) {
        return;
    }

    const Segment* s = item->segment();
    if (s && (s->isEndBarLineType() || s->isStartRepeatBarLineType())) {
        const Measure* measure = s->measure();
        if (s->isStartRepeatBarLineType()) {
            const Measure* prevMeasure = measure ? measure->prevMeasure() : nullptr;
            if (!prevMeasure) {
                return;
            }
            if (const BarLine* prevEndBl = prevMeasure->endBarLine(item->staffIdx())) {
                if (prevEndBl->segment() && prevEndBl->segment()->enabled()) {
                    return;
                }
            }
            measure = prevMeasure;
        }

        if (measure && measure->isIrregular() && !measure->isMMRest()) {
            painter->setPen(item->configuration()->invisibleColor());
            Font f(u"Edwin", Font::Type::Text);
            f.setPointSizeF(12 * item->spatium() / item->defaultSpatium());
            f.setBold(true);
            Char ch = measure->ticks() > measure->timesig() ? u'+' : u'-';
            RectF r = FontMetrics(f).boundingRect(ch);

            painter->setFont(f);
            painter->drawText(-r.width(), -item->spatium(), ch);
        }
    }
}

void TDraw::draw(const Beam* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    if (item->beamSegments().empty()) {
        return;
    }
    painter->setBrush(Brush(item->curColor(opt)));
    painter->setNoPen();

    // make beam thickness independent of slant
    // (expression can be simplified?)

    const LineF bs = item->beamSegments().front()->line;
    double d  = (std::abs(bs.y2() - bs.y1())) / (bs.x2() - bs.x1());
    if (item->beamSegments().size() > 1 && d > M_PI / 6.0) {
        d = M_PI / 6.0;
    }
    double ww = (item->beamWidth() / 2.0) / sin(M_PI_2 - atan(d));

    for (const BeamSegment* bs1 : item->beamSegments()) {
        painter->drawPolygon(
            PolygonF({
            PointF(bs1->line.x1(), bs1->line.y1() - ww),
            PointF(bs1->line.x2(), bs1->line.y2() - ww),
            PointF(bs1->line.x2(), bs1->line.y2() + ww),
            PointF(bs1->line.x1(), bs1->line.y1() + ww),
        }),
            FillRule::OddEvenFill);
    }
}

void TDraw::draw(const Bend* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    const Bend::LayoutData* data = item->ldata();
    IF_ASSERT_FAILED(data) {
        return;
    }

    double spatium = item->spatium();
    double lw = item->absoluteFromSpatium(item->lineWidth());

    Pen pen(item->curColor(opt), lw, PenStyle::SolidLine, PenCapStyle::RoundCap, PenJoinStyle::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Brush(item->curColor(opt)));

    Font f = item->font(spatium);

    double x  = data->noteWidth + spatium * .2;
    double y  = -spatium * .8;
    double x2, y2;

    double aw = item->style().styleMM(Sid::bendArrowWidth);
    PolygonF arrowUp;
    arrowUp << PointF(0, 0) << PointF(aw * .5, aw) << PointF(-aw * .5, aw);
    PolygonF arrowDown;
    arrowDown << PointF(0, 0) << PointF(aw * .5, -aw) << PointF(-aw * .5, -aw);

    size_t n = item->points().size();
    for (size_t pt = 0; pt < n - 1; ++pt) {
        int pitch = item->points()[pt].pitch;
        if (pt == 0 && pitch) {
            y2 = -data->notePos.y() - spatium * 2;
            x2 = x;
            painter->drawLine(LineF(x, y, x2, y2));

            painter->setBrush(item->curColor(opt));
            painter->drawPolygon(arrowUp.translated(x2, y2));

            int idx = (pitch + 12) / 25;
            const char* l = item->label[idx];
            painter->setFont(f);
            painter->drawText(RectF(x2, y2, .0, .0),
                              muse::draw::AlignHCenter | muse::draw::AlignBottom | muse::draw::TextDontClip,
                              String::fromAscii(l));

            y = y2;
        }
        if (pitch == item->points()[pt + 1].pitch) {
            if (pt == (n - 2)) {
                break;
            }
            x2 = x + spatium;
            y2 = y;
            painter->drawLine(LineF(x, y, x2, y2));
        } else if (pitch < item->points()[pt + 1].pitch) {
            // up
            x2 = x + spatium * .5;
            y2 = -data->notePos.y() - spatium * 2;
            double dx = x2 - x;
            double dy = y2 - y;

            PainterPath path;
            path.moveTo(x, y);
            path.cubicTo(x + dx / 2, y, x2, y + dy / 4, x2, y2);
            painter->setBrush(BrushStyle::NoBrush);
            painter->drawPath(path);

            painter->setBrush(item->curColor(opt));
            painter->drawPolygon(arrowUp.translated(x2, y2));

            int idx = (item->points()[pt + 1].pitch + 12) / 25;
            const char* l = item->label[idx];
            double ty = y2;       // - _spatium;
            painter->setFont(f);
            painter->drawText(RectF(x2, ty, .0, .0),
                              muse::draw::AlignHCenter | muse::draw::AlignBottom | muse::draw::TextDontClip,
                              String::fromAscii(l));
        } else {
            // down
            x2 = x + spatium * .5;
            y2 = y + spatium * 3;
            double dx = x2 - x;
            double dy = y2 - y;

            PainterPath path;
            path.moveTo(x, y);
            path.cubicTo(x + dx / 2, y, x2, y + dy / 4, x2, y2);
            painter->setBrush(BrushStyle::NoBrush);
            painter->drawPath(path);

            painter->setBrush(item->curColor(opt));
            painter->drawPolygon(arrowDown.translated(x2, y2));
        }
        x = x2;
        y = y2;
    }
}

void TDraw::draw(const Box* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    if (opt.isPrinting) {
        return;
    }

    const Box::LayoutData* ldata = item->ldata();

    const bool showHighlightedFrame = item->selected() || item->dropTarget();
    const bool showFrame = showHighlightedFrame || (item->score() ? item->score()->showFrames() : false);

    if (showFrame) {
        double lineWidth = item->defaultSpatium() * .10;
        Pen pen;
        pen.setWidthF(lineWidth);
        pen.setJoinStyle(PenJoinStyle::RoundJoin);
        pen.setCapStyle(PenCapStyle::RoundCap);
        pen.setColor(showHighlightedFrame
                     ? item->configuration()->selectionColor()
                     : item->configuration()->frameColor());
        pen.setDashPattern({ 5, 5 });

        painter->setBrush(BrushStyle::NoBrush);
        painter->setPen(pen);
        lineWidth *= 0.5;
        painter->drawRect(ldata->bbox().adjusted(lineWidth, lineWidth, -lineWidth, -lineWidth));
    }
}

void TDraw::draw(const HBox* item, Painter* painter, const PaintOptions& opt)
{
    draw(static_cast<const Box*>(item), painter, opt);
}

void TDraw::draw(const VBox* item, Painter* painter, const PaintOptions& opt)
{
    draw(static_cast<const Box*>(item), painter, opt);
}

void TDraw::draw(const FBox* item, Painter* painter, const PaintOptions& opt)
{
    draw(static_cast<const Box*>(item), painter, opt);
}

void TDraw::draw(const TBox* item, Painter* painter, const PaintOptions& opt)
{
    draw(static_cast<const Box*>(item), painter, opt);
}

void TDraw::draw(const Bracket* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    const Bracket::LayoutData* ldata = item->ldata();
    IF_ASSERT_FAILED(ldata) {
        return;
    }

    switch (item->bracketType()) {
    case BracketType::BRACE: {
        if (ldata->braceSymbol == SymId::noSym) {
            painter->setNoPen();
            painter->setBrush(Brush(item->curColor(opt)));
            painter->drawPath(ldata->path);
        } else {
            double h = ldata->bracketHeight;
            double glyphHeight = item->symHeight(ldata->braceSymbol);
            double mag = h / glyphHeight;
            painter->setPen(item->curColor(opt));
            painter->save();
            painter->scale(item->magx(), mag);
            item->drawSymbol(ldata->braceSymbol, painter, PointF(0.0, glyphHeight));
            painter->restore();
        }
    }
    break;
    case BracketType::NORMAL: {
        double h = ldata->bracketHeight;
        double spatium = item->spatium();
        double w = item->style().styleMM(Sid::bracketWidth);
        double bd = (item->style().styleSt(Sid::musicalSymbolFont) == "Leland") ? spatium * .5 : spatium * .25;
        Pen pen(item->curColor(opt), w, PenStyle::SolidLine, PenCapStyle::FlatCap);
        painter->setPen(pen);
        painter->drawLine(LineF(0.0, -bd - w * .5, 0.0, h + bd + w * .5));
        double x = -w * .5;
        double y1 = -bd;
        double y2 = h + bd;
        item->drawSymbol(SymId::bracketTop, painter, PointF(x, y1));
        item->drawSymbol(SymId::bracketBottom, painter, PointF(x, y2));
    }
    break;
    case BracketType::SQUARE: {
        double h = ldata->bracketHeight;
        double lineW = item->style().styleMM(Sid::staffLineWidth);
        double bracketWidth = ldata->bracketWidth - lineW / 2;
        Pen pen(item->curColor(opt), lineW, PenStyle::SolidLine, PenCapStyle::FlatCap);
        painter->setPen(pen);
        painter->drawLine(LineF(0.0, 0.0, 0.0, h));
        painter->drawLine(LineF(-lineW / 2, 0.0, lineW / 2 + bracketWidth, 0.0));
        painter->drawLine(LineF(-lineW / 2, h, lineW / 2 + bracketWidth, h));
    }
    break;
    case BracketType::LINE: {
        double h = ldata->bracketHeight;
        double w = 0.67 * item->style().styleMM(Sid::bracketWidth);
        Pen pen(item->curColor(opt), w, PenStyle::SolidLine, PenCapStyle::FlatCap);
        painter->setPen(pen);
        double bd = item->style().styleMM(Sid::staffLineWidth) * 0.5;
        painter->drawLine(LineF(0.0, -bd, 0.0, h + bd));
    }
    break;
    case BracketType::NO_BRACKET:
        break;
    }
}

void TDraw::draw(const Breath* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    painter->setPen(item->curColor(opt));
    item->drawSymbol(item->symId(), painter);
}

void TDraw::draw(const ChordLine* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    const ChordLine::LayoutData* ldata = item->ldata();
    IF_ASSERT_FAILED(ldata) {
        return;
    }

    painter->setPen(Pen(item->curColor(opt), item->style().styleMM(Sid::chordlineThickness) * item->mag(), PenStyle::SolidLine));
    painter->setBrush(BrushStyle::NoBrush);
    if (!item->isWavy()) {
        painter->drawPath(ldata->path);
    } else {
        painter->save();
        painter->rotate((item->chordLineType() == ChordLineType::FALL ? 1 : -1));
        item->drawSymbol(item->waveSym(), painter);
        painter->restore();
    }
}

void TDraw::draw(const Clef* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    const Clef::LayoutData* ldata = item->ldata();
    IF_ASSERT_FAILED(ldata) {
        return;
    }

    if (ldata->symId == SymId::noSym || (item->staff() && !const_cast<const Staff*>(item->staff())->staffType(item->tick())->genClef())) {
        return;
    }

    painter->setPen(item->curColor(opt));
    item->drawSymbol(ldata->symId, painter);
}

void TDraw::draw(const Capo* item, Painter* painter, const PaintOptions& opt)
{
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const DeadSlapped* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    const DeadSlapped::LayoutData* ldata = item->ldata();
    IF_ASSERT_FAILED(ldata) {
        return;
    }

    painter->setPen(PenStyle::NoPen);
    painter->setBrush(item->curColor(opt));
    painter->drawPath(ldata->path1);
    painter->drawPath(ldata->path2);
}

void TDraw::draw(const Dynamic* item, Painter* painter, const PaintOptions& opt)
{
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const Expression* item, Painter* painter, const PaintOptions& opt)
{
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const Fermata* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    painter->setPen(item->curColor(opt));
    item->drawSymbol(item->symId(), painter);
}

void TDraw::draw(const FiguredBass* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    const FiguredBass::LayoutData* ldata = item->ldata();
    // if not printing, draw duration line(s)
    if (!opt.isPrinting && item->score()->showUnprintable()) {
        for (double len : ldata->lineLengths) {
            if (len > 0) {
                painter->setPen(Pen(item->configuration()->invisibleColor(), 3));
                painter->drawLine(0.0, -2, len, -2); // -2: 2 rast. un. above digits
            }
        }
    }

    if (item->items().size() < 1) { // if not parseable into f.b. items
        drawTextBase(item, painter, opt); // draw as standard text
    } else {
        for (FiguredBassItem* fi : item->items()) { // if parseable into f.b. items
            painter->translate(fi->pos()); // draw each item in its proper position
            draw(fi, painter, opt);
            painter->translate(-fi->pos());
        }
    }
}

void TDraw::draw(const FiguredBassItem* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    const FiguredBassItem::LayoutData* ldata = item->ldata();
    int font = 0;
    double _spatium = item->spatium();
    // set font from general style
    Font f(FiguredBass::FBFonts().at(font).family, Font::Type::Tablature);

    // (use the same font selection as used in layout() above)
    double m = item->style().styleD(Sid::figuredBassFontSize) * item->spatium() / item->defaultSpatium();
    f.setPointSizeF(m);

    painter->setFont(f);
    painter->setBrush(BrushStyle::NoBrush);
    Pen pen(item->figuredBass()->curColor(opt), FiguredBass::FB_CONTLINE_THICKNESS * _spatium, PenStyle::SolidLine, PenCapStyle::RoundCap);
    painter->setPen(pen);
    painter->drawText(ldata->bbox(), muse::draw::TextDontClip | muse::draw::AlignLeft | muse::draw::AlignTop, ldata->displayText);

    // continuation line
    double lineEndX = 0.0;
    if (item->contLine() != FiguredBassItem::ContLine::NONE) {
        double lineStartX  = ldata->textWidth;                           // by default, line starts right after text
        if (lineStartX > 0.0) {
            lineStartX += _spatium * FiguredBass::FB_CONTLINE_LEFT_PADDING;          // if some text, give some room after it
        }
        lineEndX = item->figuredBass()->ldata()->printedLineLength;            // by default, line ends with item duration
        if (lineEndX - lineStartX < 1.0) {                         // if line length < 1 sp, ignore it
            lineEndX = 0.0;
        }

        // if extended cont.line and no closing parenthesis: look at next FB element
        if (item->contLine() > FiguredBassItem::ContLine::SIMPLE && item->parenth5() == FiguredBassItem::Parenthesis::NONE) {
            FiguredBass* nextFB;
            // if there is a contiguous FB element
            if ((nextFB = item->figuredBass()->nextFiguredBass()) != 0) {
                // retrieve the X position (in page coords) of a possible cont. line of nextFB
                // on the same line of 'this'
                PointF pgPos = item->pagePos();
                double nextContPageX = nextFB->additionalContLineX(pgPos.y());
                // if an additional cont. line has been found, extend up to its initial X coord
                if (nextContPageX > 0) {
                    lineEndX = nextContPageX - pgPos.x() + _spatium * FiguredBass::FB_CONTLINE_OVERLAP;
                }
                // with a little bit of overlap
                else {
                    lineEndX = item->figuredBass()->ldata()->lineLength(0);                  // if none found, draw to the duration end
                }
            }
        }
        // if some line, draw it
        if (lineEndX > 0.0) {
            double h = ldata->bbox().height() * FiguredBass::FB_CONTLINE_HEIGHT;
            painter->drawLine(lineStartX, h, lineEndX - ldata->pos().x(), h);
        }
    }

    // closing cont.line parenthesis
    if (item->parenth5() != FiguredBassItem::Parenthesis::NONE) {
        int x = lineEndX > 0.0 ? lineEndX : ldata->textWidth;
        painter->drawText(RectF(x, 0, ldata->bbox().width(), ldata->bbox().height()),
                          muse::draw::AlignLeft | muse::draw::AlignTop,
                          Char(FiguredBass::FBFonts().at(font).displayParenthesis[int(item->parenth5())].unicode()));
    }
}

void TDraw::draw(const Fingering* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const FretDiagram* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    const FretDiagram::LayoutData* ldata = item->ldata();
    PointF translation = -PointF(ldata->stringDist * (item->strings() - 1), 0);
    if (item->orientation() == Orientation::HORIZONTAL) {
        painter->save();
        painter->rotate(-90);
        painter->translate(translation);
    }

    // Init pen and other values
    Pen pen(item->curColor(opt));
    pen.setCapStyle(PenCapStyle::FlatCap);
    painter->setBrush(Brush(Color(painter->pen().color())));

    // x2 is the x val of the rightmost string
    double x2 = (item->strings() - 1) * ldata->stringDist;

    // Draw the nut
    pen.setWidthF(ldata->nutLineWidth);
    painter->setPen(pen);
    double nutY = ldata->nutY;
    painter->drawLine(LineF(-ldata->stringLineWidth * .5, nutY, x2 + ldata->stringLineWidth * .5, nutY));

    // Draw strings and frets
    pen.setWidthF(ldata->stringLineWidth);
    painter->setPen(pen);

    // y2 is the y val of the bottom fretline
    double y1 = ldata->stringExtendTop;
    double y2 = ldata->fretDist * item->frets() + 0.5 * ldata->stringLineWidth + ldata->stringExtendBottom;
    for (int i = 0; i < item->strings(); ++i) {
        double x = ldata->stringDist * i;
        painter->drawLine(LineF(x, y1, x, y2));
    }
    for (int i = 1; i <= item->frets(); ++i) {
        double y = ldata->fretDist * i;
        painter->drawLine(LineF(0.0, y, x2, y));
    }

    // dotd is the diameter of a dot
    double dotd = ldata->dotDiameter;

    // Draw dots, sym pen is used to draw them (and markers)
    Pen symPen(pen);
    symPen.setCapStyle(PenCapStyle::FlatCap);
    double symPenWidth = ldata->stringLineWidth * 1.2;
    symPen.setWidthF(symPenWidth);

    for (auto const& i : item->dots()) {
        for (auto const& d : i.second) {
            if (!d.exists()) {
                continue;
            }

            int string = i.first;
            int fret = d.fret - 1;

            // Calculate coords of the top left corner of the dot
            double x = ldata->stringDist * string - dotd * .5;
            double y = ldata->fretDist * fret + ldata->fretDist * .5 - dotd * .5;

            // Draw different symbols
            painter->setPen(symPen);
            switch (d.dtype) {
            case FretDotType::CROSS:
                // Give the cross a slightly larger width
                symPen.setWidthF(symPenWidth * 1.5);
                painter->setPen(symPen);
                painter->drawLine(LineF(x, y, x + dotd, y + dotd));
                painter->drawLine(LineF(x + dotd, y, x, y + dotd));
                symPen.setWidthF(symPenWidth);
                break;
            case FretDotType::SQUARE:
                painter->setBrush(BrushStyle::NoBrush);
                painter->drawRect(RectF(x, y, dotd, dotd));
                break;
            case FretDotType::TRIANGLE:
                painter->drawLine(LineF(x, y + dotd, x + .5 * dotd, y));
                painter->drawLine(LineF(x + .5 * dotd, y, x + dotd, y + dotd));
                painter->drawLine(LineF(x + dotd, y + dotd, x, y + dotd));
                break;
            case FretDotType::NORMAL:
            default:
                painter->setBrush(symPen.color());
                painter->setNoPen();
                painter->drawEllipse(RectF(x, y, dotd, dotd));
                break;
            }
        }
    }

    // Draw markers
    symPen.setWidthF(ldata->stringLineWidth * 1.2);
    painter->setBrush(BrushStyle::NoBrush);
    painter->setPen(symPen);
    for (auto const& i : item->markers()) {
        int string = i.first;
        FretItem::Marker marker = i.second;
        if (!marker.exists()) {
            continue;
        }

        double x = ldata->stringDist * string - ldata->markerSize * .5;
        double y = ldata->markerY;
        if (marker.mtype == FretMarkerType::CIRCLE) {
            painter->drawEllipse(RectF(x, y, ldata->markerSize, ldata->markerSize));
        } else if (marker.mtype == FretMarkerType::CROSS) {
            painter->drawLine(PointF(x, y), PointF(x + ldata->markerSize, y + ldata->markerSize));
            painter->drawLine(PointF(x, y + ldata->markerSize), PointF(x + ldata->markerSize, y));
        }
    }

    // Draw barres
    for (auto const& i : item->barres()) {
        int fret        = i.first;
        int startString = i.second.startString;
        int endString   = i.second.endString;

        double x1    = ldata->stringDist * startString;
        double newX2 = endString == -1 ? x2 : ldata->stringDist * endString;
        double y     = ldata->fretDist * (fret - 1) + ldata->fretDist * .5;
        if (item->style().styleB(Sid::barreAppearanceSlur)) {
            pen.setWidthF(0.25 * ldata->stringLineWidth);
            pen.setCapStyle(PenCapStyle::RoundCap);
            pen.setJoinStyle(PenJoinStyle::RoundJoin);
            painter->setPen(pen);
            painter->setBrush(Brush(pen.color()));
            for (const PainterPath& path : ldata->slurPaths) {
                painter->drawPath(path);
            }
        } else {
            pen.setWidthF(dotd * item->style().styleD(Sid::barreLineWidth));
            pen.setCapStyle(PenCapStyle::RoundCap);
            painter->setPen(pen);
            painter->drawLine(LineF(x1, y, newX2, y));
        }
    }

    // Draw fret offset number
    if (item->fretOffset() > 0) {
        Font scaledFont(item->fretNumFont());
        scaledFont.setPointSizeF(scaledFont.pointSizeF() * (item->spatium() / item->defaultSpatium()));
        painter->setFont(scaledFont);
        String text = ldata->fretText;

        if (item->orientation() == Orientation::VERTICAL) {
            if (item->numPos() == 0) {
                painter->drawText(RectF(-ldata->fretNumPadding, .0, .0, ldata->fretDist),
                                  muse::draw::AlignVCenter | muse::draw::AlignRight | muse::draw::TextDontClip, text);
            } else {
                painter->drawText(RectF(x2 + ldata->fretNumPadding, .0, .0, ldata->fretDist),
                                  muse::draw::AlignVCenter | muse::draw::AlignLeft | muse::draw::TextDontClip,
                                  text);
            }
        } else if (item->orientation() == Orientation::HORIZONTAL) {
            painter->save();
            painter->translate(-translation);
            painter->rotate(90);
            if (item->numPos() == 0) {
                painter->drawText(RectF(.0, ldata->stringDist * (item->strings() - 1), .0, .0),
                                  muse::draw::AlignLeft | muse::draw::TextDontClip, text);
            } else {
                painter->drawText(RectF(.0, .0, .0, .0),
                                  muse::draw::AlignBottom | muse::draw::AlignLeft | muse::draw::TextDontClip, text);
            }
            painter->restore();
        }
        painter->setFont(item->fretNumFont());
    }

    for (const FretDiagram::FingeringItem& fingeringItem : item->ldata()->fingeringItems) {
        painter->save();

        Font scaledFont(item->fingeringFont());
        scaledFont.setPointSizeF(scaledFont.pointSizeF() * (item->spatium() / item->defaultSpatium()));
        painter->setFont(scaledFont);
        if (item->orientation() == Orientation::HORIZONTAL) {
            painter->translate(-translation);
            painter->rotate(90);
        }

        painter->drawText(fingeringItem.pos, fingeringItem.fingerNumber);

        painter->restore();
    }

    if (item->orientation() == Orientation::HORIZONTAL) {
        painter->restore();
    }
}

static void setDashAndGapLen(const SLine* line, double& dash, double& gap, Pen& pen)
{
    static constexpr double DOTTED_DASH_LEN = 0.01;
    static constexpr double DOTTED_GAP_LEN = 1.99;
    switch (line->lineStyle()) {
    case LineType::SOLID:
        break;
    case LineType::DASHED:
        dash = line->dashLineLen(), gap = line->dashGapLen();
        break;
    case LineType::DOTTED:
        dash = DOTTED_DASH_LEN, gap = DOTTED_GAP_LEN;
        pen.setCapStyle(PenCapStyle::RoundCap); // round dots
        break;
    }
}

static std::vector<double> distributedDashPattern(double dash, double gap, double lineLength)
{
    int numPairs = std::max(1.0, lineLength / (dash + gap));
    double newGap = (lineLength - dash * (numPairs + 1)) / numPairs;

    return { dash, newGap };
}

void TDraw::draw(const GlissandoSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    if (item->pos2().x() <= 0) {
        return;
    }

    painter->save();
    double _spatium = item->spatium();
    const Glissando* glissando = item->glissando();

    Pen pen(item->curColor(item->getProperty(Pid::VISIBLE).toBool(), item->getProperty(Pid::COLOR).value<Color>(), opt));
    pen.setWidthF(item->absoluteFromSpatium(item->lineWidth()));
    pen.setCapStyle(PenCapStyle::FlatCap);
    painter->setPen(pen);

    // rotate painter so that the line become horizontal
    double w     = item->pos2().x();
    double h     = item->pos2().y();
    double l     = sqrt(w * w + h * h);
    double wi    = asin(-h / l) * 180.0 / M_PI;
    painter->rotate(-wi);

    if (glissando->glissandoType() == GlissandoType::STRAIGHT) {
        const bool isNonSolid = glissando->lineStyle() != LineType::SOLID;
        if (isNonSolid) {
            double lineWidth = glissando->absoluteFromSpatium(glissando->lineWidth());
            double dash = 0;
            double gap = 0;
            setDashAndGapLen(glissando, dash, gap, pen);
            pen.setDashPattern(distributedDashPattern(dash, gap, l / lineWidth));
            painter->setPen(pen);
        }

        painter->drawLine(LineF(0.0, 0.0, l, 0.0));
    } else if (glissando->glissandoType() == GlissandoType::WAVY) {
        RectF b = item->symBbox(SymId::wiggleGlissando);
        double a  = item->symAdvance(SymId::wiggleGlissando);
        int n    = static_cast<int>(l / a);          // always round down (truncate) to avoid overlap
        double x  = (l - n * a) * 0.5;     // centre line in available space
        SymIdList ids;
        for (int i = 0; i < n; ++i) {
            ids.push_back(SymId::wiggleGlissando);
        }

        item->score()->engravingFont()->draw(ids, painter, item->magS(), PointF(x, -(b.y() + b.height() * 0.5)));
    }

    if (glissando->showText()) {
        Font f(glissando->fontFace(), Font::Type::Unknown);
        f.setPointSizeF(glissando->fontSize() * _spatium / item->defaultSpatium());
        f.setBold(glissando->fontStyle() & FontStyle::Bold);
        f.setItalic(glissando->fontStyle() & FontStyle::Italic);
        f.setUnderline(glissando->fontStyle() & FontStyle::Underline);
        f.setStrike(glissando->fontStyle() & FontStyle::Strike);
        FontMetrics fm(f);
        RectF r = fm.boundingRect(glissando->text());

        // if text longer than available space, skip it
        if (r.width() < l) {
            double x = (l - r.width()) * 0.5;
            double yOffset = r.height() + r.y(); // find text descender height
            // raise text slightly above line and slightly more with WAVY than with STRAIGHT
            yOffset += _spatium * (glissando->glissandoType() == GlissandoType::WAVY ? 0.4 : 0.1);

            painter->setFont(f);
            painter->drawText(PointF(x, -yOffset), glissando->text());
        }
    }
    painter->restore();
}

void TDraw::draw(const GuitarBendSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    bool tabStaff = item->staff()->isTabStaff(item->tick());
    GuitarBend* bend = item->guitarBend();

    Pen pen(item->curColor(opt));
    if (bend->bendType() == GuitarBendType::SCOOP) {
        painter->setPen(pen);
        item->drawSymbol(SymId::guitarVibratoBarScoop, painter);
        return;
    }

    pen.setWidthF(item->lineWidth());
    pen.setCapStyle(PenCapStyle::FlatCap);
    pen.setJoinStyle(bend->bendType() == GuitarBendType::DIP ? PenJoinStyle::BevelJoin : PenJoinStyle::MiterJoin);
    pen.setColor(item->curColor(opt));
    if (tabStaff && bend->bendType() == GuitarBendType::PRE_DIVE) {
        const PainterPath& path = item->ldata()->path();
        if (path.elementCount() > 1) {
            double lineLength = std::abs(item->ldata()->path().elementAt(1).y);
            pen.setDashPattern(distributedDashPattern(3, 3, lineLength / pen.widthF()));
        } else {
            pen.setDashPattern({ 3, 3 });
        }
    }
    painter->setPen(pen);

    Brush brush;
    brush.setStyle(BrushStyle::NoBrush);
    painter->setBrush(brush);

    painter->drawPath(item->ldata()->path());

    if (tabStaff) {
        brush.setStyle(BrushStyle::SolidPattern);
        brush.setColor(item->curColor(opt));
        painter->setBrush(brush);
        painter->setNoPen();
        painter->drawPolygon(item->ldata()->arrow());
    }
}

void TDraw::draw(const GuitarBendHoldSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    Pen pen(item->curColor(item->visible(), opt));
    if (!item->ldata()->symIds().empty()) {
        painter->setPen(pen);
        item->drawSymbols(item->ldata()->symIds(), painter);
        return;
    }

    pen.setWidthF(item->lineWidth());
    pen.setCapStyle(PenCapStyle::FlatCap);

    switch (item->getProperty(Pid::LINE_STYLE).value<LineType>()) {
    case LineType::DASHED:
    {
        double dash = item->dashLength();
        pen.setDashPattern(distributedDashPattern(dash, dash, item->pos2().x() / pen.widthF()));
        break;
    }
    case LineType::DOTTED:
        pen.setCapStyle(PenCapStyle::RoundCap);           // True dots
        pen.setDashPattern({ 0.01, 1.99 });
        break;
    default:
        break;
    }

    pen.setJoinStyle(PenJoinStyle::MiterJoin);
    painter->setPen(pen);

    painter->drawLine(PointF(), item->pos2());
}

void TDraw::drawTextBase(const TextBase* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    const TextBase::LayoutData* ldata = item->ldata();
    if (item->hasFrame()) {
        double baseSpatium = DefaultStyle::baseStyle().value(Sid::spatium).toReal();
        if (!RealIsNull(item->frameWidth().val())) {
            Color fColor = item->curColor(item->visible(), item->frameColor(), opt);
            double frameWidthVal = item->frameWidth().val() * (item->sizeIsSpatiumDependent() ? item->spatium() : baseSpatium);

            Pen pen(fColor, frameWidthVal, PenStyle::SolidLine, PenCapStyle::SquareCap, PenJoinStyle::MiterJoin);
            painter->setPen(pen);
        } else {
            painter->setNoPen();
        }
        Color bg(item->bgColor());
        painter->setBrush(bg.alpha() ? Brush(bg) : BrushStyle::NoBrush);
        if (item->circle()) {
            painter->drawEllipse(ldata->frame);
        } else {
            double frameRadius = item->frameRound().val() * (item->sizeIsSpatiumDependent() ? item->spatium() : baseSpatium);
            painter->drawRoundedRect(ldata->frame, frameRadius, frameRadius);
        }
    }
    painter->setBrush(BrushStyle::NoBrush);
    painter->setPen(item->textColor(opt));
    for (const TextBlock& t : ldata->blocks) {
        draw(t, item, painter);
    }
}

void TDraw::draw(const TextBlock& textBlock, const TextBase* item, Painter* painter)
{
    painter->translate(0.0, textBlock.y());
    for (const TextFragment& f : textBlock.fragments()) {
        draw(f, item, painter);
    }
    painter->translate(0.0, -textBlock.y());
}

void TDraw::draw(const TextFragment& textFragment, const TextBase* item, muse::draw::Painter* painter)
{
#ifndef Q_OS_MACOS
    drawTextWorkaround(textFragment, item, painter);
    return;
#endif
    painter->setFont(textFragment.font(item));
    painter->drawText(textFragment.pos, textFragment.text);
}

void TDraw::drawTextWorkaround(const TextFragment& textFragment, const TextBase* item, muse::draw::Painter* painter)
{
    Font f = textFragment.font(item);
    const String& text = textFragment.text;
    const PointF& pos = textFragment.pos;

    painter->setFont(f);

    double mm = painter->worldTransform().m11();
    bool useWorkaround = !(MScore::pdfPrinting) && (mm < 1.0) && f.bold() && !(f.underline() || f.strike());
    if (!useWorkaround) {
        painter->drawText(pos, text);
        return;
    }

    painter->drawTextWorkaround(pos, text);
}

void TDraw::drawTextLineBaseSegment(const TextLineBaseSegment* item, Painter* painter, const PaintOptions& opt)
{
    const TextLineBase* tl = item->textLineBase();
    const TextLineBaseSegment::LayoutData* ldata = item->ldata();

    if (!item->text()->empty()) {
        painter->translate(item->text()->pos());
        item->text()->setVisible(tl->visible());
        draw(item->text(), painter, opt);
        painter->translate(-item->text()->pos());
    }

    if (!item->endText()->empty()) {
        painter->translate(item->endText()->pos());
        item->endText()->setVisible(tl->visible());
        draw(item->endText(), painter, opt);
        painter->translate(-item->endText()->pos());
    }

    if (ldata->npoints == 0
        || ((opt.isPrinting || (item->score() && !item->score()->isShowInvisible()))
            && !tl->lineVisible())) {
        return;
    }

    // color for line (text color comes from the text properties)
    Color color = item->curColor(tl->visible() && tl->lineVisible(), tl->lineColor(), opt);

    double lineWidth = tl->absoluteFromSpatium(tl->lineWidth());

    const Pen solidPen(color, lineWidth, PenStyle::SolidLine, PenCapStyle::FlatCap, PenJoinStyle::MiterJoin);
    Pen pen(solidPen);

    double dash = 0;
    double gap = 0;

    setDashAndGapLen(tl, dash, gap, pen);

    const bool isNonSolid = tl->lineStyle() != LineType::SOLID;

    // Draw lines
    if (item->twoLines()) { // hairpins
        if (isNonSolid) {
            pen.setDashPattern({ dash, gap });
        }

        pen.setJoinStyle(PenJoinStyle::BevelJoin);
        painter->setPen(pen);
        if (!ldata->joinedHairpin.empty() && !isNonSolid) {
            painter->drawPolyline(ldata->joinedHairpin);
        } else {
            painter->drawLines(&ldata->points[0], 2);
        }
        return;
    }

    int start = 0, end = ldata->npoints;

    // Draw begin hook, if it needs to be drawn separately
    if (item->isSingleBeginType() && tl->beginHookType() != HookType::NONE) {
        if (tl->beginHookType() == HookType::ARROW_FILLED) {
            Brush brush;
            brush.setStyle(BrushStyle::SolidPattern);
            brush.setColor(color);
            painter->setBrush(brush);
            painter->setNoPen();
            painter->drawPolygon(ldata->beginArrow);
        } else if (tl->beginHookType() == HookType::ARROW) {
            pen.setJoinStyle(PenJoinStyle::MiterJoin);
            painter->setPen(pen);
            painter->drawPolyline(ldata->beginArrow);
        } else {
            bool isTHook = tl->beginHookType() == HookType::HOOK_90T;

            if (isNonSolid || isTHook) {
                const PointF& p1 = ldata->points[start++];
                const PointF& p2 = ldata->points[start++];

                if (isTHook) {
                    painter->setPen(solidPen);
                } else {
                    double hookLength = sqrt(PointF::dotProduct(p2 - p1, p2 - p1));
                    pen.setDashPattern(distributedDashPattern(dash, gap, hookLength / lineWidth));
                    painter->setPen(pen);
                }

                painter->drawLine(p1, p2);
            }
        }
    }

    // Draw end hook, if it needs to be drawn separately
    if (item->isSingleEndType() && tl->endHookType() != HookType::NONE) {
        if (tl->endHookType() == HookType::ARROW_FILLED) {
            Brush brush;
            brush.setStyle(BrushStyle::SolidPattern);
            brush.setColor(color);
            painter->setBrush(brush);
            painter->setNoPen();
            painter->drawPolygon(ldata->endArrow);
        } else if (tl->endHookType() == HookType::ARROW) {
            pen.setJoinStyle(PenJoinStyle::MiterJoin);
            painter->setPen(pen);
            painter->drawPolyline(ldata->endArrow);
        } else {
            bool isTHook = tl->endHookType() == HookType::HOOK_90T;

            if (isNonSolid || isTHook) {
                const PointF& p1 = ldata->points[--end];
                const PointF& p2 = ldata->points[--end];

                if (isTHook) {
                    painter->setPen(solidPen);
                } else {
                    double hookLength = sqrt(PointF::dotProduct(p2 - p1, p2 - p1));
                    pen.setDashPattern(distributedDashPattern(dash, gap, hookLength / lineWidth));
                    painter->setPen(pen);
                }

                painter->drawLine(p1, p2);
            }
        }
    }

    // Draw the rest
    if (isNonSolid) {
        pen.setDashPattern(distributedDashPattern(dash, gap, ldata->lineLength / lineWidth));
    }

    painter->setPen(pen);
    painter->drawPolyline(&ldata->points[start], end - start);
}

void TDraw::draw(const GradualTempoChangeSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextLineBaseSegment(item, painter, opt);
}

void TDraw::draw(const HairpinSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    drawTextLineBaseSegment(item, painter, opt);

    if (item->drawCircledTip()) {
        Color color = item->curColor(item->hairpin()->visible(), item->hairpin()->lineColor(), opt);
        double w = item->absoluteFromSpatium(item->lineWidth());
        if (item->staff()) {
            w *= item->staff()->staffMag(item->hairpin()->tick());
        }

        Pen pen(color, w);
        painter->setPen(pen);
        painter->setBrush(BrushStyle::NoBrush);
        painter->drawEllipse(item->circledTip(), item->circledTipRadius(), item->circledTipRadius());
    }
}

void TDraw::draw(const HammerOnPullOffSegment* item, muse::draw::Painter* painter, const PaintOptions& opt)
{
    draw(toSlurSegment(item), painter, opt);
}

void TDraw::draw(const HammerOnPullOffText* item, muse::draw::Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const HarpPedalDiagram* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const HarmonicMarkSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextLineBaseSegment(item, painter, opt);
}

void TDraw::draw(const Harmony* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    const Harmony::LayoutData* ldata = item->ldata();

    if (ldata->renderItemList().empty()) {
        drawTextBase(item, painter, opt);
        return;
    }

    if (item->hasFrame()) {
        if (!RealIsNull(item->frameWidth().val())) {
            Color color = item->frameColor();
            Pen pen(color, item->frameWidth().val() * item->spatium(), PenStyle::SolidLine,
                    PenCapStyle::SquareCap, PenJoinStyle::MiterJoin);
            painter->setPen(pen);
        } else {
            painter->setNoPen();
        }
        Color bg(item->bgColor());
        painter->setBrush(bg.alpha() ? Brush(bg) : BrushStyle::NoBrush);
        if (item->circle()) {
            painter->drawArc(ldata->frame, 0, 5760);
        } else {
            double baseSpatium = DefaultStyle::baseStyle().value(Sid::spatium).toReal();
            double frameRadius = item->frameRound().val() * (item->sizeIsSpatiumDependent() ? item->spatium() : baseSpatium);
            painter->drawRoundedRect(ldata->frame, frameRadius, frameRadius);
        }
    }
    painter->setBrush(BrushStyle::NoBrush);
    Color color = item->textColor(opt);
    painter->setPen(color);
    for (const HarmonyRenderItem* renderItem : ldata->renderItemList()) {
        if (const TextSegment* ts = dynamic_cast<const TextSegment*>(renderItem)) {
            painter->setFont(ts->font());
            painter->drawText(ts->pos(), ts->text());
        } else if (const ChordSymbolParen* parenItem = dynamic_cast<const ChordSymbolParen*>(renderItem)) {
            Parenthesis* p = parenItem->parenItem;
            painter->translate(parenItem->pos());
            draw(p, painter, opt);
            painter->translate(-parenItem->pos());
        }
    }

    if (item->isPolychord()) {
        Pen pen(painter->pen());
        pen.setWidthF(item->style().styleS(Sid::polychordDividerThickness).toMM(item->spatium()));
        pen.setColor(color);
        painter->setPen(pen);
        for (const LineF& line : ldata->polychordDividerLines()) {
            painter->drawLine(line.translated(PointF(0.0, ldata->polychordDividerOffset)));
        }
    }
}

void TDraw::draw(const Hook* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    // hide if belonging to the second chord of a cross-measure pair
    if (item->chord() && item->chord()->crossMeasure() == CrossMeasure::SECOND) {
        return;
    }

    painter->setPen(item->curColor(opt));
    item->drawSymbol(item->sym(), painter);
}

void TDraw::draw(const Image* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    const Image::LayoutData* ldata = item->ldata();
    bool emptyImage = false;
    if (item->imageType() == ImageType::SVG) {
        if (!item->svgRenderer()) {
            emptyImage = true;
        } else {
            item->svgRenderer()->render(painter, ldata->bbox());
        }
    } else if (item->imageType() == ImageType::RASTER) {
        if (item->rasterImage() == nullptr) {
            emptyImage = true;
        } else {
            painter->save();
            SizeF s;
            if (item->sizeIsSpatium()) {
                s = item->size() * item->spatium();
            } else {
                s = item->size() * DPMM;
            }
            Transform t = painter->worldTransform();
            muse::Size ss = muse::Size(s.width() * t.m11(), s.height() * t.m22());
            int maxDim = item->configuration()->maxScaledImageDim();
            bool useDirectDraw = (opt.isPrinting && !MScore::svgPrinting)
                                 || (maxDim > 0 && std::max(ss.width(), ss.height()) > maxDim);

            if (useDirectDraw) {
                painter->scale(s.width() / item->rasterImage()->width(), s.height() / item->rasterImage()->height());
                painter->drawPixmap(PointF(0, 0), *item->rasterImage());
            } else {
                t.setMatrix(1.0, t.m12(), t.m13(), t.m21(), 1.0, t.m23(), t.m31(), t.m32(), t.m33());
                painter->setWorldTransform(t);
                if ((item->buffer().size() != ss || item->dirty()) && item->rasterImage() && !item->rasterImage()->isNull()) {
                    item->setBuffer(item->imageProvider()->scaled(*item->rasterImage(), ss));
                    item->setDirty(false);
                }
                if (item->buffer().isNull()) {
                    emptyImage = true;
                } else {
                    painter->drawPixmap(PointF(0.0, 0.0), item->buffer());
                }
            }
            painter->restore();
        }
    }

    if (emptyImage) {
        painter->setBrush(BrushStyle::NoBrush);
        painter->setPen(item->configuration()->defaultColor());
        painter->drawRect(ldata->bbox());
        painter->drawLine(0.0, 0.0, ldata->bbox().width(), ldata->bbox().height());
        painter->drawLine(ldata->bbox().width(), 0.0, 0.0, ldata->bbox().height());
    }
    if (item->selected() && !opt.isPrinting) {
        painter->setBrush(BrushStyle::NoBrush);
        painter->setPen(item->configuration()->selectionColor());
        painter->drawRect(ldata->bbox());
    }
}

void TDraw::draw(const InstrumentChange* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const InstrumentName* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const Jump* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const KeySig* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    const KeySig::LayoutData* ldata = item->ldata();

    painter->setPen(item->curColor(opt));
    double _spatium = item->spatium();
    double step = _spatium * (item->staff() ? item->staff()->staffTypeForElement(item)->lineDistance().val() * 0.5 : 0.5);
    int lines = item->staff() ? item->staff()->staffTypeForElement(item)->lines() : 5;
    double ledgerLineWidth = item->style().styleMM(Sid::ledgerLineWidth) * item->mag();
    double ledgerExtraLen = item->style().styleS(Sid::ledgerLineLength).val() * _spatium;
    for (const KeySym& ks : ldata->keySymbols) {
        double x = ks.xPos * _spatium;
        double y = ks.line * step;
        item->drawSymbol(ks.sym, painter, PointF(x, y));
        // ledger lines
        double _symWidth = item->symWidth(ks.sym);
        double x1 = x - ledgerExtraLen;
        double x2 = x + _symWidth + ledgerExtraLen;
        painter->setPen(Pen(item->curColor(opt), ledgerLineWidth, PenStyle::SolidLine, PenCapStyle::FlatCap));
        for (int i = -2; i >= ks.line; i -= 2) { // above
            y = i * step;
            painter->drawLine(LineF(x1, y, x2, y));
        }
        for (int i = lines * 2; i <= ks.line; i += 2) { // below
            y = i * step;
            painter->drawLine(LineF(x1, y, x2, y));
        }
    }
}

void TDraw::draw(const LaissezVibSegment* item, muse::draw::Painter* painter, const PaintOptions& opt)
{
    const LaissezVibSegment::LayoutData* ldata = item->ldata();
    if (item->score()->style().styleB(Sid::laissezVibUseSmuflSym)) {
        painter->setPen(item->curColor(opt));
        item->drawSymbol(ldata->symbol, painter);
    } else {
        draw(static_cast<const TieSegment*>(item), painter, opt);
    }
}

void TDraw::draw(const Lasso* item, Painter* painter, const PaintOptions&)
{
    TRACE_DRAW_ITEM;
    const Lasso::LayoutData* ldata = item->ldata();
    painter->setBrush(Brush(item->configuration()->lassoColor()));
    // always 2 pixel width
    double w = 2.0 / painter->worldTransform().m11() * item->configuration()->guiScaling();
    painter->setPen(Pen(item->configuration()->selectionColor(), w));
    painter->drawRect(ldata->bbox());
}

void TDraw::draw(const LayoutBreak* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    if (opt.isPrinting || !item->score()->showUnprintable()) {
        return;
    }

    Pen pen(item->selected() ? item->configuration()->selectionColor() : item->configuration()->formattingColor());
    painter->setPen(pen);
    painter->setFont(item->font());
    painter->drawSymbol(PointF(), item->iconCode());
}

void TDraw::draw(const LedgerLine* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    if (item->chord()->crossMeasure() == CrossMeasure::SECOND) {
        return;
    }

    const LedgerLine::LayoutData* ldata = item->ldata();

    painter->setPen(Pen(item->curColor(opt), ldata->lineWidth, PenStyle::SolidLine, PenCapStyle::FlatCap));
    if (item->vertical()) {
        painter->drawLine(LineF(0.0, 0.0, 0.0, item->len()));
    } else {
        painter->drawLine(LineF(0.0, 0.0, item->len(), 0.0));
    }
}

void TDraw::draw(const LetRingSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextLineBaseSegment(item, painter, opt);
}

void TDraw::draw(const Lyrics* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const LyricsLineSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    Pen pen(item->curColor(opt));
    pen.setWidthF(item->absoluteFromSpatium(item->lineWidth()));
    pen.setCapStyle(PenCapStyle::FlatCap);
    painter->setPen(pen);
    for (const LineF& dash : item->ldata()->dashes()) {
        painter->drawLine(dash);
    }
}

void TDraw::draw(const Marker* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const MeasureNumber* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const MeasureRepeat* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    const MeasureRepeat::LayoutData* ldata = item->ldata();

    painter->setPen(item->curColor(opt));
    item->drawSymbol(ldata->symId, painter);

    if (!ldata->numberSym.empty()) {
        PointF numberPos = item->numberPosition(item->symBbox(ldata->numberSym));
        item->drawSymbols(ldata->numberSym, painter, numberPos);
    }

    if (item->style().styleB(Sid::fourMeasureRepeatShowExtenders) && item->numMeasures() == 4) {
        double hBarThickness = item->style().styleMM(Sid::mmRestHBarThickness);
        Pen pen(painter->pen());
        pen.setCapStyle(PenCapStyle::FlatCap);
        pen.setWidthF(hBarThickness);
        painter->setPen(pen);

        painter->drawLine(ldata->extenderLineLeft);
        painter->drawLine(ldata->extenderLineRight);
    }
}

void TDraw::draw(const MMRest* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    if (item->shouldNotBeDrawn() || (item->track() % VOICES)) {     //only on voice 1
        return;
    }

    const MMRest::LayoutData* ldata = item->ldata();

    double _spatium = item->spatium();

    // draw number
    painter->setPen(item->curColor(opt));
    RectF numberBox = item->symBbox(ldata->numberSym);
    PointF numberPos = item->numberPos();
    if (item->showNumber()) {
        item->drawSymbols(ldata->numberSym, painter, numberPos);
    }

    numberBox.translate(numberPos);

    if (item->isOldStyle()) {
        // draw rest symbols
        double x = (ldata->restWidth - ldata->symsWidth) * 0.5;
        double spacing = item->style().styleMM(Sid::mmRestOldStyleSpacing);
        for (SymId sym : ldata->restSyms) {
            double y = (sym == SymId::restWhole ? -_spatium : 0);
            item->drawSymbol(sym, painter, PointF(x, y));
            x += item->symBbox(sym).width() + spacing;
        }
    } else {
        double mag = item->staff()->staffMag(item->tick());
        Pen pen(painter->pen());
        pen.setCapStyle(PenCapStyle::FlatCap);

        // draw horizontal line
        double hBarThickness = item->style().styleMM(Sid::mmRestHBarThickness) * mag;
        if (hBarThickness) { // don't draw at all if 0, QPainter interprets 0 pen width differently
            pen.setWidthF(hBarThickness);
            painter->setPen(pen);
            double halfHBarThickness = hBarThickness * .5;
            if (item->showNumber() // avoid painting line through number
                && item->style().styleB(Sid::mmRestNumberMaskHBar)
                && numberBox.bottom() >= -halfHBarThickness
                && numberBox.top() <= halfHBarThickness) {
                double gapDistance = (numberBox.width() + _spatium) * .5;
                double midpoint = ldata->restWidth * .5;
                painter->drawLine(LineF(0.0, 0.0, midpoint - gapDistance, 0.0));
                painter->drawLine(LineF(midpoint + gapDistance, 0.0, ldata->restWidth, 0.0));
            } else {
                painter->drawLine(LineF(0.0, 0.0, ldata->restWidth, 0.0));
            }
        }

        // draw vertical lines
        double vStrokeThickness = item->style().styleMM(Sid::mmRestHBarVStrokeThickness) * mag;
        if (vStrokeThickness) { // don't draw at all if 0, QPainter interprets 0 pen width differently
            pen.setWidthF(vStrokeThickness);
            painter->setPen(pen);
            double halfVStrokeHeight = item->style().styleMM(Sid::mmRestHBarVStrokeHeight) * .5 * mag;
            painter->drawLine(LineF(0.0, -halfVStrokeHeight, 0.0, halfVStrokeHeight));
            painter->drawLine(LineF(ldata->restWidth, -halfVStrokeHeight, ldata->restWidth, halfVStrokeHeight));
        }
    }
}

void TDraw::draw(const MMRestRange* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const Note* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    if (item->hidden()) {
        return;
    }

    const Note::LayoutData* ldata = item->ldata();

    const auto config = item->configuration();
    const StaffType* staffType = item->staff() ? item->staff()->staffTypeForElement(item) : nullptr;

    const bool isTabStaff = staffType && staffType->isTabStaff();
    const bool negativeFret = isTabStaff && item->negativeFretUsed();
    const bool useCriticalColor = negativeFret && !item->deadNote() && opt.isPrinting;

    painter->setPen(useCriticalColor ? config->criticalColor() : item->curColor(opt));

    // tablature
    if (isTabStaff) {
        if (item->displayFret() == Note::DisplayFretOption::Hide || item->shouldHideFret()) {
            return;
        }
        const Staff* st = item->staff();
        const StaffType* tab = st->staffTypeForElement(item);

        if (negativeFret || (item->fretConflict() && !opt.isPrinting && item->score()->showUnprintable())) { // fret conflict
            painter->save();
            painter->setPen(config->criticalColor());
            painter->setBrush(config->criticalBackgroundColor());
            painter->drawRect(ldata->bbox());
            painter->restore();
        }

        Font f(tab->fretFont());
        f.setPointSizeF(f.pointSizeF() * item->magS());
        painter->setFont(f);

        const double startPosX = ldata->bbox().x();
        const double yOffset = tab->fretFontYOffset();
        painter->drawText(PointF(startPosX, yOffset * item->magS()), item->fretString());
    }
    // NOT tablature
    else {
        // skip drawing, if second note of a cross-measure value
        if (item->chord() && item->chord()->crossMeasure() == CrossMeasure::SECOND) {
            return;
        }
        // warn if pitch extends usable range of instrument
        // by coloring the notehead
        if (item->chord() && item->chord()->segment() && item->staff() && !opt.isPrinting
            && MScore::warnPitchRange && !item->staff()->isDrumStaff(item->chord()->tick())) {
            const Instrument* in = item->part()->instrument(item->chord()->tick());
            int i = item->ppitch();
            if (i < in->minPitchP() || i > in->maxPitchP()) {
                painter->setPen(item->selected() ? config->criticalSelectedColor() : config->criticalColor());
            } else if (i < in->minPitchA() || i > in->maxPitchA()) {
                painter->setPen(item->selected() ? config->warningSelectedColor() : config->warningColor());
            }
        }
        // Warn if notes are unplayable based on previous harp diagram setting
        if (item->chord() && item->chord()->segment() && item->staff() && !opt.isPrinting
            && !item->staff()->isDrumStaff(item->chord()->tick())) {
            HarpPedalDiagram* prevDiagram = item->part()->currentHarpDiagram(item->chord()->segment()->tick());
            if (prevDiagram && !prevDiagram->isTpcPlayable(item->tpc())) {
                painter->setPen(item->selected() ? config->criticalSelectedColor() : config->criticalColor());
            }
        }
        // draw blank notehead to avoid staff and ledger lines
        if (ldata->cachedSymNull.value() != SymId::noSym) {
            painter->save();
            painter->setPen(config->noteBackgroundColor());
            item->drawSymbol(ldata->cachedSymNull.value(), painter);
            painter->restore();
        }
        item->drawSymbol(ldata->cachedNoteheadSym.value(), painter);
    }
}

void TDraw::draw(const NoteDot* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    if (item->note() && item->note()->dotsHidden()) {     // don't draw dot if note is hidden
        return;
    } else if (item->rest() && item->rest()->isGap()) {  // don't draw dot for gap rests
        return;
    }
    const Note* n = item->note();
    Fraction tick = n ? n->chord()->tick() : item->rest()->tick();
    // always draw dot for non-tab
    // for tab, draw if on a note and stems through staff or on a rest and rests shown
    if (!item->staff()->isTabStaff(tick)
        || (n && item->staff()->staffType(tick)->stemThrough())
        || (!n && item->staff()->staffType(tick)->showRests())) {
        painter->setPen(item->curColor(opt));
        item->drawSymbol(SymId::augmentationDot, painter);
    }
}

void TDraw::draw(const NoteHead* item, Painter* painter, const PaintOptions& opt)
{
    draw(static_cast<const Symbol*>(item), painter, opt);
}

void TDraw::draw(const NoteLineSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextLineBaseSegment(item, painter, opt);
}

void TDraw::draw(const OttavaSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextLineBaseSegment(item, painter, opt);
}

void TDraw::draw(const Parenthesis* item, muse::draw::Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    EngravingItem* parent = item->parentItem();
    TimeSig* parentTs = parent && parent->isTimeSig() ? toTimeSig(parent) : nullptr;

    if (parentTs && !parentTs->showOnThisStaff()) {
        return;
    }

    Color penColor = item->curColor(opt);

    Pen pen(penColor);

    if (item->ldata()->symId != SymId::noSym) {
        painter->setPen(pen);
        item->drawSymbol(item->ldata()->symId, painter, PointF(), item->ldata()->symScale);
        return;
    }

    painter->setBrush(Brush(pen.color()));
    pen.setCapStyle(PenCapStyle::RoundCap);
    pen.setJoinStyle(PenJoinStyle::RoundJoin);
    pen.setWidthF(item->ldata()->endPointThickness * item->spatium() * item->ldata()->intrinsicMag());

    painter->setPen(pen);
    painter->drawPath(item->ldata()->path());
}

void TDraw::draw(const PartialTieSegment* item, muse::draw::Painter* painter, const PaintOptions& opt)
{
    draw(static_cast<const TieSegment*>(item), painter, opt);
}

void TDraw::draw(const PalmMuteSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextLineBaseSegment(item, painter, opt);
}

void TDraw::draw(const PedalSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextLineBaseSegment(item, painter, opt);
}

void TDraw::draw(const PickScrapeSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextLineBaseSegment(item, painter, opt);
}

void TDraw::draw(const PlayCountText* item, muse::draw::Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const PlayTechAnnotation* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const RasgueadoSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextLineBaseSegment(item, painter, opt);
}

void TDraw::draw(const RehearsalMark* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const Rest* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    if (item->isGap()) {
        drawDebugGapRest(item, painter);
        return;
    }

    if (item->shouldNotBeDrawn()) {
        return;
    }

    const Rest::LayoutData* ldata = item->ldata();

    painter->setPen(item->curColor(opt));

    if (DeadSlapped* ds = item->deadSlapped()) {
        draw(ds, painter, opt);
    } else {
        item->drawSymbol(ldata->sym, painter);
    }
}

void TDraw::drawDebugGapRest(const Rest* item, muse::draw::Painter* painter)
{
    IF_ASSERT_FAILED(item->debugDrawGap()) {
        return;
    }

    painter->setPen(Color::RED);
    item->drawSymbol(item->ldata()->sym, painter);
}

//! NOTE May be removed later (should be only single mode)
void TDraw::draw(const ShadowNote* item, Painter* painter, const PaintOptions&)
{
    TRACE_DRAW_ITEM;

    if (!item->visible() || !item->isValid()) {
        return;
    }

    PointF ap(item->pagePos());
    painter->translate(ap);
    double lw = item->style().styleMM(Sid::stemWidth) * item->mag();
    Pen pen(item->color(), lw, PenStyle::SolidLine, PenCapStyle::FlatCap);
    painter->setPen(pen);

    bool up = item->computeUp();

    // Draw the accidental
    SymId acc = Accidental::subtype2symbol(item->accidentalType());
    if (acc != SymId::noSym) {
        PointF posAcc;
        posAcc.rx() -= item->symWidth(acc) + item->style().styleMM(Sid::accidentalNoteDistance) * item->mag();
        item->drawSymbol(acc, painter, posAcc);
    }

    // Draw the notehead
    item->drawSymbol(item->noteheadSymbol(), painter);

    // Draw the dots
    double sp = item->spatium();
    double sp2 = sp / 2;
    double noteheadWidth = item->symWidth(item->noteheadSymbol());

    PointF posDot;
    if (item->duration().dots() > 0) {
        double d  = item->style().styleMM(Sid::dotNoteDistance) * item->mag();
        double dd = item->style().styleMM(Sid::dotDotDistance) * item->mag();
        posDot.rx() += (noteheadWidth + d);

        if (item->isRest()) {
            posDot.ry() += Rest::getDotline(item->duration().type()) * sp2;
        } else {
            posDot.ry() -= (item->lineIndex() % 2 == 0 ? sp2 : 0);
        }

        if (item->hasFlag() && up) {
            posDot.rx() = std::max(posDot.x(), noteheadWidth + item->symBbox(item->flagSym()).right());
        }

        for (int i = 0; i < item->duration().dots(); i++) {
            posDot.rx() += dd * i;
            item->drawSymbol(SymId::augmentationDot, painter, posDot);
            posDot.rx() -= dd * i;
        }
    }

    // Draw stem and flag
    if (item->hasStem()) {
        double x = item->symSmuflAnchor(item->noteheadSymbol(), up ? SmuflAnchorId::stemUpSE : SmuflAnchorId::stemDownNW).x();
        x += up ? -0.5 * lw : 0.5 * lw;
        double y1 = item->symSmuflAnchor(item->noteheadSymbol(), up ? SmuflAnchorId::stemUpSE : SmuflAnchorId::stemDownNW).y();
        double y2 = (up ? -3.5 : 3.5) * sp;

        if (item->hasFlag()) {
            SymId flag = item->flagSym();
            item->drawSymbol(flag, painter, PointF(x - (lw / 2), y2));
            y2 += item->symSmuflAnchor(flag, up ? SmuflAnchorId::stemUpNW : SmuflAnchorId::stemDownSW).y();
        }
        painter->drawLine(LineF(x, y1, x, y2));
    }

    // Draw ledger lines if needed
    if (item->ledgerLinesVisible()) {
        double extraLen = item->style().styleS(Sid::ledgerLineLength).val() * sp;
        double x1 = -extraLen;
        double x2 = noteheadWidth + extraLen;
        double yOffset = item->staffOffsetY();
        double step = sp2 * item->staffType()->lineDistance().val();

        lw = item->style().styleMM(Sid::ledgerLineWidth) * item->mag();
        pen.setWidthF(lw);
        painter->setPen(pen);

        const int topLine = -2 + yOffset / step;
        for (int i = topLine; i >= item->lineIndex(); i -= 2) {
            double y = step * (i - item->lineIndex());
            painter->drawLine(LineF(x1, y, x2, y));
        }
        int l = item->staffType()->lines() * 2 + yOffset / step; // first ledger line below staff
        for (int i = l; i <= item->lineIndex(); i += 2) {
            double y = step * (i - item->lineIndex());
            painter->drawLine(LineF(x1, y, x2, y));
        }
    }

    item->drawArticulations(painter);

    painter->translate(-ap);
}

void TDraw::draw(const SlurSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    painter->save();
    setMask(item, painter);

    Pen pen(item->curColor(opt));
    double mag = item->staff() ? item->staff()->staffMag(item->slur()->tick()) : 1.0;

    //Replace generic Qt dash patterns with improved equivalents to show true dots (keep in sync with tie.cpp)
    std::vector<double> dotted     = { 0.01, 1.99 };   // tighter than Qt PenStyle::DotLine equivalent - would be { 0.01, 2.99 }
    std::vector<double> dashed     = { 3.00, 3.00 };   // Compensating for caps. Qt default PenStyle::DashLine is { 4.0, 2.0 }
    std::vector<double> wideDashed = { 5.00, 6.00 };

    switch (item->slurTie()->styleType()) {
    case SlurStyleType::Solid:
        painter->setBrush(Brush(pen.color()));
        pen.setCapStyle(PenCapStyle::RoundCap);
        pen.setJoinStyle(PenJoinStyle::RoundJoin);
        pen.setWidthF(item->endWidth() * mag);
        break;
    case SlurStyleType::Dotted:
        painter->setBrush(BrushStyle::NoBrush);
        pen.setCapStyle(PenCapStyle::RoundCap);           // round dots
        pen.setDashPattern(dotted);
        pen.setWidthF(item->dottedWidth() * mag);
        break;
    case SlurStyleType::Dashed:
        painter->setBrush(BrushStyle::NoBrush);
        pen.setDashPattern(dashed);
        pen.setWidthF(item->dottedWidth() * mag);
        break;
    case SlurStyleType::WideDashed:
        painter->setBrush(BrushStyle::NoBrush);
        pen.setDashPattern(wideDashed);
        pen.setWidthF(item->dottedWidth() * mag);
        break;
    case SlurStyleType::Undefined:
        break;
    }
    painter->setPen(pen);
    painter->drawPath(item->ldata()->path());

    painter->restore();
}

void TDraw::draw(const Spacer* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    if (opt.isPrinting || !item->score()->showUnprintable()) {
        return;
    }

    auto conf = item->configuration();

    Pen pen(item->selected() ? conf->selectionColor() : conf->formattingColor(), item->spatium() * 0.3);

    painter->setPen(pen);
    painter->setBrush(BrushStyle::NoBrush);
    painter->drawPath(item->ldata()->path);
}

void TDraw::drawMeloChangeTerrain(const StaffLines* item, Painter* painter, const PaintOptions& opt,
                                  const melo::ChangeIndicator& model, const StaffType* changeSt,
                                  const StaffType* displayedSt, double x0, ChangePlacement placement)
{
    {
        const double _spatium = item->spatium();
        const double dist = displayedSt->lineDistance().val() * _spatium;
        const double topY = item->pos().y();
        // The semantic state supplies the incoming labels and glyphs.
        // Vertical placement instead uses the staff frame visibly drawn
        // in this measure. They are the same at a bar boundary; inside
        // a bar the measure keeps its starting frame, so using the
        // incoming frame would detach every element from its note-line.
        const StaffType::MeloFrameView& view
            = displayedSt->meloFrameView(item->score(), item->staffIdx(), item->measure()->system());
        const double periodCents = displayedSt->meloPeriodCents();
        if (!view.empty() && periodCents > 0.0) {
            melo::PeriodicOrigins origins;
            if (!melo::periodicOrigins(displayedSt->meloStateJson(), origins)) {
                return;
            }
            auto yOf = [&](double cents) {
                return topY + displayedSt->meloYFromCents(cents, view) * _spatium;
            };
            const StaffType::MeloHeaderGeometry g
                = melo::changeTerrainGeometry(changeSt, _spatium, item->score()->style().defaultSpatium(), model);
            const double indicatorW = g.indicatorW;
            // Terrain columns, left to right, from the measure's left edge.
            const double labelRight = x0 + 0.3 * _spatium + g.changeLabelBand;
            // Owner rule 2026-09-12 (2a): the mode-arrow lane sits between
            // the labels and the dots; the key-arrow lane stays right of them.
            const double leftLaneLeft = labelRight;
            const double dotCenterX = labelRight + g.changeLeftArrowLane + indicatorW;
            const double rightLabelLeft = dotCenterX + indicatorW;                 // Grey labels start here
            const double arrowLaneLeft = rightLabelLeft + g.changeRightLabelBand;
            // Period 0 of the model = the anchor Do-line: the lowest Do-line
            // of the stave stack that keeps the whole indicator inside the
            // staff (owner ruling 2026-08-19; melo::changeAnchorPeriodCents).
            const double basePeriod = melo::changeAnchorPeriodCents(
                view, model, periodCents, origins.doCentsAboveExtentLower,
                melo::systemNoteCents(item->measure()->system(), item->staffIdx(), displayedSt));
            auto centsOf = [&](const melo::ChangePoint& p) {
                return basePeriod + (p.periodOffset + p.ordinate) * periodCents;
            };
            // A scale-change stack is a HEADER stack: like the system
            // header it is instantiated in every period of the stave
            // stack, boundary-inclusive (M3 ruling: the top Do-line
            // always carries its dot and indicator). Arrow endpoints
            // (key/mode) are single glyphs at their model position.
            const bool scaleKind = std::find(model.kinds.begin(), model.kinds.end(), u"scale") != model.kinds.end();
            const double eps = 1e-6;
            auto instancesOf = [&](const melo::ChangePoint& p) {
                std::vector<double> out;
                if (!scaleKind) {
                    out.push_back(centsOf(p));
                    return out;
                }
                for (const StaffType::MeloFrameBand& band : view.bands) {
                    for (const StaffType::MeloSegment& segment : band.segments) {
                        const double segBase = origins.doCentsAboveExtentLower
                                               + std::floor((segment.lowerCents - origins.doCentsAboveExtentLower)
                                                            / periodCents + eps) * periodCents;
                        for (double period = segBase; period <= segment.upperCents + eps; period += periodCents) {
                            const double c = period + p.ordinate * periodCents;
                            if (c >= segment.lowerCents - eps && c <= segment.upperCents + eps) {
                                out.push_back(c);
                            }
                        }
                    }
                }
                return out;
            };
            const IEngravingFontPtr font = item->score()->engravingFont();
            Font labelFont(u"Edwin", Font::Type::Text);
            labelFont.setPointSizeF(9.0 * item->spatium() / item->defaultSpatium());
            FontMetrics fm(labelFont);
            const double gap = 0.25 * _spatium;
            // Only the Kernel's new tonic class receives a pitch prefix.
            // A key arrow names a common pitch, not a tonic destination.
            melo::TonicPitchLabel keyLabel;
            const bool haveKeyLabel = melo::tonicPitchLabel(changeSt->meloStateJson(), keyLabel);
            // Locate that exact tonic lattice instance in the displayed
            // frame. Its first Do can belong to a different octave from
            // the incoming state's first Do after an extent change.
            double incomingTonicOrigin = 0.0;
            if (haveKeyLabel && !melo::noteCentsAboveExtentLower(displayedSt->meloStateJson(),
                                                                 keyLabel.nPer, keyLabel.nGen, incomingTonicOrigin)) {
                return;
            }
            std::map<int, muse::String> labelByPeriod;
            auto keyLabelForRow = [&](double rowCents) -> muse::String {
                const int k = int(std::lround((rowCents - incomingTonicOrigin) / periodCents));
                auto found = labelByPeriod.find(k);
                if (found != labelByPeriod.end()) {
                    return found->second;
                }
                melo::TonicPitchLabel rowLabel;
                const muse::String text = melo::tonicPitchLabelInPeriod(changeSt->meloStateJson(), k, rowLabel)
                                          ? rowLabel.label : keyLabel.label;
                labelByPeriod[k] = text;
                return text;
            };
            auto isNewTonic = [&](const melo::ChangePoint& tp) {
                if (!haveKeyLabel || tp.nGen != keyLabel.nGen) {
                    return false;
                }
                for (const melo::ChangeArrow& a : model.arrows) {
                    if (a.kind == u"mode" || a.trumps == u"mode") {
                        return a.to.nGen == tp.nGen && a.to.periodOffset == tp.periodOffset;
                    }
                }
                return true;
            };

            // Flanking strokes are continuous over the whole stack,
            // through every band gap. Boundary placements retain one
            // synthetic solid flank beside the real bar line. A mid-bar
            // placement owns both dashed grey flanks. Every synthetic
            // stroke resolves the normal bar-line width from the style.
            const double flankWidth = item->style().styleMM(Sid::barWidth);
            const double top = yOf(view.topCents());
            const double bottom = yOf(view.bottomCents());
            if (placement == ChangePlacement::MID_BAR) {
                painter->setPen(Pen(item->curColor(item->visible(), item->style().value(Sid::meloMidBarFlankColor).value<Color>(),
                                                   opt), flankWidth,
                                    PenStyle::DashLine, PenCapStyle::FlatCap));
                painter->drawLine(LineF(x0, top, x0, bottom));
                painter->drawLine(LineF(x0 + g.changeTerrainWidth, top,
                                        x0 + g.changeTerrainWidth, bottom));
            } else {
                const double strokeX = placement == ChangePlacement::END_BAR_COURTESY
                                       ? x0 : x0 + g.changeTerrainWidth;
                painter->setPen(Pen(item->curColor(opt), flankWidth,
                                    PenStyle::SolidLine, PenCapStyle::FlatCap));
                painter->drawLine(LineF(strokeX, top, strokeX, bottom));
            }

            // Dots (Kernel notehead classes); ALL labels LEFT of the dots
            // (owner ruling 2026-08-16: the change stack must look like
            // the header stack — same interval pattern, same collisions).
            for (const melo::ChangeStack& stack : model.dotStacks) {
                for (double stackCents : instancesOf(stack.members.front())) {
                    double dx = 0.0;
                    String text;
                    String rightText;
                    for (const melo::ChangePoint& member : stack.members) {
                        String token = member.noteheadToken;
                        SymId dotSym = SymId::noteheadHalf;
                        const bool haveToken = !token.isEmpty() || melo::noteheadToken(changeSt->meloStateJson(), member.nGen, token);
                        const bool grey = false; // all labels left (owner ruling); right band unused
                        if (font && haveToken) {
                            if (token == u"triangle-vertex-up") {
                                dotSym = SymId::noteheadTriangleUpBlack;
                            } else if (token == u"triangle-vertex-down") {
                                dotSym = SymId::noteheadTriangleDownBlack;
                            } else if (token == u"square-vertex-up") {
                                dotSym = SymId::noteheadDiamondBlack;
                            } else if (token == u"square-edge-up") {
                                dotSym = SymId::noteheadSquareBlack;
                            }
                        }
                        if (font) {
                            RectF gb = font->bbox(dotSym, 1.0);
                            double centroidDy = 0.0;
                            if (dotSym == SymId::noteheadTriangleUpBlack) {
                                centroidDy = -gb.height() / 6.0;
                            } else if (dotSym == SymId::noteheadTriangleDownBlack) {
                                centroidDy = gb.height() / 6.0;
                            }
                            painter->setPen(Pen(item->curColor(opt), item->lw()));
                            font->draw(dotSym, painter, 1.0,
                                       PointF(dotCenterX - gb.width() / 2.0 + dx, yOf(stackCents) + centroidDy));
                            dx += 0.15 * _spatium;
                        }
                        String& side = grey ? rightText : text;
                        if (!side.isEmpty()) {
                            side += u" ";
                        }
                        side += member.label;
                    }
                    const double cy = yOf(stackCents);
                    // The new tonic's row carries "[PitchN]:" first.
                    for (const melo::ChangePoint& member : stack.members) {
                        if (isNewTonic(member) && !text.isEmpty()) {
                            text = keyLabelForRow(stackCents) + u": " + text;
                            break;
                        }
                    }
                    if (!text.isEmpty()) {
                        const melo::PitchLabelLayout textLayout
                            = melo::pitchLabelLayout(text, labelFont, font);
                        painter->setPen(Pen(item->curColor(opt)));
                        melo::drawPitchLabel(painter,
                                             PointF(labelRight - gap - textLayout.bounds.right(),
                                                    cy - (textLayout.bounds.top() + textLayout.bounds.bottom()) / 2.0),
                                             labelFont, font, textLayout);
                    }
                    if (!rightText.isEmpty()) {
                        RectF tb = fm.boundingRect(rightText);
                        painter->setFont(labelFont);
                        painter->setPen(Pen(item->curColor(opt)));
                        painter->drawText(PointF(rightLabelLeft + gap, cy - (tb.top() + tb.bottom()) / 2.0), rightText);
                    }
                }
            }
            // Tonic indicators (settled §3.3 construction) with labels
            // left when no dot already labels that row; the NEW
            // tonic's row carries the current-key label "[PitchN]:"
            // (owner spec 2026-08-17).
            for (const melo::ChangePoint& tp : model.tonicIndicators) {
                for (double tpCents : instancesOf(tp)) {
                    const double h = 1.15 * dist + 0.025 * dist;
                    const double cy = yOf(tpCents);
                    PainterPath ring;
                    ring.moveTo(dotCenterX, cy - h);
                    ring.lineTo(dotCenterX + h, cy);
                    ring.lineTo(dotCenterX, cy + h);
                    ring.lineTo(dotCenterX - h, cy);
                    ring.closeSubpath();
                    painter->setPen(Pen(item->curColor(opt), 0.15 * dist));
                    painter->setBrush(BrushStyle::NoBrush);
                    painter->drawPath(ring);
                    bool labelled = false;
                    for (const melo::ChangeStack& stack : model.dotStacks) {
                        for (const melo::ChangePoint& m : stack.members) {
                            if (m.nGen == tp.nGen && m.periodOffset == tp.periodOffset) {
                                labelled = true;
                            }
                        }
                    }
                    if (!labelled) {
                        muse::String text = tp.label;
                        if (isNewTonic(tp)) {
                            text = keyLabelForRow(tpCents) + u": " + text;
                        }
                        const melo::PitchLabelLayout textLayout
                            = melo::pitchLabelLayout(text, labelFont, font);
                        painter->setPen(Pen(item->curColor(opt)));
                        melo::drawPitchLabel(painter,
                                             PointF(labelRight - gap - textLayout.bounds.right(),
                                                    cy - (textLayout.bounds.top() + textLayout.bounds.bottom()) / 2.0),
                                             labelFont, font, textLayout);
                    }
                }
            }
            // Arrows in the arrow lane: shaft between endpoint centroids,
            // Kernel connector head at the `to` end.
            melo::ConnectorGlyph head;
            if (melo::connectorGlyph(head)) {
                const double pen = head.penCents / StaffType::MELO_CENTS_PER_LINE_DISTANCE * dist;
                const double hh = head.headHeightCents / StaffType::MELO_CENTS_PER_LINE_DISTANCE * dist;
                const double hw = head.headHalfWidthCents / StaffType::MELO_CENTS_PER_LINE_DISTANCE * dist;
                // Owner ruling (M5 gate, 2026-08-16): arrows in black ink,
                // heavier than the tonic indicator, large head.
                const Color arrowInk = opt.isPrinting ? Color::BLACK
                                       : item->curColor(item->visible(),
                                                        item->style().value(Sid::meloChangeArrowColor).value<Color>(), opt);
                size_t modeArrows = 0;
                size_t keyArrows = 0;
                for (const melo::ChangeArrow& a : model.arrows) {
                    (a.kind == u"mode" ? modeArrows : keyArrows)++;
                }
                const double leftLaneWidth = g.changeLeftArrowLane / std::max(size_t(1), modeArrows);
                const double rightLaneWidth = g.changeArrowLane / std::max(size_t(1), keyArrows);
                size_t modeIndex = 0;
                size_t keyIndex = 0;
                for (const melo::ChangeArrow& a : model.arrows) {
                    const bool modeArrow = a.kind == u"mode";
                    const double arrowX = modeArrow
                                          ? leftLaneLeft + (modeIndex++ + 0.5) * leftLaneWidth
                                          : arrowLaneLeft + (keyIndex++ + 0.5) * rightLaneWidth;
                    const double yFrom = yOf(centsOf(a.from));
                    const double yTo = yOf(centsOf(a.to));
                    painter->setPen(Pen(arrowInk, pen, PenStyle::SolidLine, PenCapStyle::RoundCap));
                    // Solid head at the `to` end (owner ruling 2026-08-16:
                    // "close the notehead"), drawn as MuseScore's SMuFL
                    // arrowhead glyph (owner decision 6a, 2026-08-19: follow
                    // MuseScore's arrows) scaled to the Kernel's head height;
                    // the shaft stops at the head's base so it never pokes
                    // through the apex.
                    const double back = a.up ? hh : -hh; // toward `from`
                    painter->drawLine(LineF(arrowX, yFrom, arrowX, yTo + back));
                    const SymId headSym = a.up ? SymId::arrowheadBlackUp : SymId::arrowheadBlackDown;
                    const RectF gb = font->bbox(headSym, 1.0);
                    if (gb.height() > 0.0) {
                        const double mag = hh / gb.height();
                        // Glyph origin: SMuFL arrowheads sit on the baseline with
                        // the apex up (Up) or down (Down); centre horizontally on
                        // the shaft and put the apex on the `to` row.
                        const double x = arrowX - gb.width() * mag / 2.0 - gb.left() * mag;
                        const double y = a.up ? yTo - gb.top() * mag : yTo - gb.bottom() * mag;
                        painter->setPen(Pen(arrowInk, pen));
                        font->draw(headSym, painter, mag, PointF(x, y));
                    }
                    UNUSED(hw);
                }
            }
        }
    }
}

void TDraw::draw(const StaffLines* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    if (!opt.isPrinting) {
        item->clearMeloHeaderPitchTargets();
    }
    painter->save();

    setMask(item, painter);

    if (!item->meloGuideLines().empty()) {
        // JiMStaff (Milestone 1): each guide line carries its own style —
        // the one deliberate exception to the single-pen staff-line rule,
        // reached only when the JiMS layout branch populated guides.
        for (const StaffLines::MeloGuideLine& guide : item->meloGuideLines()) {
            Color color = item->style().value(guide.colorStyle).value<Color>();
            if (!opt.isPrinting && item->configuration()->isHighContrast()) {
                color = item->configuration()->defaultColor();
            }
            color = item->curColor(item->visible(), color, opt);
            PenStyle pattern = guide.dashed ? PenStyle::DashLine : PenStyle::SolidLine;
            switch (guide.primeLimit) {
            case 5: pattern = PenStyle::DotLine;
                break;
            case 7: pattern = PenStyle::DashDotLine;
                break;
            case 11: pattern = PenStyle::DashDotDotLine;
                break;
            default: break;
            }
            painter->setPen(Pen(color, item->lw(), pattern, PenCapStyle::FlatCap));
            painter->drawLine(guide.line);
        }

        // System-head header (owner rulings 2026-08-14): scale-dot
        // column, tonic indicator, and the continuous series of crescent
        // clefs through every visible stave segment. Partial periods get
        // the patent's sliced crescent: the glyph clipped at the cut and
        // closed by a new horizontal line there.
        // All ordinates come from the Kernel (frame cache + render
        // geometry); drawing is the only thing happening here.
        const Staff* meloStaff = item->staff();
        const StaffType* meloSt = meloStaff ? meloStaff->staffType(item->measure()->tick()) : nullptr;
        const bool systemHead = item->measure()->system()
                                && item->measure()->system()->firstMeasure() == item->measure();
        if (meloSt && meloSt->isMelo() && systemHead) {
            const double _spatium = item->spatium();
            const double dist = meloSt->lineDistance().val() * _spatium;
            const double topY = item->pos().y();
            // The frame is the Kernel's, always (Milestone 4): no
            // fork-side nominal frame; with no frame there is no header.
            // Milestone 8: the explicit view for THIS system — every band
            // draws its own header (crescents, dots, tonic indicators,
            // labels); the whole-piece legacy view is one band.
            const StaffType::MeloFrameView& view
                = meloSt->meloFrameView(item->score(), item->staffIdx(), item->measure()->system());
            const double periodCents = meloSt->meloPeriodCents();
            if (view.empty() || periodCents <= 0.0) {
                painter->restore();
                return;
            }
            auto yOf = [&](double cents) {
                return topY + meloSt->meloYFromCents(cents, view) * _spatium;
            };
            const double periodH = (periodCents / StaffType::MELO_CENTS_PER_LINE_DISTANCE) * dist;
            const double clefRy = periodH / 2.0;
            const StaffType::MeloHeaderGeometry headerGeom
                = meloSt->meloHeaderGeometry(_spatium, item->score()->style().defaultSpatium(), &view);
            const double clefRx = headerGeom.clefRx;
            const double indicatorW = headerGeom.indicatorW;
            const double clefRight = item->pos().x() - 0.3 * _spatium;
            const double clefLeft = clefRight - clefRx;
            // The Split-mode right label band sits between the dot
            // column and the clef; the dot column shifts left by it.
            const double dotCenterX = clefLeft - headerGeom.rightLabelBand
                                      - 2.0 * indicatorW + indicatorW;
            const IEngravingFontPtr font = item->score()->engravingFont();

            // Tuning label (owner rulings 2026-08-14 and 2026-08-24): the
            // generator width as "M5= <cents>¢" above the top VISIBLE JiMS
            // staff at the system head. Tuning is score-wide, so repeating
            // the same fact above lower SATB staves is redundant. If
            // hide-empty-staves elides Soprano on a later system, the label
            // follows the first JiMS staff still visible there. M5 is the
            // Kernel's canonical name for the fifth, per the (P8, M5)
            // lattice. The value comes from staff_metrics; the fork never
            // parses the state JSON for musical facts.
            bool topVisibleMeloStaff = false;
            const System* system = item->measure()->system();
            if (system && item->staffIdx() < system->staves().size()
                && system->staff(item->staffIdx())->show() && meloStaff->show()) {
                topVisibleMeloStaff = true;
                for (staff_idx_t staffIdx = 0; staffIdx < item->staffIdx(); ++staffIdx) {
                    const Staff* staff = item->score()->staff(staffIdx);
                    const StaffType* type = staff ? staff->staffType(item->measure()->tick()) : nullptr;
                    if (type && type->isMelo() && staff->show()
                        && staffIdx < system->staves().size() && system->staff(staffIdx)->show()) {
                        topVisibleMeloStaff = false;
                        break;
                    }
                }
            }
            if (topVisibleMeloStaff) {
                double generatorCents = 0.0;
                double periodCents = 0.0;
                if (melo::staffMetrics(meloSt->meloStateJson(), generatorCents, periodCents)) {
                    muse::String label = muse::String(u"M5= %1¢")
                                         .arg(muse::String::number(generatorCents, 1));
                    Font labelFont(u"Edwin", Font::Type::Text);
                    labelFont.setPointSizeF(10.0 * item->spatium() / item->defaultSpatium());
                    painter->setFont(labelFont);
                    painter->setPen(Pen(item->curColor(opt)));
                    painter->drawText(PointF(clefLeft - 2.0 * indicatorW,
                                             yOf(view.topCents()) - 1.2 * _spatium),
                                      label);
                }
            }

            std::vector<melo::ScaleDotStack> stacks;
            double tonicCents = 0.0;
            melo::PeriodicOrigins origins;
            if (!melo::periodicOrigins(meloSt->meloStateJson(), origins)) {
                return;
            }
            const bool haveTonic = melo::tonicCentsAboveDo(meloSt->meloStateJson(), tonicCents);
            const double epsilon = 1e-6;
            auto dotSymbol = [&](int nGen) {
                muse::String token;
                SymId symbol = SymId::noteheadHalf;
                if (melo::noteheadToken(meloSt->meloStateJson(), nGen, token)) {
                    if (token == u"triangle-vertex-up") {
                        symbol = SymId::noteheadTriangleUpBlack;
                    } else if (token == u"triangle-vertex-down") {
                        symbol = SymId::noteheadTriangleDownBlack;
                    } else if (token == u"square-vertex-up") {
                        symbol = SymId::noteheadDiamondBlack;
                    } else if (token == u"square-edge-up") {
                        symbol = SymId::noteheadSquareBlack;
                    }
                }
                return symbol;
            };
            auto dotCentroidDy = [&](SymId symbol, const RectF& bounds) {
                if (symbol == SymId::noteheadTriangleUpBlack) {
                    return -bounds.height() / 6.0;
                }
                if (symbol == SymId::noteheadTriangleDownBlack) {
                    return bounds.height() / 6.0;
                }
                return 0.0;
            };
            // A fixed ratio-line edge and its associated moving scale dot
            // coincide only at the ratio's exact tuning. Retain a dot stack
            // whenever its actual painted glyph intersects the fixed staff
            // segment; testing only the moving centre loses So at a pure 3/2
            // edge (and likewise Fa at its fixed boundary).
            auto dotStackIntersectsSegment = [&](double cents, const std::vector<int>& generators,
                                                 const StaffType::MeloSegment& segment) {
                if (!font) {
                    return false;
                }
                const double centerY = yOf(cents);
                double inkTop = centerY;
                double inkBottom = centerY;
                for (int nGen : generators) {
                    const SymId symbol = dotSymbol(nGen);
                    const RectF bounds = font->bbox(symbol, 1.0);
                    const double originY = centerY + dotCentroidDy(symbol, bounds);
                    inkTop = std::min(inkTop, originY + bounds.top());
                    inkBottom = std::max(inkBottom, originY + bounds.bottom());
                }
                const double segmentTop = yOf(segment.upperCents);
                const double segmentBottom = yOf(segment.lowerCents);
                return inkBottom >= segmentTop - epsilon && inkTop <= segmentBottom + epsilon;
            };
            if (font && melo::scaleDots(meloSt->meloStateJson(), stacks)) {
                for (const StaffType::MeloFrameBand& band : view.bands) {
                    std::vector<double> drawnStacks;
                    std::vector<double> drawnTonics;
                    for (const StaffType::MeloSegment& segment : band.segments) {
                        double basePeriod = origins.doCentsAboveExtentLower
                                            + std::floor((segment.lowerCents - origins.doCentsAboveExtentLower)
                                                         / periodCents) * periodCents;
                        for (double period = basePeriod; period <= segment.upperCents + epsilon;
                             period += periodCents) {
                            for (const auto& stack : stacks) {
                                double cents = period + stack.cents;
                                if (!dotStackIntersectsSegment(cents, stack.frontToBack, segment)) {
                                    continue;
                                }
                                if (std::any_of(drawnStacks.begin(), drawnStacks.end(), [&](double c) {
                                    return std::abs(c - cents) < epsilon;
                                })) {
                                    continue;
                                }
                                drawnStacks.push_back(cents);
                                double dy = yOf(cents);
                                double dx = 0.0;
                                for (int nGen : stack.frontToBack) {
                                    const SymId dotSym = dotSymbol(nGen);
                                    RectF gb = font->bbox(dotSym, 1.0);
                                    const double centroidDy = dotCentroidDy(dotSym, gb);
                                    painter->setPen(Pen(item->curColor(opt), item->lw()));
                                    font->draw(dotSym, painter, 1.0,
                                               PointF(dotCenterX - gb.width() / 2.0 + dx,
                                                      dy + centroidDy));
                                    dx += 0.15 * _spatium;
                                }
                            }
                            if (haveTonic) {
                                double cents = period + tonicCents;
                                if (std::any_of(drawnStacks.begin(), drawnStacks.end(), [&](double c) {
                                    return std::abs(c - cents) < epsilon;
                                }) && std::none_of(drawnTonics.begin(), drawnTonics.end(), [&](double c) {
                                    return std::abs(c - cents) < epsilon;
                                })) {
                                    drawnTonics.push_back(cents);
                                    // The tonic indicator per the settled
                                    // construction (JiMStudent_Spec 3.3):
                                    // hollow vertex-up square, thin pen,
                                    // sized so clear whitespace separates
                                    // the ring from the scale dot inside it
                                    // (owner correction 2026-08-14).
                                    const double h = 1.15 * dist + 0.025 * dist;
                                    const double cy = yOf(cents);
                                    PainterPath ring;
                                    ring.moveTo(dotCenterX, cy - h);
                                    ring.lineTo(dotCenterX + h, cy);
                                    ring.lineTo(dotCenterX, cy + h);
                                    ring.lineTo(dotCenterX - h, cy);
                                    ring.closeSubpath();
                                    painter->setPen(Pen(item->curColor(opt), 0.15 * dist));
                                    painter->setBrush(BrushStyle::NoBrush);
                                    painter->drawPath(ring);
                                }
                            }
                        }
                    }
                }
            }

            // Scale-dot labels (owner epiphany 2026-08-15): each stack
            // member's canonical-solfa name, Kernel-supplied, centroid-
            // aligned to its dot through the same seam. Left mode: all
            // names left of the dot column; Split: flat side (nGen<=0)
            // left, sharp side right with a semi-transparent white
            // backing that dims but never erases the lines beneath.
            {
                const MeloScaleDotLabelMode labelMode = meloSt->meloResolvedScaleDotLabelMode();
                std::vector<melo::LabeledDotStack> labelStacks;
                // Current-key label "[PitchN]:" left of the tonic indicator's
                // row (owner spec 2026-08-17) — Kernel-derived; drawn even
                // when class labels are off.
                melo::TonicPitchLabel keyLabel;
                const bool haveKeyLabel = haveTonic && melo::tonicPitchLabel(meloSt->meloStateJson(), keyLabel);
                if ((labelMode != MeloScaleDotLabelMode::None || haveKeyLabel)
                    && melo::scaleDotLabels(meloSt->meloStateJson(), labelStacks)) {
                    const bool tonicIsDo = std::any_of(labelStacks.begin(), labelStacks.end(), [&](const auto& stack) {
                        return std::any_of(stack.members.begin(), stack.members.end(), [&](const auto& member) {
                            return member.nGen == keyLabel.nGen && member.label == u"Do";
                        });
                    });
                    Font labelFont(u"Edwin", Font::Type::Text);
                    labelFont.setPointSizeF(9.0 * item->spatium() / item->defaultSpatium());
                    FontMetrics fm(labelFont);
                    const double gap = 0.25 * _spatium;
                    const double inset = 0.15 * _spatium;
                    const double dotColLeft = dotCenterX - indicatorW;
                    const double dotColRight = dotCenterX + indicatorW;
                    for (const StaffType::MeloFrameBand& band : view.bands) {
                        std::vector<double> drawnLabelStacks;
                        // The current-key label's row and text: the band's lowest
                        // drawn tonic row and THAT row's Kernel label (owner
                        // finding 2, 2026-08-18: octave numbers correct always and
                        // everywhere) — for banded and whole-piece views alike.
                        const double lowestTonicRow = double(band.labelPeriodIndex) * periodCents
                                                      + origins.tonicCentsAboveExtentLower;
                        const muse::String keyText = band.tonicLabel.isEmpty() ? keyLabel.label : band.tonicLabel;
                        for (const StaffType::MeloSegment& segment : band.segments) {
                            double basePeriod = origins.doCentsAboveExtentLower
                                                + std::floor((segment.lowerCents - origins.doCentsAboveExtentLower)
                                                             / periodCents) * periodCents;
                            for (double period = basePeriod; period <= segment.upperCents + epsilon;
                                 period += periodCents) {
                                for (const melo::LabeledDotStack& stack : labelStacks) {
                                    double cents = period + stack.cents;
                                    std::vector<int> generators;
                                    generators.reserve(stack.members.size());
                                    for (const melo::LabeledDotMember& member : stack.members) {
                                        generators.push_back(member.nGen);
                                    }
                                    if (!dotStackIntersectsSegment(cents, generators, segment)) {
                                        continue;
                                    }
                                    if (std::any_of(drawnLabelStacks.begin(), drawnLabelStacks.end(), [&](double c) {
                                        return std::abs(c - cents) < epsilon;
                                    })) {
                                        continue;
                                    }
                                    drawnLabelStacks.push_back(cents);
                                    muse::String leftText;
                                    muse::String rightText;
                                    if (labelMode != MeloScaleDotLabelMode::None) {
                                        for (const melo::LabeledDotMember& member : stack.members) {
                                            const bool leftSide = (labelMode == MeloScaleDotLabelMode::Left)
                                                                  || member.nGen <= 0;
                                            muse::String& side = leftSide ? leftText : rightText;
                                            if (!side.isEmpty()) {
                                                side += u" ";
                                            }
                                            side += member.label;
                                        }
                                    }
                                    const bool pitchRow = haveKeyLabel && std::abs(stack.cents - tonicCents) < epsilon
                                                          && std::abs(cents - lowestTonicRow) < epsilon;
                                    if (pitchRow) {
                                        // Do's pitch label fits inside the crescent. Other
                                        // tonics keep their pitch label left of the dot.
                                        muse::String& side = tonicIsDo ? rightText : leftText;
                                        side = side.isEmpty() ? keyText + u":" : keyText + u": " + side;
                                    }
                                    // Painter::drawText rescales the CURRENT
                                    // painter font by 1200/deviceDpi in place
                                    // (applyFontSizeScaling), so the font must
                                    // be re-set immediately before EVERY
                                    // drawText or the second draw on screen
                                    // comes out ~12x too large (owner finding
                                    // 2026-08-15: giant "Ti"/"Mi" at 720c).
                                    const double centroidY = yOf(cents);
                                    if (!leftText.isEmpty()) {
                                        const melo::PitchLabelLayout textLayout
                                            = melo::pitchLabelLayout(leftText, labelFont, item->score()->engravingFont());
                                        const double baseline
                                            = centroidY - (textLayout.bounds.top() + textLayout.bounds.bottom()) / 2.0;
                                        painter->setPen(Pen(item->curColor(opt)));
                                        melo::drawPitchLabel(painter,
                                                             PointF(dotColLeft - gap - textLayout.bounds.right(), baseline),
                                                             labelFont, item->score()->engravingFont(), textLayout);
                                        if (pitchRow && !tonicIsDo && !opt.isPrinting) {
                                            const auto prefix = melo::pitchLabelLayout(keyText, labelFont, item->score()->engravingFont());
                                            item->recordMeloHeaderPitchTarget({ prefix.bounds.translated(PointF(dotColLeft - gap
                                                                                                                - textLayout.bounds.right(),
                                                                                                                baseline)),
                                                                                band.labelPeriodIndex, meloSt->meloStateJson(), keyText });
                                        }
                                    }
                                    if (!rightText.isEmpty()) {
                                        const melo::PitchLabelLayout textLayout
                                            = melo::pitchLabelLayout(rightText, labelFont, item->score()->engravingFont());
                                        const double baseline
                                            = centroidY - (textLayout.bounds.top() + textLayout.bounds.bottom()) / 2.0;
                                        const double x = dotColRight + gap;
                                        RectF backing = textLayout.bounds.translated(PointF(x, baseline));
                                        backing.adjust(-inset, -inset, inset, inset);
                                        painter->setNoPen();
                                        painter->setBrush(Brush(opt.invertColors && !opt.isPrinting ? Color(0, 0, 0, 191) : Color(255, 255,
                                                                                                                                  255,
                                                                                                                                  191)));
                                        painter->drawRect(backing);
                                        painter->setBrush(BrushStyle::NoBrush);
                                        painter->setPen(Pen(item->curColor(opt)));
                                        melo::drawPitchLabel(painter, PointF(x, baseline), labelFont,
                                                             item->score()->engravingFont(), textLayout);
                                        if (pitchRow && tonicIsDo && !opt.isPrinting) {
                                            const auto prefix = melo::pitchLabelLayout(keyText, labelFont, item->score()->engravingFont());
                                            item->recordMeloHeaderPitchTarget({ prefix.bounds.translated(PointF(x, baseline)),
                                                                                band.labelPeriodIndex, meloSt->meloStateJson(), keyText });
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Crescents, drawn last so their white bodies occlude the
            // support lines. Draw every Do-to-Do crescent whose period
            // intersects the segment. Each crescent is clipped to the
            // segment band and closed by a horizontal line wherever a
            // segment edge cuts through it (the patent mechanism, J4.001).
            for (const StaffType::MeloFrameBand& band : view.bands) {
                for (const StaffType::MeloSegment& segment : band.segments) {
                    double periodFloor = origins.doCentsAboveExtentLower
                                         + std::floor((segment.lowerCents - origins.doCentsAboveExtentLower)
                                                      / periodCents + epsilon) * periodCents;
                    const double segTopY = yOf(segment.upperCents);
                    const double segBottomY = yOf(segment.lowerCents);
                    painter->save();
                    painter->setClipRect(RectF(clefLeft - _spatium, segTopY - item->lw(),
                                               clefRx + 2.0 * _spatium,
                                               segBottomY - segTopY + 2.0 * item->lw()));
                    for (; periodFloor < segment.upperCents - epsilon; periodFloor += periodCents) {
                        const double periodCeiling = periodFloor + periodCents;
                        // Extend this segment's affine cents-to-y mapping to
                        // the full period. The global view mapping compresses
                        // gaps between hollow-stack bands and would deform a
                        // crescent whose hidden portion crosses such a gap.
                        const double periodTopY
                            = segTopY + (segment.upperCents - periodCeiling)
                              / StaffType::MELO_CENTS_PER_LINE_DISTANCE * dist;
                        const RectF outerArc(clefRight - clefRx, periodTopY, 2.0 * clefRx, 2.0 * clefRy);
                        const RectF innerArc(clefRight - clefRy, periodTopY, 2.0 * clefRy, 2.0 * clefRy);
                        PainterPath crescentFill;
                        crescentFill.arcMoveTo(outerArc, 90.0);
                        crescentFill.arcTo(outerArc, 90.0, 180.0);
                        crescentFill.arcTo(innerArc, 270.0, -180.0);
                        crescentFill.closeSubpath();
                        painter->setNoPen();
                        painter->setBrush(Brush(opt.invertColors && !opt.isPrinting ? Color::BLACK : Color::WHITE));
                        painter->drawPath(crescentFill);
                        PainterPath crescentOutline;
                        crescentOutline.arcMoveTo(outerArc, 90.0);
                        crescentOutline.arcTo(outerArc, 90.0, 180.0);
                        crescentOutline.arcMoveTo(innerArc, 270.0);
                        crescentOutline.arcTo(innerArc, 270.0, -180.0);
                        painter->setPen(Pen(item->curColor(opt), item->lw() * 1.5, PenStyle::SolidLine));
                        painter->setBrush(BrushStyle::NoBrush);
                        painter->drawPath(crescentOutline);

                        // The closure spans the glyph at the cut height —
                        // outer arc to inner arc — never the bounding box
                        // (owner correction 2026-08-14). Both arcs share the
                        // vertical semi-axis clefRy about the period middle.
                        painter->setPen(Pen(item->curColor(opt), item->lw() * 1.5,
                                            PenStyle::SolidLine, PenCapStyle::FlatCap));
                        const double arcCenterY = periodTopY + clefRy;
                        auto closeGlyph = [&](double yCut) {
                            double t = (yCut - arcCenterY) / clefRy;
                            double s = std::sqrt(std::max(0.0, 1.0 - t * t));
                            painter->drawLine(LineF(clefRight - clefRx * s, yCut,
                                                    clefRight - clefRy * s, yCut));
                        };
                        if (segment.upperCents > periodFloor + epsilon
                            && segment.upperCents < periodCeiling - epsilon) {
                            closeGlyph(segTopY);
                        }
                        if (segment.lowerCents > periodFloor + epsilon
                            && segment.lowerCents < periodCeiling - epsilon) {
                            closeGlyph(segBottomY);
                        }
                    }
                    painter->restore();
                }
            }

            // Milestone 8, owner ruling 3b (2026-08-18): a hollow stack follows
            // the keyboard precedent — one brace joins its bands at the system
            // head, drawn exactly as MuseScore draws a piano/harp brace
            // (TDraw::draw(const Bracket*), BracketType::BRACE): the SMuFL brace
            // glyph, x-magnified by the Bracket span rule and stretched to the
            // stack's height, its right edge akkoladeBarDistance before the
            // header. Printed ink, unlike the Phase-4 indicator below.
            if (view.bands.size() > 1 && headerGeom.braceWidth > 0.0 && font) {
                const double stackTopY = yOf(view.topCents());
                const double stackBottomY = yOf(view.bottomCents());
                const double h = stackBottomY - stackTopY;
                const double glyphHeight = item->symHeight(SymId::brace);
                if (h > 0.0 && glyphHeight > 0.0) {
                    const double magY = h / glyphHeight;
                    const double magX = headerGeom.braceMagX;
                    const double glyphW = item->symWidth(SymId::brace) * magX;
                    const double leftEdge = item->pos().x() - headerGeom.headerWidth;   // includes the brace band
                    painter->save();
                    painter->setPen(item->curColor(opt));
                    painter->translate(leftEdge, stackTopY);
                    painter->scale(magX, magY);
                    item->drawSymbol(SymId::brace, painter, PointF(0.0, glyphHeight));
                    painter->restore();
                    UNUSED(glyphW);
                }
            }

            // Milestone 8, Phase 4 (optional, owner plan §3.5; wording and
            // placement per owner finding 2026-08-18): a small SCREEN-ONLY
            // "N empty octaves hidden" text in the topmost gap of a banded
            // system head — the StaffVisibilityIndicator precedent
            // (TDraw::draw(const IndicatorIcon*)): never when printing, never
            // when the score hides unprintables, formatting colour. Its left
            // edge sits at the right edge of the scale-dot column, or of the
            // right-hand (Split-mode) label stack when one is shown. The count
            // is the Kernel's omitted-period count for this system; the
            // printed page keeps the gap free of any marker (owner ruling 2a).
            if (view.banded && view.bands.size() > 1 && view.omittedPeriodCount > 0
                && !opt.isPrinting && item->score()->showUnprintable()) {
                const StaffType::MeloFrameBand& upper = view.bands.back();          // topmost band
                const StaffType::MeloFrameBand& lower = view.bands[view.bands.size() - 2];
                const double gapTopY = topY + (upper.yTopLd + upper.heightLd()) * dist;
                const double gapBottomY = topY + lower.yTopLd * dist;
                const muse::String text = view.omittedPeriodCount == 1
                                          ? muse::String(u"1 empty octave hidden")
                                          : muse::String(u"%1 empty octaves hidden").arg(view.omittedPeriodCount);
                Font indicatorFont(u"Edwin", Font::Type::Text);
                indicatorFont.setPointSizeF(8.0 * item->spatium() / item->defaultSpatium());
                FontMetrics ifm(indicatorFont);
                const RectF tb = ifm.boundingRect(text);
                const double baseline = (gapTopY + gapBottomY) / 2.0 - (tb.top() + tb.bottom()) / 2.0;
                const double dotColumnRight = dotCenterX + indicatorW;
                const double indicatorLeft = dotColumnRight + headerGeom.rightLabelBand;   // 0 unless Split labels
                painter->setFont(indicatorFont);
                painter->setPen(Pen(item->configuration()->formattingColor()));
                painter->drawText(PointF(indicatorLeft, baseline), text);
            }
        }

        // Change indicator (Milestone 5, owner notation rulings 2026-08-16):
        // the Kernel's ready-to-paint terrain — dot stacks, tonic indicators,
        // note-class labels (left of the glyphs), arrows in their lane — is
        // painted verbatim between its two vertical flanks. Three placements:
        // (1) mid-system, at the START of the change measure
        // (barline left, added stroke right); (2) courtesy (owner 2026-08-16,
        // option 1a): when the change measure starts the NEXT system, at the
        // END of the last measure of this system (added stroke left, the
        // closing barline right); and (3) inside a bar, with two dashed grey
        // strokes. Nothing is inferred here.
        auto paintChangeTerrain = [&](const melo::ChangeIndicator& model, const StaffType* changeSt,
                                      const StaffType* displayedSt, double x0, ChangePlacement placement) {
            drawMeloChangeTerrain(item, painter, opt, model, changeSt, displayedSt, x0, placement);
        };
        if (meloSt && meloSt->isMelo()) {
            melo::ChangeIndicator model;
            const StaffType* changeSt = nullptr;
            if (!systemHead && melo::midSystemChangeIndicator(item->measure(), item->staffIdx(), model, &changeSt) && changeSt) {
                paintChangeTerrain(model, changeSt, changeSt, item->pos().x(), ChangePlacement::START_BAR);
            }
            for (const StaffTypeChange* carrier : melo::changeCarriers(item->measure(), item->staffIdx())) {
                melo::ChangeIndicator midBar;
                const StaffType* midBarSt = nullptr;
                if (!melo::midBarChangeIndicator(carrier, midBar, &midBarSt) || !midBarSt) {
                    continue;
                }
                const Segment* anchor = item->measure()->findSegmentR(
                    Segment::CHORD_REST_OR_TIME_TICK_TYPE, carrier->rtick());
                if (!anchor) {
                    continue;
                }
                // The terrain's width is the ONE shared calculation for THIS
                // indicator's lanes (owner finding 2026-09-12: the single-lane
                // header width put a two-arrow terrain's closing flank on the
                // note it precedes); horizontal spacing reserves the same width.
                const double g = melo::changeTerrainGeometry(
                    midBarSt, item->spatium(), item->score()->style().defaultSpatium(), midBar).changeTerrainWidth;
                const double noteGap = item->style().styleMM(Sid::barNoteDistance);
                paintChangeTerrain(midBar, midBarSt, meloSt, anchor->x() - g - noteGap, ChangePlacement::MID_BAR);
            }
            melo::ChangeIndicator courtesy;
            const StaffType* courtesySt = nullptr;
            if (melo::courtesyChangeIndicator(item->measure(), item->staffIdx(), courtesy, &courtesySt) && courtesySt) {
                const Segment* endBar = item->measure()->findSegmentR(SegmentType::EndBarLine, item->measure()->ticks());
                if (endBar) {
                    const StaffType* incoming = item->score()->staff(item->staffIdx())->staffType(
                        item->measure()->nextMeasure()->tick());
                    const double g = melo::changeTerrainGeometry(incoming, item->spatium(),
                                                                 item->score()->style().defaultSpatium(), courtesy).changeTerrainWidth;
                    paintChangeTerrain(courtesy, incoming, courtesySt, endBar->x() - g, ChangePlacement::END_BAR_COURTESY);
                }
            }
        }

        painter->restore();
        return;
    }

    painter->setPen(Pen(item->curColor(opt), item->lw(), PenStyle::SolidLine, PenCapStyle::FlatCap));
    painter->drawLines(item->lines());

    painter->restore();
}

void TDraw::draw(const StaffState* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    if (opt.isPrinting || !item->score()->showUnprintable()) {
        return;
    }

    const StaffState::LayoutData* ldata = item->ldata();
    auto conf = item->configuration();

    Pen pen(item->selected() ? conf->selectionColor() : conf->formattingColor(),
            ldata->lw, PenStyle::SolidLine, PenCapStyle::RoundCap, PenJoinStyle::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(BrushStyle::NoBrush);
    painter->drawPath(ldata->path);
}

void TDraw::draw(const StaffText* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    drawTextBase(item, painter, opt);

    if (item->hasSoundFlag()) {
        draw(item->soundFlag(), painter, opt);
    }
}

void TDraw::draw(const StaffTypeChange* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    if (opt.isPrinting || !item->score()->showUnprintable()) {
        return;
    }

    auto conf = item->configuration();

    double _spatium = item->style().spatium();
    double h  = _spatium * 2.5;
    double w  = _spatium * 2.5;
    double lineDist = 0.35;           // line distance for the icon 'staff lines'
    // draw icon rectangle
    painter->setPen(Pen(item->selected() ? conf->selectionColor() : conf->formattingColor(),
                        item->lw(), PenStyle::SolidLine, PenCapStyle::SquareCap, PenJoinStyle::MiterJoin));
    painter->setBrush(BrushStyle::NoBrush);
    painter->drawRect(0, 0, w, h);

    // draw icon contents
    int lines = 5;
    if (item->staffType()) {
        if (item->staffType()->stemless()) {       // a single notehead represents a stemless staff
            item->drawSymbol(SymId::noteheadBlack, painter, PointF(w * 0.5 - 0.33 * _spatium, h * 0.5), 0.5);
        }
        if (item->staffType()->invisible()) {      // no lines needed. It's done.
            return;
        }
        // show up to 6 lines
        lines = std::min(item->staffType()->lines(), 6);
    }
    // calculate starting point Y for the lines from half the icon height (2.5) so staff lines appear vertically centered
    double startY = 1.25 - (lines - 1) * lineDist * 0.5;
    painter->setPen(Pen(item->selected() ? conf->selectionColor() : conf->formattingColor(),
                        2.5, PenStyle::SolidLine, PenCapStyle::SquareCap, PenJoinStyle::MiterJoin));
    for (int i=0; i < lines; i++) {
        int y = (startY + i * lineDist) * _spatium;
        painter->drawLine(0, y, w, y);
    }
}

void TDraw::draw(const Stem* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    if (!item->chord()) { // may be need assert?
        return;
    }

    // hide if second chord of a cross-measure pair
    if (item->chord()->crossMeasure() == CrossMeasure::SECOND) {
        return;
    }

    const Stem::LayoutData* ldata = item->ldata();

    const Staff* staff = item->staff();
    const StaffType* staffType = staff ? staff->staffTypeForElement(item->chord()) : nullptr;
    const bool isTablature = staffType && staffType->isTabStaff();

    painter->setPen(Pen(item->curColor(opt), item->lineWidthMag(), PenStyle::SolidLine, PenCapStyle::FlatCap));
    painter->drawLine(ldata->line);

    if (!isTablature) {
        return;
    }

    // TODO: adjust bounding rectangle in layout() for dots and for slash
    double sp = item->spatium();
    bool isUp = item->up();

    // slashed half note stem
    if (item->chord()->durationType().type() == DurationType::V_HALF
        && staffType->minimStyle() == TablatureMinimStyle::SLASHED) {
        // position slashes onto stem
        double y = isUp ? -item->length() + StemLayout::STAFFTYPE_TAB_SLASH_2STARTY_UP * sp
                   : item->length() - StemLayout::STAFFTYPE_TAB_SLASH_2STARTY_DN * sp;
        // if stems through, try to align slashes within or across lines
        if (staffType->stemThrough()) {
            double halfLineDist = staffType->lineDistance().val() * sp * 0.5;
            double halfSlashHgt = StemLayout::STAFFTYPE_TAB_SLASH_2TOTHEIGHT * sp * 0.5;
            y = lrint((y + halfSlashHgt) / halfLineDist) * halfLineDist - halfSlashHgt;
        }
        // draw slashes
        double hlfWdt= sp * StemLayout::STAFFTYPE_TAB_SLASH_WIDTH * 0.5;
        double sln   = sp * StemLayout::STAFFTYPE_TAB_SLASH_SLANTY;
        double thk   = sp * StemLayout::STAFFTYPE_TAB_SLASH_THICK;
        double displ = sp * StemLayout::STAFFTYPE_TAB_SLASH_DISPL;
        PainterPath path;
        for (int i = 0; i < 2; ++i) {
            path.moveTo(hlfWdt, y);                   // top-right corner
            path.lineTo(hlfWdt, y + thk);             // bottom-right corner
            path.lineTo(-hlfWdt, y + thk + sln);      // bottom-left corner
            path.lineTo(-hlfWdt, y + sln);            // top-left corner
            path.closeSubpath();
            y += displ;
        }
        painter->setBrush(Brush(item->curColor(opt)));
        painter->setNoPen();
        painter->drawPath(path);
    }

    // dots
    // NOT THE BEST PLACE FOR THIS?
    // with tablatures and stems beside staves, dots are not drawn near 'notes', but near stems
    int nDots = item->chord()->dots();
    if (nDots > 0 && !staffType->stemThrough()) {
        double x     = item->chord()->dotPosX();
        double y     = ((StemLayout::STAFFTYPE_TAB_DEFAULTSTEMLEN_DN * 0.2) * sp) * (isUp ? -1.0 : 1.0);
        double step  = item->style().styleS(Sid::dotDotDistance).val() * sp;
        for (int dot = 0; dot < nDots; dot++, x += step) {
            item->drawSymbol(SymId::augmentationDot, painter, PointF(x, y));
        }
    }
}

void TDraw::draw(const StemSlash* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    const StemSlash::LayoutData* ldata = item->ldata();
    painter->setPen(Pen(item->curColor(opt), ldata->stemWidth, PenStyle::SolidLine, PenCapStyle::FlatCap));
    painter->drawLine(ldata->line);
}

void TDraw::draw(const Sticking* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const StringTunings* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    if (item->noStringVisible()) {
        const TextBase::LayoutData* data = item->ldata();

        const double spatium = item->spatium();
        const double lineWidth = spatium * .15;

        const Pen pen(item->curColor(opt), lineWidth, PenStyle::SolidLine, PenCapStyle::RoundCap, PenJoinStyle::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Brush(item->curColor(opt)));

        const RectF rect = data->bbox();

        const double x = rect.x();
        const double y = rect.y();
        const double width = rect.width();
        const double height = rect.height();
        const double topPartHeight = height * .66;
        const double cornerRadius = height * .1;

        PainterPath path;
        path.moveTo(x, y);
        path.arcTo(x, y + (topPartHeight - 2 * cornerRadius), 2 * cornerRadius, 2 * cornerRadius, 180.0, 90.0);
        path.arcTo(x + width - 2 * cornerRadius, y + (topPartHeight - 2 * cornerRadius), 2 * cornerRadius, 2 * cornerRadius, 270, 90);
        path.lineTo(x + width, y);
        path.moveTo(x + width / 2, y + topPartHeight);
        path.lineTo(x + width / 2, y + height);

        painter->setBrush(BrushStyle::NoBrush);
        painter->drawPath(path);
    } else {
        drawTextBase(item, painter, opt);
    }
}

void TDraw::draw(const Symbol* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    bool tabStaff = item->staff() ? item->staff()->isTabStaff(item->tick()) : false;

    if (!item->isNoteDot() || !tabStaff) {
        painter->setPen(item->curColor(opt));
        if (item->scoreFont()) {
            item->scoreFont()->draw(item->sym(), painter, item->magS() * item->symbolsSize(), PointF(), item->symAngle());
        } else {
            item->drawSymbol(item->sym(), painter);
        }
    }
}

void TDraw::draw(const FSymbol* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    painter->setFont(item->font());
    painter->setPen(item->curColor(opt));
    painter->drawText(PointF(0, 0), item->toString());
}

void TDraw::draw(const SystemDivider* item, Painter* painter, const PaintOptions& opt)
{
    draw(static_cast<const Symbol*>(item), painter, opt);
}

void TDraw::draw(const SystemText* item, Painter* painter, const PaintOptions& opt)
{
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const IndicatorIcon* item, muse::draw::Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    if (opt.isPrinting || !item->score()->showUnprintable()) {
        return;
    }

    Pen pen(item->selected() ? item->configuration()->selectionColor() : item->configuration()->formattingColor());
    painter->setPen(pen);
    painter->setFont(item->font());
    painter->drawSymbol(PointF(), item->iconCode());

    if (item->isSystemLockIndicator() && item->selected()) {
        const SystemLockIndicator* sli = toSystemLockIndicator(item);

        Color lockedAreaColor = sli->configuration()->selectionColor();
        lockedAreaColor.setAlpha(38);
        Brush brush(lockedAreaColor);
        painter->setBrush(brush);
        painter->setNoPen();
        double radius = 0.5 * sli->spatium();

        painter->drawRoundedRect(sli->ldata()->rangeRect, radius, radius);
    }
}

void TDraw::draw(const SoundFlag* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    if (item->shouldHide()) {
        return;
    }

    painter->setNoPen();
    painter->setBrush(item->iconBackgroundColor());
    painter->drawEllipse(item->ldata()->bbox().adjusted(-4.0, -4.0, 4.0, 4.0));

    painter->setFont(item->iconFont());
    painter->setPen(!item->selected() ? item->curColor(true, opt) : Color::WHITE);
    painter->drawText(item->ldata()->bbox(), muse::draw::AlignCenter, Char(item->iconCode()));
}

void TDraw::draw(const TabDurationSymbol* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    if (!item->tab()) {
        return;
    }

    const TabDurationSymbol::LayoutData* ldata = item->ldata();

    if (item->isRepeat() && (item->tab()->symRepeat() == TablatureSymbolRepeat::SYSTEM)) {
        Chord* chord = toChord(item->explicitParent());
        ChordRest* prevCR = prevChordRest(chord);
        if (prevCR && (chord->measure()->system() == prevCR->measure()->system())) {
            return;
        }
    }

    double mag = item->magS();
    double imag = 1.0 / mag;

    Pen pen(item->curColor(opt));
    painter->setPen(pen);
    painter->scale(mag, mag);
    if (ldata->beamGrid == TabBeamGrid::NONE) {
        // if no beam grid, draw symbol
        painter->setFont(item->tab()->durationFont());
        painter->drawText(PointF(0.0, 0.0), item->text());
    } else {
        // if beam grid, draw stem line
        const TablatureDurationFont& font = item->tab()->tabDurationFont();
        double _spatium = item->spatium();
        pen.setCapStyle(PenCapStyle::FlatCap);
        pen.setWidthF(font.gridStemWidth * _spatium);
        painter->setPen(pen);
        // take stem height from bbox, but de-magnify it, as drawing is already magnified
        double h = ldata->bbox().y() / mag;
        painter->drawLine(PointF(0.0, h), PointF(0.0, 0.0));
        // if beam grid is medial/final, draw beam lines too: lines go from mid of
        // previous stem (delta x stored in _beamLength) to mid of this' stem (0.0)
        if (ldata->beamGrid == TabBeamGrid::MEDIALFINAL) {
            pen.setWidthF(font.gridBeamWidth * _spatium);
            painter->setPen(pen);
            // lower height available to beams by half a beam width,
            // so that top beam upper border aligns with stem top
            h += (font.gridBeamWidth * _spatium) * 0.5;
            // draw beams equally spaced within the stem height (this is
            // different from modern engraving, but common in historic prints)
            double step  = -h / ldata->beamLevel;
            double y     = h;
            for (int i = 0; i < ldata->beamLevel; i++, y += step) {
                painter->drawLine(PointF(ldata->beamLength, y), PointF(0.0, y));
            }
        }
    }
    painter->scale(imag, imag);
}

void TDraw::draw(const Tapping* item, muse::draw::Painter* painter, const PaintOptions& opt)
{
    painter->setPen(item->curColor(opt));
    if (item->ldata()->symId != SymId::noSym) {
        item->drawSymbol(item->ldata()->symId, painter);
    } else if (TappingText* text = item->text()) {
        painter->translate(text->pos());
        drawTextBase(text, painter, opt);
        painter->translate(-text->pos());
    } else {
        assert(false && "Drawing Tapping item without text or symbol");
    }
}

void TDraw::draw(const TempoText* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const Text* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const TextLineSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextLineBaseSegment(item, painter, opt);
}

void TDraw::draw(const TieSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    // hide tie toward the second chord of a cross-measure value
    if (item->tie()->endNote() && item->tie()->endNote()->chord()->crossMeasure() == CrossMeasure::SECOND) {
        return;
    }

    Color penColor = item->curColor(item->getProperty(Pid::VISIBLE).toBool(), item->getProperty(Pid::COLOR).value<Color>(), opt);
    if (!opt.isPrinting && item->ldata()->allJumpPointsInactive) {
        penColor.setAlpha(std::min(penColor.alpha(), 85));
    }

    Pen pen(penColor);
    double mag = item->staff() ? item->staff()->staffMag(item->tie()->tick()) : 1.0;
    //Replace generic Qt dash patterns with improved equivalents to show true dots (keep in sync with slur.cpp)
    std::vector<double> dotted     = { 0.01, 1.99 };   // tighter than Qt PenStyle::DotLine equivalent - would be { 0.01, 2.99 }
    std::vector<double> dashed     = { 3.00, 3.00 };   // Compensating for caps. Qt default PenStyle::DashLine is { 4.0, 2.0 }
    std::vector<double> wideDashed = { 5.00, 6.00 };

    switch (item->slurTie()->styleType()) {
    case SlurStyleType::Solid:
        painter->setBrush(Brush(pen.color()));
        pen.setCapStyle(PenCapStyle::RoundCap);
        pen.setJoinStyle(PenJoinStyle::RoundJoin);
        pen.setWidthF(item->endWidth() * mag);
        break;
    case SlurStyleType::Dotted:
        painter->setBrush(BrushStyle::NoBrush);
        pen.setCapStyle(PenCapStyle::RoundCap);           // True dots
        pen.setDashPattern(dotted);
        pen.setWidthF(item->dottedWidth() * mag);
        break;
    case SlurStyleType::Dashed:
        painter->setBrush(BrushStyle::NoBrush);
        pen.setDashPattern(dashed);
        pen.setWidthF(item->dottedWidth() * mag);
        break;
    case SlurStyleType::WideDashed:
        painter->setBrush(BrushStyle::NoBrush);
        pen.setDashPattern(wideDashed);
        pen.setWidthF(item->dottedWidth() * mag);
        break;
    case SlurStyleType::Undefined:
        break;
    }
    painter->setPen(pen);
    painter->drawPath(item->ldata()->path());
}

void TDraw::draw(const TimeSig* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    if (item->staff() && !const_cast<const Staff*>(item->staff())->staffType(item->tick())->genTimesig()) {
        return;
    }
    if (!item->showOnThisStaff()) {
        return;
    }
    painter->setPen(item->curColor(opt));

    const TimeSig::LayoutData* ldata = item->ldata();

    item->drawSymbols(ldata->ns, painter, ldata->pz, item->scale());
    item->drawSymbols(ldata->ds, painter, ldata->pn, item->scale());

    if (item->largeParentheses()) {
        item->drawSymbol(SymId::timeSigParensLeft,  painter, ldata->pointLargeLeftParen,  item->scale().width());
        item->drawSymbol(SymId::timeSigParensRight, painter, ldata->pointLargeRightParen, item->scale().width());
    }
}

void TDraw::draw(const TimeTickAnchor* item, Painter* painter, const PaintOptions& opt)
{
    if (opt.isPrinting) {
        return;
    }

    TimeTickAnchor::DrawRegion drawRegion = item->drawRegion();

    if (drawRegion == TimeTickAnchor::DrawRegion::OUT_OF_RANGE) {
        return;
    }

    Color voiceColor = item->configuration()->voiceColor(item->voiceIdx());

    static constexpr double TINT_MAIN_DARKER = 0.45;
    static constexpr double TINT_MAIN_LIGHTER = 0.55;
    static constexpr double TINT_EXTENDED_DARKER = 0.75;
    static constexpr double TINT_EXTENDED_LIGHTER = 0.85;

    double tint = drawRegion == TimeTickAnchor::DrawRegion::MAIN_REGION
                  ? item->ldata()->darker() ? TINT_MAIN_DARKER : TINT_MAIN_LIGHTER
                  : item->ldata()->darker() ? TINT_EXTENDED_DARKER : TINT_EXTENDED_LIGHTER;

    voiceColor.applyTint(tint);

    Brush brush;
    brush.setColor(voiceColor);
    brush.setStyle(BrushStyle::SolidPattern);
    painter->setBrush(brush);
    painter->setNoPen();

    painter->drawRect(item->ldata()->bbox());
}

void TDraw::draw(const TremoloSingleChord* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    if (item->isBuzzRoll()) {
        painter->setPen(item->curColor(opt));
        item->drawSymbol(SymId::buzzRoll, painter);
    } else {
        painter->setBrush(Brush(item->curColor(opt)));
        painter->setNoPen();
        painter->drawPath(item->path());
    }
}

void TDraw::draw(const TremoloTwoChord* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    const TremoloTwoChord::LayoutData* ldata = item->ldata();

    if (!item->beamSegments().empty()) {
        // two-note trems act like beams

        // make beam thickness independent of slant
        // (expression can be simplified?)
        const LineF bs = item->beamSegments().front()->line;
        double d = (std::abs(bs.y2() - bs.y1())) / (bs.x2() - bs.x1());
        if (item->beamSegments().size() > 1 && d > M_PI / 6.0) {
            d = M_PI / 6.0;
        }
        double ww = (ldata->beamWidth / 2.0) / sin(M_PI_2 - atan(d));
        painter->setBrush(Brush(item->curColor(opt)));
        painter->setNoPen();
        for (const BeamSegment* bs1 : item->beamSegments()) {
            painter->drawPolygon(
                PolygonF({
                PointF(bs1->line.x1(), bs1->line.y1() - ww),
                PointF(bs1->line.x2(), bs1->line.y2() - ww),
                PointF(bs1->line.x2(), bs1->line.y2() + ww),
                PointF(bs1->line.x1(), bs1->line.y1() + ww),
            }),
                FillRule::OddEvenFill);
        }
    }
}

void TDraw::draw(const TremoloBar* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    const TremoloBar::LayoutData* ldata = item->ldata();
    const double lw = item->absoluteFromSpatium(item->lineWidth());
    Pen pen(item->curColor(opt), lw, PenStyle::SolidLine, PenCapStyle::RoundCap, PenJoinStyle::RoundJoin);
    painter->setPen(pen);
    painter->drawPolyline(ldata->polygon);
}

void TDraw::draw(const TrillSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    painter->setPen(item->spanner()->curColor(opt));
    item->drawSymbols(item->symbols(), painter);
}

void TDraw::draw(const TripletFeel* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextBase(item, painter, opt);
}

void TDraw::draw(const Tuplet* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;

    // if in a TAB without stems, tuplets are not shown
    const StaffType* stt = item->staffType();
    if (stt && stt->isTabStaff() && stt->stemless()) {
        return;
    }

    Color color(item->curColor(opt));
    if (item->number()) {
        painter->setPen(color);
        PointF pos(item->number()->pos());
        painter->translate(pos);
        draw(item->number(), painter, opt);
        painter->translate(-pos);
    }
    if (item->hasBracket()) {
        Pen pen(color, item->absoluteFromSpatium(item->bracketWidth()));
        pen.setJoinStyle(PenJoinStyle::MiterJoin);
        pen.setCapStyle(PenCapStyle::FlatCap);
        painter->setPen(pen);
        if (!item->number()) {
            painter->drawPolyline(item->bracketL, 4);
        } else {
            painter->drawPolyline(item->bracketL, 3);
            painter->drawPolyline(item->bracketR, 3);
        }
    }
}

void TDraw::draw(const VibratoSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    painter->setPen(item->spanner()->curColor(opt));
    item->drawSymbols(item->symbols(), painter);
}

void TDraw::draw(const VoltaSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextLineBaseSegment(item, painter, opt);
}

void TDraw::draw(const WhammyBarSegment* item, Painter* painter, const PaintOptions& opt)
{
    TRACE_DRAW_ITEM;
    drawTextLineBaseSegment(item, painter, opt);
}

void TDraw::setMask(const EngravingItem* item, Painter* painter)
{
    const EngravingItem::LayoutData* ldata = item->ldata();

    const Shape& mask = ldata->mask();
    if (mask.empty()) {
        return;
    }

    RectF background = ldata->bbox().padded(item->spatium());

    painter->setMask(background, mask.toRects());
}
