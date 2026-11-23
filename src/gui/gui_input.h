#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

// Platform-neutral key and pointer input types. The backend translates
// native events at its boundary into these so the rest of the GUI never sees
// platform-specific types. Numeric values are the universal keysym numbering
// of the X Window System (the table xkbcommon carries too), the chord
// grammar's one key space; the on-screen keyboard synthesizes them directly
// (GuiPlatform::synthesize_key).
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
    constexpr GuiKey Backslash    = 0x005c;
    constexpr GuiKey BracketRight = 0x005d;
    constexpr GuiKey Grave        = 0x0060;

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
    // THE ON-SCREEN KEYBOARD'S PASS KEY (architect 2026-11-08): no physical
    // key, so no X11 name; it rides XK_Insert's code, the
    // editing block's one keysym nothing else here names. Its one meaning is
    // THE FLAG EDITOR'S TOGGLE INHERIT ON ITS TEXT (architect 2026-11-20 /
    // 21: "a true toggle inherit"; handle_top_flag_editor_key,
    // GuiFlagEditor::toggle_inherit_editor_text, which rewrites the text and
    // commits nothing); nothing in the main dispatch binds it, so outside
    // that editor it would be an unbound key — and the keyboard greys it
    // everywhere else (onscreen_keyboard::key_live).
    constexpr GuiKey Pass       = 0xff63;
}

// THE HOLD DELAY (architect 2026-09-29) — 300 ms: how long a hand rests on a
// thing before the rest crosses into its held meaning. It is the delay of
// THE PRODUCT'S ONE HOLD and of nothing else: THE
// CHROME SHIFT LONG PRESS (chrome_shift_hold_ms, app_state.h), at all three
// of its read sites — the roster lift, the RENDER PLAYER'S MODAL ROW
// (2026-08-28: its skips and Delete admit a modified press, and their
// long press reaches it), and THE NOTIFICATION CARD'S LIFT
// (2026-10-01: a long press on any card dismisses every card) — one value and
// one term. NO HOLD ON THE TRIM BAR MEANS ANYTHING (architect 2026-11-21:
// "the double tap will be enough, and we can remove the long press"): a
// still press there of any length is a tap, its double tap the bar's
// framing gesture (TrimBarPressSeed, app_state.h), and the barrel's
// press there reads no clock either (PendingClickAct, app_state.h).
// It has NO VISUAL
// ANNOUNCEMENT: the span is measured where the act runs (the lift) and
// nothing on screen marks the moment
// it is crossed. No hold on the waveform reads it: the waveform's sweep is the
// TRIM arm lamp's (architect 2026-11-07; GuiInputCore's touch state block).
// A constant by the tuning rule (a number under tuning is a device key, a
// settled one a constant): he ran 300 on the tablet and ruled it settled
// ("very good"), and the tablet is the authoring device, so the tablet's
// number is the product's.
// It rides NO SCALE: a duration is not a length.
constexpr int kHoldDelayMs = 300;

// THE PRODUCT'S FIXED BEAT (architect 2026-09-29) — 600 ms, the beat of the
// CADENCES; the holds read kHoldDelayMs above (the readers split by WHAT
// THEY MEASURE). This is the beat's one inventory.
//
// A CADENCE measures the gap between presses or the pace of a stream of
// repeats, and none is a hand resting on a thing until it crosses into a held
// meaning, which is what the hold delay times. (THE HOVER TOOLTIP reads
// neither: its waits are GTK's own, 500 and 60 ms — architect 2026-11-22;
// render.h's THE TIMING, AppState::RedesignTooltip.)
// FOUR READERS:
//   * THE DOUBLE TAP'S WINDOW (kDoubleClickMs, app_state.h; architect
//     2026-11-21, "an even 600") — the interval a deliberate SECOND
//     TAP has to arrive inside, on the three double-tap surfaces;
//   * THE UNDO TAP-COALESCE WINDOW (kTapCoalesceMs, undo.h; 2026-08-28) —
//     the interval a deliberate SECOND PRESS of the same
//     authoring key has to arrive inside to land in the burst's existing undo
//     entry instead of opening its own. The architect tied it to the beat
//     ("the global wait time for long press, key repeat, etc."); it gates the
//     TAP arm alone, and the held-key arm beside it consults no clock at all;
//   * THE HOLD-REPEATING BUTTONS' FIRST FIRE (input_pointer.cpp's arm, where
//     every LATER fire is the platform's repeat interval) — a repeat delay,
//     the first beat of a stream;
//   * THE BACKEND'S KEY-REPEAT DELAY (platform_android.cpp — the platform
//     advertises no delay to ask for; the ruling is at that site).
// So a keyboard hold's first repeat, a held button's first repeat, a double
// tap and a re-tapped nudge all land on the same beat.
//
// 600 ms BY CONVENTION WITH THE ARCHITECT'S LAPTOP KEY-REPEAT DELAY, the
// number's source, matched DELIBERATELY and not by coincidence: it is his own
// labwc <repeatDelay> on the laptop (both 600, 2026-09-29, even like the
// product's other pauses), so the beat
// the hand knows from every held key there is the beat this program's own
// cadences use.
// Android advertises no repeat delay at all to a native activity, which is
// why the inventory above lists the backend as a reader: a fallback where
// there is nothing to track, not a second opinion about a setting that
// exists.
//
// It rides NO SCALE, deliberately: a duration is not a length, so gui_scale
// has nothing to say about it.
constexpr int kHoldBeatMs = 600;

// THE NOTIFICATION CARD'S LIFE (architect design 2026-08-29): a NORMAL card
// leaves the stack on its own this long after it became VISIBLE, the pointer
// resting on it, or a press holding it (2026-10-01), pausing the clock (the
// model is at GuiNotifications,
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
    // the backend — the painted key's own character (synthesize_key); 0 when the key
    // produces no character (function keys, bare modifiers). It is a FULL
    // CODEPOINT, not a byte — a compose or dead-key sequence arrives here
    // whole (U+2026, U+2014, an accented letter) and the text editors UTF-8
    // encode it on insertion, so nothing truncates at this boundary. ONE
    // SURFACE READS IT: the text editors, at
    // their two readers in text_editor.cpp — the key classifier (whose
    // printable verdict repeat_eligible's editor-typing repeat gate asks) and
    // handle_key's printable insertion. Every other consumer reads the
    // GuiKey and ignores this.
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
    // lift, 2026-08-13) and the first fire stands in for it (the flip
    // and its argument are at tick_chrome_press_repeat, input_pointer.cpp). A
    // held KEY needs no flip: its physical press acts and IS its burst's
    // opener, so every platform repeat carries the bit as stamped.
    // For the KEY it is a platform-boundary fact in the same
    // spirit as `codepoint`. Its senior consumer is undo coalescing, where it
    // selects
    // the ARM (the hybrid is stated at the head of undo.h): a repeat merges into
    // the burst by IDENTITY with no clock consulted, so a held key coalesces by
    // construction at any repeat delay. A PHYSICAL press takes the
    // other arm — rapid manual taps of the same kind merge too (2026-08-01),
    // on a fixed 600 ms window (kTapCoalesceMs, undo.h, the product's fixed
    // beat) plus a subject test, and presses beyond it stay
    // separately undoable. The platform's
    // own consumed-edge sites read it so a burst never disarms itself.
    bool     synthesized_repeat  = false;
};

// THE MODIFIERS A KEY CARRIES ITSELF (architect 2026-10-29, the ctrl of the
// on-screen keyboard's Copy and Paste; the shift, 2026-11-08: LEFT and RIGHT
// carry the latched Shift, the selection's extend),
// the one producer the keyboard's press router
// (GuiInputHandler::claim_onscreen_keyboard_press). A property of the KEY
// EVENT, never of the tracked state: GuiPlatform::synthesize_key hands it to
// GuiInputCore::key_event, which ORs each bit into the press's
// GuiInputState and into every synthesized repeat armed off it (its
// contract), and a release carries none.
struct GuiCarriedMods {
    bool ctrl  = false;
    bool shift = false;
};

// -- THE ARROW STEP (architect 2026-08-31; ONE RUNG, 2026-11-08) -------------
//
// NO MODIFIER IS A STEP'S MAGNITUDE (architect 2026-11-08: the barrel has no
// chrome road, so no press on the Up / Down keys spells a larger rung). Each
// press steps ONE unit, whatever the
// bare Up / Down's own act steps on the addressed cell — a CENT on the tempo
// (singleton and group), a cent or a hop on a bound — and the held button's
// repeat is the way to go further.
//
// THE HORIZONTAL PAIR HAS NO LADDER (architect 2026-09-21, on every column):
// horizontal placement is
// GRAPHICAL — a multiple of the step is a number nobody thinks in —
// so Shift+Left / Shift+Right are UNBOUND on every column, for the marker and
// the playhead alike (chord_is_bound), and silent as every unbound chord is.
// Left / Right take ONE STEP IN THE ACTIVE COLUMN'S UNIT (horizontal_arrow_step
// below); the buttons' hold-repeat is the way to go further.
// NO MODIFIER IS THE CAMERA EITHER: Ctrl+Left / Ctrl+Right BIND NOTHING
// (2026-09-23) — the nudge's camera is the hold posture's (NudgeCamera
// below), so this function is never asked about a horizontal press.
//
// EVERY MODIFIED ARROW SPELLS NOTHING — strict modifier validation, so each
// is the consumed no-op it is everywhere else (chord_is_bound: the four
// arrows bind bare alone).
//
// THE WALLS: a SINGLETON step clamps onto its wall and a GROUP press refuses
// whole where any member could not take the step — the unified wall policy,
// stated at the head of position_nudge.h and instanced at the group tempo
// scan (tempo_cent_step_group_actionable).

