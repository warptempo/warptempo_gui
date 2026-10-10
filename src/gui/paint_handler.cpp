#include "paint_handler.h"
#include "target_render.h"
#include "notifications.h"
#include "chrome_spec.h"
#include "cool_edit_paint.h"
#include "color_picker.h"

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
// its first ink one field pad past the field (2026-10-08) and clipped a
// group space short of the right block — painted by
// paint_bottom_row_buttons_and_clock, the
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

// THE SHARED TEXT FACE is the one face owner's body face (gui_font.h): Wine
// Tahoma at its vertically matched em at every scale (architect 2026-10-06).
// Every row names it through that owner, gui_font(GuiFace::Body) at the live
// scale.

// (The authored-length -> device-pixels conversion every dimension below takes
// is scaled_px, render.h — the ONE conversion the whole scale axis shares.)

// (THE ACCENT'S FOCUS FORK — accent_for_focus — retired 2026-10-03 with the
// inactive selection face: ONE SELECTED PAIR, FOCUSED OR NOT, the record at
// render.h's palette block.)

// ROW 1. The row's height lives in render.h (menu_row_h_px, its two terms
// the chrome spec's menu_row_* fields), because main.cpp's lane table needs
// it; its pads and lead below are the spec's too (architect 2026-10-07).
//
// THE MENU ROW IS EXPLORER'S MENU BAND (architect 2026-10-06), NOT A PLAIN
// WINDOW'S MENU BAR: the model is tmp/reactos-hover.png's "Default User"
// window, whose File / Edit / View / Favorites / Tools / Help is browseui's
// CMenuBand — a TBSTYLE_LIST | TBSTYLE_FLAT toolbar with no image list
// (shell32/shellmenu/CMenuToolbars.cpp, CreateToolbar and UpdateImageLists)
// in a band of Explorer's rebar (browseui/internettoolbar.cpp, AddDockItem
// with ITBBID_MENUBAND) — the rebar whose band the icon row's flat large
// toolbar is. A plain window's menu.c bar (user32's MENU_DrawMenuItem) is the
// OTHER FAMILY and is not the model: WordPad, Task Manager, Sound Recorder
// and Media Player (tmp/reactos-wordpad.png, reactos-sound.png,
// reactos-media.png) all draw it with the cap on row 5 of the 19, the text
// 6 px in from the item (MenuCharSize.cx, Tahoma 8's average width) and the
// first item's text 6 px from the client edge.
//
// THE CSS FLOAT MODEL is the ruled layout vocabulary (architect 2026-07-31): a
// flat button FILLS ITS WHOLE ROW and no margin or inset exists unless the
// architect states one. An anchor's rectangle therefore spans the full height
// of the button's row, the whole lane (menu_row_h_px — render.h carries the
// ruling), flush under the caption, its label padded by the two numbers
// below, and the icon row's ground begins on the next pixel row.
//
// THE PADS ARE THE CHROME SPEC'S (menu_label_pad_left_px / _right_px;
// architect 2026-10-07). WIN2000'S ARE EXPLORER'S BUTTON'S, 9 WINDOWS PX
// LEFT OF THE TEXT AND 7 RIGHT OF IT (architect 2026-10-06), so an item is its text + 16. The
// band's button is comctl32's list-style text button (ReactOS 0.4.16's
// toolbar.c):
// TOOLBAR_MeasureButton (l.1760-1763) makes it 2 x SM_CXEDGE + nBitmapWidth
// + iListGap + the text + szPadding.cx, i.e. 4 + 1 (the width a null image
// list leaves, l.4960) + DEFLISTGAP 4 (l.219) + the text + DEFPAD_CX 7
// (l.210) = text + 16; TOOLBAR_DrawButton's text rect (l.1077-1080) insets
// it SM_CXEDGE 2 and then nBitmapWidth + iListGap + 2 = 7 more, so the text
// stands 9 from the button's left and the other 7 fall on its right.
// MEASURED: tmp/reactos-menu2.png's open Edit box spans x 202-235 (34 = 18
// + 16) round the text at 211 (its pushed ink at 212, less the open push);
// tmp/reactos-hover.png's pitches File -> Edit -> View -> Favorites
// 32 / 34 / 38 = advance + 16.

// THE BAND'S LEAD, the ground between the window's left edge and the first
// anchor, the chrome spec's menu_band_lead_px: WIN2000'S (architect
// 2026-10-06) 2 Windows px, THE REBAR'S ETCHED EDGE — the
// Shadow and Hilight columns at the client's left inside which Explorer's
// rebar hangs its menu band (this row draws the ground there, not the
// edge) — so File's button starts where Explorer's does and its text stands
// 2 + 9 = 11 from the edge (tmp/reactos-hover.png: the client edge x 165,
// File's text 176; tmp/reactos-menu2.png: the edge 168, the etched pair
// 168-169, File's button from 170, its text 179). A plain menu.c bar has no
// lead (nonclient.c's UserDrawCaptionBar hands MENU_DrawMenuBar the inside
// rect, MENU_MenuBarCalcSize starts at its left).

// THE MENU ROW'S BUTTONS, in painted order — from the band's lead
// (menu_band_lead_px) and ADJACENT WITH NO GAP, the css float model's default (the architect states a
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
//     "Paste Phase Reset State", "Projects Repository"), a modal row's WORD button ("Copy to
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
// box — its height and the two label pads, the chrome spec's push_button_*
// fields since 2026-10-07, recorded at the kModal* block below,
// which used to read the row's constants and now own the numbers, with the
// derivation recorded there. The crops and the full row-2 record are git
// history.)

// A BUTTON'S BOX — THE ONE PAINTER of the chrome's two-line button faces
// (architect 2026-10-02, the Windows-95 design at the Windows pixel): the
// face filled, then its two-line edge, and the answer is what the glyph or
// label must sit on and how far it shifts. TWO FAMILIES (the edge grammar at
// render.h's palette head): a TOOLBAR button — the on-screen keyboard's keys
// and the caption's three buttons (Windows' DFC_CAPTION, 2026-10-05) —
// wears the SOFT edges (DrawEdge's BF_SOFT, Windows' classic toolbar), a
// PUSH button — the dialogs' — the PLAIN ones. (The two roster rows are the
// program's case, paint_roster_case below, not this family.) Three faces and
// no hover:
//   REST     — RAISED on the ground, glyph unshifted. EVERY button rests
//              raised, enabled or disabled: a disabled button keeps its edge
//              and changes only its glyph (its disabled face,
//              icons::draw_disabled, ReactOS's saturate) or its label
//              (embossed, show_embossed_run — Windows' DSS_DISABLED).
//   CHECKED  — a toggled-on button (the player's Repeat One, the keyboard's
//              armed keys): SUNKEN over Windows'
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

// THE ROSTER'S CASE — the band's and row 8's every button, and the render
// player's: THE PROGRAM'S CASE (architect 2026-10-09, the program is Cool
// Edit; paint_ce_case, cool_edit_paint.h, where its faces stand), the same
// under every chrome. A PRESSED button (a live press) and a CHECKED one (the
// roster's `selected`) wear Cool Edit's one down face, the glyph one line
// further right and down; a DISABLED button keeps the face its state gives —
// a dead checked button still reads checked (the roster's rule at
// paint_icon_row), only its glyph saturated (icons::draw_cased_disabled).
// NO HOT FACE: Cool Edit's toolbar has none (program_spec.h's
// case_hot_face). Answers the glyph's shift.
ButtonBoxFace paint_roster_case(cairo_t* cr, const GuiRect& r, bool lamp,
                                bool pressed) {
    return ButtonBoxFace{paint_ce_case(cr, r, lamp || pressed)};
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
// covers whole (kIconRowViewGroup), since 2026-10-05 the icon row's
// HISTORY STAND-INS, a member that does not stand in the current state
// (kIconRowHistoryStandIns), and since 2026-10-07 THE `h` VIEW'S HIDE on
// both toolbars, a member whose chord the view refuses in every session
// (history_mode_hides_button, input_pointer.cpp).
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

// ROW 4 — THE ICON ROW: COOL EDIT'S TOOLBAR BAND (architect 2026-10-09, the
// program is Cool Edit; program_spec.h). Its metrics — the case and its
// glyph seat, the band's lines and face, the panes' margins — live in
// program_spec.h and render.h's program block, where the lane table and
// row 8 read them too; this row spells none of them.
//
// THE VERTICAL STORY: the band's two head lines and three W of face, then
// the case (icon_case_top_offset_px), two W of face and the band's three
// foot lines. Row 8 shares the case alone: its case sits at its content's
// top plus its own four W of face (row8_air_above_px).
//
// THE HORIZONTAL WALK: each group of the roster is a PANE — its light left
// column, five W of face, its cases abutting, four W of face, its mid right
// column — and between two panes a dark column, so a groove reads mid |
// dark | light (Cool Edit's §1.2). The left groups' panes start at the
// band's dark left edge column; the right-anchored pair's end at its dark
// right edge column; the stretch between them is the band's recess.

// THE PAINTER'S HALF OF THE ICON-ROW ROSTER: each button's id and its content,
// an ICON of the live set (icons.h, drawn at the case's glyph seat). The press claim's chord table (input_pointer.cpp) is the
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
    // relayout, Open Project at its head since 2026-10-07 (below), the
    // relayout having dissolved row 2 (architect: the labeled lane goes, "the icon
    // to represent all those various meanings"): Save, Undo, Redo and Render at
    // the row's left, the SAME chords, gates, disabled derivations and
    // stateful faces the labeled buttons carried — only the FACE is a glyph
    // in the toolbar case now (Save's VcsCommit swap and Render's DialogCancel
    // swap ride redesign_button_icon below; MediaRecord serves BOTH plain
    // render and the iteration sweep by the architect's same-day ruling, the
    // tooltip alone forking). The old labels are the tooltips. SINCE
    // 2026-09-29 THE FOUR ARE TWO GROUPS (architect, after his accidental
    // Save presses at the tablet's 200 %): Save (with Open Project before it
    // since 2026-10-07), then a separator, then Undo leading Redo and Render
    // (redesign_button_opens_icon_group) — one separator slot onto the walk
    // where a 2px gap stood.
    //
    // OPEN PROJECT LEADS THEM ALL (architect 2026-10-07), ONE BUTTON — "just
    // to keep those two together", the button and its shift twin, Revert:
    // no drop-down — STANDING IN SAVE'S GROUP, first, then Save, NO
    // SEPARATOR BETWEEN THEM ("separator in the drop-down, no separator in
    // the toolbar": the File menu's line parts Open and Revert from Quit,
    // the toolbar's Open and Save touch; redesign_button_opens_icon_group),
    // then Undo's group behind its separator as before. It wears
    // DocumentOpen, the sets' document-open (icons.h).
    {RedesignButton::OpenProject, icons::Icon::DocumentOpen},
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
    // THE GLYPHS: MusicNote16th for the BPM opener and Mathmode for the
    // mode lamp (icons.h; the drawings are the set README's).
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
    // The read-only pair, Lock and Unlock. The TABLE entry is Lock and the
    // resolver (redesign_button_icon, above) is what swaps it for Unlock
    // on a writable tab — every button goes through that resolver, so this
    // constant is the fallback rather than the painted truth.
    {RedesignButton::IconReadOnly,       icons::Icon::Lock},
    // SETTINGS (architect 2026-09-29), right after the padlock and ahead of
    // the tooltip lamp (architect 2026-10-01: help comes after settings): bare
    // `;`, the settings prompt, wearing SettingsConfigure —
    // the typed road's pointer spelling, one box and one 2px gap onto the walk
    // and no separator moving.
    {RedesignButton::IconSettings,       icons::Icon::SettingsConfigure},
    // ENABLE TOOLTIPS (architect 2026-09-29, evening), CLOSING the
    // render-entry group behind Settings since 2026-10-01 (architect: help
    // comes after settings): the bare backslash's LAMP, wearing
    // HelpWhatsthis. Dark at every open, and while dark no tooltip shows
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
// and the render-entry group right of it sits one case (31 Windows px)
// further left there than outside — further still since 2026-10-07, the
// zoom group and the render-entry group each standing on the one member the
// view does not refuse in every session (Full Zoom Out, Enable Tooltips),
// their others hidden (history_mode_hides_button, paint_icon_row's walk). The history opener and the view group
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
    // the viewed walk member. It wears DialogOkApply (icons.h names each
    // set's drawing), the same glyph the render player's Load in place
    // button wears for the same act.
    {RedesignButton::IconLoadInPlace,   icons::Icon::DialogOkApply},
};
constexpr IconRowDef kIconRowHistoryReadingGroup[] = {
    // THE WALK LAMP (architect 2026-08-18 as a radio pair, one button since
    // the 2026-09-04 collapse). It wears the LIT state's glyph,
    // SHALLOW-HISTORY, because Git is the walk's default and the lamp reports
    // Session — the sets' x-office-calendar since 2026-10-07 (icons.h), the
    // dated page: the walk through dated commits. The Git half's
    // deep-history glyph left icons::Icon with that half.
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
// 31 Windows px further left inside the view than outside, so the button
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
// glyphs (icons.h). THE BAND'S OWN METRICS, NONE NEW: the program's case
// (render.h's program block), the members abutting in their own pane (its
// leader is Source+Warp at redesign_button_opens_icon_group), the pane
// ending at the band's dark right edge column (paint_icon_row). THE BAND'S
// FACES, and a RADIO OF THREE: the
// lit button is the current view (redesign_button_selected reads the live
// combination) and a press on it is the consumed nothing at its lift (the
// `radio` column, kToolbarChords), the hold showing the pressed face over the
// lit one (paint_roster_case: Cool Edit's one down face for both).
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
// walks). The band holds every pane down to 571 Windows px of window at
// 100 % outside the `h` view and 410 inside it (the width math at
// paint_icon_row, its one statement, re-derived 2026-10-09 for the
// program's 23-W case), so neither host reaches the rule at its scale: the
// tablet's 2304 device px hold the row in both states up to about 404 % and
// the laptop's 1920 up to about 337 % (the W arithmetic over the device
// width; each element's own rounding moves the edge by a pixel or so).
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
// The tooltip's metrics are measured off its period capture, ReactOS's
// "Views" (tmp/reactos-tooltips.png); its chrome is the floating surfaces' one box
// (paint_popup_chrome), and its seat, its timing and its damage bound live in
// render.h.
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
// measured pair that could drift. THE PAD IS THE CHROME SPEC'S
// (tooltip_pad_px, 2026-10-07): WIN2000'S 2 (architect 2026-10-06,
// Views on tmp/reactos-tooltips.png): the capture's one-line box is 19 rows,
// 1 + 2 + 13 + 2 + 1 — the black line, two rows of face, the 13-row cell,
// two rows, the line — and two face columns stand between the left line and
// the V, so the pad is 2 each way INSIDE the four-sided frame, whose line is
// its own rounded part (relief_line_px): at the tablet's 300 %,
// 3 + 6 + 39 + 6 + 3 = 57 for one line (the band 13 x 3 = 39), and at the
// laptop's 138 %, 1 + 3 + 18 + 3 + 1 = 26 (the band 13 x 1.38 = 17.94,
// rounded once). The gap is the laptop pixel's 4 re-authored at the unit's
// change. render.h carries only a BOUND on this for the
// damage band.
constexpr double kTooltipLineGapPx       = 3.0;   // between the two bands
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
// kTooltipLineGapPx); do not re-derive one from the other.
//
// THE ITEM'S INSET: the highlight box stands one Windows px inside the frame
// on every side — the margin Windows leaves between a popup's edge and its
// lit row — so the published item rect is the frame's interior less that px
// (the spec's popup_margin_px, which the vertical margin reads too).
// THE SEPARATOR'S INSET: its etched pair runs 5 Windows px in from the popup's
// edge each side (the laptop pixel's 7 re-authored at the unit's change).
constexpr double kPopupSepInsetPx    = 5.0;   // the separator, per side

// THE POPUP SEPARATOR — ONE PAINTER, EVERY POPUP MENU'S (2026-10-08 ~21:20:
// the menu row's pull-downs, paint_dropdown, and the color picker's preset
// menu, paint_color_picker, read it alike; a later vocabulary's own kind
// forks here). `x0` is the box's left edge and `x1` its right edge, or the
// scroll bar's left edge while a bar stands (`at_bar`, which a vocabulary
// whose separator runs to the frame reads); `y` the pair's top row, below
// the block's margin (popup_sep_margin_y_px). Windows' etched pair,
// kPopupSepInsetPx in from the box's edge each side (from the bar's edge
// while it stands).
static void paint_popup_separator(cairo_t* cr, int x0, int x1,
                                  bool /*at_bar*/, int y) {
    const int inset = scaled_px(kPopupSepInsetPx);
    paint_relief_etched_hline(cr, x0 + inset, y, x1 - x0 - 2 * inset);
}

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

// (THE DROPDOWN'S HORIZONTAL PADS, kPopupPadXPx and kPopupHotkeyGapPx, are
// render.h's since 2026-10-07, beside the vertical metrics; the menu row's
// pull-downs alone take them — the color picker's drop-downs take
// color_picker::combo_text_inset_px, 2026-10-09.)

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
// 2026-10-05, gui_font.h): the recorded cap times the scale, an unrounded
// double — Tahoma's 8 rows: 8 device rows at 100 %, 11.04 at 138 %, 24 at
// 300 %, 32 at 400 % — the face's em being the one that stands its "H"
// exactly that tall.
//
// THE SEATS AT THE TABLET'S 300 %: the dropdown's 17-W item, 51 rows, seats
// at row floor((51 + 24) / 2) = 37; at the laptop's 138 % the item's 23 rows
// (17 x 1.38 = 23.46) seat at floor((23 + 11.04) / 2) = 17. The answer is
// independent of the box's y by construction rather than by a tie rule.
//
// TWO SEATS, AND NO CALLER SOLVES A LINE AS A BOX. A BOX has margins to
// centre a cap band in; a LINE is exactly the face's own ascent-plus-descent
// band and has none, so a line's baseline is line_baseline() below and the cap
// rule is not asked. THE BOX SEATS (the color picker's and the settings
// choice's combos, lists and fields seat the same way): the
// caption's title, the row-8 clock (in its time field) and the
// state line beside it (on THE FIELD's box since 2026-10-07 — the reasoning
// is at paint_bottom_row_buttons_and_clock), the notification
// card's first line, the dropdown items, the prompt's message, the render player's
// two time fields, the modal field's INK, the modal field's LABEL (on the BUTTONS' box —
// the reasoning is at that site), the modal buttons' own labels, the on-screen
// keyboard's caps, the folder overlay's rows, the menu row's anchors
// (the plain menu bar's seat, a capture of the real OS
// beating ReactOS's band, architect 2026-10-09 ~17:40). ONE LINE SEAT: the
// tooltip's two lines (line_baseline).
//
// THE CANVAS COLUMN'S TEXT TAKES NEITHER SEAT (2026-10-09, the program is
// Cool Edit): THE RULER'S DIGITS stand on an AUTHORED BASELINE ROW of their
// lane — program_spec.h's ruler_baseline_px (the architect's row under one
// W of air, 2026-10-09 ~21:00); THE CUES' LABELS stand on THE LINE BOX
// CENTRED IN THE MARKER LANE (architect 2026-10-09 evening: the face's
// ascent-plus-descent band, rounded once, centred by the box rule, the
// baseline its top plus the ascent — render.h's cue_baseline_px), so the
// flag editor's selection band, which is that line box, sits even in its
// field; the cap band's centring by this solver (2026-10-09 ~21:45) retired
// with it. (This solver is declared in render.h for its readers there.)
//
// TWO AUTHORED DROPS RETIRED WITH THIS RULE, both of them hand-measured
// corrections to the proxy the rule replaces. THE CLOCKS' 1px: the bottom
// row's content band was 46 at 100% and 104 at 225%, and cap-centring the
// monospace digits gave row 28 and row 63 — exactly where the old proxy plus
// the architect's authored drop put them, at both scales. His measured pixel
// WAS cap-centring, so the offset is gone. The time fields (2026-10-05) seat
// their own box: the 68-row field at 400 % puts the body's 32-row cap at
// row 50 (eighteen rows above it, eighteen below — a 4-row line and 14 of
// face each side), the 51-row field at 300 % its 24-row band at row 37, the
// 23-row field at 138 % its 11.04-row band at row 17.
// AND THE MODAL FIELD LABEL'S 1px: that label now reads THE BUTTONS' OWN SEAT
// rather than the field band's plus a drop, which is what levels it with OK
// and Cancel at every scale (the reasoning is at the modal's own site).
} // namespace
double redesign_baseline(const GuiFont& font, double box_y,
                         double box_h) {
    return box_y + std::floor((box_h + gui_font_cap_px(font)) * 0.5);
}
namespace {

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
// THE CAPTION BUTTONS' GLYPHS — MARLETT's characters as the caption buttons
// draw them at the body size (architect 2026-10-06): ReactOS's own Marlett
// at 12 ppem, read off its captures and its font (the 12-ppem strikes of the
// ISO's Marlett.ttf equal the capture): Minimise 7 x 2 at the cell's (0, 7)
// and Restore's two windows 7 wide — ReactOS's departure from Windows 2000's
// 6-wide pair (Windows' own Marlett at 10 ppem), followed on purpose
// (docs/engineering/win2000_deviations.md) — Maximise Windows' own, and
// Close THE SMOOTH OUTLINE OF MARLETT'S STAIRCASE (below), the architect's
// pick of the mocks (variant (a)).
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
// was wrong at 275 %, the tablet's scale until 2026-10-06 — a 44 x 38 box, u = 3, a 27-px
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
// CLOSE IS THE STAIRCASE'S OUTLINE (architect 2026-10-06, the mocks' variant
// (a)): Marlett's close X inks an 8 x 7 staircase at (1, 1) of
// the cell — rows of two-px runs, (0, 2)(6, 2) / (1, 2)(5, 2) / (2, 4) /
// (3, 2) / (2, 4) / (1, 2)(5, 2) / (0, 2)(6, 2) — and the glyph is that
// footprint drawn as TWO PARALLELOGRAMS, one per diagonal, each with its ends
// cut along the rows (horizontal) and 2 Windows px wide along a row, so the
// four corners are the staircase's full 2-px runs and the bar is 2 x 7 /
// sqrt(85) = 1.52 Windows px across (the footprint is steeper than 45
// degrees). The vertices are authored in the cell's Windows px
// (kCaptionCloseOutline) and stand on the cell's unit grid, as the
// rectangles do; ONE PATH, ONE FILL, the two parallelograms wound alike so
// their crossing is ink, never a hole, antialiased; in the label, the
// emboss's two copies the same outline.
struct CaptionGlyphRect {
    int x, y, w, h;
};
struct CaptionGlyphPoint {
    int x, y;
};
constexpr int kCaptionGlyphCellPx = 9;
constexpr int kCaptionGlyphSeatXPx = 3;
constexpr int kCaptionGlyphSeatYPx = 2;
// The cell fits every vocabulary's caption button (chrome_spec.h's
// caption_button_* fields).
static_assert(chrome_specs_all([](const ChromeSpec& s) {
    return kCaptionGlyphSeatXPx + kCaptionGlyphCellPx <= s.caption_button_w_px &&
           kCaptionGlyphSeatYPx + kCaptionGlyphCellPx <= s.caption_button_h_px;
}));
constexpr CaptionGlyphRect kCaptionMinimizeGlyph[] = {{0, 7, 7, 2}};
constexpr CaptionGlyphRect kCaptionMaximizeGlyph[] = {
    {0, 0, 9, 2}, {0, 2, 1, 6}, {8, 2, 1, 6}, {0, 8, 9, 1}};
constexpr CaptionGlyphRect kCaptionRestoreGlyph[] = {
    // the back window: its two-row top, the stub of its left side, its right
    // side and the end of its foot, the rest behind the front window
    {2, 0, 7, 2}, {2, 2, 1, 1}, {8, 2, 1, 3}, {7, 5, 2, 1},
    // the front window, whole
    {0, 3, 7, 2}, {0, 5, 1, 3}, {6, 5, 1, 3}, {0, 8, 7, 1}};
// Close (the rule above): the falling bar, then the rising one, each
// clockwise on screen (y down), in the cell's Windows px — the staircase's
// 8 x 7 footprint at (1, 1), 2 Windows px wide along a row.
constexpr CaptionGlyphPoint kCaptionCloseOutline[2][4] = {
    {{1, 1}, {3, 1}, {9, 8}, {7, 8}},
    {{7, 1}, {9, 1}, {3, 8}, {1, 8}}};

void paint_caption_glyph(cairo_t* cr, std::span<const CaptionGlyphRect> glyph,
                         int gx, int gy, int u, GuiColor ink) {
    for (const CaptionGlyphRect& r : glyph)
        paint_cell_rect(cr, GuiRect{gx + r.x * u, gy + r.y * u, r.w * u,
                                    r.h * u},
                        ink);
}

// Close (the rule above) in the cell at (gx, gy), unit u, in `ink`: the two
// parallelograms as one path, one antialiased fill.
void paint_caption_close_outline(cairo_t* cr, int gx, int gy, int u,
                                 GuiColor ink) {
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_new_path(cr);
    for (const auto& bar : kCaptionCloseOutline) {
        cairo_new_sub_path(cr);
        for (const CaptionGlyphPoint& p : bar)
            cairo_line_to(cr, gx + p.x * u, gy + p.y * u);
        cairo_close_path(cr);
    }
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
            paint_caption_close_outline(cr, gx + off, gy + off, u,
                                        pal.hilight);
        paint_caption_close_outline(cr, gx, gy, u,
                                    enabled ? pal.label : pal.shadow);
        return;
    }
    using Glyph = std::span<const CaptionGlyphRect>;
    const Glyph glyph = id == GuiCaptionButton::Minimize
                            ? Glyph(kCaptionMinimizeGlyph)
                        : maximized ? Glyph(kCaptionRestoreGlyph)
                                    : Glyph(kCaptionMaximizeGlyph);
    if (!enabled)
        paint_caption_glyph(cr, glyph, gx + off, gy + off, u, pal.hilight);
    paint_caption_glyph(cr, glyph, gx, gy, u,
                        enabled ? pal.label : pal.shadow);
}

