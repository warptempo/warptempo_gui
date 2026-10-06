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
// for key and width for width (the layout table below), full window width,
// sitting DIRECTLY ABOVE THE BOTTOM
// ROW and painting over the waveform area's lower part — which the waveform's
// own passes then do not paint at all (waveform_paint_area, below). It stands while ANY OF
// THE TEXT EDITORS stands, on a backend that asks for one, and it
// REPLACES NOTHING: the flag editor keeps painting in the marker lane, a dialog
// editor keeps painting in the bottom row with its own buttons, and this sits
// between them. THERE IS NO SECOND TEXT BUFFER — the live editor's own
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
// BACKSPACE IS "Backsp" (architect 2026-10-06), a spelling compact keyboards
// print, and the cap keeps one spelling on every page. MEASURED with the
// tracking against the symbol pages' one-standard-key box (about 54 Windows
// px at 400 %): in Nimbus (the win95 set) "Backsp" is 39.4 Windows px, 7
// clear each side, the whole "Backspace" (58.7) overflows it, and "Backspc"
// (45.3) reads as a typo; in the live set's Tahoma "Backsp" is 33.6, 10
// clear each side, and "Backspace" (50.0) would fit with 2 (measured
// 2026-10-06).
//
// SHIFT'S LAMP IS THE FACE, NOT THE CAP. The word is "Shift" armed or resting;
// what says the arm is the key's ARMED FACE — the roster's own CHECKED face,
// soft sunken over the Hilight dither (paint_button_box), which this key and the
// symbol-mode key (while a symbol page stands) wear off their lamps — and
// the letter caps themselves, every one of which turns capital while the arm
// stands. The page key wears no lamp: its cap already says the page.
inline const char* cap_word(const KeyDef& k, Page page) {
    switch (k.role) {
        case Role::Shift:       return "Shift";
        case Role::Backspace:   return "Backsp";
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
inline constexpr double kPadPx       = 3.0;    // the surface's own outer margin

inline int key_height_px()  { return scaled_px(kKeyHeightPx, 1); }
inline int key_gap_px()     { return scaled_px(kKeyGapPx, 1); }
inline int pad_px()         { return scaled_px(kPadPx); }

// The surface's whole height: four key rows, three gaps between them, and the
// outer margin at both ends. THE BAND HAS NO CHROME OF ITS OWN — no line at its
// top edge (architect 2026-08-27, on glass): the keyboard's ground is the
// bottom row's ground, so the two lanes read as one block and a seam between
// them would draw a border through the middle of it.
inline int surface_height_px() {
    return 2 * pad_px() + kRowCount * key_height_px() +
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
// both but the FILE anchor above the band, whose
// three rows open no editor); and the touch region begin refuses under
// both.
// So the second term is false whenever the overlay stands, and this
// predicate cannot answer true over the band without a producer this record
// would have to name.
inline bool stands(const AppState& a, const GuiPlatform& gui) {
    return gui.wants_onscreen_keyboard() && a.text_editor_session() != 0;
}

// -- The surface's rect ------------------------------------------------------

// THE SURFACE'S RECT: full window width, its BOTTOM edge flush on the bottom
// row's top edge, so the two lanes touch with no window ground between them.
// It OVERLAYS the waveform area's lower part — nothing in the vertical stack
// moved to make room (main.cpp's stack owner is untouched by this feature), and
// the waveform simply is not painted where this paints.
//
// IT DOES NOT ASK WHETHER THE SURFACE STANDS — a rect is a fact about geometry
// and standing is a decision, which every caller makes for itself through
// stands() (or, for the two readers below, keeps inside its own body). The one
// zero rect it answers is the degenerate one: a bottom row with no width, or a
// surface height that scales to nothing.
inline GuiRect surface_rect(const AppState& a) {
    // THE SLOT'S BAND, lifted by THIS surface's height: the band itself — its
    // x, its width and its bottom edge — is the two tenants' shared owner
    // (keyboard_slot_band, app_state.h), and the height is this keyboard's
    // four key rows. The overlay's rect is the same call with its own height.
    return keyboard_slot_band(a, surface_height_px());
}

// THE SLOT'S DAMAGE RECT, the band AT ITS TALLEST — the taller of this
// keyboard's four key rows and the overlay's ceiling (both bands are fixed and
// both rise from the slot's one bottom edge, so the taller contains the
// other). It stays a MAX rather than collapsing with the overlay's own fixed
// height (architect 2026-08-28): the two tenants still differ, this
// keyboard's height being its rows' and the panel's the ceiling.
// THE SHOW/HIDE COMPARATOR TAKES IT (main.cpp): those two edges damage a band
// whose tenant is arriving or has already gone, so the rect cannot be either
// tenant's own — on the hide the departed surface's pixels are exactly what
// has to be erased, and on the show the arriving one's whole band has to be
// covered. A damage INSIDE a standing band takes that tenant's own rect
// instead.
inline GuiRect slot_damage_rect(const AppState& a) {
    return keyboard_slot_band(
        a, std::max(surface_height_px(), keyboard_slot_max_height_px(a)));
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

// THE WAVEFORM'S PAINTED RECT — waveform_area minus the KEYBOARD SLOT's band
// when EITHER tenant stands (this keyboard, or the folder overlay that
// replaces it in the same band — folder_overlay.h), and the ONE OWNER of the
// rule that THE WAVEFORM IS NOT PAINTED WHERE
// THE SLOT PAINTS. IT SUBTRACTS THE STANDING TENANT'S OWN RECT, which since
// 2026-08-28 is a real fork rather than a formality: the overlay's band is the
// CEILING every time it stands (the icon row's foot down since 2026-09-09,
// whatever its listing's length)
// and the keyboard's is its four key rows, so a
// rect
// borrowed from the other tenant would either hide waveform nothing paints
// over or leave the panel painting where the waveform still runs.
//
// Both tenants' grounds are fully opaque and every waveform pass runs BEFORE
// them (the authoritative paint order, paint_handler.cpp),
// so a waveform pixel under the band is work whose result is thrown away in
// the same frame: a narrow scanner damage column crossing the band would
// otherwise pay the plate blit and every vertical over the
// band's whole height, at the panel's own tick rate. ONE GATE, ONE CLIP for
// both tenants.
//
// IT IS THE EXPOSURE GATE AND THE CLIP, NEVER A GEOMETRY INPUT: the column
// mapping and every hit test keep reading waveform_area itself (the displayed
// basis, the strictly-as-painted rule at app_state.h), so paint and hit
// cannot drift — this rect
// says only WHERE THE PIXELS MAY LAND.
//
// The band is a full-width lane flush on the bottom row's top edge, so what it
// hides off the waveform is always a BOTTOM SLICE and the answer is a rect. A
// band that reaches no higher than the waveform's own bottom (a tall window
// whose flexible gap 2 is deeper than the surface) subtracts nothing; a band
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
inline GuiRect waveform_paint_area(const AppState& a, const GuiPlatform& gui) {
    const GuiRect area    = waveform_area(a);
    const bool    overlay = folder_overlay::stands(a);
    if (!stands(a, gui) && !overlay) return area;
    const GuiRect surf = overlay ? folder_overlay::surface_rect(a)
                                 : surface_rect(a);
    if (surf.w <= 0 || surf.h <= 0) return area;
    const int hidden = (area.y + area.h) - surf.y;
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
// starts at the margin. One rule, both cases.
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

    int y = surf.y + pad;
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
