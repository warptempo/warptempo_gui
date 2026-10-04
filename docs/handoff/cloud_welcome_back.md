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

## NEXT ARC: THEME FILES (his rulings 2026-10-04 evening; brief on his plain go after the three decisions below)
RULED:
- A THEME FILE HOLDS EVERY COLOUR THE GUI PAINTS: the chrome and its relief, the clock panel's own colours, the
  cards' own colours, the selected and field pairs, and the twelve program colours (canvas, ink, outline, flags,
  invalid flags, labels, playhead) — the twelve device colour keys retire into it. Files live in a `themes/` folder
  beside the device config (`device_config_path()`'s folder, both devices), read ONCE AT LAUNCH; the name is given
  where a preset becomes a file (the picker keeps "Preset N").
- ONE BUILT-IN THEME, compiled: WINDOWS 95 (Windows Standard), the basis. Every other theme — the imported catalog,
  `warptempo`, `warptempo-2026-10-03`, his presets — ships as an EXTERNAL FILE bundled with the app, not compiled.
- THE CARDS (and the tooltip) on Windows 95: the tooltip yellow #FFFFE1 under black text inside a THIN BLACK BORDER
  (one line a side). This reverses the 2026-10-03 "the card stands on the ground under the label, inside the INFO
  frame" (closed_questions' info-face lines are residue to amend); it lands with the theme files as the built-in's
  card roles, not as a compiled change first.
- THE SELECTED AND UNSELECTED FLAG LABELS share one text colour (for now). DIM is already gone (retired 2026-10-03).
- THE PICKER: its Chrome keeps the Windows 95 rule for now, and the imported themes are not made by the picker; the
  long-term goal (deferred) is the picker editing any imported theme, the Windows 95 rule then one option.
- GETTING FILES ON AND OFF THE TABLET: a new verb in the sync script (his; the planner writes it); he works the
  details out with the local planner.
- An imported theme's MAPPING is BY NAME through one table (`tools/theme_catalog/roles.py`): Windows by its registry
  names, KDE 3 by its kdeglobals names (its relief by KDE's own rule at import), CDE by COLOUR-SET NUMBER (Motif's
  fixed meaning per set: 5 the ground, 4 the fields, 1 / 2 the title bars; relief by Motif's rule). The picker's
  strip shows the SOURCE's raw names (and computed ones such as `motif:set5.ts`), every name that records a hex,
  most of which no role reads: hence the strange names. Theme files carry ROLE names, so a picker that later loads a
  file as a preset reads roles, never source names.

## Decisions for you (the cloud planner, 2026-10-04 evening)
1. THE CLOCK PANEL'S COLOURS: FOUR, not six. Today it is Windows 95's STATUS-BAR panel, ONE line a side (Shadow top
   and left, Hilight bottom and right) on the chrome ground under the label. Its own roles are then: ground, text,
   top-left line, bottom-right line. Six would mean two lines a side (the sunken well's form): a geometry change as
   well. RECOMMENDED: four, the one-line panel kept (it is Windows 95's own).
2. THE DARK LEVEL: DROP IT. Kept, it is either a calculation in the app (reversing "colours are imported, never
   derived in the app") or the offline arithmetic of today, whose results he finds not dark enough. Dropped: the
   `theme_level` key retires, levels.py leaves the product's road, and a dark look is a theme he designs (his
   presets already are dark looks). RECOMMENDED: drop.
3. A FILE MAY NAME ONLY SOME ROLES: every role it does not name takes the BUILT-IN Windows 95's value (the built-in
   is the base, a file overrides what it names); an unknown key or a malformed value is the load's first-error hard
   fail. This makes files short and hand-tunable, and it is roles.py's rule already (a role a family has no word for
   takes the app's own value): an imported KDE or CDE theme with no tooltip pair gets Windows' yellow card, and every
   imported theme gets the built-in's canvas, ink and flags until he rearranges it. RECOMMENDED: yes.