// The three buttons' rects in a caption lane, left to right (render.h's
// caption block, the chrome spec's caption_button_* fields): flush right,
// the inset right of Close, the box's top inside the lane, the gap between
// Minimise and Maximise (none under win2000), the gap before Close — each
// edge a sum of rounded parts.
std::array<GuiRect, kCaptionButtonCount> caption_button_rects(
        const GuiRect& lane) {
    const ChromeSpec& spec = live_chrome_spec();
    const int w   = scaled_px(spec.caption_button_w_px, 1);
    const int h   = scaled_px(spec.caption_button_h_px, 1);
    const int y   = lane.y + scaled_px(spec.caption_button_y_px);
    const int cx  = lane.x + lane.w - scaled_px(spec.caption_button_inset_px) - w;
    const int mx  = cx - scaled_px(spec.caption_close_gap_px) - w;
    const int nx  = mx - scaled_px(spec.caption_button_gap_px) - w;
    return {GuiRect{nx, y, w, h}, GuiRect{mx, y, w, h},
            GuiRect{cx, y, w, h}};
}

} // namespace

void GuiPaintHandler::paint_caption_row(cairo_t* cr) {
    // THE CAPTION (top lane 0; the record at render.h's caption block and
    // the declaration, every seat the chrome spec's caption_* fields). Four passes on the lane: the ground, the icon, the
    // title and the buttons.
    const GuiRect row = top_caption_row_area(app);
    if (row.w <= 0 || row.h <= 0) return;
    cairo_save(cr);

    // THE GROUND: the active roles while the window has the focus, the
    // inactive ones without it (GuiPlatform::caption_active — always active
    // on the tablet), through the one gradient painter.
    const GuiPalette& pal = palette();
    const bool active = gui.caption_active();
    const std::array<GuiRect, kCaptionButtonCount> rects =
        caption_button_rects(row);
    paint_caption_gradient(cr, row,
                           active ? pal.caption_active
                                  : pal.caption_inactive,
                           active ? pal.caption_active_gradient
                                  : pal.caption_inactive_gradient);

    // THE APP'S ICON at the spec's seat ((2, 1) under win2000), 16 x 16: the
    // set's AppIcon (icons.h), no case, at the caption's own placement.
    const ChromeSpec& spec = live_chrome_spec();
    icons::draw(cr, icons::Icon::AppIcon,
                static_cast<double>(row.x + scaled_px(spec.caption_icon_x_px)),
                static_cast<double>(row.y + scaled_px(spec.caption_icon_y_px)),
                static_cast<double>(scaled_px(kCaptionIconPx, 1)));

    // THE TITLE, Windows' "Document - Program" convention (architect
    // 2026-10-05): the open piece's name (AppState::project_name, the
    // project's folder) and " - Warptempo", or "Warptempo" alone where no
    // piece is open. THE BOLD FACE (gui_font.h: the `font` key's set's bold —
    // Tahoma Bold, FreeSans Bold or Liberation Sans Bold) through the shaping
    // chokepoint, its cap band centred in the lane — Tahoma Bold's 8-row cap
    // on rows 5..12 of the 18 at 100 %, ReactOS's own seat on its captures
    // (2026-10-06). THE ROOM runs from the spec's caption_title_x_px to its
    // caption_title_trail_px short of Minimise: the title at the room's
    // left, in the caption's text role. TOO LONG FOR THE ROOM it is CUT AT A
    // CODEPOINT and ends in Windows' "..." (DrawText's end ellipsis), the
    // longest prefix whose own run and the ellipsis's fit; a room too narrow
    // for even the ellipsis paints no title.
    const GuiFont font = gui_font(GuiFace::Bold);
    const std::string title = app.project_name.empty()
                                  ? std::string("Warptempo")
                                  : app.project_name + " - Warptempo";
    const int title_x = row.x + scaled_px(spec.caption_title_x_px);
    const double room = static_cast<double>(
        rects[static_cast<size_t>(GuiCaptionButton::Minimize)].x -
        scaled_px(spec.caption_title_trail_px) - title_x);
    const double baseline = redesign_baseline(
        font, static_cast<double>(row.y), static_cast<double>(row.h));
    // One title run at the room's pen, the ellipsis after it when cut.
    const auto show_title = [&](const text_shape::ShapedRun& head,
                                const text_shape::ShapedRun* tail) {
        const double x = title_x;
        // The caption's text role (surface_text, render.h: Windows'
        // CaptionText pair).
        set_palette_source(cr, surface_text(active ? GuiSurface::CaptionActive
                                                   : GuiSurface::CaptionInactive));
        text_shape::show_shaped_run(cr, head, x, baseline);
        if (tail)
            text_shape::show_shaped_run(cr, *tail, x + head.width_px, baseline);
    };
    text_shape::ShapedRun run = text_shape::shape_text_run(font, title);
    if (run.width_px <= room) {
        show_title(run, nullptr);
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
        if (ellipsis.width_px <= room) show_title(run, &ellipsis);
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
    // THE LEFT FLOAT'S FACES (architect 2026-10-02, the period menu bar):
    // COLD, nothing is drawn — the label bare on the ground; OPEN, the anchor
    // whose menu is down is Explorer's open menu title (architect
    // 2026-10-06, the menu band's — the head of the menu row's pads: CMenuToolbars
    // marks the item whose submenu is up CHECKED, and comctl32's flat toolbar
    // draws a checked button so, toolbar.c): the ground with a
    // ONE-LINE SUNKEN BOX round the item's content rect — BDR_SUNKENOUTER,
    // Shadow on the top and left, Hilight on the bottom and right, the
    // status panel's own line (paint_relief_sunken_outer, mitred like every
    // two-tone ring) — and the label in the label role PUSHED IN
    // kMenuOpenTextShiftPx, one Windows px right and down (toolbar.c offsets a
    // checked button's text rect by (1, 1); measured on tmp/reactos-menu2.png,
    // Edit open, against tmp/reactos-hover.png, Edit closed, each from its
    // window's left edge: the E's first column 44 against 43, the cap's top
    // row 28 against 27); DEAD (the history view
    // greys every anchor but File, the partition being
    // history_mode_disables_button's), the label alone takes THE DISABLED
    // EMBOSS (show_embossed_run). NO HOVER FACE (architect 2026-10-02: "hover
    // is awkward with pen and sometimes flickers"; Windows 98's hot-tracked
    // raised title is not adopted, and ReactOS draws none) and no press face:
    // a press opens the menu, whose open title is the cue. NO MNEMONIC
    // UNDERLINES (ReactOS always draws them; Windows 2000 hides them until
    // Alt).
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

    // THE LANE IS THE ANCHOR (architect 2026-10-01 — render.h's menu-row
    // block carries the ruling and its why): the whole lane's
    // height is each anchor's rectangle AND its published hit rect, flush
    // under the caption with no air above it; the program frame's top row
    // begins on the next pixel row (paint_program_frame, 2026-10-10). The
    // anchor's foot and the lane's foot are one edge, and the dropdown hangs
    // from it over the frame's top row (dropdown_hang_y). THE LANE IS NOT ITS
    // CONTENT SINCE 2026-10-05: one row of ground stands ABOVE the content
    // (ReactOS: caption, face row, menu; the spec's menu_row_head_px,
    // render.h's menu-row block) — so every
    // label on this row is seated in the CONTENT ALONE
    // (menu_row_content_rect) rather than the taller lane — the one place
    // this row reads two different heights for two different things. NO LINE
    // STANDS UNDER THE CONTENT: Cool Edit's plain menu bar has none, the line
    // under it being its frame's (2026-10-10, render.h's program_frame_rect;
    // Windows' etched pair, a rebar's band border, stood here that morning).

    cairo_save(cr);

    // THE GROUND IS THE CONTENT GROUND, the icon row's own (architect
    // 2026-10-01: the menu row takes the icon row's ground), one fill over
    // the whole lane. It has one value focused and unfocused, so this row no
    // longer darkens on the window's focus loss.
    {
        const GuiColor ground = palette().ground;
        set_palette_source(cr, ground);
        cairo_rectangle(cr, row.x, row.y, row.w, row.h);
        cairo_fill(cr);
    }

    // THE SHAPING CHOKEPOINT (text_shape.h): each label is MEASURED and PAINTED
    // from the one ShapedRun, so a button's width and its glyphs come from the
    // same positions and cannot disagree. Shaping a handful of glyphs per paint
    // is deliberate — the chokepoint's own comment defers caching to a profile,
    // and these are the cheapest runs there are.
    const GuiFont font = gui_font(GuiFace::Body);

    const ChromeSpec& spec = live_chrome_spec();
    const int pad_l  = scaled_px(spec.menu_label_pad_left_px);
    const int pad_r  = scaled_px(spec.menu_label_pad_right_px);
    // THE CONTENT ROWS (the face row's place, above) and the open title's
    // push (the faces' block, above).
    const GuiRect content = menu_row_content_rect(row);
    constexpr int kMenuOpenTextShiftPx = 1;

    // THE LABEL'S SEAT: THE PLAIN MENU BAR'S, THE CAP BAND CENTRED
    // IN THE CONTENT AS EVERY OTHER CHROME BOX (architect 2026-10-09 ~17:40:
    // "File Edit Settings looks off center … in winme.png and win2000pro.png
    // the capitals are closer to the top"; the standing rule, "ReactOS
    // officially always gives way to screenshots — official screenshots with
    // good provenance like guidebookgallery"): redesign_baseline over the
    // content's 19 rows (render.h's menu-row block: the 13-row cell +
    // DEFPAD_CY 6) puts the cap's top on row floor((19 − 8) / 2) = 5, five W
    // of air above the cap and six below the baseline — his WordPad captures
    // of the real OS, tmp/shots/winme.png (the band rows 22–41, the cap
    // 27–35) and tmp/shots/win2000pro.png (the cap 28–35, the i's dot on 27),
    // the seat of Windows' plain window menu bar (menu.c). THE SEAT DEPARTED
    // FROM is ReactOS's Explorer menu band (2026-10-06 to 2026-10-09;
    // tmp/reactos-hover.png, content rows 161–179, cap 167–174): comctl32 draws
    // a list-style button's text DT_LEFT | DT_VCENTER | DT_SINGLELINE
    // (toolbar.c's default dwDTFlags), standing the CELL — ascent + descent —
    // at (19 − 13) / 2 = 3 rows and the cap's top on row 6; a capture of the
    // real OS beats ReactOS where the two differ (win2000_deviations.md).
    // At 300 % the cap's top stands 16 device rows into the content
    // (floor((57 − 24) / 2)), 19 under the lane's top.
    const double label_baseline = redesign_baseline(
        font, static_cast<double>(content.y), static_cast<double>(content.h));

    // THE WALK: from the band's lead (menu_band_lead_px), ADJACENT WITH NO GAP.
    // Row 2 inserts a 2px invisible separator between its adjacent buttons
    // because the architect stated one there; none is stated here, so none
    // exists (the css float model's default).
    int x = row.x + scaled_px(spec.menu_band_lead_px);
    for (const MenuButtonDef& def : kMenuButtons) {
        const text_shape::ShapedRun run =
            text_shape::shape_text_run(font, def.label);
        const int btn_w =
            pad_l + static_cast<int>(std::nearbyint(run.width_px)) + pad_r;

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

        // THE OPEN ANCHOR WEARS THE OPEN TITLE WHILE ITS DROPDOWN IS UP
        // (architect 2026-10-02; the anchor stays marked while its menu is
        // open, kdenlive's behaviour since 2026-08-02): the sunken box round
        // the content rows, the label's width plus its pads, and the label
        // pushed in (the faces' block, above), whichever of the three anchors
        // emitted the open popup, through the one anchor owner. A PAINT
        // CONDITION, NOT A `selected` BIT: no menu button has a chord
        // (redesign_button_selected is the live fact a chord flips), and the
        // popup's two writers, toggle_dropdown and the one close owner
        // close_dropdown, already invalidate the top strip on both edges. A
        // dead anchor has no open menu (toggle_dropdown refuses it), so the
        // open title needs no enabled term.
        //
        // DEAD, THE LABEL IS THE ONE THING THAT CHANGES: it takes THE
        // DISABLED EMBOSS, the product's one disabled word (the partition and
        // its derivation are at history_mode_disables_button,
        // input_pointer.cpp; this reads only the published bit).
        const bool open_anchor =
            app.dropdown.open() &&
            def.id == dropdown_anchor_button(app.dropdown.menu);
        int push = 0;
        if (open_anchor) {
            paint_relief_sunken_outer(
                cr, GuiRect{x, content.y, btn_w, content.h});
            push = scaled_px(kMenuOpenTextShiftPx, kMenuOpenTextShiftPx);
        }
        // THE LABEL SEATS IN THE ANCHOR'S CONTENT, NOT ITS LANE (the face
        // row block above): the band's text seat (label_baseline, above).
        const double label_x = static_cast<double>(x + pad_l + push);
        const double label_y = label_baseline + push;
        if (!face.enabled && !open_anchor) {
            show_embossed_run(cr, run, label_x, label_y);
        } else {
            set_palette_source(cr, palette().label);
            text_shape::show_shaped_run(cr, run, label_x, label_y);
        }

        x += btn_w;
    }

    cairo_restore(cr);
}

// -- THE PROGRAM'S FRAME (2026-10-10) ---------------------------------------
//
// COOL EDIT'S FRAME WINDOW ROUND THE PROGRAM: one sunken line, Shadow on the
// top and left and Hilight on the bottom and right, in the chrome's roles,
// mitred — the law (his Cool Edit captures), the geometry and the overlays
// laid over it at render.h's program_frame_rect, its one statement.
// paint_program_frame paints THE RING, program_frame_rect whole, with the
// rows (on_redraw's step 3): its top row is its own lane under the menu row,
// its bottom row its own lane under row 8, and its columns stand in the line
// every program lane leaves on each side, so nothing else paints them; the
// keyboard slot, the bottom overlays and the floating surfaces cover it where
// they stand. The outer damage clip bounds it; the ring is a handful of
// rects, so it is not exposure-gated.
void GuiPaintHandler::paint_program_frame(cairo_t* cr) {
    const GuiRect ring = program_frame_rect(app);
    if (ring.w <= 0 || ring.h <= 0) return;
    paint_relief_sunken_outer(cr, ring);
}

// -- THE BOTTOM OVERLAYS (architect 2026-10-10) -------------------------------
//
// THE DIALOG AND THE ON-SCREEN KEYBOARD AS COOL EDIT'S STATUS BAR — the law,
// the mocks and every rect at onscreen_keyboard.h's overlay block. This
// painter lays what the overlay owns beyond the keys and the controls: THE
// LINE, one relief line in the chrome's Hilight across the window's whole
// width on the overlay's top row (over the keyboard alone, over the dialog
// alone, or over the pair), and THE DIALOG'S GROUND in the chrome's ground
// under it. The keyboard's band is paint_onscreen_keyboard's (its ground and
// its keys, through the keyboard slot just before this), the dialog's
// controls paint_modal_dialog's (after the floating surfaces). No bottom or
// side border: the overlay ends at the window's foot and edges as Cool
// Edit's status bar does. Not exposure-gated — two fills, the outer damage
// clip bounding them — and it publishes nothing: a press on the dialog's
// ground meets the dialog's veil, a press on the keyboard's line the
// keyboard's claim.
void GuiPaintHandler::paint_bottom_overlays(cairo_t* cr) {
    const GuiRect over = onscreen_keyboard::overlay_rect(app, gui);
    if (over.w <= 0 || over.h <= 0) return;
    const GuiPalette& p = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    if (onscreen_keyboard::dialog_stands(app)) {
        paint_cell_rect(cr,
                        onscreen_keyboard::dialog_ground_rect(
                            app, onscreen_keyboard::stands(app, gui)),
                        p.ground);
    }
    const int line = std::min(onscreen_keyboard::overlay_line_px(), over.h);
    paint_cell_rect(cr, GuiRect{over.x, over.y, over.w, line}, p.hilight);
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
    // at the right block (paint_bottom_row_buttons_and_clock): where the line
    // is wider than the cell — a narrow window's; the tablet's in-view cell,
    // ~1097 device px at 300 % since the view hides row 8's dead buttons
    // (2026-10-07), holds `007/114 | GitHub: checking... | Scale: [-]1.0
    // [+]1.1` whole, ~752 px of Tahoma's advances — the cut falls on the
    // scale, which the diff lane also shows, and never on the word Ctrl+S
    // forks on. A visit with no clone (the local fallback)
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
    // THE ICON ROW (top lane 3, directly under the program frame's top row;
    // row 4 of the redesign; one frame line in from each side since
    // 2026-10-10): COOL EDIT'S TOOLBAR BAND since 2026-10-09 (the roster block
    // above), its groups the band's panes, the cases abutting within a pane,
    // a groove between two panes — TWENTY-ONE members in
    // SEVEN groups outside the `h` view and FOURTEEN in seven inside it (the
    // width math below is the count's one statement), RE-COUNTED off the
    // roster enum, the painter's tables and the divider owner rather than
    // adjusted: OPEN PROJECT at the row's head (architect 2026-10-07, its
    // Revert twin with it) and SAVE beside it in one group, no separator
    // between ("separator in the drop-down, no separator in the toolbar"),
    // then the rest of the toolbar four (Undo / Redo / Render, the deleted
    // row 2's, in a group of their own since 2026-09-29) with COPY
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
    // ONE MODE SWAP AND ONE MODE HIDE (architect 2026-10-05 and 2026-10-07,
    // reversing 2026-08-14's "no more hiding/showing icons in top icon row"
    // for the `h` view alone): the history stand-ins replace the two groups
    // the view refuses whole (the companions publish empty rects outside the
    // view, the replaced members inside it), and inside the view every other
    // member whose chord it refuses in EVERY session is not painted
    // (history_mode_hides_button, input_pointer.cpp) — the magnification
    // lamp, Follow and Restrict Undo behind Full Zoom Out, and Listen, the
    // padlock and Settings ahead of Enable Tooltips — its neighbours closing
    // up and its group standing while any member does. What the view refuses
    // only BY STATE (Save with nothing to commit, Revert with no subject,
    // Load in Place over an empty walk, Older / Newer at the walls) and what
    // any other mode refuses wears the DEAD FACE, and a window too narrow for
    // the row covers members otherwise, under the view group's overflow rule
    // — a width, never a state. The mode-collapsing roster of 2026-08-12
    // stays deleted; the walk below is a left-to-right accumulation whose two
    // forks are the stand-in table's and the hide's.
    //
    // THE WIDTH MATH, RE-DERIVED for the program's band (2026-10-09, the
    // case Cool Edit's own 23 round its 20-W seat): in Windows px, the
    // band's dark left edge column, then per pane its light column + 5 of
    // face + its cases at 23 + 4 of face + its mid column and the dark
    // column after it (23n + 12); the right-anchored pair the same from the
    // band's dark right edge column. Outside the `h` view the LEFT WALK is
    // seventeen members in five panes,
    //   1 + 17·23 + 5·12 = 452,
    // and inside it ten in five, 1 + 10·23 + 60 = 291; the RIGHT-ANCHORED
    // span — the edge column, the view group's pane (3 cases), its dark
    // column, the history opener's pane, the dark column the left panes stop
    // at — is
    //   1 + 80 + 1 + 34 + 1 = 117,
    // so the band holds every pane in any window at least 571 Windows px
    // wide outside the view (410 inside it), the band standing one program
    // frame line in from each side (2026-10-10) — the tablet's 768 at 300 %
    // a band of 766 with a recess of 197 between. IN DEVICE PX, off the
    // painted walk (each line one relief line, each face and the glyph its
    // own scaled_px): at the tablet's 300 % (cases 69) 1356 + 351 of its
    // band's 2298; at the laptop's 138 % (lines 1, faces 7 and 6, cases 31)
    // 608 + 157 of its 1918. A roster move restates these numbers.
    //
    // NO FOCUS SWAP HERE: the ground has one value focused and unfocused
    // (render.h's palette says so), and so has the menu row's.
    //
    // THE FACES ARE THE PROGRAM'S CASE (paint_roster_case, above, where the
    // faces stand): the rest face, Cool Edit's one down face for a press and
    // for a CHECKED button — the live fact its chord flips
    // (redesign_button_selected), the view group's lit view among them —
    // and no hot face. The pointer walk finds the tooltip's button
    // (recompute_redesign_button_hover).
    // THE DISABLED FACE IS THE TWO MODES' AND THE TOOLBAR MIGRANTS':
    // the row's own members never grey for a REFUSAL —
    // presses always dispatch and the CHORDS' OWN refusals answer (loading
    // blocks everything, and each arm keeps its home-view, empty-selection and
    // occupied-frame guards), inherited through on_key rather than mirrored —
    // while the toolbar four
    // BROUGHT their real disabled derivations with them at the 2026-08-12
    // relayout (Undo/Redo's locked-tab and empty-stack terms, Save's
    // in-flight lockout, Render's source path — redesign_button_enabled's
    // own arms, painted by this body's generic disabled face with nothing added
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

    // THE LANE IS COOL EDIT'S BAND (render.h's program block): no border of
    // its own beyond the band's own lines.
    cairo_save(cr);

    // THE BAND'S GROUND — the dark outline, the recess, the light last line:
    // the band where no pane stands (paint_ce_band_ground). The panes and the
    // dark columns are laid over it by the walks below.
    paint_ce_band_ground(cr, lane);

    // (NO FONT IS SELECTED HERE, and that is the row's own fact since
    // 2026-08-11: this lane paints geometry and icons only. A text face was
    // named here for the four view radios' shaped LETTER faces,
    // which the architect replaced with glyphs that day.)
    // THE CASE AND ITS GLYPH, each a composite of its rounded parts
    // (render.h's program block): the glyph at the case's (+1, +1) lines.
    const int lw       = program_line_px();
    const int btn_w    = icon_case_w_px();
    const int btn_h    = icon_case_h_px();
    const int glyph_px = icon_glyph_px();
    const int lead     = program_pane_lead_px();
    const int trail    = program_pane_trail_px();
    // A PANE of n cases, from its light column through its mid column (the
    // groove's dark column after it is the pane's outer edge, painted with
    // it: paint_ce_pane takes the extent through that column).
    const auto pane_w = [&](int n) {
        return lw + lead + n * btn_w + trail + lw;
    };

    // THE CASE STANDS UNDER THE BAND'S TWO HEAD LINES AND ITS THREE W OF FACE
    // (icon_case_top_offset_px, render.h: the one expression every reader of
    // the case's seat in the band takes).
    const int btn_y = lane.y + icon_case_top_offset_px();

    // THE RIGHT-ANCHORED PANES ARE RESOLVED FIRST, because they decide where
    // every pane to their left may paint (the overflow rule at
    // kIconRowViewGroup): the view group's pane ends at the band's dark right
    // edge column, the HISTORY OPENER's pane stands one groove left of it
    // (kIconRowHistoryOpener, architect 2026-10-06), and the dark column left
    // of the opener's pane is THE LEFT PANES' LIMIT: they paint under a clip
    // ending there and publish only the columns they painted, so a window
    // too narrow for the band covers them and never the opener or the view
    // group.
    const int view_n     = static_cast<int>(std::size(kIconRowViewGroup));
    const int right_edge = lane.x + lane.w - lw;   // the band's dark edge column
    const int view_x0    = right_edge - pane_w(view_n);
    const int history_x0 = view_x0 - lw - pane_w(1);
    const int history_x  = history_x0 + lw + lead;  // the opener's case
    const int left_limit = history_x0 - lw;         // the dark column before it

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
        // does. EVERY button standing in this row that the view refuses BY
        // STATE wears it — the MOMENT-STATE Save (an empty head delta or a
        // checkpoint in flight), Revert and Load in Place on their session
        // terms (below); what the view refuses in EVERY session does not
        // stand in it since 2026-10-07 (history_mode_hides_button: the zoom
        // group behind Full Zoom Out, Listen, the read-only toggle,
        // Settings), and the two groups it consumes WHOLE since 2026-10-05,
        // the history companions standing in their slots
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
        // THE FACE IS THE GLYPH'S ALONE (architect 2026-10-02, the AB set):
        // its disabled face, ReactOS's saturate (icons::draw_disabled,
        // architect 2026-10-06), its case the face its state gives
        // (paint_roster_case). A dead CHECKED button (the
        // cumulative reading, say) stays checked: the mode cannot change that
        // state, so hiding it would be a lie, and the greyed glyph says
        // "true, but not yours right now".
        // The press face is gated on the live bit rather than trusted: the
        // claim never records a press on a disabled button, but a button can
        // go dead UNDER a held press with no pointer event to refresh it.
        const bool pressed =
            face.enabled && redesign_button_pressed_face(app, def.id);
        const ButtonBoxFace box = paint_roster_case(
            cr, GuiRect{bx, btn_y, btn_w, btn_h}, face.selected, pressed);

        // THE GLYPH at the case's (+1, +1) lines, in the drawing's own
        // colours — or,
        // dead, its disabled face.
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
        // draw_cased takes the CASE's own corner, not a glyph origin: the
        // seat is its own (icons.h's PLACEMENT).
        if (face.enabled)
            icons::draw_cased(cr, glyph, bx, btn_y,
                              static_cast<double>(glyph_px), box.shift);
        else
            icons::draw_cased_disabled(cr, glyph, bx, btn_y,
                                       static_cast<double>(glyph_px),
                                       box.shift);
    };

    // THE LEFT PANES, IN TWO PASSES: the walk below lays out every standing
    // member into its pane first — a pane's width is its standing members'
    // — and the panes are then painted, each its ground before its cases. A
    // group LEADER (redesign_button_opens_icon_group, app_state.h — the
    // roster's own divider owner) opens a new pane and everything else joins
    // the open one. The membership forks are the history stand-ins'
    // (2026-10-05) and the `h` view's hide (2026-10-07), which asks a group
    // whether it stands before its pane is opened. Everything paints under
    // the clip that ends at the left panes' limit, and each member PUBLISHES
    // ITS RECT CUT AT THAT COLUMN — whole at every width the band fits (the
    // laptop's and the tablet's included), its painted columns alone when the
    // right-anchored panes cover part of it, and an empty rect when they cover
    // it whole, which contains no point and refreshes its bits
    // unconditionally (publish_button_face).
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
    // place, as its pane, and the rest of the replaced group is skipped up to
    // the next leader; outside the view the stand-in groups never paint.
    const bool history = app.history_mode.active;
    const auto stand_in_for =
        [&](RedesignButton leader) -> const IconRowStandIn* {
        if (!history) return nullptr;
        for (const IconRowStandIn& si : kIconRowHistoryStandIns)
            if (si.replaces == leader) return &si;
        return nullptr;
    };
    // THE HIDE (history_mode_hides_button, input_pointer.cpp, architect
    // 2026-10-07): in the `h` view a member whose chord the view refuses in
    // every session is skipped — no case, no width, its empty rect published
    // — and its neighbours close up; a group STANDS, as a pane, while any
    // member of it does, which this asks of the group led by the table's
    // entry `lead` before the walk reaches its members.
    const auto group_stands = [&](std::size_t lead_i) {
        for (std::size_t j = lead_i; j < std::size(kIconRowButtons); ++j) {
            const RedesignButton id = kIconRowButtons[j].id;
            if (j != lead_i && redesign_button_opens_icon_group(id)) break;
            if (!history_mode_hides_button(app, id)) return true;
        }
        return false;
    };
    std::vector<std::vector<IconRowDef>> panes;
    bool replacing = false;
    for (std::size_t i = 0; i < std::size(kIconRowButtons); ++i) {
        const IconRowDef& def = kIconRowButtons[i];
        if (redesign_button_opens_icon_group(def.id)) {
            const IconRowStandIn* si = stand_in_for(def.id);
            replacing = (si != nullptr);
            if (replacing) {
                panes.emplace_back(std::begin(si->members),
                                   std::end(si->members));
            } else if (group_stands(i)) {
                panes.emplace_back();
            }
        }
        if (replacing || history_mode_hides_button(app, def.id)) {
            publish_absent(def.id);
            continue;
        }
        if (panes.empty()) panes.emplace_back();
        panes.back().push_back(def);
    }
    if (!history) {
        for (const IconRowStandIn& si : kIconRowHistoryStandIns)
            for (const IconRowDef& m : si.members) publish_absent(m.id);
    }

    cairo_save(cr);
    cairo_rectangle(cr, lane.x, lane.y,
                    std::max(0, left_limit - lane.x), lane.h);
    cairo_clip(cr);
    // The band's dark left edge column, then each pane with the groove's
    // dark column after it — the pane's own outer edge, its top the mitre
    // (paint_ce_pane).
    paint_ce_band_dark_column(cr, lane, lane.x);
    int x = lane.x + lw;
    for (const std::vector<IconRowDef>& pane : panes) {
        const int n = static_cast<int>(pane.size());
        paint_ce_pane(cr, lane, x, x + pane_w(n) + lw);
        int bx = x + lw + lead;
        for (const IconRowDef& def : pane) {
            const int shown_w = std::clamp(left_limit - bx, 0, btn_w);
            paint_member(def, bx, GuiRect{bx, btn_y, shown_w, btn_h});
            bx += btn_w;
        }
        x += pane_w(n) + lw;
    }
    cairo_restore(cr);

    // THE RIGHT-ANCHORED PANES, PAINTED LAST AND WHOLE, each member
    // publishing the full case it paints: the dark column the left panes stop
    // at (a lone column, square: no pane of its own stands left of it), the
    // history opener's pane at one x in both states with the groove's dark
    // column after it, the view group's pane with the band's dark right edge
    // column after it — each of those two the pane's own outer edge, mitred
    // (paint_ce_pane).
    // NEITHER IS EVER HIDDEN (history_mode_hides_button): bare `h` is the
    // view's own vocabulary and bare 1 / 2 / 3 its admitted selectors, so
    // the hide does not reach this walk.
    paint_ce_band_dark_column(cr, lane, left_limit);
    paint_ce_pane(cr, lane, history_x0, history_x0 + pane_w(1) + lw);
    paint_member(kIconRowHistoryOpener, history_x,
                 GuiRect{history_x, btn_y, btn_w, btn_h});
    paint_ce_pane(cr, lane, view_x0, view_x0 + pane_w(view_n) + lw);
    int vx = view_x0 + lw + lead;
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
//   THE CLOCK in Cool Edit's dark TIME FIELD inside a group of its own —
//   the gripper, the field, the end bar (architect 2026-10-09; the field's
//   shape, face and fixed width at kTimeShape's block below) — with THE
//   STATE LINE on the free face past that group's end bar in the panel
//   label's shadowed pair, clipped short of the right block (architect
//   2026-10-03 for the line);
//
//   and, PACKED AGAINST THE RIGHT EDGE, four groups, each Cool Edit's
//   gripper, its cases and its end bar, two W of face between two of them
//   (architect 2026-10-09; cool_edit_paint.h):
//   THE MARKER VERBS (kMarkerVerbGroup) — drop (bare `s`), delete (Delete),
//   disable (Ctrl+D), inherit (Ctrl+N), JUMP TO DEFINING MARKER (Ctrl+J,
//   since 2026-09-29), OPEN TEXT EDITOR (bare Return, since 2026-10-09) and
//   ADD TO SELECTION (bare `k`, the sticky ctrl, the row's ONE LIT FACE
//   while the mode stands). The four verbs and Open Text Editor are the
//   row's resting greys on a locked tab; the jump and Add to selection are
//   not, both being navigation (EDIT FLAG and COPY RESOLVED VALUE stood
//   between Toggle inherit and Add to selection until 2026-09-29; Copy is
//   the icon row's since, and Edit Flag was deleted);
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
// ONE MODE TERM (architect 2026-10-07): in the `h` view a member whose chord
// the view refuses in every session is NOT PAINTED (history_mode_hides_button,
// input_pointer.cpp) — Play / Stop, Up and Down, the four verbs, Jump to
// Defining Marker and Open Text Editor — and publishes an empty rect, the
// block closing up from
// the right margin and the state line taking the room; the row in there is
// ADD TO SELECTION alone, the walk group's four, LEFT and RIGHT, and the two
// skips: nine cases in four groups. Otherwise no member publishes a zero
// rect except under a modal.
//
// (A CENTERED ESC BUTTON shipped between the groups on row 8's first day and
// was DELETED at the architect's live pass — "looks like a missing button
// with that cross out"; the mid-render CANCEL lives on the RENDER button now,
// and bare Esc is keyboard-only.)
//
// THE BUTTONS ARE THE BAND'S (the unification's point — "the same size as
// the other icon buttons", bigger finger targets without stealing waveform
// height): the program's 23-W case round Cool Edit's 20-W seat — read from
// the band's own accessors (render.h's program block) so the rows cannot
// drift apart — on THE ROW'S OWN FACE, four W above the case and five below
// (Cool Edit's row 8 at one button row, program_spec.h): 32 W under the
// 6-W dock bar. (Row 8's kdenlive transport metrics, its 26-px boxes and
// its ruled separators; Explorer's flat case with WordPad's air and the
// etched separators, 2026-10-06 to 2026-10-09 — git history.)
//
// EVERYTHING ELSE IS THE BAND'S OWN MODEL: the same faces
// (paint_roster_case), the same dead glyph. WHO WEARS THE DEAD FACE HERE, re-derived 2026-10-07 — in
// the `h` view the derived partition answers dead for the PLAY/STOP button
// (Space is consumed there), UP and DOWN (bare Up/Down are neither the mode's
// vocabulary nor on its allowlist), the FOUR MARKER VERBS, JUMP TO
// DEFINING MARKER (Ctrl+J, consumed in there like the verbs' chords) and OPEN
// TEXT EDITOR (bare Return, on neither list, 2026-10-09) — every
// one of them dead in every session, so none of them is painted in the view
// (history_mode_hides_button) — while LEFT / RIGHT are the mode's own
// playhead step since 2026-09-26 and grey on their own arm (a focused diff
// flag, the wall); ADD TO SELECTION STAYS LIT SINCE 2026-09-17, bare `k`
// being on that mode's allowlist now and the lamp producing the view's own
// multi-selection; the two
// SKIPS and THE WALK GROUP'S FOUR stay lit, Home/End being the mode's
// own absolute jumps, Tab/Shift+Tab its diff-flag cycle, `c` its centring and
// Ctrl+Tab on its allowlist (architect-confirmed for the skips). PREVIOUS
// MARKER greys only under a lit grid iterations with no reverse step left
// (its twin, the march, refusing there too). Outside the view the four VERBS
// and OPEN TEXT EDITOR grey on a locked tab, their own
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
// they were not painted in there at all under the cluster swap. In the view
// since 2026-09-26 LEFT and RIGHT are the mode's own playhead step, live, and
// UP and DOWN, whose bare chords the mode consumes, are not painted there
// since 2026-10-07 (history_mode_hides_button). Those are the partition's
// own answers and no part of the always-on ruling, which is about the
// RESTING face.)
// THE LANE'S HEAD IS COOL EDIT'S DOCK BAR (render.h's dock_bar_h_px): the
// lane's chrome is paint_bottom_strip's, which paints it and calls the body
// below onto the content band under it.

// THE ROW AUTHORS NO METRIC OF ITS OWN (program_spec.h owns every one): the
// cases and glyphs are the band's, the groups' grippers, end bars and face
// Cool Edit's. The render player's modal row in this lane takes the same
// case at the same seat (bottom_row_seats) and stands its items its own
// eight W apart on the row's face, no grippers (paint_modal_dialog's player
// branch).

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
// disable toggle (`Ctrl+D`, ViewHidden's muted speaker in both sets since
// 2026-10-07: a disabled marker is one the render does not hear) and inherit/collapse
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
// THE GROUP IS SEVEN SINCE 2026-10-09 (architect: "resurrect the Open Text
// Editor button. It should go between jump to defining marker and toggle add
// to selection"): OPEN TEXT EDITOR — bare Return, the sets'
// accessories-text-editor, Tango's the drawing its Unlock already wears —
// the glass's road to Return with no editor standing. An act that opens an
// editor, no lamp; it greys where Return would open nothing and on a locked
// tab, and the `h` view does not paint it (its roster entry, app_state.h).
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
    {RedesignButton::IconOpenTextEditor,       icons::Icon::AccessoriesTextEditor},
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
// and the mode term in this row's layout all went together. The row's one
// mode term since 2026-10-07 is the `h` view's hide, a subtraction and not a
// swap: nothing stands in for a hidden member — history_mode_hides_button,
// paint_bottom_row_buttons_and_clock's walk.)

// THE TIME FIELDS — ROW 8'S CLOCK AND THE RENDER PLAYER'S POSITION AND
// LENGTH, ONE FIELD SHAPE UNDER EVERY CHROME: COOL EDIT'S DARK FIELD
// (architect 2026-10-09, the program is Cool Edit; METRICS §5.4,
// paint_ce_time_field) — its field-dark line on the top and left, the mid
// tone inside, its field-light line on the bottom and right, all three the
// palette's Face-derived tones, the digits Cool Edit's EFF0F0 (kCeFieldText,
// a constant of the painter: the chrome's time-field roles, clock_ground and
// clock_text, are unread by the program) — kProgramSpec.field_h_px tall
// (17 W), CENTRED ON THE CASE'S ROWS (a half-row tie toward the top, the cap
// rule's own), its run in THE BODY FACE (gui_font.h; no period field used a
// monospace face), its cap band centred in the field by redesign_baseline.
// The digits are TABULAR — Tahoma's every digit 0.545 em less the one
// tracking (gui_tracking_px, gui_font.h, which every glyph takes alike),
// with no kerning between the digits, the colon and the point — so the
// colon and the point stand still as the time runs.
//
// EACH FIELD'S WIDTH IS FIXED — ITS WIDEST STRING (architect 2026-10-05:
// "a 1 is skinnier than a 0", nothing in the field or right of it may move as
// the time runs). The reserved cell is measured from the widest string the
// field can ever show in this face — the widest digit in every digit slot of
// "DD:DD.DDD" (kTimeShape) and, on row 8, the widest TAB LETTER in the
// letter's slot (kClockTabLetters) — shaped through the one chokepoint at the
// live size, so nothing is trusted to the face. The field is that cell plus
// the field's pad either side — 5 W, THE DIALOG FIELD'S OWN (program_spec.h's
// field_pad_px, where the reason it is 5 and not the 4 of 2026-10-09 evening
// stands: architect 2026-10-10, "let's add back one pixel on the left and one
// on the right side for that input box"; time_field_pad_px below) — its
// width ceiled, AND NOTHING MORE: row 8's clock exactly as the render
// player's two fields (architect 2026-10-10, "the media player's fields look
// good"; the 85-W floor of Cool Edit's mock, 2026-10-09, removed 2026-10-10).
// At 300 % under Tahoma the clock is its cell of about 201 device px plus
// 2 · 15.
//
// A PLAYER FIELD'S RUN IS RIGHT-ALIGNED IN ITS FIXED CELL (architect
// 2026-10-05, the period's ACID / Vegas fields): it ENDS AT THE RESERVED
// CELL'S RIGHT EDGE, so nothing moves as the time changes — the purpose of
// the rule, which the fixed cell keeps (row 8's composition below). THE
// CELL'S RIGHT EDGE IS THE FIELD'S RIGHT PAD in every time field, the field
// being the cell and its two pads (the floor's centering of 2026-10-09 went
// with the floor). On row 8 THE DIGITS AND THE PIPE ARE ONE RUN, the
// timestamp and ` | `, STARTING AT THE CELL'S LEFT EDGE — the digits are
// tabular, so the run is the specimen's width plus the separator's at every
// time and the pipe never moves as the time runs — and the tab
// letter is painted LEFT-ALIGNED IN ITS SLOT at that run's end, the cell's
// last part (architect 2026-10-09 ~21:20): switching tab moves at most the
// letter's own right edge and nothing else — no letter's kerning can reach
// the pipe, the two being shaped apart. (Tahoma's A is 0.600 em and its B
// 0.589, less the tracking, measured 2026-10-06; the slot is the widest, so
// where they differ the narrower letter stands left-aligned in A's room.)
//
// TWO MINUTE DIGITS, and longer sources TRUNCATE (the ruling and what it costs
// are at format_timestamp, time_format.h). The cell is that format's width and
// no wider.
//
// (The chrome's own time fields — the one-line sunken field on the chrome's
// color at the dialog field's size — stood
// 2026-10-05 to 2026-10-09; the big clock Cool Edit draws beside its fields
// is scratched, "the little one is fine", architect 2026-10-09.)
constexpr const char* kTimeShape = "DD:DD.DDD";

// The tab letters row 8's clock ends with (AppState::active_tab_view's two
// values) — the set the letter slot is measured over.
constexpr std::string_view kClockTabLetters = "AB";

// THE FIELD'S HEIGHT AND ITS PAD, each one element rounded once (scaled_px): 17 W tall (51 device rows at 300 %, 23
// at 138 %; program_spec.h's field_h_px) and THE TIME FIELD'S PAD, 5 W
// (15 device px at 300 %, 7 at 138 % — 6.9 rounded — and 18 at 360 %;
// program_spec.h's field_pad_px, architect 2026-10-10 — the dialog field's
// own 5, kModalFieldPadXPx), one constant at every time field.
static int time_field_h_px() {
    return scaled_px(kProgramSpec.field_h_px);
}
static int time_field_pad_px() {
    return scaled_px(kProgramSpec.field_pad_px);
}

// THE AIR BETWEEN TWO TIME FIELDS — the render player's position and length
// (architect 2026-10-05): ACID's adjacent fields stand three Windows px of
// ground apart (columns 1136..1138 between its first two, the same capture);
// Cool Edit's Begin / End / Length fields three too (METRICS §5.4: pitch 75
// round 72-W fields).
constexpr double kTimeFieldGapPx = 3.0;

// The time fields' metrics, MEMOISED ON THE SCALE AND THE LIVE FACE SET —
// thirteen tiny shaping passes (ten digits, the two letters and the
// specimen) that answer the same thing on every frame until the scale moves
// or a Settings commit of the `font` key swaps the set live
// (gui_live_face_set, gui_font.h, 2026-10-09; the widths are the live
// face's own).
// Single-threaded paint state; the waveform worker never reaches this file's
// text tiers.
//
// THE CELLS ARE ADVANCES: `time_w` is the specimen's advance sum and
// `letter_w` the widest letter's, so no glyph ever walks; row 8's cell is
// time_w + the shaped ` | ` + letter_w, each player field's time_w, and each
// cell is its field's damage box too.
struct TimeFieldMetrics {
    int    percent  = -1;     // the scale this was measured at
    const GuiFaceSet* set = nullptr;   // and the live set
    double time_w   = 0.0;    // the widest-digit specimen's shaped width
    double letter_w = 0.0;    // the widest tab letter's shaped width
};
static TimeFieldMetrics g_time_field_metrics;

static const TimeFieldMetrics& time_field_metrics(const GuiFont& font) {
    TimeFieldMetrics& m = g_time_field_metrics;
    if (m.percent == font.percent && m.set == &gui_live_face_set()) return m;
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
    m.set     = &gui_live_face_set();
    return m;
}

// THE BOTTOM ROW'S SEATS — ROW 8 AND THE RENDER PLAYER'S ROW ARE ONE ROW
// (architect 2026-10-05, on his 350 % screenshots, the player's Close under
// row 8's last button): both painters take their edges from this one owner,
// so the shared edges cannot drift. The LEFT PAD is where the player's
// left-aligned transport starts and the modal row's content, the RIGHT PAD
// where the modal rows' right-aligned clusters end — the lane's one pad
// (icon_row_pad_x) from each edge; CASES_RIGHT is where row 8's last case
// ends (the right block packed against the lane's edge, its last end bar
// and the group's air before it — 2026-10-09; that air 5 W since
// 2026-10-09 evening, program_spec.h's group fields), where the player's
// right pair ends too. Every BUTTON on either row is THE PROGRAM'S CASE (render.h's
// program block) at the row's four W of face under the content's top
// (row8_air_above_px); the faces are paint_roster_case's on both rows. What
// lies between the two runs is each row's own: row 8's state line, the
// player's scrub, which fills it.
struct BottomRowSeats {
    int left_x      = 0;   // the left pad: the player's and the modal row's first column
    int right_x     = 0;   // the right pad: the modal clusters' last column + 1
    int cases_right = 0;   // one past row 8's last case: the player's pair's end
    int case_y      = 0;   // the case's top: the content's top plus the row's face
    int case_w      = 0;   // the program's case, both rows' every button
    int case_h      = 0;
};
static BottomRowSeats bottom_row_seats(const GuiRect& content) {
    const int pad = icon_row_pad_x();
    const int right = content.x + content.w;
    return BottomRowSeats{
        content.x + pad, right - pad,
        right - ce_end_bar_w_px() - scaled_px(kProgramSpec.case_to_end_bar_px),
        content.y + row8_air_above_px(), icon_case_w_px(), icon_case_h_px()};
}

// ONE TIME FIELD'S RECT: its left line at `x`, `w` wide, centred on the
// case's rows [case_y, case_y + case_h).
static GuiRect time_field_rect(int x, int w, int case_y, int case_h) {
    const int h = time_field_h_px();
    return GuiRect{x, case_y + (case_h - h) / 2, w, h};
}
// A FIELD'S WIDTH round a reserved cell `cell_w` wide: the cell ceiled and
// the pad either side (no least width since 2026-10-10, kTimeShape's block).
static int time_field_w_px(double cell_w) {
    return static_cast<int>(std::ceil(cell_w)) + 2 * time_field_pad_px();
}

void GuiPaintHandler::paint_bottom_row_buttons_and_clock(cairo_t* cr) {
    const GuiRect lane    = bottom_row_area(app);
    const GuiRect content = bottom_row_content_area(app);
    if (lane.w <= 0 || lane.h <= 0 || content.h <= 0) return;
    const int content_y = content.y;
    const int content_h = content.h;

    cairo_save(cr);

    // The row's seats, shared with the render player's row (bottom_row_seats):
    // the band's case on the row's four W of face.
    const BottomRowSeats seats = bottom_row_seats(content);
    const int btn_w     = seats.case_w;
    const int btn_h     = seats.case_h;
    const int glyph_px  = icon_glyph_px();
    const int btn_y     = seats.case_y;
    // COOL EDIT'S GROUP (program_spec.h, METRICS §5.2): the gripper, its
    // air, the cases abutting, the same air, the end bar — the air the
    // product's symmetric 5 W (architect 2026-10-09 evening, the group fields'
    // paragraph); two W of face before the first group and between two.
    const int group_gap = scaled_px(kProgramSpec.row8_group_gap_px);
    const int grip_w    = ce_gripper_w_px();
    const int grip_face = scaled_px(kProgramSpec.gripper_to_case_px);
    const int end_face  = scaled_px(kProgramSpec.case_to_end_bar_px);
    const int end_w     = ce_end_bar_w_px();

    // One button, the band's face logic verbatim (paint_roster_case). THE
    // CHECKED FACE'S SUBJECT ON THIS ROW IS ADD TO SELECTION (2026-08-18),
    // the sticky-ctrl mode that closes the marker-verb group: it wears the
    // checked face while the mode stands, which is the roster's standing rule
    // for a mode. A checked button a mode greys keeps its checked face and
    // greys its glyph, the band's own composition.
    const auto paint_button = [&](const TransportRowDef& def, int x) {
        AppState::RedesignButtonFace& face = publish_button_face(
            cr, app, audio, playback, target_render,
            def.id,
            GuiRect{x, btn_y, btn_w, btn_h});

        const bool pressed =
            face.enabled && redesign_button_pressed_face(app, def.id);
        const ButtonBoxFace box = paint_roster_case(
            cr, GuiRect{x, btn_y, btn_w, btn_h}, face.selected, pressed);
        // THE GLYPH IS THE STATE RESOLVER'S since 2026-08-15, as the band's
        // already was: this row hosts a stateful face — the collapsed
        // PLAY/STOP button, which wears media-playback-stop while an audition
        // runs — so it goes through redesign_button_icon for EVERY button,
        // the band's own rule: the table icon is what the resolver returns
        // when a button has no override, so there is no membership to keep in
        // step here.
        const icons::Icon glyph = redesign_button_icon(app, def.id, def.icon);
        // The case's corner, not a glyph origin (icons.h's PLACEMENT).
        if (face.enabled)
            icons::draw_cased(cr, glyph, x, btn_y,
                              static_cast<double>(glyph_px), box.shift);
        else
            icons::draw_cased_disabled(cr, glyph, x, btn_y,
                                       static_cast<double>(glyph_px),
                                       box.shift);
    };

    // THE ROW, LEFT TO RIGHT (architect 2026-09-29, the right-handed tablet's
    // layout: the most-used buttons at the bottom right; Cool Edit's groups
    // 2026-10-09): THE CLOCK'S GROUP two W in from the lane's left edge — the
    // gripper, the dark time field, the end bar — with THE STATE LINE on the
    // free face past it (the block at the clock below) — then THE RIGHT
    // BLOCK, packed against the lane's right edge: the MARKER-VERB GROUP (its
    // seven counted off kMarkerVerbGroup), the MARKER-WALK GROUP (Previous
    // Marker, Next Marker, Center and Switch Tab, kTransportWalkGroup), the
    // four CARDINAL ARROWS (↓ ↑ ← →, kTransportArrowGroup) and the TRANSPORT
    // THREE (kTransportGroup), each Cool Edit's group, two W of face between
    // two. The whole block is measured first and laid left to right from
    // there, so one expression owns the anchor and no group re-derives it.
    // ITS ONE MODE TERM is the `h` view's hide (architect 2026-10-07,
    // history_mode_hides_button): a member hidden there publishes an empty
    // rect, the block is measured over the members that stand, and a group
    // hidden whole takes its gripper and end bar with it; every other button
    // on this row publishes a real rect on every frame, except under a modal
    // that owns the row — every one but the color picker
    // (modal_owns_bottom_row) — where the row yields whole
    // (paint_bottom_strip).
    //
    // THE NUMBERS (Windows px, re-derived whenever a group gains or loses a
    // case; the case 23 since 2026-10-09 ~10:25, the verbs seven since
    // Open Text Editor joined that evening, the air 5 / 5 since 2026-10-09
    // ~00:30 — Cool Edit's 6 / 4 before it, the same sum): a group of n is
    // 5 + 5 + 23n + 5 + 6 = 23n + 21, so the block is the 7 verbs' 182, the walk's 113, the
    // arrows' 113 and the transport's 90 with three gaps of 2, 504 W — on the
    // tablet's 768 at 300 %, the lane W 1..766 inside the program frame's
    // columns (2026-10-10), starting at W 263, the clock's group W 3..100
    // (Tahoma's ~67-W cell and its two 5-W pads, a ~77-W field; 2026-10-10),
    // the state line's first ink at W 106, clipped at W 261 (~155 W of
    // line). IN THE `h` VIEW (the hide,
    // 2026-10-07; Open Text Editor hidden there with the verbs) the block is
    // 1 verb + 4 walk + 2 arrows + 2 transport in four groups, 44 + 113 + 67
    // + 67 + 6 = 297 W, the line's room 364 W. THE ROW CARRIES NO COLLISION
    // RULE — none of the redesign does — and the crop-at-the-floor allowance
    // recorded at kMinWindowWidthPx covers a narrow window: the block reaches
    // the clock's group once the window falls below about 1 + 2 + 98 + 504 +
    // 1 = 606 W outside the view (the program frame's two lines among them).
    // THE STATE
    // LINE CANNOT PUSH ANYTHING: it is clipped short of the block, so a long
    // line is cut rather than colliding.
    int right_block_x = content.x + content.w;
    {
        // THE FOUR GROUPS IN PAINTED ORDER, each read off its own table, so a
        // box joining or leaving moves the block with no second edit.
        const std::span<const TransportRowDef> groups[] = {
            kMarkerVerbGroup, kTransportWalkGroup, kTransportArrowGroup,
            kTransportGroup};
        // THE BLOCK'S WIDTH IS THE PAINTED GROUPS' (architect 2026-10-07): in
        // the `h` view a button whose chord the view refuses in every session
        // is not painted (history_mode_hides_button, input_pointer.cpp), so
        // the count is of the members that stand, and a group stands only
        // while it keeps a member. Outside the view nothing is hidden and this
        // is every table's whole length.
        int block_w = 0;
        int standing_groups = 0;
        for (const std::span<const TransportRowDef> g : groups) {
            int n = 0;
            for (const TransportRowDef& def : g)
                if (!history_mode_hides_button(app, def.id)) ++n;
            if (n == 0) continue;
            block_w += grip_w + grip_face + n * btn_w + end_face + end_w;
            ++standing_groups;
        }
        block_w += std::max(0, standing_groups - 1) * group_gap;
        int ax = content.x + content.w - block_w;
        // THE STATE LINE'S RIGHT BOUND (below) is this block's own left
        // edge, published out of the scope so the line cannot guess it — so
        // the line widens by construction when the `h` view hides members.
        right_block_x = ax;
        // A HIDDEN MEMBER PUBLISHES THE EMPTY RECT (the band's absent
        // members' shape): no point is inside it, so no press, hover or
        // tooltip reaches it, and its bits refresh unconditionally
        // (publish_button_face) — the same rect the modal yield publishes
        // for the whole row.
        for (const std::span<const TransportRowDef> g : groups) {
            bool group_open = false;
            for (const TransportRowDef& def : g) {
                if (history_mode_hides_button(app, def.id)) {
                    (void)publish_button_face(cr, app, audio, playback,
                                              target_render, def.id,
                                              GuiRect{});
                    continue;
                }
                if (!group_open) {
                    paint_ce_gripper(cr, ax, btn_y, btn_h);
                    ax += grip_w + grip_face;
                    group_open = true;
                }
                paint_button(def, ax);
                ax += btn_w;
            }
            if (group_open) {
                ax += end_face;
                paint_ce_end_bar(cr, ax, content_y, content_h);
                ax += end_w + group_gap;
            }
        }
    }

    // THE CLOCK — the first TIME FIELD (the shape, the face and the
    // fixed-width rule at kTimeShape's block above), in the body face named
    // through the one face owner (gui_font.h), inside the save/restore this
    // body already opened, in ITS OWN COOL EDIT GROUP: the gripper, the
    // field, the end bar (2026-10-09). The render player's two fields share
    // its shape, face and metrics; THE TWO NEVER PAINT IN THE SAME FRAME: the
    // player owns the row whole while it stands.
    {
        const GuiFont font = gui_font(GuiFace::Body);
        const TimeFieldMetrics& tm =
            time_field_metrics(font);
        // THE ACTIVE TAB'S LETTER ENDS THE CELL (the tab row deleted
        // 2026-10-01 as "a waste of space", the letter moving here; the order
        // architect 2026-10-09 ~21:20, "let's try putting the timestamp
        // first, and then the pipe, and then the tab letter"): `00:45.115 | A`
        // — the timestamp, a space, the LITERAL pipe, a space, then the
        // letter. With the letter leading, its left side bearing stood at the
        // field's left pad and the digits' at the right, and the two pads read
        // unequal ("the spacing does bother me"); with the digits leading and
        // the letter last, the letter's bearing sits at the right pad. THE
        // RESERVED CELL IS THE DIGITS' SPECIMEN, THE ` | ` AND THE WIDEST
        // LETTER'S SLOT, so it is ONE WIDTH ON EVERY TAB AND AT EVERY TIME;
        // the digits and the ` | ` start at the cell's left edge and the
        // letter stands left-aligned after them (kTimeShape's block), so no
        // tab switch moves the pipe or the digits.
        const std::string sep = " | ";
        const double sep_w = text_shape::shape_text_run(font, sep).width_px;
        const double cell_w = tm.time_w + sep_w + tm.letter_w;
        // THE CLOCK'S GROUP: two W of face in from the lane's left edge, the
        // gripper, the group's 5-W air, the field (its widest string and its
        // pads), the same air, the end bar.
        const int group_x = content.x + group_gap;
        paint_ce_gripper(cr, group_x, btn_y, btn_h);
        const int field_x = group_x + grip_w + grip_face;
        const int field_w = time_field_w_px(cell_w);
        const GuiRect clock_field =
            time_field_rect(field_x, field_w, btn_y, btn_h);
        paint_ce_time_field(cr, clock_field);
        const int end_x = field_x + field_w + end_face;
        paint_ce_end_bar(cr, end_x, content_y, content_h);
        // THE STATE LINE: on the row's free face, no panel, ITS FIRST INK
        // the state air (5 W) past the clock group's end bar — the field's
        // own symmetric air of 2026-10-08, the end bar now standing between
        // the field and the line: the clip starts there and the run's origin
        // stands its own left bearing short of it (ink_left_px,
        // text_shape.h). Its RIGHT CLIP stands one group gap short of the
        // right block; a window too narrow to leave that span positive paints
        // no state text — the row's crop-at-the-floor allowance (the block
        // above).
        const int state_x =
            end_x + end_w + scaled_px(kProgramSpec.state_air_px);
        const int state_w = (right_block_x - group_gap) - state_x;
        // THE BASELINE CENTRES THE FACE'S CAP BAND IN THE FIELD — the face
        // split above and below its digits by the one box solver. THE STATE
        // LINE TAKES IT TOO (the ONE BASELINE block below).
        const double baseline =
            redesign_baseline(font, static_cast<double>(clock_field.y),
                              static_cast<double>(clock_field.h));
        // THE CELL'S RIGHT EDGE IS THE FIELD'S RIGHT PAD — with no floor
        // (removed 2026-10-10; Cool Edit's mock of 2026-10-09 set one, the cell
        // centered in its surplus) the field is the cell and its two pads, so
        // this is the one case; its left edge, where the digits start, the
        // ceiled cell short of it.
        const int cell_ceil = static_cast<int>(std::ceil(cell_w));
        const int cell_end  = field_x + field_w - time_field_pad_px();
        const int cell_x    = cell_end - cell_ceil;

        // PUBLISH THE CELL FOR THE DAMAGE OWNER (clock_invalidate_rect,
        // app_state.h — the stash contract is at the field). One pixel of slack
        // on each side: the reserved width is an ADVANCE sum and a glyph's ink
        // may sit a hair outside it, and the band is the row's content height,
        // which contains the field and so the baseline's ink by construction.
        // THE RECT FOLLOWS THE CELL BECAUSE IT IS BUILT FROM IT — the run's
        // start above is the cell's left edge and so is this box's, so the
        // damage can never miss the painted digits or the letter. CEIL, NOT nearbyint:
        // cell_w is a fractional advance sum and this is a DAMAGE box, which
        // may be a hair too wide but never a hair too narrow.
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
        // ONE FACE, ONE BASELINE (the line architect 2026-10-03; the clock's
        // face 2026-10-05; the one baseline 2026-10-07):
        //   * THE CLOCK — the tab letter and the digits in the body face,
        //     Cool Edit's field ink, on the field's own seat. IT IS NEVER
        //     CLIPPED: the row's crop-at-its-floor allowance (the block above)
        //     is what covers the narrow window where the right block has
        //     already walked over this ground.
        //   * THE STATE — THE BODY FACE (gui_font(GuiFace::Body)) IN COOL
        //     EDIT'S PANEL LABEL PAIR (show_ce_label: the label tone with the
        //     dark tone one line right and down, METRICS §6 — 2026-10-09),
        //     straight on the row's face, ON THE CLOCK'S OWN BASELINE
        //     (architect 2026-10-07: "whichever one is right, the other
        //     should be centred to behave like it", and the field is right).
        //     The normal face over the small one is the architect's
        //     (2026-10-03): "I don't really read it" at the small face, and a
        //     running render's progress should read. IT IS CLIPPED, NEVER
        //     ELLIPSISED — a cairo rectangle clip, the folder overlay rows'
        //     precedent — so a long line is cut rather than colliding with
        //     the block; the composer puts the segment that must survive the
        //     cut ahead of the one that may lose it — the GitHub word before
        //     the scale (history_walk_line, architect 2026-09-29). With no
        //     span left nothing is shown.
        //
        // THE CLOCK CARRIES NO DIRTY MARK (architect 2026-10-05): SAVE'S GREY
        // IS THE MARK on both machines (plain_save_actionable, app_state.h).
        // THE ROW YIELDS WHOLE TO A MODAL, so the field and the line are
        // hidden while a prompt, a dialog editor, the render player or the
        // picker stands — not the color picker, which leaves the row standing
        // (modal_owns_bottom_row). It is why the render player's
        // load-under-a-running-render refusal says its sentence on a CARD:
        // the explanation the state line would have carried is not on screen
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
        // THE CLOCK, UNCLIPPED, IN THE FIXED CELL — the digits and ` | ` as
        // one run starting at the cell's left edge, then the tab letter
        // starting where that run ends, so the digits and the pipe stand
        // still whatever the letter.
        const text_shape::ShapedRun time_run =
            text_shape::shape_text_run(font, format_timestamp(seconds) + sep);
        const text_shape::ShapedRun letter_run = text_shape::shape_text_run(
            font, std::string(1, app.active_tab_view));
        const double run_x = static_cast<double>(cell_x);
        set_palette_source(cr, hex(kCeFieldText));
        text_shape::show_shaped_run(cr, time_run, run_x, baseline);
        text_shape::show_shaped_run(cr, letter_run,
                                    run_x + time_run.width_px, baseline);
        if (state_w > 0 && !state.empty()) {
            // The clock's face on THE CLOCK'S BASELINE (the block above), its
            // FIRST INK at the clip's left edge: the origin stands the run's
            // own left bearing short of state_x.
            const text_shape::ShapedRun state_run =
                text_shape::shape_text_run(font, state);
            cairo_save(cr);
            cairo_rectangle(cr, state_x, lane.y, state_w, lane.h);
            cairo_clip(cr);
            show_ce_label(cr, state_run,
                          static_cast<double>(state_x) -
                              text_shape::ink_left_px(state_run),
                          baseline);
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
    //   INFO — THE CARD FACE, the period's tooltip (architect 2026-10-04;
    //          the rule is render.h's palette block): `card_ground` with a
    //          THIN flat line of `card_frame`, ONE Windows px, ON ALL FOUR
    //          SIDES (architect 2026-10-06: WS_BORDER, ReactOS's tooltip and
    //          Windows 2000's) — each side laid on the face as a square cell
    //          rect, the bottom row and the right column last, so they own
    //          their corners whole as the captures' black does (a flat line,
    //          no bevel: the relief's mitre, paint_relief_frame, is not drawn
    //          here) — the tooltip and every notification card, whose words
    //          their painters set in `card_text`. A card keeps its own height
    //          rule: the line is drawn on its outer rows; its pad is the
    //          spec's (tooltip_pad_px).
    if (face == PopupFace::Menu) {
        paint_cell_rect(cr, r, palette().ground);
        paint_relief_plain_raised(cr, r);
    } else {
        const int lx = std::min(relief_line_px(), r.w);
        const int ly = std::min(relief_line_px(), r.h);
        paint_cell_rect(cr, r, palette().card_ground);
        paint_cell_rect(cr, GuiRect{r.x, r.y, r.w, ly}, palette().card_frame);
        paint_cell_rect(cr, GuiRect{r.x, r.y, lx, r.h}, palette().card_frame);
        paint_cell_rect(cr, GuiRect{r.x, r.y + r.h - ly, r.w, ly},
                        palette().card_frame);
        paint_cell_rect(cr, GuiRect{r.x + r.w - lx, r.y, lx, r.h},
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

    // THE PADS FROM THE BOX'S EDGE: the frame's line, then the air inside it
    // (the record above).
    const int frame = relief_line_px();
    const int pad_x = frame + scaled_px(live_chrome_spec().tooltip_pad_px);
    const int pad_y = frame + scaled_px(live_chrome_spec().tooltip_pad_px);
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
// at kPanelPadPx from the window's edge, its first card at the chrome spec's
// card seat — one relief line above the trim lane's first row, read off the
// live lane table, so the card's frame ends where the view bar begins
// under every chrome (architect 2026-10-08: "it looks like absolute
// positioning where it should be relative to the elements"; the rule and
// the margins at
// notification_stack_bound) — growing
// DOWN over whatever lies there (the icon row's empty right, the thin lanes,
// the waveform), the cards kNotificationGapPx apart (the card's own 1 px
// since 2026-10-01, the icon row's 2 before — the ruling at the constant).
//
// THE LOOK (architect 2026-10-02, the Windows-95 chrome; its colours
// 2026-10-04): THE CARD FACE — the tooltip's own look, `card_ground` with a
// thin `card_frame` line on all four sides (architect 2026-10-06, his word:
// the cards take the tooltip's look), its words `card_text` (render.h's
// palette block) —
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
    // THE CARD'S GLYPH BOX IS ITS OWN, SQUARE, its glyph Windows' small 16
    // px (notifications.h's kNotificationGlyph*: the small toolbar case's
    // height, decoupled from the live case 2026-10-06; the 32-laptop-px
    // square before 2026-10-02).
    const int btn      = notification_glyph_box_px();
    const int glyph_px = notification_glyph_px();
    // THE GLYPH INSET IS THE ROUNDED LEAD: the box is the lead, the glyph
    // and the lead, each rounded at the element (notification_glyph_box_px),
    // so `btn - glyph_px` is twice the lead and always even — 18 at the
    // tablet's 300 %, 8 at the laptop's 138 % — and this division is exact.
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

namespace {
// WINDOWS' MENU CHECK MARK (architect 2026-10-08, the "True Colors" row:
// dropdown_item_checked) — Marlett's check, the glyph DrawFrameControl's
// DFCS_MENUCHECK puts in a popup's check-mark column, a 7 x 7 staircase
// whose rows are (6, 1) / (5, 2) / (0, 1)(4, 3) / (0, 2)(3, 3) / (0, 5) /
// (1, 3) / (2, 1) as (x, width) runs. DRAWN AS THE STAIRCASE'S SMOOTH HULL,
// the caption's Close X's road (paint_caption_close_outline): the polygon
// through the runs' outer corners and the notch where the two arms' inner
// edges meet, in the cell's Windows px on the unit grid u, one path, one
// antialiased fill. The column is the popup's left pad (kPopupPadXPx, the
// space Windows reserves for this mark), the cell centred in it between the
// item box's left edge and the label's pen and centred on the item's
// height. Its ink is the row's label ink; a disabled row embosses it as the
// label is (Hilight one relief line right and down, Shadow in place —
// show_embossed_run's two tones).
struct MenuCheckPoint {
    double x, y;
};
constexpr int kMenuCheckCellPx = 7;
constexpr MenuCheckPoint kMenuCheckOutline[] = {
    {0, 2}, {1, 2}, {2.5, 3.5}, {6, 0}, {7, 0},
    {7, 3}, {3, 7}, {2, 7}, {0, 5}};

void fill_menu_check(cairo_t* cr, int gx, int gy, int u, GuiColor ink) {
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_new_path(cr);
    for (const MenuCheckPoint& p : kMenuCheckOutline)
        cairo_line_to(cr, gx + p.x * u, gy + p.y * u);
    cairo_close_path(cr);
    set_palette_source(cr, ink);
    cairo_fill(cr);
    cairo_restore(cr);
}

// The mark in the check-mark column of `item` (the published item box),
// the label's pen at `pen_x`, in `ink` — or embossed when `!enabled`.
void paint_menu_check(cairo_t* cr, const GuiRect& item, int pen_x,
                      bool enabled, GuiColor ink) {
    const int u    = scaled_px(1, 1);
    const int cell = kMenuCheckCellPx * u;
    const int gx   = item.x + static_cast<int>(std::nearbyint(
                                  (pen_x - item.x - cell) / 2.0));
    const int gy   = item.y + static_cast<int>(std::nearbyint(
                                  (item.h - cell) / 2.0));
    if (enabled) {
        fill_menu_check(cr, gx, gy, u, ink);
        return;
    }
    const int off = relief_line_px();
    fill_menu_check(cr, gx + off, gy + off, u, palette().hilight);
    fill_menu_check(cr, gx, gy, u, palette().shadow);
}
} // namespace

void GuiPaintHandler::paint_dropdown(cairo_t* cr) {
    // THE MENU ROW'S DROPDOWN — ONE painter for EVERY menu, hanging flush under
    // the menu bar at ZERO margin: its top edge is the row under the menu
    // bar's CONTENT — the program frame's top row since 2026-10-10, which it
    // covers while it stands (Windows' pull-down; dropdown_hang_y owns the
    // seat) — under the emitting button's left edge. Publishes its own
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
    // and the accelerator's right margin. ONE CHECK MARK STANDS IN THAT INDENT
    // SINCE 2026-10-08 (architect, the "True Colors" row; paint_menu_check):
    // the ruling's reason is that a badge restates what the picture shows,
    // and the sRGB conversion's state is the one state the picture cannot
    // show — the mark is Windows' own, in the column Windows reserved for
    // it, and no row but that one has a state (dropdown_item_checked).
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
    // THE ANCHOR: the menu row's button, as painted.
    const GuiRect& btn = app.redesign_buttons[redesign_button_index(
        dropdown_anchor_button(menu))].rect;
    if (btn.w <= 0 || btn.h <= 0) return;

    cairo_save(cr);
    const GuiFont font = gui_font(GuiFace::Body);

    const int border    = popup_border_px();
    const int side      = border;
    const int item_h    = popup_item_h_px();
    const int block_mar = popup_item_margin_y_px();
    const int margin_x  = live_chrome_spec().popup_margin_px;
    const int inset     = scaled_px(margin_x, margin_x > 0 ? 1 : 0);
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
    const int chrome_w = 2 * inset + 2 * side;
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

    // FLUSH WITH THE BUTTON IT EMITS FROM ON X, AND WITH THE MENU BAR'S
    // CONTENT ON Y. The x is the anchor's own left edge (architect
    // 2026-08-02), the published rect's, so the band's lead
    // (menu_band_lead_px) carries the box in with the anchor. THE Y IS THE
    // ROW UNDER THE CONTENT (dropdown_hang_y): a Windows pull-down drops from
    // the menu bar's bottom row and covers what stands beneath it, so the box
    // stands on the program frame's top row and covers it while the menu is
    // down (2026-10-10), as the box covered the lane's 1-px margin strip under
    // the anchor when the architect ruled the BUTTON's foot (2026-08-02). It
    // is read from the lane's content rect (menu_row_content_rect of
    // top_menu_row_area) rather than from btn.y + btn.h because the anchor's
    // pill is the whole lane (render.h's menu-row block); the row ending at
    // its content, the two agree.
    int x = btn.x;
    int y = dropdown_hang_y(app, menu);   // flush: zero margin under the content
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
    int iy = y + popup_border_top_px(/*upward=*/false) + block_mar;
    for (int i = 0; i < count; ++i) {
        const DropdownRow row = dropdown_row(menu, i);
        if (row.separator_before) {
            // THE SEPARATOR IS ETCHED (architect 2026-10-02, Windows' menu
            // separator), inset horizontally: its Shadow line and its Hilight
            // line under it, with the block's own vertical margin above and
            // below the pair (popup_sep_block_px, dropdown_h_px's sum) — the
            // vocabulary's own painter (paint_popup_separator).
            paint_popup_separator(cr, x, x + w, /*at_bar=*/false, iy + sep_mar);
            iy += sep_block;
        }
        // ITEMS TOUCH — zero vertical gap between adjacent ones — and each
        // one's box insets horizontally from the frame by the one-px margin —
        // inside the frame's two lines, then the margin, on both sides (the
        // width chrome_w budgets is 2·inset + 2·border). The published rect is
        // that box, so the clickable area is exactly the area that lights; the
        // press claim reads it (dropdown.item_rects), and the label and the
        // accelerator are placed from the popup's own edges, not from it.
        const GuiRect item{x + side + inset, iy, item_w, item_h};
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
        const GuiColor row_ink =
            lit ? palette().selected_text : palette().label;
        const auto show_row_run = [&](const text_shape::ShapedRun& r,
                                      double rx) {
            if (!enabled[i]) {
                show_embossed_run(cr, r, rx, base);
                return;
            }
            set_palette_source(cr, row_ink);
            text_shape::show_shaped_run(cr, r, rx, base);
        };
        show_row_run(runs[i], static_cast<double>(x + pad_l));
        // THE CHECK MARK (paint_menu_check), in the label's ink.
        if (dropdown_item_checked(menu, i))
            paint_menu_check(cr, item, x + pad_l, enabled[i], row_ink);

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

// -- THE RULER LANE (top lane 6) ---------------------------------------------
//
// A LOOK/MODEL SPLIT, and it is deliberate: the ruler takes COOL EDIT'S LOOK
// and REAPER'S GEOMETRY MODEL.
//   LOOK, Cool Edit's FLIPPED RULER (architect 2026-10-09, the program is
//     Cool Edit; METRICS §4.4, the mock of record; render.h's canvas-column
//     paragraph): the Face-derived ground under the view bar's light line,
//     its own light bottom line, the ticks E0E0E0 STANDING ON THE GROUND'S
//     BOTTOM ROW — majors four W, minors two — and the digits over a black
//     (+1, +1) shadow; THE GROUND 11 W, THE DIGITS THE BASE'S SIX-ROW SMALL
//     DIGIT on its rows 1 .. 6 under one row of air, their baseline the top
//     of row 7 (architect 2026-10-09 ~16:50 / ~21:00, the product's rows
//     where Cool Edit's ruler is 17 with cap-7 digits: "one Windows pixel of
//     empty space above", the six rows saved given to the marker lane;
//     program_spec.h's ruler fields). (Kdenlive's look — the majors rising
//     above the flags, the etched ticks — stood 2026-08-01 to 2026-10-09;
//     git history.)
//   MODEL, from Reaper (architect 2026-08-01): WHERE the ticks go. A round
//     ladder of labeled steps, the smallest rung whose label pitch clears the
//     minimum, and eight binary minors inside each step; the labels' text.
//
// PAINT-ONLY. Nothing here snaps, authors, or hit-tests: the ladder decides
// pixels and nothing else. (The lane keeps its PLACEMENT-LANE role for the
// pointer, point_on_placement_lanes — a press on the ruler places the
// playhead — which reads the lane's rect, never this ladder; the playhead's
// head, which the ruler pass paints after the ladder, publishes its own hit
// for the head drag, paint_ruler_row's head block.)
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

// THE COMB'S MAJORS — the ladder's step on a displayed basis and the one
// expression that places a major on the screen grid, the ruler's walk's
// (paint_ruler_row; the canvas draws no vertical grid on them, render.h's
// row-6 canvas paragraph, 2026-10-09). `vp_ms` and `ms_per_px` the basis in
// milliseconds, `wave_w` the columns the walk spans; `valid` false on a
// basis with no span.
struct RulerComb {
    bool    valid     = false;
    double  ms_per_px = 0.0;
    double  vp_ms     = 0.0;
    int64_t step      = 0;
    int     wave_w    = 0;
    // A step index's own rounded column, waveform-relative: the ONE place a
    // tick position meets the screen grid (the walk's rigid-comb rule).
    int major_col(int64_t k) const {
        const double t = static_cast<double>(k) * static_cast<double>(step);
        return static_cast<int>(std::nearbyint((t - vp_ms) / ms_per_px));
    }
    // The walk's first step, whose major stands at or left of column 0.
    int64_t first_step() const {
        return static_cast<int64_t>(std::floor(vp_ms / step));
    }
    double end_ms() const { return vp_ms + ms_per_px * wave_w; }
};
RulerComb ruler_comb(double spp, double vp_start, int sr, int wave_w) {
    RulerComb c;
    if (spp <= 0.0 || sr <= 0 || wave_w <= 0) return c;
    c.ms_per_px = spp * 1000.0 / static_cast<double>(sr);
    c.vp_ms     = vp_start * 1000.0 / static_cast<double>(sr);
    if (c.ms_per_px <= 0.0) return c;
    c.step   = ruler_step_ms(c.ms_per_px);
    c.wave_w = wave_w;
    c.valid  = true;
    return c;
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

} // namespace

void GuiPaintHandler::paint_ruler_row(cairo_t* cr) {
    const GuiRect lane   = top_ruler_row_area(app);
    const GuiRect marker = top_marker_row_area(app);
    // THE HEAD'S HIT IS PUBLISHED AS PAINTED (AppState::playhead_head_hit;
    // architect 2026-10-09 ~17:40, the head drag): `head` the rect this run
    // paints the head in (the head block below), empty where none paints.
    // A run republishes only what its damage clip redrew — the trim stash's
    // rule (paint_trim), for its reason: on_redraw runs once per damage
    // rect, and a run under a rect that missed the head would stamp the
    // stash with a head it never drew. A head this run paints is published
    // when the clip covers it; a head that paints nowhere is published (cold,
    // nothing to grab) when the clip covers the head last published, whose
    // pixels this run erased. Every move of the playhead damages the head's
    // old and new columns from the window's top (playhead_invalidate_rect,
    // main.cpp; the discrete moves' Viewport::invalidate_waveform_area), so
    // the frame that shows a moved head is always a publishing one.
    const auto publish_head = [&](const GuiRect& head) {
        const GuiRect last = app.playhead_head_hit;
        if (head.w > 0 && head.h > 0) {
            if (clip_covers_drawable(cr, app, head))
                app.playhead_head_hit = head;
        } else if (last.w <= 0 || last.h <= 0 ||
                   clip_covers_drawable(cr, app, last)) {
            app.playhead_head_hit = GuiRect{0, 0, 0, 0};
        }
    };
    if (lane.w <= 0 || lane.h <= 0) {
        publish_head(GuiRect{0, 0, 0, 0});
        return;
    }

    cairo_save(cr);
    // THE LANE'S FACE, then THE GROUND AND ITS FRAME over the canvas's
    // columns (render.h's canvas-column paragraph, below once the width is
    // read): the column's margins beside them stay the panel's face, as the
    // view bar's do. THE MARKER LANE'S PANEL FACE is laid here
    // too, before the flag blit, the lane's ground under every cue (the flag
    // cache paints the cues alone).
    const GuiPalette& pal = palette();
    const int ground_h = ruler_ground_h_px();
    const int lw       = program_line_px();
    paint_cell_rect(cr, lane, pal.face);
    if (marker.w > 0 && marker.h > 0) paint_cell_rect(cr, marker, pal.face);

    // THE DISPLAYED BASIS, not the live viewport: the ruler must agree with the
    // pixels actually on screen, so it reads the same plate epoch the playheads
    // and the flag cache do. That is also what hooks it to the per-pan/zoom
    // repaint — every user-driven viewport change runs the synchronous plate
    // rebuild and repaints the strip, and this pass rides along with it.
    const PlateViewportBasis basis = plate_viewport_basis();
    const int sr = audio.sample_rate();
    if (basis.spp <= 0.0 || sr <= 0) {
        publish_head(GuiRect{0, 0, 0, 0});
        cairo_restore(cr);
        return;
    }
    // THE RULER'S COLUMNS ARE THE CANVAS'S (2026-10-09, the column's
    // margins): the ground and every tick, digit and the head from the
    // waveform area's x, the frame's side columns one line outside it.
    const GuiRect area = waveform_area(app);
    const int cx = area.x;

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
                                              : area.w;
    const RulerComb comb = ruler_comb(basis.spp, basis.vp_start, sr, wave_w);
    if (!comb.valid) {
        publish_head(GuiRect{0, 0, 0, 0});
        cairo_restore(cr);
        return;
    }
    // THE GROUND AND ITS FRAME over the canvas's columns (METRICS §4.1, the
    // mock's ruler): `ce_mid` ground, then the frame's L — the `ce_dark` left
    // column beside the ground and the `ce_hilight` foot line and right
    // column — as the canvas's ring would draw it one row up, the row above
    // (the view bar's light line, its own) cut away by the lane's clip, so the
    // dark column meets the light foot MITRED at the bottom-left
    // (paint_relief_frame, cool_edit_paint.h's diagonal rule). The ground and
    // the frame read the LIVE area, as the canvas's own frame does
    // (paint_canvas_column_frame); the walk below reads the plate's width.
    paint_cell_rect(cr, GuiRect{cx, lane.y, area.w, ground_h}, pal.ce_mid);
    cairo_save(cr);
    cairo_rectangle(cr, lane.x, lane.y, lane.w, ground_h + lw);
    cairo_clip(cr);
    paint_relief_frame(cr, GuiRect{cx - lw, lane.y - lw, area.w + 2 * lw,
                                   ground_h + 2 * lw},
                       pal.ce_dark, pal.ce_hilight);
    cairo_restore(cr);

    const int64_t step   = comb.step;
    const double  end_ms = comb.end_ms();
    // (There is no `minor` time step any more. It had two consumers — the float
    // placement of each minor tick and the head's since-deleted float
    // re-derivation of which columns carried one — and the rigid comb replaced
    // both with integer
    // distribution across a segment. The MINORS-PER-STEP count is still the
    // ladder's own kRulerMinorsPerStep; only its expression as a duration is
    // gone.)

    // THE TICKS STAND ON THE GROUND'S BOTTOM ROW (METRICS §4.4, the mock's
    // flipped ruler): each one quantum wide in kCeRulerTick, rising the
    // major's four W or the minor's two into the ground from its foot — its
    // rows 7 .. 10 and 9 .. 10 of 0 .. 10.
    const int ground_bottom = lane.y + ground_h;   // one past the ground's rows
    const int major_top = ground_bottom - scaled_px(kProgramSpec.ruler_major_tick_px);
    const int minor_top = ground_bottom - scaled_px(kProgramSpec.ruler_minor_tick_px);

    // THE DIGITS: THE SMALL FACE (gui_font.h: every set's small face is the
    // base's six-row digit, WordPad's ruler digit, its cell all above the
    // baseline; architect 2026-10-09 ~16:50, Cool Edit's cap-7 digits read
    // "very small" anyway), their BASELINE THE TOP OF THE GROUND'S ROW 7 —
    // the digits on rows 1 .. 6 under row 0's air, "basically touching the
    // trim bar", the black (+1, +1) shadow ending on row 7, a major tick's
    // top row — an authored row of the lane, neither solver asked
    // (redesign_baseline's block).
    const GuiFont font = gui_font(GuiFace::Small);
    const double baseline =
        static_cast<double>(lane.y + scaled_px(kProgramSpec.ruler_baseline_px));

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
    const int64_t first_step = comb.first_step();
    // A step index's own rounded column: the ONE place a tick position meets
    // the screen grid (RulerComb::major_col). Majors anchor here; minors are
    // distributed between them.
    const auto major_col = [&](int64_t k) { return comb.major_col(k); };
    // THE LABELS ARE DROPPED AT BOTH ENDS (architect 2026-10-09, the mock's
    // edge-drop rule, retiring 2026-10-05's slide): a label CENTERED on its
    // major's column whose box — its run and the shadow's quantum — would
    // cross either end of the ruler's columns [0, wave_w) is not painted, so
    // no digit is ever cut. The walk therefore starts at first_step, whose
    // major stands at or left of column 0.
    // THE TICK'S WIDTH IS THE PROGRAM'S QUANTUM (off the canvas: "otherwise,
    // scaled", waveform_line_px's rule, 2026-10-09).
    const int t = program_line_px();
    // ONE MAJOR'S LABEL at its column `col` and its step time `step_ms`,
    // CENTERED ON THE MAJOR'S COLUMN (the tick's own t wide, the run's centre
    // on the tick's). The label's TIME is still the exact step time — only
    // tick PLACEMENT is distributed, and a major is at its own exact time
    // anyway. Its x rides `col`, which for a major IS the rounded major, so
    // number and line cannot drift apart.
    const auto paint_major_label = [&](int col, double step_ms) {
        // step_ms is INTEGRAL BY CONSTRUCTION (k * step, both int64, the
        // product exact in double at any ruler magnitude), so this is a
        // representation change, not a rounding: llrint reads the integer
        // back and can never meet a tie. (nearbyint is the rule where a
        // fraction is actually rounded; the trim bar's displayed_trim_ms
        // cast makes the same integral-valued claim.)
        const int64_t label_ms = static_cast<int64_t>(std::llrint(step_ms));
        if (label_ms < 0) return;
        const std::string txt = ruler_label_text(label_ms, step);
        const text_shape::ShapedRun run =
            text_shape::shape_text_run(font, txt.c_str());
        const int run_w = static_cast<int>(std::ceil(run.width_px));
        const int lx = col + t / 2 - run_w / 2;   // within the columns
        if (lx < 0 || lx + run_w + t > wave_w) return;   // the edge-drop
        // EVERY LABEL IS ONE COLOR (architect 2026-10-02, "give the same
        // colour to all the numbers"): the tick's ink over Cool Edit's
        // black (+1, +1) shadow (METRICS §4.4).
        const double x = static_cast<double>(cx + lx);
        set_palette_source(cr, hex(kCeRulerShadow));
        text_shape::show_shaped_run(cr, run, x + t, baseline + t);
        set_palette_source(cr, hex(kCeRulerTick));
        text_shape::show_shaped_run(cr, run, x, baseline);
    };
    // THE WALK'S CLIP: the waveform's columns [0, wave_w) over the ground's
    // rows, so a tick past the last column and the leftover strip a
    // non-multiple-of-16 window leaves beside wave_w carry nothing.
    cairo_save(cr);
    cairo_rectangle(cr, cx, lane.y, wave_w, ground_h);
    cairo_clip(cr);
    for (int64_t k = first_step; ; ++k) {
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
            // A MAJOR'S LABEL PAINTS BEFORE ITS TICK, so the tick stands whole
            // over the digits' shadow where the shadow's last row meets the
            // major's top row (program_spec.h's ruler fields, 2026-10-09
            // ~21:00; the mock of record paints the ticks over). Only the
            // label's own major can meet it: a minor rises to row 9, below the
            // shadow's last row 7, and the next major stands a whole step —
            // at least eight minimum minor pitches — away.
            if (major) paint_major_label(col, step_ms);
            // One quantum wide (program_line_px; render.h's waveform_line_px
            // inventory), left edge on the tick's own column, clipped at the
            // right edge, in kCeRulerTick, standing on the ground's bottom row.
            if (tick_on_lane) {
                set_palette_source(cr, hex(kCeRulerTick));
                fill_waveform_line(cr, cx, wave_w, col,
                                   major ? major_top : minor_top,
                                   ground_bottom, t);
            }
        }
    }
    cairo_restore(cr);

    // -- THE PLAYHEAD'S HEAD ---------------------------------------------------
    //
    // THE HEAD IS COOL EDIT'S CURSOR TRIANGLE IN THE RULER (architect
    // 2026-10-09, the mock of record; METRICS §4.3; render.h's playhead
    // paragraph): the cue's own 9-7-5-3-1 quanta drawn as their envelope,
    // one antialiased triangle (paint_ce_cue_triangle, 2026-10-09 ~12:40), in
    // the palette's `playhead_stem` (Cool Edit's Curs yellow FFFF00 by
    // default, 2026-10-09) over the same triangle one quantum right in
    // `ce_cue_shadow`, ON THE GROUND'S ROWS 6 .. 10, its apex — the bottom
    // centre of the playhead's one-quantum column — on the ground's bottom
    // row, its top row the digits' last (2026-10-09 ~21:00, the 11-W
    // ground). It is OPAQUE over the ticks and digits the walk above laid
    // down, covering whatever stands under it (its diagonal edges
    // antialiased over them; the mock of record). NO SNAP, NO
    // AVOIDANCE: it may stand on a marker's column, the cue's triangle being
    // in the marker lane below (the coincident-stem rule is retired, render.h's
    // playhead paragraph). It draws NOTHING across the view bar, the marker
    // lane and the canvas's top frame row; its canvas dots are
    // paint_playheads'. CENTRED ON THE PLAYHEAD'S ONE-PX COLUMN (architect
    // 2026-10-09 ~14:35; paint_ce_cue_triangle).
    //
    // The whole head is the RESTING CURSOR'S: the `h` view, the render player
    // and the audition reach it through this one block, and the scanner keeps
    // its bare waveform line (paint_scanner).
    //
    // THE HALF-HEAD RULE (2026-05-09, d4e4e04e; restored 2026-09-26, architect):
    // the head paints whenever any part of it overlaps the waveform's columns
    // [0, wave_w), CLIPPED to them, so a playhead whose column lies just past
    // either edge shows the head's nearer half there — a frame in the song's
    // last half-column rounds to grid point wave_w, one past the last column
    // (End's landing at the whole-song zoom), and the head's left half at the
    // edge keeps that playhead on screen at its true point.
    {
        const double cursor_px = playhead_pixel_x(
            app, static_cast<int64_t>(basis.vp_start), basis.spp);
        const int col   = static_cast<int>(std::nearbyint(cursor_px));
        const int rows  = cue_triangle_h_px();
        // The head's painted columns [col − left, col + right) (render.h's
        // one extent, the damage box's too).
        const int left  = cue_triangle_half_w_px();
        const int right = cue_triangle_reach_right_px();
        GuiRect head_hit{0, 0, 0, 0};
        if (col + right - 1 >= 0 && col - left <= wave_w - 1) {
            const int head_top = ground_bottom - rows;
            cairo_save(cr);
            cairo_rectangle(cr, cx, head_top, wave_w, rows);
            cairo_clip(cr);
            paint_ce_cue_triangle(cr, cx + col, head_top,
                                  palette().playhead_stem);
            cairo_restore(cr);
            // THE HEAD'S HIT (architect 2026-10-09 ~17:40, the head drag;
            // point_on_playhead_head, input_pointer.cpp): EXACTLY THE PAINTED
            // HEAD — its triangle's and shadow's columns [col − left,
            // col + right) cut to the waveform's columns as the clip above
            // cuts them, over the head's five quanta of the ruler's rows — and
            // nothing beyond it ("I don't think we should get in the habit of
            // making invisible hitboxes"), published by the pass's rule
            // (publish_head, the pass's head).
            const int x0 = std::max(0, col - left);
            const int x1 = std::min(wave_w, col + right);
            head_hit = GuiRect{cx + x0, head_top, x1 - x0, rows};
        }
        publish_head(head_hit);
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
    // The clip is the AREA, the canvas's interior (2026-10-09: the canvas's
    // frame stands outside it), so a plate published at a stale size never
    // paints over the frame.
    if (wf_cache.surface) {
        cairo_save(cr);
        cairo_rectangle(cr, area.x, area.y, area.w, area.h);
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
    // The reset's CLASS, for the ring's dots: the column's RESTING red set
    // keyed by store index, the set and the index the flag pass reads for
    // this reset's stem — at rest and through a drag alike, the drag writing
    // only its proposal, so the ring and the stem share one class throughout.
    bool red_class = false;
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
    // (left_col) is its left side, which the ring leaves to the stem
    // (paint_phase_reset_overlay_ring).
    double x0 = static_cast<double>(area.x + left_col);
    double x1 = static_cast<double>(area.x + right_col);
    const double stem_x = x0;

    // Horizontal clip to [area.x, area.x + area.w); the band shows whenever the
    // intersection is non-empty even if the stem column is off-screen left
    // (the tail can be visible while the stem is not).
    x0 = std::max(x0, static_cast<double>(area.x));
    x1 = std::min(x1, static_cast<double>(area.x + area.w));
    if (x1 <= x0) return out;

    out.valid  = true;
    out.x0     = x0;
    out.x1     = x1;
    out.stem_x = stem_x;
    out.red    = red_class;
    return out;
}

// THE OVERLAY RING — the phase-reset overlay's WHOLE visual (architect
// 2026-07-27): the band's border and nothing else, no fill, so the band
// READS as the edges of a span rather than as a tinted region, painted
// AFTER the plate and over the ink.
//
// THE RING IS DOTTED IN THE STEM'S OWN PATTERN, NEVER A SOLID LINE
// (architect 2026-10-09 ~17:40: the ring is "made of the same stuff the
// marker looks like"; "they're one unit", 2026-08-01): every cell one device
// px (waveform_line_px, "on waveform → unscaled"), in the reset's own two dot
// colors exactly as its stem wears them — phase_reset_stem_dots asks the one
// resolver (resolve_flag_face) for the stem's pair, so the ring and the stem
// cannot drift: a valid reset the red `cue` and the blue `range`
// alternating, an invalid one the red alone; resting whether the reset is
// selected or not, as the stem is.
//   THE LEFT SIDE IS THE STEM'S OWN COLUMN and the ring does not paint it:
//     the reset's stem already stands there (paint_marker_stems, after this
//     pass), and a playhead on the reset interleaves its yellow there as on
//     any stem — "yellow, blue, yellow, red all the way around". A stem off
//     the area's left edge leaves the clipped band with no left side.
//   THE RIGHT SIDE on the band's last column, the stem's rule exactly
//     (program_spec.h's dot fields: the blue on the canvas rows ≡
//     range_dot_phase and the red on ≡ cue_dot_phase, mod cue_dot_period,
//     counted from the canvas's first row) — fill_dotted_waveform_line, the
//     stem's own painter. A band clipped at the area's right edge draws it
//     there, flush, as the trim bridge's clipped fill reads.
//   THE TOP RUN on the canvas's first row and THE BOTTOM RUN on its last:
//     the same two phases turned through the corners, counted in device
//     columns from the reset's own column (band.stem_x, unclipped, so the
//     run stays put on the song as the view pans): the blue on columns
//     stem + range_dot_phase + k · cue_dot_period, the red on stem +
//     cue_dot_phase + k · cue_dot_period — one dot every four columns,
//     alternating, as down the stem. The corners carry whatever
//     cell the rules give them; a band one column wide is its stem alone.
// PURE PALETTE BYTES (the palette composites nothing): aliased integer
// cell rectangles, the stems' own form, no alpha and no half column. The
// band's edges are whole columns by construction (displayed_column_at's
// integers off the area's x).
// THE CANVAS'S ROWS (architect 2026-10-06; the area whole since
// 2026-10-09): the runs land on the canvas's first and last rows, the
// canvas's frame round the area staying whole under a reset's span. No
// other reader takes the ring's cells: it is no hit target, and its damage
// is the whole waveform area's (the flag cache's rebuild and
// Selection::damage_overlay_on_subject_change), which holds it whole.
// DAMAGE: this pass paints live in on_redraw from app state, never from a
// cached surface, and every change to its dots' inputs misses the flag
// cache's fingerprint, whose rebuild damages the waveform with the strip
// (maybe_rebuild_flag_cache, waveform_cache.cpp) — the stem's own repaint;
// a palette install damages the whole window.
void GuiPaintHandler::paint_phase_reset_overlay_ring(
    cairo_t* cr, const GuiRect& area) {
    const PhaseResetOverlayBand band = phase_reset_overlay_band(area);
    if (!band.valid) return;

    const GuiStemDots dots = phase_reset_stem_dots(band.red);
    const ProgramSpec& ps = kProgramSpec;
    const int t      = waveform_line_px();
    const int x0     = static_cast<int>(std::nearbyint(band.x0));
    const int x1     = static_cast<int>(std::nearbyint(band.x1));
    const int stem   = static_cast<int>(std::nearbyint(band.stem_x));
    const int period = ps.cue_dot_period;
    const int y_top  = area.y;
    const int y_bot  = area.y + area.h - t;

    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    const auto paint_dots = [&](const GuiColor& color, int phase) {
        set_palette_source(cr, color);
        // The top and bottom runs: the first column at or past x0 whose
        // offset from the stem is ≡ phase (mod period).
        const int off   = x0 - stem;
        const int first = x0 + (((phase - off) % period) + period) % period;
        for (int x = first; x < x1; x += period) {
            cairo_rectangle(cr, static_cast<double>(x),
                            static_cast<double>(y_top),
                            static_cast<double>(t), static_cast<double>(t));
            cairo_rectangle(cr, static_cast<double>(x),
                            static_cast<double>(y_bot),
                            static_cast<double>(t), static_cast<double>(t));
        }
        cairo_fill(cr);
        // The right side on the band's last column.
        fill_dotted_waveform_line(cr, area.x, area.w, x1 - t - area.x,
                                  area.y, area.y + area.h, period, phase);
    };
    if (dots.range_dots) paint_dots(palette().range, ps.range_dot_phase);
    if (dots.cue_dots) paint_dots(palette().cue, ps.cue_dot_phase);
    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_trim -----------------------------------------

// The LIVE trim pass: every trim pixel — the view bar's ring and field and
// the span (render_trim_flags), and the column's air above it — paints here
// per frame, entirely inside those two lanes,
// which no later pass paints on; its slot is in the paint-order block in
// on_redraw (the one authoritative sequence).
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
    // THE COLUMN'S AIR above the view bar (top lane 4, render.h's canvas
    // column lanes): 5 W of the panel face under the band's last line
    // (METRICS §4.1), the lane's whole width inside the program's frame,
    // painted with the bar it heads.
    // The trim lane's own ground the same face, under the view bar, so the
    // column's margins beside the bar's frame (render_trim_flags spans the
    // canvas's columns and its two frame columns alone, 2026-10-09) read as
    // the panel.
    const GuiRect trim_row = top_trim_row_area(app);
    {
        const GuiRect air = top_column_air_area(app);
        if (air.w > 0 && air.h > 0) paint_cell_rect(cr, air, palette().face);
        if (trim_row.w > 0 && trim_row.h > 0)
            paint_cell_rect(cr, trim_row, palette().face);
    }
    // The ITEM basis (free owner; the member plate_viewport_basis is the other
    // epoch — see the header comment above), read ahead of the gate below
    // because the covering test measures the lane at its painted width.
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    TrimBarHit* const out_hit =
        clip_covers_drawable(cr, app,
                             GuiRect{area.x, trim_row.y,
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
    // NO WAVEFORM STEMS (architect 2026-08-01): the view bar is the trim
    // window's WHOLE display.
    // NO HELD FACE (2026-10-09): the view bar draws no caps, so a grab shows
    // nothing pressed (the held scroll arrow of 2026-10-03 went with them).
    // NO PLAYHEAD IN THE BAR (architect 2026-10-09 ~14:30, "no dots on the
    // trim bar"; render.h's view-bar paragraph).
    render_trim_flags(cr, top_strip, trim_row, wave_rect,
                      basis.vp_start_frame, basis.vp_end_frame, trim,
                      out_hit);
}

// -- GuiPaintHandler::paint_marker_stems ---------------------------------

// EVERY ENABLED MARKER STEMS, ALWAYS (row 5, architect): the per-frame waveform
// overlay that replaced the singleton selected-marker stem. The full contract —
// what stems and in which of the two cue colors — is at the declaration.
//
// It reads the marker painter's stash (app.marker_stems) instead of walking a
// store: the stem stands on its cue's APEX COLUMN, and that column was
// already resolved by the pass that painted the cue, on the displayed basis
// those pixels were laid out against. So the DragOverlay substitution, the
// source->target map walk, the per-marker cull and the color resolver all happen
// exactly once, in the painter, and a stem can never land a pixel away from its
// own triangle. Disabled markers are simply absent from the stash.
//
// THE STEM IS COOL EDIT'S DOTTED COLUMN IN ITS TWO CUE COLORS (architect
// 2026-10-09, METRICS §4.2; the two colors ~11:50; render.h's marker-lane
// paragraph): one-device-px dots on the CANVAS'S device rows from its top
// row (fill_dotted_waveform_line, the area whole) — none on the canvas's
// frame row and none in the lane — THE RED (`cue`) on rows ≡ cue_dot_phase
// and THE BLUE (`range`) on rows ≡ range_dot_phase (mod cue_dot_period,
// program_spec.h), each where the stash says the cue wears it
// (resolve_flag_face's table: a warp or phase-reset cue both, alternating
// one dot every four rows; an invalid one and the history's removed half
// the red alone, the added half the blue alone) — the playhead's dots on
// rows ≡ 1 of 4, so neither color shares a row with them. (The solid stem
// through the well's top lines between its flanks stood 2026-10-02 to
// 2026-10-09; git history.)
//
// Z-ORDER (architect 2026-09-23): the stems paint UNDER the playhead's dots,
// which follow this pass; on a shared column the phases never touch.
// The full sequence is the paint-order block in on_redraw.
void GuiPaintHandler::paint_marker_stems(cairo_t* cr, const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    if (app.marker_stems.empty()) return;

    // THE STEMS PAINT AS PUBLISHED (architect 2026-10-03): an open editor's
    // refused commit recolors nothing, so no paint-time override stands
    // here — the stash's two bits are the marker's resolved class, the
    // whole answer, and the colors the palette's two live roles.
    cairo_save(cr);
    const GuiRect& band = area;   // the canvas, the area whole (2026-10-09)
    const ProgramSpec& ps = kProgramSpec;
    for (const MarkerStem& stem : app.marker_stems) {
        // Column-gate exactly like render_playhead's line does. The producers
        // already publish only columns in [0, w) (stem_column_on_waveform,
        // render.cpp); fill_dotted_waveform_line (render.h) restates that
        // gate against the area this painter is handed, so no entry can leak
        // its column into the chrome beside the waveform, and clips the
        // dots' waveform_line_px() width at the right edge.
        const int col = static_cast<int>(std::nearbyint(
            stem.x - static_cast<double>(area.x)));
        if (stem.cue_dots) {
            set_palette_source(cr, palette().cue);
            fill_dotted_waveform_line(cr, area.x, area.w, col, band.y,
                                      band.y + band.h, ps.cue_dot_period,
                                      ps.cue_dot_phase);
        }
        if (stem.range_dots) {
            set_palette_source(cr, palette().range);
            fill_dotted_waveform_line(cr, area.x, area.w, col, band.y,
                                      band.y + band.h, ps.cue_dot_period,
                                      ps.range_dot_phase);
        }
    }
    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_strip_drag_anchor ----------------------------

// Paints the anchor stem (the Ableton pivot affordance) at the zoom anchor's
// current column, full waveform height — a live zoom gesture's, or the S
// Pen's retained one between strokes — WHITE, in the scanner's role: the
// stems that belong to the controls are white and the playhead alone is
// yellow (architect 2026-10-09; render_strip_anchor_stem). TWO PRODUCERS,
// ONE STEM:
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
    // THIS PASS IS THE CURSOR'S CANVAS RUN, nothing else: its HEAD is the
    // yellow triangle in the ruler (paint_ruler_row); across the view bar
    // (architect 2026-10-09 ~14:30), the marker lane and the canvas's top
    // frame row the playhead draws nothing (Cool Edit puts no dot on its
    // frame row).
    //
    // THE CURSOR PLAYHEAD ALWAYS PAINTS (architect 2026-07-30): ONE playhead
    // form at the resting cursor column whatever the selection is doing —
    // since 2026-10-09 COOL EDIT'S DOTTED COLUMN (render.h's playhead
    // paragraph): one-device-px dots in the palette's `playhead_stem` (Cool
    // Edit's Curs yellow by default) on the canvas's device rows ≡ 1
    // (mod 4), the cues' dots on rows ≡ 3, so a playhead on a marker's
    // column interleaves with its dots and neither yields. It paints AFTER
    // paint_marker_stems (architect 2026-09-23) and before the scanner.
    //
    // THE SCANNER LEFT THIS PASS (architect 2026-08-01) — it is paint_scanner,
    // invoked after this one, solid over everything in the canvas while it
    // runs, this cursor included.
    render_playhead(cr, area, px_x, palette().playhead_stem,
                    PlayheadForm::Dotted);
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
// THE SCANNER IS THE MOVING LINE, and it HAS ITS OWN ROLE, `scanner`
// (architect 2026-10-05), WHITE in every built-in palette (architect
// 2026-10-07: "let's go back to a white scanner") — SOLID, ONE DEVICE PX
// wide at every scale (architect 2026-10-09 ~14:25, "on waveform →
// unscaled", waveform_line_px's rule, render.h), the canvas's rows alone
// (PlayheadForm::Solid; Cool Edit's playback cursor is a solid 1-px line,
// measured 2026-10-09).
//
// It stays WAVEFORM-ONLY: no head, no lane presence, nothing in the top strip
// (render_playhead is shared with the cursor and cannot reach a strip lane at
// all: it draws inside `area`'s canvas and nothing else).
// Same displayed-plate basis the cursor uses, so both ride the blitted pixels
// through a worker rebuild; the value fields it reads are meaningful only while
// active, which is exactly what the gate asks.
void GuiPaintHandler::paint_scanner(cairo_t* cr, const GuiRect& area) {
    if (!app.playhead_scanner_active) return;

    const PlateViewportBasis basis = plate_viewport_basis();
    const double scan_px =
        scanner_pixel_x(app, wf_cache.fp_vp_start, basis.spp);
    render_playhead(cr, area, scan_px, palette().scanner,
                    PlayheadForm::Solid);
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
        // A CHOICE EDITOR'S LABEL IS ITS ROW'S (2026-10-07 evening): the
        // menu row's own word ("Chrome") in the dialogs' `<word>:` form, the
        // combo beside it carrying no key to read; the text editor keeps
        // "Setting:" before its `key=value` line.
        prefix = app.settings_choice_live()
                     ? std::string(app.settings_choice_row().label) + ":"
                     : std::string(kSettingsEditorPrefix);
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
//
// THE COLOR PICKER IS ABSENT TOO, the fifth ModalDialogOwner and the one
// that leaves the row standing (architect 2026-10-07 evening, "a different
// type of modal"): its surface is the card on the well, so ROW 8 STAYS
// PAINTED AND ACTING under it exactly as without it — the clock, the state
// line and every roster button with its truthful enabling, its hot face,
// its tooltip, its press-and-lift and its hold-repeat — AND SO DOES THE
// ICON ROW (architect 2026-10-08, "the fact that the top row is disabled
// under the picker also needs to be updated"). THE DELIBERATE ASYMMETRY,
// recorded here: under the color picker THE TWO BUTTON ROWS ARE ON for the
// POINTER AND THE PEN ALONE, and the picker veils THE WELL, THE MENU ROW and
// THE KEYBOARD — the keyboard stays the picker's (its router consumes every
// key, on_key, so the hex field types on glass), a press on the well is the
// card's or consumed, and the menu row is the list owners' (its Edit and
// Settings anchors dead, File live for Quit, the caption passing). Each
// button grays only where its own act refuses under the picker — the
// openers of another list owner or an editor (redesign_button_enabled's
// color picker terms) — never wholesale. A roster lift's chord passes the
// picker's key layer as the pointer's own
// (GuiInputHandler::roster_chord_in_flight_); the card's preset menu and
// element list, drawn last, cover a row where they hang over it, and while
// either is down the press is theirs (the press router's veil). A PROMPT
// over the picker (the palette Delete question) owns the row as every
// prompt does. THIS SET LESS THE RENDER PLAYER IS THE ROW'S CHROME TENANTS
// (chrome_tenant_owns_bottom_row, app_state.h, 2026-10-10), which stand in
// THE DIALOG, the bottom overlay laid over the row (onscreen_keyboard.h's
// overlay block).
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
    // into ONE lane on the WINDOW'S FOOT; COOL EDIT'S ROW 8 UNDER ITS DOCK BAR
    // since 2026-10-09): the clock in its time field and THE STATE LINE on
    // the face beside it at the left, and — packed against the right edge —
    // the MARKER-VERB GROUP, the marker walk, the four cardinal arrows and the
    // transport three, each Cool Edit's group (the four tables above own
    // those memberships). That is the whole roster. This painter
    // owns the lane's CHROME (its ground) and nothing else; the
    // buttons and the clock are paint_bottom_row_buttons_and_clock, called
    // from here onto the grounded band (the cluster's tables and the clock's
    // metrics live beside that body).
    // THE ROW HAS A MODAL STATE since 2026-08-13, in which those tenants
    // stand down and the render player takes the lane, or the dialog overlay
    // laid over it carries the prompt or the dialog editor (2026-10-10; the
    // ruling and the fork are at modal_owns_bottom_row,
    // just above; the modal's own layout is paint_modal_dialog's) — and with
    // the chain gone there is nothing left on the lane to negotiate with,
    // which is what makes that yield clean.
    //
    // WHAT IS NOT HERE, and why it is not missing: an S/T · W/P view readout
    // and a "(read-only)" token. The icon row's view group shows the view as
    // a lit button and its read-only toggle shows the lock, so letters would
    // restate what that row says in its own vocabulary. THE A / B LETTER IS
    // HERE, though, since 2026-10-01: the tab row that showed it is deleted,
    // and the letter ends the clock cell, after the timestamp and the pipe
    // (paint_bottom_row_buttons_and_clock owns the rule). The row carries no dirty mark: Save's grey is the mark
    // (architect 2026-10-05, plain_save_actionable).
    //
    // The row paints on EVERY frame class (loading, blank, loaded) like the
    // redesigned rows above it: the clock reads 00:00.000 with no source, and
    // the buttons must be visible on every frame class their press claim is
    // live on.
    const GuiRect lane    = bottom_row_area(app);
    const GuiRect content = bottom_row_content_area(app);
    if (lane.w <= 0 || lane.h <= 0 || content.h <= 0) return;

    // THE LANE'S GROUND (architect 2026-10-09, the program is Cool Edit;
    // the frame 2026-10-10), ON EVERY FRAME WHATEVER STANDS OVER IT: COOL
    // EDIT'S DOCK BAR at its head (paint_ce_dock_bar; the column's foot
    // stands straight on its light row) and the content band under it in THE
    // PANEL'S FACE, the lane one frame line in from each side inside the
    // program's frame. The row's tenants are the program's (row 8, the render
    // player); a CHROME TENANT — a prompt, a dialog editor or the picker's
    // Cancel — is THE DIALOG, a bottom overlay laid over the content band and
    // the frame's bottom row with the dock bar left in view above it as Cool
    // Edit's ridge (onscreen_keyboard.h's overlay block, its ground and line
    // paint_bottom_overlays'), so this lane never changes for it. The ground
    // erases whatever render_background laid down, so the strip does not
    // depend on that erase happening to hold the same value. The render
    // player paints on this ground and lays none of its own
    // (paint_modal_dialog).
    {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        paint_ce_dock_bar(cr,
                          GuiRect{lane.x, lane.y, lane.w, content.y - lane.y});
        paint_cell_rect(cr, content, palette().face);
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
    // AND THE ROW YIELDS UNDER THE ON-SCREEN KEYBOARD (2026-10-10, ON SCREEN
    // IS AS PAINTED): the keyboard is a bottom overlay standing on the
    // window's foot, its band always taller than the row, so row 8 is under
    // it whole — the flag editor's case, the one editor that raises the
    // keyboard with no dialog — and a button nothing shows must not hover,
    // wear a tooltip or answer a press (the keyboard's claim takes the press
    // first; the zero rects take the rest).
    if (modal_owns_bottom_row(app) || onscreen_keyboard::stands(app, gui)) {
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
// THE DIALOG IS A BOTTOM OVERLAY SINCE 2026-10-10 (architect, Cool Edit's
// status bar: onscreen_keyboard.h's overlay block): a prompt's, an editor's
// and the picker's controls stand on the chrome's ground laid over row 8's
// content band and the frame's bottom row under one Hilight line
// (paint_bottom_overlays), centered in that ground, or on the on-screen
// keyboard with one pad between; the render player keeps the row's own face.
// CONTENT PER KIND, on that ground (there is no box, no frame and no
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
//   THE SETTINGS CHOICE EDITOR (2026-10-07 evening) is the one editor whose
//   field is a COMBO: its row's word as the label, the chrome's combo at the
//   field's seat, its list dropping upward (paint_settings_choice; the
//   design at GuiSettingsEditor's head).
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
//   kModalButtonGapPx 6 W — modal_popup.png's inter-button gap, 8 laptop px
//                          (Save ends x=504, Do Not Save begins x=513;
//                          identically 619..626): 8 device px at 138 %
//                          (6 · 1.38 = 8.28 → 8), 18 at 300 %.
//   kModalFieldHeightPx 23 W — editor.png's field, borders included
//                          (y=5..35, 31 laptop px), vertically centred in the
//                          row's content band: 32 device px at 138 %
//                          (23 · 1.38 = 31.74 → 32), 69 at 300 %.
//   kModalFieldPadXPx 5 W — its border-to-ink inset (x=86..92), 7 laptop px:
//                          7 device px at 138 % (5 · 1.38 = 6.9 → 7), 15 at
//                          300 %.
//   (the label-to-field gap is NOT a constant of its own: since 2026-10-08
//    it is kModalButtonGapPx, the editor row's ONE GAP — label → field,
//    field → OK, OK → Cancel; the rule at the editor arm. From 2026-08-29
//    it was the window-edge pad read twice, which with the label's trailing
//    space read wider than the field's other end.)
//   kModalFieldWidthPx 378 W — AUTHORED, not sampled (the crop's field width is
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
//   kModalFocusFramePx 1 W — the keyboard focus frame, RESERVED around every
//                          button and painted for the focused one (the
//                          reflow-free rule is at the constant).
// THE FIELD ABSORBS THE SHRINK on a narrow window exactly as it did in the
// box — it takes whatever is left between the label and the buttons, floored
// at 29 W (kModalFieldMinWidthPx) so it stays a field.
//
// THE PUBLICATION is the floating surfaces' own convention: this runs
// UNCONDITIONALLY from on_redraw's tail, rewrites AppState::modal_dialog and
// AppState::dialog_editor_text every run (zero/invalid with no dialog up), so
// the pointer path always reads what is actually on screen and a closed
// dialog strands nothing. `box` is the modal's surface — the dialog's whole
// overlay, its line and its ground (onscreen_keyboard::dialog_rect, since
// 2026-10-10 the dialog is a bottom overlay laid over the row), or the lane
// under the render player — and it is what the hover invalidation and the
// damage ride. Damage: the openers invalidate the whole window (no surface
// exists before the first paint); every later edit, blink, flash and closer
// rides invalidate_modal_dialog_area, which takes this row's lane and the
// dialog's overlay whole — the modal's surface, and the rect a closer owes
// (viewport.cpp).

namespace {

// IN WINDOWS PX since the unit's change (architect 2026-10-02): every number
// in this block re-authored from the laptop pixel to the device size it had
// on the tablet — the gap 8 as 6, the field 31 as 23, its pad 7 as 5, its
// width 520 as 378 and its 40-px floor as 29, the button box 32 as 23, its
// pads 9 and 10 as 7 and 7.
// THE GAP, 6 W, IS ALSO WINDOWS' OWN: 4 dialog units between related
// controls and between command buttons, at MS Sans Serif 8's 1.5-px
// horizontal unit — and since 2026-10-08 the editor row's ONE GAP (label →
// field → OK → Cancel; the rule at paint_modal_dialog's editor arm).
constexpr double kModalButtonGapPx    = 6.0;
// (THE FIELD'S HEIGHT AND PAD, kModalFieldHeightPx 23 and kModalFieldPadXPx
// 5, are render.h's since 2026-10-08: the color picker's hex field and the
// settings choice's combo take them too, so their one source is where every
// reader sees it; the time fields, which took them 2026-10-08 to 2026-10-09,
// are Cool Edit's dark field with program_spec.h's own height and pad.)
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
// THE BOX'S HEIGHT AND THE TWO LABEL PADS ARE THE CHROME SPEC'S SINCE
// 2026-10-07 (push_button_box_px, push_button_pad_left_px / _right_px):
// win2000 the 23 and 7 / 7 below. The minimum width is Windows' 75.
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
// player's seven are row 8's case, the program's (architect 2026-10-05,
// bottom_row_seats; render.h's program block).
constexpr double kModalBtnMinWidthPx  = 75.0;
// THE FOCUS FRAME'S WIDTH — the keyboard-focused button's frame, ONE
// DkShadow line OUTSIDE its raised box (architect 2026-10-02, the planner's
// reading of the Windows default-button frame; the face at the button walk
// below). RESERVED FOR EVERY BUTTON AND PAINTED FOR ONE, which is what makes
// moving the focus reflow nothing: the cluster's right anchor spends it, the
// row's one 6-W gap absorbs it on the cluster's left (a prompt's message, an
// editor's field — 2026-10-08) as between two buttons, and the buttons
// themselves never move.
// THE RENDER PLAYER'S RIGHT PAIR SPENDS NONE (architect 2026-10-05): it ends
// where row 8's last case ends (bottom_row_seats), so its frame paints in
// the face beyond it. One relief line (relief_line_px), so it fits at every
// scale by construction: the vertical margin is (32 − 23) / 2 Windows px
// (row 8's content round the spec's push button) against its 1,
// and the 6-px inter-button gap absorbs one frame from each neighbour.
constexpr double kModalFocusFramePx   = 1.0;
// THE PLAYER ROW'S GLYPH GAP — between two of the render player's glyph
// boxes, the player's own since the roster's buttons began to touch
// (architect 2026-10-02): 1 Windows px, the laptop pixel's 2-px button gap
// re-authored.
constexpr double kPlayerGlyphGapPx    = 1.0;
// THE PLAYER ROW'S GROUP SPACE — between its items (transport → scrub,
// scrub → clock, clock → the lamp): 8 Windows px, the roster's group gap it
// stood at from 2026-10-02 until the roster's groups became Cool Edit's
// (2026-10-09), kept as the player's own.
constexpr double kPlayerGroupGapPx    = 8.0;

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
    dlg.combo      = GuiRect{0, 0, 0, 0};
    dlg.combo_list = GuiRect{0, 0, 0, 0};
    dlg.combo_list_items.clear();
    dlg.combo_list_bar = PopupScrollBar{};
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
    // THE COLOR PICKER, the fifth owner (2026-10-07): the same rank as the
    // two list owners, none of the three ever live with another, and its
    // own one field is no dialog editor — so it forks here, before the
    // editor fork, and paints ITS CARD ON THE WELL through
    // paint_color_picker below, which publishes this stash (the card as the
    // box, its field as the field, Copy / Paste / Close as the buttons)
    // and the picker's own. THIS IS ONE OF THE THREE PLACES THE RANKING IS
    // SPELLED (the other two are named above).
    const bool color_picker_up =
        !prompt_up && !player_up && !picker_up && app.color_picker.active;
    std::string prefix;
    text_editor::State* ed =
        (prompt_up || player_up || picker_up || color_picker_up)
            ? nullptr : dialog_editor_to_paint(app, prefix);
    if (!prompt_up && !player_up && !picker_up && !color_picker_up &&
        ed == nullptr) {
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

    // THE COLOR PICKER'S FORK: its surface is the card on the well, not the
    // row, so the row's layout below is not its business — the row stays
    // painted under it as without it (modal_owns_bottom_row's asymmetry),
    // and the card, painted after the row, covers it only where its preset
    // menu or chooser-row list hangs over it.
    if (color_picker_up) {
        paint_color_picker(cr, live_session, /*veiled=*/false);
        return;
    }
    // AND UNDER ITS DELETE QUESTION (2026-10-07): the prompt owns the row and
    // the stash below, and the card still stands on the well, VEILED —
    // painted, publishing nothing (paint_color_picker's head) — so the
    // question is asked over the picker it is about.
    if (prompt_up && app.color_picker.active)
        paint_color_picker(cr, 0, /*veiled=*/true);

    // THE MODAL'S SURFACE: the RENDER PLAYER'S is the bottom row's content
    // band — the program's tenant, on the row's own face, which
    // paint_bottom_strip lays on every frame class, and a prompt raised over
    // the player stands there with it — and every chrome tenant's
    // (onscreen_keyboard::dialog_stands) is THE DIALOG, the bottom overlay
    // laid over the row (architect
    // 2026-10-10; onscreen_keyboard.h's overlay block): its ground and line
    // are paint_bottom_overlays', already painted, so nothing here draws a
    // ground of its own. The dialog's controls are laid out in its BAND
    // (dialog_band_rect: the ground's top, the alone band's height) — alone
    // the ground under the line down to the window's foot, on the keyboard
    // the same top pad with the keyboard's edge pad as the gap below — and
    // keep the row's own x and width, so they stand where the row's tenants'
    // margins stand.
    const GuiRect lane        = bottom_row_area(app);
    const GuiRect row_band    = bottom_row_content_area(app);
    const bool    dialog      = onscreen_keyboard::dialog_stands(app);
    const bool    on_keyboard = onscreen_keyboard::stands(app, gui);
    const GuiRect dialog_band =
        onscreen_keyboard::dialog_band_rect(app, on_keyboard);
    const GuiRect content = dialog
        ? GuiRect{row_band.x, dialog_band.y, row_band.w, dialog_band.h}
        : row_band;
    if (lane.w <= 0 || lane.h <= 0 || content.h <= 0) {
        // A degenerate row publishes nothing, the cold answer the pointer path
        // already reads correctly (a zero stash contains no point) — and it
        // names no buttons, so the face state goes with it.
        reset_modal_dialog_face_state(app);
        return;
    }

    cairo_save(cr);
    const GuiFont font = gui_font(GuiFace::Body);

    const int bgap  = scaled_px(kModalButtonGapPx);
    // The deleted toolbar row's button box, owned by the dialog since the
    // 2026-08-12 relayout (the spec's push_button_box_px — the record at the
    // kModal* block) — on every
    // owner's row but THE PLAYER'S, whose buttons are row 8's case, the
    // program's, at row 8's seat (architect 2026-10-05, bottom_row_seats: the two rows
    // are one row). `btn_y` and `btn_h` are the one band the walk, the scrub
    // and the editor's label all read.
    const BottomRowSeats seats = bottom_row_seats(content);
    const ChromeSpec& spec = live_chrome_spec();
    const int btn_h = player_up ? seats.case_h
                                : scaled_px(spec.push_button_box_px);
    const int btn_pad_l = scaled_px(spec.push_button_pad_left_px);
    const int btn_pad_r = scaled_px(spec.push_button_pad_right_px);
    // The focus frame's reserved band (kModalFocusFramePx, above): spent by
    // the layout for EVERY button whether or not one is focused, so the frame
    // can never push the row around when the focus moves. One relief line.
    const int ring = scaled_px(kModalFocusFramePx, 1);

    // -- The buttons' words and widths, shaped up front (the layout needs the
    //    row's total before anything can be placed). --
    // A BUTTON IS A WORD OR A GLYPH (2026-08-28, the render player's row):
    // `glyph` set means the button is the roster's own icon in a
    // push-button-box SQUARE — the THREE transport buttons wear
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
        // the LOAD wears DialogOkApply — the very glyph the icon row's
        // button wears for the same act one surface over (icons.h names
        // each set's drawing) — and CLOSE wears
        // WindowClose, Marlett's close X (the
        // notification cards' dismiss was its other reader until the card's X
        // retired, 2026-10-01; a def is a glyph and several buttons are free
        // to wear one). The row is SEVEN GLYPH
        // BUTTONS and no word button, so its faces take the glyph fork whole.
        // Close stays LAST, the escape sentinel by construction, and both keep
        // their acts, their keys and their hints unchanged.
        // AT THE ROOT THE SLOT IS DELETE (architect 2026-09-29): `tmp/`'s own
        // listing is batch folders, where a load has nothing to name, so the
        // sixth button deletes — EditDelete — the
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
            // A glyph button is the PROGRAM'S CASE (the player's alone, its
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
    // message and the buttons in painted order, an EDITOR its label, the
    // field and the buttons, the button gap between each (the one-gap rule
    // at the editor arm, 2026-10-08; the prompt's at its arm, the same day).
    // The right-aligned cluster
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
    // the window edge. Between neighbours the 6-W gap absorbs both
    // (kModalFocusFramePx), and since 2026-10-08 between an editor's field
    // and its OK and between a prompt's message and its first button too
    // (the two arms). (The player's row is row 8's and
    // reserves no ring at its right pad — its branch below.) So the focus can
    // move anywhere on the row without reflowing it, which is the whole point
    // of reserving.
    const int cx0 = seats.left_x;                       // content left
    const int cx1 = seats.right_x;                      // the row's right pad
    const int buttons_x_max = std::max(cx0, cx1 - ring - buttons_w);
    // Centred in the content band on every owner's row but the player's,
    // which takes row 8's case seat (above).
    // EVERY CONTROL ON A DIALOG ROW IS CENTRED ON THE ROW'S VERTICAL CENTRE
    // (architect 2026-10-08, on the editor row: "they don't look quite
    // aligned vertically") — the push buttons here, an editor's field and
    // the choice editor's combo at the editor arm below — through this one
    // expression, so two controls of one height share their rows exactly
    // and a shorter one stands centred on the taller (where two heights
    // differ by an odd count of device px the floor leaves the odd row
    // below, the same for every control). UNDER
    // WIN2000 THE FIELD AND THE BUTTON ARE BOTH 23 W (Windows' 14 dialog
    // units, kModalBtnMinWidthPx's block and kModalFieldHeightPx) and their
    // boxes coincide row for row; the button only READS 1 W shorter at the
    // top (and narrower at the left) because a raised push button's outer
    // top-left line is 3DLight, which the Windows Standard scheme sets equal
    // to the face (render.h's palette block), while the sunken field's outer
    // top-left line is Shadow — Windows 2000's own push button beside its
    // own edit box, measured on his tablet capture (2026-10-08, 300 %: the
    // field's box y 1351–1419, OK's visible edge from 1354, its 3DLight
    // rows 1351–1353 on the ground).
    const auto row_centred_y = [&](int h) {
        return content.y + (content.h - h) / 2;
    };
    const int btn_y = player_up ? seats.case_y : row_centred_y(btn_h);
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
        // THE MESSAGE SETS WHERE THE BUTTONS START — the row's ONE GAP past
        // its ink — up to the cap, which is where an over-long message stops
        // pushing and starts CLIPPING instead.
        // THE PROMPT ROW HAS THE EDITOR ROW'S ONE GAP (architect 2026-10-08):
        // the message → first button air is `bgap`, kModalButtonGapPx's 6 W,
        // the same 6 W that stands between two of its buttons and on every
        // gap of the editor row (the rule at the editor arm) — Windows' 4
        // dialog units. The focus frame's one line spends itself inside the
        // gap, as it does between two buttons (kModalFocusFramePx). Until
        // this day it was the row's pad plus the reserved ring, 9 W.
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
        buttons_x0 = std::min(cx0 + msg_w + bgap, buttons_x_max);
        const int msg_clip = std::max(0, (buttons_x0 - bgap) - cx0);
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
        //    SEPARATORS RETIRED (architect 2026-10-02; Sound Recorder and
        //    mplay32 have none, and the roster's etched separators of
        //    2026-10-06 did not reach this row): the groups stand eight
        //    Windows px of bare face apart (kPlayerGroupGapPx) — transport →
        //    scrub, scrub → clock, clock → the lamp. THE CLOCK IS TWO TIME FIELDS since
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
        //    except the scrub. THE BOXES ARE ROW 8'S CASES (architect
        //    2026-10-05, bottom_row_seats; the program's since 2026-10-09):
        //    each box wears the roster's face (paint_roster_case; the walk
        //    below) on the program's 23-W case at row 8's seat, the roster's
        //    glyph at its (+1, +1) lines, the row on the panel's face (the
        //    player is the program's tenant of the row, paint_bottom_strip).
        //    Sound Recorder stays the model for the scrub's channel, the
        //    scrub's thumb Windows' trackbar thumb (render.h's scrub block,
        //    each chrome's own); the two time fields are Cool Edit's dark
        //    fields, every time field's shape.
        //
        //    THE ROW IS ROW 8'S (architect 2026-10-05): the transport starts
        //    at row 8's left pad, the right-flushed pair ends where row 8's
        //    last case ends (no focus-frame reserve inside it), the buttons
        //    stand at row 8's case seat, and the scrub fills whatever lies
        //    between.
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
        // GROUP SPACE between its items is kPlayerGroupGapPx, its own since
        // the roster's groups became Cool Edit's (2026-10-09).
        const int ggap     = scaled_px(kPlayerGlyphGapPx, 1);
        const int group    = scaled_px(kPlayerGroupGapPx);

        // THE CLOCK'S TWO FIELDS ARE MEASURED BEFORE ANYTHING IS PLACED,
        // their width being one of the fixed terms the scrub's own is what is
        // left over. Each is a TIME FIELD (the shape, the face and the
        // fixed-width rule at kTimeShape's block), its cell the widest-digit
        // specimen alone (time_field_metrics, row 8's own memo), so neither
        // field nor anything right of it moves as the position runs or an
        // item of another length loads — the cell and its pads, as row 8's
        // clock is.
        const GuiFont cfont = gui_font(GuiFace::Body);
        const double cell_w =
            time_field_metrics(cfont).time_w;
        const int field_pad = time_field_pad_px();
        const int field_w   = time_field_w_px(cell_w);
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
        // the GLYPH GAP between them — right-flushed where ROW 8'S LAST CASE
        // ENDS, the last box's right edge row 8's last button's (architect
        // 2026-10-05; bottom_row_seats' cases_right since row 8's groups
        // became Cool Edit's, 2026-10-09; the other owners' clusters keep
        // the reserved ring).
        const int words_w  = bw + ggap + bw;
        const int words_x0 = std::max(cx0, seats.cases_right - words_w);
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

        // -- THE PLAY-SCRUB, SOUND RECORDER'S CHANNEL UNDER WINDOWS'
        //    TRACKBAR THUMB (architect 2026-10-02, the thumb 2026-10-06; the
        //    look and its owners at render.h's scrub block). The ITEM is
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
            // THE THUMB'S COLUMN, when a thumb stands: the drag's carried x
            // while the thumb is being dragged (the sound continues where it
            // was and the seek commits at the release), the item position's
            // own otherwise.
            const int hx = rp.frames <= 0 ? -1
                         : rp.scrub.armed ? rp.scrub.marker_x
                                          : render_player_scrub_x_of(app, pos);
            // THE CHANNEL: PLAIN SUNKEN on a rect four lines tall, so the
            // edge's two rings are the whole of it — Shadow, DkShadow,
            // 3DLight, Hilight top to bottom — across the track's width,
            // nothing inside and nothing filled.
            paint_relief_plain_sunken(
                cr, GuiRect{track.x, thumb_y + above, track.w, channel_h});
            if (rp.frames > 0) {
                // THE THUMB — Windows' pointed trackbar thumb (render.h's
                // scrub block), centred on the column, its point down toward
                // the channel's bottom. No hover face: the grab band
                // (scrub_handle_box_px) is the press's business and the
                // cursor its cue.
                const int thumb_w = scrub_thumb_w_px();
                const GuiRect thumb{hx - thumb_w / 2, thumb_y, thumb_w,
                                    thumb_h};
                paint_scrub_thumb(cr, thumb);
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
        //    centred on the case's rows with its run on the field's own
        //    seat, Cool Edit's dark field and its ink (paint_ce_time_field,
        //    kCeFieldText) — the fields row 8's clock is, said twice. ONE RECT ROUND BOTH IS PUBLISHED (dlg.clock) for the
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
                const int field_x = clock_x0 + i * (field_w + field_gap);
                const GuiRect field = time_field_rect(
                    field_x, field_w, seats.case_y, seats.case_h);
                paint_ce_time_field(cr, field);
                // RIGHT-ALIGNED, ending at the field's right pad (the rule at
                // kTimeShape's block).
                const text_shape::ShapedRun run = text_shape::shape_text_run(
                    cfont, format_timestamp(seconds_of(shown[i])));
                set_palette_source(cr, hex(kCeFieldText));
                text_shape::show_shaped_run(
                    cr, run,
                    static_cast<double>(field_x + field_w - field_pad) -
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
        // THE EDITOR ROW HAS ONE GAP (architect 2026-10-08: "make sure the
        // spacing from the word Setting: to the input box is even — it looks
        // like there's more space there"): the label → field, the field →
        // OK and the OK → Cancel air are ALL `bgap`, kModalButtonGapPx's 6 W
        // — Windows' 4 dialog units between related controls and between
        // command buttons at MS Sans Serif 8's 1.5-px horizontal unit (the
        // Windows layout guidelines; their 3-DLU label-to-control figure,
        // 4.5 px, is not taken, the ruling being one gap). The label is
        // measured WITHOUT A TRAILING SPACE (every prefix is `<word>:`,
        // paint_handler.h), so the air after the colon is this gap alone —
        // until this day it was the row's pad PLUS the prefix's space, 17 W
        // of air at the label against 10 W at the field's other end. The
        // field → OK gap carries no reserved ring: the focus frame's one line
        // spends itself inside the gap, as it does between two buttons
        // (kModalFocusFramePx). The choice editor alike — its combo takes
        // the field's seat. UNDER WIN2000 THE BUTTONS' AIR
        // READS 1 W WIDER than the label's: a raised push button's outer
        // top-left line is 3DLight, which the Windows Standard scheme sets
        // equal to the face, so its left edge shows 1 W inside its box (the
        // same reading as the vertical one at btn_y, below).
        const int fx    = cx0 + label_w + bgap;
        // The room a field may take before the buttons would have to give:
        // the cluster's cap, less the gap.
        const int field_room = (buttons_x_max - bgap) - fx;
        const int field_w = std::max(std::min(scaled_px(kModalFieldWidthPx),
                                              field_room),
                                     scaled_px(kModalFieldMinWidthPx, 1));
        // The width floor is the ONE thing that can push past the cap (a window
        // too narrow for label + field + buttons), and the cap is what stops
        // it there — the buttons stay whole and the field is the surface that
        // has already given everything it can.
        buttons_x0 = std::min(fx + field_w + bgap, buttons_x_max);
        const int field_h = scaled_px(kModalFieldHeightPx);
        const int field_y = row_centred_y(field_h);
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

        // THE CHOICE EDITOR'S COMBO STANDS WHERE THE FIELD STANDS
        // (2026-10-07 evening; GuiSettingsEditor's head): field_outer's seat
        // and size — THE FIELD'S 23 W, not the picker chooser's 21, the
        // field's cell being the row's — and no text field, no caret, no
        // click-to-caret publication (dialog_editor_text stays invalid, so
        // the field claim and the I-beam find nothing).
        if (app.settings_choice_live()) {
            paint_settings_choice(cr, font, field_outer);
        } else {
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
            // takes the same rule (render_flag_editor_box). Its text,
            // selection and caret take the field pair and the selected pair.
            const bool field_focused = app.modal_dialog_focus < 0;
            const GuiColor ink_text     = palette().field_text;
            const GuiColor ink_sel_fill = palette().selected_fill;
            const GuiColor ink_sel_text = palette().selected_text;
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
                set_palette_source(cr, ink_sel_fill);
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
                set_palette_source(cr, ink_text);
                text_shape::show_shaped_run(cr, run, tx, baseline);
                cairo_restore(cr);
                cairo_save(cr);
                cairo_rectangle(cr, hx0, band_y, hw, band_h);
                cairo_clip(cr);
                set_palette_source(cr, ink_sel_text);
                text_shape::show_shaped_run(cr, run, tx, baseline);
                cairo_restore(cr);
            } else {
                set_palette_source(cr, ink_text);
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
                set_palette_source(cr, ink_text);
                cairo_rectangle(cr, static_cast<int>(std::nearbyint(caret_x)),
                                band_y, caret_px, band_h);
                cairo_fill(cr);
                cairo_restore(cr);
            }
            cairo_restore(cr);   // the field clip

            dlg.field = field_inner;
        }
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
        // HOVER FACE but on the player's row (below): the pointer walk
        // (update_modal_dialog_hover) drives the tooltip and that row's hot
        // button.
        // THE RENDER PLAYER'S ROW IS ROW 8'S CASES (architect 2026-10-05;
        // the program's since 2026-10-09): its seven boxes take the roster's
        // face through the one painter, paint_roster_case — the rest face,
        // Cool Edit's down face pressed (the glyph one line right and down)
        // and for Repeat One's lamp, NO HOT FACE (program_spec.h's
        // case_hot_face) — and no separators. The focus frame stays the
        // keyboard's cue round the outside.
        // THE DISABLED RUNG, on the PLAYER's buttons alone (architect
        // 2026-08-30) — every other owner's buttons are always live while
        // their dialog stands and publish enabled=true, and the player's are
        // all glyph buttons: the glyph's disabled face (icons::draw_disabled,
        // ReactOS's saturate), the case the face its state gives, the
        // roster's rule (architect 2026-10-02 and 2026-10-09). The press face is gated on
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
        const ButtonBoxFace box =
            player_up
                ? paint_roster_case(cr, r, plan[i].lit, pressed)
                : paint_button_box(cr, r, plan[i].lit, pressed,
                                   ButtonFamily::Push);
        if (plan[i].glyph) {
            // THE GLYPH, the band's own draw: the roster's glyph at the
            // case's (+1, +1) lines — the box is that case
            // (bottom_row_seats), so each glyph stands where row 8's does
            // (architect 2026-10-05) — in the drawing's own colours, or its
            // disabled face when the button is disabled; draw_cased takes the
            // case's corner (r.x, r.y), not a glyph origin.
            const int glyph_px  = icon_glyph_px();
            if (enabled)
                icons::draw_cased(cr, plan[i].icon, r.x, r.y,
                                  static_cast<double>(glyph_px), box.shift);
            else
                icons::draw_cased_disabled(cr, plan[i].icon, r.x, r.y,
                                           static_cast<double>(glyph_px),
                                           box.shift);
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

    // THE MODAL'S SURFACE IS THE BOX: the dialog's its whole overlay, the
    // line and the ground (onscreen_keyboard::dialog_rect, 2026-10-10), and
    // the player's (and a prompt's over it) the lane, its top row included —
    // because that is the rectangle the modal owns and the rectangle its
    // damage must erase.
    // THE SESSION IS STAMPED HERE, one write for both branches at the moment
    // the geometry becomes readable: it is the id of the surface these rects
    // belong to, and every input site that reads them compares it against the
    // live one (modal_dialog_stash_current, input_pointer.cpp).
    dlg.box     = dialog ? onscreen_keyboard::dialog_rect(app, on_keyboard)
                         : lane;
    dlg.session = live_session;
    dlg.valid   = true;
    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_color_picker (architect 2026-10-07) ---------------
//
// THE COLOR PICKER'S CARD ON THE WELL — the anatomy, every length and the
// seat are at color_picker.h's head; the layout is color_picker::layout,
// read here and by the press router alike. This body PAINTS and PUBLISHES:
// the modal stash (the card as `box`, the one field's interior as `field`,
// the three push buttons as `buttons`, so the dialogs' shared machinery —
// the hover walk, the press arm, the lift's dispatch, the tooltip wait, the
// pressed and disabled faces — serves them unchanged) and the picker's own
// (AppState::ColorPicker::Stash: the scope combo and the element chooser,
// the chooser row's list, the sliders' tracks
// and thumb columns, the field's run, the swatches, the wheel's circles,
// the preset button and its menu's rows with their enabled bits). AS
// PAINTED: everything is republished every run from the same layout the
// pixels came from.
//
// VEILED (architect 2026-10-07, the preset menu's Delete question): under a
// prompt the card still paints, so the question stands over the picker it
// asks about, and publishes NOTHING — the modal stash is the prompt's, the
// picker's stash goes invalid (a press under the prompt is the prompt's),
// and no button wears a pressed face (the dialogs' indices name the
// prompt's buttons then).
//
// THE LOOK: the GROUND with the PLAIN RAISED two-line edge; the wheel
// (color_picker::paint_wheel, the one non-role painter); the scope combo,
// the element chooser and the preset button — ONE COMBO DRAWING
// (paint_picker_combo below): a sunken field in the field pair with
// Windows' 16-W drop-down button and Marlett's wedge, the text at the
// field's pad cut with "..." where it does not fit; the six slider rows —
// the label cap-centered in `label`, the slider in the scrub's painters
// (the channel's four lines and the pointed thumb on a 4 / 4 / 9 seat
// filling the row), the value's tabular digits right-aligned in `label`;
// the one field — the dialog field's size (kModalFieldHeightPx, render.h)
// in the field pair inside one sunken-outer line, its run, selection and
// caret the dialog field's own painting and its minimal-travel scroll
// (text_editor::State::view_offset_px), and, under the name ask, the act's
// word left of it in `label`; the swatch pair in a one-line sunken frame,
// each swatch a flat fill of its word; the three push buttons
// the dialogs' (the disabled face where their bits say); and the chooser's
// list or the preset menu, when down, the menu-row popup's box and rows,
// the menu's separator and grayed rows the dropdown's.
namespace {
// THE COMBO'S DOWN WEDGE at (ax, ay), aw x ah: the ink when live, and when
// grayed THE DISABLED EMBOSS's two copies (show_embossed_run's inks) —
// Windows' inactive scroll-arrow glyph.
void paint_picker_wedge(cairo_t* cr, double ax, double ay, int aw, int ah,
                        bool enabled, GuiColor ink) {
    const auto wedge = [&](double x, double y, GuiColor c) {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
        cairo_new_path(cr);
        cairo_move_to(cr, x, y);
        cairo_line_to(cr, x + aw, y);
        cairo_line_to(cr, x + aw / 2.0, y + ah);
        cairo_close_path(cr);
        set_palette_source(cr, c);
        cairo_fill(cr);
        cairo_restore(cr);
    };
    if (enabled) {
        wedge(ax, ay, ink);
        return;
    }
    const double off = static_cast<double>(relief_line_px());
    wedge(ax + off, ay + off, palette().hilight);
    wedge(ax, ay, palette().shadow);
}

// THE COMBO'S TEXT in `room` from (x, baseline): whole when it fits, else
// CUT AT A CODEPOINT with Windows' "..." after the longest prefix that fits
// with it — the caption title's rule (paint_caption_row) — and nothing when
// even the "..." does not fit. Live in `ink`, grayed in the emboss.
void show_picker_combo_text(cairo_t* cr, const GuiFont& font, double x,
                            double baseline, double room,
                            std::string_view text, bool enabled,
                            GuiColor ink) {
    const auto show = [&](const text_shape::ShapedRun& r, double rx) {
        if (enabled) {
            set_palette_source(cr, ink);
            text_shape::show_shaped_run(cr, r, rx, baseline);
        } else {
            show_embossed_run(cr, r, rx, baseline);
        }
    };
    text_shape::ShapedRun run = text_shape::shape_text_run(font, text);
    if (run.width_px <= room) {
        show(run, x);
        return;
    }
    const text_shape::ShapedRun ellipsis = text_shape::shape_text_run(font, "...");
    if (ellipsis.width_px > room) return;
    size_t cut = text.size();
    while (cut > 0) {
        do { --cut; } while (cut > 0 &&
                             (static_cast<unsigned char>(text[cut]) & 0xC0) ==
                                 0x80);
        run = text_shape::shape_text_run(font, text.substr(0, cut));
        if (run.width_px + ellipsis.width_px <= room) break;
    }
    show(run, x);
    show(ellipsis, x + run.width_px);
}
} // namespace

// THE CHROME'S COMBO — the chooser's drawing, the preset button's too
// (2026-10-07, the planner's "reuse it"), the scope combo's (2026-10-08),
// and since 2026-10-07 evening the
// settings editor's CHOICE COMBO's, a reader outside the picker
// (paint_settings_choice): `r` the whole control, `button`
// the drop-down button inside it, `open` while its list or menu is down
// (the button pushed), `enabled` false for the grayed face (the field takes
// the ground, as a disabled Windows combo's edit does; the text and the
// wedge the emboss).
static void paint_picker_combo(cairo_t* cr, const GuiFont& font,
                               const GuiRect& r, const GuiRect& button,
                               std::string_view text, bool open,
                               bool enabled) {
    const int lw = relief_line_px();
    const int aw_gap = scaled_px(color_picker::kComboTextGapPx);
    const int fb = 2 * lw;
    paint_cell_rect(cr, GuiRect{r.x + fb, r.y + fb, r.w - 2 * fb, r.h - 2 * fb},
                    enabled ? palette().field_ground : palette().ground);
    paint_relief_plain_sunken(cr, r);
    cairo_save(cr);
    cairo_rectangle(cr, r.x + fb, r.y + fb, button.x - (r.x + fb), r.h - 2 * fb);
    cairo_clip(cr);
    const double tx = r.x + color_picker::combo_text_inset_px();
    show_picker_combo_text(cr, font, tx, redesign_baseline(font, r.y, r.h),
                           button.x - aw_gap - tx, text, enabled,
                           palette().field_text);
    cairo_restore(cr);
    // THE DROP-DOWN BUTTON: Windows' scroll-arrow button, pushed while the
    // list is down, its wedge Marlett's 7 x 4.
    const ButtonBoxFace box = paint_button_box(
        cr, button, /*lamp=*/false, /*pressed=*/open && enabled,
        ButtonFamily::Push);
    const int aw = scaled_px(color_picker::kComboArrowWPx,
                             color_picker::kComboArrowMinWPx);
    const int ah = scaled_px(color_picker::kComboArrowHPx,
                             color_picker::kComboArrowMinHPx);
    const double ax = button.x + (button.w - aw) / 2 + box.shift;
    const double ay = button.y + (button.h - ah) / 2 + box.shift;
    paint_picker_wedge(cr, ax, ay, aw, ah, enabled, palette().label);
}

// THE COMBO'S LIST — ONE PAINTER, TWO READERS (2026-10-07 evening, lifted
// from the picker's chooser for the settings editor's choice editor, its
// second reader): the dropdown's box (paint_dropdown's composition, the
// popup's Menu face) at the placed box, then the SHOWN rows at
// combo_list_item (color_picker.h; the popup lists' scroll, render.h's popup
// scroll block), each LIT — the selected pair — when it is the pressed row
// or the hovered one; then the scroll bar when it stands. Every row is live (no
// grayed row: both lists' domains are wholly choosable). ITS LABELS STAND AT
// THE DROP-DOWN INSET (color_picker::combo_text_inset_px, the one padding
// rule of every drop-down under every chrome, 2026-10-09), each row's name
// under the face's.
void GuiPaintHandler::paint_combo_list(
        cairo_t* cr, const GuiFont& font, const color_picker::ComboList& list,
        int pressed, int hover, PopupScrollPart held,
        const char* (*label_of)(const AppState&, int)) {
    paint_popup_chrome(cr, list.box, PopupFace::Menu);
    const int pad_l = color_picker::combo_text_inset_px();
    for (int i = 0; i < list.count; ++i) {
        const GuiRect item = color_picker::combo_list_item(list, i);
        if (item.w <= 0 || item.h <= 0) continue;   // scrolled out of view
        const bool lit = pressed == i || hover == i;
        if (lit) paint_cell_rect(cr, item, palette().selected_fill);
        show_row_text(cr, font, list.box.x + pad_l,
                      redesign_baseline(font, item.y, item.h),
                      label_of(app, i),
                      lit ? palette().selected_text : palette().label);
    }
    paint_popup_scroll_bar(cr, list.bar, held);
}

// THE SETTINGS EDITOR'S CHOICE COMBO (the design at GuiSettingsEditor's
// head, settings_editor.h): THE PICKER'S COMBO DRAWING, paint_picker_combo
// read from outside the picker, at the dialog field's seat and size — the field's
// 23-W cell, not the picker's 21: the combo stands where the field stands,
// in the row's cell (paint_modal_dialog's caller says so) — showing the
// shown choice, its button pushed while the list is down. No hover face on
// the combo (the picker's chooser takes none). The list, when down, is
// paint_combo_list's at combo_list — standing on the combo's head, the row
// being the window's foot, and scrolling by the popup lists' rule — the
// shown row seeded lit at the open and the lit row following the pointer
// over the rows and holding off them (the chooser's rule,
// settings_choice_motion). The combo, the list, its rows (the zero rect for a row
// scrolled out of view) and its bar publish into the modal stash, the press
// road's only geometry.
void GuiPaintHandler::paint_settings_choice(cairo_t* cr, const GuiFont& font,
                                            const GuiRect& combo) {
    const AppState::SettingsChoice& ch = app.settings_choice;
    AppState::ModalDialogGeometry& dlg = app.modal_dialog;
    paint_picker_combo(cr, font, combo, color_picker::combo_drop_button(combo),
                       app.settings_choice_label(ch.shown), ch.list_open,
                       /*enabled=*/true);
    dlg.combo = combo;
    if (!ch.list_open) return;
    const int n = app.settings_choice_count();
    const color_picker::ComboList list =
        color_picker::combo_list(combo, n, app.height, ch.list_scroll.top);
    paint_combo_list(cr, font, list, ch.list_pressed, ch.list_hover,
                     ch.list_scroll.held,
                     [](const AppState& a, int i) {
                         return a.settings_choice_label(i);
                     });
    dlg.combo_list = list.box;
    dlg.combo_list_bar = list.bar;
    dlg.combo_list_items.clear();
    for (int i = 0; i < n; ++i)
        dlg.combo_list_items.push_back(color_picker::combo_list_item(list, i));
}

void GuiPaintHandler::paint_color_picker(cairo_t* cr, uint64_t live_session,
                                         bool veiled) {
    using color_picker::Channel;
    AppState::ColorPicker&          cp  = app.color_picker;
    AppState::ModalDialogGeometry&  dlg = app.modal_dialog;
    const GuiFont font = gui_font(GuiFace::Body);
    const color_picker::Layout L = color_picker::layout(app, font);
    const bool asking = text_editor::is_active(cp.field_editor) &&
                        cp.field_editor.kind == text_editor::Kind::PaletteName;
    if (!veiled) dlg.owner = AppState::ModalDialogOwner::ColorPicker;

    cairo_save(cr);

    // THE CARD.
    paint_cell_rect(cr, L.card, palette().ground);
    paint_relief_plain_raised(cr, L.card);

    // THE WHEEL.
    {
        const uint32_t r = (cp.rgb >> 16) & 0xFF, g = (cp.rgb >> 8) & 0xFF,
                       b = cp.rgb & 0xFF;
        const double v = std::max({r, g, b}) / 255.0;
        color_picker::paint_wheel(cr, L, cp.hue_deg, cp.sat, v, cp.rgb);
    }

    // THE CHOOSER ROW: the scope combo, then the element chooser, each
    // pushed while its own list is down (the row's one list,
    // `chooser_scope` saying whose).
    paint_picker_combo(cr, font, L.scope, L.scope_button,
                       color_picker::scope_display_name(cp.scope),
                       cp.chooser_open && cp.chooser_scope, /*enabled=*/true);
    paint_picker_combo(cr, font, L.chooser, L.chooser_button,
                       color_picker::element_display_name(cp.element),
                       cp.chooser_open && !cp.chooser_scope, /*enabled=*/true);

    // THE SIX SLIDERS.
    for (int i = 0; i < color_picker::kChannelCount; ++i) {
        const Channel c = color_picker::channel_at(i);
        const GuiRect& row   = L.slider_row[i];
        const GuiRect& track = L.slider_track[i];
        const int max   = color_picker::channel_max(c);
        const int value = color_picker::channel_value(cp, c);
        show_row_text(cr, font, L.slider_label[i].x,
                      redesign_baseline(font, row.y, row.h),
                      color_picker::channel_label(c), palette().label);
        const int thumb_x = color_picker::slider_thumb_x(track, value, max);
        {
            const int above = scaled_px(color_picker::kSliderThumbAbovePx, 1);
            const int channel_h = scrub_channel_h_px();
            paint_relief_plain_sunken(
                cr, GuiRect{track.x, row.y + above, track.w, channel_h});
            const int tw = scrub_thumb_w_px();
            paint_scrub_thumb(cr, GuiRect{thumb_x - tw / 2, row.y, tw, row.h});
        }
        {
            const std::string digits = std::to_string(value);
            const text_shape::ShapedRun run = text_shape::shape_text_run(font, digits);
            set_palette_source(cr, palette().label);
            text_shape::show_shaped_run(
                cr, run,
                static_cast<double>(L.slider_value[i].x + L.slider_value[i].w) -
                    run.width_px,
                redesign_baseline(font, row.y, row.h));
        }
        cp.stash.sliders[static_cast<size_t>(i)] =
            AppState::ColorPicker::SliderStash{row, track, thumb_x};
    }

    // THE NAME ASK'S WORD, cap-centered on the button band at the row's
    // left (color_picker.h's THE NAME ASK).
    if (asking) {
        show_row_text(cr, font, L.name_label.x,
                      redesign_baseline(font, L.name_label.y, L.name_label.h),
                      color_picker::preset_act_label(
                          cp.name_ask == AppState::ColorPicker::NameAsk::Rename
                              ? color_picker::PresetAct::Rename
                              : color_picker::PresetAct::SaveAs),
                      palette().label);
    }

    // THE ONE FIELD — the hex field, or the name ask's wide field: the
    // field pair inside Windows' one sunken-outer line.
    {
        text_editor::State& ed = cp.field_editor;
        const bool editing = text_editor::is_active(ed);
        paint_cell_rect(cr, L.field, palette().field_ground);
        paint_relief_sunken_outer(cr, L.field);
        const std::string shown =
            editing ? ed.pending : color_picker::hex_spelling(cp.rgb);
        const text_shape::ShapedRun run = text_shape::shape_text_run(font, shown);
        const std::vector<double> bx =
            text_shape::byte_offsets_px(run, shown.size());
        const GuiColor ink_text     = palette().field_text;
        const GuiColor ink_sel_fill = palette().selected_fill;
        const GuiColor ink_sel_text = palette().selected_text;
        // THE VIEW: the field's two pads in, scrolled by THE MINIMAL-TRAVEL
        // RULE (text_editor::State::view_offset_px, the dialog field's four
        // lines) — the hex spelling always fits, so only a long name ever
        // travels.
        const double pad_x    = scaled_px(kModalFieldPadXPx);
        const int    caret_px = scaled_px(1.0, 1);
        const double view_x0  = L.field.x + pad_x;
        const double view_w   = std::max(1.0, L.field.w - 2.0 * pad_x);
        double vo = 0.0;
        if (editing) {
            const int cursor_pos =
                std::clamp(ed.cursor_pos, 0, static_cast<int>(shown.size()));
            const double caret_off = bx[static_cast<size_t>(cursor_pos)];
            const double travel_w  = view_w - static_cast<double>(caret_px);
            vo = ed.view_offset_px;
            if (caret_off - vo < 0.0)      vo = caret_off;
            if (caret_off - vo > travel_w) vo = caret_off - travel_w;
            const double max_vo =
                run.width_px + static_cast<double>(caret_px) - view_w;
            if (vo > max_vo) vo = max_vo;
            if (vo < 0.0)    vo = 0.0;
            ed.view_offset_px = vo;
        }
        const double tx = view_x0 - vo;
        const double baseline = redesign_baseline(font, L.field.y, L.field.h);
        const int band_y = static_cast<int>(
            std::nearbyint(baseline - gui_font_ascent_px(font)));
        const int band_h = static_cast<int>(std::nearbyint(gui_font_line_px(font)));
        cairo_save(cr);
        cairo_rectangle(cr, view_x0, L.field_inner.y, view_w, L.field_inner.h);
        cairo_clip(cr);
        const bool has_sel = editing && text_editor::has_selection(ed);
        if (has_sel) {
            const size_t s0 =
                static_cast<size_t>(text_editor::selection_start(ed));
            const size_t s1 =
                static_cast<size_t>(text_editor::selection_end(ed));
            const int hx0 = static_cast<int>(std::nearbyint(tx + bx[s0]));
            const int hx1 = static_cast<int>(std::nearbyint(tx + bx[s1]));
            const int hw  = hx1 > hx0 ? hx1 - hx0 : 1;
            paint_cell_rect(cr, GuiRect{hx0, band_y, hw, band_h}, ink_sel_fill);
            cairo_save(cr);
            cairo_rectangle(cr, view_x0, L.field_inner.y, hx0 - view_x0,
                            L.field_inner.h);
            cairo_rectangle(cr, hx0 + hw, L.field_inner.y,
                            (view_x0 + view_w) - (hx0 + hw), L.field_inner.h);
            cairo_clip(cr);
            set_palette_source(cr, ink_text);
            text_shape::show_shaped_run(cr, run, tx, baseline);
            cairo_restore(cr);
            cairo_save(cr);
            cairo_rectangle(cr, hx0, band_y, hw, band_h);
            cairo_clip(cr);
            set_palette_source(cr, ink_sel_text);
            text_shape::show_shaped_run(cr, run, tx, baseline);
            cairo_restore(cr);
        } else {
            set_palette_source(cr, ink_text);
            text_shape::show_shaped_run(cr, run, tx, baseline);
        }
        if (editing && !veiled && text_editor::cursor_visible_now(ed)) {
            const int pos = std::clamp(ed.cursor_pos, 0,
                                       static_cast<int>(shown.size()));
            paint_cell_rect(
                cr,
                GuiRect{static_cast<int>(std::nearbyint(tx + bx[static_cast<size_t>(pos)])),
                        band_y, caret_px, band_h},
                ink_text);
        }
        cairo_restore(cr);
        cp.stash.field_text_origin_x = tx;
        cp.stash.field_byte_x        = bx;
    }

    // OLD | NEW (not under the name ask, whose field stands over them).
    if (!asking) {
        paint_cell_rect(cr, L.swatch_old, hex(cp.old_rgb));
        paint_cell_rect(cr, L.swatch_new, hex(cp.rgb));
        paint_relief_sunken_outer(cr, L.swatch_frame);
    }

    // THE PRESET BUTTON, labeled with the active preset's shown name,
    // gray while the name ask stands.
    const bool menu_button_enabled = !asking;
    paint_picker_combo(
        cr, font, L.menu_button, L.menu_button_arrow,
        color_picker::preset_display_name(
            cp.scope, color_picker::active_preset(app, cp.scope)),
        cp.menu_open, menu_button_enabled);

    // THE THREE PUSH BUTTONS, published on the modal stash; all three gray
    // while the name ask stands, Paste while the slot is empty.
    {
        struct Plan {
            const char* label;
            AppState::ColorPickerButtonAct act;
            bool enabled;
        };
        const Plan plan[3] = {
            {"Copy",  AppState::ColorPickerButtonAct::Copy,  !asking},
            {"Paste", AppState::ColorPickerButtonAct::Paste,
             !asking && cp.slot_full},
            {"Close", AppState::ColorPickerButtonAct::Close, !asking},
        };
        for (int i = 0; i < 3; ++i) {
            const GuiRect& r = L.buttons[i];
            const bool enabled = plan[i].enabled;
            const bool armed   = !veiled && enabled && i == app.modal_dialog_pressed;
            const bool pressed = !veiled && enabled &&
                ((armed && app.modal_dialog_press_inside) ||
                 i == app.modal_dialog_key_pressed);
            const ButtonBoxFace box =
                paint_button_box(cr, r, /*lamp=*/false, pressed,
                                 ButtonFamily::Push);
            const text_shape::ShapedRun run =
                text_shape::shape_text_run(font, plan[i].label);
            const double lx = r.x + (r.w - std::ceil(run.width_px)) / 2 + box.shift;
            const double ly = redesign_baseline(font, r.y, r.h) + box.shift;
            if (enabled)
                show_row_text(cr, font, lx, ly, plan[i].label, palette().label);
            else
                show_row_text_embossed(cr, font, lx, ly, plan[i].label);
            if (veiled) continue;
            AppState::ModalDialogButton out;
            out.rect             = r;
            out.color_picker_act = plan[i].act;
            out.enabled          = enabled;
            out.tooltip          = plan[i].label;   // the act's name, nothing else
            dlg.buttons.push_back(out);
        }
    }

    // THE LIST, when down: the dropdown's box and rows (paint_dropdown's
    // own composition one surface over), floating over whatever lies under
    // the chooser.
    cp.stash.list = GuiRect{0, 0, 0, 0};
    cp.stash.list_items = {};
    cp.stash.list_bar = PopupScrollBar{};
    if (cp.chooser_open) {
        const auto label_of =
            cp.chooser_scope
                ? +[](const AppState&, int i) {
                      return color_picker::scope_display_name(
                          color_picker::scope_at(i));
                  }
                : +[](const AppState& a, int i) {
                      return color_picker::element_display_name(
                          color_picker::scope_element_at(
                              a.color_picker.scope,
                              static_cast<std::size_t>(i)));
                  };
        paint_combo_list(cr, font, L.list, cp.chooser_pressed,
                         cp.chooser_hover, cp.chooser_scroll.held, label_of);
        for (int i = 0; i < L.list.count; ++i)
            cp.stash.list_items[static_cast<std::size_t>(i)] = L.list_items[i];
        cp.stash.list = L.list.box;
        cp.stash.list_bar = L.list.bar;
    }

    // THE PRESET MENU, when down: the same box and THE ROWS THE LAYOUT
    // SHOWS (the scroll's block — color_picker.h's THE PRESET MENU; the
    // popup lists' scroll, render.h), its separators — one before each
    // group — the pull-downs' own (paint_popup_separator, etched and
    // inset) where each is in view — every label at THE
    // DROP-DOWN INSET (color_picker::combo_text_inset_px, 2026-10-09, the
    // scope and element lists' own), each act's enabled bit asked once here
    // (preset_act_enabled) and published with its row; a grayed row wears
    // no lit face and its label the emboss (paint_dropdown's rules); then the
    // scroll bar when it stands. EVERY ROW IS PUBLISHED, a row scrolled out
    // of view with the zero rect, so the lit and pressed indexes are the
    // menu's own.
    cp.stash.menu = GuiRect{0, 0, 0, 0};
    cp.stash.menu_rows.clear();
    cp.stash.menu_bar = PopupScrollBar{};
    if (cp.menu_open) {
        const std::vector<color_picker::PresetMenuRow>& rows = L.menu_rows;
        assert(rows.size() == L.menu_items.size());
        paint_popup_chrome(cr, L.menu, PopupFace::Menu);
        // The pull-downs' separator (paint_popup_separator), from the box's left edge to its right
        // edge — the bar's left edge while the bar stands.
        for (const int sep_y : L.menu_sep_ys) {
            const bool at_bar = L.menu_bar.present;
            const int sep_r = at_bar ? L.menu_bar.bar.x : L.menu.x + L.menu.w;
            paint_popup_separator(cr, L.menu.x, sep_r, at_bar,
                                  sep_y + popup_sep_margin_y_px());
        }
        const int pad_l = color_picker::combo_text_inset_px();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const color_picker::PresetMenuRow& row = rows[i];
            const GuiRect& item = L.menu_items[i];
            const bool enabled =
                !row.is_act || color_picker::preset_act_enabled(app, row.act);
            const bool shown = item.w > 0 && item.h > 0;
            const bool lit = enabled && (cp.menu_pressed == static_cast<int>(i) ||
                                         cp.menu_hover == static_cast<int>(i));
            if (shown) {
                if (lit) paint_cell_rect(cr, item, palette().selected_fill);
                const std::string label =
                    row.is_act
                        ? std::string(color_picker::preset_act_label(row.act))
                        : color_picker::preset_display_name(cp.scope, row.name);
                const double base = redesign_baseline(font, item.y, item.h);
                if (enabled)
                    show_row_text(cr, font, L.menu.x + pad_l, base, label,
                                  lit ? palette().selected_text
                                      : palette().label);
                else
                    show_row_text_embossed(cr, font, L.menu.x + pad_l, base,
                                           label);
            }
            AppState::ColorPicker::MenuRowStash out;
            out.rect    = item;
            out.is_act  = row.is_act;
            out.act     = static_cast<int>(row.act);
            out.name    = row.name;
            out.enabled = enabled;
            cp.stash.menu_rows.push_back(std::move(out));
        }
        paint_popup_scroll_bar(cr, L.menu_bar, cp.menu_scroll.held);
        cp.stash.menu = L.menu;
        cp.stash.menu_bar = L.menu_bar;
    }

    // THE PUBLICATION.
    cp.stash.valid     = !veiled;
    cp.stash.session   = cp.session;
    cp.stash.card      = L.card;
    cp.stash.scope     = L.scope;
    cp.stash.chooser   = L.chooser;
    cp.stash.field     = L.field;
    cp.stash.field_inner = L.field_inner;
    cp.stash.swatch_old = L.swatch_old;
    cp.stash.swatch_new = L.swatch_new;
    cp.stash.menu_button = L.menu_button;
    cp.stash.menu_button_enabled = menu_button_enabled;
    cp.stash.wheel     = L.wheel;
    cp.stash.wheel_cx  = L.cx;
    cp.stash.wheel_cy  = L.cy;
    cp.stash.wheel_outer_r = L.outer_r;
    cp.stash.wheel_inner_r = L.inner_r;
    if (!veiled) {
        dlg.box     = L.card;
        dlg.field   = L.field_inner;
        dlg.session = live_session;
        dlg.valid   = true;
    }
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

    // The band's ground, one fill across the whole band in the chrome's
    // ground. The overlay's Hilight line above it (or above the dialog
    // standing on it) is paint_bottom_overlays' (2026-10-10,
    // onscreen_keyboard.h's overlay block), and there is no line at its foot
    // or its sides: it ends at the window's edges as Cool Edit's status bar
    // does.
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
// FIELD TEXT, each row 17 Windows px with Windows' small 16-px icon
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
            // own, period pixel art no ground recolours — icons.h's head).
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
            // The row's text pair (surface_text, render.h).
            set_palette_source(cr, surface_text(lit ? GuiSurface::ListRowLit
                                                    : GuiSurface::ListRow));
            text_shape::show_shaped_run(
                cr, run, static_cast<double>(text_x), baseline);
            cairo_restore(cr);
        });

    cairo_restore(cr);
}

// -- THE CANVAS'S FRAME AND THE COLUMN'S MARGINS ---------------------------
//
// (architect 2026-10-09, the third part, "accurate to the mock-up"; METRICS
// §4.1, the mock of record's set 3; program_spec.h's column_margin_px and
// column_foot_px, render.h's canvas-frame accessors.) ONE PAINTER for the
// rows from the canvas's top frame row (top lane 8) through the column's
// foot (bottom lane 2): the panel's FACE across them, the program's columns
// inside its frame (2026-10-10) — the margins beside the canvas, the frame
// rows' own ground and the foot's 5 W — then THE CANVAS'S FRAME, one ring
// round the waveform area: the
// `ce_dark` top row and left column, the `ce_hilight` right column and
// bottom row, MITRED at the top-right and bottom-left corner blocks where
// the dark meets the light (paint_relief_frame, cool_edit_paint.h's diagonal
// rule). It runs before render_canvas, which lays the canvas over the
// interior this face covered; the view bar's and the ruler's own side
// columns are their painters' (render_trim_flags, paint_ruler_row). THE
// SAME UNDER EVERY CHROME; the program's colors alone.
namespace {
void paint_canvas_column_frame(cairo_t* cr, const AppState& app) {
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_canvas_frame_area(app);
    const GuiRect foot = bottom_column_foot_area(app);
    const int y1 = foot.y + foot.h;
    if (top.w <= 0 || y1 <= top.y) return;
    const GuiPalette& p = palette();
    const int lw = program_line_px();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    paint_cell_rect(cr, GuiRect{top.x, top.y, top.w, y1 - top.y}, p.face);
    paint_relief_frame(cr,
                       GuiRect{area.x - lw, top.y, area.w + 2 * lw,
                               foot.y + lw - top.y},
                       p.ce_dark, p.ce_hilight);
    cairo_restore(cr);
}
} // namespace

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
    // the canvas's frame and the column's margins then take the panel's face
    // and the frame's ring (paint_canvas_column_frame, above), and the
    // waveform area its own `waveform_canvas` ground with the horizontal grid
    // over it (render_canvas, render.h's row-6 canvas paragraph). Unconditional and
    // ahead of every content branch, so a cold frame (loading, no audio, or a
    // null plate before the first worker publish) shows canvas where the
    // waveform will be rather than a chrome-colored hole. The outer clip already
    // bounds this to the exposed rect, so the full-rect fill costs nothing off
    // the damage.
    //
    // THE CENTER LINES PAINT WHENEVER THE GRID DOES (the canvas's picture is
    // one thing: the canvas, the grid, the ink if any, the center lines —
    // render.h's row-6 canvas paragraph): with audio loaded they paint over
    // the plate in the branch below (its blit skipped on a null plate, the
    // lines still painted there); with none — no source, or a load running,
    // the branch's own condition `plate_branch` — they paint here, directly
    // over the canvas and its grid, under the same keyboard clip.
    const bool plate_branch = audio.total_frames() > 0 && !app.loading;
    {
        paint_canvas_column_frame(cr, app);
        const GuiRect canvas = waveform_area(app);
        // AND NOT UNDER THE ON-SCREEN KEYBOARD. The painted rect's one owner is
        // onscreen_keyboard::waveform_paint_area (the rule is stated there):
        // the band is opaque and paints after everything below, so the ground
        // under it is a fill nobody ever sees. The rect handed to render_canvas
        // is still the WHOLE area — the channels' bands and their lines are
        // its geometry — so the band is subtracted with a CLIP rather than
        // with a smaller rectangle. Nothing at all on a platform with no
        // painted keyboard.
        const GuiRect painted =
            onscreen_keyboard::waveform_paint_area(app, gui);
        const bool band_cuts = painted.h < canvas.h;
        if (band_cuts) {
            cairo_save(cr);
            cairo_rectangle(cr, painted.x, painted.y, painted.w, painted.h);
            cairo_clip(cr);
        }
        render_canvas(cr, canvas);
        if (!plate_branch) render_center_lines(cr, canvas);
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
        // THE PROGRAM'S FRAME (2026-10-10, render.h's program_frame_rect):
        // the ring round the program, with the rows it wraps — its columns
        // stand in the line every program lane leaves, so no lane painter
        // covers it. Not exposure-gated: a handful of rects, the outer clip
        // bounding them.
        paint_program_frame(cr);
    }

    if (plate_branch) {
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
        //   2. paint_canvas_column_frame — the panel's face from the canvas's
        //      top frame row through the column's foot and the canvas's
        //      frame ring — then render_canvas — the waveform area's ground
        //      and the horizontal grid (above, unconditional; 2026-10-09; no
        //      vertical grid, render.h's row-6 canvas paragraph) — and, on a
        //      frame with no audio loaded (no source, or a load running:
        //      !plate_branch), THE CENTER LINES straight over it
        //      (render_center_lines), so the grid never stands without them.
        //   3. the two redesigned top button rows and the
        //      unified bottom row (its chrome, buttons, clock AND state cell
        //      in one painter),
        //      each on its own
        //      exposure (above, outside this branch; they own lanes nothing
        //      below them paints on), then THE PROGRAM'S FRAME round the
        //      program (paint_program_frame, 2026-10-10: its two rows lanes of
        //      their own, its columns the line every program lane leaves).
        //   4. waveform plate -> THE CENTER LINES over the ink
        //      (render_center_lines, architect 2026-10-09 evening; with audio
        //      loaded they paint here and not at step 2, a null plate's frame
        //      included) -> phase-reset overlay ring.
        //   5. LIVE TRIM, one pass, entirely inside the column's air and the
        //      trim lane: Cool Edit's view bar and its span (2026-10-09; no
        //      cursor mark in the bar, ~14:30).
        //   6. the MARKER STEMS (waveform): the cues' dots, rows ≡ 3 of 4.
        //   7. the CURSOR's CANVAS DOTS (paint_playheads, rows ≡ 1 of 4 —
        //      the head is the ruler pass's, step 9).
        //   8. the SCANNER (waveform), solid.
        //   9. the RULER lane — Cool Edit's flipped ruler, its ticks and
        //      digits — AND, in the same pass, the cursor's HEAD (the yellow
        //      triangle on the ruler's bottom rows, opaque over the ticks and
        //      digits) and the MARKER LANE'S panel face under the cues.
        //  10. the FLAG BLIT — the cues.
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
        //      panel stands. Both tenants stand over the program's frame
        //      (full window width, 2026-10-10), and AFTER THE SLOT the bottom
        //      overlays' line and the dialog's ground (paint_bottom_overlays,
        //      2026-10-10: the keyboard and the dialog laid over row 8 and
        //      the frame's bottom row as Cool Edit's status bar, the line
        //      above the pair, which the waveform's painted rect also leaves
        //      out).
        //  13. the flag editor's box, then THE NOTIFICATION CARDS
        //      (paint_notifications, 2026-08-29 — the top-right stack, above
        //      every lane and the keyboard slot), then the dropdown — the
        //      floating surfaces, after every pass above and outside this
        //      branch — then the MODAL DIALOG (paint_modal_dialog,
        //      2026-08-12; the bottom row the prompts and the five modal
        //      editors paint in since 2026-08-13, the dialog overlay's ground
        //      since 2026-10-10), and LAST the TOOLTIP,
        //      which reads that stash.
        // (The bottom row left the tail of this sequence in row 7 — it paints
        // with the other redesigned rows at step 3, on every frame class, and
        // overlaps none of these passes.)
        // Two structural rulings live in this sequence:
        //   THE RECOLOR MODEL (architect 2026-07-26) — a highlight REPLACES
        //     colors, it never washes over them; and since the sweep's region
        //     highlight retired (architect 2026-10-03, "the trim bar is
        //     enough") NO pass recolours the waveform at all. The phase-reset
        //     overlay contributes no ground (architect 2026-07-27): its
        //     RING is its whole visual — one-device-px dots in its reset's
        //     stem pattern (2026-10-09 ~17:40) — painted AFTER the plate,
        //     over the ink like the stems' dots, and before them.
        //   THE COLUMN IS SHARED, NOT CONTESTED (architect 2026-10-09, the
        //     program is Cool Edit): a SELECTION adds no playhead-like mark of
        //     its own, its whole cue being its members' selected LABELS with
        //     the landed cursor on the focus; the cursor's dots paint over the
        //     marker stems' (2026-09-23) and the two phases interleave, so a
        //     playhead resting on a marker's column shows both and neither
        //     yields; its head in the ruler and the cue's triangle in the
        //     marker lane never meet. (2026-08-01 lifted the SCANNER above the
        //     stems, so the moving line does not blink out at every marker it
        //     crosses.)

        if (rects_intersect(exposed, wave_paint)) {
            paint_waveform_plate(cr, area);
            // THE CENTER LINES OVER THE INK (architect 2026-10-09 evening;
            // render_center_lines, render.h's row-6 canvas paragraph): each
            // channel's zero row, one device px in `center`, right after the
            // blit and under every dot, ring and line that follows.
            render_center_lines(cr, area);
            // The overlay band's dotted ring — the phase-reset overlay's whole
            // visual — over the plate and under the stems and the cursor's
            // dots: its left side is the focused reset's own stem, which
            // paint_marker_stems lays down, the playhead's dots interleaving
            // there as on any stem.
            paint_phase_reset_overlay_ring(cr, area);
        }

        // LIVE TRIM PASS — the old trim-stem-cache slot, now covering ALL trim
        // pixels (the view bar whole and the column's air above it).
        // Gated on EITHER half being exposed: render_background erased every
        // exposed top-strip pixel above, so a strip-only damage (hover text, a
        // flag change) must repaint the strip-resident trim pixels; the outer
        // Cairo damage clip bounds the actual work either way.
        if (rects_intersect(exposed, wave_paint) ||
            rects_intersect(exposed, top_strip)) {
            paint_trim(cr, area, top_strip);
        }

        // MARKER STEMS BEFORE THE CURSOR (architect 2026-09-23): the cursor's
        // dots paint after them; on a shared column the two dot phases never
        // touch (2026-10-09, the paint-order block above).
        if (rects_intersect(exposed, wave_paint)) {
            paint_marker_stems(cr, area);
        }

        // The CURSOR'S CANVAS DOTS AFTER THE MARKER STEMS (the
        // playhead-over-stems ruling, architect 2026-09-23). Everything laid
        // down before it — the plate, the phase-reset overlay ring — stays
        // under it as before. (The scanner paints after it, below.) Its head
        // is the ruler pass's and its view-bar dots the trim pass's, so this
        // pass is the waveform's alone.
        if (rects_intersect(exposed, wave_paint)) {
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
            // The ruler paints BEFORE the flags: it lays the marker lane's
            // panel face, which the flag blit's cues stand on.
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
    // THE BOTTOM OVERLAYS' LINE AND THE DIALOG'S GROUND (2026-10-10,
    // onscreen_keyboard.h's overlay block): after the slot, whose keyboard
    // band they head, and before the floating surfaces; the dialog's
    // controls paint on this ground at paint_modal_dialog. Nothing while
    // neither overlay stands.
    paint_bottom_overlays(cr);

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
