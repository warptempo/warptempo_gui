#include "input_core.h"

#include <time.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

uint64_t gui_monotonic_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000ull +
           static_cast<uint64_t>(ts.tv_nsec) / 1000ull;
}

namespace {

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
// common"). A down on the navigation surface runs its window to this
// deadline, and the EXPIRY there is the REGION HOLD: the region former armed
// through the region hooks, so hold-then-drag sweeps a region on glass. The
// duration is kHoldBeatMs (gui_input.h), the product's ONE hold beat — the
// same number the chrome roster's shift long press reads, matched by
// convention with the compositor's key-repeat delay, so it is a beat the hand
// already knows from every held key on the desktop — and it is long past any
// aimed drag's natural dwell, the lesson
// of the dead kTouchTrimHoldMs (the trim band's hold-a-beat deadline of
// 2026-08-11, whose first cut rode the 60 ms window and turned every
// deliberate band drag into the trim move; that GESTURE stayed dead — this
// beat revives only its two-deadline PATTERN, on a surface whose quick drag
// is the pan, not a pointer drag, so the dwell collision cannot recur).
//
// THE SLOP IS NO LONGER A CONSTANT HERE. It is touch_slop_px_, a settable
// member whose default is the authored kDefaultTouchSlopPx and whose live
// value the GUI pushes down scaled (set_touch_slop_px, input_core.h — the
// contract, the three uses and the twin-gate invariant are all stated there,
// since the value's owner is now the door rather than this block).
constexpr int    kTouchDisambiguateMs = 60;
constexpr int    kTouchRegionHoldMs   = kHoldBeatMs;

} // namespace

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

void GuiInputCore::set_surface_width(int width_px) {
    surface_width_ = width_px;
}

// ---------------------------------------------------------------------------
// The two software deadlines
// ---------------------------------------------------------------------------

