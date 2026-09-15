#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jim Plamondon
# SPDX-License-Identifier: GPL-3.0-only
"""Refusal checks for the native V5 import command; no host is launched."""
from pathlib import Path
import tempfile
import unittest
from enriched_to_melo_mscx import convert, validate_source


class NativeImportBoundary(unittest.TestCase):
    def test_old_numeric_profiles_are_refused_without_touching_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "input.musicxml"
            output = Path(directory) / "output.mscx"
            output.write_bytes(b"preserve previously generated score")
            for namespace in ["urn:jims:musicxml:1", "urn:jims:musicxml:2", "urn:jims:musicxml:3", "urn:melopresto:musicxml:4"]:
                source.write_text(f'<score-partwise xmlns:m="{namespace}"><m:staff-state><m:reference-pitch key-number="62"/></m:staff-state></score-partwise>')
                original = source.read_bytes()
                with self.assertRaisesRegex(ValueError, "canonical V5"):
                    convert("/must-not-launch", source, output)
                self.assertEqual(source.read_bytes(), original)
                self.assertEqual(output.read_bytes(), b"preserve previously generated score")

    def test_missing_or_duplicate_reference_timeline_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "input.musicxml"
            for count in [0, 2]:
                source.write_text('<score-partwise xmlns:m="urn:melopresto:musicxml:5"><m:staff-configuration/>' + '<m:reference-timeline/>' * count + '</score-partwise>')
                with self.assertRaisesRegex(ValueError, "exactly one"):
                    validate_source(source)


if __name__ == "__main__":
    unittest.main()
