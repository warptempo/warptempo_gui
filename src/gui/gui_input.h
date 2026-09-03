#pragma once
#include <cstdint>
#include <string>

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
    constexpr GuiKey Backslash    = 0x005c;
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

// -- THE ARROW STEP LADDER (architect 2026-08-31, R12) -----------------------
//
// THE MODIFIER IS THE STEP'S MAGNITUDE, ON THE ARROWS' EVERY SUBJECT: bare is
// ONE unit, Shift is THREE and Ctrl is TEN, and the unit is whatever the bare
// arrow's own act steps — a painted COLUMN for Left / Right (the playhead in
// the waveform lane, the focused marker in the marker lane, both columns) and
// a CENT for Up / Down (the tempo step, singleton and group). ONE OWNER for
// all four arms, because the ladder is one ruling and not two: a second
// spelling in the vertical arm could drift from the horizontal one.
//
// THE ORDER bare < shift < ctrl IS DELIBERATE (the architect's own): shift is
// the everyday coarse step and ctrl the long jump, so the modifier that is
// harder to hold is the one that moves further.
//
// CTRL+SHIFT SPELLS NOTHING — strict modifier validation, so the pair is the
// consumed no-op it is everywhere else and no arm composes a 30. Alt likewise.
// The dispatch arms ask for the magnitude only after they have refused those
// combinations; this function answers ctrl first and never sees the pair.
//
// THE WALLS ARE THE BARE FORM'S, at the scaled size: a SINGLETON step clamps
// onto its wall (so a 10-column press near the end lands exactly on the end)
// and a GROUP press refuses whole where any member could not take the FULL
// step — the unified wall policy, stated at the head of position_nudge.h and
// instanced at the group tempo scan (tempo_cent_step_group_actionable).
inline constexpr int64_t kArrowStepShift = 3;
inline constexpr int64_t kArrowStepCtrl  = 10;
constexpr int64_t arrow_step_magnitude(GuiInputState mods) {
    if (mods.ctrl)  return kArrowStepCtrl;
    if (mods.shift) return kArrowStepShift;
    return 1;
}

