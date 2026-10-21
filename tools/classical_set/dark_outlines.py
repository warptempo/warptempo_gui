"""tools/classical_set/dark_outlines.py: THE INPUT STYLE SHEET the Gemini prompt (prompt.md) is given — twelve Adwaita 3.22
drawings with each OBJECT'S OUTER OUTLINE inked near-black and thickened x1.5 (architect 2026-10-19: Adwaita's tonal
~1-unit outlines read soft; Red Hat's Bluecurve, whose outlines are firm ink, the guide). ONLY AN OBJECT'S OUTER OUTLINE
goes dark: "overlapping objects can have overlapping outlines", so a front object's whole outer outline is inked, also
where it crosses the object behind it; the interior lines of one object (panels, screens, buttons, lens rings, page
edges, bevels) keep their tonal color. A TILE THAT BLEEDS IS REPLACED: Gemini copies the input's drawings into its
output set (architect 2026-10-20 — the clock landed in Revert, the padlocks in Read-Only / Save / Folder, the broom in
Flatten, the wrench in Settings, GNOME's tan folder in Folder; each was replaced by a drawing that relates to no act).
The sheet carries no labels and no shadows (every drop shadow removed at the extraction or in the product's files).

THE TREATMENT, on parsed COPIES (sources never touched):
0. RECOLOR: a tile's own colors remapped at the parse, before everything below (RECOLOR, by tile): the star (tile 1, the
   model for the arrows' metal) goes from Tango's gold to OLD BRASS (architect 2026-10-21: the set's materials are Mozart's
   fortepiano's, NO GOLD anywhere), its source left verbatim.
1. CANDIDATES: every drawable whose stroke is a tonal outline — a solid or gradient stroke whose darkest opaque stop has
   HLS lightness <= L_MAX and whose effective opacity (the opacity chain x stroke-opacity) >= OP_MIN. White / light
   highlights and faint shading strokes fall out by those two numbers.
2. OBJECT OUTLINE OR INTERIOR LINE: each candidate's stroke is rasterized alone (geometry only) and tested against the
   union of every drawable painted BEFORE it (each alone, its own alpha >= 0.5, so blurred shadows and translucent
   gloss drop out). A stroke lying (>= INNER_FRAC) inside that union is an interior line of the object behind it (a
   screen, a lens ring, a button); a stroke with real length outside it bounds an object of its own (the body, or a
   front object over a back one). Per-tile overrides (FORCE_OUTER / FORCE_INNER, by element id) record the judgment calls.
3. RECOLOR: the outer outlines to near-black ink keeping a trace of hue (HLS L = INK_L, S x INK_S), opacity 1; a gradient
   stroke gets a darkened copy of its gradient (every stop the same way; a faded stop keeps its fade). ADD_OUTLINE gives
   an object with no outline at all (the 3.22 tablet) an ink one; ADD_UNDERLAY gives a bare-stroke part an ink underlay.
4. THICK: the outer outlines' stroke-width x 1.5 as well — the sheet Gemini is given. The plain-weight variant ("dark")
   and the classification review ("class": magenta = outer, cyan = interior candidate) go to the work folder alone.
The layout: 4 x 3 tiles of 480 px through resvg, 40-px gutters and margins, flat (236,236,236), NO TEXT.
Inputs: TILES — the act drawings read from assets/icons/ (read-only), the non-act drawings from style_src/ (made by
extract_style_tile.py). Outputs: tools/classical_set/style_sheet.png (the input sheet) and style_sheet.txt (its record:
the twelve tiles, the treatment, the per-tile calls); in --work (default tmp/classical_style_work/): style_sheet_dark.png,
the class sheet and its .txt (every candidate stroke's call), downscaled .jpg copies for review, the per-tile SVGs and PNGs.
Run: python3 -I tools/classical_set/dark_outlines.py [--work dir]      (--probe <svg>: every drawable in paint order)"""
import sys, os, re, copy, colorsys, subprocess, tempfile
from lxml import etree
from PIL import Image
import numpy as np

R = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))) + '/'
HERE = R + 'tools/classical_set/'
WD = R + 'tmp/classical_style_work/'   # --work replaces it
SVGNS = 'http://www.w3.org/2000/svg'
XL = '{http://www.w3.org/1999/xlink}href'

L_MAX = 0.75      # an outline is a darker shade of its fill; Tango's gray 888a85 is L .53, the light tablet's babdb6 .73
OP_MIN = 0.45     # below this a dark stroke is shading, not an outline (a gradient stop under it keeps its fade)
INNER_FRAC = 0.96 # share of a stroke inside the (eroded) earlier union that makes it an interior line
OUT_MIN_PX = 40   # ... and an outer outline needs this much stroke (mask px, 4 per unit: ~2.5 units of a 1-unit line) outside
ERODE_PX = 3      # the union eroded ~3/4 unit, so a stroke running ON the union's edge counts as outside
INK_L = 0.12
INK_S = 0.5
MPX = 192         # mask resolution

A = R + 'assets/icons/'
AS = HERE + 'style_src/'
# The twelve, row by row, left to right: (source, the record's line). The act drawings are the product's own files;
# tiles 1, 5-8 and 11 relate to no act (architect 2026-10-20: the clock, the padlocks, the wrench, the broom and the tan
# folder they replaced were copied into the output set).
TILES = [
    (AS + 'Starred.svg', '(no act) starred (the glossy brass star)'),
    (A + 'Source.svg', 'Source (accessories-dictionary)'),
    (A + 'Target.svg', 'Target (input-tablet)'),
    (A + 'PhaseReset.svg', 'PhaseReset (camera-photo)'),
    (AS + 'Optical.svg', '(no act) media-optical (the disc)'),
    (AS + 'Gaming.svg', '(no act) input-gaming (the gamepad)'),
    (AS + 'Harddisk.svg', '(no act) drive-harddisk'),
    (AS + 'Computer.svg', '(no act) computer (the monitor and keyboard)'),
    (A + 'WaveformMagnification.svg', 'WaveformMagnification (audio-volume-high)'),
    (A + 'PlayRenders.svg', 'PlayRenders (applications-multimedia)'),
    (AS + 'Internet.svg', '(no act) applications-internet (the globe)'),
    (AS + 'Cumulative.svg', '(no act since 2026-10-19) accessories-calculator'),
]
# Per-tile judgment calls, keyed by tile index 0-11: element ids forced outer / interior.
FORCE_OUTER = {}
FORCE_INNER = {}
# A body with no outline of its own gets one: id -> stroke-width in its user units (ink of its fill's darkest stop).
ADD_OUTLINE = {
    2: {'rect2676-0': 1.0},
}
# A part drawn as a bare stroke with no outline gets an ink UNDERLAY: a copy of it painted just before it, its
# stroke-width widened by this many user units (x1.5 when thick), so one outline-width of ink shows each side.
ADD_UNDERLAY = {}
# THE STAR IN OLD BRASS (architect 2026-10-21): every yellow fill and gradient stop of style_src/Starred.svg by ONE rule —
# the hue BRASS_DH degrees toward brass (Tango's 49-54 to 39-44), the saturation x BRASS_S, the lightness L' = a.L + b —
# so its lightest stop fef6b7 reads as a warm pale brass (dac284), its body edd400 as old brass (a0812e) and its darkest
# 7a6300 as deep old brass (60481c): the settled brass tile's mean 5B4024 lifted to a readable icon tone. White glints and
# black stay; the outline is inked from the brass by step 3.
BRASS_DH, BRASS_S, BRASS_L = 10, 0.55, (0.72, 0.07)
STAR_YELLOWS = ('#edd400', '#ffef65', '#fef6b7', '#eed404', '#e0b700', '#c4a000',   # its fills
                '#fce94f', '#fcf085', '#fef4a9', '#fdf4a4', '#f2c500', '#7a6300')   # its stops (and c4a000)


