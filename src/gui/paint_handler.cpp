#include "paint_handler.h"
#include "target_render.h"
#include "notifications.h"

#include "gui_font.h"
#include "folder_overlay.h"
#include "icons.h"
#include "onscreen_keyboard.h"
#include "render.h"
#include "text_editor.h"
#include "text_shape.h"
#include "time_format.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"
#include "engine/engine_geometry.h"  // kN, kRs — the seed frame mirror

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// On-screen paint handler: on_redraw and its per-strip paint passes, and
// on_resize. The off-screen surfaces
// these passes blit — the waveform plate and the flag-rect
// cache — are produced in waveform_cache.cpp. Trim paints live per frame
// (paint_trim), out of any cache.

// -- The bottom row's shaped-text tier (row 7, 2026-08-01) ------------------
//
// THE BOTTOM ROW IS ONE FACE — THE BODY FACE (architect 2026-10-05, after
// his ACID Pro 3.0 and Vegas screenshots: "no monospace face anywhere";
// gui_font.h): the clock and the render player's two time fields are the
// body face in their own sunken fields (kTimeFieldHeightPx below), the state
// line and every modal string the same face. There is no monospace face;
// the no-wiggle guarantee is the fields' own fixed widths.
//
// EVERY STRING THE STATE LINE CARRIES — the queue/render status and the
// history walk line — IS THE BODY FACE (gui_font(GuiFace::Body)) in the
// theme's label, on the row's ground a group space right
// of the clock's field (architect 2026-10-03; the block at the line's painter
// below and notifications.h are the live record). For 2026-10-02 it stood in
// a second status panel at the small face; from 2026-08-31 it was the clock's
// monospace, ONE run behind a literal pipe; before that the redesign's sans
// at the redesign's size through its three earlier homes — the bottom row to
// the tab row on 2026-08-13, the tab row to the one-day status bar on
// 2026-08-29 and the bar back into row 8's own cell that evening — none of
// which changed a glyph. Every NOTIFICATION CARD's line is the sans,
// shaped and painted through the ONE chokepoint like every other redesigned
// row (paint_notifications, 2026-08-29). (The dirty mark that rode this row
// retired 2026-10-05, Save's grey being the mark.)
//
// The no-wiggle DERIVATION — the widest digit, the widest tab letter, the
// "DD:DD.DDD" specimen — belongs to the time fields and lives at their own
// metrics (time_field_metrics), measured on the face rather than trusted to
// it.

// (The bottom row's face is the body face, gui_font(GuiFace::Body), named
// at each of its sites — the MODAL DIALOG's labels, field text and button
// words, the time fields and the state line; a run carries the font it was
// laid out on, so no context state needs selecting first.)

// Shape and paint one run at (x, baseline) in `color`. The row's plain-text
// tier — the successor of text_display::draw_line, which died with the
// monospace path it drew in.
//
// Every caller lays out from a reserved cell, a panel or a button rect, so
// the run's advance is nobody's (it was returned while row 8's state text
// butted against the clock's run, 2026-08-31 .. 2026-10-02; the clock's
// panel places it now).
static void show_row_text(cairo_t* cr, const GuiFont& font,
                          double x, double baseline,
                          std::string_view text, GuiColor color) {
    if (text.empty()) return;
    const text_shape::ShapedRun run = text_shape::shape_text_run(font, text);
    set_palette_source(cr, color);
    text_shape::show_shaped_run(cr, run, x, baseline);
}

// The same run in THE DISABLED EMBOSS (show_embossed_run, render.h) — the
// row tier's dead word.
static void show_row_text_embossed(cairo_t* cr, const GuiFont& font,
                                   double x, double baseline,
                                   std::string_view text) {
    if (text.empty()) return;
    show_embossed_run(cr, text_shape::shape_text_run(font, text), x, baseline);
}

// THE STATE TEXT IS ROW 8'S LINE ON THE GROUND (architect 2026-08-29, folding
// the STATUS BAR that had stood for that one day back into the toolbar he
// already reads — "status bars are generally the last row" was the bar's
// reasoning, and seeing it he preferred the text on the row and found the bar a
// duplicate of that panel's own foot; 2026-08-31, dissolving the separate
// CELL it had been for those two days into the clock's own monospace string;
// 2026-10-02, the Windows-95 chrome, giving it a status panel of its own at
// the small face; and 2026-10-03, taking the panel away again). The lower
// left is ONE FIELD AND A LINE, the Vegas / ACID pattern: the clock in its
// time field, the state on the row's ground beside it at the normal face,
// left-aligned a group space past the panel and clipped a group space short
// of the right block — painted by paint_bottom_row_buttons_and_clock, the
// ruling this block and that painter's own.
//
// STATE, NOT EVENTS: what is true right now, replaced as it changes, with NO
// timeouts and no clear on a key press — which is why the "revealed stale"
// class the transient tier suffered cannot exist here. EVENTS are the
// NOTIFICATION CARDS (paint_notifications, notifications.h).
//
// ONE CELL, TWO STRINGS THAT RANK: the `h` walk line wins while the view
// stands, and the render's progress line is what it carries otherwise. THE
// RESOLVED READOUT — the third state string, and the bar's right cell for its
// one day — RETIRED WHOLE with the bar: no state surface displays a resolved
// value any more, Ctrl+C copies it (its card, an event, naming the copied
// value — architect 2026-09-13) and Ctrl+J goes to the marker it came from.
//
// THE STRINGS' EARLIER HOMES, as history: they were the bottom-LEFT
// lead-in until the 2026-08-12 unification, the bottom row's right end after
// it, the TAB ROW's right-aligned STATUS CHAIN from 2026-08-13 — a
// precedence ladder that ranked FOUR tiers, then three when its TRANSIENT tier
// (the one-line refusals and reports, hidden under a progress line, revealed
// stale when it cleared and wiped on the next key press) and its CRITICAL CHIP
// (the checkpoint act's permanent red box) left for the cards on 2026-08-29 —
// and the STATUS BAR's left cell for the rest of that day. The chain, its
// right-alignment, its tabs-win-by-paint-order collision rule
// and its `h`-view overlap with the tabs all went with the bar's landing.
//
// THE DIRTY MARK IS NOT A TENANT EITHER: the row carries none since
// 2026-10-05, SAVE'S GREY BEING THE MARK (architect; plain_save_actionable,
// app_state.h). It stood here as the clock's `*` suffix from 2026-09-09, and
// in the window title before that.

// (THE FOUR MODAL EDITORS' SHARED PAINT BODY — render_bottom_strip_editor —
// DIED 2026-08-12 when the editors became dialogs: the settings, load,
// commit-title and BPM editors paint as a label + dark inset field + OK/Cancel
// buttons in paint_modal_dialog at the tail of this file, and the prompt's
// text-plus-labels line became a message + real buttons. The one-run
// prefix+pending shaping died with it — the prefix is a LABEL outside the
// field now, so the pending run shapes alone and the published click-to-caret
// geometry (AppState::DialogEditorText) is the field's own. THE MODAL IS BACK
// ON THIS ROW since 2026-08-13, but as a surface the row YIELDS WHOLE to, not
// as a tenant of the chain above: the chain's own layout has no modal term.)

// -- GuiPaintHandler::paint_flag_annotations -----------------------------

void GuiPaintHandler::paint_flag_annotations(cairo_t* cr,
                                             const GuiRect& top_strip) {
    // Flag annotations in the top strip. The marker/phase-reset flag BOXES live
    // on flag_cache.surface (rebuilt from on_tick via maybe_rebuild_flag_cache);
    // this pass is a pure blit. (Trim's bar and endcaps left this cache for the
    // live paint_trim pass.) The boxes CARRY THEIR TEXT since row 5 — the marker-text lane
    // that used to show it beneath them is gone — so the only thing painted
    // after this blit in that band is the open editor's overlay
    // (render_flag_editor_box) — which is why the EDITED BOX, and every box of
    // that marker standing to its right, are NOT in this surface at all: they
    // are skipped at cache-build time (2026-08-02, one rule for all three
    // editor kinds since 2026-09-05) rather than painted here and covered,
    // because the overlay is narrower than the committed box whenever the
    // edited text is shorter and the boxes past it ride its edge. Like the other
    // caches, the surface may be null on the very first paint after a load
    // (before the first rebuild fires); the blit is skipped and the background
    // shows through for that one frame.
    if (flag_cache.surface) {
        cairo_save(cr);
        cairo_rectangle(cr, top_strip.x, top_strip.y,
                        top_strip.w, top_strip.h);
        cairo_clip(cr);
        cairo_set_source_surface(cr, flag_cache.surface,
                                 top_strip.x, top_strip.y);
        cairo_paint(cr);
        cairo_restore(cr);
    }
}

// -- The redesigned rows: paint_menu_row / paint_icon_row ------------------

namespace {

// THE SHARED TEXT FACE is the one face owner's body face (gui_font.h):
// Nimbus Sans at its vertically matched em at every scale (architect
// 2026-10-06). Every row names it through that owner, gui_font(GuiFace::Body)
// at the live scale.

// (The authored-length -> device-pixels conversion every dimension below takes
// is scaled_px, render.h — the ONE conversion the whole scale axis shares.)

// (THE ACCENT'S FOCUS FORK — accent_for_focus — retired 2026-10-03 with the
// inactive selection face: ONE SELECTED PAIR, FOCUSED OR NOT, the record at
// render.h's palette block.)

// ROW 1. The row height itself lives in render.h as kMenuRowHeightPx,
// because main.cpp's lane table needs it.
//
// THE CSS FLOAT MODEL is the ruled layout vocabulary (architect 2026-07-31): a
// flat button FILLS ITS WHOLE ROW and no margin or inset exists unless the
// architect states one. An anchor's rectangle therefore spans the full height
// of the button's row, the whole lane (kMenuRowHeightPx — render.h carries the
// ruling), flush under the caption, its label padded this much
// each side, and the icon row's ground begins on the next pixel row. 7
// WINDOWS PX A SIDE (architect 2026-10-02, the unit's change: the laptop
// pixel's 10 re-authored to the device width it had on the tablet).
constexpr double kMenuLabelPadPx   = 7.0;   // per side, sets the button width

// THE MENU ROW'S BUTTONS, in painted order — flush from the row's left edge and
// ADJACENT WITH NO GAP, the css float model's default (the architect states a
// gap where one exists; row 2's 2px invisible separator is the only one in the
// redesign so far, and row 1 was never given one).
//
// THE PRODUCT'S TEXT RULES, STATED ONCE AND HERE (architect 2026-08-01,
// generalizing the row-by-row carve-out that started at this row; completed
// 2026-09-01 by the capitalization sweep, which retired every declared
// exception the domain had left — universal rules, an exception in user-facing
// text being the sign that the thing carrying it is poorly designed; joined
// 2026-09-03 by the dropdown items' Title Case, declared that morning as the
// ONE exception the product would allow itself; and SETTLED THE SAME EVENING,
// when a research pass over kdenlive's own strings found the two-class rule
// that explains them and the architect took it whole — so THE DROPDOWN ITEMS
// STOP BEING AN EXCEPTION AND BECOME AN INSTANCE, one class of a rule that was
// always kdenlive's, as this product's whole text convention is. Every other
// site states its own class and points here.
//
//   * A CONTROL'S NAME IS TITLE CASE — a dropdown item ("Open Project",
//     "Paste Phase Reset State", "Max Waveform Height"), a modal row's WORD button ("Copy to
//     Clipboard"), a panel's title, and A TOOLTIP THAT NAMES A GLYPH BUTTON'S
//     ACT ("Drop Marker (S)", "Go to Start (Home)", "Toggle Grid Iterations (I)").
//     The evidence is kdenlive's own: the Title Case hover on an icon button
//     IS the QAction's name echoed back by Qt, while every tooltip kdenlive
//     writes by hand is a sentence. A LAMP'S "Toggle <Mode> (<key>)" IS A NAME
//     under this — Qt names a checkable action the same way ("Show Audio
//     Thumbnails") — the lamp FACE, never the words, carrying the state.
//   * A DESCRIPTION IS SENTENCE CASE — a label, a field prefix, the SECOND
//     modifier line of a tooltip (always a whole sentence with terminal
//     punctuation: "Press Shift for the A/B audition."), a card, a prompt (a
//     question), the row-8 state cell, and a panel's row labels and prose.
//     A TOOLTIP'S FIRST LINE IS NEVER IN THIS CLASS any more (architect
//     2026-09-12): it named a STATE or a REASON on a handful of buttons until
//     then ("At the trim end (End)", "Markers are placed in source view (S)",
//     "Committing the checkpoint (Ctrl+S)"), and that whole class was ruled
//     out — a tooltip is the act's NAME, the grey is the message and the
//     reason is the key's card — so the only sentence-case text a tooltip
//     carries is its second line.
//   * SO ONE ACT SPLITS BY SURFACE, and that is the rule doing its work rather
//     than a carve-out: a CARD naming an act stays a sentence because a card
//     is a description ("BPM iterations work in source view"), while the
//     TOOLTIP naming the same act is that control's name ("Render Grid
//     Iterations (Ctrl+Alt+R)"), which is also the icon-row button's own
//     ("BPM Iterations (Ctrl+B)"). No reader compares any of these as strings — the
//     three item
//     tables are read by index (kFilePopupItems, kEditPopupItems,
//     kSettingsPopupItems, app_state.h) —
//     so each surface is free to spell its own class.
//   * TITLE CASE SPELLING: principal words capitalized; articles,
//     conjunctions and prepositions of three letters or fewer lowercase unless
//     they lead ("Copy to Clipboard", "Load in Place", "Save and Commit", "Up
//     a Folder", "Toggle Add to Selection"); a hyphenated compound capitalizes
//     both halves ("Toggle Read-Only (O)"); a single word is the same string
//     in either class and settles nothing. The chord suffix "(Ctrl+S)" is not
//     part of the name and is spelled by its own owner.
//   * ACRONYMS KEEP THEIR CAPS wherever they fall, in either case ("BPM
//     iterations work in source view", "GUI Scale", "URL").
//   * DATA IS VERBATIM: user-authored marker labels, titles and filenames,
//     literal settings and config KEY names shown as keys (`projects_path`),
//     and the routing/category tokens that are data rather than prose.
//   * KEY NAMES ARE QT'S, and so kdenlive's — Esc, Del, Return, Backspace,
//     PgUp, PgDown, Space, Tab, Home, End — with a BARE LETTER UPPERCASE and
//     punctuation naming the CAP rather than the stamped symbol ("Shift+/").
//     The spelling's one owner is spell_chord's head (gui_input.h).
//   * AN APPENDED REASON IS LOWERCASE: "<Act> refused: <reason>" is ONE
//     sentence, so its tail starts no second one. A producer whose string is
//     ever used WHOLE is itself a sentence and capitalizes at that producer;
//     the system's own words (`ec.message()`, `strerror`) arrive capitalized
//     and are the accepted class. The card-side statement is notifications.h's,
//     and the one seam helper is lowercase_initial (notifications.h).
//
// THE SUCCESSION IN ONE SENTENCE: sentence case with a Title Case pair
// (2026-08-01), the pair retired for sentence case with nothing left to except
// (2026-09-01, "Save and Commit" losing its capital C), the dropdowns carved
// back out as the one declared exception (2026-09-03 morning), and that
// exception generalized the same evening into the class it had always been —
// which is what put "Save and Commit" back, a NAME this time rather than a
// favourite.
//
// THE CASE SPLIT IS OVER (2026-08-02): the terminal round landed, so
// stderr/stdout prose follows the same rules and there is ONE set for both
// surfaces.
struct MenuButtonDef {
    RedesignButton id;
    const char*    label;
};
// SETTINGS SITS LAST (architect 2026-08-03, moving Settings behind the
// NAVIGATION anchor that then sat between it and File — the application's
// own menus end at Settings; his 2026-09-03 restatement added HELP after it
// as the shell's last menu for the Help anchor that stood 2026-09-03..09, and
// with that anchor's deletion the rule is the one clause again). The
// float is adjacent with no gap and the walk below is a pure shaped-run walk, so
// the order lives HERE and in the roster enum (app_state.h, whose comment states
// that enum order IS painted order and carries the rule in full) and in nothing
// else — no width, pad or anchor term reads it. THE FLOAT IS THREE BUTTONS
// SINCE 2026-09-09, when the Help anchor was deleted with its menu; it was
// four from 2026-09-04 (the Iterations anchor deleted), five from 2026-09-03,
// and two from 2026-08-15, when the Navigation anchor was deleted the same
// way.
constexpr MenuButtonDef kMenuButtons[] = {
    // THE FILE MENU (architect 2026-08-13) — the row's THIRD dropdown when it
    // landed, and one of TWO since the Navigation anchor's deletion on
    // 2026-08-15 — a COMMAND MENU of ONE row,
    // "Quit", in the slot the Quit BUTTON held from 2026-07-31: row 1 paints no
    // held face, so a button acting at the lift said nothing while it was down,
    // and the standard home for Quit is a File menu (kdenlive's own). Nothing
    // else moved — the label is shorter, so the float is 3px narrower at 100%.
    {RedesignButton::File,       "File"},
    // THE EDIT MENU (architect 2026-08-20) — the row's THIRD dropdown again,
    // painted between File and Settings, the standard order and kdenlive's own.
    // A COMMAND MENU of THREE rows, the propagate family whole, and a
    // RELOCATION: IconCopy and IconPaste were deleted from the icon row in the
    // same ruling, so this menu is those commands' one pointer home rather than
    // a second road to them. Nothing here needed a width or pad term — the row
    // is one left-to-right accumulation over this table.
    {RedesignButton::Edit,       "Edit"},
    // (THE ITERATIONS MENU WAS THE ROW'S FOURTH DROPDOWN from 2026-08-27 to
    // 2026-09-04, painted between Edit and Settings — a COMMAND MENU of TWO
    // rows, "BPM Iterations" and "Grid Iterations", landing as "Series" and
    // rebranded 2026-08-31, its enumerator keeping the first spelling
    // throughout. The architect DELETED IT once the icon row had room again
    // and both commands went back to that row as a group of two buttons, so
    // the 2026-08-27 relocation runs in reverse and the doctrine it satisfied
    // is satisfied from the other side. This table simply lost a row: the
    // float is one left-to-right accumulation over it, with no width, pad,
    // total or anchor expression to update.)
    // (THE SECOND DROPDOWN, "Navigation" — architect 2026-08-02, a COMMAND MENU
    // of the zoom and stepping commands — painted between these two from that
    // day until 2026-08-15, when the architect deleted it whole: every one of
    // its seven rows had grown a button, so the menu was a slower path to
    // commands that all had one. Like Settings its action was a popup toggle
    // rather than a chord, and it shared the one popup state — see
    // AppState::Dropdown. Its removal is one row here and one enumerator there;
    // no width, pad or anchor term reads this table's length.)
    {RedesignButton::Settings,   "Settings"},
    // (THE HELP MENU — architect 2026-09-03 — was the row's FIFTH dropdown
    // and the only one ever painted to the RIGHT of Settings, a COMMAND MENU
    // of ONE row, "AV Sync Stats", which was its chord, Shift+L. The
    // architect DELETED IT with the 2026-09-09 top strip relayout on the
    // Iterations anchor's own precedent: a command with an icon-row road —
    // the Play renders button's shift-click and long press — does not also
    // live in the menu row. (The panel itself went on 2026-09-30.) This
    // table simply lost a row: no width, pad,
    // total or anchor expression reads its length.)
};

// (ROW 2 — THE TOOLBAR — IS DELETED: 2026-08-12, the grand relayout's roster
// commit. The labeled Save / Undo / Redo / Render lane of 2026-07-31
// dissolved into the ICON ROW's first group of four glyph buttons
// (kIconRowButtons below), same chords and face machinery in the row's boxes,
// the old labels living on as their tooltips. Its painter, its layout
// constants, ToolbarButtonDef and kToolbarButtons went producer-less with
// it; what SURVIVES of row 2's measured anatomy is the MODAL DIALOG BUTTONS'
// box — kModalBtnBoxPx and the two label pads at the kModal* block below,
// which used to read the row's constants and now own the numbers, with the
// derivation recorded there. The crops and the full row-2 record are git
// history.)

// A BUTTON'S BOX — THE ONE PAINTER of the chrome's button faces (architect
// 2026-10-02, the Windows-95 design at the Windows pixel): the face filled,
// then its two-line edge, and the answer is what the glyph or label must sit
// on and how far it shifts. TWO FAMILIES (the edge grammar at render.h's
// palette head): a TOOLBAR button — the two roster rows, the on-screen
// keyboard's keys and the caption's three buttons (Windows' DFC_CAPTION,
// 2026-10-05) — wears the SOFT edges (DrawEdge's BF_SOFT, Windows'
// toolbar), a PUSH button — the dialogs' — the PLAIN ones. Three faces and
// no hover:
//   REST     — RAISED on the ground, glyph unshifted. EVERY button rests
//              raised, enabled or disabled: a disabled button keeps its edge
//              and changes only its glyph (engraved on a toolbar button,
//              icons::draw_engraved, or its label embossed,
//              show_embossed_run — Windows' DSS_DISABLED for both).
//   CHECKED  — a toggled-on button (the roster's `selected`, the player's
//              Repeat One, the keyboard's armed keys): SUNKEN over Windows'
//              checked face — the ground dithered with Hilight in one device
//              px cells (paint_checker_rect), its phase at the button's
//              top-left so two checked neighbours tile alike — the glyph one
//              Windows px down and right.
//   PRESSED  — a live press (AppState::ChromePress's face, the modal arm, a
//              held key): SUNKEN on the plain ground, the glyph shifted the
//              same — Windows' pushed button. It wins over the checked face
//              while it is held.
enum class ButtonFamily { Toolbar, Push };
struct ButtonBoxFace {
    int      shift;   // the glyph's down-and-right offset, device px
};
ButtonBoxFace paint_button_box(cairo_t* cr, const GuiRect& r, bool lamp,
                               bool pressed, ButtonFamily family) {
    const bool down = lamp || pressed;
    paint_cell_rect(cr, r, palette().ground);
    if (lamp && !pressed)
        paint_checker_rect(cr, r, r.x, r.y, palette().hilight,
                           palette().ground);
    if (family == ButtonFamily::Toolbar) {
        if (down) paint_relief_soft_sunken(cr, r);
        else      paint_relief_soft_raised(cr, r);
    } else {
        if (down) paint_relief_plain_sunken(cr, r);
        else      paint_relief_plain_raised(cr, r);
    }
    return ButtonBoxFace{down ? relief_line_px() : 0};
}

// THE ONE AS-PAINTED COVERAGE TEST (architect 2026-09-24), read by every
// publisher of an as-painted stash that a per-tick comparator replays — the
// roster's faces (publish_button_face) and the open dropdown's item verdicts
// (paint_dropdown). A stash may be refreshed only on a frame whose clip
// repaints every pixel the stash describes, and THOSE PIXELS ARE THE RECT'S
// DRAWABLE PART, the rect intersected with the surface — never the logical
// rect whole: cairo bounds every clip by the surface, so a rect running past
// the window's edge (a dropdown taller than a short window at a large
// gui_scale, the icon row's rightmost buttons cropping past the tablet's fit
// ceiling) could never be covered, its stash would never publish, and the
// comparator would invalidate it on every tick for as long as it stood. A
// rect with NO drawable part — empty, or wholly off the surface — refreshes
// unconditionally: it has no pixel that can be stale, and refreshing is what
// keeps the comparator from thrashing on it.
static bool clip_covers_drawable(cairo_t* cr, const AppState& app,
                                 const GuiRect& rect) {
    const int x1 = std::max(rect.x, 0);
    const int y1 = std::max(rect.y, 0);
    const int x2 = std::min(rect.x + rect.w, app.width);
    const int y2 = std::min(rect.y + rect.h, app.height);
    if (x2 <= x1 || y2 <= y1) return true;
    double cx1 = 0.0, cy1 = 0.0, cx2 = 0.0, cy2 = 0.0;
    cairo_clip_extents(cr, &cx1, &cy1, &cx2, &cy2);
    return static_cast<double>(x1) >= cx1 && static_cast<double>(y1) >= cy1 &&
           static_cast<double>(x2) <= cx2 && static_cast<double>(y2) <= cy2;
}

// THE BUTTON-FACE PUBLICATION, one writer so a row cannot forget a field: the
// painter stashes the rect it painted plus the three face bits it is painting
// (redesign_button_enabled / redesign_button_selected /
// redesign_button_glyph_swapped — the same
// predicates main.cpp's per-tick drift comparator replays), and returns the
// face so the caller reads .enabled / .selected back for its own paint. Every
// row painter publishes through here at the top of its per-button body; the
// comparator's vector is total over the roster, so the stash is written for
// every id — a button with no selected state simply stores the predicate's
// constant false. (The tab row's hand-copied stash once omitted `selected`,
// which made the comparator disagree with the painter on the active tab EVERY
// tick — a permanent idle full-top-strip repaint. This owner is why that class
// of omission cannot recur.)
//
// THE BITS REFRESH ONLY WHERE THE PIXELS DO (2026-08-15, the transport-pair
// staleness fix): on_redraw runs once per damage rect under a Cairo clip to
// exactly that rect, while each row painter runs WHOLE whenever ANY rect
// intersects its lane — so a narrow damage (the clock cell at scanner cadence,
// the tab row's status-chain band) used to run this publisher for buttons
// whose pixels the clip never touched, stamping the stash LIVE over pixels
// still wearing the OLD face. The comparator's premise — the stash is what the
// painter last PAINTED — was thereby false, and any enabled/selected flip
// whose own edge damaged a sliver of the lane (a click act's stop damaging the
// clock cell) was masked forever: stash equal to live, pixels stale, repaired
// only by an unrelated full-lane damage such as a hover. The gate below
// restores the premise at the one writer: a face whose DRAWABLE rect (the
// part on the surface, clip_covers_drawable above) is not fully inside the
// current clip keeps its as-painted bits, the comparator sees the
// drift on the next tick, and its own full-strip damage is what repaints and
// republishes — the repair mechanism the comparator was always documented to
// be. An EMPTY rect refreshes unconditionally (as does one wholly off the
// surface): it publishes "not painted at all", no pixel can be stale for it, and refreshing is what keeps the
// comparator from thrashing under a standing modal. TWO PRODUCERS of an empty
// rect: THE MODAL YIELD (since 2026-08-18; the contract is at
// paint_bottom_strip's yield branch), since 2026-10-01 the icon row's
// OVERFLOW RULE at a window too narrow for the row, a member the view group
// covers whole (kIconRowViewGroup), and since 2026-10-05 the icon row's
// HISTORY STAND-INS, a member that does not stand in the current state
// (kIconRowHistoryStandIns).
//
// THE CLIP TEST SURVIVED THE FACE-POLICY REVERSAL THAT FOLLOWED IT, and that is
// deliberate rather than an oversight: it landed in the same arc that made the
// transport row honest, and the architect reversed that policy whole hours
// later (redesign_button_enabled, app_state.h). This is NOT part of it. It is
// not a policy choice at all — it is what makes ANY face update reliable, for
// every row and every bit, and the stale-row bug it fixed had nothing to do
// with which buttons grey. Do not revert it along with the policy.
//
// AND IT IS LOAD-BEARING AGAIN RATHER THAN MERELY HISTORICAL: the transport's
// state moved axis TWICE more the same day — onto the SELECTED bit with the
// radio ruling, and onto the GLYPH with the collapse of play and stop into one
// button — so the very same masking edge (a click act's stop, or the natural
// end-of-song teardown, damaging only the clock cell) now flips that button's
// GLYPH under a clip that redraws no button. Same gate, same repair, the third
// term; `glyph` (the glyph index since Save grew a third, 2026-09-27) is
// stashed here for exactly that reason and its whole argument is at the
// predicate (app_state.h).
//
// AND THE INPUT CLAIMS ON THESE BITS (architect 2026-09-24, strictly
// as-painted): the roster's press, lift, hold-repeat and menu-row slide read
// the stash this publishes rather than the live predicates,
// so the gate is what keeps a press agreeing with the pixels under it — a
// bit stamped over pixels that never took it would now misdirect a press as
// well as blind the comparator.
//
// IT TOOK A GuiPlayback WITH THAT POLICY, GAVE IT BACK WITH IT, AND TOOK IT
// AGAIN WITH THE POLICY'S REVERSAL: the PLAY button's honest arm is the only
// reader of the object down this path (redesign_button_enabled asks
// playback_launch_playable about the bound preview buffer's domain), so the
// parameter left with its one producer on 2026-08-15 rather than resting
// unread — the same move the GuiAudio parameter of that predicate made a
// revision earlier — and both returned on 2026-08-30 under the
// truthful-buttons ruling, with a GuiTargetRender beside them for the target
// preview's readiness (the painter's own member since that day). `cr` stays
// because the clip test is the survivor of every move.
AppState::RedesignButtonFace& publish_button_face(
    cairo_t* cr, AppState& app,
    const GuiAudio& audio, const GuiPlayback& playback,
    const GuiTargetRender& target_render,
    RedesignButton id, const GuiRect& rect) {
    AppState::RedesignButtonFace& face =
        app.redesign_buttons[redesign_button_index(id)];
    face.rect = rect;
    if (clip_covers_drawable(cr, app, rect)) {
        face.enabled  = redesign_button_enabled(app, audio, audio.total_frames(),
                                                playback, target_render, id);
        face.selected = redesign_button_selected(app, id);
        face.glyph = redesign_button_glyph(app, id);
    }
    return face;
}

// ROW 4 — THE ICON ROW: Windows 95's menu-bar-plus-toolbar stack at the
// Windows pixel (architect 2026-10-02; the etched pairs and WordPad's 3 / 3
// air 2026-10-05). Its metrics — the 23 x 22 case with the 16-px glyph at
// (3, 3), the three px of ground above and below it, the eight px between
// groups, the two etched line pairs — live in render.h's icon-row block,
// where the lane table and the notification card read them too (the
// toolbar band — air, case, air — alone, never the etched lines); this row
// spells none of them.
//
// THE VERTICAL STORY: the case stands on the top etched pair and the
// toolbar's own three-px air (icon_case_top_offset_px), so its top is the
// lane's top plus that offset — NOT scaled_px(5): the bottom row shares only
// the toolbar band (icon_row_content_h_px, the air, the case, the air), with
// neither etched line nor the icon row's foot air, so its own case sits at
// its content's top plus the same air alone.
//
// THE HORIZONTAL WALK: the buttons of a group TOUCH, and between two groups
// stand eight Windows px of bare ground with NO SEPARATOR (architect
// 2026-10-02, the Y / Z / AB sets: the etched separators retired with the
// gaps). The row opens with its 8-px pad — icon_row_pad_x
// (paint_handler.h), which lives in the header because the BOTTOM ROW reads
// it too since 2026-08-14.

// THE PAINTER'S HALF OF THE ICON-ROW ROSTER: each button's id and its content,
// an ICON of the product's own set (icons.h: the 16-unit cell, drawn at the
// case's 16 Windows px). The press claim's chord table (input_pointer.cpp) is the
// other half; both key off the same ids.
//
// WHAT LEADS A BUTTON IS NOT HERE ANY MORE (2026-08-13): the struct carried an
// IconRowLead column — First / Gap / Separator — of which only Separator ever
// meant anything to the walk, First and Gap taking the same branch. The row's
// GROUP BOUNDARIES moved to redesign_button_opens_icon_group (app_state.h)
// when the collapse rule became a whole-group question and needed to ask them
// too; one source now answers both the dividers and the collapses.
//
// EVERY BUTTON IN THIS ROW IS AN ICON BUTTON since 2026-08-11. The struct
// carried a `glyph` string too — a shaped sans LETTER centered on both axes,
// selected by being non-null — for the four view radios, which were the row's
// only letter faces from its first day. The architect gave them glyphs that
// day, which left the letter arm with no producer, so the field and the painter's shaped
// branch below went with it rather than standing as a facility nothing uses.
struct IconRowDef {
    RedesignButton id;
    icons::Icon    icon;
};
constexpr IconRowDef kIconRowButtons[] = {
    // THE TOOLBAR GROUP — the row's FIRST since the 2026-08-12 grand
    // relayout dissolved row 2 (architect: the labeled lane goes, "the icon
    // to represent all those various meanings"): Save, Undo, Redo and Render at
    // the row's left, the SAME chords, gates, disabled derivations and
    // stateful faces the labeled buttons carried — only the FACE is a glyph
    // in the toolbar case now (Save's VcsCommit swap and Render's DialogCancel
    // swap ride redesign_button_icon below; MediaRecord serves BOTH plain
    // render and the iteration sweep by the architect's same-day ruling, the
    // tooltip alone forking). The old labels are the tooltips. SINCE
    // 2026-09-29 THE FOUR ARE TWO GROUPS (architect, after his accidental
    // Save presses at the tablet's 200 %): Save alone, then a separator, then
    // Undo leading Redo and Render (redesign_button_opens_icon_group) — one
    // separator slot onto the walk where a 2px gap stood.
    {RedesignButton::Save,       icons::Icon::DocumentSave},
    {RedesignButton::Undo,       icons::Icon::EditUndo},
    {RedesignButton::Redo,       icons::Icon::EditRedo},
    // COPY RESOLVED VALUE (architect 2026-09-29), BETWEEN REDO AND RENDER —
    // kdenlive's own order, Save | Undo, Redo, Copy, …: Ctrl+C (edit-copy,
    // the focused marker's resolved value to the clipboard). It came up from
    // the bottom row's verb group that evening and stood after Render for an
    // hour beside EDIT FLAG (text-field, bare Return), which the architect
    // then deleted — the flag editor's roads are Return and the double-click
    // on the flag. One box and one 2px gap off the walk, no separator moving.
    {RedesignButton::IconCopyValue, icons::Icon::EditCopy},
    {RedesignButton::Render,     icons::Icon::MediaRecord},
    // (THE TWO VIEW LAMPS SHARED ONE GROUP here from 2026-09-04 to 2026-09-15
    // — IconAudioView wearing document-import lit in Target, IconMarkerColumn
    // chronometer-start lit in Phase Reset, one button per axis where four
    // radios had stood before that day's collapse. The architect deleted the
    // whole category 2026-09-15, group slot and separator with it: the three
    // view selectors — the keys and the VIEW GROUP at this row's right end
    // (kIconRowViewGroup below) — are the axes' only faces now, and that
    // group wears the radios' 2026-08-11 glyphs again; the picks and their
    // runners-up are recorded at icons.h's enum.)
    // (THE ROW'S GROWTH, in brief: an earlier ZOOM PAIR sat after the radios
    // 2026-08-01..02 and was deleted under the no-duplicate-commands ruling —
    // superseded for today's zoom GROUP by the 2026-08-12 relayout order, at
    // that group below; the history button arrived 2026-08-04, its group grew
    // through 2026-08-08, the trim button opened its group 2026-08-11 at
    // seventeen buttons in six groups, and the 2026-08-12 relayout landed the
    // toolbar four above plus the zoom and marker-verb groups below —
    // twenty-nine members in nine groups then, of which the mode-collapsing roster
    // painted a subset per frame. The 2026-08-13 revision moved the history
    // group left, ahead of the mass-marker category; 2026-08-14 took the
    // group's four companions to the BOTTOM ROW, brought the READ-ONLY toggle
    // in from the tabs and put the opener last, which is TWENTY-SIX members in
    // EIGHT groups, all painted, every frame; 2026-08-16 made it TWENTY-SEVEN
    // by filling the trim group's second slot with the Show trim region
    // button, the group count unchanged; and the 2026-08-18 ROSTER RELAYOUT
    // brought it back to TWENTY-SIX in EIGHT — the scissors deleted, the four
    // MARKER VERBS gone to the bottom row with their group, the four HISTORY
    // COMPANIONS returned behind the opener in a group of its own — and the
    // WALK RADIOS made it TWENTY-EIGHT in EIGHT later that day, landing inside
    // the history group between the opener and the cumulative toggle; the
    // WAVEFORM MAGNIFICATION three closed the zoom group on 2026-08-26 — the
    // stepping pair, then the reset later the same day — which was TWENTY-NINE
    // in EIGHT, and the reset's deletion on 2026-08-27 put it back to
    // TWENTY-EIGHT in EIGHT. THE SERIES RELOCATION later that same day is what
    // makes it TWENTY-SIX in SIX: the BPM and ITERATION buttons left the
    // roster for the new menu, and FOLLOW and the SHOW TRIM REGION button
    // joined the zoom group, dissolving two separator-led groups into it; the
    // KEEP-CENTERED LAMP (2026-08-31) lands beside Follow at that group's
    // tail, TWENTY-SEVEN in SIX. The walk's own paragraph at paint_icon_row
    // carries the current count.)
    // THE ZOOM GROUP OPENS HERE, on the separator the TRIM GROUP held from
    // 2026-08-11 — the scissors opened it then, the Show trim region button
    // filled it on 2026-08-16, the scissors were deleted on 2026-08-18, and on
    // 2026-08-27 (architect) that one member merged with the zoom buttons
    // behind it by DELETING THE BOUNDARY IN FRONT OF THEM. The Show trim
    // region button itself — bare `[`, wearing tool-rect-selection — was
    // deleted on 2026-09-22 (the tablet's pen reaches the trim bar), one box
    // and one 2px gap
    // off the walk, no separator moving; FULL ZOOM OUT leads the group. What
    // the group collects is the VIEWPORT CLASS — the two zoom commands, the
    // magnification lamp, FOLLOW and RESTRICT UNDO — all in one
    // separator-led run. THIS TABLE IS THE
    // ROW'S PAINTED ORDER — the walk
    // below is a plain accumulation over it — so a reorder is rows swapping,
    // plus the group's leader in redesign_button_opens_icon_group
    // (app_state.h) and the roster enum's own order, which the three keep in
    // step. No count, no gap and no width follows a swap.
    // FULL ZOOM OUT (2026-08-12, the grand relayout — the architect's live
    // placement, "the rest in the icon row, after the trim"): bare `0`,
    // Shift+0 Reset Trim on its shift press, the command's pointer home (the
    // Navigation dropdown that duplicated it was deleted 2026-08-15; the
    // record is at kFilePopupItems, app_state.h). The stepped zoom buttons in
    // front of it and their keys `=` / `-` were removed 2026-09-25
    // (architect: zoom is on every surface — the Ctrl+drag, the pinch, the S
    // Pen's button-held drag), two boxes and two 2px gaps off the walk and no
    // separator moving. WORKING-ZOOM CENTER (bare `c`, zoom-original) stood
    // behind it until 2026-09-29, when the architect moved it to the bottom
    // row's walk group (kTransportWalkGroup) — one box and one 2px gap off
    // this walk, no separator moving.
    {RedesignButton::IconZoomFitBest,  icons::Icon::ZoomFitBest},
    // WAVEFORM MAGNIFICATION (architect 2026-09-22), the backtick's
    // lamp, after Full zoom out in the same group: ZoomInY, the magnifier
    // with a plus — the picture's vertical scale grown.
    // It joins the group rather than opening one, so it adds one box and one
    // 2px gap to the walk and no separator.
    {RedesignButton::IconWaveformMagnification, icons::Icon::ZoomInY},
    // (THE WAVEFORM MAGNIFICATION PAIR closed the same group from 2026-08-26
    // to 2026-09-14 — magnify wearing zoom-in-y and reduce wearing
    // zoom-out-y — and left with the setting it stepped (architect
    // approval 2026-09-14), two boxes and two 2px gaps off the walk and no
    // separator moving.)
    // (THE SINGLE-MARKER VERBS opened a separator-led group here from
    // 2026-08-12 until the architect moved them to the BOTTOM ROW's right
    // block on 2026-08-18; their four glyphs went with them and are at the
    // bottom row's own table below.)
    // (THE MASS-MARKER CATEGORY OPENED A SEPARATOR-LED GROUP HERE and no
    // longer exists. The PHASE-RESET CLIPBOARD PAIR led it until 2026-08-20,
    // when the architect's propagate relocation deleted both buttons: all five
    // propagate commands took the new EDIT MENU as their one pointer home, and
    // edit-copy and edit-paste went with them — the glyphs had no second
    // consumer, the trim scissors' own precedent — and the separator fell in
    // front of the BPM opener. On 2026-08-27 the SERIES RELOCATION did the
    // same thing to the two that were left: bare `m` and bare `i` took the new
    // SERIES MENU as their one pointer home, IconBpm's MUSIC-NOTE-16TH and
    // IconIter's MATHMODE went with them (neither glyph had a second consumer;
    // Mathmode had been iteration mode's since 2026-08-18, when it took the
    // summation sigma's slot and the sigma moved to the CUMULATIVE toggle), and
    // the separator went too — FOLLOW, the survivor, joined the ZOOM GROUP
    // above rather than standing alone behind a divider. THE TWO BUTTONS AND
    // BOTH GLYPHS CAME BACK ON 2026-09-04 in a group of their own, further
    // down this table; the mass-marker group itself did not.)
    // FOLLOW (bare `f`), wearing GoJump — an arrow crossing a sheet: the
    // view follows the playing scanner rather than reading as a transport
    // control. Out for the hours of 2026-09-23 the Shift+C chase
    // posture replaced it, and back that evening with its glyph: one box and
    // one 2px gap onto the walk, no separator moving.
    {RedesignButton::IconFollow, icons::Icon::GoJump},
    // THE RESTRICT-UNDO-TO-CURRENT-VIEW LAMP (2026-09-04; to Viewport until
    // 2026-09-22) closes the same group, "it is also a viewport
    // gesture" being the architect's own reason for moving it here from the
    // toolbar group, where it had stood between Redo and Render for the hours
    // of its first day. What it decides is whether an undo or redo may SWITCH
    // THE VIEW — the tab, the audio view or the marker column. It wears
    // TimelineLift, the sheaf under its clip (icons.h): the view held where it stands. It joins the group rather than opening one, so the move adds
    // no box and no gap; what the same ruling took off the walk is one
    // separator, the view lamps' own.
    {RedesignButton::IconRestrictUndo, icons::Icon::TimelineLift},
    // THE ITERATION GROUP (architect 2026-09-04): the BPM opener (bare `m`)
    // and grid iteration mode (bare `i`), back from the menu row behind a
    // separator of their own, just ahead of the render-entry pair. The
    // architect deleted the ITERATIONS dropdown once this row had room again,
    // which is the 2026-08-27 Series relocation run in reverse — the two
    // commands have ONE pointer home and it is these buttons. The row gains
    // two boxes, one 2px gap and ONE separator, so the group count moves too
    // (the leader is IconBpm, redesign_button_opens_icon_group).
    //
    // THE GLYPHS: MusicNote16th, the metronome, for the BPM opener, and
    // Mathmode, the grid, for the mode lamp (icons.h).
    {RedesignButton::IconBpm,  icons::Icon::MusicNote16th},
    {RedesignButton::IconIter, icons::Icon::Mathmode},
    // FLATTEN CLOSES THE GROUP (architect 2026-09-19), up from the bottom
    // row's marker verbs where it had stood since that morning: the group
    // gains one box and one 2px gap, no separator moves, and the leader is
    // unchanged. ITS SEAT IS A CLASSIFICATION BY SUBJECT — a deviation term
    // exists because a GRID SWEEP APPENDED ONE to every cell it wrote, and
    // Ctrl+F is the one road that takes those terms off again, so the act
    // stands beside the mode that produces what it removes (the reasoning in
    // full, and what is deliberately NOT claimed for it, is at the roster
    // entry, app_state.h). The glyph is Merge, two tracks joining into one —
    // what a deviation chain does under the act.
    {RedesignButton::IconFlatten, icons::Icon::Merge},
    // THE RENDER-ENTRY GROUP (architect 2026-08-14): "make the last section of
    // the icon row: listen, load-in-place, readonly, history". The render-entry
    // pair keeps its separator-led group and gained the READ-ONLY toggle, the
    // padlock off the tabs. THE HISTORY OPENER stood fourth here from that day
    // until 2026-08-18, when it left to lead its own group again with its four
    // companions behind it. IT IS TWO SINCE 2026-09-01 — Listen and the
    // padlock — the LOAD IN PLACE having moved to the history group's tail
    // below (architect: "move the button to the history section"), its act
    // outside the `h` view having been Play renders' own; the two that stay
    // keep his order and Listen still opens the group.
    {RedesignButton::IconListen, icons::Icon::PreviewRenderOn},
    // The padlock pair, Lock and Unlock. The TABLE entry is the closed lock and the
    // resolver (redesign_button_icon, above) is what swaps it for the open one
    // on a writable tab — every button goes through that resolver, so this
    // constant is the fallback rather than the painted truth.
    {RedesignButton::IconReadOnly,       icons::Icon::Lock},
    // SETTINGS (architect 2026-09-29), right after the padlock and ahead of
    // the tooltip lamp (architect 2026-10-01: help comes after settings): bare
    // `;`, the settings prompt, wearing SettingsConfigure (STD_PROPERTIES) —
    // the typed road's pointer spelling, one box and one 2px gap onto the walk
    // and no separator moving.
    {RedesignButton::IconSettings,       icons::Icon::SettingsConfigure},
    // ENABLE TOOLTIPS (architect 2026-09-29, evening), CLOSING the
    // render-entry group behind Settings since 2026-10-01 (architect: help
    // comes after settings): the bare backslash's LAMP, wearing HelpWhatsthis
    // (STD_HELP). Dark at every open, and while dark no tooltip shows
    // anywhere; lit, the Qt model as ruled. One box and one 2px gap onto the
    // walk, no separator moving.
    {RedesignButton::IconTooltips,       icons::Icon::HelpWhatsthis},
    // (THE HISTORY OPENER closed this walk until 2026-10-06; it is
    // right-anchored since, kIconRowHistoryOpener below.)
};

// THE HISTORY STAND-INS — THE ICON ROW'S ONE MODE SWAP (architect
// 2026-10-05, so the row fits the tablet's 2304 device px at gui_scale 400):
// WHILE THE `h` VIEW STANDS, the history group's six companions paint in the
// slots of the AUTHORING GROUPS THE VIEW GREYS WHOLE, and those groups'
// members publish empty rects; outside the view the authoring groups stand
// and the six publish empty rects. The opener (IconHistory) is not a
// companion and stands at its own slot in both states. Every other x is the
// walk's plain accumulation over whichever members stand, and the keyboard
// is untouched: only the row's picture changes, every chord answering
// exactly as it did.
//
// WHICH GROUPS: each entry names the LEADER of the authoring group it
// replaces (redesign_button_opens_icon_group), and the stand-in group takes
// that leader's gap. The two are the groups every member of which the view
// greys through the derived partition (history_mode_disables_button,
// input_pointer.cpp — none of their chords is the mode's vocabulary or on its
// allowlist): UNDO'S (Undo, Redo, Copy Value, Render — Ctrl+Z, Ctrl+Shift+Z,
// Ctrl+C, Ctrl+Alt+R) and THE ITERATION GROUP (Ctrl+B, bare `i`, Ctrl+F).
// No other group is grey whole in the view: the zoom group keeps Full Zoom
// Out (bare `0`) and the render-entry group the tooltip lamp (bare
// backslash). A ruling that admits one of those seven chords into the view
// removes its group from this table.
//
// THE SLOTS: seven, the six companions using six of them. UNDO'S FOUR take
// the walk's steps and the view's two acts, Older and Newer in Undo's and
// Redo's slots (the walk's step back and step forward where the session's
// own back and forward stand), then Revert and Load in Place; THE ITERATION
// GROUP'S THREE take the two reading lamps, the walk source and the
// cumulative reading, so that group stands one case narrower in the view
// and the render-entry group right of it sits one case (23 Windows px)
// further left there than outside. The history opener and the view group
// are right-anchored and do not move (kIconRowHistoryOpener).
//
// THE COMPANIONS' GLYPHS: the CUMULATIVE toggle wears BlackSum, the
// summation sigma (a cumulative delta is a sum over the walk's members).
//
// THE REPAINT: the mode's two edges, open_history_mode_fresh and
// close_history_mode (input_key_dispatch.cpp), each end in full-window
// damage, so the row repaints whole on the frame the mode flips and every
// member publishes its rect — a real one or an empty one — in that pass; a
// press resolves against that painted row (publish_button_face).
constexpr IconRowDef kIconRowHistoryStepGroup[] = {
    {RedesignButton::HistoryOlder,      icons::Icon::KeyframePrevious},
    {RedesignButton::HistoryNewer,      icons::Icon::KeyframeNext},
    {RedesignButton::HistoryRevert,     icons::Icon::DocumentRevert},
    // THE LOAD IN PLACE (architect 2026-09-01, off the render-entry group):
    // the icon row's press runs the `h` view's own load — the confirmation on
    // the viewed walk member. It keeps DIALOG-OK-APPLY, the checkmark, which
    // is why the render player's Load in place button took that same glyph
    // the same day.
    {RedesignButton::IconLoadInPlace,   icons::Icon::DialogOkApply},
};
constexpr IconRowDef kIconRowHistoryReadingGroup[] = {
    // THE WALK LAMP (architect 2026-08-18 as a radio pair, one button since
    // the 2026-09-04 collapse). It wears the LIT state's glyph —
    // SHALLOW-HISTORY, the clock dial with no sweep arm,
    // for a timeline reaching back no further than this run — because Git is
    // the walk's default and the lamp reports Session. The Git half's
    // deep-history clock left icons::Icon with that half.
    {RedesignButton::HistoryWalk,       icons::Icon::ShallowHistory},
    {RedesignButton::HistoryCumulative, icons::Icon::BlackSum},
};
struct IconRowStandIn {
    RedesignButton                replaces;   // the authoring group's leader
    std::span<const IconRowDef>   members;
};
constexpr IconRowStandIn kIconRowHistoryStandIns[] = {
    {RedesignButton::Undo,    kIconRowHistoryStepGroup},
    {RedesignButton::IconBpm, kIconRowHistoryReadingGroup},
};

// THE HISTORY OPENER — RIGHT-ANCHORED (architect 2026-10-06): bare `h`, the
// one history button that stands in every state, the toggle's same press
// entering and leaving the view. A group of its own since 2026-08-18 ("place
// a separator before the history button, and place cumulative/etc after the
// history button") and alone since 2026-10-05, when its six companions went
// to the stand-in slots above. IT TAKES NO PART IN THE LEFT WALK: it seats
// flush against the view group's left, its right edge one 8-px group gap
// short of view_x0, and the left groups' limit stands one gap further left
// again (paint_icon_row). THE REASON IS THE PEN: while the opener closed the
// left walk, the stand-in swap's one-case-narrower iteration group put its x
// 23 Windows px further left inside the view than outside, so the button
// that toggles the view moved out from under the pen at its own press;
// anchored to the lane's right edge, which no state moves, it stands at one x
// in both. It paints whole beside the view group under the same overflow
// rule (kIconRowViewGroup), the left groups yielding to both. Its seat is
// the same in both states, and the mode's two edges damage the whole window
// anyway (THE REPAINT, above).
constexpr IconRowDef kIconRowHistoryOpener =
    {RedesignButton::IconHistory, icons::Icon::VcsDiff};

// THE VIEW GROUP — THE ICON ROW'S LAST, FLUSH AT ITS RIGHT EDGE (architect
// 2026-10-01): the three ABSOLUTE VIEW SELECTORS, Source+Warp, Target+Warp and
// Target+Phase on bare 1 / 2 / 3, in their keys' order. THE ACTS CAME FROM
// THE ROW-1 VIEW BAR (2026-08-02..2026-10-01), itself kdenlive's workspace
// switcher — the blue "Logging | Editing | Audio | Effects | Color" bar at the
// far right of its menu row — reborn as the selectors; the bar's three
// labelled buttons are gone and the acts wear the architect's 2026-08-11
// glyphs (icons.h). THE ROW'S OWN METRICS, NONE NEW: the toolbar case
// (render.h's icon-row block), the members touching, the eight-px group gap
// on the group's LEFT (its leader is Source+Warp at
// redesign_button_opens_icon_group), and the row's 8px pad as the lead-out
// from the lane's right edge. THE ROW'S FACES, and a RADIO OF THREE: the
// lit button is the current view (redesign_button_selected reads the live
// combination) and a press on it is the consumed nothing (the `radio` column,
// kToolbarChords).
//
// THE OVERFLOW RULE (architect 2026-10-01): AT ANY WINDOW WIDTH WHERE THE ROW
// CANNOT HOLD EVERY GROUP, THE VIEW GROUP WINS — and since 2026-10-06 the
// history opener seated against its left with it (kIconRowHistoryOpener).
// Both paint LAST, whole, at their right-anchored places; the groups to
// their left yield, clipped where the opener's eight-px gap begins, and each
// yielding member PUBLISHES ONLY WHAT IT
// PAINTED — its box cut at that column, or an empty rect when the opener and
// the view group cover it whole — so a press, a hover and a tooltip land on
// exactly the pixels on screen (on screen is as painted; paint_icon_row's
// walks). The row fits whole down to 524 Windows px of window at 100 %
// outside the `h` view and 501 inside it (the width math at paint_icon_row,
// re-derived 2026-10-06 for the opener's seat and unchanged: the move trades
// one case and one gap of the left walk for the same two on the right), so neither
// host reaches the rule at its scale: the tablet's 2304 device px hold the
// row in both states up to 440 % and the laptop's 1920 up to 365 %.
constexpr IconRowDef kIconRowViewGroup[] = {
    {RedesignButton::ViewSW, icons::Icon::DocumentExport},
    {RedesignButton::ViewTW, icons::Icon::DocumentImport},
    {RedesignButton::ViewTP, icons::Icon::ChronometerStart},
};

// A BUTTON'S ICON, by state — the tooltip overload's sibling (app_state.h,
// which owns the hint half of the same facts and the reasoning). It lives HERE
// because the constant icons do: kIconRowButtons is the painter's roster half,
// and app_state.h carries no icon vocabulary at all. SINCE THE 2026-08-12
// RELAYOUT DELETED ROW 2'S LABELS these swaps are the toolbar pair's WHOLE
// stateful face: SAVE is the COMMIT ACT while the history mode stands and
// wears the commit icon to say so — and keeps wearing it while the checkpoint
// publishes — and RENDER wears the cancel glyph while an explicit render act
// is live. Render's two IDLE meanings (plain render, the iteration sweep)
// share media-record by the architect's same-day ruling — "the context makes
// it clear" — with the tooltip alone forking. (Render held the commit
// override 2026-08-04..08, when the act moved onto the save chord it begins
// with.)
icons::Icon redesign_button_icon(const AppState& app, RedesignButton b,
                                 icons::Icon table_icon) {
    // THE CONDITION IS NOT ASKED HERE (2026-08-15): whether a button wears its
    // second glyph is redesign_button_glyph_swapped's (app_state.h), because
    // the roster's face STASH carries that bit for the drift comparator and a
    // second spelling of the same four conditions is exactly what would drift.
    // This body owns only WHICH glyph the swap gives.
    if (!redesign_button_glyph_swapped(app, b)) return table_icon;
    switch (b) {
        // SAVE, in the history view (where Ctrl+S IS the checkpoint act) and
        // while a checkpoint publishes.
        // THE PULL GLYPH (vcs-pull, 2026-09-27) is Save's third, in the view
        // while the GitHub status reads Behind (redesign_button_glyph).
        case RedesignButton::Save:
            return redesign_button_glyph(app, b) == 2 ? icons::Icon::VcsPull
                                                      : icons::Icon::VcsCommit;
        // RENDER'S MID-RENDER FACE (architect 2026-08-11): the CANCEL glyph
        // while a render or sweep is live — DialogCancel, Marlett's close X
        // (WindowClose's drawing, icons.h). The condition and its
        // contract are at redesign_button_glyph_swapped's Render arm, the
        // click's divergence at finish_chrome_press_release's Render arm.
        case RedesignButton::Render: return icons::Icon::DialogCancel;
        // THE READ-ONLY TOGGLE'S PADLOCK (2026-08-14): the CLOSED lock while
        // the active tab is read-only — which is its TABLE glyph — and the
        // OPEN one while it is writable, so this row is the swap. It is the
        // tabs' own two-glyph face carried whole onto the button that took the
        // padlock's job, and a THIRD kind of stateful glyph: Save's and
        // Render's swap on a MODE, this one on the very bit its own chord
        // flips, so it says the same thing as its lit lamp
        // (redesign_button_selected) in the padlock's vocabulary rather than
        // the row's. The tabs' DIM on the open lock did not come along — in
        // this row a dimmed glyph means disabled.
        case RedesignButton::IconReadOnly: return icons::Icon::Unlock;
        // THE COLLAPSED PLAY/STOP BUTTON (architect 2026-08-15): one button
        // over bare Space, wearing media-playback-STOP while the TRANSPORT is
        // live — a plain audition, or an A/B audition sequence including its
        // silent rests — and the table's media-playback-start otherwise. It is
        // RENDER-IS-CANCEL's shape applied to a toggle — and unlike Render it
        // needs no chord exception, because Space already toggles.
        case RedesignButton::TransportPlayStop:
            return icons::Icon::MediaPlaybackStop;
        default:
            return table_icon;
    }
}

// -- THE FLOATING SURFACES: the hover tooltip and the menu row's dropdowns --
//
// The tooltip's metrics are Windows 95's, measured off the one period capture
// (ToastyTech's win95toolbar.png, Explorer's "Up One Level"); its chrome is the
// floating surfaces' one box (paint_popup_chrome), and its seat, its timing and
// its damage bound live in render.h.
//
// THE TOOLTIP'S TYPE AND SPACING. ONE FACE FOR BOTH LINES, the body
// (architect 2026-10-05: the period faces have no smaller text face — the
// SHIFT line's own 11 Windows px, ruled 2026-07-31 to read as subordinate,
// retired with the outline face it sized). Both go through the one shaping
// chokepoint.
//
// THE VERTICAL LAYOUT IS DERIVED, NOT AUTHORED, and it is SYMMETRIC BY
// CONSTRUCTION: each line occupies the face's (ascent + descent) band — the
// recorded 13-row cell — the two bands are separated by a real gap, and the
// SAME padding closes the box above the first band and below the last. So
// the box height falls out as
//     pad + band [+ gap + band] + pad
// and the top and bottom air are equal by the arithmetic rather than by a
// measured pair that could drift. THE PAD IS WINDOWS 95's (architect
// 2026-10-06): the capture's one-line box is 17 rows, 2 + 13 + 2 — above
// the 13-row cell the unlined top row and one more, below it one face row and
// the black line — so the pad is 2 and the frame's one line counts inside it.
// At the tablet's 275 % that is
// 6 + 36 + 6 = 48 for one line and 6 + 36 + 8 + 36 + 6 = 92 for two (the
// band 13 x 2.75 = 35.75 each, the sum rounded once). THE HORIZONTAL PAD
// stays 4 (the laptop pixel's 5 re-authored at the unit's change, architect
// 2026-10-02), within a pixel of the capture's: the unlined left column and
// three face columns before the first ink, three after the last before the
// black line. The gap is the laptop pixel's 4 re-authored the same day.
// render.h carries only a BOUND on this for the damage band.
constexpr double kTooltipPadYPx          = 2.0;   // top AND bottom, equal
constexpr double kTooltipLineGapPx       = 3.0;   // between the two bands
constexpr double kTooltipPadXPx      = 4.0;
// (The damage BOUND on the height and the timing constants live in render.h —
// the tooltip's clock reads them, for the deadlines and for the band beside
// the strip.)

// THE TOOLTIP'S TEXT lives with the roster, not here
// (redesign_button_tooltip, app_state.h, owns both the membership and the text;
// its MODIFIER line is static_asserted against redesign_button_shift_admits,
// redesign_button_ctrl_admits and redesign_button_ctrl_shift_admits, so the
// hint cannot appear where a modified press does nothing).
//
// EVERY BUTTON BUT ROW 1'S HAS ONE (architect 2026-07-31, stated as the ROW's
// property at the table): the one-line form is the whole story for most, and the
// buttons that admit a modifier add the hint line below it (the membership is
// those predicates, app_state.h, never a count restated here) — Render only
// while iteration mode is OFF, where its shift press has a twin to reach.
// (The TEXT and its membership live at redesign_button_tooltip, app_state.h —
// beside the roster, because the pointer side reads the same table.)

// THE DROPDOWN, Windows 95's popup menu at the Windows pixel (architect
// 2026-10-02). Its vertical terms — the 17-px item, the frame, the one-px
// margin, the separator's block — live with dropdown_h_px's other
// ingredients in render.h (the popup's OPEN EDGE must size the box before it
// is painted); only the HORIZONTAL terms, which depend on the widest shaped
// label, are the painter's alone, and every menu shares every one of them.
//
// IT IS NOT THE TOOLTIP'S INTERIOR: the floating surfaces share their CHROME —
// one box painter (paint_popup_chrome, its two faces) — and not their heights.
// The tooltip's box is not authored at all, its height falling out of pad +
// band [+ gap + band] + pad on the face's own cell (the record at
// kTooltipPadYPx); do not re-derive one from the other.
//
// THE ITEM'S INSET: the highlight box stands one Windows px inside the frame
// on every side — the margin Windows leaves between a popup's edge and its
// lit row — so the published item rect is the frame's interior less that px.
// THE SEPARATOR'S INSET: its etched pair runs 5 Windows px in from the popup's
// edge each side (the laptop pixel's 7 re-authored at the unit's change).
constexpr double kPopupItemInsetPx   = 1.0;   // the highlight box, per side
constexpr double kPopupSepInsetPx    = 5.0;   // the separator, per side

// THE MINIMUM ITEM WIDTH — the tab-min-width pattern, and the reason a menu of
// short labels still reads as a menu. THE VALUE IS AUTHORED, NOT DERIVED: the
// architect's knob turned at the live look (2026-08-03, 242 laptop px, a flat
// +42 on the crop's derivation), 176 Windows px since the unit's change
// (architect 2026-10-02, the same device width on the tablet). Nothing
// re-derives it and nothing should try. The painter takes the LARGER of this
// floor and a menu's content ask at every paint, so which term wins is all it
// says: the short menus, FILE and SETTINGS, sit on the floor by the
// advance-width estimate, and EDIT's widest row ("Paste Phase Reset State" |
// "Ctrl+Alt+Shift+P") asks past it. No content figure is stated here: the
// ink is the shaper's and the rows are app_state.h's (kFilePopupItems and its
// siblings), so a number written down would go stale the next time either
// moves. (The floor's measured history — the crop's 200, the deleted menus
// it bound on — is git history.)
constexpr double kPopupItemMinWidthPx = 176.0;

// THE DROPDOWN'S HORIZONTAL PADS, authored in POPUP-BOX coordinates — from
// the box's own outer edges, which is how a menu with an accelerator column is
// easiest to state. EVERY MENU TAKES THEM (architect 2026-08-03): the menus
// differ in one derived term (whether an accelerator column exists) rather
// than in their padding.
//
//  - THE LABEL PAD AND THE RIGHT PAD ARE ONE NUMBER, 22 WINDOWS PX (architect
//    2026-10-02, the mirror rule): from the popup's left edge to the label's
//    pen, and from the popup's right edge to the accelerator's last ink
//    column (or, on a menu without one, the widest label's) — Windows' popup
//    reserves its check-mark column on the left and its submenu-arrow column
//    on the right, the same width, and this product has neither and keeps
//    their space as plain padding. It is the right margin the kdenlive crop
//    measured (30 laptop px, re-authored at the unit's change); the crop's
//    57-px left indent, kdenlive's checkbox-and-icon gutter, retired for the
//    mirror.
//  - THE COLUMN GAP is the guaranteed minimum separation between the widest
//    label and the widest accelerator, the kdenlive crop's 13 laptop px
//    re-authored as 9 Windows px (architect 2026-10-02: the gap kept).
constexpr double kPopupPadXPx         = 22.0;
constexpr double kPopupHotkeyGapPx    = 9.0;

// THE BASELINE FOR A LABEL VERTICALLY CENTRED IN A BOX: the row that centres
// the face's own CAP BAND in the box, a half-row tie going toward the TOP
// (architect 2026-09-09, after his pixel pass on two screenshots at 100% and
// 225%). What the eye centres in a chrome box is the capital band — the word
// "File" is cap-height ink and nothing else — so the solver centres exactly
// that and lets the descender band fall where the face puts it:
//
//     baseline = box_y + floor((box_h + cap_height) / 2)
//
// The floor IS the cell rule (containing_pixel, input_core.h) and not an
// exception to it: a half-row tie belongs to the row that contains it, which
// is the upper one. THE ONE BASELINE SOLVER FOR EVERY EXTENTS-CENTRED CHROME
// LABEL and every time field (Qt's integer box rule fails the crops).
//
// THE CAP BAND IS THE BODY FACE'S RECORDED ONE AT EVERY SCALE (architect
// 2026-10-05, gui_font.h): 9 rows times the scale, an unrounded double — 9
// device rows at 100 %, 12.42 at 138 %, 24.75 at 275 %, 36 at 400 % —
// Nimbus's em being the one that stands its "H" exactly that tall.
//
// THE SEATS AT THE TABLET'S 275 %: the menu row's 52-row CONTENT band (not
// its 55-row lane, which carries a foot the label never centres in since
// 2026-10-05) seats at row floor((52 + 24.75) / 2) = 38, the dropdown's
// 47-row item at 35; the answer is independent of the box's y by
// construction rather than by a tie rule.
//
// TWO SEATS, AND NO CALLER SOLVES A LINE AS A BOX. A BOX has margins to
// centre a cap band in; a LINE is exactly the face's own ascent-plus-descent
// band and has none, so a line's baseline is line_baseline() below and the cap
// rule is not asked. THIRTEEN BOX SEATS (re-grepped 2026-10-05): the
// caption's title, the menu row's
// anchors (in the row's CONTENT alone
// since 2026-10-05, render.h's menu_row_content_h_px, never the taller
// lane), the row-8 clock (in its time field) and the
// state line beside it (in the row's content band), the notification
// card's first line, the dropdown items, the prompt's message, the render player's
// two time fields, the modal field's INK, the modal field's LABEL (on the BUTTONS' box —
// the reasoning is at that site), the modal buttons' own labels, the on-screen
// keyboard's caps, and the folder overlay's rows. FOUR LINE SEATS: the
// tooltip's two lines, the ruler's labels and, since 2026-10-02, the marker
// lane's flag labels (below). (The folder overlay's TEXT rows
// were a fourth, and the one site that forked between the two seats, until
// they went with the AV Sync Stats panel on 2026-09-30.)
//
// THE MARKER LANE IS A LINE SEAT TOO, and stays out of the box solver: the
// live flag pass, the history-diff flag pass and the marker-lane editor each
// seat their label at the flag box's top plus marker_flag_baseline_px() — its
// edge band, one Windows px of face, then the body face's recorded ascent
// (architect 2026-10-05): the box is exactly outline, face, the body's
// whole cell, face, outline, so there is no margin to centre in — the caps
// fall centred by the cell itself. The owner and the arithmetic are at
// render.h's marker_lane_h_px block.
//
// TWO AUTHORED DROPS RETIRED WITH THIS RULE, both of them hand-measured
// corrections to the proxy the rule replaces. THE CLOCKS' 1px: the bottom
// row's content band was 46 at 100% and 104 at 225%, and cap-centring the
// monospace digits gave row 28 and row 63 — exactly where the old proxy plus
// the architect's authored drop put them, at both scales. His measured pixel
// WAS cap-centring, so the offset is gone. The time fields (2026-10-05) seat
// their own box: the 68-row field at 400 % puts the body's 36-row cap at
// row 52 (sixteen rows above it, sixteen below — a 4-row line and 12 of
// face each side, Windows' own 3 + 9 + 3 times four), the 47-row field at
// 275 % its 24.75-row band at row 35, the 23-row field at 138 % its
// 12.42-row band at row 17.
// AND THE MODAL FIELD LABEL'S 1px: that label now reads THE BUTTONS' OWN SEAT
// rather than the field band's plus a drop, which is what levels it with OK
// and Cancel at every scale (the reasoning is at the modal's own site).
double redesign_baseline(const GuiFont& font, double box_y,
                         double box_h) {
    return box_y + std::floor((box_h + gui_font_cap_px(font)) * 0.5);
}

// THE BASELINE FOR A LINE — a band that IS the face's ascent plus its descent,
// with no margins to centre anything in, so the seat is the ascent and the
// rule above is not asked. The ceil keeps a seat on a whole pixel; on both
// backends the hinted ascent is a whole pixel already and it changes
// nothing.
double line_baseline(const GuiFont& font, double line_y) {
    return line_y + std::nearbyint(gui_font_ascent_px(font));
}

// -- THE CAPTION (architect 2026-10-05) ----------------------------------------
//
// THE CAPTION BUTTONS' GLYPHS — Windows' MARLETT characters as its caption
// buttons draw them at the body size, off the architect's reference (a
// Windows 2000 window: the Minimise bar, the Maximise box and the Close X
// measured there; the Restore pair, which that maximisable window does not
// show, is Marlett's two overlapping boxes, the back one up and right).
// Every glyph sits in ONE 9 x 9 Windows-px CELL, the character cell Windows
// places in the 16 x 14 button at (3, 2), of unit u = scaled_px(1, 1) per
// Windows px, in the theme's LABEL, the emboss when disabled, one relief
// line right and down while pushed (paint_button_box's shift). THE GLYPHS
// ARE CHROME, NOT THE ICON SET'S BITMAPS: Windows drew them in the button
// text, so they wear the label role and never a drawing's literal inks.
//
// THE CELL STANDS AT WINDOWS' AUTHORED SEAT (3, 2) THROUGH scaled_px (Sol's
// finding 2026-10-06): the seat is the rounding doctrine's composite, offset
// + glyph + offset, each part rounded at the element — gx = b.x +
// scaled_px(3), gy = b.y + scaled_px(2), plus the pushed shift. At every
// whole-multiple gui_scale it is Windows' own (3, 2): (3, 2) at 100 %,
// (12, 8) at 400 %. The road it replaced centred the 9-cell in the button in
// device px with the offset floored to a whole unit u (the architect's glass
// 2026-10-06, the disabled Restore at 400 %, where device-px centring alone
// had put the cell at (3.5, 2.5) Windows px and the emboss's Hilight copy
// over the top half of the bevel's Shadow line); right at whole multiples, it
// was wrong at 275 %, the committed default — a 44 x 38 box, u = 3, a 27-px
// cell put at (6, 3), the glyph's top three rows inside the inner bevel (the
// two relief lines of 3 device px take rows 0..5). The authored seat puts it
// at (8, 6), exactly after the bevel. AT A NON-WHOLE SCALE the cell (9u)
// is not the scaled 9 Windows px and stands at the authored seat rather than
// centred in the button: smaller where u rounds down — accepted (the
// laptop's 138 % puts its 9-px cell at (4, 3) of a 22 x 19 box) — and larger
// where it rounds up (275 %: 27 against 24.75, one row more than the 26 the
// bevels leave inside the box, so a glyph inked on the cell's last row,
// Minimise, Maximise and Restore, lays that row on the inner bottom
// bevel's first). Checked pixel for pixel, at a whole
// multiple, against Chicago95's xfwm4 caption buttons
// (maximize-toggled-*.xpm, close-*.xpm), whose glyphs are Marlett's.
//
// MINIMISE, MAXIMISE AND RESTORE ARE RECTANGLES and stay authored cells, a
// list of rectangles in the cell's Windows px painted as INTEGER RECTANGLES
// of u: they are rectangles at every scale, and a vector would draw the same
// pixels.
//
// CLOSE IS TWO BARS (architect's glass 2026-10-06: "off-centre, noticeably,
// and too thin"): Marlett's close X as a vector, smooth at any scale as
// Windows draws Marlett at any DPI, painted here as TWO FILLED BARS AT 45
// DEGREES, kCaptionCloseStrokePx = 2 Windows px thick with SQUARE ENDS, the
// pair's bounding square 7 x 7 Windows px CENTRED ON THE REFERENCE'S INK
// CENTRE, (5, 4.5) of the cell: Marlett's X inks 8 x 7 at (1, 1) of the cell
// (kCaptionCloseInk; Chicago95's close-active.xpm inks the same 8 x 7 at
// (4, 3) of its 16 x 14 button, centre (8, 6.5)), and a 45-degree pair with
// square ends is square, so it takes the box's height and stands centred in
// its width — the staircase's two outer half-columns are the pixel glyph's
// own corners. ONE PATH, ONE FILL (the bars' crossing is covered once, so its
// antialiased edges never double), antialiased like the icon set's diagonals.
// The icon set's WindowClose drawing (the render player's Close) was worn
// here before: its two-unit bars are one Windows px of cross-section at the
// cell's 8 x 7, and fitting its 11 x 9 ink cannot give both the stroke and
// the extent.
struct CaptionGlyphRect {
    int x, y, w, h;
};
constexpr int kCaptionGlyphCellPx = 9;
constexpr int kCaptionGlyphSeatXPx = 3;
constexpr int kCaptionGlyphSeatYPx = 2;
static_assert(kCaptionGlyphSeatXPx + kCaptionGlyphCellPx <= kCaptionButtonWPx &&
              kCaptionGlyphSeatYPx + kCaptionGlyphCellPx <= kCaptionButtonHPx);
constexpr CaptionGlyphRect kCaptionMinimizeGlyph[] = {{1, 7, 6, 2}};
constexpr CaptionGlyphRect kCaptionMaximizeGlyph[] = {
    {0, 0, 9, 2}, {0, 2, 1, 6}, {8, 2, 1, 6}, {0, 8, 9, 1}};
constexpr CaptionGlyphRect kCaptionRestoreGlyph[] = {
    // the back window: its two-row top, the stub of its left side, its right
    // side and the end of its foot, the rest behind the front window
    {2, 0, 6, 2}, {2, 2, 1, 1}, {7, 2, 1, 3}, {6, 5, 2, 1},
    // the front window, whole
    {0, 3, 6, 2}, {0, 5, 1, 3}, {5, 5, 1, 3}, {0, 8, 6, 1}};
constexpr CaptionGlyphRect kCaptionCloseInk = {1, 1, 8, 7};
constexpr double kCaptionCloseStrokePx = 2.0;

void paint_caption_glyph(cairo_t* cr, std::span<const CaptionGlyphRect> glyph,
                         int gx, int gy, int u, GuiColor ink) {
    for (const CaptionGlyphRect& r : glyph)
        paint_cell_rect(cr, GuiRect{gx + r.x * u, gy + r.y * u, r.w * u,
                                    r.h * u},
                        ink);
}

// The Close X (the rule above) in the cell at (gx, gy), unit u, in `ink`:
// each bar the rectangle touching the bounding square's four edges, its
// corners on the edges `d` = stroke / sqrt(2) from the two corners its axis
// runs between — so its ends are square to the bar and it is `stroke` thick.
void paint_caption_close_x(cairo_t* cr, double gx, double gy, double u,
                           GuiColor ink) {
    const double cx = kCaptionCloseInk.x + kCaptionCloseInk.w / 2.0;
    const double cy = kCaptionCloseInk.y + kCaptionCloseInk.h / 2.0;
    const double h  = std::min(kCaptionCloseInk.w, kCaptionCloseInk.h) / 2.0;
    const double d  = kCaptionCloseStrokePx / std::sqrt(2.0);
    const auto at = [&](double x, double y) {
        cairo_line_to(cr, gx + x * u, gy + y * u);
    };
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_new_path(cr);
    // The falling bar clockwise, then the rising one (its mirror) walked
    // backwards so it is clockwise too: under the winding rule the crossing
    // then counts twice and stays ink, never a hole.
    cairo_new_sub_path(cr);
    at(cx - (h - d), cy - h);
    at(cx + h,       cy + h - d);
    at(cx + (h - d), cy + h);
    at(cx - h,       cy - h + d);
    cairo_close_path(cr);
    cairo_new_sub_path(cr);
    at(cx + h,       cy - h + d);
    at(cx - (h - d), cy + h);
    at(cx - h,       cy + h - d);
    at(cx + (h - d), cy - h);
    cairo_close_path(cr);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    set_palette_source(cr, ink);
    cairo_fill(cr);
    cairo_restore(cr);
}

// One caption button's glyph on its box `b` (already painted,
// paint_button_box; `shift` its answer): the cell on its unit grid, the
// glyph by `id` (Restore for Maximise while `maximized`), in the label or
// embossed.
void paint_caption_button_glyph(cairo_t* cr, const GuiRect& b,
                                GuiCaptionButton id, bool maximized,
                                bool enabled, int shift) {
    const GuiPalette& pal = palette();
    const int u    = scaled_px(1, 1);
    const int gx   = b.x + scaled_px(kCaptionGlyphSeatXPx) + shift;
    const int gy   = b.y + scaled_px(kCaptionGlyphSeatYPx) + shift;
    const int off  = relief_line_px();
    // THE DISABLED EMBOSS (render.h's palette block): Hilight one Windows px
    // right and down, Shadow at the glyph's place.
    if (id == GuiCaptionButton::Close) {
        if (!enabled)
            paint_caption_close_x(cr, gx + off, gy + off, u, pal.hilight);
        paint_caption_close_x(cr, gx, gy, u,
                              enabled ? pal.label : pal.shadow);
        return;
    }
    const std::span<const CaptionGlyphRect> glyph =
        id == GuiCaptionButton::Minimize ? std::span<const CaptionGlyphRect>(
                                               kCaptionMinimizeGlyph)
        : maximized ? std::span<const CaptionGlyphRect>(kCaptionRestoreGlyph)
                    : std::span<const CaptionGlyphRect>(kCaptionMaximizeGlyph);
    if (!enabled)
        paint_caption_glyph(cr, glyph, gx + off, gy + off, u, pal.hilight);
    paint_caption_glyph(cr, glyph, gx, gy, u,
                        enabled ? pal.label : pal.shadow);
}

// The three buttons' rects in a caption lane, left to right (render.h's
// caption block): flush right, kCaptionButtonInsetPx right of Close and above
// all three, Minimise and Maximise touching, kCaptionCloseGapPx before Close —
// each edge a sum of rounded parts.
std::array<GuiRect, kCaptionButtonCount> caption_button_rects(
        const GuiRect& lane) {
    const int w   = scaled_px(kCaptionButtonWPx, 1);
    const int h   = scaled_px(kCaptionButtonHPx, 1);
    const int y   = lane.y + scaled_px(kCaptionButtonInsetPx);
    const int cx  = lane.x + lane.w - scaled_px(kCaptionButtonInsetPx) - w;
    const int mx  = cx - scaled_px(kCaptionCloseGapPx) - w;
    return {GuiRect{mx - w, y, w, h}, GuiRect{mx, y, w, h},
            GuiRect{cx, y, w, h}};
}

} // namespace

void GuiPaintHandler::paint_caption_row(cairo_t* cr) {
    // THE CAPTION (top lane 0; the record at render.h's kCaptionHeightPx and
    // the declaration). Four passes on the lane: the ground, the icon, the
    // title and the buttons.
    const GuiRect row = top_caption_row_area(app);
    if (row.w <= 0 || row.h <= 0) return;
    cairo_save(cr);

    // THE GROUND: the active roles while the window has the focus, the
    // inactive ones without it (GuiPlatform::caption_active — always active
    // on the tablet), through the one gradient painter.
    const GuiPalette& pal = palette();
    const bool active = gui.caption_active();
    paint_caption_gradient(cr, row,
                           active ? pal.caption_active : pal.caption_inactive,
                           active ? pal.caption_active_gradient
                                  : pal.caption_inactive_gradient);

    // THE APP'S ICON at (2, 1), 16 x 16: the set's sixteenth note
    // (icons.h), no case, at the caption's own placement.
    icons::draw(cr, icons::Icon::AppIcon,
                static_cast<double>(row.x + scaled_px(kCaptionIconXPx)),
                static_cast<double>(row.y + scaled_px(kCaptionIconYPx)),
                static_cast<double>(scaled_px(kCaptionIconPx, 1)));

    const std::array<GuiRect, kCaptionButtonCount> rects =
        caption_button_rects(row);

    // THE TITLE, Windows' "Document - Program" convention (architect
    // 2026-10-05): the open piece's name (AppState::project_name, the
    // project's folder) and " - Warptempo", or "Warptempo" alone where no
    // piece is open. THE BOLD FACE (gui_font.h: Nimbus Sans Bold) through
    // the shaping
    // chokepoint, its cap band centred in the lane, in the caption's text
    // role. TOO LONG FOR THE ROOM — the pen at x 20 to two px short of
    // Minimise — it is CUT AT A CODEPOINT and ends in Windows' "..." (DrawText's
    // end ellipsis), the longest prefix whose own run and the ellipsis's fit;
    // a room too narrow for even the ellipsis paints no title.
    const GuiFont font = gui_font(GuiFace::Bold);
    const std::string title = app.project_name.empty()
                                  ? std::string("Warptempo")
                                  : app.project_name + " - Warptempo";
    const int title_x = row.x + scaled_px(kCaptionTitleXPx);
    const double room = static_cast<double>(
        rects[static_cast<size_t>(GuiCaptionButton::Minimize)].x -
        scaled_px(kCaptionButtonInsetPx) - title_x);
    const double baseline = redesign_baseline(
        font, static_cast<double>(row.y), static_cast<double>(row.h));
    set_palette_source(cr, active ? pal.caption_active_text
                                  : pal.caption_inactive_text);
    text_shape::ShapedRun run = text_shape::shape_text_run(font, title);
    if (run.width_px <= room) {
        text_shape::show_shaped_run(cr, run, title_x, baseline);
    } else {
        const text_shape::ShapedRun ellipsis =
            text_shape::shape_text_run(font, "...");
        size_t cut = title.size();
        while (cut > 0) {
            // Back one codepoint: past any continuation bytes to a lead byte.
            do { --cut; } while (cut > 0 &&
                                 (static_cast<unsigned char>(title[cut]) &
                                  0xC0) == 0x80);
            run = text_shape::shape_text_run(
                font, std::string_view(title).substr(0, cut));
            if (run.width_px + ellipsis.width_px <= room) break;
        }
        if (ellipsis.width_px <= room) {
            text_shape::show_shaped_run(cr, run, title_x, baseline);
            text_shape::show_shaped_run(cr, ellipsis, title_x + run.width_px,
                                        baseline);
        }
    }

    // THE THREE BUTTONS, published as painted (AppState::caption_buttons):
    // each rect on every paint, its enabled bit where this frame's clip
    // covers it (the roster's gate). Maximise wears RESTORE while the window
    // is maximised, and is disabled where the window cannot be restored
    // (the tablet). PUSHED while its Caption arm stands with the pointer
    // inside it.
    for (int i = 0; i < kCaptionButtonCount; ++i) {
        const GuiCaptionButton id = static_cast<GuiCaptionButton>(i);
        AppState::CaptionButtonFace& face =
            app.caption_buttons[static_cast<size_t>(i)];
        const GuiRect& b = rects[static_cast<size_t>(i)];
        face.rect = b;
        if (clip_covers_drawable(cr, app, b))
            face.enabled = id != GuiCaptionButton::Maximize ||
                           gui.window_restorable();
        const bool pushed =
            app.chrome_press.kind == AppState::ChromePress::Kind::Caption &&
            app.chrome_press.index == i && app.chrome_press.inside;
        const ButtonBoxFace box =
            paint_button_box(cr, b, /*lamp=*/false, pushed,
                             ButtonFamily::Toolbar);
        paint_caption_button_glyph(cr, b, id, gui.window_maximized(),
                                   face.enabled, box.shift);
    }

    cairo_restore(cr);
}

void GuiPaintHandler::paint_menu_row(cairo_t* cr) {
    // THE MENU ROW (top lane 1, directly under the caption): a flat ground — the
    // content ground since 2026-10-01 — carrying ONE FLOAT, "File", "Edit"
    // and "Settings" flush left, and nothing flush right (architect
    // 2026-10-05: the battery + clock legend is not shown). No ring; the
    // kdenlive bar is flat.
    //
    // THE LEFT FLOAT'S FACES (architect 2026-10-02, the Windows-95 menu bar):
    // COLD, nothing is drawn — the label bare on the ground; OPEN, the anchor
    // whose menu is down is Windows 95's open menu title — its rectangle
    // filled with the theme's SELECTED fill (the dropdown's lit row's own)
    // under its selected text, the recorded pair (render.h's palette block);
    // DEAD (the history view greys every anchor but File, the partition being
    // history_mode_disables_button's), the label alone takes THE DISABLED
    // EMBOSS (show_embossed_run). NO HOVER FACE
    // (architect 2026-10-02: "hover is awkward with pen and sometimes
    // flickers"; Windows 98's hot-tracked raised title is not adopted) and no
    // press face: a press opens the menu, whose highlight is the cue.
    //
    // EVERY ACTION ON THE FLOAT IS THE SAME KIND since 2026-08-13: each button
    // TOGGLES A DROPDOWN — the roster's three non-chord actions, since no
    // keyboard chord
    // opens or closes a popup. The menus lead only where the keyboard already
    // goes: the bare `;` key still opens the settings editor directly, and
    // File's three items are Ctrl+O, Ctrl+Alt+O and Ctrl+Q. (The left float
    // held a CHORD button until that day — Quit, dispatched through the shared
    // chord table like every other redesigned button; the act is the File menu's
    // item now, and the chord is untouched. It held a THIRD ANCHOR, Navigation,
    // until 2026-08-15: its every item was a key too, which is exactly what
    // deleted it — the keys had all grown buttons of their own.)

    const GuiRect row = top_menu_row_area(app);
    if (row.w <= 0 || row.h <= 0) return;

    // THE LANE IS THE ANCHOR (architect 2026-10-01 — render.h's
    // kMenuRowHeightPx carries the ruling and its why): the whole lane's
    // height is each anchor's rectangle AND its published hit rect, flush
    // under the caption with no air above it; the icon row's ground begins
    // on the next pixel row with no margin, border or line between. The
    // anchor's foot, the lane's foot and the icon row's first pixel are the
    // same row — where the dropdown hangs. THE LANE IS NOT ITS CONTENT SINCE
    // 2026-10-05: a one-px foot of ground stands below the content (Windows'
    // measured 20-px menu band, render.h's kMenuRowFootPx), so every label on
    // this row is cap-centred in the CONTENT
    // ALONE (menu_row_content_h_px) rather than the taller lane — the one
    // place this row reads two different heights for two different things.

    cairo_save(cr);

    // THE GROUND IS THE CONTENT GROUND, the icon row's own (architect
    // 2026-10-01: the menu row takes the icon row's ground), one fill over
    // the whole lane. It has one value focused and unfocused, so this row no
    // longer darkens on the window's focus loss.
    const GuiColor ground = palette().ground;
    set_palette_source(cr, ground);
    cairo_rectangle(cr, row.x, row.y, row.w, row.h);
    cairo_fill(cr);

    // THE SHAPING CHOKEPOINT (text_shape.h): each label is MEASURED and PAINTED
    // from the one ShapedRun, so a button's width and its glyphs come from the
    // same positions and cannot disagree. Shaping a handful of glyphs per paint
    // is deliberate — the chokepoint's own comment defers caching to a profile,
    // and these are the cheapest runs there are.
    const GuiFont font = gui_font(GuiFace::Body);

    const int pad    = scaled_px(kMenuLabelPadPx);

    // THE WALK: flush from the row's left edge, ADJACENT WITH NO GAP. Row 2
    // inserts a 2px invisible separator between its adjacent buttons because the
    // architect stated one there; none is stated here, so none exists (the css
    // float model's default).
    int x = row.x;
    for (const MenuButtonDef& def : kMenuButtons) {
        const text_shape::ShapedRun run =
            text_shape::shape_text_run(font, def.label);
        const int btn_w =
            static_cast<int>(std::nearbyint(run.width_px)) + 2 * pad;

        // THE PAINTER PUBLISHES THE HIT RECT (the displayed-basis doctrine): the
        // width above exists only here, so the pointer code reads this stash
        // rather than re-shaping the string. Written every paint — a font, scale
        // or window change lands in it on the frame that displays it.
        // No menu button has a selected face, and this row paints outside
        // the audio branches because it stands in every state. Since
        // 2026-09-24 Edit and Settings grey during a load or on a blank state,
        // in the history view and under the folder overlay, File alone staying
        // live for Quit (menu_anchor_live, app_state.h, owns the verdict). The stash is
        // written anyway
        // (through the one publisher) so the tick comparator's vector is total
        // over the roster with no membership test.
        AppState::RedesignButtonFace& face = publish_button_face(
            cr, app, audio, playback, target_render,
            def.id,
            GuiRect{x, row.y, btn_w, row.h});

        // THE OPEN ANCHOR WEARS THE HIGHLIGHT WHILE ITS DROPDOWN IS UP
        // (architect 2026-10-02, Windows 95's open menu title; the anchor
        // stays marked while its menu is open, kdenlive's behaviour since
        // 2026-08-02): the anchor's rectangle — the whole lane tall, the
        // label's width plus its pads — filled with the selected fill, the
        // label in the selected text over it, whichever of the three anchors
        // emitted the open popup, through the one anchor owner. A PAINT
        // CONDITION, NOT A `selected` BIT: no menu button has a chord
        // (redesign_button_selected is the live fact a chord flips), and the
        // popup's two writers, toggle_dropdown and the one close owner
        // close_dropdown, already invalidate the top strip on both edges. A
        // dead anchor has no open menu (toggle_dropdown refuses it), so the
        // highlight needs no enabled term.
        //
        // DEAD, THE LABEL IS THE ONE THING THAT CHANGES: it takes THE
        // DISABLED EMBOSS, the product's one disabled word (the partition and
        // its derivation are at history_mode_disables_button,
        // input_pointer.cpp; this reads only the published bit).
        const bool open_anchor =
            app.dropdown.open() &&
            def.id == dropdown_anchor_button(app.dropdown.menu);
        if (open_anchor)
            paint_cell_rect(cr, GuiRect{x, row.y, btn_w, row.h},
                            palette().selected_fill);
        // THE LABEL CENTERS IN THE ANCHOR'S CONTENT, NOT ITS LANE (the foot
        // row block above): Qt's own menu bar centers an item's text in the
        // item rect, which is where cap-centring puts ours (redesign_baseline),
        // over the content height alone so the foot row cannot nudge a cap
        // band that already stood at Windows' own position.
        const double label_x = static_cast<double>(x + pad);
        const double label_y =
            redesign_baseline(font, static_cast<double>(row.y),
                              static_cast<double>(menu_row_content_h_px()));
        if (!face.enabled && !open_anchor) {
            show_embossed_run(cr, run, label_x, label_y);
        } else {
            set_palette_source(cr, open_anchor ? palette().selected_text
                                               : palette().label);
            text_shape::show_shaped_run(cr, run, label_x, label_y);
        }

        x += btn_w;
    }

    cairo_restore(cr);
}

// -- history_walk_line ----------------------------------------------------
//
// THE `h` HISTORY MODE'S ONE LINE, composed for ROW 8'S STATE CELL — the
// clock's neighbour since 2026-08-29's fold, the one-day status bar's LEFT
// cell before that. While the mode stands this line is what that cell is for.
//
// THE SHAPE (architect 2026-09-28; the GitHub segment ahead of the scale
// since 2026-09-29): SEGMENTS SEPARATED BY ` | `, every one alike — the
// POSITION `x/y`, then the GITHUB segment `GitHub: <word>` when there is one,
// then the SCALE segment `Scale: [-]<then token> [+]<now token>` when the
// scale changed (both below) — so an absent segment leaves no separator
// behind it and nothing leads or trails: `03/14 | GitHub: up to date |
// Scale: [-]1.0 [+]1.1`, `03/14 | GitHub: behind`, `007/114`. The same shape
// on BOTH walks. The scale segment speaks the lane's own sign vocabulary
// through the lane's own spelling owner (history_diff_label), so this line
// and the flags cannot come to bracket differently. (The "bottom-left corner" of the mode's record read
// bottom-RIGHT from the 2026-08-12 unification, TOP-RIGHT while the status
// chain sat in the tab row, bottom-LEFT on the one-day status bar, and reads
// BOTTOM-CENTRE now, beside the clock — the same line, four surfaces.)
//
// THE LINE NAMES NO COMMIT (architect 2026-09-28): the short SHA left it, so
// the position is the line's only reference to the member on either walk.
// The member's NAME still has its one owner, HistoryMode::member_label
// (app_state.h), which the `'` load confirmation reads — the line does not.
//
// THE SEGMENT APPEARS ONLY WHEN THE SCALE CHANGED (architect 2026-08-05,
// superseding the arc's unchanged-token report): a value that both sides agree
// on is not a difference, and this view shows differences. So there is no
// unchanged arm at all, and the both-sides-empty case is covered by the same
// test rather than by one of its own. A side carrying no `scale=` line has an
// EMPTY token and shows as its bracket with nothing after it — unreachable
// while `scale` is a required settings key and every walk member is
// strict-load clean, and kept as the least-surprising shape rather than a
// refusal.
//
// THE POSITION IS THE ACTIVE WALK'S (2026-08-07): `x/y` reads the viewed
// member's DISPLAYED NUMBER and the walk's count, so a Local tab counts the
// session's own timeline states with the same two numbers in the same place.
// THE COUNT RUNS UPWARD FROM THE PIECE'S PAST since 2026-09-17 — the oldest
// member is 1 and the newest is y — and the arithmetic that turns the walk's
// index into that number has ONE OWNER, HistoryMode::member_number
// (app_state.h). THE NUMBER IS ZERO-PADDED to the count's digit count
// (architect 2026-09-28): `03/14`, `007/114`, `1/1`, `0/0` — the position
// keeps one width while `,` / `.` walk, so the segments after it hold still.
//
// The reference is non-const because both walks COMPUTE THEIR DELTA LAZILY AND
// CACHE IT (displayed_delta, app_state.h) — the composition itself writes
// nothing.
static std::string history_walk_line(AppState& app) {
    static constexpr const char* kSegmentSeparator = " | ";
    const std::size_t count = app.history_mode.walk_count();
    // AN EMPTY WALK READS `0/0` (2026-08-07), where it used to read nothing at
    // all: a blank cell beside a blank lane says only that the cell has stopped
    // working. ONE SPELLING FOR BOTH WALKS, and since 2026-08-08 only the
    // COMMIT walk can reach it — its empty window, a visit opened before the
    // prefetch has delivered member 0. The LOCAL walk is never empty while the
    // mode stands: the one entry owner binds it before `active` goes up and its
    // member count is U + R + 1, so a session that has authored nothing reads
    // `1/1` — one state compared against itself. The zero arm therefore
    // describes the commit side alone now, and it lives inside member_number
    // with the counting it belongs to rather than as a test of this line's own.
    const std::string total = std::to_string(count);
    std::string line = std::to_string(
        app.history_mode.member_number(app.history_mode.walk_index()));
    if (line.size() < total.size())
        line.insert(0, total.size() - line.size(), '0');
    line += '/';
    line += total;
    // THE GITHUB SEGMENT, SECOND, AHEAD OF THE SCALE (architect 2026-09-27;
    // moved ahead of the scale 2026-09-29): how this device stands against
    // GitHub as of the last check — `GitHub: checking...` / `up to date` /
    // `ahead` / `behind` / `diverged` / `offline` / `refused`
    // (github_status_word) — in the line's own `Label: value` shape, on BOTH
    // walks, because what Ctrl+S does in the view depends on it whichever walk
    // is showing. It stands ahead of the scale because the state cell clips
    // at the verbs (paint_bottom_row_buttons_and_clock): where the line is
    // wider than the cell — the tablet's, with the scale segment showing —
    // the cut falls on the scale, which the diff lane also shows, and never
    // on the word Ctrl+S forks on. A visit with no clone (the local fallback)
    // has no segment, and neither has an Unchecked status. The position
    // always precedes it (`0/0` at worst), so the separator is unconditional.
    if (history_remote_walk_available(app)) {
        if (const char* word = github_status_word(app.github_status)) {
            line += kSegmentSeparator;
            line += "GitHub: ";
            line += word;
        }
    }
    // THE SCALE SEGMENT, LAST: something always precedes it (the position,
    // `0/0` at worst), so the separator is unconditional.
    const GuiHistoryCommitDelta* d =
        app.history_mode.displayed_delta(app.history_compare());
    // No unavailable-delta arm: walk membership is the strict whole-set load
    // (history_diff.h's gate, 2026-08-04), so every commit this line can name
    // has a real delta — the old `Ambiguous` token died with the display
    // machinery it named.
    if (d && d->scale_changed) {
        line += kSegmentSeparator;
        line += "Scale: ";
        line += history_diff_label("[-]", /*disabled=*/false,
                                   d->then_scale_token);
        line += ' ';
        line += history_diff_label("[+]", /*disabled=*/false,
                                   d->now_scale_token);
    }
    return line;
}

// (GuiPaintHandler::paint_status_bar IS DELETED — architect 2026-08-29, the
// evening of the day the STATUS BAR landed. The bar was the window's last
// lane, three bands on the row-7 crop's measure, carrying the process line or
// the `h` walk line in a LEFT cell and the resolved readout in a RIGHT one.
// The architect ruled it off after seeing it: the state text belongs on the
// toolbar he already reads, and the bar duplicated that panel's own foot. The
// LANE, its two rect accessors, its three height accessors and the window-foot
// seam went with the painter; THE STATE TEXT is row 8's own cell, painted by
// paint_bottom_row_buttons_and_clock right of the clock, and THE RESOLVED
// READOUT retired whole — Ctrl+C copies the value and Ctrl+J goes to the
// marker it came from. `history_walk_line` above is untouched: the composer
// still stands beside its one caller, which is now that cell.)

void GuiPaintHandler::paint_icon_row(cairo_t* cr) {
    // THE ICON ROW (top lane 2, directly under the menu row; row 4 of the
    // redesign): the ground the menu row above it shares since 2026-10-01,
    // WITH NO BORDER OF ITS OWN (architect 2026-10-01: a line there read as
    // "a double border"; the trim lane under it is FLUSH since 2026-10-02,
    // so the boundary is the row's ground meeting the lane's dither and
    // thumb, and where gap 1 opens it is simply the ground meeting the gap's
    // window ground of the same value), and groups of 23 x 22 Windows-px
    // cases touching within a group, eight px of bare ground between groups
    // and no separators (the roster block above) — TWENTY members in SEVEN
    // groups outside the `h` view and NINETEEN in seven inside it (the width
    // math below is the count's one statement), RE-COUNTED off the roster
    // enum, the painter's tables and the divider owner rather than adjusted:
    // the toolbar four (Save / Undo /
    // Redo / Render, the deleted row 2's, leading the row — SAVE ALONE and
    // then the other three in a group of their own since 2026-09-29) with COPY
    // VALUE between Redo and Render in that group (up from the bottom row,
    // 2026-09-29 evening; EDIT FLAG stood behind Render for that evening's
    // hour and was deleted), THE ZOOM
    // GROUP — the VIEWPORT CLASS whole since the
    // architect's 2026-08-27 merge: FULL ZOOM OUT (2026-08-12; the stepped
    // zoom buttons in front of it removed 2026-09-25, and CENTER behind it
    // moved to the bottom row's walk group 2026-09-29) leading since the Show
    // trim region button that led it was deleted on 2026-09-22, THE
    // WAVEFORM MAGNIFICATION LAMP behind it (2026-09-22), FOLLOW (in from
    // the dissolved mass-marker group on 2026-08-27; out for the hours of
    // 2026-09-23) and THE RESTRICT UNDO TO CURRENT VIEW LAMP closing the group
    // (2026-09-04, arriving from the toolbar group later that day
    // because it is a viewport gesture too) — THE ITERATION GROUP (the BPM
    // opener and grid iteration mode, back from the deleted menu row later
    // that same day, with FLATTEN behind them since 2026-09-19) — the
    // RENDER-ENTRY group
    // (listen and the READ-ONLY toggle, the architect's own order on
    // 2026-08-14 less the load-in-place, which left for the history group on
    // 2026-09-01, and SETTINGS and ENABLE TOOLTIPS at its tail since
    // 2026-09-29, help after settings since 2026-10-01) — then, RIGHT-ANCHORED
    // since 2026-10-06, THE HISTORY OPENER alone in its group
    // (kIconRowHistoryOpener, where the reason is stated) one group gap left
    // of THE VIEW GROUP, FLUSH AT THE ROW'S RIGHT EDGE since 2026-10-01
    // (kIconRowViewGroup, where its provenance and the overflow rule are
    // stated). IN THE `h` VIEW the history group's six
    // companions stand in the slots of the toolbar group behind Save and of
    // the iteration group (kIconRowHistoryStandIns, the swap's one owner).
    //
    // ONE MODE SWAP AND NO OTHER HIDING (architect 2026-10-05, partly
    // reversing 2026-08-14's "no more hiding/showing icons in top icon row"):
    // the history stand-ins are the row's one state-dependent membership,
    // and they replace only groups the view greys whole (the companions
    // publish empty rects outside the view, the replaced members inside it).
    // Everything else a mode refuses
    // wears the DEAD FACE, and only a window too narrow for the row covers
    // members otherwise, under the view group's overflow rule — a width,
    // never a state. The mode-collapsing roster of 2026-08-12 (which skipped
    // members across whole consumed groups with an owed-separator state
    // machine) stays deleted; the walk below is a left-to-right accumulation
    // whose one fork is the stand-in table's.
    //
    // THE WIDTH MATH, RE-DERIVED from the roster after each move (architect
    // 2026-10-02, the Windows case; restated 2026-10-06 for the opener's
    // right-anchored seat): in Windows px, the 8-px pad + 23-px cases
    // touching + an 8-px gap between groups (groups minus one of them — the
    // opener's and the view group's own included). Outside the `h` view the
    // LEFT WALK is sixteen members in five groups from the left pad,
    //   8 + 16·23 + (5−1)·8 = 8 + 368 + 32 = 408,
    // and inside it fifteen in five (the iteration group's three slots
    // holding the two reading lamps),
    //   8 + 15·23 + (5−1)·8 = 8 + 345 + 32 = 385;
    // the RIGHT-ANCHORED span from the right edge — the opener's gap and
    // case, the view group's gap and three cases, and the 8-px lead-out — is
    //   8 + 23 + 8 + 3·23 + 8 = 116,
    // so the row holds every group, with the full gap on both sides of the
    // opener's, in any window at least 408 + 116 = 524 Windows px wide
    // outside the view and 385 + 116 = 501 inside it.
    //
    // THE DEVICE WIDTHS are taken off THE PAINTED WALKS, not off 524·factor:
    // every element is its own scaled_px (render.h's composite rule), so the
    // device width is 2·[8s] + N·([3s] + [16s] + [4s]) + 6·[8s] with each
    // bracket a banker's rounding and N the members standing (20 outside,
    // 19 inside). The laptop's 138 % paints 567 + 161 = 728 of its 1920
    // (535 + 161 = 696 in the view); the tablet's 275 % paints 1118 + 318 =
    // 1436 of its 2304 (1055 + 318 = 1373 in the view), and its 400 % 1632 +
    // 464 = 2096 (1540 + 464 = 2004 in the view), clearing the panel by 208. THE FIT
    // CEILINGS, every scale below each one fitting too: the tablet's 2304
    // holds the row in both states up to 440 % (the view's own up to 459 %),
    // the laptop's 1920 up to 365 % (384 %). The row's width succession is
    // in git history; a roster move restates these numbers.
    //
    // THE MARGIN IS THE THING TO WATCH on this row: every further member costs
    // a 23-px case and a NEW GROUP a further 8 — at the tablet's 400 % 92 and
    // 32 device px, room for TWO more members outside the view (2096 + 2·92 =
    // 2280 of 2304), a third cropping under the view group (2372).
    //
    // NO FOCUS SWAP HERE: the ground has one value focused and unfocused
    // (render.h's palette says so), and so has the menu row's.
    //
    // THREE FACES AND NO HOVER (architect 2026-10-02, the Windows-95 design;
    // the one painter is paint_button_box, above, in its TOOLBAR family):
    // every button REST is a SOFT RAISED case on the ground; a CHECKED button
    // — the live fact its chord flips (redesign_button_selected), the view
    // group's lit view among them — is SOFT SUNKEN over the Hilight dither
    // with its glyph one Windows px down and right; a PRESS is soft sunken on
    // the plain ground with the same shift, and wins over the checked face
    // while it is held. The pointer's only cue over a button is the cursor
    // (pointer_cursor_kind); the pointer walk still finds the tooltip's
    // button (recompute_redesign_button_hover).
    // THE DISABLED FACE IS THE TWO MODES' AND THE TOOLBAR MIGRANTS':
    // the row's own members never grey for a REFUSAL —
    // presses always dispatch and the CHORDS' OWN refusals answer (loading
    // blocks everything, and each arm keeps its home-view, empty-selection and
    // occupied-frame guards), inherited through on_key rather than mirrored —
    // while the toolbar four
    // BROUGHT their real disabled derivations with them at the 2026-08-12
    // relayout (Undo/Redo's locked-tab and empty-stack terms, Save's
    // in-flight lockout, Render's source path — redesign_button_enabled's
    // own arms, painted by this body's generic engraved face with nothing added
    // here). THE TWO RULED EXCEPTIONS ARE BOTH MODES rather than refusals,
    // which is what the per-press refusals above cannot express: the `h`
    // HISTORY VIEW greys every button in this row whose act it consumes
    // (architect 2026-08-04, at the face code below), and since 2026-08-15 the
    // per-tab READ-ONLY LOCK greys what it blocks — THREE of them on this row
    // (the ITERATION PAIR, back from the deleted menu on 2026-09-04, and
    // FLATTEN, which joined their group 2026-09-19 and takes the lock inside
    // its own predicate rather than beside it; the four
    // marker verbs carried the same term down to the
    // bottom row on 2026-08-18, and the membership is inventoried once at
    // redesign_button_enabled) — so the lock looks the way the view already
    // looks (architect; the read-only-LEGAL buttons beside them stay lit, which
    // is the 2026-08-07 band ruling). A mode is entered deliberately and does
    // not flicker, which is exactly what separates these two from the refusals
    // the row still swallows silently. (THE NEVER-GREY RULE'S INVERTED
    // EXCEPTION — the history companions' resting grey outside the view —
    // stood on this row 2026-08-18..2026-10-05 and reaches no pixel since:
    // outside the view the companions do not stand. Their arm is still at
    // redesign_button_enabled, which says why it stays.)
    const GuiRect lane = top_icon_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;

    // THE LANE IS WINDOWS' MENU-BAR-PLUS-TOOLBAR STACK (render.h's icon_row_*
    // block): no border of its own beyond the foot air before the trim lane.
    cairo_save(cr);

    set_palette_source(cr, palette().ground);
    cairo_rectangle(cr, lane.x, lane.y, lane.w, lane.h);
    cairo_fill(cr);

    // THE FIRST ETCHED LINE PAIR, Windows' own menubar/toolbar separator
    // (architect 2026-10-05): under the (absent) menu row's ground at the
    // lane's top, spanning the lane's whole width like Windows' own and
    // painted over the ground fill above. Inert ground for input, like the
    // air around it. The SECOND pair, after the toolbar's own air below the
    // case, waits until btn_h is known (below).
    paint_relief_etched_hline(cr, lane.x, lane.y, lane.w);

    // (NO FONT IS SELECTED HERE, and that is the row's own fact since
    // 2026-08-11: this lane paints geometry and icons only. A text face was
    // named here for the four view radios' shaped LETTER faces,
    // which the architect replaced with glyphs that day.)
    // THE CASE AND ITS GLYPH, each a composite of its rounded parts
    // (render.h's icon-row block): the glyph at the case's (3, 3).
    const int btn_w     = icon_case_w_px();
    const int btn_h     = icon_case_h_px();
    const int glyph_px  = icon_glyph_px();
    const int group_gap = icon_group_space_px();

    // THE CASE STANDS ON THE TOP ETCHED PAIR AND THE TOOLBAR'S OWN AIR
    // (icon_case_top_offset_px, render.h: the one expression every reader of
    // the case's seat in the full lane takes), WordPad's three Windows px
    // under the case, the mirror three below it closing the toolbar band
    // before the second etched pair.
    const int btn_y = lane.y + icon_case_top_offset_px();

    // THE SECOND ETCHED LINE PAIR, right after the toolbar's own air below
    // the case — the foot air (before the trim lane) stands below IT, not
    // below the case.
    paint_relief_etched_hline(cr, lane.x,
                              btn_y + btn_h + scaled_px(kIconRowAirPx),
                              lane.w);

    // THE RIGHT-ANCHORED PLACES ARE RESOLVED FIRST, because they decide where
    // every group to their left may paint (the overflow rule at
    // kIconRowViewGroup): the view group's last button's right edge sits one
    // row pad (icon_row_pad_x, the row's own 8px lead-in, read as the
    // lead-out) in from the lane's right edge, its members touching, and its
    // eight-px group gap on its left; the HISTORY OPENER's case stands just
    // left of that gap (kIconRowHistoryOpener, architect 2026-10-06), with
    // its own eight-px gap on its left. THE LEFT GROUPS' LIMIT IS WHERE THE
    // OPENER'S GAP BEGINS: they paint under a clip ending there and publish
    // only the columns they painted, so a window too narrow for the row
    // covers them and never the opener or the view group.
    const int view_n = static_cast<int>(std::size(kIconRowViewGroup));
    const int view_w = view_n * btn_w;
    const int view_x0 = lane.x + lane.w - icon_row_pad_x() - view_w;
    const int history_x = view_x0 - group_gap - btn_w;
    const int left_limit = history_x - group_gap;

    // ONE MEMBER'S PAINT, shared by both walks: publish what is painted, then
    // the face and the glyph at the member's full box — the clip, when
    // the overflow rule has set one, cuts the pixels and the published rect
    // says exactly which survived.
    const auto paint_member = [&](const IconRowDef& def, int bx,
                                  const GuiRect& published) {
        AppState::RedesignButtonFace& face = publish_button_face(
            cr, app, audio, playback, target_render, def.id, published);

        // THE SIXTH FACE, WORN FOR TWO MODES: the `h` history view (architect
        // 2026-08-04) and, since 2026-08-15, the per-tab READ-ONLY LOCK. Both
        // are ruled EXCEPTIONS to the never-grey rule above and both are MODE
        // statements — that is what earns them the face, a refusal alone never
        // does. EVERY button standing in this row that the view consumes
        // wears it — the zoom group behind Full Zoom Out (magnification,
        // Follow, Restrict Undo), listen, the read-only toggle, Settings, and
        // the MOMENT-STATE Save (an empty head delta or a checkpoint in
        // flight); the two groups the view consumes WHOLE do not stand in it
        // since 2026-10-05, the history companions standing in their slots
        // (kIconRowHistoryStandIns). The view group, Full Zoom Out, the
        // tooltip lamp and the history opener stay live, as do the history
        // COMPANIONS — each
        // on the derivation's own answer, which is not one reason but two:
        // bare `u`, `,` and `.` are the mode's own VOCABULARY and answer live
        // whatever the session holds, while bare `v` and bare `'` are
        // ALLOWLIST admissions carrying a session term, so the walk answers
        // DEAD for Revert with no subject and for Load in place over a
        // memberless walk. Which is which is
        // DERIVED from the mode's own gates
        // (history_mode_disables_button, input_pointer.cpp, where the whole
        // partition is inventoried); nothing here decides membership. SAVE IS
        // THE ONE THAT NO LONGER COMES FROM THAT DERIVATION (2026-09-01): its
        // two moment-state terms left the allowlist for the act when a gate's
        // membership became the chord's alone, so the grey arrives from
        // redesign_button_enabled's own Save arm instead — the same face, the
        // same two states, another owner.
        //
        // (THE COMPANIONS' RESTING GREY OUTSIDE THE `h` VIEW reached this
        // row 2026-08-18..2026-10-05; since then they do not stand outside
        // the view, and their arm at redesign_button_enabled reaches no
        // pixel there.)
        // THE LOCK'S SET IS HAND-LISTED at redesign_button_enabled with
        // read_only_key_blocked named as its owner, and that arm's own case
        // list is where the membership is stated — the marker verbs
        // are the BOTTOM row's since 2026-08-18, leaving THE
        // ITERATION GROUP as this row's set — its two since 2026-09-04, when
        // the buttons came back from the deleted Iterations menu, and FLATTEN
        // since 2026-09-19, whose own predicate carries the lock (the copy/paste
        // pair left with the 2026-08-20 propagate relocation and the lock
        // still eats their chords with no face here to grey; Listen left the
        // set outright on 2026-08-28, bare `l` joining the allowlist with the
        // in-app player, and the Load in place took its lock term into the
        // history group's own arm on 2026-09-01) — the one place in
        // this face's membership that is not
        // derived, for reasons recorded there. This painter decides none of
        // the three.
        //
        // THE FACE IS THE GLYPH'S ALONE, ENGRAVED (architect 2026-10-02, the
        // AB set; Windows' DSS_DISABLED, icons::draw_engraved): the glyph's
        // disabled mask in Hilight one Windows px right and down, then in
        // Shadow at its place, and the box keeps its edge whole (a disabled
        // button keeps its raised edge). A dead CHECKED button (the
        // cumulative reading, say) stays checked: the mode cannot change that
        // state, so hiding it would be a lie, and the engraved glyph says
        // "true, but not yours right now".
        // The press face is gated on the live bit rather than trusted: the
        // claim never records a press on a disabled button, but a button can
        // go dead UNDER a held press with no pointer event to refresh it.
        const bool pressed =
            face.enabled && redesign_button_pressed_face(app, def.id);
        const ButtonBoxFace box = paint_button_box(
            cr, GuiRect{bx, btn_y, btn_w, btn_h}, face.selected, pressed,
            ButtonFamily::Toolbar);

        // THE 16-px GLYPH at the case's (3, 3), each path in its own color
        // from the icon table — or, dead, engraved.
        //
        // EVERY BUTTON TAKES THIS ARM since 2026-08-11, when the four view
        // radios' shaped LETTER faces, the only other kind, took glyphs. The
        // row paints no text at all, which is why nothing here selects a font.
        //
        // THE GLYPH IS THE STATE RESOLVER'S, not the table's
        // (redesign_button_icon, above): this row hosts the TOOLBAR PAIR since
        // the 2026-08-12 relayout dissolved row 2 into it, and their stateful
        // faces are glyph swaps now that the labels are gone — Save wears
        // VcsCommit in the history view and while a checkpoint publishes,
        // Render wears DialogCancel mid-render. It goes through the resolver
        // for EVERY button because the table icon is what the resolver returns
        // when a button has no override, so there is no membership to keep.
        // (This is the site the relayout dropped: it took the table icon
        // directly, which left the resolver caller-less and both ruled faces
        // unpainted for a day — fixed 2026-08-13. THE BOTTOM ROW'S OWN DRAW
        // TAKES THE SAME CALL since 2026-08-15: its superseded claim was that
        // "none of the transport eight has a stateful glyph, the play/stop
        // pair being TWO buttons over one chord rather than one button with
        // two faces", and the architect made it one button with two faces
        // that day.)
        const icons::Icon glyph = redesign_button_icon(app, def.id, def.icon);
        // draw_cased takes the CASE's own corner, not a (3, 3)-offset glyph
        // origin: the seat is its own (icons.h's PLACEMENT).
        if (face.enabled)
            icons::draw_cased(cr, glyph, bx, btn_y,
                              static_cast<double>(glyph_px), box.shift);
        else
            icons::draw_cased_disabled(cr, glyph, bx, btn_y,
                                       static_cast<double>(glyph_px),
                                       box.shift,
                                       static_cast<double>(relief_line_px()));
    };

    // THE LEFT GROUPS' WALK: one left-to-right accumulation, every member
    // placed. A group LEADER (redesign_button_opens_icon_group, app_state.h —
    // the roster's own divider owner) takes the eight-px group gap ahead of
    // itself and everything else touches its neighbour; the ROW'S FIRST
    // member needs no special case, `first` swallowing the gap its own
    // leader would owe. (The collapse state machine that skipped members and
    // carried an OWED separator across them is deleted, 2026-08-14; the one
    // membership fork left is the history stand-ins', 2026-10-05.) Everything
    // here paints under the
    // clip that ends where the history opener's gap begins, and each member
    // PUBLISHES ITS RECT CUT AT THAT COLUMN — whole at every width the row
    // fits (the laptop's and the tablet's included), its painted columns
    // alone when the right-anchored pair covers part of it, and an empty rect
    // when it covers it whole, which contains no point and refreshes its bits
    // unconditionally (publish_button_face).
    cairo_save(cr);
    cairo_rectangle(cr, lane.x, lane.y,
                    std::max(0, left_limit - lane.x), lane.h);
    cairo_clip(cr);
    int x = lane.x + icon_row_pad_x();
    bool first = true;
    const auto place_member = [&](const IconRowDef& def) {
        const int shown_w = std::clamp(left_limit - x, 0, btn_w);
        paint_member(def, x, GuiRect{x, btn_y, shown_w, btn_h});
        x += btn_w;
    };
    // A MEMBER THAT DOES NOT STAND in this state still publishes: an empty
    // rect, which contains no point and refreshes its bits unconditionally
    // (publish_button_face), so neither a press nor the drift comparator
    // (main.cpp's tick) reads a rect or a bit from the other state's row.
    const auto publish_absent = [&](RedesignButton id) {
        (void)publish_button_face(cr, app, audio, playback, target_render, id,
                                  GuiRect{});
    };
    // THE STAND-IN SWAP (kIconRowHistoryStandIns, its one owner): in the `h`
    // view a leader that names a stand-in group paints that group in its
    // place, at its gap, and the rest of the replaced group is skipped up to
    // the next leader; outside the view the stand-in groups never paint.
    const bool history = app.history_mode.active;
    bool replacing = false;
    for (const IconRowDef& def : kIconRowButtons) {
        const bool leads = redesign_button_opens_icon_group(def.id);
        if (leads) {
            replacing = false;
            if (!first) x += group_gap;
            if (history) {
                for (const IconRowStandIn& si : kIconRowHistoryStandIns) {
                    if (si.replaces != def.id) continue;
                    replacing = true;
                    for (const IconRowDef& m : si.members) place_member(m);
                }
            }
        }
        first = false;
        if (replacing) {
            publish_absent(def.id);
            continue;
        }
        place_member(def);
    }
    if (!history) {
        for (const IconRowStandIn& si : kIconRowHistoryStandIns)
            for (const IconRowDef& m : si.members) publish_absent(m.id);
    }
    cairo_restore(cr);

    // THE RIGHT-ANCHORED PAIR, PAINTED LAST AND WHOLE, each member publishing
    // the full case it paints: the history opener at history_x, at one x in
    // both states (its gap is the one the left groups stop at), then the view
    // group's three members from view_x0 (its leader Source+Warp's gap
    // standing between the two).
    paint_member(kIconRowHistoryOpener, history_x,
                 GuiRect{history_x, btn_y, btn_w, btn_h});
    int vx = view_x0;
    for (const IconRowDef& def : kIconRowViewGroup) {
        paint_member(def, vx, GuiRect{vx, btn_y, btn_w, btn_h});
        vx += btn_w;
    }

    cairo_restore(cr);
}

// THE BOTTOM ROW'S BUTTON CLUSTER AND CLOCK — THE UNIFIED BOTTOM ROW (rows 8
// and 9 merged 2026-08-12): permanent on every host — ordinary
// mouse-clickable buttons, no touch mode, no flag, no detection. LEFT TO
// RIGHT (architect 2026-09-29, for the right-handed tablet: the most-used
// buttons at the bottom right):
//
//   THE CLOCK at the lane's left pad — the tab letter and the timestamp in
//   a TIME FIELD (architect 2026-10-02 for Windows' status bar, 2026-10-05
//   for the field's face, height and fixed width, all at kTimeFieldHeightPx
//   below), with THE STATE LINE on
//   the ground beside it at the normal face, clipped one group space short
//   of the right block (architect 2026-10-03);
//
//   and, FLUSH AT THE RIGHT MARGIN, four groups, eight Windows px of bare
//   ground between two of them (architect 2026-10-02: no separators):
//   THE MARKER VERBS (kMarkerVerbGroup) — drop (bare `s`), delete (Delete),
//   disable (Ctrl+D), inherit (Ctrl+N), JUMP TO DEFINING MARKER (Ctrl+J,
//   since 2026-09-29) and ADD TO SELECTION (bare `k`, the sticky ctrl, the
//   row's ONE LIT FACE while the mode stands). The four verbs are the row's
//   resting greys on a locked tab; the jump and Add to selection are not,
//   both being navigation (EDIT FLAG and COPY RESOLVED VALUE stood between
//   Toggle inherit and Add to selection until 2026-09-29; Copy is the icon
//   row's since, and Edit Flag was deleted);
//   THE MARKER WALK (kTransportWalkGroup) — PREVIOUS MARKER (Shift+Tab, its
//   ctrl press the paired march), NEXT MARKER (Tab, its shifted press
//   Shift+Tab), CENTER (bare `c`, down from the icon row) and SWITCH TAB
//   (Ctrl+Tab, its shifted press the paired march), since 2026-09-29;
//   THE CARDINAL ARROWS (kTransportArrowGroup) — DOWN, UP, LEFT, RIGHT (bare
//   Down/Up/Left/Right), a single line and not a d-pad, and THE ROW'S HOLD
//   GESTURE: held past the hold beat they repeat their own chords at the
//   compositor's key-repeat rate (the record at the arrows' chord-table rows,
//   input_pointer.cpp). The pressed interior is the ordinary arm's, and the
//   hold has no cue of its own;
//   THE TRANSPORT (kTransportGroup), closing the row — skip-back (bare Home,
//   Ctrl+Home the whole-piece jump), THE ONE PLAY/STOP BUTTON (bare Space,
//   whose GLYPH and TOOLTIP swap on the live audition bit — the reason this
//   row's paint goes through redesign_button_icon) and skip-forward (bare
//   End, Ctrl+End). The last button's right edge is one lane pad in from the
//   lane's right edge.
//
// EVERY BUTTON GREYS WHERE ITS PRESS WOULD BE A NO-OP (the truthful-buttons
// ruling, redesign_button_enabled), the `h` view's derived partition on top.
// Nothing on this row is conditional on a mode, and no member of it publishes
// a zero rect except under a modal.
//
// (A CENTERED ESC BUTTON shipped between the groups on row 8's first day and
// was DELETED at the architect's live pass — "looks like a missing button
// with that cross out"; the mid-render CANCEL lives on the RENDER button now,
// and bare Esc is keyboard-only.)
//
// THE BUTTONS ARE THE ICON ROW'S (the unification's point — "the same size as
// the other icon buttons", bigger finger targets without stealing waveform
// height): Windows 95's toolbar case, 23 x 22 Windows px with its 16-px glyph
// at (3, 3), read from the icon row's own accessors (render.h) so the two rows
// cannot drift apart. SINCE 2026-08-14 EVERY METRIC ON THIS ROW IS THE ICON
// ROW'S, read from its accessors and constants rather than authored here
// (architect: "make sure bottom row is same height and metrics (padding,
// etc.) as main icon row") — the case and its glyph, the buttons touching
// within a group, the eight-px gap between groups (icon_group_space_px), the
// 8px pad at both ends (icon_row_pad_x, paint_handler.h) and the lane's
// 28-px content height — the toolbar's own air, case and air, with neither
// of the icon row's etched lines nor its foot air (bottom_row_content_h_px
// delegating to icon_row_content_h_px, render.h). One source, so a retune of
// the icon row's toolbar band carries here by construction. (Row 8's kdenlive transport metrics, its
// 26-px boxes and its own ruled separators — the etched slot between the
// groups, 2026-08-11 to 2026-10-02 — are git history.)
//
// EVERYTHING ELSE IS THE ICON ROW'S OWN MODEL (the relief, the case on the
// row's air): same ground, same three faces, same engraved dead glyph. WHO WEARS THE DEAD FACE HERE, re-derived 2026-09-29 evening — EIGHT
// of the seventeen in the `h` view, where it used to be one: there
// the derived partition greys the PLAY/STOP button (Space is consumed there),
// UP and DOWN (bare
// Up/Down are neither the mode's
// vocabulary nor on its allowlist; LEFT / RIGHT are the mode's own playhead
// step since 2026-09-26 and grey on their own arm), the FOUR MARKER VERBS and
// JUMP TO DEFINING MARKER (Ctrl+J, consumed in there like
// the verbs' chords); ADD TO SELECTION STAYS LIT SINCE 2026-09-17, bare `k`
// being on that mode's allowlist now and the lamp producing the view's own
// multi-selection; the two
// SKIPS and THE WALK GROUP'S FOUR stay lit, Home/End being the mode's
// own absolute jumps, Tab/Shift+Tab its diff-flag cycle, `c` its centring and
// Ctrl+Tab on its allowlist (architect-confirmed for the skips). PREVIOUS
// MARKER greys only under a lit grid iterations with no reverse step left
// (its twin, the march, refusing there too). Outside the view the four VERBS
// grey on a locked tab, their own
// gate — JUMP TO DEFINING MARKER, seated among them, does NOT, its chord being
// navigation the lock admits — and since 2026-08-30 EVERY MEMBER BUT THE
// TRANSPORT THREE AND ADD TO SELECTION greys on the
// selection's state where its press would be a consumed no-op (the
// truthful-buttons ruling, reversing the architect's 2026-08-15 always-on
// ruling, which was made about the RESTING face of the ten members the row
// then had). All at redesign_button_enabled; nothing decided here.
// THE SELECTED FACE IS WORN BY ADD TO SELECTION AND NOTHING ELSE HERE
// (2026-08-18): the sticky-ctrl mode lights while it stands, which is the
// roster's standing rule for a mode and the lamp iteration already
// wears up in row 4. (The row was lampless for the hours between the Cumulative
// toggle going back to the icon row with the rest of the history group —
// it had been the row's one lamp from 2026-08-14 — and this arrival; Play and
// Stop wore one for hours on 2026-08-15, as a RADIO PAIR on the live audition
// bit, and the collapse of that pair into ONE button took it with them, the
// bit picking the button's GLYPH now. The `selected` term in the shared
// expressions below was kept through that gap rather than dropped, because it
// is the icon row's own face logic verbatim and the row has now gained and
// lost a lamp four times — which is why this arrival cost the body nothing.)
// THAT IS A RULING RATHER THAN AN UNFINISHED SWEEP, and it took back the
// whole-row honesty ruling of the same morning in three steps: the four
// arrows and the four history companions first (a glyph blinking on every
// marker selection restates what the selection already shows), then the two
// SKIPS (bare Home / End are not pure jumps — each also stops a live audition,
// clears the selection, no-op jump included, so a grey
// promised less than the key delivers), then PLAY and STOP last, on the reason
// that covers those ten: "there's not a whole lot of value derived from
// the icon faces changing, and it is a little distracting... the user is
// expected to know that with the playhead outside trim it's not going to play
// in target view". (The ARROWS had no in-view answer to paint at the time —
// they were not painted in there at all under the cluster swap — and since
// 2026-08-18 they do paint in there, GREYED: the mode consumes bare
// Up/Down/Left/Right, so the derived partition dead-faces all four. That is
// the partition's own answer and no part of the always-on ruling, which is
// about the RESTING face.)
// The ONE box-model difference is the lane's top row: a 1px strip of ground
// above the content band, on the waveform side, where the row's border-top
// stood — NO LINE IS DRAWN THERE since 2026-10-02 (architect: nothing between
// the well and this row, the well's own bottom line being the seam), and the
// row is kept so nothing on the row moved (the lane's chrome is
// paint_bottom_strip's, which calls the body below onto its content band).

// THE ROW AUTHORS NO METRIC OF ITS OWN (the block above): the cases, glyphs,
// gaps, pads, relief and content height are all the icon row's — and since
// 2026-10-02 so does the render player's modal row in this lane, whose two
// etched separators retired with the roster's (Sound Recorder has none): its
// groups stand the roster's eight Windows px apart (paint_modal_dialog's
// player branch).

// The painter's half of the row's roster: four groups, all in the right
// block and painted left to right in the order the body lays them (verbs,
// walk, arrows, transport); each table is declared beside its reasons.
// The press claim's chord table (input_pointer.cpp) is the other half; both
// key off the same ids.
struct TransportRowDef {
    RedesignButton id;
    icons::Icon    icon;
};
// THE TRANSPORT THREE, the right block's LAST GROUP since 2026-09-29
// (architect: the most-used buttons at the bottom right for the right hand):
// play and stop are ONE stateful button (2026-08-15), so the table carries
// media-playback-START as that button's resting glyph and the resolver swaps
// in media-playback-STOP while an audition runs (redesign_button_icon —
// Save's and Render's own shape, and the reason this row's paint goes through
// the resolver).
constexpr TransportRowDef kTransportGroup[] = {
    {RedesignButton::TransportSkipBack,    icons::Icon::MediaSkipBackward},
    {RedesignButton::TransportPlayStop,    icons::Icon::MediaPlaybackStart},
    {RedesignButton::TransportSkipForward, icons::Icon::MediaSkipForward},
};
// THE SINGLE-MARKER VERBS (architect 2026-08-18, "move drop/delete/disable/
// toggle inherit to bottom right row"), the RIGHT BLOCK'S FIRST GROUP: drop
// (bare `s`, ListAdd's plus), delete (`Delete`, ListRemove's minus), the
// disable toggle (`Ctrl+D`, ViewHidden's no-sign) and inherit/collapse
// (`Ctrl+N`, InsertLink's chain — a pass marker links its tempo to its
// neighbor). They opened
// their own separator-led group in the ICON ROW from 2026-08-12 until this
// move and brought their four glyphs down unchanged.
//
// THE GROUP IS FIVE since later the same day, the architect seating ADD TO
// SELECTION himself — "add group selection icon ('Add to Selection') after
// toggle inherit, before the separator". It is the group's one MODE (bare `k`,
// EditSelect: the pointer with a plus) and therefore the only
// member of this table that ever wears the lit fill; the four verbs above are
// acts that complete.
//
// It was SEVEN from 2026-09-19 until 2026-09-29 — the four verbs, the EDIT
// FLAG BUTTON (2026-08-27), COPY RESOLVED VALUE (2026-08-29) and ADD TO
// SELECTION; the rest of the succession (the Marker Measure, the value drag
// lamp, the marker magnification, the Flatten button's hours) is in git
// history.
//
// THE GROUP IS SIX SINCE 2026-09-29 EVENING (architect): EDIT FLAG and COPY
// RESOLVED VALUE went up to the icon row (Copy's record is at
// kIconRowButtons; Edit Flag was deleted later that evening), and JUMP TO
// DEFINING MARKER — Ctrl+J,
// GoJumpDeclaration's jump arc — took the seat
// after Toggle Inherit, Add to Selection closing the group behind it. The jump
// was Copy Value's shifted twin until that day; it is a dedicated button now
// under the dissolved shift-twin rule (closed_questions.md). It is an act, wears no
// lamp, and greys where Ctrl+J would refuse (jump_to_value_source_actionable)
// and in the `h` view; neither lock greys it, the jump authoring nothing.
//
// (THE FLATTEN BUTTON stood between Toggle inherit and Edit flag for the hours
// of 2026-09-19 and went up to the ICON ROW'S ITERATION GROUP the same day
// (architect), taking its glyph with it — its only consumer.)
constexpr TransportRowDef kMarkerVerbGroup[] = {
    {RedesignButton::IconMarkerDrop,       icons::Icon::ListAdd},
    {RedesignButton::IconMarkerDelete,     icons::Icon::ListRemove},
    {RedesignButton::IconMarkerDisable,    icons::Icon::ViewHidden},
    {RedesignButton::IconMarkerInherit,        icons::Icon::InsertLink},
    {RedesignButton::IconJumpToDefiningMarker, icons::Icon::GoJumpDeclaration},
    {RedesignButton::IconAddToSelection,       icons::Icon::EditSelect},
};
// THE MARKER-WALK GROUP (architect 2026-08-15), the right block's second
// group between the verbs and the arrows — FOUR SINCE 2026-09-29 (architect),
// in this order: PREVIOUS MARKER (Shift+Tab, BboxPrev; its ctrl press is the
// paired march), NEXT MARKER (the walk: Tab, its shifted press Shift+Tab;
// its camera the audio view's; BboxNext), CENTER (bare `c`, ZoomOriginal,
// moved down from the icon row's zoom group) and SWITCH TAB (Ctrl+Tab,
// TabDetach; its shifted press is the paired march).
// A SHIFT- OR CTRL-MODIFIED CHORD MAY HAVE ITS OWN DEDICATED BUTTON since
// that day (architect: "an artificial constraint; the tablet is the only
// real development surface now") — Previous Marker is Next Marker's Shift
// form and Switch Tab its Ctrl form; Previous Marker had merged into the walk
// on 2026-09-22 under the rule that it could not, and the walk's modified
// presses stay. THE WALK PAIR WEARS THE ARROWS' OWN DRAWINGS (BboxPrev and
// BboxNext are GoPrevious's and GoNext's, icons.h's shared pairs).
constexpr TransportRowDef kTransportWalkGroup[] = {
    {RedesignButton::TransportWalkPrevious, icons::Icon::BboxPrev},
    {RedesignButton::TransportWalk,         icons::Icon::BboxNext},
    {RedesignButton::IconZoomOriginal,      icons::Icon::ZoomOriginal},
    {RedesignButton::TransportSwitchTab,    icons::Icon::TabDetach},
};
// DOWN, UP, LEFT, RIGHT — the architect's order, 2026-08-14, superseding the
// row's original vim order (h j k l = left / down / up / right) with no
// reasoning offered and none needed. All four are one construction, the
// set's cyan arrow — GoDown / GoUp for the verticals, GoPrevious / GoNext for
// the horizontals (the last two shared with the marker walk's pair).
constexpr TransportRowDef kTransportArrowGroup[] = {
    {RedesignButton::TransportDown,        icons::Icon::GoDown},
    {RedesignButton::TransportUp,          icons::Icon::GoUp},
    {RedesignButton::TransportLeft,        icons::Icon::GoPrevious},
    {RedesignButton::TransportRight,       icons::Icon::GoNext},
};
// (THE ARROWS' MODE TWIN IS DELETED — architect 2026-08-18. From 2026-08-14 a
// kHistoryClusterGroup of four history companions was painted at the arrows'
// own right anchor while the `h` view stood, the two fours swapping and
// whichever was unpainted publishing zero rects. The relayout moved those four
// back to the ICON ROW, so the swap had one cluster left and stopped being a
// swap: the table, the shown/hidden selection, the four zero-rect publishes
// and the mode term in this row's layout all went together. The arrows paint
// unconditionally now — nothing on this lane is conditional on a mode.)

// THE TIME FIELDS — ROW 8'S CLOCK AND THE RENDER PLAYER'S POSITION AND
// LENGTH, ONE FIELD SHAPE (architect 2026-10-05, measured on his ACID Pro 3.0
// and Vegas Audio screenshots and Windows' Sound Recorder): a period time
// field is a ONE-LINE SUNKEN field — paint_relief_status_sunken, the theme's
// Shadow top and left and Hilight bottom and right, over `clock_ground` under
// its digits in `clock_text` — kTimeFieldHeightPx tall, VERTICALLY CENTRED in
// its row's content band (a half-row tie toward the top, the cap rule's own),
// its run in THE BODY FACE (gui_font.h; no period field used a monospace
// face, ACID's big clock included), its cap band centred in the field by
// redesign_baseline: the recorded 9-row digits with 3 Windows px of face
// above and below them. The digits are TABULAR — Nimbus's every digit 0.556
// em less the one tracking (kGuiTrackingPx, gui_font.h, which every
// glyph takes alike), with no kerning between the digits, the colon and the
// point — so the colon and the point stand still as the time runs.
//
// EACH FIELD'S WIDTH IS FIXED — ITS WIDEST STRING (architect 2026-10-05:
// "a 1 is skinnier than a 0", nothing in the field or right of it may move as
// the time runs). The reserved cell is measured from the widest string the
// field can ever show in this face — the widest digit in every digit slot of
// "DD:DD.DDD" (kTimeShape) and, on row 8, the widest TAB LETTER in the
// letter's slot (kClockTabLetters) — shaped through the one chokepoint at the
// live size, so nothing is trusted to the face. The field is that cell plus
// kStatusPanelPadPx of face either side, its width ceiled.
//
// THE RUN IS RIGHT-ALIGNED (architect 2026-10-05, the period's ACID / Vegas
// fields): it ENDS AT THE CELL'S RIGHT PAD — the cell's ceiled width past its
// x, kStatusPanelPadPx inside the field's right line — and the fixed width is
// unchanged, so nothing moves as the time changes. On row 8 the pipe and the
// digits are ONE RUN, ` | ` and the timestamp, ending there, and the tab
// letter is painted RIGHT-ALIGNED AGAINST THAT RUN'S START: switching tab
// moves at most the letter's own left edge and nothing else — no letter's
// kerning can reach the pipe, the two being shaped apart. (A and B are one
// advance today, Nimbus's 0.667 em less the tracking, measured 2026-10-06;
// the slot is the widest so a face where they differ changes nothing.)
//
// TWO MINUTE DIGITS, and longer sources TRUNCATE (the ruling and what it costs
// are at format_timestamp, time_format.h). The cell is that format's width and
// no wider.
constexpr const char* kTimeShape = "DD:DD.DDD";

// The tab letters row 8's clock leads with (AppState::active_tab_view's two
// values) — the set the letter slot is measured over.
constexpr std::string_view kClockTabLetters = "AB";

// THE FIELD'S HEIGHT, 17 WINDOWS PX (architect 2026-10-05): ACID Pro 3.0's
// time fields run rows 716..732 of his lossless capture and Vegas Audio's
// 484..500 — one line, three px of face, the 9-px digits, three px of face,
// one line. One element, rounded once (scaled_px): 23 device px at 138 %, 47
// at 275 %, 68 at 400 %, against the body face's recorded cap band of
// 12.42, 24.75 and 36.
constexpr double kTimeFieldHeightPx = 17.0;

// THE AIR BETWEEN TWO TIME FIELDS — the render player's position and length
// (architect 2026-10-05): ACID's adjacent fields stand three Windows px of
// ground apart (columns 1136..1138 between its first two, the same capture).
constexpr double kTimeFieldGapPx = 3.0;

// (THE SEPARATOR → DIGITS OFFSET and THE SEPARATOR → TIMESTAMP DISTANCE,
// kClockCellOffsetXPx and kTransportSepToClockPx, retired 2026-10-02 with the
// render player's separators, their last reader: the air round the player's
// scrub and clock is the roster's group space.)

// THE FIELD'S HORIZONTAL PAD (architect 2026-10-02, Windows' status bar; the
// field's own name since 2026-10-05): the reserved cell stands 3 Windows px
// in from the field's line on each side (the laptop pixel's 4 re-authored at
// the unit's change). With row 8's clock cell at the lane's 8 px pad, its
// field's left line stands 5 px in from the window's edge. (The second
// panel, the state's, and its 2-px SB_SETPARTS gap, kStatusPanelGapPx,
// retired 2026-10-03: the state is a line on the row's ground.)
constexpr double kStatusPanelPadPx = 3.0;

// The time fields' metrics, MEMOISED ON THE SCALE — thirteen tiny shaping
// passes (ten digits, the two letters and the specimen) that answer the same
// thing on every frame, the face being fixed and the scale the only variable
// (the widths are the live face's own, gui_font.h).
// Single-threaded paint state; the waveform worker never reaches this file's
// text tiers.
//
// THE CELLS ARE ADVANCES: `time_w` is the specimen's advance sum and
// `letter_w` the widest letter's, so no glyph ever walks; row 8's cell is
// letter_w + the shaped ` | ` + time_w, each player field's time_w, and each
// cell is its field's damage box too.
struct TimeFieldMetrics {
    int    percent  = -1;     // the scale this was measured at
    double time_w   = 0.0;    // the widest-digit specimen's shaped width
    double letter_w = 0.0;    // the widest tab letter's shaped width
};
static TimeFieldMetrics g_time_field_metrics;

static const TimeFieldMetrics& time_field_metrics(const GuiFont& font) {
    TimeFieldMetrics& m = g_time_field_metrics;
    if (m.percent == font.percent) return m;
    char widest = '0';
    double widest_w = -1.0;
    for (char d = '0'; d <= '9'; ++d) {
        const char one[2] = {d, '\0'};
        const double w = text_shape::shape_text_run(font, one).width_px;
        if (w > widest_w) { widest_w = w; widest = d; }
    }
    std::string specimen(kTimeShape);
    for (char& c : specimen) if (c == 'D') c = widest;
    m.time_w = text_shape::shape_text_run(font, specimen).width_px;
    m.letter_w = 0.0;
    for (char l : kClockTabLetters) {
        const char one[2] = {l, '\0'};
        m.letter_w = std::max(m.letter_w,
                              text_shape::shape_text_run(font, one).width_px);
    }
    m.percent = font.percent;
    return m;
}

// THE BOTTOM ROW'S SEATS — ROW 8 AND THE RENDER PLAYER'S ROW ARE ONE ROW
// (architect 2026-10-05, on his 350 % screenshots, the player's Close 4 px
// left of and 2 rows under row 8's last button): both painters take their
// edges from this one owner, so the shared edges cannot drift. The LEFT PAD
// is where the left-aligned run starts (row 8's clock cell, the player's
// transport), the RIGHT PAD where the right-aligned block ends (row 8's
// transport three, the player's Load in place · Close) — the lane's one pad
// (icon_row_pad_x) from each edge, with no other reserve: a focused player
// button's frame (kModalFocusFramePx) paints in the pad's first line rather
// than pushing the block in. Every BUTTON on either row is THE TOOLBAR CASE
// (render.h's icon-row block, icon_case_w_px x icon_case_h_px) at the row's
// AIR under the content's top (kIconRowAirPx), the icon row's own seat on
// its own height; the faces differ (row 8 the toolbar family, the player
// the push-button family), the boxes do not. What lies between the two
// runs is each row's own: row 8's state line, the player's scrub, which
// fills it.
struct BottomRowSeats {
    int left_x  = 0;   // the left pad: the left-aligned run's first column
    int right_x = 0;   // the right pad: one past the right block's last column
    int case_y  = 0;   // the case's top: the content's top plus the row's air
    int case_w  = 0;   // the toolbar case, both rows' every button
    int case_h  = 0;
};
static BottomRowSeats bottom_row_seats(const GuiRect& content) {
    const int pad = icon_row_pad_x();
    return BottomRowSeats{content.x + pad, content.x + content.w - pad,
                          content.y + scaled_px(kIconRowAirPx),
                          icon_case_w_px(), icon_case_h_px()};
}

// ONE TIME FIELD'S RECT round a reserved cell starting at `cell_x`, centred
// in the content band [band_y, band_y + band_h).
static GuiRect time_field_rect(int cell_x, double cell_w, int band_y,
                               int band_h) {
    const int pad = scaled_px(kStatusPanelPadPx);
    const int h   = scaled_px(kTimeFieldHeightPx);
    return GuiRect{cell_x - pad, band_y + (band_h - h) / 2,
                   static_cast<int>(std::ceil(cell_w)) + 2 * pad, h};
}

// ONE TIME FIELD'S FACE: the ground, then the one-line sunken edge.
static void paint_time_field(cairo_t* cr, const GuiRect& field) {
    paint_cell_rect(cr, field, palette().clock_ground);
    paint_relief_status_sunken(cr, field);
}

void GuiPaintHandler::paint_bottom_row_buttons_and_clock(cairo_t* cr) {
    const GuiRect lane    = bottom_row_area(app);
    const GuiRect content = bottom_row_content_area(app);
    if (lane.w <= 0 || lane.h <= 0 || content.h <= 0) return;
    const int content_y = content.y;
    const int content_h = content.h;

    cairo_save(cr);

    // The row's seats, shared with the render player's row (bottom_row_seats):
    // the case on the row's three-px air (WordPad's, kIconRowAirPx), the icon
    // row's own arithmetic on the icon row's own height (render.h's icon-row
    // block), and the two pads.
    const BottomRowSeats seats = bottom_row_seats(content);
    const int btn_w     = seats.case_w;
    const int btn_h     = seats.case_h;
    const int glyph_px  = icon_glyph_px();
    const int group_gap = icon_group_space_px();
    const int btn_y     = seats.case_y;

    // One button, the icon row's face logic verbatim (paint_button_box's
    // toolbar family, its three faces). THE CHECKED FACE'S SUBJECT ON THIS
    // ROW IS ADD TO SELECTION (2026-08-18), the sticky-ctrl mode that closes
    // the marker-verb group: it wears the checked face while the mode stands,
    // which is the roster's standing rule for a mode. A checked button a mode
    // greys keeps its checked face and engraves its glyph, the icon row's own
    // composition.
    const auto paint_button = [&](const TransportRowDef& def, int x) {
        AppState::RedesignButtonFace& face = publish_button_face(
            cr, app, audio, playback, target_render,
            def.id,
            GuiRect{x, btn_y, btn_w, btn_h});

        const bool pressed =
            face.enabled && redesign_button_pressed_face(app, def.id);
        const ButtonBoxFace box = paint_button_box(
            cr, GuiRect{x, btn_y, btn_w, btn_h}, face.selected, pressed,
            ButtonFamily::Toolbar);
        // THE GLYPH IS THE STATE RESOLVER'S since 2026-08-15, as the icon
        // row's already was: this row hosts a stateful face now — the
        // collapsed PLAY/STOP button, which wears media-playback-stop while an
        // audition runs — so redesign_button_icon has a fourth subject and
        // this site can no longer take the table icon directly. It goes
        // through the resolver for EVERY button, the icon row's own rule: the
        // table icon is what the resolver returns when a button has no
        // override, so there is no membership to keep in step here.
        // (THE ROW'S SUPERSEDED CLAIM WAS "no button on this row has a
        // stateful face... the play/stop pair being TWO buttons over one chord
        // rather than one button with two faces", which was exactly true until
        // the architect made it one button with two faces.)
        const icons::Icon glyph = redesign_button_icon(app, def.id, def.icon);
        // The case's corner, not a (3, 3) glyph origin (icons.h's PLACEMENT).
        if (face.enabled)
            icons::draw_cased(cr, glyph, x, btn_y,
                              static_cast<double>(glyph_px), box.shift);
        else
            icons::draw_cased_disabled(cr, glyph, x, btn_y,
                                       static_cast<double>(glyph_px),
                                       box.shift,
                                       static_cast<double>(relief_line_px()));
    };

    // THE ROW, LEFT TO RIGHT (architect 2026-09-29, the right-handed tablet's
    // layout: the most-used buttons at the bottom right): the CLOCK at the
    // lane's left pad in its time field with THE STATE LINE on the ground
    // behind it (since 2026-10-03; the block at the clock below) — then THE
    // RIGHT BLOCK,
    // anchored at the right margin — the MARKER-VERB GROUP (its six counted
    // off kMarkerVerbGroup), the group gap, the MARKER-WALK GROUP (Previous
    // Marker, Next Marker, Center and Switch Tab, kTransportWalkGroup), the
    // gap, the four CARDINAL ARROWS (↓ ↑ ← →, kTransportArrowGroup), the gap
    // and the TRANSPORT THREE
    // (kTransportGroup), whose last button's right edge is one pad in from
    // the lane's right edge. The whole block is measured first and laid left
    // to right from there, so one expression owns the anchor and no group
    // re-derives it. It has no mode term: every button on this row publishes
    // a real rect on every frame, except under a modal, where the row yields
    // whole (paint_bottom_strip).
    //
    // THE NUMBERS, re-derived whenever a group gains or loses a box (each case
    // moves the block by 23 Windows px): the cases touch within a group and
    // eight px stand between two groups, so the block is 6 verbs + 4 walk +
    // 4 arrows + 3 transport = 17 cases and three gaps, 17·23 + 3·8 = 415
    // Windows px (2026-10-02, the Windows case; the verb group six since
    // 2026-09-29 evening). In device px, off the painted walk: 17·32 + 3·11 =
    // 577 at the laptop's 138 %, starting at 1920 − 11 − 577 = 1332, and the
    // clock's time field (kTimeFieldHeightPx, 2026-10-05) — its cell the
    // widest `A | 00:00.000` in the body face, Nimbus's ~97.6 px at 138 %
    // (2026-10-06, its 13 glyphs tracked, ceiled to 98) — spanning 7..113,
    // the state line from one field pad past it (2026-10-06), 117, clipped
    // one group space short of the block at 1321 (~1204 px); at the tablet's
    // 275 % 17·63 + 3·22 = 1137, starting at 2304 − 22 − 1137 = 1145, the
    // field (its cell ~194.6, ceiled to 195) spanning 14..225, the state line
    // 233..1123 (~890 device px). THE ROW CARRIES NO
    // COLLISION RULE — none of the redesign does — and the crop-at-the-floor
    // allowance recorded at kMinWindowWidthPx covers a narrow window or a
    // scale driven toward the 1000 ceiling: the block reaches the field's
    // right edge once the window falls below about 505 Windows px
    // (8 − 3 + ~71 + 6 + 415 + 8). THE STATE LINE CANNOT PUSH ANYTHING: it is clipped one
    // group space short of the block, so a long line is cut rather than
    // colliding.
    int right_block_x = seats.right_x;
    {
        // Each group's count read off its own table, so a box joining or
        // leaving moves this width with no second edit.
        const int verbs_n  = static_cast<int>(std::size(kMarkerVerbGroup));
        const int walk_n   = static_cast<int>(std::size(kTransportWalkGroup));
        const int arrows_n = static_cast<int>(std::size(kTransportArrowGroup));
        const int trans_n  = static_cast<int>(std::size(kTransportGroup));
        const int block_w  =
            (verbs_n + walk_n + arrows_n + trans_n) * btn_w + 3 * group_gap;
        int ax = seats.right_x - block_w;
        // THE STATE LINE'S RIGHT BOUND (below) is this block's own left
        // edge, published out of the scope so the line cannot guess it.
        right_block_x = ax;
        for (const TransportRowDef& def : kMarkerVerbGroup) {
            paint_button(def, ax);
            ax += btn_w;
        }
        ax += group_gap;
        for (const TransportRowDef& def : kTransportWalkGroup) {
            paint_button(def, ax);
            ax += btn_w;
        }
        ax += group_gap;
        for (const TransportRowDef& def : kTransportArrowGroup) {
            paint_button(def, ax);
            ax += btn_w;
        }
        ax += group_gap;
        for (const TransportRowDef& def : kTransportGroup) {
            paint_button(def, ax);
            ax += btn_w;
        }
    }

    // THE CLOCK — the first TIME FIELD (the shape, the face and the
    // fixed-width rule at kTimeFieldHeightPx above, architect 2026-10-05), in
    // the body face named through the one face owner (gui_font.h), inside
    // the save/restore this body already opened. The render
    // player's two fields share its shape, face and metrics; THE TWO NEVER
    // PAINT IN THE SAME FRAME: the player owns the row whole while it stands.
    {
        const GuiFont font = gui_font(GuiFace::Body);
        const TimeFieldMetrics& tm =
            time_field_metrics(font);
        // THE ACTIVE TAB'S LETTER LEADS THE CELL (architect 2026-10-01, the
        // tab row deleted as "a waste of space"): `A | 00:45.115` — the
        // letter, a space, the LITERAL pipe, a space, then the timestamp. THE
        // RESERVED CELL IS THE WIDEST LETTER'S SLOT, THE ` | ` AND THE
        // DIGITS' SPECIMEN, so it is ONE WIDTH ON EVERY TAB AND AT EVERY TIME;
        // the ` | ` and the digits end at the cell's right pad and the letter
        // stands right-aligned against them (the right-alignment rule at
        // kTimeShape's block), so no tab switch moves the pipe or the
        // digits. (The cell reserved a
        // fourteenth monospace cell for the dirty mark `*` glued to the
        // digits until the mark retired, SAVE BEING THE DIRTY MARK —
        // architect 2026-10-05, plain_save_actionable.)
        const std::string sep = " | ";
        const double sep_w = text_shape::shape_text_run(font, sep).width_px;
        const double cell_w = tm.letter_w + sep_w + tm.time_w;
        // THE CELL STARTS AT THE LANE'S LEFT PAD (architect 2026-09-29, the
        // transport having moved to the row's right end). THE AIR IS A MARGIN
        // MIRROR: the row's last button keeps one lane pad from the lane's
        // right edge, and the clock keeps the same pad from its left, so the
        // row's two ends read alike — the rule the retired separator →
        // digits offset carried while the cell sat behind a separator, which
        // at a bare margin is the pad itself. It is also where the modal row's
        // prompt message starts.
        const int cell_x = seats.left_x;
        // THE LEFT OF THE ROW IS ONE FIELD AND A LINE (architect 2026-10-02
        // for the panel, 2026-10-03 for the line, 2026-10-05 for the field's
        // height and face) — the Vegas / ACID pattern: a time in a sunken
        // field, words on the chrome.
        //   THE CLOCK'S FIELD: time_field_rect round the reserved cell,
        //   centred in the content band, painted by paint_time_field. Its
        //   width is fixed (the cell's), so the state line never moves.
        //   THE STATE LINE: on the row's ground, no panel, starting
        //   kStatusPanelPadPx (3 Windows px) right of the field's right line
        //   (architect 2026-10-06, "matching the field"): THE SAME NUMBER
        //   the field's own run stands inside that line, so the time's run
        //   ends and the state's starts the same 3 px from the field's outer
        //   edge, inside and out. It stood one group space (8) out until that
        //   ruling. Its
        //   RIGHT CLIP KEEPS ITS OWN RULE, one group space short of the right
        //   block — that edge faces a button group, not the field; a window
        //   too narrow to leave that span positive paints no state text — the
        //   row's crop-at-the-floor allowance (the block above).
        // THE CLOCK'S PER-TICK DAMAGE BOX (app.clock_cell_rect, below: the
        // cell ± 1 px over the content rows) CROSSES THE FIELD'S TOP AND
        // BOTTOM LINES and the ground above and below them, which a tick
        // repaints identically under its clip; the field's two vertical lines
        // and the whole state line — which starts a field pad right of the
        // field, past the box's one px of slack — fall outside it. Every state change damages the lane whole
        // (Viewport::invalidate_status_cell_area), which repaints the field
        // and the line.
        const GuiRect clock_field =
            time_field_rect(cell_x, cell_w, content_y, content_h);
        paint_time_field(cr, clock_field);
        const int state_x =
            clock_field.x + clock_field.w + scaled_px(kStatusPanelPadPx);
        const int state_w = (right_block_x - group_gap) - state_x;
        // THE BASELINE CENTRES THE FACE'S CAP BAND IN THE FIELD — the
        // period field's 3 px of face above and below its digits, by the one
        // box solver; the field is symmetric about its own lines, so the cap
        // band sits centred inside them too.
        const double baseline =
            redesign_baseline(font, static_cast<double>(clock_field.y),
                              static_cast<double>(clock_field.h));

        // PUBLISH THE CELL FOR THE DAMAGE OWNER (clock_invalidate_rect,
        // app_state.h — the stash contract is at the field). One pixel of slack
        // on each side: the reserved width is an ADVANCE sum and a glyph's ink
        // may sit a hair outside it, and the band is the row's content height,
        // which contains the field and so the baseline's ink by construction.
        // THE RECT FOLLOWS THE CELL BECAUSE IT IS BUILT FROM IT — the origin
        // above is `cell_x` and so is this box's, so the damage can never
        // miss the painted digits. The vertical axis needs no term at
        // all: the box is the row's whole content band, which the field
        // stands inside, and the band's bottom IS the window's, so widening
        // it downward would damage past the surface.
        // CEIL, NOT nearbyint: cell_w is a fractional advance sum and this is
        // a DAMAGE box, which may be a hair too wide but never a hair too
        // narrow — rounding down could leave the cell's last column unerased.
        // Same round-up rule the modal row's buttons and message take on
        // their shaped widths (paint_modal_dialog), for the same reason.
        app.clock_cell_rect = GuiRect{
            cell_x - 1, content_y,
            static_cast<int>(std::ceil(cell_w)) + 2, content_h};

        // THE SPLIT-PLAYHEAD READ, carried over from the status line whole:
        // track the SCANNER during playback (what the user hears), the CURSOR
        // otherwise — the scanner's value is meaningful only while active, so
        // the ternary takes the cursor at rest. The sample rate is the loaded
        // file's and the playhead samples are source frames. There is no
        // paint-site clamp: one owner caps the clock and it is format_timestamp
        // (at 59:59.999 — a longer source truncates, the architect's ruling,
        // recorded there).
        const int64_t ts_sample = app.playhead_scanner_active
            ? app.playhead_scanner_sample
            : app.playhead_cursor_sample;
        const int sr = audio.sample_rate();
        double seconds = 0.0;
        if (sr > 0) {
            seconds = static_cast<double>(ts_sample) /
                      static_cast<double>(sr);
        }
        if (seconds < 0.0) seconds = 0.0;

        // THE FIELD AND THE LINE, THEIR CONTENT AND ITS PRECEDENCE: the field
        // carries the clock's run, the line the STATE — the `h` walk line,
        // else the render / batch / loading progress line. The two state
        // strings CAN COEXIST and THE WALK LINE WINS — nothing STARTS a render
        // inside the `h` view (both render chords are off its allowlist), but
        // a render or a target preview dispatched BEFORE the visit runs on
        // through it, so the mode's line is what the line is for while the
        // view stands and the progress line is back the moment it closes.
        //
        // ONE FACE, TWO SEATS (the line architect 2026-10-03, superseding the
        // second status panel at the small face of 2026-10-02, which had
        // itself superseded the one monospace run of 2026-08-31,
        // `A | 00:00.100 | Updating...`; the clock's face 2026-10-05):
        //   * THE CLOCK — the tab letter and the digits in the body face,
        //     filling the cell (the block above) on the field's own seat. IT
        //     IS NEVER CLIPPED: the row's
        //     crop-at-its-floor allowance (the block above) is what covers the
        //     narrow window where the right block has already walked over this
        //     ground.
        //   * THE STATE — THE BODY FACE (gui_font(GuiFace::Body)) in the
        //     theme's label, painted straight on the row's
        //     ground as the menu row's words are, LEFT-ALIGNED a group space
        //     past the clock's field, on the row's solved baseline over the
        //     content band (redesign_baseline). The
        //     normal face over the small one is the architect's (2026-10-03):
        //     "I don't really read it" at the small face, and a running
        //     render's progress should read. IT IS CLIPPED, NEVER ELLIPSISED —
        //     a cairo rectangle clip, the folder overlay rows' precedent — one
        //     group space short of the right block, so a long line is cut
        //     rather than colliding with the marker verbs. The tablet's span
        //     (~860 device px at 275 %, the numbers above) can be narrower
        //     than the longest walk line, so the composer puts the segment
        //     that must survive the cut ahead of the one that may lose it —
        //     the GitHub word before the scale (history_walk_line, architect
        //     2026-09-29). With no span left nothing is shown.
        //
        // THE CLOCK CARRIES NO DIRTY MARK (architect 2026-10-05): the `*` it
        // carried as its suffix from 2026-09-09 (the window title's asterisk
        // before that) retired, SAVE'S GREY BEING THE MARK on both machines
        // (plain_save_actionable, app_state.h).
        // THE ROW YIELDS WHOLE TO A MODAL,
        // so the field and the line are hidden while a prompt, a dialog editor, the
        // render player or the picker stands
        // (architect-accepted at the fold; the
        // one-day status BAR painted through a modal, that being what a
        // separate lane buys). It is why the render player's
        // load-under-a-running-render refusal says its sentence on a CARD: the
        // explanation the state line would have carried is not on screen
        // under that modal.
        std::string state;
        if (app.history_mode.active) {
            state = history_walk_line(app);
        } else {
            // The process line — the render/batch/queue status and the
            // startup "Loading..." line, empty when there is nothing to
            // report. It is one of the reasons this body runs on every frame
            // class (it is the only feedback on the loading frame).
            state = app.queue_progress_text;
        }
        // THE CLOCK, UNCLIPPED AND RIGHT-ALIGNED — ` | ` and the digits as
        // one run ending at the cell's right pad, then the tab letter ending
        // where that run starts, so the pipe and the digits stand still
        // whatever the letter.
        const text_shape::ShapedRun time_run =
            text_shape::shape_text_run(font, sep + format_timestamp(seconds));
        const text_shape::ShapedRun letter_run = text_shape::shape_text_run(
            font, std::string(1, app.active_tab_view));
        const double run_x =
            static_cast<double>(cell_x + static_cast<int>(std::ceil(cell_w))) -
            time_run.width_px;
        set_palette_source(cr, palette().clock_text);
        text_shape::show_shaped_run(cr, letter_run,
                                    run_x - letter_run.width_px, baseline);
        text_shape::show_shaped_run(cr, time_run, run_x, baseline);
        if (state_w > 0 && !state.empty()) {
            // The same face as the clock's run, on the content band's seat.
            const double state_baseline =
                redesign_baseline(font, static_cast<double>(content_y),
                                  static_cast<double>(content_h));
            cairo_save(cr);
            cairo_rectangle(cr, state_x, lane.y, state_w, lane.h);
            cairo_clip(cr);
            show_row_text(cr, font, static_cast<double>(state_x),
                          state_baseline, state, palette().label);
            cairo_restore(cr);
        }
    }

    cairo_restore(cr);
}

// -- The floating surfaces ---------------------------------------------------

void GuiPaintHandler::paint_popup_chrome(cairo_t* cr, const GuiRect& r,
                                         PopupFace face) {
    // ONE BOX PAINTER FOR EVERY FLOATING SURFACE, TWO FACES (architect
    // 2026-10-02, the Windows-95 chrome), each a square fill then its frame:
    //   MENU — the ground inside the PLAIN RAISED two-line edge: a dropdown
    //          (Windows drew menus as raised panels, EDGE_RAISED);
    //   INFO — THE CARD FACE, Windows 95's tooltip (architect 2026-10-04;
    //          the rule is render.h's palette block): `card_ground` with a
    //          THIN flat line of `card_frame`, ONE Windows px, on the BOTTOM
    //          AND THE RIGHT ONLY (architect 2026-10-06, the one period
    //          capture's) — the bottom row and the right column laid on the
    //          face as two square cell rects, so the frame owns both far
    //          corners whole as the capture's black does (a flat line, no
    //          bevel: the relief's mitre, paint_relief_frame, is not drawn
    //          here) — the tooltip and every notification card, whose words
    //          their painters set in `card_text`.
    if (face == PopupFace::Menu) {
        paint_cell_rect(cr, r, palette().ground);
        paint_relief_plain_raised(cr, r);
    } else {
        const int lw = relief_line_px();
        paint_cell_rect(cr, r, palette().card_ground);
        paint_cell_rect(cr, GuiRect{r.x, r.y + r.h - std::min(lw, r.h), r.w,
                                    std::min(lw, r.h)},
                        palette().card_frame);
        paint_cell_rect(cr, GuiRect{r.x + r.w - std::min(lw, r.w), r.y,
                                    std::min(lw, r.w), r.h},
                        palette().card_frame);
    }
}

void GuiPaintHandler::paint_shift_tooltip(cairo_t* cr) {
    // THE HOVER TOOLTIP, for the box's own button — at most one box. The
    // tooltip's clock (tick_tooltip) owns WHEN it shows and goes; this owns
    // only what it looks like, and publishes the rect it painted so the hide
    // edge can damage it — AS PAINTED (the rule is at AppState::
    // RedesignTooltip): the previous rect is held until the new box is known.
    const GuiRect prev_rect = app.redesign_tooltip.rect;
    app.redesign_tooltip.rect = GuiRect{0, 0, 0, 0};
    if (!app.redesign_tooltip.visible) return;

    // THE BOX'S OWN OWNER IS THE SUBJECT, read rather than re-derived: the
    // clock set it from the wait that ripened (the two hover walks writing
    // that wait, one per surface), and `visible` is only ever set with an
    // owner, so one is always standing here. Re-walking
    // for a hovered button would be a SECOND membership rule to keep in step
    // with that one — and since 2026-08-07 it could not be the same rule
    // anyway: A DISABLED BUTTON SHOWS ITS HINT (the architect's
    // kdenlive-parity ruling), and the owner is the only record of which
    // button the wait ripened on — no hover is stored anywhere else.
    const AppState::RedesignTooltip::Owner owner = app.redesign_tooltip.owner;
    if (owner.index < 0) return;

    // THE TWO SURFACES (the encoding is at the field): a ROSTER owner reads
    // the roster's constant/stateful hint table and the roster's painted rect;
    // a DIALOG owner reads the modal stash the painter itself publishes — its
    // composed hint and its button rect, both written by paint_modal_dialog,
    // which runs BEFORE this body precisely so the rect a flipped box stands
    // above is the one this frame draws.
    const char* line1 = nullptr;
    const char* line2 = nullptr;
    GuiRect     btn{0, 0, 0, 0};
    if (owner.surface == AppState::RedesignTooltip::Surface::Dialog) {
        const AppState::ModalDialogGeometry& dlg = app.modal_dialog;
        if (!dlg.valid ||
            owner.index >= static_cast<int>(dlg.buttons.size())) {
            return;
        }
        // ONLY THE STASH THAT ARMED THE WAIT: an owner stamped by another
        // surface (the load-in-place prompt, closed back onto the render
        // player's row under the pointer) would read a different button at the
        // same index. This frame's stash is already published above, so the
        // refusal lands on the very frame the replacement is drawn; the roster
        // walk's tail hides the owner on the next tick (the rule is at
        // AppState::RedesignTooltip).
        if (owner.dialog_owner != dlg.owner ||
            owner.dialog_session != dlg.session) {
            return;
        }
        const AppState::ModalDialogButton& b =
            dlg.buttons[static_cast<size_t>(owner.index)];
        if (b.tooltip.empty()) return;
        line1 = b.tooltip.c_str();
        // THE MODIFIER LINE, on the buttons that have one (2026-08-28 —
        // the player's two skips): the roster branch's own two-line form,
        // reached here through the published pair instead of a table, since a
        // modal button's words are the painter's to compose.
        if (!b.tooltip2.empty()) line2 = b.tooltip2.c_str();
        btn   = b.rect;
    } else {
        if (owner.index >= kRedesignButtonCount) return;
        const RedesignButton id = static_cast<RedesignButton>(owner.index);
        // THE STATEFUL OVERLOAD, with the three objects its forks read
        // (2026-09-01): the words fork on what the acts and the faces read,
        // and this painter is the overload's one reader. It took the enabled
        // predicate's whole five for the morning of that day, `playback` and
        // `target_render` unread at the far end; they left with the rule that
        // a parameter arrives with its producer.
        const RedesignTooltipText text = redesign_button_tooltip(
            app, audio, audio.total_frames(), id);
        // A HINT WITH NO LINE 1 IS NO HINT. Nothing can take a hint away under
        // a standing box any more — tooltip MEMBERSHIP is the menu row and
        // nothing else, in every state (the rule is at redesign_button_tooltip,
        // app_state.h), and the wait's writer only ever takes a button whose
        // line 1 is non-null. So this reads as the table's total answer rather
        // than as a state guard, and it keeps the painter honest against the
        // owner without knowing which arms are null.
        if (text.line1 == nullptr) return;
        line1 = text.line1;
        line2 = text.line2;
        btn   = app.redesign_buttons[owner.index].rect;
    }
    if (btn.w <= 0 || btn.h <= 0) return;

    cairo_save(cr);
    const GuiFont font = gui_font(GuiFace::Body);
    const bool two_line = (line2 != nullptr);
    const text_shape::ShapedRun r1 = text_shape::shape_text_run(font, line1);
    const double band1 = gui_font_line_px(font);

    double band2 = 0.0, w2 = 0.0;
    text_shape::ShapedRun r2;
    if (two_line) {
        r2 = text_shape::shape_text_run(font, line2);
        band2 = gui_font_line_px(font);
        w2 = r2.width_px;
    }

    const int pad_x = scaled_px(kTooltipPadXPx);
    const int pad_y = scaled_px(kTooltipPadYPx);
    const int gap   = two_line ? scaled_px(kTooltipLineGapPx) : 0;
    const int w = static_cast<int>(std::nearbyint(std::max(r1.width_px, w2))) +
                  2 * pad_x;
    // THE SYMMETRIC BOX: the same pad above the first band and below the last,
    // with a real gap between them. Nothing is authored but the two paddings and
    // the gap; the bands are the face's own.
    const int h = static_cast<int>(std::nearbyint(band1 + band2)) + gap +
                  2 * pad_y;

    // UNDER THE POINTER, WINDOWS 95's SEAT (architect 2026-10-06): the left
    // edge at the pointer's x and the top 18 Windows px below it, the pointer
    // where it stood at the show; ABOVE THE OWNER'S BUTTON where that would
    // cross the window's foot — every bottom-row owner, the roster's and the
    // modal's alike, the lane resting on the foot — and shifted left at the
    // right edge, the box never shrinking (a truncated hint would be worse
    // than one that shifted). The rule's one statement is tooltip_box_rect
    // (app_state.h); the measurement is at render.h's kTooltipPointerDropPx.
    const GuiRect box = tooltip_box_rect(app, btn, w, h);
    const int x = box.x;
    const int y = box.y;
    // PUBLISHED AS PAINTED: a clip that covers the box draws it whole, so the
    // rect and the words are exactly this frame's; a clip that does not (the
    // player's clock and scrub cells at scanner cadence, which recompose a
    // modal button's words on the stash above without reaching the box)
    // leaves the old box's pixels where they stood, so the rect grows to the
    // union of the two and the words stay the last ones really drawn —
    // main.cpp's comparator sees that drift and damages the box whole.
    if (clip_covers_drawable(cr, app, box)) {
        app.redesign_tooltip.rect          = box;
        app.redesign_tooltip.painted_line1 = line1;
        app.redesign_tooltip.painted_line2 = line2 != nullptr ? line2 : "";
    } else {
        app.redesign_tooltip.rect =
            (prev_rect.w > 0 && prev_rect.h > 0) ? union_rect(prev_rect, box)
                                                 : box;
    }

    paint_popup_chrome(cr, box, PopupFace::Info);

    // EACH LINE SITS ON ITS OWN BAND, and a band IS the face's ascent plus
    // its descent — a LINE and not a box, so each baseline is
    // line_baseline's, its own band's ascent. The box solver is not asked
    // here: there are no margins around a band to centre a cap in, and the
    // two lines' bands are laid end to end with the authored gap between
    // them, which is what makes the symmetry above true of the ink and not
    // merely of the arithmetic.
    set_palette_source(cr, palette().card_text);
    text_shape::show_shaped_run(
        cr, r1, static_cast<double>(x + pad_x),
        line_baseline(font, static_cast<double>(y + pad_y)));
    if (two_line) {
        // The hint line takes THE CARD TEXT like the first (architect
        // 2026-10-03, Windows' ink, no dims; the card face's words are
        // `card_text` since 2026-10-04, render.h's palette block).
        text_shape::show_shaped_run(
            cr, r2, static_cast<double>(x + pad_x),
            line_baseline(font,
                          static_cast<double>(y + pad_y) + band1 + gap));
    }

    cairo_restore(cr);
}

namespace {

// THE CARD'S LINE SPACING: the body face's OWN cell (its recorded ascent
// plus descent, 13 Windows px) at the live scale, so a wrapped card's rows
// sit exactly as the face intends them to. Read ONCE per paint and handed
// down — it is the only thing that turns a line count into a card height.
int notification_line_h_px(const GuiFont& font) {
    return static_cast<int>(std::nearbyint(gui_font_line_px(font)));
}

// THE WRAP, and the ONE place a card's text is broken into lines (architect
// 2026-08-30). GREEDY WORD WRAP AT ASCII SPACES ON MEASURED ADVANCES: each
// candidate line is re-shaped as ONE run through the shaping chokepoint and
// its own width_px is what decides, so THE RUN THAT IS MEASURED IS THE RUN
// THAT IS PAINTED — the painter paints exactly the runs this returns and
// re-shapes nothing. A line closes before the first word that overflows the
// room.
//
// THREE CASES BESIDES THE ORDINARY ONE:
//   - the whole sentence FITS the room (or the room is gone in a contrived
//     window): one line, and it is the run the caller already shaped to size
//     the card — no second shaping pass for the common case;
//   - A SINGLE WORD WIDER THAN THE ROOM stands alone on its line and clips at
//     the run's right edge under the caller's cairo clip — there is nowhere
//     to break it, and no hyphenation exists in the product;
//   - THE LAST PERMITTED LINE (kNotificationMaxLines) TAKES THE WHOLE
//     REMAINDER as one run and clips the same way, so no word is ever
//     silently dropped — only cut where the eye can see the cut.
//
// The break's space is CONSUMED, and so is any run of leading spaces on a
// line; every other byte is carried verbatim out of the sentence, which is
// why the lines are substrings and never a rejoin (a rejoin would author
// text the sentence never had). Shaping runs at paint on every frame, as the
// folder overlay's rows already do: cards are at most a handful and their
// sentences are short, so nothing here is memoized.
std::vector<text_shape::ShapedRun> notification_text_lines(
        const GuiFont& font, const std::string& text,
        const text_shape::ShapedRun& whole, double room_px) {
    std::vector<text_shape::ShapedRun> lines;
    if (room_px <= 0.0 || whole.width_px <= room_px) {
        lines.push_back(whole);
        return lines;
    }
    const std::string_view all(text);
    const size_t n = all.size();
    size_t pos = 0;
    while (pos < n) {
        while (pos < n && all[pos] == ' ') ++pos;
        if (pos >= n) break;
        if (lines.size() + 1 >= static_cast<size_t>(kNotificationMaxLines)) {
            lines.push_back(
                text_shape::shape_text_run(font, all.substr(pos)));
            return lines;
        }
        size_t end = n;
        text_shape::ShapedRun line;
        bool fitted = false;
        for (size_t probe = pos;;) {
            const size_t sp = all.find(' ', probe);
            const size_t cand_end = (sp == std::string_view::npos) ? n : sp;
            text_shape::ShapedRun cand = text_shape::shape_text_run(
                font, all.substr(pos, cand_end - pos));
            if (cand.width_px > room_px) break;
            line   = std::move(cand);
            end    = cand_end;
            fitted = true;
            if (cand_end >= n) break;
            probe = cand_end + 1;
        }
        if (!fitted) {
            const size_t sp = all.find(' ', pos);
            end  = (sp == std::string_view::npos) ? n : sp;
            line = text_shape::shape_text_run(font, all.substr(pos, end - pos));
        }
        lines.push_back(std::move(line));
        pos = end;
    }
    // A sentence of nothing but spaces breaks nowhere; it is its own run.
    if (lines.empty()) lines.push_back(whole);
    return lines;
}

} // namespace

// -- GuiPaintHandler::paint_notifications ---------------------------------
//
// THE NOTIFICATION CARDS (architect design 2026-08-29; the model, the
// classes, the hit rule and the inventory at notifications.h). THE WHOLE
// STACK — AppState::Notifications::cards, newest first, every one of them on
// screen since the queue retired 2026-08-30 — painted top-right, right-aligned
// at kPanelPadPx from the window's edge, its first card filling the icon
// row's toolbar band between the two etched pairs (the margins at
// notification_stack_bound), growing
// DOWN over whatever lies there (the icon row's empty right, the thin lanes,
// the waveform), the cards kNotificationGapPx apart (the card's own 1 px
// since 2026-10-01, the icon row's 2 before — the ruling at the constant).
//
// THE LOOK (architect 2026-10-02, the Windows-95 chrome; its colours
// 2026-10-04): THE CARD FACE — Windows 95's tooltip, `card_ground` with a
// thin `card_frame` line on its bottom and right, its words `card_text`
// (render.h's palette block) —
// square, NO DROP SHADOW, through the one popup box painter
// (paint_popup_chrome's Info face, the tooltip's own);
// a row of the icon row's own height, holding — left to right, EVERY
// DISTANCE THE CARD'S ONE PAD (notification_pad_px, the ruling at its
// declaration: the box's own vertical margin, read for all five) — a square
// box the toolbar case's height with the CLASS GLYPH centred at the box's
// own inset (DialogInformation for a normal card, the balloon with its blue
// i, DialogError for a critical one, the red disc with its white X — each
// in its drawing's own colours, nothing coloured here), that pad, THE
// SENTENCE
// of the one sans in the info text, and that pad PLUS THE GLYPH'S INSET to
// the right edge (architect 2026-10-01: the air the eye sees left of the
// text, from the glyph's ink, repeated after it — the ruling at
// notification_pad_px) — the box sitting that same pad below the card's top
// and above its foot.
// (A window-close X stood in a second button box at the right from
// 2026-08-29, wearing the icon button's hover face; it retired 2026-10-01
// when the whole card became the button — "whole card dismisses, X gone" —
// and the card wears no hover face and no pressed face.)
// The card's width
// is its content's, clamped to [kNotificationMinWidthPx,
// notification_card_max_w_px] — the authored ceiling since 2026-08-31, scaled
// like every other length, with the window itself as a safety under it.
//
// THE TEXT GROWS DOWNWARD UNDER THE GLYPH (architect 2026-08-30). The
// width rule is unchanged — the WHOLE sentence's shaped width sets it, inside
// the same clamp — so a sentence that fits its room is one line exactly as it
// was; a longer one wraps through notification_text_lines above to at most
// kNotificationMaxLines, the last of which clips at the run's right edge (no
// ellipsis — the folder overlay rows' precedent). THE CARD GROWS, NOTHING
// REFLOWS: the glyph box keeps the one-line card's placement,
// the first line keeps the one-line card's baseline, each further line sits
// one face line-height lower, and the air under the last line is the first
// line's own by construction — the one pad still rules every edge. The
// stack's `y` then advances by each card's OWN height.
//
// IT PUBLISHES WHAT IT DREW: each card's rect, and their union,
// into AppState::Notifications (the owner-tag doctrine at
// ModalDialogGeometry — published geometry may only SELECT; the press claim,
// the cursor map and the hover walk read this and then ask the live stack).
// THE CARDS ARE CLIPPED TO THE ROOM and PUBLISHED CLIPPED TO IT: a stack tall
// enough to reach the bottom row is the contrived case nothing caters for,
// and clipping is what keeps it from painting over that row or claiming a
// press under it. It runs on every frame, cards or none, for the floating
// surfaces' reason: a skipped run would strand a stale publication. Its
// damage is not its own — every stack change damages the room through the
// viewport owner, and the hover paints nothing to damage.
void GuiPaintHandler::paint_notifications(cairo_t* cr) {
    AppState::Notifications& st = app.notifications;
    st.painted.clear();
    st.painted_rect = GuiRect{0, 0, 0, 0};
    if (st.cards.empty()) return;

    // THE ROOM the stack grows into, and the clip for every card below. A
    // window with none of it paints no card at all (notifications.h).
    const GuiRect room = notification_stack_bound(app);
    if (room.w <= 0 || room.h <= 0) return;

    cairo_save(cr);
    const GuiFont font = gui_font(GuiFace::Body);

    const int line1_h  = notification_card_h_px();
    const int line_h   = notification_line_h_px(font);
    const int max_w    = notification_card_max_w_px(app);
    const int min_w    = scaled_px(kNotificationMinWidthPx);
    const int gap      = scaled_px(kNotificationGapPx, 1);
    // THE CARD'S GLYPH BOX IS THE TOOLBAR CASE'S HEIGHT, SQUARE, its glyph
    // the case's 16 Windows px (render.h's icon-row block; the 32-laptop-px
    // square before 2026-10-02).
    const int btn      = icon_case_h_px();
    const int glyph_px = icon_glyph_px();
    // THE GLYPH INSET IS AN INTEGER HALF AND SITS ONE PIXEL LEFT OF CENTRE
    // WHERE THE DIFFERENCE IS ODD (recorded 2026-09-02): both terms are
    // already scaled, so `btn - glyph_px` is even at the tablet's 275 % and
    // the laptop's 138 % (16 and 8) and odd at some others, where this
    // truncating division loses the half pixel to the left and the top.
    const int inset    = (btn - glyph_px) / 2;
    // ONE NUMBER FOR ALL FIVE DISTANCES (architect 2026-08-30): the box's
    // vertical margin is the card's every pad, so the horizontal placement
    // cannot disagree with the vertical centering it is taken from — the
    // text's right air adding the inset above (2026-10-01, below).
    const int pad      = notification_pad_px();
    const int right_x  = room.x + room.w;
    // The chrome every card carries besides its text: THREE PADS, THE
    // GLYPH'S INSET AND ONE BOX (2026-10-01) — the glyph's box with a pad on
    // each side of it (the left edge's and the one between it and the text),
    // and at the text's right, the card's right edge, one pad PLUS THE INSET.
    // THE INSET IS THE TEXT'S RIGHT AIR MATCHING ITS LEFT AS THE EYE SEES IT
    // (architect 2026-10-01, the ruling at notification_pad_px): the box is
    // invisible and its glyph's ink stands `inset` inside it, so the text's
    // left air reads as pad + inset, and the right pad repeats that. (The X's
    // box and its pad left the sum the same day, with the X.)
    const int right_pad = pad + inset;
    const int chrome_w  = 2 * pad + btn + right_pad;
    // WHAT IS PUBLISHED IS WHAT IS VISIBLE: the room clips the paint, so it
    // clips the publication too, or the hit would claim a press on pixels
    // the bottom row owns. An empty answer is hit by nothing (rect_contains).
    auto in_room = [&room](GuiRect r) {
        const int x0 = std::max(r.x, room.x);
        const int y0 = std::max(r.y, room.y);
        const int x1 = std::min(r.x + r.w, room.x + room.w);
        const int y1 = std::min(r.y + r.h, room.y + room.h);
        return GuiRect{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
    };

    // THE LAYOUT PASS: every card's geometry and lines, then the card pass
    // below paints them.
    struct LaidCard {
        const AppState::Notification*        n = nullptr;
        GuiRect                              card{0, 0, 0, 0};
        int                                  glyph_x   = 0;
        int                                  text_x    = 0;
        int                                  text_room = 0;
        std::vector<text_shape::ShapedRun>   lines;
    };
    std::vector<LaidCard> laid;
    laid.reserve(st.cards.size());
    int y = room.y;
    // THE WALK IS THE WHOLE STACK (2026-08-30, the queue's retirement): what
    // ends it is the ROOM, not a count. The bump keeps the stack inside the
    // room's capacity in one-line cards, and the two overflows that count
    // cannot see — wrapped cards, and criticals the bump will not touch —
    // paint on down and are cut here.
    for (size_t i = 0; i < st.cards.size(); ++i) {
        // Wholly under the room's foot: so is every card after it.
        if (y >= room.y + room.h) break;
        const AppState::Notification& n = st.cards[i];
        // THE WIDTH IS THE WHOLE SENTENCE'S, clamped — the rule the wrap did
        // not change — and the ROOM the wrap breaks against is what that
        // width leaves between the glyph's box and the right pad.
        //
        // IT CEILS (architect 2026-08-31): a box that must CONTAIN a run is
        // not a point on a grid, so banker's rounding is the wrong class for
        // it — the cells-not-points rule reserves nearbyint for positions,
        // and a width that rounded DOWN left the sentence overflowing its own
        // card by up to half a pixel, which the wrap below then answered by
        // breaking the last word onto a second line ("There is nothing to
        // redo" wrapped while the WIDER "…to undo" did not). Ceiling makes
        // text_room ≥ the shaped width by construction, so the wrap's own
        // fits-whole test is exact.
        const text_shape::ShapedRun whole =
            text_shape::shape_text_run(font, n.text);
        int w = chrome_w + static_cast<int>(std::ceil(whole.width_px));
        // max_w never answers under min_w (the owner floors it there, so the
        // room this paints into cannot be narrower than the card), and the
        // std::max keeps the clamp's own precondition stated at the call.
        w = std::clamp(w, min_w, std::max(min_w, max_w));
        const int card_x    = right_x - w;
        const int glyph_x   = card_x + pad;
        const int text_x    = glyph_x + btn + pad;
        const int text_room = card_x + w - right_pad - text_x;
        std::vector<text_shape::ShapedRun> lines =
            notification_text_lines(font, n.text, whole,
                                    static_cast<double>(text_room));
        // THE CARD GROWS DOWNWARD BY WHOLE LINES; its first line is a
        // one-line card and the glyph's box and the first baseline sit in it.
        const GuiRect card{
            card_x, y, w,
            line1_h + (static_cast<int>(lines.size()) - 1) * line_h};
        laid.push_back(LaidCard{&n, card, glyph_x, text_x, text_room,
                                std::move(lines)});
        y += card.h + gap;
    }

    // THE CARD PASS, clipped to the room.
    cairo_rectangle(cr, room.x, room.y, room.w, room.h);
    cairo_clip(cr);
    for (const LaidCard& c : laid) {
        const AppState::Notification& n = *c.n;
        const GuiRect& card = c.card;
        const int glyph_x   = c.glyph_x;
        const int text_x    = c.text_x;
        const int text_room = c.text_room;
        const std::vector<text_shape::ShapedRun>& lines = c.lines;
        paint_popup_chrome(cr, card, PopupFace::Info);

        const int box_y = card.y + pad;
        // The card's class glyph, no case, at the card's own placement.
        icons::draw(cr,
                    n.cls == AppState::NotificationClass::Critical
                        ? icons::Icon::DialogError
                        : icons::Icon::DialogInformation,
                    static_cast<double>(glyph_x + inset),
                    static_cast<double>(box_y + inset),
                    static_cast<double>(glyph_px));

        if (text_room > 0) {
            cairo_save(cr);
            cairo_rectangle(cr, text_x, card.y, text_room, card.h);
            cairo_clip(cr);
            set_palette_source(cr, palette().card_text);
            // The FIRST line's baseline is the one-line card's, solved over
            // the first line's own band; each further line is one face
            // line-height lower.
            const double base =
                redesign_baseline(font, static_cast<double>(card.y),
                                  static_cast<double>(line1_h));
            for (size_t li = 0; li < lines.size(); ++li) {
                text_shape::show_shaped_run(
                    cr, lines[li], static_cast<double>(text_x),
                    base + static_cast<double>(li) *
                               static_cast<double>(line_h));
            }
            cairo_restore(cr);
        }

        const GuiRect shown = in_room(card);
        st.painted.push_back(AppState::NotificationPainted{n.id, shown});
        st.painted_rect = st.painted.size() == 1
                              ? shown : union_rect(st.painted_rect, shown);
    }
    cairo_restore(cr);
}

void GuiPaintHandler::paint_dropdown(cairo_t* cr) {
    // THE MENU ROW'S DROPDOWN — ONE painter for EVERY menu, hanging flush under
    // the button that emits it at ZERO margin: its top edge is the BUTTON's
    // bottom edge, which since the 2026-09-09 relayout IS the menu lane's
    // bottom and the ICON ROW's first pixel — the anchors fill the lane now,
    // so there is no margin strip between them (the ruling, and the margin
    // that held those two edges one pixel apart 2026-08-02..09-09, are at the
    // anchor arithmetic below), under that button's left edge. Publishes its own
    // rect and every item rect, so the press claim hit-tests exactly what was
    // painted and never re-shapes a label.
    //
    // ITS CHROME (architect 2026-10-02, the Windows-95 popup menu): the ONE
    // ground inside the PLAIN RAISED two-line frame, square
    // (paint_popup_chrome), one Windows px of ground margin inside it, its
    // separators ETCHED; the highlighted row a FLAT FILL in the theme's
    // selected pair (render.h's palette block).
    //
    // NO ICONS, NO CHECKBOXES, NO SUBMENU ARROWS, by ruling — the crops reserve
    // all three columns and this product has none of them, exactly as the tabs
    // dropped theirs. What the columns' SPACE becomes is the labels' left INDENT
    // and the accelerator's right margin.
    //
    // THE CHECKBOX HALF OF THAT RULING WAS TESTED ON 2026-08-27 AND HELD, and
    // it has no producer at all again since 2026-09-04: the ITERATIONS menu's
    // "Grid Iterations" row toggled a MODE, which is the only item this
    // product has ever had that could have worn one, and it did not — the
    // label was the constant act's name and the mode said what it was doing on
    // the screen itself (every warp flag grows its iteration bracket, and the
    // Render button's hint reads "Render Grid Iterations"). A checkbox column
    // would have existed
    // for one row in one menu, and the roster's own no-blink argument answered
    // it: a badge would restate what the picture already shows. That row is a
    // ROSTER BUTTON now and the mode wears the icon row's own LAMP, which is
    // the state cue a menu row never had. The full record is at the deleted
    // table's own note (app_state.h).
    //
    // THE MENUS DIFFER IN EXACTLY ONE PLACE since 2026-08-03: the
    // accelerator COLUMN, which the two COMMAND menus (File, and Edit since
    // 2026-08-20; Help had it 2026-09-03..09) have and Settings does not. The
    // width FOLLOWS from it — one expression with an optional term — rather
    // than being a second difference of its own, and everything else (chrome,
    // item height, insets, separator, faces, baseline, and now the label
    // indent and right margin) is one set of numbers by construction.
    //
    // (THE PER-ITEM DISABLED STATE, 2026-08-08 to 2026-08-15, was not a second
    // difference either, for the same reason the accelerator column is not two
    // rules: this painter asked one predicate per row, dropdown_item_enabled,
    // and the MENUS were not named in it. The Navigation menu's "Walk both
    // tabs" row inside the `h` history view was its ONE producer for its whole
    // life, and it went producer-less with that menu; this painter's own
    // disabled arms — the two dim inks and the face suppression — went with it.
    // THEY RETURNED 2026-09-24 with the truthful menus, below, the predicate
    // under its old name and asked of every row of every menu. Geometry is
    // untouched on any menu, a greyed row still occupying its slot at its full
    // height, which is why nothing about the layout below moves with the grey.
    // The record is at kFilePopupItems, app_state.h.)
    app.dropdown.rect = GuiRect{0, 0, 0, 0};
    app.dropdown.item_rects = {};
    if (!app.dropdown.open()) return;

    const DropdownMenu menu = app.dropdown.menu;
    const int count = dropdown_item_count(menu);
    const GuiRect& btn =
        app.redesign_buttons[redesign_button_index(
            dropdown_anchor_button(menu))].rect;
    if (btn.w <= 0 || btn.h <= 0) return;

    cairo_save(cr);
    const GuiFont font = gui_font(GuiFace::Body);

    const int border    = popup_border_px();
    const int item_h    = popup_item_h_px();
    const int block_mar = popup_item_margin_y_px();
    const int inset     = scaled_px(kPopupItemInsetPx, 1);
    const int sep_inset = scaled_px(kPopupSepInsetPx);
    const int sep_mar   = popup_sep_margin_y_px();
    const int sep_block = popup_sep_block_px();   // margin, etched pair, margin

    // WIDTH FROM THE WIDEST SHAPED RUN(S) behind the authored minimum — every
    // run is shaped once here and reused for the paint below, so the box and the
    // glyphs come from the same measurements (the displayed-basis doctrine).
    text_shape::ShapedRun runs[kDropdownMaxItemCount];
    text_shape::ShapedRun hot_runs[kDropdownMaxItemCount];
    double widest = 0.0, widest_hot = 0.0;
    bool has_hotkeys = false;
    for (int i = 0; i < count; ++i) {
        const DropdownRow row = dropdown_row(menu, i);
        runs[i] = text_shape::shape_text_run(font, row.label);
        widest = std::max(widest, runs[i].width_px);
        if (row.hotkey != nullptr) {
            has_hotkeys = true;
            hot_runs[i] = text_shape::shape_text_run(font, row.hotkey);
            widest_hot = std::max(widest_hot, hot_runs[i].width_px);
        }
    }
    // ONE WIDTH RULE WITH AN OPTIONAL COLUMN, authored on the POPUP box: the
    // label pad, the widest label, then — only where an accelerator column
    // exists — the guaranteed column gap and the widest accelerator, then the
    // right pad (the same number, the mirror rule), less the chrome the item
    // box adds back below.
    //
    // THE OPTIONAL TERM IS DRIVEN OFF THE ITEM TABLE, not off the menu
    // enumerator: an accelerator column is a property of the rows (a menu whose
    // rows carry no hotkey has none), and expressing it that way is what leaves
    // the menus differing by one DERIVED term instead of by hand-written
    // rules per menu — which is exactly what let the FILE menu land in 2026-08-13
    // with an accelerator column and no edit in this body at all.
    //
    // The authored minimum applies to both — it is the item box's floor and the
    // reason a menu of short labels still reads as a menu. Whether it or a
    // menu's own content wins is the expression's answer at every paint, the
    // floor and the shaped run simply trading which one wins. (The deleted NAVIGATION
    // menu's long labels never reached the floor either, which is how the two
    // were shown to compose.)
    const int pad_l    = scaled_px(kPopupPadXPx);
    const int gap      = scaled_px(kPopupHotkeyGapPx);
    const int pad_r    = pad_l;
    const int chrome_w = 2 * inset + 2 * border;
    const int content_w =
        pad_l + static_cast<int>(std::nearbyint(widest)) +
        (has_hotkeys ? gap + static_cast<int>(std::nearbyint(widest_hot)) : 0) +
        pad_r - chrome_w;
    const int item_w = std::max(scaled_px(kPopupItemMinWidthPx), content_w);
    const int w = item_w + chrome_w;
    // THE HEIGHT COMES FROM THE SHARED SUM, not a second walk here: the open
    // edge damages dropdown_h_px() before this ever runs, so the two must be one
    // expression.
    const int h = dropdown_h_px(menu);

    // FLUSH WITH THE BUTTON IT EMITS FROM ON X, AND WITH THE LANE ON Y.
    // The x is the anchor's own left edge (architect 2026-08-02, and the
    // anchors are flush with the lane's left edge anyway). THE Y IS THE MENU
    // LANE'S FOOT, which since the 2026-09-09 relayout IS the ICON ROW'S
    // FIRST PIXEL, so the box hangs straight onto the toolbar with nothing
    // between — and the anchor's pill IS the lane (render.h's
    // kMenuRowHeightPx), so the pill's foot is that row too and the dropdown
    // touches the first row as it does in kdenlive. It is read from
    // top_menu_row_area rather than from btn.y + btn.h because the LANE is
    // the owner of that row: for the hours the lane stood at 34 the pill
    // was 4 authored rows short of it, and a dropdown hung from the pill
    // floated over the row's own ground — the very thing the architect saw
    // and ruled against. From 2026-08-02 to 2026-09-09 the two differed by
    // the lane's 1px margin-bottom, where he ruled the BUTTON: the box
    // covered that margin strip while the menu was up, the ruled look and
    // not a leak. What he stated then is what this reads now — "the menu
    // row's bottom edge", the whole lane's — with the anchor keeping the x.
    const GuiRect menu_lane = top_menu_row_area(app);
    int x = btn.x;
    int y = menu_lane.y + menu_lane.h;   // flush: zero margin under the LANE
    if (x + w > app.width) x = app.width - w;
    if (x < 0) x = 0;
    app.dropdown.rect = GuiRect{x, y, w, h};

    paint_popup_chrome(cr, app.dropdown.rect, PopupFace::Menu);

    // THE ITEMS' ENABLED VERDICTS (architect 2026-09-24, the truthful menus),
    // asked once per paint of the one owner (dropdown_item_enabled); the
    // input side claims on the stash this publishes, never on the owner
    // (strictly as-painted). The as-painted stash is republished only when
    // the clip covers the box's DRAWABLE part — publish_button_face's rule,
    // one owner (clip_covers_drawable) — so a narrow damage in the frame a
    // verdict flips cannot record a face these pixels never took, the
    // per-tick comparator (main.cpp) repairs it, and a box running past the
    // window's foot still publishes on the repair's own frame.
    bool enabled[kDropdownMaxItemCount] = {};
    for (int i = 0; i < count; ++i)
        enabled[i] = dropdown_item_enabled(app, audio, menu, i);
    if (clip_covers_drawable(cr, app, app.dropdown.rect)) {
        for (int i = 0; i < count; ++i)
            app.dropdown.item_enabled[static_cast<size_t>(i)] = enabled[i];
    }

    // The item block opens BELOW the frame by its own one-px margin, and
    // closes with the same margin above the bottom frame.
    int iy = y + border + block_mar;
    for (int i = 0; i < count; ++i) {
        const DropdownRow row = dropdown_row(menu, i);
        if (row.separator_before) {
            // THE SEPARATOR IS ETCHED (architect 2026-10-02, Windows' menu
            // separator), inset horizontally: its Shadow line and its Hilight
            // line under it, with the block's own vertical margin above and
            // below the pair (popup_sep_block_px, dropdown_h_px's sum).
            paint_relief_etched_hline(cr, x + sep_inset, iy + sep_mar,
                                      w - 2 * sep_inset);
            iy += sep_block;
        }
        // ITEMS TOUCH — zero vertical gap between adjacent ones — and each
        // one's box insets horizontally from the frame by the one-px margin —
        // inside the frame's two lines, then the margin, on both sides (the
        // width chrome_w budgets is 2·inset + 2·border). The published rect is
        // that box, so the clickable area is exactly the area that lights; the
        // press claim reads it (dropdown.item_rects), and the label and the
        // accelerator are placed from the popup's own edges, not from it.
        const GuiRect item{x + border + inset, iy, item_w, item_h};
        app.dropdown.item_rects[static_cast<size_t>(i)] = item;

        // THE HIGHLIGHTED ROW (architect 2026-10-02, the Windows highlight):
        // the row the pointer is over, or the press is armed on, is a FLAT
        // FILL IN THE SELECTED FILL over the whole item box — square, no
        // outline, inside the frame's one-px margin — under the selected text,
        // label and accelerator alike (the theme's selected pair).
        // It is a SELECTION FACE,
        // NOT A HOVER: the hovered-item tracking is what a press resolves
        // against, and the row it lights is the row a press arms (ON SCREEN
        // IS AS PAINTED). EXACTLY ONE ITEM IS EVER LIT: the arm follows the
        // pointer while a press is live, so the pressed and hovered indices
        // are then the same item.
        // A GREYED ROW WEARS NO FACE, gated here rather than trusted to the
        // input side's resolve: a row can grey under a resting pointer with no
        // event to refresh it. It keeps its geometry and its slot and differs
        // in ink alone.
        const bool lit = enabled[i] && (app.dropdown.pressed_item == i ||
                                        app.dropdown.hovered_item == i);
        if (lit) paint_cell_rect(cr, item, palette().selected_fill);

        // LEFT-ALIGNED AT THE ONE INDENT, measured from the POPUP box's own left
        // edge in every menu, and vertically centred by the shared solver. On
        // the settings menu the right side carries the leftover, which the
        // minimum width above is what guarantees; on a COMMAND menu the
        // accelerator carries it.
        const double base = redesign_baseline(font,
                                              static_cast<double>(item.y),
                                              static_cast<double>(item.h));
        // THE INKS: the selected text on the highlight; the theme's label on
        // a live row; a DISABLED row's label THE DISABLED EMBOSS
        // (show_embossed_run) — one disabled rule for the menu anchors and
        // the menus (architect 2026-10-03, Windows' DSS_DISABLED). A disabled
        // row is never lit (the gate above).
        const auto show_row_run = [&](const text_shape::ShapedRun& r,
                                      double rx) {
            if (!enabled[i]) {
                show_embossed_run(cr, r, rx, base);
                return;
            }
            set_palette_source(cr, lit ? palette().selected_text
                                       : palette().label);
            text_shape::show_shaped_run(cr, r, rx, base);
        };
        show_row_run(runs[i], static_cast<double>(x + pad_l));

        // THE ACCELERATOR COLUMN is RIGHT-ALIGNED to the popup's own right
        // margin, not to the item box's: the margin is a fact about the box,
        // and aligning to it keeps every hotkey's last ink column on one line
        // whatever the item inset is. ITS INK IS ITS ITEM'S OWN (architect
        // 2026-10-03, Windows' ink, no dims: the kdenlive design's dimmer
        // accelerator column retired with the luminance rule) — the label on
        // a live row, the selected text on the lit one, the emboss on a
        // disabled one. The column exists wherever a menu's rows carry a
        // hotkey string (the optional width term above).
        if (row.hotkey != nullptr) {
            const double hot_x =
                static_cast<double>(x + w - pad_r) -
                std::nearbyint(hot_runs[i].width_px);
            show_row_run(hot_runs[i], hot_x);
        }
        iy += item_h;
    }

    cairo_restore(cr);
}

// -- THE RULER LANE (top lane 3, row 5 of the redesign) ---------------------
//
// A LOOK/MODEL SPLIT, and it is deliberate: the ruler takes KDENLIVE'S LOOK and
// REAPER'S GEOMETRY MODEL (architect 2026-08-01).
//   LOOK, from row_5_full.png (its colours and the label size the 2026-10-02
//     design's: ETCHED ticks, the theme's label at the small face): the two tick lengths
//     differing at their TOP
//     — majors rise kRulerMajorRisePx above the marker lane, minors start at it, and BOTH run
//     down to the marker lane's bottom (the waveform top). That shared bottom is
//     what makes "the majors peek above the flags" the whole mechanism; the
//     brief's "minors end where the marker band begins" was superseded by the
//     measurement.
//   MODEL, from Reaper: WHERE the ticks go. A round ladder of labeled steps, the
//     smallest rung whose label pitch clears the minimum, and eight binary
//     minors inside each step.
// The composite's own tick spacing is neither — it is a hand-assembled kdenlive
// frame, and it carries elements (a zone edge or a guide) this product has no
// analogue for. It was measured for the LOOK only.
//
// PAINT-ONLY. Nothing here snaps, authors, or hit-tests: the ladder decides
// pixels and nothing else.
namespace {

// THE ROUND LADDER of labeled steps, in milliseconds. Every rung is a value a
// musician reads without arithmetic; the gaps (no 3s, no 15s, no 45s) are the
// point, not an omission.
constexpr int64_t kRulerLadderMs[] = {
    125, 250, 500, 1000, 2000, 5000, 10000, 30000,
    60000, 120000, 300000, 600000, 1800000, 3600000,
};
// Eight binary minors inside each labeled step: the step halves three times, so
// a minor is always a musically-round fraction of its label.
constexpr int  kRulerMinorsPerStep = 8;
// The pitch rule, stated on the MINOR because that is the crowding that matters:
// a rung is admissible while its minors stay at least this far apart, which puts
// its labels at least 8x that apart. 9 Windows px (the laptop pixel's 12
// re-authored at the unit's change, architect 2026-10-02).
constexpr double kRulerMinMinorPitchPx = 9.0;
// THE LABELS' CAP TOP, in Windows px under the lane's top: 4, AT EVERY SCALE
// (architect 2026-10-02, the U4 mock's "above 6, below 10" laptop rows,
// re-authored as 4 and 7 at the unit's change, the labels at the small
// face). THE LINE'S TOP PAD IS DERIVED, never authored: the label is a LINE
// (line_baseline), whose baseline sits nearbyint(ascent) under the line's
// top, so its cap top sits nearbyint(ascent) - nearbyint(cap) under it, and
// the pad that lands the cap top at this row is
//
//     pad = max(0, scaled_px(4) - (nearbyint(ascent) - nearbyint(cap)))
//
// of the label's own face, THE SMALL FACE (gui_font(GuiFace::Small)), both
// metrics its recorded ones (gui_font.h). Its cell is all cap — ascent 7,
// cap 7, Small Fonts' digit — so the pad is scaled_px(4) itself at every
// scale (2026-10-05); the rule stays general because the face's ascent-minus-cap
// is the face's to change. The rule's one implementation is
// ruler_label_baseline_px below; the lane's height under the labels is
// ruler_lane_h_px's arithmetic (render.h).
constexpr double kRulerLabelCapTopPx   = 4.0;
// How far a MAJOR tick rises above the marker lane. Minors rise none. 3
// Windows px (the laptop pixel's 4 re-authored, architect 2026-10-02).
constexpr double kRulerMajorRisePx     = 3.0;

// The smallest ladder rung whose minors clear the minimum pitch. Falls back to
// the coarsest rung when even that crowds (an absurd zoom-out), which is the
// honest answer: keep the topmost rung rather than draw a solid band of ticks.
int64_t ruler_step_ms(double ms_per_px) {
    if (ms_per_px <= 0.0) return kRulerLadderMs[0];
    for (int64_t step : kRulerLadderMs) {
        const double minor_px =
            (static_cast<double>(step) / kRulerMinorsPerStep) / ms_per_px;
        if (minor_px >= kRulerMinMinorPitchPx * gui_scale_factor()) return step;
    }
    return kRulerLadderMs[std::size(kRulerLadderMs) - 1];
}

// `M:SS.mmm`, REAPER VERBATIM: the minutes field is ALWAYS present, zero
// included — "0:47.250" below a minute, not "47.250" (architect 2026-08-01,
// retiring the leading-unit dropping, which was the planner's invention and not
// what Reaper does; a field that appears and disappears makes the ruler's own
// format a moving target to read).
//
// THE MILLISECONDS RULE STANDS: the fraction shows only while the labelled step
// is sub-second, so a bar-level ruler reads "1:30" and a transient-level one
// reads "0:02.250" — each showing exactly what varies, which is the half of the
// old rule that was actually Reaper's.
std::string ruler_label_text(int64_t ms, int64_t step_ms) {
    if (ms < 0) ms = 0;
    const int64_t m   = ms / 60000;
    const int64_t s   = (ms % 60000) / 1000;
    const int64_t mil = ms % 1000;
    char buf[32];
    if ((step_ms % 1000) != 0)
        std::snprintf(buf, sizeof(buf), "%lld:%02lld.%03lld",
                      (long long)m, (long long)s, (long long)mil);
    else
        std::snprintf(buf, sizeof(buf), "%lld:%02lld",
                      (long long)m, (long long)s);
    return std::string(buf);
}

// THE LABELS' BASELINE, in device rows under the ruler lane's top: the
// derived pad of kRulerLabelCapTopPx's rule, then line_baseline's ascent. ONE
// SEAT FOR ITS TWO READERS, the painter (paint_ruler_row) and the lane's own
// height (ruler_lane_h_px), each handing in the label face at its painted
// size, so the digits and the ground beneath them are one measurement and
// never two.
int ruler_label_baseline_px(const GuiFont& font) {
    const int ascent_to_cap =
        static_cast<int>(std::nearbyint(gui_font_ascent_px(font))) -
        static_cast<int>(std::nearbyint(gui_font_cap_px(font)));
    const int pad =
        std::max(0, scaled_px(kRulerLabelCapTopPx) - ascent_to_cap);
    return static_cast<int>(line_baseline(font, static_cast<double>(pad)));
}

} // namespace

// THE RULER LANE'S DERIVED HEIGHT (the rule and its scales at the
// declaration, render.h): the labels' baseline, then kRulerBaselineToMarkerPx
// authored rows to the marker lane. Read at LAYOUT, outside any paint, off
// the small face's recorded metrics (gui_font.h), the very numbers the
// painter's seat reads, so the digits and the ground beneath them are one
// measurement. Cheap enough to answer per query.
int ruler_lane_h_px() {
    return ruler_label_baseline_px(gui_font(GuiFace::Small)) +
           scaled_px(kRulerBaselineToMarkerPx);
}

// THE MARKER LANE'S DERIVED ROWS (the rule and its scales at render.h's
// marker_lane_h_px block): the flag box is its top edge band, one Windows px
// of face, the BODY FACE'S WHOLE RECORDED CELL (its ascent above the
// baseline, its descent from the baseline's row down), a second Windows px of face and its
// bottom edge band (architect 2026-10-05: 17 Windows px, the period's
// one-line field); the label's baseline is the edge, the face and the ascent
// under the box's top, at every scale and in either face; the lane is the
// box with one Windows px of air above it and none below (architect
// 2026-10-03: the box stands on the well). The ascent and the descent are
// Windows-px elements each rounded on its own (scaled_px), the box the sum
// of its rounded parts. MEMOIZED ON THE SCALE: the lane table asks on every
// geometry query.
namespace {
struct MarkerLaneRows {
    int percent   = -1;
    int box_h     = 0;
    int baseline  = 0;   // under the box's top
    int lane_h    = 0;
};
const MarkerLaneRows& marker_lane_rows() {
    static MarkerLaneRows rows;
    const int percent = gui_scale_percent();
    if (percent == rows.percent) return rows;
    const GuiFaceMetrics& cell = gui_face_metrics(GuiFace::Body);
    const int above = scaled_px(cell.ascent, 1);
    const int below = scaled_px(cell.descent);
    const int edge  = marker_flag_edge_h_px();
    const int face  = marker_flag_face_px();
    rows.box_h    = edge + face + above + below + face + edge;
    rows.baseline = edge + face + above;
    rows.lane_h   = rows.box_h + marker_lane_air_px();
    rows.percent  = percent;
    return rows;
}
} // namespace

int marker_flag_box_h_px()    { return marker_lane_rows().box_h; }
int marker_flag_baseline_px() { return marker_lane_rows().baseline; }
int marker_lane_h_px()        { return marker_lane_rows().lane_h; }

void GuiPaintHandler::paint_ruler_row(cairo_t* cr) {
    const GuiRect lane   = top_ruler_row_area(app);
    const GuiRect marker = top_marker_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;

    cairo_save(cr);
    set_palette_source(cr, palette().ground);
    cairo_rectangle(cr, lane.x, lane.y, lane.w, lane.h);
    cairo_fill(cr);

    // THE DISPLAYED BASIS, not the live viewport: the ruler must agree with the
    // pixels actually on screen, so it reads the same plate epoch the playheads
    // and the flag cache do. That is also what hooks it to the per-pan/zoom
    // repaint — every user-driven viewport change runs the synchronous plate
    // rebuild and repaints the strip, and this pass rides along with it.
    const PlateViewportBasis basis = plate_viewport_basis();
    const int sr = audio.sample_rate();
    if (basis.spp <= 0.0 || sr <= 0) { cairo_restore(cr); return; }

    const double ms_per_px = basis.spp * 1000.0 / static_cast<double>(sr);
    const double vp_ms     = basis.vp_start * 1000.0 / static_cast<double>(sr);
    // THE WALK WIDTH IS THE PLATE'S OWN, NOT THE LIVE ONE — the same
    // published-width rule the flag cache spells at its wave_w read
    // (waveform_cache.cpp). The spp above comes from the published fingerprint,
    // so the width bounding the tick walk, the label span and the head's
    // column gate must be the width that spp was published AGAINST:
    // during the async resize window (on_resize stores new dimensions while the
    // OLD plate stays blitted until the worker publishes) a grown live width
    // would walk ticks across the newly exposed right-hand area at the old
    // plate's scale — ticks disagreeing with the ink they sit over. The
    // live-width fallback mirrors plate_viewport_basis's own cold arm (no plate
    // published yet, spp already live).
    const int wave_w = wf_cache.fp_area_w > 0 ? wf_cache.fp_area_w
                                              : waveform_area(app).w;
    if (ms_per_px <= 0.0 || wave_w <= 0) { cairo_restore(cr); return; }

    const int64_t step  = ruler_step_ms(ms_per_px);
    const double  end_ms = vp_ms + ms_per_px * wave_w;
    // (There is no `minor` time step any more. It had two consumers — the float
    // placement of each minor tick and the head's since-deleted float
    // re-derivation of which columns carried one — and the rigid comb replaced
    // both with integer
    // distribution across a segment. The MINORS-PER-STEP count is still the
    // ladder's own kRulerMinorsPerStep; only its expression as a duration is
    // gone.)

    const int tick_bottom = marker.y + marker.h;         // the waveform top
    const int minor_top   = marker.y;                    // no rise
    const int major_top   = marker.y - scaled_px(kRulerMajorRisePx);

    const GuiFont font = gui_font(GuiFace::Small);
    // THE LABEL IS A LINE, not a box, AT THE SMALL FACE
    // (gui_font.h): the seat is line_baseline's ascent off a top pad DERIVED so the cap top
    // lands kRulerLabelCapTopPx authored rows under the lane's top (the rule
    // and its three scales at that constant), through the one seat the lane's
    // height also reads (ruler_label_baseline_px). THE SEAT IS ANCHORED TO THE
    // LANE'S TOP, never centred in it or hung from its bottom: the lane's
    // height below the baseline is kRulerBaselineToMarkerPx's (render.h).
    const double baseline =
        static_cast<double>(lane.y + ruler_label_baseline_px(font));

    // THE COMB IS RIGID UNDER PAN (architect 2026-08-01, from the grab-pan
    // shimmer at working zoom: the minor ticks visibly stepped at different
    // moments, a breathing comb; the majors read fine).
    //
    // THE MECHANISM, verified in the code this replaces: every tick rounded its
    // OWN float time->pixel position independently, `nearbyint((t - vp_ms) /
    // ms_per_px)`. Tick spacing in pixels is minor/ms_per_px, which is not an
    // integer in general, so each tick carries its own fractional phase; a pan
    // shifts every phase by the same amount but each tick crosses ITS rounding
    // boundary at a different viewport offset, so neighbours take their 1px step
    // on different frames. The comb breathes even though nothing about the time
    // grid moved.
    //
    // THE FIX: each MAJOR keeps its own rounded position, and its EIGHT MINORS
    // are placed at integer offsets DISTRIBUTED ACROSS THE SEGMENT'S INTEGER
    // WIDTH — offset(i) = nearbyint(i * seg_w / 8) from the major's rounded x,
    // where seg_w is the distance between two rounded majors. A distribution of
    // an integer width is a pure function of that width, so while the majors
    // translate by whole columns the entire comb translates with them, rigidly:
    // no minor rounds against the screen at all. Under ZOOM the widths change
    // and the distribution re-derives, which is a real spacing change rather
    // than jitter.
    //
    // THE TRADE, the architect's own ("the ticks are purely informative — I'd
    // rather have smoothness than total precision"): a distributed minor can sit
    // up to ONE PIXEL off its exact time position — half from its major's own
    // anchor rounding and half from the distribution's — measured at 1.000px
    // worst case over a swept zoom/rung/phase range, on segments the ladder
    // keeps at least eight minimum minor pitches wide (72 Windows px). It NEVER ACCUMULATES: every segment re-anchors
    // on its own major, so the error is bounded inside one segment rather than
    // walking across the ruler. One pixel on a countable informative line, in
    // exchange for a comb that stops breathing under every pan.
    const int64_t first_step = static_cast<int64_t>(std::floor(vp_ms / step));
    // A step index's own rounded column: the ONE place a tick position meets the
    // screen grid. Majors anchor here; minors are distributed between them.
    const auto major_col = [&](int64_t k) {
        const double t = static_cast<double>(k) * static_cast<double>(step);
        return static_cast<int>(std::nearbyint((t - vp_ms) / ms_per_px));
    };
    // A label starts this far right of its major's column: the tick's own
    // width plus 2 Windows px (the rule at the label's paint below).
    const int label_dx = waveform_line_px() + scaled_px(2);
    // THE LABELS SLIDE IN AND OUT AT BOTH EDGES (architect 2026-10-05, "like
    // the marker flags and their text"): a label is the clip's to cut, never
    // the walk's to drop. A label runs RIGHTWARD from its major, so a major
    // LEFT of the view can still reach into it; the walk therefore starts at
    // the earliest major whose label, measured on the face it is drawn in,
    // still reaches column 0 (first_step's own major stands at or left of
    // column 0, so it is always walked). At the RIGHT a major past end_ms has
    // its label wholly past the view, so the break below drops nothing
    // visible.
    int64_t k_first = first_step;
    while (k_first > 0) {
        const int64_t prev = k_first - 1;
        const text_shape::ShapedRun prev_run = text_shape::shape_text_run(
            font, ruler_label_text(prev * step, step).c_str());
        if (major_col(prev) + label_dx + prev_run.width_px <= 0.0) break;
        k_first = prev;
    }
    // THE WALK'S CLIP: the waveform's columns [0, wave_w), the flags' own
    // (render.cpp, clip_to_waveform_columns), over the lane and the ticks'
    // descent — so a label is cut at the window's left edge and at the last
    // column exactly as a flag's text is, and the leftover strip a
    // non-multiple-of-16 window leaves beside wave_w carries no digit.
    cairo_save(cr);
    cairo_rectangle(cr, lane.x, lane.y, wave_w, tick_bottom - lane.y);
    cairo_clip(cr);
    for (int64_t k = k_first; ; ++k) {
        const double step_ms = static_cast<double>(k) * static_cast<double>(step);
        if (step_ms > end_ms) break;
        const int mx    = major_col(k);
        const int seg_w = major_col(k + 1) - mx;
        for (int i = 0; i < kRulerMinorsPerStep; ++i) {
            // THE INTEGER COMB, and the only culling test there is: a tick is at
            // its distributed column or it is offscreen. The old float-time
            // pre-filter went with the float positions it filtered. THE TEST
            // CULLS THE TICK ALONE: a major's label paints whether or not its
            // tick is on the lane (the walk's start above), the clip cutting it.
            const int col = (i == 0)
                ? mx
                : mx + static_cast<int>(std::nearbyint(
                      static_cast<double>(i) * static_cast<double>(seg_w) /
                      static_cast<double>(kRulerMinorsPerStep)));
            const bool major = (i == 0);
            const bool tick_on_lane = col >= 0 && col < wave_w;
            if (!tick_on_lane && !major) continue;
            // waveform_line_px() wide (render.h, the class's one inventory),
            // left edge on the tick's own column, clipped at the right edge.
            // EVERY TICK IS ETCHED (architect 2026-10-02, the Sonic Foundry
            // etching): the tick in the theme's Shadow, then one line of
            // Hilight immediately to its RIGHT over exactly the tick's own
            // rows, drawn right after it, so the labels, the head, the flags
            // and the stems cover the pair wherever they cover the tick.
            const int tick_top = major ? major_top : minor_top;
            if (tick_on_lane) {
                set_palette_source(cr, palette().shadow);
                fill_waveform_line(cr, lane.x, wave_w, col, tick_top,
                                   tick_bottom);
                set_palette_source(cr, palette().hilight);
                fill_waveform_line(cr, lane.x, wave_w,
                                   col + waveform_line_px(), tick_top,
                                   tick_bottom);
            }
            if (!major) continue;
            // The label starts past its major tick's own width plus 2 Windows
            // px — the etched Hilight line beside the tick and one px of air
            // (architect 2026-10-02: kept at 2 through the unit's change, the
            // conversion's 1 leaving the label touching the etched line).
            // The label's TIME is still the exact step time — only tick
            // PLACEMENT is distributed, and a major is at its own exact time
            // anyway. Its x rides `col`, which for a major IS the rounded major,
            // so number and line cannot drift apart.
            // step_ms is INTEGRAL BY CONSTRUCTION (k * step, both int64, the
            // product exact in double at any ruler magnitude), so this is a
            // representation change, not a rounding: llrint reads the integer
            // back and can never meet a tie. (nearbyint is the rule where a
            // fraction is actually rounded; the trim bar's displayed_trim_ms
            // cast makes the same integral-valued claim.)
            const int64_t label_ms = static_cast<int64_t>(std::llrint(step_ms));
            if (label_ms < 0) continue;
            const std::string txt =
                ruler_label_text(label_ms, step);
            const text_shape::ShapedRun run =
                text_shape::shape_text_run(font, txt.c_str());
            // EVERY LABEL IS ONE COLOUR (architect 2026-10-02, on the Y1
            // mock: "give the same colour to all the numbers"), THE THEME'S
            // LABEL (architect 2026-10-03).
            set_palette_source(cr, palette().label);
            text_shape::show_shaped_run(cr, run,
                                        static_cast<double>(lane.x + col +
                                                            label_dx),
                                        baseline);
        }
    }
    cairo_restore(cr);

    // -- THE PLAYHEAD HEAD AND ITS MARKER-LANE COLUMN --------------------------
    //
    // THE HEAD IS WORDPAD'S RULER INDENT MARKER (architect 2026-10-05, the
    // glyph and its provenance at kPlayheadHeadGlyph, render.h), a FIXED 9 x 8
    // Windows-px marker DRAWN AS NESTED OUTLINES (architect 2026-10-06: the
    // chrome is scalable) — the silhouette, the bevel's outer edge and the
    // face, the rings render.h derives from the bitmap — antialiased, its
    // diagonals smooth at every scale: a bevelled chip in four chrome roles.
    //
    // TIP-DOWN ONE WINDOWS PX INTO THE MARKER LANE (architect 2026-10-05,
    // moved down from flush on the ruler lane's own bottom row, architect
    // 2026-09-23): its tip row is the marker lane's FIRST row — the one
    // Windows px of air above the flag box, marker_lane_air_px — touching the
    // box's own top edge, rather than the ruler lane's last row. A flag box
    // still stands whole: the box painter below takes only its own band
    // (marker_flag_box_band), never the lane's air, so the head's tip and a
    // flag's top edge meet without either covering the other. THE SEAT TERM
    // IS UNCHANGED BY THE GLYPH'S OWN HEIGHT (kRulerBaselineToMarkerPx,
    // render.h, states the new height's effect on how the head's top sits
    // against the digits at each scale — a small gap at 138 %, a two-row
    // overlap at 275 %, flush at 400 %, every case within his "a little
    // overlap is fine" ruling).
    //
    // THE GLYPH IS OPAQUE (architect 2026-10-02: "the classic Windows way"):
    // it paints over whatever this painter already laid down in its band —
    // the labels and the ticks' rise — painted after them, in FOUR CHROME
    // ROLES, never a role of its own (render.h's playhead paragraph states
    // which region paints which): K in LABEL (the outline, the Windows 95
    // arrow cursor's edge), W in HILIGHT and S in SHADOW (the bevel), '.' in
    // GROUND at every time: the hold posture (AppState::camera_hold) has no
    // visible lamp for now (architect 2026-10-05: a white centre is not
    // Windows' marker; a visible hold cue is a later discussion). Outside the
    // silhouette nothing is painted, its antialiased edge blending into what
    // lies beneath.
    //
    // THE PLAYHEAD'S COLUMN THROUGH THE MARKER LANE IS THIS PAINTER'S TOO: a
    // waveform_line_px()-wide run in the `playhead_stem` role (the waveform
    // segment's own
    // columns) from the marker lane's top to the waveform top,
    // where render_playhead's waveform segment (paint_playheads) begins and
    // crosses the well's top lines, so head, column and stem read as one
    // unbroken object. It paints HERE, before the flag blit that follows this
    // pass, so a flag box standing in the column covers it over the box's
    // rows — the hidden-by-marker model — and the run shows in the lane's
    // air above the boxes (none below them since 2026-10-03: a box stands on
    // the well). It obeys the waveform segment's
    // own suppression (playhead_stem_suppressed): where a marker's stem stands
    // on the playhead's frame the whole stem yields to that marker, whose flag
    // then stands in the lane at that column, and the HEAD alone still paints, as
    // it always has in that case.
    //
    // IT STAYS IN THIS PAINTER for its band: the ruler's bottom rows are this
    // painter's lane, and the head must paint over the labels and the ticks
    // the walk above just painted.
    //
    // The whole object is the RESTING CURSOR'S: the `h` view, the render
    // player and the audition reach it through this one block, and the scanner
    // keeps its bare waveform line (paint_scanner).
    //
    // THE HALF-HEAD RULE (2026-05-09, d4e4e04e; restored 2026-09-26, architect):
    // the HEAD paints whenever any part of it overlaps the waveform's columns
    // [0, wave_w), CLIPPED to them, so a playhead whose column lies just past
    // either edge shows the head's nearer half there. The case that needs it
    // is the right edge: a frame in the song's last half-column rounds to grid
    // point wave_w, one past the last column — End's landing at the whole-song
    // zoom in the ordinary case (a file short enough that the fit saturates at
    // the floor occupies less than the window and its last frame paints
    // inside), and at the right wall at some zooms — and the head's left
    // half at the edge keeps that playhead on screen at its true point rather
    // than vanishing or being pulled inward. The marker-lane STEM stays gated
    // to [0, wave_w): a column past the last has no pixel of its own. A MARKER
    // there shows the same edge its own way (architect 2026-09-26): its flag's
    // left border alone on the last column(s), no stem — the flag iterator's
    // cull and the lane's clip to the waveform's columns, render.cpp.
    {
        const double cursor_px = playhead_pixel_x(
            app, static_cast<int64_t>(basis.vp_start), basis.spp);
        const int col = static_cast<int>(std::nearbyint(cursor_px));
        // THE GLYPH'S ONE QUANTUM, identical to the stem's own width by
        // construction (both are scaled_px(1, 1), render.h): `u` sizes every
        // glyph unit, `t` is kept as its own name because it is the stem's
        // width in the cull formula below, even though the two are always
        // the same number.
        const int u     = playhead_head_unit_px();
        const int t     = waveform_line_px();
        const int reach = playhead_head_half_w_px();  // the widest rows' half
        if (col + reach + t - 1 >= 0 && col - reach <= wave_w - 1) {
            const int rows = playhead_head_h_px();  // kPlayheadHeadRows * u
            // THE BAND'S BOTTOM EDGE IS ONE WINDOWS PX INTO THE MARKER LANE
            // (architect 2026-10-05), not the ruler lane's own bottom: the tip
            // row now lands on the marker lane's first row — the one Windows
            // px of air above the flag box (marker_lane_air_px) — touching
            // the box's top edge, so the band spans the ruler lane's last
            // (rows - 1) rows plus that one marker-lane row. The seat used to
            // sit flush on the ruler lane's bottom (kRulerBaselineToMarkerPx's
            // old rule); the move is this one term, so a playhead's damage
            // rect must include it (below).
            const int    head_bottom = marker.y + marker_lane_air_px();
            const int    head_top    = head_bottom - rows;
            // NO HOLD LAMP (architect 2026-10-05): the glyph's '.' region is
            // GROUND whether or not the hold posture stands; a visible hold
            // cue is a later discussion.
            //
            // THE FOUR REGIONS, ONE PARTITION IN ONE GROUP (render.h's head
            // paragraph): the outline (ring 0 less ring 1), the bevel's two
            // halves (ring 1 less ring 2, split at the top-right corner along
            // x = 7 and at the tip along the axis) and the face (ring 2), in
            // the glyph's Windows px from its top-left (gx, gy), u device px
            // each. Each is filled ADDED into a cleared group, so where two
            // regions share an antialiased edge their coverages sum to the
            // whole pixel and no colour bleeds through the seam, then the
            // group is painted over the ruler once. Column 4's cell (the tip
            // column) sits on the stem's own [col, col + t) because u and t
            // are the same quantum (render.h), so the tip, 4.5 units in,
            // lands on the stem's centre.
            const double gx = lane.x + col - 4.0 * u;
            const double gy = head_top;
            const auto ring = [&](double k) {
                cairo_move_to(cr, gx + k * u,         gy + k * u);
                cairo_line_to(cr, gx + (9.0 - k) * u, gy + k * u);
                cairo_line_to(cr, gx + (9.0 - k) * u, gy + 4.0 * u);
                cairo_line_to(cr, gx + 4.5 * u,       gy + (8.0 - k) * u);
                cairo_line_to(cr, gx + k * u,         gy + 4.0 * u);
                cairo_close_path(cr);
            };
            const auto poly = [&](std::initializer_list<std::pair<double, double>>
                                      pts) {
                bool first = true;
                for (const auto& [px, py] : pts) {
                    if (first) cairo_move_to(cr, gx + px * u, gy + py * u);
                    else       cairo_line_to(cr, gx + px * u, gy + py * u);
                    first = false;
                }
                cairo_close_path(cr);
            };
            // THE CLIP to the waveform's columns (the half-head rule above),
            // over the head's band alone and released before the stem.
            cairo_save(cr);
            cairo_rectangle(cr, lane.x, head_top, wave_w, rows);
            cairo_clip(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
            cairo_push_group(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
            cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
            // K: the outline, ring 0 with ring 1 cut out.
            cairo_new_path(cr);
            ring(0.0);
            ring(1.0);
            set_palette_source(cr, palette().label);
            cairo_fill(cr);
            // W: the bevel's top and left, ring 1's top-left half less ring 2.
            cairo_new_path(cr);
            poly({{1.0, 1.0}, {7.0, 1.0}, {7.0, 2.0}, {2.0, 2.0}, {2.0, 4.0},
                  {4.5, 6.0}, {4.5, 7.0}, {1.0, 4.0}});
            set_palette_source(cr, palette().hilight);
            cairo_fill(cr);
            // S: the bevel's right, the top-right corner and the tip's right
            // half.
            cairo_new_path(cr);
            poly({{7.0, 1.0}, {8.0, 1.0}, {8.0, 4.0}, {4.5, 7.0}, {4.5, 6.0},
                  {7.0, 4.0}});
            set_palette_source(cr, palette().shadow);
            cairo_fill(cr);
            // '.': the face, ring 2.
            cairo_new_path(cr);
            ring(2.0);
            set_palette_source(cr, palette().ground);
            cairo_fill(cr);
            cairo_pop_group_to_source(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
            cairo_paint(cr);
            cairo_restore(cr);

            if (!playhead_stem_suppressed()) {
                set_palette_source(cr, palette().playhead_stem);
                fill_waveform_line(cr, lane.x, wave_w, col, marker.y,
                                   marker.y + marker.h);
            }
        }
    }

    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_waveform_plate -------------------------------

void GuiPaintHandler::paint_waveform_plate(cairo_t* cr, const GuiRect& area) {
    // wf_cache.surface is produced by one of two paths, both of which
    // leave this paint path blit-only:
    //   1. Worker full render — maybe_enqueue_waveform_render
    //      dispatches a full-window render on GuiWaveformWorker,
    //      which swaps into wf_cache.surface on completion. Fires
    //      for UNDRIVEN changes — resize, the launch load — and as the
    //      on_tick backstop for any residual fingerprint drift (a
    //      warp_frame_map hash included). Map EDITS themselves are
    //      user-driven and take path 2, and so does FOLLOW'S PAGE TURN since
    //      2026-09-02 (it was on this list until then; the playhead line is
    //      drawn onto that very frame, so the page turn cannot wait a
    //      publish — Viewport::follow_scroll_if_needed).
    //   2. Synchronous full render — force_synchronous_waveform_rebuild
    //      renders the full window inline on the GUI thread for every
    //      USER-DRIVEN viewport change: zoom, center-on-playhead, the
    //      one-shot jumps, and panning/scrolling (which had its own
    //      incremental shift-and-strip path until 2026-07-26 — retired so
    //      a moving plate and a resting one come off one route).
    // The paint path is blit-only — it draws whatever pixels the
    // live surface currently holds. For worker-path renders that may
    // be a one- or two-frame-old viewport during the worker-rebuild
    // window; the synchronous path updates the plate in the same
    // frame, so it has no such lag. The flag layer closes
    // any mismatch by layering flags onto a surface keyed
    // off the same displayed-viewport.
    //
    // If wf_cache.surface is null (initial load, before the first
    // worker completion), the blit is skipped and the canvas
    // ground fill shows through. The user-visible difference is one
    // extra paint frame of empty canvas between load and first
    // waveform display, masked by the existing load-time progress
    // bar.
    //
    // BLIT-ONLY HERE, AND NOTHING RECOLORS IT AFTER: this call writes the
    // plate's pixels as the renderer wrote them, composited once over the
    // plain `waveform_canvas` ground render_canvas laid — the plate's
    // transparent gaps show that ground, the canvas having that one painter —
    // and the blitted pixels are final. No pass recolours the waveform: the
    // out-of-trim dim retired 2026-07-26 and the sweep's region highlight
    // 2026-10-03 (architect, "the trim bar is enough"), the trim bar being
    // the whole picture of the window, the sweep's included.
    //
    // The clip is the CONTENT band, not the full area: the area's top and
    // bottom rows are render_canvas's well lines and no band-filling pass may
    // cover them. (The plate's own inset band leaves those rows transparent
    // anyway, so this is the structural statement of the rule rather than a
    // pixel change.)
    if (wf_cache.surface) {
        const GuiRect content = waveform_content_rect(area);
        cairo_save(cr);
        cairo_rectangle(cr, content.x, content.y, content.w, content.h);
        cairo_clip(cr);
        cairo_set_source_surface(cr, wf_cache.surface,
                                 area.x, area.y);
        cairo_paint(cr);
        cairo_restore(cr);
    }
}

// -- GuiPaintHandler::plate_viewport_basis ------------------------------

// See the declaration comment in paint_handler.h: the fp-recipe basis locked to
// the blitted plate while the worker rebuilds, with the live spp fallback when
// no plate has published a span yet.
GuiPaintHandler::PlateViewportBasis
GuiPaintHandler::plate_viewport_basis() const {
    PlateViewportBasis b;
    b.spp = wf_cache.fp_area_w > 0
        ? static_cast<double>(wf_cache.fp_vp_end - wf_cache.fp_vp_start) /
          static_cast<double>(wf_cache.fp_area_w)
        : painter_samples_per_pixel(app, audio, waveform_area(app));
    b.vp_start = static_cast<double>(wf_cache.fp_vp_start);
    return b;
}

// -- GuiPaintHandler::phase_reset_overlay_band / its ring pass ------------

// (THE ENGINE'S SEED FRAME FOR A RESET was a file-local static here from
// 2026-09-02 until 2026-09-09, when the phase-reset column got its own
// iteration bracket: the walls, the bound editor's refusal, the arrows'
// landing and the sweep all ask the same lattice this band paints, so the
// mirror moved out to warp_frame_map_view.h beside the map functions it reads
// — phase_reset_seed_frame_index, now expressed through
// phase_reset_window_centre_frame, with the engine.cpp / stft_container.h
// LOCKSTEP warning that goes with it. This band's own use is unchanged: it
// reads the DISPLAYED map, as it always did.)

// Resolves the band shown ahead of the focused phase reset marker: from the
// marker's stem column to where the reset's SEED GRAIN ends, in target time.
// Paint-only: no persisted state, nothing on disk, no settings key, no undo
// interaction.
//
// THE BAND IS DERIVED, NOT A CONSTANT (architect 2026-09-02; the model and
// the derivation of the lead-in it depicts are at
// GuiPhaseResetMarkersOps::drop_phase_reset_lead_in_at_playhead,
// phaseresetmarkers_ops.cpp — the one prose home). The engine seeds a reset
// at the LAST synthesis frame whose window CENTRE ≤ the authored frame
// (phase_reset_seed_frame_index, warp_frame_map_view.h — the engine's rule
// mirrored) and that
// frame re-synthesizes its window verbatim — the seed grain, centred on the
// seed centre C, ending at C + N/2 — with propagation resuming past it. So:
//   LEFT EDGE  = the marker's own column (the stem's placement), the
//                authored event;
//   RIGHT EDGE = the seed centre's target image + N/2, i.e. m·kRs +
//                kPhaseResetLeadInSamples in output samples — where the
//                verbatim grain ends and the map-placed, fully re-phased
//                output begins.
// The band is what the drop's lead-in clears and what Space's lead-in launch
// skips (the launch reads the SELECTION predicate's exact
// marker + kPhaseResetLeadInSamples, never this painted edge).
// CONSEQUENCES, which are the point: the right edge sits on the engine's hop
// lattice, so it STANDS STILL while the reset is nudged within one hop cell
// and SNAPS a whole hop when the reset crosses a schedule frame's centre —
// the moment the render's bytes actually change (two resets inside one cell
// are one event). BOTH EDGES ARE PAINTED AS MAPPED COLUMNS (the column block
// below): the far edge takes its own column from its lattice point exactly as
// the near edge takes the marker's, so the standing-still is what the screen
// shows and not merely what the samples do. Its width is at most
// kPhaseResetLeadInSamples TO THE SCHEDULE'S ROUNDING — that when the marker sits on a seed centre — and at
// least about N/2 − R_s + 1 samples (the marker just short of the next
// centre); the static_assert at the constant pins the drop's offset to the
// seed window's half-width, which is exact, not the painted width, which
// carries the rounding. THE RESIDUE, ACCEPTED AND RECORDED (architect
// 2026-09-02, "no effect on audio output; accept and record"; the three terms,
// their slope and the worked case are at the drop's comment, the one prose
// home — this site states only its own share): the engine's placement compare
// is on the schedule's ROUNDED window start and this mirror rounds with it, so
// a seed centre up to half a source frame past the reset still qualifies, and
// its image is HALF A SOURCE FRAME AT THE LOCAL SEGMENT SLOPE
// 1/(tempo · marker_scale · settings_scale) — a fraction of a sample at unity
// tempo, up to ~8 at the numeric slope ceiling of 16, unbounded across a
// label-reference segment, and NOT the "sample or two" this comment claimed
// for one day. THIS SITE ADDS THE THIRD TERM: the nearbyint(T)
// below is up to another half OUTPUT sample of painted width. So the band can
// come out that much wider than N/2 (the worked case — tempo 0.30, reset
// at source 307 — paints 2049 where N/2 is 2048). NOT CLAMPED to N/2: the
// clamp would hide the engine's geometry rather than fix it, and this band
// exists to depict that geometry. It reaches no audio and it is SUB-PIXEL at
// the working zoom for every numeric slope; it is NOT the trim crop's rounding
// class (that comparison is withdrawn — the trim's rounding is transient,
// while this seam sits in every deliverable's geometry).
//
// DAMAGE. The right edge depends on the reset's frame and on the displayed
// map, both of which the left edge already depended on, so no new damage
// owner is needed: a nudge and a drag commit pay the FULL waveform-area
// invalidate (position_nudge.cpp's tail, marker_drag.cpp's commit — the
// stem's own requirement), the drag's every motion event does too
// (marker_drag.cpp's apply), and every map change (a cent step, a warp
// move) runs the full waveform damage beside its plate re-warp
// (warpmarkers_ops.cpp). A hop snap under any of them rides that damage.
//
// THE GEOMETRY AND VISIBILITY OWNER, kept SEPARATE from its one consumer
// (paint_phase_reset_overlay_ring) rather than folded into it. It carries every
// visibility gate — view, focus, the multi-select suppression, the eligible-marker
// resolve, the sub-pixel and offscreen refusals — plus the clipped span, and
// Selection::phase_overlay_subject MIRRORS its selection-state gates — MINUS the
// geometry ones, which are not selection state — to decide when a subject change
// needs waveform damage and, since 2026-07-28, whether Space auditions the
// lead-in. One rule, so it stays one function; that mirror's own reader
// inventory lives at its declaration in selection.h. (It served a second pass,
// an opaque ground recolor under the plate, until the ring became the overlay's
// whole visual — architect 2026-07-27.)
//
// Painted in TARGET view, never source view, and this is a
// phase-reset-only surface with no warp sibling (naming-symmetry asymmetry,
// recorded here per CLAUDE.md). The seed grain is an OUTPUT-domain extent —
// the engine's window, on the engine's output lattice — so target time is
// where it is a fixed picture; source view would show a map-dependent,
// varying width, misrepresenting it. Since 2026-09-21 the question no longer
// arises: the phase-reset column exists in target view alone (S+P is
// load-fatal), so the P-column gate below implies target view
// and the source-view refusal that stood beside it is an assert.
// A seed grain is a phase-reset-only concept, so there is nothing on the
// warp axis to mirror.
GuiPaintHandler::PhaseResetOverlayBand
GuiPaintHandler::phase_reset_overlay_band(const GuiRect& area) const {
    PhaseResetOverlayBand out;
    // Visibility: always-on for the focused enabled marker while the global
    // W/P mode is on P, which is target view by construction. Everything
    // downstream is domain-agnostic.
    if (app.active_markers_view != 'P') return out;
    if (area.w <= 0 || area.h <= 0) return out;
    // THE `h` HISTORY VIEW SUPPRESSES THE RING (architect 2026-08-05). The view
    // paints the DELTA and no live marker surface at all — the lane's flags and
    // its stems go through the lane's own producer ("the history mode owns the
    // lane whole", waveform_cache.cpp) — and this ring is live-store display
    // exactly as they are: what triggers it is app.last_selected_marker, the
    // LIVE focus, which the mode neither reads nor clears, so a `h` pressed with
    // a phase reset focused in P + target left the ring painting alone over the
    // diff. It joins that suppressed inventory here, at the visibility owner
    // rather than at the painter, so the one function still carries every gate.
    // NO DAMAGE OWNER IS NEEDED for the appear/disappear: the mode's entry and
    // exit each end in viewport.invalidate_all() (open_history_mode_fresh /
    // close_history_mode), which is the whole window.
    // Selection::phase_overlay_subject deliberately does NOT mirror this gate:
    // that mirror is SELECTION state, and the mode is no more selection state
    // than the geometry gates below are. The divergence has no visible
    // consequence since playback left the view (2026-08-05): the mirror's other
    // reader is Space's lead-in launch, and Space is a consumed no-op in here.
    if (app.history_mode.active) return out;
    // The multi-select suppression (architect 2026-07-23): the overlay depicts ONE
    // focused reset's lead-in, a single-focus authoring aid, so a MULTI-select
    // (2+ members) suppresses it — the state is about a span of markers rather
    // than a single focus, and the overlay would clutter. (A singleton or empty
    // selection shows it as before; the multi-select builders all damage the
    // waveform, so the overlay's appear/disappear rides their damage.)

    if (app.selected_markers.size() >= 2) return out;

    // Paint sample: the exact expression render.cpp's file-local
    // frame_to_paint_sample uses, so marker and overlay can never disagree.
    double ms;
    // The band's extent in output samples: from the marker's paint sample to
    // the seed grain's end, derived in the block below from the same frame
    // and the same map the paint sample reads.
    int64_t width_samples;
    // The reset's CLASS, for the ring's colour: the column's RESTING red set
    // keyed by store index, the set and the index the flag pass reads for
    // this reset's stem — at rest and through a drag alike, the drag writing
    // only its proposal, so the ring and the stem share one class throughout.
    bool red_class = false;
    // THE RESET'S SELECTION BIT, for the ring's colour (architect 2026-09-23:
    // the ring and the stem are one object and brighten together; 2026-10-04:
    // the stem follows its FLAG BOX). It is the bit the flag pass hands this
    // reset's PAYLOAD face, whose stem the ring mirrors
    // (render_flag_boxes_impl, render.cpp), re-spelled across the pass's
    // parameter boundary from the same state its fingerprint carries (the
    // selection, the addressed cell, the mode verdict): selected iff the reset
    // is a member (app.selected_markers, the pass's own membership set) and
    // its bright cell is the payload. This reset IS the focus, so its bright
    // cell is app.addressed_cell — falling back to the payload where that
    // bound cell is painted nowhere, which is the pass's rule and
    // marker_paints_iter_cells' question (a bound field opens only on a
    // painted cell, enter_iter_bound_edit, so the pass's suppression arm adds
    // nothing here). A selected reset whose addressed cell is a bound cell
    // keeps its resting stem, so its ring keeps the resting colour too.
    bool selected = false;
    {
        assert(app.active_audio_view == 'T');   // P stands in target alone

        const auto& markers = app.phaseresetmarkers.markers();
        const int idx = app.last_selected_marker;
        if (idx < 0 || idx >= static_cast<int>(markers.size())) return out;
        const auto& marker = markers[idx];
        // Skip a disabled focused reset — a disabled phase reset paints no
        // overlay, reading its `disabled` bool directly (phase resets carry no
        // label cascade).
        if (marker.disabled) return out;
        red_class = phase_reset_red_flag_set_cached(app).red.count(idx) > 0;
        selected  = app.selected_markers.count(idx) > 0 &&
                    (app.addressed_cell == MarkerCell::Payload ||
                     !marker_paints_iter_cells(app, 'P', idx));

        // Map selection: the DISPLAYED paint basis (displayed_or_live_target_map
        // — the SAME map the flags, stems, drag overlay and riding playhead read,
        // falling back to the live cache when cold), so the overlay stays locked
        // to the reset it annotates even inside a worker publish window where the
        // displayed map lags the live cache. No map means identity (matching the
        // stem renderer's fallback). We are already known to be in target view.
        const std::vector<WarpFrameMapSegment>* tmap = nullptr;
        const std::vector<WarpFrameMapSegment>& m =
            displayed_or_live_target_map(app, audio);
        if (!m.empty()) tmap = &m;

        // Effective time: during a phase-reset-mode drag, read the focused
        // marker's proposed time through the DragOverlay (same construction
        // as hit_test_flag). A warp-mode drag's indices refer to the warp
        // list, so guard on drag_mode 'P'; otherwise use the live store's
        // time.
        double eff_time = marker.time_frame;
        if (app.drag.active && app.drag.drag_mode == 'P') {
            DragOverlay ov;
            ov.indices = &app.drag.dragging_markers;
            ov.times   = &app.drag.moveable_times;
            eff_time = ov.effective_time(idx, marker.time_frame);
        }

        const size_t src_frame =
            static_cast<size_t>(std::nearbyint(eff_time));
        if (tmap && !tmap->empty()) {
            ms = std::nearbyint(map_source_to_target(src_frame, *tmap));
        } else {
            ms = std::nearbyint(eff_time);
        }

        // The right edge: the engine's seed frame for this reset, under the
        // same displayed map (empty = identity, the rule unchanged), then its
        // target image plus the seed window's half-width. The GUI's target
        // coordinates are the FULL target-frame domain and the trimmer's
        // pre-roll re-anchors on hop multiples of that same domain, so the
        // lattice m·kRs is the same under a trim — no anchor term.
        const int64_t seed_m = phase_reset_seed_frame_index(
            static_cast<int64_t>(src_frame), m);
        const int64_t grain_end = seed_m * kRs + kPhaseResetLeadInSamples;
        width_samples = grain_end - static_cast<int64_t>(ms);
        // ≥ 1 by construction: the seed centre's image is at most the reset's
        // TO THE SCHEDULE'S ROUNDING, so grain_end − ms > N/2 − R_s. The
        // clamp guards that rounding edge only, never a real geometry. That
        // same rounding is the residue recorded at this function's header and
        // derived at the drop's comment: the band's other end can run past N/2
        // by half a source frame AT THE LOCAL SEGMENT SLOPE — plus the half
        // output sample the nearbyint above puts into `ms` — for exactly the
        // reason this end can fall under the marker.
        width_samples = std::max<int64_t>(1, width_samples);
    }

    // Displayed-viewport recipe: same as paint_playheads, so the overlay
    // stays locked to the blitted plate while the worker
    // rebuilds against a viewport change.
    const PlateViewportBasis basis = plate_viewport_basis();
    const double spp = basis.spp;
    if (spp <= 0.0) return out;
    const double vp_start = basis.vp_start;

    // Columns: BOTH EDGES ARE MAPPED COLUMNS, each taking its own column from
    // the shared displayed_column_at rounding (warp_frame_map_view.h) —
    // left_col from the marker's paint sample, which is the stem renderer's own
    // placement, and right_col from the grain end, which is the lattice point
    // the header derives. The two endpoints round independently and the band's
    // painted WIDTH absorbs the difference, so the far edge sits on screen
    // where its lattice point sits in samples: it stands still while the reset
    // is nudged inside one hop cell and snaps a whole hop at the boundary,
    // which is what the header's "STANDS STILL" sentence promises. (The
    // fixed-width recipe that stood here — left_col plus the extent
    // banker's-rounded to whole pixels — held the width rigid instead and so
    // moved the far edge by a pixel as the marker moved inside a cell or as the
    // view panned.)
    const int left_col = displayed_column_at(ms, vp_start, spp);
    const int right_col = displayed_column_at(
        ms + static_cast<double>(width_samples), vp_start, spp);

    // Too-zoomed-out: if the extent rounds below one pixel — both edges landing
    // on the same column — paint nothing at all: no sliver, no clamped minimum.
    if (right_col <= left_col) return out;

    // The band spans columns [left_col, right_col): the stem's own column
    // (left_col) sits inside it, and the stems paint after both of the band's
    // passes, so the stem stays crisp on top of the left seam.
    double x0 = static_cast<double>(area.x + left_col);
    double x1 = static_cast<double>(area.x + right_col);

    // Horizontal clip to [area.x, area.x + area.w); the band shows whenever the
    // intersection is non-empty even if the stem column is off-screen left
    // (the tail can be visible while the stem is not).
    x0 = std::max(x0, static_cast<double>(area.x));
    x1 = std::min(x1, static_cast<double>(area.x + area.w));
    if (x1 <= x0) return out;

    out.valid = true;
    out.x0    = x0;
    out.x1    = x1;
    out.red   = red_class;
    out.selected = selected;
    return out;
}

// THE OVERLAY RING — the phase-reset overlay's WHOLE visual (architect
// 2026-07-27): the band's opaque border, waveform_line_px() thick (1px at
// 100 %, the stem's own width at every scale), in the phase-reset stem's own
// colour (see below) and nothing else,
// painted AFTER the plate. It is a BOUNDARY LINE, like the playheads and the
// stems, so an opaque line crossing waveform ink is correct and intended, and
// with no fill inside it the band now READS as the two edges of a span rather
// than as a tinted region.
//
// THE RING STANDS INSIDE THE WELL'S EDGE (architect 2026-10-06): its top run
// on the content band's first row and its bottom run on its last
// (waveform_content_rect), the verticals spanning the band between them, so
// the well's two-line sunken edge at the top and the bottom stays whole under
// a reset's span. The ring rode the area's outermost rows from 2026-08-01
// until this ruling — a frame drawn on the frame — which was settled under
// the earlier flat border, before the well had a Windows edge for the ring to
// break.
//
// A vertical side is drawn only where the band's own edge
// is the true edge — both x0 and x1 come back already clipped to the area, so a
// band running past a viewport edge draws its border there too; that is the
// same flush-to-the-edge reading the trim bridge's clipped fill has, and the
// band is an aid rather than a hit target, so no sentinel machinery is needed.
void GuiPaintHandler::paint_phase_reset_overlay_ring(
    cairo_t* cr, const GuiRect& area) {
    const PhaseResetOverlayBand band = phase_reset_overlay_band(area);
    if (!band.valid) return;

    const double w = band.x1 - band.x0;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // THE RING IS THE STEM'S COLOUR (architect 2026-08-01; the class rule
    // 2026-09-17) — "they're one unit", the ring and the stem of the reset it
    // annotates. It wears what that stem wears: the `removed_flag` face when
    // the reset is in the column's red set (band.red), the `phase_reset_flag`
    // face otherwise, each its selected face while the reset's payload box is the
    // bright one (band.selected; architect 2026-10-04, the stem follows the
    // box it leaves from). phase_reset_stem_color asks the one ladder rather
    // than restating it, so ring and stem cannot drift.
    // DAMAGE: this pass paints live in on_redraw from app state, never from a
    // cached surface, and every change to its colour's inputs misses the flag
    // cache's fingerprint, whose rebuild damages the waveform with the strip
    // (maybe_rebuild_flag_cache, waveform_cache.cpp) — the stem's own
    // repaint; a palette install damages the whole window.
    const GuiColor ring = phase_reset_stem_color(band.red, band.selected);
    // BELOW THE STEMS' FLANKS (architect 2026-10-05, fill_stem_flanks): the
    // flanks stand in the well's top lines alone, which the ring no longer
    // enters, so the reset's stem leaves its flanks straight onto the ring's
    // top run and its left side.
    set_palette_source(cr, ring);
    // THE CONTENT BAND, not the full area (architect 2026-10-06): the top run
    // lands on the canvas's first row (content.y, just under the well's top
    // lines) and the bottom on its last (content.y + content.h - 1, just
    // above the bottom lines), with the verticals spanning every row between
    // them. No other reader takes the ring's rows: it is no hit target, and
    // its damage is the whole waveform area's (the flag cache's rebuild and
    // Selection::damage_overlay_on_subject_change), which holds it at any
    // inset. EVERY SIDE IS waveform_line_px() THICK (render.h, the class's one
    // inventory), inward from the band's edges, so the left side's columns
    // [x0, x0 + t) are the stem's own; a side is never wider than the band
    // (both verticals then cover it), and the band is already clipped to the
    // waveform's columns, so no side reaches past them.
    const GuiRect content = waveform_content_rect(area);
    const double t  = static_cast<double>(waveform_line_px());
    const double sw = std::min(t, w);
    const double y0 = static_cast<double>(content.y);
    const double h  = static_cast<double>(content.h);
    cairo_rectangle(cr, band.x0, y0, w, t);              // top
    cairo_rectangle(cr, band.x0, y0 + h - t, w, t);      // bottom
    cairo_rectangle(cr, band.x0, y0, sw, h);             // left
    cairo_rectangle(cr, band.x1 - sw, y0, sw, h);        // right
    cairo_fill(cr);
    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_trim -----------------------------------------

// The LIVE trim pass: every trim pixel — the lane ground, the window bar, the
// two endcaps and the midpoint mark — paints here per frame, entirely inside
// the trim lane, which no later pass paints on; its slot is step 5 of the
// paint-order block in on_redraw (the one authoritative sequence).
//
// BASIS: the FREE item-geometry owners — item_viewport_basis(app, audio)
// and displayed_or_live_target_map(app, audio) — feeding the shared geometry
// owners displayed_trim_ms / trim_bound_column / trim_bridge_gap /
// trim_endcap_rect inside the renderer — the chain's ONE run: the trim hits
// (hit_test_trim_endcap / point_in_trim_bridge_span, both reached through
// route_trim_bar_press and the cursor map) read what this pass PUBLISHES
// (AppState::trim_bar_hit, below) rather than deriving it a second time, so
// paint == hit by publication (architect 2026-09-24) — IN EVERY STATE since
// 2026-08-18, the `h` history view's display-only diff-span substitution
// having been deleted with the architect's "trim should not change going into
// history".
// Deliberately NOT the member
// GuiPaintHandler::plate_viewport_basis(): that is the PLATE-fingerprint
// basis for plate-registered overlays, a different owner with a different
// lifecycle though one {span, width} pair at every rebuild (the split is
// stated at item_viewport_basis, app_state.h) — trim rides the ITEM basis the
// flag pixels ride, the pair they were laid out on and promoted with, which
// is the basis its published hit rects therefore carry. The
// renderer's column math therefore divides the
// basis span by basis.area_w (the width the committed items were mapped
// against), which is why the waveform rect handed to them carries that width.
//
// COLD STATES (nothing promoted yet — first paint after a load or
// load-in-place, the view
// toggle): the free accessors fall back to the LIVE viewport/map, so trim
// paints on the pre-first-publish frame too. Small intentional behavior
// change: the retired cached path SKIPPED its null cache surfaces there, so
// trim was absent for that one frame — the live pass paints it (an
// improvement, not byte-identical cold behavior).
//
// COORDINATES: both renderers take SCREEN-space rects (the top strip anchors
// at (0,0), so its screen and former cache-local coords coincide; the waveform
// rect carries its real screen x/y).
void GuiPaintHandler::paint_trim(cairo_t* cr, const GuiRect& area,
                                 const GuiRect& top_strip) {
    // No trim gate: the window is ALWAYS set (2026-07-30), so the bar and its
    // endcaps simply always paint — at the full window the caps rest on the
    // song edges and the bar spans the whole lane between them.
    //
    // THIS PASS PUBLISHES THE TRIM BAR'S HIT GEOMETRY (AppState::trim_bar_hit,
    // architect 2026-09-24 — strictly as-painted): hit_test_trim_endcap and
    // point_in_trim_bridge_span read what was drawn here rather than re-running
    // this owner chain on the live trim, so after any trim write a press
    // before the repaint grabs the caps still on screen. It republishes ONLY
    // WHEN THE DAMAGE CLIP COVERS THE WHOLE LANE, publish_button_face's gate
    // and for its reason: on_redraw runs once per damage rect and this pass
    // runs whole under each, so a narrow rect (the playhead's) would otherwise
    // stamp the stash with bounds whose pixels it never redrew. Every trim
    // write damages at least the window top through the waveform's bottom
    // (commit_trim_mutation, the sweep's and the drag's per-motion writes, the
    // maximizer, and the tab, load and load-in-place roads), so the frame that
    // shows a moved bound is always a publishing one. The early returns below
    // publish COLD when they may publish at all: a lane with no bar has
    // nothing to grab.
    const GuiRect trim_row = top_trim_row_area(app);
    // The ITEM basis (free owner; the member plate_viewport_basis is the other
    // epoch — see the header comment above), read ahead of the gate below
    // because the covering test measures the lane at its painted width.
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    TrimBarHit* const out_hit =
        clip_covers_drawable(cr, app,
                             GuiRect{trim_row.x, trim_row.y,
                                     std::max(basis.area_w, 0), trim_row.h})
            ? &app.trim_bar_hit : nullptr;
    if (area.w <= 0 || area.h <= 0 || top_strip.w <= 0 || top_strip.h <= 0 ||
        basis.area_w <= 0 || basis.spp <= 0.0) {
        if (out_hit) *out_hit = TrimBarHit{};
        return;
    }

    // The item pixels' map: empty (identity) in source view, the committed
    // displayed map (live fallback cold) in target view.
    const std::vector<WarpFrameMapSegment>& dmap =
        displayed_or_live_target_map(app, audio);
    const std::vector<WarpFrameMapSegment>* map_arg =
        dmap.empty() ? nullptr : &dmap;

    // THE SOURCE-FRAME PAIR THE BAR DISPLAYS: THE AUTHORED TRIM WINDOW, IN
    // EVERY STATE (architect 2026-08-18 — "trim should not change going into
    // history — it no longer should range from first to last diff, but stay
    // whatever it was in non-history views"). The bar showed the VIEWED
    // COMMIT'S DIFF SPAN while the `h` view stood from 2026-08-05 until then, a
    // display-only substitution at this one site; it is deleted, so the window
    // the user was in is the window the view shows, and the band's
    // double-click frames that window in here exactly as it does outside.
    // (Trim never MUTATED in the view either way: the substitution never wrote
    // app.trim, and the mode consumes every press that could.)
    const int64_t bar_begin_frame = app.trim.begin_frame;
    const int64_t bar_end_frame   = app.trim.end_frame;

    // Per-bound displayed-domain positions through the shared mapping owner
    // (displayed_trim_ms returns an integral-valued double; the int64 round
    // trip through TrimRange is exact, so trim_bound_column sees the value the
    // mapping produced). Both bounds are always meaningful.
    TrimRange trim{
        static_cast<int64_t>(displayed_trim_ms(bar_begin_frame, map_arg)),
        static_cast<int64_t>(displayed_trim_ms(bar_end_frame, map_arg))};

    // Waveform rect for the renderers: real screen origin/height, width =
    // basis.area_w (the committed item width — the column-mapping denominator
    // and the [0, wave_w) painter clip, keeping paint == hit through the
    // accepted resize window; equal to waveform_area(app).w at rest).
    const GuiRect wave_rect{area.x, area.y, basis.area_w, area.h};

    // ONE HALF ONLY since 2026-08-01: the waveform stem pass is deleted, so
    // this is the strip's bar + endcaps (with the effective-width clip that
    // slides them off the lane's edges inside render_trim_flags).
    //
    // The trim bar lane's y-band is THREADED IN as top_trim_row_area(app)
    // rather than re-derived inside the painter, and the painter publishes the
    // band it painted in as the stash's `lane` — the y-gate both trim hits
    // read — so the painted band and the clickable band are one value.
    // NO WAVEFORM STEMS (architect 2026-08-01): the bar and its two endcaps are
    // the trim window's WHOLE display. render_trim_stems drew a 1px grey
    // vertical down the waveform at each bound; the redesigned lane says the
    // window where the window is, and a pair of full-height lines competing with
    // the marker stems said it a second time in the same pixels.
    // THE HELD CAP (architect 2026-10-03, render_trim_flags' `pressed`): a
    // cap is pressed FROM ITS PRESS TO THE GESTURE'S END, as a push button
    // shows its pressed face from its press ("looks odd" when it waited for
    // the drag to start moving) — the single-bound pending (a press on the
    // cap still under the grab gate, PendingTrimDrag) and the single-bound
    // drag it becomes both press their bound's button, so a press that never
    // moves wears it until the lift. The pair (bridge) arm and drag press
    // none. The damage at both edges is input_trim.cpp's
    // (arm_pending_trim_drag, disarm_pending_trim_drag, commit_trim_drag).
    const bool single_drag = app.trim_drag.active && !app.trim_drag.both;
    const bool single_pending =
        app.pending_trim_drag.active && !app.pending_trim_drag.both;
    const bool held_is_begin = single_drag ? app.trim_drag.is_begin
                                           : app.pending_trim_drag.is_begin;
    const TrimPressedCap pressed =
        single_drag || single_pending
            ? (held_is_begin ? TrimPressedCap::Begin : TrimPressedCap::End)
            : TrimPressedCap::None;
    render_trim_flags(cr, top_strip, trim_row, wave_rect,
                      basis.vp_start_frame, basis.vp_end_frame, trim, pressed,
                      out_hit);
}

// -- GuiPaintHandler::paint_marker_stems ---------------------------------

// EVERY ENABLED MARKER STEMS, ALWAYS (row 5, architect): the per-frame waveform
// overlay that replaced the singleton selected-marker stem. The full contract —
// what stems and in what colour (a selected marker's stem brightening with its
// flag, architect 2026-09-23) — is at the declaration.
//
// It reads the marker painter's stash (app.marker_stems) instead of walking a
// store: the stem stands on its flag box's LEFT EDGE, and that column was
// already resolved by the pass that painted the box, on the displayed basis
// those pixels were laid out against. So the DragOverlay substitution, the
// source->target map walk, the per-marker cull and the colour ladder all happen
// exactly once, in the painter, and a stem can never land a pixel away from its
// own flag. Disabled markers are simply absent from the stash.
//
// The stem RUNS CONTINUOUS (architect 2026-10-02): from the waveform area's
// top — through the well's top lines, straight on from the box's bottom row,
// which is the marker lane's last (architect 2026-10-03: the box stands on
// the well) — to the canvas's foot, where it stops at the well's bottom
// lines (waveform_stem_band), as the playhead's stem does. Across the well's
// top lines it stands between its FLANKS (architect 2026-10-05), painted
// before it by paint_marker_stem_flanks, so a stem wins over a neighbour's.
//
// Z-ORDER (architect 2026-09-23): the stems paint UNDER the playhead's stem,
// which follows this pass, and under the flag boxes, so a dense run of stems at
// a coarse zoom never hides a playhead standing near them. Where the playhead's
// column IS a marker's, the marker's stem wins the column by the ruling's other
// half: the playhead's stem does not paint there (playhead_stem_suppressed).
// The full sequence is the paint-order block in on_redraw.
void GuiPaintHandler::paint_marker_stems(cairo_t* cr, const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    if (app.marker_stems.empty()) return;

    // THE STEMS PAINT AS PUBLISHED (architect 2026-10-03): an open editor's
    // refused commit recolours nothing, so no paint-time override stands
    // here — the stash's colour is the marker's resolved stem, the whole
    // answer.
    cairo_save(cr);
    const GuiRect band = waveform_stem_band(area);
    const double y0 = static_cast<double>(band.y);
    const double y1 = static_cast<double>(band.y + band.h);
    for (const MarkerStem& stem : app.marker_stems) {
        // Column-gate exactly like render_playhead's line does. The producers
        // already publish only columns in [0, w) (stem_column_on_waveform,
        // render.cpp); fill_waveform_line (render.h) restates that gate
        // against the area this painter is handed, so no entry can leak its
        // column into the chrome beside the waveform, and clips the line's
        // waveform_line_px() width at the right edge.
        const int col = static_cast<int>(std::nearbyint(
            stem.x - static_cast<double>(area.x)));
        set_palette_source(cr, stem.color);
        fill_waveform_line(cr, area.x, area.w, col, y0, y1);
    }
    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_marker_stem_flanks ---------------------------

// THE STEMS' FLANKS (the contract at the declaration; the rule at
// fill_stem_flanks, render.h): the same stash, the same column derivation
// as paint_marker_stems, so a flank can never stand a pixel away from its
// stem. DAMAGE is the stems' own: a flank lives and dies with its stem's
// entry and sits inside the waveform area, which every stash change damages
// whole (the flag cache's rebuild damages the waveform with the strip,
// maybe_rebuild_flag_cache); a narrow playhead damage repaints the flanks
// under its clip like every other pass. Nothing is cached in the well's rows.
void GuiPaintHandler::paint_marker_stem_flanks(cairo_t* cr,
                                               const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    if (app.marker_stems.empty()) return;
    const GuiRect band = waveform_well_top_band(area);
    if (band.h <= 0) return;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    set_palette_source(cr, palette().dk_shadow);
    const double y0 = static_cast<double>(band.y);
    const double y1 = static_cast<double>(band.y + band.h);
    for (const MarkerStem& stem : app.marker_stems) {
        const int col = static_cast<int>(std::nearbyint(
            stem.x - static_cast<double>(area.x)));
        fill_stem_flanks(cr, area.x, area.w, col, y0, y1);
    }
    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_strip_drag_anchor ----------------------------

// Paints the anchor stem (the Ableton pivot affordance) at the zoom anchor's
// current column, full waveform height — a live zoom gesture's, or the S
// Pen's retained one between strokes. TWO PRODUCERS, ONE STEM:
//   * THE ONE NAV DRAG'S ZOOM PHASE (scroll_drag while `zooming` — from a
//     ctrl-armed press, or from a ctrl-down edge mid-drag, and gone again at
//     the ctrl-up edge; the mode's contract is at ScrollDragState);
//   * THE TOUCH ZOOM (touch_nav_zoom.seated — the contract is at
//     TouchNavZoomState, app_state.h), added so THE TWO SURFACES SHOW THE SAME
//     AFFORDANCE (architect 2026-08-14, from the rig, asking to SEE the glass
//     gesture: "add a zoom stem to the zoom on the touchpad just so I can see
//     exactly what's going on, because at the edges there are some
//     strangeness, it seems like") — the record it reads is one seat shared
//     by the TWO-FINGER PINCH and, since 2026-09-25, the ONE-FINGER CTRL ZOOM
//     (`TouchNavZoomState::one_finger`), so the stem has a third producer
//     riding the second's record; AND THE SEAT'S THIRD POSTURE, THE PEN'S
//     RETAINED ANCHOR (`TouchNavZoomState::retained`, architect 2026-09-27):
//     a one-finger seat the pen lifted from with its side button held
//     outlives the gesture, so the stem stays painted between strokes, at
//     the anchor's live column, until the platform's release hook or a
//     view-state clear takes the seat.
// The gate is the gesture record and nothing else since 2026-08-05
// (architect), so THE PRESS ITSELF SHOWS THE PIVOT — the headless zoom stem —
// rather than the stem appearing only once the drag crosses the slack. The
// mouse arm and the mode-switch edges owe the frame's damage
// (arm_nav_zoom_press / the mode sync in on_motion); it
// vanishes the moment its gate drops (release / button loss / the force-end
// finalizer / the ctrl-up switch, each spelling its own damage; Esc no longer
// ends a gesture at all) — on the glass, at the seat's clear, which a
// retained pen anchor defers past the pen's lift. The stem is the ZOOM PIVOT and nothing more — the
// playhead jump that briefly rode the strip drag was rolled back 2026-08-06
// and the stem is what survives it.
// THE PINCH OWES ITS DAMAGE AT BOTH ENDS, exactly as the other producer
// does, and NEITHER END IS FREE — a seating frame is not an applied frame at all
// in the general case (the seat is taken above the gesture's exact-no-op
// return, so two fingers landing and sliding together seat and apply nothing;
// the ordering rule is at apply_touch_nav_update's seat), and even a seating
// frame that DOES apply can be dropped at the walls by
// apply_strip_drag_zoom's mid-gesture true-no-op return, which discards any
// frame whose post-clamp level and viewport both stand (a pinch that begins
// saturated is exactly that frame). So the SEAT damages at its own
// site, the mouse arms' rule, and the CLEAR damages through
// clear_touch_zoom_seat, because a clear can land on a frame that applies
// nothing at all (a survivor's pan refused off the wheel's surfaces).
// BOTH PRODUCERS ANCHOR THE SAME WAY AND ALWAYS DID (all three did, while
// there were three), which is why there
// is ONE expression (the nav drag's pivot went back to a SONG position
// 2026-08-14 — the clamped-zoom reversibility ruling, contract at
// ScrollDragState — and the pinch seats a song frame for the same reason): the
// anchor column is recomputed each frame from the persisted anchor_sample
// against the DISPLAYED viewport (wf_cache.fp_*), the same basis
// paint_playheads uses, so the stem stays locked to the
// blitted plate while the worker rebuilds, and the anchor lives in the active
// display domain (viewport_start + col*spp) so no warp map is walked.
// Re-projecting is what makes the stem SLIDE WITH ITS CONTENT when a clamped
// zoom keeps moving the song under it — which is now visible ON GLASS exactly
// as it is under the mouse, the ruling made watchable and the reason the
// architect asked for the stem there.
// strip_anchor_stem_column clamps the column to the visible edges (and
// render_strip_anchor_stem's own clamp agrees) — an edge-pinned anchor draws
// the clamp itself.
// PRECEDENCE IS DECLARED RATHER THAN LEFT TO THE EXPRESSION'S SHAPE: a held
// mouse capture and a glass contact are not structurally impossible together,
// so the CAPTURING gesture goes first — the nav drag's zoom phase, then the
// pinch — a capture owning the pointer for its whole life and being the more
// committed act. (With two producers the selection is a ternary again; it was
// an if/else chain while there were three, and the rule it expresses is the
// same either way.) One stem is painted either way.
void GuiPaintHandler::paint_strip_drag_anchor(cairo_t* cr, const GuiRect& area) {
    const bool nav_zoom    = app.scroll_drag.active && app.scroll_drag.zooming;
    const bool touch_pinch = app.touch_nav_zoom.seated;
    if (!nav_zoom && !touch_pinch) return;
    if (area.w <= 0 || area.h <= 0) return;

    const PlateViewportBasis basis = plate_viewport_basis();
    if (basis.spp <= 0.0) return;

    const double anchor_sample = nav_zoom ? app.scroll_drag.anchor_sample
                                          : app.touch_nav_zoom.anchor_sample;

    // The stem's one column derivation (strip_anchor_stem_column over
    // displayed_column_at, warp_frame_map_view.h), on the PLATE basis.
    const int col = strip_anchor_stem_column(anchor_sample, basis.vp_start,
                                             basis.spp, area.w);
    render_strip_anchor_stem(cr, area, col);
}

// -- GuiPaintHandler::playhead_stem_suppressed ---------------------------

// THE PLAYHEAD'S STEM SUPPRESSES WHERE A MARKER'S STEM ALREADY STANDS
// (architect 2026-08-01). SINCE 2026-09-23 THIS IS HALF OF THE Z-ORDER RULING
// ITSELF, not a side effect of paint order: the playhead's stem paints ABOVE
// every marker stem and below every flag box (so a dense run of markers at a
// coarse zoom cannot hide it), and WHEN THE PLAYHEAD'S COLUMN IS A MARKER'S,
// THE MARKER STEM WINS — this predicate is that half. With the playhead now
// painting after the stems, it is also the only thing that keeps a coincident
// marker's stem (brightened when selected) on show. This REINSTATES 035e669's model — "the cursor playhead
// is conceptually COINCIDENT with the selection and fully hidden behind the
// marker — line on the stem, triangle behind the flag; suppression as
// implementation, not absence" — which the 2026-07-30 always-paints ruling
// deleted. It is the coincident case ALONE that the always-paints clause loses:
// the playhead still paints everywhere else, unconditionally, and the HEAD
// paints even here (on the ruler's bottom rows since 2026-09-23, just above
// the coincident flag, so it stays whole; a ±1 column is invisible against the HEAD, whose widest rows are
// 9 * waveform_line_px() (the glyph's own 9 columns, kPlayheadHeadGlyph,
// render.h) — 9px at the 50% floor, 9 at 100%, 36 at 350% and 90 at the
// 1000% ceiling, so it is at least nine columns wide anywhere in the schema
// and the ±1 never approaches half
// of it. That is
// exactly what a stem beside another stem is not, at any scale: a stem is
// waveform_line_px() wide (render.h; 1 column on the laptop, 3 on the tablet —
// the line scales with gui_scale since 2026-09-27), so there the same ±1 is
// half the object or more, and a t-wide playhead one column left of a
// marker, painting over the stems, covers t − 1 of that stem's t columns).
// The suppression covers the WHOLE stem, its marker-lane run included (read by
// paint_ruler_row as well as paint_playheads): in that lane the coincident
// marker's flag fills the column, and a white run a column beside its left
// edge would be the same ±1 split.
//
// WHY IT IS PRINCIPLED AGAIN, and why it was not on 2026-07-30: in the OLD
// visual model only a selected SINGLETON stemmed, so suppressing the playhead
// over an unstemmed marker would have left the column blank — absence, not
// hiding. Row 5 gives EVERY ENABLED marker an always-on stem, so a coincident
// marker's own stem is a real, always-present line for the playhead to hide
// behind, and the deleted model becomes true again.
//
// WHAT IT FIXES: the two stems are derived through DIFFERENT column arithmetic
// — marker stems publish from the flag-cache rebuild (the flag layout's own
// column resolution; its spp rides the plate's published width since the
// 2026-08-01 resize-window fix, see waveform_cache.cpp's wave_w read), the
// playhead from playhead_pixel_x against plate_viewport_basis — so at some
// zoom rests a marker and a playhead standing on the SAME frame round to columns
// one pixel apart, and nudging or dragging the marker made the pair flicker
// between one line and two. Suppression removes the second line rather than
// trying to make two roundings agree.
// A TRACE (2026-09-26) FOUND THAT PREMISE STALE since the very 2026-08-01 fix
// cited above: maybe_rebuild_flag_cache (waveform_cache.cpp) and
// plate_viewport_basis now read the SAME wf_cache.fp_* fields — fp_vp_start,
// fp_vp_end and fp_area_w for the spp both use, fp_warp_frame_map for the
// target view's frame mapping — and a coincident marker's paint sample equals
// the cursor, so both columns come from the identical expression; no ±1 can
// arise between them at rest. The only remaining divergence is a transient
// plate-map lag in target view: playhead_cursor_sample is stamped into the
// active domain at the moment it is set (through the LIVE warp frame map),
// while a target-view flag's column crosses wf_cache.fp_warp_frame_map, the
// worker-published snapshot — so a warp edit whose plate rebuild (resize,
// load) has not yet landed can still show the old one-pixel split for the
// span the rebuild covers. The suppression stands as the ruled z-order (the
// marker's stem wins its column), not as a rounding repair.
//
// A STATE COMPARE, NEVER A PIXEL ONE: the qualifying test is the LAND's own
// exact-int64 formula — clamp_playhead_to_live_domain(source_frame_to_active_-
// domain(time_frame)) == playhead_cursor_sample — reused verbatim from
// auto_select_marker_at_playhead (input_pointer.cpp), which owns the coincidence
// family's question "is the playhead standing on a marker". Comparing columns
// instead would ask the two roundings to agree, which is the defect.
//
// THE WALK IS OVER THE PAINTED STEMS (app.marker_stems), not over a store, and
// that is what makes "a stem is standing there" the literal predicate: the stash
// holds one entry per ENABLED, VISIBLE marker (a disabled marker publishes none,
// a culled one publishes none), so a marker with no stem can never suppress the
// playhead's — the blank-column failure mode is structurally unreachable rather
// than argued. It is also bounded by the visible marker count.
//
// TWO WAYS A STEM QUALIFIES:
//   * THE DRAG RIDE. While a marker drag tows the playhead (apply_drag_motion
//     writes the cursor to the proposal's own active-domain position every
//     motion event, and commit_drag lands it on the committed frame), the
//     dragged marker's stem and the playhead ARE one object by construction —
//     but mid-motion the proposal is a fractional double and the store still
//     holds the pre-drag frame, so the exact compare below cannot see it. The
//     drag's own fact is what the arm reads instead, and it is the same fact the
//     overlay paints the flag and the stash publishes the stem with: the
//     marker's index appearing in the DragOverlay.
//   * EXACT COINCIDENCE AT REST, the compare above — which is what the keyboard
//     nudge leaves behind (the nudges re-land the playhead through
//     land_playhead_on_marker, whose write IS this formula, so a nudged marker
//     rests exactly coincident) and what every marker click, Tab jump and
//     coincidence auto-select leave behind too.
//
// IN THE HISTORY VIEW (`h`) THE PLAYHEAD YIELDS TO A COINCIDENT DIFF STEM
// exactly as it yields to a marker's stem live (architect 2026-09-27). There
// the stash is the diff lane's (render_history_diff_flags), its marker_index
// an index into app.history_mode.flags, so that arm qualifies a stem by its
// diff flag's own time_frame — an authored SOURCE frame, which the lane maps
// through the same warp frame map a live marker's takes — under the same
// land formula, which is the one the mode's own lands run
// (land_playhead_on_source_frame). Index-guarded against the flags vector; it
// has no drag arm, the mode consuming every authoring gesture.
//
// SCOPE NOTE, deliberately WIDER than "the focused marker": any marker with a
// painted stem suppresses, focused or not. The artifact is the same ±1 wherever
// the playhead stands on a marker, the display is that marker's stem either way,
// and reading the FOCUS here would make a waveform pixel depend on the
// SELECTION — the exact dependency row 5 deleted Selection::stem_subject /
// damage_stem_on_subject_change for (selection.cpp), whose mutators damage the
// top strip and not the waveform. Keyed on the playhead and the stash instead,
// every input this reads is already damaged by its own writer.
bool GuiPaintHandler::playhead_stem_suppressed() const {
    if (app.marker_stems.empty()) return false;

    // THE LAND'S OWN FORMULA, the one coincidence test both arms below call.
    const auto coincident = [&](int64_t source_frame) {
        return clamp_playhead_to_live_domain(
                   source_frame_to_active_domain(app, audio, source_frame),
                   app, audio) == app.playhead_cursor_sample;
    };

    // THE HISTORY ARM. The stash's index domain follows its painter
    // (AppState::marker_stems): in the mode it indexes history_mode.flags, so
    // the stem qualifies by its DIFF FLAG's frame and never by a live store
    // row. No drag arm: the mode consumes every authoring gesture, so no
    // marker drag tows the playhead here.
    if (app.history_mode.active) {
        const int n = static_cast<int>(app.history_mode.flags.size());
        for (const MarkerStem& stem : app.marker_stems) {
            const int i = stem.marker_index;
            if (i < 0 || i >= n) continue;
            if (coincident(app.history_mode.flags[
                    static_cast<std::size_t>(i)].time_frame))
                return true;
        }
        return false;
    }

    // The dragged marker, or -1. The view compare is a statement, not a repair:
    // the drag-modal gate swallows `p`, so a live drag's mode is always the
    // active column — the stash indices this compares against are that column's.
    const int dragged =
        (app.drag.active && app.drag.drag_mode == app.active_markers_view &&
         !app.drag.dragging_markers.empty())
            ? app.drag.dragging_markers[0]
            : -1;

    // The stash is the ACTIVE column's (both columns publish one), so the
    // store is the active one through its selector pair (active_marker_count /
    // active_marker_time_frame, app_state.h).
    const int n = active_marker_count(app);
    for (const MarkerStem& stem : app.marker_stems) {
        const int i = stem.marker_index;
        if (i == dragged) return true;
        if (i < 0) continue;
        // Index-guarded against the store the stash was published from having
        // shrunk since (an undo under a stale stash): a missing row simply does
        // not suppress.
        if (i < n && coincident(active_marker_time_frame(app, i))) return true;
    }
    return false;
}

// -- GuiPaintHandler::paint_playheads ------------------------------------

void GuiPaintHandler::paint_playheads(cairo_t* cr, const GuiRect& area) {
    // Use the displayed viewport AND its samples-per-pixel
    // (wf_cache.fp_vp_start, derived spp) so the cursor stays in
    // lockstep with the cached waveform / stem / flag layers during
    // the 1-2 paint frames while the worker rebuilds against a
    // viewport change. See declaration comment in app_state.h.
    const PlateViewportBasis basis = plate_viewport_basis();
    const double disp_spp = basis.spp;
    const double px_x = playhead_pixel_x(app, wf_cache.fp_vp_start, disp_spp);
    // ROW 5 RETIRED THE TRIANGLE and this pass draws NOTHING in a strip lane
    // any more: the tip-down triangle died with its lane, and its successor —
    // the aliased head on the ruler lane's bottom rows and the column's run
    // through the marker lane — is paint_ruler_row's (the ruling is at that
    // block). So this pass is the WAVEFORM segment of the cursor's
    // stem, nothing else — and since 2026-08-02 render_playhead draws a line and
    // only a line: the dead triangle branch is deleted, and with it the lane
    // rect this call used to thread through to it.

    // The cursor paints UNDER the marker flags (the Z-ORDER FLIP, architect
    // 2026-07-23 — see the paint-order block in on_redraw): its line passes
    // beneath a marker flag sharing its column, so a cursor resting on a marker
    // sits hidden behind that marker's flag. In the waveform it paints OVER the
    // marker STEMS (architect 2026-09-23), invoked after paint_marker_stems, so
    // a dense run of stems at a coarse zoom cannot hide it; where its column is
    // a marker's, the marker's stem wins (the suppression below). Gated on the waveform OR the top strip
    // being exposed: the cursor's HEAD and marker-lane run live in the strip
    // (paint_ruler_row) and this stem in the waveform, and the two halves of one line repaint
    // together whatever the damage shape — the outer Cairo clip bounds the
    // actual work.
    //
    // THE SCANNER LEFT THIS PASS (architect 2026-08-01) — it is paint_scanner
    // now, invoked after this one, so the moving line crosses a marker's stem
    // instead of blinking out behind it. This pass is the CURSOR alone: the
    // scanner is over everything in the waveform area while it runs, this
    // cursor included.

    // THE CURSOR PLAYHEAD ALWAYS PAINTS (architect 2026-07-30): ONE playhead
    // form, drawn at the resting cursor column whatever the selection is
    // doing — a waveform_line_px()-wide line (render.h)
    // painted solid straight over the plate ink. WITH ONE EXCEPTION SINCE 2026-08-01, and exactly one: where a
    // MARKER'S stem already stands on the playhead's frame, the playhead's STEM
    // does not paint and that marker's stem is the display (035e669's
    // hidden-behind-the-marker model, reinstated — the whole ruling is at
    // playhead_stem_suppressed; the marker-lane run obeys it too). The clause
    // above still holds everywhere else, and the HEAD paints in the suppressed
    // case too (paint_ruler_row).
    //
    // The three-way chain that used to live here is gone with the SPAN FORM: the
    // region is no longer a playhead at all (it IS THE TRIM, painted on the
    // trim bar alone), so it hides
    // nothing and suppresses nothing, and the split half-triangle renderer is
    // deleted outright. The non-empty-selection suppression is
    // gone too: a cursor resting ON the focused marker is simply hidden behind
    // that marker's flag by the z-order flip, which is what the old else-arm was
    // spelling out by not painting — and when the arrows move the focused marker
    // the cursor rides along VISIBLY, which is the lane model's honest reading.
    // THE TRIANGLE IS OFF EVERYWHERE (row 5): the cursor's tip-down triangle
    // retired with the triangle lane, and its successor is the ruler pass's.
    // So this call is the stem's WAVEFORM segment; the ruler pass draws the
    // head on the ruler's bottom rows and the column's run through the marker
    // lane down to the waveform top, where this segment begins, and the three
    // make one unbroken object.
    // THE STEM IS THE `playhead_stem` KEY, UNIFORM FROM THE HEAD TO THE
    // CANVAS'S FOOT (architect 2026-10-03; its contrast over the chrome and the
    // canvas is the user's choice): the head above it is the playhead's
    // identity, and the stem is that head's line continued down through the
    // waveform.
    //
    // Z-INTENT (architect 2026-09-23): this segment goes down OVER the marker
    // stems painted before it and UNDER the flag boxes blitted after it, and the
    // marker-lane run above it (the ruler pass) likewise goes under the flags.
    // At a coincident column there is no overlap to order: the stem yields whole
    // to the marker's (playhead_stem_suppressed), the ruling's other half. The
    // flag half is the HIDDEN-BY-MARKER model translated — a flag sharing the
    // cursor's column hides it, exactly as flags painted over the old triangle.
    // The stem runs OVER the well's top lines and stops at its bottom ones
    // (PlayheadRows::Stem, waveform_stem_band, architect 2026-10-02): it is a
    // boundary line continuing from the lane above, like the marker stems
    // beside it.
    if (!playhead_stem_suppressed()) {
        render_playhead(cr, area, px_x, palette().playhead_stem,
                        PlayheadRows::Stem);
    }
}

// -- GuiPaintHandler::paint_scanner --------------------------------------

// THE SCANNER PASSES OVER THE MARKER STEMS (architect 2026-08-01, at the row-6
// live playback look). It was drawn inside paint_playheads, which then ran
// BEFORE paint_marker_stems, so every always-on marker stem overpainted the
// moving line's column: at our marker density the scanner blinked out
// repeatedly as it crossed the song. Its own pass, invoked after the stems, was
// the fix. The resting cursor followed it above the stems on 2026-09-23 (the
// ruling is at the paint-order block in on_redraw) and paints just before this
// pass, still under the flags.
//
// SO THE SCANNER IS TOPMOST IN THE WAVEFORM AREA while it runs — over the
// stems, over the cursor where they overlap, over the plate. Everything it
// covers is a per-frame repaint anyway.
//
// THE SCANNER IS THE MOVING STEM, and it HAS ITS OWN ROLE, `scanner`
// (architect 2026-10-05, undoing 2026-10-03's choice to share the resting
// cursor's `playhead_stem` key — a yellow scanner reads apart from a white
// resting stem at a glance).
//
// It stays WAVEFORM-ONLY: no head, no lane presence, nothing in the top strip
// (the ruling is at paint_ruler_row's head block — render_playhead is shared
// with the cursor and, since 2026-08-02, cannot reach a strip lane at all: it
// draws the line inside `area` and nothing else).
// Same displayed-plate basis the cursor uses, so both ride the blitted pixels
// through a worker rebuild; the value fields it reads are meaningful only while
// active, which is exactly what the gate asks.
void GuiPaintHandler::paint_scanner(cairo_t* cr, const GuiRect& area) {
    if (!app.playhead_scanner_active) return;

    const PlateViewportBasis basis = plate_viewport_basis();
    const double scan_px =
        scanner_pixel_x(app, wf_cache.fp_vp_start, basis.spp);
    render_playhead(cr, area, scan_px, palette().scanner,
                    PlayheadRows::Canvas);
}

// -- THE BOTTOM ROW'S MODAL STATE ----------------------------------------
//
// THE MODAL LIVES ON THE BOTTOM ROW (architect 2026-08-13, scrapping the
// centered box of 2026-08-12: "it looks sloppy — no compositor drop shadow,
// and faking one wouldn't work"). NOTHING ABOUT THE RENDERER MOVED — one
// window, one surface, one frame, one painter, exactly as before; only the
// modal's RECTANGLE moved from the window's centre onto this row, so this is
// emphatically not the scrapped second-toplevel model (closed_questions.md
// carries that do-not-re-propose). WHILE A PROMPT OR A DIALOG EDITOR STANDS THE ROW
// YIELDS WHOLE: all SEVENTEEN buttons — the VERB GROUP'S SIX, the marker
// walk group's four, the four arrows and the transport three — plus the clock's
// time field and the state line beside it stand down, nothing negotiates for
// space, and paint_modal_dialog paints the modal into the lane they left.
//
// The two helpers below are that fork, shared by the row's painter (which
// wants the boolean, just under here) and by paint_modal_dialog (which wants
// the editor itself, at the tail of this file) — one answer, so the row
// cannot yield to a dialog the modal painter would then decline to paint.

// The dialog editor the modal would paint, or nullptr, with its LABEL out.
// The editors' precedence order. Only one dialog editor can be open at a time
// — every opener refuses while another owns the keyboard — so the order is
// free. A standing PROMPT outranks every editor and is the caller's own test,
// not this one's. (The `h` view's load editor and the Open project editor
// had arms here until 2026-08-28, when both became the field-less PICKER —
// a modal owner of its own, painted by the picker branch below, never an
// editor.)
//
// IT HANDS BACK A MUTABLE STATE because the field's painter WRITES one field
// of it: the horizontal view offset (text_editor::State::view_offset_px, whose
// minimal-travel rule the painter owns for the flag editor too). The boolean
// caller below wants only the null test and is unaffected.
static text_editor::State* dialog_editor_to_paint(AppState& app,
                                                  std::string& prefix) {
    if (text_editor::is_active(app.commit_title_editor)) {
        prefix = kCommitTitleEditorPrefix;
        return &app.commit_title_editor;
    }
    if (text_editor::is_active(app.settings_editor)) {
        prefix = kSettingsEditorPrefix;
        return &app.settings_editor;
    }
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        prefix = kBpmEditorPrefix;
        return &app.top_flag_editor;
    }
    return nullptr;
}

// "A modal owns the bottom row" — the prompt, the RENDER PLAYER (the third
// owner since 2026-08-28: its transport row takes the lane whole), the PICKER
// (the fourth, the same day: its Cancel-alone row) or any dialog editor. The
// top-strip FLAG editor is deliberately absent: it is positional and
// pointer-transparent, not a dialog, and it never takes this row.
static bool modal_owns_bottom_row(AppState& app) {
    if (app.prompt.active) return true;
    if (app.render_player.active) return true;
    if (app.picker.active) return true;
    std::string prefix;
    return dialog_editor_to_paint(app, prefix) != nullptr;
}

// -- GuiPaintHandler::paint_bottom_strip ---------------------------------

void GuiPaintHandler::paint_bottom_strip(cairo_t* cr) {
    // THE UNIFIED BOTTOM ROW (architect-ruled 2026-08-12, rows 8 and 9 merged
    // into ONE lane on the WINDOW'S FOOT): the monospace clock in its status
    // panel and THE STATE LINE on the ground beside it at the left pad, and —
    // flush right — the MARKER-VERB GROUP, the marker walk, the four cardinal
    // arrows and the transport three, eight Windows px of bare ground between
    // two groups (architect 2026-09-29 for the groups, 2026-10-02 for the
    // bare gaps; the four tables above own those memberships). That is the
    // whole roster. This painter
    // owns the lane's CHROME (its ground) and nothing else; the
    // buttons and the clock are paint_bottom_row_buttons_and_clock, called
    // from here onto the grounded band (the cluster's tables and the clock's
    // metrics live beside that body).
    // THE ROW HAS A MODAL STATE since 2026-08-13, in which those tenants
    // stand down and the lane carries the prompt or the dialog
    // editor instead (the ruling and the fork are at modal_owns_bottom_row,
    // just above; the modal's own layout is paint_modal_dialog's) — and with
    // the chain gone there is nothing left on the lane to negotiate with,
    // which is what makes that yield clean.
    //
    // WHAT IS NOT HERE, and why it is not missing: an S/T · W/P view readout
    // and a "(read-only)" token. The icon row's view group shows the view as
    // a lit button and its read-only toggle shows the lock, so letters would
    // restate what that row says in its own vocabulary. THE A / B LETTER IS
    // HERE, though, since 2026-10-01: the tab row that showed it is deleted,
    // and the letter leads the clock cell (paint_bottom_row_buttons_and_clock
    // owns the rule). The row carries no dirty mark: Save's grey is the mark
    // (architect 2026-10-05, plain_save_actionable).
    //
    // The row paints on EVERY frame class (loading, blank, loaded) like the
    // redesigned rows above it: the clock reads 00:00.000 with no source, and
    // the buttons must be visible on every frame class their press claim is
    // live on.
    const GuiRect lane    = bottom_row_area(app);
    const GuiRect content = bottom_row_content_area(app);
    if (lane.w <= 0 || lane.h <= 0 || content.h <= 0) return;

    // THE ROW'S GROUND, the whole lane — its top row included, which carries
    // NO LINE since 2026-10-02 (architect: nothing between the well and this
    // row; the well's own bottom line is the seam). The ground erases
    // whatever render_background laid down, so the strip does not depend on
    // that erase happening to hold the same value. The modal paints on this
    // ground and lays none of its own (paint_modal_dialog).
    {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        paint_cell_rect(cr, lane, palette().ground);
        cairo_restore(cr);
    }

    // THE ROW YIELDS TO THE MODAL (2026-08-13; the fork and the ruling are at
    // modal_owns_bottom_row just above). The ground above is the ROW'S
    // whatever it carries — the modal paints on the row's own
    // ground, having no box of its own — and every other tenant stands down
    // here: paint_modal_dialog owns the lane from this frame until the
    // dialog's closer.
    //
    // THE ROW'S BUTTONS PUBLISH ZERO RECTS rather than stranding the last
    // frame's (the roster's own model — a zero/invalid stash contains no
    // point), so nothing can hit an unpainted button and no consumer of those
    // rects can read a phantom bound. Their THREE FACE BITS ARE
    // still published: those are read by main.cpp's staleness comparator
    // alone, and a stash frozen for the modal's whole life would drift the
    // moment the open changed one — a modal open STOPS PLAYBACK, which flips
    // the transport button's GLYPH (the same edge flipped that button's
    // ENABLED bit while the row was honest, and the play/stop pair's LAMP
    // while it was a radio; the state has moved axis twice and the example
    // holds on each) — leaving the comparator to
    // invalidate this row on every tick with nothing to repaint but the modal.
    //
    // THE CLOCK'S CELL ZEROES WITH THEM, which is what makes the row's one
    // rect owner degrade HONESTLY: a zero cell answers the WHOLE LANE at
    // clock_invalidate_rect (main.cpp), which is exactly the modal's surface.
    // NOTHING READS THESE ZEROES as a bound — since 2026-08-13 nothing on
    // this row is measured from a button stash at all, the status chain having
    // taken its right anchor to the tab row with it.
    if (modal_owns_bottom_row(app)) {
        for (const TransportRowDef& def : kTransportGroup) {
            publish_button_face(cr, app, audio, playback, target_render,
                                def.id,
                                GuiRect{0, 0, 0, 0});
        }
        // The right block's other three groups stand down with the transport
        // — the MARKER VERBS, the MARKER-WALK GROUP and the four ARROWS. Each
        // is a tenant of this lane like every other member, and the modal
        // takes the lane whole: four tables, every button on the row.
        for (const TransportRowDef& def : kMarkerVerbGroup) {
            publish_button_face(cr, app, audio, playback, target_render,
                                def.id,
                                GuiRect{0, 0, 0, 0});
        }
        for (const TransportRowDef& def : kTransportWalkGroup) {
            publish_button_face(cr, app, audio, playback, target_render,
                                def.id,
                                GuiRect{0, 0, 0, 0});
        }
        for (const TransportRowDef& def : kTransportArrowGroup) {
            publish_button_face(cr, app, audio, playback, target_render,
                                def.id,
                                GuiRect{0, 0, 0, 0});
        }
        app.clock_cell_rect = GuiRect{0, 0, 0, 0};
        return;
    }

    // THE BUTTON CLUSTER AND THE CLOCK — publishes the clock's reserved cell
    // (AppState::clock_cell_rect), which the row's one remaining rect owner
    // reads (clock_invalidate_rect, main.cpp). Runs whole on every exposure
    // like the top button rows (it shapes no text beyond the clock's one
    // memoised-cell run, so a narrow clip pays almost nothing for it) and is
    // the LAST thing this painter does: with the status chain gone the row has
    // no other tenant.
    //
    // (THE STATUS CHAIN AND ITS PER-CELL EXPOSURE GATE WERE HERE UNTIL
    // 2026-08-13, when the architect moved the chain into the TAB ROW. Deleted
    // with it, all of them producer-less once it left: the gate itself — a
    // cairo clip-extents test against the clock cell's right edge, whose ONE
    // purpose was to keep the per-frame clock damage from paying for the
    // chain's HarfBuzz shaping, and there is no shaping left on this row to
    // pay for; the span arithmetic between that cell and the right block's
    // left edge, with its degenerate-span early return; and the read of the
    // painter's own TransportLeft stash as the chain's right anchor. The chain
    // itself was deleted on 2026-08-29 and its three STATE strings are the
    // STATE CELL right of the clock since 2026-08-29's evening fold — the
    // STATE LINE on the row's ground since 2026-10-03 — painted by
    // paint_bottom_row_buttons_and_clock: one string, not a ladder, and the
    // resolved readout retired with the one-day bar that carried it.)
    paint_bottom_row_buttons_and_clock(cr);
}

// -- GuiPaintHandler::paint_modal_dialog ---------------------------------
//
// THE MODAL SURFACE — THE BOTTOM ROW (architect 2026-08-13, scrapping the
// centered box he ratified the day before: "it looks sloppy — no compositor
// drop shadow, and faking one wouldn't work"). It hosts the PROMPTS and the
// THREE modal editors (settings / commit-title / BPM); the top-strip
// FLAG editor is deliberately NOT a dialog — it is positional, editing the
// marker where it stands, and stays the pointer-transparent unrolled flag
// (render_flag_editor_box).
//
// ONE RENDERER, ONE WINDOW, ONE FRAME — unchanged. Nothing here creates a
// surface, a window or a second buffer: only the modal's RECTANGLE moved,
// from the window's centre to the bottom row, so this is NOT the scrapped
// second-toplevel model (that attempt was reverted byte-exact and is recorded
// do-not-re-propose in closed_questions.md). The row yields whole while a modal
// stands — modal_owns_bottom_row, above, is the shared fork, and this body
// paints into the lane the row's tenants left.
//
// CONTENT PER KIND, on the row's own ground (there is no box, no frame and no
// window margin), EVERYTHING FLUSH LEFT since the architect's live look later
// on 2026-08-13 ("your eyes have to go the whole distance of the screen") —
// the layout rule and what gives when the row runs out of width are at the
// layout block below:
//   A PROMPT — the message at the row's left pad, one pad, then the answer
//   buttons in painted order. Each button wears its response's PLAIN WORD
//   ("Save", "Discard", "Cancel", "Retry", "Yes", "OK" on the load
//   confirmation)
//   and names its key on its TOOLTIP instead — the bracketed accelerators are
//   retired for the second time and with their reason recorded at PromptState,
//   which owns the label rule; the codepoint-exact lowercase match is
//   untouched, so a typed capital still does not answer. ONE BUTTON WEARS THE
//   PASSIVE FOCUS FACE FROM THE RAISE (2026-08-13, superseding this block's
//   "no default face: this prompt system has no Enter answer, so every button
//   is plain") — the LAST, the Escape sentinel, on every prompt but THE FOUR
//   CONFIRMATION RAISES — the load's two, File → Revert's and, since
//   2026-09-16, the phase reset paste's — which are raised
//   on their FIRST (PromptState's PromptInitialFocus owns the choice); Enter answers whichever it is, the
//   assignment site is a few dozen lines into the body below and the whole
//   supersession is at PromptState.
//   AN EDITOR — its prefix as the LABEL at the left pad, then the pending
//   buffer in a SUNKEN FIELD on the theme's field pair, then OK and Cancel.
//   The field is the existing text_editor machinery — selection, caret,
//   click-to-caret, byte-identical editing — and a refused Enter selects the
//   whole text, the edge unchanged, its owner's card saying why (architect
//   2026-10-03; render.h's palette block, A REFUSED ENTER RECOLOURS NOTHING).
//
// EVERY BUTTON CARRIES A TOOLTIP (architect 2026-08-13: "we just do a tooltip
// just like the regular icon tooltips"), through the roster's own machinery
// end to end — the same Qt timing, the same box, the same painter, the same
// AppState::redesign_tooltip state, whose owner names either surface now. The
// TEXT is composed per button from the word it wears plus the key it
// dispatches (modal_dialog_button_hint, app_state.h) and published in the
// stash beside the rect; the WAIT is written by this surface's own hover walk
// (update_modal_dialog_hover), and as everywhere no wait starts under a held
// press (the model is at AppState::RedesignTooltip).
//
// THE BUTTONS ARE PUSH BUTTONS (architect 2026-10-02, the Windows-95 design):
// the deleted toolbar row's box (row 2's height and its own label pads, in
// Windows px — these buttons carry words or glyphs) painted by the one button
// painter the roster uses (paint_button_box) in its PUSH family: PLAIN RAISED
// at rest, a checked button PLAIN SUNKEN over the dither, a live press PLAIN
// SUNKEN on the ground with its glyph or label one Windows px down and right,
// and the keyboard's FOCUS the Windows default-button frame — one DkShadow
// line outside the box. No hover face.
//
// AND THEY ACT AT THE RELEASE (the same ruling — "everything else acts on
// lift"), which is what makes the click face real: a press ARMS the button
// and paints it, the lift on that same button runs the act, and sliding off
// or lifting elsewhere cancels with nothing dispatched. The arm is
// AppState::modal_dialog_pressed — deliberately not the roster's own arm
// (AppState::ChromePress), which carries the same act-at-release lifetime
// since the chrome conversion but a different index space (the reasoning is
// at the field's declaration).
//
// METRICS. The row's own pad (icon_row_pad_x, paint_handler.h — the icon row's
// 8, which the bottom row's tenants have always walked from and which the
// modal took over on 2026-08-14, retiring the separately-measured 13 it had
// inherited from the status chain) is the left and right margin,
// which is what makes the modal sit on the same margins as the tenants it
// displaced; the rest are the surviving sampled constants, sampled in laptop
// px and each re-authored in Windows px at the block below (the field colors
// and their derivations are at the kModal* block, render.h):
//   kModalButtonGapPx 8  — modal_popup.png's inter-button gap (Save ends
//                          x=504, Do Not Save begins x=513; identically
//                          619..626).
//   kModalFieldHeightPx 31 — editor.png's field, borders included (y=5..35),
//                          vertically centred in the row's content band.
//   kModalFieldPadXPx 7  — its border-to-ink inset (x=86..92).
//   (the label-to-field gap is NOT a constant of its own since 2026-08-29:
//    the architect ruled it EQUAL TO THE WINDOW-EDGE → LABEL PAD, so both
//    are `pad`, icon_row_pad_x, read twice — the crop's own 11 is retired,
//    two numbers for one visual gap being the drift.)
//   kModalFieldWidthPx 520 — AUTHORED, not sampled (the crop's field width is
//                          its dialog's layout, not a rule): wide enough for
//                          every render-entry id and settings line met in
//                          practice. AN OVER-LONG BUFFER SCROLLS since
//                          2026-08-13 (architect at his live test; the
//                          standing "the dialog editors do not scroll"
//                          accepted cost recorded here is RETIRED): the field
//                          travels horizontally to keep the caret inside it,
//                          through the flag editor's own mechanism and rule
//                          (text_editor::State::view_offset_px), while the
//                          published byte geometry stays the painter's
//                          unclipped truth.
//   kModalFocusFramePx 1 — the keyboard focus frame, RESERVED around every
//                          button and painted for the focused one (the
//                          reflow-free rule is at the constant).
// THE FIELD ABSORBS THE SHRINK on a narrow window exactly as it did in the
// box — it takes whatever is left between the label and the buttons, floored
// at 40px so it stays a field.
//
// THE PUBLICATION is the floating surfaces' own convention: this runs
// UNCONDITIONALLY from on_redraw's tail, rewrites AppState::modal_dialog and
// AppState::dialog_editor_text every run (zero/invalid with no dialog up), so
// the pointer path always reads what is actually on screen and a closed
// dialog strands nothing. `box` is the whole lane — the modal's surface — and
// it is what the hover invalidation and the damage ride. Damage: the openers
// invalidate the whole window (no surface exists before the first paint);
// every later edit, blink, flash and closer rides invalidate_modal_dialog_area,
// which takes this row's lane whole — the modal's surface, and the rect a
// closer owes (viewport.cpp).

namespace {

// IN WINDOWS PX since the unit's change (architect 2026-10-02): every number
// in this block re-authored from the laptop pixel to the device size it had
// on the tablet — the gap 8 as 6, the field 31 as 23, its pad 7 as 5, its
// width 520 as 378 and its 40-px floor as 29, the button box 32 as 23, its
// pads 9 and 10 as 7 and 7.
constexpr double kModalButtonGapPx    = 6.0;
constexpr double kModalFieldHeightPx  = 23.0;   // includes its sunken edge
constexpr double kModalFieldPadXPx    = 5.0;
constexpr double kModalFieldWidthPx   = 378.0;  // authored; see the block above
constexpr double kModalFieldMinWidthPx = 29.0;  // the field's floor
// THE DIALOG BUTTONS' BOX — the deleted toolbar row's own anatomy, OWNED here
// since the 2026-08-12 relayout dissolved that row (these buttons read row
// 2's constants until then; the architect's original mix — "the size should
// be the size of the save, undo, redo, render... the behavior/colors should
// be the icon buttons'" — is unchanged, only the numbers' home moved). The
// 32 IS row 2's derivation frozen: its 44px content minus its two 6px
// vertical button margins, the box the crop's own buttons measure exactly;
// the 9/10 pads are its label paddings (the row-2 crop provenance is git
// history). It fits the bottom row's 32-Windows-px content band with room
// either side.
// A WORD BUTTON IS WINDOWS' STANDARD PUSH BUTTON, 75 x 23 Windows px
// (architect 2026-10-02: the dialog unit's 50 x 14 at MS Sans Serif 8 pt),
// the 23 the box above — which today's laptop-pixel 32 re-authored already
// was — and the 75 a MINIMUM WIDTH: a label whose pads and run need more
// widens its button, as a Windows button sized to its text does, and the
// label is CENTRED in its button as Windows centres a push button's text.
// Every word button the product draws fits 75 (re-greped 2026-10-02: OK,
// Cancel, Yes, Save, Discard, Retry, Reload, Keep, Delete — Discard the
// widest at about 56 with its pads at the 13-px face), so the row reads as
// Windows' row of equal buttons. A glyph button is not this box: the render
// player's seven are row 8's toolbar case, 23 x 22 (architect 2026-10-05,
// bottom_row_seats).
constexpr double kModalBtnBoxPx       = 23.0;
constexpr double kModalBtnMinWidthPx  = 75.0;
constexpr double kModalBtnPadLeftPx   = 7.0;
constexpr double kModalBtnPadRightPx  = 7.0;
// THE FOCUS FRAME'S WIDTH — the keyboard-focused button's frame, ONE
// DkShadow line OUTSIDE its raised box (architect 2026-10-02, the planner's
// reading of the Windows default-button frame; the face at the button walk
// below). RESERVED FOR EVERY BUTTON AND PAINTED FOR ONE, which is what makes
// moving the focus reflow nothing: the cluster's right anchor and the
// content's right bound both spend it, and the buttons themselves never move.
// THE RENDER PLAYER'S RIGHT PAIR SPENDS NONE (architect 2026-10-05): it ends
// at row 8's right pad (bottom_row_seats), so its frame paints in the pad.
// One relief line (relief_line_px), so it fits at every scale by
// construction: the vertical margin is (32 - 23)/2 Windows px against its 1,
// and the 6-px inter-button gap absorbs one frame from each neighbour.
constexpr double kModalFocusFramePx   = 1.0;
// THE PLAYER ROW'S GLYPH GAP — between two of the render player's glyph
// boxes, the player's own since the roster's buttons began to touch
// (architect 2026-10-02): 1 Windows px, the laptop pixel's 2-px button gap
// re-authored.
constexpr double kPlayerGlyphGapPx    = 1.0;

// THE MODAL'S FACE STATE, dropped together. The three indices all name slots
// in modal_dialog.buttons and the field bit names modal_dialog.field, so they
// all go stale on exactly the same edges — and this painter owns every one of
// those edges (the full rule and the edge list are at
// AppState::modal_dialog_focus). The two companion bits ride their own index:
// the press's inside flag and the focus's strength are meaningless without
// one, so they reset with it rather than on rules of their own. THE KEYBOARD
// PRESS ARM IS IN HERE FOR A SHARPER REASON than a stale face, the pointer
// arm's own: it is an act that has not happened yet, and a dialog that changed
// under it must not be able to receive it.
// No damage of its own: every caller is mid-frame on the lane it is about to
// repaint.
void reset_modal_dialog_face_state(AppState& app) {
    app.modal_dialog_pressed         = -1;
    app.modal_dialog_press_inside    = false;
    app.modal_dialog_press_shift     = false;
    app.modal_dialog_press_ms        = 0;
    app.modal_dialog_focus           = -1;
    app.modal_dialog_focus_active    = false;
    app.modal_dialog_key_pressed     = -1;
    app.modal_dialog_key_pressed_key = 0;
    // THE LIST BIT is modal face state too (2026-08-28): whether the ring's
    // -1 means the folder overlay's list, under both of the overlay's
    // contents alike (the ring's own record, input_key_dispatch.cpp).
    // It dies on the same edges — a prompt raised over the player and
    // answered leaves the ring off the list, as a fresh open does.
    app.folder_overlay.list_focused  = false;
}

} // namespace

void GuiPaintHandler::paint_modal_dialog(cairo_t* cr) {
    // The publication reset, every run — a dialog that is not painted leaves
    // nothing behind for the pointer path to grab. THE OUTGOING SESSION is read
    // first: it is the last frame's statement of which surface these face
    // indices name, and the face-state reset below is what keeps them from
    // outliving it.
    AppState::ModalDialogGeometry& dlg = app.modal_dialog;
    const uint64_t prev_session = dlg.session;
    // THE OUTGOING ENABLED BITS, AS PAINTED — read before the reset clears the
    // stash, for the player row's as-painted publication at the button walk
    // below (the rule is there).
    std::vector<std::pair<AppState::PlayerButtonAct, bool>> prev_enabled;
    prev_enabled.reserve(dlg.buttons.size());
    for (const AppState::ModalDialogButton& pb : dlg.buttons)
        prev_enabled.emplace_back(pb.player_act, pb.enabled);
    // THE OUTGOING THUMB COLUMN AND TRACK, AS PAINTED, for the same reason:
    // a frame whose clip misses the track carries them (the scrub, below).
    const int     prev_thumb_x = dlg.scrub_thumb_x;
    const GuiRect prev_scrub   = dlg.scrub;
    dlg.valid   = false;
    dlg.owner   = AppState::ModalDialogOwner::None;
    dlg.session = 0;
    dlg.box     = GuiRect{0, 0, 0, 0};
    dlg.field   = GuiRect{0, 0, 0, 0};
    dlg.scrub   = GuiRect{0, 0, 0, 0};
    dlg.clock   = GuiRect{0, 0, 0, 0};
    dlg.scrub_thumb_x = -1;
    dlg.buttons.clear();
    app.dialog_editor_text = AppState::DialogEditorText{};

    // WHICH DIALOG: the prompt outranks every editor (the one coexistence the
    // retired status chain also resolved prompt-first — a WM close can raise the
    // unsaved-work prompt over a standing editor, and the prompt is then what
    // owns the keyboard); the editor fork is the shared one the row's yield
    // reads too (dialog_editor_to_paint, above), so the row cannot stand its
    // tenants down for a dialog this body then declines to paint.
    const bool prompt_up = app.prompt.active;
    // THE RENDER PLAYER, the third owner (2026-08-28): under the prompt like
    // the editors, and never live beside one (its opener refuses under any
    // editor, its key router consumes every opener).
    const bool player_up = !prompt_up && app.render_player.active;
    // THE PICKER, the fourth owner (2026-08-28): the same rank as the player,
    // the two never live together (each opener refuses under the other) and
    // neither live beside an editor. THIS IS ONE OF THE THREE PLACES THE
    // RANKING IS SPELLED (AppState::modal_dialog_live_session and
    // modal_dialog_stash_current are the others) and they must agree or the
    // owner-tag doctrine breaks.
    const bool picker_up = !prompt_up && !player_up && app.picker.active;
    std::string prefix;
    text_editor::State* ed =
        (prompt_up || player_up || picker_up)
            ? nullptr : dialog_editor_to_paint(app, prefix);
    if (!prompt_up && !player_up && !picker_up && ed == nullptr) {
        // No dialog: the three pointer/keyboard face indices reset WITH the
        // stash, so a fresh dialog cannot inherit the previous one's lit
        // button, its armed button or its keyboard focus.
        reset_modal_dialog_face_state(app);
        return;
    }

    // THE FACE STATE'S OTHER RESET EDGE, which keeps a dialog STANDING and so
    // never reaches the arm above (the whole rule is at
    // AppState::modal_dialog_focus): A CHANGE OF SESSION. The indices name
    // buttons of a surface that is gone, and an armed or focused index carried
    // across would be aimed at whatever now sits at that slot. ONE test covers
    // every such edge because one raise takes one id: prompt over editor,
    // editor after prompt, a prompt REPLACING a prompt at the save-failed rung
    // (which no owner test can see), and EDITOR AFTER EDITOR — a close and an
    // open inside one dispatch batch, which is why this
    // reads the session rather than the owner as it did until 2026-08-14.
    // Read before the branches below write anything.
    // THE RESET IS ALSO THE PROMPT'S FOCUS ASSIGNMENT (2026-08-13): a prompt is
    // raised with PASSIVE focus on a button, so the frame that resets is
    // the frame that assigns. The assignment itself waits until the buttons
    // exist, a few dozen lines down — this only remembers that this frame owes
    // it.
    const uint64_t live_session = app.modal_dialog_live_session();
    const bool face_state_reset = prev_session != live_session;
    if (face_state_reset) {
        reset_modal_dialog_face_state(app);
    }

    // THE MODAL'S SURFACE IS THE BOTTOM ROW'S CONTENT BAND — the lane's ground
    // is already painted (paint_bottom_strip's chrome runs on
    // every frame class and yields the rest of the row to this body), so
    // nothing here draws a ground of its own.
    const GuiRect lane    = bottom_row_area(app);
    const GuiRect content = bottom_row_content_area(app);
    if (lane.w <= 0 || lane.h <= 0 || content.h <= 0) {
        // A degenerate row publishes nothing, the cold answer the pointer path
        // already reads correctly (a zero stash contains no point) — and it
        // names no buttons, so the face state goes with it.
        reset_modal_dialog_face_state(app);
        return;
    }

    cairo_save(cr);
    const GuiFont font = gui_font(GuiFace::Body);

    // The row's own left/right margin — the modal sits on the same pad the
    // tenants it displaced sit on, which since 2026-08-14 is literally the
    // same accessor they read (icon_row_pad_x, paint_handler.h).
    const int pad   = icon_row_pad_x();
    const int bgap  = scaled_px(kModalButtonGapPx);
    // The deleted toolbar row's button box, owned by the dialog since the
    // 2026-08-12 relayout (kModalBtnBoxPx — 32 = row 2's 44 content minus its
    // two 6px margins, the derivation frozen at the constant) — on every
    // owner's row but THE PLAYER'S, whose buttons are row 8's TOOLBAR CASE
    // at row 8's seat (architect 2026-10-05, bottom_row_seats: the two rows
    // are one row). `btn_y` and `btn_h` are the one band the walk, the scrub
    // and the editor's label all read.
    const BottomRowSeats seats = bottom_row_seats(content);
    const int btn_h = player_up ? seats.case_h : scaled_px(kModalBtnBoxPx);
    const int btn_pad_l = scaled_px(kModalBtnPadLeftPx);
    const int btn_pad_r = scaled_px(kModalBtnPadRightPx);
    // The focus frame's reserved band (kModalFocusFramePx, above): spent by
    // the layout for EVERY button whether or not one is focused, so the frame
    // can never push the row around when the focus moves. One relief line.
    const int ring = scaled_px(kModalFocusFramePx, 1);

    // -- The buttons' words and widths, shaped up front (the layout needs the
    //    row's total before anything can be placed). --
    // A BUTTON IS A WORD OR A GLYPH (2026-08-28, the render player's row):
    // `glyph` set means the button is the roster's own icon in a
    // kModalBtnBoxPx SQUARE — the THREE transport buttons wear
    // MediaSkipBackward, MediaPlaybackStart (swapped to MediaPlaybackPause
    // while the transport is live, the bottom row's one-button-two-faces
    // rule) and MediaSkipForward — the row was FOUR, MediaPlaybackStop among
    // them, until the player's Stop retired 2026-09-01, and the middle
    // button's live face stays the pause it took on 2026-08-28 — the REPEAT
    // ONE toggle wears
    // MediaRepeatSingle in both its states, the UP button GoParentFolder, and
    // since 2026-09-01 the LOAD IN PLACE wears DialogOkApply and CLOSE
    // WindowClose — while a word button keeps the
    // label box every prompt and editor button has always had. THE PLAYER'S
    // ROW IS ALL GLYPHS NOW and the word kind is the other three owners'
    // alone: a prompt's answers, the dialog editors' OK / Cancel and the
    // picker's Cancel. Both kinds wear the roster's raised box (architect
    // 2026-10-02). `lit` is the roster's SELECTED face — the row's one lamp,
    // Repeat one, and no other button has a state to say. `player_act` is the player's dispatch vocabulary; the other two
    // stay zero/false on its buttons.
    struct DialogButtonPlan {
        std::string label;
        char        response_key = 0;
        bool        editor_ok    = false;
        AppState::PlayerButtonAct player_act = AppState::PlayerButtonAct::None;
        bool        glyph        = false;
        bool        lit          = false;
        // The disabled face (architect 2026-08-30, the player's row) — the
        // live predicate's answer at plan time, published on the record
        // below. ONE OWNER READS IT, re-greped 2026-09-30: the player's seven
        // through render_player_button_enabled; true on every other owner's
        // buttons.
        bool        enabled      = true;
        icons::Icon icon         = icons::Icon::MediaPlaybackStart;
        std::string tooltip;     // the player's own; empty = the composer's
        std::string tooltip2;    // the modifier line; empty = the one-line form
        int         w            = 0;
        int         label_w      = 0;   // a word button's run, ceiled
        // WHERE IT PAINTS. The single left-flushed cluster every other owner
        // lays out is written into this field by the walk below; THE PLAYER'S
        // ROW IS NOT ONE CLUSTER (transport, separator, scrub, clock,
        // separator, the lamp, the Up button — then the last two FLUSH RIGHT),
        // so its own branch writes all seven and the walk simply paints where
        // it is told.
        int         x            = 0;
    };
    std::vector<DialogButtonPlan> plan;
    if (prompt_up) {
        for (size_t i = 0; i < app.prompt.response_labels.size(); ++i) {
            DialogButtonPlan b;
            b.label = app.prompt.response_labels[i];
            b.response_key = i < app.prompt.response_keys.size()
                                 ? app.prompt.response_keys[i] : 0;
            plan.push_back(std::move(b));
        }
    } else if (player_up) {
        // THE PLAYER'S ROW, in painted order (architect 2026-08-28; the
        // transport is THE MAIN WINDOW'S OWN TRIPLE since 2026-09-01): the
        // two SKIPS around Play/Pause, on Home and End since 2026-08-31 and
        // wearing the skip glyphs still —
        // then the scrub and the clock (both laid out in the player's own
        // branch below, the separators either side of them retired
        // 2026-10-02), then the REPEAT ONE
        // lamp, the UP button, and the last two FLUSH RIGHT: Load in place ·
        // Close, GLYPH buttons since 2026-09-01 like every other button on
        // this row.
        // Close LAST, the escape sentinel by construction as every prompt has
        // it. The plan's order is also the ring's, so Tab walks the row left to
        // right. A STOP BUTTON SAT AFTER PLAY/PAUSE from 2026-08-28
        // (Audacious's own order) until the act retired.
        // THE PAUSE FACE IS TRUTHFUL (architect 2026-08-31): the button
        // wears PAUSE only where its press would PAUSE — the transport live
        // AND the highlight on the transport's own item (or nowhere), which
        // is exactly render_player_highlight_act_row answering −1. Under a
        // highlight standing on a folder row or on another wav the press
        // would GO THERE, so the face is the play glyph and the tooltip's word
        // follows it. SINCE 2026-09-01 THE FACE AND THE WORD ARE ONE OWNER,
        // render_player_play_face (the act's own two forks read without
        // acting), and the glyph is its Pause answer alone; the hint takes
        // the whole answer, so a folder row says "Open folder" and a paused
        // transport "Resume". THE HINTS' OTHER STATE BITS ARE RESOLVED HERE
        // TOO (RenderPlayerHintState, app_state.h — each a predicate the act
        // or the face reads; the previous-track window needs the position,
        // which this painter already reads for the clock). Every bit
        // reaches a repaint through the row's one damage owner: the three
        // highlight movers and the transport writers damage the row already,
        // for the Load in place face and the pause glyph.
        RenderPlayerHintState hint;
        hint.play_face     = render_player_play_face(app);
        hint.home_previous = render_player_home_takes_previous(app, playback,
                                                                audio);
        // (END'S IDLE BIT STOOD HERE and retired 2026-09-04 with the act it
        // described: it was seek_to's second refusal asked past its first, so
        // that the hint said "At the end" only with an item bound. The right
        // skip's press is the NEXT TRACK now, whose one refusal greys the
        // button, and its word names that act in every state. TWO MORE BITS
        // STOOD HERE for the one day of 2026-09-12 — a transport-at-rest fork
        // and an up-a-folder bit — while the skips walked the band; the walk
        // is the car's own act again and the car has no tooltip.)
        // THE SHIFT LINES COMPARE DESTINATIONS, not the twins' walls alone
        // (2026-09-01): the line names the FILE the shifted
        // press plays, so it must drop wherever the plain press already
        // plays that file. Home's two destinations are home()'s own — the
        // PREVIOUS entry inside the previous-track window
        // (render_player_home_takes_previous, the fork's one owner, resolved
        // just above) and otherwise a seek that leaves the item where it is —
        // against the twin's `folder.front()`, index 0; both land at frame 0 of
        // the file they name, so the index decides. On the folder's second
        // item inside that window the two are one file and the line goes,
        // which the wall alone (item_index > 0) could not see. THE WALL IS
        // STILL A TERM and is still asked from the twin's own owner, which
        // the face reads too — it is what answers on item_index == 0, where
        // the twin is dead outright.
        const int home_plain_index =
            hint.home_previous ? app.render_player.item_index - 1
                               : app.render_player.item_index;
        hint.home_shift_differs =
            render_player_first_in_item_folder_actionable(app) &&
            home_plain_index != 0;
        // THE RIGHT SKIP COMPARES DESTINATIONS TOO SINCE 2026-09-04, and for
        // the first time: its plain act is the NEXT TRACK now, so on the
        // folder's SECOND-TO-LAST item both presses play the folder's last
        // wav and the line goes, exactly as Home's does on the second item.
        // Both destinations are indices into the ITEM's folder — the plain
        // act's is the item's own plus one, the twin's is that folder's last
        // — and the wall, still asked from the twin's own owner, is what
        // answers on the last item, where the twin is dead outright. (The bit
        // was that wall alone while the plain act was a seek inside the item,
        // which could never reach another file.)
        const int end_plain_index = app.render_player.item_index + 1;
        const int end_twin_index =
            static_cast<int>(app.render_player.item_folder.size()) - 1;
        hint.end_shift_differs =
            render_player_last_in_item_folder_actionable(app) &&
            end_plain_index != end_twin_index;
        const bool pause_face = hint.play_face == PlayerPlayFace::Pause;
        auto glyph_button = [&](AppState::PlayerButtonAct act,
                                icons::Icon icon, bool lit = false) {
            DialogButtonPlan b;
            b.player_act = act;
            b.glyph      = true;
            b.lit        = lit;
            b.enabled    = render_player_button_enabled(app, playback, act);
            b.icon       = icon;
            b.tooltip    = render_player_button_hint(act, hint);
            b.tooltip2   = render_player_button_shift_hint(act, hint);
            plan.push_back(std::move(b));
        };
        // (THE `word_button` LAMBDA IS DELETED — 2026-09-01, with its last two
        // callers: Load in place and Close became GLYPH buttons, so the
        // player's row is seven glyphs and no word at all. The other three
        // owners' word buttons are built in their own branches below and the
        // walk's word arm still paints them; what went is this owner's copy of
        // the builder, not the kind.)
        // THE SKIP GLYPHS ARE UNTOUCHED BY EITHER REMAP: the roster's own two
        // transport skips wear this pair over the same keys, and
        // MediaSkipForward says "skip forward" whether the press lands the
        // track's end (2026-08-31) or the next track (2026-09-04) — a
        // skip-forward glyph over a skip-forward act, so nothing is owed
        // here.
        glyph_button(AppState::PlayerButtonAct::Home,
                     icons::Icon::MediaSkipBackward);
        // PLAY/PAUSE WEARS THE PAUSE GLYPH ON THE PAUSE FACE (above), not the
        // stop square it wore until the row took a Stop button (2026-08-28).
        // THAT BUTTON RETIRED 2026-09-01 AND THE PAUSE GLYPH STAYS: this is a
        // player's transport, where a pause is a resumable rest, and the row
        // is now the main window's triple with the pause face on the middle
        // button. The roster's transport button is
        // untouched — bare Space over the project's audio is one toggle with
        // no pause state, so it keeps Play/Stop, and MediaPlaybackStop keeps
        // that one reader.
        glyph_button(AppState::PlayerButtonAct::PlayPause,
                     pause_face ? icons::Icon::MediaPlaybackPause
                                : icons::Icon::MediaPlaybackStart);
        glyph_button(AppState::PlayerButtonAct::NextTrack,
                     icons::Icon::MediaSkipForward);
        glyph_button(AppState::PlayerButtonAct::RepeatOne,
                     icons::Icon::MediaRepeatSingle,
                     app.render_player.repeat_one);
        // UP, THE `..` ROW'S ACT ON A BUTTON (architect 2026-09-01, with the
        // player's move inside `tmp/`): the listings carry no `..` row any
        // more, so the way out of a batch folder is here — beside the lamp,
        // ahead of the right-flushed pair, on GoParentFolder, Explorer 95's
        // VIEW_PARENTFOLDER — the folder with the bent arrow (an arrow alone
        // says "up" about a NUMBER while the act here is leaving a
        // DIRECTORY). GoUp is untouched — the roster's bare-Up
        // transport button still wears it, a def being a glyph and several
        // buttons being free to wear one. It greys at the root through the
        // act's own wall.
        glyph_button(AppState::PlayerButtonAct::Up,
                     icons::Icon::GoParentFolder);
        // THE LAST TWO ARE GLYPHS SINCE 2026-09-01 (architect, with the icon
        // row's Load in place moving to the history group): "the media player
        // button should get the checkmark glyph then. Close should then get a
        // glyph also, to avoid being the odd one out: window-close.svg". So
        // the LOAD wears DialogOkApply — the very checkmark the icon row's
        // button wears for the same act one surface over — and CLOSE wears
        // WindowClose, Marlett's close X (the
        // notification cards' dismiss was its other reader until the card's X
        // retired, 2026-10-01; a def is a glyph and several buttons are free
        // to wear one). The row is SEVEN GLYPH
        // BUTTONS and no word button, so its faces take the glyph fork whole.
        // Close stays LAST, the escape sentinel by construction, and both keep
        // their acts, their keys and their hints unchanged.
        // AT THE ROOT THE SLOT IS DELETE (architect 2026-09-29): `tmp/`'s own
        // listing is batch folders, where a load has nothing to name, so the
        // sixth button deletes — EditDelete, STD_DELETE's X — the
        // highlighted folder, and its shift press every folder listed; its
        // shift line stands only where "all" is more than the highlighted one.
        // Inside a batch folder the slot is Load in Place, unchanged.
        hint.delete_all_differs = render_player_root_folder_count(app) > 1;
        if (app.render_player.folder == AppState::RenderPlayer::Folder::Root)
            glyph_button(AppState::PlayerButtonAct::Delete,
                         icons::Icon::EditDelete);
        else
            glyph_button(AppState::PlayerButtonAct::LoadInPlace,
                         icons::Icon::DialogOkApply);
        glyph_button(AppState::PlayerButtonAct::Close,
                     icons::Icon::WindowClose);
    } else {
        // OK = the editor's Enter commit, Cancel = its Esc — the buttons
        // dispatch through the SAME key route (input_pointer's dialog press
        // claim), button-is-its-chord.
        // THE PICKER'S ROW IS **Cancel** ALONE (architect 2026-08-29): a click
        // on a row opens it, so there is nothing for an OK to add — Enter on
        // the list is the same act from the keyboard. Its one-day OK, named by
        // the act it ran, went with the row-click ruling; Cancel is untouched
        // and stays LAST, which on a one-button row makes it the escape
        // sentinel by construction. The hint composer's "OK (Return)" /
        // "Cancel (Esc)" follows for free, those being the two routers'
        // own keys under the product's one spelling (spell_chord's head,
        // gui_input.h).
        if (!picker_up) {
            DialogButtonPlan ok;
            ok.label     = "OK";
            ok.editor_ok = true;
            plan.push_back(std::move(ok));
        }
        DialogButtonPlan last;
        last.label = "Cancel";
        plan.push_back(std::move(last));
    }
    // EVERY BUTTON'S WIDTH, and the ONE CLUSTER'S total behind it — the total
    // is the SINGLE-CLUSTER owners' (a prompt, the picker, an editor), whose
    // cap `buttons_x_max` is measured against it; the player spends the same
    // widths on its own two-cluster arrangement and reads no total.
    int buttons_w = 0;
    for (size_t i = 0; i < plan.size(); ++i) {
        if (plan[i].glyph) {
            // A glyph button is the TOOLBAR CASE (the player's alone, its
            // seven being the plan's only glyph buttons): row 8's width.
            plan[i].w = seats.case_w;
        } else {
            const double lw =
                text_shape::shape_text_run(font, plan[i].label).width_px;
            // CEIL, NOT nearbyint: a measured run width is fractional and the
            // box it sizes must CONTAIN the ink, so it rounds UP — rounding
            // to nearest could clip the label's last column inside its own
            // pads. The same round-up rule the clock cell's damage box takes
            // on its width (paint_bottom_strip). FLOORED AT WINDOWS' 75
            // (kModalBtnMinWidthPx), the label centred in it (the walk).
            plan[i].label_w = static_cast<int>(std::ceil(lw));
            plan[i].w = std::max(scaled_px(kModalBtnMinWidthPx),
                                 btn_pad_l + plan[i].label_w + btn_pad_r);
        }
        buttons_w += plan[i].w + (i > 0 ? bgap : 0);
    }

    // A PROMPT IS RAISED WITH PASSIVE FOCUS ON A BUTTON (architect 2026-08-13,
    // the ruling that gave this prompt system an Enter answer — PromptState
    // owns the supersession and the two facts that make it safe, the first of
    // which is that the last button is always the ESCAPE SENTINEL). WHICH
    // button is the RAISE'S OWN CHOICE (PromptInitialFocus, carried on the
    // question since 2026-08-28): the last on every prompt but THE FOUR
    // CONFIRMATION RAISES — the load's two, File → Revert's and, since
    // 2026-09-16, THE PHASE RESET PASTE'S — which ask for
    // their FIRST, each being already the deliberate second step of an
    // explicit act, so its Enter confirms it.
    // The load's one prompt body carries both `'` subjects (the player's entry
    // and the `h` view's walk member), so the two roads answer Enter alike,
    // and the revert and the two pastes answer it as they do; the two
    // three-way Save / Discard / Cancel prompts, which confirm no act already
    // asked for, are what keep the last button. This is the
    // ONE assignment site: it rides the same reset the
    // focus's other three edges ride, so a fresh prompt and a prompt replacing
    // a prompt are one case, and it runs HERE rather than at the reset because
    // the plan's ends are not known until the plan exists. An EDITOR dialog is
    // deliberately not touched: it opens with focus in its FIELD, which is what
    // -1 already means there.
    if (face_state_reset && prompt_up && !plan.empty()) {
        app.modal_dialog_focus =
            app.prompt.initial_focus == PromptInitialFocus::FirstButton
                ? 0
                : static_cast<int>(plan.size()) - 1;
        app.modal_dialog_focus_active = false;
    }

    // -- THE ROW LAYOUT: ONE LEFT-FLUSHED BLOCK (architect 2026-08-13, at his
    //    live look: "right now you have to read the bottom left and then the
    //    bottom right — your eyes have to go the whole distance of the screen.
    //    Everything should be just flushed left"). --
    //
    // The row reads left to right from its own left pad: a PROMPT is its
    // message, one pad, then the buttons in painted order; an EDITOR is its
    // label, the field, one pad, then the buttons. The right-aligned cluster
    // of hours earlier is retired with the reserved right-pad ring that
    // anchored it.
    //
    // THE ALIGNMENT RULE, ONE SENTENCE FOR EVERY OWNER (architect 2026-08-29,
    // and its full statement is in the PICKER's branch below): a modal that
    // carries A FIELD OR A MESSAGE sits FLUSH LEFT with it — the prompts, the
    // dialog editors, and the player, whose transport is that row's left-hand
    // content — while a LIST's buttons sit FLUSH RIGHT, the picker's one
    // button being the whole of that case.
    //
    // NOTHING OVERFLOWS THE LANE, and the primacy is ROW 8'S STATE CELL'S OWN
    // (one collision discipline across the product — a text cell anchors at
    // its own edge and clips when it overflows, that cell against the row's
    // right button block): THE
    // BUTTONS STAY WHOLE AND THE CONTENT GIVES. A prompt with no reachable
    // button is a modal with no way out, while a clipped message is still
    // readable to its clip — so the cluster's left edge is capped at
    // `buttons_x_max`, the rightmost start that still leaves the last button
    // and its focus frame inside the right pad, and whatever precedes it CLIPS (the
    // message) or SHRINKS (the field) against that cap. In the pathological
    // case where the buttons alone are wider than the lane, the cap floors at
    // the left pad and the cluster runs past the right one: the content is
    // then zero-wide and the buttons are what is left, which is the same
    // choice made twice.
    //
    // THE FOCUS FRAME IS RESERVED, NOT PAINTED, on both ends of the cluster —
    // one frame line inside the right pad so the last button's cannot touch
    // the window edge, one inside the left clearance so the first button's
    // cannot touch the message or the field. Between neighbours the 8px gap
    // absorbs both (kModalFocusFramePx). (The player's row is row 8's and
    // reserves no ring at its right pad — its branch below.) So the focus can
    // move anywhere on the row without reflowing it, which is the whole point
    // of reserving.
    const int cx0 = seats.left_x;                       // content left
    const int cx1 = seats.right_x;                      // the row's right pad
    const int buttons_x_max = std::max(cx0, cx1 - ring - buttons_w);
    // Centred in the content band on every owner's row but the player's,
    // which takes row 8's case seat (above).
    const int btn_y = player_up ? seats.case_y
                                : content.y + (content.h - btn_h) / 2;
    int buttons_x0 = cx0;   // set by whichever branch runs, below

    if (prompt_up) {
        // THE PAINTED GATE'S ONE WRITER OF TRUE (2026-08-13): this branch is
        // drawing the question into the buffer paint_one_frame commits at the
        // end of this same iteration, so from here on the prompt is a surface
        // the user has seen and its keys and buttons may answer. Same surface,
        // same iteration, no sync machinery — the locality IS the mechanism;
        // the full rule, the gate's two sites and the editor asymmetry are at
        // PromptState (app_state.h). Set before the drawing calls because the
        // fact being recorded is the frame, not the ink: every raise
        // invalidates the whole window, so the frame that reaches here always
        // carries the modal's pixels, and each of the frame's damage rects
        // runs this branch (idempotent).
        app.prompt.painted = true;
        dlg.owner = AppState::ModalDialogOwner::Prompt;
        // THE MESSAGE SETS WHERE THE BUTTONS START — one pad of clearance
        // plus the reserved ring past its ink — up to the cap, which is where
        // an over-long message stops pushing and starts CLIPPING instead.
        // Shaped once here and shown from the run: the width is needed for the
        // layout, and re-shaping it to paint would measure the same string
        // twice.
        const text_shape::ShapedRun msg =
            text_shape::shape_text_run(font, app.prompt.text);
        // CEIL: the shaped width is fractional and it sets where the buttons
        // may start, so it rounds UP — a nearest-rounded width could place the
        // cluster inside the message's last column. The round-up rule again
        // (the buttons' widths above, the clock cell's damage box).
        const int msg_w = static_cast<int>(std::ceil(msg.width_px));
        buttons_x0 = std::min(cx0 + msg_w + pad + ring, buttons_x_max);
        const int msg_clip = std::max(0, (buttons_x0 - ring - pad) - cx0);
        cairo_save(cr);
        cairo_rectangle(cr, cx0, content.y, msg_clip, content.h);
        cairo_clip(cr);
        set_palette_source(cr, palette().label);
        text_shape::show_shaped_run(
            cr, msg, static_cast<double>(cx0),
            redesign_baseline(font, static_cast<double>(content.y),
                              static_cast<double>(content.h)));
        cairo_restore(cr);
    } else if (player_up) {
        // -- THE RENDER PLAYER'S ROW (architect 2026-08-28 — the relaid
        //    row): TRANSPORT · SCRUB · CLOCK · REPEAT ONE · UP ......... LOAD
        //    IN PLACE · CLOSE, the last two flush right. The arrangement is
        //    Audacious-Qt's under Breeze Dark, which is what the architect
        //    ruled off his own screen — "not the screenshot as a whole, just
        //    the layout: transport, separator, scrub, then the time, then
        //    another separator, then the repeat button" — and its TWO
        //    SEPARATORS RETIRED with the roster's (architect 2026-10-02, the
        //    Windows-95 chrome; Sound Recorder, whose slider the scrub is, has
        //    none): the groups stand the roster's eight Windows px of bare
        //    ground apart (icon_group_space_px) — transport → scrub, scrub →
        //    clock, clock → the lamp. THE CLOCK IS TWO TIME FIELDS since
        //    2026-10-05 (architect: Sound Recorder's Position and Length
        //    panels, ACID's separate fields), kTimeFieldGapPx apart — the
        //    position, then the length.
        //
        //    THE SCRUB IS THE ONE FLEXIBLE ITEM and everything else is fixed,
        //    so the row's primacy rule reads plainly here: THE BUTTONS STAY
        //    WHOLE AND THE SCRUB GIVES, and past its floor it PUBLISHES ZERO
        //    — the honest answer the tick and the press router already read
        //    (a zero rect contains no point and damages nothing) rather than a
        //    track too small to seat a handle.
        //
        //    THE ROW GAINED THE UP BUTTON on 2026-09-01, between the lamp
        //    and the right-flushed pair, so the fixed run after the scrub
        //    carries TWO glyph boxes now and the plan is seven items — and
        //    LATER THAT DAY THE PAIR ITSELF BECAME GLYPHS, so every one of the
        //    seven is a box of the same width and the whole row is fixed
        //    except the scrub. THE BOXES ARE PLAIN RAISED PUSH BUTTONS on
        //    ROW 8'S TOOLBAR CASE, 23 x 22 Windows px at row 8's seat
        //    (architect 2026-10-05, bottom_row_seats: they were the modal
        //    box's 23 x 23 square centred in the band, which stood them 2
        //    rows lower at 350 %), Sound Recorder's push-button face over the
        //    roster's 16-px glyph at the case's own (3, 3).
        //
        //    THE ROW IS ROW 8'S (architect 2026-10-05): the transport starts
        //    at row 8's left pad, the right-flushed pair ends at its right
        //    pad (no focus-frame reserve inside it, which stood the pair one
        //    relief line left of row 8's block), the buttons stand in row 8's
        //    band, and the scrub fills whatever lies between.
        //
        //    THIS BRANCH LAYS OUT AND PAINTS EVERYTHING BUT THE BUTTONS: it
        //    writes each button's own x (the walk below paints where it is
        //    told) and draws the slider and the clock between them. The items
        //    are disjoint, so painting them ahead of the buttons is only an
        //    ordering of convenience. --
        dlg.owner  = AppState::ModalDialogOwner::Player;
        buttons_x0 = cx0;   // unread on this branch; the plan carries the x's

        // THE GLYPH GAP between two of the row's boxes is kPlayerGlyphGapPx,
        // the player's own since the roster's buttons began to touch; THE
        // GROUP SPACE between its items is the roster's.
        const int ggap     = scaled_px(kPlayerGlyphGapPx, 1);
        const int group    = icon_group_space_px();

        // THE CLOCK'S TWO FIELDS ARE MEASURED BEFORE ANYTHING IS PLACED,
        // their width being one of the fixed terms the scrub's own is what is
        // left over. Each is a TIME FIELD (the shape, the face and the
        // fixed-width rule at kTimeFieldHeightPx), its cell the widest-digit
        // specimen alone (time_field_metrics, row 8's own memo), so neither
        // field nor anything right of it moves as the position runs or an
        // item of another length loads.
        const GuiFont cfont = gui_font(GuiFace::Body);
        const double cell_w =
            time_field_metrics(cfont).time_w;
        const int field_pad = scaled_px(kStatusPanelPadPx);
        const int field_w   =
            static_cast<int>(std::ceil(cell_w)) + 2 * field_pad;
        const int field_gap = scaled_px(kTimeFieldGapPx);
        const int clock_w   = field_w + field_gap + field_w;

        // -- The walk: the fixed left run, the right-flushed PAIR (word
        //    buttons until 2026-09-01, glyph boxes since), and the scrub
        //    taking what is between them. --
        const int bw = seats.case_w;
        int px = cx0;
        plan[0].x = px; px += bw + ggap;         // Home (skip back)
        plan[1].x = px; px += bw + ggap;         // Play / Pause
        plan[2].x = px; px += bw;                // End (skip forward)
        const int scrub_x0 = px + group;

        // THE RIGHT-FLUSHED CLUSTER IS TWO GLYPH BOXES since 2026-09-01 (it
        // was Load in place · Close as WORDS, measured runs in label boxes):
        // both are the box's own square now, so the cluster is two boxes and
        // the GLYPH GAP between them — right-flushed at ROW 8'S RIGHT PAD,
        // the last box's right edge row 8's last button's (architect
        // 2026-10-05; the other owners' clusters keep the reserved ring).
        const int words_w  = bw + ggap + bw;
        const int words_x0 = std::max(cx0, cx1 - words_w);
        // What the row owes AFTER the scrub: the group space, the clock
        // cell, the group space, the lamp, THE UP BUTTON with the glyph gap
        // between them, and the GAP BEFORE THE RIGHT-FLUSHED PAIR, which is
        // that same glyph gap since 2026-09-01. THE 2026-08-29 SPACING RULING
        // IS SPENT, not overruled: it put the WORD BUTTONS' OWN gap there ("the
        // air between the last glyph and Load in place is the air between Load
        // in place and Close", the modal word buttons' spacing winning over
        // the icon gap between clusters, where the layout had spent the
        // reserved ring plus a pad) — and with the words gone there is no
        // word-button gap left on this row to win: every gap between two
        // boxes on it is the row's one glyph gap (kPlayerGlyphGapPx).
        const int after_scrub =
            group + clock_w + group + bw + ggap + bw + ggap;
        // THE FLOOR IS TWO HANDLE BOXES — a track that cannot seat the handle
        // at each end is not a track (28 Windows px since the unit's change).
        int scrub_w = words_x0 - after_scrub - scrub_x0;
        if (scrub_w < 2 * scrub_handle_box_px()) scrub_w = 0;

        const int clock_x0  = scrub_x0 + (scrub_w > 0 ? scrub_w + group : 0);
        const int clock_end = clock_x0 + clock_w;
        plan[3].x = clock_end + group;              // Repeat one
        plan[4].x = plan[3].x + bw + ggap;          // Up
        plan[5].x = words_x0;                       // Load in place
        plan[6].x = words_x0 + bw + ggap;           // Close

        // -- THE PLAY-SCRUB, SOUND RECORDER'S SLIDER (architect 2026-10-02;
        //    the look and its owners at render.h's scrub block). The ITEM is
        //    published (dlg.scrub) and it is the button box's own band, so a
        //    press anywhere on it is on the slider and the per-position damage
        //    covers the thumb's whole travel; the MAPPING owns the inset
        //    (render_player_scrub_x_of), the thumb's centre being the frame's
        //    position. --
        const AppState::RenderPlayer& rp = app.render_player;
        const int64_t pos = render_player_position(app, playback);
        if (scrub_w > 0) {
            const GuiRect track{scrub_x0, btn_y, scrub_w, btn_h};
            dlg.scrub = track;
            // THE THUMB'S ROWS, CENTRED IN THE TRACK'S BAND, and THE CHANNEL
            // at its seat inside them: the thumb is its rows above the
            // channel, the channel's own four lines and its rows below, each
            // part rounded on its own.
            const int channel_h = scrub_channel_h_px();
            const int above     = scrub_thumb_above_px();
            const int thumb_h   = scrub_thumb_h_px();
            const int thumb_y   = track.y + (track.h - thumb_h) / 2;
            // THE CHANNEL: PLAIN SUNKEN on a rect four lines tall, so the
            // edge's two rings are the whole of it — Shadow, DkShadow,
            // 3DLight, Hilight top to bottom — across the track's width,
            // nothing inside and nothing filled.
            paint_relief_plain_sunken(
                cr, GuiRect{track.x, thumb_y + above, track.w, channel_h});
            if (rp.frames > 0) {
                // THE THUMB'S COLUMN: the drag's carried x while the thumb is
                // being dragged (the sound continues where it was and the
                // seek commits at the release), the item position's own
                // otherwise.
                const int hx = rp.scrub.armed
                                   ? rp.scrub.marker_x
                                   : render_player_scrub_x_of(app, pos);
                // THE THUMB — PLAIN RAISED on the ground, centred on the
                // column. No hover face: the grab band (scrub_handle_box_px)
                // is the press's business and the cursor its cue.
                const int thumb_w = scrub_thumb_w_px();
                const GuiRect thumb{hx - thumb_w / 2, thumb_y, thumb_w,
                                    thumb_h};
                paint_cell_rect(cr, thumb, palette().ground);
                paint_relief_plain_raised(cr, thumb);
                // THE COLUMN IS PUBLISHED AS PAINTED (the field's rule,
                // AppState::ModalDialogGeometry::scrub_thumb_x): a clip that
                // covers the track drew the whole slider, so this column is
                // the one on screen; a clip that misses it left the last
                // frame's thumb standing, so that frame's column is carried
                // while the session and the track are the same.
                const bool same_track =
                    prev_scrub.x == track.x && prev_scrub.y == track.y &&
                    prev_scrub.w == track.w && prev_scrub.h == track.h;
                dlg.scrub_thumb_x =
                    (!clip_covers_drawable(cr, app, track) &&
                     !face_state_reset && same_track && prev_thumb_x >= 0)
                        ? prev_thumb_x
                        : hx;
            }
        }

        // -- THE CLOCK, after the scrub (there since 2026-08-28): TWO TIME
        //    FIELDS since 2026-10-05, the POSITION and then the LENGTH, each
        //    centred in the content band with its run on the field's own
        //    seat in the field's pair (`clock_ground` / `clock_text`, row 8's,
        //    render.h's palette block) — the fields row 8's clock is, said
        //    twice. ONE RECT ROUND BOTH IS PUBLISHED (dlg.clock) for the
        //    tick's per-position damage, beside the scrub's, and the two CLIP
        //    at the lane's right pad on a narrow window. --
        const int sr = audio.sample_rate();
        auto seconds_of = [sr](int64_t frames) {
            return sr > 0 ? static_cast<double>(frames) / static_cast<double>(sr)
                          : 0.0;
        };
        const int clock_right = std::min(clock_end, cx1);
        if (clock_right > clock_x0) {
            cairo_save(cr);
            cairo_rectangle(cr, clock_x0, content.y, clock_right - clock_x0,
                            content.h);
            cairo_clip(cr);
            const int64_t shown[2] = {pos, rp.frames};
            for (int i = 0; i < 2; ++i) {
                const int cell_x =
                    clock_x0 + i * (field_w + field_gap) + field_pad;
                const GuiRect field =
                    time_field_rect(cell_x, cell_w, content.y, content.h);
                paint_time_field(cr, field);
                // RIGHT-ALIGNED, ending at the cell's right pad (the rule at
                // kTimeShape's block).
                const text_shape::ShapedRun run = text_shape::shape_text_run(
                    cfont, format_timestamp(seconds_of(shown[i])));
                set_palette_source(cr, palette().clock_text);
                text_shape::show_shaped_run(
                    cr, run,
                    static_cast<double>(
                        cell_x + static_cast<int>(std::ceil(cell_w))) -
                        run.width_px,
                    redesign_baseline(cfont, static_cast<double>(field.y),
                                      static_cast<double>(field.h)));
            }
            cairo_restore(cr);
            dlg.clock = GuiRect{clock_x0 - 1, content.y,
                                clock_right - clock_x0 + 2, content.h};
        }
    } else if (picker_up) {
        // -- THE PICKER'S ROW (architect 2026-08-28; ONE BUTTON since
        //    2026-08-29): **Cancel** and NOTHING ELSE — no label, no field,
        //    no OK. The list in the
        //    band above is the whole question, a click on a row is the answer,
        //    and the prompt's `<projects_path>/` prefix went with the field it
        //    labelled.
        //
        //    THE ALIGNMENT RULE (architect 2026-08-29, stated here because
        //    this body is where every owner's row is laid out): A MODAL THAT
        //    CARRIES A FIELD OR A MESSAGE SITS FLUSH LEFT WITH IT — the
        //    prompts, the dialog editors and the player's transport, whose
        //    row reads from the left because that is where the thing being
        //    read or typed into begins ("if my hand is on the left edge to
        //    type, the buttons should be right there") — WHILE A LIST'S
        //    BUTTONS SIT FLUSH RIGHT ("if I'm using the picker my hand stays
        //    on the right"; "I am right-handed", 2026-08-28, on seeing the
        //    picker on the tablet). So the cluster starts at
        //    `buttons_x_max`, the same rightmost seat every other owner uses
        //    as its CAP, and with nothing to its left the row is that one
        //    button. --
        dlg.owner  = AppState::ModalDialogOwner::Picker;
        buttons_x0 = buttons_x_max;
    } else {
        // -- The editor: the label at the left pad, then the inset field,
        //    then the buttons after it. The field absorbs whatever the label
        //    and the buttons leave (floored so it stays a field — the box's
        //    own narrow-window rule, now measured against the row instead of
        //    a window margin), and its width is what places the cluster. --
        dlg.owner = AppState::ModalDialogOwner::Editor;
        // CEIL, for the reason the message width above takes it: the label's
        // measured width places the field after it, so it rounds UP and the
        // field can never start inside the label's last column.
        const int label_w = static_cast<int>(
            std::ceil(text_shape::shape_text_run(font, prefix.c_str()).width_px));
        // The field's frame is the PLAIN SUNKEN edge, two relief lines (the
        // chrome below).
        const int fbord = 2 * relief_line_px();
        // THE LABEL → FIELD GAP IS THE ROW'S OWN PAD (architect 2026-08-29):
        // the air between the window edge and the label, and the air between
        // the label and the field, are ONE number read twice — the separately
        // sampled 11 that stood here is retired. Every dialog editor's label
        // ("Setting:", "BPM:", the commit title's)
        // reads it.
        const int fx    = cx0 + label_w + pad;
        // The room a field may take before the buttons would have to give:
        // the cluster's cap, less its reserved left ring and the pad.
        const int field_room = (buttons_x_max - ring - pad) - fx;
        const int field_w = std::max(std::min(scaled_px(kModalFieldWidthPx),
                                              field_room),
                                     scaled_px(kModalFieldMinWidthPx, 1));
        // The width floor is the ONE thing that can push past the cap (a window
        // too narrow for label + field + buttons), and the cap is what stops
        // it there — the buttons stay whole and the field is the surface that
        // has already given everything it can.
        buttons_x0 = std::min(fx + field_w + pad + ring, buttons_x_max);
        const int field_h = scaled_px(kModalFieldHeightPx);
        const int field_y = content.y + (content.h - field_h) / 2;
        const GuiRect field_outer{fx, field_y, field_w, field_h};
        const GuiRect field_inner{fx + fbord, field_y + fbord,
                                  field_w - 2 * fbord, field_h - 2 * fbord};
        // TWO SEATS, EACH SOLVED ON THE BOX IT BELONGS TO. The FIELD'S INK
        // takes the FIELD's band, so the buffer sits centred in its own box
        // (the field metrics are ruled correct and this is what keeps them
        // so). THE LABEL TAKES THE BUTTONS' OWN SEAT — the same `btn_y` and
        // `btn_h` the button loop and the scrub read — because what the label
        // must read level with is the row's words, "Setting:" beside OK and
        // Cancel, and the two boxes are different heights centred in one band,
        // which land a row or two apart by their own parity at some scales
        // (the laptop-pixel field's 31 against the buttons' 32 put the label
        // visibly under the buttons at 225 % until this; both are 23 Windows
        // px since 2026-10-02, the field's edge two lines). Reading the
        // buttons' box makes the two
        // level at every scale by construction rather than by an authored
        // correction, which is why the label's own 1px drop is gone.
        const double baseline =
            redesign_baseline(font, static_cast<double>(field_y),
                              static_cast<double>(field_h));

        show_row_text(cr, font, static_cast<double>(cx0),
                      redesign_baseline(font, static_cast<double>(btn_y),
                                        static_cast<double>(btn_h)),
                      prefix.c_str(), palette().label);

        // THE FIELD CHROME (architect 2026-10-02, the Windows text field): a
        // PLAIN SUNKEN edge (EDGE_SUNKEN, two relief lines) on the theme's
        // FIELD ground (architect 2026-10-03; the flag editor is the selected
        // flag opened for edit and keeps no field pair), square. No outline
        // says hover or focus: a
        // FOCUSED FIELD SHOWS ITS CARET and nothing else, the caret painting
        // only while the ring has not stepped onto a button (below).
        //
        // FOCUS IS `modal_dialog_focus < 0` and needs no term of its own: on
        // an editor dialog -1 IS the field, the ring's own meaning for it
        // (AppState::modal_dialog_focus), which includes the open, where the
        // user is there to type.
        //
        // A REFUSED ENTER RECOLOURS NOTHING (architect 2026-10-03: "a red
        // outline and the card is redundant"; the red frame retired from both
        // editors): the edge stays its plain sunken pair and the face the
        // field ground, the whole text is selected (text_editor::refuse) so
        // the first keystroke replaces it, and the reason card the refusing
        // commit posts (the owner's own sentence, GuiFlagEditor::notifications
        // and the settings editor's equivalent) says why. The flag editor
        // takes the same rule (render_flag_editor_box).
        const bool field_focused = app.modal_dialog_focus < 0;
        paint_cell_rect(cr, field_inner, palette().field_ground);
        paint_relief_plain_sunken(cr, field_outer);

        const text_shape::ShapedRun run =
            text_shape::shape_text_run(font, ed->pending);
        const std::vector<double> bx_off =
            text_shape::byte_offsets_px(run, ed->pending.size());

        // THE FIELD SCROLLS HORIZONTALLY (architect 2026-08-13, at his live
        // test — `notes=` Tab recalls a long value and "the text field has no
        // viewport scroll... it should allow me to, just like in a regular
        // text editor, use left and right or home and end to go to the end of
        // the string"). The standing "the dialog editors do not scroll"
        // accepted cost, recorded here and at kModalFieldWidthPx, is RETIRED
        // BY THAT RULING: the caret and the editing always worked, only the
        // VIEW never followed, so a caret walked past the right pad went on
        // editing text nobody could see.
        //
        // The mechanism is the FLAG EDITOR'S, called not copied in the only
        // sense a painter can call one — the same state field
        // (text_editor::State::view_offset_px, whose contract and whose
        // minimal-travel rule live at that declaration) and the same four
        // lines of arithmetic, which is what keeps the two surfaces' scrolling
        // identical. Scroll only as far as the caret demands, in whichever
        // direction it left the window, then clamp to the run's own travel: a
        // caret walking right pushes the view right one glyph at a time and
        // walking back left pulls it back the same way, never jumping and
        // never showing blank space past the end of the text.
        //
        // ITS HOME IS THE EDITOR SESSION, not this painter and not the modal
        // stash, and that is the deliberate choice: the offset must survive
        // frame to frame (recomputing it from nothing each paint would jitter
        // a caret resting mid-string) and must die with the edit. enter() and
        // deactivate() already zero it, so it RESETS when a dialog opens and
        // when it closes, with no reset site of its own to keep in step — and
        // since each of the three dialog editors owns its own State, a change
        // of the stash's owner is structurally a change of offset too. A
        // prompt has no field and writes none.
        //
        // The caret's own column is RESERVED at the right edge, so the travel
        // is measured against (view_w - caret) rather than view_w: a caret at
        // end-of-text stops with its column inside the field instead of half
        // past it.
        const double pad_x    = scaled_px(kModalFieldPadXPx);
        const int    caret_px = scaled_px(1.0, 1);
        const double view_x0  = static_cast<double>(field_inner.x) + pad_x;
        const double view_w   =
            std::max(1.0, static_cast<double>(field_inner.w) - 2.0 * pad_x);
        const int cursor_pos = std::clamp(
            ed->cursor_pos, 0, static_cast<int>(ed->pending.size()));
        const double caret_off = bx_off[static_cast<size_t>(cursor_pos)];
        const double travel_w  = view_w - static_cast<double>(caret_px);
        double vo = ed->view_offset_px;
        if (caret_off - vo < 0.0)      vo = caret_off;
        if (caret_off - vo > travel_w) vo = caret_off - travel_w;
        const double max_vo =
            run.width_px + static_cast<double>(caret_px) - view_w;
        if (vo > max_vo) vo = max_vo;
        if (vo < 0.0)    vo = 0.0;
        ed->view_offset_px = vo;

        const double tx = view_x0 - vo;

        // PUBLISH the click-to-caret geometry: origin at pending's byte 0,
        // the same shape as FlagEditorBox's pair, so editor_byte_index_at
        // searches this exactly as it searches the flag editor's. IT CARRIES
        // THE SCROLL OFFSET, which is the whole reason it is an origin and not
        // a pad: byte 0 is where byte 0 PAINTS, so a click, the F2.1 text drag
        // and the double-click's word select all land on the byte under the
        // pointer however far the field has travelled — the same rule, and the
        // same one origin, the flag editor's scrolled box has always followed.
        AppState::DialogEditorText& out = app.dialog_editor_text;
        out.valid         = true;
        out.text_origin_x = tx;
        out.byte_x        = bx_off;

        const int band_y =
            static_cast<int>(std::nearbyint(baseline -
                                            gui_font_ascent_px(font)));
        const int band_h =
            static_cast<int>(std::nearbyint(gui_font_line_px(font)));

        // Everything from here paints CLIPPED TO THE TEXT VIEWPORT — the band
        // between the pads, not the whole interior — so scrolled-out glyphs,
        // the selection highlight and the caret all stop where the ink is
        // allowed to start instead of bleeding into the pad the border needs.
        // It is the clip that hides the overflow, and the PUBLISHED byte
        // geometry above stays the painter's UNCLIPPED truth: a click outside
        // the visible run still resolves through the nearest-boundary search,
        // exactly as it did before the field travelled.
        cairo_save(cr);
        cairo_rectangle(cr, view_x0, static_cast<double>(field_inner.y),
                        view_w, static_cast<double>(field_inner.h));
        cairo_clip(cr);

        const bool   has_sel = text_editor::has_selection(*ed);
        const size_t s0 = static_cast<size_t>(text_editor::selection_start(*ed));
        const size_t s1 = static_cast<size_t>(text_editor::selection_end(*ed));
        // THE SELECTION IS THE THEME'S SELECTED PAIR OVER THE FIELD PAIR
        // (architect 2026-10-03, unified fields; render.h's palette block) —
        // the selected fill behind the selected substring, the selected text
        // for its glyphs, the field text for the rest: the flag editor's
        // marker-lane box paints the same four. ONE INK PER PIXEL, the flag
        // editor's rule (render_flag_editor_box): with a selection standing,
        // the field-text run is clipped to the band's complement and the
        // selected run to the band, two disjoint regions, so every
        // antialiased edge blends against exactly the ground it sits on.
        if (has_sel) {
            const int hx0 = static_cast<int>(std::nearbyint(tx + bx_off[s0]));
            const int hx1 = static_cast<int>(std::nearbyint(tx + bx_off[s1]));
            const int hw  = (hx1 > hx0) ? (hx1 - hx0) : 1;
            cairo_save(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            set_palette_source(cr, palette().selected_fill);
            cairo_rectangle(cr, hx0, band_y, hw, band_h);
            cairo_fill(cr);
            cairo_restore(cr);
            // The complement: the columns left and right of the band, and the
            // rows above and below it (the band is the face's line, not the
            // field's interior), as one clip path.
            cairo_save(cr);
            const double fy0 = static_cast<double>(field_inner.y);
            const double fy1 = static_cast<double>(field_inner.y + field_inner.h);
            cairo_rectangle(cr, view_x0, fy0, hx0 - view_x0, fy1 - fy0);
            cairo_rectangle(cr, hx0 + hw, fy0,
                            (view_x0 + view_w) - (hx0 + hw), fy1 - fy0);
            cairo_rectangle(cr, hx0, fy0, hw, band_y - fy0);
            cairo_rectangle(cr, hx0, band_y + band_h, hw,
                            fy1 - (band_y + band_h));
            cairo_clip(cr);
            set_palette_source(cr, palette().field_text);
            text_shape::show_shaped_run(cr, run, tx, baseline);
            cairo_restore(cr);
            cairo_save(cr);
            cairo_rectangle(cr, hx0, band_y, hw, band_h);
            cairo_clip(cr);
            set_palette_source(cr, palette().selected_text);
            text_shape::show_shaped_run(cr, run, tx, baseline);
            cairo_restore(cr);
        } else {
            set_palette_source(cr, palette().field_text);
            text_shape::show_shaped_run(cr, run, tx, baseline);
        }
        // THE CARET IS THE FIELD'S FOCUS, SO IT PAINTS ONLY WHILE THE FIELD HAS
        // IT (architect 2026-08-13, at his live test: "the blinking caret, the
        // I-beam, continues to blink in the text field even though it has lost
        // focus"). `field_focused` is the ring's own -1, resolved above, so
        // this needs no term of its own; the caret is the field's ONE focus
        // cue (architect 2026-10-02). THE SELECTION HIGHLIGHT IS
        // DELIBERATELY NOT GATED: it is buffer STATE, not focus, and it must
        // still be visible when the user walks back onto the field to act on
        // it. The blink's TICK carries the same gate (main.cpp), so an
        // unfocused field wakes the loop for nothing either.
        if (field_focused && text_editor::cursor_visible_now(*ed)) {
            // The caret's column is the one the scroll arithmetic RESERVED
            // above, so the two cannot disagree about how wide it is: the
            // travel stops with exactly this many pixels of room left at the
            // right pad, and the caret fills exactly them.
            const double caret_x = tx + caret_off;
            cairo_save(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            // THE CARET IS ITS FIELD'S TEXT (architect 2026-10-03).
            set_palette_source(cr, palette().field_text);
            cairo_rectangle(cr, static_cast<int>(std::nearbyint(caret_x)),
                            band_y, caret_px, band_h);
            cairo_fill(cr);
            cairo_restore(cr);
        }
        cairo_restore(cr);   // the field clip

        dlg.field = field_inner;
    }

    // -- The button row. EVERY BUTTON PAINTS AT ITS OWN x (2026-08-28, the
    //    player's relaid row): the single left-flushed cluster is laid out
    //    here, in the walk that has always laid it out, and the PLAYER's
    //    branch above has already written all seven of its own — one walk, two
    //    arrangements, and no second painter. --
    if (!player_up) {
        int cluster_x = buttons_x0;
        for (size_t i = 0; i < plan.size(); ++i) {
            if (i > 0) cluster_x += bgap;
            plan[i].x = cluster_x;
            cluster_x += plan[i].w;
        }
    }
    for (size_t i = 0; i < plan.size(); ++i) {
        const int x = plan[i].x;
        const GuiRect r{x, btn_y, plan[i].w, btn_h};
        // THE FACE IS THE ROSTER'S PAINTER IN ITS PUSH-BUTTON FAMILY (architect
        // 2026-10-02; the one painter is paint_button_box): a PLAIN RAISED box
        // at rest, word button and glyph button alike; the row's one checked
        // button (Repeat One, `lit`) PLAIN SUNKEN over the Hilight dither; a
        // LIVE PRESS — armed with the pointer inside it, or armed from the
        // keyboard — PLAIN SUNKEN on the ground with the glyph or label one
        // Windows px down and right, winning over the checked face while
        // held. THE FOCUS IS
        // THE WINDOWS DEFAULT-BUTTON FRAME: one DkShadow line round the
        // OUTSIDE of the box, in the reserved band (kModalFocusFramePx), worn
        // by the ACTIVELY focused button and by the PASSIVELY focused one —
        // Enter's target while a field has the keyboard — alike (the planner's
        // reading of the Windows grammar, 2026-10-02, which the architect has
        // not yet seen on the glass). The two focus strengths stay in the
        // state (AppState::modal_dialog_focus_active), where the keyboard's
        // walk reads them; the picture no longer tells them apart. A PRESSED
        // BUTTON THAT HOLDS THE FOCUS — every pointer press since 2026-10-03
        // (arm_modal_dialog_press) — wears both, the frame in the band
        // outside and the pressed face inside, as Windows draws a focused
        // pressed button: the two compose with no case of their own. NO
        // HOVER FACE: the pointer walk (update_modal_dialog_hover) drives the
        // tooltip alone.
        // THE DISABLED RUNG, on the PLAYER's buttons alone (architect
        // 2026-08-30) — every other owner's buttons are always live while
        // their dialog stands and publish enabled=true, and the player's are
        // all glyph buttons: the glyph ENGRAVED (icons::draw_engraved), the
        // box keeping its edge, the roster's rule (architect 2026-10-02:
        // Windows' DSS_DISABLED replaced the disabled mix). The press face is gated on
        // the bit rather than trusted — a button can go dead UNDER a hold —
        // while the focus frame is not: a stale focus must stay visible where
        // the keyboard is.
        const bool enabled = plan[i].enabled;
        const bool armed   = enabled &&
                             static_cast<int>(i) == app.modal_dialog_pressed;
        const bool pressed = enabled &&
            ((armed && app.modal_dialog_press_inside) ||
             static_cast<int>(i) == app.modal_dialog_key_pressed);
        const bool focused = static_cast<int>(i) == app.modal_dialog_focus;
        if (focused)
            paint_relief_line_frame(
                cr, GuiRect{r.x - ring, r.y - ring, r.w + 2 * ring,
                            r.h + 2 * ring},
                palette().dk_shadow);
        const ButtonBoxFace box = paint_button_box(
            cr, r, plan[i].lit, pressed, ButtonFamily::Push);
        if (plan[i].glyph) {
            // THE GLYPH, the icon row's own draw: the roster's 16-px glyph at
            // the toolbar case's (3, 3) — the box is that case
            // (bottom_row_seats), so each glyph stands where row 8's does
            // (architect 2026-10-05) — each path in its own colour, or
            // engraved when the button is disabled; draw_cased takes the
            // case's corner (r.x, r.y), not a (3, 3) glyph origin.
            const int glyph_px  = icon_glyph_px();
            if (enabled)
                icons::draw_cased(cr, plan[i].icon, r.x, r.y,
                                  static_cast<double>(glyph_px), box.shift);
            else
                icons::draw_cased_disabled(cr, plan[i].icon, r.x, r.y,
                                           static_cast<double>(glyph_px),
                                           box.shift,
                                           static_cast<double>(relief_line_px()));
        } else {
            // CENTRED, Windows' push-button text (kModalBtnMinWidthPx); a
            // label that set its button's width lands on its left pad. A
            // DISABLED label is THE DISABLED EMBOSS (architect 2026-10-03,
            // Windows' DSS_DISABLED), the glyph's rule for the word.
            const double lx = static_cast<double>(
                r.x + (r.w - plan[i].label_w) / 2 + box.shift);
            const double ly =
                redesign_baseline(font, static_cast<double>(r.y),
                                  static_cast<double>(r.h)) +
                static_cast<double>(box.shift);
            if (enabled)
                show_row_text(cr, font, lx, ly, plan[i].label,
                              palette().label);
            else
                show_row_text_embossed(cr, font, lx, ly, plan[i].label);
        }
        AppState::ModalDialogButton out;
        out.rect         = r;
        out.response_key = plan[i].response_key;
        out.editor_ok    = plan[i].editor_ok;
        out.player_act   = plan[i].player_act;
        out.enabled      = plan[i].enabled;
        // THE PLAYER'S ENABLED BIT IS PUBLISHED AS PAINTED (architect
        // 2026-09-24, strictly as-painted): the claim reads this bit and the
        // per-tick comparator (main.cpp) holds it against the live predicate,
        // so it must be what these pixels show — publish_button_face's rule on
        // this surface. This body runs whole under any damage touching the
        // lane (the player's clock and scrub damage at scanner cadence), so a
        // button whose drawable rect the clip does not cover KEEPS the bit it
        // was last painted with (same session, same slot, same act), and
        // with no such paint to keep it is false — nothing painted, nothing
        // clickable — which the comparator sees as drift and repaints within
        // a tick. Every other owner's buttons publish a constant true and
        // need no gate.
        if (player_up && !clip_covers_drawable(cr, app, r)) {
            const bool carry = !face_state_reset &&
                               i < prev_enabled.size() &&
                               prev_enabled[i].first == out.player_act;
            out.enabled = carry && prev_enabled[i].second;
        }
        // THE HINT, composed from the word and the DISPATCH (2026-08-13, the
        // ruling that took the accelerators off the labels and put the key on
        // a tooltip): the composer is the one owner of the format and of the
        // key's spelling (modal_dialog_button_hint, app_state.h) and it reads
        // the very fields the click and the ring's Enter dispatch on, so a
        // button cannot advertise a key it does not send. Published with the
        // rect because the WORD is the half the pointer path cannot re-derive.
        out.tooltip = plan[i].tooltip.empty()
                          ? modal_dialog_button_hint(plan[i].label,
                                                     plan[i].response_key,
                                                     plan[i].editor_ok)
                          : plan[i].tooltip;
        // THE MODIFIER LINE (2026-08-28), published beside it and empty on
        // every button with no shifted twin — the roster hint's `line2` over
        // this surface, and its one producer is the player's plan above.
        out.tooltip2 = plan[i].tooltip2;
        dlg.buttons.push_back(out);
    }

    // THE MODAL'S SURFACE IS THE LANE — its top row included, because that is
    // the rectangle the modal owns and the rectangle its damage must erase.
    // THE SESSION IS STAMPED HERE, one write for both branches at the moment
    // the geometry becomes readable: it is the id of the surface these rects
    // belong to, and every input site that reads them compares it against the
    // live one (modal_dialog_stash_current, input_pointer.cpp).
    dlg.box     = lane;
    dlg.session = live_session;
    dlg.valid   = true;
    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_onscreen_keyboard ----------------------------
//
// THE GLASS'S KEY SURFACE (2026-08-27). Contract, gate and paint order are at
// the declaration; the layout table, the geometry walk and the two lamps are at
// onscreen_keyboard.h, which this body reads and never restates.
//
// THE KEYS ARE THE ROSTER'S BUTTONS (architect 2026-10-02, the Windows-95
// design; the one painter is paint_button_box in its TOOLBAR family): each
// key a SOFT RAISED box on the ground with its cap in the theme's label, the
// band's ground between and around them the bottom row's — so the keyboard
// and the row it sits on read as one block, which is why the band paints NO
// LINE at its own top edge (architect 2026-08-27, on glass). THE OTHER TWO
// FACES:
//   ARMED     — the CHECKED face: SOFT SUNKEN over the Hilight dither, the cap
//               one Windows px down and right. Worn by SHIFT while it is armed and by the
//               SYMBOL-MODE key while a symbol page stands — the two lamps.
//               IT IS ALL SHIFT'S ARM HAS TO SAY ITSELF WITH now that the cap
//               is the word "Shift" in both states — that, and the letter
//               caps, every one of which turns capital while the arm stands.
//   PRESSED   — SOFT SUNKEN on the ground with the same shift, the roster's pressed
//               face, AND IT WINS OVER ARMED while the finger is down, the
//               roster's own rule for the same collision.
// NO HOVER FACE AND NO TOOLTIP: this surface exists on a platform with no
// pointer, so a hover has no producer and a hint has nowhere to hang.
//
// EVERY CAP IS TEXT (architect 2026-08-27, on glass — the Breeze glyphs the
// function keys wore for a day read oversized beside the letter caps): the
// function keys, Space and the two page keys say their words, the character
// keys their characters (symbols 2/2's past ASCII too, UTF-8 encoded through
// text_editor::encode_utf8, the editors' own encoder), all on the one sans
// face through the one shaping chokepoint. This surface draws no icon.
//
// STATE-AXIS: every one of the three faces is decided ONCE per key, from the
// two lamps and the one held index this body reads at its head, and the cap
// comes from the layout table's own derivations (cap_word, shifted_char). There
// is no second list of what a key looks like anywhere.
void GuiPaintHandler::paint_onscreen_keyboard(cairo_t* cr,
                                              const GuiRect& exposed) {
    // (The slot's as-painted bit is written by paint_keyboard_slot, the
    // dispatch that calls this — the keyboard shares its band with the folder
    // overlay since 2026-08-28 and one bit describes the slot.)
    const bool    standing = onscreen_keyboard::stands(app, gui);
    const GuiRect surf     = onscreen_keyboard::surface_rect(app);
    if (!standing) return;
    if (surf.w <= 0 || surf.h <= 0) return;
    // A PARTIAL exposure still paints, and correctly — the clip bounds it. Only
    // the BIT above waits for a full one.
    if (!rects_intersect(exposed, surf)) return;

    // The keyboard's state, READ ONCE here so every key below is painted
    // against one answer. THIS BODY ONLY READS IT: the reset that follows a
    // change of the live editor session has its own owner
    // (onscreen_keyboard::reconcile_session, whose own declaration names its
    // callers), and it is deliberately not called from here — the exposure
    // gates above would decide when a painter noticed the change, and a painter
    // may declare no damage.
    const AppState::OnscreenKeyboard& kb = app.onscreen_keyboard;
    const onscreen_keyboard::Page page        = kb.page;
    const bool                    shift_armed = kb.shift_armed;
    const int                     held        = kb.pressed_key;

    cairo_save(cr);

    // The lane's ground, one fill across the whole band and NO LINE AT ITS TOP
    // EDGE: the band's ground is the bottom row's ground, so the two lanes read
    // as one block and a seam between them would draw a border through the
    // middle of it.
    set_palette_source(cr, palette().ground);
    cairo_rectangle(cr, surf.x, surf.y, surf.w, surf.h);
    cairo_fill(cr);

    // THE ONE SANS FACE at the product's ONE text size, selected through the
    // one face owner and shaped through the one chokepoint (text_shape.h) like
    // every other label in the product. EVERY key on this surface takes it —
    // the character caps, the page keys' words and the function keys' words
    // alike — so there is no second way a cap can be drawn. A cap is one to
    // nine glyphs, which are the cheapest runs there are.
    const GuiFont font = gui_font(GuiFace::Body);

    onscreen_keyboard::for_each_key(
        app, page,
        [&](uint32_t index, const onscreen_keyboard::KeyDef& k,
            const GuiRect& r) {
            using Role = onscreen_keyboard::Role;
            // PER-KEY EXPOSURE. The band's ground is laid across the whole
            // surface above (one fill, bounded by the outer damage clip), but a
            // KEY costs a face box and a shaped run, so a narrow damage
            // crossing this band pays for the keys it actually crosses and for
            // no other. That matters at the panel's tick rate:
            // the scanner's own damage is a column a few pixels wide, and
            // without this test every one of them painted every key on the
            // page (thirty-five to thirty-seven).
            if (!rects_intersect(exposed, r)) return;

            const bool pressed = (static_cast<int>(index) == held);
            const bool armed   =
                (k.role == Role::Shift      && shift_armed) ||
                (k.role == Role::SymbolMode &&
                 page != onscreen_keyboard::Page::Letters);

            // The three faces (the block above), through the roster's one
            // button painter.
            const ButtonBoxFace box = paint_button_box(
                cr, r, armed, pressed, ButtonFamily::Toolbar);

            // THE CAP: the word the table's one cap owner answers (cap_word —
            // the function keys, Space and the page keys), or, where it
            // answers none, the character key's own character through the ONE
            // case derivation (shifted_char) — never a second uppercase table
            // — UTF-8 encoded by the editors' one encoder, so a cap is spelled
            // exactly as the key's press inserts it.
            std::string one;
            const char* cap = onscreen_keyboard::cap_word(k, page);
            if (cap == nullptr) {
                one = text_editor::encode_utf8(
                    onscreen_keyboard::shifted_char(k.ch, shift_armed));
                cap = one.c_str();
            }
            const text_shape::ShapedRun run =
                text_shape::shape_text_run(font, cap);
            const double cap_x = std::nearbyint(
                r.x + (r.w - run.width_px) * 0.5) + box.shift;
            const double baseline = redesign_baseline(
                font, static_cast<double>(r.y), static_cast<double>(r.h)) +
                box.shift;
            set_palette_source(cr, palette().label);
            text_shape::show_shaped_run(cr, run, cap_x, baseline);
        });

    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_keyboard_slot ----------------------------------

void GuiPaintHandler::paint_keyboard_slot(cairo_t* cr, const GuiRect& exposed) {
    // THE AS-PAINTED BIT, AND IT DESCRIBES PIXELS — the roster publisher's own
    // rule (publish_button_face, above), taken for its own reason. The tick
    // comparator (main.cpp) pays the slot's show and hide off the drift
    // between the live answer and this bit, and on_redraw runs ONCE PER DAMAGE
    // RECT under a clip to exactly that rect, so a bit stamped from a frame
    // whose rect never touched the band would be a claim about pixels this pass
    // did not write. Both edges break on that:
    //   * the SHOW — an editor or the player opens, its own route damages the
    //     marker lane or the bottom row and nothing else, and a bit stamped
    //     true there would let the comparator see agreement while the band
    //     still shows waveform;
    //   * the HIDE — the tenant closes, some unrelated narrow damage paints
    //     first, and a bit stamped false there would leave the tenant's
    //     pixels standing with nothing left to erase them.
    // So it refreshes only on a rect that FULLY COVERS the band, which is the
    // frame the comparator's own damage produces; every other frame leaves it
    // alone and the drift survives to be repaired. (The player's open and
    // close damage the whole window, so their first frame always covers.)
    // THE BAND IS THE STANDING TENANT'S OWN (2026-08-28): the overlay's is the
    // CEILING BAND every time it stands — the icon row's foot through the
    // bottom row's top since 2026-09-09, whatever its listing's length (its
    // fixed height,
    // the architect's ceiling) — and the keyboard's
    // is its four key rows,
    // so "the band" is a question with two answers and the bit describes
    // the pixels THIS frame may have written. With NEITHER standing the band
    // to cover is the slot's tallest — the hide's own damage, which is what
    // has to erase the departed tenant.
    const bool keyboard = onscreen_keyboard::stands(app, gui);
    const bool overlay  = folder_overlay::stands(app);
    const GuiRect surf  = overlay  ? folder_overlay::surface_rect(app)
                        : keyboard ? onscreen_keyboard::surface_rect(app)
                                   : onscreen_keyboard::slot_damage_rect(app);
    const bool covers_band =
        surf.w > 0 && surf.h > 0 &&
        exposed.x <= surf.x && exposed.y <= surf.y &&
        exposed.x + exposed.w >= surf.x + surf.w &&
        exposed.y + exposed.h >= surf.y + surf.h;
    if (covers_band) app.keyboard_slot_painted_standing = keyboard || overlay;
    // AT MOST ONE TENANT, structurally (the record is at
    // onscreen_keyboard::stands); the order below is free.
    if (overlay)       paint_folder_overlay(cr, exposed);
    else if (keyboard) paint_onscreen_keyboard(cr, exposed);
}

// -- GuiPaintHandler::paint_folder_overlay -----------------------------------
//
// THE KEYBOARD-SLOT LIST PANEL (2026-08-28), ONE PAINTER FOR EVERY CONTENT —
// the render player's folders and wavs and the Open project picker's project
// folders. Contract and gate are at the declaration; the geometry, the scroll
// clamp and the one row walk are at folder_overlay.h, which this body reads
// and never restates; the row table and every state bit are
// AppState::folder_overlay. EVERY ROW IS A LIST ROW in the redesign's SANS
// (the project picker's are all Folder rows, each taking its glyph from the
// row's KIND and its face from the same ladder the player's rows do, and the
// transport mark below is structurally absent under the picker — the player
// holds no item while a picker stands). (The AV Sync Stats panel's inert
// monospace TEXT rows, at a line's pitch, were a second row class from
// 2026-09-03 to 2026-09-30 and went with the panel.)
//
// THE BAND IS WINDOWS 95'S LIST VIEW, A SUNKEN FIELD (architect 2026-10-06,
// over the 2026-10-02 raised panel on the chrome ground — "like a file
// explorer, not popping out like a rising bevel"): the theme's FIELD GROUND
// inside the PLAIN SUNKEN two-line edge round the band (the dialog field's
// and the well's edge), the names and the text-class glyph paths in the
// FIELD TEXT, each row 17 Windows px with the roster's 16-px glyph
// (folder_overlay.h's row box), and the faces Windows' list:
//   a RESTING row         -> NO FILL AT ALL: the field shows through
//   the HIGHLIGHT         -> a FLAT FILL in the theme's SELECTED PAIR, the
//                            selection, which is also the list's keyboard
//                            focus — ONE PAIR FOCUSED OR NOT (architect
//                            2026-10-03, render.h's palette block: the
//                            2026-09-02 inactive face retired)
//   PRESSED               -> the highlight's own face: a row press is an arm
//                            whose act is the lift, and the highlight is what
//                            it promises
// NO HOVER FACE (architect 2026-10-02). THE RING'S FOCUS ON THE LIST
// (`list_focused`) is the dialog's focus frame round the highlighted row — one
// DkShadow line outside it, the frame a focused dialog button wears (the
// planner's reading of the Windows grammar, 2026-10-02, judged on the glass).
//
// TWO MARKS, ONE ROW EACH AND POSSIBLY THE SAME ROW: the HIGHLIGHT band (the
// list's keyboard focus — what Enter and Load in place act on, and nothing
// else: Space and the Play button answer the TRANSPORT and never read the
// highlight)
// and the TRANSPORT'S ITEM, whose wav glyph is swapped for the roster's
// MediaPlaybackStart (the one-button-two-faces precedent) whether the item is
// live or paused.
//
// STATE-AXIS: the face is decided ONCE per row from the highlight, the press
// arm and the ring, and the glyph from the row's kind and the
// item's path; there is no second list of what a row looks like anywhere.
void GuiPaintHandler::paint_folder_overlay(cairo_t* cr, const GuiRect& exposed) {
    if (!folder_overlay::stands(app)) return;
    const GuiRect surf = folder_overlay::surface_rect(app);
    if (surf.w <= 0 || surf.h <= 0) return;
    if (!rects_intersect(exposed, surf)) return;

    const AppState::FolderOverlay& ov = app.folder_overlay;
    const AppState::RenderPlayer&  rp = app.render_player;

    cairo_save(cr);
    // The band's FIELD and its PLAIN SUNKEN edge on the surface's outer two
    // lines (the block above); then EVERYTHING ELSE UNDER THE CONTENT RECT'S
    // CLIP, because a scrolled listing's first and last rows straddle the
    // content's edges and must not paint into the waveform above, the bottom
    // row below, or the band's own edge.
    paint_cell_rect(cr, surf, palette().field_ground);
    paint_relief_plain_sunken(cr, surf);
    // THE ROW WALK'S CLIP IS THE CONTENT RECT AND row_at'S CONTAINMENT IS THE
    // SAME RECT (folder_overlay.h) — the surface inside its edge — so paint
    // and hit agree about every pixel at any scroll offset. The surface stays
    // the field, the damage and the band's outer claim.
    const GuiRect content = folder_overlay::content_rect(app);
    cairo_rectangle(cr, content.x, content.y, content.w, content.h);
    cairo_clip(cr);

    // THE FACE: both contents list NAMES — files and projects — and take the
    // redesign's sans at the redesign's size like every other row in the
    // product. THE SANS-FOR-LISTINGS RULING IS HIS, AND IT IS A REVERSAL OF
    // HIS OWN (2026-09-03): the player's and the picker's listings took the
    // monospace for a few hours as the cheap first answer to their width
    // asymmetry, and at his first look he put them back — "revert the
    // monospace back to sans-serif proportional on the picker and player".
    // SHAPE WITH THE FONT YOU PAINT WITH: the handle below is taken AFTER the
    // selection, and this painter selects once and never again — nothing
    // after this call reads a face it did not select, so the borrowed handle
    // stays valid for the whole walk.
    const GuiFont font = gui_font(GuiFace::Body);

    const int    glyph  = folder_overlay::row_icon_px();
    const int    gap    = folder_overlay::row_icon_gap_px();
    const int    inset  = folder_overlay::row_icon_inset_px();
    // THE HIGHLIGHT IS THE THEME'S SELECTED PAIR, focused or not (the block
    // above).
    const GuiPalette& pal = palette();

    folder_overlay::for_each_row(
        app, [&](int index, const AppState::FolderOverlayRow& row,
                 const GuiRect& r) {
            // PER-ROW EXPOSURE, the keyboard's own economy: a row costs a face
            // box, a glyph and a shaped run, so a narrow damage pays only for
            // the rows it crosses.
            if (!rects_intersect(exposed, r)) return;
            if (!rects_intersect(content, r)) return;

            const bool highlighted = index == ov.highlight_row;
            const bool pressed     = ov.press.armed &&
                                     ov.press.row == index &&
                                     ov.press.inside && !ov.press.scrolling;
            // THE FACE (the block above): LIT — the highlight, or a live
            // press arm promising it — is the flat selected fill; a row that
            // is neither takes NO FILL and the band's field shows through it.
            const bool lit = pressed || highlighted;
            if (lit) paint_cell_rect(cr, r, pal.selected_fill);
            // THE LIST'S FOCUS: the focus frame one line outside the
            // highlighted row while the modal ring stands on the list (the
            // block above); the rows' one-Windows-px gap and pad hold it.
            if (highlighted && ov.list_focused) {
                const int fl = relief_line_px();
                paint_relief_line_frame(
                    cr, GuiRect{r.x - fl, r.y - fl, r.w + 2 * fl, r.h + 2 * fl},
                    palette().dk_shadow);
            }

            // THE GLYPH: the folder for folder rows, the wav for wav rows —
            // swapped for the transport glyph on the item's row. (The UP
            // kind, the `..` row, wore the folder glyph because it named a
            // folder, and went with the player's move inside `tmp/` on
            // 2026-09-01.) At the toolbar case's 3-px lead from the row's left
            // edge and centred in its height (folder_overlay.h's row box).
            const int gx = r.x + inset;
            icons::Icon icon = icons::Icon::Folder;
            if (row.kind == AppState::FolderOverlayRow::Kind::Wav) {
                icon = (!rp.item.empty() && row.path == rp.item)
                           ? icons::Icon::MediaPlaybackStart
                           : icons::Icon::AudioXWav;
            }
            const int gy = r.y + (r.h - glyph) / 2;
            // THE GLYPH KEEPS ITS OWN COLOURS ON EVERY ROW, lit or resting
            // (architect 2026-10-06: every icon's inks are the drawing's
            // own, period pixel art no ground recolours — icons.h's head);
            // only the name takes the row's text pair.
            icons::draw(cr, icon, static_cast<double>(gx),
                        static_cast<double>(gy), static_cast<double>(glyph));
            const int text_x = gx + glyph + gap;

            // THE NAME, shaped through the one chokepoint, after the glyph —
            // the field text on a resting row (architect 2026-10-06, the
            // field pair) and the selected text on a lit one.
            const text_shape::ShapedRun run =
                text_shape::shape_text_run(font, row.name);
            // THE SEAT: a row is a box, so its label takes the box solver's
            // cap centring.
            const double baseline =
                redesign_baseline(font, static_cast<double>(r.y),
                                  static_cast<double>(r.h));
            // A NAME TOO LONG FOR THE LINE RUNS OFF THE EDGE (2026-08-28: no
            // wrap, no ellipsis — "project and file names will be short"), and
            // the clip to the ROW's own right edge is what "off the edge" means
            // here: the glyphs stop at the row, not at the window.
            cairo_save(cr);
            cairo_rectangle(cr, text_x, r.y,
                            std::max(0, (r.x + r.w) - text_x), r.h);
            cairo_clip(cr);
            set_palette_source(cr, lit ? pal.selected_text : pal.field_text);
            text_shape::show_shaped_run(
                cr, run, static_cast<double>(text_x), baseline);
            cairo_restore(cr);
        });

    cairo_restore(cr);
}

// -- GuiPaintHandler::on_redraw ------------------------------------------

void GuiPaintHandler::on_redraw(cairo_t* cr, int x, int y, int w, int h) {
    // (The monospace grid measure that opened every frame is gone with the face
    // — row 7. Nothing in the product measures text outside a shaping pass now,
    // and every such pass owns its own font selection.)

    // THE FLAG STASH PROMOTES WITH THE FLAG SURFACE (architect 2026-09-24,
    // strictly as-painted; the contract is at AppState::flag_hit_rects). The
    // rebuild staged the pair when it drew the offscreen surface; this frame
    // is the one that blits that surface, so the pair the hit walk and the
    // stem painter read becomes the one the rebuild published HERE, at the top
    // of the frame, before the stems below paint from it. Once per stage —
    // the bit clears on the frame's first damage rect, so the frame's later
    // rects are no-ops. UNCONDITIONAL, deliberately unlike the basis promote
    // that follows: the surface blits whatever the freeze holds, so its stash
    // does too. A swap keeps both vectors' capacity; the staged side's stale
    // contents are cleared by the lane painter's own first act at the next
    // rebuild and read by nobody until then.
    if (app.flag_stash_staged) {
        std::swap(app.flag_hit_rects, app.staged_flag_hit_rects);
        std::swap(app.marker_stems, app.staged_marker_stems);
        app.flag_stash_staged = false;
    }

    // Event-synchronized hit geometry, PROMOTE phase (ruling at the selector):
    // done at the TOP of the frame, BEFORE any painting, so the flag cache this
    // frame blits (blit-only below) AND the overlays this
    // frame paints around it (the live trim pass, the flag editor overlay, the
    // marker stems, the
    // playhead) all land on the SAME map — the one the committed items were
    // built against. Promoting at the frame's END instead let the overlays paint
    // against the OLD map on the very frame that first blit the rebuilt cache,
    // then advanced the map silently with no further damage, so those overlay
    // pixels could stay misplaced (and a stationary hover could keep naming a
    // marker whose flag had moved away). Promote the staged value the last item
    // rebuild left, once — staged_displayed_valid clears on the first damage rect
    // of the frame, so the remaining rects are no-ops; idle frames with no staged
    // value do nothing. A rebuild always invalidates its item region, so the
    // committing frame's damage always includes the items — TRUE OF EVERY
    // PROMPT PROMOTE, and repaired rather than assumed for the DEFERRED one
    // below, whose committing frame is some later frame: the deferral records
    // the owed rect on deferred_basis_repaint_due (app_state.h) and the promote
    // is BLOCKED until that bit is honored, so the committing frame is by
    // construction one carrying the full strip+waveform rect. No input dispatches
    // mid-loop (single-threaded) and the whole frame still commits atomically
    // after the loop in GuiPlatform::paint_one_frame, so a press only ever reads
    // the last COMMITTED frame's geometry — that guarantee is unchanged.
    //
    // NOTHING SUBSCRIBES TO THIS EDGE any more, and it no longer publishes one:
    // the hover refresh hook that hung off it re-resolved the marker hover cache
    // against the just-promoted map, and row 5 deleted the hover cache; the
    // displayed_map_gen counter that outlived it as a bare record went too
    // (2026-08-02, write-only). The overlays below simply read the promoted
    // basis. A future subscriber hangs its key here, on this block.
    //
    // THE PROMOTE ITSELF IS INSIDE THE DISPLAYED-BASIS FREEZE (2026-08-22, the
    // THIRD consumer of displayed_basis_frozen — membership at the predicate,
    // app_state.h, not restated here). THE ORDERING THIS CLOSES: an item
    // rebuild stages a NEW {map, viewport} pair and queues its damage in one
    // loop iteration; before the compositor returns the scheduled frame
    // callback a pointer press arrives and arms pending_marker_press /
    // pending_trim_drag; the frame callback then runs this block and would
    // promote the new pair UNDER THE AIM — the press decided its subject and
    // stored its press_x against the OLD promoted basis, and the grab-gate
    // crossing (grab_moved_threshold_px)
    // would convert that stored column through the NEW one, which is exactly
    // the press-to-crossing epoch split the worker's dispatch freeze and
    // completion drop exist to prevent (they cover a job dispatched or
    // completing inside the freeze; they cannot reach a pair already staged
    // before the press). It is reachable: a frame callback and a pointer event
    // can arrive in one Wayland batch in pointer-before-frame order, so "no
    // input dispatches mid-paint" does not exclude it.
    //
    // SO THE PROMOTE DEFERS, IT DOES NOT DROP — the contrast with the
    // completion drop is the point. The drop DISCARDS a finished render because
    // the next unfrozen dispatch re-derives it from the store; the staged pair
    // here is already-done, still-current work with no producer left to re-run
    // it, so only its VISIBILITY waits: it sits staged, every frame through the
    // gesture paints the previously promoted epoch (so paint and hit stay on
    // the one basis the press was aimed in), and the first frame after the
    // freeze lifts AND the repair debt is honored promotes it normally.
    //
    // THE DEFERRAL'S ONE ACCEPTED SEAM: the staged pair's PIXELS are already
    // published (the plate blit and the flag cache's own surface are the
    // rebuild's, not this block's), so through the deferral window those two
    // layers show the newer epoch while the basis-derived surfaces — the live
    // trim pass, the flag editor's box — keep the older one. Every hit test
    // still answers what is on screen: the flag stash promoted above with its
    // surface, the trim bar's stash (AppState::trim_bar_hit) is what the trim
    // pass painted on the older basis, and the editor box is its own
    // painter's publication.
    // The window is at most the ONE stage that beat the press to the frame
    // callback, it can only open on an UNDRIVEN basis change (a worker publish
    // or a sync rebuild landing in that sub-frame gap), and it closes at the
    // gesture's end. That is strictly the drag-freeze reading the completion
    // drop already established — a gesture works in the epoch it was aimed in —
    // extended to the staged edge; the alternative, promoting, moves the
    // gesture's own anchor and produces a wrong first delta.
    // THE DEFER IS WHOLE-PAIR BY CONSTRUCTION: map and viewport promote in the
    // one block below, so gating the block defers both or neither — there is no
    // half-promoted basis for a hit test to read, and every reader (the two
    // basis accessors in app_state.h) keeps naming the same old pair for the
    // whole window.
    //
    // AND THE PROMOTE WAITS FOR THE DEBT TOO (2026-08-22, the same day's final
    // verification round): the deferral OWES a full strip+waveform repaint (the
    // bit set below), and a basis may not promote onto pixels the frame did not
    // repaint. So the promote's condition is the freeze AND the outstanding
    // debt — a standing debt defers exactly like a standing freeze. Without the
    // second condition the hole reopens the instant the gesture ends: a staged
    // pair defers under a motionless pending; playback's scanner frames consume
    // the stage's original full-rect damage out of BOTH shm buffers; the
    // motionless lift disarms the pending and damages nothing (its act ran at
    // the press); a frame callback already queued then runs BEFORE the timerfd
    // tick — the display fd is dispatched ahead of the timer in the run loop —
    // and, the freeze now being false, that scanner-narrow frame promotes. The
    // trim bar and the flag editor's box would still show the old epoch while
    // item_viewport_basis and displayed_or_live_target_map answer on the new
    // one, so a pointer event later in the SAME Wayland batch aims at a painted
    // endcap (its hit reads the trim pass's own stash, so the aim itself
    // lands) and the drag it arms converts on geometry that is not under it
    // — the epoch split this
    // family exists to prevent, seeding the next trim drag with the wrong
    // subject or delta. Deferring instead costs one more frame of the old epoch,
    // whole and self-consistent, and the stage owner's honor (waveform_cache.cpp,
    // at its first unfrozen invocation — whoever calls it, the free-running
    // tick included as the starvation backstop) clears the debt as it queues
    // the full rect, so the very next frame both promotes and repaints.
    if (app.staged_displayed_valid &&
        (displayed_basis_frozen(app) || app.deferred_basis_repaint_due)) {
        // The stage site's damage is being consumed by frames that paint the
        // OLD epoch, so the eventual promote owes the item-basis surfaces a
        // repaint that no queued rect covers any more. Record it; the stage
        // owner honors it at its first unfrozen invocation once the freeze
        // lifts (whoever calls it — the free-running tick is the starvation
        // backstop), and the promote above waits for that honor (contract at
        // the field). Damage cannot be
        // declared from inside a paint pass (the hazard is stated at
        // GuiPlatform::paint_one_frame's loop), which is the other half of why
        // the repair is a recorded bit rather than an invalidate here. Setting
        // an already-set bit is the ordinary case on the debt arm and costs
        // nothing.
        app.deferred_basis_repaint_due = true;
    } else if (app.staged_displayed_valid) {
        app.displayed_target_warp_frame_map =
            std::move(app.staged_displayed_target_warp_frame_map);
        app.staged_displayed_target_warp_frame_map.clear();
        // Promote the displayed VIEWPORT mirror in the SAME block (ONE promote):
        // the flag editor's box geometry advances to the fp_* viewport the
        // just-blitted flag cache was built against, in lockstep with the map
        // above.
        app.displayed_vp_start = app.staged_displayed_vp_start;
        app.displayed_vp_end   = app.staged_displayed_vp_end;
        app.displayed_area_w   = app.staged_displayed_area_w;
        app.staged_displayed_valid = false;
    }

    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);

    render_background(cr, x, y, w, h);
    // THE GROUND SPLIT: the chrome erase above covers the whole exposed rect;
    // the waveform area then takes its own `waveform_canvas` ground.
    // Unconditional and
    // ahead of every content branch, so a cold frame (loading, no audio, or a
    // null plate before the first worker publish) shows canvas where the
    // waveform will be rather than a chrome-colored hole. The outer clip already
    // bounds this to the exposed rect, so the full-rect fill costs nothing off
    // the damage. The rect is the EFFECTIVE-width waveform_area, so the <=15px
    // inert right gutter at a non-multiple-of-16 window stays chrome — it is
    // outside every grid-aligned surface and no waveform pixel ever paints there
    // (no gutter exists at 1920/2560/3840).
    {
        const GuiRect canvas = waveform_area(app);
        // AND NOT UNDER THE ON-SCREEN KEYBOARD. The painted rect's one owner is
        // onscreen_keyboard::waveform_paint_area (the rule is stated there):
        // the band is opaque and paints after everything below, so the ground
        // under it is a fill nobody ever sees. The rect handed to render_canvas
        // is still the WHOLE area — the well's lines are its geometry, and a
        // shortened rect would draw the bottom ones as lines across the
        // keyboard's top edge — so the band is subtracted with a
        // CLIP rather than with a smaller rectangle. Nothing at all on a
        // platform with no painted keyboard.
        const GuiRect painted =
            onscreen_keyboard::waveform_paint_area(app, gui);
        const bool band_cuts = painted.h < canvas.h;
        if (band_cuts) {
            cairo_save(cr);
            cairo_rectangle(cr, painted.x, painted.y, painted.w, painted.h);
            cairo_clip(cr);
        }
        render_canvas(cr, canvas.x, canvas.y, canvas.w, canvas.h);
        if (band_cuts) cairo_restore(cr);
    }

    // THE TWO REDESIGNED TOP BUTTON ROWS AND THE UNIFIED BOTTOM ROW PAINT
    // ON EVERY FRAME
    // CLASS, deliberately OUTSIDE
    // the loading / total>0 branches below: they are the surfaces with no
    // dependence on the loaded audio, and their buttons are claimed ABOVE the
    // pointer path's loading guard for exactly that reason. A button that is
    // clickable must be visible — painting it only in the total>0 branch would
    // leave the press claim live over a lane showing bare chrome during a load,
    // and dead on a cold launch where the row has never painted (the hit rects
    // are the painter's stash). Their own opaque grounds erase the chrome
    // render_background laid down.
    //
    // Each is gated on its OWN exposure rather than run unconditionally like the
    // canvas ground above: these passes shape labels through HarfBuzz, which the
    // outer Cairo clip would not elide, so a narrow per-frame playhead damage
    // must not pay for them. Nothing painted after this point touches the two
    // top button lanes (the flag cache is transparent over them, every other
    // pass owns a lane below them), so painting them first overdraws nothing.
    // GAP 1 has no painter: it is window ground, render_background's erase
    // above (main.cpp's vertical rule).
    //
    // THE BOTTOM ROW JOINS THEM (row 7): it is audio-independent in the same
    // sense — the timestamp reads 00:00.000 with no source, and the loading line
    // is one of the things the row carries, which is why the separate loading-
    // only draw that used to sit below is gone. Nothing painted later overlaps
    // it except the floating surfaces, which paint over everything by design.
    {
        const GuiRect exposed{x, y, w, h};
        // THE CAPTION (top lane 0, 2026-10-05) joins them on the same terms:
        // audio-independent, its buttons claimed above every veil the File
        // anchor passes, so it paints on every frame class its lane is
        // exposed on.
        if (rects_intersect(exposed, top_caption_row_area(app))) {
            paint_caption_row(cr);
        }
        if (rects_intersect(exposed, top_menu_row_area(app))) {
            paint_menu_row(cr);
        }
        if (rects_intersect(exposed, top_icon_row_area(app))) {
            paint_icon_row(cr);
        }
        // THE UNIFIED BOTTOM ROW (2026-08-12, rows 8 and 9 merged) is one
        // exposure and one painter: paint_bottom_strip grounds the lane and
        // paints the button cluster + clock through its
        // paint_bottom_row_buttons_and_clock half (audio-independent chrome
        // exactly like the two top rows — the press claim sits above the
        // pointer path's loading guard, and the painter publishes the hit
        // rects the claim reads, so it must paint on every frame class its
        // lane is exposed on). Its per-CELL exposure gate went with the status
        // chain on 2026-08-13: the row shapes no text but the clock's one
        // memoised cell now, so a narrow clock or button damage has nothing
        // left to be spared.
        if (rects_intersect(exposed, bottom_row_area(app))) {
            paint_bottom_strip(cr);
        }
    }

    if (audio.total_frames() > 0 && !app.loading) {
        const GuiRect area       = waveform_area(app);
        const GuiRect top_strip  = top_strip_area(app);
        const GuiRect exposed{x, y, w, h};

        // WHERE THE WAVEFORM'S PIXELS MAY LAND — `area` minus the on-screen
        // keyboard's opaque band, through that rule's one owner
        // (onscreen_keyboard::waveform_paint_area). `area` itself is unchanged
        // and stays what every pass below MAPS with; this rect is what they are
        // GATED on, and the clip below is what keeps a pass that straddles the
        // band's top edge from rasterizing the half nobody sees. Equal to
        // `area` on any platform with no painted keyboard, where both are
        // therefore exactly what they were.
        const GuiRect wave_paint =
            onscreen_keyboard::waveform_paint_area(app, gui);
        const bool    band_cuts  = wave_paint.h < area.h;
        if (band_cuts) {
            // TWO RECTANGLES, ONE CLIP: this branch paints in the TOP STRIP and
            // in the waveform and nowhere else, so the union of the two is the
            // whole region it is entitled to — and cairo clips to the union of
            // a path's rectangles. The passes that own pixels in BOTH lanes
            // (the trim bar, the cursor's head and stem) keep their strip half
            // whole and lose only what the band covers.
            cairo_save(cr);
            cairo_rectangle(cr, top_strip.x, top_strip.y, top_strip.w,
                            top_strip.h);
            cairo_rectangle(cr, wave_paint.x, wave_paint.y, wave_paint.w,
                            wave_paint.h);
            cairo_clip(cr);
        }

        // The live viewport / target-warp_frame_map computations live in the
        // cache rebuild paths (waveform via the worker, flags via
        // maybe_rebuild_flag_cache), not in on_redraw, which reads
        // wf_cache.fp_* for displayed-viewport inputs and treats the plate and
        // flag strips as blit-then-overlay paths. Trim is a live pass
        // (paint_trim) on the free item-basis owners.
        //
        // THE AUTHORITATIVE PAINT ORDER, bottom of the stack to top. This is
        // the ONE full enumeration in the tree — every other site states its
        // own pass's class plus a pointer here — and it is derived from the
        // call sequence below plus the two unconditional passes above this
        // branch:
        //   1. render_background — the chrome erase over the whole exposed
        //      rect (above, unconditional).
        //   2. render_canvas — the waveform area's ground AND THE WELL, its
        //      two lines top and bottom (above, unconditional).
        //   3. the two redesigned top button rows and the
        //      unified bottom row (its chrome, buttons, clock AND state cell
        //      in one painter),
        //      each on its own
        //      exposure (above, outside this branch; they own lanes nothing
        //      below them paints on).
        //   4. waveform plate -> the marker stems' FLANKS in the well's top
        //      lines (architect 2026-10-05) -> phase-reset overlay ring.
        //   5. LIVE TRIM, one pass, entirely inside the trim lane: the
        //      dithered track and the window's thumb.
        //   6. the MARKER STEMS (waveform).
        //   7. the CURSOR's WAVEFORM stem segment (paint_playheads — the head
        //      and the marker-lane run are the ruler pass's, step 9), over
        //      the marker stems and under the flags.
        //   8. the SCANNER (waveform).
        //   9. the RULER lane — etched ticks and labels — AND, in the same
        //      pass, the cursor's HEAD (WordPad's 9x8 marker, opaque) seated
        //      through the ruler with its tip row on the marker lane's own
        //      first row (marker_lane_air_px, one Windows px above the flag
        //      box) rather than flush on the ruler's own bottom: at 138 % its
        //      top stands 3 rows below the labels' baseline (no overlap), at
        //      275 % it rises 2 rows into the digits' own ink, and at 400 %
        //      it lands exactly on the baseline (render.h's
        //      kRulerBaselineToMarkerPx block) — and the cursor's column
        //      through the marker lane, under the flags (the reasoning is at
        //      that block in paint_ruler_row).
        //  10. the FLAG BLIT.
        //  11. the strip-drag anchor stem (waveform, mid-gesture only).
        //  12. the KEYBOARD SLOT (paint_keyboard_slot, outside this branch —
        //      the on-screen keyboard since 2026-08-27 or the folder overlay
        //      since 2026-08-28, one tenant at a time), whose opaque ground
        //      covers the waveform area's lower part (the keyboard) or
        //      EVERYTHING BETWEEN THE ICON ROW AND THE BOTTOM ROW (the
        //      overlay, since 2026-09-09 — the icon row's foot down, so of
        //      steps 3 through 11 only the menu row's and the icon row's
        //      lanes stay in view, greyed but for File) and so follows every
        //      pass above.
        //      Which is why the WAVEFORM passes do
        //      not paint there at all: they are gated and clipped on the
        //      waveform's PAINTED rect while either tenant stands
        //      (onscreen_keyboard::waveform_paint_area, read just above this
        //      block), which the overlay's band reduces to a ZERO-HEIGHT rect
        //      — it starts above the waveform's own top. THE LANES ARE NOT
        //      SPARED THAT WAY and deliberately so: rows 1..7 paint their
        //      exposure as they always have and the panel then covers them,
        //      because their painters own the roster's HIT RECTS and a row
        //      that skipped a frame would strand them (the reasoning is at
        //      step 3). The overdraw is a mode's cost, paid only while the
        //      panel stands.
        //  13. the flag editor's box, then THE NOTIFICATION CARDS
        //      (paint_notifications, 2026-08-29 — the top-right stack, above
        //      every lane and the keyboard slot), then the dropdown — the
        //      floating surfaces, after every pass above and outside this
        //      branch — then the MODAL DIALOG (paint_modal_dialog,
        //      2026-08-12; the bottom row the prompts and the five modal
        //      editors paint in since 2026-08-13), and LAST the TOOLTIP,
        //      which reads that stash.
        // (The bottom row left the tail of this sequence in row 7 — it paints
        // with the other redesigned rows at step 3, on every frame class, and
        // overlaps none of these passes.)
        // Two structural rulings live in this sequence:
        //   THE RECOLOR MODEL (architect 2026-07-26) — a highlight REPLACES
        //     colors, it never washes over them; and since the sweep's region
        //     highlight retired (architect 2026-10-03, "the trim bar is
        //     enough") NO pass recolours the waveform at all. The phase-reset
        //     overlay contributes no ground (architect 2026-07-27): its 1px
        //     RING is its whole visual, and a boundary line paints AFTER the
        //     plate, crossing the ink like the stems do.
        //   THE Z-ORDER FLIP (architect 2026-07-23) — the cursor playhead's
        //     STEM passes UNDER
        //     marker flags, so a cursor resting on a marker sits hidden behind
        //     that marker's flag standing in the same column; a SELECTION adds
        //     no playhead-like mark of its own, its whole cue being its
        //     members' BRIGHTENED FLAGS (the class ladder's brighter pair) with
        //     the landed cursor on the focus. (2026-08-01 lifted the SCANNER
        //     above the stems, so the moving line does not blink out at every
        //     marker it crosses.)
        //   THE PLAYHEAD STEM OVER THE MARKER STEMS (architect 2026-09-23) —
        //     the cursor's stem paints ABOVE every marker stem and BELOW every
        //     flag box, so at a coarse zoom a dense run of markers no longer
        //     hides a playhead standing near but not on one: what must read
        //     there is WHERE THE PLAYHEAD IS. Its other half keeps coincidence
        //     legible: WHEN THE PLAYHEAD'S COLUMN IS A MARKER'S, THE MARKER
        //     STEM WINS — the playhead's whole stem yields there
        //     (playhead_stem_suppressed) and the marker's stem shows,
        //     brightening when selected. (Row 5 had put the marker stems above
        //     the cursor's stem until this ruling.)

        if (rects_intersect(exposed, wave_paint)) {
            paint_waveform_plate(cr, area);
            // The stems' FLANKS (architect 2026-10-05) over the well's top
            // lines, ahead of the ring and the stems, so both paint over
            // them (paint_marker_stem_flanks' declaration).
            paint_marker_stem_flanks(cr, area);
            // The overlay band's boundary ring — the phase-reset overlay's whole
            // visual — over the plate and under trim
            // and the stems, so the focused reset's own stem stays crisp on top
            // of the left seam.
            paint_phase_reset_overlay_ring(cr, area);
        }

        // LIVE TRIM PASS — the old trim-stem-cache slot, now covering ALL trim
        // pixels (the trim bar lane's ground, the window's bar, the endcaps and
        // the midpoint mark).
        // Gated on EITHER half being exposed: render_background erased every
        // exposed top-strip pixel above, so a strip-only damage (hover text, a
        // flag change) must repaint the strip-resident trim pixels; the outer
        // Cairo damage clip bounds the actual work either way.
        if (rects_intersect(exposed, wave_paint) ||
            rects_intersect(exposed, top_strip)) {
            paint_trim(cr, area, top_strip);
        }

        // MARKER STEMS BEFORE THE CURSOR (architect 2026-09-23): the cursor's
        // waveform stem paints over them, and the flag boxes go over
        // everything in the strip blit below. The stems are the flags' waveform
        // half; the playhead stem between the two halves is the ruling itself
        // (the paint-order block above), and at a coincident column the two
        // never overlap because the playhead's stem yields whole there
        // (playhead_stem_suppressed).
        if (rects_intersect(exposed, wave_paint)) {
            paint_marker_stems(cr, area);
        }

        // The CURSOR AFTER THE MARKER STEMS and BEFORE the flag blit (the
        // playhead-over-stems ruling, architect 2026-09-23, and the Z-ORDER
        // FLIP, architect 2026-07-23): its line paints over every marker stem
        // and UNDER the marker flags that follow. Everything laid down before
        // it — the plate, the phase-reset overlay ring, the trim lane — stays
        // under it as before. (The scanner
        // used to ride along in this pass and now paints after it, below —
        // waveform-only either way, so its stacking against the lanes never
        // entered the question.)
        // flag_cache.surface is ARGB32, CLEAR-cleared
        // each rebuild and transparent outside the painted shapes, so the flag
        // blit composites source-over and never erases the playheads it does not
        // cover. Gated on area OR top_strip: the cursor line lives in the waveform
        // area, its head in the top strip.
        if (rects_intersect(exposed, wave_paint) ||
            rects_intersect(exposed, top_strip)) {
            paint_playheads(cr, area);
        }

        // THE SCANNER LAST OF THE WAVEFORM VERTICALS (architect 2026-08-01):
        // the moving line paints AFTER the stems, so it crosses them instead of
        // being erased column by column as it sweeps past every marker. Only the
        // scanner moved then; since 2026-09-23 the cursor paints over the stems
        // too, just before this pass, and stays under the flags. Where the two
        // playheads meet the scanner is on top. Waveform-only, so no top_strip
        // arm.
        if (rects_intersect(exposed, wave_paint)) {
            paint_scanner(cr, area);
        }

        if (rects_intersect(exposed, top_strip)) {
            // The ruler paints BEFORE the flags: its ticks descend past the
            // marker lane's top and must sit UNDER whatever that lane draws.
            paint_ruler_row(cr);
            paint_flag_annotations(cr, top_strip);
        }

        // Strip-drag anchor stem: over the plate/stems in the waveform area
        // only. It paints AFTER the playheads, so
        // where the pivot column coincides with the cursor/scanner column during
        // a strip drag the anchor stem sits OVER the playhead LINE (both are
        // waveform verticals; the playhead's strip-lane pixels are untouched,
        // the anchor has none). The anchor shows only mid-strip-drag, so
        // this overlap is transient and the pivot affordance reading on top is
        // acceptable. The flag editor's box likewise ends up after the
        // playheads, but on the non-overlapping marker lane.
        if (rects_intersect(exposed, wave_paint)) {
            paint_strip_drag_anchor(cr, area);
        }

        if (band_cuts) cairo_restore(cr);
    }

    // THE KEYBOARD SLOT (the on-screen keyboard since 2026-08-27, the folder
    // overlay since 2026-08-28 — one band, one tenant at a time), between the
    // waveform passes above and the three floating surfaces below — which is
    // exactly where it sits in the picture. It OVERLAYS the waveform with its
    // own opaque ground — the area's lower part under the keyboard, and
    // EVERYTHING BETWEEN THE ICON ROW AND THE BOTTOM ROW under the overlay
    // since 2026-09-09, the icon row's foot down — so it must follow
    // every pass that paints there
    // (the top button rows, the plate, the
    // trim, the ruler, the flags, the stems, the scanner, the anchor) and
    // precede the flag editor's box, the dropdown and the modal, which float
    // over it by design. It is OUTSIDE the loading /
    // total>0 branch above for the flag editor's own reason: an editor can
    // stand with no audio loaded, and the keyboard must stand with it.
    //
    // NOT exposure-gated: the dispatch's own gate is the tenants' standing
    // predicates, and it must run on EVERY frame class to write the
    // as-painted bit the tick comparator reads (the contract is at the
    // declaration). With the slot down that is one platform query and two
    // bit reads; with it up the outer Cairo clip makes a narrow damage cheap.
    paint_keyboard_slot(cr, GuiRect{x, y, w, h});

    // THE FLOATING SURFACES PAINT TOPMOST — after EVERY pass above, including
    // the waveform, because both hang outside their strips (the dropdown below
    // the top strip, the tooltip under the pointer) and overlap whatever is
    // under them. They are NOT exposure-gated the way the rows are: each
    // writes the rect it painted (or a zero rect) on every run, and a run that
    // skipped would strand a stale rect for the hit tests and the damage to
    // read. Hidden, each costs one boolean. THE NOTIFICATION CARDS (2026-08-29)
    // are a third floater of the same kind — they hang from row 1 down over
    // the lanes and the waveform and publish what they drew on every run —
    // and paint BELOW these two and the modal row: a dropdown or a hint the
    // pointer just raised belongs on top of a message, and the modal row is
    // the modal row.
    //
    // THE DROPDOWN AND THE TOOLTIP CANNOT COEXIST, so their order between
    // themselves is moot: the dropdown opens on a PRESS and a press hides the
    // tooltip, and while the dropdown is open no roster button hovers, so no
    // tooltip can arm under it.
    //
    // THE OPEN FLAG EDITOR'S BOX PAINTS HERE, ahead of those two and after every
    // pass above — including the flag blit it must cover — and UNCONDITIONALLY,
    // for the floating surfaces' own reason: it publishes the geometry the
    // pointer path grabs, and a run that skipped would strand a stale box. Off
    // the damage the outer Cairo clip makes it free, and with no editor open it
    // is two boolean tests. It sits OUTSIDE the loading / total>0 branch above
    // for the same reason — that branch is where the publication would go
    // missing.
    render_flag_editor_box(cr, app, audio);
    // THE NOTIFICATION CARDS (2026-08-29) paint after the flag editor's box
    // and before the dropdown: above every lane, the waveform, the keyboard
    // slot and the folder overlay's band, below the three surfaces that stay
    // topmost — the dropdown and the tooltip (the two pointer-transient
    // floaters) and the modal row. Unconditional for the floating surfaces'
    // reason: it publishes the geometry the pointer path reads.
    paint_notifications(cr);
    paint_dropdown(cr);

    // THE MODAL DIALOG PAINTS AFTER THOSE TWO (2026-08-12): it owns the bottom
    // row while it stands and cannot coexist with either (a dropdown and a
    // modal are never open together by the standing two-mechanism claim, and
    // the flag editor is ended by any dialog's open), so the order costs
    // nothing and states the stack honestly. UNCONDITIONAL for the floating
    // surfaces' own reason: it publishes the geometry the pointer path reads
    // (AppState::modal_dialog), and a run that skipped would strand a stale
    // box.
    paint_modal_dialog(cr);

    // THE TOOLTIP IS LAST OF ALL since 2026-08-13, when the MODAL's buttons
    // took hints of their own: this body READS the modal stash the call above
    // publishes — the hovered dialog button's composed hint and its rect — so
    // running after it is what makes the hint hang off the box THIS frame
    // draws rather than the previous one's. It also puts the one surface that
    // floats OUT of its lane on top of everything, which is what a tooltip is.
    paint_shift_tooltip(cr);

    cairo_restore(cr);

    // The target here IS the memory the compositor will read: each buffer's
    // surface is a cairo IMAGE surface created over the mmap'd wl_shm pool
    // (GuiPlatform::recreate_shm_pool), and the pool pages are shared with the
    // compositor. Flushing is cairo's handshake before anything outside cairo
    // reads that memory — it lands whatever the backend is still holding in
    // internal state, so the backing bytes are complete. This is the last point
    // where that can happen: nothing between here and the publish flushes the
    // surface again (destroying the context ends the drawing, not the surface,
    // which outlives every frame with the pool), and the very next thing
    // GuiPlatform::paint_one_frame does after the paint loop is
    // wl_surface_attach + wl_surface_commit.
    cairo_surface_flush(cairo_get_target(cr));
}

// -- GuiPaintHandler::on_resize ------------------------------------------

void GuiPaintHandler::on_resize(int w, int h) {
    app.width  = w;
    app.height = h;
    if (app.loading || audio.total_frames() <= 0) return;

    // A zoom level valid at the old width may exceed the per-file effective
    // ceiling at the new width. The level ceiling and the viewport clamp both
    // live in clamp_viewport_start now; the resize keeps only its TRIGGER role
    // and delegates. When the level actually moved the reflow changed spp under
    // the playback predictor, so re-anchor it. (A level move here cannot
    // change the magnification — the gain gate reads the magnification lamp
    // alone and no zoom term, waveform_magnified — so the
    // resize owes the gain nothing; its picture re-renders because a resize
    // moves the area dimensions, fields of the one plate fingerprint, and the
    // tick's enqueue carries that.)
    const double old_zoom = app.zoom_level;
    clamp_viewport_start(app, audio);
    if (app.zoom_level != old_zoom && playback.is_playing())
        playback.resync_predictor();
}
