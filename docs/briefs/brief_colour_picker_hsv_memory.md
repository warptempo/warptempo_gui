# BRIEF: the colour picker remembers the HSV it was dialled in — no jitter on back / forward (2026-10-04)

Model: Opus, effort high. You are the implementation subagent for warptempo_gui: read CLAUDE.md, then
`tools/palette/picker/README.md` and its sources. The fence of `docs/briefs/brief_colour_picker_app.md` applies
unchanged: write only under `tools/palette/picker/`, read-only git, NO adb (the architect is using the installed
picker right now), never the sync script, never stage or commit.

## The bug (the architect's report, 2026-10-04)
He works in HSV: one axis at a time, a tap or two on its − / +, close (save), open, tap again, and steps BACK /
FORWARD to compare. Two symptoms:
1. Stepping back / forward, an axis he never touched JITTERS: the saturation handle moves although its number does
   not change.
2. Pressing only one axis's − / + across saves, ANOTHER axis's number shifts along (he thinks the hue or the
   saturation while he stepped the value).

## The diagnosis (the planner's, verify it)
A pick is stored as BYTES only (`picks.txt`, `state.json`), and every road back to a stored colour re-derives HSV from
the bytes with `ColourState::set_rgb` (`src/picker.cpp`): the launch (`Picker::Picker`), `history_step`, the tap on
OLD, `discard_if_open`. The HSV he dialled (say S exactly 0.35) comes back as the bytes' own HSV (S 0.3467...): the
number, rounded, still reads 35, but the handle sits at the exact double — symptom 1. And the next − / + rounds from
the re-derived values (`Picker::step`'s `nearbyint(cs_.s * 100)`), while at low saturation or value one byte of
quantization is several degrees of hue or a percent of saturation — so an untouched axis's number moves — symptom 2.
Confirm both with the laptop check (a scripted session reproducing each) before fixing.

## The remedy
THE HSV HE DIALLED IS PART OF THE PICK. Keep the bytes the truth of the colour (they are what is painted and what the
product will take), and store beside each saved pick the exact HSV view it was saved under (h, s, v as the
ColourState held them — enough digits to round-trip the doubles, or the integers if the state is integral; your
call, stated). Every road back to a stored colour (the launch, BACK / FORWARD, OLD, the discard) restores THAT HSV
with the bytes instead of re-deriving it; the − / + then step from exactly what the numbers show, so no other axis
moves. Re-derivation from bytes remains only where the bytes are the input: the R / G / B sliders and their − / +,
and a stored pick that has no HSV (the lines written before this change — his current `picks.txt` has 15 of them;
it must load unchanged, and those entries behave as today).
- `picks.txt` line: append the HSV after the hex (e.g. `<time> ink #9AABEA hsv 227.0 0.3467 0.9176`); the reader
  takes both forms, strict as today otherwise. `state.json`: the same view for the active layer, old format still
  read.
- OLD restores the HSV the panel opened with, not a re-derivation.
- Also check: does anything else re-derive while the panel stays open (the wheel, the triangle, a drag's release)?
  A drag on the triangle or ring sets HSV directly and should keep doing so.
- The laptop check: both symptoms reproduced against the current code first (state the before numbers), then gone;
  the old-format `picks.txt` loads unchanged.
- README: the formats and the rule.

## Report
The confirmed diagnosis (with the numbers the check showed before), files changed, the check's results, judgment
calls. Do not install or claim anything about the device.
