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

// THE POINTER CURSOR KINDS. EVERY ONE OF THEM IS A NAMED CURSOR FROM THE USER'S
// OWN XCURSOR THEME, UNMODIFIED (architect 2026-08-03): the product ships no
// cursor art and draws no cursor pixels, so a kind is a name to look up
// (load_cursor_theme's table, platform_wayland.cpp) and the theme's own image
// and its own declared hotspot are what the compositor gets.
//
// Arrow is left_ptr, the cursor everywhere the GUI names nothing else, and it is
// also the FALLBACK: a theme missing one of the other six names degrades that
// KIND to the arrow with one stderr line, which costs the cue and nothing else.
// The six beside it each mark a zone whose gesture the arrow cannot promise —
// the mapping from zone to kind is the GUI's (pointer_cursor_kind,
// input_handler.h), and this enum is only the vocabulary.
//
// THE TRIM BAR SPENDS THREE OF THEM, and the split is the act rather than the
// surface (architect 2026-08-03): TrimBoundBegin and TrimBoundEnd are the
// window-edge shapes a window manager shows on a left / right border, and they
// mark EXTENDING ONE BOUNDARY — the trim window's begin cap and end cap, and the
// ctrl / ctrl+shift clicks that write those same two bounds — while TrimResize
// stays with the BRIDGE, which moves both bounds together. A begin/end pair
// rather than a left/right one because the bound is what the gesture names; the
// two happen to coincide because the begin bound is always the window's left
// edge.
enum class GuiCursorKind {
    Arrow,
    Scrub,
    Pan,
    Zoom,
    TrimResize,
    TrimBoundBegin,
    TrimBoundEnd,
};
// Roster size, for the platform's per-kind cursor array. Keep it equal to the
// enumerator count above.
inline constexpr int kGuiCursorKindCount = 7;

// WHY THE POINTER FOCUS WAS DROPPED — the one fact the leave hook's fire
// sites do not share, handed to the consumer because it changes what the drop
// may leave standing (2026-08-08).
//   * OrdinaryLeave is wl_pointer.leave — and, since touch phase 1
//     (2026-08-11), a touch POINTER TRANSLATION ending WITH NO PHYSICAL
//     POINTER FOCUSED: the finger left the glass and no mouse rests in the
//     window, so the pointer is gone (delivered after the release; the fires
//     are the touch up, wl_touch.cancel and touch-capability loss — the edge
//     inventory at the touch state block). A translation ending with the
//     physical pointer FOCUSED fires this hook NOT AT ALL — it delivers a
//     restore MOTION at the mouse's own position instead (the codex round-3
//     fork; the one statement is at deliver_touch_translation_end's
//     definition). Either way the stream is NOT over.
//     No position event arrives WHILE the pointer stays outside, but a
//     re-entry (or the next touch) synthesizes
//     a motion, a held button still releases, and the held state survives — so a
//     consumer may knowingly KEEP a face or a mode across this edge and rely on
//     that return motion to re-derive it.
//   * CapabilityLoss is the seat losing wl_pointer: the hard end of the stream.
//     No leave, no motion, no release will ever arrive on that object again, so
//     nothing may be KEPT DELIBERATELY across it — a consumer's keep has no
//     event left to redeem it. WHAT THE EDGE PROMISES IS BOUNDED, and no more
//     than the fire site does: the logical left hold ends in both its sources,
//     the popup's claim drops, and every face clears ONCE. It does not promise a
//     cold stream at a later capability return — a staged-motion flush can put
//     `pointer_in_window` back and leave a hover face stale, which is a recorded
//     ACCEPTED GLITCH (architect 2026-08-09; the record is at the fire site) and
//     self-heals on the pointer's next entry.
// The distinction exists for exactly one consumer today (main.cpp's hook body,
// where the menu row's armed mode and its hovered button survive an ordinary
// leave through row 1 and never survive the hard one); every other clear the
// hook performs is unconditional and reads this not at all.
enum class GuiPointerLeaveReason {
    OrdinaryLeave,
    CapabilityLoss,
};

class GuiPlatform {
public:
    using RedrawCallback       = std::function<void(cairo_t*, int x, int y, int w, int h)>;
    using ResizeCallback       = std::function<void(int w, int h)>;
    using KeyCallback          = std::function<void(GuiKey key, GuiInputState mods)>;
    // A KEY RELEASE, carrying the key alone (2026-08-13). Key releases were
    // platform-internal until the modal dialog's buttons grew a keyboard
    // press-and-hold whose act is at the LIFT; the application needs the edge
    // and needs nothing else from it, so this callback deliberately hands over
    // no modifier state — the PRESS is what is modifier-exact, exactly as
    // on_button_release's unused `mods` parameter says of the pointer.
    using KeyReleaseCallback   = std::function<void(GuiKey key)>;
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
    // THE KEY RELEASE HOOK (2026-08-13). It fires for the SAME key identity the
    // press delivered — the same keysym lookup, the same ASCII case-fold — and
    // it inherits the press path's two drop classes verbatim (standalone
    // modifier keysyms and F1..F35, keys this GUI has no use for) plus one of
    // its own: a release that ENDS A SYNTHESIZED LEFT-BUTTON HOLD is the mouse
    // button's release, not a key's, and the press it matches was never
    // delivered either.
    // IT IS DELIBERATELY NOT GATED ON SUPER, where the press path is. The Super
    // drop exists so a chord that belongs to labwc never reaches a BINDING, and
    // a release binds nothing on its own — it can only resolve an arm an
    // already-delivered press created. Gating it would strand exactly the arm
    // whose press got through before Super went down, which is the one case
    // that could go wrong. Null-safe.
    void set_on_key_release(KeyReleaseCallback cb);
    void set_on_button_press(ButtonCallback cb);
    void set_on_button_release(ButtonCallback cb);
    void set_on_wheel(WheelCallback cb);
    void set_on_motion(MotionCallback cb);
    void set_on_close(CloseCallback cb);
    void set_wheel_context_probe(WheelContextProbe cb);
    void set_text_editor_active_probe(TextEditorProbe cb);
    void set_repeat_eligible_probe(RepeatEligibleProbe cb);
    // Fired when the pointer LEAVES the surface (wl_pointer.leave), at
    // pointer-capability loss, and — since touch phase 1 — at a touch pointer
    // translation's end on its NO-FOCUS arm only (as OrdinaryLeave, after the
    // release: the finger left the glass and no mouse rests in the window; a
    // FOCUSED physical pointer gets a restore motion instead and this hook
    // stays silent — the round-3 fork, stated at
    // deliver_touch_translation_end). The edges drop pointer focus
    // and no position event will follow (outright, for capability loss; for the
    // ordinary leaves, for as long as the pointer stays outside — it may
    // re-enter
    // with a synthesized motion, and a held button still releases normally).
    // WHICH EDGE FIRED IT IS THE ARGUMENT (GuiPointerLeaveReason, above the
    // class; 2026-08-08). The body is shared, and the difference above is real:
    // a consumer may keep pointer-derived state across the ordinary leave, where
    // a return motion will re-derive it, and may keep NOTHING across the hard
    // one, where no such event exists. Each fire site passes its own reason and
    // neither infers it. The one consumer that reads it is named at the enum.
    // The one owner of the drop-what-the-pointer-was-naming behavior. What
    // main.cpp wires it to is enumerated THERE, at the hook body, which is the
    // authoritative list — this contract deliberately does not keep a second
    // copy — but the shape is: every face and claim derived from where the
    // pointer is, so a pointer that slides out through the window edge cannot
    // leave a lit pill, a stranded pressed interior, a lit menu item or a
    // hanging tooltip behind. It USED to clear the
    // marker hover popup as well; that whole surface died with the marker-text
    // lane in row 5. Widened 2026-08-03 to the open dropdown's pointer-derived
    // state — the roster's button faces are not the only such state this edge
    // drops any more; full story at clear_dropdown_pointer_state. Null-safe.
    void set_pointer_left_hook(std::function<void(GuiPointerLeaveReason)> cb);

