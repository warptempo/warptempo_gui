#pragma once
#include "device_config.h"
#include "gui_input.h"
#include "input_core.h"
#include <cairo/cairo.h>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// GuiPlatform: the platform abstraction for the GUI's window, event loop and
// presentation — THE ANDROID BACKEND, the glass half of the seam whose other
// implementation is platform_wayland.h's. It runs inside a NativeActivity over the NDK's stock android_native_app_glue:
// the glue's thread owns an ALooper, this class adds its own periodic timerfd
// and the five worker eventfds to it, paints cairo into a persistent ARGB32
// backbuffer and blits the damaged rectangle into ANativeWindow_lock's buffer
// AT THE CONTENT RECT'S ORIGIN (the window the GUI sees is the band inside the
// system bars; the rule and its two translation points are at origin_x_).
// No Android headers appear here on purpose; member pointer types are spelled
// `struct foo*` so the compiler treats them as forward declarations and the
// real interface headers stay private to platform_android.cpp — the same rule
// platform_wayland.h keeps for the same reason.
//
// THE PUBLIC API IS THE SEAM AND IS IDENTICAL TO platform_wayland.h's, member
// for member and signature for signature. It last grew on 2026-08-27, three
// times and each time on BOTH sides: the on-screen keyboard's two members
// (synthesize_key, which had stood here alone as the seam's one addition, and
// the new wants_onscreen_keyboard) grew Wayland twins, because the keyboard's
// press router is an ordinary consumer and both had to be callable against
// either backend; device_config_defaults() landed on both because the
// per-device preferences file (device_config.h) needs a first-run template only
// the platform can answer; and the reopen loop's three (request_run_stop,
// exit_requested, redeliver_geometry) landed on both because gui_main's loop
// is the one portable body driving either (the loop contract, platform.h). It
// grew twice more the same day, both on both sides: removable_volume(), the
// Synchronize to external storage act's destination — a platform fact like the
// config template, since where a machine mounts a stick is an answer only the
// backend has — and set_sync_worker_completion_fd, that act's worker taking
// the loop's fifth watched eventfd beside the other four. The
// seven
// consumers (main.cpp, viewport, paint_handler, prompt, file_loader, undo,
// input_handler) include platform.h and compile against either backend
// unchanged. WHERE A DOOR'S CONTRACT IS THE SEAM'S rather than this backend's,
// it is stated ONCE at its owner and pointed at from here: the input doors and
// the capture/cursor policy belong to GuiInputCore (input_core.h), and the
// window/paint/loop contracts the GUI depends on — the title's composition
// rule, the clipboard's payload rule, the loop-settled hook's whole rationale,
// the pointer capture's optimism ruling, set_cursor_kind's drop rule — are
// stated at platform_wayland.h's own declarations, which this file does not
// copy. What IS stated here is what Android ANSWERS differently, and every
// stub below says what its Wayland twin does and why this platform has no
// counterpart.
//
// THE INPUT POLICY IS NOT HERE, exactly as it is not in the Wayland backend.
// It lives in the ONE portable GuiInputCore this class holds (input_core.h);
// this class decodes AMotionEvent units and lifecycle commands on its own side
// and hands the core plain values.

class GuiPlatform {
public:
    using RedrawCallback       = std::function<void(cairo_t*, int x, int y, int w, int h)>;
    using ResizeCallback       = std::function<void(int w, int h)>;
    // The input callbacks and probes are the core's types, re-exported so a
    // consumer that holds this class needs no second include (contracts at
    // GuiInputCore, input_core.h).
    using KeyCallback          = GuiInputCore::KeyCallback;
    using KeyReleaseCallback   = GuiInputCore::KeyReleaseCallback;
    using ButtonCallback       = GuiInputCore::ButtonCallback;
    using WheelCallback        = GuiInputCore::WheelCallback;
    using MotionCallback       = GuiInputCore::MotionCallback;
    using CloseCallback        = std::function<void()>;
    using WheelContextProbe    = GuiInputCore::WheelContextProbe;
    using TextEditorProbe      = GuiInputCore::TextEditorProbe;
    using RepeatEligibleProbe  = GuiInputCore::RepeatEligibleProbe;
    using TickCallback         = std::function<void()>;
    using PrePaintCallback     = std::function<void()>;

    GuiPlatform();
    ~GuiPlatform();

