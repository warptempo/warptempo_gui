# WELCOME BACK, CLOUD OPUS PLANNER (rewritten 2026-10-04 evening by the cloud planner, for a fresh thread)

Read CLAUDE.md first (you are THE PLANNER; it holds every process rule), then this file, then
`docs/handoff/local_queue.md` (how you hand work to the laptop: a REQUEST block, answered by a DONE block). THE
PROJECT IS COMPLETE; the work is the COLOUR PICKER (a design tool beside the product) and carrying its looks into
the product.

## Where things stand (HEAD 9b19144 and after)
- THE PICKER (`tools/palette/picker/`, its README has every rule) is installed on the tablet at 891dbf0: TWELVE
  elements in his order — Chrome (one knob; every relief line by the Windows 95 rule, `tools/palette/colour.py`),
  Canvas, Ink, Unselected Flag, Selected Flag, Playhead Head, Playhead Stem, Label (the chrome's text; the field text
  follows it), Selection, Selected Text (the open flag's selected pair), Unselected Invalid Flag, Selected Invalid
  Flag — over six scenes (waveform, flags, playhead, label, open_flag, invalid_flags); the chooser runs over the
  slider rows and holds up to 15. Presets, the model switch (HSV / HSL / LCh), the theme strip. HIS VERDICT
  (2026-10-04): "everything looks good; the new elements look and work well". The playhead head's border has no
  element (it follows the Label): "ok for now, not a priority".
- HIS FILES: `tools/palette/picker/presets/` holds the tablet's `presets.json` (Preset 1, 2), `picks.txt`,
  `state.json`, copied by the local planner at every picker install (the standing act).
- THE PRESET-TO-PRODUCT HELPER (cecba52 and after): `tools/theme_catalog/build.py` makes each preset a compiled
  table entry `warptempo-preset-<n>` (the chrome by the picker's rule; its label, field text and selected pair when
  recorded), `--presets-only` where the theme sources are unreachable (the cloud); `preset_keys.py [n]` prints the
  preset's device lines (canvas, ink, outline, flags, playhead, invalid flags) and writes nothing. The product APK
  with the 100-entry table is installed; his config untouched (theme kde3-solaris).
- The laptop check (`bash tools/palette/picker/build_picker.sh --check`) runs here (pip numpy + pycairo), as does the
  product with `-DWARPTEMPO_GUI_GIT=OFF` (apt: libfftw3-dev libwayland-dev wayland-protocols libxkbcommon-dev
  libharfbuzz-dev libjack-jackd2-dev libfreetype-dev).
- BUDGET: the local laptop has used 2 of its 8 wakes until the weekly reset (Tuesday 2026-10-06 ~11 pm ET); batch.

## Settled 2026-10-04 (his words after the run)
- The roadmap's five picker arcs are done. THE CLOCK'S OWN COLOUR: it looks good following the Label (no element).
  ICON ACCENTS: deferred to the ICON ARC, after the current arc. The two decisions of the run (the field text
  follows the Label; the long invalid-flag names set small on the element button) stand as applied ("everything
  looks good").
- PRESETS NEED NO RENAME IN THE PICKER: they stay "Preset N"; a name is given in the step that turns a preset into a
  theme file.
- OUTSTANDING: the picker's NAVIGATION rework (history vs presets; editing a preset) — ask him when its time comes;
  his thinking-aloud about it is not a decision.

## NEXT ARC (proposed by him 2026-10-04; awaiting the rulings below, then a plain go): THEME FILES
A theme is a FILE in a `themes/` folder beside the device config (`device_config_path()`'s folder: XDG, then
HOME/.config, on both devices), read ONCE AT LAUNCH, so a new look needs no rebuild. The helper turns "Preset N"
into a named file (the rename lives there); the file carries recorded bytes for BOTH LEVELS (levels.py's arithmetic
run once by the helper), so the app still derives nothing. Its grammar is the device config's own key=value form
(src/gui, not frozen). A malformed file, or a key equal to a compiled theme's, is the load's FIRST-ERROR HARD FAIL
(NO BACKSTOPS: the files are the helper's output). Nothing has to happen before the brief but his rulings.

## Decisions for you (the cloud planner, 2026-10-04)
1. WHAT A THEME FILE HOLDS. (a) The chrome only (the 11 roles at light and dark); the twelve program colours stay
   device keys, pasted separately (today's split). (b) THE WHOLE LOOK: the chrome at both levels AND the twelve
   program colours (canvas, ink, outline, flags, invalid flags, labels, playhead); choosing the theme in Settings
   gives the whole look, and the twelve device keys RETIRE (one road for a colour). A compiled imported theme then
   pairs with the program's default colours; any other pairing is made in the picker (adopt the imported theme's
   ground as the Chrome) and saved as a preset, so nothing is lost. RECOMMENDED: (b) — a preset is a whole look and
   he tunes whole looks; two places for one look is a split he would have to keep in step by hand.
2. THE COMPILED TABLE. The 96 imported themes, `warptempo` and `warptempo-2026-10-03` stay compiled (the catalog is
   a fixed import, and the app boots with no files); the arc-2 entries `warptempo-preset-1` / `-2` LEAVE the table
   and become files (one road per kind). RECOMMENDED: yes.
3. THE ROAD ONTO THE TABLET. The tablet's config folder is the app's private storage. (a) A new verb in the sync
   script (his; the planner writes it, he runs it), e.g. pushing the laptop's themes folder; (b) the local planner
   by `adb shell run-as`, as config edits are today. RECOMMENDED: (a) for his own use, (b) for this arc's first
   install.
