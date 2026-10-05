# WELCOME BACK, CLOUD OPUS PLANNER (rewritten 2026-10-05 by the cloud planner, for a fresh thread)

Read CLAUDE.md first (you are THE PLANNER; it holds every process rule), then this file, then
`docs/handoff/local_queue.md` (the laptop channel: you append a `## REQUEST <n>`, the local planner answers
`## DONE <n>`). The previous threads' detail is in git (`git log`, this file's history); do not re-derive it.

## Budget (2026-10-05)
- CLOUD CREDIT: about $127 left of $250 (this run of threads started near $239). An arc costs roughly $5-15; a
  long design discussion costs too. Keep turns short: coders return compact reports, read diffs by stat and hunks.
- LOCAL: 4 of 16 wakes used until the weekly reset, Tuesday 2026-10-06 ~11 pm ET; batch several arcs per REQUEST.
  The local session is KEPT RUNNING across this cloud refresh (his word, at ~130k tokens); it is refreshed at his
  word or past ~250k (local_queue.md's rule).

## How the last threads worked (keep doing this)
- An ARC = one brief -> ONE Opus-high coder (`Agent`, model opus, background; at most one fix-up message to it) ->
  your review (diff stat, targeted hunks, the build / check yourself) -> commit on MAIN with plain git (title and
  body only, NO trailers; grep the diff for private names first) -> push main. A planner edit made while a coder
  works rides in the coder's commit. Then a REQUEST when a device install is due, and a background watch:
  `for i in $(seq 1 30); do sleep 180; git fetch -q origin main; git show origin/main:docs/handoff/local_queue.md |
  grep -q "^## DONE <n>" && exit 0; done` with `run_in_background` (the notification wakes you).
- THIS HOST'S SETUP (a fresh container needs it): `pip install numpy pycairo` (the picker check, the mock tool,
  the theme tools); `apt-get install -y libfftw3-dev libwayland-dev wayland-protocols libxkbcommon-dev
  libharfbuzz-dev libjack-jackd2-dev libfreetype-dev`; `cmake -B build-nogit -S . -DWARPTEMPO_GUI_GIT=OFF` and
  `cmake --build build-nogit -j$(nproc)`; `bash tools/palette/picker/build_picker.sh --check` ("all checks pass").
- He answers only "Decisions for you"; a yes is a ruling, a go is a go. He delegated the built-in theme's colours
  to the planner ("just a fallback").