// ONE HORIZONTAL ARROW PRESS'S STEP, as the dispatch hands it to the act, the
// landing owners and the faces: a signed COUNT in one of two UNITS, the count
// always ±1, the direction (there is no horizontal ladder; the struct
// keeps a count so the landing arithmetic reads one number in one unit).
// MILLISECONDS is the calibrated step (architect 2026-11-15:
// "the left-right nudge should be recalibrated to one millisecond left or
// right ... we would convert 1 millisecond to the nearest amount in frames,
// and that would be the nudge step"; "using nudge on a coarse zoom is kind
// of pointless, because the point of nudge is fine-tuning") — one
// millisecond of the displayed domain — THE COUNT'S ONE OWNER IS THE
// PARSER'S millisecond_frames (warp_coincidence.h; architect 2026-11-17: the
// same millisecond is the warp coincidence's, so one nudge always clears
// one) — read by the unit's two landings, the marker nudge
// (position_nudge_landing, position_nudge.h) and the playhead's step
// (playhead_arrow_step_landing, app_state.h),
// WHATEVER THE ZOOM, which makes no pixel claim: a press moves its subject
// about a pixel at the working zoom, a fraction of one above it and about
// two at the deepest, painted at its exact place. HOPS is the hop step, one
// hop of the engine's analysis lattice measured in the TARGET domain, which
// makes no pixel claim either. The unit travels beside the count so no
// landing owner ever reads a magic number as the other unit.
struct HorizontalArrowStep {
    enum class Unit : uint8_t { Milliseconds, Hops };
    Unit unit  = Unit::Milliseconds;
    int  count = 0;   // signed: negative is earlier
    static constexpr HorizontalArrowStep milliseconds(int n) {
        return HorizontalArrowStep{Unit::Milliseconds, n};
    }
    static constexpr HorizontalArrowStep hops(int n) {
        return HorizontalArrowStep{Unit::Hops, n};
    }
};

// THE ONE FORK OF THE HORIZONTAL ARROW'S UNIT, keyed on the ACTIVE COLUMN
// (`markers_view`, the column letter) and never on the subject: `direction`
// is -1 for Left and +1 for Right. ONE MILLISECOND on the warp column; on the
// PHASE-RESET column ONE HOP — THE P COLUMN'S ARROW UNIT IS A HOP (architect
// 2026-09-21, the recorded exception between columns, warpmarkers.h's
// naming rules; untouched by the millisecond, which he likens to it — the
// reset's step is "already calibrated"), for
// the focused reset and the playhead with an empty selection alike. The unit
// follows from the engine's lattice, which only P has: the render depends
// only on the window a reset seeds in (S - N/2, the engine seeding at the
// last window at or before it), so a millisecond nudge on P would almost
// always be SILENT — one hop is about 23 of them — re-rendering identical
// audio, and the hop is the only arrow move there that changes what is
// heard. Every reader of the horizontal pair's step asks this: the marker-
// and waveform-lane dispatch, and the Left / Right face
// (horizontal_arrow_step_actionable, app_state.h), so the face asks the step
// the press will take.
constexpr HorizontalArrowStep horizontal_arrow_step(int direction,
                                                    char markers_view) {
    if (markers_view == 'P') return HorizontalArrowStep::hops(direction);
    return HorizontalArrowStep::milliseconds(direction);
}

// THE HORIZONTAL ARROW'S CAMERA IS THE HOLD POSTURE'S (architect 2026-09-23:
// camera behaviour follows from what the user already did; the nudge reads
// no zoom level, 2026-09-22). Two answers, at every zoom, on
// every column and for the playhead alike:
//   * FollowEdge — the posture dark: the camera holds while the subject is
//     on screen, and a step that would carry it off the window scrolls the
//     viewport so it stands at the edge column and walks there — the
//     movement owner's own keep-visible edge-align (Viewport::move_playhead_to
//     / reseat_playhead_to), nothing called beyond it;
//   * HoldColumn — the posture standing (an explicit centring armed it): the
//     subject keeps the screen column it painted in before the step and the
//     waveform slides under it (Viewport::hold_subject_column_after_nudge).
// The camera changes NOTHING ELSE: the step, its unit, its walls, its
// refusals, its cards, its locks and its undo coalescing are the same under
// either answer. THE ONE OWNER OF THE FORK is nudge_camera (app_state.h,
// beside AppState::camera_hold, the bit it reads), asked by the two nudge
// dispatch sites — the marker lane's arm (input_handler.cpp) and the
// waveform lane's step (run_waveform_lane_playhead_step,
// input_key_dispatch.cpp) — and by the undo / redo singleton restore
// (restore_history_entry, undo.cpp; architect 2026-09-25), whose FollowEdge
// answer is the landing owner's Restore rather than the edge. Ctrl+Left /
// Ctrl+Right bind nothing.
enum class NudgeCamera : uint8_t { FollowEdge, HoldColumn };

// -- THE CLIPBOARD READ'S ONE PAYLOAD BOUND (2026-09-03) --------------------
//
// WHAT A PASTE WILL ACCEPT FROM OUTSIDE THIS PROGRAM, IN BYTES. The
// clipboard is the one door another application writes through, so the read
// is the one place a hostile or merely broken producer can hand the GUI's
// single thread an unbounded allocation; the answer is REFUSE THE WHOLE
// PAYLOAD, log one line, and hand back the
// empty string, which every caller already reads as "nothing to paste" (a
// paste of nothing must not delete the selection). A partial paste is worse
// than no paste, so nothing is truncated to the bound.
//
// THE NUMBER IS FAR ABOVE ANY REAL PASTE and is a ceiling, not a policy: the
// editors' own per-Kind byte caps refuse a long paste at
// text_editor::replace_selection long before this, with a card that says so.
// This exists for the pathological case alone.
//
// IT IS THE SEAM'S: platform_android.cpp's clipboard_get_text reads this
// constant (the backend header includes this one), so a retune is one edit
// here.
//
// AND THERE IS A SECOND COPY THE BUILD CANNOT CHECK: MainActivity.java's
// MAX_CLIPBOARD_BYTES refuses the same size before it encodes the clip to
// UTF-8, so the oversized array never crosses JNI at all. THAT NUMBER IS A
// MIRROR OF THIS ONE AND THE TWO ARE EDITED IN ONE ACT — the same rule the
// media command table keeps (gui_media.h), and with the same reason no
// static_assert can hold it: the APK build compiles the Java sliver against
// no C++ header. The native check below it is the real bound; the Java one is
// the early refusal that keeps the bytes out of the process.
inline constexpr size_t kClipboardMaxBytes = 1024u * 1024u;

