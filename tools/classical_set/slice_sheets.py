"""tools/classical_set/slice_sheets.py: A GEMINI SHEET CUT BY GIMP'S FUZZY SELECT AND SLICED INTO DROP-ROAD FILES
(architect 2026-10-19: "goal is fuzzy select with thresh 20, then manual fixes as needed. png output with alpha").
Each sheet given is prompt.md's: the 56 acts of ORDER on 8 x 7 cells, left to right, top to bottom, a label under
each drawn icon (architect 2026-10-20: Gemini fills the grid and never draws a partial last row, and more cells cost
resolution, so one act the toolbar or the player wears as a twin is not drawn: PLAYERNEXT.PNG IS PLAYERPREVIOUS.PNG
FLIPPED LEFT TO RIGHT, written beside it after the cells — the silver transport glyphs are lit straight from above, so
the mirror keeps their light; Redo is drawn, its gold arrow lit from the top left like Undo's, a mirror would flip the
light). THE THREE LISTS THE SHEETS HAVE CARRIED — all 8 x 7, so nothing in a sheet tells them apart and the flag picks:
  default      ORDER: Redo at cell 34, Player Next absent (the prompt of 2026-10-20 ~10:05); PlayerNext mirrored.
  --redo-mirrored  ONEDAY: the prompt of 2026-10-20 ~09:50, the ONE COMMIT efb39ea5 (Redo absent, Player Next drawn at
               cell 47; only tmp/classical_set/gemini-new4's sheets were drawn from it): Redo = Undo mirrored, no PlayerNext
               file written (it is drawn); every cell after 33 is one off ORDER's.
  --legacy56   LEGACY56: the sheets of 2026-10-19 (Stop at cell 46, no enumerator wears it; Redo at 34 and Player Next
               drawn): 55 files, nothing mirrored.
A sheet fed the wrong list slices wrong after its first differing cell (33). A sheet from the prompt of 2026-10-20
morning (one cell more than any list, packed into seven rows with a cell lost) slices WRONG and must not be fed in. The
slicer never writes a file that is not an act.
  1. GIMP pass 1 (gimp_fuzzy.py beside this file, headless): fuzzy select at threshold 20 from the top-left corner and
     from a ground-colored point inside each nominal cell of the sheet's own rows (a sheet drawn with cell-border lines
     walls the corner flood), cleared.
  2. THE POCKETS: every enclosed block of ground still opaque after pass 1 (inside a ring, between an hourglass and its
     arrows), flat like the ground — the sheet's corner pixel — (mean within 6, the 95th percentile within 12 — a white
     page is 19 off and stays), at least 100 px, gets a seed at its own pixel nearest its centroid — his Shift-clicks.
  3. GIMP pass 2 with the pass-1 seeds plus the pocket seeds -> <out>/sheet_cut_raw.png (the sheet as GIMP cut it).
     THE SECOND POCKET PASS (pockets2, 2026-10-20): the small enclosed pockets pass 2 left opaque (the light ground
     between the starling's legs), found by structure, not flatness — the rule and its measurements at pockets2's
     head — each seeded through GIMP's fuzzy select on the raw cut, which is rewritten; the seeds are recorded in
     <out>/sheet_cut_raw.json, the marker that the raw cut carries the pass.
  4. THE LABELS: Gemini's rows are NOT equal (the label rows drift from a 540- to a 465-px pitch on one sheet), so the
     rows are read off the labels: the ink's 8-connected components at quarter scale, the glyph-sized ones (12–160 px
     tall — a two-line label pools into one block; one at least 12 px wide votes, a border dash is 4–8; <= 1200 wide — a
     glyph, a pooled label line, or two labels touching across cells); the seven densest ±80-px windows of glyph centers
     (>= 20 glyphs, suppressing ±240 px round each — a loose fragment inside an icon, a repeat sign's dots, never chains
     two rows) are the seven label rows; each row is bounded by its members within 100 px of its
     peak (a two-line label's second line is ~60 px under the first). A LABEL GLYPH IS NEUTRAL INK (architect 2026-10-20:
     the label text is black on the light ground; an icon part is colored, or a dark outline round a colored fill — Add to
     Selection's gold plus stands high beside the hand, inside the window of the label row above, and the size rule alone
     cleared it with the text): a component with COLOR_SHARE (2 %) or more of its ink pixels COLORED — chroma (max - min of
     the JPEG's RGB) >= 40 — is an icon part and is NEVER cleared nor voting, whatever its size. Chroma, not HSV
     saturation (JPEG noise on near-black text gives S ~ .12; its chroma stays ~1-2), and a share, not a mean (the plus
     is a dark outline round a gold fill: the mean over its dark pixels is 7, the share 44-66 %; text's share is 0 on
     every sheet). ONLY THE GLYPH BOXES ARE CLEARED, each grown 8 px
     — a row's icons may rise into the two-line label band above them (the 16:16 sheet's Down arrow under "33. Undo"),
     so a full-width clear cut their tops. A cell's icon band runs from under the cleared label boxes of the cell above
     it to the top of its own label's boxes, both read per cell (an icon may rise into a neighbor's two-line band or
     set its foot under a neighbor's taller label; a label piece pooled into the icon above stays out). A row's
     columns are its content segments — ink runs parted by >= 60 px (an icon's own gap is narrower), themselves >= 60 px
     wide (a dash column, a border line or a speck is no column): as many as the row has cells give the edges at the
     gutter centers directly; more is one of his sheets with extra unlabeled icons at the right (213712, 212409), the
     first ones the acts; fewer (a bordered sheet whose lines join everything) fall back to the empty gutter nearest
     each W/8 multiple (the 16:16 sheet's first gutter at 676, not 593: an icon's edge was landing in its neighbor's
     cell). -> <out>/sheet_cut.png (his manual-fix base, full resolution, alpha).
  5. THE CELLS: each icon's ink box (alpha >= 26, specks and edge border dashes dropped) centered in a square at 44/48
     fill, LANCZOS to 256 -> <out>/<Act>.png, named by the act's enumerator (src/gui/icons.h's kIconNames); then each act in
     MIRRORS the list lacks is its source flipped left to right (PIL FLIP_LEFT_RIGHT, the same 256-px RGBA); a
     contact.png of the cells on the Face gray for the check, each mirrored act a tile of its own in an eighth row.
Inputs: the sheet JPEGs on the command line. Outputs: <out> = <the sheet's folder>/sliced/<the file's 14-digit stamp>/
(--out <dir>: <dir>/sliced/<stamp>/), never in the repository; GIMP's job lists and the pass-1 cut in the sibling
slice_work/. SLICE_REUSE=1 skips GIMP passes 1 and 2 where sheet_cut_raw.png exists (a re-slice after a rule change, ~5 s
a sheet; ~30 s with GIMP), and the second pocket pass too where sheet_cut_raw.json records it (a raw cut without the
record gets the pass: ~3 s to find the seeds, one GIMP run when there are any). A 'CLIPPED?' line names a cell whose ink continues just past its band's edge: so far always
a label piece pooled into the icon above it (outside the crop, left in sheet_cut.png) or a drawn border's dash, never a
cut icon — but look. 'EMPTY' names a cell with no ink.
Run: python3 -I tools/classical_set/slice_sheets.py [--redo-mirrored | --legacy56] [--out dir] sheet.jpg ..."""
import os, sys, json, subprocess, numpy as np
from collections import deque
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__)) + '/'
CHROMA_COLORED = 40; COLOR_SHARE = .02   # label_rows' neutral-ink line: label text has no pixel of chroma 40; a gold plus, a skin tone is a half colored
P2_MIN, P2_MEAN, P2_STROKE, P2_RIM, P2_FAR, P2_DIST = 20, 10, 200, .6, .8, (20, 64)   # pockets2's bounds, read off the 30 sheets of 2026-10-20
T = 20; FILL = 44 / 48   # the ground color is each sheet's own corner pixel (the prompt says ECECEC; 212907 came out 226)
_CELLS = ['OpenProject', 'Save', 'SaveCommit', 'SavePull', 'Revert', 'LoadInPlace', 'Render', 'Cancel', 'PlayRenders', 'FolderRow', 'WavRow', 'DeleteFolder', 'Close',
          'Source', 'Target', 'Warp', 'PhaseReset', 'FullZoomOut', 'Center', 'WaveformMagnification',
          'DropMarker', 'DeleteMarkers', 'Disabled', 'Inherit', 'Flatten', 'Follow', 'GridIterations', 'AddToSelection', 'JumpToDefiningMarker', 'OpenTextEditor', 'ValueDrag', 'ReadOnly',
          'Undo', 'HistoryView', 'Older', 'Newer',
          'Left', 'Right', 'Up', 'Down', 'PreviousMarker', 'NextMarker', 'SwitchTab',
          'Play', 'Pause', 'PlayerPrevious', 'PlayerNext', 'RepeatOne',
          'Settings', 'Tooltips', 'CardNormal',
          'BpmIterations', 'CardCritical', 'UpAFolder',
          'TransportPlay', 'TransportStop']   # the 56 of the ONE-COMMIT list (Redo absent, Player Next drawn): the base of the other two
