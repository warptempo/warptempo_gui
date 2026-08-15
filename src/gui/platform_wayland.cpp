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

// THE TOUCH DISAMBIGUATION CONSTANTS, all RULED RETUNABLES (touch phase 1,
// 2026-08-11; the WINDOW is back since the sixth glass ruling, 2026-08-12,
// after the timer-free model's one-session field life, and TWO-DEADLINE
// since the eighth glass ruling the same day — retune on glass, not
// on argument).
//
// kTouchDisambiguateMs is the window between the FIRST finger's down and the
// commitment to the one-finger resolution — it exists to tell tap from drag
// from two fingers, so a second finger landing inside it becomes the
// navigation gesture with no press ever delivered and nothing to unwind (the
// jump-free pinch — the field verdict that brought the window back; touch.md
// carries the record). 60 ms is short enough that a deliberate tap feels
// immediate and long enough that the two fingers of an intended pinch, which
// land a frame or two apart, are seen as a pair. Both deadlines are sampled
// on the timerfd tick (the key-repeat precedent), so expiry lands within one
// tick (<= ~16 ms) of the mark — the window is a feel bound, not an exact
// timer. It is the OFF-ZONE deadline: expiry there resolves to the pointer
// translation — hold unlocks the pointer, which is what keeps the
// endcap/bridge grabs, the flag drags and every off-zone press-and-hold
// gesture alive on glass.
//
// kTouchRegionHoldMs is the PAN ZONE's own stretched window — the
// REGION-HOLD BEAT (the eighth glass ruling, 2026-08-12: pan is the common
// act and takes the primary drag, so the region is the deliberate act —
// "region select to be hold and then drag because the pan is way more
// common", the duration his own "maybe five hundred milliseconds"). A down
// on the navigation surface runs its window to this deadline, and the
// EXPIRY there is the REGION HOLD: the region former armed through the
// region hooks, so hold-then-drag sweeps a region on glass. 500 ms is the
// same order as labwc's own 575 ms key-repeat delay — a beat the hand
// already knows — and long past any aimed drag's natural dwell, the lesson
// of the dead kTouchTrimHoldMs (the trim band's hold-a-beat deadline of
// 2026-08-11, whose first cut rode the 60 ms window and turned every
// deliberate band drag into the trim move; that GESTURE stayed dead — this
// beat revives only its two-deadline PATTERN, on a surface whose quick drag
// is the pan, not a pointer drag, so the dwell collision cannot recur).
//
// kTouchSlopPx is the physical-pixel travel that resolves the window EARLY
// (a finger that is already dragging should not wait out the window; the
// resolution FORKS on the down point's pan-zone answer — single-finger nav
// on the pan surface, the pointer elsewhere) and, reused, the navigation
// gestures' latch (centroid travel or finger-distance change past it starts
// navigating; under it a two-finger tap navigates nothing, and a
// single-finger nav is born past it by construction) and, a third time, the
// live translation's MOVED latch (the Pointer clause at the state block: a
// second finger forks on it — moved drags ignore, motionless holds upgrade).
// 8 px DELIBERATELY EQUALS the GUI's one generic press-becomes-drag gate
// (kDragMovedThresholdPx, app_state.h — not included here; the platform sits
// below the GUI model, so the twin value is stated rather than shared): a
// slop-crossing resolution delivers its crossing motion in the same burst as
// the press, and that motion crosses the GUI's own drag gate by
// construction, so a touch drag becomes a drag the moment it resolves.
constexpr int    kTouchDisambiguateMs = 60;
constexpr int    kTouchRegionHoldMs   = 500;
constexpr double kTouchSlopPx         = 8.0;

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
    static void touch_up(void* data, struct wl_touch*, uint32_t serial,
                         uint32_t time, int32_t id) {
        static_cast<GuiPlatform*>(data)->on_touch_up(serial, time, id);
    }
    static void touch_motion(void* data, struct wl_touch*, uint32_t time,
                             int32_t id, wl_fixed_t x, wl_fixed_t y) {
        static_cast<GuiPlatform*>(data)->on_touch_motion(time, id, x, y);
    }
    static void touch_frame(void* data, struct wl_touch*) {
        static_cast<GuiPlatform*>(data)->on_touch_frame();
    }
    static void touch_cancel(void* data, struct wl_touch*) {
        static_cast<GuiPlatform*>(data)->on_touch_cancel();
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
        static_cast<GuiPlatform*>(data)->on_relative_pointer_motion(
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
    pointer_captured_ = false;
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
    // which point pointer_captured_ is already false.
    if (pointer_captured_) return;

    // THE PER-KIND FALLBACK, and the one place it lives: a kind whose xcursor
    // name the theme did not carry has a null surface, and it shows the ARROW
    // instead. That is what makes a missing name cost the cue and nothing else.
    const ThemeCursor& want = cursors_[cursor_kind_index(cursor_kind_)];
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
    // restores THAT. The span and the accepted cost are at
    // pointer_position_unknown_'s declaration; the GUI needs no
    // knowledge of it, which is what lets its one per-iteration refresh run the
    // same way whether or not a capture is live.
    if (pointer_position_unknown_) return;
    // APPLY ONLY ON A CHANGE. The GUI calls this once per run-loop iteration —
    // its cursor has ONE owner and that owner runs at the loop boundary — so an
    // unmoving answer must cost nothing, and a set_cursor per iteration would be
    // real protocol traffic for no visible difference.
    if (kind == cursor_kind_) return;
    cursor_kind_ = kind;
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
            maybe_fire_repeat();
            // The touch disambiguation window's deadline rides the same tick
            // the key-repeat deadline does (its lazy twin runs at each touch
            // event's arrival; granularity is recorded at the constant).
            maybe_resolve_touch_window();
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
        if (!should_exit_) loop_settled_hook_(current_mods());
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
        // wl_pointer.axis event (value120 is wheel-only); on_pointer_frame()
        // arbitrates the two streams so each source counts once per frame
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

// THE ONE TEARDOWN FOR "the keyboard's modeled state is gone", called from both
// edges that mean it: wl_keyboard.leave and keyboard-capability loss. Each site
// keeps only the justification that is ITS OWN; everything the two share is
// here.
//
// SUPER IS RESET WITH THE OTHER THREE, and that one is load-bearing rather than
// tidy: deliver_key GATES on mod_super_ and no modifiers event need follow the
// edge, so a latched bit would deaden the keyboard for the rest of the session.
//
// THE WHEEL REMAINDER GOES UNCONDITIONALLY. scroll_accum_ is sub-detent travel
// bound to the chord it was accumulated under, and this edge ends every chord
// there was without a scroll frame to re-probe; there is no state left to
// compare it against, so it is simply dropped.
//
// THE CURSOR OWES NOTHING TO THIS EDGE ANY MORE, and that is the point of the
// per-iteration owner rather than an omission here: dropping the modifier bits
// (and, below, a held synthesized button that can end a gesture) changes what
// the cursor's zone map answers, and the run loop's tail re-derives it in this
// same iteration — a wl_keyboard.leave and a capability loss both arrive as
// dispatched events, so neither can outrun the boundary. A fire of its own
// would only be an earlier answer to the same question, from a spot that would
// then owe an ordering rule about the teardown below it.
void GuiPlatform::forget_keyboard_state() {
    // A MODIFIER EDGE IS A DELIVERY BOUNDARY FOR STAGED CAPTURED MOTION, and
    // this is the SECOND route that moves the modeled bits — the argument is
    // recorded once, at on_keyboard_modifiers' own flush. Losing the key
    // stream drops ctrl, and ctrl selects what a live navigation drag MEANS,
    // so motion staged while it was held must be delivered while it still
    // reads held. A no-op with nothing staged, which is every case but a live
    // capture.
    if (mod_ctrl_ || mod_shift_ || mod_alt_) flush_deferred_motion();
    mod_ctrl_ = mod_shift_ = mod_alt_ = mod_super_ = false;
    repeat_key_   = 0;
    scroll_accum_ = 0.0;
    // The key stream is over (or its modeled state untrustworthy), and the
    // application's own held key intent must die with the platform's: this is
    // the keyboard-intent cancellation hook's first fire class (contract at
    // set_keyboard_intent_cancel_hook; the consumer's effect list at
    // main.cpp's hook body).
    if (keyboard_intent_cancel_hook_) keyboard_intent_cancel_hook_();

    // A held synthesized-left button can never see its keycode-matched release
    // once the key stream has ended, so end it here.
    // IT TOUCHES NO POINTER CAPTURE, and that is what keeps this edge correct
    // for free where the pointer-side twin had to be ordered for it (2026-08-08):
    // a lock's release REWRITES the tracked coordinates to the cursor restore
    // hint, so a hold ended after one would deliver its release — and a moved
    // strip drag's final apply — at the hint instead of the drag's real last
    // position. Nothing above unlocks anything, so the release lands on the live
    // travel and the GUI's own release body performs the restore from inside it,
    // exactly as an ordinary release does. Do not add a capture teardown above
    // this line; end the hold first if one is ever needed here.
    // ITS FLUSH RESURRECTS NOTHING EITHER, the pointer twin's other hazard
    // (2026-08-08): the release below can flush a deferred motion, and
    // on_motion's prologue writes app-side state — the remembered coordinates
    // and `pointer_in_window` — while everything THIS edge clears is
    // platform-side (the four modifier bits, the armed repeat, the scroll
    // carry), which no motion writes. The two sets do not intersect, so this
    // edge needs no restoring second act. `pointer_in_window` going true here is
    // correct besides: a keyboard leave says nothing about where the pointer is.
    // IT DROPS NO STAGED MOTION EITHER (re-checked 2026-08-08): this edge
    // destroys no pointer object, so a flag left set is delivered by the next
    // frame on the same live relative pointer, which is the correct owner and
    // the ordinary path. What the edge DOES owe is the ATTRIBUTION — motion
    // staged under the modifiers it is about to forget — and that is paid at
    // the top of this function, above the clear, rather than here.
    end_left_hold_source(/*physical=*/false);
}

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
        // forget_keyboard_state's, in full.
        forget_keyboard_state();
    }

    if (has_pointer && !wl_pointer_) {
        wl_pointer_ = wl_seat_get_pointer(wl_seat_);
        wl_pointer_add_listener(wl_pointer_, &s_pointer_listener, this);
        // The relative pointer is created once alongside the wl_pointer (no-op
        // when the manager is absent or the object already exists).
        create_relative_pointer_if_ready();
    } else if (!has_pointer && wl_pointer_) {
        // THE HARD END OF THIS POINTER STREAM: no leave, no motion and no release
        // will arrive on this object again. Ordered hook, holds, teardown, one
        // clause each. THE HOOK LEADS because its clear_dropdown_pointer_state
        // drops the popup's press claim and armed item, so a held menu item does
        // not fire on a hardware edge — a menu item is a click convention, not a
        // drag commit (architect 2026-08-08), while the no-cancel ruling governs
        // the GESTURES below. THE HOLDS END NEXT, both sources — the synthesized
        // bare-`e` one included, its gesture being a pointer gesture in all but
        // origin — because an undelivered release would latch the drag-modal gate
        // with no event left to lift it; the pair delivers AT MOST one release
        // (each fires only on the logical OR's 1->0 edge, and a live TOUCH
        // hold — the OR's third source — keeps the logical button down, its
        // own stream owning the eventual release) and zeroes
        // synth_left_keycode_, so the later `e` keyup matches nothing. THEY
        // PRECEDE end_pointer_capture because unlocking rewrites pointer_x_/y_ to
        // the cursor restore hint, and the release — with a moved strip drag's
        // final apply riding it — must read the drag's true last position; the
        // GUI's own release body then tears the capture down from inside, leaving
        // the call below a no-op backstop for a lock no hold owned. The
        // wl_pointer goes last, since that restore runs through it.
        //
        // HANDLED TO NOT-BROKEN, NOT TO POLISHED (architect 2026-08-09): after a
        // capability loss, a staged-motion flush may leave pointer_in_window true
        // and a hover face stale until the pointer next enters; accepted, do not
        // re-guard.
        if (pointer_left_hook_)
            pointer_left_hook_(GuiPointerLeaveReason::CapabilityLoss);
        end_left_hold_source(/*physical=*/true);
        end_left_hold_source(/*physical=*/false);
        end_pointer_capture();
        // The relative pointer depends on the wl_pointer; destroy it first.
        destroy_relative_pointer();
        wl_pointer_release(wl_pointer_);
        wl_pointer_ = nullptr;
        pointer_focused_   = false;

        // A sub-detent carry and the staged half of a logical pointer frame
        // belong to the destroyed pointer object — the staged relative motion
        // included, since it was staged on the relative pointer destroyed just
        // above. They must not combine with input from a later wl_pointer created
        // when the seat regains the capability, even if cursor region and
        // modifiers happen to match.
        scroll_accum_       = 0.0;
        scroll_context_key_ = 0;
        frame_v120_accum_   = 0.0;
        frame_axis_accum_   = 0.0;
        frame_have_v120_    = false;
        frame_have_axis_    = false;
        frame_have_relmotion_ = false;
    }

    const bool has_touch = (caps & WL_SEAT_CAPABILITY_TOUCH) != 0;
    if (has_touch && !wl_touch_) {
        // wl_touch is NOT a required capability and its absence is SILENCE — no
        // stderr, nothing degraded: a seat without glass is the ordinary case
        // on the authoring laptop, not a poor environment.
        wl_touch_ = wl_seat_get_touch(wl_seat_);
        wl_touch_add_listener(wl_touch_, &s_touch_listener, this);
    } else if (!has_touch && wl_touch_) {
        // THE HARD END OF THE TOUCH STREAM: no motion, up, or cancel will
        // arrive on this object again. The contract is wl_touch.cancel's,
        // shared whole (hard_end_touch_stream): a live pointer translation
        // commits — release delivered at the last position, then the
        // focus-forked translation end (deliver_touch_translation_end: the
        // ordinary leave, or a restore motion at a focused mouse) — a live
        // nav gesture (single- or two-finger) ends through its end path with
        // its staged final frame dropped, an unresolved disambiguation
        // window drops silently, and all touch state is forgotten. The
        // pointer- and keyboard-capability edges above deliberately do not
        // reach in here: each input source dies on its own stream's edges.
        hard_end_touch_stream();
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
    // is exactly the departure that would otherwise latch mod_super_ and deaden
    // the keyboard for the rest of the session. The teardown is
    // forget_keyboard_state's, in full.
    forget_keyboard_state();
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
//     SUPER, which reaches nothing because it gates key DELIVERY instead
//     (deliver_key). The others (Meta, Hyper, the locks, the level-shifts)
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

    if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        // Cancel repeat if the released key was the one repeating.
        if (xkb_keycode == repeat_keycode_) {
            repeat_key_ = 0;
        }
        // End a synthesized-left hold on its owning keycode's release. The
        // keycode match means a kLeftClickKey press typed into an editor —
        // which never started a hold — has no effect here (synth_left_keycode_
        // is 0 unless a hold is live). The release is NEVER gated: not by the
        // editor probe (an editor opened mid-hold must not orphan the button)
        // and not by pointer focus (it lands at the last known coordinates).
        // THIS IS NOT THE HOLD'S ONLY END, and the keycode match is what makes
        // the others cost nothing here: the two HARD edges end it themselves —
        // keyboard leave / keyboard-capability loss (forget_keyboard_state) and
        // POINTER-capability loss (on_seat_capabilities, which must, the gesture
        // being a pointer gesture) — and each zeroes synth_left_keycode_ as it
        // goes, so the keyup that eventually arrives matches nothing and is a
        // plain no-op rather than a second release.
        if (xkb_keycode == synth_left_keycode_) {
            end_left_hold_source(/*physical=*/false);
            // That release WAS the mouse button's, so it is not a key release
            // as well: the press it matches was swallowed here too, and
            // delivering this one would hand the application an unpaired edge.
            return;
        }
        // THE APPLICATION-SIDE KEY RELEASE (2026-08-13). It exists for exactly
        // one consumer — the modal dialog's keyboard press-and-hold, whose act
        // is at the lift — and it is delivered for the same key identity the
        // press carried, through the one translation both branches read. NOT
        // gated on Super (a release binds nothing on its own; the reasoning is
        // at set_on_key_release, platform_wayland.h) and carrying no modifier
        // state at all.
        GuiKey key = 0;
        if (on_key_release_ && key_from_keycode(xkb_keycode, key))
            on_key_release_(key);
        return;
    }

    // Pressed.
    GuiKey key = 0;
    if (!key_from_keycode(xkb_keycode, key)) return;

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
            // The synthesized button is a button: this press is a context event
            // that kills an armed key repeat (layer 1), same as a physical one.
            repeat_key_ = 0;
            // Logical state before this source's edge: synth is false here, so
            // the OR is the other two sources (a touch hold counts — three
            // devices sharing BTN_LEFT never double-deliver).
            const bool was_held = pointer_left_held_ || touch_left_held_;
            synth_left_held_    = true;
            synth_left_keycode_ = xkb_keycode;
            if (!was_held && on_button_press_) {
                flush_deferred_motion();
                on_button_press_(GuiMouseButton::Left,
                                 pointer_x_, pointer_y_, current_mods());
            }
        }
        return;
    }

    GuiInputState mods = current_mods();
    mods.codepoint = xkb_state_key_get_utf32(xkb_state_, xkb_keycode);
    // Arm key repeat (last-key-wins). Eligibility is decided at press time by
    // the application probe under the pre-dispatch context (evaluated before
    // deliver_key runs, so a press that opens an editor is judged pre-open):
    // only held-step gestures and editor typing repeat; every edge-triggered
    // command is one-shot, and an ineligible press disarms any armed repeat.
    // The repeat contract is two layers: (1) the stored intent dies on exactly
    // three input edges — a different key press (which re-arms or disarms here
    // via press-time eligibility), a pointer-button PRESS (physical, the
    // synthesized `e`, or the touch translation's resolution press), and a
    // COMPLETED wheel emission — each clearing
    // repeat_key_ at its platform input chokepoint; pointer motion, button
    // release, and sub-detent scroll accumulation deliberately do not disarm;
    // (2) each fire additionally re-checks the level conditions (eligibility
    // under the live modifiers, and the editor-active flag still matching its
    // arm-time value). The arm-time editor flag is captured here for layer (2).
    // LAYER (1) IS LOAD-BEARING FOR UNDO CORRECTNESS as well as hand-feel — it is
    // what makes "no synthesized repeat can follow an intervening command" true,
    // which is the property undo coalescing rides. The full statement is at
    // maybe_fire_repeat, beside the bit it sets.
    // A press dropped for SUPER arms nothing (and, through the else branch below,
    // disarms whatever was armed — the ordinary "a different key press re-arms or
    // disarms" edge of layer (1)). Without this the press would be swallowed at
    // deliver_key while still arming a burst that starts firing the moment Super is
    // released, and those fires carry synthesized_repeat — an undo merge into an
    // OLDER entry, from a press the application never saw. The probe itself is
    // untouched: this is a platform-side arming gate, not a modifier predicate.
    const bool repeat_ok =
        !mod_super_ &&
        repeat_eligible_probe_ && repeat_eligible_probe_(key, mods);
    const bool editor_ctx =
        text_editor_active_probe_ && text_editor_active_probe_();
    deliver_key(key, mods);
    if (repeat_period_us_ > 0 && repeat_ok) {
        repeat_key_        = key;
        repeat_keycode_    = xkb_keycode;
        repeat_editor_ctx_ = editor_ctx;
        repeat_due_us_     = monotonic_us() + repeat_delay_us_;
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

    // THE THREE MODELED MODIFIERS ARE READ FIRST AND ASSIGNED BELOW, because
    // the gap between the two is a DELIVERY BOUNDARY.
    const bool next_ctrl  = xkb_state_mod_name_is_active(
        xkb_state_, XKB_MOD_NAME_CTRL,
        XKB_STATE_MODS_EFFECTIVE);
    const bool next_shift = xkb_state_mod_name_is_active(
        xkb_state_, XKB_MOD_NAME_SHIFT,
        XKB_STATE_MODS_EFFECTIVE);
    const bool next_alt   = xkb_state_mod_name_is_active(
        xkb_state_, XKB_MOD_NAME_ALT,
        XKB_STATE_MODS_EFFECTIVE);
    const bool modeled_edge = next_ctrl  != mod_ctrl_ ||
                              next_shift != mod_shift_ ||
                              next_alt   != mod_alt_;

    if (modeled_edge) {
        // STAGED CAPTURED MOTION IS DELIVERED UNDER THE MODIFIER STATE IT
        // ARRIVED IN. A captured drag defers its relative motion to the
        // wl_pointer.frame boundary (on_relative_pointer_motion) and the
        // delivery reads current_mods(), so motion that arrived under one
        // modifier state but was still staged when this event landed would be
        // delivered under the NEW one: a pan delta staged with ctrl UP and
        // flushed after a ctrl-down edge has its dy applied as zoom and its dx
        // discarded, the reverse edge pans with a staged zoom delta, and two
        // edges in one batch collapse every accumulated tick onto the final
        // state. Since 2026-08-14 ctrl SELECTS WHAT THE GESTURE MEANS mid-drag
        // (ScrollDragState, app_state.h), so this edge is exactly where the
        // meaning changes and motion made under the old meaning must be
        // delivered under it — the one-frame wrong-axis lurch the live-modifier
        // model exists to eliminate.
        // The POINTER FRAME'S OWN flush is reused rather than a second one
        // written: it delivers at the accumulated virtual position and clears
        // frame_have_relmotion_, so the frame's trailing delivery simply finds
        // nothing left. Splitting one frame in two costs one extra on_motion_
        // per human-rate modifier edge and batches nothing else — that flag
        // stages the DELIVERY alone (pointer_x_/y_ are already written at
        // arrival, so a button dispatched later in the frame is unaffected,
        // and the wheel's per-frame accumulators are untouched here).
        flush_deferred_motion();
    }

    mod_ctrl_  = next_ctrl;
    mod_shift_ = next_shift;
    mod_alt_   = next_alt;
    // SUPER, tracked for ONE purpose: gating key DELIVERY (deliver_key). It is
    // deliberately absent from current_mods() and from the scroll-chord reset
    // below — the wheel chords are plain and Ctrl only, so a Super press changes
    // no chord and must not drop an accumulating sub-detent remainder.
    mod_super_ = xkb_state_mod_name_is_active(
        xkb_state_, XKB_MOD_NAME_LOGO,
        XKB_STATE_MODS_EFFECTIVE);

    // THE MODIFIER EDGE, and its ONE remaining consumer AFTER the assignment
    // (the flush above is this same edge's consumer BEFORE it — that is the
    // whole reason the two are separated). This event fires when the modifier
    // state CHANGES and not on ordinary key presses, so the test is only about
    // the three modifiers the application models (a Super edge moves nothing
    // here). The OTHER place the modeled state moves is forget_keyboard_state,
    // where it vanishes with no event to announce it; it drops the wheel
    // remainder outright, for the reason recorded there.
    if (modeled_edge) {
        // It ends any continuous wheel chord session, so the sub-detent
        // remainder — bound to the old chord — is dropped outright, before a
        // scroll frame that would re-probe. TWO live wheel chords since
        // 2026-08-12 (the plain stepped pan and the Ctrl zoom step), which is
        // exactly the case the shape-general rule exists for — remainder
        // accumulated while panning must never assemble a detent as a zoom, or
        // the reverse — and every other modified wheel is a swallowed non-chord
        // the remainder must not bridge into either.
        scroll_accum_ = 0.0;
        // THE POINTER CURSOR USED TO BE THE SECOND CONSUMER HERE, through a
        // hook fired on this same test — modifiers SELECT between cursor kinds
        // over the waveform, so a Ctrl pressed under a resting pointer has to
        // move the cue with no motion under it. It is no longer this edge's
        // business: this event is DISPATCHED inside a run-loop iteration, and
        // that iteration's tail re-derives the cursor from the settled state
        // (set_loop_settled_hook), which answers the modifier case and every
        // other stale class with one owner instead of a hook per fact. THE
        // LIVE NAV DRAG'S ZOOM/PAN MODE JOINED THAT TAIL 2026-08-14 for the
        // same reason — a released Ctrl must drop the zoom stem with no motion
        // under it — so do not add a gesture hook here either. The flush above
        // is the one thing that CANNOT live at the tail: it is about motion
        // already staged when this event arrived, and by the tail the bits
        // have moved.
    }

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
    // Logical left button: the physical BTN_LEFT, the kLeftClickKey
    // synthesized hold, or the touch translation's hold. Drags consult this
    // bit on motion; without the OR a synthesized-key or touch drag tears on
    // the first motion event.
    s.primary_button_held =
        pointer_left_held_ || synth_left_held_ || touch_left_held_;
    return s;
}

