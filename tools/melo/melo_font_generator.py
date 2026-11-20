#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jim Plamondon
# SPDX-License-Identifier: Apache-2.0
"""Deterministic MeloPresto Music font generator: a SMuFL-subset font
(MeloStaff Milestone 1).

Consumes ONLY the rights-clean generated glyph SVGs from the MeloPresto Kernel
(melo-staff/assets/glyphs, themselves GEN-003 generated artifacts) and
emits a small OpenType font plus SMuFL-style metadata. No third-party
outline is read, traced, or embedded. Deterministic: fixed epoch dates,
sorted tables, no randomness — rebuilds are byte-identical.

Usage:
  python3 melo_font_generator.py <glyph_svg_dir> <out_dir>

Outputs <out_dir>/MeloPrestoMusic.otf and <out_dir>/MeloPrestoMusic-metadata.json,
which are installed as fonts/melopresto-music/MeloPrestoMusic.otf and
fonts/melopresto-music/melopresto-music_metadata.json.

Names: the family and full name are "MeloPresto Music"; the PostScript and
CFF font names, which cannot hold a space, use MeloPrestoMusic.
"""
import json
import math
import os
import re
import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.t2CharStringPen import T2CharStringPen

FAMILY = "MeloPresto Music"  # family and full name
POSTSCRIPT_NAME = "MeloPrestoMusic"  # where a name cannot hold a space
UPM = 1000  # font units per em; the SVG design grid is also 1000.

# Design-grid mapping: the SVG grid is 1000x1000, y-down, glyphs centered
# at (500, 500). A notehead occupies one 100-cent staff location whose
# engraved height equals one staff space (JiMS: 1 sp per 100 cents). In
# font units, 1 sp = UPM/4 = 250 (SMuFL convention: staff space = 0.25 em).
# The settled notehead band is ~400 design units tall, so scale
# 400 design units -> 250 font units, centered on y=0 (the glyph is placed
# on its staff location's center line).
SCALE = 250.0 / 400.0
CENTER = 500.0


def to_font(x, y):
    """SVG design grid (y down, centered 500,500) -> font units (y up)."""
    return ((x - CENTER) * SCALE, (CENTER - y) * SCALE)


