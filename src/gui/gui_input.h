#pragma once
#include <cstdint>

// Platform-neutral keyboard / mouse input types. Backends translate native
// events at their boundary into these so the rest of the GUI never sees
// platform-specific types. Numeric values mirror the universal keysym
// numbering shared by xkbcommon and the X Window System (xkbcommon was
// derived from that table), so each backend only needs a near-zero-cost
// cast plus a small case-fold for letters.
using GuiKey = uint32_t;

namespace GuiKeys {
    // Letters (X11 lowercase keysym values; case-folded at platform boundary).
    constexpr GuiKey A = 0x0061; constexpr GuiKey B = 0x0062;
    constexpr GuiKey C = 0x0063; constexpr GuiKey D = 0x0064;
    constexpr GuiKey E = 0x0065; constexpr GuiKey F = 0x0066;
    constexpr GuiKey G = 0x0067; constexpr GuiKey H = 0x0068;
    constexpr GuiKey I = 0x0069; constexpr GuiKey J = 0x006a;
    constexpr GuiKey K = 0x006b; constexpr GuiKey L = 0x006c;
    constexpr GuiKey M = 0x006d; constexpr GuiKey N = 0x006e;
    constexpr GuiKey O = 0x006f; constexpr GuiKey P = 0x0070;
    constexpr GuiKey Q = 0x0071; constexpr GuiKey R = 0x0072;
    constexpr GuiKey S = 0x0073; constexpr GuiKey T = 0x0074;
    constexpr GuiKey U = 0x0075; constexpr GuiKey V = 0x0076;
    constexpr GuiKey W = 0x0077; constexpr GuiKey X = 0x0078;
    constexpr GuiKey Y = 0x0079; constexpr GuiKey Z = 0x007a;

    // Digits.
    constexpr GuiKey Digit0 = 0x0030; constexpr GuiKey Digit1 = 0x0031;
    constexpr GuiKey Digit2 = 0x0032; constexpr GuiKey Digit3 = 0x0033;
    constexpr GuiKey Digit4 = 0x0034; constexpr GuiKey Digit5 = 0x0035;
    constexpr GuiKey Digit6 = 0x0036; constexpr GuiKey Digit7 = 0x0037;
    constexpr GuiKey Digit8 = 0x0038; constexpr GuiKey Digit9 = 0x0039;

    // Punctuation (X11 keysym values).
    constexpr GuiKey Space        = 0x0020;
    constexpr GuiKey Apostrophe   = 0x0027;
    constexpr GuiKey Asterisk     = 0x002a;
    constexpr GuiKey Plus         = 0x002b;
    constexpr GuiKey Comma        = 0x002c;
    constexpr GuiKey Minus        = 0x002d;
    constexpr GuiKey Period       = 0x002e;
    constexpr GuiKey Slash        = 0x002f;
    constexpr GuiKey Colon        = 0x003a;
    constexpr GuiKey Semicolon    = 0x003b;
    constexpr GuiKey Equal        = 0x003d;
    constexpr GuiKey At           = 0x0040;
    constexpr GuiKey BracketLeft  = 0x005b;
    constexpr GuiKey BracketRight = 0x005d;

    // Navigation, editing, control.
    constexpr GuiKey BackSpace  = 0xff08;
    constexpr GuiKey Tab        = 0xff09;
    constexpr GuiKey Return     = 0xff0d;
    constexpr GuiKey Escape     = 0xff1b;
    constexpr GuiKey Home       = 0xff50;
    constexpr GuiKey Left       = 0xff51;
    constexpr GuiKey Up         = 0xff52;
    constexpr GuiKey Right      = 0xff53;
    constexpr GuiKey Down       = 0xff54;
    constexpr GuiKey PageUp     = 0xff55;   // XK_Prior
    constexpr GuiKey PageDown   = 0xff56;   // XK_Next
    constexpr GuiKey End        = 0xff57;
    constexpr GuiKey IsoLeftTab = 0xfe20;
    constexpr GuiKey KpEnter    = 0xff8d;
    constexpr GuiKey Delete     = 0xffff;
}

// The keyboard key the platform layer translates into BTN_LEFT (press =
// button down at the pointer position, hold = button held, release = button
// up), except while a text editor is open, when it stays a normal letter.
// Rebinding the emulation to a different key is exactly this one edit. Any
// modifier state (shift/ctrl/alt) rides along to the synthesized button,
// exactly as it would for a physical BTN_LEFT device.
constexpr GuiKey kLeftClickKey = GuiKeys::E;

