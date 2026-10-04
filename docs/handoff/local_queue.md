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
- AT MOST 8 WAKES IN TOTAL until the weekly reset (Tuesday 2026-10-06 ~11 pm ET). After the 8th, or if the session's context passes ~80k tokens, append
  `## LOCAL PAUSED (<time>): needs a fresh local session` here, commit, push, and stop re-arming the watch.

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
