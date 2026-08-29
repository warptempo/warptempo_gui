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
  public API (76 declarations as of 2026-08-28 — 13 `using` aliases and 63
  members including the constructor and destructor — counted as the
  semicolon-terminated declarations in the `public:` section of each header
  with `//` comments and blank lines stripped, and the identity proved by
  diffing those two stripped sections, which come out line-for-line equal;
  there is NO Android-only member any more — the on-screen keyboard's
  two, `wants_onscreen_keyboard` and `synthesize_key`, are declared on both and
  answered differently, which is the seam's own shape rather than an
  exception, `device_config_defaults` is a third of the same kind,
  `removable_volume` (Synchronize to external storage, below) is a fourth,
  and the car's two (`set_on_media_command`, stored and never fired on
  Wayland — the STORED-HOOK shape `set_on_close` carried on the Android side
  until BACK became its producer there on 2026-08-29, leaving this the seam's
  one hook a backend never fires — and `publish_media_state`, a no-op body
  there — The car, below) are the fifth and sixth):
  `platform_wayland.{h,cpp}`
  (Wayland/xkb/cursor/shm/clipboard/pointer-lock, keymap → `GuiKey`) and
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
  period is the ONE wakeup; the two software deadlines (key repeat, the
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
  a disconnect marks the stream dead and the next `play()` reopens at the
  new device's rate, no auto-resume). THE AAUDIO STREAM IS OPENED ONCE,
  STARTED ONCE AND NEVER STOPPED BETWEEN PLAYS (architect 2026-08-27, on
  glass — starting a stream unmutes the device's output path and that
  transient was audible as a click at the head of every audition): it is
  started at open, stopped only where it is about to be closed (shutdown,
  the dead-stream reopen — `close_stream` holds the file's one
  `requestStop`), and between plays it runs while the callback's `playing`
  gate writes silence and reads no sample. So the fence is now the SAME
  PROOF ON BOTH BACKENDS — counting callback invocations, two after the
  flag is lowered, unbounded and hanging rather than weakening, with
  AAudio's escape on a dead or positively terminal stream (no callback
  left to count) — and `stop()` touches neither device. NOTHING LOOPS
  holds on both.
