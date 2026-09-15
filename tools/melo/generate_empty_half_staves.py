#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate the 14-case empty staff fixture using the built Kernel query adapter.

The adapter accepts one bridge request as an argument and prints its response.
All lattice anchors come from the Kernel's default_instrument_extent operation.
"""
import argparse
import json
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path


def generate(bridge, output):
    def query(op, **kwargs):
        request = dict(abi=2, op=op, **kwargs)
        reply = json.loads(subprocess.check_output([bridge, json.dumps(request)], text=True))
        if not reply["ok"]:
            raise ValueError(reply)
        return reply["result"]

    def add(parent, tag, text=None, **attrs):
        node = ET.SubElement(parent, tag, attrs)
        if text is not None:
            node.text = str(text)
        return node

    reference = json.loads(query("default_reference_timeline"))
    default_configuration = json.loads(query("default_staff_configuration"))
    zero = dict(numerator=0, denominator=1)

    def request_for(configuration):
        return json.loads(query("staff_request", configurations=[dict(at=zero, configuration=configuration)],
                                reference_timeline=reference, at=zero))

    root = ET.Element("museScore", version="4.70")
    score = add(root, "Score")
    add(score, "metaTag", json.dumps(reference,separators=(",",":")), name="meloReferenceTimelineV1")
    add(score, "Division", 480)
    add(score, "showInvisible", 0)
    add(score, "showUnprintable", 0)
    style = add(score, "Style")
    for tag, value in {"pageWidth":11.69, "pageHeight":16.54, "Spatium":1.2,
                       "staffDistance":4, "akkoladeDistance":4,
                       "hideEmptyStaves":0, "showInstrumentNames":1}.items():
        add(style, tag, value)
    add(score, "metaTag", "Empty half-staves — seven modes, two tonic ambits", name="workTitle")
    staff_specs = []
    for mode, rotation in [("Fa",3),("Do",0),("So",4),("Re",1),("La",5),("Mi",2),("Ti",6)]:
        for ambit in ("tonic-centered", "tonic-bounded"):
            configuration = dict(default_configuration, mode_rotation=rotation, tonic_ambit=ambit)
            state = request_for(configuration)
            tonic = query("tonic_pitch_label", state=state)["key_number"]
            # A centered range midpoint is the tonic; a bounded range midpoint
            # is a quarter-period above it. These are declared instrument ranges,
            # not manually derived lattice positions.
            low, high = (tonic-3, tonic+3) if ambit=="tonic-centered" else (tonic,tonic+6)
            state = json.loads(query("default_instrument_extent",state=state,low_key=low,high_key=high))
            number = len(staff_specs)+1
            label = f"{mode}-mode · {ambit}"
            part = add(score,"Part",id=str(number))
            staff = add(part,"Staff",id=str(number))
            st = add(staff,"StaffType",group="pitched")
            for tag,value in {"name":"melo12tet","lines":13,"clef":0,"keysig":0,
                              "ledgerlines":0,"jims":1,"jimsJiLines":1,
                              "jimsStateJson":json.dumps(state["configuration"],separators=(",",":"))}.items():
                add(st,tag,value)
            add(part,"trackName",label)
            instrument=add(part,"Instrument",id="grand-piano")
            add(instrument,"longName",label)
            add(instrument,"shortName",label)
            add(instrument,"instrumentId","keyboard.piano.grand")
            for tag,value in {"minPitchA":low,"maxPitchA":high,"minPitchP":low,"maxPitchP":high}.items():
                add(instrument,tag,value)
            channel=add(instrument,"Channel")
            add(channel,"program",value="0")
            staff_specs.append(number)
    for number in staff_specs:
        staff=add(score,"Staff",id=str(number))
        measure=add(staff,"Measure",number="1")
        voice=add(measure,"voice")
        time=add(voice,"TimeSig")
        add(time,"sigN",4)
        add(time,"sigD",4)
        add(time,"visible",0)
        rest=add(voice,"Rest")
        add(rest,"durationType","measure")
        add(rest,"duration","1/1")
        add(rest,"visible",0)
    ET.indent(root,space="  ")
    output.parent.mkdir(parents=True,exist_ok=True)
    ET.ElementTree(root).write(output,encoding="utf-8",xml_declaration=True)


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bridge",required=True)
    parser.add_argument("--output",type=Path,default=Path(__file__).resolve().parents[2]/"src/engraving/tests/jimstaff_data/empty-half-staves-14.mscx")
    args=parser.parse_args()
    generate(args.bridge,args.output)
