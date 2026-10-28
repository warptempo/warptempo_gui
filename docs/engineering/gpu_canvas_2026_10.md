# The GPU canvas — design record (2026-10-13)

The design of the tablet's GPU-drawn waveform canvas, the arc the October
pan/pinch campaign (`gui_perf_campaign_2026_10.md`) ends in. This document
holds what the code comments deliberately leave out: the measurement that
decided the arc, the inventory the design rests on, the rulings, and the
proof method. Current behavior and its rationale live at the owning symbols
(`GuiPlatform` in `src/gui/platform_android.cpp`, the canvas sink in
`src/gui/render.h`); this is the record of how they came to be.

## 1. The measurement that decided it

A throwaway frame counter (never committed) logged, once per second during
the architect's own pinch and pan over tab B of the 550 - 1 project
(~670 markers, the magnification lamp on, HEAD 7d275895 on the tablet,
2026-10-13 ~07:39, 99 seconds):

| quantity | mean | max |
|---|---|---|
| frames posted per second | 50 (median 54) | 87 |
| touch events per second | 88 | 101 |
| touch events per frame | 1.76 | |
| latest touch event → its post | 18.4 ms | 29.3 ms |
| touch event → its handling | 5.9 ms | 17.6 ms |
| `ANativeWindow_lock` blocking | 1.3 ms | 12.4 ms |
| `paint_one_frame` (pre-paint hook through the post) | 8.9 ms | 21.1 ms |

The verdict is THROUGHPUT, not queue latency: the window's buffer queue
blocked the loop for about a millisecond a frame, while the paint took 8.9 ms
and the 1.76 touch events per frame each took their own synchronous rebuild
in the event drain before it. At 90 Hz the budget is 11.1 ms; the loop spent
about 20. The GPU canvas attacks the paint (the plate blit, the canvas fill,
the present copy — 60 % of the frame after 94d92f3b); one rebuild per frame
(P4) attacks the other half.

MILESTONE 1 MEASURED the same way (his pinch, 55 seconds of touch, the
build ce3cee66 with the counter): 46 frames per second mean, 81 max; the
swap blocked 0.7 ms mean; the paint 12.9 ms mean against 9.0 before. The
texture road alone is SLOWER than the memcpy present: a pan step's damage
is canvas-sized (~10 MB) and the driver's upload costs more per byte than
the NEON row copy did. This is the hazard §6 names, and Milestone 2's
reason: once the canvas leaves the upload (§3.5) a step uploads ~1.4 MB and
the plate blit, the canvas fills and the present copy leave the CPU frame.

MILESTONE 2 AND THE FACE-BOX CUT MEASURED with the same counter, his pinches
of 2026-10-13 (the frame rate per second depends on the gesture; the latency
and the paint are the stable columns). THE SAMPLE SELECTION DIFFERS from the
first table above, which takes every second with touch motion at all
(`motion > 0`, 99 seconds); this table takes the seconds of ACTIVE gesture,
`motion >= 50` touch events in the second, each value the unweighted mean of
the per-second means over the intervals counted in its row. That is why its
7d275895 row (the same archived log, 95 of the 99 seconds) reads 52 / 18.6 /
9.0 where the first table reads 50 / 18.4 / 8.9; every row reproduces from its
archived log under the stated filter:

| build | intervals | frames/s mean | max | latest touch → post | paint per frame |
|---|---|---|---|---|---|
| 7d275895, the CPU canvas | 95 | 52 | 87 | 18.6 ms | 9.0 ms |
| ce3cee66, the texture alone | 55 | 46 | 81 | 22.0 ms | 12.9 ms |
| 2b12b9fb, the GPU canvas | 48 | 56 | 90 | 10.9 ms | 7.5 ms |
| 4c991a4b, + the face boxes over their unmasked spans | 40 | 52 | 87 | 10.0 ms | 6.7 ms |

