#!/usr/bin/env python3
"""Derive local app/document artwork from checked canonical glyph sources."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
D = ROOT / 'share/icons/melopresto'

def svg_artworks():
    source = json.loads((D / 'sources.json').read_text())
    for s in source['sources']:
        assert hashlib.sha256((D / s['file']).read_bytes()).hexdigest() == s['sha256']
    palette = source['palette']
    rose, brass, bone = [palette[x] for x in ('rosewood', 'brass', 'bone')]
    ns = '{http://www.w3.org/2000/svg}'
    crescent = ET.fromstring(re.sub(r'<!--.*?-->', '', (D / 'canonical-crescent.svg').read_text(), flags=re.S)).find(ns + 'path').attrib['d']
    tile = f'<rect x="64" y="64" width="896" height="896" rx="176" fill="{rose}"/>'
    def glyph(path, transform, fill, stroke, width):
        return f'<path d="{path}" transform="{transform}" fill="{fill}" stroke="{stroke}" stroke-width="{width}" stroke-linejoin="round"/>'
    score = tile + glyph(crescent, 'translate(112 112) scale(0.8)', bone, brass, 15)
    page = f'<path d="M 224 96 H 652 L 816 260 V 928 H 224 Z" fill="{bone}" stroke="{rose}" stroke-width="20"/>'
    page += f'<path d="M 652 96 V 260 H 816" fill="{brass}" stroke="{rose}" stroke-width="20" stroke-linejoin="round"/>'
    document = page + glyph(crescent, 'translate(212 290) scale(0.6)', rose, brass, 15)
    art = {}
    for name, title, body in [('MeloPrestoScore', 'MeloPresto Score — full Do-to-Do crescent', score), ('MeloPrestoScoreDocument', 'MeloPresto Score document', document)]:
        art[name] = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024"><title>{title}</title>{body}</svg>\n'
    return art

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--check', action='store_true')
    p.add_argument('--render', action='store_true')
    p.add_argument('--node', default='node')
    p.add_argument('--sharp-module', default='sharp')
    a = p.parse_args()
    art = svg_artworks()
    for name, svg in art.items():
        path = D / (name + '.svg')
        if a.check:
            assert path.read_text() == svg, f'stale generated SVG: {path}'
        else:
            path.write_text(svg)
    if a.render:
        with tempfile.TemporaryDirectory(prefix='melopresto-icons-') as temporary:
            script = Path(temporary) / 'render.cjs'
            script.write_text('const sharp = require(process.argv[2]); sharp(process.argv[3], {density: 384}).resize(Number(process.argv[5]), Number(process.argv[5])).png().toFile(process.argv[4]);')
            for name in art:
                iconset = Path(temporary) / (name + '.iconset')
                iconset.mkdir()
                for size in (16, 32, 128, 256, 512):
                    for factor in (1, 2):
                        png = iconset / (f'icon_{size}x{size}' + ('@2x' if factor == 2 else '') + '.png')
                        subprocess.run([a.node, str(script), a.sharp_module, str(D / (name + '.svg')), str(png), str(size * factor)], check=True)
                subprocess.run(['iconutil', '-c', 'icns', str(iconset), '-o', str(D / (name + '.icns'))], check=True)
                (D / (name + '.png')).write_bytes((iconset / 'icon_512x512@2x.png').read_bytes())
    print('Canonical glyph paths, palette, and generated app/document masters verified' if a.check else 'Generated MeloPresto app/document artwork')

if __name__ == '__main__':
    main()