    // Fired ONLY on a CHANGE of window_activated(), from the xdg_toplevel
    // configure handler. The compositor re-sends the full state array on every
    // configure (resize, maximize, focus), so the edge test lives in the
    // platform and this hook is the edge itself — main.cpp wires it to mirror
    // the flag into AppState and damage the top strip, the same shape the
    // pointer-leave hook takes for the same reason (a protocol edge with no
    // other event to carry its repaint). Null-safe.
    void set_activation_changed_hook(std::function<void()> cb);

    // THE KEYBOARD-INTENT CANCELLATION HOOK (codex round 4, 2026-08-11): fired
    // wherever the platform ENDS OR CONSUMES the keyboard stream WITHOUT a
    // delivery, so application-side key intent — the modal dialog's keyboard
    // press arm, whose own disarms can only see events that reach the
    // application — dies on the same edges the platform's own key-repeat
    // state does. TWO fire classes, each stated at its site:
    //   * forget_keyboard_state — wl_keyboard.leave and keyboard-capability
    //     loss, the edges that clear repeat_key_ itself: a keyboard-driven
    //     focus change ends every release the stream owed;
    //   * deliver_key's SUPER DROP, per swallowed NON-SYNTHESIZED press: the
    //     swallowed press is an intervening key ARRIVAL the application's
    //     on_key disarm never sees, and the platform's own layer-1 disarms its
    //     armed repeat at that very press (the arming else-branch in
    //     on_keyboard_key runs BEFORE the drop) — per-delivery is the faithful
    //     mirror. Deliberately NOT at Super's press edge: the Super keysym
    //     itself "disarms nothing" platform-side, and a bare Super hold with no
    //     key pressed runs no command (adjacency stands), while the hold
    //     itself is a POINTER act, which the Super ruling explicitly scopes
    //     out (Super+click clicks).
    // ONE hook rather than another application-side list, so a platform edge
    // added later joins by firing it. The consumer's authoritative effect
    // list is main.cpp's hook body; the arm it drops is
    // AppState::modal_dialog_key_pressed. (The transport arrows' hold-repeat
    // rode this hook until its deletion, 2026-08-13.) Null-safe.
    void set_keyboard_intent_cancel_hook(std::function<void()> cb);

    // THE TOUCH NAVIGATION HOOKS (touch phase 1, 2026-08-11; SIX members
    // since PAN-PRIMARY's touch half, the eighth glass ruling 2026-08-12 —
    // update, end, the pan-zone query, and the REGION trio
    // begin/update/end, the dead trim-move members' exact pattern reborn for
    // the region former; touch.md carries the arc). ONE finger on
    // the glass IS the pointer — translated whole inside this class, so the
    // GUI sees ordinary pointer deliveries and cannot tell which device
    // produced them (the bare-`e` precedent applied to glass; the translation
    // contract lives at the touch state block below) — with TWO ruled
    // exceptions, both born of the pan zone (the pan_zone query below): a
    // one-finger DRAG whose DOWN POINT lies on the zone is
    // SINGLE-FINGER NAVIGATION — the finger drags the pan, the phone model —
    // delivered through
    // the SAME update hook with the finger as the centroid and dist_ratio
    // pinned at 1.0 (one finger has no distance, so no zoom; the GUI body
    // needs no fork and got none), and a HOLD past the zone's stretched
    // window (kTouchRegionHoldMs) is THE REGION HOLD — the expiry drives the
    // GUI's region former through the region trio, so hold-then-drag sweeps
    // a region on glass (the eighth ruling: pan is the common act and takes
    // the primary drag, the region is the deliberate act and takes the
    // hold). TWO fingers are the dual-axis gesture:
    // per-frame centroid pan plus distance-ratio zoom about the centroid, the
    // strip drag's own semantics. None of the three delivers pointer events.
    // All are handed to the GUI through these hooks (the
    // set_keyboard_intent_cancel_hook wiring precedent — main.cpp wires them
    // to the input handler's touch-nav body, which drives the strip-drag
    // family's viewport chokepoint, and to the region former's own bodies).
    //
    //   * update(x, y, dx, dist_ratio): fired at most once per wl_touch.frame
    //     while a navigation gesture is live and past its latch. (x, y) is
    //     the CURRENT centroid in surface px (single-finger: the finger
    //     itself); dx is the centroid's horizontal delta since the previous
    //     delivered update (fractional — sub-pixel centroid motion must
    //     accumulate rather than truncate away); dist_ratio is the
    //     finger-distance ratio current/previous, > 0 always (a degenerate
    //     distance under 1 px on either side delivers 1.0 — no zoom that
    //     frame; single-finger frames are degenerate by construction, so they
    //     always carry 1.0). The latch is the platform's: nothing is
    //     delivered until the centroid has travelled kTouchSlopPx (Chebyshev)
    //     from the gesture's start OR the finger distance has changed by that
    //     same slop, and the crossing update folds the whole accumulated
    //     delta — the strip drag's own press-becomes-drag model, so a
    //     two-finger tap navigates nothing (a single-finger nav is born past
    //     its latch: it exists only by crossing the disambiguation slop, the
    //     same distance in the same Chebyshev metric, measured from the same
    //     down point).
    //   * end(): the gesture ended — a finger lifted (any end commits; a
    //     two-finger survivor is ignored until all fingers lift),
    //     wl_touch.cancel, or touch-capability loss. Fired ONLY if at least
    //     one update was delivered, so a sub-latch two-finger touch costs the
    //     GUI nothing.
    //   * pan_zone(x, y): THE ZONE QUERY — does this point lie on the
    //     one-finger PAN SURFACE? Asked ONCE per touch stream, at the FIRST
    //     finger's down; the answer is captured beside the down point, picks
    //     the window's DEADLINE (kTouchRegionHoldMs on the zone,
    //     kTouchDisambiguateMs off it) and forks both the slop-crossing
    //     resolution (nav vs pointer) and the expiry (region hold vs pointer
    //     unlock) — the touch state block below. SURFACE GEOMETRY ONLY — the
    //     GUI answers the navigation surface (the pan-primary vocabulary's
    //     one plain-drag surface, flag boxes carved out) and nothing modal;
    //     every refusal (prompt, editors, dropdown, loading/empty audio,
    //     live pointer gesture) stays downstream — at the per-frame
    //     wheel-context answer inside the update body for the nav gestures,
    //     in the region begin body for the hold — so a refused pan freezes
    //     and a refused hold is a dead stream rather than a fallback pointer
    //     drag. Null — or answering false — means no pan surface: the plain
    //     phase-1 translation everywhere.
    //   * region_begin(x, y): the hold resolved on the pan zone at the beat —
    //     the GUI arms its region former at the DOWN point (the former's own
    //     press half: deselect, playhead seat, the drag arm), or refuses
    //     inside the body (prompt, the five editors, open dropdown,
    //     loading/empty audio, live pointer gesture); a refused begin makes
    //     the two hooks below no-ops through the drag's own !active guard, so
    //     the refused stream is dead. NOTHING pointer-shaped starts — no
    //     press, no hold bit, no release, no translation end (the
    //     single-finger nav's model).
    //   * region_update(x, y): at most once per wl_touch.frame while the
    //     region gesture is live — the finger's current position, absolute
    //     (the drag's one motion path converts to columns itself).
    //   * region_end(): the gesture ended — finger up, wl_touch.cancel or
    //     touch-capability loss. Any end COMMITS (the former's own release
    //     regime: a moved drag rests the span under the sliver gate, a
    //     motionless hold-lift rests nothing and leaves the playhead where
    //     the begin seated it — the placement). Fired UNCONDITIONALLY once
    //     the gesture began — the GUI-side drag holds the drag-modal gate
    //     open and its release path is owed — the refused-begin stream
    //     covered by the same !active guard.
    //
    // The hooks carry no modifier state: the GUI bodies read nothing modal
    // from them, and their refusal answers are their own (the per-frame
    // wheel-context predicate; the region begin's gate list). Null-safe.
    void set_touch_nav_hooks(
        std::function<void(int x, int y, double dx, double dist_ratio)> update,
        std::function<void()> end,
        std::function<bool(int x, int y)> pan_zone,
        std::function<void(int x, int y)> region_begin,
        std::function<void(int x, int y)> region_update,
        std::function<void()> region_end);

