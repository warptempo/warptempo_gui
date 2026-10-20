"""tools/classical_set/flag_side.py: WHICH SIDE OF ITS POLE EACH SLICED FLAG FLIES (architect 2026-10-20: "all flags
should point to the right" — the pennant's cloth flies to the RIGHT of its pole on every cell that carries it; Next
Marker is the usual offender, Gemini turning the cloth to meet the arrow that enters it from the left). Per sheet, the
four acts of the Flags family (family_sheets.py) are probed, each 256-px tile alone:
  THE FILLS: the opaque pixels that are not outline ink (alpha >= 128, some channel >= 100) parted into 4-connected
  regions of >= 1500 px — the outlines part one object from the next, and a cloth's gloss and fold shading stay one
  fill; a GOLD fill is one with half its pixels gold (hue 25–65 deg, HSV saturation >= .35, value >= .45; the cream
  page and the gray pigeon are not gold). Each gold fill's BODY is its rows at least half its widest row (a finial
  above, a gold pole's foot below stay out), its two ENDS the body's outermost columns.
  THE POLE stands at an end where (1) a near-vertical stroke of outline ink runs on BELOW the body for >= 60 % of the
  body's height (pole_run: a tilted pole is followed, an arrowhead's diagonal is not), (2) one also runs ABOVE it for
  >= 10 px — the finial and the pole's top — (3) the run below is >= 1.5 x the run above (a repeat sign's bar beside
  an arrow runs both ways alike; a pole carries its cloth at the top), and (4) the fill is full-height at that end
  (its outer 5 % of columns at >= 35 % of its tallest column: an arrow's point is not, a tilted hoist still is).
  WHY NOT THE TALLEST DARK RUN (the first draft): on these tiles the tallest near-vertical run of outline ink is as
  often the score page's edge, an arrow's tail or a repeat bar as the pole, and a tilted pole has none; the gold
  arrow's own fill fails (2) or (4), the pigeon and the page are not gold, so the cloth is the gold fill with a pole
  at exactly one end (the largest, if several).
  THE VERDICT: the cloth's mass right of the pole against its mass left of it — RIGHT or LEFT. No gold fill with a
  pole at exactly one end is UNDECIDED: left visible and named in the table for the planner's eye (on the 2026-10-19
  sheets every Previous / Next Marker and Jump cell is an arrow with no flag, so all of them are UNDECIDED).
With --hide <hide.json>, every LEFT tile is added to that file's "tile" list for its sheet (family_sheets.py's schema;
the group is all or nothing, so the sheet's whole Flags block is omitted from the pages), the file rewritten in place.
A wrong verdict is fixed here, never by hand in the json.
Inputs: the sliced folders (slice_sheets.py's <folder>/sliced/), each with a tag prefix as family_sheets.py takes them.
Outputs: the table on stdout (tag, act, left mass, right mass, verdict); with --hide, the hide.json.
Run: python3 -I tools/classical_set/flag_side.py [--hide <hide.json>] <prefix>=<sliced folder> ..."""
import sys, json
from collections import deque
from pathlib import Path
import numpy as np
from PIL import Image

ACTS = ["DropMarker", "PreviousMarker", "NextMarker", "JumpToDefiningMarker"]   # the Flags family's members
MIN_AREA, INK, POLE_WIN, POLE_GAP, DRIFT, BELOW, FINIAL, ABOVE, EDGE, HOIST_FULL, GOLD_SHARE = 1500, 100, 12, 4, .6, .6, 10, 1.5, .05, .35, .5


def gold_mask(a):
    rgb = a[..., :3].astype(float) / 255; mx, mn = rgb.max(axis=2), rgb.min(axis=2); d = np.maximum(mx - mn, 1e-6)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    hue = np.where(mx == r, (60 * (g - b) / d) % 360, np.where(mx == g, 60 * (b - r) / d + 120, 60 * (r - g) / d + 240))
    sat = (mx - mn) / np.maximum(mx, 1e-6)
    return (a[..., 3] >= 128) & (hue >= 25) & (hue <= 65) & (sat >= .35) & (mx >= .45)


