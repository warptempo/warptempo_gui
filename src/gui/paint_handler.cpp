#include "paint_handler.h"
#include "canvas_sink.h"
#include "target_render.h"
#include "notifications.h"
#include "chrome_spec.h"
#include "cool_edit_paint.h"

#include "gui_font.h"
#include "folder_overlay.h"
#include "icons.h"
#include "display_transform.h"
#include "materials.h"
#include "onscreen_keyboard.h"
#include "render.h"
#include "score_viewer.h"
#include "text_editor.h"
#include "text_shape.h"
#include "time_format.h"
#include "warp_frame_map_view.h"
#include "warp_frame_map.h"
#include "engine/engine_geometry.h"  // kN, kRs — the seed frame mirror

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
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
// THE BOTTOM ROW HAS NO MONOSPACE FACE (architect 2026-10-05, after his
// ACID Pro 3.0 and Vegas screenshots: "no monospace face anywhere";
// gui_font.h): row 8's clock, its tab name and its state line are THE
// PROGRAM FACE, the cues' 22 px (architect 2026-10-26 ~11:30: "the bottom
// left timestamp and tab/github/etc text should take 26 as well", the size
// since tuned with the cues and settled ~13:05, gui_font.h), and the
// render player's two time fields with them (architect 2026-10-26 ~12:10,
// "yes" to symmetry with row 8's clock); every modal string the body face,
// 22 too, a row of its own (gui_font.h's table). The no-wiggle guarantee is the fields' own
// fixed widths.
//
// EVERY STRING THE STATE LINE CARRIES — the queue/render status and the
// history walk line — IS THE PROGRAM FACE (gui_font(GuiFace::Program)) in
// the bone over the cue label's shadow, on the row's ground a group space
// right of the clock's field (architect 2026-10-03; the block at the line's painter
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
// The no-wiggle DERIVATION — the widest digit, the
// "DD:DD.DDD" specimen — belongs to the time fields and lives at their own
// metrics (time_field_metrics), measured on the face rather than trusted to
// it.

// (The bottom row's faces are named at each of their sites — the program
// face for row 8's clock, name and state line and the render player's time
// fields, the body face for the MODAL DIALOG's question, line and button
// words; a run carries the font it was laid out on, so no context state
// needs selecting first.)

// (show_row_text, the row's plain-text tier — one run shaped and painted at
// (x, baseline) — lost its last two callers 2026-10-14: the dialog editor's
// label, removed, and the choice list's rows, gone with the list.)

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
// value any more, Ctrl+Shift+J copies it (its card, an event, naming the
// copied value — architect 2026-09-13; the chord 2026-10-14) and Ctrl+J goes
// to the marker it came from.
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
// commit-title and BPM editors painted as a label + dark inset field + OK/Cancel
// buttons in paint_modal_dialog (the two left since: the settings and BPM), and
// the prompt's text-plus-labels line became a message + real buttons. The
// one-run prefix+pending shaping died with it, the pending run shaping alone
// and the published click-to-caret geometry (AppState::DialogEditorText) the
// editor's own. Since 2026-10-14 the two editors' line stands in the modal
// family's box with no label and the prompt's question on its first row
// (modal_box.h); row 8 YIELDS WHOLE to the modal, which is no tenant of it.)

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
    // THE SURFACE HOLDS THE RULER LANE'S TOP THROUGH THE STRIP'S BOTTOM
    // (2026-10-13, FlagCache's head), so it lands at its own origin row,
    // flag_cache.origin_y, clipped as ever to the top strip: the clip's rows
    // above the origin take no source, and the rows it holds are the bytes
    // the whole-strip surface put there.
    // ONLY ONTO THE ROWS IT WAS LAID OUT FOR (2026-10-14, ON SCREEN IS AS
    // PAINTED): a surface whose band — its origin row and its height — is
    // not the live band (top_ruler_row_area's top through the strip's
    // bottom) is not blitted. A live gui_scale commit (apply_gui_scale)
    // moves the band at once and repaints the whole window, while the
    // surface is rebuilt only at the next tick (maybe_rebuild_flag_cache);
    // a frame painted between the two blitted the old scale's cues at the
    // old origin, inside the new, taller toolbar band — a lane the rebuild's
    // damage never reaches (it starts at the band's foot,
    // damage_window_top_rows), so the old text stood there for good. Refused,
    // that frame shows the marker lane's face without cues, and the
    // rebuild's own damage covers the lane on the frame that lands the new
    // surface. A WIDTH alone is not refused: a resize keeps the rows, the
    // surface stays inside the lanes that damage repaints, and the flags
    // ride one frame behind the window as the plate does.
    const int live_origin = top_ruler_row_area(app).y;
    const int live_h      = top_strip.y + top_strip.h - live_origin;
    if (flag_cache.surface && flag_cache.origin_y == live_origin &&
        flag_cache.height == live_h) {
        cairo_save(cr);
        cairo_rectangle(cr, top_strip.x, top_strip.y,
                        top_strip.w, top_strip.h);
        cairo_clip(cr);
        cairo_set_source_surface(cr, flag_cache.surface,
                                 top_strip.x, flag_cache.origin_y);
        cairo_paint(cr);
        cairo_restore(cr);
    }
}

// -- The redesigned rows: paint_icon_row ----------------------------------

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

// (ROW 1, THE MENU ROW — Explorer's menu band and then the plain menu bar's
// seat, its File, Edit and Settings titles flush left at a 2-W lead with 9 /
// 7 W pads, each title's Alt access key underlined — IS REMOVED, architect
// 2026-10-21: "I'd like to see v1 without the file/edit... row … keep the
// line above the top icon row". kMenuButtons, its access-key static_assert
// and paint_menu_row went with it; the record is git history.)

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
//     menus' item
//     tables were read by index (until the popups retired, 2026-10-21) —
//     so each surface is free to spell its own class.
//   * TITLE CASE SPELLING: principal words capitalized; articles,
//     conjunctions and prepositions of three letters or fewer lowercase unless
//     they lead ("Copy to Clipboard", "Load in Place", "Save and Commit", "Up
//     a Folder", "Toggle Add to Selection"); a hyphenated compound capitalizes
//     both halves ("Toggle Read-Only (O)"); a single word is the same string
//     in either class and settles nothing. The chord suffix "(Ctrl+S)" is not
//     part of the name and is spelled by its own owner.
//   * A CARD OR A LABEL NEVER NAMES A KEY TO PRESS; THE KEY TEXT IS TWO: THE
//     TOOLTIP'S KEY (architect 2026-10-27, "keep"; redesign_button_tooltip's
//     head, app_state.h, owns it) AND WINDOWS' UNDERLINED ACCESS
//     KEY on the menu row's titles and on every row of their pull-downs,
//     always shown (architect 2026-10-10, "part of the aesthetic and
//     therefore important"; the letters at access_key_of,
//     app_state.h, the line at access_underline_rect below), and since the
//     same day on THE DIALOG BUTTONS' WORDS (Windows strict; the prompts'
//     letters at PromptState, app_state.h; OK, Cancel and Close carry
//     none). It is an underline under
//     a letter of the plain word, never a bracket (the `[S]ave` spelling
//     stays retired), and the label strings stay the plain words.
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

// (THE CHROME'S OWN BUTTON PAINTER, paint_button_box — Windows' two-line
// faces in two families, a TOOLBAR button's SOFT edges on the on-screen
// keyboard's keys and a PUSH button's PLAIN ones on the dialogs' word
// buttons, raised at rest, sunken pressed and sunken over the Hilight dither
// checked — stood from 2026-10-02 until its last two wearers became the
// ebony case, 2026-10-22: architect, "buttons in ebony — yes". Its face
// answer, ButtonBoxFace and the glyph's shift it carried, went 2026-10-23:
// no roster key carries anything on its face.)

// THE ROSTER'S KEY — the band's and row 8's every button, the render
// player's and the dialogs' word buttons: THE HI-FI SKIN'S BLANK KEY
// (architect 2026-10-23; paint_key, cool_edit_paint.h, where its states
// stand). A CHECKED button (the roster's `selected`, `lamp`) is the key
// DOWN one device px right and down with its LED lit, a PRESSED one (a live
// press) TWO DEVICE PX whatever its checked state (the two depths,
// 2026-10-23) — a press lighting the LED for as long as it stands, the key
// back at its checked depth at the lift; a DISABLED button keeps the face its state gives, a
// dead checked button still down (the roster's rule at paint_icon_row),
// its legend dimmed by the caller. NO HOT FACE. It answers nothing: no
// roster key carries anything on its face (the legends, the word buttons'
// words among them, stand under the key and do not move); the on-screen
// keyboard's caps, which do ride the face, are paint_key's own caller and
// read the ride there.
void paint_roster_case(cairo_t* cr, const GuiRect& r, bool lamp,
                       bool pressed) {
    paint_key(cr, r, pressed, lamp ? GuiKeyLed::Lit : GuiKeyLed::Unlit);
}

// THE KEY'S LEGEND SEAT (architect 2026-10-23, "text under buttons": the
// act's name printed UNDER the key on the ground, "just a little label
// underneath") — THE ONE PLACE the legend's placement is decided, so a move
// inside the key face is a one-site change: the legend face (GuiFace::Legend,
// 14 px at 300 %) CENTERED on the key's x extent, its cap band centered in
// the strip's bottom pad (redesign_baseline on the pad's rows, the pad
// strip_pad_px under the key). A legend never widens a key; one wider than
// its key overhangs into the run's gap.
struct KeyLegendSeat {
    double x        = 0.0;   // the run's pen
    double baseline = 0.0;
};
KeyLegendSeat key_legend_seat(const GuiRect& key, double run_w) {
    const GuiFont font = gui_font(GuiFace::Legend);
    const double pad_y = static_cast<double>(key.y + key.h);
    const double pad_h = static_cast<double>(strip_pad_px());
    return KeyLegendSeat{
        static_cast<double>(key.x) + (static_cast<double>(key.w) - run_w) * 0.5,
        redesign_baseline(font, pad_y, pad_h)};
}

// THE RENDER PLAYER'S LEGENDS (architect 2026-10-23, on the approved
// player frame: PREV PLAY NEXT … REFRESH DOWN UP
// FOLDER LOAD CANCEL), by the act each key runs; the root's sixth slot,
// Delete, its own word. THE REPEAT KEY READS REPEAT (architect 2026-10-23,
// "player repeat btn is mislabeled refresh"): its act is Toggle Repeat One,
// the mock's REFRESH a misprint.
const char* render_player_button_legend(AppState::PlayerButtonAct act) {
    using Act = AppState::PlayerButtonAct;
    switch (act) {
        case Act::Home:          return "PREV";
        case Act::PlayPause:     return "PLAY";
        case Act::NextTrack:     return "NEXT";
        case Act::RepeatOne:     return "REPEAT";
        case Act::HighlightDown: return "DOWN";
        case Act::HighlightUp:   return "UP";
        case Act::Up:            return "FOLDER";
        case Act::LoadInPlace:   return "LOAD";
        case Act::Delete:        return "DELETE";
        case Act::Close:         return "CANCEL";
        case Act::None:          return "";
    }
    return "";
}
// THE PLAY KEY'S PRINT NAMES ITS NEXT PRESS (architect 2026-10-25, the
// roster's legend rule at redesign_button_legend's stateful overload):
// render_player_play_face, the owner the tooltip and the glyph read — PLAY,
// PAUSE, or OPEN over a folder row. Every other act forwards to the
// table above (REPEAT a toggle, one print in both states).
const char* render_player_button_legend(AppState::PlayerButtonAct act,
                                        PlayerPlayFace face) {
    if (act == AppState::PlayerButtonAct::PlayPause) {
        switch (face) {
            case PlayerPlayFace::Pause:      return "PAUSE";
            case PlayerPlayFace::OpenFolder: return "OPEN";
            case PlayerPlayFace::Play:       break;
        }
    }
    return render_player_button_legend(act);
}

// THE KEY'S LEGEND, LAID OUT ONCE (2026-10-23) — the one owner of where a
// key's legend stands and of the pixels the key and its legend paint
// together, read by the painter (paint_key_legend), by the strips' repaint
// gates (clip_meets on `extent`) and by the as-painted publications
// (publish_button_face's and the modal row's coverage tests), so a coverage
// test and the paint cannot part. THE EXTENT IS THE KEY UNITED WITH THE
// LEGEND'S LINE BOX — the run's advance across, the face's ascent above the
// baseline to its descent below (or the access underline's foot, when it
// reaches lower), each edge rounded outward to whole px — because the
// legend stands OUTSIDE the key, in the strip's bottom pad and overhanging
// the key's sides when wider: a damage rect confined to the pad (a tooltip
// closing over a bottom-row legend) has the strip's ground erased under it,
// and a gate on the key alone would skip the legend and leave it erased.
// The run is the shaping memo's (text_shape::memoized_run), so the gate's
// layout and the paint's are one shaping.
struct KeyLegendLayout {
    text_shape::SharedRun run;      // null for an empty word
    KeyLegendSeat         seat;
    GuiRect               extent;   // the key united with the legend's box
};
KeyLegendLayout key_legend_layout(const GuiRect& key, std::string_view word,
                                  int access = -1) {
    KeyLegendLayout out;
    out.extent = key;
    if (word.empty()) return out;
    const GuiFont font = gui_font(GuiFace::Legend);
    out.run  = text_shape::memoized_run(font, word);
    out.seat = key_legend_seat(key, out.run->width_px);
    const double base = out.seat.baseline;
    const int lx1 = static_cast<int>(std::floor(out.seat.x));
    const int lx2 =
        static_cast<int>(std::ceil(out.seat.x + out.run->width_px));
    const int ly1 = static_cast<int>(std::floor(base - gui_font_ascent_px(font)));
    int ly2 = static_cast<int>(std::ceil(base + gui_font_descent_px(font)));
    // THE ACCESS UNDERLINE'S FOOT (access_underline_rect: one line, one
    // line under the rounded baseline).
    if (access >= 0)
        ly2 = std::max(ly2, static_cast<int>(std::nearbyint(base)) +
                                2 * hairline_px());
    const int x1 = std::min(key.x, lx1);
    const int y1 = std::min(key.y, ly1);
    const int x2 = std::max(key.x + key.w, lx2);
    const int y2 = std::max(key.y + key.h, ly2);
    out.extent = GuiRect{x1, y1, x2 - x1, y2 - y1};
    return out;
}

// A KEY'S LEGEND under it (key_legend_layout), in the legend ink kLegendInk
// or, `enabled` false, the dim kDimInk; `access` the access key's byte in
// `word` to underline (-1 none: the Windows-strict dialog rules unchanged,
// only the drawing).
void show_access_run(cairo_t* cr, const text_shape::ShapedRun& run,
                     const char* label, int access, double x,
                     double baseline, GuiColor ink);
void paint_key_legend(cairo_t* cr, const GuiRect& key, std::string_view word,
                      bool enabled, int access = -1) {
    if (word.empty()) return;
    const KeyLegendLayout layout = key_legend_layout(key, word, access);
    const std::string text(word);
    show_access_run(cr, *layout.run, access >= 0 ? text.c_str() : nullptr,
                    access, layout.seat.x, layout.seat.baseline,
                    hex(enabled ? kLegendInk : kDimInk));
}

// THE ONE AS-PAINTED COVERAGE TEST (architect 2026-09-24), read by every
// publisher of an as-painted stash that a per-tick comparator replays — the
// roster's faces (publish_button_face) (the open dropdown's item verdicts
// were a second until 2026-10-21). A stash may be refreshed only on a frame whose clip
// repaints every pixel the stash describes, and THOSE PIXELS ARE THE RECT'S
// DRAWABLE PART, the rect intersected with the surface — never the logical
// rect whole: cairo bounds every clip by the surface, so a rect running past
// the window's edge (the icon row's rightmost buttons cropping past the tablet's fit
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

// WHETHER THE CLIP REACHES A RECT AT ALL (2026-10-13): a drawing bounded by
// `rect` under a clip whose extents miss it lays down no pixel, so a painter
// may skip it — the same pixels by construction, the cost of cairo walking a
// fully clipped path saved (the toolbar band's and row 8's cases with their
// legends, key_legend_layout's extent, row 8's clock group and state line,
// under the narrow
// damage a frame most often carries). Never a gate on a PUBLICATION: the
// faces, rects and records a painter stashes are written whether or not
// their pixels are drawn.
static bool clip_meets(cairo_t* cr, const GuiRect& rect) {
    if (rect.w <= 0 || rect.h <= 0) return false;
    double cx1 = 0.0, cy1 = 0.0, cx2 = 0.0, cy2 = 0.0;
    cairo_clip_extents(cr, &cx1, &cy1, &cx2, &cy2);
    return static_cast<double>(rect.x) < cx2 &&
           static_cast<double>(rect.x + rect.w) > cx1 &&
           static_cast<double>(rect.y) < cy2 &&
           static_cast<double>(rect.y + rect.h) > cy1;
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
// OVERFLOW RULE at a window too narrow for the row, a member the view pairs
// cover whole (kIconRowViewPairs), since 2026-10-05 the icon row's
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
//
// THE COVERAGE IS WHAT THE BUTTON PAINTS, THE RECT WHAT IT ANSWERS
// (2026-10-23): `painted` — the key united with its legend
// (key_legend_layout's extent), cut by whatever clip the row paints under —
// is the rect the clip must cover, because the legend under the key wears
// the enabled bit (dimmed when dead) and a refresh over a clip that missed
// it would stamp a bit its pixels never took; `rect`, the key alone, is what
// the face publishes for the press, the hover and the tooltip (a legend is
// print on the ground, never a target). The empty rects take the overload
// below, which covers what it publishes.
//
// THE LEGEND IS THE FOURTH TERM (2026-10-25: the word names the next press,
// redesign_button_legend's stateful overload): `legend` is the word the
// caller paints, stashed with the extent it paints over, and THE COVERAGE IS
// THE UNION OF THAT EXTENT AND THE ONE LAST STASHED — a word that narrows
// (RECALL to FIT) must have its old ends erased by the same clip before the
// stash may say the new word, or the comparator would go blind over the
// stale ends.
AppState::RedesignButtonFace& publish_button_face(
    cairo_t* cr, AppState& app,
    const GuiAudio& audio, const GuiPlayback& playback,
    const GuiTargetRender& target_render,
    RedesignButton id, const GuiRect& rect, const GuiRect& painted,
    const char* legend) {
    AppState::RedesignButtonFace& face =
        app.redesign_buttons[redesign_button_index(id)];
    face.rect = rect;
    const GuiRect covered =
        face.painted.w <= 0 || face.painted.h <= 0 ? painted
        : painted.w <= 0 || painted.h <= 0         ? face.painted
                                    : union_rect(face.painted, painted);
    if (clip_covers_drawable(cr, app, covered)) {
        face.enabled  = redesign_button_enabled(app, audio, audio.total_frames(),
                                                playback, target_render, id);
        face.selected =
            redesign_button_selected(app, audio.total_frames(), id);
        face.glyph = redesign_button_glyph(app, id);
        face.legend  = legend;
        face.painted = painted;
    }
    return face;
}
// A MEMBER THAT DOES NOT STAND publishes the empty rect and paints nothing;
// its stash takes the live word (nothing painted can disagree with it) and
// an empty extent, the walk's own damage having erased what stood there.
// ITS SECOND RECT GOES WITH IT (RedesignButtonFace::field, 2026-10-27): the
// PLAY key hidden in the `h` view or yielded under a modal takes row 8's
// clock out of the roster with it, the clock answering where the key does.
AppState::RedesignButtonFace& publish_button_face(
    cairo_t* cr, AppState& app,
    const GuiAudio& audio, const GuiPlayback& playback,
    const GuiTargetRender& target_render,
    RedesignButton id, const GuiRect& rect) {
    AppState::RedesignButtonFace& face =
        app.redesign_buttons[redesign_button_index(id)];
    face.painted = GuiRect{0, 0, 0, 0};
    face.field   = GuiRect{0, 0, 0, 0};
    return publish_button_face(cr, app, audio, playback, target_render, id,
                               rect, rect, redesign_button_legend(app, audio, id));
}

// ROW 4 — THE ICON ROW: THE TOOLBAR BAND (architect 2026-10-09, the program
// is Cool Edit; the simple layout 2026-10-22, program_spec.h's head). Its
// metrics — the case and its glyph seat, the strip pad and the run gap —
// live in program_spec.h and render.h's program block, where the lane table
// and row 8 read them too; its ends are the program ring's outer columns
// (render.h's program_ring_columns, 2026-10-23); this row spells none of
// them.
//
// THE VERTICAL STORY: the logo lane (render.h's logo lane block, the band's
// upper pad since 2026-10-24), the case (band_case_top_offset_px), the
// strip pad — row 8's lower pad; row 8 the strip pad over its case.
//
// THE HORIZONTAL WALK (architect 2026-10-22: "push all icons
// together … add a spacing after render, and after flatten"): the roster's
// left members in THREE PLAIN RUNS on the ground — Open Project .. Render,
// Zoom to Fit .. Flatten, Play Renders .. Score — the keys
// abutting inside a run and run_gap_px of ground between two
// (redesign_button_opens_icon_run, app_state.h, the runs' leaders); the
// right five — the history opener and the two view pairs — ONE RUN ending
// at the program ring's right edge, the left walk starting at its left
// column (icon_row_seats). No pane, groove, recess
// or line (Cool Edit's panes and grooved recess stood 2026-10-09 to
// 2026-10-22).

// THE PAINTER'S HALF OF THE ICON-ROW ROSTER: each button's id, in painted
// order; the key prints its legend and draws no glyph (the hi-fi skin,
// 2026-10-23). The press claim's chord table (input_pointer.cpp) is the
// other half; both key off the same ids.
//
// WHAT LEADS A BUTTON IS NOT HERE ANY MORE (2026-08-13): the struct carried an
// IconRowLead column — First / Gap / Separator — of which only Separator ever
// meant anything to the walk, First and Gap taking the same branch. The row's
// GROUP BOUNDARIES are redesign_button_opens_icon_group's (app_state.h), the
// unit of the `h` view's stand-in swap, and its RUN BOUNDARIES — the run gap
// of wood, the band's one spacing since the simple layout (2026-10-22) —
// redesign_button_opens_icon_run's.
struct IconRowDef {
    RedesignButton id;
};
constexpr IconRowDef kIconRowButtons[] = {
    // THE TOOLBAR GROUP — the row's FIRST since the 2026-08-12 grand
    // relayout, Open Project at its head since 2026-10-07 (below), the
    // relayout having dissolved row 2 (architect: the labeled lane goes, "the icon
    // to represent all those various meanings"): Save, Undo, Redo and Render at
    // the row's left, the SAME chords, gates, disabled derivations and
    // stateful faces the labeled buttons carried (Save's and Render's
    // stateful legends, redesign_button_legend; Render serves BOTH plain
    // render and the iteration sweep by the architect's same-day ruling, the
    // tooltip alone forking).
    //
    // OPEN PROJECT LEADS THEM ALL (architect 2026-10-07), ONE BUTTON — "just
    // to keep those two together", the button and its shift twin, Revert:
    // no drop-down — and THE FIVE ARE THE BAND'S FIRST RUN (the simple
    // layout, 2026-10-22: Open, Save, Undo, Redo and Render abutting, the run
    // gap after Render; redesign_button_opens_icon_run). Undo still leads a
    // GROUP with Redo and Render (redesign_button_opens_icon_group), the unit
    // the `h` view's stand-ins replace, which paints nothing.
    //
    // COPY PHASE RESETS STANDS BETWEEN REDO AND RENDER since 2026-10-25
    // (architect: "copy should be moved to the left of render, that's
    // usually where copy action would go, between redo [and render], and
    // it's where it goes in kdenlive … copy is very rarely used, a handful of
    // times in a movement, and it does affect the whole large batch of
    // markers; undo / redo also affect the markers … the copy right now is
    // pushing all the other buttons one column to the left, which is further
    // away from my pen hand"): the first run is SIX — OPEN SAVE UNDO REDO
    // COPY RENDER — and COPY is a member of Undo's group, which the `h`
    // view's stand-ins replace whole (it is hidden there anyway,
    // history_mode_hides_button). Its chords, face and tooltip came across
    // unchanged from row 8's verb group (2026-10-24 to 2026-10-25): bare P,
    // Ctrl+P, Ctrl+Shift+P, the barrel's ctrl and the long press as there.
    {RedesignButton::OpenProject},
    {RedesignButton::Save},
    {RedesignButton::Undo},
    {RedesignButton::Redo},
    {RedesignButton::IconCopyPhaseResets},
    {RedesignButton::Render},
    // (The view axes' faces are the two radio pairs at this row's right end,
    // architect 2026-10-14 — kIconRowViewPairs below; no view button stands
    // in this group.)
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
    // off the walk, no separator moving; ZOOM IN leads the group (the step
    // pair in front of Zoom to Fit since 2026-10-27). What
    // the group collects is the VIEWPORT CLASS — Zoom In, Zoom Out, Zoom to
    // Fit, the magnification lamp and FOLLOW — at the head of the band's
    // second run.
    // ZOOM IN AND ZOOM OUT (architect 2026-10-27, "the zoom in and out to the
    // left of FIT ... including the position in the toolbar"): bare `=` and
    // bare `-`, one level about the viewport's centre, the shift press and
    // the long press two (RedesignButton::IconZoomIn's record), legends IN
    // and OUT. Two cases onto the walk at the second run's head, no run
    // moving; Zoom In is the group's and the run's leader.
    {RedesignButton::IconZoomIn},
    {RedesignButton::IconZoomOut},
    // THIS TABLE IS THE ROW'S PAINTED ORDER — the walk
    // below is a plain accumulation over it — so a reorder is rows swapping,
    // plus the group's and the run's leaders in
    // redesign_button_opens_icon_group and redesign_button_opens_icon_run
    // (app_state.h) and the roster enum's own order, which they keep in
    // step. No count, no gap and no width follows a swap.
    // ZOOM TO FIT (2026-08-12, the grand relayout, as Full Zoom Out; renamed
    // 2026-10-24 — the architect's live placement, "the rest in the icon row,
    // after the trim"): bare `0`, Shift+0 Frame the Trim on its shift press
    // and Ctrl+Shift+0 Reset Trim on its ctrl+shift press (2026-10-24;
    // Shift+0 the reset from 2026-09-22), the command's pointer home (the
    // Navigation dropdown that duplicated it was deleted 2026-08-15; the
    // record is at kFilePopupItems, app_state.h). The step pair stands in
    // front of it (above). WORKING-ZOOM CENTER (bare `c`) stood
    // behind it until 2026-09-29, when the architect moved it to the bottom
    // row (kTransportNudgePlayGroup since 2026-10-18) — one box and one 2px gap off
    // this walk, no separator moving.
    {RedesignButton::IconZoomFitBest},
    // WAVEFORM MAGNIFICATION (architect 2026-09-22), the backtick's
    // lamp, after Zoom to Fit in the same group — the louder waveform, its
    // vertical scale grown.
    // It joins the group rather than opening one, so it adds one box and one
    // 2px gap to the walk and no separator.
    {RedesignButton::IconWaveformMagnification},
    // (THE WAVEFORM MAGNIFICATION PAIR closed the same group from 2026-08-26
    // to 2026-09-14 — magnify wearing zoom-in-y and reduce wearing
    // zoom-out-y — and left with the setting it stepped (architect
    // approval 2026-09-14), two boxes and two 2px gaps off the walk and no
    // separator moving.)
    // (THE SINGLE-MARKER VERBS opened a separator-led group here from
    // 2026-08-12 until the architect moved them to the BOTTOM ROW's right
    // block on 2026-08-18, the bottom row's own table below.)
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
    // GridIterations had been iteration mode's since 2026-08-18, when it took the
    // summation sigma's slot and the sigma moved to the CUMULATIVE toggle), and
    // the separator went too — FOLLOW, the survivor, joined the ZOOM GROUP
    // above rather than standing alone behind a divider. THE TWO BUTTONS AND
    // BOTH GLYPHS CAME BACK ON 2026-09-04 in a group of their own, further
    // down this table; the mass-marker group itself did not.)
    // FOLLOW (bare `f`): the view follows the playing scanner. Out for the
    // hours of 2026-09-23 the Shift+C chase posture replaced it, and back
    // that evening: one box and one 2px gap onto the walk, no separator
    // moving.
    {RedesignButton::IconFollow},
    // THE ITERATION GROUP (architect 2026-09-04): the BPM opener (bare `m`)
    // and grid iteration mode (bare `i`), back from the menu row behind a
    // separator of their own, just ahead of the render-entry pair. The
    // architect deleted the ITERATIONS dropdown once this row had room again,
    // which is the 2026-08-27 Series relocation run in reverse — the two
    // commands have ONE pointer home and it is these buttons. The row gains
    // two boxes, one 2px gap and ONE separator, so the group count moves too
    // (the leader is IconBpm, redesign_button_opens_icon_group).
    {RedesignButton::IconBpm},
    {RedesignButton::IconIter},
    // FLATTEN CLOSES THE GROUP (architect 2026-09-19), up from the bottom
    // row's marker verbs where it had stood since that morning: the group
    // gains one box and one 2px gap, no separator moves, and the leader is
    // unchanged. ITS SEAT IS A CLASSIFICATION BY SUBJECT — a deviation term
    // exists because a GRID SWEEP APPENDED ONE to every cell it wrote, and
    // Ctrl+F is the one road that takes those terms off again, so the act
    // stands beside the mode that produces what it removes (the reasoning in
    // full, and what is deliberately NOT claimed for it, is at the roster
    // entry, app_state.h).
    {RedesignButton::IconFlatten},
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
    {RedesignButton::IconListen},
    // The read-only toggle: its lit / sunken face says read-only, as every
    // toggle's does.
    {RedesignButton::IconReadOnly},
    // SETTINGS (architect 2026-09-29), right after the padlock and ahead of
    // the tooltip lamp (architect 2026-10-01: help comes after settings): bare
    // `;`, the settings prompt — the typed road's pointer spelling, one box and one 2px gap onto the walk
    // and no separator moving.
    {RedesignButton::IconSettings},
    // ENABLE TOOLTIPS (architect 2026-09-29, evening), CLOSING the
    // render-entry group behind Settings since 2026-10-01 (architect: help
    // comes after settings): the bare backslash's LAMP. Dark at every open, and while dark no tooltip shows
    // anywhere; lit, the Qt model as ruled. One box and one 2px gap onto the
    // walk, no separator moving.
    {RedesignButton::IconTooltips},
    // THE SCORE (architect 2026-10-26), the third run's last but one since
    // 2026-10-28: bare `m`, the score viewer's opener and, while it stands,
    // its closer (RedesignButton::IconScore). One case onto the walk, no run
    // moving.
    {RedesignButton::IconScore},
    // THE NOISE (architect 2026-10-28), the third run's last since
    // 2026-10-28: bare `w`, the background sound's lamp
    // (RedesignButton::IconNoise, noise.h). One case onto the walk, no run
    // moving.
    {RedesignButton::IconNoise},
    // (THE HISTORY OPENER closed this walk until 2026-10-06; it is
    // right-anchored since, kIconRowHistoryOpener below.)
};

// THE HISTORY STAND-INS — THE ICON ROW'S ONE MODE SWAP (architect
// 2026-10-05, so the row fits the tablet's 2304 device px at gui_scale 400):
// WHILE THE `h` VIEW STANDS, the history group's four companions here (the
// walk lamp, Cumulative, Revert and Load in Place; Older and Newer are row
// 8's since 2026-10-11, kTransportHistoryStandIns) paint in the
// slots of the AUTHORING GROUPS THE VIEW REFUSES WHOLE, and those groups'
// members publish empty rects; outside the view the authoring groups stand
// and the four publish empty rects. The opener (IconHistory) is not a
// companion and stands at its own slot in both states. Every other x is the
// walk's plain accumulation over whichever members stand, and the keyboard
// is untouched: only the row's picture changes, every chord answering
// exactly as it did.
//
// WHICH GROUPS: each entry names the LEADER of the authoring group it
// replaces (redesign_button_opens_icon_group), and the stand-in group takes
// that leader's place in its run. The two are the groups every member of
// which the view refuses through the derived partition
// (history_mode_disables_button, input_pointer.cpp — none of their chords
// is the mode's vocabulary or on its allowlist): UNDO'S (Undo, Redo, Copy
// Phase Resets, Render — Ctrl+Z, Ctrl+Shift+Z, the `p` family, Ctrl+Alt+R)
// and THE ITERATION GROUP (Ctrl+B, bare `i`, Ctrl+F). No other group is
// refused whole in the view: the zoom group keeps Zoom In, Zoom Out and Zoom
// to Fit (`=`, `-` and bare `0`) and the render-entry group Play Renders (bare `l`, admitted 2026-10-25),
// the tooltip lamp (bare backslash) and the NOISE lamp (bare `w`, admitted
// 2026-10-28).
//
// THE SLOTS: UNDO'S FOUR (Undo, Redo, Copy Phase Resets and Render, since
// 2026-10-25; three until then) take the view's two acts, Revert and Load in
// Place, so that group stands two cases narrower in the view; THE ITERATION
// GROUP'S THREE take the two reading lamps, the walk source and the
// cumulative reading (architect 2026-10-26, the toggles reinstated "as they
// were at the time of removal"), so that group stands one case narrower and
// its third seat is empty. OLDER AND NEWER LEFT UNDO'S GROUP FOR ROW 8
// (architect 2026-10-11 ~02:40; they stood in Undo's and Redo's slots from
// 2026-10-05): they take the Left / Right arrows' seats in the
// nudge-and-play group there, the row's per-seat stand-ins
// (kTransportHistoryStandIns). The zoom group stands on the four members the
// view does not refuse in every session (Zoom In and Zoom Out since
// 2026-10-27, Zoom to Fit and, since 2026-10-12, Waveform Magnification) and
// the render-entry group on three (Play Renders,
// since 2026-10-25, Enable Tooltips, and Noise since 2026-10-28), their others hidden
// (history_mode_hides_button, paint_icon_row's walk).
// The history opener and the view pairs are right-anchored and do not move
// (kIconRowHistoryOpener).
//
// THE REPAINT: the mode's two edges, open_history_mode_fresh and
// close_history_mode (input_key_dispatch.cpp), each end in full-window
// damage, so the row repaints whole on the frame the mode flips and every
// member publishes its rect — a real one or an empty one — in that pass; a
// press resolves against that painted row (publish_button_face).
constexpr IconRowDef kIconRowHistoryActGroup[] = {
    {RedesignButton::HistoryRevert},
    // THE LOAD IN PLACE (architect 2026-09-01, off the render-entry group):
    // the icon row's press runs the `h` view's own load — the confirmation on
    // the viewed walk member, the render player's Load in Place act.
    {RedesignButton::IconLoadInPlace},
};
// THE READING GROUP (reinstated architect 2026-10-26): Cumulative first,
// then the walk lamp — CUMUL and SESSION, each key's LED its state
// (architect 2026-10-26: "cumul and session should switch places — cumul is
// more common").
constexpr IconRowDef kIconRowHistoryReadingGroup[] = {
    {RedesignButton::HistoryCumulative},
    {RedesignButton::SessionHistory},
};
struct IconRowStandIn {
    RedesignButton                replaces;   // the authoring group's leader
    std::span<const IconRowDef>   members;
};
constexpr IconRowStandIn kIconRowHistoryStandIns[] = {
    {RedesignButton::Undo,    kIconRowHistoryActGroup},
    {RedesignButton::IconBpm, kIconRowHistoryReadingGroup},
};

// THE HISTORY OPENER — RIGHT-ANCHORED (architect 2026-10-06): bare `h`, the
// one history button that stands in every state, the toggle's same press
// entering and leaving the view. A group of its own since 2026-08-18 ("place
// a separator before the history button, and place cumulative/etc after the
// history button") and alone since 2026-10-05, when its companions went
// to the stand-in slots. IT TAKES NO PART IN THE LEFT WALK: it heads the
// band's RIGHT RUN, attached to Source with no gap (architect 2026-10-22,
// his layout verdict: "Layout 4" — History attached to Source), and the left
// runs' limit stands one run gap left of it (paint_icon_row). THE REASON IS
// THE PEN: while the opener closed the
// left walk, the stand-in swap's one-case-narrower iteration group put its x
// 31 Windows px further left inside the view than outside, so the button
// that toggles the view moved out from under the pen at its own press;
// anchored to the lane's right edge, which no state moves, it stands at one x
// in both. It paints whole beside the view pairs under the same overflow
// rule (kIconRowViewPairs), the left groups yielding to both. Its seat is
// the same in both states, and the mode's two edges damage the whole window
// anyway (THE REPAINT, above).
constexpr IconRowDef kIconRowHistoryOpener =
    {RedesignButton::IconHistory};

// THE VIEW PAIRS — THE ICON ROW'S LAST, FLUSH AT ITS RIGHT EDGE (architect
// 2026-10-01; two radio pairs since 2026-10-14): the Source / Target pair on
// bare 1 / 2 (the audio view) and the Warp / Phase-reset pair on bare 3 / 4
// (the markers view), the view the product of the
// two lit members, in their keys' order. THE ACTS CAME FROM THE ROW-1 VIEW
// BAR (2026-08-02..2026-10-01), itself kdenlive's workspace switcher — the
// blue "Logging | Editing | Audio | Effects | Color" bar at the far right of
// its menu row — reborn as the selectors, keys with printed legends since
// the hi-fi skin (2026-10-23).
// THE BAND'S OWN METRICS, NONE NEW: the program's case
// (render.h's program block), the four abutting behind the history opener in
// the band's one right run, Phase Reset's case ending at the program ring's
// right edge (icon_row_seats; the two panes with their groove
// 2026-10-14 to 2026-10-22). THE BAND'S FACES, and EACH
// PAIR A RADIO THAT POPS: its lit member is its axis's current value
// (redesign_button_selected reads one axis per button) and a press on it
// SELECTS THE PAIR'S OTHER MEMBER at its lift (architect 2026-10-23 night,
// "building a suction on the other one is the right way to go"; the consumed
// nothing of 2026-10-14 reversed; the key's own resolution, GuiInputHandler::
// on_key) — never greyed for it — the hold showing the pressed face over the
// lit one (paint_roster_case: Cool Edit's one down face for both).
//
// THE OVERFLOW RULE (architect 2026-10-01): AT ANY WINDOW WIDTH WHERE THE ROW
// CANNOT HOLD EVERY GROUP, THE VIEW PAIRS WIN — and since 2026-10-06 the
// history opener seated against their left with them (kIconRowHistoryOpener).
// All paint LAST, whole, at their right-anchored places; the runs to
// their left yield, clipped one run gap short of the opener, and each
// yielding member PUBLISHES ONLY WHAT IT
// PAINTED — its box cut at that column, or an empty rect when the clip
// covers it whole — so a press, a hover and a tooltip land on exactly the
// pixels on screen (on screen is as painted; paint_icon_row's walks). The
// band holds every run down to about 633 Windows px of window width outside
// the `h` view and 472 inside it (the width math at paint_icon_row, its one
// statement, re-derived 2026-10-28 with the NOISE lamp, plus the ring's
// inset at each window edge, about 13 W), so neither host reaches the rule
// at its scale: the tablet's 2304 device px hold the row in both states up
// to about 360 % and the laptop's 1920 up to about 300 % (the W arithmetic
// over the device width; each element's own rounding moves the edge by a
// pixel or so).
constexpr IconRowDef kIconRowViewPairs[2][2] = {
    {{RedesignButton::ViewSource},
     {RedesignButton::ViewTarget}},
    {{RedesignButton::ViewWarp},
     {RedesignButton::ViewPhase}},
};

// THE BAND'S RIGHT RUN (architect 2026-10-22, the simple layout): the
// history opener and the two view pairs, five cases abutting in painted
// order, right-anchored (icon_row_seats), never hidden.
constexpr IconRowDef kIconRowRightRun[] = {
    kIconRowHistoryOpener,
    kIconRowViewPairs[0][0], kIconRowViewPairs[0][1],
    kIconRowViewPairs[1][0], kIconRowViewPairs[1][1]};

// -- THE FLOATING SURFACES: the hover tooltip and the notification cards --
//
// The tooltip's metrics are measured off its period capture, ReactOS's
// "Views"; its chrome is the floating surfaces' one box
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
// Views on that capture): the capture's one-line box is 19 rows,
// 1 + 2 + 13 + 2 + 1 — the black line, two rows of face, the 13-row cell,
// two rows, the line — and two face columns stand between the left line and
// the V, so the pad is 2 each way INSIDE the four-sided frame, whose line is
// ONE DEVICE PX since the hi-fi skin (hairline_px, 2026-10-23; one scaled W
// until then) and whose band is the legend face's line box: at the tablet's
// 300 %, 1 + 6 + band + 6 + 1 for one line, at the laptop's 138 %,
// 1 + 3 + band + 3 + 1 (the band rounded once). The gap is the laptop pixel's 4 re-authored at the unit's
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
// THE SEATS AT THE TABLET'S 300 %: a 17-W box (a folder overlay row), 51
// rows, seats at row floor((51 + 24) / 2) = 37; at the laptop's 138 % its 23
// rows (17 x 1.38 = 23.46) seat at floor((23 + 11.04) / 2) = 17. The answer is
// independent of the box's y by construction rather than by a tie rule.
//
// ONE SEAT FOR EVERY CHROME LABEL (architect 2026-10-22: the tooltip and the
// cards came back to it — the line box centered in their interior read low
// under descenders and the one-W lift on it overshot). THE BOX SEATS: the
// logo lane's brand, the row-8 clock (in its time field) and the
// state line beside it (on THE FIELD's box since 2026-10-07 — the reasoning
// is at paint_bottom_row_buttons_and_clock), the tooltip's and the
// notification card's first lines (a further line one line height lower),
// the prompt's question and the
// dialog editor's line in the modal family's box (each row on the list's
// row box, 2026-10-14), the render player's
// two time fields, the modal buttons' own labels, the on-screen
// keyboard's caps, the folder overlay's rows, the menu row's anchors
// (the plain menu bar's seat, a capture of the real OS
// beating ReactOS's band, architect 2026-10-09 ~17:40).
//
// THE CANVAS COLUMN'S TEXT TAKES ANOTHER SEAT (2026-10-09, the program is
// Cool Edit): THE RULER'S DIGITS stand on an AUTHORED BASELINE ROW of their
// lane — program_spec.h's ruler_baseline_px (the architect's row under one
// W of air, 2026-10-09 ~21:00); THE CUES' LABELS stand on THEIR CAP BAND
// CENTRED IN THE MARKER LANE by their own rounding (architect 2026-10-26
// ~11:30: the lane's centre plus half the cap band, rounded once —
// render.h's cue_baseline_px), the flag editor's selection band, the face's
// line box, standing from that baseline. (This solver is declared in
// render.h for its readers there.)
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
// AND THE MODAL FIELD LABEL'S 1px: that label read THE BUTTONS' OWN SEAT
// rather than the field band's plus a drop until the label's removal
// (2026-10-14).
} // namespace
double redesign_baseline(const GuiFont& font, double box_y,
                         double box_h) {
    return box_y + std::floor((box_h + gui_font_cap_px(font)) * 0.5);
}
namespace {

// THE ACCESS KEY'S UNDERLINE — WINDOWS' MENU MNEMONIC (architect 2026-10-10:
// "accelerators are not used on the tablet, but they are part of the
// aesthetic and therefore important"; the letters and the keys are
// app_state.h's, access_key_of), ALWAYS DRAWN: on the menu row's
// titles, on every row of their three pull-downs and, since the same day, on
// the dialog buttons' words (Windows strict, PromptState's block; their
// legends since 2026-10-23, paint_key_legend), nowhere else (not the
// tooltips or the cards; nor the
// settings choice editor's list while it stood). Windows 2000 hides it until
// Alt is held; ReactOS
// always draws it, and so does this product — the tablet has no Alt to
// reveal it (a deliberate departure).
//
// THE LINE IS DrawText's PREFIX UNDERLINE (user32's TEXT_DrawUnderscore)
// read off his ReactOS capture (File's F, Edit's open
// E, Undo's U): ONE W THICK, ONE W OF AIR UNDER THE BASELINE (the cell's
// ascent + 1 — the F's last ink row 166, the line on 168), FROM THE MARKED
// GLYPH'S PEN TO ITS ADVANCE LESS ONE W (LineTo stops one short: the F's
// 6-px advance under a 5-px line, the U's 7 under 6). IN THE HI-FI SKIN THE
// LINE IS ONE DEVICE PX THICK, ONE DEVICE PX OF AIR UNDER THE BASELINE AND
// STOPPING ONE DEVICE PX SHORT (render.h's line rule, the sweep of
// 2026-10-23: "let's do a sweep for all of these Windows Pixel lines"; the
// three were scaled W until then); the pens are the run's own, measured
// through the shaping chokepoint (byte_offsets_px), never assumed. A run
// painted in a color takes the line in that color (a dead key's legend its
// dim ink).
GuiRect access_underline_rect(const text_shape::ShapedRun& run,
                              const char* label, int access, double x,
                              double baseline) {
    const std::size_t a = static_cast<std::size_t>(access);
    const std::vector<double> pen = text_shape::byte_offsets_px(
        run, std::char_traits<char>::length(label));
    const int line = hairline_px();
    const int x0 = static_cast<int>(std::nearbyint(x + pen[a]));
    const int x1 = static_cast<int>(std::nearbyint(x + pen[a + 1])) - line;
    const int y  = static_cast<int>(std::nearbyint(baseline)) + line;
    return GuiRect{x0, y, std::max(x1 - x0, line), line};
}
// A menu label in `ink`, its access key underlined (`label` the run's own
// text; a null label, the accelerator column's, paints the run alone).
void show_access_run(cairo_t* cr, const text_shape::ShapedRun& run,
                     const char* label, int access, double x,
                     double baseline, GuiColor ink) {
    set_palette_source(cr, ink);
    text_shape::show_shaped_run(cr, run, x, baseline);
    if (label != nullptr)
        paint_ink_rect(cr,
                       access_underline_rect(run, label, access, x, baseline),
                       ink);
}
} // namespace

// (THE CAPTION'S PAINTER — paint_caption_row, the window's own title bar
// at the top lane 0 from 2026-10-05: the quaver, "<piece> - Warptempo" and
// the three procedural glyphs, the hi-fi skin's flat lane 2026-10-23 — is
// REMOVED 2026-10-24 with the lane, architect: "release it to the window
// manager as we had in the beginning of this project". labwc draws the
// laptop's title bar; the tablet has none. Git history.)

// (THE PROGRAM'S FRAME'S PAINTER — paint_program_frame, Cool Edit's frame
// window round the program from 2026-10-10, its top row alone, one Shadow
// line under the caption, from 2026-10-21 — is retired with the row,
// architect 2026-10-22: the band stood directly under the caption.)

// (THE BOTTOM OVERLAYS' PAINTER — paint_bottom_overlays, the taskbar line in
// the chrome's Hilight across the window's whole width over the keyboard and
// the dialog, and the dialog's ground, 2026-10-10 to 2026-10-14 — is retired
// with the taskbars, architect 2026-10-14: the keyboard draws no line and
// the prompts and the dialog editors stand in the modal family's box,
// paint_modal_box.)

// THE `h` VIEW'S ONE SEPARATOR, ` | `: between the walk line's segments
// (history_walk_line below) and, since 2026-10-24, after the active tab's name
// where that line is what row 8's state cell shows
// (paint_bottom_row_buttons_and_clock) — one token, one spelling.
static constexpr const char* kSegmentSeparator = " | ";

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
// on BOTH walks. THE SCALE SEGMENT KEEPS ITS SIGNS, `[-]` before the then
// token and `[+]` before the now one, spelled here: the lane's labels lost
// theirs to the strike (architect 2026-10-26; history_diff_label), and this
// line, a plain run, has no strike to carry the side. (The "bottom-left corner" of the mode's record read
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
    // walks, because what Ctrl+Shift+S does in the view depends on it
    // whichever walk is showing. It stands ahead of the scale because the state cell clips
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
        line += "Scale: [-]";
        line += d->then_scale_token;
        line += " [+]";
        line += d->now_scale_token;
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
// READOUT retired whole — Ctrl+Shift+J copies the value and Ctrl+J goes to
// the marker it came from. `history_walk_line` above is untouched: the composer
// still stands beside its one caller, which is now that cell.)

// THE BAND'S STANDING MEMBERS, IN RUNS (2026-10-13: lifted out of
// paint_icon_row so the painter and the band's as-painted fingerprint,
// GuiPaintHandler::upper_rows_fingerprint, walk ONE membership; runs since
// the simple layout, 2026-10-22): every member that stands in this state
// laid into its run — a run's width is its standing members' — in the
// walk's order, and every member that does not stand into `absent`. A RUN
// LEADER (redesign_button_opens_icon_run, app_state.h) opens a new run, the
// run opening at its first STANDING member, so a run none of whose members
// stands is no run at all. The membership forks are keyed on the GROUPS
// (redesign_button_opens_icon_group, app_state.h — the swap's and the hide's
// unit): the history stand-ins (2026-10-05) and the `h` view's hide
// (2026-10-07). The right run is not in this walk: none of its five is ever
// hidden. `history` is the view's bit — the live one
// (app.history_mode.active) for the painter and the fingerprint, either for
// the materials' seats (program_key_seats, which walks both states).
static void icon_row_walk(bool history,
                          std::vector<std::vector<IconRowDef>>& runs,
                          std::vector<RedesignButton>& absent) {
    // THE STAND-IN SWAP (kIconRowHistoryStandIns, its one owner): in the `h`
    // view a group leader that names a stand-in group paints that group in
    // its place, and the rest of the replaced group is skipped up to the
    // next group leader; outside the view the stand-in groups never paint.
    // A DELIBERATE ASYMMETRY: A STAND-IN GROUP JOINS THE RUN TO ITS LEFT, NO
    // RUN GAP BEFORE IT (architect 2026-10-26 ~10:10) — the groups it replaces
    // open no run, so Revert and Load abut OPEN SAVE and Cumul and Session
    // abut FIT MAGNIFY, and the stand-ins take no run-opening mark of their
    // own: "I thought cumul and session were purposely placed in that group —
    // they are view-related, vaguely like fit/mag. The alternative is to have
    // two groups of two floating in the top strip, then? I think I prefer as
    // it is."
    // THE HIDE is history_mode_hides_button's answer under `history`
    // (input_pointer.cpp, architect 2026-10-07): in the `h` view a member
    // whose chord the view refuses in every session is skipped — no case, no
    // width, its empty rect published — and its neighbours close up.
    const auto hides = [history](RedesignButton id) {
        return history && history_view_hides_button(id);
    };
    const auto stand_in_for =
        [&](RedesignButton leader) -> const IconRowStandIn* {
        if (!history) return nullptr;
        for (const IconRowStandIn& si : kIconRowHistoryStandIns)
            if (si.replaces == leader) return &si;
        return nullptr;
    };
    bool run_pending = true;   // the next standing member opens a run
    const auto lay = [&](const IconRowDef& m) {
        if (run_pending || runs.empty()) runs.emplace_back();
        run_pending = false;
        runs.back().push_back(m);
    };
    bool replacing = false;
    for (const IconRowDef& def : kIconRowButtons) {
        if (redesign_button_opens_icon_run(def.id)) run_pending = true;
        if (redesign_button_opens_icon_group(def.id)) {
            const IconRowStandIn* si = stand_in_for(def.id);
            replacing = (si != nullptr);
            if (replacing)
                for (const IconRowDef& m : si->members) lay(m);
        }
        if (replacing || hides(def.id)) {
            absent.push_back(def.id);
            continue;
        }
        lay(def);
    }
    if (!history) {
        for (const IconRowStandIn& si : kIconRowHistoryStandIns)
            for (const IconRowDef& m : si.members) absent.push_back(m.id);
    }
}

// THE BAND'S SEATS (2026-10-21: lifted out of paint_icon_row so the painter
// and the materials' case patches, program_key_seats below, read ONE
// layout; the simple layout's runs 2026-10-22): the case's lengths, each a
// composite of its rounded parts (render.h's program block); THE CASE the
// logo lane under the band's top (band_case_top_offset_px, 2026-10-24; the
// strip pad until then). THE RIGHT RUN IS
// RESOLVED FIRST, because it decides where every run to its left may paint
// (the overflow rule at kIconRowViewPairs): its last case ends AT THE
// PROGRAM RING'S RIGHT EDGE (one past its outer right column; render.h's
// program_ring_columns, architect 2026-10-23), and THE LEFT RUNS' LIMIT
// stands one run gap left of its first case — they paint under a clip
// ending there and publish only the columns they painted, so a window too
// narrow for the band covers them and never the right run. THE LEFT WALK
// starts AT THE RING'S OUTER LEFT COLUMN, each run's cases abutting from
// its x, the next run a run gap after its last case. The band is flush with
// the ring at both ends, no inset of its own (program_spec.h's strip-ends
// block).
struct IconRowSeats {
    int btn_w = 0, btn_h = 0, gap = 0, btn_y = 0;
    int left_x = 0;      // the left walk's first case
    int right_x = 0;     // the right run's first case (the history opener)
    int left_limit = 0;  // the left runs' clip, a run gap short of it
    int case_x(int run_x, int i) const { return run_x + i * btn_w; }
    int next_run_x(int run_x, int n) const { return run_x + n * btn_w + gap; }
};
static IconRowSeats icon_row_seats(const AppState& app, const GuiRect& lane) {
    const ProgramRingColumns ring = program_ring_columns(app);
    IconRowSeats s;
    s.btn_w = icon_case_w_px();
    s.btn_h = icon_case_h_px();
    s.gap   = program_run_gap_px();
    s.btn_y = lane.y + band_case_top_offset_px();
    s.left_x = ring.x;
    const int right_end = ring.end;
    s.right_x =
        right_end - static_cast<int>(std::size(kIconRowRightRun)) * s.btn_w;
    s.left_limit = s.right_x - s.gap;
    return s;
}

// THE LOGO LANE'S LAYOUT (render.h's logo lane block, every rule there) —
// ONE FUNCTION for the painter and for the tick's damage
// (logo_legend_extent), so the two cannot disagree: the lane is the band's
// first logo_lane_h_px rows; the legend's runs on the body face's baseline
// with the cap band centered on the lane; the clock run's last ink on the
// program ring's outer right column; the glyph cell one space advance left
// of the clock run's first ink, its right edge rounded to a whole column,
// its rows centered on the cap's center (the cap rounded to whole rows); the
// percent run's last ink one space advance left of the cell; the extent the
// lane's rows from the first ink column through the ring's right column and
// the shadow's offset past it (2026-10-28).
namespace {
struct LogoLegendLayout {
    GuiRect lane{0, 0, 0, 0};
    text_shape::SharedRun clock, percent;   // percent null when empty
    double clock_x = 0.0, percent_x = 0.0, baseline = 0.0;
    GuiRect cell{0, 0, 0, 0};
    int cap_center = 0;   // the cap's center row
    GuiRect extent{0, 0, 0, 0};
};
GuiRect logo_lane_rect(const AppState& app) {
    const GuiRect band = top_icon_row_area(app);
    return GuiRect{band.x, band.y, band.w, std::min(band.h, logo_lane_h_px())};
}
LogoLegendLayout logo_legend_layout(const AppState& app,
                                    const GuiLogoLegend& legend) {
    LogoLegendLayout l;
    l.lane = logo_lane_rect(app);
    const ProgramRingColumns ring = program_ring_columns(app);
    const GuiFont body = gui_font(GuiFace::Body);
    l.baseline = redesign_baseline(body, static_cast<double>(l.lane.y),
                                   static_cast<double>(l.lane.h));
    const double space = text_shape::memoized_run(body, " ")->width_px;
    l.clock = text_shape::memoized_run(body, legend.clock);
    l.clock_x = static_cast<double>(ring.end) - text_shape::ink_right_px(*l.clock);
    const int cell_w = skin_px(kLogoGlyphCellWAt300, 1);
    const int cell_h = skin_px(kLogoGlyphCellHAt300, 1);
    const int cell_right = static_cast<int>(std::nearbyint(
        l.clock_x + text_shape::ink_left_px(*l.clock) - space));
    const int cap_rows = std::max(
        1, static_cast<int>(std::nearbyint(gui_font_cap_px(body))));
    const int base_row = static_cast<int>(std::nearbyint(l.baseline));
    l.cap_center = base_row - (cap_rows + 1) / 2;
    l.cell = GuiRect{cell_right - cell_w, l.cap_center - (cell_h - 1) / 2,
                     cell_w, cell_h};
    int first = l.cell.x;
    if (!legend.percent.empty()) {
        l.percent = text_shape::memoized_run(body, legend.percent);
        l.percent_x = static_cast<double>(l.cell.x) - space -
                      text_shape::ink_right_px(*l.percent);
        first = std::min(first, static_cast<int>(std::floor(
            l.percent_x + text_shape::ink_left_px(*l.percent))));
    }
    // THE EXTENT COVERS THE SHADOW (paint_logo_lane, 2026-10-28): the
    // clock's shadow reaches cue_label_shadow_offset_px past the ring's
    // column, so the columns run that far further (still inside the band,
    // which spans the window: the ring's column margin is wider). Its rows
    // stay the lane's, no clamp: the lowest ink plus the shadow stands well
    // inside it — the face's descent (gui_font_descent_px, about .21 em, an
    // upper bound for the clock's '|', the legend's one descender) and the
    // offset under the cap-centered baseline end about 51 rows down the
    // 72-row lane at 300 %, 34 of 48 at 200 %, 22 of 33 at 138 %; the
    // glyph cell, one row below the baseline, less.
    l.extent = GuiRect{first, l.lane.y,
                       std::max(0, ring.end + cue_label_shadow_offset_px() -
                                       first),
                       l.lane.h};
    return l;
}
} // namespace

GuiRect logo_legend_extent(const AppState& app, const GuiLogoLegend& legend) {
    return logo_legend_layout(app, legend).extent;
}

void GuiPaintHandler::paint_logo_lane(cairo_t* cr, const GuiRect& band) {
    // THE LOGO LANE (render.h's logo lane block, every length skin_px):
    // the quaver, the brand, the legend and its glyph, on the band's ground
    // paint_icon_row already put down — nothing else on the lane. EVERY
    // ONE WEARS THE CUE LABEL'S SHADOW (materials.h's INKS, architect
    // 2026-10-28): `ce_cue_shadow` one label quantum right and one down
    // (cue_label_shadow_offset_px), painted first, the bone over it — the
    // words through show_ce_label, the quaver through
    // icons::draw_boxed_shadow, the glyph its own path filled twice. The
    // shadow is ink, never extent: no placement below reads it. Each item's
    // shadow lands in the gap after it, never on a neighbor's bone (the
    // brand gap 10 and a space advance against the offset 3 at 300 %; 5,
    // about 3 and 1 at 138 %).
    const GuiRect lane{band.x, band.y, band.w,
                       std::min(band.h, logo_lane_h_px())};
    if (lane.w <= 0 || lane.h <= 0) return;
    const ProgramRingColumns ring = program_ring_columns(app);
    const GuiColor bone = hex(kBoneInk);
    const GuiColor shadow = palette().ce_cue_shadow;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);

    // THE QUAVER: its first ink column on the ring's outer left column and
    // its ink rows centered on the lane, both off the raster's own alpha
    // (icons::boxed_ink), in the file's own bone over its shadow.
    const int box_h = icons::icon_box_px(kLogoQuaverInkAt300);
    const icons::BoxedInk ink = icons::boxed_ink(icons::Icon::Quaver, box_h);
    const int box_x = ring.x - ink.left;
    const int box_y =
        lane.y +
        static_cast<int>(std::nearbyint((lane.h - (ink.bottom - ink.top)) * 0.5)) -
        ink.top;
    icons::draw_boxed_shadow(cr, icons::Icon::Quaver, box_x, box_y, box_h,
                             shadow);
    icons::draw_boxed(cr, icons::Icon::Quaver, box_x, box_y, box_h);

    // THE BRAND, a constant: its first ink the gap past the quaver's last
    // ink column, its cap band centered on the lane, the bone over the
    // shadow.
    {
        const GuiFont bold = gui_font(GuiFace::Bold);
        const text_shape::SharedRun brand =
            text_shape::memoized_run(bold, kLogoBrand);
        const double x = static_cast<double>(box_x + ink.right +
                                             skin_px(kLogoBrandGapAt300)) -
                         text_shape::ink_left_px(*brand);
        show_ce_label(cr, *brand, x,
                      redesign_baseline(bold, static_cast<double>(lane.y),
                                        static_cast<double>(lane.h)));
    }

    // THE LEGEND (logo_legend_layout): the clock run, the glyph in its cell,
    // the percent run, each the bone over the shadow — published as painted
    // for the tick's damage (the extent covers the shadow).
    const LogoLegendLayout l = logo_legend_layout(app, app.logo_legend);
    app.logo_legend_rect = l.extent;
    show_ce_label(cr, *l.clock, l.clock_x, l.baseline);
    if (l.percent) show_ce_label(cr, *l.percent, l.percent_x, l.baseline);

    // THE GLYPH, artwork filled with cairo's own antialiasing: ONE PATH
    // traced twice, first one label quantum right and down in the shadow,
    // then in place in the bone.
    const double cx = static_cast<double>(l.cell.x);
    const double cy = static_cast<double>(l.cell.y);
    const double cw = static_cast<double>(l.cell.w);
    const double ch = static_cast<double>(l.cell.h);
    const auto trace_glyph = [&] {
        cairo_new_path(cr);
        if (app.logo_legend.glyph == GuiLogoGlyph::Bolt) {
            // THE BOLT: kLogoBolt over the cell's unit box.
            cairo_move_to(cr, cx + kLogoBolt[0].x * cw,
                          cy + kLogoBolt[0].y * ch);
            for (std::size_t i = 1; i < std::size(kLogoBolt); ++i)
                cairo_line_to(cr, cx + kLogoBolt[i].x * cw,
                              cy + kLogoBolt[i].y * ch);
            cairo_close_path(cr);
        } else {
            // THE BATTERY: its box centered in the cell (the spare columns
            // halved, the odd one on the right — the floor;
            // the rows centered on the cap's center), the nub up.
            const int bw = skin_px(kLogoBatteryWAt300, 1);
            const int bh = skin_px(kLogoBatteryHAt300, 1);
            const double bx =
                static_cast<double>(l.cell.x + (l.cell.w - bw) / 2);
            const double by = static_cast<double>(l.cap_center - (bh - 1) / 2);
            const double w = static_cast<double>(bw);
            const double h = static_cast<double>(bh);
            const double nub = kLogoBatteryNubH * h;
            cairo_rectangle(cr, bx + kLogoBatteryNubX0 * w, by,
                            (kLogoBatteryNubX1 - kLogoBatteryNubX0) * w,
                            nub + static_cast<double>(hairline_px()));
            const double x0 = bx + kLogoBatteryBodyX0 * w;
            const double x1 = bx + kLogoBatteryBodyX1 * w;
            const double y0 = by + nub;
            const double y1 = by + h;
            const double r =
                static_cast<double>(skin_px(kLogoBatteryRadiusAt300, 1));
            constexpr double kPi = 3.14159265358979323846;
            cairo_new_sub_path(cr);
            cairo_arc(cr, x1 - r, y0 + r, r, -kPi / 2, 0.0);
            cairo_arc(cr, x1 - r, y1 - r, r, 0.0, kPi / 2);
            cairo_arc(cr, x0 + r, y1 - r, r, kPi / 2, kPi);
            cairo_arc(cr, x0 + r, y0 + r, r, kPi, 3 * kPi / 2);
            cairo_close_path(cr);
        }
    };
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    {
        // The path traced under the translation lands t right and t down.
        const double t = static_cast<double>(cue_label_shadow_offset_px());
        cairo_save(cr);
        cairo_translate(cr, t, t);
        trace_glyph();
        set_palette_source(cr, shadow);
        cairo_fill(cr);
        cairo_restore(cr);
    }
    trace_glyph();
    set_palette_source(cr, bone);
    cairo_fill(cr);

    cairo_restore(cr);
}

void GuiPaintHandler::paint_icon_row(cairo_t* cr) {
    // THE ICON ROW (top lane 0, at the window's top; row 4 of the
    // redesign; the window's width since 2026-10-21): THE TOOLBAR BAND (the
    // roster block above), the simple layout since 2026-10-22 — the strip
    // pad, the cases, the strip pad on the plain wood, the left members in
    // THREE RUNS and the right five in ONE — TWENTY-FIVE members outside the
    // `h` view and EIGHTEEN inside it (2026-10-28, the NOISE lamp closing the
    // third run, standing in the view), RE-COUNTED off the roster enum, the
    // painter's tables and the run leaders rather than adjusted: OPEN
    // PROJECT at the row's head (architect 2026-10-07, its Revert twin with
    // it) with SAVE, UNDO, REDO, COPY (2026-10-25) and RENDER behind it in
    // the first run; ZOOM IN, ZOOM OUT, ZOOM TO FIT, THE WAVEFORM
    // MAGNIFICATION LAMP and FOLLOW (the viewport
    // class), then THE ITERATION GROUP (the BPM opener, grid iteration mode
    // and FLATTEN) in the second; the RENDER-ENTRY group (Listen, the
    // READ-ONLY toggle, SETTINGS, ENABLE TOOLTIPS, since 2026-10-26 the
    // SCORE key and since 2026-10-28 the NOISE lamp) the third — then,
    // RIGHT-ANCHORED, THE HISTORY OPENER attached to THE VIEW PAIRS
    // (kIconRowRightRun). IN THE `h` VIEW the history group's four icon-row
    // companions stand in two groups' slots — Revert and Load in Place in
    // the toolbar group behind Save, Cumulative and the walk lamp in the
    // iteration group's (kIconRowHistoryStandIns, the swap's one owner).
    //
    // ONE MODE SWAP AND ONE MODE HIDE (architect 2026-10-05 and 2026-10-07,
    // reversing 2026-08-14's "no more hiding/showing icons in top icon row"
    // for the `h` view alone): the history stand-ins replace the two groups
    // the view refuses whole (the companions publish empty rects outside the
    // view, the replaced members inside it), and inside the view every other
    // member whose chord it refuses in EVERY session is not painted
    // (history_mode_hides_button, input_pointer.cpp) — Follow behind Zoom
    // In, Zoom Out, Zoom to Fit and the magnification lamp (which the view
    // admits since
    // 2026-10-12, architect: he wants to magnify while walking history), and
    // the padlock and Settings between Play Renders (admitted 2026-10-25)
    // and Enable Tooltips, and the Score key after it (2026-10-26), the NOISE
    // lamp behind it standing (admitted 2026-10-28) — its
    // neighbors
    // closing up and its run standing
    // while any member does. What the view refuses only BY STATE (Save with
    // nothing to commit, Revert with no subject, Load in Place over an empty
    // walk) and what any other mode refuses wears the DEAD FACE, and a window
    // too narrow for the row covers members otherwise, under the view pairs'
    // overflow rule — a width, never a state.
    //
    // THE WIDTH MATH, RE-DERIVED for the simple layout (2026-10-22; the case
    // Cool Edit's own 23 round its 20-W seat; the ends the ring's since
    // 2026-10-23): in Windows px, from the program ring's outer left column,
    // the runs, each 23n with 15 between two; the right run 5 · 23 = 115
    // ending at the ring's right edge. Outside the `h` view the LEFT WALK is
    // twenty members in three runs (re-derived 2026-10-28, the NOISE lamp
    // closing the third),
    //   20·23 + 2·15 = 490,
    // and inside it thirteen in three (Open, Save, Revert, Load in Place |
    // Zoom In, Zoom Out, Zoom to Fit, the lamp, Cumulative, Session | Play
    // Renders, Enable Tooltips, Noise — re-derived 2026-10-28),
    // 13·23 + 2·15 = 329;
    // with one run gap of wood before the right run the band holds every
    // run at
    //   490 + 15 + 115 = 620
    // Windows px outside the view (459 inside it) — the ring's 2258 device
    // px on the tablet at 300 % (752⅔ W) with 398 px (132⅔ W) of ground
    // between the left runs and the right run. IN DEVICE PX, off the painted
    // walk (each case its rounded parts, the gap its own scaled_px, the ends
    // program_ring_columns'; re-derived 2026-10-28, the ring's columns the
    // canvas's frame hairlines, and again that day with the NOISE lamp): at
    // the tablet's 300 % (cases 69, the gap 45, the ring 23 .. 2281) the left
    // runs on x 23..1492, the right run 1936..2280; at the laptop's 138 %
    // (cases 31, the gap 21, the ring 15 .. 1905) the left runs on x 15..676,
    // the right run 1750..1904. A roster move restates these numbers.
    //
    // NO FOCUS SWAP HERE: the ground has one value focused and unfocused
    // (render.h's palette says so), and so has the menu row's.
    //
    // THE FACES ARE THE PROGRAM'S CASE (paint_roster_case, above, where the
    // faces stand): the rest face, Cool Edit's one down face for a press and
    // for a CHECKED button — the live fact its chord flips
    // (redesign_button_selected), the view pairs' lit members among them —
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

    // THE LANE IS THE BRUSHED GROUND (materials.h's GROUND, 2026-10-23): the
    // band's ground the panel as drawn under every run and between them,
    // NO LINE on either device: the logo lane's ground is the bake's alone
    // (the laptop's caption line on its first row went with the caption,
    // 2026-10-24; labwc's title bar stands above the surface). Its upper pad THE LOGO
    // LANE (paint_logo_lane, below the ground); its keys blank
    // (paint_roster_case), their legends under them in the bottom pad.
    cairo_save(cr);
    const GuiMaterialScope ground(GuiMaterialRegion::Ground);
    {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        paint_cell_rect(cr, lane, palette().face);
        cairo_restore(cr);
    }
    paint_logo_lane(cr, lane);

    // THE KEY AND ITS LEGEND (2026-10-23): the blank key a composite of its
    // rounded parts (render.h's program block), its legend under it in the
    // bottom pad (key_legend_layout, which names the legend's face; this row
    // selects none of its own) — THE BAND'S SEATS the one layout the
    // materials' key patches read too (icon_row_seats, above, where the
    // right run's place and the left runs' limit are stated).
    const IconRowSeats seats = icon_row_seats(app, lane);
    const int btn_w    = seats.btn_w;
    const int btn_h    = seats.btn_h;
    const int btn_y = seats.btn_y;
    const int left_limit = seats.left_limit;

    // ONE MEMBER'S PAINT, shared by both walks: publish what is painted, then
    // the face and the glyph at the member's full box — the clip, when
    // the overflow rule has set one, cuts the pixels and the published rect
    // says exactly which survived.
    // THE PAINTED EXTENT (key_legend_layout: the key and its legend) is
    // what the publication's coverage test and the repaint gate read, cut
    // by `clip_x2` — the left runs' limit, the column the overflow clip
    // ends at (the right run paints whole: INT_MAX).
    const auto paint_member = [&](const IconRowDef& def, int bx,
                                  const GuiRect& published, int clip_x2) {
        const GuiRect key{bx, btn_y, btn_w, btn_h};
        // THE LEGEND NAMES THE NEXT PRESS'S ACT (2026-10-25,
        // redesign_button_legend's stateful overload), stashed with the
        // extent it paints over (publish_button_face).
        const char* legend = redesign_button_legend(app, audio, def.id);
        const GuiRect extent = key_legend_layout(key, legend).extent;
        const int painted_x2 = std::min(extent.x + extent.w, clip_x2);
        const GuiRect painted{extent.x, extent.y,
                              std::max(0, painted_x2 - extent.x), extent.h};
        AppState::RedesignButtonFace& face = publish_button_face(
            cr, app, audio, playback, target_render, def.id, published,
            painted, legend);

        // THE SIXTH FACE, WORN FOR TWO MODES: the `h` history view (architect
        // 2026-08-04) and, since 2026-08-15, the per-tab READ-ONLY LOCK. Both
        // are ruled EXCEPTIONS to the never-grey rule above and both are MODE
        // statements — that is what earns them the face, a refusal alone never
        // does. EVERY button standing in this row that the view refuses BY
        // STATE wears it — the MOMENT-STATE Save (an empty head delta or a
        // checkpoint in flight), Revert and Load in Place on their session
        // terms (below), the walk lamp on a visit with no clone (its own
        // arm, redesign_button_enabled); what the view refuses in EVERY session does not
        // stand in it since 2026-10-07 (history_mode_hides_button: the zoom
        // group behind Zoom In, Zoom Out, Zoom to Fit and the magnification
        // lamp, the read-only toggle, Settings, the Score key), and the two
        // groups it consumes WHOLE,
        // Undo's and the iteration group, have the history companions
        // standing in their slots (since 2026-10-05; the iteration group's
        // again since 2026-10-26, kIconRowHistoryStandIns). The view pairs, Zoom In,
        // Zoom Out, Zoom to Fit, the magnification lamp, the tooltip lamp, the
        // NOISE lamp (2026-10-28) and the history opener stay live, as do the history
        // COMPANIONS — each
        // on the derivation's own answer, which is not one reason but two:
        // bare `g`, `u`, `,` and `.` are the mode's own VOCABULARY and answer live
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
        // THE DEAD FACE IS THE LEGEND'S ALONE (architect 2026-10-02, the AB
        // set; the hi-fi skin 2026-10-23): dimmed (kDimInk,
        // paint_key_legend), the key the face its state gives
        // (paint_roster_case). A dead CHECKED button (the grid-iterations
        // lamp on a locked tab, say) stays down with its LED lit: the mode
        // cannot change that state, so hiding it would be a lie, and the
        // dimmed legend says "true, but not yours right now".
        // The press face is gated on the live bit rather than trusted: the
        // claim never records a press on a disabled button, but a button can
        // go dead UNDER a held press with no pointer event to refresh it.
        // A case the clip does not reach is skipped, its face published
        // above (clip_meets) — THE CASE AND ITS LEGEND (2026-10-23): the
        // gate reads the painted extent, so a damage confined to the bottom
        // pad, which the ground erases, still reprints the legend.
        if (!clip_meets(cr, painted)) return;
        const bool pressed =
            face.enabled && redesign_button_pressed_face(app, def.id);
        paint_roster_case(cr, key, face.selected, pressed);

        // THE HI-FI SKIN (architect 2026-10-23): THE KEY IS BLANK — nothing
        // stands on its face — and THE LEGEND stands UNDER it, the roster's legend column
        // (redesign_button_legend's stateful overload: the act the next
        // press runs, a toggle keeping its one print, 2026-10-25), dimmed
        // when the button is dead (paint_key_legend's one seat). The stashed
        // glyph bit (redesign_button_glyph) is still published so the
        // tooltip's drift is caught.
        paint_key_legend(cr, key, legend, face.enabled);
    };

    // THE LEFT RUNS, IN TWO PASSES: the walk (icon_row_walk, above) lays
    // out every standing member into its run first, and the runs are then
    // painted. Everything paints under the clip that ends at the left runs'
    // limit, and each member PUBLISHES ITS RECT CUT AT THAT COLUMN — whole at
    // every width the band fits (the laptop's and the tablet's included),
    // its painted columns alone when the limit cuts it, and an empty rect
    // when it lies wholly past it, which contains no point and refreshes its
    // bits unconditionally (publish_button_face).
    // A MEMBER THAT DOES NOT STAND in this state still publishes: an empty
    // rect, which contains no point and refreshes its bits unconditionally
    // (publish_button_face), so neither a press nor the drift comparator
    // (main.cpp's tick) reads a rect or a bit from the other state's row.
    std::vector<std::vector<IconRowDef>> runs;
    std::vector<RedesignButton>          absent;
    icon_row_walk(app.history_mode.active, runs, absent);
    for (const RedesignButton id : absent)
        (void)publish_button_face(cr, app, audio, playback, target_render, id,
                                  GuiRect{});

    cairo_save(cr);
    cairo_rectangle(cr, lane.x, lane.y,
                    std::max(0, left_limit - lane.x), lane.h);
    cairo_clip(cr);
    int x = seats.left_x;
    for (const std::vector<IconRowDef>& run : runs) {
        const int n = static_cast<int>(run.size());
        for (int i = 0; i < n; ++i) {
            const int bx = seats.case_x(x, i);
            const int shown_w = std::clamp(left_limit - bx, 0, btn_w);
            paint_member(run[static_cast<std::size_t>(i)], bx,
                         GuiRect{bx, btn_y, shown_w, btn_h}, left_limit);
        }
        x = seats.next_run_x(x, n);
    }
    cairo_restore(cr);

    // THE RIGHT RUN, PAINTED LAST AND WHOLE, each member publishing the full
    // case it paints: the history opener at one x in both states, Source /
    // Target and Warp / Phase Reset abutting behind it (kIconRowRightRun).
    // NONE IS EVER HIDDEN (history_mode_hides_button): bare `h` is the
    // view's own vocabulary and bare 1 / 2 / 3 / 4 its admitted selectors,
    // so the hide does not reach this run.
    for (std::size_t i = 0; i < std::size(kIconRowRightRun); ++i) {
        const int bx = seats.case_x(seats.right_x, static_cast<int>(i));
        paint_member(kIconRowRightRun[i], bx, GuiRect{bx, btn_y, btn_w, btn_h},
                     std::numeric_limits<int>::max());
    }

    cairo_restore(cr);
}

// (THE TAB STRIP'S PAINTER — paint_tab_strip, its two roster tabs and their
// labels kTabStripLabels, 2026-10-18 — IS REMOVED with the strip, architect
// 2026-10-22; the active tab's name is row 8's, beside the clock.)

// THE BOTTOM ROW'S BUTTON CLUSTER AND CLOCK — THE UNIFIED BOTTOM ROW (rows 8
// and 9 merged 2026-08-12): permanent on every host — ordinary
// mouse-clickable buttons, no touch mode, no flag, no detection. LEFT TO
// RIGHT (architect 2026-09-29, for the right-handed tablet: the most-used
// buttons at the bottom right):
//
//   THE CLOCK in Cool Edit's dark TIME FIELD in its ring (the simple
//   layout, 2026-10-22, program_spec.h; the field's shape, face and fixed
//   width at kTimeShape's block below) — with THE ACTIVE TAB'S NAME on the
//   wood past it and THE STATE LINE past the name in the panel label's
//   shadowed pair, clipped short of the right block (architect 2026-10-03
//   for the line);
//
//   and, PACKED AGAINST THE RIGHT INSET, four groups as plain runs, the run
//   gap between two (architect 2026-10-22; four since 2026-10-24):
//   THE SKIPS (kTransportSkipGroup, back 2026-10-24) — GO TO START (bare
//   Home) and GO TO END (bare End), their shifted presses the whole piece's
//   ends;
//   THE MARKER VERBS (kMarkerVerbGroup) — drop (bare `s`), delete (Delete),
//   disable (Ctrl+D), inherit (Ctrl+N), JUMP TO DEFINING MARKER (Ctrl+J,
//   since 2026-09-29), OPEN TEXT EDITOR (bare Return, since 2026-10-09),
//   ADD TO SELECTION (bare `k`, the sticky ctrl, lit while the mode stands)
//   and TOGGLE VALUE DRAG (bare `[`, since 2026-10-11, lit while armed — the
//   row's two lamps); COPY PHASE RESETS stood between the two 2026-10-24 to
//   2026-10-25 and is the band's first run's since (kIconRowButtons). The
//   four verbs, Open Text Editor and Toggle Value Drag
//   are the row's resting greys on a locked tab; the jump and Add to
//   selection are not, both being navigation (EDIT FLAG and COPY RESOLVED VALUE stood
//   between Toggle inherit and Add to selection until 2026-09-29; Copy was
//   the icon row's from then until 2026-10-14, its act now the jump's shift
//   twin, and Edit Flag was deleted);
//   THE STEP AND THE WALK (kTransportStepWalkGroup, architect 2026-10-18) —
//   DOWN and UP (bare Down/Up), PREVIOUS MARKER (Shift+Tab, its ctrl press
//   the paired march) and NEXT MARKER (Tab, its shifted press Shift+Tab);
//   THE NUDGE AND PLAY (kTransportNudgePlayGroup, architect 2026-10-18) —
//   SWITCH TAB (Ctrl+Tab, its shifted press the paired march), CENTER (bare
//   `c`, down from the icon row since 2026-09-29), LEFT and RIGHT (bare
//   Left/Right). THE FOUR ARROWS, a single line and not a d-pad until
//   2026-10-18 and two pairs since, are THE ROW'S HOLD GESTURE: held past
//   the hold beat they repeat their own chords at the compositor's
//   key-repeat rate (the record at the arrows' chord-table rows,
//   input_pointer.cpp). The pressed interior is the ordinary arm's, and the
//   hold has no cue of its own — and, THE LAST GROUP'S LAST CASE CLOSING THE
//   ROW, THE ONE PLAY/STOP BUTTON (bare Space, whose LEGEND and TOOLTIP swap
//   on the live audition bit; Shift+Space the A/B audition). The last button's
//   right edge stands on the program ring's outer right column
//   (bottom_row_seats, 2026-10-23).
//
// EVERY BUTTON GREYS WHERE ITS PRESS WOULD BE A NO-OP (the truthful-buttons
// ruling, redesign_button_enabled), the `h` view's derived partition on top.
// TWO MODE TERMS. THE HIDE (architect 2026-10-07): in the `h` view a member whose chord
// the view refuses in every session is NOT PAINTED (history_mode_hides_button,
// input_pointer.cpp) — Play / Stop, Up and Down, the four verbs, Jump to
// Defining Marker, Open Text Editor and Toggle Value
// Drag — and publishes an empty rect, the block closing up from
// the right margin and the state line taking the room; the row in there is
// GO TO START and GO TO END (the mode's own absolute jumps), ADD TO
// SELECTION alone, the walk pair, and SWITCH TAB, CENTER, OLDER and NEWER:
// nine cases in four groups (2026-10-24; seven in three while the skips
// were off the row, 2026-10-12 to 2026-10-24). THE STAND-IN (architect 2026-10-11,
// kTransportHistoryStandIns): in the view Older / Newer take the seats Left
// and Right hold outside it, the nudge pair publishing empty rects (Up and
// Down hidden), and outside it the two publish empty rects. THE STAND-INS DO NOT INHERIT THE
// ARROWS' HOLD-REPEAT (architect 2026-10-11; kToolbarChords, input_pointer.cpp,
// records why). Otherwise no member publishes a
// zero rect except under a modal.
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
// drift apart — on THE ROW'S OWN WOOD, the strip pad above the case and
// below it (the simple layout, 2026-10-22; Cool Edit's row 8 under its dock
// bar, 4 W above and 5 below, 2026-10-09 to then). (Row 8's kdenlive
// transport metrics, its 26-px boxes and
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
// (history_mode_hides_button) — while the LEFT / RIGHT chords are the mode's
// own playhead step since 2026-09-26 (admitted; the hide predicate leaves
// them be, kTransportHistoryStandIns seating Older / Newer in their buttons'
// places in the view, the painted, state-greyed pair there); ADD TO SELECTION STAYS LIT SINCE 2026-09-17, bare `k`
// being on that mode's allowlist now and the lamp producing the view's own
// multi-selection; THE WALK PAIR, SWITCH TAB AND CENTER stay lit, Tab/Shift+Tab being the
// mode's diff-flag cycle, `c` its centring and Ctrl+Tab on its allowlist
// (Home / End, its own absolute jumps, are keys alone since 2026-10-12, the
// two skip buttons gone; Play / Stop is the transport's one button and is not
// painted in the view). PREVIOUS
// MARKER greys only under a lit grid iterations with no reverse step left
// (its twin, the march, refusing there too). Outside the view the four VERBS
// and OPEN TEXT EDITOR grey on a locked tab, their own
// gate — JUMP TO DEFINING MARKER, seated among them, does NOT, its chord being
// navigation the lock admits — and since 2026-08-30 EVERY MEMBER BUT PLAY /
// STOP (the transport's one button since the skips left, 2026-10-12; its own
// launch and A/B predicates) AND ADD TO SELECTION greys on the
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
// the LEFT and RIGHT chords are the mode's own playhead step, live, but
// kTransportHistoryStandIns seats Older / Newer in their buttons' places
// there, the painted, state-greyed pair; UP and DOWN, whose bare chords the mode consumes, are not painted there
// since 2026-10-07 (history_mode_hides_button). Those are the partition's
// own answers and no part of the always-on ruling, which is about the
// RESTING face.)
// THE LANE'S GROUND IS THE PLAIN WOOD (the simple layout, 2026-10-22): the
// lane's chrome is paint_bottom_strip's, which lays it and calls the body
// below onto it.

// THE ROW AUTHORS NO METRIC OF ITS OWN (program_spec.h owns every one): the
// cases and glyphs are the band's, the strip pad the band's, the run gap
// the program's (2026-10-22; the bars and their pads 2026-10-21, Cool
// Edit's grippers and end bars before), and the row's ends the program
// ring's outer columns, flush like the band's (bottom_row_seats,
// 2026-10-23). The render
// player's modal row in this lane takes the same case at the same seat
// (modal_controls_seats) and stands its items its own eight W apart on the
// row's wood (paint_modal_dialog's player branch).

// The painter's half of the row's roster: four groups, all in the right
// block and painted left to right in the order the body lays them (the two
// skips since 2026-10-24, the marker group, the step and walk, the nudge and
// play with Play last; the `h` view's stand-ins seated in the last); each
// table is declared beside its reasons.
// The press claim's chord table (input_pointer.cpp) is the other half; both
// key off the same ids.
struct TransportRowDef {
    RedesignButton id;
};
// THE SINGLE-MARKER VERBS (architect 2026-08-18, "move drop/delete/disable/
// toggle inherit to bottom right row"), the RIGHT BLOCK'S MARKER GROUP: drop
// (bare `s`), delete (`Delete`), the disable toggle (`Ctrl+D`: a disabled
// marker is one the render does not hear) and inherit/collapse (`Ctrl+N` — a
// pass marker links its tempo to its neighbor). They opened their own
// separator-led group in the ICON ROW from 2026-08-12 until this move.
//
// THE GROUP IS FIVE since later the same day, the architect seating ADD TO
// SELECTION himself — "add group selection icon ('Add to Selection') after
// toggle inherit, before the separator". It is the group's one MODE (bare `k`)
// and therefore wears the lit fill; the
// four verbs above are acts that complete. (Toggle Value Drag, the group's
// second lamp since 2026-10-11, is the other member that does.)
//
// It was SEVEN from 2026-09-19 until 2026-09-29 — the four verbs, the EDIT
// FLAG BUTTON (2026-08-27), COPY RESOLVED VALUE (2026-08-29) and ADD TO
// SELECTION; the rest of the succession (the Marker Measure, the value drag
// lamp, the marker magnification, the Flatten button's hours) is in git
// history.
//
// THE GROUP IS SIX SINCE 2026-09-29 EVENING (architect): EDIT FLAG and COPY
// RESOLVED VALUE went up to the icon row (Edit Flag was deleted later that
// evening), and JUMP TO DEFINING MARKER — Ctrl+J,
// JumpToDefiningMarker's jump arc — took the seat
// after Toggle Inherit, Add to Selection closing the group behind it. It is an
// act, wears no lamp, and SINCE 2026-10-14 CARRIES THE COPY AS ITS SHIFT TWIN
// (architect: "it's a marker action and the marker row is the bottom row"):
// its shift-click and long press are Ctrl+Shift+J, Copy Resolved Value, whose
// icon-row button left that day. It greys where the jump and the copy both
// refuse (the twin rule at its enabled arm) and in the `h` view; neither lock
// greys it, neither act authoring anything.
//
// THE GROUP IS SEVEN SINCE 2026-10-09 (architect: "resurrect the Open Text
// Editor button. It should go between jump to defining marker and toggle add
// to selection"): OPEN TEXT EDITOR — bare Return — the glass's road to Return with no editor standing. An act that opens an
// editor, no lamp; it greys where Return would open nothing and on a locked
// tab, and the `h` view does not paint it (its roster entry, app_state.h).
//
// THE GROUP IS EIGHT SINCE 2026-10-11 (architect ~00:40): TOGGLE VALUE DRAG —
// bare `[` — IMMEDIATELY RIGHT
// OF ADD TO SELECTION, the group's second lamp and its last member ("arm Add
// to Selection, pick several flags, then arm this and drag them"). A
// one-shot: the drag it arms puts it out; under grid iterations permanently
// lit and grey (2026-10-11 ~01:45; its roster entry, app_state.h).
//
// THE GROUP WAS NINE 2026-10-24 TO 2026-10-25: COPY PHASE RESETS stood
// between Add to Selection and Toggle Value Drag, and went up to the band's
// first run between Redo and Render (architect 2026-10-25: "the copy right
// now is pushing all the other buttons one column to the left, which is
// further away from my pen hand"; kIconRowButtons). EIGHT again since.
//
// (THE FLATTEN BUTTON stood between Toggle inherit and Edit flag for the hours
// of 2026-09-19 and went up to the ICON ROW'S ITERATION GROUP the same day
// (architect).)
constexpr TransportRowDef kMarkerVerbGroup[] = {
    {RedesignButton::IconMarkerDrop},
    {RedesignButton::IconMarkerDelete},
    {RedesignButton::IconMarkerDisable},
    {RedesignButton::IconMarkerInherit},
    {RedesignButton::IconJumpToDefiningMarker},
    {RedesignButton::IconOpenTextEditor},
    {RedesignButton::IconAddToSelection},
    {RedesignButton::IconValueDrag},
};
// THE SKIP GROUP (architect 2026-10-24: the two skip buttons back as their
// own run at the left of Drop), THE RIGHT BLOCK'S FIRST: GO TO START (bare
// Home, legend START) and GO TO END (bare End, legend END), the trim bounds'
// jump, their shifted press and long press the whole piece's ends
// (Shift+Home / Shift+End). The run gap stands between it and the marker
// verbs like every group's. Both stand in the `h` view, where every jump is
// the piece's end (history_mode_owns_key). (The pair stood in row 8's
// transport group with Play until 2026-10-12, when the architect took it out
// of the row, Home and End keeping their keys.)
constexpr TransportRowDef kTransportSkipGroup[] = {
    {RedesignButton::TransportSkipBack},
    {RedesignButton::TransportSkipForward},
};
// THE STEP-AND-WALK GROUP (architect 2026-10-18 ~21:40, regrouping row 8's
// two right-hand groups: "Next marker, previous marker, up, down, separator",
// read right to left), the right block's second group between the verbs and
// the nudge-and-play group, in this order: DOWN and UP (bare Down / Up, the
// value step — its ladder and hold-repeat at their chord rows,
// input_pointer.cpp), PREVIOUS MARKER (Shift+Tab; its ctrl press is the
// paired march) and NEXT MARKER (the walk: Tab, its shifted press Shift+Tab;
// its camera the audio view's). (Until 2026-10-18 the marker-walk group was Previous Marker, Next Marker,
// Center and Switch Tab, and Down / Up led the arrow group behind it.)
// A SHIFT- OR CTRL-MODIFIED CHORD MAY HAVE ITS OWN DEDICATED BUTTON since
// 2026-09-29 (architect: "an artificial constraint; the tablet is the only
// real development surface now") — Previous Marker is Next Marker's Shift
// form and Switch Tab its Ctrl form; Previous Marker had merged into the walk
// on 2026-09-22 under the rule that it could not, and the walk's modified
// presses stay. Down / Up are the verticals here, Left / Right the
// horizontals in the last group; the vertical pair keeps the architect's
// 2026-08-14 order, down before up (which superseded the row's original vim order, h j k l).
// IN THE `h` VIEW Down and Up are hidden (history_mode_hides_button), the
// group standing as the walk pair alone.
constexpr TransportRowDef kTransportStepWalkGroup[] = {
    {RedesignButton::TransportDown},
    {RedesignButton::TransportUp},
    {RedesignButton::TransportWalkPrevious},
    {RedesignButton::TransportWalk},
};
// THE NUDGE-AND-PLAY GROUP (architect 2026-10-18 ~21:40: "play button, right
// nudge, left nudge. Center, switch tabs, separator", read right to left),
// THE ROW'S LAST: SWITCH TAB (Ctrl+Tab; its shifted press is the paired
// march), CENTER (bare `c` — down from the icon row's zoom group since
// 2026-09-29), LEFT and RIGHT (bare Left / Right, the nudge — the playhead
// step) and PLAY / STOP.
// (Until 2026-10-18 Switch Tab and Center closed the marker-walk group and
// this group was the four arrows with Play.)
//
// PLAY / STOP IS THE GROUP'S LAST MEMBER, abutting the right nudge with no
// bar or gap between them, and so closes the row at the far
// right outside the `h` view (architect 2026-10-12, revising his own ruling
// of the same morning that seated Play at the marker group's head, and
// correcting the one-member group of the hours after it: "no division
// between the Right arrow and Play"; kept by the 2026-10-18 regroup). Play /
// Stop at the far right sits at the screen's edge, where Shift+Play, the A/B
// audition, is quickest to reach; reaching across from the marker verbs to
// stop a scrub is acceptable, because at working zoom his hand moves faster
// than the scanner. (The Go to Start / Go to End pair came back on
// 2026-10-24 at the block's other end, kTransportSkipGroup.) Play and stop are
// ONE stateful button (2026-08-15): its legend prints STOP while an audition
// runs (redesign_button_legend's stateful overload — Save's and Render's own
// shape). IN THE `h` VIEW the group is Switch Tab, Center, Older and
// Newer: the nudge pair's seats are the history steps'
// (kTransportHistoryStandIns, below) and Play is hidden
// (history_mode_hides_button), so it does not stand there.
constexpr TransportRowDef kTransportNudgePlayGroup[] = {
    {RedesignButton::TransportSwitchTab},
    {RedesignButton::IconZoomOriginal},
    {RedesignButton::TransportLeft},
    {RedesignButton::TransportRight},
    {RedesignButton::TransportPlayStop},
};
// THE `h` VIEW'S STAND-INS ON THIS ROW — OLDER AND NEWER IN THE NUDGE PAIR'S
// SEATS (architect 2026-10-11 ~02:40, "Older / Newer in the arrows' seat";
// the seat restated on the 2026-10-18 regroup): A PER-SEAT SWAP — while the
// view stands Older paints in Left's seat and Newer in Right's, inside the
// nudge-and-play group, and Left and Right publish empty rects; outside the
// view the nudge pair stands and these two publish empty rects. Play, hidden
// in the view, leaves Older / Newer the row's last cases. The seat is the
// row's MOST-USED ACTION'S — stepping left / right outside the view,
// stepping older / newer in it — and the nudge pair has no seat in the view
// at all ("I never use that … the nudge was for precision placement and
// history view has no placement"). THE KEYS ARE UNCHANGED: `,` / `.` step the
// walk and bare Left / Right stay the view's playhead step
// (handle_history_mode_key) — only the buttons moved. THIS TABLE IS THE SWAP'S ONE OWNER: the painter
// seats a stand-in through transport_row_seated and publishes the absent
// side's empty rects from it.
// (The arrows' first mode twin, a kHistoryClusterGroup of four history
// companions painted at the arrows' anchor 2026-08-14..18, went back to the
// icon row with the 2026-08-18 relayout; this is a narrower swap, two for
// the two arrows the view still painted.)
struct TransportStandIn {
    RedesignButton  seat;      // the member whose seat it takes in the view
    TransportRowDef stand_in;  // what paints there while the view stands
};
constexpr TransportStandIn kTransportHistoryStandIns[] = {
    {RedesignButton::TransportLeft,
     {RedesignButton::HistoryOlder}},
    {RedesignButton::TransportRight,
     {RedesignButton::HistoryNewer}},
};
// WHAT PAINTS IN `def`'s SEAT: its stand-in while the `h` view stands, else
// the member itself.
static const TransportRowDef& transport_row_seated(const TransportRowDef& def,
                                                   bool history) {
    if (history)
        for (const TransportStandIn& s : kTransportHistoryStandIns)
            if (s.seat == def.id) return s.stand_in;
    return def;
}

// THE TIME FIELDS — ROW 8'S CLOCK AND THE RENDER PLAYER'S POSITION AND
// LENGTH, ONE FIELD SHAPE (architect 2026-10-09, the program is Cool Edit;
// THE HI-FI SKIN'S FIELD since 2026-10-23, paint_ce_time_field): BLACK
// (kTimeFieldGround) under a ONE-DEVICE-PX SUNKEN RING in the ring tones,
// dark on the top and left, light on the bottom and right, the digits THE
// BONE (E6D5A8, the clocks' own 2026-10-21 to 2026-10-23, retired: "the
// font color should not be this yellowish color") — 40 DEVICE PX TALL at
// 300 % (time_field_h_px, scaled with gui_scale), CENTRED ON THE CASE'S ROWS
// (a half-row tie toward the top, the cap rule's own), its run in THE
// PROGRAM FACE, 22 px at 300 %, in every time field (row 8's clock,
// architect 2026-10-26 ~11:30; the render player's two, ~12:10, "yes" to
// symmetry; gui_font.h's table), its cap band centred in the field by
// redesign_baseline — at 300 % the 15.1-row cap band in the 40-row field,
// its 25-row line inside it; at 138 % a 10-px em, its 6.9-row cap band in
// the 18-row field, its 11-row line inside it. The digits are
// TABULAR — Liberation Sans's every digit 0.556 em — with no kerning between
// the digits, the colon and the point — so the colon and the point stand
// still as the time runs.
//
// EACH FIELD'S WIDTH IS FIXED — ITS WIDEST STRING (architect 2026-10-05:
// "a 1 is skinnier than a 0", nothing in the field or right of it may move as
// the time runs). The reserved cell is measured from the widest string the
// field can ever show in this face — the widest digit in every digit slot of
// "DD:DD.DDD" (kTimeShape) — shaped through the one chokepoint at the
// live scale, so nothing is trusted to the face. The field is that cell plus
// THE PAD either side — 12 DEVICE PX at 300 % (time_field_pad_px; architect
// 2026-10-23, "symmetrical and much tighter"; 5 W from 2026-10-10 until
// then) — its width ceiled,
// AND NOTHING MORE: row 8's clock exactly as the render player's two fields,
// the one face, so the three fields are one width. The cell is 4.448 em
// (seven tabular digits at 0.556 em and the colon and the point at 0.278):
// at 300 % and 22 px, 97.9 device px ceiled to 98, the field 122; at
// 138 % a 10-px em, 44.5 ceiled to 45, the field 57. THE PLAYER'S ROW
// HOLDS THEM: its scrub is what the fixed terms leave (player_row_layout),
// the two fields and their gap 253 device px at 300 % and 118 at 138 %, so
// on the maximized windows the scrub's channel is 1316 device px at 300 %
// (2304 wide) and 1460 at 138 % (1920 wide).
//
// A PLAYER FIELD'S RUN IS RIGHT-ALIGNED IN ITS FIXED CELL (architect
// 2026-10-05, the period's ACID / Vegas fields): it ENDS AT THE RESERVED
// CELL'S RIGHT EDGE, so nothing moves as the time changes — the purpose of
// the rule, which the fixed cell keeps (row 8's composition below). THE
// CELL'S RIGHT EDGE IS THE FIELD'S RIGHT PAD in every time field, the field
// being the cell and its two pads (the floor's centering of 2026-10-09 went
// with the floor). ROW 8'S CLOCK IS THE TIMESTAMP ALONE (architect
// 2026-10-18: "like the media player cells" — the active tab's letter and
// the ` | ` before it left the cell; the tab's name stands on the ground after
// the field since 2026-10-22), ITS RUN STARTING AT THE CELL'S LEFT EDGE; the digits are tabular, so
// the run is the specimen's width at every time and nothing in the cell
// moves as the time runs. (The letter, `00:45.115 | A` from 2026-10-09
// ~21:20 and `A | 00:45.115` before it, stood from the tab row's deletion of
// 2026-10-01 until the strip; git history.)
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

// THE CLOCK'S RUN — the timestamp, the one string the cell shows — spelled
// once for the painter and the cell's as-painted fingerprint
// (GuiPaintHandler::clock_fingerprint).
// THE SPLIT-PLAYHEAD READ, carried over from the status line whole: track
// the SCANNER during playback (what the user hears), the CURSOR otherwise —
// the scanner's value is meaningful only while active, so the ternary takes
// the cursor at rest. The sample rate is the loaded file's and the playhead
// samples are source frames. There is no paint-site clamp: one owner caps
// the clock and it is format_timestamp (at 59:59.999 — a longer source
// truncates, the architect's ruling, recorded there).
// WITH NO PIECE THE RUN IS EMPTY and the field stands blank (architect
// 2026-10-27, the open's hold's one frame, platform.h's head: row 8's clock
// blank, not 00:00.000 — a time of no piece; the one owner
// no_audio_to_dispatch_on).
static std::string clock_time_text(const AppState& app,
                                   const GuiAudio& audio) {
    if (no_audio_to_dispatch_on(app, audio.total_frames())) return {};
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
    return format_timestamp(seconds);
}

// THE FIELD'S HEIGHT AND ITS PAD, each one element rounded once, one
// constant at every time field — THE HI-FI SKIN'S FIELD (architect 2026-10-23, "symmetrical and much
// tighter"): 40 DEVICE PX TALL AND 12 PX OF PAD
// EACH SIDE OF THE DIGITS' CELL, both authored at 300 % and scaled with
// gui_scale (skin_px; the amendment of the same night) — 18 and 6 at 138 %.
// (17 W tall with a 5-W pad, scaled_px, 2026-10-09 to 2026-10-23: Cool
// Edit's field, then the dialog field's pad; git history.)
constexpr double kTimeFieldHeightAt300 = 40.0;
constexpr double kTimeFieldPadAt300    = 12.0;
static int time_field_h_px() {
    return skin_px(kTimeFieldHeightAt300, 1);
}
static int time_field_pad_px() {
    return skin_px(kTimeFieldPadAt300, 1);
}

// THE AIR BETWEEN TWO TIME FIELDS — the render player's position and length
// (architect 2026-10-05): ACID's adjacent fields stand three Windows px of
// ground apart (columns 1136..1138 between its first two, the same capture);
// Cool Edit's Begin / End / Length fields three too (pitch 75
// round 72-W fields).
constexpr double kTimeFieldGapPx = 3.0;

// The time fields' metrics, MEMOISED ON THE SCALE — eleven tiny shaping
// passes (ten digits and the specimen) of THE PROGRAM FACE, every time
// field's (row 8's clock and the render player's two since 2026-10-26
// ~12:10), that answer the same thing on every frame until the scale
// moves — the widths are the face's own.
// Single-threaded paint state; the waveform worker never reaches this file's
// text tiers.
//
// THE CELLS ARE ADVANCES: `time_w` is the specimen's advance sum, so no
// glyph ever walks; row 8's cell and each player field's is time_w, and each
// cell is its field's damage box too.
struct TimeFieldMetrics {
    int    percent  = -1;     // the scale this was measured at
    double time_w   = 0.0;    // the widest-digit specimen's shaped width
};
static TimeFieldMetrics g_time_field_metrics;

static const TimeFieldMetrics& time_field_metrics() {
    TimeFieldMetrics& m = g_time_field_metrics;
    const GuiFont font = gui_font(GuiFace::Program);
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
    m.percent = font.percent;
    return m;
}

// THE BOTTOM ROW'S SEATS — row 8's and the modal controls row's, ONE OWNER
// (architect 2026-10-23, "shouldn't row 8 take the same rule as top
// strip?" — yes): the LEFT SEAT where a row's content starts and the RIGHT
// SEAT where its right-aligned cluster ends; CASES_RIGHT is where the row's
// last case ends; THE CASE the strip pad under the row's top
// (icon_case_top_offset_px, the band's own seat). ALL THREE ARE THE PROGRAM
// RING'S OUTER COLUMNS (render.h's program_ring_columns): the left seat its
// outer left column, the right seat and the cases' end one past its outer
// right column — the band's ends (icon_row_seats) and the modal box's
// (modal_box_rect), so both strips and the controls row stand flush with
// the ring at every scale; 23 and 2281 at 300 %, 15 and 1905 at 200 % and
// at 138 % (re-derived 2026-10-28, the ring's columns the hairlines). The
// two rows differ only in `row`: row 8's lane (bottom_row_area), or the
// controls row's, the same rows lifted onto the keyboard's band
// (modal_controls_row_rect; modal_controls_seats below reads this). Every
// BUTTON on either row is THE PROGRAM'S CASE (render.h's program block), the
// faces paint_roster_case's.
struct BottomRowSeats {
    int left_x      = 0;   // the row's first column
    int right_x     = 0;   // one past the row's right-aligned cluster
    int cases_right = 0;   // one past the row's last case
    int case_y      = 0;   // the case's top: the row's top plus the strip pad
    int case_w      = 0;   // the program's case, both rows' every button
    int case_h      = 0;
};
static BottomRowSeats bottom_row_seats(const AppState& app,
                                       const GuiRect& row) {
    const ProgramRingColumns ring = program_ring_columns(app);
    return BottomRowSeats{ring.x, ring.end, ring.end,
                          row.y + icon_case_top_offset_px(), icon_case_w_px(),
                          icon_case_h_px()};
}

// THE MODAL FAMILY'S CONTROLS ROW'S SEATS (architect 2026-10-14,
// modal_box.h): every modal's controls stand on the controls row at row 8's
// case seat, their OUTERMOST CASES AND BUTTONS FLUSH WITH THE BOX'S RING'S
// OUTER EDGES — row 8's own seats (bottom_row_seats, the one owner) on the
// controls row's rows, `keyboard_h` the stack's lift (modal_box.h's head).
static BottomRowSeats modal_controls_seats(const AppState& app,
                                           int keyboard_h) {
    return bottom_row_seats(app, modal_controls_row_rect(app, keyboard_h));
}

// ROW 8'S RIGHT BLOCK (2026-10-21: lifted out of
// paint_bottom_row_buttons_and_clock so the painter and the materials' case
// patches, program_key_seats below, read ONE layout and ONE walk; the
// numbers and the z rule at the painter's THE NUMBERS). THE FOUR GROUPS IN
// PAINTED ORDER, each read off its own table, so a box joining or leaving
// moves the block with no second edit; IN THE `h` VIEW the nudge pair's
// seats are the walk's two steps (kTransportHistoryStandIns, its one owner,
// seated through transport_row_seated). FOUR GROUPS since 2026-10-24, the
// skips first.
constexpr int kRow8GroupCount = 4;
constexpr std::span<const TransportRowDef> kRow8Groups[kRow8GroupCount] = {
    kTransportSkipGroup, kMarkerVerbGroup, kTransportStepWalkGroup,
    kTransportNudgePlayGroup};
// THE ROW'S LENGTHS (the simple layout, 2026-10-22): the run gap between
// two groups (program_spec.h's run_gap_px — the bar and its two pads it
// replaced), the case.
struct Row8Lengths {
    int gap = 0, btn_w = 0;
};
static Row8Lengths row8_lengths() {
    return Row8Lengths{program_run_gap_px(), icon_case_w_px()};
}
// THE BLOCK, MEASURED under the view's bit `history` (the live one for the
// painter, either for the seats): EACH GROUP'S STANDING MEMBERS (the hide,
// history_view_hides_button, asked of what the seat paints), whether any
// group stands, and the block's left edge. THE BLOCK'S WIDTH IS THE PAINTED
// GROUPS' (architect 2026-10-07): in the `h` view a button whose chord the
// view refuses in every session is not painted (history_mode_hides_button,
// input_pointer.cpp), so the count is of the members that stand, and a
// group stands only while it keeps a member. Outside the view nothing is
// hidden and this is every table's whole length. THE STANDING GROUPS ARE
// PLAIN RUNS (2026-10-22), the run gap between two, the last case ending at
// the row's cases_right.
struct Row8Block {
    int x = 0;                               // the block's left edge
    int standing[kRow8GroupCount] = {};
};
static bool row8_hides(bool history, const TransportRowDef& seat) {
    return history &&
           history_view_hides_button(transport_row_seated(seat, history).id);
}
static Row8Block row8_block(const AppState& app, bool history,
                            const GuiRect& lane) {
    const Row8Lengths l = row8_lengths();
    Row8Block b;
    int block_w = 0;
    int groups  = 0;
    for (int gi = 0; gi < kRow8GroupCount; ++gi) {
        for (const TransportRowDef& def : kRow8Groups[gi])
            if (!row8_hides(history, def)) ++b.standing[gi];
        if (b.standing[gi] > 0) {
            block_w += b.standing[gi] * l.btn_w;
            ++groups;
        }
    }
    if (groups > 1) block_w += (groups - 1) * l.gap;
    b.x = bottom_row_seats(app, lane).cases_right - block_w;
    return b;
}
// THE BLOCK'S ONE WALK, left to right from its left edge: per standing
// group its standing members' cases abutting (`member(def, x)`, `def` the
// seated member), the run gap before every group but the first; each hidden
// member (`hidden(def)`) wherever it falls.
template <class Member, class Hidden>
static void row8_block_walk(bool history, const Row8Block& b,
                            const Row8Lengths& l, Member&& member,
                            Hidden&& hidden) {
    int ax = b.x;
    bool any_open = false;
    for (int gi = 0; gi < kRow8GroupCount; ++gi) {
        bool group_open = false;
        for (const TransportRowDef& seat : kRow8Groups[gi]) {
            const TransportRowDef& def = transport_row_seated(seat, history);
            if (row8_hides(history, seat)) {
                hidden(def);
                continue;
            }
            if (!group_open) {
                if (any_open) ax += l.gap;
                group_open = true;
                any_open   = true;
            }
            member(def, ax);
            ax += l.btn_w;
        }
    }
}

// THE CLOCK (2026-10-22, the simple layout): the time field at the row's
// left seat (bottom_row_seats: the program ring's outer left column, the
// field's ring standing on it), its cell and its two pads
// (time_field_w_px), the field's own rect its ring (paint_ce_time_field's
// clock ring). One layout for the
// row's painter and the materials' seats.
struct Row8Clock {
    int field_x = 0, field_w = 0;
    double cell_w = 0.0;    // the digits' specimen (time_field_metrics)
};
static int time_field_w_px(double cell_w);
static Row8Clock row8_clock(const AppState& app, const GuiRect& lane) {
    Row8Clock c;
    c.cell_w  = time_field_metrics().time_w;
    c.field_x = bottom_row_seats(app, lane).left_x;
    c.field_w = time_field_w_px(c.cell_w);
    return c;
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
    const GuiRect lane = bottom_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;

    cairo_save(cr);
    // THE BRUSHED GROUND (materials.h's GROUND, 2026-10-23): the clock's
    // ring the two ring tones, one device px, the ground under the block the
    // panel as drawn; the keys blank (paint_roster_case), their legends
    // under them.
    const GuiMaterialScope ground(GuiMaterialRegion::Ground);

    // The row's seats (bottom_row_seats): the band's case at the strip pad
    // under the lane's top.
    const BottomRowSeats seats = bottom_row_seats(app, lane);
    const int btn_w     = seats.case_w;
    const int btn_h     = seats.case_h;
    const int btn_y     = seats.case_y;
    // THE GROUPS ARE PLAIN RUNS (the simple layout, 2026-10-22): the cases
    // abutting inside a group, the run gap of wood between two (row8_block;
    // the bars between groups 2026-10-21 to 2026-10-22, Cool Edit's grippers
    // and end bars before).
    const Row8Lengths lengths = row8_lengths();

    // One button, the band's face logic verbatim (paint_roster_case). THE
    // CHECKED FACE'S SUBJECT ON THIS ROW IS ADD TO SELECTION (2026-08-18),
    // the sticky-ctrl mode that closes the marker-verb group: it wears the
    // checked face while the mode stands, which is the roster's standing rule
    // for a mode. A checked button a mode greys keeps its checked face and
    // dims its legend, the band's own composition.
    const auto paint_button = [&](const TransportRowDef& def, int x) {
        const GuiRect key{x, btn_y, btn_w, btn_h};
        // The legend names the next press's act (2026-10-25, the band's
        // rule: redesign_button_legend's stateful overload).
        const char* legend = redesign_button_legend(app, audio, def.id);
        // THE PAINTED EXTENT, the key and its legend (key_legend_layout,
        // 2026-10-23): the publication's coverage and the gate below.
        const GuiRect painted = key_legend_layout(key, legend).extent;
        AppState::RedesignButtonFace& face = publish_button_face(
            cr, app, audio, playback, target_render, def.id, key, painted,
            legend);

        // A case the clip does not reach is skipped, its face published
        // above (clip_meets) — the case AND its legend, the band's gate: a
        // damage confined to the bottom pad still reprints the legend.
        if (!clip_meets(cr, painted)) return;
        const bool pressed =
            face.enabled && redesign_button_pressed_face(app, def.id);
        paint_roster_case(cr, key, face.selected, pressed);
        // THE KEY IS BLANK, ITS LEGEND UNDER IT (the band's rule, 2026-10-23:
        // the stateful legend through the one seat, dimmed when dead).
        paint_key_legend(cr, key, legend, face.enabled);
    };

    // THE ROW, LEFT TO RIGHT (architect 2026-09-29, the right-handed tablet's
    // layout: the most-used buttons at the bottom right; the simple layout
    // 2026-10-22): THE CLOCK in its ringed field at the row's left seat,
    // THE ACTIVE TAB'S NAME on the ground after it and THE STATE LINE after
    // the name (the block at the clock below) — then THE RIGHT BLOCK, packed
    // against the row's right seat (bottom_row_seats, flush with the program
    // ring's right edge), FOUR RUNS (kRow8Groups, 2026-10-24): the SKIP
    // GROUP (START | END, kTransportSkipGroup), the MARKER-VERB GROUP (its
    // eight counted off kMarkerVerbGroup), the STEP-AND-WALK GROUP (↓ ↑,
    // Previous Marker and Next Marker, kTransportStepWalkGroup) and the
    // NUDGE-AND-PLAY GROUP with Play / Stop last (Switch Tab, Center, ← →
    // Play, kTransportNudgePlayGroup; architect 2026-10-18), each a plain
    // run, the run gap between two. The whole block is measured first and laid left
    // to right from there, so one expression owns the anchor and no group
    // re-derives it.
    // ITS TWO MODE TERMS are the `h` view's: the hide (architect 2026-10-07,
    // history_mode_hides_button) — a member hidden there publishes an empty
    // rect, the block is measured over the members that stand, and a group
    // hidden whole takes its gap with it — and the stand-in (architect
    // 2026-10-11, kTransportHistoryStandIns): Older / Newer in the nudge
    // pair's seats; every other button on this row publishes a real rect on
    // every frame, except under a modal that owns the row
    // (modal_owns_bottom_row) — where the row yields whole
    // (paint_bottom_strip).
    //
    // THE NUMBERS (Windows px, re-derived 2026-10-25 with COPY gone to the
    // band; the case 23, the skips two, the verbs eight, the step-and-walk
    // group four and the nudge-and-play group five): the block is 19 cases
    // and three run gaps, 19·23 + 3·15 = 482 W, its last case ending one past
    // the program ring's outer right column (bottom_row_seats; the ring 23
    // device px in from each window edge on the tablet at 300 %, the canvas's
    // frame hairlines, re-derived 2026-10-28) — on the tablet's 2304 the
    // block's 1446 px on x 835..2280 (the skips 835..972, the verbs
    // 1018..1569, the walk 1615..1890, the nudge 1936..2280, Play
    // 2212..2280); the clock's field on the ring's outer left column, from
    // x 23 — the cell (4.45 em of Liberation Sans advances, seven
    // digits, the colon and the point, in the program face's 22 px at
    // 300 %, 98 px ceiled) and its two 12-px pads, a 122-px field, x
    // 23..144 — then the name's first ink 5 W past it (x 160, Tab A's
    // advance ~59 px), the state line's 5 W past the name's advance (x
    // ~234), clipped one run gap short of the block (x 790): about 556 px
    // of state line at 300 %. On the laptop's 1920 at 138 % (cases 31, the
    // gap 21, the program face 10 px) the block is 652 px, from x 1253 to
    // the ring's right column, the clock's field 57 px on x 15..71, the name
    // from x 79, and the state line from x ~113 to its clip at x 1232, about
    // 1120 px.
    // IN THE `h` VIEW (the hide, 2026-10-07; Play, Open Text Editor and
    // Toggle Value Drag hidden there with the verbs, Down
    // and Up with them) the block is the two skips + 1 verb + the walk pair
    // + Switch Tab, Center and Older / Newer in the nudge pair's seats, Play
    // not standing, 9·23 + 3·15 = 252 W, from W 509. THE ROW CARRIES NO
    // COLLISION RULE — none of the redesign does — and the crop-at-the-floor
    // allowance recorded at kMinWindowWidthPx covers a narrow window. THE
    // STATE LINE CANNOT PUSH ANYTHING: it is clipped short of the block, so
    // a long line is cut rather than colliding.
    //
    // THE BLOCK WINS IN Z (architect 2026-10-14, on a restored laptop window
    // about 392 W wide: "icons row should win in z-index … msg goes under it.
    // It should be fully opaque"): the block is MEASURED here, ahead of the
    // clock, because the state line's clip reads its left edge, but it is
    // PAINTED LAST — after the clock, the name and the state line, on its
    // own span of the ground laid fresh — so where the window is narrow enough
    // for it to walk over the clock, the block is what shows, whole and
    // opaque, and nothing of the field, its digits or its ring comes through,
    // exactly as the state line already goes under it. The press agrees by
    // construction: the name and the line are pointer-inert ground, the
    // clock's field answers as the PLAY key (its second rect, cut at the
    // block's left edge, the clock's block below) and the press resolves
    // against the published rects alone (claim_button_row_press,
    // input_pointer.cpp). Every repaint of the clock
    // is this body run whole under the damage clip (the clock cell's rect,
    // clock_invalidate_rect, main.cpp — there is no field-only path), so the
    // per-frame clock damage repaints the block over the clock inside the
    // clip too and cannot put the clock back on top mid-play. At a width
    // where nothing overlaps the order changes no byte: the clock and the
    // clipped line lie wholly left of the block, and the block's fresh
    // ground is paint_bottom_strip's own wood over the same span.
    // THE FOUR GROUPS IN PAINTED ORDER, each read off its own table, so a
    // box joining or leaving moves the block with no second edit.
    // IN THE `h` VIEW the nudge pair's seats are the walk's two steps
    // (kTransportHistoryStandIns, its one owner, seated through
    // transport_row_seated): the side that does not stand publishes its
    // empty rects here.
    const bool history = app.history_mode.active;
    // THE BLOCK, MEASURED (row8_block: the standing members, the left
    // edge). THE STATE LINE'S RIGHT BOUND (below) is its left edge,
    // published out of the measure so the line cannot guess it — so the
    // line widens by construction when the `h` view hides members; the
    // block's painting (after the clock) starts from it too.
    const Row8Block block = row8_block(app, history, lane);
    const int right_block_x = block.x;
    for (const TransportStandIn& s : kTransportHistoryStandIns)
        (void)publish_button_face(cr, app, audio, playback, target_render,
                                  history ? s.seat : s.stand_in.id,
                                  GuiRect{});

    // THE CLOCK — the first TIME FIELD (the shape and the fixed-width rule
    // at kTimeShape's block above), in THE PROGRAM FACE, the cues' 22 px
    // (architect 2026-10-26 ~11:30, settled ~13:05; gui_font.h's table),
    // named
    // through the one face owner (gui_font.h), inside the save/restore this
    // body already opened, IN ITS RING (2026-10-22: the program ring's two
    // tones round the field, paint_ce_time_field's clock ring). The render
    // player's two fields share its shape, its face and its metrics
    // (time_field_metrics); THE TWO NEVER PAINT IN THE SAME FRAME: the
    // player owns the row whole while it stands.
    {
        const GuiFont font = gui_font(GuiFace::Program);
        // THE CLOCK (row8_clock, the one layout): the field at the row's
        // left seat, its cell and its pads. THE CELL IS THE DIGITS'
        // SPECIMEN ALONE (architect 2026-10-18: the tab letter and its pipe
        // left the clock, "like the media player cells"), so it is ONE WIDTH
        // AT EVERY TIME, the run starting at its left edge (kTimeShape's
        // block).
        const Row8Clock clock = row8_clock(app, lane);
        const double cell_w  = clock.cell_w;
        const int    field_x = clock.field_x;
        const int    field_w = clock.field_w;
        const GuiRect clock_field =
            time_field_rect(field_x, field_w, btn_y, btn_h);
        // THE CLOCK IS PRESSED AS THE PLAY KEY (architect 2026-10-27): the
        // field as painted is Play / Stop's second rect
        // (RedesignButtonFace::field), so a press on it is a press on that
        // key — the lift bare Space, the Shift press or the long press
        // Shift+Space, the A/B audition — the key's grey its consumed
        // nothing, and its tooltip the key's own. Published on every pass,
        // whatever the clip, as the keys' rects are; CUT AT THE RIGHT
        // BLOCK'S LEFT EDGE, the block winning in z where a narrow window
        // lays it over the field (the z rule above), so no point answers
        // twice. Play hidden (the `h` view) or the row yielded (a modal)
        // publishes the key's empty rect after this, which empties the
        // field with it (publish_button_face's empty overload). NOTHING IS
        // PAINTED ON THE CLOCK FOR THE PRESS — no depth, no LED, no digit
        // shift: the press arms the PLAY key, which wears the held depth
        // and its LED, and the digits starting or stopping are the act's
        // own confirmation. The digits are never greyed. ROW 8'S CLOCK
        // ALONE: the render player's and the score viewer's time fields
        // share the field's painter (paint_ce_time_field) and never the
        // press.
        {
            GuiRect play_field = clock_field;
            play_field.w = std::min(clock_field.x + clock_field.w,
                                    right_block_x) -
                           clock_field.x;
            if (play_field.w <= 0) play_field = GuiRect{0, 0, 0, 0};
            app.redesign_buttons[redesign_button_index(
                                     RedesignButton::TransportPlayStop)]
                .field = play_field;
        }
        // THE FIELD AND ITS RUN ARE SKIPPED WHERE THE CLIP MISSES THE FIELD
        // (clip_meets): the run lies inside the cell published below, which
        // lies inside the field.
        const bool clock_drawn = clip_meets(cr, clock_field);
        if (clock_drawn)
            paint_ce_time_field(cr, clock_field, palette().ce_dark,
                                palette().ce_hilight);
        // THE BASELINE CENTRES THE FACE'S CAP BAND IN THE FIELD — the face
        // split above and below its digits by the one box solver. THE NAME
        // AND THE STATE LINE TAKE IT TOO (the ONE BASELINE block below).
        const double baseline =
            redesign_baseline(font, static_cast<double>(clock_field.y),
                              static_cast<double>(clock_field.h));
        // THE ACTIVE TAB'S NAME (architect 2026-10-22: the tab strip goes,
        // "Tab A" / "Tab B" on the plain wood right of the clock): its FIRST
        // INK the name's air (5 W) past the ring's light column, the run's
        // origin its own left bearing short of that; the program face in THE
        // BONE INK over THE CUE LABEL'S SHADOW (show_ce_label; architect
        // 2026-10-26 ~11:30, "the same shadow for the tab/git/etc text"), on
        // the clock's baseline.
        // Never greyed: it is no button (Ctrl+Tab, Switch Tab and the walk's
        // ctrl-click switch the tab). A tab switch damages the lane
        // (GuiActiveViews' switch, the state cell's owner).
        // IN THE `h` VIEW THE NAME'S RUN ENDS IN THE WALK LINE'S OWN
        // SEPARATOR, "Tab A | " (architect 2026-10-24: the name ran into the
        // line's position, `Tab A 007/114 | GitHub: ...`; kSegmentSeparator,
        // the one spelling), and THE SEPARATOR'S OWN TRAILING SPACE IS THE
        // WHOLE SEAM (architect 2026-10-25: "a giant ... amount of space"
        // after the pipe): no state air past that run's advance, so
        // `Tab A | 007/114 | GitHub: ...` reads as one line with one spacing,
        // the seam the same space as every ` | ` inside the walk line. The
        // pipe is the `h` view's alone: outside it the state cell carries
        // the ordinary progress text, the name stands bare and the state air
        // stands as below. The walk line is never empty while the view
        // stands (its position reads `0/0` at worst), so the seam is
        // unconditional there; the view's two edges repaint the whole window
        // (open_history_mode_fresh / close_history_mode).
        // WITH NO PIECE THERE IS NO NAME (architect 2026-10-27, the open's
        // hold's one frame, platform.h's head: no tab of no piece is named;
        // no_audio_to_dispatch_on): the run is empty, the state line's seat
        // computed from it as ever, and the `h` view cannot stand there.
        const bool no_piece =
            no_audio_to_dispatch_on(app, audio.total_frames());
        const bool name_ends_in_separator = app.history_mode.active;
        std::string name_text = no_piece                    ? ""
                                : app.active_tab_view == 'B' ? "Tab B"
                                                             : "Tab A";
        if (name_ends_in_separator)
            name_text += kSegmentSeparator;
        const text_shape::ShapedRun name_run =
            text_shape::shape_text_run(font, name_text);
        const int name_x =
            field_x + field_w + scaled_px(kProgramSpec.tab_name_air_px);
        const double name_origin =
            static_cast<double>(name_x) - text_shape::ink_left_px(name_run);
        // THE STATE LINE: on the row's free wood, no panel, ITS FIRST INK the
        // state air (5 W) past the name's advance — AT the advance, no air,
        // where the name's run ends in the separator (the `h` view, the
        // block above; 2026-10-25): the clip starts there and the run's
        // origin stands its own left bearing short of it (ink_left_px,
        // text_shape.h). Its RIGHT CLIP stands one run gap
        // short of the right block (2026-10-22; the pad before a bar
        // 2026-10-21, one group gap of 2 W short of its gripper before); a
        // window too narrow to leave that span positive paints no state text
        // — the row's crop-at-the-floor allowance (the block above).
        const int state_x =
            static_cast<int>(std::ceil(name_origin + name_run.width_px)) +
            (name_ends_in_separator ? 0
                                    : scaled_px(kProgramSpec.state_air_px));
        const int state_w = (right_block_x - lengths.gap) - state_x;
        show_ce_label(cr, name_run, name_origin, baseline);
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
        // may sit a hair outside it, and the band is the row's height, which
        // contains the field and so the baseline's ink by construction.
        // THE RECT FOLLOWS THE CELL BECAUSE IT IS BUILT FROM IT — the run's
        // start above is the cell's left edge and so is this box's, so the
        // damage can never miss the painted digits. CEIL, NOT nearbyint:
        // cell_w is a fractional advance sum and this is a DAMAGE box, which
        // may be a hair too wide but never a hair too narrow.
        app.clock_cell_rect = GuiRect{
            cell_x - 1, lane.y,
            static_cast<int>(std::ceil(cell_w)) + 2, lane.h};

        // THE CELL'S AS-PAINTED RECORD on the target being painted
        // (GuiClockFingerprint, app_state.h): a clip covering the cell
        // paints it whole from the live inputs, so the record takes them; a
        // clip reaching part of it under inputs other than the record's
        // leaves a mix, which only a repaint settles.
        {
            const GuiClockFingerprint live = clock_fingerprint();
            GuiClockRecord& rec = app.clock_painted[static_cast<std::size_t>(
                gui.painting_target())];
            if (clip_covers_drawable(cr, app, app.clock_cell_rect)) {
                rec.painted = live;
                rec.valid   = true;
            } else if (rec.valid && clip_meets(cr, app.clock_cell_rect) &&
                       !(live == rec.painted)) {
                rec.valid = false;
            }
        }

        // THE FIELD AND THE LINE, THEIR CONTENT AND ITS PRECEDENCE: the field
        // carries the clock's run, the line the STATE — the `h` walk line,
        // else the render / batch progress line. The two state
        // strings CAN COEXIST and THE WALK LINE WINS — nothing STARTS a render
        // inside the `h` view (both render chords are off its allowlist), but
        // a render or a target preview dispatched BEFORE the visit runs on
        // through it, so the mode's line is what the line is for while the
        // view stands and the progress line is back the moment it closes.
        //
        // ONE FACE, ONE BASELINE (the line architect 2026-10-03; the clock's
        // face 2026-10-05; the one baseline 2026-10-07; the name 2026-10-22):
        //   * THE CLOCK — the digits in the program face, the bone and no
        //     shadow, on the field's own seat. IT IS NEVER
        //     CLIPPED: the row's crop-at-its-floor allowance (the block above)
        //     is what covers the narrow window where the right block has
        //     already walked over this ground, and there the block, painted
        //     after it, covers it (the z rule, 2026-10-14).
        //   * THE NAME — above, the program face on the clock's baseline,
        //     the state's own pair.
        //   * THE STATE — THE PROGRAM FACE (gui_font(GuiFace::Program)) IN
        //     THE BONE OVER THE CUE LABEL'S SHADOW (show_ce_label: the cue
        //     triangle's shadow color one label quantum right and down,
        //     architect 2026-10-26 ~11:30, "with the same shadow for the
        //     tab/git/etc text"; the engraved pair past the label threshold,
        //     2026-10-10, is unread for this line: the one pair stands on
        //     every ground),
        //     straight on the row's wood, ON THE CLOCK'S OWN BASELINE
        //     (architect 2026-10-07: "whichever one is right, the other
        //     should be centred to behave like it", and the field is right).
        //     The larger face is the architect's (2026-10-03): "I don't
        //     really read it" at the small face, and a running render's
        //     progress should read. IT IS CLIPPED, NEVER
        //     ELLIPSISED — a cairo rectangle clip, the folder overlay rows'
        //     precedent — so a long line is cut rather than colliding with
        //     the block; the composer puts the segment that must survive the
        //     cut ahead of the one that may lose it — the GitHub word before
        //     the scale (history_walk_line, architect 2026-09-29). With no
        //     span left nothing is shown.
        //
        // THE CLOCK CARRIES NO DIRTY MARK (architect 2026-10-05): SAVE'S GREY
        // IS THE MARK on both machines (plain_save_actionable, app_state.h).
        // THE ROW YIELDS WHOLE TO A MODAL, so the field, the name and the line
        // are hidden while a prompt, a dialog editor, the render player or
        // the picker stands (modal_owns_bottom_row). It is why the render
        // player's load-under-a-running-render refusal says its sentence on a
        // CARD: the explanation the state line would have carried is not on
        // screen under that modal.
        // The line is skipped whole where the clip misses its span
        // (clip_meets; it paints under its own clip to that span).
        const GuiRect state_span{state_x, lane.y, state_w, lane.h};
        const bool state_drawn = state_w > 0 && clip_meets(cr, state_span);
        std::string state;
        if (state_drawn) {
            // The process line — the render/batch/queue status, empty when
            // there is nothing to report — unless the walk line wins.
            state = app.history_mode.active ? history_walk_line(app)
                                            : app.queue_progress_text;
        }
        // THE CLOCK, UNCLIPPED, IN THE FIXED CELL — the timestamp as one run
        // starting at the cell's left edge.
        if (clock_drawn) {
            const text_shape::ShapedRun time_run =
                text_shape::shape_text_run(font, clock_time_text(app, audio));
            // THE BONE on the field's black (architect 2026-10-23: "the font
            // color should not be this yellowish color"; E6D5A8, the
            // clocks' own 2026-10-21 to 2026-10-23, retired).
            set_palette_source(cr, hex(kBoneInk));
            text_shape::show_shaped_run(cr, time_run,
                                        static_cast<double>(cell_x), baseline);
        }
        if (state_drawn && !state.empty()) {
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

    // THE RIGHT BLOCK, PAINTED LAST (the z rule at THE NUMBERS above): its
    // span of the lane laid in the ground first — the fill paint_bottom_strip
    // lays, so at a width where nothing overlaps it rewrites the ground that
    // is already there — then the groups left to right on it, so no pixel of
    // the clock, its ring included, survives under the block.
    {
        const int block_x0 = std::max(right_block_x, lane.x);
        const int block_x1 = lane.x + lane.w;
        if (block_x1 > block_x0) {
            cairo_save(cr);
            cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
            paint_cell_rect(cr,
                            GuiRect{block_x0, lane.y, block_x1 - block_x0,
                                    lane.h},
                            palette().face);
            cairo_restore(cr);
        }
        // A HIDDEN MEMBER PUBLISHES THE EMPTY RECT (the band's absent
        // members' shape): no point is inside it, so no press, hover or
        // tooltip reaches it, and its bits refresh unconditionally
        // (publish_button_face) — the same rect the modal yield publishes
        // for the whole row. Each seat paints what transport_row_seated
        // answers: the member, or its stand-in in the `h` view
        // (row8_block_walk, the one walk).
        row8_block_walk(
            history, block, lengths,
            [&](const TransportRowDef& def, int bx) { paint_button(def, bx); },
            [&](const TransportRowDef& def) {
                (void)publish_button_face(cr, app, audio, playback,
                                          target_render, def.id, GuiRect{});
            });
    }

    cairo_restore(cr);
}

// -- The floating surfaces ---------------------------------------------------

void GuiPaintHandler::paint_popup_chrome(cairo_t* cr, const GuiRect& r) {
    // ONE BOX PAINTER FOR EVERY FLOATING SURFACE — the tooltip and every
    // notification card, whose words their painters set — IN THE HI-FI SKIN
    // (architect 2026-10-23): ONE SUNKEN FLAT FIELD, `card_ground` (the field
    // kFieldGround), under a ONE-DEVICE-PX RING in the ring tones, dark on the
    // top row and left column, light on the bottom row and right column,
    // mitred (paint_hairline_frame; render.h's line rule). A card keeps its
    // own height rule; its pad is the spec's (tooltip_pad_px). (The one-line
    // square `card_frame` border of Windows 2000's tooltip on the Face, then
    // the wood, 2026-10-04 to 2026-10-23, is git history.)
    const GuiMaterialScope flat(GuiMaterialRegion::Flat);
    paint_cell_rect(cr, r, palette().card_ground);
    paint_hairline_frame(cr, r, palette().ce_dark, palette().ce_hilight);
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
        // (2026-09-01; the frame count, the third, out for 2026-10-24 alone
        // and back 2026-10-25 with Zoom to Fit's reset term): the words fork
        // on what the acts and the faces read, and this painter is the
        // overload's one reader. It took the
        // enabled predicate's whole five for the morning of 2026-09-01,
        // `playback` and `target_render` unread at the far end; they left
        // with the rule that a parameter arrives with its producer.
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
        // The face's rect under the show's point: row 8's clock where the
        // pointer rested on it (RedesignButtonFace::rect_at), the key
        // otherwise — tooltip_hang_bands reads the same.
        btn   = app.redesign_buttons[owner.index].rect_at(
            app.redesign_tooltip.shown_x, app.redesign_tooltip.shown_y);
    }
    if (btn.w <= 0 || btn.h <= 0) return;

    cairo_save(cr);
    // THE TOOLTIP WEARS THE FLAGS' FACE (architect 2026-10-23, "the tooltips
    // font size should be the same as the flags"): GuiFace::Program, 22 px
    // at 300 % since the flags' size settled there (2026-10-26 ~13:05), IN
    // THE BONE kBoneInk like the flags and every word (architect 2026-10-23
    // night; the legend ink kLegendInk its first night, now the keys' print
    // alone) on the field, no shadow; the box grows with the face's own line
    // (its pads are the chrome spec's, below) — 44 device rows at 300 % for
    // one line (the line 30, the hairline and the 2-W pad twice). The legend
    // face, 14 px, is the keys' print alone (it was the tooltip's the first
    // night of the skin).
    const GuiFont font = gui_font(GuiFace::Program);
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
    const int frame = hairline_px();
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

    paint_popup_chrome(cr, box);

    // EACH LINE SITS ON ITS OWN BAND, the face's ascent plus its descent
    // (the box's height, above). THE FIRST LINE'S CAP BAND IS CENTERED IN
    // ITS BAND by redesign_baseline — the interior itself on a one-line
    // tooltip — the one rule every chrome label takes (architect 2026-10-22:
    // the line box centered there read low under descenders, and the one-W
    // lift on it overshot); the hint line stands one band and the gap lower.
    set_palette_source(cr, hex(kBoneInk));
    const double base1 =
        redesign_baseline(font, static_cast<double>(y + pad_y),
                          std::nearbyint(band1));
    text_shape::show_shaped_run(cr, r1, static_cast<double>(x + pad_x),
                                base1);
    if (two_line) {
        // The hint line takes THE CARD TEXT like the first (architect
        // 2026-10-03, Windows' ink, no dims; the card face's words are
        // `card_text` since 2026-10-04, render.h's palette block).
        text_shape::show_shaped_run(cr, r2, static_cast<double>(x + pad_x),
                                    base1 + band1 + static_cast<double>(gap));
    }

    cairo_restore(cr);
}

namespace {

// THE CARD'S LINE SPACING: the body face's OWN line height (the file's
// ascender, descender and line gap at the live size, gui_font_line_px;
// gui_font.h's head, 2026-10-23 — the recorded 13-W Windows cell retired
// with the skin's type table), rounded to whole px, so a wrapped card's rows
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
// 2026-10-04; the hi-fi skin 2026-10-23): THE CARD FACE — the tooltip's own
// look (architect 2026-10-06, his word: the cards take the tooltip's look),
// `card_ground` inside the one-device-px sunken ring, its words `card_text`
// (render.h's palette block, THE CARD FACE) — square, NO DROP SHADOW,
// through the one popup box painter (paint_popup_chrome, the tooltip's own);
// inside the ring ONE PAD of plain field on all four sides (P18, architect
// 2026-10-24; notification_pad_px), the first line THE BODY FACE'S CAP tall
// between the pads (notification_card_h_px), THE CLASS GLYPH at its left —
// the planner's i or X, a BOXED GLYPH (icons.h), its ink .525 of the first
// line's inside height (kNotificationGlyphRatio) — then the logo lane's brand
// gap, then THE SENTENCE (the layout block in the body).
// (A window-close X stood in a second button box at the right from
// 2026-08-29, wearing the icon button's hover face; it retired 2026-10-01
// when the whole card became the button — "whole card dismisses, X gone" —
// and the card wears no hover face and no pressed face.)
// The card's width
// is its content's, clamped to [kNotificationMinWidthPx,
// notification_card_max_w_px] — the authored ceiling since 2026-08-31, scaled
// like every other length, with the window itself as a safety under it.
//
// THE TEXT GROWS DOWNWARD (architect 2026-08-30). The
// width rule is unchanged — the WHOLE sentence's shaped width sets it, inside
// the same clamp — so a sentence that fits its room is one line exactly as it
// was; a longer one wraps through notification_text_lines above to at most
// kNotificationMaxLines, the last of which clips at the run's right edge (no
// ellipsis — the folder overlay rows' precedent). THE CARD GROWS, NOTHING
// REFLOWS: the first line keeps the one-line card's baseline, each further line sits
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
    // THE LAYOUT (architect 2026-10-24, P18 with I1 / X1): ring, ONE PAD of plain field (notifications.h,
    // every side), the class glyph's box, the logo lane's brand gap, the
    // text's pen; the wrap's room ends one pad short of the right ring. The
    // first line's INSIDE rows are pad + cap + pad; the glyph's ink is
    // kNotificationGlyphRatio of them, its box the ink's 40 / 27 rounded
    // (icons::icon_box_live_px), the box's rows centered on the inside rows
    // (rounded down), its left edge at ring + pad.
    const int ring     = hairline_px();
    const int pad      = notification_pad_px();
    const int cap      = notification_cap_px();
    const int inside_h = pad + cap + pad;
    const int glyph_h  = icons::icon_box_live_px(
        kNotificationGlyphRatio * static_cast<double>(inside_h));
    const int glyph_w  = icons::icon_box_w_px(glyph_h);
    const int glyph_gap = skin_px(kLogoBrandGapAt300);
    const int lead_w   = ring + pad + glyph_w + glyph_gap;
    const int tail_w   = pad + ring;
    const int chrome_w = lead_w + tail_w;
    const int right_x  = room.x + room.w;
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
        // width leaves between the text's pen and the right pad.
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
        const int text_x    = card_x + lead_w;
        const int text_room = card_x + w - tail_w - text_x;
        std::vector<text_shape::ShapedRun> lines =
            notification_text_lines(font, n.text, whole,
                                    static_cast<double>(text_room));
        // THE CARD GROWS DOWNWARD BY WHOLE LINES; its first line is a
        // one-line card and the first baseline sits in it.
        const GuiRect card{
            card_x, y, w,
            line1_h + (static_cast<int>(lines.size()) - 1) * line_h};
        laid.push_back(LaidCard{&n, card, text_x, text_room, std::move(lines)});
        y += card.h + gap;
    }

    // THE CARD PASS, clipped to the room.
    cairo_rectangle(cr, room.x, room.y, room.w, room.h);
    cairo_clip(cr);
    for (const LaidCard& c : laid) {
        const AppState::Notification& n = *c.n;
        const GuiRect& card = c.card;
        const int text_x    = c.text_x;
        const int text_room = c.text_room;
        const std::vector<text_shape::ShapedRun>& lines = c.lines;
        // The box, then the class glyph in its box on the first line's
        // inside rows (the layout block above).
        paint_popup_chrome(cr, card);
        icons::draw_boxed(cr,
                          n.cls == AppState::NotificationClass::Critical
                              ? icons::Icon::CardCritical
                              : icons::Icon::CardNormal,
                          card.x + ring + pad,
                          card.y + ring + (inside_h - glyph_h) / 2, glyph_h);

        if (text_room > 0) {
            cairo_save(cr);
            cairo_rectangle(cr, text_x, card.y, text_room, card.h);
            cairo_clip(cr);
            set_palette_source(cr, palette().card_text);
            // The FIRST line's baseline puts ITS CAP TOP AT RING + PAD
            // (architect 2026-10-24): the cap band centered in the first
            // line, the line being exactly pad + cap + pad inside the ring
            // (notification_card_h_px) — redesign_baseline's centering on
            // the rounded cap, stated whole so no half-px floor moves it.
            // Each further line is one face line-height lower.
            const double base = static_cast<double>(card.y + ring + pad + cap);
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

// (THE DROPDOWN'S PAINTER — paint_dropdown, Windows 95's popup menu hanging
// from its anchor, its lit row, its access-key underlines, its separators and
// the True Colors row's menu check mark — RETIRED 2026-10-21 with the Settings
// popup, its last menu: the settings editor completes the keys on Tab.)

// -- THE RULER LANE (top lane 5) ---------------------------------------------
//
// A LOOK/MODEL SPLIT, and it is deliberate: the ruler takes COOL EDIT'S LOOK
// and REAPER'S GEOMETRY MODEL.
//   LOOK, Cool Edit's FLIPPED RULER (architect 2026-10-09, the program is
//     Cool Edit; render.h's canvas-column
//     paragraph): the Face-derived ground under the view bar's light line,
//     its own light bottom line, the ticks E0E0E0 STANDING ON THE GROUND'S
//     BOTTOM ROW — majors four W, minors two — and the digits bare (Cool
//     Edit's black (+1, +1) shadow under them not drawn, architect
//     2026-10-25); THE GROUND 11 W, THE DIGITS THE BASE'S SIX-ROW SMALL
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
    // main.cpp) or the viewport's lanes whole (the discrete moves'
    // Viewport::invalidate_waveform_area), so the frame that shows a moved
    // head is always a publishing one.
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
    // THE HI-FI SKIN'S LANES (architect 2026-10-23: "brighter marker lane,
    // darker timestamp lane"; materials.h's REGIONS): the lanes' margins
    // beside the program ring the brushed GROUND; the ruler's ground THE FLAT
    // kRulerGround (the palette's `ce_mid`); the marker lane THE BRIGHTER
    // BRUSHED STRIP (the Marker tile, at the lane's phase) between the ring's
    // outer columns; THE PROGRAM RING's two columns ONE DEVICE PX, the
    // canvas's frame columns (render.h's program_ring_columns, the hairline
    // itself since 2026-10-28), the dark one left and the light one right,
    // carried down from the view bar (render_trim_flags) into the canvas
    // ring's (paint_canvas_column_frame); THE RULER'S FOOT LINE one device px
    // of the ring's light tone on its one-W slot's last row
    // (program_line_px), the slot's other rows the ruler's ground. (The
    // wood, the brass's flat ruler and its strip, the ring a scaled line,
    // 2026-10-21 to 2026-10-23, are git history.)
    const GuiPalette& pal = palette();
    const int ground_h = ruler_ground_h_px();
    const int lw       = program_line_px();
    const int px       = hairline_px();
    const ProgramRingColumns ring = program_ring_columns(app);
    const int ring_x0 = ring.x;
    const int ring_x1 = ring.end;
    {
        const GuiMaterialScope ground(GuiMaterialRegion::Ground);
        paint_cell_rect(cr, lane, pal.face);
    }
    if (marker.w > 0 && marker.h > 0) {
        const int in0 = std::clamp(ring_x0, marker.x, marker.x + marker.w);
        const int in1 = std::clamp(ring_x1, in0, marker.x + marker.w);
        {
            const GuiMaterialScope ground(GuiMaterialRegion::Ground);
            if (in0 > marker.x)
                paint_cell_rect(cr, GuiRect{marker.x, marker.y, in0 - marker.x,
                                            marker.h},
                                pal.face);
            if (marker.x + marker.w > in1)
                paint_cell_rect(cr, GuiRect{in1, marker.y,
                                            marker.x + marker.w - in1, marker.h},
                                pal.face);
        }
        if (in1 > in0) {
            const GuiMaterialScope lane_ground(GuiMaterialRegion::Marker);
            paint_cell_rect(cr, GuiRect{in0, marker.y, in1 - in0, marker.h},
                            pal.face);
        }
        // THE RING THROUGH THE LANE — never targets (the cues' paint and
        // hits stop at the canvas's columns).
        const GuiMaterialScope flat(GuiMaterialRegion::Flat);
        paint_cell_rect(cr, GuiRect{ring_x0, marker.y, px, marker.h},
                        pal.ce_dark);
        paint_cell_rect(cr, GuiRect{ring_x1 - px, marker.y, px, marker.h},
                        pal.ce_hilight);
    }

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
    // waveform area's x, the frame's side columns directly outside it.
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
    // THE GROUND, ITS RING COLUMNS AND ITS FOOT (the head of this body): the
    // flat ruler ground between the ring's columns and across the foot's
    // slot, then the two hairline columns and the light foot line. The ground
    // and the columns read the LIVE area, as the canvas's own frame does
    // (paint_canvas_column_frame); the walk below reads the plate's width.
    {
        const GuiMaterialScope flat(GuiMaterialRegion::Flat);
        const int frame_h = ground_h + lw;
        const int ring_w  = ring_x1 - ring_x0;
        paint_cell_rect(cr, GuiRect{ring_x0, lane.y, ring_w, frame_h},
                        pal.ce_mid);
        paint_cell_rect(cr, GuiRect{ring_x0, lane.y, px, frame_h}, pal.ce_dark);
        paint_cell_rect(cr, GuiRect{ring_x1 - px, lane.y, px, frame_h},
                        pal.ce_hilight);
        paint_cell_rect(cr, GuiRect{ring_x0 + px, lane.y + frame_h - px,
                                    ring_w - 2 * px, px},
                        pal.ce_hilight);
    }

    const int64_t step   = comb.step;
    const double  end_ms = comb.end_ms();
    // (There is no `minor` time step any more. It had two consumers — the float
    // placement of each minor tick and the head's since-deleted float
    // re-derivation of which columns carried one — and the rigid comb replaced
    // both with integer
    // distribution across a segment. The MINORS-PER-STEP count is still the
    // ladder's own kRulerMinorsPerStep; only its expression as a duration is
    // gone.)

    // THE TICKS STAND ON THE GROUND'S BOTTOM ROW (the flipped ruler): each ONE DEVICE PX wide in kCeRulerTick (hairline_px,
    // render.h's line rule, 2026-10-23), rising the major's four W or the
    // minor's two into the ground from its foot — its rows 7 .. 10 and
    // 9 .. 10 of 0 .. 10, the lengths areas (scaled_px).
    const int ground_bottom = lane.y + ground_h;   // one past the ground's rows
    const int major_top = ground_bottom - scaled_px(kProgramSpec.ruler_major_tick_px);
    const int minor_top = ground_bottom - scaled_px(kProgramSpec.ruler_minor_tick_px);

    // THE DIGITS: THE RULER FACE (gui_font.h's type table, `Small`: 16 px
    // at 300 %, scaled with gui_scale — architect 2026-10-24, "R16"; R17 the
    // night before, 15 the skin's first night, the six-row Windows digit
    // before it), their
    // BASELINE THE TOP OF THE GROUND'S ROW 7 — the digits standing above it
    // under the air, "basically touching the trim bar" (architect 2026-10-09
    // ~21:00): at 300 % the baseline 21 device rows under the lane's top, a
    // digit's ink 11.2 px tall (Liberation Sans's 1430 / 2048 em of 16), so
    // its top stands 9.8 rows down, just inside row 9, with 9 whole rows of
    // ground above it under the trim bar's light line, and its "0"'s
    // overshoot a fifth of a px below the baseline into row 7, a major
    // tick's top row; NO SHADOW under them (architect 2026-10-25, on the
    // glass: "the shadow on the timestamp is basically useless because it's
    // so dark, it has no effect") — an authored row of the lane, neither
    // solver asked (redesign_baseline's block).
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
    // THE LABELS ARE CLIPPED AT BOTH ENDS (architect 2026-10-22, "clip"): a
    // label is painted CENTERED on its major's column wherever it stands and
    // the walk's clip below cuts it at the canvas's columns [0, wave_w), so
    // under a pan a label slides out a digit at a time. The 2026-10-09
    // edge-drop (the mock's rule: a label whose box would cross either end
    // was not painted, so no digit was ever cut) made the numbers "pop in
    // and out … as soon as the number touches the edge of the screen, it
    // disappears"; reported as a regression, it was the rule as built, and
    // his one-word ruling replaced it. The walk therefore reaches EVERY major
    // whose label can touch the columns: it starts at first_step, whose major
    // stands at or left of column 0, and runs ONE STEP PAST the last column
    // (the loop's break), the major just right of the end whose label's left
    // half reaches in — a label is narrower than a step (the ladder keeps a
    // step at least 72 W), so one step each side is all there is.
    // THE TICK'S WIDTH IS ONE DEVICE PX, and so is its shadow's offset
    // (render.h's line rule, architect 2026-10-23; the program's quantum
    // 2026-10-09 to 2026-10-23).
    const int t = hairline_px();
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
        // THE RUN FROM THE LABEL RUNS' MEMO (text_shape::memoized_run,
        // 2026-10-13, shared with the cues): a pan repaints the ruler on
        // every frame its damage reaches, and the same timestamps on the same
        // Small face shaped afresh each time. IDENTITY RESTS ON THE MEMO'S
        // PREMISE — a hit is the run shape_text_run returns for this font and
        // these bytes — handed to the same two show_shaped_run calls below.
        const std::string txt = ruler_label_text(label_ms, step);
        const text_shape::SharedRun handle = text_shape::memoized_run(font, txt);
        const text_shape::ShapedRun& run = *handle;
        const int run_w = static_cast<int>(std::ceil(run.width_px));
        const int lx = col + t / 2 - run_w / 2;   // the walk's clip cuts it
        // EVERY LABEL IS ONE COLOR (architect 2026-10-02, "give the same
        // colour to all the numbers"): the tick's ink, bare (2026-10-25, the
        // digits' shadow removed; the block above).
        const double x = static_cast<double>(cx + lx);
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
        // One step past the end (the clip rule above): the major right of the
        // last column is walked for its label, its tick culled below.
        if (step_ms > end_ms + static_cast<double>(step)) break;
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
            // over a digit's overshoot below the baseline, where the
            // overshoot meets the major's top row 7 (program_spec.h's ruler
            // fields, 2026-10-09 ~21:00; the ticks paint over). Only the
            // label's own major can meet it: a minor rises to row 9, below
            // the overshoot's row 7, and the next major stands a whole step —
            // at least eight minimum minor pitches — away.
            if (major) paint_major_label(col, step_ms);
            // ONE DEVICE PX wide (`t`, hairline_px: render.h's line rule,
            // 2026-10-23), on the tick's own column, clipped at the right
            // edge, in kCeRulerTick, standing on the ground's bottom row.
            // THE TICK'S SHADOW (2026-10-23): one device px of black at
            // (+1, +1), under it, the walk's clip cutting its last row.
            if (tick_on_lane) {
                set_palette_source(cr, hex(kCeRulerShadow));
                fill_waveform_line(cr, cx, wave_w, col + t,
                                   (major ? major_top : minor_top) + t,
                                   ground_bottom + t, t);
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
    // 2026-10-09; render.h's playhead
    // paragraph): the cue's own triangle — its rows (cue_triangle_rows, 8 at
    // 300 %) in whole device cells, Cool Edit's aliased staircase
    // (paint_ce_cue_triangle, 2026-10-26; its cells one device px with the
    // cue's, architect 2026-10-26 ~10:10: "All triangles unscaled",
    // cue_triangle_unit_px, render.h), in the palette's `playhead_stem` (Cool Edit's Curs yellow
    // FFFF00 by default, 2026-10-09) over the same triangle one label
    // quantum right and down in `ce_cue_shadow` (2026-10-26 ~13:05, the
    // label's own offset), ON THE GROUND'S LAST `rows` DEVICE ROWS, its
    // apex — the bottom centre of the playhead's one-px column — on the
    // ground's bottom row (2026-10-09 ~21:00, the 11-W ground). It is OPAQUE over the ticks and digits the walk above laid
    // down, covering whatever stands under it (every cell whole, no edge
    // blended over them). NO SNAP, NO
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
            // THE HEAD'S CLIP: the waveform's columns over the head's own
            // rows, the ground's last `rows` — so the shadow, one label
            // quantum right AND DOWN (paint_ce_cue_triangle's head,
            // 2026-10-26 ~13:05), loses its bottom rows at the ground's
            // bottom row and never reaches the ruler's line or the marker
            // lane below, the marker lane's clip cutting the cues' shadows
            // alike (paint_cues, render.cpp). Whole pixels, so the stamp's
            // identity holds.
            cairo_save(cr);
            cairo_rectangle(cr, cx, head_top, wave_w, rows);
            cairo_clip(cr);
            paint_ce_cue_triangle(cr, cx + col, head_top,
                                  palette().playhead_stem,
                                  palette().ce_cue_shadow);
            cairo_restore(cr);
            // THE HEAD'S HIT (architect 2026-10-09 ~17:40, the head drag;
            // point_on_playhead_head, input_pointer.cpp): EXACTLY THE PAINTED
            // HEAD — its triangle's and shadow's columns [col − left,
            // col + right) cut to the waveform's columns as the clip above
            // cuts them, over the head's `rows` device rows of the ruler's
            // (the shadow's rows below them cut by that clip) — and
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

void GuiPaintHandler::paint_waveform_plate(GuiCanvasSink& sink,
                                           const GuiRect& area) {
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
    //
    // THROUGH THE CANVAS SINK (2026-10-13, canvas_sink.h): the cairo road
    // blits the live surface as above; the GL canvas's list takes the live
    // COLUMN BOUNDS instead (WaveformCache::columns, the plate it keeps in
    // place of a surface) and the published inks, the GPU drawing the same
    // select the writer stores (GuiListCanvasSink::plate).
    sink.plate(area, wf_cache.surface, wf_cache.columns, wf_cache.fp_inks);
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
// Painted in T+P ALONE, never in S+P, and this is a
// phase-reset-only surface with no warp sibling (naming-symmetry asymmetry,
// recorded here per CLAUDE.md). The seed grain is an OUTPUT-domain extent —
// the engine's window, on the engine's output lattice — so target time is
// where it is a fixed picture; source view would show a map-dependent,
// varying width, misrepresenting it. And S+P is the ROUGH-PLACEMENT view
// (architect 2026-10-12, S+P's return: "the purpose of the overlay is
// fine-tuning", which T+P alone does): S+P's drop takes the lead-in but
// paints no band, and its drag rides no ring.
// A seed grain is a phase-reset-only concept, so there is nothing on the
// warp axis to mirror.
GuiPaintHandler::PhaseResetOverlayBand
GuiPaintHandler::phase_reset_overlay_band(const GuiRect& area) const {
    PhaseResetOverlayBand out;
    // Visibility: always-on for the focused enabled marker while the global
    // W/P mode is on P, in target view; never source view. Everything
    // downstream is domain-agnostic.
    if (app.active_markers_view != 'P') return out;
    if (app.active_audio_view != 'T') return out;
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
    {
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
// colors exactly as its stem wears them — live_cue_stem_dots asks the one
// resolver (resolve_flag_face) for the stem's pair, so the ring and the stem
// cannot drift: the red `cue` and the blue `range` alternating, a
// coincident reset's too (its struck label the mark, architect 2026-10-26;
// the invalid color alone 2026-10-11 to 2026-10-26); resting whether the
// reset is selected or not, as the stem is.
//   THE LEFT SIDE IS THE STEM'S OWN COLUMN and the ring does not paint it:
//     the reset's stem already stands there (paint_marker_stems, after this
//     pass), and a playhead on the reset interleaves its yellow there as on
//     any stem — "yellow, blue, yellow, red all the way around". A stem off
//     the area's left edge leaves the clipped band with no left side.
//   THE RIGHT SIDE on the band's last column, the stem's rule exactly
//     (program_spec.h's dot fields: the blue on the canvas rows ≡
//     range_dot_phase and the red on ≡ cue_dot_phase, mod cue_dot_period,
//     counted from the canvas's first row), down both wells and not across
//     the stereo divider's rows (2026-10-16) — the dotted line's runs
//     (dotted_waveform_line_runs, render.h), the stem's own rule. A band
//     clipped at the area's right edge draws it there, flush, as the trim
//     bridge's clipped fill reads.
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
// 2026-10-09): the runs land on the canvas's first and last rows — the upper
// well's first and the lower well's last, the two wells' rings staying whole
// under a reset's span and the right side skipping the stereo divider's rows
// (2026-10-16). No
// other reader takes the ring's cells: it is no hit target, and its damage
// is the whole waveform area's (the flag cache's rebuild and
// Selection::damage_overlay_on_subject_change), which holds it whole.
// DAMAGE: this pass paints live in on_redraw from app state, never from a
// cached surface, and every change to its dots' inputs misses the flag
// cache's fingerprint, whose rebuild damages the waveform with the strip
// (maybe_rebuild_flag_cache, waveform_cache.cpp) — the stem's own repaint;
// a palette install damages the whole window.
// THROUGH THE CANVAS SINK (2026-10-13, canvas_sink.h): the top and bottom
// runs one dots call (one fill on the cairo road, as before), the right side
// another, under the one aliased scope.
void GuiPaintHandler::paint_phase_reset_overlay_ring(
    GuiCanvasSink& sink, const GuiRect& area) {
    const PhaseResetOverlayBand band = phase_reset_overlay_band(area);
    if (!band.valid) return;

    const GuiStemDots dots = live_cue_stem_dots();
    const ProgramSpec& ps = kProgramSpec;
    const int t      = waveform_line_px();
    const int x0     = static_cast<int>(std::nearbyint(band.x0));
    const int x1     = static_cast<int>(std::nearbyint(band.x1));
    const int stem   = static_cast<int>(std::nearbyint(band.stem_x));
    const int period = ps.cue_dot_period;
    const int y_top  = area.y;
    const int y_bot  = area.y + area.h - t;

    sink.begin(/*aliased=*/true);
    const auto paint_dots = [&](const GuiColor& color, int phase) {
        // The top and bottom runs: the columns of [x0, x1) whose offset from
        // the stem is ≡ phase (mod period).
        const GuiCanvasDotRun runs[2] = {
            GuiCanvasDotRun{false, x0, y_top, x1 - x0, period, stem, phase},
            GuiCanvasDotRun{false, x0, y_bot, x1 - x0, period, stem, phase},
        };
        sink.dots(runs, color);
        // The right side on the band's last column, one run per well (the
        // stereo divider's rows carry none, 2026-10-16).
        GuiCanvasDotRun side[2];
        const int n = dotted_waveform_line_runs(area, x1 - t - area.x, period,
                                                phase, side);
        if (n > 0)
            sink.dots(std::span<const GuiCanvasDotRun>(
                          side, static_cast<std::size_t>(n)),
                      color);
    };
    if (dots.range_phase_dots != GuiCueDot::None)
        paint_dots(cue_dot_color(dots.range_phase_dots), ps.range_dot_phase);
    if (dots.cue_phase_dots != GuiCueDot::None)
        paint_dots(cue_dot_color(dots.cue_phase_dots), ps.cue_dot_phase);
    sink.end();
}

// -- GuiPaintHandler::paint_trim -----------------------------------------

// The LIVE trim pass: every trim pixel — the view bar's ring and field and
// the span (render_trim_flags) — paints here per frame, entirely inside its
// lane,
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
    // write damages at least the viewport's lanes, the band's foot through
    // the waveform's bottom at the window's width, which hold this
    // lane whole (commit_trim_mutation, the sweep's and the drag's per-motion writes, the
    // maximizer, and the tab, load and load-in-place roads), so the frame that
    // shows a moved bound is always a publishing one. The early returns below
    // publish COLD when they may publish at all: a lane with no bar has
    // nothing to grab.
    // The trim lane's own ground the panel face, under the view bar, so the
    // column's margins beside the bar's frame (render_trim_flags spans the
    // canvas's columns and its two frame columns alone, 2026-10-09) read as
    // the panel. (The column's 5-W air above it, painted here 2026-10-09 to
    // 2026-10-22, went with the simple layout: the bar stands directly under
    // the band.)
    // THE BRUSHED GROUND (materials.h's GROUND, 2026-10-23): the lane's
    // ground, the column's margins beside the bar; the bar, its field and
    // its ring render_trim_flags' (flat).
    const GuiRect trim_row = top_trim_row_area(app);
    {
        const GuiMaterialScope ground(GuiMaterialRegion::Ground);
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
    const GuiMaterialScope flat(GuiMaterialRegion::Flat);
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
// THE STEM IS COOL EDIT'S DOTTED COLUMN IN ITS CUE COLORS (architect
// 2026-10-09; the two colors ~11:50; every cue's since 2026-10-26;
// render.h's marker-lane paragraph): one-device-px dots on the
// CANVAS'S device rows counted from its top row (the canvas sink's
// stem_dots, one run per well, waveform_well_rows) — none on the canvas's
// frame row, none on the stereo divider's rows (2026-10-16) and none in the
// lane — on
// rows ≡ cue_dot_phase (the red's phase) and on rows ≡ range_dot_phase (the
// blue's; mod cue_dot_period, program_spec.h), each phase in the role the
// stash names for it (GuiCueDot, cue_dot_color; resolve_flag_face's table:
// every live cue the red `cue` and the blue `range`, alternating one dot
// every four rows, the history's two halves alike) — the playhead's dots on
// rows ≡ 1 of 4, so neither color shares a row with them. (The solid stem
// through the well's top lines between its flanks stood 2026-10-02 to
// 2026-10-09; git history.)
//
// Z-ORDER (architect 2026-09-23): the stems paint UNDER the playhead's dots,
// which follow this pass; on a shared column the phases never touch.
// The full sequence is the paint-order block in on_redraw.
//
// THE DOTS ARE STORED AS PIXEL WORDS (2026-10-13, measured on the tablet:
// a one-px-wide mask costs pixman a scanline setup per row, the canvas
// height twice a stem), the same pixels as the mask op they replace — the
// two roads and why they agree are at the body's dotted_stem.
// THROUGH THE CANVAS SINK since 2026-10-13 (canvas_sink.h): both roads live
// in the cairo sink's stem_dots, unchanged in bytes, and the GL canvas's
// list takes each stem's phase as a dotted column.
void GuiPaintHandler::paint_marker_stems(GuiCanvasSink& sink,
                                         const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    paint_marker_stem_runs(sink, area);
    if (app.marker_stems.empty()) return;

    // THE STEMS PAINT AS PUBLISHED (architect 2026-10-03): an open editor's
    // refused commit recolors nothing, so no paint-time override stands
    // here — the stash's two phases are the marker's resolved class, the
    // whole answer, and the colors the palette's live roles.
    sink.begin(/*aliased=*/false);
    // THE TWO WELLS (the canvas's rows less the stereo divider's,
    // 2026-10-16): each stem one run per non-empty well, its phase counted
    // from the canvas's first row, so a dot keeps its row and the divider
    // carries none.
    const std::array<WaveformWellRows, 2> wells = waveform_well_rows(area);
    const ProgramSpec& ps = kProgramSpec;
    // THE DOTS WRITTEN AS PIXEL WORDS, NOT ONE RECTANGLE A DOT (architect
    // 2026-10-12, the marker slowdown at coarse zoom; 2026-10-13 the words:
    // cost cuts that paint the same pixels). The dots are
    // the dotted line's rule (render.h's dotted_waveform_line_runs) — rows
    // ≡ phase (mod period) from the canvas's top, one device px, at the
    // stem's column, in the two wells — and the same column gate applies on
    // both roads. Both roads are
    // the cairo sink's stem_dots (canvas_sink.cpp) since 2026-10-13, the
    // context's clip read once at stems_begin.
    //   THE DIRECT ROAD (2026-10-13): each dot's word stored straight into
    // the context's target inside its clip (direct_pixel_target says when
    // that is cairo's own result). The tablet's profile of a pan put 16 % of
    // all CPU here (31 % with the lamp dark) while the road below stood:
    // pixman pays its per-scanline setup on every row of a mask a pixel
    // wide, so one stem's two mask ops cost the canvas height twice over
    // for a dot every fourth row. THE WORD is argb32_opaque_word of the
    // color set_palette_source would set (display_color's n/255 channels),
    // which is the pixel cairo stores for that opaque source through a 255
    // mask over any ground; a 0 mask leaves the ground — so writing the dots'
    // words and nothing else, inside the clip and the surface, is the mask
    // op's pixels exactly.
    //   THE MASK ROAD, wherever the direct one is refused: a cached
    // one-px-wide A8 mask, laid at the stem's column with the source color;
    // an opaque source through a 0 / 255 mask is the rectangles' pixels
    // exactly.
    sink.stems_begin();
    const auto dotted_stem = [&](int col, int phase, GuiColor color) {
        if (col < 0 || col >= area.w || ps.cue_dot_period <= 0) return;
        for (const WaveformWellRows& w : wells) {
            if (w.y1 <= w.y0) continue;
            // stem_dots counts its phase from the run's first row: the
            // canvas-top phase restated from this well's first row.
            const int p = ps.cue_dot_period;
            const int well_phase = ((phase - (w.y0 - area.y)) % p + p) % p;
            sink.stem_dots(area.x + col, w.y0, w.y1 - w.y0, p, well_phase,
                           color);
        }
    };
    for (const MarkerStem& stem : app.marker_stems) {
        // Column-gate exactly like render_playhead's line does. The producers
        // already publish only columns in [0, w) (stem_column_on_waveform,
        // render.cpp); the gate restates that against the area this painter
        // is handed, so no entry can leak its column into the chrome beside
        // the waveform.
        const int col = static_cast<int>(std::nearbyint(
            stem.x - static_cast<double>(area.x)));
        if (stem.cue_phase_dots != GuiCueDot::None)
            dotted_stem(col, ps.cue_dot_phase,
                        cue_dot_color(stem.cue_phase_dots));
        if (stem.range_phase_dots != GuiCueDot::None)
            dotted_stem(col, ps.range_dot_phase,
                        cue_dot_color(stem.range_phase_dots));
    }
    sink.stems_end();
    sink.end();
}

// THE NUDGED PAIRS' RING RUNS (MarkerStemRun, render.h, owns the look;
// architect 2026-10-10 → 2026-10-26): the top and bottom of each `h`-view
// nudged pair's ring, on the canvas's first and last rows, between its two
// cues' stems — THE PHASE-RESET LEAD-IN RING'S RUNS' FORM EXACTLY
// (paint_phase_reset_overlay_ring, the model), IN THE STEM'S OWN PATTERN:
// one device px a dot (waveform_line_px, "on waveform → unscaled"), the
// `range` blue on columns left stem + range_dot_phase + k · cue_dot_period
// and the `cue` red on left stem + cue_dot_phase + k · cue_dot_period (a
// live cue's stem pair, live_cue_stem_dots — so the runs' density is a
// stem's, architect 2026-10-26), through the right stem's column,
// aliased integer cells, clipped to the area. Read as published, before
// the stems paint their sides (paint_marker_stems), whose dots never share
// a row with these (the stems' phases are rows 3 and 7 of 8).
// THROUGH THE CANVAS SINK (2026-10-13, canvas_sink.h): each color's runs,
// every pair's top and bottom, one dots call — one fill on the cairo road,
// as before — the columns from the left stem's own (the phase's origin)
// through the right stem's, cut to the area's columns.
void GuiPaintHandler::paint_marker_stem_runs(GuiCanvasSink& sink,
                                             const GuiRect& area) {
    if (app.marker_stem_runs.empty()) return;
    const ProgramSpec& ps = kProgramSpec;
    const int t      = waveform_line_px();
    const int period = ps.cue_dot_period;
    const int y_top  = area.y;
    const int y_bot  = area.y + area.h - t;
    const int clip_lo = area.x;
    const int clip_hi = area.x + area.w - t;   // the last whole dot's column
    sink.begin(/*aliased=*/true);
    std::vector<GuiCanvasDotRun> runs;
    runs.reserve(2 * app.marker_stem_runs.size());
    const auto paint_dots = [&](const GuiColor& color, int phase) {
        runs.clear();
        for (const MarkerStemRun& run : app.marker_stem_runs) {
            const int x0 = static_cast<int>(std::nearbyint(run.x0));
            const int x1 = static_cast<int>(std::nearbyint(run.x1));
            const int lo = std::max(x0, clip_lo);
            const int hi = std::min(x1, clip_hi);   // inclusive
            if (hi < lo) continue;
            runs.push_back(GuiCanvasDotRun{false, lo, y_top, hi + 1 - lo,
                                           period, x0, phase});
            runs.push_back(GuiCanvasDotRun{false, lo, y_bot, hi + 1 - lo,
                                           period, x0, phase});
        }
        sink.dots(runs, color);
    };
    const GuiStemDots dots = live_cue_stem_dots();
    if (dots.range_phase_dots != GuiCueDot::None)
        paint_dots(cue_dot_color(dots.range_phase_dots), ps.range_dot_phase);
    if (dots.cue_phase_dots != GuiCueDot::None)
        paint_dots(cue_dot_color(dots.cue_phase_dots), ps.cue_dot_phase);
    sink.end();
}

// -- GuiPaintHandler::paint_strip_drag_anchor ----------------------------

// Paints the anchor stem (the Ableton pivot affordance) at the zoom anchor's
// current column, full waveform height — a live zoom gesture's, or the S
// Pen's retained one between strokes — WHITE, in the scanner's role: the
// stems that belong to the controls are white and the playhead alone wears
// `playhead_stem` (architect 2026-10-09; render_strip_anchor_stem). TWO PRODUCERS,
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
// nothing at all (a survivor's pan refused under a modal state — the
// per-frame refusal asks state alone, the gesture's surface decided once at
// its down, architect 2026-10-16, apply_touch_nav_update).
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
void GuiPaintHandler::paint_strip_drag_anchor(GuiCanvasSink& sink,
                                              const GuiRect& area) {
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
    render_strip_anchor_stem(sink, area, col);
}

// -- GuiPaintHandler::paint_playheads ------------------------------------

void GuiPaintHandler::paint_playheads(GuiCanvasSink& sink,
                                      const GuiRect& area) {
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
    render_playhead(sink, area, px_x, palette().playhead_stem,
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
void GuiPaintHandler::paint_scanner(GuiCanvasSink& sink, const GuiRect& area) {
    if (!app.playhead_scanner_active) return;

    const PlateViewportBasis basis = plate_viewport_basis();
    const double scan_px =
        scanner_pixel_x(app, wf_cache.fp_vp_start, basis.spp);
    render_playhead(sink, area, scan_px, palette().scanner,
                    PlayheadForm::Solid);
}

// -- THE BOTTOM ROW'S MODAL STATE ----------------------------------------
//
// EVERY MODAL STANDS IN THE MODAL FAMILY'S BOX AND ON ITS CONTROLS ROW
// (architect 2026-10-14, modal_box.h, the one owner of the shape): a sunken
// box wearing the program ring from the view bar's top row down, and under
// it the controls row on row 8's rows. NOTHING ABOUT THE
// RENDERER MOVED for it — one window, one surface, one frame, one painter —
// so this is emphatically not the scrapped second-toplevel model
// (closed_questions.md carries that do-not-re-propose). WHILE ANY MODAL
// STANDS ROW 8 YIELDS WHOLE: all SEVENTEEN buttons — the VERB GROUP'S EIGHT,
// the step-and-walk group's four and the nudge-and-play group's five — plus the
// clock's time field and the state line beside it stand down, nothing
// negotiates for space, and the modal's controls take the content band they
// left (paint_modal_dialog, every modal's controls row; the box is
// paint_modal_box's, laid by paint_modal_dialog for the prompts, the dialog
// editors and the progress modal and by paint_folder_overlay for the render
// player and the picker). (HISTORY: from 2026-08-13 to 2026-10-14 the modal lived ON THIS
// ROW ALONE — the architect scrapping the centered box of 2026-08-12, "it
// looks sloppy — no compositor drop shadow, and faking one wouldn't work" —
// its rectangle moved from the window's centre into the lane the tenants
// left, and from 2026-10-10 the prompts and dialog editors stood in the
// taskbar dialog laid over the window's foot.)
//
// The two helpers below are that fork, shared by the row's painter (which
// wants the boolean, just under here) and by paint_modal_dialog (which wants
// the editor itself, at the tail of this file) — one answer, so the row
// cannot yield to a dialog the modal painter would then decline to paint.

// The dialog editor the modal would paint, or nullptr. The editors'
// precedence order. Only one dialog editor can be open at a time — every
// opener refuses while another owns the keyboard — so the order is free. A
// standing PROMPT outranks every editor and is the caller's own test, not
// this one's. (The `h` view's load editor and the Open project editor had
// arms here until 2026-08-28, when both became the field-less PICKER — a
// modal owner of its own, painted by the picker branch below, never an
// editor.) NO DIALOG EDITOR CARRIES A LABEL since 2026-10-14 (architect: the
// Settings editor's "Setting:" "pointless that far away. just remove it", the
// BPM editor's "BPM:" "drop"): the box is the edit control and the line is the
// whole of it.
//
// IT HANDS BACK A MUTABLE STATE for the painter's sake (the editor's own
// state is the caret and the selection it paints); the boolean caller below
// wants only the null test.
static text_editor::State* dialog_editor_to_paint(AppState& app) {
    if (text_editor::is_active(app.settings_editor))
        return &app.settings_editor;
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::BpmBracket)
        return &app.top_flag_editor;
    return nullptr;
}

// "A modal owns the bottom row" — the prompt, the RENDER PLAYER (the third
// owner since 2026-08-28: its transport row takes the lane whole), the PICKER
// (the fourth, the same day: its Cancel-alone row), THE PROGRESS MODAL (the
// fifth, 2026-10-14: a batch's Cancel alone, AppState::BatchProgress) or any
// dialog editor. The
// top-strip FLAG editor is deliberately absent: it is positional, a modal
// since 2026-10-25 (its veil swallows the row's presses) but not a dialog,
// and it never takes this row (on the glass the keyboard's band covers it).
// EVERY ONE OF THEM STANDS IN THE MODAL FAMILY'S BOX AND ON ITS CONTROLS ROW
// since 2026-10-14 (modal_box.h; a prompt and a dialog editor stood in the
// taskbar dialog laid over the row from 2026-10-10 until that day), so with
// no keyboard standing the row's rows ARE the family's controls row.
static bool modal_owns_bottom_row(AppState& app) {
    if (app.prompt.active) return true;
    if (app.batch_progress.active) return true;
    if (app.render_player.active) return true;
    if (app.picker.active) return true;
    // THE SCORE VIEWER (the sixth, 2026-10-26): its PREV / NEXT, scrub, page
    // cell and range keys take the lane whole, as the player's row does.
    if (app.score_viewer.active) return true;
    return dialog_editor_to_paint(app) != nullptr;
}

// (THE BORDER UNDER ROW 8 — paint_bottom_border, the dock bar's six rows on
// a bottom lane of their own under row 8, 2026-10-14, row 8's bars running
// through its light row from 2026-10-21 — IS REMOVED, architect 2026-10-22,
// the simple layout: row 8 stands on the window's foot.)

// -- GuiPaintHandler::paint_bottom_strip ---------------------------------

void GuiPaintHandler::paint_bottom_strip(cairo_t* cr) {
    // THE UNIFIED BOTTOM ROW (architect-ruled 2026-08-12, rows 8 and 9 merged
    // into ONE lane, on the WINDOW'S FOOT; THE STRIP since the simple layout,
    // 2026-10-22, bottom lane 0): the clock in its ringed time field, the
    // active tab's name and THE STATE LINE on the ground beside it at the
    // left, and — packed against the right edge —
    // the MARKER-VERB GROUP, the marker walk and the cardinal
    // arrows with Play last, each Cool Edit's group (the tables above own
    // those memberships). That is the whole roster. This painter
    // owns the lane's CHROME (its ground) and nothing else; the
    // buttons and the clock are paint_bottom_row_buttons_and_clock, called
    // from here onto the grounded band (the cluster's tables and the clock's
    // metrics live beside that body).
    // THE ROW HAS A MODAL STATE since 2026-08-13, in which those tenants
    // stand down and the modal family's controls row takes the row
    // (2026-10-14; the ruling and the fork are at modal_owns_bottom_row,
    // just above; the modal's own layout is paint_modal_dialog's) — and with
    // the chain gone there is nothing left on the lane to negotiate with,
    // which is what makes that yield clean.
    //
    // WHAT IS NOT HERE, and why it is not missing: an S/T · W/P view readout
    // and a "(read-only)" token. The icon row's view pairs show the view as
    // two lit buttons and its read-only toggle shows the lock, so letters would
    // restate what that row says in its own vocabulary — and the A / B tab
    // is the active tab's name beside the clock since 2026-10-22 (the tab
    // strip's lit tab 2026-10-18 to then, the clock cell's letter from the
    // tab row's deletion of 2026-10-01). The row carries no dirty mark:
    // Save's grey is the mark
    // (architect 2026-10-05, plain_save_actionable).
    //
    // The row paints on EVERY frame class (blank, loaded) like the
    // redesigned rows above it: with no piece — the open's hold's one frame,
    // platform.h's head — the clock's field stands empty and no tab is named
    // (clock_time_text, the name's block), and the buttons must be visible
    // on every frame class their press claim is live on.
    const GuiRect lane = bottom_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;

    // THE LANE'S GROUND, ON EVERY FRAME WHATEVER STANDS OVER IT: THE BRUSHED
    // GROUND (materials.h's GROUND, 2026-10-23; the plain wood of the simple
    // layout 2026-10-22, Cool Edit's dock bar before), the
    // lane the window's width on its foot. The row's tenants are the
    // program's (row 8, and the modal family's controls row — every modal's,
    // modal_box.h, 2026-10-14), so this lane never changes for them. The
    // ground erases whatever render_background laid down, so the strip does
    // not depend on that erase happening to hold the same value. The modals
    // paint their controls on this ground and lay none of their own
    // (paint_modal_dialog) — but under a dialog editor on the glass, whose
    // stack stands on the keyboard's band, the controls row is lifted off
    // this lane and laid by the family's painter (paint_modal_box).
    {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        const GuiMaterialScope ground(GuiMaterialRegion::Ground);
        paint_cell_rect(cr, lane, palette().face);
        cairo_restore(cr);
    }

    // THE ROW YIELDS TO THE MODAL (2026-08-13; the fork and the ruling are at
    // modal_owns_bottom_row just above). The ground above is the ROW'S
    // whatever it carries — the modal's CONTROLS ROW paints on the row's own
    // ground, laying none of its own (its box stands above the row, over the
    // canvas column, modal_box.h, 2026-10-14; from 2026-08-13 until then the
    // modal had no box and this lane was its whole surface) — and every other
    // tenant stands down here: paint_modal_dialog owns the row from this
    // frame until the modal's closer.
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
    // clock_invalidate_rect (main.cpp), which covers the modal's controls row
    // standing on it (the modal's whole surface from 2026-08-13 until its box
    // of 2026-10-14; the box takes the modal family's area,
    // invalidate_modal_dialog_area).
    // NOTHING READS THESE ZEROES as a bound — since 2026-08-13 nothing on
    // this row is measured from a button stash at all, the status chain having
    // taken its right anchor to the tab row with it.
    // AND THE ROW YIELDS UNDER THE ON-SCREEN KEYBOARD (2026-10-10, ON SCREEN
    // IS AS PAINTED): the keyboard's band stands on the window's foot,
    // always taller than the row, so row 8 is under
    // it whole — the flag editor's case, the one editor that raises the
    // keyboard with no dialog — and a button nothing shows must not hover,
    // wear a tooltip or answer a press (the keyboard's claim takes the press
    // first; the zero rects take the rest).
    if (modal_owns_bottom_row(app) || onscreen_keyboard::stands(app, gui)) {
        // The right block's groups stand down together: the SKIPS, the
        // MARKER VERBS, the STEP-AND-WALK GROUP, the NUDGE-AND-PLAY GROUP
        // with Play, and the `h` view's Older / Newer in the nudge pair's
        // seats. Each is a tenant of this lane like every other member, and
        // the modal takes the lane whole: the block's one table of groups
        // (kRow8Groups) and the stand-ins, every button on the row.
        for (const std::span<const TransportRowDef> group : kRow8Groups) {
            for (const TransportRowDef& def : group) {
                publish_button_face(cr, app, audio, playback, target_render,
                                    def.id,
                                    GuiRect{0, 0, 0, 0});
            }
        }
        for (const TransportStandIn& s : kTransportHistoryStandIns) {
            publish_button_face(cr, app, audio, playback, target_render,
                                s.stand_in.id,
                                GuiRect{0, 0, 0, 0});
        }
        app.clock_cell_rect = GuiRect{0, 0, 0, 0};
        // The cell's pixels are the modal's or the keyboard's now, so this
        // target's record of the clock no longer holds (GuiClockRecord).
        app.clock_painted[static_cast<std::size_t>(gui.painting_target())]
            .valid = false;
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
// TWO modal editors (settings / BPM; the commit-title editor was the third
// until its retirement 2026-10-10); the top-strip
// FLAG editor is deliberately NOT a dialog — it is positional, editing the
// marker where it stands, the unrolled flag (render_flag_editor_box), a
// modal since 2026-10-25 by its veil alone (keyboard_modal_editor_active).
//
// ONE RENDERER, ONE WINDOW, ONE FRAME — unchanged. Nothing here creates a
// surface, a window or a second buffer: only the modal's RECTANGLE moved,
// from the window's centre to the bottom row, so this is NOT the scrapped
// second-toplevel model (that attempt was reverted byte-exact and is recorded
// do-not-re-propose in closed_questions.md). The row yields whole while a modal
// stands — modal_owns_bottom_row, above, is the shared fork, and this body
// paints into the lane the row's tenants left.
//
// EVERY MODAL STANDS IN THE MODAL FAMILY'S BOX AND ON ITS CONTROLS ROW SINCE
// 2026-10-14 (architect, modal_box.h, the one owner of the shape; the
// render player and the picker that morning, the prompts and the dialog
// editors that afternoon — "we should stop doing" the taskbar dialog laid
// over the window's foot, 2026-10-10 to 2026-10-14): the controls on the
// controls row (row 8's rows on the ground, or the same row lifted onto
// the on-screen keyboard under a dialog editor on the glass), their outermost
// cases or buttons flush with the box's ring's outer edges
// (modal_controls_seats). CONTENT PER KIND:
//   A PROMPT — its question on the box's FIRST ROW at the list's row metrics,
//   on the box's one flat field (modal_box.h's head),
//   the chrome's text color, and
//   the answer buttons FLUSH RIGHT on the controls row in painted order, the
//   last one's right edge the ring's outer edge. Each button wears its
//   answer's PLAIN WORD ("Save", "Discard", "Cancel", "Retry", "Yes", "OK",
//   "Delete") with Windows' UNDERLINED ACCESS KEY (architect 2026-10-10,
//   Windows strict; OK and Cancel none) — the bracketed accelerators stay
//   retired, PromptState owning the label rule and the key match. ONE BUTTON
//   WEARS THE PASSIVE FOCUS FACE FROM THE RAISE (2026-08-13, superseding this
//   block's "no default face: this prompt system has no Enter answer, so
//   every button is plain") — the LAST, the Escape sentinel, on every prompt
//   but THE FOUR CONFIRMATION RAISES — the load's two, Revert's and,
//   since 2026-09-16, the phase reset paste's — which are raised on their
//   FIRST (PromptState's PromptInitialFocus owns the choice); Enter answers
//   whichever it is, the assignment site is a few dozen lines into the body
//   below and the whole supersession is at PromptState.
//   THE PROGRESS MODAL (2026-10-14, AppState::BatchProgress) — a prompt's
//   shape with a sentence for its question: "Rendering X of Y..." on the box's
//   first row on the field, and Cancel alone flush right on the controls row,
//   no access letter, raised with passive focus on it.
//   AN EDITOR — THE BOX IS ITS EDIT CONTROL:
//   the whole pending line on the box's flat field in the field's text,
//   from the box's first row at the list's row metrics, SOFT-WRAPPED at the
//   box's width and never scrolling (text_editor::soft_wrap_rows), NO LABEL
//   (the Settings and BPM editors share one shape);
//   then OK and Cancel flush right on the controls row, Cancel's right edge
//   the ring's outer edge and OK one button gap before it. The line is the
//   existing text_editor machinery — selection, caret, click-to-caret,
//   byte-identical editing — on the wrapped rows, and a refused Enter selects
//   the whole text, nothing recolored, its owner's card saying why (architect
//   2026-10-03; render.h's palette block, A REFUSED ENTER RECOLOURS NOTHING).
//   (THE SETTINGS CHOICE EDITOR's combo stood at the field's seat 2026-10-07
//   to 2026-10-14 and went with its rows' drop-downs.)
//
// EVERY BUTTON CARRIES A TOOLTIP (architect 2026-08-13: "we just do a tooltip
// just like the regular icon tooltips"), through the roster's own machinery
// end to end — the same Qt timing, the same box, the same painter, the same
// AppState::redesign_tooltip state, whose owner names either surface now. The
// TEXT is a word button's word alone since the underline carries its key
// (architect 2026-10-10, Windows strict), a glyph button's the player's own,
// published in the stash beside the rect; the WAIT is written by this surface's own hover walk
// (update_modal_dialog_hover), and as everywhere no wait starts under a held
// press (the model is at AppState::RedesignTooltip).
//
// THE BUTTONS ARE THE HI-FI SKIN'S KEYS (architect 2026-10-23): square, the
// program's case, laid out by modal_word_buttons and player_row_layout, each
// painted by the roster's key painter (paint_roster_case) — at rest, down
// under a live press, its LED lit for the row's one checked button — and its
// legend printed under it (paint_key_legend: a word button's word in
// capitals with its access key underlined, a player key its own legend).
// THE KEYBOARD'S FOCUS IS NOT PAINTED (the button walk's NO FOCUS FRAME,
// architect 2026-10-23). No hover face. (Windows' push buttons 2026-10-02 to
// 2026-10-22, the ebony case 2026-10-22 to 2026-10-23.)
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
// METRICS. The seats are the controls row's (modal_controls_seats: the box's
// ring's outer columns, no row-8 pad, no tail): every key the program's case
// at the case's seat, the keys abutting (modal_word_buttons, the player's
// player_row_layout). (The 6-W inter-button gap, the old popup's 8 laptop
// px, stood until 2026-10-23.) The text in the box stands 12 px from the
// ring at 300 % on both axes (modal_box.h's THE TEXT INSIDE THE BOX), one
// list row pitch a row.
// (The editor's one-row field — kModalFieldWidthPx 378 W authored, floored
// at 29 W, kModalFieldHeightPx 23 W tall with its text kModalFieldPadXPx 5 W
// in, its label before it and its horizontal travel — stood on the bottom
// row from 2026-08-12 to 2026-10-14.)
//
// THE PUBLICATION is the floating surfaces' own convention: this runs
// UNCONDITIONALLY from on_redraw (after the keyboard slot since 2026-10-14),
// rewrites AppState::modal_dialog and AppState::dialog_editor_text every run
// (zero/invalid with no dialog up), so the pointer path always reads what is
// actually on screen and a closed dialog strands nothing. `box` is the
// modal's surface — the modal family's whole area under a prompt or a dialog
// editor (modal_area_rect), the controls row under the render player and the
// picker — and it is what the hover invalidation and the damage ride.
// Damage: the openers invalidate the whole window (no surface exists before
// the first paint); a standing dialog editor's edits and caret blink take the
// box's interior alone (Viewport::invalidate_dialog_editor_box, the editor
// arm below painting nothing outside it); every closer rides
// invalidate_modal_dialog_area, which takes row 8's lane and, while a prompt
// or a dialog editor stands or was last painted, the modal family's whole
// area — the modal's surface, and the rect a closer owes (viewport.cpp).

namespace {

// (THE WORD BUTTONS' WINDOWS METRICS — the 6-W button gap, Windows' 75 x
// 23-W push button sized to its label between the spec's pads, the editor's
// one-row field's 23-W height and 5-W pad — stood 2026-10-02 to 2026-10-23;
// every word button is the program's square key since, modal_word_buttons
// below. Git history holds their derivations.)
// (THE FOCUS FRAME — kModalFocusFramePx, one relief line outside the
// keyboard-focused button, Windows' default-button frame, 2026-10-02 to
// 2026-10-23 — is not painted, architect 2026-10-23: the button walk's NO
// FOCUS FRAME.)
// THE PLAYER ROW'S GROUP SPACE — between its items (transport → scrub,
// scrub → clock, clock → the lamp): 8 Windows px, the roster's group gap it
// stood at from 2026-10-02 until the roster's groups became Cool Edit's
// (2026-10-09), kept as the player's own.
constexpr double kPlayerGroupGapPx    = 8.0;

// THE TWO WORD ROWS NO PROMPT TABLE HOLDS: a dialog editor's OK and Cancel,
// and the field-less owners' Cancel alone (the picker's, the progress
// modal's) — the painter's plans read these.
constexpr std::string_view kModalOkLabel     = "OK";
constexpr std::string_view kModalCancelLabel = "Cancel";

// THE WORD BUTTONS' LAYOUT — ONE FOR THE PAINTER AND THE PRESS: on `seats`
// (the controls row's, modal_controls_seats) the words `labels`, in painted
// order. IN THE HI-FI SKIN (architect 2026-10-23) EVERY WORD BUTTON IS A
// SQUARE KEY — the program's case at the case's seat (seats.case_*), its
// word printed under it in capitals as a legend (paint_key_legend's one
// seat) — the keys ABUTTING as the player's and the band's do, THE CLUSTER
// FLUSH RIGHT, its last key's right edge the row's right seat (the box's
// ring's outer edge); wider than the row it starts at the row's left seat.
// A legend never widens its key, so the layout reads the words' count alone.
// (Windows' push button — the label's run between the spec's pads, floored
// at 75 W, the button gap between — 2026-10-02 to 2026-10-23.)
struct ModalWordButtons {
    std::vector<GuiRect> rect;
};
ModalWordButtons modal_word_buttons(const BottomRowSeats& seats,
                                    std::span<const std::string_view> labels) {
    ModalWordButtons out;
    const int w = seats.case_w;
    for (std::size_t i = 0; i < labels.size(); ++i)
        out.rect.push_back(GuiRect{0, seats.case_y, w, seats.case_h});
    const int total = w * static_cast<int>(labels.size());
    int x = std::max(seats.left_x, seats.cases_right - total);
    for (GuiRect& r : out.rect) {
        r.x = x;
        x += r.w;
    }
    return out;
}

} // namespace

// THE RENDER PLAYER'S ROW LAYOUT (2026-10-21: lifted out of the modal
// painter's player branch; the key patches read the controls row's square
// key alone, program_key_seats below): each of the nine cases' left
// column in the plan's order, the clock's two fields and the scrub's span,
// on the controls row's seats (modal_controls_seats). The row's arrangement
// and its rulings are at the painter's player branch.
struct PlayerRowLayout {
    std::array<int, 9> x{};
    int field_pad = 0, field_w = 0, field_gap = 0;
    int scrub_x0 = 0, scrub_w = 0, clock_x0 = 0, clock_end = 0;
};
static PlayerRowLayout player_row_layout(const BottomRowSeats& seats) {
    PlayerRowLayout l;
    // THE CASES ABUT (architect 2026-10-22, "the buttons should be
    // touching"): nothing stands between two of the row's boxes, as the
    // band's and row 8's cases abut; THE GROUP SPACE between its items is
    // kPlayerGroupGapPx, its own since the roster's groups became Cool
    // Edit's (2026-10-09). The press reads the rects the button walk
    // publishes from this layout (modal_dialog_button_hit), half-open, so
    // two abutting cases share no pixel.
    const int group    = scaled_px(kPlayerGroupGapPx);

    // THE CLOCK'S TWO FIELDS ARE MEASURED BEFORE ANYTHING IS PLACED,
    // their width being one of the fixed terms the scrub's own is what is
    // left over. Each is a TIME FIELD (the shape, the face and the
    // fixed-width rule at kTimeShape's block), its cell the widest-digit
    // specimen alone (time_field_metrics, row 8's own memo), so neither
    // field nor anything right of it moves as the position runs or an
    // item of another length loads — the cell and its pads, as row 8's
    // clock is, in its face, the program face (architect 2026-10-26
    // ~12:10, "yes" to symmetry).
    const double cell_w = time_field_metrics().time_w;
    l.field_pad = time_field_pad_px();
    l.field_w   = time_field_w_px(cell_w);
    l.field_gap = scaled_px(kTimeFieldGapPx);
    const int clock_w = l.field_w + l.field_gap + l.field_w;

    // -- The walk: the fixed left run, the right-flushed PAIR (word
    //    buttons until 2026-09-01, glyph boxes since), and the scrub
    //    taking what is between them. --
    const int bw  = seats.case_w;
    const int cx0 = seats.left_x;
    int px = cx0;
    l.x[0] = px; px += bw;                 // Home (skip back)
    l.x[1] = px; px += bw;                 // Play / Pause
    l.x[2] = px; px += bw;                 // End (skip forward)
    l.scrub_x0 = px + group;

    // THE RIGHT-FLUSHED CLUSTER IS TWO GLYPH BOXES since 2026-09-01 (it
    // was Load in place · Close as WORDS, measured runs in label boxes):
    // both are the box's own square now, so the cluster is two abutting
    // boxes — right-flushed where THE CONTROLS ROW'S
    // CASES END, the last box's right edge the box's ring's outer edge
    // (architect 2026-10-14, modal_controls_seats' cases_right; row 8's
    // last case's from 2026-10-05; the other owners' clusters keep the
    // reserved ring).
    const int words_w  = bw + bw;
    const int words_x0 = std::max(cx0, seats.cases_right - words_w);
    // What the row owes AFTER the scrub: the group space, the clock
    // cell, the group space, the lamp, THE HIGHLIGHT PAIR AND THE UP A
    // FOLDER BUTTON, the four boxes abutting, and the right-flushed pair
    // directly after them (the 2026-08-29 spacing ruling, the word buttons'
    // own gap there, spent with the words on 2026-09-01).
    const int after_scrub =
        group + clock_w + group + 4 * bw;
    // THE FLOOR IS TWO THUMBS (2026-10-25; two 14-W handle boxes until
    // then) — a channel shorter than two of the thumbs that ride it is not a
    // channel. `scrub_w` is the CHANNEL's width; the thumb's center runs its
    // first column to its last and half the thumb overhangs each end, over
    // the group space either side (render.h's scrub block).
    l.scrub_w = words_x0 - after_scrub - l.scrub_x0;
    if (l.scrub_w < 2 * scrub_thumb_w_px()) l.scrub_w = 0;

    l.clock_x0  = l.scrub_x0 + (l.scrub_w > 0 ? l.scrub_w + group : 0);
    l.clock_end = l.clock_x0 + clock_w;
    l.x[3] = l.clock_end + group;          // Repeat one
    l.x[4] = l.x[3] + bw;                  // Highlight Next (Down)
    l.x[5] = l.x[4] + bw;                  // Highlight Previous (Up)
    l.x[6] = l.x[5] + bw;                  // Up a Folder
    l.x[7] = words_x0;                     // Load in place
    l.x[8] = words_x0 + bw;                // Close
    return l;
}

// THE SCORE VIEWER'S PAGE ROAD ROW LAYOUT (architect 2026-10-26,
// score_viewer.h's head, the face with no soundtrack): THE PLAYER'S SEATS — PREV and NEXT abutting at the row's left (the
// transport's), the page scrub after the group space, THE PAGE CELL where
// the player's clock stands, and RESET, START, END and CLOSE abutting flush
// right, the last key's right edge the row's — the scrub taking what is
// left, floored at two thumbs as the player's is. THE CELL'S WIDTH is fixed
// for the table: a time field round the widest-digit specimen of
// "<count> / <count>" at the table's page count's digits, so nothing moves
// as the pages turn or the range changes.
struct ScoreRowLayout {
    std::array<int, 6> x{};
    int scrub_x0 = 0, scrub_w = 0;
    int cell_x0 = 0, cell_w = 0, cell_pad = 0;
};
static ScoreRowLayout score_row_layout(const BottomRowSeats& seats,
                                       int page_count) {
    ScoreRowLayout l;
    const int group = scaled_px(kPlayerGroupGapPx);
    const GuiFont cfont = gui_font(GuiFace::Program);
    char widest = '0';
    double widest_w = -1.0;
    for (char d = '0'; d <= '9'; ++d) {
        const char one[2] = {d, '\0'};
        const double w = text_shape::memoized_run(cfont, one)->width_px;
        if (w > widest_w) { widest_w = w; widest = d; }
    }
    const std::string digits(std::to_string(std::max(1, page_count)).size(),
                             widest);
    l.cell_pad = time_field_pad_px();
    l.cell_w   = time_field_w_px(
        text_shape::memoized_run(cfont, digits + " / " + digits)->width_px);
    const int bw  = seats.case_w;
    const int cx0 = seats.left_x;
    l.x[0] = cx0;                          // PREV
    l.x[1] = cx0 + bw;                     // NEXT
    l.scrub_x0 = cx0 + 2 * bw + group;
    const int words_x0 = std::max(cx0, seats.cases_right - 4 * bw);
    l.scrub_w = words_x0 - (group + l.cell_w + group) - l.scrub_x0;
    if (l.scrub_w < 2 * scrub_thumb_w_px()) l.scrub_w = 0;
    l.cell_x0 = l.scrub_x0 + (l.scrub_w > 0 ? l.scrub_w + group : 0);
    for (int i = 0; i < 4; ++i) l.x[2 + i] = words_x0 + i * bw;  // RESET .. CLOSE
    return l;
}

// THE SCORE VIEWER'S MEDIA PLAYER ROW (architect 2026-10-26 night, the
// soundtrack's face, score_viewer.h's head): BACK, PLAY / PAUSE and FWD
// abutting at the row's left (the skips architect 2026-10-27, the render
// player's three transport seats), the scrub after the group space, THE
// CLOCK — the render player's two time
// fields, measured and spaced as its are (player_row_layout) — and RESET,
// START, END and CLOSE abutting flush right, the scrub taking what is left,
// floored at two thumbs as the player's is.
struct ScoreMediaRowLayout {
    std::array<int, 7> x{};
    int field_pad = 0, field_w = 0, field_gap = 0;
    int scrub_x0 = 0, scrub_w = 0, clock_x0 = 0, clock_end = 0;
};
static ScoreMediaRowLayout score_media_row_layout(const BottomRowSeats& seats) {
    ScoreMediaRowLayout l;
    const int group = scaled_px(kPlayerGroupGapPx);
    l.field_pad = time_field_pad_px();
    l.field_w   = time_field_w_px(time_field_metrics().time_w);
    l.field_gap = scaled_px(kTimeFieldGapPx);
    const int clock_w = l.field_w + l.field_gap + l.field_w;
    const int bw  = seats.case_w;
    const int cx0 = seats.left_x;
    l.x[0] = cx0;                          // BACK
    l.x[1] = cx0 + bw;                     // PLAY / PAUSE
    l.x[2] = cx0 + 2 * bw;                 // FWD
    l.scrub_x0 = cx0 + 3 * bw + group;
    const int words_x0 = std::max(cx0, seats.cases_right - 4 * bw);
    l.scrub_w = words_x0 - (group + clock_w + group) - l.scrub_x0;
    if (l.scrub_w < 2 * scrub_thumb_w_px()) l.scrub_w = 0;
    l.clock_x0  = l.scrub_x0 + (l.scrub_w > 0 ? l.scrub_w + group : 0);
    l.clock_end = l.clock_x0 + clock_w;
    for (int i = 0; i < 4; ++i) l.x[3 + i] = words_x0 + i * bw;  // RESET .. CLOSE
    return l;
}

// THE KEYS AT THIS GEOMETRY (the declaration, paint_handler.h): every key
// the rows lay out, in both states of the `h` view, each the layout's own
// (icon_row_seats with icon_row_walk, row8_block with row8_block_walk,
// modal_controls_seats' square key, the scrub's thumb,
// onscreen_keyboard::for_each_key) — the painters read the same.
std::vector<GuiKeySeat> program_key_seats(const AppState& app) {
    std::vector<GuiKeySeat> out;
    // A size seen twice (every roster key, the controls row's square key,
    // the keyboard's equal keys) is one seat, its depths the union.
    const auto add = [&](int w, int h, bool lamp, uint8_t depths) {
        if (w <= 0 || h <= 0) return;
        for (GuiKeySeat& o : out)
            if (o.w == w && o.h == h && o.lamp == lamp) {
                o.depths |= depths;
                return;
            }
        out.push_back(GuiKeySeat{w, h, lamp, depths});
    };
    // THE BAND: the left runs in both states, their cases cut at the left
    // runs' limit (a case wholly past it is never painted); the right run,
    // which stands in both. Every roster key at all three depths (a lamp's
    // checked one among them).
    const GuiRect lane = top_icon_row_area(app);
    if (lane.w > 0 && lane.h > 0) {
        const IconRowSeats seats = icon_row_seats(app, lane);
        for (const bool history : {false, true}) {
            std::vector<std::vector<IconRowDef>> runs;
            std::vector<RedesignButton>          absent;
            icon_row_walk(history, runs, absent);
            int x = seats.left_x;
            for (const std::vector<IconRowDef>& run : runs) {
                const int n = static_cast<int>(run.size());
                for (int i = 0; i < n; ++i)
                    if (seats.case_x(x, i) < seats.left_limit)
                        add(seats.btn_w, seats.btn_h, true, 0b111);
                x = seats.next_run_x(x, n);
            }
        }
        add(seats.btn_w, seats.btn_h, true, 0b111);
    }
    // ROW 8'S RIGHT BLOCK, in both states.
    const GuiRect row8 = bottom_row_area(app);
    if (row8.w > 0 && row8.h > 0) {
        const BottomRowSeats seats = bottom_row_seats(app, row8);
        const Row8Lengths lengths = row8_lengths();
        for (const bool history : {false, true})
            row8_block_walk(
                history, row8_block(app, history, row8), lengths,
                [&](const TransportRowDef&, int) {
                    add(seats.case_w, seats.case_h, true, 0b111);
                },
                [](const TransportRowDef&) {});
    }
    // THE CONTROLS ROW'S SQUARE KEY, on the row's seats with no keyboard
    // and with one: the render player's nine (its lamp, Repeat One, at all
    // three depths with the rest; the player never stands over the
    // keyboard) and every word button, every one the row's square key
    // (modal_word_buttons; never checked: rest and held).
    for (const int keyboard_h : {0, onscreen_keyboard::surface_height_px()}) {
        const BottomRowSeats ps = modal_controls_seats(app, keyboard_h);
        add(ps.case_w, ps.case_h, true, keyboard_h == 0 ? 0b111 : 0b101);
    }
    // THE SCRUB'S THUMB: at rest, no LED (paint_scrub_thumb).
    add(scrub_thumb_w_px(), scrub_thumb_h_px(), false, 0b001);
    // THE ON-SCREEN KEYBOARD'S KEYS, on a platform that has it: every key of
    // the three pages at rest and held, the two lamps' keys (Shift, the
    // symbol-mode key) armed too — no LED (paint_onscreen_keyboard).
    if (GuiPlatform::wants_onscreen_keyboard())
        for (int p = 0; p < onscreen_keyboard::kPageCount; ++p)
            onscreen_keyboard::for_each_key(
                app, static_cast<onscreen_keyboard::Page>(p),
                [&](uint32_t, const onscreen_keyboard::KeyDef& k,
                    const GuiRect& r) {
                    using Role = onscreen_keyboard::Role;
                    const bool armable =
                        k.role == Role::Shift || k.role == Role::SymbolMode;
                    add(r.w, r.h, false, armable ? 0b111 : 0b101);
                });
    return out;
}

// (THE WORD BUTTONS AND THE KEYS AT THIS GEOMETRY — word_and_key_case_rects,
// the ebony's patches for every answer set's cluster and every keyboard
// page's keys, 2026-10-22 to 2026-10-23 — went with the ebony; the key
// patches seat their sizes again, program_key_seats above.)


// THE MODAL FAMILY'S BOX'S PAINTER (defined with the folder overlay's
// painter below, its other caller; `hole` the GL canvas's page hole, the
// definition's head).
static void paint_modal_box(cairo_t* cr, const AppState& app,
                            int keyboard_h,
                            const GuiRect& hole = GuiRect{0, 0, 0, 0});

namespace {

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
    // stash, for the player's and the score viewer's rows' as-painted
    // publication at the button walk below (the rule is there).
    struct PrevFace {
        AppState::PlayerButtonAct player_act;
        AppState::ScoreButtonAct  score_act;
        bool                      enabled;
    };
    std::vector<PrevFace> prev_enabled;
    prev_enabled.reserve(dlg.buttons.size());
    for (const AppState::ModalDialogButton& pb : dlg.buttons)
        prev_enabled.push_back(PrevFace{pb.player_act, pb.score_act,
                                        pb.enabled});
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
    // THE PROGRESS MODAL, the fifth owner (2026-10-14, AppState::
    // BatchProgress): directly under the prompt and above the three lower
    // ranks, none of which stands beside it (ModalDialogOwner's block).
    const bool progress_up = !prompt_up && app.batch_progress.active;
    // THE RENDER PLAYER, the third owner (2026-08-28): under the prompt like
    // the editors, and never live beside one (its opener refuses under any
    // editor, its key router consumes every opener).
    const bool player_up = !prompt_up && !progress_up &&
                           app.render_player.active;
    // THE PICKER, the fourth owner (2026-08-28): the same rank as the player,
    // the two never live together (each opener refuses under the other) and
    // neither live beside an editor. THIS IS ONE OF THE THREE PLACES THE
    // RANKING IS SPELLED (AppState::modal_dialog_live_session and
    // modal_dialog_stash_current are the others) and they must agree or the
    // owner-tag doctrine breaks.
    const bool picker_up = !prompt_up && !progress_up && !player_up &&
                           app.picker.active;
    // THE SCORE VIEWER, the sixth owner (2026-10-26): the lower ranks' rank,
    // never live beside the player, the picker or an editor (no road opens
    // it under any of them — their routers and veils consume bare `m` and
    // grey its key — and its router consumes their openers).
    const bool score_up = !prompt_up && !progress_up && !player_up &&
                          !picker_up && app.score_viewer.active &&
                          app.score.has_value();
    text_editor::State* ed =
        (prompt_up || progress_up || player_up || picker_up || score_up)
            ? nullptr : dialog_editor_to_paint(app);
    if (!prompt_up && !progress_up && !player_up && !picker_up &&
        !score_up && ed == nullptr) {
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

    // THE MODAL'S SURFACE IS THE MODAL FAMILY'S (architect 2026-10-14,
    // modal_box.h): every owner's controls stand on THE CONTROLS ROW — row
    // 8's rows under the box, on the row's own wood,
    // which paint_bottom_strip lays on every frame class; or, under a dialog
    // editor on the glass, the same row lifted onto the on-screen keyboard's
    // band (`kb_h`, onscreen_keyboard::modal_keyboard_h_px), its face the
    // family's painter's. A PROMPT AND A DIALOG EDITOR (`boxed`) ALSO TAKE
    // THE BOX: this body paints it (paint_modal_box) — one flat field for a
    // prompt's question and an editor's line alike — where the render player's
    // and the picker's list is the folder overlay's painter's. (THE TASKBAR
    // DIALOG, the prompts' and the editors' surface from 2026-10-10 until
    // that day — the chrome's ground laid over row 8, the border and the
    // frame's bottom row under one Hilight line, its controls centered in it
    // or standing on the keyboard — is retired: "we should stop doing that".)
    // THE PROGRESS MODAL TAKES THE BOX TOO (2026-10-14), on the same field.
    // THE SCORE VIEWER TAKES THE BOX TOO (2026-10-26), its page the
    // interior (the score branch below).
    const bool    boxed   = prompt_up || progress_up || ed != nullptr ||
                            score_up;
    const int     kb_h    = onscreen_keyboard::modal_keyboard_h_px(app, gui);
    const GuiRect lane    = bottom_row_area(app);
    const GuiRect content = modal_controls_row_rect(app, kb_h);
    if (lane.w <= 0 || lane.h <= 0 || content.h <= 0) {
        // A degenerate row publishes nothing, the cold answer the pointer path
        // already reads correctly (a zero stash contains no point) — and it
        // names no buttons, so the face state goes with it.
        reset_modal_dialog_face_state(app);
        return;
    }

    cairo_save(cr);
    const GuiFont font = gui_font(GuiFace::Body);
    // THE HI-FI SKIN (architect 2026-10-23; materials.h's REGIONS):
    // the controls row stands on the brushed GROUND; the box, every field,
    // the scrub's lines and every key flat ("the user shouldn't be typing on
    // a non-flat color").
    const GuiMaterialScope ground(GuiMaterialRegion::Ground);

    // THE BOX, for a prompt and a dialog editor (the surface block above):
    // laid first, the controls after it on the row it leaves. UNDER THE GL
    // CANVAS THE SCORE VIEWER'S INTERIOR IS THE FRAME'S HOLE (architect
    // 2026-10-27, the GPU page: the clip on_redraw's canvas block chose at
    // the frame's first rect, GuiCanvasPage::stands — the score branch's own
    // condition, score_viewer_page_stands), which the box clears.
    GuiRect page_hole{0, 0, 0, 0};
    if constexpr (kGuiCanvasDrawList) {
        const GuiCanvasDrawList& frame = gui_canvas_frame();
        assert(frame.page.stands == score_up);
        if (frame.page.stands) page_hole = frame.clip;
    }
    if (boxed) paint_modal_box(cr, app, kb_h, page_hole);

    // The deleted toolbar row's button box, owned by the dialog since the
    // 2026-08-12 relayout (the spec's push_button_box_px — the record at the
    // kModal* block; modal_word_buttons the one layout) — on every
    // owner's row but THE PLAYER'S, whose buttons are row 8's case, the
    // program's, at row 8's seat (architect 2026-10-05). EVERY OWNER'S SEATS
    // ARE THE FAMILY'S CONTROLS ROW'S (modal_controls_seats, architect
    // 2026-10-14: the outermost cases and buttons flush with the box's ring;
    // the prompts and the editors took row 8's pads until that afternoon).
    // `btn_y` and `btn_h` are the one band the walk and the scrub read.
    const BottomRowSeats seats = modal_controls_seats(app, kb_h);
    // EVERY OWNER'S BUTTON IS THE KEY since 2026-10-23 (the word buttons
    // square keys, modal_word_buttons), so one band for all.
    const int btn_h = seats.case_h;

    // -- The buttons' words and widths, shaped up front (the layout needs the
    //    row's total before anything can be placed). --
    // A BUTTON IS A WORD OR A KEY (2026-08-28, the render player's row):
    // `glyph` set means the button is one of the player's keys, a blank key
    // with its printed legend (render_player_button_legend) — the transport
    // triple, the REPEAT ONE lamp, the HIGHLIGHT PAIR (architect 2026-10-10,
    // Highlight Next and Highlight Previous, the band walk's two keys on
    // glass), UP A FOLDER, LOAD IN PLACE (or DELETE at the root) and CLOSE —
    // while a word button keeps the label box every prompt and editor button
    // has always had. The word kind is the other three owners' alone: a
    // prompt's answers, the dialog editors' OK / Cancel and the picker's
    // Cancel. `lit` is the roster's SELECTED face — the row's one lamp,
    // Repeat one, and no other button has a state to say. `player_act` is the
    // player's dispatch vocabulary; the other two stay zero/false on its
    // buttons.
    struct DialogButtonPlan {
        std::string label;
        // The access key's byte in `label`, -1 for none (Windows strict,
        // architect 2026-10-10): a prompt answer's from its table; every
        // other owner's word button (OK, Cancel) and every glyph has none.
        int         access       = -1;
        char        response_key = 0;
        bool        editor_ok    = false;
        AppState::PlayerButtonAct player_act = AppState::PlayerButtonAct::None;
        // The score viewer's dispatch vocabulary (2026-10-26); its keys are
        // `glyph` keys too, their legends score_viewer_button_legend's.
        AppState::ScoreButtonAct  score_act  = AppState::ScoreButtonAct::None;
        bool        glyph        = false;
        bool        lit          = false;
        // The player's Play key's face (render_player_play_face), its
        // legend's fork (2026-10-25); Play on every other button.
        PlayerPlayFace play_face = PlayerPlayFace::Play;
        // The disabled face (architect 2026-08-30, the player's row) — the
        // live predicate's answer at plan time, published on the record
        // below. TWO OWNERS WRITE IT, re-grepped 2026-10-27: the player's
        // nine through render_player_button_enabled and the score viewer's
        // keys through score_viewer_button_enabled; true on every other
        // owner's buttons.
        bool        enabled      = true;
        std::string tooltip;     // the player's own; empty = the composer's
        std::string tooltip2;    // the modifier line; empty = the one-line form
        int         w            = 0;
        // WHERE IT PAINTS. The single left-flushed cluster every other owner
        // lays out is written into this field by the walk below; THE PLAYER'S
        // ROW IS NOT ONE CLUSTER (transport, separator, scrub, clock,
        // separator, the lamp, the highlight pair, the Up a Folder button —
        // then the last two FLUSH RIGHT), so its own branch writes all nine
        // and the walk simply paints where it is told.
        int         x            = 0;
    };
    std::vector<DialogButtonPlan> plan;
    if (prompt_up) {
        for (const PromptAnswer& a : app.prompt.answers) {
            DialogButtonPlan b;
            b.label        = a.label;
            b.access       = a.access;
            b.response_key = a.key;
            plan.push_back(std::move(b));
        }
    } else if (player_up) {
        // THE PLAYER'S ROW, in painted order (architect 2026-08-28; the
        // transport is THE MAIN WINDOW'S OWN TRIPLE since 2026-09-01): the
        // two SKIPS around Play/Pause, on Home and End since 2026-08-31 —
        // then the scrub and the clock (both laid out in the player's own
        // branch below, the separators either side of them retired
        // 2026-10-02), then the REPEAT ONE
        // lamp, the HIGHLIGHT PAIR (Highlight Next, Highlight Previous: the
        // bare Down / Up keys' band walk on a button, architect 2026-10-10),
        // the UP A FOLDER button, and the last two FLUSH RIGHT: Load in place ·
        // Close.
        // Close LAST, the escape sentinel by construction as every prompt has
        // it. The plan's order is also the ring's, so Tab walks the row left to
        // right. A STOP BUTTON SAT AFTER PLAY/PAUSE from 2026-08-28
        // (Audacious's own order) until the act retired.
        // THE PAUSE FACE IS TRUTHFUL (architect 2026-08-31): the button
        // prints PAUSE only where its press would PAUSE — the transport live
        // AND the highlight on the transport's own item (or nowhere), which
        // is exactly render_player_highlight_act_row answering −1. Under a
        // highlight standing on a folder row or on another wav the press
        // would GO THERE, so the face is PLAY's and the tooltip's word
        // follows it. SINCE 2026-09-01 THE FACE AND THE WORD ARE ONE OWNER,
        // render_player_play_face (the act's own two forks read without
        // acting); the hint takes
        // the whole answer, so a folder row says "Open folder" and a live
        // transport "Pause". THE HINTS' OTHER STATE BITS ARE RESOLVED HERE
        // TOO (RenderPlayerHintState, app_state.h — each a predicate the act
        // or the face reads; the previous-track window needs the position,
        // which this painter already reads for the clock). Every bit
        // reaches a repaint through the row's one damage owner: the three
        // highlight movers and the transport writers damage the row already,
        // for the Load in place face and the play key's legend.
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
        // THE PLAYER'S KEYS (`glyph` the key fork: a blank key with its
        // printed legend, no drawing since the hi-fi skin, 2026-10-23).
        auto glyph_button = [&](AppState::PlayerButtonAct act,
                                bool lit = false) {
            DialogButtonPlan b;
            b.player_act = act;
            b.glyph      = true;
            b.lit        = lit;
            b.play_face  = hint.play_face;
            b.enabled    = render_player_button_enabled(app, playback, act);
            b.tooltip    = render_player_button_hint(act, hint);
            b.tooltip2   = render_player_button_shift_hint(act, hint);
            plan.push_back(std::move(b));
        };
        // (THE `word_button` LAMBDA IS DELETED — 2026-09-01: the player's
        // row is keys alone and no word button. The other three owners' word
        // buttons are built in their own branches below.)
        glyph_button(AppState::PlayerButtonAct::Home);
        // PLAY/PAUSE: its legend follows the play face (above); this is a
        // player's transport, where a pause is a resumable rest. The roster's
        // transport button is untouched — bare Space over the project's audio
        // is one toggle with no pause state.
        glyph_button(AppState::PlayerButtonAct::PlayPause);
        glyph_button(AppState::PlayerButtonAct::NextTrack);
        glyph_button(AppState::PlayerButtonAct::RepeatOne,
                     app.render_player.repeat_one);
        // THE HIGHLIGHT PAIR (architect 2026-10-10): the render player on
        // glass has no arrow keys, so the bare Down / Up keys' band walk is a
        // pair of buttons — DOWN THEN UP, the order of row 8's own pair
        // (TransportDown, TransportUp) — that move the highlight one row and
        // nothing else (GuiRenderPlayer::move_highlight), so a file is picked
        // silently and then loaded in place or played.
        glyph_button(AppState::PlayerButtonAct::HighlightDown);
        glyph_button(AppState::PlayerButtonAct::HighlightUp);
        // UP A FOLDER, THE `..` ROW'S ACT ON A BUTTON (architect 2026-09-01, with the
        // player's move inside `tmp/`): the listings carry no `..` row any
        // more, so the way out of a batch folder is here — beside the lamp,
        // ahead of the right-flushed pair. It greys at the root through the
        // act's own wall.
        glyph_button(AppState::PlayerButtonAct::Up);
        // THE LAST TWO, FLUSH RIGHT: Load in Place (the icon row's history
        // group runs the same act one surface over) and Close. Close stays
        // LAST, the escape sentinel by construction.
        // AT THE ROOT THE SLOT IS DELETE (architect 2026-09-29): `tmp/`'s own
        // listing is batch folders, where a load has nothing to name, so the
        // sixth button deletes the highlighted folder, and its shift press
        // every folder listed; its shift line stands only where "all" is more
        // than the highlighted one. Inside a batch folder the slot is Load in
        // Place, unchanged.
        hint.delete_all_differs = render_player_root_folder_count(app) > 1;
        if (app.render_player.folder == AppState::RenderPlayer::Folder::Root)
            glyph_button(AppState::PlayerButtonAct::Delete);
        else
            glyph_button(AppState::PlayerButtonAct::LoadInPlace);
        glyph_button(AppState::PlayerButtonAct::Close);
    } else if (score_up) {
        // THE SCORE VIEWER'S ROW (architect 2026-10-26), in painted order, ON
        // ITS TWO FACES (score_viewer.h's head, 2026-10-26 night): the page
        // road's PREV and NEXT, or the media player's BACK, PLAY / PAUSE and
        // FWD — a deck's order, the render player's transport triple (Home,
        // PlayPause, NextTrack above), the skips architect 2026-10-27, each
        // with its modifier line (score_viewer_button_shift_hint); PLAY's
        // legend and its word the render player's Play key's by the face
        // (score_viewer_play_face) — then (the scrub and the page cell or the
        // clock between, laid out in the branch below) RESET, START, END and
        // CLOSE — each a key with its printed legend, its face the act's own
        // truthful answer.
        using Act = AppState::ScoreButtonAct;
        const PlayerPlayFace face = score_viewer_play_face(app);
        const auto key = [&](Act act) {
            DialogButtonPlan b;
            b.score_act = act;
            b.glyph     = true;
            b.play_face = face;
            b.enabled   = score_viewer_button_enabled(app, playback, act);
            b.tooltip   = score_viewer_button_hint(act, face);
            b.tooltip2  = score_viewer_button_shift_hint(app, playback, act);
            plan.push_back(std::move(b));
        };
        if (score_viewer_has_soundtrack(app)) {
            key(Act::SkipBack);
            key(Act::PlayPause);
            key(Act::SkipForward);
        } else {
            key(Act::PrevPage);
            key(Act::NextPage);
        }
        for (const Act act : {Act::Reset, Act::SetStart, Act::SetEnd,
                              Act::Close})
            key(act);
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
        // THE PROGRESS MODAL'S ROW IS **Cancel** ALONE TOO (architect
        // 2026-10-14): the batch's one act a user owns is stopping it, and its
        // lift is the batch's cancel road (dispatch_modal_dialog_button's
        // progress arm). No access letter: Cancel is Esc, Windows strict.
        if (!picker_up && !progress_up) {
            DialogButtonPlan ok;
            ok.label     = std::string(kModalOkLabel);
            ok.editor_ok = true;
            plan.push_back(std::move(ok));
        }
        DialogButtonPlan last;
        last.label = std::string(kModalCancelLabel);
        plan.push_back(std::move(last));
    }
    // EVERY BUTTON'S BOX: a glyph button the PROGRAM'S CASE at row 8's width
    // (the player's alone, its nine the plan's only glyph buttons, placed by
    // its own branch below); every other owner's row ONE CLUSTER OF WORD
    // BUTTONS, laid out whole — widths, the flush-right start and each x —
    // by modal_word_buttons, the one layout the painter and the press read.
    if (player_up || score_up) {
        for (DialogButtonPlan& b : plan) b.w = seats.case_w;
    } else {
        std::vector<std::string_view> labels;
        for (const DialogButtonPlan& b : plan) labels.push_back(b.label);
        const ModalWordButtons words = modal_word_buttons(seats, labels);
        for (std::size_t i = 0; i < plan.size(); ++i) {
            plan[i].x = words.rect[i].x;
            plan[i].w = words.rect[i].w;
        }
    }

    // A PROMPT IS RAISED WITH PASSIVE FOCUS ON A BUTTON (architect 2026-08-13,
    // the ruling that gave this prompt system an Enter answer — PromptState
    // owns the supersession and the two facts that make it safe, the first of
    // which is that the last button is always the ESCAPE SENTINEL). WHICH
    // button is the RAISE'S OWN CHOICE (PromptInitialFocus, carried on the
    // question since 2026-08-28): the last on every prompt but THE FOUR
    // CONFIRMATION RAISES — the load's two, Revert's and, since
    // 2026-09-16, THE PHASE RESET PASTE'S — which ask for
    // their FIRST, each being already the deliberate second step of an
    // explicit act, so its Enter confirms it.
    // The load's one prompt body carries both `'` subjects (the player's entry
    // and the `h` view's walk member), so the two roads answer Enter alike,
    // and the revert and the phase reset paste answer it as they do; the two
    // three-way Save / Discard / Cancel prompts, which confirm no act already
    // asked for, the render player's Delete and the pull's confirmation
    // (PromptInitialFocus owns why) are what keep the last button. This is the
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
    // THE PROGRESS MODAL IS RAISED WITH PASSIVE FOCUS ON ITS CANCEL
    // (2026-10-14), a prompt's LastButton raise on a one-button row: Windows'
    // progress dialog, whose Cancel is its default button, so bare Enter or
    // Space presses it and cancels at the key's release, as Esc cancels at
    // its press. The same one assignment edge as the prompt's (a fresh
    // session), so a key arm a previous surface left cannot carry over.
    if (face_state_reset && progress_up && !plan.empty()) {
        app.modal_dialog_focus        = static_cast<int>(plan.size()) - 1;
        app.modal_dialog_focus_active = false;
    }

    // -- THE CONTROLS ROW'S LAYOUT (architect 2026-10-14, modal_box.h) --
    //
    // THE ALIGNMENT RULE: the WORD BUTTONS SIT FLUSH RIGHT on every owner's
    // row — a prompt's answers, an editor's OK and Cancel and the picker's
    // Cancel — the last one's right edge the box's ring's outer edge (x 2280
    // at 300 %), today's button gap between them; the render player's row
    // reads from the left, its transport first and its right pair flush
    // right (its branch below). (From 2026-08-13 a prompt's message and an
    // editor's label and field sat FLUSH LEFT on the bottom row with their
    // buttons after them — "your eyes have to go the whole distance of the
    // screen" — and from 2026-08-29 the picker's list kept its button flush
    // right, "if I'm using the picker my hand stays on the right"; since the
    // message and the line moved into the box over the row, every word row is
    // the list's.)
    //
    // THE BUTTONS STAY WHOLE: in the pathological case where they are wider
    // than the row, the cluster starts at the row's left seat and runs past
    // the right one (modal_word_buttons).
    //
    const int cx1 = seats.right_x;                      // one past the row's right seat
    // EVERY BUTTON IS CENTRED ON THE CASE ROWS (architect 2026-10-14, the
    // family's controls row): the push button on the rows the player's cases
    // take, 23 W on 23 W under win2000, so the rows coincide
    // (modal_word_buttons). (Centered in the bottom row's content band, the
    // taskbar dialog's centered band before it, until that day.)
    const int btn_y = seats.case_y;

    // THE TEXT IN THE BOX — a prompt's question, an editor's line — 12 PX
    // OF PLAIN FIELD INSIDE THE RING ON BOTH AXES at 300 % (modal_box.h's
    // THE TEXT INSIDE THE BOX, 2026-10-23): the pen at the text room's x,
    // the wrap its width, the first row's baseline modal_box_text_baseline0
    // (the "l"'s ink top the pad under the ring's own px), each further row
    // one list row pitch below.
    // The rows keep the list's band (row_h) for the published geometry, the
    // first row's band placed so redesign_baseline lands on that baseline.
    const GuiRect box_rect   = boxed ? modal_box_rect(app, kb_h)
                                     : GuiRect{0, 0, 0, 0};
    const GuiRect interior   = boxed ? modal_box_interior_rect(app, kb_h)
                                     : GuiRect{0, 0, 0, 0};
    const GuiRect text_room  = modal_box_text_room(box_rect);
    const int     text_x0    = text_room.x;
    const int     text_w     = text_room.w;
    const int     row_h      = folder_overlay::button_row_height_px();
    const int     row_pitch  = row_h + folder_overlay::button_row_gap_px();
    const double  baseline0  = modal_box_text_baseline0(font, box_rect);
    const int     row0_y     = static_cast<int>(baseline0) -
                               static_cast<int>(redesign_baseline(font, 0.0,
                                                static_cast<double>(row_h)));
    // ONE LINE ON THE BOX'S FIRST ROW, in the chrome's text on the face —
    // a prompt's question and the progress modal's sentence alike — CLIPPED
    // at the text room's right edge, the pad short of the ring: a line longer
    // than the box's width is cut there, the buttons under it on the
    // controls row always whole.
    const auto paint_first_row_line = [&](const std::string& text) {
        const text_shape::ShapedRun msg =
            text_shape::shape_text_run(font, text);
        if (text_w <= 0 || interior.h <= 0) return;
        cairo_save(cr);
        cairo_rectangle(cr, text_x0, interior.y, text_w, interior.h);
        cairo_clip(cr);
        set_palette_source(cr, palette().label);
        text_shape::show_shaped_run(cr, msg, static_cast<double>(text_x0),
                                    baseline0);
        cairo_restore(cr);
    };

    // THE SCRUB'S ITEM — the channel and the thumb, painted and published
    // (dlg.scrub, dlg.scrub_thumb_x) — ONE BODY FOR ITS TWO OWNERS, the
    // render player's play-scrub and the score viewer's page scrub
    // (2026-10-26, lifted out of the player's branch): the channel at
    // `scrub_x0` and `scrub_w` columns on the controls row's case band, the
    // thumb at the column `thumb_x_of()` answers once the item is published.
    const auto paint_scrub_item = [&](int scrub_x0, int scrub_w,
                                      const auto& thumb_x_of) {
            // THE CHANNEL, the layout's span, and THE ITEM round it: the
            // channel widened by the thumb's left half before its first
            // column and its right half after its last (the thumb's box from
            // center − w / 2, render_player_scrub_handle_hit's and the
            // painter's own left edge), so item.w = channel − 1 + thumb.
            const int thumb_w = scrub_thumb_w_px();
            const GuiRect channel{scrub_x0, btn_y, scrub_w, btn_h};
            const GuiRect track{scrub_x0 - thumb_w / 2, btn_y,
                                scrub_w - 1 + thumb_w, btn_h};
            dlg.scrub = track;
            // THE THUMB'S ROWS, CENTERED IN THE TRACK'S BAND, and THE CHANNEL
            // at its seat inside them: the thumb is its rows above the
            // channel, the channel's own four lines and its rows below, each
            // part rounded on its own.
            const int channel_h = scrub_channel_h_px();
            const int above     = scrub_thumb_above_px();
            const int thumb_h   = scrub_thumb_h_px();
            const int thumb_y   = track.y + (track.h - thumb_h) / 2;
            // THE THUMB'S COLUMN — THE THUMB ALWAYS STANDS (2026-10-25,
            // render.h's scrub block; it stood only while an item was
            // decoded until then) — is the owner's, asked once the item is
            // published (its mapping reads dlg.scrub): the player's at its
            // branch, the score viewer's at its own.
            const int hx = thumb_x_of();
            // THE CHANNEL (render.h's scrub block, 2026-10-23): a ONE-DEVICE-PX
            // line of the ring's light tone over one of its dark tone, across
            // the channel's width, nothing filled.
            {
                const GuiMaterialScope flat(GuiMaterialRegion::Flat);
                const int px = hairline_px();
                paint_cell_rect(cr, GuiRect{channel.x, thumb_y + above,
                                            channel.w, px},
                                palette().ce_hilight);
                paint_cell_rect(cr, GuiRect{channel.x, thumb_y + above + px,
                                            channel.w, channel_h - px},
                                palette().ce_dark);
            }
            {
                // THE THUMB — the key-shaped block (render.h's scrub block:
                // a key at rest, no LED, no legend, 8 x 16 W), centered on the
                // column and on the channel, its box inside the item at
                // every column of the travel. No hover face: its painted rect
                // is the press's business (render_player_scrub_handle_hit)
                // and the cursor its cue.
                const GuiRect thumb{hx - thumb_w / 2, thumb_y, thumb_w,
                                    thumb_h};
                {
                    const GuiMaterialScope flat(GuiMaterialRegion::Flat);
                    paint_scrub_thumb(cr, thumb);
                }
                // THE COLUMN IS PUBLISHED AS PAINTED (the field's rule,
                // AppState::ModalDialogGeometry::scrub_thumb_x), with no item
                // too: a clip that covers the item — the channel and the
                // thumb's whole travel, its overhangs included — drew the
                // whole slider, so this column is
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
    };

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
        // THE QUESTION ON THE BOX'S FIRST ROW (architect 2026-10-14, the
        // progress mock's shape), the one-line rule above.
        paint_first_row_line(app.prompt.text);
    } else if (progress_up) {
        // -- THE PROGRESS MODAL (architect 2026-10-14, AppState::
        //    BatchProgress): its sentence, "Rendering X of Y...", on the box's
        //    first row at the prompt's own seat — the mock's subject — and
        //    Cancel alone flush right on the controls row (the walk below).
        //    The sentence repaints as each entry starts
        //    (GuiInputHandler::set_batch_progress_index's damage). --
        dlg.owner = AppState::ModalDialogOwner::Progress;
        paint_first_row_line(app.batch_progress.sentence());
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
        //    carried TWO glyph boxes then — and LATER THAT DAY THE PAIR ITSELF
        //    BECAME GLYPHS. THE HIGHLIGHT PAIR (architect 2026-10-10) stands
        //    between the lamp and Up a Folder, so the fixed run after the
        //    scrub carries FOUR glyph boxes now and the plan is nine items,
        //    every one of them a box of the same width and the whole row is fixed
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
        //    THE ROW IS THE MODAL FAMILY'S CONTROLS ROW (architect
        //    2026-10-14, modal_box.h; row 8's own from 2026-10-05): the
        //    buttons stand at row 8's case seat on row 8's rows, the
        //    transport's
        //    first case at the box's ring's left outer edge and the
        //    right-flushed pair's last ending at its right outer edge (no
        //    focus-frame reserve inside it; modal_controls_seats), today's
        //    gaps between the groups, and the scrub fills whatever lies
        //    between.
        //
        //    THIS BRANCH LAYS OUT AND PAINTS EVERYTHING BUT THE BUTTONS: it
        //    writes each button's own x (the walk below paints where it is
        //    told) and draws the slider and the clock between them. The items
        //    are disjoint, so painting them ahead of the buttons is only an
        //    ordering of convenience. --
        dlg.owner  = AppState::ModalDialogOwner::Player;

        // THE ROW'S LAYOUT is player_row_layout's (the abutting cases, the
        // group space, the clock's two fields measured first, the fixed left run,
        // the right-flushed pair and the scrub taking what is between them —
        // the rules stated there). THE CLOCK'S FACE IS ROW 8'S CLOCK'S, THE
        // PROGRAM FACE (architect 2026-10-26 ~12:10, "yes" to symmetry; the
        // body face until then), the face its fields were measured in.
        const GuiFont cfont = gui_font(GuiFace::Program);
        const PlayerRowLayout pl = player_row_layout(seats);
        assert(plan.size() == pl.x.size());
        for (std::size_t i = 0; i < pl.x.size(); ++i) plan[i].x = pl.x[i];
        const int field_pad = pl.field_pad;
        const int field_w   = pl.field_w;
        const int field_gap = pl.field_gap;
        const int scrub_x0  = pl.scrub_x0;
        const int scrub_w   = pl.scrub_w;
        const int clock_x0  = pl.clock_x0;
        const int clock_end = pl.clock_end;

        // -- THE PLAY-SCRUB, SOUND RECORDER'S CHANNEL UNDER THE KEY BLOCK
        //    (architect 2026-10-02; the hi-fi skin's two hairlines and
        //    key-shaped thumb 2026-10-23; the look and its owners at
        //    render.h's scrub block). The ITEM is
        //    published (dlg.scrub): the button box's own band, THE CHANNEL
        //    WIDENED BY THE THUMB'S OVERHANG AT EACH END (2026-10-25), so a
        //    press anywhere on the slider — the thumb's overhanging half
        //    included — is on it and the per-position damage covers the
        //    thumb's whole travel, its end columns included; the MAPPING
        //    (render_player_scrub_x_of) runs the thumb's centre, the frame's
        //    position, from the channel's first column to its last. --
        const AppState::RenderPlayer& rp = app.render_player;
        const int64_t pos = render_player_position(app, playback);
        if (scrub_w > 0) {
            paint_scrub_item(scrub_x0, scrub_w, [&] {
                // The drag's carried x while the thumb is being dragged (the
                // sound paused under the hold and the seek committing at the
                // release), the item position's own otherwise — the
                // channel's first column, frame 0, while no item is loaded
                // (render_player_scrub_x_of's frames <= 0 arm, the clock's
                // zeros).
                return rp.scrub.armed ? rp.scrub.marker_x
                                      : render_player_scrub_x_of(app, pos);
            });
        }

        // -- THE CLOCK, after the scrub (there since 2026-08-28): TWO TIME
        //    FIELDS since 2026-10-05, the POSITION and then the LENGTH, each
        //    centred on the case's rows with its run on the field's own
        //    seat, the skin's field and the bone (paint_ce_time_field) —
        //    the fields row 8's clock is, said twice. ONE RECT ROUND BOTH IS PUBLISHED (dlg.clock) for the
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
                {
                    // A time field stays flat (the head's scope).
                    const GuiMaterialScope flat(GuiMaterialRegion::Flat);
                    paint_ce_time_field(cr, field, palette().ce_field_dark,
                                        palette().ce_field_light);
                }
                // RIGHT-ALIGNED, ending at the field's right pad (the rule at
                // kTimeShape's block).
                const text_shape::ShapedRun run = text_shape::shape_text_run(
                    cfont, format_timestamp(seconds_of(shown[i])));
                // Row 8's clock's ink, the bone (2026-10-23).
                set_palette_source(cr, hex(kBoneInk));
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
    } else if (score_up) {
        dlg.owner = AppState::ModalDialogOwner::Score;
        const bool media = score_viewer_has_soundtrack(app);

        // THE PAGE (score_viewer.h's head), ON THE CPU CANVAS: the shown
        // page's surface, made by the page decoder at the interior's fit size
        // (score_page_worker.h: the smaller of the two ratios, cairo's GOOD
        // filter, the window's conversion), BLITTED 1:1 and centered on both
        // axes at whole device px (gui_page_place, canvas_sink.h), the rest
        // of the interior the field ground paint_modal_box laid. A page whose
        // decode failed leaves the field ground. A page NOT YET DELIVERED
        // shows THE LAST PAINTED PAGE (ScoreViewer::painted_page, its entry
        // kept by the feed's trim) until the decoder's delivery damages the
        // interior; nothing painted yet, the field ground. painted_page is
        // also the tick's gate on a page turn's owed position write: a frame
        // whose clip covers the interior and finds the shown page's entry
        // drew that page whole, or its field ground.
        // ON THE GL CANVAS (architect 2026-10-27, "1 a", THE GPU PAGE) NOTHING
        // OF THE PAGE IS PAINTED HERE: the interior is the frame's hole,
        // cleared by paint_modal_box above, and the GPU draws the field and
        // the page there at every swap from the gray store's bytes by the
        // same rule (GuiCanvasPage, canvas_sink.h; the request published at
        // the frame's first rect, score_viewer_publish_gpu_page) — and
        // painted_page is the GL canvas's report of the page it put on
        // screen, which the tick reads (GuiScoreViewer::take_gpu_page_report).
        if constexpr (!kGuiCanvasDrawList) {
            const AppState::ScoreViewer::Cached* c =
                score_viewer_cached(app, app.score_viewer.page, interior);
            if (c != nullptr) {
                if (clip_covers_drawable(cr, app, interior))
                    app.score_viewer.painted_page = app.score_viewer.page;
            } else if (app.score_viewer.painted_page >= 0) {
                c = score_viewer_cached(app, app.score_viewer.painted_page,
                                        interior);
            }
            if (c != nullptr && c->surface != nullptr && interior.w > 0 &&
                interior.h > 0) {
                const GuiRect at = gui_page_place(
                    interior, cairo_image_surface_get_width(c->surface),
                    cairo_image_surface_get_height(c->surface));
                const GuiMaterialScope flat(GuiMaterialRegion::Flat);
                cairo_save(cr);
                cairo_rectangle(cr, at.x, at.y, at.w, at.h);
                cairo_clip(cr);
                cairo_set_source_surface(cr, c->surface, at.x, at.y);
                cairo_paint(cr);
                cairo_restore(cr);
            }
        }

        if (media) {
            // -- THE MEDIA PLAYER'S ROW (2026-10-26 night): BACK, PLAY /
            //    PAUSE and FWD (2026-10-27), the scrub over the range's
            //    frames, the clock — the render
            //    player's items, painted by its own helpers and rules — and
            //    the four keys flush right. --
            const ScoreMediaRowLayout ml = score_media_row_layout(seats);
            assert(plan.size() == ml.x.size());
            for (std::size_t i = 0; i < ml.x.size(); ++i) plan[i].x = ml.x[i];
            const int64_t pos = score_viewer_position(app, playback);
            const ScoreFrameRange fr = score_viewer_frame_range(app);
            if (ml.scrub_w > 0) {
                paint_scrub_item(ml.scrub_x0, ml.scrub_w, [&] {
                    // The drag's carried column while the thumb is dragged,
                    // the cursor's own otherwise.
                    return app.score_viewer.scrub.armed
                               ? app.score_viewer.scrub.marker_x
                               : score_scrub_x_of_frame(app, pos);
                });
            }
            // THE CLOCK, the render player's two time fields verbatim: the
            // elapsed time within the range, then the range's length, at
            // the soundtrack's own rate.
            const GuiFont cfont = gui_font(GuiFace::Program);
            const int sr = app.score_audio.rate();
            auto seconds_of = [sr](int64_t frames) {
                return sr > 0 ? static_cast<double>(frames) /
                                    static_cast<double>(sr)
                              : 0.0;
            };
            const int clock_right = std::min(ml.clock_end, cx1);
            if (clock_right > ml.clock_x0) {
                cairo_save(cr);
                cairo_rectangle(cr, ml.clock_x0, content.y,
                                clock_right - ml.clock_x0, content.h);
                cairo_clip(cr);
                const int64_t shown[2] = {pos - fr.begin, fr.length()};
                for (int i = 0; i < 2; ++i) {
                    const int field_x =
                        ml.clock_x0 + i * (ml.field_w + ml.field_gap);
                    const GuiRect field = time_field_rect(
                        field_x, ml.field_w, seats.case_y, seats.case_h);
                    {
                        const GuiMaterialScope flat(GuiMaterialRegion::Flat);
                        paint_ce_time_field(cr, field, palette().ce_field_dark,
                                            palette().ce_field_light);
                    }
                    const text_shape::ShapedRun run =
                        text_shape::shape_text_run(
                            cfont, format_timestamp(seconds_of(shown[i])));
                    set_palette_source(cr, hex(kBoneInk));
                    text_shape::show_shaped_run(
                        cr, run,
                        static_cast<double>(field_x + ml.field_w -
                                            ml.field_pad) -
                            run.width_px,
                        redesign_baseline(cfont, static_cast<double>(field.y),
                                          static_cast<double>(field.h)));
                }
                cairo_restore(cr);
                dlg.clock = GuiRect{ml.clock_x0 - 1, content.y,
                                    clock_right - ml.clock_x0 + 2, content.h};
            }
        } else {
            const ScoreRowLayout sl =
                score_row_layout(seats, app.score->count());
            assert(plan.size() == sl.x.size());
            for (std::size_t i = 0; i < sl.x.size(); ++i) plan[i].x = sl.x[i];

            // THE PAGE SCRUB, the player's item over the viewing range's pages:
            // the drag's carried column while the thumb is dragged (architect
            // 2026-10-27, "the thumb follows the pen continuously, the page
            // under it changes in steps"), the shown page's column otherwise
            // (score_scrub_x_of) — the step.
            if (sl.scrub_w > 0) {
                paint_scrub_item(sl.scrub_x0, sl.scrub_w, [&] {
                    return app.score_viewer.scrub.armed
                               ? app.score_viewer.scrub.marker_x
                               : score_scrub_x_of(app, app.score_viewer.page);
                });
            }

            // THE PAGE CELL where the player's clock stands: one sunken time
            // field reading "<ordinal> / <count>" — the shown page's ordinal
            // WITHIN THE RANGE and the range's count (the architect's "a page
            // number of its own") — right-aligned on the field's pad as the
            // clocks' runs are, in the program face, the bone ink.
            const int cell_right = std::min(sl.cell_x0 + sl.cell_w, cx1);
            if (cell_right > sl.cell_x0) {
                const ScoreViewerRange r = score_viewer_range(app);
                const std::string text =
                    std::to_string(app.score_viewer.page - r.first + 1) + " / " +
                    std::to_string(r.count());
                const GuiFont cfont = gui_font(GuiFace::Program);
                const GuiRect field = time_field_rect(sl.cell_x0, sl.cell_w,
                                                      seats.case_y, seats.case_h);
                cairo_save(cr);
                cairo_rectangle(cr, sl.cell_x0, content.y,
                                cell_right - sl.cell_x0, content.h);
                cairo_clip(cr);
                {
                    const GuiMaterialScope flat(GuiMaterialRegion::Flat);
                    paint_ce_time_field(cr, field, palette().ce_field_dark,
                                        palette().ce_field_light);
                }
                const text_shape::ShapedRun run =
                    text_shape::shape_text_run(cfont, text);
                set_palette_source(cr, hex(kBoneInk));
                text_shape::show_shaped_run(
                    cr, run,
                    static_cast<double>(sl.cell_x0 + sl.cell_w - sl.cell_pad) -
                        run.width_px,
                    redesign_baseline(cfont, static_cast<double>(field.y),
                                      static_cast<double>(field.h)));
                cairo_restore(cr);
            }
        }
    } else if (picker_up) {
        // -- THE PICKER'S ROW (architect 2026-08-28; ONE BUTTON since
        //    2026-08-29): **Cancel** and NOTHING ELSE — no label, no field,
        //    no OK. The list in the
        //    band above is the whole question, a click on a row is the answer,
        //    and the prompt's `<projects_path>/` prefix went with the field it
        //    labelled.
        //
        //    A LIST'S BUTTONS SIT FLUSH RIGHT (architect 2026-08-29, "if I'm
        //    using the picker my hand stays on the right"; "I am
        //    right-handed", 2026-08-28, on seeing the picker on the tablet),
        //    the rule every word row keeps since 2026-10-14 (the layout
        //    block above). With nothing to its left the row is that one
        //    button, ON THE MODAL FAMILY'S CONTROLS ROW (architect,
        //    modal_box.h; the taskbar dialog carried it from 2026-10-10):
        //    ALONE, FLUSH RIGHT, ITS RIGHT EDGE THE BOX'S RING'S OUTER EDGE
        //    (x 2280 at 300 %), as the player's Close ends there; the focus
        //    frame, when worn, stands in the face beside the ring. --
        dlg.owner  = AppState::ModalDialogOwner::Picker;
    } else {
        // -- The editor: THE BOX IS ITS EDIT CONTROL (architect 2026-10-14,
        //    the head's AN EDITOR), its line from the box's first row,
        //    soft-wrapped, OK and Cancel flush right on the controls row
        //    (the layout block above). --
        dlg.owner = AppState::ModalDialogOwner::Editor;
        {
            // THE EDIT CONTROL'S CHROME IS THE BOX (paint_modal_box, laid at
            // this body's head): the canvas column's ring round the theme's
            // FIELD ground (architect 2026-10-03, the field pair; the sunken
            // two-line field of 2026-10-02 stood on the bottom row until
            // 2026-10-14), square. No outline says hover or focus: a FOCUSED
            // FIELD SHOWS ITS CARET and nothing else, the caret painting only
            // while the ring has not stepped onto a button (below).
            //
            // FOCUS IS `modal_dialog_focus < 0` and needs no term of its own: on
            // an editor dialog -1 IS the field, the ring's own meaning for it
            // (AppState::modal_dialog_focus), which includes the open, where the
            // user is there to type.
            //
            // A REFUSED ENTER RECOLOURS NOTHING (architect 2026-10-03: "a red
            // outline and the card is redundant"; the red frame retired from both
            // editors): the ring stays its pair and the ground the field
            // ground, the whole text is selected (text_editor::refuse) so
            // the first keystroke replaces it, and the reason card the refusing
            // commit posts (the owner's own sentence, GuiFlagEditor::notifications
            // and the settings editor's equivalent) says why. The flag editor
            // takes the same rule (render_flag_editor_box). Its text,
            // selection and caret take the field pair and the selected pair.
            const bool field_focused = app.modal_dialog_focus < 0;
            const GuiColor ink_text     = palette().field_text;
            const GuiColor ink_sel_fill = palette().selected_fill;
            const GuiColor ink_sel_text = palette().selected_text;

            // THE LINE IS SOFT-WRAPPED AT THE BOX'S WIDTH AND NEVER SCROLLS
            // (architect 2026-10-14: "looks like a multiline edit but that's ok
            // — long lines can wrap"; the one-row field's horizontal travel of
            // 2026-08-13 retired with the field). ONE SHAPED RUN, the rows cut
            // from its own pen (text_shape::byte_offsets_px) by the one wrap
            // rule, text_editor::soft_wrap_rows: each row the most text that
            // fits the text room (the box less its pad each side) less THE
            // CARET'S COLUMN, reserved at the right so a caret at a full
            // row's end stands inside the room. Row i stands one list row
            // pitch under the one before (row0_y + i · row_pitch, the first
            // row's band seated on the pad's baseline above), its bytes
            // painting from the line's start (`line_x0`, the pen): its origin
            // is that start less its first byte's pen. A line taller
            // than the box is cut at the box's interior — nothing scrolls, and
            // the 1024-byte cap keeps a real line inside it.
            // NO DIALOG EDITOR CARRIES A LABEL (2026-10-14: the Settings
            // editor's "Setting:" and the BPM editor's "BPM:" both removed,
            // architect "drop"): the two editors share one shape, the line
            // starting at the pen on every row.
            const int line_x0 = text_x0;
            const int line_w = std::max(0, text_w);
            // THE CARET ONE DEVICE PX WIDE (render.h's line rule, the
            // sweep of 2026-10-23; one scaled W until then).
            const int caret_px = hairline_px();
            const text_shape::ShapedRun run =
                text_shape::shape_text_run(font, ed->pending);
            const std::vector<double> bx_off =
                text_shape::byte_offsets_px(run, ed->pending.size());
            const std::vector<text_editor::WrapRow> ranges =
                text_editor::soft_wrap_rows(
                    ed->pending, bx_off,
                    std::max(1.0, static_cast<double>(line_w - caret_px)));

            // PUBLISH the click-to-caret geometry (DialogEditorText's
            // contract): the rows, each with its band and its origin, and the
            // run's pen — so a click, the F2.1 text drag, the double-click's
            // word select and Home / End land on the rows painted here.
            AppState::DialogEditorText& out = app.dialog_editor_text;
            out.valid  = true;
            out.byte_x = bx_off;
            out.rows.clear();
            for (std::size_t r = 0; r < ranges.size(); ++r) {
                AppState::DialogEditorText::Row row;
                row.begin    = ranges[r].begin;
                row.end      = ranges[r].end;
                row.hard     = ranges[r].hard;
                row.origin_x = static_cast<double>(line_x0) -
                               bx_off[static_cast<std::size_t>(row.begin)];
                row.y        = row0_y + static_cast<int>(r) * row_pitch;
                row.h        = row_h;
                out.rows.push_back(row);
            }

            // Everything from here paints CLIPPED TO THE BOX'S INTERIOR, and
            // each row's run under its own row's clip (its text range's
            // columns), so a row shows its own bytes and no other row's.
            cairo_save(cr);
            cairo_rectangle(cr, interior.x, interior.y, interior.w,
                            interior.h);
            cairo_clip(cr);

            const bool has_sel = text_editor::has_selection(*ed);
            const int  s0 = text_editor::selection_start(*ed);
            const int  s1 = text_editor::selection_end(*ed);
            const int  cursor_pos = std::clamp(
                ed->cursor_pos, 0, static_cast<int>(ed->pending.size()));
            const int  caret_row  =
                dialog_editor_row_of(out, cursor_pos, ed->caret_row_end);
            for (std::size_t r = 0; r < out.rows.size(); ++r) {
                const AppState::DialogEditorText::Row& row = out.rows[r];
                const double baseline =
                    redesign_baseline(font, static_cast<double>(row.y),
                                      static_cast<double>(row.h));
                // THE SELECTION AND CARET BAND IS THE FACE'S LINE BOX on the
                // text's own rows (the flag editor's, brief 69 — ascent +
                // descent), as the one-row field took it.
                const int band_y =
                    static_cast<int>(std::nearbyint(baseline -
                                                    gui_font_ascent_px(font)));
                const int band_h =
                    static_cast<int>(std::nearbyint(gui_font_line_px(font)));
                const double tx    = row.origin_x;
                const double row_w =
                    bx_off[static_cast<std::size_t>(row.end)] -
                    bx_off[static_cast<std::size_t>(row.begin)];
                // THE ROW'S OWN COLUMNS: from the line's start to its last
                // byte's pen — the last row's to the interior's edge, nothing
                // following it there.
                const bool   last_row = r + 1 == out.rows.size();
                const double clip_x0  = static_cast<double>(line_x0);
                const double clip_x1  =
                    last_row ? static_cast<double>(interior.x + interior.w)
                             : clip_x0 + row_w;
                const double ry0 = static_cast<double>(row.y);
                const double ry1 = static_cast<double>(row.y + row.h);
                // THE SELECTION IS THE THEME'S SELECTED PAIR OVER THE FIELD
                // PAIR (architect 2026-10-03, unified fields; render.h's
                // palette block) — the selected fill behind the selected
                // substring's part on this row, the selected text for its
                // glyphs, the field text for the rest: the flag editor's
                // marker-lane box paints the same four. ONE INK PER PIXEL, the
                // flag editor's rule (render_flag_editor_box): with a
                // selection on the row, the field-text run is clipped to the
                // band's complement and the selected run to the band, two
                // disjoint regions, so every antialiased edge blends against
                // exactly the ground it sits on.
                const int a = std::max(s0, row.begin);
                const int b = std::min(s1, row.end);
                if (has_sel && a < b) {
                    const int hx0 = static_cast<int>(std::nearbyint(
                        tx + bx_off[static_cast<std::size_t>(a)]));
                    const int hx1 = static_cast<int>(std::nearbyint(
                        tx + bx_off[static_cast<std::size_t>(b)]));
                    const int hw  = (hx1 > hx0) ? (hx1 - hx0) : 1;
                    cairo_save(cr);
                    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
                    set_palette_source(cr, ink_sel_fill);
                    cairo_rectangle(cr, hx0, band_y, hw, band_h);
                    cairo_fill(cr);
                    cairo_restore(cr);
                    // The complement inside the row's columns: the columns
                    // left and right of the band, and the rows above and
                    // below it (the band is the face's line, not the row's
                    // box), as one clip path.
                    cairo_save(cr);
                    cairo_rectangle(cr, clip_x0, ry0, hx0 - clip_x0, ry1 - ry0);
                    cairo_rectangle(cr, hx0 + hw, ry0,
                                    clip_x1 - (hx0 + hw), ry1 - ry0);
                    cairo_rectangle(cr, hx0, ry0, hw, band_y - ry0);
                    cairo_rectangle(cr, hx0, band_y + band_h, hw,
                                    ry1 - (band_y + band_h));
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
                    cairo_save(cr);
                    cairo_rectangle(cr, clip_x0, ry0, clip_x1 - clip_x0,
                                    ry1 - ry0);
                    cairo_clip(cr);
                    set_palette_source(cr, ink_text);
                    text_shape::show_shaped_run(cr, run, tx, baseline);
                    cairo_restore(cr);
                }
                // THE CARET IS THE FIELD'S FOCUS, SO IT PAINTS ONLY WHILE THE
                // FIELD HAS IT (architect 2026-08-13, at his live test: "the
                // blinking caret, the I-beam, continues to blink in the text
                // field even though it has lost focus"). `field_focused` is
                // the ring's own -1, resolved above, so this needs no term of
                // its own; the caret is the field's ONE focus cue (architect
                // 2026-10-02). THE SELECTION HIGHLIGHT IS DELIBERATELY NOT
                // GATED: it is buffer STATE, not focus, and it must still be
                // visible when the user walks back onto the field to act on
                // it. The blink's TICK carries the same gate (main.cpp), so an
                // unfocused field wakes the loop for nothing either. IT
                // STANDS ON ITS BYTE'S ROW (dialog_editor_row_of: a row's end
                // is the next row's begin, and its caret stands there — but
                // a HARD row's end at that row's end while the caret carries
                // the row-end affinity, 2026-10-14, the rule at
                // dialog_editor_row_last_stop), in the column the wrap
                // reserved.
                if (static_cast<int>(r) == caret_row && field_focused &&
                    text_editor::cursor_visible_now(*ed)) {
                    const double caret_x =
                        tx + bx_off[static_cast<std::size_t>(cursor_pos)];
                    cairo_save(cr);
                    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
                    // THE CARET IS ITS FIELD'S TEXT (architect 2026-10-03).
                    set_palette_source(cr, ink_text);
                    cairo_rectangle(cr, static_cast<int>(std::nearbyint(caret_x)),
                                    band_y, caret_px, band_h);
                    cairo_fill(cr);
                    cairo_restore(cr);
                }
            }
            cairo_restore(cr);   // the interior clip

            dlg.field = interior;
        }
    }

    // -- The button row. EVERY BUTTON PAINTS AT ITS OWN x (2026-08-28, the
    //    player's relaid row): the word cluster's x's are modal_word_buttons'
    //    (written with the widths, above), and the PLAYER's branch above has
    //    written all nine of its own — one walk, two arrangements, and no
    //    second painter. --
    for (size_t i = 0; i < plan.size(); ++i) {
        const int x = plan[i].x;
        const GuiRect r{x, btn_y, plan[i].w, btn_h};
        // THE FACE IS THE KEY ON EVERY OWNER'S ROW (architect 2026-10-23;
        // paint_roster_case, the one painter): at rest, word button and
        // player key alike; DOWN with its LED lit for the row's one checked
        // button (Repeat One, `lit`) — ONE device px right and down — and
        // for a LIVE PRESS — armed with the pointer inside it, or armed
        // from the keyboard — TWO device px whatever the checked state (the
        // two depths, key_depth, 2026-10-23), the legend under the key
        // unmoved. (Windows'
        // push button 2026-10-02 to 2026-10-22, the ebony case 2026-10-22 to
        // 2026-10-23.) THE FOCUS IS NOT PAINTED (below): the two focus
        // strengths stay in the state (AppState::modal_dialog_focus_active),
        // where the keyboard's walk reads them. NO
        // HOVER FACE but on the player's row (below): the pointer walk
        // (update_modal_dialog_hover) drives the tooltip and that row's hot
        // button.
        // THE RENDER PLAYER'S ROW IS ROW 8'S CASES (architect 2026-10-05;
        // the program's since 2026-10-09): its nine boxes take the roster's
        // face through the one painter, paint_roster_case — the rest face,
        // the key DOWN one device px for Repeat One's lit lamp and two for
        // a press (the two depths, 2026-10-23), NO HOT FACE
        // (program_spec.h's case_hot_face) — and no separators.
        // THE DISABLED RUNG, on the two rows that publish a stateful bit
        // (AppState::ModalDialogButton::enabled, the one statement of its
        // owners; re-grepped 2026-10-28, Sol's round): the PLAYER's buttons
        // (architect 2026-08-30, render_player_button_enabled) and the SCORE
        // VIEWER's keys (score_viewer_button_enabled) — every other owner's
        // buttons are always live while their dialog stands and publish
        // enabled=true. The legend dimmed (kDimInk, paint_key_legend,
        // 2026-10-23), the key the face its state gives, the roster's rule
        // (architect 2026-10-02 and 2026-10-09). The press face is gated on the bit rather than
        // trusted — a button can go dead UNDER a hold.
        const bool enabled = plan[i].enabled;
        const bool armed   = enabled &&
                             static_cast<int>(i) == app.modal_dialog_pressed;
        const bool pressed = enabled &&
            ((armed && app.modal_dialog_press_inside) ||
             static_cast<int>(i) == app.modal_dialog_key_pressed);
        // NO FOCUS FRAME (architect 2026-10-23: "when I open up the settings
        // and I press cancel, it gets that square drawn around it. Let's get
        // rid of that"): the focused button — Enter's target, the
        // Windows-strict tables unchanged (AppState::modal_dialog_focus) —
        // wears nothing; the key's pressed face and its LED are the only
        // signs. The default-button frame's one relief line in the ring's
        // light tone, 2026-10-02 to 2026-10-23, read as a bevel on the flat
        // ground.
        // THE KEY (2026-10-23, paint_roster_case), flat, its LED lit for the
        // row's one checked button; THE LEGEND UNDER IT (paint_key_legend's
        // one seat): a glyph button's the player's own legend
        // (render_player_button_legend, the Play key's naming its next press,
        // 2026-10-25 — its repaint the row's one damage owner, the play
        // face's writers damaging the row whole), dimmed when dead; a word button's
        // its word IN CAPITALS, its access key underlined (the Windows-strict
        // keys unchanged, only the drawing) — always live while its dialog
        // stands.
        const GuiMaterialScope flat(GuiMaterialRegion::Flat);
        paint_roster_case(cr, r, plan[i].lit, pressed);
        std::string legend;
        int legend_access = -1;
        if (plan[i].glyph) {
            // The score viewer's PLAY / PAUSE prints the render player's Play
            // key's legend by its face, the same key painted the same way.
            legend = plan[i].score_act == AppState::ScoreButtonAct::PlayPause
                         ? render_player_button_legend(
                               AppState::PlayerButtonAct::PlayPause,
                               plan[i].play_face)
                     : plan[i].score_act != AppState::ScoreButtonAct::None
                         ? std::string(
                               score_viewer_button_legend(plan[i].score_act))
                         : render_player_button_legend(plan[i].player_act,
                                                       plan[i].play_face);
            paint_key_legend(cr, r, legend, enabled);
        } else {
            legend = plan[i].label;
            for (char& ch : legend)
                if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
            legend_access = plan[i].access;
            paint_key_legend(cr, r, legend, true, legend_access);
        }
        AppState::ModalDialogButton out;
        out.rect         = r;
        out.access_key   = plan[i].access >= 0
                               ? access_key_of(plan[i].label.c_str(),
                                               plan[i].access)
                               : 0;
        out.response_key = plan[i].response_key;
        out.editor_ok    = plan[i].editor_ok;
        out.player_act   = plan[i].player_act;
        out.score_act    = plan[i].score_act;
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
        // a tick. THE SCORE VIEWER'S ROW TAKES THE SAME GATE (2026-10-27):
        // its skips' faces move with the cursor, and its clock and scrub
        // damage at the tick's cadence while the soundtrack plays — a BACK
        // greyed at the range's first frame must not be published live by a
        // frame that repainted the clock alone. Every other owner's buttons
        // publish a constant true and need no gate. THE COVERAGE IS THE KEY
        // AND ITS LEGEND (key_legend_layout's extent, 2026-10-23): the legend
        // wears the bit (dimmed when dead), so a clip that missed it must
        // keep the bit.
        if ((player_up || score_up) &&
            !clip_covers_drawable(
                cr, app, key_legend_layout(r, legend, legend_access).extent)) {
            const bool carry = !face_state_reset &&
                               i < prev_enabled.size() &&
                               prev_enabled[i].player_act == out.player_act &&
                               prev_enabled[i].score_act == out.score_act;
            out.enabled = carry && prev_enabled[i].enabled;
        }
        // THE HINT: a word button's is THE ACT'S NAME ALONE, its word
        // (architect 2026-10-10, Windows strict — the underline carries the
        // access key, and Enter and Esc are every dialog's; the key-naming
        // hint "Save (S)" of 2026-08-13 retired with it); a glyph button's
        // is the player's own. Published with the rect because the WORD is
        // the half the pointer path cannot re-derive.
        out.tooltip = plan[i].tooltip.empty() ? plan[i].label
                                              : plan[i].tooltip;
        // THE MODIFIER LINE (2026-08-28), published beside it and empty on
        // every button with no shifted twin — the roster hint's `line2` over
        // this surface, its producers the player's plan and the score
        // viewer's (its skips, 2026-10-27) above.
        out.tooltip2 = plan[i].tooltip2;
        dlg.buttons.push_back(out);
    }

    // THE MODAL'S SURFACE IS THE BOX: a prompt's, the progress modal's and a
    // dialog editor's the modal family's whole area (modal_area_rect — the
    // panel, the box and the ground margins beside it, and the controls row
    // under it, lifted onto the keyboard's band when the keyboard stands),
    // and the player's and the picker's the controls row
    // (modal_controls_row_rect, the strip: pad + case + pad, the legends in
    // its bottom pad; the list above it is the folder overlay's own rect) — because that is the rectangle the
    // modal owns and the rectangle its damage must erase.
    // THE SESSION IS STAMPED HERE, one write for both branches at the moment
    // the geometry becomes readable: it is the id of the surface these rects
    // belong to, and every input site that reads them compares it against the
    // live one (modal_dialog_stash_current, input_pointer.cpp).
    dlg.box     = boxed ? modal_area_rect(app, kb_h) : content;
    dlg.session = live_session;
    dlg.valid   = true;
    cairo_restore(cr);
}

// (THE CHOICE EDITOR'S COMBO AND ITS LIST — paint_choice_combo,
// paint_combo_list and paint_settings_choice, 2026-10-07 to 2026-10-14 —
// went with the choice editor, architect 2026-10-14.)

// -- GuiPaintHandler::paint_onscreen_keyboard ----------------------------
//
// THE GLASS'S KEY SURFACE (2026-08-27). Contract, gate and paint order are at
// the declaration; the layout table, the geometry walk and the two lamps are at
// onscreen_keyboard.h, which this body reads and never restates.
//
// THE KEYS ARE THE HI-FI SKIN'S KEYS (architect 2026-10-23; the one painter
// paint_key, cool_edit_paint.h): each key at rest on the band's brushed
// ground, NO LED (a typing key carries no lamp), its cap PRINTED ON ITS
// FACE in the bone in THE KEYS FACE, 24 px at 300 % (gui_font.h's type
// table, GuiFace::Keys; architect 2026-10-24, "C24" — the night's C34 "too
// far", "a separate face" since that night, the body's 22 before it) — the
// band painting NO LINE at its own
// top edge (architect 2026-08-27, on glass). THE OTHER TWO FACES ARE THE
// KEY DOWN at the key's TWO DEPTHS, the cap riding with the face
// (2026-10-23; key_depth):
//   ARMED     — the CHECKED face, ONE DEVICE PX right and down, the band's
//               checked case's. Worn by SHIFT
//               while it is armed and by the SYMBOL-MODE key while a symbol
//               page stands — the two lamps.
//               IT IS ALL SHIFT'S ARM HAS TO SAY ITSELF WITH now that the cap
//               is the word "Shift" in both states — that, and the letter
//               caps, every one of which turns capital while the arm stands.
//   PRESSED   — the HELD face, TWO DEVICE PX right and down whatever the
//               armed state, the roster's pressed face; a press on an armed
//               key goes deeper, as on the band's checked cases.
// (Windows' toolbar buttons — SOFT RAISED, SOFT SUNKEN pressed and over the
// Hilight dither armed, the cap in the theme's label — 2026-10-02 to
// 2026-10-22; the ebony case 2026-10-22 to 2026-10-23.)
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
    // ground, and NO LINE on any side (architect 2026-10-14,
    // onscreen_keyboard.h's placement block): the band spans the window's
    // width and ends on its foot (onscreen_keyboard::surface_rect), the
    // modal family's border, when a dialog editor's stack stands on it,
    // above it. (The program frame the band once stood inside went
    // 2026-10-21 and 2026-10-22; the taskbar overlay's raised Hilight line on its top,
    // 2026-10-10 to 2026-10-14, is gone with the overlay.)
    // THE BAND'S GROUND IS THE BRUSHED GROUND (materials.h's GROUND,
    // 2026-10-23); the keys the hi-fi skin's keys, each under the Flat scope
    // below.
    {
        const GuiMaterialScope ground(GuiMaterialRegion::Ground);
        paint_cell_rect(cr, surf, palette().ground);
    }

    // THE KEYS FACE (gui_font.h's type table, GuiFace::Keys: 24 px at 300 %,
    // scaled with gui_scale; architect 2026-10-24, "C24"), selected through
    // the one face owner and shaped through the one chokepoint (text_shape.h)
    // like every other label in the product. EVERY key on this surface takes
    // it — the character caps, the page keys' words and the function keys'
    // words alike — so there is no second way a cap can be drawn; a word too
    // long for its key is SHORTENED, never the face shrunk (cap_word,
    // onscreen_keyboard.h). A cap is one to nine glyphs, which are the
    // cheapest runs there are.
    const GuiFont font = gui_font(GuiFace::Keys);

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

            // THE KEY (architect 2026-10-23; paint_key, cool_edit_paint.h):
            // the TWO DEPTHS by key_depth — the key under the finger HELD,
            // two device px, whatever its armed state; armed (Shift, the
            // symbol page's key — a checked key) and not held CHECKED, one;
            // NO LED — a typing key carries no lamp, so the depth goes to
            // paint_key_at_depth directly; the cap printed ON its face, so it
            // RIDES WITH THE FACE by the key's depth, right and down.
            const GuiMaterialScope flat(GuiMaterialRegion::Flat);
            const int depth = key_depth(pressed, armed);
            paint_key_at_depth(cr, r, depth, GuiKeyLed::None);
            const double ride = static_cast<double>(depth * hairline_px());

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
                r.x + (r.w - run.width_px) * 0.5) + ride;
            const double baseline = redesign_baseline(
                font, static_cast<double>(r.y), static_cast<double>(r.h)) + ride;
            // THE CAP IN THE BONE in the keys face, no shadow (materials.h's
            // INKS, 2026-10-23), its cap band centered on the key by
            // redesign_baseline and riding with the key's depth.
            set_palette_source(cr, hex(kBoneInk));
            text_shape::show_shaped_run(cr, run, cap_x, baseline);
        });

    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_keyboard_slot ----------------------------------

void GuiPaintHandler::paint_keyboard_slot(cairo_t* cr, const GuiRect& exposed) {
    // THE AS-PAINTED BIT, AND IT DESCRIBES PIXELS — the roster publisher's own
    // rule (publish_button_face, above), taken for its own reason. The slot
    // comparator (onscreen_keyboard::reconcile_slot_standing) pays the slot's show and hide off the drift
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
    // MODAL FAMILY'S PANEL every time it stands — the view bar's top row
    // through the controls row's top since 2026-10-14 (modal_box.h),
    // whatever its listing's length (its fixed height) — and the keyboard's
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

namespace {
// `r` less `hole` as up to four rects — the rows above the hole, the rows
// below it, and beside it on its own rows the parts left and right — or `r`
// whole where the two do not meet (an empty hole included).
template <typename F>
void for_each_part_outside(const GuiRect& r, const GuiRect& hole, F&& f) {
    const int x0 = std::max(r.x, hole.x);
    const int y0 = std::max(r.y, hole.y);
    const int x1 = std::min(r.x + r.w, hole.x + hole.w);
    const int y1 = std::min(r.y + r.h, hole.y + hole.h);
    if (x1 <= x0 || y1 <= y0) {
        f(r);
        return;
    }
    if (y0 > r.y) f(GuiRect{r.x, r.y, r.w, y0 - r.y});
    if (r.y + r.h > y1) f(GuiRect{r.x, y1, r.w, r.y + r.h - y1});
    if (x0 > r.x) f(GuiRect{r.x, y0, x0 - r.x, y1 - y0});
    if (r.x + r.w > x1) f(GuiRect{x1, y0, r.x + r.w - x1, y1 - y0});
}

} // namespace

// -- THE MODAL FAMILY'S BOX (architect 2026-10-14) -------------------------
//
// The shape and every rect are modal_box.h's: THE PANEL in the program's
// ground (the box's margins), THE BOX'S RING — one device px in the ring
// tones on the box's outer edge, `ce_dark` top and left, `ce_hilight` right
// and bottom, mitred — and the interior ONE FLAT FIELD, kFieldGround, for
// every member (modal_box.h's head). The controls row under the panel stands
// on row 8's own rows, whose ground paint_bottom_strip lays (the row standing down under
// the modal, modal_owns_bottom_row) — UNLESS THE STACK STANDS ON THE
// ON-SCREEN KEYBOARD (`keyboard_h` > 0, a dialog editor on the glass,
// modal_box.h's head): then this lays the lifted controls row's ground
// directly on the keyboard's band. The render player and the
// picker never stand with the keyboard and pass 0 (paint_folder_overlay);
// the prompts and the dialog editors pass the live lift
// (paint_modal_dialog). Publishes nothing.
// THE GPU PAGE'S HOLE (architect 2026-10-27, "1 a"; canvas_sink.h's
// TRANSPARENCY INVARIANT): `hole`, the box's interior while the score
// viewer's page stands under the GL canvas (the frame's clip; empty
// otherwise), takes neither the panel's ground nor the field — both laid
// under one clip of the panel less the hole — and is CLEARED to alpha 0,
// erasing what the lanes painted there under the box (on_redraw's paint
// order, step 12); the ring stands outside it. So every CPU pixel inside the
// hole is 0 until a floater paints over it, and the GPU's field and page
// show there.
static void paint_modal_box(cairo_t* cr, const AppState& app,
                            int keyboard_h, const GuiRect& hole) {
    const GuiRect panel = modal_panel_rect(app, keyboard_h);
    if (panel.w <= 0 || panel.h <= 0) return;
    const GuiRect box = modal_box_rect(app, keyboard_h);
    const GuiPalette& p = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    const bool holed = hole.w > 0 && hole.h > 0;
    if (holed) {
        cairo_save(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_rectangle(cr, hole.x, hole.y, hole.w, hole.h);
        cairo_fill(cr);
        cairo_restore(cr);
        cairo_save(cr);
        for_each_part_outside(panel, hole, [&](const GuiRect& part) {
            cairo_rectangle(cr, part.x, part.y, part.w, part.h);
        });
        cairo_clip(cr);
    }
    {
        // THE HI-FI SKIN (architect 2026-10-23): THE PANEL'S MARGINS
        // AND THE LIFTED CONTROLS ROW the brushed GROUND; THE BOX ONE SUNKEN
        // FLAT FIELD, kFieldGround, whatever the member (a prompt's question
        // too: the field is the box) — the ring's slot included — under a
        // ONE-DEVICE-PX RING in the ring tones on its outer edge, dark top and
        // left, light bottom and right (render.h's line rule).
        const GuiMaterialScope surface(GuiMaterialRegion::Ground);
        paint_cell_rect(cr, panel, p.face);
        if (box.w > 0 && box.h > 0) {
            const GuiMaterialScope flat(GuiMaterialRegion::Flat);
            paint_cell_rect(cr, box, hex(kFieldGround));
            if (holed) cairo_restore(cr);   // the ring outside the hole
            paint_hairline_frame(cr, box, p.ce_dark, p.ce_hilight);
        } else if (holed) {
            cairo_restore(cr);
        }
        if (keyboard_h > 0)
            paint_cell_rect(cr, modal_controls_row_rect(app, keyboard_h),
                            p.face);
    }
    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_folder_overlay -----------------------------------
//
// THE LIST PANEL (2026-08-28; the modal family's box since 2026-10-14),
// ONE PAINTER FOR EVERY CONTENT —
// the render player's folders and wavs, the Open project picker's project
// folders and the preset picker's waveform presets (2026-10-25, glyph-less
// rows by display name). Contract and gate are at the declaration; the geometry, the scroll
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
// inside THE MODAL FAMILY'S BOX since 2026-10-14 (paint_modal_box, the canvas
// column's ring; the plain sunken two-line edge round the band before it),
// the names in the FIELD TEXT, each row 17 Windows px with its BOXED GLYPH
// (the walk's glyph block below; folder_overlay.h's row box), and the faces
// Windows' list:
//   a RESTING row         -> NO FILL AT ALL: the field shows through
//   the HIGHLIGHT         -> a FLAT FILL in the theme's SELECTED PAIR, the
//                            selection, which is also the list's keyboard
//                            focus — ONE PAIR FOCUSED OR NOT (architect
//                            2026-10-03, render.h's palette block: the
//                            2026-09-02 inactive face retired)
//   PRESSED               -> the highlight's own face: a row press is an arm
//                            whose act is the lift, and the highlight is what
//                            it promises
// NO HOVER FACE (architect 2026-10-02) AND NO FOCUS FRAME (architect
// 2026-10-23): THE RING'S FOCUS ON THE LIST (`list_focused`) is the keyboard's
// state alone, the lit row the only sign (the walk below).
//
// TWO MARKS, ONE ROW EACH AND POSSIBLY THE SAME ROW: the HIGHLIGHT band (the
// list's keyboard focus — what Enter and Load in place act on, and nothing
// else: Space and the Play button answer the TRANSPORT and never read the
// highlight)
// and the TRANSPORT'S ITEM, which its row does not mark (architect
// 2026-10-23; the wav glyph swapped for Play until then).
//
// STATE-AXIS: the face is decided ONCE per row from the highlight and the
// press arm, and the glyph from the row's kind alone; there is no second
// list of what a row looks like anywhere.
// THE LIST ROWS' BOXED GLYPH AND NAME (architect 2026-10-23; the walk's
// glyph block below): the glyph's ink height, its box's x and the name's x
// past the modal box's outer left edge, device px at 300 % (skin_px).
static constexpr double kListGlyphInkAt300 = 27.0;
static constexpr double kListGlyphXAt300   = 18.0;
static constexpr double kListNameXAt300    = 64.0;

void GuiPaintHandler::paint_folder_overlay(cairo_t* cr, const GuiRect& exposed) {
    if (!folder_overlay::stands(app)) return;
    const GuiRect surf = folder_overlay::surface_rect(app);
    if (surf.w <= 0 || surf.h <= 0) return;
    if (!rects_intersect(exposed, surf)) return;

    const AppState::FolderOverlay& ov = app.folder_overlay;

    cairo_save(cr);
    // THE MODAL FAMILY'S BOX (modal_box.h, architect 2026-10-14): the panel's
    // ground, the ring and THE FLAT FIELD inside it; then
    // EVERYTHING ELSE UNDER THE CONTENT RECT'S CLIP, because a scrolled
    // listing's first and last rows straddle the content's edges and must not
    // paint into the ring or the controls row below.
    paint_modal_box(cr, app, /*keyboard_h=*/0);
    // THE ROW WALK'S CLIP IS THE CONTENT RECT AND row_at'S CONTAINMENT IS THE
    // SAME RECT (folder_overlay.h's content_rect, the box's interior), so
    // paint and hit agree about every pixel at any scroll offset. The surface
    // stays the panel's ground, the damage and the band's outer claim.
    const GuiRect content = folder_overlay::content_rect(app);
    cairo_rectangle(cr, content.x, content.y, content.w, content.h);
    cairo_clip(cr);

    // THE FACE: every content lists NAMES — files, projects and presets — and take the
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
            // NO FOCUS FRAME (architect 2026-10-23: "a highlight around the
            // button, which is more of a usability thing for the laptop, but
            // the laptop is deprecated as an authoring surface"): the lit row
            // is the list's one sign, focused or not — `list_focused` still
            // steers the keyboard's walk. The frame's one relief line in the
            // ring's light tone round the highlighted row, 2026-10-02 to
            // 2026-10-23, read as a bevel on the flat field.

            // THE ROW'S GLYPH (architect 2026-10-23: "use the logo in the
            // render player, but then we need a folder icon as well … more
            // squares to match the buttons"; "the same proportion as the
            // caption"): BY THE ROW'S KIND through the icon road — WavRow
            // (the quaver, Quaver.svg's link) on a wav row, FolderRow (the
            // square with its tab) on a folder or project row — a BOXED
            // GLYPH (icons.h's THE BOXED GLYPHS): its ink kListGlyphInkAt300
            // = 27 px, .525 of the 51-px row, so its box icon_box_px(27) (40
            // px at 300 %), the box's rows centered on the row (rounded
            // down), its left edge kListGlyphXAt300 = 18 px past the box's
            // outer left edge; the name at kListNameXAt300 = 64. THE PLAYING
            // RENDER'S ROW IS NOT MARKED (architect 2026-10-23: "the plain
            // renders row looks good now" — the clock and the scrub say
            // which item plays). (The paint_quaver vector of the morning, and
            // the icon set's glyphs at Windows' small 16-px seat 2026-08-28
            // to 2026-10-23, Play swapped in on the item's row, are git
            // history.)
            // A PRESET ROW WEARS NO GLYPH (architect 2026-10-25, the preset
            // picker: the row is the preset's display name) and its name
            // starts at the same kListNameXAt300 as every other row's, so
            // the picker's names stand in the project picker's name column.
            const int x0 = modal_box_rect(app, 0).x;
            if (row.kind != AppState::FolderOverlayRow::Kind::Preset) {
                const int gh = icons::icon_box_px(kListGlyphInkAt300);
                icons::draw_boxed(
                    cr,
                    row.kind == AppState::FolderOverlayRow::Kind::Wav
                        ? icons::Icon::WavRow
                        : icons::Icon::FolderRow,
                    x0 + skin_px(kListGlyphXAt300), r.y + (r.h - gh) / 2, gh);
            }
            const int text_x = x0 + skin_px(kListNameXAt300);

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
// (architect 2026-10-09, the third part, "accurate to the mock-up";
// program_spec.h's column_margin_px,
// render.h's canvas-frame accessors.) ONE PAINTER for the rows from the
// canvas's top frame row (top lane 5) through its bottom frame row (bottom
// lane 1, the program ring's last row since the simple layout, 2026-10-22;
// the column's 5-W foot under it until then): the panel's FACE across them —
// the margins beside the canvas and the frame rows' own ground — then THE
// CANVAS'S FRAME, A RING
// ROUND EACH CHANNEL'S WELL (Cool Edit's stereo divider, architect
// 2026-10-16; one ring round the waveform area 2026-10-09 to then): the
// `ce_dark` top row and left column, the `ce_hilight` right column and
// bottom row, MITRED at the top-right and bottom-left corner blocks where
// the dark meets the light (the mitre, cool_edit_paint.h's diagonal
// rule), the face row between the two rings running through the frame
// columns to the margins. It runs before render_canvas, which lays the canvas over the
// interior this face covered; the view bar's and the ruler's own side
// columns are their painters' (render_trim_flags, paint_ruler_row). THE
// SAME UNDER EVERY CHROME; the program's colors alone.
// UNDER THE GL CANVAS (2026-10-13, canvas_sink.h's transparency invariant)
// the face and the rings skip `hole`, the canvas's paint area, which
// on_redraw clears for the GPU; null on the cairo road, the face and the
// rings whole, render_canvas laying the area over them.
namespace {
void paint_canvas_column_frame(cairo_t* cr, const AppState& app,
                               const GuiRect* hole) {
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_canvas_frame_area(app);
    const GuiRect foot = bottom_canvas_frame_area(app);
    const int y1 = foot.y + foot.h;
    if (top.w <= 0 || y1 <= top.y) return;
    const GuiPalette& p = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // THE HI-FI SKIN (architect 2026-10-23; materials.h's REGIONS): the face
    // the brushed GROUND as drawn over its tile (materials::paint_rect — the
    // canvas interior none of it, the hole) and THE RINGS ONE DEVICE PX
    // (render.h's line rule) in the two ring tones. THE WELLS RUN OUT TO THE
    // HAIRLINES BECAUSE THE SLOT IS THE HAIRLINE (2026-10-28, render.h's
    // canvas-frame block): the frame rows and columns are one device px, so
    // the waveform area — what every canvas painter and hit test reads — is
    // the whole inside of the rings. NO SLOT FILL STANDS UNDER THE RINGS:
    // every cell of the rings' rect outside the area is a ring cell (the two
    // mitred rings cover their rows and columns opaquely, paint_hairline_frame
    // — the top-left run aliased over the whole top row and left column, the
    // bottom-right run over the rest) or the divider's face row below, and
    // inside the area render_canvas lays the canvas whole (the cairo road) or
    // the hole is left alone (the GL canvas).
    const GuiMaterialScope ground(GuiMaterialRegion::Ground);
    const GuiRect face{top.x, top.y, top.w, y1 - top.y};
    if (hole) {
        // THE HOLE IS LEFT ALONE WHOLE: the face and the rings below paint
        // under one clip of the column's rows less the hole, so the rings'
        // divider lines, which cross the area, write nothing inside it (the
        // GL canvas's known-clear record counts on that, on_redraw).
        for_each_part_outside(face, *hole, [&](const GuiRect& part) {
            cairo_rectangle(cr, part.x, part.y, part.w, part.h);
        });
        cairo_clip(cr);
    }
    materials::paint_rect(cr, face, GuiMaterial::Ground);
    // THE TWO WELLS' RINGS (the stereo divider, render.h's waveform_divider;
    // architect 2026-10-16): the upper from the top frame row through its
    // light bottom line, the divider's first row; the lower from its dark
    // top line, the divider's last row, through the bottom frame row; the
    // divider's middle row the ground's flat mean across the frame columns.
    // Inside the area the divider's rows are render_waveform_divider's
    // (through the canvas sink, over the canvas's ground and the plate,
    // on_redraw's paint order), so on the GL canvas the list draws them.
    const WaveformDivider d = waveform_divider(area.h);
    const ProgramRingColumns ring = program_ring_columns(app);
    const int ring_x = ring.x;
    const int ring_w = ring.end - ring.x;
    const int upper_end = area.y + d.y + d.line;        // past its light line
    const int lower_top = area.y + d.y + d.line + d.face;
    const GuiMaterialScope flat(GuiMaterialRegion::Flat);
    paint_hairline_frame(cr, GuiRect{ring_x, top.y, ring_w, upper_end - top.y},
                         p.ce_dark, p.ce_hilight);
    paint_hairline_frame(cr,
                         GuiRect{ring_x, lower_top, ring_w, y1 - lower_top},
                         p.ce_dark, p.ce_hilight);
    paint_cell_rect(cr, GuiRect{ring_x, upper_end, ring_w, d.face}, p.face);
    cairo_restore(cr);
}
} // namespace

// -- THE AS-PAINTED FINGERPRINTS AND THE OWED CHECK (2026-10-13) ----------
//
// The contract is at GuiUpperRowsFingerprint (app_state.h): a viewport step
// damages the viewport's lanes and owes the rows above them, and row 8's
// clock cell under the zoom appliers, a CHECK; this is the check and the
// two functions it compares with.

// WHETHER A FLOATING SURFACE MAY STAND OVER THE ROWS THE CHECK SPARES — the
// upper rows or the clock cell. Re-derived from on_redraw's paint sequence
// 2026-10-13: past the lane painters, the ground and the canvas column (each inside its own rows), the passes that paint OUTSIDE
// their lane are the floating surfaces — the notification cards (row 1
// down), the tooltip (under the pointer, or above its button) (the
// Settings dropdown, hanging from its button, until 2026-10-21). (The keyboard slot starts below the toolbar
// band's foot, and the folder overlay and the modal family's box of a prompt
// or a dialog editor at the view bar's top row, directly under the band
// (modal_box.h, 2026-10-14), and all yield the clock's row,
// GuiClockFingerprint::yields; the flag surface's blit over the top strip is
// transparent above the marker lane.) A floater's own publications ride a
// clip that covers it, so while one stands the check declares the rect the
// route declared before 2026-10-13, whole.
// THE GL CANVAS READS IT TOO (on_redraw's canvas block), for the pixels a
// floating surface puts over the canvas: the draw list's floater bit (the
// upload walk's) and the canvas paint area's known-clear record.
// EVERY TERM IS STATE THE INPUT WROTE, NEVER A RECT THE PAINT PUBLISHED
// (architect 2026-10-14): the canvas's bit is decided at the frame's first
// damage rect, BEFORE any pass paints, so a term read off a painter's
// publication lags a frame — on the frame a floater appears it would read
// empty, and the floater's pixels over the canvas would be cut out of the
// upload with nothing damaging them again. (The settings choice editor's
// dropped list was a fourth term, its own `list_open`, 2026-10-07 to
// 2026-10-14; it went with the list. THE MODAL FAMILY'S BOX IS NO FLOATER:
// it empties the canvas's paint area outright, waveform_paint_area — and
// while the score viewer's page stands its interior IS the GL frame's hole,
// the clip, which the box itself clears, 2026-10-27: the floater bit's two
// terms are the surfaces that can paint over it, as over the canvas.)
static bool floating_surface_stands(const AppState& app) {
    return app.redesign_tooltip.visible || !app.notifications.cards.empty();
}

// THE UPPER ROWS' LIVE FINGERPRINT — the ONE function the paint (publishing
// what it painted, note_upper_rows_painted) and the owed check (comparing,
// settle_owed_lane_damage) both call. EVERY INPUT THE ROWS' PIXELS READ,
// enumerated from their painters:
//   * the GROUND (render_background): the palette and the window's
//     geometry — the globals, with the scale every length and the one face
//     read;
//   * THE LOGO LANE (paint_logo_lane, the band's upper pad since
//     2026-10-24): the legend the tick composed (AppState::logo_legend);
//     the quaver, the brand, the ring's columns and the lane's rect are the
//     globals' and the band's, the shadow's color (`ce_cue_shadow`) and
//     offset the palette's and the scale's (2026-10-28);
//   * THE TOOLBAR BAND (paint_icon_row): its lane (the strip pad among the
//     lane's inputs, its rect moving with it); the `h` view; the standing
//     members in the walk's order and their runs (icon_row_walk, the right
//     run after them); per member the enabled and
//     selected bits (publish_button_face's stash, refreshed by a covering
//     clip), the pressed face (redesign_button_pressed_face, worn only
//     enabled) and the glyph index (redesign_button_glyph, the stateful
//     legend's fork); the cases' tones are
//     the palette's.
// NONE OF THEM IS A PAINT-TIME PUBLICATION of another surface or the
// displayed basis that on_redraw's head promotes (re-derived by a call-graph
// walk 2026-10-13: the roster predicates reach the promoted map only through
// row 8's Drop and Left / Right arms), so the fingerprint the pre-paint
// check computes is the one the frame paints.
GuiUpperRowsFingerprint GuiPaintHandler::upper_rows_fingerprint() {
    GuiUpperRowsFingerprint fp;
    fp.globals = GuiChromeGlobalsFingerprint{
        app.width, app.height, gui_scale_percent(), palette_generation()};

    fp.logo = app.logo_legend;

    fp.band.lane    = top_icon_row_area(app);
    fp.band.history = app.history_mode.active;
    const auto add = [&](RedesignButton id, int run) {
        assert(fp.band.count < kRedesignButtonCount);
        GuiIconRowMember& m =
            fp.band.members[static_cast<std::size_t>(fp.band.count++)];
        m.id       = id;
        m.run      = run;
        m.enabled  = redesign_button_enabled(app, audio, audio.total_frames(),
                                             playback, target_render, id);
        m.selected = redesign_button_selected(app, audio.total_frames(), id);
        m.pressed  = m.enabled && redesign_button_pressed_face(app, id);
        m.glyph    = redesign_button_glyph(app, id);
    };
    std::vector<std::vector<IconRowDef>> runs;
    std::vector<RedesignButton>          absent;
    icon_row_walk(app.history_mode.active, runs, absent);
    int run = 0;
    for (const std::vector<IconRowDef>& members : runs) {
        for (const IconRowDef& def : members) add(def.id, run);
        ++run;
    }
    for (const IconRowDef& def : kIconRowRightRun) add(def.id, run);
    return fp;
}

// THE CLOCK CELL'S LIVE FINGERPRINT — the one function the clock's painter
// (publishing) and the owed check (comparing) call. The cell's pixels,
// enumerated from paint_bottom_strip and paint_bottom_row_buttons_and_clock:
// the row content's face and the time field (the palette and the geometry,
// the globals), the cell as published (AppState::clock_cell_rect) and the
// run (clock_time_text: the scanner or the cursor, the sample rate) in the
// field's ink; and whether the row yields, when the cell is
// not the clock's at all (modal_owns_bottom_row, or the on-screen keyboard
// standing over the row).
GuiClockFingerprint GuiPaintHandler::clock_fingerprint() {
    GuiClockFingerprint fp;
    fp.globals = GuiChromeGlobalsFingerprint{
        app.width, app.height, gui_scale_percent(), palette_generation()};
    fp.cell      = app.clock_cell_rect;
    fp.time_text = clock_time_text(app, audio);
    fp.yields    = modal_owns_bottom_row(app) ||
                   onscreen_keyboard::stands(app, gui);
    return fp;
}

// THE UPPER ROWS' AS-PAINTED RECORD, kept by every on_redraw call on the
// target it paints (GuiUpperRowsRecord, app_state.h). The target is painting
// now, so what it was owed is paid: its debts clear here, whether or not
// this frame's rects reach the upper rows. A rect COVERING the rows repaints
// every pixel of them from the live inputs — each face whose clip covers it
// refreshes its stash live (publish_button_face) — so the record takes the
// live fingerprint; a rect reaching only part of them under inputs other
// than the record's leaves a mix of states, which only a repaint settles
// (the record goes invalid). Under the record's own inputs a partial repaint
// changes no pixel: every face's stash equals the record (a valid record's
// invariant, kept by this rule), and the live glyph and pressed face are the
// record's.
void GuiPaintHandler::note_upper_rows_painted(const GuiRect& exposed) {
    const std::size_t t = static_cast<std::size_t>(gui.painting_target());
    GuiUpperRowsRecord& rec = app.upper_rows_painted[t];
    rec.owed_y1 = 0;
    app.clock_painted[t].owed = false;
    const int top = std::min(viewport_lanes_top_y(app), app.height);
    if (top <= 0 || !rects_intersect(exposed, GuiRect{0, 0, app.width, top}))
        return;
    const bool covers = exposed.x <= 0 && exposed.y <= 0 &&
                        exposed.x + exposed.w >= app.width &&
                        exposed.y + exposed.h >= top;
    if (!covers && !rec.valid) return;
    const GuiUpperRowsFingerprint live = upper_rows_fingerprint();
    if (covers) {
        rec.painted = live;
        rec.valid   = true;
    } else if (!(live == rec.painted)) {
        rec.valid = false;
    }
}

// THE OWED CHECK, run at the END OF THE PRE-PAINT HOOK (main.cpp), the one
// seat that precedes every painted frame on both backends and may still add
// damage (paint_one_frame's rule): after the hook's own writes (the
// scanner's advance, Follow's page), and with nothing able to run between it
// and the paint. Each target that still owes is checked against what IT
// shows; any target whose record is invalid or differs from the live
// fingerprint, or a floating surface standing over the rows, declares the
// rect the routes declared before 2026-10-13 — the window top through the
// largest owed foot, the lanes below already damaged by the routes — and
// with it in every target's pending damage the debts are paid. A target
// that matches keeps nothing to repaint: its rows already show exactly what
// the repaint would put there. A frame that does not paint after the check
// (no free buffer, Wayland) keeps its debts, re-checked at the next hook.
// The clock cell the same, its rect clock_invalidate_rect's, a cell the row
// has not published (before the first paint, or under a yield) passing
// straight through as the lane.
void GuiPaintHandler::settle_owed_lane_damage() {
    static_assert(GuiPlatform::kPaintTargetCount <= kGuiPaintTargetSlots,
                  "one as-painted record per paint target");
    constexpr int n = GuiPlatform::kPaintTargetCount;
    const bool overlaid = floating_surface_stands(app);

    int owed_y1 = 0;
    for (int t = 0; t < n; ++t)
        owed_y1 = std::max(owed_y1, app.upper_rows_painted[t].owed_y1);
    if (owed_y1 > 0) {
        bool stale = overlaid;
        if (!stale) {
            const GuiUpperRowsFingerprint live = upper_rows_fingerprint();
            for (int t = 0; t < n && !stale; ++t) {
                const GuiUpperRowsRecord& rec = app.upper_rows_painted[t];
                stale = rec.owed_y1 > 0 &&
                        (!rec.valid || !(rec.painted == live));
            }
        }
        if (stale) {
            gui.invalidate_region(0, 0, app.width, owed_y1);
            for (int t = 0; t < n; ++t) app.upper_rows_painted[t].owed_y1 = 0;
        }
    }

    bool clock_owed = false;
    for (int t = 0; t < n; ++t) clock_owed |= app.clock_painted[t].owed;
    if (clock_owed) {
        const GuiRect cell = app.clock_cell_rect;
        bool stale = overlaid || cell.w <= 0 || cell.h <= 0;
        if (!stale) {
            const GuiClockFingerprint live = clock_fingerprint();
            for (int t = 0; t < n && !stale; ++t) {
                const GuiClockRecord& rec = app.clock_painted[t];
                stale = rec.owed && (!rec.valid || !(rec.painted == live));
            }
        }
        if (stale) {
            const GuiRect c = clock_invalidate_rect(app);
            gui.invalidate_region(c.x, c.y, c.w, c.h);
            for (int t = 0; t < n; ++t) app.clock_painted[t].owed = false;
        }
    }
}

// -- GuiPaintHandler::on_redraw ------------------------------------------

void GuiPaintHandler::on_redraw(cairo_t* cr, int x, int y, int w, int h) {
    // (THE MATERIALS FOR THIS GEOMETRY were settled before the backend's
    // paint walk began, at the pre-paint hook — main.cpp, materials.h's
    // BAKE RULE.)

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
        std::swap(app.marker_stem_runs, app.staged_marker_stem_runs);
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
    // construction one carrying the strip+waveform rect (since 2026-10-13 the
    // viewport lanes' rect from the band's foot, the rows above owed a
    // fingerprint check, damage_window_top_rows; every item-basis surface lies
    // below the band's foot). No input dispatches
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
    // verification round): the deferral OWES a strip+waveform repaint (the
    // viewport lanes' rect from the band's foot since 2026-10-13, the rows
    // above it owed a fingerprint check, damage_window_top_rows; the bit set
    // below), and a basis may not promote onto pixels the frame did not
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

    // THE UPPER ROWS' AS-PAINTED RECORD for the target this frame paints, and
    // its debts paid (note_upper_rows_painted; the contract at
    // GuiUpperRowsFingerprint, app_state.h) — before any pixel, on the state
    // every painter below reads.
    note_upper_rows_painted(GuiRect{x, y, w, h});

    // THE CANVAS'S TWO ROADS (2026-10-13; canvas_sink.h's head owns the rule):
    // on the cairo road every canvas pass below paints through `cairo_sink`
    // where it always painted; under the GL canvas (kGuiCanvasDrawList) the
    // canvas's paint area — the waveform area less the keyboard's slice, or
    // nothing under the modal family's box
    // (onscreen_keyboard::waveform_paint_area),
    // `canvas_paint` — is left to the GPU: the chrome ground and the
    // column's face skip it, one CLEAR empties it, no canvas pass paints on
    // the CPU, and the frame's draw list is built instead — ONCE PER FRAME,
    // by the first damage rect that meets that area, WHOLE (never cut to the
    // rect) from the state every pass reads, the later rects of the frame
    // skipping it (the flags reset at the pre-paint hook,
    // begin_canvas_frame). The list's clip and floater bit are written on the
    // frame's first rect whatever it meets, so the platform's scissor and
    // upload rule follow a paint area that shrank or emptied under no
    // canvas damage (the keyboard rising, the modal family's box standing).
    // THE SAME FIRST RECT PUBLISHES THE GPU PAGE'S REQUEST (architect
    // 2026-10-27, "1 a"; GuiCanvasPage, canvas_sink.h) and, while the score
    // viewer's page stands, makes THE BOX'S INTERIOR the clip: the canvas
    // paint area is empty then, the GPU draws the interior whole, and
    // paint_modal_box clears it — after the lanes that paint under the box —
    // so the known-clear record below stays the canvas's alone.
    GuiCairoCanvasSink cairo_sink(cr);
    const GuiRect exposed_rect{x, y, w, h};
    const GuiRect canvas_paint =
        onscreen_keyboard::waveform_paint_area(app, gui);
    const bool canvas_paint_whole = canvas_paint.w > 0 && canvas_paint.h > 0;
    const bool plate_branch = audio.total_frames() > 0 && !app.loading;
    if constexpr (kGuiCanvasDrawList) {
        GuiCanvasDrawList& frame = gui_canvas_frame();
        if (!canvas_frame_seen) {
            canvas_frame_seen = true;
            // THE GPU PAGE'S REQUEST (architect 2026-10-27, "1 a";
            // GuiCanvasPage, canvas_sink.h), and with it THE HOLE'S TENANT:
            // the canvas paint area, or — while the score viewer's page
            // stands, the box having emptied the canvas paint area — the
            // box's interior, which the GPU draws whole and paint_modal_box
            // clears (canvas_sink.h's THE HOLE IS ONE RECT).
            score_viewer_publish_gpu_page(app, frame.page);
            const GuiRect page_hole =
                frame.page.stands ? modal_box_interior_rect(app, 0)
                                  : GuiRect{0, 0, 0, 0};
            assert(!(canvas_paint_whole && frame.page.stands));
            frame.clip    = canvas_paint_whole ? canvas_paint : page_hole;
            frame.floater = floating_surface_stands(app);
            gui.set_canvas_frame(&frame);
            // THE KNOWN-CLEAR RECORD'S FOLD AND THE FRAME'S DECISION (the
            // rule at the CLEAR below): the last painted frame's summary
            // becomes the record, and this frame skips its clears only on a
            // known-clear area it shares with that frame, no floating
            // surface standing.
            if (canvas_clear_open)
                canvas_clear_known = !canvas_clear_floater &&
                                     (canvas_clear_skip ||
                                      canvas_clear_covered);
            canvas_clear_open = true;
            canvas_clear_skip = canvas_clear_known && canvas_paint_whole &&
                                canvas_paint == canvas_clear_area &&
                                !frame.floater;
            canvas_clear_area    = canvas_paint_whole ? canvas_paint
                                                      : GuiRect{0, 0, 0, 0};
            canvas_clear_covered = false;
            canvas_clear_floater = frame.floater;
        }
        if (!canvas_list_built && canvas_paint_whole &&
            std::max(x, canvas_paint.x) <
                std::min(x + w, canvas_paint.x + canvas_paint.w) &&
            std::max(y, canvas_paint.y) <
                std::min(y + h, canvas_paint.y + canvas_paint.h)) {
            canvas_list_built = true;
            build_canvas_list(frame, plate_branch);
        }
    }

    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);

    if constexpr (kGuiCanvasDrawList) {
        for_each_part_outside(exposed_rect, canvas_paint,
                              [&](const GuiRect& part) {
                                  render_background(cr, part.x, part.y,
                                                    part.w, part.h);
                              });
    } else {
        render_background(cr, x, y, w, h);
    }
    // THE GROUND SPLIT: the chrome erase above covers the whole exposed rect;
    // the canvas's frame and the column's margins then take the panel's face
    // and the two wells' rings (paint_canvas_column_frame, above), and the
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
    // over the canvas and its grid, under the same keyboard clip — and THE
    // STEREO DIVIDER LIKEWISE (render_waveform_divider, after the center
    // lines on both branches: over the plate where one stands, its reason at
    // its definition, render.cpp).
    // UNDER THE GL CANVAS the CPU leaves all of it to the GPU (the block
    // above): the face skips the canvas's paint area and one CLEAR empties
    // it, so its pixels are alpha 0 until an overlay paints over them
    // (canvas_sink.h's transparency invariant).
    //
    // THE CLEAR OF AN ALREADY-CLEAR AREA IS SKIPPED (2026-10-14): the GUI
    // keeps, in the paint handler and never from the platform's upload
    // state, whether the backbuffer's canvas paint area is KNOWN CLEAR
    // (canvas_clear_known, paint_handler.h), and a frame whose area is
    // known clear clears nothing — its rects' clears would write 0 on
    // pixels already 0, so every byte is the clearing frame's.
    //   * KNOWN CLEAR after a painted frame that stood no floating surface
    //     (floating_surface_stands at the frame's first rect, the frame's
    //     floater bit — its terms are live state, which no pass of the
    //     frame writes) and whose area was clear at its end: a rect of
    //     it covered the area whole (its clear, or its skip, left every
    //     pixel 0), or the frame skipped (it began known clear). Nothing
    //     else in a frame writes inside the area: THE PAINTERS OVER THE
    //     WELL, re-derived from the passes below (2026-10-14), are the cards
    //     (paint_notifications) and the
    //     tooltip (paint_shift_tooltip) — the floater bit's two (the
    //     pull-downs a third until 2026-10-21); the
    //     keyboard alone (paint_keyboard_slot) stands below the area's foot,
    //     which waveform_paint_area cuts at its band's top; the modal
    //     family's box — the folder overlay's (paint_keyboard_slot) and a
    //     prompt's or a dialog editor's (paint_modal_dialog), with the
    //     keyboard under a dialog editor's stack — empties the area (nothing
    //     is cleared or skipped under it); the column's face and its two
    //     wells' rings above skip the area (its hole, a clip);
    //     every other pass keeps to its own lane (the band and row 8;
    //     paint_trim, the ruler and the flag blit in the top strip; the flag
    //     editor's field in the marker lane). Nothing writes the backbuffer
    //     between frames (the
    //     platform paints only through this pass).
    //   * UNKNOWN — the frame clears as before — when a floating surface
    //     stands now or stood in the last painted frame, when the area
    //     differs from the last painted frame's (the keyboard or the modal
    //     family's box came or went), at the first frame (a
    //     project set's paint handler starts unknown, the backbuffer being
    //     the process's), and after any on_resize (on_resize forgets the
    //     record): the backbuffer is remade only at a size it does not
    //     have, and the client area moves inside it only with the frame,
    //     both platforms firing the resize for either; an Android window
    //     adoption that moves nothing keeps the backbuffer and its bytes.
    // The cairo road clears nothing and keeps no record.
    if constexpr (kGuiCanvasDrawList) {
        paint_canvas_column_frame(cr, app, &canvas_paint);
        if (canvas_paint_whole) {
            if (!canvas_clear_skip) {
                cairo_save(cr);
                cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
                cairo_rectangle(cr, canvas_paint.x, canvas_paint.y,
                                canvas_paint.w, canvas_paint.h);
                cairo_fill(cr);
                cairo_restore(cr);
            }
            if (x <= canvas_paint.x && y <= canvas_paint.y &&
                x + w >= canvas_paint.x + canvas_paint.w &&
                y + h >= canvas_paint.y + canvas_paint.h)
                canvas_clear_covered = true;
        }
    } else {
        paint_canvas_column_frame(cr, app, nullptr);
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
        render_canvas(cairo_sink, canvas);
        if (!plate_branch) {
            render_center_lines(cairo_sink, canvas);
            render_waveform_divider(cairo_sink, canvas);
        }
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
    // top button lanes (the flag cache is transparent over them and lands
    // only on its own band, paint_flag_annotations; every other pass owns a
    // lane below them), so painting them first overdraws nothing.
    // GAP 1 has no painter: it is window ground, render_background's erase
    // above (main.cpp's vertical rule).
    //
    // THE BOTTOM ROW JOINS THEM (row 7): it is audio-independent in the same
    // sense — with no source the clock's field is empty and no tab is named
    // (the open's hold's one frame, platform.h's head). Nothing painted
    // later overlaps it except the floating surfaces, which paint over
    // everything by design.
    {
        const GuiRect exposed{x, y, w, h};
        // THE CHROME'S PAINT TIME (a throwaway trace for the planner,
        // 2026-10-21; materials.cpp's bake line the other): one stderr line
        // per full-window paint of the lanes below, behind
        // materials::trace_enabled — WT_PAINT_TRACE=1, or on the tablet the
        // system property debug.warptempo.trace=1, its stderr logcat.
        static const bool trace = materials::trace_enabled();
        const auto trace_t0 = std::chrono::steady_clock::now();
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
        if (trace && x == 0 && y == 0 && w >= app.width && h >= app.height)
            std::fprintf(stderr, "warptempo_gui: chrome paint %.3f ms\n",
                         std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - trace_t0)
                             .count());
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
        // branch. UNDER THE GL CANVAS (2026-10-13, canvas_sink.h) the canvas
        // interior's share of it — step 2's canvas and grid (and center
        // lines and divider without audio), step 4, steps 6 to 8 and step 11 — is the
        // frame's draw list in this same order, drawn by the GPU under
        // everything the CPU paints (build_canvas_list); the CPU's share
        // leaves that interior alpha 0:
        //   1. render_background — the chrome erase over the whole exposed
        //      rect (above, unconditional).
        //   2. paint_canvas_column_frame — the panel's face from the canvas's
        //      top frame row through its bottom frame row and the two wells'
        //      rings — then render_canvas — the waveform area's ground and
        //      the horizontal grid (above, unconditional; 2026-10-09; no
        //      vertical grid, render.h's row-6 canvas paragraph) — and, on a
        //      frame with no audio loaded (no source, or a load running:
        //      !plate_branch), THE CENTER LINES straight over it
        //      (render_center_lines), so the grid never stands without them,
        //      then THE STEREO DIVIDER'S ROWS (render_waveform_divider,
        //      2026-10-16).
        //   3. the toolbar band and the unified bottom row (its
        //      chrome, buttons, clock, the tab's name AND state cell in one
        //      painter),
        //      each on its own
        //      exposure (above, outside this branch; they own lanes nothing
        //      below them paints on). (THE PROGRAM'S FRAME, paint_program_frame,
        //      2026-10-10 to 2026-10-22, is retired.)
        //   4. waveform plate -> THE CENTER LINES over the ink
        //      (render_center_lines, architect 2026-10-09 evening; with audio
        //      loaded they paint here and not at step 2, a null plate's frame
        //      included) -> THE STEREO DIVIDER'S ROWS (render_waveform_divider,
        //      2026-10-16; likewise here and not at step 2 with audio loaded,
        //      OVER THE PLATE so a plate from before a resize, standing at its
        //      old bands until the worker republishes, never crosses the live
        //      divider — in the steady state nothing paints there, so the
        //      order changes no pixel) -> phase-reset overlay ring.
        //   5. LIVE TRIM, one pass, entirely inside the trim lane: Cool
        //      Edit's view bar and its span (2026-10-09; no cursor mark in
        //      the bar, ~14:30).
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
        //      EVERYTHING FROM THE VIEW BAR'S TOP ROW TO ROW 8'S TOP (the
        //      overlay, the modal family's box since 2026-10-14, modal_box.h
        //      — so of steps 3 through 11 only the icon row's lane, the
        //      band greyed but for what it leaves live, and
        //      row 8's rows under the controls stay in view) and so follows
        //      every pass above.
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
        //      panel stands. Both tenants stand inside the program's frame
        //      (2026-10-14; over its columns and its bottom row from
        //      2026-10-10 to then).
        //  13. the MODAL DIALOG (paint_modal_dialog, 2026-08-12; since
        //      2026-10-14 here, the modal family's box for a prompt or a
        //      dialog editor over everything from the view bar's top row
        //      down, which the waveform's painted rect leaves out, and every
        //      modal's controls row; under the GL canvas the score viewer's
        //      interior CLEARED there, the GPU's page hole, 2026-10-27), then
        //      the flag editor's box, then THE
        //      NOTIFICATION CARDS (paint_notifications, 2026-08-29 — the
        //      top-right stack, above every lane, the keyboard slot and the
        //      modal's box) — the floating surfaces, after
        //      every pass above and outside this branch — and LAST the
        //      TOOLTIP, which reads the modal's stash.
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

        // THE CANVAS PASSES BELOW PAINT ON THE CAIRO ROAD ALONE: under the GL
        // canvas the frame's list carries them, built once, whole, in this
        // order (build_canvas_list).
        if (!kGuiCanvasDrawList && rects_intersect(exposed, wave_paint)) {
            paint_waveform_plate(cairo_sink, area);
            // THE CENTER LINES OVER THE INK (architect 2026-10-09 evening;
            // render_center_lines, render.h's row-6 canvas paragraph): each
            // channel's zero row, one device px in `center`, right after the
            // blit and under every dot, ring and line that follows.
            render_center_lines(cairo_sink, area);
            // THE STEREO DIVIDER OVER THE PLATE (2026-10-16, its reason at
            // render_waveform_divider, render.cpp): the live rows on top
            // whatever plate stands.
            render_waveform_divider(cairo_sink, area);
            // The overlay band's dotted ring — the phase-reset overlay's whole
            // visual — over the plate and under the stems and the cursor's
            // dots: its left side is the focused reset's own stem, which
            // paint_marker_stems lays down, the playhead's dots interleaving
            // there as on any stem.
            paint_phase_reset_overlay_ring(cairo_sink, area);
        }

        // LIVE TRIM PASS — the old trim-stem-cache slot, now covering ALL trim
        // pixels (the view bar whole).
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
        if (!kGuiCanvasDrawList && rects_intersect(exposed, wave_paint)) {
            paint_marker_stems(cairo_sink, area);
        }

        // The CURSOR'S CANVAS DOTS AFTER THE MARKER STEMS (the
        // playhead-over-stems ruling, architect 2026-09-23). Everything laid
        // down before it — the plate, the phase-reset overlay ring — stays
        // under it as before. (The scanner paints after it, below.) Its head
        // is the ruler pass's and its view-bar dots the trim pass's, so this
        // pass is the waveform's alone.
        if (!kGuiCanvasDrawList && rects_intersect(exposed, wave_paint)) {
            paint_playheads(cairo_sink, area);
        }

        // THE SCANNER LAST OF THE WAVEFORM VERTICALS (architect 2026-08-01):
        // the moving line paints AFTER the stems, so it crosses them instead of
        // being erased column by column as it sweeps past every marker. Only the
        // scanner moved then; since 2026-09-23 the cursor paints over the stems
        // too, just before this pass, and stays under the flags. Where the two
        // playheads meet the scanner is on top. Waveform-only, so no top_strip
        // arm.
        if (!kGuiCanvasDrawList && rects_intersect(exposed, wave_paint)) {
            paint_scanner(cairo_sink, area);
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
        if (!kGuiCanvasDrawList && rects_intersect(exposed, wave_paint)) {
            paint_strip_drag_anchor(cairo_sink, area);
        }

        if (band_cuts) cairo_restore(cr);
    }

    // THE KEYBOARD SLOT (the on-screen keyboard since 2026-08-27, the folder
    // overlay since 2026-08-28 — one band, one tenant at a time), between the
    // waveform passes above and the three floating surfaces below — which is
    // exactly where it sits in the picture. It OVERLAYS the waveform with its
    // own opaque ground — the area's lower part under the keyboard, and the
    // MODAL FAMILY'S PANEL under the overlay since 2026-10-14 (the view bar's
    // top row down to the controls row, modal_box.h) — so it must follow
    // every pass that paints there
    // (the top button rows, the plate, the
    // trim, the ruler, the flags, the stems, the scanner, the anchor) and
    // precede the modal and the flag editor's box, which float
    // over it by design. It is OUTSIDE the loading /
    // total>0 branch above for the flag editor's own reason: an editor can
    // stand with no audio loaded, and the keyboard must stand with it.
    //
    // NOT exposure-gated: the dispatch's own gate is the tenants' standing
    // predicates, and it must run on EVERY frame class to write the
    // as-painted bit the slot comparator reads (the contract is at the
    // declaration). With the slot down that is one platform query and two
    // bit reads; with it up the outer Cairo clip makes a narrow damage cheap.
    paint_keyboard_slot(cr, GuiRect{x, y, w, h});

    // THE MODAL DIALOG PAINTS NEXT (2026-08-12; here since 2026-10-14): a
    // prompt's and a dialog editor's box over the canvas column and every
    // modal's controls row — after the keyboard slot, on whose band a dialog
    // editor's stack stands, and BELOW THE FLOATING SURFACES, so a refusal's
    // notification card hangs over the box it answers (the cards' press
    // claim already ranked above the modal, notification_card_at). It cannot
    // coexist with the flag editor (ended by any dialog's open). UNCONDITIONAL for the floating surfaces'
    // own reason: it publishes the geometry the pointer path reads
    // (AppState::modal_dialog), and a run that skipped would strand a stale
    // box. (From 2026-08-12 it painted after the dropdown, last but the
    // tooltip, the modal on the bottom row below every floater's reach; the
    // taskbar overlay's line and ground, paint_bottom_overlays, stood between
    // the slot and the floaters 2026-10-10 to 2026-10-14.)
    paint_modal_dialog(cr);

    // THE FLOATING SURFACES PAINT TOPMOST — after EVERY pass above, including
    // the waveform, because they hang outside their strips (the cards from
    // the band's foot down, the tooltip under the pointer) and overlap whatever is
    // under them. They are NOT exposure-gated the way the rows are: each
    // writes the rect it painted (or a zero rect) on every run, and a run that
    // skipped would strand a stale rect for the hit tests and the damage to
    // read. Hidden, each costs one boolean. THE NOTIFICATION CARDS (2026-08-29)
    // publish what they drew on every run and paint BELOW the tooltip and
    // ABOVE the modal (2026-10-14, the modal's block above): a hint the
    // pointer just raised belongs on top of a message, and a message on top
    // of the box it answers. (The Settings dropdown painted between the cards
    // and the tooltip until it retired, 2026-10-21.)
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
    // THE NOTIFICATION CARDS (2026-08-29) paint after the flag editor's box:
    // above every lane, the waveform, the keyboard slot, the folder overlay's
    // band and the modal family's box (2026-10-14), below the tooltip, the
    // pointer-transient floater. Unconditional for the floating surfaces'
    // reason: it publishes the geometry the pointer path reads.
    paint_notifications(cr);

    // THE TOOLTIP IS LAST OF ALL since 2026-08-13, when the MODAL's buttons
    // took hints of their own: this body READS the modal stash
    // paint_modal_dialog publishes — the hovered dialog button's composed
    // hint and its rect — so
    // running after it is what makes the hint hang off the box THIS frame
    // draws rather than the previous one's. It also puts the one surface that
    // floats OUT of its lane on top of everything, which is what a tooltip is.
    paint_shift_tooltip(cr);

    cairo_restore(cr);

    // The target here IS memory something outside cairo will read: under the
    // Wayland cpu canvas each buffer's surface is a cairo IMAGE surface
    // created over the mmap'd wl_shm pool (GuiPlatform::recreate_shm_pool),
    // whose pages are shared with the compositor; under every other road it
    // is the backend's persistent backbuffer, which the present uploads into
    // the GL texture or copies into the window's buffer. Flushing is cairo's
    // handshake before anything outside cairo reads that memory — it lands
    // whatever the backend is still holding in internal state, so the backing
    // bytes are complete. On the shm road this is the last point where that
    // can happen: nothing between here and the publish flushes the surface
    // again (destroying the context ends the drawing, not the surface, which
    // outlives every frame with the pool), and the very next thing
    // GuiPlatform::paint_one_frame does after the paint loop is
    // wl_surface_attach + wl_surface_commit (the backbuffer roads flush the
    // surface once more themselves before their present).
    cairo_surface_flush(cairo_get_target(cr));
}

// -- THE GL CANVAS'S FRAME (2026-10-13; canvas_sink.h owns the rule) -----
//
// The frame's draw list, built by on_redraw's first damage rect that meets
// the canvas's paint area: the canvas passes in on_redraw's paint order
// (its authoritative block), each through the list sink, WHOLE — no damage
// rect cuts it and no exposure gates a pass — so the GPU redraws the
// canvas entire at every swap from one coherent state.
void GuiPaintHandler::build_canvas_list(GuiCanvasDrawList& frame,
                                        bool plate_branch) {
    GuiListCanvasSink sink(frame);
    const GuiRect area = waveform_area(app);
    render_canvas(sink, area);
    if (!plate_branch) {
        render_center_lines(sink, area);
        render_waveform_divider(sink, area);
        return;
    }
    paint_waveform_plate(sink, area);
    render_center_lines(sink, area);
    render_waveform_divider(sink, area);
    paint_phase_reset_overlay_ring(sink, area);
    paint_marker_stems(sink, area);
    paint_playheads(sink, area);
    paint_scanner(sink, area);
    paint_strip_drag_anchor(sink, area);
}

void GuiPaintHandler::begin_canvas_frame() {
    canvas_frame_seen = false;
    canvas_list_built = false;
}

// -- GuiPaintHandler::on_resize ------------------------------------------

void GuiPaintHandler::on_resize(int w, int h) {
    app.width  = w;
    app.height = h;
    // THE MATERIALS ARE NOT BAKED HERE (materials.h's BAKE RULE, the settle
    // gate, 2026-10-21): a frame drag configures at every motion, so a resize
    // only stamps its arrival, and the paint bakes once the size has held.
    materials::note_resize();
    // THE CANVAS'S KNOWN-CLEAR RECORD IS FORGOTTEN (2026-10-14; the rule at
    // on_redraw's CLEAR): the backbuffer may be new, or the client area moved
    // inside it, so the next frame clears.
    if constexpr (kGuiCanvasDrawList) {
        canvas_clear_known = false;
        canvas_clear_open  = false;
    }
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