    // THE WINDOW ALREADY EXISTS when this runs: android_main pumps the glue
    // until APP_CMD_INIT_WINDOW has landed and only then calls gui_main, so
    // init() adopts a live ANativeWindow rather than waiting for one. The
    // width/height arguments are therefore ADVISORY ONLY — the panel's own
    // size wins, since a NativeActivity has no say in its window size — and
    // the title argument is unused (no titlebar exists to carry it).
    bool init(int width, int height, const char* title);

    // THE DEVICE CONFIG'S FIRST-RUN TEMPLATE, the seam's own member (contract at
    // platform_wayland.h, which owns it): this backend answers 225 % and
    // `<externalDataPath>/projects` as the projects path. 225 is
    // the architect's own answer on the glass — the scale that reproduces the
    // retired rig's 1024 logical pixels on this panel, with the whole icon row
    // fitting (rationale at the definition, platform_android.cpp). (Until
    // 2026-08-28 the template also stamped a blank `audio_player`, nothing on
    // the tablet being spawnable; the key retired whole with the in-app
    // render player, which plays a render through the product's own engine
    // on both devices.) The projects path is the
    // activity's own absolute path stamped literally — exactly the folder the
    // sync convention pushes into — and it is read off the backend's one
    // file-scope pointer (g_android_app), which android_main parks before
    // gui_main asks; the choice is stated at the definition. The
    // XDG_CONFIG_HOME the config resolves under is pointed at the app's
    // private internal directory by android_main before gui_main runs, beside
    // the cache home it has always set.
    static DeviceConfig device_config_defaults();

    // THE ONE MOUNTED REMOVABLE VOLUME, the seam's own member (contract at
    // platform_wayland.h, which owns it): the Synchronize to external storage
    // act's destination, FOUND AND NEVER CONFIGURED.
    //
    // THE WHOLE VOLUME RULE ON THIS BACKEND: the `/storage/<name>` MOUNT
    // POINTS in the process's own mount table (`/proc/self/mounts`) whose
    // `<name>` is one path component and is not `emulated` (the app-visible
    // view of the device's own internal storage) or `self` (the per-process
    // mount namespace's own link). What is left is exactly the mounted
    // removable volumes — today `/storage/067C-8690`. THE MOUNT TABLE AND NOT
    // A DIRECTORY LISTING, because `/storage` is traversable but not listable
    // by this app's uid; the measurement and the `/mnt/media_rw` exclusion are
    // stated at the definition. An unreadable mount table refuses with the
    // system's own words, and zero and several take the shared half's own two
    // sentences (sole_removable_volume, external_sync.h).
    //
    // THE UUID IS NEVER CONSULTED, as the laptop's label is not: the same
    // physical stick is `067C-8690` here and `SANDISK` there, and "the one
    // removable volume" is the whole identity the product has of it. Reading
    // its contents needs the All-files permission (MANAGE_EXTERNAL_STORAGE);
    // this discovery does not, and a volume the app may see but not write
    // reports the refusal at the first copy, with the path and the system's
    // own words.
    static std::expected<std::filesystem::path, std::string> removable_volume();

    // THE WINDOW TITLE HAS NO SURFACE ON ANDROID: the activity is fullscreen
    // and landscape-locked with no titlebar, so both setters store nothing and
    // paint nothing. They stay on the API because the GUI calls them from its
    // load and dirty-state paths unconditionally (contract and composition
    // rule at platform_wayland.h, which owns them).
    void set_project_title(std::string project_name);
    void set_title_dirty(bool dirty);

    void shutdown();
    void run();
    void request_exit();
    // The reopen's pair and the geometry redelivery — the seam's own members
    // (contracts at platform_wayland.h and the loop contract at platform.h);
    // this backend answers them the same way, the stop bit ending pump()'s
    // loop with the window standing.
    void request_run_stop();
    bool exit_requested() const { return should_exit_; }
    void redeliver_geometry();
    void invalidate_region(int x, int y, int w, int h);
    void drain_events();
    void paint_now();

    // THE CLIPBOARD IS THIS PROCESS'S OWN and goes no further: set stores the
    // one payload, get answers with it, and the empty string is the legal cold
    // answer every caller already handles ("nothing to paste"). Android's
    // system clipboard is a Java surface (ClipboardManager) reachable only
    // through JNI and is not reached; the Wayland twin's
    // wl_data_device machinery — the selection claim, the offer bookkeeping,
    // the bounded pipe read — has no counterpart here because there is no
    // second client to hand bytes to. The bytes are not filtered here for the
    // Wayland twin's reason: text_editor::replace_selection is the boundary
    // that validates them.
    void        clipboard_set_text(const std::string& text);
    std::string clipboard_get_text();