void GuiPlatform::deliver_key(GuiKey key, GuiInputState mods) {
    // SUPER IS DROPPED AT THE PLATFORM BOUNDARY (architect 2026-07-30: super
    // belongs to labwc and is not part of this program's vocabulary). STRICT
    // MODIFIER VALIDATION says an unbound modifier combination is a no-op
    // everywhere (architect 2026-07-28) — and it was FALSE for Super, because the
    // mask never modelled it: every bare-exact predicate reads three bools, so a
    // held Logo left mods == {false,false,false} and Super+Escape reached the
    // editors' cancel, Super+Return their commit. labwc grabs many Super chords,
    // but Escape / Return / Space / Delete / the arrows / Home / End are not among
    // them and arrive here. Rather than add a fourth bool to every reader, the
    // event is dropped HERE, which makes the rule true BY CONSTRUCTION for the one
    // modifier this program never binds.
    // THIS IS THE SHARED DELIVERY PATH, which is what makes one gate enough: both
    // the physical press (on_keyboard_key) and the SYNTHESIZED REPEAT
    // (maybe_fire_repeat) come through here. A key held BEFORE Super went down
    // keeps its armed repeat — a modifier keysym is dropped ahead of the repeat
    // arming, so it disarms nothing, and the eligibility re-probe cannot see Super
    // either — so its repeats keep firing and are simply swallowed here for as long
    // as Super is held, resuming when it is released. That is exactly the intent,
    // and it is safe for undo coalescing: no keyboard command can run in the gap
    // (every key is dropped), and a pointer press in the gap disarms the repeat
    // outright through layer (1) of the repeat contract.
    // KEY RELEASES need nothing: they are platform-internal (on_keyboard_key
    // returns on the release path), feeding only the repeat cancel and the
    // synthesized-left hold end — neither of which may be skipped, or a hold would
    // orphan. POINTER EVENTS ARE OUT OF SCOPE by the same ruling, and that includes
    // the kLeftClickKey synthesized button: `e` IS the left mouse button at this
    // boundary and returns above without reaching this function, so Super+`e`
    // behaves exactly like Super+click, which binds nothing here either way.
    if (mod_super_) {
        // THE DROPPED PRESS IS STILL AN INTERVENING KEY ARRIVAL for the
        // application's held key intent (the modal dialog's keyboard press
        // arm) — no application disarm ever sees
        // it — and the platform's own layer-1 disarmed its armed repeat at
        // this very press (the arming else-branch above, which runs before
        // this drop). So the keyboard-intent cancellation hook fires per
        // swallowed NON-SYNTHESIZED press, the faithful mirror of that edge
        // (contract and the not-at-Super's-press-edge decision at
        // set_keyboard_intent_cancel_hook). A swallowed synthesized repeat
        // fires nothing, exactly as it disarms nothing anywhere else — a
        // burst must not kill a burst.
        if (!mods.synthesized_repeat && keyboard_intent_cancel_hook_)
            keyboard_intent_cancel_hook_();
        return;
    }
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
    if (xkb_state_)
        mods.codepoint = xkb_state_key_get_utf32(xkb_state_, repeat_keycode_);
    // Layer (2) of the repeat contract — layer (1), the event-edge disarms, has
    // already killed the hold at any intervening pointer-button press or
    // completed wheel emission and re-armed/disarmed it at any key press. Re-
    // check the level conditions before delivering: the press-time eligibility
    // must still hold under the live modifiers and the editor-active flag must
    // still match its arm-time value. A mismatch or lost eligibility disarms
    // with no fire.
    const bool editor_ctx =
        text_editor_active_probe_ && text_editor_active_probe_();
    if (editor_ctx != repeat_editor_ctx_ ||
        !(repeat_eligible_probe_ && repeat_eligible_probe_(repeat_key_, mods))) {
        repeat_key_ = 0;
        return;
    }
    // THE REPEAT BIT, and the ONLY site that sets it — stamped after the
    // eligibility re-probe so the probes stay a function of key+modifiers alone.
    // It lets the application tell a held key's continuation presses from fresh
    // physical ones, and UNDO COALESCING IS BUILT ON IT (Undo::coalesce_gesture):
    // a burst carrying this bit merges with NO clock test at all, which is the
    // arm that must work at any compositor repeat delay. (A physical press takes
    // the other arm of the hybrid — the fixed 500 ms tap window — and this bit is
    // exactly what separates the two.)
    // THAT MAKES LAYER (1) OF THE REPEAT CONTRACT LOAD-BEARING FOR UNDO
    // CORRECTNESS, not just for hand-feel: because the event-edge disarms kill the
    // hold at any intervening pointer-button press, completed wheel emission, or
    // key press, no synthesized repeat can ever arrive AFTER another command ran —
    // which is the whole reason the REPEAT arm needs no adjacency test of its own
    // (the TAP arm, having no such structure, carries an explicit subject test
    // instead). Weakening a disarm would let a repeat merge a gesture into a
    // foreign command's undo entry.
    mods.synthesized_repeat = true;
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
    // AN ABSOLUTE POSITION IS THE TRUTH COMING BACK: whatever a past capture left
    // virtual is superseded here, so cursor kinds are recorded again — before the
    // synthesized motion below, whose whole job is to re-derive one. Guarded on
    // !pointer_captured_ because a lock's own virtual travel outranks a stray
    // enter (the lock keeps the pointer on this surface, so it should not arrive).
    if (!pointer_captured_) pointer_position_unknown_ = false;
    pointer_enter_serial_ = serial;

    // Hand the compositor the cursor the REMEMBERED KIND names — not the arrow.
    // A pointer that left the window over the waveform and came back to the
    // same spot must return with the same cursor; the synthesized motion below
    // records the entry coordinates, and the tail of THIS loop iteration
    // re-derives the kind from them, correcting the remembered one if the GUI's
    // answer moved while we had no pointer. The serial is already stashed above,
    // so the applier's own tracked-serial use is this enter's serial.
    apply_cursor_kind();

    // Synthesize a motion delivery so consumers register the pointer
    // as present at the entry coordinates. Matches how most clients
    // treat enter — the first "the pointer is here" notification.
    deliver_motion(pointer_x_, pointer_y_);
}

