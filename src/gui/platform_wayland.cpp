#include "platform_wayland.h"

#include "playhead_cursor_data.h"

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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

// ---------------------------------------------------------------------------
// Run-loop architecture
//
// The run loop is built on a single poll() that waits indefinitely on two
// file descriptors: the wl_display fd, which delivers compositor events,
// and a timerfd whose interval is the detected refresh rate's half-period
// (2x vblank oversample, falling back to 60 Hz if the compositor advertises
// no output mode at registry roundtrip). The poll wakes on whichever fd
// becomes readable first. Compositor events drive every other Wayland-side
// mechanism — surface configure, input event delivery, frame callbacks,
// data offers. The timerfd's wakeups drive the per-tick callback (which is
// a heartbeat: it declares damage at the current playhead position so the
// paint cycle continues, and provides the wake cadence for text-editor
// cursor blink and hover-popup dwell timing).
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
// phase coherence in the phase vocoder. The Laroche-Dolson phase vocoder
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

struct PngMemReader {
    const unsigned char* data;
    unsigned int         len;
    unsigned int         pos;
};

cairo_status_t png_mem_read(void* closure, unsigned char* out, unsigned int n) {
    auto* r = static_cast<PngMemReader*>(closure);
    if (r->pos + n > r->len) return CAIRO_STATUS_READ_ERROR;
    std::memcpy(out, r->data + r->pos, n);
    r->pos += n;
    return CAIRO_STATUS_SUCCESS;
}

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

// URL-decode in-place: %XX becomes one byte. Stops at the first
// malformed escape (preserved literally).
std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(s[i + 1]);
            const int lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 3;
                continue;
            }
        }
        out.push_back(s[i]);
        ++i;
    }
    return out;
}

