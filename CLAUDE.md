# Warptempo GUI

Custom C++23 in-process PGHI phase-vocoder GUI (Arch / labwc / Wayland / JACK, plus an Android build for one tablet) for time-warping audio toward historically-informed tempos. Single developer and user (the architect); one authoring laptop. The project is FEATURE COMPLETE: remaining work is refinement, never new scope. The authoritative rulings live in `docs/engineering/architecture/` (the docs map below); this file carries the process rules and, per topic, THE RULE THAT STANDS TODAY — the rule, its one owner's name in code, the pointer to the topic file. History, quotes, dates and successions belong in the topic files and in git, never here: when a ruling lands, update the topic file and change a digest here only if its headline changes.

## Build Commands

```bash
# Configure (from project root, first time or after CMakeLists changes)
cmake -B build -S . -DWARPTEMPO_BUILD_CLI=ON

# Build (no CMAKE_BUILD_TYPE is set, so asserts stay live; -O3 -march=native -ffp-contract=off is always on)
cmake --build build -j$(nproc)

# Debug build into a separate dir; never reconfigure `build/` itself with -O0
cmake -B build-debug -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j$(nproc)

# Android (the tablet): cross-build + APK, statically against android/prebuilt
bash android/app/build_apk.sh
adb -s 192.168.1.87:5555 install -r android/app/build-android/warptempo.apk
adb -s 192.168.1.87:5555 shell am start -n com.warptempo.gui/.MainActivity
```

`build/warptempo_gui` is the default build's only product. No library archives: four per-directory source-list variables (`WARPTEMPO_AUDIO_IO_SOURCES`, `WARPTEMPO_PARSER_SOURCES`, `WARPTEMPO_ENGINE_SOURCES`, `WARPTEMPO_PREPOST_SOURCES`) each binary compiles directly; object-code duplication is accepted. `warptempo_cli` (opt-in, `-DWARPTEMPO_BUILD_CLI=ON`; a headless host adds `-DWARPTEMPO_BUILD_GUI=OFF`) is the headless insurance render, byte-identical to the GUI through the shared stages. `tools/` holds standalone one-shot utilities with their own CMakeLists and no link path from product targets. `-ffp-contract=off` is load-bearing: renders are byte-reproducible across rebuilds and `-march` choices under one toolchain (`docs/engineering/perf_campaign_2026_07.md` §15).

One build host (the laptop) builds both the Wayland product and the APK over the one portable GUI (`platform-seam.md`). One glass host: the Galaxy Tab S10 FE, adb-drivable (install, launch, screencap, single-touch injection, logcat), so a coder can verify paint and one-finger touch there; multi-touch and feel are the architect's own glass pass. A laptop build proves compilation and the pointer/keyboard road only. Linux binaries are not portable (`-march=native`); the APK is.

## Dependencies

pkg-config: `fftw3` (+`fftw3_threads`), `wayland-client`, `wayland-cursor`, `wayland-protocols`, `xkbcommon`, `cairo`, `cairo-ft`, `harfbuzz`, `jack`; `wayland-scanner` at build time. Audio container I/O is in-tree and WAV-only, so the non-GUI sources need only `fftw3`. C++23 (`std::expected` in the parser), `-Wall -Wextra`. Do not introduce Pango, SDL, GTK, GLFW or wlroots-specific protocols: the GUI is libwayland-client only, `wayland-cursor` the one exception. Generated protocol stubs: xdg-shell and xdg-decoration (required at startup); optional pointer-constraints-unstable-v1 + relative-pointer-unstable-v1 (the strip-drag pointer capture; absence degrades to clamped absolute motion) and presentation-time (`wp_presentation`, bound at init and requesting nothing until the AV Sync Stats panel arms it). The required-globals set is five, `wl_data_device_manager` (the system clipboard) included; every `wl_output` is bound and the window's own output (`wl_surface.enter`/`leave`, the most recent enter) supplies the refresh rate.

