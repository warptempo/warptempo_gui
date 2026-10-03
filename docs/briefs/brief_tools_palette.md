# CODER BRIEF — `tools/palette/`: the mock-up renderer made permanent (architect 2026-10-02, "tools/palette works")

You are the CODER: Opus, high effort. Read `CLAUDE.md` first (you edit files, build nothing here — this is Python —,
run only read-only git, never stage or commit, never edit CLAUDE.md, never run `scripts/warptempo_sync`). The
planner commits. `tools/` holds standalone one-shot utilities with no link path from product targets (CLAUDE.md's
build section; `tools/CMakeLists.txt` is the two C++ sidecar tools' own build and is NOT touched: the palette tool
is Python and has no CMake).

## What moves, and from where

Source: `tmp/palette/mocktool/` (the tool: `common.py`, `render.py`, `extract.py`, `compare.py`, `icons95.py`,
`README.md`, `scene_*.json`, `waveform_*.json`, `glyphs/`, `themes/`, generated `fonts.conf` + `fccache/`, `out/`),
plus its two imports from `tmp/palette/`: `pngrw.py` and `derive.py`. Read the README in full first (the tool is
complete and current; every rule is in it). Destination: `tools/palette/`. Nothing under `tmp/` is deleted or
moved — copy, the planner archives tmp/ later.

- `render.py`, `extract.py`, `compare.py`, `common.py` → `tools/palette/`, their repo-root-relative paths rewritten
  to the new home (the tool must run from the repo root AND from `tools/palette/`: resolve everything from
  `__file__`, not the cwd). The usage lines at the heads of the files follow.
- `pngrw.py` → `tools/palette/pngrw.py`. PREFERRED: make `read_rgb` pure Python (zlib + the five PNG filter types,
  8-bit RGB/RGBA/grey, non-interlaced — every PNG the tool reads is a screencap or its own output) so the tool
  needs no ImageMagick; keep `magick` only if the pure reader would exceed ~100 lines or cannot reproduce
  `read_rgb`'s bytes exactly on `tmp/tablet_base_1002.png` (verify byte-for-byte against the magick road before
  switching, and say which road shipped).
- `derive.py`: fold ONLY what the tool imports (`srgb_to_p3`, `lin_mix`, `s2l`, `l2s` and the matrices they need)
  into `tools/palette/common.py` or a small `tools/palette/colour.py` — your call, one owner; the chart / HSL /
  contrast helpers stay behind in tmp/.
- THE iCCP CHUNK: `common.ICCP` currently reads the palm screenshot `tmp/from_tablet/palm_0354.png` at import.
  Extract the chunk's bytes once (`pngrw.chunk_from_png(..., b'iCCP')`, 299 bytes) into
  `tools/palette/display_p3.iccp` (the raw chunk data, binary) and read that file; the README states its
  provenance (a Samsung Gallery screenshot's Display-P3 profile chunk, 2026-10-02) in one line.
- `icons95.py` and the retro icon sets STAY OUT (the icon axis never ran; its art lives in `tmp/research/`, which
  does not ship). `render.py`'s `icons` option keeps only `"app"`; any other value is refused with one line naming
  that the retro sets were not shipped. Remove the dead imports/code paths that only served the sets
  (`icons_recolour`, `icons_disabled` "dim"/"emboss" if they only applied to set icons — check; the app glyph's
  disabled road stays).
- SCENES ship as derived data so a cloud session renders without a capture: `scene_1002.json`, `scene_1002a.json`,
  `scene_s4.json`, their `waveform_*.json` and `glyphs/{1002,1002a,s4}/` (~750 KB total). The captures
  (`tmp/tablet_base_*.png`) do NOT ship; the README says where the scenes came from and that `extract.py` re-derives
  one from a capture.
- THEMES: ship `tools/palette/themes/frozen.json` = `tmp/palette/set_v/mock_V3_head_11rows.json` renamed, its
  `name` "frozen" and its `description` rewritten as the record: the design the app paints since 2026-10-02 (the
  lineage J4 → K4 → L2/M1 → N2 → O2 → P6/T4/U4 → R3 → S4 → V3, one line) — THIS is the parity theme now (the
  app's own picture). Also ship `s4_const.json` renamed `pre_2026_10_02_constants.json` ONLY if its role as the
  old constants' geometry check still has a use once the app paints the frozen theme — I judge it does not: drop
  it, and drop the five mock themes (y1b_exact, j4_*). State what shipped.
- `out/`, `fonts.conf`, `fccache/`, `__pycache__/` are generated: add `tools/palette/out/`, `tools/palette/fonts.conf`,
  `tools/palette/fccache/` to `.gitignore` (the repo already ignores `__pycache__`? grep; add if not).
- `README.md` → `tools/palette/README.md`, rewritten for the new home: paths, the pure-Python/magick decision, the
  deps (python3, numpy, pycairo ≥ 1.29, the repo's `fonts/`; HarfBuzz through ctypes — name the shared library it
  loads and what happens when it is absent), the scenes' provenance, the one theme, the iCCP file, and the
  "What is approximated" section kept. Keep its measured-geometry tables (they are the record of the 1002 scene)
  but strip every mock-set letter from the prose except the lineage line; the sets' per-option measurement
  paragraphs ("Measured on 1002 with N2 …") keep their numbers but name the option values used instead of the set.
  The `--label` option and `label.py`'s mention: keep the option, drop the tmp/ reference.
- The tool's own doc-comments: same rule — current behaviour, no set letters, no tmp/ paths.

## Verification (you run these; they write only under tools/palette/out/ and tmp/)

1. From the repo root: `python3 tools/palette/render.py tools/palette/themes/frozen.json tmp/tools_check_root.png`
   and from `tools/palette/`: `python3 render.py themes/frozen.json ../../tmp/tools_check_sub.png`. Both must be
   byte-identical to `tmp/palette/set_v/mock_V3_head_11rows.png` (`cmp`). If the pure-Python PNG reader shipped,
   also `python3 tools/palette/compare.py tmp/tools_check_root.png tmp/palette/set_v/mock_V3_head_11rows.png`
   must report 0 differing pixels.
2. `python3 tools/palette/render.py tools/palette/themes/frozen.json tmp/tools_check_1002a.png --scene 1002a`
   must equal a render of the V3 theme by the OLD tool on scene 1002a (make that reference with
   `python3 tmp/palette/mocktool/render.py tmp/palette/set_v/mock_V3_head_11rows.json tmp/ref_1002a.png --scene 1002a`).
3. `python3 tools/palette/extract.py` on `tmp/tablet_base_1002.png` with the README's 1002 command line and
   `--out-dir tmp/tools_extract_check` must reproduce `tools/palette/scene_1002.json` and `waveform_1002.json`
   byte-identically (`cmp`), and the glyph PGMs likewise (diff -r).
4. `git status` shows only `tools/palette/**` and `.gitignore` as changes. No file under `tools/palette/` mentions
   `tmp/` except the README's provenance sentence(s) about the captures.

Report: the file list with sizes, the PNG-reader decision and its evidence, the three cmp results verbatim, what
was dropped from the theme schema and why, and anything left undone.
