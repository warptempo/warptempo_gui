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

struct GuiInputState {
    bool     ctrl                = false;
    bool     shift               = false;
    bool     alt                 = false;
    bool     primary_button_held = false;
    // The Unicode codepoint this key event resolves to under the live
    // keyboard state (shift / layout / compose applied), as computed by
    // xkb_state_key_get_utf32 at the platform boundary; 0 when the key
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
    // True iff this key event is a SYNTHESIZED KEY REPEAT — one the process
    // generated itself from a held key (GuiPlatform::maybe_fire_repeat, the
    // only writer — the transport arrows' button-side producer was deleted
    // 2026-08-13 with their hold-repeat), not a fresh physical press. A
    // platform-boundary fact in the same
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
// a Space carrying ctrl, shift, or alt has no binding, so it must not reach a
// toggle (the uniform rule: an unbound modifier combination is a no-op). Return
// / keypad Enter are NOT playback keys — Enter opens the flag editor on the
// focused marker (see the bare-Enter binding in input_handler.cpp). Shared by
// the on_key dispatch (input_handler.cpp) and the read-only allowlist predicate
// (input_key_dispatch.cpp), which are its only two callers; inline so both TUs
// see it, and one owner so the two readers cannot drift.
inline bool is_play_pause_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::Space && !mods.ctrl && !mods.shift && !mods.alt;
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
// latch, the frame coalescing); the GUI owns the gesture model on top —
// since 2026-08-14 that includes the two-finger FINGER-AGREEMENT SEGMENT
// LOCK, which is why the per-finger travel vectors ride along: the
// classification is between the two fingers' own motion vectors, which only
// the platform can measure, while the retunables it classifies against live
// in the GUI model (app_state.h's segment-lock block), so the platform hands
// the raw vectors across and the GUI applies the model.
struct GuiTouchNavFrame {
    // Current centroid, window px (single-finger: the finger itself).
    int    x = 0;
    int    y = 0;
    // Centroid horizontal delta since the previous delivered frame
    // (fractional — sub-pixel centroid motion accumulates rather than
    // truncating away).
    double dx = 0.0;
    // Finger-distance ratio current/previous, > 0 always (a degenerate
    // distance under 1 px on either side delivers 1.0; single-finger frames
    // are degenerate by construction, so they always carry 1.0).
    double dist_ratio = 1.0;
    // TWO fingers vs the phone model's one. The vectors below are meaningful
    // only while true (single-finger frames carry them at 0.0, the dormant
    // second-finger fields' own convention).
    bool   two_finger = false;
    // Each finger's CUMULATIVE travel vector (window px, y down) from the
    // two-finger gesture's start — the pair's formation, or an upgrade's
    // join, whichever seeded the pair. Absolute-from-start rather than
    // per-frame so the GUI's segment classification reads accumulated travel
    // (a slow pinch's per-frame deltas are tiny; its vectors are not) and so
    // the first delivered frame — which folds the whole latch crossing — can
    // classify in the same breath it arrives.
    double v1x = 0.0;
    double v1y = 0.0;
    double v2x = 0.0;
    double v2y = 0.0;
};