void GuiPlatform::on_pointer_leave(uint32_t /*serial*/,
                                   struct wl_surface* surface) {
    if (surface != wl_surface_) return;
    pointer_focused_ = false;
    // Fire the leave hook: no motion arrives WHILE the pointer stays outside, so
    // without this a redesigned row's button would keep its lit face for that
    // whole span after the pointer slid out the window edge. This is NOT the
    // capability-loss case — the stream is not over, re-entry fires a synthesized
    // motion (on_pointer_enter) that re-resolves, and the held state below
    // survives — so what the hook drops is the pointer-derived FACES, leaving a
    // return with nothing stale on screen rather than relying on the absence of
    // later events. The hook owns the erase damage, and main.cpp's hook body is
    // the authoritative list of what it clears. (The MARKER hover popup rode this
    // edge too until row 5 deleted it; a marker's value lives on its flag now,
    // so there is no pointer-position-dependent marker surface left — but an
    // open dropdown's item hover/arm is one, and it rides this same edge:
    // main.cpp's hook widened 2026-08-03 to drop it alongside the roster's;
    // see clear_dropdown_pointer_state.)
    // THE STAGED RELATIVE MOTION IS DELIVERED FIRST, and this edge owes that for
    // the same reason the capability branch's hold ends do: WHAT IS STILL OWED
    // GOES BEFORE THE INVARIANT RESTORE (codex 2026-08-08). A captured drag's
    // relative motion is staged, not delivered — it is coalesced to the
    // wl_pointer.frame boundary (on_relative_pointer_motion) — and the frame that
    // TERMINATES this leave runs on_pointer_frame's pending block AFTER the hook
    // below has already fired. That block calls on_motion_, whose prologue
    // unconditionally sets `pointer_in_window` TRUE, and an ordinary leave has no
    // second hook to put it back: the bit would stand for a pointer that is
    // outside, masked only while the gesture lives, and the moment that gesture
    // ends with the pointer still away (the `e` keyup, a resize or close
    // force-finalize) the tick and settled repairs would be re-admitted at stale
    // coordinates. Flushing here empties the stage, so that block is a no-op and
    // the hook's word is final.
    //
    // IT FLUSHES RATHER THAN DROPS because the delta is usually OWED, and owed
    // in a way position alone does not capture: the strip drag's `moved` latch is
    // set ONLY by the motion path, the release re-evaluates no threshold, and
    // last_x/last_y stay at the press until the crossing — so dropping the event
    // that CROSSES the Chebyshev threshold would leave `moved` false and the
    // release would commit nothing at all, losing the whole gesture rather than
    // one frame of travel. (Positionally a drop would cost nothing —
    // pointer_x_/y_ are written eagerly at stage time and last_x/last_y span any
    // gap — which is exactly why the latch is the thing that decides this.) The
    // capability teardown's reset DOES drop, beside its two frame-scratch
    // siblings, and states its own mirror-image reason: the holds are down, and
    // the relative-pointer object the motion was staged on is being destroyed.
    //
    // WHAT "STAGED" ACTUALLY IMPLIES IS ONLY THAT A CAPTURE EXISTED WHEN IT WAS
    // STAGED — not that one still stands (codex 2026-08-08, correcting the
    // stronger claim this comment used to make). THE FORCE-FINALIZE ROUTES ARE
    // THE COUNTEREXAMPLE: Ctrl+Q, the WM close and a resize each end the live
    // gesture through finalize_active_drags, and ending it releases the capture
    // WITHOUT clearing this stage (release_pointer_lock touches no frame
    // scratch). So a motion staged just before one of those, with the pointer
    // frame not yet arrived, reaches this flush with NO gesture live — and the
    // delivery lands in on_motion's PROMPT branch after Ctrl+Q or the WM close
    // (both open the close prompt) or in its NO-GESTURE TAIL after a resize.
    // Both branches recompute the roster's hover from the stale virtual
    // position.
    // WHAT MAKES THAT SAFE IS THE ORDERING, NOT UNREACHABILITY, and the
    // distinction matters for anyone editing below: THE HOOK RUNS IMMEDIATELY
    // AFTER THIS CALL, and it drops precisely what such a delivery can touch —
    // the in-window bit, the hover faces, the tooltip dwell, the armed chrome
    // press and
    // the popup's pointer state — an invariant restore placed after the last
    // thing that can disturb the invariant. ANY future change that separates the
    // hook from directly-after-this-flush has to re-establish that guarantee.
    // (The CAPABILITY branch has no such guarantee and deliberately does not: it
    // fires its hook once, ahead of its own hold ends, and records the resulting
    // staleness as an accepted glitch. This edge keeps the ordering because it
    // costs one call site and the pointer here is expected back.)
    // THE ONE EFFECT THE HOOK COULD NOT UNDO CANNOT FIRE: the tail's armed-hover
    // menu OPEN. It needs AppState::Dropdown::menu_row_armed, and every gesture
    // that can stage a relative motion began with a PRESS, which disarms the row
    // at on_button_press's top — so the bit is false at every reachable flush,
    // and the tail's open half returns at its own guard. (A stale hover PILL for
    // the frames until the hook is not a case either: the hook clears the faces
    // it just recomputed, in the same event.)
    // NO CALLER OF finalize_active_drags IS ASKED TO CLEAR THE STAGE, and that
    // is a judgement rather than an omission: the post-finalize delivery is
    // harmless on its own terms — it recomputes hover at a point that hit-tests
    // to nothing, or at worst to one button for the frames until the next motion
    // or the tick's own recompute — and it is the ORDINARY pointer-frame path
    // that delivers it when no leave follows, so clearing it at the finalize
    // would change behaviour on paths with no defect behind them.
    flush_deferred_motion();
    // OrdinaryLeave is the argument, and the sentence above is exactly what it
    // buys the consumer: the stream continues, so this is the edge on which the
    // hook body is permitted to keep state it expects a return motion to
    // re-derive (2026-08-08 — the menu row's armed mode and its hovered button,
    // on a leave whose last position was inside row 1's band).
    if (pointer_left_hook_)
        pointer_left_hook_(GuiPointerLeaveReason::OrdinaryLeave);
    // Left-held state persists across leave; the next press/release
    // will resync it. We do NOT clear pointer_left_held_ here because
    // a drag that briefly skids outside the surface and returns
    // should not lose its held state.
}

void GuiPlatform::on_pointer_motion(uint32_t /*time*/,
                                    int32_t surface_x, int32_t surface_y) {
    pointer_x_ = wl_fixed_to_int(surface_x);
    pointer_y_ = wl_fixed_to_int(surface_y);
    // The pointer's real position, from the compositor: the post-capture unknown
    // span ends here (same rule and same guard as the enter above), and this
    // iteration's tail re-derives the cursor for it once the delivery below has
    // recorded it — which is why the clear owes no cursor call of its own.
    if (!pointer_captured_) pointer_position_unknown_ = false;
    deliver_motion(pointer_x_, pointer_y_);
}

void GuiPlatform::note_notional_pointer_x(double x) {
    // ONE clamp body for every writer of the pointer's notional position (the
    // contract, and why there is exactly one such position, are at the field).
    const double max_x = width_ > 0 ? static_cast<double>(width_ - 1) : 0.0;
    // AND IT IS THE BACKSTOP RATHER THAN THE RULE for a captured pointer: the
    // capture's WRAP (on_relative_pointer_motion) folds an overshoot back in at
    // the waveform's opposite bound before the value ever gets here, so this
    // clamp bites only on a pathological delta larger than the whole span.
    // This body is deliberately NOT the wrap's owner: it serves every writer,
    // and an absolute delivery from the compositor is a real position that
    // must be stored as given rather than folded.
    notional_pointer_x_ = std::clamp(x, 0.0, max_x);
}

void GuiPlatform::deliver_motion(int x, int y) {
    // The funnel for every motion that carries a REAL position (the caller
    // classes are at the declaration): the notional position follows it, then
    // the GUI sees the ordinary delivery. Under a capture the notional one is
    // owned by the raw relative stream instead, and those deliveries carry the
    // travel ledger — which is why they do not come through here.
    note_notional_pointer_x(static_cast<double>(x));
    if (on_motion_) on_motion_(x, y, current_mods());
}

void GuiPlatform::flush_deferred_motion() {
    // A captured strip drag defers its coalesced relative motion to the pointer-
    // frame boundary (on_pointer_frame). But a wl_pointer.frame can carry both
    // that motion and a button event, and button events are NOT deferred — they
    // dispatch at arrival. Delivering the pending motion here, immediately before
    // any button, guarantees the button handler runs against the latest
    // accumulated position: a press -> threshold-crossing motion -> release inside
    // one frame then reaches the release with StripDragState.moved already true,
    // so the drag commits and a trim-bar release does not wrongly seed a
    // double-click candidate. Clearing the flag means on_pointer_frame's trailing
    // delivery does not double-fire the same motion.
    if (frame_have_relmotion_ && on_motion_) {
        on_motion_(pointer_x_, pointer_y_, current_mods());
        frame_have_relmotion_ = false;
    }
}

void GuiPlatform::end_left_hold_source(bool physical) {
    const bool held = physical ? pointer_left_held_ : synth_left_held_;
    if (!held) return;
    // Deliver only on the logical OR 1->0 edge: this source going up while
    // NEITHER other source is held (the touch translation is the third — its
    // own end lives in end_touch_left_hold, which tests these two the same
    // way). Flush the deferred motion FIRST (see the header) while this
    // source's bit still reads held, then clear, then deliver.
    const bool others_held =
        (physical ? synth_left_held_ : pointer_left_held_) || touch_left_held_;
    const bool deliver_release = !others_held && on_button_release_;
    if (deliver_release) flush_deferred_motion();
    if (physical) {
        pointer_left_held_ = false;
    } else {
        synth_left_held_    = false;
        synth_left_keycode_ = 0;
    }
    if (deliver_release) {
        on_button_release_(GuiMouseButton::Left,
                           pointer_x_, pointer_y_, current_mods());
    }
}

