# BRIEF: the colour picker's flags — the Unselected Flag and the Selected Flag (architect 2026-10-04)

Model: Opus, effort high. You are the implementation subagent for warptempo_gui: read CLAUDE.md, then
`tools/palette/picker/README.md` and its sources, `tools/palette/README.md` (the export, the elements, the scenes),
`tools/palette/themes/picker.json`. The fence of `docs/briefs/brief_colour_picker_app.md` applies: write only under
`tools/palette/`; the product is read-only; read-only git; NO adb (the planner installs on the architect's word);
never the sync script; never stage or commit. HEAD 39ecf064 (installed: presets, theme strip).

## The change
The next two elements in his order of importance, after Chrome, Canvas, Ink: UNSELECTED FLAG and SELECTED FLAG (the
open flag with its selection comes later; not now). The machinery was built for this ("each later element = a new
element and scene in picker.json + a new export, no app code"); the test-only Flag Test element in the check proves
it. Use it; add app code only if something is truly missing, and say what.
- A FLAGS SCENE: the waveform scene with the marker stems and flags switched on, showing at least one unselected flag
  and one SELECTED flag (and its stem as the product draws it: the selected stem bright only when the flag box is
  addressed — `brief_theme_catalog_17_stem_follows_payload.md`; choose the state that shows the selected flag as he
  meets it most, state it). The flags as the PRODUCT draws them today with the theme `warptempo` (flat flags, face
  and stem one colour, labels): check render.py's flag picture against the product's current painter
  (`render.h`'s flag palette block, `src/gui/flag_editor.*`, the flag painter in `src/gui/render.cpp`) and REPORT any
  discrepancy you find — fix it in render.py only if it is a plain mock-tool parity bug; never touch the product.
- The elements: Unselected Flag = the unselected face (and its stem), Selected Flag = the selected face (and its stem),
  their starting colours the current design "for now" (CE01): #666699 and #9999CC. Labels stay FIXED white on both
  (his ruling: consistency) — fixed roles re-blended over the live faces by the coverage machinery. The invalid (red)
  flags and the flag frames are fixed roles, not elements.
- Both flag elements name the flags scene; Chrome, Canvas and Ink keep the waveform scene. A flag element's scene shows
  the other elements at their live colours, as everywhere.
- Presets: a preset now carries five elements; a preset saved before (three elements) still loads, leaving the flags
  as they are. state.json and picks.txt from the tablet (copy them from `tmp/picker_backup/2026-10-04_preinstall_presets/`
  into the check data if useful) load unchanged.
- THE THEMES LIST shows each theme by its CATALOG KEY (the name he types in the app's Settings, e.g. the family
  prefix groups them by provenance), not its display title; the strip's header may keep the title under the key.
  (Planner's reading of his remark 2026-10-04; small.)

## The laptop check
The recomposition equals render.py byte for byte in the flags scene at moved flag colours (and with the chrome,
canvas and ink moved); a switch between scenes (Ink -> Unselected Flag) repaints the right picture; an old
three-element preset loads; the tablet's files load unchanged; a frame PNG of the flags scene with the panel open on
the Selected Flag.

## Report
Files changed; the flags scene (which flags, which state, where on screen relative to the panel); any parity
discrepancy found; the check's results. Build the APK, re-run the export, run `build_picker.sh --check`; do not
install or claim anything about the device.