def brass(hexc):
    h, l, s = colorsys.rgb_to_hls(*(int(hexc[i:i + 2], 16) / 255 for i in (1, 3, 5)))
    r, g, b = colorsys.hls_to_rgb((h - BRASS_DH / 360) % 1, BRASS_L[0] * l + BRASS_L[1], s * BRASS_S)
    return '#%02x%02x%02x' % tuple(int(round(x * 255)) for x in (r, g, b))


RECOLOR = {0: {c: brass(c) for c in STAR_YELLOWS}}   # tile index -> {source hex: new hex}
KEY = [None]
NOTES = {
    0: 'the star re-tinted from Tango\'s gold to OLD BRASS (2026-10-21) by one rule over its twelve yellows: hue -%d deg, '
       'saturation x%.2f, lightness x%.2f + %.2f (fef6b7 -> %s, edd400 -> %s, 7a6300 -> %s); white glints and black as '
       'drawn' % (BRASS_DH, BRASS_S, BRASS_L[0], BRASS_L[1], brass('#fef6b7'), brass('#edd400'), brass('#7a6300')),
    1: 'the ribbon left tonal (sewn to the book; inked, the narrow ribbon read black)',
    2: 'the tablet has NO outline in 3.22 (its edge is its gradient); a 1-unit ink stroke ADDED round its body '
       '(1.5 thick); the pen\'s own black outline as drawn, darkened by the rule',
    4: 'the disc\'s outline is ONE path (path4114) ringing the rim and the center hole, so the hole\'s edge '
       'inks with the rim (both are the silhouette); the play-side rings and glints stay as drawn',
    5: 'the D-pad and the four buttons stay tonal (interior parts of the pad; the D-pad\'s own 2e3436 is '
       'already dark); the cable an open stroke, as drawn',
    6: 'the front strip\'s blurred glint (path9033, a highlight, not a shadow) pokes a little below the '
       'inked foot',
    7: 'the monitor (rect2722), the stand (path4675) and the keyboard (path3698) each their own outer '
       'outline; the screen, the bezel lines, the power button and the keys stay tonal; the cable as drawn',
    10: 'the globe (2026-10-20, replacing GNOME\'s tan folder, which bled into Folder): the rule finds its one outer '
        'outline, the sphere\'s rim (path9645), with no override; the white rim line (path9767, a highlight) and the '
        'continents\' relief stay as drawn; its floor shadow removed at the extraction',
}

DRAW = {'path', 'rect', 'circle', 'ellipse', 'polygon', 'polyline', 'line', 'use', 'text', 'image'}
NODRAW = {'defs', 'clipPath', 'mask', 'pattern', 'marker', 'symbol', 'filter', 'linearGradient', 'radialGradient',
          'metadata', 'title', 'desc', 'style', 'script', 'namedview'}
INHERIT = ('fill', 'stroke', 'stroke-width', 'stroke-opacity', 'fill-opacity', 'visibility')
NAMED = {'black': (0, 0, 0), 'white': (255, 255, 255), 'red': (255, 0, 0), 'gray': (128, 128, 128), 'grey': (128, 128, 128)}


def local(tag):
    return tag.split('}')[-1] if isinstance(tag, str) else ''


def style_of(el):
    d = {}
    for k in INHERIT + ('opacity', 'display', 'filter'):
        v = el.get(k)
        if v is not None:
            d[k] = v.strip()
    s = el.get('style')
    if s:
        for part in s.split(';'):
            if ':' in part:
                k, v = part.split(':', 1); d[k.strip()] = v.strip()
    return d


def set_style(el, upd):
    d = {}
    s = el.get('style')
    if s:
        for part in s.split(';'):
            if ':' in part:
                k, v = part.split(':', 1); d[k.strip()] = v.strip()
    d.update(upd)
    el.set('style', ';'.join('%s:%s' % kv for kv in d.items()))


