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
(no requests yet)
