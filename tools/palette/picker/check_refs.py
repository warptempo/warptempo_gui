#!/usr/bin/env python3
# tools/palette/picker/check_refs.py — the laptop check's references, from the mock tool's own code (build_picker.sh
# --check runs it, then host_check against what it writes):
#
#     python3 tools/palette/picker/check_refs.py <theme.json> <dir>
#
#   <dir>/scene/           the theme exported (render.py --export), the picker's scene as the tablet receives it
#   <dir>/expect_CC9966.ppm  the theme rendered with its active layer #CC9966: the picker's picture must equal it
#   <dir>/linmix.bin       colour.lin_mix(a, b, 0.5)'s red byte for every pair (a, b), 65536 bytes, a * 256 + b
#   <dir>/derived/         a SYNTHETIC scene exercising a derived layer, which today's scene lacks (scene 1002 has no
#                          outline pixels): the export's background, its active layer's mask split at x 1152 into the
#                          picked layer (left) and a layer derived from it (right: the 50 % linear-light mix over the
#                          canvas), with its expected picture at #CC9966, expect_CC9966.ppm, composed here by colour.py
import json, os, shutil, subprocess, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); PALETTE = os.path.dirname(HERE)
sys.path.insert(0, PALETTE)

theme, out = sys.argv[1], sys.argv[2]
scene = os.path.join(out, 'scene')
if os.path.isdir(scene): shutil.rmtree(scene)
subprocess.run([sys.executable, os.path.join(PALETTE, 'render.py'), theme, '--export', scene], check=True)
import render as R, colour
man = json.load(open(os.path.join(scene, 'manifest.json')))
act = man['active']
t = json.load(open(theme)); t.setdefault('colours', {})[act] = '#CC9966'
R.write_pnm(os.path.join(out, 'expect_CC9966.ppm'), R.render_rgb(theme, data=t)[1])
tab = bytes(colour.lin_mix((a, 0, 0), (b, 0, 0), 0.5)[0] for a in range(256) for b in range(256))
open(os.path.join(out, 'linmix.bin'), 'wb').write(tab)

der = os.path.join(out, 'derived')
os.makedirs(der, exist_ok=True)
bg = R.read_pnm(os.path.join(scene, 'background.ppm'))
mask = R.read_pnm(os.path.join(scene, man['layers'][[l['name'] for l in man['layers']].index(act)]['mask'])) == 255
left = mask.copy(); left[:, 1152:] = False
right = mask & ~left
over = R.Theme(theme).get('canvas'); colour_hex = '#%02X%02X%02X' % tuple(over)
bg = bg.copy(); bg[right] = colour.lin_mix((0x80, 0x80, 0x80), tuple(over), 0.5)   # the scene as the manifest paints it
R.write_pnm(os.path.join(der, 'background.ppm'), bg)
R.write_pnm(os.path.join(der, 'picked.pgm'), left.astype(np.uint8) * 255)
R.write_pnm(os.path.join(der, 'derived.pgm'), right.astype(np.uint8) * 255)
json.dump({'width': bg.shape[1], 'height': bg.shape[0], 'background': 'background.ppm', 'active': 'picked',
           'layers': [{'name': 'picked', 'mask': 'picked.pgm', 'colour': '#808080'},
                      {'name': 'derived', 'mask': 'derived.pgm',
                       'derive': {'from': 'picked', 'over': colour_hex, 'linear_mix': 0.5}}]},
          open(os.path.join(der, 'manifest.json'), 'w'), indent=1)
exp = bg.copy(); ink = (0xCC, 0x99, 0x66)
exp[left] = ink; exp[right] = colour.lin_mix(ink, tuple(over), 0.5)
R.write_pnm(os.path.join(der, 'expect_CC9966.ppm'), exp)
print(f'references in {out}: the scene, expect_CC9966.ppm, linmix.bin, derived/ ({int(left.sum())} picked px, '
      f'{int(right.sum())} derived px over {colour_hex})')