void GuiPlatform::on_pointer_button(uint32_t /*serial*/, uint32_t /*time*/,
                                    uint32_t button, uint32_t state) {
    GuiMouseButton mb;
    if (!translate_pointer_button(button, mb)) return;

    const bool pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);

    // Any pointer-button press is a context event that kills an armed key
    // repeat (layer 1 of the repeat contract): the held key's stored intent
    // must not survive into what the click establishes (a new drag, selection,
    // or editor focus). Unconditional on the press edge, even when the
    // left-button logical-edge model swallows this event below — re-pressing
    // the held key re-arms cleanly.
    if (pressed) repeat_key_ = 0;

    // Left button rides the logical edge model shared with the kLeftClickKey
    // synthesized hold: deliver a press only on the 0->1 edge of
    // (pointer_left_held_ || synth_left_held_) and a release only on its 1->0
    // edge, so a physical press during a synthesized hold (or vice versa)
    // never double-delivers. Non-left buttons never participate in the OR and
    // are delivered unchanged.
    //
    // The held bit crosses its transition on the correct side of the flush so
    // the flushed motion always observes the PRE-RELEASE / POST-PRESS held
    // state: on a press the bit is set first, so the flush (and the press it
    // precedes) runs with the button reading held; on a release the flush runs
    // FIRST, while the button still reads held, and only then does the bit
    // clear. This is what makes the two release/motion delivery orders in one
    // pointer frame converge — a flushed threshold-crossing motion routes
    // through the normal live-drag path (threshold, moved latch, apply) instead
    // of the button-lost teardown, so an unmoved-until-now fast drag commits and
    // a fast trim-bar click seeds its double-click candidate.
    if (button == BTN_LEFT) {
        const bool was_held =
            pointer_left_held_ || synth_left_held_ || touch_left_held_;
        const bool now_held = pressed || synth_left_held_ || touch_left_held_;
        if (now_held == was_held) {
            pointer_left_held_ = pressed;   // keep the source bit current
            return;                          // no logical edge — swallow
        }
        if (pressed) {
            // 0->1 edge: set the held bit, then flush (button reads held), then
            // deliver the press.
            pointer_left_held_ = true;
            if (on_button_press_) {
                flush_deferred_motion();
                on_button_press_(mb, pointer_x_, pointer_y_, current_mods());
            }
        } else {
            // 1->0 edge: flush FIRST (button still reads held), then clear the
            // bit, then deliver the release (which now reads button-up).
            if (on_button_release_) flush_deferred_motion();
            pointer_left_held_ = false;
            if (on_button_release_) {
                on_button_release_(mb, pointer_x_, pointer_y_, current_mods());
            }
        }
        return;
    }

    // Non-left buttons never participate in the logical-left OR.
    if (pressed) {
        if (on_button_press_) {
            flush_deferred_motion();
            on_button_press_(mb, pointer_x_, pointer_y_, current_mods());
        }
    } else {
        if (on_button_release_) {
            flush_deferred_motion();
            on_button_release_(mb, pointer_x_, pointer_y_, current_mods());
        }
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
    // The context key binds remainder within one continuous chord session; a
    // modifier-state change clears scroll_accum_ outright at the modifiers
    // event (and at keyboard leave / capability loss), so remainder can never
    // bridge a chord release — the routing differs by chord (plain = the
    // stepped pan, Ctrl = the zoom step; every other combination no-ops), so
    // remainder grown under one must not complete a detent under another.
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
        // A wheel emission is a context event that kills an armed key repeat
        // (layer 1); mere sub-detent accumulation performs no action and does
        // not disarm.
        repeat_key_ = 0;
        on_wheel_(dir, steps, pointer_x_, pointer_y_, current_mods());
    }

    // Deliver the frame's coalesced captured motion, if any (see
    // on_relative_pointer_motion): one on_motion_ at the accumulated virtual
    // position, dropping the intervening sensor ticks. Fired after the wheel
    // drain, though a strip drag swallows wheels so the order is moot. A button
    // event in this same frame was ALREADY dispatched at its arrival (button
    // handlers are not deferred), and each button-delivery site first calls
    // flush_deferred_motion(), which delivers this pending motion and clears
    // frame_have_relmotion_. So a release that ends the drag has already seen
    // the motion and finalized before this point; the flag is then false here
    // and this block is a no-op. It fires only when the frame carried motion but
    // no button — the common case — refreshing hover / advancing the drag with
    // one delivery per frame.
    // TWO EDGES ALSO EMPTY THE STAGE BEFORE THIS BLOCK CAN RUN, both because
    // this delivery would otherwise land AFTER the leave hook that was supposed
    // to be the last word (2026-08-08): wl_pointer.leave FLUSHES above its hook
    // — the frame terminating that leave reaches this block, and a delivery here
    // would set `pointer_in_window` back to true with no second hook to undo it
    // — and the pointer-capability teardown DROPS the flag in its frame-scratch
    // reset, the relative-pointer object being gone by then. Each states its own
    // reason for flushing rather than dropping, or the reverse.
    // Uncaptured absolute motion is untouched (delivered live in
    // on_pointer_motion; compositors already pace it at frame cadence).
    if (frame_have_relmotion_ && on_motion_) {
        on_motion_(pointer_x_, pointer_y_, current_mods());
    }

    // Reset the per-frame scratch unconditionally — including motion-only
    // frames where no axis events arrived — so no partial delta leaks into
    // a later frame. scroll_accum_ is the only cross-frame carry.
    frame_v120_accum_ = 0.0;
    frame_axis_accum_ = 0.0;
    frame_have_v120_  = false;
    frame_have_axis_  = false;
    frame_have_relmotion_ = false;
}
// ---------------------------------------------------------------------------
// Touch event handlers (wl_touch as the pointer; touch phase 1, 2026-08-11;
// the WINDOWED MODEL, restored at the sixth glass ruling 2026-08-12 and
// TWO-DEADLINE since the eighth, the same day)
//
// The phase machine, the translation contract and the AUTHORITATIVE edge
// inventory live at the touch state block in platform_wayland.h; each body
// below states only its own clause. The GUI sees ordinary pointer deliveries
// (one finger), the touch-nav hooks (the pan and the pinch) or the region
// hooks (the hold) and nothing else.
// ---------------------------------------------------------------------------

void GuiPlatform::maybe_resolve_touch_window() {
    if (touch_phase_ != TouchPhase::Pending) return;
    if (monotonic_us() < touch_window_deadline_us_) return;
    // The window EXPIRED with one finger down — the deadline was the zone's
    // own (the two-deadline fork at the down site) and the expiry FORKS on
    // the same captured answer (the eighth glass ruling): ON the pan zone
    // the beat's expiry is THE REGION HOLD — the region former armed at the
    // down point, so hold-then-drag sweeps a region; OFF it the hold unlocks
    // the POINTER (hold-then-drag is the old pointer drag — what keeps the
    // endcap/bridge grabs, the flag drags and every off-zone press-and-hold
    // gesture alive on glass).
    if (touch_down_in_pan_zone_)
        resolve_touch_window_to_region();
    else
        resolve_touch_window_to_pointer();
}

void GuiPlatform::resolve_touch_window_to_pointer() {
    // Pending -> Pointer, delivering what the window withheld: the synthesized
    // entry motion at the ORIGINAL down point (the pointer enter's own shape —
    // the first "the pointer is here" notification), the left press there, and
    // any queued motion. Shared by all three pointer resolutions (the slop
    // crossing and the expiry both reach here only OUTSIDE the pan zone —
    // the crossing's pan-surface arm resolves to single-finger nav, the
    // phone model's fork at the Pending motion site, and the on-zone expiry
    // to the region hold, the eighth ruling's fork at
    // maybe_resolve_touch_window); the tap's
    // caller delivers the release and the focus-forked translation end itself
    // (deliver_touch_translation_end), immediately after.
    touch_phase_ = TouchPhase::Pointer;
    // THE MOVED LATCH SEEDS FROM THE WINDOW'S OWN TRAVEL (the sixth glass
    // ruling's second-down fork, 2026-08-12): a slop-crossing resolution
    // enters Pointer already MOVED — its condition is the latch's own
    // definition, Chebyshev >= kTouchSlopPx from the down point — while the
    // expiry and tap resolutions enter motionless (their drift is sub-slop
    // by construction). The Pointer motion arm latches it afterward.
    touch_translation_moved_ =
        std::max(std::abs(touch_last_x_ - touch_down_x_),
                 std::abs(touch_last_y_ - touch_down_y_)) >= kTouchSlopPx;
    // THE HOLD BIT GOES UP BEFORE THE ENTRY MOTION (codex round 2): the finger
    // has factually been down since the window opened, so EVERY delivery in
    // this burst — the entry motion included — reads primary_button_held
    // through current_mods(). That is the state the GUI's armed hover-open
    // guard reads (on_motion's no-gesture tail refuses the menu-row hover-open
    // under a held primary button): with the bit raised only after the entry
    // motion, that pre-press motion read UNHELD, hover-opened an armed
    // anchor's menu, and the press in the same burst toggle-closed it — a tap
    // on Settings/Navigation with the row armed visibly did nothing. The
    // was_held capture reads the two SIBLING sources only, so its value is
    // order-independent.
    const bool was_held = pointer_left_held_ || synth_left_held_;
    touch_left_held_ = true;
    // std::nearbyint at every delivery: touch positions are fractional on real
    // panels, and this is the project's one fractional->integer conversion.
    const int down_x = static_cast<int>(std::nearbyint(touch_down_x_));
    const int down_y = static_cast<int>(std::nearbyint(touch_down_y_));
    deliver_motion(down_x, down_y);
    // A pointer-button press is a context event that kills an armed key repeat
    // (layer 1 of the repeat contract), exactly as the physical BTN_LEFT and
    // the synthesized-`e` presses do at their own delivery sites.
    repeat_key_ = 0;
    // The logical left's OR-edge model, third source: the press is delivered
    // only on the 0->1 edge, with the bit already raised above so the press —
    // and the entry motion before it — reads held (the on_pointer_button
    // ordering, widened to the whole burst).
    if (!was_held && on_button_press_) {
        flush_deferred_motion();
        on_button_press_(GuiMouseButton::Left, down_x, down_y, current_mods());
    }
    // The queued motion: sub-slop drift for the expiry and tap resolutions,
    // the slop-crossing position itself for the motion one — either way the
    // finger's latest position, delivered after the press so a drag armed by
    // the press sees its first motion in the same burst.
    if (touch_window_moved_ &&
        (touch_last_x_ != touch_down_x_ || touch_last_y_ != touch_down_y_)) {
        deliver_motion(static_cast<int>(std::nearbyint(touch_last_x_)),
                       static_cast<int>(std::nearbyint(touch_last_y_)));
    }
    touch_window_moved_         = false;
    touch_frame_motion_pending_ = false;
}

void GuiPlatform::resolve_touch_window_to_single_nav() {
    // Pending -> Nav with ONE finger (the phone model's slop-crossing arm;
    // contract at the declaration): the finger drags the pan, and NOTHING
    // pointer-shaped starts — no entry motion, no press, the touch hold never
    // raised. The seed is the two-finger seed's own shape measured from the
    // DOWN point, unlatched with last_cx still holding the start, so the
    // first delivered frame runs deliver_touch_nav_frame's ordinary latch
    // test — the crossing position is already >= kTouchSlopPx away in the
    // same Chebyshev metric — and FOLDS the whole accumulated delta, exactly
    // as the two-finger latch folds. The distance fields stay 0.0: the pinch
    // latch arm is structurally false and the ratio guard delivers 1.0 (no
    // zoom from one finger); the GUI body's one fork on the finger count is
    // the pan-or-zoom fork itself — one finger pans, two fingers zoom.
    touch_phase_      = TouchPhase::Nav;
    touch_nav_single_ = true;
    touch_nav_id2_    = 0;
    touch_nav_x1_     = touch_last_x_;
    touch_nav_y1_     = touch_last_y_;
    touch_nav_x2_ = touch_nav_y2_ = 0.0;
    touch_nav_start_cx_ = touch_nav_last_cx_ = touch_down_x_;
    touch_nav_start_cy_ = touch_down_y_;
    touch_nav_start_dist_ = touch_nav_last_dist_ = 0.0;
    touch_nav_latched_     = false;
    touch_nav_delivered_   = false;
    // The crossing motion is staged for the frame boundary, the Nav cadence.
    touch_nav_frame_dirty_ = true;
    touch_window_moved_    = false;
}

void GuiPlatform::resolve_touch_window_to_region() {
    // Pending -> Region (the eighth glass ruling's on-zone expiry; contract
    // at the declaration): the hold resolved on the pan zone at the
    // kTouchRegionHoldMs beat, so the finger now DRIVES THE REGION FORMER
    // through the region hooks and NOTHING pointer-shaped starts — no entry
    // motion, no press, the touch hold never raised (the single-nav model,
    // not the Pointer one). The begin fires at the DOWN point — the former's
    // press half runs there (deselect, playhead seat, the drag arm), so the
    // span anchors where the finger landed — and any sub-slop drift that
    // arrived inside the window is the gesture's own first leg, staged for
    // the frame boundary exactly as the pointer resolution replays its
    // queued motion.
    touch_phase_ = TouchPhase::Region;
    if (touch_region_begin_hook_)
        touch_region_begin_hook_(
            static_cast<int>(std::nearbyint(touch_down_x_)),
            static_cast<int>(std::nearbyint(touch_down_y_)));
    touch_region_frame_dirty_ =
        touch_window_moved_ &&
        (touch_last_x_ != touch_down_x_ || touch_last_y_ != touch_down_y_);
    touch_window_moved_ = false;
}