// Open an anonymous, sealable, in-memory file for the wl_shm pool. memfd
// is preferred (Linux ≥ 3.17). Falls back to shm_open + immediate unlink
// for portability hygiene.
int open_shm_fd(size_t size) {
    int fd = -1;
#ifdef MFD_CLOEXEC
    fd = memfd_create("warptempo-shm", MFD_CLOEXEC);
#endif
    if (fd < 0) {
        char name[64];
        std::snprintf(name, sizeof(name), "/warptempo-shm-%d-%ld",
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
    static void registry_global_remove(void*, struct wl_registry*, uint32_t) {}

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

    // v5+ pointer events. We bind seat at v4 so none of these are
    // dispatched at runtime; the slots are still in the listener struct
    // because wayland-client-protocol.h ships them regardless of bind
    // version. Same abort-on-NULL rule as wl_output. These stubs are
    // forward-compat insurance if the bind version is ever raised.
    static void pointer_frame(void*, struct wl_pointer*) {}
    static void pointer_axis_source(void*, struct wl_pointer*, uint32_t) {}
    static void pointer_axis_stop(void*, struct wl_pointer*,
                                  uint32_t, uint32_t) {}
    static void pointer_axis_discrete(void*, struct wl_pointer*,
                                      uint32_t, int32_t) {}

    // v8+ and v9+ stubs.
    static void pointer_axis_value120(void*, struct wl_pointer*,
                                      uint32_t, int32_t) {}
    static void pointer_axis_relative_direction(void*, struct wl_pointer*,
                                                uint32_t, uint32_t) {}

    // wl_data_device
    static void data_device_data_offer(void* data, struct wl_data_device*,
                                       struct wl_data_offer* offer) {
        static_cast<GuiPlatform*>(data)->on_data_offer(offer);
    }
    static void data_device_enter(void* data, struct wl_data_device*,
                                  uint32_t serial, struct wl_surface* surface,
                                  wl_fixed_t sx, wl_fixed_t sy,
                                  struct wl_data_offer* offer) {
        static_cast<GuiPlatform*>(data)->on_dnd_enter(serial, surface, sx, sy, offer);
    }
    static void data_device_leave(void* data, struct wl_data_device*) {
        static_cast<GuiPlatform*>(data)->on_dnd_leave();
    }
    static void data_device_motion(void* data, struct wl_data_device*,
                                   uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
        static_cast<GuiPlatform*>(data)->on_dnd_motion(time, sx, sy);
    }
    static void data_device_drop(void* data, struct wl_data_device*) {
        static_cast<GuiPlatform*>(data)->on_dnd_drop();
    }
    // wl_data_device.selection (clipboard ownership change). Required
    // listener slot; we ignore clipboard events entirely. NOTE: the
    // offer arg may be NULL.
    static void data_device_selection(void*, struct wl_data_device*,
                                      struct wl_data_offer*) {}

    // wl_data_offer
    static void data_offer_offer(void* data, struct wl_data_offer* offer,
                                 const char* mime_type) {
        static_cast<GuiPlatform*>(data)->on_data_offer_mime_type(offer, mime_type);
    }
    // wl_data_offer.source_actions (v3+). Required listener slot; we
    // don't use the actions API beyond accepting at v3.
    static void data_offer_source_actions(void*, struct wl_data_offer*,
                                          uint32_t) {}
    // wl_data_offer.action (v3+). Required listener slot.
    static void data_offer_action(void*, struct wl_data_offer*,
                                  uint32_t) {}
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

const struct wl_data_device_listener s_data_device_listener = {
    WaylandListeners::data_device_data_offer,
    WaylandListeners::data_device_enter,
    WaylandListeners::data_device_leave,
    WaylandListeners::data_device_motion,
    WaylandListeners::data_device_drop,
    WaylandListeners::data_device_selection,
};

const struct wl_data_offer_listener s_data_offer_listener = {
    WaylandListeners::data_offer_offer,
    WaylandListeners::data_offer_source_actions,
    WaylandListeners::data_offer_action,
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
                     "warptempo_gui: required Wayland globals missing "
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
                     "playback tick will use 60 Hz fallback\n");
    }

    width_  = width;
    height_ = height;

    // Embedded playhead triangle PNG, decoded once. The cairo_t* the GUI
    // hands out for compositing the cursor onto the draw target is built
    // from this surface.
    PngMemReader reader{playhead_cursor_png, playhead_cursor_png_len, 0};
    playhead_triangle_surface_ = cairo_image_surface_create_from_png_stream(
        png_mem_read, &reader);
    const cairo_status_t st = cairo_surface_status(playhead_triangle_surface_);
    if (st != CAIRO_STATUS_SUCCESS) {
        std::fprintf(stderr,
            "warptempo_gui: failed to decode embedded playhead triangle (%s)\n",
            cairo_status_to_string(st));
        return false;
    }

    // Build the surface chain: wl_surface → xdg_surface → xdg_toplevel.
    wl_surface_   = wl_compositor_create_surface(wl_compositor_);
    xdg_surface_  = xdg_wm_base_get_xdg_surface(xdg_wm_base_, wl_surface_);
    xdg_surface_add_listener(xdg_surface_, &s_xdg_surface_listener, this);

    xdg_toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
    xdg_toplevel_add_listener(xdg_toplevel_, &s_toplevel_listener, this);
    xdg_toplevel_set_title(xdg_toplevel_, title ? title : "warptempo");
    xdg_toplevel_set_app_id(xdg_toplevel_, "warptempo");
    xdg_toplevel_set_maximized(xdg_toplevel_);

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

    playback_tick_ms_ = detect_refresh_rate_ms();
    timerfd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timerfd_ < 0) {
        std::fprintf(stderr, "warptempo_gui: timerfd_create failed: %s\n",
                     std::strerror(errno));
        return false;
    }
    {
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
    }

    if (wl_data_device_manager_ && wl_seat_) {
        wl_data_device_ = wl_data_device_manager_get_data_device(
            wl_data_device_manager_, wl_seat_);
        if (wl_data_device_) {
            wl_data_device_add_listener(wl_data_device_,
                                        &s_data_device_listener, this);
        } else {
            std::fprintf(stderr,
                "warptempo_gui: wl_data_device_manager_get_data_device "
                "failed; file drop disabled\n");
        }
    } else {
        std::fprintf(stderr,
            "warptempo_gui: compositor did not advertise "
            "wl_data_device_manager; file drop disabled\n");
    }

    // Initial commit so the compositor delivers the first xdg_surface
    // configure. The configure handler then acks, sets has_initial_configure_,
    // and arms the frame-callback chain.
    wl_surface_commit(wl_surface_);

    return true;
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

    destroy_current_offer();
    if (wl_data_device_) {
        wl_data_device_release(wl_data_device_);
        wl_data_device_ = nullptr;
    }
    if (wl_data_device_manager_) {
        wl_data_device_manager_destroy(wl_data_device_manager_);
        wl_data_device_manager_ = nullptr;
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
        // wl_seat.release is a v5+ request; we bind to v4 max, so use
        // wl_proxy_destroy via the destroy helper.
        wl_seat_destroy(wl_seat_);
        wl_seat_ = nullptr;
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

    if (playhead_triangle_surface_) {
        cairo_surface_destroy(playhead_triangle_surface_);
        playhead_triangle_surface_ = nullptr;
    }

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
    const size_t pool_bytes   = buffer_bytes * 2;

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

    for (int i = 0; i < 2; ++i) {
        const size_t offset = static_cast<size_t>(i) * buffer_bytes;
        shm_buffers_[i].pixels     = static_cast<char*>(shm_pool_map_) + offset;
        shm_buffers_[i].size_bytes = buffer_bytes;
        shm_buffers_[i].busy       = false;

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
    }
}

void GuiPlatform::destroy_shm_pool() {
    for (int i = 0; i < 2; ++i) {
        if (shm_buffers_[i].surface) {
            cairo_surface_destroy(shm_buffers_[i].surface);
            shm_buffers_[i].surface = nullptr;
        }
        if (shm_buffers_[i].buffer) {
            wl_buffer_destroy(shm_buffers_[i].buffer);
            shm_buffers_[i].buffer = nullptr;
        }
        shm_buffers_[i].pixels     = nullptr;
        shm_buffers_[i].size_bytes = 0;
        shm_buffers_[i].busy       = false;
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
            "warptempo_gui: wl_cursor_image_get_buffer returned NULL; "
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
    for (int i = 0; i < 2; ++i) {
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
    for (const DamageRect& d : damage_) {
        cairo_save(cr);
        cairo_rectangle(cr, d.x, d.y, d.w, d.h);
        cairo_clip(cr);
        if (on_redraw_) on_redraw_(cr, d.x, d.y, d.w, d.h);
        cairo_restore(cr);
    }
    cairo_destroy(cr);

    for (const DamageRect& d : damage_) {
        wl_surface_damage_buffer(wl_surface_, d.x, d.y, d.w, d.h);
    }

    wl_surface_attach(wl_surface_, buf->buffer, 0, 0);
    wl_surface_commit(wl_surface_);
    buf->busy = true;

    damage_.clear();

    schedule_frame_callback();
}

void GuiPlatform::invalidate_region(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;

    // Containment suppression: invalidate_region is called from many sites
    // per paint cycle (tick heartbeat, pre-paint hook, input handlers).
    // Each surviving rect costs one on_redraw call downstream, so coalesce
    // here. First, if any existing rect fully contains the new one, drop
    // the new one. Second, after pushing, drop any existing rects fully
    // contained by the new one. Together these maintain the invariant
    // that damage_ never holds a rect that another rect in the list
    // strictly contains.
    auto contains = [](const DamageRect& outer, const DamageRect& inner) {
        return outer.x <= inner.x &&
               outer.y <= inner.y &&
               outer.x + outer.w >= inner.x + inner.w &&
               outer.y + outer.h >= inner.y + inner.h;
    };
    const DamageRect nr{x, y, w, h};
    for (const DamageRect& e : damage_) {
        if (contains(e, nr)) return;
    }
    damage_.push_back(nr);
    damage_.erase(
        std::remove_if(damage_.begin(), damage_.end() - 1,
                       [&](const DamageRect& e) { return contains(nr, e); }),
        damage_.end() - 1);

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
        wl_display_flush(wl_display_);

        // pfds[2] is the async-render completion eventfd. When no renderer
        // is registered (fd == -1), we set events=0 so poll() ignores the
        // slot — same trick used for "watch only when we care."
        struct pollfd pfds[3];
        pfds[0].fd     = wl_display_get_fd(wl_display_);
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        pfds[1].fd     = timerfd_;
        pfds[1].events = POLLIN;
        pfds[1].revents = 0;
        pfds[2].fd     = worker_completion_fd_;
        pfds[2].events = (worker_completion_fd_ >= 0) ? POLLIN : 0;
        pfds[2].revents = 0;

        int n = poll(pfds, 3, -1);

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
    }

    wl_display_dispatch_pending(wl_display_);
}

void GuiPlatform::drain_events() {
    if (wl_display_) wl_display_dispatch_pending(wl_display_);
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
        // scope for this brief; the first output's refresh rate is used as
        // the timerfd interval source.
        if (!wl_output_) {
            const uint32_t v = std::min<uint32_t>(version, 2);
            wl_output_ = static_cast<struct wl_output*>(
                wl_registry_bind(r, name, &wl_output_interface, v));
            wl_output_add_listener(wl_output_, &s_output_listener, this);
        }
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
        // Cap to version 4 — enough for key repeat (added in v4) and
        // matches what's reasonable for the bindings we use.
        const uint32_t v = std::min<uint32_t>(version, 4);
        wl_seat_ = static_cast<struct wl_seat*>(
            wl_registry_bind(r, name, &wl_seat_interface, v));
        wl_seat_add_listener(wl_seat_, &s_seat_listener, this);
    } else if (std::strcmp(interface, wl_data_device_manager_interface.name) == 0) {
        // Require v3 minimum. v3 introduced the actions API and
        // wl_data_offer.finish, both of which this implementation
        // uses. If the compositor only advertises v1 or v2, leave
        // the manager unbound — file drop will be disabled, same
        // fallback as if the manager weren't advertised at all.
        if (version >= 3) {
            wl_data_device_manager_ = static_cast<struct wl_data_device_manager*>(
                wl_registry_bind(r, name, &wl_data_device_manager_interface, 3));
        } else {
            std::fprintf(stderr,
                "warptempo_gui: wl_data_device_manager v%u advertised; "
                "v3+ required for file drop; feature disabled\n", version);
        }
    }
}

void GuiPlatform::on_output_mode(uint32_t flags, int32_t /*width*/,
                                 int32_t /*height*/, int32_t refresh_mhz) {
    // WL_OUTPUT_MODE_CURRENT (bit 0) marks the active mode; among modes
    // reported, take the highest refresh rate (typical: only one CURRENT
    // mode is reported, so this is a single update).
    if ((flags & WL_OUTPUT_MODE_CURRENT) == 0) return;
    if (refresh_mhz > output_refresh_mhz_) output_refresh_mhz_ = refresh_mhz;
}

void GuiPlatform::on_xdg_surface_configure(struct xdg_surface* xs,
                                           uint32_t serial) {
    xdg_surface_ack_configure(xs, serial);

    const bool first = !has_initial_configure_;

    if (first) {
        has_initial_configure_ = true;
        if (pending_w_ > 0 && pending_h_ > 0 &&
            (pending_w_ != width_ || pending_h_ != height_)) {
            width_  = pending_w_;
            height_ = pending_h_;
            recreate_shm_pool(width_, height_);
        }
        damage_.push_back(DamageRect{0, 0, width_, height_});
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
        damage_.push_back(DamageRect{0, 0, width_, height_});
        if (on_resize_) on_resize_(width_, height_);
    } else if (damage_.empty()) {
        // No size change and nothing pending — still schedule a paint so
        // the compositor's reconfigure (e.g. activation/maximize state
        // change) gets honored.
        damage_.push_back(DamageRect{0, 0, width_, height_});
    }

    if (!frame_callback_) schedule_frame_callback();
}

void GuiPlatform::on_toplevel_configure(int32_t width, int32_t height) {
    pending_w_ = width;
    pending_h_ = height;
}

void GuiPlatform::on_toplevel_close() {
    if (on_close_) on_close_();
    should_exit_ = true;
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
        // Drop any in-progress repeat — the keyboard is gone.
        repeat_key_ = 0;
    }

    if (has_pointer && !wl_pointer_) {
        wl_pointer_ = wl_seat_get_pointer(wl_seat_);
        wl_pointer_add_listener(wl_pointer_, &s_pointer_listener, this);
    } else if (!has_pointer && wl_pointer_) {
        wl_pointer_release(wl_pointer_);
        wl_pointer_ = nullptr;
        pointer_focused_   = false;
        pointer_left_held_ = false;
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

    // Case-fold ASCII uppercase keysyms to lowercase so consumers see a
    // single GuiKey value per physical key regardless of shift state.
    // Other keysyms pass through.
    GuiKey key = static_cast<GuiKey>(sym);
    if (key >= 'A' && key <= 'Z') key |= 0x20;

    const GuiInputState mods = current_mods();
    deliver_key(key, mods);

    // Arm key repeat (last-key-wins: this replaces any prior repeating
    // key, even if a different one was held). Skip if repeat_period_us_
    // is zero — that's the "no repeat" advertisement from the compositor.
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
    s.ctrl  = mod_ctrl_;
    s.shift = mod_shift_;
    s.alt   = mod_alt_;
    s.primary_button_held = pointer_left_held_;
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
    deliver_key(repeat_key_, repeat_mods_);
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

    if (button == BTN_LEFT) pointer_left_held_ = pressed;

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
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
    if (value == 0) return;

    // Wheel convention on Wayland: positive value = scroll down
    // (content moves up under the cursor), negative = scroll up.
    // Translate to a discrete WheelUp / WheelDown button event pair.
    // Trackpad smooth-scroll arrives here too but we treat any
    // non-zero vertical axis tick as one discrete step; WarpTempo's
    // wheel bindings (zoom-by-level, pan-by-fraction) are discrete
    // by nature and don't benefit from sub-step resolution.
    const GuiMouseButton mb = (value > 0)
        ? GuiMouseButton::WheelDown
        : GuiMouseButton::WheelUp;

    const GuiInputState mods = current_mods();
    if (on_button_press_)
        on_button_press_(mb, pointer_x_, pointer_y_, mods);
    if (on_button_release_)
        on_button_release_(mb, pointer_x_, pointer_y_, mods);
}

// ---------------------------------------------------------------------------
// Data device (drag-and-drop) handlers
// ---------------------------------------------------------------------------

void GuiPlatform::on_data_offer(struct wl_data_offer* offer) {
    destroy_current_offer();
    current_data_offer_ = offer;
    current_offer_has_uri_list_ = false;
    wl_data_offer_add_listener(offer, &s_data_offer_listener, this);
}

void GuiPlatform::on_data_offer_mime_type(struct wl_data_offer* offer,
                                          const char* mime) {
    if (offer != current_data_offer_) return;
    if (mime && std::strcmp(mime, "text/uri-list") == 0) {
        current_offer_has_uri_list_ = true;
    }
}

void GuiPlatform::on_dnd_enter(uint32_t serial, struct wl_surface* surface,
                               int32_t surface_x, int32_t surface_y,
                               struct wl_data_offer* offer) {
    if (surface != wl_surface_) return;
    if (offer != current_data_offer_) {
        // Drag is referencing an offer we don't know about. Shouldn't
        // happen per protocol but guard.
        return;
    }
    dnd_enter_serial_ = serial;
    dnd_x_ = wl_fixed_to_int(surface_x);
    dnd_y_ = wl_fixed_to_int(surface_y);
    evaluate_drop_accept();
}

void GuiPlatform::on_dnd_leave() {
    destroy_current_offer();
}

void GuiPlatform::on_dnd_motion(uint32_t /*time*/,
                                int32_t surface_x, int32_t surface_y) {
    dnd_x_ = wl_fixed_to_int(surface_x);
    dnd_y_ = wl_fixed_to_int(surface_y);
    evaluate_drop_accept();
}

void GuiPlatform::on_dnd_drop() {
    if (!current_data_offer_) return;

    // If we didn't accept (no uri-list, or predicate said no at the
    // last position), drop without reading. The compositor will
    // cancel the source.
    if (!current_offer_has_uri_list_) {
        destroy_current_offer();
        return;
    }
    if (drop_accept_ && !drop_accept_(dnd_x_, dnd_y_)) {
        destroy_current_offer();
        return;
    }

    // Create a pipe. We give the write end to the compositor (which
    // forwards to the source); we read from the read end until EOF.
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) < 0) {
        std::fprintf(stderr,
            "warptempo_gui: pipe2 for file drop failed: %s\n",
            std::strerror(errno));
        destroy_current_offer();
        return;
    }

    wl_data_offer_receive(current_data_offer_, "text/uri-list", fds[1]);
    close(fds[1]);

    // Flush so the receive request actually goes out before we
    // start reading. Without this, the source never sees the
    // request and our read blocks forever (or until the timeout).
    wl_display_flush(wl_display_);

    const std::string uri_list = read_drop_data(fds[0]);
    close(fds[0]);

    // Per v3 protocol: signal completion to the compositor before
    // destroying the offer.
    wl_data_offer_finish(current_data_offer_);
    destroy_current_offer();

    const std::string path = parse_first_file_uri(uri_list);
    if (!path.empty() && on_file_drop_) {
        on_file_drop_(path);
    }
}

