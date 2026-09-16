#!/usr/bin/env python3
"""Exercise real native save/reopen, archive payload, notation and MIDI parity."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
import zipfile


def notation(path):
    """Compare drawing content; opaque equal-style lyric strokes commute."""
    svg = ET.parse(path).getroot()
    for title in svg.findall('{http://www.w3.org/2000/svg}title'):
        svg.remove(title)
    # Layout can enumerate lyric continuation strokes in a different order.
    # Only consecutive, opaque, unfilled polylines with identical styling can
    # commute. Notes, differing colors, transparency and other layers retain
    # their original draw order, and changed stroke coordinates still fail.
    children = list(svg)
    start = 0
    while start < len(children):
        first = children[start]
        style = {k: v for k, v in first.attrib.items() if k != 'points'}
        sortable = (first.tag == '{http://www.w3.org/2000/svg}polyline'
                    and style.get('class') == 'LyricsLineSegment'
                    and style.get('fill') == 'none' and style.get('stroke', 'none') != 'none'
                    and not any(k in style for k in ('opacity', 'stroke-opacity', 'filter', 'mask', 'style')))
        end = start + 1
        if sortable:
            while end < len(children) and children[end].tag == first.tag and {
                k: v for k, v in children[end].attrib.items() if k != 'points'
            } == style:
                end += 1
            children[start:end] = sorted(children[start:end], key=lambda e: e.get('points', ''))
        start = end
    svg[:] = children
    return ET.tostring(svg)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable', type=Path, required=True)
    p.add_argument('--fixture', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--platform', default='offscreen', help='Use cocoa for the packaged macOS app')
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    results = []
    def convert(source, name, expected=0):
        output = a.output / name
        cmd = [str(a.executable.resolve()), '-t', '-o', str(output.resolve()), str(source.resolve())]
        done = subprocess.run(cmd, env={**os.environ, 'QT_QPA_PLATFORM': a.platform}, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        (a.output / (name + '.log')).write_bytes(done.stdout)
        results.append({'source': str(source), 'output': name, 'exit': done.returncode})
        assert done.returncode == expected, results[-1]
        return output
    control = convert(a.fixture, 'control.mscz')
    native = convert(a.fixture, 'Étude.v2.meloscore')
    reopened = convert(native, 'reopened.meloscore')
    compatibility = convert(reopened, 'compatible.mscz')
    control_reopened = convert(control, 'control-reopened.mscz')
    def members(path):
        with zipfile.ZipFile(path) as archive:
            assert archive.testzip() is None
            names = archive.namelist()
            main = next(n for n in names if n.endswith('.mscx') and '/' not in n)
            result = {}
            for name in names:
                if name.endswith('/'):
                    continue
                data = archive.read(name)
                if name == 'META-INF/container.xml':
                    data = data.replace(main.encode(), b'score.mscx')
                result['score.mscx' if name == main else name] = data
            return result
    canonical = members(control)
    for path, expected_path in ((native, control), (reopened, control_reopened), (compatibility, control_reopened)):
        actual = members(path)
        expected_members = members(expected_path)
        assert actual.keys() == expected_members.keys(), (path.name, 'archive member set changed')
        for name in expected_members:
            assert actual[name] == expected_members[name], (path.name, name, 'payload changed')
    convert(control, 'control.svg')
    convert(native, 'native.svg')
    left, right = sorted(a.output.glob('control*.svg')), sorted(a.output.glob('native*.svg'))
    assert left and len(left) == len(right)
    for lhs, rhs in zip(left, right):
        assert notation(lhs) == notation(rhs), (lhs.name, rhs.name, 'notation changed')
    midi = convert(control, 'control.mid')
    midi_native = convert(native, 'native.mid')
    assert midi.read_bytes() == midi_native.read_bytes(), 'MIDI performance changed'
    # A malformed container is refused and produces no replacement document.
    bad = a.output / 'invalid.meloscore'
    bad.write_bytes(b'not a score archive')
    refused = convert(bad, 'invalid-output.meloscore', expected=40)
    assert not refused.exists(), 'invalid input produced an output'
    receipt = {'result': 'pass', 'conversions': results, 'archive_members': sorted(canonical), 'svg_pages': len(left), 'midi_sha256': hashlib.sha256(midi.read_bytes()).hexdigest()}
    (a.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))

if __name__ == '__main__':
    main()
