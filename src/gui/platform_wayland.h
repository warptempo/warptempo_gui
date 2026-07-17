#pragma once
#include "gui_input.h"
#include <cairo/cairo.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// GuiPlatform: the platform abstraction for the GUI's window, event loop,
// and presentation. The Wayland backend opens a window via libwayland-client,
// paints cairo surfaces directly into wl_shm buffers in response to compositor
// frame callbacks, drives a separate playback tick on a timerfd, and requests
// server-side decorations via the xdg-decoration unstable protocol. No
// Wayland headers appear here on purpose; member pointer types are spelled
// `struct foo*` so the compiler treats them as forward declarations and the
// real interface headers stay private to platform_wayland.cpp.
class GuiPlatform {
public:
    using RedrawCallback       = std::function<void(cairo_t*, int x, int y, int w, int h)>;
    using ResizeCallback       = std::function<void(int w, int h)>;
    using KeyCallback          = std::function<void(GuiKey key, GuiInputState mods)>;
    using ButtonCallback       = std::function<void(GuiMouseButton button, int x, int y, GuiInputState mods)>;
    // A scroll wheel notification carrying the NET number of detents crossed
    // in one pointer frame (always >= 1). on_pointer_frame() coalesces a
    // frame's worth of value120 / legacy-axis deltas into a single emission
    // of this callback, so the per-step wheel machinery (viewport move,
    // damage, hover, worker kick) runs once per frame regardless of burst
    // size. `dir` is WheelUp or WheelDown; `steps` is the magnitude.
    using WheelCallback        = std::function<void(GuiMouseButton dir, int steps, int x, int y, GuiInputState mods)>;
    using MotionCallback       = std::function<void(int x, int y, GuiInputState mods)>;
    using CloseCallback        = std::function<void()>;
    // Wheel routing predicate installed by main.cpp: given pointer
    // coordinates, returns -1 (blocked) or a region code. on_pointer_frame()
    // consults it per raw pointer frame before accumulating sub-detent
    // scroll so remainder is bound to the routing context it will emit in.
    using WheelContextProbe    = std::function<int(int x, int y)>;
    // Predicate installed by main.cpp: true when a text editor is consuming
    // printable keys. Consulted at PRESS time for both synthesized keys —
    // kLeftClickKey and kCtrlModKey — to decide whether each is its synthesized
    // form (false) or a normal character (true): while a text editor is open
    // kLeftClickKey stays a normal letter and kCtrlModKey a normal digit.
    using TextEditorProbe      = std::function<bool()>;
    using TickCallback         = std::function<void()>;
    using PrePaintCallback     = std::function<void()>;

    GuiPlatform();
    ~GuiPlatform();

    bool init(int width, int height, const char* title);
    void set_title(const std::string& title);
    void shutdown();
    void run();
    void request_exit();
    void invalidate_region(int x, int y, int w, int h);
    void drain_events();
    void paint_now();

    int width()  const;
    int height() const;
    bool has_initial_configure() const { return has_initial_configure_; }

    void set_on_redraw(RedrawCallback cb);
    void set_on_resize(ResizeCallback cb);
    void set_on_key(KeyCallback cb);
    void set_on_button_press(ButtonCallback cb);
    void set_on_button_release(ButtonCallback cb);
    void set_on_wheel(WheelCallback cb);
    void set_on_motion(MotionCallback cb);
    void set_on_close(CloseCallback cb);
    void set_wheel_context_probe(WheelContextProbe cb);
    void set_text_editor_active_probe(TextEditorProbe cb);
    void set_on_tick(TickCallback cb);
    void set_on_pre_paint(PrePaintCallback cb);

    // GuiAsyncRenderer integration. The renderer creates its own eventfd
    // (it owns the lifetime) and registers it here; the run loop's poll set
    // grows a third pollfd watching for completion writes from the worker
    // thread. On POLLIN the loop reads the 8-byte counter to clear the fd
    // and then invokes the registered callback (which routes to
    // GuiAsyncRenderer::on_completion_event).
    void set_worker_completion_fd(int fd, std::function<void()> on_event);

    // Parallel hookup for the GuiWaveformWorker's completion
    // eventfd. The poll set grows a fourth pollfd; on POLLIN the loop
    // reads the counter and invokes this callback (routes to
    // GuiWaveformWorker::on_completion_event).
    void set_waveform_worker_completion_fd(int fd, std::function<void()> on_event);

private:
    // libwayland's listener tables are C structs of function pointers, so
    // dispatch lives in static functions that cast `data` to `GuiPlatform*`
    // and call into private member functions. This struct is the friend
    // through which those statics reach the privates.
    friend struct WaylandListeners;

