"""tools/classical_set/extract_style_tile.py: THE INPUT STYLE SHEET'S NON-ACT TILES, each an Adwaita 3.22 drawing extracted
at its 48 slot as a standalone 48-unit SVG into tools/classical_set/style_src/, by the product's own extractor
(tools/icon_set_adwaita_322/extract.py, imported read-only, no bytecode written): the sheet pruned to the slot, nothing
redrawn, and the drop shadows (and any lettering — the style sheet carries no text) removed by id (ROWS' third column,
each id judged by eye on a solo render; --probe lists a written file's ids). Each line prints the faithful drawing's
check against the sheet cropped to the slot and against the theme's shipped 48 PNG.
Inputs: the extracted adwaita-icon-theme 3.22.0 tarball root (tools/icon_set_adwaita_322/README.md names the tarball
and its sha256); a sheet missing from that tree is given as --sheet <Tile>=<path to its src/fullcolor sheet>.
Outputs: style_src/<Tile>.svg; work files (the extractor's renders, <Tile>_480.png) in --work (default tmp/classical_style_work/extract/).
Run: python3 -I tools/classical_set/extract_style_tile.py <tarball-root> [Tile ...] [--sheet Tile=path] [--work dir] [--probe]
(no tiles: every row)."""
import sys, os, subprocess
sys.dont_write_bytecode = True
R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))) + '/'
sys.path.insert(0, R + 'tools/icon_set_adwaita_322')
import extract as X
import xml.etree.ElementTree as ET

OUT = R + 'tools/classical_set/style_src/'
# tile, 3.22 name, sheet under src/fullcolor/, the 48 slot (x, y, w, h) in the sheet's user units, the ids removed
ROWS = [
    ('Starred', 'starred', 'starred.svg', (296.062, 49.996, 48, 48),
     ['path7740']),                                # the blurred dark star behind (drop shadow); 3.22's emblem-favorite is a heart, the star is status/starred
    ('Optical', 'media-optical', 'media-optical.svg', (296, 50, 48, 48),
     ['path23417', 'path4182']),                   # the floor shadow (two blurred ellipses)
    ('Gaming', 'input-gaming', 'input-gaming.svg', (336, 30, 48, 48),
     ['path6206', 'text6292']),                    # the blurred halo; the "tangento" lettering
    ('Harddisk', 'drive-harddisk', 'drive-harddisk.svg', (296.062, 55.996, 48, 48),
     ['path8981', 'path8983']),                    # the blurred halo and the blurred black contact shadow
    # computers.svg is missing from some extracted trees: --sheet Computer=<a copy of the tarball's sheet>
    ('Computer', 'computer', 'computers.svg', (296.062, 49.996, 48, 48),
     ['rect6551', 'rect4733', 'path4789']),        # the floor shadow bar, the stand's and the keyboard's blurred shadows
    ('Internet', 'applications-internet', 'applications-internet.svg', (296, 50, 48, 48),
     ['path9641', 'path9643']),                    # the globe: its floor shadow (two blurred black ellipses); the white
                                                   # rim line and the blurred white glow at its foot are light, kept
    ('Cumulative', 'accessories-calculator', 'accessories-calculator.svg', (296, 50, 48, 48),
     ['rect12531']),                               # the blurred halo behind the calculator (the retired Cumulative
                                                   # act's drawing; its shipped file differed only by data-body-shift)
]


def main():
    args = sys.argv[1:]
    probe = '--probe' in args
    sheets, work, names = {}, R + 'tmp/classical_style_work/extract/', []
    i = 0
    rest = [a for a in args if a != '--probe']
    if not rest:
        sys.exit(__doc__)
    root = rest[0]; rest = rest[1:]
    while i < len(rest):
        if rest[i] == '--sheet':
            k, v = rest[i + 1].split('=', 1); sheets[k] = os.path.abspath(v); i += 2
        elif rest[i] == '--work':
            work = os.path.abspath(rest[i + 1]) + '/'; i += 2
        else:
            names.append(rest[i]); i += 1
    os.makedirs(work, exist_ok=True); os.makedirs(OUT, exist_ok=True)
    rows = [r for r in ROWS if not names or r[0] in names]
    unknown = set(names) - {r[0] for r in ROWS}
    if unknown:
        sys.exit('no row for %s' % ', '.join(sorted(unknown)))
    for out, name, sheet, slot, removed in rows:
        if out in sheets:   # extract() reads <root>/src/fullcolor/<sheet>: a sheet elsewhere is given relative to that
            sheet = os.path.relpath(sheets[out], os.path.join(root, 'src', 'fullcolor'))
        p = OUT + out + '.svg'
        r = X.extract(root, name, sheet, slot, p, work.rstrip('/'), removed)
        print('%-10s %-22s crop rgb %5.1f alpha %5.1f  shipped d %5.1f  removed -%d (+%d copies)' % (
            out, name, r['max_rgb_diff'], r['max_alpha_diff'], r['d_shipped_png'], len(removed), len(r['shadow_copies'])))
        subprocess.run(['resvg', p, work + out + '_480.png', '-w', '480', '-h', '480'], check=True)
        if probe:
            for e in ET.parse(p).getroot().iter():
                t = e.tag.split('}')[-1]
                if t in ('g', 'path', 'rect', 'circle', 'ellipse', 'use', 'text') and e.attrib.get('id'):
                    st = e.attrib.get('style', '')
                    flt = 'filter' in st or 'filter' in e.attrib
                    op = [s for s in st.split(';') if s.startswith('opacity')]
                    print('    %-5s %-28s %s %s %s' % (t, e.attrib['id'], 'FILTER' if flt else '', ' '.join(op),
                          'mask' if 'mask' in st or 'mask' in e.attrib else ''))


if __name__ == '__main__':
    main()
