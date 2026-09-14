#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Author the manifest's V5 test pieces from declared spelling and intervals.

The source files supply notes, configurations and layout. Their old numeric
references and reconstructed change summaries have no authority. Only the
manifest supplies the newly authored initial pitch and relative events.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = Path(__file__).with_name("interchange_fixture_authoring.json")


def author(entry):
    source = ROOT / entry["source"]
    raw = source.read_bytes()
    if hashlib.sha256(raw).hexdigest() != entry["source_sha256"]:
        raise ValueError(f"Fixture source changed: {source}")
    text = raw.decode()
    prefix = re.search(r'xmlns:(\w+)="urn:(?:jims|melopresto):musicxml:[1-4]"', text).group(1)
    text = re.sub(r'urn:(?:jims|melopresto):musicxml:[1-4]', 'urn:melopresto:musicxml:5', text)
    text = re.sub(r'<(\w+):reference>.*?</\1:reference>', '', text, flags=re.S)
    text = re.sub(r'<(\w+):change>.*?</\1:change>', '', text, flags=re.S)
    root = ET.Element(f"{prefix}:reference-timeline")
    if entry["initial"] is not None:
        ET.SubElement(root, f"{prefix}:initial-pitch", {k: str(v) for k,v in entry["initial"].items()})
    for event in entry["events"]:
        ET.SubElement(root, f"{prefix}:relative-key-change", {k:str(v) for k,v in event.items()})
    if entry['initial'] is not None:
        fragment = ET.tostring(root, encoding='unicode')
        if source.suffix == '.mei':
            text = text.replace('urn:jims:mei:1', 'urn:melopresto:mei:1')
            text = text.replace('<jm:musicxml>', '<jm:musicxml>\n          ' + fragment, 1)
        else:
            text = text.replace('<part-list>', fragment + '\n  <part-list>', 1)
    return text.encode()


def main(check=False):
    entries = json.loads(MANIFEST.read_text())["fixtures"]
    for entry in entries:
        expected = author(entry)
        target = ROOT / entry["output"]
        if check:
            if not target.exists() or target.read_bytes() != expected:
                raise ValueError(f"Regenerate canonical fixture: {target}")
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(expected)
    print(f"Verified {len(entries)} authored V5 fixtures" if check else f"Authored {len(entries)} V5 fixtures")


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true')
    main(parser.parse_args().check)
