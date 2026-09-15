#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jim Plamondon
# SPDX-License-Identifier: GPL-3.0-only
"""Author only the repository fixtures listed in reference_fixture_authoring.json.

The manifest declares spelled roots and relative intervals explicitly. This is
not a general legacy converter and never derives identity from numeric anchors.
All notes, configuration facts and non-reference XML remain source material.
"""
import argparse
import hashlib
import html
import io
import json
from pathlib import Path
import re
import zipfile


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = Path(__file__).with_name("reference_fixture_authoring.json")
STATE = re.compile(r"(<jimsStateJson>)(.*?)(</jimsStateJson>)", re.S)
REFERENCE = re.compile(r'<metaTag name="meloReferenceTimelineV1">.*?</metaTag>', re.S)


def author_xml(text, timeline):
    def configuration(match):
        source = json.loads(html.unescape(match[2]))
        if source.get("schema") == "jimstaff-request-v3":
            raise ValueError("A disposable request is not fixture source")
        source.pop("reference", None)
        source["schema"] = "jimstaff-v3"
        return match[1] + html.escape(json.dumps(source, separators=(",", ":")), quote=False) + match[3]

    text, count = STATE.subn(configuration, text)
    if not count:
        raise ValueError("Expected authored MeloPresto staff configurations")
    tag = '<metaTag name="meloReferenceTimelineV1">' + html.escape(json.dumps(timeline, separators=(",", ":")), quote=False) + "</metaTag>"
    if REFERENCE.search(text):
        text = REFERENCE.sub(lambda _: tag, text)
    else:
        text, count = re.subn(r"(<Score(?:\s[^>]*)?>)", lambda m: m[1] + "\n    " + tag, text, count=1)
        if count != 1:
            raise ValueError("Missing owning Score element")
    return text


def author(data, path, timeline):
    if path.suffix != ".mscz":
        return author_xml(data.decode("utf-8"), timeline).encode("utf-8")
    output = io.BytesIO()
    with zipfile.ZipFile(io.BytesIO(data)) as source, zipfile.ZipFile(output, "w") as destination:
        for entry in source.infolist():
            content = source.read(entry)
            if entry.filename.endswith(".mscx"):
                content = author_xml(content.decode("utf-8"), timeline).encode("utf-8")
            destination.writestr(entry, content)
    return output.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text())
    for entry in manifest["fixtures"]:
        path = ROOT / entry["path"]
        source = path.read_bytes()
        # Only known archived source bytes or already-canonical fixture inputs
        # are accepted. Arbitrary legacy/user scores are never an input route.
        content = source
        if path.suffix == ".mscz":
            with zipfile.ZipFile(io.BytesIO(source)) as archive:
                content = b"".join(archive.read(n) for n in archive.namelist() if n.endswith(".mscx"))
        if b'meloReferenceTimelineV1' not in content and hashlib.sha256(source).hexdigest() != entry["source_sha256"]:
            raise ValueError(f"Fixture source changed: {entry['path']}")
        result = author(source, path, entry["reference_timeline"])
        if args.check:
            if result != source:
                raise ValueError(f"Fixture needs authoring: {entry['path']}")
        elif result != source:
            path.write_bytes(result)
    print(f"Canonical reference fixtures: {len(manifest['fixtures'])} {'verified' if args.check else 'authored'}")


if __name__ == "__main__":
    main()
