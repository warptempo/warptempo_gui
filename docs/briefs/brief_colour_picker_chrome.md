> AMENDED (architect 2026-10-04): SELECTED is dropped from this round (selected_fill stays fixed #666666). The chooser's order is the order of importance: CHROME, CANVAS, INK; later Unselected Flag, Selected Flag, then an Open Flag with the selection (where the selected fill becomes pickable). The switch-commits rule (Part 1) is CONFIRMED.

# BRIEF: the colour picker's chrome round — the element chooser, canvas / chrome / selected, true antialiasing (2026-10-04)

Model: Opus, effort high. You are the implementation subagent for warptempo_gui: read CLAUDE.md, then
`tools/palette/picker/README.md` (the app as it stands, every rule), its sources, `tools/palette/README.md` and
`tools/palette/render.py`'s export section (`export_scene` and the comment block above it). The fence of
`docs/briefs/brief_colour_picker_app.md` applies unchanged: write only under `tools/palette/` (render.py, common.py /
colour.py if needed, themes/, picker/, the READMEs); the product (`src/`, `android/app/`, CMakeLists.txt) is untouched
and not built; read-only git; NO adb (the planner installs and verifies on the tablet); never the sync script; never
stage or commit. Build the APK and run `build_picker.sh --check` to self-check.

## Why
The ink is settling (his latest pick #A6B9DE, entry 58 of `picks.txt`). He now picks the CHROME and its neighbours the
same way, by hand on the glass, ONE element at a time but choosing WHICH. The list will grow (the unselected and
selected markers next, maybe more), so the choice is its own pop-up, and each element brings the scene it needs.

## Part 1 — the element chooser (picker app)
- The element's NAME at the panel's top becomes a BUTTON. A tap on it (at the lift, as every panel control) opens the
  CHOOSER POP-UP: a vertical list of the elements in manifest order — today Ink, Canvas, Chrome, Selected — the active
  one marked; a tap on an entry picks it and closes the chooser; a tap outside the chooser closes it and changes
  nothing. Title Case names; the same chrome as the panel (flat, opaque, no relief, no alpha).
- CHOOSING ANOTHER ELEMENT IS THE CLOSE FOR THE ONE BEING LEFT (the planner's ruling, the architect may amend): an
  edited colour is committed exactly as a close commits it (one `picks.txt` line, the logcat line, `state.json`),
  then the panel reopens on the chosen element (OLD = its current colour). Reason: he switches to see the new colour
  against its neighbours, and a switch that silently threw away the colour he is looking at would undo what he came to
  compare. Choosing the active element again is a no-op. Record this rule in the README beside the history's rules.
- EVERY ELEMENT KEEPS ITS OWN back / forward history (`picks.txt` already records the layer per line), its own cursor
  and its own HSV view: `state.json` grows `hsv` and `entry` for every element (the current file's single-layer form
  still reads), plus the active element, so a relaunch returns to the element he left.
- EVERY ELEMENT'S CURRENT COLOUR IS LIVE IN EVERY SCENE: picking the chrome shows the ink at its saved colour, and so
  on. `state.json`'s colours win over the manifest's, as today.

## Part 2 — each element carries its own scene
- The manifest lists the ELEMENTS, each naming its SCENE (a background + layers + coverage, below); several elements
  may share one scene. Today: Ink, Canvas, Chrome share today's scene (`themes/picker_ink.json`'s switches: stems,
  flags, playhead, state line off); SELECTED needs a scene where the selected fill (#666666, `selected_fill`, its text
  `selected_text` = the label) is visible — today's scene may show none. Propose the smallest truthful one from what
  render.py already draws (e.g. a pressed button's down face, the dialog's field with selected text, which also shows
  the field ground), state it. The marker elements later get a scene with flags and stems on: design the manifest so
  that is a new theme + export, no new app code.
- Rename the scene theme as fits (e.g. `themes/picker.json` or one theme per scene); the ink at #A6B9DE, the canvas
  #0E0E0E, the chrome ground #191919, the selected #666666. One `--export` produces the whole multi-scene export
  (your design; state it).

## Part 3 — the elements
- CANVAS: one more binary-mask layer, a flat fill. The OUTLINE's derive rule (50 % linear-light mix of the ink over
  the canvas) must now follow the LIVE canvas: `derive.over` names a layer (or a literal, as today).
- CHROME = ONE KNOB, THE GROUND. Every other chrome line is DERIVED LIVE by the WINDOWS 95 PROPORTIONS (Windows 95
  Standard's quartet over its ground 192; the theme `warptempo`'s light row is exactly that quartet x 25/192 — verify
  against `src/gui/theme_table.h` and `tools/theme_catalog/levels.py`):
  Hilight = ground x 255/192, 3DLight = ground x 223/192, Shadow = ground x 128/192, DkShadow = #000000,
  the field ground = Hilight (Windows 95's field is white = its Hilight), the emboss light copy = Hilight (the light
  level's rule); the label #FFFFFF fixed. Per channel, rounded to nearest, capped at 255 — use levels.py's exact
  rounding and quote it. A neutral ground reproduces today's chrome exactly (#191919 -> #212121 / #1D1D1D / #111111
  / #000000; prove it); a tinted ground keeps its hue in every line. Inventory every role the scenes paint (resolve
  render.py's alias graph: ruler ticks, icon / button faces, down_face, flag_border, trim roles, ...) and classify each
  as follows-the-ground (which line), selected, canvas, ink, or fixed; put the table in the report and the rule in the
  READMEs. render.py learns the rule too (a theme key stating the chrome ground and the rule, filling the quartet,
  field and emboss), so a mock render and the picker agree by construction.
- SELECTED (#666666): its own pickable element, OUTSIDE the rule; its text stays the label.

## Part 4 — antialiasing, the real thing (his ruling: "better to have the real thing than a simulation")
Text and icon glyphs are antialiased; relief lines and fills are not. Today's export refuses blended pixels; the
chrome's text sits on the chrome, so the picker must RE-BLEND them truthfully.
- The export records, per antialiased pixel, its COVERAGE STACK: the base (the role or literal under it) and, in paint
  order, each (role or literal, 8-bit coverage) painted over it — the disabled emboss is two layers (the light copy at
  +1,+1 then Shadow on top); an icon of a fixed colour over the ground is one. Any element may be a base or a layer of
  a coverage pixel (the FLAG rounds will need white labels over the flag face: build it general).
- Take the coverage from the renderer itself (e.g. render.py records each antialiased paint op's A8 coverage at its
  transform while rendering, or another road you prove), never a re-implementation of the text layout.
- The picker re-blends with cairo / pixman's OWN 8-bit OVER arithmetic (the exact rounding of the path cairo takes
  for a solid source through an A8 glyph / mask onto the frame; find it, port it step for step, cite the pixman
  source) so the recomposition is BYTE-IDENTICAL to render.py's picture.
- NO RASTERIZING ON THE DEVICE: coverage is precomputed on the laptop; a repaint is a lookup over the edge pixels
  (~10^5) beside the existing frame copy. Cache: recompute the coverage pixels only when a colour they read changes.
  THE PEN DRAG IS VERY SMOOTH NOW AND MUST STAY SO: measure the per-frame cost (host timing over the real scene's
  pixel counts; say what you expect on the tablet's cores) and report it. Aliased fonts / placeholder glyphs were his
  offered fallback and are NOT wanted unless your measurements say otherwise — then stop and report, do not ship the
  fallback.
- The export's own check grows: the written scene recomposed equals the render byte for byte, and ALSO for renders of
  the theme at OTHER colours (several chrome grounds — a tint, a bright one where x255/192 hits the cap — a selected,
  a canvas, an ink): the picker's recomposition at those colours equals render.py's render of the theme at those
  colours. That is the proof no pixel is mis-attributed.

## The laptop check (`build_picker.sh --check`) grows
The C++ recomposition equals the mock tool's render at the colour sets above, byte for byte (cmp); the blend function
equals pixman's on every (src, dst, coverage) it can meet (or exhaustively per channel); the chooser scripted session
(open, choose, the edited-then-switch commit, the no-op re-choice, a tap outside the chooser); per-element histories,
cursors and views independent across a switch and a relaunch; today's `picks.txt` (58 ink lines, old and new forms)
and `state.json` (`{"colours": {"ink": "#A6B9DE"}, "entry": {"ink": 58}}`) load unchanged. Frames as PNGs in
`build/check/` for the eye (the chooser open; the chrome at a tint).

## Report
Files changed; the export's design (manifest, scenes, coverage format) and sizes; the role inventory table; the
rounding quoted; the pixman arithmetic cited; the check's results; the per-frame cost measured; the Selected scene
chosen; judgment calls. Do not install or claim anything about the device.