def color(v):
    v = v.strip()
    if v.startswith('#'):
        h = v[1:]
        if len(h) == 3:
            h = ''.join(c * 2 for c in h)
        return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
    m = re.match(r'rgb\(\s*([\d.]+)(%?)\s*,\s*([\d.]+)%?\s*,\s*([\d.]+)%?\s*\)', v)
    if m:
        k = 2.55 if m.group(2) else 1
        return tuple(int(round(float(m.group(i)) * k)) for i in (1, 3, 4))
    return NAMED.get(v.lower())


def hls(c):
    return colorsys.rgb_to_hls(*(x / 255 for x in c))


def ink(c):
    h, l, s = hls(c)
    r, g, b = colorsys.hls_to_rgb(h, INK_L, s * INK_S)
    return '#%02x%02x%02x' % tuple(int(round(x * 255)) for x in (r, g, b))


def fnum(v, d=1.0):
    try:
        return float(re.match(r'[-+\d.eE]+', v).group(0))
    except Exception:
        return d


def recolor(root, table):
    """Every hex color in a fill, stroke, stop-color or style attribute found in table replaced by its value."""
    if not table:
        return
    pat = re.compile(r'#[0-9a-fA-F]{6}\b')
    for e in root.iter():
        if not isinstance(e.tag, str):
            continue
        for k in ('style', 'fill', 'stroke', 'stop-color'):
            v = e.get(k)
            if v and '#' in v:
                e.set(k, pat.sub(lambda m: table.get(m.group(0).lower(), m.group(0)), v))


class Doc:
    def __init__(self, path, table=None):
        parser = etree.XMLParser(resolve_entities=False, no_network=True, huge_tree=True, remove_comments=True)
        self.tree = etree.parse(path, parser)
        self.root = self.tree.getroot()
        recolor(self.root, table)
        self.ids = {e.get('id'): e for e in self.root.iter() if isinstance(e.tag, str) and e.get('id')}
        self.draw = []
        self._walk(self.root, {}, 1.0, False)

    def _walk(self, el, inh, op, hidden):
        if not isinstance(el.tag, str):
            return
        t = local(el.tag)
        if t in NODRAW:
            return
        st = style_of(el)
        cur = dict(inh)
        for k in INHERIT:
            if k in st:
                cur[k] = st[k]
        op2 = op * fnum(st.get('opacity', '1'))
        hid = hidden or st.get('display') == 'none'
        if t in DRAW:
            if not hid and cur.get('visibility') != 'hidden':
                self.draw.append((el, cur, op2, op))
            return
        for c in el:
            self._walk(c, cur, op2, hid)

    def grad_stops(self, gid, depth=0):
        g = self.ids.get(gid)
        if g is None or depth > 8:
            return None, None
        stops = [s for s in g if local(s.tag) == 'stop']
        if stops:
            out = []
            for s in stops:
                d = style_of(s)
                for k in ('stop-color', 'stop-opacity'):
                    if s.get(k) is not None and k not in d:
                        d[k] = s.get(k)
                out.append((s.get('offset', '0'), color(d.get('stop-color', '#000000')) or (0, 0, 0), fnum(d.get('stop-opacity', '1'))))
            return g, out
        h = g.get(XL) or g.get('href')
        if h and h.startswith('#'):
            _, out = self.grad_stops(h[1:], depth + 1)
            return g, out
        return g, None


def paint_info(doc, v):
    """(kind, colors-with-opacities) of a stroke value: ('solid', [(rgb,1)]) / ('grad', [(rgb,op)...], gid) / None."""
    if not v or v == 'none':
        return None
    m = re.match(r'url\(\s*#([^)\s]+)\s*\)', v)
    if m:
        g, stops = doc.grad_stops(m.group(1))
        if not stops:
            return None
        return ('grad', [(c, o) for _, c, o in stops], m.group(1))
    c = color(v)
    if c is None:
        return None
    return ('solid', [(c, 1.0)], None)


