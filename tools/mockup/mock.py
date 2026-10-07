#!/usr/bin/env python3
# tools/mockup/mock.py — A MOCK-UP OVER A TABLET CAPTURE (architect 2026-10-06: "the tablet is what we're designing
# for"; the same tool for metrics, chrome, CDE, etc.). The capture's pixels are read back into the theme roles they
# were painted in (matcher.py), written out in another theme's roles, re-laid by a CHROME VOCABULARY (chrome_<name>.py:
# win95 = the capture's own stack, motif = CDE's), optionally with another icon set's glyphs in every case the scene
# lists, and saved through tools/palette/common.py's save_png (the Display-P3 iCCP chunk and nothing else, so Samsung
# Gallery shows the bytes as the app's window would). README.md beside this file is the manual.
import argparse, glob, importlib, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, 'tools', 'palette'))
import common as C                                        # noqa: E402  save_png, read_rgb: the one PNG road
import numpy as np                                        # noqa: E402
import themes                                             # noqa: E402
import scene as S                                         # noqa: E402
import glyphs                                             # noqa: E402


def vocabularies():
    return sorted(os.path.basename(p)[len('chrome_'):-3] for p in glob.glob(os.path.join(HERE, 'chrome_*.py')))


def main(argv=None):
    ap = argparse.ArgumentParser(description='Mock a theme, a chrome vocabulary or an icon set over a tablet capture.')
    ap.add_argument('--capture', required=True, help='the capture PNG, or a fragment of a scene\'s capture name')
    ap.add_argument('--theme', required=True, help='the target .theme file, or "builtin"')
    ap.add_argument('--capture-theme', default='builtin',
                    help='the theme the capture was painted in: "builtin" (the default) or a .theme file')
    ap.add_argument('--chrome', default='win95', choices=vocabularies())
    ap.add_argument('--separator', action='store_true', help="the vocabulary's separator under the toolbar")
    ap.add_argument('--legacy-dither', action='store_true',
                    help='the capture predates effd544f: its checker and caption gradient are in Windows-px cells')
    ap.add_argument('--icons', help='a folder of <Enumerator>.svg: every case the scene lists takes its glyph')
    ap.add_argument('--scene', help="the scene JSON (default: the one in scenes/ naming the capture)")
    ap.add_argument('--extra', nargs='*', default=[], metavar='KEY=VALUE',
                    help="the colours a vocabulary needs beyond the 39 roles (motif: trough, frame_ts, frame_bs)")
    ap.add_argument('-o', '--out', required=True)
    a = ap.parse_args(argv)

    try:
        cap_path, scene_path = S.find(a.capture)
        scene_path = a.scene or scene_path
        if not scene_path:
            raise S.SceneError(f"no scene in {S.SCENES} names the capture '{os.path.basename(cap_path)}'; "
                               f"pass --scene")
        sc = S.Scene(scene_path)
        CT = themes.load(a.capture_theme)
        TT = themes.load(a.theme)
        voc = importlib.import_module('chrome_' + a.chrome)
        extras = themes.parse_extras(a.extra, voc.EXTRAS)
    except (themes.ThemeError, S.SceneError) as e:
        raise SystemExit(f'tools/mockup: {e}')
    if os.path.basename(cap_path) != sc.capture:
        print(f"tools/mockup: note: {os.path.basename(scene_path)} was measured on {sc.capture}, not on "
              f"{os.path.basename(cap_path)}", file=sys.stderr)

    img = C.read_rgb(cap_path).astype(np.uint8)
    if list(img.shape[1::-1]) != list(sc.d['size_px']):
        raise SystemExit(f'tools/mockup: {cap_path} is {img.shape[1]}x{img.shape[0]}, the scene '
                         f'{sc.d["size_px"][0]}x{sc.d["size_px"][1]}')
    rc = S.render_constants()
    out, mt, placed = voc.compose(img, sc, CT, TT, extras, rc, a)

    if a.icons:
        U = sc.U
        icons = glyphs.IconSet(a.icons, rc['kIconGlyphPx'] * U)
        for lane, items in placed.items():
            names = sc.lanes[lane]['icons']
            for (case, x0, y0), name in zip(items, names):
                bg, _ = mt.case_background(case, 'TT')
                glyphs.draw_into_case(out, case, x0, y0, name, icons, U, rc['kIconCaseLeadPx'], TT, bg)

    C.save_png(a.out, out)
    counts = ', '.join(f'{lane} {len(items)} ({sum(c.disabled for c, _, _ in items)} disabled, '
                       f'{sum(c.checked for c, _, _ in items)} lit)' for lane, items in placed.items())
    print(f'{a.out}: {a.chrome} over {os.path.basename(cap_path)} ({os.path.basename(scene_path)}); cases {counts}; '
          f'{mt.unclaimed()} capture px matched no role and were left as captured')


if __name__ == '__main__':
    main()
