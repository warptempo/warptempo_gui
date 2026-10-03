# tools/palette — the tablet screen rendered from a theme

Draws the whole 2304x1440 tablet screen (Galaxy Tab S10 FE, gui_scale 200 %: one logical px = 2 device px)
from a measured SCENE plus a JSON THEME, so a theme can change every colour, add Windows-95 relief and reshape the
ruler, trim and marker lanes, and still produce a full realistic picture. It is how the chrome's design was judged
on the glass before the app painted it: `themes/frozen.json` is the design the app paints since 2026-10-02. A
standalone utility with no link path from any product target and no CMake. Every path resolves from the scripts'
own location, so the commands run from the repository root (as written here) or from `tools/palette/` alike.

```
python3 tools/palette/render.py  tools/palette/themes/<theme>.json <out.png> [--scene <tag>] [--label]
python3 tools/palette/compare.py <render.png> <reference.png>      [--scene <tag>] [--diff out.png]
python3 tools/palette/extract.py <dir>/tablet_base_<tag>.png --first-label M:SS.mmm --flags T1,T2,... \
        --clock '<clock text>' --legend '<legend text>' [--rev <commit>] [--step 125] [--out-dir DIR]
```

- `--scene <tag>` picks `scene_<tag>.json` (+ its `waveform_<tag>.json` and `glyphs/<tag>/`). The default is
  `1002`; `compare.py` takes the same tag for its lane rows, so compare a render against a picture of the same scene.
- `--label` stamps the output's file name (no extension) in Roboto 20 px, `stamp` (140,140,140), its box's top-left
  at x 300, y 1382 on the bottom row's ground (the scene's `label_box`, which `compare.py` excludes).
- The frozen design on the default scene: `python3 tools/palette/render.py tools/palette/themes/frozen.json
  tools/palette/out/frozen_1002.png` (`out/` is the conventional, git-ignored home of renders; the renderer makes
  the output's directory when it is missing).
- Rendering is deterministic (same theme + scene -> same bytes) and takes about half a second.

## Dependencies

- Python 3, numpy, pycairo >= 1.29 (cairo with its FreeType and fontconfig backends).
- The repository's `fonts/` (Roboto, Roboto Mono): the tool's private `fonts.conf` lists only that directory.
- HarfBuzz through ctypes: `libharfbuzz.so.0`, loaded when `common.py` is imported. When it is absent the tool exits
  at import with one line naming the library; there is no fallback shaper, because the text must be shaped exactly as
  the app shapes it. `verify_fonts()` also loads `libfontconfig.so.1` (the library cairo itself resolves faces with).
- No ImageMagick and no PIL: `pngrw.py` reads and writes PNG with zlib and numpy alone. Its reader takes 8-bit,
  non-interlaced grey, grey + alpha, RGB and RGBA PNGs (every PNG the tool reads is a tablet screencap or its own
  output), undoes the five row filters, drops alpha and applies no colour profile. It replaced an ImageMagick decode
  (`magick ... -depth 8 -alpha off rgb:-`) on the evidence that both return identical bytes on the three scene
  captures, the Gallery screenshots, the tool's own renders and synthetic PNGs of every filter type in RGB and RGBA.
