#pragma once

// THE ON-SCREEN KEYBOARD — the painted key surface the GLASS types on
// (architect + planner 2026-08-27). This header is the surface's ONE OWNER of
// everything that is not pixels or a press body: the layout table, the
// geometry, the two lamps' session-change reset, the rule that the waveform is
// not painted under the band, and the predicate that says whether the surface
// stands at all. The PAINTER lives in paint_handler.cpp beside
// every other painter, and the PRESS ROUTER in input_pointer.cpp beside every
// other press router; both walk this file's table through the one walker below,
// so paint and hit cannot describe different keys.
//
// WHAT IT IS. A COMPACT, PHONE-SHAPED KEYBOARD (architect 2026-10-29: "small,
// centered, like a phone … chiclets, taller than wide"): five rows of keys
// on TWO PAGES built for the product's own grammars — FLAGS, the letters, the
// digits and the flag grammar's operators, and SYMBOLS, the rest of printable
// ASCII (the layout table below) — its key box authored at 300 % and scaled,
// under tuning (the `osk_key_w` and `osk_key_h` device keys, the geometry
// below; its caps' size the `osk_font_px` key since 2026-10-29 night,
// cap_word's paragraph), the block of keys CENTERED in a band that runs the
// window's width
// on the window's foot, in the brushed ground, nothing beside the block (the
// placement block below), no line of its own, row 8 and the waveform area's
// lower part under it and nothing moved — the waveform's own passes then not
// painting under it at all (waveform_paint_area, below). It stands while ANY
// OF THE TEXT EDITORS stands, on a backend that asks for one, and it
// REPLACES NOTHING: the flag editor keeps painting in the marker lane, a
// dialog editor keeps its line in the modal family's box and its OK and
// Cancel on the controls row, the family's stack then standing on this band
// (modal_box.h). THERE IS NO SECOND TEXT BUFFER — the live editor's own
// text_editor::State is the only text state in the product, exactly as it was
// before this surface existed.
//
// WHY IT IS NOT A UNIVERSAL FIELD. Every key here is a KEY: its press calls the
// backend's synthesize_key and the ORDINARY key path runs unchanged from there
// — GuiInputHandler::on_key, the keyboard-modal gate, route_modal_editor_key,
// each editor's own vocabulary, the undo coalescing, and the core's repeat
// synthesis for a held key. So the editors' grammars, their refusals, their
// commit and cancel bodies and their byte caps are inherited whole rather
// than mirrored, and a new editor gets a working keyboard by existing. COPY
// AND PASTE ARE KEYS LIKE EVERY OTHER (architect 2026-10-29): each
// synthesizes the editors' own chord, Ctrl+C and Ctrl+V, the ctrl carried by
// the key event itself (synthesize_key's `with_ctrl`, GuiInputCore::
// key_event's contract), so a copy with no selection copies nothing and a
// paste takes the editors' one paste body (text_editor::handle_key's chord
// arms, GuiInputHandler::apply_editor_clipboard) — and a held one does what
// a held Ctrl+C or Ctrl+V does, the editor's chord keys being one-shot
// (repeat_eligible's editor arm).
//
// TIMING: KEYS ARE HOTKEYS AND ACT AT THE PRESS (architect: phone muscle
// memory; and the core's repeat needs the press edge). That is the modality
// ruling's own split read straight — ICONS ARE UP, HOTKEYS ARE DOWN
// (GuiInputHandler::on_key) — with these keys on the hotkey side and the
// editors' own
// BUTTONS (a dialog's OK and Cancel) still chrome, still acting at the lift.
// A key held down repeats through the core exactly as a held physical key
// does, on the platform's advertised cadence.
//
// WHAT IT DELIBERATELY HAS NOT GOT: no language switching (one layout), no
// hide act (it leaves with the editor that raised it), no caps lock (shift
// is one-shot), no long-press alternates (an act at the press precludes
// them), no third page and no character past ASCII (no grammar takes one),
// no glyphs (architect 2026-10-29: "no glyphs"; every cap is text, cap_word)
// and no chords — a second finger is the navigation gesture and never
// reaches a key. No Left/Right keys either: backspace and retyping cover a
// field, and a tap in the dialog editor's wrapped line places the caret. No
// `pass` key (architect 2026-10-29: Toggle Inherit covers it).

#include "app_state.h"
// THE SLOT'S OTHER TENANT (2026-08-28): this header reads the folder
// overlay's own rect in waveform_paint_area, the one gate and clip both
// tenants share, so the include runs THIS way — the panel borrows nothing
// from the keyboard since its rows became buttons, and its band is the modal
// family's (modal_box.h, 2026-10-14).
#include "folder_overlay.h"
#include "gui_input.h"
#include "platform.h"
#include "render.h"
#include "viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace onscreen_keyboard {

// -- The layout table -------------------------------------------------------
//
// ONE OWNER FOR EVERY KEY'S IDENTITY: its role, the character it types, and
// how wide it is. The painter reads the cap a key wears from here through the
// derivations below, and the press router reads its keysym and codepoint from
// the SAME `ch`, so a key cannot type one thing and say another.
//
// THE TWO PAGES (architect 2026-10-29, on the sheets kb_f_flags and
// kb_g_symbols_repeated). Rows counted from 0 at the top; widths in units
// (one standard key), every row LEFT-ALIGNED on the block's left edge plus
// its inset — a hardware stagger, no centering of a short row:
//   FLAGS
//     row 0, inset 0:  1 2 3 4 5 6 7 8 9 0 + - *                    (13u)
//     row 1, inset 0:  Tab q w e r t y u i o p                       (11u)
//     row 2, inset 1u: a s d f g h j k l ; :                         (11u)
//     row 3, inset 0:  Shift(1.5) z x c v b n m Backspace(2.5)       (11u)
//     row 4, inset 0:  &123(1.5) Copy(1.5) Paste(1.5) Space(4) .
//                      Esc(1.5) Return(2)                            (13u)
//   SYMBOLS
//     row 0, inset 0:  1 2 3 4 5 6 7 8 9 0 + - *
//     row 1, inset 0:  Tab @ # % & _ = / \ ~ ^
//     row 2, inset 1u: ( ) [ ] { } < > | ; :
//     row 3, inset 0:  Shift ' " , ? ! $ ` Backspace
//     row 4, inset 0:  ABC Copy Paste Space . Esc Return
// NOTHING MOVES BETWEEN THE PAGES (architect 2026-10-29: "I prefer that
// things not move around"): the symbols page is the flags page with its 26
// LETTER SLOTS re-lettered — exactly the 26 printable ASCII symbols the flags
// page lacks — and every other key, the digits, `+ - *`, `;`, `:`, `.`, Tab,
// Shift, Backspace and the bottom row, in the same slot on both (the page
// key's cap alone changing, cap_word). Shift on the symbols page arms
// nothing it can spend (no letter there) and stays for its place. ESC AND
// RETURN STAND LOWER RIGHT (architect 2026-10-29: "that is where my hand
// rests"). Both facts, and the 26, are checked at compile time
// (pages_are_well_formed, below).

