# The product's faces

These four files are the product's faces on both devices (the rules at `src/gui/gui_font.h`, the one face owner). The Linux build compiles all four into `warptempo_gui`; the APK carries them as its assets. Both read the names from one list, `kGuiFontFiles` in `gui_font.h`. Neither device asks the system for a font. Each face is drawn as its antialiased outline at every scale, at the em that matches each use's recorded vertical metrics (`GuiFaceSet` in `gui_font.h`). The files' own line metrics are never read, and their embedded bitmap strikes are never drawn.

| File | Face | Format | sha256 |
|---|---|---|---|
| `tahoma.ttf` | Tahoma (Wine Tahoma), version 0.020 | TrueType, strikes 8–16 ppem (never drawn) | `08796ee7769c6a5f4a182d3f9b696184bed56c6bfd542dede75a4915789f16e4` |
| `tahomabd.ttf` | Tahoma Bold (Wine Tahoma Bold), version 0.021 | TrueType, strikes 9–13 ppem (never drawn) | `9ebca12d00d3722d92150a4089c1e44a79e6764425fa250835f67b3db3115b0e` |
| `DejaVuSans.ttf` | DejaVu Sans Book, version 2.31 | TrueType, no strikes | `2d12064604cb0380ead9061bc8aa77d1bdd6509bb82502ee45af16b0d2e63b80` |
| `DejaVuSans-Bold.ttf` | DejaVu Sans Bold, version 2.31 | TrueType, no strikes | `9ef42492773bdf4964a766d9a8727f0769d0eb92a5de80371c04ff542b8fc753` |

## The face set

A face set is one chrome vocabulary's text: which file each of the three uses is drawn from, its recorded metrics (ascent, descent, cap in Windows px) and its tracking (`GuiFaceSet` in `gui_font.h`). The chrome spec names its set (`ChromeSpec` in `src/gui/chrome_spec.h`), and the device config's `chrome` key chooses the spec at launch. There are two sets. Windows 2000's dates from the Windows 2000 pivot (architect 2026-10-06; Nimbus Sans, the Windows 95 vocabulary's face, was dropped then and stands in git history). GNOME 2's came with the clearlooks chrome (2026-10-07).

| Set | Chrome | Body | Bold (the caption's title) | Small (the ruler's labels) | Metrics body / bold / small | Tracking per glyph |
|---|---|---|---|---|---|---|
| **win2000** | `win2000` | `tahoma.ttf` | `tahomabd.ttf` | `tahoma.ttf` | {11, 2, 8} / {11, 2, 8} / {6, 0, 6} | none |
| **gnome2** | `clearlooks` | `DejaVuSans.ttf` | `DejaVuSans-Bold.ttf` | `DejaVuSans.ttf` | {11, 2, 8} / {11, 2, 8} / {6, 0, 6} | none |

The ems are worked out from the files at the install (`src/gui/gui_font_bundled.cpp`), for every set at once, because Android installs the faces before it reads the config. The win2000 body, bold and small come to 11.003, 10.996 and 7.943 Windows px. The gnome2 ones come to 13.717, 13.717 and 7.933. A retune swaps a file; only the comments that quote these numbers need working out again.

## Provenance and licences

**Tahoma** (`tahoma.ttf`, `tahomabd.ttf`): Wine Tahoma as ReactOS 0.4.16 ships it. It is ReactOS's maintained fork of Wine's `fonts/tahoma.sfd` (Larry Snyder, 2004, based on Bitstream Vera Sans), with glyphs from Tavmjong Bah's Arev fonts and Greek and Hebrew glyphs and improvements by Katayama Hirofumi MZ. The files are `media/fonts/tahoma.ttf` and `media/fonts/tahomabd.ttf` of https://github.com/reactos/reactos at its `releases/0.4.16` branch, the same commit as the `0.4.16-release` tag, `822e8647792a2c43f033e3e00ed273d59f163c7b`. They are byte-identical to the copies on the ReactOS 0.4.16 i386 ISO. The last change to each was commit `13b69725008a` for the regular (2024-06-12, "[FONTS] Tahoma: Remove bitmap glyph workaround") and `9525593660b0` for the bold (2019-12-08).

The licence statements that travel with them:
- **The files' own name table** (IDs 13 and 14) states the GNU LGPL, version 2.1 or any later version. That is the licence of Wine's sources. Its text is `COPYING-LGPL-2.1.txt`, ReactOS's `COPYING.LIB` at the same commit.
- **ReactOS's licence document for the face** (`media/fonts/doc/Tahoma/LICENSE.txt` at the same commit) travels beside them verbatim as `LICENSE-Tahoma-ReactOS.txt`. It states the Bitstream Vera licence and the Arev licence for the glyphs drawn from those fonts, and says ReactOS's changes are in the public domain.

The product ships the files unmodified under their own name ("Tahoma", as ReactOS ships them). The app draws the face at its own advances (the win2000 set's `tracking_px` is 0, architect 2026-10-07: heights matched, never widths), and the files themselves are unchanged. Its hhea ascent (2049 of 2048) stands above its caps, so a program that seats text on a font's own line box sets it low; the product never does that: the recorded metrics are the only vertical metric source (`src/gui/gui_font.h`).

**DejaVu Sans** (`DejaVuSans.ttf`, `DejaVuSans-Bold.ttf`): DejaVu Sans 2.31 as Debian 6 "squeeze" shipped it in the package `ttf-dejavu` 2.31-1, the faces GNOME 2.30 drew its "Sans 10" in on that desktop. The files are byte-identical to `/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf` and `DejaVuSans-Bold.ttf` of that package (their `head` version 2.31). DejaVu is Bitstream Vera with a wider repertoire; its Latin outlines are Vera's design.

The licence that travels with them is the package's own copyright file, `/usr/share/doc/ttf-dejavu/copyright`, copied verbatim as `LICENSE-DejaVu-ttf-dejavu.txt`. It states the Bitstream Vera Fonts licence, and it says the DejaVu changes are in the public domain.

The product ships the files unmodified under their own name ("DejaVu Sans"), with no tracking. Their hhea ascender and descender (1901 / −483 of 2048) happen to round to the recorded 13 / 4 at Sans 10, but the product does not read them: the recorded metrics are the only vertical metric source. The files carry TrueType bytecode, which the product's light hinting does not run (`src/gui/gui_font_bundled.cpp`).