// -- IS THIS CHORD BOUND IN THIS STATE? (architect 2026-08-30; the one state
// term 2026-09-01) ----------------------------------------------------------
//
// "Bound keys either show an effect or a card, so an unbound key is identified
// by its silence." The gates that answer a refused press therefore ask this
// first, and say nothing for a chord that binds nothing where the press
// landed: the deduction only works if the silence is reserved for it. ITS
// READERS: on_key's read-only lock
// (authoring_lock_refuses_chord), the car's two roads onto the same gates
// (run_undo_redo_without_key, car_play_refused_by_key_gates), and the A/B
// audition's hands-off end at on_key's head (end_audition_for_press: a bound
// chord ends a standing act).
//
// THE LIST IS THE DISPATCH'S INTERNAL KEY (architect 2026-10-30: the roster
// and the on-screen keyboard are the two producers of every key; the ruling
// at platform_android.cpp's key arm). The roster spells only chords its rows
// and their admissions reach, every one of them on this list; the on-screen
// keyboard stands only under an editor, whose own route takes its keys above
// every
// reader — so the unbound-key silence covers what a roster row never spells
// and the keyboard's characters with no editor, which cannot occur. ESCAPE,
// RETURN AND TAB STAY ON IT for the keyboard's sake: its Esc, Enter and Tab
// arrive under an editor (and Return is the Open Text Editor key's chord),
// and they must be what they are, bound keys, wherever a reader asks.
//
// WHAT COUNTS AS BOUND IS WHAT THE MAIN DISPATCH CLAIMS IN THE STANDING MODE —
// every chord on_key's own road would claim in the state the press arrived in,
// whether or not it would ACT there. The four sub-handlers on that road are in
// it (handle_render_dispatch_keys, handle_mode_keys,
// handle_tab_switch_keys and handle_plain_bare_keys' switch), and so is
// handle_history_mode_key: the `h` view ADDS its seven shapes to the dispatch
// rather than replacing it, so bare `g`, bare `u`, the `,` / `.` pair with and
// without shift and bare `v` are bindings like any other WHILE THE VIEW
// STANDS, and nothing at all outside it. (Bare `h` itself is NOT one of
// them: the toggle is claimed in every state, which is why it reads no term.)
//
// THE MODE BIT IS THE ONE STATE TERM THIS PREDICATE CARRIES (architect
// 2026-09-01), and it is THE DISPATCH'S OWN TEST MADE EXECUTABLE rather
// than a second copy of it: those seven shapes' whole binding sits behind two
// lines — handle_history_mode_key's `if (!app.history_mode.active) return
// false;` (input_key_dispatch.cpp), which owns six of them, and the revert
// arm's own `app.history_mode.active &&` (input_handler.cpp), which owns bare
// `v` — so outside the view all seven fall to handle_plain_bare_keys' default
// and to the strict-modifier tail, which already answer them with the unbound
// silence. Reading the bit here is what makes the gates say the same thing.
//
// WHY THE TERM: a state-blind predicate answers a bare `v` pressed on a
// LOCKED TAB OUTSIDE THE VIEW (the architect's own instance) with "V is not
// available on a read-only tab" — a sentence about a key that a WRITABLE tab
// in that same state ignores in silence — and the two drag gates and the
// loading gate ask this one predicate too. The read-only gate's MEMBERSHIP is
// the chord's alone (read_only_key_blocked knows no mode), and drift here is
// a MISSED CARD, never a wrong act.
//
// THE OVERLAY ROUTERS ARE NOT, and that is the line: while an overlay stands
// its router IS the whole vocabulary (its opener's chords and the tooltip
// lamp's bare backslash, 2026-11-10) and the dispatch below it
// never runs. THE EDITORS ARE THE SAME
// KIND OF WORLD and, being modals, ask nothing (2026-10-25): an editor's
// own keys (Backspace, its ctrl-exact clipboard chords, its printable
// insertion) are consumed by the editor itself, and every chord it does not
// own is consumed by its gate in silence.
//
// A NEW BINDING JOINS THIS LIST. That is the standing cost of spelling the
// dispatch's membership twice, taken deliberately: the dispatch is an if-ladder
// across three translation units with its modifier tests written inline, and
// there is no shape to read it off. Drift here is a MISSED CARD (a bound chord
// answered by silence at a gate), never a wrong act — nothing routes through
// this predicate. kToolbarChords
// is the set's one PRODUCER on the main dispatch (the chords the roster's
// buttons synthesize, with their admitted modifiers), and every chord it can
// spell is on this list.
//
// WHAT IS DELIBERATELY ABSENT: the digits 5..9;
// Backspace, and every letter the ladder never tests (A, E, Q, X, Y — Quit
// is Back's); PageUp / PageDown, the keypad's Enter and the shifted Tab's
// own keysym (2026-10-30: no producer spells them — the pans are the
// finger's); every key this switch
// names nowhere; and every modifier decoration no arm spells, which is strict
// modifier validation's whole no-op class.
constexpr bool chord_is_bound(GuiKey key, GuiInputState mods,
                              bool history_view) {
    const bool ctrl = mods.ctrl, alt = mods.alt, shift = mods.shift;
    const bool bare  = !ctrl && !alt && !shift;   // no modifier at all
    const bool sh    = !ctrl && !alt &&  shift;   // Shift alone
    const bool cl    =  ctrl && !alt && !shift;   // Ctrl alone
    const bool cs    =  ctrl && !alt &&  shift;   // Ctrl+Shift
    const bool ca    =  ctrl &&  alt && !shift;   // Ctrl+Alt
    const bool cas   =  ctrl &&  alt &&  shift;   // Ctrl+Alt+Shift
    // THE ALT VOCABULARY, WHOLE (off the `ca` and `cas` arms, the only ones
    // that read alt): ALT IS THE THREE
    // Ctrl+Alt CHORDS, NOTHING ELSE — Ctrl+Alt+R / Ctrl+Alt+Shift+R (the
    // render pair) and Ctrl+Alt+O (Revert). No Alt-alone chord binds (the
    // static_asserts below pin Alt+Tab's family and Alt+F / Alt+E / Alt+S /
    // Alt+D), and alt has no POINTER vocabulary: the pointer carries the
    // ctrl bit alone (architect 2026-10-30). THE Ctrl+Alt SHAPE IS THE INVARIANT rather
    // than the number: alt with Ctrl marks a HEAVIER SIBLING of a plain chord
    // — a write that emits (a render), or the reopen that DISCARDS (Revert is
    // Open's own letter with alt added); Alt alone spells nothing.
    switch (key) {
        // -- letters, bare only, bound in EVERY state: the mode toggles (`i`
        // iteration, `k` add to selection) and `l`, the render player's
        // opener; `c`, the centring, and `f`, the follow lamp beside the two
        // FLATTEN chords, have their own lines below.
        case GuiKeys::I:
        case GuiKeys::K:
        case GuiKeys::L:
            return bare;
        // Bare `c` centres, in every state. NO MODIFIED `c` BINDS in the main
        // window (architect 2026-10-14: Copy Resolved Value is Ctrl+Shift+J,
        // below); the text editors' own Ctrl+C is their
        // router's, ahead of this dispatch.
        case GuiKeys::C: return bare;
        // THE LETTER CARRIES THREE ACTS: bare `f` is the FOLLOW LAMP (the
        // group above's own kind; architect 2026-09-23), Ctrl+F flattens
        // every warp marker's tempo
        // deviations and Ctrl+Shift+F collapses them into one (2026-09-19) —
        // the shifted form being the plain act's twin, `s`'s shape
        // on a ctrl chord rather than a bare one. The ctrl spelling is what
        // guards an authoring verb against a stray bare press, exactly as
        // Ctrl+D and Ctrl+N do.
        case GuiKeys::F: return bare || cl || cs;
        // The BPM opener, Ctrl+B (2026-09-15: a Ctrl chord guards against a
        // stray bare press, as Ctrl+D / Ctrl+N do).
        case GuiKeys::B: return cl;
        // -- THE `h` VIEW'S OWN BARE LETTERS, bound while it stands and unbound
        // outside it (2026-09-01): `g` the walk source, `u` the compare
        // reading — both handle_history_mode_key's, behind its mode return —
        // and `v` the REVERT ACT (2026-09-01), whose own arm in
        // input_handler.cpp carries the same mode test.
        case GuiKeys::G: case GuiKeys::U: case GuiKeys::V:
            return bare && history_view;
        // THE PIGEON'S PAIR, ONE LETTER, ONE MEANING IN EVERY VIEW: CTRL+J
        // goes to where the focused value came from (architect 2026-09-29,
        // Jump to Defining Marker's own chord) and CTRL+SHIFT+J copies that
        // value (architect 2026-10-14, Copy Resolved Value), in the
        // phase-reset view too (architect 2026-10-21: "pigeon stays ctrl+j /
        // +shift"; the propagate is the letter P's, below). Bare `j` and
        // Shift+J bind nothing.
        case GuiKeys::J: return cl || cs;
        // Drop on the live column / drop a phase reset from the warp column / save,
        // and CTRL+SHIFT+S, THE PUBLISH CHORD, in the `h` view alone
        // (architect 2026-11-17, is_publish_key: its one producer is
        // that view's SAVE lift, the commit; outside the view nothing spells
        // it).
        case GuiKeys::S: return bare || sh || cl || (cs && history_view);
        // The read-only toggle, Open project and Revert.
        case GuiKeys::O: return bare || cl || ca;
        // THE PHASE-RESET PROPAGATE, ONE LETTER (architect 2026-10-24): bare
        // `p` COPIES the selected run's phase resets (the COPY key), CTRL+P
        // PASTES them and CTRL+SHIFT+P PASTES THEIR STATE (the PASTE key's
        // tap and hold, 2026-11-08; is_phase_reset_copy_key and its two
        // siblings below). Shift+P and
        // every alt spelling bind nothing.
        case GuiKeys::P: return bare || cl || cs;
        // BARE `m` OPENS THE SCORE VIEWER (architect 2026-10-26, the SCORE
        // key's chord), in both modes: the `h` view's allowlist refuses it
        // there (its key hidden there), and under the viewer its own router
        // closes on it (route_score_viewer_key, ranked above this
        // predicate's every reader). Every modified `m` binds nothing.
        case GuiKeys::M: return bare;
        // BARE `w` TOGGLES THE TRIM ARM LAMP (architect 2026-11-07, the
        // TRIM key's chord; GuiInputHandler::toggle_trim_armed): lit,
        // the next single-contact drag on the waveform is the trim sweep
        // instead of the pan (AppState::trim_armed). In both modes: the `h`
        // view's allowlist refuses it there (its key hidden there). Every
        // modified `w` binds nothing.
        case GuiKeys::W: return bare;
        // BARE `t` OPENS THE TRIM PRESETS LIST (architect 2026-10-28 night,
        // the PRESETS key's chord; GuiInputHandler::open_trim_presets), in both
        // modes: the `h` view's allowlist refuses it there (its key hidden
        // there), and under the standing list
        // the picker's router closes on it (route_picker_key, ranked above
        // this predicate's every reader). SHIFT+T RESETS THE TRIM (architect
        // 2026-11-08, the PRESETS key's hold; is_trim_reset_key
        // below), under the standing list too, the list staying. Every other
        // modified `t` binds nothing.
        case GuiKeys::T: return bare || sh;
        // The history view's toggle — bound in BOTH modes, since it is what
        // opens the view and what closes it (handle_history_mode_key claims it
        // in every state), which is why it reads no mode term while the seven
        // shapes the view ADDS do. The view's revert is bare `v` (2026-09-01;
        // the mode-only letters above), so Ctrl+H binds nothing and takes the
        // unbound silence.
        case GuiKeys::H: return bare;
        // Toggle disabled, each marker's own flag alone, and on Ctrl+Shift
        // (architect 2026-10-29) the carrying form, a label definition
        // toggled with its references — the Disable button's
        // long press (redesign_button_shift_admits). Plain `d`, Shift+D and
        // every alt spelling bind nothing.
        case GuiKeys::D: return cl || cs;
        // Toggle inherit, and THE TIE (2026-09-19): Ctrl+Shift+N makes
        // one axis of the grid iteration sweep out of the selected markers,
        // or unties them again. The shifted form is the plain act's twin on a
        // ctrl chord, `F`'s shape just above — and the letter is the inherit
        // toggle's, so the tie rides that button's long press
        // rather than asking for a button of its own. Plain `n` and Shift+N
        // stay unbound.
        case GuiKeys::N: return cl || cs;
        // Undo, and redo on the one meaningful shift bit. Bare `z` and
        // Shift+Z are unbound.
        case GuiKeys::Z: return cl || cs;
        // THE LETTER CARRIES THREE ACTS: bare `r` is the RESTRICT LAMP in the
        // main window (architect 2026-10-29, GuiPlaybackLifecycle::
        // toggle_restrict; the `h` view's allowlist refuses it there, its
        // key hidden there), and Ctrl+Alt+R / Ctrl+Alt+Shift+R are the render
        // pair, the dispatch and the archival one. Shift+R and Ctrl+R bind
        // nothing.
        case GuiKeys::R: return bare || ca || cas;

        // Zoom to Fit, and on its shifted form FRAME THE TRIM (architect
        // 2026-10-24). Ctrl+0 and Ctrl+Shift+0 bind nothing: Reset Trim is
        // Shift+T, the PRESETS key's hold (architect 2026-11-08).
        case GuiKeys::Digit0: return bare || sh;
        // The four selectors, one axis each: bare 1 is Source, bare 2
        // Target, bare 3 Warp, bare 4 Phase Reset (architect 2026-10-14, the
        // top icon row's two radio pairs; the key for the value standing
        // selects its axis's other, 2026-10-23, on_key's arm), and 5..9
        // bind nothing.
        case GuiKeys::Digit1: case GuiKeys::Digit2:
        case GuiKeys::Digit3: case GuiKeys::Digit4:
            return bare;

        // THE SETTINGS PAGE (architect 2026-10-28: "plain press only"; its
        // shifted twin, Shift+;, is unbound and silent); the load in place /
        // render player, bare alone.
        case GuiKeys::Semicolon: return bare;
        case GuiKeys::Apostrophe: return bare;
        // Toggle Waveform Magnification (architect 2026-09-22), on the
        // bare BACKTICK (architect 2026-09-23: the bracket sits on the
        // far side of the keyboard and belongs to trim by feel), bound in
        // both modes as `f` is: it toggles in both audio views, the
        // `h` view's allowlist admitting it (architect 2026-10-12: he
        // wants to magnify while walking history). Every modified backtick is
        // unbound.
        case GuiKeys::Grave: return bare;
        // THE ZOOM STEP (architect 2026-10-27, Zoom In and Zoom Out):
        // `=` deeper and `-` shallower, one level, BARE ONLY (architect
        // 2026-11-08; is_zoom_step_key below, the dispatch's and both
        // allowlists' one predicate). No modifier spells anything on either key.
        case GuiKeys::Equal: case GuiKeys::Minus: return bare;
        // Toggle Value Drag (architect 2026-10-11), the bare LEFT BRACKET,
        // bound in both modes as `k` is: the one-shot lamp arming the flag's
        // value drag (AppState::value_drag_lamp), the `h` view's allowlist
        // refusing it (its key hidden there), and under grid iterations its own arm carding
        // that the lamp is always on there (kValueDragIterationsCard). Every modified `[`, and the whole of `]`,
        // are unbound.
        case GuiKeys::BracketLeft: return bare;
        // Toggle Tooltips (architect 2026-09-29), the bare BACKSLASH, bound in
        // both modes: the lamp is chrome and the `h` view's allowlist admits
        // it. Every modified backslash is unbound.
        case GuiKeys::Backslash: return bare;
        // The `h` walk: bare steps, shift jumps to its ends — the mode's own
        // arm again (handle_history_mode_key, behind its mode return), so both
        // spellings are bound while the view stands and unbound outside it
        // (2026-09-01).
        case GuiKeys::Comma: case GuiKeys::Period:
            return (bare || sh) && history_view;

        // Play from the playhead, and the A/B audition.
        case GuiKeys::Space: return bare || sh;
        // BARE ONLY, AND THE EDITORS' ALONE (architect 2026-10-30): the
        // on-screen keyboard's Esc, its one producer, arrives only under an
        // editor, whose route cancels the edit with it, so no dispatch arm
        // outside the editors answers Esc — and it stays a bound key, the
        // keyboard's own key being what it is wherever a reader asks (the
        // head's paragraph). NO MODIFIED ESCAPE BINDS ANYWHERE.
        case GuiKeys::Escape: return bare;
        // BARE RETURN opens the addressed cell's editor on the focused marker
        // (the Open Text Editor key's), and is the editors' commit under the
        // on-screen keyboard's Enter. SHIFT+RETURN IS THE LABEL PASTE
        // (architect 2026-11-10, that key's hold: the clipboard's text onto
        // every selected marker's line, is_label_paste_key). EVERY OTHER
        // MODIFIED RETURN BINDS NOTHING (the phase-reset propagate is the
        // letter P's).
        case GuiKeys::Return: return bare || sh;
        // DELETE the selected markers, and SHIFT+DELETE (architect
        // 2026-10-29) them with every reference to every selected label
        // definition — the Delete button's long press. The
        // ctrl and alt forms bind nothing.
        case GuiKeys::Delete: return bare || sh;
        // The marker walk (bare forward — NEXT — and shift back — PREV; its
        // landing camera is the landing owner's, Viewport::land_subject), the
        // A/B tab switch and the paired march (SWITCH and its hold). No Alt
        // spelling of Tab binds.
        case GuiKeys::Tab: return bare || sh || cl || cs;
        // The value step on the addressed cell (the tempo or a bound), ONE
        // UNIT, BARE ONLY (architect 2026-11-08; the head of THE ARROW STEP
        // above). Every
        // modified Up / Down spells nothing.
        case GuiKeys::Up: case GuiKeys::Down:
            return bare;
        // The playhead / marker position step, BARE ONLY on every column
        // (architect 2026-09-21: no horizontal ladder — placement
        // is graphical; the step's unit is the active column's,
        // horizontal_arrow_step above). Shift spells nothing, and CTRL SPELLS
        // NOTHING (2026-09-23): the camera is the hold posture's
        // (NudgeCamera above).
        case GuiKeys::Left: case GuiKeys::Right:
            return bare;
        // The trim bounds bare, and the WHOLE-PIECE ends under SHIFT
        // (architect 2026-10-24, the Go to Start / Go to End keys: the glass
        // long press is Shift). Ctrl and Ctrl+Shift spell nothing.
        case GuiKeys::Home: case GuiKeys::End: return bare || sh;

        default: return false;
    }
}

