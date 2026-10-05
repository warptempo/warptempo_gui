# THE ICON PASS — the coder brief (written by the cloud planner 2026-10-05; dispatch as-is, Opus high)

You are THE CODER (Opus, effort high) for warptempo_gui. Read CLAUDE.md first; its rules bind you (the only writer
of source; read-only git; never stage or commit; never launch the GUI; never edit CLAUDE.md; no frozen-dir edits).
Comments state current behavior and rationale at the owning symbol, dated (architect 2026-10-05). Grep before
asserting; read targeted ranges.

THE RULINGS (architect 2026-10-05):
1. BITMAP MODE ICONS. At every gui_scale that is a multiple of 100 (the same switch as the fonts:
   `gui_font_is_bitmap`, src/gui/gui_font.h — share the predicate, do not duplicate it) every button glyph listed in
   `assets/icons/chicago95/mapping.md` is drawn from its Chicago95 16-px PNG in `assets/icons/chicago95/16/`, each
   icon pixel a k x k block (k = scale / 100), nearest neighbour, no filtering, the origin on whole device px. At
   every other scale today's Breeze glyphs stay exactly as they are. Embed the PNGs into both binaries and the APK
   the way the fonts are embedded (see how CMakeLists.txt and android/app/build_apk.sh carry fonts/); decode them
   once at launch (cairo's PNG reader is already linked). The rows marked as faces of one button (Save's commit and
   pull faces, Render's cancel face, Read-Only's unlocked face, stop / pause) follow the same switch; the four
   non-button glyphs in the table (folder row, wav row, the NORMAL and CRITICAL cards) too.
2. PLACEMENT. The glyph sits in the 23 x 22 Windows case as Windows placed a 16-px toolbar bitmap. THEN CENTRE ON
   THE INK: each icon is offset by whole Windows px so its ink box (the table's L/T/R/B) is as centred in the case as
   whole pixels allow; a half-pixel tie resolves UP (vertical) and LEFT (horizontal) — the optical centre. The
   architect named the cyan arrows (go-previous / go-next: vertically low; go-up / go-down) and the cyan go-jump
   (Follow's old pick; Jump to Defining Marker) — check those in particular and report each icon's resulting
   offset. Compute the offsets at build or load from the PNG's alpha, one function, no hand table.
3. DISABLED. Windows' toolbar rule (his WordPad screenshots): a mono mask of the icon's opaque pixels that are
   neither white #FFFFFF nor silver #C0C0C0, drawn as the existing DSS_DISABLED emboss (`icons::draw_engraved`'s
   rule: the Hilight copy offset +1,+1 Windows px, then Shadow) — the same road the Breeze glyphs use when grey.
   Colours come from the palette roles as today (the icons' own colours are pixels of an image, not palette
   roles; record that at the owner as the one exception and why: they are the period artwork).
4. The pressed / checked faces keep today's +1,+1 glyph shift and checked ground (paint_button_box).

RESIDUE (pre-approved): owner comments; CLAUDE.md lines are the planner's (list any sentence now false);
docs/HELP.md if it describes icons; docs/engineering/windows95_deviations.md if relevant.

SELF-CHECK: the laptop's `cmake --build build -j$(nproc)` (or build-nogit) exits 0 with no new warnings; render an
offscreen sanity sheet of all 60 glyphs at k=4 in their cases, enabled and disabled, into tmp/icons/ and name it.
REPORT under 25 lines: files touched, the embedding, each centring offset that differs from Windows' placement.
