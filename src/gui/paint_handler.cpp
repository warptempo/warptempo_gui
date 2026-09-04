#include "paint_handler.h"
#include "target_render.h"
#include "notifications.h"
#include "external_sync.h"   // GuiExternalSyncWorker::is_busy, the process line's fallback

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
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
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
// THE STATUS TEXT IS SANS, AND MONOSPACE IS A NAMED THREE-CELL SET
// (architect 2026-08-11, HIS REVERSAL of his own 2026-08-01 absolute, "I wanted
// to get rid of monospace altogether — the last row should be the same font as
// the rest"): monospace is the CLOCK's — the unified bottom row's
// centre cell (paint_bottom_row_buttons_and_clock, which owns the face, the
// size and the cell) and, since 2026-08-28, the render player's modal clock,
// which is that same cell said twice and takes its metrics from it — and,
// SINCE 2026-09-03, THE AV SYNC STATS PANEL (architect, with the panel): a
// DELIBERATE WIDENING rather than a leak, and the reason is the one the clock's
// own reversal turned on. The panel is a COLUMN OF FIGURES read against each
// other — a label column and a numbers column, over rows that are rebuilt every
// frame as the measurements move — and a proportional face would make the
// numbers walk under the eye at 60 Hz, which is exactly the wiggle the clock
// takes the face for. So the set is THREE CELLS, ENUMERATED HERE AND AT
// gui_font.h — the row-8 clock, the player's modal clock and the stats panel's
// lines — and nothing outside those three may take the face. (The RENDER
// PLAYER'S and the OPEN PROJECT PICKER'S listings were in it for the few hours
// of that evening the architect tried the monospace against their width
// asymmetry, and he reverted them himself at his first look: "revert the
// monospace back to sans-serif proportional on the picker and player and just
// keep it on the AV sync." Re-greped 2026-09-03: four GuiFontFamily::Mono
// selections in this file — the line-metrics probe, this cell, the player's
// modal clock and the stats panel's band.)
//
// EVERY STRING THE STATE CELL CARRIES — the queue/render status and the
// history walk line — is the redesign's sans at the redesign's size, shaped
// and painted through the ONE chokepoint like every other redesigned row, and
// so is every NOTIFICATION CARD's line (paint_notifications, 2026-08-29). The
// product has one text size, so none of the three moves those strings have
// made — the bottom row to the tab row on 2026-08-13, the tab row to the
// one-day status bar on 2026-08-29 and the bar back into row 8's own cell that
// evening — changed a glyph. (The dirty mark used
// to be on this list; since 2026-08-01 it is in the WINDOW TITLE, which labwc
// paints — see GuiPlatform::apply_window_title.)
//
// The no-wiggle DERIVATION — the widest digit, the "DD:DD.DDD" specimen —
// belongs to the clock and lives at its own metrics, re-derived on the
// monospace face rather than trusted to it.

// The bottom row's ONE sans face, selected on `cr` through the one face owner
// (gui_font.h). Returns the scaled font every shape and paint that takes it
// must share — the text_shape precondition is that a run is shown with the same
// font it was shaped with. Its callers are the MODAL DIALOG (whose labels,
// field text and button words are the row's only sans left) and, through
// show_row_text below, the modal's own strings; the clock selects monospace for
// itself.
static cairo_scaled_font_t* select_bottom_row_face(cairo_t* cr) {
    gui_select_font_face(cr, GuiFontFamily::Sans);
    cairo_set_font_size(cr, redesign_font_size_px());
    return cairo_get_scaled_font(cr);
}

// Shape and paint one run at (x, baseline) in `color`. The row's plain-text
// tier — the successor of text_display::draw_line, which died with the
// monospace path it drew in.
//
// RETURNS THE RUN'S ADVANCE (0 for an empty string), for the one caller that
// BUTTS a second run against the first: row 8's lower left, whose clock and
// state text are one face on one baseline but two runs with two clip fates
// (the clock never clipped, the state clipped to its own room). Every other
// caller lays out from a reserved cell or a button rect and ignores it — this
// is the run's own width, not a cell's.
static double show_row_text(cairo_t* cr, cairo_scaled_font_t* font,
                            double x, double baseline,
                            std::string_view text, GuiColor color) {
    if (text.empty()) return 0.0;
    const text_shape::ShapedRun run = text_shape::shape_text_run(font, text);
    cairo_set_source_rgb(cr, color.r, color.g, color.b);
    text_shape::show_shaped_run(cr, run, x, baseline);
    return run.width_px;
}

// THE STATE TEXT IS PART OF ROW 8'S CLOCK RUN (architect 2026-08-29, folding
// the STATUS BAR that had stood for that one day back into the toolbar he
// already reads — "status bars are generally the last row" was the bar's
// reasoning, and seeing it he preferred the text on the row and found the bar a
// duplicate of that panel's own foot; then 2026-08-31, dissolving the separate
// CELL it had been for those two days into the clock's own string). The lower
// left is ONE MONOSPACE LINE — `00:00.100 | Updating...`, the pipe a literal
// character — painted by paint_bottom_row_buttons_and_clock with the clock; the
// ruling is docs/engineering/architecture/messaging.md. It is SHAPED AS TWO
// RUNS on the one face, size and baseline (the clock, then " | " + the state
// butted at its advance) because the two halves take different clips: the
// clock is never clipped and the state is clipped to its own room, or absent
// when it has none. The line the reader sees is unaffected.
//
// STATE, NOT EVENTS: what is true right now, replaced as it changes, with NO
// timeouts and no clear on a key press — which is why the "revealed stale"
// class the transient tier suffered cannot exist here. EVENTS are the
// NOTIFICATION CARDS (paint_notifications, notifications.h).
//
// ONE CELL, TWO STRINGS THAT RANK: the `h` walk line wins while the view
// stands, and the render's progress line is what it carries otherwise. THE
// RESOLVED READOUT — the third state string, and the bar's right cell for its
// one day — RETIRED WHOLE with the bar: nothing displays a resolved value any
// more, bare `j` copies it and Shift+`j` goes to the marker it came from.
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
// and its `h`-view overlap with the tabs all went with the bar's landing: the
// tab row paints tabs over its whole width again.
//
// THE DIRTY DOT is not a tenant: it rides the WINDOW TITLE beside the project
// name, where labwc paints it (GuiPlatform::apply_window_title).

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
    // (render_flag_editor_box) — which is why the editing target's box is NOT in
    // this surface at all: it is skipped at cache-build time (2026-08-02) rather
    // than painted here and covered, because the overlay is narrower than the
    // committed box whenever the edited text is shorter. Like the other
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

// -- The redesigned rows: paint_menu_row / paint_tab_row / paint_icon_row
// -- (paint_toolbar_row died with row 2 at the 2026-08-12 relayout) ----------

namespace {

// THE KDENLIVE REDESIGN'S SHARED TEXT FACE. The SIZE (kRedesignFontSizePt /
// redesign_font_size_px) moved to render.h when row 5's marker flags began
// shaping their own labels inside render.cpp — one design size cannot have two
// definitions. THE FACE itself is stated here, where every row selects it: it
// is cairo's toy "sans" selector, which fontconfig resolves to Liberation Sans
// on the target — the family the crops were rendered in. The monospace face
// survives untouched on every un-redesigned surface, and each redesign row
// moves its own text over.

// (The authored-length -> device-pixels conversion every dimension below takes
// is scaled_px, render.h — the ONE conversion the whole scale axis shares.)

// THE GROUND ROW 1 PAINTS ON (row 2's other consumer died with that lane at
// the 2026-08-12 relayout), in one owner because three things read it:
// the ground fill itself, the disabled face's mix target, and the click face's.
// Focused it is the crops' #292c30; unfocused it darkens to #202326 with the
// labwc titlebar above (the ruling and the constant's provenance are at
// kRedesignRowGroundUnfocused, render.h). Row 3 does NOT call this — its
// ground is the fixed content ground #202326, which happens to equal the
// unfocused shade and has nothing to swap.
//
// THE TWO MIXES FOLLOW THE GROUND rather than the focused constant, which is
// the whole reason this is a function and not two literals at the fill sites: a
// disabled label is measured as a FRACTION of itself over the ground it sits on,
// and the click fill as a 30% accent tint of that same ground, so both keep
// their measured relationship in either state instead of drifting when only the
// ground moves.
//
// THE VERDICT IS chrome_focused (app_state.h), WHICH IS THE ACTIVATION FLAG
// AND NOTHING ELSE since 2026-09-03 evening: a standing folder overlay was a
// second term from 2026-09-02 and the architect took it out with the ruling
// that left the File anchor live under the band ("the title bar is still the
// regular one — the window has focus"). What that term was landed for — the
// view bar's three reading as disabled under a modal — moved to the bar's own
// verdict, view_bar_focused, which is the only surface that needed it; the
// owner carries both rulings and the successions between them. GAP 1's band
// above the row takes this same shade from paint_menu_row, so the tall row
// swaps whole.
GuiColor redesign_row_ground(const AppState& app) {
    return chrome_focused(app) ? kRedesignRowGround
                              : kRedesignRowGroundUnfocused;
}

// THE ACCENT'S FOCUS FORK — ONE OWNER for "which blue does a face that says
// SELECTED or FOCUSED wear right now" (architect 2026-09-02: "breeze blue
// should change to #1b4155 when window loses focus"). Its two readers are the
// folder overlay's panel (the highlighted row's band and its list-focused
// outline) and the modal row's ACTIVE-FOCUS outline, which is the row the
// panel's two owners take whole; the provenance, the scope and what is
// deliberately NOT in it are at kRedesignAccentInactive (render.h).
//
// IT IS A FORK ON THE FLAG AND NOTHING ELSE — no owner term, no surface term.
// A focus ring is a claim about where the KEYBOARD is, and an unfocused window
// has no keyboard, so the same dimming is honest on every button of that row
// whichever of its four owners is standing. What must NOT read it is a HOVER:
// the pointer can still act on an unfocused window, so those faces keep
// kRedesignAccent at their own sites.
GuiColor accent_for_focus(const AppState& app) {
    return app.window_activated ? kRedesignAccent : kRedesignAccentInactive;
}

// ROW 1, measured off the two 46x30 crops
// (tmp/screenshots/kdenlive/redesign/row_1_button_{rest,hover}.png). The row
// height itself lives in render.h as kMenuRowHeightPx, because main.cpp's lane
// table needs it.
//
// THE CSS FLOAT MODEL is the ruled layout vocabulary (architect 2026-07-31): a
// flat button FILLS ITS WHOLE ROW and no margin or inset exists unless the
// architect states one. The hover pill therefore spans the row's full CONTENT
// height — the 1px vertical inset that stood here was a misread of the crop
// (those rows were the title-bar seam, not design). That content height is
// kMenuRowHeightPx = 30, the crop's own height again since 2026-08-21 (it
// stood at 34 from 2026-08-02, an allowance for the right float's 32px
// buttons that now derive their box from the row instead — render.h carries
// the succession), and the lane is one pixel taller
// still: row 1 gained a 1px MARGIN-BOTTOM (kMenuRowMarginPx) which is lane, not
// button — the pill fills the 30 and stops at the margin strip.
constexpr double kMenuLabelPadPx   = 10.0;   // per side, sets the button width
constexpr double kMenuPillRadiusPx = 5.0;    // the crop's AA fits r ~ 4.6

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
//     "Synchronize to External Storage", "Paste Phase Reset State", "AV Sync
//     Stats"), a modal row's WORD button ("Copy to
//     Clipboard"), a panel's title, and A TOOLTIP THAT NAMES A GLYPH BUTTON'S
//     ACT ("Drop Marker (S)", "Go to Start (Home)", "Toggle Trim Region ([)").
//     The evidence is kdenlive's own: the Title Case hover on an icon button
//     IS the QAction's name echoed back by Qt, while every tooltip kdenlive
//     writes by hand is a sentence. A LAMP'S "Toggle <Mode> (<key>)" IS A NAME
//     under this — Qt names a checkable action the same way ("Show Audio
//     Thumbnails") — the lamp FACE, never the words, carrying the state.
//   * A DESCRIPTION IS SENTENCE CASE — a label, a field prefix, a tooltip that
//     is a STATE or a REASON rather than an act ("At the trim end (End)",
//     "Markers are placed in source view (S)", "Committing the checkpoint
//     (Ctrl+S)"), the SECOND modifier line (always a whole sentence with
//     terminal punctuation: "Press Shift for the A/B audition."), a card, a
//     prompt (a question), the row-8 state cell, and a panel's row labels and
//     prose.
//   * SO ONE ACT SPLITS BY SURFACE, and that is the rule doing its work rather
//     than a carve-out: a CARD naming an act stays a sentence because a card
//     is a description ("BPM iterations work in source view"), while the
//     TOOLTIP naming the same act is that control's name ("Render Grid
//     Iterations (Ctrl+Alt+R)"), which is also the icon-row button's own
//     ("BPM Iterations (M)"). No reader compares any of these as strings — the
//     four item
//     tables are read by index (kFilePopupItems, kEditPopupItems,
//     kSettingsPopupItems, kHelpPopupItems, app_state.h) —
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
//     literal settings and config KEY names shown as keys ("sync_path is not
//     set"), and the routing/category tokens that are data rather than prose.
//   * KEY NAMES ARE QT'S, and so kdenlive's — Esc, Del, Return, Backspace,
//     PgUp, PgDown, Space, Tab, Home, End — with a BARE LETTER UPPERCASE and
//     punctuation naming the CAP rather than the stamped symbol ("Shift+[").
//     The spelling's one owner is spell_chord's head (gui_input.h).
//   * AN APPENDED REASON IS LOWERCASE: "<Act> refused: <reason>" is ONE
//     sentence, so its tail starts no second one. A producer whose string is
//     ever used WHOLE is itself a sentence and capitalizes at that producer;
//     the system's own words (`ec.message()`, `strerror`) arrive capitalized
//     and are the accepted class. The card-side statement is messaging.md's,
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
// SETTINGS SITS LAST OF THE APPLICATION'S OWN MENUS AND HELP AFTER IT
// (architect 2026-08-03 for the first clause, moving Settings behind the
// NAVIGATION anchor that then sat between it and File; 2026-09-03 for the
// second, with the Help anchor — kdenlive's own order, and every other Qt
// application's). The
// float is adjacent with no gap and the walk below is a pure shaped-run walk, so
// the order lives HERE and in the roster enum (app_state.h, whose comment states
// that enum order IS painted order and carries the rule in full) and in nothing
// else — no width, pad or anchor term reads it. THE FLOAT IS FOUR BUTTONS
// SINCE 2026-09-04, when the Iterations anchor was deleted with its menu; it
// was five from 2026-09-03, and two from 2026-08-15, when the Navigation
// anchor was deleted the same way.
constexpr MenuButtonDef kMenuButtons[] = {
    // THE FILE MENU (architect 2026-08-13) — the row's THIRD dropdown when it
    // landed, and one of TWO since the Navigation anchor's deletion on
    // 2026-08-15 — a COMMAND MENU of ONE row,
    // "Quit", in the slot the Quit BUTTON held from 2026-07-31: row 1 paints no
    // held face, so a button acting at the lift said nothing while it was down,
    // and the standard home for Quit is a File menu (kdenlive's own). Nothing
    // else moved — the label is shorter, so the float is 3px narrower at 100%
    // (the measurement is in the right float's collision note below).
    {RedesignButton::File,       "File"},
    // THE EDIT MENU (architect 2026-08-20) — the row's THIRD dropdown again,
    // painted between File and Settings, the standard order and kdenlive's own.
    // A COMMAND MENU of FIVE rows, the propagate family whole, and a
    // RELOCATION: IconCopy and IconPaste were deleted from the icon row in the
    // same ruling, so this menu is those commands' one pointer home rather than
    // a second road to them. Nothing here needed a width or pad term — the row
    // is one left-to-right accumulation over this table — but the LABEL'S WIDTH
    // does re-enter the right float's collision corner, which is recorded (and
    // deliberately not acted on) in that note below.
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
    // total or anchor expression to update — and what the deletion gives the
    // LEFT FLOAT back is the 85px slot that label cost, which the right
    // float's collision note below re-measures.)
    // (THE SECOND DROPDOWN, "Navigation" — architect 2026-08-02, a COMMAND MENU
    // of the zoom and stepping commands — painted between these two from that
    // day until 2026-08-15, when the architect deleted it whole: every one of
    // its seven rows had grown a button, so the menu was a slower path to
    // commands that all had one. Like Settings its action was a popup toggle
    // rather than a chord, and it shared the one popup state — see
    // AppState::Dropdown. Its removal is one row here and one enumerator there;
    // no width, pad or anchor term reads this table's length.)
    {RedesignButton::Settings,   "Settings"},
    // THE HELP MENU (architect 2026-09-03) — the row's FIFTH dropdown and the
    // only one painted to the RIGHT of Settings, which is what the order rule
    // above says the shell's own menu does. A COMMAND MENU of ONE row, "AV Sync
    // Stats", which is its chord like every other command row: Shift+L since
    // 2026-09-03 evening, so the width rule's optional column term finds a
    // hotkey and the popup draws an accelerator column. It was the one menu
    // whose row carried no accelerator at all for the hours before that
    // binding — the Settings menu's shape reached from the other side — and
    // the optional term is what let it be, no edit here being needed either
    // way.
    // ITS LABEL COSTS THE FLOAT 54px at 100% and 118 at 225%, measured on this
    // file's own advance-width method, and the right float is untouched: the
    // collision corner is at the LEFT float's total against the view bar's,
    // and the note below carries what that leaves.
    {RedesignButton::Help,       "Help"},
};

// ROW 1'S RIGHT FLOAT — THE VIEW BAR (architect 2026-08-02), kdenlive's
// workspace switcher (kden1.png's blue "Logging | Editing | Audio | Effects |
// Color" bar, the one row the redesign had left out) reborn as the three
// ABSOLUTE VIEW SELECTORS: S+W, T+P, T+W, which are bare 1/2/3.
//
// THE FLOAT'S ROOM, re-measured 2026-09-04 when the ITERATIONS ANCHOR was
// deleted: the LEFT
// float is 227 authored px at 100% (File 47 + Edit 48 +
// Settings 78 + Help 54, each a shaped run plus 2 x 10px pad) and ~503 device
// px at the tablet's 225% — the Iterations slot's own ~189 off the 692 the
// five-anchor float measured, the device figure carrying its tilde because a
// shaped run's width is not exactly proportional to its scale (the same
// caveat the collision note below states in full); the RIGHT float below is
// 183 and 409. So the two meet
// only once the window is narrower than 410 device px at 100% or ~912 at
// 225% — the laptop's 1920 leaves 1510 of clear ground and the tablet's 2304
// leaves ~1392, and even the 640px DEFENSIVE FLOOR at 100% keeps 230. THE
// FIVE-ANCHOR FIGURES WERE 312 and 692, meeting at 495 and 1101; the deletion
// moved the corner further away in every column. The row
// still carries NO COLLISION RULE, which is the redesign's own answer
// everywhere (the statement is at kMinWindowWidthPx, render.h): a window driven
// under those figures crops, and no host of this product is near them.
//
// THE CSS FLOAT VOCABULARY, and this is its first RIGHT float — recorded here
// because the model has only ever walked left-to-right before. The div's right
// edge is flush with the WINDOW's right edge (no margin stated, so none exists)
// and it spans the row's full CONTENT height; the layout walk inside it runs
// left to right from the div's own left edge, exactly like every other row's.
// Only the div's ORIGIN is new, and it needs all three widths before it can be
// placed, which is why the labels are shaped up front rather than in the walk.
//
// MARGINS DO NOT COLLAPSE HERE (a stated fact, like every margin in this
// redesign): each button carries 1px on all four sides, so two adjacent buttons
// sit 2px apart and the div is 1 + w + 2 + w + 2 + w + 1 wide. The vertical
// pair DERIVES the button box from the row rather than setting the row's
// height (the 2026-08-21 reversal, render.h): content minus the two margins,
// 28 at 100% under the crop's own 30 — the walk below spells exactly that.
//
// A BUTTON'S OWN BOX is kdenlive's, read straight off the 82px "Logging" crop's
// scanline: [frame 1][fill 12][text][fill 12][frame 1], so the width is the
// shaped label plus 2*(border + padding) = label + 26, and the frame is drawn
// INSIDE the box (the row-3 side-border precedent — a face, never a size
// change).
constexpr double kViewBarBtnMarginPx = 1.0;    // all four sides, no collapse
constexpr double kViewBarBtnBorderPx = 1.0;    // drawn inside the box
constexpr double kViewBarBtnPadPx    = 12.0;   // per side, inside the border
constexpr double kViewBarRadiusPx    = 5.0;    // the redesign's one radius

struct ViewBarButtonDef {
    RedesignButton id;
    const char*    label;
};
constexpr ViewBarButtonDef kViewBarButtons[] = {
    {RedesignButton::ViewSW, "S+W"},
    {RedesignButton::ViewTP, "T+P"},
    {RedesignButton::ViewTW, "T+W"},
};
constexpr int kViewBarButtonCount =
    static_cast<int>(std::size(kViewBarButtons));

// ONE FACE PAINTER FOR THE WHOLE BAR, parameterized on the BAR BACKGROUND —
// the redesign_row_ground pattern, for its reason: every fill and every frame
// in the nine crops is either the background flat, the accent, or a FIXED LIFT
// OF THAT BACKGROUND, so the focused and unfocused bars are one rule applied to
// two grounds rather than two hand-spelled state tables that could drift.
// Provenance, the two fractions and their per-channel fits are at
// kRedesignViewBarBg (render.h).
struct ViewBarFace {
    GuiColor fill;
    GuiColor frame;
    bool     filled;   // false = the bar background already shows the rest face
    bool     framed;
};
ViewBarFace view_bar_face(GuiColor bg, bool focused, bool hovered,
                          bool selected, bool pressed) {
    ViewBarFace f{};
    // HOVER ONLY EVER MOVES THE OUTLINE, in BOTH focus states (architect
    // 2026-08-02, from the live test) — so the interior is the selected fact
    // alone and `hovered` never reaches this term.
    //
    // THIS SUPERSEDES A CROP. row_right_disabled_hover (the UNFOCUSED hover)
    // shows the interior lifted to #44464a where row_right_hover (the FOCUSED
    // one) keeps the flat #1e5774, and that asymmetry was reproduced faithfully
    // before the ruling. The architect ruled the simpler rule instead: hover is
    // the frame's job on both grounds. The crops still govern everything else
    // here — the SELECTED lifts are unchanged in both focus states, which is why
    // the two fractions below survive the ruling intact.
    const bool lift_fill  = selected;
    // THE PRESSED INTERIOR IS THE FOCUSED FACE'S ALONE, because there is no
    // unfocused click crop and there is no gesture that would need one: labwc is
    // click-to-focus, so the press that would show it ACTIVATES THE WINDOW
    // first and the button is already focused by the time it paints. If a press
    // ever does land unfocused, this falls through to the hover/selected face
    // rather than inventing a shade the crops never showed.
    const bool click_fill = pressed && focused;
    f.filled = click_fill || lift_fill;
    f.fill   = click_fill
                   ? mix_color(kRedesignAccent, bg, kRedesignClickMix)
                   : mix_color(kRedesignViewBarLiftBase, bg,
                               kRedesignViewBarSelectedMix);
    f.framed = hovered || pressed || selected;
    // The pointer's frame is the accent; a resting selected button's is the
    // calmer lift — row 4's rule, on this bar's own pair of colors.
    f.frame  = (hovered || pressed)
                   ? kRedesignAccent
                   : mix_color(kRedesignViewBarLiftBase, bg,
                               kRedesignViewBarFrameMix);
    return f;
}

// (ROW 2 — THE TOOLBAR — IS DELETED: 2026-08-12, the grand relayout's roster
// commit. The labeled Save / Undo / Redo / Render lane of 2026-07-31
// dissolved into the ICON ROW's first group of four glyph buttons
// (kIconRowButtons below), same chords and face machinery under 32px boxes,
// the old labels living on as their tooltips. Its painter, its layout
// constants, ToolbarButtonDef and kToolbarButtons went producer-less with
// it; what SURVIVES of row 2's measured anatomy is the MODAL DIALOG BUTTONS'
// box — kModalBtnBoxPx and the two label pads at the kModal* block below,
// which used to read the row's constants and now own the numbers, with the
// derivation recorded there. The crops and the full row-2 record stay in
// kdenlive-redesign.md.)

// ROW 3 — THE TAB ROW, measured at 100% off row_3_tab_{rest,hover,selected}.png
// (30 tall) with the padding taken from row_3_tab_pcmanfmqt.png and the border
// color from row_3_bottom_border.png. The lane metrics (30 content + 1 border)
// live in render.h with rows 1 and 2's, for the same reason.
//
// THE CSS FLOAT MODEL AT ITS PUREST: the tabs are FLUSH at the row's left edge,
// margin zero, adjacent with no gap, each filling the full 30px content height.
// A tab's box is its two 10px paddings around the shaped label OR the minimum
// width below, whichever is larger — and never a margin.
//
// THE MINIMUM WIDTH is what keeps a one-glyph label from producing a stub of a
// tab (architect 2026-07-31, ruled when the labels shortened to "A"/"B").
// Reconstructed from row_3_min_width.png, an 80x30 Breeze tab carrying a tiny
// label and a close button this product has no analogue for: behind the 1px
// left border the label field opens at x=1 and its ink sits at x 24..35,
// centered on 29.5 — which places the field's own center at 29 and therefore
// its right edge at 58, giving a 57px field plus the 1px border. 58 is the
// spec. The close button is IGNORED: it lives to the right of the field
// (ink at 60..67) and has no counterpart here.
//
// AT THE MINIMUM THE LABEL IS CENTERED IN ITS FIELD, not left-padded — the two
// paddings are a FLOOR term, not an anchor, so a label narrower than the minimum
// sits in the middle of the field rather than hugging its left edge. THE FIELD,
// NOT THE TAB (2026-08-01): the lock slot added its own width on the right,
// and centering in the total would have pushed the A/B off-centre in the space
// the eye reads as the tab's label area. The slot is gone (2026-08-14) and the
// field IS the tab now, so the two centrings coincide — the distinction stays
// stated because it is why the expression reads `field_w`.
constexpr double kTabLabelPadPx      = 10.0;  // per side, the width floor's term
constexpr double kTabMinWidthPx      = 58.0;  // see the reconstruction above
// THE 1px SIDE BORDERS OF THE SELECTED TAB ARE DRAWN INSIDE ITS OWN BOX, on the
// box's outermost columns, NOT outside it. That is a deliberate departure from
// "a border sits outside the content" and it is what keeps the geometry stable:
// a border outside would make the selected tab 2px wider than the same tab
// unselected, so switching tabs would visibly shove the other one sideways.
// Selection is a FACE, not a size.
constexpr double kTabTrimHeightPx    = 3.0;   // the selected tab's blue top
constexpr double kTabBorderPx        = 1.0;   // side borders / hover edge
// The selected tab's top corners round at r = 5, and that is MEASURED, not
// assumed: integrating the row_3_tab_selected.png corner's uncovered area
// against a quarter-disc gives 1.453 px^2 in row 1 for r = 5 against the crop's
// measured 1.453 (r = 4 predicts 1.204), and rows 0 and 2 agree to within a
// hundredth. It also puts the tab in the same corner family as row 1's hover
// pill and row 2's hover outline, both r = 5. The arc's uncovered pixels show
// whatever is behind them, which here is the row ground the lane fill already
// laid down.
constexpr double kTabCornerRadiusPx  = 5.0;
// (THE LOCK SLOT IS DELETED — architect 2026-08-14, "we should move the icon
// out of the tab and into the icon row, then show the current tab's readonly
// value". From 2026-08-01 every tab reserved a permanent pcmanfm-style
// CLOSE-ICON slot on its right — a 16px box 8px inside the tab's right edge,
// both numbers measured off row_3_min_width.png and row_3_tab_pcmanfmqt.png
// and confirmed against row_3_tab_pcmanfm-qt_close_hover.png — carrying a
// bright closed padlock when the tab was read-only and a dimmed open one when
// it was writable, with the ACTIVE tab's rect published for a press that
// dispatched bare `o`. The padlock is now a roster button in the icon row's
// last group (RedesignButton::IconReadOnly), reporting the ACTIVE tab's bit,
// which is what makes it its chord literally. A TAB IS ITS LABEL FIELD AGAIN:
// no slot, no added width, no rect. The crop provenance and the two numbers
// stay in kdenlive-redesign.md's row-3 record, and the slot's own
// partition bug — a third rounded constant that did not partition, codex round
// 2, 2026-08-10 — is history there too.)

// THE PAINTER'S HALF OF THE TAB ROSTER: each tab's roster id, its A/B letter
// and its label. The press claim (input_pointer.cpp) reads the same ids out of
// app.redesign_buttons; the letter is what the paint compares against
// app.active_tab_view, so the selected tab is read LIVE every paint and there
// is no second copy of "which tab is current" anywhere.
//
// TWO ROWS, ALWAYS — the row's membership does not move with any mode. It grew
// two compare-only slots on 2026-08-07 for the (walk source, reading) product
// and lost them again on 2026-08-08, when the architect moved the READING onto
// its own icon-row toggle and left the tabs naming the walk alone; the empty-rect
// publication those slots needed went with them, so every def here paints and
// publishes on every run.
//
// `letter` is the A/B tab's own — what the paint compares against
// app.active_tab_view — and `label` the painted word, in every state since
// 2026-08-18: the `h` view's walk selector had an override answering for both
// slots ("Remote" / "Local") from 2026-08-05 until then, and it is deleted with
// the repurposing.
struct TabDef {
    RedesignButton id;
    char           letter;
    const char*    label;
};
constexpr TabDef kTabs[] = {
    {RedesignButton::TabA, 'A', "A"},
    {RedesignButton::TabB, 'B', "B"},
};

// A rounded rectangle from four quarter-circle arcs, used FILLED for row 1's
// hover pill and (through redesign_face_box) for every face-box surface.
// (Row 2's hover outline was its stroked consumer until that row's 2026-08-12
// deletion.)
void redesign_rounded_rect_path(cairo_t* cr, double x, double y,
                                double w, double h, double r) {
    constexpr double kPi = 3.14159265358979323846;
    if (r > w * 0.5) r = w * 0.5;
    if (r > h * 0.5) r = h * 0.5;
    if (r <= 0.0) { cairo_rectangle(cr, x, y, w, h); return; }
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -0.5 * kPi, 0.0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0.0,         0.5 * kPi);
    cairo_arc(cr, x + r,     y + h - r, r, 0.5 * kPi,   kPi);
    cairo_arc(cr, x + r,     y + r,     r, kPi,         1.5 * kPi);
    cairo_close_path(cr);
}

// A FACE BOX: the half-stroke-inset rounded rect every redesigned button-like
// surface draws its fill and its frame on. ONE PATH, filled and/or stroked —
// never a fill on the full box under a stroke on an inset one — so the fill's
// edge and the stroke's centreline cannot describe different rectangles. THE
// INVARIANT, stated once here and pointed at from every caller: the frame
// insets by HALF ITS OWN WIDTH on all four sides, so the band lands on the
// box's outermost pixel ring with no straight edge antialiased — the +0.5
// half-pixel alignment at a 1px stroke, the integer bound at an even one, both
// parities from the one expression — and the radius insets by the SAME half so
// the corner stays concentric with the box. `radius` is the authored (outer)
// corner radius; nullptr for either colour omits that pass.
//
// Callers: the view-bar buttons, the icon-row buttons, the popup chrome
// (paint_popup_chrome) and the dropdown's hovered item. ONE surface in the
// family deliberately keeps its own body: the selected tab strokes the
// OPEN-BOTTOM redesign_rounded_top_rect_path (a shared closed box would seal
// it). (Row 2's hover outline was a second keeper — its radius passed
// UN-inset — until that row's 2026-08-12 deletion took the whole painter.)
void redesign_face_box(cairo_t* cr, int x, int y, int w, int h,
                       int lw, double radius,
                       const GuiColor* fill, const GuiColor* line) {
    const double half = static_cast<double>(lw) * 0.5;
    redesign_rounded_rect_path(cr, x + half, y + half,
                               static_cast<double>(w - lw),
                               static_cast<double>(h - lw),
                               radius - half);
    if (fill != nullptr) {
        cairo_set_source_rgb(cr, fill->r, fill->g, fill->b);
        if (line != nullptr) cairo_fill_preserve(cr);
        else                 cairo_fill(cr);
    }
    if (line != nullptr) {
        cairo_set_source_rgb(cr, line->r, line->g, line->b);
        cairo_set_line_width(cr, static_cast<double>(lw));
        cairo_stroke(cr);
    }
}

// THE BUTTON-FACE PUBLICATION, one writer so a row cannot forget a field: the
// painter stashes the rect it painted plus the three face bits it is painting
// (redesign_button_enabled / redesign_button_selected /
// redesign_button_glyph_swapped — the same
// predicates main.cpp's per-tick drift comparator replays), and returns the
// face so the caller reads .hovered / .selected back for its own paint. Every
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
// restores the premise at the one writer: a face whose rect is not fully
// inside the current clip keeps its as-painted bits, the comparator sees the
// drift on the next tick, and its own full-strip damage is what repaints and
// republishes — the repair mechanism the comparator was always documented to
// be. An EMPTY rect refreshes unconditionally: it publishes "not painted at
// all", no pixel can be stale for it, and refreshing is what keeps the
// comparator from thrashing under a standing modal. THE MODAL YIELD IS THE ONE
// PRODUCER of an empty rect since 2026-08-18 — every row paints every member
// otherwise, the bottom row's cluster swap having gone with the history
// companions' return to the icon row — and the contract is at
// paint_bottom_strip's yield branch.
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
// bit; `glyph_swapped` is stashed here for exactly that reason and its whole
// argument is at the predicate (app_state.h).
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
    bool pixels_covered = rect.w <= 0 || rect.h <= 0;
    if (!pixels_covered) {
        double cx1 = 0.0, cy1 = 0.0, cx2 = 0.0, cy2 = 0.0;
        cairo_clip_extents(cr, &cx1, &cy1, &cx2, &cy2);
        pixels_covered =
            static_cast<double>(rect.x) >= cx1 &&
            static_cast<double>(rect.y) >= cy1 &&
            static_cast<double>(rect.x + rect.w) <= cx2 &&
            static_cast<double>(rect.y + rect.h) <= cy2;
    }
    if (pixels_covered) {
        face.enabled  = redesign_button_enabled(app, audio, audio.total_frames(),
                                                playback, target_render, id);
        face.selected = redesign_button_selected(app, id);
        face.glyph_swapped = redesign_button_glyph_swapped(app, id);
    }
    return face;
}

// ROW 4 — THE ICON ROW, measured at 100% off the five 32x32 state crops
// (row_4_button_{rest,hover,click,selected,selectedhover}.png),
// row_4_separator.png (1x34) and row_4_bottom_border.png. The lane metrics
// (46 content + 1 border, so a 47px lane at 100%) live in render.h with
// rows 1-3's, and so does the BUTTON's own box since 2026-08-28 (the block
// below says why).
//
// THE VERTICAL STORY IS PURE CENTERING (and it is what resolves the architect's
// 48-vs-6+34+6 discrepancy, recorded at kIconRowHeightPx): the 32px buttons
// land at +7 and the 34px separators at +6, each centered in the 46px content
// band by its own arithmetic rather than by a stated margin — which is exactly
// what the stated margins were, so the centering reproduces them.
//
// THE HORIZONTAL WALK uses TWO different gaps, which is this row's own rule and
// not row 2's: 2px between ADJACENT buttons, and 4px on each side of a
// SEPARATOR (row 2 used 5). The row opens with 8px of padding — icon_row_pad_x
// (paint_handler.h), which lives in the header because the BOTTOM ROW reads it
// too since 2026-08-14.
//
// THE BUTTON'S OWN BOX — kIconBtnPx, kIconBtnGapPx, kIconGlyphPx and
// kIconCornerRadiusPx — LEFT THIS FILE 2026-08-28 for render.h's icon-row
// block, where their measurements are recorded beside the lane metrics they
// were taken with: the folder overlay's rows are buttons since that day
// (folder_overlay.h) and a second file reads them, so each number has one
// definition and this row spells none of them. What stays here is the ROW's
// own chrome, which nothing outside this file paints.
constexpr double kIconSepGapPx        = 4.0;    // each side of a separator
constexpr double kIconSepWidthPx      = 1.0;
constexpr double kIconSepHeightPx     = 34.0;
constexpr double kIconOutlineStrokePx = 1.0;

// THE PAINTER'S HALF OF THE ICON-ROW ROSTER: each button's id and its content,
// a 22px breeze ICON. The press claim's chord table (input_pointer.cpp) is the
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
// only letter faces from its first day. The architect gave them real Breeze
// glyphs that day (the picks and their runners-up are at icons.h's enum), which
// left the letter arm with no producer, so the field and the painter's shaped
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
    // in the 32px box now (Save's VcsCommit swap and Render's DialogCancel
    // swap ride redesign_button_icon below; media-record serves BOTH plain
    // render and the iteration sweep by the architect's same-day ruling, the
    // tooltip alone forking). The old labels are the tooltips.
    {RedesignButton::Save,       icons::Icon::DocumentSave},
    {RedesignButton::Undo,       icons::Icon::EditUndo},
    {RedesignButton::Redo,       icons::Icon::EditRedo},
    {RedesignButton::Render,     icons::Icon::MediaRecord},
    // THE TWO VIEW LAMPS SHARE ONE GROUP since later on 2026-09-04, where the
    // radio-pair collapse had left each of them alone behind a separator of its
    // own: one lamp per view axis, side by side, is what the pair reads as, so
    // the architect deleted the divider between them. The group is the row's
    // second and IconAudioView opens it.
    //
    // THE COLLAPSE ITSELF (architect 2026-09-04): one
    // button per axis where four radios stood. Each wears THE LIT STATE'S OWN
    // GLYPH, which is the collapse's rule across all three lamps — the audio
    // lamp lights in Target and wears document-import, the marker lamp lights
    // in Phase Reset and wears chronometer-start. The two glyphs the retired
    // halves wore, document-export and speedometer, left icons::Icon with
    // them; the picks and their runners-up are still recorded at that enum.
    {RedesignButton::IconAudioView,    icons::Icon::DocumentImport},
    {RedesignButton::IconMarkerColumn, icons::Icon::ChronometerStart},
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
    // CENTERED LAMP (2026-08-31, R11) lands beside Follow at that group's
    // tail, TWENTY-SEVEN in SIX.)
    // THE ZOOM GROUP OPENS HERE SINCE 2026-08-27 (architect), on the
    // separator the TRIM GROUP had held since 2026-08-11 — the scissors opened
    // it then, the Show trim region button filled it on 2026-08-16 and led it
    // later that day, the scissors were deleted on 2026-08-18 leaving it one
    // member, and this ruling merged that one member into the zoom four behind
    // it by DELETING THE BOUNDARY IN FRONT OF IconZoomIn. The architect's
    // 2026-08-11 placement ("place it after the warp/phase radio buttons,
    // create a new separator") is where the merged group still sits, and the
    // group he opened it as — a home for VIEWPORT-RELATED ACTS — is what the
    // merge finishes: the trim overlay, the four horizontal magnifiers, the
    // vertical pair and FOLLOW, all in one separator-led run. THIS TABLE IS THE
    // ROW'S PAINTED ORDER — the walk
    // below is a plain accumulation over it — so a reorder is rows swapping,
    // plus the group's leader in redesign_button_opens_icon_group
    // (app_state.h) and the roster enum's own order, which the three keep in
    // step. No count, no gap and no width follows a swap.
    //   bare `[`, the trim region toggle (bare `x` until 2026-08-24,
    //   Ctrl+Shift+X until 2026-08-18). The glyph is TOOL-RECT-SELECTION,
    //   the marching-ants rectangle — the
    //   architect's pick, taken at 22px though he named the 24px path (same
    //   rectangle, and 22 is the set's convention). A TOGGLE with a lamp on the
    //   overlay's visibility, and NO SECOND GLYPH for the hidden half: every
    //   eye-shaped alternative collides with ViewHidden, which is already
    //   IconMarkerDisable (the ruling is at the roster entry, app_state.h).
    // (THE SCISSORS SAT SECOND HERE — bare `x`, set trim from region, wearing
    // Breeze's EDIT-CUT — from 2026-08-11 until the architect retired the
    // BUTTON on 2026-08-18 ("remove the 'set trim from region' icon"). The
    // chord is untouched; the glyph went with the row, having had no other
    // consumer.)
    {RedesignButton::IconShowRegion, icons::Icon::ToolRectSelection},
    // THE ZOOM FOUR (2026-08-12, the grand relayout — the architect's live
    // placement, "the rest in the icon row, after the trim"): zoom in
    // (Ctrl+`=`), zoom out (Ctrl+`-`), full zoom out (bare `0`) and
    // working-zoom center (bare `c`), four navigation acts which OPENED the
    // group until the 2026-08-27 merge above put them behind the trim
    // overlay's own separator — the stepping pair's chords bare until
    // 2026-08-27, when they traded
    // spellings with the magnification pair below them. The
    // 2026-08-02 no-duplicate-commands deletion of the old zoom pair is
    // SUPERSEDED by this order: these buttons are the same commands' pointer
    // home for the glass rig, and they kept their rows in the Navigation
    // dropdown besides — until 2026-08-15, when that duplication was what
    // deleted the MENU instead, every one of its seven rows having grown a
    // button of its own (the record is at kFilePopupItems, app_state.h).
    {RedesignButton::IconZoomIn,       icons::Icon::ZoomIn},
    {RedesignButton::IconZoomOut,      icons::Icon::ZoomOut},
    {RedesignButton::IconZoomFitBest,  icons::Icon::ZoomFitBest},
    {RedesignButton::IconZoomOriginal, icons::Icon::ZoomOriginal},
    // THE WAVEFORM MAGNIFICATION PAIR (2026-08-26), closing the same group:
    // the picture's VERTICAL gain, magnify (bare `=`) then reduce (bare `-`),
    // wearing Breeze's zoom-in-y / zoom-out-y — the magnifier construction
    // beside them with an AXIS RULER down the left of the lens, which is what
    // tells the pair apart from the four horizontal magnifiers at row size.
    // They join the zoom group rather than opening one, so the row gains two
    // boxes and two 2px gaps and no separator moves.
    // (A THIRD BUTTON SAT HERE FOR ONE DAY — the MAGNIFICATION RESET on
    // Ctrl+0, wearing zoom-fit-height — and the architect deleted it on
    // 2026-08-27 when the pair moved onto the bare keys; the glyph went with
    // it, having had no other consumer.)
    {RedesignButton::IconWaveformMagnify, icons::Icon::ZoomInY},
    {RedesignButton::IconWaveformReduce,  icons::Icon::ZoomOutY},
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
    // Follow's icon walked twice: the provisional "F" letter, then
    // media-seek-forward (2026-07-31), then go-jump (2026-08-01) — the architect
    // settling on the chevron-and-dot, which reads as GOING to a place rather
    // than as a transport control.
    {RedesignButton::IconFollow, icons::Icon::GoJump},
    // THE CENTERED LAMP (2026-08-31, R11), Follow's neighbour at the zoom
    // group's tail: Breeze's align-horizontal-center, two boxes threaded on
    // one vertical center line — a viewport holding its subject at the
    // center column. It joins the group rather than opening one, so the row
    // gains one box and one 2px gap and no separator moves.
    {RedesignButton::IconCentered, icons::Icon::AlignHorizontalCenter},
    // THE CENTER-ON-NEXT-MARKER LAMP (2026-09-04) closes the same group behind
    // the centered pin: Breeze's zoom-next, a magnifier ring with a chevron
    // stepping forward out of it — the walk bringing its next stop into view.
    // It joins the group rather than opening one, so the row gains one box and
    // one 2px gap and no separator moves.
    {RedesignButton::IconCenterOnNext, icons::Icon::ZoomNext},
    // THE RESTRICT-UNDO-TO-VIEWPORT LAMP (2026-09-04) closes the same group
    // behind the Center on next marker lamp, "it is also a viewport gesture"
    // being the architect's own reason for moving it here from the toolbar
    // group, where it had stood between Redo and Render for the hours of its
    // first day. What it decides is whether an undo or redo may take the CAMERA
    // somewhere else, which is the question every lamp in this group answers
    // for its own gesture. Breeze's timeline-lift, a clip's two end brackets
    // with a red cross between them — a stretch of timeline the editor declines
    // to travel. It joins the group rather than opening one, so the move adds
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
    // THE GLYPHS ARE THE ONES THAT LEFT WITH THE BUTTONS: music-note-16th,
    // Breeze's flagged quaver, for the BPM opener — a tempo in beats per
    // minute asks for a note — and mathmode, an italic f beside a
    // multiplication cross reading as f(x), for the mode lamp, whose slot has
    // kept a MATH SYMBOL since 2026-08-18 (it took the summation sigma's place
    // when that glyph moved to the CUMULATIVE toggle). Both files came back
    // verbatim from the installed 22px set with their provenance assets, so
    // neither is a fresh pick and neither metaphor was re-argued.
    {RedesignButton::IconBpm,  icons::Icon::MusicNote16th},
    {RedesignButton::IconIter, icons::Icon::Mathmode},
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
    // The padlock, Breeze's object-locked / object-unlocked pair, the very
    // glyphs the tab slots drew. The TABLE entry is the closed lock and the
    // resolver (redesign_button_icon, above) is what swaps it for the open one
    // on a writable tab — every button goes through that resolver, so this
    // constant is the fallback rather than the painted truth.
    {RedesignButton::IconReadOnly,       icons::Icon::Lock},
    // THE HISTORY GROUP — the row's last, and a separator-led group of its own
    // again since 2026-08-18 ("place a separator before the history button, and
    // place cumulative/etc after the history button"). It had exactly this
    // shape from 2026-08-04 — "a separation there and then another button", the
    // architect's ask, spelled in the row's one grouping vocabulary (the 4px /
    // 1px line / 4px separator, redesign_button_opens_icon_group, app_state.h)
    // — grew to five through 2026-08-08, and moved LEFT of the mass-marker
    // category on 2026-08-13 so that the opener's x could not move on the
    // toggle. That last placement is SUPERSEDED and needs no revival: nothing
    // in this row is ever hidden, so every x here is a constant by
    // construction.
    //
    // THE OPENER (bare `h`) leads, then the WALK LAMP (bare `g`, later on
    // 2026-08-18), then the four companions in the order they
    // have always held — how the delta READS, what you can DO from inside the
    // view, then where you can STEP — and, since 2026-09-01, THE LOAD IN PLACE
    // at the tail, the view's second act arriving from the render-entry
    // group. The companions' glyphs came back with them
    // unchanged but for one: the CUMULATIVE toggle wears BLACK_SUM, the
    // summation sigma, since 2026-08-18 (a cumulative delta is a sum over the
    // walk's members), where it wore Breeze's two-colour deep-history from
    // 2026-08-09 — and that glyph dressed the Git walk radio here until the
    // 2026-09-04 collapse retired that half.
    // Revert keeps document-revert and the walk keeps the keyframe dials.
    {RedesignButton::IconHistory,       icons::Icon::VcsDiff},
    // THE WALK LAMP (architect 2026-08-18 as a radio pair, one button since the
    // 2026-09-04 collapse), where he put the surface: "add two radio buttons
    // after history button, before cumulative". It wears the LIT state's glyph
    // like the two view lamps — SHALLOW-HISTORY, the clock dial with no sweep
    // arm, for a timeline reaching back no further than this run — because Git
    // is the walk's default and the lamp reports Session. The Git half's
    // deep-history clock, which the Cumulative toggle had yielded to it on
    // 2026-08-18, left icons::Icon with that half.
    {RedesignButton::HistoryWalk,       icons::Icon::ShallowHistory},
    {RedesignButton::HistoryCumulative, icons::Icon::BlackSum},
    {RedesignButton::HistoryRevert,     icons::Icon::DocumentRevert},
    {RedesignButton::HistoryOlder,      icons::Icon::KeyframePrevious},
    {RedesignButton::HistoryNewer,      icons::Icon::KeyframeNext},
    // THE LOAD IN PLACE CLOSES THE ROW (architect 2026-09-01), off the
    // render-entry group above: the icon row's press runs the `h` view's own
    // load — the confirmation on the viewed walk member — so it paints with
    // the view's other seven and greys with them outside it. It keeps
    // DIALOG-OK-APPLY, the checkmark, which is why the render player's Load in
    // place button took that same glyph the same day. NO SEPARATOR MOVED and
    // no box was added: one box changed groups, so the row's authored-px walk
    // is what it was.
    {RedesignButton::IconLoadInPlace,   icons::Icon::DialogOkApply},
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
        case RedesignButton::Save:   return icons::Icon::VcsCommit;
        // RENDER'S MID-RENDER FACE (architect 2026-08-11): the CANCEL glyph
        // while a render or sweep is live — dialog-cancel, the circle-slash,
        // transcribed for row 8's short-lived Esc button and kept for exactly
        // this face when that button was deleted. The bit, its rank over the
        // iteration label and the click's divergence are all at
        // AppState::render_cancel_face.
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
// Measured off hover_shift.png (129x41, the two-line form we ship) and
// hover_plain.png (112x26, the one-line reference we do not). Both share every
// chrome pixel; the colors and the dim factor live in render.h.
//
// THE CORNER RADIUS IS THE REDESIGN'S ONE RADIUS, 5, on BOTH floating surfaces
// and on the dropdown's items — and it is measured, not assumed. Fitting each
// corner by summed squared per-channel error:
//   the tooltip's chrome  -> best at a PATH radius of 5.0 (1034), 4.5 next (1686);
//   a dropdown item HOVER -> best at a PATH radius of 5.0 (6856), 4.5 next (15059);
//   a dropdown item CLICK -> best as a FULL-BOX fill at 5.5 (1853), 5.0 next (9639).
// All three agree on an OUTER corner of ~5.5 (a stroked path at 5.0 has its
// outer edge half a line further out). The authored 5 is the nearest integer,
// half a pixel tight, and keeps ONE radius across the whole redesign rather than
// giving this surface a fractional constant of its own. The briefs' eyeball
// estimates (2-3 here, 2-3 for the tooltip) are far outside every fit.
constexpr double kPopupCornerRadiusPx = 5.0;

// THE TOOLTIP'S TYPE AND SPACING. Two sizes now: the NAME line stays at the
// redesign's 12pt, and the SHIFT line drops to 10pt (architect 2026-07-31 — the
// hint is subordinate text and should read as such). Both go through the one
// shaping chokepoint, which takes whatever size the context carries.
//
// THE VERTICAL LAYOUT IS DERIVED, NOT AUTHORED, and it is SYMMETRIC BY
// CONSTRUCTION: each line occupies its own face's (ascent + descent) band, the
// two bands are separated by a real gap, and the SAME padding closes the box
// above the first band and below the last. So the box height falls out as
//     pad + band1 [+ gap + band2] + pad
// and the top and bottom air are equal by the arithmetic rather than by a
// measured pair that could drift. At 100% on the target face that is 6 + 19 + 6
// = 31 for one line and 6 + 19 + 4 + 16 + 6 = 51 for two — replacing the old
// 41px two-line box, which the architect read as too tight between the lines and
// bottom-heavy. render.h carries only a BOUND on this for the damage band.
constexpr double kTooltipShiftFontSizePt = 10.0;
constexpr double kTooltipPadYPx          = 6.0;   // top AND bottom, equal
constexpr double kTooltipLineGapPx       = 4.0;   // between the two bands
constexpr double kTooltipPadXPx      = 5.0;
// (The damage BOUND on the height and the hover dwell live in render.h — the
// run loop reads both, for the due-check and for the band under the strip.)

// THE TOOLTIP'S TEXT lives with the roster, not here
// (redesign_button_tooltip, app_state.h, owns both the membership and the text;
// its MODIFIER line is static_asserted against redesign_button_shift_admits and
// redesign_button_ctrl_admits, so the hint cannot appear where a modified press
// does nothing).
//
// EVERY BUTTON BUT ROW 1'S HAS ONE (architect 2026-07-31, stated as the ROW's
// property at the table): the one-line form is the whole story for most, and the
// buttons that admit a modifier add the hint line below it (the membership is
// those two predicates, app_state.h, never a count restated here) — Render only
// while iteration mode is OFF, where its shift press has a twin to reach.
// (The TEXT and its membership live at redesign_button_tooltip, app_state.h —
// beside the roster, because the pointer side reads the same table.)

// THE DROPDOWN, in the architect's CSS terms. The item height is AUTHORED, not
// derived: 29 at 100%, measured off dropdown_full.png, and it lives with
// dropdown_h_px's other ingredients in render.h (see kPopupItemHeightPx there,
// which owns that measurement).
//
// IT IS NOT THE TOOLTIP'S INTERIOR, though it was once written that way — as
// "26 total less its two 1px borders = 24", from an era when the tooltip box was
// an authored 26. Neither number survives: the tooltip's box is not authored at
// all now, its height falling out of pad + band [+ gap + band] + pad on the
// face's own extents (31 for one line at 100%, the record at
// kTooltipShiftFontSizePt). The two floating surfaces share their CHROME — one
// face box, one border, one radius — and not their heights. Do not re-derive
// one from the other: that 31 - 2 == 29 is an arithmetic coincidence between a
// font-driven quantity and a crop-measured one, and tying them together would
// make the menu's row height move whenever the face's metrics did.
//
// The width derives rather than being authored: the widest shaped label, plus
// the redesign's standing 10px label padding per side (rows 1 and 3's), plus the
// 3px item inset per side, plus the two 1px borders. The architect pixel-tweaks
// at 100% by moving these terms.
// (The item height, its block margin, the separator's vertical margin and the
// border live in render.h with dropdown_h_px's other ingredients — the
// popup's OPEN EDGE must size the box before it is painted. Only the HORIZONTAL
// terms, which depend on the widest shaped label, are the painter's alone.)
// (EVERY MENU SHARES EVERY NUMBER IN THIS BLOCK — chrome, item height, insets,
// separator, faces — and since 2026-08-03 the horizontal terms below as well.)
constexpr double kPopupItemInsetPx   = 3.0;   // the highlight box, per side
constexpr double kPopupSepInsetPx    = 7.0;   // the separator, per side

// THE MINIMUM ITEM WIDTH — the tab-min-width pattern, and the reason a menu of
// short labels still reads as a menu. THE VALUE IS AUTHORED, NOT DERIVED: 242
// is the architect's knob turned at the live look (2026-08-03, a flat +42px on
// the number below), which is what this constant has always been for. Nothing
// re-derives it and nothing should try.
//
// ITS PREDECESSOR WAS a derivation, and the distinction is the point: 200 came
// off the crop, whose items are 401px for labels of ~150px ink with roughly
// half that width going to an accelerator and submenu column — taking the text
// half alone and scaling to THE LABEL SET OF THAT DAY (widest "Playback speed"
// at 113px — a row that left with its key when playback_speed retired whole,
// 2026-08-27) landed there. That measurement is the record of where 200 came
// from and nothing more: it is neither what sets the floor now nor a current
// roster figure.
//
// WHICH MENUS IT BINDS ON, measured against the width rule this file paints by
// (content = 57 indent + widest label + [13 gap + widest accelerator] + 30
// right margin − 8 chrome, at 100%):
//
//   File       "Synchronize to External Storage" | "Ctrl+Q"   -> content
//   Settings   "Projects Repository" (no column)              -> estimate FLOOR
//   Edit       "Paste Phase Reset State" | "Ctrl+Alt+Shift+P" -> 374   content
//   Help       "AV Sync Stats" | "Shift+L"                    -> estimate FLOOR
//
// EVERY FIGURE HERE WAS MEASURED ON THE SENTENCE-CASE SPELLING these labels
// carried until 2026-09-03, when the dropdown items took Title Case (the one
// declared exception, stated at this file's capitalization block). A capital
// for a lowercase letter is a few px per word, so each row's ink moved by
// single digits and NO VERDICT MOVED: the painter takes the larger of floor
// and content at every paint, so the columns above say which TERM wins, and
// re-measuring an ink width that was never the deciding quantity would buy
// nothing.
//
// THE SETTINGS MENU LEFT THE FLOOR ON 2026-08-26, when "Waveform magnification
// level" joined its roster and took the widest-label slot at 209px (the item's
// own note is at kSettingsPopupItems, app_state.h — the box "simply grows",
// and this is what it grew into): its content asked 288 at 100%, past the
// 242, so that popup DERIVED at every scale — 296px wide with its chrome —
// and the +42 did not land on it. THAT ROW LEFT 2026-09-02 (R-23) and three
// device-key rows joined (R-22), the widest label now "Projects Repository"
// — some 19 characters against the 28 that asked 209, so by the same
// advance-width method the content sits under the 242 and the FLOOR is the
// width again; unmeasured, and it does not have to be: the painter takes the
// larger of the two terms at every paint, so which wins is never in doubt,
// only which term it is.
//
// WHAT THE FLOOR HOLDS UP TODAY IS THE SHORT MENUS: SETTINGS and HELP, whose
// widest labels are short enough that the content ask stays under the 242 —
// which is what gives a short menu a box that reads as a menu at all, and is
// the whole of what this constant is for. Neither figure is stated: the
// painter takes the LARGER of floor and content at every paint, so which term
// wins is the only thing this table has to say.
//
// THE ITERATIONS MENU HELD THE FLOOR UP FROM 2026-08-27 UNTIL ITS DELETION ON
// 2026-09-04 and is worth one line as the measurement that showed the floor
// doing its work: two rows of "BPM Iterations"/"Grid Iterations" beside single
// letters, the widest label 104px by the advance-width method this file's
// collision note uses, for a content ask of 209 — the pre-rebrand pair, "BPM"
// and "Iterations", asked 171. Both were under 242, so the floor was that
// box's width in either spelling; the 2026-08-31 rebrand's one owed check
// was never in doubt, only which term won.
//
// FILE LEFT THE FLOOR ON 2026-08-27. It was one row of "Quit" beside "Ctrl+Q"
// when the table above was written and asked 168; it carries THREE rows now —
// "Open Project" | "Ctrl+O", "Synchronize to External Storage" | "\\" (its
// accelerator since 2026-08-31; the row was chord-less before it) and
// "Quit" | "Ctrl+Q" — and the Synchronize label's shaped
// run puts its content past the 242, so that popup is DERIVED at every scale
// now like Settings and Edit. NO NUMBER IS STATED FOR IT HERE: the ink is the
// shaper's and the rows are app_state.h's (kFilePopupItems), so a figure
// written down here would go stale the next time either moves, and which TERM
// WINS is the only thing this table has to say. (The deleted NAVIGATION menu was the
// one that never reached it: its accelerator column put content at 57 + 117
// ("Previous marker") + 13 + 101 ("Ctrl+Shift+Tab") + 30 − 8 = 310, already
// past both floors, so its box stayed 318px wide. That measurement is kept
// because it is what shows the floor and the derived column composing — the
// shape the Edit and Settings menus now stand in.)
//
// What the 2026-08-03 harmonization did to the SETTINGS box is separate and
// still true: the labels now start on the shared 57px indent rather than a 12px
// item pad, so the generous-right-side reading the original derivation leaned on
// is retired. The floor survives as what it does now — a width the shortest
// menus cannot fall below.
constexpr double kPopupItemMinWidthPx = 242.0;

// THE DROPDOWN'S HORIZONTAL TERMS, measured off the two-column crop
// (dropdown_full_hotkeys.png, 403x579, the popup box including its 1px borders)
// and authored in POPUP-BOX coordinates, which is how the crop reads: a menu
// with an accelerator column is easier to state — and to check against the crop
// — from the box's own edges than from the item box's.
//
// EVERY MENU TAKES THEM (architect 2026-08-03): the settings menu's own 12px
// item-box label pad is retired and its labels start on this same indent, so
// the menus differ in one derived term (whether an accelerator column
// exists) rather than in their padding.
//
//  - THE LABEL INDENT is 57px from the popup's left edge to the label's pen
//    origin (ink starts at 57 or 58 depending on the glyph — "Switch Monitor"
//    and "Go To" at 57, "Focus Timecode" and "Monitor Config" at 58 — and the
//    same 57 holds on the rows that carry an icon at 39, so the column is real
//    and not a per-row accident). kdenlive reserves a CHECKBOX plus an ICON
//    gutter in there; we have neither and reproduce the RESULTING INDENT as
//    plain padding, by ruling.
//  - THE COLUMN GAP is the crop's GUARANTEED minimum separation: the widest
//    label ink ends at x=239 ("Switch Monitor Fullscreen") and the leftmost
//    accelerator ink starts at x=252 ("Ctrl+Shift+Space"), so 13px is what the
//    design promises when both columns are at their widest. (Per-row gaps are
//    all larger — 100px on the widest label's own row — because those two rows
//    are different rows; 13 is the number the min-width rule is built on.)
//  - THE RIGHT MARGIN is 30px from the popup's right edge to the accelerator's
//    last ink column, uniform across every hotkey row (all end at x=372). In
//    the source that margin also holds the submenu-arrow column (the arrows run
//    out to 388); we have no submenus and keep the margin, which is what makes
//    the accelerators sit off the edge rather than against it.
// The three reproduce the crop's own width to a pixel: 57 + 183 (widest label
// ink) + 13 + 121 (widest hotkey ink) + 30 = 404 against the measured 403.
// (They lost their `Nav` prefix with the harmonization — the indent and the
// margin are now every menu's, and the gap is a term of the ONE width rule
// below, present exactly when the optional accelerator column is.)
constexpr double kPopupLabelIndentPx  = 57.0;
constexpr double kPopupHotkeyGapPx    = 13.0;
constexpr double kPopupPadRightPx     = 30.0;

// The selected tab's outline: a rectangle with ROUNDED TOP corners, and — this
// is the load-bearing part — NO BOTTOM EDGE. It is an OPEN path running up the
// left side, over the two arcs and the top, and back down the right side, which
// is the whole shape of a Breeze tab: a tab is open at the bottom, into the
// content it selects.
//
// The openness matters ONLY to the STROKE (the 1px side borders), which is
// exactly where it must: a closed path would lay a line across the tab's foot
// and wall off the opening the whole design is about. cairo_fill closes any
// open subpath implicitly, so the same helper still fills correctly for the two
// FILLED uses — the blue trim band, and the tab's own interior, which it lays
// over the darker bar since the 2026-08-13 ground ruling (the shape is what
// puts the bar's colour in the corner notches).
void redesign_rounded_top_rect_path(cairo_t* cr, double x, double y,
                                    double w, double h, double r) {
    constexpr double kPi = 3.14159265358979323846;
    if (r > w * 0.5) r = w * 0.5;
    if (r > h)       r = h;
    if (r <= 0.0) {
        // Degenerate (a scale so small the corners vanish): the same open
        // three-sided walk, so the stroke keeps its no-bottom-edge property.
        cairo_new_sub_path(cr);
        cairo_move_to(cr, x, y + h);
        cairo_line_to(cr, x, y);
        cairo_line_to(cr, x + w, y);
        cairo_line_to(cr, x + w, y + h);
        return;
    }
    // Clockwise from the bottom-left corner, so both arcs run in cairo's
    // increasing-angle direction and neither needs the negative variant.
    cairo_new_sub_path(cr);
    cairo_move_to(cr, x, y + h);
    cairo_line_to(cr, x, y + r);
    cairo_arc(cr, x + r,     y + r, r, kPi,       1.5 * kPi);  // top-left
    cairo_line_to(cr, x + w - r, y);
    cairo_arc(cr, x + w - r, y + r, r, 1.5 * kPi, 2.0 * kPi);  // top-right
    cairo_line_to(cr, x + w, y + h);
}

// The baseline for a label vertically centered in `box`: SOLVED from the face's
// own extents, never a measured literal — center the (ascent + descent) band in
// the box and put the baseline at its foot, then round to the pixel grid so the
// glyphs stay crisp at every scale. Row 1 centers in the ROW (a flush button),
// the modal dialog's buttons in their 32-tall box (row 2's, until that row's
// 2026-08-12 deletion); one formula, two box kinds.
double redesign_baseline(cairo_scaled_font_t* font, double box_y,
                         double box_h) {
    cairo_font_extents_t fe;
    cairo_scaled_font_extents(font, &fe);
    return std::nearbyint(box_y + (box_h + fe.ascent - fe.descent) * 0.5);
}

} // namespace

void GuiPaintHandler::paint_menu_row(cairo_t* cr) {
    // THE MENU ROW (top lane 0, at the window edge): a flat kdenlive-sampled
    // ground carrying TWO FLOATS — the LEFT one, "File" and
    // "Settings", and the RIGHT one, the view bar's S+W / T+P / T+W (the right
    // float 2026-08-02, File replacing the Quit button 2026-08-13, the
    // Navigation anchor deleted from between them 2026-08-15). No ring;
    // the kdenlive bar is flat.
    //
    // THE LEFT FLOAT'S HOVER MODEL IS KDENLIVE'S, and it is TWO faces for BOTH
    // buttons — plus ONE mode-scoped third, the history view's disabled
    // face, which since 2026-08-08 lands on the SETTINGS anchor alone (File
    // stays lit in there, its menu working; the partition is
    // history_mode_disables_button's and nothing here restates it) (below, at the
    // pill):
    // at rest the label paints bare on the row ground;
    // hovered, a filled blue pill sits under it, FLUSH with the row's CONTENT
    // height (the css float model — a flat button fills its whole row, architect
    // 2026-07-31). A PRESS PAINTS NOTHING NEW — a click keeps the hover face and
    // only pointer-out rests it. The click and disabled faces belong to rows 2
    // and 4, so these two have no press-state machinery at all.
    //
    // BOTH ACTIONS ARE THE SAME KIND since 2026-08-13: each button TOGGLES
    // A DROPDOWN — the roster's two non-chord actions, since no keyboard chord
    // opens or closes a popup. The menus lead only where the keyboard already
    // goes: the bare `;` key still opens the settings editor directly, and
    // File's one item is Ctrl+Q. (The left float
    // held a CHORD button until that day — Quit, dispatched through the shared
    // chord table like every other redesigned button; the act is the File menu's
    // item now, and the chord is untouched. It held a THIRD ANCHOR, Navigation,
    // until 2026-08-15: its every item was a key too, which is exactly what
    // deleted it — the keys had all grown buttons of their own.)
    //
    // THE RIGHT FLOAT IS A DIFFERENT SURFACE ON THE SAME ROW: its own background
    // div, five faces from its own crops, and three chord buttons that are bare
    // 1/2/3. Its layout, its box model and its face rule are at kViewBarButtons
    // and view_bar_face above; its colors at kRedesignViewBarBg (render.h).
    const GuiRect row = top_menu_row_area(app);
    if (row.w <= 0 || row.h <= 0) return;

    // THE LANE IS CONTENT + MARGIN-BOTTOM (render.h's menu_row_* trio). Both
    // floats fill the CONTENT band; the margin strip below it is left to the row
    // ground, which is exactly what a css margin shows. Under the left buttons
    // that is indistinguishable from the ground above; under the RIGHT float it
    // is the whole point, holding the bar's blue off the row below (the tab
    // row, since the 2026-08-12 relayout deleted the toolbar lane between).
    const int content_h = row.h - menu_row_margin_h_px();
    if (content_h <= 0) return;

    cairo_save(cr);

    // The ground covers the WHOLE LANE, margin included — one fill, and the
    // margin needs no second source. AND IT COVERS GAP 1 ABOVE THE LANE
    // (architect 2026-09-03, main.cpp's vertical rule: the flexible band
    // moved from under the menu row to above it, and the row is "a fat chunky
    // row one" whose content sits at its foot — the anchors and the view bar
    // keep their authored height and their y within the lane, the lane itself
    // having moved down by the gap). The band is top_flex_gap_area, this
    // painter's the second of its two readers; it is zero-height wherever the
    // centering leaves no gap (the tablet), and the fill is one rectangle
    // from the window's top to the lane's foot so the two cannot show a seam.
    // The exposure gate at the paint order's step 3 tests the band too, so an
    // exposure inside the gap alone repaints this ground and nothing else.
    const GuiColor ground = redesign_row_ground(app);
    const GuiRect  gap    = top_flex_gap_area(app);
    cairo_set_source_rgb(cr, ground.r, ground.g, ground.b);
    cairo_rectangle(cr, row.x, gap.y, row.w, (row.y + row.h) - gap.y);
    cairo_fill(cr);

    // THE SHAPING CHOKEPOINT (text_shape.h): each label is MEASURED and PAINTED
    // from the one ShapedRun, so a button's width and its glyphs come from the
    // same positions and cannot disagree. Shaping a handful of glyphs per paint
    // is deliberate — the chokepoint's own comment defers caching to a profile,
    // and these are the cheapest runs there are.
    gui_select_font_face(cr, GuiFontFamily::Sans);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);

    const int pad    = scaled_px(kMenuLabelPadPx);
    const double rad = std::nearbyint(kMenuPillRadiusPx * gui_scale_factor());

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
        // No menu button has a selected face, and all are live during a
        // load and on a blank state — which is the whole reason this row paints
        // outside the audio branches. (The one state that DOES dead them is the
        // history view, which cannot be entered from either.) The stash is
        // written anyway
        // (through the one publisher) so the tick comparator's vector is total
        // over the roster with no membership test.
        AppState::RedesignButtonFace& face = publish_button_face(
            cr, app, audio, playback, target_render, def.id,
            GuiRect{x, row.y, btn_w, content_h});

        // A MENU BUTTON STAYS LIT WHILE ITS DROPDOWN IS UP (architect
        // 2026-08-02, kdenlive's own behaviour): the pill is what says "this menu
        // is the one that is open", so it paints on the popup's own anchor as
        // well as on hover — whichever of the five anchors emitted the open
        // popup, through the one anchor owner. It is also what keeps the button
        // from going dark the instant the menu appears — the open edge
        // deliberately UNHOVERS the whole roster (the pointer belongs to the
        // popup while it is up), so hover alone would drop the pill on exactly
        // the frame the dropdown arrives.
        //
        // A PAINT CONDITION, NOT A `selected` BIT. Two reasons, both structural:
        // redesign_button_selected is defined as the live fact a button's CHORD
        // flips, and no menu button has a chord at all (they are the
        // roster's two non-chord actions) — a dropdown is not a resting mode. And
        // the tick comparator that watches the selected bits would be pure
        // duplication here: the popup's two writers, toggle_dropdown and the one
        // close owner close_dropdown, ALREADY invalidate the top strip on both
        // edges, so the comparator could only ever re-notice a change that was
        // damaged at its source.
        //
        // THE TWO TERMS ARE NEVER BOTH TRUE, and that is the hover predicate's
        // doing rather than this expression's: no roster button hovers while a
        // dropdown is open (redesign_button_hoverable), so the anchor term is the
        // ONLY thing lighting the open menu's button, and the hover term is what
        // lights every row-1 button the rest of the time — including the anchor
        // itself once its menu is down. They are written as a disjunction because
        // row 1 has exactly two faces and both ask for the SAME pill: were they to
        // coincide, nothing would need to win.
        // THE ROW'S THIRD FACE, AND IT EXISTS ONLY IN THE `h` HISTORY VIEW
        // (architect 2026-08-04): while that view stands the SETTINGS ANCHOR is
        // dead — toggle_dropdown refuses that one menu — so it alone wears the
        // disabled face, FILE staying live with its menu (2026-08-13; the
        // Navigation anchor was the other live one from 2026-08-08 until its
        // deletion 2026-08-15). Built from the
        // row's own vocabulary rather than a new sample: row 1's whole ink is
        // its label, so the label retains kRedesignDisabledMix of itself over
        // the row ground, exactly as row 2's icon+label pair does. The partition
        // and its derivation are at history_mode_disables_button
        // (input_pointer.cpp); this reads only the published bit.
        //
        // NO PILL ON A DEAD ANCHOR, and the term is gated here rather than
        // trusted to be impossible: both of its inputs are structurally false in
        // the view (no roster button hovers under a disabled bit, and no popup
        // can be open at all), but a button can go dead UNDER a resting hover
        // with no pointer event to refresh it — row 2's outline carries the same
        // guard for the same frame.
        const double keep = face.enabled ? 1.0 : kRedesignDisabledMix;
        const bool pill = face.enabled &&
                          (face.hovered ||
                           (app.dropdown.open() &&
                            def.id == dropdown_anchor_button(app.dropdown.menu)));
        if (pill) {
            cairo_set_source_rgb(cr, kRedesignAccent.r, kRedesignAccent.g,
                                 kRedesignAccent.b);
            redesign_rounded_rect_path(cr, x, row.y,
                                       static_cast<double>(btn_w),
                                       static_cast<double>(content_h), rad);
            cairo_fill(cr);
        }

        // The label color is the SAME in the row's two live faces; the pill
        // under it is the whole hover cue. Dead, it is the one thing that dims
        // (keep above) — mixed toward the ROW GROUND, which is what is under it
        // there, since a dead button never wears the pill.
        const GuiColor label_c = mix_color(kRedesignLabel, ground, keep);
        cairo_set_source_rgb(cr, label_c.r, label_c.g, label_c.b);
        text_shape::show_shaped_run(
            cr, run, static_cast<double>(x + pad),
            redesign_baseline(font, static_cast<double>(row.y),
                              static_cast<double>(content_h)));

        x += btn_w;
    }

    // -- THE RIGHT FLOAT: the view bar ---------------------------------------
    //
    // The div is placed from its RIGHT edge, so all three widths are needed
    // before the first button can be drawn: the labels are shaped once here and
    // both measured and painted from those runs (the shaping chokepoint's rule —
    // one run is the single width truth), and the walk below re-uses them.
    {
        const int mar  = std::max(1, scaled_px(kViewBarBtnMarginPx));
        const int bord = std::max(1, scaled_px(kViewBarBtnBorderPx));
        const int bpad = scaled_px(kViewBarBtnPadPx);
        const int btn_y = row.y + mar;
        const int btn_h = content_h - 2 * mar;

        text_shape::ShapedRun runs[kViewBarButtonCount];
        int widths[kViewBarButtonCount];
        int div_w = 0;
        for (int i = 0; i < kViewBarButtonCount; ++i) {
            runs[i] = text_shape::shape_text_run(font, kViewBarButtons[i].label);
            widths[i] = 2 * (bord + bpad) +
                        static_cast<int>(std::nearbyint(runs[i].width_px));
            // Each button's own left and right margin — they do not collapse, so
            // the pair between two neighbours sums to 2px and the div's outer
            // pair is its 1px inset on each side. One term, applied per button,
            // spells the whole width.
            div_w += widths[i] + 2 * mar;
        }

        // FLUSH AT THE WINDOW'S RIGHT EDGE (no margin stated, so none exists).
        //
        // THERE IS STILL NO COLLISION RULE, and the measurement that used to
        // justify one has now changed three times — recorded rather than acted
        // on, since an overlap layout is the architect's to specify. RE-DERIVED
        // FOR THE TWO-BUTTON FLOAT from the face's own advance widths (Liberation
        // Sans at this row's 16px; the method reproduces the three numbers the
        // 2026-08-02 shaped-run walk recorded — Quit 29, Navigation 76,
        // Settings 58 — exactly, which is what makes File's 26 comparable to
        // them): the
        // left float was 124px at 100% (shaped labels File 26 + Settings 58, each
        // plus its two 10px pads — 46 + 78) and the div 183 — 307 of the 1920px
        // deployment width, 1613px of slack, and 333 of the 640px floor. AT 200%
        // IT WAS THE RECORDED THREE-BUTTON 439 LESS NAVIGATION'S OWN DOUBLED SLOT
        // (~192), i.e. ~247, and the exact pixel is deliberately left unstated:
        // the 2026-08-02 walk recorded only the TOTAL at that scale, and a
        // shaped run's width is not exactly twice its 100% value — which is why
        // that total reads 439 rather than 440. It did not matter which way
        // that one pixel fell: against the div's 366 the pair came to ~613 on
        // the 640px floor THAT DOES NOT SCALE, so the floats CLEARED IT BY ~26-27
        // and the overlap was gone outright rather than narrowed.
        //
        // THE CORNER IS RE-ENTERED SINCE 2026-08-20, and the old answer returns
        // with it. The EDIT ANCHOR adds a shaped "Edit" (~26px at 100%, the same
        // four-glyph order as File) plus its two 10px pads — ~46px at 100%, ~92
        // at 200% — so the three-button float is ~170 at 100% and ~339 at 200%.
        // At 100% nothing is close: 170 + 183 = 353 of the 640px floor. AT 200%
        // it is ~339 + 366 = ~705 ON THAT SAME 640 FLOOR, which OVERLAPS THE DIV
        // BY ~65px — narrower than the Navigation era's 165 and the same KIND of
        // corner. NO RULE IS BUILT FOR IT, deliberately and for the reason the
        // record below already gives: the painters answer it themselves.
        //
        // AND RE-ENTERED AGAIN ON 2026-08-27 BY THE SERIES ANCHOR, with the
        // same answer a third time. MEASURED by the same advance-width method
        // (which still reproduces the 2026-08-02 walk's Quit 29, Navigation 76
        // and Settings 58 exactly): "Series" shaped to 45px at 100%, the six
        // glyphs putting it between Edit's 28 and Settings' 58, so its slot was
        // 65px with its two 10px pads. The FOUR-button float was 237 at 100%
        // (46 + 48 + 65 + 78) and ~473 at 200%. At 100% it was still nowhere:
        // 237 + 183 = 420 of the 640px floor. At 200% it was ~473 + 366 = ~839
        // on that floor, an overlap of ~199px, which finally passed the
        // Navigation era's 165 — and it changed nothing, because the KIND of
        // corner is unchanged and the div still fills its background last and
        // covers the tail of the left float's labels.
        //
        // THE 2026-08-31 REBRAND WIDENED THAT ANCHOR AND THE ANSWER HELD A
        // FOURTH TIME: "Iterations" shapes to 66px by the same method (ten
        // glyphs, a hair under "Navigation"'s 76), so the slot is 86px and the
        // four-button float is 258 at 100% (46 + 48 + 86 + 78), ~516 at 200%.
        // At 100%: 258 + 183 = 441 of the 640px floor, still nowhere. At 200%:
        // ~516 + 366 = ~882, an overlap of ~242px — 43 more than the Series
        // era's and the same corner, answered the same way. THIS IS THE CHECK
        // THE REBRAND OWED, and it is why the record is kept as numbers: a
        // longer label is one term in a sum this note already spells.
        //
        // AND THE CORNER RECEDES ON 2026-09-04, the first move here that makes
        // it SMALLER: the architect deleted the ITERATIONS ANCHOR and sent its
        // two commands back to the icon row, so the float loses that 86px
        // slot outright. By the same advance-width method the three
        // application anchors come to 172 at 100% (46 + 48 + 78) and, with
        // HELP'S OWN 74px slot (a 54px shaped run plus its two pads, the one
        // term this note never carried — the five-anchor total is the view
        // bar's note above), the FOUR-button float is 246 at 100% and ~492 at
        // 200%. At 100%: 246 + 183 = 429 of the 640px floor, nowhere. At 200%:
        // ~492 + 366 = ~858 on that floor, an overlap of ~218px — 24 LESS than
        // the rebrand era's and the same corner, answered the same way. The
        // deletion is the first term this sum has ever lost, and it is worth
        // the line for exactly that reason: the record is a running one and it
        // subtracts as readily as it adds.
        // THE DEPLOYMENTS ARE NOT
        // NEAR IT: 1920 on the laptop at 100%, and the tablet's 2304 at its
        // own 225% is 1024 logical px against a ~429 logical-px float pair.
        // IT WAS REAL WHILE THE NAVIGATION
        // ANCHOR STOOD (2026-08-02..15): with its 96px slot the left float was
        // 220px at 100% and 439 at 200%, which OVERLAPPED the div by 165px on
        // the 640px floor at ceiling scale, where the pre-Navigation pair had
        // cleared it by 19. What happened there is what the painters already do:
        // the div fills its background last and covered the tail of the left
        // float's labels — nothing clickable, nothing else changed, and the
        // deployment geometry was nowhere near it. THAT IS THE ANSWER AGAIN, and
        // it is why this stayed a record rather than becoming a rule: the SHAPE
        // of the answer ("the div covers the tail") was kept precisely for the
        // third button this row has now, and it needed no code when it arrived.
        // NEITHER DEPLOYMENT IS ANYWHERE NEAR IT — 1920 on the laptop, 1024 on
        // the rig, both at 100% — so the overlap is a ceiling-scale, floor-width
        // corner and nothing a real window reaches.
        // Defensive only, and it takes the whole float: a lane so short that the
        // two margins eat the button leaves nothing to paint and nothing to
        // click, which is the same early-out the row's own content_h guard above
        // makes. Unreachable at any schema-legal gui_scale (content_h floors at
        // 5 and the margins at 1 each).
        if (div_w <= 0 || btn_h <= 0) { cairo_restore(cr); return; }

        const int div_x = row.x + row.w - div_w;
        // IT IS THE BAR'S OWN VERDICT (view_bar_focused, app_state.h): the
        // header's answer AND no folder overlay standing, under which this
        // row shows above the band with these three dead. The bar carries
        // that second term ALONE since 2026-09-03 evening — the header went
        // back to the activation flag when the File anchor went live on it,
        // and these three have no dimmed face of their own, so the ground
        // swap is the only thing that can show their grey (both rulings and
        // the succession are at the owner). ONE local, read by the ground
        // here and by view_bar_face's `focused` term below, so the pressed
        // interior reads the same fact.
        const bool bar_focused = view_bar_focused(app);
        const GuiColor bar_bg = bar_focused
                                    ? kRedesignViewBarBg
                                    : kRedesignViewBarBgUnfocused;
        cairo_set_source_rgb(cr, bar_bg.r, bar_bg.g, bar_bg.b);
        cairo_rectangle(cr, div_x, row.y, div_w, content_h);
        cairo_fill(cr);

        const double bar_rad = std::nearbyint(kViewBarRadiusPx *
                                              gui_scale_factor());
        int vx = div_x;
        for (int i = 0; i < kViewBarButtonCount; ++i) {
            vx += mar;
            const int btn_w = widths[i];

            AppState::RedesignButtonFace& face = publish_button_face(
                cr, app, audio, playback, target_render,
                kViewBarButtons[i].id, GuiRect{vx, btn_y, btn_w, btn_h});

            const bool pressed =
                redesign_button_pressed_face(app, kViewBarButtons[i].id);
            const ViewBarFace f =
                view_bar_face(bar_bg, bar_focused, face.hovered,
                              face.selected, pressed);

            if (f.filled || f.framed) {
                // The shared face box (redesign_face_box — the half-stroke
                // inset rule lives there). The REST face paints no fill at all:
                // its color IS the div's background, already under it, which is
                // why the crop's resting button is invisible.
                redesign_face_box(cr, vx, btn_y, btn_w, btn_h, bord, bar_rad,
                                  f.filled ? &f.fill  : nullptr,
                                  f.framed ? &f.frame : nullptr);
            }

            // The label is kRedesignLabel in EVERY state, focused and unfocused
            // — the nine crops agree — and sits at the box's own padding origin,
            // which is the same place the width was measured from.
            cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                                 kRedesignLabel.b);
            text_shape::show_shaped_run(
                cr, runs[i], static_cast<double>(vx + bord + bpad),
                redesign_baseline(font, static_cast<double>(btn_y),
                                  static_cast<double>(btn_h)));

            vx += btn_w + mar;
        }
    }

    cairo_restore(cr);
}

void GuiPaintHandler::paint_tab_row(cairo_t* cr) {
    // THE TAB ROW (top lane 2, row 3 of the redesign): the Breeze tab bar for
    // the A/B navigational tabs — "Tab A" and "Tab B", flush at the left edge,
    // over the CONTENT GROUND #202326 (Breeze's standard bar, matching the pane
    // it opens into; the crops, the ruling that briefly darkened it and that
    // ruling's withdrawal are all at render.h's row-3 block), between TWO
    // border rows: a 1px #535659 line across the whole window width at the
    // lane's TOP, and a 1px #4c4e51 line at its BOTTOM that BREAKS under the
    // selected tab.
    //
    // THE SELECTED TAB IS app.active_tab_view, read live every paint. Its face
    // is a 3px accent trim with rounded top corners flanked by 1px side
    // borders, over the bar's own ground — the tab reads as an OPENING into
    // the icon row rather than as a filled shape, and it lays no interior of
    // its own because the bar's fill already IS that interior (one surface,
    // one fill; the explicit path fill this branch carried for the few hours
    // the ground was darker went with the ground). An INACTIVE tab is a flat
    // fill, rest #1b1d20 or hover, RECESSED against the bar — Breeze's model,
    // where the bar matches the pane and the unselected tab sits below both.
    // There is no selected-hover face and no click face anywhere in this row
    // (a tab press is a chord, never a refusal), and this row has NO disabled
    // face at all.
    //
    // THE ROW HAS ONE MEANING IN EVERY STATE SINCE 2026-08-18: it is the A/B
    // tabs, in the `h` history view exactly as outside it. From 2026-08-05 the
    // view REPURPOSED the surface as its WALK SELECTOR — the labels reading
    // "Remote" and "Local", the selected face marking the live walk rather than
    // the live tab, a press routed to set_history_delta by a band claim of
    // the mode's own — and the walk has its own radio pair in the icon row now,
    // so the label override, the selected-face arm, the tooltip silence and
    // that band claim are all deleted. Nothing in this painter forks on the
    // mode any more. THE LOCK SLOTS ARE GONE WHOLE and for their own reason
    // (2026-08-14): no padlock drawn, no rect published, no width reserved, the
    // padlock being the icon row's own button.
    //
    // THE ROW IS TWO SLOTS AGAIN (architect 2026-08-08). It carried the (walk
    // source, reading) product for one day — four self-labelled tabs on
    // 2026-08-07, then two labelled groups with the reading in a text block over
    // each pair — and the architect retired the whole shape: the READING is row
    // 4's Cumulative toggle now (bare `u`, a mode bit), so this row selects ONE
    // axis and needs neither the extra pair nor the headings. The compare-only
    // membership flag, the empty-rect publication it required and the text-block
    // painter are deleted whole.
    //
    const GuiRect lane = top_tab_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;

    // THE LANE IS THREE BANDS: a border row, the content band every tab box
    // fills, and a second border row. `content_y` is the band's top and the
    // origin EVERYTHING on this row anchors to — the tabs, their published hit
    // rects, the lock slot and the labels' baseline — so the top border is a
    // row nothing else can reach.
    const int border_h  = tab_row_border_h_px();
    const int content_h = lane.h - 2 * border_h;
    if (content_h <= 0) return;
    const int content_y = lane.y + border_h;

    cairo_save(cr);

    // THE BAR, over the WHOLE lane, both border rows included: the CONTENT
    // GROUND, the surface the selected tab opens into. It is that tab's
    // interior too — the crops read bar and pane as one value (render.h's
    // row-3 block carries the measurement) — which is why the selected branch
    // below lays no fill of its own: where the bottom border BREAKS under that
    // tab, what shows through is this fill, and the opening leads into the
    // icon row. It covers both border rows deliberately: the top one is
    // overwritten immediately below, and the bottom one is meant to show this
    // colour exactly where the border breaks — one rectangle, the row's only
    // ground expression.
    cairo_set_source_rgb(cr, kRedesignContentGround.r,
                         kRedesignContentGround.g,
                         kRedesignContentGround.b);
    cairo_rectangle(cr, lane.x, lane.y, lane.w, lane.h);
    cairo_fill(cr);

    // THE BORDER-TOP, full window width at the lane's FIRST row and UNBROKEN —
    // over the trough and over every tab alike (2026-08-13). WHAT THE CROPS
    // SHOW is exactly that: row_3_tab_example's y=0 is one flat run across all
    // 281 px with two unselected tabs beginning at y=1 beneath it, and
    // row_3_tab_trough agrees on the empty bar. WHAT THEY DO NOT SHOW is a
    // SELECTED tab under this line — row_3_tab_selected starts AT the tab's
    // own top row, so whether a selected tab breaks the top line the way it
    // breaks the bottom one is UNANSWERED by the crops and is not guessed at
    // here. The line is drawn straight across and the selected tab's rounded
    // accent begins on the first content row below it, which is the reading
    // the crops do support: a tab top that starts below the line, not through
    // it. Its grey is the BOTTOM border's since 2026-08-14 — the crop's own
    // #535659 is overridden for product-internal consistency, the ruling and
    // the measurement both at kRedesignTabLine, render.h.
    cairo_set_source_rgb(cr, kRedesignTabLine.r, kRedesignTabLine.g,
                         kRedesignTabLine.b);
    cairo_rectangle(cr, lane.x, lane.y, lane.w, border_h);
    cairo_fill(cr);

    gui_select_font_face(cr, GuiFontFamily::Sans);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);

    // (THE STATUS CHAIN PAINTED HERE, on this bar and under the tabs, from
    // 2026-08-13 until 2026-08-29, when its three STATE strings took the
    // STATUS BAR on the window's foot. The collision rule it needed — the
    // chain first, the tab walk over it, text pushed under a tab accepted —
    // has no subject left: the row paints tabs across its whole width.)

    const int pad      = scaled_px(kTabLabelPadPx);
    const int min_w    = scaled_px(kTabMinWidthPx);
    const int trim_h   = std::max(1, scaled_px(kTabTrimHeightPx));
    const int line_w   = std::max(1, scaled_px(kTabBorderPx));
    const double radius = std::nearbyint(kTabCornerRadiusPx * gui_scale_factor());

    // The selected tab's span, recorded during the walk so the border below can
    // break under it. Zero width means "no tab is selected here", which cannot
    // happen with active_tab_view always 'A' or 'B' but costs one int to state.
    int sel_x = 0, sel_w = 0;

    // THE WALK: tabs flush from the lane's left edge, adjacent, margin zero. A
    // tab's width is the LARGER of its two paddings around the shaped label and
    // the minimum — and nothing else, so it is identical selected or not (the
    // side borders draw inside the box). With the A/B labels the minimum is what
    // binds, which makes both tabs exactly the same width and the row regular by
    // construction — and nothing in this walk ASSUMES that, deliberately: the
    // history view's walk words sized each tab by its own shaped run until
    // 2026-08-18 ("Remote" clearing the minimum by 18 px at 100% while "Local"
    // sat at it), which is what a label-sized tab bar does, so the measure stays
    // the general one.
    int x = lane.x;
    for (const TabDef& def : kTabs) {
        // THE LABEL IS THIS TABLE'S OWN, with no override left to ask
        // (2026-08-18): the tabs say "A" and "B" in every state now that the
        // walk selector has its own radio pair in the icon row.
        const text_shape::ShapedRun run = text_shape::shape_text_run(
            font, def.label);
        const int label_w = static_cast<int>(std::nearbyint(run.width_px));
        // THE TAB IS ITS FIELD, in every state since 2026-08-14: the shaped
        // label auto-sizes it against the minimum, and with "A" and "B" the
        // minimum is what binds. (It was field + the padlock's
        // reserved slot on the A/B pair from 2026-08-01 until the padlock
        // moved into the icon row; the walk selector, which is what made the
        // auto-size visible at all until 2026-08-18, never carried the slot.)
        const int field_w = std::max(min_w, label_w + 2 * pad);
        const int tab_w   = field_w;

        // THE STASH IS WHAT THE DRIFT COMPARATOR READS (main.cpp's per-tick
        // enabled/selected sweep), so publishing `selected` is load-bearing,
        // not bookkeeping: leave it at its default and the live active-tab
        // compare disagrees with the stash on the selected tab EVERY pass,
        // which invalidates the whole top strip at tick cadence forever. The
        // one publisher writes it from redesign_button_selected — the roster
        // predicate's own active_tab_view compare, so the painted face below
        // reads THE SAME fact the comparator replays, with no second spelling.
        AppState::RedesignButtonFace& face = publish_button_face(
            cr, app, audio, playback, target_render, def.id,
            GuiRect{x, content_y, tab_w, content_h});
        const bool selected = face.selected;

        // THE ROW HAS NO DISABLED FACE AGAIN (2026-08-05). It grew one on
        // 2026-08-04 for the `h` history view, which greyed both tabs because
        // their chord was consumed; the architect then made the view REPURPOSE
        // the pair as the walk selector, so the tabs went live in the one state
        // that ever dimmed them, and since 2026-08-18 they are live in there as
        // ORDINARY TABS — Ctrl+Tab is on the mode's allowlist, so the derived
        // partition answers live and redesign_button_enabled answers true for
        // them everywhere. The dim machinery went with
        // its producer rather than sitting here unreachable; the product's one
        // disabled blend is unchanged and still the rule on row 4.
        const bool hovered = face.hovered;
        // The face this tab wears — the fill it paints and the ground every ink
        // below sits on. THE SELECTED TAB PAINTS NO FILL AT ALL: its interior
        // is the bar's own ground, already laid, so the name here is the ground
        // its trim, borders, label and lock resolve against rather than a
        // colour this branch writes.
        const GuiColor tab_face =
            selected ? kRedesignContentGround
                     : (hovered ? kRedesignTabHover : kRedesignTabRest);

        if (selected) {
            sel_x = x;
            sel_w = tab_w;

            // NO INTERIOR FILL, and that is the ground's doing rather than an
            // omission: the bar and the surface the tab opens into are ONE
            // value in the crops, so the lane fill above has already painted
            // this tab's interior — down through the bottom border row, which
            // is what the BREAK below exposes. (The explicit rounded-path fill
            // this branch carried for the few hours the row's ground was the
            // resting tab's #1b1d20 is deleted with that ruling: over the
            // restored ground it wrote the same constant over the same pixels,
            // and keeping it would state as two facts what the crops measure
            // as one.)
            //
            // THE OPEN ROUNDED-TOP PATH, TWO CLIPPED USES, and these two clips are
            // complementary — so neither writes the other's pixels and the two
            // halves cannot describe different tabs. FILLED under a clip
            // to the top trim band it is the
            // 3px blue top, whose only antialiasing is the two corner arcs;
            // STROKED under a clip to everything below that band it is the 1px
            // side borders, picking those same arcs up where the blue leaves off
            // and running vertical to the lane's last content row. BOTH anchor
            // at content_y, so the tab begins on the first row BELOW the
            // border-top and never writes into it.
            cairo_save(cr);
            cairo_rectangle(cr, x, content_y, tab_w, trim_h);
            cairo_clip(cr);
            redesign_rounded_top_rect_path(cr, x, content_y,
                                           static_cast<double>(tab_w),
                                           static_cast<double>(content_h),
                                           radius);
            cairo_set_source_rgb(cr, kRedesignAccent.r, kRedesignAccent.g,
                                 kRedesignAccent.b);
            cairo_fill(cr);
            cairo_restore(cr);

            cairo_save(cr);
            cairo_rectangle(cr, x, content_y + trim_h, tab_w,
                            content_h - trim_h);
            cairo_clip(cr);
            {
                // THE STROKE GEOMETRY, in one expression per axis:
                //  - inset by HALF the stroke width on the left, right and top
                //    (the half-stroke inset rule — its full statement lives at
                //    redesign_face_box, whose CLOSED box this deliberately does
                //    not call: the tab's path is open at the bottom);
                //  - the radius inset by the SAME half, which keeps the arc
                //    CONCENTRIC with the filled trim's arc above (both centered
                //    on x+radius, content_y+radius) so the border picks the blue
                //    up exactly where it ends;
                //  - and the height run to the content band's LAST ROW rather
                //    than inset, because the path has no bottom edge to align:
                //    a butt-capped vertical ending at content_y+content_h covers
                //    every row down to the border, which is what the crop shows.
                const double half = static_cast<double>(line_w) * 0.5;
                cairo_set_line_width(cr, static_cast<double>(line_w));
                cairo_set_source_rgb(cr, kRedesignTabLine.r, kRedesignTabLine.g,
                                     kRedesignTabLine.b);
                redesign_rounded_top_rect_path(
                    cr, x + half, content_y + half,
                    static_cast<double>(tab_w - line_w),
                    static_cast<double>(content_h) - half,
                    radius - half);
                cairo_stroke(cr);
            }
            cairo_restore(cr);
        } else {
            // The inactive tab: a flat fill, square corners, no borders, sitting
            // RECESSED against the bar (Breeze's model). Hovered
            // it takes the lighter blue-grey PLUS a 1px edge across its own
            // bottom row — the hover face recolors that row, which is the crop's
            // whole difference from rest.
            cairo_set_source_rgb(cr, tab_face.r, tab_face.g, tab_face.b);
            cairo_rectangle(cr, x, content_y, tab_w, content_h);
            cairo_fill(cr);
            if (hovered && content_h > line_w) {
                cairo_set_source_rgb(cr, kRedesignTabHoverEdge.r,
                                     kRedesignTabHoverEdge.g,
                                     kRedesignTabHoverEdge.b);
                cairo_rectangle(cr, x, content_y + content_h - line_w,
                                tab_w, line_w);
                cairo_fill(cr);
            }
        }

        // The label is the SAME white in every state, CENTERED on both axes:
        // horizontally in the tab's FIELD (the padding is the width FLOOR's
        // term, not an anchor — at the minimum width a left-padded label would
        // hug the border instead of sitting in the middle; the field is the
        // whole tab since the lock slot left), vertically by the shared
        // extents-solved baseline. Rounded to the pixel grid like every other
        // integer-domain conversion, so the glyphs stay crisp; the halving makes
        // a 1px bias unavoidable at odd leftovers and nearbyint's banker's
        // rounding is the project's one answer for that.
        cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                             kRedesignLabel.b);
        text_shape::show_shaped_run(
            cr, run,
            static_cast<double>(x) +
                std::nearbyint((static_cast<double>(field_w) - run.width_px) *
                               0.5),
            redesign_baseline(font, static_cast<double>(content_y),
                              static_cast<double>(content_h)));

        // (THE LOCK WAS DRAWN HERE, last, over whatever face the tab wore:
        // both tabs in both states, the closed padlock at full kIconText white
        // when read-only and the OPEN padlock dimmed by kRedesignDisabledMix
        // when writable, with only the ACTIVE tab's rect published for the
        // press. It left this row on 2026-08-14 for the icon row's own
        // read-only button; the two-glyph face came with it, the DIM did not —
        // in that row a dimmed glyph means disabled, so the button says its
        // state with the row's own lit fill instead. The slot's record is at
        // the retired constants above.)

        x += tab_w;
    }

    // THE BORDER-BOTTOM, full window width at the lane's last row — EXCEPT under
    // the selected tab, where it BREAKS because that tab opens into the content
    // below, and the pixels it leaves alone are that tab's own interior, which
    // the lane's ground fill already laid. Two pixel-bound rectangle fills (left
    // of the tab, right of it),
    // either of which is empty when the selected tab sits at an edge; crisp on
    // integer bounds like every other axis-aligned 1px fill in these rows. ITS
    // GREY IS THE BORDER-TOP'S TOO since 2026-08-14 — one line value on the
    // lane, the ruling and the overridden crop at kRedesignTabLine, render.h.
    cairo_set_source_rgb(cr, kRedesignTabLine.r, kRedesignTabLine.g,
                         kRedesignTabLine.b);
    const int border_y = content_y + content_h;
    if (sel_w <= 0) {
        cairo_rectangle(cr, lane.x, border_y, lane.w, border_h);
        cairo_fill(cr);
    } else {
        if (sel_x > lane.x) {
            cairo_rectangle(cr, lane.x, border_y, sel_x - lane.x, border_h);
            cairo_fill(cr);
        }
        const int right_x = sel_x + sel_w;
        if (right_x < lane.x + lane.w) {
            cairo_rectangle(cr, right_x, border_y,
                            lane.x + lane.w - right_x, border_h);
            cairo_fill(cr);
        }
    }

    cairo_restore(cr);
}

// -- history_walk_line ----------------------------------------------------
//
// THE `h` HISTORY MODE'S ONE LINE, composed for ROW 8'S STATE CELL — the
// clock's neighbour since 2026-08-29's fold, the one-day status bar's LEFT
// cell before that. While the mode stands this line is what that cell is for.
//
// THE SHAPE: the commit's position in the walk and its short SHA, then the
// scale — `Scale: [-]<then token> [+]<now token>`, in the lane's own sign
// vocabulary and through the lane's own spelling owner (history_diff_label),
// so this line and the flags cannot come to bracket differently. (The
// "bottom-left corner" of the mode's record read bottom-RIGHT from the
// 2026-08-12 unification, TOP-RIGHT while the status chain sat in the tab row,
// bottom-LEFT on the one-day status bar, and reads BOTTOM-CENTRE now, beside
// the clock — the same line, four surfaces.)
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
// THE POSITION IS THE ACTIVE WALK'S (2026-08-07): `n/N` reads walk_index /
// walk_count, so a Local tab counts the session's own timeline states with the
// same two numbers in the same place. THE SHA IS THE COMMIT WALK'S ALONE, and
// deliberately named rather than routed through an accessor: an undo entry has
// no commit, so the token simply does not appear on the Local tab — the
// empty-sha test below is that fact rather than a second branch.
//
// The reference is non-const because both walks COMPUTE THEIR DELTA LAZILY AND
// CACHE IT (displayed_delta, app_state.h) — the composition itself writes
// nothing.
static std::string history_walk_line(AppState& app) {
    std::string line;
    const std::size_t count = app.history_mode.walk_count();
    // AN EMPTY WALK READS `0/0` (2026-08-07), where it used to read nothing at
    // all: a blank cell beside a blank lane says only that the cell has stopped
    // working. ONE SPELLING FOR BOTH WALKS, and since 2026-08-08 only the
    // COMMIT walk can reach it — its empty window, a visit opened before the
    // prefetch has delivered member 0. The LOCAL walk is never empty while the
    // mode stands: the one entry owner binds it before `active` goes up and its
    // member count is U + R + 1, so a session that has authored nothing reads
    // `1/1` — one state compared against itself. The zero arm therefore
    // describes the commit side alone now, and stays one expression because the
    // arithmetic below is the walk-agnostic one.
    line += std::to_string(count == 0 ? 0 : app.history_mode.walk_index() + 1);
    line += '/';
    line += std::to_string(count);
    if (app.history_mode.source == GuiHistoryWalkSource::Commit) {
        // THE ONE SPELLING OWNER (member_label, app_state.h): the short SHA the
        // `'` load confirmation names too. Empty for an out-of-range index,
        // which is exactly the empty walk, so the count needs no test of its
        // own here.
        const std::string label =
            app.history_mode.member_label(app.history_mode.index);
        if (!label.empty()) {
            line += ' ';
            line += label;
        }
    }
    const GuiHistoryCommitDelta* d =
        app.history_mode.displayed_delta(app.history_compare());
    // No unavailable-delta arm: walk membership is the strict whole-set load
    // (history_diff.h's gate, 2026-08-04), so every commit this line can name
    // has a real delta — the old `Ambiguous` token died with the display
    // machinery it named.
    if (d && d->scale_changed) {
        // The position always precedes it now (`0/0` at worst), so the
        // separator is unconditional.
        line += ' ';
        line += "Scale: ";
        line += history_diff_label("[-]", /*disabled=*/false,
                                   d->then_scale_token);
        line += ' ';
        line += history_diff_label("[+]", /*disabled=*/false,
                                   d->now_scale_token);
    }
    return line;
}

// -- process_line_text ------------------------------------------------------
//
// Row 8's process line, composed at the reader rather than stored, and the one
// site that knows the mirror has a line at all. The cell carries what is true
// right now, so the two facts it can report are asked in the order in which
// they outrank one another: a render, a batch cell or the startup load has
// written its own string into the shared slot, and that string wins for as
// long as it stands; otherwise a synchronization to external storage is
// running and the cell says so; otherwise there is nothing to report.
//
// The mirror's line is derived here because a written one could not stay true.
// It was written at the dispatch into an empty cell and cleared at the
// completion while the cell still held its own string (architect 2026-09-04,
// the yield), which is right for the two orderings that ruling was reasoned on
// and wrong for the other two. A render started after the mirror took the cell
// and then cleared it at its own end, leaving the cell blank with the copying
// still going; and a mirror started while a render's line stood never wrote at
// all, so nothing appeared when that line went. Everything involved runs on the
// GUI thread, so it was a precedence gap and not a race. Deriving closes both:
// the render's line wins exactly while it stands and the mirror's is back the
// moment it goes, with no stack, no priority enum, and no second writer for the
// slot's owners to disagree with.
//
// The `h` walk line's precedence over the whole answer is unchanged and is
// decided by the caller, which asks the mode first; this composes the
// process-line operand alone.
//
// Damage stays the changing route's own business and no route gained or lost
// one: the render side's writes and its ownership-tested clears already damage
// the cell (input_render_dispatch and target_render, the clears invalidating
// while the longer string is still the one this would return), and the mirror's
// dispatch and completion invalidate without touching the string at all.
//
// The bit asked here is the act's own single-in-flight bit, which stays up
// through the brief CompletionPending window between the worker signalling and
// the GUI thread consuming the eventfd — so the line can outlive the last file
// copy by a frame or two. That is the same bit and the same window the quit
// gate refuses on (close_refused_by_external_sync), and the two saying the same
// thing at the same instant is worth more here than a line that goes out early.
//
// The mirror's spelling wears the three ASCII periods both its neighbours wear
// (`Loading...`, `Updating...`): the cell is the product's monospace surface
// and the ASCII-in-grammars rule governs the spelling. The constant has one
// reader, the body just below.
constexpr const char* kSyncProgressLine = "Synchronizing...";

static std::string process_line_text(const AppState& app,
                                     const GuiExternalSyncWorker& sync) {
    if (!app.queue_progress_text.empty()) return app.queue_progress_text;
    if (sync.is_busy()) return kSyncProgressLine;
    return std::string();
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
// READOUT retired whole — bare `j` copies the value and Shift+`j` goes to the
// marker it came from. `history_walk_line` above is untouched: the composer
// still stands beside its one caller, which is now that cell.)

void GuiPaintHandler::paint_icon_row(cairo_t* cr) {
    // THE ICON ROW (top lane 2, row 4 of the redesign): the same #202326
    // content ground the SELECTED TAB above opens into — and that the tab row's
    // own bar paints, the three surfaces being one value by measurement — under
    // a 1px border-bottom across the WHOLE window
    // width, separator-divided groups of 32x32 buttons — TWENTY-EIGHT members
    // in SIX groups since 2026-09-04, RE-COUNTED off the roster enum and the
    // divider owner rather than adjusted: the toolbar four (Save / Undo /
    // Redo / Render, the deleted row 2's, leading the row), THE TWO VIEW LAMPS
    // sharing one group since the second of that day's rulings deleted the
    // divider the radio-pair collapse had left standing between them, THE ZOOM
    // GROUP — the VIEWPORT CLASS whole since the
    // architect's 2026-08-27 merge: the Show trim region button leading (it
    // had a group of its own from 2026-08-11, when the scissors opened it,
    // until that ruling), then the zoom four (2026-08-12), the waveform
    // magnification pair (2026-08-26), FOLLOW, which came in from the
    // dissolved mass-marker group the same day, THE CENTERED LAMP beside
    // it (2026-08-31, R11), THE CENTER ON NEXT MARKER LAMP and THE RESTRICT
    // UNDO TO VIEWPORT LAMP closing the group
    // (both 2026-09-04, the second arriving from the toolbar group later that
    // day because it is a viewport gesture too) — THE ITERATION PAIR (the BPM
    // opener and grid iteration mode, back from the deleted menu row later
    // that same day) — the RENDER-ENTRY group
    // (listen and the READ-ONLY toggle, the architect's own order on
    // 2026-08-14 less the load-in-place, which left for the history group on
    // 2026-09-01) and THE HISTORY GROUP, the opener leading THE WALK LAMP,
    // its four companions (2026-08-18, the companions back from the bottom
    // row and the walk surface down from row 3 later that day) and THE LOAD
    // IN PLACE at the tail.
    //
    // NOTHING HERE IS EVER HIDDEN (architect 2026-08-14, "no more
    // hiding/showing icons in top icon row"): all twenty-eight paint on every
    // frame and what a mode refuses wears the DEAD FACE. The mode-collapsing
    // roster of 2026-08-12 — which skipped members and published zero rects for
    // them, over the four history mode-companions at rest and the wholly
    // consumed groups right of the history opener in the view — is deleted
    // whole, with the sep-owed state machine that served it; the walk below is
    // a plain left-to-right accumulation again. IT IS WHAT MAKES THE
    // COMPANIONS' RESTING GREY NECESSARY AGAIN (2026-08-18): they are painted
    // out here now, so a live face would promise a chord that is bound only
    // inside the `h` view (the arm and its succession are at
    // redesign_button_enabled, app_state.h).
    //
    // THE WIDTH MATH at 100%, RE-DERIVED from the roster after each move (8px
    // lead-in + 32px boxes + 2px gaps + 4+1+4 separator slots; the count of
    // drawn separators is groups minus one, and the count of gaps is buttons
    // minus groups):
    //   8 + 28·32 + (28−6)·2 + (6−1)·9 = 8 + 896 + 44 + 45 = 993px,
    // IN EVERY STATE — the row has one width, inside the `h` view as
    // outside it. Add the 8px trailing pad and the row's ink ends at 1001.
    //
    // THE ITERATIONS DELETION'S ARITHMETIC (architect 2026-09-04, the day's
    // fourth row ruling and its only ADDITION): the row gained TWO BOXES, ONE
    // GAP and ONE SEPARATOR — +64, +2 and +9 off the 918 the regrouping had
    // left it — so it stands at 993 authored px. THE TABLET FIT CEILING GOES
    // 250 → 232: the walk fits while 993·factor ≤ 2304, 993·2.32 = 2303.8, and
    // 233 overruns by ten device px. The tablet's first-run scale is
    // 225 and the walk still clears it: 993·2.25 = 2234 against the panel's
    // 2304, 70 device px of slack where the 918-px row had 238 — and 993 of
    // the panel's 1024 logical px at that scale, which is the narrowest margin
    // this row has run at on that panel and the number to watch if anything
    // else joins it.
    //
    // THE 918 IT GREW ON was the day's third ruling, the REGROUPING: the row
    // lost ONE SEPARATOR and gained ONE GAP, −9 and +2 off the 925 the
    // restrict-undo lamp had left that morning. No box moved either way — the
    // two view lamps merged into one group and the restrict-undo lamp changed
    // groups, and a member changing groups is free. Its ceiling was 250
    // (918·2.50 = 2295, 251 overrunning by ~1 device px) and the tablet's 225
    // left it 238 px of slack.
    //
    // THE 925 IT CAME OFF was the RESTRICT UNDO TO VIEWPORT lamp's own, earlier
    // on 2026-09-04: one box and one gap (+34) onto the 891 the Center on Next
    // Marker lamp had left hours before it, which itself was +34 onto the 857
    // the RADIO-PAIR COLLAPSE left. THAT COLLAPSE IS THE WIDEST SINGLE CUT THE
    // ROW HAS TAKEN (959 to 857): three boxes and three gaps (−102) off the 959
    // the centered lamp left, no separator moving, the two view groups going
    // from two members to one each — and it was taken to make room for the
    // lamps that landed after it, which is exactly what the day then spent it
    // on. The ceilings walked 240 → 268 → 258 → 249 → 250 with those four
    // moves.
    //
    // THE 959 IT SHRANK FROM WAS THE CENTERED LAMP'S (2026-08-31, R11): one box
    // and one gap (+34) onto the 925 the Series relocation left, which had
    // moved the fit ceiling from 249 to 240 (959·2.40 = 2302, 241 clipping by
    // ~3 authored px) and left the tablet's 225 with 146 px of slack.
    //
    // THE 925 IT GROWS ON WAS THE 2026-08-27 SERIES RELOCATION'S ARITHMETIC,
    // the biggest single cut this row had taken before the radio-pair
    // collapse's 102: two boxes (−68) and two
    // separators (−18) off the 1007/1015 it stood at that morning, 82px
    // narrower authored. WHAT IT BOUGHT IS SCALE HEADROOM ON THE TABLET,
    // which is the measurement that matters now that the deployment panel is
    // 2304px (the gui_scale ceiling went to 400 on 2026-08-26 for exactly
    // this panel): at 925 the row landed whole up to gui_scale 249
    // (925·2.49 = 2303) — where the 1041-px roster of 2026-08-26 fitted only
    // to 220, clipping HistoryNewer by 17 authored px at the 225 the tablet
    // runs. (250 was the template for one afternoon on
    // 2026-08-27, one step past that roster's fit ceiling — 925·2.5 = 2312, so
    // the rightmost history icon lost its last ~3 authored px (~8 device px)
    // under the crop-at-the-floor allowance at kMinWindowWidthPx, a sanctioned
    // casualty rather than a new rule — and the architect stepped it back to
    // 225 that evening for exactly that crop.)
    // (Counting the trailing pad the ceiling is 230 rather than 232 — 1001 is
    // the ink plus the pad, 1001·2.30 = 2302 fits and 231 overruns — but the
    // pad is ground, not ink, so the icons themselves are the thing measured.)
    // The laptop clears it outright at 1001 of 1920; the retired Pi panel's
    // 1024 at 100% would have cleared it by 23.
    //
    // (It was 918px at twenty-six in five groups for the hours of 2026-09-04
    // between the regrouping and the Iterations deletion; 925px at twenty-six
    // in six groups earlier that day,
    // between the restrict-undo lamp landing in the toolbar group and the
    // regrouping that moved it; 891px at twenty-five in six groups earlier that
    // day, from the Center on Next Marker lamp; 857px at twenty-four in six
    // groups for the few hours before it, from the radio-pair collapse;
    // 959px at twenty-seven in six groups from 2026-08-31; 925px at
    // twenty-six in six groups from 2026-08-27 — the same width as this day's
    // own 925 and the same fit ceiling, reached from the other direction;
    // 1007px at
    // twenty-eight in eight groups from 2026-08-26; 939px at
    // twenty-six earlier on 2026-08-18, before the WALK RADIOS landed; 973 at
    // twenty-seven from 2026-08-16; and 939 at twenty-six before that — the
    // 2026-08-16 and the relayout rosters matching by coincidence rather than
    // by symmetry, the earlier one being this one with the four verbs in, the
    // four companions out and no radios.
    // THE MARGIN IS THE
    // THING TO WATCH on this row: every further member costs 34px and a NEW
    // GROUP costs 41, which at the tablet's 225% is ~77 and ~92 device px
    // against its panel — and after the Iterations deletion there are 70 of
    // those device px left at 225%, so the NEXT addition is the one that
    // crops.)
    //
    // NO FOCUS SWAP HERE: this ground already IS the unfocused shade row 1
    // darkens to, so there is nothing for it to change to (redesign_row_ground
    // is deliberately not called).
    //
    // FIVE FACES — the architect supplied exactly these:
    //   REST          — the bare glyph on the row ground, no chrome.
    //   HOVER         — a 1px accent rounded OUTLINE. THE RULED READING is that
    //                   hover IS the outline, applied over WHICHEVER fill the
    //                   button has: the selectedhover crop is the accent outline
    //                   over the selected fill, unchanged otherwise.
    //   CLICK         — the interior filled with the row ground tinted 30%
    //                   toward the accent (kRedesignClickMix), under the
    //                   accent outline.
    //   SELECTED      — kRedesignSelectedFill under a 1px kRedesignLine outline,
    //                   persistent, reading the live fact its chord flips
    //                   (redesign_button_selected).
    //   SELECTED+HOVER— the selected fill under the ACCENT outline.
    // SELECTED + CLICK was not supplied, and THE CLICK FILL WINS while held: a
    // press is transient and its feedback should be the same wherever it lands,
    // so the pressed tint replaces the selected fill for exactly the hold and
    // the selected fill returns at the release.
    // THE DISABLED FACE IS THE TWO MODES' AND THE TOOLBAR MIGRANTS':
    // the row's own members never grey for a REFUSAL —
    // presses always dispatch and the CHORDS' OWN refusals answer (loading
    // blocks everything, and each arm keeps its home-view, empty-selection and
    // occupied-frame guards), inherited through on_key rather than mirrored —
    // while the toolbar four
    // BROUGHT their real disabled derivations with them at the 2026-08-12
    // relayout (Undo/Redo's locked-tab and empty-stack terms, Save's
    // in-flight lockout, Render's source path — redesign_button_enabled's
    // own arms, painted by this body's generic keep-mix with nothing added
    // here). THE TWO RULED EXCEPTIONS ARE BOTH MODES rather than refusals,
    // which is what the per-press refusals above cannot express: the `h`
    // HISTORY VIEW greys every button in this row whose act it consumes
    // (architect 2026-08-04, at the face code below), and since 2026-08-15 the
    // per-tab READ-ONLY LOCK greys what it blocks — TWO of them on this row
    // (the ITERATION PAIR, back from the deleted menu on 2026-09-04; the four
    // marker verbs carried the same term down to the
    // bottom row on 2026-08-18, and the membership is inventoried once at
    // redesign_button_enabled) — so the lock looks the way the view already
    // looks (architect; the read-only-LEGAL buttons beside them stay lit, which
    // is the 2026-08-07 band ruling). A mode is entered deliberately and does
    // not flicker, which is exactly what separates these two from the refusals
    // the row still swallows silently. THE NEVER-GREY RULE'S INVERTED EXCEPTION
    // IS BACK ON THIS ROW since 2026-08-18 — the resting-disabled history
    // buttons, live only inside the view, which left on 2026-08-14 and returned
    // with the roster relayout (the opener's four companions plus the two walk
    // radios that landed behind it later the same day). All of it lives at
    // redesign_button_enabled, so this row's painter needs no arm of its own.
    const GuiRect lane = top_icon_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;

    const int border_h  = icon_row_border_h_px();
    const int content_h = lane.h - border_h;
    if (content_h <= 0) return;

    cairo_save(cr);

    cairo_set_source_rgb(cr, kRedesignContentGround.r,
                         kRedesignContentGround.g,
                         kRedesignContentGround.b);
    cairo_rectangle(cr, lane.x, lane.y, lane.w, content_h);
    cairo_fill(cr);

    // The border-bottom runs the ENTIRE window width with no break — the tab
    // row's break is the tab row's own fact, about a tab opening into this
    // surface; nothing opens into what is below here.
    cairo_set_source_rgb(cr, kRedesignTabLine.r, kRedesignTabLine.g,
                         kRedesignTabLine.b);
    cairo_rectangle(cr, lane.x, lane.y + content_h, lane.w, border_h);
    cairo_fill(cr);

    // (NO FONT IS SELECTED HERE, and that is the row's own fact since
    // 2026-08-11: this lane paints geometry and icons only. The sans face and
    // its scaled font were set for the four view radios' shaped LETTER faces,
    // which the architect replaced with real Breeze glyphs that day.)
    const int btn      = scaled_px(kIconBtnPx);
    const int btn_gap  = scaled_px(kIconBtnGapPx);
    const int sep_gap  = scaled_px(kIconSepGapPx);
    // Floored at 1 for the reason row 2's separator is (a line that rounds to 0
    // at gui_scale 50 takes the five groups' dividers with it).
    const int sep_w    = scaled_px(kIconSepWidthPx, 1);
    // AUTHORED, NOT DERIVED — deliberately unlike row 2's separator, and
    // measured before deciding (codex round 3, 2026-08-10). Row 2 places its
    // line from a rounded TOP MARGIN, so a third rounding put the whole error
    // under the line and it sat off-centre; this row places EVERYTHING by the
    // centering rule below, which splits the remainder itself. The residual is
    // the integer-centering remainder alone — at most 1px, at 168 of the 351
    // legal scales — and
    // the row's own 32px BUTTONS carry exactly the same residual at 172 scales
    // by the same expression. Deriving this one height would make the
    // separator the only element in the row not placed by the shared rule, so
    // the crop's 34 stays authored.
    const int sep_h    = scaled_px(kIconSepHeightPx);
    const int glyph_px = scaled_px(kIconGlyphPx);
    const int lw       = std::max(1, scaled_px(kIconOutlineStrokePx));
    const double radius = std::nearbyint(kIconCornerRadiusPx *
                                         gui_scale_factor());

    // EVERYTHING CENTERS IN THE CONTENT BAND — see the constants block: this is
    // the whole vertical layout, and it is what absorbs the 46/48 discrepancy.
    const int btn_y = lane.y + (content_h - btn)   / 2;
    const int sep_y = lane.y + (content_h - sep_h) / 2;

    int x = lane.x + icon_row_pad_x();
    // THE WALK: one left-to-right accumulation, every member placed. A group
    // LEADER (redesign_button_opens_icon_group, app_state.h — the roster's own
    // divider owner) draws the 4px / 1px line / 4px separator ahead of itself
    // and everything else takes the 2px gap; the ROW'S FIRST member needs no
    // special case, `first` swallowing the separator its own leader would owe.
    // (The collapse state machine that skipped members and carried an OWED
    // separator across them is deleted, 2026-08-14 — this row hides nothing.)
    bool first = true;
    for (const IconRowDef& def : kIconRowButtons) {
        if (!first) {
            if (redesign_button_opens_icon_group(def.id)) {
                x += sep_gap;
                cairo_set_source_rgb(cr, kRedesignTabLine.r, kRedesignTabLine.g,
                                     kRedesignTabLine.b);
                cairo_rectangle(cr, x, sep_y, sep_w, sep_h);
                cairo_fill(cr);
                x += sep_w + sep_gap;
            } else {
                x += btn_gap;
            }
        }
        first = false;

        AppState::RedesignButtonFace& face = publish_button_face(
            cr, app, audio, playback, target_render, def.id,
            GuiRect{x, btn_y, btn, btn});

        // THE SIXTH FACE, WORN FOR TWO MODES: the `h` history view (architect
        // 2026-08-04) and, since 2026-08-15, the per-tab READ-ONLY LOCK. Both
        // are ruled EXCEPTIONS to the never-grey rule above and both are MODE
        // statements — that is what earns them the face, a refusal alone never
        // does. Since 2026-08-14 EVERY
        // button in this row the view consumes wears it — Undo / Redo /
        // Render, the Show trim region button, FOLLOW (the mass-marker
        // category's one survivor since the 2026-08-27 relocation),
        // listen, the read-only toggle, and the MOMENT-STATE
        // Save (an empty head delta or a checkpoint in flight). Nothing leaves
        // the walk any more; the two view lamps, the zoom group and the
        // history opener stay live, as do the FOUR HISTORY COMPANIONS and,
        // since 2026-09-01, the LOAD IN PLACE that joined their group — each
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
        // A THIRD MODE REACHES THIS ROW SINCE 2026-08-18 and it is the reverse
        // of the first: the four HISTORY COMPANIONS grey OUTSIDE the `h` view,
        // their keys being bound only inside it. That answer is neither
        // derived nor hand-listed here — it is their own arm at
        // redesign_button_enabled, which states why the derived partition
        // cannot express it.
        // THE LOCK'S SET IS HAND-LISTED at redesign_button_enabled with
        // read_only_key_blocked named as its owner, and that arm's own case
        // list is where the membership is stated — the four marker verbs and
        // the measure are the BOTTOM row's since 2026-08-18, leaving THE
        // ITERATION PAIR as this row's two since 2026-09-04, when the two
        // buttons came back from the deleted Iterations menu (the copy/paste
        // pair left with the 2026-08-20 propagate relocation and the lock
        // still eats their chords with no face here to grey; Listen left the
        // set outright on 2026-08-28, bare `l` joining the allowlist with the
        // in-app player, and the Load in place took its lock term into the
        // history group's own arm on 2026-09-01) — the one place in
        // this face's membership that is not
        // derived, for reasons recorded there. This painter decides none of
        // the three.
        //
        // THE FACE IS THE ROW'S OWN INKS AT kRedesignDisabledMix — the product's
        // one disabled blend, row 2's rule applied to this row's glyph and
        // box: everything retains that fraction of itself over what sits under
        // it, so a dead button dims as ONE object. A dead SELECTED toggle
        // (follow left on, or the cumulative reading) keeps its fill and
        // outline muted rather than dropped: the mode cannot change that
        // state, so hiding it would be
        // a lie, and dimming it says "true, but not yours right now". Colour
        // only — gui_scale moves geometry, so the face is identical at 100% and
        // 200%.
        const double keep = face.enabled ? 1.0 : kRedesignDisabledMix;
        // Both pointer faces are gated on the live bit rather than trusted: the
        // recompute refuses to hover a disabled button and the claim never
        // records a press on one, but a button can go dead UNDER either with no
        // pointer event to refresh it (row 2's outline carries the same guard).
        const bool hovered = face.hovered && face.enabled;
        const bool pressed =
            face.enabled && redesign_button_pressed_face(app, def.id);

        // THE FILL AND THE OUTLINE ARE DECIDED SEPARATELY, which is exactly the
        // architect's reading of the five crops: the outline says "the pointer
        // is here" and the fill says "this is the state", so every combination
        // of the two falls out instead of being enumerated.
        const bool has_fill = pressed || face.selected;
        const bool has_line = hovered || pressed || face.selected;
        // What the glyph ends up sitting on, which is the ground its own dim
        // mixes toward: the painted fill where there is one, else the row.
        GuiColor under = kRedesignContentGround;
        if (has_fill || has_line) {
            // The shared face box (redesign_face_box — one path, filled and
            // stroked, the half-stroke inset rule stated there). THIS ROW'S FIT
            // SETTLED THE CONSTRUCTION: fitting both candidates against the
            // selected crop, the shared inset path scores 227 where the
            // full-box fill scores 270 at its own best radius and 2129 at this
            // one, and it is what the source widget does (a single rounded rect
            // drawn with both a brush and a pen).
            const GuiColor fill = mix_color(
                pressed ? mix_color(kRedesignAccent, kRedesignContentGround,
                                    kRedesignClickMix)
                        : kRedesignSelectedFill,
                kRedesignContentGround, keep);
            // Accent when the pointer is on it or it is held; otherwise the
            // calm grey that frames a resting toggled-on button.
            const GuiColor line = mix_color(
                (hovered || pressed) ? kRedesignAccent : kRedesignLine,
                kRedesignContentGround, keep);
            redesign_face_box(cr, x, btn_y, btn, btn, lw, radius,
                              has_fill ? &fill : nullptr,
                              has_line ? &line : nullptr);
            if (has_fill) under = fill;
        }

        // THE 22px ICON BOX centered in the 32px button (+5 at 100%), each path
        // in its own color from the icon table. The dimming term is the
        // mode-scoped dead face's and is inert (keep == 1, the table's colors
        // bit-identical) in every other state.
        //
        // EVERY BUTTON TAKES THIS ARM since 2026-08-11 — the four view radios'
        // shaped LETTER faces were the only other kind, and they took real
        // Breeze glyphs that day (kIconRowButtons carries the record). The row
        // paints no text at all now, which is why nothing here selects a font.
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
        icons::draw(cr, redesign_button_icon(app, def.id, def.icon),
                    static_cast<double>(x + (btn - glyph_px) / 2),
                    static_cast<double>(btn_y + (btn - glyph_px) / 2),
                    static_cast<double>(glyph_px), keep, under);

        x += btn;
    }

    cairo_restore(cr);
}

// THE BOTTOM ROW'S BUTTON CLUSTER AND CLOCK — the transport half of THE
// UNIFIED BOTTOM ROW (2026-08-12, rows 8 and 9 merged; the transport row
// landed 2026-08-11 as the touch arc's first surface, its own lane for one
// day): permanent on every host — ordinary mouse-clickable buttons, no touch
// mode, no flag, no detection. The transport THREE on the row's LEFT with the
// CLOCK behind a separator, and — FLUSH AT THE RIGHT MARGIN, three groups
// divided by two more separators — the four SINGLE-MARKER VERBS, the
// MARKER-WALK GROUP and the four ARROWS (the 2026-08-12
// relayout put the arrows there — "the nudge based icons in the bottom
// right", architect-agreed — and his 2026-08-15 live look re-weighted the two
// ends: "the more I think about it, the more awkward it feels to have all of
// that right next to those three others", so the left cluster dropped to three
// and the freed weight went right; the 2026-08-18 ROSTER RELAYOUT sent it more
// still, moving the verbs down from the icon row and taking the four history
// companions back up). That is the row's whole roster
// since 2026-08-13, when the STATUS CHAIN — which right-aligned against the
// arrows' left edge from the unification — moved into the TAB ROW (and was
// deleted there on 2026-08-29 for the status bar, whose one day left this
// row's roster untouched — and whose state text folded onto this row as the
// clock's own neighbour that evening, one cell and no ladder):
//
//   THE TRANSPORT, from the row's pad in the standard order — skip-back (bare
//   Home), THE ONE PLAY/STOP BUTTON (bare Space, whose GLYPH and TOOLTIP swap
//   on the live audition bit — Render-is-Cancel's own stateful-face shape, and
//   the reason this row's paint goes through redesign_button_icon; it was TWO
//   buttons over that one chord until 2026-08-15, most recently as a radio
//   pair) and skip-forward (bare
//   End). THE ROW'S OTHER BUTTONS GREY WHERE THEIR PRESS WOULD BE A NO-OP
//   since 2026-08-30 (architect: "Any time a button would be a no-op, grey
//   it") — the selection's state on the verbs, the arrows, the walk steps and
//   Copy value — on top of what the `h` history view's derived partition greys
//   and the marker verbs' own lock grey (below). From 2026-08-15 until then
//   they were LIT UNCONDITIONALLY apart from those two, by RULING rather than
//   an unfinished sweep (the architect reversing his own whole-row honesty
//   ruling of that morning in three steps — the arrows, then the skips, then
//   play and stop): "there's not a whole lot of value derived from the icon
//   faces changing, and it is a little distracting. The whole premise of the
//   GUI is that it expects strict user knowledge — the user is expected to
//   know that with the playhead outside trim it's not going to play in target
//   view." Every reasoning, per pair and per reversal, is at
//   redesign_button_enabled;
//   THE FIRST SEPARATOR, the ruled row-8 divider, and then THE CLOCK at the
//   pen it leaves behind — the timestamp, which moved here off the
//   status line in MONOSPACE (the architect's ruling, the face, the size
//   and the no-wiggle cell are all at kClockShape below). It was CENTRED IN
//   THE LANE from 2026-08-11 until 2026-08-18, when the architect anchored it
//   to the left block ("move bottom row timestamp to left alignment, place a
//   separator between transport buttons and timestamp") and then nudged it off
//   that pen by the two authored offsets at kClockCellOffsetXPx;
//   THE SINGLE-MARKER VERBS (2026-08-18), the right block's first four — drop
//   (bare `s`), delete (Delete), disable (Ctrl+D), inherit (Ctrl+N), moved
//   down from the icon row at the architect's word ("move
//   drop/delete/disable/toggle inherit to bottom right row"). THEY AND THE
//   MEASURE BESIDE THEM ARE THE
//   ROW'S ONLY RESTING GREYS ON A LOCKED TAB: their two mode gates — the `h`
//   view and a locked tab — are the BUTTONS' own and came down with them;
//   THE MARKER MEASURE (2026-08-19), seated between Toggle inherit and Add to
//   Selection — bare `/`, minuet-scales' staff and notes, an act with no lamp
//   (it wore edit-comment's balloon until 2026-08-20). It greys
//   with the four verbs in BOTH their modes — the `h` view and a locked tab,
//   the latter again since the 2026-08-21 sunset removed its score-video
//   shift half. It is not home-view gated (measures are the
//   fourth ruled exception);
//   ADD TO SELECTION (2026-08-18), closing the verb group
//   at the architect's own placement — bare `k`, the STICKY CTRL, and the
//   row's ONE LIT FACE: it wears the selected fill while the mode stands. Its
//   gates are its own and are NOT the verbs': the `h` view greys it with them,
//   the READ-ONLY LOCK does not, a selection being navigation;
//   THE SECOND SEPARATOR, then
//   THE MARKER-WALK GROUP (2026-08-15) — previous marker (Shift+Tab), next
//   marker (Tab), walk both tabs (Ctrl+Shift+Tab). Three buttons, three
//   chords, no gesture of their own;
//   THE THIRD SEPARATOR — the row paints three of the ruled row-8 dividers
//   (1px, 32 tall, 5px each side) since 2026-08-18, where it painted one:
//   adjacent groups at one anchor need a line where anchoring alone used to be
//   the boundary, and the left block needs one where the clock used to float
//   free of it;
//   THE CARDINAL ARROWS, closing the right block (their unification-era seat
//   beside the transport lasted one day), in the architect's own order
//   left-to-right
//   since 2026-08-14 — DOWN, UP, LEFT, RIGHT (bare Down/Up/Left/Right; it was
//   vim's left-down-up-right before), a single line and not a d-pad, and THE
//   ROW'S ONE HOLD GESTURE: held past the hold beat they repeat their own
//   chords at the compositor's key-repeat rate, the roster's only buttons that
//   do (architect 2026-08-16, the record at the arrows' chord-table rows,
//   input_pointer.cpp). Nothing about the PAINT changes for it — the pressed
//   interior is the ordinary arm's, and the hold has no cue of its own. THEY
//   PAINT IN EVERY STATE since 2026-08-18: the four HISTORY COMPANIONS took
//   these same slots while the `h` view stood from 2026-08-14 until then — a
//   cluster SWAP, one four painted at the anchor and the other publishing zero
//   rects — and the relayout sent the companions back to the icon row and
//   deleted the swap whole. Nothing on this row is conditional on a mode any
//   more, and no member of it publishes a zero rect except under a modal.
//
// (A CENTERED ESC BUTTON shipped between the groups on row 8's first day and
// was DELETED at the architect's live pass — "looks like a missing button
// with that cross out"; the mid-render CANCEL lives on the RENDER button now,
// and bare Esc is keyboard-only.)
//
// THE BUTTONS ARE THE ICON ROW'S BOXES (the unification's point — "the same
// size as the other icon buttons", bigger finger targets without stealing
// waveform height): 32px boxes with 22px glyphs, read from the icon row's own
// kIconBtnPx / kIconGlyphPx so the two rows cannot drift apart. ROW 8's
// SAMPLED KDENLIVE TRANSPORT METRICS ARE SUPERSEDED — the 26px button box
// measured off transport.png's Project Monitor cluster ((1569,367)-(1594,392))
// and the 16px glyph box derived from its inks via the viewBox (the ruled
// 26/16 pair of the 2026-08-11 tune-up) served the two-lane bottom strip and
// died with it. SINCE 2026-08-14 EVERY METRIC ON THIS ROW IS THE ICON ROW'S,
// read from its accessors and constants rather than authored here (architect:
// "make sure bottom row is same height and metrics (padding, etc.) as main
// icon row") — the 32px box, the 22px glyph, the 2px inter-button gap
// (kIconBtnGapPx; row 8's own ruled 2 from the 2026-08-11 tune-up happened to
// equal it and is retired as a separate constant), the 8px pad at both ends
// (icon_row_pad_x, paint_handler.h) and the lane's 46px content height
// (bottom_row_content_h_px delegating to icon_row_content_h_px, render.h). One
// source, so a retune of the icon row carries here by construction.
//
// THE ROW PAINTS THREE SEPARATORS since 2026-08-18, all on one spec. The
// 2026-08-12 relayout moved the arrows FLUSH RIGHT, which
// made anchoring the boundary between the row's two ends and left the ROW-8
// SEPARATOR SPEC with no consumer; the architect's marker-walk group put a
// second cluster at that right end on 2026-08-15 and cashed the spec in for
// ONE line; and the ROSTER RELAYOUT added two more — a THIRD group in the
// right block (the single-marker verbs, so two boundaries there instead of
// one) and the LEFT block's own, between the transport and the clock ("place
// a separator between transport buttons and timestamp"), the left cluster's
// first line ever. The spec's own ruled numbers (architect 2026-08-11): SOURCE
// tmp/screenshots/kdenlive/redesign/row_8_separator.png, 1x32 — a
// single flat column sampling #4c4e51, which IS kRedesignTabLine (the same
// grey the icon row's separators paint; recorded as the sampled value, per
// the palette rule) — a 1px, 32-tall kRedesignTabLine line
// centered in the content band, with FIVE pixels from button to separator on
// each side (the ruled metric, against the icon row's 4). THE TRIO GAINED
// CONSUMERS AND NOT NUMBERS: all three lines read kTransportSepGapPx /
// kTransportSepWidthPx / kTransportSepHeightPx, and each takes the same
// `+= sep_gap - btn_gap` step so its own gap REPLACES the trailing button gap
// rather than adding to it.
//
// EVERYTHING ELSE IS THE ICON ROW'S OWN MODEL (the outline stroke, the corner
// radius, the centering rule): same ground, same five faces, same one disabled
// blend. WHO WEARS THE DEAD FACE HERE, re-derived after the 2026-08-18
// rulings — THIRTEEN of the eighteen, where it used to be one: in the `h` view
// the derived partition greys the PLAY/STOP button (Space is consumed there),
// the FOUR CARDINAL ARROWS (bare Up/Down/Left/Right are neither the mode's
// vocabulary nor on its allowlist, and they are painted in there at all only
// since the cluster swap's deletion), the FOUR SINGLE-MARKER VERBS, COPY
// VALUE, THE EDIT
// FLAG BUTTON, THE MARKER
// MEASURE and ADD TO SELECTION (bare `j`, bare Return, bare `/` and bare `k`,
// consumed in there like
// the verbs' chords); the two
// SKIPS and the MARKER-WALK GROUP'S THREE stay lit, Home/End being the mode's
// own absolute jumps, Tab/Shift+Tab its diff-flag cycle (architect-confirmed
// for the skips) and Ctrl+Shift+Tab the march composing that cycle with the A/B
// switch. Outside the view the four VERBS, the EDIT FLAG BUTTON and the
// MARKER MEASURE grey on a locked tab, their own
// gate — COPY VALUE, seated among them, does NOT, both its chords being
// navigation the lock admits — and since 2026-08-30 EVERY MEMBER BUT THE
// TRANSPORT THREE, WALK BOTH TABS AND ADD TO SELECTION greys on the
// selection's state where its press would be a consumed no-op (the
// truthful-buttons ruling, reversing the architect's 2026-08-15 always-on
// ruling, which was made about the RESTING face of the ten members the row
// then had). All at redesign_button_enabled; nothing decided here.
// THE SELECTED FACE IS WORN BY ADD TO SELECTION AND NOTHING ELSE HERE
// (2026-08-18): the sticky-ctrl mode lights while it stands, which is the
// roster's standing rule for a mode and the lamp iteration and follow already
// wear up in row 4. (The row was lampless for the hours between the Cumulative
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
// clears the selection and hides the trim region overlay, no-op jump included, so a grey
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
// The
// ONE box-model difference is the border edge: this lane's 1px border is on
// TOP, the waveform side — the bottom strip's chrome grows toward the
// waveform, so the border facing it is the one drawn (the mirror of the icon
// row's border-bottom; the lane's chrome is paint_bottom_strip's, which calls
// the body below onto its content band).

// THE ROW AUTHORS NO METRIC OF ITS OWN BUT ITS SEPARATOR'S (the block above):
// the boxes, glyphs, gap, pads, stroke, radius and content height are all the
// icon row's, read from its constants and accessors. The kTransportSep* trio
// IS LIVE since 2026-08-15, when the MARKER-WALK GROUP became the divider's
// third consumer — it was the unification's adjacent left cluster for one day
// in 2026-08-12, forward spec again when the arrows moved flush right, and
// forward spec is exactly what that arrival cashed in: the numbers are the
// ruled ones off row_8_separator.png rather than a re-derivation, which is
// what a spec kept without a consumer is for. IT HAS THREE DRAWING SITES since
// 2026-08-18 and still one set of numbers, which is the point of authoring
// them once.
//
// THEY ARE THE ICON ROW'S SEPARATOR AT ROW 8'S OWN METRICS, deliberately not
// the icon row's constants: same 1px kRedesignTabLine line, but 32 tall
// against row 4's 34 and a 5px gap per side against row 4's 4, both sampled
// from this row's own crop. (The rest of the row reads row 4's constants; a
// sampled number of this row's own does not become a shared one because its
// neighbours are.)
constexpr double kTransportSepGapPx    = 5.0;
constexpr double kTransportSepWidthPx  = 1.0;
constexpr double kTransportSepHeightPx = 32.0;

// The painter's half of the row's roster: four groups, painted left to right
// (the transport at the left pad, then the right block's three).
// The press claim's chord table (input_pointer.cpp) is the other half; both
// key off the same ids.
struct TransportRowDef {
    RedesignButton id;
    icons::Icon    icon;
};
// THE LEFT CLUSTER IS THREE since 2026-08-15 (architect, at his live look):
// play and stop collapsed into ONE stateful button, so the table carries
// media-playback-START as that button's resting glyph and the resolver swaps
// in media-playback-STOP while an audition runs (redesign_button_icon —
// Save's and Render's own shape, and the reason this row's paint goes through
// the resolver at all now). The freed weight went to the row's RIGHT, where
// the marker-walk group landed in the same ruling.
constexpr TransportRowDef kTransportGroup[] = {
    {RedesignButton::TransportSkipBack,    icons::Icon::MediaSkipBackward},
    {RedesignButton::TransportPlayStop,    icons::Icon::MediaPlaybackStart},
    {RedesignButton::TransportSkipForward, icons::Icon::MediaSkipForward},
};
// THE SINGLE-MARKER VERBS (architect 2026-08-18, "move drop/delete/disable/
// toggle inherit to bottom right row"), the RIGHT BLOCK'S FIRST GROUP: drop
// (bare `s`, list-add), delete (`Delete`, Breeze's RED list-remove — the
// resolved color recorded at the icons.cpp table), the disable toggle
// (`Ctrl+D`, view-hidden's crossed-out eye) and inherit/collapse (`Ctrl+N`,
// insert-link — a pass marker links its tempo to its neighbor). They opened
// their own separator-led group in the ICON ROW from 2026-08-12 until this
// move and brought their four glyphs down unchanged.
//
// THE GROUP IS FIVE since later the same day, the architect seating ADD TO
// SELECTION himself — "add group selection icon ('Add to Selection') after
// toggle inherit, before the separator". It is the group's one MODE (bare `k`,
// edit-select: the pointer arrow over a marquee corner) and therefore the only
// member of this table that ever wears the lit fill; the four verbs above are
// acts that complete.
//
// THE GROUP IS SIX since 2026-08-19, the MARKER MEASURE seated between Toggle
// inherit and Add to selection (bare `/`, minuet-scales: notes climbing a staff
// — it wore edit-comment's speech balloon for the one day the field was a free
// text comment, and the architect swapped the glyph with the grammar on
// 2026-08-20). It is an act like the four above it, not a mode, so it wears no
// lamp — the editor's own open session is its state. ONE THING SETS IT APART
// FROM THE FOUR VERBS: it is not home-view gated (measures are the fourth ruled
// exception, so it works on both columns in both audio views). The READ-ONLY
// LOCK greys it with them, as it did before the score-video jump's one lit day
// (2026-08-20 to the 2026-08-21 sunset).
//
// THE GROUP IS SEVEN since 2026-08-27, the EDIT FLAG BUTTON seated between
// Toggle inherit and the Measure — bare Enter, text-field (Breeze's own text
// cursor: a serif I-beam on a field's underline rule). THE SEAT IS THE
// ARCHITECT'S: "before Measure, away from the delete button", an editor open
// kept two boxes clear of a destruction. It opens the FLAG editor where its
// neighbour opens the MEASURE one, on the same machinery and the same focused
// marker, so the two sit together — and like the Measure it is an act rather
// than a mode, wears no lamp, and greys under the READ-ONLY lock and in the
// `h` view. It is the flag editor's THIRD ROAD, added when the architect
// retired the one-evening touch halo that had tried to rescue the double tap
// instead.
//
// THE GROUP IS EIGHT since 2026-08-29, the COPY RESOLVED VALUE button seated
// IMMEDIATELY AFTER THE MEASURE (bare `j`, edit-copy's two stacked sheets) —
// THE ARCHITECT'S OWN SEAT, taken on his live pass that evening. It landed
// that afternoon in the slot after Toggle inherit, on a coder's reading of his
// first ask "beside Toggle inherit", and he moved it to the group's tail the
// same day: the four verbs and the two editor openers keep their run, and this
// button joins Add to selection at the end, the two members that reach a
// marker without authoring it. It is the group's one member the READ-ONLY LOCK
// does not grey (its two chords author nothing) and the second on this row to
// admit SHIFT, whose twin is the jump to the marker the value came from.
constexpr TransportRowDef kMarkerVerbGroup[] = {
    {RedesignButton::IconMarkerDrop,       icons::Icon::ListAdd},
    {RedesignButton::IconMarkerDelete,     icons::Icon::ListRemove},
    {RedesignButton::IconMarkerDisable,    icons::Icon::ViewHidden},
    {RedesignButton::IconMarkerInherit,    icons::Icon::InsertLink},
    {RedesignButton::IconMarkerEditFlag,   icons::Icon::TextField},
    {RedesignButton::IconMarkerMeasure,    icons::Icon::MinuetScales},
    {RedesignButton::IconCopyValue,        icons::Icon::EditCopy},
    {RedesignButton::IconAddToSelection,   icons::Icon::EditSelect},
};
// THE MARKER-WALK GROUP (architect 2026-08-15), the right block's middle three
// between the verbs and the arrows: previous marker (Shift+Tab), next marker
// (Tab), walk both tabs (Ctrl+Shift+Tab). THE GLYPHS ARE HIS OWN PICKS from a
// rendered candidate sheet and the reasons are at their icons.h entries — bbox
// is an arrow meeting a bar (the Tab key's own shape, sharing no silhouette
// with the chevrons two slots away, the media triangles at the row's left, or
// the keyframe dials the history walk wears up in the icon row), boost is a
// two-arrow cycle, which is what walking both tabs is.
constexpr TransportRowDef kTransportWalkGroup[] = {
    {RedesignButton::TransportWalkPrev,    icons::Icon::BboxPrev},
    {RedesignButton::TransportWalkNext,    icons::Icon::BboxNext},
    {RedesignButton::TransportWalkBoth,    icons::Icon::Boost},
};
// DOWN, UP, LEFT, RIGHT — the architect's order, 2026-08-14, superseding the
// row's original vim order (h j k l = left / down / up / right) with no
// reasoning offered and none needed. All four are Breeze's chevron family —
// GoDown / GoUp for the verticals, GoPrevious / GoNext for the horizontals —
// so the group is one construction. The horizontal pair was SHARED with the
// icon row's walk arrows for a few hours on 2026-08-11 (an icon is a glyph,
// not a button) and is this row's alone since the walk took the keyframe dials
// that afternoon.
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

// THE CLOCK — ROW 8'S LEFT-ALIGNED CELL, AND THE FIRST OF THE PRODUCT'S
// THREE MONOSPACE CELLS (with the render player's modal clock, which derives
// from it, and the AV Sync Stats panel's lines since 2026-09-03 — the set is
// named at gui_font.h)
// (architect 2026-08-11, moving the timestamp off the status line and reversing
// his own 2026-08-01 "monospace is gone from the product" in the same breath —
// the reversal is scoped to this cell and the status line stays sans, which is
// stated at that row's own header block; the chain has been the TAB ROW's
// since 2026-08-13 and this is the only text left on this lane).
//
// THE FACE IS "monospace" at NORMAL weight, which fontconfig resolves to
// Liberation Mono on this host (it shipped BOLD for the row's first hours and
// the architect dropped the weight at his second look — keep the face, lose the
// bold; the specimen derivation below is weight-agnostic and the memoised
// metrics key on size, so nothing else about the cell moved). THE SIZE IS
// 11pt SINCE 2026-08-14 (clock_font_size_px, render.h — the architect's live
// call, and the product's one departure from the redesign's shared 12pt); it
// rides gui_scale like every other string, and it is a RETUNABLE rather than
// a sampled number, unlike this row's geometry. The cell re-measures itself
// at whatever size this returns — the memo keys on it — so the smaller face
// simply gives a narrower cell, at the same left-aligned origin.
//
// THE NO-WIGGLE GUARANTEE IS BY CONSTRUCTION, NOT BY FACE TRUST, which is why
// the widest-digit derivation came along from row 9 rather than dying with
// section A: a monospace face is a strong reason to believe every digit
// advances alike, and this measures it instead. The ten digits are shaped at
// the CURRENT scaled font, the widest advance wins, and the cell is a specimen
// built from THAT digit — "DD:DD.DDD", the MM:SS.mmm shape with its 7 digit
// slots — shaped through the same one-run path the painted clock takes. The
// cell then starts at the LEFT BLOCK'S separator pen plus the authored offset
// below (architect 2026-08-18; it was CENTERED in the lane from 2026-08-11
// until then) and the live text starts
// at its LEFT pen too, so the reserved box does not move at all on a given
// scale and the glyphs never walk inside it.
//
// TWO MINUTE DIGITS, and longer sources TRUNCATE (the ruling and what it costs
// are at format_timestamp, time_format.h). The cell is that format's width and
// no wider.
constexpr const char* kClockShape = "DD:DD.DDD";

// THE CELL'S TWO AUTHORED OFFSETS OFF THAT SEAT (architect 2026-08-18, his own
// measured numbers from looking at the row): the cell sits FOUR PIXELS RIGHT of
// the separator's pen and ONE PIXEL DOWN of the band's centred baseline.
//   * the 4px is a MARGIN MIRROR — the row's last button keeps one lane pad
//     from the lane's RIGHT edge, and this gives the clock the same air on its
//     left, so the row's two text-free margins read alike;
//   * the 1px puts the digits perfectly vertically centred in the lane.
// THEY ARE AUTHORED, NOT DERIVED — sampled off the painted row exactly as this
// row's separator trio is, so neither is folded into an expression over the pad
// or the baseline. They ride gui_scale like every other authored length here.
constexpr double kClockCellOffsetXPx = 4.0;
constexpr double kClockCellOffsetYPx = 1.0;

// THE SEPARATOR → TIMESTAMP DISTANCE, this row's own and the ONE OWNER of
// that distance (architect 2026-08-29): the whole air between the divider's
// line and the first digit, which this row spends in its two halves — the
// separator's own trailing gap above, then the cell's authored margin mirror.
// ONE SITE READS THE CONSTANT and one site spends the same air by its two
// addends (retold 2026-09-02 — the count of two read the clock cell as a
// reader of this name, which it is not):
//   * the CLOCK CELL itself, above, which lays the separator's trailing gap
//     and then the cell's own margin, i.e. these two addends in order;
//   * the RENDER PLAYER'S MODAL ROW, the constant's one reader by name, which
//     takes it whole for every gap around
//     its play-scrub (left separator → scrub, scrub → clock, clock → right
//     separator), the architect's ruling being that those gaps ARE this one:
//     the modal row stands in row 8's lane and its clock is row 8's cell said
//     twice, so it takes this distance rather than authoring a second spec for
//     the same air in the same place.
// (A THIRD READER stood from 2026-08-29 to 2026-08-31: the sans STATE CELL
// right of the clock took this same distance as its DIGITS-TO-TEXT gap, so no
// second number was authored for it. It went when the state text joined the
// clock's own run behind a literal ` | ` — the separation is a character now,
// not a measured gap.)
constexpr double kTransportSepToClockPx =
    kTransportSepGapPx + kClockCellOffsetXPx;

// The clock's cell, MEMOISED ON THE FONT SIZE — eleven tiny shaping
// passes (ten digits plus the specimen) that answer the same thing on every
// frame, the face being fixed and the size the only variable. Single-threaded
// paint state; the waveform worker never reaches this file's text tiers.
//
// THE CELL IS THE SPECIMEN'S ADVANCE: `cell_w` is the reserved WIDTH the digits
// are laid inside — an advance sum, so no digit ever walks — and it is the
// cell's damage box too. (It carried the specimen's INK pair beside it from
// 2026-08-30 to 2026-08-31, for the sans STATE CELL that butted against the
// digits by eye rather than by advance; both fields and the one ink reader in
// text_shape retired when the state text became part of this clock's own run.)
struct TransportClockMetrics {
    double px        = -1.0;   // the size this was measured at
    double cell_w    = 0.0;    // the widest specimen's shaped width (advances)
};
static TransportClockMetrics g_clock_metrics;

static const TransportClockMetrics& clock_cell_metrics(
        cairo_scaled_font_t* font, double size_px) {
    if (g_clock_metrics.px == size_px) return g_clock_metrics;
    char widest = '0';
    double widest_w = -1.0;
    for (char d = '0'; d <= '9'; ++d) {
        const char one[2] = {d, '\0'};
        const double w = text_shape::shape_text_run(font, one).width_px;
        if (w > widest_w) { widest_w = w; widest = d; }
    }
    std::string specimen(kClockShape);
    for (char& c : specimen) if (c == 'D') c = widest;
    const text_shape::ShapedRun run =
        text_shape::shape_text_run(font, specimen);
    g_clock_metrics.cell_w    = run.width_px;
    g_clock_metrics.px        = size_px;
    return g_clock_metrics;
}

static double clock_cell_width_px(cairo_scaled_font_t* font, double size_px) {
    return clock_cell_metrics(font, size_px).cell_w;
}

// THE MONOSPACE LINE HEIGHT, MEMOISED ON THE SIZE like the cell above (render.h
// declares it; the AV sync stats panel's text rows stand at it through
// folder_overlay::text_row_pitch_px, which does the rounding). A line is the
// face's ascent plus its descent and nothing more — a monospace line's leading
// is inside its extents, which is why the panel's rows have no gap between
// them. THE FACE IS SELECTED ON A PRIVATE 1x1 IMAGE SURFACE through the one
// face owner: this file's painters borrow a context they are handed, while
// this measure has readers that hold none — the panel's damage rect and its
// hit test — and may ask before the panel has painted once. The paint target
// is an image surface too on both backends, so the two contexts report the
// same hinted extents. Single-threaded paint state, as the clock's memo is.
struct MonoLineMetrics {
    double px = -1.0;   // the size this was measured at
    double h  = 0.0;    // ascent + descent at that size
};
static MonoLineMetrics g_mono_line_metrics;

double mono_line_height_px(double size_px) {
    if (g_mono_line_metrics.px == size_px) return g_mono_line_metrics.h;
    cairo_surface_t* surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(surface);
    gui_select_font_face(cr, GuiFontFamily::Mono);
    cairo_set_font_size(cr, size_px);
    cairo_font_extents_t fe;
    cairo_font_extents(cr, &fe);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_mono_line_metrics.h  = fe.ascent + fe.descent;
    g_mono_line_metrics.px = size_px;
    return g_mono_line_metrics.h;
}

void GuiPaintHandler::paint_bottom_row_buttons_and_clock(cairo_t* cr) {
    const GuiRect lane    = bottom_row_area(app);
    const GuiRect content = bottom_row_content_area(app);
    if (lane.w <= 0 || lane.h <= 0 || content.h <= 0) return;
    const int content_y = content.y;
    const int content_h = content.h;

    cairo_save(cr);

    const int btn      = scaled_px(kIconBtnPx);
    const int btn_gap  = scaled_px(kIconBtnGapPx);
    const int pad      = icon_row_pad_x();
    const int glyph_px = scaled_px(kIconGlyphPx);
    const int lw       = std::max(1, scaled_px(kIconOutlineStrokePx));
    const double radius = std::nearbyint(kIconCornerRadiusPx *
                                         gui_scale_factor());
    // The 32px box centred in the row's 46px content band, which is the icon
    // row's own arithmetic on the icon row's own height ((46-32)/2 = 7).
    const int btn_y = content_y + (content_h - btn) / 2;

    // One button, the icon row's face logic verbatim minus the letter arm.
    // THE SELECTED TERM'S SUBJECT ON THIS ROW IS ADD TO SELECTION (2026-08-18),
    // the sticky-ctrl mode that closes the marker-verb group: it wears the lit
    // fill while the mode stands, which is the roster's standing rule for a
    // mode. The row was lampless for the hours between the Cumulative toggle —
    // its lamp from 2026-08-14 — going back to the icon row with the rest of
    // the history group and that arrival, and the Play / Stop RADIO PAIR that
    // was a second lamp for hours on 2026-08-15 went with that pair's collapse
    // into one button. The term was kept through the gap rather than folded
    // away, which is why this arrival cost the body nothing: this is
    // the icon row's face logic verbatim, and a row that has gained and lost a
    // lamp four times is not a row to hard-code a false into. THE DISABLED
    // BLEND WOULD COMPOSE WITH IT rather than exclude it — `keep` mixes both
    // the fill and the line toward the ground, so a lit button a mode greys
    // wears the DIMMED SELECTED face — which is the combination the Cumulative
    // toggle relies on and now relies on up in row 4.
    const auto paint_button = [&](const TransportRowDef& def, int x) {
        AppState::RedesignButtonFace& face = publish_button_face(
            cr, app, audio, playback, target_render, def.id,
            GuiRect{x, btn_y, btn, btn});

        const double keep = face.enabled ? 1.0 : kRedesignDisabledMix;
        const bool hovered = face.hovered && face.enabled;
        const bool pressed =
            face.enabled && redesign_button_pressed_face(app, def.id);

        const bool has_fill = pressed || face.selected;
        const bool has_line = hovered || pressed || face.selected;
        GuiColor under = kRedesignContentGround;
        if (has_fill || has_line) {
            const GuiColor fill = mix_color(
                pressed ? mix_color(kRedesignAccent, kRedesignContentGround,
                                    kRedesignClickMix)
                        : kRedesignSelectedFill,
                kRedesignContentGround, keep);
            const GuiColor line = mix_color(
                (hovered || pressed) ? kRedesignAccent : kRedesignLine,
                kRedesignContentGround, keep);
            redesign_face_box(cr, x, btn_y, btn, btn, lw, radius,
                              has_fill ? &fill : nullptr,
                              has_line ? &line : nullptr);
            if (has_fill) under = fill;
        }
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
        icons::draw(cr, redesign_button_icon(app, def.id, def.icon),
                    static_cast<double>(x + (btn - glyph_px) / 2),
                    static_cast<double>(btn_y + (btn - glyph_px) / 2),
                    static_cast<double>(glyph_px), keep, under);
    };

    // THE ROW'S SEPARATOR, AUTHORED ONCE AND DRAWN THREE TIMES (2026-08-18;
    // the ruled numbers and their crop are at the kTransportSep* block above).
    // Every site takes the same step, which is the whole idiom: the pen arrives
    // holding the TRAILING BUTTON GAP the group's loop left behind, and the
    // separator's own 5px gap REPLACES it rather than adding to it — that is
    // what the spec's "five pixels from button to separator" means. The lambda
    // returns the pen the next tenant starts at.
    const int sep_gap = scaled_px(kTransportSepGapPx);
    // Floored at 1 for the reason the icon row's separator is: a line that
    // rounds to 0 at gui_scale 50 is a divider that is simply not there.
    const int sep_w   = scaled_px(kTransportSepWidthPx, 1);
    const int sep_h   = scaled_px(kTransportSepHeightPx);
    const int sep_y   = content_y + (content_h - sep_h) / 2;
    const auto paint_separator = [&](int pen) {
        pen += sep_gap - btn_gap;
        cairo_set_source_rgb(cr, kRedesignTabLine.r, kRedesignTabLine.g,
                             kRedesignTabLine.b);
        cairo_rectangle(cr, pen, sep_y, sep_w, sep_h);
        cairo_fill(cr);
        return pen + sep_w + sep_gap;
    };

    // THE TRANSPORT AND THE CLOCK AT THE LEFT, THE RIGHT BLOCK FLUSH RIGHT
    // (the 2026-08-12 relayout's rearrangement, architect-agreed: "the
    // nudge based icons in the bottom right"; RE-WEIGHTED 2026-08-15 at his
    // live look — "the more I think about it, the more awkward it feels to
    // have all of that right next to those three others"; RE-WEIGHTED AGAIN
    // 2026-08-18, the roster relayout sending the four single-marker verbs
    // down here and taking the four history companions back up): the TRANSPORT
    // THREE walk from the row's left pad, a SEPARATOR follows them and the
    // CLOCK CELL starts at the pen it leaves — the architect's own ask, "move
    // bottom row timestamp to left alignment, place a separator between
    // transport buttons and timestamp". The RIGHT BLOCK anchors at the RIGHT
    // margin as FIVE + SEPARATOR + THREE + SEPARATOR + FOUR — the MARKER
    // VERBS with the MARKER MEASURE and ADD TO SELECTION behind them, the
    // MARKER-WALK GROUP, and the CARDINAL ARROWS (↓ ↑ ← →, the
    // architect's order since 2026-08-14). The span between the cell and the
    // right block is THE STATUS CELL since 2026-08-29 (the status bar's fold
    // into this row) — the clock's neighbour, laid out with it below and
    // clipped one lane pad short of the right block's own left edge. THE VERB
    // GROUP IS EIGHT since 2026-08-29, the COPY VALUE button joining it after
    // Toggle inherit (it was seven from 2026-08-27, the EDIT FLAG BUTTON's
    // arrival, and six from 2026-08-19, the MARKER MEASURE's).
    //
    // THE TWO ENDS CANNOT CRAWL INTO EACH OTHER FROM THE CLOCK'S SIDE ANY MORE
    // (2026-08-18). The cell was CENTRED IN THE LANE until then, so it TRAVELLED
    // toward the right block as the window narrowed and the clearance had to be
    // re-derived at every metrics change; anchored to the left block it is at a
    // fixed pen on every window, and only the RIGHT block moves. At 100% the
    // left block ends at the clock's pen — 8px pad + three 32px boxes + two 2px
    // gaps = 108, then 5 + 1 + 5 = 119, and the cell's own authored 4px offset
    // seats it at 123 — and the right block is 526 wide
    // (270 verbs + 11 separator span + 100 walk + 11 + 134 arrows), so it
    // starts at 106 on the 640px defensive floor, 490 on the retired rig's
    // 1024 and 1386 at 1920. The 9-glyph cell measures about 80px at 100% (it
    // narrowed when the clock went to 11pt on 2026-08-14, so that is an upper
    // bound), which leaves the rig's own 1024 some 287px of ground between
    // the cell and the verbs — the room THE STATUS CELL now takes, clipped
    // one pad short of the block. THE 640px DEFENSIVE FLOOR NOW CROPS INTO
    // THE CLOCK — the
    // block's origin lands left of the cell's ~203px right edge — and that is
    // ACCEPTED under the crop-at-the-floor allowance recorded at
    // kMinWindowWidthPx rather than answered: 640 is a floor no real host of
    // this product uses (the rig was 1024, the laptop 1920). The Marker Measure
    // button took 34 of the 40 that Add to selection left on 2026-08-18, the
    // EDIT FLAG BUTTON took 34 more on 2026-08-27 and the COPY VALUE button
    // another 34 on 2026-08-29 — the block's ONE
    // dimension that moves when this group gains a box, which is why the
    // numbers in this paragraph are re-derived at every such landing rather
    // than inherited. THE
    // ROW STILL CARRIES NO COLLISION RULE — none of the
    // redesign does, row 1's floats included — and the crop-at-the-floor
    // allowance recorded at kMinWindowWidthPx is what covers a scale driven
    // toward the 350 ceiling (2026-08-29). THE RIGHT BLOCK IS 526 AUTHORED PX
    // WIDE and anchored one pad in from the right edge, so it reaches the
    // clock's own ~203px right edge once the LOGICAL width (device width over
    // the factor) falls below about 737 — at 350% on a 2304px panel that is
    // 658, where the verb group lands on the cell outright; the tablet's own
    // 225% leaves 1024 logical px and 287 of clear ground. Still no collision
    // rule, for the reason above: the row crops at its floor. THE STATUS CELL
    // TAKES THAT GROUND AND CANNOT PUSH ANYTHING: it CLIPS at the block's own
    // left edge less one pad, so a long line is cut rather than colliding.
    int x = lane.x + pad;
    for (const TransportRowDef& def : kTransportGroup) {
        paint_button(def, x);
        x += btn + btn_gap;
    }
    const int clock_cell_x = paint_separator(x);

    // THE RIGHT BLOCK, from its right-anchored origin: the VERB GROUP's seven
    // boxes (the four single-marker verbs with the EDIT FLAG BUTTON since
    // 2026-08-27, the MARKER MEASURE since 2026-08-19 and ADD TO SELECTION
    // since 2026-08-18 behind them), a separator, the WALK GROUP's three, a separator, and the four
    // ARROWS whose LAST button's right edge is one pad in from the lane's
    // right edge. The whole block is measured first and laid left to right
    // from there, so one expression owns the anchor and no group re-derives it.
    //
    // IT HAS NO MODE TERM AT ALL since 2026-08-18. From 2026-08-14 the arrows'
    // four slots were a SWAP — the history companions painted there while the
    // `h` view stood, the unpainted four publishing zero rects — and the
    // relayout took those companions back to the icon row, which leaves one
    // cluster at that anchor in every state. So the shown/hidden selection,
    // the zero-rect publishes and the swap's whole-window damage note are
    // deleted rather than kept: every button on this row publishes a real rect
    // on every frame now, except under a modal, where the row yields whole.
    int right_block_x = lane.x + lane.w - pad;
    {
        const int verbs_w  = 8 * btn + 7 * btn_gap;
        const int walk_w   = 3 * btn + 2 * btn_gap;
        const int arrows_w = 4 * btn + 3 * btn_gap;
        const int sep_span = sep_gap + sep_w + sep_gap;
        const int block_w  =
            verbs_w + sep_span + walk_w + sep_span + arrows_w;
        int ax = lane.x + lane.w - pad - block_w;
        // THE STATE CELL'S CLIP BOUND (below) is this block's own left edge,
        // published out of the scope so the cell cannot guess it.
        right_block_x = ax;
        for (const TransportRowDef& def : kMarkerVerbGroup) {
            paint_button(def, ax);
            ax += btn + btn_gap;
        }
        ax = paint_separator(ax);
        for (const TransportRowDef& def : kTransportWalkGroup) {
            paint_button(def, ax);
            ax += btn + btn_gap;
        }
        ax = paint_separator(ax);
        for (const TransportRowDef& def : kTransportArrowGroup) {
            paint_button(def, ax);
            ax += btn + btn_gap;
        }
    }

    // THE CLOCK. Its own face on this context — the monospace face, selected
    // through the one face owner (gui_font.h) — contained by the save/restore
    // this body already opened; nothing else on the row draws text. ONE FACE,
    // THREE CELLS since 2026-09-03: this one, the RENDER PLAYER's
    // `<position> / <length>` on the modal row — which takes this cell's size,
    // metrics and authored offsets so the digits never walk between them
    // (2026-08-28) — and the AV SYNC STATS PANEL's rows, which take the face
    // and the size and none of this cell's geometry, being a list rather than
    // a cell. NONE OF THE THREE EVER PAINTS IN THE SAME FRAME AS ANOTHER: the
    // player and the panel each own the row whole while they stand, and the
    // panel's band covers everything between the menu row and this one. The
    // set's own record is at this file's bottom-row text block.
    {
        gui_select_font_face(cr, GuiFontFamily::Mono);
        const double size_px = clock_font_size_px();
        cairo_set_font_size(cr, size_px);
        cairo_scaled_font_t* font = cairo_get_scaled_font(cr);
        const TransportClockMetrics& clock_metrics =
            clock_cell_metrics(font, size_px);
        const double cell_w = clock_metrics.cell_w;
        // THE CELL STARTS AT THE LEFT BLOCK'S SEPARATOR PEN (architect
        // 2026-08-18, "move bottom row timestamp to left alignment"), PLUS THE
        // AUTHORED MARGIN-MIRROR OFFSET (the same day, at his live look; both
        // offsets and their reasons are at kClockCellOffset*Px). The cell
        // is still a reserved WIDTH — measured from the widest digit's
        // specimen, so the glyphs never walk inside it — and only its ORIGIN
        // moved: it was `lane.x + nearbyint((lane.w - cell_w) * 0.5)`, the lane
        // midline, from 2026-08-11 until then.
        const int cell_x = clock_cell_x + scaled_px(kClockCellOffsetXPx);
        // AND THE BASELINE TAKES THE VERTICAL OFFSET, off the band's centred
        // one, which is what leaves the digits reading centred in the lane.
        const double baseline =
            redesign_baseline(font, static_cast<double>(content_y),
                              static_cast<double>(content_h)) +
            scaled_px(kClockCellOffsetYPx);

        // PUBLISH THE CELL FOR THE DAMAGE OWNER (clock_invalidate_rect,
        // app_state.h — the stash contract is at the field). One pixel of slack
        // on each side: the reserved width is an ADVANCE sum and a glyph's ink
        // may sit a hair outside it, and the band is the row's content height,
        // which contains the baseline's ascent and descent by construction.
        // THE RECT FOLLOWS THE CELL BECAUSE IT IS BUILT FROM IT — the horizontal
        // offset above is in `cell_x` and so is in this box, which is what stops
        // the moved cell leaving a trail. The VERTICAL offset needs no term: the
        // box is the row's whole content band, which the nudged baseline's ink
        // stays inside, and the band's bottom IS the window's, so widening it
        // downward would damage past the surface.
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

        // THE LOWER LEFT IS ONE MONOSPACE RUN (architect 2026-08-31): the
        // clock, a LITERAL pipe, and the state text — `00:00.100 | Updating...`
        // is his own example — painted as ONE string in the clock's own face,
        // at the clock's size, on the clock's baseline, from the clock's own
        // origin. There is no second cell and no second face here any more.
        //
        // WHAT THIS SUPERSEDES (2026-08-29 to 2026-08-31): the state text was
        // the clock's NEIGHBOUR, a sans run in the row's ordinary label ink,
        // laid a second read of kTransportSepToClockPx off the clock — the
        // layout `| sep | gap | 02:42.608 | gap | <state text> |` — with an
        // ink-to-ink origin correction (the constant taken off the memoised
        // specimen's ink RIGHT edge, plus the specimen's left bearing, less the
        // sans run's own, so both gaps around the digits read alike) and its
        // own centred baseline. THE SEPARATION IS NOW A CHARACTER, so all of
        // that geometry retires: the second read of the constant, the ink pair's
        // use at this site, the face switch and the sans baseline. The constant
        // keeps its FIRST reader (this row's separator -> clock distance, above)
        // and the render player's modal row's three; the memoised ink pair
        // keeps the specimen it was measured from.
        //
        // THE CONTENT is unchanged and so is its precedence: the `h` walk line,
        // else the render / batch / loading progress line. The two CAN COEXIST
        // and THE WALK LINE WINS — nothing STARTS a render inside the `h` view
        // (both render chords are off its allowlist), but a render or a target
        // preview dispatched BEFORE the visit runs on through it, so the mode's
        // line is what the cell is for while the view stands and the progress
        // line is back the moment it closes.
        //
        // IT IS CLIPPED, NEVER ELLIPSISED — a cairo rectangle clip, the folder
        // overlay rows' precedent — at the right block's own left edge less
        // one lane pad, so a long line is cut rather than colliding with the
        // marker verbs.
        //
        // THE CLIP IS THE STATE'S OWN RUN, AND THAT IS WHY THERE ARE TWO RUNS
        // (2026-08-31, the round-B conversion). One face, one size, one
        // baseline, the second run butted against the first at its own
        // advance — so the pixels are exactly the single string's — but two
        // clip fates, because the two texts do not answer to the same rule:
        //   * THE CLOCK IS NEVER CLIPPED. It paints from the cell's origin
        //     exactly as it does with no state standing, so making a state
        //     string appear can never take a digit away, and the row's
        //     crop-at-its-floor allowance (the block above) is what covers the
        //     narrow window where the right block has already walked over this
        //     ground — the same allowance, unchanged, in both cases.
        //   * THE STATE IS CLIPPED FROM ITS OWN START X to the bound, and a
        //     start already at or past the bound leaves it ABSENT: no clip is
        //     installed on a degenerate or negative room and nothing is
        //     painted, rather than the whole run escaping onto the marker
        //     verbs. A state with no room is simply not shown.
        // (Both faults were one expression until this conversion: the clip ran
        // from the CLOCK's origin, so a narrow-but-honest window cut digits
        // off the clock, and its `x1 > x0` guard turned a bound left of the
        // cell into NO clip at all — the collision it exists to prevent.)
        // THE ROW YIELDS WHOLE TO A MODAL,
        // so this text is hidden while a prompt, a dialog editor, the render
        // player, the picker or the AV Sync Stats panel stands
        // (architect-accepted at the fold; the
        // one-day status BAR painted through a modal, that being what a
        // separate lane buys). It is why the render player's
        // load-under-a-running-render refusal says its sentence on a CARD: the
        // explanation this cell would have carried is not on screen under that
        // modal.
        std::string state;
        if (app.history_mode.active) {
            state = history_walk_line(app);
        } else {
            // The process line — the render/batch/queue status, the startup
            // "Loading..." line and the running mirror's, composed above and
            // empty when there is nothing to report. It is one of the reasons
            // this body runs on every frame class (it is the only feedback on
            // the loading frame).
            state = process_line_text(app, external_sync_worker);
        }
        const double x0 = static_cast<double>(cell_x);
        // THE CLOCK, UNCLIPPED, and its advance is where the state begins.
        const double clock_w = show_row_text(cr, font, x0, baseline,
                                             format_timestamp(seconds),
                                             kRedesignLabel);
        if (!state.empty()) {
            // THE SEPARATION IS A CHARACTER — the literal " | " leading the
            // state's own run, which is why the two runs read as the one
            // string the ruling asks for.
            const double state_x = x0 + clock_w;
            const double x1      = static_cast<double>(right_block_x - pad);
            if (x1 > state_x) {
                cairo_save(cr);
                cairo_rectangle(cr, state_x, static_cast<double>(content_y),
                                x1 - state_x, static_cast<double>(content_h));
                cairo_clip(cr);
                show_row_text(cr, font, state_x, baseline, " | " + state,
                              kRedesignLabel);
                cairo_restore(cr);
            }
        }
    }

    cairo_restore(cr);
}


// -- The floating surfaces ---------------------------------------------------

void GuiPaintHandler::paint_popup_chrome(cairo_t* cr, const GuiRect& r,
                                         GuiColor ground, GuiColor border) {
    // ONE BOX SHAPE FOR BOTH FLOATING SURFACES, TWO COLOR PAIRS. The tooltip and
    // the dropdown are built the same way and dressed differently — kdenlive
    // gives its menus a darker ground and a softer border than its tooltips, and
    // each crop pinned its own pair — so the colors are the caller's and the
    // geometry is the shared face box (redesign_face_box — one path, filled
    // then stroked, the half-stroke inset rule stated there).
    const int    lw  = popup_border_px();
    const double rad = std::nearbyint(kPopupCornerRadiusPx * gui_scale_factor());
    redesign_face_box(cr, r.x, r.y, r.w, r.h, lw, rad, &ground, &border);
}

void GuiPaintHandler::paint_shift_tooltip(cairo_t* cr) {
    // THE HOVER TOOLTIP, on whichever button the dwell belongs to — at most
    // one, because at most one button is under the pointer. The tick owns
    // WHEN it appears (the dwell); this owns only what it looks like, and
    // publishes the rect it painted so the hide edge can damage it.
    app.redesign_tooltip.rect = GuiRect{0, 0, 0, 0};
    if (!app.redesign_tooltip.visible) return;

    // THE DWELL'S OWN OWNER IS THE SUBJECT, read rather than re-derived: the
    // input side decided which button the hint belongs to when it stamped the
    // clock (the two hover walks, one per surface), and `visible` is only ever
    // set for a stamp, so an owner is always standing here. Re-walking for a
    // hovered button would be a SECOND membership rule to keep in step with
    // that one — and since 2026-08-07 it could not be the same rule anyway: A
    // DISABLED BUTTON SHOWS ITS HINT (the architect's kdenlive-parity ruling)
    // while it never sets the hover FACE, so `hovered` no longer names the
    // tooltip's subject.
    const AppState::RedesignTooltip::Owner owner = app.redesign_tooltip.owner;
    if (owner.index < 0) return;

    // THE TWO SURFACES (the encoding is at the field): a ROSTER owner reads
    // the roster's constant/stateful hint table and the roster's painted rect;
    // a DIALOG owner reads the modal stash the painter itself publishes — its
    // composed hint and its button rect, both written by paint_modal_dialog,
    // which runs BEFORE this body precisely so the rect this hangs off is the
    // one this frame draws.
    const char* line1 = nullptr;
    const char* line2 = nullptr;
    GuiRect     btn{0, 0, 0, 0};
    bool        flip_above = false;
    if (owner.surface == AppState::RedesignTooltip::Surface::Dialog) {
        const AppState::ModalDialogGeometry& dlg = app.modal_dialog;
        if (!dlg.valid ||
            owner.index >= static_cast<int>(dlg.buttons.size())) {
            return;
        }
        const AppState::ModalDialogButton& b =
            dlg.buttons[static_cast<size_t>(owner.index)];
        if (b.tooltip.empty()) return;
        line1 = b.tooltip.c_str();
        // THE MODIFIER LINE, on the buttons that have one (2026-08-28, R37 —
        // the player's two skips): the roster branch's own two-line form,
        // reached here through the published pair instead of a table, since a
        // modal button's words are the painter's to compose.
        if (!b.tooltip2.empty()) line2 = b.tooltip2.c_str();
        btn   = b.rect;
        // The modal is the BOTTOM ROW, so its hints always hang UPWARD — the
        // same flip the row's own tenants take, for the same reason (the lane
        // rests on the window's foot, so there is nothing below it for a hint
        // to hang in); their membership is
        // redesign_button_in_transport_row, which the roster branch below
        // reads.
        flip_above = true;
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
        // a standing dwell any more — tooltip MEMBERSHIP is the menu row and
        // nothing else, in every state (the rule is at redesign_button_tooltip,
        // app_state.h), and the dwell writer only ever stamps a button whose
        // line 1 is non-null. So this reads as the table's total answer rather
        // than as a state guard, and it keeps the painter honest against the
        // stamp without knowing which arms are null.
        if (text.line1 == nullptr) return;
        line1      = text.line1;
        line2      = text.line2;
        btn        = app.redesign_buttons[owner.index].rect;
        flip_above = redesign_button_in_transport_row(id);
    }
    if (btn.w <= 0 || btn.h <= 0) return;

    cairo_save(cr);
    gui_select_font_face(cr, GuiFontFamily::Sans);

    // TWO SIZES ON ONE CONTEXT, IN TWO PHASES — MEASURE, THEN PAINT — and the
    // phase split is not tidiness, it is the chokepoint's stated precondition:
    // show_shaped_run must run with THE SAME scaled font set on `cr` that shaped
    // the run (text_shape.h). Shaping both lines up front and then painting both
    // would leave the SECOND size on the context while the FIRST line's glyphs
    // were emitted — its 12pt-shaped positions rendered at 10pt, which is a
    // wrong-size line with mis-spaced glyphs, exactly the mismatch signature the
    // contract warns about. So each line's size is (re-)set immediately before
    // its own paint, below.
    //
    // The SIZE, not the font POINTER, is what is carried between the phases:
    // cairo_get_scaled_font returns a borrowed reference that a later
    // cairo_set_font_size releases, so a pointer held across a size change is not
    // ours to use. Each phase re-fetches; only plain doubles cross.
    const double size1 = redesign_font_size_px();
    const double size2 =
        kTooltipShiftFontSizePt * 96.0 / 72.0 * gui_scale_factor();
    const bool two_line = (line2 != nullptr);

    cairo_set_font_size(cr, size1);
    cairo_scaled_font_t* f1 = cairo_get_scaled_font(cr);
    const text_shape::ShapedRun r1 = text_shape::shape_text_run(f1, line1);
    cairo_font_extents_t fe1;
    cairo_scaled_font_extents(f1, &fe1);
    const double band1 = fe1.ascent + fe1.descent;

    double band2 = 0.0, w2 = 0.0;
    text_shape::ShapedRun r2;
    if (two_line) {
        cairo_set_font_size(cr, size2);
        cairo_scaled_font_t* f2 = cairo_get_scaled_font(cr);
        r2 = text_shape::shape_text_run(f2, line2);
        cairo_font_extents_t fe2;
        cairo_scaled_font_extents(f2, &fe2);
        band2 = fe2.ascent + fe2.descent;
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

    // BELOW THE BUTTON, LEFT-ALIGNED WITH IT — or ABOVE it for every BOTTOM-ROW
    // owner, whose lane rests ON the foot of the window (since the relayout's
    // commit B, apart from the one day a STATUS BAR stood under it; the blank
    // foot's own band, zero on a short window, before that): there is nothing
    // below them at all, so a hint dropped there would fall off the window and
    // it hangs upward instead, the
    // same box flipped about the button. That covers BOTH bottom-row surfaces —
    // the row's eighteen roster buttons and, since 2026-08-13, the modal's own,
    // which paint in the same lane (the fork was resolved with the owner,
    // above). Then CLAMPED
    // FULLY ON-WINDOW so a
    // button near an edge cannot push it off. The clamp is a pure position fix —
    // the box never shrinks, because a truncated hint would be worse than one
    // that shifted.
    int x = btn.x;
    int y = flip_above ? btn.y - h : btn.y + btn.h;
    if (x + w > app.width)  x = app.width - w;
    if (x < 0) x = 0;
    if (y + h > app.height) y = app.height - h;
    if (y < 0) y = 0;
    app.redesign_tooltip.rect = GuiRect{x, y, w, h};

    paint_popup_chrome(cr, app.redesign_tooltip.rect, kRedesignRowGround,
                       kRedesignLine);

    // THE PAINT PHASE. Each line RE-SETS its own size first, so the context
    // carries the very font that shaped the run it is about to emit — the
    // chokepoint's precondition, honored per line. Line 1 is therefore
    // byte-identical in face, size and paint to the one-line form's line, which
    // is the whole point: the two forms differ by an added line, not by anything
    // about the first one.
    //
    // Each line sits on ITS OWN band, so each baseline is that band's foot — the
    // shared centrer applied to a slot that is exactly the band, which makes the
    // symmetry above true of the ink and not merely of the arithmetic.
    cairo_set_font_size(cr, size1);
    cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                         kRedesignLabel.b);
    text_shape::show_shaped_run(
        cr, r1, static_cast<double>(x + pad_x),
        redesign_baseline(cairo_get_scaled_font(cr),
                          static_cast<double>(y + pad_y), band1));
    if (two_line) {
        cairo_set_font_size(cr, size2);
        // The hint line is DIMMED by the one measured factor, uniformly.
        const GuiColor dim =
            mix_color(kRedesignLabel, kRedesignRowGround, kRedesignDimMix);
        cairo_set_source_rgb(cr, dim.r, dim.g, dim.b);
        text_shape::show_shaped_run(
            cr, r2, static_cast<double>(x + pad_x),
            redesign_baseline(cairo_get_scaled_font(cr),
                              static_cast<double>(y + pad_y) + band1 + gap,
                              band2));
    }

    cairo_restore(cr);
}

namespace {

// THE CARD'S LINE SPACING: the sans face's OWN height at the redesign size,
// asked of the same scaled font the lines are shaped and painted with, so a
// wrapped card's rows sit exactly as the face intends them to. Read ONCE per
// paint and handed down — it is the only thing that turns a line count into
// a card height.
int notification_line_h_px(cairo_scaled_font_t* font) {
    cairo_font_extents_t fe;
    cairo_scaled_font_extents(font, &fe);
    return static_cast<int>(std::nearbyint(fe.height));
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
        cairo_scaled_font_t* font, const std::string& text,
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
// screen since the queue retired 2026-08-30 — painted top-right under ROW 1's
// view radios, right-aligned
// at kPanelPadPx from the window's edge and the same kPanelPadPx below row 1
// (the stack's margins are one number, notification_stack_bound), growing
// DOWN over whatever lies there (the tab row's right stretch, the icon row's
// empty right, the thin lanes, the waveform), the cards kIconBtnGapPx apart.
//
// THE LOOK (his picked mockup, tmp/previous/messaging_mockups/cards_AB.png's
// look 1; the chrome record at render.h's palette block): the player's dark
// ground under the popup's 1 px border through the one popup box painter;
// a row of the icon row's own height, holding — left to right, EVERY
// DISTANCE THE CARD'S ONE PAD (notification_pad_px, the ruling at its
// declaration: the box's own vertical margin, read for all six) — a 32 px
// button box with the CLASS GLYPH centred at the box's
// own inset (dialog-information for a normal card, dialog-error for a
// critical one, each in its file's own colours: the roster paints every
// path in the table's ink and colours nothing here — the two files are a
// blue or red plate under a white glyph, and that plate is what tells the
// classes apart at a glance), that pad, THE SENTENCE
// of the one sans in the row's ink, that pad again, and the
// window-close X in a second button box at that pad from the right edge —
// the boxes sitting that same pad below the card's top and above its foot.
// The card's width
// is its content's, clamped to [kNotificationMinWidthPx,
// notification_card_max_w_px] — the authored ceiling since 2026-08-31, scaled
// like every other length, with the window itself as a safety under it. THE X WEARS THE ICON BUTTON'S HOVER FACE while the
// pointer rests in its box — the 1 px accent outline through the shared face
// box, nothing else — and the card's body wears none.
//
// THE TEXT GROWS DOWNWARD UNDER THE TWO ICONS (architect 2026-08-30). The
// width rule is unchanged — the WHOLE sentence's shaped width sets it, inside
// the same clamp — so a sentence that fits its room is one line exactly as it
// was; a longer one wraps through notification_text_lines above to at most
// kNotificationMaxLines, the last of which clips at the run's right edge (no
// ellipsis — the folder overlay rows' precedent). THE CARD GROWS, NOTHING
// REFLOWS: the glyph box and the X box keep the one-line card's placement,
// the first line keeps the one-line card's baseline, each further line sits
// one face line-height lower, and the air under the last line is the first
// line's own by construction — the one pad still rules every edge. The
// stack's `y` then advances by each card's OWN height.
//
// IT PUBLISHES WHAT IT DREW: each card's rect and X box, and their union,
// into AppState::Notifications (the owner-tag doctrine at
// ModalDialogGeometry — published geometry may only SELECT; the press claim,
// the cursor map and the hover walk read this and then ask the live stack).
// EVERYTHING IS CLIPPED TO THE ROOM and PUBLISHED CLIPPED TO IT: a stack tall
// enough to reach the bottom row is the contrived case nothing caters for,
// and clipping is what keeps it from painting over that row or claiming a
// press under it. It runs on every frame, cards or none, for the floating
// surfaces' reason: a skipped run would strand a stale publication. Its
// damage is not its own — every stack change damages the stack's ROOM through
// the viewport owner, and the hover its X box.
void GuiPaintHandler::paint_notifications(cairo_t* cr) {
    AppState::Notifications& st = app.notifications;
    st.painted.clear();
    st.painted_rect = GuiRect{0, 0, 0, 0};
    if (st.cards.empty()) return;

    // THE ROOM the stack grows into, and the clip for everything below. A
    // window with none of it paints no card at all (notifications.h).
    const GuiRect room = notification_stack_bound(app);
    if (room.w <= 0 || room.h <= 0) return;

    cairo_save(cr);
    gui_select_font_face(cr, GuiFontFamily::Sans);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);
    cairo_rectangle(cr, room.x, room.y, room.w, room.h);
    cairo_clip(cr);

    const int line1_h  = notification_card_h_px();
    const int line_h   = notification_line_h_px(font);
    const int max_w    = notification_card_max_w_px(app);
    const int min_w    = scaled_px(kNotificationMinWidthPx);
    const int gap      = scaled_px(kIconBtnGapPx, 1);
    const int btn      = scaled_px(kIconBtnPx);
    const int glyph_px = scaled_px(kIconGlyphPx);
    // THE GLYPH INSET IS AN INTEGER HALF AND SITS ONE PIXEL LEFT OF CENTRE
    // WHERE THE DIFFERENCE IS ODD (recorded 2026-09-02): both terms are
    // already scaled, so `btn - glyph_px` is even at 100 % and at the tablet's
    // 225 % (the two scales anyone runs) and odd at some others — 150 % and
    // 350 % among them — where this truncating division loses the half pixel
    // to the left and the top. It is the same recipe every icon box uses in
    // this file, deliberately shared; a rounded half would move every glyph in
    // the product to fix a sub-pixel bias at scales the architect does not
    // run.
    const int inset    = (btn - glyph_px) / 2;
    // ONE NUMBER FOR ALL SIX DISTANCES (architect 2026-08-30): the boxes'
    // vertical margin is the card's every pad, so the horizontal placement
    // cannot disagree with the vertical centering it is taken from.
    const int pad      = notification_pad_px();
    const int lw       = std::max(1, scaled_px(kIconOutlineStrokePx));
    const double radius = std::nearbyint(kIconCornerRadiusPx *
                                         gui_scale_factor());
    const int right_x = room.x + room.w;
    // The chrome every card carries besides its text: FOUR pads (the two
    // edges and the two sides of the text) and two boxes.
    const int chrome_w = 4 * pad + 2 * btn;
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
        // width leaves between the two boxes.
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
        const int close_x   = card_x + w - pad - btn;
        const int text_x    = glyph_x + btn + pad;
        const int text_room = close_x - pad - text_x;
        const std::vector<text_shape::ShapedRun> lines =
            notification_text_lines(font, n.text, whole,
                                    static_cast<double>(text_room));
        // THE CARD GROWS DOWNWARD BY WHOLE LINES; its first line is a
        // one-line card and the boxes and the first baseline sit in it.
        const GuiRect card{
            card_x, y, w,
            line1_h + (static_cast<int>(lines.size()) - 1) * line_h};
        paint_popup_chrome(cr, card, kModalFieldGround, kRedesignTabLine);

        const int box_y = card.y + pad;
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
            cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                                 kRedesignLabel.b);
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

        const GuiRect close{close_x, box_y, btn, btn};
        if (st.hovered_id == n.id && st.close_hovered) {
            const GuiColor line = kRedesignAccent;
            redesign_face_box(cr, close.x, close.y, btn, btn, lw, radius,
                              nullptr, &line);
        }
        icons::draw(cr, icons::Icon::WindowClose,
                    static_cast<double>(close.x + inset),
                    static_cast<double>(close.y + inset),
                    static_cast<double>(glyph_px));

        const GuiRect shown = in_room(card);
        st.painted.push_back(
            AppState::NotificationPainted{n.id, shown, in_room(close)});
        st.painted_rect = st.painted.size() == 1
                              ? shown : union_rect(st.painted_rect, shown);
        y += card.h + gap;
    }
    cairo_restore(cr);
}

void GuiPaintHandler::paint_dropdown(cairo_t* cr) {
    // THE MENU ROW'S DROPDOWN — ONE painter for EVERY menu, hanging flush under
    // the button that emits it at ZERO margin: its top edge is the BUTTON's
    // bottom edge (not the lane's: row 1's 1px margin-bottom puts those one pixel
    // apart, and the architect ruled the button; the full argument is at the
    // anchor arithmetic below), under that button's left edge. Publishes its own
    // rect and every item rect, so the press claim hit-tests exactly what was
    // painted and never re-shapes a label.
    //
    // ITS CHROME IS ITS OWN (kRedesignPopupGround under kRedesignTabLine), not
    // the tooltip's: kdenlive dresses menus darker than hints, and dropdown_full
    // is the authority for this surface.
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
    // accelerator COLUMN, which the three COMMAND menus (File, Edit since
    // 2026-08-20 and Help since 2026-09-03) have and Settings does not. The
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
    // Geometry was untouched on any menu, a greyed row still occupying its slot
    // at its full height, which is why nothing about the layout below moved when
    // the arms left. The record is at kFilePopupItems, app_state.h.)
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
    gui_select_font_face(cr, GuiFontFamily::Sans);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);

    const int border    = popup_border_px();
    const int item_h    = popup_item_h_px();
    const int block_mar = popup_item_margin_y_px();
    const int inset     = scaled_px(kPopupItemInsetPx);
    const int sep_inset = scaled_px(kPopupSepInsetPx);
    const int sep_mar   = popup_sep_margin_y_px();
    const int sep_block = 2 * sep_mar + border;   // margin, line, margin
    const int lw        = border;
    const double radius = std::nearbyint(kPopupCornerRadiusPx *
                                         gui_scale_factor());

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
    // label indent, the widest label, then — only where an accelerator column
    // exists — the guaranteed column gap and the widest accelerator, then the
    // right margin, less the chrome the item box adds back below.
    //
    // THE OPTIONAL TERM IS DRIVEN OFF THE ITEM TABLE, not off the menu
    // enumerator: an accelerator column is a property of the rows (a menu whose
    // rows carry no hotkey has none), and expressing it that way is what leaves
    // the menus differing by one DERIVED term instead of by hand-written
    // rules per menu — which is exactly what let the FILE menu land in 2026-08-13
    // with an accelerator column and no edit in this body at all.
    //
    // The authored minimum applies to both — it is the item box's floor and the
    // reason a menu of short labels still reads as a menu. It gave the FILE
    // menu its whole width while its rows were short, and stopped doing so on
    // 2026-08-27, when "Synchronize to External Storage" joined its rows and
    // its own content won; the expression is unchanged either way, the floor
    // and the shaped run simply trading which one wins. (The deleted NAVIGATION
    // menu's long labels never reached the floor either, which is how the two
    // were shown to compose.)
    const int pad_l    = scaled_px(kPopupLabelIndentPx);
    const int gap      = scaled_px(kPopupHotkeyGapPx);
    const int pad_r    = scaled_px(kPopupPadRightPx);
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

    // FLUSH WITH THE BUTTON IT EMITS FROM, on BOTH axes (architect 2026-08-02).
    // When row 1 gained its 1px margin-bottom the button's bottom edge and the
    // lane's stopped being the same row of pixels, and the anchor briefly moved
    // to the LANE on the reading that "the menu row's bottom edge" meant the
    // whole lane. He ruled the button: the dropdown hangs off the thing that
    // opened it. So the box's top edge lands on row 1's MARGIN STRIP and covers
    // it for as long as the menu is up — that is the ruled look, not a leak.
    int x = btn.x;
    int y = btn.y + btn.h;               // flush: zero margin under the button
    if (x + w > app.width) x = app.width - w;
    if (x < 0) x = 0;
    app.dropdown.rect = GuiRect{x, y, w, h};

    paint_popup_chrome(cr, app.dropdown.rect, kRedesignPopupGround,
                       kRedesignTabLine);

    // The item block opens BELOW the border by its own margin, and closes with
    // the same margin above the bottom border — the crop's 3px, mirrored.
    int iy = y + border + block_mar;
    for (int i = 0; i < count; ++i) {
        const DropdownRow row = dropdown_row(menu, i);
        if (row.separator_before) {
            // 1px line, inset horizontally, with its own vertical margin against
            // the item on each side. Pixel-bound fill, crisp by construction.
            cairo_set_source_rgb(cr, kRedesignTabLine.r, kRedesignTabLine.g,
                                 kRedesignTabLine.b);
            cairo_rectangle(cr, x + sep_inset, iy + sep_mar,
                            w - 2 * sep_inset, border);
            cairo_fill(cr);
            iy += sep_block;
        }
        // ITEMS TOUCH — zero vertical gap between adjacent ones — and each
        // one's box insets horizontally from the border. The published rect is
        // that box, so the clickable area is exactly the area that lights.
        const GuiRect item{x + inset, iy, item_w, item_h};
        app.dropdown.item_rects[static_cast<size_t>(i)] = item;

        // (A DISABLED ROW KEPT ITS GEOMETRY AND LOST ITS FACES, 2026-08-08 to
        // 2026-08-15: the rect above was published exactly as a live row's is —
        // the row still occupied its slot, and the width, the layout and the item
        // block's height knew nothing about it — and what changed was that no
        // hover or press face was drawn and the two inks swapped to their sampled
        // dim pair. The input side already refused to hover or arm one, so the
        // test here was the SAME predicate read a second time rather than a
        // second rule: the face and the press could not disagree about a row,
        // which is the roster's disabled-button doctrine one surface out. The
        // predicate went producer-less with the Navigation menu and the two
        // terms below are unconditional again.)
        const bool pressed = (app.dropdown.pressed_item == i);
        const bool hovered = (app.dropdown.hovered_item == i);
        if (pressed || hovered) {
            // TWO FACES FROM THE ITEM CROPS, and they are built differently
            // because one has an outline and the other does not:
            //   PRESSED — the FULL accent fill over the WHOLE item box. No
            //     stroke, so no inset: the fill's own edge is the visible edge.
            //     It is visible at all only because items act on RELEASE —
            //     the redesign's first such surface, and since 2026-08-13 the
            //     whole chrome roster's rule.
            //     EXACTLY ONE ITEM IS EVER LIT, and that is the input side's
            //     doing rather than a rule here: the ARM FOLLOWS THE POINTER
            //     while a press is live, so the pressed and hovered indices are
            //     the same item then and this test simply picks the face —
            //     pressed while the button is down, hovered when it is not.
            //     Nothing distinguishes an item the pointer has left.
            //   HOVERED — the same 30% accent tint the click faces use, over
            //     THIS popup's ground, under a 1px outline of the accent
            //     lightened 15% toward white. Fill and stroke share ONE INSET
            //     path so they describe one rectangle.
            // The half-stroke inset on the hovered form makes the two faces the
            // SAME SIZE on screen (both outer edges land at `radius` from the
            // box corner), which is what the crops show — each saturates in the
            // same column.
            if (pressed) {
                cairo_set_source_rgb(cr, kRedesignAccent.r, kRedesignAccent.g,
                                     kRedesignAccent.b);
                redesign_rounded_rect_path(cr, item.x, item.y,
                                           static_cast<double>(item.w),
                                           static_cast<double>(item.h), radius);
                cairo_fill(cr);
            } else {
                const GuiColor fill = mix_color(kRedesignAccent,
                                                kRedesignPopupGround,
                                                kRedesignClickMix);
                const GuiColor line = mix_color(GuiColor{1.0, 1.0, 1.0},
                                                kRedesignAccent,
                                                kRedesignHoverLightenMix);
                // The shared face box (redesign_face_box — the half-stroke
                // inset rule lives there).
                redesign_face_box(cr, item.x, item.y, item.w, item.h, lw,
                                  radius, &fill, &line);
            }
        }

        // LEFT-ALIGNED AT THE ONE INDENT, measured from the POPUP box's own left
        // edge in every menu, and vertically centred by the shared solver. On
        // the settings menu the right side carries the leftover, which the
        // minimum width above is what guarantees; on a COMMAND menu the
        // accelerator carries it.
        const double base = redesign_baseline(font,
                                              static_cast<double>(item.y),
                                              static_cast<double>(item.h));
        cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                             kRedesignLabel.b);
        text_shape::show_shaped_run(cr, runs[i],
                                    static_cast<double>(x + pad_l), base);

        // THE ACCELERATOR COLUMN is RIGHT-ALIGNED to the popup's own right
        // margin, not to the item box's: the margin is a fact about the box the
        // crop measured, and aligning to it keeps every hotkey's last ink column
        // on one line whatever the item inset is. Its ink is the sampled dim
        // (kRedesignPopupHotkey), in every face — the item's fill is the whole
        // hover/press cue, as it is for the label. (A DISABLED row swapped it
        // for the sampled dim pair's other half, keeping the accelerator dimmer
        // than its own label exactly as the live pair does; both derivations are
        // recorded at render.h's palette block, where the retired pair lives.)
        //
        // ITS PRODUCERS ARE TABLE-DRIVEN, not one row: the column was authored
        // for the Navigation menu's seven rows, and once that menu retired
        // (2026-08-15) File's "Ctrl+Q" carried it alone for a time — the reach
        // has widened since with every popup item table that sets a hotkey
        // string (File's "Ctrl+O", 2026-08-28, and its chord-less "\\" for
        // Synchronize, 2026-08-31; Edit's five propagate chords, 2026-08-20), so
        // the optional term above is exercised by whichever live menu's rows
        // carry one — which is the whole reason the term was driven off the
        // item TABLE rather than off the menu enumerator or a single row.
        if (row.hotkey != nullptr) {
            const double hot_x =
                static_cast<double>(x + w - pad_r) -
                std::nearbyint(hot_runs[i].width_px);
            cairo_set_source_rgb(cr, kRedesignPopupHotkey.r,
                                 kRedesignPopupHotkey.g,
                                 kRedesignPopupHotkey.b);
            text_shape::show_shaped_run(cr, hot_runs[i], hot_x, base);
        }
        iy += item_h;
    }

    cairo_restore(cr);
}

// -- THE RULER LANE (top lane 5, row 5 of the redesign) ---------------------
//
// A LOOK/MODEL SPLIT, and it is deliberate: the ruler takes KDENLIVE'S LOOK and
// REAPER'S GEOMETRY MODEL (architect 2026-08-01).
//   LOOK, from row_5_full.png: 1px #737373 ticks, a #c2c2c2 label at the
//     redesign's ordinary 12pt, and the two tick lengths differing at their TOP
//     — majors rise 4px above the marker lane, minors start at it, and BOTH run
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
// its labels at least 8x that apart.
constexpr double kRulerMinMinorPitchPx = 12.0;
// The label band's top padding; with the 12pt band this lands the baseline on
// the composite's own label rows.
constexpr double kRulerLabelPadTopPx   = 4.0;
// How far a MAJOR tick rises above the marker lane. Minors rise none.
constexpr double kRulerMajorRisePx     = 4.0;

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

} // namespace

void GuiPaintHandler::paint_ruler_row(cairo_t* cr) {
    const GuiRect lane   = top_ruler_row_area(app);
    const GuiRect marker = top_marker_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;

    cairo_save(cr);
    cairo_set_source_rgb(cr, kRedesignContentGround.r,
                         kRedesignContentGround.g,
                         kRedesignContentGround.b);
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
    // tick-crossing recording must be the width that spp was published AGAINST:
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
    // placement of each minor tick and the head's float re-derivation of which
    // columns carried one — and the rigid comb replaced both with integer
    // distribution across a segment. The MINORS-PER-STEP count is still the
    // ladder's own kRulerMinorsPerStep; only its expression as a duration is
    // gone.)

    const int tick_bottom = marker.y + marker.h;         // the waveform top
    const int minor_top   = marker.y;                    // no rise
    const int major_top   = marker.y - scaled_px(kRulerMajorRisePx);

    gui_select_font_face(cr, GuiFontFamily::Sans);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);
    cairo_font_extents_t fe;
    cairo_scaled_font_extents(font, &fe);
    const double baseline =
        redesign_baseline(font,
                          static_cast<double>(lane.y) +
                              std::nearbyint(kRulerLabelPadTopPx *
                                             gui_scale_factor()),
                          fe.ascent + fe.descent);

    // SUB-SECOND EMPHASIS: while the step is finer than a second, the WHOLE
    // SECONDS are the landmarks, so they take the brighter label white while
    // every other label stays the ruler's dim grey. One color swap, no second
    // type size — the simplest reproduction of Reaper's emphasis that survives
    // at every scale.
    const bool emphasize = step < 1000;

    // THE PLAYHEAD HEAD'S TICK-CROSSING WINDOW, declared before the walk because
    // the walk fills it: one byte per column across the head's widest row —
    // 0 = no tick, 1 = minor, 2 = major. The head block below repaints exactly
    // these columns in the pre-blended value, so the crossing is the ticks the
    // walk actually painted rather than a second derivation of where ticks ought
    // to be (which is what it was, and what the rigid comb would have made
    // wrong).
    //
    // FIXED CAPACITY, no allocation: kPlayheadHeadHalf[0] is 9 authored px and
    // the gui_scale schema caps at 400%, so the window is at most 2*36+1 = 73
    // columns. 80 is headroom; the recording clamps to it, so a raised ceiling
    // would lose the pre-blend at the outermost head columns rather than write
    // out of bounds — which is what 48 (the 200%-ceiling number) would have
    // done from 2026-08-26, when the schema's ceiling went 200 -> 400.
    constexpr int kHeadTickWindowCap = 80;
    std::array<uint8_t, kHeadTickWindowCap> head_ticks{};
    const int head_half_max = playhead_head_half_px(0, gui_scale_factor());
    const double head_px_pre = playhead_pixel_x(
        app, static_cast<int64_t>(basis.vp_start), basis.spp);
    const int head_cursor_col = static_cast<int>(std::nearbyint(head_px_pre));
    int head_window = 2 * head_half_max + 1;
    if (head_window > kHeadTickWindowCap) head_window = kHeadTickWindowCap;
    const int head_col0 = head_cursor_col - head_half_max;

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
    // keeps at least 96px wide. It NEVER ACCUMULATES: every segment re-anchors
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
    for (int64_t k = first_step; ; ++k) {
        const double step_ms = static_cast<double>(k) * static_cast<double>(step);
        if (step_ms > end_ms) break;
        const int mx    = major_col(k);
        const int seg_w = major_col(k + 1) - mx;
        for (int i = 0; i < kRulerMinorsPerStep; ++i) {
            // THE INTEGER COMB, and the only culling test there is: a tick is at
            // its distributed column or it is offscreen. The old float-time
            // pre-filter went with the float positions it filtered.
            const int col = (i == 0)
                ? mx
                : mx + static_cast<int>(std::nearbyint(
                      static_cast<double>(i) * static_cast<double>(seg_w) /
                      static_cast<double>(kRulerMinorsPerStep)));
            if (col < 0 || col >= wave_w) continue;
            const bool major = (i == 0);
            // Record the crossing for the playhead head below, which repaints
            // these exact columns in the pre-blended value. Recording what the
            // walk PAINTS is what keeps head and ticks one source of truth; the
            // head used to re-derive them from the float minor grid, which now
            // no longer describes where the ticks are.
            if (head_window > 0) {
                const int w_idx = col - head_col0;
                if (w_idx >= 0 && w_idx < head_window)
                    head_ticks[static_cast<size_t>(w_idx)] = major ? 2 : 1;
            }
            cairo_set_source_rgb(cr, kRulerTick.r, kRulerTick.g, kRulerTick.b);
            cairo_rectangle(cr, lane.x + col, major ? major_top : minor_top,
                            1, tick_bottom - (major ? major_top : minor_top));
            cairo_fill(cr);
            if (!major) continue;
            // The label sits just right of its own major tick, so the number and
            // the line it names cannot drift apart.
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
            const GuiColor c = (emphasize && (label_ms % 1000) == 0)
                                   ? kRedesignLabel : kRulerLabel;
            cairo_set_source_rgb(cr, c.r, c.g, c.b);
            text_shape::show_shaped_run(cr, run,
                                        static_cast<double>(lane.x + col + 3),
                                        baseline);
        }
    }

    // -- THE PLAYHEAD HEAD, the successor to the retired cursor triangle -----
    //
    // IT LIVES IN THIS PAINTER because it must know where the TICKS are: where a
    // tick's column crosses the head, those head pixels take a PRE-BLENDED
    // constant rather than the head's own grey, and the opaque-palette doctrine
    // has no compositing to do that with. Ticks and head therefore share one
    // owner and the crossing is exact instead of approximated.
    //
    // ALIASED BY CONSTRUCTION: the shape is a transcribed per-row HALF-WIDTH
    // table (kPlayheadHeadHalf), painted as integer rectangles — one per row —
    // so it has hard edges at every scale, which a path fill would not. At
    // gui_scale > 100% each source row becomes `s` device rows and each
    // half-width scales with it, which enlarges the pixel steps rather than
    // smoothing them: the shape stays the drawing it was transcribed from.
    //
    // TIP-DOWN ON THE MARKER LANE'S BOTTOM ROWS, its tip ON the waveform
    // boundary, centered on the playhead column — the same column the stem below
    // runs on, so head and stem are one object.
    //
    // THE HEAD MOVED OUT OF THE RULER LANE (architect 2026-08-01, at the row-5
    // live test). It sat on the ruler's bottom rows; it now occupies the MARKER
    // lane's BOTTOM rows (the relocation's first shape put it at that lane's TOP
    // and the architect amended it the same day, for the stem parity the band
    // block below states), and the ruler lane is labels + tick-tops only. The point
    // of the move is OCCLUSION: the flag blit follows this pass, so a marker
    // sharing the cursor's column now covers part of the head — the accepted
    // look, and the hidden-by-marker model reaching the head itself rather than
    // only the stem below it.
    //
    // IT STAYS IN THIS PAINTER even though it no longer paints in this painter's
    // own lane, and for the reason it was here to begin with: the tick-crossing
    // pre-blend needs the tick columns, and the ticks are walked here. Moving
    // the head to a marker-lane painter would split one object across two
    // owners and re-derive the tick grid a second time.
    //
    // THE HEAD'S TIP STANDS ON THE WAVEFORM BOUNDARY, so the cursor's stem
    // begins exactly where the head ends: render_playhead's waveform segment
    // (paint_playheads) starts at the waveform's top row, and head_bottom below
    // IS that row, so head and stem meet with no gap and no lane pixel between
    // them — one unbroken line across the seam, with no other playhead pixel in
    // this lane. The pass order does the rest: ticks, then the head, then the
    // flag blit over both (the hidden-by-marker model reaches the head too).
    // The stem's WAVEFORM segment and the marker stems are already down by the
    // time this pass runs, in a band this one never touches (the sequence is
    // the paint-order block in on_redraw).
    {
        const double cursor_px = playhead_pixel_x(
            app, static_cast<int64_t>(basis.vp_start), basis.spp);
        const int col = static_cast<int>(std::nearbyint(cursor_px));
        if (col >= 0 && col < wave_w) {
            const double s   = gui_scale_factor();
            // THE ROW COUNT NEEDS NO FLOOR: 12 authored rows reach 6 at the
            // schema's own bottom (gui_scale 50), and only a factor below 1/24
            // could empty the loop — outside the vocabulary entirely. The
            // per-row HALF-WIDTH is where the floor lives (render.h).
            const int    rows = static_cast<int>(std::nearbyint(
                                    kPlayheadHeadHeightPx * s));
            // THE BAND IS THE MARKER LANE'S BOTTOM `rows`, tip ON the waveform
            // boundary (architect 2026-08-01, amending the first relocation,
            // which put it at the lane's top). At 100% that is rows 45..56 of a
            // 37..56 lane, with the waveform starting at 57.
            //
            // THE POINT IS STEM PARITY: the playhead's stem begins exactly
            // where every marker stem begins — the waveform top — so the two
            // read as the same object at the same length. head_bottom IS the
            // waveform top, which is what hands the line off to
            // render_playhead's waveform segment with no lane pixels in
            // between.
            const int    head_bottom = marker.y + marker.h;
            const int    head_top    = head_bottom - rows;
            for (int r = 0; r < rows; ++r) {
                // Each device row reads its SOURCE row's half-width through the
                // ONE silhouette accessor (playhead_head_half_px, render.h),
                // which also owns the tip's floor of 1 — shared verbatim with
                // the tick pre-blend below, so the two cannot disagree about
                // what the head's shape is.
                const int half = playhead_head_half_px(r, s);
                const int y0 = head_top + r;
                const int x0 = lane.x + col - half;
                const int w  = 2 * half + 1;
                cairo_set_source_rgb(cr, kPlayheadHead.r, kPlayheadHead.g,
                                     kPlayheadHead.b);
                cairo_rectangle(cr, x0, y0, w, 1);
                cairo_fill(cr);
            }
            // THE TICK CROSSINGS, painted back over the head in the pre-blended
            // value. Re-walking the ladder would be a second source of truth, so
            // the crossing is decided the cheap way instead: a column carries a
            // tick exactly when it is a whole number of minors from the origin,
            // which is the same test the walk above used.
            //
            // ONLY THE ACTUAL INTERSECTION IS RECOLORED, and the intersection is
            // computed from the SAME tops and bottom the tick walk drew with
            // rather than assumed. THE MOVE INTO THE MARKER LANE CHANGED THE
            // ANSWER COMPLETELY, which is exactly why this is computed:
            //   * BEFORE, on the ruler's bottom rows, a MINOR never touched the
            //     head at any scale (its top was marker.y, which WAS the head's
            //     bottom — abutting bands) and a MAJOR touched only its own
            //     rise, 4 rows of 12 at 100%.
            //   * NOW both classes cut the FULL SILHOUETTE. The head sits on
            //     the marker lane's BOTTOM rows and every tick runs the lane's
            //     whole height to the waveform top, which is exactly the head's
            //     tip row. Minors start at the lane top, majors `rise` above it;
            //     either way the tick covers the head end to end. So both
            //     intersections are [head_top, head_bottom) entire — 12 rows of
            //     12 at 100%, 18 of 18 at 150%, 24 of 24 at 200%.
            // The major/minor split is KEPT even though the two now resolve to
            // the same band: it is the tick walk's own `major = (i == 0)` fact,
            // and asserting "they coincide" in code instead of deriving it is
            // how the previous geometry's answer would have survived this move
            // as a silent lie.
            cairo_set_source_rgb(cr, kPlayheadHeadTick.r, kPlayheadHeadTick.g,
                                 kPlayheadHeadTick.b);
            // THE CROSSING READS THE COMB THE WALK PAINTED. It used to re-derive
            // the tick columns from the float minor grid — "is this column within
            // half a pixel of a whole multiple of `minor`" — which was a second
            // derivation that happened to agree while every tick rounded its own
            // float position. IT WOULD NOT AGREE NOW: the minors are distributed
            // across their segment's integer width (the rigid-comb note at the
            // walk), so their columns are no longer a function of time alone.
            // The walk records each painted tick into head_ticks, and this reads
            // it back — one source of truth by construction rather than by two
            // expressions being kept in step.
            //
            // The window is centred on the same cursor column this block paints
            // the head at (head_col0 was derived from it before the walk), so
            // the two cannot drift.
            for (int dx = -head_half_max; dx <= head_half_max; ++dx) {
                const int tc = col + dx;
                if (tc < 0 || tc >= wave_w) continue;
                const int w_idx = tc - head_col0;
                if (w_idx < 0 || w_idx >= head_window) continue;
                const uint8_t kind = head_ticks[static_cast<size_t>(w_idx)];
                if (kind == 0) continue;
                // WHICH tick it is decides where it starts, straight off the
                // walk's own `major = (i == 0)` verdict (2 = major, 1 = minor).
                const bool major_here = (kind == 2);
                const int tick_top = major_here ? major_top : minor_top;
                const int y_lo = std::max(tick_top, head_top);
                const int y_hi = std::min(tick_bottom, head_bottom);
                for (int y = y_lo; y < y_hi; ++y) {
                    // The SAME accessor the silhouette pass filled with (floor
                    // included), so the crossing clips to the pixels that are
                    // actually there rather than to a second derivation.
                    const int half = playhead_head_half_px(y - head_top, s);
                    if (dx < -half || dx > half) continue;
                    cairo_rectangle(cr, lane.x + tc, y, 1, 1);
                }
            }
            cairo_fill(cr);
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
    // BLIT-ONLY HERE, WITH EXACTLY ONE PASS RECOLORING IT AFTER: this call
    // writes the plate's pixels as the renderer wrote them, composited once
    // over whichever ground — kWaveformCanvas, or a kWaveformRegionCanvas
    // recolor — the pass before this one left. The one later pass that touches
    // those pixels is paint_region_ink, the very next call in on_redraw, which
    // masks kWaveformRegionInk through the plate's own alpha inside the REGION's
    // column span alone; outside that span, and on every frame where no region
    // stands, the blitted pixels are final. The plate SURFACE is never rewritten
    // either way — both passes recolor at paint time.
    //
    // The out-of-trim dim — the same second-masked-pass mechanism applied to the
    // whole out-of-trim stretch — stays retired (architect 2026-07-26): the trim
    // bridge bar is the whole inside-the-window signal now.
    //
    // The clip is the CONTENT band, not the full area: the area's top and
    // bottom rows are render_canvas's 2px black border (row 6) and no
    // band-filling pass may cover them. (The plate's own inset band leaves those rows transparent
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

// -- GuiPaintHandler::plate_viewport_basis / region_columns ----------

// See the declaration comment in paint_handler.h: the fp-recipe basis locked to
// the blitted plate while the worker rebuilds, with the live spp fallback when
// no plate has published a span yet.
GuiPaintHandler::PlateViewportBasis
GuiPaintHandler::plate_viewport_basis() const {
    PlateViewportBasis b;
    b.spp = wf_cache.fp_area_w > 0
        ? static_cast<double>(wf_cache.fp_vp_end - wf_cache.fp_vp_start) /
          static_cast<double>(wf_cache.fp_area_w)
        : current_samples_per_pixel(app, audio);
    b.vp_start = static_cast<double>(wf_cache.fp_vp_start);
    return b;
}

GuiPaintHandler::RegionColumns
GuiPaintHandler::region_columns(const PlateViewportBasis& basis) const {
    // DERIVED FROM THE TRIM, not from a stored span (2026-08-18 — the region IS
    // the trim; the model is at RegionState, app_state.h). trim_overlay_span is
    // the one owner of "where the overlay is": it crosses both bounds into the
    // active display domain and hands them back ordered, so this pass and the
    // hit test read the same two numbers on the same frame with nothing cached
    // between them.
    const TrimOverlaySpan span = trim_overlay_span(app, audio);
    const int64_t lo = span.lo;
    const int64_t hi = span.hi;
    RegionColumns c;
    // The one column rounding (displayed_column_at, warp_frame_map_view.h), on
    // the PLATE basis the caller passed — the endpoints already live in the
    // active display domain, so no warp map is walked.
    c.lo_col = displayed_column_at(static_cast<double>(lo),
                                   basis.vp_start, basis.spp);
    c.hi_col = displayed_column_at(static_cast<double>(hi),
                                   basis.vp_start, basis.spp);
    return c;
}

// -- GuiPaintHandler::paint_region_ground --------------------------------

// THE REGION HIGHLIGHT'S GROUND HALF (the Ableton model, architect 2026-07-26):
// the span's CANVAS becomes the opaque kWaveformRegionCanvas over
// the full
// content height. Called from on_redraw after render_canvas and BEFORE
// paint_waveform_plate, so the ARGB32 plate composites over the recolored
// ground — and since the aliased renderer's alpha is BINARY (the antialiased
// plate is deleted; docs/engineering/waveform_antialiasing_retired.md), an ink
// pixel is fully opaque and a gap fully transparent, so this fill shows through
// the gaps exactly and blends with nothing.
//
// It is HALF the highlight, not all of it: paint_region_ink below lifts the INK
// over the same span after the blit (architect 2026-08-18), so the highlight
// reads as one lit region rather than as a lit background behind unlit content.
// That is still no wash — it masks a second OPAQUE colour through the plate's
// own binary alpha, the mechanism the recolor model admits, where a translucent
// wash painted over the plate is the form it rejects.
// Session-only, nothing persisted; not part of the plate/flag caches — a direct
// per-frame pass, so no cache is involved. AA off, integer edges. The fill is
// clipped to the CONTENT band so it cannot cover the area's border rows.
void GuiPaintHandler::paint_region_ground(cairo_t* cr, const GuiRect& area) {
    if (!app.region.shown) return;
    if (area.w <= 0 || area.h <= 0) return;

    // Displayed-viewport recipe: the same fp_* fingerprint paint_playheads and
    // the overlay band use, so the ground stays locked to the blitted plate
    // while the worker rebuilds against a viewport change.
    const PlateViewportBasis basis = plate_viewport_basis();
    if (basis.spp <= 0.0) return;

    // Endpoints normalized to [lo, hi] and mapped to columns via the shared
    // region_columns owner (the plain viewport transform — the endpoints already
    // live in the displayed domain, so no warp map is walked, unlike the phase
    // reset overlay whose source-frame marker crosses to target first).
    const RegionColumns cols = region_columns(basis);

    double x0 = static_cast<double>(area.x + cols.lo_col);
    double x1 = static_cast<double>(area.x + cols.hi_col);
    // Clamp to the visible strip; a span wholly offscreen paints nothing.
    x0 = std::max(x0, static_cast<double>(area.x));
    x1 = std::min(x1, static_cast<double>(area.x + area.w));
    if (x1 <= x0) return;

    const GuiRect content = waveform_content_rect(area);
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    cairo_set_source_rgb(cr, kWaveformRegionCanvas.r, kWaveformRegionCanvas.g,
                         kWaveformRegionCanvas.b);
    cairo_rectangle(cr, x0, static_cast<double>(content.y),
                    x1 - x0, static_cast<double>(content.h));
    cairo_fill(cr);
    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_region_ink -----------------------------------

// THE HIGHLIGHT'S SECOND HALF (architect 2026-08-18): the span's INK takes the
// same doubled Breeze lift its canvas already takes, so the highlight lifts the
// whole picture instead of only the ground behind it. Called from on_redraw
// immediately AFTER paint_waveform_plate — the pair with paint_region_ground
// above, one highlight in two passes with the blit between them.
//
// A SECOND OPAQUE COLOR MASKED THROUGH THE PLATE'S OWN ALPHA, never a
// translucent wash over the plate — the wash is the retired form the opaque
// recolor model rejects. Because the aliased renderer's alpha is BINARY (the
// antialiased plate is deleted; docs/engineering/waveform_antialiasing_retired.md)
// the mask has no fractional coverage anywhere: every ink pixel in the span
// becomes exactly kWaveformRegionInk and every gap is left alone, so the
// kWaveformRegionCanvas ground the previous pass laid down still shows through
// the gaps unchanged.
//
// The plate is not rewritten: this recolors at PAINT time and writes nothing
// into the cache, so a pan or a zoom that reuses the surface reuses the plain
// ink. Damage is the ground pass's — a subspan of pixels that pass already owns
// in the same redraw. Session-only, nothing persisted, no cache involved.
//
// The span comes from plate_viewport_basis() and region_columns(), the very
// calls paint_region_ground makes, so the ground and the ink cannot disagree
// about where the region is; the clip is the CONTENT band, so neither half can
// reach the area's 2px black border rows.
void GuiPaintHandler::paint_region_ink(cairo_t* cr, const GuiRect& area) {
    if (!app.region.shown) return;
    if (area.w <= 0 || area.h <= 0) return;
    // The blit's own guard: with no published plate there is no alpha to mask
    // through, and the ground pass's fill is the whole highlight for that frame.
    if (!wf_cache.surface) return;

    const PlateViewportBasis basis = plate_viewport_basis();
    if (basis.spp <= 0.0) return;

    const RegionColumns cols = region_columns(basis);

    double x0 = static_cast<double>(area.x + cols.lo_col);
    double x1 = static_cast<double>(area.x + cols.hi_col);
    // Clamp to the visible strip; a span wholly offscreen paints nothing.
    x0 = std::max(x0, static_cast<double>(area.x));
    x1 = std::min(x1, static_cast<double>(area.x + area.w));
    if (x1 <= x0) return;

    const GuiRect content = waveform_content_rect(area);
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // (the region's column span) INTERSECT (the content band)
    cairo_rectangle(cr, x0, static_cast<double>(content.y),
                    x1 - x0, static_cast<double>(content.h));
    cairo_clip(cr);
    cairo_set_source_rgb(cr, kWaveformRegionInk.r, kWaveformRegionInk.g,
                         kWaveformRegionInk.b);
    // The blit's own origin, so the mask lands on the pixels it came from.
    cairo_mask_surface(cr, wf_cache.surface, area.x, area.y);
    cairo_restore(cr);
}

// -- GuiPaintHandler::phase_reset_overlay_band / its ring pass ------------

// THE ENGINE'S SEED FRAME FOR A RESET, mirrored in the GUI (2026-09-02): the
// schedule index m the engine seeds at for a reset authored at source frame
// `reset_source_frame` under `map` — an EMPTY map is the identity, as it is
// for the map functions themselves. It restates engine.cpp's pass 1 (the
// last schedule entry ≤ the parser's `S − N/2`) over stft_container.h's
// schedule (generate_source_frame_positions: positions[m] =
// llrint(map_target_to_source(m·R_s) − N/2), half-to-even), and it MUST STAY
// IN LOCKSTEP with both — a change to either is a change here. The compare
// is in SOURCE terms exactly as the engine's is; ⌊T/R_s⌋ in the target
// domain is NOT the rule (the schedule rounds in source terms, and a
// piecewise map can put the two on different sides of a boundary). The
// condition is monotone in m (the map is monotone), so the search starts at
// ⌊T/R_s⌋ and steps until it flips — a few iterations at most. m = 0 always
// qualifies (positions[0] = −N/2 ≤ S − N/2 for every authored S ≥ 0), so the
// engine's before-the-first-frame drop has no twin here. The schedule's END
// is not applied: a reset past the map's final anchor, which the parser
// drops from participation, still paints its band as it paints its flag.
static int64_t phase_reset_seed_frame_index(
    int64_t reset_source_frame,
    const std::vector<WarpFrameMapSegment>& map) {
    const auto seeds_at_or_before = [&](int64_t m) {
        const double window_start =
            map_target_to_source(static_cast<double>(m * kRs), map)
            - static_cast<double>(kN) / 2.0;
        return std::llrint(window_start) <= reset_source_frame - kN / 2;
    };
    const double t_reset = map_source_to_target(
        static_cast<double>(reset_source_frame), map);
    int64_t m = std::max<int64_t>(
        0, static_cast<int64_t>(std::floor(t_reset / static_cast<double>(kRs))));
    while (m > 0 && !seeds_at_or_before(m)) --m;
    while (seeds_at_or_before(m + 1)) ++m;
    return m;
}

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
// (phase_reset_seed_frame_index above, the engine's rule mirrored) and that
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
// for one day (codex round 2). THIS SITE ADDS THE THIRD TERM: the nearbyint(T)
// below is up to another half OUTPUT sample of painted width. So the band can
// come out that much wider than N/2 (codex round 1's case — tempo 0.30, reset
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
// varying width, misrepresenting it, and the S+P drop is a stub with no
// lead-in to depict (the drop's comment), so the overlay is not drawn there.
// A seed grain is a phase-reset-only concept, so there is nothing on the
// warp axis to mirror.
GuiPaintHandler::PhaseResetOverlayBand
GuiPaintHandler::phase_reset_overlay_band(const GuiRect& area) const {
    PhaseResetOverlayBand out;
    // Visibility: always-on for the focused enabled marker while the global
    // W/P mode is on P, in target view; never source view. Everything
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
    //
    // NO REGION GATE HERE, and none is wanted — THE DERIVATION, recorded once at
    // this site with Selection::phase_overlay_subject's mirror pointing here:
    // the two annotate DIFFERENT THINGS and neither hides the other — the
    // overlay is the trim window, this band is one phase reset's lead-in — so
    // there is nothing for a gate to arbitrate. (The DEAD-CODE argument that
    // stood here first is retired, 2026-08-18: it rested on the overlay only
    // ever resting beside an EMPTY selection, which held while both formers
    // deselected at press and does not hold now that bare `[` shows the overlay
    // and writes no selection. The conclusion is unchanged, and it never needed
    // that premise.)
    if (app.selected_markers.size() >= 2) return out;

    // Paint sample: the exact expression render.cpp's file-local
    // frame_to_paint_sample uses, so marker and overlay can never disagree.
    double ms;
    // The band's extent in output samples: from the marker's paint sample to
    // the seed grain's end, derived in the block below from the same frame
    // and the same map the paint sample reads.
    int64_t width_samples;
    {
        if (app.active_audio_view != 'T') return out;

        const auto& markers = app.phaseresetmarkers.markers();
        const int idx = app.last_selected_marker;
        if (idx < 0 || idx >= static_cast<int>(markers.size())) return out;
        const auto& marker = markers[idx];
        // Skip a disabled focused reset — a disabled phase reset paints no
        // overlay, reading its `disabled` bool directly (phase resets carry no
        // label cascade).
        if (marker.disabled) return out;

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
    return out;
}

// THE OVERLAY RING — the phase-reset overlay's WHOLE visual (architect
// 2026-07-27): the band's 1px opaque border in the phase-reset stem's own
// purple (2026-08-01) and nothing else,
// painted AFTER the plate. It is a BOUNDARY LINE, like the playheads and the
// stems, so an opaque line crossing waveform ink is correct and intended, and
// with no fill inside it the band now READS as the two edges of a span rather
// than as a tinted region.
//
// THE HORIZONTALS RIDE THE BORDER'S OUTERMOST ROWS (architect 2026-08-01,
// GIMP-verified at the row-6 live look). They sat on the CONTENT band, one row
// inside each 2px border — which put them where the marker and playhead stems
// cross, so the ring read as broken by every stem. The top run moved UP 2px onto
// the top border's FIRST row and the bottom run DOWN 2px onto the bottom
// border's LAST row, and the verticals extend to meet them. This pass is
// therefore the ONE band-filling pass that deliberately does NOT clip to
// waveform_content_rect (the inventory at that helper says so): the ring is not
// content, it is a frame drawn ON the frame.
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
    // THE RING IS THE STEM'S PURPLE (architect 2026-08-01): kMarkerFlagFill
    // #9b59b6, the phase-reset class's own UNSELECTED fill — "they're one unit",
    // the ring and the stem of the reset it annotates. Hard-coded per the
    // redesign's colour ruling, superseding the tunable grey #7f8c8d this drew
    // in, whose ONE paint site this was — which is what left its config key
    // unread and, a day later, deleted with the whole tunable palette. It reads
    // the same constant the
    // stems resolve to rather than a copy of its value, so the two cannot drift.
    cairo_set_source_rgb(cr, kMarkerFlagFill.r, kMarkerFlagFill.g,
                         kMarkerFlagFill.b);
    // THE FULL AREA, not the content band: the top run lands on row area.y (the
    // top border's first row) and the bottom on row area.y + area.h - 1 (the
    // bottom border's last), with the verticals spanning every row between them.
    const double y0 = static_cast<double>(area.y);
    const double h  = static_cast<double>(area.h);
    cairo_rectangle(cr, band.x0, y0, w, 1.0);            // top
    cairo_rectangle(cr, band.x0, y0 + h - 1.0, w, 1.0);  // bottom
    cairo_rectangle(cr, band.x0, y0, 1.0, h);            // left
    cairo_rectangle(cr, band.x1 - 1.0, y0, 1.0, h);      // right
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
// trim_endcap_rect inside the two renderers, so paint stays column-coherent with
// hit_test_trim_endcap / route_trim_bar_press, which read exactly that basis
// (paint == hit by shared owners) — IN EVERY STATE since 2026-08-18, the `h`
// history view's display-only diff-span substitution having been deleted with
// the architect's "trim should not change going into history".
// Deliberately NOT the member
// GuiPaintHandler::plate_viewport_basis(): that is the PLATE-fingerprint
// basis for plate-registered overlays, and the two differ inside the accepted
// resize item-only-promotion window — trim must ride the ITEM basis the
// endcaps' hit rects resolve on. The renderers' column math therefore divides the
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
    if (area.w <= 0 || area.h <= 0) return;
    if (top_strip.w <= 0 || top_strip.h <= 0) return;

    // The ITEM basis (free owner; the member plate_viewport_basis is the other
    // epoch — see the header comment above).
    const ItemViewportBasis basis = item_viewport_basis(app, audio);
    if (basis.area_w <= 0 || basis.spp <= 0.0) return;

    // The item pixels' map: empty (identity) in source view, the committed
    // displayed map (live fallback cold) in target view — exactly
    // hit_test_trim_endcap's selection.
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
    // trip through TrimRange is exact, so trim_bound_column sees the same
    // value the hit sites pass). Both bounds are always meaningful.
    TrimRange trim{
        static_cast<int64_t>(displayed_trim_ms(bar_begin_frame, map_arg)),
        static_cast<int64_t>(displayed_trim_ms(bar_end_frame, map_arg))};

    // Waveform rect for the renderers: real screen origin/height, width =
    // basis.area_w (the committed item width — the column-mapping denominator
    // and the [0, wave_w) painter clip, keeping paint == hit through the
    // accepted resize window; equal to waveform_area(app).w at rest).
    const GuiRect wave_rect{area.x, area.y, basis.area_w, area.h};

    // ONE HALF ONLY since 2026-08-01: the waveform stem pass is deleted, so
    // this is the strip's bar + endcaps (with the side-aware offscreen
    // sentinels and the effective-width clip inside render_trim_flags).
    //
    // The trim bar lane's y-band is THREADED IN as top_trim_row_area(app)
    // rather than re-derived inside the painter: this is the same accessor
    // hit_test_trim_endcap's y-gate and route_trim_bar_press' bridge y-gate
    // read, so the painted band and the clickable band have ONE owner and
    // cannot drift if the lanes above the trim bar ever change.
    // NO WAVEFORM STEMS (architect 2026-08-01): the bar and its two endcaps are
    // the trim window's WHOLE display. render_trim_stems drew a 1px grey
    // vertical down the waveform at each bound; the redesigned lane says the
    // window where the window is, and a pair of full-height lines competing with
    // the marker stems said it a second time in the same pixels.
    render_trim_flags(cr, top_strip, top_trim_row_area(app), wave_rect,
                      basis.vp_start_frame, basis.vp_end_frame, trim);
}

// -- GuiPaintHandler::paint_marker_stems ---------------------------------

// EVERY ENABLED MARKER STEMS, ALWAYS (row 5, architect): the per-frame waveform
// overlay that replaced the singleton selected-marker stem. The full contract —
// what stems, in what colour, and why selection changes none of it — is at the
// declaration.
//
// It reads the marker painter's stash (app.marker_stems) instead of walking a
// store: the stem stands on its flag box's LEFT EDGE, and that column was
// already resolved by the pass that painted the box, on the displayed basis
// those pixels were laid out against. So the DragOverlay substitution, the
// source->target map walk, the per-marker cull and the colour ladder all happen
// exactly once, in the painter, and a stem can never land a pixel away from its
// own flag. Disabled markers are simply absent from the stash.
//
// The stem spans the waveform area top to bottom — the flag's bottom edge IS
// the waveform top (the marker lane rests flush on it), so the two meet with no
// seam and no strip-crossing segment to draw. It runs OVER the waveform's own
// borders when row 6 adds them, the same z-intent the playhead stem records: a
// stem is a boundary line, not something the borders clip.
void GuiPaintHandler::paint_marker_stems(cairo_t* cr, const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    if (app.marker_stems.empty()) return;

    // THE INVALID-COMMIT RED FLASH REACHES THE STEM (architect 2026-08-01): a
    // flashing flag and its stem read as one object, exactly as a coincident
    // marker's red pair already does (#da4453 either way — the flash borrows the
    // red CLASS, it does not invent a colour).
    //
    // IT IS A PAINT-TIME OVERRIDE, mirroring how the flash face itself is stored
    // and painted: render_flag_editor_box resolves the marker's ordinary face
    // through the one class ladder and then overrides the pair when `ed.red` is
    // set, per frame, out of any cache. This is that override on the stem's own
    // live pass — the stash keeps publishing the marker's real class, so nothing
    // has to be un-published when the flash clears and the flag cache needs no
    // fingerprint for a transient. A DISABLED marker has no stash entry and
    // therefore no flashing stem, which is the same absence its flag's missing
    // stem always was.
    //
    // The 'W' test is the guard the index needs, not decoration: the flag editor
    // is a warp-column surface by its open gates, and stash indices belong to
    // whichever column is active, so without it a P-view stash row could match a
    // warp target's index and redden an unrelated phase reset.
    const text_editor::State& ed = app.top_flag_editor;
    const int flash_idx =
        (text_editor::is_active(ed) &&
         ed.kind == text_editor::Kind::FlagPayload && ed.red &&
         app.active_markers_view == 'W')
            ? ed.target
            : -1;

    cairo_save(cr);
    cairo_set_line_width(cr, 1.0);
    const double y0 = static_cast<double>(area.y);
    const double y1 = static_cast<double>(area.y + area.h);
    for (const MarkerStem& stem : app.marker_stems) {
        // Column-gate exactly like render_playhead's line does, so a stem whose
        // flag hangs into view from the left (the boxes run rightward) never
        // leaks its column into the chrome beside the waveform.
        const double col = stem.x - static_cast<double>(area.x);
        if (col < 0.0 || col >= static_cast<double>(area.w)) continue;
        const double x_px = static_cast<double>(area.x) + col + 0.5;
        const GuiColor c = (stem.marker_index == flash_idx) ? kMarkerStemRed
                                                            : stem.color;
        cairo_set_source_rgb(cr, c.r, c.g, c.b);
        cairo_move_to(cr, x_px, y0);
        cairo_line_to(cr, x_px, y1);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_strip_drag_anchor ----------------------------

// Paints the anchor stem (the Ableton pivot affordance) at the live zoom
// gesture's current anchor column, full waveform height. TWO PRODUCERS, ONE
// STEM (it was two, then three when the touch pinch joined on 2026-08-14, and
// two again on 2026-08-15 when the overview lane's ctrl strip drag — the
// original producer — was deleted with the lane's zoom):
//   * THE ONE NAV DRAG'S ZOOM PHASE (scroll_drag while `zooming` — from a
//     ctrl-armed press, or from a ctrl-down edge mid-drag, and gone again at
//     the ctrl-up edge; the mode's contract is at ScrollDragState);
//   * THE TOUCH TWO-FINGER PINCH (touch_nav_zoom.seated — the contract is at
//     TouchNavZoomState, app_state.h), added so THE TWO SURFACES SHOW THE SAME
//     AFFORDANCE (architect 2026-08-14, from the rig, asking to SEE the glass
//     gesture: "add a zoom stem to the zoom on the touchpad just so I can see
//     exactly what's going on, because at the edges there are some
//     strangeness, it seems like").
// The gate is the gesture record and nothing else since 2026-08-05
// (architect), so THE PRESS ITSELF SHOWS THE PIVOT — the headless zoom stem —
// rather than the stem appearing only once the drag crosses the slack. The
// mouse arm and the mode-switch edges owe the frame's damage
// (arm_nav_zoom_press / the mode sync in on_motion); it
// vanishes the moment its gate drops (release / button loss / the force-end
// finalizer / the ctrl-up switch, each spelling its own damage; Esc no longer
// ends a gesture at all). The stem is the ZOOM PIVOT and nothing more — the
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
// paint_region_ground and paint_playheads use, so the stem stays locked to the
// blitted plate while the worker rebuilds, and the anchor lives in the active
// display domain (viewport_start + col*spp) so no warp map is walked.
// Re-projecting is what makes the stem SLIDE WITH ITS CONTENT when a clamped
// zoom keeps moving the song under it — which is now visible ON GLASS exactly
// as it is under the mouse, the ruling made watchable and the reason the
// architect asked for the stem there.
// render_strip_anchor_stem clamps the column to the visible edges — an
// edge-pinned anchor draws the clamp itself.
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

    // The one column rounding (displayed_column_at, warp_frame_map_view.h), on
    // the PLATE basis.
    const int col = displayed_column_at(anchor_sample, basis.vp_start,
                                        basis.spp);
    render_strip_anchor_stem(cr, area, col);
}

// -- GuiPaintHandler::playhead_stem_suppressed ---------------------------

// THE PLAYHEAD'S STEM SUPPRESSES WHERE A MARKER'S STEM ALREADY STANDS
// (architect 2026-08-01). This REINSTATES 035e669's model — "the cursor playhead
// is conceptually COINCIDENT with the selection and fully hidden behind the
// marker — line on the stem, triangle behind the flag; suppression as
// implementation, not absence" — which the 2026-07-30 always-paints ruling
// deleted. It is the coincident case ALONE that the always-paints clause loses:
// the playhead still paints everywhere else, unconditionally, and the HEAD
// paints even here (the architect expects it partly visible behind a coincident
// flag; a ±1 column is invisible against the HEAD, whose widest row is
// 2 * playhead_head_half_px(0, s) + 1 — 9px at the 50% floor, 19 at 100%, 73 at
// the 400% ceiling, so it is at least nine columns wide anywhere in the schema
// and the ±1 never approaches half of it. That is exactly what a 1px stem
// beside another 1px stem is not, at any scale: the stem is one column by
// ruling and does not scale at all, so there the same ±1 is the whole object).
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

    // The dragged marker, or -1. The view compare is a statement, not a repair:
    // the drag-modal gate swallows `p`, so a live drag's mode is always the
    // active column — the stash indices this compares against are that column's.
    const int dragged =
        (app.drag.active && app.drag.drag_mode == app.active_markers_view &&
         !app.drag.dragging_markers.empty())
            ? app.drag.dragging_markers[0]
            : -1;

    const auto coincident = [&](int64_t source_frame) {
        return clamp_playhead_to_live_domain(
                   source_frame_to_active_domain(app, audio, source_frame),
                   app, audio) == app.playhead_cursor_sample;
    };

    const auto& wv = app.warpmarkers.markers();
    const auto& pv = app.phaseresetmarkers.markers();
    const bool phase = (app.active_markers_view == 'P');
    for (const MarkerStem& stem : app.marker_stems) {
        const int i = stem.marker_index;
        if (i == dragged) return true;
        if (i < 0) continue;
        // Index-guarded against the store the stash was published from having
        // shrunk since (an undo under a stale stash): a missing row simply does
        // not suppress.
        if (phase) {
            if (i < static_cast<int>(pv.size()) && coincident(pv[i].time_frame))
                return true;
        } else {
            if (i < static_cast<int>(wv.size()) && coincident(wv[i].time_frame))
                return true;
        }
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
    // any more: the tip-down triangle died with its lane, its successor is the
    // aliased head on the MARKER lane's bottom rows, and paint_ruler_row owns it
    // despite the lane (it needs the tick columns for the pre-blended
    // crossing). So this pass is the WAVEFORM segment of the cursor's
    // stem, nothing else — and since 2026-08-02 render_playhead draws a line and
    // only a line: the dead triangle branch is deleted, and with it the lane
    // rect this call used to thread through to it.

    // The cursor paints UNDER the marker flags (the Z-ORDER FLIP, architect
    // 2026-07-23 — see the paint-order block in on_redraw): its line passes
    // beneath a marker flag sharing its column, so a cursor resting on a marker
    // sits hidden behind that marker's flag, and it passes under the marker
    // STEMS below it in the waveform too. Gated on the waveform OR the top strip
    // being exposed: the cursor's HEAD lives in the strip (paint_ruler_row) and
    // this stem in the waveform, and the two halves of one line repaint
    // together whatever the damage shape — the outer Cairo clip bounds the
    // actual work.
    //
    // THE SCANNER LEFT THIS PASS (architect 2026-08-01) — it is paint_scanner
    // now, invoked after the marker stems, so the moving line crosses a marker's
    // stem instead of blinking out behind it. This pass is the CURSOR alone, and
    // the two are no longer ordered against each other here: the scanner is over
    // everything in the waveform area while it runs.

    // THE CURSOR PLAYHEAD ALWAYS PAINTS (architect 2026-07-30): ONE playhead
    // form, drawn at the resting cursor column whatever the selection and
    // whatever the region are doing — a 1px line painted solid straight over the
    // plate ink. WITH ONE EXCEPTION SINCE 2026-08-01, and exactly one: where a
    // MARKER'S stem already stands on the playhead's frame, the playhead's STEM
    // does not paint and that marker's stem is the display (035e669's
    // hidden-behind-the-marker model, reinstated — the whole ruling is at
    // playhead_stem_suppressed). The clause above still holds everywhere else,
    // and the HEAD paints in the suppressed case too (paint_ruler_row).
    //
    // The three-way chain that used to live here is gone with the SPAN FORM: the
    // region is no longer a playhead at all (it IS THE TRIM — a ground recolor
    // DERIVED from the trim window every frame, written by the shift waveform
    // sweep, previewed by the lower half's scrub click act, shown and hidden by
    // bare `[`), so it hides
    // nothing and suppresses nothing, and the split half-triangle renderer is
    // deleted outright. The non-empty-selection suppression is
    // gone too: a cursor resting ON the focused marker is simply hidden behind
    // that marker's flag by the z-order flip, which is what the old else-arm was
    // spelling out by not painting — and when the arrows move the focused marker
    // the cursor rides along VISIBLY, which is the lane model's honest reading.
    // The region ground still paints under the plate (paint_region_ground); the
    // cursor line crosses it exactly as it crosses waveform ink.
    // THE TRIANGLE IS OFF EVERYWHERE (row 5): the cursor's tip-down triangle
    // retired with the triangle lane, and its successor — the aliased head on
    // the MARKER lane's bottom rows — is painted by the ruler pass anyway, that pass
    // owning the tick columns the head's pre-blended crossing needs. So this call is the stem's WAVEFORM
    // segment; the ruler pass draws the head, its tip on the waveform boundary
    // where this segment begins, and the two make one unbroken line.
    // THE STEM IS kPlayheadStem NOW (#fcfcfc), superseding the old cursor line's
    // color at this surface: the head above it is the playhead's identity, and
    // the stem is that head's line continued down through the waveform.
    //
    // Z-INTENT: this segment goes down UNDER the marker stems painted after it
    // and under the flag boxes blitted after those, and the head above it (the
    // ruler pass) likewise goes under the flags. That is the HIDDEN-BY-MARKER
    // model translated — a flag sharing the cursor's column hides it, exactly as
    // flags painted over the old triangle — and it is also why the stem is drawn
    // to run OVER the waveform's own borders: the stem is a boundary line like
    // the marker stems beside it, not a thing the borders clip.
    if (!playhead_stem_suppressed()) {
        render_playhead(cr, area, px_x, kPlayheadStem);
    }
}

// -- GuiPaintHandler::paint_scanner --------------------------------------

// THE SCANNER PASSES OVER THE MARKER STEMS (architect 2026-08-01, at the row-6
// live playback look). It was drawn inside paint_playheads, which runs BEFORE
// paint_marker_stems, so every always-on marker stem overpainted the moving
// line's column: at our marker density the scanner blinked out repeatedly as it
// crossed the song. Its own pass, invoked after the stems, is the fix — and it
// is the SCANNER ALONE that moved. The resting cursor keeps painting under the
// stems and under the flags (the hidden-by-marker model, which is about a
// cursor COINCIDENT with a marker); a scanner sweeping past one is not that
// case, and a line that vanishes where the user is looking is not a z-order
// statement, it is a dropout.
//
// SO THE SCANNER IS NOW TOPMOST IN THE WAVEFORM AREA while it runs — over the
// stems, over the cursor where they overlap, over the plate and the region
// ground. Everything it covers is a per-frame repaint anyway.
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
    render_playhead(cr, area, scan_px, kPlayheadScanner);
}

// -- GuiPaintHandler::paint_overview_strip -------------------------------
//
// THE OVERVIEW STRIP (architect-ratified 2026-08-12 — his pick from the
// offered fillers for the row unification's freed space: "the whole song
// overview strip, yes, that's the best one... that's perfect"; the Ableton
// model per his own reference, ableton.png in the redesign folder: "a Zoom
// strip right underneath the transport buttons... it draws a box around the
// area that you currently view"). TOP lane 3 since the relayout's commit B —
// inside the CENTERED BLOCK, between the ICON ROW and the TRIM BAR — at ONE
// fixed tiny height on every host (render.h's kOverviewHeightPx owns the ruling
// and the deleted min/max clamp pair; it was bottom lane 0 under the unified row
// for the afternoon it landed).
//
// FIVE LAYERS, bottom to top, all inside the lane:
//   1. GROUND + TWO BORDERS, spelled here rather than through render_canvas:
//      the waveform's kWaveformCanvas ground (reused rather than resampled —
//      the lane IS a miniature of the waveform surface, and a third ground
//      would be a new color with no crop behind it) under a 1px
//      kWaveformBorder row at the lane's TOP edge and another at its BOTTOM.
//      The top row landed 2026-08-13 (architect: the lane "gains an
//      almost-black top border, the same colour as the bottom one"), the lane
//      growing by it — commit B's single bottom line is superseded, its own
//      predecessor having been the waveform's 2px rows at both ends under the
//      old bottom-strip home. render_canvas still cannot serve: its rows are
//      waveform_border_px thick, this lane's are its own 1px (the CSS box
//      model and the succession are recorded at the constant). Painted on
//      every frame class, audio or none.
//
//      THE GROUND HAS A REGION HALF SINCE 2026-09-04 (architect): while the
//      trim region overlay stands, the trim's own columns take the waveform's
//      kWaveformRegionCanvas recolor here too — the whole content band at a
//      full window, where the lane recolors exactly what the waveform recolors
//      — so the overlay has a SECOND SURFACE and the whole-song picture
//      answers where the trim is. It is the same ruling that took the framing
//      off bare `[`'s show half — the camera
//      no longer travels to the window, because this lane already shows it
//      (handle_toggle_trim_region, input_trim.cpp). It is layer 1's other half
//      rather than a layer of its own: the recolor REPLACES ground, it lands
//      before the bars exactly as paint_region_ground lands before the plate
//      blit, and the trim line, the box and the tick all draw over the
//      finished highlight. IT IS HALF OF A PAIR, the ink lift under layer 2
//      being the other half, exactly as on the waveform. The passes sit past
//      the audio guards in the body, the span owner needing a lane scale,
//      which is the one thing that separates them from the borders above.
//   2. THE BARS (the cached blit; maybe_rebuild_overview_bar_cache below):
//      the WHOLE PIECE as per-column min/max bars in kWaveformInk, two
//      channel bands exactly as the plate stacks them. THE DATA IS THE
//      SOURCE DOMAIN, ALWAYS — a deliberate ruled choice: the whole-song
//      TARGET domain does not exist as audio (the preview buffer is
//      trim-scoped), so the overview shows the piece itself in every view
//      and the BOX does the domain work. Clipped to the lane's content band
//      (overview_content_rect — the lane less its two border rows, which
//      survive every frame) exactly as the plate clips to the waveform's.
//
//      THE BARS HAVE A REGION HALF TOO, the other end of layer 1's: right
//      after this blit the span's bars take kWaveformRegionInk, masked through
//      the cache's own binary alpha, so the highlight is the SAME PAIR the
//      waveform paints — ground under the picture, ink through it — and the
//      span reads as one lit region rather than as a lit background behind
//      unlit content. Both halves read one span, resolved once above off
//      overview_region_span, so they cover the whole band together at a full
//      window and the trim's columns alone at a sub-window.
//   3. The trim line (architect 2026-09-04): a 1px horizontal run along the
//      lane's top row, showing where in the whole song the trim sits. That is
//      the one thing the 9px trim bar a lane down cannot show —
//      render_trim_flags is viewport-scaled (it takes the basis's
//      vp_start/vp_end), so at a zoomed-in view the trim's place in the piece
//      was shown nowhere. It sits on the lane's first pixel row, the top
//      border's own, and covers it over the trim's span exactly as the tick
//      covers both rows over its column. The head is where it reads, and it
//      landed there on the architect's ruling of 2026-09-04, from a laptop
//      screenshot he sampled by pixel: on the foot the line was painted and
//      invisible, the trim bar lane beginning on the very next pixel row in
//      this same blue and swallowing it — and the trim belongs above the
//      waveform, which is where that bar sits. Its colour is the trim bar
//      body's kTrimLaneBar — a new reader of that constant rather than a new
//      colour, so the two surfaces that depict the trim wear one shade at two
//      scales; the endcap shade stays the endcaps' (and the shared shade is
//      exactly what made the foot unreadable). Its span is overview_trim_span,
//      the hiding face of the lane's one span owner: the line draws nothing at
//      a full window, the whole song not being information, where the region
//      recolor above takes that owner's other face and lights the whole band.
//      It is drawn under the box by ruling, though the two cannot meet: the
//      box is inside the content band and this row is outside it.
//      THE LINE IS THE LANE'S ALWAYS-ON TRIM PRESENCE and layer 1's region
//      recolor is the emphasis (architect 2026-09-04, ruling the pair): the
//      line paints whether or not the overlay stands, so the lane tells where
//      the trim is at all times and says additionally, by the recolor, when
//      the big surface is up. The line FADES AGAINST THE VIEWPORT BOX where
//      the box contains the trim — the box's own white corner sits on this
//      same head row — and that is accepted rather than reordered.
//      It needs no damage of its own, for the box's reason — every route that
//      writes a trim bound raises Viewport::invalidate_waveform_area, whose
//      one rect runs the window top through the waveform's bottom and so
//      contains this lane, per motion event during a drag and once per commit
//      everywhere else.
//   4. THE VIEWPORT BOX: a 1px outline marking the visible span, in
//      kOverviewBoxLine — brightened off kRedesignLine at the lane rework
//      (2026-08-12, "increase contrast on the outline": the outline is a
//      GRAB SURFACE now — its edges are the endcap handles below — and the
//      derivation is at the constant), still grey so the lane's one WHITE
//      vertical stays the playhead's alone. The span comes from the ONE
//      owner the hit geometry shares (overview_box_span). In TARGET view
//      the viewport's target span maps back to source columns through the
//      memoized inverse map, so the box may BREATHE NONLINEARLY across a
//      domain switch or a tempo edit — correct, the map is the truth. Drawn
//      inside the content band (a box edge on the border row would vanish
//      against it); read off the LIVE viewport, which the damage story
//      keeps within one synchronous-rebuild frame of the plate — at
//      whole-song scale an async publish window's divergence is under a
//      column.
//   5. THE PLAYHEAD TICK: one kPlayheadStem column at the playhead's source
//      position — the scanner while one is live, the resting cursor
//      otherwise — full LANE height, OVER BOTH border rows: this stem is a
//      boundary line and the borders do not clip it, which is the recorded
//      z-intent of every 1px position vertical in the product (render_canvas's
//      own note, and waveform_content_rect's). It is layered here exactly as
//      it always was — last, over layer 1 — so the top border inherited the
//      overlap the bottom one already had, with no second arrangement.
//      Its per-frame damage is the two scanner
//      sites' narrow column pair (main.cpp); every discrete write is covered
//      by Viewport::invalidate_waveform_area's one rect, which contains this
//      lane since commit B moved it into the block (that owner's dedicated
//      overview rider died with the move).
//
// INTERACTION lives elsewhere (the lane rework, 2026-08-12 — the RECORDED
// LATER PHASE of the ratification, RESOLVED: the box-drag pan and the
// trim-style zoom brackets are BUILT, architect-ratified the same day):
// the box's edges are ENDCAP handles (edge drags mutating the viewport
// span), a plain press elsewhere TELEPORTS outside the box — AT THE PRESS
// ITSELF (2026-08-17) — and grabs it inside, where the drag is
// the box-follows-pointer PAN; an outside press arms that same pan behind its
// teleport (2026-08-18), and the wheel is unchanged (wheel_context's
// overview arm: plain pan / ctrl zoom step). THE LANE'S VOCABULARY IS THOSE
// THREE GESTURES AND NOTHING ELSE since the redesign of 2026-08-15: the
// DUAL-AXIS strip drag that sat behind CTRL here — the last of that gesture's
// entries — is DELETED whole, so ctrl binds nothing on the lane and no lane
// drag captures the pointer. The press claim and the drag bodies are
// input_pointer.cpp's; the mappings are overview_anchor_sample_at_x and
// overview_box_span; cursors (pointer_cursor_kind): the endcap pair on the
// edges, TrimResize on the whole plain surface off them (the pan is what a
// plain press arms everywhere there), and the ARROW under every modifier, a
// point arming nothing showing the arrow.
void GuiPaintHandler::maybe_rebuild_overview_bar_cache(const GuiRect& lane) {
    if (lane.w <= 0 || lane.h <= 0) {
        overview_bar_cache.destroy_surface();
        return;
    }
    // THE KEY IS (width, height, magnification level) — the contract is at
    // OverviewBarCache. The level is an input to these bars' own tip mapping,
    // so a change to it dirties them BY FIELD.
    if (overview_bar_cache.rendered &&
        overview_bar_cache.width  == lane.w &&
        overview_bar_cache.height == lane.h &&
        overview_bar_cache.magnification_level ==
            app.waveform_magnification_level) {
        return;
    }
    overview_bar_cache.magnification_level = app.waveform_magnification_level;
    if (!overview_bar_cache.surface ||
        overview_bar_cache.width  != lane.w ||
        overview_bar_cache.height != lane.h) {
        if (overview_bar_cache.surface) {
            cairo_surface_destroy(overview_bar_cache.surface);
            overview_bar_cache.surface = nullptr;
        }
        overview_bar_cache.surface = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, lane.w, lane.h);
        overview_bar_cache.width  = lane.w;
        overview_bar_cache.height = lane.h;
    }
    // Clear to transparent — the lane's ground shows through the ink's gaps,
    // the plate's own convention (render_waveform_to_cache_surface).
    {
        cairo_t* ccr = cairo_create(overview_bar_cache.surface);
        cairo_set_operator(ccr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(ccr);
        cairo_destroy(ccr);
    }
    // TWO CHANNEL BANDS, the plate's own stack (stereo is structural), filling
    // the CONTENT band whole — the plate's symmetric waveform_inset_px serves
    // the playhead head's clearance there and would eat a third of this lane's
    // 24px content band, so the bars run the whole band. The band is the lane
    // less its TWO border rows (the split is computed with a ZERO inset over
    // that band's OWN height, the channel splitter's own arithmetic — the
    // borders are already off, so asking it for a symmetric inset would take
    // them twice),
    // and the odd spare row falls at the band's bottom where nothing draws,
    // exactly as in the plate. The cache surface is LANE-sized and blitted at
    // the lane's own origin, so the band's y offset is carried into both
    // channel rects here and the bars land inside the borders rather than
    // under the top one.
    const GuiRect band = overview_content_rect(GuiRect{0, 0, lane.w, lane.h});
    const int split = waveform_channel_split_row(band.h, /*inset_px=*/0);
    const double spp = overview_samples_per_pixel(app, audio);
    if (split >= 0 && spp > 0.0) {
        const int ch_h = split;
        const GuiRect ch0{0, band.y, lane.w, ch_h};
        const GuiRect ch1{0, band.y + split, lane.w, ch_h};
        // THE BASIS: viewport start 0, the whole piece over the lane's width.
        // THE PYRAMID RUNG IS THE ONE OWNER'S PICK, per column from this spp
        // (GuiAudio::level_for_span inside render_waveform — the coarse rungs
        // exist for exactly this span: a whole piece over ~2000 columns reads
        // thousands of frames per column, landing on the ladder's upper
        // rungs at the unconditional <=5-pairs-per-column bound, so the
        // rebuild is O(lane width) like any plate render).
        const WaveformBasis basis{0, spp, lane.w};
        // ONE GAIN ON EVERY WAVEFORM PICTURE (2026-08-26): the whole-song map
        // magnifies exactly as the plate does, this band being where a quiet
        // passage disappears first. The PICTURE only — no sample is scaled.
        render_waveform(overview_bar_cache.surface, ch0, /*col0=*/0, audio, 0,
                        basis, kWaveformInk,
                        app.waveform_magnification_level, nullptr);
        render_waveform(overview_bar_cache.surface, ch1, /*col0=*/0, audio, 1,
                        basis, kWaveformInk,
                        app.waveform_magnification_level, nullptr);
    }
    overview_bar_cache.rendered = true;
}

void GuiPaintHandler::paint_overview_strip(cairo_t* cr) {
    const GuiRect lane = top_overview_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;

    // Layer 1 — the waveform's ground under the TWO border rows, one at the
    // lane's top edge and one at its bottom (the succession is at
    // kOverviewHeightPx). One pass, integer-edged with AA off like
    // render_canvas's own, so ground and lines can never disagree about where
    // the lane ends. A lane too short to carry both rows draws NEITHER rather
    // than overlapping them — render_canvas's own shape, and the shape
    // overview_content_rect degenerates to. Every frame class.
    {
        const int b = overview_lane_border_h_px();
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, kWaveformCanvas.r, kWaveformCanvas.g,
                             kWaveformCanvas.b);
        cairo_rectangle(cr, lane.x, lane.y, lane.w, lane.h);
        cairo_fill(cr);
        if (lane.h > 2 * b) {
            cairo_set_source_rgb(cr, kWaveformBorder.r, kWaveformBorder.g,
                                 kWaveformBorder.b);
            cairo_rectangle(cr, lane.x, lane.y, lane.w, b);
            cairo_rectangle(cr, lane.x, lane.y + lane.h - b, lane.w, b);
            cairo_fill(cr);
        }
        cairo_restore(cr);
    }
    if (app.loading || audio.total_frames() <= 0) return;

    const double spp_ov = overview_samples_per_pixel(app, audio);
    if (spp_ov <= 0.0) return;

    // THE REGION HIGHLIGHT'S LANE COLUMNS, resolved ONCE for the highlight's
    // TWO passes — the ground under the bars just below and the ink lift
    // through them after the blit — so the pair cannot straddle two answers on
    // one frame. It is this lane's answer to what region_columns is for the
    // waveform: one derivation, two readers, no chance of a half-lit span.
    // The owner is overview_region_span, the trim line's span owner wearing its
    // other face: the trim's own columns at a sub-window and the whole content
    // width at a full one, which is what makes this recolor and the waveform's
    // overlay recolor the same music in every state.
    int  region_x0 = 0;
    int  region_x1 = 0;
    const bool region_span = app.region.shown &&
        overview_region_span(app, audio, &region_x0, &region_x1);

    // LAYER 1'S REGION HALF — the trim region overlay's second surface
    // (architect 2026-09-04). While the overlay stands, this lane's ground
    // takes the same recolor the waveform's ground takes, over the same two
    // numbers, so the strip shows WHERE IN THE WHOLE SONG the shown window
    // sits. That is the answer the camera used to give: bare `[`'s show half
    // framed the span until the same ruling took the framing off it, on the
    // reading that a user should not be walked through zoom levels to see a
    // window this lane can simply point at.
    //
    // IT IS HALF OF THE HIGHLIGHT, NOT ALL OF IT — the waveform's own shape:
    // the ink lift after the blit (below layer 2) is the other half, so the
    // span reads as ONE LIT REGION rather than as a lit background behind
    // unlit bars, which is exactly why the waveform gained its second pass on
    // 2026-08-18.
    //
    // IT CANNOT DISAGREE WITH THE WAVEFORM'S OVERLAY. Both read
    // app.region.shown — the one bit that is the whole region state — and both
    // derive their span from the RESTING trim bounds on the frame they paint,
    // with nothing stored between them: the waveform through trim_overlay_span
    // into displayed columns, this lane through overview_region_span into its
    // own cell columns.
    // Neither surface can hold a span the other has moved on from.
    //
    // A NEW READER OF kWaveformRegionCanvas AND NOT A NEW COLOUR, exactly as
    // the trim line below is a new reader of kTrimLaneBar. It is an OPAQUE
    // GROUND REPLACEMENT and never a wash: the cached bar surface is cleared to
    // transparent and carries binary-alpha ink alone, so an ink pixel covers
    // this fill and a gap shows it through — the plate's own relationship at
    // the lane's scale, which is the only form the palette admits.
    //
    // CLIPPED TO THE CONTENT BAND, mirroring paint_region_ground's own clip to
    // waveform_content_rect: the recolor leaves the lane's two border rows
    // alone, so the trim line on the head row and the box inside the band are
    // both drawn over it rather than swallowed by it, and the lane's frame
    // still reads as the lane's frame.
    //
    // AT A FULL WINDOW IT RECOLORS THE WHOLE CONTENT BAND, every column, ground
    // and ink alike (architect 2026-09-04, refusing the asymmetry the pair
    // landed with hours earlier: "it is more distracting to have an
    // asymmetry"). The waveform's overlay stands at a full window, so the lane
    // recolors exactly what the waveform recolors and the two surfaces are
    // alike in every state. The 1px trim LINE below keeps its own hide there,
    // which is the line's rule and not this one's: a line the full width of the
    // lane is a picture of nothing, while a lit band is the region itself.
    //
    // NO DAMAGE OF ITS OWN, for the trim line's reason: both halves of the
    // show/hide toggle and every route that writes a trim bound raise
    // Viewport::invalidate_waveform_area, whose one rect runs the window top
    // through the waveform's bottom and so contains this lane.
    //
    // POINTER-INERT like everything else painted here. The lane's press
    // vocabulary is the three gestures the header names — drag the outline,
    // click to teleport, drag the bounds — and a press inside the recolored
    // span is whatever the lane already makes of that column.
    if (region_span) {
        const GuiRect band = overview_content_rect(lane);
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, kWaveformRegionCanvas.r,
                             kWaveformRegionCanvas.g,
                             kWaveformRegionCanvas.b);
        // Inclusive at both ends, the span owner's contract — the same
        // arithmetic the trim line takes below.
        cairo_rectangle(cr, lane.x + region_x0, band.y,
                        region_x1 - region_x0 + 1, band.h);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // Layer 2 — the cached bars, content-band clipped like the plate blit.
    maybe_rebuild_overview_bar_cache(lane);
    if (overview_bar_cache.surface) {
        const GuiRect content = overview_content_rect(lane);
        cairo_save(cr);
        cairo_rectangle(cr, content.x, content.y, content.w, content.h);
        cairo_clip(cr);
        cairo_set_source_surface(cr, overview_bar_cache.surface,
                                 lane.x, lane.y);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    // LAYER 2'S REGION HALF — the highlight's ink lift, the pair's second pass
    // (architect 2026-09-04, accepting the lane's half-highlight as the same
    // defect the waveform's own ground-only pass was on 2026-08-18: a recolored
    // ground behind unlit bars reads as a lit background, not as a lit region).
    // The bars inside the span take kWaveformRegionInk, the same doubled Breeze
    // lift their ground already takes, so the two surfaces that depict the trim
    // region wear ONE construction at two scales.
    //
    // A SECOND OPAQUE COLOUR MASKED THROUGH THE CACHE'S OWN ALPHA, never a
    // translucent wash — the retired form the recolor model rejects, and the
    // mechanism is paint_region_ink's verbatim: the cache surface is cleared to
    // transparent and written by the aliased renderer, so its alpha is BINARY,
    // and masking with it makes every bar pixel inside the span exactly this
    // colour while every gap is left showing the recolored ground beneath.
    //
    // THE MASK IS TAKEN AT THE BLIT'S OWN ORIGIN (lane.x, lane.y), so it lands
    // on exactly the pixels layer 2 just painted, and the clip is (the span)
    // INTERSECT (the content band) — the same two-term clip the waveform's ink
    // pass takes, which is what keeps the lift off the lane's border rows and
    // therefore off the trim line's head row.
    //
    // IT READS THE SAME region_span AS THE GROUND, so the two halves of one
    // highlight cannot disagree, and it runs BEFORE the trim line, the box and
    // the tick — all three still paint over the finished highlight.
    if (region_span && overview_bar_cache.surface) {
        const GuiRect band = overview_content_rect(lane);
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_rectangle(cr, lane.x + region_x0, band.y,
                        region_x1 - region_x0 + 1, band.h);
        cairo_clip(cr);
        cairo_set_source_rgb(cr, kWaveformRegionInk.r, kWaveformRegionInk.g,
                             kWaveformRegionInk.b);
        cairo_mask_surface(cr, overview_bar_cache.surface, lane.x, lane.y);
        cairo_restore(cr);
    }

    // Layer 3 — the trim line on the lane's top row, off its own span owner
    // (overview_trim_span, app_state.cpp), which reports nothing at a full
    // window. Its columns are the lane's cell class like everything else here,
    // and the span is inclusive at both ends because both trim bounds are
    // inclusive authored frames — the box below is half-open because a viewport
    // end is exclusive. It is the lane's ALWAYS-ON trim presence, where layer
    // 1's region recolor above is the emphasis that comes and goes with the
    // overlay — one span body behind both, so the two can only ever agree
    // about where the trim is; they part on one case alone, the full window,
    // where the line hides and the recolor takes the whole band (the fork is at
    // the two faces, app_state.cpp, and the reasons are there).
    // The line is pointer-inert: the lane's press vocabulary
    // is the three gestures the header names and nothing more, so do not add a
    // hit test for it — a press on this row is whatever the lane already makes
    // of that column.
    {
        int tx0 = 0;
        int tx1 = 0;
        if (overview_trim_span(app, audio, &tx0, &tx1)) {
            cairo_save(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            cairo_set_source_rgb(cr, kTrimLaneBar.r, kTrimLaneBar.g,
                                 kTrimLaneBar.b);
            cairo_rectangle(cr, lane.x + tx0, lane.y,
                            tx1 - tx0 + 1, 1);
            cairo_fill(cr);
            cairo_restore(cr);
        }
    }

    // Layer 4 — the viewport box, off the ONE span owner the lane's hit
    // geometry shares (overview_box_span, app_state.cpp — the arithmetic was
    // this painter's inline block until the box grew grab handles, and the
    // hoist is what makes a grabbed edge exactly a painted one). The visible
    // span in the ACTIVE domain, inverse-mapped to source columns in target
    // view inside the owner (the header's domain rule).
    {
        int x0 = 0;
        int x1 = 0;
        if (overview_box_span(app, audio, &x0, &x1)) {
            const GuiRect band = overview_content_rect(lane);
            cairo_save(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            cairo_set_source_rgb(cr, kOverviewBoxLine.r, kOverviewBoxLine.g,
                                 kOverviewBoxLine.b);
            const int bx = lane.x + x0;
            const int bw = x1 - x0;
            // 1px outline: two horizontals across the span's content band,
            // two verticals down it. A 1px-wide span degenerates to one
            // vertical (the rects coincide — cairo draws them once over).
            // The right outline sits at x1 − 1, the column CONTAINING the last
            // visible frame under the lane's cell class (declared above
            // overview_samples_per_pixel, app_state.cpp — the same class the
            // tick below takes, which is what keeps the tick inside this
            // outline while the playhead is inside the viewport).
            cairo_rectangle(cr, bx, band.y, bw, 1);
            cairo_rectangle(cr, bx, band.y + band.h - 1, bw, 1);
            cairo_rectangle(cr, bx, band.y, 1, band.h);
            cairo_rectangle(cr, bx + bw - 1, band.y, 1, band.h);
            cairo_fill(cr);
            cairo_restore(cr);
        }
    }

    // Layer 5 — the playhead tick: the scanner while live (its precise
    // position, the value the waveform scanner paints from), the resting
    // cursor otherwise; through the ONE column owner the damage sites share
    // (overview_tick_column), full LANE height across both border rows.
    {
        const double active_pos = app.playhead_scanner_active
            ? app.playhead_scanner_precise
            : static_cast<double>(app.playhead_cursor_sample);
        const int col = overview_tick_column(app, audio, active_pos);
        if (col >= 0) {
            cairo_save(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            cairo_set_source_rgb(cr, kPlayheadStem.r, kPlayheadStem.g,
                                 kPlayheadStem.b);
            cairo_rectangle(cr, lane.x + col, lane.y, 1, lane.h);
            cairo_fill(cr);
            cairo_restore(cr);
        }
    }
}

// -- THE BOTTOM ROW'S MODAL STATE ----------------------------------------
//
// THE MODAL LIVES ON THE BOTTOM ROW (architect 2026-08-13, scrapping the
// centered box of 2026-08-12: "it looks sloppy — no compositor drop shadow,
// and faking one wouldn't work"). NOTHING ABOUT THE RENDERER MOVED — one
// window, one surface, one frame, one painter, exactly as before; only the
// modal's RECTANGLE moved from the window's centre onto this row, so this is
// emphatically not the scrapped second-toplevel model (conventions.md carries
// that do-not-re-propose). WHILE A PROMPT OR A DIALOG EDITOR STANDS THE ROW
// YIELDS WHOLE: all EIGHTEEN buttons — the transport three, the four
// single-marker verbs with the Edit flag button, the Marker Measure and Add to
// Selection behind
// them, the marker-walk three and the four arrows — plus the clock and the row's three separators stand
// down, nothing negotiates
// for space,
// and paint_modal_dialog paints the modal into the lane they left.
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
    if (text_editor::is_active(app.measure_offset_editor)) {
        prefix = kMeasureOffsetEditorPrefix;
        return &app.measure_offset_editor;
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
// (the fourth, the same day: its Cancel-alone row), the AV SYNC STATS PANEL
// (the fifth, 2026-09-03: its Copy to Clipboard · Close row) or any dialog
// editor. The
// top-strip FLAG editor is deliberately absent: it is positional and
// pointer-transparent, not a dialog, and it never takes this row.
static bool modal_owns_bottom_row(AppState& app) {
    if (app.prompt.active) return true;
    if (app.render_player.active) return true;
    if (app.picker.active) return true;
    if (app.stats_panel.active) return true;
    std::string prefix;
    return dialog_editor_to_paint(app, prefix) != nullptr;
}

// -- GuiPaintHandler::paint_bottom_strip ---------------------------------

void GuiPaintHandler::paint_bottom_strip(cairo_t* cr) {
    // THE UNIFIED BOTTOM ROW (architect-ruled 2026-08-12, rows 8 and 9 merged
    // into ONE lane — under the waveform then, on the WINDOW'S FOOT from the
    // same day's relayout commit B, which also moved the arrows FLUSH RIGHT,
    // apart from the one day a STATUS BAR stood under it, 2026-08-29):
    // the transport three
    // on the left at the icon row's boxes with the monospace clock behind their
    // separator (left-aligned since 2026-08-18, lane-centred before), and —
    // flush right since 2026-08-15 — the four marker verbs, the marker-walk
    // three and the four cardinal arrows behind two more of the ruled
    // separators. THAT IS THE WHOLE ROSTER since
    // 2026-08-13, when the architect moved the STATUS CHAIN — the critical
    // chip and section C's precedence ladder — up into the TAB ROW (the chain
    // was deleted there on 2026-08-29; its critical chip became the cards and
    // its two surviving STATE strings came back to this row that evening, not
    // as a ladder but as the ONE STATE CELL right of the clock, which is the
    // buttons-and-clock body's tenant and not a roster member): this
    // painter owns the lane's CHROME (ground + border-top) and nothing else;
    // the buttons and the clock are
    // paint_bottom_row_buttons_and_clock, called from here onto the grounded
    // band (the cluster's tables and the clock's metrics live beside that
    // body).
    // THE ROW HAS A MODAL STATE since 2026-08-13, in which those tenants
    // stand down and the lane carries the prompt or the dialog
    // editor instead (the ruling and the fork are at modal_owns_bottom_row,
    // just above; the modal's own layout is paint_modal_dialog's) — and with
    // the chain gone there is nothing left on the lane to negotiate with,
    // which is what makes that yield clean.
    //
    // WHAT DIED WITH THE 2026-08-01 COLLAPSE, and why it is not missing: the
    // S/T · W/P · A/B view readout and the "(read-only)" token. Rows 3 and 4
    // display all three view states as lit buttons and tabs, and the icon
    // row's read-only toggle shows the lock (the tabs' own padlocks did until
    // 2026-08-14), so the letters were restating what the redesigned
    // rows say in their own vocabulary. The dirty mark's section moved to the
    // WINDOW TITLE beside the project name (GuiPlatform::apply_window_title)
    // — the title is the mark's only home now.
    //
    // The row paints on EVERY frame class (loading, blank, loaded) like the
    // redesigned rows above it: the clock reads 00:00.000 with no source, and
    // the buttons must be visible on every frame class their press claim is
    // live on.
    const GuiRect lane    = bottom_row_area(app);
    const GuiRect content = bottom_row_content_area(app);
    const int     border  = bottom_row_border_h_px();
    if (lane.w <= 0 || lane.h <= 0 || content.h <= 0) return;

    // THE ROW'S GROUND AND ITS ONE BORDER-TOP — the waveform-side seam, row
    // 8's box-model convention kept whole by the unification (row 9's second
    // border, the window-foot seam, died with the row's window-edge position;
    // the window-foot seam's final retirement note is at render.h's row-7
    // crop block, on the corrected provenance).
    // Hard-coded per the redesign's color ruling; the ground erases whatever
    // chrome render_background laid down, so the strip does not depend on the
    // kBackground constant happening to hold the same value.
    {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_set_source_rgb(cr, kRedesignTabLine.r, kRedesignTabLine.g,
                             kRedesignTabLine.b);
        cairo_rectangle(cr, lane.x, lane.y, lane.w, border);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, kRedesignContentGround.r,
                             kRedesignContentGround.g,
                             kRedesignContentGround.b);
        cairo_rectangle(cr, content.x, content.y, content.w, content.h);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    // THE ROW YIELDS TO THE MODAL (2026-08-13; the fork and the ruling are at
    // modal_owns_bottom_row just above). The ground and the border-top above
    // are the ROW'S whatever it carries — the modal paints on the row's own
    // ground, having no box of its own — and every other tenant stands down
    // here: paint_modal_dialog owns the lane from this frame until the
    // dialog's closer.
    //
    // THE EIGHTEEN BUTTONS PUBLISH ZERO RECTS rather than stranding the last
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
            publish_button_face(cr, app, audio, playback, target_render, def.id,
                                GuiRect{0, 0, 0, 0});
        }
        // The RIGHT BLOCK's three groups stand down with them — the MARKER
        // VERBS (2026-08-18), the MARKER-WALK GROUP (2026-08-15) and the four
        // ARROWS. Each is a tenant of this lane like every other member, and
        // the modal takes the lane whole. FOUR TABLES, NOT FIVE, since the
        // relayout: the arrows' slots carried a second table until then (the
        // history companions' cluster, which had to stand down too whichever
        // of the two the mode would have painted), and those four are the icon
        // row's again.
        for (const TransportRowDef& def : kMarkerVerbGroup) {
            publish_button_face(cr, app, audio, playback, target_render, def.id,
                                GuiRect{0, 0, 0, 0});
        }
        for (const TransportRowDef& def : kTransportWalkGroup) {
            publish_button_face(cr, app, audio, playback, target_render, def.id,
                                GuiRect{0, 0, 0, 0});
        }
        for (const TransportRowDef& def : kTransportArrowGroup) {
            publish_button_face(cr, app, audio, playback, target_render, def.id,
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
    // STATE CELL right of the clock since 2026-08-29's evening fold, painted
    // by paint_bottom_row_buttons_and_clock — one string, not a ladder, and
    // the resolved readout retired with the one-day bar that carried it.)
    paint_bottom_row_buttons_and_clock(cr);
}

// -- GuiPaintHandler::paint_modal_dialog ---------------------------------
//
// THE MODAL SURFACE — THE BOTTOM ROW (architect 2026-08-13, scrapping the
// centered box he ratified the day before: "it looks sloppy — no compositor
// drop shadow, and faking one wouldn't work"). It hosts the PROMPTS and the
// FIVE modal editors (settings / load / commit-title / measure paste-offset /
// BPM); the top-strip
// FLAG editor is deliberately NOT a dialog — it is positional, editing the
// marker where it stands, and stays the pointer-transparent unrolled flag
// (render_flag_editor_box).
//
// ONE RENDERER, ONE WINDOW, ONE FRAME — unchanged. Nothing here creates a
// surface, a window or a second buffer: only the modal's RECTANGLE moved,
// from the window's centre to the bottom row, so this is NOT the scrapped
// second-toplevel model (that attempt was reverted byte-exact and is recorded
// do-not-re-propose in conventions.md). The row yields whole while a modal
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
//   is plain") — the LAST, the Escape sentinel, on every prompt but the render
//   player's load confirmation, which is raised on its FIRST (PromptState's
//   PromptInitialFocus owns the choice); Enter answers whichever it is, the
//   assignment site is a few dozen lines into the body below and the whole
//   supersession is at PromptState.
//   AN EDITOR — its prefix as the LABEL at the left pad, then the pending
//   buffer in a DARK INSET FIELD (editor.png's look), then OK and Cancel. The
//   field is the existing text_editor machinery — selection, caret,
//   click-to-caret, byte-identical editing — and the red flash RECOLORS THE
//   FIELD in the marker-flag red pair (the one invalid red, called not
//   copied).
//
// EVERY BUTTON CARRIES A TOOLTIP (architect 2026-08-13: "we just do a tooltip
// just like the regular icon tooltips"), through the roster's own machinery
// end to end — the same 700ms dwell, the same box, the same painter, the same
// AppState::redesign_tooltip state, whose owner names either surface now. The
// TEXT is composed per button from the word it wears plus the key it
// dispatches (modal_dialog_button_hint, app_state.h) and published in the
// stash beside the rect; the DWELL is stamped by this surface's own hover walk
// (update_modal_dialog_hover), which is independent of the press arm, so
// holding a button neither starts nor stops a hint.
//
// THE BUTTONS ARE THE ARCHITECT'S EXPLICIT MIX: the deleted toolbar row's box
// (row 2's 32px height and its own label pads, the icon slot dropped — these
// buttons carry words, not glyphs) wearing the ICON ROW's face colors — "the
// modal buttons should basically look like the icons, but with a resting
// outline" (2026-08-13). FIVE FACES, the ladder stated at the paint site
// below: REST is that resting 1px kRedesignLine outline over the bare row
// ground, HOVER swaps it for the accent, PRESSED adds the icon row's own 30%
// accent interior, FOCUSED the sampled #2d4655 fill plus a 2px halo outside
// the box, and there is no selected and no disabled face at all.
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
// displaced; the rest are the surviving sampled constants (the field colors
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
//   kModalFocusRingPx 2  — the keyboard focus halo, RESERVED around every
//                          button and painted for the focused one (the
//                          reflow-free rule and the per-scale fit are at the
//                          constant).
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

constexpr double kModalButtonGapPx    = 8.0;
constexpr double kModalFieldHeightPx  = 31.0;   // includes its two 1px borders
constexpr double kModalFieldBorderPx  = 1.0;
constexpr double kModalFieldPadXPx    = 7.0;
// THE LABEL'S AUTHORED 1px DROP (architect-measured 2026-08-21, looking at the
// painted row: the dialog label — "Setting:" and its siblings — sat one pixel
// high). The kClockCellOffsetYPx arrangement exactly: an authored offset, not
// a derivation, added to the LABEL's baseline alone. The FIELD's ink keeps the
// shared field-band baseline untouched — the field metrics are ruled correct —
// so the label and buffer part by this one authored pixel. Rides gui_scale
// like every authored length here.
constexpr double kModalLabelOffsetYPx = 1.0;
constexpr double kModalFieldWidthPx   = 520.0;  // authored; see the block above
// THE DIALOG BUTTONS' BOX — the deleted toolbar row's own anatomy, OWNED here
// since the 2026-08-12 relayout dissolved that row (these buttons read row
// 2's constants until then; the architect's original mix — "the size should
// be the size of the save, undo, redo, render... the behavior/colors should
// be the icon buttons'" — is unchanged, only the numbers' home moved). The
// 32 IS row 2's derivation frozen: its 44px content minus its two 6px
// vertical button margins, the box the crop's own buttons measure exactly;
// the 9/10 pads are its label paddings (the row-2 record, kdenlive-redesign
// .md, keeps the crop provenance). It fits the bottom row's 46px content band
// with 7px of margin either side — the same box, in the same band, that the
// row's own buttons wear, which is why the band's 2026-08-14 shrink cost the
// dialog nothing.
constexpr double kModalBtnBoxPx       = 32.0;
constexpr double kModalBtnPadLeftPx   = 9.0;
constexpr double kModalBtnPadRightPx  = 10.0;
// THE FOCUS RING'S WIDTH — the 2px halo a keyboard-focused button wears
// OUTSIDE its box (kModalFocusRing, render.h, where the sampled face is
// recorded). RESERVED FOR EVERY BUTTON AND PAINTED FOR ONE, which is what
// makes moving the focus reflow nothing: the cluster's right anchor and the
// content's right bound both spend it, and the buttons themselves never move.
// IT FITS AT EVERY SCALE BY CONSTRUCTION, both axes riding one factor: the
// vertical margin is (46 - 32)/2 = 7 authored px against the ring's 2 — at 50%
// the floored ring is 1px against a 3px margin (the row's 23px content less
// the 16px box, halved), and at 200% it is 4 against 14 — and horizontally the
// 8px inter-button gap absorbs one ring from each neighbour with half of it to
// spare. (The margin was 9 while the row authored its own 50px content; it
// took the icon row's 46 on 2026-08-14 and the ring still clears at every
// scale, which was checked rather than assumed.)
constexpr double kModalFocusRingPx    = 2.0;

// THE MODAL'S FACE STATE, dropped together. The four indices all name slots
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
    app.modal_dialog_hovered         = -1;
    app.modal_dialog_pressed         = -1;
    app.modal_dialog_press_inside    = false;
    app.modal_dialog_press_shift     = false;
    app.modal_dialog_press_ms        = 0;
    app.modal_dialog_focus           = -1;
    app.modal_dialog_focus_active    = false;
    app.modal_dialog_key_pressed     = -1;
    app.modal_dialog_key_pressed_key = 0;
    app.modal_dialog_field_hovered   = false;
    // THE LIST BIT is modal face state too (2026-08-28): whether the ring's
    // -1 means the folder overlay's list, under all three of the overlay's
    // contents alike (the panel's band lands the ring at -1 too, showing
    // nothing there — the ring's own record, input_key_dispatch.cpp).
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
    dlg.valid   = false;
    dlg.owner   = AppState::ModalDialogOwner::None;
    dlg.session = 0;
    dlg.box     = GuiRect{0, 0, 0, 0};
    dlg.field   = GuiRect{0, 0, 0, 0};
    dlg.scrub   = GuiRect{0, 0, 0, 0};
    dlg.clock   = GuiRect{0, 0, 0, 0};
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
    // neither live beside an editor.
    const bool picker_up = !prompt_up && !player_up && app.picker.active;
    // THE AV SYNC STATS PANEL, the fifth owner (2026-09-03): the same rank
    // again — the three list owners never stand together, each opener refusing
    // under the other two, and none stands beside an editor. THIS IS ONE OF
    // THE THREE PLACES THE RANKING IS SPELLED (AppState::
    // modal_dialog_live_session and modal_dialog_stash_current are the others)
    // and they must agree or the owner-tag doctrine breaks.
    const bool stats_up = !prompt_up && !player_up && !picker_up &&
                          app.stats_panel.active;
    std::string prefix;
    text_editor::State* ed =
        (prompt_up || player_up || picker_up || stats_up)
            ? nullptr : dialog_editor_to_paint(app, prefix);
    if (!prompt_up && !player_up && !picker_up && !stats_up && ed == nullptr) {
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
    // open inside one dispatch batch, the round-15 finding, which is why this
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
    // and border-top are already painted (paint_bottom_strip's chrome runs on
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
    cairo_scaled_font_t* font = select_bottom_row_face(cr);
    cairo_font_extents_t fe;
    cairo_scaled_font_extents(font, &fe);

    // The row's own left/right margin — the modal sits on the same pad the
    // tenants it displaced sit on, which since 2026-08-14 is literally the
    // same accessor they read (icon_row_pad_x, paint_handler.h).
    const int pad   = icon_row_pad_x();
    const int bgap  = scaled_px(kModalButtonGapPx);
    // The deleted toolbar row's button box, owned by the dialog since the
    // 2026-08-12 relayout (kModalBtnBoxPx — 32 = row 2's 44 content minus its
    // two 6px margins, the derivation frozen at the constant).
    const int btn_h = scaled_px(kModalBtnBoxPx);
    const int btn_pad_l = scaled_px(kModalBtnPadLeftPx);
    const int btn_pad_r = scaled_px(kModalBtnPadRightPx);
    // The focus halo's reserved band (kModalFocusRingPx, above): spent by the
    // layout for EVERY button whether or not one is focused, so the ring can
    // never push the row around when the focus moves.
    const int ring = scaled_px(kModalFocusRingPx, 1);

    // -- The buttons' words and widths, shaped up front (the layout needs the
    //    row's total before anything can be placed). --
    // A BUTTON IS A WORD OR A GLYPH (2026-08-28, the render player's row):
    // `glyph` set means the button is the roster's own icon in a
    // kModalBtnBoxPx SQUARE — the THREE transport buttons wear
    // MediaSkipBackward, MediaPlaybackStart (swapped to MediaPlaybackPause
    // while the transport is live, the bottom row's one-button-two-faces
    // rule) and MediaSkipForward — the row was FOUR, MediaPlaybackStop among
    // them, until the player's Stop retired 2026-09-01, and the middle
    // button's live face stays the pause it took from R36 — the REPEAT
    // ONE toggle wears
    // MediaRepeatSingle in both its states, the UP button GoParentFolder, and
    // since 2026-09-01 the LOAD IN PLACE wears DialogOkApply and CLOSE
    // WindowClose — while a word button keeps the
    // label box every prompt and editor button has always had. THE PLAYER'S
    // ROW IS ALL GLYPHS NOW and the word kind is the other three owners'
    // alone: a prompt's answers, the dialog editors' OK / Cancel and the
    // picker's Cancel. A GLYPH BUTTON
    // WEARS NO RESTING OUTLINE (architect R25: "icon buttons have no border —
    // the regular transport has none; text buttons keep theirs"), which is the
    // face ladder's one fork on this field. `lit` is the roster's SELECTED
    // face — the row's one lamp, Repeat one, and no other button has a state
    // to say. `player_act` is the player's dispatch vocabulary; the other two
    // stay zero/false on its buttons.
    struct DialogButtonPlan {
        std::string label;
        char        response_key = 0;
        bool        editor_ok    = false;
        AppState::PlayerButtonAct player_act = AppState::PlayerButtonAct::None;
        // The AV sync stats panel's own vocabulary (2026-09-03), zero on every
        // other owner's buttons exactly as `player_act` is.
        AppState::StatsButtonAct  stats_act  = AppState::StatsButtonAct::None;
        bool        glyph        = false;
        bool        lit          = false;
        // The disabled face (architect 2026-08-30, the player's row) — the
        // live predicate's answer at plan time, published on the record
        // below. ONE OWNER READS IT, re-greped 2026-09-03: the player's seven
        // through render_player_button_enabled; true on every other owner's
        // buttons. (The stats panel's Copy to Clipboard read it for one day,
        // off a backend capability that is gone — the tablet's clipboard now
        // publishes too, so nothing greys that button ahead of the press.)
        bool        enabled      = true;
        icons::Icon icon         = icons::Icon::MediaPlaybackStart;
        std::string tooltip;     // the player's own; empty = the composer's
        std::string tooltip2;    // the modifier line; empty = the one-line form
        int         w            = 0;
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
        // THE PLAYER'S ROW, in painted order (architect 2026-08-28, R25; the
        // transport is THE MAIN WINDOW'S OWN TRIPLE since 2026-09-01): the
        // two SKIPS around Play/Pause, on Home and End since 2026-08-31 and
        // wearing the skip glyphs still —
        // then the
        // separator, the scrub, the clock and the second separator (all three
        // laid out in the player's own branch below), then the REPEAT ONE
        // lamp, the UP button, and the last two FLUSH RIGHT: Load in place ·
        // Close, GLYPH buttons since 2026-09-01 like every other button on
        // this row.
        // Close LAST, the escape sentinel by construction as every prompt has
        // it. The plan's order is also the ring's, so Tab walks the row left to
        // right. A STOP BUTTON SAT AFTER PLAY/PAUSE from R36 (2026-08-28,
        // Audacious's own order) until the act retired.
        // THE PAUSE FACE IS TRUTHFUL (architect 2026-08-31, R6): the button
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
        // END'S IDLE BIT IS seek_to's SECOND REFUSAL, ASKED IN ITS OWN ORDER
        // (2026-09-01): the act refuses first for NO ITEM and then for an idle
        // transport, so the hint may say "At the end" only past the first —
        // otherwise a freshly opened player, which is idle with nothing bound,
        // said where a track it does not have was standing.
        hint.end_idle      = !app.render_player.item.empty() &&
                             app.render_player.frames > 0 &&
                             app.render_player.transport ==
                                 AppState::RenderPlayer::Transport::Idle;
        // THE SHIFT LINES COMPARE DESTINATIONS, not the twins' walls alone
        // (codex round A, 2026-09-01): the line names the FILE the shifted
        // press plays, so it must drop wherever the plain press already
        // plays that file. Home's two destinations are home()'s own — the
        // PREVIOUS entry inside the previous-track window
        // (render_player_home_takes_previous, the fork's one owner, resolved
        // just above) and otherwise a seek that leaves the item where it is —
        // against the twin's `folder.front()`, index 0; both land at frame 0
        // of the file they name, so the index decides. On the folder's second
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
        // END HAS NO SUCH WINDOW: the plain act is seek_to(frames), always
        // inside the item and never a track change, so it can never reach the
        // folder's last wav — the twin's own wall is the whole question.
        hint.end_shift_differs =
            render_player_last_in_item_folder_actionable(app);
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
        // THE SKIP GLYPHS ARE UNTOUCHED BY THE 2026-08-31 REMAP: the acts
        // are Home and End (the roster's own two transport skips wear the
        // same pair over the same keys), so the icon says what it always
        // said — skip to this track's start, skip to its end.
        glyph_button(AppState::PlayerButtonAct::Home,
                     icons::Icon::MediaSkipBackward);
        // PLAY/PAUSE WEARS THE PAUSE GLYPH ON THE PAUSE FACE (above), not the
        // stop square it wore until R36 gave the row a Stop button of its own.
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
        glyph_button(AppState::PlayerButtonAct::End,
                     icons::Icon::MediaSkipForward);
        glyph_button(AppState::PlayerButtonAct::RepeatOne,
                     icons::Icon::MediaRepeatSingle,
                     app.render_player.repeat_one);
        // UP, THE `..` ROW'S ACT ON A BUTTON (architect 2026-09-01, with the
        // player's move inside `tmp/`): the listings carry no `..` row any
        // more, so the way out of a batch folder is here — beside the lamp,
        // ahead of the right-flushed pair, on Breeze's own GO-PARENT-FOLDER, the
        // open folder with an arrow rising out of it (architect, later the same
        // day: it wore the roster's `go-up` chevron for the button's first
        // hours, and a chevron says "up" about a NUMBER while the act here is
        // leaving a DIRECTORY). GoUp is untouched — the roster's bare-Up
        // transport button still wears it, a def being a glyph and several
        // buttons being free to wear one. It greys at the root through the
        // act's own wall.
        glyph_button(AppState::PlayerButtonAct::Up,
                     icons::Icon::GoParentFolder);
        // THE LAST TWO ARE GLYPHS SINCE 2026-09-01 (architect, with the icon
        // row's Load in place moving to the history group): "the media player
        // button should get the checkmark glyph then. Close should then get a
        // glyph also, to avoid being the odd one out: window-close.svg". So
        // the LOAD wears DIALOG-OK-APPLY — the very checkmark the icon row's
        // button wears for the same act one surface over — and CLOSE wears
        // Breeze's WINDOW-CLOSE X, the roster's existing entry (the
        // notification cards' dismiss is its other reader; a def is a glyph
        // and several buttons are free to wear one). The row is SEVEN GLYPH
        // BUTTONS and no word button, so its faces take the glyph fork whole.
        // Close stays LAST, the escape sentinel by construction, and both keep
        // their acts, their keys and their hints unchanged.
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
        //
        // THE STATS PANEL'S ROW IS **Copy to Clipboard · Close** (architect
        // 2026-09-03), the picker's shape with the word its own act names: the
        // panel reads and nothing is being answered, so "Cancel" would name a
        // decision it does not ask for. Close stays LAST, the escape sentinel
        // by construction, and the hint composer's "Close (Esc)" follows for
        // free — Esc being the router's own key under the product's one
        // spelling (spell_chord's head, gui_input.h).
        //
        // COPY TO CLIPBOARD IS A WORD BUTTON AND DELIBERATELY NOT A GLYPH
        // (architect 2026-09-03: "we don't need a glyph button, because this
        // is more like a modal, so you can literally just write Copy to
        // clipboard on the button face"). The player's row is all glyphs; the
        // three word-button owners are the prompts, the dialog editors and
        // the picker's Cancel, and this joins them — same label box, same
        // resting outline. ITS WORDS ARE A CONTROL'S NAME, so they are TITLE
        // CASE (the two-class rule, the capitalization block at the head of
        // this file: a name is Title Case, a description is a sentence). It
        // read "Copy to Clipboard" for the few hours between the button
        // landing and the rule that classed it, the product's other word
        // buttons all being single words that settle nothing.
        // ITS HINT IS WRITTEN OUT rather than composed: the default composer
        // spells one key off `response_key`/`editor_ok`, and this button's key
        // is a CHORD (Ctrl+C in route_stats_panel_key), so the tooltip carries
        // the product's own modifier spelling directly. One line — the button
        // admits no modified press, so there is no second line to add.
        // ITS FACE NEVER GREYS. It read a backend capability for one day
        // (2026-09-03, codex) — whether the clipboard reached another program
        // at all — and both backends answer yes since the tablet's
        // ClipboardManager road landed that evening, so the condition had no
        // producer left and went with the capability. Every other half of the
        // verdict (the Wayland serial, the data device, Java's own refusal)
        // is unknowable ahead of the press and is the CARD's
        // (copy_stats_panel_report), never the face's. No twin question
        // arises: the button admits no modified press.
        if (stats_up) {
            DialogButtonPlan copy;
            copy.label     = "Copy to Clipboard";
            copy.stats_act = AppState::StatsButtonAct::CopyReport;
            copy.tooltip   = "Copy to Clipboard (Ctrl+C)";
            plan.push_back(std::move(copy));
        } else if (!picker_up) {
            DialogButtonPlan ok;
            ok.label     = "OK";
            ok.editor_ok = true;
            plan.push_back(std::move(ok));
        }
        DialogButtonPlan last;
        last.label = stats_up ? "Close" : "Cancel";
        if (stats_up) last.stats_act = AppState::StatsButtonAct::Close;
        plan.push_back(std::move(last));
    }
    // EVERY BUTTON'S WIDTH, and the ONE CLUSTER'S total behind it — the total
    // is the SINGLE-CLUSTER owners' (a prompt, the picker, an editor), whose
    // cap `buttons_x_max` is measured against it; the player spends the same
    // widths on its own two-cluster arrangement and reads no total.
    int buttons_w = 0;
    for (size_t i = 0; i < plan.size(); ++i) {
        if (plan[i].glyph) {
            // A glyph button is the box's own square.
            plan[i].w = btn_h;
        } else {
            const double lw =
                text_shape::shape_text_run(font, plan[i].label).width_px;
            // CEIL, NOT nearbyint: a measured run width is fractional and the
            // box it sizes must CONTAIN the ink, so it rounds UP — rounding
            // to nearest could clip the label's last column inside its own
            // pads. The same round-up rule the clock cell's damage box takes
            // on its width (paint_bottom_strip).
            plan[i].w = btn_pad_l + static_cast<int>(std::ceil(lw)) + btn_pad_r;
        }
        buttons_w += plan[i].w + (i > 0 ? bgap : 0);
    }

    // A PROMPT IS RAISED WITH PASSIVE FOCUS ON A BUTTON (architect 2026-08-13,
    // the ruling that gave this prompt system an Enter answer — PromptState
    // owns the supersession and the two facts that make it safe, the first of
    // which is that the last button is always the ESCAPE SENTINEL). WHICH
    // button is the RAISE'S OWN CHOICE (PromptInitialFocus, carried on the
    // question since 2026-08-28): the last on every prompt but THE LOAD
    // CONFIRMATION, which asks for its FIRST — its OK is one
    // undo entry away from being undone rather than destructive. That one
    // prompt body carries both `'` subjects (the player's entry and the `h`
    // view's walk member), so the two roads answer Enter alike. This is the
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
    // and its halo inside the right pad, and whatever precedes it CLIPS (the
    // message) or SHRINKS (the field) against that cap. In the pathological
    // case where the buttons alone are wider than the lane, the cap floors at
    // the left pad and the cluster runs past the right one: the content is
    // then zero-wide and the buttons are what is left, which is the same
    // choice made twice.
    //
    // THE FOCUS RING IS RESERVED, NOT PAINTED, on both ends of the cluster —
    // one ring inside the right pad so the last button's halo cannot touch the
    // window edge, one inside the left clearance so the first button's cannot
    // touch the message or the field. Between neighbours the 8px gap absorbs
    // both halves with room to spare (the per-scale fit is at
    // kModalFocusRingPx). So the focus can move anywhere on the row without
    // reflowing it, which is the whole point of reserving.
    const int cx0 = content.x + pad;                    // content left
    const int cx1 = content.x + content.w - pad;        // the row's right pad
    const int buttons_x_max = std::max(cx0, cx1 - ring - buttons_w);
    const int btn_y = content.y + (content.h - btn_h) / 2;
    int buttons_x0 = cx0;   // set by whichever branch runs, below

    // THE CORNER RADIUS, resolved once for the whole modal: the icon buttons'
    // own kIconCornerRadiusPx on their own scale. The buttons below take it,
    // and since 2026-08-13 so does the EDITOR'S FIELD — the architect's "the
    // same rounded corner as the buttons have, the same corner radius" — so
    // one expression describes every corner on this surface.
    const double rad = std::nearbyint(kIconCornerRadiusPx * gui_scale_factor());

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
        cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                             kRedesignLabel.b);
        text_shape::show_shaped_run(
            cr, msg, static_cast<double>(cx0),
            redesign_baseline(font, static_cast<double>(content.y),
                              static_cast<double>(content.h)));
        cairo_restore(cr);
    } else if (player_up) {
        // -- THE RENDER PLAYER'S ROW (architect 2026-08-28, R25 — the relaid
        //    row): TRANSPORT · separator · SCRUB · CLOCK · separator · REPEAT
        //    ONE ......... LOAD IN PLACE · CLOSE, the last two flush right.
        //    The arrangement is Audacious-Qt's under Breeze Dark, which is
        //    what the architect ruled off his own screen — "not the
        //    screenshot as a whole, just the layout: transport, separator,
        //    scrub, then the time, then another separator, then the repeat
        //    button".
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
        //    except the scrub.
        //
        //    THIS BRANCH LAYS OUT AND PAINTS EVERYTHING BUT THE BUTTONS: it
        //    writes each button's own x (the walk below paints where it is
        //    told) and draws the two separators, the slider and the clock
        //    between them. The items are disjoint, so painting them ahead of
        //    the buttons is only an ordering of convenience. --
        dlg.owner  = AppState::ModalDialogOwner::Player;
        buttons_x0 = cx0;   // unread on this branch; the plan carries the x's

        // THE SEPARATOR IS THE BOTTOM ROW'S OWN (kTransportSep*, above): a 1px
        // kRedesignTabLine line the height of the button box, with 5px of air
        // each side. Breeze's toolbar separator is the same 1px line in the
        // same #4c4e51 — its own item is 8px wide with 4px of air and 3, and
        // the ruled height is the toolbar row's — so this row's sampled
        // numbers ARE that separator at row 8's metrics, and the modal row
        // (which stands in row 8's lane, in place of exactly those buttons)
        // takes them rather than authoring a second spec for the same line in
        // the same place.
        const int ggap     = scaled_px(kIconBtnGapPx);
        const int sep_gap  = scaled_px(kTransportSepGapPx);
        // THE AIR AROUND THE SCRUB, all three of it, is ROW 8'S OWN SEPARATOR
        // → TIMESTAMP DISTANCE (architect 2026-08-29; the owner and its
        // derivation are at kTransportSepToClockPx): left separator → scrub,
        // scrub → clock, clock → right separator. One number, read three
        // times, and it is the number the row under this modal already spends
        // between its divider and its digits.
        const int sep_pad  = scaled_px(kTransportSepToClockPx);
        const int sep_w    = scaled_px(kTransportSepWidthPx, 1);
        const int sep_h    = scaled_px(kTransportSepHeightPx);
        const int sep_y    = content.y + (content.h - sep_h) / 2;
        const auto paint_separator = [&](int line_x) {
            cairo_save(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            cairo_set_source_rgb(cr, kRedesignTabLine.r, kRedesignTabLine.g,
                                 kRedesignTabLine.b);
            cairo_rectangle(cr, line_x, sep_y, sep_w, sep_h);
            cairo_fill(cr);
            cairo_restore(cr);
        };

        // THE CLOCK'S CELL IS MEASURED BEFORE ANYTHING IS PLACED, its width
        // being one of the fixed terms the scrub's own is what is left over.
        // ONE FACE, THREE CELLS since 2026-09-03 (this pair since 2026-08-28;
        // the AV sync stats panel is the third — the set's own record is at
        // this file's bottom-row text block): the row-8 clock's face, size,
        // cell metrics and authored offsets, so the digits never walk between
        // the project's clock and the player's.
        gui_select_font_face(cr, GuiFontFamily::Mono);
        const double csize = clock_font_size_px();
        cairo_set_font_size(cr, csize);
        cairo_scaled_font_t* cfont = cairo_get_scaled_font(cr);
        const double cell_w = clock_cell_width_px(cfont, csize);
        const double csep_w = text_shape::shape_text_run(cfont, " / ").width_px;
        const int clock_w   = static_cast<int>(std::ceil(2.0 * cell_w + csep_w));

        // -- The walk: the fixed left run, the right-flushed PAIR (word
        //    buttons until 2026-09-01, glyph boxes since), and the scrub
        //    taking what is between them. --
        int px = cx0;
        plan[0].x = px; px += btn_h + ggap;      // Home (skip back)
        plan[1].x = px; px += btn_h + ggap;      // Play / Pause
        plan[2].x = px; px += btn_h;             // End (skip forward)
        // The separator keeps the spec's own 5px on its BUTTON side; the
        // scrub side takes the distance above, exactly as row 8's clock does.
        const int sep1_x   = px + sep_gap;
        const int scrub_x0 = sep1_x + sep_w + sep_pad;

        // THE RIGHT-FLUSHED CLUSTER IS TWO GLYPH BOXES since 2026-09-01 (it
        // was Load in place · Close as WORDS, measured runs in label boxes):
        // both are the box's own square now, so the cluster is two boxes and
        // the GLYPH GAP between them — right-flushed inside the reserved ring
        // exactly as the single cluster's cap is on every other owner.
        const int words_w  = btn_h + ggap + btn_h;
        const int words_x0 = std::max(cx0, cx1 - ring - words_w);
        // What the row owes AFTER the scrub: the scrub → clock distance, the
        // clock cell, the clock → separator distance, the second separator
        // with its own button-side air, the lamp, THE UP BUTTON with the
        // glyph gap between them, and the GAP BEFORE THE RIGHT-FLUSHED PAIR,
        // which is that same glyph gap since 2026-09-01. THE 2026-08-29
        // SPACING RULING IS SPENT, not overruled: it put the WORD BUTTONS' OWN
        // gap there ("the air between the last glyph and Load in place is the
        // air between Load in place and Close", the modal word buttons'
        // spacing winning over the icon gap between clusters, where the layout
        // had spent the reserved ring plus a pad) — and with the words gone
        // there is no word-button gap left on this row to win: every gap on it
        // is the transport triple's own 2 px, so the run reads as one glyph
        // row split by its two separators.
        const int after_scrub =
            sep_pad + clock_w + sep_pad + sep_w + sep_gap +
            btn_h + ggap + btn_h + ggap;
        // THE FLOOR IS TWO HANDLE BOXES — a track that cannot seat the handle
        // at each end is not a track, and it is the same 40 authored px the
        // scrub floored at before the slider gave the number a derivation.
        int scrub_w = words_x0 - after_scrub - scrub_x0;
        if (scrub_w < 2 * scrub_handle_box_px()) scrub_w = 0;

        const int clock_x0  = scrub_x0 + (scrub_w > 0 ? scrub_w + sep_pad : 0);
        const int clock_end = clock_x0 + clock_w;
        const int sep2_x    = clock_end + sep_pad;
        plan[3].x = sep2_x + sep_w + sep_gap;       // Repeat one
        plan[4].x = plan[3].x + btn_h + ggap;       // Up
        plan[5].x = words_x0;                       // Load in place
        plan[6].x = words_x0 + btn_h + ggap;        // Close

        paint_separator(sep1_x);
        paint_separator(sep2_x);

        // -- THE PLAY-SCRUB, A BREEZE SLIDER (R29; the metrics, the colours
        //    and their provenance are at render.h's scrub block). The ITEM is
        //    published (dlg.scrub) and it is the button box's own band, so a
        //    press anywhere on it is on the slider and the per-position damage
        //    covers the handle's whole travel; the MAPPING owns the inset
        //    (render_player_scrub_x_of), the handle's centre being the frame's
        //    position. --
        const AppState::RenderPlayer& rp = app.render_player;
        const int64_t pos = render_player_position(app, playback);
        if (scrub_w > 0) {
            const GuiRect track{scrub_x0, btn_y, scrub_w, btn_h};
            dlg.scrub = track;
            const int    gh   = scaled_px(kScrubGrooveThicknessPx, 2);
            const int    gy   = track.y + (track.h - gh) / 2;
            const int    gs   = scaled_px(kScrubOutlineWidthPx, 1);
            const double grad = kScrubGrooveRadiusPx * gui_scale_factor();
            // THE WHOLE GROOVE FIRST, then the played part over its left
            // portion — Breeze's own two-pass construction, both passes the
            // same rounded rect at the same radius, so the played part's right
            // cap is round and hidden under the handle.
            redesign_face_box(cr, track.x, gy, track.w, gh, gs, grad,
                              &kScrubGroove, &kScrubGrooveOutline);
            if (rp.frames > 0) {
                // THE HANDLE'S COLUMN: the drag's carried x while the marker
                // is being dragged (the sound continues where it was and the
                // seek commits at the release), the item position's own
                // otherwise.
                const int hx = rp.scrub.armed
                                   ? rp.scrub.marker_x
                                   : render_player_scrub_x_of(app, pos);
                // THE PLAYED PART TAKES THE WINDOW'S FOCUS (R29): the 17:46
                // shot's blue while the window is activated, the 16:46 shot's
                // dimmed one while it is not — the scrub is the third surface
                // that reads AppState::window_activated, after rows 1 and 2,
                // and the activation hook damages this row for it.
                const bool     act = app.window_activated;
                const GuiColor played_fill =
                    act ? kScrubPlayed : kScrubPlayedInactive;
                const GuiColor played_line =
                    act ? kScrubPlayedOutline : kScrubPlayedInactiveOutline;
                const int played_w = hx - track.x;
                if (played_w > 2 * gs) {
                    redesign_face_box(cr, track.x, gy, played_w, gh, gs, grad,
                                      &played_fill, &played_line);
                }
                // THE HANDLE — an 18px circle inside its 20px box, filled and
                // stroked on ONE path at the half-stroke inset every face box
                // in the tree takes. ITS OUTLINE SAYS WHERE THE POINTER IS,
                // the accent's job everywhere else on this surface: hovered or
                // dragged it is kRedesignAccent, at rest Breeze's own
                // mix(Button, ButtonText, 0.2).
                //
                // THE HOVER IS READ AT PAINT from the remembered pointer
                // position rather than kept as a bit, and that is the cheaper
                // HONEST answer: the handle MOVES under a stationary pointer
                // while the transport plays, so a bit written by the motion
                // walk would be stale in both directions between motions,
                // while this cannot be — the tick damages this cell on every
                // position change and the frame recomputes it. The motion
                // walk's part is one damage of the cell, not an answer.
                const int    hdia = scaled_px(kScrubHandleDiameterPx, 2);
                const double hcy  = static_cast<double>(track.y) +
                                    static_cast<double>(track.h) * 0.5;
                const double hrad = static_cast<double>(hdia) * 0.5 -
                                    static_cast<double>(gs) * 0.5;
                // IDLE NEVER SHOWS THE ACCENT: the press over the handle is a
                // consumed no-op while the transport is idle
                // (claim_player_scrub_press), so a face that lit up under the
                // pointer there would promise a gesture the press refuses.
                // LIVE and PAUSED still hover and drag normally.
                const bool hovered =
                    rp.transport != AppState::RenderPlayer::Transport::Idle &&
                    (rp.scrub.armed ||
                     (app.pointer_in_window &&
                      render_player_scrub_handle_hit(track, hx,
                                                     app.last_mouse_x,
                                                     app.last_mouse_y)));
                cairo_save(cr);
                cairo_new_path(cr);
                cairo_arc(cr, static_cast<double>(hx), hcy, hrad,
                          0.0, 2.0 * 3.14159265358979323846);
                cairo_set_source_rgb(cr, kScrubHandleFill.r, kScrubHandleFill.g,
                                     kScrubHandleFill.b);
                cairo_fill_preserve(cr);
                const GuiColor hline =
                    hovered ? kRedesignAccent : kScrubHandleOutline;
                cairo_set_source_rgb(cr, hline.r, hline.g, hline.b);
                cairo_set_line_width(cr, static_cast<double>(gs));
                cairo_stroke(cr);
                cairo_restore(cr);
            }
        }

        // -- THE CLOCK, after the scrub (R25 put the time there; the cell
        //    itself is unchanged). Its cell is PUBLISHED (dlg.clock) for the
        //    tick's per-position damage, beside the scrub's, and it CLIPS at
        //    the lane's right pad on a narrow window. --
        const double cbase =
            redesign_baseline(cfont, static_cast<double>(content.y),
                              static_cast<double>(content.h)) +
            scaled_px(kClockCellOffsetYPx);
        const int sr = audio.sample_rate();
        auto seconds_of = [sr](int64_t frames) {
            return sr > 0 ? static_cast<double>(frames) / static_cast<double>(sr)
                          : 0.0;
        };
        const std::string clock_text =
            format_timestamp(seconds_of(pos)) + " / " +
            format_timestamp(seconds_of(rp.frames));
        const int clock_right = std::min(clock_end, cx1);
        if (clock_right > clock_x0) {
            cairo_save(cr);
            cairo_rectangle(cr, clock_x0, content.y, clock_right - clock_x0,
                            content.h);
            cairo_clip(cr);
            show_row_text(cr, cfont, static_cast<double>(clock_x0), cbase,
                          clock_text, kRedesignLabel);
            cairo_restore(cr);
            dlg.clock = GuiRect{clock_x0 - 1, content.y,
                                clock_right - clock_x0 + 2, content.h};
        }
        // THE SANS FACE BACK, AND THE HANDLE WITH IT: a cairo_scaled_font_t*
        // borrowed from the context is only good until the context's font
        // changes, and the clock's monospace switch is exactly that change —
        // so the row's `font` is RE-TAKEN here rather than assumed to have
        // survived. (This is the one branch that switches faces before the
        // buttons are painted; the clock's own cell was painted after them
        // until R25 moved the time.) IT STAYS THOUGH THIS OWNER'S ROW PAINTS
        // NO WORD BUTTON SINCE 2026-09-01: the walk below is SHARED, its word
        // arm reads `font`, and this line is what keeps that arm's
        // precondition a property of the body rather than of which branch
        // happened to run.
        font = select_bottom_row_face(cr);
    } else if (picker_up) {
        // -- THE PICKER'S ROW (2026-08-28, architect R22; ONE BUTTON since
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
    } else if (stats_up) {
        // -- THE AV SYNC STATS PANEL'S ROW (architect 2026-09-03): **Copy to
        //    clipboard · Close** and nothing else, and it takes the picker's
        //    seat for the picker's reason — THE ALIGNMENT RULE (stated in full
        //    in the branch above): a modal carrying a FIELD OR A MESSAGE is
        //    flush left with it, and a LIST's buttons are flush right. This
        //    owner carries neither a field nor a message — the band above is
        //    the whole content — so its cluster starts at `buttons_x_max`, the
        //    same rightmost seat every other owner uses as its CAP. --
        dlg.owner  = AppState::ModalDialogOwner::Stats;
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
        const int fbord = scaled_px(kModalFieldBorderPx, 1);
        // THE LABEL → FIELD GAP IS THE ROW'S OWN PAD (architect 2026-08-29):
        // the air between the window edge and the label, and the air between
        // the label and the field, are ONE number read twice — the separately
        // sampled 11 that stood here is retired. Every dialog editor's label
        // ("Setting:", "BPM:", the commit title's, the measure offset's)
        // reads it.
        const int fx    = cx0 + label_w + pad;
        // The room a field may take before the buttons would have to give:
        // the cluster's cap, less its reserved left ring and the pad.
        const int field_room = (buttons_x_max - ring - pad) - fx;
        const int field_w = std::max(std::min(scaled_px(kModalFieldWidthPx),
                                              field_room),
                                     scaled_px(40.0, 1));
        // The 40px floor is the ONE thing that can push past the cap (a window
        // too narrow for label + field + buttons), and the cap is what stops
        // it there — the buttons stay whole and the field is the surface that
        // has already given everything it can.
        buttons_x0 = std::min(fx + field_w + pad + ring, buttons_x_max);
        const int field_h = scaled_px(kModalFieldHeightPx);
        const int field_y = content.y + (content.h - field_h) / 2;
        const GuiRect field_outer{fx, field_y, field_w, field_h};
        const GuiRect field_inner{fx + fbord, field_y + fbord,
                                  field_w - 2 * fbord, field_h - 2 * fbord};
        // ONE BASELINE FOR LABEL AND FIELD INK, solved on the FIELD's band so
        // the buffer sits centred in its own box and the label reads level
        // with it — the LABEL then takes its own authored 1px drop
        // (kModalLabelOffsetYPx, above) off that shared seat; the field's ink
        // does not.
        const double baseline =
            redesign_baseline(font, static_cast<double>(field_y),
                              static_cast<double>(field_h));

        show_row_text(cr, font, static_cast<double>(cx0),
                      baseline + scaled_px(kModalLabelOffsetYPx),
                      prefix.c_str(), kRedesignLabel);

        // THE FIELD CHROME — THE BUTTONS' OWN BOX (architect 2026-08-13, at
        // his live test: "give the text editor the same rounded corner as the
        // buttons have, the same corner radius, and the same outline — the
        // breeze blue highlight — when it's hovered and when it has the
        // focus"). It draws through redesign_face_box, the very path the
        // buttons below take, so field and button describe their corners with
        // one expression and cannot drift; `rad` is the buttons' own
        // kIconCornerRadiusPx on the buttons' own scale, resolved once for
        // this whole body just under the button plan.
        //
        // THE LINE SAYS WHERE THE POINTER OR THE KEYBOARD IS, exactly as it
        // does on a button — and NOTHING ELSE MOVES WITH IT. A focused BUTTON
        // wears fill + accent outline + a 2px halo; THE FIELD'S FOCUS
        // INDICATOR IS THE OUTLINE ALONE, deliberately: its dark inset ground
        // is what makes typed text readable, a button's focus fill would
        // destroy that, and a halo would need reserved room the field (which
        // absorbs the row's shrink) has none of. So rest keeps the field's own
        // sampled kModalFieldBorder over kModalFieldGround, and hover and
        // focus each swap that 1px border for kRedesignAccent with the ground
        // untouched.
        //
        // FOCUS IS `modal_dialog_focus < 0` and needs no term of its own: on
        // an editor dialog -1 IS the field, the ring's own meaning for it
        // (AppState::modal_dialog_focus), so the field is lit exactly while
        // the ring has not stepped onto a button — which includes the open,
        // where the user is there to type.
        //
        // THE RED FLASH STILL RECOLORS THE FIELD — the editors' one invalid
        // state paints the interior in the marker-flag red pair (fill under
        // its 1px top edge, the flag anatomy's own order), so there is ONE
        // invalid red in the product and no second box. THE TOP EDGE IS
        // CLIPPED TO THE ROUNDED INTERIOR (2026-08-13, when the box grew
        // corners): a straight 1px band across a rounded box would poke out
        // past both upper corners. Clipping it keeps the anatomy the flag
        // editor and this field have always shared — a fill under a 1px edge
        // — rather than dropping the edge and making the invalid state read
        // differently on the two surfaces that share the red.
        const bool field_focused = app.modal_dialog_focus < 0;
        const GuiColor field_ground =
            ed->red ? kMarkerFlagFillRed : kModalFieldGround;
        const GuiColor field_line =
            (app.modal_dialog_field_hovered || field_focused)
                ? kRedesignAccent : kModalFieldBorder;
        redesign_face_box(cr, field_outer.x, field_outer.y,
                          field_outer.w, field_outer.h,
                          fbord, rad, &field_ground, &field_line);
        if (ed->red) {
            cairo_save(cr);
            redesign_rounded_rect_path(
                cr, static_cast<double>(field_inner.x),
                static_cast<double>(field_inner.y),
                static_cast<double>(field_inner.w),
                static_cast<double>(field_inner.h),
                rad - static_cast<double>(fbord));
            cairo_clip(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            cairo_set_source_rgb(cr, kMarkerFlagEdgeRed.r,
                                 kMarkerFlagEdgeRed.g, kMarkerFlagEdgeRed.b);
            cairo_rectangle(cr, field_inner.x, field_inner.y,
                            field_inner.w, marker_flag_edge_h_px());
            cairo_fill(cr);
            cairo_restore(cr);
        }

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
        // since each of the four dialog editors owns its own State, a change
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
            static_cast<int>(std::nearbyint(baseline - fe.ascent));
        const int band_h =
            static_cast<int>(std::nearbyint(fe.ascent + fe.descent));

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
        // THE SELECTION BAND IS THE ACCENT (architect 2026-08-28, the palette
        // block beside kMarkerFlagLabel): kRedesignAccent behind the selected
        // substring, kRedesignLabel for its glyphs — the product's one
        // selection pairing, which the flag editor's marker-lane box paints
        // too. Here it costs a pass rather than adding one: the run ALREADY
        // shows in kRedesignLabel, so the selected glyphs are the right white
        // with no second show, and the KNOCKOUT that used to re-show them in
        // the resolved field ground (a near-white band wanted dark letters) is
        // DELETED with the band it served. The red-flash ground therefore no
        // longer reaches the selected glyphs at all — a selection standing
        // through an invalid flash reads accent-on-red, which is the same
        // selection it read before the flash.
        if (has_sel) {
            const int hx0 = static_cast<int>(std::nearbyint(tx + bx_off[s0]));
            const int hx1 = static_cast<int>(std::nearbyint(tx + bx_off[s1]));
            cairo_save(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            cairo_set_source_rgb(cr, kRedesignAccent.r, kRedesignAccent.g,
                                 kRedesignAccent.b);
            cairo_rectangle(cr, hx0, band_y,
                            (hx1 > hx0) ? (hx1 - hx0) : 1, band_h);
            cairo_fill(cr);
            cairo_restore(cr);
        }
        cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                             kRedesignLabel.b);
        text_shape::show_shaped_run(cr, run, tx, baseline);
        // THE CARET IS THE FIELD'S FOCUS, SO IT PAINTS ONLY WHILE THE FIELD HAS
        // IT (architect 2026-08-13, at his live test: "the blinking caret, the
        // I-beam, continues to blink in the text field even though it has lost
        // focus"). `field_focused` is the ring's own -1, resolved above, so
        // this needs no term of its own and cannot disagree with the outline
        // that says the same thing one box out. THE SELECTION HIGHLIGHT IS
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
            cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                                 kRedesignLabel.b);
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
    const int lw = std::max(1, scaled_px(kIconOutlineStrokePx));
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
        // THE FACE LADDER (architect 2026-08-13: "the modal buttons should
        // basically look like the icons, but with a resting outline"). It IS
        // the icon row's, read off paint_icon_row's own body — the fill says
        // the STATE and the outline says where the pointer or the keyboard
        // IS, decided separately, so every combination falls out instead of
        // being enumerated — with two differences, both the ruling's:
        //   REST paints the 1px kRedesignLine outline ON A WORD BUTTON where
        //   the icon row paints nothing (the row ground still shows through —
        //   no fill, the icon buttons plus that one line). A GLYPH BUTTON
        //   TAKES THE ICON ROW'S REST INSTEAD — nothing at all — which is
        //   architect R25 (2026-08-28): "icon buttons have no border (the
        //   regular transport has none), text buttons keep theirs", the player
        //   row's transport reading as the transport it replaces. So the rest
        //   line is the `glyph` fork and not the unconditional it was.
        //   FOCUS is a face the icon row does not have (nothing up there takes
        //   the keyboard): its own fill under the accent outline, plus the
        //   halo painted below. A GLYPH BUTTON KEEPS IT WHOLE — the ring must
        //   be visible wherever it lands, and a borderless button with no
        //   focus face would swallow it.
        // THE SELECTED FACE ARRIVED WITH THE ROW'S ONE LAMP (2026-08-28, R26 —
        // Repeat one): kRedesignSelectedFill under the resting line, the
        // roster's own SELECTED face, ranked under the press and over the
        // focus fill exactly as the icon row ranks them — the fill says the
        // STATE, so a focused lamp still reads as lit and its outline is what
        // says the keyboard is there. THE DISABLED RUNG EXISTS SINCE
        // 2026-08-30 (architect: the transport keys are their own class), on
        // the PLAYER's buttons alone — every other owner's dialog buttons
        // are always live while their dialog stands and publish
        // enabled=true, so the `keep` term below is inert for them. The rung
        // is the icon row's own kRedesignDisabledMix toward
        // kRedesignContentGround, the row's ground (the derivation record is
        // at the constant, render.h).
        // THE CLICK FACE IS REAL NOW: these buttons act at the RELEASE, so the
        // pressed interior is the standing statement that the act is armed and
        // the lift will run it (it was unpainted while they acted at the
        // press, when there was nothing to hold a face for).
        //
        // THE THREE PIECES, DECIDED SEPARATELY (2026-08-13, when the focus grew
        // its two STRENGTHS — AppState::modal_dialog_focus_active — and the
        // FEINT grew a face of its own):
        //   THE FILL says the focus. Both strengths wear kModalFocusFill; a
        //   LIVE PRESS — armed with the pointer inside it, or armed from the
        //   keyboard — outranks it with the icon row's own 30% click mix.
        //   THE OUTLINE says who is claiming the button, at the accent when
        //   the POINTER, a LIVE ARM or the keyboard's own WALK claims it, at
        //   the new kModalFocusLinePassive when only an assigned focus does,
        //   and at the resting kRedesignLine otherwise. A HELD-AWAY FEINT is
        //   inside the arm term, which is exactly the ruling's "while the
        //   button is being held, but away from the button's hit area, the
        //   button looks like a hover".
        //   THE HALO is the ACTIVE strength alone.
        // Every rung of the ladder falls out of those three; none is
        // enumerated. Two consequences worth naming because they read like
        // omissions: ACTIVE FOCUS PLUS HOVER IS IDENTICAL TO ACTIVE FOCUS (the
        // outline is already accent, so hover adds nothing), and PRESSED
        // outranks every fill above it, focus included.
        const bool enabled = plan[i].enabled;
        // THE DISABLED GATES mirror the roster's (paint_icon_row): the
        // pointer and keyboard press faces are gated on the bit rather than
        // trusted — the claim never arms a disabled button and the ring's
        // walk skips one, but a button can go dead UNDER a hover, a hold or
        // a parked focus with no event to refresh them (a pressed Stop greys
        // Stop under its own focus). The FOCUS faces are deliberately NOT
        // gated: a stale ring must stay visible where the keyboard is, so
        // they DIM with the button instead — the roster's own
        // disabled+selected composition, mixed by `keep` below.
        const bool armed   = enabled &&
                             static_cast<int>(i) == app.modal_dialog_pressed;
        const bool pressed = enabled &&
            ((armed && app.modal_dialog_press_inside) ||
             static_cast<int>(i) == app.modal_dialog_key_pressed);
        const bool hovered = enabled &&
                             static_cast<int>(i) == app.modal_dialog_hovered;
        const bool focused = static_cast<int>(i) == app.modal_dialog_focus;
        const bool active_focus = focused && app.modal_dialog_focus_active;
        // THE DISABLED RUNG'S ONE KNOB: every ink below retains this
        // fraction of itself over kRedesignContentGround — the bottom row's
        // own ground — per the recorded derivation at the constant
        // (render.h). 1.0 on every enabled button, leaving the ladder
        // bit-identical there.
        const double keep = enabled ? 1.0 : kRedesignDisabledMix;
        if (active_focus) {
            // THE HALO, drawn first so the button's own outline lands over its
            // inner edge: a `ring`-wide stroke whose centreline runs half a
            // ring outside the box, i.e. a band filling exactly the reserved
            // pixels from the box edge outward. The radius grows by the same
            // ring so the corner stays concentric with the button's. It dims
            // with a disabled button like the rest of the focus face.
            const GuiColor halo =
                mix_color(kModalFocusRing, kRedesignContentGround, keep);
            redesign_face_box(cr, r.x - ring, r.y - ring,
                              r.w + 2 * ring, r.h + 2 * ring,
                              ring, rad + ring, nullptr, &halo);
        }
        const bool lit = plan[i].lit;
        const GuiColor fill = mix_color(
            pressed ? mix_color(kRedesignAccent, kRedesignContentGround,
                                kRedesignClickMix)
                    : lit ? kRedesignSelectedFill
                          : kModalFocusFill,
            kRedesignContentGround, keep);
        // THE OUTLINE'S ACCENT FORKS ON THE WINDOW'S FOCUS AT THE FOCUS TERM
        // ALONE (architect 2026-09-02, arriving through the render player's
        // own panel): a KEYBOARD focus ring on
        // an unfocused window wears Breeze's inactive blue, through the one
        // fork accent_for_focus above, while the POINTER's three claims —
        // hovered, armed, pressed — keep the live accent, the pointer being
        // able to act on an unfocused window. The order below is what makes
        // that split exact: a hovered ACTIVE focus is still a pointer claim
        // and takes the live blue, which is the same reading as the block
        // above's "active focus plus hover is identical to active focus".
        const GuiColor line = mix_color(
            (hovered || armed || pressed)
                ? kRedesignAccent
                : active_focus ? accent_for_focus(app)
                : focused ? kModalFocusLinePassive
                          : kRedesignLine,
            kRedesignContentGround, keep);
        const bool has_fill = pressed || focused || lit;
        // The rest line is the word button's; a glyph button shows one only
        // while something claims it or its lamp stands (the ladder above).
        // SINCE 2026-09-01 THE FORK IS OWNER-SHAPED: the player's row is SEVEN
        // glyph buttons and no word, so none of them wears a resting outline
        // (architect R25's "icon buttons have no border — the regular
        // transport has none", now true of the whole row), while a prompt's,
        // an editor's and the picker's buttons are all words and all keep
        // theirs. The expression is unchanged — it asks the button, not the
        // owner.
        const bool has_line = !plan[i].glyph || hovered || armed || pressed ||
                              focused || lit;
        redesign_face_box(cr, r.x, r.y, r.w, r.h, lw, rad,
                          has_fill ? &fill : nullptr,
                          has_line ? &line : nullptr);
        if (plan[i].glyph) {
            // THE GLYPH, the icon row's own draw: the roster's 22 px icon box
            // centred in the 32 px button, each path in its own color,
            // dimmed by the same `keep` toward what sits under it when the
            // button is disabled — the icon row's own dimming term, inert
            // (keep == 1, the table's colors bit-identical) on every enabled
            // button.
            const int glyph_px = scaled_px(kIconGlyphPx);
            icons::draw(cr, plan[i].icon,
                        static_cast<double>(r.x + (r.w - glyph_px) / 2),
                        static_cast<double>(r.y + (r.h - glyph_px) / 2),
                        static_cast<double>(glyph_px),
                        keep,
                        has_fill ? fill : kRedesignContentGround);
        } else {
            show_row_text(cr, font, static_cast<double>(r.x + btn_pad_l),
                          redesign_baseline(font, static_cast<double>(r.y),
                                            static_cast<double>(r.h)),
                          plan[i].label,
                          mix_color(kRedesignLabel, kRedesignContentGround,
                                    keep));
        }
        AppState::ModalDialogButton out;
        out.rect         = r;
        out.response_key = plan[i].response_key;
        out.editor_ok    = plan[i].editor_ok;
        out.player_act   = plan[i].player_act;
        out.stats_act    = plan[i].stats_act;
        out.enabled      = plan[i].enabled;
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
        // THE MODIFIER LINE (R37), published beside it and empty on every
        // button with no shifted twin — the roster hint's `line2` over this
        // surface, and its one producer is the player's plan above.
        out.tooltip2 = plan[i].tooltip2;
        dlg.buttons.push_back(out);
    }

    // THE MODAL'S SURFACE IS THE LANE — border-top included, because that is
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
// THE PALETTE IS THREE EXISTING CONSTANTS AND NOTHING ELSE (architect ruling
// 2026-08-27, MEASURED off his own Maliit screenshot — means over the key
// faces, the ground between and around them, and the caps — then matched to
// the constants already in render.h's palette block rather than sampled into
// new ones):
//   the KEY FACE          -> kRedesignRowGround     #292C30 (measured 45,48,52)
//   the GROUND around and
//     between the keys    -> kRedesignContentGround #202326 (measured 30,34,37)
//   the CAPS              -> kRedesignLabel         #FCFCFC
// (The measured pairs carry the source JPEG's own drift; the constants are the
// values, per the palette block's hard-coded-whole rule.) The ground is also
// the BOTTOM ROW'S, so the keyboard and the row it sits on read as one block —
// which is why the band paints NO LINE at its own top edge (architect
// 2026-08-27, on glass): the only line in this neighbourhood is the bottom
// row's own border-top, painted by that row.
//
// THE OTHER TWO FACES ARE THE ICON ROW'S, BORROWED WHOLE — nothing here is
// keyboard-specific (the architect's ruling: reuse what the chrome button
// painter already does for a pressed button and for a lit lamp):
//   REST      — the face colour alone, no frame, which is the icon row's own
//               resting shape said with a fill (a key cap is always there,
//               where a toolbar icon is not).
//   ARMED     — kRedesignSelectedFill under a 1px kRedesignLine frame, the
//               roster's SELECTED face verbatim. Worn by SHIFT while it is
//               armed and by the LAYER TOGGLE while the symbol layer stands:
//               the two lamps, one face, the icon row's own lamp. IT IS ALL
//               SHIFT'S ARM HAS TO SAY ITSELF WITH now that the cap is the
//               word "Shift" in both states — that, and the letter caps, every
//               one of which turns capital while the arm stands.
//   PRESSED   — the interior at kRedesignClickMix toward kRedesignAccent under
//               an accent frame, the roster's CLICK face verbatim — the same
//               expression over the same ground — AND IT WINS OVER ARMED while
//               the finger is down, which is the roster's own rule for the
//               same collision (a press is transient and should feel the same
//               wherever it lands).
// Every key is drawn with redesign_face_box, the one path every button-like
// surface in the product is filled and framed on.
// NO HOVER FACE AND NO TOOLTIP: this surface exists on a platform with no
// pointer, so a hover has no producer and a hint has nowhere to hang.
//
// EVERY CAP IS TEXT (architect 2026-08-27, on glass — the Breeze glyphs the
// function keys wore for a day read oversized beside the letter caps): the
// function keys, Space and the layer toggle say their words, the letter keys
// their characters, all on the one sans face through the one shaping
// chokepoint. This surface draws no icon.
//
// STATE-AXIS: every one of the three faces is decided ONCE per key, from the
// two lamp bits and the one held index this body reads at its head, and the cap
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
    const bool  symbol_layer = kb.symbol_layer;
    const bool  shift_armed  = kb.shift_armed;
    const int   held         = kb.pressed_key;

    cairo_save(cr);

    // The lane's ground, one fill across the whole band and NO LINE AT ITS TOP
    // EDGE: the band's ground is the bottom row's ground, so the two lanes read
    // as one block and a seam between them would draw a border through the
    // middle of it. The 1px line below the band belongs to the BOTTOM ROW (its
    // own border-top, painted by that row's body).
    cairo_set_source_rgb(cr, kRedesignContentGround.r,
                         kRedesignContentGround.g,
                         kRedesignContentGround.b);
    cairo_rectangle(cr, surf.x, surf.y, surf.w, surf.h);
    cairo_fill(cr);

    // THE ONE SANS FACE at the product's ONE text size, selected through the
    // one face owner and shaped through the one chokepoint (text_shape.h) like
    // every other label in the product. EVERY key on this surface takes it —
    // the letter caps, the layer toggle's word and the function keys' words
    // alike — so there is no second way a cap can be drawn. A cap is one to
    // nine glyphs, which are the cheapest runs there are.
    gui_select_font_face(cr, GuiFontFamily::Sans);
    cairo_set_font_size(cr, redesign_font_size_px());
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);

    const int    lw     = std::max(1, scaled_px(kIconOutlineStrokePx));
    const double radius = std::nearbyint(onscreen_keyboard::kCornerPx *
                                         gui_scale_factor());

    onscreen_keyboard::for_each_key(
        app, symbol_layer,
        [&](uint32_t index, const onscreen_keyboard::KeyDef& k,
            const GuiRect& r) {
            using Role = onscreen_keyboard::Role;
            // PER-KEY EXPOSURE. The band's ground is laid across the whole
            // surface above (one fill, bounded by the outer damage clip), but a
            // KEY costs a face box and a shaped run, so a narrow damage
            // crossing this band pays for the keys it actually crosses and for
            // no other. That matters at the panel's tick rate:
            // the scanner's own damage is a column a few pixels wide, and
            // without this test every one of them walked forty-odd keys.
            if (!rects_intersect(exposed, r)) return;

            const bool pressed = (static_cast<int>(index) == held);
            const bool armed   =
                (k.role == Role::Shift       && shift_armed) ||
                (k.role == Role::LayerToggle && symbol_layer);

            // The three faces (the block above): the fill is always painted,
            // the frame only where a face has one — a resting key has none,
            // exactly as a resting icon-row button has none.
            const GuiColor fill =
                pressed ? mix_color(kRedesignAccent, kRedesignContentGround,
                                    kRedesignClickMix)
                        : (armed ? kRedesignSelectedFill : kRedesignRowGround);
            const GuiColor line = pressed ? kRedesignAccent : kRedesignLine;
            redesign_face_box(cr, r.x, r.y, r.w, r.h, lw, radius, &fill,
                              (pressed || armed) ? &line : nullptr);

            // THE CAP: the word the table's one cap owner answers (cap_word —
            // the function keys, Space and the layer toggle), or, where it
            // answers none, the character key's own letter through the ONE case
            // derivation (shifted_char) — never a second uppercase table.
            char one[2] = {'\0', '\0'};
            const char* cap = onscreen_keyboard::cap_word(k, symbol_layer);
            if (cap == nullptr) {
                one[0] = onscreen_keyboard::shifted_char(k.ch, shift_armed);
                cap    = one;
            }
            const text_shape::ShapedRun run =
                text_shape::shape_text_run(font, cap);
            const double cap_x = std::nearbyint(
                r.x + (r.w - run.width_px) * 0.5);
            const double baseline = redesign_baseline(
                font, static_cast<double>(r.y), static_cast<double>(r.h));
            cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                                 kRedesignLabel.b);
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
    // CEILING BAND every time it stands — the tab row's first pixel through
    // the bottom row's top since 2026-09-03, whatever its listing's length
    // (R35's fixed height, the architect's new ceiling) — and the keyboard's
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
// the render player's folders and wavs, the Open project picker's project
// folders and, since 2026-09-03, the AV sync stats panel's text lines.
// Contract and gate are at the declaration; the geometry, the scroll
// clamp and the one row walk are at folder_overlay.h, which this body reads
// and never restates; the row table and every state bit are
// AppState::folder_overlay. TWO ROW CLASSES, ONE PREDICATE FOR BOTH THE FACE
// AND THE PITCH: the player's and the picker's rows are BUTTON ROWS in the
// redesign's SANS (the project picker's are all Folder rows, each taking its
// glyph from the row's KIND and its face from the same ladder the player's
// rows do, and the transport mark below is structurally absent under the
// picker — the player holds no item while a picker stands), while the stats
// panel's Text rows are INERT MONOSPACE LINES — no face, no glyph, no press
// arm, standing at the monospace line's own pitch with no gap. THE OWNER TAG
// DECIDES BOTH HALVES THROUGH ONE PREDICATE, `folder_overlay::text_listing`:
// it selects the font below AND forks the row height and gap in
// folder_overlay.h, since a row shaped in one face and spaced for another
// would be two surfaces disagreeing about one row (fc40318e). The fork
// collapsed for the few hours of 2026-09-03 the two listings wore the
// monospace too, and came back with the architect's own reversal that
// evening — the quote is at the selection. A row's KIND is what the walk
// reads per row to skip the glyph and the face ladder, and the walk asserts
// the two agree — a listing is homogeneous in kind.
//
// THE BUTTON ROWS ARE THE ICON ROW'S BUTTON AND THE PALETTE IS THE FILE
// MANAGER'S (architect 2026-08-28, R31/R32, superseding the keyboard's
// palette this band opened with): each such row is the ICON ROW'S BUTTON —
// its box, its gap, its corner radius, its glyph inset (folder_overlay.h
// reads all four from render.h) — and its faces come off kdenlive's project
// bin and pcmanfm-qt's compact view, which agree.
//   the GROUND, around and
//     between the rows    -> kModalFieldGround, ONE fill across the band and
//                            NO ALTERNATING ROWS
//   a RESTING row         -> NO FILL AT ALL: the ground shows through, which
//                            is what makes a lit row the only thing the eye
//                            lands on
//   the INK               -> kRedesignLabel in EVERY face (the icon glyphs
//                            carry their own Breeze inks)
//   HOVER                 -> kFolderRowHover under a 1px kFolderRowHoverOutline
//                            frame, the button's own outline width and inset
//   the HIGHLIGHT band    -> kRedesignAccent, kdenlive's own selection band
//   HOVERED + HIGHLIGHTED -> kFolderRowHoverSelected, the band lifted under
//                            the pointer as the hover face lifts the ground
//   PRESSED               -> the highlight's own fill: a row press is an arm
//                            whose act is the lift, and the accent is what it
//                            promises
//   the RING'S FOCUS      -> the modal's focus line as the highlighted row's
//                            OUTLINE: kModalFocusLinePassive while the ring
//                            stands elsewhere, the accent while it is on the
//                            list
// Every BUTTON row is drawn with redesign_face_box, the one path every
// button-like surface in the product is filled and framed on, at the
// button's corner radius; a Text row skips it whole. The band wears the tab
// row's own top border (architect 2026-09-03 evening: "let's add the same
// border that the tab row has up at the top — add that to the overlays").
// The band starts at the tab row's first pixel, so it draws the line that row
// would have drawn there — kRedesignTabLine at tab_row_border_h_px, the same
// colour and the same scaled thickness, aliased like every chrome line — and
// the overlay reads as the lane it replaces. It is painted inside the band as
// its first row and comes out of the content's room
// (folder_overlay::border_h_px and content_rect own that geometry), so the
// surface rect and the damage rect are unchanged by it and the rows — the row
// walk's clip here, row_at's containment there — live under the line at every
// scroll offset. The band's press claim is still the surface's, so a press on
// the line is consumed and simply arms no row.
// A line stood here from 2026-08-29 for a different reason, framing the panel
// off the waveform ground above, and went out with that ground for the hours
// of 2026-09-03 the band covered the menu row.
//
// TWO MARKS, ONE ROW EACH AND POSSIBLY THE SAME ROW: the HIGHLIGHT band (the
// list's keyboard focus — what Enter and Load in place act on, and nothing
// else: Space and the Play button answer the TRANSPORT and never read the
// highlight)
// and the TRANSPORT'S ITEM, whose wav glyph is swapped for the roster's
// MediaPlaybackStart (the one-button-two-faces precedent) whether the item is
// live or paused.
//
// STATE-AXIS: the face is decided ONCE per row from the highlight, the hover,
// the press arm and the ring, and the glyph from the row's kind and the
// item's path; there is no second list of what a row looks like anywhere.
void GuiPaintHandler::paint_folder_overlay(cairo_t* cr, const GuiRect& exposed) {
    if (!folder_overlay::stands(app)) return;
    const GuiRect surf = folder_overlay::surface_rect(app);
    if (surf.w <= 0 || surf.h <= 0) return;
    if (!rects_intersect(exposed, surf)) return;

    const AppState::FolderOverlay& ov = app.folder_overlay;
    const AppState::RenderPlayer&  rp = app.render_player;

    cairo_save(cr);
    // The band's ground, one fill; then EVERYTHING ELSE UNDER THE CONTENT
    // RECT'S CLIP, because a scrolled listing's first and last rows straddle
    // the content's edges and must not paint into the waveform above, the
    // bottom row below, or the band's own top border.
    cairo_set_source_rgb(cr, kModalFieldGround.r, kModalFieldGround.g,
                         kModalFieldGround.b);
    cairo_rectangle(cr, surf.x, surf.y, surf.w, surf.h);
    cairo_fill(cr);
    // The top border, the band's first row (the block above): the tab row's
    // own line, laid over the ground before the clip — row_rect reads
    // content_rect, which is this band less this row, and the clip below is
    // that same rect, so a row scrolled part-way off the top is cut at the
    // line instead of covering it.
    {
        const int border = folder_overlay::border_h_px();
        if (border > 0 && border < surf.h) {
            cairo_save(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            cairo_set_source_rgb(cr, kRedesignTabLine.r, kRedesignTabLine.g,
                                 kRedesignTabLine.b);
            cairo_rectangle(cr, surf.x, surf.y, surf.w, border);
            cairo_fill(cr);
            cairo_restore(cr);
        }
    }
    // THE ROW WALK'S CLIP IS THE CONTENT RECT AND row_at'S CONTAINMENT IS THE
    // SAME RECT (folder_overlay.h), so paint and hit agree about the border
    // line as they agree about every other pixel: the line can be neither
    // erased by a row's fill nor pressed through, at any scroll offset. The
    // surface stays the ground, the damage and the band's outer claim.
    const GuiRect content = folder_overlay::content_rect(app);
    cairo_rectangle(cr, content.x, content.y, content.w, content.h);
    cairo_clip(cr);

    // THE FACE IS THE CONTENT'S. Two of the three contents list NAMES — files
    // and projects — and take the redesign's sans at the redesign's size like
    // every other row in the product; the AV SYNC STATS PANEL takes the
    // product's MONOSPACE at the clock's own size, because its rows are a
    // label column beside a numbers column rebuilt every frame and a
    // proportional face would make the digits walk (the widening is the
    // architect's, 2026-09-03; the rule and the three-cell set are stated at
    // this file's bottom-row text block and at gui_font.h).
    //
    // THE SANS-FOR-LISTINGS RULING IS HIS, AND IT IS A REVERSAL OF HIS OWN
    // (2026-09-03, the same evening): the player's and the picker's listings
    // took the monospace for a few hours as the cheap first answer to their
    // width asymmetry ("maybe we could just use monospace font, and that way
    // it'll tidy up the width asymmetry ... let's try that first ... I
    // actually think that maybe all that's needed"), and at his first look he
    // put them back — "revert the monospace back to sans-serif proportional
    // on the picker and player and just keep it on the AV sync." So the fork
    // is here again and the set is THREE CELLS with the panel as the third,
    // not the band whole.
    //
    // THE PREDICATE IS THE ROW PITCH'S OWN (folder_overlay::text_listing): a
    // text row stands at this face's line height, so the face and the pitch
    // must fork on ONE question or a row could be shaped in one face and
    // spaced for another. SHAPE WITH THE FONT YOU PAINT WITH: the handle
    // below is taken AFTER the selection, and this painter selects once and
    // never again — nothing after this call reads a face it did not select,
    // so the borrowed handle stays valid for the whole walk.
    const bool stats = folder_overlay::text_listing(app);
    if (stats) {
        gui_select_font_face(cr, GuiFontFamily::Mono);
        cairo_set_font_size(cr, clock_font_size_px());
    } else {
        gui_select_font_face(cr, GuiFontFamily::Sans);
        cairo_set_font_size(cr, redesign_font_size_px());
    }
    cairo_scaled_font_t* font = cairo_get_scaled_font(cr);

    const int    lw     = std::max(1, scaled_px(kIconOutlineStrokePx));
    const double radius = std::nearbyint(kIconCornerRadiusPx *
                                         gui_scale_factor());
    const int    glyph  = folder_overlay::row_icon_px();
    const int    gap    = folder_overlay::row_icon_gap_px();
    const int    inset  = folder_overlay::row_icon_inset_px();
    // THE HIGHLIGHT'S BLUE DIMS WITH THE WINDOW (architect 2026-09-02), through
    // the one fork accent_for_focus above: the band and the list-focused
    // outline are the SELECTION, and a selection in an unfocused window wears
    // Breeze's inactive blue. The two HOVER faces below do not — a hover is a
    // promise the pointer can act, and it still can — and the hovered+
    // highlighted face is the hover's own lift of this band, so it goes with
    // the hover rather than with the swap.
    const GuiColor accent = accent_for_focus(app);

    folder_overlay::for_each_row(
        app, [&](int index, const AppState::FolderOverlayRow& row,
                 const GuiRect& r) {
            // PER-ROW EXPOSURE, the keyboard's own economy: a row costs a face
            // box, a glyph and a shaped run, so a narrow damage pays only for
            // the rows it crosses.
            if (!rects_intersect(exposed, r)) return;
            if (!rects_intersect(content, r)) return;

            // A TEXT ROW IS INERT AND THE FACE LADDER SKIPS IT WHOLE
            // (2026-09-03): no fill, no outline, no press arm — the row is a
            // line of text on the band's own ground, and the two axes below
            // have nothing to say about it. Its rect `r` is already a LINE's
            // — the monospace ascent plus descent, no gap to the next row —
            // because the walk forks the pitch on the listing's owner
            // (folder_overlay::text_listing), and a listing is homogeneous in
            // kind: this assert is that fact, checked per row.
            const bool text_row =
                row.kind == AppState::FolderOverlayRow::Kind::Text;
            assert(text_row == stats);
            const bool highlighted = !text_row && index == ov.highlight_row;
            // NO ROW HOVERS UNDER A PROMPT. A prompt outranks every content
            // and its veil takes the pointer, so on_motion's prompt branch
            // returns before the band's hover walk and the stored row keeps
            // whatever it held at the raise. The term lives HERE, at the
            // paint, for the same reason the roster's does: the hover face is
            // a promise the pointer can act, and under a prompt it cannot.
            // The raise damages the whole window, so the row goes out with it.
            const bool hovered     = !text_row && index == ov.hovered_row &&
                                     !app.prompt.active;
            const bool pressed     = !text_row && ov.press.armed &&
                                     ov.press.row == index &&
                                     ov.press.inside && !ov.press.scrolling;
            // THE FACE IS TWO AXES, NOT A LADDER (R32's own table): LIT — the
            // highlight, or a live press arm promising it — and HOVERED. A
            // row that is neither takes NO FILL and the band's ground shows
            // through it.
            const bool lit = pressed || highlighted;
            const GuiColor fill =
                lit     ? (hovered ? kFolderRowHoverSelected : accent)
                        : kFolderRowHover;
            // THE OUTLINE: the ring's focus strength on the highlighted row
            // (the modal's own two lines), the hover frame on a hovered row
            // that is not lit — the reference's own frame, which the lit
            // faces do not wear.
            const GuiColor* line = nullptr;
            if (highlighted) {
                line = ov.list_focused ? &accent
                                       : &kModalFocusLinePassive;
            } else if (hovered) {
                line = &kFolderRowHoverOutline;
            }
            if (!text_row) {
                redesign_face_box(cr, r.x, r.y, r.w, r.h, lw, radius,
                                  (lit || hovered) ? &fill : nullptr, line);
            }

            // THE GLYPH: the folder for folder rows, the wav for wav rows —
            // swapped for the transport glyph on the item's row — and NONE for
            // a TEXT row, which names neither a folder nor a file. (The UP
            // kind, the `..` row, wore the folder glyph because it named a
            // folder, and went with the player's move inside `tmp/` on
            // 2026-09-01.) At the BUTTON'S OWN INSET from the row's left edge
            // and centred in its height, which are the same number: a row is a
            // wide button and this is how a button seats its glyph. A TEXT
            // ROW'S WORD STARTS WHERE THE GLYPH WOULD HAVE — the button's own
            // inset is the row's left pad (read off the BUTTON's box, not the
            // row's line height, so the words did not move when the pitch
            // became the line's), so a glyph-less row reads as the same
            // button family with its ink where a word button seats it.
            const int gx = r.x + inset;
            int text_x = gx;
            if (!text_row) {
                icons::Icon icon = icons::Icon::Folder;
                if (row.kind == AppState::FolderOverlayRow::Kind::Wav) {
                    icon = (!rp.item.empty() && row.path == rp.item)
                               ? icons::Icon::MediaPlaybackStart
                               : icons::Icon::AudioXWav;
                }
                const int gy = r.y + (r.h - glyph) / 2;
                icons::draw(cr, icon, static_cast<double>(gx),
                            static_cast<double>(gy),
                            static_cast<double>(glyph));
                text_x = gx + glyph + gap;
            }

            // THE NAME, shaped through the one chokepoint, after the glyph —
            // in the band's ONE ink (black on the
            // accent would be the marker lane's rule, not this band's: R32
            // gives the selected row white text, kdenlive's own band carries
            // it, and a row that changes ink with its face would be a second
            // thing to read).
            const text_shape::ShapedRun run =
                text_shape::shape_text_run(font, row.name);
            // The baseline is solved from the row's rect and the face's
            // extents (redesign_baseline) for both row kinds. In a button box
            // that centres the line in the box; in a TEXT row the box IS the
            // line — ceil(ascent + descent) — so the formula collapses to
            // y + ascent (rounded) at every gui_scale, and the descenders end
            // at the row's foot, which is where the next line starts.
            const double baseline = redesign_baseline(
                font, static_cast<double>(r.y), static_cast<double>(r.h));
            // A NAME TOO LONG FOR THE LINE RUNS OFF THE EDGE (R31: no wrap,
            // no ellipsis — "project and file names will be short"), and the
            // clip to the ROW's own right edge is what "off the edge" means
            // here: the glyphs stop at the row, not at the window.
            cairo_save(cr);
            cairo_rectangle(cr, text_x, r.y,
                            std::max(0, (r.x + r.w) - text_x), r.h);
            cairo_clip(cr);
            cairo_set_source_rgb(cr, kRedesignLabel.r, kRedesignLabel.g,
                                 kRedesignLabel.b);
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
    // stored its press_x against the OLD promoted basis, and the 8px crossing
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
    // trim pass, the flag editor's box — and every hit test keep the older one.
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
    // endcap and routes on geometry that is not under it — the epoch split this
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
    // the waveform area then takes its own kWaveformCanvas ground. Unconditional and
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
        // is still the WHOLE area — its own two black border rows are its
        // geometry, and a shortened rect would draw the bottom one as a line
        // across the keyboard's top edge — so the band is subtracted with a
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

    // THE THREE REDESIGNED TOP BUTTON ROWS, THE UNIFIED BOTTOM ROW, THE STATUS
    // BAR AND THE
    // OVERVIEW STRIP PAINT
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
    // must not pay for them. Nothing painted after this point touches the three
    // top button lanes or the overview strip between the icons and the trim bar
    // (the flag cache is transparent over them, every other pass owns a
    // lane below them), so painting them first overdraws nothing.
    //
    // THE BOTTOM ROW JOINS THEM (row 7): it is audio-independent in the same
    // sense — the timestamp reads 00:00.000 with no source, and the loading line
    // is one of the things the row carries, which is why the separate loading-
    // only draw that used to sit below is gone. Nothing painted later overlaps
    // it except the floating surfaces, which paint over everything by design.
    {
        const GuiRect exposed{x, y, w, h};
        // THE MENU ROW'S EXPOSURE IS ITS LANE OR GAP 1 ABOVE IT: the painter
        // fills both in the row's ground (the vertical rule, main.cpp), so an
        // exposure inside the gap alone must reach it too.
        if (rects_intersect(exposed, top_menu_row_area(app)) ||
            rects_intersect(exposed, top_flex_gap_area(app))) {
            paint_menu_row(cr);
        }
        // THE TAB ROW PAINTS TABS AND NOTHING ELSE since 2026-08-29: it
        // carried the STATUS CHAIN under them from 2026-08-13 until the state
        // strings left for the one-day status bar, and they are row 8's own
        // cell now.
        if (rects_intersect(exposed, top_tab_row_area(app))) {
            paint_tab_row(cr);
        }
        if (rects_intersect(exposed, top_icon_row_area(app))) {
            paint_icon_row(cr);
        }
        // THE UNIFIED BOTTOM ROW (2026-08-12, rows 8 and 9 merged) is one
        // exposure and one painter: paint_bottom_strip grounds the lane and
        // paints the button cluster + clock through its
        // paint_bottom_row_buttons_and_clock half (audio-independent chrome
        // exactly like the four top rows — the press claim sits above the
        // pointer path's loading guard, and the painter publishes the hit
        // rects the claim reads, so it must paint on every frame class its
        // lane is exposed on). Its per-CELL exposure gate went with the status
        // chain on 2026-08-13: the row shapes no text but the clock's one
        // memoised cell now, so a narrow clock or button damage has nothing
        // left to be spared.
        if (rects_intersect(exposed, bottom_row_area(app))) {
            paint_bottom_strip(cr);
        }
        // THE OVERVIEW STRIP (top lane 3 since the relayout's commit B — it was
        // bottom lane 0 for the afternoon it landed), on its own
        // exposure like its four siblings: the lane's GROUND paints on every
        // frame class — a lane inside the centered block must not read as a
        // hole while loading — and the audio-dependent
        // content (bars / box / tick) gates inside the painter. Cheap off
        // the damage: no text shaping anywhere in the pass, and the bars are
        // a cached blit. Nothing painted later covers it: the passes below own
        // the trim, ruler and marker lanes under it and the waveform, and the
        // flag cache's blit spans the whole top strip but is TRANSPARENT
        // everywhere but the marker lane's boxes.
        if (rects_intersect(exposed, top_overview_row_area(app))) {
            paint_overview_strip(cr);
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
        //   2. render_canvas — the waveform area's ground AND its 2px black
        //      top/bottom borders (above, unconditional).
        //   3. the three redesigned top button rows (the TAB row painting tabs
        //      across its whole width since 2026-08-29), the unified bottom
        //      row (its chrome, buttons, clock AND state cell in one painter)
        //      and
        //      the OVERVIEW STRIP (ground + its one border row, cached bars,
        //      viewport box, playhead tick — paint_overview_strip), each
        //      on its own
        //      exposure (above, outside this branch; they own lanes nothing
        //      below them paints on).
        //   4. region ground -> waveform plate -> region ink -> phase-reset
        //      overlay ring.
        //   5. LIVE TRIM, one pass, entirely inside the trim lane: the lane
        //      ground, the window's bar, the two endcaps and the midpoint mark.
        //   6. the CURSOR's WAVEFORM stem segment (paint_playheads — the head
        //      is the ruler pass's, step 9).
        //   7. the MARKER STEMS (waveform).
        //   8. the SCANNER (waveform).
        //   9. the RULER lane — ticks and labels — AND, in the same pass, the
        //      cursor's HEAD on the marker lane: the head's pre-blended tick
        //      crossing needs the tick columns, so one owner walks both (the
        //      reasoning is at that block in paint_ruler_row).
        //  10. the FLAG BLIT.
        //  11. the strip-drag anchor stem (waveform, mid-gesture only).
        //  12. the KEYBOARD SLOT (paint_keyboard_slot, outside this branch —
        //      the on-screen keyboard since 2026-08-27 or the folder overlay
        //      since 2026-08-28, one tenant at a time), whose opaque ground
        //      covers the waveform area's lower part (the keyboard) or
        //      EVERYTHING BETWEEN THE MENU ROW AND THE BOTTOM ROW (the
        //      overlay, since 2026-09-03 — the tab row's first pixel down, so
        //      of steps 3 through 11 only the menu row's lane stays in view,
        //      greyed) and so follows every pass above. Which is why the WAVEFORM passes do
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
        //   THE RECOLOR MODEL (architect 2026-07-26, extended to the ink
        //     2026-08-18) — a highlight REPLACES colors, it never washes over
        //     them, and the region's is the ONE highlight that recolors: its
        //     GROUND half paints BEFORE the plate and the ink composites over
        //     it, then its INK half masks a second opaque color through the
        //     blitted plate's binary alpha over the same span, so the whole
        //     span lifts without a single compositing alpha. The phase-reset
        //     overlay contributes no ground at all (architect 2026-07-27): its
        //     1px RING is its whole visual, and a boundary line paints AFTER
        //     the plate, crossing the ink like the stems do.
        //   THE Z-ORDER FLIP (architect 2026-07-23) — the cursor playhead's
        //     STEM passes UNDER
        //     marker flags, so a cursor resting on a marker sits hidden behind
        //     that marker's flag standing in the same column; a SELECTION adds
        //     no playhead-like mark of its own, its whole cue being its
        //     members' BRIGHTENED FLAGS (the class ladder's brighter pair) with
        //     the landed cursor on the focus. (Row 5 moved the marker stems
        //     ABOVE the cursor's stem — the hidden-by-marker z-intent —
        //     and 2026-08-01 lifted the SCANNER alone above the stems, so the
        //     moving line does not blink out at every marker it crosses.)

        if (rects_intersect(exposed, wave_paint)) {
            // THE REGION HIGHLIGHT'S TWO HALVES, straddling the blit.
            // GROUND, under the plate: render_canvas already laid
            // the kWaveformCanvas ground for the whole area above; this repaints the
            // region's span of it opaquely, so the plate's transparent gaps show
            // the recolored ground rather than the plain one.
            paint_region_ground(cr, area);
            paint_waveform_plate(cr, area);
            // INK, over the plate and over the identical span: the blitted ink
            // is remasked in the lifted colour, so the highlight lifts the whole
            // picture rather than only the ground behind it.
            paint_region_ink(cr, area);
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

        // The CURSOR BEFORE the flag blit (Z-ORDER FLIP, architect 2026-07-23):
        // its line paints UNDER the
        // marker flags that follow. (The scanner used to ride along in this pass
        // and now paints after the stems, below — waveform-only either way, so
        // its stacking against the lanes never entered the question.)
        // flag_cache.surface is ARGB32, CLEAR-cleared
        // each rebuild and transparent outside the painted shapes, so the flag
        // blit composites source-over and never erases the playheads it does not
        // cover. Gated on area OR top_strip: the cursor line lives in the waveform
        // area, its head in the top strip.
        if (rects_intersect(exposed, wave_paint) ||
            rects_intersect(exposed, top_strip)) {
            paint_playheads(cr, area);
        }

        // MARKER STEMS AFTER THE CURSOR (row 5's z-intent): the cursor's
        // waveform stem passes UNDER them, and the flag boxes go over
        // everything in the strip blit below. That is the hidden-by-marker
        // model translated — a marker sharing the cursor's column hides it,
        // exactly as flags painted over the old triangle — and it is why the
        // stems paint here rather than in the pre-playhead slot the singleton
        // selected stem occupied. The stems are the flags' waveform half and
        // must not be split across the cursor by paint order. (The full
        // sequence is the paint-order block above.)
        if (rects_intersect(exposed, wave_paint)) {
            paint_marker_stems(cr, area);
        }

        // THE SCANNER LAST OF THE WAVEFORM VERTICALS (architect 2026-08-01):
        // the moving line paints AFTER the stems, so it crosses them instead of
        // being erased column by column as it sweeps past every marker. Only the
        // scanner moved — the cursor stayed in paint_playheads above, under the
        // stems and under the flags. Waveform-only, so no top_strip arm.
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
        // only. It now paints AFTER the playheads (the flip moved them up), so
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
    // EVERYTHING BETWEEN THE MENU ROW AND THE BOTTOM ROW under the overlay
    // since 2026-09-03, the tab row's first pixel down — so it must follow
    // every pass that paints there
    // (the top button rows, the overview strip, the plate, the region ink, the
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
    // the waveform, because both hang below the top strip and overlap whatever
    // is under them. They are NOT exposure-gated the way the rows are: each
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

    // A RELAYOUT VOIDS THE CENTERED DERIVATION'S CURSOR TERM (architect
    // 2026-09-02, the four-tier review's R-17f). The memory records what the
    // last derivation was made against — the cursor, the tab, the audio view
    // and the subject — and NOT the geometry it centered in, so a resize under
    // a lit lamp leaves all four terms matching while the waveform's width and
    // the samples-per-pixel under it have both moved: the resting hook then
    // finds nothing due and the playhead sits off-centre until the next
    // playhead or view change. Voiding the CURSOR term alone — -1, the load's
    // own "no derivation stands" sentinel and never a real cursor value — is
    // the same one-line edge clear_audition_sequence makes at the act's end,
    // and it makes the next engaged pre-paint due, which re-derives on the
    // resting cursor in the new geometry. Unconditional: the hook reads the
    // pin itself, so with the lamp off this writes a sentinel nothing consults.
    app.centered_derived_cursor = -1;

    // A zoom level valid at the old width may exceed the per-file effective
    // ceiling at the new width. The level ceiling and the viewport clamp both
    // live in clamp_viewport_start now; the resize keeps only its TRIGGER role
    // and delegates. When the level actually moved the reflow changed spp under
    // the playback predictor, so re-anchor it.
    const double old_zoom = app.zoom_level;
    clamp_viewport_start(app, audio);
    if (app.zoom_level != old_zoom && playback.is_playing())
        playback.resync_predictor();
}