// THE PAGE, the keyboard's one layout state beside the shift arm. Declared on
// AppState (this header includes app_state.h) and aliased here.
using Page = AppState::OnscreenKeyboard::Page;
inline constexpr int kPageCount = 2;

enum class Role : uint8_t {
    Character,     // types `ch` — every letter, digit and symbol, space included
    Shift,         // ONE-SHOT: the next Character key is capital, then it clears
    Backspace,     // GuiKeys::BackSpace
    Enter,         // GuiKeys::Return
    Escape,        // GuiKeys::Escape
    Tab,           // GuiKeys::Tab, bare — the prompts' completion key
    SymbolMode,    // `&123` / `ABC`: flips the two pages
    Copy,          // GuiKeys::C carrying ctrl — the editors' Ctrl+C
    Paste,         // GuiKeys::V carrying ctrl — the editors' Ctrl+V
};

// WIDTHS ARE AUTHORED IN QUARTER-UNITS (one standard key, 1u, is 4), so the
// half and quarter widths of the table are whole numbers; the geometry turns
// a quarter into device px (for_each_key). THE BLOCK IS THE WIDEST ROW,
// thirteen units (rows 0 and 4).
inline constexpr int kQuartersPerUnit = 4;
inline constexpr int kBlockQuarters   = 13 * kQuartersPerUnit;
inline constexpr int kRowCount        = 5;
// The most keys a row holds (row 0's thirteen); the index arithmetic below
// reserves this many slots per row so a key's index is a pure function of
// where it sits.
inline constexpr int kMaxRowKeys      = 13;

struct KeyDef {
    Role     role   = Role::Character;
    // Character keys only: the codepoint it types, the LOWERCASE / base form
    // for a letter. Printable ASCII alone (the table's coverage check).
    char32_t ch     = 0;
    uint8_t  span_q = 4;      // width in quarter-units
};

struct Row {
    const KeyDef* keys    = nullptr;
    int           count   = 0;
    int           inset_q = 0;  // the stagger: quarters in from the block's left
};

namespace detail {

// ROW 0, ONE ARRAY FOR BOTH PAGES: the digits and the flag grammar's three
// operators.
inline constexpr KeyDef kRow0[] = {
    {Role::Character, U'1'}, {Role::Character, U'2'}, {Role::Character, U'3'},
    {Role::Character, U'4'}, {Role::Character, U'5'}, {Role::Character, U'6'},
    {Role::Character, U'7'}, {Role::Character, U'8'}, {Role::Character, U'9'},
    {Role::Character, U'0'}, {Role::Character, U'+'}, {Role::Character, U'-'},
    {Role::Character, U'*'},
};

// TAB (architect 2026-08-27, with the project model) leads row 1 on both
// pages: it is what the product's prompts COMPLETE on (the one autocomplete
// model, route_modal_editor_key) and what walks a dialog's focus ring — a
// BARE Tab, no modifier, on the same synthesize_key road as every other key
// here. IT IS NOT THE OPEN PROJECT PICKER'S GLASS ROAD (2026-08-28): that
// picker is field-less and stands in this band, so the keyboard does not
// paint there at all and the gesture is the Open Project button, tap the
// project's row. What the key serves is every prompt a finger can raise —
// the settings editor's value recall and the ring walk on the dialogs that
// publish buttons.
inline constexpr KeyDef kFlagsRow1[] = {
    {Role::Tab},
    {Role::Character, U'q'}, {Role::Character, U'w'}, {Role::Character, U'e'},
    {Role::Character, U'r'}, {Role::Character, U't'}, {Role::Character, U'y'},
    {Role::Character, U'u'}, {Role::Character, U'i'}, {Role::Character, U'o'},
    {Role::Character, U'p'},
};
inline constexpr KeyDef kFlagsRow2[] = {
    {Role::Character, U'a'}, {Role::Character, U's'}, {Role::Character, U'd'},
    {Role::Character, U'f'}, {Role::Character, U'g'}, {Role::Character, U'h'},
    {Role::Character, U'j'}, {Role::Character, U'k'}, {Role::Character, U'l'},
    {Role::Character, U';'}, {Role::Character, U':'},
};
inline constexpr KeyDef kFlagsRow3[] = {
    {Role::Shift, 0, 6},
    {Role::Character, U'z'}, {Role::Character, U'x'}, {Role::Character, U'c'},
    {Role::Character, U'v'}, {Role::Character, U'b'}, {Role::Character, U'n'},
    {Role::Character, U'm'},
    {Role::Backspace, 0, 10},
};

// THE SYMBOLS PAGE'S THREE RE-LETTERED ROWS, slot for slot the three above.
inline constexpr KeyDef kSymbolsRow1[] = {
    {Role::Tab},
    {Role::Character, U'@'}, {Role::Character, U'#'}, {Role::Character, U'%'},
    {Role::Character, U'&'}, {Role::Character, U'_'}, {Role::Character, U'='},
    {Role::Character, U'/'}, {Role::Character, U'\\'}, {Role::Character, U'~'},
    {Role::Character, U'^'},
};
inline constexpr KeyDef kSymbolsRow2[] = {
    {Role::Character, U'('}, {Role::Character, U')'}, {Role::Character, U'['},
    {Role::Character, U']'}, {Role::Character, U'{'}, {Role::Character, U'}'},
    {Role::Character, U'<'}, {Role::Character, U'>'}, {Role::Character, U'|'},
    {Role::Character, U';'}, {Role::Character, U':'},
};
inline constexpr KeyDef kSymbolsRow3[] = {
    {Role::Shift, 0, 6},
    {Role::Character, U'\''}, {Role::Character, U'"'}, {Role::Character, U','},
    {Role::Character, U'?'}, {Role::Character, U'!'}, {Role::Character, U'$'},
    {Role::Character, U'`'},
    {Role::Backspace, 0, 10},
};

// THE BOTTOM ROW, ONE ARRAY FOR BOTH PAGES: the page key, Copy, Paste,
// Space, `.`, Esc and Return — 6 + 6 + 6 + 16 + 4 + 6 + 8 = 52, the block.
inline constexpr KeyDef kBottomRow[] = {
    {Role::SymbolMode, 0,     6},
    {Role::Copy,       0,     6},
    {Role::Paste,      0,     6},
    {Role::Character,  U' ', 16},
    {Role::Character,  U'.',  4},
    {Role::Escape,     0,     6},
    {Role::Enter,      0,     8},
};

template <int N>
constexpr Row make_row(const KeyDef (&a)[N], int inset_q = 0) {
    return Row{a, N, inset_q};
}

inline constexpr Row kPages[kPageCount][kRowCount] = {
    {make_row(kRow0), make_row(kFlagsRow1), make_row(kFlagsRow2, 4),
     make_row(kFlagsRow3), make_row(kBottomRow)},
    {make_row(kRow0), make_row(kSymbolsRow1), make_row(kSymbolsRow2, 4),
     make_row(kSymbolsRow3), make_row(kBottomRow)},
};

constexpr bool is_letter(char32_t c) { return c >= U'a' && c <= U'z'; }

// THE TABLE'S SHAPE FACTS, checked at compile time: every row fits the index
// arithmetic's slot reservation and the block, the widest row IS the block;
// NOTHING MOVES BETWEEN THE PAGES — every slot has one role and one width on
// both, and a character slot types the same character on both unless it is
// a letter slot; and THE SYMBOLS PAGE'S LETTER SLOTS HOLD EXACTLY THE
// PRINTABLE ASCII THE FLAGS PAGE LACKS — across the flags page's characters
// and the symbols page's letter slots every printable ASCII character but
// the capitals (the shift arm's) appears exactly once, and space with them.
constexpr bool pages_are_well_formed() {
    int widest = 0;
    for (int p = 0; p < kPageCount; ++p) {
        for (int r = 0; r < kRowCount; ++r) {
            const Row& row = kPages[p][r];
            if (row.count > kMaxRowKeys) return false;
            int span = row.inset_q;
            for (int i = 0; i < row.count; ++i) span += row.keys[i].span_q;
            if (span > kBlockQuarters) return false;
            widest = std::max(widest, span);
        }
    }
    if (widest != kBlockQuarters) return false;
    int seen[128] = {};
    for (int r = 0; r < kRowCount; ++r) {
        const Row& f = kPages[0][r];
        const Row& s = kPages[1][r];
        if (f.count != s.count || f.inset_q != s.inset_q) return false;
        for (int i = 0; i < f.count; ++i) {
            const KeyDef& a = f.keys[i];
            const KeyDef& b = s.keys[i];
            if (a.role != b.role || a.span_q != b.span_q) return false;
            if (a.role != Role::Character) continue;
            if (a.ch >= 128 || b.ch >= 128) return false;
            if (is_letter(a.ch)) {
                if (is_letter(b.ch)) return false;
                ++seen[b.ch];
            } else if (a.ch != b.ch) {
                return false;
            }
            ++seen[a.ch];
        }
    }
    for (int c = 0x20; c <= 0x7e; ++c) {
        const bool capital = c >= 'A' && c <= 'Z';
        if (seen[c] != (capital ? 0 : 1)) return false;
    }
    return true;
}
static_assert(pages_are_well_formed(),
              "a keyboard row overflows its slots or the block, the pages "
              "move a key, or the symbols page misses printable ASCII");

constexpr int page_number(Page page) { return static_cast<int>(page); }

} // namespace detail