void GuiPlatform::flush_touch_frame_motion() {
    // The touch twin of flush_deferred_motion: Pointer-phase motion is
    // coalesced to the wl_touch.frame boundary, and a button delivery in the
    // same frame must see the latest position first (delivered while the hold
    // still reads held, so the motion takes the live-drag path).
    // THE INVARIANT, stated once here (the flush owner): the staged motion is
    // the FINGER's own and is independent of the logical-left OR's edge, so
    // every end of the touch translation FLUSHES it rather than clearing it —
    // end_touch_left_hold flushes UNCONDITIONALLY, ahead of its release
    // decision, because a sibling source (physical BTN_LEFT / bare-`e`) still
    // holding suppresses only the RELEASE, never the motion. The only bare
    // clears of touch_frame_motion_pending_ are resets of state already
    // flushed or never deliverable: the resolution's replay tail (Pending
    // never stages this flag) and forget_touch_state (which runs after the
    // hard-end contract has already ended the hold through the flush here).
    if (touch_frame_motion_pending_) {
        deliver_motion(static_cast<int>(std::nearbyint(touch_last_x_)),
                       static_cast<int>(std::nearbyint(touch_last_y_)));
    }
    touch_frame_motion_pending_ = false;
}

bool GuiPlatform::end_touch_left_hold() {
    if (!touch_left_held_) return false;
    // The staged TOUCH motion flushes UNCONDITIONALLY, before the release
    // decision and while this bit still reads held: the finger's final
    // position is owed to on_motion whatever the logical-left OR says,
    // because a sibling source (physical BTN_LEFT / bare-`e`) staying held
    // suppresses only the RELEASE edge, never the motion (the invariant at
    // flush_touch_frame_motion, the flush owner). Gating this on the edge
    // made a shared drag silently stop short of the finger whenever the
    // mouse kept the OR true through the finger's last frame.
    flush_touch_frame_motion();
    // The end_left_hold_source ordering for the third source: deliver only on
    // the logical 1->0 edge (neither sibling source held); the deferred
    // POINTER motion flushes ahead of the delivery it exists for; then clear;
    // then deliver at the owner's last position.
    const bool deliver_release =
        !pointer_left_held_ && !synth_left_held_ && on_button_release_;
    if (deliver_release) flush_deferred_motion();
    touch_left_held_ = false;
    if (deliver_release) {
        on_button_release_(GuiMouseButton::Left,
                           static_cast<int>(std::nearbyint(touch_last_x_)),
                           static_cast<int>(std::nearbyint(touch_last_y_)),
                           current_mods());
    }
    // The return is the END'S OWN EDGE (codex round 2): both callers end the
    // translation through deliver_touch_translation_end, which acts iff the
    // release was delivered — a sibling-suppressed release means the unified
    // pointer has NOT left (the mouse is still there, mid-press), and the
    // leave hook's clears belong to that still-held press (the row-8
    // held-arrow repeat, the popup's press claim), not to the finger that
    // lifted.
    return deliver_release;
}

void GuiPlatform::deliver_touch_translation_end() {
    // The release's own edge first (codex round 2): a sibling-held logical
    // left suppressed the release, and then NOTHING below fires either — the
    // mouse is mid-press, and neither a leave nor a restore motion is this
    // stream's to deliver. A suppressed end can strand a hover face where the
    // finger last was — the accepted-glitch class, self-healing on the next
    // pointer event.
    if (!end_touch_left_hold()) return;
    // THE END FORKS ON PHYSICAL POINTER FOCUS (codex round 3, the
    // cursor-residue fix): a resolved touch drives the GUI's remembered
    // position to the finger, and the loop-settled cursor owner applies the
    // finger zone's kind to the REAL wl_pointer — so before this fork a mouse
    // resting in the window kept the finger's cue (Arrow/resize over a
    // Pan zone, say) until its own next motion. The finger lifting means
    // the unified pointer is now wherever the MOUSE is:
    //   * physical pointer FOCUSED (wl_pointer enter/leave, which touch never
    //     writes) — synthesize an ordinary MOTION at its last
    //     platform-tracked position (pointer_x_/pointer_y_, wl_pointer's own
    //     fields under the recorded split; after a touch-armed capture the
    //     release already rewrote them to the warp-restore position, so the
    //     value is the cursor's honest whereabouts) INSTEAD of the leave: the
    //     motion re-derives hover, the menu-row mode's own exit test and the
    //     settled cursor from truth. It runs the ordinary motion path WHOLE —
    //     deliberately unmarked, so every consequence of the pointer standing
    //     at the mouse's resting spot is the standing rules' own: with the
    //     menu row ARMED and the mouse resting on an anchor, the hover-open
    //     opens that menu; with a menu OPEN and the mouse resting on the
    //     OTHER anchor, the hover switch switches to it. Those are the armed
    //     mode's defining semantics for a pointer at that position, not
    //     surprises to suppress — suppression would need a "restore" mark on
    //     the motion, a device branch in spirit (judgment recorded here and
    //     in touch.md).
    //   * physical pointer NOT focused — the ordinary leave, as before: no
    //     mouse rests in the window, so the finger's lift IS the pointer
    //     leaving.
    // The mods are current_mods() at delivery: the touch bit is already down
    // and the delivering edge means neither sibling holds, so the motion
    // honestly reads unheld — an ordinary resting motion.
    // THIS IS THE ONE DELIBERATE TOUCH-SIDE READER OF pointer_focused_ (the
    // state block's recorded split): it asks a MOUSE question — "is the mouse
    // resting in the window" — and gates no touch delivery.
    if (pointer_focused_) {
        // The mouse's own resting position again, which is also what takes the
        // notional position back off the finger (deliver_motion's contract).
        deliver_motion(pointer_x_, pointer_y_);
        return;
    }
    if (pointer_left_hook_)
        pointer_left_hook_(GuiPointerLeaveReason::OrdinaryLeave);
}

void GuiPlatform::on_touch_down(uint32_t /*serial*/, uint32_t /*time*/,
                                int32_t id, int32_t fx, int32_t fy) {
    // An event past the deadline sees the resolved phase (Pointer).
    maybe_resolve_touch_window();
    ++touch_point_count_;
    const double x = wl_fixed_to_double(fx);
    const double y = wl_fixed_to_double(fy);
    switch (touch_phase_) {
        case TouchPhase::Idle:
            // The FIRST finger opens the disambiguation window: remember
            // {id, position, deadline} and deliver NOTHING — the window exists
            // only to tell one finger from two (the constants above own the
            // tuning rationale).
            touch_phase_    = TouchPhase::Pending;
            touch_owner_id_ = id;
            touch_down_x_ = touch_last_x_ = x;
            touch_down_y_ = touch_last_y_ = y;
            touch_window_moved_ = false;
            // The PAN-ZONE answer is captured ONCE, here at the down (the
            // phone model): the window's slop-crossing resolution AND its
            // expiry fork on it. Surface geometry only, by the query's
            // contract; nearbyint is the
            // one fractional->integer rule. Null hook = no pan surface.
            touch_down_in_pan_zone_ =
                touch_pan_zone_hook_ &&
                touch_pan_zone_hook_(static_cast<int>(std::nearbyint(x)),
                                     static_cast<int>(std::nearbyint(y)));
            // THE TWO-DEADLINE FORK (the eighth glass ruling, 2026-08-12 —
            // the dead trim-band beat's pattern reborn): ON the zone the
            // window runs to the REGION-HOLD beat, 500 ms; OFF it the 60 ms
            // disambiguation window as before. The arithmetic at this site:
            // on the zone a tap still lifts long before 500 ms and delivers
            // whole at the lift, a drag still crosses the 8 px slop into the
            // pan within the first frames, so the stretch costs neither —
            // only the deliberate motionless hold ever reaches the beat.
            // Monotonic, not the event
            // timestamp (whose base this program never compares against).
            touch_window_deadline_us_ =
                monotonic_us() +
                static_cast<uint64_t>(touch_down_in_pan_zone_
                                          ? kTouchRegionHoldMs
                                          : kTouchDisambiguateMs) * 1000ull;
            break;
        case TouchPhase::Pending: {
            if (id == touch_owner_id_) break;  // protocol nonsense; ignore
            // A SECOND finger inside the window: the two-finger navigation
            // gesture, and no press was ever delivered — nothing to unwind,
            // which is the window's whole purpose (the jump-free pinch).
            // The pair is seeded from the
            // owner's latest position and the new point; the latch reference
            // and the per-frame delta basis both start here. touch_nav_single_
            // is set EXPLICITLY at every Nav entry (here, the single-finger
            // resolve, and both upgrades): a normal nav end does not run the
            // one forget, so a stale flag from a finished single-finger pan
            // would otherwise leak into the next gesture.
            touch_phase_      = TouchPhase::Nav;
            touch_nav_single_ = false;
            touch_nav_id2_    = id;
            touch_nav_x1_  = touch_last_x_;
            touch_nav_y1_  = touch_last_y_;
            touch_nav_x2_  = x;
            touch_nav_y2_  = y;
            const double cx = 0.5 * (touch_nav_x1_ + touch_nav_x2_);
            const double cy = 0.5 * (touch_nav_y1_ + touch_nav_y2_);
            const double d  = std::hypot(touch_nav_x2_ - touch_nav_x1_,
                                         touch_nav_y2_ - touch_nav_y1_);
            touch_nav_start_cx_   = touch_nav_last_cx_   = cx;
            touch_nav_start_cy_   = cy;
            touch_nav_start_dist_ = touch_nav_last_dist_ = d;
            touch_nav_latched_     = false;
            touch_nav_delivered_   = false;
            touch_nav_frame_dirty_ = false;
            break;
        }
        case TouchPhase::Pointer: {
            if (id == touch_owner_id_) break;  // protocol nonsense; ignore
            // A SECOND finger during a live translation FORKS ON THE MOVED
            // LATCH (the sixth glass ruling, 2026-08-12 — the one piece of
            // the timer-free model kept when the window returned):
            //   * MOVED (a live drag — marker, region, trim, strip): IGNORED
            //     whole — recorded (the count above), not routed: mid-gesture
            //     finger-count changes do not mutate a committed gesture (the
            //     any-end-commits family; the architect's explicit mid-drag
            //     ruling).
            //   * MOTIONLESS (a hold): THE UPGRADE — the translation ends by
            //     ORDINARY RELEASE, the finger-up path's own shape through
            //     the one owner (staged motion flushed, release on the
            //     logical left's 1->0 edge, the focus-forked translation
            //     end; the sibling-suppression rule applies identically),
            //     and the two-finger gesture SEEDS AT THE JOIN: both
            //     fingers' current positions are the gesture start, the
            //     latch measured from there. The hold's press already landed
            //     at the window's expiry, so the upgrade adds NO further
            //     jump — it only keeps a slow pinch (fingers landing further
            //     apart than the window) alive instead of dead; a sub-latch
            //     release of that pair delivers nothing more.
            if (touch_translation_moved_) break;
            deliver_touch_translation_end();
            touch_phase_      = TouchPhase::Nav;
            touch_nav_single_ = false;
            touch_nav_id2_    = id;
            touch_nav_x1_  = touch_last_x_;
            touch_nav_y1_  = touch_last_y_;
            touch_nav_x2_  = x;
            touch_nav_y2_  = y;
            const double cx = 0.5 * (touch_nav_x1_ + touch_nav_x2_);
            const double cy = 0.5 * (touch_nav_y1_ + touch_nav_y2_);
            const double d  = std::hypot(touch_nav_x2_ - touch_nav_x1_,
                                         touch_nav_y2_ - touch_nav_y1_);
            touch_nav_start_cx_   = touch_nav_last_cx_   = cx;
            touch_nav_start_cy_   = cy;
            touch_nav_start_dist_ = touch_nav_last_dist_ = d;
            touch_nav_latched_     = false;
            touch_nav_delivered_   = false;
            touch_nav_frame_dirty_ = false;
            break;
        }
        case TouchPhase::Nav:
            if (touch_nav_single_ && id != touch_owner_id_) {
                // THE UPGRADE (the phone model): a second finger landing
                // during single-finger nav upgrades it to the two-finger
                // gesture IN PLACE — a transform, not an end (the end hook is
                // not owed here; the eventual end commits the whole stream).
                // The delta bases REBASE to the join: the centroid jumps from
                // the finger to the pair's midpoint, and folding that jump
                // would pan by half the finger gap, so last_cx takes the join
                // centroid and the distance basis starts at the join (zoom
                // relative to the join — the ruled semantics). The latch
                // state CARRIES: a live pan does not re-latch (freezing at
                // the join for another slop's travel would break the pan's
                // continuity), while a still-unlatched single nav (possible
                // only within the crossing's own frame batch) latches from
                // the join's start values below, the two-finger seed's own
                // shape. An undelivered staged single-finger frame is DROPPED
                // by the rebase — a sub-frame sliver the join supersedes
                // (the edge inventory's upgrade clause).
                touch_nav_single_ = false;
                touch_nav_id2_    = id;
                touch_nav_x2_     = x;
                touch_nav_y2_     = y;
                const double cx = 0.5 * (touch_nav_x1_ + touch_nav_x2_);
                const double cy = 0.5 * (touch_nav_y1_ + touch_nav_y2_);
                const double d  = std::hypot(touch_nav_x2_ - touch_nav_x1_,
                                             touch_nav_y2_ - touch_nav_y1_);
                touch_nav_start_cx_   = touch_nav_last_cx_   = cx;
                touch_nav_start_cy_   = cy;
                touch_nav_start_dist_ = touch_nav_last_dist_ = d;
                touch_nav_frame_dirty_ = false;
            }
            // Otherwise a third finger is ignored — recorded (the count
            // above), not routed: mid-gesture finger-count changes do not
            // mutate a committed gesture (the any-end-commits family).
            break;
        case TouchPhase::Region:
            // A second finger during the region gesture is IGNORED whole —
            // recorded (the count above), not routed: a committed gesture,
            // the moved-drag rule's family (the edge inventory's clause).
            break;
        case TouchPhase::Drain:  // fingers landing mid-drain are ignored
            break;
    }
}