COLS = 8; ROWS = 7
ONEDAY = _CELLS   # --redo-mirrored: the prompt of 2026-10-20 ~09:50
ORDER = _CELLS[:33] + ['Redo'] + _CELLS[33:46] + _CELLS[47:]   # the current prompt: Redo drawn at cell 34, Player Next not drawn
_WITH_REDO = _CELLS[:33] + ['Redo'] + _CELLS[33:]
LEGACY56 = _WITH_REDO[:45] + [None] + _WITH_REDO[45:55]   # --legacy56: cell 46 the silver Stop (no act), no Transport pair
MIRRORS = {'Redo': 'Undo', 'PlayerNext': 'PlayerPrevious'}   # an act no sheet of a list draws, made from its twin
assert len(ORDER) == len(ONEDAY) == len(LEGACY56) == COLS * ROWS
assert ORDER[33] == 'Redo' and 'PlayerNext' not in ORDER and ORDER[46] == 'PlayerPrevious' and ONEDAY[46] == 'PlayerNext'
assert LEGACY56[33] == 'Redo' and LEGACY56[45] is None and 'PlayerNext' in LEGACY56

def gimp(jobs, tag, work):
    jp = work + 'slice_jobs_%s.json' % tag; json.dump(jobs, open(jp, 'w'))
    env = dict(os.environ, GIMP_FUZZY_JOBS=jp, GIMP_FUZZY_THRESH=str(T))
    r = subprocess.run(['gimp-console', '-i', '--quit', '--batch-interpreter=python-fu-eval', '-b', open(HERE + 'gimp_fuzzy.py').read()],
                       env=env, capture_output=True, text=True, timeout=600)
    wrote = [l for l in r.stdout.splitlines() if l.startswith('wrote')]
    if len(wrote) != len(jobs): sys.exit('gimp: %d of %d jobs written\n%s\n%s' % (len(wrote), len(jobs), r.stdout[-2000:], r.stderr[-2000:]))
    for l in wrote: print(' ', l)