inline const Row& row_of(Page page, int row) {
    return detail::kPages[detail::page_number(page)][row];
}

// ZERO IS THE CORE'S "NO STABLE CODE" SENTINEL and this table may not produce
// it. GuiInputCore compares an incoming stable code against two fields that
// rest at 0 when nothing is held — the armed repeat's code and the synthesized-
// left hold's — so a key whose code were 0 would match "nothing" on its own
// release and be taken for the end of a hold it never started. The Wayland
// backend never met this because an xkb keycode is 8 or more by construction;
// this table's codes are its own small integers, so the base is stated here
// rather than left to luck. THIS IS THE PRODUCT'S ONLY SYNTHESIZER since
// 2026-09-12: the render player's car buttons took a base of their own
// (kCarStableCodeBase = 1000, above this table's ceiling) while each of them
// pressed one of the player's keys, and that road went whole when the car
// became an interface of its own and every command a direct act.
inline constexpr uint32_t kStableCodeBase = 1;

// A KEY'S STABLE PER-KEY IDENTITY, which is what the core's repeat cancel and
// the synthesized-hold end compare against (contract at GuiInputCore::
// key_event). It is the key's PLACE in this table — page, row, column, off the
// base above — and deliberately not the keysym: the pages put different
// characters on one slot, so only the place is unique per key. Codes run from
// kStableCodeBase to kStableCodeBase + kPageCount*kRowCount*kMaxRowKeys - 1,
// which is 1..130.
inline constexpr uint32_t key_index(Page page, int row, int col) {
    return kStableCodeBase + static_cast<uint32_t>(
        ((detail::page_number(page) * kRowCount) + row) * kMaxRowKeys + col);
}

// THE PAGE A KEY INDEX BELONGS TO — the inverse of the page term above, and
// the one place that reads it back. Its consumer is the RELEASE, which must
// damage the key the finger pressed even when that key's own act (the page
// key) has moved the live page out from under it; asking the live page
// there would look the key up on a page it is not on and damage nothing.
inline constexpr Page page_of_key_index(int index) {
    return static_cast<Page>(
        (static_cast<uint32_t>(index) - kStableCodeBase) /
        static_cast<uint32_t>(kRowCount * kMaxRowKeys));
}

