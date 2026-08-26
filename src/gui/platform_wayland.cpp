#include "platform_wayland.h"

#include "render.h"   // kMinWindowWidthPx / kMinWindowHeightPx

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xdg-shell-client-protocol.h>
#include <xdg-decoration-unstable-v1-client-protocol.h>
#include <pointer-constraints-unstable-v1-client-protocol.h>
#include <relative-pointer-unstable-v1-client-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <fcntl.h>
#include <strings.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>

// ---------------------------------------------------------------------------
// Run-loop architecture
//
// The run loop is built on a single poll() that waits indefinitely on the
// wl_display fd, the idle/playback timerfd, and the two optional renderer
// completion eventfds. The timer interval tracks the bound single output's
// current refresh half-period (2x vblank oversample), falling back to 60 Hz
// while no output mode is known. The poll wakes on whichever fd becomes
// readable first. Compositor events drive surface configure, input delivery,
// frame callbacks, and clipboard selection bookkeeping; renderer eventfds
// deliver async results. The clipboard's actual byte transfers are NOT poll-set
// members: both directions are short synchronous pipe operations made from
// inside their own handlers (the read is deadline-bounded, the write services a
// consumer that asked for it), which is what a keyboard-triggered copy or paste
// of a short string can afford. No drag-and-drop path exists.
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

