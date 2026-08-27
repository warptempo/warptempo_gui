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
  public API (67 declarations as of 2026-08-27 — 13 `using` aliases and 54
  members including the constructor and destructor — counted as the
  semicolon-terminated declarations in the `public:` section of each header
  with `//` comments and blank lines stripped, and the identity proved by
  diffing those two stripped sections, which come out line-for-line equal;
  there is NO Android-only member any more — the on-screen keyboard's
  two, `wants_onscreen_keyboard` and `synthesize_key`, are declared on both and
  answered differently, which is the seam's own shape rather than an
  exception, and `device_config_defaults` is a third of the same kind):
  `platform_wayland.{h,cpp}`
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
  per-frame increment `source_rate / output_rate`, where
  `output_rate` was the JACK graph rate and is now either device's granted
  rate, 0 = the suspended device; the increment carried a `speed *` factor
  until 2026-08-27, when `playback_speed` retired whole and left the RATE
  ratio alone behind it), the predictor, the domain-offset / bind
  logic — is `playback_common.{h,cpp}`, proved statement-identical
  to the old body with five recorded deviations. `playback.cpp` is the JACK
  device half, `playback_aaudio.cpp` the AAudio one (granted-rate open —
  48 000 on the S10 FE's speaker, natively; LOW_LATENCY granted, burst 96;
  the stop fence exits only on STOPPED / a positively terminal state / the
  dead latch and otherwise keeps waiting; a disconnect marks the stream
  dead and the next `play()` reopens at the new device's rate, no
  auto-resume). NOTHING LOOPS holds on both.
- **The device config's first-run template**: `GuiPlatform::device_config_defaults()`,
  ONE static accessor each backend answers, and the seam's third
  both-sides member. `gui_scale` and `audio_player` are per-DEVICE
  preferences (settings.md owns the file and its schema), and the values a
  fresh device should start from are the one thing only the platform knows:
  the laptop answers 100 % / `audacious`, Android 250 % / blank (no
  spawnable player exists there, so `l` reports "No audio_player set"
  through the ordinary opt-out road). `gui_main` asks it before
  `GuiPlatform::init` and stamps the file if none exists, which is what
  keeps the GUI proper free of the `#ifdef` the alternative would need.
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
  it with THE CURRENT PROJECT'S SOURCE — the app opens that and nothing else,
  there being no file picker on this platform. The convention lives under
  `<externalDataPath>` (`/sdcard/Android/data/<pkg>/files`, which adb pushes
  into with no permission granted): a one-line `current` names a folder under
  `projects/`, mirroring the laptop's own `projects/<name>/`, and THE SOURCE IS
  THE ONE `.wav` THERE WHOSE STEM HAS A `.warpmarkers` SIBLING (the sync
  script's own rule for "the source", so the two ends cannot disagree; a
  finished render beside it carries no sidecars and never matches). EVERY
  FAILURE IS FATAL — the sync script is the tree's one producer, so a fallback
  arm would have nothing that could ever place files where it looked. The app
  half is `platform_android.cpp`'s `resolve_source_path`, which is authoritative.
- **Android stubs** (each named at its site with its Wayland twin):
  clipboard over one stored string, pointer capture as no-ops (the notional-x
  FIELD survives and tracks the finger), cursor kinds stored and never
  applied, titles, close.
- **Key repeat on Android** is hard-coded `set_repeat_info(30, kHoldBeatMs)`
  (architect 2026-08-23: labwc's numbers by convention; the platform
  advertises none). Hardware keyboards are out of scope; the owned painted
  keyboard reaches the core through `synthesize_key`.

## The on-screen keyboard

The glass has no hardware keys, so the product paints its own (2026-08-27): a
four-row Maliit-shaped surface standing while ANY OF THE SEVEN TEXT EDITORS
stands on a backend that asks for one (`wants_onscreen_keyboard`), sitting
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
**Cancel**, **Enter**. **Backspace** is on both layers (each layer's own row 2
ends with one), and **Space**, **Cancel** and **Enter** are on both by
construction — row 3 is one array shared by the two. **Shift** is the LETTER
layer's alone: the symbol layer's row 2 opens with a `Role::Blank` slot in its
place, which is the whole answer to what shift would do on a symbol page. The
symbol layer's SECOND SPACE is not that row's leading slot either but a
deliberate duplicate CHARACTER key beside its `_`, so a hand already in the
symbol layer for the `/` of `12 7/8` need not go looking for the bar. The function keys wore unmodified Breeze glyphs for their first day
and read OVERSIZED beside the letter caps — a 22-unit icon scaled to the key's
own height beside a 12pt letter — and a full-width row has room for words, so
they say what they do. `onscreen_keyboard::cap_word` is the words' ONE owner,
the painter's icon branch is gone, and the five glyphs that surface alone read
(keyboard-caps-disabled / keyboard-caps-enabled / keyboard-enter /
keyboard-spacebar / edit-clear-locationbar-rtl) went with it — enumerators,
defs and committed assets, the roster 51 → 46. SHIFT'S ONE-SHOT ARM IS THE
FACE: the cap says "Shift" armed or resting, and what says the arm is the key's
ARMED FACE — `kRedesignSelectedFill` under a `kRedesignLine` frame, the icon
row's own lit-toggle face, which this key and the layer toggle already wore off
their lamp bits — together with the letter caps, every one of which turns
capital while the arm stands. No new colour; the caps pair's stateful glyph is
what the face replaced.

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
image+ft); DT_NEEDED is exactly the NDK stable-ABI set — `libdl libm
libaaudio libandroid libnativewindow liblog libc`, `libnativewindow` since the
frame-rate pin (`ANativeWindow_setFrameRate` lives there rather than in
libandroid), and nothing to ship beside the app. targetSdk is PINNED
at 35 (Android gates behavior on it; sideload has no ceiling), the whole
freeze story: a decade-later replacement tablet runs the same APK. The
Linux target's flags and object set are byte-identical to before the port.

## Device facts (Galaxy Tab S10 FE, SM-X520)

Android 16 / One UI 8.0.5, 2304x1440 @ 280 dpi (exactly 1.75x; the
ROADOM rig's layout is reproduced at gui_scale 225 = 1024 logical px
wide, and the icon row fits WHOLE up to gui_scale 249 since the 2026-08-27
Series relocation cut it to 925 authored px — it fitted only to 220 before
that. THE TABLET'S FIRST-RUN SCALE IS 250, one step past that fit ceiling and
chosen on the glass 2026-08-27: the architect's question is whether a marker
flag is TAPPABLE — whether the second tap of a double-tap lands on the flag
rather than on the waveform — which the rig's cursor-width 225 does not settle.
The icon row's rightmost authored pixels therefore crop, which is the standing
crop-at-the-floor allowance at `kMinWindowWidthPx` and the sanctioned casualty
it already names, not a new rule), 90 Hz panel PINNED (`ANativeWindow_setFrameRate(90, FIXED_SOURCE)` at
every window adoption; the backend ticks at 5 ms, the Wayland rule's own half
of the pinned refresh period, where it took the 60 Hz fallback's 8 ms until
2026-08-27), PAGE_SIZE 4096 (the
16 KB alignment is headroom), `/storage` is 0711 (traversable, never
listable — discovery through `/proc/mounts`), one USB-C port (cable and
any OTG device are mutually exclusive; wireless adb for the rest),
`block_usb_lock` blocks USB while locked (reads like Auto Blocker; is
not). The provisioning log is the architect's, outside the repo.