// THE HELD FACE IS A SLOT, NOT AN INDEX (architect 2026-10-29 night, on the
// glass: the page key never sank to its second depth). Two key indices name
// the same SLOT when they differ in the page term alone — the same row and
// column, which on these pages is the same rect, role and width, since
// nothing moves between them (pages_are_well_formed pins it). The painter's
// held test is this one: THE PAGE KEY IS THE ONE KEY WHOSE OWN PRESS MOVES
// THE LIVE PAGE OUT FROM UNDER ITS INDEX before the paint, so an index test
// against the new page's walk finds no held key and the page key paints its
// checked or rest depth under the finger; the held face must follow the
// finger (paint_key's HELD row, cool_edit_paint.h), and the finger is on the
// slot.
// A negative index — `pressed_key`'s "nothing held" — is on no slot.
inline constexpr bool same_slot(int a, int b) {
    constexpr int base  = static_cast<int>(kStableCodeBase);
    constexpr int slots = kRowCount * kMaxRowKeys;
    if (a < base || b < base) return false;
    return (a - base) % slots == (b - base) % slots;
}

// -- The derivations off the table ------------------------------------------
//
// THE CASE IS A DERIVATION AND NOT A SECOND TABLE (the state-axis rule for a
// new painted surface): the table holds the base character once, and the shift
// lamp turns it into the cap the key WEARS and the codepoint it TYPES through
// this one function, so the two can never disagree about what a shifted key is.
// Non-letters are unmoved — this keyboard has no shifted punctuation, the
// symbols page being where the rest lives, and every letter is on the flags
// page, so the arm acts there alone. THAT PROPERTY IS ALSO THE
// ONE-SHOT ARM'S TEST: the press router spends the arm exactly where this
// function moved the character, so "the arm is spent by the next LETTER" needs
// no second list of which keys have a capital form.
inline constexpr char32_t shifted_char(char32_t base, bool shift_armed) {
    if (!shift_armed) return base;
    if (base >= U'a' && base <= U'z') return base - U'a' + U'A';
    return base;
}

// THE KEYSYM OF A CHARACTER KEY. GuiKey is the universal keysym numbering, in
// which every printable ASCII character IS its own code point — `a` is 0x61,
// `$` is 0x24, space is 0x20 — and GuiKey is ASCII CASE-FOLDED besides (the
// backend's contract, at GuiInputCore::key_event), so the LOWERCASE base is the
// keysym for a letter in both cases. That identity is why the punctuation
// this keyboard types needs no named constants in GuiKeys: a name earns its
// place by being BOUND somewhere, and nothing in the dispatch binds `$`, `(`
// or `_` — they exist only as characters an editor inserts, and what the
// editor inserts is the CODEPOINT the press carries beside the keysym (the
// printable branch of text_editor::handle_key, into replace_selection — the
// road a hardware keyboard's character takes on the laptop). (The numbering
// past ASCII — Latin-1 its own code points, everything else 0x01000000 + the
// code point — stays here for the walk's completeness; the table types none
// of it since 2026-10-29.)
inline constexpr GuiKey keysym_of(char32_t base) {
    return base < 0x100 ? static_cast<GuiKey>(base)
                        : static_cast<GuiKey>(0x01000000u | base);
}

// WHAT A KEY WEARS, AND IT IS TEXT AND NOTHING ELSE (architect 2026-08-27, on
// glass; "no glyphs", 2026-10-29): every cap is a WORD or a character on the
// ONE sans face in the keys' own size (gui_font.h's GuiFace::Keys, the
// `osk_font_px` tuning key's px at 300 %, 24 by default, architect
// 2026-10-29 night; "C24", architect 2026-10-24; "the glyph size is good",
// 2026-10-29), shaped through the one chokepoint like every other label.
//
// A FUNCTION KEY WEARS ITS NAME (planner 2026-09-01, under the capitalization
// sweep's universal-rules spine; architect 2026-10-29, "Backspace and Return
// spelled out"): Shift, Backspace, Return, Esc, Tab, Space, Copy, Paste — in
// the product's one key spelling, which is Qt's and so kdenlive's
// (spell_chord's head, gui_input.h), Title case — and the PAGE KEY names the
// page it goes TO, `&123` on the flags page and `ABC` on the symbols page.
// This function is the ONE OWNER of the words; a Character key other than
// space answers nullptr and the painter spells it out of the table's own
// `ch` through the one case derivation above.
//
// THE KEY IS WIDE ENOUGH FOR ITS WORD (architect 2026-10-29): a function
// key's width is authored to hold its word in the keys face, measured
// (Liberation Sans, HarfBuzz's unhinted advances, text_shape's road) against
// the key's face inside its one-device-px outline at the defaults
// (osk_key_w 48, osk_key_h 60, osk_font_px 24): at 300 % "Backspace" 117 px
// in the 2.5u key's 127, "Return" 72 in the 2u key's 100, "Space" 68 in the
// 4u key's 208, "Paste" 61, "&123" and "Copy" 56, "ABC" 49, "Shift" 48 and
// "Esc" 40 in the 1.5u key's 73, and "Tab" 39 in the 1u key's 46; at 200 %
// (em 16) and 150 % (em 12) every word fits as well, the key and the face
// scaling together.
// A WORD THAT FAILS ITS KEY TAKES ITS SHORTENED FORM (architect 2026-10-29:
// "ok to use smaller font and/or shortened forms for long words, but with
// sizing that might not be needed"): a tuning of osk_key_w below the
// default or of osk_font_px above it (the cap size a tuning key since
// 2026-10-29 night) can make a word fail, and the fallback is the
// keyboard's own abbreviation in capitals, as keyboards print it, Backspace
// -> BKSP (64 px at 300 % at em 24) and Return -> RET (48) — measured at
// paint against the key's face in the live font, never a second face
// (cap_short_word; the painter's measure). At 300 % with osk_key_h 60 and
// osk_font_px 24, Backspace falls back below osk_key_w 45 and Return below
// 34; at the default box, Backspace falls back from osk_font_px 27, Return
// from 34, and BKSP itself overhangs its key from 48, the key's ceiling
// (device_config.h's grammar). No other word has a short form ruled:
// "Tab", the first to outgrow its key, overhangs it below osk_key_w 41, and
// at the default box "Tab" and "Paste" overhang theirs from osk_font_px 29.
//
// SHIFT'S LAMP IS THE FACE, NOT THE CAP. The word is "Shift" armed or resting;
// what says the arm is the key's ARMED FACE — the key DOWN one device px, its
// checked depth (paint_key, 2026-10-23), which this key and the page key
// (while the symbols page stands) wear off their lamps — and the letter caps
// themselves, every one of which turns capital while the arm stands.
inline const char* cap_word(const KeyDef& k, Page page) {
    switch (k.role) {
        case Role::Shift:       return "Shift";
        case Role::Backspace:   return "Backspace";
        case Role::Enter:       return "Return";
        case Role::Escape:      return "Esc";
        case Role::Tab:         return "Tab";
        case Role::SymbolMode:  return page == Page::Flags ? "&123" : "ABC";
        case Role::Copy:        return "Copy";
        case Role::Paste:       return "Paste";
        case Role::Character:   return k.ch == U' ' ? "Space" : nullptr;
    }
    return nullptr;
}