// -- THE CHORD SPELLER (architect 2026-08-30) --------------------------------
//
// THE ONE PLACE A PRESS IS TURNED INTO THE NAME THE USER READS. The strictness
// ruling ("go very verbose — a card for every refusal that is silent today")
// gave the gates sentences that must say WHICH press was refused, and THE
// THREE THAT NAME ONE compose it here: the keyboard-modal editor gate, the `h`
// view's allowlist and the read-only lock. NO OTHER SITE COMPOSES A CHORD NAME
// FOR A CARD, and spell_modifiers below has no caller of its own — it is this
// speller's prefix half and nothing else. The roster's TOOLTIPS carry chords
// too, as hand-written literals in redesign_button_tooltip, and they are a different
// job: a tooltip advertises a BOUND chord in advance, this spells a press that
// just happened.
//
// AND ALL THREE SPEAK ONLY FOR A CHORD THIS PRODUCT BINDS IN THE STANDING MODE
// (architect 2026-08-30: bound keys either show an effect or a card, so an
// unbound key is identified by its silence; the mode term added 2026-09-01,
// U4). Each asks chord_is_bound — the inventory below this speller, which reads
// the `h` view's bit so that the view's own seven shapes are spoken for only
// while it stands — before it composes anything, and a press nothing binds
// there dies at its gate saying nothing at all. So every chord a card names is
// one the user could have expected to do something IN THE STATE HE PRESSED IT.
//
// (WHAT THIS SPELLER SERVED UNTIL THAT RULING, all retired with the "<chord>
// is not bound" class: the strict-modifier tail, the unbound bare default,
// the render player's and the picker's two catch-alls each, and — through
// spell_modifiers alone — the pointer's unbound modified press.)
//
// AND THE NAMES ARE THE PRODUCT'S, NOT THIS SURFACE'S: every user-facing
// spelling of a key follows KDENLIVE (Qt), one convention stated here and
// followed everywhere — the roster's tooltips, the modal hints, the on-screen
// keyboard's caps, HELP and the README alike (architect 2026-09-01, retiring
// the "two surfaces, two conventions" split the tooltips and the cards ran
// under until that day: an exception in user-facing text is a design smell,
// so there is one rule and no per-surface convention left). Qt's own English
// names for the named keys (Esc, Del, Return, Backspace, PgUp, PgDown, Space,
// Tab, Home, End, the four arrows — QKeySequence's keyname[] table, researched
// against the source), bare letters UPPERCASE, punctuation naming the CAP
// rather than the stamped symbol ("Shift+[", "Ctrl+=", "Ctrl+-"), modifiers
// Ctrl, Alt, Shift joined by '+'.
//
// THE UPPER-CASING OF A BARE LETTER took a one-day round trip that the
// succession records whole rather than pretending it never happened:
// UPPER-CASED at the cards' own landing (architect 2026-08-30 — the
// chord stands alone inside a sentence rather than in parentheses after a
// verb, where a lone lowercase letter reads as prose instead of as a key, and
// no capital can be mistaken for a shifted press because THE SHIFT IS ALWAYS
// SPELLED: "Shift+F" is the shifted one, "F" is the key); LOWERCASED FOR THE
// BARE LETTER on the morning of 2026-09-01 ("with no modifier named, the
// letter's case is the only shift signal", so a card reading "V is not
// available on a read-only tab" was held to imply a shift the sentence never
// spells); and UPPER-CASE AGAIN that same day (architect 2026-09-01, the
// standing ruling), on the evidence of KDENLIVE ITSELF — the design reference
// this GUI is modelled on writes "Ctrl+S" and a bare "T" alike in uppercase in
// its own accelerator convention, and the product follows its reference. THE
// SHIFT AMBIGUITY IS AN ACCEPTED COST, not an oversight: the morning's
// objection is sound and was weighed and lost, the idiom the reader already
// knows from the application beside this one beating a spelling that is
// unambiguous only on its own terms.
//
// THE TOOLTIPS FOLLOW THE SAME NAMES (architect 2026-09-01): the roster's
// "(V)"-style chord suffixes are hand-written literals in
// redesign_button_tooltip's own table (app_state.h) — a different JOB, since
// they advertise a BOUND chord in advance rather than naming a press that just
// happened — but no longer a different CONVENTION. Two surfaces, one spelling,
// stated here.
//
// A CARD NAMING THE CHORD THE USER PRESSED IS NOT A GESTURE HINT (the standing
// no-hints rule): it names what just happened, never what to press instead.
//
// THE SPELLING: modifiers first in the fixed order Ctrl, Alt, Shift, joined by
// '+', then the key — a printable ASCII key by its own character upper-cased
// ("F", "1", "/"), a named key by its name. THE ORDER IS THE PRODUCT'S OWN, the
// one every chord in HELP and in the docs is written in (Ctrl+Alt+Shift+R), so
// the card and the documentation spell one chord one way.
//
// EVERY CHORD THIS SPELLER IS ASKED FOR HAS A NAME, BY CONSTRUCTION, and there
// is no fallback for one that does not (architect 2026-08-31): the three
// readers ask chord_is_bound first, and every key that inventory admits is
// either named in the table below or a printable ASCII character spell_chord
// upper-cases. every_bound_key_is_spellable, beside chord_is_bound's own
// anchors, PINS the implication at compile time, so a binding added on a key
// with no name breaks the build rather than reaching a card.
//
// THE TABLE IS THE BOUND SET'S AND NOTHING WIDER (architect 2026-08-31, his
// DELETE). It carried THREE MORE BLOCKS — the keypad whole, the editing and
// system block, and the laptop's own vendor strip, in a GuiSpellKeys namespace
// of their own — written when a press the platform could name and the card
// would not was an identity thrown away: the Wayland boundary forwards the
// level-0 keysym of every key that is not an F-key and not a modifier
// (GuiPlatform::key_from_keycode), so the unbound half of a PC keyboard
// reaches this speller. THE UNBOUND-KEYS RULING TOOK THEIR LAST PRODUCER on
// 2026-08-30 — every gate that names a chord speaks only for a chord
// chord_is_bound admits, and no value in those blocks was in that inventory —
// and they stood one more day as "one owner of what a key is called, complete
// rather than caller-shaped". THE ARCHITECT RULED THAT KEEPING OUT: an arm
// exists iff a producer exists (validation_topology.md), and there is no
// FUNCTIONAL side to the deletion at all — the ignoring of a keypad or a
// vendor press never lived in this speller, it lives in the dispatch, which
// binds none of them and answers them with the silence either way. A future
// binding on one of those keys brings its name with it.
//
// WHAT WENT WITH THE BLOCKS: the "That key" fallback ("A KEY WITH NO SPELLING
// IS 'That key', whole, modifiers and all" — an unknown keysym having no name
// to hang a prefix on), which those blocks were the last thing standing
// between and unreachability; and the vendor space the 2026-08-30 narrowing
// had accepted as unnamed (XF86Calculator, browser Back/Forward, Power, Sleep,
// WLAN, keyboard brightness, Touchpad Toggle, a space of hundreds), which that
// day's ruling had already stopped owing a sentence to. THE XKB ROAD STAYS
// REJECTED on its own reasoning — deriving the name from xkbcommon at the
// Wayland boundary (xkb_keysym_get_name) would put a STRING on the key event
// and so widen the platform seam for a name the Android backend, which
// produces GuiKeys only through synthesize_key, could never spell the same
// way; the two hosts must answer one press one sentence.
//
// NO F-KEY ARM, deliberately: F1..F35 are dropped at the Wayland boundary
// before delivery (GuiPlatform::key_from_keycode, "this GUI binds none of
// them") and the Android backend produces GuiKeys only through synthesize_key,
// so no F-key can reach a card on either host — and none is bound, so none
// could reach this speller in any case. An arm exists iff a producer does
// (validation_topology.md): a backend that delivered one would have to BIND it
// before a card could name it, and the compile-time pin above is what would
// then ask for the name.
//
// KEY REPEAT RIDES THE CARDS' OWN CARVE-OUT: a held key whose repeats are
// eligible (repeat_eligible, input_key_dispatch.cpp) re-fires its gate at the
// compositor's cadence and each fire re-pushes the SAME sentence, which
// GuiNotifications::notify keeps as ONE card moved back to the top with a fresh
// clock — one card for a hold, exactly as for a single press. DISTINCT PRESSES
// DO NOT COALESCE ANY MORE (architect 2026-09-01): each pushes its own card, so
// a wall hit three times shows three, and what tells the two apart is
// synthesized_repeat itself, read once at on_key's head and parked for the
// dispatch (AppState::Notifications::held_repeat_dispatch). No gate here counts
// presses or reads the bit — the one reader is that chokepoint.
inline std::string spell_modifiers(GuiInputState mods) {
    std::string out;
    if (mods.ctrl) out += "Ctrl";
    if (mods.alt) {
        if (!out.empty()) out += '+';
        out += "Alt";
    }
    if (mods.shift) {
        if (!out.empty()) out += '+';
        out += "Shift";
    }
    return out;
}

