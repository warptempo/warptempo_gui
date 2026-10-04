#!/usr/bin/env python3
# tools/palette/picker/check_refs.py — the laptop check's references, from the mock tool's own code (build_picker.sh
# --check runs it, then host_check against what it writes):
#
#     python3 tools/palette/picker/check_refs.py <theme.json> <dir>
#
#   <dir>/scene/            the theme exported (render.py --export): the picker's export as the tablet receives it
#   <dir>/multi/            THE CHECK THEME exported (<dir>/picker_check.json: the theme plus a Selection Test element,
#                           the selected fill, over a fourth scene with the flags on and the first flag's editor open,
#                           its whole text selected), which the sessions drive: the switches of scene, stacks over a
#                           picked base (the flag labels over the flag faces, the selected text over the band)
#   <dir>/expect/*.ppm      every scene of both rendered by render.py at the manifest's colours and at every colour set
#                           of render.picker_check_sets (each element moved, the chrome to a tint whose 32 and 96 hit
#                           the rule's half-to-even ties and to a bright ground whose lines cap, all at once)
#   <dir>/{scene,multi}/expects.txt  one line per reference: "<scene> <ppm> [<key>=#RRGGBB ...]" (the elements moved
#                           from the manifest's colours); the picker's picture at those colours must equal the ppm
#   <dir>/linmix.bin        colour.lin_mix(a, b, 0.5)'s red byte for every pair (a, b), 65536 bytes, a * 256 + b
#   <dir>/models_ref.txt    THE MODELS' REFERENCE, computed here independently of the C++ (colour.h): for every byte
#                           triple of a set -- the cube's corners, all 256 greys, the P3 primaries and secondaries and
#                           20000 seeded random triples -- one line "r g b  H S L  L C h": HSL by Python's own colorsys
#                           (rgb_to_hls, hue scaled to degrees) and CIE LCh(ab) over the bytes as Display-P3 (D65, no
#                           adaptation) by numpy, the matrix solved from the primaries' chromaticities, Lab by CIE's
#                           epsilon / kappa form (216/24389, 24389/27), every number repr'd (exact doubles)
#   <dir>/derived/          a SYNTHETIC export exercising the derive rule, which today's scenes do not paint (scene
#                           1002 has no outline pixels at its zoom): the export's waveform scene with its ink pixels
#                           from x 1152 to 1727 given to a role derived from the ink over the CANVAS ELEMENT (the
#                           outline's rule, following the live canvas) and from x 1728 on to one derived over a literal,
#                           with its expects.txt and references composed by colour.py (render.picker_picture)
import json, os, shutil, subprocess, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); PALETTE = os.path.dirname(HERE)
sys.path.insert(0, PALETTE)

theme, out = sys.argv[1], sys.argv[2]
sys.argv = [sys.argv[0]]
import render as R, colour

def export_with_refs(theme_path, name):
    """<out>/<name>/ the export, <out>/expect/<name>_<scene>_<n>.ppm its references, -> the expects.txt lines"""
    d = os.path.join(out, name)
    if os.path.isdir(d): shutil.rmtree(d)
    subprocess.run([sys.executable, os.path.join(PALETTE, 'render.py'), theme_path, '--export', d], check=True)
    base = json.load(open(theme_path)); pk = base['picker']; lines = []
    for sc, ov in pk['scenes'].items():
        for n, cs in enumerate([{}] + R.picker_check_sets(pk['elements'])):
            rgb = R.render_rgb(theme_path, data=R.picker_apply(R.picker_scene_theme(base, ov), pk['elements'], cs))[1]
            ppm = os.path.join(expect, f'{name}_{sc}_{n}.ppm'); R.write_pnm(ppm, rgb)
            lines.append(' '.join([sc, ppm] + [f'{k}={v}' for k, v in cs.items()]))
    open(os.path.join(d, 'expects.txt'), 'w').write('\n'.join(lines) + '\n')
    return lines

expect = os.path.join(out, 'expect')
if os.path.isdir(expect): shutil.rmtree(expect)
os.makedirs(expect)
lines = export_with_refs(theme, 'scene')
# THE CHECK THEME: the round's theme (its waveform, flags and playhead scenes, the flags round's two elements over the
# second, the playhead round's two over the third; architect 2026-10-04) plus one element over a fourth scene -- the
# selected fill, the in-place editor's selection band (the open-flag round's coming element), over the flags scene with
# the first flag's editor open and its whole text selected: the selected text's antialiased edges are stacks over a
# picked base on a picked face -- so four scenes and a chooser that walks them are exercised; never exported for the
# tablet
ct = json.load(open(theme)); cpk = ct['picker']
if 'selected_fill' in {e['role'] for e in cpk['elements']}:
    raise SystemExit('check_refs.py: the round picks the selected fill itself; give the check theme another test element')
