#pragma once
#include "gui_input.h"
#include <cairo/cairo.h>
#include <cstdint>
#include <functional>
#include <optional>
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

// THE POINTER CURSOR HAS TWO KINDS AND EXACTLY TWO (architect 2026-08-03).
// Arrow is the system theme's left_ptr, the cursor everywhere in the window;
// Speaker is our own player-volume image, shown only while the pointer rests on
// the waveform's audition-scrub surface. There is no cursor-per-zone framework
// here and none is wanted: the speaker exists because that one zone runs a
// gesture the arrow cannot promise, and a second custom cursor would need its
// own ruling.
enum class GuiCursorKind {
    Arrow,
    Speaker,
};

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
    // printable keys. Consulted at PRESS time for kLeftClickKey to decide
    // whether it is its synthesized form (false) or a normal character (true):
    // while a text editor is open kLeftClickKey stays a normal letter.
    using TextEditorProbe      = std::function<bool()>;
    // Predicate installed by main.cpp: true when the pressed key+mods should
    // arm key repeat. Consulted at PRESS time (after the codepoint is filled)
    // so a press that opens an editor — evaluated before the open — does not
    // arm, while typing inside an already-open editor does.
    using RepeatEligibleProbe  = std::function<bool(GuiKey, GuiInputState)>;
    using TickCallback         = std::function<void()>;
    using PrePaintCallback     = std::function<void()>;

    GuiPlatform();
    ~GuiPlatform();

    bool init(int width, int height, const char* title);

    // THE WINDOW TITLE IS THE CLASSIC APPLICATION FORM (architect 2026-08-01):
    // "K551 - warptempo_gui" clean, "K551 * - warptempo_gui" with unsaved work.
    // These two setters are its ONLY writers — set_title itself is private below
    // so the composition cannot be bypassed by an inline string somewhere in the
    // GUI.
    //
    // set_project_title takes the source's PARENT FOLDER BASENAME (derived once
    // at load, file_loader.cpp — the folder is the project's name, distinct from
    // the audio filename and from the output `title=` settings key).
    // set_title_dirty is called at every dirty-state transition; it is a cheap
    // no-op when the flag has not moved, so the per-command recompute_dirty can
    // call it unconditionally.
    //
    // THE TITLE IS COMPOSITOR-SIDE TEXT: labwc shapes and paints the titlebar,
    // so the product's one-face HarfBuzz rule (which governs pixels WE paint)
    // does not reach here. The string is all-ASCII since the dirty mark became
    // an asterisk, so nothing depends on that latitude any more.
    void set_project_title(std::string project_name);
    void set_title_dirty(bool dirty);

    void shutdown();
    void run();
    void request_exit();
    void invalidate_region(int x, int y, int w, int h);
    void drain_events();
    void paint_now();

    // THE SYSTEM CLIPBOARD (the CLIPBOARD selection). PRIMARY is deliberately
    // absent — middle-click paste is out of scope and zwp_primary_selection is
    // not bound — and so is drag-and-drop, retired with the in-session file
    // load and not coming back; the wl_data_device listener's DnD slots are
    // inert stubs.
    //
    // THIS IS THE PAYLOAD'S ONE REPRESENTATION. The bytes handed to
    // clipboard_set_text are stored here (clipboard_send_text_) and nowhere
    // else: the store is not an optimization but a requirement, since the
    // compositor's `send` event arrives later and must be serviced without
    // reaching back into the GUI, and it is what the self-paste short circuit
    // answers with. AppState carried a second copy until 2026-08-02; it was
    // deleted as duplication with drift risk, so a copy site composes its
    // string, hands it over, and keeps nothing.
    //
    // clipboard_set_text claims the selection with `text` as the payload,
    // offered as text/plain;charset=utf-8 and text/plain.
    //
    // clipboard_get_text answers with that stored payload while WE hold
    // the selection (the self-paste short circuit — a same-thread
    // send-then-read over the pipe would deadlock), and otherwise performs a
    // bounded synchronous pipe read from the offering client. It returns the
    // EMPTY STRING when nothing text-shaped is on the clipboard and on every
    // failure (no acceptable mime, pipe error, timeout, runaway payload); a
    // partial read is never published. Callers treat empty as "nothing to
    // paste" — the bytes are not filtered here, because the editor's own
    // incoming filter (text_editor::replace_selection) is the boundary that
    // validates UTF-8 well-formedness and drops control bytes.
    void        clipboard_set_text(const std::string& text);
    std::string clipboard_get_text();

    int width()  const;
    int height() const;
    bool has_initial_configure() const { return has_initial_configure_; }

    // WINDOW ACTIVATION (keyboard focus), straight off xdg_toplevel.configure's
    // state array: true while the compositor lists XDG_TOPLEVEL_STATE_ACTIVATED.
    // The redesigned rows 1 and 2 darken their ground when it goes false, so the
    // header tracks the labwc titlebar above it (which darkens the same way).
    //
    // FALSE UNTIL THE FIRST CONFIGURE, which is the honest cold answer — we have
    // not been told we are active. labwc focuses a newly mapped window, so the
    // initial configure carries ACTIVATED and flips this before any frame is
    // painted; there is no unfocused flash to design around.
    bool window_activated() const { return window_activated_; }

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
    void set_repeat_eligible_probe(RepeatEligibleProbe cb);
    // Fired when the pointer LEAVES the surface (wl_pointer.leave) and at
    // pointer-capability loss — the two edges that drop pointer focus without a
    // motion event. The one owner of the hover-off-on-leave behavior: main.cpp
    // wires it to the REDESIGNED ROWS' button-face clears (hover, click, popup
    // press), so a pointer that slides out through the window edge cannot leave
    // a lit pill or a stranded pressed interior behind. It USED to clear the
    // marker hover popup as well; that whole surface died with the marker-text
    // lane in row 5, so the button faces are the only hover state left on this
    // edge. Null-safe.
    void set_pointer_left_hook(std::function<void()> cb);

    // Fired ONLY on a CHANGE of window_activated(), from the xdg_toplevel
    // configure handler. The compositor re-sends the full state array on every
    // configure (resize, maximize, focus), so the edge test lives in the
    // platform and this hook is the edge itself — main.cpp wires it to mirror
    // the flag into AppState and damage the top strip, the same shape the
    // pointer-leave hook takes for the same reason (a protocol edge with no
    // other event to carry its repaint). Null-safe.
    void set_activation_changed_hook(std::function<void()> cb);
    void set_on_tick(TickCallback cb);
    void set_on_pre_paint(PrePaintCallback cb);

    // GuiAsyncRenderer integration. The renderer creates its own eventfd
    // (it owns the lifetime) and registers it here; the run loop's poll set
    // grows a third pollfd watching for completion writes from the worker
    // thread. On POLLIN the loop reads the 8-byte counter to clear the fd
    // and then invokes the registered callback (which routes to
    // GuiAsyncRenderer::on_completion_event).
    void set_worker_completion_fd(int fd, std::function<void()> on_event);

    // Strip-drag pointer capture (Ableton-style): hide and lock the cursor at
    // the press position, feed subsequent relative-pointer motion into the
    // gesture as UNBOUNDED virtual coordinates so zoom travel is infinite.
    // Wired from main.cpp into the input handler's strip-drag begin/end hooks;
    // capture is SHARED by two gestures — the ctrl+waveform strip drag and
    // the alt-pan. On release the restore x
    // differs: the STRIP drag reappears the cursor at the anchor-stem column
    // (the capture_restore_x_override_ the GUI supplies via set_capture_restore_x
    // below), the alt-pan at the raw traveled virtual_pointer_x_; y is frozen at
    // the press row for both. Both degrade to a silent no-op when the
    // compositor advertises neither pointer-constraints nor relative-pointer
    // (the gesture then runs with clamped absolute motion, exactly as before).
    // begin is a guarded no-op when a capture is already active; end is
    // idempotent, so every gesture exit path (release, lost button, the force-end
    // finalizer — no cancel path exists) may call it unconditionally.
    void begin_pointer_capture();
    void end_pointer_capture();

    // (THE pointer_focused() ACCESSOR IS GONE — 2026-08-02. It answered "is the
    // pointer over our surface?" for the deferred-click completions, which read
    // it to decide between re-resolving the marker hover here and leaving the
    // clear to the pointer-left hook; row 5 deleted the marker hover and both
    // callers with it, and an accessor recorded as caller-less is still a
    // public surface nothing asks for. The FIELD stays — pointer_focused_ is
    // live inside this class, gating the synthesized-enter motion — so this
    // deletes the door, not the state behind it.)

    // Override the release-restore x for the active capture. The strip drags
    // set this each event to the surface x of their anchor stem, so the cursor
    // reappears dead on the stem's column (the edge-trick rebind pins the stem
    // while the raw cursor travel keeps going, so the raw virtual_pointer_x_
    // would land past it). begin_pointer_capture clears it, so a capture with
    // no override set (the alt-pan, which has no stem) restores at the raw
    // traveled virtual_pointer_x_.
    void set_capture_restore_x(double surface_x);

    // THE ONE DOOR TO THE CURSOR IMAGE. The GUI names the kind it wants for the
    // pointer's current position; this remembers it and applies it only on a
    // CHANGE, so the per-motion call an unmoving zone makes costs no protocol
    // traffic. The kind is REMEMBERED rather than passed because the platform
    // re-applies the cursor on its own edges — a pointer enter, and the end of a
    // pointer capture — and neither of those knows where the pointer is in the
    // GUI's terms; they just restore what was last asked for.
    //
    // WHILE A POINTER CAPTURE IS LIVE the cursor is HIDDEN, and this never
    // un-hides it: the kind is recorded and nothing is applied until the capture
    // releases (the guard is inside apply_cursor_kind, so every applier shares
    // it). A no-op when there is no wl_pointer.
    //
    // Speaker falls back to Arrow when the speaker image could not be built at
    // init; Arrow is itself the NULL-surface hide when the theme failed to load.
    // The two failures are independent — the speaker is our own buffer and owes
    // the theme nothing, so a themeless session still shows it.
    void set_cursor_kind(GuiCursorKind kind);

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

    // -- The window title, composed in one place --
    // set_title is the raw xdg_toplevel call and is PRIVATE so that the two
    // public setters above are the whole vocabulary: init() seeds the pre-load
    // fallback with it, apply_window_title() is the only other caller.
    void set_title(const std::string& title);
    void apply_window_title();
    std::string project_title_;          // the parent-folder basename; empty pre-load
    bool        title_dirty_ = false;

    // -- Wayland globals (bound during init()) --
    // THE PROTOCOL CLASSES, stated here once (init() enforces them): the five
    // below through wl_data_device_manager_ are REQUIRED — a missing one is a
    // startup failure. zxdg_decoration_manager_v1 joined that class 2026-07-30
    // and wl_data_device_manager (core protocol, the system clipboard)
    // 2026-08-02, both on the same reasoning (architect): labwc always
    // advertises them, so an absence is a broken environment rather than a
    // degraded one — running undecorated, or with copy and paste silently dead
    // in every text editor, is not a behavior anybody wanted. wl_output_ is
    // best-effort (absence falls back to a 60 Hz tick). The ruled OPTIONAL list
    // is exactly TWO, and both live in the pointer-capture block below.
    struct wl_display*    wl_display_     = nullptr;
    struct wl_registry*   wl_registry_    = nullptr;
    struct wl_compositor* wl_compositor_  = nullptr;
    struct wl_shm*        wl_shm_         = nullptr;
    struct xdg_wm_base*   xdg_wm_base_    = nullptr;
    struct zxdg_decoration_manager_v1* xdg_decoration_manager_ = nullptr;
    struct wl_data_device_manager*     wl_data_device_manager_ = nullptr;
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
    // Latest XDG_TOPLEVEL_STATE_ACTIVATED reading; see window_activated().
    bool window_activated_ = false;

    // True only while paint_one_frame is executing the pre-paint hook.
    // invalidate_region() consults this flag and skips its trailing
    // schedule_frame_callback() call when set, so the hook can declare
    // additional damage without producing a spurious extra commit.
    bool in_pre_paint_ = false;

    // True only while paint_one_frame is inside the on_redraw paint loop.
    // on_redraw may re-enter invalidate_region (the displayed-map promotion's
    // hover recompute), but that loop is iterating the current buffer's pending
    // list — the same list invalidate_region appends to — so a push_back there
    // would invalidate the range-for. While this flag is set, invalidate_region
    // holds the rect in deferred_damage_ instead; paint_one_frame replays the
    // held rects after the loop, so the damage lands on the next frame.
    bool in_redraw_ = false;
    std::vector<DamageRect> deferred_damage_;

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

    // -- The system clipboard (the CLIPBOARD selection) --
    // The device is created against the seat, so it is recreated when a seat
    // appears and torn down when one is removed; the manager above it is a
    // plain registry global with no seat dependency.
    struct wl_data_device* wl_data_device_ = nullptr;

    // A data_offer announcement is UNCLASSIFIED until the selection event that
    // follows it claims the object. It parks here meanwhile, in a slot distinct
    // from clipboard_offer_ so that an announcement can never destroy the live
    // clipboard offer out from under a paste. A drag-and-drop announcement
    // lands here too and is simply never claimed (the DnD listener slots are
    // inert): the next announcement supersedes and destroys it.
    // pending_offer_text_mime_ accumulates the best text mime the offer named.
    struct wl_data_offer* pending_data_offer_ = nullptr;
    std::string           pending_offer_text_mime_;

    // clipboard_offer_ is the current EXTERNAL selection offer (one we do not
    // own); null when the selection is empty or ours. clipboard_offer_text_mime_
    // is the EXACT token to hand back to wl_data_offer.receive — the offered
    // spelling, not a canonical one — preferring text/plain;charset=utf-8 (in
    // either charset casing) over bare text/plain, and empty when the offer
    // named no text mime at all, which is what makes a paste a silent no-op.
    struct wl_data_offer* clipboard_offer_ = nullptr;
    std::string           clipboard_offer_text_mime_;

    // clipboard_source_ is the wl_data_source we own while we hold the
    // selection; clipboard_send_text_ IS THE PAYLOAD, the program's one and
    // only copy of what it last put on the clipboard (no GUI-side twin — see
    // the public declaration). It is both the on-the-wire bytes written at each
    // `send` event, current as of the last clipboard_set_text, and the local
    // answer a self-paste reads, so a copy-then-paste inside this one process
    // never touches the pipe and cannot self-deadlock.
    // clipboard_we_own_ is true between a successful set_selection and the
    // `cancelled` event another client's claim produces. The payload SURVIVES
    // that cancellation deliberately: losing the selection does not erase what
    // we last copied.
    struct wl_data_source* clipboard_source_ = nullptr;
    std::string            clipboard_send_text_;
    bool                   clipboard_we_own_ = false;

    // Most recent serial from a keyboard event, required by
    // wl_data_device.set_selection. Cached in on_keyboard_key: every copy is a
    // Ctrl+C or Ctrl+X key event, so this is the triggering event's own serial
    // at set time. Cleared when the seat goes away.
    uint32_t               last_input_serial_ = 0;

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
    // SUPER (Logo) is tracked but NEVER projected into GuiInputState: it belongs
    // to labwc, and this program's answer to it is to deliver no key event at all
    // while it is held (the ruling is at deliver_key, platform_wayland.cpp). That
    // is why there is no `super` bool on GuiInputState and no reader anywhere.
    bool mod_super_ = false;

    // -- Pointer capture (pointer-constraints + relative-pointer) --
    // THE WHOLE OF THE RULED OPTIONAL LIST, both members, nothing else: null when
    // the compositor does not advertise them, and every capture entry point then
    // degrades to a silent no-op (strip drags run on clamped absolute motion,
    // announced by one stderr line at init). Any other protocol is required or
    // best-effort — see the globals block above.
    // relative_pointer_ is created once alongside wl_pointer_ and destroyed
    // with it; its motion events are consumed only while a capture is active.
    // locked_pointer_ is non-null only for the duration of a captured gesture.
    struct zwp_pointer_constraints_v1*      pointer_constraints_       = nullptr;
    struct zwp_relative_pointer_manager_v1* relative_pointer_manager_  = nullptr;
    struct zwp_relative_pointer_v1*         relative_pointer_          = nullptr;
    struct zwp_locked_pointer_v1*           locked_pointer_            = nullptr;

    // Virtual-pointer state, live only while pointer_captured_ is true. The
    // virtual position is seeded from the absolute press position and then
    // advances by each relative-motion delta WITHOUT clamping (unbounded
    // travel); its rounded value is written into pointer_x_/pointer_y_ and
    // delivered through on_motion_ exactly like an absolute motion. On release
    // the cursor reappears at capture_restore_x_override_ when a strip drag set
    // it (the anchor stem's surface x), else at the raw drag-traveled
    // virtual_pointer_x_ (the compositor clamps an off-window hint on-screen at
    // unlock); y is always frozen at the press row (capture_restore_y_).
    bool   pointer_captured_   = false;
    double virtual_pointer_x_  = 0.0;
    double virtual_pointer_y_  = 0.0;
    double capture_restore_y_  = 0.0;
    std::optional<double> capture_restore_x_override_;

    // Latest wl_pointer.enter serial. Tracked for wl_pointer.set_cursor: both of
    // that request's callers need a recent enter serial — apply_cursor_kind (the
    // one owner of the VISIBLE cursor, shared by the enter, the capture-lock
    // failure and the capture release) and the NULL-surface hide at capture
    // begin, which is the only set_cursor call outside that owner.
    uint32_t pointer_enter_serial_ = 0;

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

    // Per-frame captured-motion coalescing. While pointer_captured_, each
    // relative-motion event advances the virtual position (and pointer_x_/y_)
    // immediately but DEFERS its on_motion_ delivery, setting this flag;
    // on_pointer_frame() delivers exactly one on_motion_ at the accumulated
    // position when set, then clears it — collapsing a 500-1000 Hz capture
    // torrent to one gesture event per pointer frame so the strip drag's
    // synchronous per-event repaint runs at frame cadence. Reset with the
    // scroll scratch at the frame boundary.
    bool   frame_have_relmotion_ = false;

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

    // THE SCRUB CURSOR — our own image beside the theme's, built once at init
    // and kept for the process lifetime (build_speaker_cursor /
    // destroy_speaker_cursor). wl_pointer.set_cursor takes ANY wl_surface, so a
    // client cursor is just a surface with a buffer under it; this one carries
    // the Breeze speaker glyph drawn through icons::draw.
    //
    // Its shm pool is its OWN, not a slice of the window's: this one is sized
    // once for a single cursor image and never resized, which is exactly why
    // sharing the window pool (which is destroyed and rebuilt on every window
    // resize) would be wrong. The create/destroy pair follows recreate_shm_pool
    // / destroy_shm_pool as a pattern and shares no state with them.
    //
    // The whole group is null/-1 when the build failed; the cursor then simply
    // never changes (see set_cursor_kind).
    struct wl_surface*  speaker_cursor_surface_ = nullptr;
    struct wl_buffer*   speaker_cursor_buffer_  = nullptr;
    struct wl_shm_pool* speaker_cursor_pool_    = nullptr;
    int                 speaker_cursor_fd_      = -1;
    void*               speaker_cursor_map_     = nullptr;
    size_t              speaker_cursor_bytes_   = 0;
    int32_t             speaker_hotspot_x_      = 0;
    int32_t             speaker_hotspot_y_      = 0;

    // The kind last asked for. THE ONE PLACE the current cursor is recorded —
    // every applier reads it and none takes a kind as an argument, so a re-apply
    // on an enter or a capture release cannot restore a different cursor than
    // the one that was showing.
    GuiCursorKind cursor_kind_ = GuiCursorKind::Arrow;

    // Key repeat (last-key-wins, timerfd-tick-piggyback).
    // repeat_key_ is the GuiKey currently repeating (0 = none).
    // repeat_keycode_ is the raw xkb keycode of that key, used so the
    // wl_keyboard.key release event can match-and-cancel and so each
    // synthesized repeat recomputes its codepoint live from the xkb state.
    // repeat_due_us_ is the next-fire monotonic time in microseconds; when
    // the playback tick fires and current_monotonic_us >= repeat_due_us_,
    // the on_key_ callback fires and repeat_due_us_ is advanced by the
    // repeat interval.
    // repeat_delay_us_ is the compositor-advertised initial delay before
    // the first repeat (from wl_keyboard.repeat_info), in microseconds.
    // repeat_period_us_ is the compositor-advertised inter-repeat interval
    // (1_000_000 / rate), in microseconds.
    // Zero rate means "no repeat" and is honored — held keys do not repeat.
    // repeat_editor_ctx_ is the arm-time editor-active flag (from
    // text_editor_active_probe_); each fire re-checks that the live flag still
    // matches it. This boolean is sufficient because the editor target/session
    // can only change via a key or pointer-button event, and both kill the hold
    // outright — a key press re-arms or disarms via press-time eligibility, and
    // a pointer-button press or a completed wheel emission clears repeat_key_ at
    // the platform input chokepoints — so no armed hold can straddle a target
    // change.
    GuiKey        repeat_key_       = 0;
    uint32_t      repeat_keycode_   = 0;
    bool          repeat_editor_ctx_ = false;
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
    RepeatEligibleProbe  repeat_eligible_probe_;
    // The one owner of hover-off-on-pointer-leave: fired at wl_pointer.leave and
    // at pointer-capability loss (the two focus-dropping edges with no motion
    // event to re-resolve hover). Wired to the redesigned rows' face clears; the
    // marker hover popup it also dropped no longer exists. Null-safe.
    std::function<void()> pointer_left_hook_;
    // Fired at each window_activated_ EDGE (see set_activation_changed_hook).
    std::function<void()> activation_changed_hook_;
    TickCallback         on_tick_;
    PrePaintCallback     on_pre_paint_;

    // -- Internal helpers --
    void recreate_shm_pool(int w, int h);
    void destroy_shm_pool();
    bool load_cursor_theme();
    // Build the speaker cursor's pool, buffer, cairo drawing and surface. One
    // stderr line and false on any failure, leaving the group torn back down —
    // a degraded cursor, never a fatal one.
    bool build_speaker_cursor();
    void destroy_speaker_cursor();
    // THE ONE wl_pointer.set_cursor CALLER for the visible cursor: hands the
    // compositor the surface and hotspot cursor_kind_ names, at the tracked
    // enter serial. Silent no-op while a capture holds the cursor hidden, and
    // while there is no wl_pointer.
    void apply_cursor_kind();
    ShmBuffer* acquire_free_buffer();
    void schedule_frame_callback();
    void paint_one_frame();
    void destroy_wayland_state();
    int  detect_refresh_rate_ms();
    bool arm_playback_timer();

    // -- Clipboard helpers --
    // Create the wl_data_device once both the manager and the seat exist. Both
    // registry bind arms call it, so whichever is advertised second wins the
    // race; a seat that reappears after a registry removal recreates the
    // device. Idempotent.
    void ensure_data_device();
    void destroy_pending_offer();
    // THE ONE TEARDOWN for everything the seat-bound device owns — pending
    // offer, clipboard offer and its mime, our source and the ownership bit,
    // the device itself, the cached serial. Both exits call it (shutdown and
    // seat registry removal) so neither can grow its own partial copy: the
    // lifetime bugs this area had were exactly two teardown paths disagreeing
    // about which slots existed. Idempotent; leaves the manager alone, which
    // is not seat-bound.
    void destroy_data_device_state();
    // Bounded non-blocking read of a receive pipe to EOF. Empty on any
    // failure — see clipboard_get_text's contract.
    std::string read_clipboard_data(int read_fd);

    // -- Event handlers (called from file-static dispatchers) --
    void on_registry_global(struct wl_registry* r, uint32_t name,
                            const char* interface, uint32_t version);
    void on_registry_global_remove(uint32_t name);
    void on_output_mode(uint32_t flags, int32_t width, int32_t height,
                        int32_t refresh_mhz);
    void on_xdg_surface_configure(struct xdg_surface* xs, uint32_t serial);
    void on_toplevel_configure(int32_t width, int32_t height,
                               struct wl_array* states);
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
    // Deliver any captured relative motion that was deferred to the pointer-
    // frame boundary (see on_relative_pointer_motion) before a button event is
    // dispatched, so the button handler always sees the latest accumulated
    // position. Idempotent — a no-op when no deferred motion is pending; clears
    // the flag so on_pointer_frame's trailing delivery does not double-fire.
    // Every button-delivery site (the wl_pointer button listener, the
    // kLeftClickKey synth edges, and the keyboard-leave / capability-loss
    // synth releases) calls this immediately before delivering.
    void flush_deferred_motion();

    // Ends ONE source's contribution to the logical left hold (logical left =
    // pointer_left_held_ || synth_left_held_; see the OR-edge model). Delivers
    // the release only on the logical 1->0 edge — i.e. when the OTHER source is
    // not held — and encodes the ordering invariant ONCE: flush the deferred
    // motion FIRST, while this source's bit still reads held, so the flushed
    // motion observes the pre-release held state and takes the live-drag path,
    // not the button-lost teardown; then clear the bit; then deliver at the
    // last known pointer coordinates with current_mods(). When no edge occurs
    // (the other source still holds), just clear — the other source's later
    // release delivers the single edge. physical selects pointer_left_held_
    // (true) vs synth_left_held_ (false, which also clears synth_left_keycode_).
    void end_left_hold_source(bool physical);

    // -- Pointer-capture helpers --
    // Create relative_pointer_ once both wl_pointer_ and the manager exist
    // (called from both on_seat_capabilities and init(), whichever wins the
    // registry/seat ordering race); destroy it alongside the wl_pointer.
    void create_relative_pointer_if_ready();
    void destroy_relative_pointer();
    // Release a live lock: apply the restore-position hint (release path) or
    // skip it (compositor-revoked path), destroy the lock, restore the theme
    // cursor, and drop virtual mode. Idempotent.
    void release_pointer_lock(bool apply_restore_hint);
    void on_relative_pointer_motion(double dx, double dy);
    void on_locked_pointer_locked();
    void on_locked_pointer_unlocked();

    // -- Clipboard handlers --
    void on_data_offer(struct wl_data_offer* offer);
    void on_data_offer_mime_type(struct wl_data_offer* offer, const char* mime);
    void on_selection(struct wl_data_offer* offer);
    void on_data_source_send(struct wl_data_source* src,
                             const char* mime, int fd);
    void on_data_source_cancelled(struct wl_data_source* src);
};
