#!/usr/bin/env python3
"""Verify M10 SATB renders, extent centres, and continuous crescent clefs.

The empty-staff contract this checks is the owner's ruling of 2026-09-15
(option 1a, workstream ws_satb_template_verifiers_20260914): an empty staff is
centred on its own vocal range, half a period on the Kernel's range-centre
anchor, and each edge is expanded outward to the nearest visible ratio line,
an edge within the Kernel's 25-cent tempering snap of a line counting as on
it. For the template's four voices in C Do-mode at a 700-cent generator the
Kernel (melo-staff, automatic_frame) derives: Soprano Fa4 to Do5 with Do on
the upper edge; Alto Do4 to So4 with Do on the lower edge; Tenor So3 to Re4
with Do4 inside; Bass Ti2 to Fa3 with Do3 inside. The 2026-09-14 revision of
this verifier briefly pinned the Kernel's then-current tonic-start frames
(every voice Do4 to So4); the owner rejected those renders on 2026-09-15 as
not centred on any voice's range.
"""

import hashlib
import importlib.util
import json
import pathlib
import sys
import xml.etree.ElementTree as ET

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
RENDERS = HERE / "renders"
SUMMARY = HERE / "verify-summary.json"
TEMPLATE = ROOT / "share/templates/02-Choral/12-SATB_(MeloPresto_Staff)/12-SATB_(MeloPresto_Staff).mscx"
M9_VERIFIER = HERE.parent / "m9-satb/verify_satb_layout.py"

EXPECTED_EXTENTS = [
    {"lower": {"nPer": 3, "nGen": -5}, "upper": {"nPer": 4, "nGen": -5}},
    {"lower": {"nPer": 2, "nGen": -4}, "upper": {"nPer": 3, "nGen": -4}},
    {"lower": {"nPer": 1, "nGen": -3}, "upper": {"nPer": 2, "nGen": -3}},
    {"lower": {"nPer": 2, "nGen": -6}, "upper": {"nPer": 3, "nGen": -6}},
]
# Per voice, from the Kernel at a 700-cent generator (see the module
# docstring): the frame's height in cents and the red Do row's height above
# the frame's lower edge. Do on an edge is 0 (lower) or the height (upper).
EXPECTED_EMPTY_FRAMES = [
    {"voice": "soprano", "height_cents": 701.955, "do_above_lower_cents": 701.955},
    {"voice": "alto", "height_cents": 701.955, "do_above_lower_cents": 0.0},
    {"voice": "tenor", "height_cents": 701.955, "do_above_lower_cents": 498.045},
    {"voice": "bass", "height_cents": 609.777, "do_above_lower_cents": 111.731},
]
# Pixels per line distance in the renders: render_evidence.sh renders at 120
# dpi and the template leaves MuseScore's default spatium (1.74978 mm); one
# line distance is 100 cents (StaffType::MELO_CENTS_PER_LINE_DISTANCE).
RENDER_DPI = 120
SPATIUM_MM = 1.74978
CENTS_PER_LINE_DISTANCE = 100.0
PX_PER_CENT = SPATIUM_MM / 25.4 * RENDER_DPI / CENTS_PER_LINE_DISTANCE
FRAME_HEIGHT_TOL_PX = 3.0
# The red Do row may sit this far from its expected row (the stroke's
# anti-aliased end and the line's own width).
DO_ROW_TOL_PX = 2
CLOSURE_MIN_RUN = 8
HORN_SIDE_MIN_INK = 12


def longest_horizontal_run(row):
    longest = current = 0
    for inked in row:
        if inked:
            current += 1
            longest = max(longest, current)
        else:
            current = 0
    return longest