void GuiPlatform::on_touch_up(uint32_t /*serial*/, uint32_t /*time*/,
                              int32_t id) {
    maybe_resolve_touch_window();
    if (touch_point_count_ > 0) --touch_point_count_;
    switch (touch_phase_) {
        case TouchPhase::Idle:
            break;
        case TouchPhase::Pending:
            if (id != touch_owner_id_) break;  // only the owner exists here
            // A TAP: the finger lifted inside the window, so the whole burst
            // delivers now — the resolution's enter-motion + press (+ any
            // queued sub-slop motion), then the Pointer arm below adds the
            // release and the focus-forked translation end immediately after.
            resolve_touch_window_to_pointer();
            [[fallthrough]];
        case TouchPhase::Pointer:
            if (id != touch_owner_id_) break;  // an ignored finger lifting
            // end_touch_left_hold flushes the staged motion unconditionally
            // (the invariant at flush_touch_frame_motion), so there is
            // nothing to clear here — a bare clear at this site would be the
            // swallow the flush owner forbids.
            // THE TRANSLATION END RIDES THE RELEASE'S OWN EDGE (codex round
            // 2), and since round 3 it FORKS on physical pointer focus — the
            // ordinary leave when no mouse rests in the window, a restore
            // MOTION at the mouse's own position when one does (the finger
            // lifted, so the unified pointer is where the mouse is). The
            // fork, the sibling-suppression rule (a still-held mouse press
            // suppresses release, leave and restore alike) and the rationale
            // live at deliver_touch_translation_end, the one owner both this
            // site and the hard end call.
            deliver_touch_translation_end();
            touch_phase_ = touch_point_count_ > 0 ? TouchPhase::Drain
                                                  : TouchPhase::Idle;
            break;
        case TouchPhase::Nav:
            // Single-finger nav: only the owner exists (the !single term is
            // the dormant-id2 guard, as at the motion arm).
            if (id != touch_owner_id_ &&
                (touch_nav_single_ || id != touch_nav_id2_))
                break;
            if (!touch_nav_single_) {
                // THE DOWNGRADE (architect 2026-08-14, the one-model ruling:
                // "add the zoom modifier at any time, drop it at any time" —
                // the second finger IS the zoom modifier, so lifting it
                // CONTINUES the gesture as the single-finger pan on the
                // survivor instead of ending it; the "upgrade yes, downgrade
                // no" asymmetry of 2026-08-11..14 is superseded, while the
                // upgrade's own justification — two fingers cannot land on
                // the same frame, so the upgrade is the pinch's only way in —
                // stands untouched). The upgrade's rebase in reverse: the
                // delta basis re-seats on the SURVIVOR'S current position, so
                // the next frame's dx measures from the survivor itself
                // rather than folding the centroid's half-gap jump off the
                // pair midpoint; the distance basis drops to the
                // single-finger degenerate 0.0 (the ratio guard then delivers
                // 1.0 — no zoom from one finger). THE LATCH STATE CARRIES —
                // the gesture is already live, and re-latching would freeze
                // the pan for another slop's travel — while a still-unlatched
                // pair (a sub-latch second lift) re-seats its latch reference
                // at the survivor, the upgrade's own unlatched shape
                // mirrored. Transitions repeat freely within one contact
                // stream — 1→2→1→2 through the upgrade and this — and a THIRD
                // finger stays ignored across a downgrade (it can join only
                // by lifting and pressing again, a fresh second-down).
                // NO RE-JOIN WINDOW, considered and RULED OUT (architect
                // 2026-08-14): a panel that momentarily drops a contact
                // mid-pinch would silently become a pan for that gap, and a
                // grace window re-admitting the returning finger was weighed
                // — "I'd test on the ROADOM, and then if it does occasionally
                // drop a finger, I'd still weigh how cumbersome that is
                // against the mitigation for a potentially non-existent
                // defect." Do not build one without field evidence.
                //
                // THE PAIR'S STAGED FRAME DELIVERS FIRST, and this is the ONE
                // place the downgrade DIVERGES from the upgrade it otherwise
                // mirrors. On the upgrade an undelivered single-finger sliver
                // is dropped because the join's new basis SUPERSEDES it — a
                // fragment of a mode that is over. Here the staged frame is
                // the PAIR'S OWN COMPLETED MOTION, measured between two
                // fingers that were both still down, and nothing supersedes
                // it: Wayland orders motion and EVERY up before the
                // wl_touch.frame that closes the batch, so when both fingers
                // lift together this site takes the first up with the pinch's
                // last — often only — motion still staged, and dropping it
                // left the second up calling end_touch_nav_gesture with
                // nothing to deliver (a short pinch then did nothing at all,
                // a long one lost its final zoom step). THE ORDERING: deliver
                // while the pair geometry is still intact — both positions
                // live and touch_nav_single_ still false, so the frame
                // carries the PAIR's centroid and distance ratio — and only
                // then rebase. The rebase cannot double-apply any of it,
                // because the delivery's own last_cx_/last_dist_ writes are
                // OVERWRITTEN below by the survivor's position and the
                // degenerate 0.0: the next single-finger frame measures dx
                // from the survivor itself and its ratio guard delivers 1.0.
                // A sub-latch pair delivers nothing here (the latch arm
                // returns), which leaves the still-unlatched re-seat below
                // exactly as it was.
                if (touch_nav_frame_dirty_) {
                    touch_nav_frame_dirty_ = false;
                    deliver_touch_nav_frame();
                }
                if (id == touch_owner_id_) {
                    touch_owner_id_ = touch_nav_id2_;
                    touch_nav_x1_   = touch_nav_x2_;
                    touch_nav_y1_   = touch_nav_y2_;
                }
                touch_nav_single_ = true;
                touch_nav_id2_    = 0;
                touch_nav_x2_ = touch_nav_y2_ = 0.0;
                touch_nav_start_cx_   = touch_nav_last_cx_   = touch_nav_x1_;
                touch_nav_start_cy_   = touch_nav_y1_;
                touch_nav_start_dist_ = touch_nav_last_dist_ = 0.0;
                break;
            }
            // The LAST nav finger lifting ENDS the gesture (any end commits).
            // The finger's own lift DELIVERS the staged dirty frame first (the
            // end split's finger-up clause, at end_touch_nav_gesture). No
            // release and no translation end: a nav gesture never held the
            // logical button (a nav born of the motionless-hold upgrade had
            // its translation released at the join).
            end_touch_nav_gesture(/*deliver_final_frame=*/true);
            touch_phase_ = touch_point_count_ > 0 ? TouchPhase::Drain
                                                  : TouchPhase::Idle;
            break;
        case TouchPhase::Region:
            if (id != touch_owner_id_) break;  // an ignored finger lifting
            // The owner's lift ends the region gesture: the staged dirty
            // frame delivers first (the user's own final leg — the nav
            // finger-up's model), then region_end (any end commits — the
            // former's release regime, at end_touch_region_gesture). No
            // release and no translation end: nothing pointer-shaped ever
            // started.
            end_touch_region_gesture(/*deliver_final_frame=*/true);
            touch_phase_ = touch_point_count_ > 0 ? TouchPhase::Drain
                                                  : TouchPhase::Idle;
            break;
        case TouchPhase::Drain:
            break;
    }
    if (touch_phase_ == TouchPhase::Drain && touch_point_count_ == 0)
        touch_phase_ = TouchPhase::Idle;
}

void GuiPlatform::on_touch_motion(uint32_t /*time*/, int32_t id,
                                  int32_t fx, int32_t fy) {
    maybe_resolve_touch_window();
    const double x = wl_fixed_to_double(fx);
    const double y = wl_fixed_to_double(fy);
    switch (touch_phase_) {
        case TouchPhase::Pending:
            if (id != touch_owner_id_) break;
            touch_last_x_ = x;
            touch_last_y_ = y;
            touch_window_moved_ = true;
            // Motion beyond the slop resolves the window EARLY — a finger
            // already dragging should not wait the window out — and FORKS on
            // the down point's captured pan-zone answer (the phone model):
            // on the pan surface the drag IS the pan (single-finger nav, no
            // press ever delivered); elsewhere the crossing
            // position is the queued motion the resolution replays, and at
            // kTouchSlopPx == the GUI's own drag gate it crosses that gate in
            // the same burst as the press (the constants' comment).
            if (std::max(std::abs(x - touch_down_x_),
                         std::abs(y - touch_down_y_)) >= kTouchSlopPx) {
                if (touch_down_in_pan_zone_)
                    resolve_touch_window_to_single_nav();
                else
                    resolve_touch_window_to_pointer();
            }
            break;
        case TouchPhase::Pointer:
            if (id != touch_owner_id_) break;   // ignored fingers stay ignored
            touch_last_x_ = x;
            touch_last_y_ = y;
            // THE MOVED LATCH (the sixth glass ruling's second-down fork):
            // once the finger has travelled the slop from its down point the
            // translation is a DRAG for the rest of its life — Chebyshev, the
            // resolver's own metric, latched once and never re-derived from a
            // later position (a drag wandering back near its down point is
            // still a drag).
            if (!touch_translation_moved_ &&
                std::max(std::abs(x - touch_down_x_),
                         std::abs(y - touch_down_y_)) >= kTouchSlopPx)
                touch_translation_moved_ = true;
            // Coalesced to the wl_touch.frame boundary — the pointer-frame
            // precedent: a panel can report at sensor rate, and the strip
            // drag's synchronous per-event repaint wants one delivery per
            // frame. Button deliveries flush this first.
            touch_frame_motion_pending_ = true;
            break;
        case TouchPhase::Nav:
            if (id == touch_owner_id_) {
                touch_nav_x1_ = x;
                touch_nav_y1_ = y;
            } else if (!touch_nav_single_ && id == touch_nav_id2_) {
                // The !single guard: id2 is 0 while one finger navigates, and
                // 0 is a real id on some compositors — protocol nonsense must
                // not write the dormant second-finger fields.
                touch_nav_x2_ = x;
                touch_nav_y2_ = y;
            } else {
                break;                          // a third finger's motion
            }
            touch_nav_frame_dirty_ = true;
            break;
        case TouchPhase::Region:
            if (id != touch_owner_id_) break;   // ignored fingers stay ignored
            touch_last_x_ = x;
            touch_last_y_ = y;
            // The Nav dirty-frame cadence: one region_update per
            // wl_touch.frame.
            touch_region_frame_dirty_ = true;
            break;
        case TouchPhase::Idle:
        case TouchPhase::Drain:
            break;
    }
}

void GuiPlatform::on_touch_frame() {
    // The per-frame drain, the wl_pointer.frame precedent: one motion delivery
    // (Pointer), one nav update (Nav) or one region update (Region) per
    // logical touch frame, whatever the sensor rate.
    maybe_resolve_touch_window();
    if (touch_phase_ == TouchPhase::Pointer) {
        flush_touch_frame_motion();
    } else if (touch_phase_ == TouchPhase::Nav && touch_nav_frame_dirty_) {
        touch_nav_frame_dirty_ = false;
        deliver_touch_nav_frame();
    } else if (touch_phase_ == TouchPhase::Region &&
               touch_region_frame_dirty_) {
        touch_region_frame_dirty_ = false;
        if (touch_region_update_hook_)
            touch_region_update_hook_(
                static_cast<int>(std::nearbyint(touch_last_x_)),
                static_cast<int>(std::nearbyint(touch_last_y_)));
    }
}

