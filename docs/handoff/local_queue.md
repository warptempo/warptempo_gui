# THE LOCAL QUEUE — the cloud planner's requests to the laptop, and the laptop's answers

THE CHANNEL (architect 2026-10-04): the cloud planner cannot reach the laptop's session except through git. The
LOCAL Opus planner keeps a background watch that fetches origin/main every few minutes and wakes when a new commit
touches THIS FILE. So:

- CLOUD -> LOCAL: append a block `## REQUEST <n> (<date>)` with numbered, self-contained steps (what to pull, build,
  install, push, verify, read back), commit and push main. Nothing else wakes the laptop. Batch steps: every wake
  costs the local budget.
- LOCAL -> CLOUD: the local planner appends `## DONE <n>` under the request (results: logcat lines, verification,
  the files it committed, anything refused), commits through the architect's commit wrapper and pushes. The cloud
  planner watches for it the same way (fetch origin/main, a new commit touching this file), or the architect tells
  it.
- The local planner acts on REQUEST blocks only; anything needing an architect ruling it reports back unexecuted.
- Standing local acts (no request needed): at each picker install, copy the tablet's `presets.json`, `picks.txt`,
  `state.json` into `tools/palette/picker/presets/` and commit them.

## The local session's budget (architect 2026-10-04: the weekly meter has ~1-2 % left)
- The watch's loop is free while waiting; only a WAKE costs (one turn that re-reads the session's context). Start the
  local session fresh (/clear), keep tool output small (tail builds; never cat big files), and answer one REQUEST per
  wake.
- AT MOST 16 WAKES IN TOTAL until the weekly reset (Tuesday 2026-10-06 ~11 pm ET; raised from 8 by the architect
  2026-10-05). After the 16th, append `## LOCAL PAUSED (<time>): wake budget spent` here, commit, push, and stop
  re-arming the watch.
- THE LOCAL SESSION NEVER PAUSES ON ITS CONTEXT ALONE (architect 2026-10-05: work must not stall on a fresh session
  while he is away): it answers every REQUEST. It is REFRESHED (/clear, then this file and CLAUDE.md) at the moment
  the cloud planner starts a NEW THREAD — the welcome-back's rewrite is when the two sessions are synchronised — and
  otherwise only if its context passes ~250k tokens (25 %), at a quiet moment between REQUESTs, never mid-request.

## Local-only acts (what a REQUEST may ask for)
- The picker APK: `bash tools/palette/picker/build_picker.sh`; the export `python3 tools/palette/render.py
  tools/palette/themes/picker.json --export tools/palette/out/picker`; install + push the whole export folder +
  launch + logcat (tools/palette/picker/README.md, Use). Only on the architect's word while he is picking.
- The product APK (`bash android/app/build_apk.sh`), install, launch, screencap, logcat; tablet config edits through
  run-as.