    int width()  const;
    int height() const;
    bool has_initial_configure() const { return has_initial_configure_; }

    // WINDOW ACTIVATION (keyboard focus), driven by APP_CMD_GAINED_FOCUS /
    // APP_CMD_LOST_FOCUS. FALSE UNTIL THE FIRST GAINED_FOCUS, the same honest
    // cold answer the Wayland backend gives before its first configure; the
    // launched activity is focused, so the flag flips before any frame paints.
    bool window_activated() const { return window_activated_; }

    void set_on_redraw(RedrawCallback cb);
    void set_on_resize(ResizeCallback cb);
    void set_on_key(KeyCallback cb);
    // Contract at GuiInputCore::set_on_key_release, input_core.h.
    void set_on_key_release(KeyReleaseCallback cb);
    void set_on_button_press(ButtonCallback cb);
    void set_on_button_release(ButtonCallback cb);
    void set_on_wheel(WheelCallback cb);
    void set_on_motion(MotionCallback cb);
    // THE CLOSE HOOK HAS NO PRODUCER ON THIS PLATFORM and is deliberately
    // never fired. Its Wayland twin is xdg_toplevel.close — a REQUEST the
    // program may answer with the unsaved-work prompt — while Android's
    // counterpart (APP_CMD_DESTROY / destroyRequested) is the system stating
    // that the activity is already going and the loop must return at once.
    // Raising a prompt nobody could answer would be worse than silence, so the
    // teardown runs on the way out of gui_main instead. The setter stays
    // because the seam's promise is that a consumer compiles against either
    // backend unchanged.
    void set_on_close(CloseCallback cb);
    void set_wheel_context_probe(WheelContextProbe cb);
    void set_text_editor_active_probe(TextEditorProbe cb);
    void set_repeat_eligible_probe(RepeatEligibleProbe cb);

    // THE KEY-REPEAT INTERVAL in milliseconds. Contract at
    // GuiInputCore::key_repeat_period_ms, input_core.h. Android advertises no
    // repeat cadence of its own, so init() seeds the core with the hard-coded
    // pair (the ruling and the numbers are at that call, platform_android.cpp)
    // and this answers off it exactly as the Wayland backend answers off
    // wl_keyboard.repeat_info.
    int64_t key_repeat_period_ms() const;

    // Contract at GuiInputCore::set_pointer_left_hook, input_core.h.
    void set_pointer_left_hook(std::function<void(GuiPointerLeaveReason)> cb);

    // Fired ONLY on a CHANGE of window_activated(), from the focus commands.
    // Null-safe. Rationale at platform_wayland.h's own declaration.
    void set_activation_changed_hook(std::function<void()> cb);

    // Contract at GuiInputCore::set_keyboard_intent_cancel_hook, input_core.h.
    void set_keyboard_intent_cancel_hook(std::function<void()> cb);

    // THE TOUCH NAVIGATION HOOKS. Contract at
    // GuiInputCore::set_touch_nav_hooks, input_core.h.
    void set_touch_nav_hooks(
        std::function<void(const GuiTouchNavFrame&)> update,
        std::function<void()> end,
        std::function<bool(int x, int y)> pan_zone,
        std::function<bool(int x, int y)> thin_lane,
        std::function<void(int x, int y)> region_begin,
        std::function<void(int x, int y)> region_update,
        std::function<void()> region_end);

    // TRUE WHILE ANY FINGER IS ON THE GLASS. Contract at
    // GuiInputCore::touch_contact_active, input_core.h.
    bool touch_contact_active() const;

    // THE TOUCH SLOP, in device pixels — the GUI's scaled press-becomes-drag
    // gate pushed down. Contract, uses, twin-gate invariant and the two-call-site
    // inventory are all at GuiInputCore::set_touch_slop_px, input_core.h.
    void set_touch_slop_px(double px);

    // Fired ONCE PER ITERATION of run()'s loop, at the TAIL of the body, after
    // every source this pass dispatched. The hook's whole rationale — why a
    // loop boundary rather than the tick or the pre-paint, and what class of
    // consumer it exists for — is at platform_wayland.h's own declaration and
    // is not copied here; what this backend owes is the same placement, and
    // the exact skip conditions are stated at the fire site
    // (platform_android.cpp). Null-free (seeded with a no-op).
    void set_loop_settled_hook(std::function<void(GuiInputState)> cb);

