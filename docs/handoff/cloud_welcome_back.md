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

## THE TITLE BAR — LANDED 6155f29, INSTALLED ON BOTH DEVICES (DONE 5: one bar on labwc, the tablet's caption 50
device px; the Restore frame, the drag and the height are his to judge); the record below is what was ruled
(Built as ruled, plus: the caption buttons SOFT (Windows' DFC_CAPTION, his reference); the dither cell one Windows
px (his ruling); the matrix read off the reference, Bayer transposed and shifted; an unsized restore returns to a
remembered 1400x800; a maximised window does not move by its caption. Stale outside the arc: the mock tool's
`tools/palette/tablet.py` lane sum (283 -> 333 device px) and the scenes / crops, which have no caption.)
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

## CODEX WHILE ON THE CLOUD (architect 2026-10-05)
Manual rounds until the Tuesday reset (CLAUDE.md's codex routine, its cloud sentence): the brief in the tracked
`docs/handoff/codex_brief.md`, he runs Sol and uploads the review here. The PICKER's arcs are DEFERRED (a design tool
with no link path to the product). Unreviewed product code since the last Sol round (04a3fe1): the theme catalog in
the app (7227c38, 9885509), theme files (3c3575d), the bundle (71acf95), 40623b9, Save as the dirty mark (54ba6b6) —
FOLDED INTO THE TITLE BAR'S ROUND (his yes, 2026-10-05), the Save dirty flag first.

## HIS TO-DO LIST (2026-10-05; each a small arc, the timing his)
- RETIRE `max_waveform_height`: both devices already run 0 (no maximum); the variable, its Settings row and its
  device-config key go, and the REQUEST that installs it deletes the line on both devices first (an unknown key is
  fatal). Owner: device_config.{h,cpp} (not frozen), waveform_max_h_px (render.h), main.cpp's vertical rule.
- REMOVE THE MENU ROW'S BATTERY + CLOCK LEGEND (his ruling; the charger issue is resolved, he has other clocks):
  compose_menu_legend (gui_battery.h), the battery reads on both platforms, the menu row's paint.
- ROW 8's CLOCK PANEL PADDING: the tab letter sits tight to the panel's left edge since the panel was made even;
  keep it even, widen both sides (kStatusPanelPadPx, paint_handler.cpp).
- RULED OUT 2026-10-05: menus behind the caption icon (the menu row stays); the flags stay as they are (the white
  outline is to be MOCKED only); removing the well was raised by him and set aside with the rest.

## AFTER THE TITLE BAR
1. DISCUSS the picker's NAVIGATION with him (history vs presets; editing a preset): ask; nothing is briefed before.
2. THE ICON ARC, LAST, possibly after the Tuesday reset, WITH FABLE (his go given for it): the icons, a new LOGO; the
   picker mock tool still draws the old icon inks and the clock's `*` cell (its export bytes change when updated).
3. Small items, each on his word: PASTE greying when the paste would change nothing; a LIT (magnified) tablet
   screencap measured into the picker's `magnified` scene (its bar heights are a stand-in today).
- LONG TERM (his): the picker editing imported themes (the Windows 95 chrome rule then one option); a protocol for
  him to move themes onto the devices himself (a sync-script verb, worked out with the local planner).
- 2026-10-05 (his glass at gui_scale 350, the maximum): "everything looks good", the icons' aliased bitmap style
  included. Raised: the CLOCK's mono (12 Windows px) a little large at 350 — the body (13) and the ruler (10, too
  small to go to) stay; a decision put to him: the clock at 11. The CLOSE glyph's X sits off-centre at 350 by
  rounding (correct, the icon arc's to fix with scalable glyphs, Fable). A MOCK sent of the FLAG AS A RAISED PUSH
  BUTTON (EDGE_RAISED + BF_SOFT) with its stem from the far left, beside today's flat flag (tmp/mock/flag_button.py,
  scratch; the bevelled flag was retired 2026-10-03 as "a cut through a 3D surface" — this variant moves the stem
  out of the face to the box's outer left edge). His Sol run launched (manual codex, his terminal).
- MEASURED 2026-10-05 on his ACID Pro 3.0 (lossless PNG) and Vegas Audio (JPEG) screenshots (copies in tmp/ref/,
  gitignored): THE TIME FIELDS are a ONE-LINE SUNKEN field (Shadow top / left, Hilight bottom / right) 17 Windows px
  tall (ACID 716..732; Vegas 484..500), their digits 9 px tall with 3 px of face above and below, right-aligned
  3 px in; the toolbar cases beside them 22. Ours: the clock panel takes the buttons' 22, its Roboto Mono 12 gives
  the SAME 9-px cap, but every character one 7-px cell (the colon, dot and bar too — the anachronism he named).
  Roboto 13 (the body face) also gives a 9-px cap with TABULAR digits (all 7 px) and 3-px punctuation. MOCKS sent:
  the clock today vs a 17-px field in Roboto 13 (tmp/mock/clock_350.png), and B2 — the flag button floating in the
  marker lane, the stem only inside the well under its left edge (tmp/mock/flag_button2.py). His Sol run is alive.
- RULED 2026-10-05: THE FLAG IS "A+" — today's flat flag, and the DkShadow OUTLINE CARRIED DOWN BOTH SIDES OF THE
  STEM through the well's two top lines, stopping at the canvas (tmp/mock/flag_flank.py), so the stem never touches
  the well; the well's BOTTOM unchanged. Brief it after the time-fields arc (one coder at a time).
- RULED 2026-10-05: ROBOTO MONO RETIRES; every time shows in Roboto 13 (tabular digits) in a 17-px one-line sunken
  field of FIXED width (the widest string it can show); the player's two times each their own field (the period's
  Sound Recorder / ACID). In progress with a coder, with the Sol review's still-true findings (the theme commit's
  one stale frame: "the live face always reflects what's painted").
- THE SOL REVIEW READ AN OLD SNAPSHOT in places because the brief listed arc hashes (7227c38 ...) and Sol inspected
  those commits as such; his pull was fine. NEXT BRIEFS: "review the code AT HEAD only; the hashes are history
  pointers, never a snapshot to check out". The review is archived at tmp/codex_review_sol_2026-10-05.md.
- LOW PRIORITY, FIT IN WHEREVER (his word 2026-10-05, "deferred indefinitely", small enough to ride beside another
  arc): MENU ACCELERATORS for realism — the underlined first letter of File, Edit, Settings (Alt+F / Alt+E / Alt+S
  open them, Windows' mnemonic), which needs the bare `E` key's current binding (a mouse / pointer act) REMOVED to
  free it; read chord_is_bound and the alt vocabulary (gui_input.h, input_key_dispatch.cpp) and closed_questions
  before briefing, and confirm with him which act `E` does today.
- LANDED 2026-10-05 (the time-fields commit): Roboto Mono gone; row 8's clock and the player's position / length are
  17-px time fields in Roboto 13, fixed width (the widest tab letter's slot + " | " + the widest digit in every place:
  the pipe and digits never move; only A / B's own ink differs); the Sol defect fixed (a theme commit rebuilds the
  plate and the flag cache before the next paint, kick_waveform_sync). Open, his word if he wants them: the player's
  fields unlabelled (Sound Recorder said "Position:" / "Length:"); digits left-aligned in the fixed cell (ACID
  right-aligns; near-identical at the widest cell). NEXT: brief A+; then a REQUEST (laptop build + APK).
  BUDGET: about $65 of cloud credit left (2026-10-05). HE ASKS FOR TERSE REPLIES: no "holding off / uncommitted"
  status notes.

