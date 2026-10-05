# The product's faces

These three files are the product's faces on both devices (architect 2026-10-02): Roboto for every proportional row, Roboto Bold for the window caption's title (architect 2026-10-05, Windows' caption font being the body face in bold) and Roboto Mono for the two clock cells. The Linux build compiles them into `warptempo_gui`, and the APK carries them as its three assets. Neither device asks the system for a font. The rules and the measured metrics live at `src/gui/gui_font.h` and `src/gui/gui_font_bundled.cpp`.

| File | Arch package | Upstream |
|---|---|---|
| `Roboto-Regular.ttf` | ttf-roboto 3.016-1 (Google's roboto-3-classic release) | https://github.com/googlefonts/roboto-3-classic |
| `Roboto-Bold.ttf` | none (copied from the release itself) | https://github.com/googlefonts/roboto-3-classic, release v3.016, `hinted/static/Roboto-Bold.ttf` (the release's `hinted/static/Roboto-Regular.ttf` is byte-identical to the Regular above) |
| `RobotoMono-Regular.ttf` | ttf-roboto-mono 3.001-1 | https://fonts.google.com/specimen/Roboto+Mono |

All three are under the SIL Open Font License 1.1. Each licence travels beside its font, copied from the package: `OFL-Roboto.txt` (`/usr/share/licenses/ttf-roboto/OFL.txt`, which covers Roboto Bold too, the same family under the same licence) and `OFL-RobotoMono.txt` (`/usr/share/licenses/ttf-roboto-mono/OFL.txt`).

A retune swaps a file and re-measures the table at the head of `src/gui/gui_font_bundled.cpp`, then re-derives every comment that quotes the table's rows.