// The named keys, and the whole of them: every key chord_is_bound admits that
// is not a printable ASCII character, plus Space (a printable that reads as a
// blank). THE MEMBERSHIP IS THE BOUND SET'S (architect 2026-08-31, the head's
// ruling) and it is closed in both directions: nothing can ask for the name of
// a key this product binds nowhere, every reader gating on chord_is_bound
// first, and every_bound_key_is_spellable pins the other way round — no bound
// key is left without a name here, which is what lets spell_chord carry no
// unnamed fallback. THE NAMES ARE QT'S OWN (the head's one convention): Esc,
// Del, Return, PgUp, PgDown — kdenlive's spellings, not the longhand this
// table wrote until 2026-09-01. The two pairs that share a name share it
// deliberately — Return / KpEnter are one "Return" to the hand, since both
// keysyms open the flag editor and Qt's separate "Enter" for the keypad would
// name a second key the product does not distinguish — and IsoLeftTab IS the
// shifted Tab keysym. HELP KEEPS THE SPLIT IN ONE SENTENCE (planner
// 2026-09-01): the flag editor's paragraph says "`Return` or keypad `Enter`",
// which is Qt-correct and the one place the distinction tells the reader
// something; nothing this speller composes ever says "Enter".
//
// TWO NAMES THIS TABLE DOES NOT HOLD, and cannot: the SUPER MODIFIER is
// dropped at the platform boundary and no chord names it, so its Qt spelling —
// `Meta`, kdenlive's own — lives in HELP alone, introduced there once as
// "`Meta` (the Super or Logo key)" for the reader whose keycap says Super
// (planner 2026-09-01). The PAINTED KEYBOARD'S CAPS are the other: they name
// the same keys the same way but are their own table (onscreen_keyboard.h's
// cap_word, which points here for the convention).
//
// BACKSPACE LEFT WITH THE THREE NAME-ONLY BLOCKS on 2026-08-31: it is one of
// the keys chord_is_bound lists as deliberately absent — the editors consume
// it themselves and no gate on the main dispatch can name it — so its arm had
// no producer either.
constexpr const char* spell_key_name(GuiKey key) {
    switch (key) {
        case GuiKeys::Space:      return "Space";
        case GuiKeys::Return:
        case GuiKeys::KpEnter:    return "Return";
        case GuiKeys::Tab:
        case GuiKeys::IsoLeftTab: return "Tab";
        case GuiKeys::Escape:     return "Esc";
        case GuiKeys::Delete:     return "Del";
        case GuiKeys::Home:       return "Home";
        case GuiKeys::End:        return "End";
        case GuiKeys::PageUp:     return "PgUp";
        case GuiKeys::PageDown:   return "PgDown";
        case GuiKeys::Up:         return "Up";
        case GuiKeys::Down:       return "Down";
        case GuiKeys::Left:       return "Left";
        case GuiKeys::Right:      return "Right";
        default:                  return nullptr;
    }
}

