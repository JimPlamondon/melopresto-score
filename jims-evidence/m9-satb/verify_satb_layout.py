#!/usr/bin/env python3
"""JiMStaff Milestone 9 — SATB open score: the committed pixel verifier.

Turns the milestone's visual claim into a machine assertion against live
renders produced by render_evidence.sh. Prints a machine-readable verdict on
stdout and exits non-zero if any assertion fails.

The load-bearing assertion is the broken barline. MuseScore's octavo
convention — and the fork's own ScoreOrder rule for the `voices` section — is
that a barline stroke is drawn inside each vocal staff and NOT between vocal
staves. Code inspection cannot show that; this does, from the rendered page:

    every measure barline of a system carries a stroke inside each of that
    system's four staff frames, and ZERO ink in any of its three inter-staff
    gaps.

Two things are deliberately NOT counted as measure barlines.

  * The system's own left edge. MuseScore draws a bracketed system's leading
    vertical full height, through the gaps, and the STOCK 02-Choral/01-SATB
    template renders identically (its system edge is one continuous run; its
    measure barlines are four separate runs per system). It is system
    furniture, not a measure division, so it is reported separately rather
    than asserted on.
  * Anything that is not a barline: lyrics, note heads, stems, dynamics. These
    legitimately occupy the space between staves, so the gap assertion is made
    per barline column, never over the whole gap area.

Also asserted: repeated renders of one fixture are byte-identical, every
render matches SHA256SUMS, and each rendered system draws four JiMStaff frames.
A system is what its leading edge spans (system_spans): since the staff-edge
fix accepted on 2026-09-08 an empty tonic-bounded staff is a Do-to-So frame,
shorter than the space between staves, so systems can no longer be told apart
by an outsized gap alone.

Usage:
    .venv/bin/python jims-evidence/m9-satb/verify_satb_layout.py \\
        > jims-evidence/m9-satb/verify-summary.json
"""

import hashlib
import json
import pathlib
import sys

import numpy as np
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
RENDERS = HERE / "renders"

# A pixel is "ink" when it is dark in all three channels. The JiMS staff lines
# are saturated red / green / purple and never qualify; barlines, note heads,
# stems, text and the bracket do.
INK_MAX = 100
# Vertical ink runs shorter than this are note heads, stems, text — not a
# barline stroke through a staff. A JiMStaff frame at 120 dpi is ~100 px.
MIN_STROKE = 40
# A staff frame's edge, measured by consensus, can sit a pixel or two off the
# end of any one barline stroke (anti-aliasing), and since the staff-edge fix
# accepted on 2026-09-08 an empty tonic-bounded staff's lower edge IS its red
# Do line, over which the stroke ends. Rows this close to a frame edge are
# therefore not part of the inter-staff gap.
EDGE_SLACK = 4
# Runs this close together are one stroke. A JiMStaff draws its coloured staff
# lines OVER the barline, so a barline is interrupted wherever a line crosses
# it: measured at 2 replaced pixels (the red Do line, RGB 195,41,41, at
# y=173-174 of hymn-p1.png), which is a 3-pixel step between run ends. Four is
# the smallest value that reads such a barline as the single stroke it is,
# and it is far below the ~120-pixel gaps between staves.
RUN_JOIN = 4
# A barline column must be inked over at least this much of each staff frame.
BARLINE_FILL = 0.80
# Voices per system in an SATB open score.
VOICES_PER_SYSTEM = 4


def ink_of(path):
    im = np.asarray(Image.open(path).convert("RGB")).astype(np.int16)
    return im.max(2) < INK_MAX


def runs_in_column(ink, x, min_len):
    """Maximal vertical ink runs in column x, as (top, bottom) pairs."""
    ys = np.flatnonzero(ink[:, x])
    out = []
    for y in ys:
        if out and y - out[-1][1] <= RUN_JOIN:
            out[-1][1] = int(y)
        else:
            out.append([int(y), int(y)])
    return [(a, b) for a, b in out if b - a + 1 >= min_len]


# A staff frame must be corroborated by barline strokes at at least this many
# DISTINCT places across the page. Counting raw columns is not enough: a note
# stem is tall and two or three pixels wide, so it would vote for itself.
FRAME_SUPPORT = 3
# Ink columns further apart than this are different strokes.
STROKE_GAP = 10
# Run edges within this many pixels are the same frame (anti-aliasing, and the
# double barline's two strokes).
FRAME_TOL = 4


