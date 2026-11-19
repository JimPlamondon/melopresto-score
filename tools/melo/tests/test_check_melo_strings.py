# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
"""Exercise the production label guard without modifying the live source tree."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


class StringTableGuardTests(unittest.TestCase):
    def check_tree(self, files):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            checker = root / "tools/melo/check_melo_strings.py"
            checker.parent.mkdir(parents=True)
            shutil.copyfile(Path(__file__).resolve().parents[1] / checker.name, checker)
            for name, text in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")
            return subprocess.run([sys.executable, str(checker)], capture_output=True, text=True)

    def test_old_and_new_labels_outside_table_are_rejected(self):
        for label in ("JiMStaff", "JiMS staff", "MeloPresto Staff", "tonic indicator"):
            with self.subTest(label=label):
                result = self.check_tree({"src/panel.qml": f'text: "{label}"'})
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("src/panel.qml:1", result.stdout)

    def test_table_and_test_fixture_names_are_allowed(self):
        result = self.check_tree({
            "src/engraving/melo/melostrings.h": 'return "MeloPresto Staff";',
            "src/engraving/tests/test.cpp": 'load("SATB_(MeloStaff).mscx");',
        })
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_diagnostics_also_require_the_central_table(self):
        existing = 'LOGE() << "MeloPresto Staff: Kernel frame derivation failed for staff ";'
        self.assertNotEqual(self.check_tree({"src/engraving/dom/stafftype.cpp": existing}).returncode, 0)
        self.assertNotEqual(self.check_tree({"src/other.cpp": existing}).returncode, 0)
        self.assertNotEqual(self.check_tree({"src/engraving/dom/stafftype.cpp": existing.replace("failed", "stopped")}).returncode, 0)


if __name__ == "__main__":
    unittest.main()
