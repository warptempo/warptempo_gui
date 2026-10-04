#!/usr/bin/env python3
# tools/palette/picker/check_refs.py — the laptop check's references, from the mock tool's own code (build_picker.sh
# --check runs it, then host_check against what it writes):
#
#     python3 tools/palette/picker/check_refs.py <theme.json> <dir>
#
#   <dir>/scene/            the theme exported (render.py --export): the picker's export as the tablet receives it
#   <dir>/multi/            THE CHECK THEME exported (<dir>/picker_check.json: the theme plus a Flag Test element, the
#                           flag face, over a second scene with the flags on and the first flag's editor open), which
#                           the sessions drive: a switch of scene, stacks over a picked base
#   <dir>/expect/*.ppm      every scene of both rendered by render.py at the manifest's colours and at every colour set
#                           of render.picker_check_sets (each element moved, the chrome to a tint whose 32 and 96 hit
#                           the rule's half-to-even ties and to a bright ground whose lines cap, all at once)
#   <dir>/{scene,multi}/expects.txt  one line per reference: "<scene> <ppm> [<key>=#RRGGBB ...]" (the elements moved
#                           from the manifest's colours); the picker's picture at those colours must equal the ppm
#   <dir>/linmix.bin        colour.lin_mix(a, b, 0.5)'s red byte for every pair (a, b), 65536 bytes, a * 256 + b
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
# THE CHECK THEME: the round's theme plus one element over a second scene -- the first flag's face (its labels'
# antialiased edges over it: an element as the base of stacks) over a scene with the flags, their stems and the first
# flag's editor open -- so the chooser's switch of scene and the stacks over a picked base are exercised before the
# flag rounds need them (architect 2026-10-04: the flags come next); never exported for the tablet
ct = json.load(open(theme)); cpk = ct['picker']
cpk['elements'].append({'key': 'flag_test', 'name': 'Flag Test', 'role': 'flag_fill', 'scene': 'flags'})
cpk['scenes']['flags'] = {'elements': {'ink': True, 'outline': True, 'stems': True, 'flags': True, 'playhead': False,
                                       'state_line': False}, 'flags': {'states': {'editing': [0]}}}
check_theme = os.path.join(out, 'picker_check.json'); json.dump(ct, open(check_theme, 'w'), indent=1)
lines += export_with_refs(check_theme, 'multi')
scene = os.path.join(out, 'scene'); man = json.load(open(os.path.join(scene, 'manifest.json')))
tab = bytes(colour.lin_mix((a, 0, 0), (b, 0, 0), 0.5)[0] for a in range(256) for b in range(256))
open(os.path.join(out, 'linmix.bin'), 'wb').write(tab)

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