def cell_seeds(src, rows):
    """The corner plus a seed 24 px inside each nominal cell's top-left corner where the source is ground-colored: a sheet
    drawn with faint cell-border lines (212907) walls the corner flood into one cell — his Shift-click in every cell. The
    grid is the sheet's seven rows: fuzzy select compares against each SEED's color, so every seed widens the
    cut a little where the JPEG's ground varies, and a grid of the wrong pitch is a different cut."""
    rgb = np.asarray(Image.open(src).convert('RGB')).astype(np.int16); H, W, _ = rgb.shape; seeds = [[2, 2]]; ground = rgb[2, 2]   # the corner IS the ground: 212907's is 226, not the prompt's 236
    for j in range(rows):
        for k in range(COLS):
            x, y = round(k * W / COLS) + 24, round(j * H / rows) + 24
            if (k or j) and np.abs(rgb[y, x] - ground).max() <= 6: seeds.append([x, y])
    return seeds

def pockets(src, pass1):
    rgb = np.asarray(Image.open(src).convert('RGB')).astype(np.int16); A = np.asarray(Image.open(pass1).convert('RGBA'))[..., 3]; ground = rgb[2, 2]
    ok = np.abs(rgb - ground).max(axis=2) <= T; outer = A < 255
    near = np.zeros_like(outer); near[1:] |= outer[:-1]; near[:-1] |= outer[1:]; near[:, 1:] |= outer[:, :-1]; near[:, :-1] |= outer[:, 1:]
    cand = ok & ~outer & ~near; H, W = cand.shape; seen = np.zeros_like(cand); seeds = []
    for y0, x0 in zip(*np.nonzero(cand)):
        if seen[y0, x0]: continue
        q = deque([(y0, x0)]); seen[y0, x0] = True; pts = []
        while q:
            y, x = q.popleft(); pts.append((y, x))
            for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                if 0 <= ny < H and 0 <= nx < W and cand[ny, nx] and not seen[ny, nx]: seen[ny, nx] = True; q.append((ny, nx))
        if len(pts) < 100: continue
        P = np.array(pts); d = np.abs(rgb[P[:, 0], P[:, 1]] - ground).max(axis=1)
        if d.mean() > 6 or np.percentile(d, 95) > 12: continue
        c = P.mean(axis=0); k = int(np.argmin(((P - c) ** 2).sum(axis=1))); seeds.append([int(P[k, 1]), int(P[k, 0])])
    return seeds

