# The product's faces

These two files are the product's faces on both devices (architect 2026-10-02): Roboto for every row, the time fields included, and Roboto Bold for the window caption's title (architect 2026-10-05, Windows' caption font being the body face in bold). There is no monospace face: Roboto Mono, the clocks' face from 2026-10-02, retired 2026-10-05 with its file and its licence (the time fields, `src/gui/paint_handler.cpp`). The Linux build compiles them into `warptempo_gui`, and the APK carries them as its two assets. Neither device asks the system for a font. The rules and the measured metrics live at `src/gui/gui_font.h` and `src/gui/gui_font_bundled.cpp`.

| File | Arch package | Upstream |
|---|---|---|
| `Roboto-Regular.ttf` | ttf-roboto 3.016-1 (Google's roboto-3-classic release) | https://github.com/googlefonts/roboto-3-classic |
| `Roboto-Bold.ttf` | none (copied from the release itself) | https://github.com/googlefonts/roboto-3-classic, release v3.016, `hinted/static/Roboto-Bold.ttf` (the release's `hinted/static/Roboto-Regular.ttf` is byte-identical to the Regular above) |

Both are under the SIL Open Font License 1.1, whose text travels beside them, copied from the package: `OFL-Roboto.txt` (`/usr/share/licenses/ttf-roboto/OFL.txt`, which covers Roboto Bold too, the same family under the same licence).

A retune swaps a file and re-measures the table at the head of `src/gui/gui_font_bundled.cpp`, then re-derives every comment that quotes the table's rows.
