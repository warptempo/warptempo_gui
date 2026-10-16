# The product's face

The two Liberation Sans files are the product's face on both devices (the rules at `src/gui/gui_font.h`, the one face owner): LIBERATION SANS, Regular and Bold, the only face since 2026-10-12 (architect: "making Liberation Sans and GNOME 2.28 the only options, hard-coded, and drop their config keys"; Tahoma and FreeSans, the `font` device key and the Settings Font row were removed that day). The Linux build compiles both into `warptempo_gui`; the APK carries them as its assets. Both read the names from one list, `kGuiFontFiles` in `gui_font.h`. Neither device asks the system for a font. The face is drawn as its antialiased outline at every scale, at the em that matches each use's recorded vertical metrics (`GuiFaceSet` in `gui_font.h`). The files' own line metrics are never read, and any embedded bitmap strike would never be drawn.

| File | Face | Format | sha256 |
|---|---|---|---|
| `LiberationSans-Regular.ttf` | Liberation Sans Regular, version 2.1.5 | TrueType (glyf, GPOS kerning) | `baccc64becc3eb7d104b7c84d99f5314a0a1f896e2b3ea6c2f22fc08d2003bee` |
| `LiberationSans-Bold.ttf` | Liberation Sans Bold, version 2.1.5 | TrueType (glyf, GPOS kerning) | `769673c4355020b1e28a14c366a152da410ab6b16239fe883ebc35b73624835b` |

## The face set

A face set is one face's text: which file each of its four uses is drawn from, its recorded metrics (ascent, descent and one measured height in Windows px, `GuiFaceMeasure`) and its tracking (`GuiFaceSet` in `gui_font.h`). The one set, named by its files, is a stand-in for Arial (architect 2026-10-09): body and bold {11, 2, x 6} (MS Sans Serif 8's 13-row cell, measured by THE X-HEIGHT, architect 2026-10-09: "authentic to 1 windows px per measure for 1:1 proportion … I'm talking about height"), small {6, 0, 6} (the ruler's six-row digit), no tracking, its four math signs set down onto the hyphen's axis and its pipe lifted onto the digits' band. The ems are worked out from the files at the install (`src/gui/gui_font_bundled.cpp`): the body 11.357 Windows px off Liberation Sans's "x" (1082 of 2048), its cap derived to 7.813 off the "H" (1409), the small 8.474 off the "0" (1450); the bold's em comes off its own "x". Every measure is a height; the face keeps its own widths. A retune swaps a file; only the comments that quote these numbers need working out again.

## Provenance and licence

**Liberation Sans 2.1.5** (`LiberationSans-Regular.ttf`, `LiberationSans-Bold.ttf`): the liberation-fonts 2.1.5 release (https://github.com/liberationfonts/liberation-fonts), as Arch Linux packages it in `ttf-liberation` 2.1.5-2 (`/usr/share/fonts/liberation/`), byte-identical. Their `name` table says "Liberation Sans" / "Regular" and "Bold", Version 2.1.5. Liberation Sans is metric-compatible with Arial, a stand-in for it, and the product names it by its own name; its kerning is why the architect keeps it ("my go-to font because of the kerning. It looks the best at 1080p … the most well-behaved of all the fonts", 2026-10-09). TrueType, it meets FreeType's light autohinter (`src/gui/gui_font_bundled.cpp`), keeping every glyph on the baseline row at the tablet's scales. It was the product's fallback face once before, 2026-10-05 to 2026-10-06, and again beside FreeSans from 2026-10-09 (git history).

The licence that travels with them is the package's `LICENSE`, the SIL Open Font License 1.1, verbatim as `LICENSE-Liberation-OFL.txt`: "Digitized data copyright (c) 2010 Google Corporation with Reserved Font Arimo, Tinos and Cousine. Copyright (c) 2012 Red Hat, Inc." The product ships the files unmodified under their own names, as the OFL's reserved-name clause wants, with no tracking (architect 2026-10-09: no custom font targeted at a few scales is ever shipped). The face's own line box is never read: the recorded metrics are the only vertical metric source (`src/gui/gui_font.h`).

## The outline source: DejaVu Sans Bold

`DejaVuSans-Bold.ttf` is NOT a product face: it is not in `kGuiFontFiles`, neither binary compiles it in or loads it (the Linux build's font step and the APK's take the names from that list alone), and no device ever draws text in it. It is the OUTLINE SOURCE of the own icon's lettering alone: the "WARP" on the digital metronome, Warp's glyph and the program icon (architect 2026-10-16: the lettering was judged as rsvg drew it on the authoring laptop, where fontconfig resolves Gemini's family list to DejaVu Sans Bold — "dejavu looks like what gemini used — let's use that"). `tools/icon_set_adwaita_322/text_to_paths.py` reads it to turn that one `<text>` into glyph outlines, because the product's resvg draws no text; the shipped icon carries the outlines, not the font.

| File | Face | Format | sha256 |
|---|---|---|---|
| `DejaVuSans-Bold.ttf` | DejaVu Sans Bold, version 2.37 | TrueType (glyf, GPOS and kern kerning) | `b5d64817b6331723b5e59eaaa6db90057cbed58e9733f65687f110638192359f` |

Provenance: Arch Linux's `ttf-dejavu` 2.37+18+g9b5d1b2f-8 (`/usr/share/fonts/TTF/DejaVuSans-Bold.ttf`), byte-identical; its `name` table says "DejaVu Sans" / "Bold", Version 2.37. Licence: the package's `LICENSE`, verbatim as `LICENSE-DejaVu.txt` — the Bitstream Vera Fonts licence ("Fonts are (c) Bitstream"), the DejaVu changes in the public domain, and the notice of the glyphs imported from Arev (c) Tavmjong Bah. The icon's lettering is that face's glyph outlines, under those terms; the drawing itself is under the GNU LGPL v3 (`assets/icons/adwaita-3.22/README.md`).