    void set_on_tick(TickCallback cb);
    void set_on_pre_paint(PrePaintCallback cb);

    // GuiAsyncRenderer integration. The renderer creates its own eventfd (it
    // owns the lifetime) and registers it here; the run loop's ALooper watch
    // set grows a source for it. On readiness the loop reads the 8-byte
    // counter to clear the fd and then invokes the registered callback.
    void set_worker_completion_fd(int fd, std::function<void()> on_event);

    // POINTER CAPTURE HAS NO MEANING ON GLASS and both halves are no-ops: the
    // Wayland twin hides the cursor, locks the pointer through
    // zwp_pointer_constraints_v1 and feeds relative motion in as unbounded
    // virtual travel, and Android has no cursor to hide, no pointer to lock
    // and no relative stream — a finger is an absolute position or it is
    // nothing. The gestures that ask for a capture (the ctrl strip drag and
    // the grab-pan) then run on real coordinates, which is the SAME degraded
    // path the Wayland backend takes on a compositor advertising neither
    // optional protocol. The core's own capture state is deliberately NOT
    // seeded here — an uncaptured backend must leave pointer_captured() false
    // so nothing downstream believes travel is virtual.
    void begin_pointer_capture(GuiCursorKind restore_kind);
    void end_pointer_capture();

    // Contract at GuiInputCore::set_capture_restore_x, input_core.h.
    void set_capture_restore_x(double surface_x);

    // Contract at GuiInputCore::clear_capture_restore_x, input_core.h.
    void clear_capture_restore_x();

    // Contract at GuiInputCore::set_capture_restore_kind, input_core.h.
    void set_capture_restore_kind(GuiCursorKind kind);

    // Contract at GuiInputCore::set_notional_x_frozen, input_core.h.
    void set_notional_x_frozen(bool frozen);

    // Contract at GuiInputCore::set_notional_pointer_x, input_core.h.
    void set_notional_pointer_x(double surface_x);

    // Contract at GuiInputCore::set_capture_wrap_span, input_core.h.
    void set_capture_wrap_span(double lo, double hi);

    // THE POINTER'S NOTIONAL POSITION (surface x, px). Contract at
    // GuiInputCore::notional_pointer_x, input_core.h. The field is live here
    // too — the finger's translation writes it through the core — so the GUI's
    // one answer to "where is the pointer?" stays answerable on glass.
    double notional_pointer_x() const;

    // THE ONE DOOR TO THE CURSOR IMAGE, and on Android it has no image behind
    // it: there is no pointer, so the kind is REMEMBERED (the core's policy,
    // including the drop of a kind named for a position the pointer does not
    // occupy — stated at GuiInputCore::pointer_position_unknown) and nothing
    // is applied. Remembering rather than discarding keeps the accessor
    // honest for a future backend that grows a cursor, and costs one store.
    void set_cursor_kind(GuiCursorKind kind);

    // The GuiWaveformWorker's completion eventfd; same shape as the async
    // renderer's above.
    void set_waveform_worker_completion_fd(int fd, std::function<void()> on_event);

    // And the GuiHistoryCommitWorker's completion eventfd.
    void set_history_worker_completion_fd(int fd, std::function<void()> on_event);

    // And the SIXTH, the history walk's prefetch worker — the one whose eventfd
    // is a READY signal rather than a completion (the callback DRAINS whatever
    // has queued; the loop's read of the counter is unchanged, the value being
    // the count of nothing the callback needs).
    void set_history_prefetch_completion_fd(int fd,
                                            std::function<void()> on_event);

    // And the SEVENTH, the Synchronize to external storage act's worker.
    void set_sync_worker_completion_fd(int fd, std::function<void()> on_event);

    // -- THE ON-SCREEN KEYBOARD'S TWO SEAM MEMBERS -------------------------
    //
    // DOES THIS PLATFORM WANT THE GUI TO PAINT A KEYBOARD? Android answers YES,
    // unconditionally: the glass has no keys and this backend translates no
    // hardware ones, so the painted surface is the ONLY key producer the
    // platform can have. The declaration's contract — why it is a platform
    // question rather than a setting, and what the false answer buys the
    // laptop build — is at platform_wayland.h's own, which this file does not
    // copy.
    bool wants_onscreen_keyboard() const;

