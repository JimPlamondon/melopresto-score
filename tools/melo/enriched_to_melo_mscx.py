#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jim Plamondon
# SPDX-License-Identifier: GPL-3.0-only
"""Import canonical V5 enriched MusicXML through MeloPresto Score itself.

Usage: enriched_to_melo_mscx.py --score /path/to/executable input.musicxml output.mscx
The native importer owns note ordering, exact score times and staff contexts.
This command does not rewrite musical facts or accept environment overrides.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET

NAMESPACE = "urn:melopresto:musicxml:5"


def validate_source(source):
    root = ET.parse(source).getroot()
    namespaces = {element.tag[1:].split("}", 1)[0] for element in root.iter()
                  if isinstance(element.tag, str) and element.tag.startswith("{")
                  and (element.tag.startswith("{urn:jims:") or element.tag.startswith("{urn:melopresto:"))}
    if namespaces != {NAMESPACE}:
        raise ValueError("Use canonical V5 MusicXML with a spelled initial reference and relative key-change history.")
    references = list(root.iter(f"{{{NAMESPACE}}}reference-timeline"))
    if len(references) != 1:
        raise ValueError("The composition must have exactly one reference timeline.")


def convert(executable, source, destination):
    source, destination = Path(source), Path(destination)
    if source.resolve() == destination.resolve():
        raise ValueError("The output must be a separate .mscx file.")
    if destination.suffix.lower() != ".mscx":
        raise ValueError("The output filename must end in .mscx.")
    overrides = [key for key in ("JIMS_REFERENCE", "JIMS_KEY_CHANGE", "JIMS_MODE_CHANGE", "JIMS_SCALE_CHANGE", "JIMS_TONIC_TOKEN", "JIMS_TONIC_TOKEN2") if os.environ.get(key)]
    if overrides:
        raise ValueError("Author the canonical input instead of supplying overrides: " + ", ".join(overrides))
    validate_source(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    # A failed import cannot truncate a previously generated score.
    with tempfile.TemporaryDirectory(prefix="melo-native-import-", dir=destination.parent) as directory:
        temporary = Path(directory) / destination.name
        environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software")
        subprocess.run([str(executable), "-o", str(temporary), str(source.resolve())], env=environment, check=True)
        if not temporary.is_file() or temporary.stat().st_size == 0:
            raise ValueError("The native importer produced no score.")
        temporary.replace(destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--score", type=Path, required=True)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    convert(args.score, args.source, args.destination)