// THE SHORTENED FORM a word falls back to where it fails its key (cap_word's
// rule), or nullptr for a key that has none.
inline const char* cap_short_word(const KeyDef& k) {
    switch (k.role) {
        case Role::Backspace: return "BKSP";
        case Role::Enter:     return "RET";
        default:              return nullptr;
    }
}

// THE PAGE KEY'S ACT, one owner for where it goes: the flags page to the
// symbols page and back.
inline constexpr Page page_after(Role role, Page page) {
    if (role == Role::SymbolMode)
        return page == Page::Flags ? Page::Symbols : Page::Flags;
    return page;
}

// -- The authored geometry --------------------------------------------------
//
// THE KEY BOX IS AUTHORED AT 300 % AND SCALED (architect 2026-10-29, the
// compact keyboard), like every other skin length (skin_px, rounded at the
// element): its WIDTH, one unit's key, and its HEIGHT, every row's, the two
// TUNING DEVICE KEYS `osk_key_w` and `osk_key_h` (render.h's seat;
// device_config.h's grammar, [24, 160] each), defaults 48 and 60, the FLAGS
// sheet's numbers. THE GAP between adjacent keys, both axes, is A TENTH OF
// THE HEIGHT, rounded at the element; THE PITCH is the key plus the gap; a
// key of n units is n pitches less one gap. At the defaults on the tablet
// (300 %): the key 48 x 60, the gap 6, the pitch 54, so 1u 48, 1.5u 75, 2u
// 102, 2.5u 129 and 4u 210 device px; at the laptop's 200 % 32 x 40, the
// gap 4, the pitch 36. THE KEYBOARD'S KEYS DO NOT FOLLOW THE PROGRAM KEY'S
// `key_height` (architect 2026-10-29; render.h's icon_case_h_px): their
// height is `osk_key_h` alone. (The window-derived pitch — Plasma's ten keys
// across the full width, the one unscaled dimension the product had — is
// retired 2026-10-29, git history.)
inline int key_w_px()     { return skin_px(osk_key_w_at300(), 1); }
inline int key_h_px()     { return skin_px(osk_key_h_at300(), 1); }
inline int key_gap_px()   { return skin_px(osk_key_h_at300() * 0.1, 1); }
inline int key_pitch_px() { return key_w_px() + key_gap_px(); }

// A SLOT'S LEFT EDGE, `q` quarters into the block, off the block's left edge:
// rounded at the element, so a key's width is the rounded span less one gap
// and the gap between neighbors is the gap exactly.
inline int quarter_offset_px(int q) {
    return static_cast<int>(std::nearbyint(
        q * static_cast<double>(key_pitch_px()) / kQuartersPerUnit));
}

// THE BLOCK'S WIDTH: the widest row, thirteen units — thirteen pitches less
// one gap, 696 device px at the defaults on the tablet.
inline int block_w_px() {
    return quarter_offset_px(kBlockQuarters) - key_gap_px();
}

// THE EDGE PAD — THE SAME ON ALL FOUR SIDES (architect 2026-10-10, the
// overlay's mocks: "the pad the same on all four sides"; kept 2026-10-29):
// 14 device px at 300 %, the band's top to the first key row and the last
// key row to the window's foot (across, the block is centered and the
// ground beside it is what the window's width leaves). The same 14 at 300 %,
// 9 at 200 % and 6 at 138 % it measured when it was the full-width walk's
// leftover read back.
inline constexpr double kEdgePadAt300 = 14.0;
inline int edge_pad_px() { return skin_px(kEdgePadAt300, 1); }

// The band's whole height: the edge pad, five key rows and the four gaps
// between them, and the edge pad — at the defaults 14 + 324 + 14 = 352
// device rows at 300 %, 6 + 152 + 6 = 164 at the laptop's 138 % (the key 28,
// the gap 3). THE BAND CARRIES NO LINE (architect 2026-10-14: the raised
// Hilight line the taskbar overlay laid on its top, 2026-10-10 to 2026-10-14,
// is gone with the overlay; the placement block below).
inline int surface_height_px() {
    return 2 * edge_pad_px() + kRowCount * key_h_px() +
           (kRowCount - 1) * key_gap_px();
}

// -- Standing ----------------------------------------------------------------

// DOES THE SURFACE STAND? Two terms: the PLATFORM must want a painted
// keyboard (false forever on Wayland — the ruling is at that backend's
// wants_onscreen_keyboard), and one of the editors must own the keyboard.
// EVERY paint site and EVERY hit site in the product asks this and nothing
// else, which is what makes the laptop build's behaviour identical by
// construction rather than by care.
//
// The editor term is text_editor_session() (app_state.h) rather than
// GuiInputHandler::keyboard_modal_editor_active because the painter has no
// input handler to ask; the two are the same set by construction — that
// predicate delegates to any_text_editor_active, which is exactly the editors
// this session id is taken from.
//
// THE OVERLAY AND THE KEYBOARD NEVER BOTH STAND, AND NO THIRD TERM SAYS SO
// (2026-08-28): the folder overlay REPLACES the keyboard in this band
// (architect, "neither use needs typing"), and for one afternoon that day
// this predicate carried `!folder_overlay::stands(a)` as a third term — its
// producer being the Open project prompt, whose text editor stood UNDER the
// picker's band. The prompt lost its field and the pickers became a
// modal owner that is NOT an editor, so the term lost its producer and was
// deleted (a gate term exists iff a producer exists — the type rule
// applied to a gate). THE EXCLUSION IS STRUCTURAL NOW: the overlay
// stands only under the render player or a picker, neither of which is a
// text editor; each opener refuses under every editor and each router
// consumes every editor opener; each veil consumes every pointer press that
// could raise one (the flag editor's double-click; the roster is dead under
// both); and the touch region begin refuses under
// both.
// So the second term is false whenever the overlay stands, and this
// predicate cannot answer true over the band without a producer this record
// would have to name.
//
// (A THIRD TERM, the settings CHOICE editor's, stood 2026-10-07 to
// 2026-10-14: a choice row had nothing to type, so the band did not rise for
// it. The choice editor is removed — the color rows are line editors like
// every other row, architect 2026-10-14 — and the term with it.)
inline bool stands(const AppState& a, const GuiPlatform& gui) {
    return gui.wants_onscreen_keyboard() && a.text_editor_session() != 0;
}