void GuiPlatform::evaluate_drop_accept() {
    if (!current_data_offer_) return;

    const bool predicate_ok = drop_accept_
        ? drop_accept_(dnd_x_, dnd_y_)
        : true;
    const bool offers_uri_list = current_offer_has_uri_list_;

    const char* mime = (predicate_ok && offers_uri_list)
        ? "text/uri-list"
        : nullptr;

    wl_data_offer_accept(current_data_offer_, dnd_enter_serial_, mime);

    // At v3+, also signal supported actions. Copy is the natural
    // semantic for "open this file" — we don't remove or modify the
    // source.
    wl_data_offer_set_actions(current_data_offer_,
        WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
        WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
}

void GuiPlatform::destroy_current_offer() {
    if (current_data_offer_) {
        wl_data_offer_destroy(current_data_offer_);
        current_data_offer_ = nullptr;
    }
    current_offer_has_uri_list_ = false;
    dnd_enter_serial_ = 0;
}

std::string GuiPlatform::read_drop_data(int read_fd) {
    std::string out;
    out.reserve(4096);
    constexpr int timeout_ms = 1000;

    for (;;) {
        struct pollfd pfd{};
        pfd.fd = read_fd;
        pfd.events = POLLIN;
        const int n = poll(&pfd, 1, timeout_ms);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            std::fprintf(stderr,
                "warptempo_gui: file drop read timed out or failed\n");
            break;
        }
        char buf[4096];
        const ssize_t r = read(read_fd, buf, sizeof(buf));
        if (r < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr,
                "warptempo_gui: file drop read failed: %s\n",
                std::strerror(errno));
            break;
        }
        if (r == 0) break;  // EOF
        out.append(buf, static_cast<size_t>(r));
    }
    return out;
}

