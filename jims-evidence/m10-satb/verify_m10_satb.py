#!/usr/bin/env python3
"""Verify M10 SATB renders, extent centres, and continuous crescent clefs."""

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
EXPECTED_DO_ORIGINS = [900.0, 200.0, 700.0, 400.0]
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
    image = m9.np.asarray(m9.Image.open(path).convert("RGB"))
    ink = image.max(2) < m9.INK_MAX
    red = ((image[:, :, 0] > 140) & (image[:, :, 1] < 110)
           & (image[:, :, 2] < 110) & (image[:, :, 0] > image[:, :, 1] * 1.5))
    _, frames = m9.staff_frames(m9.ink_of(path))
    systems = m9.group_systems(frames)
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
        for voice_index, ((top, bottom), origin) in enumerate(zip(system, EXPECTED_DO_ORIGINS)):
            counts = red[top:bottom + 1].sum(axis=1)
            actual = top + int(counts.argmax())
            expected = top + (1.0 - origin / 1200.0) * (bottom - top)
            tolerance = max(4.0, 0.06 * (bottom - top))
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
                         "upper_horn_ink": upper_horn_ink, "lower_horn_ink": lower_horn_ink,
                         "top_closure_run": top_closure, "bottom_closure_run": bottom_closure})
            if abs(actual - expected) > tolerance:
                result["failures"].append(
                    f"system {system_index + 1} voice {voice_index + 1}: Do row y={actual}, expected {expected:.1f}")
            if upper_horn_ink < HORN_SIDE_MIN_INK or lower_horn_ink < HORN_SIDE_MIN_INK:
                result["failures"].append(
                    f"system {system_index + 1} voice {voice_index + 1}: crescent does not join Do from both sides "
                    f"(upper={upper_horn_ink}, lower={lower_horn_ink})")
            if top_closure < CLOSURE_MIN_RUN or bottom_closure < CLOSURE_MIN_RUN:
                result["failures"].append(
                    f"system {system_index + 1} voice {voice_index + 1}: clipped crescent edge is not closed "
                    f"(top={top_closure}, bottom={bottom_closure})")
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
