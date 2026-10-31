#include "input_core.h"

#include <time.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <utility>

uint64_t gui_monotonic_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000ull +
           static_cast<uint64_t>(ts.tv_nsec) / 1000ull;
}

namespace {

// THE TOUCH WINDOW'S CONSTANTS, RULED RETUNABLES (touch phase 1,
// 2026-08-11; the WINDOW is back since the sixth glass ruling, 2026-08-12,
// after the timer-free model's one-session field life — retune on glass, not
// on argument).
//
// THE WINDOW OPENS IN TWO PLACES ONLY (architect 2026-09-25: "zoom is not
// useful outside waveform", and a second finger's only use is the pinch): on
// the PAN ZONE and in an OPEN EDITOR'S FIELD. Anywhere else a down is the
// pointer ON CONTACT (touch_down's Idle arm), so the off-zone deadline that
// told one finger from two there — 60 ms, the eighth ruling's "as before" —
// has no producer and is gone with its window.
//
// kTouchRegionHoldMs is the PAN ZONE's own stretched window — the
// REGION-HOLD BEAT (the eighth glass ruling, 2026-08-12: pan is the common
// act and takes the primary drag, so the region is the deliberate act —
// "region select to be hold and then drag because the pan is way more
// common"). A down on the navigation surface runs its window to this
// deadline, and the EXPIRY there is the REGION HOLD: the region former armed
// through the region hooks, so hold-then-drag sweeps a region on glass. The
// duration is the HOLD DELAY, kHoldDelayMs (gui_input.h, 300 ms, architect
// 2026-09-29; that declaration names the two holds that read it), one number
// with the chrome shift long press — and it is long past an aimed drag's
// natural dwell, which crosses the slop into the pan within the first
// frames. A DELIBERATE SLOW START DOES NOT (measured 2026-10-22 on the
// tablet: a press creeping under the slop for the whole beat — 18 device px
// in 300 ms at 300 %, under ~60 px/s — reached the expiry and swept a trim
// region where a pan was meant), so since that day THE EXPIRY IS THE HOLD
// ONLY FOR A MOTIONLESS PRESS: a contact that left the stillness radius
// (kDefaultTouchStillnessPx, 2 W, input_core.h) inside the beat keeps its
// window open with no deadline until the slop or the lift, and no hold can
// form again before that lift (maybe_resolve_touch_window). The lesson
// of the dead kTouchTrimHoldMs (the trim band's hold-a-beat deadline of
// 2026-08-11, whose first cut rode the 60 ms window and turned every
// deliberate band drag into the trim move; that GESTURE stayed dead — this
// beat revives only its two-deadline PATTERN, on a surface whose quick drag
// is the pan, not a pointer drag, so the dwell collision cannot recur).
// NOR DOES IT COLLIDE WITH THE DOUBLE TAP, though the two numbers differ since
// 2026-09-29 (the double-click window rides the 600 ms beat, kDoubleClickMs):
// a tap's whole burst delivers at the LIFT, so a double-click candidate is
// only ever seeded by a finger that has already left the glass, and a finger
// held motionless to this beat becomes the region hold with no press
// delivered at all — the two readings are exclusive by construction, not by
// their numbers.
//
// THE SLOP IS NO LONGER A CONSTANT HERE. It is touch_slop_px_, a settable
// member whose default is the authored kDefaultTouchSlopPx and whose live
// value the GUI pushes down scaled (set_touch_slop_px, input_core.h — the
// contract, the two uses and the twin-gate invariant are all stated there,
// since the value's owner is now the door rather than this block).
//
// AND THE OTHER ANSWER IS NO DEADLINE AT ALL. Inside an OPEN EDITOR'S
// FIELD the window never expires (architect's tablet vocabulary for the
// field, 2026-09-05: a TAP seats the caret and seeds the double tap, a DRAG
// moves it, a DOUBLE TAP selects the word — there is no hold meaning there,
// no handles and no toolbar, so no duration can decide anything). A finger
// resting in the field therefore stays Pending however long it rests and its
// LIFT delivers the ordinary tap burst, seeding the double-tap candidate
// exactly as a quick tap does; only the SLOP CROSSING resolves it, to the
// caret drag. That is what keeps a resting finger out of the desk's
// selection sweep — the field's contact can no longer expire onto the
// pointer road at all — while a slow tap still opens a double tap.
constexpr int    kTouchRegionHoldMs   = kHoldDelayMs;
// The deadline a window that must never expire carries: no monotonic reading
// can reach it, so maybe_resolve_touch_window's one time compare is the whole
// mechanism and no second predicate exists to drift from it.
constexpr uint64_t kTouchWindowNoExpiry = UINT64_MAX;

} // namespace

// ---------------------------------------------------------------------------
// The two software deadlines
// ---------------------------------------------------------------------------

