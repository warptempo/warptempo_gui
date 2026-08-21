#!/usr/bin/env python3
"""Extract a page-flip -> measure map from a one-system-per-page score video.

Purpose: build the score-video `sheet.map` the GUI's score-video act reads.
Pipeline: frame-diff FLIP DETECTION -> per-page BARLINE COUNTING -> printed
bar-number OCR CROSS-CHECK -> REFUSE-OR-EMIT. Any cross-check failure refuses
whole; there is no partial map.

Usage:
    extract_sheet_map.py <video> <workdir> [--window START END|eof]
                                           [--min-page SECONDS]
                                           [--expect-final N]
                                           [--expect-sections N]

The map is written to <workdir>/sheet.map; copy it to the piece's
<project>/sheet/sheet.map, which is where the GUI looks for it.

--window runs on a time slice of a full (untrimmed) video. Either end may be
written as SS, MM:SS or HH:MM:SS (the seconds field integer or fractional), and
END may instead be the literal "eof". Map timestamps stay window-relative and
the window is recorded in the map header, so the map can be recreated from the
stable full video without a trimmed copy.

MAP FORMAT. Header lines (window mode only, since only a window has a source
offset to record):

    # src <basename of the video>
    # window <start seconds> <end seconds|eof>

The header spells NORMALIZED SECONDS, trailing-zero-free — `# window 820 1093`
for `--window 13:40 18:13` — while "eof" is kept literal. Then data lines, one
per anchor:

    <seconds>|<measure>

Seconds are relative to the WINDOW START (not the video start).

THE MEASURE IS THE PRINTED BAR NUMBER, exactly what the page shows (architect
2026-08-20, replacing a continuous count cross-referenced against the score).
A movement whose printed numbering RESTARTS mid-way — the K.550 menuetto's trio
— opens a SECTION, numbered 1 at the movement's start and +1 at each restart in
video order, and an anchor in section S >= 2 carries the qualifier:

    <seconds>|<local>        section 1
    <seconds>|<S>:<local>    section S >= 2

MONOTONICITY IS PER SECTION now, which is what the restart forces: time is
strictly increasing across the whole map, while the measure only has to
increase WITHIN a section. The drop rules are unchanged and simply apply per
section — a page the performance re-shows (a repeat, a da capo, a volta
re-crop) inherits its original's start and emits no anchor, so a da capo back
into section 1 adds nothing after section 2's anchors.
"""
# DEPENDENCIES (all standalone — none of this touches the product binaries;
# the tool has no link path from the GUI or CLI and is not built by any
# CMakeLists): python3 with numpy, scipy and Pillow; `tesseract`, `ffmpeg` and
# `ffprobe` on PATH.
#
# VALIDATED INVOCATIONS. Every constant below is measured, not guessed, and
# the whole chain is validated end to end against the four K.550 movements of
# the architect's rip:
#
#   extract_sheet_map.py <rip> <workdir> --window 0     5:59  --expect-final 299
#   extract_sheet_map.py <rip> <workdir> --window 6:00 13:37  --expect-final 123
#   extract_sheet_map.py <rip> <workdir> --window 13:40 18:13 --expect-final 42 \
#                                                             --expect-sections 2
#   extract_sheet_map.py <rip> <workdir> --window 18:15 eof   --expect-final 308
#
# THE WINDOWS ARE WHOLE SECONDS (2026-08-20) — 5:59 is 359, 13:40 is 820 — and
# they are deliberately loose at both ends: a window that opens a fraction early
# catches a transition flash, which the --min-page floor drops as bleed, so a
# hand-typed MM:SS boundary needs no frame accuracy at all.
#
# THE KERN CROSS-CHECK IS PER SECTION FOR MOVEMENT 3, and that is the printed
# rule working rather than an exception: the kern ground truth counts the
# menuetto and its trio as one continuous 84 bars, while the video PRINTS the
# trio restarting at 1, so section 2's final printed bar is 42 == 84 - 42. The
# other three movements print one continuous section each and their finals are
# the kern totals unchanged (299 / 123 / 308).

import math
import os
import subprocess
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

