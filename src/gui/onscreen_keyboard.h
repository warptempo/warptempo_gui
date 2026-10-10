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
// WHAT IT IS. A four-row keyboard wearing plasma-keyboard's three pages key
// for key and width for width (the layout table below), full window width in
// the chrome's ground — A BOTTOM OVERLAY (2026-10-10, the overlay block
// below): laid over the design at the window's foot like Cool Edit's status
// bar, one Hilight line on its top, row 8 and the waveform area's lower part
// under it and nothing moved — the waveform's own passes then not painting
// under it at all (waveform_paint_area, below). It stands while ANY OF
// THE TEXT EDITORS stands, on a backend that asks for one, and it
// REPLACES NOTHING: the flag editor keeps painting in the marker lane, a
// dialog editor keeps its own buttons in THE DIALOG, the other bottom
// overlay, which then stands directly on this one. THERE IS NO SECOND TEXT
// BUFFER — the live editor's own
// text_editor::State is the only text state in the product, exactly as it was
// before this surface existed.
//
// WHY IT IS NOT A UNIVERSAL FIELD. Every key here is a KEY: its press calls the
// backend's synthesize_key and the ORDINARY key path runs unchanged from there
// — GuiInputHandler::on_key, the keyboard-modal gate, route_modal_editor_key,
// each editor's own vocabulary, the undo coalescing, and the core's repeat
// synthesis for a held key. So the editors' grammars, their refusals, their commit and cancel bodies and their byte caps are inherited
// whole rather than mirrored, and a new editor gets a working keyboard by
// existing.
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
// WHAT IT DELIBERATELY HAS NOT GOT: no language switching (one layout; Tab
// wears the globe's slot), no hide act (it leaves with the editor that raised
// it; Esc wears the hide key's slot), no caps lock (shift is one-shot), no
// long-press alternates (Plasma's small digits on the letters and `.`'s `!.?`;
// an act at the press precludes them) and no chords — a second finger is the
// navigation gesture and never reaches a key. No Left/Right keys either:
// backspace and retyping cover a one-line field.

#include "app_state.h"
// THE SLOT'S OTHER TENANT (2026-08-28): this header reads the folder
// overlay's own rect in waveform_paint_area, the one gate and clip both
// tenants share, so the include runs THIS way — the panel borrows nothing
// from the keyboard since its rows became buttons, and what the two share
// (the band, the ceiling) is app_state.h's.
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
// THE THREE PAGES ARE PLASMA-KEYBOARD'S, VERBATIM (architect 2026-09-28,
// built 2026-09-29): plasma/plasma-keyboard, src/layouts/fallback/main.qml
// (the letters) and symbols.qml (its two symbol pages, 1/2 and 2/2), key for
// key and width for width, with TWO DEPARTURES, both in the bottom row: TAB
// TAKES THE GLOBE'S SLOT (the language key; one layout here) and ESC TAKES THE
// HIDE KEY'S (between `.` and Return) — each at Plasma's own width for that
// slot, on all three pages. The function keys wear WORDS, not Plasma's glyphs
// (cap_word, below). (History: 2026-09-28 the symbol page followed Plasma's
// page-1 order within sixteen slots, Tab leading its row 2; the three pages
// were ruled the same day and built the next.)

// THE PAGE, the keyboard's one layout state beside the shift arm. Declared on
// AppState (this header includes app_state.h) and aliased here.
using Page = AppState::OnscreenKeyboard::Page;
inline constexpr int kPageCount = 3;

enum class Role : uint8_t {
    Character,     // types `ch` — every letter, digit and symbol, space included
    Shift,         // ONE-SHOT: the next Character key is capital, then it clears
    Backspace,     // GuiKeys::BackSpace
    Enter,         // GuiKeys::Return
    Escape,        // GuiKeys::Escape
    Tab,           // GuiKeys::Tab, bare — the prompts' completion key
    SymbolMode,    // Plasma's SymbolModeKey, `&123` / `ABC`: letters <-> symbols
    SymbolPage,    // Plasma's page key, `1/2` / `2/2`: flips the symbol pages
};

// THE ROW IS FORTY QUARTER-UNITS WIDE. Every key's width is authored in
// QUARTERS OF A STANDARD KEY, and every one of Plasma's widths is a whole
// number of them, so the table states Plasma's proportions EXACTLY.
// plasma-keyboard (Qt Virtual Keyboard's layout model) sizes keys by WEIGHT:
// every key defaults to the layout's one keyWeight and shares its row's
// width in proportion, while a key marked `Layout.fillWidth: false` takes a
// fixed width — `normalKeyWidth`, one standard key (a tenth of the row, row 0
// being ten default keys), or `functionKeyWidth`, the x of that standard
// key's centre, which is the SECOND key of row 0, so one and a half standard
// keys. Read off the QML, rows counted from 0 at the top as the arrays below
// are, the mapping is:
//   * default key in a ten-key row (row 0 on every page, row 1 on the symbol
//     pages, and the symbol pages' row 2, whose page key, eight characters
//     and Backspace are all default keys): 4;
//   * the letters' row 1: a function-width group of a half-key FillerKey and
//     `a`, seven default keys, and `l` with its filler in another — nine
//     standard keys (36) inset by half a key (2) at each end, which the
//     centring rule of for_each_key states, so a filler is ground, not a key;
//   * the letters' row 2: Shift and Backspace at function width (6) around
//     seven default keys (4);
//   * row 3, the bottom row: the symbol-mode key and Return at function width
//     (6), the globe (Tab here), `,`, `.` and the hide key (Esc here) at
//     normal width (4), and Space, the one default key, filling the rest (12).
//
// The unit itself is DERIVED from the window — the surface is full width by
// ruling, so ten keys across a 2304 px panel are 230 px each — which is why
// this is the one dimension in the product that is not authored at 100% and
// scaled: gui_scale moves the ROW HEIGHTS and the gaps below, and the window
// decides the pitch.
inline constexpr int kUnitsPerRow = 40;
inline constexpr int kRowCount    = 4;
// The widest row on any page; the index arithmetic below reserves this many
// slots per row so a key's index is a pure function of where it sits.
inline constexpr int kMaxRowKeys  = 10;

