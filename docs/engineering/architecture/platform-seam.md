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
  public API (65 declarations, proved by a comment-stripped diff of the two
  headers; Android adds exactly one, `synthesize_key`): `platform_wayland.{h,cpp}`
  (Wayland/xkb/cursor/shm/clipboard/pointer-lock, keymap → `GuiKey`) and
  `platform_android.{h,cpp}` (NativeActivity glue, ANativeWindow present,
  AInputQueue → the core, stubs). `platform.h` is the ONE include that
  selects the header; the seven consumers include it and change nothing.
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
  Wayland polls the display fd + timerfd + four worker eventfds; Android
  puts the same fds on the glue's ALooper. `drain_events` is paint-only on
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
  per-frame increment `speed * source_rate / output_rate`, where
  `output_rate` was the JACK graph rate and is now either device's granted
  rate, 0 = the suspended device), the predictor, the domain-offset / bind
  / speed logic — is `playback_common.{h,cpp}`, proved statement-identical
  to the old body with five recorded deviations. `playback.cpp` is the JACK
  device half, `playback_aaudio.cpp` the AAudio one (granted-rate open —
  48 000 on the S10 FE's speaker, natively; LOW_LATENCY granted, burst 96;
  the stop fence exits only on STOPPED / a positively terminal state / the
  dead latch and otherwise keeps waiting; a disconnect marks the stream
  dead and the next `play()` reopens at the new device's rate, no
  auto-resume). NOTHING LOOPS holds on both.
- **Fonts**: `gui_font.h` is the ONE face owner (`GuiFontFamily::Sans/Mono`);
  the ten former `cairo_select_font_face` sites call it. Linux resolves
  through fontconfig (`gui_font_fontconfig.cpp`, byte-identical to before);
  Android through the two bundled Liberation files
  (`gui_font_bundled.cpp`, FT faces over owned copies; a failed install
  aborts before the first paint — a missing asset is a build defect).
- **Entry**: `gui_main(source_path)` (`gui_main.h`) is the one GUI body;
  Linux `main()` is a thin wrapper, Android's `android_main` pins
  `LC_ALL=C` (bionic starts in C.UTF-8), sets `XDG_CACHE_HOME`/`HOME` to
  the app's files dir, installs the fonts, waits for the window and calls
  it with `<externalFilesDir>/source.wav` — the sync layer's convention.
- **Android stubs** (each named at its site with its Wayland twin):
  clipboard over one stored string, pointer capture as no-ops (the notional-x
  FIELD survives and tracks the finger), cursor kinds stored and never
  applied, titles, close.
- **Key repeat on Android** is hard-coded `set_repeat_info(30, kHoldBeatMs)`
  (architect 2026-08-23: labwc's numbers by convention; the platform
  advertises none). Hardware keyboards are out of scope; the owned painted
  keyboard reaches the core through `synthesize_key`.

## The Java sliver

`android/app/java/com/warptempo/gui/MainActivity.java` is the product's ONE
Java class: a `NativeActivity` subclass hiding both system bars
sticky-immersive at create and every focus gain. It is load-bearing — the
taskbar owns the INPUT of the band the bottom row (the transport and the
modal surface) paints into. Every later Java need (the SAF picker's
`onActivityResult`, the clipboard) joins this class as a method. Launch
component: `com.warptempo.gui/.MainActivity`.

## Build and freeze posture

`android/app/build_apk.sh` = configure (NDK toolchain, arm64-v8a, API 30
min / 35 target) → cross-build (`add_library(warptempo_gui SHARED)` over the
same `WARPTEMPO_GUI_SOURCES` the Linux target draws, three per-backend
arms; `-D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__` mandatory; no
`-march=native`) → javac/d8 → aapt2 → zipalign `-P 16` → apksigner. Deps
are STATIC from `android/prebuilt/arm64-v8a` (gitignored; rebuilt by
`android/deps/build_all.sh` from pinned checksummed sources — fftw double
+NEON+threads, freetype without fontconfig, harfbuzz, pixman, cairo
image+ft); DT_NEEDED is exactly the NDK stable-ABI set. targetSdk is PINNED
at 35 (Android gates behavior on it; sideload has no ceiling), the whole
freeze story: a decade-later replacement tablet runs the same APK. The
Linux target's flags and object set are byte-identical to before the port.

## Device facts (Galaxy Tab S10 FE, SM-X520)

Android 16 / One UI 8.0.5, 2304x1440 @ 280 dpi (exactly 1.75x; the
ROADOM rig's layout is reproduced at gui_scale 225 = 1024 logical px
wide, every icon fits up to 220), 90 Hz panel (the backend ticks at 8 ms —
the 60 Hz fallback, pending a Choreographer query), PAGE_SIZE 4096 (the
16 KB alignment is headroom), `/storage` is 0711 (traversable, never
listable — discovery through `/proc/mounts`), one USB-C port (cable and
any OTG device are mutually exclusive; wireless adb for the rest),
`block_usb_lock` blocks USB while locked (reads like Auto Blocker; is
not). The provisioning log is the architect's, outside the repo.
