# SPDX-FileCopyrightText: 2026 Jim Plamondon
# SPDX-License-Identifier: GPL-3.0-only
"""Generate the held-note regression through the common Melo-Ready writer."""
import argparse
from pathlib import Path
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--core-root', required=True, type=Path)
args = parser.parse_args()
sys.path.insert(0, str(args.core_root.resolve()))
from tools.tests.test_melo_ready_complete import held_note_package
from tools.musicxml_to_melo.ready_writer import convert_package

data = Path(__file__).resolve().parents[2] / 'src/importexport/musicxml/tests/data/jims/v5'
package = data / 'held-note-reference'
package.mkdir(exist_ok=True)
held_note_package(package)
convert_package(package, data / 'melo-held-note-reference.musicxml')