    // TRUE WHILE ANY FINGER IS ON THE GLASS — the phase machine simply not
    // Idle. The full rationale (and why Drain's inclusion is harmless) is at
    // the touch state block below, beside the phases it reads; the ONE
    // consumer is main.cpp's pre-paint follow chase, which must not page the
    // song out from under a finger that is aiming or gesturing.
    bool touch_contact_active() const {
        return touch_phase_ != TouchPhase::Idle;
    }

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
    // to at all), and not once should_exit_ is set (the iteration is the last
    // one — the objects the consumer reads are still alive, main.cpp's outlive
    // run(), but the frame it would compute for is never presented). The
    // condition is stated once, at the fire site.
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
    // GuiInputState; it gates key delivery instead).
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
    // below), the grab-pan at the raw traveled virtual_pointer_x_; y is frozen at
    // the press row for both. Both degrade to a silent no-op when the
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
    // live inside this class, gating the synthesized-enter motion — so this
    // deletes the door, not the state behind it.)

    // Override the release-restore x for the active capture. The strip drags
    // set this each event to the surface x of their anchor stem, so the cursor
    // reappears dead on the stem's column (the edge-trick rebind pins the stem
    // while the raw cursor travel keeps going, so the raw virtual_pointer_x_
    // would land past it). begin_pointer_capture clears it, so a capture with
    // no override set (the grab-pan, which has no stem) restores at the raw
    // traveled virtual_pointer_x_.
    void set_capture_restore_x(double surface_x);

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

    // Checkpoint-worker completion fd. Same lifetime story as the two above.
    // -1 when no checkpoint worker is registered.
    int  history_worker_completion_fd_ = -1;
    std::function<void()> on_history_worker_completion_;

    // History-prefetch ready fd. Same lifetime story again; -1 when no prefetch
    // worker is registered.
    int  history_prefetch_completion_fd_ = -1;
    std::function<void()> on_history_prefetch_ready_;

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
    // THE RESTORE ALSO WRITES THE TRACKED POSITION BACK to that same hint —
    // this pair and pointer_x_/pointer_y_ alike — so the travel does NOT outlive
    // the lock in the coordinates. It has to: button events carry no
    // coordinates in the protocol and are delivered at pointer_x_/pointer_y_,
    // and the unlock warp comes back as no wl_pointer.motion, so a click made
    // before the user next moved was routed at the travel's end. The full
    // account of the defect (a ruler zoom drag — that entry is history now,
    // the strip drag arming from the ctrl-waveform press alone — then a click
    // that flipped the marker view from the icon row) is at
    // release_pointer_lock.
    bool   pointer_captured_   = false;
    double virtual_pointer_x_  = 0.0;
    double virtual_pointer_y_  = 0.0;
    double capture_restore_y_  = 0.0;
    std::optional<double> capture_restore_x_override_;