cpk['elements'].append({'key': 'selection_test', 'name': 'Selection Test', 'role': 'selected_fill', 'scene': 'flag_editor'})
cpk['scenes']['flag_editor'] = {'elements': {'ink': True, 'outline': True, 'stems': True, 'flags': True, 'playhead': False,
                                             'state_line': False}, 'flags': {'states': {'editing': [0]}}}
check_theme = os.path.join(out, 'picker_check.json'); json.dump(ct, open(check_theme, 'w'), indent=1)
lines += export_with_refs(check_theme, 'multi')
scene = os.path.join(out, 'scene'); man = json.load(open(os.path.join(scene, 'manifest.json')))
tab = bytes(colour.lin_mix((a, 0, 0), (b, 0, 0), 0.5)[0] for a in range(256) for b in range(256))
open(os.path.join(out, 'linmix.bin'), 'wb').write(tab)

# THE MODELS' REFERENCE: HSL by colorsys, LCh by numpy over Display-P3 (D65), independent of colour.h's code
import colorsys, random
_xy = [(0.680, 0.320), (0.265, 0.690), (0.150, 0.060)]; _w = (0.3127, 0.3290)
_xyz = lambda x, y: np.array([x / y, 1.0, (1 - x - y) / y])
_P = np.stack([_xyz(*c) for c in _xy], 1); _M = _P * np.linalg.solve(_P, _xyz(*_w)); _Wn = _M.sum(1)
def _lch(r, g, b):
    c = np.array([r, g, b]) / 255.0
    lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    t = (_M @ lin) / _Wn; eps, kap = 216 / 24389, 24389 / 27
    f = np.where(t > eps, np.cbrt(t), (kap * t + 16) / 116)
    L, a, bb = 116 * f[1] - 16, 500 * (f[0] - f[1]), 200 * (f[1] - f[2])
    return float(L), float(np.hypot(a, bb)), float(np.degrees(np.arctan2(bb, a)) % 360)
_set = [(r, g, b) for r in (0, 255) for g in (0, 255) for b in (0, 255)] + [(k, k, k) for k in range(256)]
_rnd = random.Random(20261004); _set += [tuple(_rnd.randrange(256) for _ in range(3)) for _ in range(20000)]
with open(os.path.join(out, 'models_ref.txt'), 'w') as f:
    for r, g, b in _set:
        hh, ll, ss = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
        f.write(' '.join(repr(v) for v in (r, g, b, hh * 360, ss, ll) + _lch(r, g, b)) + '\n')

# the synthetic derive export
der = os.path.join(out, 'derived')
if os.path.isdir(der): shutil.rmtree(der)
shutil.copytree(scene, der)
dm = json.loads(json.dumps(man)); names = [r['name'] for r in dm['roles']]
ink = names.index('ink')
dm['roles'] += [{'name': 'derived_over_canvas', 'derive': {'from': 'ink', 'over': 'canvas', 'linear_mix': 0.5}},
                {'name': 'derived_over_literal', 'derive': {'from': 'ink', 'over': '#203040', 'linear_mix': 0.25}}]
b = R.read_pnm(os.path.join(der, 'waveform.base.pgm')).copy(); xs = np.arange(b.shape[1])[None, :]
b[(b == ink) & (xs >= 1152) & (xs < 1728)] = len(names); b[(b == ink) & (xs >= 1728)] = len(names) + 1
R.write_pnm(os.path.join(der, 'waveform.base.pgm'), b)
json.dump(dm, open(os.path.join(der, 'manifest.json'), 'w'), indent=1)
back = R.picker_read(der); dl = []
for n, cs in enumerate([{}, {'ink': '#CC9966'}, {'canvas': '#203040'}, {'ink': '#CC9966', 'canvas': '#405060'}]):
    el = {**R.picker_manifest_rgb(dm), **{k: R.C.parse_colour(v) for k, v in cs.items()}}
    ppm = os.path.join(der, f'expect_{n}.ppm'); R.write_pnm(ppm, R.picker_picture(back, 'waveform', el))
    dl.append(' '.join(['waveform', ppm] + [f'{k}={v}' for k, v in cs.items()]))
open(os.path.join(der, 'expects.txt'), 'w').write('\n'.join(dl) + '\n')
print(f"references in {out}: the round's export scene/ and the check's multi/, {len(lines)} renders (their "
      f"expects.txt), linmix.bin, derived/ ({int((b == len(names)).sum())} px derived over the canvas, "
      f"{int((b == len(names) + 1).sum())} over #203040)")
