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
  both-sides member. `gui_scale` and `audio_player` are per-DEVICE
  preferences (settings.md owns the file and its schema), and the values a
  fresh device should start from are the one thing only the platform knows:
  the laptop answers 100 % / `audacious`, Android 225 % / blank (no
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
constant. The one thing it does subtract is `kStatusBarAirPx` (16 DEVICE pixels,
the retune knob), added to the rect's TOP inset inside `resolve_content_rect`
and only when the framework reports a top inset at all, so that a fullscreen
future gets no blank band: the clock sat closer to our first row than to the top
of the panel (architect 2026-08-27), and the rows given up join the top band,
which paints `kRedesignRowGround` — the status bar's own colour and the menu
row's — so bar, air and menu row read as one title strip. Origin, size, damage
and every touch coordinate follow from that one function. THE FRAMEWORK'S RECT ALREADY EXCLUDES BOTH BARS, measured on an AWAKE Tab S10
FE under the architect's own Screen zoom (override density 320):
`window 2304x1268 at (0,76) of surface 2304x1440` — a 60 px status bar plus the
16 px of air we add above, and a 96 px taskbar below. So the backend subtracts
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
later Java need (the SAF picker's `onActivityResult`, the clipboard, the
key-repeat cadence) joins this class as a method. Launch component:
`com.warptempo.gui/.MainActivity`.

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
listable — discovery through `/proc/mounts`), one USB-C port (cable and
any OTG device are mutually exclusive; wireless adb for the rest),
`block_usb_lock` blocks USB while locked (reads like Auto Blocker; is
not). The provisioning log is the architect's, outside the repo.