// -- The keyboard's placement (architect 2026-10-14) -------------------------
//
// THE KEYBOARD STANDS ACROSS THE PROGRAM'S LANES (architect 2026-10-14, on
// the settings-keyboard mock): its band runs
// across the program lanes — the band's width the lanes' (bottom_row_area's
// x and width, the window's since 2026-10-21, when the program frame's
// columns and bottom row went) — and stands DIRECTLY ON THE WINDOW'S FOOT;
// THE BLOCK OF KEYS IS CENTERED ACROSS IT (architect 2026-10-29, the compact
// keyboard: "small, centered, like a phone"), the edge pad above and below
// it (edge_pad_px), the band's ground beside it and nothing else there, and
// NO LINE OF ITS OWN. ITS GROUND IS THE BRUSHED GROUND, ITS KEYS THE HI-FI
// SKIN'S KEYS WITH NO LED, THE CAP PRINTED ON THE FACE (architect
// 2026-10-23) —
// paint_onscreen_keyboard,
// materials.h's GROUND and KEY. Under a dialog editor the modal
// family's stack stands on it (modal_box.h's head: the box shortened to fit
// and the controls row lifted onto the band, the space between the row's
// cases and the first key row the strip pad plus this band's edge pad,
// 2026-10-22; the band shorter since 2026-10-29, so the box taller); under
// the flag editor, the one editor with no dialog, it
// stands alone over row 8 and the canvas's lower part, which keep their
// geometry and are covered.
// (THE TASKBARS, 2026-10-10 to 2026-10-14 — architect, "they're analogous to
// the taskbar … they sit on top of the design": the keyboard and the dialog
// laid over the window's foot at its whole width, over the frame's columns
// and its bottom row, one raised Hilight line on top, the dialog standing
// directly on the keyboard with one pad between — ARE RETIRED, "we should
// stop doing that". The dialog's surface is the modal family's box and
// controls row; its band, its pad and its line went with the overlay.)
//   ALONE (the flag editor), AT THE DEFAULTS (osk_key_w 48, osk_key_h 60):
//     at 300 % on 2304 x 1440 the band 1088–1439 (14 + 324 + 14) across x
//     0..2303, its first key row's top 1102, the block of keys x 804..1499
//     (696 wide, 804 clear each side); at the laptop's 138 % on 1920 x 1080
//     the band 916–1079 (6 + 152 + 6) across x 0..1919. Row 8 is under it
//     and unreachable: the keyboard's press claim takes the band, the ground
//     beside the block included (claim_rect).
//   UNDER A DIALOG EDITOR: the same band, the modal's stack on it (the
//     arithmetic at modal_box.h's head; at 300 %, the strip pad's 13 W,
//     key_height 33 and the keyboard's defaults the controls row 977–1087,
//     its cases 1016–1048, the box 144–976, its top under the band's rows
//     0–143 with the 72-row logo lane, 2026-10-24).
// ON SCREEN IS AS PAINTED: nothing under the band answers a press, a cursor
// zone or the touch pan zone — the waveform's painted rect stops at the
// band's top (waveform_paint_area below, which point_on_nav_surface reads),
// row 8 yields its buttons under it (paint_bottom_strip) and the keyboard's
// claim is opaque. The keys and the band's ground are paint_onscreen_keyboard's.
// Geometry only here: every rect below is a fact about geometry and asks no
// standing unless its name or its contract says so (modal_keyboard_h_px,
// standing_rect).

