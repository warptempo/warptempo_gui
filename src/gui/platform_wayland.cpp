#include "platform_wayland.h"

#include "render.h"   // kMinWindowWidthPx / kMinWindowHeightPx

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xdg-shell-client-protocol.h>
#include <xdg-decoration-unstable-v1-client-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

// ---------------------------------------------------------------------------
// Run-loop architecture
//
// The run loop is built on a single poll() that waits indefinitely on the
// wl_display fd, the idle/playback timerfd, and the two optional renderer
// completion eventfds. The timer interval tracks the bound single output's
// current refresh half-period (2x vblank oversample), falling back to 60 Hz
// while no output mode is known. The poll wakes on whichever fd becomes
// readable first. Compositor events drive surface configure, input delivery,
// and frame callbacks (no clipboard or drag-and-drop path exists); renderer
// eventfds deliver async results.
// Timer wakeups drive the periodic model/validation callback and sample the
// monotonic key-repeat deadline.
//
// Paint pacing is separate from tick pacing. wl_surface.frame is a request
// the client makes after each commit asking the compositor to deliver a
// callback when the surface's next presentation is appropriate. The
// compositor knows when it is ready to display a new frame and tells the
// client directly, rather than the client inferring it from refresh rate.
// When a frame callback fires, paint_one_frame runs: it invokes the
// pre-paint hook (where the playhead's painted position is resolved from
// the playback predictor at paint time, so the rendered position is fresh
// as of the paint moment), consumes the damage list, paints into the next
// wl_shm buffer, attaches and commits, then schedules the next frame
// callback.
//
// The two cadences (tick and paint) drift independently. Conflating them
// by driving the tick off frame callbacks would mean that whenever the
// compositor pauses frame delivery — window occlusion, throttling during
// heavy compositor load, the user dragging another window over ours — the
// tick would also pause. The audio engine would continue regardless, and
// at the next frame callback after the pause the playhead would jump to
// its true current position. That jump is honest in the sense that it
// reflects where the audio actually is, but it is exactly the kind of
// discontinuity that user-initiated change does not mask. The separate
// timerfd keeps the tick running through compositor pauses; on resumption
// of frame delivery the next paint reads the predictor fresh and the
// playhead is where it should be without a visible jump.
//
// This separation is structurally identical to the project's approach to
// phase coherence in the phase vocoder. The project's PGHI phase vocoder
// sacrifices phase coherence at transients, accepting the discontinuity
// rather than coloring the signal with transient enhancement. The user
// manually places phase reset markers at transients, where the natural
// energy spike masks the phase reset's perceptual cost. Here, the GUI
// sacrifices continuous synchronization between paint cadence and playback
// cadence, accepting drift rather than calculating a unified clock.
// Moments of user-initiated change mask the re-sync's perceptual cost.
// Both are applications of the same minimalist principle: do not
// synthesize coherence the underlying mechanism cannot honestly provide;
// arrange for the inevitable discontinuity to occur where the user already
// expects change.
// ---------------------------------------------------------------------------

namespace {

uint64_t monotonic_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000ull +
           static_cast<uint64_t>(ts.tv_nsec) / 1000ull;
}

bool translate_pointer_button(uint32_t button, GuiMouseButton& out) {
    switch (button) {
        case BTN_LEFT:   out = GuiMouseButton::Left;   return true;
        case BTN_MIDDLE: out = GuiMouseButton::Middle; return true;
        case BTN_RIGHT:  out = GuiMouseButton::Right;  return true;
        default:         return false;
    }
}

template <typename Rect>
bool contains_rect(const Rect& outer, const Rect& inner) {
    return outer.x <= inner.x &&
           outer.y <= inner.y &&
           outer.x + outer.w >= inner.x + inner.w &&
           outer.y + outer.h >= inner.y + inner.h;
}

template <typename Rect>
bool append_coalesced_rect(std::vector<Rect>& rects, const Rect& nr) {
    for (const Rect& e : rects) {
        if (contains_rect(e, nr)) return false;
    }
    rects.push_back(nr);
    rects.erase(
        std::remove_if(rects.begin(), rects.end() - 1,
                       [&](const Rect& e) { return contains_rect(nr, e); }),
        rects.end() - 1);
    return true;
}

// Open an anonymous, sealable, in-memory file for the wl_shm pool. memfd
// is preferred (Linux ≥ 3.17). Falls back to shm_open + immediate unlink
// for portability hygiene.
int open_shm_fd(size_t size) {
    int fd = -1;
#ifdef MFD_CLOEXEC
    fd = memfd_create("warptempo_gui-shm", MFD_CLOEXEC);
#endif
    if (fd < 0) {
        char name[64];
        std::snprintf(name, sizeof(name), "/warptempo_gui-shm-%d-%ld",
                      getpid(), (long)time(nullptr));
        fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) shm_unlink(name);
    }
    if (fd < 0) return -1;
    if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

} // namespace

// ---------------------------------------------------------------------------
// Listener dispatch: file-static C-style functions that forward into private
// member methods via the GuiPlatform* stored in `data`. Friended in the
// class definition so the statics can reach private members directly.
// ---------------------------------------------------------------------------

struct WaylandListeners {
    // wl_registry
    static void registry_global(void* data, struct wl_registry* r,
                                uint32_t name, const char* interface,
                                uint32_t version) {
        static_cast<GuiPlatform*>(data)->on_registry_global(r, name, interface, version);
    }
    static void registry_global_remove(void* data, struct wl_registry*,
                                       uint32_t name) {
        static_cast<GuiPlatform*>(data)->on_registry_global_remove(name);
    }

    // xdg_wm_base
    static void wm_base_ping(void*, struct xdg_wm_base* base, uint32_t serial) {
        xdg_wm_base_pong(base, serial);
    }

    // wl_output
    //
    // Every slot the wl_output_listener struct exposes must be a real
    // function pointer. libwayland-client *aborts* (it does not silently
    // ignore) when the compositor dispatches an event whose listener slot
    // is NULL — `listener function for opcode N of wl_output is NULL`,
    // followed by abort(). At bind v=2, `done` (opcode 2) and `scale`
    // (opcode 3) are guaranteed to be dispatched following the initial
    // geometry/mode burst, so do-nothing stubs are mandatory. `name` and
    // `description` are v4+ and aren't dispatched at our bind version,
    // but stubs cost nothing and forward-compatibility is one less foot-
    // gun if the bind version is ever raised.
    static void output_geometry(void*, struct wl_output*, int32_t, int32_t,
                                int32_t, int32_t, int32_t, const char*,
                                const char*, int32_t) {}
    static void output_mode(void* data, struct wl_output*, uint32_t flags,
                            int32_t width, int32_t height, int32_t refresh_mhz) {
        static_cast<GuiPlatform*>(data)->on_output_mode(flags, width, height, refresh_mhz);
    }
    static void output_done(void*, struct wl_output*) {}
    // Scaling-unaware by design: the main surface keeps wl_surface's default
    // buffer scale 1, so the compositor scales its buffer while application
    // paint and pointer coordinates remain in one surface-local pixel space.
    // A scale change that also changes logical window dimensions arrives via
    // xdg_toplevel.configure and takes the normal resize/cancel path.
    static void output_scale(void*, struct wl_output*, int32_t) {}
    static void output_name(void*, struct wl_output*, const char*) {}
    static void output_description(void*, struct wl_output*, const char*) {}

    // xdg_surface
    static void xdg_surface_configure(void* data, struct xdg_surface* xs,
                                      uint32_t serial) {
        static_cast<GuiPlatform*>(data)->on_xdg_surface_configure(xs, serial);
    }

    // xdg_toplevel
    //
    // configure_bounds (v4+) and wm_capabilities (v5+) won't be dispatched
    // at our current bind version (1), but the generated listener struct
    // exposes the slots regardless. Filling them with stubs is cheap
    // forward-compat insurance against a future bind-version bump.
    static void toplevel_configure(void* data, struct xdg_toplevel*,
                                   int32_t width, int32_t height,
                                   struct wl_array*) {
        static_cast<GuiPlatform*>(data)->on_toplevel_configure(width, height);
    }
    static void toplevel_close(void* data, struct xdg_toplevel*) {
        static_cast<GuiPlatform*>(data)->on_toplevel_close();
    }
    static void toplevel_configure_bounds(void*, struct xdg_toplevel*,
                                          int32_t, int32_t) {}
    static void toplevel_wm_capabilities(void*, struct xdg_toplevel*,
                                         struct wl_array*) {}

    // wl_callback (frame)
    static void frame_done(void* data, struct wl_callback* cb, uint32_t /*time*/) {
        static_cast<GuiPlatform*>(data)->on_frame_done(cb);
    }

    // wl_buffer (release)
    static void buffer_release(void* data, struct wl_buffer*) {
        static_cast<GuiPlatform::ShmBuffer*>(data)->busy = false;
    }

    // wl_seat
    static void seat_capabilities(void* data, struct wl_seat*, uint32_t caps) {
        static_cast<GuiPlatform*>(data)->on_seat_capabilities(caps);
    }
    static void seat_name(void*, struct wl_seat*, const char*) {}