def candidate(doc, item):
    el, st, op, _ = item
    if local(el.tag) in ('text', 'image', 'use'):
        return None
    pi = paint_info(doc, st.get('stroke'))
    if pi is None:
        return None
    sop = op * fnum(st.get('stroke-opacity', '1'))
    opaque = [(c, o) for c, o in pi[1] if o * sop >= OP_MIN]
    if not opaque:
        return None
    lmin = min(hls(c)[1] for c, _ in opaque)
    if lmin > L_MAX:
        return None
    # an open stroke with no fill is a drawn line (the speaker's waves, a cable, a hand), not an object's outline
    if st.get('fill', 'black') == 'none':
        t = local(el.tag)
        if t in ('line', 'polyline') or (t == 'path' and not re.search(r'[zZ]', el.get('d', ''))):
            return None
    return pi


def render(svg_bytes, px, out_png):
    with tempfile.NamedTemporaryFile(suffix='.svg', dir=WD + 'tmp', delete=False) as f:
        f.write(svg_bytes); name = f.name
    subprocess.run(['resvg', name, out_png, '-w', str(px), '-h', str(px)], check=True, stderr=subprocess.DEVNULL)
    os.unlink(name)


def alpha_of(png):
    return np.asarray(Image.open(png).convert('RGBA'))[:, :, 3].astype(np.float32) / 255


def isolate_masks(doc, cand_idx):
    """Per drawable i: its own alpha (original style) >= .5; per candidate: its stroke alone as geometry."""
    n = len(doc.draw)
    tags = [it[0] for it in doc.draw]
    for i, e in enumerate(tags):
        e.set('data-dk', str(i))
    raw = etree.tostring(doc.root)
    own, stroke = {}, {}
    last = max(cand_idx) if cand_idx else -1
    tmpp = WD + 'tmp/m.png'

    def solo(i, geom):
        r = etree.fromstring(raw, etree.XMLParser(huge_tree=True))
        tgt = None
        for e in r.iter():
            if not isinstance(e.tag, str):
                continue
            k = e.get('data-dk')
            if k is None:
                continue
            if int(k) == i:
                tgt = e
            else:
                set_style(e, {'display': 'none'})
        if geom:
            set_style(tgt, {'fill': 'none', 'stroke': '#000000', 'stroke-opacity': '1', 'opacity': '1', 'filter': 'none'})
            a = tgt.getparent()
            while a is not None:
                if isinstance(a.tag, str) and local(a.tag) not in NODRAW:
                    set_style(a, {'opacity': '1', 'filter': 'none'})
                a = a.getparent()
        render(etree.tostring(r), MPX, tmpp)
        return alpha_of(tmpp)

    for i in range(0, last + 1):
        own[i] = solo(i, False) >= 0.5
    for i in cand_idx:
        stroke[i] = solo(i, True) >= 0.3
    for e in tags:
        del e.attrib['data-dk']
    return own, stroke


def erode(m, r):
    out = m.copy()
    for _ in range(r):
        e = out.copy()
        e[1:, :] &= out[:-1, :]; e[:-1, :] &= out[1:, :]; e[:, 1:] &= out[:, :-1]; e[:, :-1] &= out[:, 1:]
        out = e
    return out


def classify(doc, key):
    cands = {}
    for i, it in enumerate(doc.draw):
        pi = candidate(doc, it)
        eid = it[0].get('id')
        if not pi and eid in FORCE_OUTER.get(key, ()):
            pi = paint_info(doc, it[1].get('stroke'))
        if eid in ADD_OUTLINE.get(key, {}):
            fp = paint_info(doc, it[1].get('fill'))
            dark = min(fp[1], key=lambda co: hls(co[0])[1])[0]
            pi = ('solid', [(dark, 1.0)], None)
            FORCE_OUTER.setdefault(key, set()).add(eid)
        if pi:
            cands[i] = pi
    own, stroke = isolate_masks(doc, sorted(cands))
    res = {}
    acc = np.zeros((MPX, MPX), bool)
    for i in range(len(doc.draw)):
        if i in cands:
            s = stroke[i]; tot = s.sum()
            core = erode(acc, ERODE_PX)
            out_px = int((s & ~core).sum())
            frac = 1 - out_px / tot if tot else 1.0
            eid = doc.draw[i][0].get('id')
            outer = out_px >= OUT_MIN_PX and frac < INNER_FRAC
            if eid in FORCE_OUTER.get(key, ()):
                outer = True
            if eid in FORCE_INNER.get(key, ()):
                outer = False
            res[i] = (outer, frac, cands[i], out_px)
        if i in own:
            acc |= own[i]
    return res


