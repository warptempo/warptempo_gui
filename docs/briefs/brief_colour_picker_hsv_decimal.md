# BRIEF: the colour picker shows H, S and V to one decimal (architect 2026-10-04)

Model: Opus, effort high. You are the implementation subagent for warptempo_gui: read CLAUDE.md, then
`tools/palette/picker/README.md` and its sources. The fence of `docs/briefs/brief_colour_picker_app.md` applies
unchanged: write only under `tools/palette/picker/`, read-only git, NO adb (the architect is using the installed picker
right now; nothing is installed until his word), never the sync script, never stage or commit.

## The change (cosmetic, his ruling)
The H, S and V readouts at the right of their tracks today show whole numbers (`Picker`'s readout, `nearbyint` of h,
s x 100, v x 100), while the stored view is exact doubles. Show each to ONE DECIMAL (H 247.3, S 47.1, V 54.9), rounded
to nearest, so the readout shows where he actually is (GIMP's HSV fields read decimals). R, G, B stay integers.
- The − / + are UNCHANGED (whole degree / percent steps, snapping from the rounded whole number as today) — speed over
  fineness; the RGB − / + are the one-byte nudge.
- The readout box must fit "360.0" and "100.0" in the mono face without crowding: widen it as needed, and keep the
  six rows' boxes one width (symmetry); the layout stays otherwise as is.
- Nothing about storage changes (already exact doubles).
- README: the readout line. The laptop check: the frames still render; add an assertion for a readout at a
  fractional view (e.g. 247.27 -> "247.3", 0.4714 -> "47.1", 0.99999 -> "100.0").

## Report
Files changed, the readout width chosen, the check's result. Build the APK and run `build_picker.sh --check`; do not
install or claim anything about the device.