// ANCHORS, not a second copy of the inventory: each pins one rule the list
// above must keep, so a careless widening trips at compile time. The chords
// that read no mode term are pinned OUTSIDE the view (the ordinary state), and
// the three anchors that follow them pin the term itself, both ways.
// AN ANCHOR PINS ONLY WHAT A PRODUCER CAN SPELL (architect 2026-10-30, the
// roster and the on-screen keyboard the two producers of every key): an
// absence no producer can type guards nothing, so none is pinned.
static_assert(chord_is_bound(GuiKeys::Digit1, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::Digit4, GuiInputState{}, false),
              "bare 1 is the Source selector and bare 4 the Phase Reset one");
static_assert(chord_is_bound(GuiKeys::Escape, GuiInputState{}, false),
              "bare Esc is bound: the on-screen keyboard's Esc, the editors' "
              "cancel, is a bound key wherever a reader asks");
static_assert(!chord_is_bound(GuiKeys::Escape,
                              GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::Escape,
                                  GuiInputState{true, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Escape,
                                  GuiInputState{false, true, false}, false),
              "Esc is bare-exact: no modified Escape binds anywhere, Ctrl+Esc "
              "included since it retired on 2026-09-01");
static_assert(chord_is_bound(GuiKeys::C, GuiInputState{}, false) &&
                  !chord_is_bound(GuiKeys::C,
                                  GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::C,
                                  GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::C,
                                  GuiInputState{true, true, false}, false) &&
                  chord_is_bound(GuiKeys::F, GuiInputState{}, false),
              "bare `c` centres; Ctrl+C, Shift+C and Ctrl+Shift+C bind nothing "
              "in the main window (the copy is Ctrl+Shift+J since 2026-10-14); "
              "bare `f` is the follow lamp");
static_assert(chord_is_bound(GuiKeys::Home, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::End, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::Home,
                                 GuiInputState{false, true, false}, false) &&
                  chord_is_bound(GuiKeys::End,
                                 GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Home,
                                  GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::End,
                                  GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::End,
                                  GuiInputState{true, true, false}, false),
              "Home / End bind bare (the trim-bound jump) and under Shift (the "
              "whole-piece jump, 2026-10-24); Ctrl and Ctrl+Shift forms bind "
              "nothing");
static_assert(chord_is_bound(GuiKeys::Space, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::Space,
                                 GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Space,
                                  GuiInputState{true, false, false}, false),
              "Space binds bare and shifted only — strict modifier validation");
static_assert(chord_is_bound(GuiKeys::Grave, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::Grave, GuiInputState{}, true) &&
                  !chord_is_bound(GuiKeys::Grave,
                                  GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Grave,
                                  GuiInputState{true, false, false}, false) &&
                  chord_is_bound(GuiKeys::BracketLeft, GuiInputState{},
                                 false) &&
                  chord_is_bound(GuiKeys::BracketLeft, GuiInputState{},
                                 true) &&
                  !chord_is_bound(GuiKeys::BracketLeft,
                                  GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::BracketLeft,
                                  GuiInputState{true, false, false}, false),
              "the backtick is the magnification lamp, bare in both modes and "
              "no decoration of it; bare `[` is the value drag lamp "
              "(2026-10-11), and no decoration of it binds");
static_assert(chord_is_bound(GuiKeys::Backslash, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::Backslash, GuiInputState{}, true) &&
                  !chord_is_bound(GuiKeys::Backslash,
                                  GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Backslash,
                                  GuiInputState{true, false, false}, false),
              "the bare backslash is the tooltip lamp in both modes, and no "
              "decoration of it binds");
static_assert(!chord_is_bound(GuiKeys::J, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::J,
                                 GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::J,
                                  GuiInputState{false, true, false}, false) &&
                  chord_is_bound(GuiKeys::J,
                                 GuiInputState{true, true, false}, false) &&
                  !chord_is_bound(GuiKeys::J,
                                  GuiInputState{true, true, true}, false),
              "Ctrl+J jumps to the focused value's defining marker and "
              "Ctrl+Shift+J copies its resolved value; bare `j`, Shift+J and "
              "Ctrl+Alt+Shift+J bind nothing");
static_assert(chord_is_bound(GuiKeys::Digit0, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::Digit0,
                                 GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Digit0,
                                  GuiInputState{true, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Digit0,
                                  GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::Digit0,
                                  GuiInputState{true, true, true}, false),
              "bare `0` is Zoom to Fit and Shift+0 frames the trim; "
              "Ctrl+Shift+0, the reset until 2026-11-08, and every other "
              "decoration of `0` bind nothing");
static_assert(chord_is_bound(GuiKeys::Equal, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::Minus, GuiInputState{}, false) &&
                  !chord_is_bound(GuiKeys::Equal,
                                  GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Minus,
                                  GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Equal,
                                  GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::Minus,
                                  GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::Equal,
                                  GuiInputState{true, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Minus,
                                  GuiInputState{false, false, true}, false),
              "bare `=` / `-` step the zoom one level in / out; Shift+= / "
              "Shift+-, the two-level step until 2026-11-08, and every ctrl "
              "or alt form bind nothing");
// `l` binds bare only, so its anchor witnesses the "only" whole: the one
// positive, then EVERY non-bare combination of the three modifiers as a
// negative. A widening onto any of them trips here.
static_assert(chord_is_bound(GuiKeys::L, GuiInputState{}, false) &&
                  !chord_is_bound(GuiKeys::L,
                                  GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::L,
                                  GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::L,
                                  GuiInputState{false, false, true}, false) &&
                  !chord_is_bound(GuiKeys::L,
                                  GuiInputState{true, true, false}, false) &&
                  !chord_is_bound(GuiKeys::L,
                                  GuiInputState{true, false, true}, false) &&
                  !chord_is_bound(GuiKeys::L,
                                  GuiInputState{false, true, true}, false) &&
                  !chord_is_bound(GuiKeys::L,
                                  GuiInputState{true, true, true}, false),
              "`l` binds bare only — the render player — and none of the "
              "seven modifier combinations spells anything");
// `m` binds bare only — the score viewer (2026-10-26) — in both modes, and
// no modifier spells anything on it.
static_assert(chord_is_bound(GuiKeys::M, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::M, GuiInputState{}, true) &&
                  !chord_is_bound(GuiKeys::M,
                                  GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::M,
                                  GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::M,
                                  GuiInputState{false, false, true}, false) &&
                  !chord_is_bound(GuiKeys::M,
                                  GuiInputState{true, true, false}, false) &&
                  !chord_is_bound(GuiKeys::M,
                                  GuiInputState{true, false, true}, false) &&
                  !chord_is_bound(GuiKeys::M,
                                  GuiInputState{false, true, true}, false) &&
                  !chord_is_bound(GuiKeys::M,
                                  GuiInputState{true, true, true}, false),
              "`m` binds bare only — the score viewer — and none of the "
              "seven modifier combinations spells anything");
// `w` binds bare only — the TRIM arm lamp (2026-11-07) — in both modes, and
// no modifier spells anything on it.
static_assert(chord_is_bound(GuiKeys::W, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::W, GuiInputState{}, true) &&
                  !chord_is_bound(GuiKeys::W,
                                  GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::W,
                                  GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::W,
                                  GuiInputState{false, false, true}, false) &&
                  !chord_is_bound(GuiKeys::W,
                                  GuiInputState{true, true, false}, false) &&
                  !chord_is_bound(GuiKeys::W,
                                  GuiInputState{true, false, true}, false) &&
                  !chord_is_bound(GuiKeys::W,
                                  GuiInputState{false, true, true}, false) &&
                  !chord_is_bound(GuiKeys::W,
                                  GuiInputState{true, true, true}, false),
              "`w` binds bare only — the TRIM arm lamp — and none of the "
              "seven modifier combinations spells anything");
// `r` binds bare — the RESTRICT lamp (2026-10-29) — in both modes, and the
// render pair's two Ctrl+Alt spellings; no other modifier spells anything.
static_assert(chord_is_bound(GuiKeys::R, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::R, GuiInputState{}, true) &&
                  chord_is_bound(GuiKeys::R,
                                 GuiInputState{true, false, true}, false) &&
                  chord_is_bound(GuiKeys::R,
                                 GuiInputState{true, true, true}, false) &&
                  !chord_is_bound(GuiKeys::R,
                                  GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::R,
                                  GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::R,
                                  GuiInputState{false, false, true}, false) &&
                  !chord_is_bound(GuiKeys::R,
                                  GuiInputState{true, true, false}, false) &&
                  !chord_is_bound(GuiKeys::R,
                                  GuiInputState{false, true, true}, false),
              "`r` binds bare — the RESTRICT lamp — and the render pair "
              "alone");
// `t` binds bare — the Trim Presets list (2026-10-28 night) — and shifted —
// Reset Trim, the PRESETS key's hold (2026-11-08) — in both modes, and no
// other modifier spells anything on it.
static_assert(chord_is_bound(GuiKeys::T, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::T, GuiInputState{}, true) &&
                  chord_is_bound(GuiKeys::T,
                                 GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::T,
                                  GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::T,
                                  GuiInputState{false, false, true}, false) &&
                  !chord_is_bound(GuiKeys::T,
                                  GuiInputState{true, true, false}, false) &&
                  !chord_is_bound(GuiKeys::T,
                                  GuiInputState{true, false, true}, false) &&
                  !chord_is_bound(GuiKeys::T,
                                  GuiInputState{false, true, true}, false) &&
                  !chord_is_bound(GuiKeys::T,
                                  GuiInputState{true, true, true}, false),
              "`t` binds bare — the Trim Presets list — and Shift+T, Reset "
              "Trim; none of the six other modifier combinations spells "
              "anything");
// `o` carries three neighbours — bare the read-only toggle, Ctrl the picker,
// Ctrl+Alt Revert — and no shifted spelling of any of them. (Open
// Project's icon-row button reaches Revert by a shift press, the button's
// lift turning it into this alt — the icon road's twin, never a keyboard
// spelling; architect 2026-10-07, at finish_chrome_press_release.)
static_assert(chord_is_bound(GuiKeys::O,
                             GuiInputState{true, false, true}, false) &&
                  chord_is_bound(GuiKeys::O,
                                 GuiInputState{true, false, true}, true) &&
                  !chord_is_bound(GuiKeys::O,
                                  GuiInputState{true, true, true}, false) &&
                  !chord_is_bound(GuiKeys::O,
                                  GuiInputState{false, false, true}, false),
              "Revert is Ctrl+Alt+O exactly, bound in both modes; "
              "Ctrl+Alt+Shift+O and Alt+O spell nothing");
static_assert(!chord_is_bound(GuiKeys::Tab,
                              GuiInputState{false, false, true}, false) &&
                  !chord_is_bound(GuiKeys::Tab,
                                  GuiInputState{false, false, true}, true) &&
                  !chord_is_bound(GuiKeys::Tab,
                                  GuiInputState{false, true, true}, false) &&
                  !chord_is_bound(GuiKeys::Tab,
                                  GuiInputState{false, true, true}, true) &&
                  !chord_is_bound(GuiKeys::Tab,
                                  GuiInputState{true, false, true}, false) &&
                  !chord_is_bound(GuiKeys::Tab,
                                  GuiInputState{true, true, true}, true),
              "no Alt spelling of Tab binds in either mode, with or without "
              "Shift or Ctrl: each is an unbound no-op");
// NO ALT-ALONE LETTER BINDS (2026-10-21).
static_assert(!chord_is_bound(GuiKeys::F,
                              GuiInputState{false, false, true}, false) &&
                  !chord_is_bound(GuiKeys::E,
                                  GuiInputState{false, false, true}, false) &&
                  !chord_is_bound(GuiKeys::S,
                                  GuiInputState{false, false, true}, false) &&
                  !chord_is_bound(GuiKeys::E, GuiInputState{}, false) &&
                  !chord_is_bound(GuiKeys::D,
                                  GuiInputState{false, false, true}, false),
              "no Alt-alone letter binds");
// THE LETTER P IS THE PROPAGATE (architect 2026-10-24): bare the copy (COPY),
// Ctrl the paste and Ctrl+Shift the state paste (PASTE's tap and hold,
// 2026-11-08), in both modes (the `h` view's
// allowlist then refuses all three, the key hidden there); Shift+P and every alt
// spelling bind nothing — the anchor walks all eight combinations.
consteval bool letter_p_binds_the_propagate() {
    for (int bits = 0; bits < 8; ++bits) {
        const GuiInputState mods{(bits & 1) != 0, (bits & 2) != 0,
                                 (bits & 4) != 0};
        const bool want = !mods.alt && (!mods.shift || mods.ctrl);
        if (chord_is_bound(GuiKeys::P, mods, false) != want ||
            chord_is_bound(GuiKeys::P, mods, true) != want)
            return false;
    }
    return true;
}
static_assert(letter_p_binds_the_propagate(),
              "the letter P binds bare (copy phase resets), Ctrl (paste them) "
              "and Ctrl+Shift (paste their state); Shift+P and the alt "
              "spellings bind nothing");
// RETURN BINDS BARE AND SHIFT (2026-11-10): the editor's open and the label
// paste, the Open Text Editor key's press and hold; its ctrl forms bind
// nothing (the propagate is the letter P's, 2026-10-24).
static_assert(chord_is_bound(GuiKeys::Return, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::Return,
                                 GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Return,
                                  GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::Return,
                                  GuiInputState{true, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Return,
                                  GuiInputState{true, false, true}, false) &&
                  !chord_is_bound(GuiKeys::Return,
                                  GuiInputState{false, false, true}, false),
              "Enter binds bare (open the editor) and Shift (paste the "
              "label); Ctrl+Enter, Ctrl+Shift+Enter and the alt spellings "
              "bind nothing");
// THE PUBLISH CHORD binds in the `h` view alone (architect 2026-11-17); its
// Alt spelling nowhere.
static_assert(!chord_is_bound(GuiKeys::S,
                              GuiInputState{true, true, false}, false) &&
                  chord_is_bound(GuiKeys::S,
                                 GuiInputState{true, true, false}, true) &&
                  !chord_is_bound(GuiKeys::S,
                                  GuiInputState{true, true, true}, true),
              "Ctrl+Shift+S, the publish chord, binds in the history view "
              "alone; Ctrl+Alt+Shift+S stays unbound");
static_assert(chord_is_bound(GuiKeys::Up, GuiInputState{}, false) &&
                  !chord_is_bound(GuiKeys::Up,
                                  GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::Down,
                                  GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::Up,
                                  GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Up,
                                  GuiInputState{true, true, false}, false),
              "a vertical arrow binds bare alone — one unit — and Ctrl+Up / "
              "Ctrl+Down (the three-step until 2026-11-08), Shift and "
              "Ctrl+Shift spell nothing");
static_assert(chord_is_bound(GuiKeys::Left, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::Right, GuiInputState{}, false) &&
                  !chord_is_bound(GuiKeys::Left,
                                  GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::Right,
                                  GuiInputState{true, false, false}, true) &&
                  !chord_is_bound(GuiKeys::Left,
                                  GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Right,
                                  GuiInputState{true, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Right,
                                  GuiInputState{false, true, false}, true),
              "Left / Right bind bare only on every column — the horizontal "
              "ladder is retired, so Shift and Ctrl+Shift spell nothing, and "
              "Ctrl+Left / Ctrl+Right are unbound since 2026-09-23: the "
              "nudge's camera is the hold posture's (AppState::camera_hold), "
              "not a modifier's");
// THE MODE TERM, pinned in both directions (2026-09-01). Bare `v` is the
// architect's own instance — the revert act, which binds nothing outside the
// view, so the gates below it must say nothing there.
static_assert(!chord_is_bound(GuiKeys::V, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::V, GuiInputState{}, true),
              "bare `v` is the `h` view's revert act and binds nothing outside "
              "it: unbound with the view down, bound with it up");
static_assert(!chord_is_bound(GuiKeys::Comma, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::Comma, GuiInputState{}, true) &&
                  !chord_is_bound(GuiKeys::Comma,
                                  GuiInputState{false, true, false}, false) &&
                  chord_is_bound(GuiKeys::Comma,
                                 GuiInputState{false, true, false}, true),
              "the walk's `,` carries the mode term in BOTH its spellings — "
              "the bare step and the shifted jump alike");
static_assert(chord_is_bound(GuiKeys::H, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::H, GuiInputState{}, true),
              "bare `h` is the toggle and is bound in both modes: it is what "
              "opens the view and what closes it, so it reads no mode term");

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
// SHIFT+Space exactly — no ctrl, no alt. This is the shifted Space's one
// binding, and every other decoration on Space stays unbound. The same two
// readers as the bare form's predicate — on_key's dispatch arm and the
// read-only allowlist — and the same one-owner reason. The act itself is
// GuiAbAudition::start
// (ab_audition.h); the play button's long press dispatches this
// chord through on_key (redesign_button_shift_admits).
inline bool is_ab_audition_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::Space && !mods.ctrl && mods.shift && !mods.alt;
}

// True for the chord that opens the OPEN PROJECT prompt (architect
// 2026-08-28): CTRL+O exactly — no shift, no alt. The convention for Open is
// too strong to spell it anywhere else. IT IS NOT bare `o`, which stays the
// active tab's read-only toggle: the
// two are NEIGHBOURS ON ONE LETTER, NOT TWO HALVES OF ONE ACT, so neither
// reader may fold them together. The same two readers as the Space pair above
// — on_key's dispatch arm (input_handler.cpp) and the read-only allowlist
// (read_only_key_blocked, input_key_dispatch.cpp, which admits the chord
// because the picker authors nothing) — and the same one-owner reason. The act
// is GuiInputHandler::open_project_picker, whose own body carries the gates
// (the modal refusals, the `h` view, the loading state); the top icon row's
// Open Project button dispatches this very chord through on_key (2026-10-07).
inline bool is_open_project_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::O && mods.ctrl && !mods.shift && !mods.alt;
}

// True for the chord that runs REVERT (architect 2026-09-13): CTRL+ALT+O
// exactly — no shift. The act reopens the CURRENT project from disk, discarding
// unsaved changes and the undo history, so it sits on Open's letter with alt
// added: the same act on the session as a whole, aimed at the project already
// open. It is a third neighbour on the letter and folds into neither: bare `o`
// is the read-only toggle and Ctrl+O the picker. The same three readers as
// Open's predicate and for the same reasons: on_key's dispatch arm
// (input_handler.cpp), the read-only allowlist (read_only_key_blocked, which
// admits it: the act writes nothing and discards only what was never saved)
// and the `h` view's allowlist (history_mode_key_blocked, which admits it
// beside Ctrl+O). The act is GuiInputHandler::revert_project, whose body
// carries its own refusals. Not repeat-eligible, so a held chord reverts
// once. ITS ONE ROAD IS THE TOP ICON ROW'S OPEN PROJECT BUTTON'S shifted
// press (architect 2026-10-07): the button's lift turns its shift into this
// chord's alt (finish_chrome_press_release), so the spelling here stays
// shift-free — the icon's twin is the icon road's, and Ctrl+Shift+O binds
// nothing.
inline bool is_revert_project_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::O && mods.ctrl && !mods.shift && mods.alt;
}

// True for the chord that DROPS A PHASE RESET FROM THE WARP COLUMN'S EITHER
// VIEW (architect 2026-08-28; the P column refuses it whole, 2026-08-30 —
// the paragraph below): SHIFT+S exactly — no ctrl,
// no alt. It switches the column to P IN THE AUDIO VIEW IT IS PRESSED IN
// (architect 2026-10-12: S+W lands in S+P, T+W in T+P) and drops the
// lead-in reset kN/2 output samples before the
// playhead there, so the reset the architect wants while he is looking at
// the warp column costs one key instead of a view trip and back. OVER A
// MULTI-SELECTION in the warp column it drops one at each selected marker's
// place instead (architect 2026-11-10, the group drop;
// GuiInputHandler::drop_phase_reset_crossing). Ctrl+S is
// the save on this letter and Ctrl+Shift+S the `h` view's publish
// (is_publish_key).
//
// IT IS BARE `s`'s ACT WITH THE COLUMN SWITCH IN FRONT, not a second one:
// from the W column the difference is exactly the column writer Shift+S
// runs first, and then the same one drop body (the act is
// GuiInputHandler::drop_phase_reset_crossing, input_handler.cpp), which drops
// wherever the lead-in lands as bare `s` does there — no place is taken on
// the phase-reset column (architect 2026-11-17). IN
// THE P COLUMN THE CHORD REFUSES WHOLE (architect 2026-08-30): the
// command IS the crossing from warp view, so with phase resets already
// showing — S+P or T+P — there is nothing to cross and the act answers
// "Already in phase reset view" before any switch, bare `s` untouched
// (the leading refusal is phase_reset_drop_crossing_actionable,
// app_state.h).
// The same two readers as the Space pair above — on_key's dispatch arm
// (input_handler.cpp) and the read-only allowlist (read_only_key_blocked,
// input_key_dispatch.cpp, which DROPS the chord: the drop is authored
// content, exactly as bare `s` is) — and the same one-owner reason.
inline bool is_phase_reset_drop_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::S && !mods.ctrl && mods.shift && !mods.alt;
}

// True for THE PUBLISH CHORD (architect 2026-10-10 ~18:35): CTRL+SHIFT+S
// exactly — no alt. IT BINDS IN THE `h` VIEW ALONE (architect 2026-11-17:
// the commit is that view's act alone), where it runs
// GuiInputHandler::run_publish_chord: the pull while GitHub is behind, the
// check while offline, else save and commit under the default title with no
// question asked. ITS ONE PRODUCER is the `h` view's Save button, which
// spells it at its lift (finish_chrome_press_release); outside the view
// nothing spells it and it is unbound (chord_is_bound). Ctrl+S beside it is
// the plain save in both views. THREE READERS, one shape: on_key's `s` arm
// (input_handler.cpp, behind the view's mode test), the read-only allowlist
// (read_only_key_blocked, which admits it as it admits the save — a
// checkpoint of the state the tab already holds authors nothing) and the `h`
// view's allowlist (history_mode_key_blocked, which admits it as the view's
// act).
inline bool is_publish_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::S && mods.ctrl && mods.shift && !mods.alt;
}

