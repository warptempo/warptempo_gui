#pragma once
#include "device_config.h"
#include "gui_input.h"
#include "gui_media.h"
#include "input_core.h"
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
//
// THE INPUT POLICY IS NOT HERE. It lives in the ONE portable GuiInputCore this
// class holds (input_core.h): the touch translation, key-repeat synthesis, the
// logical pointer, the notional-x bookkeeping and the containment conversion.
// This class decodes protocol units, enums and keymap state on its own side and
// hands the core plain values; every input door below is the core's, forwarded
// so the GUI's consumers see one object. The contract for each such door is at
// the core's own declaration.

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

    bool init(int width, int height, const char* title);

    // THE DEVICE CONFIG'S FIRST-RUN TEMPLATE — the per-device preferences this
    // BACKEND is born with, stamped into
    // `$XDG_CONFIG_HOME/warptempo_gui/config` on the first launch that finds no
    // file there and never consulted again (device_config.h owns the file, its
    // schema and its five keys). It is a PLATFORM FACT and lives on the seam for
    // exactly that reason: the scale a panel wants and where on THIS device the
    // projects live are
    // answers only the backend has, and routing them through here is what keeps
    // the GUI proper free of the `#ifdef` the alternative would need. STATIC because it is asked before any window
    // exists — gui_main resolves the config ahead of init().
    static DeviceConfig device_config_defaults();

    // (THE ONE MOUNTED REMOVABLE VOLUME stood here from 2026-08-27 until
    // 2026-08-30 as `removable_volume()`, the seam's fourth per-backend
    // answer: Synchronize to external storage found its destination — the
    // udisks mount points under `/run/media/<user>/` here, the
    // `/storage/<name>` mount table lines on Android — rather than being told
    // it. THE DESTINATION IS CONFIGURED NOW, the device config's `sync_path`
    // key (device_config.h), so this member and both discoveries are DELETED:
    // where a machine mounts a stick is still a per-device fact, and the
    // per-device facts file is where it belongs. The finding rule worked here
    // and could not work on the tablet at all, that build mounting the OTG
    // stick invisibly to every app; the whole record is in
    // platform-seam.md's Synchronize section.)

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
    // Pump until an exit or a run stop is requested (the loop contract at
    // platform.h). Re-entrant across the process: the stop bit is cleared at
    // the head, the exit bit never is.
    void run();
    void request_exit();
    // THE REOPEN'S HALF OF THE PAIR: make run() return with the window
    // standing and the exit bit untouched, so gui_main's loop can rebuild the
    // object set and call run() again (platform.h). Nothing else stops.
    void request_run_stop();
    // Has an exit been requested or forced? Read by gui_main after run()
    // returns to tell the two returns apart.
    bool exit_requested() const { return should_exit_; }
    // Fire on_resize with the window's current geometry, for a freshly
    // installed object set whose window will send no configure of its own —
    // the reopen's case. A no-op before the first configure (no geometry yet;
    // that configure delivers it). One call, one fire.
    void redeliver_geometry();
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
    // Contract at GuiInputCore::set_on_key_release, input_core.h.
    void set_on_key_release(KeyReleaseCallback cb);
    void set_on_button_press(ButtonCallback cb);
    void set_on_button_release(ButtonCallback cb);
    void set_on_wheel(WheelCallback cb);
    void set_on_motion(MotionCallback cb);
    void set_on_close(CloseCallback cb);
    void set_wheel_context_probe(WheelContextProbe cb);
    void set_text_editor_active_probe(TextEditorProbe cb);
    void set_repeat_eligible_probe(RepeatEligibleProbe cb);

    // THE COMPOSITOR'S ADVERTISED KEY-REPEAT INTERVAL, in milliseconds, or 0
    // when key repeat is disabled. Contract at
    // GuiInputCore::key_repeat_period_ms, input_core.h.
    int64_t key_repeat_period_ms() const;

    // Contract at GuiInputCore::set_pointer_left_hook, input_core.h.
    void set_pointer_left_hook(std::function<void(GuiPointerLeaveReason)> cb);

    // Fired ONLY on a CHANGE of window_activated(), from the xdg_toplevel
    // configure handler. The compositor re-sends the full state array on every
    // configure (resize, maximize, focus), so the edge test lives in the
    // platform and this hook is the edge itself — main.cpp wires it to mirror
    // the flag into AppState and damage the top strip, the same shape the
    // pointer-leave hook takes for the same reason (a protocol edge with no
    // other event to carry its repaint). Null-safe.
    // THE HOOK IS THE EDGE AND NOTHING ELSE, so main.cpp SEEDS its mirror from
    // window_activated() at the install: a reopened project's fresh AppState
    // would otherwise start unfocused under an already-activated window that
    // has no edge left to fire (the reason is at that site and at
    // AppState::window_activated).
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

    // Fired ONCE PER ITERATION of run()'s loop, at the TAIL of the body — below
    // the display dispatch, the tick and both worker completions, so it observes
    // the iteration's FULLY SETTLED state. That placement is the whole point: a
    // loop boundary is by definition after every write an iteration made, so a
    // consumer that derives an answer here needs no list of the writers it
    // depends on and no "call me after your state has settled" rule at each of
    // them. Null-free (seeded with a no-op), so the fire site needs no test.
    //
    // WHEN IT DOES NOT FIRE, and why: not on the EINTR `continue` (nothing was
    // dispatched, so nothing settled), not on any `break` (the loop is leaving,
    // and the two connection-loss breaks leave a display that cannot be talked
    // to at all), and not once should_exit_ or the run stop is set (the
    // iteration is the last one of this run — the objects the consumer reads
    // are still alive, main.cpp's outlive run(), but the frame it would
    // compute for is never presented, or is presented by the NEXT object
    // set). The condition is stated once, at the fire site.
    //
    // IT EXISTS FOR POINTER-DERIVED FACES WHOSE INPUTS CAN SETTLE WITH NO POINTER
    // EVENT UNDER THEM — one class, and the reason it is a class rather than a
    // single wiring: such a face cannot be maintained by pushes at the sites that
    // move its inputs, because that set is not enumerable and does not stay
    // enumerated. THE POINTER CURSOR WAS THE FIRST CONSUMER and is that cue's ONE
    // owner: the kind is derived from roughly ten independent facts (the pointer's
    // position, the modifiers, every gesture's state, the trim window, the layout,
    // read-only, the modal surfaces), and two review rounds each found a class the
    // previous per-site derivation had missed. THE OPEN DROPDOWN'S ITEM FACES ARE
    // THE SECOND (2026-08-03): their hit test reads PAINTER-PUBLISHED rects, which
    // are zero until the popup's first paint, so a pointer that stops moving
    // before that paint would otherwise have nothing lit and nothing armed with no
    // event left to fix it. WHAT IS WIRED HERE is enumerated in main.cpp's hook
    // body, which is the authoritative list — this contract deliberately keeps no
    // second copy.
    // IT CARRIES THE LIVE MODIFIER STATE, the same GuiInputState the pointer
    // callbacks are built from, because modifiers SELECT between cursor kinds
    // over the waveform and the platform is that fact's owner — the consumer
    // therefore never reaches back for it, and no second copy of the modifier
    // state exists to go stale. Super is deliberately not in it (absent from
    // GuiInputState; it gates key PRESS delivery instead, releases ungated).
    void set_loop_settled_hook(std::function<void(GuiInputState)> cb);

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
    // the grab-pan. On release the restore x
    // differs: the STRIP drag reappears the cursor at the anchor-stem column
    // (the capture_restore_x_override_ the GUI supplies via set_capture_restore_x
    // below), the grab-pan at the pointer's notional position
    // (notional_pointer_x_, which WRAPS edge to edge across the waveform rather
    // than pinning at a bound — the record is at that field); y is frozen at
    // the press row for both. Both
    // degrade to a silent no-op when the
    // compositor advertises neither pointer-constraints nor relative-pointer
    // (the gesture then runs with clamped absolute motion, exactly as before).
    // begin is a guarded no-op when a capture is already active; end is
    // idempotent, so every gesture exit path (release, lost button, the force-end
    // finalizer — no cancel path exists) may call it unconditionally.
    //
    // THE RESTORE KIND IS A PARAMETER BECAUSE THE GESTURE IS THE ONLY THING THAT
    // KNOWS IT. Under a lock the cursor is hidden and every kind the GUI names is
    // about a place the pointer is not, so the release hands back a REMEMBERED
    // kind (apply_cursor_kind) — and inferring that from whatever was remembered
    // at press time is exactly what cannot be relied on: the cursor is resolved
    // once per RUN-LOOP ITERATION, and a single dispatch batch can carry the
    // motion (or the modifier edge) that selects the gesture's zone AND the press
    // that begins the capture, with no re-derivation in between. So the caller
    // passes the cue its own gesture wears — Zoom for the strip drag (its one
    // entry is a Zoom zone), Pan for the grab-pan — the same "read the drag's own record"
    // the live trim cue uses, and the platform STAMPS it as the remembered kind on
    // the path that CREATED THE LOCK PROXY. Only there, and neither other exit
    // stamps or restores anything: a DEGRADED compositor returns before the
    // hide and never touches the cursor at all, while a FAILED PROXY CREATION
    // hides and immediately un-hides again (the hide is issued ahead of the
    // request — see below — and that arm's apply_cursor_kind puts back whatever
    // kind was showing, a transient pair the body states at the site). Both
    // keep their gesture running on real coordinates with the loop tail
    // deriving normally, so a stamp would be a cue nobody asked for.
    //
    // THE CREATED PROXY IS TREATED AS A LIVE LOCK, AND THAT IS OPTIMISTIC BY
    // RULING (architect 2026-08-03). Activation is ASYNCHRONOUS — the protocol's
    // zwp_locked_pointer_v1.locked event announces it, and our listener for that
    // event deliberately does nothing — so the stamp, pointer_captured_ and
    // the unknown span, which all follow the creation, ride the lock REQUEST
    // rather than a granted lock. THE HIDE IS THE SAME OPTIMISM ONE STEP
    // EARLIER: it is issued BEFORE the request, so the cursor is already gone
    // when the gesture's first relative motion arrives rather than a round trip
    // later, which is what makes the creation-failure arm an undo rather than
    // an omission.
    // On labwc, the supported compositor, a lock requested while our surface
    // holds pointer focus activates promptly, and
    // mid-press on our surface that focus is structurally ours; properly-coded
    // compositors are assumed equivalent. One that DEFERS or DECLINES the lock is
    // an unsupported environment — adversarial usage, the category a hand-broken
    // sidecar is in — and the pending/active split that would cover it was
    // rejected for costing the real target a visible-cursor round trip at every
    // capture start. WHAT WOULD DEGRADE THERE, recorded rather than defended: the
    // drag still runs on the relative stream, the cursor stays hidden over our
    // surface, and the release restores the stamped cue wherever the pointer
    // really is, corrected at the compositor's next absolute event.
    void begin_pointer_capture(GuiCursorKind restore_kind);
    void end_pointer_capture();

    // (THE pointer_focused() ACCESSOR IS GONE — 2026-08-02. It answered "is the
    // pointer over our surface?" for the deferred-click completions, which read
    // it to decide between re-resolving the marker hover here and leaving the
    // clear to the pointer-left hook; row 5 deleted the marker hover and both
    // callers with it, and an accessor recorded as caller-less is still a
    // public surface nothing asks for. The FIELD stays — pointer_focused_ is
    // live inside the input core, gating the synthesized-enter motion — so this
    // deletes the door, not the state behind it.)

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

    // THE POINTER'S NOTIONAL POSITION (surface x, px) — THE PRODUCT'S ONE
    // ANSWER TO "WHERE IS THE POINTER?". Contract at
    // GuiInputCore::notional_pointer_x, input_core.h.
    double notional_pointer_x() const;

    // THE ONE DOOR TO THE CURSOR IMAGE. The GUI names the kind it wants for the
    // pointer's current position; this remembers it and applies it only on a
    // CHANGE, so the once-per-loop-iteration call an unmoving answer makes costs
    // no protocol traffic. The kind is REMEMBERED rather than passed because the platform
    // re-applies the cursor on its own edges — a pointer enter, and the end of a
    // pointer capture — and neither of those knows where the pointer is in the
    // GUI's terms; they just restore what was last asked for.
    //
    // A KIND NAMED FOR A POSITION THE POINTER DOES NOT OCCUPY IS DROPPED, not
    // remembered (pointer_position_unknown_, whose contract is at the field). The
    // GUI resolves the cursor from ITS idea of the pointer position, and a capture
    // makes that idea virtual: motion keeps arriving through the lock as unbounded
    // relative travel, so the GUI keeps answering — every iteration of the run
    // loop, with the live-gesture Arrow and then from wherever the travel ended —
    // for a place the pointer is not.
    // Remembering those answers is what would make the restore a lie, since the
    // restore hands back the REMEMBERED kind: dropping them keeps the remembered
    // kind the last one derived from a real position OR THE KIND THE GESTURE
    // STAMPED at capture begin, whichever is later — and for a capture that
    // stamped (the lock-proxy path) it is always the stamp, which is the cue the
    // gesture wears and the cue the restore position sits in (the full statement
    // is at begin_pointer_capture's restore_kind parameter, including why a
    // created proxy is treated as a live lock; the drop keeps the stamp standing).
    // THE SPAN OUTLASTS THE LOCK by design, so a force-end that ends the capture
    // and then re-derives from the remembered (still virtual) coordinates is
    // dropped too, rather than replacing a stale cue with a confidently wrong one.
    // ITS COST, RECORDED: an answer that did NOT depend on the position — the
    // Arrow a prompt or an editor claims — is dropped in that span as well, so a
    // Ctrl+Q taken mid-capture shows the gesture's own cue over the prompt it
    // raises until the pointer next moves. That is the accepted-staleness class
    // the zone map already documents, and it is one mouse movement wide.
    //
    // WHILE A POINTER CAPTURE IS LIVE the cursor is HIDDEN, and this never
    // un-hides it: nothing is applied until the capture releases (the guard is
    // inside apply_cursor_kind, so every applier shares it — including the enter
    // that can arrive with a kind this method did admit). A no-op when there is no
    // wl_pointer.
    //
    // A kind whose xcursor name is missing from the theme falls back to Arrow,
    // and Arrow is itself the NULL-surface hide when no theme loaded at all.
    void set_cursor_kind(GuiCursorKind kind);

    // Parallel hookup for the GuiWaveformWorker's completion
    // eventfd. The poll set grows a fourth pollfd; on POLLIN the loop
    // reads the counter and invokes this callback (routes to
    // GuiWaveformWorker::on_completion_event).
    void set_waveform_worker_completion_fd(int fd, std::function<void()> on_event);

    // And the same hookup for the GuiHistoryCommitWorker's completion eventfd
    // (2026-08-07, the checkpoint act's move onto a background worker). The
    // poll set grows a fifth pollfd; on POLLIN the loop reads the counter and
    // invokes this callback (routes to
    // GuiHistoryCommitWorker::on_completion_event).
    void set_history_worker_completion_fd(int fd, std::function<void()> on_event);

    // And the SIXTH, the history walk's prefetch worker (2026-08-07). It is the
    // one whose eventfd is a READY signal rather than a completion: the scan
    // streams many results, the counter coalesces their signals, and the
    // callback DRAINS whatever has queued (routes to
    // GuiHistoryPrefetch::drain through the input handler's arrival hook). The
    // loop's read of the counter is unchanged — the value is not the count of
    // anything the callback needs.
    void set_history_prefetch_completion_fd(int fd,
                                            std::function<void()> on_event);

    // And the SEVENTH, the Synchronize to external storage act's worker
    // (2026-08-27). The poll set grows a seventh pollfd; on POLLIN the loop
    // reads the counter and invokes this callback (routes to
    // GuiExternalSyncWorker::on_completion_event).
    void set_sync_worker_completion_fd(int fd, std::function<void()> on_event);

    // -- THE ON-SCREEN KEYBOARD'S TWO SEAM MEMBERS (2026-08-27) ------------
    //
    // DOES THIS PLATFORM WANT THE GUI TO PAINT A KEYBOARD? Wayland answers
    // NO, permanently and by construction: the laptop has a physical keyboard
    // and the compositor delivers its keys, so a painted one would be a second
    // road onto the same key path. The GUI's keyboard surface is gated on this
    // ALONE at every paint and hit site it has (onscreen_keyboard.h), which is
    // what makes "the laptop build is behaviourally identical" a structural
    // fact rather than a promise: the surface's rect is empty here, its
    // painter returns at its head and its press claim never fires.
    //
    // It is a PLATFORM question and not a settings one deliberately. A backend
    // knows whether the machine it runs on has keys; a preference would let
    // the laptop grow a surface with no reason to exist, and the glass has no
    // choice to offer.
    bool wants_onscreen_keyboard() const;

    // A KEY EVENT FROM SOMETHING THAT IS NOT A PHYSICAL KEYBOARD — the road
    // the painted on-screen keyboard takes into the core's key path. THE
    // ARGUMENTS' CONTRACT IS THE CORE'S, at GuiInputCore::key_event
    // (input_core.h), and is not copied here.
    //
    // IT EXISTS ON THIS BACKEND FOR THE SEAM'S SAKE AND IS NEVER CALLED HERE:
    // the one caller is the keyboard's press router, which asks
    // wants_onscreen_keyboard() first and gets false on this platform. The
    // declaration is what lets that caller compile against either backend —
    // the seam's whole promise — and the forward below is the honest body for
    // it rather than a stub, since a synthesized key on THIS platform would
    // mean exactly what it means on the other one.
    void synthesize_key(GuiKey key, uint32_t stable_code, bool pressed,
                        uint32_t codepoint);

    // -- THE CAR'S TWO SEAM MEMBERS (architect design 2026-08-28 §3) --------
    //
    // A head unit's buttons reach the product over Bluetooth as media-button
    // events delivered to whichever app holds an ACTIVE media session, and
    // the head unit's display reads that session's metadata and playback
    // state back. Both are a PLATFORM SERVICE and not a GUI surface — the
    // session object, its callbacks, the audio focus and the thread they all
    // run on are the backend's — so the seam carries exactly two doors: a hook
    // the loop fires for each button the platform received, and a push the
    // GUI makes when what the display should show has changed. The
    // vocabulary is gui_media.h's; the consumer is the render player alone
    // (GuiRenderPlayer::on_media_command / publish_media_state), which holds
    // the platform for exactly these two calls.
    //
    // THE HOOK IS FIRED ON THE LOOP'S OWN THREAD, one call per command, in
    // arrival order, from the same pass that dispatches the worker
    // completions and before that pass's settled hook and paint — so a button
    // acts and its frame paints in one pass, and nothing the GUI holds needs a
    // lock. The platform receiving the button on another thread (Android's UI
    // thread) queues it and wakes the loop; that mechanism is the backend's,
    // stated at its definition. A command that arrives with no hook installed
    // is DROPPED by the platform's null test: main.cpp installs the hook per
    // project and clears it at the session tail, so a button pressed between
    // two projects reaches nothing.
    //
    // ON THIS BACKEND THE HOOK IS STORED AND NEVER FIRED — the seam's
    // STORED-HOOK SHAPE, which set_on_close carried on the ANDROID side until
    // BACK became its producer there (2026-08-29) and which this hook is the
    // seam's one remaining example of: the laptop has no media session and no
    // head unit, and its car keys are simply the keyboard. The setter stays because the seam's
    // promise is that a consumer compiles against either backend unchanged.
    void set_on_media_command(std::function<void(GuiMediaCommand)> cb);

    // THE STATE PUSH: what the head unit should show now. Called from the loop
    // thread only, at the edges its one owner inventories; never per tick
    // (the consuming side advances a playing position on its own clock from
    // the last push). ON THIS BACKEND IT IS A NO-OP BODY, which would be wrong
    // for a producer on this platform and is exactly right for the one it
    // has, which is none. Android's body is the JNI call up into the Java
    // sliver, which builds the session's metadata and playback state from it,
    // sets the session active or inactive, and requests / abandons audio
    // focus (MainActivity.mediaState).
    void publish_media_state(const GuiMediaState& state);

    // -- THE DISPLAY LEAD (architect 2026-09-02) -------------------------
    //
    // HOW LONG AFTER THE PRE-PAINT HOOK SAMPLES THE PLAYBACK PREDICTOR DO THE
    // PIXELS IT PAINTS TURN INTO LIGHT, in nanoseconds — the display's own
    // latency, the twin of the audio device's output latency the predictor
    // already compensates (playback_common.h). The consumer is main.cpp's
    // pre-paint hook, which hands the figure to the predictor once per painted
    // frame (GuiPlayback::set_display_lead_ns), and the predictor reads its
    // POSITION that far ahead of `now` — and nothing else: the natural-end
    // hold's deadline keeps the bare clock (the one read site is at `observe`,
    // playback_common.cpp). The sign is the whole point: a line drawn where
    // the sound IS at paint time reaches the panel one to two refresh periods
    // later and reads BEHIND the sound by that much (~33 ms at 60 Hz under
    // labwc, 13–33 px at the working zoom); drawn where the sound WILL BE when
    // the pixel lights, it reads on the sound.
    //
    // ON THIS BACKEND IT IS SELF-MEASURED through wp_presentation, the
    // compositor's presentation feedback (the globals block below): every
    // content commit carries a feedback request stamped with the instant the
    // pre-paint hook began, the compositor answers with the instant that
    // commit turned into light, and the figure is a 30-frame moving mean of
    // the difference (Kodi's window; the ring is at the state block). Until the
    // first sample arrives, where the global is absent, or where its clock is
    // not CLOCK_MONOTONIC (the predictor's clock — the two must share a domain),
    // it is the FALLBACK: 2 × the refresh period of THE OUTPUT THE WINDOW IS ON
    // (k = 2: wlroots repaints right after vblank and sends the frame callback
    // after that composite has run, so a commit made in the callback waits a
    // further period — sway's author measured "a little less than 2 frames",
    // and labwc has no max_render_time knob to shorten it), 60 Hz when no
    // output is known. The measured figure and the fallback are each announced
    // once on stderr beside the JACK latency line, and the mean again whenever
    // it moves by a millisecond, so the number is seen at every launch. Main
    // thread only; no on-screen surface, no settings key.
    //
    // ANDROID ANSWERS A CONSTANT 0, and that is a ruling rather than a gap
    // (the record is at that backend's body): the AAudio backend reports no
    // output latency by ruling — the car's Bluetooth route is large, variable
    // and unreported — so the tablet's predictor still carries the WHOLE
    // uncompensated audio lead, and a display lead added on top of it would
    // double-count against that. Record-only there until the tablet's audio
    // latency is itself compensated.
    int64_t display_lead_ns() const;

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
    // in every text editor, is not a behavior anybody wanted. EVERY wl_output
    // the registry names is bound, best-effort (`outputs_` below; no output
    // with a mode falls back to a 60 Hz tick). The ruled OPTIONAL list is
    // exactly THREE: the two pointer-capture managers in their block below,
    // and wp_presentation here (2026-09-02, the display lead's measuring
    // instrument — the sixth bound global; absence degrades to the lead's
    // fallback figure, announced by one stderr line at init).
    struct wl_display*    wl_display_     = nullptr;
    struct wl_registry*   wl_registry_    = nullptr;
    struct wl_compositor* wl_compositor_  = nullptr;
    struct wl_shm*        wl_shm_         = nullptr;
    struct xdg_wm_base*   xdg_wm_base_    = nullptr;
    struct zxdg_decoration_manager_v1* xdg_decoration_manager_ = nullptr;
    struct wl_data_device_manager*     wl_data_device_manager_ = nullptr;

    // -- Outputs: every wl_output, and THE ONE THE WINDOW IS ON --
    // One record per advertised wl_output (bound at v2 — the mode event is
    // all this program reads; no name, no description), kept while the global
    // stands and destroyed at its registry removal. `refresh_mhz` is that
    // output's latest CURRENT mode, 0 until one arrives. `entered` mirrors
    // wl_surface.enter/leave for the main surface: true while the compositor
    // says the window is (at least partly) on this output.
    //
    // THE SELECTION RULE (2026-09-02, replacing "the first output advertised"
    // — a coin flip on a laptop with a 60 Hz panel and a 120 Hz external,
    // which cost redraw density on the external and would have been a 2× error
    // in the display lead's fallback): `window_output_` is THE OUTPUT THE
    // WINDOW IS ON — the MOST RECENT wl_surface.enter, so a window spanning
    // two outputs follows the one it was last seen entering; on the LEAVE of
    // the selected output the selection falls to any other output still
    // entered, else stays where it was, keeping the last figure (a window on
    // no output — minimized, or mid-move — has no better answer); a removed
    // output leaves the selection the same way. Before the first enter (the
    // initial configure and the first paint precede it) the selection is
    // seeded with the first output bound, exactly the old rule, so the tick
    // starts on a real figure. `output_refresh_mhz_` below is always the
    // selected output's refresh, re-derived by `select_window_output` at every
    // edge — enter, leave, a mode change on the selected output, a removal —
    // and every change re-arms the tick.
    struct OutputRecord {
        struct wl_output* output      = nullptr;
        uint32_t          global_name = 0;
        int               refresh_mhz = 0;
        bool              entered     = false;
    };
    std::vector<OutputRecord> outputs_;
    struct wl_output*         window_output_ = nullptr;

    // -- wp_presentation: the display lead's instrument (display_lead_ns) --
    // Bound at v1 (the `presented` event this reads is v1's; the proxy is
    // OPTIONAL — see the protocol classes above). `presentation_clock_ok_`
    // records that the compositor's clock_id event named CLOCK_MONOTONIC, the
    // predictor's own clock (steady_clock on glibc/Linux), so `presented`'s
    // timestamp and the pre-paint stamp are subtracted in one domain with no
    // conversion; any other clock leaves the global bound but UNUSED —
    // treated as absent, said once on stderr — since a lead measured across
    // two clocks is not a measurement.
    struct wp_presentation* wp_presentation_        = nullptr;
    bool                    presentation_clock_ok_  = false;

    // ONE FEEDBACK OBJECT PER CONTENT COMMIT, requested BEFORE that commit
    // (the protocol's shape; paint_one_frame's order), several possibly
    // outstanding at once, so each carries ITS OWN pre-paint stamp in its own
    // user data rather than on this singleton — a stamp on the platform would
    // be overwritten by the next paint before the compositor answered for
    // this one. The record owns nothing but the stamp and its proxy; both
    // listener arms (presented, discarded) destroy the proxy and the record,
    // and shutdown destroys whatever is still outstanding.
    struct PresentationFeedback {
        GuiPlatform*                     self      = nullptr;
        struct wp_presentation_feedback* proxy     = nullptr;
        int64_t                          sample_ns = 0;
    };
    std::vector<PresentationFeedback*> presentation_feedbacks_;

    // THE 30-FRAME MOVING MEAN of (presented − sampled), Kodi's window: long
    // enough to average the compositor's per-frame jitter, short enough to
    // follow a change (a window moved to the 120 Hz output settles in half a
    // second). A RING rather than a bare running sum, because a moving mean
    // must DROP its oldest sample and only the ring knows which one that is;
    // the sum is kept beside it so the mean is one division. `count` grows to
    // the window and stays there; `next` is the slot the next sample
    // overwrites. The mean is published from the FIRST sample (one measured
    // frame already beats the fallback's guess), announced on stderr when the
    // window first fills and again whenever it moves a millisecond from the
    // figure last announced (`presentation_lead_announced_ns_`, -1 until the
    // first announcement).
    static constexpr int kDisplayLeadWindow = 30;
    int64_t presentation_samples_[kDisplayLeadWindow] = {};
    int     presentation_sample_count_ = 0;
    int     presentation_sample_next_  = 0;
    int64_t presentation_sample_sum_   = 0;
    int64_t presentation_lead_announced_ns_ = -1;

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
    // The run-stop request, cleared at the head of every run() (platform.h).
    bool run_stop_requested_ = false;
    bool has_initial_configure_ = false;
    // Latest XDG_TOPLEVEL_STATE_ACTIVATED reading; see window_activated().
    bool window_activated_ = false;

    // True only while paint_one_frame is executing the pre-paint hook.
    // invalidate_region() consults this flag and skips its trailing
    // schedule_frame_callback() call when set, so the hook can declare
    // additional damage without producing a spurious extra commit.
    bool in_pre_paint_ = false;

    // THE WINDOW'S OUTPUT'S latest CURRENT mode, in millihertz — the selected
    // record's figure (the selection rule at `outputs_`), re-derived at every
    // output edge by select_window_output. Zero means no output has reported
    // a usable mode yet; detect_refresh_rate_ms() then falls back to 60 Hz,
    // and so does the display lead's fallback period.
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

    // Checkpoint-worker completion fd. Same lifetime story as the two above.
    // -1 when no checkpoint worker is registered.
    int  history_worker_completion_fd_ = -1;
    std::function<void()> on_history_worker_completion_;

    // History-prefetch ready fd. Same lifetime story again; -1 when no prefetch
    // worker is registered.
    int  history_prefetch_completion_fd_ = -1;
    std::function<void()> on_history_prefetch_ready_;

    // Synchronization-worker completion fd. Same lifetime story again; -1 when
    // no synchronization worker is registered.
    int  sync_worker_completion_fd_ = -1;
    std::function<void()> on_sync_worker_completion_;

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
    // Latest wl_pointer.enter serial. Tracked for wl_pointer.set_cursor: both of
    // that request's callers need a recent enter serial — apply_cursor_kind (the
    // one owner of the VISIBLE cursor, shared by the enter, the capture-lock
    // failure and the capture release) and the NULL-surface hide at capture
    // begin, which is the only set_cursor call outside that owner.
    uint32_t pointer_enter_serial_ = 0;

    // -- Pointer --
    struct wl_pointer* wl_pointer_ = nullptr;

    // -- Touch --
    // The wl_touch proxy alone: the phase machine, the translation contract and
    // the edge inventory all live in the input core (its touch state block).
    // wl_touch is CORE protocol (wl_seat's third capability), bound from the
    // seat listener when WL_SEAT_CAPABILITY_TOUCH is advertised and NOT
    // required: absence is silence — no stderr, nothing degraded (the
    // authoring laptop simply has no glass; the target panel has one).
    struct wl_touch* wl_touch_ = nullptr;

    // THE CURSOR SET (system theme, loaded once at init, kept for the process
    // lifetime). ONE ENTRY PER GuiCursorKind, indexed by the enumerator.
    //
    // Each entry is a dedicated wl_surface — distinct from the main window
    // wl_surface_ — with the theme image's buffer attached once and never
    // re-attached, plus THE HOTSPOT THAT IMAGE DECLARES. A set_cursor therefore
    // swaps kinds by naming a different SURFACE, with no image work per swap,
    // which is what makes the per-run-loop-iteration applier free.
    //
    // The BUFFERS belong to libwayland-cursor and die with wl_cursor_theme_; the
    // surfaces are ours and are destroyed before the theme (shutdown). A null
    // surface means EVERY name that kind's table row carries was missing from
    // the theme (one for all but the I-beam, which tries two): apply_cursor_kind
    // falls back to Arrow's, and Arrow's own null (a theme with no left_ptr, or
    // no theme at all) is the protocol hide.
    struct ThemeCursor {
        struct wl_surface* surface   = nullptr;
        int32_t            hotspot_x = 0;
        int32_t            hotspot_y = 0;
    };
    struct wl_cursor_theme* wl_cursor_theme_ = nullptr;
    ThemeCursor             cursors_[kGuiCursorKindCount];

    // -- THE PORTABLE INPUT POLICY (input_core.h) --
    // Every input event this class decodes is handed to this object in plain
    // units, and every input door on the public surface above forwards to it.
    GuiInputCore input_;

    // -- Callbacks --
    RedrawCallback       on_redraw_;
    ResizeCallback       on_resize_;
    CloseCallback        on_close_;
    // Stored and never fired on this backend (see set_on_media_command).
    std::function<void(GuiMediaCommand)> on_media_command_;
    // Fired at each window_activated_ EDGE (see set_activation_changed_hook).
    std::function<void()> activation_changed_hook_;
    // Fired at the TAIL of every run() iteration that is not leaving the loop
    // (see set_loop_settled_hook). SEEDED with a no-op, like the input handler's
    // capture hooks, so the fire site needs no null test.
    std::function<void(GuiInputState)> loop_settled_hook_ =
        [](GuiInputState) {};
    TickCallback         on_tick_;
    PrePaintCallback     on_pre_paint_;

    // -- Internal helpers --
    void recreate_shm_pool(int w, int h);
    void destroy_shm_pool();
    bool load_cursor_theme();
    // Look one theme cursor up by its xcursor NAME and give it a surface with
    // the theme image's buffer and the image's OWN hotspot. False when the theme
    // has no such name (or the buffer/surface could not be made), leaving that
    // kind's entry null; the caller decides whether that is fatal (Arrow) or a
    // degraded cue (every other kind).
    bool load_theme_cursor(GuiCursorKind kind, const char* xcursor_name);
    // THE ONE wl_pointer.set_cursor CALLER for the visible cursor: hands the
    // compositor the surface and hotspot cursor_kind_ names, at the tracked
    // enter serial. Silent no-op while a capture holds the cursor hidden, and
    // while there is no wl_pointer.
    void apply_cursor_kind();
    ShmBuffer* acquire_free_buffer();
    // THE FRAME REQUEST'S TWO SHAPES (2026-09-02). request_frame_callback
    // issues wl_surface.frame and NOTHING ELSE — the request is double-
    // buffered surface state and rides whatever commit follows, which in
    // paint_one_frame is the content commit itself (frame → feedback → damage
    // → attach → ONE commit, the conventional order; two commits per painted
    // frame in the reverse order until this day). schedule_frame_callback is
    // the STANDALONE form — the request plus a commit of its own — for the
    // callers that have no content to attach and still need the request sent:
    // the empty-damage and both-buffers-busy returns inside paint_one_frame,
    // invalidate_region's first damage after an idle stretch, and a
    // reconfigure with nothing pending. Both are no-ops while a callback is in
    // flight.
    void request_frame_callback();
    void schedule_frame_callback();
    void paint_one_frame();
    void destroy_wayland_state();
    int  detect_refresh_rate_ms();
    bool arm_playback_timer();

    // -- Output helpers --
    // The record for a wl_output proxy, or null for one this program never
    // bound (a cursor surface's output events never reach here; the main
    // surface's enter names only outputs the registry named).
    OutputRecord* find_output(struct wl_output* output);
    // THE ONE WRITER of output_refresh_mhz_: apply the selection rule (at
    // `outputs_`) to `window_output_` and, when the figure moved, re-arm the
    // tick. Called at every output edge.
    void select_window_output();

    // -- Presentation helpers --
    // Request one wp_presentation_feedback for the content commit that
    // follows, stamped with `sample_ns`; a no-op while the global is absent or
    // its clock unusable. The stderr announcements and the ring are at the
    // presented handler.
    void request_presentation_feedback(int64_t sample_ns);
    // The one teardown of a feedback record: unlist it, destroy its proxy,
    // free it. Both terminal arms end here; shutdown runs it over the rest.
    void finish_presentation_feedback(PresentationFeedback* fb);
    void destroy_presentation_feedbacks();

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
    void on_output_mode(struct wl_output* output, uint32_t flags,
                        int32_t width, int32_t height, int32_t refresh_mhz);
    // wl_surface.enter / leave on the MAIN surface (the selection rule at
    // `outputs_`).
    void on_surface_enter(struct wl_output* output);
    void on_surface_leave(struct wl_output* output);
    // wp_presentation.clock_id, and the feedback's two terminal arms.
    void on_presentation_clock_id(uint32_t clk_id);
    void on_presentation_presented(PresentationFeedback* fb,
                                   uint32_t tv_sec_hi, uint32_t tv_sec_lo,
                                   uint32_t tv_nsec, uint32_t flags);
    void on_presentation_discarded(PresentationFeedback* fb);
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
    // A raw xkb keycode -> the GuiKey the application sees, or false when this
    // GUI has no use for the key at all (standalone modifiers, F1..F35, an
    // unmapped keycode). THE ONE translation, read by the press path and by the
    // release path, so a release can never disagree with its press about which
    // key it is; the drop classes and their reasoning are at the definition.
    bool key_from_keycode(uint32_t xkb_keycode, GuiKey& out) const;

    // -- Pointer handlers --
    void on_pointer_enter(uint32_t serial, struct wl_surface* surface,
                          int32_t surface_x, int32_t surface_y);
    void on_pointer_leave(uint32_t serial, struct wl_surface* surface);
    void on_pointer_motion(uint32_t time, int32_t surface_x, int32_t surface_y);
    void on_pointer_button(uint32_t serial, uint32_t time,
                           uint32_t button, uint32_t state);
    void on_pointer_axis(uint32_t time, uint32_t axis, int32_t value);
    void on_pointer_axis_value120(uint32_t axis, int32_t value120);

    // -- Touch handlers --
    // The two events carrying coordinates: this half decodes the fixed-point
    // pair and the core owns the machine. The other three (up, frame, cancel)
    // carry nothing to decode and reach the core straight from the listener.
    void on_touch_down(uint32_t serial, uint32_t time, int32_t id,
                       int32_t fx, int32_t fy);
    void on_touch_motion(uint32_t time, int32_t id, int32_t fx, int32_t fy);

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