    // "THE POSITION WE HAVE IS NOT THE POINTER'S." ONE FACT, ONE OWNER, and the
    // whole reason set_cursor_kind can ignore a write (the rule is stated at that
    // method's declaration). It goes TRUE when a lock is REQUESTED — the proxy is
    // taken for a live lock by ruling, see begin_pointer_capture — from that moment
    // pointer_x_/pointer_y_ carry the unbounded VIRTUAL travel, a point the
    // pointer does not occupy — and stays true PAST the unlock, because the
    // restore only tells the compositor where to put the cursor; where it ended
    // up is not ours to know until the compositor says so.
    // WHAT THE UNLOCK DOES SETTLE IS THE COORDINATES, NOT THIS FLAG, and the two
    // must not be read as one fact: release_pointer_lock writes
    // pointer_x_/pointer_y_ (and the virtual pair) to the restore hint, so past
    // the unlock they name the pixels the cursor is DRAWN on rather than the
    // travel — an estimate, which is exactly why this flag stays set over it.
    // The split is load-bearing: the kinds this flag guards can afford to wait
    // for the compositor, and the BUTTON deliveries — which carry no
    // coordinates of their own and so read those fields — cannot, having once
    // routed a click at the travel's end (the account is at
    // release_pointer_lock). It goes false again at
    // the next ABSOLUTE position the compositor delivers (wl_pointer.enter or
    // wl_pointer.motion), which is exactly the event that re-establishes the
    // truth — and the re-derivation follows in that same loop iteration, at the
    // tail hook that owns the cursor (set_loop_settled_hook).
    // WHAT SURVIVES THE SPAN is not an inference: the same lock-request path that
    // sets this STAMPS the gesture's own cue as the remembered kind (the
    // restore_kind parameter at begin_pointer_capture), so the release restores a
    // fact the gesture supplied while the position was still real, rather than
    // whatever the last dispatch batch happened to leave behind.
    // ONLY A REQUESTED LOCK SETS IT — treated as a live one by ruling, with the
    // scope and the accepted degradation at begin_pointer_capture: the degraded
    // compositor (no pointer-constraints or no relative-pointer) never asks for a
    // lock at all, so its "captured" gestures run on ordinary absolute motion with
    // the position true throughout and every cursor write landing normally. That is what lets the GUI's ONE per-iteration
    // re-resolve serve the captured and the degraded path alike, with nothing in
    // the GUI testing which case it is in.
    // The clear is guarded on !pointer_captured_ so a stray absolute event mid-
    // capture cannot declare the virtual position true.
    bool pointer_position_unknown_ = false;

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
    // (pointer_left_held_ || synth_left_held_ || touch_left_held_): a press is
    // delivered only on the 0->1 edge of that OR and a release only on the
    // 1->0 edge (the evdev model for devices sharing BTN_LEFT), so the
    // physical, synthesized and touch sources never double-deliver. Each
    // source's hold ends on its OWN stream's edges and never on a sibling's
    // (the keyboard edges end the synth hold, the pointer-capability loss ends
    // the physical and synth holds, the touch edges end the touch hold).
    bool     synth_left_held_    = false;
    uint32_t synth_left_keycode_ = 0;