def staff_frames(ink):
    """Each staff frame on the page, by consensus across every barline.

    A barline stroke runs the height of its staff and nothing else does, so the
    staff frames are the vertical runs that recur — once per barline — at the
    same y. Taking the consensus rather than one chosen column keeps this
    working whatever the melody-derived frame heights turn out to be, and
    whatever spacing the page settles on.
    """
    seen = {}
    for x in range(ink.shape[1]):
        for top, bottom in runs_in_column(ink, x, MIN_STROKE):
            key = (round(top / FRAME_TOL), round(bottom / FRAME_TOL))
            slot = seen.setdefault(key, [[], top, bottom])
            slot[0].append(x)
            slot[1] = min(slot[1], top)
            slot[2] = max(slot[2], bottom)

    def distinct_strokes(xs):
        n, last = 0, None
        for x in sorted(xs):
            if last is None or x - last > STROKE_GAP:
                n += 1
            last = x
        return n

    frames = sorted((top, bottom) for xs, top, bottom in seen.values()
                    if distinct_strokes(xs) >= FRAME_SUPPORT)
    # Keep only the innermost runs. The system's leading edge is a single run
    # spanning its whole system — it CONTAINS the staff frames rather than
    # being one — so anything that contains another run is discarded.
    kept = []
    for top, bottom in frames:
        if any(top <= t and b <= bottom for t, b in frames if (t, b) != (top, bottom)):
            continue
        kept.append((top, bottom))
    return (len(kept), kept)


def system_spans(ink, frames):
    """The vertical extent of every system on the page, from its leading edge.

    MuseScore draws a bracketed system's leading vertical (the bracket and the
    initial barline beside it) as one run through every staff of the system and
    through the gaps between them. Such a run is the one vertical on the page
    that CONTAINS two or more staff frames, so it names the system's staves
    directly, whatever the frames' heights and however the page spaces its
    systems. Overlapping or abutting runs are merged.
    """
    spans = []
    for x in range(ink.shape[1]):
        for top, bottom in runs_in_column(ink, x, 2 * MIN_STROKE):
            inside = [f for f in frames if top - FRAME_TOL <= f[0] and f[1] <= bottom + FRAME_TOL]
            if len(inside) >= 2:
                spans.append((top, bottom))
    merged = []
    for top, bottom in sorted(spans):
        if merged and top <= merged[-1][1] + FRAME_TOL:
            merged[-1][1] = max(merged[-1][1], bottom)
        else:
            merged.append([top, bottom])
    return [(a, b) for a, b in merged]