void GuiInputCore::tick() {
    maybe_fire_repeat();
    // The touch disambiguation window's deadline rides the same tick the
    // key-repeat deadline does (its lazy twin runs at each touch event's
    // arrival; granularity is recorded at the constants).
    maybe_resolve_touch_window();
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

// THE ONE TEARDOWN FOR "the keyboard's modeled state is gone", called from both
// edges that mean it: the keyboard focus leaving (wl_keyboard.leave on
// Wayland) and keyboard-capability loss. Each site
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
// same iteration — a keyboard leave and a capability loss both arrive as
// dispatched events, so neither can outrun the boundary. A fire of its own
// would only be an earlier answer to the same question, from a spot that would
// then owe an ordering rule about the teardown below it.
void GuiInputCore::forget_keyboard_state() {
    // A MODIFIER EDGE IS A DELIVERY BOUNDARY FOR STAGED CAPTURED MOTION, and
    // this is the SECOND route that moves the modeled bits — the argument is
    // recorded once, at set_modifiers' own flush. Losing the key
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

void GuiInputCore::key_event(GuiKey key, uint32_t stable_code, bool pressed,
                             uint32_t codepoint) {
    if (!pressed) {
        // Cancel repeat if the released key was the one repeating.
        if (stable_code == repeat_keycode_) {
            repeat_key_ = 0;
        }
        // End a synthesized-left hold on its owning key's release. The
        // stable-code match means a kLeftClickKey press typed into an editor —
        // which never started a hold — has no effect here (synth_left_keycode_
        // is 0 unless a hold is live). The release is NEVER gated: not by the
        // editor probe (an editor opened mid-hold must not orphan the button)
        // and not by pointer focus (it lands at the last known coordinates).
        // THIS IS NOT THE HOLD'S ONLY END, and the code match is what makes
        // the others cost nothing here: the two HARD edges end it themselves —
        // keyboard leave / keyboard-capability loss (forget_keyboard_state) and
        // POINTER-capability loss (pointer_capability_lost, which must, the
        // gesture being a pointer gesture) — and each zeroes
        // synth_left_keycode_ as it goes, so the keyup that eventually arrives
        // matches nothing and is a plain no-op rather than a second release.
        if (stable_code == synth_left_keycode_) {
            end_left_hold_source(/*physical=*/false);
            // That release WAS the mouse button's, so it is not a key release
            // as well: the press it matches was swallowed here too, and
            // delivering this one would hand the application an unpaired edge.
            return;
        }
        // THE APPLICATION-SIDE KEY RELEASE (2026-08-13). It exists for exactly
        // one consumer — the modal dialog's keyboard press-and-hold, whose act
        // is at the lift — and it is delivered for the same key identity the
        // press carried, through the backend's one translation, which both
        // branches read. NOT gated on Super (a release binds nothing on its
        // own; the reasoning is at set_on_key_release, input_core.h) and
        // carrying no modifier state at all. A key this GUI has no use for
        // arrives as 0 and is not a delivery.
        if (key != 0 && on_key_release_) on_key_release_(key);
        return;
    }

    // Pressed. A key with no GuiKey identity is nothing to deliver.
    if (key == 0) return;

    // kLeftClickKey emulates BTN_LEFT at this boundary, so downstream it IS
    // the mouse and inherits every mouse gate (read-only tabs, drag gates,
    // prompt/editor modality) for free. The editor probe is consulted at
    // PRESS time ONLY: when a text editor is open the key stays a normal
    // letter and falls through to delivery AND repeat arming below (a held
    // letter repeats in the editor like any key). Otherwise it is the button
    // and is swallowed entirely as a key event — no delivery, no repeat
    // arming (a held button must not machine-gun re-press). pointer_focused_
    // gating means a press with the pointer off the window silently no-ops, as
    // a real left-button press would not be delivered to this surface either.
    // Any modifier state rides along to the synthesized button, exactly as it
    // would for a physical left-button device (see kLeftClickKey's comment).
    if (key == kLeftClickKey &&
        !(text_editor_active_probe_ && text_editor_active_probe_())) {
        if (!synth_left_held_ && pointer_focused_) {
            // The synthesized button is a button: this press is a context event
            // that kills an armed key repeat (layer 1), same as a physical one.
            repeat_key_ = 0;
            // Logical state before this source's edge: synth is false here, so
            // the OR is the other two sources (a touch hold counts — three
            // devices sharing the left button never double-deliver).
            const bool was_held = pointer_left_held_ || touch_left_held_;
            synth_left_held_    = true;
            synth_left_keycode_ = stable_code;
            if (!was_held && on_button_press_) {
                flush_deferred_motion();
                on_button_press_(GuiMouseButton::Left,
                                 pointer_x_, pointer_y_, current_mods());
            }
        }
        return;
    }

    GuiInputState mods = current_mods();
    mods.codepoint = codepoint;
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
        repeat_keycode_    = stable_code;
        repeat_editor_ctx_ = editor_ctx;
        repeat_due_us_     = gui_monotonic_us() + repeat_delay_us_;
    } else {
        repeat_key_ = 0;
    }
}

void GuiInputCore::set_modifiers(bool ctrl, bool shift, bool alt, bool super) {
    // THE THREE MODELED MODIFIERS ARE READ FIRST AND ASSIGNED BELOW, because
    // the gap between the two is a DELIVERY BOUNDARY.
    const bool next_ctrl  = ctrl;
    const bool next_shift = shift;
    const bool next_alt   = alt;
    const bool modeled_edge = next_ctrl  != mod_ctrl_ ||
                              next_shift != mod_shift_ ||
                              next_alt   != mod_alt_;

    if (modeled_edge) {
        // STAGED CAPTURED MOTION IS DELIVERED UNDER THE MODIFIER STATE IT
        // ARRIVED IN. A captured drag defers its relative motion to the
        // pointer_frame boundary (relative_motion) and the
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
    // SUPER, tracked for ONE purpose: gating key PRESS delivery (deliver_key;
    // releases are ungated by ruling). It is
    // deliberately absent from current_mods() and from the scroll-chord reset
    // below — the wheel chords are plain and Ctrl only, so a Super press changes
    // no chord and must not drop an accumulating sub-detent remainder.
    mod_super_ = super;

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
        // scroll frame that would re-probe. THREE live wheel chords since
        // 2026-08-27 (the plain magnification step, the Alt stepped pan and the
        // Ctrl zoom step), which is
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

void GuiInputCore::set_repeat_info(int32_t rate, int32_t delay) {
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

GuiInputState GuiInputCore::current_mods() const {
    GuiInputState s;
    s.ctrl  = mod_ctrl_;
    s.shift = mod_shift_;
    s.alt   = mod_alt_;
    // Logical left button: the physical left button, the kLeftClickKey
    // synthesized hold, or the touch translation's hold. Drags consult this
    // bit on motion; without the OR a synthesized-key or touch drag tears on
    // the first motion event.
    s.primary_button_held =
        pointer_left_held_ || synth_left_held_ || touch_left_held_;
    return s;
}

void GuiInputCore::deliver_key(GuiKey key, GuiInputState mods) {
    // SUPER IS DROPPED AT THE PLATFORM BOUNDARY (architect 2026-07-30: super
    // belongs to labwc and is not part of this program's vocabulary). STRICT
    // MODIFIER VALIDATION says an unbound modifier combination is a no-op
    // everywhere (architect 2026-07-28) — and it was FALSE for Super, because the
    // mask never modelled it: every bare-exact predicate reads three bools, so a
    // held Logo left mods == {false,false,false} and Super+Escape reached the
    // editors' cancel, Super+Return their commit. labwc grabs many Super chords,
    // but Escape / Return / Space / Delete / the arrows / Home / End are not among
    // them and arrive here. Rather than add a fourth bool to every reader, the
    // event is dropped HERE. WHAT THAT MAKES TRUE BY CONSTRUCTION IS EXACTLY WHAT
    // IS CONSTRUCTED: no Super-carrying PRESS is ever delivered, so no Super chord
    // can reach a binding — "the GUI binds no Super chord" is the rule, for the one
    // modifier this program never binds. IT IS NOT THE WIDER CLAIM THAT NOTHING CAN
    // HAPPEN WHILE SUPER IS PHYSICALLY DOWN (architect 2026-08-16, ruling the corner
    // KEPT: "super is the desktop's; the GUI basically ignores it — Super usage
    // inside the GUI is outside the GUI's providence"). An act the user ARMED BEFORE
    // Super went down — the modal Enter/Space arm, the product's one armed keyboard
    // act — completes at its release on its own terms. That is the
    // GUI running its own command, not a Super chord, and refereeing what the user
    // does with the desktop's modifier meanwhile is not this program's business. THE
    // CORNER'S HOME is set_on_key_release's contract (input_core.h), which
    // carries the argument for leaving the release path ungated.
    // THIS IS THE SHARED DELIVERY PATH, which is what makes one gate enough: both
    // the physical press (key_event) and the SYNTHESIZED REPEAT
    // (maybe_fire_repeat) come through here. A key held BEFORE Super went down
    // keeps its armed repeat — a modifier keysym is dropped ahead of the repeat
    // arming, so it disarms nothing, and the eligibility re-probe cannot see Super
    // either — so its repeats keep firing and are simply swallowed here for as long
    // as Super is held, resuming when it is released. That is exactly the intent,
    // and it is safe for undo coalescing: no keyboard command can run in the gap
    // (every press is dropped), and a pointer press in the gap disarms the repeat
    // outright through layer (1) of the repeat contract. The ruled corner above —
    // the modal Enter/Space arm COMPLETING at its release inside the gap — is
    // no hole in that either: that arm's own arming press was itself a layer-(1)
    // edge (a different key press re-arms or disarms; the same key's release cancels
    // the repeat), so no burst older than the act can outlive it.
    // KEY RELEASES do not come through here: key_event's release branch
    // feeds the repeat cancel and the synthesized-left hold end — neither of
    // which may be skipped, or a hold would orphan — and then delivers the
    // release to the application's release hook (the modal arm's feed) DIRECTLY,
    // deliberately ungated on Super (the argument is at set_on_key_release,
    // input_core.h: a release can only resolve an arm an
    // already-delivered press created, so the drop below is enough).
    // POINTER EVENTS ARE OUT OF SCOPE by the same ruling, and that includes
    // the kLeftClickKey synthesized button: `e` IS the left mouse button at this
    // boundary and returns above without reaching this function, so Super+`e`
    // behaves exactly like Super+click, which binds nothing here either way.
    if (mod_super_) {
        // THE DROPPED PRESS IS STILL AN INTERVENING KEY ARRIVAL for the
        // application's held key intent (the modal dialog's keyboard press
        // arm; main.cpp's keyboard-intent cancellation hook body is the
        // authoritative effect list) — no application
        // disarm ever sees it — and the platform's own layer-1 disarmed its armed repeat at
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

void GuiInputCore::maybe_fire_repeat() {
    if (repeat_key_ == 0 || repeat_period_us_ == 0) return;
    const uint64_t now = gui_monotonic_us();
    if (now < repeat_due_us_) return;

    // Deliver one synthesized repeat. Then advance repeat_due_us_ by
    // repeat_period_us_. If we missed multiple periods (e.g. the main
    // thread was slow), deliver only one and resync — bursting wouldn't
    // serve the user.
    GuiInputState mods = current_mods();
    // THE CODEPOINT IS RE-RESOLVED PER FIRE, never reused from the press: the
    // live keyboard state may have moved under the held key. The backend
    // answers for its own keymap (contract at set_codepoint_probe).
    if (codepoint_probe_) mods.codepoint = codepoint_probe_(repeat_keycode_);
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
    // THE REPEAT BIT — stamped after the eligibility re-probe so the probes stay
    // a function of key+modifiers alone. It lets the application tell a held
    // key's continuation presses from fresh physical ones, and UNDO COALESCING
    // IS BUILT ON IT (Undo::coalesce_gesture): a burst carrying this bit merges
    // with NO clock test at all, which is the arm that must work at any
    // compositor repeat delay. (A physical press takes the other arm of the
    // hybrid — the fixed 500 ms tap window — and this bit is exactly what
    // separates the two.)
    // THE BIT HAS TWO PRODUCERS, one per held surface: THIS SITE for a held KEY,
    // and GuiInputHandler::tick_chrome_press_repeat (input_pointer.cpp) for a
    // held BUTTON — the four cardinal arrow buttons and the waveform
    // magnification pair, whose hold-repeat returned 2026-08-16. The button producer never passes through this class (it calls
    // on_key application-side) and buys the adjacency property below from
    // its own edge, so nothing here has to account for it; this site is the KEY
    // surface's whole producer.
    // ONLY THE BUTTON PRODUCER CLEARS THE BIT ON ITS HOLD'S FIRST FIRE — its
    // burst's undo OPENER, standing in for the press act a chrome button does
    // not perform (its act is at the LIFT since 2026-08-13; the flip and its
    // argument are at that producer's own head). A held KEY needs no flip: its
    // physical press acts and IS the burst's opener, so every fire from this
    // site carries the bit as stamped.
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

void GuiInputCore::pointer_enter(double x, double y) {
    pointer_focused_ = true;
    pointer_x_ = containing_pixel(x);
    pointer_y_ = containing_pixel(y);
    // AN ABSOLUTE POSITION IS THE TRUTH COMING BACK: whatever a past capture left
    // virtual is superseded here, so cursor kinds are recorded again — before the
    // synthesized motion below, whose whole job is to re-derive one. Guarded on
    // !pointer_captured_ because a lock's own virtual travel outranks a stray
    // enter (the lock keeps the pointer on this surface, so it should not arrive).
    if (!pointer_captured_) pointer_position_unknown_ = false;

    // Synthesize a motion delivery so consumers register the pointer
    // as present at the entry coordinates. Matches how most clients
    // treat enter — the first "the pointer is here" notification.
    deliver_motion(pointer_x_, pointer_y_);
}

void GuiInputCore::pointer_leave() {
    pointer_focused_ = false;
    // Fire the leave hook: no motion arrives WHILE the pointer stays outside, so
    // without this a redesigned row's button would keep its lit face for that
    // whole span after the pointer slid out the window edge. This is NOT the
    // capability-loss case — the stream is not over, re-entry fires a synthesized
    // motion (pointer_enter) that re-resolves, and the held state below
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
    // pointer_frame boundary (relative_motion) — and the frame that
    // TERMINATES this leave runs pointer_frame's pending block AFTER the hook
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
    // last_x stays at the press until the crossing — so dropping the event
    // that CROSSES the Chebyshev threshold would leave `moved` false and the
    // release would commit nothing at all, losing the whole gesture rather than
    // one frame of travel. (Positionally a drop would cost nothing —
    // pointer_x_/y_ are written eagerly at stage time and last_x spans any
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

void GuiInputCore::pointer_capability_lost() {
    // THE HARD END OF THE POINTER STREAM: no leave, no motion and no release
    // will arrive from this device again. Ordered hook and holds, one clause
    // each, and both are owed BEFORE the backend tears its pointer objects
    // down (the split is at the declaration). THE HOOK LEADS because its
    // clear_dropdown_pointer_state drops the popup's press claim and armed
    // item, so a held menu item does not fire on a hardware edge — a menu item
    // is a click convention, not a drag commit (architect 2026-08-08), while
    // the no-cancel ruling governs the GESTURES. THE HOLDS END NEXT, both
    // sources — the synthesized bare-`e` one included, its gesture being a
    // pointer gesture in all but origin — because an undelivered release would
    // latch the drag-modal gate with no event left to lift it; the pair
    // delivers AT MOST one release (each fires only on the logical OR's 1->0
    // edge, and a live TOUCH hold — the OR's third source — keeps the logical
    // button down, its own stream owning the eventual release) and zeroes
    // synth_left_keycode_, so the later `e` keyup matches nothing. THEY
    // PRECEDE the backend's capture release because unlocking rewrites the
    // tracked position to the cursor restore hint, and the release — with a
    // moved strip drag's final apply riding it — must read the drag's true last
    // position; the GUI's own release body then tears the capture down from
    // inside, leaving the backend's call a no-op backstop for a lock no hold
    // owned.
    //
    // HANDLED TO NOT-BROKEN, NOT TO POLISHED (architect 2026-08-09): after a
    // capability loss, a staged-motion flush may leave pointer_in_window true
    // and a hover face stale until the pointer next enters; accepted, do not
    // re-guard.
    if (pointer_left_hook_)
        pointer_left_hook_(GuiPointerLeaveReason::CapabilityLoss);
    end_left_hold_source(/*physical=*/true);
    end_left_hold_source(/*physical=*/false);
}

void GuiInputCore::forget_pointer_state() {
    pointer_focused_ = false;

    // A sub-detent carry and the staged half of a logical pointer frame
    // belong to the destroyed pointer device — the staged relative motion
    // included, since it was staged on a relative stream that is gone too.
    // They must not combine with input from a pointer created when the seat
    // regains the capability, even if cursor region and modifiers happen to
    // match.
    scroll_accum_       = 0.0;
    scroll_context_key_ = 0;
    frame_v120_accum_   = 0.0;
    frame_axis_accum_   = 0.0;
    frame_have_v120_    = false;
    frame_have_axis_    = false;
    frame_have_relmotion_ = false;
}

void GuiInputCore::pointer_motion(double x, double y) {
    pointer_x_ = containing_pixel(x);
    pointer_y_ = containing_pixel(y);
    // The pointer's real position, from the backend: the post-capture unknown
    // span ends here (same rule and same guard as the enter above), and this
    // iteration's tail re-derives the cursor for it once the delivery below has
    // recorded it — which is why the clear owes no cursor call of its own.
    if (!pointer_captured_) pointer_position_unknown_ = false;
    deliver_motion(pointer_x_, pointer_y_);
}

void GuiInputCore::note_notional_pointer_x(double x) {
    // ONE clamp body for every writer of the pointer's notional position (the
    // contract, and why there is exactly one such position, are at the field).
    const double max_x =
        surface_width_ > 0 ? static_cast<double>(surface_width_ - 1) : 0.0;
    // AND IT IS THE BACKSTOP RATHER THAN THE RULE for a captured pointer: the
    // capture's WRAP (relative_motion) folds an overshoot back in at
    // the waveform's opposite bound before the value ever gets here, so this
    // clamp bites only on a pathological delta larger than the whole span.
    // This body is deliberately NOT the wrap's owner: it serves every writer,
    // and an absolute delivery from the backend is a real position that
    // must be stored as given rather than folded.
    notional_pointer_x_ = std::clamp(x, 0.0, max_x);
}

void GuiInputCore::deliver_motion(int x, int y) {
    // The funnel for every motion that carries a REAL position (the caller
    // classes are at the declaration): the notional position follows it, then
    // the GUI sees the ordinary delivery. Under a capture the notional one is
    // owned by the raw relative stream instead, and those deliveries carry the
    // travel ledger — which is why they do not come through here.
    note_notional_pointer_x(static_cast<double>(x));
    if (on_motion_) on_motion_(x, y, current_mods());
}

void GuiInputCore::flush_deferred_motion() {
    // A captured strip drag defers its coalesced relative motion to the pointer-
    // frame boundary (pointer_frame). But a pointer frame can carry both
    // that motion and a button event, and button events are NOT deferred — they
    // dispatch at arrival. Delivering the pending motion here, immediately before
    // any button, guarantees the button handler runs against the latest
    // accumulated position: a press -> threshold-crossing motion -> release inside
    // one frame then reaches the release with the drag's `moved` already true,
    // so the drag commits and a trim-bar release does not wrongly seed a
    // double-click candidate. Clearing the flag means pointer_frame's trailing
    // delivery does not double-fire the same motion.
    if (frame_have_relmotion_ && on_motion_) {
        on_motion_(pointer_x_, pointer_y_, current_mods());
        frame_have_relmotion_ = false;
    }
}

void GuiInputCore::end_left_hold_source(bool physical) {
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

void GuiInputCore::pointer_button(GuiMouseButton button, bool pressed) {
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
    if (button == GuiMouseButton::Left) {
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
                on_button_press_(button, pointer_x_, pointer_y_,
                                 current_mods());
            }
        } else {
            // 1->0 edge: flush FIRST (button still reads held), then clear the
            // bit, then deliver the release (which now reads button-up).
            if (on_button_release_) flush_deferred_motion();
            pointer_left_held_ = false;
            if (on_button_release_) {
                on_button_release_(button, pointer_x_, pointer_y_,
                                   current_mods());
            }
        }
        return;
    }

    // Non-left buttons never participate in the logical-left OR.
    if (pressed) {
        if (on_button_press_) {
            flush_deferred_motion();
            on_button_press_(button, pointer_x_, pointer_y_, current_mods());
        }
    } else {
        if (on_button_release_) {
            flush_deferred_motion();
            on_button_release_(button, pointer_x_, pointer_y_, current_mods());
        }
    }
}

void GuiInputCore::pointer_axis(double value) {
    // Live path for touchpad two-finger scroll and any other continuous
    // (non-wheel) source. value120 carries WHEEL scroll only; the
    // backend delivers a touchpad's continuous delta through this legacy
    // axis path (wl_pointer.axis on Wayland, in its own continuous scroll
    // unit) and delivers no value120 for that frame. So this is the touchpad's
    // only
    // path. We stage the delta into the per-frame scratch and do not emit
    // here — pointer_frame() arbitrates so exactly one source counts
    // per frame (value120 wins when both arrive) and drains to detents.
    frame_axis_accum_ += value;
    frame_have_axis_  = true;
}

// One logical scroll detent is 120 value120 units (the high-resolution
// scroll protocol's fixed convention). A mouse-wheel click arrives as a
// single value120 = 120 (or a multiple), so the drain below emits exactly
// one step per detent — identical to the pre-value120 feel. A touchpad
// arrives via the legacy axis event as a stream of small continuous
// deltas; pointer_frame() scales those into value120 units with
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

void GuiInputCore::pointer_axis_value120(int32_t value120) {
    // Stage into the per-frame scratch; pointer_frame() arbitrates and
    // drains. value120 is the wheel (high-resolution) source.
    frame_v120_accum_ += static_cast<double>(value120);
    frame_have_v120_  = true;
}

void GuiInputCore::pointer_frame() {
    // Per-frame arbitration: a pointer frame may carry value120 (wheel)
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
    // waveform magnification step, Alt = the stepped pan, Ctrl = the zoom
    // step; every other combination no-ops), so
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
    // relative_motion): one on_motion_ at the accumulated virtual
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
    // to be the last word (2026-08-08): pointer_leave FLUSHES above its hook
    // — the frame terminating that leave reaches this block, and a delivery here
    // would set `pointer_in_window` back to true with no second hook to undo it
    // — and the pointer-capability teardown DROPS the flag in its frame-scratch
    // reset, the relative-pointer object being gone by then. Each states its own
    // reason for flushing rather than dropping, or the reverse.
    // Uncaptured absolute motion is untouched (delivered live in
    // pointer_motion; compositors already pace it at frame cadence).
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
// Touch event handlers (the finger as the pointer; touch phase 1, 2026-08-11;
// the WINDOWED MODEL, restored at the sixth glass ruling 2026-08-12 and
// TWO-DEADLINE since the eighth, the same day)
//
// The phase machine, the translation contract and the AUTHORITATIVE edge
// inventory live at the touch state block in input_core.h; each body
// below states only its own clause. The GUI sees ordinary pointer deliveries
// (one finger), the touch-nav hooks (the pan and the pinch) or the region
// hooks (the hold) and nothing else.
// ---------------------------------------------------------------------------

void GuiInputCore::maybe_resolve_touch_window() {
    if (touch_phase_ != TouchPhase::Pending) return;
    if (gui_monotonic_us() < touch_window_deadline_us_) return;
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

void GuiInputCore::resolve_touch_window_to_pointer() {
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
    // definition, Chebyshev >= touch_slop_px_ from the down point — while the
    // expiry and tap resolutions enter motionless (their drift is sub-slop
    // by construction). The Pointer motion arm latches it afterward.
    touch_translation_moved_ =
        std::max(std::abs(touch_last_x_ - touch_down_x_),
                 std::abs(touch_last_y_ - touch_down_y_)) >= touch_slop_px_;
    // THE HOLD BIT GOES UP BEFORE THE ENTRY MOTION (codex round 2): the finger
    // has factually been down since the window opened, so EVERY delivery in
    // this burst — the entry motion included — reads primary_button_held
    // through current_mods(). That is the state the GUI's armed hover-open
    // guard reads (on_motion's no-gesture tail refuses the menu-row hover-open
    // under a held primary button): with the bit raised only after the entry
    // motion, that pre-press motion read UNHELD, hover-opened an armed
    // anchor's menu, and the press in the same burst toggle-closed it — a tap
    // on ANY menu anchor with the row armed visibly did nothing. The
    // was_held capture reads the two SIBLING sources only, so its value is
    // order-independent.
    const bool was_held = pointer_left_held_ || synth_left_held_;
    touch_left_held_ = true;
    // Touch positions are fractional on real panels; every delivery names the
    // pixel that contains one (containing_pixel, the one owner).
    const int down_x = containing_pixel(touch_down_x_);
    const int down_y = containing_pixel(touch_down_y_);
    deliver_motion(down_x, down_y);
    // A pointer-button press is a context event that kills an armed key repeat
    // (layer 1 of the repeat contract), exactly as the physical left button and
    // the synthesized-`e` presses do at their own delivery sites.
    repeat_key_ = 0;
    // The logical left's OR-edge model, third source: the press is delivered
    // only on the 0->1 edge, with the bit already raised above so the press —
    // and the entry motion before it — reads held (the pointer_button
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
        deliver_motion(containing_pixel(touch_last_x_),
                       containing_pixel(touch_last_y_));
    }
    touch_window_moved_         = false;
    touch_frame_motion_pending_ = false;
}

void GuiInputCore::resolve_touch_window_to_single_nav() {
    // Pending -> Nav with ONE finger (the phone model's slop-crossing arm;
    // contract at the declaration): the finger drags the pan, and NOTHING
    // pointer-shaped starts — no entry motion, no press, the touch hold never
    // raised. The seed is the two-finger seed's own shape measured from the
    // DOWN point, unlatched with last_cx still holding the start, so the
    // first delivered frame runs deliver_touch_nav_frame's ordinary latch
    // test — the crossing position is already >= touch_slop_px_ away in the
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

void GuiInputCore::resolve_touch_window_to_region() {
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
            containing_pixel(touch_down_x_),
            containing_pixel(touch_down_y_));
    touch_region_frame_dirty_ =
        touch_window_moved_ &&
        (touch_last_x_ != touch_down_x_ || touch_last_y_ != touch_down_y_);
    touch_window_moved_ = false;
}

void GuiInputCore::flush_touch_frame_motion() {
    // The touch twin of flush_deferred_motion: Pointer-phase motion is
    // coalesced to the touch_frame boundary, and a button delivery in the
    // same frame must see the latest position first (delivered while the hold
    // still reads held, so the motion takes the live-drag path).
    // THE INVARIANT, stated once here (the flush owner): the staged motion is
    // the FINGER's own and is independent of the logical-left OR's edge, so
    // every end of the touch translation FLUSHES it rather than clearing it —
    // end_touch_left_hold flushes UNCONDITIONALLY, ahead of its release
    // decision, because a sibling source (the physical left button / bare-`e`)
    // still holding suppresses only the RELEASE, never the motion. The only
    // bare
    // clears of touch_frame_motion_pending_ are resets of state already
    // flushed or never deliverable: the resolution's replay tail (Pending
    // never stages this flag) and forget_touch_state (which runs after the
    // hard-end contract has already ended the hold through the flush here).
    if (touch_frame_motion_pending_) {
        deliver_motion(containing_pixel(touch_last_x_),
                       containing_pixel(touch_last_y_));
    }
    touch_frame_motion_pending_ = false;
}

bool GuiInputCore::end_touch_left_hold(bool clean_release) {
    if (!touch_left_held_) return false;
    // The staged TOUCH motion flushes UNCONDITIONALLY, before the release
    // decision and while this bit still reads held: the finger's final
    // position is owed to on_motion whatever the logical-left OR says,
    // because a sibling source (the physical left button / bare-`e`) staying
    // held suppresses only the RELEASE edge, never the motion (the invariant at
    // flush_touch_frame_motion, the flush owner). Gating this on the edge
    // made a shared drag silently stop short of the finger whenever the
    // mouse kept the OR true through the finger's last frame.
    flush_touch_frame_motion();
    // The end_left_hold_source ordering for the third source: deliver only on
    // the logical 1->0 edge (neither sibling source held); the deferred
    // POINTER motion flushes ahead of the delivery it exists for; then clear;
    // then deliver at the owner's last position.
    // WHAT is delivered on that edge FORKS ON clean_release (codex round 19),
    // and the fork is one line at the bottom of this body:
    //   * CLEAN (the finger's own lift, the hard end) — the left RELEASE, as
    //     ever: the press was a click, and every act-at-lift surface runs.
    //   * ABNORMAL (the second-finger upgrade) — the product's own LOST-BUTTON
    //     edge instead: the hold bit drops here exactly as it does on a clean
    //     end, so nothing sticks, and the GUI is told through a MOTION at the
    //     finger's last position carrying `primary_button_held` false. That is
    //     the spelling every gesture already answers (the button-lost arms in
    //     GuiInputHandler::on_motion, and the force-end finalizer's own rule):
    //     a MOVED drag finalizes like a release, an UNMOVED press commits
    //     NOTHING FURTHER — no click act still owed, no double-click seed, and
    //     no chrome dispatch, since dispatching an armed chrome/modal/menu
    //     press is the RELEASE's job and no release is delivered. NOTHING
    //     FURTHER RATHER THAN NOTHING SINCE 2026-08-17: a resolved MARKER touch
    //     has already run its click act at the synthesized PRESS, and this
    //     abnormal end drops the pending ALONE and leaves that committed click
    //     — the selection, the land, the playback stop — standing (the
    //     pending_marker_press arm at input_pointer.cpp's on_motion). That is
    //     the recorded accepted cost of press-time acting, undo the mitigation;
    //     the surfaces that still commit nothing at an unmoved press are the
    //     ones whose act was never owed until the lift, the nav surface's
    //     deferred click among them. touch.md's upgrade bullet is the ruling.
    //     AND THE ARMS THE CHROME / MODAL / MENU SURFACES HOLD ARE
    //     DROPPED BY THAT SAME MOTION, on EITHER arm of the focus fork below
    //     (codex round 20): the GUI ends its release-time claims — the chrome
    //     press, the modal's armed button, the popup's item claim — at exactly
    //     this edge, an unheld motion while one stands
    //     (clear_release_time_press_arms, at on_motion's top). That is a
    //     correction of what this comment used to say: those claims are NOT
    //     button-lost consumers of their own, so calling the end "the standing
    //     lost-button shape" was true of the motion-driven drag states and false
    //     of them, and with a mouse focused in the window — where the fork takes
    //     the restore motion instead of the leave hook — a stale arm survived
    //     the whole pinch and swallowed the next tap. The promise now is flat:
    //     an upgrade ends every arm the vanished press could have committed,
    //     because the finger that armed them is not going to lift.
    const bool edge = !pointer_left_held_ && !synth_left_held_;
    const bool deliver =
        edge && (clean_release ? on_button_release_ != nullptr
                               : on_motion_ != nullptr);
    if (deliver) flush_deferred_motion();
    touch_left_held_ = false;
    if (deliver) {
        const int x = containing_pixel(touch_last_x_);
        const int y = containing_pixel(touch_last_y_);
        if (clean_release)
            on_button_release_(GuiMouseButton::Left, x, y, current_mods());
        else
            deliver_motion(x, y);   // the button-lost edge (the fork above)
    }
    // The return is the END'S OWN EDGE (codex round 2): every caller ends the
    // translation through deliver_touch_translation_end, which acts iff the
    // end was delivered — a sibling-suppressed end means the unified
    // pointer has NOT left (the mouse is still there, mid-press), and the
    // leave hook's clears belong to that still-held press (the armed chrome
    // press, the modal's armed button, the popup's press claim — the body's
    // own list, main.cpp), not to the finger that lifted.
    return deliver;
}

void GuiInputCore::deliver_touch_translation_end(bool clean_release) {
    // The end's own edge first (codex round 2): a sibling-held logical
    // left suppressed the release, and then NOTHING below fires either — the
    // mouse is mid-press, and neither a leave nor a restore motion is this
    // stream's to deliver. A suppressed end can strand a hover face where the
    // finger last was — the accepted-glitch class, self-healing on the next
    // pointer event.
    // clean_release is passed straight through and decides only WHAT that edge
    // delivers — the left release, or the lost-button motion the upgrade needs
    // (the fork and its whole rationale are at end_touch_left_hold). Everything
    // below is shared verbatim: whichever end it was, the finger is no longer
    // the pointer, and the fork below answers where the pointer now IS.
    if (!end_touch_left_hold(clean_release)) return;
    // THE END FORKS ON PHYSICAL POINTER FOCUS (codex round 3, the
    // cursor-residue fix): a resolved touch drives the GUI's remembered
    // position to the finger, and the loop-settled cursor owner applies the
    // finger zone's kind to the REAL pointer — so before this fork a mouse
    // resting in the window kept the finger's cue (Arrow/resize over a
    // Pan zone, say) until its own next motion. The finger ceasing to BE the
    // pointer — its lift, or the upgrade's handover to the nav gesture — means
    // the unified pointer is now wherever the MOUSE is:
    //   * physical pointer FOCUSED (pointer_enter / pointer_leave, which touch
    //     never writes) — synthesize an ordinary MOTION at its last
    //     platform-tracked position (pointer_x_/pointer_y_, the physical
    //     pointer's own
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

void GuiInputCore::touch_down(int32_t id, double x, double y) {
    // An event past the deadline sees the resolved phase (Pointer).
    maybe_resolve_touch_window();
    ++touch_point_count_;
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
            // contract. Null hook = no pan surface.
            touch_down_in_pan_zone_ =
                touch_pan_zone_hook_ &&
                touch_pan_zone_hook_(containing_pixel(x),
                                     containing_pixel(y));
            // THE THIN-LANE ANSWER rides beside it, same query shape, same one
            // asking, same lifecycle (both cleared in forget_touch_state). It
            // forks NO deadline and NO resolution here, so a finger landing on
            // the overview strip or the trim bar resolves exactly as it would
            // otherwise (neither lane is pan surface, so that is the plain
            // pointer translation); what it decides comes later, at the two
            // doors that refuse with it — the second-finger fork in the Pointer
            // arm below, and the GUI's own refusal on every nav frame it is
            // carried onto.
            touch_down_on_thin_lane_ =
                touch_thin_lane_hook_ &&
                touch_thin_lane_hook_(containing_pixel(x),
                                      containing_pixel(y));
            // THE TWO-DEADLINE FORK (the eighth glass ruling, 2026-08-12 —
            // the dead trim-band beat's pattern reborn): ON the zone the
            // window runs to the REGION-HOLD beat (kTouchRegionHoldMs, the
            // product's one hold beat); OFF it the 60 ms disambiguation
            // window as before. The arithmetic at this site:
            // on the zone a tap still lifts long before that beat and delivers
            // whole at the lift, a drag still crosses the 8 px slop into the
            // pan within the first frames, so the stretch costs neither —
            // only the deliberate motionless hold ever reaches the beat.
            // Monotonic, not the event
            // timestamp (whose base this program never compares against).
            touch_window_deadline_us_ =
                gui_monotonic_us() +
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
            //   * MOVED (a live drag — marker, region, trim, the overview
            //     lane's box and bound drags): IGNORED whole — recorded (the
            //     count above), not routed: mid-gesture finger-count changes do
            //     not mutate a committed gesture (the any-end-commits family;
            //     the architect's explicit mid-drag ruling).
            //   * ON A THIN LANE (the overview strip or the trim bar,
            //     touch_down_on_thin_lane_): IGNORED WHETHER THE FIRST FINGER
            //     HAS MOVED OR NOT, so the first finger's drag simply continues
            //     — architect 2026-08-15: "get rid of all two-finger gestures on
            //     the overview strip and on the trim bar; once one finger is
            //     down, the second finger is completely ignored, which is what
            //     we do with three-finger gestures on the waveform — which makes
            //     sense, because the waveform is large and the overview and trim
            //     are small". THIS IS THE FIRST DOOR AND THE REASON IT EXISTS:
            //     without it a MOTIONLESS finger on such a lane would take the
            //     upgrade below, tearing down a live and perfectly correct
            //     pointer translation to start a gesture the GUI's own refusal
            //     then drops frame by frame.
            //     WHAT THE WINDOW PATH DOES IS DIFFERENT AND IS NOT FIXED HERE:
            //     two fingers landing within the disambiguation window never
            //     reach the Pointer phase at all — the Pending arm above sends
            //     them straight to Nav — so a FAST two-finger landing on a thin
            //     lane does nothing whatever, not even the first finger's drag,
            //     because the GUI refuses every frame of it. "Two fingers do
            //     nothing there" is the intent either way; it is simply reached
            //     through the other door, and a reader should not have to
            //     discover that.
            //   * MOTIONLESS (a hold, off these lanes): THE UPGRADE — the
            //     translation ends by the ABNORMAL end, the finger-up path's
            //     own shape through the one owner MINUS its click (staged
            //     motion flushed, the hold bit dropped on the logical left's
            //     1->0 edge, the focus-forked translation end; the
            //     sibling-suppression rule applies identically),
            //     and the two-finger gesture SEEDS AT THE JOIN: both
            //     fingers' current positions are the gesture start, the
            //     latch measured from there. The hold's press already landed
            //     at the window's expiry, so the upgrade adds NO further
            //     jump — it only keeps a slow pinch (fingers landing further
            //     apart than the window) alive instead of dead; a sub-latch
            //     release of that pair delivers nothing more.
            //     AN UPGRADE IS A CHANGE OF GESTURE, NOT A LIFT (codex round
            //     19), and that is why the end is the ABNORMAL one: the user
            //     did not raise a finger, they added one, and speaking for the
            //     finger they did NOT lift is what produced the defect. The
            //     clean release this used to deliver was read as a completed
            //     CLICK by every act-at-lift surface the hold could be sitting
            //     on — off the pan zone, which is where a Pointer-phase hold
            //     lives by construction:
            //       - THE CLASS, not the case. Every CHROME BUTTON has acted at
            //         the lift since 2026-08-13, so a finger held on Render,
            //         Save, a transport button, a menu ITEM or a modal's OK
            //         fired it the moment the second finger touched down — a
            //         pinch that ran a command. This is the worse half.
            //       - The one codex found: a finger held inside a STANDING
            //         REGION (the region editor's carve-out puts it off the
            //         zone) ran the motionless release's click act — the upper
            //         half placing the playhead and hiding the overlay, the
            //         lower half firing the scrub and its playback edge.
            //     The abnormal end is the RIGHT spelling rather than a new
            //     suppression because it is the product's existing ruled answer
            //     to "this press ended without being a click", and it already
            //     carries the split this needs: a MOVED drag finalizes exactly
            //     as a lost button does (unreachable from here today — the
            //     latch above refuses a moved translation — and correct by
            //     construction if it ever is), an UNMOVED press commits
            //     nothing.
            if (touch_translation_moved_ || touch_down_on_thin_lane_) break;
            deliver_touch_translation_end(/*clean_release=*/false);
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

void GuiInputCore::touch_up(int32_t id) {
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
            // live at deliver_touch_translation_end, the one owner this site,
            // the hard end and the second-finger upgrade all call. THE LIFT IS
            // A CLEAN END — the finger really did leave, so its press was a
            // click and every act-at-lift surface is owed its act (the upgrade
            // is the one caller that passes false; the fork is at
            // end_touch_left_hold).
            deliver_touch_translation_end(/*clean_release=*/true);
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
                // touch_frame that closes the batch, so when both fingers
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
            // its translation ENDED at the join — the hold bit dropped there,
            // by the abnormal end).
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

void GuiInputCore::touch_motion(int32_t id, double x, double y) {
    maybe_resolve_touch_window();
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
            // touch_slop_px_ IS the GUI's own drag gate — the one number pushed
            // down scaled — it crosses that gate in the same burst as the press
            // (the invariant at set_touch_slop_px, input_core.h).
            if (std::max(std::abs(x - touch_down_x_),
                         std::abs(y - touch_down_y_)) >= touch_slop_px_) {
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
                         std::abs(y - touch_down_y_)) >= touch_slop_px_)
                touch_translation_moved_ = true;
            // Coalesced to the touch_frame boundary — the pointer-frame
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
            // The Nav dirty-frame cadence: one region_update per touch_frame.
            touch_region_frame_dirty_ = true;
            break;
        case TouchPhase::Idle:
        case TouchPhase::Drain:
            break;
    }
}

void GuiInputCore::touch_frame() {
    // The per-frame drain, the pointer_frame precedent: one motion delivery
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
                containing_pixel(touch_last_x_),
                containing_pixel(touch_last_y_));
    }
}

void GuiInputCore::deliver_touch_nav_frame() {
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
                     std::abs(cy - touch_nav_start_cy_)) >= touch_slop_px_;
        const bool pinch =
            std::abs(dist - touch_nav_start_dist_) >= touch_slop_px_;
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
        frame.x          = containing_pixel(cx);
        frame.y          = containing_pixel(cy);
        frame.dx         = dx;
        frame.dist_ratio = ratio;
        // The finger count is the GUI's one fork (contract at
        // GuiTouchNavFrame, gui_input.h): it discards dx on a two-finger
        // frame — two fingers zoom and never pan — so this layer still
        // measures and delivers both deltas and applies no policy of its own.
        frame.two_finger = !touch_nav_single_;
        // THE DOWN POINT'S SURFACE ANSWER, carried on every frame of the
        // stream (field contract at GuiTouchNavFrame, gui_input.h): the FIRST
        // finger's thin-lane bit, captured once at the `Idle` down and never
        // re-measured, so the GUI's refusal reads where the gesture BEGAN
        // instead of a centroid that moves. Delivering it is not policy — it is
        // the same surface geometry the pan-zone answer already is, asked once
        // and handed over.
        frame.down_on_thin_lane = touch_down_on_thin_lane_;
        touch_nav_update_hook_(frame);
    }
}

void GuiInputCore::end_touch_nav_gesture(bool deliver_final_frame) {
    // THE END SPLIT (recorded here, at the one owner; each caller passes its
    // own clause and the edge inventory at the touch state block names both):
    // Wayland orders the terminating up/cancel BEFORE the touch_frame that
    // closes its batch, so motion batched with the end is still staged in
    // touch_nav_frame_dirty_ when this runs.
    //   * FINGER-UP (true): the staged frame is the user's own FINAL MOTION
    //     and DELIVERS first — the any-end-commits family. This is what lets
    //     a short pinch whose only latch-crossing motion batches with the up
    //     act at all: with no prior update delivered, dropping that frame
    //     would erase the crossing, owe no end hook, and make the whole
    //     gesture silently do nothing.
    //   * HARD ENDS (touch_cancel / touch-capability loss, false): the
    //     staged frame is DROPPED deliberately — the window system's claim
    //     means that motion retroactively was not ours — and the end hook still
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

void GuiInputCore::end_touch_region_gesture(bool deliver_final_frame) {
    // The Nav end split restated for the region trio (the contract at the
    // declaration): the finger's own lift delivers the staged frame first —
    // the user's final leg, the span's resting extent — while the hard ends
    // drop it (the window system's claim means that motion retroactively was
    // not ours). region_end then fires UNCONDITIONALLY, unlike the nav end's
    // delivered-gate: the GUI-side region drag has held the drag-modal gate
    // open since the begin, so its release path is owed even when no update
    // was ever delivered (a motionless hold-lift is the former's placement),
    // and a refused begin is covered by the end body's own !active guard.
    if (deliver_final_frame && touch_region_frame_dirty_) {
        touch_region_frame_dirty_ = false;
        if (touch_region_update_hook_)
            touch_region_update_hook_(
                containing_pixel(touch_last_x_),
                containing_pixel(touch_last_y_));
    }
    touch_region_frame_dirty_ = false;
    if (touch_region_end_hook_) touch_region_end_hook_();
}

void GuiInputCore::touch_cancel() {
    // The window system claims the touches (its own gesture recognition, a
    // grab; wl_touch.cancel on Wayland).
    // One contract with touch-capability loss, in full.
    hard_end_touch_stream();
}

void GuiInputCore::touch_capability_lost() {
    // THE HARD END OF THE TOUCH STREAM: no motion, up, or cancel will arrive
    // from this device again. The contract is the cancel's, shared whole
    // (hard_end_touch_stream): a live pointer translation commits — release
    // delivered at the last position, then the focus-forked translation end
    // (deliver_touch_translation_end: the ordinary leave, or a restore motion
    // at a focused mouse) — a live nav gesture (single- or two-finger) ends
    // through its end path with its staged final frame dropped, an unresolved
    // disambiguation window drops silently, and all touch state is forgotten.
    // The pointer- and keyboard-capability edges deliberately do not reach in
    // here: each input source dies on its own stream's edges.
    hard_end_touch_stream();
}

void GuiInputCore::hard_end_touch_stream() {
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
            // A CLEAN END: the hard end COMMITS (the no-cancel family), so the
            // press ends as the click it was, exactly as the lift's does. Only
            // the second-finger upgrade — where no finger left — takes the
            // abnormal end (the fork is at end_touch_left_hold).
            deliver_touch_translation_end(/*clean_release=*/true);
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

void GuiInputCore::forget_touch_state() {
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
    touch_down_on_thin_lane_    = false;
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
// The pointer capture's policy half (the requests themselves are the
// backend's; the seam is at the accessors' contracts, input_core.h)
// ---------------------------------------------------------------------------

void GuiInputCore::begin_capture_seed() {

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
    // at every ctrl edge after that (contract at set_notional_x_frozen) — and
    // it is the freeze's ONE client, that drag being the process's one
    // capturing gesture since the overview lane's dual-axis strip drag was
    // deleted (2026-08-15).
    notional_x_frozen_ = false;
}

void GuiInputCore::note_capture_locked(GuiCursorKind restore_kind) {

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
    // the backend next tells us where the pointer really is (the contract is at
    // the field). Set beside pointer_captured_ and only on the path that CREATED
    // THE LOCK PROXY — the degraded and creation-failed returns above leave it
    // false, which is what keeps those gestures on the ordinary cursor path. That
    // the proxy is taken for a live lock, without waiting for the asynchronous
    // `locked` event, is the ruling recorded at the header contract.
    pointer_position_unknown_ = true;
}

void GuiInputCore::apply_capture_restore(double tracked_x, double tracked_y) {
    virtual_pointer_x_ = tracked_x;
    virtual_pointer_y_ = tracked_y;
    note_notional_pointer_x(tracked_x);
    pointer_x_ = containing_pixel(tracked_x);
    pointer_y_ = containing_pixel(tracked_y);
}

void GuiInputCore::end_capture() {

    pointer_captured_ = false;
    // The lateral freeze dies with the capture it belongs to, on BOTH exits
    // (the hint arm above and the revoked-lock path that skips it), so nothing
    // a zoom phase asserted can survive into the next gesture. Deliberately
    // below the write-back above: that write states where the cursor was
    // DRAWN, a real position rather than an accumulation, so the freeze never
    // gated it and the order carries no meaning either way.
    notional_x_frozen_ = false;
}

void GuiInputCore::set_capture_restore_x(double surface_x) {
    // The active zoom gesture names the surface x its anchor stem paints at;
    // the release restore uses it in place of the pointer's notional position.
    // Ignored when no capture is live (nothing to restore).
    if (!pointer_captured_) return;
    capture_restore_x_override_ = surface_x;
}

void GuiInputCore::clear_capture_restore_x() {
    // The nav drag's zoom→pan switch drops the stem override so the release
    // goes back to the notional x (contract at the declaration). Ignored
    // when no capture is live.
    if (!pointer_captured_) return;
    capture_restore_x_override_.reset();
}

void GuiInputCore::set_capture_restore_kind(GuiCursorKind kind) {
    // The mid-capture re-stamp of what the release restores (contract at the
    // declaration): the same direct write begin_pointer_capture's stamp uses
    // — "this is what comes back", not "show this now" (the cursor is hidden
    // for the capture's whole life, and pointer_position_unknown_ keeps
    // loop-tail kinds dropped) — guarded on a live capture so an uncaptured
    // caller cannot clobber the loop-tail owner's remembered kind.
    if (!pointer_captured_) return;
    cursor_kind_ = kind;
}

void GuiInputCore::set_notional_x_frozen(bool frozen) {
    // The gesture states its phase; this holds it (contract at the
    // declaration). Guarded on a live capture like its three siblings, which
    // is why the nav drag re-asserts at its threshold crossing: the ctrl edges
    // it took while the press was still sub-threshold had no capture to speak
    // to.
    if (!pointer_captured_) return;
    notional_x_frozen_ = frozen;
}

void GuiInputCore::set_notional_pointer_x(double surface_x) {
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

void GuiInputCore::set_capture_wrap_span(double lo, double hi) {
    // The GUI states the waveform's span, and the fold is edge to edge inside
    // it (contract at the declaration). Capture-guarded like its siblings —
    // with no capture there is no wrap, the notional position then simply
    // being the delivery funnel's.
    if (!pointer_captured_) return;
    capture_wrap_lo_ = lo;
    capture_wrap_hi_ = hi;
}

void GuiInputCore::relative_motion(double dx, double dy) {
    // Consumed only while a capture is active; ignored otherwise (a relative
    // pointer exists for the process lifetime but only the strip drags lock).
    if (!pointer_captured_) return;

    // Advance the UNBOUNDED virtual position — no clamp to the window is what
    // makes pan/zoom travel infinite, every captured gesture differencing these
    // deliveries for its per-event delta — and write the containing pixel into
    // pointer_x_/y_ so a button event dispatched later in the same frame already
    // sees the final coordinates. The on_motion_ DELIVERY is deferred to the
    // pointer-frame boundary (pointer_frame): a captured relative pointer
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
    // undelivered (which would leave the drag's `moved` false and lose the
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
    // Through containing_pixel, the one owner: this is where a continuous
    // pointer position becomes the window pixel every gesture reads.
    pointer_x_ = containing_pixel(virtual_pointer_x_);
    pointer_y_ = containing_pixel(virtual_pointer_y_);
    frame_have_relmotion_ = true;
}

// ---------------------------------------------------------------------------
// Setters (callbacks)
// ---------------------------------------------------------------------------

void GuiInputCore::set_on_key(KeyCallback cb)                    { on_key_ = std::move(cb); }
void GuiInputCore::set_on_key_release(KeyReleaseCallback cb)     { on_key_release_ = std::move(cb); }
void GuiInputCore::set_on_button_press(ButtonCallback cb)        { on_button_press_ = std::move(cb); }
void GuiInputCore::set_on_button_release(ButtonCallback cb)      { on_button_release_ = std::move(cb); }
void GuiInputCore::set_on_wheel(WheelCallback cb)                { on_wheel_ = std::move(cb); }
void GuiInputCore::set_on_motion(MotionCallback cb)              { on_motion_ = std::move(cb); }
void GuiInputCore::set_wheel_context_probe(WheelContextProbe cb)    { wheel_context_probe_ = std::move(cb); }
void GuiInputCore::set_text_editor_active_probe(TextEditorProbe cb) { text_editor_active_probe_ = std::move(cb); }
void GuiInputCore::set_repeat_eligible_probe(RepeatEligibleProbe cb) { repeat_eligible_probe_ = std::move(cb); }
void GuiInputCore::set_pointer_left_hook(std::function<void(GuiPointerLeaveReason)> cb) { pointer_left_hook_ = std::move(cb); }
void GuiInputCore::set_keyboard_intent_cancel_hook(std::function<void()> cb) { keyboard_intent_cancel_hook_ = std::move(cb); }
void GuiInputCore::set_touch_nav_hooks(
    std::function<void(const GuiTouchNavFrame&)> update,
    std::function<void()> end,
    std::function<bool(int x, int y)> pan_zone,
    std::function<bool(int x, int y)> thin_lane,
    std::function<void(int x, int y)> region_begin,
    std::function<void(int x, int y)> region_update,
    std::function<void()> region_end) {
    touch_nav_update_hook_    = std::move(update);
    touch_nav_end_hook_       = std::move(end);
    touch_pan_zone_hook_      = std::move(pan_zone);
    touch_thin_lane_hook_     = std::move(thin_lane);
    touch_region_begin_hook_  = std::move(region_begin);
    touch_region_update_hook_ = std::move(region_update);
    touch_region_end_hook_    = std::move(region_end);
}
void GuiInputCore::set_codepoint_probe(
    std::function<uint32_t(uint32_t stable_code)> probe) {
    codepoint_probe_ = std::move(probe);
}