def apply(doc, res, thick, debug=False):
    defs = doc.root.find('{%s}defs' % SVGNS)
    if defs is None:
        defs = etree.SubElement(doc.root, '{%s}defs' % SVGNS); doc.root.insert(0, defs)
    made = {}
    for el, st, op, _ in doc.draw:
        extra = ADD_UNDERLAY.get(KEY[0], {}).get(el.get('id'))
        if extra is None:
            continue
        sp = paint_info(doc, st.get('stroke'))
        dark = min(sp[1], key=lambda co: hls(co[0])[1])[0]
        u = copy.deepcopy(el); del u.attrib['id']
        set_style(u, {'stroke': '#ff00ff' if debug else ink(dark), 'stroke-opacity': '1', 'opacity': '1',
                      'stroke-width': '%g' % (fnum(st.get('stroke-width', '1')) + extra * (1.5 if thick else 1))})
        el.addprevious(u)
    for i, (outer, frac, pi, _op) in res.items():
        el, st, op, parent_op = doc.draw[i]
        if debug:
            set_style(el, {'stroke': '#ff00ff' if outer else '#00c8ff', 'stroke-opacity': '1'})
            continue
        if not outer:
            continue
        upd = {'stroke-opacity': '1'}
        own_op = fnum(style_of(el).get('opacity', '1'))
        if own_op < 1:
            upd['opacity'] = '1'
            upd['fill-opacity'] = '%g' % (own_op * fnum(st.get('fill-opacity', '1')))
        if pi[0] == 'solid':
            upd['stroke'] = ink(pi[1][0][0])
        else:
            gid = pi[2]
            if gid not in made:
                g, stops = doc.grad_stops(gid)
                ng = copy.deepcopy(g)
                for s in list(ng):
                    ng.remove(s)
                ng.set('id', gid + '-dkink')
                if not (g.get(XL) or g.get('href')):
                    pass
                for off, c, o in stops:
                    s = etree.SubElement(ng, '{%s}stop' % SVGNS)
                    s.set('offset', off); s.set('style', 'stop-color:%s;stop-opacity:%g' % (ink(c), 1 if o >= OP_MIN else o))
                defs.append(ng); made[gid] = gid + '-dkink'
            upd['stroke'] = 'url(#%s)' % made[gid]
        if thick:
            upd['stroke-width'] = '%g' % (fnum(st.get('stroke-width', '1')) * 1.5)
        added = ADD_OUTLINE.get(KEY[0], {}).get(el.get('id'))
        if added:
            upd['stroke-width'] = '%g' % (added * (1.5 if thick else 1)); upd['stroke-linejoin'] = 'round'
        set_style(el, upd)


