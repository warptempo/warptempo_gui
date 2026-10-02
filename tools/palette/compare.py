#!/usr/bin/env python3
# tools/palette/compare.py — pixel parity of a render against a reference picture.
#     python3 tools/palette/compare.py <render.png> <reference.png> [--scene <tag>] [--diff out.png]
# Prints the mismatch overall and per lane (menu, icon row, trim, ruler, flags, well, bottom row): the share of
# pixels whose bytes differ at all, the share differing by more than 8 in some channel, and the max difference.
# The lanes are the scene's (scene_<tag>.json, default 1002: compare a render against the capture of the scene it
# was drawn on). The label box (render.py --label's file-name stamp, the scene's label_box) is excluded from every
# count, in both pictures. Writes diff_<render name>.png beside the render (or --diff): render | reference, side by
# side at 50 %, over a third panel's worth of difference map (red = differs by > 8, amber = 1..8) under them.
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C
import numpy as np

def main():
    argv = sys.argv[1:]
    args = [a for i, a in enumerate(argv) if not a.startswith('--') and not (i and argv[i - 1] in ('--scene', '--diff'))]
    ren, ref = args[0], args[1]
    tag = next((argv[i + 1] for i, x in enumerate(argv) if x == '--scene'), '1002')
    sc = json.load(open(os.path.join(C.HERE, f'scene_{tag}.json')))
    a = C.read_rgb(ren).astype(np.int64); b = C.read_rgb(ref).astype(np.int64)
    assert a.shape == b.shape, (a.shape, b.shape)
    lb = sc['label_box']; excl = np.zeros(a.shape[:2], bool); excl[lb['y0']:lb['y1'], lb['x0']:lb['x1']] = True
    d = np.abs(a - b).max(-1)
    L = sc['lanes']
    lanes = [('menu', L['menu']), ('icon row', L['icon']), ('trim', L['trim']), ('ruler', L['ruler']), ('flags', L['marker']),
             ('well', L['well']), ('bottom row', L['bottom']), ('ALL', [0, a.shape[0]])]
    print(f'{os.path.basename(ren)} vs {os.path.basename(ref)}  (scene {tag}; label box x {lb["x0"]}..{lb["x1"]-1} y {lb["y0"]}..{lb["y1"]-1} excluded)')
    print(f'  {"lane":11s} {"rows":>11s} {"pixels":>9s} {"differ":>9s} {"%":>7s} {">8":>8s} {"%>8":>7s} {"max":>4s}')
    out = {}
    for name, (y0, y1) in lanes:
        m = ~excl[y0:y1]; dd = d[y0:y1][m]; n = dd.size; k = int((dd > 0).sum()); k8 = int((dd > 8).sum())
        out[name] = dict(pixels=n, differ=k, differ_gt8=k8, max=int(dd.max()) if n else 0)
        print(f'  {name:11s} {y0:5d}..{y1-1:4d} {n:9d} {k:9d} {100*k/n:6.3f}% {k8:8d} {100*k8/n:6.3f}% {int(dd.max()):4d}')
    # side-by-side at 50 % (2x2 box mean) with the difference map below
    half = lambda x: ((x[0::2, 0::2] + x[1::2, 0::2] + x[0::2, 1::2] + x[1::2, 1::2]) // 4).astype(np.uint8)
    dm = np.zeros(a.shape, np.uint8); dm[:] = (16, 16, 16); dm[(d > 0)] = (200, 140, 0); dm[(d > 8)] = (255, 40, 40)
    dm[excl] = (60, 60, 120)
    top = np.concatenate([half(a), half(b)], 1)
    bot = np.concatenate([half(dm), np.full_like(half(dm), 16)], 1)
    dpath = next((sys.argv[i + 1] for i, x in enumerate(sys.argv) if x == '--diff'), None) or \
        os.path.join(os.path.dirname(os.path.abspath(ren)), 'diff_' + os.path.basename(ren))
    C.save_png(dpath, np.concatenate([top, bot], 0)); print('  wrote', dpath)
    return out

if __name__ == '__main__': main()