def grow(m, k):
    """m dilated k steps, 4-connected (the window carries a margin wider than k, so np.roll's wrap never reaches it)."""
    for _ in range(k): m = m | np.roll(m, 1, 0) | np.roll(m, -1, 0) | np.roll(m, 1, 1) | np.roll(m, -1, 1)
    return m

def pockets2(src, raw):
    """THE SECOND POCKET PASS (architect 2026-10-20: "add a post-processing step" for the light ground left between the
    starling's legs — "many starling images have them"), on the full-resolution cut: the seeds of the enclosed ground
    pockets pass 2 left opaque. WHY THE FIRST RULE MISSED THEM: not the size floor — the starling's pocket on 100905 is
    145 px — but the flatness: a pocket a few dozen px across is mostly RIM, and the JPEG rings along the outline ink, so
    its mean distance from the ground is 8.3 and its p95 16 (the first rule wants 6 and 12); its interior (two px in) is
    6.2 / 9. A TIGHTER COLOR RULE CANNOT FIND IT: the lace cuff's holes (mean 4), the silver keys' highlights (2-6), the
    clapperboard's white stripes (4-5) and the slate's chalk lines are as close to the ground as the pocket or closer. So
    the pass is STRUCTURAL — a pocket is ground seen THROUGH the object, not a light mark painted on it. Each enclosed
    opaque region with every pixel within T of the ground (as the GIMP cut reads it) is a pocket when:
      size >= P2_MIN px (a speck in a lace hole or a bird's eye catchlight is smaller; a ring sliver of 20 is kept);
      ground-colored: its per-channel mean offset NEUTRAL (spread <= 4 — the cream page, parchment, the gold are not),
      its mean distance <= P2_MEAN and its interior's (two px in) <= P2_MEAN;
      an AREA, not a stroke: one of >= P2_STROKE px has an interior (a slate's chalk line is 800 px and nowhere 5 thick);
      its RIM (the 4 px round it) >= P2_RIM outline ink (every channel < 90): ground meets the silhouette's outline, while
        a lace hole meets gray lace and a key's highlight gray metal;
      its FAR RING (10-24 px out, the cut excluded) < P2_FAR ink: past the outline lies the object's own fill (the gold
        bow, the orange legs), while a clapper stripe, a slate's chalk line and a black bird's eye sit inside black paint;
      its DISTANCE to the cut within P2_DIST: >= 20 px, a whole object part (outline, fill, outline) between it and the
        outside — a white baton or an edge stripe has one outline alone; <= 64 px, a hole through the object, never a
        mark in the middle of a page or a medallion.
    Each passes as one seed at its pixel nearest its centroid, the first pass's way."""
    rgb = np.asarray(Image.open(src).convert('RGB')).astype(np.int16); A = np.asarray(Image.open(raw).convert('RGBA'))[..., 3]
    g = rgb[2, 2]; d = np.abs(rgb - g).max(axis=2); outer = A < 255
    near = outer.copy(); near[1:] |= outer[:-1]; near[:-1] |= outer[1:]; near[:, 1:] |= outer[:, :-1]; near[:, :-1] |= outer[:, 1:]
    cand = (d <= T) & ~outer; H, W = cand.shape; seen = np.zeros_like(cand); seeds = []; M = P2_DIST[1] + 6
    for y0, x0 in zip(*np.nonzero(cand)):
        if seen[y0, x0]: continue
        q = deque([(y0, x0)]); seen[y0, x0] = True; pts = []; touch = False
        while q:
            y, x = q.popleft(); pts.append((y, x)); touch |= bool(near[y, x])
            for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                if 0 <= ny < H and 0 <= nx < W and cand[ny, nx] and not seen[ny, nx]: seen[ny, nx] = True; q.append((ny, nx))
        if touch or len(pts) < P2_MIN: continue
        P = np.array(pts); off = (rgb[P[:, 0], P[:, 1]] - g).mean(axis=0); v = d[P[:, 0], P[:, 1]]
        if off.max() - off.min() > 4 or v.mean() > P2_MEAN: continue
        ya, xa = P.min(axis=0) - M; h, w = np.ptp(P[:, 0]) + 2 * M + 1, np.ptp(P[:, 1]) + 2 * M + 1
        m = np.zeros((h, w), bool); m[P[:, 0] - ya, P[:, 1] - xa] = True
        ys, xs = np.mgrid[ya:ya + h, xa:xa + w]; inb = (ys >= 0) & (ys < H) & (xs >= 0) & (xs < W)
        a = np.zeros((h, w), np.int32); c = np.zeros((h, w, 3), np.int16); a[inb] = A[ys[inb], xs[inb]]; c[inb] = rgb[ys[inb], xs[inb]]
        ink = c.max(axis=2) < 90; out = a < 128
        e = m.copy()
        for _ in range(2): e = e & np.roll(e, 1, 0) & np.roll(e, -1, 0) & np.roll(e, 1, 1) & np.roll(e, -1, 1)
        if (e.any() and np.abs(c[e] - g).max(axis=1).mean() > P2_MEAN) or (len(P) >= P2_STROKE and not e.any()): continue
        if ink[grow(m, 4) & ~m].mean() < P2_RIM: continue
        d10 = grow(m, 10); far = grow(d10, 14) & ~d10 & ~out
        if far.any() and ink[far].mean() >= P2_FAR: continue
        k, r = 0, m
        while k < P2_DIST[1] and not (r & out).any(): r = grow(r, 1); k += 1
        if not P2_DIST[0] <= k <= P2_DIST[1] or not (r & out).any(): continue
        cy, cx = P.mean(axis=0); i = int(np.argmin((P[:, 0] - cy) ** 2 + (P[:, 1] - cx) ** 2)); seeds.append([int(P[i, 1]), int(P[i, 0])])
    return seeds