// THE PRODUCT'S ONE HOLD-BEAT DURATION — how long any deliberate hold must
// rest before it crosses into its held meaning, and THE ONE CADENCE THE HAND
// IS ASKED FOR ANYWHERE. This is the readers' one inventory, re-derived from
// the tree rather than inherited:
//   * the chrome roster's shift long press (kChromeShiftHoldMs, app_state.h),
//     which the RENDER PLAYER'S MODAL ROW joined 2026-08-28 (R37) — its two
//     skips admit a modified press, so their long press reaches the same twin
//     a shift-click does, one constant and one term;
//   * the touch pan zone's region hold (kTouchRegionHoldMs, input_core.cpp);
//   * the hold-repeating buttons' FIRST fire (input_pointer.cpp's arm, where
//     every LATER fire is the compositor's advertised repeat interval);
//   * the Android backend's key-repeat DELAY (platform_android.cpp — the one
//     place the paragraph below does not reach, because that platform
//     advertises no delay to ask for; the ruling is at that site);
//   * and, since 2026-08-27, the DOUBLE-CLICK WINDOW (kDoubleClickMs,
//     app_state.h), which is NOT a hold and is listed apart for that reason:
//     it is the interval a deliberate SECOND TAP has to arrive inside, tied to
//     this beat so the product asks the hand for one cadence and not two;
//   * and, since 2026-08-28, THE UNDO TAP-COALESCE WINDOW (kTapCoalesceMs,
//     undo.h), which is not a hold either and joins the double-click apart
//     from the holds for the same reason: it is the interval a deliberate
//     SECOND PRESS of the same authoring key has to arrive inside to land in
//     the burst's existing undo entry instead of opening its own. It carried
//     its own 500 from 2026-08-01 until the architect tied it to the beat
//     ("the global wait time for long press, key repeat, etc."), and the
//     properties that made it its own number are untouched by the move: it
//     gates the TAP arm alone, and the held-key arm beside it consults no
//     clock at all.
// So a keyboard hold, a chrome shift hold, a touch region hold, a held
// button's first repeat, a double tap and a re-tapped nudge all land on the
// same beat rather than on numbers that happen to be near each other.
//
// 575 ms BY CONVENTION WITH THE COMPOSITOR'S KEY-REPEAT DELAY, matched
// DELIBERATELY and not by coincidence: it is the architect's own labwc
// <repeatDelay>, so the beat the hand already knows from every held key on
// the desktop is the beat this program's own holds use.
//
// THE KEYBOARD'S OWN DELAY IS NOT THIS CONSTANT AND NEVER SHOULD BE WHERE THE
// PLATFORM ADVERTISES ONE. Key repeat arrives from the compositor through
// wl_keyboard.repeat_info and reaches the input core through set_repeat_info,
// so it tracks the user's desktop setting and moves with every other
// application if that setting is ever edited. This constant is the number OUR
// gesture holds use to agree with it — hard-coding the key delay to match
// would be the one place the product fights the desktop, and it would buy
// nothing, because the two numbers already agree. (Android advertises no delay
// at all to a native activity, which is why the inventory above lists it as a
// reader there: a fallback where there is nothing to track, not a second
// opinion about a setting that exists.)
//
// It rides NO SCALE, deliberately: a duration is not a length, so gui_scale
// has nothing to say about it.
constexpr int kHoldBeatMs = 575;

// THE NOTIFICATION CARD'S LIFE (architect design 2026-08-29): a NORMAL card
// leaves the stack on its own this long after it became VISIBLE, the pointer
// resting on it pausing the clock (the model is at GuiNotifications,
// notifications.h; the sampler is its fire_if_due on the run loop's deadline
// tick, beside the beat's own readers). It is NOT the beat and does not read
// it: a card's life is neither a hold nor a second tap, it is how long a
// sentence stays readable, and Plasma's own default for the same thing is
// what it takes. A duration like the beat, so it rides no scale either.
constexpr int kNotificationMs = 5000;