void GuiPlatform::deliver_touch_nav_frame() {
    // SINGLE-FINGER NAV (the phone model) forks only these three reads: the
    // finger is the centroid and the distance stays 0.0 — the pinch latch arm
    // below is then structurally false and the ratio guard delivers 1.0, so
    // one finger pans and cannot zoom. Everything downstream is shared.
    const double cx   = touch_nav_single_
                            ? touch_nav_x1_
                            : 0.5 * (touch_nav_x1_ + touch_nav_x2_);
    const double cy   = touch_nav_single_
                            ? touch_nav_y1_
                            : 0.5 * (touch_nav_y1_ + touch_nav_y2_);
    const double dist = touch_nav_single_
                            ? 0.0
                            : std::hypot(touch_nav_x2_ - touch_nav_x1_,
                                         touch_nav_y2_ - touch_nav_y1_);
    if (!touch_nav_latched_) {
        // The LATCH: nothing navigates until the centroid has travelled the
        // slop (Chebyshev, the drag gate's own metric) or the finger distance
        // has changed by it — so a two-finger tap navigates nothing (a
        // single-finger nav crosses on its first frame by construction: it
        // exists only by crossing the disambiguation slop, the same distance
        // in the same metric from the same down point). The
        // crossing folds the whole accumulated delta (the strip drag's own
        // crossing model): last_cx/last_dist still hold the gesture start.
        const bool travel =
            std::max(std::abs(cx - touch_nav_start_cx_),
                     std::abs(cy - touch_nav_start_cy_)) >= kTouchSlopPx;
        const bool pinch =
            std::abs(dist - touch_nav_start_dist_) >= kTouchSlopPx;
        if (!travel && !pinch) return;
        touch_nav_latched_ = true;
    }
    // A pair collapsed under 1 px on either end of the ratio zooms nothing
    // that frame — a degenerate distance has no ratio.
    constexpr double kMinNavDistPx = 1.0;
    const double ratio =
        (touch_nav_last_dist_ >= kMinNavDistPx && dist >= kMinNavDistPx)
            ? dist / touch_nav_last_dist_
            : 1.0;
    const double dx = cx - touch_nav_last_cx_;
    touch_nav_last_cx_   = cx;
    touch_nav_last_dist_ = dist;
    if (dx == 0.0 && ratio == 1.0) return;  // a no-op frame delivers nothing
    touch_nav_delivered_ = true;
    if (touch_nav_update_hook_) {
        GuiTouchNavFrame frame;
        frame.x          = static_cast<int>(std::nearbyint(cx));
        frame.y          = static_cast<int>(std::nearbyint(cy));
        frame.dx         = dx;
        frame.dist_ratio = ratio;
        // The finger count is the GUI's one fork (contract at
        // GuiTouchNavFrame, gui_input.h): it discards dx on a two-finger
        // frame — two fingers zoom and never pan — so this layer still
        // measures and delivers both deltas and applies no policy of its own.
        frame.two_finger = !touch_nav_single_;
        touch_nav_update_hook_(frame);
    }
}

void GuiPlatform::end_touch_nav_gesture(bool deliver_final_frame) {
    // THE END SPLIT (recorded here, at the one owner; each caller passes its
    // own clause and the edge inventory at the touch state block names both):
    // Wayland orders the terminating up/cancel BEFORE the wl_touch.frame that
    // closes its batch, so motion batched with the end is still staged in
    // touch_nav_frame_dirty_ when this runs.
    //   * FINGER-UP (true): the staged frame is the user's own FINAL MOTION
    //     and DELIVERS first — the any-end-commits family. This is what lets
    //     a short pinch whose only latch-crossing motion batches with the up
    //     act at all: with no prior update delivered, dropping that frame
    //     would erase the crossing, owe no end hook, and make the whole
    //     gesture silently do nothing.
    //   * HARD ENDS (wl_touch.cancel / touch-capability loss, false): the
    //     staged frame is DROPPED deliberately — the compositor's claim means
    //     that motion retroactively was not ours — and the end hook still
    //     fires iff an update was delivered. The asymmetry with the POINTER
    //     translation's hard end is deliberate: that release MUST deliver (a
    //     vanished hold would latch the drag-modal gate with no event left to
    //     lift it), while dropped nav motion wedges nothing — nothing the
    //     gesture leaves in the GUI is held OPEN. Its one GUI-side record
    //     since 2026-08-14 is the pinch's seated pivot (TouchNavZoomState),
    //     and it cannot survive a hard end: the seat exists only where a frame
    //     was DELIVERED (it is taken by the first two-finger frame that
    //     survives the GUI's refusal, applied or not), which is exactly the
    //     condition this end hook is owed on, and the hook clears it.
    if (deliver_final_frame && touch_nav_frame_dirty_) {
        touch_nav_frame_dirty_ = false;
        deliver_touch_nav_frame();
    }
    touch_nav_frame_dirty_ = false;
    // The end hook is owed a commit only if an update was ever delivered — a
    // sub-latch two-finger touch costs the GUI nothing at all.
    if (touch_nav_delivered_ && touch_nav_end_hook_) touch_nav_end_hook_();
    touch_nav_delivered_ = false;
    touch_nav_latched_   = false;
}

void GuiPlatform::end_touch_region_gesture(bool deliver_final_frame) {
    // The Nav end split restated for the region trio (the contract at the
    // declaration): the finger's own lift delivers the staged frame first —
    // the user's final leg, the span's resting extent — while the hard ends
    // drop it (the compositor's claim means that motion retroactively was
    // not ours). region_end then fires UNCONDITIONALLY, unlike the nav end's
    // delivered-gate: the GUI-side region drag has held the drag-modal gate
    // open since the begin, so its release path is owed even when no update
    // was ever delivered (a motionless hold-lift is the former's placement),
    // and a refused begin is covered by the end body's own !active guard.
    if (deliver_final_frame && touch_region_frame_dirty_) {
        touch_region_frame_dirty_ = false;
        if (touch_region_update_hook_)
            touch_region_update_hook_(
                static_cast<int>(std::nearbyint(touch_last_x_)),
                static_cast<int>(std::nearbyint(touch_last_y_)));
    }
    touch_region_frame_dirty_ = false;
    if (touch_region_end_hook_) touch_region_end_hook_();
}

void GuiPlatform::on_touch_cancel() {
    // The compositor claims the touches (its own gesture recognition, a grab).
    // One contract with touch-capability loss, in full.
    hard_end_touch_stream();
}

void GuiPlatform::hard_end_touch_stream() {
    switch (touch_phase_) {
        case TouchPhase::Pointer:
            // A live translation COMMITS — one delivered release on the
            // logical edge, at the last position (the pointer-capability-loss
            // precedent: a vanished hold would latch the drag-modal gate with
            // no event left to lift it) — then the translation end ON THE
            // RELEASE'S OWN EDGE, exactly as the finger's own lift delivers
            // it: the ordinary leave, or the focus-forked restore motion when
            // a mouse rests in the window (the round-3 fork), through the one
            // owner. A sibling-held logical left suppresses the release here
            // too, and the end with it — whatever happened to the glass, the
            // mouse is still there, mid-press, and its live press claim is
            // not this stream's to destroy.
            deliver_touch_translation_end();
            break;
        case TouchPhase::Nav:
            // A live nav gesture — single- or two-finger, one arm — ends
            // through its end path (commits iff anything was applied),
            // DROPPING its staged dirty frame — the end split's hard-end
            // clause, at end_touch_nav_gesture. No release, no leave: a nav
            // gesture never holds the logical button (a nav born of the
            // motionless-hold upgrade had its translation released at the
            // join; every other entry never delivered a press).
            end_touch_nav_gesture(/*deliver_final_frame=*/false);
            break;
        case TouchPhase::Region:
            // The Nav shape for the region trio — staged frame DROPPED, then
            // region_end, which ALWAYS fires (the GUI-side drag holds the
            // drag-modal gate open and its release path is owed; the end
            // split at end_touch_region_gesture). No release, no leave:
            // nothing pointer-shaped ever started.
            end_touch_region_gesture(/*deliver_final_frame=*/false);
            break;
        case TouchPhase::Pending:
            // Nothing was delivered, so there is nothing to end.
            break;
        case TouchPhase::Idle:
        case TouchPhase::Drain:
            break;
    }
    forget_touch_state();
}

void GuiPlatform::forget_touch_state() {
    touch_phase_       = TouchPhase::Idle;
    touch_point_count_ = 0;
    touch_owner_id_    = 0;
    touch_down_x_ = touch_down_y_ = 0.0;
    touch_last_x_ = touch_last_y_ = 0.0;
    touch_window_deadline_us_ = 0;
    touch_window_moved_       = false;
    touch_left_held_          = false;  // any delivering edge already ended it
    // A reset, not a swallow: every delivering end flushed this already (the
    // invariant at flush_touch_frame_motion); only never-delivered phases can
    // still reach here with it clear.
    touch_frame_motion_pending_ = false;
    touch_down_in_pan_zone_     = false;
    touch_region_frame_dirty_   = false;
    touch_translation_moved_    = false;
    touch_nav_single_ = false;
    touch_nav_id2_ = 0;
    touch_nav_x1_ = touch_nav_y1_ = 0.0;
    touch_nav_x2_ = touch_nav_y2_ = 0.0;
    touch_nav_start_cx_ = touch_nav_start_cy_ = touch_nav_start_dist_ = 0.0;
    touch_nav_last_cx_ = touch_nav_last_dist_ = 0.0;
    touch_nav_latched_ = touch_nav_delivered_ = touch_nav_frame_dirty_ = false;
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
    if (pointer_captured_) return;
    if (!pointer_constraints_ || !relative_pointer_ || !wl_pointer_ ||
        !wl_surface_)
        return;

    // Seed the TRAVEL LEDGER from the current absolute position and remember
    // THE ROW THE CURSOR IS ABOUT TO VANISH FROM (capture_restore_y_ — where it
    // reappears on release, always). Its restore x rides the notional
    // position, unless a strip drag overrides it with its anchor-stem column.
    // Each capture starts with no x override, so the grab-pan (no stem) falls
    // back to the notional x.
    // THE NOTIONAL POSITION IS NOT SEEDED HERE, and that is the shape rather
    // than an omission: it is live for the whole process and already holds the
    // pointer's position (the contract at notional_pointer_x_), so a capture
    // simply changes who advances it — the raw relative stream instead of the
    // deliveries. Seeding would be a second owner of the same fact.
    virtual_pointer_x_ = static_cast<double>(pointer_x_);
    virtual_pointer_y_ = static_cast<double>(pointer_y_);
    capture_restore_y_ = virtual_pointer_y_;
    capture_restore_x_override_.reset();
    // And with a DEGENERATE wrap span until the GUI supplies this capture's
    // own (set_capture_wrap_span, fired immediately after this call at both
    // capture sites): a capture that was never told a span must not wrap on
    // the previous one's numbers, and the degenerate span is simply skipped.
    capture_wrap_lo_ = 0.0;
    capture_wrap_hi_ = 0.0;
    // Every capture opens with the notional x LIVE. The nav drag asserts the
    // freeze immediately after this call when it crosses into a zoom phase, and
    // at every ctrl edge after that (contract at set_notional_x_frozen); the
    // overview lane's dual-axis strip drag never asserts it at all.
    notional_x_frozen_ = false;

    // Hide the cursor. set_cursor with a NULL surface is the protocol's "hide"
    // request; the tracked enter serial authorizes it. THE ONE set_cursor CALL
    // OUTSIDE apply_cursor_kind, and deliberately so: the hide is not a KIND, it
    // is the absence of one, and routing it through the owner would need a third
    // enumerator whose only job is to mean "no cursor". cursor_kind_ keeps
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
        // Creation failed: un-hide and stay uncaptured. pointer_captured_ is
        // still false here, so the applier's capture guard lets this through,
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
    pointer_captured_ = true;

    // STAMP THE KIND THE RELEASE WILL RESTORE, from the gesture's own identity
    // rather than from whatever the remembered kind happens to be right now.
    // WHY IT IS NEEDED: the cursor is re-derived once per RUN-LOOP ITERATION, and
    // the batch that delivered this press may have delivered, just above it, the
    // motion or the modifier edge that moved the answer — so the remembered kind
    // can still be a cue from before this gesture's zone was entered. The caller
    // knows what its gesture is; nothing here can work that out.
    // WHY IT IS WRITTEN DIRECTLY rather than through set_cursor_kind: this is not
    // the statement "the cursor should be X now" — the line above HID it — but
    // "X is what comes back". Going through the setter would make the stamp
    // depend on the two flags' ordering (it drops once the line below opens the
    // unknown span) and lean on the applier's capture guard to swallow the
    // protocol traffic. One assignment states the fact and owes neither.
    cursor_kind_ = restore_kind;

    // From here the GUI's pointer position is the virtual travel, so the kinds it
    // names are about a place the pointer is not: they stop being recorded until
    // the compositor next tells us where the pointer really is (the contract is at
    // the field). Set beside pointer_captured_ and only on the path that CREATED
    // THE LOCK PROXY — the degraded and creation-failed returns above leave it
    // false, which is what keeps those gestures on the ordinary cursor path. That
    // the proxy is taken for a live lock, without waiting for the asynchronous
    // `locked` event, is the ruling recorded at the header contract.
    pointer_position_unknown_ = true;
}

void GuiPlatform::end_pointer_capture() {
    release_pointer_lock(/*apply_restore_hint=*/true);
}

void GuiPlatform::set_capture_restore_x(double surface_x) {
    // The active zoom gesture names the surface x its anchor stem paints at;
    // the release restore uses it in place of the pointer's notional position.
    // Ignored when no capture is live (nothing to restore).
    if (!pointer_captured_) return;
    capture_restore_x_override_ = surface_x;
}

void GuiPlatform::clear_capture_restore_x() {
    // The nav drag's zoom→pan switch drops the stem override so the release
    // goes back to the notional x (contract at the declaration). Ignored
    // when no capture is live.
    if (!pointer_captured_) return;
    capture_restore_x_override_.reset();
}

void GuiPlatform::set_capture_restore_kind(GuiCursorKind kind) {
    // The mid-capture re-stamp of what the release restores (contract at the
    // declaration): the same direct write begin_pointer_capture's stamp uses
    // — "this is what comes back", not "show this now" (the cursor is hidden
    // for the capture's whole life, and pointer_position_unknown_ keeps
    // loop-tail kinds dropped) — guarded on a live capture so an uncaptured
    // caller cannot clobber the loop-tail owner's remembered kind.
    if (!pointer_captured_) return;
    cursor_kind_ = kind;
}

