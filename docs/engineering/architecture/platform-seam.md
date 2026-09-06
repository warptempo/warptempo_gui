# The platform seam — Linux/Wayland and Android over one portable GUI

Authoritative record of the Android port's architecture (landed
2026-08-26, commits `b6b37c60`..`b2086fca`; the arc's decision log is the
planner's local tmp/android_port_scoping.md, not committed). Read this
file before touching `platform_*.{h,cpp}`, `input_core.{h,cpp}`,
`playback*.{h,cpp}`, `gui_font*`, `gui_main.h`, the root CMakeLists'
Android branch, or anything under `android/`.

## The three layers

The seam does not run between files; it ran through one class. `GuiPlatform`
held (A) backend mechanics, (B) portable input policy and (C) the run-loop
contract. The port split them:

- **A — the backend**, one per platform, same class name and IDENTICAL
  public API (77 declarations as of 2026-09-03 — 13 `using` aliases and 64
  members including the constructor and destructor — counted over the
  `public:` section of each header with `//` comments and blank lines
  stripped: the semicolon-terminated declarations, plus the three one-line
  inline accessors that end in `}` rather than `;`, which the count has
  always included and which the words "semicolon-terminated" alone used to
  leave out. The identity is proved by diffing those two stripped sections,
  which come out line-for-line equal — 90 lines each at this writing, the
  count being 77 because three declarations wrap (`set_touch_nav_hooks`'s
  twelve lines since the caret trio and the editor-field query joined it on
  2026-09-05, and one wrapped line each in `set_history_prefetch_completion_fd`
  and `synthesize_key`). The
  count FELL BY ONE on 2026-08-30, `removable_volume` retiring with the
  mirror's discovery — Synchronize is told its destination by the device
  config's `sync_path` now, so neither backend goes looking for a volume —
  ROSE BY ONE on 2026-09-02 with `display_lead_ns`, FELL BY ONE AGAIN on
  2026-09-03 when that member went with the playback leads (the Playback seam
  below), and ROSE BY TWO the same day with the AV sync panel's pair,
  `set_display_measurement` and `display_stats` (the Display measurement seam
  below). Re-derive it; never decrement the number you find.
  There is NO Android-only member any more — the on-screen keyboard's
  two, `wants_onscreen_keyboard` and `synthesize_key`, are declared on both and
  answered differently, which is the seam's own shape rather than an
  exception, `device_config_defaults` is a third of the same kind,
  and the car's two (`set_on_media_command`, stored and never fired on
  Wayland — the STORED-HOOK shape `set_on_close` carried on the Android side
  until BACK became its producer there on 2026-08-29, leaving this the seam's
  one hook a backend never fires — and `publish_media_state`, a no-op body
  there — The car, below) are the fourth and fifth):
  `platform_wayland.{h,cpp}`
  (Wayland/xkb/cursor/shm/clipboard/pointer-lock, keymap
  → `GuiKey`; the protocol classes — five REQUIRED globals, every `wl_output`
  best-effort, and exactly THREE OPTIONAL protocols, pointer-constraints,
  relative-pointer and `wp_presentation` — are stated once at
  `platform_wayland.h`'s globals block, which also carries the third one's
  succession: it was the DISPLAY LEAD's instrument 2026-09-02 to 2026-09-03,
  went with the playback leads that day, and came back the same day under the
  AV sync panel's gate — BOUND at init and asked for nothing while the panel
  is down. EVERY `wl_output` BINDS AT v4 SINCE 2026-09-03 (it was v2): v4's
  `name` event is the connector string the panel prints, and the version is
  kept per record so the teardown can use v3's `release` in place of
  `destroy` — an older compositor binds at what it offers and reports its
  make and model instead) and
  `platform_android.{h,cpp}` (NativeActivity glue, ANativeWindow present,
  AInputQueue → the core, stubs). `platform.h` is the ONE include that
  selects the header; every consumer includes it and changes nothing (NINE translation units and headers today, re-grepped 2026-08-29 — the number is a consequence of the seam, not a fact about it, so nothing here keeps a list).