## Where things stand (HEAD edf8bde and after; everything installed on both devices at DONE 4)
- THEME FILES (CLAUDE.md's COLOURS rule is the summary): 30 roles in `kGuiThemeRoles` (src/gui/theme_file.h), one
  built-in `windows-95-standard` (Windows 95 Standard's chrome; the program's colours from Windows' 20: lime ink on
  black, green outline, warp purple / fuchsia, phase reset teal / blue, added olive / green, removed and invalid
  maroon / red, both flag labels white, playhead gray / white, the card #FFFFE1 / black / black, the clock panel
  silver / black), 98 bundled files in `assets/themes/` (`tools/theme_catalog/gen_theme_files.py`), copied in at
  every launch. His themes: laptop `warptempo`, tablet `kde3-solaris` (its selection #718BA5 is that theme's own).
- SAVE IS THE DIRTY MARK: greys unless the undo-tracked dirty flag is set (trim and the lock never light it, his
  accepted trade-off); Ctrl+S silent when grey; the clock's `*` retired. THE ICON INKS are raw Breeze again.
- THE PICKER (tools/palette/picker/, its README has every rule): 17 elements (Chrome, Canvas, Ink, Outline, Warp /
  Phase Reset / Added / Removed flags each face + selected, Playhead Head / Stem, Label, Selection, Selected Text)
  over 8 scenes, a two-column chooser (30), Copy / Paste under OLD | NEW, presets, the theme strip, his old flag keys
  read as aliases. His files: `tools/palette/picker/presets/` (copied by the laptop at every picker install).

## NEXT: THE TITLE BAR (ALL RULED 2026-10-05; WAITING ON HIS PLAIN GO — ask once, then brief)
Bumped ahead of everything: "I want the final height before I start finalizing the warp markers and phase reset
markers". The settled design:
- A Windows caption painted BY THE APP on both devices, 18 Windows px tall (SM_CYCAPTION), the app's EXISTING icon
  (do not touch the XFCE taskbar), the title in ROBOTO BOLD (add `Roboto-Bold.ttf`, OFL, beside Roboto-Regular in
  `fonts/`, provenance in its README), Minimise / Maximise-Restore / Close in Windows 95's caption buttons. Title
  text by Windows' "Document - Program" convention (the piece's name - Warptempo; confirm the wording in the brief's
  report).
- LAPTOP: ask labwc for CLIENT-SIDE decorations (`ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE`; today
  SERVER_SIDE at platform_wayland.cpp ~826), so no double bar; the caption drag is `xdg_toplevel_move`, a double
  click toggles maximised, the buttons minimise / maximise / the existing quit prompt; it starts maximised (no frame,
  as Windows hid the sizing border when maximised); RESTORED it draws Windows 95's 4-px SIZING FRAME and resizes
  through `xdg_toplevel_resize` ("always").
- TABLET: the caption across the top of the full-screen window; Close = the quit act, Minimise = to the background,
  Restore greyed (always maximised).
- COLOURS: new theme roles — the ACTIVE caption (start, gradient end, text) and the INACTIVE caption (start,
  gradient end, text; the laptop on focus loss); the catalog already records ActiveTitle / GradientActiveTitle /
  TitleText / InactiveTitle / GradientInactiveTitle / InactiveTitleText for the Windows families (18 with gradient
  pairs, e.g. Windows Standard #000080 -> #1084D0, Windows Classic #0A246A -> #A6CAF0); a theme without a gradient
  end gets a flat caption (end = start); KDE 3 / CDE map their own title colours (roles.py) — the generator imports
  them. The built-in: Windows 95's #000080 / white, inactive #808080 / #C0C0C0, flat.
- THE GRADIENT (the one exception to "no gradients"): linear per channel left to right, then DITHERED AS 15-BIT
  HIGH COLOUR — measured on his Toasty Tech reference (`tmp/ref/toastytech_win2000_my_documents.png`, gitignored;
  a new container will not have it, so these measured facts are the record): every channel 5 bits, bit-replicated,
  under an ORDERED dither repeating every 4 rows. The coder reads the exact matrix off the reference if present
  (else a 4x4 Bayer, stated as the planner's reading). His reason: the tablet showed BANDING on an earlier
  gradient, and the dither is authentic.
- NOT IN SCOPE: a status bar at the foot (ruled off 2026-08-29; row 8 is it); dithering anywhere else (the playhead
  not now; no general rule).
- Residue: CLAUDE.md's COLOURS rule (the role count, the gradient exception) and Source Map, closed_questions (the
  "no gradients" line if stated as absolute), windows95_deviations.md if the caption departs anywhere, docs/HELP.md.
- Then ONE REQUEST: the laptop build (and an eyeball: no double title bar, the frame on Restore), the APK, his
  glass judges the height.

## AFTER THE TITLE BAR
1. DISCUSS the picker's NAVIGATION with him (history vs presets; editing a preset): ask; nothing is briefed before.
2. THE ICON ARC, LAST, possibly after the Tuesday reset, WITH FABLE (his go given for it): the icons, a new LOGO; the
   picker mock tool still draws the old icon inks and the clock's `*` cell (its export bytes change when updated).
3. Small items, each on his word: PASTE greying when the paste would change nothing; a LIT (magnified) tablet
   screencap measured into the picker's `magnified` scene (its bar heights are a stand-in today).
- LONG TERM (his): the picker editing imported themes (the Windows 95 chrome rule then one option); a protocol for
  him to move themes onto the devices himself (a sync-script verb, worked out with the local planner).
