# WELCOME BACK, CLOUD OPUS PLANNER (rewritten 2026-10-05 morning by the cloud planner, for a fresh thread)

Read CLAUDE.md first (you are THE PLANNER; it holds every process rule), then this file, then
`docs/handoff/local_queue.md` (the laptop channel: you append a `## REQUEST <n>`, the local planner answers
`## DONE <n>`). Everything earlier is in git (`git log`, this file's history); do not re-derive it.

## Budget (2026-10-05 morning)
- CLOUD CREDIT: about $40 left of $250 until the reset (Tuesday 2026-10-06 ~11 pm ET); he may top up $10-20. KEEP
  TURNS SHORT AND TERSE (his word): no status chatter ("holding off", "uncommitted"), no re-explaining. Coders
  return compact reports; read diffs by stat and hunks.
- LOCAL: 8 of 16 wakes used until the reset; the local session keeps running (he says it is "going strong").
  Batch several arcs per REQUEST.

## How the threads work (keep doing this)
- An ARC = one brief -> ONE Opus-high coder (`Agent`, model opus, background; fix-up messages to the same coder are
  fine) -> your review (diff stat, targeted hunks, build + picker check yourself) -> commit on MAIN with plain git
  (title and body only, NO trailers; name comment-only edits in frozen dirs) -> push main. A planner edit made while
  a coder works rides in the coder's commit. A REQUEST when a device install is due, then a background watch:
  `for i in $(seq 1 30); do sleep 180; git fetch -q origin main; git show origin/main:docs/handoff/local_queue.md |
  grep -q "^## DONE <n>" && exit 0; done` with `run_in_background`.
- HOST SETUP (a fresh container): `pip install numpy pycairo`; `apt-get install -y libfftw3-dev libwayland-dev
  wayland-protocols libxkbcommon-dev libharfbuzz-dev libjack-jackd2-dev libfreetype-dev`; `cmake -B build-nogit -S .
  -DWARPTEMPO_GUI_GIT=OFF && cmake --build build-nogit -j$(nproc)`; `bash tools/palette/picker/build_picker.sh
  --check`. For scratch MOCKS (sent with SendUserFile and judged in chat — he likes this): install
  fonts/Roboto-*.ttf into ~/.local/share/fonts, pycairo scripts in tmp/mock/ (gone in a new container).
- CODEX while on the cloud: he runs Sol by hand from the tracked `docs/handoff/codex_brief.md` and uploads the
  review. THE BRIEF MUST SAY: review the code AT HEAD only; commit hashes are history pointers, never a snapshot to
  check out (the last round read 7227c38 in places).
- He answers only "Decisions for you"; a yes is a ruling, a go is a go.

## Where things stand (HEAD 6940cd0; DONE 7 installed on both devices)
- THEMES: 36 roles (`kGuiThemeRoles`, theme_file.h), one built-in `windows-95-standard`, 98 bundled files. The
  caption (title bar) on both devices, Roboto Bold, its gradient dithered as 15-bit high colour. The tablet runs
  `windows-95-standard` at gui_scale 400 (his choice; the ceiling is now 1000).
- TIME FIELDS: Roboto Mono retired; every time a 17-px one-line sunken field, Roboto 13 tabular digits,
  right-aligned, fixed width. The player row seats exactly as row 8 (bottom_row_seats).
- FLAGS: "A+" (the DkShadow outline carried down both sides of the stem through the well's top lines). The flag box
  = edge + 1 W face + the printable-ASCII ink + 1 W face + edge (kMarkerFlagInkClearPx; marker_lane_rows): no
  descender meets the outline, but the box GREW (350 %: 60 -> 66 device px). Save is the dirty mark. Icon inks raw
  Breeze.

## NEXT: THE DISCUSSION HE OPENS (2026-10-05): BITMAP MODE AT MULTIPLES OF 100
His words: 400 % looked good. "Multiples of 100 get full bitmap support; non-multiples of 100 get Roboto and Breeze
icons" (Roboto + Breeze stay as the fallbacks). At an integer scale every Windows px is a whole number of device px,
so period bitmap assets scale without rounding: MS Sans Serif-equivalent BITMAP FONTS and Chicago 95 icons
(recreations of the Windows 95 assets in a Linux context). He will UPLOAD an OTB (OpenType bitmap) font in the new
thread — a Cyrillic-era font whose Latin is a pixel-by-pixel trace of MS Sans Serif (the workaround he suggested to
the Chicago95 project after modern Linux, Pango / HarfBuzz, dropped BDF / PCF; OTB is the bitmap format still
supported). FACTS FOR THE DISCUSSION: the app drives FreeType directly (gui_font.h, one road, both binaries embed
the faces); FreeType reads OTB's bitmap strikes (sfnt EBDT / EBLC); HarfBuzz shapes from the face's tables; check
cairo-ft's handling of a bitmap-only face and integer scaling of a strike (nearest neighbour, no filtering).
To settle with him: the assets and their LICENCES (an OTB trace of MS Sans Serif, Chicago95's icons — check before
bundling), the switch (gui_scale % 100 == 0 -> bitmap mode), the strike sizes (MS Sans Serif 8 pt = the 13-px cell;
the small 10), the bitmap face's ascent / descent — WORK THE FLAG BOX OUT ON IT (the flag's height is to be settled
on the period font, not Roboto) — and how the icons map (Chicago95's 16 / 22 px sets vs our 23 x 22 cases).
- THE FONT ARRIVED (2026-10-05) and is staged as fonts/crox1h.otb + crox1hb.otb (README there). MEASURED: one
  strike, ppem 11, cell 13 = ascent 11 + descent 2; caps 9, x-height 6; printable-ASCII ink rows 1..12 of the cell
  ("^{}" top 10 above the baseline, "()[]_gjpqy" 2 below). Roboto's advances match its widths at em ~10.8, i.e.
  Windows' own 8 pt em of 11 px (the "13 body" is the CELL, today used as Roboto's em: 18 % wide). His rulings:
  BUNDLE (licence settled); ROBOTO INHERITS MS SANS SERIF'S METRICS, the bitmap face the ONLY metric source, the
  same for icons and caption glyphs (the X); a DEDICATED HISTORY ICON ROW (reopens closed_questions' 2026-08-14
  "nothing hides a button"). Put to him with recommendations: Roboto's em, the flag box (16 W px on the ink rule),
  the small face, the history row's membership (derived from history_mode_disables_button).
- PENDING (put to him, unanswered; the bitmap font supersedes it for the flag): the flag box's specimen set — all
  printable ASCII (built) vs only what a resting flag shows.
- AT 400 THE ICON ROW OVERFLOWS (Load in Place, History Newer / Older hidden, History Revert cut). Put to him,
  unanswered: the history group's six non-entry buttons shown only in history mode, in the slots of the authoring
  buttons (saves ~146 W px; 400 fits, ~450 max). It matters if he stays at 400.
- LOWEST PRIORITY (his word): at 400 the caption's Maximise / Restore glyph shows artifacts and the Close X may be
  off-centre — the icon arc's.

## LATER (each on his word)
- THE ICON ARC with Fable after the reset (now tied to the bitmap mode above); a new LOGO.
- The Alt-key menu accelerators (File / Edit / Settings; the bare `E` binding freed) — low, after the reset.
- Flag colours: he picks phase reset and history-added from the 20-colour chart (warp purple and the reds stay).
- The picker's navigation: deferred ("moving down in importance").
- His to-do: retire `max_waveform_height`; remove the menu row's battery + clock legend. Ruled out 2026-10-05: menus
  behind the caption icon, removing the well.