    // -- Touch (wl_touch as the pointer; touch phase 1, 2026-08-11; the
    //    WINDOWED MODEL, restored at the sixth glass ruling, 2026-08-12) --
    //
    // THE UNIFIED-MODE PREMISE, made literal at this boundary: ONE finger IS
    // the pointer, translated whole here so the GUI sees ordinary pointer
    // deliveries — enter-motion, left press, motion, release — and cannot tell
    // which device produced an event (no touch mode, no flag, no GUI-side
    // branch; the bare-`e` precedent applied to glass). TWO fingers are the
    // navigation gesture, delivered through the touch-nav hooks and NEVER as
    // pointer events. The DISAMBIGUATION WINDOW is what buys that vocabulary
    // (its one-day timer-free deletion and the field verdict that reversed it
    // — a pinch whose press lands at contact jumps the playhead before it can
    // zoom — are touch.md's record). wl_touch is CORE protocol (wl_seat's
    // third capability — the wl_data_device_manager precedent: same
    // wayland.xml, no new library, no generated stub), bound from the seat
    // listener when WL_SEAT_CAPABILITY_TOUCH is advertised and NOT required:
    // absence is silence — no stderr, nothing degraded (the authoring laptop
    // simply has no glass; the target rig's panel does).
    //
    // THE PHASES:
    //   * Idle    — no touch points.
    //   * Pending — the DISAMBIGUATION WINDOW: the first finger is down and
    //     NOTHING has been delivered. The window exists only to tell tap from
    //     drag from two fingers from the region hold, and its DEADLINE FORKS
    //     ON THE DOWN POINT'S PAN-ZONE ANSWER (the eighth glass ruling,
    //     2026-08-12 — the two-deadline fork, the dead trim-band beat's exact
    //     pattern): ON the zone the window runs to kTouchRegionHoldMs (the
    //     region-hold beat), OFF it kTouchDisambiguateMs as before.
    //     It resolves to Pointer on the finger lifting inside it (a TAP —
    //     the whole burst delivers at the lift, and on the navigation
    //     surface that burst's motionless press-release IS the deferred
    //     click act — the playhead placement, the pan-primary mouse half's
    //     own machinery); on EXPIRY
    //     it FORKS on the zone answer: OFF the zone HOLD UNLOCKS THE POINTER
    //     (expiry with the finger stationary is the DELIBERATE escape to the
    //     ordinary pointer translation — hold-then-drag is the old pointer
    //     drag, which is what keeps the endcap/bridge grabs, the flag drags
    //     and every off-zone press-and-hold gesture alive on glass), ON the
    //     zone the beat's expiry is THE REGION HOLD (-> Region below —
    //     hold-then-drag sweeps a region, the deliberate act's glass form);
    //     on MOTION beyond kTouchSlopPx it FORKS on the same
    //     captured pan-zone answer (the PHONE MODEL, second glass session
    //     2026-08-11): inside the pan surface -> SINGLE-FINGER Nav (the
    //     finger drags the pan; no press was ever delivered — nothing to
    //     unwind, the window's whole purpose), outside -> Pointer. A second
    //     finger landing inside the window resolves to two-finger Nav
    //     wherever the down point was — the jump-free pinch, the window's
    //     whole point in the field.
    //   * Pointer — the translation is live: the owning finger's motion is
    //     pointer motion (coalesced to the wl_touch.frame boundary, the
    //     pointer-frame precedent), its lift is the release on the logical
    //     left 1->0 edge AND the TRANSLATION END ON THAT SAME EDGE — an
    //     ordinary leave, or a restore motion at the mouse when the physical
    //     pointer has focus (the finger left the glass; what keeps hover
    //     faces from resting lit where a finger last was; a sibling-held
    //     logical left suppresses ALL of it — see the UP clauses below). The
    //     phase tracks whether the translation has MOVED — Chebyshev >=
    //     kTouchSlopPx from the down point, latched once — and a SECOND
    //     finger landing here FORKS on that latch (the edge inventory below):
    //     a moved drag ignores it whole, a motionless hold UPGRADES to
    //     two-finger Nav.
    //   * Nav     — ONE or TWO fingers drive the touch-nav hooks (contract at
    //     set_touch_nav_hooks; touch_nav_single_ splits them). Single-finger
    //     Nav is the phone model's pan: the finger is the centroid, the
    //     distance stays degenerate so every frame carries dist_ratio 1.0. A
    //     second finger landing during SINGLE-finger nav UPGRADES it to the
    //     two-finger gesture IN PLACE — same hook stream, the delta bases
    //     REBASED to the join (folding the centroid's jump to the pair
    //     midpoint would pan by half the finger gap), so pan is continuous
    //     and zoom is relative to the join. The reverse is DELIBERATELY
    //     ASYMMETRIC: one finger lifting from TWO-finger nav ENDS the gesture
    //     (a lift is an end — the any-end-commits family) and the survivor is
    //     ignored until all fingers lift (Drain); no downgrade to
    //     single-finger nav. A third finger is ignored.
    //   * Region  — THE REGION HOLD (the eighth glass ruling, 2026-08-12):
    //     the zone window EXPIRED with the down point on the pan zone, and
    //     the owning finger now drives the REGION former through the region
    //     trio — begin fired at entry with the DOWN point (the GUI runs the
    //     former's own press half and arms the drag, or refuses), motion
    //     staged to the frame boundary and delivered as region_update(x, y),
    //     the lift delivering the staged final frame then region_end (any
    //     end commits — the former's own release regime). NOTHING
    //     pointer-shaped ever starts: no press, no hold bit, no release, no
    //     translation end — the single-finger Nav model, not the Pointer
    //     one. A second finger landing here is IGNORED whole (a committed
    //     gesture — the moved-drag rule's family).
    //   * Drain   — waiting for every remaining (ignored) finger to lift;
    //     nothing is delivered. Entered from Pointer, Nav and Region ends
    //     that leave ignored fingers on the glass.
    //
    // THE EDGE INVENTORY — every route that ends or transforms touch state,
    // authoritative here (the one-authoritative-site rule; each fire site
    // states only its own clause):
    //   * touch UP, owner, Pending  — a TAP, whichever deadline the window
    //     rides: resolve (enter-motion + press at
    //     the down point, any queued motion), then the release, then the
    //     translation end on the release's own edge; -> Idle (a lone finger
    //     cannot leave a survivor). On the navigation surface the burst's
    //     motionless press-release runs the GUI's DEFERRED CLICK ACT at its
    //     release — the placement at the tap point — and on the marker
    //     lane's empty stretch that release also seeds the EmptyLane
    //     double-click candidate, so TWO TAPS there reach the marker CREATE
    //     (the second tap's press consumes the candidate; nothing
    //     touch-side special-cases any of it).
    //   * window EXPIRY (sampled on the timerfd tick beside the key-repeat
    //     deadline, and lazily at every touch event's arrival) — FORK on the
    //     down point's captured pan-zone answer (the eighth glass ruling):
    //     ON the zone -> Region (the region hold at the kTouchRegionHoldMs
    //     beat; the begin hook fires at the down point, and any sub-slop
    //     drift inside the window stages as the gesture's first frame); OFF
    //     it -> Pointer at the kTouchDisambiguateMs mark
    //     (hold-unlocks-the-pointer, the Pending clause above).
    //   * motion beyond kTouchSlopPx inside the window — FORK on the down
    //     point's captured pan-zone answer (the phone model): pan surface ->
    //     SINGLE-FINGER Nav (the nav seed measures its latch from the DOWN
    //     point, so the first delivered frame folds the whole accumulated
    //     delta exactly as the two-finger latch folds); elsewhere -> Pointer
    //     (the crossing position delivered as the queued motion — a quick
    //     flag drag is the immediate marker drag, a quick trim drag trims).
    //   * second DOWN inside the window — Pending -> two-finger Nav, nothing
    //     delivered, whatever the zone answer.
    //   * second DOWN during Pointer — FORK ON THE MOVED LATCH (the sixth
    //     glass ruling, 2026-08-12 — the one piece of the timer-free model
    //     kept):
    //       - MOVED (a live drag — marker, region, trim, strip): IGNORED
    //         whole — recorded (the point count), not routed: mid-gesture
    //         finger-count changes do not mutate a committed gesture (the
    //         any-end-commits family; the architect's explicit mid-drag
    //         ruling).
    //       - MOTIONLESS (a hold): THE UPGRADE — the translation ends by
    //         ORDINARY RELEASE, the finger-up path's own shape through the
    //         one owner (staged motion flushed, release on the logical 1->0
    //         edge, the focus-forked translation end; the sibling-suppression
    //         rule applies identically), and the TWO-FINGER gesture seeds at
    //         the JOIN: both fingers' current positions are the gesture
    //         start, the latch measured from there. The hold's press already
    //         landed at the window's expiry, so the upgrade adds NO further
    //         jump — it only keeps a slow pinch (fingers landing further
    //         apart than the window) alive instead of dead; a sub-latch
    //         release of that pair delivers nothing more.
    //   * second DOWN during SINGLE-finger Nav — the UPGRADE (a transform,
    //     not an end; the end hook is not owed): touch_nav_single_ drops,
    //     the second finger is recorded, and the delta bases REBASE to the
    //     join; the latch state carries (a live pan does not re-latch). An
    //     undelivered staged single-finger frame is DROPPED at the join — a
    //     sub-frame sliver the rebase supersedes (recorded at the site).
    //   * second DOWN during Region — IGNORED whole: recorded (the point
    //     count), not routed — a committed gesture, the moved-drag rule's
    //     family (a region hold that expired into the gesture is as
    //     deliberate as a moved drag; there is no motionless sub-state to
    //     upgrade from, the hold having already become the former).
    //   * touch UP, owner, Pointer  — staged motion flushed, release on the
    //     logical left 1->0 edge at the last position, THE TRANSLATION END ON
    //     THAT SAME EDGE (codex round 2; one owner,
    //     deliver_touch_translation_end), which FORKS ON PHYSICAL POINTER
    //     FOCUS (codex round 3): the ordinary leave when no mouse rests in
    //     the window, an ordinary restore MOTION at pointer_x_/pointer_y_
    //     when one does — the finger lifted, so the unified pointer is where
    //     the mouse is, and the motion re-derives hover and the settled
    //     cursor from truth (the cursor-residue fix). A sibling source
    //     (physical BTN_LEFT / bare-`e`) still holding suppresses the
    //     release, the leave AND the restore — the unified pointer has not
    //     left, the mouse is mid-press, and the leave hook's clears (the
    //     row-8 held-arrow repeat, the popup's press claim) belong to that
    //     still-held press. A suppressed end can strand a hover face where
    //     the finger last was — the accepted-glitch class, self-healing on
    //     the next pointer event;
    //     -> Drain if ignored fingers remain, else Idle.
    //   * touch UP, any nav finger (single-finger nav: the owner) — the
    //     staged dirty frame DELIVERS
    //     first (Wayland orders the up before the wl_touch.frame that closes
    //     its batch, and that motion is the user's own final leg — the
    //     any-end-commits family; the end split at end_touch_nav_gesture),
    //     then the nav end (commits through the end hook iff an update was
    //     delivered). A nav gesture never held the logical button, so no
    //     release and no translation end fire — a nav born of the motionless
    //     hold's upgrade had its translation released at the join, and every
    //     other nav entry never delivered a press at all; -> Drain / Idle.
    //   * touch UP, owner, Region — the staged dirty frame DELIVERS first
    //     (the user's own final leg, the nav finger-up's model), then the
    //     region_end hook (any end commits — the former's release regime:
    //     the span rests under the sliver gate, or a motionless hold-lift
    //     rests nothing with the playhead where the begin seated it). No
    //     release and no translation end: nothing pointer-shaped ever
    //     started; -> Drain / Idle.
    //   * touch UP, ignored finger  — bookkeeping only.
    //   * third DOWN during two-finger Nav / any DOWN during Drain — ignored:
    //     recorded (the point count), not routed — the mid-gesture rule
    //     above.
    //   * wl_touch.cancel — the compositor claims the touches. One contract
    //     with capability loss: a live Pointer translation gets its release
    //     DELIVERED at the last position (a commit, not a vanish) then the
    //     translation end on the release's own edge (the UP clause's
    //     focus fork and sibling-suppression rule apply here identically —
    //     one owner); a live Nav
    //     gesture (single- or two-finger — one arm) DROPS its staged dirty
    //     frame
    //     (the compositor's claim means that motion retroactively was not
    //     ours — the recorded asymmetry with the Pointer release, which MUST
    //     deliver; the end split at end_touch_nav_gesture) and ends through
    //     its end path (commits iff an update was delivered); a live REGION
    //     gesture takes the Nav shape — staged frame DROPPED, then
    //     region_end (the end itself ALWAYS fires: the GUI-side region drag
    //     holds the drag-modal gate open and its release path is owed; only
    //     the final MOTION is dropped, and dropped motion wedges nothing);
    //     an unresolved
    //     Pending window is dropped silently (nothing was delivered, so
    //     there is nothing to end); all touch state forgotten either way.
    //   * TOUCH capability loss — the cancel contract above, then the
    //     wl_touch proxy is released. The pointer- and keyboard-capability
    //     edges do NOT touch this state (each source dies on its own
    //     stream's edges), and shutdown releases the proxy with no
    //     deliveries (destroy_wayland_state is not an input edge — the
    //     pointer and keyboard teardowns there deliver nothing either).
    //
    // TOUCH NEVER WRITES pointer_x_/pointer_y_ (a deliberate split): those
    // fields are wl_pointer's own — the wheel emits there, the bare-`e` press
    // lands there, the capture machinery rewrites them — and every touch
    // delivery carries its own explicit coordinates, so the unified "one
    // pointer" view lives in the DELIVERIES while each device's platform
    // state stays self-consistent. For the same reason touch does not clear
    // pointer_position_unknown_ (a touch says nothing about where the mouse
    // cursor is) and gates NO DELIVERY on pointer_focused_ (wl_touch delivers
    // only to the touched surface). The split has ONE deliberate reader since
    // codex round 3: deliver_touch_translation_end reads pointer_focused_ —
    // and pointer_x_/pointer_y_ on the focused arm — because the translation's
    // END is a MOUSE question ("is the mouse resting in the window, and
    // where"), not a touch delivery to gate.
    //
    // ANY CONTACT DOWN PAUSES THE FOLLOW CHASE — the one thing outside this
    // layer that reads the phase machine, through touch_contact_active()
    // above. THE HONEST PHONE-MODEL STATEMENT: a finger on the glass means the
    // user is aiming or gesturing, and the autopager must not move the song
    // under it. It is not a nicety — the window's whole design defers
    // conversion: a TAP delivers its press+release burst AT THE LIFT carrying
    // the DOWN point's coordinates (up to kTouchDisambiguateMs later off the
    // zone, up to kTouchRegionHoldMs on it) and the REGION HOLD converts the
    // down point at the beat's EXPIRY, so a chase that paged in between would
    // land the act on whatever frame had slid under that screen column instead
    // of the frame aimed at. Nothing GUI-side is armed during Pending, so
    // any_pointer_gesture_active cannot see that window — this query is what
    // covers it, along with the live translation, Nav, Region and Drain. DRAIN
    // IS IN BY CONSTRUCTION rather than by need (nothing converts during a
    // drain): the rule is "any contact down", which needs no phase list to
    // stay true as phases are added.
    struct wl_touch* wl_touch_ = nullptr;
    enum class TouchPhase { Idle, Pending, Pointer, Nav, Region, Drain };
    TouchPhase touch_phase_       = TouchPhase::Idle;
    // ALL live touch points, the ignored ones included — the Drain exit test.
    int        touch_point_count_ = 0;
    // Pending/Pointer: the translating finger. Positions are surface-local
    // doubles (touch positions are fractional on real panels); deliveries
    // round through std::nearbyint, the project's one fractional->integer
    // conversion rule.
    int32_t    touch_owner_id_    = 0;
    double     touch_down_x_      = 0.0;
    double     touch_down_y_      = 0.0;
    double     touch_last_x_      = 0.0;
    double     touch_last_y_      = 0.0;
    // Pending only: the disambiguation deadline (monotonic; event timestamps
    // ride a base this program never compares against — kTouchRegionHoldMs
    // on the pan zone, kTouchDisambiguateMs off it, the two-deadline fork)
    // and whether any sub-slop motion arrived inside the window (the queued
    // motion the resolution replays).
    uint64_t   touch_window_deadline_us_ = 0;
    bool       touch_window_moved_       = false;
    // Pending: the down point's PAN-ZONE answer, captured ONCE at the first
    // finger's down (the pan_zone query at set_touch_nav_hooks). It picks
    // the window's deadline, and BOTH one-finger resolutions fork on it:
    // the slop crossing (the phone model's pan vs the pointer) and the
    // expiry (the region hold vs the pointer unlock).
    bool       touch_down_in_pan_zone_   = false;
    // Region: a finger position staged for the wl_touch.frame boundary
    // (the Nav dirty-frame cadence; delivered as region_update(x, y)).
    bool       touch_region_frame_dirty_ = false;
    // Pointer: the translation has MOVED — Chebyshev >= kTouchSlopPx from the
    // down point, LATCHED ONCE (the sixth glass ruling, 2026-08-12). Seeded
    // by the resolver from the window's own travel (a slop-crossing
    // resolution enters already moved, expiry enters motionless), latched by
    // the Pointer motion arm afterward; the second-down fork reads it —
    // moved = ignore, motionless = the upgrade.
    bool       touch_translation_moved_  = false;
    // The logical left's third source (see the OR-edge model above).
    bool       touch_left_held_          = false;
    // Pointer: a motion staged for the wl_touch.frame boundary (the
    // pointer-frame coalescing precedent; flushed ahead of the release).
    bool       touch_frame_motion_pending_ = false;
    // Nav: ONE finger (the phone model's pan — the finger is the centroid and
    // the distance fields stay degenerate) vs two. touch_nav_id2_ and the
    // second finger's positions are meaningful only while this is false.
    bool       touch_nav_single_ = false;
    // Nav: the second finger and the per-finger latest positions.
    int32_t    touch_nav_id2_   = 0;
    double     touch_nav_x1_    = 0.0;
    double     touch_nav_y1_    = 0.0;
    double     touch_nav_x2_    = 0.0;
    double     touch_nav_y2_    = 0.0;
    // Nav: the gesture START (the latch reference — the two-finger seed, the
    // single-finger down point, or an upgrade's join) and the last DELIVERED
    // centroid/distance (the per-frame delta basis). Single-finger nav keeps
    // the distance fields at 0.0.
    double     touch_nav_start_cx_   = 0.0;
    double     touch_nav_start_cy_   = 0.0;
    double     touch_nav_start_dist_ = 0.0;
    double     touch_nav_last_cx_    = 0.0;
    double     touch_nav_last_dist_  = 0.0;
    bool       touch_nav_latched_    = false;
    // An update hook fired — the end hook is owed a commit.
    bool       touch_nav_delivered_  = false;
    // Positions moved this wl_touch frame (compute + deliver at the frame).
    bool       touch_nav_frame_dirty_ = false;

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
    // surface means that name was missing from the theme: apply_cursor_kind
    // falls back to Arrow's, and Arrow's own null (a theme with no left_ptr, or
    // no theme at all) is the protocol hide.
    struct ThemeCursor {
        struct wl_surface* surface   = nullptr;
        int32_t            hotspot_x = 0;
        int32_t            hotspot_y = 0;
    };
    struct wl_cursor_theme* wl_cursor_theme_ = nullptr;
    ThemeCursor             cursors_[kGuiCursorKindCount];