inline std::string spell_chord(GuiKey key, GuiInputState mods) {
    std::string name;
    if (const char* named = spell_key_name(key)) {
        name = named;
    } else {
        // EVERY OTHER BOUND KEY IS A PRINTABLE ASCII CHARACTER, and the GuiKey
        // IS that character (the keysym table is ASCII here) — the guarantee
        // at spell_key_name, pinned by every_bound_key_is_spellable — so this
        // is the second and last arm and there is no unnamed third (the "That
        // key" fallback retired 2026-08-31 with the speller's three name-only
        // blocks, which were the only thing that could reach it). It is the
        // KEY'S OWN character, never mods.codepoint: the shifted `1` is
        // still the 1 key, and a card that called it "!" would name a press the
        // user cannot find on the board. Letters arrive case-folded from the
        // platform boundary, so this is the one place they are put back up —
        // UNCONDITIONALLY, modifier or none ("Ctrl+S" and a bare "T" alike),
        // which is kdenlive's own accelerator convention and the head's
        // standing ruling; the one-day lowercase fork lived exactly here and
        // its succession is recorded there. Digits and punctuation have no
        // case, so the compare reaches only the letters.
        char c = static_cast<char>(key);
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        name.assign(1, c);
    }
    const std::string prefix = spell_modifiers(mods);
    if (prefix.empty()) return name;
    return prefix + "+" + name;
}