FLIP_THR = 5.0        # mean abs frame diff (0-255 scale); slideshow noise is ~0
# THE PAGE FLOOR, in seconds: a span shorter than this is boundary bleed — a
# transition flash at a window edge, or a frame of crossfade between pages — and
# is dropped rather than counted as a page. IT IS THE DEFAULT FOR --min-page,
# not a fixed constant, because the right floor is a property of the rip.
#
# THE MARGINS ARE MEASURED ON THE K.550 RIP AND BOTH ARE WIDE. Observed bleed
# tops out at 0.2s (the movement-2 transition flash), while the shortest REAL
# page is 5.47s (movement 4) and the shortest real VOLTA PARTIAL — the tightest
# thing that must survive — is 6.03s (movement 3). So 2.0 sits 10x above the
# largest bleed and 2.7x under the shortest page worth keeping. 5.0 WAS
# CONSIDERED AND REJECTED on exactly this data: it would still clear every
# observed bleed, but it leaves only a 1.09x margin under that 5.47s page, and a
# floor that close to a real page is one slow flip away from eating one.
MIN_PAGE_S = 2.0
# THE SECTION CEILING, and it is the GRAMMAR'S rather than this tool's: the
# marker measure's qualifier brackets at 99 (kMeasureMaxSection,
# src/parser/marker_measure.h), and load_score_video_map refuses a map anchor
# past it. THE PRODUCER MUST NOT SPELL WHAT THE CONSUMER MUST REJECT — a map
# this tool published and the GUI then refused whole would be a silent dead
# jump — so a restart that would open section 100 is refused during the solve,
# where it is still a backtrack point, rather than at the emit where it could
# only be a crash. Keep this equal to kMeasureMaxSection.
MAX_SECTION = 99
MIN_BARLINE_RUN = 260 # px vertical dark run to count as a barline (4K frames)
BINARIZE = 170        # gray threshold for ink
CLOSE_GAP = 6         # heal vertical scan breaks up to this many px
MERGE_GAP = 60        # px: merge barline column clusters (double bars, repeats)
DIFF_W, DIFF_H = 480, 270

def parse_window_time(text):
    """`SS`, `MM:SS` or `HH:MM:SS` -> seconds. The SECONDS field may be
    fractional; the minute and hour fields are whole. Returns None for
    anything else, which the caller refuses.

    IT EXISTS BECAUSE THE WINDOWS ARE TYPED BY HAND off a video player's own
    readout, and that readout is MM:SS. Converting in the head is the one step
    of this tool's use that was pure clerical error waiting to happen.

    THE SUBFIELDS ARE RANGE-CHECKED, so the advertised grammar is the accepted
    one: in a clock spelling every field BELOW the highest is a real clock
    field and must be under 60, which makes `1:60` and `1:60:00` refusals
    rather than second spellings of 120 and 3600. A BARE SECONDS-ONLY value
    stays unbounded — `--window 0 900` is seconds, not a malformed clock, and
    a rip is allowed to be long.

    NON-FINITE IS REFUSED: `float()` happily reads `nan` and `inf`, and either
    one poisons every comparison downstream — an infinite floor keeps no page
    at all, a NaN floor makes the keep and drop tests BOTH false and leaves an
    empty page set for the solver to index."""
    parts = text.split(":")
    if not 1 <= len(parts) <= 3:
        return None
    try:
        seconds = float(parts[-1])
    except ValueError:
        return None
    if not math.isfinite(seconds) or seconds < 0.0:
        return None
    # The seconds field is a clock field too once anything sits above it.
    if len(parts) > 1 and seconds >= 60.0:
        return None
    for i, field in enumerate(reversed(parts[:-1]), start=1):
        if not field.isdigit():
            return None
        value = int(field)
        # Every field below the highest is bounded; the highest is not (a
        # window may legitimately open at 100 minutes into a long rip).
        if i < len(parts) - 1 and value >= 60:
            return None
        seconds += value * (60 ** i)
    return seconds

