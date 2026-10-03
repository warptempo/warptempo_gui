# Brief: the theme catalog, step 11 — the field and the card at dim / dark, the selected flag, the held trim cap: mock sets AR–AV (2026-10-03)

Model: Opus, effort high. You are THE CODER.

Project: Warptempo GUI, /home/b/.warptempo/warptempo_gui. Read CLAUDE.md first (you are THE CODER: you edit files, run only read-only git, never stage or commit, never launch the GUI, never build the APK, never run scripts/warptempo_sync in any form, never edit CLAUDE.md). HEAD is 98855090: the app now carries the theme catalog (docs/briefs/brief_theme_catalog_app.md; the palette at src/gui/render.h's palette block, the levels at tools/theme_catalog/levels.py). THIS BRIEF TOUCHES tools/palette/ (render.py, README.md, colour.py if needed), scratch under tmp/colour_arc2/{ar,as,at,av}/, and ONE comment-only residue item in src/gui (item 6). No product behaviour changes.

## The architect's notes (2026-10-03, on the glass)

The Windows 95 Standard DARK level is now THE TEMPLATE: every mock below is windows-95-standard only, NO other themes. On the tablet the dark level "looks excellent"; but THE TEXT FIELD (recorded #FFFFFF / #000000) "is screaming" once the selection collapses to a caret, and the TOOLTIP / CARD (#FFFFE1) is too bright as well. Both should darken with the ground. He is not happy with the UNDERLINED selected flag; the red frame on invalid input "shows up very well", which suggests a WHITE OUTLINE for selection. The held trim cap reads flat beside every other button's inverse relief. He has not ruled on any of this yet: these are mock-ups for him to judge.

## 1. The renderer gains a dialog field and a card (tools/palette/render.py)

The scene has neither today. Add, as options a theme JSON switches on (default off, so every existing theme and mock renders byte-identically — prove it with compare.py on ad2, win95_standard and frozen, 0 px):
- A MODAL DIALOG as the app paints it at 275 (read paint_modal_dialog and its field painter in src/gui/paint_handler.cpp for the geometry: the dialog face, the label, the sunken field with its margin strips, the word buttons): pick one common single-field dialog (say which); its field holds a short value with THE CARET ONLY, no selection (the state he called screaming).
- A NOTIFICATION CARD as the app paints it (paint_popup_chrome's Info face, the card's real position and text size; notifications.h's head): one ordinary card, e.g. a refusal's reason text.
- Colour roles for them, theme-overridable: field_ground / field_text (already a role: the in-place flag editor reads them too, so the EDITING flag follows every step below — keep that), info_ground / info_text, and the selected pair as today.
- THE SELECTED FLAG under flags.style "flat": keep "underline" and "outline" (step 5's white outline); add "fill" (the face in the theme's selected_fill, the label in selected_text, the DkShadow outline unchanged) and "outline+fill". The outline is ONE Windows px (3 device px at 275), white #FFFFFF, standing where the red invalid frame of the in-place editor stands (the flag's outline ring).
- THE HELD TRIM END CAP: an option to paint one end cap held, in either "flat" (today's app: Windows' DFCS_PUSHED | DFCS_FLAT, one Shadow line round the face, the glyph one Windows px right and down — read the app's painter in render.cpp) or "sunken" (the push button's pressed face: the plain sunken edge, the glyph shifted the same).

## 2. Set AR — the selected flag (dark level), 4 mocks

All at windows-95-standard DARK (tmp/colour_arc2/aq/make_aq.py's dark recipe = levels.py's table row), flags left to right EDITING, unselected, SELECTED, INVALID as in AQ, dialog and card OFF:
AR01 underline (today, the reference) · AR02 white outline · AR03 fill (the theme's navy selected pair) · AR04 outline + fill.

## 3. Set AS — the field and the card at DARK, 5 mocks (one axis: the field's and the card's darkness)

Dialog and card ON, the flags as AR01. THE RULE UNDER TEST is the ground's own level rule applied to the field and the card: hue and saturation kept (HLS), relative luminance moved to a target; on every step but AS01 the text in both is the dark level's label white #FFFFFF; the field's selected pair stays navy / white (a later axis).
AS01 today (recorded: field #FFFFFF / #000000, card #FFFFE1 / #000000) — the reference.
AS02 PROPORTIONAL: each target = its recorded luminance × L(dark ground) / L(base ground) (the ground's own ratio, 0.035 / L(#C0C0C0)).
AS03 the ground's own luminance, 0.035 (the field read by its sunken edge alone).
AS04 0.015.
AS05 RGB INVERSION of the recorded pairs (field #000000 / #FFFFFF; card #00001E / #FFFFFF).

## 4. Set AT — the field and the card at DIM, 5 mocks (one axis)

windows-95-standard DIM, dialog and card ON, flags as AR01. The field and the card STAY LIGHT with DARK text (#000000 in both), dimmer than recorded: hue and saturation kept, relative luminance at AT01 recorded (1.0 / the card's own, the reference), AT02 0.60, AT03 0.40, AT04 0.28, AT05 0.20 (the card at the same targets).

## 5. Set AV — the held trim cap (dark), 2 mocks

AV01 the held begin cap "flat" (today's app) · AV02 "sunken". Dialog and card off, flags as AR01.

For every set: full-screen 2304 × 1440 renders with `--label`, named mock_<SET><nn>_<what>.png, into tmp/colour_arc2/<set>/, each set's generator a make_<set>.py beside them (start from make_aq.py; take the levels from tools/theme_catalog/levels.py, not a copy). Print each mock's field / card / text bytes and their contrast ratios. LOOK at every mock before reporting (enlarge the dialog field, the card and the selected flag on each).

## 6. Residue (comment-only)

The architect RULED (2026-10-03): the refusal REASON CARDS STAY beside the red frame (the frame says that a value was refused, the card says why). Correct src/gui/paint_handler.cpp's modal field-chrome comment (it says the ruling is pending) and any sibling, and add a closed_questions.md line: "Removing the refusal reason cards beside the red frame — ruled out, 2026-10-03" with its owner.

## Report

Files changed, the dialog you chose, every mock's path with its bytes and contrasts, the compare.py results, and anything he might want to rule on. Commit nothing.