// -- IS THIS CHORD BOUND IN THIS STATE? (architect 2026-08-30; the one state
// term added 2026-09-01, U4) ------------------------------------------------
//
// "Bound keys either show an effect or a card, so an unbound key is identified
// by its silence." The gates that answer a swallowed press therefore ask this
// first, and say nothing for a chord that binds nothing where the press
// landed: the deduction only works if the silence is reserved for it. SIX
// READERS, all of them gates and all in on_key (re-greped at this edit): the
// keyboard-modal editor gate, the editor TEXT DRAG's gate and the pointer
// gestures' DRAG-MODAL gate, the loading gate, the `h` view's allowlist and
// the read-only lock — five
// sentences between them, the two drag gates sharing one literal. THE
// PLAYER'S AND THE PICKER'S OWN DRAG ARMS, which say that same literal one
// rank higher, DO NOT ASK: while either mode stands its router is the whole
// vocabulary, and this inventory does not know it — a bare `r` swallowed by
// the player's scroll drag is a bound key there and must be answered.
//
// WHAT COUNTS AS BOUND IS WHAT THE MAIN DISPATCH CLAIMS IN THE STANDING MODE —
// every chord on_key's own road would claim in the state the press arrived in,
// whether or not it would ACT there. The five sub-handlers on that road are in
// it (handle_escape_cancels, handle_render_dispatch_keys, handle_mode_keys,
// handle_tab_switch_keys and handle_plain_bare_keys' switch), and so is
// handle_history_mode_key: the `h` view ADDS its seven shapes to the dispatch
// rather than replacing it — the allowlist below it still admits the ordinary
// vocabulary — so bare `g`, bare `u`, the `,` / `.` pair with and without shift
// and bare `v` are bindings of this product's keyboard like any other WHILE THE
// VIEW STANDS, and nothing at all outside it. (Bare `h` itself is NOT one of
// them: the toggle is claimed in every state, which is why it reads no term.)
//
// THE MODE BIT IS THE ONE STATE TERM THIS PREDICATE CARRIES (architect
// 2026-09-01, U4), and it is THE DISPATCH'S OWN TEST MADE EXECUTABLE rather
// than a second copy of it: those seven shapes' whole binding sits behind two
// lines — handle_history_mode_key's `if (!app.history_mode.active) return
// false;` (input_key_dispatch.cpp), which owns six of them, and the revert
// arm's own `app.history_mode.active &&` (input_handler.cpp), which owns bare
// `v` — so outside the view all seven fall to handle_plain_bare_keys' default
// and to the strict-modifier tail, which already answer them with the unbound
// silence. Reading the bit here is what makes the gates say the same thing.
//
// THE SUCCESSION: this predicate was STATE-BLIND from its landing (2026-08-30)
// to 2026-09-01, and the architect's own instance was a bare `v` pressed on a
// LOCKED TAB OUTSIDE THE VIEW, answered with "V is not available on a read-only
// tab" — a sentence about a key that a WRITABLE tab in that same state would
// have ignored in silence. The same lie stood at the two drag gates and the
// loading gate, all four gates asking this one predicate, and all four went
// truthful in the one edit with no site of theirs touched. What did NOT change:
// the read-only gate's MEMBERSHIP is still the chord's alone
// (read_only_key_blocked knows no mode), every act, every face and every
// existing lock refusal are untouched, and drift here stays what it always was
// — a MISSED CARD, never a wrong act.
//
// THE THREE MODE ROUTERS ARE NOT, and that is the line: while the RENDER
// PLAYER, the PICKER or the AV SYNC STATS PANEL stands, its router IS the whole
// vocabulary and the dispatch
// below it never runs, so its keys are its own world and not the product's —
// bare `r` flips Repeat one in the player and binds nothing anywhere else
// (the render chords are Ctrl+Alt), and an `r` pressed outside it must be as
// silent as any unbound letter. None of the three
// routers can reach a gate below it either (all are ranked above all six), so
// no term for them could ever be asked. THE EDITORS ARE THE SAME KIND OF
// WORLD: an editor's own keys (its motion arm, its ctrl-exact clipboard
// chords, its printable insertion) are consumed by the editor itself and
// never reach the gate that would ask this — what the gate blocks is the
// chords the editor does NOT own, which is what this inventory is for.
//
// A NEW BINDING JOINS THIS LIST. That is the standing cost of spelling the
// dispatch's membership twice, taken deliberately: the dispatch is an if-ladder
// across three translation units with its modifier tests written inline, and
// there is no shape to read it off. Drift here is a MISSED CARD (a bound chord
// answered by silence at a gate), never a wrong act — nothing routes through
// this predicate — and codex is the backstop that finds it. kToolbarChords
// carries a fraction of the set (the chords the roster's buttons synthesize)
// and is no substitute for reading the dispatch.
//
// WHAT IS DELIBERATELY ABSENT: bare `e`, which the platform boundary turns
// into the left mouse button before a key event exists (kLeftClickKey — it
// reaches on_key only as a character inside an editor); the digits 4..9;
// Backspace, and every letter the ladder never tests (A, B, E, W, X — Y left
// the class 2026-08-31, the centered pin's toggle, and V on 2026-09-01, the
// `h` view's revert act moving onto it off Ctrl+H);
// every key the boards carry that this switch names nowhere (the keypad, the
// editing and system block, the vendor strip — the speller named them too
// until 2026-08-31, when the blocks were deleted for want of a producer); and
// every modifier decoration no arm spells, which is strict modifier
// validation's whole no-op class.
constexpr bool chord_is_bound(GuiKey key, GuiInputState mods,
                              bool history_view) {
    const bool ctrl = mods.ctrl, alt = mods.alt, shift = mods.shift;
    const bool bare  = !ctrl && !alt && !shift;   // no modifier at all
    const bool sh    = !ctrl && !alt &&  shift;   // Shift alone
    const bool cl    =  ctrl && !alt && !shift;   // Ctrl alone
    const bool cs    =  ctrl && !alt &&  shift;   // Ctrl+Shift
    const bool ca    =  ctrl &&  alt && !shift;   // Ctrl+Alt
    const bool cas   =  ctrl &&  alt &&  shift;   // Ctrl+Alt+Shift
    switch (key) {
        // -- letters, bare only, bound in EVERY state: the view toggles and the
        // mode toggles (`c` centre, `f` follow, `i` iteration, `k` add to
        // selection, `l` the render player, `m` bpm mode, `t` the S/T flip,
        // `y` the centered pin).
        case GuiKeys::C: case GuiKeys::F: case GuiKeys::I:
        case GuiKeys::K: case GuiKeys::L: case GuiKeys::M: case GuiKeys::T:
        case GuiKeys::Y:
            return bare;
        // -- THE `h` VIEW'S OWN BARE LETTERS, bound while it stands and unbound
        // outside it (2026-09-01, U4): `g` the walk source, `u` the compare
        // reading — both handle_history_mode_key's, behind its mode return —
        // and `v` the REVERT ACT (2026-09-01, off Ctrl+H), whose own arm in
        // input_handler.cpp carries the same mode test.
        case GuiKeys::G: case GuiKeys::U: case GuiKeys::V:
            return bare && history_view;
        // The value pair: copy, and the jump to where the value came from.
        case GuiKeys::J: return bare || sh;
        // Drop a warp marker / drop a phase reset from the warp column / save.
        case GuiKeys::S: return bare || sh || cl;
        // The read-only toggle and Open project.
        case GuiKeys::O: return bare || cl;
        // The W/P flip and the three phase-reset propagate chords.
        case GuiKeys::P: return bare || cl || ca || cas;
        // The history view's toggle — bound in BOTH modes, since it is what
        // opens the view and what closes it (handle_history_mode_key claims it
        // in every state), which is why it reads no mode term while the seven
        // shapes the view ADDS do. ITS REVERT LEFT THIS LINE 2026-09-01:
        // the act was Ctrl+H from 2026-08-05 and is bare `v` now (with the
        // mode-only letters above), so the ctrl spelling binds nothing again
        // and takes the unbound silence.
        case GuiKeys::H: return bare;
        // Toggle disabled / toggle inherit / quit.
        case GuiKeys::D: case GuiKeys::N: case GuiKeys::Q: return cl;
        // Undo, and redo on the one meaningful shift bit.
        case GuiKeys::Z: return cl || cs;
        // The render pair: the dispatch and the archival one.
        case GuiKeys::R: return ca || cas;

        // The zoom-out and the three absolute view selectors.
        case GuiKeys::Digit0: case GuiKeys::Digit1:
        case GuiKeys::Digit2: case GuiKeys::Digit3:
            return bare;

        // The measure editor, the measure copy and the measure paste.
        case GuiKeys::Slash: return bare || cl || ca;
        // The settings editor, and the load in place / render player.
        case GuiKeys::Semicolon: case GuiKeys::Apostrophe: return bare;
        // Show the trim region, and maximize it to the whole song.
        case GuiKeys::BracketLeft: return bare || sh;
        // Bare is the waveform magnification, ctrl the horizontal zoom.
        case GuiKeys::Equal: case GuiKeys::Minus: return bare || cl;
        // The `h` walk: bare steps, shift jumps to its ends — the mode's own
        // arm again (handle_history_mode_key, behind its mode return), so both
        // spellings are bound while the view stands and unbound outside it
        // (2026-09-01, U4).
        case GuiKeys::Comma: case GuiKeys::Period:
            return (bare || sh) && history_view;
        // Synchronize to external storage (2026-08-31).
        case GuiKeys::Backslash: return bare;

        // Play from the playhead, and the A/B audition.
        case GuiKeys::Space: return bare || sh;
        // BARE ONLY: the render cancel, the notification stack's clear and the
        // top-level no-op arm that is deliberately silent — an arm all the
        // same, so Esc is a bound key. NO MODIFIED ESCAPE BINDS ANYWHERE:
        // Ctrl+Esc carried the stack's bulk clear for one morning of
        // 2026-09-01 and retired that evening when bare Esc took the whole
        // stack, one act wanting one chord.
        case GuiKeys::Escape: return bare;
        // Open the flag editor on the focused marker.
        case GuiKeys::Return: case GuiKeys::KpEnter: return bare;
        case GuiKeys::Delete: return bare;
        // The marker walk (bare forward, shift back), the A/B tab switch and
        // the paired march.
        case GuiKeys::Tab: return bare || sh || cl || cs;
        // The shifted Tab's own keysym, admitted shift-agnostically as the
        // live walk admits it.
        case GuiKeys::IsoLeftTab: return bare || sh;
        // The tempo cent step, and the playhead / marker position step —
        // EACH IN THREE MAGNITUDES since 2026-08-31 (R12): bare one unit,
        // Shift three, Ctrl ten, on whatever the bare arrow's own subject is
        // (the ladder's owner is arrow_step_magnitude above). Ctrl+Shift
        // spells nothing on any of the four.
        case GuiKeys::Up: case GuiKeys::Down:
        case GuiKeys::Left: case GuiKeys::Right:
            return bare || sh || cl;
        // The trim bounds, and the whole-piece ends under ctrl.
        case GuiKeys::Home: case GuiKeys::End: return bare || cl;
        // The viewport's stepped scroll.
        case GuiKeys::PageUp: case GuiKeys::PageDown: return bare;

        default: return false;
    }
}

