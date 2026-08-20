#!/usr/bin/env python3
"""Extract a page-flip -> measure map from a one-system-per-page score video.

Purpose: build the score-video `sheet.map` the GUI's score-video act reads.
Pipeline: frame-diff FLIP DETECTION -> per-page BARLINE COUNTING -> printed
bar-number OCR CROSS-CHECK -> REFUSE-OR-EMIT. Any cross-check failure refuses
whole; there is no partial map.

Usage:
    extract_sheet_map.py <video> <workdir> [--window START END|eof]
                                           [--expect-final N]

The map is written to <workdir>/sheet.map; copy it to the piece's
<project>/sheet/sheet.map, which is where the GUI looks for it.

--window runs on a time slice of a full (untrimmed) video; END may be the
literal "eof". Map timestamps stay window-relative and the window is recorded
in the map header, so the map can be recreated from the stable full video
without a trimmed copy.

MAP FORMAT. Header lines (window mode only, since only a window has a source
offset to record):

    # src <basename of the video>
    # window <start seconds> <end seconds|eof>

then data lines, one per anchor:

    <seconds>|<measure>

Seconds are relative to the WINDOW START (not the video start). Anchors are
the FIRST-PASS page starts and are MONOTONIC in both fields: a page the
performance re-shows (a repeat, a da capo, a volta re-crop) inherits its
original's start measure and emits no anchor. Measure numbers are CONTINUOUS
across the whole window even where the printed score restarts its numbering at
a new section — the printed restart is solved for and folded into a single
continuous count.
"""
# DEPENDENCIES (all standalone — none of this touches the product binaries;
# the tool has no link path from the GUI or CLI and is not built by any
# CMakeLists): python3 with numpy, scipy and Pillow; `tesseract`, `ffmpeg` and
# `ffprobe` on PATH.
#
# VALIDATED INVOCATIONS. Every constant below is measured, not guessed, and
# the whole chain is validated end to end against the four K.550 movements of
# the architect's rip (kern ground truth 299 / 123 / 84 / 308 bars):
#
#   extract_sheet_map.py <rip> <workdir> --window 0.0      359.008  --expect-final 299
#   extract_sheet_map.py <rip> <workdir> --window 362.667  816.0    --expect-final 123
#   extract_sheet_map.py <rip> <workdir> --window 821.333  1093.333 --expect-final 84
#   extract_sheet_map.py <rip> <workdir> --window 1098.667 eof      --expect-final 308

import os
import subprocess
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

FLIP_THR = 5.0        # mean abs frame diff (0-255 scale); slideshow noise is ~0
MIN_PAGE_S = 2.0      # spans shorter than this are boundary bleed -> dropped
MIN_BARLINE_RUN = 260 # px vertical dark run to count as a barline (4K frames)
BINARIZE = 170        # gray threshold for ink
CLOSE_GAP = 6         # heal vertical scan breaks up to this many px
MERGE_GAP = 60        # px: merge barline column clusters (double bars, repeats)
DIFF_W, DIFF_H = 480, 270

def scan_diffs(video, wstart=0.0, wdur=None):
    cmd = ["ffmpeg", "-v", "error"]
    if wstart:
        cmd += ["-ss", f"{wstart:.3f}"]
    if wdur is not None:
        cmd += ["-t", f"{wdur:.3f}"]
    cmd += ["-i", video, "-vf", f"scale={DIFF_W}:{DIFF_H}",
            "-pix_fmt", "gray", "-f", "rawvideo", "-"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    nbytes = DIFF_W * DIFF_H
    prev, diffs, n = None, [], 0
    while True:
        buf = proc.stdout.read(nbytes)
        if len(buf) < nbytes:
            break
        cur = np.frombuffer(buf, dtype=np.uint8).astype(np.int16)
        if prev is not None:
            diffs.append(float(np.abs(cur - prev).mean()))
        prev = cur; n += 1
    proc.wait()
    return np.array(diffs, dtype=np.float32), n

def video_fps(video):
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0",
                          "-show_entries", "stream=avg_frame_rate", "-of", "csv=p=0", video],
                         capture_output=True, text=True).stdout.strip()
    num, den = out.split("/")
    return float(num) / float(den)

def grab_frame(video, t, out):
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", f"{t:.3f}", "-i", video,
                    "-frames:v", "1", out], check=True)