(Under `motion > 0` the 2b12b9fb run is 51 seconds at 52.6 frames/s, 10.5 ms,
7.3 ms, and the 4c991a4b run 41 seconds at 51.7, 10.0, 6.7; ce3cee66's 55
seconds all qualify under both filters, so its row is the same either way.
The latency and paint orderings of the four builds hold under either filter;
the frames-per-second ordering does not, 7d275895 and 4c991a4b tying at 52
under `motion >= 50` but reading 50.2 against 51.7 under `motion > 0`.)

His verdict on the glass (2026-10-13 ~14:50): the pinch over ~670 markers
"feels close to flagless history walk". The profile of 2b12b9fb under
injected pans (the canvas gone from the frame): the flag cache rebuild
30 % — the face boxes 12.8 (cut in 4c991a4b), the triangles 8, the text
7.8 — the plate columns 5.4, present 9.8 (the swap 5.1), the lanes' paint
14.7. What stays on the CPU and is accepted: the flag cache rebuilt per
step (P3 rejected, re-openable), the lanes' paint and their upload, the
plate's column bounds.

## 2. The inventory (HEAD 7d275895)

Every road that puts pixels inside `waveform_area` was enumerated and read
(the painter table is reproduced from that inventory, verified at the code):

| painter | draws | antialias | class |
|---|---|---|---|
| `render_background`, `paint_canvas_column_frame` (interior part: its face and, since 2026-10-16, the two wells' divider lines, which render_waveform_divider repaints) | ground fills the canvas covers | integral rects | overdraw |
| `render_canvas` | canvas ground, the grid rows | NONE | solid |
| `render_center_lines` | one row per channel | NONE | solid |
| `render_waveform_divider` | the stereo divider's three rows (2026-10-16; the press seam's line until then), painted after the plate and the center lines so a plate from before a resize never crosses them | NONE | solid |
| `paint_waveform_plate` | the plate surface blitted OVER at an integral offset | binary alpha | solid |
| `paint_phase_reset_overlay_ring` | dotted runs on the first and last row and the band's last column | NONE | solid |
| `paint_marker_stem_runs`, `paint_marker_stems` | dotted columns (rows ≡ 7 and ≡ 3 mod 8 from the canvas top), the `h` view's joining runs | NONE / direct words | solid |
| `paint_playheads` | dots every 4 rows, phase 1 | integral rects | solid |
| `paint_scanner`, `paint_strip_drag_anchor` | one solid column each | integral rects | solid |

No antialiased pixel is ever put inside the canvas interior: every mark is a
solid converted color word on whole device pixels (the rule ON WAVEFORM →
UNSCALED, `waveform_line_px` = 1). The view bar, the ruler, the cue
triangles, the flag blit and the flag editor stay in their lanes; the flag
blit is clipped to the top strip, which ends at the canvas's first row.

Every overlay that covers canvas rows — the notification cards, the color
picker card, a tooltip flipped above row 8, a tall pull-down, the Settings
combo list, the on-screen keyboard and a dialog on it, the folder overlay —
is an OPAQUE INTEGRAL RECTANGLE whose antialiasing (text, mitred bevels,
icons) blends only against its own ground, never against a canvas pixel.
(That list is the inventory of its time, HEAD 7d275895. The color picker
card, the Settings combo list and the dialog standing on the keyboard were
removed 2026-10-14 — the picker with the preset files, the combo list and
the taskbar dialog in e7a77ddb, when every modal took the modal family's
box over the canvas column, the keyboard inside the frame; the box is the
same kind of opaque rectangle, and the property stands.)

The display transform (sRGB → Display-P3 while True Colors is on) runs AT
THE SOURCE, per color word, at each painter's entry; `present()` copies
bytes and swaps R and B. The plate stores converted words.

## 3. The design