def group_systems(frames, spans=None):
    """Split staff frames into systems.

    With the system spans from the page's leading edges (system_spans), each
    frame belongs to the span that contains it. Without them, or for frames no
    span contains, fall back to splitting on the outsized between-system gap —
    which only works while frames are taller than the space between them.
    """
    if spans:
        systems, rest = [], list(frames)
        for top, bottom in spans:
            inside = [f for f in rest if top - FRAME_TOL <= f[0] and f[1] <= bottom + FRAME_TOL]
            if inside:
                systems.append(inside)
                rest = [f for f in rest if f not in inside]
        if rest:
            systems.extend(group_systems(rest))
        return sorted(systems)
    if len(frames) <= VOICES_PER_SYSTEM:
        return [frames]
    gaps = [frames[i + 1][0] - frames[i][1] for i in range(len(frames) - 1)]
    inner = sorted(gaps)[: len(gaps) // 2] or gaps
    threshold = 2 * (sum(inner) / len(inner))
    systems, current = [], [frames[0]]
    for i, g in enumerate(gaps):
        if g > threshold:
            systems.append(current)
            current = []
        current.append(frames[i + 1])
    systems.append(current)
    return systems


def page_systems(ink):
    """(frames, systems) for one page: every staff frame, grouped by system."""
    _, frames = staff_frames(ink)
    return frames, group_systems(frames, system_spans(ink, frames))


def check_page(path):
    ink = ink_of(path)
    frames, systems = page_systems(ink)
    result = {"page": path.name, "frames_found": len(frames),
              "staff_frames": [list(f) for f in frames], "systems": [], "failures": []}
    if not frames:
        result["ok"] = False
        result["failures"].append("no barline strokes found at all")
        return result

    for si, system in enumerate(systems):
        entry = {"system": si + 1, "staff_frames": [list(f) for f in system]}
        if len(system) != VOICES_PER_SYSTEM:
            entry["ok"] = False
            result["failures"].append(
                f"system {si + 1} drew {len(system)} staff frames, expected {VOICES_PER_SYSTEM}")
            result["systems"].append(entry)
            continue

        # The inter-staff gap starts EDGE_SLACK rows below one frame and ends
        # EDGE_SLACK rows above the next; the gaps are tens of pixels tall, so
        # the assertion loses nothing and stops reading a stroke's own end as
        # ink between staves.
        gaps = [(system[i][1] + 1 + EDGE_SLACK, system[i + 1][0] - 1 - EDGE_SLACK)
                for i in range(len(system) - 1)]
        entry["inter_staff_gaps"] = [list(g) for g in gaps]
        heights = [b - a + 1 for a, b in system]
        top, bottom = system[0][0], system[-1][1]

        spanning, measure = [], []
        for x in range(ink.shape[1]):
            per_frame = [int(ink[a:b + 1, x].sum()) for a, b in system]
            if not all(c >= BARLINE_FILL * h for c, h in zip(per_frame, heights)):
                continue
            gap_ink = [int(ink[a:b + 1, x].sum()) for a, b in gaps]
            (spanning if any(gap_ink) else measure).append((x, gap_ink))

        # The system's leading edge: every full-height vertical LEFT of the
        # first measure division. MuseScore draws a bracketed system's leading
        # vertical (and, here, the initial barline beside it) through the
        # gaps, and stock 02-Choral/01-SATB renders identically, so those
        # columns are system furniture and are reported, not asserted on.
        # Everything right of the first measure barline is a measure division
        # and must be broken.
        first_measure = measure[0][0] if measure else ink.shape[1]
        edge = [x for x, _ in spanning if x < first_measure]
        stray = [x for x, _ in spanning if x > first_measure]
        entry["system_edge_columns"] = edge
        entry["system_edge_spans_gaps"] = bool(edge)
        entry["spanning_barline_columns"] = stray

        entry["measure_barline_columns"] = [x for x, _ in measure]
        entry["gap_ink_in_barline_columns"] = sorted({g for _, gi in measure for g in gi})

        if not measure:
            result["failures"].append(f"system {si + 1}: no measure barline runs through all four staff frames")
        if stray:
            result["failures"].append(
                f"system {si + 1}: {len(stray)} barline column(s) at x={stray[:5]} carry ink between vocal "
                "staves; vocal barlines must not span")
        entry["ok"] = bool(measure) and not stray
        entry["staff_span_y"] = [top, bottom]
        result["systems"].append(entry)

    result["ok"] = not result["failures"]
    return result


def main():
    if not RENDERS.is_dir():
        print(json.dumps({"ok": False, "error": f"no renders at {RENDERS}; run render_evidence.sh"}, indent=2))
        return 1

    summary = {"ok": True, "renders_dir": "jims-evidence/m9-satb/renders", "failures": []}

    # 1. Every render matches the committed SHA256SUMS.
    sums = RENDERS / "SHA256SUMS"
    checked = 0
    if not sums.is_file():
        summary["failures"].append("renders/SHA256SUMS is missing")
    else:
        for line in sums.read_text().splitlines():
            if not line.strip():
                continue
            digest, name = line.split()
            f = RENDERS / name
            if not f.is_file():
                summary["failures"].append(f"{name} listed in SHA256SUMS but not present")
                continue
            if hashlib.sha256(f.read_bytes()).hexdigest() != digest:
                summary["failures"].append(f"{name} does not match SHA256SUMS")
            checked += 1
    summary["sha256_checked"] = checked

    pages = sorted(RENDERS.glob("*.png"))
    if not pages:
        summary["failures"].append("no rendered pages")

    # 2. Repeated renders of one fixture are byte-identical.
    repeats = []
    for p in pages:
        if "-2-p" not in p.name:
            continue
        first = RENDERS / p.name.replace("-2-p", "-p")
        entry = {"page": p.name, "against": first.name}
        if not first.is_file():
            entry["identical"] = False
            summary["failures"].append(f"{p.name} has no first render to compare against")
        else:
            entry["identical"] = first.read_bytes() == p.read_bytes()
            if not entry["identical"]:
                summary["failures"].append(f"{p.name} is not byte-identical to {first.name}")
        repeats.append(entry)
    summary["repeat_renders"] = repeats
    if not repeats:
        summary["failures"].append("no repeat renders to compare")

    # 3. The barline assertion, on every rendered page.
    checks = []
    for p in pages:
        r = check_page(p)
        checks.append(r)
        summary["failures"].extend(f"{p.name}: {f}" for f in r["failures"])
    summary["pages_checked"] = checks

    systems = [s for c in checks for s in c["systems"]]
    summary["systems_checked"] = len(systems)
    summary["broken_barlines_confirmed"] = bool(systems) and all(s.get("ok") for s in systems)
    if not systems:
        summary["failures"].append("no four-voice system was found to assert on")

    summary["ok"] = not summary["failures"]
    print(json.dumps(summary, indent=2))
    return 0 if summary["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