def sheet(pngs, out, jpg):
    PX = 480; COLS = 4; CW = PX + 40
    sh = Image.new('RGB', (COLS * CW + 40, 3 * CW + 40), (236, 236, 236))
    for i, p in enumerate(pngs):
        im = Image.open(p).convert('RGBA')
        sh.paste(im, (40 + (i % COLS) * CW, 40 + (i // COLS) * CW), im)
    sh.save(out)
    sh.resize((1060, 800), Image.LANCZOS).save(jpg, quality=90)


def run():
    d = WD + 'tiles/'
    os.makedirs(d, exist_ok=True); os.makedirs(WD + 'tmp', exist_ok=True)
    outs = {'dark': [], 'thick': [], 'class': []}
    log = []
    for idx, (src, _) in enumerate(TILES):
        src = os.path.realpath(src)
        KEY[0] = idx
        res = classify(Doc(src, RECOLOR.get(idx)), idx)
        for kind, thick, dbg in (('dark', False, False), ('thick', True, False), ('class', False, True)):
            doc = Doc(src, RECOLOR.get(idx))
            apply(doc, res, thick, dbg)
            sv = d + '%02d_%s.svg' % (idx, kind); png = d + '%02d_%s.png' % (idx, kind)
            doc.tree.write(sv)
            subprocess.run(['resvg', sv, png, '-w', '480', '-h', '480'], check=True, stderr=subprocess.DEVNULL)
            outs[kind].append(png)
        doc = Doc(src, RECOLOR.get(idx))
        rows = []
        for i, (outer, frac, pi, opx) in sorted(res.items()):
            el = doc.draw[i][0]
            col = ','.join('%02x%02x%02x' % c for c, _ in pi[1])
            rows.append('   %s %-22s %-6s in=%.2f out=%4d %s' % ('OUT' if outer else 'in ', el.get('id'), local(el.tag), frac, opx, col))
        for i, it in enumerate(doc.draw):
            if i in res:
                continue
            pi = paint_info(doc, it[1].get('stroke'))
            if pi:
                sop = it[2] * fnum(it[1].get('stroke-opacity', '1'))
                rows.append('   --- %-22s %-6s op=%.2f %s' % (it[0].get('id'), local(it[0].tag), sop,
                            ','.join('%02x%02x%02x@%.2f' % (c + (o,)) for c, o in pi[1])))
        log.append('%d  %s\n%s' % (idx + 1, os.path.basename(src), '\n'.join(rows)))
    sheet(outs['thick'], HERE + 'style_sheet.png', WD + 'style_sheet.jpg')
    sheet(outs['dark'], WD + 'style_sheet_dark.png', WD + 'style_sheet_dark.jpg')
    sheet(outs['class'], WD + 'style_sheet_class.png', WD + 'style_sheet_class.jpg')
    open(WD + 'style_sheet_class.txt', 'w').write('\n'.join(log) + '\n')
    txt = 'the twelve, row by row, left to right (no labels on the sheet; 4 columns):\n'
    txt += ''.join('%d  %s\n' % (i + 1, t) for i, (_, t) in enumerate(TILES))
    txt += ('treatment: tools/classical_set/dark_outlines.py — each OBJECT\'S OUTER OUTLINE (a tonal stroke with real length '
            'outside everything painted before it; a front object over a back one included) recolored to near-black ink '
            '(HLS L %.2f, saturation x%.1f, opacity 1, gradient strokes every stop) and their stroke-width x1.5; interior '
            'lines, highlights and fills as drawn; open unfilled strokes (sound waves, cables) are lines, not outlines, and '
            'stay as drawn; a gradient stroke\'s faded stops keep their fade; where an outline path also rings holes the '
            'holes ink with it.\n' % (INK_L, INK_S))
    for k, v in sorted(NOTES.items()):
        txt += 'tile %d: %s\n' % (k + 1, v)
    open(HERE + 'style_sheet.txt', 'w').write(txt)
    print('wrote', HERE + 'style_sheet.png', '(review copies in %s)' % WD)


def probe(path):
    """every drawable in paint order with its fill, stroke and opacity."""
    d = Doc(path)
    for i, (el, st, op, _) in enumerate(d.draw):
        print('%3d %-24s %-7s op=%.2f fill=%s stroke=%s sw=%s' % (i, el.get('id'), local(el.tag), op, st.get('fill'),
              st.get('stroke'), st.get('stroke-width')))


if __name__ == '__main__':
    a = sys.argv[1:]
    if a[:1] == ['--probe']:
        probe(a[1])
    else:
        if a[:1] == ['--work']:
            WD = os.path.abspath(a[1]) + '/'
        run()