    // The kind last asked for. THE ONE PLACE the current cursor is recorded —
    // every applier reads it and none takes a kind as an argument, so a re-apply
    // on an enter or a capture release restores exactly what this field last
    // recorded. That is NOT "whatever was showing": a capture STAMPS this field
    // with the gesture's own cue and then hides the cursor, so the release
    // deliberately hands back a kind (Zoom, Pan) that may never have been on
    // screen before the drag. It outlived the custom-buffer cursor it was written
    // for and is load-bearing for those two platform-side edges, which know
    // nothing about where the pointer is in the GUI's terms.
    // TWO WRITERS: set_cursor_kind (the GUI's answer for a REAL position) and
    // begin_pointer_capture's stamp on the lock-request path (the gesture's own
    // cue, written for the release that will hand it back; why a requested lock
    // counts is at that contract). Nothing else.
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
    KeyReleaseCallback   on_key_release_;
    ButtonCallback       on_button_press_;
    ButtonCallback       on_button_release_;
    WheelCallback        on_wheel_;
    MotionCallback       on_motion_;
    CloseCallback        on_close_;
    WheelContextProbe    wheel_context_probe_;
    TextEditorProbe      text_editor_active_probe_;
    RepeatEligibleProbe  repeat_eligible_probe_;
    // The one owner of the pointer-leave drop: fired at wl_pointer.leave, at
    // pointer-capability loss, and at a touch translation end's NO-FOCUS arm
    // (the round-3 fork at deliver_touch_translation_end — a focused mouse
    // gets a restore motion, not this hook): the focus-dropping edges with no
    // motion to
    // re-resolve — permanently on capability loss, and for the duration of the
    // absence on an ordinary leave. Wired to everything derived from where the
    // pointer is, main.cpp's hook body holding the authoritative list; the marker
    // hover popup it also dropped no longer exists. It carries WHICH of the two
    // edges fired it, because a consumer may keep state across the soft one and
    // none across the hard one (GuiPointerLeaveReason). Null-safe.
    std::function<void(GuiPointerLeaveReason)> pointer_left_hook_;
    // Fired at each window_activated_ EDGE (see set_activation_changed_hook).
    std::function<void()> activation_changed_hook_;
    // Fired at the platform's consumed keyboard edges (see
    // set_keyboard_intent_cancel_hook). Null-safe at each fire site.
    std::function<void()> keyboard_intent_cancel_hook_;
    // The touch navigation hooks (see set_touch_nav_hooks). Null-safe at
    // each fire site.
    std::function<void(int, int, double, double)> touch_nav_update_hook_;
    std::function<void()>                         touch_nav_end_hook_;
    // The pan-zone query (asked once, at the first finger's down; surface
    // geometry only — the contract at set_touch_nav_hooks).
    std::function<bool(int, int)>                 touch_pan_zone_hook_;
    // The region trio the Region phase drives (contracts at
    // set_touch_nav_hooks). Null-safe at each fire site.
    std::function<void(int, int)>                 touch_region_begin_hook_;
    std::function<void(int, int)>                 touch_region_update_hook_;
    std::function<void()>                         touch_region_end_hook_;
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
    // THE KEYBOARD'S MODELED STATE GOING AWAY, and the one place that spells it.
    // Two edges reach it — wl_keyboard.leave and keyboard-capability loss — and
    // both mean the same thing: no further key event can arrive on the state
    // built so far, so the modifier bits, the repeat arm, the wheel sub-detent
    // remainder and a held synthesized-left button are all dropped together.
    // Hoisted so a third such edge cannot land with one of the four forgotten.
    void forget_keyboard_state();
    // A raw xkb keycode -> the GuiKey the application sees, or false when this
    // GUI has no use for the key at all (standalone modifiers, F1..F35, an
    // unmapped keycode). THE ONE translation, read by the press path and by the
    // release path, so a release can never disagree with its press about which
    // key it is; the drop classes and their reasoning are at the definition.
    bool key_from_keycode(uint32_t xkb_keycode, GuiKey& out) const;
    void deliver_key(GuiKey key, GuiInputState mods);
    void maybe_fire_repeat();
    // THE LIVE MODIFIER TRUTH, on demand — the same GuiInputState every pointer
    // callback and the loop-settled hook are built from, read out of the tracked
    // ctrl/shift/alt bits and the logical left-button hold.
    // PRIVATE AGAIN since 2026-08-03: it was public for exactly two callers,
    // main.cpp's WM-close and resize callbacks, which carried no modifier state
    // of their own and had to re-derive the pointer cursor after force-ending a
    // gesture. The per-iteration cursor owner deleted both calls, and the hook
    // that replaced them is HANDED this state rather than fetching it — so the
    // door is closed rather than left standing with nothing asking for it (the
    // pointer_focused() note above records the same disposal).
    // THE STANDING SPLIT, which is why this is read live and never stashed: the
    // POSITION must be remembered (only pointer events carry it) and the
    // MODIFIERS must not (they are live here) — end_left_hold_source documents
    // the same split for its synthesized release. A remembered copy of the
    // modifiers would be a second representation of a fact this object owns,
    // with its own staleness invariant to keep.
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
    // Every button-delivery site calls this immediately before delivering; the
    // classes, re-derived by grep: the wl_pointer button listener, the
    // kLeftClickKey synth edges, the HARD-EDGE hold ends — keyboard leave /
    // keyboard-capability loss (forget_keyboard_state) and pointer-capability
    // loss (on_seat_capabilities, which ends BOTH keyboard-adjacent sources) —
    // which reach it inside end_left_hold_source rather than calling it
    // themselves, and the TOUCH translation's two delivery sites (the
    // resolution press in resolve_touch_window_to_pointer and the release in
    // end_touch_left_hold), which also flush their own staged touch motion
    // first.
    void flush_deferred_motion();