std::string GuiPlatform::parse_first_file_uri(const std::string& uri_list) {
    // RFC 2483 text/uri-list: CRLF-separated URIs, '#' comment lines
    // skipped. Many implementations send LF only; accept either. Each
    // URI is URL-encoded; we URL-decode and require the file:// scheme
    // with an absolute path. Multi-file drops collapse to first-wins
    // because WarpTempo is single-source-audio; a stderr line announces
    // when the user dropped more than one.
    std::string first_path;
    int valid_count = 0;

    size_t i = 0;
    while (i < uri_list.size()) {
        size_t j = i;
        while (j < uri_list.size() && uri_list[j] != '\n' && uri_list[j] != '\r') ++j;
        std::string line = uri_list.substr(i, j - i);

        if (j < uri_list.size() && uri_list[j] == '\r') ++j;
        if (j < uri_list.size() && uri_list[j] == '\n') ++j;
        i = j;

        if (line.empty() || line[0] == '#') continue;

        const char* prefix = "file://";
        if (line.compare(0, std::strlen(prefix), prefix) != 0) continue;

        std::string path = url_decode(line.substr(std::strlen(prefix)));
        if (path.empty() || path[0] != '/') continue;

        ++valid_count;
        if (first_path.empty()) first_path = path;
    }

    if (valid_count > 1) {
        std::fprintf(stderr,
            "warptempo_gui: multi-file drop, loading first of %d\n",
            valid_count);
    }
    return first_path;
}

