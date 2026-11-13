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
// ASCII (the layout table below) — its key box authored in W and scaled,
// under tuning (the `osk_key_w` and `osk_key_h` device keys, the geometry
// below; its caps' size the `osk_font_px` key since 2026-10-29 night,
// cap_word's paragraph), the block of keys SEATED WITH THE GAP BETWEEN ITS
// `5` AND `6` ON THE WINDOW'S CENTER (architect 2026-11-08, block_x_px) in a
// band that runs the window's width
// on the window's foot, its top AT OR ABOVE THE LOWER CHANNEL'S CENTER LINE
// (the height floor, 2026-11-08, edge_pad_px; the pads never under a quarter
// of osk_key_h, 2026-11-13), on the flat ground, nothing
// beside the block (the
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
// the key event itself (synthesize_key's `carried`, GuiCarriedMods,
// GuiInputCore::key_event's contract), so a copy with no selection copies
// nothing and a
// paste takes the editors' one paste body (text_editor::handle_key's chord
// arms, GuiInputHandler::apply_editor_clipboard) — and a held one does what
// a held Ctrl+C or Ctrl+V does, the editor's chord keys being one-shot
// (repeat_eligible's editor arm). SO ARE LEFT, RIGHT AND THE PASS KEY
// (architect 2026-11-08, sessions 95 / 96): LEFT and RIGHT synthesize the
// editors' Left / Right, carrying the latched Shift as the key event's own
// shift (the selection's extend, text_editor::handle_key's caret arm), and
// repeat while held; the pass key synthesizes GuiKeys::Pass, which the
// editors answer with `pass` at the caret through the one incoming filter,
// one-shot. THAT OWN CTRL IS THE ONLY CTRL A KEY HERE
// CARRIES: THE BARREL HAS NO ROAD ON ANY KEY, THIS KEYBOARD'S INCLUDED
// (architect 2026-11-08, session 94) — a press carrying it is consumed with
// no act, Shift and the page key included (GuiInputHandler::
// claim_onscreen_keyboard_press), and the barrel's word erase, select-all
// and cut went with it.
//
// TIMING: KEYS ARE HOTKEYS AND ACT AT THE PRESS (architect: phone muscle
// memory; and the core's repeat needs the press edge). That is the modality
// ruling's own split read straight — ICONS ARE UP, HOTKEYS ARE DOWN
// (GuiInputHandler::on_key) — with these keys on the hotkey side and the
// editors' own
// BUTTONS (a dialog's OK and Cancel) still chrome, still acting at the lift.
// A key held down repeats through the core's key repeat, on the platform's
// advertised cadence.
//
// WHAT IT HAS (architect 2026-11-08, sessions 95 / 96): LEFT and RIGHT, the
// caret keys; A PASS KEY (`pass` typed whole — Toggle Inherit is veiled
// under the editor); A STICKY SHIFT, a latch (a press lights it, a second
// puts it out; lit, the letters type capital and LEFT / RIGHT extend the
// selection — THE SELECTION'S ONE ROAD, the field's double tap retired the
// same day); and BREEZE GLYPHS on the function keys that act on the text
// (Tab, Bksp, Enter, Copy, Paste, LEFT, RIGHT, the pass key; cap_word).
// WHAT IT DELIBERATELY HAS NOT GOT: no language switching (one layout), no
// hide act (it leaves with the editor that raised it), no long-press
// alternates (an act at the press precludes them), no third page and no
// character past ASCII (no grammar takes one), no word-select keys (s95's,
// retired by s96 before landing), and no chords — a second finger on the
// keyboard is ignored (the pointer translation's rule, input_core.cpp) and
// never reaches a key, and the barrel's press is consumed (above).

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
#include "warp_frame_map_view.h"  // lattice_index_ties_right (the slots' tie)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace onscreen_keyboard {

// -- The layout table -------------------------------------------------------
//
// ONE OWNER FOR EVERY KEY'S IDENTITY: its role, the character it types, and
// how wide it is. The painter reads the cap a key wears from here through the
// derivations below, and the press router reads its keysym and codepoint from
// the SAME `ch`, so a key cannot type one thing and say another.
//
// THE TWO PAGES (architect 2026-10-29, on the sheets kb_f_flags and
// kb_g_symbols_repeated; rows 0 to 3 re-laid 2026-11-08, sessions 95 / 96).
// Rows counted from 0 at the top; widths in units
// (one standard key), every row LEFT-ALIGNED on the block's left edge — no
// centering of a short row, and since 2026-11-08 no stagger (row 2's
// one-unit inset is the pass key's slot):
//   FLAGS
//     row 0:  1 2 3 4 5 6 7 8 9 0 + -                                (12u)
//     row 1:  Tb q w e r t y u i o p *                                (12u)
//     row 2:  Pass a s d f g h j k l ; :                              (12u)
//     row 3:  Shift(1.5) z x c v b n m Bksp(2.5) Left Right           (13u)
//     row 4:  &123(1.5) Copy(1.5) Paste(1.5) Space(4) . Esc(1.5)
//             Enter(2)                                                (13u)
//   SYMBOLS
//     row 0:  1 2 3 4 5 6 7 8 9 0 + -
//     row 1:  Tb @ # % & _ = / \ ~ ^ *
//     row 2:  Pass ( ) [ ] { } < > | ; :
//     row 3:  Shift ' " , ? ! $ ` Bksp Left Right
//     row 4:  ABC Copy Paste Space . Esc Enter
// THE 2026-11-08 RE-LAYING: the `*` left row 0 for row 1's tail, the same
// character in the same slot on both pages (a non-letter slot, like the
// digits); the PASS KEY took row 2's old one-unit inset; LEFT and RIGHT
// stand at row 3's tail, quarters 44..48 and 48..52, flush over Enter.
// NOTHING MOVES BETWEEN THE PAGES (architect 2026-10-29: "I prefer that
// things not move around"): the symbols page is the flags page with its 26
// LETTER SLOTS re-lettered — exactly the 26 printable ASCII symbols the flags
// page lacks — and every other key, the digits, `+ - *`, `;`, `:`, `.`, Tb,
// the pass key, Shift, Bksp, LEFT, RIGHT and the bottom row, in the same slot
// on both (the page key's cap alone changing, cap_word). Shift on the symbols
// page capitalizes nothing (no letter there) but its latch still extends the
// selection with LEFT / RIGHT. ESC AND
// ENTER STAND LOWER RIGHT (architect 2026-10-29: "that is where my hand
// rests"). Both facts, and the 26, are checked at compile time
// (pages_are_well_formed, below); the pass key, LEFT and RIGHT are function
// keys, outside the character count.