def components(m):
    H, W = m.shape; seen = np.zeros_like(m); out = []
    for y0, x0 in zip(*np.nonzero(m)):
        if seen[y0, x0]:
            continue
        q = deque([(y0, x0)]); seen[y0, x0] = True; pts = []
        while q:
            y, x = q.popleft(); pts.append((y, x))
            for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                if 0 <= ny < H and 0 <= nx < W and m[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True; q.append((ny, nx))
        if len(pts) >= MIN_AREA:
            out.append(np.array(pts))
    return out


def pole_run(ink, x, y, step):
    """How many rows a near-vertical stroke of outline ink runs from row y in direction step (+1 down, -1 up) within
    POLE_WIN px of x: each row re-centers on the nearest ink by at most one px, and the whole run may drift at most
    DRIFT of its length (a pole tilted up to ~30 deg is followed, an arrowhead's 45-deg edge is not); a gap of more than
    POLE_GAP rows ends it (the ground, the cream page under a fly)."""
    H, W = ink.shape; n = gap = 0; x0 = x
    while 0 <= y < H and gap <= POLE_GAP:
        lo = max(0, x - POLE_WIN); xs = np.nonzero(ink[y, lo:min(W, x + POLE_WIN + 1)])[0]
        if len(xs):
            nx = lo + int(xs[np.argmin(np.abs(xs + lo - x))]); x += int(np.sign(nx - x)); n += gap + 1; gap = 0
            if abs(x - x0) > DRIFT * n + 4:
                break
        else:
            gap += 1
        y += step
    return n


def probe(path, debug=False):
    """(left mass, right mass, verdict, pole x[, the chosen fill's (below, above) runs and fullness at its ends, its body box])."""
    a = np.asarray(Image.open(path).convert("RGBA")).astype(np.int16)
    ink = (a[..., 3] >= 128) & (a[..., :3].max(axis=2) < INK)
    best = None; gold = gold_mask(a)
    for P in components((a[..., 3] >= 128) & ~ink):   # the fills between the outlines; a cloth's gloss and folds stay one fill
        if gold[P[:, 0], P[:, 1]].mean() < GOLD_SHARE:
            continue
        y0, x0 = P.min(axis=0); y1, x1 = P.max(axis=0)
        m = np.zeros((y1 - y0 + 1, x1 - x0 + 1), bool); m[P[:, 0] - y0, P[:, 1] - x0] = True
        wid = m.sum(axis=1); rows = np.nonzero(wid >= .5 * wid.max())[0]   # the body rows: a finial and a gold pole's foot stay out
        by0, by1 = y0 + rows[0], y0 + rows[-1]; h = by1 - by0 + 1
        cs = np.nonzero(m[rows].any(axis=0))[0]; hl, hr = x0 + cs[0], x0 + cs[-1]
        ext = [(pole_run(ink, xe, by1 + 1, 1), pole_run(ink, xe, by0 - 1, -1)) for xe in (hl, hr)]   # (below, above) at each end
        cnt = m[rows].sum(axis=0)[cs[0]:cs[-1] + 1]; k = max(2, int(EDGE * len(cnt)))   # the fill's height column by column
        full = (cnt[:k].mean() / cnt.max(), cnt[-k:].mean() / cnt.max())
        pole = [d >= BELOW * h and FINIAL <= u and d >= ABOVE * u and f >= HOIST_FULL for (d, u), f in zip(ext, full)]
        hoist = "L" if pole[0] and not pole[1] else "R" if pole[1] and not pole[0] else None
        cand = (hoist is not None, len(P), hoist, P, hl, hr, (ext, tuple(round(f, 2) for f in full)), (by0, hl, by1, hr))
        if best is None or cand[:2] > best[:2]:
            best = cand
    if best is None or best[2] is None:
        return (0, 0, "UNDECIDED", None) + (((best[6], best[7]) if best else None,) if debug else ())
    _, _, hoist, P, hl, hr, shares, box = best
    pole = hl if hoist == "L" else hr
    left, right = int((P[:, 1] < pole).sum()), int((P[:, 1] > pole).sum())
    v = "RIGHT" if right > left else "LEFT"
    return (left, right, v, pole) + (((shares, box),) if debug else ())


def main():
    a = sys.argv[1:]; hide = None
    if a[:1] == ["--hide"]:
        hide = Path(a[1]); a = a[2:]
    if not a:
        sys.exit(__doc__)
    rows = []
    for spec in a:
        pre, folder = spec.split("=", 1)
        for d in sorted(Path(folder).iterdir()):
            if not (d.is_dir() and len(d.name) == 14 and d.name.isdigit()):
                continue
            for act in ACTS:
                if (d / f"{act}.png").exists():
                    rows.append((f"{pre}-{d.name[8:]}", act) + probe(d / f"{act}.png"))
    print("%-10s %-22s %6s %6s  %s" % ("sheet", "act", "left", "right", "verdict"))
    for t, act, l, r, v, x in rows:
        print("%-10s %-22s %6d %6d  %s" % (t, act, l, r, v))
    tot = {v: sum(1 for row in rows if row[4] == v) for v in ("RIGHT", "LEFT", "UNDECIDED")}
    print("tiles %d: right %d, left %d, undecided %d" % (len(rows), tot["RIGHT"], tot["LEFT"], tot["UNDECIDED"]))
    if hide:
        h = json.loads(hide.read_text()) if hide.exists() else {"tile": {}, "front": {}}
        for t, act, l, r, v, x in rows:
            if v == "LEFT" and act not in h["tile"].setdefault(t, []):
                h["tile"][t].append(act)
        hide.write_text(json.dumps(h, indent=1) + "\n")
        print("wrote", hide)


if __name__ == "__main__":
    main()
