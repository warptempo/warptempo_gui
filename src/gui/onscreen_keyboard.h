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
// WHAT IT IS. A four-row Maliit-shaped keyboard (the reference is Plasma
// Mobile's, Breeze Dark), full window width, sitting DIRECTLY ABOVE THE BOTTOM
// ROW and painting over the waveform area's lower part — which the waveform's
// own passes then do not paint at all (waveform_paint_area, below). It stands while ANY OF
// THE SEVEN TEXT EDITORS stands, on a backend that asks for one, and it
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
// synthesis for a held key. So the seven editors' grammars, their red-flash
// refusals, their commit and cancel bodies and their byte caps are inherited
// whole rather than mirrored, and a new editor gets a working keyboard by
// existing.
//
// TIMING: KEYS ARE HOTKEYS AND ACT AT THE PRESS (architect: phone muscle
// memory; and the core's repeat needs the press edge). That is the modality
// ruling's own split read straight — ICONS ARE UP, HOTKEYS ARE DOWN
// (conventions.md) — with these keys on the hotkey side and the editors' own
// BUTTONS (a dialog's OK and Cancel) still chrome, still acting at the lift.
// A key held down repeats through the core exactly as a held physical key
// does, on the platform's advertised cadence.
//
// WHAT IT DELIBERATELY HAS NOT GOT: no globe and no language label (one
// layout), no hide-keyboard key (it leaves with the editor that raised it), no
// caps lock (shift is one-shot), no long-press alternates and no chords — an
// act at the press precludes the first, and a second finger is the navigation
// gesture and never reaches a key. No Left/Right keys either: backspace and
// retyping cover a one-line field.

#include "app_state.h"
#include "gui_input.h"
#include "icons.h"
#include "platform.h"
#include "render.h"
#include "viewport.h"

#include <cmath>
#include <cstdint>