// THE PAGE, the keyboard's one layout state beside the shift latch. Declared
// on AppState (this header includes app_state.h) and aliased here.
using Page = AppState::OnscreenKeyboard::Page;
inline constexpr int kPageCount = 2;

enum class Role : uint8_t {
    Character,     // types `ch` — every letter, digit and symbol, space included
    Shift,         // A LATCH (2026-11-08): lit, the letters type capital and
                   // LEFT / RIGHT carry its shift; a second press puts it out
    Backspace,     // GuiKeys::BackSpace
    Enter,         // GuiKeys::Return
    Escape,        // GuiKeys::Escape
    Tab,           // GuiKeys::Tab, bare — the prompts' completion key
    SymbolMode,    // `&123` / `ABC`: flips the two pages
    Copy,          // GuiKeys::C carrying ctrl — the editors' Ctrl+C
    Paste,         // GuiKeys::V carrying ctrl — the editors' Ctrl+V
    Left,          // GuiKeys::Left, carrying the latch's shift when lit
    Right,         // GuiKeys::Right, the same
    Pass,          // GuiKeys::Pass — the editors type `pass` (2026-11-08)
};

// WIDTHS ARE AUTHORED IN QUARTER-UNITS (one standard key, 1u, is 4), so the
// half and quarter widths of the table are whole numbers; the geometry turns
// a quarter into device px (for_each_key). THE BLOCK IS THE WIDEST ROW,
// thirteen units (rows 3 and 4).
inline constexpr int kQuartersPerUnit = 4;
inline constexpr int kBlockQuarters   = 13 * kQuartersPerUnit;
inline constexpr int kRowCount        = 5;
// The most keys a row holds (rows 0, 1 and 2's twelve since 2026-11-08); the
// index arithmetic below reserves this many slots per row so a key's index
// is a pure function of where it sits.
inline constexpr int kMaxRowKeys      = 12;

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
};