// ANCHORS, not a second copy of the inventory: each pins one rule the list
// above must keep, so a careless widening trips at compile time. The chords
// that read no mode term are pinned OUTSIDE the view (the ordinary state), and
// the three anchors that follow them pin the term itself, both ways.
static_assert(!chord_is_bound(kLeftClickKey, GuiInputState{}, false),
              "bare `e` is the left mouse button at the platform boundary and "
              "must never become a key binding");
static_assert(!chord_is_bound(GuiKeys::Digit4, GuiInputState{}, false) &&
                  !chord_is_bound(GuiKeys::Digit9, GuiInputState{}, false),
              "digits 4..9 are unbound");
static_assert(chord_is_bound(GuiKeys::Escape, GuiInputState{}, false),
              "bare Esc is bound; it is one of the nine-place contract's own "
              "arms (the notification stack's clear), and its top-level "
              "silence is that arm's own, reached only with no card standing");
static_assert(!chord_is_bound(GuiKeys::Escape,
                              GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::Escape,
                                  GuiInputState{true, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Escape,
                                  GuiInputState{false, true, false}, false),
              "Esc is bare-exact: no modified Escape binds anywhere, Ctrl+Esc "
              "included since it retired on 2026-09-01");
static_assert(chord_is_bound(GuiKeys::Space, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::Space,
                                 GuiInputState{false, true, false}, false) &&
                  !chord_is_bound(GuiKeys::Space,
                                  GuiInputState{true, false, false}, false),
              "Space binds bare and shifted only — strict modifier validation");
static_assert(chord_is_bound(GuiKeys::Backslash, GuiInputState{}, false) &&
                  !chord_is_bound(GuiKeys::Backslash,
                                  GuiInputState{true, false, false}, false),
              "Synchronize is bare backslash and no decoration of it");
static_assert(chord_is_bound(GuiKeys::Up, GuiInputState{}, false) &&
                  chord_is_bound(GuiKeys::Up,
                                 GuiInputState{false, true, false}, false) &&
                  chord_is_bound(GuiKeys::Up,
                                 GuiInputState{true, false, false}, false) &&
                  !chord_is_bound(GuiKeys::Up,
                                  GuiInputState{true, true, false}, false),
              "an arrow binds bare, Shift and Ctrl — the step ladder's three "
              "magnitudes — and Ctrl+Shift spells no fourth");
// THE MODE TERM, pinned in both directions (2026-09-01, U4). Bare `v` is the
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

// THE SPELLER'S GUARANTEE, PINNED (2026-08-31, the day its three name-only
// blocks and its "That key" fallback were deleted): EVERY CHORD THIS
// PREDICATE ADMITS HAS A NAME — its key is either in spell_key_name's table or
// a printable ASCII character spell_chord writes by itself — which is exactly
// what lets that speller carry no unnamed arm. A binding added on a key with
// neither breaks the build here instead of putting a control byte on a card.
//
// THE WALK'S RANGE IS THE TWO HOMES GuiKey LIVES IN, and it is a claim about
// the switch above rather than about keysym space: every GuiKeys value is
// either an ASCII keysym (0x20..0x7e) or one of the function-key blocks'
// (0xfe20 for the shifted Tab, 0xff00..0xffff for the rest), so a case label
// outside these ranges would need a GuiKeys value outside them — the day this
// range grows with it. Eight modifier combinations per key × BOTH VALUES OF THE
// MODE BIT (2026-09-01, U4), which is the whole space a press can present:
// strict modifier validation bounds the first factor, and the second is the one
// state term the inventory carries, so a binding added on a mode-only key with
// no name breaks the build exactly as an ordinary one does.
consteval bool every_bound_key_is_spellable() {
    auto spellable = [](GuiKey k) {
        return spell_key_name(k) != nullptr || (k > 0x20 && k < 0x7f);
    };
    auto scan = [&](uint32_t lo, uint32_t hi) {
        for (uint32_t k = lo; k <= hi; ++k) {
            const GuiKey key = static_cast<GuiKey>(k);
            if (spellable(key)) continue;
            for (int bits = 0; bits < 8; ++bits) {
                const GuiInputState mods{(bits & 1) != 0, (bits & 2) != 0,
                                         (bits & 4) != 0};
                if (chord_is_bound(key, mods, /*history_view=*/false) ||
                    chord_is_bound(key, mods, /*history_view=*/true))
                    return false;
            }
        }
        return true;
    };
    return scan(0x0020, 0x007f) && scan(0xfe00, 0xffff);
}
static_assert(every_bound_key_is_spellable(),
              "a bound chord with no name: give the key an arm in "
              "spell_key_name");

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

// True for the chord that runs SYNCHRONIZE TO EXTERNAL STORAGE (architect
// 2026-08-31): BARE BACKSLASH exactly — no ctrl, no shift, no alt. Synchronize
// was the File menu's ONE CHORD-LESS ROW from its 2026-08-27 landing (the
// architect refusing a binding then rather than deferring one, Ctrl+Alt+Shift+R
// keeping its meaning), and this is the row joining the keyboard: `\` is a key
// the product bound nowhere and carries no convention to honour, so it costs
// no chord from the render family. The act is
// GuiInputHandler::synchronize_to_external_storage, whose own body carries
// every gate (the modal refusals, the loading state, no source loaded, the
// single act in flight) — which is why the key needs none of its own beyond
// its place in the dispatch. THREE READERS, the shape shared so none can
// drift: on_key's dispatch arm (input_handler.cpp), the read-only allowlist
// (read_only_key_blocked — ADMITTED, the act authoring nothing and writing
// outside the project entirely) and the `h` view's allowlist
// (history_mode_key_blocked — ADMITTED, on the 2026-08-29 "admit both" ruling
// that put Ctrl+O beside Ctrl+Q there; the menu's row already ran in the view
// through its own body).
inline bool is_sync_external_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::Backslash && !mods.ctrl && !mods.shift && !mods.alt;
}