struct GuiInputState {
    bool     ctrl                = false;
    bool     shift               = false;
    bool     alt                 = false;
    bool     primary_button_held = false;
    // The Unicode codepoint this key event resolves to under the live
    // keyboard state (shift / layout / compose applied), as computed by
    // the backend — xkb_state_key_get_utf32 on Wayland; 0 when the key
    // produces no character (function keys, bare modifiers). It is a FULL
    // CODEPOINT, not a byte — a compose or dead-key sequence arrives here
    // whole (U+2026, U+2014, an accented letter) and the text editors UTF-8
    // encode it on insertion, so nothing truncates at this boundary. THREE
    // CONSUMERS
    // (re-derived 2026-07-30): the text editors' printable-insertion path,
    // repeat_eligible's editor-typing repeat gate, and the modal prompt's
    // LETTER responses — the last joined 2026-07-30, because the prompt is the
    // product's one CASE-SENSITIVE letter surface and the case-folded GuiKey
    // cannot express that (CapsLock defeated the old !shift spelling). Every
    // other consumer reads the GuiKey and ignores this.
    uint32_t codepoint           = 0;
    // True iff this key event is a SYNTHESIZED REPEAT — one the process
    // generated itself from a HELD input rather than a fresh physical press.
    // TWO PRODUCERS, one per surface: GuiInputCore::maybe_fire_repeat for a
    // held KEY (the portable input core's own, input_core.cpp), and
    // GuiInputHandler::tick_chrome_press_repeat for a held BUTTON (the chord
    // table's `repeats` rows, 2026-08-16 — the pointer twin, which exists so
    // the keyboard-less touch panel has a step run of its own). The two carry the same
    // meaning to the same senior consumer and differ only in what is being
    // held. ONLY THE BUTTON PRODUCER CLEARS THE BIT ON ITS BURST'S OPENER —
    // its first fire goes out with the bit FALSE and takes undo's PHYSICAL
    // arm, because a button's press dispatches nothing (its act is at the
    // lift since 2026-08-13) and the first fire stands in for it (the flip
    // and its argument are at tick_chrome_press_repeat, input_pointer.cpp). A
    // held KEY needs no flip: its physical press acts and IS its burst's
    // opener, so every platform repeat carries the bit as stamped.
    // For the KEY it is a platform-boundary fact in the same
    // spirit as `codepoint`. Its senior consumer is undo coalescing, where it
    // selects
    // the ARM (the hybrid is stated at the head of undo.h): a repeat merges into
    // the burst by IDENTITY with no clock consulted, so a held key coalesces by
    // construction at any compositor's repeat delay. A PHYSICAL press takes the
    // other arm — since 2026-08-01 rapid manual taps of the same kind merge too,
    // on a fixed 500 ms window plus a subject test, and presses beyond it stay
    // separately undoable. The modal focus ring's Enter/Space arm consumes a
    // repeat outright (a press-and-lift act fires once), and the platform's
    // own consumed-edge sites read it so a burst never disarms itself.
    bool     synthesized_repeat  = false;
};

// True for the chord that toggles playback: BARE Space only. Modifier-strict —
// a Space carrying ctrl or alt has no binding, and the ONE shifted form is a
// different act (is_ab_audition_key, below), so none of them may reach a
// toggle (the uniform rule: an unbound modifier combination is a no-op). Return
// / keypad Enter are NOT playback keys — Enter opens the flag editor on the
// focused marker (see the bare-Enter binding in input_handler.cpp). Shared by
// the on_key dispatch (input_handler.cpp) and the read-only allowlist predicate
// (input_key_dispatch.cpp), which are its only two callers; inline so both TUs
// see it, and one owner so the two readers cannot drift.
inline bool is_play_pause_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::Space && !mods.ctrl && !mods.shift && !mods.alt;
}

// True for the chord that runs THE A/B AUDITION (architect 2026-08-26):
// SHIFT+Space exactly — no ctrl, no alt. The shifted Space was the strict
// rule's consumed no-op until that date; this is its one binding, and every
// other decoration on Space stays unbound. The same two readers as the bare
// form's predicate — on_key's dispatch arm and the read-only allowlist — and
// the same one-owner reason. The act itself is GuiAbAudition::start
// (ab_audition.h); the play button's shift-click and long press dispatch this
// chord through on_key (redesign_button_shift_admits).
inline bool is_ab_audition_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::Space && !mods.ctrl && mods.shift && !mods.alt;
}

// True for the chord that opens the OPEN PROJECT prompt (architect
// 2026-08-28): CTRL+O exactly — no shift, no alt. Open was the File menu's one
// act with no key of its own; the convention for it is too strong to spell it
// anywhere else, and Ctrl+O was the strict rule's consumed no-op until this
// date. IT IS NOT bare `o`, which stays the active tab's read-only toggle: the
// two are NEIGHBOURS ON ONE LETTER, NOT TWO HALVES OF ONE ACT, so neither
// reader may fold them together. The same two readers as the Space pair above
// — on_key's dispatch arm (input_handler.cpp) and the read-only allowlist
// (read_only_key_blocked, input_key_dispatch.cpp, which admits the chord
// because the picker authors nothing) — and the same one-owner reason. The act
// is GuiInputHandler::open_project_picker, whose own body carries the gates
// (the modal refusals, the `h` view, the loading state); the File menu's Open
// row dispatches this very chord through on_key like every other command row.
inline bool is_open_project_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::O && mods.ctrl && !mods.shift && !mods.alt;
}

