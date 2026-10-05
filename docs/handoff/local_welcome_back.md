# WELCOME BACK, LOCAL OPUS PLANNER (written 2026-10-05 by the local planner at the cloud's hand-back)

THE CLOUD HANDED PLANNING BACK TO THE LAPTOP (REQUEST 9, 2026-10-05: the cloud credit is nearly spent). You are THE
PLANNER again in the full role CLAUDE.md gives it: design with him, briefs, ONE coder at a time, review, build,
commit through the commit wrapper, install. The queue channel (`docs/handoff/local_queue.md`) is CLOSED: no watch,
no REQUEST / DONE blocks.

READ, IN ORDER: CLAUDE.md; this file; `docs/handoff/cloud_welcome_back.md` — its sections "Where things stand" and
"LANDED: THE FONTS AND METRICS ARC" are the design state and the open items (everything there is landed AND
installed now, see below); `docs/handoff/icon_pass_brief.md` — THE NEXT ARC, dispatch it AS-IS to one Opus-high
coder (the 51 PNGs and mapping.md are in `assets/icons/chicago95/16/` and `assets/icons/chicago95/`).

## Decisions for him (carried from the cloud)
- FLATTEN'S ICON: his pick (object-merge) has only 32-px art in Chicago95's 16 folder. The cloud's recommendation:
  object-group (candidate 2). Ask before or while dispatching the icon brief (it names the open slot).

## State at the hand-back (HEAD after DONE 9)
- INSTALLED ON BOTH DEVICES, verified: everything through 4a78dd0 (the 17-W-px flag box) — the theme files, the title
  bar, the time fields, A+, the history row, the period faces (Cronyx Helvetica + the Small Fonts digits at
  whole-hundred scales, Liberation Sans elsewhere). The picker APK is at the time-fields round (DONE 6).
- THE FLAG BOX AT 400 % MEASURES 68 DEVICE PX (4 edge + 60 face + 4 edge = 17 W px x 4), the label's caps 36 px with
  12 above and 12 below (3 W / 9 W / 3 W): exactly the ruling. The cloud's "64 device px at 400" in its welcome-back
  is an arithmetic slip (16 x 4), not a defect.
- Text at bitmap scales is crisp 1-bit: every text crop holds exactly its two colours (DONE 8).

## The devices (as found 2026-10-05)
- TABLET: wireless adb `192.168.1.87:5555`. THE SHELL'S `ANDROID_SERIAL` IS THE USB SERIAL, so every adb call
  needs `-s 192.168.1.87:5555` (or export ANDROID_SERIAL to it first) while the cable is out. His config (6 lines):
  `gui_scale=400`, `theme=windows-95-standard` — he sets both himself on the glass; never edit them.
- LAPTOP: labwc 1920x1080, gui_scale 138 (Liberation fallback), config 6 lines, `theme=warptempo`.
- THE PICKER (com.warptempo.picker): his active element `selected_fill`; his files in the repository at
  `tools/palette/picker/presets/` (copy them in at each picker install: the standing act).
- An advisory line on every launch, both devices: `History hid 68 commits whose sidecars refuse the strict load`
  (the history prefetch hiding old commits that fail today's load walls). Reported to the cloud in DONE 4, never
  answered: ask him whether it is expected before treating it as a defect.

## The recipes as run
- LAPTOP: `cmake --build build -j$(nproc)` (reconfigure first, `cmake -B build -S . -DWARPTEMPO_BUILD_CLI=ON`, when
  CMakeLists changed); a launch check is `timeout 8 ./build/warptempo_gui > tmp/run.log 2>&1` (exit 124 = it ran);
  a screenshot is `grim` while it runs (start it in the background, sleep 5, grim).
- PRODUCT APK: `bash android/app/build_apk.sh > tmp/apk.log 2>&1` (grep the log for `error:|warning:|asset`), then
  `adb install -r android/app/build-android/warptempo.apk`, `adb logcat -c`, `am force-stop com.warptempo.gui`,
  `am start -n com.warptempo.gui/.MainActivity`, sleep 7, `adb logcat -d -s warptempo:I`.
- PICKER: copy his three files in first; `bash tools/palette/picker/build_picker.sh --check` ("all checks pass"),
  the APK, `python3 tools/palette/render.py tools/palette/themes/picker.json --export tools/palette/out/picker`;
  THE EXPORT NEVER DELETES A RETIRED SCENE's files: compare `ls tools/palette/out/picker` with the manifest's scenes
  and delete strays on both sides before `adb push tools/palette/out/picker/. $D/scene/` + `chmod -R 777`. Never
  choose an element by touch (an empty-history element commits its colour as a first pick).
- SCREENCAPS: check first — `dumpsys power | grep mWakefulness` (Awake) and `dumpsys window | grep mCurrentFocus`
  (the app in front). A dozing tablet captures black. If he has another app in front, do not pull the product over
  it. Launch the picker BEFORE the product when both install, so the product stands in front.
- MEASURING: no PIL and no xxd on the laptop; ImageMagick does it — `magick cap.png -crop 1xH+X+Y txt:-` piped to
  `uniq -c` for run lengths, `-unique-colors` for antialiasing checks, `-fill white +opaque '#000000' -trim info:`
  for an ink box.
- TABLET CONFIG WRITE (only on his word): `adb exec-in run-as com.warptempo.gui sh -c 'cat >
  files/warptempo_gui/config' < file`, then read it back with `adb exec-out run-as … cat …` into a FILE and cmp
  (piping exec-out straight into cmp returned empty once).

## Budget
- The weekly meter resets Tuesday 2026-10-06 ~11 pm ET; he added $20 of local credit. Name the model on every
  dispatch; Opus high is the workhorse; Fable needs his approval each time (the icon arc's later redesigns and a new
  logo are Fable jobs after the reset).
- The queue's wake count closed at 10 of 16.