// Open an anonymous, in-memory file for the wl_shm pool. memfd IS the path
// (Linux >= 3.17; the target is current Arch). The pre-3.17 shm_open + unlink
// fallback is DELETED (architect 2026-07-30): it was unreachable on any kernel
// this program runs on, and on a genuine memfd_create failure it bought nothing
// — the failure is rare and LOUD (recreate_shm_pool prints the errno and leaves
// the pool unmapped, so nothing is silently wrong and the user relaunches),
// which is exactly the class of fault that gets no recovery code.
int open_shm_fd(size_t size) {
    int fd = memfd_create("warptempo_gui-shm", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// THE ACCEPTED CLIPBOARD TEXT MIMES, ranked. text/plain;charset=utf-8 is the
// preferred token and bare text/plain the fallback; anything else is not text
// we will read. The comparison is case-insensitive because the media type and
// the charset parameter both are (RFC 2045), and real applications disagree on
// the spelling — GTK offers `;charset=utf-8`, several Qt and Java toolkits
// offer `;charset=UTF-8`, and a paste must not miss on the difference. Zero
// means "not an acceptable text mime", which the empty mime slot also scores.
int text_mime_rank(const char* mime) {
    if (strcasecmp(mime, "text/plain;charset=utf-8") == 0) return 2;
    if (strcasecmp(mime, "text/plain") == 0)               return 1;
    return 0;
}

// Latch `mime` into an offer's remembered text mime when it outranks whatever
// is already there. The EXACT offered spelling is what gets stored, because
// that is the token wl_data_offer.receive must be handed back.
void note_offer_text_mime(std::string& slot, const char* mime) {
    if (text_mime_rank(mime) > text_mime_rank(slot.c_str())) slot = mime;
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
                                   struct wl_array* states) {
        static_cast<GuiPlatform*>(data)->on_toplevel_configure(width, height,
                                                               states);
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
    // The events with nothing to decode reach the input core straight from
    // here; the ones carrying protocol units or a surface identity go through a
    // GuiPlatform member that decodes first (the split is stated at the touch
    // handler declarations, platform_wayland.h).
    static void pointer_frame(void* data, struct wl_pointer*) {
        static_cast<GuiPlatform*>(data)->input_.pointer_frame();
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

    // wl_touch (touch phase 1: one finger is the pointer, two are the
    // navigation gesture — the machine lives in the on_touch_* members).
    // shape and orientation are v6+ slots; the seat binds at v8, so they can
    // be dispatched and get do-nothing stubs (the abort-on-NULL rule). The
    // panel's contact geometry is nothing this program reads — a touch is a
    // point here, exactly as a pointer is.
    static void touch_down(void* data, struct wl_touch*, uint32_t serial,
                           uint32_t time, struct wl_surface* /*surface*/,
                           int32_t id, wl_fixed_t x, wl_fixed_t y) {
        static_cast<GuiPlatform*>(data)->on_touch_down(serial, time, id, x, y);
    }
    static void touch_up(void* data, struct wl_touch*, uint32_t /*serial*/,
                         uint32_t /*time*/, int32_t id) {
        static_cast<GuiPlatform*>(data)->input_.touch_up(id);
    }
    static void touch_motion(void* data, struct wl_touch*, uint32_t time,
                             int32_t id, wl_fixed_t x, wl_fixed_t y) {
        static_cast<GuiPlatform*>(data)->on_touch_motion(time, id, x, y);
    }
    static void touch_frame(void* data, struct wl_touch*) {
        static_cast<GuiPlatform*>(data)->input_.touch_frame();
    }
    static void touch_cancel(void* data, struct wl_touch*) {
        static_cast<GuiPlatform*>(data)->input_.touch_cancel();
    }
    static void touch_shape(void*, struct wl_touch*, int32_t,
                            wl_fixed_t, wl_fixed_t) {}
    static void touch_orientation(void*, struct wl_touch*, int32_t,
                                  wl_fixed_t) {}

    // zwp_relative_pointer_v1
    static void relative_motion(void* data, struct zwp_relative_pointer_v1*,
                                uint32_t /*utime_hi*/, uint32_t /*utime_lo*/,
                                wl_fixed_t dx, wl_fixed_t dy,
                                wl_fixed_t /*dx_unaccel*/,
                                wl_fixed_t /*dy_unaccel*/) {
        // Feed the ACCELERATED delta (dx/dy, not the unaccelerated pair) so
        // captured travel matches the normal pointer feel of the uncaptured
        // gesture.
        static_cast<GuiPlatform*>(data)->input_.relative_motion(
            wl_fixed_to_double(dx), wl_fixed_to_double(dy));
    }

    // zwp_locked_pointer_v1
    static void locked_pointer_locked(void* data, struct zwp_locked_pointer_v1*) {
        static_cast<GuiPlatform*>(data)->on_locked_pointer_locked();
    }
    static void locked_pointer_unlocked(void* data,
                                        struct zwp_locked_pointer_v1*) {
        static_cast<GuiPlatform*>(data)->on_locked_pointer_unlocked();
    }

    // wl_data_device. Only data_offer and selection do anything: the four
    // drag-and-drop slots are INERT and stay that way — DnD was retired with
    // the in-session file load and is not coming back. They exist because a
    // listener struct with a NULL slot is a latent abort, not because a drag is
    // being tracked. A DnD offer therefore parks unclaimed in the pending slot
    // and the next announcement destroys it.
    static void data_device_data_offer(void* data, struct wl_data_device*,
                                       struct wl_data_offer* offer) {
        static_cast<GuiPlatform*>(data)->on_data_offer(offer);
    }
    static void data_device_enter(void*, struct wl_data_device*, uint32_t,
                                  struct wl_surface*, wl_fixed_t, wl_fixed_t,
                                  struct wl_data_offer*) {}
    static void data_device_leave(void*, struct wl_data_device*) {}
    static void data_device_motion(void*, struct wl_data_device*, uint32_t,
                                   wl_fixed_t, wl_fixed_t) {}
    static void data_device_drop(void*, struct wl_data_device*) {}
    // The offer argument is the NEW clipboard selection offer, or NULL when the
    // selection was cleared.
    static void data_device_selection(void* data, struct wl_data_device*,
                                      struct wl_data_offer* offer) {
        static_cast<GuiPlatform*>(data)->on_selection(offer);
    }

    // wl_data_offer. source_actions and action are v3 slots belonging to the
    // drag-and-drop actions API, which the clipboard never uses.
    static void data_offer_offer(void* data, struct wl_data_offer* offer,
                                 const char* mime_type) {
        static_cast<GuiPlatform*>(data)->on_data_offer_mime_type(offer, mime_type);
    }
    static void data_offer_source_actions(void*, struct wl_data_offer*,
                                          uint32_t) {}
    static void data_offer_action(void*, struct wl_data_offer*, uint32_t) {}

    // wl_data_source (the clipboard payload we own). Only send (a consumer is
    // reading our payload) and cancelled (another client took the selection)
    // do anything; target and the three dnd_* slots are drag-and-drop.
    static void data_source_target(void*, struct wl_data_source*, const char*) {}
    static void data_source_send(void* data, struct wl_data_source* src,
                                 const char* mime, int32_t fd) {
        static_cast<GuiPlatform*>(data)->on_data_source_send(src, mime, fd);
    }
    static void data_source_cancelled(void* data, struct wl_data_source* src) {
        static_cast<GuiPlatform*>(data)->on_data_source_cancelled(src);
    }
    static void data_source_dnd_drop_performed(void*, struct wl_data_source*) {}
    static void data_source_dnd_finished(void*, struct wl_data_source*) {}
    static void data_source_action(void*, struct wl_data_source*, uint32_t) {}
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

const struct wl_touch_listener s_touch_listener = {
    WaylandListeners::touch_down,
    WaylandListeners::touch_up,
    WaylandListeners::touch_motion,
    WaylandListeners::touch_frame,
    WaylandListeners::touch_cancel,
    WaylandListeners::touch_shape,
    WaylandListeners::touch_orientation,
};

const struct zwp_relative_pointer_v1_listener s_relative_pointer_listener = {
    WaylandListeners::relative_motion,
};

const struct zwp_locked_pointer_v1_listener s_locked_pointer_listener = {
    WaylandListeners::locked_pointer_locked,
    WaylandListeners::locked_pointer_unlocked,
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

const struct wl_data_source_listener s_data_source_listener = {
    WaylandListeners::data_source_target,
    WaylandListeners::data_source_send,
    WaylandListeners::data_source_cancelled,
    WaylandListeners::data_source_dnd_drop_performed,
    WaylandListeners::data_source_dnd_finished,
    WaylandListeners::data_source_action,
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

    // THE REQUIRED GLOBALS. zxdg_decoration_manager_v1 JOINED THEM 2026-07-30
    // and wl_data_device_manager 2026-08-02 (architect, both times), leaving the
    // ruled OPTIONAL list at exactly two (pointer-constraints and
    // relative-pointer, whose absence has a defined degraded behavior — see the
    // pointer-capture warning below). labwc always advertises the decoration
    // manager, and the data device manager is CORE protocol — the same
    // wayland.xml as wl_seat, so a compositor without it is not a compositor —
    // which makes either absence a broken environment rather than a degraded
    // one, and the program's answer to a broken environment is to fail at
    // startup rather than run undecorated or with a dead clipboard.
    if (!wl_compositor_ || !wl_shm_ || !xdg_wm_base_ ||
        !xdg_decoration_manager_ || !wl_data_device_manager_) {
        std::fprintf(stderr,
                     "warptempo_gui: Required wayland globals missing "
                     "(wl_compositor=%p wl_shm=%p xdg_wm_base=%p "
                     "zxdg_decoration_manager_v1=%p "
                     "wl_data_device_manager=%p)\n",
                     (void*)wl_compositor_, (void*)wl_shm_, (void*)xdg_wm_base_,
                     (void*)xdg_decoration_manager_,
                     (void*)wl_data_device_manager_);
        return false;
    }
    if (!wl_output_) {
        std::fprintf(stderr,
                     "warptempo_gui: No wl_output advertised; "
                     "playback tick will use 60 Hz fallback\n");
    }
    if (!pointer_constraints_ || !relative_pointer_manager_) {
        std::fprintf(stderr,
                     "warptempo_gui: Pointer capture unavailable "
                     "(zwp_pointer_constraints_v1=%p "
                     "zwp_relative_pointer_manager_v1=%p); strip drags run "
                     "without cursor lock\n",
                     (void*)pointer_constraints_,
                     (void*)relative_pointer_manager_);
    }

    // The seat's capabilities event may have created wl_pointer_ during the
    // roundtrips above; if the relative-pointer manager was bound in the same
    // burst, create the relative pointer now (idempotent, no-op if already
    // created by on_seat_capabilities or if either half is absent).
    create_relative_pointer_if_ready();

    width_  = width;
    height_ = height;
    input_.set_surface_width(width_);

    // THE CORE'S CODEPOINT REFILL, the one probe pointing DOWNWARD across the
    // seam: a synthesized key repeat must resolve the held key afresh under the
    // live keyboard state rather than reuse the press's codepoint, and the
    // keymap is this side's (contract at GuiInputCore::set_codepoint_probe).
    // Answering 0 with no keymap is the same "no character" a function key
    // already means.
    input_.set_codepoint_probe([this](uint32_t stable_code) -> uint32_t {
        return xkb_state_ ? xkb_state_key_get_utf32(xkb_state_, stable_code)
                          : 0u;
    });

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

    // Unconditional: the decoration manager is a REQUIRED global (the gate
    // above returns false without it), so there is no undecorated arm.
    xdg_toplevel_decoration_ = zxdg_decoration_manager_v1_get_toplevel_decoration(
        xdg_decoration_manager_, xdg_toplevel_);
    zxdg_toplevel_decoration_v1_set_mode(
        xdg_toplevel_decoration_,
        ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);

    recreate_shm_pool(width_, height_);

    // Cursor theme load is best-effort: failure leaves every kind's surface
    // NULL, and apply_cursor_kind then passes NULL to wl_pointer.set_cursor
    // (which hides the cursor over our window). That degraded state is
    // acceptable — the GUI is still fully usable. It loads the WHOLE SET here,
    // once, so no cue is resolved on a pointer event.
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

namespace {

// THE TITLE'S UTF-8 BOUNDARY. xdg_toplevel.set_title's argument type is
// `string`, which the Wayland protocol DEFINES as UTF-8 — sending bytes that
// are not well-formed is a protocol violation, and a compositor is entitled to
// disconnect the client over one. The composed title carries a FILESYSTEM
// basename (a folder name is an arbitrary byte string on Linux, NUL and '/'
// excluded), so the malformed case has a real producer: a directory spelled in
// Latin-1, or in any other non-UTF-8 encoding, would reach this call.
//
// The acceptance shape is the product's ONE incoming-text vocabulary, whose
// rules and rationale are owned by `text_editor::replace_selection`
// (text_editor.cpp): printable ASCII and well-formed multi-byte UTF-8 pass
// verbatim; ASCII control bytes and DEL drop, and so does every malformed
// sequence — a lone continuation byte, an 0xf8..0xff byte, a truncated tail, an
// overlong form, a surrogate or an out-of-range value. Recovery is the same
// too: drop that ONE byte and re-scan from the next, so a single bad byte never
// eats the good text after it. This is a SECOND SPELLING of those rules rather
// than a call into that owner, deliberately: platform_wayland.cpp sits below
// the GUI model and includes none of it, and a protocol boundary that cannot
// send its bytes is not the editors' business. It judges nothing else — the
// stored project name stays the folder's verbatim bytes everywhere else.
std::string title_bytes_to_utf8(const std::string& raw) {
    std::string  clean;
    clean.reserve(raw.size());
    const size_t n = raw.size();
    size_t       i = 0;
    while (i < n) {
        const unsigned char b0 = static_cast<unsigned char>(raw[i]);
        if (b0 < 0x80) {                       // ASCII: printable only
            if (b0 >= 0x20 && b0 != 0x7f) clean.push_back(static_cast<char>(b0));
            ++i;
            continue;
        }
        size_t   len = 0;
        unsigned cp  = 0;
        if      ((b0 & 0xe0) == 0xc0) { len = 2; cp = b0 & 0x1fu; }
        else if ((b0 & 0xf0) == 0xe0) { len = 3; cp = b0 & 0x0fu; }
        else if ((b0 & 0xf8) == 0xf0) { len = 4; cp = b0 & 0x07u; }
        else                          { ++i; continue; }
        if (i + len > n) { ++i; continue; }    // truncated at the string's end
        bool ok = true;
        for (size_t k = 1; k < len; ++k) {
            const unsigned char bk = static_cast<unsigned char>(raw[i + k]);
            if ((bk & 0xc0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (bk & 0x3fu);
        }
        if (!ok) { ++i; continue; }
        static constexpr unsigned kShortest[5] = {0, 0, 0x80, 0x800, 0x10000};
        if (cp < kShortest[len] || cp > 0x10ffffu ||
            (cp >= 0xd800u && cp <= 0xdfffu)) {
            ++i;
            continue;
        }
        clean.append(raw, i, len);
        i += len;
    }
    return clean;
}

}  // namespace

void GuiPlatform::set_title(const std::string& title) {
    if (!xdg_toplevel_) return;
    // Every title the product sends passes the UTF-8 boundary above — this is
    // the one xdg_toplevel.set_title call in the tree, so sanitizing here
    // covers the composed title and init()'s bare-name seed alike.
    const std::string safe = title_bytes_to_utf8(title);
    xdg_toplevel_set_title(xdg_toplevel_, safe.c_str());
}

// THE TITLE'S ONE COMPOSITION SITE (architect 2026-08-01, second pass): the
// CLASSIC APPLICATION FORM — "K551 - warptempo_gui" clean, "K551 * - warptempo_gui"
// with unsaved work. The dirty mark is an ASTERISK PLUS ONE SPACE inserted before
// the separator, and it is present only while dirty; the U+25CF dot the title
// shipped with a few hours earlier is retired (the asterisk is the convention
// every editor uses, and it is plain ASCII). The mark lives here and nowhere
// else; the bottom strip's old dirty cell is gone.
//
// The project name inside the string is a FILESYSTEM folder name taken
// verbatim, so it carries whatever bytes that folder is spelled with; only the
// fixed parts this site composes — the separator, the binary name, the asterisk
// — are the product's own, and those are ASCII. Composition keeps those bytes
// whole; set_title above is where they meet the protocol's UTF-8 requirement
// and drops anything malformed (the rules are at title_bytes_to_utf8). A
// well-formed name therefore reaches the compositor verbatim, which is the
// ordinary case. What the titlebar then LOOKS like is not ours either way:
// labwc shapes it with its own font stack, so this string never touches
// text_shape and the product's one-face rule does not apply to it.
//
// project_title_ is empty until the load derives it, so the pre-load frames
// (and the loading line) keep the bare binary name init() seeded: with no
// project there is no name to separate from, and no unsaved work to mark.
void GuiPlatform::apply_window_title() {
    if (project_title_.empty()) {
        set_title("warptempo_gui");
        return;
    }
    std::string t = project_title_;
    t += title_dirty_ ? " * - warptempo_gui" : " - warptempo_gui";
    set_title(t);
}

void GuiPlatform::set_project_title(std::string project_name) {
    project_title_ = std::move(project_name);
    apply_window_title();
}

void GuiPlatform::set_title_dirty(bool dirty) {
    // Cheap-no-op on an unchanged flag: recompute_dirty runs after every
    // command, and re-sending an identical title on each one would be pure
    // protocol traffic.
    if (dirty == title_dirty_) return;
    title_dirty_ = dirty;
    apply_window_title();
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

    // The clipboard's device, offers and source go before the wl_seat and
    // wl_keyboard they were created against. The manager is not seat-bound, so
    // it is destroyed here rather than inside the shared teardown.
    destroy_data_device_state();
    if (wl_data_device_manager_) {
        wl_data_device_manager_destroy(wl_data_device_manager_);
        wl_data_device_manager_ = nullptr;
    }

    if (wl_keyboard_) {
        wl_keyboard_release(wl_keyboard_);
        wl_keyboard_ = nullptr;
    }
    // Release any live pointer lock and the relative pointer before the
    // wl_pointer they depend on. No cursor restore at shutdown.
    if (locked_pointer_) {
        zwp_locked_pointer_v1_destroy(locked_pointer_);
        locked_pointer_ = nullptr;
    }
    input_.end_capture();
    destroy_relative_pointer();
    if (wl_pointer_) {
        wl_pointer_release(wl_pointer_);
        wl_pointer_ = nullptr;
    }
    // Shutdown is not an input edge: the touch proxy is released with no
    // deliveries, exactly as the keyboard and pointer teardowns above deliver
    // nothing (the edge inventory at the touch state block records this).
    if (wl_touch_) {
        wl_touch_release(wl_touch_);
        wl_touch_ = nullptr;
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

    // Cursor: destroy EVERY kind's surface before the theme. The surfaces hold
    // buffers owned by the theme, and freeing in dependency order avoids any
    // compositor surprise even though libwayland-cursor's buffers can
    // technically outlive the surface. The wl_cursor objects themselves are the
    // theme's and are never held past the load.
    for (ThemeCursor& tc : cursors_) {
        if (!tc.surface) continue;
        wl_surface_destroy(tc.surface);
        tc.surface = nullptr;
    }
    if (wl_cursor_theme_) {
        wl_cursor_theme_destroy(wl_cursor_theme_);
        wl_cursor_theme_ = nullptr;
    }

    destroy_shm_pool();

    if (pointer_constraints_) {
        zwp_pointer_constraints_v1_destroy(pointer_constraints_);
        pointer_constraints_ = nullptr;
    }
    if (relative_pointer_manager_) {
        zwp_relative_pointer_manager_v1_destroy(relative_pointer_manager_);
        relative_pointer_manager_ = nullptr;
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
        std::fprintf(stderr, "warptempo_gui: Failed to open shm fd: %s\n",
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

namespace {

// THE CURSOR SIZE AXIS, and it is the THEME's, not gui_scale's. gui_scale is
// the product's one scale axis for everything the WINDOW paints; a cursor is not
// painted by the window — it is a surface the compositor composites at pointer
// scale, and the user's pointer size is a desktop-wide setting the cursor theme
// already answers. Every kind is loaded at this one size, which is what keeps
// the pointer from resizing as it crosses a zone boundary.
//
// XCURSOR_SIZE if the user has set it; otherwise 24, the freedesktop fallback.
int cursor_theme_size() {
    int size = 24;
    if (const char* env = std::getenv("XCURSOR_SIZE")) {
        int parsed = std::atoi(env);
        if (parsed > 0) size = parsed;
    }
    return size;
}

int cursor_kind_index(GuiCursorKind kind) {
    return static_cast<int>(kind);
}

// THE KIND -> XCURSOR NAME TABLE, and the whole of what the product knows about
// cursor art: SEVEN KINDS over eight standard freedesktop names, all present
// in Breeze and in Adwaita. (`crosshair` LEFT THE TABLE with the Scrub kind,
// 2026-08-13 — the waveform's two halves became one surface and the lower
// half's audition became a click act, which carries no cue; the enum's own
// comment holds the ruling.
// `text` JOINED IT the same day with the Text kind, the editors' I-beam.)
//
// THE HOTSPOT IS THE FILE'S, NEVER A CENTRE WE COMPUTE, and the installed theme
// is what settles it. What load_theme_cursor reads is wl_cursor_image's INTEGER
// xhot/yhot, from the XCursor binary, at whatever the theme load resolved
// (cursor_theme_size above: XCURSOR_SIZE, else 24) — and Breeze_Light serves
// that default request from its 32x32 images, whose declared hotspots are:
//
//     left_ptr 4,4   grab 16,16   zoom-in 15,15
//     ew-resize 16,15   left_side 4,15   right_side 27,15   text 16,15
//
// The POINTER-ISH shapes are centred (grab exactly, zoom-in within a pixel of
// it, the I-beam's 16,15 on its own waist) while left_side and right_side sit
// hard against their OWN edge
// — 4,15 and 27,15, which is the whole point of an edge cue — and left_ptr sits
// at its tip. No single rule we could compute produces all three, which is
// exactly why the file's declaration is taken verbatim. The numbers scale with
// the resolved size, so they are the default load's, not constants.
//
// ARROW IS FIRST, and its position matters: it is the fallback every other kind
// degrades to, and the only one whose absence is reported as a broken theme.
//
// THERE IS NO "grabbing" CURSOR AND THAT IS DELIBERATE (architect): the grab-pan
// and the strip drag both take a POINTER CAPTURE, and the capture HIDES the
// cursor — that hide is what buys unlimited travel, and the strip drag paints an
// anchor stem to show where the gesture is. A pressed-state cursor was
// considered and refused: it would flash for the frame before the hide and then
// be invisible for the whole gesture. The hidden-during-capture behaviour is
// unchanged by any of this.
//
// `alt_name` is the ONE row-level option in this table: a second spelling to
// try before the per-kind degrade, for a shape the freedesktop world names two
// ways. Only the I-beam carries one (`text`, then the older `xterm` — Breeze
// ships the second as a symlink to the first, and a theme carrying only the
// legacy name still gets its cue). Null everywhere else: a kind with one
// conventional name gets one lookup.
struct CursorKindName {
    GuiCursorKind kind;
    const char*   name;
    const char*   alt_name = nullptr;
};
//
// left_side / right_side ARE THE BOUNDARY-EXTENSION SHAPES — the arrow-with-a-bar
// a window manager shows on a window's left or right edge — and they are what
// distinguishes moving ONE trim bound from the bridge's ew-resize, which moves
// both. Each is the cue for its own bound, so the begin cap (and the ctrl click
// that sets begin) takes left_side and the end cap (and ctrl+shift) right_side.
constexpr CursorKindName kCursorKindNames[] = {
    {GuiCursorKind::Arrow,          "left_ptr"},
    {GuiCursorKind::Pan,            "grab"},
    {GuiCursorKind::Zoom,           "zoom-in"},
    {GuiCursorKind::TrimResize,     "ew-resize"},
    {GuiCursorKind::TrimBoundBegin, "left_side"},
    {GuiCursorKind::TrimBoundEnd,   "right_side"},
    {GuiCursorKind::Text,           "text", "xterm"},
};
static_assert(static_cast<int>(std::size(kCursorKindNames)) ==
                  kGuiCursorKindCount,
              "Every GuiCursorKind needs exactly one row in this table");

}  // namespace

bool GuiPlatform::load_theme_cursor(GuiCursorKind kind,
                                    const char* xcursor_name) {
    struct wl_cursor* cur =
        wl_cursor_theme_get_cursor(wl_cursor_theme_, xcursor_name);
    if (!cur || cur->image_count == 0) return false;

    // First frame only; animated cursors (rare) collapse to frame 0.
    struct wl_cursor_image* image = cur->images[0];
    struct wl_buffer* buf = wl_cursor_image_get_buffer(image);
    if (!buf) return false;

    // One surface per kind, created once for the process lifetime. The buffer
    // attachment is STICKY and this surface never switches images, which is what
    // lets set_cursor swap kinds by naming a different surface with no image
    // work per swap.
    struct wl_surface* surface = wl_compositor_create_surface(wl_compositor_);
    if (!surface) return false;
    wl_surface_attach(surface, buf, 0, 0);
    wl_surface_damage(surface, 0, 0, image->width, image->height);
    wl_surface_commit(surface);

    ThemeCursor& tc = cursors_[cursor_kind_index(kind)];
    tc.surface   = surface;
    tc.hotspot_x = static_cast<int32_t>(image->hotspot_x);
    tc.hotspot_y = static_cast<int32_t>(image->hotspot_y);
    return true;
}

bool GuiPlatform::load_cursor_theme() {
    const int size = cursor_theme_size();

    // Theme name NULL = "system default" per libwayland-cursor.
    wl_cursor_theme_ = wl_cursor_theme_load(nullptr, size, wl_shm_);
    if (!wl_cursor_theme_) {
        std::fprintf(stderr,
            "warptempo_gui: wl_cursor_theme_load failed; "
            "pointer will not display a cursor image\n");
        return false;
    }

    for (const CursorKindName& row : kCursorKindNames) {
        if (load_theme_cursor(row.kind, row.name)) continue;
        // THE SECOND SPELLING, where the row carries one: a missing primary
        // name is not yet a missing cue if the shape has an older conventional
        // name (the I-beam's `xterm`). Tried in the row's own order, and a hit
        // reports nothing — the kind has its cursor.
        if (row.alt_name && load_theme_cursor(row.kind, row.alt_name)) continue;
        // left_ptr is the freedesktop standard arrow name. If the active theme
        // is missing it, the theme is broken; report and move on without a
        // cursor rather than guess at an alternative.
        if (row.kind == GuiCursorKind::Arrow) {
            std::fprintf(stderr,
                "warptempo_gui: Cursor theme has no \"%s\"; "
                "pointer will not display a cursor image\n", row.name);
            return false;
        }
        // A MISSING NAME DEGRADES TO THE ARROW, per kind — the theme-load
        // failure's own shape, one stderr line and nothing stops working. A
        // theme without these names is a poor environment, not a broken one:
        // the zone loses its cue and every gesture in it still runs. The line
        // names every spelling that was tried, so a row with a second name
        // reports both rather than blaming its primary alone.
        if (row.alt_name) {
            std::fprintf(stderr,
                "warptempo_gui: Cursor theme has no \"%s\" or \"%s\"; "
                "that pointer cue falls back to the arrow\n",
                row.name, row.alt_name);
        } else {
            std::fprintf(stderr,
                "warptempo_gui: Cursor theme has no \"%s\"; "
                "that pointer cue falls back to the arrow\n", row.name);
        }
    }
    return true;
}

void GuiPlatform::apply_cursor_kind() {
    if (!wl_pointer_) return;
    // A LIVE CAPTURE OWNS THE CURSOR AND IT IS HIDDEN. Every applier passes
    // through here, so this one guard is what makes "the setter must not
    // un-hide" true for the GUI's per-iteration calls AND for a pointer enter that
    // arrives mid-capture. The capture's own release re-applies afterwards, at
    // which point the core's captured bit is already false.
    if (input_.pointer_captured()) return;

    // THE PER-KIND FALLBACK, and the one place it lives: a kind whose xcursor
    // name the theme did not carry has a null surface, and it shows the ARROW
    // instead. That is what makes a missing name cost the cue and nothing else.
    const ThemeCursor& want = cursors_[cursor_kind_index(input_.cursor_kind())];
    const ThemeCursor& use =
        want.surface ? want : cursors_[cursor_kind_index(GuiCursorKind::Arrow)];
    // A NULL surface here is the protocol's "hide the cursor" request, not
    // "use the default" — there is no protocol-level default-cursor request, so
    // a client must supply an image. That is the ARROW's own degraded state when
    // no theme loaded at all, and it is strictly better than skipping the call
    // (which leaves the cursor appearance undefined per protocol). No other kind
    // reaches it: they all fall back to the arrow above.
    wl_pointer_set_cursor(wl_pointer_, pointer_enter_serial_, use.surface,
                          use.hotspot_x, use.hotspot_y);
}

void GuiPlatform::set_cursor_kind(GuiCursorKind kind) {
    // A KIND DERIVED FOR A POSITION THE POINTER DOES NOT OCCUPY IS DROPPED — not
    // recorded, so the remembered kind stays what the capture's own begin STAMPED
    // there (the gesture's cue, see begin_pointer_capture) and the release
    // restores THAT. The span and the accepted cost are at the core's
    // pointer_position_unknown_ declaration; the GUI needs no
    // knowledge of it, which is what lets its one per-iteration refresh run the
    // same way whether or not a capture is live.
    if (input_.pointer_position_unknown()) return;
    // APPLY ONLY ON A CHANGE. The GUI calls this once per run-loop iteration —
    // its cursor has ONE owner and that owner runs at the loop boundary — so an
    // unmoving answer must cost nothing, and a set_cursor per iteration would be
    // real protocol traffic for no visible difference.
    if (kind == input_.cursor_kind()) return;
    input_.remember_cursor_kind(kind);
    apply_cursor_kind();
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

    // THE PAINT LOOP MUST NOT DECLARE DAMAGE. It range-fors over buf->pending
    // — the same vector invalidate_region appends to — so an invalidate_region
    // call from inside on_redraw (from any paint pass) would push_back into
    // the vector being iterated and invalidate the loop. No paint pass does:
    // painting is pure pixel production here, and nothing it could ask for
    // mid-walk can be honoured by the pass already walking. Anything that
    // needs to declare damage around a frame does it BEFORE this loop through
    // the pre-paint hook (in_pre_paint_ above, the supported route — it runs
    // before buffer acquisition precisely so it may add to damage_) or from
    // ordinary event/tick code after the frame, never from inside a paint
    // pass.
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

    // Never called from inside the paint loop (the hazard and the supported
    // pre-paint route are stated at that loop, paint_one_frame).

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
                         "warptempo_gui: Connection to the compositor lost; "
                         "wl_display_flush failed: %s\n",
                         std::strerror(errno));
            should_exit_ = true;
            wl_display_cancel_read(wl_display_);
            break;
        }

        // pfds[2] is the async-render completion eventfd; pfds[3] is the
        // waveform-worker completion eventfd; pfds[4] is the checkpoint
        // worker's; pfds[5] is the history prefetch's ready signal. When no fd
        // is registered (fd == -1), events=0 so poll() ignores the slot —
        // same trick used for "watch only when we care."
        struct pollfd pfds[6];
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
        pfds[4].fd     = history_worker_completion_fd_;
        pfds[4].events = (history_worker_completion_fd_ >= 0) ? POLLIN : 0;
        pfds[4].revents = 0;
        pfds[5].fd     = history_prefetch_completion_fd_;
        pfds[5].events = (history_prefetch_completion_fd_ >= 0) ? POLLIN : 0;
        pfds[5].revents = 0;

        int n = poll(pfds, 6, -1);

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
                         "warptempo_gui: Connection to the compositor lost; "
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
            // Both software deadlines, sampled in the core's own fixed order
            // (key repeat, then the touch window) — the arbitration is stated
            // at GuiInputCore::tick.
            input_.tick();
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

        if (history_worker_completion_fd_ >= 0 &&
            (pfds[4].revents & POLLIN)) {
            uint64_t cnt = 0;
            (void)read(history_worker_completion_fd_, &cnt, sizeof(cnt));
            if (on_history_worker_completion_) {
                on_history_worker_completion_();
            }
        }

        if (history_prefetch_completion_fd_ >= 0 &&
            (pfds[5].revents & POLLIN)) {
            uint64_t cnt = 0;
            (void)read(history_prefetch_completion_fd_, &cnt, sizeof(cnt));
            if (on_history_prefetch_ready_) {
                on_history_prefetch_ready_();
            }
        }

        // THE ITERATION HAS SETTLED. Everything this pass dispatched is above:
        // the display's events, the tick, and all four worker events. A loop
        // boundary is by definition after every write any of them made, which is
        // what lets a consumer here derive an answer without knowing who wrote
        // what — the reason this hook exists at all is stated at its setter.
        //
        // NOT THE PRE-PAINT AND NOT THE TICK, both considered and both wrong for
        // it: the pre-paint runs only when a frame is scheduled, so a state
        // change that damages nothing would never reach it, and the tick is the
        // PLAYBACK cadence — firing there would make a second cadence own a fact
        // that belongs to the loop.
        //
        // THE `continue` AND `break` PATHS ABOVE DELIBERATELY SKIP THIS. EINTR
        // dispatched nothing, so nothing settled; the two connection-loss breaks
        // leave a display that can no longer be talked to; and the should_exit_
        // test below covers the clean quit, where the loop is about to end and
        // the answer would be computed for a frame that is never presented. The
        // test is here rather than inside the consumer so the platform keeps the
        // one fact ("are we leaving?") that the consumer has no way to see.
        if (!should_exit_) loop_settled_hook_(input_.current_mods());
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
    } else if (std::strcmp(interface, wl_data_device_manager_interface.name) == 0) {
        // Core protocol (the same wayland.xml as wl_seat), REQUIRED since
        // 2026-08-02: it is the system clipboard. v2 is the FLOOR — that is
        // where wl_data_device.release arrived, and both teardown paths call
        // it. v3 is the cap, the highest the protocol defines and the highest
        // the listener slots above cover. An older advertisement leaves the
        // manager unbound, which the required-globals gate in init() refuses.
        if (version >= 2) {
            const uint32_t v = std::min<uint32_t>(version, 3);
            wl_data_device_manager_ = static_cast<struct wl_data_device_manager*>(
                wl_registry_bind(r, name, &wl_data_device_manager_interface, v));
            ensure_data_device();
        } else {
            std::fprintf(stderr,
                         "warptempo_gui: wl_data_device_manager v%u advertised; "
                         "v2 or newer is required\n", version);
        }
    } else if (std::strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0) {
        // Both pointer-capture managers are v1 and OPTIONAL: absence degrades
        // strip drags to clamped absolute motion (see begin_pointer_capture).
        pointer_constraints_ = static_cast<struct zwp_pointer_constraints_v1*>(
            wl_registry_bind(r, name, &zwp_pointer_constraints_v1_interface, 1));
    } else if (std::strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0) {
        relative_pointer_manager_ = static_cast<struct zwp_relative_pointer_manager_v1*>(
            wl_registry_bind(r, name, &zwp_relative_pointer_manager_v1_interface, 1));
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
        // wl_pointer.axis event (value120 is wheel-only); the input core's
        // pointer_frame() arbitrates the two streams so each source counts once per frame
        // and drains to discrete wheel steps. v5 release semantics still
        // apply at v8 (see the wl_seat cleanup in destroy_wayland_state).
        const uint32_t v = std::min<uint32_t>(version, 8);
        wl_seat_ = static_cast<struct wl_seat*>(
            wl_registry_bind(r, name, &wl_seat_interface, v));
        seat_global_name_ = name;
        wl_seat_add_listener(wl_seat_, &s_seat_listener, this);
        // The data device is created against the seat; this arm and the data
        // device manager's arm both call it, so whichever global is advertised
        // second creates it (and a replacement seat gets a fresh device).
        ensure_data_device();
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

    // The wl_data_device was created from this seat, so it dies with it (along
    // with every offer and source hanging off it, and the input serial their
    // set_selection needed). A replacement seat's bind arm creates a new one.
    destroy_data_device_state();

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
            input_.set_surface_width(width_);
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
        input_.set_surface_width(width_);
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

void GuiPlatform::on_toplevel_configure(int32_t width, int32_t height,
                                        struct wl_array* states) {
    pending_w_ = width;
    pending_h_ = height;

    // WINDOW ACTIVATION, read off this configure's state array. The protocol
    // sends the COMPLETE state set every time, so "activated" is simply whether
    // the value is present in THIS array — absence means deactivated, and no
    // separate un-set event exists to listen for.
    //
    // The array is walked by hand rather than with wl_array_for_each: that macro
    // assigns the array's `void* data` straight to the iterator, which is a C
    // conversion C++ rejects. Same walk, spelled with an explicit cast.
    bool activated = false;
    if (states != nullptr && states->data != nullptr) {
        const uint32_t* v = static_cast<const uint32_t*>(states->data);
        const size_t    n = states->size / sizeof(uint32_t);
        for (size_t i = 0; i < n; ++i) {
            if (v[i] == XDG_TOPLEVEL_STATE_ACTIVATED) { activated = true; break; }
        }
    }
    // EDGE ONLY. Every resize and every maximize re-delivers the same states, so
    // firing unconditionally would damage the top strip on each one for no
    // change; the hook's contract is the edge, and it is stated at its setter.
    if (activated != window_activated_) {
        window_activated_ = activated;
        if (activation_changed_hook_) activation_changed_hook_();
    }
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
        // CAPABILITY LOSS NEED NOT BE PRECEDED BY keyboard.leave, which is why
        // this edge tears the keyboard's modeled state down itself rather than
        // trusting the leave to have done it: pointer input delivered while no
        // keyboard exists must not inherit a phantom chord from the last
        // keyboard event (a modifier-carrying chord). The teardown is
        // GuiInputCore::forget_keyboard_state's, in full.
        input_.forget_keyboard_state();
    }

    if (has_pointer && !wl_pointer_) {
        wl_pointer_ = wl_seat_get_pointer(wl_seat_);
        wl_pointer_add_listener(wl_pointer_, &s_pointer_listener, this);
        // The relative pointer is created once alongside the wl_pointer (no-op
        // when the manager is absent or the object already exists).
        create_relative_pointer_if_ready();
    } else if (!has_pointer && wl_pointer_) {
        // THE HARD END OF THIS POINTER STREAM, and the POLICY half of it is the
        // core's (pointer_capability_lost / forget_pointer_state, whose ordering
        // contract is at their declarations): the leave hook and both
        // keyboard-adjacent hold ends are OWED while these objects still exist,
        // and the focus and frame scratch are dropped once they are gone. This
        // body keeps the protocol teardown and its own ordering clause — the
        // holds precede end_pointer_capture because unlocking rewrites the
        // tracked position to the cursor restore hint, and the relative pointer
        // depends on the wl_pointer, which goes last because the restore runs
        // through it.
        input_.pointer_capability_lost();
        end_pointer_capture();
        destroy_relative_pointer();
        wl_pointer_release(wl_pointer_);
        wl_pointer_ = nullptr;
        input_.forget_pointer_state();
    }

    const bool has_touch = (caps & WL_SEAT_CAPABILITY_TOUCH) != 0;
    if (has_touch && !wl_touch_) {
        // wl_touch is NOT a required capability and its absence is SILENCE — no
        // stderr, nothing degraded: a seat without glass is the ordinary case
        // on the authoring laptop, not a poor environment.
        wl_touch_ = wl_seat_get_touch(wl_seat_);
        wl_touch_add_listener(wl_touch_, &s_touch_listener, this);
    } else if (!has_touch && wl_touch_) {
        // THE HARD END OF THE TOUCH STREAM: the contract is wl_touch.cancel's,
        // shared whole, and it is the core's (touch_capability_lost). The
        // pointer- and keyboard-capability edges above deliberately do not
        // reach into it: each input source dies on its own stream's edges.
        input_.touch_capability_lost();
        wl_touch_release(wl_touch_);
        wl_touch_ = nullptr;
    }
}

void GuiPlatform::on_keyboard_keymap(uint32_t format, int fd, uint32_t size) {
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        std::fprintf(stderr,
                     "warptempo_gui: Unsupported keymap format %u\n", format);
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
                     "warptempo_gui: Keymap mmap failed: %s\n",
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
    // FOCUS LOSS IS THE COMMON CASE FOR THE SUPER RESET forget_keyboard_state
    // performs: focus commonly leaves on a Super chord labwc grabbed, and that
    // is exactly the departure that would otherwise latch the tracked Super bit
    // and deaden the keyboard for the rest of the session. The teardown is
    // GuiInputCore::forget_keyboard_state's, in full.
    input_.forget_keyboard_state();
}

// THE KEYCODE -> GuiKey TRANSLATION, and the ONE place a key event's identity
// is decided (hoisted 2026-08-13, when key RELEASES grew an application-side
// consumer and had to agree with their press about what key they are). It
// answers false for a keycode this GUI has no use for, which is the same thing
// as "there is nothing to deliver".
//
// Drop keys the GUI never acts on, before delivery and before arming
// repeat, for two reasons that converge on the same handling:
//
//   - Standalone modifier presses (Shift / Control / Alt / Super /
//     Meta / Hyper, the Caps and Shift locks, the AltGr level-shifts):
//     the modifier STATE this program acts on arrives SEPARATELY via
//     on_keyboard_modifiers, and no consumer matches a modifier keysym
//     as a GuiKey, so the key event is pure noise. "SEPARATELY", NOT
//     "COMPLETELY": on_keyboard_modifiers tracks exactly FOUR — ctrl /
//     shift / alt, which reach the application through GuiInputState, and
//     SUPER, which reaches nothing because it gates key PRESS delivery
//     instead (deliver_key; releases are ungated, the corner at
//     set_on_key_release). The others (Meta, Hyper, the locks, the level-shifts)
//     are modelled nowhere, so no predicate can see them; a future binding
//     that needed one would have to model it first — which is exactly the
//     gap Super had until 2026-07-30.
//   - Function keys F1..F35: this GUI binds none of them.
//
// Ignoring both matters because a text editor treats any key it does
// not own as "exit the edit". A bare Shift press (the prefix of
// Shift+Left) would tear down the edit before the arrow arrived, and an
// F-key press would discard the edit for nothing. Ignoring them is the
// correct behavior for keys the GUI has no use for.
bool GuiPlatform::key_from_keycode(uint32_t xkb_keycode, GuiKey& out) const {
    if (!xkb_keymap_ || !xkb_state_) return false;
    const xkb_layout_index_t layout =
        xkb_state_serialize_layout(xkb_state_, XKB_STATE_LAYOUT_EFFECTIVE);
    const xkb_keysym_t* syms = nullptr;
    const int nsyms = xkb_keymap_key_get_syms_by_level(
        xkb_keymap_, xkb_keycode, layout, 0, &syms);
    if (nsyms <= 0 || !syms) return false;
    const xkb_keysym_t sym = syms[0];
    if (sym == XKB_KEY_NoSymbol) return false;
    if ((sym >= XKB_KEY_F1      && sym <= XKB_KEY_F35) ||      // 0xffbe..0xffe0, function keys
        (sym >= XKB_KEY_Shift_L && sym <= XKB_KEY_Hyper_R) ||  // 0xffe1..0xffee, modifiers
        sym == XKB_KEY_ISO_Level3_Shift ||                     // AltGr
        sym == XKB_KEY_ISO_Level5_Shift ||
        sym == XKB_KEY_Mode_switch) {
        return false;
    }
    // Case-fold ASCII uppercase keysyms to lowercase so consumers see a
    // single GuiKey value per physical key regardless of shift state.
    // Other keysyms pass through.
    GuiKey key = static_cast<GuiKey>(sym);
    if (key >= 'A' && key <= 'Z') key |= 0x20;
    out = key;
    return true;
}

void GuiPlatform::on_keyboard_key(uint32_t serial, uint32_t /*time*/,
                                  uint32_t keycode, uint32_t state) {
    // Cache the serial for wl_data_device.set_selection. Every copy is a Ctrl+C
    // or Ctrl+X key event, so the serial stored here IS the triggering event's
    // own by the time clipboard_set_text runs. Cached BEFORE the xkb and
    // release gates below so a release, a key with no keymap, and a key held
    // under Super all keep it current — the compositor validates the serial
    // against its input history, not against what this program did with the
    // key.
    last_input_serial_ = serial;

    if (!xkb_state_) return;

    // Wayland delivers raw evdev keycodes (offset by 8 for X11
    // compatibility — xkbcommon expects this offset).
    const uint32_t xkb_keycode = keycode + 8;

    // THE BACKEND'S HALF OF A KEY EVENT, and all of it: the keymap translation
    // (key_from_keycode, which answers 0 — nothing to deliver — for a key this
    // GUI has no use for), the xkb keycode as the core's stable per-key
    // identity, and the codepoint the press resolves to under the live keyboard
    // state. Everything the event MEANS — the release routing, the bare-`e`
    // emulation, the repeat arming and the delivery — is the core's key_event.
    const bool pressed = (state != WL_KEYBOARD_KEY_STATE_RELEASED);
    GuiKey key = 0;
    if (!key_from_keycode(xkb_keycode, key)) key = 0;
    const uint32_t codepoint =
        pressed ? xkb_state_key_get_utf32(xkb_state_, xkb_keycode) : 0u;
    input_.key_event(key, xkb_keycode, pressed, codepoint);
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

    // The four tracked modifiers, read out of the updated xkb state and handed
    // over as plain bools. What an EDGE in them means — the staged-motion flush
    // and the wheel remainder's drop — is the core's set_modifiers.
    input_.set_modifiers(
        xkb_state_mod_name_is_active(xkb_state_, XKB_MOD_NAME_CTRL,
                                     XKB_STATE_MODS_EFFECTIVE),
        xkb_state_mod_name_is_active(xkb_state_, XKB_MOD_NAME_SHIFT,
                                     XKB_STATE_MODS_EFFECTIVE),
        xkb_state_mod_name_is_active(xkb_state_, XKB_MOD_NAME_ALT,
                                     XKB_STATE_MODS_EFFECTIVE),
        xkb_state_mod_name_is_active(xkb_state_, XKB_MOD_NAME_LOGO,
                                     XKB_STATE_MODS_EFFECTIVE));

    // No on_key synthesis on modifier change — the next non-modifier
    // key event carries the updated state.
}

void GuiPlatform::on_keyboard_repeat_info(int32_t rate, int32_t delay) {
    input_.set_repeat_info(rate, delay);
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

    pointer_enter_serial_ = serial;

    // Hand the compositor the cursor the REMEMBERED KIND names — not the arrow.
    // A pointer that left the window over the waveform and came back to the
    // same spot must return with the same cursor; the core's entry motion
    // records the entry coordinates, and the tail of THIS loop iteration
    // re-derives the kind from them, correcting the remembered one if the GUI's
    // answer moved while we had no pointer. The serial is stashed above, so the
    // applier's own tracked-serial use is this enter's serial.
    apply_cursor_kind();

    // The position as a fractional DOUBLE: which pixel it names is the core's
    // one containment owner to say, never a decode here.
    input_.pointer_enter(wl_fixed_to_double(surface_x),
                         wl_fixed_to_double(surface_y));
}

void GuiPlatform::on_pointer_leave(uint32_t /*serial*/,
                                   struct wl_surface* surface) {
    if (surface != wl_surface_) return;
    input_.pointer_leave();
}

void GuiPlatform::on_pointer_motion(uint32_t /*time*/,
                                    int32_t surface_x, int32_t surface_y) {
    input_.pointer_motion(wl_fixed_to_double(surface_x),
                          wl_fixed_to_double(surface_y));
}

void GuiPlatform::on_pointer_button(uint32_t /*serial*/, uint32_t /*time*/,
                                    uint32_t button, uint32_t state) {
    GuiMouseButton mb;
    if (!translate_pointer_button(button, mb)) return;
    input_.pointer_button(mb, state == WL_POINTER_BUTTON_STATE_PRESSED);
}

void GuiPlatform::on_pointer_axis(uint32_t /*time*/,
                                  uint32_t axis, int32_t value) {
    // Live path for touchpad two-finger scroll and any other continuous
    // (non-wheel) source. value120 carries WHEEL scroll only; the compositor
    // sends a touchpad's continuous delta through this legacy wl_pointer.axis
    // event (in its continuous scroll unit, a wl_fixed_t) and sends no value120
    // for that frame. So this is the touchpad's only path. The vertical axis is
    // the only one this program reads; the staging and the per-frame
    // arbitration are the core's.
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
    input_.pointer_axis(wl_fixed_to_double(value));
}

void GuiPlatform::on_pointer_axis_value120(uint32_t axis, int32_t value120) {
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
    input_.pointer_axis_value120(value120);
}

// ---------------------------------------------------------------------------
// Strip-drag pointer capture
// ---------------------------------------------------------------------------

void GuiPlatform::create_relative_pointer_if_ready() {
    if (!relative_pointer_manager_ || !wl_pointer_ || relative_pointer_) return;
    relative_pointer_ = zwp_relative_pointer_manager_v1_get_relative_pointer(
        relative_pointer_manager_, wl_pointer_);
    zwp_relative_pointer_v1_add_listener(relative_pointer_,
                                         &s_relative_pointer_listener, this);
}

// ---------------------------------------------------------------------------
// Touch: the unit decode, and nothing else (the phase machine is the core's)
// ---------------------------------------------------------------------------

void GuiPlatform::on_touch_down(uint32_t /*serial*/, uint32_t /*time*/,
                                int32_t id, int32_t fx, int32_t fy) {
    input_.touch_down(id, wl_fixed_to_double(fx), wl_fixed_to_double(fy));
}

void GuiPlatform::on_touch_motion(uint32_t /*time*/, int32_t id,
                                  int32_t fx, int32_t fy) {
    input_.touch_motion(id, wl_fixed_to_double(fx), wl_fixed_to_double(fy));
}

void GuiPlatform::destroy_relative_pointer() {
    if (relative_pointer_) {
        zwp_relative_pointer_v1_destroy(relative_pointer_);
        relative_pointer_ = nullptr;
    }
}

void GuiPlatform::begin_pointer_capture(GuiCursorKind restore_kind) {
    // Guarded no-op when a capture is already active (a second strip-drag press
    // cannot exist while the button is held, but the guard makes the contract
    // explicit). Degraded compositor (either manager absent, so no relative
    // pointer) leaves the gesture on clamped absolute motion.
    if (input_.pointer_captured()) return;
    if (!pointer_constraints_ || !relative_pointer_ || !wl_pointer_ ||
        !wl_surface_)
        return;

    // Everything a capture OPENS with — the travel ledger seeded from the
    // tracked position, the restore row, the cleared override, the degenerate
    // wrap span and the unfrozen notional x — is the core's
    // (begin_capture_seed, whose contract carries the whole of it).
    input_.begin_capture_seed();

    // Hide the cursor. set_cursor with a NULL surface is the protocol's "hide"
    // request; the tracked enter serial authorizes it. THE ONE set_cursor CALL
    // OUTSIDE apply_cursor_kind, and deliberately so: the hide is not a KIND, it
    // is the absence of one, and routing it through the owner would need a third
    // enumerator whose only job is to mean "no cursor". The remembered kind keeps
    // naming what will come BACK at the release — the stamp below writes it once
    // the lock PROXY exists (activation is asynchronous and deliberately not
    // waited for; the ruling is at the header contract), which is also why the
    // hide can precede the request: the request is expected to be granted.
    wl_pointer_set_cursor(wl_pointer_, pointer_enter_serial_, nullptr, 0, 0);

    // Lock the pointer at its current position. NULL region = surface input
    // region; PERSISTENT so a transient focus wobble does not tear the lock out
    // from under the gesture (we destroy it explicitly at gesture end).
    locked_pointer_ = zwp_pointer_constraints_v1_lock_pointer(
        pointer_constraints_, wl_surface_, wl_pointer_, nullptr,
        ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    if (!locked_pointer_) {
        // Creation failed: un-hide and stay uncaptured. The core's captured bit
        // is still false here, so the applier's capture guard lets this through,
        // and the cursor comes back as whatever kind was showing — NOT the
        // gesture's restore kind, which is deliberately not stamped until the
        // lock proxy exists. This arm is a CREATION failure — the compositor
        // never got the request — and is a different thing from a compositor
        // that takes the request and defers or declines the activation, which
        // this code does not detect at all (the ruling on that is at the header
        // contract). Nothing was requested, so nothing is virtual: the gesture
        // runs on real coordinates and the loop tail owns the cue from here,
        // exactly as on the degraded returns above.
        apply_cursor_kind();
        return;
    }
    zwp_locked_pointer_v1_add_listener(locked_pointer_,
                                       &s_locked_pointer_listener, this);

    // THE LOCK PROXY EXISTS, so the capture is live, the gesture's own cue is
    // stamped as the kind the release will hand back, and the tracked position
    // stops being the pointer's — all three the core's, at note_capture_locked.
    // Why the stamp is the CALLER'S answer rather than something worked out
    // here is at this method's contract (the restore_kind parameter).
    input_.note_capture_locked(restore_kind);
}

void GuiPlatform::end_pointer_capture() {
    release_pointer_lock(/*apply_restore_hint=*/true);
}

void GuiPlatform::release_pointer_lock(bool apply_restore_hint) {
    if (!input_.pointer_captured() && !locked_pointer_) return;  // idempotent

    if (locked_pointer_) {
        if (apply_restore_hint) {
            // Return the cursor when the lock is destroyed, y frozen at the
            // press row (capture_restore_y_). The x is the anchor-stem column
            // the strip drag supplied (capture_restore_x_override_) when set —
            // the edge-trick rebind pins the stem while the raw cursor travel
            // keeps going, so restoring at the stem lands the cursor dead on it
            // rather than past it. With no override (the grab-pan, which has no
            // stem) the x is notional_pointer_x_ — THE NOTIONAL POSITION, not
            // the travel ledger (architect 2026-08-14; the pair's contract is
            // at the declaration), and that is the WHOLE fork now: the position
            // wraps edge to edge inside the waveform under a capture instead of
            // pinning at a bound, so it is always somewhere ordinary and there
            // is no runaway case left to detect. The raw travel used to be
            // handed over unclamped on the argument that the compositor clamps
            // an off-window hint back on-screen anyway, and it does — but its
            // clamp is applied ONCE, to a number carrying the whole
            // off-window DEBT, so a drag that went 3000 px past the edge and
            // came 750 px back still restored AT the edge. The notional
            // position clamps at every step and has no debt — and, under a
            // capture, WRAPS to the waveform's opposite bound rather than
            // pinning at one, so however far the hand ran it is somewhere
            // ordinary and the cursor comes back where the pointer notionally
            // is. The hint
            // is surface-local, the same space as the stem's surface x, and is
            // double-buffered against the constrained surface, so commit it
            // before destroying the lock.
            // THERE IS NO SECOND ARM (architect 2026-08-14, from the rig:
            // "whatever the virtual point is is where the cursor shows back
            // up"). The teleport-on-clamp that used to send a pinned pointer
            // back to the capture's home is gone with the pinning itself — the
            // wrap removes the runaway case rather than answering it, so the
            // release has one rule and needs no verdict to read (the full
            // record is at notional_pointer_x_). An override still outranks
            // this: a stem column is a place the gesture NAMED.
            const double restore_x = input_.capture_restore_x();
            const double restore_y = input_.capture_restore_y();
            zwp_locked_pointer_v1_set_cursor_position_hint(
                locked_pointer_,
                wl_fixed_from_double(restore_x),
                wl_fixed_from_double(restore_y));
            if (wl_surface_) wl_surface_commit(wl_surface_);
            // THE TRACKED POSITION FOLLOWS THE HINT — the fix for a click
            // dispatched at the capture's unbounded VIRTUAL TRAVEL. Button
            // events carry no coordinates in the protocol, so every delivery
            // site hands the GUI pointer_x_/pointer_y_ — and through the lock
            // those fields hold the virtual travel, a point the pointer does
            // not occupy. The unlock warp does NOT come back as a
            // wl_pointer.motion (the compositor warps its cursor and updates
            // the seat's position without sending one), so without this write
            // the stale travel survived until the user next physically moved
            // the mouse, and a click made before that moved was routed at it.
            // THE DEFECT THAT CLOSED (historical account — the ruler entry it
            // rode was deleted for good on 2026-08-12 and the dual-axis strip
            // drag itself on 2026-08-15; the exposure and this
            // fix are entry-agnostic, and the nav drag's own capture reaches
            // them the same way): a ruler-band press armed that strip drag,
            // a drag UP to zoom walked the virtual
            // position up out of the ruler and into row 4's icon band — one
            // zoom level is 60 px, the bands ~20-70 px apart — the release
            // drew the cursor back on the ruler at the press row, and the NEXT
            // click, made without moving, was delivered at the travel's end:
            // over the W radio, whose chord is bare `p`. A single visible
            // click in the ruler switched the marker view. Every roster
            // button, and the bare-`e` synthesized click (which reads the same
            // two fields), had the same exposure; the grab-pan's is wider
            // still, since it sets no restore-x override and its raw travel
            // can end anywhere.
            // WHY THE HINT IS THE RIGHT VALUE: it is precisely where the cursor
            // is DRAWN from here, so the tracked position and the pixels the
            // user is aiming at now agree. It is an estimate, not an
            // observation — which is why pointer_position_unknown_ is
            // deliberately NOT cleared here (see its contract, and the tail of
            // this function): the compositor is still the only authority on
            // where the pointer came back, cursor kinds stay dropped until it
            // says so, and the next absolute event overwrites this with the
            // truth. The virtual pair moves with it so a second capture seeds
            // from the drawn position rather than from the last travel, AND SO
            // DOES THE NOTIONAL POSITION: it outlives the capture (it is the
            // process-wide answer to "where is the pointer?"), and past the
            // unlock the pixels the cursor is drawn on are the best answer
            // there is — a stem-override restore MOVED the cursor, so leaving
            // the notional position at the capture's last travel column would
            // leave it naming a place the cursor is not.
            // No motion is synthesized: this edge runs inside the GUI's own
            // release handler, and re-entering it is not worth the hover
            // refresh the next real motion delivers anyway.
            //
            // THE WRITE-BACK'S CLAMP IS NOW A GUARD RATHER THAN A CORRECTION
            // (2026-08-14). It was introduced 2026-08-08 because the HINT was
            // the raw travel and could name a point outside the surface, while
            // the RECORD models where a CLICK CAN BE ROUTED — surface-local by
            // definition, every consumer of pointer_x_/pointer_y_ hit-testing
            // against surface rects — so an off-window value stored here was a
            // point no press could legitimately land on, and until the next
            // physical motion a click would route at it. The grab-pan was the
            // one producer, setting no restore-x override. BOTH hint sources
            // are in-surface as they are written (the notional position wrapped
            // into the waveform's span and clamped at every step, and a stem
            // column that lives inside the waveform), so this clamp changes
            // nothing it is handed — with one narrow arm left to earn it, a
            // window NARROWED mid-capture, which leaves the stem column and the
            // wrap span measured against a width that is gone. It stays as the
            // record's own guard either
            // way, since the record's rule is about the record and not about
            // who supplied the value. It is
            // exact in every case that matters besides: if the compositor's
            // screen-clamp really did draw the cursor outside this window, a
            // pointer-leave follows and the record stops being consulted at
            // all; if it drew it inside, the drawn point IS the clamped one.
            // (The separately accepted staleness — a compositor that revokes
            // the lock without applying the hint — is untouched: that path
            // passes apply_restore_hint = false and never reaches here.)
            const double max_x = width_  > 0 ? static_cast<double>(width_  - 1)
                                             : 0.0;
            const double max_y = height_ > 0 ? static_cast<double>(height_ - 1)
                                             : 0.0;
            const double tracked_x = std::clamp(restore_x, 0.0, max_x);
            const double tracked_y = std::clamp(restore_y, 0.0, max_y);
            input_.apply_capture_restore(tracked_x, tracked_y);
        }
        zwp_locked_pointer_v1_destroy(locked_pointer_);
        locked_pointer_ = nullptr;
    }
    // The captured bit drops here, and the lateral freeze with it, on BOTH
    // exits — the hint arm above and the revoked-lock path that skips it — so
    // nothing a zoom phase asserted can survive into the next gesture (the
    // core's end_capture, which also records why its order against the
    // write-back above carries no meaning).
    input_.end_capture();

    // Restore the REMEMBERED KIND at the tracked enter serial — not the arrow.
    // For a capture that stamped (the lock-proxy path) that kind is the one the
    // GESTURE ITSELF STAMPED at begin_pointer_capture: Zoom for the strip drag,
    // Pan for the grab-pan, the
    // cue the gesture wears by identity rather than one inferred from what was
    // on screen when the press landed. That is the whole reason it is stamped —
    // the cursor is re-derived once per RUN-LOOP ITERATION, so the batch that
    // delivered the press can also have delivered the motion or the modifier
    // edge that chose the zone, leaving the remembered kind a cue from BEFORE
    // the gesture existed. The restore puts the pointer back inside the zone the
    // stamped kind names (the anchor-stem column or the notional x, y frozen at
    // the press row). What keeps the stamp standing through the drag is
    // set_cursor_kind's drop: no kind named during the capture — the live-gesture
    // Arrow the GUI re-derives at every loop iteration the lock's relative motion
    // wakes, above all — was ever recorded here.
    // The GUI's next motion would correct a hard-coded arrow, but the frames in
    // between would be a lie — which is why THIS edge reads a value rather than
    // computing one: the platform's edges do not know where the pointer is in the
    // GUI's terms, so the answer has to have been handed to them (by the loop
    // tail's set_cursor_kind ordinarily, by the gesture's stamp for a capture).
    // The captured bit was cleared just above, so the applier's capture guard
    // admits this call. It is a no-op when the wl_pointer is already gone (a
    // pointer-capability loss releases it before this runs).
    // The core's pointer_position_unknown_ deliberately STAYS SET past this
    // point: the hint
    // above is a request, and only the compositor's next absolute event says
    // where the pointer actually came back. THE COORDINATES AND THE FLAG PART
    // COMPANY HERE, which is the whole shape of the fix above: the tracked
    // position is written to the hint clamped into the surface (the pixels the
    // cursor is drawn on where that is inside this window, and the nearest point
    // a click could route at where it is not), while the flag keeps saying the
    // estimate is not
    // an observation — so cursor kinds stay dropped until the compositor speaks
    // and no OTHER consumer is left holding a point the pointer never occupied.
    apply_cursor_kind();
}

void GuiPlatform::on_locked_pointer_locked() {
    // The lock activated — and there is deliberately nothing to do, because the
    // capture never waited for this event: begin_pointer_capture hid the cursor,
    // seeded the virtual position and stamped the restore kind the moment the
    // lock PROXY was created. Treating the request as granted is the optimistic
    // model, ruled 2026-08-03; its scope (labwc and properly-coded compositors,
    // a deferring or declining one being an unsupported environment) and the
    // degradation it accepts are recorded once at begin_pointer_capture's
    // contract. The listener stays installed for its other half —
    // on_locked_pointer_unlocked, which is live and releases.
}

void GuiPlatform::on_locked_pointer_unlocked() {
    // Compositor revoked the lock (e.g. focus loss). Restore the cursor and
    // drop virtual mode without the restore-position hint (the lock is already
    // inactive). The gesture ends later through the normal button-release /
    // lost-button paths, where end_pointer_capture is a harmless idempotent
    // no-op.
    //
    // A NOTED RESIDUAL (2026-08-06): the tracked-position write-back that
    // release_pointer_lock performs lives in its HINT arm, so this path leaves
    // pointer_x_/pointer_y_ carrying the capture's virtual travel until the next
    // absolute event re-seats them. There is no hint to follow here — the lock
    // is already gone, so no warp happens and there is no destination to write —
    // which is why the arm is where it is. The exposure is a click made after a
    // compositor revoke and before any enter or motion, narrower than the defect
    // the write-back closed and adversarial-compositor adjacent; recorded rather
    // than patched, so a future reader does not read the write-back as
    // unconditional.
    release_pointer_lock(/*apply_restore_hint=*/false);
}

// ---------------------------------------------------------------------------
// The system clipboard (the CLIPBOARD selection)
//
// THE OBJECT LIFETIMES, stated once here because every bug this area ever had
// was a lifetime bug and the two 2026-07-12 fixes existed for exactly that:
//
//   * AN ANNOUNCEMENT IS NOT A CLAIM. wl_data_device.data_offer creates an
//     object whose ROLE is unknown until the selection event that follows it.
//     It parks in pending_data_offer_, and only an announcement supersedes an
//     announcement — clipboard_offer_ is a separate slot and is never freed by
//     the announcement path. Aliasing those two slots is what used to
//     double-free on the second external clipboard change and on a null-clear.
//   * ONE TEARDOWN, TWO EXITS. destroy_data_device_state is the whole story
//     for the seat-bound objects, and both shutdown and seat registry removal
//     call it. Two hand-written teardowns disagreeing about which slots existed
//     is the other half of the original defect.
//   * NULL IS A REAL SELECTION EVENT. selection(NULL) means the clipboard was
//     cleared (or is ours); it destroys the superseded offer and leaves the
//     slot empty, and a paste then finds nothing and does nothing.
//   * OWNERSHIP IS A BIT, NOT AN OBJECT. clipboard_we_own_ spans a successful
//     set_selection to the cancelled event, and its ONLY job is the self-paste
//     short circuit — reading our own selection through the pipe would deadlock
//     this single-threaded loop against itself.
// ---------------------------------------------------------------------------

void GuiPlatform::ensure_data_device() {
    if (wl_data_device_ || !wl_data_device_manager_ || !wl_seat_) return;
    wl_data_device_ = wl_data_device_manager_get_data_device(
        wl_data_device_manager_, wl_seat_);
    if (wl_data_device_) {
        wl_data_device_add_listener(wl_data_device_,
                                    &s_data_device_listener, this);
    } else {
        std::fprintf(stderr,
                     "warptempo_gui: wl_data_device_manager_get_data_device "
                     "failed; the system clipboard is unavailable\n");
    }
}

void GuiPlatform::destroy_pending_offer() {
    if (pending_data_offer_) {
        wl_data_offer_destroy(pending_data_offer_);
        pending_data_offer_ = nullptr;
    }
    pending_offer_text_mime_.clear();
}

void GuiPlatform::destroy_data_device_state() {
    destroy_pending_offer();
    if (clipboard_offer_) {
        // The pending and clipboard slots are disjoint by construction, so the
        // destroy above cannot have freed this object.
        wl_data_offer_destroy(clipboard_offer_);
        clipboard_offer_ = nullptr;
    }
    clipboard_offer_text_mime_.clear();
    if (clipboard_source_) {
        wl_data_source_destroy(clipboard_source_);
        clipboard_source_ = nullptr;
    }
    clipboard_we_own_ = false;
    if (wl_data_device_) {
        wl_data_device_release(wl_data_device_);  // v2+; the bind floor
        wl_data_device_ = nullptr;
    }
    last_input_serial_ = 0;
    // clipboard_send_text_ is deliberately NOT cleared: it is the app's own
    // last copied payload, not a protocol object.
}

void GuiPlatform::on_data_offer(struct wl_data_offer* offer) {
    destroy_pending_offer();
    pending_data_offer_ = offer;
    wl_data_offer_add_listener(offer, &s_data_offer_listener, this);
}

void GuiPlatform::on_data_offer_mime_type(struct wl_data_offer* offer,
                                          const char* mime) {
    if (!mime) return;
    if (offer == pending_data_offer_) {
        note_offer_text_mime(pending_offer_text_mime_, mime);
    } else if (offer == clipboard_offer_) {
        // The protocol sends every mime before the selection event claims the
        // offer, but keep the claimed slot truthful if one arrives late.
        note_offer_text_mime(clipboard_offer_text_mime_, mime);
    }
}

void GuiPlatform::on_selection(struct wl_data_offer* offer) {
    if (offer == clipboard_offer_) return;
    if (clipboard_offer_) wl_data_offer_destroy(clipboard_offer_);
    clipboard_offer_ = offer;              // null when the selection was cleared
    clipboard_offer_text_mime_.clear();
    if (offer && offer == pending_data_offer_) {
        // Claim the announcement: the mimes it collected become the clipboard
        // slot's, and the pending slot empties WITHOUT destroying the object it
        // just handed over.
        clipboard_offer_text_mime_ = pending_offer_text_mime_;
        pending_data_offer_ = nullptr;
        pending_offer_text_mime_.clear();
    }
}

void GuiPlatform::clipboard_set_text(const std::string& text) {
    // The payload is mirrored first and unconditionally, so it is current for
    // the `send` below and for a self-paste even if the claim itself fails.
    clipboard_send_text_ = text;
    if (!wl_data_device_manager_ || !wl_data_device_) return;
    if (clipboard_source_) {
        // Replacing our own source. Destroying it first means the compositor's
        // answering `cancelled` can never be routed at the source that replaces
        // it — a destroyed proxy delivers no more events.
        wl_data_source_destroy(clipboard_source_);
        clipboard_source_ = nullptr;
    }
    clipboard_source_ = wl_data_device_manager_create_data_source(
        wl_data_device_manager_);
    if (!clipboard_source_) {
        clipboard_we_own_ = false;
        return;
    }
    wl_data_source_add_listener(clipboard_source_,
                                &s_data_source_listener, this);
    wl_data_source_offer(clipboard_source_, "text/plain;charset=utf-8");
    wl_data_source_offer(clipboard_source_, "text/plain");
    wl_data_device_set_selection(wl_data_device_, clipboard_source_,
                                 last_input_serial_);
    clipboard_we_own_ = true;
}

void GuiPlatform::on_data_source_send(struct wl_data_source* /*src*/,
                                      const char* /*mime*/, int fd) {
    // Both offered mimes name the same bytes, so the requested one does not
    // change what is written. This runs only for an EXTERNAL consumer (a
    // self-paste never reaches the pipe), and the payloads are one-line values
    // far below a pipe buffer, so the blocking write cannot stall the loop.
    //
    // THE FD IS THE CONSUMER'S, so it can vanish under us: a consumer that
    // closes its read end mid-transfer makes the write fail. That is a
    // survivable outcome only because SIGPIPE IS IGNORED PROCESS-WIDE (set at
    // startup in main.cpp, where the rationale lives) — with the default
    // disposition the signal would kill the GUI before the loop below ever saw
    // the error. Abandoning is the whole recovery: the payload is still ours,
    // the selection claim is untouched, and nothing here holds state to unwind.
    const char* p = clipboard_send_text_.data();
    size_t      left = clipboard_send_text_.size();
    while (left > 0) {
        const ssize_t n = ::write(fd, p, left);
        if (n <= 0) break;            // the receiver went away (EPIPE etc.)
        p    += n;
        left -= static_cast<size_t>(n);
    }
    ::close(fd);
}

void GuiPlatform::on_data_source_cancelled(struct wl_data_source* src) {
    // Another client claimed the selection. The source is dead; the payload is
    // not — clipboard_send_text_ survives as what we last copied.
    if (src != clipboard_source_) return;
    wl_data_source_destroy(clipboard_source_);
    clipboard_source_ = nullptr;
    clipboard_we_own_ = false;
}

std::string GuiPlatform::clipboard_get_text() {
    if (clipboard_we_own_) return clipboard_send_text_;
    if (!clipboard_offer_ || clipboard_offer_text_mime_.empty()) {
        return std::string();
    }
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        std::fprintf(stderr,
                     "warptempo_gui: pipe2 for the clipboard read failed: %s\n",
                     std::strerror(errno));
        return std::string();
    }
    wl_data_offer_receive(clipboard_offer_,
                          clipboard_offer_text_mime_.c_str(), fds[1]);
    ::close(fds[1]);
    // Flush so the receive request actually reaches the offering client before
    // the read starts; without it the source never sees the request and the
    // read burns its whole deadline for nothing.
    wl_display_flush(wl_display_);
    const std::string out = read_clipboard_data(fds[0]);
    ::close(fds[0]);
    return out;
}

std::string GuiPlatform::read_clipboard_data(int read_fd) {
    // Bounded and non-blocking. The offering client gets one full second for
    // its whole payload — a per-poll cutoff would turn ordinary scheduler delay
    // into a silently truncated paste — and kMaxBytes bounds a source that
    // streams without end, hostile or merely broken. EVERY failure returns the
    // empty string rather than what arrived so far: a partial paste is worse
    // than no paste, and the caller reads empty as nothing to paste.
    std::string out;
    fcntl(read_fd, F_SETFL, O_NONBLOCK);
    constexpr size_t kMaxBytes = 1024u * 1024u;
    const uint64_t deadline_us = gui_monotonic_us() + 1'000'000;
    bool failed = false;
    char buf[4096];
    for (;;) {
        const uint64_t now_us = gui_monotonic_us();
        if (now_us >= deadline_us) {
            std::fprintf(stderr, "warptempo_gui: Clipboard read timed out\n");
            failed = true;
            break;
        }
        const int remaining_ms =
            static_cast<int>((deadline_us - now_us + 999) / 1000);
        struct pollfd pfd { read_fd, POLLIN, 0 };
        const int pr = poll(&pfd, 1, remaining_ms);
        if (pr == 0) {
            std::fprintf(stderr, "warptempo_gui: Clipboard read timed out\n");
            failed = true;
            break;
        }
        if (pr < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "warptempo_gui: Clipboard read failed: %s\n",
                         std::strerror(errno));
            failed = true;
            break;
        }
        const ssize_t n = ::read(read_fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            std::fprintf(stderr, "warptempo_gui: Clipboard read failed: %s\n",
                         std::strerror(errno));
            failed = true;
            break;
        }
        if (n == 0) break;                       // EOF
        out.append(buf, static_cast<size_t>(n));
        if (out.size() > kMaxBytes) {
            std::fprintf(stderr,
                         "warptempo_gui: Clipboard payload exceeded %zu bytes; "
                         "paste abandoned\n", kMaxBytes);
            failed = true;
            break;
        }
    }
    return failed ? std::string() : out;
}

// ---------------------------------------------------------------------------
// Setters (callbacks)
// ---------------------------------------------------------------------------

void GuiPlatform::set_on_redraw(RedrawCallback cb)              { on_redraw_ = std::move(cb); }
void GuiPlatform::set_on_resize(ResizeCallback cb)              { on_resize_ = std::move(cb); }
void GuiPlatform::set_on_close(CloseCallback cb)                { on_close_ = std::move(cb); }
void GuiPlatform::set_activation_changed_hook(std::function<void()> cb) { activation_changed_hook_ = std::move(cb); }
void GuiPlatform::set_loop_settled_hook(std::function<void(GuiInputState)> cb) { loop_settled_hook_ = std::move(cb); }
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
void GuiPlatform::set_history_worker_completion_fd(int fd, std::function<void()> on_event) {
    history_worker_completion_fd_  = fd;
    on_history_worker_completion_  = std::move(on_event);
}
void GuiPlatform::set_history_prefetch_completion_fd(int fd, std::function<void()> on_event) {
    history_prefetch_completion_fd_ = fd;
    on_history_prefetch_ready_      = std::move(on_event);
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

int GuiPlatform::width()  const { return width_; }
int GuiPlatform::height() const { return height_; }

// ---------------------------------------------------------------------------
// The input core's doors, forwarded
//
// Every one of these is GuiInputCore's, re-exported so the GUI's seven
// consumers hold ONE object and see no seam. The contracts are the core's, at
// each method's own declaration in input_core.h; nothing is decided here.
// ---------------------------------------------------------------------------

void GuiPlatform::set_on_key(KeyCallback cb)                    { input_.set_on_key(std::move(cb)); }
void GuiPlatform::set_on_key_release(KeyReleaseCallback cb)     { input_.set_on_key_release(std::move(cb)); }
void GuiPlatform::set_on_button_press(ButtonCallback cb)        { input_.set_on_button_press(std::move(cb)); }
void GuiPlatform::set_on_button_release(ButtonCallback cb)      { input_.set_on_button_release(std::move(cb)); }
void GuiPlatform::set_on_wheel(WheelCallback cb)                { input_.set_on_wheel(std::move(cb)); }
void GuiPlatform::set_on_motion(MotionCallback cb)              { input_.set_on_motion(std::move(cb)); }
void GuiPlatform::set_wheel_context_probe(WheelContextProbe cb)     { input_.set_wheel_context_probe(std::move(cb)); }
void GuiPlatform::set_text_editor_active_probe(TextEditorProbe cb)  { input_.set_text_editor_active_probe(std::move(cb)); }
void GuiPlatform::set_repeat_eligible_probe(RepeatEligibleProbe cb) { input_.set_repeat_eligible_probe(std::move(cb)); }
int64_t GuiPlatform::key_repeat_period_ms() const { return input_.key_repeat_period_ms(); }
void GuiPlatform::set_pointer_left_hook(std::function<void(GuiPointerLeaveReason)> cb) { input_.set_pointer_left_hook(std::move(cb)); }
void GuiPlatform::set_keyboard_intent_cancel_hook(std::function<void()> cb) { input_.set_keyboard_intent_cancel_hook(std::move(cb)); }
void GuiPlatform::set_touch_nav_hooks(
    std::function<void(const GuiTouchNavFrame&)> update,
    std::function<void()> end,
    std::function<bool(int x, int y)> pan_zone,
    std::function<bool(int x, int y)> thin_lane,
    std::function<void(int x, int y)> region_begin,
    std::function<void(int x, int y)> region_update,
    std::function<void()> region_end) {
    input_.set_touch_nav_hooks(std::move(update), std::move(end),
                               std::move(pan_zone), std::move(thin_lane),
                               std::move(region_begin),
                               std::move(region_update),
                               std::move(region_end));
}
bool GuiPlatform::touch_contact_active() const { return input_.touch_contact_active(); }
void GuiPlatform::set_capture_restore_x(double surface_x)   { input_.set_capture_restore_x(surface_x); }
void GuiPlatform::clear_capture_restore_x()                 { input_.clear_capture_restore_x(); }
void GuiPlatform::set_capture_restore_kind(GuiCursorKind kind) { input_.set_capture_restore_kind(kind); }
void GuiPlatform::set_notional_x_frozen(bool frozen)        { input_.set_notional_x_frozen(frozen); }
void GuiPlatform::set_notional_pointer_x(double surface_x)  { input_.set_notional_pointer_x(surface_x); }
void GuiPlatform::set_capture_wrap_span(double lo, double hi) { input_.set_capture_wrap_span(lo, hi); }
double GuiPlatform::notional_pointer_x() const { return input_.notional_pointer_x(); }