    // wl_keyboard
    static void keyboard_keymap(void* data, struct wl_keyboard*,
                                uint32_t format, int fd, uint32_t size) {
        static_cast<GuiPlatform*>(data)->on_keyboard_keymap(format, fd, size);
    }
    static void keyboard_enter(void* data, struct wl_keyboard*,
                               uint32_t serial, struct wl_surface* s,
                               struct wl_array* keys) {
        static_cast<GuiPlatform*>(data)->on_keyboard_enter(serial, s, keys);
    }
    static void keyboard_leave(void* data, struct wl_keyboard*,
                               uint32_t serial, struct wl_surface* s) {
        static_cast<GuiPlatform*>(data)->on_keyboard_leave(serial, s);
    }
    static void keyboard_key(void* data, struct wl_keyboard*,
                             uint32_t serial, uint32_t time,
                             uint32_t key, uint32_t state) {
        static_cast<GuiPlatform*>(data)->on_keyboard_key(serial, time, key, state);
    }
    static void keyboard_modifiers(void* data, struct wl_keyboard*,
                                   uint32_t serial,
                                   uint32_t depressed, uint32_t latched,
                                   uint32_t locked, uint32_t group) {
        static_cast<GuiPlatform*>(data)->on_keyboard_modifiers(
            serial, depressed, latched, locked, group);
    }
    static void keyboard_repeat_info(void* data, struct wl_keyboard*,
                                     int32_t rate, int32_t delay) {
        static_cast<GuiPlatform*>(data)->on_keyboard_repeat_info(rate, delay);
    }

    // wl_pointer
    static void pointer_enter(void* data, struct wl_pointer*,
                              uint32_t serial, struct wl_surface* surface,
                              wl_fixed_t sx, wl_fixed_t sy) {
        static_cast<GuiPlatform*>(data)->on_pointer_enter(
            serial, surface, sx, sy);
    }
    static void pointer_leave(void* data, struct wl_pointer*,
                              uint32_t serial, struct wl_surface* surface) {
        static_cast<GuiPlatform*>(data)->on_pointer_leave(serial, surface);
    }
    static void pointer_motion(void* data, struct wl_pointer*,
                               uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
        static_cast<GuiPlatform*>(data)->on_pointer_motion(time, sx, sy);
    }
    static void pointer_button(void* data, struct wl_pointer*,
                               uint32_t serial, uint32_t time,
                               uint32_t button, uint32_t state) {
        static_cast<GuiPlatform*>(data)->on_pointer_button(
            serial, time, button, state);
    }
    static void pointer_axis(void* data, struct wl_pointer*,
                             uint32_t time, uint32_t axis, wl_fixed_t value) {
        static_cast<GuiPlatform*>(data)->on_pointer_axis(time, axis, value);
    }

    // v5+ pointer events. We bind seat at v8, so pointer_frame and
    // pointer_axis_value120 are now live (they drive high-resolution
    // scroll); the remaining slots stay as stubs. They are all in the
    // listener struct because wayland-client-protocol.h ships them
    // regardless of bind version. Same abort-on-NULL rule as wl_output.
    static void pointer_frame(void* data, struct wl_pointer*) {
        static_cast<GuiPlatform*>(data)->on_pointer_frame();
    }
    static void pointer_axis_source(void*, struct wl_pointer*, uint32_t) {}
    static void pointer_axis_stop(void*, struct wl_pointer*,
                                  uint32_t, uint32_t) {}
    static void pointer_axis_discrete(void*, struct wl_pointer*,
                                      uint32_t, int32_t) {}

    // v8 axis_value120: high-resolution WHEEL scroll delta in 1/120-detent
    // units. Staged by on_pointer_axis_value120; the frame boundary
    // arbitrates it against the legacy axis (touchpad) stream and drains
    // to discrete steps. v9 relative_direction is still a stub.
    static void pointer_axis_value120(void* data, struct wl_pointer*,
                                      uint32_t axis, int32_t value120) {
        static_cast<GuiPlatform*>(data)->on_pointer_axis_value120(axis, value120);
    }
    static void pointer_axis_relative_direction(void*, struct wl_pointer*,
                                                uint32_t, uint32_t) {}
};

namespace {

// Listener structs use positional aggregate initialization. Every slot the
// installed wayland-protocols header exposes is filled with a real function
// pointer — libwayland-client aborts (`listener function for opcode N of
// <interface> is NULL`) when the compositor dispatches an event whose
// listener slot is NULL, so a NULL trailing slot is a latent crash, not a
// silent no-op. For events the implementation has no use for, a stub that
// does nothing satisfies the contract.
//
// -Wmissing-field-initializers stays suppressed as a defensive hedge: if a
// future wayland-protocols version adds new trailing events, this file will
// still compile, but those slots will be NULL and the same abort returns.
// The bind-version caps in on_registry_global() are the actual safety net
// against that — they prevent the compositor from advertising newer events
// than the listeners cover.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

const struct wl_registry_listener s_registry_listener = {
    WaylandListeners::registry_global,
    WaylandListeners::registry_global_remove,
};

const struct xdg_wm_base_listener s_wm_base_listener = {
    WaylandListeners::wm_base_ping,
};

const struct wl_output_listener s_output_listener = {
    WaylandListeners::output_geometry,
    WaylandListeners::output_mode,
    WaylandListeners::output_done,
    WaylandListeners::output_scale,
    WaylandListeners::output_name,
    WaylandListeners::output_description,
};

const struct xdg_surface_listener s_xdg_surface_listener = {
    WaylandListeners::xdg_surface_configure,
};

const struct xdg_toplevel_listener s_toplevel_listener = {
    WaylandListeners::toplevel_configure,
    WaylandListeners::toplevel_close,
    WaylandListeners::toplevel_configure_bounds,
    WaylandListeners::toplevel_wm_capabilities,
};

const struct wl_callback_listener s_frame_listener = {
    WaylandListeners::frame_done,
};

const struct wl_buffer_listener s_buffer_listener = {
    WaylandListeners::buffer_release,
};

const struct wl_seat_listener s_seat_listener = {
    WaylandListeners::seat_capabilities,
    WaylandListeners::seat_name,
};

const struct wl_keyboard_listener s_keyboard_listener = {
    WaylandListeners::keyboard_keymap,
    WaylandListeners::keyboard_enter,
    WaylandListeners::keyboard_leave,
    WaylandListeners::keyboard_key,
    WaylandListeners::keyboard_modifiers,
    WaylandListeners::keyboard_repeat_info,
};

const struct wl_pointer_listener s_pointer_listener = {
    WaylandListeners::pointer_enter,
    WaylandListeners::pointer_leave,
    WaylandListeners::pointer_motion,
    WaylandListeners::pointer_button,
    WaylandListeners::pointer_axis,
    WaylandListeners::pointer_frame,
    WaylandListeners::pointer_axis_source,
    WaylandListeners::pointer_axis_stop,
    WaylandListeners::pointer_axis_discrete,
    WaylandListeners::pointer_axis_value120,
    WaylandListeners::pointer_axis_relative_direction,
};

#pragma GCC diagnostic pop

} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

GuiPlatform::GuiPlatform() {}

GuiPlatform::~GuiPlatform() {
    destroy_wayland_state();
}

// ---------------------------------------------------------------------------
// init()
// ---------------------------------------------------------------------------

bool GuiPlatform::init(int width, int height, const char* title) {
    wl_display_ = wl_display_connect(nullptr);
    if (!wl_display_) {
        const char* wd = std::getenv("WAYLAND_DISPLAY");
        const char* xdg = std::getenv("XDG_RUNTIME_DIR");
        std::fprintf(stderr,
                     "warptempo_gui: wl_display_connect failed "
                     "(WAYLAND_DISPLAY=\"%s\", XDG_RUNTIME_DIR=\"%s\")\n",
                     wd  ? wd  : "",
                     xdg ? xdg : "");
        return false;
    }

    wl_registry_ = wl_display_get_registry(wl_display_);
    wl_registry_add_listener(wl_registry_, &s_registry_listener, this);

    // Two roundtrips: first surfaces the registry advertisements, second
    // drains any output-mode events that follow the wl_output bind. Without
    // the second roundtrip the refresh rate can be missing on first use.
    wl_display_roundtrip(wl_display_);
    wl_display_roundtrip(wl_display_);

    if (!wl_compositor_ || !wl_shm_ || !xdg_wm_base_) {
        std::fprintf(stderr,
                     "warptempo_gui: required wayland globals missing "
                     "(wl_compositor=%p wl_shm=%p xdg_wm_base=%p)\n",
                     (void*)wl_compositor_, (void*)wl_shm_, (void*)xdg_wm_base_);
        return false;
    }
    if (!xdg_decoration_manager_) {
        std::fprintf(stderr,
                     "warptempo_gui: zxdg_decoration_manager_v1 not advertised; "
                     "window will appear without server-side decorations\n");
    }
    if (!wl_output_) {
        std::fprintf(stderr,
                     "warptempo_gui: no wl_output advertised; "
                     "playback tick will use 60 hz fallback\n");
    }

    width_  = width;
    height_ = height;

    // Build the surface chain: wl_surface → xdg_surface → xdg_toplevel.
    wl_surface_   = wl_compositor_create_surface(wl_compositor_);
    xdg_surface_  = xdg_wm_base_get_xdg_surface(xdg_wm_base_, wl_surface_);
    xdg_surface_add_listener(xdg_surface_, &s_xdg_surface_listener, this);

    xdg_toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
    xdg_toplevel_add_listener(xdg_toplevel_, &s_toplevel_listener, this);
    set_title(title ? title : "warptempo_gui");
    xdg_toplevel_set_app_id(xdg_toplevel_, "warptempo_gui");
    xdg_toplevel_set_maximized(xdg_toplevel_);
    // Ask the compositor to refuse sizing the surface below the
    // 640x480 floor. The geometry helpers also clamp internally, so the
    // waveform arithmetic stays valid even if a compositor ignores the hint.
    xdg_toplevel_set_min_size(xdg_toplevel_,
                              kMinWindowWidthPx, kMinWindowHeightPx);

    if (xdg_decoration_manager_) {
        xdg_toplevel_decoration_ = zxdg_decoration_manager_v1_get_toplevel_decoration(
            xdg_decoration_manager_, xdg_toplevel_);
        zxdg_toplevel_decoration_v1_set_mode(
            xdg_toplevel_decoration_,
            ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    recreate_shm_pool(width_, height_);

    // Cursor theme load is best-effort: failure leaves cursor_surface_ NULL,
    // and on_pointer_enter then passes NULL to wl_pointer.set_cursor (which
    // hides the cursor over our window). That degraded state is acceptable —
    // the GUI is still fully usable.
    load_cursor_theme();

    xkb_context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_context_) {
        std::fprintf(stderr,
                     "warptempo_gui: xkb_context_new failed; "
                     "keyboard input disabled\n");
        // Non-fatal — the binary can still run, just without keyboard input.
    }

    timerfd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timerfd_ < 0) {
        std::fprintf(stderr, "warptempo_gui: timerfd_create failed: %s\n",
                     std::strerror(errno));
        return false;
    }
    if (!arm_playback_timer()) return false;

    // Initial commit so the compositor delivers the first xdg_surface
    // configure. The configure handler then acks, sets has_initial_configure_,
    // and arms the frame-callback chain.
    wl_surface_commit(wl_surface_);

    return true;
}