    // Ends one of the two KEYBOARD-ADJACENT sources' contribution to the
    // logical left hold. The logical left is a THREE-source OR —
    // pointer_left_held_ || synth_left_held_ || touch_left_held_ — whose one
    // authoritative statement is the OR-edge model at synth_left_held_'s
    // declaration; this helper ends the physical or the synth bit only (the
    // TOUCH bit has its own end, end_touch_left_hold, which takes the same
    // ordering). Delivers the release only on the logical 1->0 edge — i.e.
    // when NEITHER sibling source is still held — and encodes the ordering
    // invariant ONCE: flush the deferred motion FIRST, while this source's
    // bit still reads held, so the flushed motion observes the pre-release
    // held state and takes the live-drag path, not the button-lost teardown;
    // then clear the bit; then deliver at the last known pointer coordinates
    // with current_mods(). When no edge occurs (a sibling source still
    // holds), just clear — the last surviving source's own end delivers the
    // single edge. physical selects pointer_left_held_
    // (true) vs synth_left_held_ (false, which also clears synth_left_keycode_).
    void end_left_hold_source(bool physical);

    // -- Touch handlers (wl_touch as the pointer; the phase machine and edge
    // inventory are at the touch state block above) --
    void on_touch_down(uint32_t serial, uint32_t time, int32_t id,
                       int32_t fx, int32_t fy);
    void on_touch_up(uint32_t serial, uint32_t time, int32_t id);
    void on_touch_motion(uint32_t time, int32_t id, int32_t fx, int32_t fy);
    void on_touch_frame();
    void on_touch_cancel();
    // Resolve the Pending disambiguation window to the POINTER translation:
    // deliver the synthesized enter-motion at the ORIGINAL down point, the
    // left press there (on the logical OR's 0->1 edge), then any queued
    // motion; seed the MOVED latch from the window's own travel. Shared by
    // all three pointer resolutions — the OFF-ZONE expiry (the on-zone one
    // takes resolve_touch_window_to_region below), the
    // slop crossing's outside-the-pan-zone arm (its pan-surface arm takes
    // resolve_touch_window_to_single_nav below instead), and the tap (whose
    // caller then delivers the release + the translation end itself, through
    // deliver_touch_translation_end).
    void resolve_touch_window_to_pointer();
    // Resolve the Pending window to SINGLE-FINGER NAV instead (the phone
    // model's slop-crossing arm, taken when the captured down point lay in
    // the pan zone): seed the Nav state from the DOWN point, unlatched, so
    // the first delivered frame runs the ordinary latch test and folds the
    // whole accumulated delta — "latched and folded exactly as the
    // two-finger latch is", literally the same code path. No press was ever
    // delivered and the touch hold is never raised.
    void resolve_touch_window_to_single_nav();
    // Resolve the Pending window to the REGION HOLD (the on-zone expiry at
    // the kTouchRegionHoldMs beat — the eighth glass ruling): fire the
    // region begin at the DOWN point and stage any sub-slop window drift as
    // the gesture's first frame. Nothing pointer-shaped starts — no press,
    // no hold bit — the single-nav model.
    void resolve_touch_window_to_region();
    // Sample the Pending window's deadline (whichever of the two the zone
    // answer picked). Called from the timerfd tick
    // (beside maybe_fire_repeat — the run loop's one deadline-sampling spot,
    // so expiry lands within one tick of its mark) and lazily at every
    // touch event's arrival, so an event past the deadline is processed
    // against the resolved state.
    void maybe_resolve_touch_window();
    // Deliver the staged Pointer-phase motion (wl_touch.frame coalescing) —
    // the touch twin of flush_deferred_motion, called at the frame boundary,
    // ahead of every touch button delivery, and UNCONDITIONALLY at every end
    // of the touch hold (the flush-vs-clear invariant is at the definition).
    void flush_touch_frame_motion();
    // End the touch source's contribution to the logical left hold: flush the
    // staged TOUCH motion unconditionally while the bit still reads held (it
    // is independent of the logical-left OR — the flush owner's invariant),
    // flush the deferred POINTER motion on the delivering edge, clear the
    // bit, then deliver the release at the owner's last position only on the
    // logical 1->0 edge (the end_left_hold_source ordering, third source).
    // RETURNS whether the release was delivered — the translation end's own
    // edge: both end sites go through deliver_touch_translation_end below,
    // which acts iff the release fired (a sibling-suppressed release means
    // the unified pointer has not left; the rationale is at the definition).
    bool end_touch_left_hold();
    // The one owner of the translation's END, called by the touch-up site
    // and the hard-end contract: end the touch hold, and on the delivering
    // edge FORK ON PHYSICAL POINTER FOCUS (codex round 3) — a mouse resting
    // in the window gets an ordinary restore MOTION at its last
    // platform-tracked position instead of the leave (the finger lifted, so
    // the unified pointer is where the mouse is; the cursor-residue fix), no
    // mouse focus gets the ordinary leave as before. Rationale, edges and
    // the armed-anchor judgment at the definition.
    void deliver_touch_translation_end();
    // Compute + deliver the Nav frame's centroid/distance update through the
    // update hook (latch, fold-at-crossing, per-frame deltas — the contract
    // is at set_touch_nav_hooks). Single-finger nav forks only the three
    // input reads: the finger is the centroid and the distance stays 0.0, so
    // the pinch latch arm is structurally false and the ratio guard delivers
    // 1.0 — everything downstream is shared verbatim.
    void deliver_touch_nav_frame();
    // End a live Nav gesture. deliver_final_frame selects the END SPLIT
    // (recorded at the definition): the finger's own lift delivers the staged
    // dirty frame first (true — the user's final motion, the any-end-commits
    // family), the hard ends drop it (false — the compositor's claim means
    // that motion retroactively was not ours). Either way the end hook fires
    // iff an update was delivered.
    void end_touch_nav_gesture(bool deliver_final_frame);
    // End a live Region gesture — the Nav end's own split, restated for the
    // region trio (the finger's lift delivers the staged frame first, the
    // hard ends drop it) with ONE deliberate difference: region_end fires
    // UNCONDITIONALLY, the GUI-side drag holding the drag-modal gate open
    // (the trim-move precedent; rationale at the definition).
    void end_touch_region_gesture(bool deliver_final_frame);
    // THE HARD-END CONTRACT, shared verbatim by wl_touch.cancel and
    // touch-capability loss (the edge inventory names it): commit-and-END a
    // live Pointer translation (the release, then the focus-forked translation
    // end through deliver_touch_translation_end), end a live Nav or Region
    // gesture (staged frame dropped), drop a Pending window silently, forget
    // all touch state.
    void hard_end_touch_stream();
    // Reset every touch field to its rest value (the one forget, so a new
    // edge cannot land with one field remembered).
    void forget_touch_state();

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