def runs(mask):
    out = []; start = None
    for i, v in enumerate(mask):
        if v and start is None: start = i
        if not v and start is not None: out.append((start, i)); start = None
    if start is not None: out.append((start, len(mask)))
    return out

def color_share(rgb, ink, S, pts, box):
    """The share of the quarter-scale component `pts` (box (ya, yb, xa, xb) in quarter cells)'s ink pixels whose JPEG chroma
    (max - min of RGB) is >= CHROMA_COLORED: ~0 for label text, .4-.9 for an icon part with a colored fill."""
    ya, yb, xa, xb = box; mm = np.zeros((yb - ya, xb - xa), bool)
    for y, x in pts: mm[y - ya, x - xa] = True
    full = np.kron(mm, np.ones((S, S), bool)) & ink[ya * S:yb * S, xa * S:xb * S]
    px = rgb[ya * S:yb * S, xa * S:xb * S][full].astype(np.int16)
    return float(((px.max(axis=1) - px.min(axis=1)) >= CHROMA_COLORED).mean()) if len(px) else 0.

def label_rows(ink, rgb):
    """The label rows as (top, bottom, the glyph boxes to clear, the peak) in full-res y, read off the glyph-sized
    NEUTRAL-INK components (rgb the sheet's JPEG): the seven rows."""
    S = 4; H, W = ink.shape; m = ink[:H // S * S, :W // S * S].reshape(H // S, S, W // S, S).any(axis=(1, 3)); h, w = m.shape
    seen = np.zeros_like(m); boxes = []
    for y0, x0 in zip(*np.nonzero(m)):
        if seen[y0, x0]: continue
        q = deque([(y0, x0)]); seen[y0, x0] = True; ya, yb, xa, xb = y0, y0, x0, x0; pts = []
        while q:
            y, x = q.popleft(); pts.append((y, x)); ya, yb, xa, xb = min(ya, y), max(yb, y), min(xa, x), max(xb, x)
            for ny in (y - 1, y, y + 1):
                for nx in (x - 1, x, x + 1):
                    if 0 <= ny < h and 0 <= nx < w and m[ny, nx] and not seen[ny, nx]: seen[ny, nx] = True; q.append((ny, nx))
        box = (ya, yb + 1, xa, xb + 1)
        if 3 <= box[1] - box[0] <= 40 and box[3] - box[2] <= 300 and color_share(rgb, ink, S, pts, box) >= COLOR_SHARE: continue   # colored: an icon part, never a label glyph whatever its size
        boxes.append(box)
    glyphs = [b for b in boxes if 3 <= b[1] - b[0] <= 40 and b[3] - b[2] <= 300]   # a glyph, a pooled label line or two-line block, or two labels touching across cells
    votes = np.array([b[3] - b[2] >= 3 for b in glyphs])   # a dash of a drawn cell border (28 x 4 px) is glyph-sized too: the narrow never vote for a row or bound it, they are only cleared
    cy = np.array([(b[0] + b[1]) / 2 for b in glyphs])
    dens = np.array([((cy >= t - 20) & (cy <= t + 20) & votes).sum() for t in range(h)]); peaks = []   # voting glyphs within ±80 px of each row
    for _ in range(ROWS):   # the seven densest windows, each suppressing ±240 px (a loose fragment inside an icon never chains two rows)
        t = int(np.argmax(dens))
        if dens[t] < 20: break
        peaks.append(t); dens[max(0, t - 60):t + 61] = 0
    assert len(peaks) == ROWS, peaks
    bands = []
    for t in sorted(peaks):
        r = [b for b, c, v in zip(glyphs, cy, votes) if abs(c - t) <= 25 and v]; clear = [b for b, c in zip(glyphs, cy) if abs(c - t) <= 25]
        bands.append((min(b[0] for b in r) * S, max(b[1] for b in r) * S, [(b[0] * S, b[1] * S, b[2] * S, b[3] * S) for b in clear], t * S))
    return bands

def ink_box(ink, x0, x1, y0, y1):
    """The icon's ink box in its cell: row and column runs of >= 30 px. A run <= 16 px thick whose ink spans >= 60 % of
    the cell's other side is a dashed cell border Gemini drew against the prompt (the 16:17 sheet: vertical dashes at the
    column edges, horizontal ones between the rows), dropped before the box is read — the prompt allows no loose line."""
    sub = ink[y0:y1, x0:x1].copy(); h, w = sub.shape   # the dash profiles count >= 20 px, so a 4-px dash line across never hides the thin run along
    for a, b in runs(sub.sum(axis=0) >= 20):
        if b - a <= 16 and (sub[:, a:b].any(axis=1).sum() >= .5 * h or a < 30 or b > w - 30): sub[:, a:b] = False
    for a, b in runs(sub.sum(axis=1) >= 20):
        if b - a <= 16 and sub[a:b].any(axis=0).sum() >= .6 * w: sub[a:b] = False
    rr = [r for r in runs(sub.sum(axis=1) >= 1) if sub[r[0]:r[1]].sum() >= 30]
    cc = [r for r in runs(sub.sum(axis=0) >= 1) if sub[:, r[0]:r[1]].sum() >= 30]
    if not rr or not cc: return None
    return x0 + cc[0][0], y0 + rr[0][0], x0 + cc[-1][1], y0 + rr[-1][1]

def slice_sheet(src, root, acts):
    stamp = os.path.basename(src)[:14]; root = root or os.path.dirname(os.path.abspath(src))
    out = os.path.join(root, 'sliced', stamp) + '/'; work = os.path.join(root, 'slice_work') + '/'
    os.makedirs(out, exist_ok=True); os.makedirs(work, exist_ok=True)
    p1 = work + 'slice_%s_pass1.png' % stamp; raw = out + 'sheet_cut_raw.png'
    side = out + 'sheet_cut_raw.json'   # the raw cut's record: which passes it carries
    if os.environ.get('SLICE_REUSE') and os.path.exists(raw): print(stamp, 'reusing', raw)
    else:
        first = cell_seeds(src, ROWS); print(stamp, 'pass 1, cell seeds', len(first)); gimp([{'src': src, 'out': p1, 'seeds': first}], stamp + '_1', work)
        seeds = first + pockets(src, p1); print(stamp, 'pockets', len(seeds) - len(first))
        gimp([{'src': src, 'out': raw, 'seeds': seeds}], stamp + '_2', work)
        if os.path.exists(side): os.remove(side)
    done = json.load(open(side)) if os.path.exists(side) else {}
    if 'pockets2' not in done:   # THE SECOND POCKET PASS on the raw cut, GIMP's fuzzy select from each seed as pass 2's pockets
        more = pockets2(src, raw); print(stamp, 'pockets2', len(more), more)
        if more:
            p3 = work + 'slice_%s_pockets2.png' % stamp; gimp([{'src': raw, 'out': p3, 'seeds': more}], stamp + '_3', work); os.replace(p3, raw)
        done['pockets2'] = more; json.dump(done, open(side, 'w'))
    rgb = np.asarray(Image.open(src).convert('RGB')); im = Image.open(raw).convert('RGBA'); W, H = im.size; A = np.asarray(im)[..., 3]; ink = A >= 26
    cw = W / COLS; alpha = A.copy(); bands = label_rows(ink, rgb); print(stamp, 'label rows', [(int(t), int(b)) for t, b, _, _ in bands])
    for _, _, boxes, _ in bands:   # ONLY THE GLYPH BOXES are cleared, each grown 8 px: a row's icons may rise into the label band above them
        for y0, y1, x0, x1 in boxes: alpha[max(0, y0 - 8):y1 + 8, max(0, x0 - 8):x1 + 8] = 0
    def col_edges(r, n=COLS):   # the row's n + 1 cell edges (n its eight cells)
        y0, y1 = (bands[r - 1][1] if r else 0), bands[r][0]; prof = ink[y0:y1].sum(axis=0) == 0
        empty = []   # empty runs; two parted by <= 16 px of ink (a drawn border's dash column) are one gutter
        for a, b in runs(prof):
            if empty and a - empty[-1][1] <= 16: empty[-1] = (empty[-1][0], b)
            else: empty.append((a, b))
        segs = []   # the content columns: ink runs, two parted by < 60 px joined (an icon's own gap; a gutter is wider)
        for a, b in runs(~prof):
            if segs and a - segs[-1][1] < 60: segs[-1] = (segs[-1][0], b)
            else: segs.append((a, b))
        segs = [(a, b) for a, b in segs if b - a >= 60]   # a dash column, a border line or a speck is no column
        if len(segs) >= n:   # the cells parted by their gutters; more is a sheet with EXTRA, UNLABELED ICONS at the right (213712, 212409) — the first n are the acts
            return [0] + [(segs[i][1] + segs[i + 1][0]) // 2 for i in range(n - 1)] + [(segs[n - 1][1] + segs[n][0]) // 2 if len(segs) > n else W]
        gutters = [((a + b) / 2, b - a) for a, b in empty if b - a >= 20 and a > 0 and b < W]; edges = [0]
        for k in range(1, n):   # the gutter nearest each W/8 multiple, else that multiple — Gemini's columns drift like its rows
            near = [g for g in gutters if abs(g[0] - k * cw) <= 150]; edges.append(round(max(near, key=lambda g: g[1])[0]) if near else round(k * cw))
        return edges + [W]
    edges = [col_edges(r) for r in range(len(bands))]
    def own_boxes(r, c):   # the cleared label boxes standing in the cell's columns (a dash never counts)
        x0, x1 = edges[r][c], edges[r][c + 1]; return [b for b in bands[r][2] if b[2] < x1 and b[3] > x0 and b[3] - b[2] >= 12]
    def cell_top(r, c):   # under the cell above's own label (one line or two); the row's center line where it left no box
        if not r: return 0
        bots = [b[1] for b in own_boxes(r - 1, c)]; return max(bots) + 4 if bots else bands[r - 1][3]
    def cell_bot(r, c):   # the cell's own label's top, no margin (the 8-px growth already clears above it): an icon's foot or handle tip may
        tops = [b[0] for b in own_boxes(r, c)]; return min(tops) if tops else bands[r][0]   # sit under a neighbor's taller two-line label's top
    arr = np.asarray(im).copy(); arr[..., 3] = alpha; clean = Image.fromarray(arr); clean.save(out + 'sheet_cut.png'); ink = alpha >= 26
    made = [a for a in MIRRORS if a not in acts]   # the acts this list mirrors
    contact = Image.new('RGBA', (COLS * 112, (len(bands) + bool(made)) * 112), (0x62, 0x6C, 0x7B, 255))   # the eighth row holds the mirrored acts
    for i, act in enumerate(acts):
        if act is None: continue   # a legacy sheet's silver Stop: no act, no file
        c, r = i % COLS, i // COLS; t0, t1 = cell_top(r, c), cell_bot(r, c)   # the icon band of a cell: from the label above it to its own
        box = ink_box(ink, edges[r][c], edges[r][c + 1], t0, t1)
        if box is None: print('  EMPTY', act); continue
        bx0, by0, bx1, by1 = box
        l0, l1 = edges[r][c], edges[r][c + 1]   # ink continuing past the band's edge in the box's columns: the icon crosses it (or a label piece sits on it) — look
        if (t0 and ink[max(0, t0 - 6):t0, bx0:bx1].any()) or ink[t1:t1 + 6, bx0:bx1].any() or (l0 and ink[by0:by1, max(0, l0 - 6):l0].any()) or ink[by0:by1, l1:l1 + 6].any():
            print('  CLIPPED?', stamp, act, box, 'band', (t0, t1), 'cols', (l0, l1))
        side = int(np.ceil(max(bx1 - bx0, by1 - by0) / FILL)); sq = Image.new('RGBA', (side, side), (0, 0, 0, 0))
        crop = clean.crop(box); sq.paste(crop, ((side - crop.size[0]) // 2, (side - crop.size[1]) // 2))
        icon = sq.resize((256, 256), Image.LANCZOS); icon.save(out + act + '.png')
        g = icon.resize((96, 96), Image.LANCZOS); contact.paste(g, (c * 112 + 8, r * 112 + 8), g)
    for k, act in enumerate(made):   # THE MIRRORED TWINS (architect 2026-10-20): the source's file flipped left to right, never drawn
        twin = Image.open(out + MIRRORS[act] + '.png').transpose(Image.FLIP_LEFT_RIGHT); twin.save(out + act + '.png')
        g = twin.resize((96, 96), Image.LANCZOS); contact.paste(g, (k * 112 + 8, len(bands) * 112 + 8), g)
    contact.save(out + 'contact.png'); print(stamp, '->', out)

def main():
    a = sys.argv[1:]; root = None
    if '--legacy56' in a and '--redo-mirrored' in a: sys.exit('--legacy56 and --redo-mirrored name two lists: give one')
    acts = LEGACY56 if '--legacy56' in a else ONEDAY if '--redo-mirrored' in a else ORDER
    a = [x for x in a if x not in ('--legacy56', '--redo-mirrored')]
    if a[:1] == ['--out']: root = os.path.abspath(a[1]); a = a[2:]
    if not a: sys.exit(__doc__)
    for src in a: slice_sheet(os.path.abspath(src), root, acts)

if __name__ == '__main__':
    main()