def spell_seconds(value):
    """The header's spelling: normalized seconds, trailing-zero-free, so a
    window typed `13:40` is recorded as `820` and one typed `359.008` keeps its
    fraction. Three decimals is the whole precision this tool has — it is what
    the ffmpeg seeks are spelled at — so nothing is lost by stopping there."""
    text = f"{value:.3f}".rstrip("0").rstrip(".")
    return text if text else "0"

def scan_diffs(video, wstart=0.0, wdur=None):
    """Per-frame mean absolute difference over the window. Returns
    (diffs, nframes, ok). ok is False for a decode that did not finish
    cleanly, and the caller must then refuse: a HALF-DECODED stream looks
    exactly like a shorter video, so its tail pages simply vanish and the
    remaining prefix can still pass every self-consistent check below
    (the bar counts and the OCR chain read the same truncated set). A
    partial trailing frame is the same fault: `read` on a pipe returns
    short only at EOF, so a non-empty short read is a stream cut mid-frame."""
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
    truncated = False
    while True:
        buf = proc.stdout.read(nbytes)
        if len(buf) < nbytes:
            truncated = len(buf) > 0
            break
        cur = np.frombuffer(buf, dtype=np.uint8).astype(np.int16)
        if prev is not None:
            diffs.append(float(np.abs(cur - prev).mean()))
        prev = cur; n += 1
    status = proc.wait()
    ok = status == 0 and not truncated and n > 0
    if not ok:
        print(f"decode failed: ffmpeg exit {status}, {n} frames"
              f"{', truncated final frame' if truncated else ''}")
    return np.array(diffs, dtype=np.float32), n, ok

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

class OptionError(Exception):
    """One bad option, carrying the line the user should read. EVERY option
    fault lands here and comes out as the tool's own REFUSE protocol — a
    message and exit 1 — rather than as a traceback: a Python stack trace over
    a mistyped flag is not a refusal, it is a crash that happens to stop the
    run, and it says nothing about what to type instead."""

def option_values(flag, count):
    """The `count` arguments after `flag`, or an OptionError naming what is
    missing. ARITY IS CHECKED BEFORE CONVERSION so a truncated command line
    (`--window 6:00` with no end) refuses by name instead of raising
    IndexError somewhere later."""
    k = sys.argv.index(flag)
    if k + count >= len(sys.argv):
        raise OptionError(f"{flag} takes {count} argument"
                          f"{'' if count == 1 else 's'}")
    return sys.argv[k + 1:k + 1 + count]

def option_int(flag):
    (raw,) = option_values(flag, 1)
    try:
        return int(raw)
    except ValueError:
        raise OptionError(f"{flag} takes a whole number, not {raw!r}") from None

def read_options():
    """Every command-line decision, made BEFORE any scan or cache work touches
    the disk: a run that is going to refuse must refuse having done nothing.
    Returns the parsed settings; raises OptionError with the line to print."""
    expect_final = option_int("--expect-final") if "--expect-final" in sys.argv \
        else None
    expect_sections = option_int("--expect-sections") \
        if "--expect-sections" in sys.argv else None

    min_page_s = MIN_PAGE_S
    if "--min-page" in sys.argv:
        (raw,) = option_values("--min-page", 1)
        try:
            min_page_s = float(raw)
        except ValueError:
            raise OptionError(f"--min-page takes seconds, not {raw!r}") from None
        # FINITE AND POSITIVE, both. `float()` reads `nan` and `inf` happily and
        # either one silently empties the page set — an infinite floor keeps no
        # span, and a NaN floor makes the keep and the drop tests BOTH false, so
        # every page vanishes and the solver indexes an empty list.
        if not math.isfinite(min_page_s) or min_page_s <= 0.0:
            raise OptionError("--min-page must be a positive number of seconds")

    wstart, wend_arg, wdur = 0.0, None, None
    if "--window" in sys.argv:
        raw_start, raw_end = option_values("--window", 2)
        wstart = parse_window_time(raw_start)
        if wstart is None:
            raise OptionError(f"unreadable window start {raw_start!r} "
                              "(SS, MM:SS or HH:MM:SS)")
        if raw_end == "eof":
            # "eof" is kept LITERAL all the way to the header: the end is not a
            # number this tool knows, and writing one it guessed would be worse
            # than saying so.
            wend_arg = "eof"
        else:
            wend = parse_window_time(raw_end)
            if wend is None:
                raise OptionError(f"unreadable window end {raw_end!r} "
                                  "(SS, MM:SS, HH:MM:SS or eof)")
            if wend <= wstart:
                raise OptionError("window end is not after its start")
            # The HEADER SPELLS THE NORMALIZED VALUE, not what was typed, so a
            # window is one number in the map however it was written at the
            # shell — and two spellings of one window share a cache stamp.
            wend_arg = spell_seconds(wend)
            wdur = wend - wstart
    return expect_final, expect_sections, min_page_s, wstart, wend_arg, wdur