// True for the chord that DROPS A PHASE RESET FROM ANY VIEW (architect
// 2026-08-28): SHIFT+S exactly — no ctrl, no alt. It lands the session in
// T+P, phase reset's home, and drops the lead-in reset at the playhead's own
// musical instant there, so the reset the architect wants while he is looking
// at the warp column costs one key instead of a view trip and back. Ctrl+S is
// the save on this letter and Shift+S was the strict rule's consumed no-op
// until this date; the shifted `s` is the only decoration on it that binds.
//
// IT IS BARE `s`'s ACT WITH THE VIEW TRIP IN FRONT, not a second one: in T+P
// the two chords reach the same one drop body, and everywhere else the
// difference is exactly the two view chokepoints Shift+S runs first (the act
// is GuiInputHandler::drop_phase_reset_in_target_view, input_handler.cpp).
// The same two readers as the Space pair above — on_key's dispatch arm
// (input_handler.cpp) and the read-only allowlist (read_only_key_blocked,
// input_key_dispatch.cpp, which DROPS the chord: the drop is authored
// content, exactly as bare `s` is) — and the same one-owner reason.
inline bool is_phase_reset_drop_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::S && !mods.ctrl && mods.shift && !mods.alt;
}

enum class GuiMouseButton {
    Left,
    Middle,
    Right,
    WheelUp,
    WheelDown
};

// One delivered touch-navigation frame (the nav update hook's payload — the
// platform's deliver_touch_nav_frame builds it, the GUI's
// apply_touch_nav_update consumes it). The platform owns the per-frame delta
// bookkeeping (dx / dist_ratio against the previous DELIVERED frame, the
// latch, the frame coalescing) and applies NO gesture policy of its own; the
// GUI owns the model on top, which since 2026-08-14 is ONE FINGER PANS, TWO
// FINGERS ZOOM (touch.md's two-finger section) — hence the finger count
// below, the one field the GUI forks on. Every field here is read.
struct GuiTouchNavFrame {
    // Current centroid, window px (single-finger: the finger itself). Also
    // the two-finger gesture's zoom pivot.
    int    x = 0;
    int    y = 0;
    // Centroid horizontal delta since the previous delivered frame
    // (fractional — sub-pixel centroid motion accumulates rather than
    // truncating away). Read on SINGLE-finger frames only: two fingers never
    // pan, so the centroid's travel is discarded there.
    double dx = 0.0;
    // Finger-distance ratio current/previous, > 0 always (a degenerate
    // distance under 1 px on either side delivers 1.0; single-finger frames
    // are degenerate by construction, so they always carry 1.0).
    double dist_ratio = 1.0;
    // TWO fingers vs the phone model's one — the GUI's fork between the
    // zoom-only gesture and the pan-only one.
    bool   two_finger = false;
    // THE FIRST FINGER'S DOWN POINT LAY ON A THIN LANE (2026-08-15) — the
    // OVERVIEW STRIP or the TRIM BAR, the class the GUI's
    // touch_point_on_thin_lane answers (its declaration owns what makes a lane a
    // member). Captured ONCE, at the `Idle` down that opened this contact stream
    // (the platform's touch_down_on_thin_lane_), and CONSTANT for the stream's
    // whole life: it is a fact about where the gesture STARTED, never about
    // where the fingers are now. IT HAS TWO READERS, one per door.
    //   * apply_touch_nav_update drops EVERY nav frame carrying it — two
    //     fingers and one alike — because a gesture begun on a thin lane must do
    //     nothing at all rather than fall through to the waveform's pinch and
    //     zoom the view from a strip the user was touching for another reason.
    //   * the PLATFORM's own second-finger fork reads its copy: a second finger
    //     landing during a live translation on such a lane is ignored outright,
    //     so the first finger's drag continues instead of being torn down for a
    //     gesture that would then be refused frame by frame.
    // It answers the DOWN POINT and not the live centroid because these lanes
    // are ~26 px tall while their drags are x-only, so a finger that grabbed a
    // bound wanders far off the strip and a centroid test would change the
    // answer under the fingers; a gesture's surface is decided where it began
    // (the seat, the press-time act and the crossing's mode all follow that
    // rule), so the answer travels on the frame.
    bool   down_on_thin_lane = false;
};