    // A KEY EVENT FROM SOMETHING THAT IS NOT A PHYSICAL KEYBOARD. Hardware
    // keyboards are out of scope on this platform (touch.md) and this backend
    // translates none: AInputEvent key events are handed back to the system so
    // BACK still leaves the app. THIS IS THE ROAD THE OWNED ON-SCREEN KEYBOARD
    // TAKES INTO THE CORE'S KEY PATH — a painted surface emitting keysyms
    // directly (onscreen_keyboard.h; its press router is the one caller).
    //
    // `key` is a GuiKey (an X11 keysym, ASCII case-folded, standalone
    // modifiers and F1..F35 already dropped — the backend's contract, at
    // GuiInputCore::key_event). `stable_code` is the caller's own per-key
    // identity, which the repeat cancel and the synthesized-left hold end
    // compare against; it must be stable for a given key and must not be the
    // keysym. `codepoint` is the UTF-32 value the key produces, or 0 for a key
    // that produces no character — it is REMEMBERED against stable_code so the
    // core's codepoint probe can re-answer for a synthesized repeat, which is
    // the whole reason this is a method rather than a bare forward.
    //
    // IT HAS A CONSUMER SINCE 2026-08-27 and that is why its Wayland twin
    // exists: the keyboard's press router calls it through the seam like any
    // other member, having asked wants_onscreen_keyboard() first. (Until then
    // no consumer could call it — main.cpp compiles against both backends —
    // and it was reachable from inside this backend alone.)
    void synthesize_key(GuiKey key, uint32_t stable_code, bool pressed,
                        uint32_t codepoint);

private:
    // The glue's callback tables are C function pointers taking `android_app*`,
    // so dispatch lives in file-static functions that cast `app->userData` to
    // `GuiPlatform*` and call into private member functions. This struct is the
    // friend through which those statics reach the privates.
    friend struct AndroidCallbacks;

    // -- The glue and the window --
    // app_ is the glue's state, adopted at init() from the file-scope pointer
    // android_main parks there (the reason is at that pointer's definition).
    // window_ is BORROWED and never ANativeWindow_acquire()d — the glue owns
    // it between APP_CMD_INIT_WINDOW and APP_CMD_TERM_WINDOW, and a null here
    // means painting is suspended.
    struct android_app*   app_    = nullptr;
    struct ANativeWindow* window_ = nullptr;

    // -- Damage accumulator --
    // Set by invalidate_region() at any time; consumed by the next paint pass
    // and cleared once the damaged pixels have been posted.
    struct DamageRect { int x, y, w, h; };
    std::vector<DamageRect> damage_;

    // -- The backbuffer (pattern: ONE persistent surface, not a pool) --
    // Cairo paints into this ARGB32 image surface and the damaged rectangle is
    // swizzled out of it into whatever buffer ANativeWindow_lock hands back.
    // THE PERSISTENCE IS WHAT MAKES PARTIAL DAMAGE CORRECT: the window rotates
    // through several buffers, and lock() may widen the dirty rect it was
    // asked for, so the copy answers from a surface that holds the WHOLE last
    // frame rather than from a pool entry that holds part of an older one.
    // (The Wayland backend's double-buffered wl_shm pool exists because the
    // compositor reads OUR memory; here the copy is explicit, so one buffer
    // does.)
    cairo_surface_t* back_   = nullptr;
    int              back_w_ = 0;
    int              back_h_ = 0;