void GuiPlatform::set_notional_x_frozen(bool frozen) {
    // The gesture states its phase; this holds it (contract at the
    // declaration). Guarded on a live capture like its three siblings, which
    // is why the nav drag re-asserts at its threshold crossing: the ctrl edges
    // it took while the press was still sub-threshold had no capture to speak
    // to.
    if (!pointer_captured_) return;
    notional_x_frozen_ = frozen;
}

void GuiPlatform::set_notional_pointer_x(double surface_x) {
    // The gesture states where the pointer now is (contract at the
    // declaration). Guarded on a live capture like its four siblings — with no
    // capture the position is the delivery funnel's, and nothing may push a
    // gesture's idea of it in over the top.
    // THROUGH THE CLAMP BODY, never the field: the window clamp and the
    // ran-out verdict belong to the one owner. A stem column is interior, so
    // the verdict comes back false, which is exactly right — the position is
    // known again, so a later pan release follows the hand instead of going
    // home. NOT GATED BY notional_x_frozen_, and that is the class distinction
    // rather than an exemption: the freeze suppresses the relative stream's
    // ACCUMULATION of dx, while this states a position, as the release's own
    // write-back does (release_pointer_lock records the same split).
    if (!pointer_captured_) return;
    note_notional_pointer_x(surface_x);
}

void GuiPlatform::set_capture_wrap_span(double lo, double hi) {
    // The GUI states the waveform's span, and the fold is edge to edge inside
    // it (contract at the declaration). Capture-guarded like its siblings —
    // with no capture there is no wrap, the notional position then simply
    // being the delivery funnel's.
    if (!pointer_captured_) return;
    capture_wrap_lo_ = lo;
    capture_wrap_hi_ = hi;
}

void GuiPlatform::release_pointer_lock(bool apply_restore_hint) {
    if (!pointer_captured_ && !locked_pointer_) return;  // idempotent

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
            const double restore_x =
                capture_restore_x_override_.value_or(notional_pointer_x_);
            zwp_locked_pointer_v1_set_cursor_position_hint(
                locked_pointer_,
                wl_fixed_from_double(restore_x),
                wl_fixed_from_double(capture_restore_y_));
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
            // rode is deleted for good since 2026-08-12, the strip drag arming
            // from the ctrl-waveform press alone now; the exposure and this
            // fix are entry-agnostic): a ruler-band press armed the strip drag
            // (arm_strip_drag_at), a drag UP to zoom walked the virtual
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
            const double tracked_y =
                std::clamp(capture_restore_y_, 0.0, max_y);
            virtual_pointer_x_ = tracked_x;
            virtual_pointer_y_ = tracked_y;
            note_notional_pointer_x(tracked_x);
            pointer_x_ = static_cast<int>(std::nearbyint(tracked_x));
            pointer_y_ = static_cast<int>(std::nearbyint(tracked_y));
        }
        zwp_locked_pointer_v1_destroy(locked_pointer_);
        locked_pointer_ = nullptr;
    }
    pointer_captured_ = false;
    // The lateral freeze dies with the capture it belongs to, on BOTH exits
    // (the hint arm above and the revoked-lock path that skips it), so nothing
    // a zoom phase asserted can survive into the next gesture. Deliberately
    // below the write-back above: that write states where the cursor was
    // DRAWN, a real position rather than an accumulation, so the freeze never
    // gated it and the order carries no meaning either way.
    notional_x_frozen_ = false;

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
    // pointer_captured_ was cleared just above, so the applier's capture guard
    // admits this call. It is a no-op when the wl_pointer is already gone (a
    // pointer-capability loss releases it before this runs).
    // pointer_position_unknown_ deliberately STAYS SET past this point: the hint
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

void GuiPlatform::on_relative_pointer_motion(double dx, double dy) {
    // Consumed only while a capture is active; ignored otherwise (a relative
    // pointer exists for the process lifetime but only the strip drags lock).
    if (!pointer_captured_) return;

    // Advance the UNBOUNDED virtual position — no clamp to the window is what
    // makes pan/zoom travel infinite, every captured gesture differencing these
    // deliveries for its per-event delta — and write the nearbyint'd position into
    // pointer_x_/y_ so a button event dispatched later in the same frame already
    // sees the final coordinates. The on_motion_ DELIVERY is deferred to the
    // pointer-frame boundary (on_pointer_frame): a captured relative pointer
    // fires one event per sensor tick (500-1000 Hz mice), and the strip drag now
    // repaints synchronously per event, so delivering every tick would flood the
    // repaint. Coalescing to one on_motion_ per frame (the same model the wheel
    // uses) makes that synchronous repaint affordable. The gesture layer is
    // agnostic: it just sees coordinates that can now leave the window.
    //
    // The deferral is safe against a button in the SAME frame because every
    // button-delivery site calls flush_deferred_motion() first: the pending
    // motion is delivered before the button, so a fast press -> motion -> release
    // inside one frame cannot reach the release with the drag's motion still
    // undelivered (which would leave StripDragState.moved false and lose the
    // gesture).
    virtual_pointer_x_ += dx;
    virtual_pointer_y_ += dy;
    // THE NOTIONAL POSITION ADVANCES BY THE SAME DELTA AND IS CLAMPED HERE,
    // PER RAW EVENT (the field's contract at the declaration): clamping at the
    // accumulation is what leaves it with no off-window DEBT to unwind, so the
    // instant the hand reverses the position leaves the edge — clamping a
    // consumer instead cannot do that, because the debt is in the number it
    // reads. AND PER RAW EVENT IS LOAD-BEARING, not incidental: the deliveries
    // are COALESCED to one per pointer frame, and clamping once on a frame's
    // NET delta is a different answer at a wall (raw +20 then -8 at the right
    // edge is max-8 here and max there) — which is why nothing downstream may
    // keep a clamped position of its own to advance on the delivery cadence.
    // X only: the y axis has no position consumer (the declaration records
    // why).
    // AND IT DOES NOT ADVANCE AT ALL WHILE THE ACTIVE PHASE HAS FROZEN IT
    // (architect 2026-08-14: "we need to clamp to zero horizontal movement on
    // zoom") — the zoom phase's lateral hand travel moves nothing on screen,
    // so letting it move the pointer made the position a record of travel
    // nobody could see: the next ctrl-down seated the pivot far from where the
    // user believed the pointer was, and a zoom→pan switch's release restored
    // the cursor there too. The freeze is a POSITION rule and touches no delta
    // — the ledger above is unconditional, so the gesture's own travel, its
    // per-event dx/dy and every clamp are exactly as they were. The bit is the
    // GUI's to set; the contract is at set_notional_x_frozen. A frozen phase
    // therefore never wraps either: it writes no position at all.
    //
    // AND IT WRAPS EDGE TO EDGE RATHER THAN PINNING AT A BOUND (architect
    // 2026-08-14, from the rig: "what if instead we had the cursor, every time
    // that it touches the bounds, teleport back to the centre of the waveform?"
    // — and then, having driven that centre form, "make the wraparound a full
    // screen wraparound, not just the half width wraparound"). A crossing of
    // the RIGHT bound reappears at the LEFT one and a crossing of the left at
    // the right: the modular fold he had named as the alternative when he chose
    // the centre form one commit earlier. THE FOLD IS TWICE AS LONG, which is
    // the whole of the change — each crossing now buys the waveform's FULL
    // width of travel instead of half of it, so a pan of several screens folds
    // half as often and the cursor spends its time spread across the whole
    // surface rather than clustered around the middle. THE BOUNDS ARE THE
    // WAVEFORM'S, not the window's, on his own reasoning that this makes the
    // behaviour identical at every resolution; they are TOLD by the GUI
    // (set_capture_wrap_span), this class knowing nothing about a waveform.
    //   * CROSSING WRAPS, LANDING DOES NOT: a value exactly on lo or hi is
    //     inside and stays, so a hand that comes honestly to rest on the last
    //     pixel rests there. Only an event that would push the pointer PAST a
    //     bound folds it to the opposite one.
    //   * THE OVERSHOOT IS CARRIED, so no travel is lost — the pointer moves
    //     continuously through a folded space rather than being reset.
    //   * ONE APPLICATION SUFFICES and there is deliberately no loop: a single
    //     event's overshoot cannot realistically exceed the span, and
    //     note_notional_pointer_x's clamp is the backstop for a pathological
    //     delta that somehow did.
    //   * A DEGENERATE SPAN IS SKIPPED (hi <= lo — an unset span before any
    //     capture has supplied one, or a zero-width waveform); the clamp still
    //     answers.
    //   * A POSITION THAT IS ALREADY OUTSIDE THE SPAN when the capture opens
    //     folds in on its very first event, in either direction. The one way
    //     that happens is a pointer parked in the <=15px inert right gutter,
    //     which exists only at a window width that is not a multiple of 16 —
    //     neither host's. Recorded, not guarded: it is one event wide and it
    //     lands the pointer somewhere legitimate.
    // THE WRAP IS FREE BECAUSE THE CURSOR IS HIDDEN: Wayland gives a client no
    // pointer-warp request at all, so a VISIBLE cursor could never be moved by
    // us — the only position we may ever state is the locked pointer's release
    // hint — but while captured this position is our own bookkeeping entirely,
    // so folding it costs nothing, needs no protocol, and cannot be seen.
    // AND IT TOUCHES NOTHING ELSE, which is the property that makes it cheap:
    // every gesture's dx comes from the TRAVEL LEDGER above, which is unbounded
    // and untouched, so the wrap changes no view, no delta and no gesture
    // arithmetic anywhere. It moves only where the cursor is understood to be,
    // and therefore only where it reappears.
    // THE TRANSFORM IS THE CALLER'S AND THE STORE IS THE OWNER'S: this is the
    // one site that accumulates, so it is the one site where an overshoot
    // exists; note_notional_pointer_x stays the ONE clamp body for every
    // writer and never wraps a value it is handed.
    if (!notional_x_frozen_) {
        double nx = notional_pointer_x_ + dx;
        if (capture_wrap_hi_ > capture_wrap_lo_) {
            // The second arm's arithmetic is the mirror of the first, not a
            // sign error: below the low bound `nx - capture_wrap_lo_` is
            // NEGATIVE, so adding it to the HIGH bound walks inward from the
            // right edge by exactly the overshoot, just as the first arm walks
            // inward from the left edge by its own positive overshoot.
            if (nx > capture_wrap_hi_)
                nx = capture_wrap_lo_ + (nx - capture_wrap_hi_);
            else if (nx < capture_wrap_lo_)
                nx = capture_wrap_hi_ + (nx - capture_wrap_lo_);
        }
        note_notional_pointer_x(nx);
    }
    // std::nearbyint, the project's one fractional->integer pixel conversion —
    // this is the platform boundary where a continuous pointer position becomes
    // the integer window pixel every gesture reads.
    pointer_x_ = static_cast<int>(std::nearbyint(virtual_pointer_x_));
    pointer_y_ = static_cast<int>(std::nearbyint(virtual_pointer_y_));
    frame_have_relmotion_ = true;
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
    const uint64_t deadline_us = monotonic_us() + 1'000'000;
    bool failed = false;
    char buf[4096];
    for (;;) {
        const uint64_t now_us = monotonic_us();
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
void GuiPlatform::set_on_key(KeyCallback cb)                    { on_key_ = std::move(cb); }
void GuiPlatform::set_on_key_release(KeyReleaseCallback cb)     { on_key_release_ = std::move(cb); }
void GuiPlatform::set_on_button_press(ButtonCallback cb)        { on_button_press_ = std::move(cb); }
void GuiPlatform::set_on_button_release(ButtonCallback cb)      { on_button_release_ = std::move(cb); }
void GuiPlatform::set_on_wheel(WheelCallback cb)                { on_wheel_ = std::move(cb); }
void GuiPlatform::set_on_motion(MotionCallback cb)              { on_motion_ = std::move(cb); }
void GuiPlatform::set_on_close(CloseCallback cb)                { on_close_ = std::move(cb); }
void GuiPlatform::set_wheel_context_probe(WheelContextProbe cb)    { wheel_context_probe_ = std::move(cb); }
void GuiPlatform::set_text_editor_active_probe(TextEditorProbe cb) { text_editor_active_probe_ = std::move(cb); }
void GuiPlatform::set_repeat_eligible_probe(RepeatEligibleProbe cb) { repeat_eligible_probe_ = std::move(cb); }
void GuiPlatform::set_pointer_left_hook(std::function<void(GuiPointerLeaveReason)> cb) { pointer_left_hook_ = std::move(cb); }
void GuiPlatform::set_activation_changed_hook(std::function<void()> cb) { activation_changed_hook_ = std::move(cb); }
void GuiPlatform::set_keyboard_intent_cancel_hook(std::function<void()> cb) { keyboard_intent_cancel_hook_ = std::move(cb); }
void GuiPlatform::set_touch_nav_hooks(
    std::function<void(const GuiTouchNavFrame&)> update,
    std::function<void()> end,
    std::function<bool(int x, int y)> pan_zone,
    std::function<void(int x, int y)> region_begin,
    std::function<void(int x, int y)> region_update,
    std::function<void()> region_end) {
    touch_nav_update_hook_    = std::move(update);
    touch_nav_end_hook_       = std::move(end);
    touch_pan_zone_hook_      = std::move(pan_zone);
    touch_region_begin_hook_  = std::move(region_begin);
    touch_region_update_hook_ = std::move(region_update);
    touch_region_end_hook_    = std::move(region_end);
}
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
