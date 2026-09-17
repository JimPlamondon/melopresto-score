# SPDX-FileCopyrightText: 2026 Jim Plamondon
# SPDX-License-Identifier: GPL-3.0-only
"""Inspect one explicit native Score against source-derived notation expectations."""
import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test-executable', required=True, type=Path)
    parser.add_argument('--score', required=True, type=Path)
    parser.add_argument('--oracle', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--corruption-controls', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    oracle = json.loads(args.oracle.read_text())
    cases = [('selected-source', oracle, True)]
    if args.corruption_controls:
        def changed(name, update):
            value = deepcopy(oracle)
            update(value)
            cases.append((name, value, False))
        changed('missing-painted-clefs', lambda v: v.update(header_paint_damage='clefs'))
        changed('missing-clef-clipping', lambda v: v.update(header_paint_damage='clips'))
        changed('missing-tuning-label', lambda v: v.update(header_paint_damage='tuning'))
        changed('wrong-note-height', lambda v: v['notes'][0].update(cents_above_extent_lower=v['notes'][0]['cents_above_extent_lower'] + 100))
        changed('wrong-playback-frequency', lambda v: v['notes'][0].update(frequency_hz=v['notes'][0]['frequency_hz'] * 1.01))
        changed('wrong-notehead', lambda v: v['notes'][0].update(notehead='triangle-vertex-up' if v['notes'][0]['notehead'] != 'triangle-vertex-up' else 'conventional'))
        changed('wrong-staff', lambda v: v['notes'][0].update(staff=len(v['staves'])))
        changed('missing-note', lambda v: v['notes'].pop())
        changed('wrong-mode', lambda v: v['staves'][0]['states'][0]['configuration'].update(mode_rotation=999))
        changed('wrong-lyric', lambda v: v['source_lyric_texts'].append('CORRUPTION CONTROL'))
        if vgroups := [n['voice_group'] for n in oracle['notes']]:
            if vgroups.count(vgroups[0]) > 1:
                changed('split-source-voice', lambda v: v['notes'][0].update(voice_group='corrupted-source-voice'))
        transitions = [(i, j) for i, staff in enumerate(oracle['staves']) for j, state in enumerate(staff['states'])
                       if state.get('indicator') and state['indicator']['kinds']]
        if transitions:
            si, sj = transitions[0]
            changed('missing-modulation-indicator', lambda v: v['staves'][si]['states'][sj]['indicator'].update(kinds=[]))
            changed('wrong-modulation-kind', lambda v: v['staves'][si]['states'][sj]['indicator'].update(kinds=['CORRUPTION CONTROL']))
            tonic_changes = [(i, j) for i, j in transitions if oracle['staves'][i]['states'][j]['indicator']['terrain']['tonic_indicators']]
            if tonic_changes:
                ti, tj = tonic_changes[0]
                changed('wrong-indicator-position', lambda v: v['staves'][ti]['states'][tj]['indicator']['terrain']['tonic_indicators'][0].update(ordinate=0.123456789))
            arrow_changes = [(i, j) for i, j in transitions if oracle['staves'][i]['states'][j]['indicator']['terrain']['arrows']]
            if arrow_changes:
                ai, aj = arrow_changes[0]
                changed('missing-painted-arrow-shafts', lambda v: v['staves'][ai]['states'][aj]['indicator'].update(paint_damage='shafts'))
                changed('missing-painted-arrowheads', lambda v: v['staves'][ai]['states'][aj]['indicator'].update(paint_damage='heads'))
        if oracle['harmonies']:
            changed('wrong-chord-name', lambda v: v['harmonies'][0].update(name='CORRUPTION CONTROL'))
    receipt = dict(schema='melopresto.source-notation-check.v1', score=str(args.score.resolve()),
        score_sha256=digest(args.score), oracle_sha256=digest(args.oracle),
        executable_sha256=digest(args.test_executable), notes=len(oracle['notes']),
        harmonies=len(oracle['harmonies']), cases=[])
    for name, value, expect_pass in cases:
        selected = args.output / (name + '.json')
        selected.write_text(json.dumps(value, indent=2) + '\n')
        xml = args.output / (name + '.xml')
        env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
            MELO_NOTATION_SCORE=str(args.score.resolve()), MELO_NOTATION_ORACLE=str(selected.resolve()))
        with (args.output / (name + '.log')).open('w') as log:
            result = subprocess.run([str(args.test_executable.resolve()),
                '--gtest_filter=MusicXml_Melo_Tests.CompleteSourceNotationOptionalPrivateCorpus',
                '--gtest_output=xml:' + str(xml.resolve())], env=env, stdout=log, stderr=subprocess.STDOUT)
        # A crash, skipped test or empty selection cannot pass a corruption control.
        tests = ET.parse(xml).getroot() if xml.exists() else None
        ran = tests is not None and tests.get('tests') == '1' and tests.get('disabled', '0') == '0' and not tests.findall('.//skipped')
        observed_pass = ran and result.returncode == 0 and tests.get('failures') == '0'
        observed_failure = ran and result.returncode == 1 and tests.get('failures') == '1'
        passed = observed_pass if expect_pass else observed_failure
        receipt['cases'].append(dict(name=name, expected='pass' if expect_pass else 'rejection', passed=bool(passed), exit_code=result.returncode))
        print(name, 'PASS' if passed else 'FAIL', flush=True)
    receipt['passed'] = all(case['passed'] for case in receipt['cases'])
    (args.output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    raise SystemExit(0 if receipt['passed'] else 1)


if __name__ == '__main__':
    main()