// True for the chord that toggles THE WAVEFORM MAGNIFICATION LAMP
// (architect 2026-09-22): the BARE BACKTICK exactly — no ctrl, no shift, no
// alt (the backtick, architect 2026-09-23: the bracket sits on the far side
// of the keyboard and belongs to trim by feel); `[` and `]` are unbound. The
// backtick arrives as its
// level-0 keysym (GuiKeys::Grave), so a shifted press is the backtick plus
// the shift bit and this predicate refuses it. A display posture that
// authors nothing. One-shot
// (repeat-ineligible: a held toggle
// would flicker). The lamp governs both audio views (the arm is Grave's case
// in handle_plain_bare_keys, input_key_dispatch.cpp, which the bare road
// alone reaches). TWO READERS: the read-only allowlist (read_only_key_blocked,
// which ADMITS it — a view posture — and which the
// grid-iterations lock's gate falls through to) and (architect 2026-10-12:
// he wants to magnify while walking history) the `h` view's
// allowlist (history_mode_key_blocked), which admits it beside the tooltip
// lamp; Follow, its group's other lamp, stays refused there, its key hidden
// like every chord's the view does not name.
inline bool is_waveform_magnification_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::Grave && !mods.ctrl && !mods.shift && !mods.alt;
}

// True for the chord that RESETS THE TRIM to the whole song
// (GuiInputHandler::handle_trim_maximize): SHIFT+T exactly — shift, no ctrl,
// no alt (architect 2026-11-08: the PRESETS key's hold, its own
// letter under the hold's shift). It
// arrives as the `t` key plus the shift bit: the backend produces keys only
// through synthesize_key, which carries the GuiKey and the modifier bits
// apart. One-shot. THREE READERS: on_key's dispatch arm (input_handler.cpp),
// the trim presets list's router (route_picker_key, input_key_dispatch.cpp —
// the hold under the standing list, the list staying) and the read-only
// allowlist (read_only_key_blocked, which ADMITS it — trim is not marker
// content). The `h` view's allowlist names it no more than bare `t`
// (history_mode_allowlist_refuses), so the view refuses it, the PRESETS key
// hidden there.
inline bool is_trim_reset_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::T && !mods.ctrl && mods.shift && !mods.alt;
}