namespace detail {

// ROW 0, ONE ARRAY FOR BOTH PAGES: the digits and two of the flag grammar's
// three operators (the third, `*`, at row 1's tail since 2026-11-08).
inline constexpr KeyDef kRow0[] = {
    {Role::Character, U'1'}, {Role::Character, U'2'}, {Role::Character, U'3'},
    {Role::Character, U'4'}, {Role::Character, U'5'}, {Role::Character, U'6'},
    {Role::Character, U'7'}, {Role::Character, U'8'}, {Role::Character, U'9'},
    {Role::Character, U'0'}, {Role::Character, U'+'}, {Role::Character, U'-'},
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
    {Role::Character, U'p'}, {Role::Character, U'*'},
};
// THE PASS KEY leads row 2 on both pages (architect 2026-11-08, sessions
// 95 / 96), in what was the row's one-unit inset.
inline constexpr KeyDef kFlagsRow2[] = {
    {Role::Pass},
    {Role::Character, U'a'}, {Role::Character, U's'}, {Role::Character, U'd'},
    {Role::Character, U'f'}, {Role::Character, U'g'}, {Role::Character, U'h'},
    {Role::Character, U'j'}, {Role::Character, U'k'}, {Role::Character, U'l'},
    {Role::Character, U';'}, {Role::Character, U':'},
};
// LEFT AND RIGHT close row 3 on both pages (architect 2026-11-08, sessions
// 95 / 96), flush over Enter: Shift 0..6, the seven 6..34, Bksp 34..44, LEFT
// 44..48 and RIGHT 48..52 in quarters.
inline constexpr KeyDef kFlagsRow3[] = {
    {Role::Shift, 0, 6},
    {Role::Character, U'z'}, {Role::Character, U'x'}, {Role::Character, U'c'},
    {Role::Character, U'v'}, {Role::Character, U'b'}, {Role::Character, U'n'},
    {Role::Character, U'm'},
    {Role::Backspace, 0, 10},
    {Role::Left}, {Role::Right},
};

// THE SYMBOLS PAGE'S THREE RE-LETTERED ROWS, slot for slot the three above.
inline constexpr KeyDef kSymbolsRow1[] = {
    {Role::Tab},
    {Role::Character, U'@'}, {Role::Character, U'#'}, {Role::Character, U'%'},
    {Role::Character, U'&'}, {Role::Character, U'_'}, {Role::Character, U'='},
    {Role::Character, U'/'}, {Role::Character, U'\\'}, {Role::Character, U'~'},
    {Role::Character, U'^'}, {Role::Character, U'*'},
};
inline constexpr KeyDef kSymbolsRow2[] = {
    {Role::Pass},
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
    {Role::Left}, {Role::Right},
};

// THE BOTTOM ROW, ONE ARRAY FOR BOTH PAGES: the page key, Copy, Paste,
// Space, `.`, Esc and Enter — 6 + 6 + 6 + 16 + 4 + 6 + 8 = 52, the block.
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
constexpr Row make_row(const KeyDef (&a)[N]) {
    return Row{a, N};
}

inline constexpr Row kPages[kPageCount][kRowCount] = {
    {make_row(kRow0), make_row(kFlagsRow1), make_row(kFlagsRow2),
     make_row(kFlagsRow3), make_row(kBottomRow)},
    {make_row(kRow0), make_row(kSymbolsRow1), make_row(kSymbolsRow2),
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
// the capitals (the shift latch's) appears exactly once, and space with them
// (the function keys — the pass key, LEFT and RIGHT among them — type no
// character and are outside the count).
constexpr bool pages_are_well_formed() {
    int widest = 0;
    for (int p = 0; p < kPageCount; ++p) {
        for (int r = 0; r < kRowCount; ++r) {
            const Row& row = kPages[p][r];
            if (row.count > kMaxRowKeys) return false;
            int span = 0;
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
        if (f.count != s.count) return false;
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

// THE SEAT'S GAP, in quarters into the block: where row 0's `6` key starts,
// the gap between the `5` and the `6` (block_x_px's rule, architect
// 2026-11-08), read off the table so the seat cannot part from the keys it
// names — 20 quarters since that day.
constexpr int seat_gap_quarters() {
    int q = 0;
    for (const KeyDef& k : kRow0) {
        if (k.role == Role::Character && k.ch == U'6') return q;
        q += k.span_q;
    }
    return -1;
}
static_assert(seat_gap_quarters() > 0, "row 0 lost the `5` | `6` seat");

constexpr int page_number(Page page) { return static_cast<int>(page); }

} // namespace detail

inline const Row& row_of(Page page, int row) {
    return detail::kPages[detail::page_number(page)][row];
}

// ZERO IS THE CORE'S "NO STABLE CODE" SENTINEL and this table may not produce
// it. GuiInputCore compares an incoming stable code against two fields that
// rest at 0 when nothing is held — the armed repeat's code and the synthesized-
// left hold's — so a key whose code were 0 would match "nothing" on its own
// release and be taken for the end of a hold it never started. This
// table's codes are its own small integers, so the base is stated here
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
// damage the key the finger pressed even when that key's own act (the page
// key) has moved the live page out from under it; asking the live page
// there would look the key up on a page it is not on and damage nothing
// (finish_onscreen_keyboard_release; back 2026-11-05 with the press face).
inline constexpr Page page_of_key_index(int index) {
    return static_cast<Page>(
        (static_cast<uint32_t>(index) - kStableCodeBase) /
        static_cast<uint32_t>(kRowCount * kMaxRowKeys));
}

// THE HELD FACE IS A SLOT, NOT AN INDEX (architect 2026-10-29 night, on the
// glass: the page key never sank to its second depth; back 2026-11-05 with
// the press face, the press arc). Two key indices name the same SLOT when
// they differ in the page term alone — the same row and column, which on
// these pages is the same rect, role and width, since nothing moves between
// them (pages_are_well_formed pins it). The painter's held test is this one:
// THE PAGE KEY IS THE ONE KEY WHOSE OWN PRESS MOVES THE LIVE PAGE OUT FROM
// UNDER ITS INDEX before the paint, so an index test against the new page's
// walk finds no held key and the page key paints its lit or rest face
// under the finger; the press face must follow the finger (materials.h's
// paint_key and key_look), and the finger is on the slot.
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
// page, so the latch capitalizes there alone. THE LATCH IS NOT SPENT BY A
// LETTER (architect 2026-11-08, sessions 95 / 96: Shift is sticky, a second
// press puts it out — the one-shot arm of 2026-08-27 retired), so every
// letter typed while it is lit is capital.
inline constexpr char32_t shifted_char(char32_t base, bool shift_latched) {
    if (!shift_latched) return base;
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
// printable branch of text_editor::handle_key, into replace_selection). (The
// numbering
// past ASCII — Latin-1 its own code points, everything else 0x01000000 + the
// code point — stays here for the walk's completeness; the table types none
// of it since 2026-10-29.)
inline constexpr GuiKey keysym_of(char32_t base) {
    return base < 0x100 ? static_cast<GuiKey>(base)
                        : static_cast<GuiKey>(0x01000000u | base);
}

// WHAT A KEY WEARS: A GLYPH, A WORD OR ITS CHARACTER, decided here alone —
// cap_icon first, then cap_word, then the character (the painter's order,
// paint_onscreen_keyboard).
//
// THE GLYPH KEYS (architect 2026-11-08, sessions 95 / 96, reversing "no
// glyphs" of 2026-08-27 / 2026-09-01 / 2026-10-29 night): the function keys
// that act on the text wear a BREEZE GLYPH — Tab `format-indent-more`
// (FormatIndentMore), Bksp `edit-clear` (EditClear, the leftward key with
// its X), Enter `key-enter` (KeyEnter), Copy and Paste row 8's EditCopy /
// EditPaste, LEFT and RIGHT the chevrons GoPrevious / GoNext, the pass key
// the INHERIT key's InsertLink (redesign_button_icon's IconMarkerInherit
// arm: the same act, `pass` inherits). AT THE PROGRAM KEYS' ONE INK SIZE,
// centered on the face (paint_key_glyph, paint_handler.cpp — the one owner
// of the size, kKeyGlyphInkPerKeySide of `key_height`, never of osk_key_h),
// in the key's ink (materials.h's key_ink: the text tone at rest and under
// the press; text_on_lit on a lit face, which no glyph key wears — only
// Shift and the page key light, and they keep their words; key_dim_ink on a
// disabled key, of which there is none). A null answer: the key wears its
// word or its character.
inline std::optional<icons::Icon> cap_icon(const KeyDef& k) {
    using icons::Icon;
    switch (k.role) {
        case Role::Tab:       return Icon::FormatIndentMore;
        case Role::Backspace: return Icon::EditClear;
        case Role::Enter:     return Icon::KeyEnter;
        case Role::Copy:      return Icon::EditCopy;
        case Role::Paste:     return Icon::EditPaste;
        case Role::Left:      return Icon::GoPrevious;
        case Role::Right:     return Icon::GoNext;
        case Role::Pass:      return Icon::InsertLink;
        case Role::Character:
        case Role::Shift:
        case Role::Escape:
        case Role::SymbolMode: return std::nullopt;
    }
    return std::nullopt;
}

// THE WORD KEYS: every word on the ONE sans face in the keys' own size
// (gui_font.h's GuiFace::Keys, the `osk_font_px` tuning key's em in W, 12 by
// default, architect 2026-10-29 night; "C24", architect 2026-10-24), shaped
// through the one chokepoint like every other label — Shift, Esc, Space and
// the page key KEEP THEIR WORDS (architect 2026-11-08, sessions 95 / 96).
//
// A FUNCTION KEY THAT WEARS NO GLYPH WEARS ITS NAME (planner 2026-09-01,
// under the capitalization sweep's universal-rules spine; architect
// 2026-10-29 night, on the mocks): Shift, Esc, Space — Title case, the first
// letter alone capital — and the PAGE KEY names the page it goes TO, `&123`
// on the flags page and `ABC` on the symbols page. These caps and HELP's
// mentions of the keys are the only text that names a key at all (a
// tooltip, a card or a label never does, 2026-10-30); Enter is the
// product's one word for that key (architect 2026-10-29 night) though its
// cap is now a glyph. ONE FACE stands, every word in the keys face. This
// function is the ONE OWNER of the words; a glyph key or a Character key
// other than space answers nullptr — the painter takes the glyph from
// cap_icon above or spells the character out of the table's own `ch`
// through the one case derivation above.
//
// EVERY WORD FITS ITS KEY WITH AIR, at the defaults and at his tuning (his
// osk_key_w 24 and osk_font_px 12 are the defaults' widths and size; his
// osk_key_h 35 moves no width), and there is NO FALLBACK: no second form, no
// measure at paint, no second face. Measured (Liberation Sans Bold, the Keys
// face's file since 2026-11-04 night, gui_font.h's table; HarfBuzz's
// unhinted advances through FreeType, text_shape's road) against the key's
// face — the whole key, no outline (2026-11-04) — in the tablet's device px
// at 200 %, the key widths the slots' at the one-W gap (2026-11-04): at the
// defaults (osk_key_w 24, osk_font_px 12 — 48 and 24 device px) "Space" 71
// px in the 4u key's 198, "&123" 57, "Shift" 53 and "ABC" 52 in their 1.5u
// keys' 74, "Esc" 43 in its 72 (the slots' rounding, quarter_offset_px), and
// the widest character cap, "@", 23 in the 1u key's 48 (the capital "W"
// 23). At 100 % the key and the face scale together and every word fits as
// well. A WORD THAT FAILS ITS KEY UNDER AN EXTREME TUNING SIMPLY OVERHANGS
// IT — a tuning key's own consequence, centered on its key like every cap:
// at the default box "&123" is the first to overhang, from osk_font_px 16 W
// ("Shift" from 17, "ABC" from 18, "Esc" from 21; each threshold the least
// whole W at which the word's advance, scaled with the em, exceeds its
// key's face at the slots' rounding in W of 2026-11-06). (THE RECORD WAS
// TRIMMED, NOT RE-MEASURED, 2026-11-08: the words that became glyphs left
// it — Bksp, Enter, Tb, Copy, Paste — and the slots of the words that stay
// are unchanged by the re-laying.)
//
// SHIFT'S LAMP IS THE FACE, NOT THE CAP. The word is "Shift" latched or
// resting; what says the latch is the key's LIT FACE — THE ACCENT, `cue`,
// Default's FFAD56, its cap `text_on_lit`, Default's 111111 (architect
// 2026-11-11, session 106, the keyboard following the lamps; materials.h's
// THE KEY and key_ink), which this key and the page key (while the symbols
// page stands) wear off their lamps — a press on a lit one the accent
// darkened, `lit_press_fill`, Default's C68743, a press on an unlit key
// the press face, Default's 2A2A2A, from the press to the lift (the press
// arc, 2026-11-05) — and the letter
// caps themselves, every one of which turns capital while the latch stands.
inline const char* cap_word(const KeyDef& k, Page page) {
    switch (k.role) {
        case Role::Shift:       return "Shift";
        case Role::Escape:      return "Esc";
        case Role::SymbolMode:  return page == Page::Flags ? "&123" : "ABC";
        case Role::Character:   return k.ch == U' ' ? "Space" : nullptr;
        case Role::Backspace:
        case Role::Enter:
        case Role::Tab:
        case Role::Copy:
        case Role::Paste:
        case Role::Left:
        case Role::Right:
        case Role::Pass:        return nullptr;   // a glyph, cap_icon
    }
    return nullptr;
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
// THE KEY BOX IS AUTHORED IN W AND SCALED (architect 2026-10-29, the
// compact keyboard), like every other skin length (scaled_px, rounded at the
// element): its WIDTH, one unit's key, and its HEIGHT, every row's, the two
// TUNING DEVICE KEYS `osk_key_w` and `osk_key_h` (render.h's seat;
// device_config.h's grammar, [12, 80] each), defaults 24 and 30 W, the
// FLAGS sheet's 48 x 60 device px on the tablet. THE GAP between adjacent
// keys, both axes, IS THE PROGRAM KEYS' GAP, ONE W (render.h's key_gap_px:
// 2 device px on the tablet at 200 %, 1 at 100 %; architect 2026-11-04, the
// unit: "the same face and gap on every key surface", the tenth of the
// height of 2026-10-29 retired); THE PITCH is the key plus the gap, in W;
// a key of n units is n pitches less one gap, EACH SLOT'S EDGE ROUNDED IN W
// at the element and only then scaled (quarter_offset_px; architect
// 2026-11-06, session 77), so every slot edge stands on a whole W at every
// tuning. At an ODD pitch (an even osk_key_w) an edge an odd number of
// half-units in falls on a half W, and EVERY SUCH TIE GOES UP, toward +inf
// (quarter_offset_px), so keys of one span differ by at most one W by
// their slot and a run of 1u keys between two tied edges keeps the 1u
// width: at the defaults (the key 24 x 30 W, the gap 1, the pitch 25;
// 48 x 60, 2 and 50 device px on the tablet at 200 %) every 1u key is 24 W
// (48 device px) — the pass key, LEFT and RIGHT included — row 3's Shift
// 37 W (74) and Bksp 61 W (122), the bottom
// row's page key 37 W (74), Copy 36 (72), Paste 37 (74), Space 99 (198),
// `.` 24 (48), Esc 36 (72) and Enter 49 (98); his tuning's osk_key_w is the
// default 24 (2026-11-08), so his widths are these. (At an even pitch — an
// odd osk_key_w, 25 say, the pitch 26 W — no edge needs rounding: 1u 25,
// 1.5u 38, 2u 51, 2.5u 64 and 4u 103 W.) THE
// KEYBOARD'S KEYS DO NOT FOLLOW THE PROGRAM KEY'S face (architect
// 2026-10-29; render.h's icon_case_w_px / icon_case_h_px, the key_width x
// key_height face): their box is `osk_key_w` x `osk_key_h` alone, their gap
// the program's. (The window-derived pitch — Plasma's ten keys
// across the full width, the one unscaled dimension the product had — is
// retired 2026-10-29, git history.)
inline int key_w_px()     { return scaled_px(osk_key_width_w(), 1); }
inline int key_h_px()     { return scaled_px(osk_key_height_w(), 1); }
inline int key_gap_px()   { return ::key_gap_px(); }

// A SLOT'S LEFT EDGE, `q` quarters into the block, off the block's left edge:
// q quarter-pitches IN W, ROUNDED TO A WHOLE W at the element, then through
// scaled_px, the one road (architect 2026-11-06, session 77); so a key's
// width is the rounded span less one gap and the gap between neighbors is
// the gap exactly. Every span of the table is an even number of
// quarters, so an edge falls on a half-pitch at worst and a tie arises only
// at an odd pitch. AN EXACT TIE GOES UP, toward +inf (lattice_index_ties_right,
// warp_frame_map_view.h — the placement's selector, ruled the same day,
// architect 2026-11-06, session 69: translation-consistent, so every tied
// edge of a row moves the same way and same-span keys differ by at most one
// W — where ties to even would round them alternately, two W apart).
inline int quarter_offset_px(int q) {
    const double pitch_w = osk_key_width_w() + kKeyGapW;
    return scaled_px(
        lattice_index_ties_right(q * pitch_w / kQuartersPerUnit));
}

// THE BLOCK'S WIDTH: the widest row, thirteen units — thirteen pitches less
// one gap, a whole W at every tuning (52 quarters, no rounding): 324 W, 648
// device px at the defaults (osk_key_w 24, his tuning's too) on the tablet.
inline int block_w_px() {
    return quarter_offset_px(kBlockQuarters) - key_gap_px();
}

// THE BLOCK'S SEAT — THE `5` | `6` GAP ON THE WINDOW'S CENTER (architect
// 2026-11-08, sessions 95 / 96, replacing the block centered in the band,
// 2026-10-29): the block's left edge, window x in device px, placed so the
// gap between row 0's `5` and `6` keys (detail::seat_gap_quarters, 20
// quarters into the block) stands on the band's center — the band being the
// window's width, its center the window's. IN W, rounded at the element:
//   block_left_w = center_w − (q · pitch_w / 4 − gap_w / 2)
// with q the seat's quarters, pitch_w osk_key_w + the one-W key gap and the
// subtrahend the gap's own center off the block's left edge; the result
// ROUNDED TO A WHOLE W WITH EXACT TIES UP, toward +inf
// (lattice_index_ties_right, quarter_offset_px's own rule), then through
// scaled_px. Worked on the tablet (2304 px = 1152 W at 200 %, center 576 W)
// at osk_key_w 24 (the default and his tuning): the pitch 25, the gap's
// center 125 − 0.5 = 124.5 W in, 576 − 124.5 = 451.5 → 452 W → the block x
// 904 .. 1551 (648 px), the `5` ending on 1151 and the `6` starting on
// 1154, the gap's two cells 1152 .. 1153. AN EXTREME TUNING THAT PUSHES THE
// BLOCK PAST THE WINDOW'S EDGE IS THAT TUNING KEY'S OWN CONSEQUENCE, like an
// overhanging cap word: no clamp. (In this table the `5` | `6` gap stands
// over r | t and f | g — row 1 leads with Tab and row 2 with the pass key.
// The architect, shown that on 2026-11-09, session 97 — not t | y, as "the
// true center of the alphabet" might suggest — RULED "keep 5|6": the seat
// stays on the digit row's gap.)
inline int block_x_px(const GuiRect& band) {
    const double factor   = gui_scale_factor();
    const double pitch_w  = osk_key_width_w() + kKeyGapW;
    const double center_w = (band.x + band.w * 0.5) / factor;
    const double gap_c_w  = detail::seat_gap_quarters() * pitch_w /
                                kQuartersPerUnit -
                            kKeyGapW * 0.5;
    return scaled_px(lattice_index_ties_right(center_w - gap_c_w));
}

// THE EDGE PAD — THE BAND'S TOP AND FOOT, EQUAL, AND THE HEIGHT FLOOR'S
// ANSWER (architect 2026-11-08, sessions 95 / 96: "the keyboard band's top
// stands AT OR ABOVE the canvas's LOWER channel's center line … plus 1 W if
// necessary to make the padding symmetrical"). THE PAD'S OWN FLOOR IS A
// QUARTER OF THE KEYBOARD'S KEY HEIGHT (architect 2026-11-13, session 121:
// "the run gap, but applied vertically" — his run_gap 9 to key_width 36 —
// "more symmetric" as a quarter of the keyboard key's own height):
// edge_pad_floor_w, osk_key_h / 4 ROUNDED TO THE NEAREST W through
// std::nearbyint (the ruled off-grid rule; the default rounding mode takes
// an exact half TO EVEN — 7.5 → 8, 8.5 → 8, 9.5 → 10). THE LINE'S FLOOR IS
// A FORMULA, NEVER A CONSTANT, RECOMPUTED AT EVERY LAYOUT — key_height and
// the lane table move the line, osk_key_h moves the band and its pads'
// floor, gui_scale moves both — so it is asked of the live geometry on every
// read (no cache: every relayout road, relayout_window behind
// apply_key_height / apply_key_width / apply_run_gap / apply_osk_key_size /
// apply_gui_scale, damages the whole window):
//   the natural band = 2 × the pad floor + five key rows + four gaps;
//   shortfall_px = (the window's foot − the natural band) − the line's top
//     row (lower_center_line_top_row, render.h);
//   shortfall_px ≤ 0: each pad the floor;
//   else extra_w = the shortfall in whole W ROUNDED UP (px over the device
//     px per W), MADE EVEN by one W when odd, split equally — each pad
//     the floor + extra_w / 2 W through scaled_px.
// THE KEYS KEEP THEIR OWN BOX; only the two pads grow. ONE BAND HEIGHT under
// every editor (the flag editor's band and a dialog editor's alike; the
// modal stack on the band under a dialog editor shortened by the same
// amount, accepted). Across there is no pad: the block stands on its seat
// (block_x_px) and the ground beside it is what the window's width leaves.
// A canvas with no rows paints no line, so no line's floor (the pads the
// floor).
// Worked on the tablet at 200 % (2304 x 1440):
//   AT THE DEFAULTS (key_height 17, osk_key_h 30): the floor 7.5 → 8 W
//     (16 px); the line on rows 1056..1057, the natural band
//     16 + 300 + 8 + 16 = 340 rows from 1100 — 44 px = 22 W short, even →
//     each pad 8 + 11 = 19 W (38 px), the band 384 rows from 1056, its
//     first key row 1094.
//   AT HIS TUNING (key_height 25, osk_key_h 35): the floor 8.75 → 9 W
//     (18 px); the line on rows 1048..1049, the natural band
//     18 + 350 + 8 + 18 = 394 rows from 1046 — above the line, no shortfall
//     → each pad 9 W (18 px), the floor binding, the band 394 rows from 1046
//     (the session-96 capture's own), its first key row 1064.
//   THE FLOOR ALONE (key_height 17, osk_key_h 38): the floor 9.5 → 10 W
//     (20 px); the natural band 20 + 380 + 8 + 20 = 428 rows from 1012,
//     44 rows above the line on 1056 → each pad 10 W, the band 428 rows
//     from 1012, its first key row 1032.
inline double edge_pad_floor_w() {
    return std::nearbyint(osk_key_height_w() / 4.0);
}

// The five key rows and the four gaps between them, the band less its pads.
inline int key_rows_h_px() {
    return kRowCount * key_h_px() + (kRowCount - 1) * key_gap_px();
}

inline int edge_pad_px(const AppState& a) {
    const double  floor_w     = edge_pad_floor_w();
    const int     natural_pad = scaled_px(floor_w, 1);
    const GuiRect lane        = bottom_row_area(a);
    const int     line_top    = lower_center_line_top_row(waveform_area(a));
    if (line_top < 0) return natural_pad;
    const int natural_top =
        lane.y + lane.h - (2 * natural_pad + key_rows_h_px());
    const int shortfall_px = natural_top - line_top;
    if (shortfall_px <= 0) return natural_pad;
    int extra_w = static_cast<int>(
        std::ceil(static_cast<double>(shortfall_px) / gui_scale_factor()));
    if (extra_w % 2 != 0) ++extra_w;
    return scaled_px(floor_w + extra_w / 2, 1);
}

// The band's whole height: the edge pad, five key rows and the four gaps
// between them, and the edge pad (the floor's numbers at edge_pad_px).
// THE BAND CARRIES NO LINE (architect 2026-10-14: the raised
// Hilight line the taskbar overlay laid on its top, 2026-10-10 to 2026-10-14,
// is gone with the overlay; the placement block below).
inline int surface_height_px(const AppState& a) {
    return 2 * edge_pad_px(a) + key_rows_h_px();
}

// -- Standing ----------------------------------------------------------------

// DOES THE SURFACE STAND? One term: one of the editors owns the keyboard.
// The painted keyboard is the one key producer (architect 2026-10-30: the
// hardware keyboard is closed on the tablet), so no platform term stands
// beside it. EVERY paint site and EVERY hit site in the product asks this and
// nothing else.
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
inline bool stands(const AppState& a) {
    return a.text_editor_session() != 0;
}

// -- The keyboard's placement (architect 2026-10-14) -------------------------
//
// THE KEYBOARD STANDS ACROSS THE PROGRAM'S LANES (architect 2026-10-14, on
// the settings-keyboard mock): its band runs
// across the program lanes — the band's width the lanes' (bottom_row_area's
// x and width, the window's since 2026-10-21, when the program frame's
// columns and bottom row went) — and stands DIRECTLY ON THE WINDOW'S FOOT,
// ITS TOP AT OR ABOVE THE LOWER CHANNEL'S CENTER LINE (the height floor,
// architect 2026-11-08, edge_pad_px); THE BLOCK OF KEYS STANDS ON ITS SEAT,
// THE `5` | `6` GAP ON THE WINDOW'S CENTER (architect 2026-11-08,
// block_x_px; the compact keyboard of 2026-10-29, "small, centered, like a
// phone", stood centered until then), the edge pad above and below
// it (edge_pad_px), the band's ground beside it and nothing else there, and
// NO LINE OF ITS OWN. ITS GROUND IS THE FLAT GROUND, ITS KEYS THE FLAT
// LOOK'S KEYS, THE CAP PRINTED ON THE FACE (architect 2026-10-23; the flat
// look 2026-11-01) — paint_onscreen_keyboard, materials.h's head. Under a dialog editor the modal
// family's stack stands on it (modal_box.h's head: the box shortened to fit
// and the controls row lifted onto the band, the space between the row's
// cases and the first key row the strip pad plus this band's edge pad,
// 2026-10-22; the box as much shorter as the floor grows the pad); under
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
//   ALONE (the flag editor), on the tablet (200 %, 2304 x 1440), across x
//     0..2303, the block of keys x 904..1551 (648 wide, its seat
//     block_x_px) at osk_key_w 24:
//     AT THE DEFAULTS (key_height 17, osk_key_h 30 W, the keys 48 x 60
//     device px) the band 1056–1439 (38 + 308 + 38, the floor's pads), its
//     first key row's top 1094;
//     AT HIS TUNING (key_height 25, osk_key_h 35 W, the keys 48 x 70) the
//     band 1046–1439 (18 + 358 + 18), its first key row's top 1064.
//     Row 8 is under it
//     and unreachable: the keyboard's press claim takes the band, the ground
//     beside the block included (claim_rect).
//   UNDER A DIALOG EDITOR: the same band, the modal's stack on it (the
//     arithmetic at modal_box.h's head; on the tablet at key_height 17 and
//     the keyboard's defaults the controls row 946–1055, its faces
//     984–1017, the box 144–945, its top under the band's rows 0–143 with
//     the 72-row logo lane; at his tuning the controls row 920–1045, its
//     faces 958–1007).
// ON SCREEN IS AS PAINTED: nothing under the band answers a press or the
// touch pan zone — the waveform's painted rect stops at the
// band's top (waveform_paint_area below, which point_on_nav_surface reads),
// row 8 yields its buttons under it (paint_bottom_strip) and the keyboard's
// claim is opaque. The keys and the band's ground are paint_onscreen_keyboard's.
// Geometry only here: every rect below is a fact about geometry and asks no
// standing unless its name or its contract says so (modal_keyboard_h_px).

// THE KEYBOARD'S BAND: the program lanes' x and width (bottom_row_area's,
// the window's), standing on the window's foot (row 8's lane's foot), its
// height surface_height_px (the height floor's, at the live geometry), the
// block of keys on its seat in it (for_each_key, block_x_px). It OVERLAYS
// what stands there: nothing in the
// vertical stack moved to make room, and the waveform simply is not painted
// where this paints. The one zero rect is the degenerate one: a window with
// no width, or a height that scales to nothing.
inline GuiRect surface_rect(const AppState& a) {
    const GuiRect lane   = bottom_row_area(a);
    const int     h      = surface_height_px(a);
    if (lane.w <= 0 || h <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{lane.x, lane.y + lane.h - h, lane.w, h};
}

// THE HEIGHT THE MODAL'S STACK STANDS ON (modal_box.h's `keyboard_h`): the
// band's height while the keyboard stands, 0 otherwise. Every modal rect a
// painter or a hit site reads under a standing keyboard takes this.
inline int modal_keyboard_h_px(const AppState& a) {
    return stands(a) ? surface_height_px(a) : 0;
}

// THE KEYBOARD'S CLAIMED SURFACE: its band, whole — the pad round the keys
// and the ground beside the seated block included, so a press between
// keys, on the pad or beside the block is the keyboard's consumed nothing
// rather than a reach into what lies under the band. (Until
// 2026-10-14 the overlay's line above the band joined it when no dialog
// stood on the keyboard.)
inline GuiRect claim_rect(const AppState& a) {
    return surface_rect(a);
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
// under the finger (the page) and what that key types (the latch): a surface
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
// Shift latch moves every letter cap's case and the page re-letters every
// letter slot and the page key's cap. THE LATCH OUTLIVES NOTHING BUT ITS
// EDIT (architect 2026-11-08: sticky within the edit, reset at every
// session change here; the page key leaves it standing).
//
// `pressed_key` IS DELIBERATELY NOT CLEARED HERE. It is a fact about the
// FINGER, not about the edit, and THE KEY-UP IT OWES THE CORE IS OWED EXACTLY
// IN THIS CASE: the press that moved the session is the Enter or the Esc that
// closed the editor under itself, and dropping its release would leave the
// core's repeat arm standing on a key nothing will ever release (the contract
// is at GuiInputCore::key_event). The release path — and the touch hard-end
// beside it — is its one clearer.
//
// IT IS A NO-OP WHEREVER THE SURFACE DOES NOT STAND: one compare, no write,
// no damage.
inline void reconcile_session(AppState& a, Viewport& viewport) {
    if (!stands(a)) return;
    const uint64_t live = a.text_editor_session();
    if (a.onscreen_keyboard.lamp_session == live) return;
    a.onscreen_keyboard.lamp_session = live;
    a.onscreen_keyboard.shift_latched = false;
    a.onscreen_keyboard.page          = Page::Flags;
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
// Cancel, Esc and Enter, the flag editor's — moves this comparison and
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
// (main.cpp), ahead of every frame the backend paints, and the TICK. The
// pre-paint call is the fix: a pointer release or a key closes an editor and
// the pump paints in the SAME pass, before its next tick, so a tick-only
// comparator let the first frame after a hide paint without the band, and
// under the GL CANVAS that stranded the keyboard for good — that frame's
// canvas clip grew back over
// the band with no damage there, so the band's old texels inside the new
// clip were never uploaded (the upload rule skips the clip unless it moved
// on that frame, GuiPlatform::present), and the GPU showed the departed keys
// over a canvas that answered the press. Paid at pre-paint, the clip moves
// on the very frame whose damage covers the band, so the whole rect goes up
// cleared. The tick call stays for a standing change that declares no damage
// of its own: a frame paints only with damage, so the pre-paint would never
// run for it.
//
// A few compares per call with the slot down.
inline void reconcile_slot_standing(AppState& a, Viewport& viewport) {
    const bool live = stands(a) || folder_overlay::stands(a);
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
// while the press and the touch pan zone ask this rect where the waveform
// ENDS (point_on_nav_surface, input_pointer.cpp,
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
// covers (the trim bar, the ruler, the marker lane, the well's rows of ground and
// row 8; the icon row stands above the box, every icon greyed under the folder overlay but the
// ones it leaves live) are not spared this way — their painters
// run and the panel covers them, because they publish the roster's hit rects
// (the record is at the paint-order block, paint_handler.cpp).
inline GuiRect waveform_paint_area(const AppState& a) {
    const GuiRect area  = waveform_area(a);
    int           cut_y = area.y + area.h;
    if (modal_box_stands(a)) {
        cut_y = std::min(cut_y, modal_panel_rect(a, modal_keyboard_h_px(a)).y);
    } else if (stands(a)) {
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
// ACROSS: THE BLOCK — the widest row, block_w_px — stands on its SEAT, the
// `5` | `6` gap on the window's center (block_x_px, architect 2026-11-08;
// centered in the band 2026-10-29 to 2026-11-08), and EVERY ROW STARTS AT
// THE BLOCK'S LEFT EDGE (no centering of a short row, no stagger since
// 2026-11-08), each key `q` quarters in at quarter_offset_px(q), its
// width the next slot's offset less one gap. DOWN, the first row starts the
// edge pad (the height floor's, edge_pad_px) under the band's top and each
// next one a key and a gap lower. THE ONE READER OF THE BLOCK'S X: every
// key rect the product uses comes out of this walk (key_at, key_rect, the
// painter, the press router's damage).
template <class Fn>
inline void for_each_key(const AppState& a, Page page, Fn&& fn) {
    const GuiRect surf = surface_rect(a);
    if (surf.w <= 0 || surf.h <= 0) return;

    const int gap     = key_gap_px();
    const int key_h   = key_h_px();
    const int block_x = block_x_px(surf);

    int y = surf.y + edge_pad_px(a);
    for (int r = 0; r < kRowCount; ++r) {
        const Row& row = row_of(page, r);
        int q = 0;
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

// THE KEY UNDER A POINT, or -1, asked of the PAINTED key through the keys'
// one predicate (point_on_key, materials.h). A press inside the surface but
// off every key — the gaps, the margins — answers -1 (architect 2026-11-02,
// R11: the keyboard's gap is dead like the ground round a program key's
// face) and is still CONSUMED
// by the surface's own claim (the press router owns that rule); this
// function answers only "which key", never "whose press".
//
// It writes the found key's def through `out_def` so the caller needs no second
// lookup, and it walks the same for_each_key the painter does.
inline int key_at(const AppState& a, Page page, int x, int y,
                  KeyDef& out_def) {
    int found = -1;
    KeyDef found_def{};
    for_each_key(a, page,
                 [&](uint32_t index, const KeyDef& k, const GuiRect& r) {
                     if (found < 0 && point_on_key(r, x, y)) {
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
