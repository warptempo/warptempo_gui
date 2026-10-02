# The product's faces

These two files are the product's faces on both devices (architect 2026-10-02): Roboto for every proportional row and Roboto Mono for the two clock cells. The Linux build compiles them into `warptempo_gui`, and the APK carries them as its two assets. Neither device asks the system for a font. The rules and the measured metrics live at `src/gui/gui_font.h` and `src/gui/gui_font_bundled.cpp`.

| File | Arch package | Upstream |
|---|---|---|
| `Roboto-Regular.ttf` | ttf-roboto 3.016-1 (Google's roboto-3-classic release) | https://github.com/googlefonts/roboto-3-classic |
| `RobotoMono-Regular.ttf` | ttf-roboto-mono 3.001-1 | https://fonts.google.com/specimen/Roboto+Mono |

Both are under the SIL Open Font License 1.1. Each licence travels beside its font, copied from the package: `OFL-Roboto.txt` (`/usr/share/licenses/ttf-roboto/OFL.txt`) and `OFL-RobotoMono.txt` (`/usr/share/licenses/ttf-roboto-mono/OFL.txt`).

A retune swaps a file and re-measures the table at the head of `src/gui/gui_font_bundled.cpp`, then re-derives every comment that quotes the table's rows.