- **The device config's first-run template**: `GuiPlatform::device_config_defaults()`,
  ONE static accessor each backend answers, and the seam's third
  both-sides member. The FOUR keys it stamps are per-DEVICE preferences
  (settings.md owns the file and its schema), and the values a
  fresh device should start from are the one thing only the platform knows:
  the laptop answers 100 % and the clone's own `projects/`, Android 225 %
  and `<externalDataPath>/projects`; both stamp `kDefaultProjectsRepo` and a
  blank `last_project`. (A FIFTH key, `audio_player`, stood here until
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
  `projects_path`, and File → Open is the picker on both platforms. The
  `current` file and `resolve_source_path` are RETIRED (a stale `current` on
  the device is simply never read). What the backend still owns of the sync
  convention is WHERE the projects are: its template stamps
  `<externalDataPath>/projects` (`/sdcard/Android/data/<pkg>/files/projects`,
  which adb pushes into with no permission granted) as the projects path.
- **The loop contract** (`platform.h`, authoritative): ONE `GuiPlatform` per
  process, MANY `run()` calls. `gui_main` is a loop — everything ONE PER
  PROCESS (the signals, the device config, the scale install, the platform and
  its `init`, the render cache) outside it; everything ONE PER PROJECT
  (`run_project`, main.cpp: AppState, audio, playback, the caches, the workers,
  the handlers, every callback) built inside, run, torn down in today's order.
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
- **Android stubs** (each named at its site with its Wayland twin):
  clipboard over one stored string, pointer capture as no-ops (the notional-x
  FIELD survives and tracks the finger), cursor kinds stored and never
  applied, titles. **CLOSE LEFT THAT LIST ON 2026-08-29**: THE ANDROID CLOSE IS
  BACK (architect — the tablet's BACK asks the unsaved-work question the
  laptop's X asks). `AKEYCODE_BACK` is consumed WHOLE in `on_input_event` —
  the one KeyEvent this backend answers — and its ACTION_UP fires the seam's
  `CloseCallback`, the very hook Wayland fires for `xdg_toplevel.close`, so the
  consumer's one close road runs: `GuiPrompt::request_close` prompts on a dirty
  tab and completes at once on a clean one, and the exit that follows is
  `request_exit`'s own (`should_exit_` plus `ANativeActivity_finish`, the one
  asker `android_main`'s tail shares), so `run()` returns, `gui_main` returns
  and the activity goes exactly as Quit's does. The DOWN is consumed too, not
  merely ignored: the framework's own back handling runs off the UP after
  tracking the DOWN, so an unconsumed DOWN would leave the system holding a
  tracked press it could still act on beside a prompt we had just raised; DOWN
  repeats ride the consume and fire nothing. Firing from the input drain is
  safe under `drain_looper`'s "nothing may block" rule — the callback raises a
  bottom-row prompt or requests an exit and returns. Every other key still
  returns 0 (hardware keyboards are out of scope, `touch.md`), and the road
  into the core's key path stays `synthesize_key`. **BACK ASKS; A DESTROY
  CANNOT** is the residual loss, recorded at `drain_looper`'s
  `destroyRequested` arm: `APP_CMD_DESTROY` is the system stating the activity
  is already going, with nobody left to answer a prompt, so a session killed
  from the task switcher still goes unasked. Predictive back does not take this
  road away: the manifest declares no `android:enableOnBackInvokedCallback` and
  targets SDK 34, so the framework dispatches the legacy `KEYCODE_BACK` to the
  activity — which is the mechanism BACK already left the app by (the key arm
  returned 0 and the system's default finished the activity), measured on the
  tablet (`android/NOTES.md`).
- **Key repeat on Android** is hard-coded `set_repeat_info(30, kHoldBeatMs)`
  (architect 2026-08-23: labwc's numbers by convention; the platform
  advertises none). Hardware keyboards are out of scope; the owned painted
  keyboard reaches the core through `synthesize_key`.

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
**Cancel**, **Enter**, **Tab**. **Backspace** is on both layers (each layer's own row 2
ends with one), and **Space**, **Cancel** and **Enter** are on both by
construction — row 3 is one array shared by the two. **Shift** is the LETTER
layer's alone: the symbol layer's row 2 opens with **Tab** in its place
(2026-08-27, with File → Open project — the one key the letter layer has no
room for, and what the product's prompts COMPLETE on; a bare `GuiKeys::Tab`
through the same `synthesize_key` road; the blank role is deleted with its last
slot). IT IS NO LONGER THE OPEN PROMPT'S GLASS ROAD: that prompt's PICKER
landed 2026-08-28 and takes this keyboard's own band, so the glass gesture is
File → Open project, tap the row — the tap's lift both highlights and opens
it, the row's Cancel button its only other reach — and typing a name is the
laptop's.
The key stays for every OTHER prompt's completion and ring walk.
The symbol layer's SECOND SPACE is not that row's leading slot either but a
deliberate duplicate CHARACTER key beside its `_`, so a hand already in the
symbol layer for the `/` of `12 7/8` need not go looking for the bar. The function keys wore unmodified Breeze glyphs for their first day
and read OVERSIZED beside the letter caps — a 22-unit icon scaled to the key's
own height beside a 12pt letter — and a full-width row has room for words, so
they say what they do. `onscreen_keyboard::cap_word` is the words' ONE owner,
the painter's icon branch is gone, and the five glyphs that surface alone read
(keyboard-caps-disabled / keyboard-caps-enabled / keyboard-enter /
keyboard-spacebar / edit-clear-locationbar-rtl) went with it — enumerators,
defs and committed assets, taking the roster from 51 to 46 AT THAT MOMENT (it stands at 51 again today, `kIconCount` in icons.h — the transport and player glyphs added since). SHIFT'S ONE-SHOT ARM IS THE
FACE: the cap says "Shift" armed or resting, and what says the arm is the key's
ARMED FACE — `kRedesignSelectedFill` under a `kRedesignLine` frame, the icon
row's own lit-toggle face, which this key and the layer toggle already wore off
their lamp bits — together with the letter caps, every one of which turns
capital while the arm stands. No new colour; the caps pair's stateful glyph is
what the face replaced.

## Synchronize to external storage

`GuiPopupAct::SyncExternal`, the File menu's one chordless row (architect
2026-08-27, landed 2026-08-28 in `b92ea097`/`95ea84d4`), mirrors the open
project onto the one mounted removable volume, found and never configured.
The lift runs `GuiInputHandler::synchronize_to_external_storage`
(`input_key_dispatch.cpp`), which refuses silently through the Open row's own
three gates (a prompt or editor standing, the `h` history view, a load in
progress) and with nothing loaded; it is LEGAL ON A READ-ONLY TAB and STOPS
NO PLAYBACK, since it authors nothing and writes outside the project
entirely. A second dispatch while one is already running writes
`Synchronization already running` to the status line and stops there — the
checkpoint act's own single-in-flight shape, answered in words since a menu
item never greys. Otherwise it asks `GuiPlatform::removable_volume()` for the
destination; that call's own refusal (`No removable volume mounted` /
`Several removable volumes mounted: a, b`) ends the act there too. Passing
both, it composes the job — the render folder's deliverable wav, composed
exactly as a render composes it, and the `tmp/` batch root — writes
`Synchronizing to <volume>...` and dispatches to `GuiExternalSyncWorker`.

THE MIRROR'S LAYOUT AND SCOPE are `external_sync.h`'s whole statement:
`<volume>/<project name>/` holds the deliverable directly and each `tmp/`
batch folder AS ITSELF (folder name and NN numbering verbatim), wav files
only — no sidecars, no `.fingerprint`, no `peaks/`, the volume being played
from and not authored in. Copies run first; afterward every file and folder
under that one destination folder which is not in the set is deleted, so an
act interrupted mid-way (a pulled stick, a killed process) leaves the volume
with at most EXTRA files, never fewer. The scope is that destination folder
alone, never the volume root or another project's folder on it, which is
what lets one stick carry several projects side by side. Nothing is skipped
or retried on an mtime guess: the stick is carried between two clocks, so
every file is copied on every act — each onto a staging sibling that is
renamed onto the final name only once the copy is complete, so a failed or
interrupted copy leaves the previous file on the volume whole.

THE FOUR STRICTNESS RULES are `external_sync.h`'s head, stated there once and
nowhere else: the mirror deletes only against a listing it finished (any
enumeration or status error other than an absent optional root ends the act
before a single deletion, a destination-side one included, which therefore
cannot report success — SO THE DELETION IS TWO PASSES, classifying the whole
destination into kept, unkept link and unkept subtree, top level and then each
kept batch folder, before its first removal, which is also why no
`directory_iterator` is ever live while its own directory is being changed); no
destination symlink is ever followed, which makes the scope claim above true by
construction (THE VOLUME ITSELF IS THE FIRST NAME CHECKED, every path in the act
being composed under it, and a link at one of the act's own names is a REFUSAL
and not a deletion, an unkept link being removed as a link) — the checks run at
the act's start and not again at each use, that check-then-use window being an
ACCEPTED COST, a hand on a mounted volume mid-act and so the adversarial class
this product never backstops; every copy is staged; and what is kept is kept by
filesystem identity (`std::filesystem::equivalent`) rather than by spelling, the
volume being case-insensitive vfat.

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
the main thread through `on_external_sync_complete`, which writes it to the
status line and nothing else: `Synchronized <N> file(s) to <path>` on
success, or, on the first failure of any kind, the path it was reading or
writing and the system's own words (`Cannot read '<path>': <...>` / `Could
not copy '<path>': <...>` / `Could not remove '<path>': <...>`), or one of the
symlink rule's own three lines (`'<path>' is a symbolic link` / `'<path>' is
not a directory` / `'<path>' is not a regular file`). A FAILURE IS A STATUS LINE ONLY, never the permanent critical chip —
that chip is the checkpoint act's, whose failure needs the terminal; a failed
synchronization is retried by pressing the row again.

THE SEAM GREW TWO MEMBERS FOR IT, declared identically on both backends
(contract at `platform_wayland.h`, which owns it) and each answered
per-backend:

- **`GuiPlatform::removable_volume()`** — static, needing no window, like
  `device_config_defaults`. EACH BACKEND OWNS ITS DISCOVERY, the COUNTING is
  shared (`sole_removable_volume`, `external_sync.h`): zero candidates
  answers `No removable volume mounted`, several answers `Several removable
  volumes mounted: a, b` naming them (sorted and de-duplicated once, so two
  mount-table lines naming one mount point are one volume, not several).
  LAPTOP: the directory entries under `/run/media/<user>/`, the udisks mount
  root (`<user>` from `getpwuid(geteuid())`, `$USER` the fallback spelling),
  each read with `symlink_status` so a LINK THERE REFUSES with the mirror's own
  `'<path>' is a symbolic link` — udisks mounts a real directory for every
  volume, so a link at that root is a hand's work, and neither counting it nor
  passing over it in silence would be honest (rule 2 asked on the discovery
  side; an entry simply gone by the time it is read is no volume and no fault);
  ENOENT on the root itself is the ONE error that honestly means zero — udisks
  creates the directory at the first mount and removes it with the last —
  and any other read failure refuses out loud with the system's own words
  rather than counting as empty. ANDROID: the `/storage/<name>` mount points
  in the process's own mount table (`/proc/self/mounts`) whose `<name>` is
  neither `emulated` (the app-visible view of the device's own internal
  storage) nor `self` (the per-process mount namespace's own link). THIS IS
  THE MOUNT TABLE AND NOT `opendir("/storage")`, and that is a fact of the
  platform rather than a preference: `/storage` is `drwx--x--x` to the app's
  uid — TRAVERSABLE BUT NOT LISTABLE — so a directory listing there answers
  EACCES, a permission the app was denied and not an empty device, measured
  on the tablet 2026-08-28 with the stick mounted (the listing road answered
  `No removable volume mounted` where the mount-table road answers
  correctly). Those candidates are mount points and so real directories by
  construction, which is why that side carries no symlink test of its own. A
  `/mnt/media_rw/<name>` line is NEVER A CANDIDATE: that is
  vold's own mount and the app's uid cannot open it — All-files access
  reaches the volume through the `/storage/<uuid>` view alone — so a device
  where only that line appears has nothing this app can write and stays `No
  removable volume mounted` rather than a path that would fail at the first
  copy. An unreadable mount table refuses out loud too, for the laptop
  root's own reason.
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
`mountFlags=0`, not VISIBLE, so no `/storage/<uuid>` view exists for ANY app
and `removable_volume()` on Android finds nothing — the act refuses `No
removable volume mounted` on the tablet until this is solved (2026-08-28).
THE SAF ROAD THROUGH THE JAVA SLIVER — a Storage Access Framework picker
(`ACTION_OPEN_DOCUMENT_TREE`) granting a scoped tree URI regardless of
`mountFlags` — is the design's named contingency for this and IS NOT BUILT.
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
`window 2304x1268 at (0,76) of surface 2304x1440, tick 5 ms`.

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
later Java need (the SAF picker's `onActivityResult`, the clipboard) joins
this class as a method, and the first one did on 2026-08-28: THE CAR'S
MediaSession (the section below), which brought the class its `static {
System.loadLibrary("warptempo_gui"); }` initialiser (NativeActivity's own
dlopen does not register the library for name-based JNI resolution), its
one `native` method `nativeMediaCommand(int, long)`, the instance method
`mediaState(...)` the native side calls up, an inner
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
  `SeekTo` is the one direct act, no keysym carrying an absolute position. (The
  head unit's STOP became a real stop on 2026-08-28, R36 — the player's own
  Stop key rather than the pause it had been mapped to; the mapping table is
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
  down as `FocusLost` / `FocusLostTransient` and pauses the player through the
  same key road ("Android's one imposed interrupt"); GAIN is forwarded and
  does nothing — NOTHING RECOVERS BY ITSELF. Ducking stays the framework's
  default, so a navigation prompt ducks rather than pauses.

THIS PHASE IS A MediaSession ALONE, by ruling: no notification, no foreground
service, no background playback, no lock-screen transport — the tablet is a
kiosk on a stand with the app in the foreground — so the manifest gains
nothing (no `<service>`, no `FOREGROUND_SERVICE*` / `POST_NOTIFICATIONS`, no
`res/`). Backgrounding (`APP_CMD_LOST_FOCUS`) does not deactivate the session;
the player standing is the one condition.

NO DEVICE PAUSES, IT DOES NOT ADVANCE (the same day): `GuiPlayback` gained
`device_unavailable()` on both backends — true whenever the engine cannot
sound: the AAudio disconnect latch and its refused reopen AND, on both, the
device that never came up (an init that failed; JACK still records nothing for
a server that vanishes mid-play) — and the render player's tick forks on it
BEFORE its natural-end test, pausing in place with "No audio device" on the
status line and a "paused" push, where before a Bluetooth drop read as a
natural end and auto-advanced, and a laptop without pipewire-jack raced
through the folder a wav per tick.

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

## Device facts (Galaxy Tab S10 FE, SM-X520)

Android 16 / One UI 8.0.5, 2304x1440 @ 280 dpi (exactly 1.75x; the
ROADOM rig's layout is reproduced at gui_scale 225 = 1024 logical px
wide, and the icon row fits WHOLE up to gui_scale 249 since the 2026-08-27
Series relocation cut it to 925 authored px — it fitted only to 220 before
that. THE TABLET'S FIRST-RUN SCALE IS 225, settled on the glass 2026-08-27:
the whole icon row lands (925*2.25 = 2081 of the panel's 2304) and the layout
is the one the redesign was drawn against. 250 held the template for one
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