    // -- Wayland globals (bound during init()) --
    struct wl_display*    wl_display_     = nullptr;
    struct wl_registry*   wl_registry_    = nullptr;
    struct wl_compositor* wl_compositor_  = nullptr;
    struct wl_shm*        wl_shm_         = nullptr;
    struct xdg_wm_base*   xdg_wm_base_    = nullptr;
    struct zxdg_decoration_manager_v1* xdg_decoration_manager_ = nullptr;
    struct wl_output*     wl_output_      = nullptr;
    uint32_t              output_global_name_ = 0;

    // -- Surface objects --
    struct wl_surface*       wl_surface_       = nullptr;
    struct xdg_surface*      xdg_surface_      = nullptr;
    struct xdg_toplevel*     xdg_toplevel_     = nullptr;
    struct zxdg_toplevel_decoration_v1* xdg_toplevel_decoration_ = nullptr;

    // -- Frame callback (one in flight at a time, or none) --
    struct wl_callback*      frame_callback_   = nullptr;

    // -- Damage accumulator --
    // Set by invalidate_region() at any time; consumed at the next
    // frame-callback paint and cleared after attach + commit.
    struct DamageRect { int x, y, w, h; };
    std::vector<DamageRect> damage_;

    // -- Buffer pool (pattern B: double-buffered wl_shm) --
    // Pattern X: cairo surfaces are created directly on the wl_shm buffer's
    // mmap'd memory. The cairo_t* the on_redraw callback receives is a
    // context on whichever buffer paint_one_frame just acquired. No
    // off-screen canonical surface — every paint goes straight into a
    // presentation buffer. The double-buffered pool guarantees the
    // compositor never reads a buffer we're writing to.
    struct ShmBuffer {
        struct wl_buffer* buffer       = nullptr;
        cairo_surface_t*  surface      = nullptr;  // image-surface on `pixels`
        void*             pixels       = nullptr;  // points into mmap region
        size_t            size_bytes   = 0;
        bool              busy         = false;    // true between attach and release
        // Damage accumulated since this buffer was last attached. The list is
        // bounded by the same containment coalescing as damage_; in practice
        // it stays to a few rects, and the occasional larger clip set on
        // buffer alternation is the cost of correctness.
        std::vector<DamageRect> pending;
    };
    // Buffer count is the single source of truth for the array size and
    // every loop that walks the buffers (pool sizing, per-buffer layout,
    // acquire, destroy). Change it here only.
    static constexpr int kShmBufferCount = 2;
    ShmBuffer shm_buffers_[kShmBufferCount];
    int       shm_pool_fd_   = -1;
    void*     shm_pool_map_  = nullptr;
    size_t    shm_pool_size_ = 0;
    struct wl_shm_pool* shm_pool_ = nullptr;

    // -- Window state --
    int  width_  = 0;
    int  height_ = 0;
    int  pending_w_ = 0;   // populated by xdg_toplevel.configure between
    int  pending_h_ = 0;   // configure events; consumed by xdg_surface.configure
    bool should_exit_ = false;
    bool has_initial_configure_ = false;

    // True only while paint_one_frame is executing the pre-paint hook.
    // invalidate_region() consults this flag and skips its trailing
    // schedule_frame_callback() call when set, so the hook can declare
    // additional damage without producing a spurious extra commit.
    bool in_pre_paint_ = false;

    // The bound single wl_output's latest CURRENT mode, in millihertz.
    // Replaced on mode changes and cleared on output removal. Zero means no
    // output advertised a usable mode; detect_refresh_rate_ms() then falls
    // back to 60 Hz.
    int  output_refresh_mhz_ = 0;

    // -- Idle-tick timing --
    int  playback_tick_ms_ = 8;
    int  timerfd_ = -1;

    // -- Async renderer completion fd (owned by GuiAsyncRenderer; this is
    // just a watch handle, not a lifetime claim). -1 when no renderer is
    // registered.
    int  worker_completion_fd_ = -1;
    std::function<void()> on_worker_completion_;

    // Waveform-worker completion fd. Same lifetime story as the
    // async-renderer fd above. -1 when no waveform worker is registered.
    int  waveform_worker_completion_fd_ = -1;
    std::function<void()> on_waveform_worker_completion_;

    // -- Keyboard --
    struct wl_seat*     wl_seat_     = nullptr;
    uint32_t            seat_global_name_ = 0;
    struct wl_keyboard* wl_keyboard_ = nullptr;