// True for the chord that FRAMES THE TRIM (GuiInputHandler::
// run_span_framing_command, the trim bar's double tap exactly — a proper
// trim sub-window with at most the edge margin each side, else the whole song):
// SHIFT+0 exactly — shift, no ctrl, no alt (architect 2026-10-24). Bare `0`
// is Zoom to Fit's toggle and untouched;
// `0` is the whole song to the camera, shifted the trim's span to the camera.
// A CAMERA ACT: it writes no store and no trim, so the read-only allowlist
// (read_only_key_blocked) and the `h` view's (history_mode_key_blocked)
// both ADMIT it, as both states admit the trim bar's double tap (its glass
// twin, TrimBarPressSeed / DoubleClickSurface), and the
// iteration lock
// inherits the read-only admission. One-shot. THREE READERS: on_key's
// dispatch arm (input_handler.cpp) and the two allowlists; the Zoom to Fit
// button's long press reaches the first through on_key
// (redesign_button_shift_admits).
inline bool is_trim_frame_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::Digit0 && mods.shift && !mods.ctrl && !mods.alt;
}

// True for the chords that STEP THE ZOOM (architect 2026-10-27: Zoom In and
// Zoom Out "basically as they used to be"): `=` zooms in and `-`
// out, ONE LEVEL, BARE EXACTLY — no ctrl, no shift, no alt (architect
// 2026-11-08: the two keys carry no hold). A CAMERA ACT about the viewport's
// centre (Viewport::zoom_step), writing no store, no trim and no playhead, so the
// read-only allowlist (read_only_key_blocked, which the grid-iterations lock
// falls through to) and the `h` view's (history_mode_key_blocked) both ADMIT
// it, as the view admits the barrel-held drag zoom. ONE-SHOT
// (repeat_eligible names neither key). THREE READERS: on_key's dispatch arm
// (input_handler.cpp) and the two allowlists; the Zoom In / Zoom Out keys
// reach the first through on_key.
inline bool is_zoom_step_key(GuiKey key, GuiInputState mods) {
    return (key == GuiKeys::Equal || key == GuiKeys::Minus) && !mods.ctrl &&
           !mods.shift && !mods.alt;
}
// THE STEP'S SIZE, in zoom levels: the map is spp(level) = column ×
// 2^(level − 2) (kWorkingZoomLevel's block, app_state.h), so one level is ×2
// — "the steps were doubles".
inline constexpr double kZoomStepLevels = 1.0;
// THE SIGNED STEP a chord is_zoom_step_key admits asks for: `=` deeper
// (negative, a smaller level being more zoomed in), `-` shallower. Read by
// the dispatch arm alone; the two keys' faces ask the size by name
// (redesign_button_enabled's arms).
inline constexpr double zoom_step_levels(GuiKey key) {
    return key == GuiKeys::Equal ? -kZoomStepLevels : kZoomStepLevels;
}