def check_empty_staff_do_rows(path, m9):
    image = m9.np.asarray(m9.Image.open(path).convert("RGB")).astype(int)
    ink = image.max(2) < m9.INK_MAX
    # The Do line is a red hue at any weight: the thin line renders around
    # RGB 235,118,118 and a heavier one around 195,41,41.
    red = ((image[:, :, 0] - image[:, :, 1] > 60) & (image[:, :, 0] - image[:, :, 2] > 60)
           & (abs(image[:, :, 1] - image[:, :, 2]) < 30))
    _, systems = m9.page_systems(ink)
    layout_systems = m9.check_page(path)["systems"]
    result = {"page": path.name, "systems": [], "failures": []}
    for system_index, system in enumerate(systems):
        if len(system) != 4:
            result["failures"].append(f"system {system_index + 1}: expected four staff frames")
            continue
        edge_columns = layout_systems[system_index].get("system_edge_columns", [])
        if not edge_columns:
            result["failures"].append(f"system {system_index + 1}: no system edge for crescent inspection")
            continue
        clef_right = min(edge_columns)
        rows = []
        for voice_index, ((top, bottom), contract) in enumerate(zip(system, EXPECTED_EMPTY_FRAMES)):
            # The red Do row: searched to the frame edges plus the row
            # tolerance, because it may BE an edge (alto lower, soprano upper).
            search_top = max(0, top - DO_ROW_TOL_PX)
            search_bottom = min(ink.shape[0] - 1, bottom + DO_ROW_TOL_PX)
            counts = red[search_top:search_bottom + 1].sum(axis=1)
            actual = search_top + int(counts.argmax())
            expected = bottom - contract["do_above_lower_cents"] * PX_PER_CENT
            tolerance = DO_ROW_TOL_PX
            height = bottom - top + 1
            expected_height = contract["height_cents"] * PX_PER_CENT
            do_on_lower_edge = contract["do_above_lower_cents"] == 0.0
            do_on_upper_edge = contract["do_above_lower_cents"] == contract["height_cents"]
            closure_left = max(0, clef_right - 75)
            closure_right = max(closure_left + 1, clef_right - 3)
            top_closure = max(longest_horizontal_run(ink[y, closure_left:closure_right])
                              for y in range(max(0, top - 2), min(ink.shape[0], top + 3)))
            bottom_closure = max(longest_horizontal_run(ink[y, closure_left:closure_right])
                                 for y in range(max(0, bottom - 2), min(ink.shape[0], bottom + 3)))
            horn_left = max(0, clef_right - 36)
            horn_right = max(horn_left + 1, clef_right - 3)
            upper_horn_ink = int(ink[max(top, actual - 10):max(top, actual - 2),
                                     horn_left:horn_right].sum())
            lower_horn_ink = int(ink[min(bottom + 1, actual + 3):min(bottom + 1, actual + 11),
                                     horn_left:horn_right].sum())
            rows.append({"voice": ["soprano", "alto", "tenor", "bass"][voice_index],
                         "actual_y": actual, "expected_y": expected, "tolerance": tolerance,
                         "red_pixels_on_row": int(counts.max()),
                         "frame_height_px": height, "expected_frame_height_px": round(expected_height, 2),
                         "upper_horn_ink": upper_horn_ink, "lower_horn_ink": lower_horn_ink,
                         "top_closure_run": top_closure, "bottom_closure_run": bottom_closure})
            if int(counts.max()) == 0:
                result["failures"].append(
                    f"system {system_index + 1} voice {voice_index + 1}: no red Do row in the frame")
            elif abs(actual - expected) > tolerance:
                result["failures"].append(
                    f"system {system_index + 1} voice {voice_index + 1}: Do row y={actual}, "
                    f"expected y={expected:.1f} ({contract['do_above_lower_cents']:.0f} cents above the lower edge)")
            if abs(height - expected_height) > FRAME_HEIGHT_TOL_PX:
                result["failures"].append(
                    f"system {system_index + 1} voice {voice_index + 1}: frame is {height} px tall, expected "
                    f"{expected_height:.1f} ({contract['height_cents']:.0f} cents)")
            # The crescent joins Do from every side that lies inside the frame:
            # both sides for an interior Do, one side for a Do on an edge, with
            # no crescent ink past that edge.
            if not do_on_upper_edge and upper_horn_ink < HORN_SIDE_MIN_INK:
                result["failures"].append(
                    f"system {system_index + 1} voice {voice_index + 1}: crescent horn does not reach Do from "
                    f"above (upper={upper_horn_ink})")
            if not do_on_lower_edge and lower_horn_ink < HORN_SIDE_MIN_INK:
                result["failures"].append(
                    f"system {system_index + 1} voice {voice_index + 1}: crescent horn does not reach Do from "
                    f"below (lower={lower_horn_ink})")
            if do_on_lower_edge and lower_horn_ink >= HORN_SIDE_MIN_INK:
                result["failures"].append(
                    f"system {system_index + 1} voice {voice_index + 1}: crescent ink continues below the Do "
                    f"edge (lower={lower_horn_ink})")
            if do_on_upper_edge and upper_horn_ink >= HORN_SIDE_MIN_INK:
                result["failures"].append(
                    f"system {system_index + 1} voice {voice_index + 1}: crescent ink continues above the Do "
                    f"edge (upper={upper_horn_ink})")
            # A frame edge that is not a Do row is a clipped crescent edge and
            # must be closed.
            if not do_on_upper_edge and top_closure < CLOSURE_MIN_RUN:
                result["failures"].append(
                    f"system {system_index + 1} voice {voice_index + 1}: clipped crescent top edge is not "
                    f"closed (top={top_closure})")
            if not do_on_lower_edge and bottom_closure < CLOSURE_MIN_RUN:
                result["failures"].append(
                    f"system {system_index + 1} voice {voice_index + 1}: clipped crescent bottom edge is not "
                    f"closed (bottom={bottom_closure})")
        result["systems"].append({"system": system_index + 1, "do_rows": rows})
    result["ok"] = not result["failures"]
    return result


