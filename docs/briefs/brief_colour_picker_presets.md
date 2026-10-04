# BRIEF: the colour picker's presets, and the product's themes as starting points (architect 2026-10-04)

Model: Opus, effort high. You are the implementation subagent for warptempo_gui: read CLAUDE.md, then
`tools/palette/picker/README.md` (every rule as it stands: the elements, the chooser, the histories, the one
deliberate save, the HSV view), its sources, and `tools/palette/README.md`'s export section. The fence of
`docs/briefs/brief_colour_picker_app.md` applies: write only under `tools/palette/` (the picker, render.py's export,
the READMEs); the product (`src/`, `android/app/`, `tools/theme_catalog/`) is read-only; read-only git; NO adb (the
architect is using the installed picker; nothing is installed until his word); never the sync script; never stage or
commit. HEAD has the one-decimal readouts (930d5eb3), not yet installed; build on it.

## Why
He tunes whole looks on the glass (the chrome, canvas and ink together; darks seen on the laptop come out two to three
times brighter on the tablet, so hand-tuning on the glass is the truth). He wants to keep looks and come back to them,
and to start from the product's existing themes for inspiration. No keyboard yet: names are automatic.

## Part 1 — presets
- A PRESET is the whole look: every element's colour and its exact HSV view (as `state.json` holds them), under an
  automatic name "Preset N" (N = the highest existing number + 1; never reused while the file lives).
- A PRESETS button on the panel beside the element's name (the chooser button) opens the PRESETS POP-UP, built like the
  chooser (same chrome, same tap rules: a tap on an entry acts and closes it; a tap outside closes it, nothing changes).
  Its first line is "Save as Preset N"; below it the saved presets, oldest first, each with small swatches of its
  element colours in manifest order.
- OPENING THE POP-UP IS THE CLOSE FOR THE PANEL'S EDIT (the rule the chooser already follows, architect 2026-10-04):
  an edited colour commits first, so a preset always snapshots saved colours.
- SAVE appends the snapshot to `presets.json` in the app's files dir (rewritten whole; one logcat line). Duplicates
  are allowed (his standing ruling: leave duplicates).
- LOAD sets every element to the preset's colour and view: each element whose colour or view changes gets one
  committed pick in its own history (`picks.txt`, as a commit), so BACK returns to where he was; `state.json` follows;
  the panel stays open on the active element, OLD now the loaded colour. One logcat line.
- No rename, no delete, no overwrite (they wait for a keyboard). The file format in the README; strict load, first
  error a plain message, as every file of the app.

## Part 2 — the product's themes as starting points
- The same pop-up lists, after the presets under a plain heading (e.g. "Themes"), the product's themes: every entry of
  `src/gui/theme_table.h`, LIGHT level only (the theme as its makers recorded it), by its display name, with a swatch
  of its ground. Take the list from the product's own generated table (or from exactly what generates it) at export
  time — render.py's `--export` writes it beside the manifest (e.g. `themes.json`: key, name, ground hex) — never a
  hand-kept copy; state your source.
- LOADING A THEME SETS THE CHROME ONLY: the chrome element takes the theme's light ground (one committed pick, its view
  re-derived from the bytes: there is no stored view), and every relief line follows by the WINDOWS 95 RULE, always
  (architect 2026-10-04: a loaded theme is inspiration; its own 3D Light / Hilight are not carried). Canvas and ink
  keep theirs (themes do not carry them).
- The list is long (98 themes plus the presets): the pop-up SCROLLS by a pen or finger drag on the list (a drag that
  moves past a small slop scrolls and acts on nothing; a tap acts at the lift, as every control). Keep the drag
  smooth: no re-export or re-blend per scroll frame beyond painting the list.

## The laptop check grows
Scripted sessions: save twice (names Preset 1, Preset 2; the edited panel colour committed first), load a preset
(each changed element one pick, an unchanged one none; BACK returns), a relaunch keeps presets, a theme load (Windows
95 Standard's #C0C0C0 ground gives its own quartet exactly: #FFFFFF / #DFDFDF / #808080 / #000000; a tinted theme's
ground gives the rule's lines, not its own), a scroll that acts on nothing, a tap outside; today's `picks.txt` and
`state.json` (the tablet's: 110 lines, three elements; a copy is in `tmp/picker_backup/2026-10-04_0755/`) load
unchanged. A frame PNG of the pop-up open, scrolled into the themes.

## Report
Files changed; the pop-up's layout; the formats; the theme list's source and count; the check's results; judgment
calls. Build the APK and run `build_picker.sh --check`; do not install or claim anything about the device.