// THE KEYBOARD'S BAND: the program lanes' x and width (bottom_row_area's,
// the window's), standing on the window's foot (row 8's lane's foot), its
// height surface_height_px, the block of keys centered in it
// (for_each_key). It OVERLAYS what stands there: nothing in the
// vertical stack moved to make room, and the waveform simply is not painted
// where this paints. The one zero rect is the degenerate one: a window with
// no width, or a height that scales to nothing.
inline GuiRect surface_rect(const AppState& a) {
    const GuiRect lane   = bottom_row_area(a);
    const int     h      = surface_height_px();
    if (lane.w <= 0 || h <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{lane.x, lane.y + lane.h - h, lane.w, h};
}

// THE HEIGHT THE MODAL'S STACK STANDS ON (modal_box.h's `keyboard_h`): the
// band's height while the keyboard stands, 0 otherwise. Every modal rect a
// painter or a hit site reads under a standing keyboard takes this.
inline int modal_keyboard_h_px(const AppState& a, const GuiPlatform& gui) {
    return stands(a, gui) ? surface_height_px() : 0;
}

// THE KEYBOARD'S CLAIMED SURFACE: its band, whole — the pad round the keys
// and the ground beside the centered block included, so a press between
// keys, on the pad or beside the block is the keyboard's consumed nothing
// rather than a reach into what lies under the band. (Until
// 2026-10-14 the overlay's line above the band joined it when no dialog
// stood on the keyboard.)
inline GuiRect claim_rect(const AppState& a) {
    return surface_rect(a);
}

// THE STANDING KEYBOARD'S BAND, or a zero rect when it does not stand — the
// wheel's swallow (GuiInputHandler::wheel_context).
inline GuiRect standing_rect(const AppState& a, const GuiPlatform& gui) {
    return stands(a, gui) ? surface_rect(a) : GuiRect{0, 0, 0, 0};
}

// THE SLOT'S DAMAGE RECT, the slot AT ITS TALLEST: the whole area the modal
// family takes — the folder overlay's box, its margins, and the
// controls row under them (modal_area_rect, modal_box.h, 2026-10-14), which
// contains the stack's lifted form too (a dialog editor's on the keyboard,
// from the box's top to the lifted controls row) — united with the keyboard's band,
// so one rect contains every tenant's pixels in every form.
// THE SHOW/HIDE COMPARATOR TAKES IT (reconcile_slot_standing, below): those
// two edges damage a band whose tenant is arriving or has already gone, so
// the rect cannot be either
// tenant's own — on the hide the departed surface's pixels are exactly what
// has to be erased, and on the show the arriving one's whole band has to be
// covered. A damage INSIDE a standing band takes that tenant's own rect
// instead.
inline GuiRect slot_damage_rect(const AppState& a) {
    const GuiRect band = modal_area_rect(a, /*keyboard_h=*/0);
    const GuiRect kb   = surface_rect(a);
    if (kb.w <= 0 || kb.h <= 0) return band;
    if (band.w <= 0 || band.h <= 0) return kb;
    return union_rect(band, kb);
}

// -- The session-change owner, and the waveform's painted rect --------------

// THE SESSION-CHANGE OWNER, and the ONE writer of the transient state's reset.
// THE TWO LAMPS BELONG TO THE EDIT THEY WERE SET IN, so a close, a reopen or a
// RETARGET of the live flag editor must clear them — and the PIXELS MUST SAY SO
// BEFORE THE NEXT PRESS IS ROUTED, because the lamps decide both which key is
// under the finger (the page) and what that key types (the arm): a surface
// left describing one key while the press dispatches another is the defect this
// owner exists to make impossible.
//
// So the reset is a WRITE THAT RUNS ON ITS OWN, never a reconciliation a reader
// happens to discover. Its two callers are:
//   * the PRE-PAINT HOOK (main.cpp), which runs ahead of EVERY frame either
//     backend paints — before the damage list is read and free to add to it,
//     which is what a painter may not do. Nothing reaches the glass without
//     passing here first, so the first frame of a newly opened editor cannot
//     show the previous one's lamps even when the release that opened it is
//     followed straight by a paint with no tick between them;
//   * the HEAD OF THE PRESS ROUTER (input_pointer.cpp), which covers a close
//     and a reopen completed inside ONE DRAINED INPUT BATCH, with no paint
//     between them — the hit test must not run against the old session's
//     lamps either.
// THE PAINTER DOES NOT CALL IT: a painter that reconciled would discover the
// change only on a frame whose exposure happened to reach this band, and it
// would be declaring damage from inside a frame (the paint loop may not — the
// contract is at GuiPlatform::paint_one_frame). THE TICK DOES NOT CALL IT
// EITHER: a tick with no paint behind it has nothing to correct on screen, and
// a tick that is followed by one is already covered by the pre-paint call.
//
// IT DAMAGES THE WHOLE BAND because both lamps are whole-surface facts: the
// arm moves every letter cap's case and the page re-letters every letter
// slot and the page key's cap.
//
// `pressed_key` IS DELIBERATELY NOT CLEARED HERE. It is a fact about the
// FINGER, not about the edit, and THE KEY-UP IT OWES THE CORE IS OWED EXACTLY
// IN THIS CASE: the press that moved the session is the Enter or the Esc that
// closed the editor under itself, and dropping its release would leave the
// core's repeat arm standing on a key nothing will ever release (the contract
// is at GuiInputCore::key_event). The release path — and the touch hard-end
// beside it — is its one clearer.
//
// IT IS A NO-OP WHEREVER THE SURFACE DOES NOT STAND, the laptop forever
// included: one platform query, no write, no damage.
inline void reconcile_session(AppState& a, const GuiPlatform& gui,
                              Viewport& viewport) {
    if (!stands(a, gui)) return;
    const uint64_t live = a.text_editor_session();
    if (a.onscreen_keyboard.lamp_session == live) return;
    a.onscreen_keyboard.lamp_session = live;
    a.onscreen_keyboard.shift_armed  = false;
    a.onscreen_keyboard.page         = Page::Flags;
    viewport.invalidate_rect(surface_rect(a));
}

// THE SLOT'S SHOW AND HIDE (2026-08-27 for the keyboard, 2026-08-28 for the
// slot's two tenants), the roster comparator's own mechanism applied to a
// whole surface: the keyboard appears and disappears with the editors' open
// and close, the folder overlay with the render player and the picker, and
// those routes damage the marker lane, the bottom row or the dialog — never
// the band this slot paints in. So the live answer (either tenant standing)
// is compared against the AS-PAINTED bit, which the slot's paint dispatch
// refreshes only on a frame whose rect FULLY COVERS the band
// (AppState::keyboard_slot_painted_standing, GuiPaintHandler::
// paint_keyboard_slot), and the drift is paid here. THIS IS THE ONE PLACE
// THE KEYBOARD'S HIDE IS PAID, whatever closed its editor: standing is
// derived (stands, above), so every close road — the dialog editors' OK,
// Cancel, Esc and Return, the flag editor's — moves this comparison and
// nothing else has to remember the band.
//
// THE DAMAGE IS THE SLOT'S BAND AT ITS TALLEST (slot_damage_rect), and only
// that: on the HIDE the departed surface no longer stands, so there is
// nothing to ask its height of, and the one rect contains every tenant's
// pixels in every form — the modal family's whole area (the folder overlay's
// box and the controls row under it, and a dialog editor's stack lifted onto
// the keyboard) and the keyboard's band — and with them the waveform they
// covered (the box spans the waveform area whole).
//
// TWO CALLERS (2026-10-13, the tablet's stale keyboard): the PRE-PAINT HOOK
// (main.cpp), ahead of every frame either backend paints, and the TICK. The
// pre-paint call is the fix: a pointer release or a key closes an editor and
// the Android pump paints in the SAME pass, before its next tick, so a
// tick-only comparator let the first frame after a hide paint without the
// band. On the cairo road that cost one stale frame; under the GL CANVAS it
// stranded the keyboard for good — that frame's canvas clip grew back over
// the band with no damage there, so the band's old texels inside the new
// clip were never uploaded (the upload rule skips the clip unless it moved
// on that frame, GuiPlatform::present), and the GPU showed the departed keys
// over a canvas that answered the press. Paid at pre-paint, the clip moves
// on the very frame whose damage covers the band, so the whole rect goes up
// cleared. The tick call stays for a standing change that declares no damage
// of its own: a frame paints only with damage, so the pre-paint would never
// run for it.
//
// One platform query and a bit compare per call with the slot down, which
// on the laptop is its permanent state.
inline void reconcile_slot_standing(AppState& a, const GuiPlatform& gui,
                                    Viewport& viewport) {
    const bool live = stands(a, gui) || folder_overlay::stands(a);
    if (live == a.keyboard_slot_painted_standing) return;
    viewport.invalidate_rect(slot_damage_rect(a));
}

// THE WAVEFORM'S PAINTED RECT — waveform_area minus what stands over it: THE
// MODAL FAMILY'S BOX (the folder overlay's, a prompt's or a dialog editor's —
// modal_box_stands, modal_box.h) or THE KEYBOARD alone (the flag editor's) —
// and the ONE OWNER of the rule that THE WAVEFORM IS NOT PAINTED WHERE THOSE
// PAINT. IT SUBTRACTS THE STANDING SURFACE'S OWN RECT, which is a real fork
// rather than a formality: the box is the modal family's panel every time it
// stands (the view bar's top row down since 2026-10-14, modal_box.h, whatever
// its content) and the keyboard's is its band, so a rect borrowed from the
// other would either hide waveform nothing paints over or leave the panel
// painting where the waveform still runs.
//
// Every one of those grounds is fully opaque and every waveform pass runs
// BEFORE them (the authoritative paint order, paint_handler.cpp),
// so a waveform pixel under the band is work whose result is thrown away in
// the same frame: a narrow scanner damage column crossing the band would
// otherwise pay the plate blit and every vertical over the
// band's whole height, at the panel's own tick rate. ONE GATE, ONE CLIP for
// every surface.
//
// IT IS THE EXPOSURE GATE, THE CLIP AND THE NAVIGATION SURFACE'S EXTENT,
// NEVER A COORDINATE INPUT: the column mapping keeps reading waveform_area
// itself (the displayed basis, the strictly-as-painted rule at app_state.h),
// while the press, the cursor's Pan / Zoom zone and the touch pan zone ask
// this rect where the waveform ENDS (point_on_nav_surface, input_pointer.cpp,
// 2026-10-10) — so a press never answers where no waveform is painted, and
// paint and hit cannot drift. This rect says WHERE THE PIXELS MAY LAND, and
// so where the waveform takes a press.
//
// Both surfaces span the waveform's columns and run down past its foot, so
// what they hide off the waveform is always a BOTTOM SLICE and the answer is
// a rect. THE KEYBOARD always overlaps the waveform (since the waveform
// became the lanes' whole leftover, 2026-10-07), and the waveform stops at
// its band's top; THE BOX swallows the waveform whole and answers a
// ZERO-HEIGHT rect, and it is the CLIP rather than the gate that makes that
// case paint nothing (rects_intersect can still answer true for an empty
// rect an exposure straddles — it compares edges, not areas). The box's band
// starts ABOVE the waveform (the view bar's top row since 2026-10-14; the
// panel's ceiling first moved above the waveform 2026-09-02), so every
// waveform pass is clipped out whole while it stands, and the LANES it also
// covers (the view bar, the ruler, the marker lane, the canvas's frame and
// row 8; the icon row stands above the box, every icon greyed under the folder overlay but the
// ones it leaves live) are not spared this way — their painters
// run and the panel covers them, because they publish the roster's hit rects
// (the record is at the paint-order block, paint_handler.cpp).
inline GuiRect waveform_paint_area(const AppState& a, const GuiPlatform& gui) {
    const GuiRect area  = waveform_area(a);
    int           cut_y = area.y + area.h;
    if (modal_box_stands(a)) {
        cut_y = std::min(cut_y,
                         modal_panel_rect(a, modal_keyboard_h_px(a, gui)).y);
    } else if (stands(a, gui)) {
        const GuiRect kb = surface_rect(a);
        if (kb.w > 0 && kb.h > 0) cut_y = std::min(cut_y, kb.y);
    }
    const int hidden = (area.y + area.h) - cut_y;
    if (hidden <= 0) return area;
    GuiRect painted = area;
    painted.h = hidden >= area.h ? 0 : area.h - hidden;
    return painted;
}

// -- The ONE walk over the keys ----------------------------------------------

// THE ONE WALK OVER THE KEYS, and the reason paint and hit cannot drift: both
// go through it. `fn(index, def, rect)` is called for every key of `page` in
// painted order, with `rect` the key's PAINTED box.
//
// ACROSS (architect 2026-10-29, the compact keyboard): THE BLOCK — the
// widest row, block_w_px — is CENTERED in the band (the leftover halved,
// the odd pixel to the right), and EVERY ROW STARTS AT THE BLOCK'S LEFT EDGE
// PLUS ITS INSET (the stagger, Row::inset_q; no centering of a short row —
// the mock's shape), each key `q` quarters in at quarter_offset_px(q), its
// width the next slot's offset less one gap. DOWN, the first row starts the
// edge pad under the band's top and each next one a key and a gap lower.
template <class Fn>
inline void for_each_key(const AppState& a, Page page, Fn&& fn) {
    const GuiRect surf = surface_rect(a);
    if (surf.w <= 0 || surf.h <= 0) return;

    const int gap     = key_gap_px();
    const int key_h   = key_h_px();
    const int block_x = surf.x + (surf.w - block_w_px()) / 2;

    int y = surf.y + edge_pad_px();
    for (int r = 0; r < kRowCount; ++r) {
        const Row& row = row_of(page, r);
        int q = row.inset_q;
        for (int i = 0; i < row.count; ++i) {
            const KeyDef& k  = row.keys[i];
            const int     x0 = quarter_offset_px(q);
            const int     kw = quarter_offset_px(q + k.span_q) - gap - x0;
            if (kw > 0) {
                fn(key_index(page, r, i), k,
                   GuiRect{block_x + x0, y, kw, key_h});
            }
            q += k.span_q;
        }
        y += key_h + gap;
    }
}

// THE KEY UNDER A POINT, or -1. A press inside the surface but off every key —
// the gaps, the margins — answers -1 and is still CONSUMED by the surface's own
// claim (the press router owns that rule); this function answers only "which
// key", never "whose press".
//
// It writes the found key's def through `out_def` so the caller needs no second
// lookup, and it walks the same for_each_key the painter does.
inline int key_at(const AppState& a, Page page, int x, int y,
                  KeyDef& out_def) {
    int found = -1;
    KeyDef found_def{};
    for_each_key(a, page,
                 [&](uint32_t index, const KeyDef& k, const GuiRect& r) {
                     if (found < 0 && rect_contains(r, x, y)) {
                         found     = static_cast<int>(index);
                         found_def = k;
                     }
                 });
    out_def = found_def;
    return found;
}

// The painted rect of one key index, for the per-key press/lift damage. A zero
// rect when the index is not on the given page.
inline GuiRect key_rect(const AppState& a, Page page, int index) {
    GuiRect found{0, 0, 0, 0};
    for_each_key(a, page,
                 [&](uint32_t i, const KeyDef&, const GuiRect& r) {
                     if (static_cast<int>(i) == index) found = r;
                 });
    return found;
}

} // namespace onscreen_keyboard