namespace onscreen_keyboard {

// -- The layout table -------------------------------------------------------
//
// ONE OWNER FOR EVERY KEY'S IDENTITY: its role, the character it types, and
// how wide it is. The painter reads its cap and its icon from here through the
// two derivations below, and the press router reads its keysym and codepoint
// from the SAME `ch`, so a key cannot type one thing and say another.

enum class Role : uint8_t {
    Character,     // types `ch` — every letter, digit and symbol, space included
    Shift,         // ONE-SHOT: the next Character key is capital, then it clears
    Backspace,     // GuiKeys::BackSpace
    Enter,         // GuiKeys::Return
    Escape,        // GuiKeys::Escape
    LayerToggle,   // the `&123` / `abc` key: flips the symbol layer
    Blank,         // a slot with no key in it — paints nothing, consumes a press
};

// THE ROW IS FORTY QUARTER-UNITS WIDE. Every key's width is authored in
// QUARTERS OF A STANDARD KEY, so a row's spans sum to 40 when it fills the
// width and to less when it is inset (row 2's nine letters, the reference's own
// half-key indent). The unit itself is DERIVED from the window — the surface is
// full width by ruling, so ten keys across a 2304 px panel are 230 px each —
// which is why this is the one dimension in the product that is not authored at
// 100% and scaled: gui_scale moves the ROW HEIGHTS and the gaps below, and the
// window decides the pitch.
inline constexpr int kUnitsPerRow = 40;
inline constexpr int kRowCount    = 4;
// The widest row in either layer; the index arithmetic below reserves this many
// slots per row so a key's index is a pure function of where it sits.
inline constexpr int kMaxRowKeys  = 12;

struct KeyDef {
    Role    role   = Role::Blank;
    char    ch     = '\0';   // Character keys only; the LOWERCASE / base form
    uint8_t span_q = 4;      // width in quarter-units
};

struct Row {
    const KeyDef* keys  = nullptr;
    int           count = 0;
};

namespace detail {

// THE LETTER LAYER — the reference's own four rows.
inline constexpr KeyDef kLetterRow0[] = {
    {Role::Character, 'q'}, {Role::Character, 'w'}, {Role::Character, 'e'},
    {Role::Character, 'r'}, {Role::Character, 't'}, {Role::Character, 'y'},
    {Role::Character, 'u'}, {Role::Character, 'i'}, {Role::Character, 'o'},
    {Role::Character, 'p'},
};
inline constexpr KeyDef kLetterRow1[] = {
    {Role::Character, 'a'}, {Role::Character, 's'}, {Role::Character, 'd'},
    {Role::Character, 'f'}, {Role::Character, 'g'}, {Role::Character, 'h'},
    {Role::Character, 'j'}, {Role::Character, 'k'}, {Role::Character, 'l'},
};
inline constexpr KeyDef kLetterRow2[] = {
    {Role::Shift, '\0', 6},
    {Role::Character, 'z'}, {Role::Character, 'x'}, {Role::Character, 'c'},
    {Role::Character, 'v'}, {Role::Character, 'b'}, {Role::Character, 'n'},
    {Role::Character, 'm'},
    {Role::Backspace, '\0', 6},
};

// THE SYMBOL LAYER'S THREE. Digits on row 0 in order; the grammars' symbols
// below them, the arithmetic and separator family on row 1 and the brackets and
// joiners on row 2. THE SET IS THE PRODUCT'S OWN GRAMMARS, not a general
// symbol page: `.` and the digits spell a tempo, `#` disables a marker,
// `+ - [ ] ,` are the iteration bracket, `/` and space and `+` are the marker
// measure, `_` `:` `=` `;` `'` `|` `*` cover the settings keys, the commit
// titles and the render-entry paths.
//
// ROW 2'S LEADING SLOT IS BLANK and that is the whole answer to "what does
// shift do on the symbol page": nothing, so there is no key there. Its SPACE
// key and its comma are deliberate DUPLICATES of row 3's, which is layer-blind
// — a hand already in the symbol layer for the `/` of `12 7/8` should not have
// to look for the space bar, and a physical keyboard's own numpad settles that
// a second painted key onto the same synthesize_key road is not a second road.
inline constexpr KeyDef kSymbolRow0[] = {
    {Role::Character, '1'}, {Role::Character, '2'}, {Role::Character, '3'},
    {Role::Character, '4'}, {Role::Character, '5'}, {Role::Character, '6'},
    {Role::Character, '7'}, {Role::Character, '8'}, {Role::Character, '9'},
    {Role::Character, '0'},
};
inline constexpr KeyDef kSymbolRow1[] = {
    {Role::Character, '.'}, {Role::Character, '/'}, {Role::Character, '+'},
    {Role::Character, '-'}, {Role::Character, '*'}, {Role::Character, ':'},
    {Role::Character, '#'}, {Role::Character, '|'}, {Role::Character, '\''},
};
inline constexpr KeyDef kSymbolRow2[] = {
    {Role::Blank, '\0', 6},
    {Role::Character, ','}, {Role::Character, ';'}, {Role::Character, '='},
    {Role::Character, '['}, {Role::Character, ']'}, {Role::Character, '_'},
    {Role::Character, ' '},
    {Role::Backspace, '\0', 6},
};

// ROW 3 IS ONE ARRAY SHARED BY BOTH LAYERS — the bottom row does not change
// under the toggle, so it is not written twice. 6 + 4 + 14 + 4 + 6 + 6 = 40.
inline constexpr KeyDef kBottomRow[] = {
    {Role::LayerToggle, '\0', 6},
    {Role::Character,   ',',  4},
    {Role::Character,   ' ', 14},
    {Role::Character,   '.',  4},
    {Role::Escape,      '\0', 6},
    {Role::Enter,       '\0', 6},
};

template <int N>
constexpr Row make_row(const KeyDef (&a)[N]) { return Row{a, N}; }

inline constexpr Row kLayers[2][kRowCount] = {
    {make_row(kLetterRow0), make_row(kLetterRow1),
     make_row(kLetterRow2), make_row(kBottomRow)},
    {make_row(kSymbolRow0), make_row(kSymbolRow1),
     make_row(kSymbolRow2), make_row(kBottomRow)},
};

} // namespace detail

inline const Row& row_of(bool symbol_layer, int row) {
    return detail::kLayers[symbol_layer ? 1 : 0][row];
}

// ZERO IS THE CORE'S "NO STABLE CODE" SENTINEL and this table may not produce
// it. GuiInputCore compares an incoming stable code against two fields that
// rest at 0 when nothing is held — the armed repeat's code and the synthesized-
// left hold's — so a key whose code were 0 would match "nothing" on its own
// release and be taken for the end of a hold it never started. The Wayland
// backend never met this because an xkb keycode is 8 or more by construction;
// this table's codes are its own small integers, so the base is stated here
// rather than left to luck.
inline constexpr uint32_t kStableCodeBase = 1;

// A KEY'S STABLE PER-KEY IDENTITY, which is what the core's repeat cancel and
// the synthesized-hold end compare against (contract at GuiInputCore::
// key_event). It is the key's PLACE in this table — layer, row, column, off the
// base above — and deliberately not the keysym: two layers put different
// characters on one slot, and one character (space, the comma) sits in two
// slots, so only the place is unique per key. Bounded by
// kStableCodeBase + 2*kRowCount*kMaxRowKeys = 97.
inline constexpr uint32_t key_index(bool symbol_layer, int row, int col) {
    return kStableCodeBase + static_cast<uint32_t>(
        (((symbol_layer ? 1 : 0) * kRowCount) + row) * kMaxRowKeys + col);
}

// THE LAYER A KEY INDEX BELONGS TO — the inverse of the layer term above, and
// the one place that reads it back. Its consumer is the RELEASE, which must
// damage the key the finger pressed even when that key's own act (the layer
// toggle) has moved the live layer out from under it; asking the live layer
// there would look the key up on a page it is not on and damage nothing.
inline constexpr bool layer_of_key_index(int index) {
    return (static_cast<uint32_t>(index) - kStableCodeBase) >=
           static_cast<uint32_t>(kRowCount * kMaxRowKeys);
}

// -- The two derivations off `ch` -------------------------------------------
//
// THE CASE IS A DERIVATION AND NOT A SECOND TABLE (the state-axis rule for a
// new painted surface): the table holds the base character once, and the shift
// lamp turns it into the cap the key WEARS and the codepoint it TYPES through
// this one function, so the two can never disagree about what a shifted key is.
// Non-letters are unmoved — this keyboard has no shifted punctuation, the
// symbol layer being where the rest of ASCII lives. THAT PROPERTY IS ALSO THE
// ONE-SHOT ARM'S TEST: the press router spends the arm exactly where this
// function moved the character, so "the arm is spent by the next LETTER" needs
// no second list of which keys have a capital form.
inline char shifted_char(char base, bool shift_armed) {
    if (!shift_armed) return base;
    if (base >= 'a' && base <= 'z') return static_cast<char>(base - 'a' + 'A');
    return base;
}

// THE KEYSYM OF A CHARACTER KEY. GuiKey is the universal keysym numbering, in
// which every printable ASCII character IS its own code point — `a` is 0x61,
// `#` is 0x23, space is 0x20 — and GuiKey is ASCII CASE-FOLDED besides (the
// backend's contract, at GuiInputCore::key_event), so the LOWERCASE base is the
// keysym for a letter in both cases. That identity is why the punctuation this
// keyboard types needs no named constants in GuiKeys: a name earns its place by
// being BOUND somewhere, and nothing in the dispatch binds `#`, `|` or `_` —
// they exist only as characters an editor inserts.
inline constexpr GuiKey keysym_of(char base) {
    return static_cast<GuiKey>(static_cast<unsigned char>(base));
}

// WHAT A KEY WEARS. A Character key wears its own cap TEXT, with one exception
// that is a property of the character rather than a list: SPACE has no glyph,
// so it wears Breeze's own spacebar icon. The four function keys wear
// unmodified Breeze icons, and SHIFT's is its two faces off the one lamp bit
// (the caps pair — the reading is at the enumerators, icons.h). The layer
// toggle and the blank slot wear neither.
inline bool wears_icon(const KeyDef& k) {
    switch (k.role) {
        case Role::Character:   return k.ch == ' ';
        case Role::Shift:
        case Role::Backspace:
        case Role::Enter:
        case Role::Escape:      return true;
        case Role::LayerToggle:
        case Role::Blank:       return false;
    }
    return false;
}

inline icons::Icon icon_of(const KeyDef& k, bool shift_armed) {
    switch (k.role) {
        case Role::Shift:
            return shift_armed ? icons::Icon::KeyboardCapsEnabled
                               : icons::Icon::KeyboardCapsDisabled;
        case Role::Backspace:   return icons::Icon::EditClearLocationbarRtl;
        case Role::Enter:       return icons::Icon::KeyboardEnter;
        case Role::Escape:      return icons::Icon::DialogCancel;
        case Role::Character:
        case Role::LayerToggle:
        case Role::Blank:       break;
    }
    return icons::Icon::KeyboardSpacebar;   // the space bar's own
}

// THE LAYER TOGGLE'S CAP names the layer it goes TO, which is the reference's
// own convention and the only one that reads right on a key you press to
// leave where you are.
inline const char* layer_toggle_cap(bool symbol_layer) {
    return symbol_layer ? "abc" : "&123";
}

// -- The authored geometry --------------------------------------------------
//
// Only the VERTICAL dimensions and the gaps are authored at 100% and scaled
// like every other redesigned dimension; the key PITCH is the window's (see
// kUnitsPerRow). The proportions are the reference photograph's: a key about
// two and a half times wider than tall, gaps a tenth of the key's height.
inline constexpr double kKeyHeightPx = 40.0;   // one row's key box
inline constexpr double kKeyGapPx    = 4.0;    // between adjacent keys, both axes
inline constexpr double kPadPx       = 4.0;    // the surface's own outer margin
inline constexpr double kBorderPx    = 1.0;    // the top seam, the bottom row's own
inline constexpr double kCornerPx    = 5.0;    // the roster's radius (kIconCornerRadiusPx)

inline int key_height_px()  { return scaled_px(kKeyHeightPx, 1); }
inline int key_gap_px()     { return scaled_px(kKeyGapPx, 1); }
inline int pad_px()         { return scaled_px(kPadPx); }
inline int border_px()      { return scaled_px(kBorderPx, 1); }

// The surface's whole height: four key rows, three gaps between them, the outer
// margin at both ends, and the 1px top seam.
inline int surface_height_px() {
    return border_px() + 2 * pad_px() + kRowCount * key_height_px() +
           (kRowCount - 1) * key_gap_px();
}

// -- Standing ----------------------------------------------------------------

// DOES THE SURFACE STAND? Two terms and no third: the PLATFORM must want a
// painted keyboard (false forever on Wayland — the ruling is at that backend's
// wants_onscreen_keyboard), and one of the SEVEN editors must own the keyboard.
// EVERY paint site and EVERY hit site in the product asks this and nothing
// else, which is what makes the laptop build's behaviour identical by
// construction rather than by care.
//
// The editor term is text_editor_session() (app_state.h) rather than
// GuiInputHandler::keyboard_modal_editor_active because the painter has no
// input handler to ask; the two are the same set by construction — that
// predicate delegates to any_text_editor_active, which is exactly the seven
// this session id is taken from.
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
    // THE BOTTOM ROW'S OWN BAND, lifted by this surface's height: x and w are
    // taken from that lane rather than from a.width so the two rects are the
    // same band by construction — the lane accessors run on the CLAMPED window
    // dimensions (clamp_dims, main.cpp) and a raw a.width would disagree with
    // them on a sub-minimum window.
    const GuiRect bottom = bottom_row_area(a);
    const int h = surface_height_px();
    if (bottom.w <= 0 || h <= 0) return GuiRect{0, 0, 0, 0};
    return GuiRect{bottom.x, bottom.y - h, bottom.w, h};
}

