# SPDX-FileCopyrightText: 2026 Jim Plamondon
# SPDX-License-Identifier: GPL-3.0-only
"""Author a finite cross-staff voice with an independent lower voice."""
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys
from lxml import etree

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--core-root', required=True, type=Path)
args = parser.parse_args()
sys.path.insert(0, str(args.core_root.resolve()))
from tools.tests.test_melo_ready_complete import complete_package, write
from tools.musicxml_to_melo.ready_time import index_musicxml
from tools.musicxml_to_melo.ready_writer import convert_package
from tools.musicxml_to_melo.notation_oracle import build_oracle

data = Path(__file__).resolve().parents[2] / 'src/importexport/musicxml/tests/data/jims/v5'
package = data / 'cross-staff-voice'
package.mkdir(exist_ok=True)
document = complete_package(package)
root = etree.fromstring((package / 'score.musicxml').read_bytes())
measure = root.find('part/measure')
attributes = measure.find('attributes')
staves = etree.Element('staves')
staves.text = '2'
attributes.insert(list(attributes).index(attributes.find('clef')), staves)
notes = measure.findall('note')
for number, note in enumerate(notes, 1):
    voice = etree.Element('voice')
    voice.text = '1'
    note.insert(list(note).index(note.find('type')), voice)
    staff = etree.Element('staff')
    staff.text = str(number)
    note.insert(list(note).index(note.find('lyric')), staff)
backup = etree.SubElement(measure, 'backup')
etree.SubElement(backup, 'duration').text = '4'
lower = deepcopy(notes[0])
lower.set('id', 'lower')
lower.find('pitch/octave').text = '3'
lower.find('duration').text = '4'
lower.find('type').text = 'half'
lower.find('voice').text = '5'
lower.find('staff').text = '2'
lower.remove(lower.find('lyric'))
measure.append(lower)
source = etree.tostring(root)
(package / 'score.musicxml').write_bytes(source)
document['work']['id'] = 'cross-staff-voice'
document['resources'][0]['sha256'] = hashlib.sha256(source).hexdigest()
document['anchors']['score_map'] = index_musicxml(source).to_json()
document['configurations'].append(deepcopy(document['configurations'][0]) | dict(id='lower', staff=2))
document['roles']['staves'].append(dict(part='P1', staff=2, target_instrument='jammer'))
document['harmonies'][0]['observed_note_ids'].append('lower')
write(package, document)
convert_package(package, data / 'melo-cross-staff-voice.musicxml')
(package / 'notation-oracle.json').write_text(json.dumps(build_oracle(package), indent=2) + '\n')