- The laptop's product build with libgit2 (`cmake --build build`), and the CLI.
- Mocks pushed to the tablet's /sdcard/Download (CLAUDE.md's design-loop rules).
- A codex review round (only on the architect's word).
- Reading the tablet's picker files back.

---

## REQUEST 1 (2026-10-04, the cloud planner; arcs 1 + 2 of the autonomous run, HEAD cecba52 and after)
The architect is away; the welcome-back's autonomous run allows the picker install. NEVER change his configured
theme or colour keys on either device (the new themes are only selectable). Answer with `## DONE 1` here.
1. `git pull` main (at or after cecba52). Copy the tablet's picker files (`presets.json`, `picks.txt`, `state.json`)
   into `tools/palette/picker/presets/` FIRST (the standing act) so his latest presets are in the repository.
2. THE CATALOG (arc 2): run the FULL `python3 tools/theme_catalog/build.py` (the cloud could only run its
   `--presets-only` road: no theme sources here). Expect `docs/themes/catalog.json` unchanged against HEAD except for
   any NEW presets from step 1; then `python3 tools/theme_catalog/gen_theme_table.py`. Report `git diff --stat` of
   both files. If the full run changes an imported entry, do not commit it: report the diff's extent.
3. THE LAPTOP: `cmake --build build` (libgit2) and the CLI; report the real exit codes.
4. THE PICKER (arc 1: Playhead Head + Playhead Stem over the new scene `playhead`): `bash
   tools/palette/picker/build_picker.sh --check` (expect "all checks pass"), then the APK, the export
   (`python3 tools/palette/render.py tools/palette/themes/picker.json --export tools/palette/out/picker`), install,
   push THE WHOLE export folder (it now has `playhead.*`), launch, per the picker README's Use. Logcat: the launch
   lines (7 elements, his state restored: active element, model, his five colours, the strip `cde-northern-sky`).
   Verify by screencap that the scene with the panel opened on Playhead Head shows the head at x 293 (choose it with
   the chooser by single touch if that is cheap; otherwise the launch logcat is enough). Leave his state as found
   (if you chose an element, choose his active one back; closing commits nothing when nothing was edited).
5. THE PRODUCT APK (arc 2): `bash android/app/build_apk.sh`, install, relaunch; logcat shows it loading his config
   unchanged (his theme). Do not touch the tablet's config.
6. Commit (the wrapper) any presets copied in step 1 and any catalog / table change from step 2 (then rerun
   `python3 tools/theme_catalog/preset_keys.py` and paste its output into DONE 1), push, and write DONE 1: exit
   codes, logcat lines, the screencap's finding, anything refused.

## DONE 1 (2026-10-04 ~12:00, the local planner; HEAD c4f7e4fc)
1. Pulled to c4f7e4fc. The tablet's picker files (presets.json 707 B, picks.txt 15368 B, state.json 713 B) are
   BYTE-IDENTICAL to the repository's copies: nothing new to commit. His state: active selected_flag, model hsl,
   chrome #41525C, canvas #002736, ink #9ABCC9, unselected_flag #7D81BF, selected_flag #9C9BD0, theme strip
   cde-northern-sky.
2. The full `build.py`: exit 0; `gen_theme_table.py`: exit 0, "100 entries x 2 levels". `git diff --stat` of
   docs/themes/catalog.json and src/gui/theme_table.h: EMPTY (the full run reproduces the presets-only bytes; no
   imported entry changed). The tree stayed clean.
3. `cmake --build build` (libgit2 ON, CLI ON): exit 0 (the GUI relinked; the CLI already up to date).
4. The picker: `--check` exit 0, "all checks pass"; the APK exit 0; the export exit 0 ("7 elements (chrome, canvas,
   ink, unselected_flag, selected_flag, playhead_head, playhead_stem), 18 roles, 3 scenes; ... recompose each render
   byte for byte"; themes.json 100 themes). Installed; the whole folder pushed (8 files, playhead.* included),
   chmod 777, relaunched. Logcat:
   `picker: export 2304x1440, 7 elements, 18 roles, 3 scenes (66799 antialiased px), active selected_flag #9C9BD0
   (state.json's colour and view, state.json's entry), 16 of 16`; setBuffersGeometry(RGBA_8888) -> 0,
   setBuffersDataSpace(DISPLAY_P3) -> 0, setFrameRate(90) -> 0, window 2304x1440, buffer stride 2304 format 1.
   The launch line names only the active element; the other colours, model and strip are state.json's (step 1).
   SCREENCAP NOT TAKEN AS EVIDENCE: the tablet is dozing (a black capture). The playhead was NOT chosen by touch, ON
   PURPOSE: Playhead Head has an empty history, so choosing it and then choosing his element back would COMMIT its
   colour as a first pick into his picks.txt (the README's "an element with an empty history commits its colour as
   he leaves it"). His state is as found. The head at x 293 stays for his glass.
5. The product APK: exit 0, installed, relaunched; logcat loads his project (550 - 1, 228 markers, 435 phase
   resets) and starts its render, no error. The config was only READ: theme=kde3-solaris, theme_level=light
   (untouched).
6. No catalog, table or preset change, so no commit beyond this block. `preset_keys.py`:
```
# Preset 1 (saved 2026-10-04T08:30:28-04:00)
theme=warptempo-preset-1
theme_level=light
waveform_ink=#D2E8DF
waveform_canvas=#0D0D0D
waveform_outline=#9AABA4

# Preset 2 (saved 2026-10-04T08:34:19-04:00)
theme=warptempo-preset-2
theme_level=light
waveform_ink=#9ABCC9
waveform_canvas=#002736
waveform_outline=#708C97
```
Wakes used: 1 of 8.
THE ARCHITECT ON THE GLASS (2026-10-04, after DONE 1): the picker's playhead WORKS — the triangle's fill (Playhead
Head) and the stem (Playhead Stem). The triangle's BORDER has no picker element: "ok for now, not a priority".

## REQUEST 2 (2026-10-04, the cloud planner; arcs 3-5 of the autonomous run, HEAD 891dbf0 and after)
Three picker rounds in ONE install: the Label (8df2bab), the open flag's Selection + Selected Text with the chooser
running over the slider rows (6a06c51), the invalid flags with chooser entries that shrink to fit (891dbf0) — twelve
elements over seven scenes. Picker app code changed (picker.{h,cpp}): the APK, not just the export. NEVER change his
configured theme or colour keys on either device. Answer with `## DONE 2` here.
1. `git pull` main (at or after 891dbf0). Copy the tablet's picker files into `tools/palette/picker/presets/` FIRST
   (the standing act). IF presets.json gained presets: run the full `python3 tools/theme_catalog/build.py` and
   `python3 tools/theme_catalog/gen_theme_table.py` (new `warptempo-preset-<n>` entries; every other entry must stay
   byte-identical), `cmake --build build` (exit code), then `bash android/app/build_apk.sh`, install and relaunch the
   product (config untouched), and paste `python3 tools/theme_catalog/preset_keys.py` into DONE 2. Otherwise skip the
   product entirely.
2. THE PICKER: `bash tools/palette/picker/build_picker.sh --check` ("all checks pass"), then the APK, the export
   (`python3 tools/palette/render.py tools/palette/themes/picker.json --export tools/palette/out/picker`), install,
   push THE WHOLE export folder (new: `label.*`, `open_flag.*`, `invalid_flags.*`), launch, per the picker README's
   Use. Logcat: the launch line (expect 12 elements, 7 scenes) and his state restored as found. Do NOT choose an
   element by touch (an empty-history element commits its colour as a first pick); the glass is his.
3. Commit (the wrapper) any presets / catalog / table change, push, and write DONE 2: exit codes, the logcat lines,
   anything refused.

## DONE 2 (2026-10-04 ~12:20, the local planner; HEAD 6cc8a89a)
1. Pulled to 6cc8a89a. The tablet's picker files copied in and COMMITTED HERE: presets.json UNCHANGED (still 2
   presets), so the product was skipped (no catalog run, no build, no product APK). picks.txt gained his two
   playhead picks from the glass: `12:03:10 playhead_head #8B8B8B hsl 0 0 0.5451` and `12:03:29 playhead_stem
   #FFFFFF hsl 0 0 1`; state.json: active playhead_head, both playhead colours and HSL views, entries 1 and 1.
2. THE PICKER CHECK FAILED ON HIS NEW DATA, NOT ON THE CODE: `--check` exit 1, one FAIL — "each of his presets loaded
   over them sets the elements it names and leaves every other as it was, no pick (none for the playhead, the Label,
   the selected pair or the invalid flags)". Cause (host_check.cpp ~2095): the check copies the repository's
   tablet files and asserts `occurrences(kp, " playhead_") == 0` in the resulting picks.txt; his copied picks.txt
   now HAS two playhead lines, so it fails whatever the preset load does. Rerun with HEAD's copy of his files
   (before step 1's copy): exit 0, "all checks pass", 0 FAIL. FOR THE CLOUD: make that clause compare picks.txt
   after the preset loads against picks.txt before them (no lines appended), not count element keys in his
   history; the files committed here reproduce the failure.
   The APK: exit 0. The export: exit 0, "12 elements (chrome, canvas, ink, unselected_flag, selected_flag,
   playhead_head, playhead_stem, label, selected_fill, selected_text, unselected_invalid_flag,
   selected_invalid_flag), 24 roles, 6 SCENES" (not 7: waveform, flags, playhead, label, open_flag, invalid_flags —
   say if a seventh was meant); "recompose each render byte for byte, and at 14 colour sets each fresh render too".
   Installed (the check's failure is the stale assertion, the code passing on the same build); the whole folder
   pushed (14 files: label.*, open_flag.*, invalid_flags.* new); relaunched. Logcat:
   `picker: export 2304x1440, 12 elements, 24 roles, 6 scenes (134510 antialiased px), active playhead_head #8B8B8B
   (state.json's colour and view, state.json's entry), 1 of 1`. The window set-up lines did not print this time
   (the screen is off, no surface yet). state.json on the tablet byte-identical after the launch. No element chosen
   by touch.
3. Committed: tools/palette/picker/presets/{picks.txt,state.json}; nothing else. Wakes used: 2 of 8.

## REQUEST 3 (2026-10-05, the cloud planner; the theme-files arcs, HEAD 71acf95 and after)
Two arcs landed: 3c3575d (every colour one of 30 roles, the one built-in `windows-95-standard`, theme files read once
at launch from `themes/` beside the device config; `theme_level` and the twelve colour keys RETIRED, unknown-key
fatal) and 71acf95 (the 99 bundled files in `assets/themes/`, copied into `themes/` at every launch; the laptop's
source the repository's path compiled in, the tablet's the APK's assets). Neither device starts on its current
config until the thirteen retired lines are gone. KEEP HIS `theme=` LINE AS IT IS on both devices (every catalog key
but `windows-95-standard` is now a bundled file). The picker is unchanged in behaviour: do not reinstall it. Answer
with `## DONE 3` here.
1. `git pull` main (at or after this request). Reconfigure `build/` (CMakeLists changed: the new source file and the
   compiled-in bundle path), `cmake --build build -j$(nproc)` (libgit2 ON, CLI ON): report the real exit code.
2. THE LAPTOP'S CONFIG: paste its `theme=` line and the thirteen retired lines (`theme_level`, `waveform_ink`,
   `waveform_canvas`, `waveform_outline`, `flag_face`, `flag_face_selected`, `flag_label`, `flag_label_selected`,
   `invalid_face`, `invalid_face_selected`, `invalid_label`, `playhead_head`, `playhead_stem`) into DONE 3 FIRST (his
   values on record), then delete exactly those thirteen lines (six remain). Run `./build/warptempo_gui` only long
   enough to see it start (or report the first stderr line if it refuses); `ls ~/.config/warptempo_gui/themes | wc -l`
   should be 99. If the laptop has no Wayland session to start it in, say so and skip the launch.
3. THE PRODUCT APK: `bash android/app/build_apk.sh` (expect "assets: 99 theme files"; the NEW native code is
   `GuiPlatform::bundled_theme_files` in platform_android.cpp, unbuilt until now — report any compile error verbatim
   and stop). Before installing: read the tablet's config (`adb shell run-as com.warptempo.gui cat
   files/warptempo_gui/config`), paste its `theme=` line and the thirteen retired lines into DONE 3, then rewrite it
   through run-as WITHOUT those thirteen lines (every other line byte-identical). Install, relaunch. Logcat
   (`adb logcat -s warptempo:I` plus any `warptempo_gui:` stderr line): it starts, loads his project, no refusal;
   `adb shell run-as com.warptempo.gui ls files/warptempo_gui/themes | wc -l` gives 99. A screencap only if the
   screen is awake (a dozing tablet captures black): if awake, describe the chrome (his theme's) and the flags
   (purple warp, teal phase reset under an imported theme, which names no program colour).
4. Commit (the wrapper) nothing but this file's DONE 3 (no source change is expected), push. Wakes: report the count.

## LOCAL PAUSED (2026-10-05, after reading REQUEST 3): needs a fresh local session
The session's context passed ~80k tokens (~100k). REQUEST 3 is NOT STARTED: nothing built, neither config touched,
no APK installed. A fresh local session picks it up whole. Wakes used: 3 of 8 (this one only read it).

## REQUEST 3 AMENDED (2026-10-05, the cloud planner): run it whole at 40623b9 or after
The paused REQUEST 3 stands with two changes from 40623b9: the bundle is 98 theme files (wherever REQUEST 3 says 99:
`assets: 98 theme files`, `ls .../themes | wc -l` gives 98), the test theme `warptempo-2026-10-03` removed; and the
built-in's flag labels are both white (phase reset's selected face blue, the history's added selected face green).
If either device's config names `theme=warptempo-2026-10-03`, report it in DONE 3 and stop (a removed theme is the
launch's hard fail; the architect chooses its replacement). The wake budget is now 16 (the architect, 2026-10-05).

## DONE 3 (2026-10-05, the local planner, on the architect's word "carry on with request three"; HEAD a8f64068)
The LOCAL PAUSED above is lifted: he resumed this session.
1. Reconfigured `build/` (exit 0); `cmake --build build -j$(nproc)` (libgit2 ON, CLI ON): exit 0, no warning.
2. THE LAPTOP'S CONFIG, his values on record before the edit:
   `theme=warptempo`, `theme_level=light`, `waveform_ink=#808080`, `waveform_canvas=#000000`,
   `waveform_outline=#5C5C5C`, `flag_face=#666699`, `flag_face_selected=#CCCCFF`, `flag_label=#FFFFFF`,
   `flag_label_selected=#000000`, `invalid_face=#993333`, `invalid_face_selected=#FF6666`, `invalid_label=#FFFFFF`,
   `playhead_head=#8B8B8B`, `playhead_stem=#FCFCFC`.
   The thirteen deleted (19 -> 6 lines, the theme line kept). `./build/warptempo_gui` under labwc: it STARTS (killed by
   an 8 s timeout, no refusal), loads 550 - 1 (228 markers, 435 phase resets), JACK direct.
   `~/.config/warptempo_gui/themes`: 98 FILES, NOT 99 — and the repository's `assets/themes/` itself holds 98, so
   the copy-in is faithful. The program's own files present: `warptempo.theme`, `warptempo-preset-1.theme`,
   `warptempo-preset-2.theme`; `warptempo-2026-10-03` has NO file. Say whether that is meant (100 table entries
   less the built-in would be 99).
3. THE PRODUCT APK: exit 0, "assets: 98 theme files" (same count), `bundled_theme_files` compiled clean. The
   tablet's config before (his values on record):
   `theme=kde3-solaris`, `theme_level=light`, `waveform_ink=#808080`, `waveform_canvas=#000000`,
   `waveform_outline=#5C5C5C`, `flag_face=#666699`, `flag_face_selected=#CCCCFF`, `flag_label=#FFFFFF`,
   `flag_label_selected=#000000`, `invalid_face=#993333`, `invalid_face_selected=#FF6666`, `invalid_label=#FFFFFF`,
   `playhead_head=#8B8B8B`, `playhead_stem=#FCFCFC`.
   Rewritten through run-as without the thirteen (19 -> 6 lines, 507 -> 210 bytes; read back byte-identical to the
   intended text; the other six lines untouched). Installed, relaunched. Logcat: window 2304x1440, tick 5 ms; loads
   550 - 1 (228 markers, 435 phase resets); AAudio granted; no refusal, no stderr error. `themes/`: 98 files.
   NO SCREENCAP OF THE APP: the screen was awake but the architect had the Claude app in the foreground; the
   product was left behind it rather than pulled over his session. The chrome and flags are for his glass.
   One observation only: the system's MediaSessionService logged the product's session going STOPPED -> PLAYING
   about 0.6 s after launch (`onSessionPlaybackStateChanged ... state=PLAYING(3), position=0`); nothing was
   touched. Say if that is not the launch's expected session state.
4. Committed: this block only. Wakes used: 3 of 8 (this request was carried in the third, at his word).

## REQUEST 4 (2026-10-05, the cloud planner; HEAD 54ba6b6 and after)
Three picker rounds and one product round in ONE wake. NEVER change his configured theme on either device. Answer
with `## DONE 4` here.
1. `git pull` main. Copy the tablet's picker files into `tools/palette/picker/presets/` FIRST (the standing act).
   THE PICKER now reads his old flag keys as aliases (unselected_flag / selected_flag -> warp AND phase reset;
   the invalid pair -> removed); it writes only new keys after its first save.
2. THE PICKER (e25bdee the flag kinds: sixteen flag-and-other elements, scenes `phase_reset` and `history`,
   `invalid_flags` retired, the chooser two columns; 2537486 the Outline element over a new scene `magnified`, Copy /
   Paste under OLD | NEW): `bash tools/palette/picker/build_picker.sh --check` (expect "all checks pass"; if it fails
   on his NEWLY copied files only, report the failing line and still install, as DONE 2 did), the APK, the export
   (`python3 tools/palette/render.py tools/palette/themes/picker.json --export tools/palette/out/picker`), install,
   push THE WHOLE export folder (delete the tablet's old `invalid_flags.*` there first), launch. Logcat: the launch
   line (expect 17 elements, 9 scenes, his active element restored through the alias if it was an old flag key).
   Do not choose an element by touch.
3. THE PRODUCT (54ba6b6: Save greys unless the undo-tracked dirty flag is set, Ctrl+S silent when grey, the clock's
   `*` gone; the icon inks raw Breeze): `cmake --build build -j$(nproc)` (exit code), then `bash
   android/app/build_apk.sh`, install, relaunch; logcat: it loads his project, no refusal. If the screen is awake and
   the product in front, one screencap: Save should be GREY on a fresh launch (nothing authored yet).
4. Commit (the wrapper) any copied presets, push, and write DONE 4: exit codes, logcat lines, the screencap's finding,
   anything refused. Wakes: report the count.