// True for the chord that DROPS A PHASE RESET FROM THE WARP COLUMN'S EITHER
// VIEW (architect 2026-08-28, born "from any view"; the P column refuses it
// whole since 2026-08-30 — the paragraph below): SHIFT+S exactly — no ctrl,
// no alt. It lands the session in
// T+P, the lead-in's own view, and drops the lead-in reset at the playhead's own
// musical instant there, so the reset the architect wants while he is looking
// at the warp column costs one key instead of a view trip and back. Ctrl+S is
// the save on this letter and Shift+S was the strict rule's consumed no-op
// until this date; the shifted `s` is the only decoration on it that binds.
//
// IT IS BARE `s`'s ACT WITH THE VIEW TRIP IN FRONT, not a second one: from
// the W column the difference is exactly the two view chokepoints Shift+S
// runs first, and then the same one drop body (the act is
// GuiInputHandler::drop_phase_reset_in_target_view, input_handler.cpp). IN
// THE P COLUMN THE CHORD REFUSES WHOLE since 2026-08-30 (architect): the
// command IS the crossing from warp view, so with phase resets already
// showing there is nothing to cross and the act answers "Already in phase
// reset view" before any switch — T+P and S+P alike, bare `s` untouched
// (the leading refusal is phase_reset_drop_crossing_actionable,
// app_state.h).
// The same two readers as the Space pair above — on_key's dispatch arm
// (input_handler.cpp) and the read-only allowlist (read_only_key_blocked,
// input_key_dispatch.cpp, which DROPS the chord: the drop is authored
// content, exactly as bare `s` is) — and the same one-owner reason.
inline bool is_phase_reset_drop_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::S && !mods.ctrl && mods.shift && !mods.alt;
}