    // -- Window state --
    // THE CONTENT RECT IS THE WINDOW, and width_/height_ are ITS size — what
    // the GUI is told the window is, and what every rect that crosses the seam
    // is measured in. The SURFACE ANativeWindow hands back is bigger: an app
    // window's frame is the whole display by construction on modern Android
    // (its own layout params carry FLAG_LAYOUT_IN_SCREEN | FLAG_LAYOUT_INSET_-
    // DECOR, and "fitting the system windows" is DecorView PADDING, which a
    // NativeActivity never sees because it takes the WINDOW's surface), so the
    // band inside the system bars arrives as the CONTENT RECT instead.
    //
    // THE RECT IS THE FRAMEWORK'S, MINUS THE AIR — this backend measures no
    // inset of its own; the one thing it subtracts is kStatusBarAirPx, the air
    // it leaves between the status bar and the first row, and that subtraction
    // lives in resolve_content_rect with its reasoning at the constant
    // (platform_android.cpp). Measured on the Tab S10 FE
    // 2026-08-27: surface frame [0,0][2304,1440], content 2304x1387 at (0,53),
    // i.e. the STATUS BAR ALONE. One UI's 84 px taskbar was NOT part of the
    // rect: dumpsys reported `InsetsSource type=navigationBars
    // frame=[0,1356][2304,1440] visible=false` and a `tappableElement` source
    // on that same frame with visible=true, alongside
    // `mAppBounds=(0,0-2304,1356)` — and the reading was taken with the panel
    // DOZING (the cover shut), so the taskbar was not a visible navigation
    // inset at that moment. WHETHER IT OVERLAYS THE BOTTOM ROW ON AN AWAKE
    // PANEL IS OPEN (the architect's next look). If it does, the recorded next
    // step is to read WindowInsets.Type.tappableElement()'s bottom through the
    // Java sliver — a JNI call this side makes at each content-rect / config
    // change — and subtract it from the rect here. Not done.
    //
    // ORIGIN IS THIS BACKEND'S ALONE. Nothing above the seam knows it exists:
    // it is ADDED on the way out (present, the one blit) and SUBTRACTED on the
    // way in (the AMotionEvent decode's two coordinate lambdas), which is the
    // whole of the translation and the reason GuiInputCore, main.cpp and every
    // painter stay identical to the Wayland build's. Before the first
    // APP_CMD_CONTENT_RECT_CHANGED the rect is the whole surface and every
    // offset below is 0, which is exactly the pre-2026-08-27 behaviour.
    int  origin_x_  = 0;
    int  origin_y_  = 0;
    int  surface_w_ = 0;
    int  surface_h_ = 0;
    // ONE FULL-SURFACE POST IS OWED AFTER EVERY ADOPTION, so the two bands
    // outside the content rect get their ground (present picks it per row —
    // the top band's is the title strip's) at least once per window rather
    // than showing whatever the buffer held. Later frames
    // keep them right through lock()'s own widening, which is the same
    // mechanism partial damage already relies on (present).
    bool surface_bands_owed_ = false;
    int  width_  = 0;
    int  height_ = 0;
    bool should_exit_ = false;
    // The run-stop request, cleared at the head of every run() (platform.h).
    bool run_stop_requested_ = false;
    bool has_initial_configure_ = false;
    // Latest focus reading; see window_activated().
    bool window_activated_ = false;

    // THE FIRST on_resize_ FIRE IS OWED RATHER THAN MADE. init() adopts a
    // window that already exists (android_main waits for it), which is BEFORE
    // main.cpp has wired a single hook — so the geometry is taken at once and
    // the CALLBACK is deferred to whichever comes first of the first loop pass
    // and the first paint, by which time every hook is installed
    // (deliver_owed_resize is the one owner). Every later adoption (an
    // APP_CMD_INIT_WINDOW after a TERM) fires it on the spot and never sets
    // this.
    bool initial_resize_owed_ = false;

    // THE TWO DEFERRABLE SOURCES, recorded by the drain and consumed by the
    // loop pass's tail. They are MEMBERS rather than locals because the drain
    // hands the looper's events back in readiness order while the ORDER THEY
    // ARE ACTED ON IN IS POLICY (stated at pump): a tick and a worker
    // completion that arrive mid-drain are recorded here and dispatched at the
    // pass's tail, after the window-system sources are empty.
    bool timer_fired_ = false;
    bool worker_fired_[5] = {false, false, false, false, false};

    // -- Idle-tick timing --
    // The ONE wakeup: a periodic timerfd (bionic has them), exactly the
    // Wayland model. The number and its whole rationale are at
    // detect_refresh_rate_ms().
    int  playback_tick_ms_ = 5;
    int  timerfd_ = -1;

    // -- Async renderer completion fd (owned by GuiAsyncRenderer; this is just
    // a watch handle, not a lifetime claim). -1 when none is registered.
    int  worker_completion_fd_ = -1;
    std::function<void()> on_worker_completion_;

    // Waveform-worker completion fd. Same lifetime story as above.
    int  waveform_worker_completion_fd_ = -1;
    std::function<void()> on_waveform_worker_completion_;

    // Checkpoint-worker completion fd. Same lifetime story again.
    int  history_worker_completion_fd_ = -1;
    std::function<void()> on_history_worker_completion_;

    // History-prefetch ready fd. Same lifetime story again.
    int  history_prefetch_completion_fd_ = -1;
    std::function<void()> on_history_prefetch_ready_;