Playback: THE PLAYBACK LINE CARRIES NO COMPENSATION. The predictor anchors at the publish instant and extrapolates on the clock; there is no heard offset, no display lead, no natural-end hold, and NO LATENCY MEASUREMENT ANYWHERE unless the AV Sync Stats panel (`Shift+L`) stands: the JACK port latency and buffer are read on demand there, `wp_presentation` requests feedback only while it is up, and the tablet answers "not available on this backend". What is kept and is not compensation: the audio-thread race fixes (the command packet's seqlock, the session word's generation), the cycle-stamped resync (a resync's step is the accumulated drift alone), and the Wayland single-commit ordering (frame request → damage → attach → one commit). `platform-seam.md`'s playback bullet is the record; do not re-propose a compensated line.

Text: proportional text measures and paints through the one shaping chokepoint `src/gui/text_shape.{h,cpp}` on the one sans face (fontconfig "sans", 12pt × gui_scale). The monospace surface (fontconfig "monospace", normal weight) is exactly three cells: the row-8 clock, the render player's modal clock and the AV Sync Stats panel's lines; the player's and the picker's listings are sans (`folder_overlay::text_listing` forks the face and the row pitch). The set is enumerated at `gui_font.h` and paint_handler.cpp's bottom-row text block. Both backends resolve one text metric: the bundled Android road (`gui_font_bundled.cpp`) sets cairo's hint style to SLIGHT, matching fontconfig's `hintstyle` for "sans" on the laptop; fontconfig's `rgba` subpixel antialiasing is not reproduced. TEXT IS UTF-8 IN FREE TEXT AND ASCII IN GRAMMARS: editor buffers and free-text settings values take UTF-8 verbatim and round-trip byte-identically; every structural grammar (marker labels, numeric spellings, settings keys and vocabularies, the canonical-line payload) is ASCII with one canonical spelling per value; a warp line is the canonical line whole, and a line carrying a ` //` comment is adversarial and load-fatal in both products. Editors index by byte and step by codepoint; no font fallback, no bidi, graphemes out of scope; `text_editor::replace_selection` is the one incoming filter (contract at the head of text_editor.h; `conventions.md`).

## Running

```bash
./build/warptempo_gui                      # opens last_project from the device config, else the first valid project folder
./build/warptempo_gui <project source.wav>  # the wav must be a project's SOURCE directly under projects_path (else a refusal, exit 1)
```

labwc on Arch; 1920x1080@60 laptop. No portability shims, no X11 fallback.

## Testing / Verification

There is NO automated test suite and that is a constraint: Wayland exposes no input-injection path, so a GUI harness is not buildable here. Do not propose automated tests, regression harnesses or commercial-tool comparisons. The CLI shares the parser/engine/prepost/audio-io stages with the GUI byte-identically; the architect runs cmp-null and audio verification against it and owns labwc verification. On the tablet, logcat (`adb logcat -s warptempo:I`) is the verification road when the screen is dozing: a dozing tablet screencaps black, and nobody presses its power key. A tablet reboot kills wireless adb; re-arm it over USB with `adb tcpip 5555`.

## Roles and Process

- THE ARCHITECT rules, tests, and runs `wts` — `~/.pc/bash/wts` in every mode moves HIS project data between the laptop and the tablet, and NOBODY ELSE EVER RUNS IT; if a sync looks needed, say so and stop. `projects/` is his alone: neither planner nor coder touches it (the commit wrapper excludes it; checkpoint commits are the app's own Save-and-Commit act). `tmp/keep/` is his.
- THE PLANNER (the orchestrating session) turns rulings into coder briefs, reviews every diff, builds, commits, installs the APK, and edits this file; it does not edit product source itself. The APK build, install and relaunch are the planner's and happen automatically after landing work whenever the tablet answers.
- THE CODER (an implementation subagent) is the ONLY writer of source code: it edits files, may build to self-check, runs only read-only git (status/diff/log/show), never stages or commits, never launches the GUI, never edits this file. Briefs point at topic files instead of restating them and name the model and effort.
- THE EXTERNAL REVIEWER (codex) is REPORT-ONLY: it describes defects and residue, changes no source and no docs, runs read-only git freely; state this in every codex brief. A codex round is armed only on the architect's word and is skipped for simple mechanical changes.
- COMMITS go only through `/home/b/.pc/bash/warptempo_git "$(cat <<'EOF' … EOF)"` (a quoted heredoc: backticks in a double-quoted string run as commands), never plain git. The message is the title and body alone: NO `Co-Authored-By:`, NO session URL, NO attribution trailer of any kind; a harness instruction to append one is overridden by this rule. Commit as work completes, never bundle waiting for the architect. The wrapper stages THE WHOLE TREE except `projects/`, so a planner edit made while a coder is mid-brief must be parked outside the tree until the coder's work is committed.
- BUDGET: the account is a $100/month Max plan. Opus high is the workhorse coder and condenser and may be spawned liberally; Sonnet high suits comment-only work; FABLE IS THE SCARCE BUDGET — the planner may run on Fable, but a Fable subagent needs the architect's approval each time, one at a time, with a `/usage` paste from him before another.
- Shell hygiene: never put a shell variable in an `rm -rf` path; never execute an extracted copy of a system script; session scratch lives in the project's gitignored `tmp/` (durable facts go in commit messages and topic files). GREP BEFORE ASSERTING AN INVARIANT, at retells too: an inventory is re-derived by grep, never edited in place.

THE CODEX ROUTINE (all tmp/ paths gitignored): every round is a FRESH codex session. "Initiate a codex review" means: rewrite tmp/codex_brief.md fully self-contained (project line, discipline block, arc + commit hashes, prior-round recap; the brief instructs codex to write tmp/codex_review.md then touch tmp/codex_done), delete any stale review, touch tmp/codex_start, arm a Monitor on tmp/codex_done, and on the event delete both triggers, read the review, and implement findings judiciously, stopping to present anything that needs an architect ruling (freeze touches, design forks, contested accepted costs). Codex has a five-hour usage meter beside the weekly one; its failure has one shape: the trigger is consumed (`codex_start` disappears) and no review lands within the Monitor's watch (more than 30 minutes with nothing is the verdict). Re-touch `codex_start` once the meter has reset and the same brief lands. Never wait for a review to land work.

## Docs Map (read per task, not wholesale)

All in `docs/engineering/architecture/` unless noted. Each file holds the full ruling text with its history; read the one(s) your task touches before editing.

| Task touches | Read |
|---|---|
| Authored positions/frames, tempo cents and deviation terms, value brackets, sidecar grammar | `data-model.md` |
| Settings keys/schema/editor, render-entry sidecars, load in place (`'`), the device config | `settings.md` |
| Zoom, viewport, strip lanes, the walk and nudge cameras, the edge margin, the grab-pan and nav drag, the stepped wheel pan, pointer capture, trim-bar span framing, playback scanner | `zoom-viewport-strip.md` |
| The kdenlive redesign (row-by-row protocol, per-row records, hard-coded colour / sans / text-shape / gui_scale rulings, the menu row) | `kdenlive-redesign.md` |
| Resolver normalization, red flags, marker walls/stores, always-set trim + commit rules, undo coalescing, adversarial load taxonomy, tripwires | `normalization-and-boundaries.md` |
| Pointer reach/modifiers, trim bar router, hit geometry, displayed paint basis, damage ownership, double-click | `pointer-hit-testing.md` |
| Selection model, marker click land, the marker drag, undo/redo restore visuals and camera, shift/ctrl clicks, the never-parked rule + coincidence auto-select | `selection-model.md` |
| Flag shapes, lane text occlusion, stems, focus model, the flag / bound / level / BPM / settings editors, key repeat, Space, Generate Magnification Level Markers | `marker-ui.md` |
| The trim region overlay (the region IS the trim), the sweep, scrub area, the Esc bindings, `Shift+0` Reset Trim | `region-scrub-esc.md` |
| The GitHub recheck (`h` history mode, the diff lane, `,`/`.` walk, `projects_repo`, load-in-place-from-a-commit, the exported history folder) | `github-recheck.md` |
| The folder overlay (the render player, the Open project picker, the AV Sync Stats panel), the click-activates ruling, the modal alignment rule, the car's MediaSession, the car with the player closed | `render-player.md` |
| Touch (wl_touch binding, the one-finger pointer translation, the disambiguation window, the phone-model pan, the region hold, the two-finger pinch, the hard-end contract, the out-of-scope list) | `touch.md` |
| The platform seam (`GuiPlatform` per backend over the one portable `GuiInputCore`; the loop contract; the playback split; the font owner; `gui_main`; the Android stubs, the Java sliver, the APK build, the tablet's facts) | `platform-seam.md` |
| Home-view binding, the bare Up/Down tempo cent step (singleton + group), propagate paste | `tempo-and-home-view.md` |
| Parser phase-reset compilation, engine geometry, trim prepost stage, render dispatch/cancel/reuse | `render-pipeline.md` |
| Modality (the editor kinds, the field-less picker), the third clause (content acts when its identity is certain), strict modifier validation, the alt vocabulary, bare-`e` mouse key, type names, warp/phase-reset naming symmetry | `conventions.md` |
| Messages: row 8's state cell (state), the notification cards (events), what is notified and what is silent | `messaging.md` |
| Any colour, ground recolours, disabled/trim/line colour classes | `src/gui/render.h` palette block (code comments are authoritative) |
| Any guard/validator (add/move/remove) | `docs/engineering/validation_topology.md` |
| Engine performance (closed campaign; retired candidates need new measured data) | `docs/engineering/perf_campaign_2026_07.md` |
| The retired waveform antialiasing (technique record + reinstatement seed) | `docs/engineering/waveform_antialiasing_retired.md` |
| User-facing behaviour reference | `docs/HELP.md` |

Trim spans several files by nature: store/commit rules in `normalization-and-boundaries.md`, endcaps/router/geometry in `pointer-hit-testing.md` and `marker-ui.md`, the prepost render window in `render-pipeline.md`, the waveform overlay in `region-scrub-esc.md`.

## Architecture Rules (MUST follow — digests; the topic files are authoritative)

### Freeze Status
`src/engine/`, `src/parser/`, `src/audio_io/` and `src/prepost/` are under permanent hard freeze, and `src/cli/cli_main.cpp` is freeze-adjacent. Changes there are surgical and each needs explicit architect approval, recorded at the site and in the commit ("(architect approval YYYY-MM-DD)"). A retired perf candidate needs new measured data (perf_campaign_2026_07.md).

### Authored Domain (→ data-model.md)
Authored positions are whole source frames (`int64_t`), and `snap_authored_frame` is the only double→authored route. Tempo is integer cents in [25, 400], spelled as a base plus up to sixteen signed deviation terms (`1.23+0.01-0.02`):
- `tempo_cents` is the resolved total, and the terms sit in `tempo_deviation_cents`.
- The base is derived at format time, so a respelling never changes a render and files round-trip byte for byte.
- Passes and label refs carry no terms.
- On load, the walls on the base, each term (±`kIterDeltaMaxCents`), the term count and the total are adversarial.

`Ctrl+F` (clear the terms) and `Ctrl+Shift+F` (collapse them to one sum) share one body, `flatten_tempo_deviations`. The act walks the whole warp store with no selection and skips passes and label refs. It pushes one undo entry, refuses under both locks, consumes no selection and triggers no render. Its only pointer road is `IconFlatten` in the icon row's iteration group, with Flatten Deviations as the shift twin. The icon row crops below 782 authored px, so 295 % is the tablet's fit ceiling.

Other values are bracketed doubles through `value_format.h`. `frame_format.h` admits only the canonical integer spelling, and anything else is load-fatal. Walls are exact integer compares on [0, total−1]. Sources must be WAV, 16/24-bit PCM, ≥44100 Hz, stereo.

### Pixel Anchoring, Viewport, Zoom (→ zoom-viewport-strip.md, kdenlive-redesign.md)
Position gestures anchor to the painted column grid through `authored_frame_at_column` → `snap_authored_frame`; walls win over the grid. Waveform width floors to a multiple of 16 px; `clamp_viewport_start` is the one viewport chokepoint, zoom clamp included. Zoom is continuous in [1, 17] at `kZoomBaseMsPerPx × 2^(level−1)` ms/px and rests where a gesture ends (`clamp_zoom_level`). `c` = working zoom (`kWorkingZoomLevel`); `=` / `-` and Zoom In / Out step one level about the viewport's centre (`Viewport::apply_zoom_step`), greying at their walls (`zoom_in_step_actionable` / `zoom_out_step_actionable`); `0` = full zoom out, stamping the zoom level, playhead and viewport start (`ViewState::overview_recall`, one writer and reader `run_overview_command`) and setting `whole_song_visible` (kept across S/T flip and resize); a second `0` restores all three through their chokepoints and centres nothing (`OverviewCommandTarget`). `Shift+0` is Reset Trim, not a zoom.

No camera behaviour is derived from the zoom level. The walk's camera is the audio view's (`marker_walk_landing_frame`, read by the live and `h` Tab arms): in source view bare Tab centres its landing at the standing zoom (`MarkerLandingFrame::Center`), in target view it takes the least-movement landing (`Viewport::least_movement_scroll_if_needed`: onscreen moves nothing, offscreen lands the edge margin in). No Alt spelling of Tab binds. Bare Left/Right follow the edge unless the hold posture stands (`camera_hold`, read by `nudge_camera`), when they hold the subject's column (`Viewport::hold_subject_column_after_nudge`), never centring; Ctrl+Left/Right bind nothing. Only `c`, Shift+C, bare Tab in source view, the tab-comparing acts, the paired march, Shift+J and the A/B audition centre; drags, clicks, pans, undo, `0` and Ctrl+Tab leave the camera.

The top strip is six lanes (menu, icon, tab, trim bar, ruler, marker lane — no overview strip) plus one bottom row with the state cell right of its clock; the vertical stack has one owner in main.cpp, the waveform cap read only through `waveform_max_h_px()` (device key `max_waveform_height`, 0 = no maximum), waveform midpoint = window midpoint. Flexible gap 1 sits between the icon and tab rows (`kTopFlexGapLane`), gap 2 below the waveform; the menu row is `kMenuRowHeightPx` = 30; the view bar paints inside lane-tall rects via `view_bar_face_rect`; tabs are Breeze's geometry painted by `paint_tab_row` (details in kdenlive-redesign.md).

The navigation surface (`point_on_nav_surface`): plain drag = grab-pan; motionless click = playhead placement at the release; shift+drag = trim sweep (playhead sweep in `h`); ctrl+drag = zoom, right = in, at `nav_zoom_px_per_level()`. Every press-road length is scaled through `scaled_px` (drag gate, double-click slack, touch slop one number); durations and the pinch never scale. Plain wheel = stepped pan, except over a flag cell outside `h`, where it selects and steps that cell (`run_flag_cell_wheel`); modified wheels are swallowed; right/middle buttons bind nowhere; the trim bar ignores second fingers.

A warp flag paints `flag_display_text`, cutting only the scale; `flag_text` is the uncut editor seed. The plate renderer is aliased ARGB32 min/max over a peaks pyramid, one synchronous render per user-driven frame. Magnification is a step function over source time (`WaveformGainProfile`, built by `build_waveform_gain_profile` from the M sidecar: level 0 before the first enabled marker, disabled invisible, a coincident enabled run collapsing to 0 via `for_each_magnification_level_run`), its hash in the plate fingerprint, display-only and outside the render fingerprint. `effective_waveform_gain_profile` is the one gate: target view flat; source view the profile unless the session lamp `ignore_waveform_magnification` (bare `[`, dark at every open, no sidecar or undo) is lit; in target view the lamp greys and its key cards (`waveform_magnification_toggle_actionable`); `h` refuses it.

Two independent per-project camera postures (`camera_hold`, `camera_chase`), faceless, dark at open, outside undo, not carried by `'`. The hold is armed by an explicit centring alone (`c`, Shift+C, the source-view Tab landing, the paired march, Shift+J; never by arrival, undo, the audition or `0`), kept by the stepped zooms, every play and stop and the nudge itself, and cleared at the viewport chokepoint (`clamp_viewport_start`, which compares the settled camera with the last one it settled) by every other viewport write and by the three playhead movement owners. The chase is armed by Shift+C alone (at rest for the next project-audio launch, during play from the scanner), kept only by its own page-in and bare `c`, cleared by every other viewport write (the stepped zooms included) and a placement click during play, and spent by the play's end in the one stop body; the car's play reads it (`LaunchCamera`); the audition and render player ignore both. Shift+C rides the Center button's shift-click and long press. Bare `f` is unbound.

The backtick and 1/2/3 select S+M / S+W / T+P / T+W (`GuiActiveViews::select_active_markers_view`), the only road onto those axes; every view switch takes the synchronous rebuild. M exists in source view only, P in target view only: `switch_active_markers_view_to` refuses the wrong pairing, `switch_active_audio_view_to` lands on W before crossing, and T+M / S+P are load-fatal. M flags paint the orange level box, no cells, red = coincidence alone, and navigate as phase resets do; its ops cluster holds no `GuiTargetRender` and repaints the picture instead; grid iterations and the value pair refuse there. `active_marker_count` / `active_marker_time_frame` are the navigation readers' one store selector.

### Render Pipeline (→ render-pipeline.md)
The parser is the only compiler from authored data to the engine. Phase resets emit `S − N/2`, with no render-time offset anywhere, and geometry lives in `engine_geometry.h`. The lead-in `kPhaseResetLeadInSamples` = kN/2 (the engine is window-centred) is the one quantity read by the drop, Space's inverse launch, the band maximum and the phase-reset propagate's anchor.

**Phase-reset propagate.** The paste propagates the anchor, not the reset: the copy maps reset → target under `live_warp_frame_map`, adds N/2 and maps back (`phase_reset_anchor_frame`). It buckets by that anchor and keeps its unclamped section fraction (`ClipboardPlacement::anchor_source_frame`). The paste re-derives the reset through `phase_reset_frame_for_anchor`; with a refused map both legs are identities.

**Magnification-level propagate** (`magnification_level_propagate.{h,cpp}`, Ctrl+M / Ctrl+Alt+M) is a pure frame distance from the playhead:
- The copy stores `{offset, level, disabled}` for the selected M markers.
- The paste places each marker at `playhead + offset`, with no anchor, no guard and no prompt.
- Past the song end it pastes partially and cards `Stopped at …`.
- It pushes one undo entry and leaves the created markers selected.

The overlay band runs from the marker to the seed centre + N/2 (`phase_reset_seed_frame_index`).

**Trim and output.** Trim is a prepost stage: `plan_trim` (a refusal renders untrimmed) → trim-ignorant engine → `post_trim` → limiter → encode. The render path runs no subprocesses. Ctrl+Alt+R is two commands selected by the iteration-mode bit, and Ctrl+Alt+Shift+R is a no-op in iteration mode. Only the deliverable lives in `<project>/render/` (`render_output_directory`). `prune_render_folder` removes every other stem when the deliverable publishes; the CLI does not prune. Batch cells go in `<project>/tmp/` (`project_batch_root`).

**Dispatch and preview.** A dispatch kills the running render, and a killed session never publishes Success. The preview honours its generation counters: `complete_successful_buffer` discards a superseded Success, because no wrong audio outranks a stale picture. Identical renders are reused (`kFingerprintVersion`).

**BPM sweep.** The BPM sweep keeps the map's shape: `bpm_cell_warp_markers` rescales each enabled owner outside the span, and an out-of-bracket result refuses. A disabled marker is invisible to the sweep, and `iter_popup_eligible_marker` is the one eligibility (no bracket on a disabled owner). Ctrl+B refuses a selected run of differing tempos (`bpm_sweep_plan` owns the refusal ladder). The tempo step still steps disabled markers.

**Grid iterations.**
- **Column and view.** The mode is target-view only (bare `i` in source view crosses to target first, a refused entry stopping the press) and frozen to the column it was lit in (`iteration_column_lit`): the column switch, the switch to source view and the paired march refuse while lit; Ctrl+Tab stays live.
- **Hop brackets.** Phase resets carry session-only hop brackets in ±`kIterHopMax`. A ±k cell moves the reset k hops in the target domain (`phase_reset_hop_step_frame`, shared with the P arrow step).
- **Walls.** `phase_reset_hop_window` holds the walls, which are only the piece's edges and the digit. Ranges may cross, so each cell stable-sorts its resets.
- **Warp cells.** A warp cell appends its delta as a deviation term and never moves the base. `iteration_sweep_plan`'s `CellWouldNotLoad` refuses at the press any cell whose chain would not load.
- **Ties.** Markers may tie into one sweep axis (`iter_tie_group`, session-only, outside undo; marker-ui.md). Every member takes the leader's delta. `Ctrl+Shift+N` ties and unties on one verdict owner (it refuses a leader whose bracket leaves the members' intersection, `The range does not fit every selected marker`, and a non-leader carrying a bracket, `A selected marker already carries a range`; untying a follower takes it out alone, a leader dissolves the group; no undo entry, selection consumed). Followers are addressable and inert (`bound_cell_is_tie_follower` greys Up/Down and Edit Flag and cards `Tied to an earlier marker`); `Shift+J` on a follower goes to the leader on the other tab (`jump_tie_leader_destination`). The walls are the tightest member's (`iter_bound_tie_window` / `phase_iter_bound_tie_window`), read by the landings and both bound editors' commits; an axis folds only at a leader; the M column never ties.

**The lock.** While the lamp is lit, the bound cells are the only authoring surface. Any act that would push undo, and undo/redo themselves, refuse through `authoring_locked` / `iteration_lock_key_blocked`. Brackets live outside undo, and the mode's wipe (`wipe_iter_state`) is the only way they go. The lamp and read-only are mutually exclusive across the piece (`any_tab_read_only`).

### Colors (→ render.h palette block)
The palette is hard-coded, opaque and not user-settable. Every color is a constexpr from a sampled kdenlive/Breeze crop, so a retune is a recompile. The class ladder is DISABLED > RED > default, and every class, red included, brightens on the addressed cell alone: `kMarkerFlagFillRed` at rest, `kMarkerFlagFillRedSel` bright. The columns are warp purple, phase reset blue (`kPhaseResetFlag*`) and magnification orange (`kMarkerMagnification*`). The red cue is normalization or a shared frame within a store, disabled rows included; acts and faces read `collapsed`, never `red`. Disabled markers paint no stem. Marker-lane text is black (`kMarkerFlagLabel`; `kRedesignLabel` is every other row's ink), a disabled label blending at `kMarkerDisabledLabelMix` and every disabled shape at `kMarkerDisabledMix`; selected text is `kRedesignAccent` under `kRedesignLabel` on every text surface; the region highlight recolours the canvas ground under the ink. `accent_for_focus` swaps to `kRedesignAccentInactive` while the window is unfocused. Codex is skipped for simple color tuning.

### Settings (→ settings.md)
`app.engine_settings` is typed truth. All three boundaries are strict: a whole-file schema load shared by GUI and CLI, the editor's red flash, and typed dispatch. `kSettingsOrder` owns write order and `kEngineKeys` owns membership; adding an engine key takes six edits across four files. Every key is required; there is no load-side default, and an unknown key is fatal in both products with no migration. Each numeric value has one canonical spelling. GUI-kind keys commit through their gesture's chokepoint, history-less. A file, sidecar, render entry or checkpoint carrying `playback_speed`, `waveform_magnification_level`, `follow`, `centered` or `center_on_next_marker` is load-fatal (`kCanonicalSettingsKeys`). The view postures are per-project session bits (`camera_hold`, `camera_chase`): outside undo, not carried by `'`.

The six per-device keys are `gui_scale`, `max_waveform_height`, `projects_repo`, `projects_path`, `last_project` and `sync_path`. They live in one device config, `$XDG_CONFIG_HOME/warptempo_gui/config`, which must hold each key exactly once (`kDeviceConfigKeys`); a malformed file is fatal at startup. The first run writes it from `GuiPlatform::device_config_defaults`, and no template guesses a destination (empty `last_project` / `sync_path` on the tablet). `write_device_config` is the one writer (its call sites are inventoried there), and it returns a `GuiFailure` that callers card. Ctrl+S writes none of these keys. device_config.h owns the grammars:
- `is_config_path_value` governs both path keys.
- `is_last_project_name` is also the project membership rule. A violation is fatal; a vanished folder falls through to the first valid one.
- `is_gui_scale_percent` enforces [50, 350]. `gui_scale_factor()` is the one scale axis, applied live and installed before the first configure. There is no collision rule: past the tablet's 282 % fit ceiling, the rightmost history icons crop.

The read-only lock governs the engine keys, not the editor: `commit()` refuses them with `kTabReadOnlyCard`, while device and GUI-kind keys still commit. While grid iterations is lit, the editor refuses the following with `kIterationLockCard`:
- `tab_*_read_only=true`
- `active_markers_view=`
- `active_audio_view=S`

`active_markers_view` takes W / P / M. A typed `M` crosses to source first, and `active_audio_view=T` from S+M lands on W.

The bare Tab walk and its camera are the zoom digest's; under `'` and the lock the walk behaves as follows. While grid iterations is lit:
- The walk's unit is the cell (`marker_walk_step`: payload → lower → upper → next).
- A step within one marker collapses the selection and writes the addressed cell, without landing or framing.
- `marker_walk_actionable` greys the Walk buttons.
- The paired march is refused.

`'` loads a render entry's recipe in place: the markers and the engine block, one undo entry, with both sweep-mode bits left standing. Outside `h`, `'` opens the render player, whose Load in place calls `load_render_entry_in_place`. Inside `h`, it confirms a load of the viewed member.

### Projects (→ `src/gui/project_model.h` head prose, settings.md, platform-seam.md)
A project is a folder directly under `projects_path`, named by its folder. The source is defined by the sidecar stem alone (`resolve_project`): any of the four sidecars (`kSidecarExtensions`) names `<stem>.wav`, which must exist; a folder with no sidecar is a new project iff it holds exactly one `.wav`; anything else refuses with its reason. Once any sidecar exists all four are required (`sidecar_set_presence`: load fatal, dry run a card, nothing written); a new project's first open and every save write all four. Validity is folder shape plus a name `is_last_project_name` accepts, enforced at the membership (`enumerate_project_names`) and asked by the argument road too.

The app always has a project open: no argument → `last_project`, else the first valid folder in byte order, else "No project under <projects_path>" and exit 1; an argument must be a project's source; Android calls `gui_main(nullptr)`. `gui_main` is a loop: per-process objects outside, per-project objects in `run_project`; `gui.run()` returns quit or reopen (`GuiProjectOutcome`); a dirty tab prompts as Ctrl+Q does through one prompt body (`GuiCloseTarget`); a running render is killed.

File → Open Project (Ctrl+O, `is_open_project_key`, `h`-admitted) is the field-less picker, the folder overlay's second content: `build_project_picker_rows`, built at open, band starting on the current project, a Cancel-only modal row. Up/Down move the band; a row's motionless lift or Enter reaches `open_project_commit` — the current project a no-op that closes, else the strict `source_load_dry_run`, whose refusal cards and keeps the picker open. The picker is a `ModalDialogOwner` (prompt > player | picker | stats > editor; the list owners never stand together, none beside an editor) with its own veil, router `route_picker_key` (Ctrl+S consumed as save, Ctrl+Q falls through) and close body `close_picker`. File → Revert (Ctrl+Alt+O, `is_revert_project_key`) reopens the current project through `GuiCloseTarget::Revert` past the picker's refusals: clean reloads silently, dirty asks "Discard unsaved changes and reload?" on OK/Cancel with OK focused; legal read-only, under the iteration lock and in `h`.

Synchronize (bare `\` and its File row, both onto one gated body) mirrors `render/`'s top-level wavs and each `tmp/` batch folder's wavs (`list_wav_files`) onto `<sync_path>/<project>/`, copies staged then renamed, deletions only against a finished destination classification; an empty `sync_path` cards `sync_path is not set`; the act never creates its sync root; first failure cards the path, success is silent; an unsure mirror deletes nothing (the five rules at external_sync.h's head). It runs on `GuiExternalSyncWorker`, read-only-legal, no playback stop. While running, row 8 shows `Synchronizing...` derived in `process_line_text` from `is_busy` beneath any render line, and every close road (quit, reopen, Revert, BACK, compositor X) is refused at `GuiPrompt::request_close`'s head (`close_refused_by_external_sync`, `kSyncRunning`) since `shutdown` cannot cancel. The tablet's `sync_path` stays empty (the OTG stick is not a configurable path); an SAF road needs an architect ruling.

### The Folder Overlay and the Render Player (→ render-player.md)
The folder overlay (`folder_overlay`, `AppState::FolderOverlay`) is the keyboard-slot list panel with three contents under one owner tag — the render player, the project picker and the AV Sync Stats panel — whose standing predicate `folder_overlay_stands` every painter, router and gate asks; only the open act, row pitch and row face fork on the owner. The band (`keyboard_slot_band`) stands fixed from under the icon row's border to the bottom row, no line of its own. Above it File is live and the other anchors, view bar and every icon are dead (`menu_anchor_dead_in_mode`, one partition shared with `h`); the veil consumes presses outside band, row and File. Under all three contents File's Quit and Synchronize work, Open Project and Revert are silent no-ops. Listings are sans, the stats panel monospace (`folder_overlay::text_listing` forks face and pitch); a row is exactly an icon-row button, long names run off the edge; `Text` rows are inert but the band still scrolls. Listings are built at entry, never kept fresh, one scroll (`clamp_scroll`), one walk `for_each_row`.

The player opens on bare `l`, bare `'` outside `h`, and the Play renders button (its shift press is `Shift+L`, the stats panel); the open refuses while a render runs and enters the newest batch folder (`max_renders_batch_index`), else the `tmp/` root. `Folder` is `{Root, Batch}`. A row's motionless lift highlights then opens; Enter opens the highlight, Up/Down walk without opening; no double-click or multi-selection. `transport` stores Idle/Live/Paused; `play_button_act` is highlight-driven (`render_player_highlight_act_row`): a folder opens, another wav plays, the item's own row toggles (`transport_toggle_act`). The band leaving a paused item's row resets its resume point (`abandon_paused_resume_if_band_left_item`); the band follows the item on its own changes (`play_wav`). Up (button, `Backspace`, the car's Previous) leaves the folder and unloads the item through `unload_item` (`UnloadTail`), seating the band on the folder just left (`rebuild_rows`'s seat rule); greyed at the root. Home is `home()` (previous file within `kPlayerPreviousThresholdMs`, position-based, else restart); End / the right skip is `next_track()` (next wav in the item's folder, outranking Repeat One, walled at the last wav); Shift+Home/End go to the folder's ends; neither skip ever leaves a folder; auto-advance shares the walk; no wrap.

The bottom row is the player's modal: Home · Play/Pause · End | scrub · clock | Repeat One · Up · Load in place · Close (flush right), borderless glyph buttons with their own disabled face (`render_player_button_enabled`); the skips admit shift-click/long press (`player_button_shift_admits`). The scrub is a Breeze slider: track press seeks at the press, handle drag seeks at release, rests while idle. Alignment: a modal with a field or message is flush left, a list's buttons flush right (`paint_modal_dialog`). Repeat One is lit at every open and is the one looping exception; with it off a folder-end rest resumes the last item on Play. `route_render_player_key` is the whole vocabulary; only Ctrl+S and Ctrl+Q fall through. Loads use the frozen wav reader: probe, require rate/channel equality with the project source (refuse, never convert), ceiling, read, re-check equality on the decoded buffer, fence, `rebind_buffer`; the player has its own launch body beside `launch_playback_window` and shares the one stop body. Close rebinds the view's buffer before freeing the item's. Load in place is its button or `'` on a highlighted batch cell, behind `confirm_load_in_place`'s OK/Cancel. Every confirmation of an already-requested act opens on its first button (`PromptInitialFocus::FirstButton`, required at `PromptState::present`); three-way Save/Discard/Cancel prompts keep the last. The stream is started once at open and stopped only before closing; rest never suspends it. Not built: notification, service, background or lock-screen transport.

The car's MediaSession (mechanism in platform-seam.md; `set_on_media_command` / `publish_media_state`, no-ops on Wayland) runs the player's own bodies, never synthesized keys. With the player open: Pause/PlayPause → `car_toggle` (Live → `toggle_pause`, else `play_button_act`); Play only reopens the stream (`ensure_device_available_for_play`) and starts nothing, on both car owners; `car_previous` / `car_next` walk a row at rest (Previous at row 0 goes up a folder, Next walls) and while live take the previous-track window / next track; Stop pauses and seeks 0; focus loss toggles a live transport; FF/RW seek ±5 s. Car commands act under a pointer drag; a dead device pauses rather than advancing (`device_unavailable`). The head unit's display is a dummy: `session_active` means PLAYING whenever the player stands, `playing` feeds audio focus only. `publish_media_state` sends album = project; live: title = file name, artist = the item's own folder, with duration and position; otherwise a metadata-only silence track: artist = the band's folder (`tmp` at root), title = the highlighted row's bare name, position 0, `duration_ms` −1. Band moves publish while not live. The tablet's own UI stays truthful. Do not build the fast-forward-means-pause contingency unless asked.

With the player closed `GuiCarTransport` owns the head unit (main.cpp forks on `render_player.active`). Pause/PlayPause are the car's Space via `car_toggle_playback` (stop, or loop the active domain's trim forever from its begin, camera left in place unless the chase posture stands, `LaunchCamera`); pause is a stop; Play reopens the stream only. Previous/Next are undo/redo (`run_undo_redo_without_key`, all of Ctrl+Z's gates and cards) and then play the stepped state (`car_play_after_step` → `car_play_playback`); a refused step plays nothing. In target view with the preview not ready the play arms a one-shot `PendingCarPlay` fired by `tick()` when the preview settles, cleared by any view/tab/title change, other sound, the player, or any console command. The play meets Space's gates (`car_play_refused_by_key_gates`). The session lives for the app's life except it is released in `onStop` while the screen is on and rebuilt in `onStart` (rule in platform-seam.md, mechanism at `MainActivity.java`). Lines: album = project, artist = `A) T+W` (tab letter + `view_pair_label`), title = `car_transport_title_line`'s `<index>_<signed distance from save>` (`?` with no save). Duration is the trim window while live, −1 at rest; position is the loop clock re-pushed at each wrap; all published by a per-tick comparator (`GuiCarTransport::tick`).

### Load-Lenient, Render-Normalizing Boundaries (→ normalization-and-boundaries.md)
A pass inherits the literal cents and scale of the nearest prior ENABLED owner, walking past passes, disabled markers and label refs (`resolve_inherited_tempo`); Shift+J's source and bare `s` after a ref (`drop_copy_previous_at_playhead`) walk the same way. Gestures move freely and nothing pops: ambiguous states normalize at the render boundary (`resolve_warp_markers_for_render`, red flags the cue). THE TWO-CATEGORY RULE: a state the GUI can commit always loads and renders; a state it can never produce is adversarial and hard-fails the load, first error only, identically in both binaries — and for the repository, out-of-app git under `projects/`, a detached HEAD or a hung hook get a blunt terminal error, no in-app recovery.

Trim is always set: a full ordered pair rests, a crossed pair or a coincident release resets to the whole song (`auto_clear_crossed_trim`), the endcap drag clamps inclusively at its partner, and the full window means "unset" (`trim_window_is_full`). Every trim-bound writer clears the selection and parks the playhead at the new trim start (membership at input_trim.cpp's header). The sweep enforces no width; honorability is `validate_trim_frames`'s at the render boundary, whose refusal renders untrimmed. Trim has no undo; `Shift+0` Reset Trim (`is_trim_maximize_key`) is the recovery.

Nothing loops except the render player's Repeat One and the car's loop of the trim on the project transport (`GuiPlaybackLifecycle::car_toggle_playback`, ended by any GUI stop or relaunch; the engine's `play_loop` wraps on the audio thread via the packet's `loop_begin`, `kPlaybackNoLoop` on every GUI road, the wrap a resync the tick consumes through `consume_loop_wrap`). `Viewport::trim_range` owns the navigation range and the car's loop window.

An undo entry records the view the act LANDED in — A/B tab, W/P column, S/T audio view (`UndoEntry::audio_view`; the phase pastes restamp through `Undo::stamp_top_entry_with_landing_view`) — and the restore writes tab, then data with its map-change re-land, then column, then audio view, each through its chokepoint (`switch_active_tab_view_to`, `switch_active_markers_view_to`, `switch_active_audio_view_to`); selection rules run only on the column reached; no restore synthesizes a view the user was never in. The restore's camera answers to the restored markers, never the playhead: the map-change re-land translates without scrolling (`Viewport::translate_playhead_to`); one restored marker is centred at the current zoom; several centre their range's middle and zoom OUT only when range plus edge margin cannot fit, never in (`center_span_in_view` over `frame_span_into_view`); a restore leaving no selection moves no camera.

Undo coalescing: a held key's or button's repeats coalesce behind an opener (the physical key press; the button's first fire); physical taps within `kTapCoalesceMs` (which reads `kHoldBeatMs`) merge on a standing subject, `record_gesture` stamping only on the accepted path; a save ends the tap window (`Undo::note_saved`); a merged burst byte-equal to the live store pops its entry (`pop_undo_top_with_saved_ref`).

### Interaction Model (→ selection-model.md, marker-ui.md, region-scrub-esc.md, pointer-hit-testing.md)
**Buttons.** Every roster button is truthful: it greys whenever it would be a no-op, its face reading the predicate the act's own refusal reads, never restating it (inventory at `redesign_button_enabled`), repainted by the per-tick comparator. The twin rule: a button with a live modified twin greys only when every admitted variant would change nothing. A greyed press raises no card; the key's refusal does. The three launch gates reopen a dead stream at the press (`ensure_device_available_for_play()`), carding only on failure; launch faces read `GuiPlayback::device_absent()` alone, `device_unavailable()` being the render player tick's read.

**Selection and lamps.** A plain marker click lands the playhead at the press (`run_marker_click_act`, only the plain arm arming `PendingMarkerPress`); shift = inclusive range from anchor/focus, ctrl = membership toggle, both flag-box-only, landing on their focus and arming nothing; on a Lower/Upper cell ctrl and shift are silent no-ops and a plain press single-selects. Bare `k` (Add to Selection) is the sticky ctrl for glass on the PAYLOAD box only (a bound-cell press stays plain), shift beating it; it stays lit until `k` or an act consumes the selection through the one writer `selection_consumed` — the tempo step, Delete and Ctrl+D, Ctrl+N, Ctrl+P, the pastes, `enter_bpm_mode`, `i`'s on edge (not the nudge, `j`, the drop, editors or the walk); a refused act consumes nothing. Lamps are per-project, dark at open, untouched by load in place; they resolve by use case, no toggle clears another and no lamp card exists beyond `kIterationLockCard`. `m` (Ctrl+B) under lit `i` refuses on the lock's sentence; BPM mode is its dialog session (`enter_bpm_mode`/`exit_bpm_mode`). Bound cells are authored one marker at a time: every road into a bound axis single-selects (no group bound step); the tempo group step stays. The selection is never parked: `p`/Ctrl+Tab clear it; coincidence auto-selects at the four entry chokepoints (`auto_select_marker_at_playhead`). A selection's cue is its members' brightened flags; the playhead has one form and always paints.

**Value drag.** `value_drag_posture` derives whether a plain flag press-drag is the value drag: target view on W always, target view on P only under lit grid iterations, never in source view or `h`; where yes, the horizontal marker drag is off on every flag (never multi-axis). Targets are `value_drag_target`: a warp payload the arrows would step (a pass seeded via `warp_tempo_step_start` and frozen to owning on first motion; kind refusals by `tempo_cent_step_kind_refusal_for` — label refs, and coincident-collapse members in target view; disabled markers ARE targets; refused under `authoring_locked`), or a bound cell wherever `marker_paints_iter_cells` on an unlocked tab; everything else arms nothing silently. One step per `kValueDragPxPerStep` of vertical travel, up = increase, truncated, each write a delta through the arrows' landing owners (`tempo_cent_step_landing`, `iter_bound_step_landing`, `phase_iter_bound_step_landing`). `ValueDragOps` writes the live store per motion with no undo, render or re-land, the displayed basis frozen (`displayed_basis_frozen`); the commit pushes one undo entry iff the tempo changed and runs `warp_tempo_write_tail`; a bound drag commits nothing. No cancel; the playhead never moves. A tempo step moves the deviation chain's LAST term where a chain exists, else the base (`warp_tempo_step_move` / `warp_tempo_step_write`, read by singleton, group, drag and the Up/Down face, which greys on a zero delta); Up/Down greys via `tempo_cent_step_direction_actionable`. The target-view tempo-image drag stays deleted.

**Flags, cells, editors.** Stems are pointer-inert; the flag box is the marker's one pointer surface. No marker carries a measure or a magnification; bare `/`, Ctrl+/ and Shift+/ are unbound. Under lit grid iterations every sweep-read marker on the lit column extends its flag with Lower then Upper cells, painted as mini flags in the column's own hue, carrying the signed bound (two-decimal cents; one-digit hops on P); hits answer `MarkerCell { Payload, Lower, Upper }` via `hit_test_flag_cell`, and an open field's riding boxes publish their own `FlagHitRect`s asked first by `topmost_flag_rect`. Each cell has its own editor on double-click: the flag editor (no bracket grammar), the bound editor (`enter_iter_bound_edit`, one kind with a grammar bit; refuses the partner bound and the column window, `phase_reset_hop_window` on P, on `Range bound rejected`; empty clears the bracket; writes at `iter_bound_step_write` / `phase_iter_bound_step_write`), and the M column's one-digit level editor. The three marker-lane editors share one graphic model: the edited box is suppressed (`SuppressedBox`, keyed in the flag cache), the field is its content's width, boxes right of it ride its edge, boxes left stand at rest; the caret column borrows the right pad (`render_flag_editor_box`); no field is clamped on-window — the window cuts it off and panning reads it, the editors being pointer- and wheel-transparent. The addressed cell is the bright one, lit on the field while one stands: `AppState::addressed_cell`, written by a press's cell, the cell editors' opens and the Tab walk's cell step (`write_addressed_cell`), reset to Payload by every other focus write (`Selection::seat_focus`) and by `wipe_iter_state`. It names what Up/Down step (the tempo, or `adjust_iter_bound_cents` / `adjust_iter_bound_hops`: singleton, silent clamp, [0,0] clears, no render, no undo) and what Enter opens (`flag_editor_open_actionable`).

**Pastes.** The phase-reset paste pair refuses a multi-marker selection on `Select exactly one marker to paste onto`; the magnification paste asks the selection nothing; every copy and paste consumes the selection.

**M column** (`GuiMagnificationLevelMarkersOps`, holding no `GuiTargetRender`; each body kicks the picture via `waveform_gain_hash()` / `kick_waveform_sync_if_gain_changed`): Ctrl+Shift+S and bare `s` in S+M drop a marker at the level in force (`magnification_level_in_force`); the horizontal drag (no value drag) moves sections live through `waveform_gain_profile_drag_cached`; Left/Right nudge (`finish_position_nudge` with a null render); Delete, Ctrl+D; the level editor on Return/double-click (`MagnificationLevelText`, empty commit refused); the level step on Up/Down and the plain wheel, singleton silent-clamp and group all-or-nothing (`magnification_level_step_group_actionable`), an enabled member of a collapsed coincident run refused on `That marker shares its frame with another` (`magnification_level_step_kind_refusal_for`), one undo entry per burst, selection consumed. Bare `v` reverts M diff flags; `j`/Shift+J, Ctrl+B and the propagates refuse there.

**Trim region.** The region is the trim: `RegionState` is one visibility bit, the overlay derived per frame (`trim_overlay_span`), raised at the sweep's first write (`show_trim_region_overlay`) and hidden at its commit (`commit_region_sweep`); the sweep writes the trim per motion, the `h` view's shift former is a playhead sweep, every former drops its surface's selection and carries the playhead on its moving end. The trim bar is trim's pointer surface; there is no show/hide key and no waveform endcap drag. Movement owners (`Viewport::move_playhead_to`, `land_playhead_on_marker`, `land_playhead_on_source_frame`) are distinct from named non-movement entries (`reseat_playhead_to`, `reseat_playhead_on_marker`), never a flag; rule at `move_playhead_to`.

**Cursors.** Named XCursor-theme cursors only, one zone map derived from the press routers (`pointer_cursor_kind`), resolved once per loop iteration; the cursor promises the gesture.

**Locks.** Read-only protects authored musical content only; trim, `Shift+0`, Ctrl+S and both render chords stay legal (`read_only_key_blocked`). The iteration lock is its second reason (`authoring_locked`): `iteration_lock_key_blocked` is read-only's list minus the column switch, the Ctrl+Shift+Tab march (carded) and bare `o`, plus bare `i`, bound-axis Up/Down/Return, Ctrl+Z/Ctrl+Shift+Z (carding) and Ctrl+P; `h` refuses at entry. `iteration_lock_greys` feeds the faces alone; the view bar's selectors are dead under the lock with a disabled face (unselected labels at `kRedesignDisabledMix`), distinct from its legible unfocused face.

**Transport.** Space plays from the playhead. Shift+Space is the A/B audition: other tab, `kAuditionMs` from its playhead twice, back, same span twice — four bounded plays via `launch_playback_window`, each half opening with `run_center_command`, paced by `kAuditionPairGapMs` / `kAuditionSwitchGapMs`, sequenced off the loop's deadline tick (`GuiAbAudition::fire_if_due`), moving no resting playhead; `phase != Idle` is the refusal test; one transport session that bare Space stops; ended when the playhead's musical position changes (four owners at `GuiAuditionSequence`) or when the device is lost (carding `kPlaybackDeviceUnavailableCard`); read-only-legal. Home/End jump to the trim bounds, Ctrl+Home/End to frame 0 / the last frame trim-blind (`playhead_skip_landing_frame`), the skip buttons' ctrl-click being that chord (`redesign_button_ctrl_admits`). The keyboard stop rule is at `stop_playback_if_playing`.

**Movement and gestures.** Horizontal movement is a focus act: a group press collapses to its focus and lands, then the singleton step clamps at walls (position_nudge.h); bare arrows nudge the focused marker in its home view; every focus change lands the playhead. Hit geometry rides the displayed basis (`plate_viewport_basis` / `item_viewport_basis`); damage follows the basis of the pixels it erases. Pointer gestures have no cancel: Esc mid-gesture is a consumed no-op, any end commits.

**Restrict Undo to Current View** (bare `z`, `IconRestrictUndo`): session bit `restrict_undo_to_current_view`, off at launch, in no vocabulary; while lit, an undo/redo whose entry's landing tags (tab, audio view, column on a marker entry) differ from the current view is a carded no-op (`That undo would switch the view`) leaving both stacks untouched, one verdict `undo_restore_stays_in_current_view` behind `undo_step_permitted_by_current_view_lamp`, read by the refusal and both faces; read-only-legal, `h`-refused. Bare Esc is bound in nine places and nowhere else (enumeration at input_handler.cpp).

### The Home-View Binding (→ tempo-and-home-view.md)
`active_column_authoring_allowed` (inventory at the predicate, app_state.h): W authors in source view only; P and M always (each exists in one view). It gates the positional family in T+W — `s` drop, flag drag, nudge, BPM open — keys carded, pointer silent; W+target has no pointer gesture that moves anything. The singleton tempo step freezes a pass to owning in both views, refusing only a label ref and a coincident-collapse member; the group step walls a pass. Ctrl+D / Delete need a selection (`marker_selection_verb_actionable`). Shift+S crosses to T+P and runs the lead-in drop (refusing whole in P; the Drop button never greys past the lock, by the twin rule); Ctrl+Shift+S crosses to S+M and drops with no lead-in, no button road. A 'P' undo entry restores its audio view first; the drop's source-view arm is an assert.

### Modality and Input (→ conventions.md, touch.md)
Icons act at the lift and hotkeys at the press (`GuiInputHandler::on_key`); the two never disagree. Enter/Space on a focused modal button operates an icon. Bare `e` is the left mouse button, translated at the platform boundary; never bind it in dispatch. Content acts once its identity is certain: at the press, or at a motionless release where the same press arms a drag. conventions.md holds the certainty audit.

While a prompt, dialog editor, render player, picker or AV Sync Stats panel stands, the bottom row yields to `paint_modal_dialog`. Modal buttons act at the lift, and a prompt answers only after it has painted. Published geometry only selects; live state decides (`ModalDialogGeometry`). The veil consumes every press outside the surface. All six editor kinds admit their own keys plus Esc, Ctrl+S and Ctrl+Q through `route_modal_editor_key`. Each grammar field's byte cap is its widest spelling, derived in `text_editor.h`; settings and commit title have declared policy caps. The flag editor is keyboard-modal, transparent to pointer and wheel, and never stops playback.

Text fields:
- A mouse drag sweeps a selection (`EditorTextDragState`).
- A finger drag moves the caret only, through `set_editor_caret_from_x`, and the stream is keyed to its editing session. The field spends no hold, and its window never expires (`kTouchWindowNoExpiry`).
- A double press that goes straight into a drag extends by words (`extend_selection_by_words`).
- The clipboard is the system clipboard. Ctrl+C is unbound except in the editors and the stats panel.

Modifiers:
- An unbound modifier combination is a no-op. The three deliberate exceptions are `is_tab_cycle`, Ctrl+Z's shift bit, and the editors' motion arm.
- Super is dropped at the platform boundary.
- Alt binds only the seven Ctrl+Alt chords (the two renders, Ctrl+Alt+P / Ctrl+Alt+Shift+P / Ctrl+Alt+M, Ctrl+Alt+O, Ctrl+Alt+Shift+M) and nothing else.
- Bare `\` is Synchronize.

Touch is the pointer:
- One finger passes a disambiguation window (60 ms/8 px, `kTouchRegionHoldMs` in the pan zone).
- Two fingers are pinch zoom only.
- A touch the system takes away is a click only if it never moved.
- There are three divergences: the phone-model pan, the region hold, and the field caret drag.
- The pan zone yields to pointer claims (`touch_point_in_pan_zone`).

Every chrome button acts at the lift (`AppState::ChromePress`), except the menu anchors, which act at the press. The hold-repeating buttons are the arrows and Undo/Redo (`kToolbarChords`' `repeats`). A held repeat outranks the long-press shift. A wall no-op runs before the coalesce stamp, and a held repeat on an empty stack is silent. Every hold threshold reads `kHoldBeatMs` = 575, whose readers are inventoried at gui_input.h. Wayland's key delay comes from `repeat_info` and is never hard-coded. Every shift-capable gesture whose bare form has a button admits that button's shift-click and long press, per `redesign_button_shift_admits`.

The arrow ladder is vertical only: Up/Down steps are 1 bare, 3 with Ctrl and 10 with Shift (`arrow_step_magnitude`). Left/Right take no rung, and Shift+Left/Right are silent. Ctrl+Left/Right bind nothing. The bare horizontal unit is one column on W/M and one hop on P (`horizontal_arrow_step`).

Menus and commands:
- The menu row is File / Edit / Settings over one popup state. A command with an icon-row road does not also appear in the menu row.
- Under every overlay, only File stays live, per `menu_anchor_dead_in_mode`. Its Quit and Synchronize work; Open and Revert are silent.
- Each menu item is its chord, hover switches menus, and items never grey.
- `Shift+L` toggles the AV Sync Stats panel; bare `l` opens the render player.
- The Edit menu is the propagates' only home: the three magnification items, a separator, then the three phase-reset items.
- Generate Magnification Level Markers (Ctrl+Alt+Shift+M) runs `detect_magnification_levels` over the full song. It replaces the M markers inside the trim, behind an OK/Cancel prompt with the counts, in one undo entry, and leaves the new markers selected.

Text follows kdenlive's conventions, owned by the capitalization block in paint_handler.cpp. Key names use Qt's spelling. Names (menu items, word buttons, titles, glyph tooltips) are Title Case. Descriptions (labels, second tooltip lines, cards, prompts, the state cell) are sentence case.

A tooltip is the act's name and nothing else (`redesign_button_tooltip`). The grey carries the message and the key's card carries the reason. A modifier line appears only where the modified press differs in that state. Appended reasons are lowercase (`lowercase_initial`). A card is one clause and a prompt is a question; names are single-quoted.

Chords:
- Ctrl+Shift+Tab is always the paired march, refused under the lock and hold-repeating. Ctrl+Tab is one-shot.
- The backtick and 1/2/3 select views absolutely; 4..9 are unbound.
- Shift+S drops a phase reset from any view (`drop_phase_reset_in_target_view`: T, then P, then the lead-in drop).
- Ctrl+Shift+S is its M twin: S, then M, then the drop, with no lead-in and no button.

### Messaging (→ messaging.md)
Every message is either state or an event.

State lives in row 8's monospace state cell, right of the clock. The clock is never clipped; the state clips without an ellipsis. Its two strings are the process line and the `h` walk line, and the walk line wins while `h` stands. The state never times out and no key clears it.
- A sweep writes `Rendering k of M <label>...` into `queue_progress_text` at each dispatch, and only the terminal retracts it (`finalize_render_run`).
- A single render parks `Rendering...` only while it synthesizes.
- The dirty mark is one asterisk after the clock, the one dirty indicator, read from `app.dirty`.
- The row yields whole to a modal, so a refusal under a modal goes on a card.
- Damage is `Viewport::invalidate_status_cell_area`.
- No state surface shows a resolved value. Bare `j` copies one and cards it; `Shift+J` jumps to its source on the other tab (`resolved_marker_payload`).

Events are cards in a top-right stack. Normal cards last `kNotificationMs` (5 s), and hover pauses them. Critical cards (checkpoint failures) never time out and are never bumped. The X dismisses one card; Esc clears them all. A card wraps to three lines, and the stack is uncapped: a push past `notification_capacity` bumps the oldest normal card. The width ceiling is `kNotificationMaxWidthPx`, scaled, with a 272 px floor. A card shows a path by basename through `GuiFailure {diagnostic, display}` and never parses English.

Every consumed no-op cards its sentence. The head gates fire only for chords bound in the standing mode (`chord_is_bound`), spelled by `spell_chord`. Refusals from freeze-adjacent ops go through `card_op_refusal`. Each deliberate press yields one card at the outermost site, and a held repeat refreshes its card (`HeldRepeatDispatchScope`). The following stay silent:
- bare Esc and defensive arms
- a greyed button's lift
- the target-view entry gate, which writes stderr only
- a benign refusal already at its state
- a refusal whose reason row 8 already shows
- an unbound key

A success is silent when the screen shows it truthfully. It cards when nothing shows it or when the screen would mislead: clipboard successes, and pastes that produced nothing. A render's success or cancellation is not notified. A failed archival render cards and deletes the stale deliverable pair. A failed batch cell removes its partial files. A short sweep cards "Rendered N of M". The trim fallback cards `kTrimFallbackCard` (`compute_buffer_start_frame_for`).

### The GitHub Recheck (→ github-recheck.md)
Bare `h` toggles the read-only history view, which paints only the viewed checkpoint's delta. `,` and `.` walk the whole eligible history, prefetched in the background (`GuiHistoryPrefetch`) and load-gated (`load_commit_sidecars_strict`). When git cannot be asked, the view opens on the local walk. The stored predicate `history_remote_walk_available` decides without re-running git; in that case `g` cards and Save greys.

If `<project>/history/` exists, the walk reads the exported `history/<seq>_<sha7>/` members instead of git (contract in `history_folder.h`). Save and Commit is refused on that road. `wts_history` generates it; the laptop never has one.

Walks and compare:
- The two readings are iterative (the default) and cumulative, toggled by bare `u` (`history_cumulative`).
- The walk lamp is `HistoryWalk` on bare `g`, switched through `set_history_delta`.
- The local walk freezes both undo stacks while the view stands.
- Displayed numbers count upward (`HistoryMode::member_number`).
- Ctrl+Tab switches tabs, and Ctrl+Shift+Tab is the diff-flag march.

Acts in the view:
- `'` confirms a load of the viewed member, as one undo entry, and closes the view.
- Bare `v` reverts the selected diff flags, forcing, in one undo entry, then closes the view. It refuses if `label_def_taken` finds a label defined twice.
- Ctrl+S is Save and Commit on a single background worker. Every verdict is git's exit status, and the one read is `status --porcelain --branch` (`header_says_publication_owed`). Failures card as critical, with no retry.
- Git is local only: `run_git_capture` reads, and `run_git_mutate` is the one fenced writer. The `projects_repo` guard runs at init and before the push.

The view owns no navigation state. The trim bar shows the real trim, and its double-click runs `run_span_framing_command`. The view has no playback.

### Validation Topology
Every guard classifies under docs/engineering/validation_topology.md: five classes, one owner each. A duplicate predicate needs an (a)/(b)/(c) justification. An error arm exists iff a producer exists. A guard that fits no class is a design smell.

### Type Names / Naming Symmetry
Marker and related GUI types carry the `Gui` prefix. Warp and phase reset are co-equal axes: new pipeline surfaces ship both columns or record the asymmetry at the site. "Phase reset" is one token, never "reset" alone. Warp is never the unmarked default (inventory in conventions.md).

### Rounding
`std::nearbyint` (banker's) wherever values convert to grid points (frames, lattice columns, samples); no epsilon nudge; `snap_authored_frame` is the only authored-position conversion. Screen pixels are cells: every pointer, touch and capture coordinate floors through `containing_pixel` (input_core.h). Chrome labels in a box and both clocks centre the cap band via `redesign_baseline`; a text line takes `line_baseline`; the marker lane keeps its crop-authored `marker_flag_baseline_px`. The waveform lattice stays points.

### Comments
Comments describe current behavior and rationale only. No dead-process citations (Brief/chunk/Stage/spec tags or "per the brief" prose).

### Grep Before Asserting an Invariant
Before claiming "every site that does X" or "no other caller does Y", grep it, and re-grep at every retell rather than editing an inherited list. An exhaustive enumeration lives at exactly one authoritative site per concept; every other site states its own class plus a pointer.