// ---------------------------------------------------------------------------
// Setters (callbacks)
// ---------------------------------------------------------------------------

void GuiPlatform::set_on_redraw(RedrawCallback cb)              { on_redraw_ = std::move(cb); }
void GuiPlatform::set_on_resize(ResizeCallback cb)              { on_resize_ = std::move(cb); }
void GuiPlatform::set_on_key(KeyCallback cb)                    { on_key_ = std::move(cb); }
void GuiPlatform::set_on_button_press(ButtonCallback cb)        { on_button_press_ = std::move(cb); }
void GuiPlatform::set_on_button_release(ButtonCallback cb)      { on_button_release_ = std::move(cb); }
void GuiPlatform::set_on_motion(MotionCallback cb)              { on_motion_ = std::move(cb); }
void GuiPlatform::set_on_close(CloseCallback cb)                { on_close_ = std::move(cb); }
void GuiPlatform::set_on_file_drop(FileDropCallback cb)         { on_file_drop_ = std::move(cb); }
void GuiPlatform::set_drop_accept_predicate(DropAcceptPredicate p) { drop_accept_ = std::move(p); }
void GuiPlatform::set_on_tick(TickCallback cb)                  { on_tick_ = std::move(cb); }
void GuiPlatform::set_on_pre_paint(PrePaintCallback cb)         { on_pre_paint_ = std::move(cb); }
void GuiPlatform::set_worker_completion_fd(int fd, std::function<void()> on_event) {
    worker_completion_fd_  = fd;
    on_worker_completion_  = std::move(on_event);
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

int GuiPlatform::width()  const { return width_; }
int GuiPlatform::height() const { return height_; }
int GuiPlatform::playback_tick_ms() const { return playback_tick_ms_; }
cairo_surface_t* GuiPlatform::playhead_triangle_surface() const {
    return playhead_triangle_surface_;
}