// -- The session-change owner, and the waveform's painted rect --------------

// THE SESSION-CHANGE OWNER, and the ONE writer of the transient state's reset.
// THE TWO LAMPS BELONG TO THE EDIT THEY WERE SET IN, so a close, a reopen or a
// RETARGET of the live flag editor must clear them — and the PIXELS MUST SAY SO
// BEFORE THE NEXT PRESS IS ROUTED, because the lamps decide both which key is
// under the finger (the layer) and what that key types (the arm): a surface
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
// arm moves every letter cap's case and the layer moves every key on three
// rows.
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
    a.onscreen_keyboard.symbol_layer = false;
    viewport.invalidate_rect(surface_rect(a));
}

// THE WAVEFORM'S PAINTED RECT — waveform_area minus this surface's band, and
// the ONE OWNER of the rule that THE WAVEFORM IS NOT PAINTED WHERE THIS PAINTS.
// The surface's ground is fully opaque and every waveform pass runs BEFORE it
// (the authoritative paint order, paint_handler.cpp), so a waveform pixel under
// the band is work whose result is thrown away in the same frame: a narrow
// scanner damage column crossing the band would otherwise pay the plate blit,
// the region ink and every vertical over the band's whole height, at the
// panel's own tick rate.
//
// IT IS THE EXPOSURE GATE AND THE CLIP, NEVER A GEOMETRY INPUT: the column
// mapping and every hit test keep reading waveform_area itself (the displayed
// basis, pointer-hit-testing.md), so paint and hit cannot drift — this rect
// says only WHERE THE PIXELS MAY LAND.
//
// The band is a full-width lane flush on the bottom row's top edge, so what it
// hides off the waveform is always a BOTTOM SLICE and the answer is a rect. A
// band that reaches no higher than the waveform's own bottom (a tall window
// whose flexible gap 2 is deeper than the surface) subtracts nothing; a band
// that swallows the waveform whole answers a ZERO-HEIGHT rect, and it is the
// CLIP rather than the gate that makes that case paint nothing (rects_intersect
// can still answer true for an empty rect an exposure straddles — it compares
// edges, not areas).
inline GuiRect waveform_paint_area(const AppState& a, const GuiPlatform& gui) {
    const GuiRect area = waveform_area(a);
    if (!stands(a, gui)) return area;
    const GuiRect surf = surface_rect(a);
    if (surf.w <= 0 || surf.h <= 0) return area;
    const int hidden = (area.y + area.h) - surf.y;
    if (hidden <= 0) return area;
    GuiRect painted = area;
    painted.h = hidden >= area.h ? 0 : area.h - hidden;
    return painted;
}