    // Synchronization-worker completion fd. Same lifetime story again.
    int  sync_worker_completion_fd_ = -1;
    std::function<void()> on_sync_worker_completion_;

    // -- The clipboard's one payload (see clipboard_set_text) --
    std::string clipboard_text_;

    // -- The synthesized keyboard's codepoint table --
    // stable_code -> codepoint, written by synthesize_key and read by the
    // core's codepoint probe when it re-fills a synthesized repeat. It is a
    // MAP rather than a single slot because the probe may be asked about a key
    // whose press is no longer the newest one; it never shrinks, and its size
    // is bounded by the painted keyboard's own key count.
    std::unordered_map<uint32_t, uint32_t> key_codepoints_;

    // -- THE PORTABLE INPUT POLICY (input_core.h) --
    // Every input event this class decodes is handed to this object in plain
    // units, and every input door on the public surface above forwards to it.
    GuiInputCore input_;

    // -- Callbacks --
    RedrawCallback       on_redraw_;
    ResizeCallback       on_resize_;
    CloseCallback        on_close_;
    // Fired at each window_activated_ EDGE (see set_activation_changed_hook).
    std::function<void()> activation_changed_hook_;
    // Fired at the TAIL of every run() iteration that is not leaving the loop
    // (see set_loop_settled_hook). SEEDED with a no-op so the fire site needs
    // no null test.
    std::function<void(GuiInputState)> loop_settled_hook_ =
        [](GuiInputState) {};
    TickCallback         on_tick_;
    PrePaintCallback     on_pre_paint_;

    // -- Internal helpers --
    // Take the glue's current ANativeWindow as ours: the surface geometry AND
    // the content rect (origin + size, re-read every time), the backbuffer,
    // the core's surface width, has_initial_configure_, and full damage. `fire_resize` is false for init()'s adoption alone (see
    // initial_resize_owed_) and true everywhere else.
    void adopt_window(bool fire_resize);
    // Resolve the glue's current content rect against the surface size: the
    // origin and the size the GUI will be told, the status-bar air already
    // taken off the top (kStatusBarAirPx, platform_android.cpp). Falls back to
    // the whole surface for the zero rect the glue starts with and for anything
    // empty, inverted or out of bounds — the window is never nothing.
    void resolve_content_rect(int surf_w, int surf_h,
                              int& ox, int& oy, int& cw, int& ch) const;
    void ensure_backbuffer(int w, int h);
    void destroy_backbuffer();
    void paint_one_frame();
    // Blit the damaged rectangle out of the backbuffer into the window,
    // honoring the stride and the (possibly widened) dirty rect lock hands
    // back. Silent false with no window or no backbuffer. TRUE MEANS THE
    // PIXELS REACHED THE WINDOW — the caller clears its damage on that answer
    // and on no other (the rule is at the call site).
    bool present(int x, int y, int w, int h);
    // Deliver the owed first on_resize_ if one is owed. THE ONE OWNER, called
    // from pump()'s head and from paint_one_frame's (contract at its
    // definition).
    void deliver_owed_resize();
    int  detect_refresh_rate_ms();
    bool arm_playback_timer();
    // Add one fd to the glue's looper under `ident`; a negative fd is the
    // "nothing registered" answer and is a silent no-op. The four worker
    // registrations and the timerfd all go through it, and unwatch_fd is its
    // counterpart at shutdown (a looper holding a closed fd is the one thing
    // teardown must not leave behind).
    void watch_fd(int fd, int ident);
    void unwatch_fd(int fd);
    // Drain the looper into the two flag members above. Window-system sources
    // (the glue's cmd pipe and input queue) are processed ON THE SPOT — their
    // process() bodies are the glue's own and deferring one would mean holding
    // an unfinished AInputEvent — while the timer and the worker fds are only
    // recorded. `timeout_ms` is -1 to block for the first event and 0 to take
    // whatever is already there.
    void drain_looper(int timeout_ms);
    // One pass of the loop body: the blocking drain, then the fixed dispatch
    // order, the settled hook and the paint.
    void pump();

    // -- Lifecycle and input handlers (called from the file-static glue hooks) --
    void on_app_cmd(int32_t cmd);
    // Returns 1 when the event was consumed, 0 to let the system have it.
    int32_t on_input_event(struct AInputEvent* event);
    void on_motion_event(struct AInputEvent* event);
};