void GuiInputCore::tick() {
    maybe_fire_repeat();
    // The touch window's deadline rides the same tick the
    // key-repeat deadline does (its lazy twin runs at each touch event's
    // arrival; granularity is recorded at the constants).
    maybe_resolve_touch_window();
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

void GuiInputCore::key_event(GuiKey key, uint32_t stable_code, bool pressed,
                             uint32_t codepoint, bool with_ctrl) {
    if (!pressed) {
        // Cancel repeat if the released key was the one repeating.
        if (stable_code == repeat_keycode_) {
            repeat_key_ = 0;
        }
        // THE APPLICATION-SIDE KEY RELEASE (2026-08-13). It exists for exactly
        // one consumer — the modal dialog's keyboard press-and-hold, whose act
        // is at the lift — and it is delivered for the same key identity the
        // press carried, carrying no modifier state at all (the reasoning is
        // at set_on_key_release, input_core.h). A key this GUI has no use for
        // arrives as 0 and is not a delivery.
        if (key != 0 && on_key_release_) on_key_release_(key);
        return;
    }

    // Pressed. A key with no GuiKey identity is nothing to deliver.
    if (key == 0) return;

    GuiInputState mods = current_mods();
    mods.codepoint = codepoint;
    // THE KEY'S OWN CTRL (the contract at the declaration): ORed in before
    // the eligibility probe and the delivery read it, so both judge the
    // chord the key IS.
    if (with_ctrl) mods.ctrl = true;
    // Arm key repeat (last-key-wins). Eligibility is decided at press time by
    // the application probe under the pre-dispatch context (evaluated before
    // deliver_key runs, so a press that opens an editor is judged pre-open):
    // only held-step gestures and editor typing repeat; every edge-triggered
    // command is one-shot, and an ineligible press disarms any armed repeat.
    // The repeat contract is two layers: (1) the stored intent dies on exactly
    // two input edges — a different key press (which re-arms or disarms here
    // via press-time eligibility) and a pointer-button PRESS (the touch
    // translation's resolution press) — each clearing repeat_key_ at its
    // platform input chokepoint; pointer motion and button release
    // deliberately do not disarm;
    // (2) each fire additionally re-checks the level conditions (eligibility
    // under the live modifiers, and the editor-active flag still matching its
    // arm-time value). The arm-time editor flag is captured here for layer (2).
    // LAYER (1) IS LOAD-BEARING FOR UNDO CORRECTNESS as well as hand-feel — it is
    // what makes "no synthesized repeat can follow an intervening command" true,
    // which is the property undo coalescing rides. The full statement is at
    // maybe_fire_repeat, beside the bit it sets.
    const bool repeat_ok =
        repeat_eligible_probe_ && repeat_eligible_probe_(key, mods);
    const bool editor_ctx =
        text_editor_active_probe_ && text_editor_active_probe_();
    deliver_key(key, mods);
    if (repeat_ok) {
        repeat_key_        = key;
        repeat_keycode_    = stable_code;
        repeat_editor_ctx_ = editor_ctx;
        repeat_with_ctrl_  = with_ctrl;
        repeat_due_us_     = gui_monotonic_us() + repeat_delay_us_;
    } else {
        repeat_key_ = 0;
    }
}

void GuiInputCore::set_ctrl(bool ctrl) {
    // THE ONE-FINGER NAV'S FORK READS THIS BIT (the nav frame's ctrl bit,
    // gui_input.h), so a ctrl edge under a live single-finger nav owes the
    // touch machine two deliveries, one on either side of the assignment.
    if (ctrl == mod_ctrl_) return;
    const bool single_nav_edge = touch_phase_ == TouchPhase::Nav &&
                                 touch_nav_single_;

    // MOTION STAGED UNDER THE OLD BIT IS DELIVERED UNDER IT (2026-09-25, the
    // S Pen's button): a single-finger nav frame STAGED for the touch_frame
    // boundary is motion made under the old ctrl bit, and the frame reads the
    // bit at delivery, so it is delivered HERE, while the bit still reads old
    // — a pan leg staged before the button went down pans, it does not zoom.
    // The nav cadence owes nothing else: the frame boundary finds the flag
    // clear.
    if (single_nav_edge && touch_nav_frame_dirty_) {
        touch_nav_frame_dirty_ = false;
        deliver_touch_nav_frame(/*deliver_even_if_no_op=*/false);
    }

    mod_ctrl_ = ctrl;

    // THE CTRL EDGE IS ANNOUNCED TO A LIVE SINGLE-FINGER NAV (2026-09-25):
    // one exempt frame carrying the NEW bit and no delta (the exemption at
    // set_touch_nav_hooks' update contract), so the GUI seats the one-finger
    // zoom's pivot and paints its stem at a ctrl-down, and clears both at a
    // ctrl-up, AT THE EDGE, which a finger standing still would otherwise
    // never produce. Gated on the latch: an unlatched single nav has
    // delivered nothing, and its first frame reads the new bit anyway.
    if (single_nav_edge && touch_nav_latched_)
        deliver_touch_nav_frame(/*deliver_even_if_no_op=*/true);

    // No on_key synthesis on a ctrl change — the next key event carries the
    // updated state.
}

void GuiInputCore::set_repeat_info(int32_t rate, int32_t delay) {
    repeat_delay_us_  = static_cast<uint64_t>(delay) * 1000ull;
    repeat_period_us_ = 1'000'000ull / static_cast<uint64_t>(rate);
}

GuiInputState GuiInputCore::current_mods() const {
    GuiInputState s;
    s.ctrl = mod_ctrl_;
    // The logical left button is the touch translation's hold. Drags consult
    // this bit on motion; without it a touch drag tears on the first motion
    // event.
    s.primary_button_held = touch_left_held_;
    return s;
}

void GuiInputCore::deliver_key(GuiKey key, GuiInputState mods) {
    // THE SHARED DELIVERY PATH: both the press (key_event) and the
    // SYNTHESIZED REPEAT (maybe_fire_repeat) come through here. Key releases
    // do not: key_event's release branch feeds the repeat cancel and then the
    // application's release hook directly.
    if (on_key_) on_key_(key, mods);
}

void GuiInputCore::maybe_fire_repeat() {
    if (repeat_key_ == 0) return;
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
    // The armed press's own ctrl rides every fire (key_event's contract).
    if (repeat_with_ctrl_) mods.ctrl = true;
    // Layer (2) of the repeat contract — layer (1), the event-edge disarms, has
    // already killed the hold at any intervening pointer-button press and
    // re-armed/disarmed it at any key press. Re-
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
    // platform repeat delay. (A physical press takes the other arm of the
    // hybrid — the fixed 500 ms tap window — and this bit is exactly what
    // separates the two.)
    // THE BIT HAS TWO PRODUCERS, one per held surface: THIS SITE for a held KEY,
    // and GuiInputHandler::tick_chrome_press_repeat (input_pointer.cpp) for a
    // held BUTTON — the chord table's `repeats` rows (the four cardinal arrow
    // buttons, whose hold-repeat returned 2026-08-16, and Undo / Redo). The button producer never passes through this class (it calls
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
    // hold at any intervening pointer-button press or key press, no synthesized
    // repeat can ever arrive AFTER another command ran —
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
    pointer_focus_at(x, y);
    // Synthesize a motion delivery so consumers register the pointer
    // as present at the entry coordinates. Matches how most clients
    // treat enter — the first "the pointer is here" notification.
    deliver_motion(pointer_x_, pointer_y_);
}

void GuiInputCore::pointer_focus_at(double x, double y) {
    // The enter's focus and position, and nothing delivered (the contract and
    // its one caller are at the declaration).
    pointer_focused_ = true;
    pointer_x_ = containing_pixel(x);
    pointer_y_ = containing_pixel(y);
}

void GuiInputCore::pointer_leave(GuiPointerLeaveReason reason) {
    pointer_focused_ = false;
    // Fire the leave hook: no motion arrives while the pen hovers nowhere, so
    // without this a button would keep its pointer-derived face after the pen
    // left. The hook owns the erase damage, and main.cpp's hook body is the
    // authoritative list of what it clears. THE CALLER'S REASON is the
    // argument: PenHoverEnd from the Android pen's hover ending, whose leave
    // darkens no lit row (2026-10-10; the enum carries the rule), and
    // OrdinaryLeave by default.
    if (pointer_left_hook_)
        pointer_left_hook_(reason);
}

void GuiInputCore::pointer_motion(double x, double y) {
    pointer_x_ = containing_pixel(x);
    pointer_y_ = containing_pixel(y);
    deliver_motion(pointer_x_, pointer_y_);
}

void GuiInputCore::deliver_motion(int x, int y) {
    // The funnel for every motion that carries a REAL position (the caller
    // classes are at the declaration): the GUI sees the ordinary delivery.
    if (on_motion_) on_motion_(x, y, current_mods());
}

// ---------------------------------------------------------------------------
// Touch event handlers (the finger as the pointer; touch phase 1, 2026-08-11;
// the WINDOWED MODEL, restored at the sixth glass ruling 2026-08-12 and
// TWO-DEADLINE since the eighth, the same day)
//
// The phase machine, the translation contract and the AUTHORITATIVE edge
// inventory live at the touch state block in input_core.h; each body
// below states only its own clause. The GUI sees ordinary pointer deliveries
// (one finger), the touch-nav hooks (the pan and the pinch), the region
// hooks (the hold) or the caret hooks (the editor field's drag) and nothing
// else.
// ---------------------------------------------------------------------------

void GuiInputCore::maybe_resolve_touch_window() {
    if (touch_phase_ != TouchPhase::Pending) return;
    // THE REGION HOLD IS A MOTIONLESS PRESS (architect 2026-10-22, "2 yes"):
    // the expiry is the hold ONLY WHILE THE CONTACT HAS NEVER LEFT THE
    // STILLNESS RADIUS round its down point (touch_window_left_still_,
    // kDefaultTouchStillnessPx — 2 Windows px, a third of the slop). A
    // contact that has left it and not crossed the slop is a pan starting
    // slowly: its window stays Pending with no deadline to the slop crossing
    // (the nav) or the lift (the tap — a sub-slop lift is a tap already),
    // and no hold can form again before that lift — the latch is a latch,
    // cleared only at the next down. MEASURED THAT DAY on the tablet: a press creeping
    // slower than the slop over the 300 ms beat (6 W, 18 device px at
    // 300 %: under ~60 px/s) reached the expiry first and became the hold,
    // so every slow-starting pan swept a trim region and seated the playhead
    // at the press point — what his test swipes left behind. A latch read
    // here, ahead of the clock, so a strayed contact never pays the time
    // compare.
    if (touch_window_left_still_) return;
    if (gui_monotonic_us() < touch_window_deadline_us_) return;
    // The window EXPIRED with one MOTIONLESS contact down, and only the
    // ZONE'S window can: it is THE REGION HOLD — the region former armed at
    // the down point, so hold-then-drag sweeps a region (the eighth glass
    // ruling).
    //
    // THE EDITOR FIELD NEVER GETS HERE, because its window has no deadline to
    // expire (kTouchWindowNoExpiry at the down site): a finger resting in the
    // field is still Pending, and its lift is a TAP. Its two resolutions are
    // the slop crossing (the caret drag, at the motion site) and the lift (the
    // tap, at touch_up), so nothing about it is decided by a duration. And no
    // other window exists to expire: off the zone and the field the down is
    // the pointer on contact (touch_down's Idle arm).
    resolve_touch_window_to_region();
}

void GuiInputCore::resolve_touch_window_to_pointer() {
    // Pending -> Pointer, delivering what the window withheld: the synthesized
    // entry motion at the ORIGINAL down point (the pointer enter's own shape —
    // the first "the pointer is here" notification), the left press there, and
    // any queued motion. Shared by all three pointer resolutions: the
    // OFF-ZONE DOWN (on contact, 2026-09-25 — off the pan zone and outside
    // an editor's field no window opens), the field's DOUBLE PRESS (on
    // contact at its very down), and the TAP, which reaches here from the
    // two surfaces that do open a window — the zone and the field — while
    // their other resolutions go elsewhere (the crossing to single-finger
    // nav or the caret drag, the zone's expiry to the region hold). The
    // tap's caller delivers the release and the focus-forked translation end
    // itself (deliver_touch_translation_end), immediately after.
    touch_phase_ = TouchPhase::Pointer;
    // THE HOLD BIT GOES UP BEFORE THE ENTRY MOTION: the finger has factually
    // been down since the window opened, so EVERY delivery in this burst —
    // the entry motion included — reads primary_button_held through
    // current_mods().
    touch_left_held_ = true;
    // Touch positions are fractional on real panels; every delivery names the
    // pixel that contains one (containing_pixel, the one owner).
    const int down_x = containing_pixel(touch_down_x_);
    const int down_y = containing_pixel(touch_down_y_);
    deliver_motion(down_x, down_y);
    // A pointer-button press is a context event that kills an armed key repeat
    // (layer 1 of the repeat contract).
    repeat_key_ = 0;
    if (on_button_press_)
        on_button_press_(GuiMouseButton::Left, down_x, down_y, current_mods());
    // The queued motion: the tap's sub-slop drift (the two on-contact
    // resolutions have none — the contact has not moved yet), the finger's
    // latest position, delivered after the press so it reaches the lift's
    // release where the finger actually was.
    // DELIVERED ONLY STRICTLY INSIDE THE SLOP IN THE GUI'S OWN METRIC
    // (architect 2026-10-30, the rounding sliver): the window's slop test
    // runs on the UNROUNDED doubles (touch_motion's Pending arm), while the
    // GUI judges a press's drag on the delivered CELLS against the same
    // number (the twin-gate invariant at set_touch_slop_px) — so a drift
    // that stayed under the slop by a fraction could land its cell exactly at
    // the slop and make a tap the GUI's drag (the waveform's pending reading
    // `moved`, the deferred click act lost). The cells' own Chebyshev
    // distance is asked here instead, so what the window called a tap is a
    // tap on the GUI's side by construction; a drift at the slop in cells is
    // simply not delivered (the release still lands at the finger's cell, and
    // no release body re-judges travel).
    if (touch_window_moved_ &&
        (touch_last_x_ != touch_down_x_ || touch_last_y_ != touch_down_y_)) {
        const int last_x = containing_pixel(touch_last_x_);
        const int last_y = containing_pixel(touch_last_y_);
        if (std::max(std::abs(last_x - down_x), std::abs(last_y - down_y)) <
            touch_slop_px_)
            deliver_motion(last_x, last_y);
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

void GuiInputCore::resolve_touch_window_to_caret() {
    // Pending -> Caret (2026-09-05; contract at the declaration and at the
    // editor_field query): the finger CROSSED THE SLOP with its down point
    // in an open editor's field, so it now DRIVES THE EDITOR'S CARET through
    // the caret trio and NOTHING pointer-shaped starts — no entry motion, no
    // press, the touch hold never raised (the region model, not the Pointer
    // one). THE CROSSING IS THE ONLY ROAD HERE: the field's window carries
    // no deadline, so a motionless finger never arrives at this resolution
    // however long it rests — it stays Pending and its lift is a tap (the
    // constants block's no-expiry ruling). The begin fires at the DOWN
    // point — the GUI seats the caret where the finger landed — and the
    // window's travel is the drag's own first leg, the crossing position
    // staged for the frame boundary exactly as the region resolver stages
    // its drift, so the caret is seated at the landing and then under the
    // finger in one burst.
    touch_phase_ = TouchPhase::Caret;
    if (touch_caret_begin_hook_)
        touch_caret_begin_hook_(
            containing_pixel(touch_down_x_),
            containing_pixel(touch_down_y_));
    touch_caret_frame_dirty_ =
        touch_window_moved_ &&
        (touch_last_x_ != touch_down_x_ || touch_last_y_ != touch_down_y_);
    touch_window_moved_ = false;
}

void GuiInputCore::flush_touch_frame_motion() {
    // Pointer-phase motion is coalesced to the touch_frame boundary, and a
    // button delivery in the same frame must see the latest position first
    // (delivered while the hold still reads held, so the motion takes the
    // live-drag path).
    // THE INVARIANT, stated once here (the flush owner): the staged motion is
    // the FINGER's own, so every end of the touch translation FLUSHES it
    // rather than clearing it — end_touch_left_hold flushes UNCONDITIONALLY,
    // ahead of its delivery. The only bare
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
    // The staged TOUCH motion flushes UNCONDITIONALLY, before the delivery
    // and while this bit still reads held: the finger's final position is
    // owed to on_motion (the invariant at flush_touch_frame_motion, the flush
    // owner). Then clear; then deliver at the owner's last position.
    flush_touch_frame_motion();
    // WHAT is delivered FORKS ON clean_release,
    // and the fork is one line at the bottom of this body:
    //   * CLEAN (the finger's own lift, and nothing else since 2026-08-29) —
    //     the left RELEASE: the press was a click, and every act-at-lift
    //     surface runs.
    //   * ABNORMAL (THE HARD ENDS since 2026-08-29 — a touch the system
    //     takes away is not a click unless it moved; the arm is at
    //     hard_end_touch_stream) — the product's own LOST-BUTTON
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
    //     deferred click among them. The ruling is the architect's
    //     (2026-08-29): an edge back-swipe that began on a bottom-row button
    //     toggled playback or authored on its way out of the app, the clean
    //     release it used to deliver speaking for a lift that never happened.
    //     AND THE ARMS THE CHROME / MODAL / MENU SURFACES HOLD ARE
    //     DROPPED BY THAT SAME MOTION, on EITHER arm of the focus fork below:
    //     the GUI ends its release-time claims — the chrome
    //     press, the modal's armed button, the popup's item claim — at exactly
    //     this edge, an unheld motion while one stands
    //     (clear_release_time_press_arms, at on_motion's top). That is a
    //     correction of what this comment used to say: those claims are NOT
    //     button-lost consumers of their own, so calling the end "the standing
    //     lost-button shape" was true of the motion-driven drag states and false
    //     of them, and with the pointer focused in the window — where the fork
    //     takes the restore motion instead of the leave hook — a stale arm
    //     could survive and swallow the next tap. The promise now is flat:
    //     an abnormal end ends every arm the vanished press could have
    //     committed, because the finger that armed them is not going to lift —
    //     and since 2026-08-29 that promise is what carries the HARD ENDS too,
    //     where the finger is not going to lift because the window system took
    //     it.
    const bool deliver = clean_release ? on_button_release_ != nullptr
                                       : on_motion_ != nullptr;
    touch_left_held_ = false;
    if (deliver) {
        const int x = containing_pixel(touch_last_x_);
        const int y = containing_pixel(touch_last_y_);
        if (clean_release)
            on_button_release_(GuiMouseButton::Left, x, y, current_mods());
        else
            deliver_motion(x, y);   // the button-lost edge (the fork above)
    }
    // The return is the END'S OWN EDGE: every caller ends the
    // translation through deliver_touch_translation_end, which acts iff the
    // end was delivered.
    return deliver;
}

void GuiInputCore::deliver_touch_translation_end(bool clean_release) {
    // The end's own edge first: with no hold standing nothing below fires.
    // clean_release is passed straight through and decides only WHAT that edge
    // delivers — the left release, or the lost-button motion a hard end needs
    // (the fork and its whole rationale are at end_touch_left_hold). Everything
    // below is shared verbatim: whichever end it was, the finger is no longer
    // the pointer, and the fork below answers where the pointer now IS.
    if (!end_touch_left_hold(clean_release)) return;
    // THE END FORKS ON POINTER FOCUS: the finger ceasing to BE the pointer —
    // its lift, or a hard end taking it away — means the unified pointer is
    // now wherever the hovering pen is, or nowhere:
    //   * pointer FOCUSED (pointer_enter / pointer_leave, which touch never
    //     writes, and the Android pen's lift through pointer_focus_at — the
    //     pen goes on hovering where it lifted, architect 2026-10-07, so its
    //     lift is no leave and the position below is the lift's) — synthesize
    //     an ordinary MOTION at its last platform-tracked position
    //     (pointer_x_/pointer_y_, the hovering pen's own fields under the
    //     recorded split) INSTEAD of the leave: the motion re-derives hover
    //     from truth. It runs the ordinary motion path WHOLE — deliberately
    //     unmarked, so every consequence of the pointer standing there is the
    //     standing rules' own; suppression would need a "restore" mark on the
    //     motion, a device branch in spirit (the judgment's one record).
    //   * pointer NOT focused — the leave: no pen hovers in the window, so
    //     the contact ending IS the pointer leaving. Its REASON
    //     is the one term clean_release still decides here: the lift passes
    //     TouchLift, the hard end OrdinaryLeave. The hover tooltip is the
    //     consumer that tells them apart — a lift is a release under a
    //     pointer that has not moved, so the wait's button, its anchor and
    //     its spent state stand across it and a later hover on that
    //     button starts no wait until it has left it (the rule is at
    //     AppState::RedesignTooltip) — while a contact
    //     the window system took ends the tooltip's hover as a hard end
    //     (GuiPointerLeaveReason carries both).
    // The mods are current_mods() at delivery: the touch bit is already down,
    // so the motion honestly reads unheld — an ordinary resting motion.
    // THIS IS THE ONE DELIBERATE TOUCH-SIDE READER OF pointer_focused_ (the
    // state block's recorded split): it asks where the pointer now is and
    // gates no touch delivery.
    if (pointer_focused_) {
        // The hovering pen's own position again (deliver_motion's contract).
        deliver_motion(pointer_x_, pointer_y_);
        return;
    }
    if (pointer_left_hook_)
        pointer_left_hook_(clean_release ? GuiPointerLeaveReason::TouchLift
                                         : GuiPointerLeaveReason::OrdinaryLeave);
}

void GuiInputCore::touch_down(int32_t id, double x, double y,
                              GuiTouchTool tool) {
    // An event past the deadline sees the resolved phase (Region).
    maybe_resolve_touch_window();
    ++touch_point_count_;
    switch (touch_phase_) {
        case TouchPhase::Idle:
            // The FIRST contact: remember {id, tool, position} and ask the
            // surface questions once. Whether a WINDOW opens at all is their
            // answer (the constants above own the rationale): on the pan zone
            // and in an open editor's field it does and NOTHING is delivered
            // yet; anywhere else the down resolves to the pointer here, on
            // contact.
            touch_phase_     = TouchPhase::Pending;
            touch_owner_id_  = id;
            touch_owner_tool_ = tool;
            touch_down_x_ = touch_last_x_ = x;
            touch_down_y_ = touch_last_y_ = y;
            touch_window_moved_ = false;
            touch_window_left_still_ = false;
            // The PAN-ZONE answer is captured ONCE, here at the down (the
            // phone model): it decides whether the window opens, and the
            // window's slop crossing, expiry and second-down arm all fork on
            // it. Surface geometry only, by the query's contract. Null hook =
            // no pan surface.
            touch_down_in_pan_zone_ =
                touch_pan_zone_hook_ &&
                touch_pan_zone_hook_(containing_pixel(x),
                                     containing_pixel(y));
            // THE EDITOR-FIELD ANSWER rides beside it (2026-09-05), the
            // same shape once more: asked once here, captured, cleared with
            // it. It forks the window's SLOP CROSSING toward the caret
            // drag (at the motion site) — the field is off the pan zone, the
            // waveform alone on the GUI's answer, so the two bits cannot
            // both be true — it takes the window's DEADLINE AWAY ALTOGETHER
            // just below, and its DoublePress answer resolves the window at
            // the bottom of this arm, on this very down.
            touch_down_in_editor_field_ =
                touch_editor_field_hook_
                    ? touch_editor_field_hook_(containing_pixel(x),
                                               containing_pixel(y))
                    : GuiTouchEditorField::Outside;
            // THE DEADLINE: ON the zone the window runs to the REGION-HOLD
            // beat (kTouchRegionHoldMs, the hold delay); IN AN
            // OPEN EDITOR'S FIELD it runs to NOTHING (kTouchWindowNoExpiry —
            // the field has no hold meaning, so a motionless finger stays
            // Pending and its lift is the tap that seeds the double tap; the
            // constants block carries the ruling). The arithmetic on the zone:
            // a tap still lifts before that beat and delivers whole at
            // the lift, a drag still crosses the 8 px slop into the pan within
            // the first frames, so the stretch costs neither — only the
            // deliberate motionless hold ever reaches the beat. Monotonic,
            // not the event timestamp (whose base this program never compares
            // against). A contact that resolves on this very call takes the
            // zone's arithmetic and never reads it.
            touch_window_deadline_us_ =
                touch_down_in_editor_field_ == GuiTouchEditorField::Field
                    ? kTouchWindowNoExpiry
                    : gui_monotonic_us() +
                          static_cast<uint64_t>(kTouchRegionHoldMs) * 1000ull;
            // THE ON-CONTACT RESOLUTIONS — the third clause (content acts the
            // moment its identity is certain), twice:
            //   * THE DOUBLE PRESS (2026-09-05). A down inside the editor's
            //     field within the double-click window and slack of a seed
            //     the GUI holds can only be the SECOND PRESS — a tap would
            //     have been one already, and a drag from it is the
            //     double-click-drag, which the pointer road carries — so the
            //     entry motion and the press land at this down point, the
            //     GUI's consumed second press selects the word and arms its
            //     word-wise drag, and every motion from here is Pointer motion
            //     driving that arm. A lift without motion is then the release,
            //     exactly the double tap.
            //   * EVERY DOWN OFF THE PAN ZONE AND OUTSIDE AN OPEN EDITOR'S
            //     FIELD (architect 2026-09-25, for the pen and the finger
            //     alike: "zoom is not useful outside waveform", and the second
            //     finger's only use is the pinch). Nothing a window could
            //     decide is meant there — no pan, no region hold, no pinch —
            //     so the press is the pointer's at the down: a flag selects
            //     and lands, a trim endcap or the bridge grabs, a chrome
            //     button arms, all with no wait, and the finger's motion from
            //     here is the pointer's own drag (the GUI's press-becomes-drag
            //     gate, the slop's twin, decides it). THE CHROME'S RULES ARE
            //     THE POINTER ROAD'S, UNTOUCHED: an icon still acts at the
            //     lift and its shift twin is still the long press timed from
            //     the press's own clock.
            //     A second contact landing from here is ignored (the Pointer
            //     arm below).
            // Nothing is queued in either case (the contact has not moved).
            if (touch_down_in_editor_field_ ==
                    GuiTouchEditorField::DoublePress ||
                (!touch_down_in_pan_zone_ &&
                 touch_down_in_editor_field_ == GuiTouchEditorField::Outside))
                resolve_touch_window_to_pointer();
            break;
        case TouchPhase::Pending: {
            if (id == touch_owner_id_) break;  // protocol nonsense; ignore
            // A SECOND contact inside the window. ON THE ZONE it is the
            // two-finger navigation gesture, and no press was ever delivered
            // — nothing to unwind, which is the window's whole purpose there
            // (the jump-free pinch). ANYWHERE ELSE — the editor field, the
            // one other place a window opens — it is IGNORED, recorded (the
            // count above) and not routed: a pinch is admitted only from the
            // zone's window (architect 2026-09-25, "zoom is not useful
            // outside waveform"). AND THE PEN IS NEVER A PINCH MEMBER (the
            // same ruling's third amendment), in either order: a finger
            // landing beside a pen's window, or a pen beside a finger's, is
            // ignored the same way, so a hand resting on the glass while the
            // pen draws changes nothing. The owner's window runs on either
            // way. (A pinch whose FIRST finger lands on the waveform and
            // whose second lands off it still zooms: the first contact's
            // zone answer governs.)
            if (!touch_down_in_pan_zone_ ||
                touch_owner_tool_ == GuiTouchTool::Pen ||
                tool == GuiTouchTool::Pen)
                break;
            // The pair is seeded from the
            // owner's latest position and the new point; the latch reference
            // and the per-frame delta basis both start here. touch_nav_single_
            // is set EXPLICITLY at every Nav entry (here, the single-finger
            // resolve, and the single-finger upgrade): a normal nav end does
            // not run the one forget, so a stale flag from a finished
            // single-finger pan would otherwise leak into the next gesture.
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
        case TouchPhase::Pointer:
            // A SECOND contact during a live translation is IGNORED whole —
            // recorded (the count above), not routed, whether the first has
            // moved or not (architect 2026-09-25: a pinch is admitted only
            // from the pan zone's window, and a live translation lies off
            // the zone by construction — the zone's one road here is the tap,
            // which ends in the same call). The first contact's press, drag
            // or hold simply continues.
            break;
        case TouchPhase::Nav:
            // THE PEN IS NEVER A PINCH MEMBER here either (2026-09-25): a
            // finger landing while the pen pans or zooms, or a pen landing
            // beside a panning finger, is ignored like a third finger and the
            // single-finger gesture runs on.
            if (touch_nav_single_ && id != touch_owner_id_ &&
                touch_owner_tool_ != GuiTouchTool::Pen &&
                tool != GuiTouchTool::Pen) {
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
            // Otherwise a third finger (or a pen-side second contact) is
            // ignored — recorded (the count above), not routed: mid-gesture
            // finger-count changes do not mutate a committed gesture (the
            // any-end-commits family).
            break;
        case TouchPhase::Region:
            // A second finger during the region gesture is IGNORED whole —
            // recorded (the count above), not routed: a committed gesture,
            // the moved-drag rule's family (the edge inventory's clause).
            break;
        case TouchPhase::Caret:
            // A second finger during the caret drag is IGNORED whole on the
            // region clause's own terms: the finger becomes nothing, not a
            // pinch pivot inside the field (the field's pinch is a pair
            // landing inside the WINDOW, the Pending arm above).
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
            if (id != touch_owner_id_) break;  // an ignored contact's own up;
                                                // the owner's falls through
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
            // THE TRANSLATION END RIDES THE RELEASE'S OWN EDGE, and
            // it FORKS on pointer focus — the ordinary leave when no pen
            // hovers in the window, a restore MOTION at the pen's own position
            // when one does (the pen's lift off an empty glass). The fork and
            // the rationale live at deliver_touch_translation_end, the one
            // owner this site and the hard end call. THE LIFT IS A CLEAN END, AND SINCE
            // 2026-08-29 IT IS THE ONLY ONE — the finger really did leave, so
            // its press was a click and every act-at-lift surface is owed its
            // act, while the hard end, where no finger left, passes false; the
            // fork is at end_touch_left_hold.
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
                // it: the backend delivers an up event's whole snapshot as
                // motion and then the up, before the touch_frame that closes
                // the batch (platform_android.cpp's up arm), so when the pair
                // breaks this site takes the first up with the pinch's last —
                // often only — motion still staged, and dropping it
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
                    deliver_touch_nav_frame(
                        /*deliver_even_if_no_op=*/false);
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
                // AND THE TRANSITION ANNOUNCES ITSELF, even where nothing
                // moved. The GUI learns the pinch is over from a delivered
                // frame ARRIVING not-two-finger — that is the seat's whole
                // clear road (apply_touch_nav_update's leading line, and
                // TouchNavZoomState's lifecycle) — but a survivor that does
                // not move produces no such frame: its next frames carry
                // dx 0.0 and ratio 1.0 and die at the exact-no-op return
                // below, so the dead pinch's pivot stayed seated and its
                // stem stayed painted under one finger, and a second finger
                // landing elsewhere pinched about the OLD song point. So the
                // downgrade itself delivers ONE single-finger frame, exempt
                // from that return and from nothing else: it is measured
                // AFTER the rebase, so it carries the survivor's own
                // centroid with dx 0.0 and ratio 1.0 by construction (the
                // rebase wrote last_cx_/last_dist_ to exactly those bases) —
                // an honest statement of the new geometry that applies
                // nothing, seats nothing and consumes no double-click
                // candidate, because the GUI's own no-op return sits above
                // that clear. It stays inside the update hook's vocabulary:
                // the end hook is still NOT owed here (a downgrade is a
                // transform, not an end) and no hook member was added.
                // GATED ON A PRIOR DELIVERY, which is exactly the condition
                // under which the GUI can be holding a seat at all (a seat
                // exists only where a frame was delivered): a sub-latch pair
                // announces nothing and still costs the GUI nothing, and
                // this call cannot latch a gesture that never latched — the
                // latch arm measures the survivor against the bases the
                // rebase just seated on it, which are zero on both axes.
                if (touch_nav_delivered_)
                    deliver_touch_nav_frame(/*deliver_even_if_no_op=*/true);
                break;
            }
            // The LAST nav finger lifting ENDS the gesture (any end commits).
            // The finger's own lift DELIVERS the staged dirty frame first (the
            // end split's finger-up clause, at end_touch_nav_gesture). No
            // release and no translation end: a nav gesture never held the
            // logical button (no nav entry delivers a press).
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
        case TouchPhase::Caret:
            if (id != touch_owner_id_) break;  // an ignored finger lifting
            // The owner's lift ends the caret drag on the region arm's
            // terms: the staged dirty frame delivers first (the finger's own
            // final leg — the caret lands where the finger lifted), then
            // caret_end (the drain rule at the contract). No release and no
            // translation end: nothing pointer-shaped ever started.
            end_touch_caret_gesture(/*deliver_final_frame=*/true);
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
            // THE STILLNESS LATCH (2026-10-22; the rule at
            // maybe_resolve_touch_window): once the contact has left the
            // stillness radius of its down point, the window's expiry can no
            // longer be the region hold — it stays Pending with no deadline
            // until the slop crossing below or the lift. A latch, not a
            // distance read at the expiry: a finger that strays and returns
            // has not been motionless.
            if (std::max(std::abs(x - touch_down_x_),
                         std::abs(y - touch_down_y_)) >= touch_stillness_px_)
                touch_window_left_still_ = true;
            // Motion beyond the slop resolves the window EARLY — a finger
            // already dragging should not wait the window out — and FORKS on
            // the down point's captured answers, the only two a window opens
            // on (touch_down's Idle arm): on the pan surface the drag IS the
            // single-finger nav (the phone model — the pan, or under the ctrl
            // bit the one-finger zoom; no press ever delivered); IN AN OPEN
            // EDITOR'S FIELD it is the CARET DRAG (2026-09-05): the field is
            // off the pan zone, and its quick drag moves the caret rather
            // than reaching the pointer's press (the editor_field query's
            // contract).
            if (std::max(std::abs(x - touch_down_x_),
                         std::abs(y - touch_down_y_)) >= touch_slop_px_) {
                if (touch_down_in_pan_zone_)
                    resolve_touch_window_to_single_nav();
                else
                    resolve_touch_window_to_caret();
            }
            break;
        case TouchPhase::Pointer:
            if (id != touch_owner_id_) break;   // ignored fingers stay ignored
            touch_last_x_ = x;
            touch_last_y_ = y;
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
        case TouchPhase::Caret:
            if (id != touch_owner_id_) break;   // ignored fingers stay ignored
            touch_last_x_ = x;
            touch_last_y_ = y;
            // The same cadence: one caret_update per touch_frame.
            touch_caret_frame_dirty_ = true;
            break;
        case TouchPhase::Idle:
        case TouchPhase::Drain:
            break;
    }
}

void GuiInputCore::touch_frame() {
    // The per-frame drain: one motion delivery
    // (Pointer), one nav update (Nav), one region update (Region) or one
    // caret update (Caret) per logical touch frame, whatever the sensor rate.
    maybe_resolve_touch_window();
    if (touch_phase_ == TouchPhase::Pointer) {
        flush_touch_frame_motion();
    } else if (touch_phase_ == TouchPhase::Nav && touch_nav_frame_dirty_) {
        touch_nav_frame_dirty_ = false;
        deliver_touch_nav_frame(/*deliver_even_if_no_op=*/false);
    } else if (touch_phase_ == TouchPhase::Region &&
               touch_region_frame_dirty_) {
        touch_region_frame_dirty_ = false;
        if (touch_region_update_hook_)
            touch_region_update_hook_(
                containing_pixel(touch_last_x_),
                containing_pixel(touch_last_y_));
    } else if (touch_phase_ == TouchPhase::Caret &&
               touch_caret_frame_dirty_) {
        touch_caret_frame_dirty_ = false;
        if (touch_caret_update_hook_)
            touch_caret_update_hook_(
                containing_pixel(touch_last_x_),
                containing_pixel(touch_last_y_));
    }
}

void GuiInputCore::deliver_touch_nav_frame(bool deliver_even_if_no_op) {
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
        // exists only by crossing the window's slop, the same distance
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
    // A no-op frame delivers nothing — with TWO exemptions, passed in by
    // their sites alone (each site's own record): the DOWNGRADE, whose
    // two-to-one transition must reach the GUI even when the survivor is
    // standing still, because a delivered not-two-finger frame is the only
    // thing that tells it the pinch is over; and the CTRL EDGE under a live
    // single-finger nav (set_ctrl), whose frame carries the new bit so
    // the one-finger zoom's pivot seats or clears at the edge. Nothing else
    // may set the flag: a stream of zero-delta frames is noise the GUI would
    // have to filter again.
    if (dx == 0.0 && ratio == 1.0 && !deliver_even_if_no_op) return;
    touch_nav_delivered_ = true;
    if (touch_nav_update_hook_) {
        GuiTouchNavFrame frame;
        frame.x          = containing_pixel(cx);
        frame.y          = containing_pixel(cy);
        frame.cx         = cx;
        frame.dx         = dx;
        frame.dist_ratio = ratio;
        // The finger count is the GUI's one fork (contract at
        // GuiTouchNavFrame, gui_input.h): it discards dx on a two-finger
        // frame — two fingers zoom and never pan — so this layer still
        // measures and delivers both deltas and applies no policy of its own.
        frame.two_finger = !touch_nav_single_;
        // THE LIVE CTRL BIT, read at delivery (field contract at
        // GuiTouchNavFrame): on a single-finger frame it is the GUI's
        // pan-or-zoom fork (the S Pen's button on the tablet, 2026-09-25).
        // Delivering it is not policy — the tracked ctrl, handed over; a ctrl
        // edge between frames is delivered by set_ctrl itself, the staged
        // frame under the old bit and an exempt one under the new.
        frame.ctrl = mod_ctrl_;
        touch_nav_update_hook_(frame);
    }
}

void GuiInputCore::end_touch_nav_gesture(bool deliver_final_frame) {
    // THE END SPLIT (recorded here, at the one owner; each caller passes its
    // own clause and the edge inventory at the touch state block names both):
    // the backend delivers the terminating up BEFORE the touch_frame that
    // closes its batch (platform_android.cpp's up arm, the event's snapshot
    // staged as motion first), so motion batched with the end is still
    // staged in touch_nav_frame_dirty_ when this runs.
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
    //     translation's hard end is deliberate and survives that arm's move to
    //     the abnormal end (2026-08-29): the translation's edge MUST DELIVER
    //     SOMETHING (a vanished hold would latch the drag-modal gate with no
    //     event left to lift it) and only WHAT it delivers changed — a
    //     lost-button motion rather than a release — while dropped nav motion
    //     wedges nothing: nothing the
    //     gesture leaves in the GUI is held OPEN. Its one GUI-side record
    //     since 2026-08-14 is the pinch's seated pivot (TouchNavZoomState),
    //     and it cannot survive a hard end: the seat exists only where a frame
    //     was DELIVERED (it is taken by the first zoom frame — two-finger, or
    //     one-finger carrying ctrl — that survives the GUI's refusal, applied
    //     or not), which is exactly the condition this end hook is owed on,
    //     and the hook clears it (the pen's retained anchor is kept only
    //     inside the pen's own lift, never at a cancel or a capability loss).
    if (deliver_final_frame && touch_nav_frame_dirty_) {
        touch_nav_frame_dirty_ = false;
        deliver_touch_nav_frame(/*deliver_even_if_no_op=*/false);
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

void GuiInputCore::end_touch_caret_gesture(bool deliver_final_frame) {
    // The Region end restated for the caret trio (the contract at the
    // declaration): the finger's own lift delivers the staged frame first —
    // the caret lands where the finger lifted — while the hard ends drop it
    // (a touch the system takes away seats nothing more). caret_end then
    // fires UNCONDITIONALLY: the GUI's drain rule owes nothing to the text
    // but the blink restart, and a body that fires every time is one fewer
    // gate to keep in step with a begin that always fired.
    if (deliver_final_frame && touch_caret_frame_dirty_) {
        touch_caret_frame_dirty_ = false;
        if (touch_caret_update_hook_)
            touch_caret_update_hook_(
                containing_pixel(touch_last_x_),
                containing_pixel(touch_last_y_));
    }
    touch_caret_frame_dirty_ = false;
    if (touch_caret_end_hook_) touch_caret_end_hook_();
}

void GuiInputCore::touch_cancel() {
    // The window system claims the touches (its own gesture recognition, a
    // grab; AMOTION_EVENT_ACTION_CANCEL).
    // One contract with touch-capability loss, in full.
    hard_end_touch_stream();
}

void GuiInputCore::touch_capability_lost() {
    // THE HARD END OF THE TOUCH STREAM: no motion, up, or cancel will arrive
    // from this device again. The contract is the cancel's, shared whole
    // (hard_end_touch_stream): a live pointer translation takes the ABNORMAL
    // end — a lost-button motion delivered at the last position, so a moved
    // drag finalizes and an unmoved press commits nothing, then the
    // focus-forked translation end (deliver_touch_translation_end: the
    // ordinary leave, or a restore motion at a hovering pen) — a live nav
    // gesture (single- or two-finger) ends through its end path with its
    // staged final frame dropped, an unresolved window drops silently, and
    // all touch state is forgotten.
    hard_end_touch_stream();
}

void GuiInputCore::hard_end_touch_stream() {
    switch (touch_phase_) {
        case TouchPhase::Pointer:
            // A live translation ENDS, and the END IS DELIVERED — one
            // delivery at the last position (a vanished hold would latch
            // the drag-modal gate with no event left to lift it) — then the
            // translation end ON THAT DELIVERY'S OWN EDGE, exactly as the
            // finger's own lift delivers it: the leave (OrdinaryLeave here,
            // where the lift's is TouchLift), or the focus-forked restore
            // motion when a pen hovers in the window, through the one owner.
            // AN ABNORMAL END SINCE 2026-08-29 (architect: "a touch the system
            // takes away is not a click unless it moved"), where this arm
            // passed a CLEAN release from the contract's landing until then.
            // A HARD END IS NOT A LIFT: the window system took the contacts —
            // its own gesture recognition, an edge back-swipe, a shade pull, a
            // task switch — and the user never raised the finger.
            // A clean release SPOKE FOR A LIFT THAT DID NOT HAPPEN: it re-hit
            // the box at the last position and every act-at-lift surface ran,
            // so a back-swipe that started on a bottom-row button toggled
            // playback or authored (Undo, Delete, Drop marker) on its way out
            // of the app.
            // THE ABNORMAL END GIVES THIS ARM EXACTLY WHAT IT NEEDED, and the
            // three properties are the whole reason it is the right spelling
            // rather than a new suppression (the fork and its own rationale
            // are at end_touch_left_hold): a MOVED drag still finalizes, so
            // nothing pops mid-gesture and the no-cancel family is untouched;
            // an UNMOVED press commits nothing further; and the drag-modal
            // gate still un-latches, because the lost-button MOTION this
            // delivers ends every member of any_pointer_gesture_active — each
            // motion-driven gesture through its own button-lost arm and the
            // release-time arms through clear_release_time_press_arms (both in
            // GuiInputHandler::on_motion).
            deliver_touch_translation_end(/*clean_release=*/false);
            break;
        case TouchPhase::Nav:
            // A live nav gesture — single- or two-finger, one arm — ends
            // through its end path (commits iff anything was applied),
            // DROPPING its staged dirty frame — the end split's hard-end
            // clause, at end_touch_nav_gesture. No release, no leave: a nav
            // gesture never holds the logical button (no nav entry delivers a
            // press).
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
        case TouchPhase::Caret:
            // The Region arm's shape for the caret trio — staged frame
            // DROPPED (no seat), then caret_end: no selection, no
            // double-click seed, the caret where the last delivered frame
            // put it and the editor still open, so a back-swipe mid-drag
            // leaves the edit standing (the drain rule at the contract).
            end_touch_caret_gesture(/*deliver_final_frame=*/false);
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
    touch_window_left_still_  = false;
    touch_left_held_          = false;  // any delivering edge already ended it
    // A reset, not a swallow: every delivering end flushed this already (the
    // invariant at flush_touch_frame_motion); only never-delivered phases can
    // still reach here with it clear.
    touch_frame_motion_pending_ = false;
    touch_down_in_pan_zone_     = false;
    touch_down_in_editor_field_ = GuiTouchEditorField::Outside;
    touch_owner_tool_           = GuiTouchTool::Finger;
    touch_region_frame_dirty_   = false;
    touch_caret_frame_dirty_    = false;
    touch_nav_single_ = false;
    touch_nav_id2_ = 0;
    touch_nav_x1_ = touch_nav_y1_ = 0.0;
    touch_nav_x2_ = touch_nav_y2_ = 0.0;
    touch_nav_start_cx_ = touch_nav_start_cy_ = touch_nav_start_dist_ = 0.0;
    touch_nav_last_cx_ = touch_nav_last_dist_ = 0.0;
    touch_nav_latched_ = touch_nav_delivered_ = touch_nav_frame_dirty_ = false;
}


// ---------------------------------------------------------------------------
// Setters (callbacks)
// ---------------------------------------------------------------------------

void GuiInputCore::set_on_key(KeyCallback cb)                    { on_key_ = std::move(cb); }
void GuiInputCore::set_on_key_release(KeyReleaseCallback cb)     { on_key_release_ = std::move(cb); }
void GuiInputCore::set_on_button_press(ButtonCallback cb)        { on_button_press_ = std::move(cb); }
void GuiInputCore::set_on_button_release(ButtonCallback cb)      { on_button_release_ = std::move(cb); }
void GuiInputCore::set_on_motion(MotionCallback cb)              { on_motion_ = std::move(cb); }
void GuiInputCore::set_text_editor_active_probe(TextEditorProbe cb) { text_editor_active_probe_ = std::move(cb); }
void GuiInputCore::set_repeat_eligible_probe(RepeatEligibleProbe cb) { repeat_eligible_probe_ = std::move(cb); }
void GuiInputCore::set_pointer_left_hook(std::function<void(GuiPointerLeaveReason)> cb) { pointer_left_hook_ = std::move(cb); }
void GuiInputCore::set_touch_nav_hooks(
    std::function<void(const GuiTouchNavFrame&)> update,
    std::function<void()> end,
    std::function<bool(int x, int y)> pan_zone,
    std::function<void(int x, int y)> region_begin,
    std::function<void(int x, int y)> region_update,
    std::function<void()> region_end,
    std::function<GuiTouchEditorField(int x, int y)> editor_field,
    std::function<void(int x, int y)> caret_begin,
    std::function<void(int x, int y)> caret_update,
    std::function<void()> caret_end) {
    touch_nav_update_hook_    = std::move(update);
    touch_nav_end_hook_       = std::move(end);
    touch_pan_zone_hook_      = std::move(pan_zone);
    touch_region_begin_hook_  = std::move(region_begin);
    touch_region_update_hook_ = std::move(region_update);
    touch_region_end_hook_    = std::move(region_end);
    touch_editor_field_hook_  = std::move(editor_field);
    touch_caret_begin_hook_   = std::move(caret_begin);
    touch_caret_update_hook_  = std::move(caret_update);
    touch_caret_end_hook_     = std::move(caret_end);
}
void GuiInputCore::set_codepoint_probe(
    std::function<uint32_t(uint32_t stable_code)> probe) {
    codepoint_probe_ = std::move(probe);
}
