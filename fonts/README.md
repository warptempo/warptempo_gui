# The product's faces

These two files are the product's faces on both devices (the rules at `src/gui/gui_font.h`, the one face owner). The Linux build compiles both into `warptempo_gui`; the APK carries them as its assets. Neither device asks the system for a font. Each face is drawn as its antialiased outline at every scale, at the em that matches each use's recorded vertical metrics (`kGuiFaceMetrics` in `gui_font.h`; the files' own line metrics are never read, and their embedded bitmap strikes are never drawn).

| File | Face | Format | sha256 |
|---|---|---|---|
| `tahoma.ttf` | Tahoma (Wine Tahoma), version 0.020 | TrueType, strikes 8–16 ppem (never drawn) | `08796ee7769c6a5f4a182d3f9b696184bed56c6bfd542dede75a4915789f16e4` |
| `tahomabd.ttf` | Tahoma Bold (Wine Tahoma Bold), version 0.021 | TrueType, strikes 9–13 ppem (never drawn) | `9ebca12d00d3722d92150a4089c1e44a79e6764425fa250835f67b3db3115b0e` |

## The face set

A face set is one chrome vocabulary's text: which file each of the three uses is drawn from, its recorded metrics (ascent, descent, cap in Windows px) and its tracking (`GuiFaceSet` in `gui_font.h`), named by the chrome spec (`ChromeSpec` in `src/gui/chrome_spec.h`). There is one set, Windows 2000's (architect 2026-10-06, the Windows 2000 pivot; Nimbus Sans, the Windows 95 vocabulary's face, was dropped with it and stands in git history).

| Set | Body | Bold (the caption's title) | Small (the ruler's labels) | Metrics body / bold / small | Tracking per glyph |
|---|---|---|---|---|---|
| **win2000** | `tahoma.ttf` | `tahomabd.ttf` | `tahoma.ttf` | {11, 2, 8} / {11, 2, 8} / {6, 0, 6} | −1/20 Windows px |

The ems are read off the files at the install (`src/gui/gui_font_bundled.cpp`): the body, bold and small come to 11.003, 10.996 and 7.943 Windows px. A retune swaps a file; only the comments that quote these numbers re-derive.

## Provenance and licences

**Tahoma** (`tahoma.ttf`, `tahomabd.ttf`): Wine Tahoma as ReactOS 0.4.16 ships it. It is ReactOS's maintained fork of Wine's `fonts/tahoma.sfd` (Larry Snyder, 2004, based on Bitstream Vera Sans), with glyphs from Tavmjong Bah's Arev fonts and Greek and Hebrew glyphs and improvements by Katayama Hirofumi MZ. The files are `media/fonts/tahoma.ttf` and `media/fonts/tahomabd.ttf` of https://github.com/reactos/reactos at its `releases/0.4.16` branch, the same commit as the `0.4.16-release` tag, `822e8647792a2c43f033e3e00ed273d59f163c7b`. They are byte-identical to the copies on the ReactOS 0.4.16 i386 ISO. The last change to each was commit `13b69725008a` for the regular (2024-06-12, "[FONTS] Tahoma: Remove bitmap glyph workaround") and `9525593660b0` for the bold (2019-12-08).

The licence statements that travel with them:
- **The files' own name table** (IDs 13 and 14) states the GNU LGPL, version 2.1 or any later version. That is the licence of Wine's sources. Its text is `COPYING-LGPL-2.1.txt`, ReactOS's `COPYING.LIB` at the same commit.
- **ReactOS's licence document for the face** (`media/fonts/doc/Tahoma/LICENSE.txt` at the same commit) travels beside them verbatim as `LICENSE-Tahoma-ReactOS.txt`. It states the Bitstream Vera licence and the Arev licence for the glyphs drawn from those fonts, and says ReactOS's changes are in the public domain.

The product ships the files unmodified under their own name ("Tahoma", as ReactOS ships them). The app tracks the face by 1/20 Windows px per glyph (`kGuiTrackingPx`, architect 2026-10-06), and the files themselves are unchanged. Its hhea ascent (2049 of 2048) stands above its caps, so a program that seats text on a font's own line box sets it low; the product never does that: the recorded metrics are the only vertical metric source (`src/gui/gui_font.h`).