THE PRINCIPLE: a hybrid, never a cairo → GLES port. The GPU draws what is
solid and whole-pixel; the CPU keeps painting what is antialiased, exactly as
today, and the GPU only places those pixels. Pixel identity is PROVED by
`adb screencap` comparison, never argued (§5).

1. **One producer per window.** An `ANativeWindow` connected to an EGL
   surface cannot also be CPU-locked, so the WHOLE window goes through GL:
   the CPU backbuffer (the persistent cairo ARGB32 image, every lane painted
   as today) is uploaded as a texture by damage rect and drawn as one
   full-screen textured triangle; the canvas interior is drawn by GL
   primitives; the frame is swapped. There is no second surface and no
   SurfaceView.
2. **The texture is placed, never filtered.** (Milestone 1 proved this on
   the tablet 2026-10-13: eleven screencap states byte-identical between
   the CPU and GL variants; the driver ANGLE on Vulkan over the Xclipse
   540, OpenGL ES 3.2; the Display-P3 passthrough accepted and the layer's
   dataspace DISPLAY_P3 in the compositor's dump under both.) The fragment
   shader fetches
   texels by integer coordinate (`texelFetch`, the row flipped against GL's
   bottom-up framebuffer) and swizzles cairo's BGRA bytes to RGBA, writing
   alpha 1. Dithering is disabled, blending is off, the framebuffer is plain
   RGBA8 (never an sRGB-encoded one). An 8-bit unsigned-normalized texel
   round-trips exactly, so the window shows the backbuffer's bytes.
3. **The canvas is a draw list.** The canvas painters paint through ONE
   sink with two implementations: the cairo sink (the CPU reference, today's
   pixels) and the GL sink, which records solid rects and dotted runs
   (column or row, period, phase origin, color word) and the plate's
   per-column bar bounds. The GL frame draws the canvas FIRST and the
   texture over it (its alpha-0 texels discarded, §3.4): the canvas
   ground, the grid, the plate (one quad over the canvas with a per-column
   bounds texture and the same priority select `render_waveform` makes —
   the lit inner bar's interior ink, its boundary rows outline, the outer
   bar ink, else nothing), the center lines, the ring, the stem runs, the
   stem dots, the playhead dots, the scanner and the anchor, in the CPU's
   order, every write opaque, later wins. Dot runs discard by
   `(row − origin) mod period ≠ phase` in integer math. Every color is the
   same converted word the cairo sink would store (`argb32_opaque_word` of
   `display_color`). THE PLATE'S BOUNDS HAVE A LIFETIME OF THEIR OWN
   (2026-10-14): every publication of a plate stamps it with a process-wide
   revision (`GuiPlateColumns::revision`, `next_plate_revision`), the list
   sink copies the bounds into the list only when the revision moved, and
   the GL owner repacks and re-uploads the bounds texture only when its key
   — the revision, the plate records' spans in list order, the texture's
   dimensions — differs from the texture's; a scanner-only frame rebuilds
   the list and re-uploads the instances alone.
4. **The canvas interior is transparent in the backbuffer.** Under the GL
   build the CPU clears the interior inside the canvas's paint area to
   0x00000000 instead of filling and painting it — AND SKIPS THAT CLEAR
   WHEN THE AREA IS KNOWN CLEAR (2026-10-14): the paint handler records
   that the last painted frame stood no floating surface and left the
   whole area 0, and a frame on the same area with no floater standing
   clears nothing; a floater now or last frame, a changed paint area, the
   first frame of a project set and any resize make it unknown and the
   frame clears as before (the rule and the overlay inventory at
   on_redraw's canvas block, paint_handler.cpp); the overlays paint over
   it opaque as today. The texture draw DISCARDS alpha-0 texels, so wherever
   the CPU painted an overlay the overlay shows and everywhere else the GL
   canvas shows. A texel with alpha strictly between 0 and 255 inside the
   canvas is a design violation (§2's opaque-rectangle property); the
   harness is its detector.
5. **The upload excludes the canvas.** Each damage rect uploads minus the
   canvas's paint area, the canvas's clip. The whole rect uploads only
   when `GuiGlCanvas::present_frame` says so: a remade texture's first walk,
   a changed clip, a floating surface standing now or at the last walk
   (`floating_surface_stands`; a floater's change rides a containing rect),
   or an empty clip. The on-screen keyboard is no floater: it cuts the
   list's clip through `waveform_paint_area`, so its show and hide change the
   clip (one whole walk), and later walks with the same clip upload each rect
   minus the clip, the keyboard's own pixels outside the clip uploading
   normally. A pan step uploads
   the lanes above the canvas and the two frame columns (~1.4 MB) where
   today's present copies ~10 MB.
6. **The GL canvas is scissored to the canvas's paint area**
   (`onscreen_keyboard::waveform_paint_area`), the same rect the CPU passes
   clip to: the keyboard's slice and the folder overlay (zero height) are
   the CPU's, as today.
7. **The plate's producer is unchanged in substance.** `render_waveform`'s
   per-column bounds (`outer_rows`, `inner_rows`, the inner interior) are
   computed as today on the worker or synchronously; under the GL build they
   are kept as a column array instead of being written as rows, and uploaded
   as a small texture (~36 KB a step). The delta-rows write pass and the
   plate surface are the CPU build's alone.
8. **True Colors.** The EGL surface is created with
   `EGL_GL_COLORSPACE_DISPLAY_P3_PASSTHROUGH_EXT`, the EGL twin of today's
   `ANativeWindow_setBuffersDataSpace(DISPLAY_P3)`: the compositor takes the
   bytes as P3 coordinates without converting them, and
   `window_is_display_p3()` reads true iff that creation succeeded (a
   refusal is reported and not fatal, exactly as the data-space refusal is
   today; the painters then convert nothing). The dataspace is verified in
   `dumpsys SurfaceFlinger`'s layer list, because a screencap does not show
   it (the 2026-10-02 finding).
9. **The frame loop keeps its one wakeup.** `eglSwapBuffers` (swap interval
   1) blocks when the queue is full exactly where `ANativeWindow_lock`
   blocked; no Choreographer, no second cadence (architect 2026-08-27).
   *(REVERSED 2026-10-14 evening: the paint's gate is now AChoreographer's
   frame callback, one paint per vsync, and the loop has two wake sources,
   the tick and the callback — a clock-spaced coalescer had doubled every
   other display frame, measured by SurfaceFlinger's timestats;
   `gui_perf_campaign_2026_10.md` §7(c), `GuiPlatform::pump`'s tail.)* The
   GL frame is redrawn WHOLE every swap (a textured triangle and the canvas
   list are trivial for the GPU), so EGL buffer age and partial-update are
   NOT depended on; `eglSwapBuffersWithDamageKHR` may pass the damage union
   as a hint and nothing else.
10. **P4 — one rebuild per frame.** `Viewport::kick_waveform_sync` marks the
    rebuild OWED instead of running it; the pre-paint hook runs it once, at
    its tail after the scanner advance and Follow's page (which may kick)
    and before `settle_owed_lane_damage` (the rebuild declares damage, which
    the hook may). Every frame still takes a synchronous full rebuild from
    the latest state — the 2026-07-26 ruling's intent; the frames skipped
    are those no display ever showed. A caller that must read the rebuilt
    geometry inside its own event flushes the owed rebuild first (the
    inventory at the kick's declaration names them).
11. **The platform seam.** THE GL OWNER IS ONE FILE, `src/gui/gl_canvas.{h,cpp}`
    (`GuiGlCanvas`, 2026-10-14): everything GLES 3.0 and neither EGL nor a
    window — the two programs, the backbuffer's texture, the list's
    instance buffer and the plate's bounds texture, the upload walk less the
    canvas clip, the clear, the list's draw under the scissor and the
    texture's draw — driven by each platform's `present` with its context
    current; the platform keeps the display, the config, the context, the
    window's surface, the make current, the swap interval, the swap and its
    failure rule. Android: EGL on the ANativeWindow with the Display-P3
    passthrough and its tripwire; `libEGL.so` and `libGLESv3.so` on the
    APK's DT_NEEDED allowlist. THE LAPTOP (the Wayland backend, 2026-10-14,
    for parity — the two code bases sit as equals): EGL on the Wayland
    display (`eglGetPlatformDisplay`, a `wl_egl_window` over the one
    `wl_surface`, Mesa's own EGL and GLES, nothing third-party), ONE
    persistent cairo backbuffer of the surface's size in place of the two
    wl_shm buffers (`kPaintTargetCount` 1, as on Android), no colorspace
    attribute (the desktop is sRGB; the bytes go out as painted), SWAP
    INTERVAL 0 — Mesa's interval-1 swap waits inside `eglSwapBuffers` for
    the previous frame callback, which would block a loop that already
    paces itself by its own frame request and one timerfd wakeup — the
    frame request issued before the swap so it rides the swap's own commit,
    the frame redrawn whole at every swap (no buffer age, no partial
    update, no `wl_surface_damage_buffer`), the restored window's sizing
    frame painted into the backbuffer and its thickness handed to
    `present_frame` as the canvas clip's offset (§6's hazard, the laptop's
    one instance of it). The CLI paints nothing and is untouched.
12. **The CPU build stays buildable on Android** for the comparison: a CMake
    option, never a device key (`WARPTEMPO_CANVAS`, `gl` the
    product, `cpu` the reference), nothing the user picks.

13. **The page** (architect 2026-10-27, "1 a", the GPU page; his rule
    across the three surfaces: THE PICTURE FOLLOWS THE THUMB, THE SOUND
    WAITS FOR THE LIFT). MEASURED on the tablet 2026-10-27 over the PDF
    page scrub (4f953b52, tmp/scrub_rec/): frames that do not change the
    page came every 11 ms (90 Hz); every frame that changed it took
    25-30 ms and the thumb lurched 8-12 px. The pages were already in
    memory (an expansion is about 3 ms, off the main thread); the cost was
    THE CANVAS UPLOAD — the page blitted into the backbuffer and its ~9 MB
    box interior then uploaded by damage rect, §1's milestone-1 hazard for
    the waveform. THE PAGE LEAVES THE UPLOAD AS THE CANVAS DID: while the
    score viewer's page stands, the list's clip is THE MODAL BOX'S INTERIOR
    (the box empties the canvas paint area, so the hole has one tenant at a
    time and the scissor, the rect every walk leaves out and the floater
    bit's whole-walk rule are unchanged); `paint_modal_box` clears it to
    alpha 0 after the lanes that paint under the box; and the GPU draws the
    whole interior at every swap with one program — the field ground's word
    and, over it, the page from an R8 texture of the gray store's bytes,
    `texelFetch`ed at integer coordinates and written (g, g, g, 1) at the
    cpu painter's centered rect (`gui_page_place`). No color table: the cpu
    road's page is FF g g g under either conversion bit (`expand_gray`'s
    proof, re-checked over all 256 neutrals). THE TEXTURES MIRROR THE GRAY
    STORE (the GUI's request names its pages by a process-unique serial in
    the feed's order; `GuiGlCanvas` deletes what the request no longer
    names and, while the page stands, uploads at most one page a present,
    ~2.2 MB at the tablet's box), so the range scopes GPU memory as it
    scopes RAM, and once the range is resident a scrub uploads nothing. The
    GPU shows the shown page's texture where it is held, else the last
    painted page's, else the field ground — the cpu painter's rule — and
    reports the page it put on screen, which becomes `painted_page` at the
    next tick (the owed position write's gate). Under the GL canvas the
    ARGB cache is not kept: the feed asks the gray bytes alone, in the
    cache's order. The request and the report: `GuiCanvasPage`
    (`canvas_sink.h`); the lifecycle: `gl_canvas.h`; the GUI's half:
    `score_viewer.h`'s head. THE PROOF (2026-10-27, on the tablet, the 550 -
    2 PDF score, its 45-page range): §5's method, the cpu APK against the gl
    APK from the same viewer file, ten states each with True Colors on and
    off — the launch, the viewer's open, NEXT, NEXT, PREV, a scrub held at
    two columns (`input motionevent` DOWN / MOVE, captured before the UP),
    the lift, the close, the reopen — every pair byte-identical over the
    whole frame but the logo lane's clock digits when the minute turned
    between the two runs; the cpu APK of the tree before the change
    identical to the new cpu APK the same way. THE RECORDED SCRUB (adb
    screenrecord over two injected swipes along the scrub, ~35 pages each
    way, the frames' presentation times and a per-frame page-change score):
    the 82 frames that changed the page came 11.4 ms apart on mean (median
    10.8, 90th percentile 12.3), the 311 that did not 11.3 ms — the
    page-change frame no longer costs a frame.

    THE MEMORY RULE (architect 2026-10-27, the memory fix): a page is held
    ONCE, not twice. Under the GL canvas the gray store's RAM bytes live
    only until the page's texture is held: once the report lists a store
    entry's serial, the viewer's feed lets the bytes go — the store's copy
    and the request's, their last two owners, no job carrying them under
    this canvas — and the entry stands on, RELEASED (still the page had,
    so nothing is asked twice and the store's completeness, its scope and
    `painted_page` are unchanged), its request record carrying no bytes.
    The RAM price is the pages decoded and not yet uploaded, the GPU's the
    range's. An entry whose texture goes leaves the store with it — the
    roads are the interior leaving the store's box and coming back with no
    feed clearing the store in between: the viewer closed, or an interior
    too small to feed (the feed's empty-box return) — and the feed decodes
    that page again from the file. The cpu canvas keeps
    its bytes: the cache's expansions read them. Owners:
    `GuiScoreGrayPage` (`score_page_worker.h`),
    `GuiScoreViewer::settle_gray_on_gpu` (`score_viewer.cpp`).

## 4. The milestones

1. P4, then the EGL surface with the WHOLE backbuffer uploaded as one texture
   by damage rect — pixel-identical by construction, the EGL vendor,
   renderer, version and the P3 attribute's acceptance logged once at the
   window's adoption; measured by the frame counter; installed.
2. The canvas sink: the interior transparent in the backbuffer, the draw
   list, the plate as column bounds, the upload minus the canvas, the
   identity harness green; installed.
3. The campaign and design records (this file).
4. The laptop's twin, the following week, on the architect's word.

## 5. The proof

`adb exec-out screencap -p` reads the composited frame, so the CPU build and
the GL build on the same project state and view are compared BYTE FOR BYTE
over the whole frame (the method of the damage cut's screen test). The
harness installs each build in turn and drives the same deterministic acts
by injected taps on band and row-8 buttons (never a swipe: a pan's end
position depends on timing) — the launch view, Zoom to Fit, Full Zoom Out,
the magnification lamp, Next Marker, Toggle Follow, the `h` view, a card-free
sequence — capturing after each, then compares every pair. Any difference is
a defect until the architect rules otherwise. The P3 dataspace is checked in
`dumpsys SurfaceFlinger` separately.

THE PROOF RE-RUN AT b93f7178 (2026-10-14, the modal family's arc, on the
tablet): SEVENTEEN states — the original fifteen's states 0–10, then the
Settings menu, the Chrome row's dialog editor standing in the modal box with
the on-screen keyboard inside the frame, its Cancel, the render player in
the box, its Close, and the project picker in the box — ALL IDENTICAL, cpu
against gl, over the whole frame. The sixteenth state this section owed
from earlier that day (the Settings Chrome or Caption row's dropped list
over the canvas) is moot: the list was removed in e7a77ddb with the choice
drop-downs.

THE LAPTOP'S PROOF (2026-10-14, the twin's first day): the same method over
`grim` on labwc, the builds `build/` (gl) and `build-cpu/` (cpu) launched
in turn on the session's display, the acts injected as KEYS through
ydotool (its uinput device is relative-only, so the pointer is pinned off
the window by one large relative move and never clicked): the launch,
Full Zoom Out, two zoom steps, two walks right and one left, the tab
switch and back, the `h` view and back, the Settings pull-down over the
canvas and its Esc, bare `o` and its Esc (the read-only toggle's key, not
Open Project, which is Ctrl+O: those two states captured another view act,
so the project picker was first proved on the laptop at f538345e below) —
fifteen states on
the MAXIMIZED window (the app's own launch request) and eleven on the
RESTORED one (labwc's Ctrl+F10, ToggleMaximize), the sizing frame
standing. Every pair identical over the whole output above the desktop's
taskbar (its clock and a terminal title's spinner move; the window never
reaches it). The harness lives in tmp/ (`laptop_identity.sh`,
`laptop_identity_restored.sh`), a measurement and never a test suite.

THE LAPTOP'S PROOF RE-RUN AT f538345e (2026-10-14 evening, after the Fable
review's two fixes): the fifteen maximized and eleven restored states again
(the harness now pressing Ctrl+O for the project picker), plus a second
sequence (`laptop_identity2.sh`) of thirteen maximized and ten restored — the
Settings pull-down, the Chrome row's dialog editor in the modal box, two
typed letters, its Esc, the render player (bare `l`) and its Esc, the project
picker (Ctrl+O) and its Esc, then restore and maximize captured 0.2 s after
the grow and again settled (the backbuffer-size viewport of f538345e). All
identical, cpu against gl, above the taskbar; every consecutive pair of states
verified to differ where the act changes the screen.

THE TYPING AND CARET-BLINK PROOF ON THE TABLET AT f538345e: `identity3.sh` —
the Settings menu, the Chrome editor with the keyboard inside the frame, two
Backspaces and two letters on the glass keyboard, Cancel — seven states
identical, cpu against gl; and eight rapid captures of the standing editor on
each build caught both blink phases (caret shown and hidden), every gl frame
byte-identical to a cpu frame of the same phase. This measures the box-interior
damage of f538345e (`Viewport::invalidate_dialog_editor_box`) for typing and
the blink.

THE LIFECYCLE PASS (2026-10-14, the session's closing install, HEAD's
build): on the tablet, after the fifteen tap states came out identical to
the build before it, the app was sent home and brought back, put to sleep
(KEYCODE_SLEEP, the panel reporting Dozing) and woken, and asked to
rotate (the system's user_rotation; the activity is landscape-locked and
the system left it alone) — each a new surface adoption where the
system makes one, observed by screencap and logcat, no recovery code
anywhere. The same process stood through all of it, and the frame after
the return and the frame after the wake are byte-identical to the launch
frame (tmp/gpu_arc/lifecycle.sh, id_life_*.png).

## 6. Hazards named at the design

The EGL surface's colorspace attribute on this driver (a refusal makes the
tablet convert nothing — visible as the "neon" look, not as a byte change);
the upload cost of the texture road at milestone 1 before the canvas leaves
the upload at milestone 2; any future overlay that is translucent or not a
rectangle (it breaks §3.4's discard rule and must composite on the CPU); the
CTM under a non-zero frame (the Wayland twin's restored window) for the
three canvas painters that never set antialias — exact because every rect
and the translation are integral, and MEASURED SO on 2026-10-14: the
restored window's eleven states are identical between the two laptop
builds (§5).