// THE VALUE PAIR (architect 2026-08-29), ONE LETTER (2026-10-14): CTRL+J
// JUMPS to the marker the focused value came from — the pass's owner or the
// ref's definition — on the OTHER A/B tab, so the two tabs stand on the
// reference and its definition at once (Jump to Defining Marker, the bottom
// icon row's verb group), and CTRL+SHIFT+J COPIES the focused marker's
// resolved value to the system clipboard (architect 2026-10-14, Copy
// Resolved Value as the Jump button's shift twin — "it's a marker action and
// the marker row is the bottom row"). Ctrl+C binds nothing in the main
// window — the text editors' own Ctrl+C is their router's, which runs ahead
// of this dispatch while it stands.
//
// BOTH ARE EXACT — the jump ctrl-exact, the copy ctrl+shift-exact — so the
// alt decorations stay the strict rule's consumed no-ops, and both are ONE-SHOT
// (repeat-ineligible: a copy repeats onto itself and a jump has one
// destination). Their subject is the SELECTION'S FOCUS, and an ineligible
// focus — an owner, a phase reset, a disabled marker, the `P` column, nothing
// focused — is a consumed no-op at the act that says so on a card
// (2026-08-30), each chord in its own words (payload_eligibility, app_state.h;
// a coincident member of any kind is eligible and reads 1.00, architect
// 2026-11-17).
// Both are READ-ONLY-LEGAL (they author nothing: read_only_key_blocked admits
// them, and the grid-iterations lock inherits the admission) and legal in both
// audio views, being navigation rather than positional authoring; the `h`
// view refuses them at its own allowlist like every chord it does not name.
// The same two readers as the pairs above — on_key's dispatch arms
// (input_handler.cpp) and the read-only allowlist — and the same one-owner
// reason.
inline bool is_copy_value_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::J && mods.ctrl && mods.shift && !mods.alt;
}
inline bool is_jump_to_value_source_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::J && mods.ctrl && !mods.shift && !mods.alt;
}

