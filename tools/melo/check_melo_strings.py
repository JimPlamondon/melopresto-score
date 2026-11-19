#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
"""MeloPresto string-table verifier (owner Q22 answer, 2026-08-13).

Every user-visible MeloPresto name must live only in
src/engraving/melo/melostrings.h so a future trademark rename is one-file
work. This check fails when any of the tracked names appears as a quoted
literal elsewhere in production src/. Test evidence is not a shipped interface label. Exit 0 = clean.
"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
TABLE = os.path.join(ROOT, "src", "engraving", "melo", "melostrings.h")
NAMES = ["MeloPresto", "JiMStaff", "JiMS staff", "crescent clef", "scale dots", "tonic indicator"]

# All production diagnostics now use the central table; no literal exceptions.
NON_UI_LITERALS = set()

failures = []
for dirpath, dirnames, filenames in os.walk(os.path.join(ROOT, "src")):
    # Fixture paths, assertions and failure descriptions are not shipped UI.
    dirnames[:] = [d for d in dirnames if d not in (".git", "thirdparty", "tests")]
    for name in filenames:
        if not name.endswith((".cpp", ".h", ".mm", ".qml", ".ui")):
            continue
        path = os.path.join(dirpath, name)
        if os.path.abspath(path) == os.path.abspath(TABLE):
            continue
        try:
            text = open(path, encoding="utf-8", errors="ignore").read()
        except OSError:
            continue
        relative_path = os.path.relpath(path, ROOT)
        for melo_name in NAMES:
            for match in re.finditer(rf'"[^"\n]*{re.escape(melo_name)}[^"\n]*"', text):
                if (relative_path, match.group()[1:-1]) in NON_UI_LITERALS:
                    continue
                line = text.count("\n", 0, match.start()) + 1
                failures.append(f"{os.path.relpath(path, ROOT)}:{line}: literal {melo_name!r} outside the string table")

if failures:
    print("\n".join(failures))
    print(f"{len(failures)} MeloPresto string-table violation(s)")
    sys.exit(1)
print("MeloPresto string table check passed")
