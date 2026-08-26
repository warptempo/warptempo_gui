#pragma once
#include "gui_input.h"
#include "input_core.h"
#include <cairo/cairo.h>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// GuiPlatform: the platform abstraction for the GUI's window, event loop and
// presentation — the ANDROID backend, the seam's second implementation. It
// runs inside a NativeActivity over the NDK's stock android_native_app_glue:
// the glue's thread owns an ALooper, this class adds its own periodic timerfd
// and the four worker eventfds to it, paints cairo into a persistent ARGB32
// backbuffer and blits the damaged rectangle into ANativeWindow_lock's buffer.
// No Android headers appear here on purpose; member pointer types are spelled
// `struct foo*` so the compiler treats them as forward declarations and the
// real interface headers stay private to platform_android.cpp — the same rule
// platform_wayland.h keeps for the same reason.
//
// THE PUBLIC API IS THE SEAM AND IS IDENTICAL TO platform_wayland.h's, member
// for member and signature for signature (plus exactly one addition,
// synthesize_key, which no consumer calls — see its declaration). The seven
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
    void invalidate_region(int x, int y, int w, int h);
    void drain_events();
    void paint_now();

    // THE CLIPBOARD IS THIS PROCESS'S OWN and goes no further: set stores the
    // one payload, get answers with it, and the empty string is the legal cold
    // answer every caller already handles ("nothing to paste"). Android's
    // system clipboard is a Java surface (ClipboardManager) reachable only
    // through JNI, which is a later nicety and not M3's; the Wayland twin's
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

    // -- THE ONE ADDITION TO THE SEAM'S PUBLIC SURFACE ---------------------
    // A KEY EVENT FROM SOMETHING THAT IS NOT A PHYSICAL KEYBOARD. Hardware
    // keyboards are out of scope on this platform (touch.md) and this backend
    // translates none: AInputEvent key events are handed back to the system so
    // BACK still leaves the app. What this exists for is M5's PAINTED
    // keyboard, which will emit keysyms directly — it is the road from that
    // surface into the core's key path, and it exists NOW so the path is built
    // and readable rather than discovered later.
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
    // NO CONSUMER CALLS IT TODAY and none may: main.cpp compiles against both
    // backends, so a call here would break the Wayland build. It is reached
    // from inside this backend alone until M5 gives it a caller.
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
    int  width_  = 0;
    int  height_ = 0;
    bool should_exit_ = false;
    bool has_initial_configure_ = false;
    // Latest focus reading; see window_activated().
    bool window_activated_ = false;

    // THE FIRST on_resize_ FIRE IS OWED RATHER THAN MADE. init() adopts a
    // window that already exists (android_main waits for it), which is BEFORE
    // main.cpp has wired a single hook — so the geometry is taken at once and
    // the CALLBACK is deferred to the head of the first loop pass, by which
    // time every hook is installed. Every later adoption (an APP_CMD_INIT_WINDOW
    // after a TERM) fires it on the spot and never sets this.
    bool initial_resize_owed_ = false;

    // THE TWO DEFERRABLE SOURCES, recorded by the drain and consumed by the
    // loop pass's tail. They are MEMBERS rather than locals because
    // drain_events() runs the drain WITHOUT the tail (see its definition): a
    // tick or a worker completion that lands inside a blocking load is
    // recorded here and dispatched at the first real loop pass, which is the
    // same deferral the Wayland backend gets for free from a timerfd nobody
    // reads while it is only dispatching the display.
    bool timer_fired_ = false;
    bool worker_fired_[4] = {false, false, false, false};

    // -- Idle-tick timing --
    // The ONE wakeup: a periodic timerfd (bionic has them) at half the
    // refresh period, exactly the Wayland model. The rate query and its
    // fallback are at detect_refresh_rate_ms().
    int  playback_tick_ms_ = 8;
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
    // Take the glue's current ANativeWindow as ours: the geometry, the
    // backbuffer, the core's surface width, has_initial_configure_, and full
    // damage. `fire_resize` is false for init()'s adoption alone (see
    // initial_resize_owed_) and true everywhere else.
    void adopt_window(bool fire_resize);
    void ensure_backbuffer(int w, int h);
    void destroy_backbuffer();
    void paint_one_frame();
    // Blit the damaged rectangle out of the backbuffer into the window,
    // honoring the stride and the (possibly widened) dirty rect lock hands
    // back. Silent no-op with no window or no backbuffer.
    void present(int x, int y, int w, int h);
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