// -- The ONE walk over the keys ----------------------------------------------

// THE ONE WALK OVER THE KEYS, and the reason paint and hit cannot drift: both
// go through it. `fn(index, def, rect)` is called for every key of `layer` in
// painted order, with `rect` the key's PAINTED box — the slot inset by half a
// gap on each side, so the gaps between keys are uniform and the row's ends sit
// on the margin exactly.
//
// A row whose spans sum to less than kUnitsPerRow is CENTERED in the surface
// (the reference's own half-key indent on the nine-letter row); a row that
// fills it starts at the margin. One rule, both cases.
template <class Fn>
inline void for_each_key(const AppState& a, bool symbol_layer, Fn&& fn) {
    const GuiRect surf = surface_rect(a);
    if (surf.w <= 0 || surf.h <= 0) return;

    const int gap    = key_gap_px();
    const int pad    = pad_px();
    const int key_h  = key_height_px();
    const int inner_x = surf.x + pad;
    const int inner_w = surf.w - 2 * pad;
    if (inner_w <= 0) return;
    const double unit = static_cast<double>(inner_w) / kUnitsPerRow;

    int y = surf.y + border_px() + pad;
    for (int r = 0; r < kRowCount; ++r) {
        const Row& row = row_of(symbol_layer, r);
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
                fn(key_index(symbol_layer, r, i), k, GuiRect{kx, y, kw, key_h});
            }
            q += k.span_q;
        }
        y += key_h + gap;
    }
}

// THE KEY UNDER A POINT, or -1. A press inside the surface but off every key —
// the gaps, the margins, a Blank slot's box — answers -1 and is still CONSUMED
// by the surface's own claim (the press router owns that rule); this function
// answers only "which key", never "whose press".
//
// It writes the found key's def through `out_def` so the caller needs no second
// lookup, and it walks the same for_each_key the painter does.
inline int key_at(const AppState& a, bool symbol_layer, int x, int y,
                  KeyDef& out_def) {
    int found = -1;
    KeyDef found_def{};
    for_each_key(a, symbol_layer,
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
// rect when the index is not on the given layer.
inline GuiRect key_rect(const AppState& a, bool symbol_layer, int index) {
    GuiRect found{0, 0, 0, 0};
    for_each_key(a, symbol_layer,
                 [&](uint32_t i, const KeyDef&, const GuiRect& r) {
                     if (static_cast<int>(i) == index) found = r;
                 });
    return found;
}

} // namespace onscreen_keyboard