def deskew(gray):
    """Straighten a slightly crooked scan. Angle from maximizing the
    sharpness of the staff-line row profile at quarter resolution."""
    small = (gray[::4, ::4] < BINARIZE).astype(np.float32)
    best_a, best_s = 0.0, -1.0
    for a in np.arange(-1.0, 1.001, 0.05):
        rot = ndimage.rotate(small, a, reshape=False, order=1)
        prof = rot.sum(axis=1)
        s = float(((prof[1:] - prof[:-1]) ** 2).sum())
        if s > best_s:
            best_s, best_a = s, float(a)
    if abs(best_a) < 0.049:
        return gray, 0.0
    out = ndimage.rotate(gray, best_a, reshape=False, order=1, mode="constant", cval=255)
    return out.astype(np.uint8), best_a

def column_max_run(dark):
    h, w = dark.shape
    best = np.zeros(w, np.int32); cur = np.zeros(w, np.int32)
    d = dark.astype(np.int32)
    for y in range(h):
        cur = (cur + d[y]) * d[y]
        np.maximum(best, cur, out=best)
    return best

def barline_events(dark):
    """Barline x-centers. A thick cluster (repeat sign) near the system head
    is a START-REPEAT: it closes no numbered bar, and a trailing event within
    200px of it closes only the unnumbered upbeat partial that follows a
    mid-bar repeat — both are dropped."""
    runs = column_max_run(dark)
    cols = np.where(runs >= MIN_BARLINE_RUN)[0]
    if len(cols) == 0:
        return []
    clusters, start, prev = [], cols[0], cols[0]
    for c in cols[1:]:
        if c - prev > MERGE_GAP:
            clusters.append((start, prev)); start = c
        prev = c
    clusters.append((start, prev))
    events = [(s + e) // 2 for s, e in clusters]
    widths = [e - s for s, e in clusters]
    if len(events) >= 2 and widths[1] >= 15 and events[1] - events[0] < 400:
        drop = {1}
        if len(events) >= 3 and events[2] - events[1] < 200:
            drop.add(2)
        events = [e for i, e in enumerate(events) if i not in drop]
    return events

def ocr_reads(gray, dark, x0, workdir):
    """All variant OCR reads of the printed top-left bar number, as a
    string -> hit-count dict. Empty dict = no printed number found."""
    ry0, ry1, rx0, rx1 = 10, 210, x0 + 35, x0 + 285
    reg = dark[ry0:ry1, rx0:rx1]
    lab, _ = ndimage.label(reg)
    digs = []
    for sl in ndimage.find_objects(lab):
        yh = sl[0].stop - sl[0].start; xw = sl[1].stop - sl[1].start
        if 28 <= yh <= 50 and 10 <= xw <= 150 and ry0 + sl[0].stop <= 205:
            digs.append(sl)
    if not digs:
        return {}
    digs.sort(key=lambda s: s[0].start)
    clusters = [[digs[0]]]
    for s in digs[1:]:
        if s[0].start - clusters[-1][-1][0].start > 25:
            clusters.append([s])
        else:
            clusters[-1].append(s)
    cl = clusters[-1]  # lowest cluster: the bar number (page number sits above)
    uy0 = min(s[0].start for s in cl); uy1 = max(s[0].stop for s in cl)
    ux0 = min(s[1].start for s in cl); ux1 = max(s[1].stop for s in cl)
    reads = {}
    tmp = os.path.join(workdir, "_ocr.png")
    for dl, dr in ((8, 12), (58, 12), (8, 80), (58, 80)):
        sub = gray[max(0, ry0 + uy0 - 8):ry0 + uy1 + 8,
                   max(0, rx0 + ux0 - dl):rx0 + ux1 + dr]
        for scale in (3, 4):
            im = Image.fromarray(sub).resize((sub.shape[1] * scale, sub.shape[0] * scale), Image.LANCZOS)
            for binar in (False, True):
                arr = np.asarray(im)
                if binar:
                    arr = np.where(arr < 160, 0, 255).astype(np.uint8)
                Image.fromarray(arr).save(tmp)
                for psm in ("7", "8"):
                    o = subprocess.run(["tesseract", tmp, "-", "--psm", psm,
                                        "-c", "tessedit_char_whitelist=0123456789"],
                                       capture_output=True, text=True)
                    s = o.stdout.strip().replace(" ", "")
                    if s:
                        reads[s] = reads.get(s, 0) + 1
    return reads

def main():
    video, workdir = sys.argv[1], sys.argv[2]
    expect_final = None
    if "--expect-final" in sys.argv:
        expect_final = int(sys.argv[sys.argv.index("--expect-final") + 1])
    wstart, wend_arg, wdur = 0.0, None, None
    if "--window" in sys.argv:
        k = sys.argv.index("--window")
        wstart = float(sys.argv[k + 1])
        wend_arg = sys.argv[k + 2]
        if wend_arg != "eof":
            wdur = float(wend_arg) - wstart
    os.makedirs(workdir, exist_ok=True)
    fps = video_fps(video)
    diffs_npy = os.path.join(workdir, "diffs.npy")
    if os.path.exists(diffs_npy):
        d = np.load(diffs_npy); nframes = len(d) + 1
    else:
        d, nframes = scan_diffs(video, wstart, wdur)
        np.save(diffs_npy, d)
    duration = nframes / fps
    flip_ts = [(i + 1) / fps for i in np.where(d > FLIP_THR)[0]]
    edges = [0.0] + flip_ts + [duration]
    spans = [(edges[i], edges[i + 1]) for i in range(len(edges) - 1)]
    kept = [s for s in spans if s[1] - s[0] >= MIN_PAGE_S]
    dropped = [s for s in spans if s[1] - s[0] < MIN_PAGE_S]
    print(f"fps={fps:.3f} duration={duration:.3f}s flips={len(flip_ts)} "
          f"pages={len(kept)} dropped_bleed={[(round(a,3), round(b,3)) for a, b in dropped]}")

    # pass 1: frames + bar counts
    counts = []
    geo = []
    for i, (a, b) in enumerate(kept):
        png = os.path.join(workdir, f"page{i:03d}.png")
        if not os.path.exists(png):
            grab_frame(video, wstart + a + min(1.0, (b - a) / 2), png)
        gray = np.asarray(Image.open(png).convert("L"))
        gray, angle = deskew(gray)
        dark = gray < 128
        healed = ndimage.binary_closing(gray < BINARIZE, structure=np.ones((CLOSE_GAP + 1, 1), bool))
        ev = barline_events(healed)
        if len(ev) < 2:
            print(f"REFUSE: page {i} has {len(ev)} barline events"); return 1
        counts.append(len(ev) - 1)
        geo.append((gray, dark, ev[0]))
        print(f"page {i:3d} span {a:8.3f}-{b:8.3f} bars={len(ev)-1} skew={angle:+.2f}")

    # pass 2: pickup hypothesis + OCR cross-check
    # no pickup: page0 covers bars 1..c0 -> page1 prints c0+1
    # pickup:    page0 covers bar0 + bars 1..c0-1 -> page1 prints c0
    # A page whose printed number cannot be OCR-confirmed may be BRIDGED
    # (max one in a row): the next page must then confirm the accumulated
    # sum, which still pins every bar count. Bridged pages are reported.
    # Replay detection: a performance that takes a repeat (or a da capo)
    # re-shows earlier pages verbatim. A replayed page inherits the
    # original's start measure, skips OCR, and leaves the continuation
    # counter untouched.
    quarter = [g[::4, ::4].astype(np.int16) for g, _, _ in geo]
    origin = [None] * len(kept)
    for i in range(1, len(kept)):
        for j in range(i):
            if origin[j] is None and counts[j] == counts[i] and \
               float(np.abs(quarter[i] - quarter[j]).mean()) < 2.0:
                origin[i] = j
                break
    replays = [(i, j) for i, j in enumerate(origin) if j is not None]
    if replays:
        print(f"replayed pages: {replays}")

    # One OCR sweep per new page; a value is CONFIRMED on a page iff it was
    # read by >= 2 independent variants.
    reads = {}
    for i in range(1, len(kept)):
        if origin[i] is None:
            g, dk, x0 = geo[i]
            reads[i] = ocr_reads(g, dk, x0, workdir)

    def confirmed(i, value):
        return reads[i].get(str(value), 0) >= 2

    n = len(kept)
    notes = []

    # Chain solver over CONTINUOUS numbering. Section-local numbering is
    # continuous minus the section base (print restarts at each section).
    # Backtracking handles the ambiguous no-read pages (unreadable number
    # -> BRIDGE, vs unnumbered section-first page -> RESTART).
    def solve(i, base, next_local, pend, bridge_open, max_local, starts, trail):
        # pend: (first_page_count, base) when a section-first page still
        # awaits its pickup resolution; None otherwise.
        if i == n:
            return (starts, trail) if not pend or True else None
        if origin[i] is not None:
            j = origin[i]
            if starts[j] is None:
                return None
            return solve(i + 1, base, next_local, pend, False, max_local,
                         starts + [starts[j]], trail)
        rd = reads[i]
        c = counts[i]
        # candidate expected section-local starts, in preference order
        if pend is not None:
            cands = [(pend[0] + 1, "no-pickup"), (pend[0], "pickup")]
        elif next_local is not None:
            cands = [(next_local, "chain")]
        else:
            cands = []
        for e, why in cands:
            if confirmed(i, e):
                nm = e if max_local is None else max(max_local, e)
                r = solve(i + 1, base, e + c, None, False, nm,
                          starts + [base + e], trail + [(i, e, why)])
                if r: return r
        # volta re-crop: the page re-shows the last reached region
        if max_local is not None and confirmed(i, max_local):
            r = solve_reseed(i, base, max_local, starts, trail)
            if r: return r
        # BRIDGE: number unreadable (or read too weakly); assume the chain
        # value, the next new page must then confirm the accumulated sum
        if pend is None and next_local is not None and not bridge_open:
            nm = next_local if max_local is None else max(max_local, next_local)
            r = solve(i + 1, base, next_local + c, None, True, nm,
                      starts + [base + next_local], trail + [(i, next_local, "bridged")])
            if r: return r
        # RESTART: a section-first page opens a new printed-numbering
        # section (tried last; the final-bar check gates misfires)
        if pend is None and next_local is not None:
            nb = base + next_local - 1
            r = solve(i + 1, nb, None, (c, nb), False, None,
                      starts + [nb + 1], trail + [(i, 1, "section-restart")])
            if r: return r
        return None

    def solve_reseed(i, base, vmax, starts, trail):
        # page i re-shows from vmax; successor's start lies in (vmax, vmax+c_i]
        c = counts[i]
        st = starts + [base + vmax]
        tr = trail + [(i, vmax, "volta-recrop")]
        j = i + 1
        if j == n:
            return (st, tr)
        if origin[j] is not None:
            return None  # replay directly after a re-crop: ambiguous, refuse
        hits = [e for e in range(vmax + 1, vmax + c + 1) if confirmed(j, e)]
        if len(hits) == 1:
            e = hits[0]
            return solve(j + 1, base, e + counts[j], None, False, e,
                         st + [base + e], tr + [(j, e, "reseed")])
        return None

    solution = solve(1, 0, None, (counts[0], 0), False, None, [1], [])
    if solution is None:
        print("REFUSE: no consistent chain interpretation; per-page reads:")
        for i in sorted(reads):
            print(f"   page {i}: {dict(sorted(reads[i].items(), key=lambda kv: -kv[1]))}")
        return 1
    starts, trail = solution
    pickup = any(why == "pickup" for _, _, why in trail)
    for i, e, why in trail:
        if why != "chain":
            print(f"page {i}: {why} (local start {e})")

    final_bar = max(starts[i] + counts[i] - 1 for i in range(n))
    print(f"CHECK PASSED: pickup={pickup} pages={n} final_bar={final_bar}")
    if expect_final is not None and final_bar != expect_final:
        print(f"REFUSE: final bar {final_bar} != expected {expect_final}"); return 1
    map_path = os.path.join(workdir, "sheet.map")
    with open(map_path, "w") as f:
        if wend_arg is not None:
            f.write(f"# src {os.path.basename(video)}\n")
            f.write(f"# window {wstart:.3f} {wend_arg}\n")
        f.write(f"0.000|{starts[0]}\n")
        top = starts[0]
        for i in range(1, n):
            if starts[i] > top:
                f.write(f"{kept[i][0]:.3f}|{starts[i]}\n")
                top = starts[i]
    print(f"wrote {map_path} (monotonic first-pass anchors)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
