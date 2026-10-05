# WELCOME BACK, CLOUD OPUS PLANNER (rewritten 2026-10-05 morning by the cloud planner, for a fresh thread)

Read CLAUDE.md first (you are THE PLANNER; it holds every process rule), then this file, then
`docs/handoff/local_queue.md` (the laptop channel: you append a `## REQUEST <n>`, the local planner answers
`## DONE <n>`). Everything earlier is in git (`git log`, this file's history); do not re-derive it.

## Budget (2026-10-05 midday)
- CLOUD CREDIT: $31 left until the reset (Tuesday 2026-10-06 ~11 pm ET); he added $20 to the local credits. KEEP
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
- THE STOP HOOK ("uncommitted changes") fires every turn while a coder works: never answer it with an explanation
  of the commit order (his word, 2026-10-05: it wastes tokens); a planner edit riding the coder's commit is known.

## Where things stand (HEAD 6940cd0; DONE 7 installed on both devices)
- THEMES: 36 roles (`kGuiThemeRoles`, theme_file.h), one built-in `windows-95-standard`, 98 bundled files. The
  caption (title bar) on both devices, Cronyx bold / Liberation Bold, its gradient dithered as 15-bit high colour. The tablet runs
  `windows-95-standard` at gui_scale 400 (his choice; the ceiling is now 1000).
- TIME FIELDS: Roboto Mono retired; every time a 17-px one-line sunken field, the body face's tabular digits,
  right-aligned, fixed width. The player row seats exactly as row 8 (bottom_row_seats).
- FLAGS: "A+" (the DkShadow outline carried down both sides of the stem through the well's top lines). The flag box
  = edge + 1 W face + the printable-ASCII ink + 1 W face + edge (kMarkerFlagInkClearPx; marker_lane_rows): no
  descender meets the outline, but the box GREW (350 %: 60 -> 66 device px). Save is the dirty mark. Icon inks raw
  Breeze.

## LANDED: THE FONTS AND METRICS ARC (ruled and committed 2026-10-05; not yet installed — the next REQUEST; the font decides the waveform's height, which he needs
settled before horizontal warp-marker placement work). Brief in flight / landed (git log). The rulings:
- BITMAP MODE at gui_scale % 100 == 0: Cronyx Helvetica 11 px (fonts/crox1h.otb, MS Sans Serif 8's 13-px cell;
  its bold crox1hb the caption) and the RECONSTRUCTED SMALL FONTS DIGITS for the ruler (0-9 . : read pixel-exact
  from his ACID 3.0 / Vegas Audio screenshots: 7 rows, tabular advance 5, '.' ':' advance 2; tools/small_fonts/),
  each strike pixel a k x k block. Elsewhere LIBERATION SANS 2.1.5 (OFL, bundled; Roboto RETIRED) at the
  em that matches the bitmap face's HEIGHT (body: Cronyx's 9-px cap, ~13.1 W px; small: Small Fonts' 7-px digit),
  each face its own true widths; Liberation's occasional glyph poking past a box is accepted. THE BITMAP FACES ARE THE ONLY
  VERTICAL METRIC SOURCE at every scale (the height is what stays constant); HORIZONTALLY the live face measures
  itself (his amendment: widths flexible). The ruler stays.
- THE FLAG BOX (2026-10-05, after the tablet look): the WHOLE CELL — edge + face + Cronyx's 13 + face + edge =
  17 W px (64 device px at 400; the caps centred, 3 above and 3 below); the waveform gave up the 2 W px.
- FLAG COLOURS: phase reset teal / AQUA (selected); history added GREEN / LIME; warp purple / fuchsia and removed
  maroon / red stay. All four are true dark / bright pairs of Windows' 16.
- History row (b0904c1): his yes to the `h` shift and the slot order (not yet seen on glass). Not installed yet.
- THE ICON PASS (his go, 2026-10-05; bitmap mode only, Breeze elsewhere): the candidate sheets went to him
  (tmp/icons/: mapping.md, contact_sheet_1/2.png, data.py + build.py regenerate them; 50 buttons + 6 alternate
  faces + 4 card/row glyphs, 1-3 Chicago95 16-px candidates each). AWAITING HIS PICKS. No match: IconRestrictUndo,
  IconBpm (no note), HistoryWalk, HistoryCumulative (no sum). DISABLED = Windows' toolbar rule (his WordPad
  screenshots): the icon's pixels that are neither white nor silver in Shadow, a Hilight copy +1,+1 (the same
  DSS_DISABLED emboss the Breeze glyphs use). The brief records Chicago95's GPL-3.0 provenance beside the assets.
  His uploads (icons, xfwm4) and tmp/icons live only in this container; a new one needs a re-upload.
- THE ICON ARC with Fable after the reset (now tied to the bitmap mode above); a new LOGO.
- The Alt-key menu accelerators (File / Edit / Settings; the bare `E` binding freed) — low, after the reset.
- The picker's navigation: deferred ("moving down in importance").
- His to-do: retire `max_waveform_height`; remove the menu row's battery + clock legend. Ruled out 2026-10-05: menus
  behind the caption icon, removing the well.