    struct xkb_context* xkb_context_ = nullptr;
    struct xkb_keymap*  xkb_keymap_  = nullptr;
    struct xkb_state*   xkb_state_   = nullptr;

    // Tracked modifier state, refreshed by the wl_keyboard.modifiers event
    // and consumed by GuiInputState construction on every key delivery.
    bool mod_ctrl_  = false;
    bool mod_shift_ = false;
    bool mod_alt_   = false;

    // -- Pointer --
    struct wl_pointer* wl_pointer_ = nullptr;

    // Pointer focus and last-known position. surface_x/y are wl_fixed_t
    // values delivered with enter and motion; we convert to int with
    // wl_fixed_to_int at delivery time. pointer_x_/pointer_y_ hold the
    // most recent int coordinates so we have a position to report with
    // button events (wl_pointer.button does not carry coordinates).
    bool pointer_focused_ = false;
    int  pointer_x_ = 0;
    int  pointer_y_ = 0;

    // Live truth for left-button held state. Updated on every press and
    // release for BTN_LEFT. Read by current_mods() into the
    // primary_button_held field of GuiInputState.
    bool pointer_left_held_ = false;

    // kLeftClickKey emulation state. synth_left_held_ is true while that key
    // is held as a synthesized left button; synth_left_keycode_ is the xkb
    // keycode that owns the hold, so the release is matched by keycode
    // exactly like repeat cancellation is. The logical left button is
    // (pointer_left_held_ || synth_left_held_): a press is delivered only on
    // the 0->1 edge of that OR and a release only on the 1->0 edge (the evdev
    // model for two devices sharing BTN_LEFT), so physical and synthesized
    // sources never double-deliver.
    bool     synth_left_held_    = false;
    uint32_t synth_left_keycode_ = 0;

    // kCtrlModKey emulation state, the modifier sibling of the pair above.
    // synth_ctrl_held_ is true while that key is held as a synthesized Ctrl
    // modifier; synth_ctrl_keycode_ is the xkb keycode that owns the hold, so
    // the release is matched by keycode exactly like the synth-left release.
    // Logical Ctrl is (mod_ctrl_ || synth_ctrl_held_): current_mods() ORs the
    // physical modifier bit with this synthesized hold, so both spellings work
    // everywhere a chord reads Ctrl. No button and no logical-OR edge model —
    // a modifier only projects state; the next key/pointer/wheel event carries
    // it, the same convention on_keyboard_modifiers uses for real modifiers.
    bool     synth_ctrl_held_    = false;
    uint32_t synth_ctrl_keycode_ = 0;

    // Accumulated vertical scroll carry, in value120 units (120 = one
    // detent). on_pointer_frame() folds the per-frame delta (arbitrated
    // from the two staged sources below) into this and drains it into
    // discrete WheelUp/WheelDown steps once per logical pointer frame,
    // carrying the sub-detent remainder forward. This is what collapses a
    // touchpad's stream of small axis events into the same handful of
    // steps a mouse wheel produces.
    double scroll_accum_ = 0.0;

    // The routing context every unit currently in scroll_accum_ was
    // contributed under: (probe region << 3) | modifier chord bits. When a
    // frame carrying axis input re-probes to a different key, the stale
    // remainder is cleared before the new delta is added, so a completed
    // detent is never assembled across a modifier or region change.
    int    scroll_context_key_ = 0;

    // Per-frame scroll staging. A wl_pointer.frame may carry a value120
    // event (wheel, high-resolution) and/or a legacy wl_pointer.axis event
    // (touchpad and other continuous sources). Both handlers stage into
    // these scratch fields; on_pointer_frame() arbitrates — value120 wins
    // when present, legacy axis is used otherwise — folds the result into
    // scroll_accum_, then resets all four unconditionally at the frame
    // boundary so no partial delta leaks into a later frame.
    double frame_v120_accum_ = 0.0;   // value120 units seen this frame (wheel)
    double frame_axis_accum_ = 0.0;   // legacy axis units seen this frame
    bool   frame_have_v120_  = false; // a value120 event arrived this frame
    bool   frame_have_axis_  = false; // a legacy axis event arrived this frame

    // Cursor (system theme, loaded once at init, kept for the process
    // lifetime). cursor_surface_ is a dedicated wl_surface that holds
    // the cursor image buffer; it is distinct from the main window
    // wl_surface_ and is passed to wl_pointer.set_cursor on every
    // pointer enter.
    struct wl_cursor_theme* wl_cursor_theme_   = nullptr;
    struct wl_cursor*       wl_cursor_arrow_   = nullptr;
    struct wl_surface*      cursor_surface_    = nullptr;
    int32_t                 cursor_hotspot_x_  = 0;
    int32_t                 cursor_hotspot_y_  = 0;

