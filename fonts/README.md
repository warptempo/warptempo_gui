# The product's faces

These four files are the product's faces on both devices (the rules at `src/gui/gui_font.h`, the one face owner). The Linux build compiles all four into `warptempo_gui`; the APK carries them as its assets. Neither device asks the system for a font. Each face is drawn as its antialiased outline at every scale, at the em that matches each use's recorded vertical metrics (`kGuiFaceMetrics` in `gui_font.h`; the files' own line metrics are never read, and their embedded bitmap strikes are never drawn).

| File | Face | Format | sha256 |
|---|---|---|---|
| `tahoma.ttf` | Tahoma (Wine Tahoma), version 0.020 | TrueType, strikes 8–16 ppem (never drawn) | `08796ee7769c6a5f4a182d3f9b696184bed56c6bfd542dede75a4915789f16e4` |
| `tahomabd.ttf` | Tahoma Bold (Wine Tahoma Bold), version 0.021 | TrueType, strikes 9–13 ppem (never drawn) | `9ebca12d00d3722d92150a4089c1e44a79e6764425fa250835f67b3db3115b0e` |
| `NimbusSans-Regular.otf` | Nimbus Sans Regular (URW base35) | OpenType CFF | `7c25be4d78155523080ab85b10277150657ff7dabbcad7037bdd536c9b6d0d08` |
| `NimbusSans-Bold.otf` | Nimbus Sans Bold (URW base35) | OpenType CFF | `7f33328e6b4d4cd21b45fa625791928c9407dc702db6780e56b09ca9a3ecaa67` |

## The two sets

A face set is one chrome vocabulary's text: which file each of the three uses is drawn from, its recorded metrics (ascent, descent, cap in Windows px), its tracking and whether its four math signs are set onto the hyphen's axis (`GuiFaceSet` in `gui_font.h`).

| Set | Body | Bold (the caption's title) | Small (the ruler's labels) | Metrics body / bold / small | Tracking per glyph |
|---|---|---|---|---|---|
| **ReactOS (live)** | `tahoma.ttf` | `tahomabd.ttf` | `tahoma.ttf` | {11, 2, 8} / {11, 2, 8} / {6, 0, 6} | −1/20 Windows px |
| win95 | `NimbusSans-Regular.otf` | `NimbusSans-Bold.otf` | `NimbusSans-Regular.otf` | {11, 2, 9} / {11, 2, 9} / {7, 0, 7} | −3/16 Windows px |

The ReactOS set is live (architect 2026-10-06), selected by `kGuiLiveFaceSet` in `gui_font.h`. The win95 set keeps the look of 2026-10-06's morning byte for byte. The ems are read off the files at the install (`src/gui/gui_font_bundled.cpp`): the ReactOS set's body, bold and small come to 11.003, 10.996 and 7.943 Windows px, the win95 set's to 12.35, 12.35 and 9.38. A retune swaps a file; only the comments that quote these numbers re-derive.

## Provenance and licences

**Tahoma** (`tahoma.ttf`, `tahomabd.ttf`): Wine Tahoma as ReactOS 0.4.16 ships it. It is ReactOS's maintained fork of Wine's `fonts/tahoma.sfd` (Larry Snyder, 2004, based on Bitstream Vera Sans), with glyphs from Tavmjong Bah's Arev fonts and Greek and Hebrew glyphs and improvements by Katayama Hirofumi MZ. The files are `media/fonts/tahoma.ttf` and `media/fonts/tahomabd.ttf` of https://github.com/reactos/reactos at its `releases/0.4.16` branch, the same commit as the `0.4.16-release` tag, `822e8647792a2c43f033e3e00ed273d59f163c7b`. They are byte-identical to the copies on the ReactOS 0.4.16 i386 ISO. The last change to each was commit `13b69725008a` for the regular (2024-06-12, "[FONTS] Tahoma: Remove bitmap glyph workaround") and `9525593660b0` for the bold (2019-12-08).

The licence statements that travel with them:
- **The files' own name table** (IDs 13 and 14) states the GNU LGPL, version 2.1 or any later version. That is the licence of Wine's sources. Its text is `COPYING-LGPL-2.1.txt`, ReactOS's `COPYING.LIB` at the same commit.
- **ReactOS's licence document for the face** (`media/fonts/doc/Tahoma/LICENSE.txt` at the same commit) travels beside them verbatim as `LICENSE-Tahoma-ReactOS.txt`. It states the Bitstream Vera licence and the Arev licence for the glyphs drawn from those fonts, and says ReactOS's changes are in the public domain.

The product ships the files unmodified under their own name ("Tahoma", as ReactOS ships them). The app tracks the face by 1/20 Windows px per glyph (`kGuiTrackingPx`, architect 2026-10-06), and the files themselves are unchanged.

**Nimbus Sans** (`NimbusSans-Regular.otf`, `NimbusSans-Bold.otf`): URW's Nimbus Sans, the Helvetica clone of the URW base35 set, (URW)++ 1999–2017. Artifex released it in 2017 under the GNU AGPL-3 with URW's font-embedding exception. The files are `fonts/NimbusSans-Regular.otf` and `fonts/NimbusSans-Bold.otf` of https://github.com/ArtifexSoftware/urw-base35-fonts at its `20200910` release tag, commit `c15105598aa7eb256b1ebfcecd3d078801521e73`; master's `3c0ba3b5687632dfc66526544a4e811fe0ec0cd9` has identical bytes (checked 2026-10-06). The repository's `LICENSE` (the AGPL-3 with the exception) and `COPYING` (the AGPL-3 text) travel beside them as `LICENSE-URW-base35.txt` and `COPYING-URW-base35.txt`.

It was chosen as the genre MS Sans Serif was drawn from (architect 2026-10-06). The app tracks it by 3/16 Windows px per glyph, ruled to match MS Sans Serif's pixel fitting, and the files themselves are unmodified. Its hhea ascent equals its "H" height, so it sits high in any program that seats text on a font's own line box. The product never does that: the recorded metrics are the only vertical metric source (`src/gui/gui_font.h`).