// THE PHASE-RESET PROPAGATE, THE LETTER P (architect 2026-10-24, the Copy
// Phase Resets key): BARE P COPIES the phase resets of the selected run of
// warp markers into the session clipboard, CTRL+P PASTES them onto the one
// selected warp marker behind the paste's confirmation, and CTRL+SHIFT+P
// PASTES THEIR STATE — the clipboard's enabled / disabled bits onto the
// matching destination resets in lockstep, positions untouched, no prompt
// (PhaseResetPropagate::paste_state_apply). THE COPY / PASTE DUO carries the
// three (architect 2026-11-08): COPY is bare P; PASTE's row is Ctrl+P, its
// hold Ctrl+Shift+P (redesign_button_shift_admits, app_state.h).
//
// EXACT — bare-exact, ctrl-exact and ctrl+shift-exact, no alt — and ONE-SHOT
// (a copy repeats onto itself, a paste raises a prompt and a state paste
// repeats onto itself). Their acts are the propagate's own arms
// (handle_mode_keys, input_key_dispatch.cpp) with their ladders and cards:
// all three act from the WARP column alone (phase_reset_copy_refusal /
// phase_reset_paste_refusal, phase_reset_propagate.h, the first rung; the two
// pastes one ladder). THE COPY'S TWO READERS: its dispatch arm and the
// read-only allowlist (read_only_key_blocked, which admits it — architect
// 2026-11-17: "We should allow copy in read-only, the copy phase
// resets button." — clipboard-only, it writes no store; the iteration lock
// inherits the admission). The two pastes have one reader each, their
// dispatch arm: no allowlist names them, so they drop on a locked tab and
// under grid iterations. The `h` view's allowlist names none of the three.
inline bool is_phase_reset_copy_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::P && !mods.ctrl && !mods.shift && !mods.alt;
}
inline bool is_phase_reset_paste_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::P && mods.ctrl && !mods.shift && !mods.alt;
}
inline bool is_phase_reset_state_paste_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::P && mods.ctrl && mods.shift && !mods.alt;
}

// True for SHIFT+RETURN, THE LABEL PASTE (architect 2026-11-10: the Open Text
// Editor key's hold — the system clipboard's text onto every selected
// marker's line, no editor opened): shift-exact, no ctrl, no alt. One
// producer, that key's long press (redesign_button_shift_admits, app_state.h;
// the on-screen keyboard's Enter carries no shift). One-shot. ONE READER:
// the dispatch arm below bare Return's (on_key, input_handler.cpp). No
// allowlist names it — the read-only lock and the `h` view refuse it as they
// refuse the editor's open, and the iteration lock, whose one Return
// admission is the plain open on a bound axis, refuses it on its card.
inline bool is_label_paste_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::Return && mods.shift && !mods.ctrl && !mods.alt;
}

// True for the chord that toggles THE TOOLTIP LAMP (architect 2026-09-29,
// Enable Tooltips): the BARE BACKSLASH exactly — no ctrl, no shift, no alt.
// It arrives as its level-0 keysym (GuiKeys::Backslash), so a shifted press is
// the backslash plus the shift bit and this predicate refuses it. A chrome
// posture that authors nothing. One-shot (a held toggle would flicker). ITS
// READERS: the read-only allowlist (read_only_key_blocked, which ADMITS it,
// and the grid-iterations lock's gate falls through to that list), the `h`
// view's allowlist (history_mode_key_blocked, which admits it — the lamp is
// chrome, not that view's business), the dispatch arm (Backslash's case in
// handle_plain_bare_keys, input_key_dispatch.cpp, which the bare road alone
// reaches) and the three overlays' routers (2026-11-10;
// route_render_player_key, route_picker_key, route_score_viewer_key: the
// lamp stays live under every overlay).
inline bool is_tooltip_lamp_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::Backslash && !mods.ctrl && !mods.shift && !mods.alt;
}

// One delivered touch-navigation frame (the nav update hook's payload — the
// platform's deliver_touch_nav_frame builds it, the GUI's
// apply_touch_nav_update consumes it). The platform owns the per-frame delta
// bookkeeping (dx / dist_ratio against the previous DELIVERED frame, the
// latch, the frame coalescing) and applies NO gesture policy of its own; the
// GUI owns the model on top, which is ONE FINGER PANS, TWO FINGERS ZOOM
// (2026-08-14; apply_touch_nav_update's declaration, input_handler.h), and
// ONE FINGER UNDER CTRL ZOOMS TOO (2026-09-25; the S Pen's side button) —
// hence the finger count and the ctrl bit below, the two fields the GUI
// forks on. Every field here is read.
struct GuiTouchNavFrame {
    // THE CURRENT CENTROID'S REAL x, window px (single-finger: the finger
    // itself; the one horizontal place this frame carries, at its real
    // place by the no-grid rule, architect 2026-11-15): the pan's placement
    // column — the pan seats a song frame once per phase and places it at
    // this column every frame (apply_touch_nav_update's one-finger arm), so
    // the column must keep its fraction — and the zoom's pivot, the pinch's
    // and the one-finger zoom's alike, seated on the real centroid. No pixel
    // rides beside it and no y: the frame's readers place content by x alone
    // and test no box (the gesture's one surface answer is the down's).
    double cx = 0.0;
    // Centroid horizontal delta since the previous delivered frame
    // (fractional — sub-pixel centroid motion accumulates rather than
    // truncating away). Read on SINGLE-finger frames only — the pan's
    // travel, or under `ctrl` the one-finger zoom's (the nav drag's own
    // dx-to-level rule): two fingers never pan, so the centroid's travel is
    // discarded there.
    double dx = 0.0;
    // Finger-distance ratio current/previous, > 0 always (a degenerate
    // distance under 1 px on either side delivers 1.0; single-finger frames
    // are degenerate by construction, so they always carry 1.0).
    double dist_ratio = 1.0;
    // BOTH DELTAS AT THEIR NO-OP VALUES (dx 0.0, ratio 1.0) IS A DELIVERED
    // FRAME IN EXACTLY TWO CASES — the platform otherwise suppresses it, and
    // both are single-finger frames announcing a change of MEANING the GUI
    // could not otherwise hear from a finger that is standing still (the
    // exemption at set_touch_nav_hooks' update contract, input_core.h):
    //   * THE DOWNGRADE'S TRANSITION FRAME, delivered at the two-to-one lift
    //     so the GUI hears the pinch end; its errand is the seated pivot's
    //     clear, which apply_touch_nav_update runs above its no-op return.
    //   * THE CTRL EDGE'S FRAME (2026-09-25, the S Pen), delivered when the
    //     ctrl bit changes under a live single-finger nav: it carries the NEW
    //     bit, so a ctrl-down seats the one-finger zoom's pivot and paints its
    //     stem at the edge itself and a ctrl-up clears them there — not at the
    //     next motion.
    // Each carries the finger's own centroid, and the model applies nothing
    // for either.
    // TWO fingers vs the phone model's one — the GUI's fork between the
    // zoom-only gesture and the one-finger one.
    bool   two_finger = false;
    // THE LIVE CTRL BIT at this frame's delivery (GuiInputCore's tracked
    // ctrl, set_ctrl's state — the S Pen's side button, its one producer).
    // READ ON SINGLE-FINGER FRAMES ONLY: while it stands the one-finger
    // gesture is THE ZOOM ABOUT THE FINGER'S POINT rather than the pan, a
    // live fork mid-gesture (architect 2026-09-25: "the ctrl-zoom is the only
    // thing I want from pointer"). A two-finger frame
    // ignores it — the pinch is already the zoom. The platform reads it at
    // delivery and applies no policy; the fork is the GUI's.
    bool   ctrl = false;
};

// THE EDITOR-FIELD QUERY'S ANSWER (2026-09-05) — what the platform asks the
// GUI once at a first finger's down, beside the pan-zone query (the
// contract at GuiInputCore::set_touch_nav_hooks). It names the
// one surface whose plain drag diverges from the pointer road: inside an OPEN
// EDITOR'S FIELD a finger that drags moves the caret through the caret-drag
// hook trio and never becomes a pointer drag (begin_touch_caret_drag's
// declaration, input_handler.h).
//   * Outside     — not in the active editor's field (or no editor is open):
//                   the ordinary window, whatever surface it is.
//   * Field       — in the field: the window's slop crossing resolves to the
//                   CARET DRAG, and the window, like every touch window
//                   (2026-11-07), has NO DEADLINE
//                   (the field spends no hold, so a motionless finger stays
//                   Pending however long it rests and its lift is the
//                   ordinary tap burst — press and release, the press
//                   seating the caret). The pointer's press is reached there
//                   by a tap alone.
// The GUI answers it from the field rect its press claim reads, so the
// platform keeps no memory of taps and no second spelling of the field
// exists.
enum class GuiTouchEditorField { Outside, Field };

// THE TOOL THAT TOUCHED (2026-09-25, the S Pen) — the backend's answer at a
// contact's down, handed through GuiInputCore::touch_down (the door's
// contract there). THE PEN IS A FINGER WITH TWO AMENDMENTS (architect
// 2026-09-25; the pen rules are at on_motion_event,
// platform_android.cpp): it enters the touch machine exactly as
// a finger does — the phone-model pan, the caret drag and
// the tap-at-lift all its own — and differs in two places only: its side
// button is the Ctrl bit (the modifier door, not this enum), and IT IS NEVER
// A PINCH MEMBER — the one thing this tag decides inside the core. Finger is
// the default; the platform reads the tool type (STYLUS and ERASER are Pen).
enum class GuiTouchTool { Finger, Pen };