    // Key repeat (last-key-wins, timerfd-tick-piggyback).
    // repeat_key_ is the GuiKey currently repeating (0 = none).
    // repeat_keycode_ is the raw xkb keycode of that key, used so the
    // wl_keyboard.key release event can match-and-cancel.
    // repeat_mods_ captures the modifier state at the moment the key was
    // pressed; the same modifiers are delivered with each synthesized
    // repeat.
    // repeat_due_us_ is the next-fire monotonic time in microseconds; when
    // the playback tick fires and current_monotonic_us >= repeat_due_us_,
    // the on_key_ callback fires and repeat_due_us_ is advanced by the
    // repeat interval.
    // repeat_delay_us_ is the compositor-advertised initial delay before
    // the first repeat (from wl_keyboard.repeat_info), in microseconds.
    // repeat_period_us_ is the compositor-advertised inter-repeat interval
    // (1_000_000 / rate), in microseconds.
    // Zero rate means "no repeat" and is honored — held keys do not repeat.
    GuiKey        repeat_key_       = 0;
    uint32_t      repeat_keycode_   = 0;
    GuiInputState repeat_mods_      = {};
    uint64_t      repeat_due_us_    = 0;
    uint64_t      repeat_delay_us_  = 600'000;   // sensible default if compositor
    uint64_t      repeat_period_us_ = 33'000;    // doesn't advertise (600ms/30Hz)

    // -- Callbacks --
    RedrawCallback       on_redraw_;
    ResizeCallback       on_resize_;
    KeyCallback          on_key_;
    ButtonCallback       on_button_press_;
    ButtonCallback       on_button_release_;
    WheelCallback        on_wheel_;
    MotionCallback       on_motion_;
    CloseCallback        on_close_;
    WheelContextProbe    wheel_context_probe_;
    TextEditorProbe      text_editor_active_probe_;
    TickCallback         on_tick_;
    PrePaintCallback     on_pre_paint_;

    // -- Internal helpers --
    void recreate_shm_pool(int w, int h);
    void destroy_shm_pool();
    bool load_cursor_theme();
    ShmBuffer* acquire_free_buffer();
    void schedule_frame_callback();
    void paint_one_frame();
    void destroy_wayland_state();
    int  detect_refresh_rate_ms();
    bool arm_playback_timer();

    // -- Event handlers (called from file-static dispatchers) --
    void on_registry_global(struct wl_registry* r, uint32_t name,
                            const char* interface, uint32_t version);
    void on_registry_global_remove(uint32_t name);
    void on_output_mode(uint32_t flags, int32_t width, int32_t height,
                        int32_t refresh_mhz);
    void on_xdg_surface_configure(struct xdg_surface* xs, uint32_t serial);
    void on_toplevel_configure(int32_t width, int32_t height);
    void on_toplevel_close();
    void on_frame_done(struct wl_callback* cb);

    // -- Keyboard handlers --
    void on_seat_capabilities(uint32_t caps);
    void on_keyboard_keymap(uint32_t format, int fd, uint32_t size);
    void on_keyboard_enter(uint32_t serial, struct wl_surface* surface,
                           struct wl_array* keys);
    void on_keyboard_leave(uint32_t serial, struct wl_surface* surface);
    void on_keyboard_key(uint32_t serial, uint32_t time,
                         uint32_t keycode, uint32_t state);
    void on_keyboard_modifiers(uint32_t serial, uint32_t depressed,
                               uint32_t latched, uint32_t locked,
                               uint32_t group);
    void on_keyboard_repeat_info(int32_t rate, int32_t delay);
    void deliver_key(GuiKey key, GuiInputState mods);
    void maybe_fire_repeat();
    GuiInputState current_mods() const;

    // -- Pointer handlers --
    void on_pointer_enter(uint32_t serial, struct wl_surface* surface,
                          int32_t surface_x, int32_t surface_y);
    void on_pointer_leave(uint32_t serial, struct wl_surface* surface);
    void on_pointer_motion(uint32_t time, int32_t surface_x, int32_t surface_y);
    void on_pointer_button(uint32_t serial, uint32_t time,
                           uint32_t button, uint32_t state);
    void on_pointer_axis(uint32_t time, uint32_t axis, int32_t value);
    void on_pointer_axis_value120(uint32_t axis, int32_t value120);
    void on_pointer_frame();
};