def main():
    if len(sys.argv) < 3:
        print("REFUSE: usage: extract_sheet_map.py <video> <workdir> "
              "[--window START END|eof] [--min-page SECONDS] "
              "[--expect-final N] [--expect-sections N]")
        return 1
    video, workdir = sys.argv[1], sys.argv[2]
    try:
        (expect_final, expect_sections, min_page_s,
         wstart, wend_arg, wdur) = read_options()
    except OptionError as err:
        print(f"REFUSE: {err}")
        return 1
    os.makedirs(workdir, exist_ok=True)
    fps = video_fps(video)
    # The frame-diff scan is cached, and the cache is STAMPED with the exact
    # input it was taken from: a workdir reused across two different slices —
    # or against a video that has since been replaced in place — would
    # otherwise emit a map for the wrong music, silently. The stamp is a
    # RESOLVED IDENTITY plus a CHANGE WITNESS: the realpath (so two files that
    # share a basename in different folders are two inputs), the device and
    # inode (so a different file at the same path is a different input), and
    # the size and mtime (so the same path re-encoded in place is a different
    # input). Deliberately NOT a content digest: these are 400 MB rips and a
    # digest would cost more than the scan it guards.
    #
    # A stamp that does not match the current invocation rescans AND drops the
    # cached page frames with it, since those are keyed by page index alone and
    # are exactly as input-specific as the diffs are. A FAILED SCAN ACQUIRES NO
    # STAMP: the cache is written only after the decode is known clean, so a
    # refusal cannot leave a half-stream behind for the next run to trust.
    #
    # --min-page IS IN THE STAMP THOUGH IT CANNOT CHANGE A DIFF, and that is the
    # page cache's doing rather than the scan's: the floor decides which spans
    # survive as pages, so a different floor RENUMBERS them, and page007.png
    # would then be a different page than the one on disk. Rescanning the diffs
    # to re-cut the pages is wasted work and is accepted whole — one stamp, one
    # answer, no second cache to keep in step.
    #
    # IT IS STAMPED LOSSLESSLY (`repr`, 2026-08-20) and not at three decimals,
    # because the value is USED at full precision: two floors that round to the
    # same three-decimal text can still fall on opposite sides of one
    # frame-quantized span, keeping a page under one and dropping it under the
    # other. A stamp that could not tell them apart would hand the second run
    # the first run's page numbering — the exact staleness this term exists to
    # stop. The number that decides is the number that is stamped.
    st = os.stat(video)
    diffs_npy = os.path.join(workdir, "diffs.npy")
    diffs_meta = os.path.join(workdir, "diffs.meta")
    stamp = (f"{os.path.realpath(video)}|{st.st_dev}|{st.st_ino}|"
             f"{st.st_size}|{st.st_mtime_ns}|{wstart:.3f}|{wend_arg}|"
             f"{min_page_s!r}\n")
    cached = False
    if os.path.exists(diffs_npy) and os.path.exists(diffs_meta):
        with open(diffs_meta) as f:
            cached = f.read() == stamp
    if cached:
        d = np.load(diffs_npy); nframes = len(d) + 1
    else:
        for stale in sorted(p for p in os.listdir(workdir)
                            if p.startswith("page") and p.endswith(".png")):
            os.remove(os.path.join(workdir, stale))
        if os.path.exists(diffs_meta):
            os.remove(diffs_meta)
        d, nframes, ok = scan_diffs(video, wstart, wdur)
        if not ok:
            print("REFUSE: the frame scan did not complete"); return 1
        np.save(diffs_npy, d)
        with open(diffs_meta, "w") as f:
            f.write(stamp)
    duration = nframes / fps
    flip_ts = [(i + 1) / fps for i in np.where(d > FLIP_THR)[0]]
    edges = [0.0] + flip_ts + [duration]
    spans = [(edges[i], edges[i + 1]) for i in range(len(edges) - 1)]
    kept = [s for s in spans if s[1] - s[0] >= min_page_s]
    dropped = [s for s in spans if s[1] - s[0] < min_page_s]
    print(f"fps={fps:.3f} duration={duration:.3f}s flips={len(flip_ts)} "
          f"min_page={min_page_s:g}s pages={len(kept)} "
          f"dropped_bleed={[(round(a,3), round(b,3)) for a, b in dropped]}")

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

    # Chain solver over CONTINUOUS numbering, EMITTING PRINTED NUMBERS. The
    # continuous count is the solver's own arithmetic — it is what makes a
    # chain, a bridge and a restart comparable — while what the map records is
    # the LOCAL (printed) start plus the SECTION it belongs to, which the walk
    # already knows: local = continuous - base, and the section index steps at
    # each accepted restart. Both ride alongside `starts` as parallel
    # accumulators rather than being re-derived afterwards, because a
    # re-derivation would have to guess which base a page resolved under.
    # Backtracking handles the ambiguous no-read pages (unreadable number
    # -> BRIDGE, vs unnumbered section-first page -> RESTART).
    def solve(i, base, next_local, pend, owed, max_local, sect,
              starts, sects, locs, trail):
        # pend: (first_page_count, base) when a section-first page still
        # awaits its pickup resolution; None otherwise.
        # sect: the section index in force, 1 at the movement's start.
        #
        # owed: THE BRIDGE OBLIGATION, AND IT IS A VALUE RATHER THAN A FLAG
        # (2026-08-20). A bridged page assumed a number nobody could read, and
        # what discharges that assumption is not "some later page was legible"
        # but "the next new page confirmed EXACTLY the value the assumption
        # implies". So the obligation travels as `(base, local)` — the section
        # base it was taken under and the local start its continuation demands —
        # and only a confirmation of that pair closes it. A boolean could be
        # cleared by any confirmation at all, which is how a restart's pickup
        # read or a volta re-crop's successor could stand in for a
        # confirmation of something else entirely.
        if i == n:
            # RUNNING OUT OF PAGES CLOSES NO OBLIGATION. A standing `pend`
            # means a section-first page never had its pickup resolved, and a
            # standing `owed` means an unreadable page's assumed value was
            # never confirmed by a later one; either way the interpretation
            # rests on nothing but itself, which is exactly what this solver
            # refuses everywhere else. Both are backtrack points, so a chain
            # that can close its obligations another way is still found.
            if pend is not None or owed is not None:
                return None
            return (starts, sects, locs, trail)
        if origin[i] is not None:
            j = origin[i]
            if starts[j] is None:
                return None
            # A REPLAY CONFIRMS NOTHING — it re-shows a page already read, and
            # skips OCR by construction — so it carries the bridge obligation
            # forward untouched rather than clearing it. `pend` rides through
            # for the same reason and always has.
            # The replayed page shows page j, so it takes page j's SECTION and
            # printed start along with its continuous one — a da capo re-shows
            # section 1's pages while the section counter stands still.
            return solve(i + 1, base, next_local, pend, owed, max_local,
                         sect, starts + [starts[j]], sects + [sects[j]],
                         locs + [locs[j]], trail)
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
                # THE DISCHARGE IS AN EXACT MATCH, base and value both: this
                # page closes the obligation only if what it confirmed IS the
                # bridged assumption's continuation, in the very section that
                # assumption was made in. Anything else leaves it standing, so
                # the chain runs on still owing a confirmation and must find one
                # or refuse at EOF.
                nxt = None if (owed is None or (base, e) == owed) else owed
                nm = e if max_local is None else max(max_local, e)
                r = solve(i + 1, base, e + c, None, nxt, nm, sect,
                          starts + [base + e], sects + [sect], locs + [e],
                          trail + [(i, e, why)])
                if r: return r
        # VOLTA RE-CROP: the page re-shows the last reached region. IT IS NOT
        # TRIED WHILE AN OBLIGATION STANDS, and that is the same rule as the
        # restart's below: this path's own confirmations are of `vmax` and of a
        # reseed value in (vmax, vmax + c], neither of which is ever the
        # bridged continuation, so entering it could only carry the obligation
        # to an end that cannot close it.
        if owed is None and max_local is not None and confirmed(i, max_local):
            r = solve_reseed(i, base, max_local, owed, sect,
                             starts, sects, locs, trail)
            if r: return r
        # BRIDGE: number unreadable (or read too weakly); assume the chain
        # value, and OWE the next new page a confirmation of exactly what that
        # assumption implies — `next_local + c` under this same base.
        if pend is None and next_local is not None and owed is None:
            nm = next_local if max_local is None else max(max_local, next_local)
            r = solve(i + 1, base, next_local + c, None,
                      (base, next_local + c), nm, sect,
                      starts + [base + next_local], sects + [sect],
                      locs + [next_local], trail + [(i, next_local, "bridged")])
            if r: return r
        # RESTART: a section-first page opens a new printed-numbering
        # section (tried last; the final-bar check gates misfires). IT IS
        # REFUSED WHILE AN OBLIGATION STANDS: a new section restarts the
        # numbering, so no page after it can ever confirm a value owed under the
        # OLD base — carrying the obligation across would guarantee a refusal at
        # EOF, and clearing it would let a fresh numbering vouch for a bar it
        # never printed.
        #
        # THE SECTION CEILING IS THE GRAMMAR'S (kMeasureMaxSection, 99): this is
        # the PRODUCER of a field a consumer must accept, so a restart that
        # would open section 100 is refused here rather than published as a map
        # `load_score_video_map` is obliged to reject. Ninety-nine printed
        # restarts in one movement is far past any real score; the bound exists
        # so the two ends of the format cannot disagree.
        if (pend is None and next_local is not None and owed is None
                and sect + 1 <= MAX_SECTION):
            nb = base + next_local - 1
            # THE SECTION INDEX STEPS HERE and nowhere else, so it counts
            # accepted restarts in VIDEO ORDER: this page prints 1 again, and
            # every page after it is section sect + 1 until the next restart.
            r = solve(i + 1, nb, None, (c, nb), owed, None, sect + 1,
                      starts + [nb + 1], sects + [sect + 1], locs + [1],
                      trail + [(i, 1, "section-restart")])
            if r: return r
        return None

    def solve_reseed(i, base, vmax, owed, sect, starts, sects, locs, trail):
        # page i re-shows from vmax; successor's start lies in (vmax, vmax+c_i]
        #
        # IT IS NEVER ENTERED WITH A BRIDGE OBLIGATION STANDING — the caller
        # gates on that — and the check is repeated here because the REASON is
        # local to this path: nothing it can confirm is the bridged
        # continuation, so a future caller that let one through would be
        # discharging an assumption with an unrelated number.
        if owed is not None:
            return None
        c = counts[i]
        st = starts + [base + vmax]
        sc = sects + [sect]
        lc = locs + [vmax]
        tr = trail + [(i, vmax, "volta-recrop")]
        j = i + 1
        if j == n:
            return (st, sc, lc, tr)
        if origin[j] is not None:
            return None  # replay directly after a re-crop: ambiguous, refuse
        hits = [e for e in range(vmax + 1, vmax + c + 1) if confirmed(j, e)]
        if len(hits) == 1:
            e = hits[0]
            return solve(j + 1, base, e + counts[j], None, None, e, sect,
                         st + [base + e], sc + [sect], lc + [e],
                         tr + [(j, e, "reseed")])
        return None

    solution = solve(1, 0, None, (counts[0], 0), None, None, 1,
                     [1], [1], [1], [])
    if solution is None:
        print("REFUSE: no consistent chain interpretation; per-page reads:")
        for i in sorted(reads):
            print(f"   page {i}: {dict(sorted(reads[i].items(), key=lambda kv: -kv[1]))}")
        return 1
    starts, sects, locs, trail = solution
    pickup = any(why == "pickup" for _, _, why in trail)
    for i, e, why in trail:
        if why != "chain":
            print(f"page {i}: {why} (local start {e})")

    # THE FINAL BAR IS THE LAST SECTION'S LAST PRINTED ONE (2026-08-20, with
    # the printed-number ruling): the check is against what the score SHOWS, so
    # a movement that restarts its numbering is checked on the restarted count.
    # THE LAST SECTION IS THE HIGHEST INDEX, not the last page's, and the
    # difference is a real case rather than pedantry: the index only ever steps
    # UP, but a da capo re-shows section 1's pages after section 2's, so the
    # final PAGE can belong to an earlier section than the one the movement
    # reached. What is being checked is how far the printing got.
    section_count = max(sects)
    finals = {}
    for i in range(n):
        end = locs[i] + counts[i] - 1
        finals[sects[i]] = max(finals.get(sects[i], 0), end)
    final_bar = finals[section_count]
    print(f"CHECK PASSED: pickup={pickup} pages={n} sections={section_count} "
          f"finals={{{', '.join(f'{s}: {finals[s]}' for s in sorted(finals))}}} "
          f"final_bar={final_bar}")
    if expect_final is not None and final_bar != expect_final:
        print(f"REFUSE: final bar {final_bar} != expected {expect_final}"); return 1
    if expect_sections is not None and section_count != expect_sections:
        print(f"REFUSE: {section_count} sections != expected "
              f"{expect_sections}"); return 1
    # PUBLISH ATOMICALLY, because "no partial map" is the whole refuse-or-emit
    # contract and a direct write can break it after every check has passed: a
    # full filesystem or an I/O error mid-write leaves a TRUNCATED map at the
    # canonical path, and a truncated map is syntactically valid — a prefix of
    # monotonic anchors — so the GUI would read it and jump confidently to the
    # wrong bar past the cut. The temporary lands in the SAME directory (a
    # rename is only atomic within one filesystem), is flushed and fsync'd
    # before it is named, and os.replace does the rest.
    #
    # THE ANCHOR SPELLING is the printed number for section 1 and `<S>:<local>`
    # for section 2 and up — the measure grammar's own qualifier, so a map line
    # and a marker's measure field read alike. Section 1 is spelled BARE and
    # never `1:`, which is the grammar's one canonical spelling for it.
    #
    # MONOTONIC PER SECTION, which is the printed rule's one structural
    # consequence: time still rises strictly across the whole map, but a
    # restart takes the measure back to 1, so the drop test is against the top
    # reached IN THAT SECTION. Every existing drop keeps working unchanged
    # under it — a replay, a volta re-crop and a second pass all re-show numbers
    # their own section already reached — and a da capo back into section 1 is
    # dropped by section 1's own top rather than accidentally by section 2's.
    def spell(section, local):
        return f"{local}" if section <= 1 else f"{section}:{local}"

    map_path = os.path.join(workdir, "sheet.map")
    tmp_path = map_path + ".tmp"
    with open(tmp_path, "w") as f:
        if wend_arg is not None:
            f.write(f"# src {os.path.basename(video)}\n")
            f.write(f"# window {spell_seconds(wstart)} {wend_arg}\n")
        f.write(f"0.000|{spell(sects[0], locs[0])}\n")
        tops = {sects[0]: locs[0]}
        for i in range(1, n):
            s = sects[i]
            if s in tops and locs[i] <= tops[s]:
                continue
            f.write(f"{kept[i][0]:.3f}|{spell(s, locs[i])}\n")
            tops[s] = locs[i]
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp_path, map_path)
    print(f"wrote {map_path} (monotonic first-pass anchors, "
          f"{section_count} section{'' if section_count == 1 else 's'})")
    return 0

if __name__ == "__main__":
    sys.exit(main())