void GuiPlatform::set_title(const std::string& title) {
    if (!xdg_toplevel_) return;
    xdg_toplevel_set_title(xdg_toplevel_, title.c_str());
}

// ---------------------------------------------------------------------------
// shutdown / request_exit / destroy_wayland_state
// ---------------------------------------------------------------------------

void GuiPlatform::shutdown() {
    destroy_wayland_state();
}

void GuiPlatform::request_exit() {
    should_exit_ = true;
}

void GuiPlatform::destroy_wayland_state() {
    if (timerfd_ >= 0) {
        close(timerfd_);
        timerfd_ = -1;
    }

    if (wl_keyboard_) {
        wl_keyboard_release(wl_keyboard_);
        wl_keyboard_ = nullptr;
    }
    if (wl_pointer_) {
        wl_pointer_release(wl_pointer_);
        wl_pointer_ = nullptr;
    }
    if (wl_seat_) {
        // wl_seat.release is a v5+ request. We now bind at v8, but
        // wl_seat_destroy (plain wl_proxy_destroy) is still correct at
        // shutdown — it tears the proxy down unconditionally without
        // needing the release round-trip, so this guard stays as-is.
        wl_seat_destroy(wl_seat_);
        wl_seat_ = nullptr;
        seat_global_name_ = 0;
    }
    if (xkb_state_) {
        xkb_state_unref(xkb_state_);
        xkb_state_ = nullptr;
    }
    if (xkb_keymap_) {
        xkb_keymap_unref(xkb_keymap_);
        xkb_keymap_ = nullptr;
    }
    if (xkb_context_) {
        xkb_context_unref(xkb_context_);
        xkb_context_ = nullptr;
    }

    if (frame_callback_) {
        wl_callback_destroy(frame_callback_);
        frame_callback_ = nullptr;
    }

    if (xdg_toplevel_decoration_) {
        zxdg_toplevel_decoration_v1_destroy(xdg_toplevel_decoration_);
        xdg_toplevel_decoration_ = nullptr;
    }
    if (xdg_toplevel_) {
        xdg_toplevel_destroy(xdg_toplevel_);
        xdg_toplevel_ = nullptr;
    }
    if (xdg_surface_) {
        xdg_surface_destroy(xdg_surface_);
        xdg_surface_ = nullptr;
    }
    if (wl_surface_) {
        wl_surface_destroy(wl_surface_);
        wl_surface_ = nullptr;
    }

    // Cursor: destroy the surface before the theme. The surface holds a
    // buffer owned by the theme, and freeing in dependency order avoids
    // any compositor surprise even though libwayland-cursor's buffers can
    // technically outlive the surface.
    if (cursor_surface_) {
        wl_surface_destroy(cursor_surface_);
        cursor_surface_ = nullptr;
    }
    if (wl_cursor_theme_) {
        wl_cursor_theme_destroy(wl_cursor_theme_);
        wl_cursor_theme_ = nullptr;
        wl_cursor_arrow_ = nullptr;  // owned by the theme; no destroy
    }

    destroy_shm_pool();

    if (xdg_decoration_manager_) {
        zxdg_decoration_manager_v1_destroy(xdg_decoration_manager_);
        xdg_decoration_manager_ = nullptr;
    }
    if (xdg_wm_base_) {
        xdg_wm_base_destroy(xdg_wm_base_);
        xdg_wm_base_ = nullptr;
    }
    if (wl_output_) {
        wl_output_destroy(wl_output_);
        wl_output_ = nullptr;
        output_global_name_ = 0;
    }
    if (wl_shm_) {
        wl_shm_destroy(wl_shm_);
        wl_shm_ = nullptr;
    }
    if (wl_compositor_) {
        wl_compositor_destroy(wl_compositor_);
        wl_compositor_ = nullptr;
    }
    if (wl_registry_) {
        wl_registry_destroy(wl_registry_);
        wl_registry_ = nullptr;
    }
    if (wl_display_) {
        wl_display_disconnect(wl_display_);
        wl_display_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Shared-memory pool (double-buffered)
// ---------------------------------------------------------------------------

void GuiPlatform::recreate_shm_pool(int w, int h) {
    destroy_shm_pool();

    if (w <= 0 || h <= 0) return;

    const int    stride       = w * 4;                         // ARGB32
    const size_t buffer_bytes = static_cast<size_t>(stride) * static_cast<size_t>(h);
    const size_t pool_bytes   = buffer_bytes * kShmBufferCount;

    shm_pool_fd_ = open_shm_fd(pool_bytes);
    if (shm_pool_fd_ < 0) {
        std::fprintf(stderr, "warptempo_gui: failed to open shm fd: %s\n",
                     std::strerror(errno));
        return;
    }

    shm_pool_map_ = mmap(nullptr, pool_bytes, PROT_READ | PROT_WRITE,
                         MAP_SHARED, shm_pool_fd_, 0);
    if (shm_pool_map_ == MAP_FAILED) {
        std::fprintf(stderr, "warptempo_gui: mmap of shm pool failed: %s\n",
                     std::strerror(errno));
        close(shm_pool_fd_);
        shm_pool_fd_  = -1;
        shm_pool_map_ = nullptr;
        return;
    }
    shm_pool_size_ = pool_bytes;

    shm_pool_ = wl_shm_create_pool(wl_shm_, shm_pool_fd_,
                                   static_cast<int32_t>(pool_bytes));

    for (int i = 0; i < kShmBufferCount; ++i) {
        const size_t offset = static_cast<size_t>(i) * buffer_bytes;
        shm_buffers_[i].pixels     = static_cast<char*>(shm_pool_map_) + offset;
        shm_buffers_[i].busy       = false;
        shm_buffers_[i].pending.clear();

        shm_buffers_[i].buffer = wl_shm_pool_create_buffer(
            shm_pool_,
            static_cast<int32_t>(offset),
            w, h, stride,
            WL_SHM_FORMAT_ARGB8888);
        wl_buffer_add_listener(shm_buffers_[i].buffer,
                               &s_buffer_listener,
                               &shm_buffers_[i]);

        shm_buffers_[i].surface = cairo_image_surface_create_for_data(
            static_cast<unsigned char*>(shm_buffers_[i].pixels),
            CAIRO_FORMAT_ARGB32,
            w, h, stride);
        shm_buffers_[i].pending.push_back(DamageRect{0, 0, w, h});
    }
}

void GuiPlatform::destroy_shm_pool() {
    for (int i = 0; i < kShmBufferCount; ++i) {
        if (shm_buffers_[i].surface) {
            cairo_surface_destroy(shm_buffers_[i].surface);
            shm_buffers_[i].surface = nullptr;
        }
        if (shm_buffers_[i].buffer) {
            wl_buffer_destroy(shm_buffers_[i].buffer);
            shm_buffers_[i].buffer = nullptr;
        }
        shm_buffers_[i].pixels     = nullptr;
        shm_buffers_[i].busy       = false;
        shm_buffers_[i].pending.clear();
    }
    if (shm_pool_) {
        wl_shm_pool_destroy(shm_pool_);
        shm_pool_ = nullptr;
    }
    if (shm_pool_map_) {
        munmap(shm_pool_map_, shm_pool_size_);
        shm_pool_map_  = nullptr;
        shm_pool_size_ = 0;
    }
    if (shm_pool_fd_ >= 0) {
        close(shm_pool_fd_);
        shm_pool_fd_ = -1;
    }
}

bool GuiPlatform::load_cursor_theme() {
    // Size honors XCURSOR_SIZE if the user has set it; otherwise 24 is the
    // freedesktop fallback.
    int size = 24;
    if (const char* env = std::getenv("XCURSOR_SIZE")) {
        int parsed = std::atoi(env);
        if (parsed > 0) size = parsed;
    }

    // Theme name NULL = "system default" per libwayland-cursor.
    wl_cursor_theme_ = wl_cursor_theme_load(nullptr, size, wl_shm_);
    if (!wl_cursor_theme_) {
        std::fprintf(stderr,
            "warptempo_gui: wl_cursor_theme_load failed; "
            "pointer will not display a cursor image\n");
        return false;
    }

    // left_ptr is the freedesktop standard arrow name. If the active theme
    // is missing it, the theme is broken; report and move on without a
    // cursor rather than guess at an alternative.
    wl_cursor_arrow_ = wl_cursor_theme_get_cursor(wl_cursor_theme_, "left_ptr");
    if (!wl_cursor_arrow_ || wl_cursor_arrow_->image_count == 0) {
        std::fprintf(stderr,
            "warptempo_gui: cursor theme has no \"left_ptr\"; "
            "pointer will not display a cursor image\n");
        return false;
    }

    // First frame only; animated arrows (rare) collapse to frame 0.
    struct wl_cursor_image* image = wl_cursor_arrow_->images[0];
    struct wl_buffer* buf = wl_cursor_image_get_buffer(image);
    if (!buf) {
        std::fprintf(stderr,
            "warptempo_gui: wl_cursor_image_get_buffer returned null; "
            "pointer will not display a cursor image\n");
        return false;
    }

    cursor_hotspot_x_ = static_cast<int32_t>(image->hotspot_x);
    cursor_hotspot_y_ = static_cast<int32_t>(image->hotspot_y);

    // Dedicated cursor surface, created once for the process lifetime.
    // The buffer attachment is sticky; we never switch images.
    cursor_surface_ = wl_compositor_create_surface(wl_compositor_);
    if (!cursor_surface_) {
        std::fprintf(stderr,
            "warptempo_gui: wl_compositor_create_surface for cursor failed\n");
        return false;
    }
    wl_surface_attach(cursor_surface_, buf, 0, 0);
    wl_surface_damage(cursor_surface_, 0, 0, image->width, image->height);
    wl_surface_commit(cursor_surface_);
    return true;
}

GuiPlatform::ShmBuffer* GuiPlatform::acquire_free_buffer() {
    for (int i = 0; i < kShmBufferCount; ++i) {
        if (!shm_buffers_[i].busy && shm_buffers_[i].buffer) return &shm_buffers_[i];
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Frame callback flow
// ---------------------------------------------------------------------------

void GuiPlatform::schedule_frame_callback() {
    if (frame_callback_ || !wl_surface_) return;
    frame_callback_ = wl_surface_frame(wl_surface_);
    wl_callback_add_listener(frame_callback_, &s_frame_listener, this);
    wl_surface_commit(wl_surface_);
}

/*
 * Each buffer's pending list is the damage accumulated since that buffer was
 * last attached. Painting and surface-damaging exactly that list makes an
 * attach correct regardless of which buffer the compositor was holding.
 */
void GuiPlatform::paint_one_frame() {
    if (!has_initial_configure_) return;

    if (damage_.empty()) {
        schedule_frame_callback();
        return;
    }

    // Pre-paint hook: gives the application a chance to update model
    // state (e.g., re-read the playback predictor) and declare any
    // additional damage based on the freshly-updated state. The hook
    // runs before buffer acquisition because it may add to damage_.
    // invalidate_region() suppresses its trailing schedule_frame_callback()
    // while in_pre_paint_ is true so we don't produce a spurious empty
    // commit before the real attach + commit below.
    if (on_pre_paint_) {
        in_pre_paint_ = true;
        on_pre_paint_();
        in_pre_paint_ = false;
    }

    ShmBuffer* buf = acquire_free_buffer();
    if (!buf) {
        // Both buffers in flight — try again next compositor frame.
        schedule_frame_callback();
        return;
    }

    cairo_t* cr = cairo_create(buf->surface);
    for (const DamageRect& d : buf->pending) {
        cairo_save(cr);
        cairo_rectangle(cr, d.x, d.y, d.w, d.h);
        cairo_clip(cr);
        if (on_redraw_) on_redraw_(cr, d.x, d.y, d.w, d.h);
        cairo_restore(cr);
    }
    cairo_destroy(cr);

    for (const DamageRect& d : buf->pending) {
        wl_surface_damage_buffer(wl_surface_, d.x, d.y, d.w, d.h);
    }

    wl_surface_attach(wl_surface_, buf->buffer, 0, 0);
    wl_surface_commit(wl_surface_);
    buf->busy = true;

    buf->pending.clear();
    damage_.clear();

    schedule_frame_callback();
}

void GuiPlatform::invalidate_region(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;

    // Each surviving rect costs one on_redraw call downstream, so the
    // global damage signal and every per-buffer pending list use the same
    // containment coalescing.
    const DamageRect nr{x, y, w, h};
    if (!append_coalesced_rect(damage_, nr)) return;
    for (int i = 0; i < kShmBufferCount; ++i) {
        append_coalesced_rect(shm_buffers_[i].pending, nr);
    }

    if (in_pre_paint_) return;  // paint_one_frame will commit shortly
    if (has_initial_configure_ && !frame_callback_) {
        schedule_frame_callback();
    }
}

// ---------------------------------------------------------------------------
// Run loop
// ---------------------------------------------------------------------------

void GuiPlatform::run() {
    while (!should_exit_) {
        // Canonical libwayland-client poll-loop dance: prepare_read /
        // read_events / cancel_read. Replacing this with wl_display_dispatch
        // produces hangs under concurrent event traffic.
        while (wl_display_prepare_read(wl_display_) != 0) {
            wl_display_dispatch_pending(wl_display_);
        }
        if (wl_display_flush(wl_display_) < 0 && errno != EAGAIN) {
            std::fprintf(stderr,
                         "warptempo_gui: connection to the compositor lost; "
                         "wl_display_flush failed: %s\n",
                         std::strerror(errno));
            should_exit_ = true;
            wl_display_cancel_read(wl_display_);
            break;
        }

        // pfds[2] is the async-render completion eventfd; pfds[3] is the
        // waveform-worker completion eventfd. When no fd is
        // registered (fd == -1), events=0 so poll() ignores the slot —
        // same trick used for "watch only when we care."
        struct pollfd pfds[4];
        pfds[0].fd     = wl_display_get_fd(wl_display_);
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        pfds[1].fd     = timerfd_;
        pfds[1].events = POLLIN;
        pfds[1].revents = 0;
        pfds[2].fd     = worker_completion_fd_;
        pfds[2].events = (worker_completion_fd_ >= 0) ? POLLIN : 0;
        pfds[2].revents = 0;
        pfds[3].fd     = waveform_worker_completion_fd_;
        pfds[3].events = (waveform_worker_completion_fd_ >= 0) ? POLLIN : 0;
        pfds[3].revents = 0;

        int n = poll(pfds, 4, -1);

        if (n < 0) {
            if (errno == EINTR) {
                wl_display_cancel_read(wl_display_);
                continue;
            }
            wl_display_cancel_read(wl_display_);
            std::fprintf(stderr, "warptempo_gui: poll() failed: %s\n",
                         std::strerror(errno));
            should_exit_ = true;
            break;
        }

        if (pfds[0].revents & POLLIN) {
            if (wl_display_read_events(wl_display_) < 0) {
                std::fprintf(stderr,
                             "warptempo_gui: wl_display_read_events failed\n");
                should_exit_ = true;
                break;
            }
            wl_display_dispatch_pending(wl_display_);
        } else {
            wl_display_cancel_read(wl_display_);
        }

        if (pfds[0].revents & (POLLHUP | POLLERR)) {
            const bool hup = (pfds[0].revents & POLLHUP) != 0;
            const bool err = (pfds[0].revents & POLLERR) != 0;
            std::fprintf(stderr,
                         "warptempo_gui: connection to the compositor lost; "
                         "display fd reported %s%s%s\n",
                         hup ? "POLLHUP" : "",
                         (hup && err) ? " and " : "",
                         err ? "POLLERR" : "");
            should_exit_ = true;
            break;
        }

        if (pfds[1].revents & POLLIN) {
            uint64_t expirations = 0;
            (void)read(timerfd_, &expirations, sizeof(expirations));
            if (on_tick_) on_tick_();
            maybe_fire_repeat();
        }

        if (worker_completion_fd_ >= 0 && (pfds[2].revents & POLLIN)) {
            uint64_t cnt = 0;
            (void)read(worker_completion_fd_, &cnt, sizeof(cnt));
            if (on_worker_completion_) on_worker_completion_();
        }

        if (waveform_worker_completion_fd_ >= 0 &&
            (pfds[3].revents & POLLIN)) {
            uint64_t cnt = 0;
            (void)read(waveform_worker_completion_fd_, &cnt, sizeof(cnt));
            if (on_waveform_worker_completion_) {
                on_waveform_worker_completion_();
            }
        }
    }

    wl_display_dispatch_pending(wl_display_);
}

void GuiPlatform::drain_events() {
    if (wl_display_) wl_display_dispatch_pending(wl_display_);
}

void GuiPlatform::paint_now() {
    // Synchronous render + commit + flush for the one case that can't wait for
    // run()'s frame-callback loop: a blocking load. paint_one_frame is null-safe
    // before the initial configure and no-ops on empty damage; the flush pushes
    // the commit to the compositor now instead of on the loop's next pass.
    paint_one_frame();
    if (wl_display_) wl_display_flush(wl_display_);
}

// ---------------------------------------------------------------------------
// Refresh-rate detection
// ---------------------------------------------------------------------------

int GuiPlatform::detect_refresh_rate_ms() {
    int hz = 60;
    if (output_refresh_mhz_ > 0) {
        const int reported = output_refresh_mhz_ / 1000;
        if (reported > 0) hz = reported;
    }
    int t = 1000 / hz / 2;
    if (t < 1)  t = 1;
    if (t > 16) t = 16;
    return t;
}

bool GuiPlatform::arm_playback_timer() {
    playback_tick_ms_ = detect_refresh_rate_ms();
    if (timerfd_ < 0) return true;

    struct itimerspec its{};
    its.it_value.tv_sec     = playback_tick_ms_ / 1000;
    its.it_value.tv_nsec    = (playback_tick_ms_ % 1000) * 1000000L;
    its.it_interval.tv_sec  = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;
    if (timerfd_settime(timerfd_, 0, &its, nullptr) < 0) {
        std::fprintf(stderr, "warptempo_gui: timerfd_settime failed: %s\n",
                     std::strerror(errno));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Wayland event handlers
// ---------------------------------------------------------------------------

void GuiPlatform::on_registry_global(struct wl_registry* r, uint32_t name,
                                     const char* interface, uint32_t version) {
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        const uint32_t v = std::min<uint32_t>(version, 4);  // need v4 for damage_buffer
        wl_compositor_ = static_cast<struct wl_compositor*>(
            wl_registry_bind(r, name, &wl_compositor_interface, v));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        wl_shm_ = static_cast<struct wl_shm*>(
            wl_registry_bind(r, name, &wl_shm_interface, 1));
    } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
        const uint32_t v = std::min<uint32_t>(version, 1);
        xdg_wm_base_ = static_cast<struct xdg_wm_base*>(
            wl_registry_bind(r, name, &xdg_wm_base_interface, v));
        xdg_wm_base_add_listener(xdg_wm_base_, &s_wm_base_listener, this);
    } else if (std::strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        xdg_decoration_manager_ = static_cast<struct zxdg_decoration_manager_v1*>(
            wl_registry_bind(r, name, &zxdg_decoration_manager_v1_interface, 1));
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        // Bind only the first wl_output advertised. Multi-monitor is out of
        // scope here; the first output's refresh rate is used as
        // the timerfd interval source.
        if (!wl_output_) {
            const uint32_t v = std::min<uint32_t>(version, 2);
            wl_output_ = static_cast<struct wl_output*>(
                wl_registry_bind(r, name, &wl_output_interface, v));
            output_global_name_ = name;
            output_refresh_mhz_ = 0;  // a replacement output starts cacheless
            wl_output_add_listener(wl_output_, &s_output_listener, this);
        }
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0 &&
               !wl_seat_) {
        // Cap to version 8. v4 gave us key repeat; v8 adds
        // wl_pointer.axis_value120, which gives high-resolution WHEEL
        // scroll. Touchpad two-finger scroll still arrives via the legacy
        // wl_pointer.axis event (value120 is wheel-only); on_pointer_frame()
        // arbitrates the two streams so each source counts once per frame
        // and drains to discrete wheel steps. v5 release semantics still
        // apply at v8 (see the wl_seat cleanup in destroy_wayland_state).
        const uint32_t v = std::min<uint32_t>(version, 8);
        wl_seat_ = static_cast<struct wl_seat*>(
            wl_registry_bind(r, name, &wl_seat_interface, v));
        seat_global_name_ = name;
        wl_seat_add_listener(wl_seat_, &s_seat_listener, this);
    }
}

void GuiPlatform::on_registry_global_remove(uint32_t name) {
    if (wl_output_ && name == output_global_name_) {
        // wl_registry.global_remove makes the bound object unavailable and
        // asks the client to destroy its proxy. Clearing the singleton slot
        // matters as much as destruction: a later wl_output advertisement can
        // now bind and become the new single-monitor timing source.
        wl_output_destroy(wl_output_);  // bound at v2; release is v3
        wl_output_ = nullptr;
        output_global_name_ = 0;
        output_refresh_mhz_ = 0;
        if (timerfd_ >= 0) (void)arm_playback_timer();  // 60 Hz fallback
        return;
    }

    if (!wl_seat_ || name != seat_global_name_) return;

    // A registry removal is a harder seat-lifetime boundary than a
    // capability loss: no final capabilities event is required. Run the same
    // keyboard/pointer teardown first so held gestures get their release tail
    // and no modifier/scroll state crosses into a replacement seat.
    on_seat_capabilities(0);

    wl_seat_destroy(wl_seat_);
    wl_seat_ = nullptr;
    seat_global_name_ = 0;
}

void GuiPlatform::on_output_mode(uint32_t flags, int32_t /*width*/,
                                 int32_t /*height*/, int32_t refresh_mhz) {
    // WL_OUTPUT_MODE_CURRENT (bit 0) marks the active mode. The protocol makes
    // the most recently reported CURRENT mode authoritative, including a
    // transition to a lower refresh rate, so replace rather than max-merge.
    if ((flags & WL_OUTPUT_MODE_CURRENT) == 0) return;
    output_refresh_mhz_ = refresh_mhz;
    // The initial mode burst precedes timerfd creation. Later mode changes or
    // a replacement output re-arm the idle/playback/input-repeat tick now.
    if (timerfd_ >= 0) (void)arm_playback_timer();
}

void GuiPlatform::on_xdg_surface_configure(struct xdg_surface* xs,
                                           uint32_t serial) {
    xdg_surface_ack_configure(xs, serial);

    const bool first = !has_initial_configure_;
    auto queue_full_surface_damage = [&]() {
        const DamageRect full{0, 0, width_, height_};
        append_coalesced_rect(damage_, full);
        for (int i = 0; i < kShmBufferCount; ++i) {
            append_coalesced_rect(shm_buffers_[i].pending, full);
        }
    };

    if (first) {
        has_initial_configure_ = true;
        if (pending_w_ > 0 && pending_h_ > 0 &&
            (pending_w_ != width_ || pending_h_ != height_)) {
            width_  = pending_w_;
            height_ = pending_h_;
            recreate_shm_pool(width_, height_);
        }
        queue_full_surface_damage();
        if (on_resize_) on_resize_(width_, height_);
        paint_one_frame();
        return;
    }

    // Subsequent configure: act on any pending dimension change.
    if (pending_w_ > 0 && pending_h_ > 0 &&
        (pending_w_ != width_ || pending_h_ != height_)) {
        width_  = pending_w_;
        height_ = pending_h_;
        recreate_shm_pool(width_, height_);
        queue_full_surface_damage();
        if (on_resize_) on_resize_(width_, height_);
    } else if (damage_.empty()) {
        // No size change and nothing pending — still schedule a paint so
        // the compositor's reconfigure (e.g. activation/maximize state
        // change) gets honored.
        queue_full_surface_damage();
    }

    if (!frame_callback_) schedule_frame_callback();
}

void GuiPlatform::on_toplevel_configure(int32_t width, int32_t height) {
    pending_w_ = width;
    pending_h_ = height;
}

void GuiPlatform::on_toplevel_close() {
    // The compositor (title-bar X) REQUESTS a close; it does not force one.
    // Delegate to the close callback, which routes through the unsaved-work
    // dialog and calls request_exit() itself once the user confirms — or
    // immediately when the document is clean. Setting should_exit_ here
    // unconditionally was the bug: the run loop (while !should_exit_) exited in
    // the same frame the dialog opened, so a dirty document closed without the
    // prompt. Ctrl+Q was unaffected because it reaches request_close
    // directly and only exits via proceed() when clean. Honor the close
    // directly only when no callback is wired.
    if (on_close_) on_close_();
    else           should_exit_ = true;
}

void GuiPlatform::on_frame_done(struct wl_callback* cb) {
    if (cb == frame_callback_) frame_callback_ = nullptr;
    wl_callback_destroy(cb);
    paint_one_frame();
}

// ---------------------------------------------------------------------------
// Keyboard event handlers
// ---------------------------------------------------------------------------

void GuiPlatform::on_seat_capabilities(uint32_t caps) {
    const bool has_kb      = (caps & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    const bool has_pointer = (caps & WL_SEAT_CAPABILITY_POINTER)  != 0;

    if (has_kb && !wl_keyboard_) {
        wl_keyboard_ = wl_seat_get_keyboard(wl_seat_);
        wl_keyboard_add_listener(wl_keyboard_, &s_keyboard_listener, this);
    } else if (!has_kb && wl_keyboard_) {
        wl_keyboard_release(wl_keyboard_);
        wl_keyboard_ = nullptr;
        // Capability loss need not be preceded by keyboard.leave. Drop both
        // repeat and the cached modifier projection here so pointer input
        // delivered while no keyboard exists cannot inherit a phantom chord
        // from the last keyboard event (for example the Ctrl+Alt+wheel end-move).
        repeat_key_ = 0;
        mod_ctrl_ = mod_shift_ = mod_alt_ = false;

        // A held synthesized-Ctrl modifier can never see its keycode-matched
        // release once the key stream ends, so drop it here beside the mod
        // clears. No release event to deliver — the state just falls to off,
        // exactly as the real mod_* bits do.
        synth_ctrl_held_    = false;
        synth_ctrl_keycode_ = 0;

        // Losing the keyboard is the hard end of the key stream: a held
        // synthesized-left button will never see its release. End it here on
        // the logical 1->0 edge, the same tail as on_keyboard_leave.
        if (synth_left_held_) {
            synth_left_held_    = false;
            synth_left_keycode_ = 0;
            if (!pointer_left_held_ && on_button_release_)
                on_button_release_(GuiMouseButton::Left,
                                   pointer_x_, pointer_y_, current_mods());
        }
    }

    if (has_pointer && !wl_pointer_) {
        wl_pointer_ = wl_seat_get_pointer(wl_seat_);
        wl_pointer_add_listener(wl_pointer_, &s_pointer_listener, this);
    } else if (!has_pointer && wl_pointer_) {
        const bool left_was_held = pointer_left_held_;
        wl_pointer_release(wl_pointer_);
        wl_pointer_ = nullptr;
        pointer_focused_   = false;
        pointer_left_held_ = false;

        // Capability loss is the hard end of this wl_pointer event stream:
        // the protocol guarantees that no further events (and therefore no
        // matching button release or frame boundary) arrive on this object.
        // End a held application gesture at the last delivered coordinates,
        // using the same release tail as an ordinary/lost-button finish, so
        // marker/trim commits and the three navigation/text gestures cannot
        // remain stuck until a future pointer capability happens to appear.
        //
        // The logical left button is `pointer_left_held_ || synth_left_held_`.
        // Clearing the physical bit ends the gesture only when it drives that
        // OR 1->0 -- physical left was held AND no synthesized hold remains. If
        // a synthesized left is still held (bare `e` synth-left plus a physical
        // BTN_LEFT), the OR stays 1: leave the synth hold and its keycode owner
        // untouched so its later release delivers the single 1->0 edge, and
        // suppress the release here. This mirrors the keyboard-leave/capability
        // tails, which likewise hold the release while the OTHER source is held.
        if (left_was_held && !synth_left_held_ && on_button_release_) {
            on_button_release_(GuiMouseButton::Left,
                               pointer_x_, pointer_y_, current_mods());
        }

        // A sub-detent carry and the staged half of a logical pointer frame
        // belong to the destroyed pointer object. They must not combine with
        // input from a later wl_pointer created when the seat regains the
        // capability, even if cursor region and modifiers happen to match.
        scroll_accum_       = 0.0;
        scroll_context_key_ = 0;
        frame_v120_accum_   = 0.0;
        frame_axis_accum_   = 0.0;
        frame_have_v120_    = false;
        frame_have_axis_    = false;
    }
    // Touch capability bit remains ignored.
}

void GuiPlatform::on_keyboard_keymap(uint32_t format, int fd, uint32_t size) {
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        std::fprintf(stderr,
                     "warptempo_gui: unsupported keymap format %u\n", format);
        close(fd);
        return;
    }
    if (!xkb_context_) {
        close(fd);
        return;
    }

    char* mapped = static_cast<char*>(
        mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    close(fd);
    if (mapped == MAP_FAILED) {
        std::fprintf(stderr,
                     "warptempo_gui: keymap mmap failed: %s\n",
                     std::strerror(errno));
        return;
    }

    struct xkb_keymap* km = xkb_keymap_new_from_string(
        xkb_context_, mapped, XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(mapped, size);
    if (!km) {
        std::fprintf(stderr,
                     "warptempo_gui: xkb_keymap_new_from_string failed\n");
        return;
    }

    struct xkb_state* st = xkb_state_new(km);
    if (!st) {
        xkb_keymap_unref(km);
        std::fprintf(stderr, "warptempo_gui: xkb_state_new failed\n");
        return;
    }

    // Swap in. Free any prior state/keymap.
    if (xkb_state_)  xkb_state_unref(xkb_state_);
    if (xkb_keymap_) xkb_keymap_unref(xkb_keymap_);
    xkb_keymap_ = km;
    xkb_state_  = st;
}

void GuiPlatform::on_keyboard_enter(uint32_t /*serial*/,
                                    struct wl_surface* /*surface*/,
                                    struct wl_array* /*keys*/) {
    // No-op. Modifier state arrives via the modifiers event; per-key
    // state arrives via subsequent key events.
}

void GuiPlatform::on_keyboard_leave(uint32_t /*serial*/,
                                    struct wl_surface* /*surface*/) {
    mod_ctrl_ = mod_shift_ = mod_alt_ = false;
    repeat_key_ = 0;

    // A held synthesized-Ctrl modifier can never see its keycode-matched
    // release once keyboard focus is gone, so drop it here beside the mod
    // clears. No release event to deliver — the state just falls to off, the
    // same tail as the pointer/keyboard capability-loss branches.
    synth_ctrl_held_    = false;
    synth_ctrl_keycode_ = 0;

    // A held synthesized-left button can never see its keycode-matched release
    // once keyboard focus is gone, so end it here on the logical 1->0 edge —
    // the same rationale as the pointer-capability-loss tail that ends a held
    // physical button. A concurrent physical hold keeps the button down.
    if (synth_left_held_) {
        synth_left_held_    = false;
        synth_left_keycode_ = 0;
        if (!pointer_left_held_ && on_button_release_)
            on_button_release_(GuiMouseButton::Left,
                               pointer_x_, pointer_y_, current_mods());
    }
}

void GuiPlatform::on_keyboard_key(uint32_t /*serial*/, uint32_t /*time*/,
                                  uint32_t keycode, uint32_t state) {
    if (!xkb_state_) return;

    // Wayland delivers raw evdev keycodes (offset by 8 for X11
    // compatibility — xkbcommon expects this offset).
    const uint32_t xkb_keycode = keycode + 8;

    if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        // Cancel repeat if the released key was the one repeating.
        if (xkb_keycode == repeat_keycode_) {
            repeat_key_ = 0;
        }
        // End a synthesized-left hold on its owning keycode's release. The
        // keycode match means a kLeftClickKey press typed into an editor —
        // which never started a hold — has no effect here. The release is
        // NEVER gated: not by the editor probe (an editor opened mid-hold must
        // not orphan the button) and not by pointer focus (it lands at the
        // last known coordinates, the same convention as the
        // pointer-capability-loss tail). It fires only on the logical 1->0
        // edge, so a concurrent physical hold keeps the button down.
        if (synth_left_held_ && xkb_keycode == synth_left_keycode_) {
            synth_left_held_    = false;
            synth_left_keycode_ = 0;
            if (!pointer_left_held_ && on_button_release_)
                on_button_release_(GuiMouseButton::Left,
                                   pointer_x_, pointer_y_, current_mods());
        }
        // End a synthesized-Ctrl hold on its owning keycode's release. The
        // keycode match means a kCtrlModKey press typed into an editor — which
        // never started a hold — has no effect here. Like the synth-left
        // release the clear is NEVER gated by the editor probe (an editor
        // opened mid-hold must not orphan the modifier); no event fires, the
        // state simply falls to off and the next event carries it.
        if (synth_ctrl_held_ && xkb_keycode == synth_ctrl_keycode_) {
            synth_ctrl_held_    = false;
            synth_ctrl_keycode_ = 0;
        }
        return;
    }

    // Pressed.
    if (!xkb_keymap_) return;
    const xkb_layout_index_t layout =
        xkb_state_serialize_layout(xkb_state_, XKB_STATE_LAYOUT_EFFECTIVE);
    const xkb_keysym_t* syms = nullptr;
    const int nsyms = xkb_keymap_key_get_syms_by_level(
        xkb_keymap_, xkb_keycode, layout, 0, &syms);
    if (nsyms <= 0 || !syms) return;
    const xkb_keysym_t sym = syms[0];
    if (sym == XKB_KEY_NoSymbol) return;

    // Drop keys the GUI never acts on, before delivery and before arming
    // repeat, for two reasons that converge on the same handling:
    //
    //   - Standalone modifier presses (Shift / Control / Alt / Super /
    //     Meta / Hyper, the Caps and Shift locks, the AltGr level-shifts):
    //     modifier STATE arrives separately and completely via
    //     on_keyboard_modifiers, and no consumer matches a modifier keysym
    //     as a GuiKey, so the key event is pure noise.
    //   - Function keys F1..F35: this GUI binds none of them.
    //
    // Ignoring both matters because a text editor treats any key it does
    // not own as "exit the edit". A bare Shift press (the prefix of
    // Shift+Left) would tear down the edit before the arrow arrived, and an
    // F-key press would discard the edit for nothing. Ignoring them is the
    // correct behavior for keys the GUI has no use for.
    if ((sym >= XKB_KEY_F1      && sym <= XKB_KEY_F35) ||      // 0xffbe..0xffe0, function keys
        (sym >= XKB_KEY_Shift_L && sym <= XKB_KEY_Hyper_R) ||  // 0xffe1..0xffee, modifiers
        sym == XKB_KEY_ISO_Level3_Shift ||                     // AltGr
        sym == XKB_KEY_ISO_Level5_Shift ||
        sym == XKB_KEY_Mode_switch) {
        return;
    }

    // Case-fold ASCII uppercase keysyms to lowercase so consumers see a
    // single GuiKey value per physical key regardless of shift state.
    // Other keysyms pass through.
    GuiKey key = static_cast<GuiKey>(sym);
    if (key >= 'A' && key <= 'Z') key |= 0x20;

    // kLeftClickKey emulates BTN_LEFT at this boundary, so downstream it IS
    // the mouse and inherits every mouse gate (read-only tabs, drag gates,
    // prompt/editor modality) for free. The editor probe is consulted at
    // PRESS time ONLY: when a text editor is open the key stays a normal
    // letter and falls through to delivery AND repeat arming below (a held
    // letter repeats in the editor like any key). Otherwise it is the button
    // and is swallowed entirely as a key event — no delivery, no repeat
    // arming (a held button must not machine-gun re-press). pointer_focused_
    // gating means a press with the pointer off the window silently no-ops, as
    // a real BTN_LEFT would not be delivered to this surface either. Any
    // modifier state rides along to the synthesized button, exactly as it
    // would for a physical BTN_LEFT device (see kLeftClickKey's comment).
    if (key == kLeftClickKey &&
        !(text_editor_active_probe_ && text_editor_active_probe_())) {
        if (!synth_left_held_ && pointer_focused_) {
            const bool was_held = pointer_left_held_;   // logical, synth is false
            synth_left_held_    = true;
            synth_left_keycode_ = xkb_keycode;
            if (!was_held && on_button_press_)
                on_button_press_(GuiMouseButton::Left,
                                 pointer_x_, pointer_y_, current_mods());
        }
        return;
    }

    // kCtrlModKey synthesizes Ctrl modifier STATE at this boundary, the
    // modifier sibling of the click key above. The editor probe is consulted
    // at PRESS time ONLY: while a text editor is open the key stays a normal
    // digit and falls through to delivery AND repeat arming below. Otherwise
    // it sets the synthesized-Ctrl hold and is swallowed entirely as a key
    // event — no delivery, no repeat arming (a held modifier must not machine-
    // gun re-press). No event is synthesized on the state change: the next
    // key/pointer/wheel event carries the updated Ctrl via current_mods(), the
    // exact convention on_keyboard_modifiers uses for real modifiers. There is
    // no pointer_focused_ gate — a modifier serves key chords and the wheel,
    // not just pointer events.
    if (key == kCtrlModKey &&
        !(text_editor_active_probe_ && text_editor_active_probe_())) {
        if (!synth_ctrl_held_) {
            synth_ctrl_held_    = true;
            synth_ctrl_keycode_ = xkb_keycode;
        }
        return;
    }

    GuiInputState mods = current_mods();
    mods.codepoint = xkb_state_key_get_utf32(xkb_state_, xkb_keycode);
    deliver_key(key, mods);

    // Arm key repeat (last-key-wins). Keep the press-time codepoint; each
    // repeat fire refreshes live modifier bits so released modifiers stop
    // affecting the repeated chord.
    if (repeat_period_us_ > 0) {
        repeat_key_     = key;
        repeat_keycode_ = xkb_keycode;
        repeat_mods_    = mods;
        repeat_due_us_  = monotonic_us() + repeat_delay_us_;
    } else {
        repeat_key_ = 0;
    }
}

void GuiPlatform::on_keyboard_modifiers(uint32_t /*serial*/,
                                        uint32_t depressed,
                                        uint32_t latched,
                                        uint32_t locked,
                                        uint32_t group) {
    if (!xkb_state_) return;
    xkb_state_update_mask(xkb_state_,
                          depressed, latched, locked,
                          0, 0, group);

    mod_ctrl_  = xkb_state_mod_name_is_active(
        xkb_state_, XKB_MOD_NAME_CTRL,
        XKB_STATE_MODS_EFFECTIVE);
    mod_shift_ = xkb_state_mod_name_is_active(
        xkb_state_, XKB_MOD_NAME_SHIFT,
        XKB_STATE_MODS_EFFECTIVE);
    mod_alt_   = xkb_state_mod_name_is_active(
        xkb_state_, XKB_MOD_NAME_ALT,
        XKB_STATE_MODS_EFFECTIVE);

    // No on_key synthesis on modifier change — the next non-modifier
    // key event carries the updated state.
}

void GuiPlatform::on_keyboard_repeat_info(int32_t rate, int32_t delay) {
    if (rate <= 0) {
        // Compositor opts out of repeat entirely.
        repeat_period_us_ = 0;
        repeat_delay_us_  = 0;
        repeat_key_       = 0;
        return;
    }
    repeat_delay_us_  = static_cast<uint64_t>(delay) * 1000ull;
    repeat_period_us_ = 1'000'000ull / static_cast<uint64_t>(rate);
}

GuiInputState GuiPlatform::current_mods() const {
    GuiInputState s;
    // Logical Ctrl: either the physical Ctrl modifier or the kCtrlModKey
    // synthesized hold, the same OR the left button uses for its two sources.
    s.ctrl  = mod_ctrl_ || synth_ctrl_held_;
    s.shift = mod_shift_;
    s.alt   = mod_alt_;
    // Logical left button: either the physical BTN_LEFT or the kLeftClickKey
    // synthesized hold. Drags consult this bit on motion; without the OR a
    // synthesized-key drag tears on the first motion event.
    s.primary_button_held = pointer_left_held_ || synth_left_held_;
    return s;
}

void GuiPlatform::deliver_key(GuiKey key, GuiInputState mods) {
    if (on_key_) on_key_(key, mods);
}

void GuiPlatform::maybe_fire_repeat() {
    if (repeat_key_ == 0 || repeat_period_us_ == 0) return;
    const uint64_t now = monotonic_us();
    if (now < repeat_due_us_) return;

    // Deliver one synthesized repeat. Then advance repeat_due_us_ by
    // repeat_period_us_. If we missed multiple periods (e.g. the main
    // thread was slow), deliver only one and resync — bursting wouldn't
    // serve the user.
    GuiInputState mods = current_mods();
    mods.codepoint = repeat_mods_.codepoint;
    deliver_key(repeat_key_, mods);
    repeat_due_us_ = now + repeat_period_us_;
}

// ---------------------------------------------------------------------------
// Pointer event handlers
// ---------------------------------------------------------------------------

void GuiPlatform::on_pointer_enter(uint32_t serial,
                                   struct wl_surface* surface,
                                   int32_t surface_x, int32_t surface_y) {
    // Only our own surface should fire this, but a stale enter could
    // arrive after a hypothetical multi-surface setup. Guard.
    if (surface != wl_surface_) return;

    pointer_focused_ = true;
    pointer_x_ = wl_fixed_to_int(surface_x);
    pointer_y_ = wl_fixed_to_int(surface_y);

    // Hand the compositor our cursor surface so the standard arrow appears
    // over our window. wl_pointer.set_cursor with a NULL surface is the
    // protocol's "hide the cursor" request, not "use the default" — there
    // is no protocol-level default-cursor request, so the client must supply
    // an image. If cursor loading failed at init, cursor_surface_ is NULL
    // and this call hides the cursor; that's a degraded but defined state,
    // strictly better than skipping the call (which leaves the cursor
    // appearance undefined per protocol).
    wl_pointer_set_cursor(wl_pointer_, serial, cursor_surface_,
                          cursor_hotspot_x_, cursor_hotspot_y_);

    // Synthesize a motion delivery so consumers register the pointer
    // as present at the entry coordinates. Matches how most clients
    // treat enter — the first "the pointer is here" notification.
    if (on_motion_) on_motion_(pointer_x_, pointer_y_, current_mods());
}

void GuiPlatform::on_pointer_leave(uint32_t /*serial*/,
                                   struct wl_surface* surface) {
    if (surface != wl_surface_) return;
    pointer_focused_ = false;
    // Left-held state persists across leave; the next press/release
    // will resync it. We do NOT clear pointer_left_held_ here because
    // a drag that briefly skids outside the surface and returns
    // should not lose its held state.
}

void GuiPlatform::on_pointer_motion(uint32_t /*time*/,
                                    int32_t surface_x, int32_t surface_y) {
    pointer_x_ = wl_fixed_to_int(surface_x);
    pointer_y_ = wl_fixed_to_int(surface_y);
    if (on_motion_) on_motion_(pointer_x_, pointer_y_, current_mods());
}

void GuiPlatform::on_pointer_button(uint32_t /*serial*/, uint32_t /*time*/,
                                    uint32_t button, uint32_t state) {
    GuiMouseButton mb;
    if (!translate_pointer_button(button, mb)) return;

    const bool pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);

    // Left button rides the logical edge model shared with the kLeftClickKey
    // synthesized hold: deliver a press only on the 0->1 edge of
    // (pointer_left_held_ || synth_left_held_) and a release only on its 1->0
    // edge, so a physical press during a synthesized hold (or vice versa)
    // never double-delivers. Non-left buttons never participate in the OR and
    // are delivered unchanged.
    if (button == BTN_LEFT) {
        const bool was_held = pointer_left_held_ || synth_left_held_;
        pointer_left_held_  = pressed;
        const bool now_held = pointer_left_held_ || synth_left_held_;
        if (now_held == was_held) return;   // no logical edge — swallow
    }

    if (pressed) {
        if (on_button_press_)
            on_button_press_(mb, pointer_x_, pointer_y_, current_mods());
    } else {
        if (on_button_release_)
            on_button_release_(mb, pointer_x_, pointer_y_, current_mods());
    }
}

void GuiPlatform::on_pointer_axis(uint32_t /*time*/,
                                  uint32_t axis, int32_t value) {
    // Live path for touchpad two-finger scroll and any other continuous
    // (non-wheel) source. value120 carries WHEEL scroll only; the
    // compositor sends a touchpad's continuous delta through this legacy
    // wl_pointer.axis event (in its continuous scroll unit, a wl_fixed_t)
    // and sends no value120 for that frame. So this is the touchpad's only
    // path. We stage the delta into the per-frame scratch and do not emit
    // here — on_pointer_frame() arbitrates so exactly one source counts
    // per frame (value120 wins when both arrive) and drains to detents.
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
    frame_axis_accum_ += wl_fixed_to_double(value);
    frame_have_axis_  = true;
}

// One logical scroll detent is 120 value120 units (the high-resolution
// scroll protocol's fixed convention). A mouse-wheel click arrives as a
// single value120 = 120 (or a multiple), so the drain below emits exactly
// one step per detent — identical to the pre-value120 feel. A touchpad
// arrives via the legacy axis event as a stream of small continuous
// deltas; on_pointer_frame() scales those into value120 units with
// kAxisToV120 so they too emit proportionally and smoothly.
namespace {
constexpr double kScrollDetent = 120.0;
// Legacy continuous axis unit -> value120 unit. Touchpad deltas arrive in
// the compositor's continuous scroll unit (historically ~15 units per
// detent on wlroots compositors), so 120/15 = 8 treats ~15 legacy units as
// one detent. This is a feel constant, not a derived one: raise it if
// touchpad scrolling feels too slow, lower it if too fast or jumpy.
constexpr double kAxisToV120 = 120.0 / 15.0;
}

void GuiPlatform::on_pointer_axis_value120(uint32_t axis, int32_t value120) {
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
    // Stage into the per-frame scratch; on_pointer_frame() arbitrates and
    // drains. value120 is the wheel (high-resolution) source.
    frame_v120_accum_ += static_cast<double>(value120);
    frame_have_v120_  = true;
}

void GuiPlatform::on_pointer_frame() {
    // Per-frame arbitration: a wl_pointer.frame may carry value120 (wheel)
    // and/or legacy axis (touchpad) events for the vertical axis. Resolve
    // them to a single delta in value120 units:
    //   - value120 present  -> use it; discard the paired legacy axis delta
    //     (the compositor mirrors wheels onto both streams, so counting
    //     both would double every wheel click).
    //   - else axis present -> continuous source (touchpad); scale the
    //     legacy delta into value120 units with kAxisToV120.
    //   - neither            -> motion-only frame, no scroll.
    double delta120 = 0.0;
    if (frame_have_v120_) {
        delta120 = frame_v120_accum_;
    } else if (frame_have_axis_) {
        delta120 = frame_axis_accum_ * kAxisToV120;
    }

    // Bind the carried remainder to its routing context before growing it.
    // Every unit inside scroll_accum_ must have been contributed under the
    // SAME routing context (modifier chord plus hit region plus not-blocked)
    // that the eventual on_wheel_ emission fires with, so a completed detent
    // can never be assembled from motion the emission-time context did not
    // own. The probe is the application's wheel_context predicate, consulted
    // per raw frame precisely because sub-detent remainder never reaches the
    // application's own completed-detent gate — that gate sees only whole
    // detents, after this attribution has happened. Only frames carrying
    // axis input consult the probe: a motion-only frame (no scroll events)
    // neither re-probes nor clears the remainder, so the remainder is
    // re-validated at exactly the moment new scroll input arrives, the only
    // time it can grow or emit. Cursor drift within one hit region keeps the
    // remainder, matching how a physical wheel behaves under drift — the key
    // is region-granular, not per-pixel.
    //
    // Accepted: a remainder contributed in an accepted context, interrupted
    // by a modal that opens and closes with NO scroll frames in between,
    // deliberately survives — it is consumed under the same context key and
    // no frame arrived to re-probe. The defect this guards against is
    // cross-context attribution, not staleness.
    if (frame_have_v120_ || frame_have_axis_) {
        const int probe = wheel_context_probe_
            ? wheel_context_probe_(pointer_x_, pointer_y_)
            : 0;
        if (probe < 0) {
            // A blocked surface contributes nothing, and any remainder
            // carried into a blocked context dies here.
            scroll_accum_ = 0.0;
        } else {
            const int key = (probe << 3) | (mod_ctrl_ << 2) |
                            (mod_shift_ << 1) | (mod_alt_ ? 1 : 0);
            if (scroll_accum_ != 0.0 && key != scroll_context_key_) {
                scroll_accum_ = 0.0;
            }
            scroll_context_key_ = key;
            scroll_accum_ += delta120;
        }
    }

    // Drain the accumulated delta into a NET signed step count for this
    // frame, carrying the sub-detent remainder forward to the next frame.
    // We tally the detents crossed instead of emitting one wheel event per
    // detent, then fire a single coalesced on_wheel_ carrying the count.
    // The per-step wheel machinery (viewport move, full-width damage, hover
    // hit-test, worker kick) then runs once per frame, not once per detent —
    // which is the whole point: a fast touchpad burst no longer piles that
    // work up between paints. Wheel convention on Wayland: positive value =
    // scroll down (content moves up under the cursor) = WheelDown, negative
    // = WheelUp. The sign of scroll_accum_ cannot flip mid-drain (each
    // iteration moves it toward zero by exactly one detent and stops once
    // below threshold), so every step this frame shares one direction.
    //
    // Guard against a runaway accumulator: a glitch must not back up an
    // unbounded queue of steps. A normal swipe is a few steps per frame;
    // if we somehow exceed the cap, drop the rest of the accumulator so a
    // pathological input cannot tail off into a long burst after the
    // fingers lift.
    constexpr int kMaxStepsPerFrame = 16;
    int steps = 0;
    GuiMouseButton dir = GuiMouseButton::WheelDown;
    while (std::abs(scroll_accum_) >= kScrollDetent) {
        if (steps >= kMaxStepsPerFrame) {
            scroll_accum_ = 0.0;
            break;
        }
        if (scroll_accum_ > 0.0) {
            dir = GuiMouseButton::WheelDown;
            scroll_accum_ -= kScrollDetent;
        } else {
            dir = GuiMouseButton::WheelUp;
            scroll_accum_ += kScrollDetent;
        }
        ++steps;
    }
    if (steps > 0 && on_wheel_) {
        on_wheel_(dir, steps, pointer_x_, pointer_y_, current_mods());
    }

    // Reset the per-frame scratch unconditionally — including motion-only
    // frames where no axis events arrived — so no partial delta leaks into
    // a later frame. scroll_accum_ is the only cross-frame carry.
    frame_v120_accum_ = 0.0;
    frame_axis_accum_ = 0.0;
    frame_have_v120_  = false;
    frame_have_axis_  = false;
}
// ---------------------------------------------------------------------------
// Setters (callbacks)
// ---------------------------------------------------------------------------

void GuiPlatform::set_on_redraw(RedrawCallback cb)              { on_redraw_ = std::move(cb); }
void GuiPlatform::set_on_resize(ResizeCallback cb)              { on_resize_ = std::move(cb); }
void GuiPlatform::set_on_key(KeyCallback cb)                    { on_key_ = std::move(cb); }
void GuiPlatform::set_on_button_press(ButtonCallback cb)        { on_button_press_ = std::move(cb); }
void GuiPlatform::set_on_button_release(ButtonCallback cb)      { on_button_release_ = std::move(cb); }
void GuiPlatform::set_on_wheel(WheelCallback cb)                { on_wheel_ = std::move(cb); }
void GuiPlatform::set_on_motion(MotionCallback cb)              { on_motion_ = std::move(cb); }
void GuiPlatform::set_on_close(CloseCallback cb)                { on_close_ = std::move(cb); }
void GuiPlatform::set_wheel_context_probe(WheelContextProbe cb)    { wheel_context_probe_ = std::move(cb); }
void GuiPlatform::set_text_editor_active_probe(TextEditorProbe cb) { text_editor_active_probe_ = std::move(cb); }
void GuiPlatform::set_on_tick(TickCallback cb)                  { on_tick_ = std::move(cb); }
void GuiPlatform::set_on_pre_paint(PrePaintCallback cb)         { on_pre_paint_ = std::move(cb); }
void GuiPlatform::set_worker_completion_fd(int fd, std::function<void()> on_event) {
    worker_completion_fd_  = fd;
    on_worker_completion_  = std::move(on_event);
}
void GuiPlatform::set_waveform_worker_completion_fd(int fd, std::function<void()> on_event) {
    waveform_worker_completion_fd_  = fd;
    on_waveform_worker_completion_  = std::move(on_event);
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

int GuiPlatform::width()  const { return width_; }
int GuiPlatform::height() const { return height_; }