def arc_to_beziers(x1, y1, rx, ry, large_arc, sweep, x2, y2):
    """Convert one SVG elliptical arc (no rotation) to cubic beziers.

    Standard endpoint->center parameterization from the SVG 1.1 spec
    (implementation notes B.2.4), public-domain math.
    """
    if rx == 0 or ry == 0:
        return [((x1, y1), (x2, y2), (x2, y2))]
    dx2, dy2 = (x1 - x2) / 2.0, (y1 - y2) / 2.0
    x1p, y1p = dx2, dy2
    rc = x1p ** 2 / rx ** 2 + y1p ** 2 / ry ** 2
    if rc > 1:
        rx, ry = rx * math.sqrt(rc), ry * math.sqrt(rc)
    num = rx ** 2 * ry ** 2 - rx ** 2 * y1p ** 2 - ry ** 2 * x1p ** 2
    den = rx ** 2 * y1p ** 2 + ry ** 2 * x1p ** 2
    co = math.sqrt(max(0.0, num / den)) * (-1 if large_arc == sweep else 1)
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx, cy = cxp + (x1 + x2) / 2.0, cyp + (y1 + y2) / 2.0

    def angle(ux, uy, vx, vy):
        dot = ux * vx + uy * vy
        norm = math.hypot(ux, uy) * math.hypot(vx, vy)
        a = math.acos(max(-1.0, min(1.0, dot / norm)))
        return -a if ux * vy - uy * vx < 0 else a

    theta1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dtheta = angle((x1p - cxp) / rx, (y1p - cyp) / ry,
                   (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dtheta > 0:
        dtheta -= 2 * math.pi
    elif sweep and dtheta < 0:
        dtheta += 2 * math.pi

    segments = max(1, int(math.ceil(abs(dtheta) / (math.pi / 2))))
    delta = dtheta / segments
    t = 4.0 / 3.0 * math.tan(delta / 4.0)
    beziers = []
    for i in range(segments):
        a1 = theta1 + i * delta
        a2 = a1 + delta
        c1, s1, c2, s2 = math.cos(a1), math.sin(a1), math.cos(a2), math.sin(a2)
        p1 = (cx + rx * c1, cy + ry * s1)
        p4 = (cx + rx * c2, cy + ry * s2)
        q1 = (p1[0] - t * rx * s1, p1[1] + t * ry * c1)
        q2 = (p4[0] + t * rx * s2, p4[1] - t * ry * c2)
        beziers.append((q1, q2, p4))
    return beziers


TOKEN = re.compile(r"([MLAZz])|(-?\d+(?:\.\d+)?)")


def parse_path(d):
    """Parse the generator SVGs' small path dialect (M, L, A, Z, absolute)."""
    tokens = [m.group(0) for m in TOKEN.finditer(d)]
    contours, current, cursor = [], [], (0.0, 0.0)
    i = 0
    while i < len(tokens):
        op = tokens[i]
        if op == "M":
            if current:
                contours.append(current)
            cursor = (float(tokens[i + 1]), float(tokens[i + 2]))
            current = [("moveTo", cursor)]
            i += 3
        elif op == "L":
            cursor = (float(tokens[i + 1]), float(tokens[i + 2]))
            current.append(("lineTo", cursor))
            i += 3
        elif op == "A":
            rx, ry = float(tokens[i + 1]), float(tokens[i + 2])
            large, sweep = bool(float(tokens[i + 4])), bool(float(tokens[i + 5]))
            end = (float(tokens[i + 6]), float(tokens[i + 7]))
            for q1, q2, p4 in arc_to_beziers(*cursor, rx, ry, large, sweep, *end):
                current.append(("curveTo", (q1, q2, p4)))
            cursor = end
            i += 8
        elif op in "Zz":
            current.append(("closePath", None))
            i += 1
        else:
            raise ValueError(f"unsupported path token {op!r}")
    if current:
        contours.append(current)
    return contours


def draw_commands(pen, contours, scale_about=None, offset_radius=0.0):
    """Replay parsed contours into a pen, in font coordinates.

    offset_radius grows (positive) or shrinks (negative) the contour
    radially about its bounding-box center — the stroked-outline
    approximation for this dialect's convex shapes.
    """
    for contour in contours:
        points = [p for op, p in contour if op in ("moveTo", "lineTo")]
        arcs = [p for op, p in contour if op == "curveTo"]
        if offset_radius and points:
            cx = sum(p[0] for p in points) / len(points)
            cy = sum(p[1] for p in points) / len(points)

            def out(p):
                dx, dy = p[0] - cx, p[1] - cy
                length = math.hypot(dx, dy) or 1.0
                grow = (length + offset_radius) / length
                return (cx + dx * grow, cy + dy * grow)
        else:
            def out(p):
                return p
        for op, payload in contour:
            if op == "moveTo":
                pen.moveTo(to_font(*out(payload)))
            elif op == "lineTo":
                pen.lineTo(to_font(*out(payload)))
            elif op == "curveTo":
                q1, q2, p4 = payload
                pen.curveTo(to_font(*out(q1)), to_font(*out(q2)), to_font(*out(p4)))
            elif op == "closePath":
                pen.closePath()


def read_svg_paths(path):
    text = open(path, encoding="utf-8").read()
    out = []
    for m in re.finditer(r"<path([^>]*)/?>", text):
        attrs = m.group(1)
        d = re.search(r'd="([^"]*)"', attrs)
        fill = re.search(r'fill="([^"]*)"', attrs)
        stroke = re.search(r'stroke-width="([^"]*)"', attrs)
        out.append({
            "d": d.group(1),
            "fill": fill.group(1) if fill else "black",
            "stroke_width": float(stroke.group(1)) if stroke else 0.0,
        })
    return out


# Glyph plan: SVG file -> (glyph name, codepoint, SMuFL name). Noteheads
# land on the standard SMuFL codepoints MuseScore's SymId table already
# consumes; the crescent clef and tonic indicator take SMuFL's unused
# schaefferClefs range neighbors in the private-buildable region.
GLYPHS = [
    ("notehead-sharp.svg", "noteheadTriangleUpBlack", 0xE0BE),
    ("notehead-flat.svg", "noteheadTriangleDownBlack", 0xE0C7),
    ("notehead-double-sharp.svg", "noteheadDiamondBlack", 0xE0DB),
    ("notehead-double-flat.svg", "noteheadSquareBlack", 0xE0B9),
    ("crescent-clef.svg", "meloCrescentClef", 0xF400),
    ("tonic-indicator.svg", "meloTonicIndicator", 0xF401),
]


def build(svg_dir, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    fb = FontBuilder(UPM, isTTF=False)
    names = [".notdef"] + [name for _, name, _ in GLYPHS]
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap({cp: name for _, name, cp in GLYPHS})

    charstrings = {}
    advances = {}
    pen = T2CharStringPen(0, None)
    charstrings[".notdef"] = pen.getCharString()
    advances[".notdef"] = (0, 0)

    from fontTools.pens.boundsPen import BoundsPen
    from fontTools.pens.recordingPen import RecordingPen

    metadata_glyphs = {}
    metrics = {".notdef": (250, 0)}
    for filename, name, cp in GLYPHS:
        recording = RecordingPen()
        for path in read_svg_paths(os.path.join(svg_dir, filename)):
            contours = parse_path(path["d"])
            if path["stroke_width"] and path["fill"] in ("none", "white"):
                half = path["stroke_width"] / 2.0
                draw_commands(recording, contours, offset_radius=+half)
                draw_commands(recording, contours, offset_radius=-half)
            else:
                draw_commands(recording, contours)
        pen = T2CharStringPen(0, None)
        recording.replay(pen)
        charstrings[name] = pen.getCharString()
        bounds = BoundsPen(None)
        recording.replay(bounds)
        if bounds.bounds:
            x_min, y_min, x_max, y_max = bounds.bounds
            metrics[name] = (int(round(x_max - x_min)), int(round(x_min)))
        else:
            x_min = y_min = 0.0
            x_max = y_max = 250.0
            metrics[name] = (250, 0)
        metadata_glyphs[name] = {"codepoint": f"U+{cp:04X}"}
        # SMuFL stem anchors (owner correction 2026-08-14): a stem
        # attaches at the head's edge, never its center. Units are staff
        # spaces (1 sp = UPM/4), y-up, relative to the glyph origin.
        # Triangles attach at their base corners; the vertex-up square
        # (diamond) and the edge-up square attach at their widest side.
        sp = UPM / 4.0
        anchor_y = {
            "noteheadTriangleUpBlack": y_min,
            "noteheadTriangleDownBlack": y_max,
            "noteheadDiamondBlack": (y_min + y_max) / 2.0,
            "noteheadSquareBlack": (y_min + y_max) / 2.0,
        }.get(name)
        if anchor_y is not None:
            metadata_glyphs[name]["_anchors"] = {
                "stemUpSE": [round(x_max / sp, 4), round(anchor_y / sp, 4)],
                "stemDownNW": [round(x_min / sp, 4), round(anchor_y / sp, 4)],
            }

    fb.setupCFF(POSTSCRIPT_NAME, {"FamilyName": FAMILY, "FullName": FAMILY},
                charstrings, {})
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=UPM, descent=-UPM // 4)
    fb.setupNameTable({
        "familyName": FAMILY,
        "styleName": "Regular",
        "uniqueFontIdentifier": "MeloPrestoMusic-1.0-melostaff-m1",
        "fullName": FAMILY,
        "version": "Version 1.0",
        "psName": POSTSCRIPT_NAME + "-Regular",
        "copyright": "SPDX-FileCopyrightText: 2026 Jim Plamondon; SPDX-License-Identifier: OFL-1.1",
        "licenseDescription": "SIL Open Font License 1.1",
    })
    fb.setupOS2(sTypoAscender=UPM, sTypoDescender=-UPM // 4, usWinAscent=UPM,
                usWinDescent=UPM // 4)
    fb.setupPost()
    # Determinism: pin every date the format carries.
    fb.font["head"].created = 0
    fb.font["head"].modified = 0

    otf_path = os.path.join(out_dir, POSTSCRIPT_NAME + ".otf")
    fb.save(otf_path)

    glyphs_with_anchors = {}
    for name, info in metadata_glyphs.items():
        anchors = info.pop("_anchors", None)
        if anchors:
            glyphs_with_anchors[name] = anchors
    metadata = {
        "fontName": FAMILY,
        "fontVersion": "1.0",
        "engravingDefaults": {"staffLineThickness": 0.1},
        "glyphsWithAnchors": glyphs_with_anchors,
        "glyphs": metadata_glyphs,
        "provenance": "Generated from melo-staff/assets/glyphs (GEN-003); no third-party outlines.",
    }
    meta_path = os.path.join(out_dir, POSTSCRIPT_NAME + "-metadata.json")
    with open(meta_path, "w", encoding="utf-8") as f:
        json.dump(metadata, f, indent=2, sort_keys=True)
        f.write("\n")
    return otf_path, meta_path


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    otf, meta = build(sys.argv[1], sys.argv[2])
    print(otf)
    print(meta)