def load_m9_verifier():
    spec = importlib.util.spec_from_file_location("m9_satb_verifier", M9_VERIFIER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    summary = {"ok": True, "failures": [], "renders_dir": "jims-evidence/m10-satb/renders"}
    pages = sorted(RENDERS.glob("*.png"))

    sums = RENDERS / "SHA256SUMS"
    checked = 0
    if not sums.is_file():
        summary["failures"].append("renders/SHA256SUMS is missing")
    else:
        for line in sums.read_text().splitlines():
            digest, name = line.split()
            path = RENDERS / name
            if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                summary["failures"].append(f"SHA-256 mismatch: {name}")
            checked += 1
    summary["sha256_checked"] = checked

    repeats = []
    for path in pages:
        if "-2-p" not in path.name:
            continue
        first = RENDERS / path.name.replace("-2-p", "-p")
        identical = first.is_file() and first.read_bytes() == path.read_bytes()
        repeats.append({"page": path.name, "against": first.name, "identical": identical})
        if not identical:
            summary["failures"].append(f"repeat differs: {path.name}")
    summary["repeat_renders"] = repeats
    if not repeats:
        summary["failures"].append("no repeat renders")

    m9 = load_m9_verifier()
    page_checks = [m9.check_page(path) for path in pages]
    summary["pages_checked"] = page_checks
    for check in page_checks:
        summary["failures"].extend(f"{check['page']}: {failure}" for failure in check["failures"])

    do_row_checks = [check_empty_staff_do_rows(path, m9) for path in pages if path.name.startswith("empty-template")]
    summary["empty_staff_do_rows"] = do_row_checks
    for check in do_row_checks:
        summary["failures"].extend(f"{check['page']}: {failure}" for failure in check["failures"])

    states = [json.loads(node.text) for node in ET.parse(TEMPLATE).findall(".//jimsStateJson")]
    actual = [state.get("extent") for state in states]
    summary["empty_staff_centres"] = {
        "source": str(TEMPLATE.relative_to(ROOT)),
        "voices": ["soprano", "alto", "tenor", "bass"],
        "actual_extents": actual,
        "expected_kernel_defaults": EXPECTED_EXTENTS,
        "centred_correctly": actual == EXPECTED_EXTENTS,
    }
    if actual != EXPECTED_EXTENTS:
        summary["failures"].append("empty SATB extents do not match the Kernel-pinned vocal defaults")

    summary["ok"] = not summary["failures"]
    rendered = json.dumps(summary, indent=2)
    SUMMARY.write_text(rendered + "\n")
    print(rendered)
    return 0 if summary["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