- **B — `GuiInputCore` (`input_core.{h,cpp}`)**, the product's platform-neutral
  input policy, existing ONCE: the touch translation state machine (the
  disambiguation window, the region hold, the two-finger nav frames, the
  thin-lane answers, the hard-end contract), key-repeat synthesis and the
  bare-`e` left-click emulation, modifier and logical-pointer state, the
  notional-x / capture bookkeeping, the pointer-frame and deferred-motion
  scratch, and every consumer hook those bodies fire. A backend hands it
  PLAIN VALUES — window-pixel doubles, bools, ids, X11 keysyms — and installs
  ONE downward probe (the codepoint re-fill per synthesized repeat). Every
  moved body is line-for-line the pre-move Wayland body (verified by
  diffing the extracted ranges); the backend forwards its public API to the
  core. Comments in the core speak the core's event names
  (`touch_frame`, `touch_cancel`, `pointer_leave`, `set_modifiers`,
  `set_repeat_info`), each Wayland instance named once in parentheses;
  provenance sentences (measured compositor behavior, labwc's repeat delay)
  stay verbatim.
- **C — the loop contract**: ONE periodic timerfd at half the refresh
  period — on Wayland the refresh of THE OUTPUT THE WINDOW IS ON, every
  `wl_output` bound and `wl_surface.enter`/`leave` selecting among them (the
  most recent enter wins, and the first output BOUND seeds the selection
  until the first enter and never after it, so a hot-plugged panel cannot
  become the window's output; the rule is at `outputs_`,
  `platform_wayland.h`; until 2026-09-02 it was the first output the registry
  named, a coin flip on a 60 Hz panel beside a 120 Hz external) — is the ONE wakeup; the two software deadlines (key repeat, the
  touch window) are POLLED against it in a fixed order — `on_tick` →
  `input_.tick()` (= `maybe_fire_repeat` then `maybe_resolve_touch_window`)
  → worker completions in registration order → the settled hook →
  paint-if-dirty — and the touch window is ALSO checked eagerly at the head
  of every touch event (without which a fast tap resolves a tick late).
  Wayland polls the display fd + timerfd + five worker eventfds (the `pfds`
  array is 7 slots; `pfds[6]` is the fifth worker eventfd,
  `set_sync_worker_completion_fd` — the Synchronize to external storage
  act's, below); Android puts the same fds on the glue's ALooper
  (`kWorkerCount` = 5) and, since the car's arc, ONE MORE SOURCE OF ITS OWN
  — the media command eventfd under `kIdentMedia` (`LOOPER_ID_USER + 1 +
  kWorkerCount`), per PROCESS like the timer rather than per project like
  the workers, drained in `pump()` after the worker completions and before
  the settled hook (The car, below). `drain_events` is paint-only on
  Android because Wayland's `wl_display_dispatch_pending` reads no socket —
  a blocking load observes no new input on either platform (Android's ANR
  watchdog at ~5 s against ~0.5 s loads is the recorded accepted cost).

## Pixel containment — the one conversion owner

`containing_pixel(double) = floor` at `input_core.h` is the product's ONE
fractional→pixel conversion (architect 2026-08-25, landed 2026-08-26):
a pixel x covers [x, x+1), a surface coordinate names the pixel that
CONTAINS it; `std::nearbyint` stays the rule for POINTS on a grid
(CLAUDE.md "Rounding"). The 20 touch, 4 capture and 4 mouse conversion
lines all pass through it. Before it the ABSOLUTE mouse path truncated
while the finger and the captured-pointer ledger rounded — the two
families one pixel apart on the 10px endcap bands and the 8px crossing.
Laptop-visible consequence, accepted: the nav drag's ledger moved from
rounding to floor (≤1px on delivered captured coordinates and on a
motionless click right after a capture release); negative off-surface
drag coordinates floor instead of truncating.

## The other seams

- **Playback**: `playback.h` is the contract on both backends. Its
  portable half — the render body (fractional source cursor; the
  per-frame increment `source_rate / output_rate`, where
  `output_rate` was the JACK graph rate and is now either device's granted
  rate, 0 = the suspended device; the increment carried a `speed *` factor
  until 2026-08-27, when `playback_speed` retired whole and left the RATE
  ratio alone behind it), the predictor, the domain-offset / bind
  logic — is `playback_common.{h,cpp}`, proved statement-identical
  to the old body with five recorded deviations. `playback.cpp` is the JACK
  device half, `playback_aaudio.cpp` the AAudio one (granted-rate open —
  48 000 on the S10 FE's speaker, natively; LOW_LATENCY granted, burst 96;
  a disconnect marks the stream dead and THE NEXT LAUNCH PRESS reopens at
  the new device's rate, no auto-resume — `GuiPlayback::
  ensure_device_available_for_play`, a member of the playback header both
  backends implement, not of the seam (architect 2026-09-02, the four-tier
  review's R-3): the three main-window launch gates
  ask it in place of the read, AAudio closes a dead stream and reopens a
  dead or null one there — `play()`'s own head check, hoisted into the one
  file-local body `reopen_stream_if_dead` both call — and answers false only
  after that reopen failed, so the card comes only then; JACK answers its
  unchanged read `!device_unavailable()`; `play()` keeps the head check and
  its post-publish race check for the render player's ungated road; before
  it the gates READ the latch ahead of `play()` and the tablet stayed mute
  behind the card after every route drop). THE AAUDIO STREAM IS OPENED ONCE,
  STARTED ONCE AND NEVER STOPPED BETWEEN PLAYS (architect 2026-08-27, on
  glass — starting a stream unmutes the device's output path and that
  transient was audible as a click at the head of every audition): it is
  started at open, stopped only where it is about to be closed (shutdown,
  the dead-stream reopen — `close_stream` holds one of the file's two
  `requestStop` calls), and between plays it runs while the callback's gate on the
  session word's playing bit writes silence and reads no sample.
  THE RENDER PLAYER'S PAUSE IS THE ONE NARROWING OF THAT RULING (architect
  2026-09-04, after a road test): a stream left started through the player's
  pause keeps the Bluetooth link fed with silence, so the head unit sees an
  active player under a session that says paused and resolves the
  contradiction by flipping its display back to playing — after which its one
  toggle button sends the already-true direction forever (AVRCP has no
  play/pause opcode). So the pause reaches the device: `GuiPlayback::
  suspend_stream`, a member of the playback header both backends implement
  (JACK: nothing — the laptop has no head unit and no link to suspend), asks
  the stream stopped after `stop()`'s own fence and clears `started`, and the
  next `play()` starts it again through the SAME `start_stream` a reopen
  takes; a start that finds the stream still STOPPING waits briefly for the
  transition it asked for, which is the only place this can add a delay and
  is reached only by a resume pressed straight after a pause. THE ONE CALL
  SITE IS THE PLAYER'S OWN REST ACT (`GuiRenderPlayer::rest_stream`), whose
  five roads are the pause, the tick's dead-device arm, the natural end's TWO
  rests (its terminal rest and its Repeat One arm where the replay refused to
  decode) and the unload on both of its tails — so the main window's
  Space, the waveform scrub and the A/B audition's four plays keep the
  2026-08-27 lifecycle whole, AND SO DO THE PLAYER'S OWN LIVE-TO-LIVE
  TRANSITIONS (codex round 3, the same evening, moving the suspension off the
  stop body's player fork, which every player stop takes and which therefore
  could not tell a rest from a track change): a Next, another row pressed
  while live, and the natural end's Repeat One replay and auto-advance all
  sound again within microseconds and never touch the device. WHAT IT COSTS,
  both accepted at the ruling:
  the start transient is back at the player's resume on the tablet's own
  speaker (*"I don't use the speakers ever — leave it"*), and over Bluetooth a
  resume waits for the link to come back — accepted as that and as nothing
  else, the player's and the GUI's responsiveness over the link being good and
  staying so. `device_unavailable()` and `device_absent()` are untouched
  across a suspension: a suspended stream is neither dead nor absent, and the
  quiescence fence's `!started` early return stays sound because the session
  word's playing bit is already down when the suspend runs — a callback still
  retiring reads no sample at all. So the fence is now the SAME
  PROOF ON BOTH BACKENDS — counting callback invocations, two after the
  flag is lowered, unbounded and hanging rather than weakening, with
  AAudio's escape on a dead or positively terminal stream (no callback
  left to count) — and `stop()` touches neither device. NOTHING LOOPS
  holds on both. AAUDIO'S `stop()` SAYS THE SESSION'S UNDERRUN COUNT
  (architect 2026-09-02, the four-tier review's R-18(d)): at the tail of that
  fence, with the callback quiesced and the counter therefore still,
  `report_xrun_count` reads `AAudioStream_getXRunCount` and prints ONE stderr
  line — a logcat line, every diagnostic riding the redirected fd the log pump
  drains — `AAudio underruns: N this session, M since the stream opened`, only
  when the SESSION lost something, a clean session saying nothing. THE SESSION
  RUNS FROM THE LAUNCH, not from the previous stop (codex round D on R-18(d),
  2026-09-02): the stream stays STARTED between plays and keeps callbacking
  silence, so a previous-stop baseline charged every idle underrun — the whole
  interval before the first play included — to the audio just heard. So
  `xrun_at_launch` on the `Impl` is the SESSION'S FLOOR, `-1` meaning no
  session standing, with THREE WRITERS: `play()`'s tail stamps the count at
  each successful launch, `report_xrun_count` says the difference and voids the
  floor (so a second stop with no play between says nothing), and
  `close_stream` voids it with the stream the counter belongs to, the next
  stream opening clean. The stream-lifetime figure rides unsubtracted in the
  same line. It is DIAGNOSTIC ALONE — no card, no state cell, no engine behaviour
  — because an underrun is already audible and the buffer cannot grow without
  giving up the LOW_LATENCY mode the car's transport wants; what was missing
  was the ability to NAME it afterwards. THE JACK HALF HAS NO TWIN — it
  registers no `jack_set_xrun_callback` and says nothing — the recorded
  asymmetry: the laptop's graph is the architect's own and its xruns are
  already visible in the server's own output, while the tablet's stream is
  inside a phone the app is the only window onto.

  THE LINE IS THE RAW PREDICTOR AND IT LEADS THE SOUND (architect
  2026-09-03): `play()` anchors the predictor at the PUBLISH INSTANT and it
  extrapolates in wall-clock at the source rate from there, and no latency
  figure of either device enters the anchor or the position read. So the painted line lights at `press + D` — the
  compositor's paint-to-light delay — while the sound starts at
  `press + phase + L_audio`, the audio thread's pickup phase (0 to one
  period, re-rolled at every launch) and then the device's own output
  latency; the line therefore runs AHEAD of the ear by `L_audio − D + phase`,
  by a different amount on every launch. THAT IS THE CHOSEN BEHAVIOUR AND NOT
  A DEFECT LEFT STANDING: on 2026-09-03 the architect ran a blind comparison
  at his own rig between this line and a fully compensated build and chose
  this one — *"two continuous streams that we sort of happen to pick up here
  and there — sometimes they match, sometimes they don't, and that's exactly
  what they are."* The derivation and the ruling are at
  `playback_publish_play` (`playback_common.cpp`) and in playback.h's
  predictor design note. EVERY LAUNCH ROAD SHARES IT — bare Space, the
  waveform scrub, the A/B audition's four bounded plays and the render
  player's own launch all publish through that one body — and the line goes
  out at the far end the same way, `is_playing()` dropping when the FILL
  ends: ahead of the last sound by the output latency PLUS that ending fill's
  own valid prefix (the frames it placed before it met the window's end, 0 to
  one callback period), which is the near end of the launch's lead rather than
  the same quantity. THE POSITION SURFACE
  IS TWO FACES OF ONE OBSERVATION and nothing more:
  `cursor()`, the domain integer every consumer's change detection rides, and
  `cursor_precise()`, its pre-truncation double for the sub-frame scanner.
  There is no second predictor and no second position face.

  THE COMPENSATION ARC IS A PARAGRAPH OF RECORD AND NOTHING ELSE. It was
  built 2026-09-01/02 and rolled back whole on 2026-09-03: the anchor moved
  onto the audio thread's first fill, the device's reported output latency
  added to every anchor under a per-epoch re-anchor, a self-measured display
  lead added to the position read, and a natural-end hold keeping the line
  alive until the last queued frame had been heard. THE MECHANISM IS IN GIT
  AND NOWHERE ELSE — a reader who wants it has `git log`, and this file does
  not re-document it. WHY IT WENT is perceptual and it is his: the
  compensated line's START varied with the pickup phase, so the line left
  "sometimes from the marker, sometimes in front of it", which reads as
  non-determinism, while a deliberate constant lag read as the program
  waiting for something. HE PERCEIVES ASYNCHRONY AT THE TOP FEW PERCENT —
  nothing about this residue is imperceptible to him, and what stands is
  ACCEPTED, never unnoticed. THE INSTRUMENTS WENT WITH THE LEADS on the same
  ruling — the JACK port-latency figure with its callbacks and its stderr
  line, and the Wayland presentation-feedback lead with its own — because the
  product does not measure what it was not asked to measure. **THE OTHER HALF
  OF THAT RULING LANDED THE SAME DAY**: the measurements are re-homed under
  `Help → AV Sync Stats` and run ONLY WHILE THAT PANEL STANDS (the Display
  measurement seam below, and render-player.md for the panel itself). NEITHER
  MEASUREMENT IS COMPENSATION and neither reaches the line: the predictor is
  the raw one above whether the panel is up or down, and what the panel does
  is SAY what the residue is, in words, when the user asks. **THE TWO INIT
  STDERR LINES DID NOT COME BACK** — the JACK latency line and the display
  lead's — because the panel IS the instrument now, and a measurement nobody
  asked for is not printed.

  WHAT STAYED IS EVERY CORRECTION, AND EACH STANDS ON ITS OWN GROUND — read
  this list before deleting any of it as lead residue. THE WINDOW TRAVELS AS
  A COMMAND PACKET UNDER A SEQLOCK: `play()` writes (start, end) under a
  sequence word carrying the new generation and then releases the session
  word; a fill consumes only the packet of the generation ITS OWN GATE
  acquired — one read, no retry, a rejected read meaning a newer publish the
  next callback's gate will acquire — and keeps that window as
  audio-thread-private state, so a publish landing mid-fill can move neither
  the start it seated nor the end it renders against. THE SESSION WORD AND
  ITS GENERATION stay, with the TERMINAL qualified by the word the gate
  acquired, and `bind` resetting the cycle stamp together with the word. THE
  AAUDIO DISCONNECT FENCE stays. AND THE CYCLE-STAMPED READ CURSOR STAYS: the
  audio thread publishes (the read cursor, the instant that cursor's frame
  enters the output port) under a seqlock at every fill's end, and every
  RESYNC anchors on that pair rather than on the main thread's `now`, which
  sat anywhere inside the period before that fill. THE CYCLE STAMP IS AN
  ACCURACY DEVICE, NOT A COMPENSATION — it is about the same raw line, and
  what it buys is that a resync's step is the accumulated DRIFT alone instead
  of re-rolling a whole pickup period into the line at every pan end, page
  turn or `c` (playback.h's resync paragraph owns the reasoning and the call
  sites).

  THE WAYLAND SINGLE-COMMIT ORDERING STAYS TOO, by his ruling of 2026-09-03
  although the presentation feedback it was landed for is gone: frame
  request → damage → attach → ONE commit per painted frame, where before the
  arc the frame request went out on a second, empty commit AFTER the content
  commit. One commit per painted frame is what the protocol's shape asks for.
  The record lives at `paint_one_frame` (`platform_wayland.cpp`) and belongs
  there; this file only points at it. The multi-output binding and the
  `wl_surface.enter` selection stay as well, serving the tick's refresh
  period — section C above owns which output that is.

  ANDROID RUNS THE SAME RAW PREDICTOR. With no compensation on either
  backend there is no asymmetry left on this axis to record, and no latency
  figure of either device enters any anchor or position read on either one
  (the seam's two members below MEASURE and publish; nothing consumes them
  but the panel's text). The tablet fact still worth
  keeping is the ROUTE AND WHAT IT PAINTS: the car's route is Bluetooth,
  whose latency is large, variable and unreported, and in the car the render
  player stands, under which the waveform scanner is neither sampled nor
  painted (`main.cpp`'s pre-paint hook returns above it) — so the only moving
  picture there is the modal row's scrub and clock, registered against no
  waveform.
- **Display measurement** (architect 2026-09-03, the AV sync panel): TWO SEAM
  MEMBERS, `set_display_measurement(bool)` and `display_stats()`, whose
  contract is at `platform_wayland.h` beside them and whose types
  (`GuiDisplayStats`, and the audio half's `GuiAudioStats`) are plain values
  in `av_sync_stats.h`, so both backends, both playback halves and the
  panel's composer share ONE spelling. THE ARM IS THE WHOLE DESIGN: it has
  exactly TWO callers, the panel's open passing true and its one close body
  passing false, and with the bit DOWN — which is every frame of every other
  session — the backend takes no clock stamp, requests no feedback and holds
  no ring. Arming and disarming both CLEAR the ring and destroy whatever is
  outstanding, so a panel opened twice reads its own session. `display_stats`
  is a pure read the panel makes once per frame while it is up.
  **WAYLAND** answers it with `wp_presentation`, bound at init and asked for
  nothing until the arm: every content commit then carries one feedback
  request stamped with the instant the pre-paint hook began, and the reading
  is the MEAN, MIN AND MAX of (presented − sampled) over the last thirty
  presented frames, beside the window's own output name and refresh. TWO
  PRECONDITIONS ARE REPORTED RATHER THAN ASSUMED — the global was advertised,
  and the compositor's `clock_id` IS `CLOCK_MONOTONIC`, the predictor's own
  clock — because a figure measured across two clocks is a number and not a
  measurement; either one missing, the reading says which and the panel
  prints no net line. There is NO FALLBACK FIGURE and no stderr line.
  **ANDROID** answers `available` false and its arm is a no-op, and that is a
  FACT ABOUT THE PLATFORM rather than a gap: there is no feedback road on the
  `lock`/`unlockAndPost` path (EGL frame timestamps need an EGLSurface;
  Choreographer timelines describe the next frame, not this one's light), and
  the audio half says the same on its own side — AAudio reports no
  trustworthy output latency, the car's Bluetooth route being large, variable
  and unreported. So the tablet prints its backend, rate, burst and stream
  buffer and then says the rest is not available on this backend. **NO
  ESTIMATE IS INVENTED**, and that is the standing record, not an omission to
  fill in later. (The AUDIO half is NOT a seam member: `GuiPlayback::
  audio_stats` is a member of the playback header both backends implement, the
  shape `ensure_device_available_for_play` already set.)
- **The device config's first-run template**: `GuiPlatform::device_config_defaults()`,
  ONE static accessor each backend answers, and the seam's third
  both-sides member. The FIVE keys it stamps are per-DEVICE preferences
  (settings.md owns the file and its schema), and the values a
  fresh device should start from are the one thing only the platform knows:
  the laptop answers 100 % and the clone's own `projects/`, Android 225 %
  and `<externalDataPath>/projects`; both stamp `kDefaultProjectsRepo` and a
  blank `last_project` AND a blank `sync_path` — neither template guesses a
  destination for the mirror, a wrong guess aiming its creates, copies and
  removals at a folder the user never named. (A key the template no longer stamps, `audio_player`, stood here until
  2026-08-28 — the laptop answered `audacious` and the tablet a blank, no
  spawnable player existing there — and retired whole with the in-app render
  player, which plays a render through the product's own engine on both
  devices; render-player.md.) `gui_main` asks it before
  `GuiPlatform::init` and stamps the file if none exists, which is what
  keeps the GUI proper free of the `#ifdef` the alternative would need.
- **Fonts**: `gui_font.h` is the ONE face owner (`GuiFontFamily::Sans/Mono`);
  the ten former `cairo_select_font_face` sites call it. Linux resolves
  through fontconfig (`gui_font_fontconfig.cpp`, byte-identical to before);
  Android through the two bundled Liberation files
  (`gui_font_bundled.cpp`, FT faces over owned copies; a failed install
  aborts before the first paint — a missing asset is a build defect).
- **Entry**: `gui_main(argument)` (`gui_main.h`) is the one GUI body;
  Linux `main()` is a thin wrapper passing its optional `<wav>` or nullptr,
  Android's `android_main` pins `LC_ALL=C` (bionic starts in C.UTF-8), sets
  `XDG_CACHE_HOME`/`XDG_CONFIG_HOME`/`HOME` to the app's dirs, installs the
  fonts, waits for the window and calls it with NO ARGUMENT (2026-08-27, the
  project model): which project opens is the portable owner's question —
  `startup_source` (`project_model.h`) opens the device config's
  `last_project` or the first valid project in name order under its
  `projects_path`, and File → Open is the picker on both platforms. WHEN
  `gui_main` RETURNS, `android_main` asks the activity to finish and then
  STAYS: it services the glue until `destroyRequested` is set and only then
  returns into `android_app_destroy`. **THE LOOP OUTLIVES THE ACTIVITY, NEVER
  THE REVERSE** (2026-09-06): every glue callback the framework runs on the UI
  thread blocks on the glue's condition until the loop thread acknowledges it,
  so a thread that left at the finish stranded the UI thread in `onPause`, and
  the system ANRed and killed the process ten seconds later (six
  `data_app_anr` records on the tablet). The two waits — for the first window
  and for the destroy — are one helper, `pump_glue_until`. The
  `current` file and `resolve_source_path` are RETIRED (a stale `current` on
  the device is simply never read). What the backend still owns of the sync
  convention is WHERE the projects are: its template stamps
  `<externalDataPath>/projects` (`/sdcard/Android/data/<pkg>/files/projects`,
  which adb pushes into with no permission granted) as the projects path.
- **The loop contract** (`platform.h`, authoritative): ONE `GuiPlatform` per
  process, MANY `run()` calls. `gui_main` is a loop — everything ONE PER
  PROCESS (the signals, the device config, the scale install, the platform
  object, the render cache) outside it; everything ONE PER PROJECT
  (`run_project`, main.cpp: AppState, audio, playback, the caches, the workers,
  the handlers, every callback) built inside, run, torn down in today's order.
  `GuiPlatform::init` is the one per-process act that RUNS INSIDE the loop, in
  the FIRST session (`run_project`, guarded by `window_up`): it takes the cold
  window size, which is a per-project object's, and every later session
  inherits the window standing. The scale and the touch slop are installed by
  `gui_main` ahead of it either way, so the first configure is at the user's
  scale.
  `run()` returns for an EXIT (`request_exit`; `exit_requested()` true) or a
  RUN STOP (`request_run_stop`, the Open project picker's reopen — the window and the
  input core stand; the stop bit is cleared at each `run()`'s head). The
  worker fds are re-registered per session (each setter takes -1 and, on
  Android, unwatches what it replaces); the callbacks are re-installed; the
  geometry is REDELIVERED (`redeliver_geometry`) since the window sends no
  configure for an unchanged size. Nothing is reset for key repeat, the touch
  window or a capture — a reopen comes from a key press or a button lift on
  a modal, where none can stand. Playback is shut down and re-inited per
  session (one AAudio stream start per reopen, accepted). The unsaved-tab
  prompt is Ctrl+Q's own with a REOPEN target (`GuiCloseTarget`, prompt.h).
- **Android stubs** (each named at its site with its Wayland twin), THREE
  since 2026-09-03 and re-greped at that count: pointer capture as no-ops (the
  notional-x FIELD survives and tracks the finger), cursor kinds stored and
  never applied, titles. **THE CLIPBOARD LEFT THIS LIST** the evening it was
  written into it (architect, *"if it's cheap, let's go ahead and build it"*):
  it was one stored string with `clipboard_set_text` answering FALSE, and it
  is now the SYSTEM clipboard over the MediaSession's own JNI road — two more
  `MainActivity` methods, `clipboardSet(byte[])` / `clipboardGet()`, the ids
  looked up in the same init block as `mediaState`'s and the payload crossing
  as BYTES because JNI strings are modified UTF-8 (conventions.md's clipboard
  section owns the whole ruling; the stored string survives only as the
  road-absent fallback, for a failed attach or a missing method). The static
  capability `clipboard_publishes()` went with the stub: both backends now
  publish, so it had no producer and no face reads it. THE READ'S PAYLOAD
  BOUND IS ONE NUMBER ACROSS THE SEAM since 2026-09-03 (codex):
  `kClipboardMaxBytes` in gui_input.h, which both backend headers include —
  the Wayland pipe read's own cap since it was written, and now the Android
  read's, asked of the returned array's length before the `std::string`
  resize, with a mirror in `MainActivity` refusing the same size before it
  encodes anything. A third copy of a number in the Java sliver is the media
  command table's situation and takes its rule: no build checks the two, so
  they are edited in one act. **CLOSE LEFT THAT LIST ON 2026-08-29**: THE ANDROID CLOSE IS
  BACK (architect — the tablet's BACK asks the unsaved-work question the
  laptop's X asks). `AKEYCODE_BACK` is consumed WHOLE in `on_input_event` —
  the one KeyEvent this backend answers — and its ACTION_UP fires the seam's
  `CloseCallback`, the very hook Wayland fires for `xdg_toplevel.close`, so the
  consumer's one close road runs: `GuiPrompt::request_close` prompts on a dirty
  tab and completes at once on a clean one, and the exit that follows is
  `request_exit`'s own (`should_exit_` plus `ANativeActivity_finish`, the one
  asker `android_main`'s tail shares), so `run()` returns, `gui_main` returns,
  `android_main` services the glue until the destroy the finish asked for has
  landed and only then returns, and the activity goes exactly as Quit's does.
  The DOWN is consumed too, not merely ignored: the framework's own back
  handling runs off the UP after tracking the DOWN, so an unconsumed DOWN
  would leave the system holding a tracked press it could still act on beside
  a prompt we had just raised; DOWN repeats ride the consume and fire nothing.
  Firing from the input drain is safe under `drain_looper`'s "nothing may
  block" rule — the callback raises a bottom-row prompt or requests an exit
  and returns. Every other key still returns 0 (hardware keyboards are out of
  scope, `touch.md`), and the road into the core's key path stays
  `synthesize_key`. **BACK ASKS; A DESTROY
  CANNOT** is the residual loss, recorded at `drain_looper`'s
  `destroyRequested` arm: `APP_CMD_DESTROY` is the system stating the activity
  is already going, with nobody left to answer a prompt, so a session killed
  from the task switcher still goes unasked. Predictive back does not take this
  road away: the manifest declares no `android:enableOnBackInvokedCallback` and
  targets SDK 34, so the framework dispatches the legacy `KEYCODE_BACK` to the
  activity — which is the mechanism BACK already left the app by (the key arm
  returned 0 and the system's default finished the activity), measured on the
  tablet (`android/NOTES.md`).
- **Key repeat on Android** is hard-coded `set_repeat_info(25, kHoldBeatMs)`
  (architect 2026-08-23: labwc's numbers by convention; the platform
  advertises none) — his `rc.xml`'s `<repeatRate>` 25 beside the
  `<repeatDelay>` 575 that IS `kHoldBeatMs`. THE RATE READ 30 UNTIL 2026-09-02,
  a number the convention sentence did not support, corrected under the
  four-tier review's R-18(a). Hardware keyboards are out of scope; the owned
  painted keyboard reaches the core through `synthesize_key`.

## The on-screen keyboard

The glass has no hardware keys, so the product paints its own (2026-08-27): a
four-row Maliit-shaped surface standing while ANY OF THE TEXT EDITORS
(`text_editor::Kind` is the authoritative list — six today) stands on a backend that asks for one (`wants_onscreen_keyboard`), sitting
directly above the bottom row over the waveform area's lower part, whose every
key press goes through `synthesize_key` into the ORDINARY key path — so the
editors' grammars, their refusals, the undo coalescing and the core's repeat
synthesis are inherited whole rather than mirrored, and a new editor gets a
working keyboard by existing. `src/gui/onscreen_keyboard.h` IS AUTHORITATIVE
for all of it — the layout table and the two derivations off it, the geometry
walk the painter and the press router share, the one-shot shift and the symbol
layer, the session-change owner that clears them ahead of the next press, and
the rule that the waveform is not painted under the opaque band — with the
painter in `paint_handler.cpp` and the press router in `input_pointer.cpp`
beside every other painter and router. The standing predicate is false forever
on Wayland, which is what makes the laptop build's behaviour identical by
construction rather than by care.

THE BAND HAS A SECOND TENANT SINCE 2026-08-28 and the keyboard yields it: the
RENDER PLAYER'S FOLDER OVERLAY paints in this exact rect. `onscreen_keyboard::
stands` gained a third term for it that same day and LOST IT AGAIN when the
pickers lost their fields — the exclusion is structural, no editor and the
overlay being able to stand together (the record is at the predicate) — while
`waveform_paint_area` gates on EITHER tenant, against one as-painted bit that
describes the SLOT rather than either surface
(`AppState::keyboard_slot_painted_standing`). The overlay takes no platform
term, unlike this surface's — it serves the pointer and the finger alike and
stands on both backends. render-player.md owns it.

THE BAND CARRIES NO CHROME OF ITS OWN (architect 2026-08-27, on his first glass
drive of the surface): no line at its top edge. Its ground IS the bottom row's
ground, so the keyboard and the row it sits on read as one block, and the only
line in that neighbourhood is the bottom row's own border-top, which that row
paints and this surface neither owns nor touches. `kBorderPx` / `border_px()`
are gone with the seam, so the band's published height, its paint and its hit
rect are one number by construction.

EVERY CAP IS TEXT, on the one sans face at the product's one text size through
the one shaping chokepoint — the letter caps, the layer toggle's `abc` / `&123`
and the FUNCTION KEYS' WORDS alike: **Shift**, **Backspace**, **Space**,
**Esc**, **Return**, **Tab** (the words are `cap_word`'s, retold here after the
2026-09-01 capitalization sweep renamed the act-named pair — the paragraph
below owns the ruling). **Backspace** is on both layers (each layer's own row 2
ends with one), and **Space**, **Esc** and **Return** are on both by
construction — row 3 is one array shared by the two. **Shift** is the LETTER
layer's alone: the symbol layer's row 2 opens with **Tab** in its place
(2026-08-27, with File → Open Project — the one key the letter layer has no
room for, and what the product's prompts COMPLETE on; a bare `GuiKeys::Tab`
through the same `synthesize_key` road; the blank role is deleted with its last
slot). IT IS NO LONGER THE OPEN PROMPT'S GLASS ROAD: that prompt's PICKER
landed 2026-08-28 and takes this keyboard's own band, so the glass gesture is
File → Open Project, tap the row — the tap's lift both highlights and opens
it, the row's Cancel button its only other reach — and typing a name is the
laptop's.
The key stays for every OTHER prompt's completion and ring walk.
The symbol layer's SECOND SPACE is not that row's leading slot either but a
deliberate duplicate CHARACTER key beside its `_`, so a hand already in the
symbol layer for the `/` of `12 7/8` need not go looking for the bar. The function keys wore unmodified Breeze glyphs for their first day
and read OVERSIZED beside the letter caps — a 22-unit icon scaled to the key's
own height beside a 12pt letter — and a full-width row has room for words, so
they wear words. THE WORD A FUNCTION KEY WEARS IS THE KEY'S NAME (planner 2026-09-01, under the capitalization sweep): all five say what they ARE — Shift, Backspace, Return, Esc, Tab — in the product's one key spelling (`spell_chord`'s head, `gui_input.h`). They said what they DO from 2026-08-27, which is why the Enter cap read "Enter" and the Escape key read "Cancel"; the first took its Qt name that day and the second followed the same evening, one act-named cap beside four key-named ones being exactly the exception this product no longer keeps. `onscreen_keyboard::cap_word` is the words' ONE owner,
the painter's icon branch is gone, and the five glyphs that surface alone read
(keyboard-caps-disabled / keyboard-caps-enabled / keyboard-enter /
keyboard-spacebar / edit-clear-locationbar-rtl) went with it — enumerators,
defs and committed assets, taking the roster from 51 to 46 AT THAT MOMENT (it stands well above that today — `kIconCount` in icons.h is the one authority, the transport, player, card and value-copy glyphs having been added since). SHIFT'S ONE-SHOT ARM IS THE
FACE: the cap says "Shift" armed or resting, and what says the arm is the key's
ARMED FACE — `kRedesignSelectedFill` under a `kRedesignLine` frame, the icon
row's own lit-toggle face, which this key and the layer toggle already wore off
their lamp bits — together with the letter caps, every one of which turns
capital while the arm stands. No new colour; the caps pair's stateful glyph is
what the face replaced.

## Synchronize to external storage

`GuiPopupAct::SyncExternal`, the File menu's row (architect 2026-08-27, landed
2026-08-28 in `b92ea097`/`95ea84d4`), mirrors the open project onto the folder
the DEVICE CONFIG names. It was the menu's ONE CHORD-LESS ROW until 2026-08-31,
when the architect gave the act BARE BACKSLASH — a spelling the product bound
nowhere, so the render family keeps `Ctrl+Alt+Shift+R` exactly as his refusal
of a binding in 2026-08-27 intended. The row still calls the act directly
rather than dispatching that chord (the act's body carries the gates), so the
two are roads to one body, and both allowlists — read-only and the `h` view's —
admit the key.

**THE DESTINATION IS TOLD, NOT FOUND** (architect 2026-08-30). It is
`sync_path`, the device config's fifth key (`settings.md`'s device-config
section and `device_config.h` own its grammar: EMPTY, or an absolute path
under the shared path-value rules), and the act composes
`<sync_path>/<project name>/`. It was DISCOVERED for three days —
`GuiPlatform::removable_volume()` per backend over the one counting rule
`sole_removable_volume` — and that rule worked on the laptop and COULD NOT
WORK ON THE TABLET AT ALL (the open device fact below). A per-device
destination is a per-device fact, which is what the device config is for, and
a configured path is what every desktop mirror does; the seam member, both
discoveries, the counting rule and its two sentences (`No removable volume
mounted`, `Several removable volumes mounted: a, b`) are DELETED, so the act
has one road to its destination and no fallback chain.

The lift runs `GuiInputHandler::synchronize_to_external_storage`
(`input_key_dispatch.cpp`), which refuses silently through the Open row's own
gates (a prompt or editor standing, a load in progress) and with nothing
loaded; it is LEGAL ON A READ-ONLY TAB, ADMITTED IN THE `h` VIEW (2026-08-29)
and STOPS NO PLAYBACK, since it authors nothing and writes outside the project
entirely. A second dispatch while one is already running writes
`A synchronization is already running` to a notification card and stops there — the
checkpoint act's own single-in-flight shape, answered in words since a menu
item never greys. Then the destination: an EMPTY `sync_path` is the device
saying it has none and answers `sync_path is not set` on a card — THE KEY BY
ITS OWN SPELLING, a config key being named the way it is written in the file
everywhere in the product — and nothing runs. Passing both, it composes the
job — the sync root, the project name and folder, and the project's TWO
OUTPUT FOLDERS THEMSELVES, `render/` and `tmp/` (the title is not a term of the
act: the mirror LISTS those folders, architect 2026-09-02) — and dispatches to
`GuiExternalSyncWorker`; nothing says the act has
started (a process line is state, and the verdict follows within seconds).

THE MIRROR'S LAYOUT AND SCOPE are `external_sync.h`'s whole statement:
`<sync_path>/<project name>/` holds every regular `.wav` out of `render/`
directly and each `tmp/` batch folder AS ITSELF (folder name and NN numbering
verbatim), wav files only — no sidecars, no `.fingerprint`, no `peaks/`, the
stick being played from and not authored in. **THE SET IS THE FOLDERS' REAL
CONTENTS** (architect 2026-09-02): the act reads `render/` the same way it
reads a batch cell, through the one listing rule `list_wav_files`, so the stick
equals the disk by construction. It COMPOSED `render/<live title>.wav` until
that day, which made the mirror's set and `prune_render_folder`'s definition
two rules that had to agree, and between a retitle and the next render they did
not: the disk kept the old wav, the stick lost it, nothing was copied in its
place and the act said nothing. The prune is a separate act with its own
trigger now, and the mirror simply follows the folder — a stale pair is
mirrored as it stands and leaves the stick on the first Synchronize after the
prune takes it off disk. An absent `render/` is an empty set, never a refusal;
the act creates nothing on the source side. Copies run first; afterward every file and folder
under that one destination folder which is not in the set is deleted, so an
act interrupted mid-way (a pulled stick, a killed process) leaves the stick
with at most EXTRA files, never fewer. The scope is that destination folder
alone, never the sync path itself or another project's folder under it, which is
what lets one stick carry several projects side by side. Nothing is skipped
or retried on an mtime guess: the stick is carried between two clocks, so
every file is copied on every act — each onto a staging sibling that is
renamed onto the final name only once the copy is complete, so a failed or
interrupted copy leaves the previous file on the stick whole.

THE FIVE STRICTNESS RULES are `external_sync.h`'s head, stated there once and
nowhere else: the mirror deletes only against a listing it finished (any
enumeration or status error other than an absent optional root ends the act
before a single deletion, a destination-side one included, which therefore
cannot report success — SO THE DELETION IS TWO PASSES, classifying the whole
destination into kept, unkept link and unkept subtree, top level and then each
kept batch folder, before its first removal, which is also why no
`directory_iterator` is ever live while its own directory is being changed); no
symlink is ever followed ON EITHER SIDE, which makes the scope claim above true by
construction (THE SYNC ROOT ITSELF IS THE FIRST NAME CHECKED, every path in the act
being composed under it, and a link at one of the act's own names is a REFUSAL
and not a deletion, an unkept link being removed as a link; SINCE 2026-08-30
that check is also where a destination that is simply NOT THERE answers — an
unplugged stick or a mistyped `sync_path` refuses `'<name>' is not a
directory`, the act never creating its own sync root; and SINCE 2026-09-02 the
SOURCE side is in the rule too — the classifiers followed links until then,
so a symlinked `render/`, `tmp/` or batch folder was walked and a symlinked
`x.wav` was copied through, which HELP already promised against: the three
roots the act opens and the wavs it copies refuse on a link, while a link the
act would neither walk nor copy is simply not in the set, the same line the
destination side draws) — the checks run at
the act's start and not again at each use, that check-then-use window being an
ACCEPTED COST, a hand on a mounted stick mid-act and so the adversarial class
this product never backstops; every copy is staged; what is kept is kept by
filesystem identity (`std::filesystem::equivalent`) rather than by spelling, the
stick being case-insensitive vfat; and — THE FIFTH, 2026-09-02, rule 1's
copy-side twin — a SET THE DESTINATION COULD NOT HOLD APART is refused before
anything is created or copied: two desired entries of one destination
directory whose names fold together under an ASCII case fold are two files on
the project's case-sensitive filesystem and ONE entry on the stick, so the
act ends with `'<a>' and '<b>' would be one file at the destination` (the
fourth fixed sentence) instead of letting the second rename replace the first
and the identity test keep the survivor as either, silently and successfully.

WHAT A FAILURE LEAVES is `external_sync.h`'s (a)(b)(c) and nothing stronger:
(a) no deletion runs at all unless every copy succeeded and the destination
classification finished; (b) a copy-phase failure leaves every replacement
completed before it standing, each having been its own rename, and the file it
failed on holding its previous contents whole; (c) a deletion-phase failure
leaves the removals before it done and the rest undone, each removal being its
own act and an unkept subtree's `remove_all` able to stop part-way itself. The
act is not transactional and rolls nothing back: pressing the row again is the
whole recovery.

THE WORKER, `GuiExternalSyncWorker` (`external_sync.{h,cpp}`), is shaped
exactly like `GuiHistoryCommitWorker`: its own thread, a condition variable,
one completion eventfd the platform polls. SINGLE JOB IN FLIGHT structurally,
and NO CANCEL — `shutdown()` JOINS an act already running rather than
interrupting it, a copy left half-written being worse than a mirror caught
between its copies and its deletions. The worker's own verdict lands back on
the main thread through `on_external_sync_complete`, which raises a NORMAL
notification card and nothing else (2026-08-29; the status chain's transient
tier for one day before that) — AND ONLY ON A FAILURE. **A SUCCESSFUL
SYNCHRONIZATION SAYS NOTHING** (architect 2026-08-30: "if it succeeds, we
don't necessarily need [a notice]"), the render's own precedent, a render
served silently publishing silently; the count sentence `Synchronized <N>
file(s) to <path>` that stood from 2026-08-28 is DELETED from the outcome, not
merely unraised, so a successful verdict carries an empty message by
construction. On the first failure of any kind the card names the path it was
reading or writing and the system's own words (`Cannot read '<path>': <...>`
/ `Could not copy '<path>': <...>` / `Could not remove '<path>': <...>`), or
one of the FOUR fixed lines (the symlink rule's own three, `'<path>' is a
symbolic link` — which a SOURCE root, a `tmp/` entry or a `.wav` earns since
2026-09-02 exactly as a destination name does — / `'<path>' is not a
directory` / `'<path>' is not a regular file`, the second
of which is also the unplugged stick's and the mistyped path's answer; and the
fold rule's `'<a>' and '<b>' would be one file at the destination`) — every
`<path>` named RELATIVE TO THE MIRROR'S TWO ROOTS (`<sync path's last
component>/…` on the stick, the path under the project folder in the project;
the full path is on
stderr), the basename rule of the cards. A FAILURE IS A NORMAL CARD, never
the critical class — that class is the checkpoint act's, whose failure needs
the terminal; a failed synchronization is retried by pressing the row again.

THE SEAM GREW TWO MEMBERS FOR IT, and ONE OF THEM HAS SINCE LEFT.
`GuiPlatform::removable_volume()` — static like `device_config_defaults`, each
backend owning its discovery over the one shared counting rule — was the
destination's answer from 2026-08-28 until 2026-08-30, when the destination
became the device config's `sync_path` and the member, both discoveries and the
counting rule were deleted whole (the ruling is at the head of this section).
WHAT THE TWO DISCOVERIES MEASURED IS KEPT, because it is why the key exists:
the laptop read the directory entries under `/run/media/<user>/`, the udisks
mount root, which worked; ANDROID read the `/storage/<name>` mount points out
of the process's own mount table (`/proc/self/mounts`) and NOT
`opendir("/storage")`, because `/storage` is `drwx--x--x` to the app's uid —
TRAVERSABLE BUT NOT LISTABLE — so a listing there answers EACCES, a permission
the app was denied and not an empty device (measured on the tablet 2026-08-28
with the stick mounted: the listing road answered `No removable volume mounted`
where the mount-table road answered correctly). Even the working road found
nothing there, for the open device fact below.

THE ONE MEMBER THAT REMAINS, declared identically on both backends (contract at
`platform_wayland.h`, which owns it):

- **`set_sync_worker_completion_fd`** — the FIFTH worker completion eventfd,
  ordinary in shape (store the fd, store the callback) beside the async
  render, waveform, checkpoint and history-prefetch workers' own setters.
  Retelling the loop contract's own inventory (section C, above): Wayland's
  `pfds` array is now 7 slots (the display fd, the timerfd, five worker
  eventfds — `pfds[6]` is this one); Android's `kWorkerCount` is 5,
  dispatched in registration order (async renderer, waveform, checkpoint,
  prefetch, synchronization) over the glue's ALooper idents
  `kIdentWorker0 .. kIdentWorker0+4`.

THE OPEN DEVICE FACT: on this One UI build the OTG stick mounts with
`mountFlags=0`, not VISIBLE, so no `/storage/<uuid>` view exists for ANY app.
That is what the discovery could not get around and it is what the key does
not get around either — a path that does not exist cannot be configured, so
THE TABLET'S `sync_path` STAYS EMPTY and the act answers `sync_path is not
set` there until a writable destination exists (2026-08-28, unchanged by the
2026-08-30 move; what the move bought is that the LAPTOP now names its stick
instead of hunting for it, and that the tablet's refusal names the thing the
user would have to set). THE SAF ROAD THROUGH THE JAVA SLIVER — a Storage
Access Framework picker (`ACTION_OPEN_DOCUMENT_TREE`) granting a scoped tree
URI regardless of `mountFlags` — is the design's named contingency for this,
IS NOT BUILT and NEEDS AN ARCHITECT RULING; it would reach the stick through a
tree URI rather than through a path, so it is not something `sync_path` can
name.
What the shell showed, 2026-08-28: `sm list-volumes` — `public:8,81 mounted
067C-8690`; `dumpsys mount` — `mountFlags=0`, `path=/mnt/media_rw/067C-8690`.

## The content rect is the window

THE SURFACE IS THE WHOLE PANEL AND THE WINDOW IS NOT. An app window's frame on
modern Android is the full display by construction — the framework gives the
activity `FLAG_LAYOUT_IN_SCREEN | FLAG_LAYOUT_INSET_DECOR` and "fitting the
system windows" is DecorView PADDING, which a `NativeActivity` never sees
because it takes the WINDOW's own surface (`Window#takeSurface`). So neither
the theme, nor the target SDK, nor `setDecorFitsSystemWindows` shrinks what
`ANativeWindow_getWidth/Height` report: measured on the tablet 2026-08-27,
`frame=[0,0][2304,1440]` at every setting tried. What the framework DOES
deliver to native code is the **content rect** — the band inside the system
bars — through `onContentRectChanged`, which the glue stores in
`android_app::contentRect` and announces as `APP_CMD_CONTENT_RECT_CHANGED`.

THE ANDROID BACKEND MAKES THAT RECT THE WINDOW. `width()`/`height()` are the
RECT's size, `adopt_window` re-reads it on all four window commands (INIT,
RESIZED, CONFIG_CHANGED, CONTENT_RECT_CHANGED) and fires the ordinary resize,
so the vertical stack (`main.cpp`, which takes any height) lays out inside the
bars and the waveform pays the difference. THE ORIGIN IS THE BACKEND'S OWN
CONTAINMENT and crosses the seam nowhere: it is ADDED at the one blit
(`present`, which also fills the two bands outside the rect — the TOP one with
`kRedesignRowGround`, everything else with `kRedesignContentGround`; one owed
full-surface post per adoption, and `ANativeWindow_lock`'s own dirty-rect
widening after that) and SUBTRACTED at
the one input decode (`on_motion_event`'s two coordinate lambdas — the key path
carries no coordinates and the capture doors take GUI coordinates that never
reach the window). `GuiInputCore`, `main.cpp` and every painter are identical
to the Wayland build's and none of them can name the origin. A touch inside a
band translates to a coordinate outside the window and is delivered as such,
neither clamped nor dropped — the shape a Wayland drag past an edge already
takes (`containing_pixel`, input_core.h); the bars' own windows take those
touches in practice. The startup line reports both:
`window 2304x1270 at (0,74) of surface 2304x1440, tick 5 ms`.

THE RECT IS THE FRAMEWORK'S MINUS THE AIR — the backend measures no bar and
subtracts no inset of its own, which is why nothing here names a bar height as a
constant. The one thing it does subtract is `kStatusBarAirPx` (14 DEVICE pixels,
the retune knob — 16 for the forty-five minutes between the strip's landing and
its retune on 2026-08-27, the mirror of One UI's own top gap), added to the rect's TOP inset inside `resolve_content_rect`
and only when the framework reports a top inset at all, so that a fullscreen
future gets no blank band: the clock sat closer to our first row than to the top
of the panel (architect 2026-08-27), and the rows given up join the top band,
which paints `kRedesignRowGround` — the status bar's own colour and the menu
row's — so bar, air and menu row read as one title strip. Origin, size, damage
and every touch coordinate follow from that one function. THE FRAMEWORK'S RECT ALREADY EXCLUDES BOTH BARS, measured on an AWAKE Tab S10
FE under the architect's own Screen zoom (override density 320):
`window 2304x1270 at (0,74) of surface 2304x1440` — a 60 px status bar plus the
14 px of air we add above, and a 96 px taskbar below. So the backend subtracts
no bar and needs none of its own arithmetic; the air is still the only thing it
takes off. (History, one line: an earlier reading of 2304x1387 at (0,53) showed
the status bar alone, because the taskbar reported no inset while the panel
DOZED with the cover shut — dumpsys had it as `tappableElement` and as
`mAppBounds`, `type=navigationBars ... visible=false`. The next step recorded
from that reading — a JNI call handing the native side
`WindowInsets.Type.tappableElement()`'s bottom to subtract — IS RETIRED: on an
awake panel it would subtract the taskbar a second time.)

A surface pixel is still a panel pixel — there is no scaling anywhere on this
platform — and the window is now the content rect rather than the surface.

## The Java sliver

`android/app/java/com/warptempo/gui/MainActivity.java` is the product's ONE
Java class: a `NativeActivity` subclass whose `onCreate` body is
`setDecorFitsSystemWindows(true)` plus both bars' colours. The first ASKS
for the inset layout; what actually delivers the inset area is the content rect
above, and the pair (target 34 + this call) is what makes the framework report
one. THE SYSTEM BARS
SHOW PERMANENTLY (architect 2026-08-27): immersive mode — both bars hidden
sticky-immersive at create and every focus gain — is RETIRED as a reversible
experiment, because a swipe brought the taskbar's icons up OVER the app with no
background of their own (Android's transient bars, its behaviour rather than
ours) and read as a bug, and the waveform is tall enough to lose the bars'
height. THE STATUS BAR IS THIS WINDOW'S TITLE BAR and takes the colour the
architect's own labwc theme gives one (architect 2026-08-27): `setStatusBarColor
(0xFF292C30)` — the provenance is a chain, not a derivation, from
`~/.config/labwc/themerc-override`'s `window.active.title.bg.color: #292c30`,
which IS `kRedesignRowGround`, with its `window.active.label.text.color: #fcfcfc`
= `kRedesignLabel` the reason the bar's icons stay light (the
`APPEARANCE_LIGHT_STATUS_BARS` bit is CLEARED — it means dark icons for a light
bar). `FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS` is set by hand beside it because
`setStatusBarColor` needs it and the legacy theme below does not set it; both
are deprecated at API 35 and honoured at the 34 the manifest targets. THE
TASKBAR'S ICONS ARE THE LAUNCHER'S and nothing here touches them — the
architect: "the taskbar looks great, it's already the correct color" — but THE
BAND UNDER THEM IS OURS, and has been since `FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS`
made the window draw BOTH bars' backgrounds: it took the inherited
`navigationBarColor`, measured on the device at (33,35,38), which is the system
default and is also #202326, so nothing visibly changed by luck. It is now
stated: `setNavigationBarColor(0xFF202326)` = `kRedesignContentGround`, the
ground the taskbar's icons already sit on (provenance: the palette block in
`src/gui/render.h` — NOT a labwc colour, this one). `APPEARANCE_LIGHT_NAVIGATION_BARS`
is untouched, so the icons stay exactly as they were, and
`setNavigationBarColor` carries the status colour's own deprecation note
(deprecated at 35, honoured at the 34 the manifest targets). The activity's theme is `Theme.NoTitleBar` (not `.Fullscreen`)
and **targetSdk is 34**, stepped back from 35 the same day: Android 15 lays a
target-35 window out edge-to-edge whatever it asks for, and the 35-era opt-out
is the `windowOptOutEdgeToEdgeEnforcement` THEME attribute, needing a
`res/values` style and an `aapt2 compile` step this APK has never had. Every
later Java need (the SAF picker's `onActivityResult` is the one still
outstanding) joins this class as a method, and TWO HAVE — THE CAR'S
MediaSession on 2026-08-28 (the section below) and THE SYSTEM CLIPBOARD on
2026-09-03 (`clipboardSet` / `clipboardGet`, riding the same attach, the same
env and the same lookup block) — the first of them bringing the class its
`static {
System.loadLibrary("warptempo_gui"); }` initialiser (NativeActivity's own
dlopen does not register the library for name-based JNI resolution), its
one `native` method `nativeMediaCommand(int, long)` — STILL the only one,
the clipboard's pair going UP rather than down — the first of the instance
methods the native side calls up (`mediaState(...)`, joined 2026-09-03 by
`clipboardSet` / `clipboardGet`), an inner
`MediaSession.Callback` (an inner class, not a second top-level one) and
the first lifecycle override beside `onCreate` — `onDestroy`, which
releases the session after `super.onDestroy()` has joined the native
thread. Launch component: `com.warptempo.gui/.MainActivity`.

## The car: the MediaSession and the command road

Architect design 2026-08-28 §3 (`tmp/player_design.md`), landed the same day
as the render player's second phase. In the car the head unit's buttons reach
an app over Bluetooth AVRCP as media-button events delivered to whichever app
holds an ACTIVE `MediaSession`, and the head unit's display reads that
session's metadata and playback state. The session is Android's alone; the
seam carries exactly two doors, contracts at `platform_wayland.h`
(`gui_media.h` is the vocabulary — `GuiMediaCommand`, `GuiMediaState`, and the
kind table shared with the Java sliver BY NUMBER, `kGuiMediaCommandKindCount`
under a static_assert on one side and `MEDIA_KIND_COUNT` on the other):

- **`set_on_media_command`** — the hook the loop fires ON ITS OWN THREAD, one
  call per command in arrival order, from the pass that dispatches the worker
  completions and before that pass's settled hook and paint. Wayland stores
  it and never fires it (the stored-hook shape `set_on_close` carried on the Android side until BACK became its producer there, 2026-08-29). Android's road DOWN:
  `MainActivity`'s `MediaSession.Callback` (`onPlay`, `onPause`, `onStop`,
  `onSkipToNext`, `onSkipToPrevious`, `onFastForward`, `onRewind`,
  `onSeekTo` — reached now only by a controller calling an action directly —
  and `onMediaButtonEvent`, WHICH IS OVERRIDDEN and takes every media KEY
  first: the framework's default is not this product's behaviour, holding a
  `KEYCODE_MEDIA_PLAY_PAUSE` press for `ViewConfiguration.getDoubleTapTimeout()`
  and turning two quick presses into `onSkipToNext` while `ACTION_SKIP_TO_NEXT`
  is declared — a hidden delay and an unruled double-tap gesture. So the
  override reads the `KeyEvent` off `Intent.EXTRA_KEY_EVENT`, acts on
  ACTION_DOWN with no repeat (the press timing the product's own hotkeys keep;
  the UP and the repeats are consumed and dropped), maps the keycodes itself
  — PLAY_PAUSE and HEADSETHOOK to the UNDIVIDED `PlayPause` kind, the rest
  one to one — and hands anything it does not map to `super`) and its focus
  listener each call the one
  `native` method, whose JNI entry
  (`Java_com_warptempo_gui_MainActivity_nativeMediaCommand`,
  `platform_android.cpp`, name-based resolution, no `JNI_OnLoad`) does exactly
  this on the UI thread: lock, push onto the backend's queue, unlock, write
  the backend's own eventfd — and touches NOTHING else, the AAudio error
  callback's three-atomics discipline. ONE MUTEX GUARDS BOTH THE SINK POINTER
  AND THE QUEUE (`g_media_mutex` / `g_media_sink`, parked in `init()` and
  cleared in `shutdown()`), so a command can never reach an object being
  dismantled. THE THREE DROP RULES: before `init()` / after `shutdown()` the
  sink is null; an integer outside the kind table is refused at the entry;
  and with no hook installed (between two projects — main.cpp installs it per
  project and CLEARS it at the session tail, the one handler it clears) the
  drained commands go nowhere. The consumer is
  `GuiRenderPlayer::on_media_command` (render-player.md's territory): each
  command becomes THE PLAYER'S OWN KEYS through `synthesize_key`, press and
  release, under `kCarStableCodeBase` = 1000 (recorded beside the keyboard's
  `kStableCodeBase`), so the ordinary `on_key` dispatch runs — no second road;
  the DIRECT ACTS take no key at all, `SeekTo` among them because no keysym
  carries an absolute position. (The head unit's STOP became a real stop on
  2026-08-28, R36 — the player's own Stop key rather than the pause it had
  been mapped to — and became PAUSE AND THEN HOME on 2026-09-01, a direct act,
  when that key and its button retired; the mapping table is
  render-player.md's.)
- **`publish_media_state`** — the push UP, from the ONE owner
  `GuiRenderPlayer::publish_media_state` at every edge where the display
  should change (its inventory is at that declaration) and never per tick.
  Wayland's body is empty. Android's runs on the glue thread, ATTACHED TO THE
  VM ONCE in `init()` (`AttachCurrentThread`, the env cached; detached in
  `shutdown()`, a thread exiting attached being a VM abort), and calls
  `MainActivity.mediaState(boolean active, boolean playing, String title,
  String artist, long durationMs, long positionMs)` through the activity
  instance (`activity->clazz`, a global ref despite its name) with a
  `GetMethodID` looked up once; the strings cross as UTF-16 through
  `NewString` (not `NewStringUTF`, whose modified UTF-8 CheckJNI aborts on a
  four-byte sequence), inside a local frame. That method builds the
  `MediaMetadata` (TITLE = the wav's spelling with its folder, ARTIST and ALBUM
  = the project's name, DURATION) and the `PlaybackState` (PLAYING / PAUSED /
  STOPPED with the position, every action declared, and THE SPEED THE RATE OF
  PLAYBACK — 1.0 for PLAYING and 0.0 otherwise, since a controller
  extrapolates the position off that speed from the moment of the push and a
  resting transport must not have its clock run on), calls
  `setActive(active)` — THE SESSION IS ACTIVE ONLY WHILE THE RENDER PLAYER
  STANDS (R7), created in `onCreate` on the UI thread so its callbacks land
  there and released in `onDestroy` — and owns the AUDIO FOCUS machine:
  `AudioFocusRequest` GAIN with the AAudio stream's own attributes
  (USAGE_MEDIA / CONTENT_TYPE_MUSIC), requested when a push says playing and
  none is held, abandoned when a push says inactive, a refused request logged
  and playback proceeding (the stream is already running). A LOSS is forwarded
  down as `FocusLost` / `FocusLostTransient` and pauses the player — through
  its TRANSPORT DIRECTLY since 2026-08-31 rather than through the key road, a
  direction-named command acting on the transport alone now that Space reads
  the band first (render-player.md's car section owns the split); it is
  "Android's one imposed interrupt", and it always pauses. GAIN is forwarded and
  does nothing — NOTHING RECOVERS BY ITSELF. Ducking stays the framework's
  default, so a navigation prompt ducks rather than pauses — and THE LISTENER
  HAS NO CAN_DUCK ARM, deleted 2026-09-02 under the four-tier review's R-18(e):
  `setWillPauseWhenDucked` is left false, so the system lowers the volume
  itself and never calls the listener for
  `AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK`; the arm that stood there forwarded it
  as a TRANSIENT LOSS, which pauses — the stated policy's opposite in a case
  with no producer. It falls to `default` now, so the absence is the policy.

THIS PHASE IS A MediaSession ALONE, by ruling: no notification, no foreground
service, no background playback, no lock-screen transport — the tablet is a
kiosk on a stand with the app in the foreground — so the manifest gains
nothing (no `<service>`, no `FOREGROUND_SERVICE*` / `POST_NOTIFICATIONS`, no
`res/`). Backgrounding (`APP_CMD_LOST_FOCUS`) does not deactivate the session;
the player standing is the one condition.

"NO BACKGROUND PLAYBACK" IS BUILD SCOPE, NOT BEHAVIOUR (recorded 2026-09-02,
the four-tier review's R-18): it says this build ships no service to keep
sound alive properly, not that sound stops when the app goes behind. A
BACKGROUNDED TABLET KEEPS SOUNDING — `APP_CMD_LOST_FOCUS` has no pause arm
and the AAudio stream runs on — which is right for the car (the head unit's
own screen is what is in front) and is simply what happens elsewhere. And THE
MAIN WINDOW'S OWN AUDITIONS HOLD NO AUDIO FOCUS: only the render player
requests focus, so Space and the A/B audition play beside whatever else the
device is playing and neither ducks nor is ducked. Both are recorded rather
than fixed; a `LOST_FOCUS` → stop-body arm is the shape if the architect ever
wants the first of them.

NO DEVICE PAUSES, IT DOES NOT ADVANCE (the same day): `GuiPlayback` gained
`device_unavailable()` on both backends — true whenever the engine cannot
sound: the AAudio disconnect latch and its refused reopen AND, on both, the
device that never came up (an init that failed; JACK still records nothing for
a server that vanishes mid-play) — and the render player's tick forks on it
BEFORE its natural-end test, pausing in place with "No audio device to play the
wav" on a notification card and a "paused" push, where before a Bluetooth drop read as a
natural end and auto-advanced, and a laptop without pipewire-jack raced
through the folder a wav per tick. THE READ IS A READ (2026-09-02): the tick
keeps `device_unavailable()`, which reopens nothing; the reopen is the launch
press's, `ensure_device_available_for_play` (the playback bullet above), and
the player's own play road reopens through `play()`'s head check as it always
did. THE LAUNCH FACES READ THE NEVER-CAME-UP HALF ALONE, `device_absent()`
(JACK identical to the read; AAudio `device_ready` alone, the latch not a
term): a face must not grey what the press reopens, so a dropped route leaves
Play lit and its press plays.

## Build and freeze posture

`android/app/build_apk.sh` = configure (NDK toolchain, arm64-v8a, API 30
min / 34 target; the COMPILE platform is a third number, 35) → cross-build
(`add_library(warptempo_gui SHARED)` over the
same `WARPTEMPO_GUI_SOURCES` the Linux target draws, three per-backend
arms; `-D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__` mandatory; no
`-march=native`) → javac/d8 → aapt2 → zipalign `-P 16` → apksigner. Deps
are STATIC from `android/prebuilt/arm64-v8a` (gitignored; rebuilt by
`android/deps/build_all.sh` from pinned checksummed sources — fftw double
+NEON+threads, freetype without fontconfig, harfbuzz, pixman, cairo
image+ft); DT_NEEDED is exactly the NDK stable-ABI set — `libdl libm
libaaudio libandroid libnativewindow liblog libc`, `libnativewindow` since the
frame-rate pin (`ANativeWindow_setFrameRate` lives there rather than in
libandroid), and nothing to ship beside the app. targetSdk is PINNED
at 34 (Android gates behavior on it; sideload has no ceiling), the whole
freeze story: a decade-later replacement tablet runs the same APK. It was 35
until 2026-08-27, when the system bars came back and 35's edge-to-edge
enforcement proved unopt-out-able without a `res/` — 36 was never a candidate,
Android 16 revoking `screenOrientation` on a screen this size. The COMPILE
platform stays 35 (`WT_PLATFORM_SDK`, the only `android.jar` installed): the
runtime gates on the stamped target, not on the jar. The
Linux target's flags and object set are byte-identical to before the port.

WHAT THE APK DOES NOT CARRY, and the one behaviour that follows (recorded
2026-09-02, the four-tier review's R-18): GIT. The GitHub recheck shells out
to a `git` binary, and there is none on the tablet, so the remote walk's very
first question — which clone holds the source — has no answer there. THAT IS
NOT A STUB OR AN OMISSION TO FILL: the checkpoint workflow is the authoring
laptop's, the tablet being the glass the work is played and judged on.

**AND `h` WORKS ON THE TABLET SINCE 2026-09-04, ON THE LOCAL WALK** (architect,
from the car on the first real road test, SUPERSEDING "bare `h` REFUSES there,
with the entry's own `History is unavailable: <reason>` card, and the mode's
buttons grey with it"). The view opens on the session's own undo/redo timeline
read as states, and everything that walk supports works there exactly as it
does when bare `g` chooses it on the laptop — `,` / `.`, the diff lane, `'`,
bare `v`, the paired march, Ctrl+Tab. WHAT NEEDS GIT IS REFUSED TRUTHFULLY: the
walk lamp greys and bare `g` cards the bootstrap's own reason, Save and Commit
greys and Ctrl+S cards the same, and the entry itself says nothing on screen
(one stderr line, as ever). github-recheck.md's own section owns the ruling;
HELP's history section says so, this being one of the few places behaviour
differs by host.

## Device facts (Galaxy Tab S10 FE, SM-X520)

Android 16 / One UI 8.0.5, 2304x1440 @ 280 dpi (exactly 1.75x; the
ROADOM rig's layout is reproduced at gui_scale 225 = 1024 logical px
wide, and the icon row fits WHOLE up to gui_scale 232 since the ITERATIONS MENU
WAS DELETED on 2026-09-04 and its two commands came back to the row as a group
of their own — two boxes, one gap and one separator, leaving the walk 993
authored px (250 for the hours the day's earlier REGROUPING had it at 918, the
two view lamps merging into one group and the RESTRICT UNDO TO VIEWPORT lamp
moving to the viewport-class group's tail;
249 for the hours that same day's RESTRICT UNDO TO VIEWPORT lamp had it at 925
in the toolbar group; 258 for the hours the CENTER ON NEXT MARKER lamp before
it had it at 891;
268 for the hours the radio collapse before it had it at 857; 240 at the centered
lamp's 959 from 2026-08-31; 249 at the 2026-08-27
Series relocation's 925, the same walk and the same ceiling reached from the
other direction; 228 at the 1007-px row before that, and 221 for the one
day the row was 1041). THE TABLET'S FIRST-RUN SCALE IS 225, settled on the glass 2026-08-27:
the whole icon row lands (993*2.25 = 2234 of the panel's 2304, 70 px of slack
where the 918-px row had 238) and the layout is the one the redesign was drawn
against. 250 held the template for one
afternoon that day — the architect's question was whether a marker flag is
TAPPABLE, whether the second tap of a double-tap lands on the flag rather than
on the waveform — and he stepped it back that evening: one step past the fit
ceiling was one step too far for the ~3 authored px it cropped off the
rightmost history icon, and with the press road's lengths now scaling with
`gui_scale` the double-tap holds together at 225), 90 Hz panel PINNED (`ANativeWindow_setFrameRate(90, FIXED_SOURCE)` at
every window adoption; the backend ticks at 5 ms, the Wayland rule's own half
of the pinned refresh period, where it took the 60 Hz fallback's 8 ms until
2026-08-27), PAGE_SIZE 4096 (the
16 KB alignment is headroom), `/storage` is 0711 (traversable, never
listable — discovery through `/proc/mounts`), the OTG stick mounts with
`mountFlags=0` and no `/storage/<uuid>` view for any app (Synchronize to
external storage refuses on the tablet until this is solved, 2026-08-28;
`sm list-volumes`: `public:8,81 mounted 067C-8690`; `dumpsys mount`:
`mountFlags=0`, `path=/mnt/media_rw/067C-8690`), one USB-C port (cable and
any OTG device are mutually exclusive; wireless adb for the rest),
`block_usb_lock` blocks USB while locked (reads like Auto Blocker; is
not). The provisioning log is the architect's, outside the repo.