struct KeyDef {
    Role     role   = Role::Character;
    // Character keys only: the codepoint it types, the LOWERCASE / base form
    // for a letter. A char32_t because symbols 2/2 types past ASCII
    // (· √ ÷ × ½ € £ ¢ ¥ § ™ ® « » “ ” …).
    char32_t ch     = 0;
    uint8_t  span_q = 4;      // width in quarter-units
};

struct Row {
    const KeyDef* keys  = nullptr;
    int           count = 0;
};

namespace detail {

// THE LETTERS — main.qml's four rows (row 1's inset is Plasma's two
// FillerKeys, the mapping above).
inline constexpr KeyDef kLetterRow0[] = {
    {Role::Character, U'q'}, {Role::Character, U'w'}, {Role::Character, U'e'},
    {Role::Character, U'r'}, {Role::Character, U't'}, {Role::Character, U'y'},
    {Role::Character, U'u'}, {Role::Character, U'i'}, {Role::Character, U'o'},
    {Role::Character, U'p'},
};
inline constexpr KeyDef kLetterRow1[] = {
    {Role::Character, U'a'}, {Role::Character, U's'}, {Role::Character, U'd'},
    {Role::Character, U'f'}, {Role::Character, U'g'}, {Role::Character, U'h'},
    {Role::Character, U'j'}, {Role::Character, U'k'}, {Role::Character, U'l'},
};
inline constexpr KeyDef kLetterRow2[] = {
    {Role::Shift, 0, 6},
    {Role::Character, U'z'}, {Role::Character, U'x'}, {Role::Character, U'c'},
    {Role::Character, U'v'}, {Role::Character, U'b'}, {Role::Character, U'n'},
    {Role::Character, U'm'},
    {Role::Backspace, 0, 6},
};

// SYMBOLS 1/2 — symbols.qml's first page. Row 1 is ten keys, not inset.
inline constexpr KeyDef kSymbols1Row0[] = {
    {Role::Character, U'1'}, {Role::Character, U'2'}, {Role::Character, U'3'},
    {Role::Character, U'4'}, {Role::Character, U'5'}, {Role::Character, U'6'},
    {Role::Character, U'7'}, {Role::Character, U'8'}, {Role::Character, U'9'},
    {Role::Character, U'0'},
};
inline constexpr KeyDef kSymbols1Row1[] = {
    {Role::Character, U'@'}, {Role::Character, U'#'}, {Role::Character, U'%'},
    {Role::Character, U'&'}, {Role::Character, U'*'}, {Role::Character, U'_'},
    {Role::Character, U'-'}, {Role::Character, U'+'}, {Role::Character, U'('},
    {Role::Character, U')'},
};
inline constexpr KeyDef kSymbols1Row2[] = {
    {Role::SymbolPage},
    {Role::Character, U'"'}, {Role::Character, U'<'}, {Role::Character, U'>'},
    {Role::Character, U'\''}, {Role::Character, U':'}, {Role::Character, U'/'},
    {Role::Character, U'!'}, {Role::Character, U'?'},
    {Role::Backspace},
};

// SYMBOLS 2/2 — symbols.qml's second page.
inline constexpr KeyDef kSymbols2Row0[] = {
    {Role::Character, U'~'}, {Role::Character, U'`'}, {Role::Character, U'|'},
    {Role::Character, U'·'}, {Role::Character, U'√'}, {Role::Character, U'÷'},
    {Role::Character, U'×'}, {Role::Character, U'½'}, {Role::Character, U'{'},
    {Role::Character, U'}'},
};
inline constexpr KeyDef kSymbols2Row1[] = {
    {Role::Character, U'$'}, {Role::Character, U'€'}, {Role::Character, U'£'},
    {Role::Character, U'¢'}, {Role::Character, U'¥'}, {Role::Character, U'^'},
    {Role::Character, U'='}, {Role::Character, U'§'}, {Role::Character, U'['},
    {Role::Character, U']'},
};
inline constexpr KeyDef kSymbols2Row2[] = {
    {Role::SymbolPage},
    {Role::Character, U'™'}, {Role::Character, U'®'}, {Role::Character, U'«'},
    {Role::Character, U'»'}, {Role::Character, U';'}, {Role::Character, U'“'},
    {Role::Character, U'”'}, {Role::Character, U'\\'},
    {Role::Backspace},
};

// THE BOTTOM ROW, Plasma's `[&123/ABC] [globe] , [Space] . [hide] [Enter]`
// with Tab in the globe's slot and Esc in the hide key's. The letters and
// symbols 1/2 share one array; symbols 2/2 differs in the `.` slot alone,
// which wears and types `…` there as Plasma's does.
// 6 + 4 + 4 + 12 + 4 + 4 + 6 = 40.
//
// TAB (architect 2026-08-27, with the project model; in the globe's slot on
// every page since 2026-09-29) is what the product's prompts COMPLETE on (the
// one autocomplete model, route_modal_editor_key) and what walks a dialog's
// focus ring: a BARE Tab, no modifier, on the same synthesize_key road as
// every other key here. IT IS NOT THE OPEN PROJECT PICKER'S GLASS ROAD
// (2026-08-28): that picker is field-less and stands in this band, so the
// keyboard does not paint there at all and the gesture is File → Open
// project, tap the project's row. What the key serves is every prompt a
// finger can raise — the settings editor's value recall and the ring walk on
// the dialogs that publish buttons.
inline constexpr KeyDef kBottomRow[] = {
    {Role::SymbolMode, 0,     6},
    {Role::Tab,        0,     4},
    {Role::Character,  U',',  4},
    {Role::Character,  U' ', 12},
    {Role::Character,  U'.',  4},
    {Role::Escape,     0,     4},
    {Role::Enter,      0,     6},
};
inline constexpr KeyDef kSymbols2BottomRow[] = {
    {Role::SymbolMode, 0,     6},
    {Role::Tab,        0,     4},
    {Role::Character,  U',',  4},
    {Role::Character,  U' ', 12},
    {Role::Character,  U'…',  4},
    {Role::Escape,     0,     4},
    {Role::Enter,      0,     6},
};

template <int N>
constexpr Row make_row(const KeyDef (&a)[N]) { return Row{a, N}; }

inline constexpr Row kPages[kPageCount][kRowCount] = {
    {make_row(kLetterRow0), make_row(kLetterRow1),
     make_row(kLetterRow2), make_row(kBottomRow)},
    {make_row(kSymbols1Row0), make_row(kSymbols1Row1),
     make_row(kSymbols1Row2), make_row(kBottomRow)},
    {make_row(kSymbols2Row0), make_row(kSymbols2Row1),
     make_row(kSymbols2Row2), make_row(kSymbols2BottomRow)},
};

// THE TABLE'S TWO SHAPE FACTS, checked at compile time: every row fits the
// index arithmetic's slot reservation, and every row but the letters' inset
// row 1 fills the width exactly (that one is nine standard keys, centred).
constexpr bool pages_are_well_formed() {
    for (int p = 0; p < kPageCount; ++p) {
        for (int r = 0; r < kRowCount; ++r) {
            const Row& row = kPages[p][r];
            if (row.count > kMaxRowKeys) return false;
            int span = 0;
            for (int i = 0; i < row.count; ++i) span += row.keys[i].span_q;
            const int want = (p == 0 && r == 1) ? kUnitsPerRow - 4
                                                : kUnitsPerRow;
            if (span != want) return false;
        }
    }
    return true;
}
static_assert(pages_are_well_formed(),
              "a keyboard row overflows its slots or misses Plasma's width");

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
// which is 1..120.
inline constexpr uint32_t key_index(Page page, int row, int col) {
    return kStableCodeBase + static_cast<uint32_t>(
        ((detail::page_number(page) * kRowCount) + row) * kMaxRowKeys + col);
}

// THE PAGE A KEY INDEX BELONGS TO — the inverse of the page term above, and
// the one place that reads it back. Its consumer is the RELEASE, which must
// damage the key the finger pressed even when that key's own act (the two
// page keys) has moved the live page out from under it; asking the live page
// there would look the key up on a page it is not on and damage nothing.
inline constexpr Page page_of_key_index(int index) {
    return static_cast<Page>(
        (static_cast<uint32_t>(index) - kStableCodeBase) /
        static_cast<uint32_t>(kRowCount * kMaxRowKeys));
}

// -- The derivations off the table ------------------------------------------
//
// THE CASE IS A DERIVATION AND NOT A SECOND TABLE (the state-axis rule for a
// new painted surface): the table holds the base character once, and the shift
// lamp turns it into the cap the key WEARS and the codepoint it TYPES through
// this one function, so the two can never disagree about what a shifted key is.
// Non-letters are unmoved — this keyboard has no shifted punctuation, the
// symbol pages being where the rest lives, and every letter is on the letter
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
// keysym for a letter in both cases. The same numbering gives Latin-1 its own
// code points as keysyms (`£` is 0xa3, `×` is 0xd7) and every other character
// the Unicode keysym 0x01000000 + its code point (`€` is 0x010020ac), which is
// how symbols 2/2's keys get theirs. That identity is why the punctuation this
// keyboard types needs no named constants in GuiKeys: a name earns its place by
// being BOUND somewhere, and nothing in the dispatch binds `$`, `(`, `_` or
// `€` — they exist only as characters an editor inserts, and what the editor
// inserts is the CODEPOINT the press carries beside the keysym (the printable
// branch of text_editor::handle_key, into replace_selection — the road a
// hardware keyboard's composed character takes on the laptop).
inline constexpr GuiKey keysym_of(char32_t base) {
    return base < 0x100 ? static_cast<GuiKey>(base)
                        : static_cast<GuiKey>(0x01000000u | base);
}

// WHAT A KEY WEARS, AND IT IS TEXT AND NOTHING ELSE (architect 2026-08-27, on
// glass): every cap is a WORD or a character on the ONE sans face at the
// product's one text size, shaped through the one chokepoint like every other
// label. The function keys wore Breeze glyphs for a day and read OVERSIZED
// beside the letter caps — a 22-unit icon scaled to the key's own height next
// to a letter at the one text size — and there is plenty of horizontal room on a full-width
// row, so they wear words.
//
// AND THE WORD A FUNCTION KEY WEARS IS THE KEY'S NAME (planner 2026-09-01,
// under the capitalization sweep's universal-rules spine): all five say what
// they ARE — Shift, Backspace, Return, Esc, Tab — in the product's one key
// spelling, which is Qt's and so kdenlive's (spell_chord's head, gui_input.h).
// They said what they DO from 2026-08-27, which is why "Return" read "Enter"
// and the Escape key read "Cancel"; the Enter cap took its name that day, and
// leaving one act-named cap beside four key-named ones would have been the
// kind of exception this product no longer keeps. ONE RULE FOR THE FIVE.
//
// This function is the ONE OWNER of the words. It answers the cap for every key
// that has one that is not simply its own character: the five function keys
// (Tab among them since 2026-08-27, a word exactly as Shift, Backspace and
// Return are), SPACE (which has no glyph of its own to wear), and the two PAGE
// KEYS, each in Plasma's own caps: the SYMBOL-MODE key names the page it goes
// TO (`&123` on the letters, `ABC` on either symbol page), and the PAGE KEY
// names the symbol page it stands ON (`1/2`, `2/2`). A Character key other
// than space answers nullptr and the painter spells it out of the table's own
// `ch` through the one case derivation above.
//
// THE CAPS ARE SPELLED THE PRODUCT'S ONE WAY (architect 2026-09-01), which is
// Qt's and so kdenlive's: "Return" — not "Enter", the word stamped on the
// plastic, which this cap read until that day — and "Esc", which read "Cancel"
// until the same evening's ruling closed the one act-named cap out (it named
// what the key DOES to the editor standing over it, the button convention);
// Shift, Tab and Space Qt spells the same as this keyboard always did.
// BACKSPACE'S WORD IS THE WHOLE WORD, "Backspace" (architect 2026-10-06: in
// Tahoma it fits the key with 2 clear each side), one spelling on every
// page. MEASURED with the tracking against the symbol pages'
// one-standard-key box (about 54 Windows px at 400 %): "Backspace" is 50.0
// Windows px in Tahoma, 2 clear each side (measured 2026-10-06).
//
// SHIFT'S LAMP IS THE FACE, NOT THE CAP. The word is "Shift" armed or resting;
// what says the arm is the key's ARMED FACE — the button painter's CHECKED
// face, soft sunken over the Hilight dither (paint_button_box), which this key and the
// symbol-mode key (while a symbol page stands) wear off their lamps — and
// the letter caps themselves, every one of which turns capital while the arm
// stands. The page key wears no lamp: its cap already says the page.
inline const char* cap_word(const KeyDef& k, Page page) {
    switch (k.role) {
        case Role::Shift:       return "Shift";
        case Role::Backspace:   return "Backspace";
        case Role::Enter:       return "Return";
        case Role::Escape:      return "Esc";
        case Role::Tab:         return "Tab";
        case Role::SymbolMode:  return page == Page::Letters ? "&123" : "ABC";
        case Role::SymbolPage:  return page == Page::Symbols2 ? "2/2" : "1/2";
        case Role::Character:   return k.ch == U' ' ? "Space" : nullptr;
    }
    return nullptr;
}

// THE TWO PAGE KEYS' ACTS, one owner for where each goes. The symbol-mode key
// goes letters -> symbols 1/2 and either symbol page -> letters; the page key
// flips 1/2 <-> 2/2. Returning to the symbols always lands on 1/2, as
// Plasma's does (its symbols loader resets `secondPage` whenever it is
// hidden).
inline constexpr Page page_after(Role role, Page page) {
    if (role == Role::SymbolMode)
        return page == Page::Letters ? Page::Symbols1 : Page::Letters;
    if (role == Role::SymbolPage)
        return page == Page::Symbols1 ? Page::Symbols2 : Page::Symbols1;
    return page;
}

// -- The authored geometry --------------------------------------------------
//
// Only the VERTICAL dimensions and the gaps are authored at 100% and scaled
// like every other redesigned dimension; the key PITCH is the window's (see
// kUnitsPerRow). The proportions are the reference photograph's: a key about
// two and a half times wider than tall, gaps a tenth of the key's height.
// IN WINDOWS PX since the unit's change (architect 2026-10-02): the laptop
// pixel's 40 / 4 / 4 re-authored to the device sizes they had on the tablet.
inline constexpr double kKeyHeightPx = 29.0;   // one row's key box
inline constexpr double kKeyGapPx    = 3.0;    // between adjacent keys, both axes
inline constexpr double kPadPx       = 3.0;    // the rows' inset across; + half a gap = the edge pad

inline int key_height_px()  { return scaled_px(kKeyHeightPx, 1); }
inline int key_gap_px()     { return scaled_px(kKeyGapPx, 1); }
inline int pad_px()         { return scaled_px(kPadPx); }

// THE EDGE PAD — THE SAME ON ALL FOUR SIDES (architect 2026-10-10, the
// overlay's mocks: "the pad the same on all four sides"). Across, the layout
// already leaves it: the key walk (for_each_key) insets the row by pad_px and
// then each key's slot by half a gap, rounding the key's edge at the
// element, so the first key's left edge stands nearbyint(pad + gap / 2) in
// from the band's — 14 device px at 2304 x 1440 and 300 % (9 + 4.5, the tie
// to even), 16 at 360 % (11 + 5.5), 6 at the laptop's 138 % — and the last
// key's right edge as far in from the other side on both devices. That
// leftover is the pad: the band's top and bottom take the same number, so
// the first key row starts it below the band's top and the last ends it
// above the window's foot. The walk's horizontal arithmetic is unchanged;
// this is its own leftover read back.
inline int edge_pad_px() {
    return static_cast<int>(std::nearbyint(pad_px() + key_gap_px() * 0.5));
}

// The band's whole height: the edge pad, four key rows and the three gaps
// between them, and the edge pad — 14 + 375 + 14 = 403 device rows at 300 %.
// THE BAND ITSELF CARRIES NO LINE: its one line, the overlay's Hilight top,
// stands directly ABOVE it (or above the dialog standing on it) and belongs
// to the overlay (the overlay block below).
inline int surface_height_px() {
    return 2 * edge_pad_px() + kRowCount * key_height_px() +
           (kRowCount - 1) * key_gap_px();
}

// -- Standing ----------------------------------------------------------------

// DOES THE SURFACE STAND? Three terms: the PLATFORM must want a painted
// keyboard (false forever on Wayland — the ruling is at that backend's
// wants_onscreen_keyboard), one of the editors must own the keyboard, and
// that editor must not be the settings CHOICE editor (the third term's
// record is at the predicate).
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
// both but the FILE anchor above the band, whose
// three rows open no editor); and the touch region begin refuses under
// both.
// So the second term is false whenever the overlay stands, and this
// predicate cannot answer true over the band without a producer this record
// would have to name.
//
// THE THIRD TERM IS THE CHOICE EDITOR'S (2026-10-07 evening): the settings
// editor standing as a CHOICE editor (settings_editor.h's head) is a text
// editor session with nothing to type — its combo is chosen, not typed —
// so the band does not rise for it, and the well stays in view under the
// list it drops.
inline bool stands(const AppState& a, const GuiPlatform& gui) {
    return gui.wants_onscreen_keyboard() && a.text_editor_session() != 0 &&
           !a.settings_choice_live();
}

// -- The bottom overlays (architect 2026-10-10) ------------------------------
//
// THE DIALOG AND THE ON-SCREEN KEYBOARD ARE TASKBARS (architect 2026-10-10
// ~09:40, "this matches exactly what I asked for … okay to implement", on
// the planner's mocks tmp/mocks/ring/mock_KB4_1_dialog_alone.png,
// mock_KB4_2_keyboard_alone.png and mock_KB5_3_dialog_keyboard_shared_pad.png).
// HIS MODEL is Cool Edit's own status bar in his Wine capture: "the taskbar at
// the bottom … has nothing below it. There's no border below that. That's how
// the keyboard and the modal operate. They're analogous to the taskbar,
// basically, but they sit on top of the design. They don't move anything
// below them." A BOTTOM OVERLAY is a chrome surface laid over the design at
// the window's foot: the window's whole width (over the program frame's side
// columns and bottom row in its rows), its top edge ONE relief line in the
// chrome's Hilight — the light of the program frame's bottom and right sides,
// "not a bevel … just a one pixel border" — the chrome's ground below it, no
// bottom or side border, and NOTHING BENEATH IT MOVES: the program lanes, the
// program frame, the dock bar and row 8 keep their geometry and paint as
// always, and the overlay covers them. Two exist:
//   THE DIALOG — a chrome tenant of the bottom row (dialog_stands: a prompt,
//     a dialog editor, the picker's Cancel; the render player is the
//     program's and is no overlay). ALONE its top is the dock bar's bottom,
//     the dock bar painted above it as Cool Edit's ridge: the line takes row
//     8's content band's first line and the ground runs to the window's foot
//     — row 8's content band less that line plus the frame's bottom row, 32 W
//     at every scale (dialog_band_h_px). Its controls are VERTICALLY CENTERED
//     in that ground, the half-row tie to the top (the cap rule's own;
//     paint_modal_dialog's row_centred_y). At 300 %: the line 1341–1343, the
//     ground 1344–1439, the field 1357–1425 (13 above, 14 below); at the
//     laptop's 138 % on 1920 x 1080 the line row 1035, the ground 1036–1079,
//     the field 1042–1073 (6 above, 6 below).
//   THE KEYBOARD — ALONE at the window's foot, its band the edge pad, the
//     four key rows and their three gaps, the edge pad (surface_height_px),
//     the line directly above it. At 300 %: the line 1034–1036, the band
//     1037–1439 (14 + 375 + 14), the first key row's top 1051. Row 8 is under
//     it and unreachable: the keyboard's press claim takes the band and its
//     line (claim_rect).
//   BOTH — a dialog editor with the keyboard: THE DIALOG STANDS DIRECTLY ON
//     THE KEYBOARD, one line above the pair and no separation. The gap
//     between the dialog's controls and the first key row is ONE PAD, not
//     two: the dialog keeps its own top pad (the centered pad of the dialog
//     alone, dialog_top_pad_px) and drops its bottom pad, and the keyboard's
//     edge pad is the gap — his words, "if they're both the same, then they
//     just combine into one". At 300 % the two are both 14; THE KEYBOARD'S
//     PAD IS THE ONE ROAD, so they never double at a scale where they differ.
//     At 300 %: the line 952–954, the dialog 955–1036 (its pad 955–967, the
//     field 968–1036), the keyboard 1037–1439.
// ON SCREEN IS AS PAINTED: nothing under an overlay answers a press, a cursor
// zone or the touch pan zone — the waveform's painted rect stops at the
// overlay's line (waveform_paint_area below, which point_on_nav_surface
// reads), row 8 yields its buttons to a dialog (paint_bottom_strip) and the
// keyboard's claim is opaque. Painted by paint_bottom_overlays
// (paint_handler.cpp: the line and the dialog's ground, after the keyboard
// slot), the keys by paint_onscreen_keyboard, the dialog's controls by
// paint_modal_dialog. Geometry only here: every rect below is a fact about
// geometry and asks no standing unless its name says so (overlay_rect).

// THE OVERLAY'S LINE: one relief line, the program frame's own weight.
inline int overlay_line_px() {
    return relief_line_px();
}

// THE WINDOW'S FOOT: the program frame's bottom row's last line, bottom lane 0
// on the clamped window (main.cpp's lane table), so every overlay rect runs
// on the same clamped geometry as the lanes it covers.
inline int window_foot_y(const AppState& a) {
    const GuiRect f = bottom_program_frame_area(a);
    return f.y + f.h;
}

// THE KEYBOARD'S BAND: the window's whole width — the clamped width, read off
// the caption's lane — standing on the window's foot, its height
// surface_height_px. It OVERLAYS row 8 and the waveform area's lower part:
// nothing in the vertical stack moved to make room, and the waveform simply
// is not painted where this paints. The line above it is the overlay's
// (claim_rect, overlay_rect). The one zero rect is the degenerate one: a
// window with no width, or a height that scales to nothing.
inline GuiRect surface_rect(const AppState& a) {
    const GuiRect window = top_caption_row_area(a);
    const int     h      = surface_height_px();
    if (window.w <= 0 || h <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{window.x, window_foot_y(a) - h, window.w, h};
}

// THE DIALOG STANDS while a chrome tenant owns the bottom row (the set's one
// owner, chrome_tenant_owns_bottom_row, app_state.h).
inline bool dialog_stands(const AppState& a) {
    return chrome_tenant_owns_bottom_row(a);
}

// THE DIALOG'S CONTROLS' HEIGHT: the taller of the dialog field and the push
// button (both 23 W under win2000 — Windows' 14 dialog units), what the
// centered pad is measured against and what the dialog keeps of its ground
// when it stands on the keyboard.
inline int dialog_content_h_px() {
    return std::max(scaled_px(kModalFieldHeightPx),
                    scaled_px(live_chrome_spec().push_button_box_px));
}

// THE DIALOG'S BAND HEIGHT: its ground when it stands alone — from under the
// line on row 8's content band's first line down to the window's foot. 96
// device rows at 300 %, 44 at the laptop's 138 %.
inline int dialog_band_h_px(const AppState& a) {
    const GuiRect c = bottom_row_content_area(a);
    return std::max(0, window_foot_y(a) - (c.y + overlay_line_px()));
}

// THE DIALOG'S TOP PAD: its controls centered in the band, the half-row tie
// to the top — 13 at 300 % (27 rows of air, 13 over 14), 6 at 138 %.
inline int dialog_top_pad_px(const AppState& a) {
    return std::max(0, (dialog_band_h_px(a) - dialog_content_h_px()) / 2);
}

// THE DIALOG'S GROUND, under its line, the window's whole width: ALONE the
// band to the window's foot; ON THE KEYBOARD its top pad and its controls'
// height standing directly on the keyboard's band, the bottom pad dropped
// (the block's head).
inline GuiRect dialog_ground_rect(const AppState& a, bool on_keyboard) {
    const GuiRect window = top_caption_row_area(a);
    if (window.w <= 0) return GuiRect{0, 0, 0, 0};
    if (!on_keyboard) {
        const int y = bottom_row_content_area(a).y + overlay_line_px();
        return GuiRect{window.x, y, window.w,
                       std::max(0, window_foot_y(a) - y)};
    }
    const GuiRect kb = surface_rect(a);
    const int     h  = dialog_top_pad_px(a) + dialog_content_h_px();
    return GuiRect{window.x, kb.y - h, window.w, h};
}

// THE BAND THE DIALOG'S CONTROLS ARE CENTERED IN: the ground's top, the
// alone band's height — so the controls take the same top pad in both forms,
// and on the keyboard the band's lower rows (the pad the dialog drops) fall
// on the keyboard's own edge pad, which is the gap.
inline GuiRect dialog_band_rect(const AppState& a, bool on_keyboard) {
    const GuiRect g = dialog_ground_rect(a, on_keyboard);
    return GuiRect{g.x, g.y, g.w, dialog_band_h_px(a)};
}

// THE DIALOG'S WHOLE OVERLAY: its line and its ground — the modal's surface
// (ModalDialogGeometry::box) and the rect its damage takes.
inline GuiRect dialog_rect(const AppState& a, bool on_keyboard) {
    const GuiRect g    = dialog_ground_rect(a, on_keyboard);
    const int     line = overlay_line_px();
    if (g.w <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{g.x, g.y - line, g.w, g.h + line};
}

// THE KEYBOARD'S CLAIMED SURFACE: its band, and the overlay's line above it
// when no dialog stands on it — the line is then the keyboard's top, and a
// press on it is the keyboard's consumed nothing rather than a reach into
// what lies under the overlay. Under a dialog the line is the dialog's and
// the dialog's veil answers it.
inline GuiRect claim_rect(const AppState& a) {
    const GuiRect s = surface_rect(a);
    if (s.w <= 0 || s.h <= 0 || dialog_stands(a)) return s;
    const int line = overlay_line_px();
    return GuiRect{s.x, s.y - line, s.w, s.h + line};
}

// THE STANDING OVERLAY'S WHOLE RECT, its line on top: the keyboard alone,
// the dialog alone, or the dialog on the keyboard — from the line to the
// window's foot. A zero rect when neither stands.
inline GuiRect overlay_rect(const AppState& a, const GuiPlatform& gui) {
    const bool kb  = stands(a, gui);
    const bool dlg = dialog_stands(a);
    if (!kb && !dlg) return GuiRect{0, 0, 0, 0};
    const GuiRect window = top_caption_row_area(a);
    if (window.w <= 0) return GuiRect{0, 0, 0, 0};
    const int top = (dlg ? dialog_ground_rect(a, kb).y : surface_rect(a).y) -
                    overlay_line_px();
    return GuiRect{window.x, top, window.w,
                   std::max(0, window_foot_y(a) - top)};
}

// THE SLOT'S DAMAGE RECT, the slot AT ITS TALLEST: the folder overlay's band
// at the ceiling (keyboard_slot_band, app_state.h) united with the keyboard
// at its tallest — its band, its line and a dialog standing on it — so one
// rect contains every tenant's pixels in every form.
// THE SHOW/HIDE COMPARATOR TAKES IT (main.cpp): those two edges damage a band
// whose tenant is arriving or has already gone, so the rect cannot be either
// tenant's own — on the hide the departed surface's pixels are exactly what
// has to be erased, and on the show the arriving one's whole band has to be
// covered. A damage INSIDE a standing band takes that tenant's own rect
// instead.
inline GuiRect slot_damage_rect(const AppState& a) {
    const GuiRect band = keyboard_slot_band(a, keyboard_slot_max_height_px(a));
    const GuiRect kb   = surface_rect(a);
    if (kb.w <= 0 || kb.h <= 0) return band;
    const int top = std::min(kb.y - overlay_line_px(),
                             dialog_rect(a, /*on_keyboard=*/true).y);
    const GuiRect tall{kb.x, top, kb.w, kb.y + kb.h - top};
    if (band.w <= 0 || band.h <= 0) return tall;
    return union_rect(band, tall);
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
// arm moves every letter cap's case and the page moves every key.
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
    a.onscreen_keyboard.page         = Page::Letters;
    viewport.invalidate_rect(surface_rect(a));
}

// THE WAVEFORM'S PAINTED RECT — waveform_area minus the KEYBOARD SLOT's
// standing tenant (this keyboard with the bottom overlay it heads, or the
// folder overlay that replaces it in the slot — folder_overlay.h), and the
// ONE OWNER of the rule that THE WAVEFORM IS NOT PAINTED WHERE THE SLOT
// PAINTS. IT SUBTRACTS THE STANDING TENANT'S OWN RECT, which since
// 2026-08-28 is a real fork rather than a formality: the overlay's band is the
// CEILING every time it stands (the icon row's foot down since 2026-09-09,
// whatever its listing's length) and the keyboard's is its band, its line and
// any dialog standing on it (overlay_rect), so a rect borrowed from the other
// tenant would either hide waveform nothing paints over or leave the panel
// painting where the waveform still runs.
//
// Both tenants' grounds are fully opaque and every waveform pass runs BEFORE
// them (the authoritative paint order, paint_handler.cpp),
// so a waveform pixel under the band is work whose result is thrown away in
// the same frame: a narrow scanner damage column crossing the band would
// otherwise pay the plate blit and every vertical over the
// band's whole height, at the panel's own tick rate. ONE GATE, ONE CLIP for
// both tenants.
//
// IT IS THE EXPOSURE GATE, THE CLIP AND THE NAVIGATION SURFACE'S EXTENT,
// NEVER A COORDINATE INPUT: the column mapping keeps reading waveform_area
// itself (the displayed basis, the strictly-as-painted rule at app_state.h),
// while the press, the cursor's Pan / Zoom zone and the touch pan zone ask
// this rect where the waveform ENDS (point_on_nav_surface, input_pointer.cpp,
// 2026-10-10) — so a press never answers where no waveform is painted, a
// bottom overlay's line and a dialog standing on the keyboard among them,
// and paint and hit cannot drift. This rect says WHERE THE PIXELS MAY LAND,
// and so where the waveform takes a press.
//
// Every band is full width and runs down to the window's foot or to the
// bottom row (the folder overlay), so what it hides off the waveform is
// always a BOTTOM SLICE and the answer is a rect. A
// band that reaches no higher than the waveform's own bottom subtracts
// nothing (the dialog standing alone, below; the keyboard always overlaps it
// since the waveform became the lanes' whole leftover, 2026-10-07); a band
// that swallows the waveform whole answers a ZERO-HEIGHT rect, and it is the
// CLIP rather than the gate that makes that case paint nothing (rects_intersect
// can still answer true for an empty rect an exposure straddles — it compares
// edges, not areas). THAT LAST CASE IS THE OVERLAY'S ORDINARY ONE SINCE
// 2026-09-02, when the panel's ceiling first moved above the waveform (the
// icon row's foot since 2026-09-09):
// its band starts ABOVE the waveform, so
// every waveform pass is clipped out whole while it stands, and the LANES it
// also covers (every lane below the icon row; the menu row AND the icon row
// stand above the band since 2026-09-09, File lit and the other two anchors
// and every icon greyed) are not
// spared this way — their painters run and the panel covers them, because
// they publish the roster's hit rects (the record is at the paint-order
// block, paint_handler.cpp).
// THE KEYBOARD'S SHARE IS THE WHOLE BOTTOM OVERLAY (2026-10-10, the overlay
// block above): its band, the dialog standing on it and the line on top of
// the pair, overlay_rect — the waveform stops above the line. A dialog alone
// starts under the dock bar, below the waveform's foot, and subtracts
// nothing; the folder overlay (the render player's and the picker's band)
// starts above the waveform and subtracts it whole.
inline GuiRect waveform_paint_area(const AppState& a, const GuiPlatform& gui) {
    const GuiRect area  = waveform_area(a);
    int           cut_y = area.y + area.h;
    if (folder_overlay::stands(a)) {
        const GuiRect band = folder_overlay::surface_rect(a);
        if (band.w > 0 && band.h > 0) cut_y = std::min(cut_y, band.y);
    }
    const GuiRect over = overlay_rect(a, gui);
    if (over.w > 0 && over.h > 0) cut_y = std::min(cut_y, over.y);
    const int hidden = (area.y + area.h) - cut_y;
    if (hidden <= 0) return area;
    GuiRect painted = area;
    painted.h = hidden >= area.h ? 0 : area.h - hidden;
    return painted;
}

// -- The ONE walk over the keys ----------------------------------------------

// THE ONE WALK OVER THE KEYS, and the reason paint and hit cannot drift: both
// go through it. `fn(index, def, rect)` is called for every key of `page` in
// painted order, with `rect` the key's PAINTED box — the slot inset by half a
// gap on each side, so the gaps between keys are uniform and the row's ends sit
// on the margin exactly.
//
// A row whose spans sum to less than kUnitsPerRow is CENTERED in the surface
// (Plasma's half-key FillerKeys on the nine-letter row); a row that fills it
// starts at the margin. One rule, both cases. DOWN, the first row starts the
// edge pad under the band's top (2026-10-10, edge_pad_px: the leftover this
// walk leaves across, read back, so the pad is one on all four sides).
template <class Fn>
inline void for_each_key(const AppState& a, Page page, Fn&& fn) {
    const GuiRect surf = surface_rect(a);
    if (surf.w <= 0 || surf.h <= 0) return;

    const int gap    = key_gap_px();
    const int pad    = pad_px();
    const int key_h  = key_height_px();
    const int inner_x = surf.x + pad;
    const int inner_w = surf.w - 2 * pad;
    if (inner_w <= 0) return;
    const double unit = static_cast<double>(inner_w) / kUnitsPerRow;

    int y = surf.y + edge_pad_px();
    for (int r = 0; r < kRowCount; ++r) {
        const Row& row = row_of(page, r);
        int span_total = 0;
        for (int i = 0; i < row.count; ++i) span_total += row.keys[i].span_q;
        const double row_x0 =
            inner_x + (kUnitsPerRow - span_total) * unit * 0.5;

        int q = 0;
        for (int i = 0; i < row.count; ++i) {
            const KeyDef& k = row.keys[i];
            const double slot_x0 = row_x0 + q * unit;
            const double slot_x1 = row_x0 + (q + k.span_q) * unit;
            const int kx = static_cast<int>(std::nearbyint(slot_x0 + gap * 0.5));
            const int kw =
                static_cast<int>(std::nearbyint(slot_x1 - gap * 0.5)) - kx;
            if (kw > 0) {
                fn(key_index(page, r, i), k, GuiRect{kx, y, kw, key_h});
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