// THE VALUE PAIR (architect 2026-08-29), the two acts that replaced the
// retired resolved readout and its Ctrl+C: bare `j` COPIES the focused
// marker's resolved value to the system clipboard, and SHIFT+`j` JUMPS to the
// marker that value came from — the pass's owner or the ref's definition — on
// the OTHER A/B tab, so the two tabs stand on the reference and its
// definition at once. `j` was unbound (verified by grep at the landing), and
// the two spellings are the roster's own shift-twin shape: one button, its
// plain lift the copy and its shift-click or long press the jump.
//
// BOTH ARE BARE-/SHIFT-EXACT, so ctrl and alt decorations stay the strict
// rule's consumed no-ops, and both are ONE-SHOT (repeat-ineligible: a copy
// repeats onto itself and a jump has one destination). Their subject is the
// SELECTION'S FOCUS, and an ineligible focus — an owner, a phase reset, a
// disabled marker, the `P` column, nothing focused, or a member of a
// coincident-collapsed stack — is a consumed no-op at the act that says so
// on a card since 2026-08-30, each chord in its own words and the stack in
// its own sentence (payload_eligibility, app_state.h). Both are
// READ-ONLY-LEGAL (they author nothing: read_only_key_blocked admits them)
// and legal in both audio views, being navigation rather than positional
// authoring; the `h` view refuses them at its own allowlist like every chord
// it does not name. The same two readers as the pairs above — on_key's
// dispatch arms (input_handler.cpp) and the read-only allowlist — and the
// same one-owner reason.
inline bool is_copy_value_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::J && !mods.ctrl && !mods.shift && !mods.alt;
}
inline bool is_jump_to_value_source_key(GuiKey key, GuiInputState mods) {
    return key == GuiKeys::J && !mods.ctrl && mods.shift && !mods.alt;
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
