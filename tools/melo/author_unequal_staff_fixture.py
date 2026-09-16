# SPDX-FileCopyrightText: 2026 Jim Plamondon
# SPDX-License-Identifier: GPL-3.0-only
"""Author the finite one-staff plus two-staff shared-timeline regression."""
from copy import deepcopy
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'src/importexport/musicxml/tests/data/jims/v5'
NS = 'urn:melopresto:musicxml:5'
ET.register_namespace('melo', NS)
tree = ET.parse(DATA / 'jims-multi-part-perstaff-differs.musicxml')
root = tree.getroot()
root.find('work/work-title').text = 'Unequal staff counts share one musical timeline'
part = root.findall('part')[1]
for ordinal, measure in enumerate(part.findall('measure')):
    attributes = measure.find('attributes')
    state = attributes.find('{' + NS + '}staff-state')
    if ordinal == 0:
        staves = ET.Element('staves')
        staves.text = '2'
        attributes.insert(list(attributes).index(state), staves)
    state.set('number', '1')
    second = deepcopy(state)
    second.set('number', '2')
    attributes.append(second)
    note = measure.find('note')
    voice = ET.Element('voice')
    voice.text = '1'
    note.insert(list(note).index(note.find('type')), voice)
    ET.SubElement(note, 'staff').text = '1'
    ET.SubElement(ET.SubElement(measure, 'backup'), 'duration').text = '16'
    lower = deepcopy(note)
    lower.find('voice').text = '2'
    lower.find('staff').text = '2'
    measure.append(lower)
ET.indent(tree)
tree.write(DATA / 'melo-unequal-staff-counts.musicxml', encoding='utf-8', xml_declaration=True)
