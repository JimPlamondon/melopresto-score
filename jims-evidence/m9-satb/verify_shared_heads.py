#!/usr/bin/env python3
"""MeloPresto Staff shared note heads: the committed pixel verifier for the
Milestone 9 follow-up fixed in PR 56 (Chord::noteHeadWidth() on a MeloPresto
staff is the widest head the chord draws, so two heads MuseScore shares sit
on one x instead of 5 to 13 layout units apart).

Renders src/engraving/tests/jimstaff_data/m9-dense-voices.mscx at 600 dpi
twice (reproducibility), plus a variant with the second voice removed from
bars 2 to 4 so each shared pair becomes one single-voice note: the reference
width of one head of that shape. In bars 2 (hollow triangle heads) and 3
(filled triangle heads) the ink component holding the shared pair, which
carries an up stem and a down stem and is therefore the tallest note
component of its bar, must be no wider than the single-voice reference plus
one pixel. Before PR 56 it was 7 px (bar 2) and 3 px (bar 3) wider at
600 dpi, the double outline of two offset heads.

Usage: verify_shared_heads.py <path/to/mscore binary> [--out DIR]
Prints a JSON verdict; exit 1 on any failure.
"""
import argparse, hashlib, json, os, pathlib, subprocess, sys, tempfile
import numpy as np
from PIL import Image
from lxml import etree

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
FIXTURE = ROOT / "src/engraving/tests/jimstaff_data/m9-dense-voices.mscx"


def render(mscore, src, out_png):
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    r = subprocess.run([mscore, "-r", "600", "-o", str(out_png), str(src)], capture_output=True, text=True, env=env)
    page = out_png.with_name(out_png.stem + "-1.png")
    if r.returncode != 0 or not page.exists():
        sys.exit(f"render failed: {src}\n{r.stderr[-400:]}")
    return page


def ink(path):
    a = np.array(Image.open(path).convert("RGB")).astype(int)
    return (a[:, :, 0] < 90) & (a[:, :, 1] < 90) & (a[:, :, 2] < 90)


def components(mask):
    h, w = mask.shape
    lab = np.zeros((h, w), np.int32)
    boxes = []
    n = 0
    for sy, sx in zip(*np.nonzero(mask)):
        if lab[sy, sx]:
            continue
        n += 1
        st = [(sy, sx)]
        lab[sy, sx] = n
        miny = maxy = sy
        minx = maxx = sx
        while st:
            cy, cx = st.pop()
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = cy + dy, cx + dx
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not lab[ny, nx]:
                        lab[ny, nx] = n
                        st.append((ny, nx))
                        miny, maxy, minx, maxx = min(miny, ny), max(maxy, ny), min(minx, nx), max(maxx, nx)
        boxes.append((int(minx), int(miny), int(maxx), int(maxy)))
    return boxes


def bars_and_notes(png):
    m = ink(png)
    rows = np.nonzero(m.sum(1))[0]
    y0, y1 = int(rows.min()) - 20, int(rows.max()) + 20
    boxes = components(m[y0:y1, :])
    boxes = [(x0, y0 + a, x1, y0 + b) for (x0, a, x1, b) in boxes]
    # barlines: thin, spanning the staff; notes: stemmed heads
    bars = sorted(b[0] for b in boxes if (b[3] - b[1]) >= 120 and (b[2] - b[0]) < 12)
    notes = [b for b in boxes if 150 < (b[3] - b[1]) < 480 and 15 < (b[2] - b[0]) < 120]
    per_bar = {}
    for i in range(len(bars) - 1):
        inbar = sorted([b for b in notes if bars[i] < b[0] < bars[i + 1]], key=lambda b: b[0])
        per_bar[i + 2] = [(b[2] - b[0] + 1, b[3] - b[1] + 1) for b in inbar]  # first detected barline closes bar 1
    return bars, per_bar


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mscore")
    ap.add_argument("--out", default=str(HERE / "renders" / "shared-heads"))
    args = ap.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    single = out / "single-voice.mscx"
    t = etree.parse(str(FIXTURE))
    for i, meas in enumerate(t.xpath("//Staff[@id='1']/Measure")):
        if i >= 1:
            for v in meas.findall("voice")[1:]:
                meas.remove(v)
    t.write(str(single), xml_declaration=True, encoding="UTF-8")
    pages = {k: render(args.mscore, s, out / f"{k}.png") for k, s in
             (("dense-voices", FIXTURE), ("dense-voices-2", FIXTURE), ("single-voice", single))}
    sha = {k: hashlib.sha256(p.read_bytes()).hexdigest() for k, p in pages.items()}
    report = {"fixture": str(FIXTURE), "mscore": args.mscore, "dpi": 600,
              "reproducible": sha["dense-voices"] == sha["dense-voices-2"], "sha256": sha, "bars": {}, "failures": []}
    if not report["reproducible"]:
        report["failures"].append("repeated renders differ")
    _, pair = bars_and_notes(pages["dense-voices"])
    _, ref = bars_and_notes(pages["single-voice"])
    for bar, shape in ((2, "hollow triangle"), (3, "filled triangle")):
        if bar not in pair or bar not in ref or not pair[bar] or not ref[bar]:
            report["failures"].append(f"bar {bar}: notes not found")
            continue
        shared = max(pair[bar], key=lambda wh: wh[1])   # the pair carries two stems: tallest
        single_w = ref[bar][0][0]
        report["bars"][bar] = {"head": shape, "shared_pair_width_px": shared[0], "shared_pair_height_px": shared[1],
                               "single_head_width_px": single_w}
        if shared[0] > single_w + 1:
            report["failures"].append(f"bar {bar}: shared pair {shared[0]} px wide, one {shape} head is {single_w} px")
        if shared[1] < 1.5 * ref[bar][0][1]:
            report["failures"].append(f"bar {bar}: the tallest note does not carry two stems; is the pair still shared?")
    report["ok"] = not report["failures"]
    json.dump(report, open(HERE / "verify-shared-heads-summary.json", "w"), indent=2)
    print(json.dumps(report, indent=2))
    sys.exit(0 if report["ok"] else 1)


if __name__ == "__main__":
    main()
