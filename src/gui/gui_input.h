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
    // keyboard state (shift / layout applied), as computed by
    // xkb_state_key_get_utf32 at the platform boundary; 0 when the key
    // produces no character (function keys, bare modifiers). THREE CONSUMERS
    // (re-derived 2026-07-30): the text editors' printable-insertion path,
    // repeat_eligible's editor-typing repeat gate, and the bottom-strip prompt's
    // LETTER responses — the last joined 2026-07-30, because the prompt is the
    // product's one CASE-SENSITIVE letter surface and the case-folded GuiKey
    // cannot express that (CapsLock defeated the old !shift spelling). Every
    // other consumer reads the GuiKey and ignores this.
    uint32_t codepoint           = 0;
    // True iff this key event is a SYNTHESIZED KEY REPEAT — one the process
    // generated itself from a held key (GuiPlatform::maybe_fire_repeat, the only
    // writer), not a fresh physical press. A platform-boundary fact in the same
    // spirit as `codepoint`. Its one consumer is undo coalescing: a burst of
    // eligible gesture presses collapses into ONE undo entry exactly when the
    // continuation presses are repeats of the press that opened it, so a held key
    // coalesces by construction at any compositor's repeat delay and rapid
    // MANUAL taps stay separately undoable.
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
