# SPDX-FileCopyrightText: 2026 Jim Plamondon
# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('roundtrip', Path(__file__).parents[1] / 'check_meloscore_roundtrip.py')
roundtrip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(roundtrip)


class NotationComparison(unittest.TestCase):
    def compare(self, left, right):
        with tempfile.TemporaryDirectory() as directory:
            a, b = (Path(directory) / name for name in ('a.svg', 'b.svg'))
            for p, body in ((a, left), (b, right)):
                p.write_text('<svg xmlns="http://www.w3.org/2000/svg">' + body + '</svg>')
            return roundtrip.notation(a) == roundtrip.notation(b)

    def test_identical_opaque_lyric_segments_can_change_emission_order(self):
        a = '<polyline class="LyricsLineSegment" fill="none" stroke="#000000" points="1,2 3,2"/>'
        b = a.replace('1,2 3,2', '4,2 6,2')
        self.assertTrue(self.compare(a + b, b + a))
        self.assertFalse(self.compare(a + b, a + b.replace('6,2', '7,2')))

    def test_other_drawing_order_and_stroke_changes_remain_significant(self):
        a = '<polyline class="LyricsLineSegment" fill="none" stroke="#000000" points="1,2 3,2"/>'
        b = a.replace('#000000', '#ffffff')
        self.assertFalse(self.compare(a + b, b + a))
        c = '<path class="Note" d="M0,0 L4,4"/>'
        self.assertFalse(self.compare(a + c, c + a))


if __name__ == '__main__':
    unittest.main()
