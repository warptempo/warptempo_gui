# BRIEF: the colour picker's model switch — HSV, HSL, LCh (architect 2026-10-04)

Model: Opus, effort high. You are the implementation subagent for warptempo_gui: read CLAUDE.md, then
`tools/palette/picker/README.md` (the HSV view rule above all: the view he dials is part of the pick; bytes are the
truth) and its sources. The fence of `docs/briefs/brief_colour_picker_app.md` applies: write only under
`tools/palette/picker/` (and its README); read-only git; NO adb; never the sync script; never stage or commit. HEAD
60aa96bf (installed on the tablet: the flags round).

## The change
The three upper tracks (today H, S, V) get a MODEL SWITCH: a dropdown button (the chooser's style) that picks HSV, HSL
or LCh; the three tracks, their − / + and their one-decimal readouts then speak that model. R, G, B stay. The ring and
triangle stay HSV (GTK / GIMP's own selector) and keep working in every model. The chosen model persists in
`state.json`.
- HSL: GIMP's / CSS's HSL over the bytes (H 0–360, S 0–100, L 0–100).
- LCh: CIE LCh(ab) as GIMP's LCh scales show it (L 0–100, C 0–~150 (choose the track's range and state it), h 0–360),
  computed over the bytes AS DISPLAY-P3 (the window's colour space, render.h's "A HEX HERE IS A DISPLAY-P3 BYTE
  TRIPLE"), D65 white (P3's own; no adaptation), so the numbers describe what the glass shows. State it in the README:
  the same hex reads different LCh numbers in GIMP on the laptop (sRGB there). If you find a reason GIMP's exact
  convention (D50-adapted Lab via babl) must be followed instead, stop and report rather than choose.
- OUT OF GAMUT (LCh and nothing else can leave it): a track paints its out-of-gamut stretch in a flat neutral (no alpha,
  no hatching), and a drag or − / + that would leave the gamut stops at the last in-gamut value along that axis
  (state the search and its precision). Never write a clipped colour silently.
- THE VIEW: a pick stores the view of the model it was dialled in (the model's name + its three numbers; HSV as today),
  restored exactly on every road back (launch, BACK / FORWARD, OLD, discard, preset load) when the shown model is the
  stored one; a different model re-derives its view from the bytes. HSV keeps its retained hue (and saturation) through
  grey and black; give HSL and LCh the same retention (the hue through grey; LCh's hue through C 0). Older lines
  (`hsv h s v`, none) read as today; picks.txt / state.json / presets.json formats extended, never broken.
- − / + steps: one unit (degree / percent / L or C unit), snapping from the rounded number as today.

## The laptop check
The conversions against a reference (Python with your own exact formulas, or colour-science if installed — state it):
HSL round trip over the whole cube; LCh of known P3 values (white L 100 C 0; P3 primaries' values stated); every
model's view restored exactly through a save / relaunch / BACK; the gamut stop on a C drag; old files load unchanged
(the tablet's, in check_data); frames of the panel in HSL and in LCh (with an out-of-gamut stretch visible).

## Report
Files changed; the LCh convention and ranges; the gamut search; formats; the check's results. Build the APK and run
`build_picker.sh --check`; do not install or claim anything about the device.
